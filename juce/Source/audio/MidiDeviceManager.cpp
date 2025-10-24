#include "MidiDeviceManager.h"

//==============================================================================
MidiDeviceManager::MidiDeviceManager(juce::AudioDeviceManager& adm)
    : deviceManager(adm)
{
    // Start hot-plug detection timer (checks every second)
    startTimer(1000);
    
    // Initial device scan
    scanDevices();
    
    juce::Logger::writeToLog("[MidiDeviceManager] Initialized");
}

MidiDeviceManager::~MidiDeviceManager()
{
    stopTimer();
    
    // Clean up all MIDI callbacks
    for (const auto& [identifier, deviceInfo] : devices)
    {
        if (deviceInfo.enabled)
        {
            try
            {
                deviceManager.removeMidiInputDeviceCallback(identifier, this);
            }
            catch (...)
            {
                // Ignore errors during shutdown - device may already be gone
            }
        }
    }
    
    juce::Logger::writeToLog("[MidiDeviceManager] Shut down");
}

//==============================================================================
// Device Management
//==============================================================================

void MidiDeviceManager::scanDevices()
{
    auto availableDevices = juce::MidiInput::getAvailableDevices();
    
    // Build a set of current identifiers for comparison
    juce::StringArray currentIdentifiers;
    for (const auto& device : availableDevices)
        currentIdentifiers.add(device.identifier);
    
    // Remove devices that are no longer available
    std::vector<juce::String> toRemove;
    for (const auto& [identifier, deviceInfo] : devices)
    {
        if (!currentIdentifiers.contains(identifier))
        {
            toRemove.push_back(identifier);
            juce::Logger::writeToLog("[MidiDeviceManager] Device removed: " + deviceInfo.name);
        }
    }
    
    for (const auto& identifier : toRemove)
    {
        if (devices[identifier].enabled)
        {
            try
            {
                deviceManager.removeMidiInputDeviceCallback(identifier, this);
            }
            catch (...) {}
        }
        devices.erase(identifier);
    }
    
    // Add or update devices
    for (const auto& device : availableDevices)
    {
        if (devices.find(device.identifier) == devices.end())
        {
            // New device
            DeviceInfo info;
            info.identifier = device.identifier;
            info.name = device.name;
            info.enabled = false;
            info.deviceIndex = nextDeviceIndex++;
            
            devices[device.identifier] = info;
            
            juce::Logger::writeToLog("[MidiDeviceManager] Device found: " + device.name + 
                                    " (index " + juce::String(info.deviceIndex) + ")");
        }
        else
        {
            // Update name in case it changed
            devices[device.identifier].name = device.name;
        }
    }
    
    juce::Logger::writeToLog("[MidiDeviceManager] Scan complete. Total devices: " + 
                            juce::String(devices.size()));
}

void MidiDeviceManager::enableDevice(const juce::String& identifier)
{
    auto it = devices.find(identifier);
    if (it == devices.end())
    {
        juce::Logger::writeToLog("[MidiDeviceManager] Cannot enable unknown device: " + identifier);
        return;
    }
    
    if (it->second.enabled)
    {
        // Already enabled
        return;
    }
    
    // Enable in AudioDeviceManager
    if (!deviceManager.isMidiInputDeviceEnabled(identifier))
    {
        deviceManager.setMidiInputDeviceEnabled(identifier, true);
    }
    
    // Add our callback
    deviceManager.addMidiInputDeviceCallback(identifier, this);
    
    it->second.enabled = true;
    
    juce::Logger::writeToLog("[MidiDeviceManager] Enabled device: " + it->second.name);
}

void MidiDeviceManager::disableDevice(const juce::String& identifier)
{
    auto it = devices.find(identifier);
    if (it == devices.end())
    {
        juce::Logger::writeToLog("[MidiDeviceManager] Cannot disable unknown device: " + identifier);
        return;
    }
    
    if (!it->second.enabled)
    {
        // Already disabled
        return;
    }
    
    // Remove our callback
    try
    {
        deviceManager.removeMidiInputDeviceCallback(identifier, this);
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("[MidiDeviceManager] Error removing callback: " + juce::String(e.what()));
    }
    
    it->second.enabled = false;
    
    juce::Logger::writeToLog("[MidiDeviceManager] Disabled device: " + it->second.name);
}

void MidiDeviceManager::enableAllDevices()
{
    for (const auto& [identifier, deviceInfo] : devices)
    {
        if (!deviceInfo.enabled)
        {
            enableDevice(identifier);
        }
    }
    
    juce::Logger::writeToLog("[MidiDeviceManager] All devices enabled");
}

void MidiDeviceManager::disableAllDevices()
{
    for (const auto& [identifier, deviceInfo] : devices)
    {
        if (deviceInfo.enabled)
        {
            disableDevice(identifier);
        }
    }
    
    juce::Logger::writeToLog("[MidiDeviceManager] All devices disabled");
}

//==============================================================================
// Device Information
//==============================================================================

std::vector<MidiDeviceManager::DeviceInfo> MidiDeviceManager::getAvailableDevices() const
{
    std::vector<DeviceInfo> result;
    result.reserve(devices.size());
    
    for (const auto& [identifier, deviceInfo] : devices)
    {
        result.push_back(deviceInfo);
    }
    
    // Sort by device index for consistent ordering
    std::sort(result.begin(), result.end(), 
        [](const DeviceInfo& a, const DeviceInfo& b) {
            return a.deviceIndex < b.deviceIndex;
        });
    
    return result;
}

std::vector<MidiDeviceManager::DeviceInfo> MidiDeviceManager::getEnabledDevices() const
{
    std::vector<DeviceInfo> result;
    
    for (const auto& [identifier, deviceInfo] : devices)
    {
        if (deviceInfo.enabled)
        {
            result.push_back(deviceInfo);
        }
    }
    
    // Sort by device index
    std::sort(result.begin(), result.end(), 
        [](const DeviceInfo& a, const DeviceInfo& b) {
            return a.deviceIndex < b.deviceIndex;
        });
    
    return result;
}

MidiDeviceManager::DeviceInfo MidiDeviceManager::getDeviceInfo(const juce::String& identifier) const
{
    auto it = devices.find(identifier);
    if (it != devices.end())
    {
        return it->second;
    }
    
    // Return empty info if not found
    return DeviceInfo();
}

bool MidiDeviceManager::isDeviceEnabled(const juce::String& identifier) const
{
    auto it = devices.find(identifier);
    if (it != devices.end())
    {
        return it->second.enabled;
    }
    return false;
}

//==============================================================================
// Message Buffer Access
//==============================================================================

void MidiDeviceManager::swapMessageBuffer(std::vector<MidiMessageWithSource>& targetBuffer)
{
    const juce::ScopedLock lock(bufferLock);
    targetBuffer.swap(messageBuffer);
    messageBuffer.clear();
}

std::map<int, MidiDeviceManager::ActivityInfo> MidiDeviceManager::getActivitySnapshot() const
{
    const juce::ScopedLock lock(activityLock);
    return activityMap;
}

void MidiDeviceManager::clearActivityHistory()
{
    const juce::ScopedLock lock(activityLock);
    activityMap.clear();
    currentFrame = 0;
}

//==============================================================================
// MidiInputCallback implementation
//==============================================================================

void MidiDeviceManager::handleIncomingMidiMessage(juce::MidiInput* source, 
                                                  const juce::MidiMessage& message)
{
    if (source == nullptr)
        return;
    
    // Get device information
    juce::String sourceIdentifier = source->getIdentifier();
    
    auto it = devices.find(sourceIdentifier);
    if (it == devices.end())
        return;  // Unknown device
    
    // Create message with source info
    MidiMessageWithSource msgWithSource;
    msgWithSource.message = message;
    msgWithSource.deviceIdentifier = it->second.identifier;
    msgWithSource.deviceName = it->second.name;
    msgWithSource.deviceIndex = it->second.deviceIndex;
    msgWithSource.timestamp = juce::Time::getMillisecondCounterHiRes();
    
    // Add to buffer (thread-safe)
    {
        const juce::ScopedLock lock(bufferLock);
        messageBuffer.push_back(msgWithSource);
        
        // Limit buffer size to prevent memory growth
        if (messageBuffer.size() > 1000)
        {
            messageBuffer.erase(messageBuffer.begin());
        }
    }
    
    // Update activity tracking
    updateActivityTracking(msgWithSource);
}

//==============================================================================
// Timer implementation (hot-plug detection)
//==============================================================================

void MidiDeviceManager::timerCallback()
{
    // Get current device list
    auto currentDevices = juce::MidiInput::getAvailableDevices();
    juce::StringArray currentNames;
    for (const auto& device : currentDevices)
        currentNames.add(device.name);
    
    // Check if the list has changed
    if (currentNames != lastDeviceList)
    {
        juce::Logger::writeToLog("[MidiDeviceManager] Device list changed - rescanning");
        scanDevices();
        lastDeviceList = currentNames;
    }
    
    // Increment frame counter for activity fade-out
    {
        const juce::ScopedLock lock(activityLock);
        currentFrame++;
    }
}

//==============================================================================
// Internal helper methods
//==============================================================================

void MidiDeviceManager::updateActivityTracking(const MidiMessageWithSource& msg)
{
    // Skip system realtime messages for activity tracking
    if (msg.message.isMidiClock() || msg.message.isActiveSense())
        return;
    
    const juce::ScopedLock lock(activityLock);
    
    // Get or create activity info for this device
    ActivityInfo& activity = activityMap[msg.deviceIndex];
    activity.deviceName = msg.deviceName;
    activity.deviceIndex = msg.deviceIndex;
    activity.lastActivityFrame = currentFrame;
    
    // Track activity by type and channel
    int channel = msg.message.getChannel();
    if (channel >= 1 && channel <= 16)
    {
        int channelIndex = channel - 1;  // 0-15
        
        if (msg.message.isNoteOn() || msg.message.isNoteOff())
        {
            activity.hasNoteActivity[channelIndex] = true;
        }
        else if (msg.message.isController())
        {
            activity.hasCCActivity[channelIndex] = true;
        }
        else if (msg.message.isPitchWheel())
        {
            activity.hasPitchBendActivity[channelIndex] = true;
        }
    }
}

int MidiDeviceManager::getDeviceIndexByIdentifier(const juce::String& identifier) const
{
    auto it = devices.find(identifier);
    if (it != devices.end())
    {
        return it->second.deviceIndex;
    }
    return -1;
}

