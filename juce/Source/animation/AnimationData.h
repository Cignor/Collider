#pragma once

#include <string>
#include <vector>
#include <map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct KeyPosition
{
    glm::vec3 position;
    double timeStamp;
};

struct KeyRotation
{
    glm::quat orientation;
    double timeStamp;
};

struct KeyScale
{
    glm::vec3 scale;
    double timeStamp;
};

// Contains all position, rotation, and scale keyframes for a single bone
class BoneAnimation
{
public:
    std::vector<KeyPosition> positions;
    std::vector<KeyRotation> rotations;
    std::vector<KeyScale> scales;
    std::string boneName;
};

// Represents a single, self-contained animation clip (e.g., "walk", "run")
class AnimationClip
{
public:
    std::string name;
    double durationInTicks;
    double ticksPerSecond;
    std::map<std::string, BoneAnimation> boneAnimations;
};

// Contains static information about a single bone that influences the mesh
struct BoneInfo
{
    int id; // Unique ID for the bone, used as an index into the final transform array
    std::string name;
    glm::mat4 offsetMatrix; // Transforms vertices from model space to bone space
};

// Forward declare
class BoneAnimation;

// Represents a node in the skeleton's hierarchy. A node can be a bone or just a transform group.
struct NodeData
{
    glm::mat4 transformation = glm::mat4(1.0f); // The node's local transform relative to its parent
    std::string name;
    std::vector<NodeData> children;
    NodeData* parent = nullptr;
    BoneAnimation* currentBoneAnimation = nullptr; // Pre-linked for current animation (NO string lookup needed!)
    int boneIndex = -1; // Pre-linked bone index (-1 if not a bone)
    glm::mat4 offsetMatrix = glm::mat4(1.0f); // Pre-linked offset matrix (identity if not a bone)
};

// The top-level container for all parsed animation and skeleton data from a single file
class AnimationData
{
public:
    NodeData rootNode;
    std::map<std::string, BoneInfo> boneInfoMap;
    std::vector<AnimationClip> animationClips;
};

