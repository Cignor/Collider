#include "QuantizerModuleProcessor.h"

QuantizerModuleProcessor::QuantizerModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // 0: Audio In, 1: Scale Mod, 2: Root Mod
                        .withOutput("Out", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "QuantizerParams", createParameterLayout())
{
    scaleParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("scale"));
    rootNoteParam = dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter("rootNote"));
    scaleModParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("scale_mod"));
    rootModParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("root_mod"));

    // Define scales as semitone offsets from the root
    scales.push_back({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }); // Chromatic
    scales.push_back({ 0, 2, 4, 5, 7, 9, 11 }); // Major
    scales.push_back({ 0, 2, 3, 5, 7, 8, 10 }); // Natural Minor
    scales.push_back({ 0, 2, 4, 7, 9 }); // Major Pentatonic
    scales.push_back({ 0, 3, 5, 7, 10 }); // Minor Pentatonic
    
    // ADD THIS:
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout QuantizerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale",
        juce::StringArray{ "Chromatic", "Major", "Natural Minor", "Major Pentatonic", "Minor Pentatonic" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterInt>("rootNote", "Root Note", 0, 11, 0)); // 0=C, 1=C#, etc.
    p.push_back(std::make_unique<juce::AudioParameterFloat>("scale_mod", "Scale Mod", 0.0f, 1.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("root_mod", "Root Mod", 0.0f, 1.0f, 0.0f));
    return { p.begin(), p.end() };
}

void QuantizerModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void QuantizerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);

    // Read CV from unified input bus (if connected)
    float scaleModCV = 0.0f;
    float rootModCV = 0.0f;
    
    // Check if scale mod is connected and read CV from channel 1
    if (isParamInputConnected("scale_mod") && in.getNumChannels() > 1)
    {
        scaleModCV = in.getReadPointer(1)[0];
    }
    
    // Check if root mod is connected and read CV from channel 2
    if (isParamInputConnected("root_mod") && in.getNumChannels() > 2)
    {
        rootModCV = in.getReadPointer(2)[0];
    }

    // Apply modulation or use parameter values
    float scaleModValue = 0.0f;
    if (isParamInputConnected("scale_mod")) // Scale Mod bus connected
    {
        scaleModValue = scaleModCV;
    }
    else
    {
        scaleModValue = scaleModParam != nullptr ? scaleModParam->get() : 0.0f;
    }
    
    float rootModValue = 0.0f;
    if (isParamInputConnected("root_mod")) // Root Mod bus connected
    {
        rootModValue = rootModCV;
    }
    else
    {
        rootModValue = rootModParam != nullptr ? rootModParam->get() : 0.0f;
    }

    // Calculate final scale index, wrapping around if necessary
    int finalScaleIdx = (scaleParam != nullptr ? scaleParam->getIndex() : 0) + static_cast<int>(scaleModValue * (float)scales.size());
    finalScaleIdx = finalScaleIdx % (int)scales.size();

    // Calculate final root note, wrapping around the 12-semitone octave
    int finalRootNote = (rootNoteParam != nullptr ? rootNoteParam->get() : 0) + static_cast<int>(rootModValue * 12.0f);
    finalRootNote = finalRootNote % 12;

    const auto& currentScale = scales[finalScaleIdx];
    const float rootNote = (float)finalRootNote;

    const float* src = in.getReadPointer(0);
    float* dst = out.getWritePointer(0);
    
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float inputCV = juce::jlimit(0.0f, 1.0f, src[i]);
        
        // Map 0..1 CV to a 5-octave range (60 semitones)
        const float totalSemitones = inputCV * 60.0f;
        const int octave = static_cast<int>(totalSemitones / 12.0f);
        const float noteInOctave = totalSemitones - (octave * 12.0f);
        
        // Find the closest note in the scale
        float closestNote = currentScale[0];
        float minDistance = 12.0f;
        for (float scaleNote : currentScale)
        {
            float distance = std::abs(noteInOctave - scaleNote);
            if (distance < minDistance)
            {
                minDistance = distance;
                closestNote = scaleNote;
            }
        }
        
        // Combine octave, root, and quantized note, then map back to 0..1 CV
        float finalSemitones = (octave * 12.0f) + closestNote + rootNote;
        dst[i] = juce::jlimit(0.0f, 1.0f, finalSemitones / 60.0f);
    }
    
    // Store live modulated values for UI display
    setLiveParamValue("scale_live", static_cast<float>(finalScaleIdx));
    setLiveParamValue("root_live", static_cast<float>(finalRootNote));

    // ADD THIS BLOCK:
    if (!lastOutputValues.empty() && lastOutputValues[0])
    {
        lastOutputValues[0]->store(out.getSample(0, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void QuantizerModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    int scale = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("scale"))) scale = p->getIndex();
    int root = 0; if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter("rootNote"))) root = *p;

    const char* scales = "Chromatic\0Major\0Natural Minor\0Major Pentatonic\0Minor Pentatonic\0\0";
    const char* notes = "C\0C#\0D\0D#\0E\0F\0F#\0G\0G#\0A\0A#\0B\0\0";
    
    ImGui::PushItemWidth(itemWidth);

    // Scale Combo Box
    bool isScaleModulated = isParamModulated("scale_mod");
    if (isScaleModulated) {
        scale = static_cast<int>(getLiveParamValueFor("scale_mod", "scale_live", static_cast<float>(scale)));
        ImGui::BeginDisabled();
    }
    if (ImGui::Combo("Scale", &scale, scales)) if (!isScaleModulated) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("scale"))) *p = scale;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isScaleModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Root Note Combo Box
    bool isRootModulated = isParamModulated("root_mod");
    if (isRootModulated) {
        root = static_cast<int>(getLiveParamValueFor("root_mod", "root_live", static_cast<float>(root)));
        ImGui::BeginDisabled();
    }
    if (ImGui::Combo("Root", &root, notes)) if (!isRootModulated) if (auto* p = dynamic_cast<juce::AudioParameterInt*>(ap.getParameter("rootNote"))) *p = root;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isRootModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    ImGui::PopItemWidth();
}
#endif

void QuantizerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In", 0);
    
    int busIdx, chanInBus;
    if (getParamRouting("scale_mod", busIdx, chanInBus))
        helpers.drawAudioInputPin("Scale Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("root_mod", busIdx, chanInBus))
        helpers.drawAudioInputPin("Root Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    
    helpers.drawAudioOutputPin("Out", 0);
}

bool QuantizerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "scale_mod") { outChannelIndexInBus = 1; return true; }
    if (paramId == "root_mod")  { outChannelIndexInBus = 2; return true; }
    return false;
}
