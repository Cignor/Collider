#pragma once

#include <string>
#include <memory>
#include "RawAnimationData.h"

class GltfLoader
{
public:
    // The main public function to load raw animation data from a file.
    // Returns simple, pointer-free data that must be processed by AnimationBinder.
    static std::unique_ptr<RawAnimationData> LoadFromFile(const std::string& filePath);
};

