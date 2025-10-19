#include "TimePitchModuleProcessor.h"

static inline void ensureCapacity (juce::HeapBlock<float>& block, int frames, int channels, int& capacityFrames)
{
    if (frames > capacityFrames)
    {
        capacityFrames = juce::jmax (frames, capacityFrames * 2 + 128);
        block.allocate ((size_t) (capacityFrames * channels), true);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout TimePitchModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdSpeed, "Speed", juce::NormalisableRange<float> (0.25f, 4.0f, 0.0001f, 0.5f), 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdPitch, "Pitch (st)", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (paramIdEngine, "Engine", juce::StringArray { "RubberBand", "Naive" }, 0));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdSpeedMod, "Speed Mod", juce::NormalisableRange<float> (0.25f, 4.0f, 0.0001f, 0.5f), 1.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> (paramIdPitchMod, "Pitch Mod", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f));
    return { p.begin(), p.end() };
}

TimePitchModuleProcessor::TimePitchModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("Inputs", juce::AudioChannelSet::discreteChannels(4), true) // ch0 L in, ch1 R in, ch2 Speed Mod, ch3 Pitch Mod
        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TimePitchParams", createParameterLayout())
{
    speedParam     = apvts.getRawParameterValue (paramIdSpeed);
    pitchParam     = apvts.getRawParameterValue (paramIdPitch);
    speedModParam  = apvts.getRawParameterValue (paramIdSpeedMod);
    pitchModParam  = apvts.getRawParameterValue (paramIdPitchMod);
    engineParam    = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter (paramIdEngine));

    lastOutputValues.clear();
    lastOutputValues.push_back (std::make_unique<std::atomic<float>> (0.0f));
    lastOutputValues.push_back (std::make_unique<std::atomic<float>> (0.0f));
    
    // Initialize smoothed values
    speedSm.reset(1.0f);
    pitchSm.reset(0.0f);
}

void TimePitchModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    timePitch.prepare (sampleRate, 2, samplesPerBlock);

    // Initialize FIFO to ~2 seconds of audio
    fifoSize = (int) (sampleRate * 2.0);
    if (fifoSize < samplesPerBlock * 4) fifoSize = samplesPerBlock * 4; // safety minimum
    inputFifo.setSize (2, fifoSize);
    abstractFifo.setTotalSize (fifoSize);

    interleavedCapacityFrames = 0;
    ensureCapacity (interleavedInput, samplesPerBlock, 2, interleavedCapacityFrames);
    ensureCapacity (interleavedOutput, samplesPerBlock * 2, 2, interleavedCapacityFrames); // some headroom
    timePitch.reset();
}

void TimePitchModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    auto inBus = getBusBuffer(buffer, true, 0);  // Single bus
    auto outBus = getBusBuffer(buffer, false, 0);
    
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // 1) Write incoming audio into FIFO (use inBus explicitly)
    int start1=0,size1=0,start2=0,size2=0;
    abstractFifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    if (size1 > 0)
    {
        inputFifo.copyFrom (0, start1, inBus, 0, 0, size1);
        inputFifo.copyFrom (1, start1, inBus, 1, 0, size1);
    }
    if (size2 > 0)
    {
        inputFifo.copyFrom (0, start2, inBus, 0, size1, size2);
        inputFifo.copyFrom (1, start2, inBus, 1, size1, size2);
    }
    const int written = size1 + size2;
    abstractFifo.finishedWrite (written);

    // 2) Read params and configure engine
    const int engineIdx = engineParam != nullptr ? engineParam->getIndex() : 0;
    {
        static int currentMode = -1;
        const int requestedMode = (engineIdx == 0 ? (int) TimePitchProcessor::Mode::RubberBand : (int) TimePitchProcessor::Mode::Fifo);
        if (requestedMode != currentMode)
        {
            timePitch.reset();
            currentMode = requestedMode;
        }
        timePitch.setMode ((TimePitchProcessor::Mode) requestedMode);
    }

    // Get pointers to modulation CV inputs
    const bool isSpeedMod = isParamInputConnected("speed");
    const bool isPitchMod = isParamInputConnected("pitch");
    
    const float* speedCV = isSpeedMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* pitchCV = isPitchMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    
    // Process in slices to reduce engine reconfig cost
    const int sliceSize = 32;
    for (int sliceStart = 0; sliceStart < numSamples; sliceStart += sliceSize)
    {
        const int sliceEnd = juce::jmin(sliceStart + sliceSize, numSamples);
        const int sliceSamples = sliceEnd - sliceStart;
        
        // Calculate target values from CV (use middle of slice)
        const int midSample = sliceStart + sliceSamples / 2;
        
        float targetSpeed = speedParam->load();
        if (isSpeedMod && speedCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, speedCV[midSample]);
            const float minSpeed = 0.25f;
            const float maxSpeed = 4.0f;
            targetSpeed = minSpeed * std::pow(maxSpeed / minSpeed, cv);
        }
        
        float targetPitch = pitchParam->load();
        if (isPitchMod && pitchCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, pitchCV[midSample]);
            targetPitch = -24.0f + cv * 48.0f; // -24 to +24 semitones
        }
        
        // Set targets and advance smoothing
        speedSm.setTargetValue(juce::jlimit(0.1f, 4.0f, targetSpeed));
        pitchSm.setTargetValue(juce::jlimit(-24.0f, 24.0f, targetPitch));
        
        // Update telemetry for live UI feedback (once per slice)
        setLiveParamValue("speed_live", speedSm.getCurrentValue());
        setLiveParamValue("pitch_live", pitchSm.getCurrentValue());
        
        // Advance smoothing for this slice
        for (int i = 0; i < sliceSamples; ++i) {
            speedSm.skip(1);
            pitchSm.skip(1);
        }
        
        // Configure engine with current smoothed values
        timePitch.setTimeStretchRatio(speedSm.getCurrentValue());
        timePitch.setPitchSemitones(pitchSm.getCurrentValue());
        
        // Publish telemetry
        setLiveParamValue("speed", speedSm.getCurrentValue());
        setLiveParamValue("pitch", pitchSm.getCurrentValue());
    }

    // 3) Compute frames needed to fill this block
    const double safeSpeed = juce::jlimit (0.1, 4.0, (double) speedSm.getCurrentValue());
    const int framesToFeed = juce::jmax (1, (int) std::ceil ((double) numSamples / safeSpeed));

    outBus.clear();
    if (abstractFifo.getNumReady() >= framesToFeed)
    {
        // 4) Read from FIFO and interleave
        ensureCapacity (interleavedInput, framesToFeed, 2, interleavedCapacityFrames);
        abstractFifo.prepareToRead (framesToFeed, start1, size1, start2, size2);
        auto* inL = inputFifo.getReadPointer (0);
        auto* inR = inputFifo.getReadPointer (1);
        float* inLR = interleavedInput.getData();
        for (int i = 0; i < size1; ++i) { inLR[2*i+0] = inL[start1 + i]; inLR[2*i+1] = inR[start1 + i]; }
        if (size2 > 0)
            for (int i = 0; i < size2; ++i) { inLR[2*(size1+i)+0] = inL[start2 + i]; inLR[2*(size1+i)+1] = inR[start2 + i]; }
        const int readCount = size1 + size2;
        abstractFifo.finishedRead (readCount);

        // 5) Process and copy back
        // Guard against engine internal errors with try/catch (non-RT critical path)
        try { timePitch.putInterleaved (inLR, framesToFeed); }
        catch (...) { /* swallow to avoid crash; output will be silence */ }
        ensureCapacity (interleavedOutput, numSamples, 2, interleavedCapacityFrames);
        int produced = 0;
        try { produced = timePitch.receiveInterleaved (interleavedOutput.getData(), numSamples); }
        catch (...) { produced = 0; }
        if (produced > 0)
        {
            const int outFrames = juce::jmin (numSamples, produced);
            const float* outLR = interleavedOutput.getData();
            float* L = outBus.getNumChannels() > 0 ? outBus.getWritePointer (0) : buffer.getWritePointer(0);
            float* R = outBus.getNumChannels() > 1 ? outBus.getWritePointer (1) : L;
            for (int i = 0; i < outFrames; ++i) { L[i] = outLR[2*i+0]; if (R) R[i] = outLR[2*i+1]; }
        }
    }

    // Update lastOutputValues
    if (lastOutputValues.size() >= 2)
    {
        lastOutputValues[0]->store (buffer.getMagnitude (0, 0, numSamples));
        lastOutputValues[1]->store (buffer.getNumChannels() > 1 ? buffer.getMagnitude (1, 0, numSamples) : 0.0f);
    }
}

// Parameter bus contract implementation
bool TimePitchModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == "speed") { outChannelIndexInBus = 2; return true; }  // Speed Mod
    if (paramId == "pitch") { outChannelIndexInBus = 3; return true; }  // Pitch Mod
    return false;
}

#if defined(PRESET_CREATOR_UI)
void TimePitchModuleProcessor::drawParametersInNode (float itemWidth,
                                                    const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                    const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth (itemWidth);
    auto& ap = getAPVTS();

    // Speed
    bool spMod = isParamModulated ("speed");
    if (spMod) { 
        ImGui::BeginDisabled(); 
        ImGui::PushStyleColor (ImGuiCol_FrameBg, ImVec4 (1,1,0,0.3f)); 
    }
    float speed = ap.getRawParameterValue (paramIdSpeed)->load();
    if (spMod) {
        speed = getLiveParamValueFor("speed", "speed_live", speed);
    }
    if (ImGui::SliderFloat ("Speed", &speed, 0.25f, 4.0f, "%.2fx")) {
        if (!spMod) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter (paramIdSpeed))) *p = speed;
        }
    }
    if (!spMod) adjustParamOnWheel (ap.getParameter (paramIdSpeed), paramIdSpeed, speed);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (spMod) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }

    // Pitch
    bool piMod = isParamModulated ("pitch");
    if (piMod) { 
        ImGui::BeginDisabled(); 
        ImGui::PushStyleColor (ImGuiCol_FrameBg, ImVec4 (1,1,0,0.3f)); 
    }
    float pitch = ap.getRawParameterValue (paramIdPitch)->load();
    if (piMod) {
        pitch = getLiveParamValueFor("pitch", "pitch_live", pitch);
    }
    if (ImGui::SliderFloat ("Pitch", &pitch, -24.0f, 24.0f, "%.1f st"))
        if (!piMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter (paramIdPitch))) *p = pitch;
    if (!piMod) adjustParamOnWheel (ap.getParameter (paramIdPitch), paramIdPitch, pitch);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (piMod) { ImGui::PopStyleColor(); ImGui::EndDisabled(); }

    // Engine
    int engineIdx = engineParam != nullptr ? engineParam->getIndex() : 0;
    const char* items[] = { "RubberBand", "Naive" };
    if (ImGui::Combo ("Engine", &engineIdx, items, 2))
        if (engineParam) *engineParam = engineIdx;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

    ImGui::PopItemWidth();
}

void TimePitchModuleProcessor::drawIoPins (const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin ("In L", 0);
    helpers.drawAudioInputPin ("In R", 1);
    helpers.drawAudioInputPin ("Speed Mod", 2);
    helpers.drawAudioInputPin ("Pitch Mod", 3);

    helpers.drawAudioOutputPin ("Out L", 0);
    helpers.drawAudioOutputPin ("Out R", 1);
}
#endif


