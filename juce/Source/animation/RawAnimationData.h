#pragma once

#include <string>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// A simple, "dumb" container for a single node's raw data.
// It uses an integer index for its parent instead of a raw pointer.
struct RawNodeData
{
    std::string name;
    glm::mat4 localTransform;
    int parentIndex = -1; // -1 means it's a root node
    std::vector<int> childIndices;
};

// A simple container for a single bone's skinning data.
struct RawBoneInfo
{
    std::string name;
    glm::mat4 offsetMatrix;
    int id = 0;
};

// A simple container for a single animation track (e.g., a bone's translation).
struct RawAnimationTrack
{
    std::vector<double> keyframeTimes;
    // We use vec4 to hold either position(x,y,z), scale(x,y,z), or rotation(x,y,z,w).
    std::vector<glm::vec4> keyframeValues;
};

// A container for all the animation tracks for a single bone.
struct RawBoneAnimation
{
    std::string boneName;
    RawAnimationTrack positions;
    RawAnimationTrack rotations;
    RawAnimationTrack scales;
};

// A container for a full animation clip.
struct RawAnimationClip
{
    std::string name;
    double duration = 0.0;
    std::map<std::string, RawBoneAnimation> boneAnimations;
};

// The top-level container that holds all raw data parsed from a file.
// This object is completely self-contained and pointer-free.
struct RawAnimationData
{
    std::vector<RawNodeData> nodes;
    std::vector<RawBoneInfo> bones;
    std::vector<RawAnimationClip> clips;
    
    /**
     * @brief Validates the integrity of the raw animation data.
     * 
     * Checks for common issues that could cause crashes:
     * - Empty or missing data
     * - Invalid parent/child indices
     * - Out-of-bounds node references
     * - Malformed animation tracks
     * 
     * @param data The RawAnimationData object to validate.
     * @param outErrorMessage A string that will be populated with a detailed error message if validation fails.
     * @return True if the data is valid and safe to use, false otherwise.
     */
    static bool validate(const RawAnimationData& data, std::string& outErrorMessage);
};

