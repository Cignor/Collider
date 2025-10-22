// Rationale: CommandBus is the single-producer/multi-consumer queue between OSC
// receiver and the engine. We provide enqueueLatest to coalesce noisy updates
// (e.g., positionX/Y) and a hard size cap to avoid unbounded growth under load.
#pragma once

#include <juce_core/juce_core.h>
#include <limits>

struct Command
{
    enum class Type { Create, Destroy, Update, DebugDump, LoadPreset, ResetFx, RandomizePitch, RandomizeTime, SetChaosMode, LoadPatchState };
    Type type { Type::Update };
    juce::uint64 voiceId { 0 };
    // Create
    juce::String voiceType;    // "sample", "synth", "noise"
    juce::String resourceName; // file name or preset name
    // Optional initial parameters for Create
    float initialPosX { std::numeric_limits<float>::quiet_NaN() };
    float initialPosY { std::numeric_limits<float>::quiet_NaN() };
    float initialAmplitude { std::numeric_limits<float>::quiet_NaN() };
    int   initialPitchOnGrid { -1 }; // -1 unknown, 0 false, 1 true
    int   initialLooping     { -1 }; // -1 unknown, 0 false, 1 true
    float initialVolume      { std::numeric_limits<float>::quiet_NaN() };
    juce::String presetData; // For LoadPreset command
    // Update
    juce::String paramName;    // e.g. "pan", "gain", "cutoff"
    float paramValue { 0.0f };
    // Chaos Mode
    bool chaosModeEnabled { false };
    // Patch State (for LoadPatchState command from Snapshot Sequencer)
    juce::MemoryBlock patchState;
};

class CommandBus
{
public:
    void enqueue (const Command& c)
    {
        const juce::ScopedLock sl (lock);
        queue.add (c);
        trimIfOversize();
    }

    bool tryDequeue (Command& out)
    {
        const juce::ScopedLock sl (lock);
        if (queue.isEmpty()) return false;
        out = queue.removeAndReturn (0);
        return true;
    }

    // Replace any existing queued update with the same (voiceId, paramName)
    void enqueueLatest (const Command& c)
    {
        const juce::ScopedLock sl (lock);
        if (c.type == Command::Type::Update)
        {
            for (int i = queue.size(); --i >= 0; )
            {
                const auto& q = queue.getReference (i);
                if (q.type == Command::Type::Update && q.voiceId == c.voiceId && q.paramName == c.paramName)
                {
                    queue.remove (i);
                    break;
                }
            }
        }
        queue.add (c);
        trimIfOversize();
    }

    int getSize() const
    {
        const juce::ScopedLock sl (lock);
        return queue.size();
    }

    void clear()
    {
        const juce::ScopedLock sl (lock);
        queue.clear();
    }

private:
    juce::CriticalSection lock;
    juce::Array<Command> queue;

    void trimIfOversize()
    {
        const int maxSize = 20000; // safety cap
        if (queue.size() > maxSize)
        {
            const int excess = queue.size() - maxSize;
            queue.removeRange (0, excess);
        }
    }
};


