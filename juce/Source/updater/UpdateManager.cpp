#include "UpdateManager.h"
#include "ui/UpdateAvailableDialog.h"
#include "ui/DownloadProgressDialog.h"
#include <juce_events/juce_events.h>

namespace Updater
{

// Manifest URL - update this to your OVH server
const juce::String MANIFEST_URL = "https://pimpant.club/pikon-raditsz/manifest.json";

UpdateManager::UpdateManager()
{
    // Initialize core components
    versionManager = std::make_unique<VersionManager>();
    updateChecker = std::make_unique<UpdateChecker>(MANIFEST_URL, *versionManager);
    fileDownloader = std::make_unique<FileDownloader>();
    updateApplier = std::make_unique<UpdateApplier>(*versionManager);

    updateApplier = std::make_unique<UpdateApplier>(*versionManager);

    // Setup ImGui dialog callbacks
    updateDownloadDialog.onStartDownload = [this]() { startDownload(); };
    updateDownloadDialog.onCancelDownload = [this]() { cancelDownload(); };
    updateDownloadDialog.onSkipVersion = [this]() { skipVersion(); };

    loadPreferences();
}

UpdateManager::~UpdateManager()
{
    savePreferences();
    savePreferences();
    // closeUpdateAvailableDialog();
    // closeDownloadProgressDialog();
}

juce::String UpdateManager::getManifestUrl() { return MANIFEST_URL; }

void UpdateManager::render() { updateDownloadDialog.render(); }

void UpdateManager::checkForUpdatesManual()
{
    if (isCheckingForUpdates)
    {
        DBG("Update check already in progress");
        return;
    }

    isCheckingForUpdates = true;
    DBG("Manual update check started");

    updateChecker->checkForUpdatesAsync([this](UpdateInfo info) {
        isCheckingForUpdates = false;
        onUpdateCheckComplete(info);
    });
}

void UpdateManager::checkForUpdatesAutomatic(int delaySeconds)
{
    if (!getAutoCheckEnabled())
    {
        DBG("Automatic update check disabled");
        return;
    }

    if (isCheckingForUpdates)
    {
        DBG("Update check already in progress");
        return;
    }

    // Delay the check
    juce::Timer::callAfterDelay(delaySeconds * 1000, [this]() {
        isCheckingForUpdates = true;
        DBG("Automatic update check started");

        updateChecker->checkForUpdatesAsync([this](UpdateInfo info) {
            isCheckingForUpdates = false;

            // Only show dialog if update is available
            if (info.updateAvailable)
                onUpdateCheckComplete(info);
            else
                DBG("No updates available (automatic check)");
        });
    });
}

void UpdateManager::onUpdateCheckComplete(UpdateInfo info)
{
    currentUpdateInfo = info;

    // Always show the dialog so user can see status
    showUpdateAvailableDialog(info);
}

void UpdateManager::showUpdateAvailableDialog(const UpdateInfo& info)
{
    // Use ImGui dialog instead of native window
    updateDownloadDialog.open(info);

    /* Native dialog code disabled
    closeUpdateAvailableDialog();

    auto* dialog = new UpdateAvailableDialog(info);
    // ... (rest of native code)
    updateAvailableWindow->setResizable(false, false);
    */
}

void UpdateManager::showDownloadProgressDialog()
{
    // Disabled for ImGui
}

void UpdateManager::startDownload()
{
    if (isDownloading)
    {
        DBG("Download already in progress");
        return;
    }

    isDownloading = true;
    updateDownloadDialog.setDownloading(true);
    // showDownloadProgressDialog(); // Disabled for ImGui

    auto tempDir = getTempDirectory();
    tempDir.createDirectory();

    fileDownloader->downloadFiles(
        currentUpdateInfo.filesToDownload,
        tempDir,
        [this](DownloadProgress progress) { onDownloadProgress(progress); },
        [this](bool success, juce::String error) { onDownloadComplete(success, error); });
}

void UpdateManager::onDownloadProgress(DownloadProgress progress)
{
    updateDownloadDialog.setDownloadProgress(progress);

    /* Native dialog code disabled
    if (downloadProgressWindow != nullptr)
    {
        if (auto* dialog = dynamic_cast<DownloadProgressDialog*>(
                downloadProgressWindow->getContentComponent()))
        {
            dialog->setProgress(progress);
        }
    }
    */
}

void UpdateManager::onDownloadComplete(bool success, juce::String error)
{
    isDownloading = false;
    updateDownloadDialog.setDownloading(false);

    if (!success)
    {
        // closeDownloadProgressDialog();
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Download Failed",
            "Failed to download update: " + error,
            "OK");
        return;
    }

    // Apply the update
    auto tempDir = getTempDirectory();

    bool applied = updateApplier->applyUpdates(
        currentUpdateInfo.filesToDownload,
        tempDir,
        currentUpdateInfo.requiresRestart ? UpdateApplier::UpdateType::OnRestart
                                          : UpdateApplier::UpdateType::Immediate);

    if (applied)
    {
        // Update version info
        versionManager->setCurrentVersion(currentUpdateInfo.newVersion);

        // Show completion
        // if (downloadProgressWindow != nullptr) ...
    }
    else
    {
        // closeDownloadProgressDialog();
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Update Failed",
            "Failed to apply update. Please try again later.",
            "OK");
    }
}

void UpdateManager::cancelDownload()
{
    if (fileDownloader)
        fileDownloader->cancelDownload();

    isDownloading = false;
}

void UpdateManager::skipVersion()
{
    skippedVersion = currentUpdateInfo.newVersion;
    savePreferences();
    DBG("Skipped version: " + skippedVersion);
}

void UpdateManager::restartApplication()
{
    // Save preferences before restarting
    savePreferences();

    // Quit the application
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

void UpdateManager::closeUpdateAvailableDialog() { /* updateAvailableWindow.reset(); */ }

void UpdateManager::closeDownloadProgressDialog() { /* downloadProgressWindow.reset(); */ }

juce::File UpdateManager::getTempDirectory()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("PikonRaditszUpdates");
}

bool UpdateManager::getAutoCheckEnabled() const
{
    auto* props = const_cast<UpdateManager*>(this)->getPropertiesFile();
    if (props != nullptr)
        return props->getBoolValue("autoCheckForUpdates", true);
    return true;
}

void UpdateManager::setAutoCheckEnabled(bool enabled)
{
    if (auto* props = getPropertiesFile())
    {
        props->setValue("autoCheckForUpdates", enabled);
        savePreferences();
    }
}

void UpdateManager::loadPreferences()
{
    if (auto* props = getPropertiesFile())
    {
        skippedVersion = props->getValue("skippedVersion", "");
    }
}

void UpdateManager::savePreferences()
{
    if (auto* props = getPropertiesFile())
    {
        props->setValue("skippedVersion", skippedVersion);
        props->saveIfNeeded();
    }
}

juce::PropertiesFile* UpdateManager::getPropertiesFile()
{
    // Get the application's properties file
    if (auto* app = juce::JUCEApplication::getInstance())
    {
        // Try to get properties from PresetCreatorApplication
        // This is a bit hacky but works for now
        juce::PropertiesFile::Options options;
        options.applicationName = app->getApplicationName();
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";

        static std::unique_ptr<juce::PropertiesFile> props;
        if (props == nullptr)
            props = std::make_unique<juce::PropertiesFile>(options);

        return props.get();
    }
    return nullptr;
}

} // namespace Updater
