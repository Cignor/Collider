#pragma once

#include "AnimationData.h"
#include <string>
#include <memory>

/**
 * FbxLoader - Static utility class for loading FBX files into our custom AnimationData format.
 * 
 * This loader uses the ufbx library (https://github.com/ufbx/ufbx) to parse FBX files.
 * It handles coordinate system conversions, unit scaling, and animation stack extraction.
 */
class FbxLoader
{
public:
    /**
     * Load an FBX file and return an AnimationData object.
     * @param filePath Full path to the .fbx file
     * @return A unique_ptr to the loaded AnimationData, or nullptr if loading failed
     */
    static std::unique_ptr<AnimationData> LoadFromFile(const std::string& filePath);
};

