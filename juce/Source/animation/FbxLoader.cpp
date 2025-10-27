// ufbx is a single-header library - we need to define the implementation in exactly one .cpp file
// This MUST be defined before any includes
#define UFBX_IMPLEMENTATION

#define GLM_ENABLE_EXPERIMENTAL

#include "FbxLoader.h"
#include <ufbx.h>
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

// Forward-declare the recursive node parsing function
static void ParseNodesRecursive(ufbx_node* ufbxNode, NodeData& parentNodeData);

// Helper to convert ufbx matrix to glm::mat4
static glm::mat4 ToGlmMat4(const ufbx_matrix& m)
{
    // ufbx stores matrices in column-major order (same as glm)
    // We can directly construct from the cols array
    return glm::mat4(
        m.cols[0].x, m.cols[0].y, m.cols[0].z, 0.0f,
        m.cols[1].x, m.cols[1].y, m.cols[1].z, 0.0f,
        m.cols[2].x, m.cols[2].y, m.cols[2].z, 0.0f,
        m.cols[3].x, m.cols[3].y, m.cols[3].z, 1.0f
    );
}

// Helper to convert ufbx transform to glm::mat4
static glm::mat4 TransformToMat4(const ufbx_transform& t)
{
    // Convert the transform to a matrix using ufbx's built-in function
    ufbx_matrix m = ufbx_transform_to_matrix(&t);
    return ToGlmMat4(m);
}

// Main public function
std::unique_ptr<AnimationData> FbxLoader::LoadFromFile(const std::string& filePath)
{
    ufbx_load_opts opts = {};
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filePath.c_str(), &opts, &error);

    if (!scene)
    {
        std::cerr << "[FbxLoader] Failed to load FBX file: " << filePath << "\n" 
                  << error.description.data << std::endl;
        return nullptr;
    }

    std::cout << "[FbxLoader] Successfully loaded FBX: " << filePath << std::endl;
    std::cout << "[FbxLoader] Nodes: " << scene->nodes.count << std::endl;
    std::cout << "[FbxLoader] Skins: " << scene->skin_deformers.count << std::endl;
    std::cout << "[FbxLoader] Anim stacks: " << scene->anim_stacks.count << std::endl;

    auto animData = std::make_unique<AnimationData>();

    // Parse the node hierarchy
    if (scene->root_node)
    {
        ParseNodesRecursive(scene->root_node, animData->rootNode);
        std::cout << "[FbxLoader] Parsed node hierarchy" << std::endl;
    }

    // --- Parse Skinning Data ---
    // Use the first skin deformer found in the scene
    if (scene->skin_deformers.count > 0)
    {
        ufbx_skin_deformer* skin = scene->skin_deformers.data[0];
        for (size_t i = 0; i < skin->clusters.count; ++i)
        {
            ufbx_skin_cluster* cluster = skin->clusters.data[i];
            if (!cluster->bone_node) continue;

            std::string boneName(cluster->bone_node->name.data, cluster->bone_node->name.length);

            BoneInfo boneInfo;
            boneInfo.id = static_cast<int>(i);
            boneInfo.name = boneName;
            boneInfo.offsetMatrix = ToGlmMat4(cluster->geometry_to_bone);

            animData->boneInfoMap[boneName] = boneInfo;
        }
        std::cout << "[FbxLoader] Parsed " << animData->boneInfoMap.size() << " bones from skin" << std::endl;
    }
    else
    {
        std::cout << "[FbxLoader] No skin data found, bones will be extracted from node hierarchy if needed" << std::endl;
    }

    // --- Parse Animation Data ---
    for (size_t stackIdx = 0; stackIdx < scene->anim_stacks.count; ++stackIdx)
    {
        ufbx_anim_stack* stack = scene->anim_stacks.data[stackIdx];
        if (!stack->anim) continue;

        AnimationClip clip;
        clip.name = std::string(stack->name.data, stack->name.length);
        clip.durationInTicks = stack->time_end - stack->time_begin;
        clip.ticksPerSecond = static_cast<double>(scene->settings.frames_per_second);

        std::cout << "[FbxLoader] Processing animation: " << clip.name 
                  << " (duration: " << clip.durationInTicks << "s, fps: " << clip.ticksPerSecond << ")" << std::endl;

        // Sample the animation at regular intervals (30 FPS)
        const double sampleRate = 30.0;
        const double timeStep = 1.0 / sampleRate;
        const int numSamples = static_cast<int>(clip.durationInTicks * sampleRate) + 1;

        // Iterate through all nodes that might be bones
        for (auto& bonePair : animData->boneInfoMap)
        {
            const std::string& boneName = bonePair.first;
            
            // Find the corresponding node in the scene
            ufbx_node* node = nullptr;
            for (size_t i = 0; i < scene->nodes.count; ++i)
            {
                std::string nodeName(scene->nodes.data[i]->name.data, scene->nodes.data[i]->name.length);
                if (nodeName == boneName)
                {
                    node = scene->nodes.data[i];
                    break;
                }
            }

            if (!node) continue;

            BoneAnimation boneAnim;
            boneAnim.boneName = boneName;

            // Sample the animation at regular intervals
            for (int sampleIdx = 0; sampleIdx < numSamples; ++sampleIdx)
            {
                double time = stack->time_begin + (sampleIdx * timeStep);
                if (time > stack->time_end) time = stack->time_end;

                // Evaluate the transform at this time
                ufbx_transform transform = ufbx_evaluate_transform(stack->anim, node, time);

                // Store position keyframe
                KeyPosition posKey;
                posKey.position = glm::vec3(
                    static_cast<float>(transform.translation.x),
                    static_cast<float>(transform.translation.y),
                    static_cast<float>(transform.translation.z)
                );
                posKey.timeStamp = time - stack->time_begin;
                boneAnim.positions.push_back(posKey);

                // Store rotation keyframe
                KeyRotation rotKey;
                rotKey.orientation = glm::quat(
                    static_cast<float>(transform.rotation.w),
                    static_cast<float>(transform.rotation.x),
                    static_cast<float>(transform.rotation.y),
                    static_cast<float>(transform.rotation.z)
                );
                rotKey.timeStamp = time - stack->time_begin;
                boneAnim.rotations.push_back(rotKey);

                // Store scale keyframe
                KeyScale scaleKey;
                scaleKey.scale = glm::vec3(
                    static_cast<float>(transform.scale.x),
                    static_cast<float>(transform.scale.y),
                    static_cast<float>(transform.scale.z)
                );
                scaleKey.timeStamp = time - stack->time_begin;
                boneAnim.scales.push_back(scaleKey);
            }

            // Add this bone's animation to the clip
            clip.boneAnimations[boneName] = boneAnim;
        }

        std::cout << "[FbxLoader] Animation contains " << clip.boneAnimations.size() << " bone tracks" << std::endl;
        animData->animationClips.push_back(clip);
    }

    ufbx_free_scene(scene);
    std::cout << "[FbxLoader] Successfully loaded " << animData->animationClips.size() << " animation clips" << std::endl;
    return animData;
}

// Recursive function to build our NodeData hierarchy
static void ParseNodesRecursive(ufbx_node* ufbxNode, NodeData& parentNodeData)
{
    if (!ufbxNode) return;

    parentNodeData.name = std::string(ufbxNode->name.data, ufbxNode->name.length);
    parentNodeData.transformation = TransformToMat4(ufbxNode->local_transform);

    // Recursively parse all children
    for (size_t i = 0; i < ufbxNode->children.count; ++i)
    {
        ufbx_node* childUfbxNode = ufbxNode->children.data[i];
        
        // Create a new child node (not a unique_ptr, just a regular struct)
        parentNodeData.children.emplace_back();
        NodeData& newChild = parentNodeData.children.back();
        newChild.parent = &parentNodeData;
        
        // Recursively parse this child
        ParseNodesRecursive(childUfbxNode, newChild);
    }
}
