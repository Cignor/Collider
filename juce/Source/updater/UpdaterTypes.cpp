#include "UpdaterTypes.h"

namespace Updater
{

// FileInfo implementation
FileInfo FileInfo::fromJson(const juce::String& path, const juce::var& json)
{
    FileInfo info;
    info.relativePath = path;

    if (auto* obj = json.getDynamicObject())
    {
        info.size = (juce::int64)obj->getProperty("size");
        info.sha256 = obj->getProperty("sha256").toString();
        info.version = obj->getProperty("version").toString();
        info.critical = (bool)obj->getProperty("critical");

        // URL might be in the object, or constructed from base URL
        if (obj->hasProperty("url"))
            info.url = obj->getProperty("url").toString();
    }

    return info;
}

juce::var FileInfo::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("size", size);
    obj->setProperty("sha256", sha256);
    obj->setProperty("version", version);
    obj->setProperty("critical", critical);

    if (url.isNotEmpty())
        obj->setProperty("url", url);

    return juce::var(obj);
}

// VariantInfo implementation
VariantInfo VariantInfo::fromJson(
    const juce::String& variantName,
    const juce::var&    json,
    const juce::String& baseUrl)
{
    VariantInfo variant;
    variant.name = variantName;

    if (auto* obj = json.getDynamicObject())
    {
        variant.displayName = obj->getProperty("displayName").toString();

        // Parse files
        if (auto filesObj = obj->getProperty("files"))
        {
            if (auto* filesDict = filesObj.getDynamicObject())
            {
                for (auto& prop : filesDict->getProperties())
                {
                    auto fileInfo = FileInfo::fromJson(prop.name.toString(), prop.value);

                    // Construct URL if not provided
                    if (fileInfo.url.isEmpty() && baseUrl.isNotEmpty())
                    {
                        // Ensure URL uses forward slashes
                        auto relativePath = fileInfo.relativePath.replace("\\", "/");

                        // URL should be direct relative path from base URL
                        fileInfo.url = baseUrl + "/" + relativePath;
                    }

                    variant.files.add(fileInfo);
                }
            }
        }
    }

    return variant;
}

// UpdateManifest implementation
UpdateManifest UpdateManifest::fromJson(const juce::String& jsonString)
{
    UpdateManifest manifest;

    auto json = juce::JSON::parse(jsonString);
    if (auto* obj = json.getDynamicObject())
    {
        manifest.appName = obj->getProperty("appName").toString();
        manifest.latestVersion = obj->getProperty("latestVersion").toString();
        manifest.minimumVersion = obj->getProperty("minimumVersion").toString();
        manifest.updateUrl = obj->getProperty("updateUrl").toString();

        // Parse release date
        auto dateStr = obj->getProperty("releaseDate").toString();
        manifest.releaseDate = juce::Time::fromISO8601(dateStr);

        // Parse changelog
        if (auto changelogObj = obj->getProperty("changelog"))
        {
            if (auto* changelog = changelogObj.getDynamicObject())
            {
                manifest.changelogUrl = changelog->getProperty("url").toString();
                manifest.changelogSummary = changelog->getProperty("summary").toString();
            }
        }

        // Parse variants
        if (auto variantsObj = obj->getProperty("variants"))
        {
            if (auto* variantsDict = variantsObj.getDynamicObject())
            {
                for (auto& prop : variantsDict->getProperties())
                {
                    auto variant =
                        VariantInfo::fromJson(prop.name.toString(), prop.value, manifest.updateUrl);
                    manifest.variants.add(variant);
                }
            }
        }
    }

    return manifest;
}

const VariantInfo* UpdateManifest::getVariant(const juce::String& variantName) const
{
    for (const auto& variant : variants)
    {
        if (variant.name == variantName)
            return &variant;
    }
    return nullptr;
}

// InstalledFileInfo implementation
InstalledFileInfo InstalledFileInfo::fromJson(const juce::var& json)
{
    InstalledFileInfo info;

    if (auto* obj = json.getDynamicObject())
    {
        info.version = obj->getProperty("version").toString();
        info.sha256 = obj->getProperty("sha256").toString();

        auto dateStr = obj->getProperty("installedDate").toString();
        info.installedDate = juce::Time::fromISO8601(dateStr);
    }

    return info;
}

juce::var InstalledFileInfo::toJson() const
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version", version);
    obj->setProperty("sha256", sha256);
    obj->setProperty("installedDate", installedDate.toISO8601(true));
    return juce::var(obj);
}

} // namespace Updater
