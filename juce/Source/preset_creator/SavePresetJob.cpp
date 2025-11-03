#include "SavePresetJob.h"
#include <juce_events/juce_events.h> // For MessageManager
#include <juce_core/juce_core.h>

SavePresetJob::SavePresetJob(juce::MemoryBlock synthStateData, juce::ValueTree uiStateData, juce::File targetFile)
    : juce::ThreadPoolJob("Save Preset To Disk"), 
      synthState(std::move(synthStateData)), 
      uiState(std::move(uiStateData)), 
      fileToSave(std::move(targetFile))
{
    juce::Logger::writeToLog("[SavePresetJob] Created job for: " + fileToSave.getFullPathName());
}

juce::ThreadPoolJob::JobStatus SavePresetJob::runJob()
{
    juce::Logger::writeToLog("[SavePresetJob] runJob() started on background thread for: " + fileToSave.getFullPathName());
    
    bool writeSuccess = false;
    juce::String errorMessage;
    
    try
    {
        // This entire function runs safely on a background thread.
        // It has no access to the synth or editor, only the data it was given.
        
        juce::Logger::writeToLog("[SavePresetJob] Parsing synth state XML (size: " + juce::String(synthState.getSize()) + " bytes)");
        auto xml = juce::XmlDocument::parse(synthState.toString());
        
        if (!xml)
        {
            errorMessage = "Failed to parse synth state XML";
            juce::Logger::writeToLog("[SavePresetJob] ERROR: " + errorMessage);
        }
        else
        {
            juce::Logger::writeToLog("[SavePresetJob] Creating preset ValueTree from XML");
            auto presetVT = juce::ValueTree::fromXml(*xml);
            
            if (!presetVT.isValid())
            {
                errorMessage = "Failed to create ValueTree from XML";
                juce::Logger::writeToLog("[SavePresetJob] ERROR: " + errorMessage);
            }
            else
            {
                juce::Logger::writeToLog("[SavePresetJob] Adding UI state to preset (UI state valid: " + juce::String(uiState.isValid() ? 1 : 0) + ")");
                presetVT.addChild(uiState, -1, nullptr);
                
                juce::Logger::writeToLog("[SavePresetJob] Converting preset to XML string");
                auto finalXml = presetVT.createXml();
                
                if (!finalXml)
                {
                    errorMessage = "Failed to create XML from preset ValueTree";
                    juce::Logger::writeToLog("[SavePresetJob] ERROR: " + errorMessage);
                }
                else
                {
                    juce::Logger::writeToLog("[SavePresetJob] Writing to file: " + fileToSave.getFullPathName());
                    
                    // Ensure parent directory exists
                    auto parentDir = fileToSave.getParentDirectory();
                    if (!parentDir.exists())
                    {
                        juce::Logger::writeToLog("[SavePresetJob] Creating parent directory: " + parentDir.getFullPathName());
                        auto dirResult = parentDir.createDirectory();
                        if (!dirResult.wasOk())
                        {
                            errorMessage = "Failed to create parent directory: " + dirResult.getErrorMessage();
                            juce::Logger::writeToLog("[SavePresetJob] ERROR: " + errorMessage);
                        }
                    }
                    
                    if (errorMessage.isEmpty())
                    {
                        // This is the only slow part, and it's safely on a background thread.
                        writeSuccess = fileToSave.replaceWithText(finalXml->toString());
                        
                        if (writeSuccess)
                        {
                            juce::Logger::writeToLog("[SavePresetJob] SUCCESS: File written successfully (" + juce::String(fileToSave.getSize()) + " bytes)");
                        }
                        else
                        {
                            errorMessage = "File write operation returned false";
                            juce::Logger::writeToLog("[SavePresetJob] ERROR: " + errorMessage);
                        }
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        errorMessage = "Exception in runJob(): " + juce::String(e.what());
        juce::Logger::writeToLog("[SavePresetJob] EXCEPTION: " + errorMessage);
        writeSuccess = false;
    }
    catch (...)
    {
        errorMessage = "Unknown exception in runJob()";
        juce::Logger::writeToLog("[SavePresetJob] EXCEPTION: " + errorMessage);
        writeSuccess = false;
    }

    // ALWAYS signal completion back to the UI thread, even on error
    juce::Logger::writeToLog("[SavePresetJob] Signaling completion (success: " + juce::String(writeSuccess ? 1 : 0) + ")");
    
    // CRITICAL FIX: Capture the callback by value BEFORE callAsync
    // The job object may be destroyed by the thread pool before callAsync executes,
    // so we must capture the callback function itself, not 'this'
    auto callback = onSaveComplete;
    auto targetFile = fileToSave;
    bool callbackSet = (callback != nullptr);
    juce::Logger::writeToLog("[SavePresetJob] Callback captured: " + juce::String(callbackSet ? 1 : 0));
    
    juce::MessageManager::callAsync([callback, targetFile, success = writeSuccess, error = errorMessage]() {
        juce::Logger::writeToLog("[SavePresetJob] Callback executing on UI thread (success: " + juce::String(success ? 1 : 0) + ")");
        if (callback) 
        {
            callback(targetFile, success);
            juce::Logger::writeToLog("[SavePresetJob] Callback completed successfully");
        }
        else
        {
            juce::Logger::writeToLog("[SavePresetJob] ERROR: Callback was null when executing!");
        }
    });
    
    juce::Logger::writeToLog("[SavePresetJob] runJob() finished, returning jobHasFinished");
    return juce::ThreadPoolJob::jobHasFinished;
}
