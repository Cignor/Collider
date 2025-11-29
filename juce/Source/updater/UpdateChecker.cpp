#include "UpdateChecker.h"
#include "HashVerifier.h"
#include <juce_core/juce_core.h>

namespace Updater
{

// Helper thread for async update checking
class UpdateCheckThread : public juce::Thread
{
public:
    UpdateCheckThread(UpdateChecker* checker, std::function<void(UpdateInfo)> callback)
        : juce::Thread("UpdateCheckThread"), checker(checker), callback(std::move(callback))
    {
    }

    void run() override
    {
        auto info = checker->checkForUpdates();

        // Call callback on message thread
        juce::MessageManager::callAsync([this, info]() {
            if (callback)
                callback(info);
        });
    }

private:
    UpdateChecker*                  checker;
    std::function<void(UpdateInfo)> callback;
};

UpdateChecker::UpdateChecker(const juce::String& manifestUrl, VersionManager& versionManager)
    : manifestUrl(manifestUrl), versionManager(versionManager)
{
}

UpdateChecker::~UpdateChecker() { cancelCheck(); }

void UpdateChecker::checkForUpdatesAsync(std::function<void(UpdateInfo)> callback)
{
    cancelCheck();

    shouldCancel = false;
    checkThread = std::make_unique<UpdateCheckThread>(this, std::move(callback));
    checkThread->startThread();
}

UpdateInfo UpdateChecker::checkForUpdates()
{
    UpdateInfo info;
    info.currentVersion = versionManager.getCurrentVersion();

    try
    {
        // Fetch manifest from server
        auto manifest = fetchManifest();

        if (shouldCancel)
            return info;

        // Compare versions
        info = compareVersions(manifest);
    }
    catch (const std::exception& e)
    {
        DBG("Update check failed: " + juce::String(e.what()));
        info.updateAvailable = false;
    }

    return info;
}

void UpdateChecker::cancelCheck()
{
    shouldCancel = true;

    if (checkThread != nullptr)
    {
        checkThread->stopThread(5000);
        checkThread.reset();
    }
}

UpdateManifest UpdateChecker::fetchManifest()
{
    DBG("Fetching manifest from: " + manifestUrl);

    juce::URL url(manifestUrl);

    // Set timeout
    url = url.withParameter("t", juce::String(juce::Time::getCurrentTime().toMilliseconds()));

    // Fetch with timeout
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000)
            .withNumRedirectsToFollow(5));

    if (stream == nullptr)
    {
        throw std::runtime_error("Failed to connect to update server");
    }

    // Read response
    juce::String jsonString = stream->readEntireStreamAsString();

    if (jsonString.isEmpty())
    {
        throw std::runtime_error("Empty response from update server");
    }

    DBG("Manifest fetched successfully, size: " + juce::String(jsonString.length()) + " bytes");
    // Log first 500 chars of manifest for debugging
    DBG("Manifest content (start): " + jsonString.substring(0, 500));

    // Cache manifest for later use (e.g., registerRunningExecutable)
    cacheManifestLocally(jsonString);

    // Parse manifest
    auto manifest = UpdateManifest::fromJson(jsonString);
    DBG("Parsed updateUrl: " + manifest.updateUrl);

    if (manifest.updateUrl.isEmpty())
    {
        DBG("WARNING: updateUrl is empty in manifest!");
        juce::Logger::writeToLog("WARNING: updateUrl is empty in manifest!");
    }
    else
    {
        juce::Logger::writeToLog("Parsed updateUrl: " + manifest.updateUrl);
    }

    return manifest;
}

UpdateInfo UpdateChecker::compareVersions(const UpdateManifest& manifest)
{
    UpdateInfo info;
    info.currentVersion = versionManager.getCurrentVersion();
    info.newVersion = manifest.latestVersion;
    info.changelogUrl = manifest.changelogUrl;
    info.changelogSummary = manifest.changelogSummary;

    // Get variant info early to populate allRemoteFiles
    auto  currentVariant = versionManager.getCurrentVariant();
    auto* variant = manifest.getVariant(currentVariant);

    if (variant != nullptr)
    {
        info.allRemoteFiles = variant->files;
    }
    else
    {
        DBG("Variant not found in manifest: " + currentVariant);
    }

    // Compare versions
    int versionComparison = compareVersionStrings(info.currentVersion, info.newVersion);

    if (versionComparison > 0)
    {
        // Current version is newer (dev build?)
        DBG("Current version is newer than server. Current: " + info.currentVersion +
            ", Latest: " + info.newVersion);
        info.updateAvailable = false;
        return info;
    }

    // Update is available
    info.updateAvailable = true;
    DBG("Update available! Current: " + info.currentVersion + ", New: " + info.newVersion);

    if (variant == nullptr)
    {
        info.updateAvailable = false;
        return info;
    }

    // Get installed files
    const auto& installedFiles = versionManager.getInstalledFiles();

    // Get install directory
    auto installDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();

    juce::Logger::writeToLog("UpdateChecker: Install dir: " + installDir.getFullPathName());

    // Get running executable path for special handling
    auto runningExePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto runningExeName = runningExePath.getFileName();

    // Determine which files need updating
    for (const auto& fileInfo : variant->files)
    {
        bool needsUpdate = false;
        auto localFile = installDir.getChildFile(fileInfo.relativePath);
        bool isRunningExe = (fileInfo.relativePath.equalsIgnoreCase(runningExeName) || 
                            (fileInfo.relativePath.endsWithIgnoreCase(".exe") && 
                             localFile == runningExePath));

        // Check if file exists locally
        if (!localFile.exists())
        {
            // File doesn't exist physically
            needsUpdate = true;
            // Only log first few missing files to avoid spam
            if (info.filesToDownload.size() < 5)
                juce::Logger::writeToLog(
                    "UpdateChecker: Missing file: " + localFile.getFullPathName());
        }
        else if (!installedFiles.contains(fileInfo.relativePath))
        {
            // File exists but not tracked - verify hash
            // Special handling for running executable (can't hash locked file)
            if (isRunningExe)
            {
                // For running EXE, check if it's already registered (might have happened after delay)
                // If not registered, we'll skip it for now and let registerRunningExecutable handle it
                // But if user manually checks, we should try to register it now
                DBG("UpdateChecker: Running EXE found but not tracked - checking if already registered...");
                
                // Try to calculate hash (may fail if locked)
                auto localHash = HashVerifier::calculateSHA256(localFile);
                
                if (localHash.isEmpty())
                {
                    // Can't hash running EXE - check if it's in installed_files.json now
                    // (registerRunningExecutable might have run)
                    if (versionManager.hasFile(fileInfo.relativePath))
                    {
                        DBG("UpdateChecker: Running EXE now tracked in installed_files.json - skipping");
                        needsUpdate = false;
                    }
                    else
                    {
                        // Still not tracked - assume it needs update for now
                        // But log a warning
                        DBG("UpdateChecker: Running EXE cannot be hashed (locked) and not tracked - will be handled by registerRunningExecutable");
                        juce::Logger::writeToLog(
                            "UpdateChecker: Running EXE " + fileInfo.relativePath + 
                            " cannot be verified (file locked). If hash matches manifest, it will be registered on next check.");
                        // Skip for now - registerRunningExecutable will handle it
                        needsUpdate = false;
                    }
                }
                else if (localHash == fileInfo.sha256)
                {
                    // Hash matches, mark as installed and don't download
                    versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
                    needsUpdate = false;
                    DBG("UpdateChecker: Running EXE hash verified - registered");
                }
                else
                {
                    // Hash mismatch, needs update
                    needsUpdate = true;
                    juce::Logger::writeToLog(
                        "UpdateChecker: Running EXE hash mismatch: " + fileInfo.relativePath +
                        " Local: " + localHash.substring(0, 16) + "... Remote: " + fileInfo.sha256.substring(0, 16) + "...");
                }
            }
            else
            {
                // Non-EXE file - normal hash verification
                auto localHash = HashVerifier::calculateSHA256(localFile);

                if (localHash == fileInfo.sha256)
                {
                    // Hash matches, mark as installed and don't download
                    versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
                    needsUpdate = false;
                }
                else
                {
                    // Hash mismatch, needs update
                    needsUpdate = true;
                    juce::Logger::writeToLog(
                        "UpdateChecker: Hash mismatch for " + fileInfo.relativePath +
                        " Local: " + localHash + " Remote: " + fileInfo.sha256);
                }
            }
        }
        else
        {
            // File is tracked - but we should still verify the actual file on disk
            // to avoid re-downloading correct files due to stale tracking data
            // Special handling for running executable
            if (isRunningExe)
            {
                // For running EXE, if it's tracked and hash matches manifest, trust it
                const auto& installed = installedFiles[fileInfo.relativePath];
                if (installed.sha256 == fileInfo.sha256)
                {
                    // Hash in record matches manifest - trust it (can't verify running EXE)
                    if (installed.version != fileInfo.version)
                    {
                        // Update version if needed
                        versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
                        juce::Logger::writeToLog(
                            "UpdateChecker: Updated version for running EXE: " + fileInfo.relativePath);
                    }
                    needsUpdate = false;
                    DBG("UpdateChecker: Running EXE is tracked and hash matches manifest - skipping");
                }
                else
                {
                    // Hash in record doesn't match - needs update
                    needsUpdate = true;
                    juce::Logger::writeToLog(
                        "UpdateChecker: Running EXE hash in record doesn't match manifest: " + fileInfo.relativePath +
                        " Record: " + installed.sha256.substring(0, 16) + "... Manifest: " + fileInfo.sha256.substring(0, 16) + "...");
                }
            }
            else
            {
                // Non-EXE file - normal hash verification
                auto localHash = HashVerifier::calculateSHA256(localFile);

                if (localHash == fileInfo.sha256)
                {
                    // File on disk matches manifest - update record if needed and skip download
                    const auto& installed = installedFiles[fileInfo.relativePath];
                    if (installed.version != fileInfo.version || installed.sha256 != fileInfo.sha256)
                    {
                        // Record was stale - update it
                        versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
                        juce::Logger::writeToLog(
                            "UpdateChecker: Updated stale record for " + fileInfo.relativePath);
                    }
                    needsUpdate = false;
                }
                else
                {
                    // File on disk doesn't match - needs update
                    needsUpdate = true;
                    juce::Logger::writeToLog(
                        "UpdateChecker: File changed on disk: " + fileInfo.relativePath +
                        " Local: " + localHash + " Remote: " + fileInfo.sha256);
                }
            }
        }

        if (needsUpdate)
        {
            info.filesToDownload.add(fileInfo);
            info.totalDownloadSize += fileInfo.size;

            if (fileInfo.critical)
                info.requiresRestart = true;
        }
    }

    // Check for files that should be deleted (exist locally but not in manifest)
    for (auto it = installedFiles.begin(); it != installedFiles.end(); ++it)
    {
        bool foundInManifest = false;

        for (const auto& fileInfo : variant->files)
        {
            if (fileInfo.relativePath == it.getKey())
            {
                foundInManifest = true;
                break;
            }
        }

        if (!foundInManifest)
        {
            info.filesToDelete.add(it.getKey());
            DBG("File to delete: " + it.getKey());
        }
    }

    DBG("Files to download: " + juce::String(info.filesToDownload.size()));
    DBG("Files to delete: " + juce::String(info.filesToDelete.size()));
    DBG("Total download size: " + juce::String(info.totalDownloadSize / 1024 / 1024) + " MB");
    DBG("Requires restart: " + juce::String(info.requiresRestart ? "yes" : "no"));

    return info;
}

int UpdateChecker::compareVersionStrings(const juce::String& v1, const juce::String& v2)
{
    // Parse semantic version strings (e.g., "1.2.3")
    auto parts1 = juce::StringArray::fromTokens(v1, ".", "");
    auto parts2 = juce::StringArray::fromTokens(v2, ".", "");

    // Pad with zeros if needed
    while (parts1.size() < 3)
        parts1.add("0");
    while (parts2.size() < 3)
        parts2.add("0");

    // Compare each part
    for (int i = 0; i < 3; ++i)
    {
        int num1 = parts1[i].getIntValue();
        int num2 = parts2[i].getIntValue();

        if (num1 < num2)
            return -1; // v1 is older
        if (num1 > num2)
            return 1; // v1 is newer
    }

    return 0; // Versions are equal
}

void UpdateChecker::cacheManifestLocally(const juce::String& jsonString)
{
    auto cacheFile = versionManager.getVersionFile()
        .getParentDirectory()
        .getChildFile("manifest_cache.json");
    
    cacheFile.getParentDirectory().createDirectory();
    cacheFile.replaceWithText(jsonString);
    DBG("Manifest cached to: " + cacheFile.getFullPathName());
}

} // namespace Updater
