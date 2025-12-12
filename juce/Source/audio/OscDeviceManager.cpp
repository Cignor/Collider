#include "OscDeviceManager.h"
#include <algorithm>

//==============================================================================
// PortListener implementation
//==============================================================================

void OscDeviceManager::PortListener::oscMessageReceived(const juce::OSCMessage& message)
{
    if (owner != nullptr)
        owner->handleOscMessage(message, sourcePort);
}

void OscDeviceManager::PortListener::oscBundleReceived(const juce::OSCBundle& bundle)
{
    if (owner != nullptr)
        owner->handleOscBundle(bundle, sourcePort);
}

//==============================================================================
OscDeviceManager::OscDeviceManager()
{
    juce::Logger::writeToLog("[OscDeviceManager] Initialized");
}

OscDeviceManager::~OscDeviceManager()
{
    // Clean up all OSC receivers
    for (const auto& [port, wrapper] : receivers)
    {
        if (wrapper.receiver != nullptr)
        {
            wrapper.receiver->disconnect();
        }
    }
    receivers.clear();
    
    juce::Logger::writeToLog("[OscDeviceManager] Shut down");
}

//==============================================================================
// Device Management
//==============================================================================

void OscDeviceManager::scanDevices()
{
    // TODO: In the future, load saved devices from application properties or preset files
    // For now, this is a placeholder that does nothing
    // Devices are added manually via addDevice()
    
    juce::Logger::writeToLog("[OscDeviceManager] Scan complete. Total devices: " + 
                            juce::String(devices.size()));
}

juce::String OscDeviceManager::addDevice(const juce::String& name, int port)
{
    juce::String identifier = createIdentifier(port);
    
    // Check if device with this identifier already exists
    if (devices.find(identifier) != devices.end())
    {
        juce::Logger::writeToLog("[OscDeviceManager] Device already exists: " + identifier);
        return identifier;
    }
    
    // Check if port is already in use
    if (receivers.find(port) != receivers.end())
    {
        juce::Logger::writeToLog("[OscDeviceManager] Port " + juce::String(port) + " is already in use");
        return juce::String();
    }
    
    // Create device info
    DeviceInfo info;
    info.identifier = identifier;
    info.name = name;
    info.port = port;
    info.enabled = false;
    info.deviceIndex = nextDeviceIndex++;
    
    // Create OSC receiver for this port
    ReceiverWrapper wrapper;
    wrapper.receiver = std::make_unique<juce::OSCReceiver>();
    bool connected = wrapper.receiver->connect(port);
    
    if (!connected)
    {
        juce::Logger::writeToLog("[OscDeviceManager] Failed to bind to port " + juce::String(port) + 
                                " (port may be in use)");
        return juce::String();
    }
    
    // Create port-specific listener
    wrapper.listener = std::make_unique<PortListener>(this, port);
    wrapper.receiver->addListener(wrapper.listener.get());
    
    // Store receiver wrapper and device info
    receivers[port] = std::move(wrapper);
    portToIdentifier[port] = identifier;
    devices[identifier] = info;
    
    juce::Logger::writeToLog("[OscDeviceManager] Added device: " + name + 
                            " on port " + juce::String(port) + 
                            " (index " + juce::String(info.deviceIndex) + ")");
    
    return identifier;
}

void OscDeviceManager::removeDevice(const juce::String& identifier)
{
    auto it = devices.find(identifier);
    if (it == devices.end())
    {
        juce::Logger::writeToLog("[OscDeviceManager] Cannot remove unknown device: " + identifier);
        return;
    }
    
    int port = it->second.port;
    
    // Disable if currently enabled
    if (it->second.enabled)
    {
        disableDevice(identifier);
    }
    
    // Remove receiver
    auto receiverIt = receivers.find(port);
    if (receiverIt != receivers.end())
    {
        if (receiverIt->second.receiver != nullptr)
        {
            receiverIt->second.receiver->disconnect();
        }
        receivers.erase(receiverIt);
    }
    
    // Remove port mapping
    portToIdentifier.erase(port);
    
    // Remove device
    devices.erase(it);
    
    juce::Logger::writeToLog("[OscDeviceManager] Removed device: " + identifier);
}

void OscDeviceManager::enableDevice(const juce::String& identifier)
{
    auto it = devices.find(identifier);
    if (it == devices.end())
    {
        juce::Logger::writeToLog("[OscDeviceManager] Cannot enable unknown device: " + identifier);
        return;
    }
    
    if (it->second.enabled)
    {
        // Already enabled
        return;
    }
    
    int port = it->second.port;
    auto receiverIt = receivers.find(port);
    
    if (receiverIt == receivers.end() || receiverIt->second.receiver == nullptr)
    {
        // Receiver doesn't exist, try to create it
        ReceiverWrapper wrapper;
        wrapper.receiver = std::make_unique<juce::OSCReceiver>();
        bool connected = wrapper.receiver->connect(port);
        
        if (!connected)
        {
            juce::Logger::writeToLog("[OscDeviceManager] Failed to bind to port " + juce::String(port) + 
                                    " when enabling device");
            return;
        }
        
        wrapper.listener = std::make_unique<PortListener>(this, port);
        wrapper.receiver->addListener(wrapper.listener.get());
        receivers[port] = std::move(wrapper);
    }
    
    it->second.enabled = true;
    
    juce::Logger::writeToLog("[OscDeviceManager] Enabled device: " + it->second.name);
}

void OscDeviceManager::disableDevice(const juce::String& identifier)
{
    auto it = devices.find(identifier);
    if (it == devices.end())
    {
        juce::Logger::writeToLog("[OscDeviceManager] Cannot disable unknown device: " + identifier);
        return;
    }
    
    if (!it->second.enabled)
    {
        // Already disabled
        return;
    }
    
    it->second.enabled = false;
    
    // Note: We don't disconnect the receiver here, as it might be needed for re-enabling
    // The receiver stays connected but won't receive messages (or we could remove listener)
    
    juce::Logger::writeToLog("[OscDeviceManager] Disabled device: " + it->second.name);
}

//==============================================================================
// Device Information
//==============================================================================

std::vector<OscDeviceManager::DeviceInfo> OscDeviceManager::getAvailableDevices() const
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

std::vector<OscDeviceManager::DeviceInfo> OscDeviceManager::getEnabledDevices() const
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

OscDeviceManager::DeviceInfo OscDeviceManager::getDeviceInfo(const juce::String& identifier) const
{
    auto it = devices.find(identifier);
    if (it != devices.end())
    {
        return it->second;
    }
    
    // Return empty info if not found
    return DeviceInfo();
}

bool OscDeviceManager::isDeviceEnabled(const juce::String& identifier) const
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

void OscDeviceManager::swapMessageBuffer(std::vector<OscMessageWithSource>& targetBuffer)
{
    const juce::ScopedLock lock(bufferLock);
    targetBuffer.swap(messageBuffer);
    messageBuffer.clear();
    // Removed verbose logging - messages are being received successfully
}

std::map<int, OscDeviceManager::ActivityInfo> OscDeviceManager::getActivitySnapshot() const
{
    const juce::ScopedLock lock(activityLock);
    return activityMap;
}

OscDeviceManager::ActivityInfo OscDeviceManager::getDeviceActivity(const juce::String& identifier) const
{
    const juce::ScopedLock lock(activityLock);
    
    int deviceIndex = getDeviceIndexByIdentifier(identifier);
    if (deviceIndex >= 0)
    {
        auto it = activityMap.find(deviceIndex);
        if (it != activityMap.end())
        {
            return it->second;
        }
    }
    
    return ActivityInfo();
}

void OscDeviceManager::clearActivityHistory()
{
    const juce::ScopedLock lock(activityLock);
    activityMap.clear();
    currentFrame = 0;
}

//==============================================================================
// OSC Message Handlers (called by PortListener)
//==============================================================================

void OscDeviceManager::handleOscMessage(const juce::OSCMessage& message, int sourcePort)
{
    // Create message with source info
    juce::String identifier = createIdentifier(sourcePort);
    
    // Construct using the constructor that takes an OSCMessage
    OscMessageWithSource msgWithSource(message);
    msgWithSource.sourceIdentifier = identifier;
    
    auto deviceIt = devices.find(identifier);
    if (deviceIt != devices.end())
    {
        msgWithSource.sourceName = deviceIt->second.name;
        msgWithSource.deviceIndex = deviceIt->second.deviceIndex;
    }
    else
    {
        msgWithSource.sourceName = "Unknown";
        msgWithSource.deviceIndex = -1;
    }
    
    msgWithSource.timestamp = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    
    // Add to buffer (thread-safe)
    {
        const juce::ScopedLock lock(bufferLock);
        messageBuffer.push_back(msgWithSource);
        
        // Limit buffer size to prevent memory growth
        if (messageBuffer.size() > 1000)
        {
            messageBuffer.erase(messageBuffer.begin());
            static int overflowCount = 0;
            if (++overflowCount % 100 == 0) // Log overflow every 100 times
            {
                juce::Logger::writeToLog("[OscDeviceManager] WARNING: OSC buffer overflow (dropped " + juce::String(overflowCount) + " messages)");
            }
        }
    }
    
    // Update activity tracking
    updateActivityTracking(msgWithSource);
}

void OscDeviceManager::handleOscBundle(const juce::OSCBundle& bundle, int sourcePort)
{
    // Unpack bundle and process each message
    for (const auto& element : bundle)
    {
        if (element.isMessage())
        {
            handleOscMessage(element.getMessage(), sourcePort);
        }
        else if (element.isBundle())
        {
            // Recursive bundle (not common, but handle it)
            handleOscBundle(element.getBundle(), sourcePort);
        }
    }
}

//==============================================================================
// Internal helper methods
//==============================================================================

void OscDeviceManager::updateActivityTracking(const OscMessageWithSource& msg)
{
    const juce::ScopedLock lock(activityLock);
    
    currentFrame++;
    
    int deviceIndex = msg.deviceIndex;
    if (deviceIndex < 0)
        return;
    
    ActivityInfo& info = activityMap[deviceIndex];
    info.sourceName = msg.sourceName;
    info.deviceIndex = deviceIndex;
    info.lastAddress = msg.message.getAddressPattern().toString();
    info.lastActivityFrame = currentFrame;
    info.lastMessageTime = juce::Time::getMillisecondCounterHiRes();
}

int OscDeviceManager::getDeviceIndexByIdentifier(const juce::String& identifier) const
{
    auto it = devices.find(identifier);
    if (it != devices.end())
    {
        return it->second.deviceIndex;
    }
    return -1;
}

int OscDeviceManager::getDeviceIndexByPort(int port) const
{
    auto identifierIt = portToIdentifier.find(port);
    if (identifierIt != portToIdentifier.end())
    {
        return getDeviceIndexByIdentifier(identifierIt->second);
    }
    return -1;
}

juce::String OscDeviceManager::createIdentifier(int port) const
{
    // For now, use "localhost:port" as identifier
    // In the future, could use actual IP address if we can determine it
    return "localhost:" + juce::String(port);
}

