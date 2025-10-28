#include "AnimationBinder.h"
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include <juce_core/juce_core.h>

// Forward-declarations
static void BuildNodeHierarchyRecursive(const RawAnimationData& rawData, NodeData& parentNode, int rawNodeIndex);
static void MapNodes(NodeData& node, std::map<std::string, NodeData*>& map);
static void CalculateGlobalInitialTransforms(NodeData& node, const glm::mat4& parentTransform, std::map<std::string, glm::mat4>& globalTransforms);

std::unique_ptr<AnimationData> AnimationBinder::Bind(const RawAnimationData& rawData)
{
    juce::Logger::writeToLog("AnimationBinder: Starting bind process...");
    if (rawData.nodes.empty()) {
        juce::Logger::writeToLog("AnimationBinder ERROR: No nodes in raw data.");
        return nullptr;
    }
    
    auto animData = std::make_unique<AnimationData>();

    // Step 1: Build the initial hierarchy with parent pointers from the raw data.
    juce::Logger::writeToLog("AnimationBinder: Step 1 - Building node hierarchy from raw data...");
    int rootNodeIndex = -1;
    for(size_t i = 0; i < rawData.nodes.size(); ++i) {
        if(rawData.nodes[i].parentIndex == -1) { 
            rootNodeIndex = i; 
            juce::Logger::writeToLog("AnimationBinder: Found root node: " + juce::String(rawData.nodes[i].name));
            break; 
        }
    }
    if(rootNodeIndex == -1) {
        juce::Logger::writeToLog("AnimationBinder ERROR: No root node found.");
        return nullptr;
    }
    
    BuildNodeHierarchyRecursive(rawData, animData->rootNode, rootNodeIndex);
    juce::Logger::writeToLog("AnimationBinder: Node hierarchy built successfully.");

    // Step 2: Create a map of all NodeData pointers by name for easy lookup.
    juce::Logger::writeToLog("AnimationBinder: Step 2 - Creating node map...");
    std::map<std::string, NodeData*> nodeMap;
    MapNodes(animData->rootNode, nodeMap);
    juce::Logger::writeToLog("AnimationBinder: Node map created with " + juce::String(nodeMap.size()) + " entries.");

    // Step 3: Calculate the GLOBAL transform of ALL nodes based on the INITIAL file data.
    // This MUST be done BEFORE we start modifying any bone transforms!
    juce::Logger::writeToLog("AnimationBinder: Step 3 - Calculating global transforms from ORIGINAL file data...");
    std::map<std::string, glm::mat4> globalInitialTransforms;
    CalculateGlobalInitialTransforms(animData->rootNode, glm::mat4(1.0f), globalInitialTransforms);
    juce::Logger::writeToLog("AnimationBinder: Calculated " + juce::String(globalInitialTransforms.size()) + " global transforms.");

    // Step 4: Reconstruct the true LOCAL bind pose for every BONE.
    juce::Logger::writeToLog("AnimationBinder: Step 4 - Reconstructing bone local bind poses...");
    int reconstructedCount = 0;
    for (const auto& rawBone : rawData.bones) {
        if (nodeMap.count(rawBone.name)) {
            NodeData* boneNode = nodeMap.at(rawBone.name);
            glm::mat4 globalBindPose = glm::inverse(rawBone.offsetMatrix);
            glm::mat4 localBindPose = globalBindPose;

            if (boneNode->parent && globalInitialTransforms.count(boneNode->parent->name)) {
                glm::mat4 parentGlobalInitial = globalInitialTransforms.at(boneNode->parent->name);
                localBindPose = glm::inverse(parentGlobalInitial) * globalBindPose;
                juce::Logger::writeToLog("AnimationBinder: " + juce::String(rawBone.name) + " local pose calculated relative to parent: " + juce::String(boneNode->parent->name));
            } else {
                juce::Logger::writeToLog("AnimationBinder: " + juce::String(rawBone.name) + " using global pose as local (root or no parent)");
            }
            
            boneNode->transformation = localBindPose;
            reconstructedCount++;
        } else {
            juce::Logger::writeToLog("AnimationBinder WARNING: Bone " + juce::String(rawBone.name) + " not found in node map.");
        }
    }
    juce::Logger::writeToLog("AnimationBinder: Reconstructed " + juce::String(reconstructedCount) + " bone local bind poses.");
    
    // Step 5: Copy over the simple bone and animation data.
    juce::Logger::writeToLog("AnimationBinder: Step 5 - Binding bones and clips...");
    for (const auto& rawBone : rawData.bones) {
        animData->boneInfoMap[rawBone.name] = { rawBone.id, rawBone.name, rawBone.offsetMatrix };
    }
    
    for (const auto& rawClip : rawData.clips) {
        AnimationClip clip;
        clip.name = rawClip.name;
        clip.durationInTicks = rawClip.duration;
        clip.ticksPerSecond = 1.0;
        
        for (const auto& pair : rawClip.boneAnimations) {
            const auto& rawBoneAnim = pair.second;
            BoneAnimation boneAnim;
            boneAnim.boneName = rawBoneAnim.boneName;
            
            for (size_t i = 0; i < rawBoneAnim.positions.keyframeTimes.size(); ++i) {
                boneAnim.positions.push_back({ glm::vec3(rawBoneAnim.positions.keyframeValues[i]), rawBoneAnim.positions.keyframeTimes[i] });
            }
            for (size_t i = 0; i < rawBoneAnim.rotations.keyframeTimes.size(); ++i) {
                glm::vec4 v = rawBoneAnim.rotations.keyframeValues[i];
                boneAnim.rotations.push_back({ glm::quat(v.w, v.x, v.y, v.z), rawBoneAnim.rotations.keyframeTimes[i] });
            }
            for (size_t i = 0; i < rawBoneAnim.scales.keyframeTimes.size(); ++i) {
                boneAnim.scales.push_back({ glm::vec3(rawBoneAnim.scales.keyframeValues[i]), rawBoneAnim.scales.keyframeTimes[i] });
            }
            
            clip.boneAnimations[boneAnim.boneName] = boneAnim;
        }
        animData->animationClips.push_back(clip);
    }

    juce::Logger::writeToLog("AnimationBinder: Binding complete. Bones: " + juce::String(animData->boneInfoMap.size()) + ", Clips: " + juce::String(animData->animationClips.size()));
    return animData;
}

// Helper implementations
void BuildNodeHierarchyRecursive(const RawAnimationData& rawData, NodeData& parentNode, int rawNodeIndex) {
    const RawNodeData& rawNode = rawData.nodes[rawNodeIndex];
    parentNode.name = rawNode.name;
    parentNode.transformation = rawNode.localTransform;
    for (int childIndex : rawNode.childIndices) {
        if (childIndex >= 0 && childIndex < rawData.nodes.size()) {
            parentNode.children.emplace_back();
            NodeData& newChildNode = parentNode.children.back();
            newChildNode.parent = &parentNode;
            BuildNodeHierarchyRecursive(rawData, newChildNode, childIndex);
        }
    }
}

void MapNodes(NodeData& node, std::map<std::string, NodeData*>& map) {
    map[node.name] = &node;
    for (auto& child : node.children) {
        MapNodes(child, map);
    }
}

void CalculateGlobalInitialTransforms(NodeData& node, const glm::mat4& parentTransform, std::map<std::string, glm::mat4>& globalTransforms) {
    glm::mat4 globalTransform = parentTransform * node.transformation;
    globalTransforms[node.name] = globalTransform;
    for (auto& child : node.children) {
        CalculateGlobalInitialTransforms(child, globalTransform, globalTransforms);
    }
}
