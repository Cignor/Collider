#pragma once

#include "ModuleProcessor.h"
#include <box2d/box2d.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <array>
#include <deque>

// The different types of materials a user can draw with
enum class MaterialType { Metal, Wood, Soil };

// Data structure for defining the sound of a material
struct MaterialSoundData 
{
    std::vector<float> frequencies = { 1.0f, 1.618f, 2.718f };
    float decayTime = 0.5f;
    float basePitchHz = 440.0f;
};

// Data structure for a user-drawn line
struct Stroke 
{
    std::vector<juce::Point<float>> points;
    MaterialType material;
    b2Body* physicsBody = nullptr; // Pointer to the Box2D physics body
};

// The different types of dynamic shapes
enum class ShapeType { Circle, Square, Triangle };

// Data structure for a dynamic physics object
struct PhysicsObject
{
    b2Body* physicsBody = nullptr;
    ShapeType type = ShapeType::Circle;
    float radius = 10.0f; // Only used for circles
    std::vector<b2Vec2> vertices; // Used for polygons (in local space)
    juce::uint32 lastSoundTimeMs = 0; // Timestamp of last sound event (prevents jitter spam)
};

// ==============================================================================
// A simple synth voice for our modal synthesizer
// ==============================================================================

struct SynthVoice
{
    void prepare(double sampleRate)
    {
        for (auto& osc : oscillators)
        {
            osc.prepare({ sampleRate, 512, 1 }); // Spec for processor
            osc.initialise([](float x) { return std::sin(x); }); // Sine wave
        }
        envelope.setSampleRate(sampleRate);
    }

    // Start a new note based on material properties and collision impact
    void startNote(const MaterialSoundData& material, float impactImpulse, float newPan)
    {
        // Debug: Log voice starting
        DBG("SynthVoice: startNote called! Impulse=" << impactImpulse 
            << " Gain=" << juce::jlimit(0.1f, 1.0f, impactImpulse / 5.0f)
            << " BasePitch=" << material.basePitchHz);
        
        // Set the frequencies of our oscillators based on the material's partials
        for (size_t i = 0; i < oscillators.size() && i < material.frequencies.size(); ++i)
        {
            float freq = material.basePitchHz * material.frequencies[i];
            oscillators[i].setFrequency(freq, true);
        }

        // Set the envelope's decay based on the material
        juce::ADSR::Parameters params;
        params.attack = 0.005f;
        params.decay = material.decayTime;
        params.sustain = 0.0f;
        params.release = 0.1f;
        envelope.setParameters(params);

        // The impact impulse directly controls the loudness
        noteGain = juce::jlimit(0.1f, 1.0f, impactImpulse / 5.0f); // Scale impulse to a usable gain
        
        // Store the pan value (0 = left, 0.5 = center, 1 = right)
        pan = juce::jlimit(0.0f, 1.0f, newPan);

        envelope.noteOn();
    }

    // Get the next audio sample from this voice
    float getNextSample()
    {
        if (!envelope.isActive())
            return 0.0f;

        // Sum the output of all our oscillators
        float sample = 0.0f;
        sample += oscillators[0].processSample(0.0f);
        sample += oscillators[1].processSample(0.0f);
        sample += oscillators[2].processSample(0.0f);

        // Apply the envelope and gain
        return envelope.getNextSample() * (sample / 3.0f) * noteGain;
    }

    bool isActive() const { return envelope.isActive(); }
    
    float getPan() const { return pan; }

private:
    // A voice with 3 partials (oscillators) is a good starting point
    std::array<juce::dsp::Oscillator<float>, 3> oscillators;
    juce::ADSR envelope;
    float noteGain = 0.0f;
    float pan = 0.5f; // 0 = left, 0.5 = center, 1 = right
};

// ==============================================================================
class PhysicsModuleProcessor : public ModuleProcessor, private juce::Timer
{
public:
    PhysicsModuleProcessor();
    ~PhysicsModuleProcessor() override;

    // --- JUCE / Module Overrides ---
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    const juce::String getName() const override { return "Physics"; }
    juce::AudioProcessorValueTreeState& getAPVTS() override { return apvts; }
    
    // Trigger a sound based on material, collision impact, position, and shape type
    void playSound(MaterialType material, float impulse, float collisionX, ShapeType shapeType);
    
    // Dynamic pin interface - expose all 6 output channels and 3 input triggers
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
#endif

    // --- State Management ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;

private:
    // --- Physics ---
    void timerCallback() override;
    void createStrokeBody(Stroke& stroke);
    void spawnObject(ShapeType type, b2Vec2 position = {0, 0}, b2Vec2 velocity = {0, 0});
    
    std::unique_ptr<b2World> world;
    std::unique_ptr<class PhysicsContactListener> contactListener;
    std::vector<Stroke> userStrokes;
    std::deque<std::unique_ptr<PhysicsObject>> physicsObjects;  // Use deque for efficient front deletion
    std::vector<b2Body*> bodiesToDestroy;  // Buffer for deferred body destruction
    
    // --- Audio Synthesis ---
    std::array<SynthVoice, 8> synthVoices; // A small synth with 8 voices for polyphony
    int nextVoice = 0; // To keep track of which voice to use next
    
    // --- Trigger Outputs ---
    // Index 0: Main trigger (all collisions)
    // Index 1: Ball/Circle trigger
    // Index 2: Square trigger  
    // Index 3: Triangle trigger
    std::array<std::atomic<float>, 4> triggerOutputValues;
    
    // --- GUI State ---
    bool isDrawing = false; // Flag to track if we're currently drawing a stroke
    bool isErasing = false;
    MaterialType currentMaterial = MaterialType::Metal;
    Stroke currentDrawingStroke; // The stroke currently being drawn by the user
    
    // --- Inertia Simulation ---
    juce::Point<float> previousNodePos { 0.0f, 0.0f };
    b2Vec2 inertialForce { 0.0f, 0.0f };
    
    // --- Thread-Safe Spawning ---
    juce::AbstractFifo spawnQueue { 32 }; // Lock-free queue for spawn requests
    std::vector<ShapeType> spawnQueueBuffer; // Buffer for queue data
    std::array<bool, 3> lastTriggerStates { false, false, false }; // Edge detection

    // --- Audio Synthesis ---
    std::map<MaterialType, MaterialSoundData> materialDatabase;
    std::map<MaterialType, juce::Colour> materialColourMap;
    // Add your synthesizer voice(s) and other DSP components here

    // --- Parameter Management ---
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicsModuleProcessor)
};
