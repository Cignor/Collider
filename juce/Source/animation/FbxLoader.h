#pragma once

#include "RawAnimationData.h"
#include <string>
#include <memory>

class FbxLoader
{
public:
    // Change the return type from AnimationData to RawAnimationData
    static std::unique_ptr<RawAnimationData> LoadFromFile(const std::string& filePath);
};
