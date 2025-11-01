#include "PoseEstimatorModule.h"
#include "../../video/VideoFrameManager.h"
#include "../graph/ModularSynthProcessor.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#include "../../preset_creator/ImGuiNodeEditorComponent.h"
#endif

void PoseEstimatorModule::loadModel(int modelIndex)
{
    // --- THIS IS THE CORRECTED PATH LOGIC ---
    // 1. Get the directory containing the running executable.
    auto exeFile = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
    auto appDir = exeFile.getParentDirectory();
    juce::Logger::writeToLog("[PoseEstimator] Executable directory: " + appDir.getFullPathName());

    // 2. Look for the 'assets' folder next to the executable (fixed path as requested).
    juce::File assetsDir = appDir.getChildFile("assets");
    juce::Logger::writeToLog("[PoseEstimator] Searching for assets in: " + assetsDir.getFullPathName());
    
    // 3. Navigate to the specific model subdirectory.
    auto poseModelsDir = assetsDir.getChildFile("openpose_models").getChildFile("pose");

    juce::String protoPath, modelPath;
    juce::String modelName;

    switch (modelIndex)
    {
        case 0: // BODY_25
            modelName = "BODY_25";
            protoPath = poseModelsDir.getChildFile("body_25/pose_deploy.prototxt").getFullPathName();
            modelPath = poseModelsDir.getChildFile("body_25/pose_iter_584000.caffemodel").getFullPathName();
            break;
        case 1: // COCO
            modelName = "COCO";
            protoPath = poseModelsDir.getChildFile("coco/pose_deploy_linevec.prototxt").getFullPathName();
            modelPath = poseModelsDir.getChildFile("coco/pose_iter_440000.caffemodel").getFullPathName();
            break;
        case 2: // MPI
            modelName = "MPI";
            protoPath = poseModelsDir.getChildFile("mpi/pose_deploy_linevec.prototxt").getFullPathName();
            modelPath = poseModelsDir.getChildFile("mpi/pose_iter_160000.caffemodel").getFullPathName();
            break;
        case 3: // MPI (Fast)
        default:
            modelName = "MPI (Fast)";
            protoPath = poseModelsDir.getChildFile("mpi/pose_deploy_linevec_faster_4_stages.prototxt").getFullPathName();
            modelPath = poseModelsDir.getChildFile("mpi/pose_iter_160000.caffemodel").getFullPathName();
            break;
    }

    juce::Logger::writeToLog("[PoseEstimator] Attempting to load " + modelName + " model...");
    juce::Logger::writeToLog("  - Prototxt: " + protoPath);
    juce::Logger::writeToLog("  - Caffemodel: " + modelPath);
    
    juce::File protoFile(protoPath);
    juce::File modelFile(modelPath);

    if (protoFile.existsAsFile() && modelFile.existsAsFile()) {
        try {
            net = cv::dnn::readNetFromCaffe(protoPath.toStdString(), modelPath.toStdString());
            
            // CRITICAL: Set backend immediately after loading model
            #if WITH_CUDA_SUPPORT
                bool useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                    juce::Logger::writeToLog("[PoseEstimator] ✓ Model loaded with CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[PoseEstimator] Model loaded with CPU backend");
                }
            #else
                net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                juce::Logger::writeToLog("[PoseEstimator] Model loaded with CPU backend (CUDA not compiled)");
            #endif
            
            modelLoaded = true;
            juce::Logger::writeToLog("[PoseEstimator] SUCCESS: Loaded model: " + modelName);
        } catch (const cv::Exception& e) {
            juce::Logger::writeToLog("[PoseEstimator] FAILED: OpenCV exception while loading model: " + juce::String(e.what()));
            modelLoaded = false;
        }
    } else {
        juce::Logger::writeToLog("[PoseEstimator] FAILED: Could not find model files at the specified paths.");
        if (!protoFile.existsAsFile()) juce::Logger::writeToLog("  - Missing file: " + protoPath);
        if (!modelFile.existsAsFile()) juce::Logger::writeToLog("  - Missing file: " + modelPath);
        modelLoaded = false;
    }
}

juce::ValueTree PoseEstimatorModule::getExtraStateTree() const
{
    juce::ValueTree state("PoseEstimatorState");
    state.setProperty("assetsPath", assetsPath, nullptr);
    return state;
}

void PoseEstimatorModule::setExtraStateTree(const juce::ValueTree& state)
{
    if (state.hasType("PoseEstimatorState"))
    {
        assetsPath = state.getProperty("assetsPath", "").toString();
        if (assetsPath.isNotEmpty())
        {
            requestedModelIndex = modelChoiceParam ? modelChoiceParam->getIndex() : 3;
        }
    }
}

// Network input size for MPI model
constexpr int POSE_NET_WIDTH = 368;
constexpr int POSE_NET_HEIGHT = 368;

juce::AudioProcessorValueTreeState::ParameterLayout PoseEstimatorModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Source ID input (which video source to connect to)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    
    // Model choice (BODY_25, COCO, MPI, MPI Fast)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "model", "Model", juce::StringArray{ "BODY_25", "COCO", "MPI", "MPI (Fast)" }, 3));

    // Model quality (affects blob size)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "quality", "Quality", juce::StringArray{ "Low (Fast)", "Medium (Default)" }, 1));
    
    // Note: assets path is stored via extra state, not as a parameter
    
    // Zoom level for UI: 0=Small(240),1=Normal(480),2=Large(960)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    
    // Confidence threshold (0.0 - 1.0) - keypoints below this confidence will be ignored
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "confidence", "Confidence", 0.0f, 1.0f, 0.1f));
    
    // Toggle skeleton drawing on preview
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "drawSkeleton", "Draw Skeleton", true));
    
    // GPU acceleration toggle - default from global setting
    #if defined(PRESET_CREATOR_UI)
        bool defaultGpu = ImGuiNodeEditorComponent::getGlobalGpuEnabled();
    #else
        bool defaultGpu = true; // Default to GPU for non-UI builds
    #endif
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "useGpu", "Use GPU (CUDA)", defaultGpu));
    
    return { params.begin(), params.end() };
}

PoseEstimatorModule::PoseEstimatorModule()
    : ModuleProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::mono(), true)
                     .withOutput("CV Out", juce::AudioChannelSet::discreteChannels(30), true) // 15 keypoints x 2 (x,y)
                     .withOutput("Video Out", juce::AudioChannelSet::mono(), true)), // PASSTHROUGH
      juce::Thread("Pose Estimator Thread"),
      apvts(*this, nullptr, "PoseEstimatorParams", createParameterLayout())
{
    // Get parameter pointers
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    qualityParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("quality"));
    modelChoiceParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("model"));
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    drawSkeletonParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("drawSkeleton"));
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    
    // Initialize FIFO buffer
    fifoBuffer.resize(16);
    
    // Defer initial model load to the thread (default to current selection or MPI Fast)
    requestedModelIndex = modelChoiceParam ? modelChoiceParam->getIndex() : 3;
}

PoseEstimatorModule::~PoseEstimatorModule()
{
    stopThread(5000);
}

void PoseEstimatorModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Always start the processing thread; it handles model loading on demand.
    startThread(juce::Thread::Priority::normal);
}

void PoseEstimatorModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void PoseEstimatorModule::run()
{
    juce::Logger::writeToLog("[PoseEstimator] Processing thread started");
    
    #if WITH_CUDA_SUPPORT
        bool lastGpuState = false; // Track GPU state to minimize backend switches
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        // Handle deferred model reload requests from UI
        int toLoad = requestedModelIndex.exchange(-1);
        if (toLoad != -1)
        {
            loadModel(toLoad);
        }
        
        if (!modelLoaded)
        {
            wait(200);
            continue;
        }

        // Get the source ID from the input cable (set by processBlock from the audio thread)
        juce::uint32 sourceId = currentSourceId.load();
        
        // Fetch frame from the VideoFrameManager
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        
        if (!frame.empty())
        {
            #if WITH_CUDA_SUPPORT
                // Check if user wants GPU and if a CUDA device is available
                bool useGpu = useGpuParam ? useGpuParam->get() : false;
                if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
                {
                    useGpu = false; // Fallback to CPU
                    if (!loggedGpuWarning)
                    {
                        juce::Logger::writeToLog("[PoseEstimator] WARNING: GPU requested but no CUDA device found. Using CPU.");
                        loggedGpuWarning = true;
                    }
                }
                
                // Set DNN backend only when state changes (this is an expensive operation)
                if (useGpu != lastGpuState)
                {
                    if (useGpu)
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
                        juce::Logger::writeToLog("[PoseEstimator] ✓ Switched to CUDA backend (GPU)");
                    }
                    else
                    {
                        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                        juce::Logger::writeToLog("[PoseEstimator] Switched to CPU backend");
                    }
                    lastGpuState = useGpu;
                }
            #endif
            
            // --- SIMPLIFIED AND CORRECTED LOGIC ---
            // 1. Prepare image for the network. This happens on the CPU regardless of the backend.
            int q = qualityParam ? qualityParam->getIndex() : 1;
            cv::Size blobSize = (q == 0) ? cv::Size(224, 224) : cv::Size(368, 368);
            cv::Mat inputBlob = cv::dnn::blobFromImage(
                frame,
                1.0 / 255.0,
                blobSize,
                cv::Scalar(0, 0, 0),
                false,
                false);

            // 2. Set the input and run the forward pass.
            // This `forward()` call is where the GPU acceleration happens if the backend was set to CUDA.
            net.setInput(inputBlob);
            cv::Mat netOutput = net.forward();
            
            // --- END OF CORRECTION ---
            
            // 3. Parse the output to extract keypoint coordinates
            PoseResult result;
            parsePoseOutput(netOutput, frame.cols, frame.rows, result);
            
            // Apply confidence threshold
            result.isValid = (result.detectedPoints > 5); // Need at least 5 keypoints for valid pose
            
            // 4. Push result to the audio thread via lock-free FIFO (UPDATED API)
            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                {
                    fifoBuffer[writeScope.startIndex1] = result;
                }
            }
            
            // 5. Draw the skeleton on the frame for UI preview (if enabled)
            if (drawSkeletonParam->get())
            {
                // Draw skeleton lines
                for (const auto& pair : MPI_SKELETON_PAIRS)
                {
                    int idxA = pair.first;
                    int idxB = pair.second;
                    
                    if (result.keypoints[idxA][0] >= 0 && result.keypoints[idxB][0] >= 0)
                    {
                        cv::Point ptA((int)result.keypoints[idxA][0], (int)result.keypoints[idxA][1]);
                        cv::Point ptB((int)result.keypoints[idxB][0], (int)result.keypoints[idxB][1]);
                        cv::line(frame, ptA, ptB, cv::Scalar(0, 255, 0), 3);
                    }
                }
                
                // Draw keypoint circles
                for (int i = 0; i < MPI_NUM_KEYPOINTS; ++i)
                {
                    if (result.keypoints[i][0] >= 0)
                    {
                        cv::Point pt((int)result.keypoints[i][0], (int)result.keypoints[i][1]);
                        cv::circle(frame, pt, 5, cv::Scalar(0, 0, 255), -1);
                    }
                }
            }
            
            // --- PASSTHROUGH LOGIC ---
            VideoFrameManager::getInstance().setFrame(getLogicalId(), frame);
            // 6. Update the GUI preview frame
            updateGuiFrame(frame);
        }
        
        // Run at ~15 FPS (pose estimation is computationally expensive)
        wait(66);
    }
    
    juce::Logger::writeToLog("[PoseEstimator] Processing thread stopped");
}

void PoseEstimatorModule::parsePoseOutput(const cv::Mat& netOutput, int frameWidth, int frameHeight, PoseResult& result)
{
    // OpenPose output format: [1, num_keypoints, height, width]
    // Each heatmap represents the probability of a keypoint at each location
    
    int H = netOutput.size[2]; // Heatmap height
    int W = netOutput.size[3]; // Heatmap width
    
    result.detectedPoints = 0;
    float confidenceThreshold = confidenceThresholdParam->load();
    
    int numHeatmaps = netOutput.size[1];
    int count = juce::jmin(MPI_NUM_KEYPOINTS, numHeatmaps);
    for (int i = 0; i < count; ++i)
    {
        // CORRECTED: Create a Mat view from the 4D blob for OpenCV 4.x
        cv::Mat heatMap(H, W, CV_32F, (void*)netOutput.ptr<float>(0, i));
        
        // Find the location of maximum confidence
        double maxConfidence;
        cv::Point maxLoc;
        cv::minMaxLoc(heatMap, nullptr, &maxConfidence, nullptr, &maxLoc);
        
        if (maxConfidence > confidenceThreshold)
        {
            // Scale the heatmap coordinates back to the original frame size
            result.keypoints[i][0] = (float)maxLoc.x * frameWidth / W;
            result.keypoints[i][1] = (float)maxLoc.y * frameHeight / H;
            result.detectedPoints++;
        }
        else
        {
            // Mark as not detected
            result.keypoints[i][0] = -1.0f;
            result.keypoints[i][1] = -1.0f;
        }
    }
}

void PoseEstimatorModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    
    const juce::ScopedLock lock(imageLock);
    
    if (latestFrameForGui.isNull() || 
        latestFrameForGui.getWidth() != bgraFrame.cols || 
        latestFrameForGui.getHeight() != bgraFrame.rows)
    {
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    }
    
    juce::Image::BitmapData destData(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(destData.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image PoseEstimatorModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void PoseEstimatorModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    // Read Source ID from input pin (BEFORE clearing the buffer!)
    auto inputBuffer = getBusBuffer(buffer, true, 0);
    if (inputBuffer.getNumChannels() > 0 && inputBuffer.getNumSamples() > 0)
    {
        float sourceIdFloat = inputBuffer.getSample(0, 0);
        currentSourceId.store((juce::uint32)sourceIdFloat);
    }
    
    // Clear the buffer for output
    buffer.clear();
    
    // Check if there's new pose data from the processing thread (UPDATED FIFO API)
    if (fifo.getNumReady() > 0)
    {
        auto readScope = fifo.read(1);
        if (readScope.blockSize1 > 0)
        {
            lastResultForAudio = fifoBuffer[readScope.startIndex1];
        }
    }
    
    // Map keypoint coordinates to output channels (bus 0 - CV Out)
    auto cvOutBus = getBusBuffer(buffer, false, 0);
    // Channel layout: [Head X, Head Y, Neck X, Neck Y, R Shoulder X, R Shoulder Y, ...]
    for (int i = 0; i < MPI_NUM_KEYPOINTS; ++i)
    {
        int chX = i * 2;
        int chY = i * 2 + 1;
        
        if (chY < cvOutBus.getNumChannels())
        {
            // Normalize coordinates to 0-1 range based on typical video resolution (640x480 or similar)
            // If keypoint not detected (negative value), output 0
            float x_normalized = (lastResultForAudio.keypoints[i][0] >= 0) 
                ? juce::jlimit(0.0f, 1.0f, lastResultForAudio.keypoints[i][0] / 640.0f)
                : 0.0f;
            
            float y_normalized = (lastResultForAudio.keypoints[i][1] >= 0) 
                ? juce::jlimit(0.0f, 1.0f, lastResultForAudio.keypoints[i][1] / 480.0f)
                : 0.0f;
            
            // Fill the entire buffer with the current value (DC signal)
            for (int sample = 0; sample < cvOutBus.getNumSamples(); ++sample)
            {
                cvOutBus.setSample(chX, sample, x_normalized);
                cvOutBus.setSample(chY, sample, y_normalized);
            }
        }
    }
    
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
ImVec2 PoseEstimatorModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void PoseEstimatorModule::drawParametersInNode(float itemWidth,
                                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                              const std::function<void()>& onModificationEnded)
{
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
            ImGui::SetTooltip("Enable GPU acceleration for pose detection.\nRequires CUDA-capable NVIDIA GPU.");
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
    
    // Only show Model selection (as requested)
    if (modelChoiceParam)
    {
        int m = modelChoiceParam->getIndex();
        if (ImGui::Combo("Model", &m, "BODY_25 (25 pts)\0COCO (18 pts)\0MPI (15 pts)\0MPI Fast (15 pts)\0\0"))
        {
            *modelChoiceParam = m;
            requestedModelIndex = m; // signal thread to reload
            onModificationEnded();
        }
    }

    // Blob size (maps to quality tiers)
    if (qualityParam)
    {
        int blobSize = (qualityParam->getIndex() == 0) ? 224 : 368;
        if (ImGui::SliderInt("Blob Size", &blobSize, 224, 368))
        {
            int q = (blobSize <= 296) ? 0 : 1; // snap to Low/Medium
            *qualityParam = q;
            onModificationEnded();
        }
    }

    // Confidence threshold
    float confidence = confidenceThresholdParam->load();
    if (ImGui::SliderFloat("Confidence", &confidence, 0.0f, 1.0f, "%.2f"))
    {
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence")) = confidence;
        onModificationEnded();
    }

    // Restore Zoom (-/+) controls
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
    
    // Status display
    ImGui::Separator();
    if (modelLoaded)
    {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Model: Loaded");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Keypoints: %d/%d", 
                          lastResultForAudio.detectedPoints, MPI_NUM_KEYPOINTS);
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Model: NOT LOADED");
        ImGui::TextWrapped("Place model files in: assets/openpose_models/pose/mpi/");
    }
    
    ImGui::PopItemWidth();
}

void PoseEstimatorModule::drawIoPins(const NodePinHelpers& helpers)
{
    // Input: Source ID from video loader
    helpers.drawAudioInputPin("Source In", 0);
    
    // Outputs: 30 pins (15 keypoints x 2 coordinates)
    for (int i = 0; i < MPI_NUM_KEYPOINTS; ++i)
    {
        const std::string& name = MPI_KEYPOINT_NAMES[i];
        // CORRECTED: Proper string conversion for ImGui
        juce::String xLabel = juce::String(name) + " X";
        juce::String yLabel = juce::String(name) + " Y";
        helpers.drawAudioOutputPin(xLabel.toRawUTF8(), i * 2);
        helpers.drawAudioOutputPin(yLabel.toRawUTF8(), i * 2 + 1);
    }
    helpers.drawAudioOutputPin("Video Out", 0); // Bus 1
}
#endif
