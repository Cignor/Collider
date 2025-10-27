#include "Animator.h"

Animator::Animator(AnimationData* animationData)
{
    m_CurrentTime = 0.0;
    m_AnimationData = animationData;
    m_CurrentAnimation = nullptr;
    m_AnimationSpeed = 1.0f;

    // The final bone matrices array is resized to the total number of bones.
    // We initialize all matrices to the identity matrix.
    m_FinalBoneMatrices.resize(m_AnimationData->boneInfoMap.size(), glm::mat4(1.0f));
}

void Animator::PlayAnimation(const std::string& animationName)
{
    for (auto& clip : m_AnimationData->animationClips)
    {
        if (clip.name == animationName)
        {
            m_CurrentAnimation = &clip;
            m_CurrentTime = 0.0f; // Reset time when a new animation starts
            return;
        }
    }
    // If the animation is not found, you might want to log a warning.
    // For now, we'll just do nothing.
}

void Animator::Update(float deltaTime)
{
    if (!m_CurrentAnimation)
    {
        return; // Do nothing if no animation is playing
    }

    // Advance the current time, applying the animation speed
    m_CurrentTime += m_CurrentAnimation->ticksPerSecond * deltaTime * m_AnimationSpeed;

    // Loop the animation
    m_CurrentTime = fmod(m_CurrentTime, m_CurrentAnimation->durationInTicks);

    // Start the recursive calculation from the root node of the skeleton
    CalculateBoneTransform(&m_AnimationData->rootNode, glm::mat4(1.0f));
}

void Animator::CalculateBoneTransform(const NodeData* node, const glm::mat4& parentTransform)
{
    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation;

    // Find the animation data for the current node (bone)
    BoneAnimation* boneAnim = nullptr;
    if (m_CurrentAnimation->boneAnimations.count(nodeName))
    {
        boneAnim = &m_CurrentAnimation->boneAnimations[nodeName];
    }

    // Calculate the local transformation by interpolating keyframes if animation data exists
    if (boneAnim)
    {
        glm::mat4 translationMatrix = InterpolatePosition(m_CurrentTime, *boneAnim);
        glm::mat4 rotationMatrix = InterpolateRotation(m_CurrentTime, *boneAnim);
        glm::mat4 scaleMatrix = InterpolateScale(m_CurrentTime, *boneAnim);
        nodeTransform = translationMatrix * rotationMatrix * scaleMatrix;
    }

    // Combine with the parent's transform to get the global transform
    glm::mat4 globalTransform = parentTransform * nodeTransform;

    // If this node is a bone that influences the mesh, calculate its final skinning matrix
    if (m_AnimationData->boneInfoMap.count(nodeName))
    {
        int boneIndex = m_AnimationData->boneInfoMap[nodeName].id;
        glm::mat4 offset = m_AnimationData->boneInfoMap[nodeName].offsetMatrix;
        m_FinalBoneMatrices[boneIndex] = globalTransform * offset;
    }

    // Recurse for all children, passing down the new global transform
    for (const auto& child : node->children)
    {
        CalculateBoneTransform(&child, globalTransform);
    }
}

glm::mat4 Animator::InterpolatePosition(float animationTime, const BoneAnimation& boneAnim)
{
    if (boneAnim.positions.size() == 1)
        return glm::translate(glm::mat4(1.0f), boneAnim.positions[0].position);

    int p0Index = -1;
    for (size_t i = 0; i < boneAnim.positions.size() - 1; ++i)
    {
        if (animationTime < boneAnim.positions[i + 1].timeStamp)
        {
            p0Index = i;
            break;
        }
    }
    int p1Index = p0Index + 1;

    float scaleFactor = (animationTime - boneAnim.positions[p0Index].timeStamp) / 
                        (boneAnim.positions[p1Index].timeStamp - boneAnim.positions[p0Index].timeStamp);

    glm::vec3 finalPosition = glm::mix(boneAnim.positions[p0Index].position,
                                       boneAnim.positions[p1Index].position, scaleFactor);

    return glm::translate(glm::mat4(1.0f), finalPosition);
}

glm::mat4 Animator::InterpolateScale(float animationTime, const BoneAnimation& boneAnim)
{
    if (boneAnim.scales.size() == 1)
        return glm::scale(glm::mat4(1.0f), boneAnim.scales[0].scale);

    int p0Index = -1;
    for (size_t i = 0; i < boneAnim.scales.size() - 1; ++i)
    {
        if (animationTime < boneAnim.scales[i + 1].timeStamp)
        {
            p0Index = i;
            break;
        }
    }
    int p1Index = p0Index + 1;

    float scaleFactor = (animationTime - boneAnim.scales[p0Index].timeStamp) /
                        (boneAnim.scales[p1Index].timeStamp - boneAnim.scales[p0Index].timeStamp);

    glm::vec3 finalScale = glm::mix(boneAnim.scales[p0Index].scale,
                                    boneAnim.scales[p1Index].scale, scaleFactor);

    return glm::scale(glm::mat4(1.0f), finalScale);
}

glm::mat4 Animator::InterpolateRotation(float animationTime, const BoneAnimation& boneAnim)
{
    if (boneAnim.rotations.size() == 1)
        return glm::toMat4(glm::normalize(boneAnim.rotations[0].orientation));

    int p0Index = -1;
    for (size_t i = 0; i < boneAnim.rotations.size() - 1; ++i)
    {
        if (animationTime < boneAnim.rotations[i + 1].timeStamp)
        {
            p0Index = i;
            break;
        }
    }
    int p1Index = p0Index + 1;

    float scaleFactor = (animationTime - boneAnim.rotations[p0Index].timeStamp) /
                        (boneAnim.rotations[p1Index].timeStamp - boneAnim.rotations[p0Index].timeStamp);

    glm::quat finalRotation = glm::slerp(boneAnim.rotations[p0Index].orientation,
                                         boneAnim.rotations[p1Index].orientation, scaleFactor);
    finalRotation = glm::normalize(finalRotation);
    return glm::toMat4(finalRotation);
}

