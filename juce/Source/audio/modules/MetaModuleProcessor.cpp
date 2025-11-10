#include "MetaModuleProcessor.h"
#include <algorithm>

MetaModuleProcessor::MetaModuleProcessor()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MetaModuleParams", createParameterLayout()),
      metaModuleLabel("Meta Module")
{
    // Create the internal graph
    internalGraph = std::make_unique<ModularSynthProcessor>();
    
    // Initialize output value tracking (default 2 channels)
    lastOutputValues.clear();
    for (int i = 0; i < 2; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

juce::AudioProcessorValueTreeState::ParameterLayout MetaModuleProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    
    // Initially empty - parameters will be added dynamically when exposing internal parameters
    // For now, just have a bypass parameter
    layout.add(std::make_unique<juce::AudioParameterBool>(
        "bypass",
        "Bypass",
        false
    ));
    
    return layout;
}

void MetaModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    if (internalGraph)
    {
        internalGraph->prepareToPlay(sampleRate, samplesPerBlock);
    }
    
    updateInletOutletCache();
    rebuildBusLayout();
    resizeIOBuffers(samplesPerBlock);
}

void MetaModuleProcessor::releaseResources()
{
    if (internalGraph)
    {
        internalGraph->releaseResources();
    }
}

void MetaModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (!internalGraph)
    {
        buffer.clear();
        return;
    }

    if (layoutDirty.exchange(false))
    {
        updateInletOutletCache();
        rebuildBusLayout();
        resizeIOBuffers(buffer.getNumSamples());
    }
    
    // Logging example (throttled to avoid spam)
    static int frameCounter = 0;
    if ((frameCounter++ % 100) == 0) {
        juce::Logger::writeToLog("[META_PROC] Processing block for Meta Module " + juce::String((int)getLogicalId()));
        juce::Logger::writeToLog("  - Copying " + juce::String(buffer.getNumChannels()) + " channels to internal inlets.");
    }
    
    // Check bypass
    if (auto* bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")))
    {
        if (*bypassParam)
        {
            // Bypass: just pass through input to output
            return;
        }
    }
    
    auto inlets = getInletNodes();
    std::sort(inlets.begin(), inlets.end(), [](auto* a, auto* b)
    {
        const int aIdx = a->getPinIndex();
        const int bIdx = b->getPinIndex();
        if (aIdx != bIdx)
            return aIdx < bIdx;
        return a->getLogicalId() < b->getLogicalId();
    });
    
    int bufferChannelOffset = 0;
    for (size_t i = 0; i < inlets.size() && i < inletBuffers.size(); ++i)
    {
        auto* inlet = inlets[i];
        const int inletChannels = (i < inletChannelLayouts.size())
            ? inletChannelLayouts[i].channelCount
            : 1;
        const int available = juce::jmin(inletChannels, buffer.getNumChannels() - bufferChannelOffset);
        
        if (available > 0)
        {
            inletBuffers[i].setSize(inletChannels, buffer.getNumSamples(), false, false, true);
            for (int ch = 0; ch < available; ++ch)
            {
                inletBuffers[i].copyFrom(ch, 0, buffer, bufferChannelOffset + ch, 0, buffer.getNumSamples());
            }
            inlet->setIncomingBuffer(&inletBuffers[i]);
        }
        
        bufferChannelOffset += inletChannels;
    }
    
    // 2. Process the internal graph
    juce::AudioBuffer<float> internalBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    internalBuffer.clear();
    
    internalGraph->processBlock(internalBuffer, midi);
    
    if ((frameCounter % 100) == 0) {
        juce::Logger::writeToLog("  - Copying audio from internal outlets to main output buffer.");
    }
    
    auto outlets = getOutletNodes();
    std::sort(outlets.begin(), outlets.end(), [](auto* a, auto* b)
    {
        const int aIdx = a->getPinIndex();
        const int bIdx = b->getPinIndex();
        if (aIdx != bIdx)
            return aIdx < bIdx;
        return a->getLogicalId() < b->getLogicalId();
    });
    
    buffer.clear();
    bufferChannelOffset = 0;
    
    for (size_t i = 0; i < outlets.size() && i < outletBuffers.size(); ++i)
    {
        auto* outlet = outlets[i];
        const auto& outletBuffer = outlet->getOutputBuffer();
        const int outletChannels = (i < outletChannelLayouts.size())
            ? outletChannelLayouts[i].channelCount
            : outletBuffer.getNumChannels();
        const int available = juce::jmin(outletChannels, buffer.getNumChannels() - bufferChannelOffset);
        if (available > 0)
        {
            for (int ch = 0; ch < available; ++ch)
            {
                buffer.addFrom(bufferChannelOffset + ch, 0,
                               outletBuffer, ch, 0,
                               juce::jmin(buffer.getNumSamples(), outletBuffer.getNumSamples()));
            }
        }
        bufferChannelOffset += outletChannels;
    }
    
    // 4. Update output telemetry
    const int telemetryChannels = juce::jmin((int)lastOutputValues.size(), buffer.getNumChannels());
    for (int ch = 0; ch < telemetryChannels; ++ch)
        lastOutputValues[ch]->store(buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
}

void MetaModuleProcessor::updateInletOutletCache()
{
    cachedInletCount = 0;
    cachedOutletCount = 0;
    inletChannelLayouts.clear();
    outletChannelLayouts.clear();
    
    if (!internalGraph)
        return;
    
    auto inlets = getInletNodes();
    std::sort(inlets.begin(), inlets.end(), [](auto* a, auto* b)
    {
        const int aIdx = a->getPinIndex();
        const int bIdx = b->getPinIndex();
        if (aIdx != bIdx)
            return aIdx < bIdx;
        return a->getLogicalId() < b->getLogicalId();
    });
    
    for (auto* inlet : inlets)
    {
        int channelCount = 0;
        if (inlet != nullptr)
        {
            if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                    inlet->getAPVTS().getParameter(InletModuleProcessor::paramIdChannelCount)))
            {
                channelCount = param->get();
            }
        }
        channelCount = juce::jmax(1, channelCount);
        inletChannelLayouts.push_back({ channelCount });
    }
    cachedInletCount = (int) inletChannelLayouts.size();
    
    auto outlets = getOutletNodes();
    std::sort(outlets.begin(), outlets.end(), [](auto* a, auto* b)
    {
        const int aIdx = a->getPinIndex();
        const int bIdx = b->getPinIndex();
        if (aIdx != bIdx)
            return aIdx < bIdx;
        return a->getLogicalId() < b->getLogicalId();
    });
    
    for (auto* outlet : outlets)
    {
        int channelCount = 0;
        if (outlet != nullptr)
        {
            if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                    outlet->getAPVTS().getParameter(OutletModuleProcessor::paramIdChannelCount)))
            {
                channelCount = param->get();
            }
        }
        channelCount = juce::jmax(1, channelCount);
        outletChannelLayouts.push_back({ channelCount });
    }
    cachedOutletCount = (int) outletChannelLayouts.size();
}

std::vector<InletModuleProcessor*> MetaModuleProcessor::getInletNodes() const
{
    std::vector<InletModuleProcessor*> inlets;
    
    if (internalGraph)
    {
        auto modules = internalGraph->getModulesInfo();
        for (const auto& [logicalId, typeName] : modules)
        {
            if (typeName.equalsIgnoreCase("inlet"))
            {
                if (auto* module = internalGraph->getModuleForLogical(logicalId))
                {
                    if (auto* inlet = dynamic_cast<InletModuleProcessor*>(module))
                    {
                        inlets.push_back(inlet);
                    }
                }
            }
        }
    }
    
    return inlets;
}

std::vector<OutletModuleProcessor*> MetaModuleProcessor::getOutletNodes() const
{
    std::vector<OutletModuleProcessor*> outlets;
    
    if (internalGraph)
    {
        auto modules = internalGraph->getModulesInfo();
        for (const auto& [logicalId, typeName] : modules)
        {
            if (typeName.equalsIgnoreCase("outlet"))
            {
                if (auto* module = internalGraph->getModuleForLogical(logicalId))
                {
                    if (auto* outlet = dynamic_cast<OutletModuleProcessor*>(module))
                    {
                        outlets.push_back(outlet);
                    }
                }
            }
        }
    }
    
    return outlets;
}

juce::ValueTree MetaModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("MetaModuleState");
    vt.setProperty("label", metaModuleLabel, nullptr);
    
    // Save the complete state of the internal graph
    if (internalGraph)
    {
        juce::MemoryBlock graphState;
        internalGraph->getStateInformation(graphState);
        
        // Convert to base64 for safe storage in ValueTree
        juce::MemoryOutputStream mos;
        juce::Base64::convertToBase64(mos, graphState.getData(), graphState.getSize());
        vt.setProperty("internalGraphState", mos.toString(), nullptr);
    }
    
    return vt;
}

void MetaModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("MetaModuleState"))
    {
        metaModuleLabel = vt.getProperty("label", "Meta Module").toString();
        
        // Restore the internal graph state
        juce::String base64State = vt.getProperty("internalGraphState", "").toString();
        if (base64State.isNotEmpty() && internalGraph)
        {
            juce::MemoryOutputStream mos;
            if (juce::Base64::convertFromBase64(mos, base64State))
            {
                const auto* data = static_cast<const void*>(mos.getData());
                const int size = (int)mos.getDataSize();
                
                internalGraph->setStateInformation(data, size);
                
                // Update our cached info
                updateInletOutletCache();
                
                // Rebuild bus layout if needed
                rebuildBusLayout();
            }
        }
    }
}

void MetaModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto stateTree = getExtraStateTree(); stateTree.isValid())
    {
        if (auto xml = stateTree.createXml())
        {
            juce::MemoryOutputStream mos(destData, false);
            xml->writeTo(mos);
        }
    }
}

void MetaModuleProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    juce::String xmlString = juce::String::fromUTF8(static_cast<const char*>(data),
                                                    (size_t)sizeInBytes);

    if (xmlString.isEmpty())
        return;

    if (auto xml = juce::XmlDocument::parse(xmlString))
    {
        auto vt = juce::ValueTree::fromXml(*xml);
        if (vt.isValid())
            setExtraStateTree(vt);
    }
}

void MetaModuleProcessor::rebuildBusLayout()
{
    int totalInputChannels = 0;
    for (const auto& layout : inletChannelLayouts)
        totalInputChannels += layout.channelCount;
    
    int totalOutputChannels = 0;
    for (const auto& layout : outletChannelLayouts)
        totalOutputChannels += layout.channelCount;
    
    const double sr = juce::jmax(1.0, getSampleRate());
    const int blockSize = juce::jmax(1, getBlockSize());
    setPlayConfigDetails(totalInputChannels, totalOutputChannels, sr, blockSize);
    
    lastOutputValues.clear();
    for (int i = 0; i < totalOutputChannels; ++i)
        lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

void MetaModuleProcessor::requestLayoutRebuild()
{
    layoutDirty.store(true, std::memory_order_release);
}

void MetaModuleProcessor::refreshCachedLayout()
{
    updateInletOutletCache();
    requestLayoutRebuild();
}

void MetaModuleProcessor::resizeIOBuffers(int samplesPerBlock)
{
    const int effectiveBlockSize = juce::jmax(1, samplesPerBlock);

    inletBuffers.clear();
    inletBuffers.reserve(inletChannelLayouts.size());
    for (const auto& layout : inletChannelLayouts)
    {
        juce::AudioBuffer<float> temp;
        temp.setSize(juce::jmax(1, layout.channelCount), effectiveBlockSize, false, false, true);
        inletBuffers.push_back(std::move(temp));
    }

    outletBuffers.clear();
    outletBuffers.reserve(outletChannelLayouts.size());
    for (const auto& layout : outletChannelLayouts)
    {
        juce::AudioBuffer<float> temp;
        temp.setSize(juce::jmax(1, layout.channelCount), effectiveBlockSize, false, false, true);
        outletBuffers.push_back(std::move(temp));
    }
}

#if defined(PRESET_CREATOR_UI)
void MetaModuleProcessor::drawParametersInNode(float itemWidth,
                                               const std::function<bool(const juce::String& paramId)>&,
                                               const std::function<void()>& onModificationEnded)
{
    // FIX #1: Constrain widget widths to prevent infinite scaling
    ImGui::PushItemWidth(itemWidth);
    
    // Label editor
    char labelBuf[64];
    strncpy(labelBuf, metaModuleLabel.toRawUTF8(), sizeof(labelBuf) - 1);
    labelBuf[sizeof(labelBuf) - 1] = '\0';
    
    if (ImGui::InputText("Label", labelBuf, sizeof(labelBuf)))
    {
        metaModuleLabel = juce::String(labelBuf);
        if (ImGui::IsItemDeactivatedAfterEdit())
            onModificationEnded();
    }
    
    // Bypass
    bool bypass = false;
    if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")))
        bypass = *p;
    
    if (ImGui::Checkbox("Bypass", &bypass))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")))
            *p = bypass;
        onModificationEnded();
    }
    
    
    // Get stats from the internal graph
    int numModules = 0, numInlets = 0, numOutlets = 0;
    if (internalGraph)
    {
        for (const auto& modInfo : internalGraph->getModulesInfo())
        {
            if (modInfo.second.equalsIgnoreCase("inlet"))
                numInlets++;
            else if (modInfo.second.equalsIgnoreCase("outlet"))
                numOutlets++;
            else
                numModules++;
        }
    }
    
    // Info display
    ImGui::Text("Internal Graph:");
    ImGui::Text("  Modules: %d", numModules);
    ImGui::Text("  Inlets: %d", numInlets);
    ImGui::Text("  Outlets: %d", numOutlets);
    
    // FIX #2: Set atomic flag when button is clicked
    if (ImGui::Button("Edit Internal Patch", ImVec2(itemWidth, 0)))
    {
        juce::Logger::writeToLog("[MetaModule] Edit button clicked for L-ID " + juce::String((int)getLogicalId()));
        editRequested = true;
    }
    
    ImGui::PopItemWidth();
}

void MetaModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Draw input pins for each inlet
    auto inlets = getInletNodes();
    std::sort(inlets.begin(), inlets.end(), [](auto* a, auto* b)
    {
        if (a->getPinIndex() != b->getPinIndex())
            return a->getPinIndex() < b->getPinIndex();
        return a->getLogicalId() < b->getLogicalId();
    });
    int inChannel = 0;
    for (auto* inlet : inlets)
    {
        if (inlet == nullptr)
            continue;

        const juce::String baseLabel = inlet->getCustomLabel().isNotEmpty()
            ? inlet->getCustomLabel()
            : juce::String("In");

        int channelCount = 1;
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                inlet->getAPVTS().getParameter(InletModuleProcessor::paramIdChannelCount)))
        {
            channelCount = juce::jmax(1, param->get());
        }

        for (int c = 0; c < channelCount; ++c)
        {
            juce::String label = channelCount > 1
                ? baseLabel + " " + juce::String(c + 1)
                : baseLabel;
            helpers.drawAudioInputPin(label.toRawUTF8(), inChannel++);
        }
    }
    
    // Draw output pins for each outlet
    auto outlets = getOutletNodes();
    std::sort(outlets.begin(), outlets.end(), [](auto* a, auto* b)
    {
        if (a->getPinIndex() != b->getPinIndex())
            return a->getPinIndex() < b->getPinIndex();
        return a->getLogicalId() < b->getLogicalId();
    });
    int outChannel = 0;
    for (auto* outlet : outlets)
    {
        if (outlet == nullptr)
            continue;

        const juce::String baseLabel = outlet->getCustomLabel().isNotEmpty()
            ? outlet->getCustomLabel()
            : juce::String("Out");

        int channelCount = 1;
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                outlet->getAPVTS().getParameter(OutletModuleProcessor::paramIdChannelCount)))
        {
            channelCount = juce::jmax(1, param->get());
        }

        for (int c = 0; c < channelCount; ++c)
        {
            juce::String label = channelCount > 1
                ? baseLabel + " " + juce::String(c + 1)
                : baseLabel;
            helpers.drawAudioOutputPin(label.toRawUTF8(), outChannel++);
        }
    }
}

juce::String MetaModuleProcessor::getAudioInputLabel(int channel) const
{
    auto inlets = getInletNodes();
    std::sort(inlets.begin(), inlets.end(), [](auto* a, auto* b)
    {
        if (a->getPinIndex() != b->getPinIndex())
            return a->getPinIndex() < b->getPinIndex();
        return a->getLogicalId() < b->getLogicalId();
    });

    int runningChannel = 0;
    for (auto* inlet : inlets)
    {
        if (inlet == nullptr)
            continue;

        const juce::String baseLabel = inlet->getCustomLabel().isNotEmpty()
            ? inlet->getCustomLabel()
            : juce::String("In");

        int channelCount = 1;
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                inlet->getAPVTS().getParameter(InletModuleProcessor::paramIdChannelCount)))
        {
            channelCount = juce::jmax(1, param->get());
        }

        if (channel >= runningChannel && channel < runningChannel + channelCount)
        {
            if (channelCount > 1)
                return baseLabel + " " + juce::String(channel - runningChannel + 1);
            return baseLabel;
        }

        runningChannel += channelCount;
    }

    return juce::String("In ") + juce::String(channel + 1);
}

juce::String MetaModuleProcessor::getAudioOutputLabel(int channel) const
{
    auto outlets = getOutletNodes();
    std::sort(outlets.begin(), outlets.end(), [](auto* a, auto* b)
    {
        if (a->getPinIndex() != b->getPinIndex())
            return a->getPinIndex() < b->getPinIndex();
        return a->getLogicalId() < b->getLogicalId();
    });

    int runningChannel = 0;
    for (auto* outlet : outlets)
    {
        if (outlet == nullptr)
            continue;

        const juce::String baseLabel = outlet->getCustomLabel().isNotEmpty()
            ? outlet->getCustomLabel()
            : juce::String("Out");

        int channelCount = 1;
        if (auto* param = dynamic_cast<juce::AudioParameterInt*>(
                outlet->getAPVTS().getParameter(OutletModuleProcessor::paramIdChannelCount)))
        {
            channelCount = juce::jmax(1, param->get());
        }

        if (channel >= runningChannel && channel < runningChannel + channelCount)
        {
            if (channelCount > 1)
                return baseLabel + " " + juce::String(channel - runningChannel + 1);
            return baseLabel;
        }

        runningChannel += channelCount;
    }

    return juce::String("Out ") + juce::String(channel + 1);
}
#endif

