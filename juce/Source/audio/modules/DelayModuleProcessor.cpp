#include "DelayModuleProcessor.h"

DelayModuleProcessor::DelayModuleProcessor()
    : ModuleProcessor (BusesProperties()
        .withInput ("In", juce::AudioChannelSet::stereo(), true)
        .withInput ("Time Mod", juce::AudioChannelSet::mono(), true)
        .withInput ("Feedback Mod", juce::AudioChannelSet::mono(), true)
        .withInput ("Mix Mod", juce::AudioChannelSet::mono(), true)
        .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DelayParams", createParameterLayout())
{
    timeMsParam   = apvts.getRawParameterValue ("timeMs");
    feedbackParam = apvts.getRawParameterValue ("feedback");
    mixParam      = apvts.getRawParameterValue ("mix");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
    
    // Initialize smoothed values
    timeSm.reset(400.0f);
    feedbackSm.reset(0.4f);
    mixSm.reset(0.3f);
}

juce::AudioProcessorValueTreeState::ParameterLayout DelayModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("timeMs",  "Time (ms)", juce::NormalisableRange<float> (1.0f, 2000.0f, 0.01f, 0.4f), 400.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("feedback","Feedback",  juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",     "Mix",       juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    return { p.begin(), p.end() };
}

void DelayModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sr = sampleRate;
    maxDelaySamples = (int) std::ceil (2.0 * sr); // allow up to 2s safely
    dlL.setMaximumDelayInSamples (maxDelaySamples);
    dlR.setMaximumDelayInSamples (maxDelaySamples);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) samplesPerBlock, 1 };
    dlL.prepare (spec);
    dlR.prepare (spec);
    dlL.reset(); dlR.reset();
    
    // Set smoothing time for parameters (20ms for delay time, 10ms for others)
    timeSm.reset(sampleRate, 0.02);
    feedbackSm.reset(sampleRate, 0.01);
    mixSm.reset(sampleRate, 0.01);
    
    juce::Logger::writeToLog ("[Delay] prepare sr=" + juce::String (sr) + " maxSamps=" + juce::String (maxDelaySamples));
}

void DelayModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    // PER-SAMPLE FIX: Get pointers to modulation CV inputs, if they are connected
    const bool isTimeMod = isParamInputConnected("timeMs");
    const bool isFeedbackMod = isParamInputConnected("feedback");
    const bool isMixMod = isParamInputConnected("mix");

    const float* timeCV = isTimeMod ? getBusBuffer(buffer, true, 1).getReadPointer(0) : nullptr;
    const float* feedbackCV = isFeedbackMod ? getBusBuffer(buffer, true, 2).getReadPointer(0) : nullptr;
    const float* mixCV = isMixMod ? getBusBuffer(buffer, true, 3).getReadPointer(0) : nullptr;

    // Get base parameter values ONCE
    const float baseTimeMs = timeMsParam != nullptr ? timeMsParam->load() : 400.0f;
    const float baseFeedback = feedbackParam != nullptr ? feedbackParam->load() : 0.4f;
    const float baseMix = mixParam != nullptr ? mixParam->load() : 0.3f;

    // Variables to store last calculated values for UI feedback
    float lastTimeMs = baseTimeMs;
    float lastFeedback = baseFeedback;
    float lastMix = baseMix;

    auto processChannel = [&] (int ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // PER-SAMPLE FIX: Calculate effective parameters FOR THIS SAMPLE
            float timeMs = baseTimeMs;
            if (isTimeMod && timeCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, timeCV[i]);
                // ADDITIVE MODULATION FIX: Add CV offset to base delay time
                const float octaveRange = 3.0f; // CV can modulate +/- 3 octaves of delay time
                const float octaveOffset = (cv - 0.5f) * octaveRange; // Center around 0, range [-1.5, +1.5] octaves
                timeMs = baseTimeMs * std::pow(2.0f, octaveOffset);
                timeMs = juce::jlimit(1.0f, 2000.0f, timeMs);
            }
            
            // Apply smoothing to delay time to prevent clicks
            timeSm.setTargetValue(timeMs);
            timeMs = timeSm.getNextValue();
            
            // Store for UI feedback
            lastTimeMs = timeMs;
            
            float fb = baseFeedback;
            if (isFeedbackMod && feedbackCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, feedbackCV[i]);
                // ADDITIVE MODULATION FIX: Add CV offset to base feedback
                const float feedbackRange = 0.3f; // CV can modulate feedback by +/- 0.3
                const float feedbackOffset = (cv - 0.5f) * feedbackRange; // Center around 0
                fb = baseFeedback + feedbackOffset;
                fb = juce::jlimit(0.0f, 0.95f, fb);
            }
            
            // Apply smoothing to feedback to prevent zipper noise
            feedbackSm.setTargetValue(fb);
            fb = feedbackSm.getNextValue();
            
            // Store for UI feedback
            lastFeedback = fb;
            
            float mix = baseMix;
            if (isMixMod && mixCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, mixCV[i]);
                // ADDITIVE MODULATION FIX: Add CV offset to base mix
                const float mixRange = 0.5f; // CV can modulate mix by +/- 0.5
                const float mixOffset = (cv - 0.5f) * mixRange; // Center around 0
                mix = baseMix + mixOffset;
                mix = juce::jlimit(0.0f, 1.0f, mix);
            }
            
            // Apply smoothing to mix to prevent zipper noise
            mixSm.setTargetValue(mix);
            mix = mixSm.getNextValue();
            
            // Store for UI feedback
            lastMix = mix;
            
            // Calculate delay samples for this sample
            float delaySamps = (timeMs / 1000.0f) * (float) sr;
            delaySamps = juce::jlimit (1.0f, (float) (maxDelaySamples - 1), delaySamps);
            
            // Set delay for this sample
            dlL.setDelay (delaySamps); dlR.setDelay (delaySamps);
            
            // Process this sample
            const float in = d[i];
            const float delayed = (ch == 0 ? dlL.popSample (0, delaySamps) : dlR.popSample (0, delaySamps));
            if (ch == 0) dlL.pushSample (0, in + delayed * fb); else dlR.pushSample (0, in + delayed * fb);
            d[i] = in * (1.0f - mix) + delayed * mix;
        }
    };
    processChannel (0);
    if (buffer.getNumChannels() > 1) processChannel (1);
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(buffer.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1]) lastOutputValues[1]->store(buffer.getSample(1, buffer.getNumSamples() - 1));
    }

    // Store live modulated values for UI display
    setLiveParamValue("timeMs_live", lastTimeMs);
    setLiveParamValue("feedback_live", lastFeedback);
    setLiveParamValue("mix_live", lastMix);
}

// Parameter bus contract implementation
bool DelayModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outChannelIndexInBus = 0;
    if (paramId == "timeMs")   { outBusIndex = 1; return true; }
    if (paramId == "feedback") { outBusIndex = 2; return true; }
    if (paramId == "mix")      { outBusIndex = 3; return true; }
    return false;
}


