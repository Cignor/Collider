#include "ColorTrackerModule.h"
#include "../../video/VideoFrameManager.h"
#include <opencv2/imgproc.hpp>

#if defined(PRESET_CREATOR_UI)
#include <imgui.h>
#endif

juce::AudioProcessorValueTreeState::ParameterLayout ColorTrackerModule::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "sourceId", "Source ID", 0.0f, 1000.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "zoomLevel", "Zoom Level", juce::StringArray{ "Small", "Normal", "Large" }, 1));
    return { params.begin(), params.end() };
}

ColorTrackerModule::ColorTrackerModule()
    : ModuleProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::mono(), true)
                      .withOutput("Output", juce::AudioChannelSet::discreteChannels(24), true)), // up to 8 colors x 3
      juce::Thread("Color Tracker Thread"),
      apvts(*this, nullptr, "ColorTrackerParams", createParameterLayout())
{
    sourceIdParam = apvts.getRawParameterValue("sourceId");
    zoomLevelParam = apvts.getRawParameterValue("zoomLevel");
    fifoBuffer.resize(16);
}

ColorTrackerModule::~ColorTrackerModule()
{
    stopThread(5000);
}

void ColorTrackerModule::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    startThread(juce::Thread::Priority::normal);
}

void ColorTrackerModule::releaseResources()
{
    signalThreadShouldExit();
}

void ColorTrackerModule::run()
{
    while (!threadShouldExit())
    {
        juce::uint32 sourceId = currentSourceId.load();
        cv::Mat frame = VideoFrameManager::getInstance().getFrame(sourceId);
        if (!frame.empty())
        {
            cv::Mat hsv;
            cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);

            // Color picker integration (coordinates provided by UI)
            if (addColorRequested.exchange(false))
            {
                int mx = pickerMouseX.exchange(-1);
                int my = pickerMouseY.exchange(-1);
                if (mx >= 0 && my >= 0 && mx < hsv.cols && my < hsv.rows)
                {
                    cv::Vec3b pixel = hsv.at<cv::Vec3b>(my, mx);
                    int hue = pixel[0];
                    TrackedColor tc;
                    tc.name = juce::String("Color ") + juce::String((int)trackedColors.size() + 1);
                    tc.hsvLower = cv::Scalar(std::max(0, hue - 10), 100, 100);
                    tc.hsvUpper = cv::Scalar(std::min(179, hue + 10), 255, 255);
                    const juce::ScopedLock lock(colorListLock);
                    trackedColors.push_back(tc);
                }
                isColorPickerActive.store(false);
            }

            ColorResult result;
            {
                const juce::ScopedLock lock(colorListLock);
                for (auto& tc : trackedColors)
                {
                    cv::Mat mask;
                    cv::inRange(hsv, tc.hsvLower, tc.hsvUpper, mask);
                    
                    // Morphological cleanup
                    cv::erode(mask, mask, cv::Mat(), cv::Point(-1,-1), 1);
                    cv::dilate(mask, mask, cv::Mat(), cv::Point(-1,-1), 1);
                    
                    std::vector<std::vector<cv::Point>> contours;
                    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
                    
                    if (!contours.empty())
                    {
                        size_t best = 0;
                        double maxArea = 0.0;
                        for (size_t i = 0; i < contours.size(); ++i)
                        {
                            double a = cv::contourArea(contours[i]);
                            if (a > maxArea) { maxArea = a; best = i; }
                        }
                        const auto& c = contours[best];
                        cv::Moments m = cv::moments(c);
                        float cx = (m.m00 > 0.0) ? (float)(m.m10 / m.m00) / (float)frame.cols : 0.5f;
                        float cy = (m.m00 > 0.0) ? (float)(m.m01 / m.m00) / (float)frame.rows : 0.5f;
                        float area = juce::jlimit(0.0f, 1.0f, (float)(maxArea / (frame.cols * frame.rows)));
                        result.emplace_back(cx, cy, area);
                        
                        // Draw
                        cv::Rect bbox = cv::boundingRect(c);
                        cv::rectangle(frame, bbox, cv::Scalar(0, 255, 255), 2);
                        cv::putText(frame, tc.name.toStdString(), bbox.tl() + cv::Point(0, -5), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
                    }
                    else
                    {
                        result.emplace_back(0.5f, 0.5f, 0.0f);
                    }
                }
            }

            if (fifo.getFreeSpace() >= 1)
            {
                auto writeScope = fifo.write(1);
                if (writeScope.blockSize1 > 0)
                    fifoBuffer[writeScope.startIndex1] = result;
            }
            
            updateGuiFrame(frame);
        }
        
        wait(33);
    }
}

void ColorTrackerModule::updateGuiFrame(const cv::Mat& frame)
{
    cv::Mat bgraFrame;
    cv::cvtColor(frame, bgraFrame, cv::COLOR_BGR2BGRA);
    const juce::ScopedLock lock(imageLock);
    if (latestFrameForGui.isNull() || latestFrameForGui.getWidth() != bgraFrame.cols || latestFrameForGui.getHeight() != bgraFrame.rows)
        latestFrameForGui = juce::Image(juce::Image::ARGB, bgraFrame.cols, bgraFrame.rows, true);
    juce::Image::BitmapData dest(latestFrameForGui, juce::Image::BitmapData::writeOnly);
    memcpy(dest.data, bgraFrame.data, bgraFrame.total() * bgraFrame.elemSize());
}

juce::Image ColorTrackerModule::getLatestFrame()
{
    const juce::ScopedLock lock(imageLock);
    return latestFrameForGui.createCopy();
}

void ColorTrackerModule::addColorAt(int x, int y)
{
    pickerMouseX.store(x);
    pickerMouseY.store(y);
    addColorRequested.store(true);
}

void ColorTrackerModule::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    // Map each tracked color to 3 outputs: X, Y, Area
    for (size_t i = 0; i < lastResultForAudio.size(); ++i)
    {
        int chX = (int)i * 3 + 0;
        int chY = (int)i * 3 + 1;
        int chA = (int)i * 3 + 2;
        if (chA < buffer.getNumChannels())
        {
            const auto& tpl = lastResultForAudio[i];
            float vx = std::get<0>(tpl);
            float vy = std::get<1>(tpl);
            float va = std::get<2>(tpl);
            for (int s = 0; s < buffer.getNumSamples(); ++s)
            {
                buffer.setSample(chX, s, vx);
                buffer.setSample(chY, s, vy);
                buffer.setSample(chA, s, va);
            }
        }
    }
}

std::vector<DynamicPinInfo> ColorTrackerModule::getDynamicOutputPins() const
{
    std::vector<DynamicPinInfo> pins;
    const juce::ScopedLock lock(colorListLock);
    for (size_t i = 0; i < trackedColors.size(); ++i)
    {
        pins.emplace_back(trackedColors[i].name + " X", (int)(i * 3 + 0), PinDataType::CV);
        pins.emplace_back(trackedColors[i].name + " Y", (int)(i * 3 + 1), PinDataType::CV);
        pins.emplace_back(trackedColors[i].name + " Area", (int)(i * 3 + 2), PinDataType::CV);
    }
    return pins;
}

#if defined(PRESET_CREATOR_UI)
ImVec2 ColorTrackerModule::getCustomNodeSize() const
{
    int level = zoomLevelParam ? (int) zoomLevelParam->load() : 1;
    level = juce::jlimit(0, 2, level);
    const float widths[3] { 240.0f, 480.0f, 960.0f };
    return ImVec2(widths[level], 0.0f);
}

void ColorTrackerModule::drawParametersInNode(float itemWidth,
                                              const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                              const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded);
    ImGui::PushItemWidth(itemWidth);

    if (ImGui::Button("Add Color...", ImVec2(itemWidth, 0)))
        isColorPickerActive.store(true);

    if (isColorPickerActive.load())
    {
        ImGui::TextColored(ImVec4(1.f,1.f,0.f,1.f), "Click on the video preview to pick a color");
    }

    // Render tracked color list with remove buttons
    {
        const juce::ScopedLock lock(colorListLock);
        for (size_t i = 0; i < trackedColors.size(); )
        {
            ImGui::Separator();
            juce::String label = trackedColors[i].name + "##" + juce::String((int)i);
            ImGui::TextUnformatted(label.toRawUTF8());
            ImGui::SameLine();
            if (ImGui::SmallButton((juce::String("Remove##") + juce::String((int)i)).toRawUTF8()))
            {
                trackedColors.erase(trackedColors.begin() + (long long)i);
                continue; // don't increment i
            }
            ++i;
        }
    }

    ImGui::PopItemWidth();
}

void ColorTrackerModule::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Source In", 0);
    // Outputs are dynamic; editor queries via getDynamicOutputPins
}
#endif


