#pragma once

#include <vector>
#include <string>
#include "AnimationData.h"
#include <glm/glm.hpp>
#include <juce_core/juce_core.h> // For JUCE_ASSERT

class Animator
{
public:
    Animator(AnimationData* animationData);

    void Update(float deltaTime);
    void PlayAnimation(const std::string& animationName);
    void SetAnimationSpeed(float speed) { m_AnimationSpeed = speed; }
    float GetAnimationSpeed() const { return m_AnimationSpeed; }
    
    const std::vector<glm::mat4>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }
    const std::vector<glm::mat4>& GetBoneWorldTransforms() const { return m_BoneWorldTransforms; }
    const AnimationData* GetAnimationData() const { return m_AnimationData; }
    float GetCurrentTime() const { return m_CurrentTime; }
    const AnimationClip* GetCurrentAnimation() const { return m_CurrentAnimation; }

private:
    glm::vec3 InterpolatePosition(float animationTime, const std::vector<KeyPosition>& keyframes, const glm::vec3& defaultPos);
    glm::quat InterpolateRotation(float animationTime, const std::vector<KeyRotation>& keyframes, const glm::quat& defaultRot);
    glm::vec3 InterpolateScale(float animationTime, const std::vector<KeyScale>& keyframes, const glm::vec3& defaultScale);
    void CalculateBoneTransform(NodeData* node, const glm::mat4& parentTransform);
    void LinkBoneAnimationsToNodes(NodeData* node); // Pre-link animations to nodes (called on main thread)

    std::vector<glm::mat4> m_FinalBoneMatrices; // For skinning (includes offset matrix)
    std::vector<glm::mat4> m_BoneWorldTransforms; // For visualization (world positions only)
    AnimationData* m_AnimationData;
    AnimationClip* m_CurrentAnimation;
    float m_CurrentTime;
    float m_AnimationSpeed;
};
