#pragma once

#include <string>
#include <memory>
#include "AnimationData.h"

// Forward-declarations to avoid including tiny_gltf.h in the header
namespace tinygltf {
    class Model;
    class Node;
    class Skin;
    class Animation;
}

class GltfLoader
{
public:
    // The main public function to load an animation from a file.
    static std::unique_ptr<AnimationData> LoadFromFile(const std::string& filePath);

private:
    // Private helper methods to parse different parts of the glTF file.
    static void ParseNodes(const tinygltf::Model& model, const tinygltf::Node& inputNode, NodeData& parentNode);
    static void ParseSkin(const tinygltf::Model& model, AnimationData& animData);
    static void ParseAnimations(const tinygltf::Model& model, AnimationData& animData);

    // Helper to read raw data from glTF buffers
    template <typename T>
    static void ReadDataFromBuffer(const tinygltf::Model& model, int accessorIndex, std::vector<T>& outData);

    // Helper to convert tinygltf matrices/vectors to glm types
    static glm::mat4 GetMatrix(const tinygltf::Node& node);
    static glm::vec3 GetVec3(const std::vector<double>& vec);
    static glm::quat GetQuat(const std::vector<double>& vec);
};

