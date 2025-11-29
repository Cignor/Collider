#include "VersionManager.h"

namespace Updater
{

VersionManager::VersionManager()
    : currentVersion("0.6.2") // Default version
      ,
      currentVariant("cuda") // Default variant
{
    loadVersionInfo();
}

VersionManager::~VersionManager() { saveVersionInfo(); }

juce::String VersionManager::getCurrentVersion() const { return currentVersion; }

juce::String VersionManager::getCurrentVariant() const { return currentVariant; }

const juce::HashMap<juce::String, InstalledFileInfo>& VersionManager::getInstalledFiles() const
{
    return installedFiles;
}

InstalledFileInfo VersionManager::getFileInfo(const juce::String& relativePath) const
{
    if (installedFiles.contains(relativePath))
        return installedFiles[relativePath];

    return InstalledFileInfo();
}

bool VersionManager::hasFile(const juce::String& relativePath) const
{
    return installedFiles.contains(relativePath);
}

void VersionManager::updateFileRecord(const juce::String& relativePath, const FileInfo& info)
{
    InstalledFileInfo installed;
    installed.version = info.version;
    installed.sha256 = info.sha256;
    installed.installedDate = juce::Time::getCurrentTime();

    installedFiles.set(relativePath, installed);
}

void VersionManager::removeFileRecord(const juce::String& relativePath)
{
    installedFiles.remove(relativePath);
}

void VersionManager::setCurrentVersion(const juce::String& version) { currentVersion = version; }

void VersionManager::setCurrentVariant(const juce::String& variant) { currentVariant = variant; }

juce::File VersionManager::getAppDataDirectory() const
{
    // Use JUCE's application data directory
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Pikon Raditsz");
}

juce::File VersionManager::getVersionFile() const
{
    return getAppDataDirectory().getChildFile("installed_files.json");
}

bool VersionManager::saveVersionInfo()
{
    auto versionFile = getVersionFile();

    // Create directory if it doesn't exist
    versionFile.getParentDirectory().createDirectory();

    // Build JSON structure
    auto* root = new juce::DynamicObject();
    root->setProperty("appVersion", currentVersion);
    root->setProperty("variant", currentVariant);
    root->setProperty("lastUpdateCheck", lastUpdateCheck.toISO8601(true));

    // Add installed files
    auto* filesObj = new juce::DynamicObject();
    for (auto it = installedFiles.begin(); it != installedFiles.end(); ++it)
    {
        filesObj->setProperty(it.getKey(), it.getValue().toJson());
    }
    root->setProperty("files", juce::var(filesObj));

    // Write to file
    juce::var json(root);
    auto      jsonString = juce::JSON::toString(json, true);

    return versionFile.replaceWithText(jsonString);
}

bool VersionManager::loadVersionInfo()
{
    auto versionFile = getVersionFile();

    if (!versionFile.existsAsFile())
    {
        DBG("Version file doesn't exist yet: " + versionFile.getFullPathName());
        return false;
    }

    auto jsonString = versionFile.loadFileAsString();
    auto json = juce::JSON::parse(jsonString);

    if (auto* obj = json.getDynamicObject())
    {
        currentVersion = obj->getProperty("appVersion").toString();
        currentVariant = obj->getProperty("variant").toString();

        auto dateStr = obj->getProperty("lastUpdateCheck").toString();
        if (dateStr.isNotEmpty())
            lastUpdateCheck = juce::Time::fromISO8601(dateStr);

        // Load installed files
        if (auto filesObj = obj->getProperty("files"))
        {
            if (auto* filesDict = filesObj.getDynamicObject())
            {
                installedFiles.clear();

                for (auto& prop : filesDict->getProperties())
                {
                    auto fileInfo = InstalledFileInfo::fromJson(prop.value);
                    installedFiles.set(prop.name.toString(), fileInfo);
                }
            }
        }

        return true;
    }

    return false;
}

} // namespace Updater
