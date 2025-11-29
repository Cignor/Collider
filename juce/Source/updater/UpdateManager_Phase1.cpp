// ============================================================================
// NEW METHODS - Phase 1: Hash Verification & Manifest Caching
// ============================================================================

juce::String UpdateManager::getCachedManifest()
{
    auto cacheFile =
        versionManager->getVersionFile().getParentDirectory().getChildFile("manifest_cache.json");

    if (cacheFile.existsAsFile())
    {
        // Check if cache is fresh (less than 1 hour old)
        auto age = juce::Time::getCurrentTime() - cacheFile.getLastModificationTime();
        if (age.inHours() < 1)
        {
            DBG("Using cached manifest (age: " + juce::String(age.inMinutes()) + " minutes)");
            return cacheFile.loadFileAsString();
        }
        else
        {
            DBG("Cached manifest too old (age: " + juce::String(age.inHours()) + " hours)");
        }
    }

    return juce::String();
}

void UpdateManager::cacheManifest(const juce::String& manifestJson)
{
    auto cacheFile =
        versionManager->getVersionFile().getParentDirectory().getChildFile("manifest_cache.json");

    cacheFile.getParentDirectory().createDirectory();
    cacheFile.replaceWithText(manifestJson);
    DBG("Manifest cached to: " + cacheFile.getFullPathName());
}

void UpdateManager::registerRunningExecutable()
{
    auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    auto installDir = exePath.getParentDirectory();
    auto exeName = exePath.getFileName(); // "Pikon Raditsz.exe" or similar

    DBG("Registering running executable: " + exeName);

    // Check if already tracked
    if (versionManager->hasFile(exeName))
    {
        DBG("Executable already tracked: " + exeName);
        return;
    }

    // Calculate hash of running exe (may fail if locked, that's OK)
    auto exeHash = HashVerifier::calculateSHA256(exePath);

    if (exeHash.isEmpty())
    {
        DBG("Could not calculate hash of running executable (file may be locked)");
        // Don't fail - we'll try again on next update check
        return;
    }

    // Try to use cached manifest first
    auto cachedManifest = getCachedManifest();

    if (cachedManifest.isEmpty())
    {
        // No cache - skip for now, will verify on first update check
        DBG("No cached manifest - will verify on first update check");
        return;
    }

    try
    {
        // Parse cached manifest
        auto json = juce::JSON::parse(cachedManifest);

        if (auto* obj = json.getDynamicObject())
        {
            // Get current variant
            auto currentVariant = versionManager->getCurrentVariant();
            DBG("Current variant: " + currentVariant);

            auto variantsArray = obj->getProperty("variants");
            if (auto* variantsObj = variantsArray.getDynamicObject())
            {
                auto variantData = variantsObj->getProperty(currentVariant);
                if (auto* variantObj = variantData.getDynamicObject())
                {
                    auto filesObj = variantObj->getProperty("files");
                    if (auto* files = filesObj.getDynamicObject())
                    {
                        // Look for EXE entry
                        for (auto& prop : files->getProperties())
                        {
                            auto fileName = prop.name.toString();

                            if (fileName.equalsIgnoreCase(exeName) ||
                                fileName.endsWithIgnoreCase(".exe"))
                            {
                                // Found EXE entry
                                if (auto* fileObj = prop.value.getDynamicObject())
                                {
                                    juce::String manifestHash = fileObj->getProperty("sha256");
                                    juce::String version = fileObj->getProperty("version");
                                    int64        size = (int64)fileObj->getProperty("size");

                                    if (exeHash.equalsIgnoreCase(manifestHash))
                                    {
                                        // Hash matches! Register as installed
                                        FileInfo info;
                                        info.relativePath = fileName;
                                        info.sha256 = manifestHash;
                                        info.version = version;
                                        info.size = size;
                                        info.critical = true;
                                        info.url = ""; // Not needed for installed files

                                        versionManager->updateFileRecord(fileName, info);
                                        versionManager->saveVersionInfo();
                                        DBG("Running EXE verified and registered: " + fileName +
                                            " (hash: " + exeHash.substring(0, 16) + "...)");
                                        return;
                                    }
                                    else
                                    {
                                        DBG("Hash mismatch for " + fileName);
                                        DBG("  Local:  " + exeHash);
                                        DBG("  Remote: " + manifestHash);
                                        // Don't register - needs update
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        DBG("Executable not found in manifest files");
    }
    catch (const std::exception& e)
    {
        DBG("Error verifying executable: " + juce::String(e.what()));
        // Don't fail -will try again later
    }
}

juce::File UpdateManager::getInstallDirectory() const
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
}

juce::File UpdateManager::createUpdateManifest(
    const juce::Array<FileInfo>& files,
    const juce::File&            tempDir)
{
    auto manifestFile = tempDir.getChildFile("update_manifest.json");

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::DynamicObject::Ptr filesObj = new juce::DynamicObject();

    for (const auto& file : files)
    {
        juce::DynamicObject::Ptr fileObj = new juce::DynamicObject();
        fileObj->setProperty("sha256", file.sha256);
        fileObj->setProperty("size", file.size);
        filesObj->setProperty(file.relativePath, juce::var(fileObj));
    }

    root->setProperty("files", juce::var(filesObj));

    juce::FileOutputStream output(manifestFile);
    if (output.openedOk())
    {
        juce::JSON::writeToStream(output, juce::var(root), true);
        DBG("Update manifest created: " + manifestFile.getFullPathName());
    }
    else
    {
        DBG("Failed to create update manifest");
    }

    return manifestFile;
}
