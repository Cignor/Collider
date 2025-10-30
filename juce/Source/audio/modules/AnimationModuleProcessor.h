#pragma once

#include "ModuleProcessor.h"
#include "../../animation/AnimationFileLoader.h"
#include "../../animation/AnimationBinder.h"
#include "../../animation/Animator.h"
#include "../../animation/AnimationRenderer.h"
#include <memory>
#include <atomic>
#include <algorithm>
#include <glm/glm.hpp>

// Inherit from juce::ChangeListener to receive notifications from the background loader
class AnimationModuleProcessor : public ModuleProcessor,
                                 public juce::ChangeListener
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
    
    // Opens a file chooser and loads the selected animation file in the background
    void openAnimationFile();
    
    // Check if an animation file is currently being loaded in the background
    bool isCurrentlyLoading() const;
    
    // Callback executed on the main thread when background loading completes
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    
    const std::vector<glm::mat4>& getFinalBoneMatrices() const;
    
    // Dynamic bone tracking
    void addTrackedBone(const std::string& boneName);
    void removeTrackedBone(const std::string& boneName);
    
    // Ground Plane data structure with depth
    struct GroundPlane {
        float y = 0.0f;
        float depth = 0.0f;
    };
    void addGroundPlane(float initialY = 0.0f, float initialDepth = 0.0f);
    void removeGroundPlane(int index = -1);
    std::vector<GroundPlane> getGroundPlanes() const;

    // --- State Management (for saving/loading presets) ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

#if defined(PRESET_CREATOR_UI)
    // Auto-connection feature flags
    std::atomic<bool> autoBuildTriggersAudioTriggered { false };
#endif

private:
    static constexpr int MAX_TRACKED_BONES = 10; // Maximum number of bones we can track simultaneously
    
    // Update bone IDs for all tracked bones from the currently loaded animation
    void updateTrackedBoneIDs();
    
    // Helper structure for tracking multiple bones
    struct TrackedBone
    {
        std::string name;
        int boneId = -1;

        // UI-thread state for kinematics
        glm::vec2 lastScreenPos { 0.0f, 0.0f };
        bool isFirstFrame = true;
        bool wasBelowGround = false; // Legacy screen-space flag
        bool wasBelowWorldGround = false; // Legacy single-plane flag
        std::vector<bool> wasAboveCenter; // NEW: Tracks if bone was above the plane's center line last frame
        float previousScreenY = 0.0f; // Kept for compatibility
        float hitFlashTimer = 0.0f; // Countdown timer for red flash visual feedback

        // Atomics for audio thread
        std::atomic<float> velX { 0.0f };
        std::atomic<float> velY { 0.0f };
        std::atomic<bool> triggerState { false };
        
        // Copy constructor (atomics can't be copied, so load/store their values)
        TrackedBone(const TrackedBone& other)
            : name(other.name), boneId(other.boneId),
              lastScreenPos(other.lastScreenPos), isFirstFrame(other.isFirstFrame),
              wasBelowGround(other.wasBelowGround), wasBelowWorldGround(other.wasBelowWorldGround),
              wasAboveCenter(other.wasAboveCenter), previousScreenY(other.previousScreenY),
              hitFlashTimer(other.hitFlashTimer),
              velX(other.velX.load()), velY(other.velY.load()),
              triggerState(other.triggerState.load())
        {}
        
        // Copy assignment operator
        TrackedBone& operator=(const TrackedBone& other)
        {
            if (this != &other)
            {
                name = other.name;
                boneId = other.boneId;
                lastScreenPos = other.lastScreenPos;
                isFirstFrame = other.isFirstFrame;
                wasBelowGround = other.wasBelowGround;
                wasBelowWorldGround = other.wasBelowWorldGround;
                wasAboveCenter = other.wasAboveCenter;
                previousScreenY = other.previousScreenY;
                hitFlashTimer = other.hitFlashTimer;
                velX.store(other.velX.load());
                velY.store(other.velY.load());
                triggerState.store(other.triggerState.load());
            }
            return *this;
        }
        
        // Default constructor
        TrackedBone() = default;
    };

    // Called after raw data is loaded to bind and set up the animation
    void setupAnimationFromRawData(std::unique_ptr<RawAnimationData> rawData);
    
    // Parameter state (empty for this module, but required by ModuleProcessor)
    juce::AudioProcessorValueTreeState apvts;

    // Background animation file loader
    AnimationFileLoader m_fileLoader;

    // --- Thread-Safe Animation Data Management ---
    
    // The audio thread reads from this atomic pointer (lock-free).
    // It points to the currently active Animator that's being used for audio processing.
    std::atomic<Animator*> m_activeAnimator { nullptr };
    
    // This owns the AnimationData for the currently active animator.
    // We must keep this alive as long as the active animator might be in use.
    std::unique_ptr<AnimationData> m_activeData;
    
    // When new data is loaded, it's prepared here first, away from the audio thread.
    std::unique_ptr<AnimationData> m_stagedAnimationData;
    std::unique_ptr<Animator> m_stagedAnimator;
    
    // Old animators/data that need to be deleted safely after the audio thread is done with them.
    // We can't delete immediately after swapping because the audio thread might still be using it.
    std::vector<std::unique_ptr<Animator>> m_animatorsToFree;
    std::vector<std::unique_ptr<AnimationData>> m_dataToFree;
    juce::CriticalSection m_freeingLock; // Protects the above arrays
    
    // Tracked bones (dynamic list) for dedicated outputs - preserves insertion order
    std::vector<TrackedBone> m_trackedBones;
    juce::CriticalSection m_trackedBonesLock; // Protects m_trackedBones from concurrent access
    
    // Dynamic ground planes for multi-level trigger detection
    std::vector<GroundPlane> m_groundPlanes;
    mutable juce::CriticalSection m_groundPlanesLock; // Protects m_groundPlanes from concurrent access
    
    // Rendering
    std::unique_ptr<AnimationRenderer> m_Renderer;
    
    // Per-frame bone colors for rendering (green=tracked, red=hit flash, white=default)
    std::vector<glm::vec3> m_boneColors;

    // NEW: Per-frame vertex pairs for bone edges
    std::vector<glm::vec3> m_boneEdges;

    // File chooser (kept alive during async operation)
    std::unique_ptr<juce::FileChooser> m_FileChooser;

    // Zoom and pan for the animation viewport
    float m_zoom = 10.0f;
    float m_panX = 0.0f;
    float m_panY = 0.0f;

    // View rotation angles (in radians)
    float m_viewRotationX = 0.0f;
    float m_viewRotationY = 0.0f;
    float m_viewRotationZ = 0.0f;

    // Ground line Y position for trigger detection
    float m_groundY = 180.0f;
    
    // UI bone selection (for visualization, not directly tied to outputs anymore)
    int m_selectedBoneIndex = -1;
    std::string m_selectedBoneName = "None";
    int m_selectedBoneID = -1; // Cached bone ID to avoid map lookups
    std::vector<std::string> m_cachedBoneNames; // Thread-safe cache of bone names for UI

    // Name of the animation clip to play when a preset is loaded
    juce::String m_clipToPlayOnLoad;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationModuleProcessor)
};

