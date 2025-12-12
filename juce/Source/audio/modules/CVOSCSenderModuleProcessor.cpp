#include "CVOSCSenderModuleProcessor.h"
#include <algorithm>
#include <cmath>
#include <set>

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif

//==============================================================================
CVOSCSenderModuleProcessor::CVOSCSenderModuleProcessor()
    : ModuleProcessor(juce::AudioProcessor::BusesProperties()
        .withInput("Main", juce::AudioChannelSet::discreteChannels(MAX_INPUTS), true)),  // Maximum 32 channels (flexible, can use 1-32)
      apvts(*this, nullptr, "CVOSCSenderParams", createParameterLayout()),
      targetHost("localhost"),
      targetPort(57120)
{
    // Get parameter pointers
    enabledParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("enabled"));
    sendModeParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("send_mode"));
    throttleRateParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("throttle_rate"));
    changeThresholdParam = dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("change_threshold"));
    
    // Initialize OSC sender
    oscSender = std::make_unique<juce::OSCSender>();
    
    // Create 8 default input mappings so there are always some pins available
    // This will be overwritten if mappings are loaded from APVTS state in prepareToPlay()
    {
        const juce::ScopedLock lock(mappingLock);
        for (int i = 1; i <= 8; ++i)
        {
            InputMapping defaultMapping;
            defaultMapping.oscAddress = "/cv/input" + juce::String(i);
            defaultMapping.inputType = PinDataType::CV;
            defaultMapping.enabled = true;
            defaultMapping.lastSentValue = 0.0f;
            defaultMapping.lastSendTime = juce::Time::getMillisecondCounter();
            inputMappings.push_back(defaultMapping);
        }
    }
    
    // Note: Input mappings and network settings will be loaded in prepareToPlay() after APVTS state is restored from patch
    updateConnection();
}

CVOSCSenderModuleProcessor::~CVOSCSenderModuleProcessor()
{
    if (oscSender)
    {
        oscSender->disconnect();
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout CVOSCSenderModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Master enable
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "enabled", "Enabled", true));
    
    // Send mode
    juce::StringArray sendModeChoices;
    sendModeChoices.add("Per Block");
    sendModeChoices.add("Throttled");
    sendModeChoices.add("On Change");
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "send_mode", "Send Mode", sendModeChoices, 2)); // Default to "On Change" (index 2)
    
    // Throttle rate (messages per second)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "throttle_rate", "Throttle Rate", juce::NormalisableRange<float>(1.0f, 1000.0f, 1.0f), 30.0f));
    
    // Change threshold
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "change_threshold", "Change Threshold", juce::NormalisableRange<float>(0.001f, 1.0f, 0.001f), 0.01f));
    
    // Note: targetHost and targetPort are stored as member variables, not in APVTS
    // (AudioParameterString doesn't exist in JUCE)
    
    return { params.begin(), params.end() };
}

//==============================================================================
void CVOSCSenderModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    // Load saved input mappings and network settings from APVTS state (state is guaranteed to be loaded by this point)
    loadInputMappingsFromState();
    
    // CRITICAL FIX: Always ensure bus layout is set to maximum 32 channels
    // JUCE validates connections against the CURRENT bus layout, not the maximum declared.
    // By always keeping the bus at 32 channels, connections to any channel 0-31 will work.
    // We don't need to change it dynamically - we just use as many channels as we have mappings.
    {
        const juce::ScopedLock lock(mappingLock);
        int numMappings = (int)inputMappings.size();
        
        // CRITICAL: Truncate mappings if somehow we have more than MAX_INPUTS (e.g., from old patch)
        if (numMappings > MAX_INPUTS)
        {
            juce::Logger::writeToLog("[CVOSCSender] prepareToPlay(): WARNING - Found " + juce::String(numMappings) + " mappings, truncating to " + juce::String(MAX_INPUTS));
            inputMappings.resize(MAX_INPUTS);
            numMappings = MAX_INPUTS;
            // Save the truncated state
            saveInputMappingsToState();
        }
        
        // ALWAYS set bus layout to maximum 32 channels (so connections to 0-31 always work)
        BusesLayout maxLayout;
        maxLayout.inputBuses.add(juce::AudioChannelSet::discreteChannels(MAX_INPUTS));
        maxLayout.outputBuses.add(juce::AudioChannelSet::disabled());
        
        BusesLayout currentLayout = getBusesLayout();
        int currentChannels = currentLayout.getMainInputChannelSet().size();
        
        if (currentChannels != MAX_INPUTS)
        {
            juce::Logger::writeToLog("[CVOSCSender] prepareToPlay(): Setting bus layout to " + juce::String(MAX_INPUTS) + " channels (current: " + juce::String(currentChannels) + ", mappings: " + juce::String(numMappings) + ")");
            bool layoutChanged = setBusesLayout(maxLayout);
            if (!layoutChanged)
            {
                juce::Logger::writeToLog("[CVOSCSender] prepareToPlay(): WARNING - setBusesLayout() returned false! Graph may be controlling layout.");
            }
        }
    }
    
    updateConnection();
}

void CVOSCSenderModuleProcessor::releaseResources()
{
    if (oscSender)
    {
        oscSender->disconnect();
        isConnected = false;
    }
}

//==============================================================================
void CVOSCSenderModuleProcessor::updateConnection()
{
    const juce::ScopedLock lock(senderLock);
    
    if (!oscSender)
        return;
    
    bool shouldBeConnected = enabledParam && enabledParam->get();
    
    if (shouldBeConnected && !isConnected)
    {
        // Connect
        bool connected = oscSender->connect(targetHost, targetPort);
        if (connected)
        {
            isConnected = true;
            juce::Logger::writeToLog("[CVOSCSender] Connected to " + targetHost + ":" + juce::String(targetPort));
        }
        else
        {
            juce::Logger::writeToLog("[CVOSCSender] Failed to connect to " + targetHost + ":" + juce::String(targetPort));
        }
    }
    else if (!shouldBeConnected && isConnected)
    {
        // Disconnect
        oscSender->disconnect();
        isConnected = false;
        juce::Logger::writeToLog("[CVOSCSender] Disconnected");
    }
}

//==============================================================================
float CVOSCSenderModuleProcessor::computeOutputValue(int channel, const juce::AudioBuffer<float>& buffer)
{
    if (channel >= buffer.getNumChannels())
        return 0.0f;
    
    const float* channelData = buffer.getReadPointer(channel);
    const int numSamples = buffer.getNumSamples();
    
    if (numSamples == 0)
        return 0.0f;
    
    // Detect pin type based on signal characteristics
    PinDataType pinType = detectPinType(channel, buffer);
    
    switch (pinType)
    {
        case PinDataType::Gate:
            // For gates, check if any sample is > 0.5
            for (int i = 0; i < numSamples; ++i)
            {
                if (channelData[i] > 0.5f)
                    return 1.0f;
            }
            return 0.0f;
            
        case PinDataType::CV:
            // For CV, use average value
            {
                float sum = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                    sum += channelData[i];
                return sum / numSamples;
            }
            
        case PinDataType::Audio:
            // For audio, use peak magnitude
            {
                float peak = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                    peak = std::max(peak, std::abs(channelData[i]));
                return peak;
            }
            
        default:
            // Default: use average
            {
                float sum = 0.0f;
                for (int i = 0; i < numSamples; ++i)
                    sum += channelData[i];
                return sum / numSamples;
            }
    }
}

PinDataType CVOSCSenderModuleProcessor::detectPinType(int channel, const juce::AudioBuffer<float>& buffer) const
{
    if (channel >= buffer.getNumChannels())
        return PinDataType::CV;
    
    const float* channelData = buffer.getReadPointer(channel);
    const int numSamples = buffer.getNumSamples();
    
    if (numSamples == 0)
        return PinDataType::CV;
    
    // Check if it's a gate signal (switches between 0.0 and 1.0)
    int nearZero = 0;
    int nearOne = 0;
    float sum = 0.0f;
    float maxAbs = 0.0f;
    float minVal = channelData[0];
    float maxVal = channelData[0];
    int transitions = 0; // Count transitions between 0 and 1
    
    for (int i = 0; i < numSamples; ++i)
    {
        float val = channelData[i];
        sum += val;
        maxAbs = std::max(maxAbs, std::abs(val));
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
        
        if (std::abs(val) < 0.1f)
            nearZero++;
        else if (std::abs(val - 1.0f) < 0.1f)
            nearOne++;
        
        // Detect transitions (0->1 or 1->0) - gates have frequent transitions
        if (i > 0)
        {
            float prev = channelData[i - 1];
            bool prevIsGate = (std::abs(prev) < 0.1f || std::abs(prev - 1.0f) < 0.1f);
            bool currIsGate = (std::abs(val) < 0.1f || std::abs(val - 1.0f) < 0.1f);
            if (prevIsGate && currIsGate && std::abs(prev - val) > 0.5f)
                transitions++;
        }
    }
    
    // If peak is > 1.0, it's likely audio
    if (maxAbs > 1.0f)
        return PinDataType::Audio;
    
    // Gate detection: Must have both 0s and 1s (not just one), and frequent transitions
    // Also check if range is very limited (gate signals stay close to 0 or 1)
    bool hasBothZeroAndOne = (nearZero > numSamples * 0.2f) && (nearOne > numSamples * 0.2f);
    bool mostlyBinary = (nearZero + nearOne > numSamples * 0.9f);
    float range = maxVal - minVal;
    bool hasTransitions = (transitions > numSamples * 0.05f); // At least 5% of samples have transitions
    
    // More strict: gate must have both 0s and 1s AND transitions (not just sitting at one value)
    if (mostlyBinary && (hasBothZeroAndOne || hasTransitions) && range > 0.5f)
        return PinDataType::Gate;
    
    // If signal has variation but is within 0-1 range, it's CV
    if (range > 0.01f && maxVal <= 1.0f && minVal >= 0.0f)
        return PinDataType::CV;
    
    // Default to CV (safer default than Gate)
    return PinDataType::CV;
}

bool CVOSCSenderModuleProcessor::shouldSend(int channel, float currentValue, float lastValue)
{
    if (!sendModeParam)
        return false;
    
    int sendMode = sendModeParam->getIndex();
    
    switch (sendMode)
    {
        case 0: // Per Block
            return true; // Always send once per block
            
        case 1: // Throttled
        {
            const juce::ScopedLock lock(mappingLock);
            if (channel >= 0 && channel < (int)inputMappings.size())
            {
                juce::uint64 now = juce::Time::getMillisecondCounter();
                float throttleMs = 1000.0f / (throttleRateParam ? throttleRateParam->get() : 30.0f);
                
                if (now - inputMappings[channel].lastSendTime >= (juce::uint64)throttleMs)
                {
                    inputMappings[channel].lastSendTime = now;
                    return true;
                }
            }
            return false;
        }
        
        case 2: // On Change
        {
            float threshold = changeThresholdParam ? changeThresholdParam->get() : 0.01f;
            return std::abs(currentValue - lastValue) >= threshold;
        }
        
        default:
            return false;
    }
}

void CVOSCSenderModuleProcessor::sendOSCMessage(int channel, float value)
{
    const juce::ScopedLock lock(senderLock);
    
    if (!oscSender || !isConnected)
        return;
    
    // Get mapping for this channel
    const juce::ScopedLock mappingLockGuard(mappingLock);
    if (channel < 0 || channel >= (int)inputMappings.size())
        return;
    
    const auto& mapping = inputMappings[channel];
    if (!mapping.enabled || mapping.oscAddress.isEmpty())
        return;
    
    // Create OSC message
    juce::OSCMessage msg(mapping.oscAddress);
    
    // Add value based on type
    switch (mapping.inputType)
    {
        case PinDataType::Gate:
            // Send as float32 (0.0 or 1.0)
            msg.addFloat32(value >= 0.5f ? 1.0f : 0.0f);
            break;
            
        case PinDataType::CV:
        case PinDataType::Audio:
        default:
            msg.addFloat32(value);
            break;
    }
    
    // Send message
    oscSender->send(msg);
    
    // Update activity tracking
    messagesSentThisBlock++;
    totalMessagesSent++;
}

//==============================================================================
void CVOSCSenderModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    
    // Check if enabled
    if (!enabledParam || !enabledParam->get())
    {
        return;
    }
    
    // Update connection if needed (check periodically, not every block)
    static int connectionCheckCounter = 0;
    if (++connectionCheckCounter >= 1000) // Check every 1000 blocks (~20 seconds at 48kHz/512)
    {
        connectionCheckCounter = 0;
        updateConnection();
    }
    
    // Reset messages sent counter
    messagesSentThisBlock.store(0);
    
    // Get current mappings (thread-safe copy)
    std::vector<InputMapping> currentMappings;
    {
        const juce::ScopedLock lock(mappingLock);
        currentMappings = inputMappings;
    }
    
    const int numInputs = buffer.getNumChannels();
    const int numMappings = (int)currentMappings.size();
    
    // Don't auto-create mappings - user must explicitly add them via UI
    // Only process as many channels as we have mappings
    
    // Process each mapped input
    for (int i = 0; i < numMappings && i < numInputs; ++i)
    {
        const auto& mapping = currentMappings[i];
        if (!mapping.enabled)
            continue;
        
        // Compute representative value for this block
        float value = computeOutputValue(i, buffer);
        
        // Update input type if needed (for dynamic detection)
        {
            PinDataType detectedType = detectPinType(i, buffer);
            const juce::ScopedLock lock(mappingLock);
            if (i < (int)inputMappings.size())
            {
                // Only update if not manually set (for now, just use detected)
                inputMappings[i].inputType = detectedType;
            }
        }
        
        // Check if we should send
        if (shouldSend(i, value, mapping.lastSentValue))
        {
            sendOSCMessage(i, value);
            
            // Update last sent value
            {
                const juce::ScopedLock lock(mappingLock);
                if (i < (int)inputMappings.size())
                {
                    inputMappings[i].lastSentValue = value;
                }
            }
        }
    }
    
    // Reset activity counter periodically (for UI display)
    juce::uint64 now = juce::Time::getMillisecondCounter();
    if (now - lastActivityResetTime > 1000) // Reset every second
    {
        totalMessagesSent.store(0);
        lastActivityResetTime = now;
    }
}

//==============================================================================
bool CVOSCSenderModuleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Support up to 32 input channels (no outputs)
    const int maxChannels = MAX_INPUTS;
    return layouts.getMainInputChannelSet().size() <= maxChannels
           && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::disabled();
}

//==============================================================================
std::vector<DynamicPinInfo> CVOSCSenderModuleProcessor::getDynamicInputPins() const
{
    std::vector<DynamicPinInfo> pins;
    
    const juce::ScopedLock lock(mappingLock);
    for (size_t i = 0; i < inputMappings.size(); ++i)
    {
        const auto& mapping = inputMappings[i];
        
        // Generate label from OSC address or use default (exactly like OSCCVModuleProcessor)
        juce::String label = mapping.oscAddress;
        if (label.isEmpty())
            label = "input" + juce::String((int)(i + 1));
        else
        {
            // Use last part of address as label (e.g., "/data/motion/gyroscope/x" -> "gyroscope/x")
            int lastSlash = label.lastIndexOfChar('/');
            if (lastSlash >= 0 && lastSlash < label.length() - 1)
            {
                label = label.substring(lastSlash + 1);
            }
            // Limit label length (like OSCCVModuleProcessor)
            if (label.length() > 20)
                label = label.substring(0, 17) + "...";
        }
        
        // Use the index in the mappings array as the channel index (must match processBlock indexing)
        pins.push_back(DynamicPinInfo(label, (int)i, mapping.inputType));
    }
    
    // Don't show default pin - pins are only shown when mappings exist
    // User must click "+ Add Input Mapping" to create the first pin
    
    return pins;
}

//==============================================================================
void CVOSCSenderModuleProcessor::addInputMapping(const juce::String& address)
{
    juce::Logger::writeToLog("[CVOSCSender] addInputMapping() called with address: " + address);
    
    if (address.isEmpty())
    {
        juce::Logger::writeToLog("[CVOSCSender] ERROR: Empty address provided!");
        return;
    }
    
    const juce::ScopedLock lock(mappingLock);
    
    int oldSize = (int)inputMappings.size();
    juce::Logger::writeToLog("[CVOSCSender] Current mappings count: " + juce::String(oldSize));
    
    // Enforce maximum of 32 input channels
    if (oldSize >= MAX_INPUTS)
    {
        juce::Logger::writeToLog("[CVOSCSender] ERROR: Maximum of " + juce::String(MAX_INPUTS) + " input mappings reached!");
        return;
    }
    
    // Check if already mapped
    for (const auto& mapping : inputMappings)
    {
        if (mapping.oscAddress == address)
        {
            juce::Logger::writeToLog("[CVOSCSender] Address already mapped, skipping: " + address);
            return; // Already mapped
        }
    }
    
    // Add new mapping
    InputMapping mapping;
    mapping.oscAddress = address;
    mapping.inputType = PinDataType::CV; // Default, will be auto-detected
    mapping.enabled = true;
    mapping.lastSentValue = 0.0f;
    mapping.lastSendTime = juce::Time::getMillisecondCounter();
    
    inputMappings.push_back(mapping);
    int newSize = (int)inputMappings.size();
    juce::Logger::writeToLog("[CVOSCSender] Added mapping, new count: " + juce::String(newSize) + ", channel index will be: " + juce::String(newSize - 1));
    
    // Bus layout is always set to 32 channels in prepareToPlay(), so connections to any channel 0-31 will work
    // No need to update it here - the graph will rebuild and call prepareToPlay() which ensures it's at MAX_INPUTS
    
    // Save to APVTS state for persistence
    saveInputMappingsToState();
    juce::Logger::writeToLog("[CVOSCSender] Saved mappings to APVTS state");
    
    // Note: Graph rebuild will happen automatically when nodes are redrawn
}

// Helper function to find next available input number for default addresses
int CVOSCSenderModuleProcessor::findNextAvailableInputNumber() const
{
    const juce::ScopedLock lock(mappingLock);
    
    // Extract numbers from existing addresses (e.g., "/cv/input12" -> 12)
    std::set<int> usedNumbers;
    for (const auto& mapping : inputMappings)
    {
        juce::String addr = mapping.oscAddress;
        if (addr.startsWith("/cv/input"))
        {
            juce::String numStr = addr.substring(9); // Skip "/cv/input"
            int num = numStr.getIntValue();
            if (num > 0)
                usedNumbers.insert(num);
        }
    }
    
    // Find the first unused number starting from 1
    for (int i = 1; i <= MAX_INPUTS * 2; ++i) // Check up to double MAX_INPUTS to handle edge cases
    {
        if (usedNumbers.find(i) == usedNumbers.end())
            return i;
    }
    
    // Fallback: use size + 1 if somehow all numbers are used
    return (int)inputMappings.size() + 1;
}

void CVOSCSenderModuleProcessor::removeInputMapping(int index)
{
    const juce::ScopedLock lock(mappingLock);
    
    // Prevent removing if we're at the minimum (8 default mappings)
    const int minMappings = 8;
    if ((int)inputMappings.size() <= minMappings)
    {
        juce::Logger::writeToLog("[CVOSCSender] Cannot remove mapping - minimum of " + juce::String(minMappings) + " mappings required");
        return;
    }
    
    if (index >= 0 && index < (int)inputMappings.size())
    {
        inputMappings.erase(inputMappings.begin() + index);
        
        // Save to APVTS state for persistence
        saveInputMappingsToState();
        
        // Note: Graph rebuild will happen automatically when nodes are redrawn
    }
}

void CVOSCSenderModuleProcessor::updateInputMappingAddress(int index, const juce::String& newAddress)
{
    const juce::ScopedLock lock(mappingLock);
    
    if (index >= 0 && index < (int)inputMappings.size())
    {
        inputMappings[index].oscAddress = newAddress;
        
        // Save to APVTS state for persistence
        saveInputMappingsToState();
    }
}

void CVOSCSenderModuleProcessor::setInputMappingEnabled(int index, bool enabled)
{
    const juce::ScopedLock lock(mappingLock);
    
    if (index >= 0 && index < (int)inputMappings.size())
    {
        inputMappings[index].enabled = enabled;
        
        // Save to APVTS state for persistence
        saveInputMappingsToState();
    }
}

#if defined(PRESET_CREATOR_UI)
void CVOSCSenderModuleProcessor::drawParametersInNode(float itemWidth, 
                                                       const std::function<bool(const juce::String&)>& isParamModulated,
                                                       const std::function<void()>& onModificationEnded)
{
    #if defined(PRESET_CREATOR_UI)
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();
    #endif
    
    // Network Settings
    ThemeText("Network", theme.text.section_header);
    ImGui::Spacing();
    
    // Enable checkbox
    bool enabled = enabledParam ? enabledParam->get() : false;
    if (ImGui::Checkbox("Enabled", &enabled))
    {
        if (enabledParam)
            enabledParam->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
        updateConnection();
        onModificationEnded();
    }
    
    // Host input
    ImGui::Text("Host:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemWidth * 0.6f);
    char hostBuf[256];
    targetHost.copyToUTF8(hostBuf, 256);
    bool hostChanged = ImGui::InputText("##host", hostBuf, 256, ImGuiInputTextFlags_EnterReturnsTrue);
    bool hostDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    // Update when text changes or when Enter is pressed/focus is lost
    if (hostChanged || hostDeactivated)
    {
        juce::String newHost = juce::String(hostBuf);
        bool valueChanged = (newHost != targetHost);
        if (valueChanged)
        {
            targetHost = newHost;
            apvts.state.setProperty("target_host", targetHost, nullptr);
        }
        // Always update connection if Enter was pressed (deactivated after edit) or value changed
        if (hostDeactivated || valueChanged)
        {
            updateConnection();
        }
    }
    
    // Port input
    ImGui::Text("Port:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(itemWidth * 0.4f);
    int port = targetPort;
    bool portChanged = ImGui::InputInt("##port", &port, 1, 100, ImGuiInputTextFlags_EnterReturnsTrue);
    bool portDeactivated = ImGui::IsItemDeactivatedAfterEdit();
    // Update when value changes or when Enter is pressed/focus is lost
    if (portChanged || portDeactivated)
    {
        int newPort = juce::jlimit(1, 65535, port);
        bool valueChanged = (newPort != targetPort);
        if (valueChanged)
        {
            targetPort = newPort;
            apvts.state.setProperty("target_port", targetPort, nullptr);
        }
        // Always update connection if Enter was pressed (deactivated after edit) or value changed
        if (portDeactivated || valueChanged)
        {
            updateConnection();
        }
    }
    
    // Connection status
    ImGui::SameLine();
    if (isConnected)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Green
        ImGui::Text("● Connected");
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red
        ImGui::Text("○ Disconnected");
        ImGui::PopStyleColor();
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Send Mode
    ThemeText("Send Mode", theme.text.section_header);
    ImGui::Spacing();
    ImGui::SetNextItemWidth(itemWidth * 0.6f);
    if (sendModeParam)
    {
        bool isSendModeModulated = isParamModulated("send_mode");
        if (isSendModeModulated) ImGui::BeginDisabled();
        
        int mode = sendModeParam->getIndex();
        juce::StringArray choices = sendModeParam->choices;
        if (ImGui::Combo("##send_mode", &mode, [](void* data, int idx, const char** out_text) {
            juce::StringArray* choices = static_cast<juce::StringArray*>(data);
            if (idx >= 0 && idx < choices->size())
            {
                *out_text = (*choices)[idx].toRawUTF8();
                return true;
            }
            return false;
        }, &choices, choices.size()))
        {
            if (!isSendModeModulated)
            {
                sendModeParam->setValueNotifyingHost((float)mode / (float)(choices.size() - 1));
                onModificationEnded();
            }
        }
        // Scroll-edit for Combo (adjustParamOnWheel handles AudioParameterChoice)
        if (!isSendModeModulated)
            adjustParamOnWheel(ap.getParameter("send_mode"), "send_mode", (float)mode);
        if (ImGui::IsItemDeactivatedAfterEdit() && !isSendModeModulated)
            onModificationEnded();
        if (isSendModeModulated)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }
    
    // Throttle rate (only show if throttled mode)
    if (sendModeParam && sendModeParam->getIndex() == 1)
    {
        bool isThrottleModulated = isParamModulated("throttle_rate");
        if (isThrottleModulated) ImGui::BeginDisabled();
        
        ImGui::Text("Rate:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth * 0.5f);
        if (throttleRateParam)
        {
            float rate = isThrottleModulated ? getLiveParamValueFor("throttle_rate", "throttle_rate_live", throttleRateParam->get()) : throttleRateParam->get();
            if (ImGui::SliderFloat("##throttle", &rate, 1.0f, 1000.0f, "%.0f msg/s"))
            {
                if (!isThrottleModulated)
                {
                    *throttleRateParam = rate;
                    onModificationEnded();
                }
            }
            if (!isThrottleModulated)
                adjustParamOnWheel(ap.getParameter("throttle_rate"), "throttle_rate", rate);
            if (ImGui::IsItemDeactivatedAfterEdit() && !isThrottleModulated)
                onModificationEnded();
        }
        if (isThrottleModulated)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }
    
    // Change threshold (only show if on-change mode)
    if (sendModeParam && sendModeParam->getIndex() == 2)
    {
        bool isThresholdModulated = isParamModulated("change_threshold");
        if (isThresholdModulated) ImGui::BeginDisabled();
        
        ImGui::Text("Threshold:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(itemWidth * 0.5f);
        if (changeThresholdParam)
        {
            float threshold = isThresholdModulated ? getLiveParamValueFor("change_threshold", "change_threshold_live", changeThresholdParam->get()) : changeThresholdParam->get();
            if (ImGui::SliderFloat("##threshold", &threshold, 0.001f, 1.0f, "%.3f"))
            {
                if (!isThresholdModulated)
                {
                    *changeThresholdParam = threshold;
                    onModificationEnded();
                }
            }
            if (!isThresholdModulated)
                adjustParamOnWheel(ap.getParameter("change_threshold"), "change_threshold", threshold);
            if (ImGui::IsItemDeactivatedAfterEdit() && !isThresholdModulated)
                onModificationEnded();
        }
        if (isThresholdModulated)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }
    
    ImGui::Spacing();
    ImGui::Spacing();
    
    // Input Mappings
    ThemeText("Input Mappings", theme.text.section_header);
    ImGui::Spacing();
    
    {
        const juce::ScopedLock lock(mappingLock);
        
        if (inputMappings.empty())
        {
            ImGui::TextDisabled("No inputs mapped. Add mappings below.");
        }
        else
        {
            // Calculate fixed size - show more items with proper scrolling
            // Use a reasonable height that shows multiple items but allows scrolling
            const float itemHeight = ImGui::GetTextLineHeightWithSpacing() + 4.0f; // ~22-24px per item
            const int visibleItems = std::min(20, (int)inputMappings.size()); // Show up to 20 items
            const float mappingsHeight = std::max(300.0f, visibleItems * itemHeight); // Minimum 300px height
            const ImVec2 mappingsSize(itemWidth, mappingsHeight);
            
            if (ImGui::BeginChild("CVOSCInputMappings", mappingsSize, true, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove))
            {
                // Show all mappings with scrolling
                for (size_t i = 0; i < inputMappings.size(); ++i)
                {
                auto& mapping = inputMappings[i];
                ImGui::PushID((int)i);
                
                ImGui::Text("Input %d:", (int)(i + 1));
                
                // Enable checkbox
                bool enabled = mapping.enabled;
                ImGui::SameLine(50);
                if (ImGui::Checkbox("##enabled", &enabled))
                {
                    setInputMappingEnabled((int)i, enabled);
                }
                
                // Address input (constrained width)
                ImGui::SameLine(70);
                ImGui::SetNextItemWidth(itemWidth * 0.55f);
                char addrBuf[256];
                mapping.oscAddress.copyToUTF8(addrBuf, 256);
                if (ImGui::InputText("##address", addrBuf, 256))
                {
                    updateInputMappingAddress((int)i, juce::String(addrBuf));
                }
                
                // Type indicator
                ImGui::SameLine(itemWidth * 0.65f);
                const char* typeStr = "CV";
                if (mapping.inputType == PinDataType::Gate)
                    typeStr = "Gate";
                else if (mapping.inputType == PinDataType::Audio)
                    typeStr = "Audio";
                ImGui::TextDisabled("[%s]", typeStr);
                
                // Last value
                ImGui::SameLine(itemWidth * 0.75f);
                ImGui::Text("= %.3f", mapping.lastSentValue);
                
                // Remove button (aligned to right, disabled if at minimum)
                ImGui::SameLine(itemWidth - 25);
                bool canRemove = (inputMappings.size() > 8); // Minimum 8 mappings
                if (!canRemove) ImGui::BeginDisabled();
                if (ImGui::SmallButton("×"))
                {
                    removeInputMapping((int)i);
                    onModificationEnded();
                    ImGui::PopID();
                    if (!canRemove) ImGui::EndDisabled();
                    break; // Exit loop after removal (indices will shift, will redraw next frame)
                }
                if (!canRemove)
                {
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Minimum of 8 mappings required");
                        ImGui::EndTooltip();
                    }
                }
                
                ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        
        // Add mapping button (disabled when at maximum)
        {
            const juce::ScopedLock lock(mappingLock);
            bool atMax = (inputMappings.size() >= MAX_INPUTS);
            if (atMax) ImGui::BeginDisabled();
            
            if (ImGui::Button("+ Add Input Mapping"))
            {
                juce::Logger::writeToLog("[CVOSCSender] UI: '+ Add Input Mapping' button clicked!");
                int nextNum = findNextAvailableInputNumber();
                juce::String newAddress = "/cv/input" + juce::String(nextNum);
                juce::Logger::writeToLog("[CVOSCSender] UI: Current mappings size: " + juce::String((int)inputMappings.size()) + ", creating address: " + newAddress);
                addInputMapping(newAddress);
                onModificationEnded();
            }
            
            if (atMax)
            {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Maximum of 32 input mappings reached");
                    ImGui::EndTooltip();
                }
            }
        }
    }
    
    // Activity Monitor
    ImGui::Spacing();
    ImGui::Spacing();
    int msgsPerSec = totalMessagesSent.load();
    ImGui::Text("Activity: %d msg/s", msgsPerSec);
    
    // Simple activity bar
    float activityLevel = juce::jlimit(0.0f, 1.0f, msgsPerSec / 100.0f);
    ImGui::ProgressBar(activityLevel, ImVec2(itemWidth, 0), "");
}

void CVOSCSenderModuleProcessor::saveInputMappingsToState()
{
    const juce::ScopedLock lock(mappingLock);
    
    juce::StringArray mappingStrings;
    for (const auto& mapping : inputMappings)
    {
        // Format: "address|type|enabled" where type is 0=CV, 1=Gate, 2=Audio, enabled is 0 or 1
        int typeInt = 0;
        if (mapping.inputType == PinDataType::Gate)
            typeInt = 1;
        else if (mapping.inputType == PinDataType::Audio)
            typeInt = 2;
        
        int enabledInt = mapping.enabled ? 1 : 0;
        mappingStrings.add(mapping.oscAddress + "|" + juce::String(typeInt) + "|" + juce::String(enabledInt));
    }
    
    // Store as comma-separated string
    juce::String data = mappingStrings.joinIntoString(",");
    apvts.state.setProperty("input_mappings", data, nullptr);
}

void CVOSCSenderModuleProcessor::loadInputMappingsFromState()
{
    const juce::ScopedLock lock(mappingLock);
    
    // Load network settings first
    juce::var hostVar = apvts.state.getProperty("target_host");
    if (hostVar.isString())
        targetHost = hostVar.toString();
    
    juce::var portVar = apvts.state.getProperty("target_port");
    if (portVar.isInt() || portVar.isInt64())
        targetPort = (int)portVar;
    
    // Load input mappings - only clear and reload if we have saved mappings
    juce::var value = apvts.state.getProperty("input_mappings");
    if (value.isString())
    {
        juce::String data = value.toString();
        if (data.isNotEmpty())
        {
            // Only clear if we have saved mappings to load (preserves 4 defaults for new nodes)
            inputMappings.clear();
            
            juce::StringArray mappingStrings;
            mappingStrings.addTokens(data, ",", "");
            
            for (const auto& mappingStr : mappingStrings)
            {
                juce::StringArray parts;
                parts.addTokens(mappingStr, "|", "");
                
                if (parts.size() >= 1 && parts[0].isNotEmpty())
                {
                    InputMapping mapping;
                    mapping.oscAddress = parts[0];
                    
                    // Parse type (default to CV)
                    if (parts.size() >= 2)
                    {
                        int typeInt = parts[1].getIntValue();
                        mapping.inputType = (typeInt == 1) ? PinDataType::Gate : 
                                           (typeInt == 2) ? PinDataType::Audio : PinDataType::CV;
                    }
                    else
                    {
                        mapping.inputType = PinDataType::CV;
                    }
                    
                    // Parse enabled (default to true)
                    if (parts.size() >= 3)
                        mapping.enabled = (parts[2].getIntValue() != 0);
                    else
                        mapping.enabled = true;
                    
                    mapping.lastSentValue = 0.0f;
                    mapping.lastSendTime = 0;
                    
                    inputMappings.push_back(mapping);
                    
                    // CRITICAL: Enforce maximum of 32 mappings (to match bus layout limit)
                    if (inputMappings.size() >= MAX_INPUTS)
                    {
                        juce::Logger::writeToLog("[CVOSCSender] loadInputMappingsFromState(): Reached maximum of " + juce::String(MAX_INPUTS) + " mappings, truncating remaining");
                        break; // Stop loading more mappings
                    }
                }
            }
            
            // Log final count after loading
            if (inputMappings.size() > MAX_INPUTS)
            {
                juce::Logger::writeToLog("[CVOSCSender] loadInputMappingsFromState(): WARNING - Loaded " + juce::String((int)inputMappings.size()) + " mappings, truncating to " + juce::String(MAX_INPUTS));
                inputMappings.resize(MAX_INPUTS); // Safety truncation
            }
            
            juce::Logger::writeToLog("[CVOSCSender] loadInputMappingsFromState(): Loaded " + juce::String((int)inputMappings.size()) + " mappings from saved state");
        }
    }
    // If no saved mappings, keep the 8 default mappings created in constructor
    else
    {
        juce::Logger::writeToLog("[CVOSCSender] loadInputMappingsFromState(): No saved mappings, using " + juce::String((int)inputMappings.size()) + " default mappings");
    }
    
    // Ensure at least 8 default mappings exist (if fewer were loaded, create defaults)
    const int minMappings = 8;
    while ((int)inputMappings.size() < minMappings)
    {
        int nextNum = findNextAvailableInputNumber();
        juce::Logger::writeToLog("[CVOSCSender] loadInputMappingsFromState(): Creating default mapping #" + juce::String((int)inputMappings.size() + 1));
        InputMapping defaultMapping;
        defaultMapping.oscAddress = "/cv/input" + juce::String(nextNum);
        defaultMapping.inputType = PinDataType::CV;
        defaultMapping.enabled = true;
        defaultMapping.lastSentValue = 0.0f;
        defaultMapping.lastSendTime = 0;
        inputMappings.push_back(defaultMapping);
    }
}

void CVOSCSenderModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Pins are handled dynamically via getDynamicInputPins()
    juce::ignoreUnused(helpers);
}
#endif

