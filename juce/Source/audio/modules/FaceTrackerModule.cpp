#include "FaceTrackerModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

juce::AudioProcessorValueTreeState::ParameterLayout FaceTrackerModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("confidence", "Confidence", 0.0f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{"Small","Normal","Large"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterBool>("useGpu", "Use GPU (CUDA)", false)); // Default OFF for compatibility
    return { params.begin(), params.end() };
}

FaceTrackerModule::FaceTrackerModule()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(FACE_NUM_KEYPOINTS * 2), true)),
      juce::Thread("Face Tracker Thread"),
      apvts(*this, nullptr, "FaceTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    useGpuParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("useGpu"));
    fifoBuffer.resize(16);
}

FaceTrackerModule::~FaceTrackerModule()
{
    stopThread(5000);
}

void FaceTrackerModule::prepareToPlay(double, int)
{
    startThread(juce::Thread::Priority::normal);
}

void FaceTrackerModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void FaceTrackerModule::loadModel()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
    auto assetsDir = appDir.getChildFile("assets");
    auto faceDir = assetsDir.getChildFile("openpose_models").getChildFile("face");
    auto haarPath = faceDir.getChildFile("haarcascade_frontalface_alt.xml").getFullPathName();
    auto protoPath = faceDir.getChildFile("pose_deploy.prototxt").getFullPathName();
    auto modelPath = faceDir.getChildFile("pose_iter_116000.caffemodel").getFullPathName();
    bool ok = faceCascade.load(haarPath.toStdString());
    if (ok && juce::File(protoPath).existsAsFile() && juce::File(modelPath).existsAsFile())
    {
        try { net = cv::dnn::readNetFromCaffe(protoPath.toStdString(), modelPath.toStdString()); modelLoaded = true; }
        catch (...) { modelLoaded = false; }
    }
}

void FaceTrackerModule::run()
{
    if (!modelLoaded) loadModel();
    
    #if WITH_CUDA_SUPPORT
        bool lastGpuState = false; // Track GPU state to minimize backend switches
        bool loggedGpuWarning = false; // Only warn once if no GPU available
    #endif
    
    while (!threadShouldExit())
    {
        auto srcId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(srcId);
        if (frame.empty()) { wait(50); continue; }

        bool useGpu = false;
        
        #if WITH_CUDA_SUPPORT
            // Check if user wants GPU and if CUDA device is available
            useGpu = useGpuParam->get();
            if (useGpu && cv::cuda::getCudaEnabledDeviceCount() == 0)
            {
                useGpu = false; // Fallback to CPU
                if (!loggedGpuWarning)
                {
                    juce::Logger::writeToLog("[FaceTracker] WARNING: GPU requested but no CUDA device found. Using CPU.");
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
                    juce::Logger::writeToLog("[FaceTracker] ✓ Switched to CUDA backend (GPU)");
                }
                else
                {
                    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
                    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
                    juce::Logger::writeToLog("[FaceTracker] Switched to CPU backend");
                }
                lastGpuState = useGpu;
            }
        #endif

        cv::Mat gray; cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        std::vector<cv::Rect> faces; faceCascade.detectMultiScale(gray, faces);
        FaceResult result{};
        if (!faces.empty())
        {
            cv::Rect box = faces[0];
            cv::Mat roi = frame(box);
            // NOTE: For DNN models, blobFromImage works on CPU
            // The GPU acceleration happens in net.forward() when backend is set to CUDA
            cv::Mat blob = cv::dnn::blobFromImage(roi, 1.0/255.0, cv::Size(368,368), cv::Scalar(), false, false);
            net.setInput(blob);
            // Forward pass (GPU-accelerated if backend is CUDA)
            cv::Mat out = net.forward();
            parseFaceOutput(out, box, result);
            // Draw box
            cv::rectangle(frame, box, {0,255,0}, 2);
            for (int i=0;i<FACE_NUM_KEYPOINTS;++i)
                if (result.keypoints[i][0] >= 0)
                    cv::circle(frame, { (int)result.keypoints[i][0], (int)result.keypoints[i][1] }, 2, {0,0,255}, -1);
        }
        if (fifo.getFreeSpace()>=1){ auto w=fifo.write(1); if (w.blockSize1>0) fifoBuffer[w.startIndex1]=result; }
        updateGuiFrame(frame);
        wait(66);
    }
}

void FaceTrackerModule::parseFaceOutput(const cv::Mat& netOutput, const cv::Rect& faceBox, FaceResult& result)
{
    int H=netOutput.size[2], W=netOutput.size[3];
    float thresh = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    result.detectedPoints=0; int count=juce::jmin(FACE_NUM_KEYPOINTS, netOutput.size[1]);
    for (int i=0;i<count;++i)
    {
        cv::Mat heat(H,W,CV_32F,(void*)netOutput.ptr<float>(0,i));
        double mv; cv::Point ml; cv::minMaxLoc(heat,nullptr,&mv,nullptr,&ml);
        if (mv>thresh){ result.keypoints[i][0]=(float)faceBox.x + (float)ml.x*faceBox.width/W; result.keypoints[i][1]=(float)faceBox.y + (float)ml.y*faceBox.height/H; result.detectedPoints++; }
        else { result.keypoints[i][0]=-1.0f; result.keypoints[i][1]=-1.0f; }
    }
}

void FaceTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    auto in=getBusBuffer(buffer,true,0); if(in.getNumChannels()>0 && in.getNumSamples()>0) currentSourceId.store((juce::uint32)in.getSample(0,0));
    buffer.clear(); if(fifo.getNumReady()>0){auto r=fifo.read(1); if(r.blockSize1>0) lastResultForAudio=fifoBuffer[r.startIndex1];}
    for(int i=0;i<FACE_NUM_KEYPOINTS;++i){int chX=i*2, chY=i*2+1; if(chY>=buffer.getNumChannels()) break; float xn=(lastResultForAudio.keypoints[i][0]>=0)?juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][0]/640.0f):0.0f; float yn=(lastResultForAudio.keypoints[i][1]>=0)?juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][1]/480.0f):0.0f; for(int s=0;s<buffer.getNumSamples();++s){buffer.setSample(chX,s,xn); buffer.setSample(chY,s,yn);} }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 FaceTrackerModule::getCustomNodeSize() const
{ int level=zoomLevelParam?(int)zoomLevelParam->load():1; level=juce::jlimit(0,2,level); const float widths[3]{240.0f,480.0f,960.0f}; return ImVec2(widths[level],0.0f);} 

void FaceTrackerModule::drawParametersInNode(float itemWidth,
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
            ImGui::SetTooltip("Enable GPU acceleration for face tracking.\nRequires CUDA-capable NVIDIA GPU.");
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
    
    float conf=confidenceThresholdParam?confidenceThresholdParam->load():0.1f;
    if (ImGui::SliderFloat("Confidence", &conf, 0.0f, 1.0f, "%.2f")) { *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence"))=conf; onModificationEnded(); }
    int level=zoomLevelParam?(int)zoomLevelParam->load():1; level=juce::jlimit(0,2,level); float bw=(itemWidth/2.0f)-4.0f; bool atMin=(level<=0), atMax=(level>=2);
    if(atMin) ImGui::BeginDisabled(); if(ImGui::Button("-", ImVec2(bw,0))){ int nl=juce::jmax(0,level-1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMin) ImGui::EndDisabled(); ImGui::SameLine(); if(atMax) ImGui::BeginDisabled(); if(ImGui::Button("+", ImVec2(bw,0))){ int nl=juce::jmin(2,level+1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMax) ImGui::EndDisabled();
    ImGui::PopItemWidth();
}

void FaceTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    for(int i=0;i<FACE_NUM_KEYPOINTS;++i)
    { juce::String base = juce::String("Pt ") + juce::String(i+1); helpers.drawAudioOutputPin((base+" X").toRawUTF8(), i*2); helpers.drawAudioOutputPin((base+" Y").toRawUTF8(), i*2+1);}    
}

#endif

void FaceTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra; cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA); const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth()!=bgra.cols || latestFrameForGui.getHeight()!=bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly); memcpy(dest.data, bgra.data, bgra.total()*bgra.elemSize());
}

juce::Image FaceTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}


