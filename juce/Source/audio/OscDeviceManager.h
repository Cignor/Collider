#pragma once

#include <juce_osc/juce_osc.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <memory>

/**
 * @brief Central manager for OSC (Open Sound Control) devices
 * 
 * This class handles:
 * - Managing multiple OSC receiver ports
 * - Enabling/disabling OSC devices independently
 * - Tracking device information (name, IP:port, enabled state)
 * - Buffering OSC messages with source information
 * - Activity monitoring for UI visualization
 * 
 * Thread Safety:
 * - OSC callbacks run on network thread
 * - Message buffering uses CriticalSection protection
 * - Activity tracking uses atomic operations where possible
 */
class OscDeviceManager
{
public:
    /**
     * @brief Information about an OSC device (listening port)
     */
    struct DeviceInfo {
        juce::String identifier;    // "IP:port" (e.g., "localhost:57120" or "192.168.1.100:8000")
        juce::String name;           // Human-readable device name (user-defined)
        int port = 57120;            // UDP receive port
        bool enabled = false;        // Is this device currently enabled?
        int deviceIndex = -1;        // Sequential index for this device
    };
    
    /**
     * @brief OSC message with source information
     * 
     * Note: This struct cannot be default-constructed because juce::OSCMessage
     * is not default-constructible. Use aggregate initialization or construct
     * with a valid OSCMessage.
     */
    struct OscMessageWithSource {
        juce::OSCMessage message;
        juce::String sourceIdentifier;  // "IP:port" (derived from port for now)
        juce::String sourceName;        // User-friendly name
        int deviceIndex = -1;
        double timestamp = 0.0;         // Time when message was received
        
        // Default constructor deleted - must use aggregate initialization
        OscMessageWithSource() = delete;
        
        // Constructor that takes an OSCMessage
        OscMessageWithSource(const juce::OSCMessage& msg)
            : message(msg), deviceIndex(-1), timestamp(0.0) {}
    };
    
    /**
     * @brief Activity tracking for an OSC device
     */
    struct ActivityInfo {
        juce::String sourceName;
        int deviceIndex;
        juce::String lastAddress;       // Last OSC address received
        juce::uint32 lastActivityFrame = 0;  // Frame counter for fade-out
        juce::uint64 lastMessageTime = 0;    // Timestamp of last message (milliseconds)
    };
    
    //==============================================================================
    /**
     * @brief Construct a new OSC Device Manager
     */
    OscDeviceManager();
    
    /**
     * @brief Destructor - cleans up all OSC receivers
     */
    ~OscDeviceManager();
    
    //==============================================================================
    // Device Management
    //==============================================================================
    
    /**
     * @brief Scan for configured OSC devices (load from config/persistent storage)
     * 
     * For now, this is a placeholder. In the future, this will load
     * saved devices from application properties or preset files.
     */
    void scanDevices();
    
    /**
     * @brief Add a new OSC device (receiver on specified port)
     * @param name Human-readable name for this device
     * @param port UDP port to listen on
     * @return The device identifier (IP:port format)
     */
    juce::String addDevice(const juce::String& name, int port);
    
    /**
     * @brief Remove an OSC device
     * @param identifier The device's unique identifier (IP:port)
     */
    void removeDevice(const juce::String& identifier);
    
    /**
     * @brief Enable a specific OSC device
     * @param identifier The device's unique identifier
     */
    void enableDevice(const juce::String& identifier);
    
    /**
     * @brief Disable a specific OSC device
     * @param identifier The device's unique identifier
     */
    void disableDevice(const juce::String& identifier);
    
    //==============================================================================
    // Device Information
    //==============================================================================
    
    /**
     * @brief Get list of all configured OSC devices
     * @return Vector of DeviceInfo structures
     */
    std::vector<DeviceInfo> getAvailableDevices() const;
    
    /**
     * @brief Alias for getAvailableDevices() - for UI convenience
     */
    std::vector<DeviceInfo> getDevices() const { return getAvailableDevices(); }
    
    /**
     * @brief Get list of currently enabled devices
     * @return Vector of DeviceInfo structures for enabled devices only
     */
    std::vector<DeviceInfo> getEnabledDevices() const;
    
    /**
     * @brief Get information about a specific device
     * @param identifier The device's unique identifier
     * @return DeviceInfo structure (empty if not found)
     */
    DeviceInfo getDeviceInfo(const juce::String& identifier) const;
    
    /**
     * @brief Check if a device is currently enabled
     * @param identifier The device's unique identifier
     * @return True if device is enabled, false otherwise
     */
    bool isDeviceEnabled(const juce::String& identifier) const;
    
    //==============================================================================
    // Message Buffer Access
    //==============================================================================
    
    /**
     * @brief Swap internal message buffer with provided buffer
     * 
     * This is a thread-safe way to retrieve all buffered OSC messages.
     * The internal buffer is cleared after the swap.
     * 
     * @param targetBuffer Buffer to receive the messages
     */
    void swapMessageBuffer(std::vector<OscMessageWithSource>& targetBuffer);
    
    /**
     * @brief Get a snapshot of current OSC activity
     * @return Map of device index to ActivityInfo
     */
    std::map<int, ActivityInfo> getActivitySnapshot() const;
    
    /**
     * @brief Get activity info for a specific device
     * @param identifier The device's unique identifier
     * @return ActivityInfo for the device (empty if not found)
     */
    ActivityInfo getDeviceActivity(const juce::String& identifier) const;
    
    /**
     * @brief Clear all activity history
     */
    void clearActivityHistory();
    
private:
    //==============================================================================
    // Internal port-specific listener wrapper
    //==============================================================================
    class PortListener : public juce::OSCReceiver::Listener<>
    {
    public:
        PortListener(OscDeviceManager* manager, int port)
            : owner(manager), sourcePort(port) {}
        
        void oscMessageReceived(const juce::OSCMessage& message) override;
        void oscBundleReceived(const juce::OSCBundle& bundle) override;
        
    private:
        OscDeviceManager* owner;
        int sourcePort;
    };
    
    //==============================================================================
    // Internal message handlers
    //==============================================================================
    void handleOscMessage(const juce::OSCMessage& message, int sourcePort);
    void handleOscBundle(const juce::OSCBundle& bundle, int sourcePort);
    
    //==============================================================================
    // Internal helper methods
    //==============================================================================
    void updateActivityTracking(const OscMessageWithSource& msg);
    int getDeviceIndexByIdentifier(const juce::String& identifier) const;
    int getDeviceIndexByPort(int port) const;
    juce::String createIdentifier(int port) const;
    
    //==============================================================================
    // Member variables
    //==============================================================================
    
    // Device tracking
    std::map<juce::String, DeviceInfo> devices;  // identifier -> DeviceInfo
    int nextDeviceIndex = 0;
    
    // Per-port OSCReceiver instances with listeners
    struct ReceiverWrapper
    {
        std::unique_ptr<juce::OSCReceiver> receiver;
        std::unique_ptr<PortListener> listener;
    };
    std::map<int, ReceiverWrapper> receivers;  // port -> ReceiverWrapper
    
    // Port to device identifier mapping
    std::map<int, juce::String> portToIdentifier;  // port -> identifier
    
    // Message buffering (protected by bufferLock)
    juce::CriticalSection bufferLock;
    std::vector<OscMessageWithSource> messageBuffer;
    
    // Activity tracking (protected by activityLock)
    juce::CriticalSection activityLock;
    std::map<int, ActivityInfo> activityMap;  // deviceIndex -> ActivityInfo
    juce::uint32 currentFrame = 0;             // Frame counter for fade-out
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscDeviceManager)
};

