#pragma once

#include <juce_core/juce_core.h>
#include <imgui.h>
#include "../UpdaterTypes.h"
#include <vector>

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

    // Callbacks
    std::function<void()> onStartDownload;
    std::function<void()> onCancelDownload;
    std::function<void()> onSkipVersion;

private:
    bool isOpen = false;
    bool isDownloading = false;
    bool isChecking = false; // True while checking for updates

    UpdateInfo       updateInfo;
    DownloadProgress currentProgress;

    // UI state
    char searchFilter[256] = {0};
    bool showCriticalOnly = false;

    // Selection state (if we want to allow partial updates, though usually updates are
    // all-or-nothing) For now, we'll just show the files.

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
};

} // namespace Updater
