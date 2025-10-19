#include "CVMixerModuleProcessor.h"

CVMixerModuleProcessor::CVMixerModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("CV Inputs", juce::AudioChannelSet::discreteChannels(4), true)  // Bus 0: A, B, C, D
        .withInput ("Crossfade Mod", juce::AudioChannelSet::mono(), true)           // Bus 1
        .withInput ("Level A Mod", juce::AudioChannelSet::mono(), true)             // Bus 2
        .withInput ("Level C Mod", juce::AudioChannelSet::mono(), true)             // Bus 3
        .withInput ("Level D Mod", juce::AudioChannelSet::mono(), true)             // Bus 4
        .withOutput("Outputs", juce::AudioChannelSet::discreteChannels(2), true)),  // Bus 0: Mix Out, Inv Out
      apvts (*this, nullptr, "CVMixerParams", createParameterLayout())
{
    crossfadeParam = apvts.getRawParameterValue ("crossfade");
    levelAParam    = apvts.getRawParameterValue ("levelA");
    levelCParam    = apvts.getRawParameterValue ("levelC");
    levelDParam    = apvts.getRawParameterValue ("levelD");

    // Initialize value tooltips for the two outputs
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Mix Out
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Inv Out
}

juce::AudioProcessorValueTreeState::ParameterLayout CVMixerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    
    // Crossfade: -1 = full A, 0 = equal mix, +1 = full B
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "crossfade", "Crossfade A/B", 
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    
    // Level A: master level for the A/B crossfade section (0..1)
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "levelA", "Level A/B", 
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));
    
    // Level C: bipolar for adding/subtracting input C
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "levelC", "Level C", 
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    
    // Level D: bipolar for adding/subtracting input D
    p.push_back (std::make_unique<juce::AudioParameterFloat> (
        "levelD", "Level D", 
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
    
    return { p.begin(), p.end() };
}

void CVMixerModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void CVMixerModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    // Get input and output buses
    auto cvInputs = getBusBuffer(buffer, true, 0);  // 4 discrete channels: A, B, C, D
    auto outputs = getBusBuffer(buffer, false, 0);  // 2 discrete channels: Mix Out, Inv Out
    
    const int numSamples = buffer.getNumSamples();
    
    // Read modulation CV values (first sample of each mod bus)
    float crossfadeMod = 0.0f;
    float levelAMod = 0.0f;
    float levelCMod = 0.0f;
    float levelDMod = 0.0f;
    
    if (isParamInputConnected("crossfade"))
    {
        const auto& modBus = getBusBuffer(buffer, true, 1);
        if (modBus.getNumChannels() > 0)
            crossfadeMod = modBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("levelA"))
    {
        const auto& modBus = getBusBuffer(buffer, true, 2);
        if (modBus.getNumChannels() > 0)
            levelAMod = modBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("levelC"))
    {
        const auto& modBus = getBusBuffer(buffer, true, 3);
        if (modBus.getNumChannels() > 0)
            levelCMod = modBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("levelD"))
    {
        const auto& modBus = getBusBuffer(buffer, true, 4);
        if (modBus.getNumChannels() > 0)
            levelDMod = modBus.getReadPointer(0)[0];
    }
    
    // Determine final parameter values (modulated or from parameters)
    float crossfade = 0.0f;
    if (isParamInputConnected("crossfade"))
    {
        // Map CV [0,1] to crossfade [-1, 1]
        crossfade = -1.0f + crossfadeMod * 2.0f;
    }
    else
    {
        crossfade = crossfadeParam != nullptr ? crossfadeParam->load() : 0.0f;
    }
    
    float levelA = 0.0f;
    if (isParamInputConnected("levelA"))
    {
        // Map CV [0,1] to levelA [0, 1]
        levelA = levelAMod;
    }
    else
    {
        levelA = levelAParam != nullptr ? levelAParam->load() : 1.0f;
    }
    
    float levelC = 0.0f;
    if (isParamInputConnected("levelC"))
    {
        // Map CV [0,1] to levelC [-1, 1]
        levelC = -1.0f + levelCMod * 2.0f;
    }
    else
    {
        levelC = levelCParam != nullptr ? levelCParam->load() : 0.0f;
    }
    
    float levelD = 0.0f;
    if (isParamInputConnected("levelD"))
    {
        // Map CV [0,1] to levelD [-1, 1]
        levelD = -1.0f + levelDMod * 2.0f;
    }
    else
    {
        levelD = levelDParam != nullptr ? levelDParam->load() : 0.0f;
    }
    
    // Get read pointers for all inputs (may be null if not connected)
    const float* inA = cvInputs.getNumChannels() > 0 ? cvInputs.getReadPointer(0) : nullptr;
    const float* inB = cvInputs.getNumChannels() > 1 ? cvInputs.getReadPointer(1) : nullptr;
    const float* inC = cvInputs.getNumChannels() > 2 ? cvInputs.getReadPointer(2) : nullptr;
    const float* inD = cvInputs.getNumChannels() > 3 ? cvInputs.getReadPointer(3) : nullptr;
    
    // Get write pointers for outputs
    float* mixOut = outputs.getNumChannels() > 0 ? outputs.getWritePointer(0) : nullptr;
    float* invOut = outputs.getNumChannels() > 1 ? outputs.getWritePointer(1) : nullptr;
    
    // Process each sample
    for (int i = 0; i < numSamples; ++i)
    {
        // Read input samples (0.0 if not connected)
        const float a = inA ? inA[i] : 0.0f;
        const float b = inB ? inB[i] : 0.0f;
        const float c = inC ? inC[i] : 0.0f;
        const float d = inD ? inD[i] : 0.0f;
        
        // 1. Linear crossfade between A and B
        // Convert crossfade from [-1, 1] to mix amount [0, 1]
        const float mixAmount = (crossfade + 1.0f) * 0.5f;
        const float crossfaded_AB = (a * (1.0f - mixAmount)) + (b * mixAmount);
        
        // 2. Apply master level for the A/B section
        const float final_AB = crossfaded_AB * levelA;
        
        // 3. Sum all inputs with their respective levels
        const float finalMix = final_AB + (c * levelC) + (d * levelD);
        
        // 4. Write to outputs
        if (mixOut) mixOut[i] = finalMix;
        if (invOut) invOut[i] = -finalMix;
    }
    
    // Store live modulated values for UI display
    setLiveParamValue("crossfade_live", crossfade);
    setLiveParamValue("levelA_live", levelA);
    setLiveParamValue("levelC_live", levelC);
    setLiveParamValue("levelD_live", levelD);
    
    // Update tooltips with last sample values
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0] && mixOut) lastOutputValues[0]->store(mixOut[numSamples - 1]);
        if (lastOutputValues[1] && invOut) lastOutputValues[1]->store(invOut[numSamples - 1]);
    }
}

#if defined(PRESET_CREATOR_UI)
void CVMixerModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    float crossfade = crossfadeParam != nullptr ? crossfadeParam->load() : 0.0f;
    float levelA = levelAParam != nullptr ? levelAParam->load() : 1.0f;
    float levelC = levelCParam != nullptr ? levelCParam->load() : 0.0f;
    float levelD = levelDParam != nullptr ? levelDParam->load() : 0.0f;

    ImGui::PushItemWidth (itemWidth);

    // Crossfade A/B (horizontal slider)
    bool isCrossfadeModulated = isParamModulated("crossfade");
    if (isCrossfadeModulated) {
        crossfade = getLiveParamValueFor("crossfade", "crossfade_live", crossfade);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("A <-> B", &crossfade, -1.0f, 1.0f)) {
        if (!isCrossfadeModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("crossfade"))) *p = crossfade;
        }
    }
    if (!isCrossfadeModulated) adjustParamOnWheel (ap.getParameter ("crossfade"), "crossfade", crossfade);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isCrossfadeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::Spacing();

    // Level A (master level for A/B section)
    bool isLevelAModulated = isParamModulated("levelA");
    if (isLevelAModulated) {
        levelA = getLiveParamValueFor("levelA", "levelA_live", levelA);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Level A/B", &levelA, 0.0f, 1.0f)) {
        if (!isLevelAModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("levelA"))) *p = levelA;
        }
    }
    if (!isLevelAModulated) adjustParamOnWheel (ap.getParameter ("levelA"), "levelA", levelA);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isLevelAModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Level C (bipolar)
    bool isLevelCModulated = isParamModulated("levelC");
    if (isLevelCModulated) {
        levelC = getLiveParamValueFor("levelC", "levelC_live", levelC);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Level C", &levelC, -1.0f, 1.0f)) {
        if (!isLevelCModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("levelC"))) *p = levelC;
        }
    }
    if (!isLevelCModulated) adjustParamOnWheel (ap.getParameter ("levelC"), "levelC", levelC);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isLevelCModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Level D (bipolar)
    bool isLevelDModulated = isParamModulated("levelD");
    if (isLevelDModulated) {
        levelD = getLiveParamValueFor("levelD", "levelD_live", levelD);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Level D", &levelD, -1.0f, 1.0f)) {
        if (!isLevelDModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("levelD"))) *p = levelD;
        }
    }
    if (!isLevelDModulated) adjustParamOnWheel (ap.getParameter ("levelD"), "levelD", levelD);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isLevelDModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::PopItemWidth();
}
#endif

void CVMixerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Audio inputs
    helpers.drawAudioInputPin("In A", 0);
    helpers.drawAudioInputPin("In B", 1);
    helpers.drawAudioInputPin("In C", 2);
    helpers.drawAudioInputPin("In D", 3);

    // Modulation inputs
    int busIdx, chanInBus;
    if (getParamRouting("crossfade", busIdx, chanInBus))
        helpers.drawAudioInputPin("Crossfade Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("levelA", busIdx, chanInBus))
        helpers.drawAudioInputPin("Level A Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("levelC", busIdx, chanInBus))
        helpers.drawAudioInputPin("Level C Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("levelD", busIdx, chanInBus))
        helpers.drawAudioInputPin("Level D Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));

    // Outputs
    helpers.drawAudioOutputPin("Mix Out", 0);
    helpers.drawAudioOutputPin("Inv Out", 1);
}

bool CVMixerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outChannelIndexInBus = 0;
    if (paramId == "crossfade") { outBusIndex = 1; return true; }
    if (paramId == "levelA")    { outBusIndex = 2; return true; }
    if (paramId == "levelC")    { outBusIndex = 3; return true; }
    if (paramId == "levelD")    { outBusIndex = 4; return true; }
    return false;
}

