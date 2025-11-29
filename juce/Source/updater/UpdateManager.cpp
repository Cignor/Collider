#include "UpdateManager.h"
#include "HashVerifier.h"
#include "ui/UpdateAvailableDialog.h"
#include "ui/DownloadProgressDialog.h"
#include <juce_events/juce_events.h>
#if JUCE_WINDOWS
#include <windows.h>
#endif

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

    // Setup ImGui dialog callbacks
    updateDownloadDialog.onStartDownload = [this]() { startDownload(); };
    updateDownloadDialog.onCancelDownload = [this]() { cancelDownload(); };
    updateDownloadDialog.onSkipVersion = [this]() { skipVersion(); };
    updateDownloadDialog.setVersionManager(versionManager.get());

    loadPreferences();

    // Register the running executable (non-blocking)
    // Do this after a short delay to not block startup
    juce::Logger::writeToLog("=== SCHEDULING registerRunningExecutable() in 500ms ===");
    DBG("=== SCHEDULING registerRunningExecutable() in 500ms ===");
    juce::Timer::callAfterDelay(500, [this]() {
        juce::Logger::writeToLog("=== TIMER FIRED - Calling registerRunningExecutable() ===");
        DBG("=== TIMER FIRED - Calling registerRunningExecutable() ===");
        registerRunningExecutable();
    });
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

    // Ensure running executable is registered before checking
    // (in case registerRunningExecutable hasn't run yet or failed)
    registerRunningExecutable();

    // Show dialog immediately in "checking" state for UX feedback
    updateDownloadDialog.showChecking();

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

        // Ensure running executable is registered before checking
        registerRunningExecutable();

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

    if (!applied)
    {
        DBG("Failed to apply updates");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon,
            "Update Failed",
            "Failed to install update. Please try again.",
            "OK");
        return;
    }

    // Update version info
    versionManager->setCurrentVersion(currentUpdateInfo.newVersion);
    DBG("Updates applied successfully");

    // If update requires restart (EXE was updated), launch PikonUpdater.exe
    if (currentUpdateInfo.requiresRestart)
    {
        DBG("Update requires restart - launching PikonUpdater.exe");
        
        // Create update manifest for PikonUpdater
        auto updateManifest = createUpdateManifest(
            currentUpdateInfo.filesToDownload, tempDir);
        
        // Get path to PikonUpdater.exe (shipped with app)
        auto updaterPath = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile)
            .getParentDirectory()
            .getChildFile("PikonUpdater.exe");
        
        if (!updaterPath.existsAsFile())
        {
            DBG("PikonUpdater.exe not found!");
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Update Error",
                "Updater tool not found. Please reinstall the application.",
                "OK");
            return;
        }
        
        // Get current process ID
        auto currentPID = GetCurrentProcessId();
        
        // Build command line
        juce::String cmdLine;
        cmdLine << "\"" << updaterPath.getFullPathName() << "\" ";
        cmdLine << "--source \"" << tempDir.getFullPathName() << "\" ";
        cmdLine << "--dest \"" << getInstallDirectory().getFullPathName() << "\" ";
        cmdLine << "--manifest \"" << updateManifest.getFullPathName() << "\" ";
        cmdLine << "--relaunch \"" << juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getFileName() << "\" ";
        cmdLine << "--wait-pid " << juce::String(currentPID);
        
        DBG("Launching updater: " + cmdLine);
        
        // Show message
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon,
            "Update Complete - Restarting",
            "The application will now restart to complete the update.",
            "OK");
        
        // Launch updater and quit after short delay
        juce::Timer::callAfterDelay(1000, [this, updaterPath, cmdLine]() {
            if (updaterPath.startAsProcess(cmdLine))
            {
                DBG("PikonUpdater launched successfully");
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
            else
            {
                DBG("Failed to launch PikonUpdater");
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon,
                    "Update Error",
                    "Failed to launch updater. Please restart manually.",
                    "OK");
            }
        });
    }
    else
    {
        // Non-critical files updated - show success message
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, "Update Complete", "Update installed successfully!", "OK");
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

// ============================================================================
// Phase 1: Hash Verification & Manifest Caching
// ============================================================================

juce::String UpdateManager::getCachedManifest()
{
    auto cacheFile =
        versionManager->getVersionFile().getParentDirectory().getChildFile("manifest_cache.json");

    if (cacheFile.existsAsFile())
    {
        // Check if cache is fresh (less than 1 hour old)
        auto age = juce::Time::getCurrentTime() - cacheFile.getLastModificationTime();
        if (age.inHours() < 1)
        {
            DBG("Using cached manifest (age: " + juce::String(age.inMinutes()) + " minutes)");
            return cacheFile.loadFileAsString();
        }
        else
        {
            DBG("Cached manifest too old (age: " + juce::String(age.inHours()) + " hours)");
        }
    }

    return juce::String();
}

void UpdateManager::cacheManifest(const juce::String& manifestJson)
{
    auto cacheFile =
        versionManager->getVersionFile().getParentDirectory().getChildFile("manifest_cache.json");

    cacheFile.getParentDirectory().createDirectory();
    cacheFile.replaceWithText(manifestJson);
    DBG("Manifest cached to: " + cacheFile.getFullPathName());
}

void UpdateManager::registerRunningExecutable()
{
    juce::Logger::writeToLog("================================================");
    juce::Logger::writeToLog("=== REGISTER RUNNING EXECUTABLE START ===");
    DBG("================================================");
    DBG("=== REGISTER RUNNING EXECUTABLE START ===");
    
    auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeName = exePath.getFileName();

    juce::Logger::writeToLog("EXE Name: " + exeName);
    juce::Logger::writeToLog("EXE Path: " + exePath.getFullPathName());
    juce::Logger::writeToLog("EXE Exists: " + juce::String(exePath.existsAsFile() ? "YES" : "NO"));
    DBG("EXE Name: " + exeName);
    DBG("EXE Path: " + exePath.getFullPathName());
    DBG("EXE Exists: " + juce::String(exePath.existsAsFile() ? "YES" : "NO"));
    
    if (exePath.existsAsFile())
    {
        juce::Logger::writeToLog("EXE Size: " + juce::String(exePath.getSize()) + " bytes");
        juce::Logger::writeToLog("EXE Modified: " + exePath.getLastModificationTime().toString(true, true, true, true));
        DBG("EXE Size: " + juce::String(exePath.getSize()) + " bytes");
        DBG("EXE Modified: " + exePath.getLastModificationTime().toString(true, true, true, true));
    }

    // Calculate hash FIRST to see in logs (before checking if tracked)
    auto exeHash = HashVerifier::calculateSHA256(exePath);
    juce::Logger::writeToLog("EXE Hash (calculated): " + (exeHash.isEmpty() ? "[FAILED - file may be locked]" : exeHash));
    DBG("EXE Hash (calculated): " + (exeHash.isEmpty() ? "[FAILED - file may be locked]" : exeHash));

    // Check if already tracked
    if (versionManager->hasFile(exeName))
    {
        auto existingInfo = versionManager->getFileInfo(exeName);
        juce::Logger::writeToLog("Executable already tracked in installed_files.json");
        juce::Logger::writeToLog("  Recorded hash: " + existingInfo.sha256);
        juce::Logger::writeToLog("  Recorded version: " + existingInfo.version);
        juce::Logger::writeToLog("  Recorded date: " + existingInfo.installedDate.toString(true, true, true, true));
        
        // Still calculate current hash to compare
        auto exeHash = HashVerifier::calculateSHA256(exePath);
        if (!exeHash.isEmpty())
        {
            juce::Logger::writeToLog("  Current EXE hash: " + exeHash);
            if (exeHash.equalsIgnoreCase(existingInfo.sha256))
            {
                juce::Logger::writeToLog("  ✅ Current EXE hash matches recorded hash");
                juce::Logger::writeToLog("================================================");
                return; // Everything matches, no update needed
            }
            else
            {
                juce::Logger::writeToLog("  ❌ Current EXE hash DOES NOT match recorded hash!");
                juce::Logger::writeToLog("    This means the EXE on disk was modified/replaced since last registration");
                juce::Logger::writeToLog("    Will verify against manifest and update record if it matches...");
                
                // Hash changed - verify against manifest and update if it matches
                // This handles the case where EXE was rebuilt but installed_files.json is stale
                auto cachedManifest = getCachedManifest();
                if (!cachedManifest.isEmpty())
                {
                    try
                    {
                        auto json = juce::JSON::parse(cachedManifest);
                        if (auto* obj = json.getDynamicObject())
                        {
                            auto currentVariant = versionManager->getCurrentVariant();
                            auto variantsArray = obj->getProperty("variants");
                            if (auto* variantsObj = variantsArray.getDynamicObject())
                            {
                                auto variantData = variantsObj->getProperty(currentVariant);
                                if (auto* variantObj = variantData.getDynamicObject())
                                {
                                    auto filesObj = variantObj->getProperty("files");
                                    if (auto* files = filesObj.getDynamicObject())
                                    {
                                        for (auto& prop : files->getProperties())
                                        {
                                            auto fileName = prop.name.toString();
                                            if (fileName.equalsIgnoreCase(exeName))
                                            {
                                                if (auto* fileObj = prop.value.getDynamicObject())
                                                {
                                                    juce::String manifestHash = fileObj->getProperty("sha256").toString();
                                                    juce::String version = fileObj->getProperty("version").toString();
                                                    juce::int64 size = (juce::int64)fileObj->getProperty("size");
                                                    
                                                    if (exeHash.equalsIgnoreCase(manifestHash))
                                                    {
                                                        // Current EXE hash matches manifest - update the stale record!
                                                        FileInfo info;
                                                        info.relativePath = fileName;
                                                        info.sha256 = manifestHash;
                                                        info.version = version;
                                                        info.size = size;
                                                        info.critical = true;
                                                        info.url = "";
                                                        
                                                        versionManager->updateFileRecord(fileName, info);
                                                        versionManager->saveVersionInfo();
                                                        
                                                        juce::Logger::writeToLog("  ✅✅ Updated stale record! EXE hash now matches manifest");
                                                        juce::Logger::writeToLog("    New hash: " + manifestHash);
                                                        juce::Logger::writeToLog("    New version: " + version);
                                                        juce::Logger::writeToLog("================================================");
                                                        return;
                                                    }
                                                    else
                                                    {
                                                        juce::Logger::writeToLog("  ❌ Current EXE hash doesn't match manifest either");
                                                        juce::Logger::writeToLog("    Local hash:  " + exeHash);
                                                        juce::Logger::writeToLog("    Manifest hash: " + manifestHash);
                                                        juce::Logger::writeToLog("    EXE needs update");
                                                        juce::Logger::writeToLog("================================================");
                                                        return;
                                                    }
                                                }
                                            }
                                        }
                                        juce::Logger::writeToLog("  ⚠️ EXE not found in manifest");
                                    }
                                }
                            }
                        }
                    }
                    catch (const std::exception& e)
                    {
                        juce::Logger::writeToLog("  ⚠️ Error parsing manifest: " + juce::String(e.what()));
                    }
                }
                else
                {
                    juce::Logger::writeToLog("  ⚠️ No cached manifest available to verify");
                }
            }
        }
        else
        {
            juce::Logger::writeToLog("  ⚠️ Could not calculate current hash (file may be locked)");
        }
        juce::Logger::writeToLog("================================================");
        return;
    }

    juce::Logger::writeToLog("Executable NOT tracked - will attempt to register");
    DBG("Executable NOT tracked - will attempt to register");

    // Hash already calculated above, check if it's valid
    if (exeHash.isEmpty())
    {
        juce::Logger::writeToLog("❌ Could not calculate hash (file may be locked)");
        juce::Logger::writeToLog("================================================");
        DBG("❌ Could not calculate hash (file may be locked)");
        DBG("================================================");
        return;
    }

    juce::Logger::writeToLog("Calculated EXE hash: " + exeHash);
    DBG("Calculated EXE hash: " + exeHash);

    // Try cached manifest
    auto cachedManifest = getCachedManifest();

    if (cachedManifest.isEmpty())
    {
        juce::Logger::writeToLog("⚠️ No cached manifest - will verify later when manifest is fetched");
        juce::Logger::writeToLog("================================================");
        return;
    }

    juce::Logger::writeToLog("Found cached manifest, checking for EXE entry...");

    try
    {
        auto json = juce::JSON::parse(cachedManifest);

        if (auto* obj = json.getDynamicObject())
        {
            auto currentVariant = versionManager->getCurrentVariant();
            auto variantsArray = obj->getProperty("variants");

            if (auto* variantsObj = variantsArray.getDynamicObject())
            {
                auto variantData = variantsObj->getProperty(currentVariant);

                if (auto* variantObj = variantData.getDynamicObject())
                {
                    auto filesObj = variantObj->getProperty("files");

                    if (auto* files = filesObj.getDynamicObject())
                    {
                        // Find EXE in manifest files
                        for (auto& prop : files->getProperties())
                        {
                            auto fileName = prop.name.toString();
                            
                            // Check if this is the executable file (EXACT MATCH ONLY)
                            if (fileName.equalsIgnoreCase(exeName))
                            {
                                DBG("✅ Found exact match for running EXE: " + fileName);
                                
                                if (auto* fileObj = prop.value.getDynamicObject())
                                {
                                    juce::String manifestHash = fileObj->getProperty("sha256").toString();
                                    juce::String version = fileObj->getProperty("version").toString();
                                    juce::int64 size = (juce::int64)fileObj->getProperty("size");
                                    
                                    juce::Logger::writeToLog("Found EXE in manifest:");
                                    juce::Logger::writeToLog("  Manifest hash: " + manifestHash);
                                    juce::Logger::writeToLog("  Manifest version: " + version);
                                    juce::Logger::writeToLog("  Manifest size: " + juce::String(size) + " bytes");
                                    juce::Logger::writeToLog("  Local EXE hash: " + exeHash);
                                    juce::Logger::writeToLog("  Local EXE size: " + juce::String(exePath.getSize()) + " bytes");
                                    
                                    // Compare hashes
                                    if (exeHash.equalsIgnoreCase(manifestHash))
                                    {
                                        // Hash matches! Register as installed
                                        FileInfo info;
                                        info.relativePath = fileName;
                                        info.sha256 = manifestHash;
                                        info.version = version;
                                        info.size = size;
                                        info.critical = true;
                                        info.url = "";
                                        
                                        versionManager->updateFileRecord(fileName, info);
                                        versionManager->saveVersionInfo();
                                        juce::Logger::writeToLog("✅✅ EXE verified and registered: " + fileName);
                                        juce::Logger::writeToLog("  Saved to: " + versionManager->getVersionFile().getFullPathName());
                                        juce::Logger::writeToLog("================================================");
                                        return;
                                    }
                                    else
                                    {
                                        juce::Logger::writeToLog("❌ Hash mismatch for running EXE");
                                        juce::Logger::writeToLog("  Manifest hash: " + manifestHash);
                                        juce::Logger::writeToLog("  Local hash:    " + exeHash);
                                        juce::Logger::writeToLog("  Size match: " + juce::String(exePath.getSize() == size ? "YES" : "NO"));
                                        juce::Logger::writeToLog("  ⚠️ Local EXE is different from manifest - needs update");
                                        juce::Logger::writeToLog("  ⚠️ EXE will NOT be registered (will show as Pending)");
                                        juce::Logger::writeToLog("================================================");
                                        // Don't register - needs update
                                        return;
                                    }
                                }
                            }
                        }
                        
                        juce::Logger::writeToLog("❌ Running EXE not found in manifest: " + exeName);
                        juce::Logger::writeToLog("  This means the manifest doesn't contain an entry for the running EXE");
                        juce::Logger::writeToLog("================================================");
                    }
                }
            }
        }
    }
    catch (const std::exception& e)
    {
        juce::Logger::writeToLog("❌ Error verifying executable: " + juce::String(e.what()));
        juce::Logger::writeToLog("================================================");
        // Don't fail - will try again later
    }
}

juce::File UpdateManager::getInstallDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory();
}

juce::File UpdateManager::createUpdateManifest(
    const juce::Array<FileInfo>& files, const juce::File& tempDir)
{
    auto manifestFile = tempDir.getChildFile("update_manifest.json");

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::DynamicObject::Ptr filesObj = new juce::DynamicObject();

    for (const auto& file : files)
    {
        juce::DynamicObject::Ptr fileObj = new juce::DynamicObject();
        fileObj->setProperty("sha256", file.sha256);
        fileObj->setProperty("size", file.size);
        filesObj->setProperty(file.relativePath, juce::var(fileObj));
    }

    root->setProperty("files", juce::var(filesObj));

    juce::FileOutputStream output(manifestFile);
    if (output.openedOk())
    {
        juce::JSON::writeToStream(output, juce::var(root), true);
        DBG("Update manifest created: " + manifestFile.getFullPathName());
    }
    else
    {
        DBG("Failed to create update manifest");
    }

    return manifestFile;
}

} // namespace Updater
