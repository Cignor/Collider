#include "PatchGenerator.h"
#include "PinDatabase.h"
#include <juce_core/juce_core.h>

// Static member definition
std::map<juce::uint32, juce::Point<float>> PatchGenerator::nodePositions;

void PatchGenerator::generate(ModularSynthProcessor* synth, PatchArchetype type, float chaosAmount)
{
    if (!synth)
        return;

    // Clear existing patch
    synth->clearAll();

    // Clear node positions
    nodePositions.clear();

    switch (type)
    {
    case PatchArchetype::EastCoast:
        generateEastCoast(synth, chaosAmount);
        break;
    case PatchArchetype::WestCoast:
        generateWestCoast(synth, chaosAmount);
        break;
    case PatchArchetype::AmbientDrone:
        generateAmbient(synth, chaosAmount);
        break;
    case PatchArchetype::TechnoBass:
        generateTechnoBass(synth, chaosAmount);
        break;
    case PatchArchetype::Glitch:
        generateGlitch(synth, chaosAmount);
        break;
    case PatchArchetype::Ethereal:
        generateEthereal(synth, chaosAmount);
        break;
    case PatchArchetype::AcidLead:
        generateAcidLead(synth, chaosAmount);
        break;
    case PatchArchetype::Pluck:
        generatePluck(synth, chaosAmount);
        break;
    case PatchArchetype::WarmPad:
        generateWarmPad(synth, chaosAmount);
        break;
    case PatchArchetype::DeepBass:
        generateDeepBass(synth, chaosAmount);
        break;
    case PatchArchetype::BrightLead:
        generateBrightLead(synth, chaosAmount);
        break;
    case PatchArchetype::Arpeggio:
        generateArpeggio(synth, chaosAmount);
        break;
    case PatchArchetype::Percussion:
        generatePercussion(synth, chaosAmount);
        break;
    case PatchArchetype::ChordProg:
        generateChordProg(synth, chaosAmount);
        break;
    case PatchArchetype::NoiseSweep:
        generateNoiseSweep(synth, chaosAmount);
        break;
    case PatchArchetype::FM:
        generateFM(synth, chaosAmount);
        break;
    case PatchArchetype::Granular:
        generateGranular(synth, chaosAmount);
        break;
    case PatchArchetype::DelayLoop:
        generateDelayLoop(synth, chaosAmount);
        break;
    case PatchArchetype::ReverbWash:
        generateReverbWash(synth, chaosAmount);
        break;
    case PatchArchetype::Distorted:
        generateDistorted(synth, chaosAmount);
        break;
    case PatchArchetype::WobbleBass:
        generateWobbleBass(synth, chaosAmount);
        break;
    case PatchArchetype::Stutter:
        generateStutter(synth, chaosAmount);
        break;
    case PatchArchetype::Harmonic:
        generateHarmonic(synth, chaosAmount);
        break;
    case PatchArchetype::Minimal:
        generateMinimal(synth, chaosAmount);
        break;
    case PatchArchetype::Complex:
        generateComplex(synth, chaosAmount);
        break;
    case PatchArchetype::Experimental:
        generateExperimental(synth, chaosAmount);
        break;
    case PatchArchetype::Random:
    {
        juce::Random rng;
        int          pick = rng.nextInt(26); // 6 original + 20 new
        switch (pick)
        {
        case 0: generateEastCoast(synth, chaosAmount); break;
        case 1: generateWestCoast(synth, chaosAmount); break;
        case 2: generateAmbient(synth, chaosAmount); break;
        case 3: generateTechnoBass(synth, chaosAmount); break;
        case 4: generateGlitch(synth, chaosAmount); break;
        case 5: generateEthereal(synth, chaosAmount); break;
        case 6: generateAcidLead(synth, chaosAmount); break;
        case 7: generatePluck(synth, chaosAmount); break;
        case 8: generateWarmPad(synth, chaosAmount); break;
        case 9: generateDeepBass(synth, chaosAmount); break;
        case 10: generateBrightLead(synth, chaosAmount); break;
        case 11: generateArpeggio(synth, chaosAmount); break;
        case 12: generatePercussion(synth, chaosAmount); break;
        case 13: generateChordProg(synth, chaosAmount); break;
        case 14: generateNoiseSweep(synth, chaosAmount); break;
        case 15: generateFM(synth, chaosAmount); break;
        case 16: generateGranular(synth, chaosAmount); break;
        case 17: generateDelayLoop(synth, chaosAmount); break;
        case 18: generateReverbWash(synth, chaosAmount); break;
        case 19: generateDistorted(synth, chaosAmount); break;
        case 20: generateWobbleBass(synth, chaosAmount); break;
        case 21: generateStutter(synth, chaosAmount); break;
        case 22: generateHarmonic(synth, chaosAmount); break;
        case 23: generateMinimal(synth, chaosAmount); break;
        case 24: generateComplex(synth, chaosAmount); break;
        case 25: generateExperimental(synth, chaosAmount); break;
        }
        break;
    }
    }

    // Commit all changes to ensure connections are properly established
    synth->commitChanges();
}

juce::uint32 PatchGenerator::addModule(
    ModularSynthProcessor* synth,
    const juce::String&    type,
    float                  x,
    float                  y)
{
    if (!synth)
    {
        juce::Logger::writeToLog("[PatchGenerator] Cannot add module: synth is null");
        return 0;
    }
    
    auto node = synth->addModule(type);
    if (node != juce::AudioProcessorGraph::NodeID{})
    {
        auto logicalId = synth->getLogicalIdForNode(node);
        // Store position for later retrieval by UI component
        nodePositions[logicalId] = juce::Point<float>(x, y);
        return logicalId;
    }
    else
    {
        juce::Logger::writeToLog("[PatchGenerator] Failed to create module of type: " + type);
    }
    return 0;
}

void PatchGenerator::connect(
    ModularSynthProcessor* synth,
    juce::uint32           sourceId,
    int                    sourcePin,
    juce::uint32           destId,
    int                    destPin)
{
    // Validate source ID
    if (sourceId == 0)
        return;

    // Convert source logical ID to NodeID
    auto sourceNodeId = synth->getNodeIdForLogical(sourceId);
    if (sourceNodeId == juce::AudioProcessorGraph::NodeID{})
        return;

    // Handle output node connection (output node has logical ID 0)
    if (destId == 0)
    {
        // Connect directly to output node using its NodeID
        auto outputNodeId = synth->getOutputNodeID();
        if (outputNodeId != juce::AudioProcessorGraph::NodeID{})
        {
            bool success = synth->connect(sourceNodeId, sourcePin, outputNodeId, destPin);
            if (!success)
            {
                juce::Logger::writeToLog(
                    "[PatchGenerator] Failed to connect module " + juce::String(sourceId) +
                    " to output node");
            }
        }
    }
    else
    {
        // Convert destination logical ID to NodeID
        auto destNodeId = synth->getNodeIdForLogical(destId);
        if (destNodeId != juce::AudioProcessorGraph::NodeID{})
        {
            synth->connect(sourceNodeId, sourcePin, destNodeId, destPin);
        }
    }
}

void PatchGenerator::setParam(
    ModularSynthProcessor* synth,
    juce::uint32           moduleId,
    const juce::String&    paramId,
    float                  value)
{
    safeSetParam(synth, moduleId, paramId, value);
}

// --- Pin/Parameter Query Helpers ---

int PatchGenerator::findPinIndex(const juce::String& moduleType, const juce::String& pinName, bool isOutput)
{
    auto& db = getModulePinDatabase();
    auto it = db.find(moduleType.toLowerCase());
    if (it == db.end())
    {
        juce::Logger::writeToLog("[PatchGenerator] Module type '" + moduleType + "' not found in PinDatabase");
        return -1;
    }
    
    const ModulePinInfo& info = it->second;
    const std::vector<AudioPin>& pins = isOutput ? info.audioOuts : info.audioIns;
    
    for (const auto& pin : pins)
    {
        if (pin.name.equalsIgnoreCase(pinName))
        {
            return pin.channel;
        }
    }
    
    juce::Logger::writeToLog(
        "[PatchGenerator] Pin '" + pinName + "' not found in " + (isOutput ? "output" : "input") +
        " pins of module '" + moduleType + "'");
    return -1;
}

bool PatchGenerator::paramExists(ModularSynthProcessor* synth, juce::uint32 moduleId, const juce::String& paramId)
{
    if (!synth || moduleId == 0)
        return false;
    
    auto* processor = synth->getModuleForLogical(moduleId);
    if (!processor)
        return false;
    
    auto& apvts = processor->getAPVTS();
    return apvts.getParameter(paramId) != nullptr;
}

// --- Safe Connection/Parameter Setting ---

bool PatchGenerator::safeConnect(
    ModularSynthProcessor* synth,
    juce::uint32           sourceId,
    const juce::String&    sourcePinName,
    juce::uint32           destId,
    const juce::String&    destPinName)
{
    if (!synth || sourceId == 0)
        return false;
    
    // Get module types for pin lookup
    juce::String sourceType = synth->getModuleTypeForLogical(sourceId);
    juce::String destType;
    
    if (destId == 0)
    {
        // Output node - use special handling
        // Output node typically has standard audio pins (0=L, 1=R)
        int sourcePin = findPinIndex(sourceType, sourcePinName, true);
        if (sourcePin < 0)
            return false;
        
        // For output, we'll use pin 0 for left, 1 for right
        // If destPinName is "Out L" or "Out R", parse it, otherwise default to 0
        int destPin = 0;
        if (destPinName.equalsIgnoreCase("Out R") || destPinName.equalsIgnoreCase("Right"))
            destPin = 1;
        
        return safeConnect(synth, sourceId, sourcePin, destId, destPin);
    }
    else
    {
        destType = synth->getModuleTypeForLogical(destId);
    }
    
    if (sourceType.isEmpty() || (destId != 0 && destType.isEmpty()))
    {
        juce::Logger::writeToLog("[PatchGenerator] Could not determine module type for connection");
        return false;
    }
    
    int sourcePin = findPinIndex(sourceType, sourcePinName, true);
    int destPin = findPinIndex(destType, destPinName, false);
    
    if (sourcePin < 0 || destPin < 0)
        return false;
    
    return safeConnect(synth, sourceId, sourcePin, destId, destPin);
}

bool PatchGenerator::safeConnect(
    ModularSynthProcessor* synth,
    juce::uint32           sourceId,
    int                    sourcePin,
    juce::uint32           destId,
    int                    destPin)
{
    if (!synth || sourceId == 0)
    {
        juce::Logger::writeToLog("[PatchGenerator] Invalid source ID for connection");
        return false;
    }
    
    // Convert source logical ID to NodeID
    auto sourceNodeId = synth->getNodeIdForLogical(sourceId);
    if (sourceNodeId == juce::AudioProcessorGraph::NodeID{})
    {
        juce::Logger::writeToLog("[PatchGenerator] Source module " + juce::String(sourceId) + " not found");
        return false;
    }
    
    // Handle output node connection (output node has logical ID 0)
    if (destId == 0)
    {
        auto outputNodeId = synth->getOutputNodeID();
        if (outputNodeId == juce::AudioProcessorGraph::NodeID{})
        {
            juce::Logger::writeToLog("[PatchGenerator] Output node not found");
            return false;
        }
        
        bool success = synth->connect(sourceNodeId, sourcePin, outputNodeId, destPin);
        if (!success)
        {
            juce::Logger::writeToLog(
                "[PatchGenerator] Failed to connect module " + juce::String(sourceId) + " pin " +
                juce::String(sourcePin) + " to output node pin " + juce::String(destPin));
        }
        return success;
    }
    else
    {
        auto destNodeId = synth->getNodeIdForLogical(destId);
        if (destNodeId == juce::AudioProcessorGraph::NodeID{})
        {
            juce::Logger::writeToLog("[PatchGenerator] Destination module " + juce::String(destId) + " not found");
            return false;
        }
        
        bool success = synth->connect(sourceNodeId, sourcePin, destNodeId, destPin);
        if (!success)
        {
            juce::Logger::writeToLog(
                "[PatchGenerator] Failed to connect module " + juce::String(sourceId) + " pin " +
                juce::String(sourcePin) + " to module " + juce::String(destId) + " pin " +
                juce::String(destPin));
        }
        return success;
    }
}

bool PatchGenerator::safeSetParam(
    ModularSynthProcessor* synth,
    juce::uint32           moduleId,
    const juce::String&    paramId,
    float                  value)
{
    if (!synth || moduleId == 0)
    {
        juce::Logger::writeToLog("[PatchGenerator] Invalid module ID for setParam");
        return false;
    }
    
    auto* processor = synth->getModuleForLogical(moduleId);
    if (!processor)
    {
        juce::Logger::writeToLog("[PatchGenerator] Module " + juce::String(moduleId) + " not found for setParam");
        return false;
    }
    
    auto& apvts = processor->getAPVTS();
    auto* param = apvts.getParameter(paramId);
    if (!param)
    {
        juce::Logger::writeToLog(
            "[PatchGenerator] Parameter '" + paramId + "' not found in module " + juce::String(moduleId));
        return false;
    }
    
    param->setValueNotifyingHost(value);
    return true;
}

// --- RECIPES ---

void PatchGenerator::generateEastCoast(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating East Coast patch (chaos: " + juce::String(chaos, 2) + ")");

    // Simplified layout - beautify will fix positioning
    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- 1. Modules ---
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    x += spacing;
    
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    x += spacing;
    
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    // Control modules
    x = 0.0f;
    y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    
    auto adsrAmp = addModule(synth, "adsr", x, y);
    if (adsrAmp == 0) return;
    x += spacing;
    
    auto adsrFilter = addModule(synth, "adsr", x, y);
    if (adsrFilter == 0) return;
    x += spacing;
    
    auto comp1 = addModule(synth, "comparator", x, y);
    if (comp1 == 0) return;
    x += spacing;
    
    auto comp2 = addModule(synth, "comparator", x, y);
    if (comp2 == 0) return;
    
    y = 600.0f;
    x = 0.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;

    const juce::uint32 out = 0; // Output node

    // --- 2. Parameters ---
    // VCOs
    safeSetParam(synth, vco1, "waveform", 0.0f); // Saw
    safeSetParam(synth, vco2, "waveform", 0.25f); // Square
    safeSetParam(synth, vco2, "detune", 0.505f + (rng.nextFloat() * 0.02f * chaos));

    // VCF - use chaos to vary
    safeSetParam(synth, vcf, "cutoff", 0.3f + (rng.nextFloat() * 0.3f * chaos));
    safeSetParam(synth, vcf, "res", 0.2f + (rng.nextFloat() * 0.5f * chaos));

    // VCA - Set to +6dB max output (no gain mod on final VCA)
    // Normalized value 1.0f = +6dB (max), 0.0f = -60dB (min)
    safeSetParam(synth, vca, "gain", 1.0f); // +6dB output (full volume!)

    // Sequencer
    safeSetParam(synth, seq, "numSteps", 8.0f);
    safeSetParam(synth, seq, "rate", 2.0f + rng.nextInt(3)); // 2-4 Hz

    for (int i = 1; i <= 8; ++i)
    {
        safeSetParam(synth, seq, "step" + juce::String(i), rng.nextFloat());
        safeSetParam(synth, seq, "step" + juce::String(i) + "_gate", rng.nextFloat() > 0.3f ? 1.0f : 0.0f);
    }

    // Comparators
    safeSetParam(synth, comp1, "threshold", 0.4f + (rng.nextFloat() * 0.3f));
    safeSetParam(synth, comp2, "threshold", 0.7f + (rng.nextFloat() * 0.2f));

    // ADSRs
    safeSetParam(synth, adsrFilter, "attack", 0.01f);
    safeSetParam(synth, adsrFilter, "decay", 0.2f + (rng.nextFloat() * 0.3f));
    safeSetParam(synth, adsrFilter, "sustain", 0.0f);
    safeSetParam(synth, adsrFilter, "release", 0.1f);

    safeSetParam(synth, adsrAmp, "attack", 0.01f);
    safeSetParam(synth, adsrAmp, "decay", 0.2f + (rng.nextFloat() * 0.2f));
    safeSetParam(synth, adsrAmp, "sustain", 0.6f);
    safeSetParam(synth, adsrAmp, "release", 0.2f);

    // LFO
    safeSetParam(synth, lfo, "rate", 0.2f + (rng.nextFloat() * 0.5f * (1.0f + chaos)));

    // --- 3. Connections ---
    // Audio Path: VCOs -> Mixer -> VCF -> VCA -> Output
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, mixer, "Out L", vcf, "In L");
    safeConnect(synth, mixer, "Out R", vcf, "In R");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    
    // Connect to output (CRITICAL for sound)
    bool outputConnected = safeConnect(synth, vca, "Out L", out, "Out L");
    if (!outputConnected)
    {
        juce::Logger::writeToLog("[PatchGenerator] WARNING: Failed to connect VCA to output! Patch may be silent.");
    }
    safeConnect(synth, vca, "Out R", out, "Out R");

    // Control Path: Sequencer -> VCOs (Pitch)
    safeConnect(synth, seq, "Pitch", vco1, "Frequency");
    safeConnect(synth, seq, "Pitch", vco2, "Frequency");

    // Sequencer Gate -> ADSR Amp (for rhythmic gating, but VCA stays at fixed gain)
    safeConnect(synth, seq, "Gate", adsrAmp, "Gate In");
    
    // Note: Final VCA gain is fixed at +6dB, no gain modulation

    // Complex Logic: Sequencer Nuanced Gate -> Comparators
    safeConnect(synth, seq, "Gate Nuanced", comp1, "In");
    safeConnect(synth, seq, "Gate Nuanced", comp2, "In");

    // Comparator 1 -> ADSR Filter
    safeConnect(synth, comp1, "Out", adsrFilter, "Gate In");

    // ADSR Filter -> VCF Cutoff Mod
    safeConnect(synth, adsrFilter, "Env Out", vcf, "Cutoff Mod");

    // LFO -> VCO2 Waveform Mod (for PWM-like effect)
    safeConnect(synth, lfo, "Out", vco2, "Waveform");

    // --- CHAOS: Add cross-modulation and feedback ---
    if (chaos > 0.3f)
    {
        // LFO modulates sequencer rate
        safeConnect(synth, lfo, "Out", seq, "Rate Mod");
    }
    
    if (chaos > 0.6f)
    {
        // Cross-modulation: VCO2 -> VCO1 Frequency (FM)
        safeConnect(synth, vco2, "Out", vco1, "Frequency");
    }

    juce::Logger::writeToLog("[PatchGenerator] East Coast patch generation complete");
}

void PatchGenerator::generateWestCoast(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating West Coast patch (chaos: " + juce::String(chaos, 2) + ")");

    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- Modules (Krell Topology) ---
    auto carrier = addModule(synth, "vco", x, y);
    if (carrier == 0) return;
    x += spacing;
    
    auto folder = addModule(synth, "waveshaper", x, y);
    if (folder == 0) return;
    x += spacing;
    
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;

    // Control modules
    x = 0.0f;
    y = 300.0f;
    auto func1 = addModule(synth, "function_generator", x, y);
    if (func1 == 0) return;
    x += spacing;
    
    auto func2 = addModule(synth, "function_generator", x, y);
    if (func2 == 0) return;
    x += spacing;
    
    auto comp = addModule(synth, "comparator", x, y);
    if (comp == 0) return;
    
    y = 600.0f;
    x = 0.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;

    const juce::uint32 out = 0;

    // --- Parameters ---
    float freqs[] = {65.41f, 98.00f, 130.81f};
    float root = freqs[rng.nextInt(3)];
    safeSetParam(synth, carrier, "frequency", root);
    safeSetParam(synth, carrier, "waveform", 0.5f); // Triangle

    safeSetParam(synth, folder, "type", 2.0f); // West Coast Folder
    safeSetParam(synth, folder, "drive", 0.2f + (rng.nextFloat() * 0.5f * (1.0f + chaos)));
    safeSetParam(synth, folder, "mix", 1.0f);

    // VCA - Set to +6dB max output (no gain mod on final VCA)
    // Normalized value 1.0f = +6dB (max), 0.0f = -60dB (min)
    safeSetParam(synth, vca, "gain", 1.0f); // +6dB output (full volume!)

    // Function Generators (Krell Engine)
    safeSetParam(synth, func1, "attack", 0.5f + (rng.nextFloat() * 1.0f * (1.0f + chaos)));
    safeSetParam(synth, func1, "decay", 0.5f + (rng.nextFloat() * 1.0f * (1.0f + chaos)));
    safeSetParam(synth, func1, "mode", 0.0f); // AR mode

    safeSetParam(synth, func2, "attack", 0.05f + (rng.nextFloat() * 0.2f));
    safeSetParam(synth, func2, "decay", 0.2f + (rng.nextFloat() * 0.5f));
    safeSetParam(synth, func2, "mode", 0.0f); // AR

    safeSetParam(synth, comp, "threshold", 0.3f + (rng.nextFloat() * 0.4f));

    safeSetParam(synth, lfo, "rate", 0.1f + (rng.nextFloat() * 0.2f * (1.0f + chaos)));

    // --- Connections ---
    // Audio Path: Carrier -> Folder -> VCA -> Output
    safeConnect(synth, carrier, "Out", folder, "In L");
    safeConnect(synth, carrier, "Out", folder, "In R");
    safeConnect(synth, folder, "Out L", vca, "In L");
    safeConnect(synth, folder, "Out R", vca, "In R");
    
    // Connect to output (CRITICAL)
    bool outputConnected = safeConnect(synth, vca, "Out L", out, "Out L");
    if (!outputConnected)
    {
        juce::Logger::writeToLog("[PatchGenerator] WARNING: Failed to connect VCA to output!");
    }
    safeConnect(synth, vca, "Out R", out, "Out R");

    // Krell Logic: Cross-triggering Function Generators
    safeConnect(synth, func1, "End of Cycle", func2, "Trigger In");
    safeConnect(synth, func2, "End of Cycle", func1, "Trigger In");
    
    // Kickstart with LFO
    safeConnect(synth, lfo, "Out", func1, "Trigger In");

    // Modulation
    safeConnect(synth, func1, "Value", folder, "Drive Mod");
    // Note: Final VCA gain is fixed at +6dB, no gain modulation

    // Comparator Logic
    safeConnect(synth, func2, "Value", comp, "In");
    safeConnect(synth, comp, "Out", carrier, "Waveform");

    // LFO Drift
    safeConnect(synth, lfo, "Out", carrier, "Frequency");

    // --- CHAOS ---
    if (chaos > 0.3f)
    {
        safeConnect(synth, lfo, "Out", func1, "Rate Mod");
    }

    if (chaos > 0.6f)
    {
        // Feedback: VCA Out -> Carrier FM
        safeConnect(synth, vca, "Out L", carrier, "Frequency");
    }

    juce::Logger::writeToLog("[PatchGenerator] West Coast patch generation complete");
}

void PatchGenerator::generateAmbient(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Ambient Drone patch (chaos: " + juce::String(chaos, 2) + ")");

    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- Modules ---
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    y += spacing;
    
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    y += spacing;
    
    auto vco3 = addModule(synth, "vco", x, y);
    if (vco3 == 0) return;
    
    x = spacing;
    y = spacing;
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    
    auto delay = addModule(synth, "delay", x, y);
    if (delay == 0) return;
    x += spacing;
    
    auto reverb = addModule(synth, "reverb", x, y);
    if (reverb == 0) return;

    x = spacing;
    y = 600.0f;
    auto lfo1 = addModule(synth, "lfo", x, y);
    if (lfo1 == 0) return;
    x += spacing;
    
    auto lfo2 = addModule(synth, "lfo", x, y);
    if (lfo2 == 0) return;

    const juce::uint32 out = 0;

    // --- Parameters ---
    safeSetParam(synth, vco1, "waveform", 0.5f); // Triangle
    safeSetParam(synth, vco2, "waveform", 0.5f);
    safeSetParam(synth, vco3, "waveform", 0.5f);

    safeSetParam(synth, vco2, "detune", 0.52f + (rng.nextFloat() * 0.02f * chaos));
    safeSetParam(synth, vco3, "detune", 0.48f - (rng.nextFloat() * 0.02f * chaos));

    safeSetParam(synth, delay, "timeMs", 700.0f + (rng.nextFloat() * 200.0f * chaos)); // Convert normalized to ms (0.7-0.9 normalized -> 700-900ms)
    safeSetParam(synth, delay, "feedback", 0.6f + (rng.nextFloat() * 0.2f * chaos));
    safeSetParam(synth, reverb, "size", 0.9f + (rng.nextFloat() * 0.05f * chaos));
    safeSetParam(synth, reverb, "decay", 0.8f + (rng.nextFloat() * 0.15f * chaos));

    safeSetParam(synth, lfo1, "rate", 0.05f + (rng.nextFloat() * 0.1f * chaos)); // Slow
    safeSetParam(synth, lfo2, "rate", 0.03f + (rng.nextFloat() * 0.05f * chaos)); // Very slow

    // --- Connections ---
    // Audio Path: VCOs -> Mixer -> Delay -> Reverb -> Output
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, vco3, "Out", mixer, "In B L");
    safeConnect(synth, mixer, "Out L", delay, "In L");
    safeConnect(synth, mixer, "Out R", delay, "In R");
    safeConnect(synth, delay, "Out L", reverb, "In L");
    safeConnect(synth, delay, "Out R", reverb, "In R");
    
    // Connect to output (CRITICAL)
    bool outputConnected = safeConnect(synth, reverb, "Out L", out, "Out L");
    if (!outputConnected)
    {
        juce::Logger::writeToLog("[PatchGenerator] WARNING: Failed to connect Reverb to output!");
    }
    safeConnect(synth, reverb, "Out R", out, "Out R");

    // Modulation: Slow LFOs for drift
    safeConnect(synth, lfo1, "Out", vco1, "Frequency");
    safeConnect(synth, lfo2, "Out", delay, "Time Mod");

    // --- CHAOS: Add more modulation at higher chaos ---
    if (chaos > 0.5f)
    {
        safeConnect(synth, lfo1, "Out", vco2, "Frequency");
        safeConnect(synth, lfo2, "Out", vco3, "Frequency");
    }

    juce::Logger::writeToLog("[PatchGenerator] Ambient Drone patch generation complete");
}

void PatchGenerator::generateTechnoBass(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Techno Bass patch (chaos: " + juce::String(chaos, 2) + ")");

    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- Modules ---
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    
    auto drive = addModule(synth, "drive", x, y);
    if (drive == 0) return;
    x += spacing;
    
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;

    // Control
    x = 0.0f;
    y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;

    const juce::uint32 out = 0;

    // --- Parameters ---
    safeSetParam(synth, vco, "waveform", 0.0f); // Saw (classic techno)
    
    // High resonance filter (acid-style)
    safeSetParam(synth, vcf, "cutoff", 0.2f + (rng.nextFloat() * 0.3f * chaos));
    safeSetParam(synth, vcf, "res", 0.7f + (rng.nextFloat() * 0.25f * (1.0f + chaos))); // High resonance
    
    // Distortion
    safeSetParam(synth, drive, "drive", 0.5f + (rng.nextFloat() * 0.4f * (1.0f + chaos)));
    safeSetParam(synth, drive, "mix", 0.8f + (rng.nextFloat() * 0.2f));
    
    // VCA - Set to +6dB max output (no gain mod on final VCA)
    // Normalized value 1.0f = +6dB (max), 0.0f = -60dB (min)
    safeSetParam(synth, vca, "gain", 1.0f); // +6dB output (full volume!)

    // Sequencer - fast, repetitive pattern
    safeSetParam(synth, seq, "numSteps", 8.0f);
    safeSetParam(synth, seq, "rate", 4.0f + rng.nextInt(4)); // 4-7 Hz (faster than East Coast)
    
    for (int i = 1; i <= 8; ++i)
    {
        // Techno basslines often use root + fifth pattern
        float stepVal = (i % 2 == 1) ? 0.4f : 0.6f; // Alternating pattern
        stepVal += rng.nextFloat() * 0.1f * chaos; // Add chaos
        safeSetParam(synth, seq, "step" + juce::String(i), stepVal);
        safeSetParam(synth, seq, "step" + juce::String(i) + "_gate", 1.0f); // All gates on
    }

    // ADSR - snappy envelope
    safeSetParam(synth, adsr, "attack", 0.0f);
    safeSetParam(synth, adsr, "decay", 0.1f + (rng.nextFloat() * 0.1f));
    safeSetParam(synth, adsr, "sustain", 0.0f);
    safeSetParam(synth, adsr, "release", 0.05f + (rng.nextFloat() * 0.1f));

    // --- Connections ---
    // Audio Path: VCO -> VCF -> Drive -> VCA -> Output
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", drive, "In L");
    safeConnect(synth, drive, "Out L", vca, "In L");
    safeConnect(synth, drive, "Out R", vca, "In R");
    
    // Connect to output (CRITICAL)
    bool outputConnected = safeConnect(synth, vca, "Out L", out, "Out L");
    if (!outputConnected)
    {
        juce::Logger::writeToLog("[PatchGenerator] WARNING: Failed to connect VCA to output!");
    }
    safeConnect(synth, vca, "Out R", out, "Out R");

    // Control: Sequencer -> VCO Pitch
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    // Note: Final VCA gain is fixed at +6dB, no gain modulation

    // --- CHAOS: Add filter modulation ---
    if (chaos > 0.4f)
    {
        // Sequencer can modulate filter cutoff for acid-style sweeps
        safeConnect(synth, seq, "Mod", vcf, "Cutoff Mod");
    }

    juce::Logger::writeToLog("[PatchGenerator] Techno Bass patch generation complete");
}

void PatchGenerator::generateGlitch(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Glitch patch (chaos: " + juce::String(chaos, 2) + ")");

    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- Modules ---
    auto noise = addModule(synth, "noise", x, y);
    if (noise == 0) return;
    x += spacing;
    
    auto s_and_h = addModule(synth, "s_and_h", x, y);
    if (s_and_h == 0) return;
    x += spacing;
    
    auto clockDiv = addModule(synth, "clock_divider", x, y);
    if (clockDiv == 0) return;
    x += spacing;
    
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    
    auto bitCrusher = addModule(synth, "bit_crusher", x, y);
    if (bitCrusher == 0) return;

    // Control
    x = 0.0f;
    y = 300.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    x += spacing;
    
    auto random = addModule(synth, "random", x, y);
    if (random == 0) return;
    x += spacing;
    
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;

    const juce::uint32 out = 0;

    // --- Parameters ---
    safeSetParam(synth, noise, "colour", 0.5f + (rng.nextFloat() * 0.5f * chaos));
    // Set noise level to audible (default is -12dB, too quiet)
    // Normalized value: (target_dB - min_dB) / (max_dB - min_dB) = (0.0 - (-60.0)) / (6.0 - (-60.0)) = 60/66 ≈ 0.909
    safeSetParam(synth, noise, "level", 0.909f); // 0.909f normalized ≈ 0.0dB (audible level)
    
    safeSetParam(synth, vcf, "cutoff", 0.3f + (rng.nextFloat() * 0.5f * chaos));
    safeSetParam(synth, vcf, "res", 0.4f + (rng.nextFloat() * 0.4f * chaos));
    
    safeSetParam(synth, bitCrusher, "bitDepth", 0.2f + (rng.nextFloat() * 0.6f * chaos));
    safeSetParam(synth, bitCrusher, "sampleRate", 0.3f + (rng.nextFloat() * 0.5f * chaos));
    
    safeSetParam(synth, lfo, "rate", 0.5f + (rng.nextFloat() * 2.0f * (1.0f + chaos))); // Fast, erratic
    // VCA - Set to +6dB max output (no gain mod on final VCA)
    // Normalized value 1.0f = +6dB (max), 0.0f = -60dB (min)
    safeSetParam(synth, vca, "gain", 1.0f); // +6dB output (full volume!)

    // --- Connections ---
    // Audio Path: Noise -> S&H -> VCF -> Bit Crusher -> VCA -> Output
    // Noise has mono output "Out", not "Out L"
    safeConnect(synth, noise, "Out", s_and_h, "Signal In L");
    safeConnect(synth, s_and_h, "Out L", vcf, "In L");
    safeConnect(synth, vcf, "Out L", bitCrusher, "In L");
    safeConnect(synth, bitCrusher, "Out L", vca, "In L");
    safeConnect(synth, bitCrusher, "Out R", vca, "In R");
    
    // Connect to output (CRITICAL)
    bool outputConnected = safeConnect(synth, vca, "Out L", out, "Out L");
    if (!outputConnected)
    {
        juce::Logger::writeToLog("[PatchGenerator] WARNING: Failed to connect VCA to output!");
    }
    safeConnect(synth, vca, "Out R", out, "Out R");

    // Control: LFO -> S&H Gate, Random -> S&H Gate
    safeConnect(synth, lfo, "Out", s_and_h, "Gate In L");
    safeConnect(synth, random, "Trig Out", s_and_h, "Gate In R");
    
    // Clock Divider for rhythmic glitches
    safeConnect(synth, lfo, "Out", clockDiv, "Clock In");
    safeConnect(synth, clockDiv, "/4", vcf, "Cutoff Mod");

    // --- CHAOS: Add more erratic modulation ---
    if (chaos > 0.5f)
    {
        safeConnect(synth, random, "Norm Out", vcf, "Resonance Mod");
        safeConnect(synth, random, "CV Out", bitCrusher, "Bit Depth Mod");
    }

    juce::Logger::writeToLog("[PatchGenerator] Glitch patch generation complete");
}

float PatchGenerator::getRandomNote(float root, const std::vector<float>& scale, juce::Random& rng)
{
    if (scale.empty())
        return root;
    return scale[rng.nextInt((int)scale.size())];
}

void PatchGenerator::generateEthereal(ModularSynthProcessor* synth, float chaos)
{
    if (!synth)
        return;
    
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Ethereal patch (chaos: " + juce::String(chaos, 2) + ")");

    // Base values from XML preset
    float vco1Freq = 251.0f;
    float vco2Freq = 229.0f;
    float vco3Freq = 163.0f;
    float mixerGain = -11.38f;
    float delayTimeMs = 536.4f;
    float delayFeedback = 0.361f;
    float delayMix = 0.3f;
    float reverbSize = 0.936f;
    float reverbDamp = 0.302f;
    float reverbMix = 0.5f;
    float lfo1Rate = 0.05f;
    float lfo2Rate = 0.08f;
    float mathValueA = -100.0f;
    float mathValueB = -1.107f;

    // Apply chaos randomization to frequencies and some parameters
    if (chaos > 0.0f)
    {
        // Randomize VCO frequencies (musical intervals)
        float freqVariation = chaos * 0.3f; // ±30% variation
        vco1Freq *= (1.0f + rng.nextFloat() * freqVariation * 2.0f - freqVariation);
        vco2Freq *= (1.0f + rng.nextFloat() * freqVariation * 2.0f - freqVariation);
        vco3Freq *= (1.0f + rng.nextFloat() * freqVariation * 2.0f - freqVariation);
        
        // Randomize delay time
        delayTimeMs *= (1.0f + rng.nextFloat() * chaos * 0.5f - chaos * 0.25f);
        delayTimeMs = juce::jlimit(100.0f, 2000.0f, delayTimeMs);
        
        // Randomize LFO rates
        lfo1Rate *= (1.0f + rng.nextFloat() * chaos * 0.4f - chaos * 0.2f);
        lfo1Rate = juce::jlimit(0.01f, 2.0f, lfo1Rate);
        lfo2Rate *= (1.0f + rng.nextFloat() * chaos * 0.4f - chaos * 0.2f);
        lfo2Rate = juce::jlimit(0.01f, 2.0f, lfo2Rate);
        
        // Randomize reverb size slightly
        reverbSize += rng.nextFloat() * chaos * 0.2f - chaos * 0.1f;
        reverbSize = juce::jlimit(0.0f, 1.0f, reverbSize);
    }

    // Node positions from XML (will be applied by beautify)
    float x = 0.0f, y = 0.0f;
    float spacing = 200.0f;

    // --- Modules --- (matching XML preset exactly)
    auto vco1 = addModule(synth, "vco", 441.0f, 666.5f);
    if (vco1 == 0) return;
    
    auto vco2 = addModule(synth, "vco", -5.0f, 710.5f);
    if (vco2 == 0) return;
    
    auto vco3 = addModule(synth, "vco", -5.0f, 1433.5f);
    if (vco3 == 0) return;
    
    auto mixer = addModule(synth, "mixer", 442.0f, 1325.25f);
    if (mixer == 0) return;
    
    auto delay = addModule(synth, "delay", 870.0f, 1322.25f);
    if (delay == 0) return;
    
    auto reverb = addModule(synth, "reverb", 1303.0f, 1237.75f);
    if (reverb == 0) return;

    auto lfo1 = addModule(synth, "lfo", -5.0f, 2156.5f);
    if (lfo1 == 0) return;
    
    auto lfo2 = addModule(synth, "lfo", -5.0f, 2715.5f);
    if (lfo2 == 0) return;
    
    auto math = addModule(synth, "math", 428.0f, 1925.25f);
    if (math == 0) return;

    auto att1 = addModule(synth, "attenuverter", 456.5f, 1804.875f);
    if (att1 == 0) return;
    
    auto att2 = addModule(synth, "attenuverter", 649.0f, 1335.625f);
    if (att2 == 0) return;
    
    auto att3 = addModule(synth, "attenuverter", 976.0f, 1286.6875f);
    if (att3 == 0) return;
    
    auto att4 = addModule(synth, "attenuverter", 1139.5f, 1262.21875f);
    if (att4 == 0) return;

    const juce::uint32 out = 0;

    // --- Parameters --- (matching XML preset exactly, with chaos applied)
    // VCO 1: frequency, waveform 2.0 (Square), portamento 0.0, relative_freq_mod 1.0
    safeSetParam(synth, vco1, "frequency", vco1Freq);
    safeSetParam(synth, vco1, "waveform", 2.0f); // Square
    safeSetParam(synth, vco1, "portamento", 0.0f);
    safeSetParam(synth, vco1, "relative_freq_mod", 1.0f);

    // VCO 2: frequency, waveform 2.0, portamento 0.0, relative_freq_mod 1.0
    safeSetParam(synth, vco2, "frequency", vco2Freq);
    safeSetParam(synth, vco2, "waveform", 2.0f); // Square
    safeSetParam(synth, vco2, "portamento", 0.0f);
    safeSetParam(synth, vco2, "relative_freq_mod", 1.0f);

    // VCO 3: frequency, waveform 2.0, portamento 0.0, relative_freq_mod 1.0
    safeSetParam(synth, vco3, "frequency", vco3Freq);
    safeSetParam(synth, vco3, "waveform", 2.0f); // Square
    safeSetParam(synth, vco3, "portamento", 0.0f);
    safeSetParam(synth, vco3, "relative_freq_mod", 1.0f);

    // Mixer: gain, crossfade 0.0, pan 0.0
    safeSetParam(synth, mixer, "gain", mixerGain);
    safeSetParam(synth, mixer, "crossfade", 0.0f);
    safeSetParam(synth, mixer, "pan", 0.0f);

    // Delay: timeMs, feedback, mix, relativeTimeMod 1.0, relativeFeedbackMod 1.0, relativeMixMod 1.0
    safeSetParam(synth, delay, "timeMs", delayTimeMs);
    safeSetParam(synth, delay, "feedback", delayFeedback);
    safeSetParam(synth, delay, "mix", delayMix);
    safeSetParam(synth, delay, "relativeTimeMod", 1.0f);
    safeSetParam(synth, delay, "relativeFeedbackMod", 1.0f);
    safeSetParam(synth, delay, "relativeMixMod", 1.0f);

    // Reverb: size, damp, mix, relativeSizeMod 1.0, relativeDampMod 1.0, relativeMixMod 1.0
    safeSetParam(synth, reverb, "size", reverbSize);
    safeSetParam(synth, reverb, "damp", reverbDamp);
    safeSetParam(synth, reverb, "mix", reverbMix);
    safeSetParam(synth, reverb, "relativeSizeMod", 1.0f);
    safeSetParam(synth, reverb, "relativeDampMod", 1.0f);
    safeSetParam(synth, reverb, "relativeMixMod", 1.0f);

    // LFO 1: rate, depth 0.5, wave 0.0 (Sine), bipolar 1.0, sync 0.0, rate_division 3.0, relative_mode 1.0
    safeSetParam(synth, lfo1, "rate", lfo1Rate);
    safeSetParam(synth, lfo1, "depth", 0.5f);
    safeSetParam(synth, lfo1, "wave", 0.0f); // Sine
    safeSetParam(synth, lfo1, "bipolar", 1.0f);
    safeSetParam(synth, lfo1, "sync", 0.0f);
    safeSetParam(synth, lfo1, "rate_division", 3.0f);
    safeSetParam(synth, lfo1, "relative_mode", 1.0f);

    // LFO 2: rate, depth 0.5, wave 0.0, bipolar 1.0, sync 0.0, rate_division 3.0, relative_mode 1.0
    safeSetParam(synth, lfo2, "rate", lfo2Rate);
    safeSetParam(synth, lfo2, "depth", 0.5f);
    safeSetParam(synth, lfo2, "wave", 0.0f); // Sine
    safeSetParam(synth, lfo2, "bipolar", 1.0f);
    safeSetParam(synth, lfo2, "sync", 0.0f);
    safeSetParam(synth, lfo2, "rate_division", 3.0f);
    safeSetParam(synth, lfo2, "relative_mode", 1.0f);

    // Math: operation 12.0 (Modulo), valueA, valueB
    safeSetParam(synth, math, "operation", 12.0f); // Modulo
    safeSetParam(synth, math, "valueA", mathValueA);
    safeSetParam(synth, math, "valueB", mathValueB);

    // Attenuverters: all amount 1.0, rectify 0.0
    safeSetParam(synth, att1, "amount", 1.0f);
    safeSetParam(synth, att1, "rectify", 0.0f);
    safeSetParam(synth, att2, "amount", 1.0f);
    safeSetParam(synth, att2, "rectify", 0.0f);
    safeSetParam(synth, att3, "amount", 1.0f);
    safeSetParam(synth, att3, "rectify", 0.0f);
    safeSetParam(synth, att4, "amount", 1.0f);
    safeSetParam(synth, att4, "rectify", 0.0f);

    // --- Connections --- (matching XML preset exactly)
    // Audio Path: VCOs -> Mixer -> Delay -> Reverb -> Output
    safeConnect(synth, vco1, "Out", mixer, "In A L"); // channel 0
    safeConnect(synth, vco2, "Out", mixer, "In A R"); // channel 1
    safeConnect(synth, vco3, "Out", mixer, "In B L"); // channel 2
    
    // VCO3 also feeds attenuverters
    safeConnect(synth, vco3, "Out", att1, "In L");
    safeConnect(synth, vco3, "Out", att2, "In L");
    
    safeConnect(synth, mixer, "Out L", delay, "In L");
    safeConnect(synth, mixer, "Out R", delay, "In R");
    safeConnect(synth, delay, "Out L", reverb, "In L");
    safeConnect(synth, delay, "Out R", reverb, "In R");
    
    // Connect to output
    safeConnect(synth, reverb, "Out L", out, "Out L");
    safeConnect(synth, reverb, "Out R", out, "Out R");

    // Modulation: LFO1 -> Delay Mix Mod (channel 4)
    safeConnect(synth, lfo1, "Out", delay, "Mix Mod");
    
    // Modulation: LFO2 -> Reverb Mix Mod (channel 4)
    safeConnect(synth, lfo2, "Out", reverb, "Mix Mod");
    
    // Math Logic: LFO2 -> Math In A, Math output -> Delay Time Mod (channel 3)
    safeConnect(synth, lfo2, "Out", math, "In A");
    safeConnect(synth, math, "Out", delay, "Time Mod");
    
    // Attenuverter chain: att1 -> delay Feedback Mod, att2 -> att3 -> att4 -> reverb Size Mod
    safeConnect(synth, att1, "Out L", delay, "Feedback Mod");
    safeConnect(synth, att2, "Out L", att3, "In L");
    safeConnect(synth, att3, "Out L", att4, "In L");
    safeConnect(synth, att4, "Out L", reverb, "Size Mod");

    juce::Logger::writeToLog("[PatchGenerator] Ethereal patch generation complete");
}

// ========== NEW PRESETS (20) ==========

void PatchGenerator::generateAcidLead(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Acid Lead patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    x += spacing;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.0f); // Saw
    safeSetParam(synth, vcf, "cutoff", 0.15f + chaos * 0.2f);
    safeSetParam(synth, vcf, "res", 0.8f + chaos * 0.15f); // High resonance
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, seq, "numSteps", 16.0f);
    safeSetParam(synth, seq, "rate", 3.0f + chaos * 2.0f);
    safeSetParam(synth, adsr, "attack", 0.0f);
    safeSetParam(synth, adsr, "decay", 0.1f);
    safeSetParam(synth, adsr, "sustain", 0.0f);
    safeSetParam(synth, adsr, "release", 0.05f);
    safeSetParam(synth, lfo, "rate", 0.3f + chaos * 0.5f);
    
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vcf, "Cutoff Mod");
    safeConnect(synth, lfo, "Out", vcf, "Cutoff Mod");
}

void PatchGenerator::generatePluck(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Pluck patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f); // Triangle
    safeSetParam(synth, vcf, "cutoff", 0.6f + chaos * 0.2f);
    safeSetParam(synth, vcf, "res", 0.1f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, adsr, "attack", 0.0f);
    safeSetParam(synth, adsr, "decay", 0.3f + chaos * 0.2f);
    safeSetParam(synth, adsr, "sustain", 0.0f);
    safeSetParam(synth, adsr, "release", 0.1f);
    
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vcf, "Cutoff Mod");
    // Note: Final VCA gain is fixed at +6dB, no gain modulation
}

void PatchGenerator::generateWarmPad(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Warm Pad patch");
    
    // Base values from XML preset
    float vco1Freq = 344.0f;
    float vco2Freq = 217.0f;
    float mixerGain = 6.0f;
    float vcfCutoff = 531.0f;
    float vcfResonance = 3.03f;
    float reverbSize = 0.931f;
    float reverbDamp = 0.598f;
    float reverbMix = 1.0f;
    float lfoRate = 0.06f;

    // Apply chaos randomization to frequencies and some parameters
    if (chaos > 0.0f)
    {
        // Randomize VCO frequencies (musical intervals)
        float freqVariation = chaos * 0.2f; // ±20% variation
        vco1Freq *= (1.0f + rng.nextFloat() * freqVariation * 2.0f - freqVariation);
        vco2Freq *= (1.0f + rng.nextFloat() * freqVariation * 2.0f - freqVariation);
        
        // Randomize VCF cutoff slightly
        vcfCutoff *= (1.0f + rng.nextFloat() * chaos * 0.15f - chaos * 0.075f);
        vcfCutoff = juce::jlimit(20.0f, 20000.0f, vcfCutoff);
        
        // Randomize LFO rate
        lfoRate *= (1.0f + rng.nextFloat() * chaos * 0.3f - chaos * 0.15f);
        lfoRate = juce::jlimit(0.01f, 2.0f, lfoRate);
    }

    // Node positions from XML
    auto vco1 = addModule(synth, "vco", 398.0f, -10.5f);
    if (vco1 == 0) return;
    
    auto vco2 = addModule(synth, "vco", -16.0f, 691.5f);
    if (vco2 == 0) return;
    
    auto mixer = addModule(synth, "mixer", 427.0f, 695.75f);
    if (mixer == 0) return;
    
    auto vcf = addModule(synth, "vcf", 835.0f, 650.75f);
    if (vcf == 0) return;
    
    auto reverb = addModule(synth, "reverb", 1330.0f, 575.75f);
    if (reverb == 0) return;
    
    auto lfo = addModule(synth, "lfo", -375.0f, 1070.5f);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    // --- Parameters --- (matching XML preset exactly, with chaos applied)
    // VCO 1: frequency, waveform 1.0 (Sawtooth), portamento 0.0, relative_freq_mod 1.0
    safeSetParam(synth, vco1, "frequency", vco1Freq);
    safeSetParam(synth, vco1, "waveform", 1.0f); // Sawtooth
    safeSetParam(synth, vco1, "portamento", 0.0f);
    safeSetParam(synth, vco1, "relative_freq_mod", 1.0f);

    // VCO 2: frequency, waveform 1.0, portamento 0.0, relative_freq_mod 1.0
    safeSetParam(synth, vco2, "frequency", vco2Freq);
    safeSetParam(synth, vco2, "waveform", 1.0f); // Sawtooth
    safeSetParam(synth, vco2, "portamento", 0.0f);
    safeSetParam(synth, vco2, "relative_freq_mod", 1.0f);

    // Mixer: crossfade -1.0, gain, pan 0.0
    safeSetParam(synth, mixer, "crossfade", -1.0f);
    safeSetParam(synth, mixer, "gain", mixerGain);
    safeSetParam(synth, mixer, "pan", 0.0f);

    // VCF: cutoff, resonance, type 0.0 (Low-pass), relativeCutoffMod 1.0, relativeResonanceMod 1.0, type_mod 0.0
    safeSetParam(synth, vcf, "cutoff", vcfCutoff);
    safeSetParam(synth, vcf, "resonance", vcfResonance);
    safeSetParam(synth, vcf, "type", 0.0f); // Low-pass
    safeSetParam(synth, vcf, "relativeCutoffMod", 1.0f);
    safeSetParam(synth, vcf, "relativeResonanceMod", 1.0f);
    safeSetParam(synth, vcf, "type_mod", 0.0f);

    // Reverb: size, damp, mix, relativeSizeMod 1.0, relativeDampMod 1.0, relativeMixMod 1.0
    safeSetParam(synth, reverb, "size", reverbSize);
    safeSetParam(synth, reverb, "damp", reverbDamp);
    safeSetParam(synth, reverb, "mix", reverbMix);
    safeSetParam(synth, reverb, "relativeSizeMod", 1.0f);
    safeSetParam(synth, reverb, "relativeDampMod", 1.0f);
    safeSetParam(synth, reverb, "relativeMixMod", 1.0f);

    // LFO: rate, depth 0.5, wave 0.0 (Sine), bipolar 1.0, sync 0.0, rate_division 3.0, relative_mode 1.0
    safeSetParam(synth, lfo, "rate", lfoRate);
    safeSetParam(synth, lfo, "depth", 0.5f);
    safeSetParam(synth, lfo, "wave", 0.0f); // Sine
    safeSetParam(synth, lfo, "bipolar", 1.0f);
    safeSetParam(synth, lfo, "sync", 0.0f);
    safeSetParam(synth, lfo, "rate_division", 3.0f);
    safeSetParam(synth, lfo, "relative_mode", 1.0f);
    
    // --- Connections --- (matching XML preset exactly)
    safeConnect(synth, vco1, "Out", mixer, "In A L"); // channel 0
    safeConnect(synth, vco2, "Out", mixer, "In A R"); // channel 1
    safeConnect(synth, mixer, "Out L", vcf, "In L");
    safeConnect(synth, mixer, "Out R", vcf, "In R");
    safeConnect(synth, vcf, "Out L", reverb, "In L");
    safeConnect(synth, vcf, "Out R", reverb, "In R");
    safeConnect(synth, reverb, "Out L", out, "Out L");
    safeConnect(synth, reverb, "Out R", out, "Out R");
    safeConnect(synth, lfo, "Out", vcf, "Cutoff Mod"); // channel 2
}

void PatchGenerator::generateDeepBass(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Deep Bass patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.0f); // Saw
    safeSetParam(synth, vcf, "cutoff", 0.1f + chaos * 0.15f); // Low cutoff
    safeSetParam(synth, vcf, "res", 0.3f + chaos * 0.2f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, adsr, "attack", 0.01f);
    safeSetParam(synth, adsr, "decay", 0.2f);
    safeSetParam(synth, adsr, "sustain", 0.7f);
    safeSetParam(synth, adsr, "release", 0.3f);
    
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vcf, "Cutoff Mod");
}

void PatchGenerator::generateBrightLead(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Bright Lead patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    x += spacing;
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    x += spacing;
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco1, "waveform", 1.0f); // Saw
    safeSetParam(synth, vco2, "waveform", 0.75f); // Square
    safeSetParam(synth, vco2, "detune", 0.52f);
    safeSetParam(synth, vcf, "cutoff", 0.7f + chaos * 0.2f); // High cutoff
    safeSetParam(synth, vcf, "res", 0.2f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, adsr, "attack", 0.01f);
    safeSetParam(synth, adsr, "decay", 0.1f);
    safeSetParam(synth, adsr, "sustain", 0.8f);
    safeSetParam(synth, adsr, "release", 0.15f);
    
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, mixer, "Out L", vcf, "In L");
    safeConnect(synth, mixer, "Out R", vcf, "In R");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco1, "Frequency");
    safeConnect(synth, seq, "Pitch", vco2, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vca, "Gain Mod");
}

void PatchGenerator::generateArpeggio(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Arpeggio patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f); // Triangle
    safeSetParam(synth, vcf, "cutoff", 0.5f + chaos * 0.3f);
    safeSetParam(synth, vcf, "res", 0.2f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, seq, "numSteps", 8.0f);
    safeSetParam(synth, seq, "rate", 4.0f + chaos * 3.0f); // Fast
    safeSetParam(synth, adsr, "attack", 0.0f);
    safeSetParam(synth, adsr, "decay", 0.05f);
    safeSetParam(synth, adsr, "sustain", 0.0f);
    safeSetParam(synth, adsr, "release", 0.1f);
    
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vca, "Gain Mod");
}

void PatchGenerator::generatePercussion(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Percussion patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto noise = addModule(synth, "noise", x, y);
    if (noise == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, noise, "level", 0.909f); // 0dB
    safeSetParam(synth, vcf, "cutoff", 0.3f + chaos * 0.4f);
    safeSetParam(synth, vcf, "res", 0.5f + chaos * 0.3f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, adsr, "attack", 0.0f);
    safeSetParam(synth, adsr, "decay", 0.1f + chaos * 0.2f);
    safeSetParam(synth, adsr, "sustain", 0.0f);
    safeSetParam(synth, adsr, "release", 0.05f);
    
    safeConnect(synth, noise, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    safeConnect(synth, adsr, "Env Out", vcf, "Cutoff Mod");
    // Note: Final VCA gain is fixed at +6dB, no gain modulation
}

void PatchGenerator::generateChordProg(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Chord Progression patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    y += spacing;
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    y += spacing;
    auto vco3 = addModule(synth, "vco", x, y);
    if (vco3 == 0) return;
    x = spacing; y = spacing;
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 900.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco1, "waveform", 0.5f);
    safeSetParam(synth, vco2, "waveform", 0.5f);
    safeSetParam(synth, vco3, "waveform", 0.5f);
    safeSetParam(synth, vco2, "detune", 0.52f);
    safeSetParam(synth, vco3, "detune", 0.48f);
    safeSetParam(synth, vcf, "cutoff", 0.4f + chaos * 0.3f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, seq, "numSteps", 4.0f);
    safeSetParam(synth, seq, "rate", 1.0f);
    
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, vco3, "Out", mixer, "In B L");
    safeConnect(synth, mixer, "Out L", vcf, "In L");
    safeConnect(synth, mixer, "Out R", vcf, "In R");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco1, "Frequency");
}

void PatchGenerator::generateNoiseSweep(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Noise Sweep patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto noise = addModule(synth, "noise", x, y);
    if (noise == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    x += spacing;
    auto funcGen = addModule(synth, "function_generator", x, y);
    if (funcGen == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, noise, "level", 0.909f);
    safeSetParam(synth, vcf, "cutoff", 0.2f);
    safeSetParam(synth, vcf, "res", 0.6f + chaos * 0.3f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, lfo, "rate", 0.1f + chaos * 0.3f);
    
    safeConnect(synth, noise, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, lfo, "Out", vcf, "Cutoff Mod");
    safeConnect(synth, funcGen, "Value", vcf, "Cutoff Mod");
}

void PatchGenerator::generateFM(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating FM patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto carrier = addModule(synth, "vco", x, y);
    if (carrier == 0) return;
    x += spacing;
    auto modulator = addModule(synth, "vco", x, y);
    if (modulator == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, carrier, "waveform", 0.5f);
    safeSetParam(synth, modulator, "waveform", 0.0f); // Saw
    safeSetParam(synth, modulator, "detune", 0.6f + chaos * 0.2f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, lfo, "rate", 0.5f + chaos * 1.0f);
    
    safeConnect(synth, modulator, "Out", carrier, "Frequency");
    safeConnect(synth, carrier, "Out", vca, "In L");
    safeConnect(synth, carrier, "Out", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", carrier, "Frequency");
    safeConnect(synth, lfo, "Out", modulator, "Frequency");
}

void PatchGenerator::generateGranular(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Granular patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto granulator = addModule(synth, "granulator", x, y);
    if (granulator == 0) return;
    x += spacing;
    auto reverb = addModule(synth, "reverb", x, y);
    if (reverb == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f);
    safeSetParam(synth, reverb, "size", 0.8f + chaos * 0.15f);
    safeSetParam(synth, reverb, "mix", 0.4f + chaos * 0.3f);
    safeSetParam(synth, lfo, "rate", 0.2f + chaos * 0.5f);
    
    safeConnect(synth, vco, "Out", granulator, "In L");
    safeConnect(synth, granulator, "Out L", reverb, "In L");
    safeConnect(synth, granulator, "Out R", reverb, "In R");
    safeConnect(synth, reverb, "Out L", out, "Out L");
    safeConnect(synth, reverb, "Out R", out, "Out R");
    safeConnect(synth, lfo, "Out", granulator, "Position Mod");
}

void PatchGenerator::generateDelayLoop(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Delay Loop patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto delay1 = addModule(synth, "delay", x, y);
    if (delay1 == 0) return;
    x += spacing;
    auto delay2 = addModule(synth, "delay", x, y);
    if (delay2 == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f);
    safeSetParam(synth, delay1, "timeMs", 300.0f + chaos * 200.0f); // 300-500ms
    safeSetParam(synth, delay1, "feedback", 0.7f + chaos * 0.2f);
    safeSetParam(synth, delay2, "timeMs", 500.0f + chaos * 300.0f); // 500-800ms
    safeSetParam(synth, delay2, "feedback", 0.6f + chaos * 0.3f);
    safeSetParam(synth, vca, "gain", 1.0f);
    
    safeConnect(synth, vco, "Out", delay1, "In L");
    safeConnect(synth, delay1, "Out L", delay2, "In L");
    safeConnect(synth, delay2, "Out L", vca, "In L");
    safeConnect(synth, delay2, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
}

void PatchGenerator::generateReverbWash(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Reverb Wash patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto reverb1 = addModule(synth, "reverb", x, y);
    if (reverb1 == 0) return;
    x += spacing;
    auto reverb2 = addModule(synth, "reverb", x, y);
    if (reverb2 == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f);
    safeSetParam(synth, reverb1, "size", 0.95f);
    safeSetParam(synth, reverb1, "mix", 0.8f + chaos * 0.15f);
    safeSetParam(synth, reverb2, "size", 0.9f);
    safeSetParam(synth, reverb2, "mix", 0.7f + chaos * 0.2f);
    safeSetParam(synth, lfo, "rate", 0.05f + chaos * 0.1f);
    
    safeConnect(synth, vco, "Out", reverb1, "In L");
    safeConnect(synth, reverb1, "Out L", reverb2, "In L");
    safeConnect(synth, reverb1, "Out R", reverb2, "In R");
    safeConnect(synth, reverb2, "Out L", out, "Out L");
    safeConnect(synth, reverb2, "Out R", out, "Out R");
    safeConnect(synth, lfo, "Out", vco, "Frequency");
}

void PatchGenerator::generateDistorted(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Distorted patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto drive = addModule(synth, "drive", x, y);
    if (drive == 0) return;
    x += spacing;
    auto waveshaper = addModule(synth, "waveshaper", x, y);
    if (waveshaper == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.0f); // Saw
    safeSetParam(synth, drive, "drive", 0.7f + chaos * 0.25f);
    safeSetParam(synth, drive, "mix", 0.9f);
    safeSetParam(synth, vca, "gain", 1.0f);
    
    safeConnect(synth, vco, "Out", drive, "In L");
    safeConnect(synth, drive, "Out L", waveshaper, "In L");
    safeConnect(synth, waveshaper, "Out L", vca, "In L");
    safeConnect(synth, waveshaper, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
}

void PatchGenerator::generateWobbleBass(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Wobble Bass patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.0f); // Saw
    safeSetParam(synth, vcf, "cutoff", 0.2f + chaos * 0.3f);
    safeSetParam(synth, vcf, "res", 0.5f + chaos * 0.3f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, lfo, "rate", 0.3f + chaos * 0.5f); // Wobble rate
    
    safeConnect(synth, vco, "Out", vcf, "In L");
    safeConnect(synth, vcf, "Out L", vca, "In L");
    safeConnect(synth, vcf, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, lfo, "Out", vcf, "Cutoff Mod");
}

void PatchGenerator::generateStutter(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Stutter patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto s_and_h = addModule(synth, "s_and_h", x, y);
    if (s_and_h == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto clockDiv = addModule(synth, "clock_divider", x, y);
    if (clockDiv == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.0f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, seq, "rate", 2.0f + chaos * 3.0f);
    
    safeConnect(synth, vco, "Out", s_and_h, "Signal In L");
    safeConnect(synth, s_and_h, "Out L", vca, "In L");
    safeConnect(synth, s_and_h, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
    safeConnect(synth, seq, "Gate", clockDiv, "Clock In");
    safeConnect(synth, clockDiv, "/4", s_and_h, "Gate In L");
}

void PatchGenerator::generateHarmonic(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Harmonic patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    y += spacing;
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    y += spacing;
    auto vco3 = addModule(synth, "vco", x, y);
    if (vco3 == 0) return;
    x = spacing; y = spacing;
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco1, "waveform", 0.0f); // Saw
    safeSetParam(synth, vco2, "waveform", 0.0f);
    safeSetParam(synth, vco3, "waveform", 0.0f);
    safeSetParam(synth, vco2, "detune", 0.5f); // Octave
    safeSetParam(synth, vco3, "detune", 0.33f); // Fifth
    safeSetParam(synth, vca, "gain", 1.0f);
    
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, vco3, "Out", mixer, "In B L");
    safeConnect(synth, mixer, "Out L", vca, "In L");
    safeConnect(synth, mixer, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
}

void PatchGenerator::generateMinimal(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Minimal patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, vco, "waveform", 0.5f); // Triangle
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, seq, "rate", 1.0f + chaos * 2.0f);
    
    safeConnect(synth, vco, "Out", vca, "In L");
    safeConnect(synth, vco, "Out", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, seq, "Pitch", vco, "Frequency");
}

void PatchGenerator::generateComplex(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Complex patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto vco1 = addModule(synth, "vco", x, y);
    if (vco1 == 0) return;
    x += spacing;
    auto vco2 = addModule(synth, "vco", x, y);
    if (vco2 == 0) return;
    x += spacing;
    auto mixer = addModule(synth, "mixer", x, y);
    if (mixer == 0) return;
    x += spacing;
    auto vcf = addModule(synth, "vcf", x, y);
    if (vcf == 0) return;
    x += spacing;
    auto delay = addModule(synth, "delay", x, y);
    if (delay == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto seq = addModule(synth, "sequencer", x, y);
    if (seq == 0) return;
    x += spacing;
    auto lfo1 = addModule(synth, "lfo", x, y);
    if (lfo1 == 0) return;
    x += spacing;
    auto lfo2 = addModule(synth, "lfo", x, y);
    if (lfo2 == 0) return;
    x += spacing;
    auto adsr = addModule(synth, "adsr", x, y);
    if (adsr == 0) return;
    
    const juce::uint32 out = 0;
    
    // --- Parameters --- (matching XML preset exactly)
    // VCOs: both 440.0 Hz, VCO1 waveform 0.0 (Sine), VCO2 waveform 1.0 (Sawtooth)
    safeSetParam(synth, vco1, "frequency", 440.0f);
    safeSetParam(synth, vco1, "waveform", 0.0f); // Sine
    
    safeSetParam(synth, vco2, "frequency", 440.0f);
    safeSetParam(synth, vco2, "waveform", 1.0f); // Sawtooth
    
    // Mixer: gain 0.0 dB
    safeSetParam(synth, mixer, "gain", 0.0f);
    
    // VCF: cutoff 531.0 Hz (normalized: (531.0 - 20.0) / (20000.0 - 20.0) ≈ 0.0256), resonance 1.0, type 0.0
    // VCF cutoff range is typically 20-20000 Hz, so normalize: (531.0 - 20.0) / (20000.0 - 20.0)
    float cutoffNormalized = (531.0f - 20.0f) / (20000.0f - 20.0f);
    safeSetParam(synth, vcf, "cutoff", cutoffNormalized);
    safeSetParam(synth, vcf, "res", 1.0f); // Max resonance
    safeSetParam(synth, vcf, "type", 0.0f); // Low-pass
    
    // Delay: timeMs 400.0, feedback 0.475, mix 0.3
    safeSetParam(synth, delay, "timeMs", 400.0f);
    safeSetParam(synth, delay, "feedback", 0.475f);
    safeSetParam(synth, delay, "mix", 0.3f);
    
    // VCA: gain 6.0 dB (normalized: (6.0 - (-60.0)) / (6.0 - (-60.0)) = 1.0)
    safeSetParam(synth, vca, "gain", 1.0f); // +6dB max (normalized 1.0)
    
    // Sequencer: 8 steps, rate 2.0
    safeSetParam(synth, seq, "numSteps", 8.0f);
    safeSetParam(synth, seq, "rate", 2.0f);
    // Set sequencer step values from XML
    safeSetParam(synth, seq, "step1", 0.557f);
    safeSetParam(synth, seq, "step1_gate", 0.53f);
    safeSetParam(synth, seq, "step1_trig", 1.0f);
    safeSetParam(synth, seq, "step2", 0.328f);
    safeSetParam(synth, seq, "step2_gate", 0.59f);
    safeSetParam(synth, seq, "step3", 0.738f);
    safeSetParam(synth, seq, "step3_gate", 0.39f);
    safeSetParam(synth, seq, "step3_trig", 1.0f);
    safeSetParam(synth, seq, "step4", 0.630f);
    safeSetParam(synth, seq, "step4_gate", 0.05f);
    safeSetParam(synth, seq, "step5", 0.315f);
    safeSetParam(synth, seq, "step5_gate", 0.12f);
    safeSetParam(synth, seq, "step5_trig", 1.0f);
    safeSetParam(synth, seq, "step6", 0.769f);
    safeSetParam(synth, seq, "step6_gate", 0.88f);
    safeSetParam(synth, seq, "step7", 0.829f);
    safeSetParam(synth, seq, "step7_gate", 0.15f);
    safeSetParam(synth, seq, "step8", 0.289f);
    safeSetParam(synth, seq, "step8_gate", 0.37f);
    
    // LFOs: rate 0.14 and 0.09
    safeSetParam(synth, lfo1, "rate", 0.14f);
    safeSetParam(synth, lfo2, "rate", 0.09f);
    
    // ADSR: attack 0.001, decay 0.091, sustain 0.5, release 0.251
    safeSetParam(synth, adsr, "attack", 0.001f);
    safeSetParam(synth, adsr, "decay", 0.091f);
    safeSetParam(synth, adsr, "sustain", 0.5f);
    safeSetParam(synth, adsr, "release", 0.251f);
    
    // --- Connections --- (matching XML preset exactly)
    // Audio Path: VCO1 -> Mixer, VCO2 -> VCO1 (FM) + Mixer, Mixer -> VCF -> Delay -> VCA -> Output
    safeConnect(synth, vco1, "Out", mixer, "In A L");
    safeConnect(synth, vco2, "Out", vco1, "Frequency"); // FM: VCO2 modulates VCO1 frequency
    safeConnect(synth, vco2, "Out", mixer, "In A R");
    safeConnect(synth, mixer, "Out L", vcf, "In L");
    safeConnect(synth, mixer, "Out R", vcf, "In R");
    safeConnect(synth, vcf, "Out L", delay, "In L");
    safeConnect(synth, vcf, "Out R", delay, "In R");
    safeConnect(synth, delay, "Out L", vca, "In L");
    safeConnect(synth, delay, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    
    // Control: Sequencer -> VCOs, ADSR
    safeConnect(synth, seq, "Pitch", vco1, "Frequency");
    safeConnect(synth, seq, "Pitch", vco2, "Frequency");
    safeConnect(synth, seq, "Gate", adsr, "Gate In");
    
    // Modulation: LFO1 -> VCF Cutoff Mod, LFO2 -> Delay Feedback Mod
    safeConnect(synth, lfo1, "Out", vcf, "Cutoff Mod");
    safeConnect(synth, lfo2, "Out", delay, "Feedback Mod");
    
    // Note: ADSR is not connected to VCA in the XML, so VCA stays at fixed +6dB gain
}

void PatchGenerator::generateExperimental(ModularSynthProcessor* synth, float chaos)
{
    if (!synth) return;
    juce::Random rng;
    juce::Logger::writeToLog("[PatchGenerator] Generating Experimental patch");
    
    float x = 0.0f, y = 0.0f, spacing = 200.0f;
    
    auto noise = addModule(synth, "noise", x, y);
    if (noise == 0) return;
    x += spacing;
    auto vco = addModule(synth, "vco", x, y);
    if (vco == 0) return;
    x += spacing;
    auto s_and_h = addModule(synth, "s_and_h", x, y);
    if (s_and_h == 0) return;
    x += spacing;
    auto bitCrusher = addModule(synth, "bit_crusher", x, y);
    if (bitCrusher == 0) return;
    x += spacing;
    auto waveshaper = addModule(synth, "waveshaper", x, y);
    if (waveshaper == 0) return;
    x += spacing;
    auto vca = addModule(synth, "vca", x, y);
    if (vca == 0) return;
    
    x = 0.0f; y = 300.0f;
    auto random = addModule(synth, "random", x, y);
    if (random == 0) return;
    x += spacing;
    auto lfo = addModule(synth, "lfo", x, y);
    if (lfo == 0) return;
    
    const juce::uint32 out = 0;
    
    safeSetParam(synth, noise, "level", 0.909f);
    safeSetParam(synth, vco, "waveform", 0.0f);
    safeSetParam(synth, bitCrusher, "bitDepth", 0.1f + chaos * 0.5f);
    safeSetParam(synth, bitCrusher, "sampleRate", 0.2f + chaos * 0.6f);
    safeSetParam(synth, vca, "gain", 1.0f);
    safeSetParam(synth, lfo, "rate", 0.5f + chaos * 2.0f);
    
    safeConnect(synth, noise, "Out", s_and_h, "Signal In L");
    safeConnect(synth, vco, "Out", s_and_h, "Signal In R");
    safeConnect(synth, s_and_h, "Out L", bitCrusher, "In L");
    safeConnect(synth, bitCrusher, "Out L", waveshaper, "In L");
    safeConnect(synth, waveshaper, "Out L", vca, "In L");
    safeConnect(synth, waveshaper, "Out R", vca, "In R");
    safeConnect(synth, vca, "Out L", out, "Out L");
    safeConnect(synth, vca, "Out R", out, "Out R");
    safeConnect(synth, random, "Trig Out", s_and_h, "Gate In L");
    safeConnect(synth, lfo, "Out", s_and_h, "Gate In R");
    safeConnect(synth, random, "CV Out", bitCrusher, "Bit Depth Mod");
}

void PatchGenerator::connectComplexControl(
    ModularSynthProcessor* synth,
    juce::uint32           seqId,
    juce::uint32           compId,
    juce::uint32           funcGenId,
    int                    seqStep)
{
    // Helper to wire up the specific Sequencer -> Comparator -> FuncGen chain
    // This abstracts the logic where a specific sequencer step's nuanced output
    // drives a comparator, which then triggers an envelope.

    // Note: This helper is not currently used in the main generateEastCoast function
    // because we are using the global "Nuanced Gate" output (Pin 2) of the sequencer
    // rather than individual step outputs. However, it's kept for future per-step logic.
}
