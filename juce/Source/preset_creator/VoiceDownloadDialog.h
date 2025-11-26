#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include "../audio/voices/VoiceDownloadHelper.h"
#include "../audio/voices/VoiceDownloadThread.h"
#include <map>
#include <vector>

/**
 * VoiceDownloadDialog - Dialog component for managing Piper TTS voice downloads.
 * 
 * Displays available voices, shows download status, and allows users to download voices.
 * Accessible via Settings > Download Piper Voices... menu item.
 */
class VoiceDownloadDialog
{
public:
    VoiceDownloadDialog();
    ~VoiceDownloadDialog();
    
    /**
     * Render the dialog window (called from ImGuiNodeEditorComponent render loop).
     */
    void render();
    
    /**
     * Open the dialog.
     */
    void open() { isOpen = true; refreshVoiceStatuses(); }
    
    /**
     * Close the dialog.
     */
    void close() { isOpen = false; }
    
    /**
     * Check if dialog is open.
     */
    bool getIsOpen() const { return isOpen; }
    
private:
    bool isOpen = false;
    
    // Voice download thread
    VoiceDownloadThread downloadThread;
    
    // Cached voice data
    std::vector<VoiceDownloadHelper::VoiceEntry> availableVoices;
    std::map<juce::String, VoiceDownloadHelper::VoiceStatus> voiceStatuses;
    std::map<juce::String, juce::int64> voiceFileSizes;  // File size in bytes for installed voices
    
    // UI state
    char searchFilter[256] = { 0 };
    int selectedLanguageFilter = 0;  // 0 = All languages
    juce::StringArray languageList;
    
    // Selection state
    std::vector<bool> voiceSelected;  // Checkbox states
    
    // Track download state for auto-refresh
    bool wasDownloading = false;
    
    /**
     * Refresh voice statuses from disk.
     */
    void refreshVoiceStatuses();
    
    /**
     * Render the voice list with filters.
     */
    void renderVoiceList();
    
    /**
     * Render download controls and progress.
     */
    void renderDownloadControls();
    
    /**
     * Get filtered voices based on search and language filter.
     */
    std::vector<VoiceDownloadHelper::VoiceEntry> getFilteredVoices() const;
    
    /**
     * Build language list from available voices.
     */
    void buildLanguageList();
    
    /**
     * Get formatted file size string for a voice.
     */
    juce::String getFormattedFileSize(const juce::String& voiceName) const;
};


