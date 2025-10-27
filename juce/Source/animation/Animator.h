#pragma once

#include <vector>
#include <string>
#include "AnimationData.h"
#include <glm/glm.hpp>

class Animator
{
public:
    Animator(AnimationData* animationData);

    void Update(float deltaTime);
    void PlayAnimation(const std::string& animationName);
    void SetAnimationSpeed(float speed) { m_AnimationSpeed = speed; }

    const std::vector<glm::mat4>& GetFinalBoneMatrices() const { return m_FinalBoneMatrices; }

private:
    // Helper methods for interpolation and kinematics
    glm::mat4 InterpolatePosition(float animationTime, const BoneAnimation& boneAnim);
    glm::mat4 InterpolateRotation(float animationTime, const BoneAnimation& boneAnim);
    glm::mat4 InterpolateScale(float animationTime, const BoneAnimation& boneAnim);
    void CalculateBoneTransform(const NodeData* node, const glm::mat4& parentTransform);

    std::vector<glm::mat4> m_FinalBoneMatrices;
    AnimationData* m_AnimationData;
    AnimationClip* m_CurrentAnimation;
    float m_CurrentTime;
    float m_AnimationSpeed;
};

