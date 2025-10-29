#define GLM_ENABLE_EXPERIMENTAL
#include "Animator.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

Animator::Animator(AnimationData* animationData) {
    m_CurrentTime = 0.0;
    m_AnimationData = animationData;
    m_CurrentAnimation = nullptr;
    m_AnimationSpeed = 1.0f;
    if(m_AnimationData && !m_AnimationData->boneInfoMap.empty()) {
        m_FinalBoneMatrices.resize(m_AnimationData->boneInfoMap.size(), glm::mat4(1.0f));
        m_BoneWorldTransforms.resize(m_AnimationData->boneInfoMap.size(), glm::mat4(1.0f));
    }
}

void Animator::PlayAnimation(const std::string& animationName) {
    if(!m_AnimationData) return;
    for (auto& clip : m_AnimationData->animationClips) {
        if (clip.name == animationName) {
            m_CurrentAnimation = &clip;
            m_CurrentTime = 0.0f;
            
            // Pre-link bone animations to nodes (MAIN THREAD ONLY - string operations here!)
            // This eliminates ALL string lookups in the audio thread
            LinkBoneAnimationsToNodes(&m_AnimationData->rootNode);
            return;
        }
    }
}

// Recursively pre-link bone animations to node tree (called on main thread)
void Animator::LinkBoneAnimationsToNodes(NodeData* node) {
    if (!node || !m_CurrentAnimation) return;
    
    // Try to find a bone animation for this node
    if (m_CurrentAnimation->boneAnimations.count(node->name)) {
        node->currentBoneAnimation = &m_CurrentAnimation->boneAnimations[node->name];
    } else {
        node->currentBoneAnimation = nullptr;
    }
    
    // Recurse to children
    for (auto& child : node->children) {
        LinkBoneAnimationsToNodes(&child);
    }
}

void Animator::Update(float deltaTime) {
    // NO LOGGING ALLOWED HERE - called from audio thread!
    if (!m_AnimationData || !m_CurrentAnimation || m_CurrentAnimation->durationInTicks <= 0.0f) 
    {
        return;
    }
    
    m_CurrentTime += m_CurrentAnimation->ticksPerSecond * deltaTime * m_AnimationSpeed;
    m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->durationInTicks);
    
    CalculateBoneTransform(&m_AnimationData->rootNode, glm::mat4(1.0f));
}

void Animator::CalculateBoneTransform(NodeData* node, const glm::mat4& parentTransform) {
    if(!node) return;

    BoneAnimation* boneAnim = node->currentBoneAnimation;
    glm::mat4 nodeTransform; // This will hold the final local transform for this node.

    // === START: THE DEFINITIVE FIX ===
    // If there is an active animation clip affecting this specific bone...
    if (boneAnim)
    {
        // ...then we decompose the bone's base transform, apply the animation keyframes,
        // and recompose a new transform matrix for this frame.
        glm::vec3 scale, translation;
        glm::quat rotation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(node->transformation, scale, rotation, translation, skew, perspective);

        // These calls are safe even if a track (e.g., scale) is missing from the animation
        translation = InterpolatePosition(m_CurrentTime, boneAnim->positions, translation);
        rotation    = InterpolateRotation(m_CurrentTime, boneAnim->rotations, rotation);
        scale       = InterpolateScale(m_CurrentTime, boneAnim->scales, scale);
        
        nodeTransform = glm::translate(glm::mat4(1.0f), translation) * glm::toMat4(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }
    else
    {
        // ...otherwise, if this bone is NOT animated, we use its original, unmodified
        // transformation matrix. This prevents the decompose/recompose cycle that was
        // corrupting the transforms by introducing accumulating floating-point errors.
        nodeTransform = node->transformation;
    }
    // === END: THE DEFINITIVE FIX ===

    glm::mat4 globalTransform = parentTransform * nodeTransform;

    // Use pre-linked bone index and offset matrix (NO string operations!)
    if (node->boneIndex >= 0 && node->boneIndex < m_FinalBoneMatrices.size())
    {
        // Store world transform for visualization (without offset matrix)
        m_BoneWorldTransforms[node->boneIndex] = globalTransform;
        // Apply the offset matrix to get the final skinning transform
        m_FinalBoneMatrices[node->boneIndex] = globalTransform * node->offsetMatrix;
    }

    for (auto& child : node->children) {
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
