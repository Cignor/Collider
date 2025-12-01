#pragma once

#include <juce_core/juce_core.h>
#include "UpdaterTypes.h"

namespace Updater
{

/**
 * Downloads files from URLs with progress tracking
 */
class FileDownloader
{
public:
    FileDownloader();
    ~FileDownloader();

    /**
     * Download multiple files asynchronously
     * @param files List of files to download
     * @param tempDirectory Directory to download files to
     * @param progressCallback Called periodically with progress updates (on message thread)
     * @param completionCallback Called when download completes or fails (on message thread)
     */
    void downloadFiles(
        const juce::Array<FileInfo>&                          files,
        const juce::File&                                     tempDirectory,
        std::function<void(DownloadProgress)>                 progressCallback,
        std::function<void(bool success, juce::String error)> completionCallback);

    /**
     * Cancel ongoing download
     */
    void cancelDownload();

    /**
     * Check if download is in progress
     */
    bool isDownloading() const;

    /**
     * Get list of successfully downloaded files (call after completion)
     */
    juce::Array<FileInfo> getSuccessfulFiles() const;

private:
    class DownloadThread;
    std::unique_ptr<DownloadThread> downloadThread;
    std::atomic<bool>               shouldCancel{false};
    juce::Array<FileInfo>           lastSuccessfulFiles; // Cache of last successful downloads

    // Download a single file
    bool downloadFile(
        const FileInfo&                              fileInfo,
        const juce::File&                            destination,
        DownloadProgress&                            progress,
        const std::function<void(DownloadProgress)>& progressCallback);
};

} // namespace Updater
