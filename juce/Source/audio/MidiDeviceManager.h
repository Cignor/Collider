#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <memory>

/**
 * @brief Central manager for multiple MIDI input devices
 * 
 * This class handles:
 * - Scanning and enumerating all available MIDI devices
 * - Enabling/disabling multiple devices simultaneously
 * - Tracking device information (name, identifier, enabled state)
 * - Buffering MIDI messages with device source information
 * - Activity monitoring for UI visualization
 * - Hot-plug detection
 * 
 * Thread Safety:
 * - MIDI callbacks run on MIDI thread
 * - Message buffering uses CriticalSection protection
 * - Activity tracking uses atomic operations where possible
 */
class MidiDeviceManager : public juce::MidiInputCallback,
                          public juce::Timer
{
public:
    /**
     * @brief Information about a MIDI device
     */
    struct DeviceInfo {
        juce::String identifier;    // Unique device ID from JUCE
        juce::String name;           // Human-readable device name
        bool enabled = false;        // Is this device currently enabled?
        int deviceIndex = -1;        // Sequential index for this device
    };
    
    /**
     * @brief MIDI message with device source information
     */
    struct MidiMessageWithSource {
        juce::MidiMessage message;
        juce::String deviceIdentifier;
        juce::String deviceName;
        int deviceIndex;
        double timestamp;            // Time when message was received
    };
    
    /**
     * @brief Activity tracking for a MIDI device and its channels
     */
    struct ActivityInfo {
        juce::String deviceName;
        int deviceIndex;
        bool hasNoteActivity[16] = {false};        // Per-channel note activity
        bool hasCCActivity[16] = {false};          // Per-channel CC activity
        bool hasPitchBendActivity[16] = {false};   // Per-channel pitch bend activity
        juce::uint32 lastActivityFrame = 0;        // Frame counter for fade-out
        juce::uint64 lastMessageTime = 0;          // Timestamp of last message (milliseconds)
    };
    
    //==============================================================================
    /**
     * @brief Construct a new MIDI Device Manager
     * @param adm Reference to the shared AudioDeviceManager
     */
    explicit MidiDeviceManager(juce::AudioDeviceManager& adm);
    
    /**
     * @brief Destructor - cleans up all device callbacks
     */
    ~MidiDeviceManager() override;
    
    //==============================================================================
    // Device Management
    //==============================================================================
    
    /**
     * @brief Scan for available MIDI devices and update internal list
     * 
     * This should be called on startup and whenever you want to refresh
     * the device list (e.g., after hot-plugging).
     */
    void scanDevices();
    
    /**
     * @brief Enable a specific MIDI device
     * @param identifier The device's unique identifier
     */
    void enableDevice(const juce::String& identifier);
    
    /**
     * @brief Disable a specific MIDI device
     * @param identifier The device's unique identifier
     */
    void disableDevice(const juce::String& identifier);
    
    /**
     * @brief Enable all available MIDI devices
     */
    void enableAllDevices();
    
    /**
     * @brief Disable all MIDI devices
     */
    void disableAllDevices();
    
    //==============================================================================
    // Device Information
    //==============================================================================
    
    /**
     * @brief Get list of all available MIDI devices
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
     * This is a thread-safe way to retrieve all buffered MIDI messages.
     * The internal buffer is cleared after the swap.
     * 
     * @param targetBuffer Buffer to receive the messages
     */
    void swapMessageBuffer(std::vector<MidiMessageWithSource>& targetBuffer);
    
    /**
     * @brief Get a snapshot of current MIDI activity
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
    // MidiInputCallback implementation
    //==============================================================================
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                   const juce::MidiMessage& message) override;
    
    //==============================================================================
    // Timer implementation (for hot-plug detection)
    //==============================================================================
    void timerCallback() override;
    
    //==============================================================================
    // Internal helper methods
    //==============================================================================
    void updateActivityTracking(const MidiMessageWithSource& msg);
    int getDeviceIndexByIdentifier(const juce::String& identifier) const;
    
    //==============================================================================
    // Member variables
    //==============================================================================
    
    // Reference to the application's AudioDeviceManager
    juce::AudioDeviceManager& deviceManager;
    
    // Device tracking
    std::map<juce::String, DeviceInfo> devices;  // identifier -> DeviceInfo
    int nextDeviceIndex = 0;
    
    // Message buffering (protected by bufferLock)
    juce::CriticalSection bufferLock;
    std::vector<MidiMessageWithSource> messageBuffer;
    
    // Activity tracking (protected by activityLock)
    juce::CriticalSection activityLock;
    std::map<int, ActivityInfo> activityMap;  // deviceIndex -> ActivityInfo
    juce::uint32 currentFrame = 0;             // Frame counter for fade-out
    
    // Hot-plug detection
    juce::StringArray lastDeviceList;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDeviceManager)
};

