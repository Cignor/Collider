#include "OSCCVModuleProcessor.h"
#include "../OscDeviceManager.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

//==============================================================================
OSCCVModuleProcessor::OSCCVModuleProcessor()
    : ModuleProcessor(juce::AudioProcessor::BusesProperties()
        .withOutput("Main", juce::AudioChannelSet::discreteChannels(16), true)),  // Dynamic outputs (up to 16)
      apvts(*this, nullptr, "OSCCVParams", createParameterLayout())
{
    // Get parameter pointers
    sourceFilterParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("osc_source_filter"));
    
    // Note: Address mappings will be loaded in prepareToPlay() after APVTS state is restored from patch
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout OSCCVModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Source filter: "All Sources" or specific device
    juce::StringArray sourceChoices;
    sourceChoices.add("All Sources");
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "osc_source_filter", "OSC Source", sourceChoices, 0));
    
    // Note: Address pattern is stored as a member variable, not in APVTS
    // (AudioParameterString doesn't exist in JUCE)
    
    return { params.begin(), params.end() };
}

//==============================================================================
void OSCCVModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    // Load saved address mappings from APVTS state (state is guaranteed to be loaded by this point)
    loadAddressMappingsFromState();
}

void OSCCVModuleProcessor::releaseResources()
{
    // Cleanup if needed
}

//==============================================================================
void OSCCVModuleProcessor::handleOscSignal(const std::vector<OscDeviceManager::OscMessageWithSource>& oscMessages)
{
    if (oscMessages.empty())
        return;
    
    int sourceFilter = sourceFilterParam ? sourceFilterParam->getIndex() : 0;
    juce::uint64 now = juce::Time::getMillisecondCounter();
    
    // Update address monitoring and values
    {
        const juce::ScopedLock lock(addressMonitorLock);
        for (const auto& msg : oscMessages)
        {
            const auto& address = msg.message.getAddressPattern().toString();
            lastSeenAddresses[address] = now;
            
            // Source filtering: 0 = "All Sources", 1+ = specific device
            if (sourceFilter != 0 && msg.deviceIndex != (sourceFilter - 1))
                continue;
            
            // Extract value from OSC message
            float value = 0.0f;
            if (msg.message.size() > 0)
            {
                if (msg.message[0].isFloat32())
                    value = msg.message[0].getFloat32();
                else if (msg.message[0].isInt32())
                    value = (float)msg.message[0].getInt32();
                else if (msg.message[0].isString())
                    value = msg.message[0].getString().getFloatValue();
                
                // Clamp to 0-1 range
                value = juce::jlimit(0.0f, 1.0f, value);
            }
            
            // Store value for this address (thread-safe)
            {
                const juce::ScopedLock valueLockGuard(valueLock);
                addressValues[address] = value;
            }
        }
    }
    
    // Legacy pattern-based processing removed - now using dynamic address-based mapping system
    
    // Clean up old addresses (older than 2 seconds)
    {
        const juce::ScopedLock lock(addressMonitorLock);
        for (auto it = lastSeenAddresses.begin(); it != lastSeenAddresses.end();)
        {
            if (now - it->second > 2000)
                it = lastSeenAddresses.erase(it);
            else
                ++it;
        }
    }
}

std::vector<juce::String> OSCCVModuleProcessor::getRecentAddresses() const
{
    const juce::ScopedLock lock(addressMonitorLock);
    std::vector<juce::String> result;
    result.reserve(lastSeenAddresses.size());
    
    juce::uint64 now = juce::Time::getMillisecondCounter();
    for (const auto& [address, timestamp] : lastSeenAddresses)
    {
        if (now - timestamp < 2000) // Only addresses seen in last 2 seconds
        {
            result.push_back(address);
        }
    }
    
    // Sort by most recent first
    std::sort(result.begin(), result.end(), [this, now](const juce::String& a, const juce::String& b) {
        auto itA = lastSeenAddresses.find(a);
        auto itB = lastSeenAddresses.find(b);
        if (itA == lastSeenAddresses.end()) return false;
        if (itB == lastSeenAddresses.end()) return true;
        return itA->second > itB->second; // Most recent first
    });
    
    return result;
}

std::vector<DynamicPinInfo> OSCCVModuleProcessor::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    const juce::ScopedLock lock(mappingLock);
    for (const auto& mapping : addressMappings)
    {
        // Generate a short label from the address
        juce::String label = mapping.oscAddress;
        // Use last part of path as label (e.g., "/data/motion/gyroscope/x" -> "gyroscope/x")
        int lastSlash = label.lastIndexOfChar('/');
        if (lastSlash >= 0 && lastSlash < label.length() - 1)
        {
            label = label.substring(lastSlash + 1);
        }
        // Limit label length
        if (label.length() > 20)
            label = label.substring(0, 17) + "...";
        
        pins.push_back(DynamicPinInfo(label, mapping.outputChannel, mapping.pinType));
    }
    
    return pins;
}

void OSCCVModuleProcessor::addAddressMapping(const juce::String& address)
{
    if (address.isEmpty())
        return;
    
    const juce::ScopedLock lock(mappingLock);
    
    // Check if already mapped
    for (const auto& mapping : addressMappings)
    {
        if (mapping.oscAddress == address)
            return; // Already mapped
    }
    
    // Determine pin type based on address (default to CV)
    PinDataType pinType = PinDataType::CV;
    if (address.containsIgnoreCase("gate") || address.containsIgnoreCase("trigger"))
        pinType = PinDataType::Gate;
    
    // Add new mapping
    AddressMapping mapping;
    mapping.oscAddress = address;
    mapping.outputChannel = (int)addressMappings.size();
    mapping.pinType = pinType;
    mapping.lastValue = 0.0f;
    mapping.lastUpdateTime = juce::Time::getMillisecondCounter();
    
    addressMappings.push_back(mapping);
    
    // Save to APVTS state for persistence
    saveAddressMappingsToState();
    
    // Note: Graph rebuild will happen automatically when nodes are redrawn
}

void OSCCVModuleProcessor::removeAddressMapping(int outputChannel)
{
    const juce::ScopedLock lock(mappingLock);
    
    if (outputChannel >= 0 && outputChannel < (int)addressMappings.size())
    {
        addressMappings.erase(addressMappings.begin() + outputChannel);
        
        // Reassign channel indices
        for (int i = 0; i < (int)addressMappings.size(); ++i)
        {
            addressMappings[i].outputChannel = i;
        }
        
        // Save to APVTS state for persistence
        saveAddressMappingsToState();
        
        // Note: Graph rebuild will happen automatically when nodes are redrawn
    }
}

bool OSCCVModuleProcessor::isAddressMapped(const juce::String& address) const
{
    const juce::ScopedLock lock(mappingLock);
    
    for (const auto& mapping : addressMappings)
    {
        if (mapping.oscAddress == address)
            return true;
    }
    
    return false;
}

float OSCCVModuleProcessor::getOutputValueForAddress(const juce::String& address) const
{
    const juce::ScopedLock lock(valueLock);
    
    auto it = addressValues.find(address);
    if (it != addressValues.end())
        return it->second;
    
    return 0.0f;
}

//==============================================================================
void OSCCVModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    
    const int numSamples = buffer.getNumSamples();
    
    // Get current mappings (thread-safe copy)
    std::vector<AddressMapping> currentMappings;
    {
        const juce::ScopedLock lock(mappingLock);
        currentMappings = addressMappings;
    }
    
    // Get current address values (thread-safe copy)
    std::map<juce::String, float> currentValues;
    {
        const juce::ScopedLock lock(valueLock);
        currentValues = addressValues;
    }
    
    // Output values for each mapped address
    const int numMappings = (int)currentMappings.size();
    const int numOutputs = buffer.getNumChannels();
    
    for (int i = 0; i < numMappings && i < numOutputs; ++i)
    {
        const auto& mapping = currentMappings[i];
        float value = 0.0f;
        
        // Get value for this address
        auto it = currentValues.find(mapping.oscAddress);
        if (it != currentValues.end())
        {
            value = it->second;
            
            // Update mapping's last value
            {
                const juce::ScopedLock lock(mappingLock);
                if (i < (int)addressMappings.size())
                    addressMappings[i].lastValue = value;
            }
        }
        else
        {
            // Use last known value if address hasn't been seen recently
            value = mapping.lastValue;
        }
        
        // Output to channel
        float* channelBuffer = buffer.getWritePointer(i);
        juce::FloatVectorOperations::fill(channelBuffer, value, numSamples);
    }
    
    // Clear unused channels
    for (int i = numMappings; i < numOutputs; ++i)
    {
        float* channelBuffer = buffer.getWritePointer(i);
        juce::FloatVectorOperations::clear(channelBuffer, numSamples);
    }
}

//==============================================================================
bool OSCCVModuleProcessor::matchesPattern(const juce::String& address, const juce::String& pattern)
{
    if (pattern.isEmpty() || address.isEmpty())
        return false;
    
    // Use JUCE's OSCAddressPattern for pattern matching
    juce::OSCAddressPattern oscPattern(pattern);
    return oscPattern.matches(address);
}

float OSCCVModuleProcessor::midiNoteToCv(int noteNumber) const
{
    // 1V/octave standard: C4 (MIDI note 60) = 0V
    // Each semitone = 1/12 V
    // Map to 0-1 range (assuming -5V to +5V range = 0-1 normalized)
    float cv = (noteNumber - 60) / 12.0f;
    // Normalize to 0-1 range (assuming ±5V = 10V range, centered at 0.5)
    return juce::jlimit(0.0f, 1.0f, (cv / 10.0f) + 0.5f);
}

int OSCCVModuleProcessor::cvToMidiNote(float cv) const
{
    // Reverse of midiNoteToCv
    float normalizedCv = (cv - 0.5f) * 10.0f;
    int note = 60 + static_cast<int>(normalizedCv * 12.0f);
    return juce::jlimit(0, 127, note);
}

#if defined(PRESET_CREATOR_UI)
void OSCCVModuleProcessor::drawParametersInNode(float itemWidth, 
                                                  const std::function<bool(const juce::String&)>&,
                                                  const std::function<void()>&)
{
    #if defined(PRESET_CREATOR_UI)
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    #endif
    
    // Source filter
    ThemeText("Source", theme.text.section_header);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(itemWidth * 0.6f);
    if (ImGui::BeginCombo("##osc_source", sourceFilterParam ? "All Sources" : "None"))
    {
        ImGui::Selectable("All Sources", false);
        ImGui::EndCombo();
    }
    
    ImGui::Spacing();
    
    // Activity indicator
    auto recentAddresses = getRecentAddresses();
    bool hasActivity = !recentAddresses.empty();
    
    if (hasActivity)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green
        ImGui::Text("● Active");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // Gray
        ImGui::Text("○ Idle");
        ImGui::PopStyleColor();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Address Monitor Section
    ThemeText("Monitor Addresses", theme.text.section_header);
    ImGui::Spacing();
    
    {
        if (recentAddresses.empty())
        {
            ImGui::TextDisabled("No OSC messages received");
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Send OSC messages to see them here");
                ImGui::EndTooltip();
            }
        }
        else
        {
            // Read data BEFORE BeginChild (per guide)
            juce::uint64 now = juce::Time::getMillisecondCounter();
            
            // Calculate fixed size - show more items with proper scrolling
            // Use a reasonable height that shows multiple items but allows scrolling
            // Larger item height for better clickability
            const float itemHeight = ImGui::GetTextLineHeightWithSpacing() + 12.0f; // ~32-36px per item (larger for easier clicking)
            const int visibleItems = std::min(12, (int)recentAddresses.size()); // Show up to 12 items
            const float listHeight = std::max(200.0f, visibleItems * itemHeight); // Minimum 200px height
            const ImVec2 listSize(itemWidth, listHeight);
            
            if (ImGui::BeginChild("OSCAddressList", listSize, true, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove))
            {
                for (int i = 0; i < (int)recentAddresses.size(); ++i)
                {
                const auto& address = recentAddresses[i];
                
                // Check if address is mapped
                bool isMapped = isAddressMapped(address);
                
                // Check if address is active (received in last 500ms)
                bool isActive = false;
                {
                    const juce::ScopedLock lock(addressMonitorLock);
                    auto it = lastSeenAddresses.find(address);
                    if (it != lastSeenAddresses.end())
                    {
                        isActive = (now - it->second) < 500;
                    }
                }
                
                // Determine color: yellow if mapped, green if active, gray if stale
                ImVec4 textColor;
                if (isMapped)
                    textColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow for mapped
                else if (isActive)
                    textColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f); // Green if active
                else
                    textColor = ImVec4(0.9f, 0.9f, 0.9f, 1.0f); // Light gray for visibility (brighter)
                
                // Show mapped indicator
                if (isMapped)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                    ImGui::Text("✓ ");
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                }
                
                // Clickable address - use InvisibleButton for click detection, render text separately
                // This prevents drag capture while ensuring text is visible
                const float buttonHeight = ImGui::GetTextLineHeightWithSpacing() + 8.0f; // ~28-32px for better clickability
                
                // Use InvisibleButton for hit detection (doesn't interfere with drags)
                ImGui::InvisibleButton((address + "##click").toRawUTF8(), ImVec2(-1, buttonHeight));
                
                // Store rect for text rendering
                const ImVec2 buttonMin = ImGui::GetItemRectMin();
                const ImVec2 buttonMax = ImGui::GetItemRectMax();
                const bool isHovered = ImGui::IsItemHovered();
                
                // Only trigger action on actual click (not drag) - check if mouse is not currently dragging
                if (ImGui::IsItemClicked(0) && !ImGui::IsMouseDragging(0))
                {
                    addAddressMapping(address);
                }
                
                // Render text on top with proper color (ensures visibility)
                // Use screen coordinates for accurate positioning
                ImGui::SetCursorScreenPos(ImVec2(buttonMin.x + ImGui::GetStyle().FramePadding.x, 
                                                 buttonMin.y + ImGui::GetStyle().FramePadding.y));
                ImGui::PushStyleColor(ImGuiCol_Text, textColor);
                ImGui::Text("%s", address.toRawUTF8());
                ImGui::PopStyleColor();
                
                // Draw subtle background on hover for visual feedback
                if (isHovered)
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    drawList->AddRectFilled(buttonMin, buttonMax, 
                                           ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.3f, 0.3f, 0.3f)));
                }
                
                // Add small spacing between items for better separation
                ImGui::Spacing();
                
                // Tooltip with full address and value
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    if (isMapped)
                        ImGui::Text("Mapped to output");
                    else
                        ImGui::Text("Click to add as output");
                    ImGui::Text("Address: %s", address.toRawUTF8());
                    float value = getOutputValueForAddress(address);
                    ImGui::Text("Value: %.3f", value);
                    ImGui::EndTooltip();
                }
                }
                ImGui::EndChild();
            }
        }
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Show mapped outputs
    const juce::ScopedLock lock(mappingLock);
    if (!addressMappings.empty())
    {
        ThemeText("Mapped Outputs", theme.text.section_header);
        ImGui::Spacing();
        
        {
            // Calculate fixed size (per guide - use graphSize pattern)
            const float outputsHeight = std::min(150.0f, (float)addressMappings.size() * ImGui::GetTextLineHeightWithSpacing() * 1.5f);
            const ImVec2 outputsSize(itemWidth, outputsHeight);
            
            if (ImGui::BeginChild("OSCMappedOutputs", outputsSize, false, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove))
            {
                for (size_t i = 0; i < addressMappings.size(); ++i)
                {
                    const auto& mapping = addressMappings[i];
                    ImGui::PushID((int)i);
                    
                    // Truncate address if too long
                    juce::String addressText = mapping.oscAddress;
                    if (addressText.length() > 30)
                        addressText = addressText.substring(0, 27) + "...";
                    
                    ImGui::Text("%d: %s", (int)i, addressText.toRawUTF8());
                    ImGui::SameLine(itemWidth * 0.7f);
                    
                    // Show current value
                    ImGui::Text("= %.3f", mapping.lastValue);
                    ImGui::SameLine(itemWidth - 25);
                    
                    // Remove button
                    bool shouldBreak = false;
                    if (ImGui::SmallButton("×"))
                    {
                        removeAddressMapping((int)i);
                        shouldBreak = true;
                    }
                    
                    ImGui::PopID();
                    
                    if (shouldBreak)
                        break; // Exit loop after removal (indices will shift, will redraw next frame)
                }
                
                ImGui::EndChild();
            }
        }
    }
}

void OSCCVModuleProcessor::saveAddressMappingsToState()
{
    const juce::ScopedLock lock(mappingLock);
    
    juce::StringArray mappingStrings;
    for (const auto& mapping : addressMappings)
    {
        // Format: "address|type" where type is 0=CV, 1=Gate, 2=Audio
        int typeInt = 0;
        if (mapping.pinType == PinDataType::Gate)
            typeInt = 1;
        else if (mapping.pinType == PinDataType::Audio)
            typeInt = 2;
        
        mappingStrings.add(mapping.oscAddress + "|" + juce::String(typeInt));
    }
    
    // Store as comma-separated string
    juce::String data = mappingStrings.joinIntoString(",");
    apvts.state.setProperty("address_mappings", data, nullptr);
}

void OSCCVModuleProcessor::loadAddressMappingsFromState()
{
    const juce::ScopedLock lock(mappingLock);
    
    addressMappings.clear();
    
    juce::var value = apvts.state.getProperty("address_mappings");
    if (value.isString())
    {
        juce::String data = value.toString();
        if (data.isNotEmpty())
        {
            juce::StringArray mappingStrings;
            mappingStrings.addTokens(data, ",", "");
            
            for (const auto& mappingStr : mappingStrings)
            {
                int pipeIndex = mappingStr.indexOfChar('|');
                if (pipeIndex > 0)
                {
                    juce::String address = mappingStr.substring(0, pipeIndex);
                    juce::String typeStr = mappingStr.substring(pipeIndex + 1);
                    int typeInt = typeStr.getIntValue();
                    
                    AddressMapping mapping;
                    mapping.oscAddress = address;
                    mapping.outputChannel = (int)addressMappings.size();
                    mapping.pinType = (typeInt == 1) ? PinDataType::Gate : 
                                     (typeInt == 2) ? PinDataType::Audio : PinDataType::CV;
                    mapping.lastValue = 0.0f;
                    mapping.lastUpdateTime = 0;
                    
                    addressMappings.push_back(mapping);
                }
                else if (mappingStr.isNotEmpty())
                {
                    // Legacy format: just address (assume CV)
                    AddressMapping mapping;
                    mapping.oscAddress = mappingStr;
                    mapping.outputChannel = (int)addressMappings.size();
                    mapping.pinType = PinDataType::CV;
                    mapping.lastValue = 0.0f;
                    mapping.lastUpdateTime = 0;
                    
                    addressMappings.push_back(mapping);
                }
            }
        }
    }
}

void OSCCVModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Pins are handled dynamically via getDynamicOutputPins()
    // This function is called but we don't draw pins here since they're dynamic
    juce::ignoreUnused(helpers);
}
#endif

