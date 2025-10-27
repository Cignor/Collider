#define GLM_ENABLE_EXPERIMENTAL
#include "GltfLoader.h"
#include "tiny_gltf.h" // Include the actual library here
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

// Template helper to read raw data from a glTF buffer
template <typename T>
void GltfLoader::ReadDataFromBuffer(const tinygltf::Model& model, int accessorIndex, std::vector<T>& outData)
{
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& bufferView = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[bufferView.buffer];

    // Get a pointer to the start of the data for this accessor
    const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    
    // Get the number of elements to read
    size_t numElements = accessor.count;
    outData.resize(numElements);

    // Copy the data from the buffer into our output vector
    memcpy(outData.data(), dataPtr, numElements * sizeof(T));
}

// Main function to load a glTF file
std::unique_ptr<AnimationData> GltfLoader::LoadFromFile(const std::string& filePath)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool success = false;
    // Check file extension to decide which loading method to use
    if (filePath.substr(filePath.length() - 4) == ".glb") {
        success = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
    } else {
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);
    }

    if (!warn.empty()) {
        std::cout << "glTF WARNING: " << warn << std::endl;
    }
    if (!err.empty()) {
        std::cerr << "glTF ERROR: " << err << std::endl;
    }
    if (!success) {
        std::cerr << "Failed to load glTF file: " << filePath << std::endl;
        return nullptr;
    }

    // If loading was successful, create our AnimationData object
    auto animData = std::make_unique<AnimationData>();

    // The glTF scene contains the root node(s) of the hierarchy
    const auto& scene = model.scenes[model.defaultScene];
    // We assume the first node in the scene is the root of our skeleton
    const auto& rootNode = model.nodes[scene.nodes[0]];

    // Call the helper functions to parse the data
    ParseNodes(model, rootNode, animData->rootNode);
    ParseSkin(model, *animData);
    
    // If there's no skin data, extract bone info from the node hierarchy
    if (model.skins.empty() && animData->boneInfoMap.empty())
    {
        int boneCounter = 0;
        ExtractBonesFromNodes(animData->rootNode, *animData, boneCounter);
        std::cout << "No skin data found. Extracted " << boneCounter << " bones from node hierarchy." << std::endl;
    }
    
    ParseAnimations(model, *animData);
    
    return animData;
}

glm::mat4 GltfLoader::GetMatrix(const tinygltf::Node& node)
{
    // If the node has a full matrix defined, use it
    if (node.matrix.size() == 16) {
        return glm::make_mat4(node.matrix.data());
    }

    // Otherwise, build the matrix from TRS (Translation, Rotation, Scale) components
    glm::vec3 translation = node.translation.empty() ? glm::vec3(0.0f) : GetVec3(node.translation);
    glm::quat rotation = node.rotation.empty() ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : GetQuat(node.rotation);
    glm::vec3 scale = node.scale.empty() ? glm::vec3(1.0f) : GetVec3(node.scale);

    glm::mat4 transMat = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 rotMat = glm::toMat4(rotation);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), scale);

    return transMat * rotMat * scaleMat;
}

glm::vec3 GltfLoader::GetVec3(const std::vector<double>& vec)
{
    return glm::vec3(
        static_cast<float>(vec[0]),
        static_cast<float>(vec[1]),
        static_cast<float>(vec[2])
    );
}

glm::quat GltfLoader::GetQuat(const std::vector<double>& vec)
{
    // glTF quaternions are in (x, y, z, w) order
    // GLM quaternions are in (w, x, y, z) order for the constructor
    return glm::quat(
        static_cast<float>(vec[3]), // w
        static_cast<float>(vec[0]), // x
        static_cast<float>(vec[1]), // y
        static_cast<float>(vec[2])  // z
    );
}

void GltfLoader::ParseNodes(const tinygltf::Model& model, const tinygltf::Node& inputNode, NodeData& parentNode)
{
    // Copy data from the glTF node to our custom NodeData struct
    parentNode.name = inputNode.name;
    parentNode.transformation = GetMatrix(inputNode);

    // Recursively call this function for all children of the current node
    for (int childNodeIndex : inputNode.children)
    {
        const tinygltf::Node& childInputNode = model.nodes[childNodeIndex];
        
        // Create a new child in our hierarchy
        parentNode.children.emplace_back();
        NodeData& newChildNode = parentNode.children.back();
        
        // Set the parent pointer for the new child
        newChildNode.parent = &parentNode;
        
        // Recurse
        ParseNodes(model, childInputNode, newChildNode);
    }
}

void GltfLoader::ParseSkin(const tinygltf::Model& model, AnimationData& animData)
{
    // A model may not have skins, so we check first.
    if (model.skins.empty()) {
        return;
    }

    // We'll only process the first skin in the file.
    const auto& skin = model.skins[0];

    // Read all the inverse bind matrices from the buffer.
    std::vector<glm::mat4> inverseBindMatrices;
    ReadDataFromBuffer(model, skin.inverseBindMatrices, inverseBindMatrices);

    // Iterate through all the joints (bones) defined in the skin.
    // The order of joints here corresponds to the order of the matrices we just read.
    for (size_t i = 0; i < skin.joints.size(); ++i)
    {
        int jointNodeIndex = skin.joints[i];
        const auto& jointNode = model.nodes[jointNodeIndex];
        
        std::string boneName = jointNode.name;
        
        BoneInfo boneInfo;
        boneInfo.id = i; // The ID is the index in the joint array.
        boneInfo.name = boneName;
        boneInfo.offsetMatrix = inverseBindMatrices[i];
        
        // Add the new bone info to our map.
        animData.boneInfoMap[boneName] = boneInfo;
    }
}

void GltfLoader::ExtractBonesFromNodes(const NodeData& node, AnimationData& animData, int& boneCounter)
{
    // Create a BoneInfo entry for this node
    BoneInfo boneInfo;
    boneInfo.id = boneCounter++;
    boneInfo.name = node.name;
    // Use identity matrix as offset since there's no mesh binding
    boneInfo.offsetMatrix = glm::mat4(1.0f);
    
    // Add to the bone info map
    animData.boneInfoMap[node.name] = boneInfo;
    
    // Recursively process all children
    for (const auto& child : node.children)
    {
        ExtractBonesFromNodes(child, animData, boneCounter);
    }
}

void GltfLoader::ParseAnimations(const tinygltf::Model& model, AnimationData& animData)
{
    for (const auto& anim : model.animations)
    {
        AnimationClip clip;
        clip.name = anim.name;
        clip.ticksPerSecond = 1.0; // In glTF, time is always in seconds.
        float maxTimestamp = 0.0f;

        for (const auto& channel : anim.channels)
        {
            const auto& sampler = anim.samplers[channel.sampler];
            
            // Get the node (bone) that this channel is targeting.
            int targetNodeIndex = channel.target_node;
            std::string boneName = model.nodes[targetNodeIndex].name;

            // Get a reference to the BoneAnimation for this bone.
            // If it doesn't exist, it will be created.
            BoneAnimation& boneAnim = clip.boneAnimations[boneName];
            boneAnim.boneName = boneName;

            // Read the keyframe timestamps (input).
            std::vector<float> timestamps;
            ReadDataFromBuffer(model, sampler.input, timestamps);

            // Find the maximum timestamp to determine animation duration.
            if (!timestamps.empty()) {
                maxTimestamp = std::max(maxTimestamp, timestamps.back());
            }

            // Read the keyframe values (output) and populate the correct track.
            if (channel.target_path == "translation")
            {
                std::vector<glm::vec3> positions;
                ReadDataFromBuffer(model, sampler.output, positions);
                for (size_t i = 0; i < positions.size(); ++i) {
                    boneAnim.positions.push_back({positions[i], timestamps[i]});
                }
            }
            else if (channel.target_path == "rotation")
            {
                std::vector<glm::quat> rotations;
                ReadDataFromBuffer(model, sampler.output, rotations);
                for (size_t i = 0; i < rotations.size(); ++i) {
                    boneAnim.rotations.push_back({rotations[i], timestamps[i]});
                }
            }
            else if (channel.target_path == "scale")
            {
                std::vector<glm::vec3> scales;
                ReadDataFromBuffer(model, sampler.output, scales);
                for (size_t i = 0; i < scales.size(); ++i) {
                    boneAnim.scales.push_back({scales[i], timestamps[i]});
                }
            }
        }
        clip.durationInTicks = maxTimestamp;
        animData.animationClips.push_back(clip);
    }
}

