#include "ShapingOscillatorModuleProcessor.h"

juce::AudioProcessorValueTreeState::ParameterLayout ShapingOscillatorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdFrequency, "Frequency",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 440.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdWaveform, "Waveform",
        juce::StringArray { "Sine", "Saw", "Square" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdDrive, "Drive",
        juce::NormalisableRange<float>(1.0f, 50.0f, 0.01f, 0.5f), 1.0f));

    return { params.begin(), params.end() };
}

ShapingOscillatorModuleProcessor::ShapingOscillatorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(5), true)
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "ShapingOscillatorParams", createParameterLayout())
{
    frequencyParam = apvts.getRawParameterValue(paramIdFrequency);
    waveformParam  = apvts.getRawParameterValue(paramIdWaveform);
    driveParam     = apvts.getRawParameterValue(paramIdDrive);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));

    oscillator.initialise([](float x){ return std::sin(x); }, 128);
}

void ShapingOscillatorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    oscillator.prepare(spec);

    smoothedFrequency.reset(sampleRate, 0.01);
    smoothedDrive.reset(sampleRate, 0.01);
}

void ShapingOscillatorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto inBus  = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const bool isFreqMod  = isParamInputConnected(paramIdFrequencyMod);
    const bool isWaveMod  = isParamInputConnected(paramIdWaveformMod);
    const bool isDriveMod = isParamInputConnected(paramIdDriveMod);

    const float* audioInL = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;
    const float* freqCV   = isFreqMod  && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* waveCV   = isWaveMod  && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* driveCV  = isDriveMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;

    auto* out = outBus.getWritePointer(0);

    const float baseFrequency = frequencyParam != nullptr ? frequencyParam->load() : 440.0f;
    const int   baseWaveform  = waveformParam  != nullptr ? (int) waveformParam->load()  : 0;
    const float baseDrive     = driveParam     != nullptr ? driveParam->load()     : 1.0f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float currentFreq = baseFrequency;
        if (isFreqMod && freqCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, freqCV[i]);
            constexpr float fMin = 20.0f;
            constexpr float fMax = 20000.0f;
            const float spanOct = std::log2(fMax / fMin);
            currentFreq = fMin * std::pow(2.0f, cv * spanOct);
        }

        int currentWave = baseWaveform;
        if (isWaveMod && waveCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, waveCV[i]);
            currentWave = (int) (cv * 2.99f);
        }

        float currentDrive = baseDrive;
        if (isDriveMod && driveCV)
        {
            const float cv = juce::jlimit(0.0f, 1.0f, driveCV[i]);
            currentDrive = juce::jmap(cv, 1.0f, 50.0f);
        }

        smoothedFrequency.setTargetValue(currentFreq);
        smoothedDrive.setTargetValue(currentDrive);

        if (currentWaveform != currentWave)
        {
            if (currentWave == 0)      oscillator.initialise([](float x){ return std::sin(x); }, 128);
            else if (currentWave == 1) oscillator.initialise([](float x){ return (x / juce::MathConstants<float>::pi); }, 128);
            else                       oscillator.initialise([](float x){ return x < 0.0f ? -1.0f : 1.0f; }, 128);
            currentWaveform = currentWave;
        }

        oscillator.setFrequency(smoothedFrequency.getNextValue(), false);
        const float osc = oscillator.processSample(0.0f);
        const float shaped = std::tanh(osc * smoothedDrive.getNextValue());

        const float inL = audioInL ? audioInL[i] : 1.0f;
        const float outSample = shaped * inL;

        out[i] = outSample;

        if ((i & 0x3F) == 0)
        {
            setLiveParamValue("frequency_live", smoothedFrequency.getCurrentValue());
            setLiveParamValue("waveform_live", (float) currentWave);
            setLiveParamValue("drive_live", smoothedDrive.getCurrentValue());
        }
    }

    if (lastOutputValues.size() >= 1)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
    }
}

#if defined(PRESET_CREATOR_UI)
void ShapingOscillatorModuleProcessor::drawParametersInNode (float itemWidth,
                                                      const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                      const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    const bool freqIsMod = isParamModulated(paramIdFrequencyMod);
    float freq = freqIsMod ? getLiveParamValueFor(paramIdFrequencyMod, "frequency_live", frequencyParam ? frequencyParam->load() : 440.0f)
                           : (frequencyParam ? frequencyParam->load() : 440.0f);

    int wave = (int) (waveformParam ? waveformParam->load() : 0.0f);
    if (isParamModulated(paramIdWaveformMod))
        wave = (int) getLiveParamValueFor(paramIdWaveformMod, "waveform_live", (float) wave);

    const bool driveIsMod = isParamModulated(paramIdDriveMod);
    float drive = driveIsMod ? getLiveParamValueFor(paramIdDriveMod, "drive_live", driveParam ? driveParam->load() : 1.0f)
                             : (driveParam ? driveParam->load() : 1.0f);

    ImGui::PushItemWidth(itemWidth);

    if (freqIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Frequency", &freq, 20.0f, 20000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic))
    {
        if (!freqIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdFrequency))) *p = freq;
    }
    if (!freqIsMod) adjustParamOnWheel(ap.getParameter(paramIdFrequency), "frequencyHz", freq);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (freqIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    const bool waveIsMod = isParamModulated(paramIdWaveformMod);
    if (waveIsMod) ImGui::BeginDisabled();
    if (ImGui::Combo("Waveform", &wave, "Sine\0Saw\0Square\0\0"))
    {
        if (!waveIsMod) if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdWaveform))) *p = wave;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (waveIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    if (driveIsMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Drive", &drive, 1.0f, 50.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
    {
        if (!driveIsMod) if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdDrive))) *p = drive;
    }
    if (!driveIsMod) adjustParamOnWheel(ap.getParameter(paramIdDrive), "drive", drive);
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
    if (driveIsMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::PopItemWidth();
}

void ShapingOscillatorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    helpers.drawAudioInputPin("Freq Mod", 2);
    helpers.drawAudioInputPin("Wave Mod", 3);
    helpers.drawAudioInputPin("Drive Mod", 4);

    helpers.drawAudioOutputPin("Out", 0);
}

juce::String ShapingOscillatorModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Freq Mod";
        case 3: return "Wave Mod";
        case 4: return "Drive Mod";
        default: return {};
    }
}

juce::String ShapingOscillatorModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Out";
        default: return {};
    }
}

#endif

bool ShapingOscillatorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdFrequencyMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdWaveformMod)  { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdDriveMod)     { outChannelIndexInBus = 4; return true; }
    return false;
}
