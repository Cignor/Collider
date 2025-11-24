#pragma once

#include "ModuleProcessor.h"
#include <box2d/box2d.h>
#include <juce_dsp/juce_dsp.h>
#include <map>
#include <array>
#include <deque>

// The different types of strokes a user can draw with (materials + functional types)
enum class StrokeType {
    // Material Types
    Metal, Wood, Soil,
    // Functional Types
    Conveyor,
    BouncyGoo,
    StickyMud
};

// Data structure for defining both sound and physics properties of a material
struct MaterialData 
{
    // Sound Properties
    std::vector<float> frequencies = { 1.0f, 1.618f, 2.718f };
    float decayTime = 0.5f;
    float basePitchHz = 440.0f;
    
    // Physics Properties
    float friction = 0.5f;
    float restitution = 0.3f; // Bounciness
};

// Data structure for a user-drawn line
struct Stroke
{
    std::vector<juce::Point<float>> points;
    StrokeType type; // Material or functional type
    b2Body* physicsBody = nullptr; // Pointer to the Box2D physics body
    juce::Point<float> conveyorDirection; // Store normalized direction vector (for conveyor belts)
};

// The different types of dynamic shapes
enum class ShapeType { Circle, Square, Triangle };

// The different types of magnetic polarity for emitters
enum class Polarity { None, North, South };

// Data structure for a dynamic physics object
struct PhysicsObject
{
    b2Body* physicsBody = nullptr;
    ShapeType type = ShapeType::Circle;
    float radius = 10.0f; // Only used for circles
    std::vector<b2Vec2> vertices; // Used for polygons (in local space)
    float mass = 1.0f; // Mass of the object (affects physics and sound)
    Polarity polarity = Polarity::None; // Magnetic polarity (for future electromagnetic features)
    MaterialData materialData; // ADD THIS - Each object has its own material properties
    juce::uint32 lastSoundTimeMs = 0; // Timestamp of last sound event (prevents jitter spam)
};

// The different types of force objects that can be placed
enum class ForceType { Vortex };

// Data structure for a placed force object
struct ForceObject
{
    b2Vec2 position; // Position in physics coordinates (meters)
    ForceType type;
};

// Data structure for a physics-based emitter
struct EmitterObject
{
    b2Vec2 position;         // Position in physics coordinates
    ShapeType shapeToSpawn = ShapeType::Circle;
    float spawnRateHz = 1.0f;  // Objects per second
    b2Vec2 initialVelocity = { 0.0f, 0.0f };
    float mass = 1.0f;       // Mass of spawned objects
    Polarity polarity = Polarity::None; // Magnetic polarity (for future electromagnetic features)
    float timeSinceLastSpawn = 0.0f; // Internal timer
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
            osc.prepare({ sampleRate, 512, 1 });
            osc.initialise([](float x) { return std::sin(x); });
        }
        envelope.setSampleRate(sampleRate);
    }

    // REVERTED to the simpler signature
    void startNote(const MaterialData& material, float impactImpulse, float newPan)
    {
        // Hard reset
        envelope.reset();
        for (auto& osc : oscillators)
            osc.reset();

        // Set frequencies from the material
        for (size_t i = 0; i < oscillators.size() && i < material.frequencies.size(); ++i)
        {
            float freq = material.basePitchHz * material.frequencies[i];
            oscillators[i].setFrequency(freq, true);
        }

        juce::ADSR::Parameters params;
        params.attack = 0.005f;
        params.decay = material.decayTime;
        params.sustain = 0.0f;
        params.release = 0.1f;
        envelope.setParameters(params);

        // REVERTED to simpler gain calculation
        noteGain = juce::jlimit(0.1f, 1.0f, impactImpulse / 5.0f);

        pan = juce::jlimit(0.0f, 1.0f, newPan);
        envelope.noteOn();
    }

    float getNextSample()
    {
        if (!envelope.isActive())
            return 0.0f;

        float sample = 0.0f;
        sample += oscillators[0].processSample(0.0f);
        sample += oscillators[1].processSample(0.0f);
        sample += oscillators[2].processSample(0.0f);

        return envelope.getNextSample() * (sample / 3.0f) * noteGain;
    }

    bool isActive() const { return envelope.isActive(); }
    float getPan() const { return pan; }

private:
    std::array<juce::dsp::Oscillator<float>, 3> oscillators;
    juce::ADSR envelope;
    float noteGain = 0.0f;
    float pan = 0.5f;
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
    
    // Trigger a sound based on material, collision impact, position, shape type, and object mass
    void playSound(StrokeType strokeType, float impulse, float collisionX, ShapeType shapeType);

    // ADD THIS - Trigger a sound directly from MaterialData (for object-object collisions)
    void playSoundWithMaterial(const MaterialData& material, float impulse, float mass, float collisionX);
    
    // Dynamic pin interface - expose all 6 output channels and 3 input triggers
    std::vector<DynamicPinInfo> getDynamicInputPins() const override;
    std::vector<DynamicPinInfo> getDynamicOutputPins() const override;
    bool usesCustomPinLayout() const override { return true; }
    
    // Modulation routing for CV inputs
    bool getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const override;
    
#if defined(PRESET_CREATOR_UI)
    void drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded) override;
    void drawIoPins(const NodePinHelpers& helpers) override;
    
    // Custom node dimensions - Physics module needs a large canvas for the simulation
    ImVec2 getCustomNodeSize() const override 
    { 
        return ImVec2(640.0f, 0.0f); // Wide canvas, auto-height
    }
#endif

    // --- State Management ---
    juce::ValueTree getExtraStateTree() const override;
    void setExtraStateTree(const juce::ValueTree& state) override;
    void forceStop() override; // Force stop (used after patch load)

private:
    // --- Physics ---
    void timerCallback() override;
    void setTimingInfo(const TransportState& state) override;
    void createStrokeBody(Stroke& stroke);
    void spawnObject(ShapeType type, float mass = 1.0f, b2Vec2 position = {0, 0}, b2Vec2 velocity = {0, 0}, Polarity polarity = Polarity::None);
    
    std::unique_ptr<b2World> world;
    std::unique_ptr<class PhysicsContactListener> contactListener;
    std::vector<Stroke> userStrokes;
    std::deque<std::unique_ptr<PhysicsObject>> physicsObjects;  // Use deque for efficient front deletion
    std::vector<std::unique_ptr<PhysicsObject>> objectsToDestroy; // Buffer for deferred object destruction
    
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
    StrokeType currentStrokeType = StrokeType::Metal;
    Stroke currentDrawingStroke; // The stroke currently being drawn by the user

    // --- Draggable Spawn Point ---
    b2Vec2 manualSpawnPoint;
    bool isDraggingSpawnPoint = false;

    // --- Spawning State ---
    float currentMass = 1.0f; // Mass for manually spawned objects
    Polarity currentPolarity = Polarity::None; // Polarity for manually spawned objects

    // --- Force Tool State ---
    std::optional<ForceType> currentForceTool; // The force tool selected, if any
    std::vector<ForceObject> forceObjects;     // A list of all placed forces

    // --- Emitter Tool State ---
    bool isPlacingEmitter = false;
    int selectedEmitterIndex = -1;             // Index of currently selected emitter (-1 = none)
    std::vector<EmitterObject> emitters;       // A list of all placed emitters
    
    // --- Inertia Simulation ---
    juce::Point<float> previousNodePos { 0.0f, 0.0f };
    b2Vec2 inertialForce { 0.0f, 0.0f };
    
    // --- Transport State ---
    TransportState m_currentTransport;
    
    // --- Thread-Safe Spawning ---
    juce::AbstractFifo spawnQueue { 32 }; // Lock-free queue for spawn requests

    // ADD THIS STRUCT
    struct SpawnRequest
    {
        ShapeType type;
        float mass;
        Polarity polarity;
        b2Vec2 position = {0, 0}; // Default to 0,0 to signal manual spawn point
        b2Vec2 velocity = {0, 0};
    };

    // CHANGE THIS LINE
    std::vector<SpawnRequest> spawnQueueBuffer; // Buffer for queue data

    // --- Thread-Safe Clear All ---
    std::atomic<bool> clearAllRequested { false }; // Flag for clear all operations

    // --- Thread-Safe Stroke Creation ---
    struct StrokeCreationRequest
    {
        std::vector<juce::Point<float>> points;
        StrokeType type;
    };
    juce::AbstractFifo strokeCreationQueue { 16 }; // Queue for stroke creation requests
    std::vector<StrokeCreationRequest> strokeCreationQueueBuffer; // Buffer for queue data

    // --- Thread-Safe Destruction ---
    juce::AbstractFifo destructionQueue { 32 }; // Queue for body destruction requests
    std::vector<b2Body*> destructionQueueBuffer; // Buffer for bodies to destroy

    std::array<bool, 3> lastTriggerStates { false, false, false }; // Edge detection

    // --- Audio Synthesis ---
    std::map<StrokeType, MaterialData> strokeDatabase;
    std::map<StrokeType, juce::Colour> strokeColourMap;

    // ADD THIS - Default material database for our shapes
    std::map<ShapeType, MaterialData> shapeMaterialDatabase;
    // Add your synthesizer voice(s) and other DSP components here

    // --- CV Output Data (Thread-Safe) ---
    // Holds the four CV values for one shape type
    struct CVData
    {
        std::atomic<float> posX { 0.0f };
        std::atomic<float> posY { 0.0f };
        std::atomic<float> velX { 0.0f };
        std::atomic<float> velY { 0.0f };
    };
    
    // A map to store the CV data for each shape type
    std::map<ShapeType, CVData> cvOutputValues;

    // --- Parameter Management ---
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // Parameter IDs for APVTS
    static constexpr auto paramIdGravity        = "gravity";
    static constexpr auto paramIdWind           = "wind";
    static constexpr auto paramIdStrokeSize     = "strokeSize";
    static constexpr auto paramIdMaxObjects     = "maxObjects";
    static constexpr auto paramIdVortexStrength = "vortexStrength";
    static constexpr auto paramIdVortexSpin     = "vortexSpin";
    static constexpr auto paramIdMagnetForce    = "magnetForce"; // ADD THIS

    // Virtual modulation target IDs (no APVTS parameters required)
    static constexpr auto paramIdGravityMod        = "gravity_mod";
    static constexpr auto paramIdWindMod           = "wind_mod";
    static constexpr auto paramIdVortexStrengthMod = "vortexStrength_mod";
    static constexpr auto paramIdVortexSpinMod     = "vortexSpin_mod";
    static constexpr auto paramIdMagnetForceMod    = "magnetForce_mod"; // ADD THIS

    // Atomics to hold CV values read from the audio thread
    std::atomic<float> gravityModValue        { -1.0f }; // -1.0f indicates not modulated
    std::atomic<float> windModValue          { -1.0f }; // -1.0f indicates not modulated
    std::atomic<float> vortexStrengthModValue { -1.0f }; // -1.0f indicates not modulated
    std::atomic<float> vortexSpinModValue     { -1.0f }; // -1.0f indicates not modulated
    std::atomic<float> magnetForceModValue    { -1.0f }; // ADD THIS

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhysicsModuleProcessor)
};
