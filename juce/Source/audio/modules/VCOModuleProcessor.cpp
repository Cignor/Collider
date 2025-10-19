#include "VCOModuleProcessor.h"

VCOModuleProcessor::VCOModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput("Inputs", juce::AudioChannelSet::discreteChannels(3), true) // ch0: Freq Mod, ch1: Wave Mod, ch2: Gate
                        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "VCOParams", createParameterLayout())
{
    frequencyParam = apvts.getRawParameterValue(paramIdFrequency);
    waveformParam  = apvts.getRawParameterValue(paramIdWaveform);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));

    oscillator.initialise([](float x) { return std::sin(x); }, 128);
}

juce::AudioProcessorValueTreeState::ParameterLayout VCOModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdFrequency, "Frequency",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f), 440.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdWaveform, "Waveform",
        juce::StringArray { "Sine", "Sawtooth", "Square" }, 0));
    return { params.begin(), params.end() };
}

void VCOModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    oscillator.prepare(spec);
}

void VCOModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto outBus = getBusBuffer(buffer, false, 0);

    auto inBus = getBusBuffer(buffer, true, 0);
    const float* freqCV = (inBus.getNumChannels() > 0) ? inBus.getReadPointer(0) : nullptr;
    const float* waveCV = (inBus.getNumChannels() > 1) ? inBus.getReadPointer(1) : nullptr;
    const float* gateCV = (inBus.getNumChannels() > 2) ? inBus.getReadPointer(2) : nullptr;

    const bool freqActive = isParamInputConnected(paramIdFrequency);
    const bool waveActive = isParamInputConnected(paramIdWaveformMod);
    const bool gateActive = isParamInputConnected("gate_mod");

#if defined(PRESET_CREATOR_UI)
    {
        static int dbgCounter = 0;
        if ((dbgCounter++ & 0x3F) == 0)
        {
            const float s0 = (freqCV && buffer.getNumSamples() > 0) ? freqCV[0] : 0.0f;
            const float s1 = (freqCV && buffer.getNumSamples() > 1) ? freqCV[1] : 0.0f;
            juce::Logger::writeToLog(
                juce::String("[VCO] inCh=") + juce::String(inBus.getNumChannels()) +
                " freqRMS=" + juce::String((inBus.getNumChannels()>0)?inBus.getRMSLevel(0,0,buffer.getNumSamples()):0.0f) +
                " s0=" + juce::String(s0) + " s1=" + juce::String(s1));
        }
    }
#endif

    const float baseFrequency = frequencyParam != nullptr ? frequencyParam->load() : 440.0f;
    const int baseWaveform = (int) (waveformParam != nullptr ? waveformParam->load() : 0.0f);

    // Define smoothing factor for click-free gating
    constexpr float GATE_SMOOTHING_FACTOR = 0.002f;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        int waveform = baseWaveform;
        if (waveActive)
        {
            const float cvRaw = waveCV[i];
            const float cv01  = juce::jlimit(0.0f, 1.0f, (cvRaw + 1.0f) * 0.5f);
            waveform = (int) (cv01 * 2.99f);
        }

        float freq = baseFrequency;
        if (freqActive)
        {
            const float cvRaw = freqCV[i];
            // Normalize CV: prefer unipolar [0,1]; if outside, treat as bipolar [-1,1]
            const float cv01  = (cvRaw >= 0.0f && cvRaw <= 1.0f)
                                ? juce::jlimit(0.0f, 1.0f, cvRaw)
                                : juce::jlimit(0.0f, 1.0f, (cvRaw + 1.0f) * 0.5f);
            // Absolute mapping: 20 Hz .. 20000 Hz (log scale)
            constexpr float fMin = 20.0f;
            constexpr float fMax = 20000.0f;
            const float spanOct = std::log2(fMax / fMin);
            freq = fMin * std::pow(2.0f, cv01 * spanOct);
        }
        freq = juce::jlimit(20.0f, 20000.0f, freq);

        if (currentWaveform != waveform)
        {
            if (waveform == 0)      oscillator.initialise([](float x){ return std::sin(x); }, 128);
            else if (waveform == 1) oscillator.initialise([](float x){ return (x / juce::MathConstants<float>::pi); }, 128);
            else                    oscillator.initialise([](float x){ return x < 0.0f ? -1.0f : 1.0f; }, 128);
            currentWaveform = waveform;
        }

        oscillator.setFrequency(freq, false);
        const float s = oscillator.processSample(0.0f);
        
        // Apply gate with click-free smoothing
        float targetGate = gateActive ? gateCV[i] : 1.0f;
        smoothedGate += (targetGate - smoothedGate) * GATE_SMOOTHING_FACTOR;
        const float finalSample = s * smoothedGate;
        
        outBus.setSample(0, i, finalSample);

        if ((i & 0x3F) == 0)
        {
            setLiveParamValue(paramIdFrequency, freq);
            setLiveParamValue(paramIdWaveform, (float) waveform);
        }
    }
}

bool VCOModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All inputs are on the same bus
    if (paramId == paramIdFrequency)   { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdWaveformMod) { outChannelIndexInBus = 1; return true; }
    if (paramId == "gate_mod")         { outChannelIndexInBus = 2; return true; }
    return false;
}


