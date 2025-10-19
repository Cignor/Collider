#include "MathModuleProcessor.h"

MathModuleProcessor::MathModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("In A", juce::AudioChannelSet::mono(), true)
                        .withInput ("In B", juce::AudioChannelSet::mono(), true)
                        .withOutput("Out", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "MathParams", createParameterLayout())
{
    valueAParam    = apvts.getRawParameterValue ("valueA");
    valueBParam    = apvts.getRawParameterValue ("valueB");
    operationParam = apvts.getRawParameterValue ("operation");
    
    // ADD THIS:
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout MathModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    // Enhanced operation list with 17 mathematical functions
    p.push_back (std::make_unique<juce::AudioParameterChoice> ("operation", "Operation", 
        juce::StringArray { 
            "Add", "Subtract", "Multiply", "Divide",
            "Min", "Max", "Power", "Sqrt(A)",
            "Sin(A)", "Cos(A)", "Tan(A)",
            "Abs(A)", "Modulo", "Fract(A)", "Int(A)",
            "A > B", "A < B"
        }, 0));
    // New Value A slider default
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("valueA", "Value A", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f));
    // Expanded Value B range from -100 to 100 for more creative possibilities
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("valueB", "Value B", juce::NormalisableRange<float> (-100.0f, 100.0f), 0.0f));
    return { p.begin(), p.end() };
}

void MathModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void MathModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);

    auto inA = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);

    // CORRECTED LOGIC:
    auto inB = getBusBuffer(buffer, true, 1);
    // Use robust connection detection
    const bool inAConnected = isParamInputConnected("valueA");
    const bool inBConnected = isParamInputConnected("valueB");

    const float valueA = valueAParam != nullptr ? valueAParam->load() : 0.0f;
    const float valueB = valueBParam->load();
    const int operation = static_cast<int>(operationParam->load());
    
    const float* srcA = inA.getNumChannels() > 0 ? inA.getReadPointer (0) : nullptr;
    const float* srcB = inBConnected ? inB.getReadPointer (0) : nullptr;
    float* dst = out.getWritePointer (0);

    float sum = 0.0f;
    float sumA = 0.0f;
    float sumB = 0.0f;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float valA = inAConnected && srcA != nullptr ? srcA[i] : valueA;
        float valB = inBConnected ? srcB[i] : valueB;

        // Enhanced mathematical operations with 17 different functions
        switch (operation)
        {
            case 0:  dst[i] = valA + valB; break; // Add
            case 1:  dst[i] = valA - valB; break; // Subtract
            case 2:  dst[i] = valA * valB; break; // Multiply
            case 3:  dst[i] = (std::abs(valB) < 1e-9f) ? 0.0f : (valA / valB); break; // Divide (safe)
            case 4:  dst[i] = std::min(valA, valB); break; // Min
            case 5:  dst[i] = std::max(valA, valB); break; // Max
            case 6:  dst[i] = std::pow(valA, valB); break; // Power
            case 7:  dst[i] = std::sqrt(std::abs(valA)); break; // Sqrt(A) - only on A
            case 8:  dst[i] = std::sin(valA * juce::MathConstants<float>::twoPi); break; // Sin(A) - only on A
            case 9:  dst[i] = std::cos(valA * juce::MathConstants<float>::twoPi); break; // Cos(A) - only on A
            case 10: dst[i] = std::tan(valA * juce::MathConstants<float>::pi); break; // Tan(A) - only on A
            case 11: dst[i] = std::abs(valA); break; // Abs(A) - only on A
            case 12: dst[i] = (std::abs(valB) < 1e-9f) ? 0.0f : std::fmod(valA, valB); break; // Modulo (safe)
            case 13: dst[i] = valA - std::trunc(valA); break; // Fract(A) - only on A
            case 14: dst[i] = std::trunc(valA); break; // Int(A) - only on A
            case 15: dst[i] = (valA > valB) ? 1.0f : 0.0f; break; // A > B
            case 16: dst[i] = (valA < valB) ? 1.0f : 0.0f; break; // A < B
        }
        sum += dst[i];
        sumA += valA;
        sumB += valB;
        
        // Update telemetry for live UI feedback (throttled to every 64 samples)
        if ((i & 0x3F) == 0) {
            setLiveParamValue("valueA_live", valA);
            setLiveParamValue("valueB_live", valB);
            setLiveParamValue("operation_live", static_cast<float>(operation));
        }
    }
    lastValue.store(sum / (float) buffer.getNumSamples());
    lastValueA.store(sumA / (float) buffer.getNumSamples());
    lastValueB.store(sumB / (float) buffer.getNumSamples());
    
    // ADD THIS BLOCK:
    if (!lastOutputValues.empty() && lastOutputValues[0])
    {
        lastOutputValues[0]->store(out.getSample(0, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void MathModuleProcessor::drawParametersInNode (float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    int op = 0; if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("operation"))) op = p->getIndex();
    float valA = valueAParam != nullptr ? valueAParam->load() : 0.0f;
    float valB = valueBParam != nullptr ? valueBParam->load() : 0.0f;
    
    ImGui::PushItemWidth (itemWidth);
    
    // Operation combo box (no modulation input, so no live feedback needed)
    if (ImGui::Combo ("Operation", &op, 
        "Add\0Subtract\0Multiply\0Divide\0"
        "Min\0Max\0Power\0Sqrt(A)\0"
        "Sin(A)\0Cos(A)\0Tan(A)\0"
        "Abs(A)\0Modulo\0Fract(A)\0Int(A)\0"
        "A > B\0A < B\0\0"))
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("operation"))) *p = op;
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

    // Value A slider with live modulation feedback
    bool isValueAModulated = isParamModulated("valueA");
    if (isValueAModulated) {
        valA = getLiveParamValueFor("valueA", "valueA_live", valA);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Value A", &valA, -100.0f, 100.0f)) {
        if (!isValueAModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("valueA"))) *p = valA;
        }
    }
    if (!isValueAModulated) adjustParamOnWheel (ap.getParameter("valueA"), "valueA", valA);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isValueAModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // Value B slider with live modulation feedback
    bool isValueBModulated = isParamModulated("valueB");
    if (isValueBModulated) {
        valB = getLiveParamValueFor("valueB", "valueB_live", valB);
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat ("Value B", &valB, -100.0f, 100.0f)) {
        if (!isValueBModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("valueB"))) *p = valB;
        }
    }
    if (!isValueBModulated) adjustParamOnWheel (ap.getParameter("valueB"), "valueB", valB);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (isValueBModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::Text("In A: %.2f", getLastValueA());
    ImGui::Text("In B: %.2f", getLastValueB());
    ImGui::Text("Out: %.2f", getLastValue());

    ImGui::PopItemWidth();
}

void MathModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In A", 0);
    helpers.drawAudioInputPin("In B", 1);
    helpers.drawAudioOutputPin("Out", 0);
}
#endif

float MathModuleProcessor::getLastValue() const
{
    return lastValue.load();
}

float MathModuleProcessor::getLastValueA() const
{
    return lastValueA.load();
}

float MathModuleProcessor::getLastValueB() const
{
    return lastValueB.load();
}

bool MathModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    if (paramId == "valueA") { outBusIndex = 0; outChannelIndexInBus = 0; return true; }
    if (paramId == "valueB") { outBusIndex = 1; outChannelIndexInBus = 0; return true; }
    return false;
}