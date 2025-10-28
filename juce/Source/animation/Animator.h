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

    const std::vector<glm::mat4>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }

private:
    glm::vec3 InterpolatePosition(float animationTime, const std::vector<KeyPosition>& keyframes, const glm::vec3& defaultPos);
    glm::quat InterpolateRotation(float animationTime, const std::vector<KeyRotation>& keyframes, const glm::quat& defaultRot);
    glm::vec3 InterpolateScale(float animationTime, const std::vector<KeyScale>& keyframes, const glm::vec3& defaultScale);
    void CalculateBoneTransform(const NodeData* node, const glm::mat4& parentTransform);

    std::vector<glm::mat4> m_FinalBoneMatrices;
    AnimationData* m_AnimationData;
    AnimationClip* m_CurrentAnimation;
    float m_CurrentTime;
    float m_AnimationSpeed;
};
