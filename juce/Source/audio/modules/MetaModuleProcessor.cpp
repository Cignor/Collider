#include "MetaModuleProcessor.h"

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
    
    // Prepare inlet/outlet buffers
    updateInletOutletCache();
    
    inletBuffers.clear();
    outletBuffers.clear();
    
    for (int i = 0; i < cachedInletCount; ++i)
    {
        inletBuffers.emplace_back(2, samplesPerBlock);
    }
    
    for (int i = 0; i < cachedOutletCount; ++i)
    {
        outletBuffers.emplace_back(2, samplesPerBlock);
    }
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
    
    // 1. Get all inlet nodes and feed them with our input buffer
    auto inlets = getInletNodes();
    for (size_t i = 0; i < inlets.size() && i < inletBuffers.size(); ++i)
    {
        if (inlets[i])
        {
            // Copy relevant channels from main input to this inlet's buffer
            const int startChannel = (int)i * 2; // Each inlet gets 2 channels
            const int numChannels = juce::jmin(2, buffer.getNumChannels() - startChannel);
            
            if (numChannels > 0 && startChannel < buffer.getNumChannels())
            {
                inletBuffers[i].setSize(numChannels, buffer.getNumSamples(), false, false, true);
                
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    inletBuffers[i].copyFrom(ch, 0, buffer, startChannel + ch, 0, buffer.getNumSamples());
                }
                
                inlets[i]->setIncomingBuffer(&inletBuffers[i]);
            }
        }
    }
    
    // 2. Process the internal graph
    juce::AudioBuffer<float> internalBuffer(buffer.getNumChannels(), buffer.getNumSamples());
    internalBuffer.clear();
    
    internalGraph->processBlock(internalBuffer, midi);
    
    if ((frameCounter % 100) == 0) {
        juce::Logger::writeToLog("  - Copying audio from internal outlets to main output buffer.");
    }
    
    // 3. Collect outputs from outlet nodes
    auto outlets = getOutletNodes();
    buffer.clear(); // Clear before accumulating
    
    for (size_t i = 0; i < outlets.size(); ++i)
    {
        if (outlets[i])
        {
            const auto& outletBuffer = outlets[i]->getOutputBuffer();
            const int startChannel = (int)i * 2; // Each outlet provides 2 channels
            const int numChannels = juce::jmin(outletBuffer.getNumChannels(), buffer.getNumChannels() - startChannel);
            
            if (numChannels > 0 && startChannel < buffer.getNumChannels())
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    buffer.addFrom(startChannel + ch, 0, outletBuffer, ch, 0, 
                                  juce::jmin(buffer.getNumSamples(), outletBuffer.getNumSamples()));
                }
            }
        }
    }
    
    // 4. Update output telemetry
    if (lastOutputValues.size() >= 2)
    {
        lastOutputValues[0]->store(buffer.getMagnitude(0, 0, buffer.getNumSamples()));
        if (buffer.getNumChannels() > 1)
            lastOutputValues[1]->store(buffer.getMagnitude(1, 0, buffer.getNumSamples()));
    }
}

void MetaModuleProcessor::updateInletOutletCache()
{
    cachedInletCount = 0;
    cachedOutletCount = 0;
    
    if (internalGraph)
    {
        auto modules = internalGraph->getModulesInfo();
        for (const auto& [logicalId, typeName] : modules)
        {
            if (typeName.equalsIgnoreCase("inlet"))
                cachedInletCount++;
            else if (typeName.equalsIgnoreCase("outlet"))
                cachedOutletCount++;
        }
    }
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

void MetaModuleProcessor::rebuildBusLayout()
{
    // This is a simplified version - in a full implementation, you would
    // dynamically rebuild the AudioProcessor's bus layout based on inlet/outlet counts
    // For now, we use a fixed stereo bus layout
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
    
    ImGui::Separator();
    
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
    int inChannel = 0;
    for (size_t i = 0; i < inlets.size(); ++i)
    {
        if (inlets[i])
        {
            juce::String label = "In " + juce::String(i + 1);
            helpers.drawAudioInputPin(label.toRawUTF8(), inChannel++);
        }
    }
    
    // Draw output pins for each outlet
    auto outlets = getOutletNodes();
    int outChannel = 0;
    for (size_t i = 0; i < outlets.size(); ++i)
    {
        if (outlets[i])
        {
            juce::String label = "Out " + juce::String(i + 1);
            helpers.drawAudioOutputPin(label.toRawUTF8(), outChannel++);
        }
    }
}

juce::String MetaModuleProcessor::getAudioInputLabel(int channel) const
{
    auto inlets = getInletNodes();
    if (juce::isPositiveAndBelow(channel, (int)inlets.size()) && inlets[channel])
    {
        // Use the inlet's custom label if available
        return juce::String("In ") + juce::String(channel + 1);
    }
    return juce::String("In ") + juce::String(channel + 1);
}

juce::String MetaModuleProcessor::getAudioOutputLabel(int channel) const
{
    auto outlets = getOutletNodes();
    if (juce::isPositiveAndBelow(channel, (int)outlets.size()) && outlets[channel])
    {
        // Use the outlet's custom label if available
        return juce::String("Out ") + juce::String(channel + 1);
    }
    return juce::String("Out ") + juce::String(channel + 1);
}
#endif

