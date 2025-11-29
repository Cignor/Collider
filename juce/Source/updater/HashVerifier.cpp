#include "HashVerifier.h"
#include <juce_cryptography/juce_cryptography.h>

namespace Updater
{

juce::String HashVerifier::calculateSHA256(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};

    juce::FileInputStream stream(file);
    if (stream.failedToOpen())
        return {};

    // Read entire file into memory block
    juce::MemoryBlock fileData;
    stream.readIntoMemoryBlock(fileData);

    // Calculate hash
    juce::SHA256 hasher(fileData);
    return hasher.toHexString().toLowerCase();
}

bool HashVerifier::verifyFile(const juce::File& file, const juce::String& expectedHash)
{
    auto actualHash = calculateSHA256(file);

    if (actualHash.isEmpty())
        return false;

    // Case-insensitive comparison
    return actualHash.equalsIgnoreCase(expectedHash);
}

bool HashVerifier::verifyDownloadedFiles(
    const juce::Array<FileInfo>& files,
    const juce::File&            tempDir)
{
    for (const auto& fileInfo : files)
    {
        auto filePath = tempDir.getChildFile(fileInfo.relativePath);

        if (!verifyFile(filePath, fileInfo.sha256))
        {
            DBG("Hash verification failed for: " + fileInfo.relativePath);
            DBG("Expected: " + fileInfo.sha256);
            DBG("Got: " + calculateSHA256(filePath));
            return false;
        }
    }

    return true;
}

} // namespace Updater
