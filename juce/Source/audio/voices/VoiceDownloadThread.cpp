#include "VoiceDownloadThread.h"
#include "VoiceDownloadHelper.h"
#include <juce_core/juce_core.h>

VoiceDownloadThread::VoiceDownloadThread()
    : juce::Thread("Voice Download Thread"),
      downloadQueue(64),
      downloadQueueBuffer(64)
{
}

VoiceDownloadThread::~VoiceDownloadThread()
{
    stopThread(5000);
}

void VoiceDownloadThread::run()
{
    juce::Logger::writeToLog("[VoiceDownloadThread] Thread started");
    
    while (!threadShouldExit())
    {
        // Check if there are downloads in the queue
        if (downloadQueue.getNumReady() == 0)
        {
            wait(-1);
            continue;
        }
        
        // Dequeue a voice name
        juce::String voiceName;
        {
            const juce::ScopedLock lock(queueLock);
            int start1, size1, start2, size2;
            downloadQueue.prepareToRead(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                voiceName = downloadQueueBuffer[start1];
                downloadQueue.finishedRead(1);
            }
        }
        
        if (voiceName.isEmpty() || threadShouldExit())
        {
            isDownloading_.store(false);
            continue;
        }
        
        // Start downloading
        isDownloading_.store(true);
        shouldCancel.store(false);
        progress.store(0.0f);
        
        {
            const juce::ScopedLock lock(statusLock);
            currentVoice = voiceName;
            statusMessage = "Starting download: " + voiceName;
        }
        
        juce::Logger::writeToLog("[VoiceDownloadThread] Starting download: " + voiceName);
        
        // Download the voice
        bool success = downloadSingleVoice(voiceName);
        
        if (success && !shouldCancel.load())
        {
            setStatusMessage("Download complete: " + voiceName);
            progress.store(1.0f);
            juce::Logger::writeToLog("[VoiceDownloadThread] Download complete: " + voiceName);
        }
        else if (shouldCancel.load())
        {
            setStatusMessage("Download cancelled: " + voiceName);
            progress.store(0.0f);  // Reset progress on cancel (not an error)
            juce::Logger::writeToLog("[VoiceDownloadThread] Download cancelled: " + voiceName);
        }
        else
        {
            // Download failed - keep the detailed error message from downloadSingleVoice
            // Don't overwrite it with a generic "Download failed" message
            if (progress.load() >= 0.0f)
            {
                progress.store(-1.0f);  // Mark as error only if not already set
            }
            juce::Logger::writeToLog("[VoiceDownloadThread] Download failed: " + voiceName);
        }
        
        // Small delay before next download (if any)
        wait(100);
        isDownloading_.store(false);
    }
    
    juce::Logger::writeToLog("[VoiceDownloadThread] Thread exiting");
}

void VoiceDownloadThread::downloadVoice(const juce::String& voiceName)
{
    if (voiceName.isEmpty()) return;
    
    const juce::ScopedLock lock(queueLock);
    int start1, size1, start2, size2;
    downloadQueue.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 > 0)
    {
        downloadQueueBuffer[start1] = voiceName;
        downloadQueue.finishedWrite(1);
        notify();
    }
}

void VoiceDownloadThread::downloadBatch(const std::vector<juce::String>& voices)
{
    const juce::ScopedLock lock(queueLock);
    for (const auto& voice : voices)
    {
        if (voice.isEmpty()) continue;
        
        int start1, size1, start2, size2;
        downloadQueue.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 > 0)
        {
            downloadQueueBuffer[start1] = voice;
            downloadQueue.finishedWrite(1);
        }
    }
    notify();
}

void VoiceDownloadThread::cancelCurrentDownload()
{
    shouldCancel.store(true);
    setStatusMessage("Cancelling download...");
}

juce::String VoiceDownloadThread::getCurrentVoice() const
{
    const juce::ScopedLock lock(statusLock);
    return currentVoice;
}

juce::String VoiceDownloadThread::getStatusMessage() const
{
    const juce::ScopedLock lock(statusLock);
    return statusMessage;
}

void VoiceDownloadThread::setStatusMessage(const juce::String& message)
{
    const juce::ScopedLock lock(statusLock);
    statusMessage = message;
}

juce::URL VoiceDownloadThread::buildVoiceUrl(const juce::String& voiceName, bool isOnnx)
{
    // Build URL using the nested path structure from HuggingFace repository
    // Format: "en_US-lessac-medium" -> "{lang}/{locale}/{voice}/{quality}/{voiceName}.onnx"
    // Example: "en_US-lessac-medium" -> "en/en_US/lessac/medium/en_US-lessac-medium.onnx"
    // This matches the structure in voices.json and the local file system
    
    // Parse voice name to extract components (same logic as checkVoiceStatus)
    int lastDash = voiceName.lastIndexOfChar('-');
    if (lastDash < 0)
    {
        juce::Logger::writeToLog("[VoiceDownloadThread] Invalid voice name format (no dashes): " + voiceName);
        // Fallback to flat structure
        juce::String filename = voiceName;
        if (isOnnx)
            filename += ".onnx";
        else
            filename += ".onnx.json";
        return juce::URL(juce::String(BASE_URL) + "/" + filename);
    }
    
    juce::String beforeLastDash = voiceName.substring(0, lastDash);
    int secondLastDash = beforeLastDash.lastIndexOfChar('-');
    if (secondLastDash < 0)
    {
        juce::Logger::writeToLog("[VoiceDownloadThread] Invalid voice name format (need at least 2 dashes): " + voiceName);
        // Fallback to flat structure
        juce::String filename = voiceName;
        if (isOnnx)
            filename += ".onnx";
        else
            filename += ".onnx.json";
        return juce::URL(juce::String(BASE_URL) + "/" + filename);
    }
    
    juce::String locale = voiceName.substring(0, secondLastDash);  // "en_US"
    // Extract voice name between secondLastDash+1 and lastDash (same logic as checkVoiceStatus)
    // JUCE substring(start, end) extracts from start to end (exclusive of end), so we need to adjust
    juce::String afterSecondDash = voiceName.substring(secondLastDash + 1);  // Everything after second dash
    int voiceEndPos = afterSecondDash.indexOfChar('-');  // Position of quality dash in substring
    juce::String voice = (voiceEndPos > 0) ? afterSecondDash.substring(0, voiceEndPos) : voiceName.substring(secondLastDash + 1, lastDash);
    juce::String quality = voiceName.substring(lastDash + 1);  // "medium"
    
    // Extract language code from locale (e.g., "en_US" -> "en")
    juce::String lang = locale.substring(0, locale.indexOfChar('_'));
    if (lang.isEmpty()) lang = locale;  // Fallback if no underscore
    
    // Build nested path: {lang}/{locale}/{voice}/{quality}/{voiceName}.onnx[.json]
    juce::String filename = voiceName;
    if (isOnnx)
        filename += ".onnx";
    else
        filename += ".onnx.json";
    
    juce::String nestedPath = lang + "/" + locale + "/" + voice + "/" + quality + "/" + filename;
    juce::String fullPath = juce::String(BASE_URL) + "/" + nestedPath;
    
    return juce::URL(fullPath);
}

bool VoiceDownloadThread::downloadSingleVoice(const juce::String& voiceName)
{
    setStatusMessage("Resolving paths for: " + voiceName);
    
    // Get models directory
    juce::File modelsDir = VoiceDownloadHelper::resolveModelsBaseDir();
    if (!modelsDir.exists())
    {
        if (!modelsDir.createDirectory())
        {
            setStatusMessage("Failed to create models directory");
            return false;
        }
    }
    
    // Parse voice name to construct directory structure
    // Format: "en_US-lessac-medium" -> "piper-voices/en/en_US/lessac/medium/"
    int lastDash = voiceName.lastIndexOfChar('-');
    if (lastDash < 0)
    {
        setStatusMessage("Invalid voice name format");
        return false;
    }
    
    juce::String beforeLastDash = voiceName.substring(0, lastDash);
    int secondLastDash = beforeLastDash.lastIndexOfChar('-');
    if (secondLastDash < 0)
    {
        setStatusMessage("Invalid voice name format");
        return false;
    }
    
    juce::String locale = voiceName.substring(0, secondLastDash);  // "en_US"
    juce::String voice = voiceName.substring(secondLastDash + 1, lastDash);  // "lessac"
    juce::String quality = voiceName.substring(lastDash + 1);  // "medium"
    
    juce::String lang = locale.substring(0, locale.indexOfChar('_'));
    if (lang.isEmpty()) lang = locale;
    
    // Build target directory path
    juce::File targetDir = modelsDir.getChildFile("piper-voices")
                                .getChildFile(lang)
                                .getChildFile(locale)
                                .getChildFile(voice)
                                .getChildFile(quality);
    
    if (!targetDir.exists())
    {
        if (!targetDir.createDirectory())
        {
            setStatusMessage("Failed to create voice directory: " + targetDir.getFullPathName());
            return false;
        }
    }
    
    // Download ONNX file
    juce::File onnxFile = targetDir.getChildFile(voiceName + ".onnx");
    juce::URL onnxUrl = buildVoiceUrl(voiceName, true);
    
    setStatusMessage("Downloading ONNX file: " + voiceName + ".onnx");
    juce::Logger::writeToLog("[VoiceDownloadThread] Downloading from: " + onnxUrl.toString(true));
    
    // Download ONNX file using WebInputStream for progress tracking
    {
        juce::File tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getNonexistentChildFile("piper_voice_", ".onnx", true);
        
        std::unique_ptr<juce::InputStream> inputStream(onnxUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)));
        
        if (!inputStream)
        {
            setStatusMessage("Download failed: Unable to connect to server. Please check your internet connection and try again.");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: ONNX download connection failed - unable to create input stream");
            return false;
        }
        
        // Note: We can't easily get total file size without WebInputStream definition
        // So we'll track progress based on bytes downloaded without total size
        
        // Create output file
        std::unique_ptr<juce::FileOutputStream> out(tempFile.createOutputStream());
        if (!out || !out->openedOk())
        {
            setStatusMessage("Download failed: Unable to create temporary file. Please check disk space and permissions.");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Failed to create temp file: " + tempFile.getFullPathName());
            return false;
        }
        
        // Download in chunks
        char buffer[8192];
        juce::int64 downloaded = 0;
        bool success = true;
        
        setStatusMessage("Downloading ONNX: " + voiceName);
        
        while (!inputStream->isExhausted() && !shouldCancel.load() && success)
        {
            int bytesRead = inputStream->read(buffer, sizeof(buffer));
            if (bytesRead <= 0)
            {
                // Stream might be exhausted or there's an error
                if (inputStream->isExhausted())
                {
                    juce::Logger::writeToLog("[VoiceDownloadThread] Stream exhausted normally after " + juce::String(downloaded) + " bytes");
                    break;  // Normal end of stream
                }
                else
                {
                    // Error reading from stream
                    juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Failed to read from stream after " + juce::String(downloaded) + " bytes");
                    success = false;
                    break;
                }
            }
            
            if (!out->write(buffer, bytesRead))
            {
                juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Failed to write to temp file");
                success = false;
                break;
            }
            
            downloaded += bytesRead;
            
            // Update progress (0.0 to 0.5 for ONNX file)
            // Since we don't have total size, estimate based on typical voice file size (~60MB)
            // Progress will jump to 0.5 when file completes
            if (downloaded > 0)
            {
                // Estimate: typical voice file is ~60MB, so approximate progress
                float estimatedProgress = juce::jmin(0.5f, (float)downloaded / (60.0f * 1024.0f * 1024.0f) * 0.5f);
                progress.store(estimatedProgress);
            }
        }
        
        out.reset();  // Close file stream (this flushes any remaining data)
        
        if (shouldCancel.load())
        {
            setStatusMessage("Download cancelled by user");
            juce::Logger::writeToLog("[VoiceDownloadThread] Download cancelled. Downloaded: " + juce::String(downloaded) + " bytes");
            tempFile.deleteFile();
            return false;
        }
        
        if (!success)
        {
            setStatusMessage("Download failed: stream error");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Download failed - stream error. Downloaded: " + 
                                     juce::String(downloaded) + " bytes. File will be deleted.");
            tempFile.deleteFile();
            return false;
        }
        
        // Warn if stream wasn't exhausted (might indicate incomplete download)
        // But don't fail here - let file size validation catch it
        if (!inputStream->isExhausted() && downloaded > 0)
        {
            juce::Logger::writeToLog("[VoiceDownloadThread] WARNING: Stream not exhausted, but " + 
                                     juce::String(downloaded) + " bytes downloaded. Will validate file size.");
        }
        
        // Ensure file is fully written to disk before checking size
        juce::Thread::getCurrentThread()->sleep(100);  // Slightly longer delay to ensure file system sync
        
        // Verify file is not empty and has reasonable size
        juce::int64 fileSize = tempFile.getSize();
        const juce::int64 MIN_ONNX_SIZE = 1024 * 1024; // 1 MB minimum (valid models are 60-120 MB)
        
        juce::Logger::writeToLog("[VoiceDownloadThread] Downloaded ONNX file size: " + juce::String(fileSize) + " bytes");
        
        if (fileSize == 0)
        {
            setStatusMessage("Download failed: File is empty. This usually means the download was interrupted or the server returned an error. Please try again.");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Downloaded ONNX file is empty");
            tempFile.deleteFile();
            return false;
        }
        
        if (fileSize < MIN_ONNX_SIZE)
        {
            juce::String sizeStr = (fileSize < 1024) ? juce::String(fileSize) + " bytes" :
                                   (fileSize < 1024 * 1024) ? juce::String::formatted("%.1f KB", fileSize / 1024.0) :
                                   juce::String::formatted("%.2f MB", fileSize / (1024.0 * 1024.0));
            
            juce::String errorMsg = "Download failed: File is corrupted (" + sizeStr + 
                                   "). Expected at least 1 MB. The incomplete file has been deleted.";
            setStatusMessage(errorMsg);
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Downloaded ONNX file is too small (corrupted/incomplete): " + 
                                     juce::String(fileSize) + " bytes (expected at least " + juce::String(MIN_ONNX_SIZE) + " bytes)");
            juce::Logger::writeToLog("[VoiceDownloadThread] File will be deleted. Please retry the download.");
            tempFile.deleteFile();
            return false;
        }
        
        juce::Logger::writeToLog("[VoiceDownloadThread] ONNX file size validated: " + juce::String(fileSize) + " bytes");
        
        // Move to final location (atomic operation)
        if (onnxFile.exists())
            onnxFile.deleteFile();
        
        if (!tempFile.moveFileTo(onnxFile))
        {
            setStatusMessage("Failed to move file to final location");
            tempFile.deleteFile();
            return false;
        }
        
        // Small delay after move to ensure file system has updated
        juce::Thread::getCurrentThread()->sleep(50);
        
        // Verify final file size after move
        if (onnxFile.getSize() != fileSize)
        {
            juce::Logger::writeToLog("[VoiceDownloadThread] WARNING: File size changed after move! Expected: " + 
                                     juce::String(fileSize) + ", Got: " + juce::String(onnxFile.getSize()));
        }
        
        progress.store(0.5f);  // ONNX file downloaded (50% progress)
        juce::Logger::writeToLog("[VoiceDownloadThread] ONNX file downloaded and validated: " + onnxFile.getFullPathName() + 
                                 " (" + juce::String(onnxFile.getSize()) + " bytes)");
    }
    
    // Check for cancellation before JSON download
    if (shouldCancel.load())
    {
        onnxFile.deleteFile();  // Clean up partial download
        return false;
    }
    
    // Download JSON config file using WebInputStream
    juce::File jsonFile = targetDir.getChildFile(voiceName + ".onnx.json");
    juce::URL jsonUrl = buildVoiceUrl(voiceName, false);
    
    setStatusMessage("Downloading JSON config: " + voiceName + ".onnx.json");
    
    {
        juce::File tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getNonexistentChildFile("piper_voice_", ".json", true);
        
        std::unique_ptr<juce::InputStream> inputStream(jsonUrl.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)));
        
        if (!inputStream)
        {
            setStatusMessage("JSON download connection failed");
            juce::Logger::writeToLog("[VoiceDownloadThread] JSON download connection failed");
            // Don't delete ONNX file - partial download is better than nothing
            return false;
        }
        
        // Note: We can't easily get total file size without WebInputStream definition
        // So we'll track progress based on bytes downloaded without total size
        
        std::unique_ptr<juce::FileOutputStream> out(tempFile.createOutputStream());
        if (!out || !out->openedOk())
        {
            setStatusMessage("Failed to create JSON output file");
            juce::Logger::writeToLog("[VoiceDownloadThread] Failed to create JSON temp file");
            return false;
        }
        
        // Download in chunks
        char buffer[8192];
        juce::int64 downloaded = 0;
        bool success = true;
        
        while (!inputStream->isExhausted() && !shouldCancel.load() && success)
        {
            int bytesRead = inputStream->read(buffer, sizeof(buffer));
            if (bytesRead <= 0)
            {
                // Stream might be exhausted or there's an error
                if (inputStream->isExhausted())
                {
                    juce::Logger::writeToLog("[VoiceDownloadThread] JSON stream exhausted normally after " + juce::String(downloaded) + " bytes");
                    break;  // Normal end of stream
                }
                else
                {
                    // Error reading from stream
                    juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Failed to read JSON stream after " + juce::String(downloaded) + " bytes");
                    success = false;
                    break;
                }
            }
            
            if (!out->write(buffer, bytesRead))
            {
                juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Failed to write JSON to temp file");
                success = false;
                break;
            }
            
            downloaded += bytesRead;
            
            // Update progress (0.5 to 1.0 for JSON file)
            // Since we don't have total size, estimate based on typical JSON file size (~5KB)
            // Progress will jump to 1.0 when file completes
            if (downloaded > 0)
            {
                // Estimate: typical JSON file is ~5KB, so progress from 0.5 to 1.0
                float estimatedProgress = juce::jmin(1.0f, 0.5f + ((float)downloaded / (5.0f * 1024.0f)) * 0.5f);
                progress.store(estimatedProgress);
            }
        }
        
        out.reset();  // Close file stream (this flushes any remaining data)
        
        if (shouldCancel.load())
        {
            setStatusMessage("JSON download cancelled by user");
            juce::Logger::writeToLog("[VoiceDownloadThread] JSON download cancelled. Downloaded: " + juce::String(downloaded) + " bytes");
            tempFile.deleteFile();
            return false;
        }
        
        if (!success)
        {
            setStatusMessage("JSON download failed: stream error");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: JSON download failed - stream error. Downloaded: " + 
                                     juce::String(downloaded) + " bytes. File will be deleted.");
            juce::Logger::writeToLog("[VoiceDownloadThread] WARNING: ONNX file downloaded but JSON failed. Voice may not work properly.");
            tempFile.deleteFile();
            // Don't delete ONNX file - partial download is better than nothing
            return false;
        }
        
        // Ensure file is fully written to disk before checking size
        juce::Thread::getCurrentThread()->sleep(50);  // Small delay to ensure file system sync
        
        // Verify JSON file is not empty and has reasonable size
        juce::int64 jsonFileSize = tempFile.getSize();
        const juce::int64 MIN_JSON_SIZE = 1000; // 1 KB minimum (valid configs are typically 4-8 KB)
        
        if (jsonFileSize == 0)
        {
            setStatusMessage("Downloaded JSON file is empty");
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: Downloaded JSON file is empty");
            tempFile.deleteFile();
            return false;
        }
        
        if (jsonFileSize < MIN_JSON_SIZE)
        {
            juce::String errorMsg = "Downloaded JSON file is too small (corrupted/incomplete): " + juce::String(jsonFileSize) + 
                                   " bytes (expected at least " + juce::String(MIN_JSON_SIZE) + " bytes)";
            setStatusMessage(errorMsg);
            juce::Logger::writeToLog("[VoiceDownloadThread] ERROR: " + errorMsg);
            juce::Logger::writeToLog("[VoiceDownloadThread] File will be deleted. Please retry the download.");
            tempFile.deleteFile();
            return false;
        }
        
        juce::Logger::writeToLog("[VoiceDownloadThread] JSON file size validated: " + juce::String(jsonFileSize) + " bytes");
        
        if (jsonFile.exists())
            jsonFile.deleteFile();
        
        if (!tempFile.moveFileTo(jsonFile))
        {
            setStatusMessage("Failed to move JSON file");
            tempFile.deleteFile();
            return false;
        }
        
        // Small delay after move to ensure file system has updated
        juce::Thread::getCurrentThread()->sleep(50);
        
        // Verify final file size after move
        if (jsonFile.getSize() != jsonFileSize)
        {
            juce::Logger::writeToLog("[VoiceDownloadThread] WARNING: JSON file size changed after move! Expected: " + 
                                     juce::String(jsonFileSize) + ", Got: " + juce::String(jsonFile.getSize()));
        }
        
        progress.store(1.0f);  // Both files downloaded (100% progress)
        juce::Logger::writeToLog("[VoiceDownloadThread] JSON file downloaded and validated: " + jsonFile.getFullPathName() + 
                                 " (" + juce::String(jsonFile.getSize()) + " bytes)");
    }
    
    // Optional: Download MODEL_CARD and ALIASES if they exist (metadata files - not required for synthesis)
    // These are informational files from HuggingFace, but the TTS module doesn't need them
    // Uncomment below if you want to download them for completeness:
    /*
    juce::File modelCardFile = targetDir.getChildFile("MODEL_CARD");
    juce::URL modelCardUrl = buildVoiceUrl(voiceName, false).withNewSubPath("MODEL_CARD");
    juce::File aliasesFile = targetDir.getChildFile("ALIASES");
    juce::URL aliasesUrl = buildVoiceUrl(voiceName, false).withNewSubPath("ALIASES");
    // Download these if needed (they're optional metadata)
    */
    
    return true;
}

