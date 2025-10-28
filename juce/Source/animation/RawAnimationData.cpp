#include "RawAnimationData.h"
#include <sstream>

bool RawAnimationData::validate(const RawAnimationData& data, std::string& outErrorMessage)
{
    // === Check 1: Ensure there is at least one node ===
    if (data.nodes.empty())
    {
        outErrorMessage = "Validation Failed: The 'nodes' array is empty. At least one node is required.";
        return false;
    }
    
    const int numNodes = static_cast<int>(data.nodes.size());
    
    // === Check 2: Ensure there is at least one bone ===
    // Note: Some animation files might not have bones (e.g., camera animations)
    // but for skeletal animation we need at least one bone
    if (data.bones.empty())
    {
        outErrorMessage = "Validation Failed: The 'bones' array is empty. At least one bone is required for skeletal animation.";
        return false;
    }
    
    // === Check 3: Validate bone data ===
    for (size_t i = 0; i < data.bones.size(); ++i)
    {
        const auto& bone = data.bones[i];
        
        // Check bone has a name
        if (bone.name.empty())
        {
            std::ostringstream oss;
            oss << "Validation Failed: Bone at index " << i << " has an empty name.";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Check bone ID is valid
        if (bone.id < 0 || bone.id >= static_cast<int>(data.bones.size()))
        {
            std::ostringstream oss;
            oss << "Validation Failed: Bone '" << bone.name 
                << "' has an invalid ID (" << bone.id 
                << "). It must be between 0 and " << (data.bones.size() - 1) << ".";
            outErrorMessage = oss.str();
            return false;
        }
    }
    
    // === Check 4: Validate node parent indices ===
    int rootNodeCount = 0;
    for (int i = 0; i < numNodes; ++i)
    {
        const auto& node = data.nodes[i];
        
        // Check node has a name
        if (node.name.empty())
        {
            std::ostringstream oss;
            oss << "Validation Failed: Node at index " << i << " has an empty name.";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Validate parentIndex
        // Must be -1 (root) or a valid index in the range [0, numNodes-1]
        if (node.parentIndex < -1 || node.parentIndex >= numNodes)
        {
            std::ostringstream oss;
            oss << "Validation Failed: Node '" << node.name 
                << "' at index " << i << " has an invalid parentIndex (" << node.parentIndex 
                << "). It must be -1 (for root) or between 0 and " << (numNodes - 1) << ".";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Check for self-referencing parent (node can't be its own parent)
        if (node.parentIndex == i)
        {
            std::ostringstream oss;
            oss << "Validation Failed: Node '" << node.name 
                << "' at index " << i << " has itself as parent (circular reference).";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Count root nodes (should have at least one, but usually just one)
        if (node.parentIndex == -1)
        {
            rootNodeCount++;
        }
    }
    
    // === Check 5: Ensure there's at least one root node ===
    if (rootNodeCount == 0)
    {
        outErrorMessage = "Validation Failed: No root node found. At least one node must have parentIndex = -1.";
        return false;
    }
    
    // === Check 6: Validate node child indices ===
    for (int i = 0; i < numNodes; ++i)
    {
        const auto& node = data.nodes[i];
        
        for (size_t childIdx = 0; childIdx < node.childIndices.size(); ++childIdx)
        {
            int childIndex = node.childIndices[childIdx];
            
            // Child index must be valid
            if (childIndex < 0 || childIndex >= numNodes)
            {
                std::ostringstream oss;
                oss << "Validation Failed: Node '" << node.name 
                    << "' at index " << i << " has an invalid childIndex (" << childIndex 
                    << ") at position " << childIdx 
                    << ". It must be between 0 and " << (numNodes - 1) << ".";
                outErrorMessage = oss.str();
                return false;
            }
            
            // Check for self-referencing child (node can't be its own child)
            if (childIndex == i)
            {
                std::ostringstream oss;
                oss << "Validation Failed: Node '" << node.name 
                    << "' at index " << i << " has itself as child (circular reference).";
                outErrorMessage = oss.str();
                return false;
            }
        }
    }
    
    // === Check 7: Validate animation clip data ===
    for (size_t clipIdx = 0; clipIdx < data.clips.size(); ++clipIdx)
    {
        const auto& clip = data.clips[clipIdx];
        
        // Check clip has a name
        if (clip.name.empty())
        {
            std::ostringstream oss;
            oss << "Validation Failed: Animation clip at index " << clipIdx << " has an empty name.";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Check clip has valid duration
        if (clip.duration < 0.0)
        {
            std::ostringstream oss;
            oss << "Validation Failed: Animation clip '" << clip.name 
                << "' has negative duration (" << clip.duration << ").";
            outErrorMessage = oss.str();
            return false;
        }
        
        // Check animation tracks
        for (const auto& pair : clip.boneAnimations)
        {
            const auto& boneAnim = pair.second;
            
            // Check bone animation has a valid bone name
            if (boneAnim.boneName.empty())
            {
                std::ostringstream oss;
                oss << "Validation Failed: Animation clip '" << clip.name 
                    << "' has a bone animation with empty bone name.";
                outErrorMessage = oss.str();
                return false;
            }
            
            // Check that keyframe times and values arrays match in size
            if (!boneAnim.positions.keyframeTimes.empty() && 
                boneAnim.positions.keyframeTimes.size() != boneAnim.positions.keyframeValues.size())
            {
                std::ostringstream oss;
                oss << "Validation Failed: Animation clip '" << clip.name 
                    << "', bone '" << boneAnim.boneName 
                    << "' has mismatched position keyframe times (" << boneAnim.positions.keyframeTimes.size()
                    << ") and values (" << boneAnim.positions.keyframeValues.size() << ").";
                outErrorMessage = oss.str();
                return false;
            }
            
            if (!boneAnim.rotations.keyframeTimes.empty() && 
                boneAnim.rotations.keyframeTimes.size() != boneAnim.rotations.keyframeValues.size())
            {
                std::ostringstream oss;
                oss << "Validation Failed: Animation clip '" << clip.name 
                    << "', bone '" << boneAnim.boneName 
                    << "' has mismatched rotation keyframe times (" << boneAnim.rotations.keyframeTimes.size()
                    << ") and values (" << boneAnim.rotations.keyframeValues.size() << ").";
                outErrorMessage = oss.str();
                return false;
            }
            
            if (!boneAnim.scales.keyframeTimes.empty() && 
                boneAnim.scales.keyframeTimes.size() != boneAnim.scales.keyframeValues.size())
            {
                std::ostringstream oss;
                oss << "Validation Failed: Animation clip '" << clip.name 
                    << "', bone '" << boneAnim.boneName 
                    << "' has mismatched scale keyframe times (" << boneAnim.scales.keyframeTimes.size()
                    << ") and values (" << boneAnim.scales.keyframeValues.size() << ").";
                outErrorMessage = oss.str();
                return false;
            }
        }
    }
    
    // === All checks passed! ===
    outErrorMessage = "Validation Succeeded: All checks passed.";
    return true;
}



