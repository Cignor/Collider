#pragma once

#include "ModuleProcessor.h"
#include "../../animation/GltfLoader.h"
#include "../../animation/Animator.h"
#include "../../animation/AnimationRenderer.h"
#include <memory>
#include <atomic>
#include <glm/glm.hpp>

class AnimationModuleProcessor : public ModuleProcessor
{
public:
    AnimationModuleProcessor();
    ~AnimationModuleProcessor() override;

    // --- Main JUCE Functions ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    const juce::String getName() const override { return "Animation Node"; }

    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }

    // --- Force this node to always be processed ---
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    
    // Tell the UI about our output pins
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
#endif

    // --- Custom Functions ---
    void loadFile(const juce::File& file); // Supports both .gltf/.glb and .fbx
    const std::vector<glm::mat4>& getFinalBoneMatrices() const;

private:
    // Parameter state (empty for this module, but required by ModuleProcessor)
    juce::AudioProcessorValueTreeState apvts;

    // Our animation system!
    std::unique_ptr<AnimationData> m_AnimationData;
    std::unique_ptr<Animator> m_Animator;
    std::unique_ptr<AnimationRenderer> m_Renderer;

    // A mutex to protect the bone matrices when accessed from the UI thread
    juce::CriticalSection m_AnimatorLock;

    // File chooser (kept alive during async operation)
    std::unique_ptr<juce::FileChooser> m_FileChooser;

    // Zoom and pan for the animation viewport
    float m_zoom = 10.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;

    // Bone selection for parameter mapping
    int m_selectedBoneIndex = -1;
    std::string m_selectedBoneName = "None";

    // State for UI thread kinematic calculations
    glm::vec2 m_lastScreenPos { 0.0f, 0.0f };
    bool m_isFirstFrame = true;

    // Atomics for thread-safe data transfer to audio thread
    std::atomic<float> m_outputPosX { 0.0f };
    std::atomic<float> m_outputPosY { 0.0f };
    std::atomic<float> m_outputVelX { 0.0f };
    std::atomic<float> m_outputVelY { 0.0f };

    // Ground trigger system
    float m_groundY = 180.0f;
    bool m_wasBoneBelowGround = false;
    std::atomic<bool> m_triggerState { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationModuleProcessor)
};

