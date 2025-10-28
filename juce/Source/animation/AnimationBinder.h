#pragma once

#include "RawAnimationData.h"
#include "AnimationData.h"
#include <memory>

class AnimationBinder
{
public:
    // The main public function. Takes raw data and returns a fully processed,
    // ready-to-use AnimationData object.
    static std::unique_ptr<AnimationData> Bind(const RawAnimationData& rawData);

private:
    // Private helper to recursively build the node hierarchy.
    static void BindNodesRecursive(const RawAnimationData& rawData, NodeData& parentNode, int rawNodeIndex);
};

