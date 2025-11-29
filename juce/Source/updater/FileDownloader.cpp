#include "FileDownloader.h"

namespace Updater
{

// Helper thread for async file downloading
class FileDownloader::DownloadThread : public juce::Thread
{
public:
    DownloadThread(
        FileDownloader*                         downloader,
        juce::Array<FileInfo>                   files,
        juce::File                              tempDir,
        std::function<void(DownloadProgress)>   progressCallback,
        std::function<void(bool, juce::String)> completionCallback)
        : juce::Thread("FileDownloadThread"), downloader(downloader), files(std::move(files)),
          tempDir(tempDir), progressCallback(std::move(progressCallback)),
          completionCallback(std::move(completionCallback))
    {
    }

    void run() override
    {
        DownloadProgress progress;
        progress.totalFiles = files.size();
        progress.totalBytes = 0;

        // Calculate total size
        for (const auto& file : files)
            progress.totalBytes += file.size;

        bool         success = true;
        juce::String errorMessage;

        // Download each file
        for (int i = 0; i < files.size(); ++i)
        {
            if (threadShouldExit())
            {
                success = false;
                errorMessage = "Download cancelled by user";
                break;
            }

            const auto& fileInfo = files[i];
            progress.currentFile = fileInfo.relativePath;
            progress.filesCompleted = i;

            auto destination = tempDir.getChildFile(fileInfo.relativePath);

            if (!downloader->downloadFile(fileInfo, destination, progress, progressCallback))
            {
                success = false;
                errorMessage = "Failed to download: " + fileInfo.relativePath;
                break;
            }

            progress.filesCompleted = i + 1;
        }

        // Call completion callback on message thread
        juce::MessageManager::callAsync([this, success, errorMessage]() {
            if (completionCallback)
                completionCallback(success, errorMessage);
        });
    }

private:
    FileDownloader*                         downloader;
    juce::Array<FileInfo>                   files;
    juce::File                              tempDir;
    std::function<void(DownloadProgress)>   progressCallback;
    std::function<void(bool, juce::String)> completionCallback;
};

FileDownloader::FileDownloader() {}

FileDownloader::~FileDownloader() { cancelDownload(); }

void FileDownloader::downloadFiles(
    const juce::Array<FileInfo>&                          files,
    const juce::File&                                     tempDirectory,
    std::function<void(DownloadProgress)>                 progressCallback,
    std::function<void(bool success, juce::String error)> completionCallback)
{
    cancelDownload();

    shouldCancel = false;
    downloadThread = std::make_unique<DownloadThread>(
        this, files, tempDirectory, std::move(progressCallback), std::move(completionCallback));

    downloadThread->startThread();
}

void FileDownloader::cancelDownload()
{
    shouldCancel = true;

    if (downloadThread != nullptr)
    {
        downloadThread->stopThread(5000);
        downloadThread.reset();
    }
}

bool FileDownloader::isDownloading() const
{
    return downloadThread != nullptr && downloadThread->isThreadRunning();
}

bool FileDownloader::downloadFile(
    const FileInfo&                              fileInfo,
    const juce::File&                            destination,
    DownloadProgress&                            progress,
    const std::function<void(DownloadProgress)>& progressCallback)
{
    DBG("Downloading: " + fileInfo.relativePath + " from " + fileInfo.url);
    juce::Logger::writeToLog("Attempting to download: " + fileInfo.url);
    juce::Logger::writeToLog("Target temp file: " + destination.getFullPathName());

    // Create parent directories
    auto parentDir = destination.getParentDirectory();
    if (!parentDir.createDirectory())
    {
        juce::String err = "Failed to create parent directory: " + parentDir.getFullPathName();
        DBG(err);
        juce::Logger::writeToLog(err);
        return false;
    }

    // Delete existing temp file if it exists (from previous failed attempts)
    if (destination.exists())
    {
        juce::Logger::writeToLog(
            "FileDownloader: Deleting existing temp file: " + destination.getFullPathName());
        destination.deleteFile();
    }

    // Create URL
    juce::URL url(fileInfo.url);

    // Open stream with timeout and User-Agent
    auto stream = url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(30000)
            .withNumRedirectsToFollow(5)
            .withExtraHeaders("User-Agent: PikonRaditsz-Updater/1.0\r\n"));

    if (stream == nullptr)
    {
        juce::String err = "Failed to connect to: " + fileInfo.url + " (Stream is null)";
        DBG(err);
        juce::Logger::writeToLog(err);
        return false;
    }

    // Check HTTP status code if possible
    if (auto* webStream = dynamic_cast<juce::WebInputStream*>(stream.get()))
    {
        int statusCode = webStream->getStatusCode();
        if (statusCode != 200)
        {
            juce::String err = "HTTP Error " + juce::String(statusCode) + " for: " + fileInfo.url;
            DBG(err);
            juce::Logger::writeToLog(err);
            return false;
        }
    }

    // Create output file
    juce::FileOutputStream output(destination);
    if (output.failedToOpen())
    {
        juce::String err = "Failed to open output file: " + destination.getFullPathName();
        DBG(err);
        juce::Logger::writeToLog(err);
        return false;
    }

    // Download in chunks
    const int             bufferSize = 8192;
    juce::HeapBlock<char> buffer(bufferSize);
    juce::int64           bytesDownloadedThisFile = 0;
    juce::int64           startBytesDownloaded = progress.bytesDownloaded;
    auto                  startTime = juce::Time::getMillisecondCounterHiRes();

    juce::Logger::writeToLog(
        "FileDownloader: Starting download loop for " + fileInfo.relativePath +
        ", expected size: " + juce::String(fileInfo.size));

    int       loopCount = 0;
    int       consecutiveZeroReads = 0;
    const int maxConsecutiveZeroReads =
        100; // Exit after 100 consecutive zero-byte reads (1 second)

    while (!stream->isExhausted() && !shouldCancel)
    {
        auto bytesRead = stream->read(buffer.get(), bufferSize);
        loopCount++;

        if (loopCount == 1)
            juce::Logger::writeToLog(
                "FileDownloader: First read returned " + juce::String(bytesRead) + " bytes");

        if (bytesRead > 0)
        {
            consecutiveZeroReads = 0; // Reset counter on successful read
            output.write(buffer.get(), (size_t)bytesRead);
            bytesDownloadedThisFile += bytesRead;
            progress.bytesDownloaded = startBytesDownloaded + bytesDownloadedThisFile;

            // Calculate speed
            auto elapsed = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;
            if (elapsed > 0)
                progress.speedBytesPerSec = bytesDownloadedThisFile / elapsed;

            // Report progress periodically (every 8KB for smooth updates)
            if (bytesDownloadedThisFile % (8 * 1024) < bufferSize ||
                bytesDownloadedThisFile == fileInfo.size)
            {
                // Update UI asynchronously - copy progress by value
                auto currentProgress = progress;
                juce::MessageManager::callAsync([currentProgress, &progressCallback]() {
                    if (progressCallback)
                        progressCallback(currentProgress);
                });
            }

            // Check if we've downloaded the expected amount
            if (bytesDownloadedThisFile >= fileInfo.size)
            {
                juce::Logger::writeToLog("FileDownloader: Downloaded expected size, exiting loop");
                break;
            }
        }
        else if (bytesRead == 0)
        {
            consecutiveZeroReads++;

            // Exit if we get too many consecutive zero-byte reads
            if (consecutiveZeroReads >= maxConsecutiveZeroReads)
            {
                juce::Logger::writeToLog(
                    "FileDownloader: Too many consecutive zero-byte reads, exiting loop");
                break;
            }

            // Log first few zero-byte reads
            if (consecutiveZeroReads <= 5)
                juce::Logger::writeToLog(
                    "FileDownloader: Read returned 0 bytes (consecutive=" +
                    juce::String(consecutiveZeroReads) + "), downloaded=" +
                    juce::String(bytesDownloadedThisFile) + "/" + juce::String(fileInfo.size));

            // Small delay to avoid busy-waiting
            juce::Thread::sleep(10);
        }
    }

    output.flush();

    if (shouldCancel)
    {
        destination.deleteFile();
        return false;
    }

    // Verify file size
    if (destination.getSize() != fileInfo.size)
    {
        juce::String err = "File size mismatch! Expected: " + juce::String(fileInfo.size) +
                           ", Got: " + juce::String(destination.getSize()) + " for " +
                           fileInfo.relativePath;
        DBG(err);
        juce::Logger::writeToLog(err);

        // Read and log the content of the error page
        juce::File        errorFile = destination;
        juce::MemoryBlock content;
        errorFile.loadFileAsData(content);
        juce::String errorText = content.toString().substring(0, 1024); // First 1KB
        juce::Logger::writeToLog("Server Response Content (Start):");
        juce::Logger::writeToLog(errorText);

        destination.deleteFile();
        return false;
    }

    DBG("Downloaded successfully: " + fileInfo.relativePath);
    return true;
}

} // namespace Updater
