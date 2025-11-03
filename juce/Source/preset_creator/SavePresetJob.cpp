#include "SavePresetJob.h"
#include <juce_events/juce_events.h> // For MessageManager

SavePresetJob::SavePresetJob(juce::MemoryBlock synthStateData, juce::ValueTree uiStateData, juce::File targetFile)
    : juce::ThreadPoolJob("Save Preset To Disk"), 
      synthState(std::move(synthStateData)), 
      uiState(std::move(uiStateData)), 
      fileToSave(std::move(targetFile))
{}

juce::ThreadPoolJob::JobStatus SavePresetJob::runJob()
{
    // This entire function runs safely on a background thread.
    // It has no access to the synth or editor, only the data it was given.
    
    auto xml = juce::XmlDocument::parse(synthState.toString());
    bool writeSuccess = false;

    if (xml)
    {
        auto presetVT = juce::ValueTree::fromXml(*xml);
        presetVT.addChild(uiState, -1, nullptr);
        
        // This is the only slow part, and it's safely on a background thread.
        writeSuccess = fileToSave.replaceWithText(presetVT.createXml()->toString());
    }

    // Signal completion back to the UI thread.
    juce::MessageManager::callAsync([this, success = writeSuccess]() {
        if (onSaveComplete) onSaveComplete(fileToSave, success);
    });
    
    return juce::ThreadPoolJob::jobHasFinished;
}
