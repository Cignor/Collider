#include "GltfLoader.h"
#include "tiny_gltf.h" // Include the actual library here
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

// TODO: Implement ParseSkin and ParseAnimations below...

