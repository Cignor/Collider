/**
 * PikonUpdater.exe - Standalone update installer
 * Waits for main app to exit, copies files, verifies hashes, relaunches app
 */

#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <windows.h>
#include <iostream>

struct UpdateManifest
{
    struct FileEntry
    {
        juce::String relativePath;
        juce::String sha256;
        juce::int64 size;
    };
    
    juce::Array<FileEntry> files;
    
    static UpdateManifest fromJson(const juce::File& manifestFile)
    {
        UpdateManifest manifest;
        
        auto jsonString = manifestFile.loadFileAsString();
        auto json = juce::JSON::parse(jsonString);
        
        if (auto* obj = json.getDynamicObject())
        {
            if (auto filesObj = obj->getProperty("files"))
            {
                if (auto* filesDict = filesObj.getDynamicObject())
                {
                    for (auto& prop : filesDict->getProperties())
                    {
                        FileEntry entry;
                        entry.relativePath = prop.name.toString();
                        
                        if (auto* fileObj = prop.value.getDynamicObject())
                        {
                            entry.sha256 = fileObj->getProperty("sha256").toString();
                            entry.size = (juce::int64)fileObj->getProperty("size");
                        }
                        
                        manifest.files.add(entry);
                    }
                }
            }
        }
        
        return manifest;
    }
};

class PikonUpdater
{
public:
    PikonUpdater(int argc, char* argv[])
    {
        parseArguments(argc, argv);
    }
    
    int run()
    {
        std::cout << "Pikon Raditsz Updater v1.0\n";
        std::cout << "==========================\n\n";
        
        // Validate arguments
        if (!sourceDir.exists() || !destDir.exists() || !manifestFile.existsAsFile())
        {
            std::cerr << "ERROR: Invalid arguments\n";
            std::cerr << "  Source: " << sourceDir.getFullPathName() << "\n";
            std::cerr << "  Dest: " << destDir.getFullPathName() << "\n";
            std::cerr << "  Manifest: " << manifestFile.getFullPathName() << "\n";
            return 1;
        }
        
        // Step 1: Wait for main app to exit
        std::cout << "Waiting for application to exit...\n";
        if (!waitForProcessExit())
        {
            std::cerr << "ERROR: Application did not exit in time\n";
            return 2;
        }
        std::cout << "Application exited.\n\n";
        
        // Step 2: Load manifest
        std::cout << "Loading update manifest...\n";
        auto manifest = UpdateManifest::fromJson(manifestFile);
        if (manifest.files.isEmpty())
        {
            std::cerr << "ERROR: No files in manifest\n";
            return 3;
        }
        std::cout << "Found " << manifest.files.size() << " files to update.\n\n";
        
        // Step 3: Copy and verify files
        std::cout << "Copying files...\n";
        if (!copyFiles(manifest))
        {
            std::cerr << "ERROR: Failed to copy files\n";
            return 4;
        }
        std::cout << "All files copied successfully.\n\n";
        
        // Step 4: Cleanup temp folder
        std::cout << "Cleaning up...\n";
        cleanupTemp();
        
        // Step 5: Relaunch app
        std::cout << "Relaunching application...\n";
        if (!relaunchApp())
        {
            std::cerr << "WARNING: Failed to relaunch application\n";
            std::cerr << "Please start it manually: " 
                      << destDir.getChildFile(relaunchExe).getFullPathName() << "\n";
            return 5;
        }
        
        std::cout << "Update complete!\n";
        return 0;
    }
    
private:
    juce::File sourceDir;
    juce::File destDir;
    juce::File manifestFile;
    juce::String relaunchExe;
    DWORD waitPID = 0;
    int waitTimeout = 30000;
    
    void parseArguments(int argc, char* argv[])
    {
        for (int i = 1; i < argc; i++)
        {
            juce::String arg = argv[i];
            
            if (arg == "--source" && i + 1 < argc)
                sourceDir = juce::File(argv[++i]);
            else if (arg == "--dest" && i + 1 < argc)
                destDir = juce::File(argv[++i]);
            else if (arg == "--manifest" && i + 1 < argc)
                manifestFile = juce::File(argv[++i]);
            else if (arg == "--relaunch" && i + 1 < argc)
                relaunchExe = argv[++i];
            else if (arg == "--wait-pid" && i + 1 < argc)
                waitPID = (DWORD)juce::String(argv[++i]).getIntValue();
            else if (arg == "--wait-timeout" && i + 1 < argc)
                waitTimeout = juce::String(argv[++i]).getIntValue();
        }
    }
    
    bool waitForProcessExit()
    {
        if (waitPID == 0)
        {
            // Simple wait - app should have exited by now
            juce::Thread::sleep(2000);
            return true;
        }
        
        HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, waitPID);
        if (hProcess == NULL)
            return true; // Process already gone
        
        DWORD result = WaitForSingleObject(hProcess, waitTimeout);
        CloseHandle(hProcess);
        
        return (result == WAIT_OBJECT_0);
    }
    
    bool copyFiles(const UpdateManifest& manifest)
    {
        int fileNum = 0;
        for (const auto& entry : manifest.files)
        {
            fileNum++;
            std::cout << "[" << fileNum << "/" << manifest.files.size() << "] "
                      << entry.relativePath << "... ";
            
            auto sourceFile = sourceDir.getChildFile(entry.relativePath);
            auto destFile = destDir.getChildFile(entry.relativePath);
            
            if (!sourceFile.existsAsFile())
            {
                std::cerr << "ERROR: Source file not found\n";
                return false;
            }
            
            // Verify source file hash
            auto sourceHash = calculateSHA256(sourceFile);
            if (!sourceHash.equalsIgnoreCase(entry.sha256))
            {
                std::cerr << "ERROR: Source file hash mismatch\n";
                std::cerr << "  Expected: " << entry.sha256 << "\n";
                std::cerr << "  Got: " << sourceHash << "\n";
                return false;
            }
            
            // Create parent directory
            destFile.getParentDirectory().createDirectory();
            
            // Copy file (with retry for locked files)
            if (!copyFileWithRetry(sourceFile, destFile))
            {
                std::cerr << "ERROR: Failed to copy file\n";
                return false;
            }
            
            // Verify destination hash
            auto destHash = calculateSHA256(destFile);
            if (!destHash.equalsIgnoreCase(entry.sha256))
            {
                std::cerr << "ERROR: Destination file hash mismatch\n";
                return false;
            }
            
            std::cout << "OK\n";
        }
        
        return true;
    }
    
    bool copyFileWithRetry(const juce::File& src, const juce::File& dst, int maxAttempts = 3)
    {
        for (int attempt = 0; attempt < maxAttempts; attempt++)
        {
            if (src.copyFileTo(dst))
                return true;
            
            if (attempt < maxAttempts - 1)
            {
                juce::Thread::sleep(1000); // Wait 1 second
            }
        }
        
        return false;
    }
    
    juce::String calculateSHA256(const juce::File& file)
    {
        juce::FileInputStream stream(file);
        if (stream.failedToOpen())
            return {};
        
        juce::MemoryBlock data;
        stream.readIntoMemoryBlock(data);
        
        juce::SHA256 hasher(data);
        return hasher.toHexString().toLowerCase();
    }
    
    void cleanupTemp()
    {
        // Delete source directory
        sourceDir.deleteRecursively();
    }
    
    bool relaunchApp()
    {
        if (relaunchExe.isEmpty())
            return true;
        
        auto exePath = destDir.getChildFile(relaunchExe);
        return exePath.startAsProcess();
    }
};

int main(int argc, char* argv[])
{
    // No JUCE initializer needed for console app using only core/cryptography
    PikonUpdater updater(argc, argv);
    return updater.run();
}

