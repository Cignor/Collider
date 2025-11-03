#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>

// This class is now self-contained and does not need forward declarations.

class SavePresetJob : public juce::ThreadPoolJob
{
public:
    // The job now takes the already-prepared state data by value.
    SavePresetJob(juce::MemoryBlock synthState, juce::ValueTree uiState, juce::File targetFile);

    juce::ThreadPoolJob::JobStatus runJob() override;

    std::function<void(const juce::File&, bool success)> onSaveComplete;

private:
    juce::MemoryBlock synthState;
    juce::ValueTree uiState;
    juce::File fileToSave;
};
