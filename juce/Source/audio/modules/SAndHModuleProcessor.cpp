#include "SAndHModuleProcessor.h"

SAndHModuleProcessor::SAndHModuleProcessor()
    : ModuleProcessor (BusesProperties()
                        .withInput ("Inputs", juce::AudioChannelSet::discreteChannels(7), true) // 0-1=signal, 2-3=gate, 4=threshold mod, 5=edge mod, 6=slew mod
                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SAndHParams", createParameterLayout())
{
    thresholdParam = apvts.getRawParameterValue ("threshold");
    hysteresisParam = apvts.getRawParameterValue ("hysteresis");
    slewMsParam = apvts.getRawParameterValue ("slewMs");
    edgeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter ("edge"));
    thresholdModParam = apvts.getRawParameterValue ("threshold_mod");
    slewMsModParam = apvts.getRawParameterValue ("slewMs_mod");
    edgeModParam = apvts.getRawParameterValue ("edge_mod");
    
    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // For Out R
}

bool SAndHModuleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // We require exactly 1 input bus and 1 output bus
    if (layouts.inputBuses.size() != 1 || layouts.outputBuses.size() != 1)
        return false;

    // Input MUST be exactly 7 discrete channels (Signal L/R + Gate L/R + 3 CV mods)
    // This strict check prevents the host from aliasing channels, which would cause
    // all modulation inputs to read the same value (critical fix for CV routing)
    if (layouts.inputBuses[0] != juce::AudioChannelSet::discreteChannels(7))
        return false;

    // Output MUST be Stereo
    if (layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

juce::AudioProcessorValueTreeState::ParameterLayout SAndHModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("threshold", "Threshold", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("hysteresis", "Hysteresis", juce::NormalisableRange<float> (0.0f, 0.1f, 0.001f), 0.01f));
    params.push_back (std::make_unique<juce::AudioParameterChoice> ("edge", "Edge", juce::StringArray { "Rising", "Falling", "Both" }, 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("slewMs", "Slew (ms)", juce::NormalisableRange<float> (0.0f, 2000.0f, 0.01f, 0.35f), 0.0f));
    
    // Add modulation parameters
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("threshold_mod", "Threshold Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("slewMs_mod", "Slew Mod", 0.0f, 1.0f, 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("edge_mod", "Edge Mod", 0.0f, 1.0f, 0.0f));
    return { params.begin(), params.end() };
}

void SAndHModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    sr = sampleRate;
    lastGateL = lastGateR = 0.0f;
    heldL = heldR = 0.0f;
    outL = outR = 0.0f;
    lastGateLForNorm = lastGateRForNorm = 0.0f;

#if defined(PRESET_CREATOR_UI)
    captureBuffer.setSize(4, samplesPerBlock);
    captureBuffer.clear();
    for (auto* arr : { &vizData.signalL, &vizData.signalR, &vizData.gateL, &vizData.gateR, &vizData.heldWaveL, &vizData.heldWaveR })
        for (auto& v : *arr) v.store(0.0f);
    for (auto* arr : { &vizData.gateMarkersL, &vizData.gateMarkersR })
        for (auto& v : *arr) v.store(0);
    vizData.latchAgeMsL.store(0.0f);
    vizData.latchAgeMsR.store(0.0f);
    vizData.liveThreshold.store(0.5f);
#endif
}

void SAndHModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    // Single input bus with 4 channels: 0-1 signal, 2-3 gate
    auto in  = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);

    const int numSamples = buffer.getNumSamples();
    
    // --- CORRECTED POINTER LOGIC ---
    const float* sigL = in.getReadPointer(0);
    // If input is mono, sigR should be the same as sigL.
    const float* sigR = in.getNumChannels() > 1 ? in.getReadPointer(1) : sigL;
    
    // CRITICAL FIX: Gate inputs should be nullptr when not connected, not fallback to signal!
    // When gate is nullptr, we treat it as always low (no edges detected)
    const float* gateL = in.getNumChannels() > 2 ? in.getReadPointer(2) : nullptr;
    const float* gateR = in.getNumChannels() > 3 ? in.getReadPointer(3) : nullptr;
    // --- END OF CORRECTION ---
    float* outLw = out.getWritePointer (0);
    float* outRw = out.getNumChannels() > 1 ? out.getWritePointer (1) : out.getWritePointer (0);

    // Get modulation CV inputs from the single unified input bus
    const bool isThresholdMod = isParamInputConnected("threshold_mod");
    const bool isEdgeMod = isParamInputConnected("edge_mod");
    const bool isSlewMod = isParamInputConnected("slewMs_mod");

    const float* thresholdCV = isThresholdMod && in.getNumChannels() > 4 ? in.getReadPointer(4) : nullptr;
    const float* edgeCV = isEdgeMod && in.getNumChannels() > 5 ? in.getReadPointer(5) : nullptr;
    const float* slewCV = isSlewMod && in.getNumChannels() > 6 ? in.getReadPointer(6) : nullptr;

    // Get base parameter values ONCE
    const float baseThreshold = thresholdParam != nullptr ? thresholdParam->load() : 0.5f;
    const float baseHysteresis = hysteresisParam != nullptr ? hysteresisParam->load() : 0.01f;
    const float baseSlewMs = slewMsParam != nullptr ? slewMsParam->load() : 0.0f;
    const int baseEdge = edgeParam != nullptr ? edgeParam->getIndex() : 0;

#if defined(DEBUG_SANDH_VERBOSE)
    // DEBUG LOGGING: Log gate connection status and initial values (throttled)
    static int debugBlockCounter = 0;
    const bool shouldLogBlock = ((debugBlockCounter++ % 200) == 0); // Every 200 blocks (~4.5s at 44.1kHz)
    
    if (shouldLogBlock)
    {
        juce::String gateStatusL = gateL != nullptr ? "CONNECTED" : "NOT CONNECTED";
        juce::String gateStatusR = gateR != nullptr ? "CONNECTED" : "NOT CONNECTED";
        float gateRawL = gateL != nullptr ? gateL[0] : 0.0f;
        float gateRawR = gateR != nullptr ? gateR[0] : 0.0f;
        float sigRawL = sigL != nullptr ? sigL[0] : 0.0f;
        float sigRawR = sigR != nullptr ? sigR[0] : 0.0f;
        float outRawL = outLw != nullptr ? outLw[0] : 0.0f;
        float outRawR = outRw != nullptr ? outRw[0] : 0.0f;
        
        juce::Logger::writeToLog("[S&H] ===== BLOCK " + juce::String(debugBlockCounter) + " =====");
        juce::Logger::writeToLog("[S&H] Gate L: " + gateStatusL + " (raw=" + juce::String(gateRawL, 4) + ")" +
                                 " | Gate R: " + gateStatusR + " (raw=" + juce::String(gateRawR, 4) + ")");
        juce::Logger::writeToLog("[S&H] Sig L=" + juce::String(sigRawL, 4) + " R=" + juce::String(sigRawR, 4) +
                                 " | Out L=" + juce::String(outRawL, 4) + " R=" + juce::String(outRawR, 4) +
                                 " | Held L=" + juce::String(heldL, 4) + " R=" + juce::String(heldR, 4));
        juce::Logger::writeToLog("[S&H] Threshold=" + juce::String(baseThreshold, 3) +
                                 " | Edge=" + juce::String(baseEdge) + " (0=rise,1=fall,2=both)" +
                                 " | Slew=" + juce::String(baseSlewMs, 2) + "ms");
    }
#else
    const bool shouldLogBlock = false;
#endif

    // OPTIMIZATION: Cache parameter values when not modulated (calculate once per block)
    // Only recalculate per-sample if modulation is connected
    const float thr = baseThreshold;  // Use base value if not modulated
    const float slewMs = baseSlewMs;  // Use base value if not modulated
    const int edge = baseEdge;         // Use base value if not modulated
    
    // Variables to store final modulated values for UI telemetry
    float finalThreshold = baseThreshold;
    float finalSlewMs = baseSlewMs;
    int finalEdge = baseEdge;

    for (int i = 0; i < numSamples; ++i)
    {
        // PER-SAMPLE: Calculate effective parameters FOR THIS SAMPLE (only if modulated)
        float currentThr = thr;
        if (isThresholdMod && thresholdCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, thresholdCV[i]);
            const float thresholdRange = 0.6f;
            const float offset = (cv - 0.5f) * thresholdRange;
            currentThr = juce::jlimit(0.0f, 1.0f, baseThreshold + offset);
        }
        
        float currentSlewMs = slewMs;
        if (isSlewMod && slewCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, slewCV[i]);
            // ADDITIVE MODULATION FIX: Add CV offset to base slew time
            const float slewRange = 500.0f; // CV can modulate slew by +/- 500ms
            const float slewOffset = (cv - 0.5f) * slewRange; // Center around 0
            currentSlewMs = baseSlewMs + slewOffset;
            currentSlewMs = juce::jlimit(0.0f, 2000.0f, currentSlewMs);
        }
        
        int currentEdge = edge;
        if (isEdgeMod && edgeCV != nullptr) {
            const float cv = juce::jlimit(0.0f, 1.0f, edgeCV[i]);
            // ADDITIVE MODULATION FIX: Add CV offset to base edge setting
            const int edgeOffset = static_cast<int>((cv - 0.5f) * 3.0f); // Range [-1, +1]
            currentEdge = (baseEdge + edgeOffset + 3) % 3; // Wrap around (0,1,2)
        }

        // Store final values for telemetry (use last sample's values)
        finalThreshold = currentThr;
        finalSlewMs = currentSlewMs;
        finalEdge = currentEdge;
        
        // Apply slew limiting: allow instant transitions when slew = 0, otherwise use minimum to prevent clicks
        float slewCoeff = 1.0f;  // Default to instant (no slew)
        if (currentSlewMs > 0.001f) {  // Only apply slew if > 0.001ms
            const float effectiveSlewMs = std::max(currentSlewMs, 0.1f);  // Minimum 0.1ms for non-zero slew
            slewCoeff = (float) (1.0 - std::exp(-1.0 / (0.001 * effectiveSlewMs * sr)));
        }
        
        // CRITICAL FIX: Handle nullptr gates (not connected) and normalize gate values
        // Gates can be unipolar (0-1) or bipolar (-1 to 1), normalize to 0-1 for comparison
        // Improved detection: use threshold to distinguish bipolar vs unipolar
        auto normalizeGate = [this](float gate, bool isLeft) -> float {
            float& lastGate = isLeft ? lastGateLForNorm : lastGateRForNorm;
            // If signal goes below -0.5, assume bipolar and normalize
            bool isBipolar = (gate < -0.5f) || (lastGate < -0.5f);
            lastGate = gate;
            
            if (isBipolar) {
                // Bipolar signal: normalize from [-1, 1] to [0, 1]
                return juce::jlimit(0.0f, 1.0f, (gate + 1.0f) * 0.5f);
            }
            // Unipolar signal: assume [0, 1], just clamp
            return juce::jlimit(0.0f, 1.0f, gate);
        };
        
        const float gL = gateL != nullptr ? normalizeGate(gateL[i], true) : 0.0f;  // Not connected = always low
        const float gR = gateR != nullptr ? normalizeGate(gateR[i], false) : 0.0f;  // Not connected = always low
        
        // Apply hysteresis to prevent multiple triggers on noisy signals
        const float hyst = baseHysteresis;
        const float upperThr = currentThr + hyst;
        const float lowerThr = currentThr - hyst;
        
        // Edge detection with hysteresis: rising edge crosses upper threshold, falling edge crosses lower threshold
        const bool riseL = (gL > upperThr && lastGateL <= lowerThr);
        const bool fallL = (gL < lowerThr && lastGateL >= upperThr);
        const bool riseR = (gR > upperThr && lastGateR <= lowerThr);
        const bool fallR = (gR < lowerThr && lastGateR >= upperThr);

        const bool doL = (currentEdge == 0 && riseL) || (currentEdge == 1 && fallL) || (currentEdge == 2 && (riseL || fallL));
        const bool doR = (currentEdge == 0 && riseR) || (currentEdge == 1 && fallR) || (currentEdge == 2 && (riseR || fallR));

#if defined(DEBUG_SANDH_VERBOSE)
        // DEBUG: Log edge detection and latch events immediately
        if (riseL || fallL || riseR || fallR)
        {
            juce::String edgeTypeL = riseL ? "RISE" : (fallL ? "FALL" : "NONE");
            juce::String edgeTypeR = riseR ? "RISE" : (fallR ? "FALL" : "NONE");
            juce::Logger::writeToLog("[S&H] EDGE DETECTED | Sample " + juce::String(i) + 
                                     " | L: " + edgeTypeL + " (gL=" + juce::String(gL, 4) + 
                                     " thr=" + juce::String(thr, 4) + " last=" + juce::String(lastGateL, 4) + ")" +
                                     " | R: " + edgeTypeR + " (gR=" + juce::String(gR, 4) + 
                                     " thr=" + juce::String(thr, 4) + " last=" + juce::String(lastGateR, 4) + ")" +
                                     " | EdgeMode=" + juce::String(edge) + " (0=rise,1=fall,2=both)");
        }
#endif

        if (doL)
        {
            heldL = sigL[i];
#if defined(PRESET_CREATOR_UI)
            vizData.lastLatchValueL.store(heldL);
            vizData.latchAgeMsL.store(0.0f);
#endif
#if defined(DEBUG_SANDH_VERBOSE)
            // DEBUG: Log latch event
            juce::Logger::writeToLog("[S&H] LATCH L | Sample " + juce::String(i) + 
                                     " | Sampled value=" + juce::String(heldL, 4) + 
                                     " from signal=" + juce::String(sigL[i], 4));
#endif
        }
        if (doR)
        {
            heldR = sigR[i];
#if defined(PRESET_CREATOR_UI)
            vizData.lastLatchValueR.store(heldR);
            vizData.latchAgeMsR.store(0.0f);
#endif
#if defined(DEBUG_SANDH_VERBOSE)
            // DEBUG: Log latch event
            juce::Logger::writeToLog("[S&H] LATCH R | Sample " + juce::String(i) + 
                                     " | Sampled value=" + juce::String(heldR, 4) + 
                                     " from signal=" + juce::String(sigR[i], 4));
#endif
        }
        lastGateL = gL; lastGateR = gR;
        // Slew limiting toward target
        outL = outL + slewCoeff * (heldL - outL);
        outR = outR + slewCoeff * (heldR - outR);
        outLw[i] = outL;
        outRw[i] = outR;
    }
    
    // Store live modulated values for UI display (use last sample values)
    setLiveParamValue("threshold_live", finalThreshold);
    setLiveParamValue("edge_live", static_cast<float>(finalEdge));
    setLiveParamValue("slewMs_live", finalSlewMs);

    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outLw[numSamples - 1]);
        if (lastOutputValues[1]) lastOutputValues[1]->store(outRw[numSamples - 1]);
    }

#if defined(PRESET_CREATOR_UI)
    const int captureChannels = juce::jmin(4, in.getNumChannels());
    if (captureBuffer.getNumSamples() < numSamples)
        captureBuffer.setSize(4, numSamples, false, false, true);
    captureBuffer.clear();
    for (int ch = 0; ch < captureChannels; ++ch)
        captureBuffer.copyFrom(ch, 0, in, ch, 0, numSamples);

    auto downsampleTo = [&](const float* src, std::array<std::atomic<float>, VizData::waveformPoints>& dest)
    {
        if (!src)
        {
            for (auto& v : dest) v.store(0.0f);
            return;
        }
        for (int i = 0; i < VizData::waveformPoints; ++i)
        {
            const float t = (float)i / (float)(VizData::waveformPoints - 1);
            const int idx = juce::jlimit(0, numSamples - 1, (int)std::round(t * (float)(numSamples - 1)));
            dest[(size_t)i].store(src[idx]);
        }
    };

    downsampleTo(sigL, vizData.signalL);
    downsampleTo(sigR, vizData.signalR);
    downsampleTo(gateL, vizData.gateL);
    downsampleTo(gateR, vizData.gateR);
    downsampleTo(outLw, vizData.heldWaveL);
    downsampleTo(outRw, vizData.heldWaveR);

    auto writeMarkers = [&](const float* gate, float threshold, std::array<std::atomic<uint8_t>, VizData::waveformPoints>& dest)
    {
        for (int i = 0; i < VizData::waveformPoints; ++i)
            dest[(size_t)i].store(0);
        if (!gate) return;
        // Normalize gate values same as in main processing loop
        auto normalizeGate = [](float g) -> float {
            if (g < 0.0f) return (g + 1.0f) * 0.5f;
            return juce::jlimit(0.0f, 1.0f, g);
        };
        float last = normalizeGate(gate[0]);
        for (int n = 1; n < numSamples; ++n)
        {
            const float gNorm = normalizeGate(gate[n]);
            const bool rise = gNorm > threshold && last <= threshold;
            const bool fall = gNorm < threshold && last >= threshold;
            if (rise || fall)
            {
                const int dsIdx = juce::jlimit(0, VizData::waveformPoints - 1, (int)std::round((float)n / (float)(numSamples - 1) * (VizData::waveformPoints - 1)));
                uint8_t code = dest[(size_t)dsIdx].load();
                if (rise) code |= 1;
                if (fall) code |= 2;
                dest[(size_t)dsIdx].store(code);
            }
            last = gNorm;
        }
    };

    writeMarkers(gateL, finalThreshold, vizData.gateMarkersL);
    writeMarkers(gateR, finalThreshold, vizData.gateMarkersR);

    const float blockMs = (float)numSamples / (float)juce::jmax(1.0, sr) * 1000.0f;
    vizData.latchAgeMsL.store(vizData.latchAgeMsL.load() + blockMs);
    vizData.latchAgeMsR.store(vizData.latchAgeMsR.load() + blockMs);
    vizData.liveThreshold.store(finalThreshold);
    vizData.liveSlewMs.store(finalSlewMs);
    vizData.liveEdgeMode.store(finalEdge);
    auto computeAbsPeak = [&](const float* data)
    {
        if (!data) return 0.0f;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax(peak, std::abs(data[i]));
        return peak;
    };
    vizData.gatePeakL.store(computeAbsPeak(gateL));
    vizData.gatePeakR.store(computeAbsPeak(gateR));
#endif
}

#if defined(PRESET_CREATOR_UI)
void SAndHModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();

    float threshold = thresholdParam != nullptr ? thresholdParam->load() : 0.5f;
    float slew = slewMsParam != nullptr ? slewMsParam->load() : 0.0f;
    int edge = edgeParam != nullptr ? edgeParam->getIndex() : 0;

    std::array<float, VizData::waveformPoints> signalL {};
    std::array<float, VizData::waveformPoints> signalR {};
    std::array<float, VizData::waveformPoints> gateL {};
    std::array<float, VizData::waveformPoints> gateR {};
    std::array<float, VizData::waveformPoints> heldWaveL {};
    std::array<float, VizData::waveformPoints> heldWaveR {};
    std::array<uint8_t, VizData::waveformPoints> markersL {};
    std::array<uint8_t, VizData::waveformPoints> markersR {};
    for (int i = 0; i < VizData::waveformPoints; ++i)
    {
        signalL[(size_t)i] = vizData.signalL[(size_t)i].load();
        signalR[(size_t)i] = vizData.signalR[(size_t)i].load();
        gateL[(size_t)i] = vizData.gateL[(size_t)i].load();
        gateR[(size_t)i] = vizData.gateR[(size_t)i].load();
        heldWaveL[(size_t)i] = vizData.heldWaveL[(size_t)i].load();
        heldWaveR[(size_t)i] = vizData.heldWaveR[(size_t)i].load();
        markersL[(size_t)i] = vizData.gateMarkersL[(size_t)i].load();
        markersR[(size_t)i] = vizData.gateMarkersR[(size_t)i].load();
    }

    const float liveThreshold = vizData.liveThreshold.load();
    const float liveThresholdBip = juce::jmap(liveThreshold, 0.0f, 1.0f, -1.0f, 1.0f);
    const float liveSlewMs = vizData.liveSlewMs.load();
    const int liveEdge = vizData.liveEdgeMode.load();
    const float latchValueL = vizData.lastLatchValueL.load();
    const float latchValueR = vizData.lastLatchValueR.load();
    const float latchAgeL = vizData.latchAgeMsL.load();
    const float latchAgeR = vizData.latchAgeMsR.load();
    const float gatePeakL = vizData.gatePeakL.load();
    const float gatePeakR = vizData.gatePeakR.load();

    const auto& freqColors = theme.modules.frequency_graph;
    auto resolveColor = [](ImU32 value, ImU32 fallback) { return value != 0 ? value : fallback; };
    const ImU32 bgColor = resolveColor(freqColors.background, IM_COL32(18, 20, 24, 255));
    const ImU32 gridColor = resolveColor(freqColors.grid, IM_COL32(50, 55, 65, 255));
    const ImU32 signalColor = resolveColor(freqColors.live_line, IM_COL32(130, 210, 255, 255));
    const ImU32 heldColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre);
    const ImU32 gateColor = resolveColor(freqColors.peak_line, IM_COL32(255, 180, 120, 220));
    const ImU32 thresholdColor = resolveColor(freqColors.threshold, IM_COL32(200, 255, 140, 150));
    const ImU32 risingColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency);
    const ImU32 fallingColor = ImGui::ColorConvertFloat4ToU32(theme.modulation.amplitude);

    ImGui::PushID(this);
    ImGui::PushItemWidth(itemWidth);

    const ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginChild("SandHScope", ImVec2(itemWidth, 170.0f), false, childFlags))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        const ImVec2 p1 { p0.x + childSize.x, p0.y + childSize.y };
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        drawList->PushClipRect(p0, p1, true);

        const float paddingX = 8.0f;
        auto xForIndex = [&](int idx)
        {
            return juce::jmap((float)idx, 0.0f, (float)(VizData::waveformPoints - 1), p0.x + paddingX, p1.x - paddingX);
        };
        auto yForValue = [&](float sample)
        {
            const float clamped = juce::jlimit(-1.2f, 1.2f, sample);
            return juce::jmap(clamped, 1.2f, -1.2f, p0.y + 12.0f, p1.y - 12.0f);
        };
        drawList->AddLine(ImVec2(p0.x, yForValue(0.0f)), ImVec2(p1.x, yForValue(0.0f)), gridColor, 1.0f);

        auto drawWave = [&](const std::array<float, VizData::waveformPoints>& data, ImU32 color, float thickness)
        {
            float prevX = xForIndex(0);
            float prevY = yForValue(data[0]);
            for (int i = 1; i < VizData::waveformPoints; ++i)
            {
                const float x = xForIndex(i);
                const float y = yForValue(data[(size_t)i]);
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), color, thickness);
                prevX = x;
                prevY = y;
            }
        };

        drawWave(signalL, signalColor, 1.5f);
        drawWave(signalR, ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre), 1.2f);
        drawWave(heldWaveL, heldColor, 2.0f);
        drawWave(gateL, gateColor, 1.0f);

        const float thrY = yForValue(liveThresholdBip);
        drawList->AddLine(ImVec2(p0.x + 4.0f, thrY), ImVec2(p1.x - 4.0f, thrY), thresholdColor, 1.3f);
        drawList->AddText(ImVec2(p1.x - 70.0f, thrY - ImGui::GetTextLineHeight()), thresholdColor, "Threshold");

        auto drawMarkers = [&](const std::array<uint8_t, VizData::waveformPoints>& markers, bool topRow)
        {
            for (int i = 0; i < VizData::waveformPoints; ++i)
            {
                const uint8_t code = markers[(size_t)i];
                if (code == 0) continue;
                const float x = xForIndex(i);
                const float baseY = topRow ? (p0.y + 16.0f) : (p1.y - 16.0f);
                if (code & 1)
                {
                    drawList->AddTriangleFilled(ImVec2(x, baseY - 6.0f),
                                                ImVec2(x - 4.0f, baseY + 4.0f),
                                                ImVec2(x + 4.0f, baseY + 4.0f),
                                                risingColor);
                }
                if (code & 2)
                {
                    drawList->AddTriangleFilled(ImVec2(x, baseY + 6.0f),
                                                ImVec2(x - 4.0f, baseY - 4.0f),
                                                ImVec2(x + 4.0f, baseY - 4.0f),
                                                fallingColor);
                }
            }
        };

        drawMarkers(markersL, true);
        drawMarkers(markersR, false);

        drawList->PopClipRect();
        drawList->AddText(ImVec2(p0.x + 8.0f, p0.y + 6.0f), IM_COL32(220, 220, 230, 255), "Signal / Gate Monitor");

        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("ScopeDragBlocker", childSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();

    ImGui::Spacing();

    if (ImGui::BeginChild("SandHMeters", ImVec2(itemWidth, 90.0f), false, childFlags))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        const ImVec2 p1 { p0.x + childSize.x, p0.y + childSize.y };
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);

        auto drawMeter = [&](float xStart, float peak, const char* label)
        {
            const float meterWidth = (childSize.x - 80.0f) * 0.5f;
            const float meterHeight = 60.0f;
            const ImVec2 base { xStart, p0.y + 20.0f };
            const ImVec2 rectMax { base.x + meterWidth, base.y + meterHeight };
            drawList->AddRectFilled(base, rectMax, IM_COL32(30, 32, 38, 255), 3.0f);
            const float fill = juce::jlimit(0.0f, 1.0f, peak);
            const float filledHeight = meterHeight * fill;
            drawList->AddRectFilled(ImVec2(base.x + 2.0f, rectMax.y - filledHeight),
                                    ImVec2(rectMax.x - 2.0f, rectMax.y - 2.0f),
                                    ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency), 3.0f);
            const float thrNorm = juce::jlimit(0.0f, 1.0f, liveThreshold);
            const float thrY = rectMax.y - meterHeight * thrNorm;
            drawList->AddLine(ImVec2(base.x + 2.0f, thrY), ImVec2(rectMax.x - 2.0f, thrY), thresholdColor, 1.2f);
            drawList->AddText(ImVec2(base.x, p0.y + 4.0f), IM_COL32(200, 200, 210, 255), label);
        };

        drawMeter(p0.x + 12.0f, gatePeakL, "Gate L");
        drawMeter(p0.x + childSize.x * 0.5f + 8.0f, gatePeakR, "Gate R");

        const char* edgeLabels[] = { "Rising", "Falling", "Both" };
        drawList->AddText(ImVec2(p1.x - 120.0f, p0.y + 8.0f), IM_COL32(180, 180, 190, 255), "Edge Mode");
        drawList->AddText(ImVec2(p1.x - 120.0f, p0.y + 28.0f), IM_COL32(230, 230, 240, 255), edgeLabels[juce::jlimit(0, 2, liveEdge)]);

        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("MetersDragBlocker", childSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();

    ImGui::Spacing();

    if (ImGui::BeginChild("SandHTimeline", ImVec2(itemWidth, 110.0f), false, childFlags))
    {
        auto* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 childSize = ImGui::GetWindowSize();
        const ImVec2 p1 { p0.x + childSize.x, p0.y + childSize.y };
        drawList->AddRectFilled(p0, p1, bgColor, 4.0f);
        drawList->PushClipRect(p0, p1, true);

        auto xForIndex = [&](int idx)
        {
            return juce::jmap((float)idx, 0.0f, (float)(VizData::waveformPoints - 1), p0.x + 6.0f, p1.x - 6.0f);
        };
        auto yForValue = [&](float v)
        {
            const float clamped = juce::jlimit(-1.2f, 1.2f, v);
            return juce::jmap(clamped, 1.2f, -1.2f, p0.y + 16.0f, p1.y - 16.0f);
        };

        auto drawLine = [&](const std::array<float, VizData::waveformPoints>& data, ImU32 color)
        {
            float prevX = xForIndex(0);
            float prevY = yForValue(data[0]);
            for (int i = 1; i < VizData::waveformPoints; ++i)
            {
                const float x = xForIndex(i);
                const float y = yForValue(data[(size_t)i]);
                drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), color, 1.6f);
                prevX = x;
                prevY = y;
            }
        };

        drawLine(heldWaveL, heldColor);
        drawLine(heldWaveR, ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency));
        drawList->PopClipRect();

        drawList->AddText(ImVec2(p0.x + 8.0f, p0.y + 6.0f), IM_COL32(210, 210, 220, 255), "Held output timeline");
        ImGui::SetCursorPos(ImVec2(8.0f, 60.0f));
        ImGui::Text("Last L latch: %.3f (%.1f ms ago)", latchValueL, latchAgeL);
        ImGui::Text("Last R latch: %.3f (%.1f ms ago)", latchValueR, latchAgeR);

        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
        ImGui::InvisibleButton("TimelineDragBlocker", childSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ThemeText("HELD VALUES", theme.modulation.frequency);
    const float labelWidth = ImGui::CalcTextSize("R:").x;
    const float valueWidth = ImGui::CalcTextSize("-0.000").x;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float barWidth = itemWidth - labelWidth - valueWidth - spacing * 2.0f;
    ImGui::Text("L:");
    ImGui::SameLine();
    ImGui::ProgressBar((latchValueL + 1.0f) * 0.5f, ImVec2(barWidth, 0), "");
    ImGui::SameLine();
    ImGui::Text("%.3f", latchValueL);

    ImGui::Text("R:");
    ImGui::SameLine();
    ImGui::ProgressBar((latchValueR + 1.0f) * 0.5f, ImVec2(barWidth, 0), "");
    ImGui::SameLine();
    ImGui::Text("%.3f", latchValueR);

    auto computeRms = [](const std::array<float, VizData::waveformPoints>& data)
    {
        double sum = 0.0;
        for (float v : data) sum += v * v;
        return (float)std::sqrt(sum / (double)data.size());
    };
    ImGui::Text("Input RMS  L: %.3f  R: %.3f", computeRms(signalL), computeRms(signalR));

    ImGui::Spacing();
    ThemeText("SAMPLE SETTINGS", theme.modulation.frequency);
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 10.0f);

    const bool threshMod = isParamModulated("threshold_mod");
    float thresholdDisplay = threshMod ? getLiveParamValueFor("threshold_mod", "threshold_live", threshold) : threshold;
    if (threshMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Threshold", &thresholdDisplay, 0.0f, 1.0f, "%.3f"))
        if (!threshMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("threshold")))
                *p = thresholdDisplay;
    if (!threshMod) adjustParamOnWheel(ap.getParameter("threshold"), "threshold", thresholdDisplay);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (threshMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    ImGui::Text("= %.3f", juce::jmap(thresholdDisplay, 0.0f, 1.0f, -1.0f, 1.0f));

    const bool edgeMod = isParamModulated("edge_mod");
    int edgeDisplay = edge;
    if (edgeMod)
    {
        edgeDisplay = static_cast<int>(getLiveParamValueFor("edge_mod", "edge_live", (float)edge));
        ImGui::BeginDisabled();
    }
    const char* edgeItems = "Rising\0Falling\0Both\0\0";
    if (ImGui::Combo("Edge", &edgeDisplay, edgeItems))
        if (!edgeMod)
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("edge")))
                *p = edgeDisplay;
    if (!edgeMod && ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int newEdge = juce::jlimit(0, 2, edgeDisplay + (wheel > 0.0f ? -1 : 1));
            if (newEdge != edgeDisplay)
            {
                edgeDisplay = newEdge;
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter("edge")))
                    *p = edgeDisplay;
                onModificationEnded();
            }
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (edgeMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    const bool slewMod = isParamModulated("slewMs_mod");
    float slewDisplay = slewMod ? getLiveParamValueFor("slewMs_mod", "slewMs_live", slew) : slew;
    if (slewMod) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Slew", &slewDisplay, 0.0f, 2000.0f, "%.1f ms", ImGuiSliderFlags_Logarithmic))
        if (!slewMod)
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter("slewMs")))
                *p = slewDisplay;
    if (!slewMod) adjustParamOnWheel(ap.getParameter("slewMs"), "slewMs", slewDisplay);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (slewMod) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();
    ImGui::PopID();
}
#endif

// Parameter bus contract implementation
bool SAndHModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    if (paramId == "threshold_mod") { outChannelIndexInBus = 4; return true; }
    if (paramId == "edge_mod") { outChannelIndexInBus = 5; return true; }
    if (paramId == "slewMs_mod") { outChannelIndexInBus = 6; return true; }
    return false;
}


