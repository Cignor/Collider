#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace Updater
{

/**
 * Information about a single file in the update manifest
 */
struct FileInfo
{
    juce::String relativePath; // Path relative to install directory
    juce::String url;          // Download URL for this file
    juce::int64  size;         // File size in bytes
    juce::String sha256;       // SHA256 hash for verification
    juce::String version;      // Version when this file was last updated
    bool         critical;     // If true, requires app restart to apply

    FileInfo() : size(0), critical(false) {}

    // Parse from JSON
    static FileInfo fromJson(const juce::String& path, const juce::var& json);

    // Convert to JSON
    juce::var toJson() const;
};

/**
 * Information about a variant (standard, cuda, etc.)
 */
struct VariantInfo
{
    juce::String          name;        // Variant identifier (e.g., "cuda")
    juce::String          displayName; // Human-readable name
    juce::Array<FileInfo> files;       // All files in this variant

    // Parse from JSON
    static VariantInfo fromJson(
        const juce::String& variantName,
        const juce::var&    json,
        const juce::String& baseUrl);
};

/**
 * Complete update manifest from server
 */
struct UpdateManifest
{
    juce::String             appName;
    juce::String             latestVersion;
    juce::Time               releaseDate;
    juce::String             minimumVersion;
    juce::String             updateUrl;
    juce::Array<VariantInfo> variants;
    juce::String             changelogUrl;
    juce::String             changelogSummary;

    // Parse from JSON
    static UpdateManifest fromJson(const juce::String& jsonString);

    // Get variant by name
    const VariantInfo* getVariant(const juce::String& variantName) const;
};

/**
 * Information about available update
 */
struct UpdateInfo
{
    bool                      updateAvailable;
    juce::String              currentVersion;
    juce::String              newVersion;
    juce::Array<FileInfo>     filesToDownload;
    juce::Array<juce::String> filesToDelete;
    juce::int64               totalDownloadSize;
    bool                      requiresRestart;
    juce::String              changelogUrl;
    juce::String              changelogSummary;
    juce::Array<FileInfo>     allRemoteFiles; // All files available on the server

    UpdateInfo() : updateAvailable(false), totalDownloadSize(0), requiresRestart(false) {}
};

/**
 * Progress information for file downloads
 */
struct DownloadProgress
{
    juce::int64  bytesDownloaded;
    juce::int64  totalBytes;
    double       speedBytesPerSec;
    int          filesCompleted;
    int          totalFiles;
    juce::String currentFile;

    DownloadProgress()
        : bytesDownloaded(0), totalBytes(0), speedBytesPerSec(0.0), filesCompleted(0), totalFiles(0)
    {
    }

    double getProgress() const
    {
        return totalBytes > 0 ? (double)bytesDownloaded / (double)totalBytes : 0.0;
    }
};

/**
 * Information about an installed file
 */
struct InstalledFileInfo
{
    juce::String version;
    juce::String sha256;
    juce::Time   installedDate;

    // Parse from JSON
    static InstalledFileInfo fromJson(const juce::var& json);

    // Convert to JSON
    juce::var toJson() const;
};

} // namespace Updater
