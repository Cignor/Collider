#define GLM_ENABLE_EXPERIMENTAL

#include "GltfLoader.h"
#include "tiny_gltf.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <juce_core/juce_core.h>

// --- Helper function declarations ---
static void ParseNodes(const tinygltf::Model& model, RawAnimationData& outData);
static void ParseSkins(const tinygltf::Model& model, RawAnimationData& outData);
static void ParseAnimations(const tinygltf::Model& model, RawAnimationData& outData);

template <typename T>
static void ReadDataFromBuffer(const tinygltf::Model& model, int accessorIndex, std::vector<T>& outData);

static glm::mat4 GetMatrix(const tinygltf::Node& node);
static glm::vec3 GetVec3(const std::vector<double>& vec);
static glm::quat GetQuat(const std::vector<double>& vec);
// --- End of helpers ---

std::unique_ptr<RawAnimationData> GltfLoader::LoadFromFile(const std::string& filePath)
{
    juce::Logger::writeToLog("GltfLoader: Starting to load " + juce::String(filePath));
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool success = false;
    if (filePath.substr(filePath.length() - 4) == ".glb") {
        juce::Logger::writeToLog("GltfLoader: Loading as Binary (.glb)");
        success = loader.LoadBinaryFromFile(&model, &err, &warn, filePath);
    } else {
        juce::Logger::writeToLog("GltfLoader: Loading as ASCII (.gltf)");
        success = loader.LoadASCIIFromFile(&model, &err, &warn, filePath);
    }

    if (!warn.empty()) juce::Logger::writeToLog("GltfLoader WARNING: " + juce::String(warn));
    if (!err.empty()) juce::Logger::writeToLog("GltfLoader ERROR: " + juce::String(err));
    if (!success) {
        juce::Logger::writeToLog("GltfLoader: FAILED to parse file with tinygltf.");
        return nullptr;
    }
    juce::Logger::writeToLog("GltfLoader: Successfully parsed with tinygltf.");

    auto rawData = std::make_unique<RawAnimationData>();

    ParseNodes(model, *rawData);
    ParseSkins(model, *rawData);
    ParseAnimations(model, *rawData);
    
    juce::Logger::writeToLog("GltfLoader: Finished creating RawAnimationData.");
    return rawData;
}


// --- Implementation of Helper Functions ---

void ParseNodes(const tinygltf::Model& model, RawAnimationData& outData)
{
    outData.nodes.resize(model.nodes.size());

    for (size_t i = 0; i < model.nodes.size(); ++i)
    {
        const auto& inputNode = model.nodes[i];
        auto& outputNode = outData.nodes[i];

        outputNode.name = inputNode.name;
        outputNode.localTransform = GetMatrix(inputNode);

        for (int childIndex : inputNode.children) {
            outputNode.childIndices.push_back(childIndex);
            // We find the parent later, but we need to know who the parent of the child is
            // This is slightly inefficient but safe.
            if(outData.nodes.size() > childIndex)
                outData.nodes[childIndex].parentIndex = i;
        }
    }
}

void ParseSkins(const tinygltf::Model& model, RawAnimationData& outData)
{
    if (!model.skins.empty())
    {
        juce::Logger::writeToLog("GltfLoader: Found explicit skin data. Parsing bones from skin.");
        const auto& skin = model.skins[0];
        outData.bones.resize(skin.joints.size());

        std::vector<glm::mat4> inverseBindMatrices;
        ReadDataFromBuffer(model, skin.inverseBindMatrices, inverseBindMatrices);

        for (size_t i = 0; i < skin.joints.size(); ++i)
        {
            int jointNodeIndex = skin.joints[i];
            const auto& jointNode = model.nodes[jointNodeIndex];
            
            auto& boneInfo = outData.bones[i];
            boneInfo.id = i;
            boneInfo.name = jointNode.name;
            boneInfo.offsetMatrix = inverseBindMatrices[i];
        }
    }
    else
    {
        juce::Logger::writeToLog("GltfLoader: No skin data found. Using fallback: creating bones from animation targets.");
        // FALLBACK: Create bones from any node that is animated.
        std::map<std::string, int> boneNameMap;
        for (const auto& anim : model.animations) {
            for (const auto& channel : anim.channels) {
                std::string boneName = model.nodes[channel.target_node].name;
                if (boneNameMap.find(boneName) == boneNameMap.end()) {
                    boneNameMap[boneName] = outData.bones.size();
                    RawBoneInfo boneInfo;
                    boneInfo.id = boneNameMap[boneName];
                    boneInfo.name = boneName;
                    boneInfo.offsetMatrix = glm::mat4(1.0f); // Default to identity matrix
                    outData.bones.push_back(boneInfo);
                }
            }
        }
        juce::Logger::writeToLog("GltfLoader: Fallback created " + juce::String(outData.bones.size()) + " bones from animation data.");
    }
}

void ParseAnimations(const tinygltf::Model& model, RawAnimationData& outData)
{
    for (const auto& anim : model.animations)
    {
        RawAnimationClip clip;
        clip.name = anim.name;
        float maxTimestamp = 0.0f;

        for (const auto& channel : anim.channels)
        {
            const auto& sampler = anim.samplers[channel.sampler];
            std::string boneName = model.nodes[channel.target_node].name;
            RawBoneAnimation& boneAnim = clip.boneAnimations[boneName];

            std::vector<float> timestampsFloat;
            ReadDataFromBuffer(model, sampler.input, timestampsFloat);

            if (!timestampsFloat.empty()) maxTimestamp = std::max(maxTimestamp, timestampsFloat.back());

            // THIS IS THE FIX: Only assign timestamps and values to the correct track.
            if (channel.target_path == "translation")
            {
                boneAnim.positions.keyframeTimes.assign(timestampsFloat.begin(), timestampsFloat.end());
                std::vector<glm::vec3> values;
                ReadDataFromBuffer(model, sampler.output, values);
                for(const auto& v : values) boneAnim.positions.keyframeValues.emplace_back(v, 0.0f);
            }
            else if (channel.target_path == "rotation")
            {
                boneAnim.rotations.keyframeTimes.assign(timestampsFloat.begin(), timestampsFloat.end());
                std::vector<glm::quat> values;
                ReadDataFromBuffer(model, sampler.output, values);
                for(const auto& v : values) boneAnim.rotations.keyframeValues.emplace_back(v.x, v.y, v.z, v.w);
            }
            else if (channel.target_path == "scale")
            {
                boneAnim.scales.keyframeTimes.assign(timestampsFloat.begin(), timestampsFloat.end());
                std::vector<glm::vec3> values;
                ReadDataFromBuffer(model, sampler.output, values);
                for(const auto& v : values) boneAnim.scales.keyframeValues.emplace_back(v, 0.0f);
            }
        }
        clip.duration = maxTimestamp;
        outData.clips.push_back(clip);
    }
}


template <typename T>
void ReadDataFromBuffer(const tinygltf::Model& model, int accessorIndex, std::vector<T>& outData)
{
    const auto& accessor = model.accessors[accessorIndex];
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[bufferView.buffer];
    const unsigned char* dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
    size_t numElements = accessor.count;
    outData.resize(numElements);
    memcpy(outData.data(), dataPtr, numElements * sizeof(T));
}

glm::mat4 GetMatrix(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16) return glm::make_mat4(node.matrix.data());
    glm::vec3 t = node.translation.empty() ? glm::vec3(0.0f) : GetVec3(node.translation);
    glm::quat r = node.rotation.empty() ? glm::quat(1.0f, 0.0f, 0.0f, 0.0f) : GetQuat(node.rotation);
    glm::vec3 s = node.scale.empty() ? glm::vec3(1.0f) : GetVec3(node.scale);
    return glm::translate(glm::mat4(1.0f), t) * glm::toMat4(r) * glm::scale(glm::mat4(1.0f), s);
}

glm::vec3 GetVec3(const std::vector<double>& vec) { return { (float)vec[0], (float)vec[1], (float)vec[2] }; }
glm::quat GetQuat(const std::vector<double>& vec) { return { (float)vec[3], (float)vec[0], (float)vec[1], (float)vec[2] }; }
