#include "FrequencyGraphModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout FrequencyGraphModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdDecay, "Decay Time", 0.90f, 0.999f, 0.98f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSubThreshold, "Sub Threshold", -96.0f, 0.0f, -24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdBassThreshold, "Bass Threshold", -96.0f, 0.0f, -24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMidThreshold, "Mid Threshold", -96.0f, 0.0f, -24.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdHighThreshold, "High Threshold", -96.0f, 0.0f, -24.0f));
    return { params.begin(), params.end() };
}

FrequencyGraphModuleProcessor::FrequencyGraphModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("In", juce::AudioChannelSet::mono(), true)
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)
          .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(TotalCVOutputs), true)),
      apvts(*this, nullptr, "FreqGraphParams", createParameterLayout()),
      fft(fftOrder),
      window(fftSize, juce::dsp::WindowingFunction<float>::hann),
      abstractFifo(128)
{
    fftInputBuffer.resize(fftSize, 0.0f);
    fftData.resize(fftSize * 2, 0.0f);

    fifoBuffer.resize(128);
    for (auto& frame : fifoBuffer) { frame.resize(fftSize / 2 + 1, 0.0f); }
    latestFftData.resize(fftSize / 2 + 1, -100.0f);
    peakHoldData.resize(fftSize / 2 + 1, -100.0f);

    decayParam = apvts.getRawParameterValue(paramIdDecay);
    subThresholdParam = apvts.getRawParameterValue(paramIdSubThreshold);
    bassThresholdParam = apvts.getRawParameterValue(paramIdBassThreshold);
    midThresholdParam = apvts.getRawParameterValue(paramIdMidThreshold);
    highThresholdParam = apvts.getRawParameterValue(paramIdHighThreshold);
    
    juce::Logger::writeToLog("[FrequencyGraph] Constructor: Instance created.");
}

void FrequencyGraphModuleProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    samplesAccumulated = 0;
    abstractFifo.reset();
    std::fill(latestFftData.begin(), latestFftData.end(), -100.0f);
    std::fill(peakHoldData.begin(), peakHoldData.end(), -100.0f);
    for(auto& analyser : bandAnalysers)
    {
        analyser.lastGateState = false;
        analyser.triggerSamplesRemaining = 0;
    }
    juce::Logger::writeToLog("[FrequencyGraph] prepareToPlay: State reset for sample rate " + juce::String(sampleRate));
}

void FrequencyGraphModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& /*midi*/)
{
    auto inBus = getBusBuffer(buffer, true, 0);
    auto audioOutBus = getBusBuffer(buffer, false, 0);
    auto cvOutBus = getBusBuffer(buffer, false, 1);
    
    const int numSamples = buffer.getNumSamples();
    const double sampleRate = getSampleRate();

    const float* inputData = inBus.getNumChannels() > 0 ? inBus.getReadPointer(0) : nullptr;

    // --- DEBUG-INPLACE: Check if clearing/writing would nuke inputs (shared buffer case) ---
    float rmsBefore = 0.0f, rmsAfter = 0.0f;
    if (inBus.getNumChannels() > 0)
        rmsBefore = inBus.getRMSLevel(0, 0, numSamples);

    // --- REAL-TIME SAFE FFT PROCESSING ---
    // Instead of looping through every sample individually, we process the buffer in chunks.
    // This prevents the expensive FFT from blocking individual sample processing.
    if (inputData)
    {
        // Update threshold values from parameters
        bandAnalysers[0].thresholdDb = subThresholdParam->load();
        bandAnalysers[1].thresholdDb = bassThresholdParam->load();
        bandAnalysers[2].thresholdDb = midThresholdParam->load();
        bandAnalysers[3].thresholdDb = highThresholdParam->load();
        
        int inputSamplesConsumed = 0;
        
        // Process the incoming buffer in chunks
        while (inputSamplesConsumed < numSamples)
        {
            // Calculate how many samples we can copy before the FFT buffer is full
            const int samplesToCopy = std::min(numSamples - inputSamplesConsumed, 
                                               fftSize - samplesAccumulated);
            
            // Quickly copy the available data into our FFT buffer
            std::copy(inputData + inputSamplesConsumed,
                      inputData + inputSamplesConsumed + samplesToCopy,
                      fftInputBuffer.begin() + samplesAccumulated);
            
            samplesAccumulated += samplesToCopy;
            inputSamplesConsumed += samplesToCopy;

            // If the FFT buffer is full, process it now
            // This happens outside the per-sample loop, preventing stalls
            if (samplesAccumulated >= fftSize)
            {
                // 1. Perform FFT
                std::fill(fftData.begin(), fftData.end(), 0.0f);
                std::copy(fftInputBuffer.begin(), fftInputBuffer.end(), fftData.begin());
                window.multiplyWithWindowingTable(fftData.data(), fftSize);
                fft.performFrequencyOnlyForwardTransform(fftData.data());

                // 2. Analyze frequency bands and update gate/trigger states
                float bandEnergyDb[4] = { -100.0f, -100.0f, -100.0f, -100.0f };
                const float bandRanges[] = { 60, 250, 2000, 8000, 22000 };
                int currentBand = 0;
                float maxInBand = 0.0f;

                for (int bin = 1; bin < fftSize / 2 + 1; ++bin)
                {
                    float freq = (float)bin * (float)sampleRate / (float)fftSize;
                    if (freq > bandRanges[currentBand])
                    {
                        // FIX: Correctly normalize FFT magnitude before converting to dB
                        float normalizedMagnitude = maxInBand / (float)fftSize;
                        bandEnergyDb[currentBand] = juce::Decibels::gainToDecibels(normalizedMagnitude, -100.0f);
                        currentBand++;
                        maxInBand = 0.0f;
                        if (currentBand >= 4) break;
                    }
                    maxInBand = std::max(maxInBand, fftData[bin]);
                }

                // Update gate/trigger states for each band
                for (int band = 0; band < 4; ++band)
                {
                    bool gateState = bandEnergyDb[band] > bandAnalysers[band].thresholdDb;
                    if (gateState && !bandAnalysers[band].lastGateState)
                    {
                        bandAnalysers[band].triggerSamplesRemaining = (int)(sampleRate * 0.001);
                    }
                    bandAnalysers[band].lastGateState = gateState;
                }

                // 3. Push data to UI thread via lock-free FIFO
                int start1, size1, start2, size2;
                abstractFifo.prepareToWrite(1, start1, size1, start2, size2);
                if (size1 > 0)
                {
                    for (int bin = 0; bin < fftSize / 2 + 1; ++bin)
                    {
                        // FIX: Correctly normalize FFT magnitude before converting to dB
                        float magnitude = fftData[bin] / (float)fftSize;
                        fifoBuffer[start1][bin] = juce::Decibels::gainToDecibels(magnitude, -100.0f);
                    }
                    abstractFifo.finishedWrite(1);
                }

                // 4. Shift buffer for overlap-add (hop size)
                std::move(fftInputBuffer.begin() + hopSize, fftInputBuffer.end(), fftInputBuffer.begin());
                samplesAccumulated -= hopSize;
            }
        }
    }

    // --- HANDLE AUDIO PASSTHROUGH ---
    // The analysis is done. Now we can safely write to our output buffers.
    if (inBus.getNumChannels() > 0 && audioOutBus.getNumChannels() > 0)
    {
        audioOutBus.copyFrom(0, 0, inBus, 0, 0, numSamples); // L channel
        if (audioOutBus.getNumChannels() > 1)
        {
            audioOutBus.copyFrom(1, 0, inBus, 0, 0, numSamples); // R channel (duplicate mono)
        }
    }
    else
    {
        audioOutBus.clear();
    }

    if (inBus.getNumChannels() > 0)
    {
        rmsAfter = inBus.getRMSLevel(0, 0, numSamples);
        static int dbgCtr = 0;
        if ((++dbgCtr % 200) == 0 && rmsBefore > 1.0e-5f)
            juce::Logger::writeToLog("[DEBUG-INPLACE] FreqGraph RMS before/after output ops: " + juce::String(rmsBefore, 6) + " / " + juce::String(rmsAfter, 6));
    }

    // --- WRITE CV/GATE OUTPUTS ---
    // This loop uses the gate/trigger states that were last updated by the FFT block.
    // The state is held constant between FFT calculations, which is the correct behavior.
    cvOutBus.clear();
    for (int i = 0; i < numSamples; ++i)
    {
        for (int band = 0; band < 4; ++band)
        {
            if (cvOutBus.getNumChannels() > (1 + band * 2))
            {
                float* gateOut = cvOutBus.getWritePointer(band * 2);
                float* trigOut = cvOutBus.getWritePointer(band * 2 + 1);
                gateOut[i] = bandAnalysers[band].lastGateState ? 1.0f : 0.0f;
                trigOut[i] = (bandAnalysers[band].triggerSamplesRemaining > 0) ? 1.0f : 0.0f;
            }
        }
        
        // Decrement trigger counters once per sample for all bands
        for (auto& analyser : bandAnalysers)
        {
            if (analyser.triggerSamplesRemaining > 0)
            {
                analyser.triggerSamplesRemaining--;
            }
        }
    }
}

#if defined(PRESET_CREATOR_UI)

void FrequencyGraphModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)>&, const std::function<void()>& onModificationEnded)
{
    if (!isFrozen && abstractFifo.getNumReady() > 0)
    {
        int start1, size1, start2, size2;
        abstractFifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 > 0) { latestFftData = fifoBuffer[start1]; }
        abstractFifo.finishedRead(1);
    }
    
    float decayFactor = decayParam->load();
    for (size_t i = 0; i < latestFftData.size(); ++i)
    {
        if (latestFftData[i] > peakHoldData[i])
        {
            peakHoldData[i] = latestFftData[i];
        }
        else
        {
            peakHoldData[i] = peakHoldData[i] * decayFactor + (1.0f - decayFactor) * -100.0f;
        }
        peakHoldData[i] = juce::jmax(-100.0f, peakHoldData[i]);
    }

    const float graphWidth = 400.0f;
    const float graphHeight = 200.0f;
    ImGui::Dummy(ImVec2(graphWidth, graphHeight));
    ImVec2 p0 = ImGui::GetItemRectMin();
    ImVec2 p1 = ImGui::GetItemRectMax();
    auto* drawList = ImGui::GetWindowDrawList();
    
    // --- Define the dB range with more headroom ---
    const float minDb = -96.0f;
    const float maxDb = 24.0f;

    // --- FIX 1: Add a clipping rectangle WITHOUT intersecting with parent ---
    // The 'false' parameter means don't intersect with current clip rect
    drawList->PushClipRect(p0, p1, false);

    // Draw background and grid lines
    drawList->AddRectFilled(p0, p1, IM_COL32(20, 22, 24, 255));
    
    // --- Adjust grid lines for the new range ---
    for (int db = 12; db >= (int)minDb; db -= 12)
    {
        float y = juce::jmap((float)db, minDb, maxDb, p1.y, p0.y);
        drawList->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(50, 55, 60, 255));
        if (db <= 12) // Only draw labels within the old visible range to avoid clutter
        {
            drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(p0.x + 4, y - 14), IM_COL32(150, 150, 150, 255), juce::String(db).toRawUTF8());
        }
    }
    const float freqs[] = { 30, 100, 300, 1000, 3000, 10000, 20000 };
    for (float freq : freqs)
    {
        float x = juce::jmap(std::log10(freq), std::log10(20.0f), std::log10(22000.0f), p0.x, p1.x);
        drawList->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(50, 55, 60, 255));
    }
    
    // --- FIX 2: Draw two separate lines instead of a filled polygon ---
    // Helper lambda to draw a line graph from a data vector
    auto drawLineGraph = [&](const std::vector<float>& data, ImU32 color, float thickness) {
        if (data.size() < 2) return;
        for (size_t i = 1; i < data.size(); ++i)
        {
            float freq_prev = (float)(i - 1) * (float)getSampleRate() / (float)fftSize;
            float freq_curr = (float)i * (float)getSampleRate() / (float)fftSize;

            if (freq_curr < 20.0f) continue;
            if (freq_prev > 22000.0f) break;

            float x_prev = p0.x + (std::log10(freq_prev) - std::log10(20.0f)) / (std::log10(22000.0f) - std::log10(20.0f)) * graphWidth;
            // Use new dB range for data plotting
            float y_prev = p1.y + (data[i - 1] - minDb) / (maxDb - minDb) * (p0.y - p1.y);

            float x_curr = p0.x + (std::log10(freq_curr) - std::log10(20.0f)) / (std::log10(22000.0f) - std::log10(20.0f)) * graphWidth;
            float y_curr = p1.y + (data[i] - minDb) / (maxDb - minDb) * (p0.y - p1.y);
            
            drawList->AddLine(ImVec2(x_prev, y_prev), ImVec2(x_curr, y_curr), color, thickness);
        }
    };

    // Draw the peak-hold line (dimmer, in the background)
    drawLineGraph(peakHoldData, IM_COL32(255, 150, 80, 150), 1.5f);

    // Draw the live FFT data line (brighter, on top)
    drawLineGraph(latestFftData, IM_COL32(120, 170, 255, 220), 2.0f);
    
    // Border and clip cleanup
    drawList->AddRect(p0, p1, IM_COL32(80, 80, 80, 255));
    drawList->PopClipRect(); // Pop the clipping rectangle

    ImGui::PushItemWidth(itemWidth);
    
    ImGui::Checkbox("Freeze", &isFrozen);
    
    auto& ap = getAPVTS();
    float decay = decayParam->load();
    if (ImGui::SliderFloat("Decay", &decay, 0.90f, 0.999f, "%.3f", ImGuiSliderFlags_Logarithmic)) {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(FrequencyGraphModuleProcessor::paramIdDecay)))
        {
            *p = decay;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();

    auto drawThresholdSlider = [&](const char* label, std::atomic<float>* param, const char* paramId) {
        float val = param->load();
        if (ImGui::SliderFloat(label, &val, -96.0f, 0.0f, "%.1f dB")) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramId)))
            {
                *p = val;
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) onModificationEnded();
        // Use new dB range for threshold line plotting
        float y = juce::jmap(val, minDb, maxDb, p1.y, p0.y);
        drawList->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(255, 100, 100, 150), 1.5f);
    };

    drawThresholdSlider("Sub Thr", subThresholdParam, FrequencyGraphModuleProcessor::paramIdSubThreshold);
    drawThresholdSlider("Bass Thr", bassThresholdParam, FrequencyGraphModuleProcessor::paramIdBassThreshold);
    drawThresholdSlider("Mid Thr", midThresholdParam, FrequencyGraphModuleProcessor::paramIdMidThreshold);
    drawThresholdSlider("High Thr", highThresholdParam, FrequencyGraphModuleProcessor::paramIdHighThreshold);

    ImGui::PopItemWidth();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        if (ImGui::IsMousePosValid(&mousePos) && mousePos.x >= p0.x && mousePos.x <= p1.x && mousePos.y >= p0.y && mousePos.y <= p1.y)
        {
            float mouseFreq = std::pow(10.0f, juce::jmap(mousePos.x, p0.x, p1.x, std::log10(20.0f), std::log10(22000.0f)));
            // Use new dB range for tooltip calculation
            float mouseDb = juce::jmap(mousePos.y, p1.y, p0.y, minDb, maxDb);
            ImGui::BeginTooltip();
            ImGui::Text("%.1f Hz", mouseFreq);
            ImGui::Text("%.1f dB", mouseDb);
            ImGui::EndTooltip();
        }
    }
}

void FrequencyGraphModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawParallelPins("In", 0, "Out L", 0);
    helpers.drawParallelPins(nullptr, -1, "Out R", 1);
    ImGui::Spacing();
    helpers.drawParallelPins(nullptr, -1, "Sub Gate", 2);
    helpers.drawParallelPins(nullptr, -1, "Sub Trig", 3);
    helpers.drawParallelPins(nullptr, -1, "Bass Gate", 4);
    helpers.drawParallelPins(nullptr, -1, "Bass Trig", 5);
    helpers.drawParallelPins(nullptr, -1, "Mid Gate", 6);
    helpers.drawParallelPins(nullptr, -1, "Mid Trig", 7);
    helpers.drawParallelPins(nullptr, -1, "High Gate", 8);
    helpers.drawParallelPins(nullptr, -1, "High Trig", 9);
}

#endif

juce::String FrequencyGraphModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel == 0) return "In";
    return {};
}

juce::String FrequencyGraphModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    
    int cvChannel = channel - 2;
    if (cvChannel >= 0)
    {
        switch (cvChannel)
        {
            case SubGate: return "Sub Gate";
            case SubTrig: return "Sub Trig";
            case BassGate: return "Bass Gate";
            case BassTrig: return "Bass Trig";
            case MidGate: return "Mid Gate";
            case MidTrig: return "Mid Trig";
            case HighGate: return "High Gate";
            case HighTrig: return "High Trig";
            default: return {};
        }
    }
    return {};
}

bool FrequencyGraphModuleProcessor::getParamRouting(const juce::String&, int&, int&) const
{
    return false;
}
