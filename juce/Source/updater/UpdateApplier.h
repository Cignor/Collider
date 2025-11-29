#pragma once

#include <juce_core/juce_core.h>
#include "UpdaterTypes.h"
#include "VersionManager.h"

namespace Updater
{

/**
 * Applies downloaded updates to the installation
 * Handles both immediate updates (non-critical files) and staged updates (critical files)
 */
class UpdateApplier
{
public:
    enum class UpdateType
    {
        Immediate, // Apply now (non-critical files like presets, docs)
        OnRestart  // Stage for next restart (critical files like exe/dll)
    };

    UpdateApplier(VersionManager& versionManager);
    ~UpdateApplier();

    /**
     * Apply updates from temp directory
     * @param files Files to apply
     * @param tempDirectory Directory containing downloaded files
     * @param type Whether to apply immediately or on restart
     * @return true if successful, false on error
     */
    bool applyUpdates(
        const juce::Array<FileInfo>& files,
        const juce::File&            tempDirectory,
        UpdateType                   type);

    /**
     * Delete files that are no longer needed
     * @param filesToDelete List of relative paths to delete
     * @return true if successful
     */
    bool deleteOldFiles(const juce::Array<juce::String>& filesToDelete);

    /**
     * Launch updater helper and exit application
     * This is used for critical updates that require app restart
     * @param tempDirectory Directory containing staged files
     * @return true if updater launched successfully
     */
    bool launchUpdaterAndExit(const juce::File& tempDirectory);

    /**
     * Rollback to previous version (if backup exists)
     * @return true if rollback successful
     */
    bool rollbackUpdate();

private:
    VersionManager& versionManager;

    // Get installation directory
    juce::File getInstallDirectory() const;

    // Get backup directory
    juce::File getBackupDirectory() const;

    // Backup a file before replacing
    bool backupFile(const juce::File& file);

    // Replace a file (with backup)
    bool replaceFile(const juce::File& source, const juce::File& destination);

    // Create batch script for Windows update
    bool createUpdateScript(const juce::File& tempDirectory, const juce::File& scriptFile);
};

} // namespace Updater
