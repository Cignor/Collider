#include "MainComponent.h"
#include "../audio/AudioEngine.h"

struct ConnTimer : public juce::Timer
{
    MainComponent& mc;
    ConnTimer(MainComponent& m) : mc(m) { startTimerHz(10); }
    void timerCallback() override
    {
        mc.connLabel.setText("OSC: listening", juce::dontSendNotification);
    }
};

MainComponent::MainComponent()
    : testHarness(
          deviceManager) // Initialize testHarness with deviceManager in member initializer list
{
    // Create audio engine (OSC + graph, acts as AudioSource)
    audioEngine = std::make_unique<AudioEngine>(deviceManager);

    // Make the UI visible
    addAndMakeVisible(testHarness);

    // THIS IS THE CRITICAL CONNECTION:
    // Pass the engine pointer to the UI component.
    testHarness.setAudioEngine(audioEngine.get());

    // Open default audio device on this MainComponent (the AudioAppComponent)

#if JUCE_ASIO
    // Try to set ASIO as the preferred device type before initialization
    juce::Logger::writeToLog("Setting preferred audio device type to ASIO...");
    deviceManager.setCurrentAudioDeviceType("ASIO", true);
#endif

    setAudioChannels(2, 2); // Request 2 input channels, 2 output channels
}

MainComponent::~MainComponent()
{
    // Close audio device on MainComponent
    shutdownAudio();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawFittedText(
        "Collider Audio Engine (JUCE)", getLocalBounds(), juce::Justification::centredTop, 1);
}

void MainComponent::resized()
{
    // Make the TestHarnessComponent fill the entire window.
    testHarness.setBounds(getLocalBounds());
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    juce::Logger::writeToLog("[APP] MainComponent::prepareToPlay called");
    // Log current audio device selection for diagnostics
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup(setup);
        auto* dev = deviceManager.getCurrentAudioDevice();
        juce::Logger::writeToLog(
            "[APP] Audio device: out='" + setup.outputDeviceName +
            "' sr=" + juce::String(dev ? dev->getCurrentSampleRate() : 0.0) +
            " bs=" + juce::String(dev ? dev->getCurrentBufferSizeSamples() : 0));
    }
    if (audioEngine)
        audioEngine->prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (audioEngine)
        audioEngine->getNextAudioBlock(bufferToFill);
    else if (bufferToFill.buffer != nullptr)
        bufferToFill.buffer->clear();
}

void MainComponent::releaseResources()
{
    juce::Logger::writeToLog("[APP] MainComponent::releaseResources called");
    if (audioEngine)
        audioEngine->releaseResources();
}
