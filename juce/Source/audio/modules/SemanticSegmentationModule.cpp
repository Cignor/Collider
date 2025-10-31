#include "SemanticSegmentationModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>
#include <fstream>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

static constexpr int ENET_W = 1024;
static constexpr int ENET_H = 512;

juce::AudioProcessorValueTreeState::ParameterLayout SemanticSegmentationModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>("sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("targetClass", "Target Class", juce::StringArray{ "person" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    return { params.begin(), params.end() };
}

SemanticSegmentationModule::SemanticSegmentationModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::discreteChannels(4), true)),
      juce::Thread("Semantic Segmentation Thread"),
      apvts(*this, nullptr, "SemanticSegmentationParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    targetClassParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter("targetClass"));
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
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

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
    while (!threadShouldExit())
    {
        if (!modelLoaded)
        {
            wait(200);
            continue;
        }
        
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(currentSourceId.load());
        if (!frame.empty())
        {
            cv::Mat blob = cv::dnn::blobFromImage(frame, 1.0, cv::Size(ENET_W, ENET_H), cv::Scalar(), true, false);
            net.setInput(blob);
            cv::Mat out = net.forward(); // shape: 1 x C x H x W
            
            int dims = out.dims;
            if (dims == 4)
            {
                int N = out.size[0];
                int C = out.size[1];
                int H = out.size[2];
                int W = out.size[3];
                juce::ignoreUnused(N);
                
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

    const float values[4] { lastResultForAudio.area, lastResultForAudio.centerX, lastResultForAudio.centerY, lastResultForAudio.detected ? 1.0f : 0.0f };
    for (int ch = 0; ch < juce::jmin(4, buffer.getNumChannels()); ++ch)
        for (int s = 0; s < buffer.getNumSamples(); ++s)
            buffer.setSample(ch, s, values[ch]);
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
    if (targetClassParam)
    {
        int idx = targetClassParam->getIndex();
        juce::String items;
        if (!classNames.empty())
        {
            for (const auto& n : classNames) { items += juce::String(n); items += "\0"; }
            items += "\0";
        }
        else
        {
            items = juce::String("person\0\0");
        }
        if (ImGui::Combo("Target Class", &idx, items.toRawUTF8()))
        {
            *targetClassParam = idx;
            onModificationEnded();
        }
    }
    ImGui::PopItemWidth();
}

void SemanticSegmentationModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    helpers.drawAudioOutputPin("Area", 0);
    helpers.drawAudioOutputPin("Center X", 1);
    helpers.drawAudioOutputPin("Center Y", 2);
    helpers.drawAudioOutputPin("Gate", 3);
}
#endif


