#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include "../UpdaterTypes.h"
#include <vector>

// Forward declaration
namespace Updater
{
class VersionManager;
}

namespace Updater
{

/**
 * UpdateDownloadDialog - ImGui dialog for managing application updates.
 *
 * Displays available updates (changed files), shows download status,
 * and allows users to start the update process.
 * Modeled after VoiceDownloadDialog for consistent UX.
 */
class UpdateDownloadDialog
{
public:
    UpdateDownloadDialog();
    ~UpdateDownloadDialog() = default;

    /**
     * Render the dialog window (called from ImGui render loop).
     */
    void render();

    /**
     * Open the dialog with new update info.
     */
    void open(const UpdateInfo& info);

    /**
     * Show the dialog in "checking" state while update check is in progress.
     */
    void showChecking();

    /**
     * Close the dialog.
     */
    void close() { isOpen = false; }

    /**
     * Check if dialog is open.
     */
    bool getIsOpen() const { return isOpen; }

    /**
     * Update download progress.
     */
    void setDownloadProgress(const DownloadProgress& progress);

    /**
     * Set download state.
     */
    void setDownloading(bool downloading) { isDownloading = downloading; }

    /**
     * Set VersionManager reference for hash comparison.
     */
    void setVersionManager(VersionManager* vm) { versionManager = vm; }

    // Callbacks
    // onStartDownload is called when the user clicks "Update Now" with the
    // list of files they have selected (can be empty).
    std::function<void(const juce::Array<FileInfo>&)> onStartDownload;
    std::function<void()> onCancelDownload;
    std::function<void()> onSkipVersion;

private:
    bool isOpen = false;
    bool isDownloading = false;
    bool isChecking = false; // True while checking for updates

    UpdateInfo       updateInfo;
    DownloadProgress currentProgress;
    VersionManager*  versionManager = nullptr; // For hash comparison

    // UI state
    char searchFilter[256] = {0};
    bool showCriticalOnly = false;

    // Selection state for per-file updates.
    // This array is parallel to updateInfo.filesToDownload; selection is only
    // meaningful for files that actually need an update.
    std::vector<bool> fileSelected;

    /**
     * Render the file list.
     */
    void renderFileList();

    /**
     * Render download controls and progress.
     */
    void renderControls();

    /**
     * Get formatted file size string.
     */
    juce::String getFormattedFileSize(juce::int64 size) const;

    /**
     * Get local hash for a file (from VersionManager or calculate it).
     */
    juce::String getLocalHash(const juce::String& relativePath) const;

    /**
     * Helper to build the list of selected files based on fileSelected[]
     * and updateInfo.filesToDownload.
     */
    juce::Array<FileInfo> getSelectedFiles() const;
};

} // namespace Updater
