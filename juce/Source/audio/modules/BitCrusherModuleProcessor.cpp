#include "BitCrusherModuleProcessor.h"
#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/theme/ThemeManager.h"
#endif
#include <cmath> // For std::exp2, std::floor

juce::AudioProcessorValueTreeState::ParameterLayout BitCrusherModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Bit Depth: 1.0f to 24.0f with logarithmic scaling (skew factor 0.3 like Waveshaper's drive)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdBitDepth, "Bit Depth", 
        juce::NormalisableRange<float>(1.0f, 24.0f, 0.01f, 0.3f), 16.0f));
    
    // Sample Rate: 0.1f to 1.0f with logarithmic scaling (skew factor 0.3)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdSampleRate, "Sample Rate", 
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.001f, 0.3f), 1.0f));
    
    // Mix: 0.0f to 1.0f (linear, like DriveModuleProcessor)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(paramIdMix, "Mix", 0.0f, 1.0f, 1.0f));
    
    // Anti-aliasing filter
    params.push_back(std::make_unique<juce::AudioParameterBool>(paramIdAntiAlias, "Anti-Aliasing", true));
    
    // Quantization mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(paramIdQuantMode, "Quant Mode", 
        juce::StringArray{"Linear", "Dither (TPDF)", "Noise Shaping"}, 0));
    
    // Relative modulation parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeBitDepthMod", "Relative Bit Depth Mod", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>("relativeSampleRateMod", "Relative Sample Rate Mod", true));
    
    return { params.begin(), params.end() };
}

BitCrusherModuleProcessor::BitCrusherModuleProcessor()
    : ModuleProcessor(BusesProperties()
          .withInput("Audio In", juce::AudioChannelSet::discreteChannels(6), true) // 0-1: Audio, 2: BitDepth Mod, 3: SampleRate Mod, 4: AntiAlias Mod, 5: QuantMode Mod
          .withOutput("Audio Out", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "BitCrusherParams", createParameterLayout())
{
    bitDepthParam = apvts.getRawParameterValue(paramIdBitDepth);
    sampleRateParam = apvts.getRawParameterValue(paramIdSampleRate);
    mixParam = apvts.getRawParameterValue(paramIdMix);
    antiAliasParam = apvts.getRawParameterValue(paramIdAntiAlias);
    quantModeParam = apvts.getRawParameterValue(paramIdQuantMode);
    relativeBitDepthModParam = apvts.getRawParameterValue("relativeBitDepthMod");
    relativeSampleRateModParam = apvts.getRawParameterValue("relativeSampleRateMod");

    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out L
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f)); // Out R
    
    // Initialize smoothed values
    mBitDepthSm.reset(16.0f);
    mSampleRateSm.reset(1.0f);
}

void BitCrusherModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    tempBuffer.setSize(2, samplesPerBlock);
    
    // Set smoothing time for parameters (10ms)
    mBitDepthSm.reset(sampleRate, 0.01);
    mSampleRateSm.reset(sampleRate, 0.01);
    
    // Prepare anti-aliasing filters
    juce::dsp::ProcessSpec specL { sampleRate, (juce::uint32)samplesPerBlock, 1 };
    juce::dsp::ProcessSpec specR { sampleRate, (juce::uint32)samplesPerBlock, 1 };
    mAntiAliasFilterL.prepare(specL);
    mAntiAliasFilterR.prepare(specR);
    mAntiAliasFilterL.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    mAntiAliasFilterR.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    
    // Reset quantization errors
    mQuantErrorL = 0.0f;
    mQuantErrorR = 0.0f;
}

void BitCrusherModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);
    
    auto inBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);
    
    const float baseBitDepth = bitDepthParam != nullptr ? bitDepthParam->load() : 16.0f;
    const float baseSampleRate = sampleRateParam != nullptr ? sampleRateParam->load() : 1.0f;
    const float mixAmount = mixParam != nullptr ? mixParam->load() : 1.0f;

    const int numInputChannels = inBus.getNumChannels();
    const int numOutputChannels = outBus.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(numInputChannels, numOutputChannels);

    // ✅ CRITICAL FIX: Read CV inputs BEFORE any output operations to avoid buffer aliasing issues
    // According to DEBUG_INPUT_IMPORTANT.md: "Read all inputs BEFORE clearing any outputs"
    // Get pointers to modulation CV inputs from unified input bus FIRST
    // Use virtual _mod IDs as per BestPracticeNodeProcessor.md
    const bool isBitDepthMod = isParamInputConnected(paramIdBitDepthMod);
    const bool isSampleRateMod = isParamInputConnected(paramIdSampleRateMod);
    const bool isAntiAliasMod = isParamInputConnected(paramIdAntiAliasMod);
    const bool isQuantModeMod = isParamInputConnected(paramIdQuantModeMod);
    const float* bitDepthCV = isBitDepthMod && inBus.getNumChannels() > 2 ? inBus.getReadPointer(2) : nullptr;
    const float* sampleRateCV = isSampleRateMod && inBus.getNumChannels() > 3 ? inBus.getReadPointer(3) : nullptr;
    const float* antiAliasCV = isAntiAliasMod && inBus.getNumChannels() > 4 ? inBus.getReadPointer(4) : nullptr;
    const float* quantModeCV = isQuantModeMod && inBus.getNumChannels() > 5 ? inBus.getReadPointer(5) : nullptr;
    
    const bool baseAntiAlias = antiAliasParam != nullptr && antiAliasParam->load() > 0.5f;
    const int baseQuantMode = quantModeParam != nullptr ? static_cast<int>(quantModeParam->load()) : 0;
    const bool relativeBitDepthMode = relativeBitDepthModParam != nullptr && relativeBitDepthModParam->load() > 0.5f;
    const bool relativeSampleRateMode = relativeSampleRateModParam != nullptr && relativeSampleRateModParam->load() > 0.5f;

    // ✅ Now safe to copy input to output (CV pointers already obtained)
    if (numInputChannels > 0)
    {
        // If input is mono, copy it to both left and right outputs.
        if (numInputChannels == 1 && numOutputChannels > 1)
        {
            outBus.copyFrom(0, 0, inBus, 0, 0, numSamples);
            outBus.copyFrom(1, 0, inBus, 0, 0, numSamples);
        }
        // Otherwise, perform a standard stereo copy.
        else
        {
            const int channelsToCopy = juce::jmin(numInputChannels, numOutputChannels);
            for (int ch = 0; ch < channelsToCopy; ++ch)
            {
                outBus.copyFrom(ch, 0, inBus, ch, 0, numSamples);
            }
        }
    }
    else
    {
        // If no input is connected, ensure the output is silent.
        // ✅ Safe to clear here - we've already read CV inputs above
        outBus.clear();
    }

    // If bit depth is at maximum and sample rate is at maximum and mix is fully dry, we can skip processing entirely.
    if (baseBitDepth >= 23.99f && baseSampleRate >= 0.999f && mixAmount <= 0.001f)
    {
        // Update output values for tooltips
        if (lastOutputValues.size() >= 2)
        {
            if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
            if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
        }
        return;
    }

    // --- Dry/Wet Mix Implementation (inspired by DriveModuleProcessor) ---
    // 1. Make a copy of the original (dry) signal.
    tempBuffer.makeCopyOf(outBus);

    // Create temporary single-sample buffers for anti-aliasing filter processing (per channel)
    juce::AudioBuffer<float> sampleBufferL(1, 1);
    juce::AudioBuffer<float> sampleBufferR(1, 1);
    juce::dsp::AudioBlock<float> blockL(sampleBufferL);
    juce::dsp::AudioBlock<float> blockR(sampleBufferR);
    juce::dsp::ProcessContextReplacing<float> contextL(blockL);
    juce::dsp::ProcessContextReplacing<float> contextR(blockR);

    // Process each channel
    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = outBus.getWritePointer(ch);
        const float* dryData = tempBuffer.getReadPointer(ch);
        
        // Per-channel decimator state
        float& srCounter = (ch == 0) ? mSrCounterL : mSrCounterR;
        float& lastSample = (ch == 0) ? mLastSampleL : mLastSampleR;
        
        // Per-channel quantization error state
        float& quantError = (ch == 0) ? mQuantErrorL : mQuantErrorR;
        
        // Select the correct anti-aliasing filter and context for this channel
        auto& antiAliasFilter = (ch == 0) ? mAntiAliasFilterL : mAntiAliasFilterR;
        auto& sampleBuffer = (ch == 0) ? sampleBufferL : sampleBufferR;
        auto& context = (ch == 0) ? contextL : contextR;
        
        for (int i = 0; i < numSamples; ++i)
        {
            // PER-SAMPLE FIX: Calculate effective bit depth FOR THIS SAMPLE
            float bitDepth = baseBitDepth;
            if (isBitDepthMod && bitDepthCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, bitDepthCV[i]);
                if (relativeBitDepthMode) {
                    // RELATIVE: ±12 bits around base (e.g., base 16 -> range 4-28, clamped to 1-24)
                    const float bitRange = 12.0f;
                    const float bitOffset = (cv - 0.5f) * (bitRange * 2.0f);
                    bitDepth = baseBitDepth + bitOffset;
                } else {
                    // ABSOLUTE: CV directly sets bit depth (1-24)
                    bitDepth = juce::jmap(cv, 1.0f, 24.0f);
                }
                bitDepth = juce::jlimit(1.0f, 24.0f, bitDepth);
            }
            
            // Apply smoothing to bit depth to prevent zipper noise
            mBitDepthSm.setTargetValue(bitDepth);
            bitDepth = mBitDepthSm.getNextValue();
            
            // PER-SAMPLE FIX: Calculate effective sample rate FOR THIS SAMPLE
            float sampleRate = baseSampleRate;
            if (isSampleRateMod && sampleRateCV != nullptr) {
                const float cv = juce::jlimit(0.0f, 1.0f, sampleRateCV[i]);
                if (relativeSampleRateMode) {
                    // RELATIVE: ±3 octaves (0.125x to 8x, clamped to 0.1x-1.0x)
                    const float octaveRange = 3.0f;
                    const float octaveOffset = (cv - 0.5f) * (octaveRange * 2.0f);
                    sampleRate = baseSampleRate * std::pow(2.0f, octaveOffset);
                } else {
                    // ABSOLUTE: CV directly sets sample rate (0.1-1.0)
                    sampleRate = juce::jmap(cv, 0.1f, 1.0f);
                }
                sampleRate = juce::jlimit(0.1f, 1.0f, sampleRate);
            }
            
            // Apply smoothing to sample rate to prevent clicks
            mSampleRateSm.setTargetValue(sampleRate);
            sampleRate = mSampleRateSm.getNextValue();
            
            // Determine effective anti-aliasing state for this sample
            bool isAntiAliasOn = baseAntiAlias;
            if (isAntiAliasMod && antiAliasCV != nullptr) {
                isAntiAliasOn = (antiAliasCV[i] > 0.5f);
            }
            
            // Determine effective quantization mode for this sample
            int quantMode = baseQuantMode;
            if (isQuantModeMod && quantModeCV != nullptr) {
                quantMode = static_cast<int>(juce::jmap(juce::jlimit(0.0f, 1.0f, quantModeCV[i]), 0.0f, 2.99f));
            }
            
            // Get the sample *before* decimation
            float sampleToDecimate = data[i];
            
            // Update the anti-aliasing filter cutoff based on the *current* smoothed sample rate
            antiAliasFilter.setCutoffFrequency(sampleRate * (getSampleRate() * 0.45f));
            
            // Conditionally filter the sample using ProcessContextReplacing (like VCFModuleProcessor)
            if (isAntiAliasOn) {
                sampleBuffer.setSample(0, 0, sampleToDecimate);
                antiAliasFilter.process(context);
                sampleToDecimate = sampleBuffer.getSample(0, 0);
            }
            
            // Now, perform the sample-and-hold on the (potentially filtered) sample
            srCounter += sampleRate;
            if (srCounter >= 1.0f)
            {
                srCounter -= 1.0f;
                lastSample = sampleToDecimate;
            }
            float decimatedSample = lastSample;
            
            // --- New Quantization Logic ---
            float numLevels = std::exp2(bitDepth);
            float step = 2.0f / (numLevels - 1.0f); // Step size for [-1, 1] range
            
            float dither = 0.0f;
            float sampleToQuantize = decimatedSample;
            
            switch (quantMode)
            {
                case 1: // Dither (TPDF)
                    dither = (mRandom.nextFloat() - mRandom.nextFloat()) * 0.5f * step;
                    quantError = 0.0f; // Reset error if switching
                    break;
                case 2: // Noise Shaping
                    dither = (mRandom.nextFloat() - mRandom.nextFloat()) * 0.5f * step;
                    sampleToQuantize += quantError; // Add previous sample's error
                    break;
                case 0: // Linear
                default:
                    quantError = 0.0f; // Reset error
                    break;
            }
            
            // Quantize using floor-rounding
            float quantizedSample = std::floor((sampleToQuantize + dither) / step + 0.5f) * step;
            quantizedSample = juce::jlimit(-1.0f, 1.0f, quantizedSample);
            
            // If noise shaping, calculate and store the error for the *next* sample
            if (quantMode == 2)
            {
                float error = sampleToQuantize - quantizedSample;
                quantError = error * 0.95f; // Simple 1-pole high-pass on error
            }
            // --- End of New Quantization Logic ---
            
            // Apply dry/wet mix
            const float dryLevel = 1.0f - mixAmount;
            const float wetLevel = mixAmount;
            data[i] = dryData[i] * dryLevel + quantizedSample * wetLevel;
            
            // Update telemetry (throttled - every 64 samples as per BestPracticeNodeProcessor.md)
            if ((i & 0x3F) == 0) {
                setLiveParamValue("bit_depth_live", bitDepth);
                setLiveParamValue("sample_rate_live", sampleRate);
                setLiveParamValue("antiAlias_live", isAntiAliasOn ? 1.0f : 0.0f);
                setLiveParamValue("quant_mode_live", static_cast<float>(quantMode));
            }
        }
    }
    
    // Update output values for tooltips
    if (lastOutputValues.size() >= 2)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, buffer.getNumSamples() - 1));
        if (lastOutputValues[1] && numChannels > 1) lastOutputValues[1]->store(outBus.getSample(1, buffer.getNumSamples() - 1));
    }
}

bool BitCrusherModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    
    // Use virtual _mod IDs as per BestPracticeNodeProcessor.md
    if (paramId == paramIdBitDepthMod) { outChannelIndexInBus = 2; return true; }
    if (paramId == paramIdSampleRateMod) { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdAntiAliasMod) { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdQuantModeMod) { outChannelIndexInBus = 5; return true; }
    return false;
}

juce::String BitCrusherModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "In L";
        case 1: return "In R";
        case 2: return "Bit Depth Mod";
        case 3: return "Sample Rate Mod";
        case 4: return "Anti-Alias Mod";
        case 5: return "Quant Mode Mod";
        default: return juce::String("In ") + juce::String(channel + 1);
    }
}

juce::String BitCrusherModuleProcessor::getAudioOutputLabel(int channel) const
{
    if (channel == 0) return "Out L";
    if (channel == 1) return "Out R";
    return {};
}

#if defined(PRESET_CREATOR_UI)
void BitCrusherModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    auto& ap = getAPVTS();
    ImGui::PushItemWidth(itemWidth);

    auto HelpMarker = [](const char* desc) {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) { ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f); ImGui::TextUnformatted(desc); ImGui::PopTextWrapPos(); ImGui::EndTooltip(); }
    };

    ThemeText("Bit Crusher Parameters", theme.text.section_header);
    ImGui::Spacing();

    // Bit Depth
    // Use virtual _mod ID to check for modulation as per BestPracticeNodeProcessor.md
    bool isBitDepthModulated = isParamModulated(paramIdBitDepthMod);
    // Get live value if modulated, otherwise use base parameter value
    float bitDepth = isBitDepthModulated 
        ? getLiveParamValueFor(paramIdBitDepthMod, "bit_depth_live", bitDepthParam != nullptr ? bitDepthParam->load() : 16.0f)
        : (bitDepthParam != nullptr ? bitDepthParam->load() : 16.0f);
    if (isBitDepthModulated) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Bit Depth", &bitDepth, 1.0f, 24.0f, "%.2f bits", ImGuiSliderFlags_Logarithmic)) {
        if (!isBitDepthModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdBitDepth))) *p = bitDepth;
        }
    }
    if (!isBitDepthModulated) adjustParamOnWheel(ap.getParameter(paramIdBitDepth), paramIdBitDepth, bitDepth);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isBitDepthModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Bit depth reduction (1-24 bits)\nLower values create more quantization artifacts\nLogarithmic scale for fine control");

    // Sample Rate
    // Use virtual _mod ID to check for modulation as per BestPracticeNodeProcessor.md
    bool isSampleRateModulated = isParamModulated(paramIdSampleRateMod);
    // Get live value if modulated, otherwise use base parameter value
    float sampleRate = isSampleRateModulated
        ? getLiveParamValueFor(paramIdSampleRateMod, "sample_rate_live", sampleRateParam != nullptr ? sampleRateParam->load() : 1.0f)
        : (sampleRateParam != nullptr ? sampleRateParam->load() : 1.0f);
    if (isSampleRateModulated) {
        ImGui::BeginDisabled();
    }
    if (ImGui::SliderFloat("Sample Rate", &sampleRate, 0.1f, 1.0f, "%.3fx", ImGuiSliderFlags_Logarithmic)) {
        if (!isSampleRateModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdSampleRate))) *p = sampleRate;
        }
    }
    if (!isSampleRateModulated) adjustParamOnWheel(ap.getParameter(paramIdSampleRate), paramIdSampleRate, sampleRate);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isSampleRateModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Sample rate reduction (0.1x-1.0x)\nLower values create more aliasing and stuttering\n1.0x = full rate, 0.1x = 10% of original rate");

    // Mix
    float mix = mixParam != nullptr ? mixParam->load() : 1.0f;
    if (ImGui::SliderFloat("Mix", &mix, 0.0f, 1.0f, "%.2f")) {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdMix))) *p = mix;
    }
    adjustParamOnWheel(ap.getParameter(paramIdMix), paramIdMix, mix);
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    ImGui::SameLine();
    HelpMarker("Dry/wet mix (0-1)\n0 = clean, 1 = fully crushed");

    ImGui::Spacing();

    // Anti-Aliasing
    // Use virtual _mod ID to check for modulation as per BestPracticeNodeProcessor.md
    bool isAntiAliasModulated = isParamModulated(paramIdAntiAliasMod);
    // Get live value if modulated, otherwise use base parameter value
    bool antiAlias = isAntiAliasModulated
        ? (getLiveParamValueFor(paramIdAntiAliasMod, "antiAlias_live", antiAliasParam != nullptr && antiAliasParam->load() > 0.5f ? 1.0f : 0.0f) > 0.5f)
        : (antiAliasParam != nullptr && antiAliasParam->load() > 0.5f);
    if (isAntiAliasModulated) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Checkbox("Anti-Aliasing", &antiAlias)) {
        if (!isAntiAliasModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter(paramIdAntiAlias))) *p = antiAlias;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isAntiAliasModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("ON: Applies a low-pass filter before decimation to reduce aliasing.\nOFF: Disables the filter for a harsher, classic aliased sound.");

    // Quant Mode
    // Use virtual _mod ID to check for modulation as per BestPracticeNodeProcessor.md
    bool isQuantModeModulated = isParamModulated(paramIdQuantModeMod);
    // Get live value if modulated, otherwise use base parameter value
    int quantMode = isQuantModeModulated
        ? static_cast<int>(getLiveParamValueFor(paramIdQuantModeMod, "quant_mode_live", quantModeParam != nullptr ? quantModeParam->load() : 0.0f))
        : (quantModeParam != nullptr ? static_cast<int>(quantModeParam->load()) : 0);
    if (isQuantModeModulated) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Combo("Quant Mode", &quantMode, "Linear\0Dither (TPDF)\0Noise Shaping\0\0")) {
        if (!isQuantModeModulated) {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(ap.getParameter(paramIdQuantMode))) *p = quantMode;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
    if (isQuantModeModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }
    ImGui::SameLine();
    HelpMarker("Quantization Algorithm:\nLinear: Basic, harsh quantization.\nDither: Adds noise to reduce artifacts.\nNoise Shaping: Pushes quantization noise into higher, less audible frequencies.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === RELATIVE MODULATION SECTION ===
    ThemeText("CV Input Modes", theme.modulation.frequency);
    ImGui::Spacing();
    
    // Relative Bit Depth Mod checkbox
    bool relativeBitDepthMod = relativeBitDepthModParam != nullptr && relativeBitDepthModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Bit Depth Mod", &relativeBitDepthMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeBitDepthMod")))
            *p = relativeBitDepthMod;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±12 bits)\nOFF: CV directly sets bit depth (1-24)");
    }

    // Relative Sample Rate Mod checkbox
    bool relativeSampleRateMod = relativeSampleRateModParam != nullptr && relativeSampleRateModParam->load() > 0.5f;
    if (ImGui::Checkbox("Relative Sample Rate Mod", &relativeSampleRateMod))
    {
        if (auto* p = dynamic_cast<juce::AudioParameterBool*>(ap.getParameter("relativeSampleRateMod")))
            *p = relativeSampleRateMod;
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ON: CV modulates around slider (±3 octaves)\nOFF: CV directly sets sample rate (0.1x-1.0x)");
    }

    ImGui::PopItemWidth();
}

void BitCrusherModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("In L", 0);
    helpers.drawAudioInputPin("In R", 1);
    
    // Modulation pins - use virtual _mod IDs as per BestPracticeNodeProcessor.md
    int busIdx, chanInBus;
    if (getParamRouting(paramIdBitDepthMod, busIdx, chanInBus))
        helpers.drawAudioInputPin("Bit Depth Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting(paramIdSampleRateMod, busIdx, chanInBus))
        helpers.drawAudioInputPin("Sample Rate Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting(paramIdAntiAliasMod, busIdx, chanInBus))
        helpers.drawAudioInputPin("Anti-Alias Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    if (getParamRouting(paramIdQuantModeMod, busIdx, chanInBus))
        helpers.drawAudioInputPin("Quant Mode Mod", getChannelIndexInProcessBlockBuffer(true, busIdx, chanInBus));
    
    helpers.drawAudioOutputPin("Out L", 0);
    helpers.drawAudioOutputPin("Out R", 1);
}
#endif

