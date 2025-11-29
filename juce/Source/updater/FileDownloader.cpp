#include "FileDownloader.h"
#include "HashVerifier.h"
#include <cmath>

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

    // Create URL (JUCE automatically handles URL encoding)
    juce::URL url(fileInfo.url);
    
    // Log the URL being used (for debugging)
    juce::Logger::writeToLog("FileDownloader: Constructed URL: " + url.toString(true));

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
    { // Scope for output stream - ensures it closes before verification
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

                // Don't exit early - wait for stream to be exhausted
                // The hash verification will catch any size mismatches
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
        
        // Log final download size
        juce::Logger::writeToLog(
            "FileDownloader: Download complete. Downloaded: " + 
            juce::String(bytesDownloadedThisFile) + " bytes, Expected: " + 
            juce::String(fileInfo.size) + " bytes, Stream exhausted: " + 
            (stream->isExhausted() ? "yes" : "no"));

        output.flush();
    } // Output stream closes here

    if (shouldCancel)
    {
        destination.deleteFile();
        return false;
    }

    // Verify file size (warning only - hash is definitive)
    if (destination.getSize() != fileInfo.size)
    {
        juce::String warn = "File size mismatch (will verify hash): Expected: " + 
                           juce::String(fileInfo.size) + ", Got: " + 
                           juce::String(destination.getSize()) + " for " +
                           fileInfo.relativePath;
        DBG(warn);
        juce::Logger::writeToLog(warn);
        
        // If size is way off (more than 10% difference), it's likely an error page
        auto sizeDiff = std::abs(destination.getSize() - fileInfo.size);
        auto sizePercent = (fileInfo.size > 0) ? (100.0 * sizeDiff / fileInfo.size) : 100.0;
        
        if (sizePercent > 10.0)
        {
            juce::String err = "File size differs by more than 10% - likely download error for: " + 
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
    }

    // Verify hash (definitive check)
    if (!HashVerifier::verifyFile(destination, fileInfo.sha256))
    {
        juce::String err = "Hash verification failed for: " + fileInfo.relativePath;
        DBG(err);
        juce::Logger::writeToLog(err);
        
        // Calculate and log actual hash for debugging
        auto actualHash = HashVerifier::calculateSHA256(destination);
        auto actualSize = destination.getSize();
        juce::Logger::writeToLog("Expected hash: " + fileInfo.sha256);
        juce::Logger::writeToLog("Actual hash:   " + actualHash);
        juce::Logger::writeToLog("Expected size: " + juce::String(fileInfo.size));
        juce::Logger::writeToLog("Actual size:   " + juce::String(actualSize));
        juce::Logger::writeToLog("Download URL:  " + fileInfo.url);
        
        // More helpful error message
        err = "Downloaded file doesn't match manifest. The file on the server may be different from what's in the manifest. Please check:\n";
        err += "1. The file on the server matches the manifest hash\n";
        err += "2. The URL is correct: " + fileInfo.url + "\n";
        err += "3. The manifest has the correct hash for the current server file";
        
        destination.deleteFile();
        return false;
    }

    DBG("Downloaded and verified successfully: " + fileInfo.relativePath);
    return true;
}

} // namespace Updater
