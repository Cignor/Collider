#include "AnimationBinder.h"
#include <stdexcept>
#include <glm/gtc/matrix_inverse.hpp>
#include <juce_core/juce_core.h>

// Forward-declarations
static void BuildNodeHierarchyRecursive(const RawAnimationData& rawData, NodeData& parentNode, int rawNodeIndex);
static void SetParentPointersRecursive(NodeData& node);
static void MapNodes(NodeData& node, std::map<std::string, NodeData*>& map);
static void CalculateGlobalInitialTransforms(NodeData& node, const glm::mat4& parentTransform, std::map<std::string, glm::mat4>& globalTransforms);
static void PreLinkBoneInfoToNodes(NodeData* node, const std::map<std::string, BoneInfo>& boneInfoMap);

std::unique_ptr<AnimationData> AnimationBinder::Bind(const RawAnimationData& rawData)
{
    juce::Logger::writeToLog("AnimationBinder: Starting bind process...");
    if (rawData.nodes.empty()) {
        juce::Logger::writeToLog("AnimationBinder ERROR: No nodes in raw data.");
        return nullptr;
    }
    
    auto animData = std::make_unique<AnimationData>();

    // === START: Universal Multi-Root Handling ===
    juce::Logger::writeToLog("AnimationBinder: Step 1 - Unifying hierarchy under a master root...");
    
    // 1. Create our own master root node. This will be the single root of our final hierarchy.
    animData->rootNode.name = "MASTER_ROOT";
    animData->rootNode.transformation = glm::mat4(1.0f);

    // 2. Iterate through ALL raw nodes and find every node that is a root (parentIndex == -1).
    int rootsFound = 0;
    for(size_t i = 0; i < rawData.nodes.size(); ++i) {
        if(rawData.nodes[i].parentIndex == -1) { 
            rootsFound++;
            juce::Logger::writeToLog("  [Binder] Found root node '" + juce::String(rawData.nodes[i].name) + "' from file. Attaching to MASTER_ROOT.");
            
            // 3. For each root found, build its entire child hierarchy and add it as a child of our master root.
            NodeData newChildRoot;
            BuildNodeHierarchyRecursive(rawData, newChildRoot, i);
            animData->rootNode.children.push_back(std::move(newChildRoot));
        }
    }

    if (rootsFound == 0) {
        juce::Logger::writeToLog("AnimationBinder ERROR: No root node found in raw data.");
        return nullptr;
    }
    
    juce::Logger::writeToLog("AnimationBinder: Successfully attached " + juce::String(rootsFound) + " root node(s) to MASTER_ROOT.");
    // === END: Universal Multi-Root Handling ===
    
    juce::Logger::writeToLog("AnimationBinder: Node hierarchy built successfully.");
    
    // CRITICAL: Set parent pointers AFTER the entire hierarchy is built
    // to avoid dangling pointers from vector reallocation
    juce::Logger::writeToLog("AnimationBinder: Setting parent pointers...");
    SetParentPointersRecursive(animData->rootNode);
    juce::Logger::writeToLog("AnimationBinder: Parent pointers set successfully.");

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
    juce::Logger::writeToLog("=== EXECUTING LATEST AnimationBinder CODE WITH DEFENSIVE CHECKS ===");
    int reconstructedCount = 0;
    int rootBoneCount = 0;
    int skippedCount = 0;
    for (const auto& rawBone : rawData.bones) {
        
        // === START: CONDITIONAL RECONSTRUCTION CHECK ===
        // If the offset matrix is identity, it means we're using a loader fallback (no skin data).
        // In this case, we MUST trust the localTransform from the file and NOT try to reconstruct it.
        if (rawBone.offsetMatrix == glm::mat4(1.0f))
        {
            // Log that we are intentionally skipping this bone's reconstruction.
            juce::Logger::writeToLog("AnimationBinder: Skipping reconstruction for bone '" + juce::String(rawBone.name) + "' (using fallback with identity offset matrix).");
            skippedCount++;
            continue; // Skip to the next bone.
        }
        // === END: CONDITIONAL RECONSTRUCTION CHECK ===
        
        if (nodeMap.count(rawBone.name)) {
            NodeData* boneNode = nodeMap.at(rawBone.name);
            glm::mat4 globalBindPose = glm::inverse(rawBone.offsetMatrix);
            glm::mat4 localBindPose = globalBindPose;

            // Check if this is a root bone (no parent)
            if (!boneNode->parent) {
                // This is a root bone - its local pose IS its global pose
                localBindPose = globalBindPose;
                rootBoneCount++;
                juce::Logger::writeToLog("AnimationBinder: " + juce::String(rawBone.name) + " is a ROOT BONE. Using global pose as local pose.");
            } 
            else if (globalInitialTransforms.count(boneNode->parent->name)) {
                // This bone has a valid parent - calculate local pose relative to parent
                glm::mat4 parentGlobalInitial = globalInitialTransforms.at(boneNode->parent->name);
                localBindPose = glm::inverse(parentGlobalInitial) * globalBindPose;
                juce::Logger::writeToLog("AnimationBinder: " + juce::String(rawBone.name) + " local pose calculated relative to parent: " + juce::String(boneNode->parent->name));
            } 
            else {
                // Parent exists but not in global transforms map - this is unexpected
                juce::Logger::writeToLog("AnimationBinder WARNING: " + juce::String(rawBone.name) + " has parent " + juce::String(boneNode->parent->name) + " but parent not in global transforms. Using global pose as fallback.");
            }
            
            boneNode->transformation = localBindPose;
            reconstructedCount++;
        } else {
            juce::Logger::writeToLog("AnimationBinder WARNING: Bone " + juce::String(rawBone.name) + " not found in node map.");
        }
    }
    juce::Logger::writeToLog("AnimationBinder: Reconstructed " + juce::String(reconstructedCount) + " bone local bind poses (" + juce::String(rootBoneCount) + " root bones). Skipped " + juce::String(skippedCount) + " bones with identity offset matrices.");
    
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

    // Step 6: Pre-link bone info to nodes for lock-free audio thread access
    juce::Logger::writeToLog("AnimationBinder: Step 6 - Pre-linking bone info to nodes...");
    PreLinkBoneInfoToNodes(&animData->rootNode, animData->boneInfoMap);
    
    juce::Logger::writeToLog("AnimationBinder: Binding complete. Bones: " + juce::String(animData->boneInfoMap.size()) + ", Clips: " + juce::String(animData->animationClips.size()));
    return animData;
}

// Helper implementations
void BuildNodeHierarchyRecursive(const RawAnimationData& rawData, NodeData& parentNode, int rawNodeIndex) {
    const RawNodeData& rawNode = rawData.nodes[rawNodeIndex];
    parentNode.name = rawNode.name;
    parentNode.transformation = rawNode.localTransform;
    parentNode.parent = nullptr; // Will be set later in SetParentPointersRecursive
    
    // Recursively build child nodes
    for (int childIndex : rawNode.childIndices) {
        // CRITICAL: Validate child index before accessing the nodes array
        // Root nodes and some edges cases may have invalid indices
        if (childIndex >= 0 && childIndex < static_cast<int>(rawData.nodes.size())) {
            // === THE DEFINITIVE FIX: Build child completely before adding to the parent's vector ===
            
            // 1. Create a temporary, stack-allocated child node.
            NodeData newChildNode;
            
            // 2. Recursively build the full hierarchy FOR this new child.
            //    This is safe because newChildNode is a local variable, not a reference into a vector.
            juce::Logger::writeToLog("  [Binder] Building child '" + juce::String(rawData.nodes[childIndex].name) + "' before adding to parent '" + juce::String(parentNode.name) + "'");
            BuildNodeHierarchyRecursive(rawData, newChildNode, childIndex);
            
            // 3. Now that the child is fully built and stable, move it into the parent's vector.
            //    This single push_back may reallocate, but it won't affect other children in this loop.
            parentNode.children.push_back(std::move(newChildNode));
            
            // =====================================================================================
        } else {
            juce::Logger::writeToLog("AnimationBinder WARNING: Invalid child index " + juce::String(childIndex) + " for node " + juce::String(rawNode.name));
        }
    }
}

// Set parent pointers after the hierarchy is fully built
// This prevents dangling pointers from vector reallocations
void SetParentPointersRecursive(NodeData& node) {
    for (auto& child : node.children) {
        child.parent = &node;
        SetParentPointersRecursive(child);
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

// Pre-link bone info to nodes for lock-free audio thread access
void PreLinkBoneInfoToNodes(NodeData* node, const std::map<std::string, BoneInfo>& boneInfoMap) {
    if (!node) return;
    
    // Check if this node is a bone
    if (boneInfoMap.count(node->name)) {
        const BoneInfo& boneInfo = boneInfoMap.at(node->name);
        node->boneIndex = boneInfo.id;
        node->offsetMatrix = boneInfo.offsetMatrix;
    } else {
        node->boneIndex = -1;
        node->offsetMatrix = glm::mat4(1.0f); // Identity
    }
    
    // Recurse to children
    for (auto& child : node->children) {
        PreLinkBoneInfoToNodes(&child, boneInfoMap);
    }
}
