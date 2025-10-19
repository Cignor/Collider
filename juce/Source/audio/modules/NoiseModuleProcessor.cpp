#include "NoiseModuleProcessor.h"

// --- Parameter Layout Definition ---
juce::AudioProcessorValueTreeState::ParameterLayout NoiseModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        paramIdColour, "Colour", juce::StringArray{ "White", "Pink", "Brown" }, 0));
        
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdLevel, "Level dB", juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), -12.0f));

    return { params.begin(), params.end() };
}

// --- Constructor ---
NoiseModuleProcessor::NoiseModuleProcessor()
    : ModuleProcessor(BusesProperties()
        .withInput("Modulation", juce::AudioChannelSet::discreteChannels(2), true) // ch0: Level, ch1: Colour
        .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      apvts(*this, nullptr, "NoiseParams", createParameterLayout())
{
    levelDbParam = apvts.getRawParameterValue(paramIdLevel);
    colourParam = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramIdColour));

    // Initialize output value tracking for tooltips
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
}

// --- Audio Processing Setup ---
void NoiseModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{ sampleRate, (juce::uint32)samplesPerBlock, 2 };

    // Pink noise is ~-3dB/octave. A simple 1-pole low-pass can approximate this.
    pinkFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(sampleRate, 1000.0);
    
    // Brown noise is ~-6dB/octave. A stronger low-pass.
    brownFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeFirstOrderLowPass(sampleRate, 250.0);
    
    pinkFilter.prepare(spec);
    brownFilter.prepare(spec);
    pinkFilter.reset();
    brownFilter.reset();
}

// --- Main Audio Processing Block ---
void NoiseModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused(midi);

    auto modInBus = getBusBuffer(buffer, true, 0);
    auto outBus = getBusBuffer(buffer, false, 0);

    const int numSamples = buffer.getNumSamples();

    // --- Get Modulation CVs ---
    // CORRECTED: Use the _mod parameter IDs to check for connections
    const bool isLevelModulated = isParamInputConnected(paramIdLevelMod);
    const bool isColourModulated = isParamInputConnected(paramIdColourMod);

    const float* levelCV = isLevelModulated && modInBus.getNumChannels() > 0 ? modInBus.getReadPointer(0) : nullptr;
    const float* colourCV = isColourModulated && modInBus.getNumChannels() > 1 ? modInBus.getReadPointer(1) : nullptr;

    // --- Get Base Parameter Values ---
    const float baseLevelDb = levelDbParam->load();
    const int baseColour = colourParam->getIndex();

    // --- Per-Sample Processing for Responsive Modulation ---
    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Calculate effective parameter values for this sample
        float effectiveLevelDb = baseLevelDb;
        if (isLevelModulated && levelCV != nullptr) {
            // CV maps 0..1 to the full dB range
            effectiveLevelDb = juce::jmap(levelCV[i], 0.0f, 1.0f, -60.0f, 6.0f);
        }

        int effectiveColour = baseColour;
        if (isColourModulated && colourCV != nullptr) {
            // CV maps 0..1 to the 3 choices
            effectiveColour = static_cast<int>(juce::jlimit(0.0f, 1.0f, colourCV[i]) * 2.99f);
        }

        // 2. Generate raw white noise
        float sample = random.nextFloat() * 2.0f - 1.0f;

        // 3. Filter noise based on effective colour
        switch (effectiveColour)
        {
            case 0: /* White noise, no filter */ break;
            case 1: sample = pinkFilter.processSample(sample); break;
            case 2: sample = brownFilter.processSample(sample); break;
        }

        // 4. Apply gain
        sample *= juce::Decibels::decibelsToGain(effectiveLevelDb);

        // 5. Write to mono output
        outBus.setSample(0, i, sample);

        // 6. Update telemetry for UI (throttled)
        if ((i & 0x3F) == 0) // Every 64 samples
        {
            setLiveParamValue("level_live", effectiveLevelDb);
            setLiveParamValue("colour_live", (float)effectiveColour);
        }
    }

    // --- Update Inspector Values ---
    if (lastOutputValues.size() >= 1)
    {
        if (lastOutputValues[0]) lastOutputValues[0]->store(outBus.getSample(0, numSamples - 1));
    }
}

bool NoiseModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation inputs are on the first bus
    
    // CORRECTED: Map the virtual _mod IDs to physical channels
    if (paramId == paramIdLevelMod)  { outChannelIndexInBus = 0; return true; }
    if (paramId == paramIdColourMod) { outChannelIndexInBus = 1; return true; }
    
    return false;
}

#if defined(PRESET_CREATOR_UI)
// --- UI Drawing Logic ---

void NoiseModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    auto& ap = getAPVTS();

    // --- Get values for display, checking for modulation ---
    // CORRECTED: Use the _mod parameter IDs to check for modulation
    bool levelIsModulated = isParamModulated(paramIdLevelMod);
    float levelDb = levelIsModulated ? getLiveParamValueFor(paramIdLevelMod, "level_live", levelDbParam->load()) : levelDbParam->load();

    bool colourIsModulated = isParamModulated(paramIdColourMod);
    int colourIndex = colourIsModulated ? (int)getLiveParamValueFor(paramIdColourMod, "colour_live", (float)colourParam->getIndex()) : colourParam->getIndex();

    ImGui::PushItemWidth(itemWidth);

    // --- Colour Dropdown ---
    if (colourIsModulated) ImGui::BeginDisabled();
    if (ImGui::Combo("Colour", &colourIndex, "White\0Pink\0Brown\0\0"))
    {
        if (!colourIsModulated) *colourParam = colourIndex;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !colourIsModulated) { onModificationEnded(); }
    if (colourIsModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    // --- Level Slider ---
    if (levelIsModulated) ImGui::BeginDisabled();
    if (ImGui::SliderFloat("Level dB", &levelDb, -60.0f, 6.0f, "%.3f"))
    {
        if (!levelIsModulated) *dynamic_cast<juce::AudioParameterFloat*>(ap.getParameter(paramIdLevel)) = levelDb;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && !levelIsModulated) { onModificationEnded(); }
    if (!levelIsModulated) adjustParamOnWheel(ap.getParameter(paramIdLevel), "level", levelDb);
    if (levelIsModulated) { ImGui::EndDisabled(); ImGui::SameLine(); ImGui::TextUnformatted("(mod)"); }

    ImGui::PopItemWidth();
}

void NoiseModuleProcessor::drawIoPins(const NodePinHelpers& helpers)
{
    helpers.drawAudioInputPin("Level Mod", 0);
    helpers.drawAudioInputPin("Colour Mod", 1);
    helpers.drawAudioOutputPin("Out", 0);
}

// --- Pin Label and Routing Definitions ---

juce::String NoiseModuleProcessor::getAudioInputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Level Mod";
        case 1: return "Colour Mod";
        default: return {};
    }
}

juce::String NoiseModuleProcessor::getAudioOutputLabel(int channel) const
{
    switch (channel)
    {
        case 0: return "Out";
        default: return {};
    }
}
#endif