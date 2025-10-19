#include "MapRangeModuleProcessor.h"

MapRangeModuleProcessor::MapRangeModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("In", juce::AudioChannelSet::mono(), true)
                        .withOutput("Out", juce::AudioChannelSet::discreteChannels(3), true)),
      apvts(*this, nullptr, "MapRangeParams", createParameterLayout())
{
    inMinParam  = apvts.getRawParameterValue("inMin");
    inMaxParam  = apvts.getRawParameterValue("inMax");
    outMinParam = apvts.getRawParameterValue("outMin");
    outMaxParam = apvts.getRawParameterValue("outMax");
    normMinParam = apvts.getRawParameterValue("normMin");
    normMaxParam = apvts.getRawParameterValue("normMax");
    
    // Cache new CV parameters
    cvMinParam = apvts.getRawParameterValue("cvMin");
    cvMaxParam = apvts.getRawParameterValue("cvMax");
    
    // Initialize storage for the three output pins (Norm Out, Raw Out, CV Out)
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout MapRangeModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inMin", "Input Min", juce::NormalisableRange<float>(-100.0f, 100.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("inMax", "Input Max", juce::NormalisableRange<float>(-100.0f, 100.0f), 1.0f));
    // Bipolar Norm Out range [-1, 1]
    params.push_back(std::make_unique<juce::AudioParameterFloat>("normMin", "Norm Min", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f), -1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("normMax", "Norm Max", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.0001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("outMin", "Output Min", juce::NormalisableRange<float>(-10000.0f, 10000.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("outMax", "Output Max", juce::NormalisableRange<float>(-10000.0f, 10000.0f), 1.0f));
    
    // Add new CV parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cvMin", "CV Min", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("cvMax", "CV Max", juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    
    return { params.begin(), params.end() };
}

void MapRangeModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void MapRangeModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto in = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    // Ensure we present audio on a conventional stereo bus for downstream audio nodes.
    // Norm Out (0) is intended for CV; Raw Out (1) is wide-range audio.
    // Duplicate Raw Out into both L/R when the main graph expects stereo.
    
    const float inMin = inMinParam->load();
    const float inMax = inMaxParam->load();
    const float normMin = normMinParam->load();
    const float normMax = normMaxParam->load();
    const float outMin = outMinParam->load();
    const float outMax = outMaxParam->load();
    const float cvMin = cvMinParam->load();
    const float cvMax = cvMaxParam->load();
    
    const float inRange = inMax - inMin;
    const float outRange = outMax - outMin;
    
    // Get pointers to all three output channels: 0 for Norm (CV), 1 for Raw (audio), 2 for CV
    float* normDst = out.getWritePointer(0);
    float* rawDst = out.getNumChannels() > 1 ? out.getWritePointer(1) : nullptr;
    float* cvDst = out.getNumChannels() > 2 ? out.getWritePointer(2) : nullptr;

    if (std::abs(inRange) < 0.0001f)
    {
        // Handle division by zero: output the middle of the output range.
        const float rawVal = (outMin + outMax) * 0.5f;
        const float normVal = 0.5f;
        const float cvVal = (cvMin + cvMax) * 0.5f;
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            normDst[i] = normVal;
            if (rawDst) rawDst[i] = rawVal;
            if (cvDst) cvDst[i] = cvVal;
        }
        lastInputValue.store(inMin);
        lastOutputValue.store(rawVal);
        lastCvOutputValue.store(cvVal);
    }
    else
    {
        const float* src = in.getReadPointer(0);
        float sumInput = 0.0f;
        float sumOutput = 0.0f;
        float sumCvOutput = 0.0f;
        
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // 1. Clamp and normalize the input signal (0..1 range)
            float clampedInput = juce::jlimit(inMin, inMax, src[i]);
            float normalizedInput = 0.0f;
            if (std::abs(inRange) > 1e-9f)
                normalizedInput = (clampedInput - inMin) / inRange;
            
            // 2. Calculate the three separate outputs from the normalized value
            float rawOutputVal = juce::jmap(normalizedInput, outMin, outMax);
            float cvOutputVal = juce::jmap(normalizedInput, cvMin, cvMax);
            
            // 3. Write to the respective output channels
            const float norm01 = juce::jlimit(0.0f, 1.0f, normalizedInput);
            const float normVal = juce::jmap(norm01, normMin, normMax);
            normDst[i] = normVal;              // Norm Out (bipolar CV)
            if (rawDst) rawDst[i] = rawOutputVal; // Raw Out (audio)
            if (cvDst)  cvDst[i]  = cvOutputVal;  // CV Out
            
            sumInput += clampedInput;
            sumOutput += rawOutputVal;
            sumCvOutput += cvOutputVal;
        }
        
    lastInputValue.store(sumInput / (float) buffer.getNumSamples());
    lastOutputValue.store(sumOutput / (float) buffer.getNumSamples());
    lastCvOutputValue.store(sumCvOutput / (float) buffer.getNumSamples());

    // Store live parameter values for UI display (currently no modulation, so store parameter values)
    setLiveParamValue("inMin_live", inMin);
    setLiveParamValue("inMax_live", inMax);
    setLiveParamValue("normMin_live", normMin);
    setLiveParamValue("normMax_live", normMax);
    setLiveParamValue("outMin_live", outMin);
    setLiveParamValue("outMax_live", outMax);
    setLiveParamValue("cvMin_live", cvMin);
    setLiveParamValue("cvMax_live", cvMax);
    }

    // Update the hover-value display for all three output pins
    if (lastOutputValues.size() >= 3)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(out.getSample(0, buffer.getNumSamples() - 1));
        if (out.getNumChannels() > 1 && lastOutputValues[1]) lastOutputValues[1]->store(out.getSample(1, buffer.getNumSamples() - 1));
        if (out.getNumChannels() > 2 && lastOutputValues[2]) lastOutputValues[2]->store(out.getSample(2, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void MapRangeModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    float inMin = inMinParam->load();
    float inMax = inMaxParam->load();
    float normMin = normMinParam->load();
    float normMax = normMaxParam->load();
    float outMin = outMinParam->load();
    float outMax = outMaxParam->load();
    float cvMin = cvMinParam->load();
    float cvMax = cvMaxParam->load();

    ImGui::PushItemWidth(itemWidth);
    
    // Input Range Sliders
    if (ImGui::SliderFloat("Input Min", &inMin, -100.0f, 100.0f))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMin"))) *p = inMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("inMin"), "inMin", inMin);
    
    if (ImGui::SliderFloat("Input Max", &inMax, -100.0f, 100.0f))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("inMax"))) *p = inMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("inMax"), "inMax", inMax);
    

    // Norm Out precise bipolar range [-1, 1]
    if (ImGui::SliderFloat("Norm Min", &normMin, -1.0f, 1.0f, "%.4f"))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("normMin"))) *p = normMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("normMin"), "normMin", normMin);

    if (ImGui::SliderFloat("Norm Max", &normMax, -1.0f, 1.0f, "%.4f"))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("normMax"))) *p = normMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("normMax"), "normMax", normMax);

    // CV Output Range Sliders (0.0-1.0 range)
    if (ImGui::SliderFloat("CV Min", &cvMin, 0.0f, 1.0f))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("cvMin"))) *p = cvMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("cvMin"), "cvMin", cvMin);
    
    if (ImGui::SliderFloat("CV Max", &cvMax, 0.0f, 1.0f))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("cvMax"))) *p = cvMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("cvMax"), "cvMax", cvMax);


    // Raw Output Range Sliders (wide range)
    if (ImGui::SliderFloat("Output Min", &outMin, -10000.0f, 10000.0f, "%.1f", ImGuiSliderFlags_Logarithmic))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMin"))) *p = outMin;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("outMin"), "outMin", outMin);
    
    if (ImGui::SliderFloat("Output Max", &outMax, -10000.0f, 10000.0f, "%.1f", ImGuiSliderFlags_Logarithmic))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("outMax"))) *p = outMax;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    adjustParamOnWheel(ap.getParameter("outMax"), "outMax", outMax);


    // Output value displays
    ImGui::Text("Input:     %.2f", getLastInputValue());
    ImGui::Text("Raw Out:   %.2f", getLastOutputValue());
    ImGui::Text("CV Out:    %.2f", getLastCvOutputValue());

    ImGui::PopItemWidth();
}

void MapRangeModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Input", 0);
    helpers.drawAudioOutputPin("Norm Out", 0);
    helpers.drawAudioOutputPin("Raw Out", 1);
    helpers.drawAudioOutputPin("CV Out", 2);
}
#endif

float MapRangeModuleProcessor::getLastInputValue() const
{
    return lastInputValue.load();
}

float MapRangeModuleProcessor::getLastOutputValue() const
{
    return lastOutputValue.load();
}

float MapRangeModuleProcessor::getLastCvOutputValue() const
{
    return lastCvOutputValue.load();
}
