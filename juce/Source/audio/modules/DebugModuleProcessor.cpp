#include "DebugModuleProcessor.h"
#include "../../utils/RtLogger.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#include "../../preset_creator/PinDatabase.h"
#include "../../preset_creator/theme/ThemeManager.h"
#endif

juce::AudioProcessorValueTreeState::ParameterLayout DebugModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    return { params.begin(), params.end() };
}

DebugModuleProcessor::DebugModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("In", juce::AudioChannelSet::discreteChannels(8), true)),
      apvts(*this, nullptr, "DebugParams", createParameterLayout())
{
    fifoBuffer.resize(2048);
}

void DebugModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    totalSamples = 0;
    for (auto& v : lastReported) v = 0.0f;
    droppedEvents.store(0);
    for (auto& s : stats) { s.last = 0.0f; s.min = 1e9f; s.max = -1e9f; s.rmsAcc = 0.0f; s.rmsCount = 0; }
    
#if defined(PRESET_CREATOR_UI)
    captureBuffer.setSize(8, samplesPerBlock);
    captureBuffer.clear();
    for (auto& ch : vizData.inputWaveforms)
        for (auto& v : ch) v.store(0.0f);
    for (auto& c : vizData.eventMarkerCounts) c.store(0);
    for (auto& ch : vizData.eventMarkerPositions)
        for (auto& p : ch) p.store(-1);
    for (auto& r : vizData.inputRms) r.store(0.0f);
    vizData.writeIndex.store(0);
#endif
}

void DebugModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(getTotalNumInputChannels(), 8);

#if defined(PRESET_CREATOR_UI)
    // Prepare capture buffer
    if (captureBuffer.getNumSamples() < numSamples)
        captureBuffer.setSize(8, numSamples, false, false, true);
    
    auto in = getBusBuffer(buffer, true, 0);
    std::array<float, 8> rmsAcc = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::array<int, 8> eventPositions; // Store event positions in this block
    for (int i = 0; i < 8; ++i) eventPositions[i] = -1;
#endif

    int eventsThisBlock = 0;

    for (int ch = 0; ch < channels; ++ch)
    {
        if (! pinEnabled[(size_t) ch])
            continue;

        const float value = in.getMagnitude(ch, 0, numSamples);

        const float delta = std::abs(value - lastReported[(size_t) ch]);
        if (delta >= threshold && eventsThisBlock < maxEventsPerBlock)
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                fifoBuffer[(size_t) start1] = DebugEvent{ (juce::uint8) ch, value, totalSamples };
                fifo.finishedWrite(1);
                lastReported[(size_t) ch] = value;
                ++eventsThisBlock;
                
#if defined(PRESET_CREATOR_UI)
                // Mark event position (approximate - use middle of block)
                eventPositions[ch] = numSamples / 2;
#endif
            }
            else
            {
                droppedEvents.fetch_add(1);
            }
        }
        
#if defined(PRESET_CREATOR_UI)
        // Capture waveform and compute RMS
        if (in.getNumChannels() > ch)
        {
            const float* data = in.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
            {
                captureBuffer.setSample(ch, i, data[i]);
                rmsAcc[ch] += data[i] * data[i];
            }
        }
#endif
    }

#if defined(PRESET_CREATOR_UI)
    // Update visualization data
    const int stride = juce::jmax(1, numSamples / VizData::waveformPoints);
    
    for (int ch = 0; ch < 8; ++ch)
    {
        // Down-sample waveform
        for (int i = 0; i < VizData::waveformPoints && (i * stride) < numSamples; ++i)
        {
            const int sampleIdx = i * stride;
            vizData.inputWaveforms[ch][i].store(captureBuffer.getSample(ch, sampleIdx));
        }
        
        // Update RMS
        const float rms = numSamples > 0 ? std::sqrt(rmsAcc[ch] / numSamples) : 0.0f;
        vizData.inputRms[ch].store(rms);
        
        // Update event markers
        if (eventPositions[ch] >= 0)
        {
            // Convert sample position to waveform index
            const int waveformPos = juce::jlimit(0, VizData::waveformPoints - 1, eventPositions[ch] / stride);
            
            // Add marker (circular buffer)
            int markerCount = vizData.eventMarkerCounts[ch].load();
            int nextSlot = markerCount % VizData::maxEventMarkers;
            vizData.eventMarkerPositions[ch][nextSlot].store(waveformPos);
            vizData.eventMarkerCounts[ch].store(markerCount + 1);
        }
    }
#endif

    totalSamples += (juce::uint64) numSamples;
}

#if defined(PRESET_CREATOR_UI)
void DebugModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)> & /*isParamModulated*/, const std::function<void()> & /*onModificationEnded*/)
{
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::Checkbox("Pause", &uiPaused)) {}
    ImGui::SameLine(); ImGui::Text("Dropped: %u", droppedEvents.load());
    ImGui::SliderFloat("Threshold", &threshold, 0.0f, 0.05f, "%.4f");
    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const float step = 0.0005f;
            threshold = juce::jlimit (0.0f, 0.05f, threshold + (wheel > 0.0f ? step : -step));
        }
    }
    ImGui::SliderInt("Max events/block", &maxEventsPerBlock, 1, 512);
    if (ImGui::IsItemHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const int delta = wheel > 0.0f ? 1 : -1;
            maxEventsPerBlock = juce::jlimit (1, 512, maxEventsPerBlock + delta);
        }
    }
    if (ImGui::Button("Clear")) { uiEvents.clear(); for (auto& s : stats) { s.min = 1e9f; s.max = -1e9f; s.rmsAcc = 0.0f; s.rmsCount = 0; } }
    ImGui::SameLine();
    if (ImGui::Button("Copy CSV"))
    {
        // Build connection map from parent synth (src module/channel for each Debug input channel)
        juce::String csv;
        csv << "time_seconds,src_logical_id,src_module,src_channel,src_label,dst_logical_id,dst_module,dst_channel,dst_label,value\n";

        auto* synth = getParent();
        juce::uint32 selfLid = 0;
        std::array<std::vector<std::pair<juce::uint32,int>>, 8> chanSources; // per dst channel: list of (srcLid, srcChan)
        std::map<juce::uint32, juce::String> lidToType;
        if (synth != nullptr)
        {
            for (const auto& p : synth->getModulesInfo())
            {
                if (synth->getModuleForLogical(p.first) == this) { selfLid = p.first; break; }
            }
            for (const auto& p : synth->getModulesInfo()) lidToType[p.first] = p.second;
            if (selfLid != 0)
            {
                for (const auto& c : synth->getConnectionsInfo())
                {
                    if (!c.dstIsOutput && c.dstLogicalId == selfLid && c.dstChan >= 0 && c.dstChan < 8)
                        chanSources[(size_t) c.dstChan].push_back({ c.srcLogicalId, c.srcChan });
                }
            }
        }

        auto outputLabelFor = [&](const juce::String& moduleType, int channel, ModuleProcessor* mp) -> juce::String
        {
#if defined(PRESET_CREATOR_UI)
            auto it = getModulePinDatabase().find(moduleType);
            if (it != getModulePinDatabase().end())
            {
                for (const auto& ap : it->second.audioOuts)
                    if (ap.channel == channel) return ap.name;
            }
#endif
            return mp ? mp->getAudioOutputLabel(channel) : juce::String();
        };

        for (const auto& ev : uiEvents)
        {
            const double tSec = (currentSampleRate > 0.0 ? (double) ev.sampleCounter / currentSampleRate : 0.0);
            const int dstChan = (int) ev.pinIndex; // 0-based
            const juce::String dstModule = "Debug";
            if (synth != nullptr && selfLid != 0 && !chanSources[(size_t) dstChan].empty())
            {
                for (const auto& src : chanSources[(size_t) dstChan])
                {
                    ModuleProcessor* srcMp = synth->getModuleForLogical(src.first);
                    const juce::String srcName = lidToType.count(src.first) ? lidToType[src.first] : (srcMp ? srcMp->getName() : juce::String("<unknown>"));
                    const juce::String srcLabel = outputLabelFor(srcName, src.second, srcMp);
                    const juce::String dstLabel = getAudioInputLabel(dstChan);
                    csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcName << "," << juce::String(src.second) << "," << srcLabel
                        << "," << juce::String((int) selfLid) << "," << dstModule << "," << juce::String(dstChan) << "," << dstLabel << "," << juce::String(ev.value, 6) << "\n";
                }
            }
            else
            {
                const juce::String dstLabel = getAudioInputLabel(dstChan);
                csv << juce::String(tSec, 6) << ",,,,,," << juce::String((int) selfLid) << "," << dstModule << "," << juce::String(dstChan) << "," << dstLabel << "," << juce::String(ev.value, 6) << "\n";
            }
        }
        const juce::String copy = std::move(csv);
        std::string utf8 = copy.toStdString();
        ImGui::SetClipboardText(utf8.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Export CSV"))
    {
        static juce::String lastExportPath;
        juce::File dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              .getChildFile("ColliderDebugLogs");
        if (! dir.exists()) (void) dir.createDirectory();
        juce::File file = dir.getNonexistentChildFile("debug_log", ".csv");
        juce::FileOutputStream out (file);
        if (out.openedOk())
        {
            // Build mapping like in Copy CSV
            juce::String csv;
            csv << "time_seconds,src_logical_id,src_module,src_channel,src_label,dst_logical_id,dst_module,dst_channel,dst_label,value\n";
            auto* synth = getParent();
            juce::uint32 selfLid = 0;
            std::array<std::vector<std::pair<juce::uint32,int>>, 8> chanSources;
            std::map<juce::uint32, juce::String> lidToType;
            if (synth != nullptr)
            {
                for (const auto& p : synth->getModulesInfo())
                {
                    if (synth->getModuleForLogical(p.first) == this) { selfLid = p.first; break; }
                }
                for (const auto& p : synth->getModulesInfo()) lidToType[p.first] = p.second;
                if (selfLid != 0)
                {
                    for (const auto& c : synth->getConnectionsInfo())
                    {
                    if (!c.dstIsOutput && c.dstLogicalId == selfLid && c.dstChan >= 0 && c.dstChan < 8)
                        chanSources[(size_t) c.dstChan].push_back({ c.srcLogicalId, c.srcChan });
                    }
                }
            }

            auto outputLabelFor = [&](const juce::String& moduleType, int channel, ModuleProcessor* mp) -> juce::String
            {
#if defined(PRESET_CREATOR_UI)
                auto it = getModulePinDatabase().find(moduleType);
                if (it != getModulePinDatabase().end())
                {
                    for (const auto& ap : it->second.audioOuts)
                        if (ap.channel == channel) return ap.name;
                }
#endif
                return mp ? mp->getAudioOutputLabel(channel) : juce::String();
            };

            for (const auto& ev : uiEvents)
            {
                const double tSec = (currentSampleRate > 0.0 ? (double) ev.sampleCounter / currentSampleRate : 0.0);
                const int dstChan = (int) ev.pinIndex;
                const juce::String dstModule = "Debug";
                if (synth != nullptr && selfLid != 0 && !chanSources[(size_t) dstChan].empty())
                {
                    for (const auto& src : chanSources[(size_t) dstChan])
                    {
                        ModuleProcessor* srcMp = synth->getModuleForLogical(src.first);
                        const juce::String srcName = lidToType.count(src.first) ? lidToType[src.first] : (srcMp ? srcMp->getName() : juce::String("<unknown>"));
                        const juce::String srcLabel = outputLabelFor(srcName, src.second, srcMp);
                        const juce::String dstLabel = getAudioInputLabel(dstChan);
                        csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcName << "," << juce::String(src.second) << "," << srcLabel
                            << "," << juce::String((int) selfLid) << "," << dstModule << "," << juce::String(dstChan) << "," << dstLabel << "," << juce::String(ev.value, 6) << "\n";
                    }
                }
                else
                {
                    const juce::String dstLabel = getAudioInputLabel(dstChan);
                    csv << juce::String(tSec, 6) << ",,,,,," << juce::String((int) selfLid) << "," << dstModule << "," << juce::String(dstChan) << "," << dstLabel << "," << juce::String(ev.value, 6) << "\n";
                }
            }
            out.writeText (csv, false, false, "\n");
            out.flush();
            lastExportPath = file.getFullPathName();
        }
        if (lastExportPath.isNotEmpty())
        {
            ImGui::SameLine();
            ImGui::TextUnformatted(lastExportPath.toRawUTF8());
        }
    }
    ImGui::PopItemWidth();

    // Drain FIFO
    int ready = fifo.getNumReady();
    while (ready > 0)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead(ready, start1, size1, start2, size2);
        auto consume = [&](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& ev = fifoBuffer[(size_t) (start + i)];
                // update stats
                auto& s = stats[(size_t) ev.pinIndex];
                s.last = ev.value;
                s.min = std::min(s.min, ev.value);
                s.max = std::max(s.max, ev.value);
                s.rmsAcc += ev.value * ev.value;
                s.rmsCount += 1;
                if (! uiPaused)
                    uiEvents.push_back(ev);
            }
        };
        if (size1 > 0) consume(start1, size1);
        if (size2 > 0) consume(start2, size2);
        fifo.finishedRead(size1 + size2);
        ready -= (size1 + size2);
    }

    // Bound UI list
    constexpr size_t kMaxUiEvents = 1000;
    if (uiEvents.size() > kMaxUiEvents)
        uiEvents.erase(uiEvents.begin(), uiEvents.begin() + (uiEvents.size() - kMaxUiEvents));

    // Waveform Visualization
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    const auto& freqColors = theme.modules.frequency_graph;
    const auto resolveColor = [](ImU32 value, ImU32 fallback) { return value != 0 ? value : fallback; };
    const float graphHeight = 200.0f;
    const ImVec2 graphSize(itemWidth, graphHeight);
    
    if (ImGui::BeginChild("DebugWaveform", graphSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = resolveColor(freqColors.background, IM_COL32(18, 20, 24, 255));
        drawList->AddRectFilled(p0, p1, bgColor);
        
        // Grid lines
        const ImU32 gridColor = resolveColor(freqColors.grid, IM_COL32(50, 55, 65, 255));
        const float centerY = p0.y + graphSize.y * 0.5f;
        drawList->AddLine(ImVec2(p0.x, centerY), ImVec2(p1.x, centerY), gridColor, 1.0f);
        drawList->AddLine(ImVec2(p0.x, p0.y), ImVec2(p1.x, p0.y), gridColor, 1.0f);
        drawList->AddLine(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y), gridColor, 1.0f);
        
        // Threshold lines
        const ImU32 thresholdColor = IM_COL32(255, 100, 100, 100);
        const float thresholdYPos = centerY - threshold * graphSize.y * 0.4f;
        const float thresholdYNeg = centerY + threshold * graphSize.y * 0.4f;
        drawList->AddLine(ImVec2(p0.x, thresholdYPos), ImVec2(p1.x, thresholdYPos), thresholdColor, 1.0f);
        drawList->AddLine(ImVec2(p0.x, thresholdYNeg), ImVec2(p1.x, thresholdYNeg), thresholdColor, 1.0f);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        // Read waveform data
        std::array<std::array<float, VizData::waveformPoints>, 8> waveforms;
        std::array<std::vector<int>, 8> eventMarkers;
        
        for (int ch = 0; ch < 8; ++ch)
        {
            for (int i = 0; i < VizData::waveformPoints; ++i)
                waveforms[ch][i] = vizData.inputWaveforms[ch][i].load();
            
            // Collect event markers
            int markerCount = juce::jmin(vizData.eventMarkerCounts[ch].load(), VizData::maxEventMarkers);
            for (int i = 0; i < markerCount; ++i)
            {
                int pos = vizData.eventMarkerPositions[ch][i].load();
                if (pos >= 0 && pos < VizData::waveformPoints)
                    eventMarkers[ch].push_back(pos);
            }
        }
        
        // Draw waveforms with different vertical offsets
        const float channelHeight = graphSize.y / 8.0f;
        const float scale = channelHeight * 0.35f;
        
        // Color palette for 8 channels
        const ImU32 channelColors[8] = {
            IM_COL32(100, 200, 255, 255), // Ch 1: Cyan
            IM_COL32(255, 150, 100, 255), // Ch 2: Orange
            IM_COL32(150, 255, 150, 255), // Ch 3: Green
            IM_COL32(255, 200, 100, 255), // Ch 4: Yellow
            IM_COL32(200, 150, 255, 255), // Ch 5: Purple
            IM_COL32(255, 100, 200, 255), // Ch 6: Pink
            IM_COL32(100, 255, 255, 255), // Ch 7: Aqua
            IM_COL32(255, 255, 150, 255)  // Ch 8: Light Yellow
        };
        
        for (int ch = 0; ch < 8; ++ch)
        {
            const float yBase = p0.y + channelHeight * (ch + 0.5f);
            const ImU32 color = channelColors[ch];
            
            // Draw waveform
            for (int i = 1; i < VizData::waveformPoints; ++i)
            {
                float x0 = p0.x + (float)(i - 1) / (float)(VizData::waveformPoints - 1) * graphSize.x;
                float x1 = p0.x + (float)i / (float)(VizData::waveformPoints - 1) * graphSize.x;
                float y0 = juce::jlimit(p0.y, p1.y, yBase - waveforms[ch][i - 1] * scale);
                float y1 = juce::jlimit(p0.y, p1.y, yBase - waveforms[ch][i] * scale);
                drawList->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 1.5f);
            }
            
            // Draw event markers
            const ImU32 markerColor = IM_COL32(255, 255, 0, 255);
            for (int pos : eventMarkers[ch])
            {
                const float x = p0.x + (float)pos / (float)(VizData::waveformPoints - 1) * graphSize.x;
                drawList->AddLine(ImVec2(x, yBase - scale), ImVec2(x, yBase + scale), markerColor, 2.0f);
            }
            
            // Channel label
            ImGui::SetCursorPos(ImVec2(4, channelHeight * ch + 2));
            ImGui::TextColored(ImVec4(
                ((color >> 0) & 0xFF) / 255.0f,
                ((color >> 8) & 0xFF) / 255.0f,
                ((color >> 16) & 0xFF) / 255.0f,
                1.0f), "Ch %d", ch + 1);
        }
        
        drawList->PopClipRect();
        
        // Threshold label
        ImGui::SetCursorPos(ImVec2(itemWidth - 100, 4));
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Threshold: %.4f", threshold);
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##debugWaveformDrag", graphSize);
    }
    ImGui::EndChild();
    
    ImGui::Spacing();

    // Log view
    ImGui::Text("Events (newest first):");
    ImGui::BeginChild("##dbg_log", ImVec2(itemWidth, 160), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto it = uiEvents.rbegin(); it != uiEvents.rend(); ++it)
    {
        // Convert sampleCounter to time in seconds for quick reference
        const double tSec = (currentSampleRate > 0.0 ? (double) it->sampleCounter / currentSampleRate : 0.0);
        ImGui::Text("t=%.3fs pin=%u val=%.4f", tSec, (unsigned) it->pinIndex + 1u, it->value);
    }
    ImGui::EndChild();
}

void DebugModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // One bus with 8 channels, render inputs using the parallel helper for alignment
    for (int ch = 0; ch < 8; ++ch)
    {
        const juce::String label = "In " + juce::String (ch + 1);
        helpers.drawParallelPins (label.toRawUTF8(), ch, nullptr, -1);
    }
}
#endif



