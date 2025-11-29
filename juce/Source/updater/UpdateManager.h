#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "UpdaterTypes.h"
#include "UpdateChecker.h"
#include "FileDownloader.h"
#include "UpdateApplier.h"
#include "VersionManager.h"
#include "ui/UpdateDownloadDialog.h"

namespace Updater
{

// Forward declarations
class UpdateAvailableDialog;
class DownloadProgressDialog;

/**
 * Main orchestrator for the auto-updater system
 * Coordinates checking, downloading, and applying updates
 * Manages UI dialogs and user preferences
 */
class UpdateManager
{
public:
    UpdateManager();
    ~UpdateManager();

    /**
     * Check for updates manually (from menu)
     */
    void checkForUpdatesManual();

    /**
     * Check for updates automatically on startup
     * @param delaySeconds Delay before checking (default: 3 seconds)
     */
    void checkForUpdatesAutomatic(int delaySeconds = 3);

    /**
     * Get/set auto-check preference
     */
    bool getAutoCheckEnabled() const;
    void setAutoCheckEnabled(bool enabled);

    /**
     * Get manifest URL
     */
    static juce::String getManifestUrl();

    /**
     * Render ImGui dialogs (must be called from ImGui render loop)
     */
    void render();

private:
    // Core updater components
    std::unique_ptr<VersionManager> versionManager;
    std::unique_ptr<UpdateChecker>  updateChecker;
    std::unique_ptr<FileDownloader> fileDownloader;
    std::unique_ptr<UpdateApplier>  updateApplier;

    // UI dialogs
    // UI dialogs
    // std::unique_ptr<juce::DialogWindow> updateAvailableWindow; // Replaced by ImGui
    // std::unique_ptr<juce::DialogWindow> downloadProgressWindow; // Replaced by ImGui
    UpdateDownloadDialog updateDownloadDialog;

    // State
    UpdateInfo   currentUpdateInfo;
    juce::String skippedVersion;
    bool         isCheckingForUpdates = false;
    bool         isDownloading = false;

    // Callbacks
    void onUpdateCheckComplete(UpdateInfo info);
    void onDownloadProgress(DownloadProgress progress);
    void onDownloadComplete(bool success, juce::String error);
    void onUpdateApplied(bool success, juce::String error);

    // UI handlers
    void showUpdateAvailableDialog(const UpdateInfo& info);
    void showDownloadProgressDialog();
    void closeUpdateAvailableDialog();
    void closeDownloadProgressDialog();

    // User actions
    void startDownload();
    void cancelDownload();
    void skipVersion();
    void restartApplication();

    // Preferences
    void                  loadPreferences();
    void                  savePreferences();
    juce::PropertiesFile* getPropertiesFile();

    // Temp directory for downloads
    juce::File getTempDirectory();
    juce::File getInstallDirectory() const;

    // Manifest caching
    juce::String getCachedManifest();
    void         cacheManifest(const juce::String& manifestJson);

    // Self-registration
    void registerRunningExecutable();

    // Update manifest creation
    juce::File createUpdateManifest(const juce::Array<FileInfo>& files, const juce::File& tempDir);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateManager)
};

} // namespace Updater
