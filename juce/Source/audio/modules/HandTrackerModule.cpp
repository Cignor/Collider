#include "HandTrackerModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

// Thumb/Index/Middle/Ring/Pinky chains plus wrist
static const std::vector<std::pair<int, int>> HAND_SKELETON_PAIRS = {
    {0,1},{1,2},{2,3},{3,4},      // Thumb
    {0,5},{5,6},{6,7},{7,8},      // Index
    {0,9},{9,10},{10,11},{11,12}, // Middle
    {0,13},{13,14},{14,15},{15,16}, // Ring
    {0,17},{17,18},{18,19},{19,20}  // Pinky
};

juce::AudioProcessorValueTreeState::ParameterLayout HandTrackerModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("confidence", "Confidence", 0.0f, 1.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{"Small","Normal","Large"}, 1));
    return { params.begin(), params.end() };
}

HandTrackerModule::HandTrackerModule()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::mono(), true)
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(HAND_NUM_KEYPOINTS * 2), true)),
      juce::Thread("Hand Tracker Thread"),
      apvts(*this, nullptr, "HandTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    confidenceThresholdParam = apvts.getRawParameterValue("confidence");
    fifoBuffer.resize(16);
}

HandTrackerModule::~HandTrackerModule()
{
    stopThread(5000);
}

void HandTrackerModule::prepareToPlay(double, int)
{
    startThread(juce::Thread::Priority::normal);
}

void HandTrackerModule::releaseResources()
{
    signalThreadShouldExit();
    stopThread(5000);
}

void HandTrackerModule::loadModel()
{
    auto appDir = juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory();
    auto assetsDir = appDir.getChildFile("assets");
    auto handDir = assetsDir.getChildFile("openpose_models").getChildFile("hand");
    auto protoPath = handDir.getChildFile("pose_deploy.prototxt").getFullPathName();
    auto modelPath = handDir.getChildFile("pose_iter_102000.caffemodel").getFullPathName();
    if (juce::File(protoPath).existsAsFile() && juce::File(modelPath).existsAsFile())
    {
        try { net = cv::dnn::readNetFromCaffe(protoPath.toStdString(), modelPath.toStdString()); modelLoaded = true; }
        catch (...) { modelLoaded = false; }
    }
}

void HandTrackerModule::run()
{
    if (!modelLoaded) loadModel();
    while (!threadShouldExit())
    {
        auto srcId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(srcId);
        if (frame.empty()) { wait(50); continue; }

        cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0/255.0, cv::Size(368,368), cv::Scalar(), false, false);
        net.setInput(blob);
        cv::Mat out = net.forward();
        HandResult result{};
        parseHandOutput(out, frame.cols, frame.rows, result);

        if (fifo.getFreeSpace() >= 1) { auto w = fifo.write(1); if (w.blockSize1 > 0) fifoBuffer[w.startIndex1] = result; }

        // Draw minimal skeleton
        for (const auto& p : HAND_SKELETON_PAIRS)
        {
            int a=p.first,b=p.second; if (result.keypoints[a][0] >= 0 && result.keypoints[b][0] >= 0)
                cv::line(frame, { (int)result.keypoints[a][0], (int)result.keypoints[a][1] },
                              { (int)result.keypoints[b][0], (int)result.keypoints[b][1] }, {0,255,0}, 2);
        }
        for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
            if (result.keypoints[i][0] >= 0)
                cv::circle(frame, { (int)result.keypoints[i][0], (int)result.keypoints[i][1] }, 3, {0,0,255}, -1);
        updateGuiFrame(frame);
        wait(66);
    }
}

void HandTrackerModule::parseHandOutput(const cv::Mat& netOutput, int frameWidth, int frameHeight, HandResult& result)
{
    int H = netOutput.size[2];
    int W = netOutput.size[3];
    float thresh = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    result.detectedPoints = 0;
    int count = juce::jmin(HAND_NUM_KEYPOINTS, netOutput.size[1]);
    for (int i=0;i<count;++i)
    {
        cv::Mat heat(H, W, CV_32F, (void*)netOutput.ptr<float>(0,i));
        double maxVal; cv::Point maxLoc; cv::minMaxLoc(heat, nullptr, &maxVal, nullptr, &maxLoc);
        if (maxVal > thresh) { result.keypoints[i][0] = (float)maxLoc.x * frameWidth / W; result.keypoints[i][1] = (float)maxLoc.y * frameHeight / H; result.detectedPoints++; }
        else { result.keypoints[i][0] = -1.0f; result.keypoints[i][1] = -1.0f; }
    }
}

void HandTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    // Read Source ID from input pin
    auto in = getBusBuffer(buffer, true, 0);
    if (in.getNumChannels()>0 && in.getNumSamples()>0) currentSourceId.store((juce::uint32)in.getSample(0,0));
    buffer.clear();
    if (fifo.getNumReady()>0) { auto r=fifo.read(1); if (r.blockSize1>0) lastResultForAudio=fifoBuffer[r.startIndex1]; }
    for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
    {
        int chX=i*2, chY=i*2+1; if (chY>=buffer.getNumChannels()) break;
        float xn = (lastResultForAudio.keypoints[i][0] >= 0) ? juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][0]/640.0f) : 0.0f;
        float yn = (lastResultForAudio.keypoints[i][1] >= 0) ? juce::jlimit(0.0f,1.0f,lastResultForAudio.keypoints[i][1]/480.0f) : 0.0f;
        for (int s=0;s<buffer.getNumSamples();++s){ buffer.setSample(chX,s,xn); buffer.setSample(chY,s,yn);}    
    }
}

#if defined(PRESET_CREATOR_UI)
ImVec2 HandTrackerModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1; level = juce::jlimit(0,2,level);
    const float widths[3]{240.0f,480.0f,960.0f}; return ImVec2(widths[level],0.0f);
}

static const char* HAND_NAMES[HAND_NUM_KEYPOINTS] = {
    "Wrist",
    "Thumb1","Thumb2","Thumb3","Thumb4",
    "Index1","Index2","Index3","Index4",
    "Middle1","Middle2","Middle3","Middle4",
    "Ring1","Ring2","Ring3","Ring4",
    "Pinky1","Pinky2","Pinky3","Pinky4"
};

void HandTrackerModule::drawParametersInNode(float itemWidth,
                                             const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                             const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    // Confidence
    float conf = confidenceThresholdParam ? confidenceThresholdParam->load() : 0.1f;
    if (ImGui::SliderFloat("Confidence", &conf, 0.0f, 1.0f, "%.2f")) {
        *dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter("confidence")) = conf; onModificationEnded();
    }
    // Zoom -/+
    int level = zoomLevelParam ? (int)zoomLevelParam->load() : 1; level = juce::jlimit(0,2,level);
    float bw = (itemWidth/2.0f)-4.0f; bool atMin=(level<=0), atMax=(level>=2);
    if(atMin) ImGui::BeginDisabled(); if(ImGui::Button("-", ImVec2(bw,0))){ int nl=juce::jmax(0,level-1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMin) ImGui::EndDisabled(); ImGui::SameLine(); if(atMax) ImGui::BeginDisabled(); if(ImGui::Button("+", ImVec2(bw,0))){ int nl=juce::jmin(2,level+1); if(auto* p=apvts.getParameter("zoomLevel")) p->setValueNotifyingHost((float)nl/2.0f); onModificationEnded(); }
    if(atMax) ImGui::EndDisabled();
    ImGui::PopItemWidth();
}

void HandTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    for (int i=0;i<HAND_NUM_KEYPOINTS;++i)
    {
        juce::String x = juce::String(HAND_NAMES[i]) + " X";
        juce::String y = juce::String(HAND_NAMES[i]) + " Y";
        helpers.drawAudioOutputPin(x.toRawUTF8(), i*2);
        helpers.drawAudioOutputPin(y.toRawUTF8(), i*2+1);
    }
}

#endif

void HandTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgra; cv::cvtColor(frame, bgra, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth()!=bgra.cols || latestFrameForGui.getHeight()!=bgra.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgra.cols, bgra.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgra.data, bgra.total()*bgra.elemSize());
}

juce::Image HandTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}


