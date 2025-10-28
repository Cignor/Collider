#define GLM_ENABLE_EXPERIMENTAL
#include "Animator.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

Animator::Animator(AnimationData* animationData) {
    m_CurrentTime = 0.0;
    m_AnimationData = animationData;
    m_CurrentAnimation = nullptr;
    m_AnimationSpeed = 1.0f;

    // THIS IS THE CRITICAL FIX: Check if the data is valid before using it.
    if (m_AnimationData && !m_AnimationData->boneInfoMap.empty()) {
        m_FinalBoneMatrices.resize(m_AnimationData->boneInfoMap.size(), glm::mat4(1.0f));
    }
}

void Animator::PlayAnimation(const std::string& animationName) {
    if (!m_AnimationData) return;
    for (auto& clip : m_AnimationData->animationClips) {
        if (clip.name == animationName) {
            m_CurrentAnimation = &clip;
            m_CurrentTime = 0.0f;
            return;
        }
    }
}

void Animator::Update(float deltaTime) {
    if (!m_AnimationData || !m_CurrentAnimation || m_CurrentAnimation->durationInTicks <= 0.0f) return;
    m_CurrentTime += m_CurrentAnimation->ticksPerSecond * deltaTime * m_AnimationSpeed;
    m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->durationInTicks);
    CalculateBoneTransform(&m_AnimationData->rootNode, glm::mat4(1.0f));
}

void Animator::CalculateBoneTransform(const NodeData* node, const glm::mat4& parentTransform) {
    if (!node) return;
    std::string nodeName = node->name;
    
    BoneAnimation* boneAnim = nullptr;
    if (m_CurrentAnimation && m_CurrentAnimation->boneAnimations.count(nodeName)) {
        boneAnim = &m_CurrentAnimation->boneAnimations[nodeName];
    }
    
    glm::vec3 scale, translation;
    glm::quat rotation;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(node->transformation, scale, rotation, translation, skew, perspective);

    translation = InterpolatePosition(m_CurrentTime, boneAnim ? boneAnim->positions : std::vector<KeyPosition>(), translation);
    rotation = InterpolateRotation(m_CurrentTime, boneAnim ? boneAnim->rotations : std::vector<KeyRotation>(), rotation);
    scale = InterpolateScale(m_CurrentTime, boneAnim ? boneAnim->scales : std::vector<KeyScale>(), scale);
    
    glm::mat4 nodeTransform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
    glm::mat4 globalTransform = parentTransform * nodeTransform;

    if (m_AnimationData->boneInfoMap.count(nodeName)) {
        int boneIndex = m_AnimationData->boneInfoMap.at(nodeName).id;
        if (boneIndex >= 0 && boneIndex < m_FinalBoneMatrices.size())
            m_FinalBoneMatrices[boneIndex] = globalTransform;
    }
    for (const auto& child : node->children) {
        CalculateBoneTransform(&child, globalTransform);
    }
}

// --- Interpolation Helpers ---
glm::vec3 Animator::InterpolatePosition(float animationTime, const std::vector<KeyPosition>& keyframes, const glm::vec3& defaultPos) {
    if (keyframes.empty()) return defaultPos;
    if (keyframes.size() == 1) return keyframes[0].position;
    
    int p0Index = 0;
    if (animationTime >= keyframes.back().timeStamp) return keyframes.back().position;
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (animationTime < keyframes[i + 1].timeStamp) { p0Index = i; break; }
    }
    int p1Index = p0Index + 1;
    if (p1Index >= keyframes.size()) return keyframes[p0Index].position;

    float p1Time = keyframes[p1Index].timeStamp;
    float p0Time = keyframes[p0Index].timeStamp;
    float scaleFactor = (p1Time - p0Time > 0.0f) ? (animationTime - p0Time) / (p1Time - p0Time) : 0.0f;
    return glm::mix(keyframes[p0Index].position, keyframes[p1Index].position, scaleFactor);
}

glm::quat Animator::InterpolateRotation(float animationTime, const std::vector<KeyRotation>& keyframes, const glm::quat& defaultRot) {
    if (keyframes.empty()) return defaultRot;
    if (keyframes.size() == 1) return glm::normalize(keyframes[0].orientation);

    int p0Index = 0;
    if (animationTime >= keyframes.back().timeStamp) return glm::normalize(keyframes.back().orientation);
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (animationTime < keyframes[i + 1].timeStamp) { p0Index = i; break; }
    }
    int p1Index = p0Index + 1;
    if (p1Index >= keyframes.size()) return glm::normalize(keyframes[p0Index].orientation);

    float p1Time = keyframes[p1Index].timeStamp;
    float p0Time = keyframes[p0Index].timeStamp;
    float scaleFactor = (p1Time - p0Time > 0.0f) ? (animationTime - p0Time) / (p1Time - p0Time) : 0.0f;
    return glm::normalize(glm::slerp(keyframes[p0Index].orientation, keyframes[p1Index].orientation, scaleFactor));
}

glm::vec3 Animator::InterpolateScale(float animationTime, const std::vector<KeyScale>& keyframes, const glm::vec3& defaultScale) {
    if (keyframes.empty()) return defaultScale;
    if (keyframes.size() == 1) return keyframes[0].scale;

    int p0Index = 0;
    if (animationTime >= keyframes.back().timeStamp) return keyframes.back().scale;
    for (size_t i = 0; i < keyframes.size() - 1; ++i) {
        if (animationTime < keyframes[i + 1].timeStamp) { p0Index = i; break; }
    }
    int p1Index = p0Index + 1;
    if (p1Index >= keyframes.size()) return keyframes[p0Index].scale;

    float p1Time = keyframes[p1Index].timeStamp;
    float p0Time = keyframes[p0Index].timeStamp;
    float scaleFactor = (p1Time - p0Time > 0.0f) ? (animationTime - p0Time) / (p1Time - p0Time) : 0.0f;
    return glm::mix(keyframes[p0Index].scale, keyframes[p1Index].scale, scaleFactor);
}
