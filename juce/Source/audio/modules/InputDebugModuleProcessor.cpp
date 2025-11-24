#include "InputDebugModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    // No parameters for now; could add per-pin enables later
    return { params.begin(), params.end() };
}

juce::AudioProcessorValueTreeState::ParameterLayout InputDebugModuleProcessor::createParameterLayout()
{
    return makeLayout();
}

InputDebugModuleProcessor::InputDebugModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput ("Tap In",  juce::AudioChannelSet::discreteChannels(8), true)
                        .withOutput("Tap Out", juce::AudioChannelSet::discreteChannels(8), true)),
      apvts(*this, nullptr, "InputDebugParams", createParameterLayout()),
      abstractFifo(4096)
{
    fifoBackingStore.resize(4096);
    for (auto& v : lastValues) v = 0.0f;
    for (auto& v : lastReportedValues) v = 0.0f;
}

void InputDebugModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    totalSamplesProcessed = 0;
    droppedEvents.store(0, std::memory_order_relaxed);

#if defined(PRESET_CREATOR_UI)
    // Initialize visualization buffers (circular buffers for waveform capture)
    const int vizBufferSize = 4096;
    for (int ch = 0; ch < 8; ++ch)
    {
        vizBuffers[ch].setSize(1, vizBufferSize);
        vizBuffers[ch].clear();
        vizWritePositions[ch] = 0;
    }
#endif
}

void InputDebugModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto in  = getBusBuffer(buffer, true,  0);
    auto out = getBusBuffer(buffer, false, 0);
    const int numChannels = juce::jmin(in.getNumChannels(), out.getNumChannels());
    const int numSamples = buffer.getNumSamples();

    // Transparent pass-through
    for (int ch = 0; ch < numChannels; ++ch)
        out.copyFrom(ch, 0, in, ch, 0, numSamples);

    // Log changes with threshold + hysteresis
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float v = in.getMagnitude(ch, 0, numSamples);
        const float last = lastReportedValues[(size_t) ch];
        const float delta = std::abs(v - last);

        if (delta > CHANGE_THRESHOLD || (delta > HYSTERESIS && v != lastValues[(size_t) ch]))
        {
            int start1, size1, start2, size2;
            abstractFifo.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                fifoBackingStore[(size_t) start1] = InputDebugEvent{ totalSamplesProcessed, ch, v };
                abstractFifo.finishedWrite(1);
                lastReportedValues[(size_t) ch] = v;
            }
            else
            {
                droppedEvents.fetch_add(1, std::memory_order_relaxed);
            }
        }
        lastValues[(size_t) ch] = v;
    }

#if defined(PRESET_CREATOR_UI)
    // Capture waveform data for visualization (downsampled)
    for (int ch = 0; ch < numChannels && ch < 8; ++ch)
    {
        auto& vizBuffer = vizBuffers[ch];
        const int vizBufferSize = vizBuffer.getNumSamples();
        if (vizBufferSize > 0)
        {
            // Write incoming samples to circular buffer
            float* writePtr = vizBuffer.getWritePointer(0);
            for (int i = 0; i < numSamples; ++i)
            {
                writePtr[vizWritePositions[ch]] = in.getSample(ch, i);
                vizWritePositions[ch] = (vizWritePositions[ch] + 1) % vizBufferSize;
            }

            // Downsample to visualization points
            const int stride = juce::jmax(1, vizBufferSize / InputDebugModuleProcessor::VizData::waveformPoints);
            for (int i = 0; i < InputDebugModuleProcessor::VizData::waveformPoints; ++i)
            {
                const int readIdx = (vizWritePositions[ch] - InputDebugModuleProcessor::VizData::waveformPoints * stride + i * stride + vizBufferSize) % vizBufferSize;
                const float sample = writePtr[readIdx];
                vizData.waveforms[ch][i].store(sample, std::memory_order_relaxed);
            }

            // Store current value (magnitude)
            const float currentVal = in.getMagnitude(ch, 0, numSamples);
            vizData.currentValues[ch].store(currentVal, std::memory_order_relaxed);
        }
    }
#endif

    totalSamplesProcessed += (juce::uint64) numSamples;
}

#if defined(PRESET_CREATOR_UI)
void InputDebugModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)> & /*isParamModulated*/, const std::function<void()> & /*onModificationEnded*/)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    ImGui::PushItemWidth(itemWidth);

    // === WAVEFORM VISUALIZATION ===
    ImGui::PushID(this);
    
    // Read visualization data (thread-safe) - BEFORE BeginChild
    std::array<std::array<float, InputDebugModuleProcessor::VizData::waveformPoints>, 8> waveforms;
    std::array<float, 8> currentValues;
    for (int ch = 0; ch < 8; ++ch)
    {
        for (int i = 0; i < InputDebugModuleProcessor::VizData::waveformPoints; ++i)
            waveforms[ch][i] = vizData.waveforms[ch][i].load();
        currentValues[ch] = vizData.currentValues[ch].load();
    }

    const float waveHeight = 180.0f;
    const ImVec2 graphSize(itemWidth, waveHeight);
    
    if (ImGui::BeginChild("InputDebugViz", graphSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 p0 = ImGui::GetWindowPos();
        const ImVec2 p1 = ImVec2(p0.x + graphSize.x, p0.y + graphSize.y);
        
        // Background
        const ImU32 bgColor = ThemeManager::getInstance().getCanvasBackground();
        drawList->AddRectFilled(p0, p1, bgColor);
        
        // Clip to graph area
        drawList->PushClipRect(p0, p1, true);
        
        // Draw waveforms for each channel (8 channels, stacked)
        const float channelHeight = graphSize.y / 8.0f;
        const float scaleY = channelHeight * 0.35f;
        const float stepX = graphSize.x / (float)(InputDebugModuleProcessor::VizData::waveformPoints - 1);
        
        // Color palette for channels
        const ImU32 channelColors[8] = {
            ImGui::ColorConvertFloat4ToU32(theme.accent),
            ImGui::ColorConvertFloat4ToU32(theme.modulation.frequency),
            ImGui::ColorConvertFloat4ToU32(theme.modulation.amplitude),
            ImGui::ColorConvertFloat4ToU32(theme.modulation.timbre),
            ImGui::ColorConvertFloat4ToU32(theme.modulation.filter),
            ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.5f, 0.0f, 1.0f)), // Orange
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.5f, 0.0f, 1.0f, 1.0f)), // Purple
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 1.0f, 0.5f, 1.0f))  // Cyan
        };
        
        // Center line color
        const ImU32 centerLineColor = IM_COL32(100, 100, 100, 80);
        
        for (int ch = 0; ch < 8; ++ch)
        {
            const float channelTop = p0.y + ch * channelHeight;
            const float channelMid = channelTop + channelHeight * 0.5f;
            const float channelBottom = channelTop + channelHeight;
            
            // Draw center line for each channel
            drawList->AddLine(ImVec2(p0.x, channelMid), ImVec2(p1.x, channelMid), centerLineColor, 0.5f);
            
            // Draw waveform
            const ImU32 waveformColor = channelColors[ch];
            float prevX = p0.x;
            float prevY = channelMid;
            
            for (int i = 0; i < InputDebugModuleProcessor::VizData::waveformPoints; ++i)
            {
                const float sample = juce::jlimit(-1.0f, 1.0f, waveforms[ch][i]);
                const float x = p0.x + i * stepX;
                const float y = juce::jlimit(channelTop, channelBottom, channelMid - sample * scaleY);
                if (i > 0)
                    drawList->AddLine(ImVec2(prevX, prevY), ImVec2(x, y), waveformColor, 1.0f);
                prevX = x;
                prevY = y;
            }
            
            // Channel label and value
            const float labelX = p0.x + 4.0f;
            const float labelY = channelTop + 2.0f;
            const juce::String label = "Ch" + juce::String(ch + 1) + ": " + juce::String(currentValues[ch], 3);
            drawList->AddText(ImVec2(labelX, labelY), waveformColor, label.toRawUTF8());
        }
        
        drawList->PopClipRect();
        
        // Invisible drag blocker
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::InvisibleButton("##inputDebugVizDrag", graphSize);
    }
    ImGui::EndChild();
    
    ImGui::PopID();
    ImGui::Spacing();

    // === CONTROLS ===
    if (ImGui::Checkbox("Pause", &isPaused)) {}
    ImGui::SameLine(); ImGui::Text("Dropped: %u", droppedEvents.load());

    if (ImGui::Button("Copy CSV"))
    {
        juce::String csv;
        csv << "time_seconds,src_logical_id,src_module,src_channel,src_label,src_value,tap_module,tap_input,tap_output,dst_logical_id,dst_module,dst_channel,dst_label,tap_value,delta\n";

        auto* synth = getParent();
        juce::uint32 selfLid = 0;
        std::map<juce::uint32, juce::String> lidToType;
        if (synth != nullptr)
        {
            for (const auto& p : synth->getModulesInfo())
            {
                lidToType[p.first] = p.second;
                if (synth->getModuleForLogical(p.first) == this) selfLid = p.first;
            }
        }

        // Build upstream/downstream maps for each tap channel
        std::array<std::vector<std::pair<juce::uint32,int>>, 8> upstream;   // for each ch, sources feeding Tap In ch
        std::array<std::vector<std::pair<juce::uint32,int>>, 8> downstream; // for each ch, destinations fed by Tap Out ch
        if (synth != nullptr && selfLid != 0)
        {
            for (const auto& c : synth->getConnectionsInfo())
            {
                if (!c.dstIsOutput && c.dstLogicalId == selfLid)
                    upstream[(size_t) c.dstChan].push_back({ c.srcLogicalId, c.srcChan });
                if (c.srcLogicalId == selfLid)
                    downstream[(size_t) c.srcChan].push_back({ c.dstLogicalId, c.dstChan });
            }
        }

        auto outLabelFor = [&](juce::uint32 lid, int ch) -> juce::String
        {
            if (synth == nullptr || lid == 0) return {};
            if (auto* mp = synth->getModuleForLogical(lid)) return mp->getAudioOutputLabel(ch);
            return {};
        };
        auto inLabelFor = [&](juce::uint32 lid, int ch) -> juce::String
        {
            if (synth == nullptr || lid == 0) return {};
            if (auto* mp = synth->getModuleForLogical(lid)) return mp->getAudioInputLabel(ch);
            return {};
        };

        for (const auto& ev : displayedEvents)
        {
            const double tSec = (currentSampleRate > 0.0 ? (double) ev.sampleCounter / currentSampleRate : 0.0);
            const int ch = ev.pinIndex;
            const juce::String tapInLabel = getAudioInputLabel(ch);
            const juce::String tapOutLabel = getAudioOutputLabel(ch);

            if (synth != nullptr && selfLid != 0)
            {
                if (!upstream[(size_t) ch].empty())
                {
                    for (const auto& src : upstream[(size_t) ch])
                    {
                        const juce::String srcType = lidToType.count(src.first) ? lidToType[src.first] : juce::String("<unknown>");
                        const juce::String srcLabel = outLabelFor(src.first, src.second);
                        float srcVal = 0.0f; if (auto* srcMp = synth->getModuleForLogical(src.first)) srcVal = srcMp->getOutputChannelValue(src.second);
                        const float tapVal = ev.value;
                        const float delta = tapVal - srcVal;
                        if (!downstream[(size_t) ch].empty())
                        {
                            for (const auto& dst : downstream[(size_t) ch])
                            {
                                const juce::String dstType = lidToType.count(dst.first) ? lidToType[dst.first] : juce::String("<unknown>");
                                const juce::String dstLabel = inLabelFor(dst.first, dst.second);
                                csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcType << "," << juce::String(src.second) << "," << srcLabel << "," << juce::String(srcVal, 6)
                                    << ",Input Debug," << tapInLabel << "," << tapOutLabel << "," << juce::String((int) dst.first) << "," << dstType << "," << juce::String(dst.second) << "," << dstLabel << "," << juce::String(tapVal, 6) << "," << juce::String(delta, 6) << "\n";
                            }
                        }
                        else
                        {
                            csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcType << "," << juce::String(src.second) << "," << srcLabel << "," << juce::String(srcVal, 6)
                                << ",Input Debug," << tapInLabel << "," << tapOutLabel << ",,,,," << juce::String(tapVal, 6) << "," << juce::String(tapVal - srcVal, 6) << "\n";
                        }
                    }
                }
                else
                {
                    // No upstream
                    if (!downstream[(size_t) ch].empty())
                    {
                        for (const auto& dst : downstream[(size_t) ch])
                        {
                            const juce::String dstType = lidToType.count(dst.first) ? lidToType[dst.first] : juce::String("<unknown>");
                            const juce::String dstLabel = inLabelFor(dst.first, dst.second);
                            csv << juce::String(tSec, 6) << ",,,,,,Input Debug," << tapInLabel << "," << tapOutLabel << "," << juce::String((int) dst.first) << "," << dstType << "," << juce::String(dst.second) << "," << dstLabel << "," << juce::String(ev.value, 6) << "," << juce::String() << "\n";
                        }
                    }
                    else
                    {
                        csv << juce::String(tSec, 6) << ",,,,,,Input Debug," << tapInLabel << "," << tapOutLabel << ",,,," << juce::String(ev.value, 6) << "," << juce::String() << "\n";
                    }
                }
            }
        }

        std::string utf8 = csv.toStdString();
        ImGui::SetClipboardText(utf8.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Export CSV"))
    {
        juce::File dir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ColliderDebugLogs");
        if (!dir.exists()) (void) dir.createDirectory();
        juce::File file = dir.getNonexistentChildFile("input_debug_log", ".csv");
        juce::FileOutputStream out(file);
        if (out.openedOk())
        {
            juce::String csv;
            csv << "time_seconds,src_logical_id,src_module,src_channel,src_label,src_value,tap_module,tap_input,tap_output,dst_logical_id,dst_module,dst_channel,dst_label,tap_value,delta\n";

            auto* synth = getParent();
            juce::uint32 selfLid = 0;
            std::map<juce::uint32, juce::String> lidToType;
            if (synth != nullptr)
            {
                for (const auto& p : synth->getModulesInfo())
                {
                    lidToType[p.first] = p.second;
                    if (synth->getModuleForLogical(p.first) == this) selfLid = p.first;
                }
            }

            std::array<std::vector<std::pair<juce::uint32,int>>, 8> upstream;
            std::array<std::vector<std::pair<juce::uint32,int>>, 8> downstream;
            if (synth != nullptr && selfLid != 0)
            {
                for (const auto& c : synth->getConnectionsInfo())
                {
                    if (!c.dstIsOutput && c.dstLogicalId == selfLid)
                        upstream[(size_t) c.dstChan].push_back({ c.srcLogicalId, c.srcChan });
                    if (c.srcLogicalId == selfLid)
                        downstream[(size_t) c.srcChan].push_back({ c.dstLogicalId, c.dstChan });
                }
            }

            auto outLabelFor = [&](juce::uint32 lid, int ch) -> juce::String
            {
                if (synth == nullptr || lid == 0) return {};
                if (auto* mp = synth->getModuleForLogical(lid)) return mp->getAudioOutputLabel(ch);
                return {};
            };
            auto inLabelFor = [&](juce::uint32 lid, int ch) -> juce::String
            {
                if (synth == nullptr || lid == 0) return {};
                if (auto* mp = synth->getModuleForLogical(lid)) return mp->getAudioInputLabel(ch);
                return {};
            };

            for (const auto& ev : displayedEvents)
            {
                const double tSec = (currentSampleRate > 0.0 ? (double) ev.sampleCounter / currentSampleRate : 0.0);
                const int ch = ev.pinIndex;
                const juce::String tapInLabel = getAudioInputLabel(ch);
                const juce::String tapOutLabel = getAudioOutputLabel(ch);

                if (synth != nullptr && selfLid != 0)
                {
                    if (!upstream[(size_t) ch].empty())
                    {
                        for (const auto& src : upstream[(size_t) ch])
                        {
                            const juce::String srcType = lidToType.count(src.first) ? lidToType[src.first] : juce::String("<unknown>");
                            const juce::String srcLabel = outLabelFor(src.first, src.second);
                            float srcVal = 0.0f; if (auto* srcMp = synth->getModuleForLogical(src.first)) srcVal = srcMp->getOutputChannelValue(src.second);
                            const float tapVal = ev.value;
                            const float delta = tapVal - srcVal;
                            if (!downstream[(size_t) ch].empty())
                            {
                                for (const auto& dst : downstream[(size_t) ch])
                                {
                                    const juce::String dstType = lidToType.count(dst.first) ? lidToType[dst.first] : juce::String("<unknown>");
                                    const juce::String dstLabel = inLabelFor(dst.first, dst.second);
                                    csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcType << "," << juce::String(src.second) << "," << srcLabel << "," << juce::String(srcVal, 6)
                                        << ",Input Debug," << tapInLabel << "," << tapOutLabel << "," << juce::String((int) dst.first) << "," << dstType << "," << juce::String(dst.second) << "," << dstLabel << "," << juce::String(tapVal, 6) << "," << juce::String(delta, 6) << "\n";
                                }
                            }
                            else
                            {
                                csv << juce::String(tSec, 6) << "," << juce::String((int) src.first) << "," << srcType << "," << juce::String(src.second) << "," << srcLabel << "," << juce::String(srcVal, 6)
                                    << ",Input Debug," << tapInLabel << "," << tapOutLabel << ",,,,," << juce::String(tapVal, 6) << "," << juce::String(tapVal - srcVal, 6) << "\n";
                            }
                        }
                    }
                    else
                    {
                        if (!downstream[(size_t) ch].empty())
                        {
                            for (const auto& dst : downstream[(size_t) ch])
                            {
                                const juce::String dstType = lidToType.count(dst.first) ? lidToType[dst.first] : juce::String("<unknown>");
                                const juce::String dstLabel = inLabelFor(dst.first, dst.second);
                                csv << juce::String(tSec, 6) << ",,,,,,Input Debug," << tapInLabel << "," << tapOutLabel << "," << juce::String((int) dst.first) << "," << dstType << "," << juce::String(dst.second) << "," << dstLabel << "," << juce::String(ev.value, 6) << "," << juce::String() << "\n";
                            }
                        }
                        else
                        {
                            csv << juce::String(tSec, 6) << ",,,,,,Input Debug," << tapInLabel << "," << tapOutLabel << ",,,," << juce::String(ev.value, 6) << "," << juce::String() << "\n";
                        }
                    }
                }
            }

            out.writeText(csv, false, false, "\n");
            out.flush();
        }
    }
    ImGui::PopItemWidth();

    // Drain FIFO into displayedEvents
    int available = abstractFifo.getNumReady();
    while (available > 0)
    {
        int start1, size1, start2, size2;
        abstractFifo.prepareToRead(available, start1, size1, start2, size2);
        auto consume = [&](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& ev = fifoBackingStore[(size_t) (start + i)];
                if (!isPaused)
                    displayedEvents.push_back(ev);
            }
        };
        if (size1 > 0) consume(start1, size1);
        if (size2 > 0) consume(start2, size2);
        abstractFifo.finishedRead(size1 + size2);
        available -= (size1 + size2);
    }

    if (displayedEvents.size() > MAX_DISPLAYED_EVENTS)
        displayedEvents.erase(displayedEvents.begin(), displayedEvents.begin() + (displayedEvents.size() - MAX_DISPLAYED_EVENTS));
}

void InputDebugModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // 8-channel pass-through
    for (int ch = 0; ch < 8; ++ch)
    {
        helpers.drawAudioInputPin((juce::String("Tap In ") + juce::String(ch + 1)).toRawUTF8(), ch);
    }
    for (int ch = 0; ch < 8; ++ch)
    {
        helpers.drawAudioOutputPin((juce::String("Tap Out ") + juce::String(ch + 1)).toRawUTF8(), ch);
    }
}
#endif


