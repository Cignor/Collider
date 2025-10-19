#pragma once

#include <juce_core/juce_core.h>
#include "../graph/VoiceProcessor.h"

namespace VoiceDeletion
{
    inline void updateLastControlledAfterRemoval(juce::OwnedArray<VoiceProcessor>& activeVoices,
                                                 VoiceProcessor*& lastControlledVoice)
    {
        if (activeVoices.isEmpty())
            lastControlledVoice = nullptr;
        else
            lastControlledVoice = activeVoices.getLast();
    }

    inline bool destroyLastVoice(juce::OwnedArray<VoiceProcessor>& activeVoices,
                                 VoiceProcessor*& lastControlledVoice,
                                 juce::SpinLock& voicesLock)
    {
        const juce::SpinLock::ScopedLockType guard (voicesLock);
        if (lastControlledVoice == nullptr)
            return false;

        auto* v = lastControlledVoice;
        activeVoices.removeObject (v, true);
        updateLastControlledAfterRemoval (activeVoices, lastControlledVoice);
        return true;
    }

    inline bool destroyRandomVoice(juce::OwnedArray<VoiceProcessor>& activeVoices,
                                   VoiceProcessor*& lastControlledVoice,
                                   juce::SpinLock& voicesLock)
    {
        const juce::SpinLock::ScopedLockType guard (voicesLock);
        if (activeVoices.isEmpty())
            return false;

        auto& rng = juce::Random::getSystemRandom();
        const int index = rng.nextInt (activeVoices.size());
        auto* v = activeVoices[index];
        const bool wasLastControlled = (v == lastControlledVoice);

        activeVoices.remove (index, true);
        if (wasLastControlled)
            updateLastControlledAfterRemoval (activeVoices, lastControlledVoice);

        return true;
    }

    inline bool destroyByPointer(juce::OwnedArray<VoiceProcessor>& activeVoices,
                                 VoiceProcessor*& lastControlledVoice,
                                 juce::SpinLock& voicesLock,
                                 VoiceProcessor* voice)
    {
        if (voice == nullptr)
            return false;

        const juce::SpinLock::ScopedLockType guard (voicesLock);
        const bool wasLastControlled = (voice == lastControlledVoice);
        const int idx = activeVoices.indexOf (voice);
        if (idx < 0)
            return false;

        activeVoices.remove (idx, true);
        if (wasLastControlled)
            updateLastControlledAfterRemoval (activeVoices, lastControlledVoice);

        return true;
    }

    inline bool destroyByIndex(juce::OwnedArray<VoiceProcessor>& activeVoices,
                               VoiceProcessor*& lastControlledVoice,
                               juce::SpinLock& voicesLock,
                               int index)
    {
        const juce::SpinLock::ScopedLockType guard (voicesLock);
        if (index < 0 || index >= activeVoices.size())
            return false;

        auto* v = activeVoices[index];
        const bool wasLastControlled = (v == lastControlledVoice);
        activeVoices.remove (index, true);
        if (wasLastControlled)
            updateLastControlledAfterRemoval (activeVoices, lastControlledVoice);

        return true;
    }
}


