#pragma once

#include <juce_core/juce_core.h>
#include "UpdaterTypes.h"
#include "VersionManager.h"

namespace Updater
{

/**
 * Checks for available updates by fetching and comparing manifests
 */
class UpdateChecker
{
public:
    UpdateChecker(const juce::String& manifestUrl, VersionManager& versionManager);
    ~UpdateChecker();

    /**
     * Check for updates asynchronously
     * Callback will be called on the message thread
     */
    void checkForUpdatesAsync(std::function<void(UpdateInfo)> callback);

    /**
     * Check for updates synchronously (blocking)
     * @return UpdateInfo with details about available update
     */
    UpdateInfo checkForUpdates();

    /**
     * Cancel ongoing update check
     */
    void cancelCheck();

private:
    juce::String                  manifestUrl;
    VersionManager&               versionManager;
    std::unique_ptr<juce::Thread> checkThread;
    std::atomic<bool>             shouldCancel{false};

    // Fetch manifest from server
    UpdateManifest fetchManifest();

    // Compare versions and determine what needs updating
    UpdateInfo compareVersions(const UpdateManifest& manifest);

    // Helper to compare version strings (semantic versioning)
    static int compareVersionStrings(const juce::String& v1, const juce::String& v2);
};

} // namespace Updater
