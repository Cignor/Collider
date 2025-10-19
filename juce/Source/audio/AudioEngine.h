#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_core/juce_core.h>
#include "assets/SampleBank.h"
#include "graph/VoiceProcessor.h"
#include "fx/GainProcessor.h"
#include "voices/SampleVoiceProcessor.h"
#include "../ipc/CommandBus.h"
#include "../ipc/IpcServer.h"
#include "../ipc/OscClient.h"
#include "../ui/DebugInfo.h"

class AudioEngine : public juce::AudioSource, private juce::Timer
{
public:
    AudioEngine(juce::AudioDeviceManager& adm);
    ~AudioEngine() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    VisualiserState getVisualiserState() const;
    void setMasterGain (float newGain);
    void dumpCurrentStateToLog();
    
    // Access to command bus for UI communication
    CommandBus& getCommandBus() { return commandBus; }
    
    // Get active voices info for UI
    struct VoiceInfo {
        juce::uint64 voiceId;
        juce::String voiceType;
        juce::String displayName;
    };
    juce::Array<VoiceInfo> getActiveVoicesInfo() const;
    float getVoiceParameterValue(juce::uint64 voiceId, const juce::String& paramName) const;
    
    // Get available audio input channel names from the current device
    juce::StringArray getAvailableInputChannelNames() const;
    
    // Get the name of the current input device
    juce::String getCurrentInputDeviceName() const;
    
    // Get list of all available input devices
    juce::StringArray getAvailableInputDeviceNames() const;
    
    // Change the global input device for the application
    void setInputDevice(const juce::String& deviceName);

    // --- Test-harness direct control API (bypasses OSC/CommandBus) ---
    // Create a voice directly and wire it to the master gain node. Returns new voiceId (or 0 on failure).
    juce::uint64 test_createVoice (const juce::String& voiceType);
    // Update an APVTS-mapped parameter on a specific voice (e.g., "gain", "pan").
    void test_updateVoiceParameter (juce::uint64 voiceId, const juce::String& paramId, float value);
    // Destroy a voice immediately.
    void test_destroyVoice (juce::uint64 voiceId);

private:
    void timerCallback() override;

    void handleCreateVoice (const Command& cmd);
    void handleDestroyVoice (juce::uint64 voiceId);
    void handleUpdateParam (const Command& cmd);
    void handleListenerUpdate (float x, float y, float radius, float nearRatio);
    void sendFullInfoSnapshot();
    void resetVoiceParamsToDefaults (VoiceProcessor* v);

    juce::AudioProcessorGraph::Node::Ptr connectAndAddVoice (std::unique_ptr<juce::AudioProcessor> processor);

    juce::AudioDeviceManager& deviceManager;
    CommandBus commandBus;
    IpcServer oscServer;
    OscClient oscClient;
    SampleBank sampleBank;

    using Node = juce::AudioProcessorGraph::Node;
    std::unique_ptr<juce::AudioProcessorGraph> mainGraph;
    Node::Ptr audioOutputNode;
    Node::Ptr masterGainNode;
    std::map<juce::uint64, Node::Ptr> activeVoices;
    std::map<juce::uint64, std::shared_ptr<SampleBank::Sample>> activeSampleRefs;

    // Runtime format
    double lastSampleRate { 0.0 };
    int lastBlockSize { 0 };
    bool chaosModeEnabled { false };

    // Lightweight logger and stats for the harness UI
    mutable juce::CriticalSection logLock;
    juce::StringArray logQueue;
    std::atomic<float> lastOutputPeak { 0.0f };

    // Reusable realtime scratch buffers to avoid per-callback allocations
    juce::AudioBuffer<float> tmpGraphBuffer;
    juce::AudioBuffer<float> tmpVoiceBuffer;

public:
    void appendLog (const juce::String& msg)
    {
        const juce::ScopedLock sl (logLock);
        logQueue.add ("[" + juce::Time::getCurrentTime().toString (true, true) + "] " + msg);
    }

    juce::StringArray drainLogs()
    {
        const juce::ScopedLock sl (logLock);
        juce::StringArray out;
        out.swapWith (logQueue);
        return out;
    }

    struct RuntimeStats { double sampleRate; int blockSize; int nodeCount; float masterGain; float lastPeak; int voiceCount; };
    RuntimeStats getRuntimeStats() const
    {
        RuntimeStats rs{};
        rs.sampleRate = lastSampleRate;
        rs.blockSize = lastBlockSize;
        rs.nodeCount = (int) (mainGraph ? mainGraph->getNodes().size() : 0);
        rs.lastPeak = lastOutputPeak.load();
        rs.voiceCount = (int) activeVoices.size();
        float g = 1.0f;
        if (masterGainNode != nullptr)
        {
            if (auto* gp = dynamic_cast<GainProcessor*> (masterGainNode->getProcessor()))
            {
                if (auto* p = gp->getAPVTS().getRawParameterValue ("gain"))
                    g = p->load();
            }
        }
        rs.masterGain = g;
        return rs;
    }

    float listenerX { 0.0f }, listenerY { 0.0f }, listenerRadius { 300.0f }, listenerNear { 0.12f };
    double clockOffsetMs { 0.0 };
    bool clockSynced { false };

    mutable juce::CriticalSection visualiserLock;
    VisualiserState visualiserState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
}; 
