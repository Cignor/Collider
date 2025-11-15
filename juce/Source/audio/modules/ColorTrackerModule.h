#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#if WITH_CUDA_SUPPORT
    #include <opencv2/core/cuda.hpp>
    #include <opencv2/cudaimgproc.hpp>
#endif
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#if defined(PRESET_CREATOR_UI)
#include <juce_gui_basics/juce_gui_basics.h>
#endif

// A struct to hold the state and results for a single tracked color.
struct TrackedColor
{
    juce::String name;
    juce::Colour displayColour; // representative color for UI swatch
    cv::Scalar hsvLower { 0, 100, 100 };
    cv::Scalar hsvUpper { 10, 255, 255 };
    float tolerance { 1.0f }; // 1.0 = default window; <1 shrink, >1 expand
};

// x, y, area for each color, plus zone hits
struct ColorResultEntry
{
    float x = 0.5f;
    float y = 0.5f;
    float area = 0.0f;
    bool zoneHits[4] = {false, false, false, false};  // Zone hit detection results
};
using ColorResult = std::vector<ColorResultEntry>;

class ColorTrackerModule : public ModuleProcessor, private juce::Thread
{
public:
    ColorTrackerModule();
    ~ColorTrackerModule() override;

    const juce::String getName() const override { return "color_tracker"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    juce::Image getLatestFrame();

    // UI integration helpers
    void addColorAt(int x, int y);
    void autoTrackColors(); // Auto-detect and track 12 dominant colors
    bool isPickerActive() const { return isColorPickerActive.load(); }
    void exitPickerMode() { isColorPickerActive.store(false); }

    // Dynamic outputs: 3 per color
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

    // Persist tracked colors across sessions
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

#if defined(PRESET_CREATOR_UI)
    // Auto-connect trigger flags (similar to MultiSequencerModuleProcessor)
    std::atomic<bool> autoConnectPolyVCOTriggered { false };
    std::atomic<bool> autoConnectSamplersTriggered { false };
    
    // Helper for auto-connect
    int getTrackedColorsCount() const
    {
        const juce::ScopedLock lock(colorListLock);
        return (int)trackedColors.size();
    }
    
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    ImVec2 getCustomNodeSize() const override;
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Zone rectangles structure: each color zone can have multiple rectangles
    struct ZoneRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };
    
    // Helper functions to serialize/deserialize zone rectangles
    static juce::String serializeZoneRects(const std::vector<ZoneRect>& rects);
    static std::vector<ZoneRect> deserializeZoneRects(const juce::String& data);
    void loadZoneRects(int colorIndex, std::vector<ZoneRect>& rects) const;
    void saveZoneRects(int colorIndex, const std::vector<ZoneRect>& rects);
    juce::AudioProcessorValueTreeState apvts;
    
    std::atomic<float>* sourceIdParam = nullptr;
    std::atomic<float>* zoomLevelParam = nullptr; // 0=Small,1=Normal,2=Large
    juce::AudioParameterBool* useGpuParam = nullptr;
    juce::AudioParameterInt* numAutoColorsParam = nullptr;
    
    // Thread-safe color list
    std::vector<TrackedColor> trackedColors;
    mutable juce::CriticalSection colorListLock;
    
    // Source ID (set by audio thread)
    std::atomic<juce::uint32> currentSourceId { 0 };
    
    // FIFO for communication
    ColorResult lastResultForAudio;
    juce::AbstractFifo fifo { 16 };
    std::vector<ColorResult> fifoBuffer;
    
    // UI interaction
    std::atomic<bool> isColorPickerActive { false };
    // -1 means add a new color; 0+ means update that tracked color index
    std::atomic<int> pickerTargetIndex { -1 };
    std::atomic<int> pickerMouseX { -1 }, pickerMouseY { -1 };
    std::atomic<bool> addColorRequested { false };

    // UI preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;

    // Cached last BGR frame for operations while source is paused/no new frames
    cv::Mat lastFrameBgr;
    juce::CriticalSection frameLock;
};


