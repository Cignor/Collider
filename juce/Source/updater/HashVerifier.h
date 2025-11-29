#pragma once

#include <juce_core/juce_core.h>
#include "UpdaterTypes.h"

namespace Updater
{

/**
 * Utility class for calculating and verifying SHA256 hashes of files
 */
class HashVerifier
{
public:
    /**
     * Calculate SHA256 hash of a file
     * @param file File to hash
     * @return Lowercase hex string of SHA256 hash, or empty string on error
     */
    static juce::String calculateSHA256(const juce::File& file);

    /**
     * Verify a file matches expected hash
     * @param file File to verify
     * @param expectedHash Expected SHA256 hash (lowercase hex)
     * @return true if hash matches, false otherwise
     */
    static bool verifyFile(const juce::File& file, const juce::String& expectedHash);

    /**
     * Verify multiple downloaded files
     * @param files Array of file info with expected hashes
     * @param tempDir Directory containing downloaded files
     * @return true if all files verify successfully, false if any fail
     */
    static bool verifyDownloadedFiles(
        const juce::Array<FileInfo>& files,
        const juce::File&            tempDir);
};

} // namespace Updater
