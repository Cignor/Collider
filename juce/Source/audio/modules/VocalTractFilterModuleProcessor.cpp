// Rebuilt module implementation
#include "VocalTractFilterModuleProcessor.h"
#include "../../utils/RtLogger.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

// Static formant tables
const FormantData VocalTractFilterModuleProcessor::VOWEL_A[4] = { {700.0f,1.0f,6.0f},{1220.0f,0.5f,8.0f},{2600.0f,0.2f,12.0f},{3800.0f,0.15f,15.0f} };
const FormantData VocalTractFilterModuleProcessor::VOWEL_E[4] = { {500.0f,1.0f,7.0f},{1800.0f,0.6f,9.0f},{2800.0f,0.3f,13.0f},{3900.0f,0.2f,16.0f} };
const FormantData VocalTractFilterModuleProcessor::VOWEL_I[4] = { {270.0f,1.0f,8.0f},{2300.0f,0.4f,10.0f},{3000.0f,0.2f,14.0f},{4000.0f,0.1f,18.0f} };
const FormantData VocalTractFilterModuleProcessor::VOWEL_O[4] = { {450.0f,1.0f,6.0f},{800.0f,0.7f,8.0f},{2830.0f,0.15f,12.0f},{3800.0f,0.1f,15.0f} };
const FormantData VocalTractFilterModuleProcessor::VOWEL_U[4] = { {300.0f,1.0f,7.0f},{870.0f,0.6f,9.0f},{2240.0f,0.1f,13.0f},{3500.0f,0.05f,16.0f} };

VocalTractFilterModuleProcessor::VocalTractFilterModuleProcessor()
    : ModuleProcessor(BusesProperties().withInput("Audio In", juce::AudioChannelSet::mono(), true)
                                        .withInput("Vowel Mod", juce::AudioChannelSet::mono(), true)
                                        .withInput("Formant Mod", juce::AudioChannelSet::mono(), true)
                                        .withInput("Instability Mod", juce::AudioChannelSet::mono(), true)
                                        .withInput("Gain Mod", juce::AudioChannelSet::mono(), true)
                                        .withOutput("Audio Out", juce::AudioChannelSet::mono(), true))
    , apvts(*this, nullptr, "VocalTractParams", createParameterLayout())
{
    vowelShapeParam   = apvts.getRawParameterValue("vowelShape");
    formantShiftParam = apvts.getRawParameterValue("formantShift");
    instabilityParam  = apvts.getRawParameterValue("instability");
    outputGainParam   = apvts.getRawParameterValue("formantGain");

    // Initialize oscillators with sine wave functions
    wowOscillator.initialise([](float x) { return std::sin(x); }, 128);
    flutterOscillator.initialise([](float x) { return std::sin(x); }, 128);
    
    // Initialize lastOutputValues for cable inspector
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void VocalTractFilterModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (sampleRate <= 0.0 || samplesPerBlock <= 0) return;

    dspSpec.sampleRate = sampleRate;
    dspSpec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    dspSpec.numChannels = 1;

    bands.b0.prepare(dspSpec); bands.b1.prepare(dspSpec);
    bands.b2.prepare(dspSpec); bands.b3.prepare(dspSpec);
    bands.b0.reset(); bands.b1.reset(); bands.b2.reset(); bands.b3.reset();

    wowOscillator.prepare(dspSpec); wowOscillator.setFrequency(0.5f); wowOscillator.reset();
    flutterOscillator.prepare(dspSpec); flutterOscillator.setFrequency(7.5f); flutterOscillator.reset();

    ensureWorkBuffers(1, samplesPerBlock);
    updateCoefficients(0.0f, 0.0f, 0.0f); // Initialize with default values
}

void VocalTractFilterModuleProcessor::ensureWorkBuffers(int numChannels, int numSamples)
{
    workBuffer.setSize(numChannels, numSamples, false, false, true);
    sumBuffer.setSize(numChannels, numSamples, false, false, true);
    tmpBuffer.setSize(numChannels, numSamples, false, false, true);
}

void VocalTractFilterModuleProcessor::updateCoefficients(float vowelShape, float formantShift, float instability)
{
    const FormantData* tables[] = { VOWEL_A, VOWEL_E, VOWEL_I, VOWEL_O, VOWEL_U };
    float shape = juce::jlimit(0.0f, 3.999f, vowelShape);
    int i0 = (int) std::floor(shape);
    int i1 = juce::jmin(4, i0 + 1);
    float t = shape - (float)i0;
    float shift = std::pow(2.0f, juce::jlimit(-1.0f, 1.0f, formantShift));
    float inst = juce::jlimit(0.0f, 1.0f, instability);
    float wow = wowOscillator.processSample(0.0f) * 0.03f * inst;
    float flt = flutterOscillator.processSample(0.0f) * 0.01f * inst;
    float mult = 1.0f + wow + flt;

    const FormantData* a = tables[i0];
    const FormantData* b = tables[i1];

    auto setBand = [&](IIRFilter& f, int bandIdx)
    {
        float cf = juce::jmap(t, a[bandIdx].frequency, b[bandIdx].frequency) * shift * mult;
        float q  = juce::jmap(t, a[bandIdx].q,         b[bandIdx].q);
        bandGains[bandIdx] = juce::jmap(t, a[bandIdx].gain, b[bandIdx].gain);
        cf = juce::jlimit(20.0f, (float)dspSpec.sampleRate * 0.49f, cf);
        q  = juce::jlimit(0.1f, 40.0f, q);
        f.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass(dspSpec.sampleRate, cf, q);
    };

    setBand(bands.b0, 0);
    setBand(bands.b1, 1);
    setBand(bands.b2, 2);
    setBand(bands.b3, 3);
}

void VocalTractFilterModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    // Check for modulation inputs and read CV values
    float vowelModCV = 0.0f, formantModCV = 0.0f, instabilityModCV = 0.0f, gainModCV = 0.0f;
    
    if (isParamInputConnected("vowelShape")) {
        const auto& vowelModBus = getBusBuffer(buffer, true, 1);
        if (vowelModBus.getNumChannels() > 0)
            vowelModCV = vowelModBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("formantShift")) {
        const auto& formantModBus = getBusBuffer(buffer, true, 2);
        if (formantModBus.getNumChannels() > 0)
            formantModCV = formantModBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("instability")) {
        const auto& instabilityModBus = getBusBuffer(buffer, true, 3);
        if (instabilityModBus.getNumChannels() > 0)
            instabilityModCV = instabilityModBus.getReadPointer(0)[0];
    }
    
    if (isParamInputConnected("formantGain")) {
        const auto& gainModBus = getBusBuffer(buffer, true, 4);
        if (gainModBus.getNumChannels() > 0)
            gainModCV = gainModBus.getReadPointer(0)[0];
    }

    // Apply modulation to parameters
    float vowelShape = vowelShapeParam ? vowelShapeParam->load() : 0.0f;
    float formantShift = formantShiftParam ? formantShiftParam->load() : 0.0f;
    float instability = instabilityParam ? instabilityParam->load() : 0.0f;
    float outputGain = outputGainParam ? outputGainParam->load() : 0.0f;
    
    if (isParamInputConnected("vowelShape")) {
        vowelShape = juce::jlimit(0.0f, 4.0f, vowelShape + (vowelModCV - 0.5f) * 2.0f);
    }
    if (isParamInputConnected("formantShift")) {
        formantShift = juce::jlimit(-1.0f, 1.0f, formantShift + (formantModCV - 0.5f) * 2.0f);
    }
    if (isParamInputConnected("instability")) {
        instability = juce::jlimit(0.0f, 1.0f, instability + (instabilityModCV - 0.5f) * 0.5f);
    }
    if (isParamInputConnected("formantGain")) {
        outputGain = juce::jlimit(-24.0f, 24.0f, outputGain + (gainModCV - 0.5f) * 48.0f);
    }

    // Store live modulated values for UI display
    setLiveParamValue("vowelShape_live", vowelShape);
    setLiveParamValue("formantShift_live", formantShift);
    setLiveParamValue("instability_live", instability);
    setLiveParamValue("formantGain_live", outputGain);

    // Update coefficients every block so UI changes apply
    updateCoefficients(vowelShape, formantShift, instability);

    auto in  = getBusBuffer(buffer, true, 0);
    auto out = getBusBuffer(buffer, false, 0);
    if (in.getNumChannels() == 0 || out.getNumChannels() < 1)
    { out.clear(); return; }

    ensureWorkBuffers(out.getNumChannels(), numSamples);

    // Fan-out mono input to all workBuffer channels
    for (int ch = 0; ch < workBuffer.getNumChannels(); ++ch)
        workBuffer.copyFrom(ch, 0, in, 0, 0, numSamples);

    // Sum of bands
    sumBuffer.clear();
    auto processBand = [&](IIRFilter& f, float g)
    {
        tmpBuffer.makeCopyOf(workBuffer);
        juce::dsp::AudioBlock<float> b(tmpBuffer);
        juce::dsp::ProcessContextReplacing<float> ctx(b);
        f.process(ctx);
        tmpBuffer.applyGain(g);
        for (int ch = 0; ch < sumBuffer.getNumChannels(); ++ch)
            sumBuffer.addFrom(ch, 0, tmpBuffer, ch, 0, numSamples);
    };

    processBand(bands.b0, bandGains[0]);
    processBand(bands.b1, bandGains[1]);
    processBand(bands.b2, bandGains[2]);
    processBand(bands.b3, bandGains[3]);

    // Output with gain
    float outGain = juce::Decibels::decibelsToGain(juce::jlimit(-24.0f, 24.0f, outputGain));
    sumBuffer.applyGain(outGain);
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        out.copyFrom(ch, 0, sumBuffer, ch % sumBuffer.getNumChannels(), 0, numSamples);
    
    // Update lastOutputValues for cable inspector
    if (!lastOutputValues.empty() && lastOutputValues[0])
    {
        lastOutputValues[0]->store(out.getSample(0, out.getNumSamples() - 1));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalTractFilterModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back(std::make_unique<juce::AudioParameterFloat>("vowelShape",   "Vowel Shape",   0.0f, 4.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("formantShift", "Formant Shift", -1.0f, 1.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("instability",  "Instability",  0.0f, 1.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("formantGain",  "Formant Gain", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

#if defined(PRESET_CREATOR_UI)
void VocalTractFilterModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    if (!vowelShapeParam || !formantShiftParam || !instabilityParam || !outputGainParam) return;
    ImGui::PushItemWidth(itemWidth);
    
    // Vowel Shape
    bool isVowelModulated = isParamModulated("vowelShape");
    float v = vowelShapeParam->load();
    if (isVowelModulated) {
        v = getLiveParamValueFor("vowelShape", "vowelShape_live", v);
        ImGui::BeginDisabled();
    }
    
    if (ImGui::SliderFloat("Vowel", &v, 0.0f, 4.0f, "%.1f")) {
        if (!isVowelModulated) { *vowelShapeParam = v; if (onModificationEnded) onModificationEnded(); }
    }
    if (!isVowelModulated) adjustParamOnWheel(apvts.getParameter("vowelShape"), "vowelShape", v);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isVowelModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    // Formant Shift
    bool isFormantModulated = isParamModulated("formantShift");
    float s = formantShiftParam->load();
    if (isFormantModulated) {
        s = getLiveParamValueFor("formantShift", "formantShift_live", s);
        ImGui::BeginDisabled();
    }
    
    if (ImGui::SliderFloat("Formant", &s, -1.0f, 1.0f, "%.2f")) {
        if (!isFormantModulated) { *formantShiftParam = s; if (onModificationEnded) onModificationEnded(); }
    }
    if (!isFormantModulated) adjustParamOnWheel(apvts.getParameter("formantShift"), "formantShift", s);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isFormantModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    // Instability
    bool isInstabilityModulated = isParamModulated("instability");
    float i = instabilityParam->load();
    if (isInstabilityModulated) {
        i = getLiveParamValueFor("instability", "instability_live", i);
        ImGui::BeginDisabled();
    }
    
    if (ImGui::SliderFloat("Instab", &i, 0.0f, 1.0f, "%.2f")) {
        if (!isInstabilityModulated) { *instabilityParam = i; if (onModificationEnded) onModificationEnded(); }
    }
    if (!isInstabilityModulated) adjustParamOnWheel(apvts.getParameter("instability"), "instability", i);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isInstabilityModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    // Gain
    bool isGainModulated = isParamModulated("formantGain");
    float g = outputGainParam->load();
    if (isGainModulated) {
        g = getLiveParamValueFor("formantGain", "formantGain_live", g);
        ImGui::BeginDisabled();
    }
    
    if (ImGui::SliderFloat("Gain", &g, -24.0f, 24.0f, "%.1f dB")) {
        if (!isGainModulated) { *outputGainParam = g; if (onModificationEnded) onModificationEnded(); }
    }
    if (!isGainModulated) adjustParamOnWheel(apvts.getParameter("formantGain"), "formantGain", g);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isGainModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    
    ImGui::PopItemWidth();
}

void VocalTractFilterModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Audio In", 0);
    
    // Modulation input pins
    int busIdx, chanInBus;
    if (getParamRouting("vowelShape", busIdx, chanInBus))
        helpers.drawAudioInputPin("Vowel Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("formantShift", busIdx, chanInBus))
        helpers.drawAudioInputPin("Formant Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("instability", busIdx, chanInBus))
        helpers.drawAudioInputPin("Instability Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting("formantGain", busIdx, chanInBus))
        helpers.drawAudioInputPin("Gain Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    
    helpers.drawAudioOutputPin("Audio Out", 0);
}
#endif

bool VocalTractFilterModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outChannelIndexInBus = 0;
    if (paramId == "vowelShape") { outBusIndex = 1; return true; }
    if (paramId == "formantShift") { outBusIndex = 2; return true; }
    if (paramId == "instability") { outBusIndex = 3; return true; }
    if (paramId == "formantGain") { outBusIndex = 4; return true; }
    return false;
}
