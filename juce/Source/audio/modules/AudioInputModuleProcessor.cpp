#include "AudioInputModuleProcessor.h"
#include <juce_audio_devices/juce_audio_devices.h>

juce::AudioProcessorValueTreeState::ParameterLayout AudioInputModuleProcessor::
    createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(
        std::make_unique<juce::AudioParameterInt>(
            paramIdNumChannels, "Channels", 1, MAX_CHANNELS, 2));

    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdGateThreshold, "Gate Threshold", 0.0f, 1.0f, 0.1f));
    params.push_back(
        std::make_unique<juce::AudioParameterFloat>(
            paramIdTriggerThreshold, "Trigger Threshold", 0.0f, 1.0f, 0.5f));

    // Create a parameter for each potential channel mapping
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        params.push_back(
            std::make_unique<juce::AudioParameterInt>(
                "channelMap" + juce::String(i),
                "Channel " + juce::String(i + 1) + " Source",
                0,
                255,
                i));
    }
    return {params.begin(), params.end()};
}

AudioInputModuleProcessor::AudioInputModuleProcessor()
    : ModuleProcessor(
          BusesProperties()
              .withInput("In", juce::AudioChannelSet::discreteChannels(MAX_CHANNELS), true)
              .withOutput(
                  "Out",
                  juce::AudioChannelSet::discreteChannels(MAX_CHANNELS + 3),
                  true)), // +3 for Gate, Trig, EOP
      apvts(*this, nullptr, "AudioInputParams", createParameterLayout())
{
    numChannelsParam =
        dynamic_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdNumChannels));

    gateThresholdParam =
        dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdGateThreshold));
    triggerThresholdParam =
        dynamic_cast<juce::AudioParameterFloat*>(apvts.getParameter(paramIdTriggerThreshold));

    channelMappingParams.resize(MAX_CHANNELS);
    for (int i = 0; i < MAX_CHANNELS; ++i)
    {
        channelMappingParams[i] = dynamic_cast<juce::AudioParameterInt*>(
            apvts.getParameter("channelMap" + juce::String(i)));
    }

    lastOutputValues.resize(MAX_CHANNELS + 3); // +3 for new outputs
    channelLevels.resize(MAX_CHANNELS);
    for (int i = 0; i < MAX_CHANNELS + 3; ++i)
    {
        if (i < (int)channelLevels.size())
            channelLevels[i] = std::make_unique<std::atomic<float>>(0.0f);
        lastOutputValues[i] = std::make_unique<std::atomic<float>>(0.0f);
    }

    peakState.resize(MAX_CHANNELS, PeakState::SILENT);
    lastTriggerState.resize(MAX_CHANNELS, false);
    silenceCounter.resize(MAX_CHANNELS, 0);
    eopPulseRemaining.resize(MAX_CHANNELS, 0);
    trigPulseRemaining.resize(MAX_CHANNELS, 0);
    
    // Initialize lock-free FIFO buffers
    fifoBuffer.setSize(MAX_CHANNELS, FIFO_SIZE);
    readBuffer.setSize(MAX_CHANNELS, 512);
    audioFifo.reset();
}

AudioInputModuleProcessor::~AudioInputModuleProcessor()
{
    closeDevice();
}

void AudioInputModuleProcessor::setSelectedDeviceName(const juce::String& name)
{
    if (selectedDeviceName != name)
    {
        selectedDeviceName = name;
        // Only update device if we have both name and type
        if (!selectedDeviceName.isEmpty() && !selectedDeviceType.isEmpty())
        {
            updateDevice();
        }
    }
}

void AudioInputModuleProcessor::updateDevice()
{
    closeDevice();
    
    if (deviceManager == nullptr || selectedDeviceName.isEmpty() || selectedDeviceType.isEmpty())
    {
        juce::Logger::writeToLog("[AudioInput] Cannot update device - missing manager, name, or type");
        return;
    }
    
    // CRITICAL: ASIO drivers don't allow multiple simultaneous connections
    // If the global AudioDeviceManager is using ASIO, we must use the global input bus
    // instead of opening a separate device
    juce::String currentGlobalDeviceType = deviceManager->getCurrentAudioDeviceType();
    bool isASIO = (currentGlobalDeviceType == "ASIO" || selectedDeviceType == "ASIO");
    
    if (isASIO)
    {
        // For ASIO: Use global input bus, don't open separate device
        // The global AudioDeviceManager already has the ASIO device open
        juce::Logger::writeToLog("[AudioInput] ASIO detected - using global input bus (cannot open separate ASIO device)");
        
        // Clear any stored device name to prevent conflicts
        selectedDeviceName = "";
        
        // Check if global device is actually open
        if (auto* globalDevice = deviceManager->getCurrentAudioDevice())
        {
            juce::Logger::writeToLog("[AudioInput] Global ASIO device: " + globalDevice->getName() + 
                                     " (sr: " + juce::String(globalDevice->getCurrentSampleRate()) + 
                                     ", bs: " + juce::String(globalDevice->getCurrentBufferSizeSamples()) + ")");
        }
        else
        {
            juce::Logger::writeToLog("[AudioInput] WARNING: Global ASIO device not open!");
        }
        
        // Don't open a separate device - we'll use the input bus in processBlock
        return;
    }
    
    // For Windows Audio (WASAPI/DirectSound): Can open separate devices
    const auto& deviceTypes = deviceManager->getAvailableDeviceTypes();
    juce::AudioIODeviceType* deviceTypeObj = nullptr;
    
    for (int i = 0; i < deviceTypes.size(); ++i)
    {
        if (auto* type = deviceTypes[i])
        {
            if (type->getTypeName() == selectedDeviceType)
            {
                deviceTypeObj = type;
                break;
            }
        }
    }
    
    if (deviceTypeObj == nullptr)
    {
        juce::Logger::writeToLog("[AudioInput] Device type not found: " + selectedDeviceType);
        return;
    }
    
    deviceTypeObj->scanForDevices();
    auto* devicePtr = deviceTypeObj->createDevice(selectedDeviceName, selectedDeviceName);
    
    if (devicePtr == nullptr)
    {
        juce::Logger::writeToLog("[AudioInput] Failed to create device: " + selectedDeviceName);
        return;
    }
    
    std::unique_ptr<juce::AudioIODevice> device(devicePtr);
    
    // Get available input channels
    auto inputChannels = device->getInputChannelNames();
    int numInputChannels = juce::jmin(inputChannels.size(), MAX_CHANNELS);
    
    if (numInputChannels == 0)
    {
        juce::Logger::writeToLog("[AudioInput] Device has no input channels: " + selectedDeviceName);
        return;
    }
    
    // Configure input channels (enable first N channels)
    juce::BigInteger inputChannelsEnabled;
    inputChannelsEnabled.setRange(0, numInputChannels, true);
    
    // No output channels needed
    juce::BigInteger outputChannelsEnabled;
    
    // Get sample rate and buffer size from current audio setup
    double sampleRate = getSampleRate();
    int bufferSize = getBlockSize();
    
    if (sampleRate <= 0.0)
        sampleRate = 44100.0;
    if (bufferSize <= 0)
        bufferSize = 512;
    
    // Try to open the device for input only
    juce::String error = device->open(inputChannelsEnabled, 
                                      outputChannelsEnabled, 
                                      sampleRate, 
                                      bufferSize);
    
    if (!error.isEmpty())
    {
        // Try with default settings if custom settings failed
        juce::Logger::writeToLog("[AudioInput] Failed to open device with custom settings " + selectedDeviceName + ": " + error);
        device->close(); // Close any partial state
        error = device->open(inputChannelsEnabled, outputChannelsEnabled, 44100.0, 512);
    }
    
    if (error.isEmpty())
    {
        audioDevice = std::move(device);
        
        // Reset callback tracking
        lastCallbackTime.store(0);
        callbackCount.store(0);
        
        audioDevice->start(this);
        
        // Log actual device settings (might differ from requested)
        double actualSR = audioDevice->getCurrentSampleRate();
        int actualBS = audioDevice->getCurrentBufferSizeSamples();
        double expectedIntervalMs = (actualBS / actualSR) * 1000.0;
        
        juce::Logger::writeToLog("[AudioInput] Successfully opened Windows Audio device: " + selectedDeviceName + 
                                 " (channels: " + juce::String(numInputChannels) + 
                                 ", sr: " + juce::String(actualSR) + 
                                 ", bs: " + juce::String(actualBS) + 
                                 ", expected callback interval: " + juce::String(expectedIntervalMs, 2) + "ms" +
                                 ") | Requested: sr=" + juce::String(sampleRate) + " bs=" + juce::String(bufferSize));
        
        // Warn if buffer size mismatch
        if (actualBS != bufferSize)
        {
            juce::Logger::writeToLog("[AudioInput] WARNING: Buffer size mismatch! Device=" + 
                                     juce::String(actualBS) + " vs Requested=" + juce::String(bufferSize));
        }
    }
    else
    {
        juce::Logger::writeToLog("[AudioInput] Failed to open device even with default settings: " + error);
    }
}

void AudioInputModuleProcessor::closeDevice()
{
    if (audioDevice != nullptr)
    {
        audioDevice->stop();
        audioDevice->close();
        audioDevice.reset();
    }
}

void AudioInputModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // Reset all state variables
    std::fill(peakState.begin(), peakState.end(), PeakState::SILENT);
    std::fill(lastTriggerState.begin(), lastTriggerState.end(), false);
    std::fill(silenceCounter.begin(), silenceCounter.end(), 0);
    std::fill(eopPulseRemaining.begin(), eopPulseRemaining.end(), 0);
    std::fill(trigPulseRemaining.begin(), trigPulseRemaining.end(), 0);
    
    // Reset FIFO and pre-allocate readBuffer to handle maximum expected block size
    // ASIO might run at 64-128 samples, but main graph might be 512-2048
    // Allocate for worst case to avoid allocations in audio thread
    const int maxBlockSize = juce::jmax(samplesPerBlock, 2048);
    audioFifo.reset();
    readBuffer.setSize(MAX_CHANNELS, maxBlockSize, false, false, true);
    
    // Update device if needed (may need to reopen with new sample rate/buffer size)
    if (!selectedDeviceName.isEmpty() && !selectedDeviceType.isEmpty())
    {
        // Close and reopen with new settings
        closeDevice();
        updateDevice();
    }
}

void AudioInputModuleProcessor::releaseResources()
{
    closeDevice();
}

void AudioInputModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    auto outBus = getBusBuffer(buffer, false, 0);
    const int numSamples = buffer.getNumSamples();
    
    static int processCount = 0;
    static juce::int64 lastLogTime = 0;
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    
    // Get audio from our dedicated device if available (lock-free FIFO)
    // Use readBuffer directly (pre-allocated) to avoid allocations in audio thread
    bool useCapturedAudio = false;
    bool hasDevice = (audioDevice != nullptr && audioDevice->isOpen());
    
    // Check if we're using ASIO (which uses global input bus instead of separate device)
    bool isUsingASIO = false;
    if (deviceManager != nullptr)
    {
        juce::String currentGlobalDeviceType = deviceManager->getCurrentAudioDeviceType();
        isUsingASIO = (currentGlobalDeviceType == "ASIO" || selectedDeviceType == "ASIO");
    }
    
    if (hasDevice && !isUsingASIO)
    {
        // Read from lock-free FIFO (ASIO-safe, no blocking)
        int samplesAvailable = audioFifo.getNumReady();
        
        // Calculate expected callback interval (should be ~10.67ms for 512 samples at 48kHz)
        juce::int64 lastCallback = lastCallbackTime.load();
        juce::int64 timeSinceCallback = (lastCallback > 0) ? (currentTime - lastCallback) : 999;
        int callbackCountValue = callbackCount.load();
        
        // Log every 100 process blocks or every 5 seconds, or if timing issues detected
        bool shouldLog = (++processCount % 100 == 0) || 
                         ((currentTime - lastLogTime) > 5000) ||
                         (timeSinceCallback > 50); // Callback hasn't run in >50ms
        
        if (shouldLog)
        {
            juce::Logger::writeToLog("[AudioInput] ProcessBlock #" + juce::String(processCount) + 
                                     " | samples=" + juce::String(numSamples) + 
                                     " | FIFO available=" + juce::String(samplesAvailable) +
                                     " | callbacks=" + juce::String(callbackCountValue) +
                                     " | timeSinceCallback=" + juce::String(timeSinceCallback) + "ms" +
                                     " | device open=" + juce::String(hasDevice ? "yes" : "no"));
            lastLogTime = currentTime;
        }
        
        // Read if we have enough samples - no minimum buffer threshold needed
        // The FIFO size (8192) provides enough headroom for timing mismatches
        if (samplesAvailable >= numSamples)
        {
            // We have enough samples - read from FIFO directly into readBuffer
            // readBuffer is pre-allocated in prepareToPlay, no setSize needed
            int start1, size1, start2, size2;
            audioFifo.prepareToRead(numSamples, start1, size1, start2, size2);
            
            for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            {
                if (size1 > 0)
                    readBuffer.copyFrom(ch, 0, fifoBuffer, ch, start1, size1);
                if (size2 > 0)
                    readBuffer.copyFrom(ch, size1, fifoBuffer, ch, start2, size2);
            }
            
            audioFifo.finishedRead(numSamples);
            useCapturedAudio = true;
        }
        else
        {
            // Not enough samples yet - clear readBuffer to silence
            readBuffer.clear();
            useCapturedAudio = true;
            
            // Log underrun with timing info
            static int underrunCount = 0;
            if (++underrunCount % 10 == 0 || timeSinceCallback > 50)
            {
                juce::Logger::writeToLog("[AudioInput] FIFO UNDERRUN! available=" + 
                                         juce::String(samplesAvailable) + " need=" + juce::String(numSamples) +
                                         " | callbacks=" + juce::String(callbackCountValue) +
                                         " | timeSinceCallback=" + juce::String(timeSinceCallback) + "ms");
            }
        }
    }
    
    // Get reference to input buffer (either from FIFO or input bus)
    juce::AudioBuffer<float>* inputBufferPtr = nullptr;
    
    if (useCapturedAudio)
    {
        // Use readBuffer (from FIFO or silence) - Windows Audio with separate device
        inputBufferPtr = &readBuffer;
    }
    else if (isUsingASIO || selectedDeviceName.isEmpty())
    {
        // For ASIO: Always use global input bus (ASIO doesn't allow separate devices)
        // For no device selected: Fall back to input bus
        auto inBus = getBusBuffer(buffer, true, 0);
        // Copy to readBuffer to have consistent interface (pre-allocated, no setSize needed)
        readBuffer.clear();
        for (int ch = 0; ch < inBus.getNumChannels() && ch < MAX_CHANNELS; ++ch)
        {
            readBuffer.copyFrom(ch, 0, inBus, ch, 0, numSamples);
        }
        inputBufferPtr = &readBuffer;
    }
    else
    {
        // Device is selected but not open yet - use silence
        readBuffer.clear();
        inputBufferPtr = &readBuffer;
    }
    
    // Now use inputBufferPtr instead of inputBuffer
    auto& inputBuffer = *inputBufferPtr;

    const int    activeChannels = numChannelsParam ? numChannelsParam->get() : 2;
    const double sampleRate = getSampleRate();

    const float gateThresh = gateThresholdParam ? gateThresholdParam->get() : 0.1f;
    const float trigThresh = triggerThresholdParam ? triggerThresholdParam->get() : 0.5f;

    const int gateOutChannel = MAX_CHANNELS + 0;
    const int trigOutChannel = MAX_CHANNELS + 1;
    const int eopOutChannel = MAX_CHANNELS + 2;

    auto* gateOut =
        outBus.getNumChannels() > gateOutChannel ? outBus.getWritePointer(gateOutChannel) : nullptr;
    auto* trigOut =
        outBus.getNumChannels() > trigOutChannel ? outBus.getWritePointer(trigOutChannel) : nullptr;
    auto* eopOut =
        outBus.getNumChannels() > eopOutChannel ? outBus.getWritePointer(eopOutChannel) : nullptr;

    // Perform CV analysis on the FIRST MAPPED CHANNEL only
    // This allows the user to select which input drives the Gate/Trigger logic
    int cvSourceIndex = -1;
    if (activeChannels > 0 && !channelMappingParams.empty() && channelMappingParams[0])
    {
        cvSourceIndex = channelMappingParams[0]->get();
    }

    if (cvSourceIndex >= 0 && cvSourceIndex < inputBuffer.getNumChannels())
    {
        const float* inData = inputBuffer.getReadPointer(cvSourceIndex);

        for (int s = 0; s < numSamples; ++s)
        {
            const float sampleAbs = std::abs(inData[s]);

            // GATE LOGIC
            if (gateOut)
                gateOut[s] = (sampleAbs > gateThresh) ? 1.0f : 0.0f;

            // TRIGGER LOGIC
            bool isAboveTrig = sampleAbs > trigThresh;
            if (isAboveTrig && !lastTriggerState[0])
            {
                trigPulseRemaining[0] = (int)(0.001 * sampleRate); // 1ms pulse
            }
            lastTriggerState[0] = isAboveTrig;

            if (trigOut)
            {
                trigOut[s] = (trigPulseRemaining[0] > 0) ? 1.0f : 0.0f;
                if (trigPulseRemaining[0] > 0)
                    --trigPulseRemaining[0];
            }

            // EOP LOGIC
            if (peakState[0] == PeakState::PEAK)
            {
                if (sampleAbs < gateThresh)
                {
                    silenceCounter[0]++;
                    if (silenceCounter[0] >= MIN_SILENCE_SAMPLES)
                    {
                        peakState[0] = PeakState::SILENT;
                        eopPulseRemaining[0] = (int)(0.001 * sampleRate); // Fire 1ms pulse
                    }
                }
                else
                {
                    silenceCounter[0] = 0;
                }
            }
            else
            { // PeakState::SILENT
                if (sampleAbs > gateThresh)
                {
                    peakState[0] = PeakState::PEAK;
                    silenceCounter[0] = 0;
                }
            }

            if (eopOut)
            {
                eopOut[s] = (eopPulseRemaining[0] > 0) ? 1.0f : 0.0f;
                if (eopPulseRemaining[0] > 0)
                    --eopPulseRemaining[0];
            }
        }
    }
    else
    {
        // If the source for CV is invalid, clear CV outputs
        if (gateOut)
            juce::FloatVectorOperations::clear(gateOut, numSamples);
        if (trigOut)
            juce::FloatVectorOperations::clear(trigOut, numSamples);
        if (eopOut)
            juce::FloatVectorOperations::clear(eopOut, numSamples);
    }

    // Now, loop through the active channels for pass-through and metering
    for (int i = 0; i < activeChannels; ++i)
    {
        // Determine source channel from parameters
        int sourceChannelIndex = i; // Default fallback
        if (i < (int)channelMappingParams.size() && channelMappingParams[i])
            sourceChannelIndex = channelMappingParams[i]->get();

        if (i < outBus.getNumChannels())
        {
        if (sourceChannelIndex >= 0 && sourceChannelIndex < inputBuffer.getNumChannels())
        {
            // Valid source: copy audio and update meter
            outBus.copyFrom(i, 0, inputBuffer, sourceChannelIndex, 0, numSamples);

            float peakForMeter = inputBuffer.getMagnitude(sourceChannelIndex, 0, numSamples);
                if (i < (int)channelLevels.size() && channelLevels[i])
                {
                    channelLevels[i]->store(peakForMeter);
                }
            }
            else
            {
                // Invalid source: clear output and meter
                outBus.clear(i, 0, numSamples);
                if (i < (int)channelLevels.size() && channelLevels[i])
                {
                    channelLevels[i]->store(0.0f);
                }
            }

            // Update inspector value for this output channel
            if (i < (int)lastOutputValues.size() && lastOutputValues[i] && numSamples > 0)
                lastOutputValues[i]->store(outBus.getSample(i, numSamples - 1));
        }
    }

    // Clear any unused audio output channels (but NOT the CV channels)
    for (int i = activeChannels; i < MAX_CHANNELS; ++i)
    {
        if (i < outBus.getNumChannels())
            outBus.clear(i, 0, numSamples);
        if (i < (int)channelLevels.size() && channelLevels[i])
            channelLevels[i]->store(0.0f);
    }

    // Update the inspector values for the new CV outs
    if (numSamples > 0)
    {
        if (gateOut && lastOutputValues[gateOutChannel])
            lastOutputValues[gateOutChannel]->store(gateOut[numSamples - 1]);
        if (trigOut && lastOutputValues[trigOutChannel])
            lastOutputValues[trigOutChannel]->store(trigOut[numSamples - 1]);
        if (eopOut && lastOutputValues[eopOutChannel])
            lastOutputValues[eopOutChannel]->store(eopOut[numSamples - 1]);
    }
}

juce::ValueTree AudioInputModuleProcessor::getExtraStateTree() const
{
    juce::ValueTree vt("AudioInputState");
    vt.setProperty("deviceName", selectedDeviceName, nullptr);
    vt.setProperty("deviceType", selectedDeviceType, nullptr);
    vt.addChild(apvts.state.createCopy(), -1, nullptr);
    return vt;
}

void AudioInputModuleProcessor::setExtraStateTree(const juce::ValueTree& vt)
{
    if (vt.hasType("AudioInputState"))
    {
        selectedDeviceName = vt.getProperty("deviceName", "").toString();
        selectedDeviceType = vt.getProperty("deviceType", "").toString();
        auto params = vt.getChildWithName(apvts.state.getType());
        if (params.isValid())
        {
            apvts.replaceState(params);
        }
        updateDevice();
    }
}

void AudioInputModuleProcessor::audioDeviceIOCallbackWithContext(const float* const* inputChannelData, int numInputChannels,
                                                                   float* const* outputChannelData, int numOutputChannels,
                                                                   int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    juce::ignoreUnused(outputChannelData, numOutputChannels, context);
    
    // Track callback timing
    juce::int64 currentTime = juce::Time::currentTimeMillis();
    juce::int64 lastTime = lastCallbackTime.exchange(currentTime);
    int count = callbackCount.fetch_add(1) + 1;
    
    // Calculate time since last callback
    juce::int64 timeSinceLast = (lastTime > 0) ? (currentTime - lastTime) : 0;
    
    // Log every 1000 callbacks or every 5 seconds, or if timing is off
    static juce::int64 lastLogTime = 0;
    bool shouldLog = (count % 1000 == 0) || 
                    ((currentTime - lastLogTime) > 5000) ||
                    (timeSinceLast > 50); // More than 50ms between callbacks is suspicious
    
    if (shouldLog)
    {
        int freeSpace = audioFifo.getFreeSpace();
        int ready = audioFifo.getNumReady();
        juce::Logger::writeToLog("[AudioInput] ASIO Callback #" + juce::String(count) + 
                                 " | samples=" + juce::String(numSamples) + 
                                 " | ch=" + juce::String(numInputChannels) +
                                 " | timeSinceLast=" + juce::String(timeSinceLast) + "ms" +
                                 " | FIFO free=" + juce::String(freeSpace) + 
                                 " | FIFO ready=" + juce::String(ready));
        lastLogTime = currentTime;
    }
    
    // Lock-free write to FIFO (ASIO-safe, no blocking)
    if (inputChannelData != nullptr && numInputChannels > 0 && numSamples > 0)
    {
        int freeSpace = audioFifo.getFreeSpace();
        
        if (freeSpace >= numSamples)
        {
            // Write to FIFO
            int start1, size1, start2, size2;
            audioFifo.prepareToWrite(numSamples, start1, size1, start2, size2);
            
            for (int ch = 0; ch < numInputChannels && ch < MAX_CHANNELS; ++ch)
            {
                if (inputChannelData[ch] != nullptr)
                {
                    if (size1 > 0)
                        fifoBuffer.copyFrom(ch, start1, inputChannelData[ch], size1);
                    if (size2 > 0)
                        fifoBuffer.copyFrom(ch, start2, inputChannelData[ch] + size1, size2);
                }
            }
            
            // Zero out any unused channels
            for (int ch = numInputChannels; ch < MAX_CHANNELS; ++ch)
            {
                if (size1 > 0)
                    fifoBuffer.clear(ch, start1, size1);
                if (size2 > 0)
                    fifoBuffer.clear(ch, start2, size2);
            }
            
            audioFifo.finishedWrite(numSamples);
        }
        else
        {
            // FIFO is full - log warning occasionally
            static int dropCount = 0;
            if (++dropCount % 100 == 0)
            {
                juce::Logger::writeToLog("[AudioInput] WARNING: FIFO full, dropping samples! free=" + 
                                         juce::String(freeSpace) + " need=" + juce::String(numSamples));
            }
        }
    }
}

void AudioInputModuleProcessor::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    juce::ignoreUnused(device);
    juce::Logger::writeToLog("[AudioInput] Device about to start: " + (device ? device->getName() : juce::String("unknown")));
}

void AudioInputModuleProcessor::audioDeviceStopped()
{
    // Clear FIFO (lock-free operation)
    audioFifo.reset();
    fifoBuffer.clear();
    juce::Logger::writeToLog("[AudioInput] Device stopped");
}

void AudioInputModuleProcessor::audioDeviceError(const juce::String& errorMessage)
{
    juce::Logger::writeToLog("[AudioInput] Device error: " + errorMessage);
}

juce::String AudioInputModuleProcessor::getAudioInputLabel(int channel) const
{
    if (channel < MAX_CHANNELS)
        return "HW In " + juce::String(channel + 1);
    return {};
}

juce::String AudioInputModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == MAX_CHANNELS + 0)
        return "Gate";
    if (channel == MAX_CHANNELS + 1)
        return "Trigger";
    if (channel == MAX_CHANNELS + 2)
        return "EOP";

    if (channel < MAX_CHANNELS)
        return "Out " + juce::String(channel + 1);
    return {};
}

#if defined(PRESET_CREATOR_UI)
// The actual UI drawing will be handled by a special case in ImGuiNodeEditorComponent,
// so this can remain empty.
void AudioInputModuleProcessor::drawParametersInNode(
    float,
    const std::function<bool(const juce::String&)>&,
    const std::function<void()>&)
{
    // UI is custom-drawn in ImGuiNodeEditorComponent.cpp
}

void AudioInputModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    // Dynamically draw output pins based on the number of active channels
    int numChannels = numChannelsParam ? numChannelsParam->get() : 2;
    for (int i = 0; i < numChannels; ++i)
    {
        helpers.drawAudioOutputPin(("Out " + juce::String(i + 1)).toRawUTF8(), i);
    }

    // Draw CV outputs
    helpers.drawAudioOutputPin("Gate", MAX_CHANNELS + 0);
    helpers.drawAudioOutputPin("Trigger", MAX_CHANNELS + 1);
    helpers.drawAudioOutputPin("EOP", MAX_CHANNELS + 2);
}
#endif
