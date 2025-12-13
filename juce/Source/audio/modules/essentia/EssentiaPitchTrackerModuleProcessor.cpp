#include "EssentiaPitchTrackerModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

EssentiaPitchTrackerModuleProcessor::EssentiaPitchTrackerModuleProcessor()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::discreteChannels(3), true)
                      .withOutput("Pitch CV", juce::AudioChannelSet::mono(), true)
                      .withOutput("Confidence", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "EssentiaPitchTrackerParams", createParameterLayout())
{
    minFrequencyParam = apvts.getRawParameterValue(paramIdMinFrequency);
    maxFrequencyParam = apvts.getRawParameterValue(paramIdMaxFrequency);
    methodParam = apvts.getRawParameterValue(paramIdMethod);

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    
    analysisBuffer.resize(FRAME_SIZE, 0.0f);
    
#if defined(PRESET_CREATOR_UI)
    vizInputBuffer.setSize(1, vizBufferSize);
    vizInputBuffer.clear();
#endif
}

EssentiaPitchTrackerModuleProcessor::~EssentiaPitchTrackerModuleProcessor()
{
    shutdownEssentiaAlgorithms();
}

juce::AudioProcessorValueTreeState::ParameterLayout EssentiaPitchTrackerModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdMinFrequency, "Min Frequency",
        juce::NormalisableRange<float>(20.0f, 2000.0f, 1.0f, 0.25f), 80.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdMaxFrequency, "Max Frequency",
        juce::NormalisableRange<float>(100.0f, 8000.0f, 1.0f, 0.25f), 2000.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdMethod, "Method",
        juce::StringArray { "YinFFT", "Yin", "Melodia" }, 0));
    
    return { params.begin(), params.end() };
}

void EssentiaPitchTrackerModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    
    // Initialize Essentia wrapper
    EssentiaWrapper::initializeEssentia();
    
    // Initialize Essentia algorithms
    initializeEssentiaAlgorithms();
    
    bufferWritePos = 0;
    samplesSinceAnalysis = 0;
    currentPitchHz = 0.0f;
    currentConfidence = 0.0f;
    smoothedPitchCV = 0.0f;
    smoothedConfidence = 0.0f;
    analysisBuffer.assign(FRAME_SIZE, 0.0f);
    
#if defined(PRESET_CREATOR_UI)
    vizInputBuffer.clear();
    vizWritePos = 0;
    for (int i = 0; i < VizData::waveformPoints; ++i)
        vizData.inputWaveform[i].store(0.0f);
    vizData.pitchCV.store(0.0f);
    vizData.confidence.store(0.0f);
    vizData.detectedPitchHz.store(0.0f);
#endif
}

void EssentiaPitchTrackerModuleProcessor::releaseResources()
{
    shutdownEssentiaAlgorithms();
}

void EssentiaPitchTrackerModuleProcessor::initializeEssentiaAlgorithms()
{
#ifdef ESSENTIA_FOUND
    if (!EssentiaWrapper::isInitialized())
    {
        juce::Logger::writeToLog("[Essentia Pitch] Essentia not initialized, using fallback detection");
        return;
    }
    
    try
    {
        essentia::standard::AlgorithmFactory& factory = essentia::standard::AlgorithmFactory::instance();
        
        // Create frame cutter
        frameCutter = factory.create("FrameCutter",
                                     "frameSize", FRAME_SIZE,
                                     "hopSize", HOP_SIZE,
                                     "startFromZero", false);
        
        // Create windowing
        windowing = factory.create("Windowing",
                                  "type", "hann",
                                  "zeroPadding", 0);
        
        // Create spectrum
        spectrum = factory.create("Spectrum",
                                 "size", FRAME_SIZE);
        
        // Create pitch detector based on method
        const int method = (int)(methodParam != nullptr ? methodParam->load() : 0.0f);
        juce::String algorithmName;
        
        switch (method)
        {
            case 0: algorithmName = "PitchYinFFT"; break;
            case 1: algorithmName = "PitchYin"; break;
            case 2: algorithmName = "PitchMelodia"; break;
            default: algorithmName = "PitchYinFFT"; break;
        }
        
        if (algorithmName == "PitchYinFFT")
        {
            pitchDetector = factory.create("PitchYinFFT",
                                          "frameSize", FRAME_SIZE,
                                          "sampleRate", currentSampleRate);
        }
        else if (algorithmName == "PitchYin")
        {
            pitchDetector = factory.create("PitchYin",
                                          "sampleRate", currentSampleRate);
        }
        else if (algorithmName == "PitchMelodia")
        {
            pitchDetector = factory.create("PitchMelodia",
                                          "sampleRate", currentSampleRate,
                                          "hopSize", HOP_SIZE,
                                          "frameSize", FRAME_SIZE);
        }
        
        juce::Logger::writeToLog("[Essentia Pitch] Algorithm created: " + algorithmName);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[Essentia Pitch] ERROR creating algorithm: " + juce::String(e.what()));
        pitchDetector = nullptr;
    }
#else
    juce::Logger::writeToLog("[Essentia Pitch] WARNING: ESSENTIA_FOUND not defined - using fallback detection");
#endif
}

void EssentiaPitchTrackerModuleProcessor::shutdownEssentiaAlgorithms()
{
#ifdef ESSENTIA_FOUND
    if (pitchDetector) { delete pitchDetector; pitchDetector = nullptr; }
    if (spectrum) { delete spectrum; spectrum = nullptr; }
    if (windowing) { delete windowing; windowing = nullptr; }
    if (frameCutter) { delete frameCutter; frameCutter = nullptr; }
    pool.clear();
#endif
}

void EssentiaPitchTrackerModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    
    static int logCounter = 0;
    const bool shouldLog = (logCounter++ % 100) == 0; // Log every 100 blocks
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Pitch] === processBlock START ===");

    auto inBus = getBusBuffer(buffer, true, 0);
    auto pitchBus = getBusBuffer(buffer, false, 0);
    auto confidenceBus = getBusBuffer(buffer, false, 1);
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Pitch] Input: channels=" + juce::String(inBus.getNumChannels()) + 
                                ", samples=" + juce::String(buffer.getNumSamples()) +
                                ", sampleRate=" + juce::String(currentSampleRate, 2));
    
    if (inBus.getNumChannels() == 0)
    {
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Pitch] ERROR: No input channels, clearing and returning");
        buffer.clear();
        return;
    }
    
    // CRITICAL: Read input BEFORE clearing outputs (Modulation Trinity: Buffer Aliasing)
    const float* input = inBus.getReadPointer(0);
    float* pitchOut = pitchBus.getWritePointer(0);
    float* confidenceOut = confidenceBus.getWritePointer(0);
    
    // CRITICAL: Copy input samples to local buffer BEFORE any processing
    // The input pointer may become invalid after other modules process the buffer
    juce::AudioBuffer<float> inputCopy(1, buffer.getNumSamples());
    inputCopy.copyFrom(0, 0, inBus, 0, 0, buffer.getNumSamples());
    const float* inputSamples = inputCopy.getReadPointer(0);
    
    // Check input signal level
    float inputRms = inBus.getRMSLevel(0, 0, buffer.getNumSamples());
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Pitch] Input RMS: " + juce::String(inputRms, 6));
    
    const float minFreq = minFrequencyParam != nullptr ? getLiveParamValueFor(paramIdMinFrequencyMod, paramIdMinFrequency, minFrequencyParam->load()) : 80.0f;
    const float maxFreq = maxFrequencyParam != nullptr ? getLiveParamValueFor(paramIdMaxFrequencyMod, paramIdMaxFrequency, maxFrequencyParam->load()) : 2000.0f;
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Pitch] Parameters: minFreq=" + juce::String(minFreq, 1) + 
                                "Hz, maxFreq=" + juce::String(maxFreq, 1) + "Hz");
    
    // Clear outputs (will be filled with smoothed values below)
    pitchBus.clear();
    confidenceBus.clear();
    
    if (shouldLog)
        juce::Logger::writeToLog("[Essentia Pitch] Outputs cleared, starting processing...");
    
    // Ensure we always have valid output pointers
    if (!pitchOut || !confidenceOut)
    {
        return; // Safety check
    }
    
    // Accumulate audio into analysis buffer
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        analysisBuffer[bufferWritePos] = inputSamples[i];
        bufferWritePos = (bufferWritePos + 1) % FRAME_SIZE;
    }
    
    // Run Essentia analysis when we have enough samples
    samplesSinceAnalysis += buffer.getNumSamples();
    bool shouldRunAnalysis = (samplesSinceAnalysis >= HOP_SIZE);
    
    if (shouldLog)
    {
        juce::Logger::writeToLog("[Essentia Pitch] Buffer filled: writePos=" + juce::String(bufferWritePos) + 
                                ", samplesSinceAnalysis=" + juce::String(samplesSinceAnalysis));
        juce::Logger::writeToLog("[Essentia Pitch] Analysis check: shouldRun=" + juce::String(shouldRunAnalysis ? "YES" : "NO"));
#ifdef ESSENTIA_FOUND
        juce::Logger::writeToLog("[Essentia Pitch] Essentia status: detector=" + juce::String(pitchDetector != nullptr ? "OK" : "NULL") + 
                                ", initialized=" + juce::String(EssentiaWrapper::isInitialized() ? "YES" : "NO"));
#else
        juce::Logger::writeToLog("[Essentia Pitch] Essentia: NOT COMPILED");
#endif
    }
    
#ifdef ESSENTIA_FOUND
    if (shouldRunAnalysis && pitchDetector && EssentiaWrapper::isInitialized())
    {
        try
        {
            if (shouldLog)
                juce::Logger::writeToLog("[Essentia Pitch] Running Essentia analysis...");
            
            // Get a frame from the analysis buffer
            std::vector<essentia::Real> frame(FRAME_SIZE);
            for (int i = 0; i < FRAME_SIZE; ++i)
            {
                int idx = (bufferWritePos - FRAME_SIZE + i + FRAME_SIZE) % FRAME_SIZE;
                frame[i] = analysisBuffer[idx];
            }
            
            // Check if frame is silent (skip analysis if so)
            bool isSilent = true;
            float frameRms = 0.0f;
            for (const auto& sample : frame)
            {
                frameRms += sample * sample;
                if (std::abs(sample) > 1e-6f)
                {
                    isSilent = false;
                }
            }
            frameRms = std::sqrt(frameRms / frame.size());
            
            if (shouldLog)
                juce::Logger::writeToLog("[Essentia Pitch] Frame: size=" + juce::String(frame.size()) + 
                                        ", RMS=" + juce::String(frameRms, 6) + 
                                        ", silent=" + juce::String(isSilent ? "YES" : "NO"));
            
            if (!isSilent)
            {
                // Apply windowing
                std::vector<essentia::Real> windowedFrame;
                windowing->input("frame").set(frame);
                windowing->output("frame").set(windowedFrame);
                windowing->compute();
                
                // Compute spectrum
                std::vector<essentia::Real> spec;
                spectrum->input("frame").set(windowedFrame);
                spectrum->output("spectrum").set(spec);
                spectrum->compute();
                
                // Detect pitch
                essentia::Real pitchHz = 0.0;
                essentia::Real confidence = 0.0;
                
                if (currentMethod == 0) // PitchYinFFT
                {
                    pitchDetector->input("spectrum").set(spec);
                    pitchDetector->output("pitch").set(pitchHz);
                    pitchDetector->output("pitchConfidence").set(confidence);
                    pitchDetector->compute();
                }
                else if (currentMethod == 1) // PitchYin
                {
                    pitchDetector->input("signal").set(frame);
                    pitchDetector->output("pitch").set(pitchHz);
                    pitchDetector->output("pitchConfidence").set(confidence);
                    pitchDetector->compute();
                }
                else if (currentMethod == 2) // PitchMelodia (requires full signal, not frame)
                {
                    // For Melodia, we'd need to accumulate more samples
                    // For now, use fallback
                    pitchHz = 0.0;
                    confidence = 0.0;
                }
                
                // Clamp pitch to valid range
                if (pitchHz >= minFreq && pitchHz <= maxFreq && confidence > 0.1f)
                {
                    currentPitchHz = (float)pitchHz;
                    currentConfidence = (float)confidence;
                    
                    if (shouldLog)
                        juce::Logger::writeToLog("[Essentia Pitch] Pitch detected: " + juce::String(pitchHz, 2) + 
                                                "Hz, confidence=" + juce::String(confidence, 3));
                }
                else
                {
                    // Invalid pitch, fade out
                    currentPitchHz *= 0.95f;
                    currentConfidence *= 0.9f;
                    
                    if (shouldLog)
                        juce::Logger::writeToLog("[Essentia Pitch] Invalid pitch (out of range or low confidence), fading out");
                }
            }
            else
            {
                if (shouldLog)
                    juce::Logger::writeToLog("[Essentia Pitch] Frame is silent, skipping analysis");
                // Silent frame, fade out
                currentPitchHz *= 0.95f;
                currentConfidence *= 0.9f;
            }
            
            samplesSinceAnalysis = 0;
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("[Essentia Pitch] ERROR in compute(): " + juce::String(e.what()));
            // Fall through to fallback
        }
    }
#endif
    
    // Fallback pitch detection if Essentia not available
    bool useEssentiaPitch = false;
#ifdef ESSENTIA_FOUND
    useEssentiaPitch = (shouldRunAnalysis && pitchDetector && EssentiaWrapper::isInitialized());
#endif
    
    if (!useEssentiaPitch)
    {
        if (shouldLog)
            juce::Logger::writeToLog("[Essentia Pitch] Using FALLBACK zero-crossing pitch detection");
        
        // Simple zero-crossing based pitch estimation fallback
        static thread_local int zeroCrossings = 0;
        static thread_local float lastSample = 0.0f;
        static thread_local int sampleCount = 0;
        
        // Debug: Check input samples
        if (shouldLog && buffer.getNumSamples() > 0)
        {
            juce::Logger::writeToLog("[Essentia Pitch] Fallback: inputSamples[0]=" + juce::String(inputSamples[0], 6) + 
                                    ", inputSamples[10]=" + juce::String(inputSamples[10], 6) + 
                                    ", inputSamples[100]=" + juce::String(inputSamples[100], 6) +
                                    ", lastSample=" + juce::String(lastSample, 6));
        }
        
        int totalZeroCrossings = 0;
        float maxSample = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float sample = inputSamples[i];
            maxSample = std::max(maxSample, std::abs(sample));
            if ((lastSample < 0.0f && sample >= 0.0f) || (lastSample > 0.0f && sample <= 0.0f))
            {
                zeroCrossings++;
                totalZeroCrossings++;
            }
            lastSample = sample;
            sampleCount++;
            
            // Estimate pitch every 1024 samples (or at end of buffer if we have enough)
            if (sampleCount >= 1024 || (i == buffer.getNumSamples() - 1 && sampleCount >= 256))
            {
                if (zeroCrossings > 0)
                {
                    const float period = (float)sampleCount / (float)zeroCrossings;
                    const float estimatedFreq = currentSampleRate / period;
                    
                    if (shouldLog)
                        juce::Logger::writeToLog("[Essentia Pitch] Fallback calculation: period=" + juce::String(period, 2) + 
                                                " samples, freq=" + juce::String(estimatedFreq, 2) + 
                                                "Hz (zeroCrossings=" + juce::String(zeroCrossings) + 
                                                ", sampleCount=" + juce::String(sampleCount) + ")");
                    
                    if (estimatedFreq >= minFreq && estimatedFreq <= maxFreq)
                    {
                        currentPitchHz = estimatedFreq;
                        currentConfidence = 0.5f; // Medium confidence for fallback
                        
                        if (shouldLog)
                            juce::Logger::writeToLog("[Essentia Pitch] Fallback pitch ACCEPTED: " + juce::String(estimatedFreq, 2) + "Hz");
                    }
                    else if (shouldLog && estimatedFreq > 0.0f)
                    {
                        juce::Logger::writeToLog("[Essentia Pitch] Fallback pitch out of range: " + 
                                                juce::String(estimatedFreq, 2) + "Hz (need " + 
                                                juce::String(minFreq, 1) + "-" + juce::String(maxFreq, 1) + "Hz)");
                    }
                }
                else if (shouldLog && sampleCount >= 1024)
                {
                    juce::Logger::writeToLog("[Essentia Pitch] Fallback: No zero crossings detected in " + 
                                            juce::String(sampleCount) + " samples");
                }
                
                zeroCrossings = 0;
                sampleCount = 0;
            }
        }
        
        if (shouldLog && buffer.getNumSamples() > 0)
            juce::Logger::writeToLog("[Essentia Pitch] Fallback: maxSample=" + juce::String(maxSample, 6) + 
                                    ", totalZeroCrossings=" + juce::String(totalZeroCrossings) +
                                    ", lastSample=" + juce::String(lastSample, 6));
    }
    
    // Convert pitch to CV (normalized 0-1, mapping frequency range to CV)
    // Map frequency to CV: 0V = minFreq, 1V = maxFreq (logarithmic mapping)
    const float minFreqLog = std::log10(std::max(1.0f, minFreq));
    const float maxFreqLog = std::log10(std::max(1.0f, maxFreq));
    const float pitchLog = std::log10(std::max(1.0f, currentPitchHz));
    const float pitchCV = juce::jlimit(0.0f, 1.0f, (pitchLog - minFreqLog) / (maxFreqLog - minFreqLog + 0.001f));
    
    // Smooth the CV output to avoid jumps
    const float smoothingFactor = 0.95f;
    
    // Output smoothed pitch CV and confidence
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        smoothedPitchCV = smoothingFactor * smoothedPitchCV + (1.0f - smoothingFactor) * pitchCV;
        smoothedConfidence = smoothingFactor * smoothedConfidence + (1.0f - smoothingFactor) * currentConfidence;
        
        pitchOut[i] = smoothedPitchCV;
        confidenceOut[i] = smoothedConfidence;
    }
    
    // Check output levels
    if (shouldLog)
    {
        float maxPitchCV = 0.0f, maxConfidence = 0.0f;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            maxPitchCV = std::max(maxPitchCV, pitchOut[i]);
            maxConfidence = std::max(maxConfidence, confidenceOut[i]);
        }
        juce::Logger::writeToLog("[Essentia Pitch] Outputs: pitchCV=" + juce::String(maxPitchCV, 3) + 
                                " (" + juce::String(currentPitchHz, 1) + "Hz)" +
                                ", confidence=" + juce::String(maxConfidence, 3));
        juce::Logger::writeToLog("[Essentia Pitch] === processBlock END ===");
    }
    
    // Update telemetry
    if ((buffer.getNumSamples() & 0x3F) == 0)
    {
        setLiveParamValue(paramIdMinFrequency, minFreq);
        setLiveParamValue(paramIdMaxFrequency, maxFreq);
    }
    
    // Store current method for algorithm selection
    if (methodParam)
        currentMethod = (int)methodParam->load();
    
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
    vizData.pitchCV.store(smoothedPitchCV);
    vizData.confidence.store(smoothedConfidence);
    vizData.detectedPitchHz.store(currentPitchHz);
#endif
    
    updateOutputTelemetry(buffer);
}

void EssentiaPitchTrackerModuleProcessor::setTimingInfo(const TransportState& state)
{
    m_currentTransport = state;
}

void EssentiaPitchTrackerModuleProcessor::forceStop()
{
    currentPitchHz = 0.0f;
    currentConfidence = 0.0f;
    smoothedPitchCV = 0.0f;
    smoothedConfidence = 0.0f;
    bufferWritePos = 0;
    samplesSinceAnalysis = 0;
}

bool EssentiaPitchTrackerModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0;
    if (paramId == paramIdMinFrequencyMod) { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdMaxFrequencyMod) { outChannelIndexInBus = 1; return true; }
    return false;
}

#if defined(PRESET_CREATOR_UI)
void EssentiaPitchTrackerModuleProcessor::drawParametersInNode(float itemWidth,
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
    
    // Read visualization data (thread-safe)
    float inputWaveform[VizData::waveformPoints];
    for (int i = 0; i < VizData::waveformPoints; ++i)
        inputWaveform[i] = vizData.inputWaveform[i].load();
    const float pitchCV = vizData.pitchCV.load();
    const float confidence = vizData.confidence.load();
    const float detectedPitchHz = vizData.detectedPitchHz.load();
    
    // Waveform visualization in child window
    const auto& freqColors = theme.modules.frequency_graph;
    const auto resolveColor = [](ImU32 value, ImU32 fallback) { return value != 0 ? value : fallback; };
    const float waveHeight = 100.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    
    if (ImGui::BeginChild("EssentiaPitchWaveform", graphSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
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
        
        // Draw pitch indicator line (horizontal line at detected pitch)
        if (detectedPitchHz > 0.0f && confidence > 0.1f)
        {
            const ImU32 pitchColor = IM_COL32(100, 255, 100, 200);
            const float pitchY = midY - (pitchCV - 0.5f) * scaleY * 0.5f;
            const float clampedPitchY = juce::jlimit(p0.y, p1.y, pitchY);
            drawList->AddLine(ImVec2(p0.x, clampedPitchY), ImVec2(p1.x, clampedPitchY), pitchColor, 2.0f);
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
    
    const char* methodNames[] = { "YinFFT", "Yin", "Melodia" };
    if (ImGui::Combo("##method", &method, methodNames, 3))
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
            const int maxIndex = 2; // 0-2 (3 methods)
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
    HelpMarker("Pitch detection algorithm\nYinFFT: Fast FFT-based (recommended)\nYin: Time-domain based\nMelodia: More accurate but slower");

    ImGui::Spacing();
    ImGui::Spacing();

    // === MIN FREQUENCY ===
    ThemeText("Min Frequency", theme.text.section_header);
    ImGui::Spacing();
    
    const bool minFreqMod = isParamModulated(paramIdMinFrequencyMod);
    if (minFreqMod)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
    }
    
    if (minFreqMod) ImGui::BeginDisabled();
    float minFreq = minFrequencyParam != nullptr ? getLiveParamValueFor(paramIdMinFrequencyMod, paramIdMinFrequency, minFrequencyParam->load()) : 80.0f;
    if (ImGui::SliderFloat("##minfreq", &minFreq, 20.0f, 2000.0f, "%.1f Hz"))
    {
        if (!minFreqMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMinFrequency)))
                *p = minFreq;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (!minFreqMod) adjustParamOnWheel(ap.getParameter(paramIdMinFrequency), "minFreq", minFreq);
    if (minFreqMod) ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (minFreqMod)
    {
        ThemeText("Min Freq (CV)", theme.text.active);
        ImGui::PopStyleColor(3);
    }
    else
    {
        ImGui::Text("Min Frequency");
    }
    HelpMarker("Minimum frequency to detect\nLower values allow detection of bass notes\nCV modulation: 0-1V maps to 20-2000Hz");

    ImGui::Spacing();
    ImGui::Spacing();

    // === MAX FREQUENCY ===
    ThemeText("Max Frequency", theme.text.section_header);
    ImGui::Spacing();
    
    const bool maxFreqMod = isParamModulated(paramIdMaxFrequencyMod);
    if (maxFreqMod)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.4f, 0.8f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.2f, 0.4f, 0.5f, 0.5f));
    }
    
    if (maxFreqMod) ImGui::BeginDisabled();
    float maxFreq = maxFrequencyParam != nullptr ? getLiveParamValueFor(paramIdMaxFrequencyMod, paramIdMaxFrequency, maxFrequencyParam->load()) : 2000.0f;
    if (ImGui::SliderFloat("##maxfreq", &maxFreq, 100.0f, 8000.0f, "%.1f Hz"))
    {
        if (!maxFreqMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMaxFrequency)))
                *p = maxFreq;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (!maxFreqMod) adjustParamOnWheel(ap.getParameter(paramIdMaxFrequency), "maxFreq", maxFreq);
    if (maxFreqMod) ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (maxFreqMod)
    {
        ThemeText("Max Freq (CV)", theme.text.active);
        ImGui::PopStyleColor(3);
    }
    else
    {
        ImGui::Text("Max Frequency");
    }
    HelpMarker("Maximum frequency to detect\nHigher values allow detection of high notes\nCV modulation: 0-1V maps to 100-8000Hz");

    ImGui::Spacing();
    ImGui::Spacing();

    // === OUTPUTS ===
    ThemeText("Outputs", theme.text.section_header);
    ImGui::Spacing();
    
    float pitchHz = minFreq + (maxFreq - minFreq) * pitchCV;
    
    ImGui::Text("Pitch CV: %.3f (%.1f Hz)", pitchCV, pitchHz);
    ImGui::Text("Confidence: %.3f", confidence);
    if (detectedPitchHz > 0.0f)
        ImGui::Text("Detected: %.1f Hz", detectedPitchHz);
    HelpMarker("Live output values\nPitch CV: Normalized pitch (0-1V)\nConfidence: Detection confidence (0-1)");

    ImGui::PopID();
    ImGui::PopItemWidth();
}

void EssentiaPitchTrackerModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("Audio In", 0, "Pitch CV", 0);
    helpers.drawParallelPins(nullptr, -1, "Confidence", 1);
}

juce::String EssentiaPitchTrackerModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Audio In";
        case 1: return "Min Freq Mod";
        case 2: return "Max Freq Mod";
        default: return juce::String("In ") + juce::String(channel + 1);
    }
}

juce::String EssentiaPitchTrackerModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Pitch CV";
        case 1: return "Confidence";
        default: return juce::String("Out ") + juce::String(channel + 1);
    }
}
#endif

