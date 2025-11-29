#include "UpdateApplier.h"
#include "HashVerifier.h"

namespace Updater
{

UpdateApplier::UpdateApplier(VersionManager& versionManager) : versionManager(versionManager) {}

UpdateApplier::~UpdateApplier() {}

juce::File UpdateApplier::getInstallDirectory() const
{
    // Get directory where the executable is located
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
}

juce::File UpdateApplier::getBackupDirectory() const
{
    return versionManager.getVersionFile().getParentDirectory().getChildFile("backup");
}

bool UpdateApplier::backupFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return true; // Nothing to backup

    auto backupDir = getBackupDirectory();
    backupDir.createDirectory();

    auto backupFile = backupDir.getChildFile(file.getFileName());

    // Delete old backup if exists
    if (backupFile.exists())
        backupFile.deleteFile();

    return file.copyFileTo(backupFile);
}

bool UpdateApplier::replaceFile(const juce::File& source, const juce::File& destination)
{
    // Backup existing file
    if (!backupFile(destination))
    {
        DBG("Failed to backup file: " + destination.getFullPathName());
        return false;
    }

    // Create parent directory if needed
    destination.getParentDirectory().createDirectory();

    // Copy new file
    if (!source.copyFileTo(destination))
    {
        DBG("Failed to copy file: " + source.getFullPathName() + " -> " +
            destination.getFullPathName());
        return false;
    }

    return true;
}

bool UpdateApplier::applyUpdates(
    const juce::Array<FileInfo>& files,
    const juce::File&            tempDirectory,
    UpdateType                   type)
{
    auto installDir = getInstallDirectory();

    DBG("Applying " + juce::String(files.size()) + " updates...");
    DBG("Install directory: " + installDir.getFullPathName());
    DBG("Temp directory: " + tempDirectory.getFullPathName());

    if (type == UpdateType::Immediate)
    {
        // Apply non-critical files immediately
        for (const auto& fileInfo : files)
        {
            if (fileInfo.critical)
                continue; // Skip critical files

            auto source = tempDirectory.getChildFile(fileInfo.relativePath);
            auto destination = installDir.getChildFile(fileInfo.relativePath);

            DBG("Applying: " + fileInfo.relativePath);

            if (!source.existsAsFile())
            {
                DBG("Source file not found: " + source.getFullPathName());
                return false;
            }

            // Verify hash before applying
            if (!HashVerifier::verifyFile(source, fileInfo.sha256))
            {
                DBG("Hash verification failed for: " + fileInfo.relativePath);
                return false;
            }

            if (!replaceFile(source, destination))
            {
                DBG("Failed to replace file: " + fileInfo.relativePath);
                return false;
            }

            // Update version manager
            versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
        }

        // Save version info
        versionManager.saveVersionInfo();

        DBG("Immediate updates applied successfully");
        return true;
    }
    else // UpdateType::OnRestart
    {
        // Install files that can be updated immediately
        // SKIP the running executable - PikonUpdater will handle it after app exits
        
        auto runningExePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        auto runningExeName = runningExePath.getFileName();
        
        int filesApplied = 0;
        int filesSkipped = 0;

        for (const auto& fileInfo : files)
        {
            // Skip the running executable - it will be handled by PikonUpdater
            bool isRunningExe = (fileInfo.relativePath.equalsIgnoreCase(runningExeName) ||
                                installDir.getChildFile(fileInfo.relativePath) == runningExePath);
            
            if (isRunningExe)
            {
                DBG("Skipping running executable: " + fileInfo.relativePath + " (will be handled by PikonUpdater)");
                filesSkipped++;
                continue;
            }

            auto source = tempDirectory.getChildFile(fileInfo.relativePath);
            auto destination = installDir.getChildFile(fileInfo.relativePath);

            DBG("Applying file: " + fileInfo.relativePath +
                (fileInfo.critical ? " (critical)" : " (non-critical)"));

            if (!source.existsAsFile())
            {
                DBG("Source file not found: " + source.getFullPathName());
                return false;
            }

            // Verify hash before applying
            if (!HashVerifier::verifyFile(source, fileInfo.sha256))
            {
                DBG("Hash verification failed for: " + fileInfo.relativePath);
                return false;
            }

            // Copy to install directory
            if (!replaceFile(source, destination))
            {
                DBG("Failed to replace file: " + fileInfo.relativePath);
                return false;
            }

            // Update version manager
            versionManager.updateFileRecord(fileInfo.relativePath, fileInfo);
            filesApplied++;
        }

        // Save version info
        versionManager.saveVersionInfo();

        DBG("Files applied: " + juce::String(filesApplied) + 
            ", Files skipped (running EXE): " + juce::String(filesSkipped) +
            " (will be handled by PikonUpdater)");
        return true;
    }
}

bool UpdateApplier::deleteOldFiles(const juce::Array<juce::String>& filesToDelete)
{
    auto installDir = getInstallDirectory();

    for (const auto& relativePath : filesToDelete)
    {
        auto file = installDir.getChildFile(relativePath);

        if (file.existsAsFile())
        {
            DBG("Deleting old file: " + relativePath);

            // Backup before deleting
            backupFile(file);

            if (!file.deleteFile())
            {
                DBG("Failed to delete file: " + file.getFullPathName());
                return false;
            }

            // Remove from version manager
            versionManager.removeFileRecord(relativePath);
        }
    }

    versionManager.saveVersionInfo();
    return true;
}

bool UpdateApplier::createUpdateScript(
    const juce::File& tempDirectory,
    const juce::File& scriptFile)
{
    auto installDir = getInstallDirectory();
    auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    // Create batch script for Windows
    juce::String script;
    script << "@echo off\r\n";
    script << "echo Updating Pikon Raditsz...\r\n";
    script << "timeout /t 2 /nobreak > nul\r\n"; // Wait for app to exit
    script << "\r\n";

    // Copy all files from temp to install directory
    script << "xcopy /E /Y /I \"" << tempDirectory.getFullPathName() << "\" \""
           << installDir.getFullPathName() << "\"\r\n";
    script << "\r\n";

    // Restart application
    script << "start \"\" \"" << exePath.getFullPathName() << "\"\r\n";
    script << "\r\n";

    // Clean up
    script << "rd /s /q \"" << tempDirectory.getFullPathName() << "\"\r\n";
    script << "del \"%~f0\"\r\n"; // Delete this script

    return scriptFile.replaceWithText(script);
}

bool UpdateApplier::launchUpdaterAndExit(const juce::File& tempDirectory)
{
    // Create update script
    auto scriptFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("pikon_raditsz_update.bat");

    if (!createUpdateScript(tempDirectory, scriptFile))
    {
        DBG("Failed to create update script");
        return false;
    }

    DBG("Launching updater script: " + scriptFile.getFullPathName());

    // Launch script
    if (!scriptFile.startAsProcess())
    {
        DBG("Failed to launch updater script");
        return false;
    }

    // Exit application
    juce::JUCEApplicationBase::quit();

    return true;
}

bool UpdateApplier::rollbackUpdate()
{
    auto backupDir = getBackupDirectory();

    if (!backupDir.exists())
    {
        DBG("No backup found");
        return false;
    }

    auto installDir = getInstallDirectory();

    // Restore all backed up files
    auto backupFiles = backupDir.findChildFiles(juce::File::findFiles, false);

    for (const auto& backupFile : backupFiles)
    {
        auto destination = installDir.getChildFile(backupFile.getFileName());

        DBG("Restoring: " + backupFile.getFileName());

        if (!backupFile.copyFileTo(destination))
        {
            DBG("Failed to restore file: " + backupFile.getFileName());
            return false;
        }
    }

    // Clean up backup
    backupDir.deleteRecursively();

    DBG("Rollback completed successfully");
    return true;
}

} // namespace Updater
