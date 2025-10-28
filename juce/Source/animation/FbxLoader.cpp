#define GLM_ENABLE_EXPERIMENTAL

#include "FbxLoader.h"
#include <ufbx.h>
#include <iostream>
#include <map>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <juce_core/juce_core.h>

// Helper to convert ufbx transform to glm::mat4
static glm::mat4 ToGlmMat4(const ufbx_transform& t)
{
    glm::vec3 translation(t.translation.x, t.translation.y, t.translation.z);
    // t.rotation is already a quaternion, not Euler angles
    glm::quat rotation(t.rotation.w, t.rotation.x, t.rotation.y, t.rotation.z);
    glm::vec3 scale(t.scale.x, t.scale.y, t.scale.z);

    return glm::translate(glm::mat4(1.0f), translation) *
           glm::toMat4(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

std::unique_ptr<RawAnimationData> FbxLoader::LoadFromFile(const std::string& filePath)
{
    juce::Logger::writeToLog("FbxLoader: Starting to load " + juce::String(filePath));
    ufbx_load_opts opts = {};
    opts.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
    opts.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
    opts.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
    opts.target_unit_meters = 1.0;

    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(filePath.c_str(), &opts, &error);

    if (!scene) {
        juce::Logger::writeToLog("FbxLoader ERROR: " + juce::String(error.description.data));
        return nullptr;
    }
    juce::Logger::writeToLog("FbxLoader: Successfully parsed with ufbx.");

    auto rawData = std::make_unique<RawAnimationData>();
    std::map<uint32_t, int> nodeIdToIndexMap;

    // --- 1. Parse All Nodes ---
    juce::Logger::writeToLog("FbxLoader: Parsing " + juce::String(scene->nodes.count) + " nodes...");
    for (size_t i = 0; i < scene->nodes.count; ++i) {
        ufbx_node* ufbNode = scene->nodes.data[i];
        if (!ufbNode) continue;
        
        nodeIdToIndexMap[ufbNode->element_id] = rawData->nodes.size();
        RawNodeData node;
        node.name = ufbNode->name.data;
        
        // If the node's name is empty, give it a default name.
        // This is common for the implicit root node in some FBX files.
        if (node.name.empty())
        {
            node.name = "fbx_node_" + std::to_string(i);
            juce::Logger::writeToLog("FbxLoader: Found node with empty name at index " + juce::String(i) + ". Assigning default name '" + juce::String(node.name) + "'.");
        }
        
        node.localTransform = ToGlmMat4(ufbNode->local_transform);
        rawData->nodes.push_back(node);
    }

    // --- 2. Link Node Parents/Children ---
    for (size_t i = 0; i < scene->nodes.count; ++i) {
         ufbx_node* ufbNode = scene->nodes.data[i];
         if (!ufbNode || !ufbNode->parent) continue;

         if (nodeIdToIndexMap.count(ufbNode->parent->element_id)) {
            int parentIndex = nodeIdToIndexMap[ufbNode->parent->element_id];
            // Validate parentIndex before using it to access arrays
            if (parentIndex >= 0 && parentIndex < static_cast<int>(rawData->nodes.size())) {
                rawData->nodes[i].parentIndex = parentIndex;
                rawData->nodes[parentIndex].childIndices.push_back(i);
            }
         }
    }

    // --- 3. Parse Bones (with robust fallback) ---
    std::map<std::string, int> boneNameMap;
    
    juce::Logger::writeToLog("FbxLoader: Checking for skin data... (skin_deformers.count = " + juce::String(scene->skin_deformers.count) + ")");
    
    if (scene->skin_deformers.count > 0) 
    {
        juce::Logger::writeToLog("FbxLoader: Found explicit skin data. Parsing bones from skin deformers.");
        ufbx_skin_deformer* skin = scene->skin_deformers.data[0];
        juce::Logger::writeToLog("FbxLoader: Skin has " + juce::String(skin->clusters.count) + " clusters.");
        
        for (size_t i = 0; i < skin->clusters.count; ++i) {
            ufbx_skin_cluster* cluster = skin->clusters.data[i];
            if (!cluster || !cluster->bone_node) continue;
            
            std::string boneName = cluster->bone_node->name.data;

            if (boneNameMap.find(boneName) == boneNameMap.end()) {
                boneNameMap[boneName] = rawData->bones.size();
                RawBoneInfo boneInfo;
                boneInfo.id = rawData->bones.size();
                boneInfo.name = boneName;
                boneInfo.offsetMatrix = glm::transpose(glm::make_mat4(&cluster->geometry_to_bone.m00));
                rawData->bones.push_back(boneInfo);
                juce::Logger::writeToLog("FbxLoader: Found skin bone #" + juce::String(boneInfo.id) + ": " + juce::String(boneName));
            }
        }
    }
    else 
    {
        juce::Logger::writeToLog("FbxLoader: No skin data found. Using fallback: creating bones from animation targets.");
        juce::Logger::writeToLog("FbxLoader: Animation stacks count: " + juce::String(scene->anim_stacks.count));
        
        for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
            ufbx_anim_stack* stack = scene->anim_stacks.data[i];
            if (!stack || !stack->anim) continue;
            
            ufbx_anim* anim = stack->anim;
            juce::Logger::writeToLog("FbxLoader: Animation stack '" + juce::String(stack->name.data) + "' has " + juce::String(anim->layers.count) + " layers.");
            
            // Use anim->layers approach (more reliable for some FBX files)
            for (size_t j = 0; j < anim->layers.count; ++j) {
                ufbx_anim_layer* layer = anim->layers.data[j];
                if (!layer) continue;
                
                for (size_t k = 0; k < layer->anim_props.count; ++k) {
                    ufbx_anim_prop* prop = &layer->anim_props.data[k];
                    if (!prop->anim_value || !prop->element) continue;
                    
                    ufbx_node* node = ufbx_as_node(prop->element);
                    if (!node) continue;
                    
                    std::string boneName = node->name.data;
                    if (boneName.empty()) continue;

                    if (boneNameMap.find(boneName) == boneNameMap.end()) {
                        boneNameMap[boneName] = rawData->bones.size();
                        RawBoneInfo boneInfo;
                        boneInfo.id = rawData->bones.size();
                        boneInfo.name = boneName;
                        boneInfo.offsetMatrix = glm::mat4(1.0f);
                        rawData->bones.push_back(boneInfo);
                        juce::Logger::writeToLog("FbxLoader: Created fallback bone #" + juce::String(boneInfo.id) + ": " + juce::String(boneName));
                    }
                }
            }
        }
    }
    
    juce::Logger::writeToLog("FbxLoader: Total bones found: " + juce::String(rawData->bones.size()));
    
    // --- DEBUG: Validate node structure ---
    juce::Logger::writeToLog("FbxLoader: Validating node structure...");
    int nodesWithoutParent = 0;
    for (size_t i = 0; i < rawData->nodes.size(); ++i) {
        if (rawData->nodes[i].parentIndex == -1) {
            nodesWithoutParent++;
            juce::Logger::writeToLog("FbxLoader: Root node found: " + juce::String(rawData->nodes[i].name));
        }
    }
    juce::Logger::writeToLog("FbxLoader: Found " + juce::String(nodesWithoutParent) + " root nodes in hierarchy.");

    // --- 4. Parse Animations ---
    juce::Logger::writeToLog("FbxLoader: Parsing animations...");
    for (size_t i = 0; i < scene->anim_stacks.count; ++i) {
        ufbx_anim_stack* stack = scene->anim_stacks.data[i];
        if (!stack || !stack->anim) continue;
        
        ufbx_anim* anim = stack->anim;
        RawAnimationClip clip;
        clip.name = stack->name.data;
        clip.duration = anim->time_end;
        
        juce::Logger::writeToLog("FbxLoader: Processing animation '" + juce::String(clip.name) + "' (duration: " + juce::String(clip.duration) + "s)");

        for (size_t j = 0; j < anim->layers.count; ++j) {
            ufbx_anim_layer* layer = anim->layers.data[j];
            if (!layer) continue;
            
            for (size_t k = 0; k < layer->anim_props.count; ++k) {
                ufbx_anim_prop* prop = &layer->anim_props.data[k];
                if (!prop->anim_value || !prop->element) continue;
                
                ufbx_node* node = ufbx_as_node(prop->element);
                if (!node) continue;
                
                std::string boneName = node->name.data;
                
                // If the bone name is empty, try to look it up from our node map
                if (boneName.empty())
                {
                    // The target node might be a root node we renamed earlier
                    if (nodeIdToIndexMap.count(node->element_id))
                    {
                        int nodeIndex = nodeIdToIndexMap[node->element_id];
                        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(rawData->nodes.size()))
                        {
                            boneName = rawData->nodes[nodeIndex].name;
                            juce::Logger::writeToLog("FbxLoader: Animation property targets unnamed node, resolved to '" + 
                                                   juce::String(boneName) + "' from node map.");
                        }
                    }
                }
                
                // After attempting to resolve, if it's STILL empty, skip this track
                if (boneName.empty())
                {
                    juce::Logger::writeToLog("FbxLoader WARNING: Skipping animation property for node ID " + 
                                           juce::String(node->element_id) + " because its name could not be resolved.");
                    continue; // Safely skip this animation track
                }
                
                RawBoneAnimation& boneAnim = clip.boneAnimations[boneName];
                boneAnim.boneName = boneName; // Explicitly set the bone name
                
                // Translation
                if (strcmp(prop->prop_name.data, "Lcl Translation") == 0) {
                    if (prop->anim_value->curves[0]) {
                        for (size_t m = 0; m < prop->anim_value->curves[0]->keyframes.count; ++m) {
                            double time = prop->anim_value->curves[0]->keyframes.data[m].time;
                            ufbx_vec3 value = ufbx_evaluate_anim_value_vec3(prop->anim_value, time);
                            boneAnim.positions.keyframeTimes.push_back(time);
                            boneAnim.positions.keyframeValues.push_back(glm::vec4(value.x, value.y, value.z, 0.0f));
                        }
                    }
                }
                // Rotation
                else if (strcmp(prop->prop_name.data, "Lcl Rotation") == 0) {
                    if (prop->anim_value->curves[0]) {
                        for (size_t m = 0; m < prop->anim_value->curves[0]->keyframes.count; ++m) {
                            double time = prop->anim_value->curves[0]->keyframes.data[m].time;
                            ufbx_vec3 eulerDeg = ufbx_evaluate_anim_value_vec3(prop->anim_value, time);
                            ufbx_quat q = ufbx_euler_to_quat(eulerDeg, UFBX_ROTATION_ORDER_XYZ);
                            boneAnim.rotations.keyframeTimes.push_back(time);
                            boneAnim.rotations.keyframeValues.push_back(glm::vec4(q.x, q.y, q.z, q.w));
                        }
                    }
                }
                // Scale
                else if (strcmp(prop->prop_name.data, "Lcl Scaling") == 0) {
                    if (prop->anim_value->curves[0]) {
                        for (size_t m = 0; m < prop->anim_value->curves[0]->keyframes.count; ++m) {
                            double time = prop->anim_value->curves[0]->keyframes.data[m].time;
                            ufbx_vec3 value = ufbx_evaluate_anim_value_vec3(prop->anim_value, time);
                            boneAnim.scales.keyframeTimes.push_back(time);
                            boneAnim.scales.keyframeValues.push_back(glm::vec4(value.x, value.y, value.z, 0.0f));
                        }
                    }
                }
            }
        }
        rawData->clips.push_back(clip);
    }

    juce::Logger::writeToLog("FbxLoader: Finished creating RawAnimationData. Bones: " + juce::String(rawData->bones.size()) + ", Clips: " + juce::String(rawData->clips.size()));
    
    // --- VALIDATION STEP ---
    // Verify the data integrity before returning it
    std::string validationError;
    if (!RawAnimationData::validate(*rawData, validationError))
    {
        // Validation failed - log the specific error and return nullptr
        juce::Logger::writeToLog("FbxLoader ERROR: Raw data validation failed for file: " + juce::String(filePath));
        juce::Logger::writeToLog("Validation message: " + juce::String(validationError));
        
        // Free the ufbx scene before returning
        ufbx_free_scene(scene);
        
        // Return nullptr to signal that loading has failed
        return nullptr;
    }
    
    juce::Logger::writeToLog("FbxLoader: Raw data validated successfully.");
    // --- END OF VALIDATION STEP ---
    
    ufbx_free_scene(scene);
    return rawData;
}
