#include "InputDebugModuleProcessor.h"
#include "../graph/ModularSynthProcessor.h"

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
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    totalSamplesProcessed = 0;
    droppedEvents.store(0, std::memory_order_relaxed);
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

    totalSamplesProcessed += (juce::uint64) numSamples;
}

#if defined(PRESET_CREATOR_UI)
void InputDebugModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)> & /*isParamModulated*/, const std::function<void()> & /*onModificationEnded*/)
{
    ImGui::PushItemWidth(itemWidth);
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


