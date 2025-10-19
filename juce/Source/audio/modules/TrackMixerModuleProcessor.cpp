#include "TrackMixerModuleProcessor.h"
#include <cmath>

TrackMixerModuleProcessor::TrackMixerModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::discreteChannels(MAX_TRACKS), true)
          .withInput("Mod In", juce::AudioChannelSet::discreteChannels(1 + (MAX_TRACKS * 2)), true) // 1 for NumTracks + 2 for each track (Gain/Pan)
          .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "TrackMixerParams", createParameterLayout())
{
    numTracksParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numTracks"));
    numTracksMaxParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("numTracks_max"));

    trackGainParams.resize(MAX_TRACKS);
    trackPanParams.resize(MAX_TRACKS);
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        trackGainParams[i] = apvts.getRawParameterValue("track_gain_" + juce::String(i + 1));
        trackPanParams[i]  = apvts.getRawParameterValue("track_pan_" + juce::String(i + 1));
    }
  // Initialize effective track count for UI
  if (numTracksParam != nullptr)
      lastActiveTracks.store(numTracksParam->get());
}

juce::AudioProcessorValueTreeState::ParameterLayout TrackMixerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterInt>("numTracks", "Num Tracks", 2, MAX_TRACKS, 8));
    p.push_back(std::make_unique<juce::AudioParameterInt>("numTracks_max", "Num Tracks Max", 2, MAX_TRACKS, MAX_TRACKS));

    for (int i = 1; i <= MAX_TRACKS; ++i)
    {
        p.push_back(std::make_unique<juce::AudioParameterFloat>("track_gain_" + juce::String(i),
            "Track " + juce::String(i) + " Gain",
            juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), 0.0f));
        p.push_back(std::make_unique<juce::AudioParameterFloat>("track_pan_" + juce::String(i),
            "Track " + juce::String(i) + " Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    }
    return { p.begin(), p.end() };
}

void TrackMixerModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void TrackMixerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto audioInBus = getBusBuffer(buffer, true, 0);
    auto modInBus   = getBusBuffer(buffer, true, 1);
    auto outBus     = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    
    // Determine the number of active tracks from the parameter or its modulation input
    int numTracks = numTracksParam->get();
    
    if (isParamInputConnected(paramIdNumTracksMod))
    {
        float modValue = modInBus.getReadPointer(0)[0];

        // Interpret modValue as a raw track count (not normalized CV)
        int maxTracks = numTracksMaxParam->get();
        numTracks = juce::roundToInt(modValue); // Round the raw value to the nearest integer
        numTracks = juce::jlimit(2, maxTracks, numTracks); // Clamp it to the valid range
    }

    // Publish live value for UI and cache for drawing pins/controls
    lastActiveTracks.store(juce::jlimit(2, MAX_TRACKS, numTracks));
    setLiveParamValue("numTracks_live", (float) numTracks);

    juce::AudioBuffer<float> mixBus(2, numSamples);
    mixBus.clear();

    // Loop through every active track and add its sound to the mix
    for (int t = 0; t < numTracks; ++t)
    {
        const float* src = (t < audioInBus.getNumChannels()) ? audioInBus.getReadPointer(t) : nullptr;
        if (src == nullptr) continue;

        float* mixL = mixBus.getWritePointer(0);
        float* mixR = mixBus.getWritePointer(1);

        const juce::String trackNumStr = juce::String(t + 1);

        const bool isGainModulated = isParamInputConnected(paramIdGainModPrefix + trackNumStr);
        const bool isPanModulated  = isParamInputConnected(paramIdPanModPrefix + trackNumStr);

        if (!isGainModulated && !isPanModulated)
        {
            // Optimized path for non-modulated tracks
            const float gainDb = trackGainParams[t]->load();
            const float panVal = trackPanParams[t]->load();
            const float gainLin = juce::Decibels::decibelsToGain(gainDb);
            const float angle = (panVal * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
            const float lMul = gainLin * std::cos(angle);
            const float rMul = gainLin * std::sin(angle);
            juce::FloatVectorOperations::addWithMultiply(mixL, src, lMul, numSamples);
            juce::FloatVectorOperations::addWithMultiply(mixR, src, rMul, numSamples);
        }
        else // Per-sample processing is needed if either gain or pan is modulated
        {
            const float baseGainDb = trackGainParams[t]->load();
            const float basePan = trackPanParams[t]->load();
            const float* gainModSignal = isGainModulated ? modInBus.getReadPointer(1 + t * 2) : nullptr;
            const float* panModSignal  = isPanModulated ? modInBus.getReadPointer(1 + t * 2 + 1) : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                float currentGainDb = isGainModulated && gainModSignal ? juce::jmap(gainModSignal[i], 0.0f, 1.0f, -60.0f, 6.0f) : baseGainDb;
                float currentPan    = isPanModulated && panModSignal ? juce::jmap(panModSignal[i], 0.0f, 1.0f, -1.0f, 1.0f) : basePan;
                
                const float gainLin = juce::Decibels::decibelsToGain(currentGainDb);
                const float angle = (currentPan * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
                const float lMul = gainLin * std::cos(angle);
                const float rMul = gainLin * std::sin(angle);
                
                mixL[i] += src[i] * lMul;
                mixR[i] += src[i] * rMul;
                
                // Store live values for UI telemetry (update every 64 samples to avoid overhead)
                if ((i & 0x3F) == 0)
                {
                    if (isGainModulated) setLiveParamValue("track_gain_" + trackNumStr + "_live", currentGainDb);
                    if (isPanModulated) setLiveParamValue("track_pan_" + trackNumStr + "_live", currentPan);
                }
            }
        }
    }
    
    // Copy the final mixed signal to the output
    outBus.copyFrom(0, 0, mixBus, 0, 0, numSamples);
    if (outBus.getNumChannels() > 1)
        outBus.copyFrom(1, 0, mixBus, 1, 0, numSamples);
}

int TrackMixerModuleProcessor::getEffectiveNumTracks() const
{
    return numTracksParam ? numTracksParam->get() : 8;
}

void TrackMixerModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TrackMixerModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

#if defined(PRESET_CREATOR_UI)
void TrackMixerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const int activeTracks = getEffectiveNumTracks();

    // --- Master "Tracks" Slider with correct modulation detection ---
    const bool isCountModulated = isParamModulated(paramIdNumTracksMod);
    int displayedTracks = numTracksParam->get();
    // If modulated, show the live computed value in the disabled slider
    if (isCountModulated)
        displayedTracks = juce::roundToInt(getLiveParamValueFor(paramIdNumTracksMod, "numTracks_live", (float) displayedTracks));
    const int maxTracksBound = numTracksMaxParam->get();

    if (isCountModulated) ImGui::BeginDisabled();
    
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::SliderInt("Tracks", &displayedTracks, 2, maxTracksBound))
    {
        if (!isCountModulated)
        {
            *numTracksParam = displayedTracks;
            onModificationEnded();
        }
    }
    if (!isCountModulated)
    {
        adjustParamOnWheel(ap.getParameter("numTracks"), "numTracks", (float)displayedTracks);
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !isCountModulated) { onModificationEnded(); }
    ImGui::PopItemWidth();

    if (isCountModulated)
    {
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted("(mod)");
    }


    // --- Per-Track Sliders (Dynamically created for all active tracks) ---
    // --- FIX: Use the 'displayedTracks' variable here, which respects modulation ---
    for (int t = 0; t < displayedTracks; ++t)
    {
        ImGui::PushID(t);
        const juce::String trackNumStr = juce::String(t + 1);
        
        auto* gainParamPtr = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("track_gain_" + trackNumStr));
        auto* panParamPtr  = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("track_pan_" + trackNumStr));

        if (!gainParamPtr || !panParamPtr)
        {
            ImGui::PopID();
            continue;
        }

        // --- Gain Slider for Track t+1 ---
        const bool isGainModulated = isParamModulated("track_gain_" + trackNumStr);
        float gainVal = gainParamPtr->get(); // Get base value
        if (isGainModulated)
        {
            // If modulated, show the live computed value
            gainVal = getLiveParamValueFor("track_gain_" + trackNumStr, "track_gain_" + trackNumStr + "_live", gainVal);
            ImGui::BeginDisabled();
        }

        ImGui::PushItemWidth(itemWidth * 0.5f - 20); // Adjust width for mod indicator
        if (ImGui::SliderFloat(("G" + trackNumStr).toRawUTF8(), &gainVal, -60.0f, 6.0f, "%.1f dB"))
        {
            if (!isGainModulated) *gainParamPtr = gainVal;
        }
        if (!isGainModulated) adjustParamOnWheel(gainParamPtr, "gain", gainVal);
        if (ImGui::IsItemDeactivatedAfterEdit() && !isGainModulated) { onModificationEnded(); }
        ImGui::PopItemWidth();

        if (isGainModulated)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted("(m)");
        }

        ImGui::SameLine();

        // --- Pan Slider for Track t+1 ---
        const bool isPanModulated = isParamModulated("track_pan_" + trackNumStr);
        float panVal  = panParamPtr->get(); // Get base value
        if (isPanModulated)
        {
            // If modulated, show the live computed value
            panVal = getLiveParamValueFor("track_pan_" + trackNumStr, "track_pan_" + trackNumStr + "_live", panVal);
            ImGui::BeginDisabled();
        }
        
        ImGui::PushItemWidth(itemWidth * 0.5f - 20); // Adjust width for mod indicator
        if (ImGui::SliderFloat(("P" + trackNumStr).toRawUTF8(), &panVal, -1.0f, 1.0f, "%.2f"))
        {
            if (!isPanModulated) *panParamPtr = panVal;
        }
        if (!isPanModulated) adjustParamOnWheel(panParamPtr, "pan", panVal);
        if (ImGui::IsItemDeactivatedAfterEdit() && !isPanModulated) { onModificationEnded(); }
        ImGui::PopItemWidth();

        if (isPanModulated)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextUnformatted("(m)");
        }
        
        ImGui::PopID();
    }
}
#endif

// Human-legible per-channel labels for the single multichannel input bus
juce::String TrackMixerModuleProcessor::getAudioInputLabel(int channel) const
{
    // Channel names mirror visual controls: Audio N, Num Tracks Mod, Gain N Mod, Pan N Mod
    // Audio inputs occupy channels [0 .. MAX_TRACKS-1]
    if (channel >= 0 && channel < MAX_TRACKS)
        return "Audio " + juce::String(channel + 1);
    // Mod lanes begin after audio inputs
    const int modBase = MAX_TRACKS; // conceptual; we expose labels by absolute channel
    // For labeling, we follow the param routing: 0: numTracks, then pairs for each track
    const int idx = channel - MAX_TRACKS;
    if (idx == 0) return "Num Tracks Mod";
    if (idx > 0)
    {
        const int pair = (idx - 1) / 2; // 0-based track
        const bool isGain = ((idx - 1) % 2) == 0;
        if (pair >= 0 && pair < MAX_TRACKS)
            return juce::String(isGain ? "Gain " : "Pan ") + juce::String(pair + 1) + " Mod";
    }
    return {};
}

void TrackMixerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Use the last value computed on audio thread if available
    const int activeTracks = juce::jlimit(2, MAX_TRACKS, lastActiveTracks.load());

    // --- Draw Output Pins First ---
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);

    // --- Draw Input Pins ---
    // Replace generic bus pins with human-legible per-channel pins
    for (int t = 0; t < activeTracks; ++t)
        helpers.drawAudioInputPin(("Audio " + juce::String(t + 1)).toRawUTF8(), t);

    // --- Draw Modulation Pins ---
    int busIdx, chanInBus;
    if (getParamRouting(paramIdNumTracksMod, busIdx, chanInBus))
        helpers.drawAudioInputPin("Num Tracks Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));

    // Draw per-track modulation pins
    for (int t = 1; t <= activeTracks; ++t)
    {
        const juce::String trackNumStr = juce::String(t);
        const juce::String gainModId = paramIdGainModPrefix + trackNumStr;
        const juce::String panModId = paramIdPanModPrefix + trackNumStr;

        if (getParamRouting(gainModId, busIdx, chanInBus))
            helpers.drawAudioInputPin(("Gain " + trackNumStr + " Mod").toRawUTF8(), getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
        if (getParamRouting(panModId, busIdx, chanInBus))
            helpers.drawAudioInputPin(("Pan " + trackNumStr + " Mod").toRawUTF8(), getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    }
}

bool TrackMixerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    // All modulation is on Bus 1, "Mod In".
    outBusIndex = 1;

    if (paramId == paramIdNumTracksMod)
    {
        outChannelIndexInBus = 0; // First channel in mod bus
        return true;
    }

    if (paramId.startsWith(paramIdGainModPrefix))
    {
        const juce::String trackNumStr = paramId.substring(juce::String(paramIdGainModPrefix).length());
        const int trackNum = trackNumStr.getIntValue();
        if (trackNum > 0 && trackNum <= MAX_TRACKS)
        {
            outChannelIndexInBus = 1 + (trackNum - 1) * 2; // Channels 1, 3, 5...
            return true;
        }
    }
    else if (paramId.startsWith(paramIdPanModPrefix))
    {
        const juce::String trackNumStr = paramId.substring(juce::String(paramIdPanModPrefix).length());
        const int trackNum = trackNumStr.getIntValue();
        if (trackNum > 0 && trackNum <= MAX_TRACKS)
        {
            outChannelIndexInBus = 1 + (trackNum - 1) * 2 + 1; // Channels 2, 4, 6...
            return true;
        }
    }
    return false;
}