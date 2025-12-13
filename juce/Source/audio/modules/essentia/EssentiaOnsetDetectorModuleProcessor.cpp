#include "EssentiaOnsetDetectorModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

EssentiaOnsetDetectorModuleProcessor::EssentiaOnsetDetectorModuleProcessor()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Onset", juce::AudioChannelSet::mono(), true)
                      .withOutput("Velocity", juce::AudioChannelSet::mono(), true)
                      .withOutput("Confidence", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "EssentiaOnsetDetectorParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue(paramIdThreshold);
    minIntervalParam = apvts.getRawParameterValue(paramIdMinInterval);
    sensitivityParam = apvts.getRawParameterValue(paramIdSensitivity);
    methodParam = apvts.getRawParameterValue(paramIdMethod);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    
    analysisBuffer.resize(ANALYSIS_BUFFER_SIZE, 0.0f);
    
#if defined(PRESET_CREATOR_UI)
    vizInputBuffer.setSize(1, vizBufferSize);
    vizInputBuffer.clear();
#endif
}

EssentiaOnsetDetectorModuleProcessor::~EssentiaOnsetDetectorModuleProcessor()
{
    shutdownEssentiaAlgorithms();
}

juce::AudioProcessorValueTreeState::ParameterLayout EssentiaOnsetDetectorModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdThreshold, "Threshold",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 1.0f), 0.3f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdMinInterval, "Min Interval",
        juce::NormalisableRange<float>(0.0f, 1000.0f, 1.0f, 0.25f), 50.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdSensitivity, "Sensitivity",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f, 1.0f), 0.5f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdMethod, "Method",
        juce::StringArray { "Energy", "Spectral", "Complex", "HFC", "Phase" }, 0));
    
    return { params.begin(), params.end() };
}

void EssentiaOnsetDetectorModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    
    // Initialize Essentia wrapper
    EssentiaWrapper::initializeEssentia();
    
    // Initialize Essentia algorithms
    initializeEssentiaAlgorithms();
    
    bufferWritePos = 0;
    samplesSinceAnalysis = 0;
    lastOnsetTime = -1.0f;
    absoluteSamplePos = 0.0f;
    pendingOnsets.clear();
    analysisBuffer.assign(ANALYSIS_BUFFER_SIZE, 0.0f);
    
#if defined(PRESET_CREATOR_UI)
    vizInputBuffer.clear();
    vizWritePos = 0;
    for (int i = 0; i < VizData::waveformPoints; ++i)
        vizData.inputWaveform[i].store(0.0f);
    vizData.onsetGateLevel.store(0.0f);
    vizData.velocityLevel.store(0.0f);
    vizData.confidenceLevel.store(0.0f);
    vizData.detectedOnsets.store(0.0f);
#endif
}

void EssentiaOnsetDetectorModuleProcessor::releaseResources()
{
    shutdownEssentiaAlgorithms();
}

void EssentiaOnsetDetectorModuleProcessor::initializeEssentiaAlgorithms()
{
#ifdef ESSENTIA_FOUND
    if (!EssentiaWrapper::isInitialized())
    {
        juce::Logger::writeToLog("[Essentia Onset] Essentia not initialized, using fallback detection");
        return;
    }
    
    try
    {
        essentia::standard::AlgorithmFactory& factory = essentia::standard::AlgorithmFactory::instance();
        
        // Create onset detector based on method
        const int method = (int)(methodParam != nullptr ? methodParam->load() : 0.0f);
        juce::String algorithmName;
        
        switch (method)
        {
            case 0: algorithmName = "OnsetRate"; break;
            case 1: algorithmName = "Onsets"; break;
            case 2: algorithmName = "OnsetRate"; break; // Complex uses same as Energy for now
            case 3: algorithmName = "OnsetRate"; break; // HFC
            case 4: algorithmName = "OnsetRate"; break; // Phase
            default: algorithmName = "OnsetRate"; break;
        }
        
        // OnsetRate requires 44100Hz sample rate (hardcoded in algorithm)
        // If sample rate doesn't match, we'll use fallback detection
        if (currentSampleRate == 44100.0)
        {
            // OnsetRate doesn't take constructor parameters - it's configured internally
            onsetDetector = factory.create(algorithmName.toStdString());
            juce::Logger::writeToLog("[Essentia Onset] Algorithm created: " + algorithmName + " at 44100Hz");
        }
        else
        {
            juce::Logger::writeToLog("[Essentia Onset] Sample rate is " + juce::String(currentSampleRate) + 
                                     "Hz, but OnsetRate requires 44100Hz. Using fallback detection.");
            onsetDetector = nullptr;
        }
        
        juce::Logger::writeToLog("[Essentia Onset] Algorithm created: " + algorithmName);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[Essentia Onset] ERROR creating algorithm: " + juce::String(e.what()));
        onsetDetector = nullptr;
    }
#else
    juce::Logger::writeToLog("[Essentia Onset] WARNING: ESSENTIA_FOUND not defined - using fallback detection");
#endif
}

void EssentiaOnsetDetectorModuleProcessor::shutdownEssentiaAlgorithms()
{
#ifdef ESSENTIA_FOUND
    if (onsetDetector)
    {
        delete onsetDetector;
        onsetDetector = nullptr;
    }
    pool.clear();
    onsetTimes.clear();
    onsetValues.clear();
#endif
}

void EssentiaOnsetDetectorModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    
    static int logCounter = 0;
    const bool shouldLog = (logCounter++ % 100) == 0; // Log every 100 blocks
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Onset] === processBlock START ===");

    auto inBus = getBusBuffer(buffer, true, 0);
    auto onsetBus = getBusBuffer(buffer, false, 0);
    auto velocityBus = getBusBuffer(buffer, false, 1);
    auto confidenceBus = getBusBuffer(buffer, false, 2);
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Onset] Input: channels=" + juce::String(inBus.getNumChannels()) + 
                                ", samples=" + juce::String(buffer.getNumSamples()) +
                                ", sampleRate=" + juce::String(currentSampleRate, 2));
    
    if (inBus.getNumChannels() == 0)
    {
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Onset] ERROR: No input channels, clearing and returning");
        buffer.clear();
        return;
    }
    
    const float* input = inBus.getReadPointer(0);
    float* onsetOut = onsetBus.getWritePointer(0);
    float* velocityOut = velocityBus.getWritePointer(0);
    float* confidenceOut = confidenceBus.getWritePointer(0);
    
    // CRITICAL: Copy input samples to local buffer BEFORE any processing
    // The input pointer may become invalid after other modules process the buffer
    juce::AudioBuffer<float> inputCopy(1, buffer.getNumSamples());
    inputCopy.copyFrom(0, 0, inBus, 0, 0, buffer.getNumSamples());
    const float* inputSamples = inputCopy.getReadPointer(0);
    
    // Check input signal level
    float inputRms = inBus.getRMSLevel(0, 0, buffer.getNumSamples());
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Onset] Input RMS: " + juce::String(inputRms, 6));
    
    const float threshold = thresholdParam != nullptr ? thresholdParam->load() : 0.3f;
    const float minIntervalMs = minIntervalParam != nullptr ? minIntervalParam->load() : 50.0f;
    const float minIntervalSamples = minIntervalMs * 0.001f * currentSampleRate;
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Onset] Parameters: threshold=" + juce::String(threshold, 3) + 
                                ", minInterval=" + juce::String(minIntervalMs, 1) + "ms");
    
    // Clear outputs
    onsetBus.clear();
    velocityBus.clear();
    confidenceBus.clear();
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Onset] Outputs cleared, starting processing...");
    
    // Accumulate audio into analysis buffer and track absolute sample position
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        analysisBuffer[bufferWritePos] = inputSamples[i];
        bufferWritePos = (bufferWritePos + 1) % ANALYSIS_BUFFER_SIZE;
    }
    
    // Run Essentia analysis when we have enough samples
    samplesSinceAnalysis += buffer.getNumSamples();
    bool shouldRunAnalysis = (samplesSinceAnalysis >= ANALYSIS_BUFFER_SIZE);
    
    if (shouldLog)
    {
        juce::Logger::writeToLog("[Essentia Onset] Buffer filled: writePos=" + juce::String(bufferWritePos) + 
                                ", samplesSinceAnalysis=" + juce::String(samplesSinceAnalysis) + 
                                ", absoluteSamplePos=" + juce::String(absoluteSamplePos));
        juce::Logger::writeToLog("[Essentia Onset] Analysis check: shouldRun=" + juce::String(shouldRunAnalysis ? "YES" : "NO"));
#ifdef ESSENTIA_FOUND
        juce::Logger::writeToLog("[Essentia Onset] Essentia status: detector=" + juce::String(onsetDetector != nullptr ? "OK" : "NULL") + 
                                ", initialized=" + juce::String(EssentiaWrapper::isInitialized() ? "YES" : "NO") +
                                ", sampleRate=" + juce::String(currentSampleRate, 2) + " (need 44100)");
#else
        juce::Logger::writeToLog("[Essentia Onset] Essentia: NOT COMPILED");
#endif
    }
    
#ifdef ESSENTIA_FOUND
    if (shouldRunAnalysis && onsetDetector && EssentiaWrapper::isInitialized() && currentSampleRate == 44100.0)
    {
        try
        {
            if (shouldLog)
                juce::Logger::writeToLog("[Essentia Onset] Running Essentia analysis...");
            
            // Convert analysis buffer to Essentia Real vector
            std::vector<essentia::Real> signal(analysisBuffer.begin(), analysisBuffer.end());
            
            if (shouldLog)
            {
                float signalRms = 0.0f;
                for (const auto& s : signal)
                    signalRms += s * s;
                signalRms = std::sqrt(signalRms / signal.size());
                juce::Logger::writeToLog("[Essentia Onset] Essentia input: samples=" + juce::String(signal.size()) + 
                                        ", RMS=" + juce::String(signalRms, 6));
            }
            
            // Prepare outputs
            std::vector<essentia::Real> onsets;
            essentia::Real onsetRate;
            
            // Set inputs and outputs
            onsetDetector->input("signal").set(signal);
            onsetDetector->output("onsets").set(onsets);
            onsetDetector->output("onsetRate").set(onsetRate);
            
            // Compute (this may take some time, but OnsetRate is relatively fast)
            onsetDetector->compute();
            
            if (shouldLog)
                juce::Logger::writeToLog("[Essentia Onset] Essentia compute() completed: onsets=" + 
                                        juce::String(onsets.size()) + 
                                        ", onsetRate=" + juce::String(onsetRate, 6));
            
            // Process detected onsets
            // Onset times are in seconds relative to the start of the signal buffer
            // We need to map them to the current audio buffer
            const float bufferStartTime = (absoluteSamplePos - ANALYSIS_BUFFER_SIZE) / currentSampleRate;
            
            int onsetsProcessed = 0;
            int onsetsInBuffer = 0;
            int onsetsPending = 0;
            int onsetsIgnored = 0;
            
            for (const auto& onsetTime : onsets)
            {
                // Convert onset time (seconds) to absolute sample position
                float onsetSamplePos = onsetTime * currentSampleRate;
                
                // Calculate position relative to current buffer start
                float relativeSamplePos = onsetSamplePos - (absoluteSamplePos - ANALYSIS_BUFFER_SIZE);
                
                // Check if onset is within current output buffer
                if (relativeSamplePos >= 0.0f && relativeSamplePos < buffer.getNumSamples())
                {
                    int sampleIdx = (int)relativeSamplePos;
                    if (sampleIdx >= 0 && sampleIdx < buffer.getNumSamples())
                    {
                        // Check min interval constraint
                        float timeSinceLast = (onsetSamplePos - lastOnsetTime) / currentSampleRate;
                        if (lastOnsetTime < 0.0f || timeSinceLast * 1000.0f > minIntervalMs)
                        {
                            onsetOut[sampleIdx] = 1.0f;
                            
                            // Estimate velocity and confidence
                            // Use sensitivity parameter to scale
                            float sensitivity = sensitivityParam != nullptr ? sensitivityParam->load() : 0.5f;
                            float velocity = juce::jlimit(0.0f, 1.0f, sensitivity);
                            velocityOut[sampleIdx] = velocity;
                            
                            // Confidence based on threshold (lower threshold = higher confidence for detected onsets)
                            float confidence = juce::jlimit(0.0f, 1.0f, 1.0f - threshold);
                            confidenceOut[sampleIdx] = confidence;
                            
                            lastOnsetTime = onsetSamplePos;
                            onsetsInBuffer++;
                            
                            if (shouldLog && onsetsInBuffer <= 3) // Log first 3 onsets
                                juce::Logger::writeToLog("[Essentia Onset] Onset written at sampleIdx=" + 
                                                        juce::String(sampleIdx) + 
                                                        ", velocity=" + juce::String(velocity, 3) + 
                                                        ", confidence=" + juce::String(confidence, 3));
                        }
                        else
                        {
                            onsetsIgnored++;
                        }
                    }
                }
                else if (relativeSamplePos >= buffer.getNumSamples())
                {
                    // Onset is in the future, store for next buffer
                    pendingOnsets.push_back(onsetSamplePos);
                    onsetsPending++;
                }
                else
                {
                    // If relativeSamplePos < 0, onset is too old, ignore it
                    onsetsIgnored++;
                }
                onsetsProcessed++;
            }
            
            if (shouldLog)
                juce::Logger::writeToLog("[Essentia Onset] Onsets processed: total=" + juce::String(onsetsProcessed) + 
                                        ", inBuffer=" + juce::String(onsetsInBuffer) + 
                                        ", pending=" + juce::String(onsetsPending) + 
                                        ", ignored=" + juce::String(onsetsIgnored));
            
            samplesSinceAnalysis = 0;
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[Essentia Onset] ERROR in compute(): " + juce::String(e.what()));
            // Fall through to fallback detection
        }
    }
    
    // Process pending onsets from previous analysis
    int pendingProcessed = 0;
    while (!pendingOnsets.empty())
    {
        float pendingOnset = pendingOnsets.front();
        float relativePos = pendingOnset - absoluteSamplePos;
        
        if (relativePos >= 0.0f && relativePos < buffer.getNumSamples())
        {
            int sampleIdx = (int)relativePos;
            if (sampleIdx >= 0 && sampleIdx < buffer.getNumSamples())
            {
                float timeSinceLast = (pendingOnset - lastOnsetTime) / currentSampleRate;
                if (lastOnsetTime < 0.0f || timeSinceLast * 1000.0f > minIntervalMs)
                {
                    onsetOut[sampleIdx] = 1.0f;
                    float sensitivity = sensitivityParam != nullptr ? sensitivityParam->load() : 0.5f;
                    velocityOut[sampleIdx] = juce::jlimit(0.0f, 1.0f, sensitivity);
                    confidenceOut[sampleIdx] = juce::jlimit(0.0f, 1.0f, 1.0f - threshold);
                    lastOnsetTime = pendingOnset;
                    pendingProcessed++;
                }
            }
            pendingOnsets.pop_front();
        }
        else if (relativePos < 0.0f)
        {
            // Too old, discard
            pendingOnsets.pop_front();
        }
        else
        {
            // Still in future
            break;
        }
    }
    
    if (shouldLog && pendingProcessed > 0)
        juce::Logger::writeToLog("[Essentia Onset] Processed " + juce::String(pendingProcessed) + " pending onsets");
#endif
    
    // Fallback detection (used when Essentia not available or sample rate mismatch)
    // MUST run BEFORE updating absoluteSamplePos and output logging
    bool useEssentia = false;
#ifdef ESSENTIA_FOUND
    useEssentia = (shouldRunAnalysis && onsetDetector && EssentiaWrapper::isInitialized() && currentSampleRate == 44100.0);
#endif
    
    if (!useEssentia)
    {
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Onset] Using FALLBACK energy-based detection");
        
        // Fallback energy-based detection (instance variables, not static)
        static thread_local float lastEnergy = 0.0f;
        static thread_local float energyHistory[8] = {0.0f};
        static thread_local int historyPos = 0;
        
        int fallbackOnsets = 0;
        float maxEnergyDiff = 0.0f;
        float maxAvgEnergy = 0.0f;
        float maxSample = 0.0f;
        float minSample = 0.0f;
        
        // Debug: Check first few samples
        if (shouldLog && buffer.getNumSamples() > 0)
        {
            juce::Logger::writeToLog("[Essentia Onset] Fallback: inputSamples[0]=" + juce::String(inputSamples[0], 6) + 
                                    ", inputSamples[10]=" + juce::String(inputSamples[10], 6) + 
                                    ", inputSamples[100]=" + juce::String(inputSamples[100], 6));
        }
        
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sample = inputSamples[i];
            maxSample = std::max(maxSample, std::abs(sample));
            minSample = std::min(minSample, std::abs(sample));
            const float energy = sample * sample;
            
            // Update energy history
            energyHistory[historyPos] = energy;
            historyPos = (historyPos + 1) % 8;
            
            // Calculate average energy
            float avgEnergy = 0.0f;
            for (int j = 0; j < 8; ++j)
                avgEnergy += energyHistory[j];
            avgEnergy /= 8.0f;
            maxAvgEnergy = std::max(maxAvgEnergy, avgEnergy);
            
            // Detect onset: energy increase above threshold
            // Use a scaled threshold for energy differences (energy is squared, so differences can be large)
            const float energyDiff = avgEnergy - lastEnergy;
            maxEnergyDiff = std::max(maxEnergyDiff, energyDiff);
            const float timeSinceLastOnset = (absoluteSamplePos + i - lastOnsetTime) / currentSampleRate;
            
            // Scale threshold appropriately for energy differences
            // Since we're looking at energy differences, use a much smaller threshold
            // Energy differences for onsets are typically 0.01-0.1 range
            const float energyThreshold = threshold * 0.01f; // Much smaller threshold
            
            if (energyDiff > energyThreshold && (lastOnsetTime < 0.0f || timeSinceLastOnset * 1000.0f > minIntervalMs))
            {
                // Onset detected!
                onsetOut[i] = 1.0f;
                velocityOut[i] = juce::jlimit(0.0f, 1.0f, avgEnergy * 10.0f);
                confidenceOut[i] = juce::jlimit(0.0f, 1.0f, energyDiff * 5.0f);
                
                lastOnsetTime = absoluteSamplePos + i;
                fallbackOnsets++;
                
                if (shouldLog && fallbackOnsets <= 3) // Log first 3
                    juce::Logger::writeToLog("[Essentia Onset] Fallback onset at sample " + juce::String(i) + 
                                            ", energyDiff=" + juce::String(energyDiff, 6) + 
                                            ", threshold=" + juce::String(energyThreshold, 6) +
                                            ", avgEnergy=" + juce::String(avgEnergy, 6));
            }
            
            lastEnergy = avgEnergy;
        }
        
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Onset] Fallback stats: maxSample=" + juce::String(maxSample, 6) + 
                                    ", minSample=" + juce::String(minSample, 6) +
                                    ", maxEnergyDiff=" + juce::String(maxEnergyDiff, 6) + 
                                    ", maxAvgEnergy=" + juce::String(maxAvgEnergy, 6) + 
                                    ", threshold=" + juce::String(threshold * 0.01f, 6) +
                                    ", lastEnergy=" + juce::String(lastEnergy, 6));
        
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Onset] Fallback detected " + juce::String(fallbackOnsets) + " onsets");
    }
    
    // Update absolute sample position
    absoluteSamplePos += buffer.getNumSamples();
    
    // Check output levels
    if (shouldLog)
    {
        float maxOnset = 0.0f, maxVelocity = 0.0f, maxConfidence = 0.0f;
        int onsetCount = 0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (onsetOut[i] > 0.0f) onsetCount++;
            maxOnset = std::max(maxOnset, onsetOut[i]);
            maxVelocity = std::max(maxVelocity, velocityOut[i]);
            maxConfidence = std::max(maxConfidence, confidenceOut[i]);
        }
        juce::Logger::writeToLog("[Essentia Onset] Outputs: onsetMax=" + juce::String(maxOnset, 3) + 
                                ", velocityMax=" + juce::String(maxVelocity, 3) + 
                                ", confidenceMax=" + juce::String(maxConfidence, 3) + 
                                ", onsetCount=" + juce::String(onsetCount));
        juce::Logger::writeToLog("[Essentia Onset] === processBlock END ===");
    }
    
    // Update telemetry
    if ((buffer.getNumSamples() & 0x3F) == 0)
    {
        setLiveParamValue(paramIdThreshold, threshold);
    }
    
#if defined(PRESET_CREATOR_UI)
    // Capture input audio for visualization
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int writeIdx = (vizWritePos + i) % vizBufferSize;
        vizInputBuffer.setSample(0, writeIdx, input[i]);
    }
    vizWritePos = (vizWritePos + buffer.getNumSamples()) % vizBufferSize;
    
    // Update visualization data (thread-safe)
    const int stride = vizBufferSize / VizData::waveformPoints;
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        const int readIdx = (vizWritePos - VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
        vizData.inputWaveform[i].store(vizInputBuffer.getSample(0, readIdx));
    }
    
    // Update live values
    float maxOnset = 0.0f;
    float maxVelocity = 0.0f;
    float maxConfidence = 0.0f;
    int onsetCount = 0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        if (onsetOut[i] > 0.0f) onsetCount++;
        maxOnset = std::max(maxOnset, onsetOut[i]);
        maxVelocity = std::max(maxVelocity, velocityOut[i]);
        maxConfidence = std::max(maxConfidence, confidenceOut[i]);
    }
    vizData.onsetGateLevel.store(maxOnset);
    vizData.velocityLevel.store(maxVelocity);
    vizData.confidenceLevel.store(maxConfidence);
    vizData.detectedOnsets.store((float)onsetCount);
#endif
    
    updateOutputTelemetry(buffer);
}

void EssentiaOnsetDetectorModuleProcessor::setTimingInfo(const TransportState& state)
{
    m_currentTransport = state;
}

void EssentiaOnsetDetectorModuleProcessor::forceStop()
{
    pendingOnsets.clear();
    lastOnsetTime = -1.0f;
    absoluteSamplePos = 0.0f;
    samplesSinceAnalysis = 0;
    bufferWritePos = 0;
}

bool EssentiaOnsetDetectorModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdThresholdMod) { outChannelIndexInBus = 0; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void EssentiaOnsetDetectorModuleProcessor::drawParametersInNode(float itemWidth,
                                                                  const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                                  const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    
    auto HelpMarker = [](const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(desc);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    ImGui::PushItemWidth(itemWidth);
    ImGui::PushID(this);
    
    // Read visualization data (thread-safe) - BEFORE BeginChild
    float inputWaveform[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
        inputWaveform[i] = vizData.inputWaveform[i].load();
    const float onsetLevel = vizData.onsetGateLevel.load();
    const float velocityLevel = vizData.velocityLevel.load();
    const float confidenceLevel = vizData.confidenceLevel.load();
    const float detectedOnsets = vizData.detectedOnsets.load();
    
    // Get threshold for visualization (use different name to avoid conflict)
    const float thresholdForViz = thresholdParam != nullptr ? thresholdParam->load() : 0.3f;
    
    // Waveform visualization in child window
    const auto& freqColors = theme.modules.frequency_graph;
    const auto resolveColor = [](ImU32 value, ImU32 fallback) { return value != 0 ? value : fallback; };
    const float waveHeight = 100.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    
    if (ImGui::BeginChild("EssentiaOnsetWaveform", graphSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = resolveColor(freqColors.background, IM_COL32(18, 20, 24, 255));
        drawList->AddRectFilled(p0, p1, bgColor);
        
        // Grid lines
        const ImU32 gridColor = resolveColor(freqColors.grid, IM_COL32(50, 55, 65, 255));
        const float midY = p0.y + graphSize.y * 0.5f;
        drawList->AddLine(ImVec2(p0.x, midY), ImVec2(p1.x, midY), gridColor, 1.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        // Draw input waveform
        const float scaleY = graphSize.y * 0.45f;
        const float stepX = graphSize.x / (float)(VizData::waveformPoints - 1);
        
        const ImU32 waveformColor = ImGui::ColorConvertFloat4ToU32(theme.accent);
        float prevX = p0.x;
        float prevY = midY;
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float sample = juce::jlimit(-1.0f, 1.0f, inputWaveform[i]);
            const float x = p0.x + i * stepX;
            const float y = midY - sample * scaleY;
            const float clampedY = juce::jlimit(p0.y, p1.y, y);
            
            if (i > 0)
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, clampedY), waveformColor, 1.5f);
            
            prevX = x;
            prevY = clampedY;
        }
        
        // Draw onset markers (red vertical lines) - use energy-based detection for visualization
        if (onsetLevel > 0.0f)
        {
            const ImU32 onsetColor = IM_COL32(255, 100, 100, 255);
            float lastEnergy = 0.0f;
            for (int i = 1; i < VizData::waveformPoints; ++i)
            {
                const float energy = inputWaveform[i] * inputWaveform[i];
                const float energyDiff = energy - lastEnergy;
                if (energyDiff > thresholdForViz * 0.1f) // Scale threshold for visualization
                {
                    const float x = p0.x + i * stepX;
                    drawList->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), onsetColor, 1.0f);
                }
                lastEnergy = energy;
            }
        }
        
        drawList->PopClipRect();
    }
    ImGui::EndChild();
    
    ImGui::Spacing();

    // === METHOD ===
    ThemeText("Detection Method", theme.text.section_header);
    ImGui::Spacing();
    
    int method = 0;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdMethod)))
        method = p->getIndex();
    
    const char* methodNames[] = { "Energy", "Spectral", "Complex", "HFC", "Phase" };
    if (ImGui::Combo("##method", &method, methodNames, 5))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdMethod)))
            *p = method;
        onModificationEnded();
        // Reinitialize algorithm if method changed
        shutdownEssentiaAlgorithms();
        initializeEssentiaAlgorithms();
    }
    // Scroll-edit support for method combo
    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int maxIndex = 4; // 0-4 (5 methods)
            const int newIndex = juce::jlimit(0, maxIndex, method + (wheel > 0.0f ? -1 : 1));
            if (newIndex != method)
            {
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdMethod)))
                {
                    *p = newIndex;
                    onModificationEnded();
                    // Reinitialize algorithm if method changed
                    shutdownEssentiaAlgorithms();
                    initializeEssentiaAlgorithms();
                }
            }
        }
    }
    ImGui::SameLine();
    ImGui::Text("Method");
    HelpMarker("Onset detection algorithm\nEnergy: Energy-based detection\nSpectral: Spectral flux-based\nComplex: Complex domain\nHFC: High Frequency Content\nPhase: Phase-based");

    ImGui::Spacing();
    ImGui::Spacing();

    // === THRESHOLD ===
    ThemeText("Threshold", theme.text.section_header);
    ImGui::Spacing();
    
    const bool thresholdMod = isParamModulated(paramIdThresholdMod);
    if (thresholdMod)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
    }
    
    if (thresholdMod) ImGui::BeginDisabled();
    float threshold = thresholdParam != nullptr ? getLiveParamValueFor(paramIdThresholdMod, paramIdThreshold, thresholdParam->load()) : 0.3f;
    if (ImGui::SliderFloat("##threshold", &threshold, 0.0f, 1.0f, "%.2f"))
    {
        if (!thresholdMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdThreshold)))
                *p = threshold;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (!thresholdMod) adjustParamOnWheel(ap.getParameter(paramIdThreshold), "threshold", threshold);
    if (thresholdMod) ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (thresholdMod)
    {
        ThemeText("Threshold (CV)", theme.text.active);
        ImGui::PopStyleColor(3);
    }
    else
    {
        ImGui::Text("Threshold");
    }
    HelpMarker("Onset detection threshold\n0.0 = Very sensitive (many onsets)\n1.0 = Less sensitive (fewer onsets)\nCV modulation: 0-1V maps to 0-1 threshold");

    ImGui::Spacing();
    ImGui::Spacing();

    // === MIN INTERVAL ===
    ThemeText("Min Interval", theme.text.section_header);
    ImGui::Spacing();
    
    float minInterval = minIntervalParam != nullptr ? minIntervalParam->load() : 50.0f;
    if (ImGui::SliderFloat("##mininterval", &minInterval, 0.0f, 1000.0f, "%.1f ms"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMinInterval)))
            *p = minInterval;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    adjustParamOnWheel(ap.getParameter(paramIdMinInterval), "minInterval", minInterval);
    
    ImGui::SameLine();
    ImGui::Text("Min Interval");
    HelpMarker("Minimum time between detected onsets\nPrevents multiple triggers on the same note\n0 ms = No limit\n1000 ms = Maximum spacing");

    ImGui::Spacing();
    ImGui::Spacing();

    // === SENSITIVITY ===
    ThemeText("Sensitivity", theme.text.section_header);
    ImGui::Spacing();
    
    float sensitivity = sensitivityParam != nullptr ? sensitivityParam->load() : 0.5f;
    if (ImGui::SliderFloat("##sensitivity", &sensitivity, 0.0f, 1.0f, "%.2f"))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSensitivity)))
            *p = sensitivity;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    adjustParamOnWheel(ap.getParameter(paramIdSensitivity), "sensitivity", sensitivity);
    
    ImGui::SameLine();
    ImGui::Text("Sensitivity");
    HelpMarker("Overall detection sensitivity\n0.0 = Low sensitivity\n1.0 = High sensitivity");

    ImGui::Spacing();
    ImGui::Spacing();

    // === OUTPUTS ===
    ThemeText("Outputs", theme.text.section_header);
    ImGui::Spacing();
    
    ImGui::Text("Onset Gate: %.3f", onsetLevel);
    ImGui::Text("Velocity: %.3f", velocityLevel);
    ImGui::Text("Confidence: %.3f", confidenceLevel);
    ImGui::Text("Detected: %.0f", detectedOnsets);
    HelpMarker("Live output values\nOnset: Gate trigger (0 or 1)\nVelocity: Onset intensity (0-1)\nConfidence: Detection confidence (0-1)\nDetected: Number of onsets detected in current buffer");

    ImGui::PopID();
    ImGui::PopItemWidth();
}

void EssentiaOnsetDetectorModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("Audio In", 0, "Onset", 0);
    helpers.drawParallelPins(nullptr, -1, "Velocity", 1);
    helpers.drawParallelPins(nullptr, -1, "Confidence", 2);
}

juce::String EssentiaOnsetDetectorModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Audio In";
        default: return juce::String("In ") + juce::String(channel + 1);
    }
}

juce::String EssentiaOnsetDetectorModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Onset";
        case 1: return "Velocity";
        case 2: return "Confidence";
        default: return juce::String("Out ") + juce::String(channel + 1);
    }
}
#endif

