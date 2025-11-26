#pragma once

#include "ModuleProcessor.h"
#include <opencv2/core.hpp>
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#if defined(WITH_CUDA_SUPPORT)
    #include <opencv2/core/cuda.hpp>
#endif

/**
 * A "Swiss Army knife" video processing node.
 * Takes a source ID as input, applies a chain of effects, and outputs a new
 * source ID for the processed video stream, allowing for effect chaining.
 */
class VideoFXModule : public ModuleProcessor, private juce::Thread
{
public:
    VideoFXModule();
    ~VideoFXModule() override;

    const juce::String getName() const override { return "video_fx"; }
    
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    juce::Image getLatestFrame();
    
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;
    
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;

#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth,
                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                              const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    ImVec2 getCustomNodeSize() const override;
#endif

private:
    void run() override;
    void updateGuiFrame(const cv::Mat& frame);
    
    // --- PRIVATE HELPER FUNCTIONS ---
    void applyBrightnessContrast(cv::Mat& ioFrame, float brightness, float contrast);
    void applyTemperature(cv::Mat& ioFrame, float temperature);
    void applySepia(cv::Mat& ioFrame, bool sepia);
    void applySaturationHue(cv::Mat& ioFrame, float saturation, float hueShift);
    void applyRgbGain(cv::Mat& ioFrame, float gainR, float gainG, float gainB);
    void applyPosterize(cv::Mat& ioFrame, int levels);
    void applyGrayscale(cv::Mat& ioFrame, bool grayscale);
    void applyCanny(cv::Mat& ioFrame, float thresh1, float thresh2);
    void applyThreshold(cv::Mat& ioFrame, float level);
    void applyInvert(cv::Mat& ioFrame, bool invert);
    void applyFlip(cv::Mat& ioFrame, bool flipH, bool flipV);
    void applyVignette(cv::Mat& ioFrame, float amount, float size);
    void applyPixelate(cv::Mat& ioFrame, int pixelSize);
    void applyBlur(cv::Mat& ioFrame, float blur);
    void applySharpen(cv::Mat& ioFrame, float sharpen);
    void applyKaleidoscope(cv::Mat& ioFrame, int mode);

#if defined(WITH_CUDA_SUPPORT)
    // --- Reusable GPU Buffers ---
    cv::cuda::GpuMat gpuTemp;       // 8-bit, 3-channel
    cv::cuda::GpuMat gpuGray;       // 8-bit, 1-channel
    std::vector<cv::cuda::GpuMat> gpuChannels; // 8-bit, 1-channel (x3)
    
    // --- NEW BUFFERS FOR PHASE 2C ---
    
    // Buffers for Sharpen (16-bit signed)
    cv::cuda::GpuMat gpuTemp16S;
    cv::cuda::GpuMat gpuBlurred16S;
    
    // Buffers for Sat/Hue (32-bit float)
    cv::cuda::GpuMat gpuTempF1;
    cv::cuda::GpuMat gpuTempF2;
    cv::cuda::GpuMat gpuMask;       // Mask for comparisons
    
    // Buffers for Kaleidoscope (8-bit, 3-channel)
    cv::cuda::GpuMat gpuQuadrant;
    cv::cuda::GpuMat gpuFlipH;
    cv::cuda::GpuMat gpuFlipV;
    cv::cuda::GpuMat gpuFlipHV;
    
    // Buffers for Vignette (caching)
    cv::Mat cpuVignetteMask;        // 32-bit, 1-channel (CPU cache)
    cv::cuda::GpuMat gpuVignetteMask; // 32-bit, 1-channel (GPU cache)
    int lastVignetteW = 0, lastVignetteH = 0;
    float lastVignetteAmount = -1.f, lastVignetteSize = -1.f;
    
    // --- End of new buffers ---

    void applyBrightnessContrast_gpu(cv::cuda::GpuMat& ioFrame, float brightness, float contrast);
    void applyTemperature_gpu(cv::cuda::GpuMat& ioFrame, float temperature);
    void applySepia_gpu(cv::cuda::GpuMat& ioFrame, bool sepia);
    void applySaturationHue_gpu(cv::cuda::GpuMat& ioFrame, float saturation, float hueShift);
    void applyRgbGain_gpu(cv::cuda::GpuMat& ioFrame, float gainR, float gainG, float gainB);
    void applyPosterize_gpu(cv::cuda::GpuMat& ioFrame, int levels);
    void applyGrayscale_gpu(cv::cuda::GpuMat& ioFrame, bool grayscale);
    void applyCanny_gpu(cv::cuda::GpuMat& ioFrame, float thresh1, float thresh2);
    void applyThreshold_gpu(cv::cuda::GpuMat& ioFrame, float level);
    void applyInvert_gpu(cv::cuda::GpuMat& ioFrame, bool invert);
    void applyFlip_gpu(cv::cuda::GpuMat& ioFrame, bool flipH, bool flipV);
    void applyVignette_gpu(cv::cuda::GpuMat& ioFrame, float amount, float size);
    void applyPixelate_gpu(cv::cuda::GpuMat& ioFrame, int pixelSize);
    void applyBlur_gpu(cv::cuda::GpuMat& ioFrame, float blur);
    void applySharpen_gpu(cv::cuda::GpuMat& ioFrame, float sharpen);
    void applyKaleidoscope_gpu(cv::cuda::GpuMat& ioFrame, int mode);
#endif
    
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;
    
    // Parameters
    std::atomic<float>* zoomLevelParam = nullptr;
    juce::AudioParameterBool* useGpuParam = nullptr;
    
    // Color Adjustments
    std::atomic<float>* brightnessParam = nullptr; // -100 to 100
    std::atomic<float>* contrastParam = nullptr;   // 0.0 to 3.0
    std::atomic<float>* saturationParam = nullptr; // 0.0 to 3.0
    std::atomic<float>* hueShiftParam = nullptr;   // -180 to 180 degrees
    std::atomic<float>* gainRedParam = nullptr;
    std::atomic<float>* gainGreenParam = nullptr;
    std::atomic<float>* gainBlueParam = nullptr;
    juce::AudioParameterBool* sepiaParam = nullptr;
    std::atomic<float>* temperatureParam = nullptr; // -1.0 to 1.0

    // Filters & Effects
    std::atomic<float>* sharpenParam = nullptr;    // 0.0 to 2.0
    std::atomic<float>* blurParam = nullptr;       // 0 to 20
    juce::AudioParameterBool* grayscaleParam = nullptr;
    juce::AudioParameterBool* invertParam = nullptr;
    juce::AudioParameterBool* flipHorizontalParam = nullptr;
    juce::AudioParameterBool* flipVerticalParam = nullptr;
    
    // Threshold Effect
    juce::AudioParameterBool* thresholdEnableParam = nullptr;
    std::atomic<float>* thresholdLevelParam = nullptr; // 0 to 255
    
    // New Effects
    juce::AudioParameterInt* posterizeLevelsParam = nullptr; // 2 to 16
    std::atomic<float>* vignetteAmountParam = nullptr; // 0.0 to 1.0
    std::atomic<float>* vignetteSizeParam = nullptr; // 0.1 to 2.0
    juce::AudioParameterInt* pixelateBlockSizeParam = nullptr; // 2 to 64
    juce::AudioParameterBool* cannyEnableParam = nullptr;
    std::atomic<float>* cannyThresh1Param = nullptr; // 0 to 255
    std::atomic<float>* cannyThresh2Param = nullptr; // 0 to 255
    juce::AudioParameterChoice* kaleidoscopeModeParam = nullptr; // None, 4-Way, 8-Way

    // Source ID (read from input pin)
    std::atomic<juce::uint32> currentSourceId { 0 };
    juce::uint32 cachedResolvedSourceId { 0 };
    
    // UI Preview
    juce::Image latestFrameForGui;
    juce::CriticalSection imageLock;

    cv::Mat lastFrameBgr;
    juce::CriticalSection frameLock;

    juce::uint32 storedLogicalId { 0 };
};

