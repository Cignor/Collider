#include "DebugModuleProcessor.h"
#include "../../utils/RtLogger.h"
#include "../graph/ModularSynthProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
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

void DebugModuleProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    totalSamples = 0;
    for (auto& v : lastReported) v = 0.0f;
    droppedEvents.store(0);
    for (auto& s : stats) { s.last = 0.0f; s.min = 1e9f; s.max = -1e9f; s.rmsAcc = 0.0f; s.rmsCount = 0; }
}

void DebugModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin(getTotalNumInputChannels(), 8);

    int eventsThisBlock = 0;

    for (int ch = 0; ch < channels; ++ch)
    {
        if (! pinEnabled[(size_t) ch])
            continue;

        auto in = getBusBuffer(buffer, true, 0); // single bus, multi-channel
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
            }
            else
            {
                droppedEvents.fetch_add(1);
            }
        }
    }

    totalSamples += (juce::uint64) numSamples;
}

#if defined(PRESET_CREATOR_UI)
void DebugModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String&)> & /*isParamModulated*/, const std::function<void()> & /*onModificationEnded*/)
{
    ImGui::PushItemWidth(itemWidth);
    if (ImGui::Checkbox("Pause", &uiPaused)) {}
    ImGui::SameLine(); ImGui::Text("Dropped: %u", droppedEvents.load());
    ImGui::SliderFloat("Threshold", &threshold, 0.0f, 0.05f, "%.4f");
    ImGui::SliderInt("Max events/block", &maxEventsPerBlock, 1, 512);
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
            auto it = modulePinDatabase.find(moduleType);
            if (it != modulePinDatabase.end())
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
                auto it = modulePinDatabase.find(moduleType);
                if (it != modulePinDatabase.end())
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

    // Live per-pin row
    if (ImGui::BeginTable("dbg_stats", 8, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg))
    {
        for (int ch = 0; ch < 8; ++ch)
        {
            ImGui::TableNextColumn();
            const auto& s = stats[(size_t) ch];
            const float rms = s.rmsCount > 0 ? std::sqrt(s.rmsAcc / (float) s.rmsCount) : 0.0f;
            ImGui::Text("%d: %.3f\nmin %.3f\nmax %.3f\nrms %.3f", ch+1, s.last, s.min, s.max, rms);
        }
        ImGui::EndTable();
    }

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
    // One bus with 8 channels, editor pins refer to channel indices 0..7
    helpers.drawAudioInputPin("In 1", 0);
    helpers.drawAudioInputPin("In 2", 1);
    helpers.drawAudioInputPin("In 3", 2);
    helpers.drawAudioInputPin("In 4", 3);
    helpers.drawAudioInputPin("In 5", 4);
    helpers.drawAudioInputPin("In 6", 5);
    helpers.drawAudioInputPin("In 7", 6);
    helpers.drawAudioInputPin("In 8", 7);
}
#endif


