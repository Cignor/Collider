#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "UpdaterTypes.h"

namespace Updater
{

/**
 * Manages information about installed files and application version
 * Tracks what files are installed, their versions, and hashes
 */
class VersionManager
{
public:
    VersionManager();
    ~VersionManager();

    /**
     * Get current application version
     */
    juce::String getCurrentVersion() const;

    /**
     * Get current variant (standard, cuda, etc.)
     */
    juce::String getCurrentVariant() const;

    /**
     * Get information about all installed files
     */
    const juce::HashMap<juce::String, InstalledFileInfo>& getInstalledFiles() const;

    /**
     * Get information about a specific installed file
     */
    InstalledFileInfo getFileInfo(const juce::String& relativePath) const;

    /**
     * Check if a file is tracked as installed
     */
    bool hasFile(const juce::String& relativePath) const;

    /**
     * Update record for a file after installation
     */
    void updateFileRecord(const juce::String& relativePath, const FileInfo& info);

    /**
     * Remove record for a file
     */
    void removeFileRecord(const juce::String& relativePath);

    /**
     * Set current application version
     */
    void setCurrentVersion(const juce::String& version);

    /**
     * Set current variant
     */
    void setCurrentVariant(const juce::String& variant);

    /**
     * Save version info to disk
     */
    bool saveVersionInfo();

    /**
     * Load version info from disk
     */
    bool loadVersionInfo();

    /**
     * Get the file where version info is stored
     */
    juce::File getVersionFile() const;

private:
    juce::String                                   currentVersion;
    juce::String                                   currentVariant;
    juce::HashMap<juce::String, InstalledFileInfo> installedFiles;
    juce::Time                                     lastUpdateCheck;
    mutable bool                                   versionInfoLoaded; // Lazy load flag

    // Get application data directory
    juce::File getAppDataDirectory() const;
    
    // Ensure version info is loaded (lazy loading)
    void ensureVersionInfoLoaded() const;
};

} // namespace Updater
