#pragma once

#include "../modules/TTSPerformerModuleProcessor.h"
#include <juce_core/juce_core.h>
#include <vector>
#include <map>
#include <memory>

/**
 * VoiceDownloadHelper - Utility class for accessing TTS module voice information
 * without modifying the TTS module itself.
 * 
 * This class provides a clean interface to voice scanning and status checking
 * functionality that's already implemented in TTSPerformerModuleProcessor.
 */
class VoiceDownloadHelper
{
public:
    // Use the types directly from TTSPerformerModuleProcessor
    using VoiceEntry = TTSPerformerModuleProcessor::VoiceEntry;
    using VoiceStatus = TTSPerformerModuleProcessor::VoiceStatus;
    
    /**
     * Get all available voices from the manifest.
     * This is a static method in TTS module, so we can call it directly.
     */
    static std::vector<VoiceEntry> getAllAvailableVoices();
    
    /**
     * Resolve the models base directory where voices are stored.
     * Creates a temporary TTS processor instance to access the path resolution.
     */
    static juce::File resolveModelsBaseDir();
    
    /**
     * Check the status of a specific voice (installed/missing/partial/error).
     * Creates a temporary TTS processor instance for status checking.
     */
    static VoiceStatus checkVoiceStatus(const juce::String& voiceName);
    
    /**
     * Check the status of all available voices.
     * Returns a map of voice name -> status.
     */
    static std::map<juce::String, VoiceStatus> checkAllVoiceStatuses();
    
    /**
     * Get voice metadata by voice name.
     * Returns nullptr if voice not found in manifest.
     */
    static const VoiceEntry* getVoiceEntry(const juce::String& voiceName);
    
private:
    // Helper to create a temporary processor instance for path resolution
    // This is safe because the constructor/destructor are lightweight
    static std::unique_ptr<TTSPerformerModuleProcessor> createTempProcessor();
};

