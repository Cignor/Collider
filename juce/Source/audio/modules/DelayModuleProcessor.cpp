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
    relativeTimeModParam = apvts.getRawParameterValue("relativeTimeMod");
    relativeFeedbackModParam = apvts.getRawParameterValue("relativeFeedbackMod");
    relativeMixModParam = apvts.getRawParameterValue("relativeMixMod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
    
    // Initialize smoothed values
    timeSm.reset(400.0f);
    feedbackSm.reset(0.4f);
    mixSm.reset(0.3f);
    
    // Initialize visualization data
    for (auto& w : vizData.inputWaveformL) w.store(0.0f);
    for (auto& w : vizData.inputWaveformR) w.store(0.0f);
    for (auto& w : vizData.outputWaveformL) w.store(0.0f);
    for (auto& w : vizData.outputWaveformR) w.store(0.0f);
    for (auto& p : vizData.tapPositions) p.store(-1.0f); // -1 = inactive
    for (auto& l : vizData.tapLevels) l.store(0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout DelayModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("timeMs",  "Time (ms)", juce::NormalisableRange<float> (1.0f, 2000.0f, 0.01f, 0.4f), 400.0f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("feedback","Feedback",  juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f));
    p.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",     "Mix",       juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f));
    
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeTimeMod", "Relative Time Mod", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeFeedbackMod", "Relative Feedback Mod", true));
    p.push_back (std::make_unique<juce::AudioParameterBool>("relativeMixMod", "Relative Mix Mod", true));
    
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
    
    // Initialize visualization buffers
    vizInputBuffer.setSize(2, vizBufferSize);
    vizOutputBuffer.setSize(2, vizBufferSize);
    vizDryBuffer.setSize(2, vizBufferSize);
    vizInputBuffer.clear();
    vizOutputBuffer.clear();
    vizDryBuffer.clear();
    vizWritePos = 0;
    
    juce::Logger::writeToLog ("[Delay] prepare sr=" + juce::String (sr) + " maxSamps=" + juce::String (maxDelaySamples));
}

void DelayModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
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
    const bool relativeTimeMode = relativeTimeModParam && relativeTimeModParam->load() > 0.5f;
    const bool relativeFeedbackMode = relativeFeedbackModParam && relativeFeedbackModParam->load() > 0.5f;
    const bool relativeMixMode = relativeMixModParam && relativeMixModParam->load() > 0.5f;

    // Variables to store last calculated values for UI feedback
    float lastTimeMs = baseTimeMs;
    float lastFeedback = baseFeedback;
    float lastMix = baseMix;
    
    // Store dry signal for visualization (before processing)
    juce::AudioBuffer<float> dryBuffer;
    dryBuffer.makeCopyOf(outBus);

    auto processChannel = [&] (int ch)
    {
        float* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // PER-SAMPLE FIX: Calculate effective parameters FOR THIS SAMPLE
            float timeMs = baseTimeMs;
            if (isTimeMod && timeCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, timeCV[i]);
                
                if (relativeTimeMode) {
                    // RELATIVE: CV modulates around base time (±3 octaves)
                    const float octaveRange = 3.0f;
                    const float octaveOffset = (cv - 0.5f) * octaveRange;
                    timeMs = baseTimeMs * std::pow(2.0f, octaveOffset);
                } else {
                    // ABSOLUTE: CV directly maps to full time range
                    timeMs = juce::jmap(cv, 1.0f, 2000.0f);
                }
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
                
                if (relativeFeedbackMode) {
                    // RELATIVE: CV adds offset to base feedback (±0.5)
                    const float feedbackRange = 1.0f;
                    const float feedbackOffset = (cv - 0.5f) * feedbackRange;
                    fb = baseFeedback + feedbackOffset;
                } else {
                    // ABSOLUTE: CV directly sets feedback
                    fb = juce::jmap(cv, 0.0f, 0.95f);
                }
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
                
                if (relativeMixMode) {
                    // RELATIVE: CV adds offset to base mix (±0.5)
                    const float mixRange = 1.0f;
                    const float mixOffset = (cv - 0.5f) * mixRange;
                    mix = baseMix + mixOffset;
                } else {
                    // ABSOLUTE: CV directly sets mix
                    mix = cv;
                }
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
            
            // Record to visualization buffers (circular, only left channel to avoid redundancy)
            if (ch == 0)
            {
                // Record input (before delay processing)
                vizInputBuffer.setSample(0, vizWritePos, in);
                // Record output (after delay + mix)
                vizOutputBuffer.setSample(0, vizWritePos, d[i]);
                // Record dry signal (original input)
                vizDryBuffer.setSample(0, vizWritePos, dryBuffer.getSample(0, i));
                if (vizInputBuffer.getNumChannels() > 1 && vizOutputBuffer.getNumChannels() > 1 && dryBuffer.getNumChannels() > 1)
                {
                    // Right channel
                    const float inR = inBus.getNumChannels() > 1 ? inBus.getSample(1, i) : 0.0f;
                    vizInputBuffer.setSample(1, vizWritePos, inR);
                    vizOutputBuffer.setSample(1, vizWritePos, outBus.getSample(1, i));
                    vizDryBuffer.setSample(1, vizWritePos, dryBuffer.getSample(1, i));
                }
                vizWritePos = (vizWritePos + 1) % vizBufferSize;
                
                // Update visualization data (throttled - every 64 samples, like BitCrusher)
                if ((i & 0x3F) == 0)
                {
                    // Update current parameter state
                    vizData.currentTimeMs.store(timeMs);
                    vizData.currentFeedback.store(fb);
                    vizData.currentMix.store(mix);
                    
                    // Update waveform snapshots (downsampled from circular buffer)
                    const int step = vizBufferSize / VizData::waveformPoints;
                    for (int j = 0; j < VizData::waveformPoints; ++j)
                    {
                        int idx = (vizWritePos - (VizData::waveformPoints - j) * step + vizBufferSize) % vizBufferSize;
                        vizData.inputWaveformL[j].store(vizInputBuffer.getSample(0, idx));
                        vizData.outputWaveformL[j].store(vizOutputBuffer.getSample(0, idx));
                        if (vizInputBuffer.getNumChannels() > 1 && vizOutputBuffer.getNumChannels() > 1)
                        {
                            vizData.inputWaveformR[j].store(vizInputBuffer.getSample(1, idx));
                            vizData.outputWaveformR[j].store(vizOutputBuffer.getSample(1, idx));
                        }
                    }
                    
                    // Calculate delay tap positions and levels
                    // Taps occur at multiples of delay time, with exponential decay
                    const float delayTimeNorm = timeMs / 2000.0f; // Normalize to 0-1 (max 2000ms)
                    int activeTapCount = 0;
                    for (int tap = 0; tap < 8 && activeTapCount < 8; ++tap)
                    {
                        const float tapLevel = fb * std::pow(fb, (float)tap);
                        if (tapLevel > 0.01f) // Only show taps above 1% level
                        {
                            // Position: each tap is offset by delay time
                            const float tapPosition = 1.0f - (delayTimeNorm * (float)(tap + 1));
                            if (tapPosition >= 0.0f && tapPosition <= 1.0f)
                            {
                                vizData.tapPositions[activeTapCount].store(tapPosition);
                                vizData.tapLevels[activeTapCount].store(tapLevel);
                                activeTapCount++;
                            }
                        }
                    }
                    vizData.activeTapCount.store(activeTapCount);
                    
                    // Clear inactive tap slots
                    for (int j = activeTapCount; j < 8; ++j)
                    {
                        vizData.tapPositions[j].store(-1.0f);
                        vizData.tapLevels[j].store(0.0f);
                    }
                }
            }
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

std::vector<DynamicPinInfo> DelayModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio inputs (bus 0, channels 0-1)
    pins.push_back({"In L", 0, PinDataType::Audio});
    pins.push_back({"In R", 1, PinDataType::Audio});
    
    // Modulation inputs (separate buses, but appear as channels 2-4 in the editor)
    pins.push_back({"Time Mod", 2, PinDataType::CV});
    pins.push_back({"Feedback Mod", 3, PinDataType::CV});
    pins.push_back({"Mix Mod", 4, PinDataType::CV});
    
    return pins;
}

std::vector<DynamicPinInfo> DelayModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    // Audio outputs (channels 0-1)
    pins.push_back({"Out L", 0, PinDataType::Audio});
    pins.push_back({"Out R", 1, PinDataType::Audio});
    
    return pins;
}


