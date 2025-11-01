#include "SemanticSegmentationModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <fstream>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

static constexpr int ENET_W = 1024;
static constexpr int ENET_H = 512;

juce::AudioProcessorValueTreeState::ParameterLayout SemanticSegmentationModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("targetClass", "Target Class", juce::StringArray{ "person" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true;
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", defaultGpu));
    return { params.begin(), params.end() };
}

SemanticSegmentationModule::SemanticSegmentationModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(4), true)
                      .withOutput("Video Out", juce::AudioChannelSet::mono(), true)), // PASSTHROUGH
      juce::Thread("Semantic Segmentation Thread"),
      apvts(*this, nullptr, "SemanticSegmentationParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    targetClassParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("targetClass"));
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    fifoBuffer.resize(16);
    loadModel();
}

SemanticSegmentationModule::~SemanticSegmentationModule()
{
    stopThread(5000);
}

void SemanticSegmentationModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void SemanticSegmentationModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void SemanticSegmentationModule::loadModel()
{
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir = exeFile.getParentDirectory();
    juce::File assetsDir = appDir.getChildFile("assets");

    // Try ENet Cityscapes first, then DeepLabV3 fallback
    juce::File enetOnnx  = assetsDir.getChildFile("enet-cityscapes-pytorch.onnx");
    juce::File enetNames = assetsDir.getChildFile("enet-classes.txt");
    juce::File dlOnnx    = assetsDir.getChildFile("deeplabv3.onnx");
    juce::File dlNames   = assetsDir.getChildFile("deeplabv3-classes.txt");

    juce::File chosenOnnx;
    juce::File chosenNames;

    if (enetOnnx.existsAsFile()) { chosenOnnx = enetOnnx; chosenNames = enetNames; }
    else if (dlOnnx.existsAsFile()) { chosenOnnx = dlOnnx; chosenNames = dlNames; }

    if (! chosenOnnx.existsAsFile())
    {
        juce::Logger::writeToLog("[Segmentation] No ONNX model found in assets (expected enet-cityscapes-pytorch.onnx or deeplabv3.onnx)");
        modelLoaded = false;
        return;
    }

    try
    {
        net = cv::dnn::readNet(chosenOnnx.getFullPathName().toStdString());
        
        // CRITICAL: Set backend immediately after loading model
        #if WITH_CUDA_SUPPORT
            bool useGpu = useGpuParam ? useGpuParam->get() : false;
            if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
            {
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                juce::Logger::writeToLog("[SemanticSegmentation] ✓ Model loaded with CUDA backend (GPU)");
            }
            else
            {
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[SemanticSegmentation] Model loaded with CPU backend");
            }
        #else
            net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
            net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
            juce::Logger::writeToLog("[SemanticSegmentation] Model loaded with CPU backend (CUDA not compiled)");
        #endif

        classNames.clear();
        if (chosenNames.existsAsFile())
        {
            std::ifstream ifs(chosenNames.getFullPathName().toStdString().c_str());
            std::string line;
            while (std::getline(ifs, line)) if (!line.empty()) classNames.push_back(line);
        }
        else
        {
            for (int i = 0; i < 256; ++i) classNames.push_back("class_" + std::to_string(i));
        }

        classColors.resize(classNames.size());
        for (size_t i = 0; i < classColors.size(); ++i)
            classColors[i] = cv::Vec3b((uchar)(i*53%255), (uchar)(i*97%255), (uchar)(i*193%255));
        modelLoaded = true;
        juce::Logger::writeToLog("[Segmentation] Loaded ONNX: " + chosenOnnx.getFileName());
    }
    catch (const cv::Exception& e)
    {
        juce::Logger::writeToLog("[Segmentation] OpenCV exception: " + juce::String(e.what()));
        modelLoaded = false;
    }
}

void SemanticSegmentationModule::run()
{
    #if WITH_CUDA_SUPPORT
        bool lastGpuState = false; // Track GPU state to minimize backend switches
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(currentSourceId.load());
        if (!frame.empty())
        {
            bool useGpu = false;
            
            #if WITH_CUDA_SUPPORT
                // Check if user wants GPU and if CUDA device is available
                useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
                {
                    useGpu = false; // Fallback to CPU
                    if (!loggedGpuWarning)
                    {
                        juce::Logger::writeToLog("[SemanticSegmentation] WARNING: GPU requested but no CUDA device found. Using CPU.");
                        loggedGpuWarning = true;
                    }
                }
                
                // Set DNN backend only when state changes (expensive operation)
                if (useGpu != lastGpuState)
                {
                    if (useGpu)
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                        juce::Logger::writeToLog("[SemanticSegmentation] ✓ Switched to CUDA backend (GPU)");
                    }
                    else
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                        juce::Logger::writeToLog("[SemanticSegmentation] Switched to CPU backend");
                    }
                    lastGpuState = useGpu;
                }
            #endif
            
            if (modelLoaded)
            {
                // NOTE: For DNN models, blobFromImage works on CPU
                // The GPU acceleration happens in net.forward() when backend is set to CUDA
                cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0, cv::Size(ENET_W, ENET_H), cv::Scalar(), true, false);
                net.setInput(blob);
                // Forward pass (GPU-accelerated if backend is CUDA)
                cv::Mat out = net.forward(); // shape: 1 x C x H x W

                if (out.dims == 4)
                {
                    int C = out.size[1];
                    int H = out.size[2];
                    int W = out.size[3];

                    // Argmax across channels per pixel
                    cv::Mat classId(H, W, CV_8S);
                    for (int y = 0; y < H; ++y)
                    {
                        for (int x = 0; x < W; ++x)
                        {
                            int bestClass = 0;
                            float bestScore = -1e9f;
                            for (int c = 0; c < C; ++c)
                            {
                                float score = out.ptr<float>(0, c, y)[x];
                                if (score > bestScore) { bestScore = score; bestClass = c; }
                            }
                            classId.at<signed char>(y, x) = (signed char)bestClass;
                        }
                    }

                    // Target mask
                    int target = targetClassParam ? targetClassParam->getIndex() : 0;
                    cv::Mat mask(H, W, CV_8U);
                    for (int y = 0; y < H; ++y)
                        for (int x = 0; x < W; ++x)
                            mask.at<unsigned char>(y, x) = (unsigned char)(classId.at<signed char>(y, x) == target ? 255 : 0);

                    SegmentationResult result;
                    int pix = cv::countNonZero(mask);
                    if (pix > 0)
                    {
                        result.detected = true;
                        result.area = (float)pix / (float)(H * W);
                        cv::Moments m = cv::moments(mask, true);
                        result.centerX = (float)(m.m10 / m.m00) / (float)W;
                        result.centerY = (float)(m.m01 / m.m00) / (float)H;
                    }

                    if (fifo.getFreeSpace() >= 1)
                    {
                        auto writeScope = fifo.write(1);
                        if (writeScope.blockSize1 > 0)
                            fifoBuffer[writeScope.startIndex1] = result;
                    }

                    // Colorize for preview
                    cv::Mat color(H, W, CV_8UC3);
                    for (int y = 0; y < H; ++y)
                    {
                        for (int x = 0; x < W; ++x)
                        {
                            int cid = (int)classId.at<signed char>(y, x);
                            cv::Vec3b col = (cid >= 0 && cid < (int)classColors.size()) ? classColors[(size_t)cid] : cv::Vec3b(0,0,0);
                            color.at<cv::Vec3b>(y, x) = col;
                        }
                    }
                    cv::resize(color, color, frame.size(), 0, 0, cv::INTER_NEAREST);
                    cv::addWeighted(frame, 1.0, color, 0.4, 0.0, frame);
                }
            }

            // --- PASSTHROUGH LOGIC ---
            VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
            // Always update preview with the latest frame (with or without overlay)
            updateGuiFrame(frame);
        }

        wait(100);
    }
}

void SemanticSegmentationModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra;
    cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgra.cols || latestFrameForGui.getHeight() != bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgra.data, bgra.total() * bgra.elemSize());
}

juce::Image SemanticSegmentationModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

std::vector<DynamicPinInfo> SemanticSegmentationModule::getDynamicOutputPins() const
{
    // Bus 0: CV Out (4 channels)
    // Bus 1: Video Out (1 channel)
    const int cvOutChannels = 4;
    const int videoOutStartChannel = cvOutChannels;

    return {
        { "Area", 0, PinDataType::CV },
        { "Center X", 1, PinDataType::CV },
        { "Center Y", 2, PinDataType::CV },
        { "Gate", 3, PinDataType::Gate },
        { "Video Out", videoOutStartChannel, PinDataType::Video }
    };
}

void SemanticSegmentationModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
        currentSourceId.store((juce::uint32)inputBuffer.getSample(0, 0));

    buffer.clear();
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
    }

    // Output CV on bus 0
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    const float values[4] { lastResultForAudio.area, lastResultForAudio.centerX, lastResultForAudio.centerY, lastResultForAudio.detected ? 1.0f : 0.0f };
    for (int ch = 0; ch < juce::jmin(4, cvOutBus.getNumChannels()); ++ch)
        for (int s = 0; s < cvOutBus.getNumSamples(); ++s)
            cvOutBus.setSample(ch, s, values[ch]);
    
    // Passthrough Video ID on bus 1
    auto videoOutBus = getBusBuffer(buffer, false, 1);
    if (videoOutBus.getNumChannels() > 0)
    {
        float primaryId = static_cast<float>(getLogicalId());
        for (int s = 0; s < videoOutBus.getNumSamples(); ++s)
            videoOutBus.setSample(0, s, primaryId);
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 SemanticSegmentationModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void SemanticSegmentationModule::drawParametersInNode(float itemWidth,
                                                      const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                      const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated);
    ImGui::PushItemWidth(itemWidth);
    
    // GPU ACCELERATION TOGGLE
    #if WITH_CUDA_SUPPORT
        bool cudaAvailable = (cv::cuda::getCudaEnabledDeviceCount() > 0);
        
        if (!cudaAvailable)
        {
            ImGui::BeginDisabled();
        }
        
        bool useGpu = useGpuParam->get();
        if (ImGui::Checkbox("⚡ Use GPU (CUDA)", &useGpu))
        {
            *useGpuParam = useGpu;
            onModificationEnded();
        }
        
        if (!cudaAvailable)
        {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("No CUDA-enabled GPU detected.\nCheck that your GPU supports CUDA and drivers are installed.");
            }
        }
        else if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Enable GPU acceleration for semantic segmentation.\nRequires CUDA-capable NVIDIA GPU.");
        }
        
        ImGui::Separator();
    #else
        ImGui::TextDisabled("🚫 GPU support not compiled");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("OpenCV was built without CUDA support.\nRebuild with WITH_CUDA=ON to enable GPU acceleration.");
        }
        ImGui::Separator();
    #endif

    // Target class dropdown (populated from classNames if available)
    if (targetClassParam)
    {
        int currentIndex = targetClassParam->getIndex();
        const char* preview = (!classNames.empty() && juce::isPositiveAndBelow(currentIndex, (int)classNames.size()))
                                ? classNames[(size_t)currentIndex].c_str()
                                : "person";

        if (ImGui::BeginCombo("Target Class", preview))
        {
            if (classNames.empty())
            {
                const bool isSelected = (currentIndex == 0);
                if (ImGui::Selectable("person", isSelected))
                {
                    *targetClassParam = 0;
                    onModificationEnded();
                }
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            else
            {
                for (int i = 0; i < (int)classNames.size(); ++i)
                {
                    const bool isSelected = (currentIndex == i);
                    if (ImGui::Selectable(classNames[(size_t)i].c_str(), isSelected))
                    {
                        *targetClassParam = i;
                        onModificationEnded();
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    // Zoom (-/+) controls, consistent with PoseEstimatorModule
    ImGui::Separator();
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    float buttonWidth = (itemWidth / 2.0f) - 4.0f;
    const bool atMin = (level <= 0);
    const bool atMax = (level >= 2);
    if (atMin) ImGui::BeginDisabled();
    if (ImGui::Button("-", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmax(0, level - 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMin) ImGui::EndDisabled();
    ImGui::SameLine();
    if (atMax) ImGui::BeginDisabled();
    if (ImGui::Button("+", ImVec2(buttonWidth, 0)))
    {
        int newLevel = juce::jmin(2, level + 1);
        if (auto* p = apvts.getParameter("zoomLevel"))
            p->setValueNotifyingHost((float)newLevel / 2.0f);
        onModificationEnded();
    }
    if (atMax) ImGui::EndDisabled();

    ImGui::PopItemWidth();
}

void SemanticSegmentationModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Area", 0);
    helpers.drawAudioOutputPin("Center X", 1);
    helpers.drawAudioOutputPin("Center Y", 2);
    helpers.drawAudioOutputPin("Gate", 3);
    helpers.drawAudioOutputPin("Video Out", 0); // Bus 1
}
#endif


