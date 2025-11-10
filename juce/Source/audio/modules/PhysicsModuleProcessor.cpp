#include "PhysicsModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h" // For ImGui calls
#include "../../preset_creator/theme/ThemeManager.h"
#endif

// ==============================================================================
// Contact Listener for Collision Detection
// ==============================================================================

// Struct to hold information about a new collision event
struct CollisionInfo
{
    Stroke* stroke = nullptr;
    PhysicsObject* object = nullptr;
};


class PhysicsContactListener : public b2ContactListener
{
public:
    PhysicsContactListener(PhysicsModuleProcessor* p) : processor(p) {}

    void BeginContact(b2Contact* contact) override;
    void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) override;
    void EndContact(b2Contact* contact) override;

    // Called after the physics engine calculates the collision response
    void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override
    {
        float totalImpulse = 0.0f;
        for (int i = 0; i < impulse->count; ++i)
            totalImpulse += impulse->normalImpulses[i];

        if (totalImpulse < 0.1f)
            return;

        b2Body* bodyA = contact->GetFixtureA()->GetBody();
        b2Body* bodyB = contact->GetFixtureB()->GetBody();

        // --- CASE 1: Object vs Object Collision (NEW) ---
        if (bodyA->GetType() == b2_dynamicBody && bodyB->GetType() == b2_dynamicBody)
        {
            auto* objectA = reinterpret_cast<PhysicsObject*>(bodyA->GetUserData().pointer);
            auto* objectB = reinterpret_cast<PhysicsObject*>(bodyB->GetUserData().pointer);

            if (objectA && objectB)
            {
                // Use the object that was hit harder for the sound, or the heavier one
                auto* soundSource = (objectA->mass > objectB->mass) ? objectA : objectB;

                const juce::uint32 currentTimeMs = juce::Time::getMillisecondCounter();
                if (currentTimeMs > soundSource->lastSoundTimeMs + 50)
                {
                    b2WorldManifold worldManifold;
                    contact->GetWorldManifold(&worldManifold);

                    // Call playSoundWithMaterial with the object's own material data
                    processor->playSoundWithMaterial(soundSource->materialData, totalImpulse, soundSource->mass, worldManifold.points[0].x);

                    soundSource->lastSoundTimeMs = currentTimeMs;
                }
            }
        }
        // --- CASE 2: Object vs Stroke Collision (Existing) ---
        else
        {
            // Check if this contact matches one of the new ones we detected in BeginContact
            for (const auto& collision : newCollisionsThisStep)
            {
                b2Body* objectBody = (bodyA->GetType() == b2_dynamicBody) ? bodyA : bodyB;
                auto* currentObject = reinterpret_cast<PhysicsObject*>(objectBody->GetUserData().pointer);

                if (currentObject == collision.object)
                {
                    const juce::uint32 currentTimeMs = juce::Time::getMillisecondCounter();
                    if (currentTimeMs > currentObject->lastSoundTimeMs + 50)
                    {
                        b2WorldManifold worldManifold;
                        contact->GetWorldManifold(&worldManifold);

                        processor->playSound(collision.stroke->type, totalImpulse, worldManifold.points[0].x, collision.object->type);

                        currentObject->lastSoundTimeMs = currentTimeMs;
                    }
                    break;
                }
            }
        }
    }

    void clearNewCollisions()
    {
        newCollisionsThisStep.clear();
    }

private:
    PhysicsModuleProcessor* processor;
    std::vector<CollisionInfo> newCollisionsThisStep;
};

// Implementation of the contact listener methods (defined outside the class)
void PhysicsContactListener::BeginContact(b2Contact* contact)
{
    b2Body* bodyA = contact->GetFixtureA()->GetBody();
    b2Body* bodyB = contact->GetFixtureB()->GetBody();

    // Identify which body is the stroke and which is the dynamic object
    b2Body* strokeBody = (bodyA->GetType() == b2_staticBody) ? bodyA : bodyB;
    b2Body* objectBody = (bodyA->GetType() == b2_dynamicBody) ? bodyA : bodyB;

    if (strokeBody && objectBody)
    {
        auto* stroke = reinterpret_cast<Stroke*>(strokeBody->GetUserData().pointer);
        auto* object = reinterpret_cast<PhysicsObject*>(objectBody->GetUserData().pointer);

        if (stroke && object)
        {
            // Handle conveyor belt interaction
            if (stroke->type == StrokeType::Conveyor)
            {
                const float conveyorSpeed = 5.0f; // Speed in meters/sec
                b2Vec2 velocity = {
                    stroke->conveyorDirection.x * conveyorSpeed,
                    stroke->conveyorDirection.y * conveyorSpeed
                };
                objectBody->SetLinearVelocity(velocity);
            }

            // Handle Sticky Mud interaction (entering)
            if (stroke->type == StrokeType::StickyMud)
            {
                // High damping slows the object down rapidly
                objectBody->SetLinearDamping(10.0f);
                objectBody->SetAngularDamping(10.0f);
            }

            // GUARD: Only store collisions with sound-making materials for PostSolve
            if (stroke->type == StrokeType::Metal || stroke->type == StrokeType::Wood || stroke->type == StrokeType::Soil)
            {
                // Store this new collision pair to be processed in PostSolve
                newCollisionsThisStep.push_back({ stroke, object });
            }
        }
    }
}

void PhysicsContactListener::PreSolve(b2Contact* contact, const b2Manifold* oldManifold)
{
    juce::ignoreUnused(oldManifold);

    b2Body* bodyA = contact->GetFixtureA()->GetBody();
    b2Body* bodyB = contact->GetFixtureB()->GetBody();
    b2Body* strokeBody = (bodyA->GetType() == b2_staticBody) ? bodyA : bodyB;

    auto* stroke = reinterpret_cast<Stroke*>(strokeBody->GetUserData().pointer);

    if (stroke && stroke->type == StrokeType::BouncyGoo)
    {
        // Values > 1.0 create energy, making it super bouncy
        contact->SetRestitution(2.0f);
    }
}

void PhysicsContactListener::EndContact(b2Contact* contact)
{
    b2Body* bodyA = contact->GetFixtureA()->GetBody();
    b2Body* bodyB = contact->GetFixtureB()->GetBody();
    b2Body* strokeBody = (bodyA->GetType() == b2_staticBody) ? bodyA : bodyB;
    b2Body* objectBody = (bodyA->GetType() == b2_dynamicBody) ? bodyA : bodyB;

    auto* stroke = reinterpret_cast<Stroke*>(strokeBody->GetUserData().pointer);

    // If an object is leaving a sticky mud stroke, reset its damping
    if (stroke && objectBody && stroke->type == StrokeType::StickyMud)
    {
        objectBody->SetLinearDamping(0.0f);
        objectBody->SetAngularDamping(0.0f);
    }
}

// ==============================================================================
// PhysicsModuleProcessor Implementation
// ==============================================================================

PhysicsModuleProcessor::PhysicsModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::discreteChannels(8), true) // 3x Spawn Triggers + 2x CV Mod + 2x Vortex CV Mod + 1x Magnet Force CV Mod
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(18), true)), // L, R, 4x Triggers, 12x CV
      apvts(*this, nullptr, "PhysicsParams", createParameterLayout())
{
    // Define the physics world with some gravity
    b2Vec2 gravity(0.0f, 9.8f);
    world = std::make_unique<b2World>(gravity);
    
    // Create and set the contact listener
    contactListener = std::make_unique<PhysicsContactListener>(this);
    world->SetContactListener(contactListener.get());
    
    // Set up your stroke sound database
    // Initialize stroke database with sound and physics properties
    strokeDatabase[StrokeType::Metal] = { {1.0f, 2.76f, 5.4f}, 1.5f, 500.0f, 0.3f, 0.8f }; // Low friction, high bounce
    strokeDatabase[StrokeType::Wood]  = { {1.0f, 1.8f}, 0.2f, 250.0f, 0.7f, 0.2f };        // High friction, low bounce
    strokeDatabase[StrokeType::Soil]  = { {1.0f}, 0.05f, 100.0f, 0.9f, 0.05f };           // Very high friction, very low bounce

    // Add sound properties for functional stroke types (they use material physics properties)
    strokeDatabase[StrokeType::Conveyor]  = { {1.0f, 1.5f}, 0.1f, 200.0f, 0.5f, 0.5f };  // Smooth, mechanical sound
    strokeDatabase[StrokeType::BouncyGoo] = { {1.0f, 1.8f, 2.2f}, 0.3f, 300.0f, 0.1f, 1.0f }; // Bright, springy sound
    strokeDatabase[StrokeType::StickyMud]  = { {1.0f}, 0.8f, 80.0f, 0.8f, 0.1f };         // Deep, muted sound

    // Define the colors for our stroke types
    strokeColourMap[StrokeType::Metal] = juce::Colours::lightblue;
    strokeColourMap[StrokeType::Wood]  = juce::Colours::sandybrown;
    strokeColourMap[StrokeType::Soil]  = juce::Colours::darkgreen;
    strokeColourMap[StrokeType::Conveyor] = juce::Colours::mediumpurple;
    strokeColourMap[StrokeType::BouncyGoo] = juce::Colours::springgreen;
    strokeColourMap[StrokeType::StickyMud] = juce::Colours::saddlebrown;

    // --- ADD THIS INITIALIZATION ---
    // Define default sounds for dynamic shapes
    shapeMaterialDatabase[ShapeType::Circle]   = { {1.0f, 3.1f, 5.8f}, 0.8f, 600.0f }; // Ringing, metallic sound
    shapeMaterialDatabase[ShapeType::Square]   = { {1.0f, 1.9f}, 0.3f, 350.0f };       // Dull, woody sound
    shapeMaterialDatabase[ShapeType::Triangle] = { {1.0f, 2.1f}, 0.3f, 350.0f };       // Also woody


    // Initialize trigger outputs (Main, Ball, Square, Triangle)
    for (auto& val : triggerOutputValues)
        val = 0.0f;

    // Initialize the stroke creation queue buffer
    strokeCreationQueueBuffer.resize(64);

    // Initialize thread-safe destruction queue buffer (larger size for eraser operations)
    destructionQueueBuffer.resize(128);

    // Initialize thread-safe spawn queue buffer (larger size for high trigger rates)
    spawnQueueBuffer.resize(256);

    // Initialize draggable spawn point to top-center (same as old default)
    const float scale = 50.0f;
    manualSpawnPoint.Set((600.0f / 2.0f) / scale, 10.0f / scale);

    // Initialize output telemetry (L, R, Main Trigger, Ball Trigger, Square Trigger, Triangle Trigger)
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));
    lastOutputValues.push_back(std::make_unique<std::atomic<float>>(0.0f));

    // Start the physics simulation timer (e.g., at 60 FPS)
    startTimerHz(60); 
}

PhysicsModuleProcessor::~PhysicsModuleProcessor()
{
    stopTimer();
    
    // Clean up all physics bodies before destroying the world
    // Use robust approach: iterate through world's body list directly
    if (world)
    {
        std::vector<b2Body*> bodiesToDestroy;
        for (b2Body* b = world->GetBodyList(); b; b = b->GetNext())
        {
            bodiesToDestroy.push_back(b);
        }
        
        for (auto* body : bodiesToDestroy)
        {
            world->DestroyBody(body);
        }
    }
    
    // The unique_ptr for the world will handle cleanup
}

juce::AudioProcessorValueTreeState::ParameterLayout PhysicsModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Add parameters here as needed (e.g., gravity, wind, stroke size, etc.)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdGravity, "Gravity",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f), 9.8f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdWind, "Wind",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdStrokeSize, "Stroke Size",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 3.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        paramIdMaxObjects, "Max Objects",
        1, 500, 100));  // Min=1, Max=500, Default=100

    // Global force parameters
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdVortexStrength, "Vortex Strength",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdVortexSpin, "Vortex Spin",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        paramIdMagnetForce, "Magnet Force",
        juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), 0.0f));

    // Material physics properties (friction and restitution/bounciness)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "metalFriction", "Metal Friction",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "metalRestitution", "Metal Bounciness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.8f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "woodFriction", "Wood Friction",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "woodRestitution", "Wood Bounciness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "soilFriction", "Soil Friction",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.9f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "soilRestitution", "Soil Bounciness",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.05f));
    
    return { params.begin(), params.end() };
}

void PhysicsModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    // Prepare all of our synth voices with the current sample rate
    for (auto& voice : synthVoices)
    {
        voice.prepare(sampleRate);
    }
}

void PhysicsModuleProcessor::releaseResources()
{
    // Called when playback stops. Free any resources that are no longer needed.
}

std::vector<DynamicPinInfo> PhysicsModuleProcessor::getDynamicInputPins() const
{
    // Define 8 input pins: 3 triggers for spawning shapes + 5 CV modulation inputs
    return {
        { "Spawn Ball",       0, PinDataType::Gate },      // Trigger to spawn circle
        { "Spawn Square",     1, PinDataType::Gate },      // Trigger to spawn square
        { "Spawn Triangle",   2, PinDataType::Gate },      // Trigger to spawn triangle
        { "Gravity Mod",      3, PinDataType::CV   },      // CV modulation for gravity
        { "Wind Mod",         4, PinDataType::CV   },      // CV modulation for wind
        { "Vortex Str Mod",   5, PinDataType::CV   },      // CV modulation for vortex strength
        { "Vortex Spin Mod",  6, PinDataType::CV   },      // CV modulation for vortex spin
        { "Magnet Force Mod", 7, PinDataType::CV   }       // CV modulation for magnet force
    };
}

bool PhysicsModuleProcessor::getParamRouting(const juce::String& paramId, int& outBusIndex, int& outChannelIndexInBus) const
{
    outBusIndex = 0; // All modulation is on the single input bus
    if (paramId == paramIdGravityMod)        { outChannelIndexInBus = 3; return true; }
    if (paramId == paramIdWindMod)           { outChannelIndexInBus = 4; return true; }
    if (paramId == paramIdVortexStrengthMod) { outChannelIndexInBus = 5; return true; }
    if (paramId == paramIdVortexSpinMod)     { outChannelIndexInBus = 6; return true; }
    if (paramId == paramIdMagnetForceMod)    { outChannelIndexInBus = 7; return true; }
    return false;
}

std::vector<DynamicPinInfo> PhysicsModuleProcessor::getDynamicOutputPins() const
{
    // Define all 18 output pins for the Physics module
    return {
        // Audio Outputs (Green)
        { "Out L",          0, PinDataType::Audio },     // Left audio channel
        { "Out R",          1, PinDataType::Audio },     // Right audio channel
        // Trigger Outputs (Yellow)
        { "Main Trigger",   2, PinDataType::Gate },      // Main trigger (all collisions)
        { "Ball Trigger",   3, PinDataType::Gate },      // Ball/Circle trigger
        { "Square Trigger", 4, PinDataType::Gate },      // Square trigger
        { "Triangle Trig",  5, PinDataType::Gate },      // Triangle trigger
        // CV Outputs (Blue) - Median values for each shape type
        { "Ball Pos X",     6, PinDataType::CV },        // Median X position of all balls
        { "Ball Pos Y",     7, PinDataType::CV },        // Median Y position of all balls
        { "Ball Vel X",     8, PinDataType::CV },        // Median X velocity of all balls
        { "Ball Vel Y",     9, PinDataType::CV },        // Median Y velocity of all balls
        { "Square Pos X",   10, PinDataType::CV },       // Median X position of all squares
        { "Square Pos Y",   11, PinDataType::CV },       // Median Y position of all squares
        { "Square Vel X",   12, PinDataType::CV },       // Median X velocity of all squares
        { "Square Vel Y",   13, PinDataType::CV },       // Median Y velocity of all squares
        { "Triangle Pos X", 14, PinDataType::CV },       // Median X position of all triangles
        { "Triangle Pos Y", 15, PinDataType::CV },       // Median Y position of all triangles
        { "Triangle Vel X", 16, PinDataType::CV },       // Median X velocity of all triangles
        { "Triangle Vel Y", 17, PinDataType::CV }        // Median Y velocity of all triangles
    };
}

void PhysicsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    
    // Get access to the input bus BEFORE clearing the main buffer
    const auto inBus = getBusBuffer(buffer, true, 0);
    const int numInputChannels = inBus.getNumChannels();
    
    // --- Modulation Input Processing ---
    // Read CV values and store them atomically for the physics thread
    const bool isGravityMod = isParamInputConnected(paramIdGravityMod);
    if (isGravityMod && numInputChannels > 3 && numSamples > 0)
        gravityModValue.store(inBus.getReadPointer(3)[0]);
    else
        gravityModValue.store(-1.0f); // Sentinel for "not connected"

    const bool isWindMod = isParamInputConnected(paramIdWindMod);
    if (isWindMod && numInputChannels > 4 && numSamples > 0)
        windModValue.store(inBus.getReadPointer(4)[0]);
    else
        windModValue.store(-1.0f); // Sentinel for "not connected"

    const bool isVortexStrMod = isParamInputConnected(paramIdVortexStrengthMod);
    if (isVortexStrMod && numInputChannels > 5 && numSamples > 0)
        vortexStrengthModValue.store(inBus.getReadPointer(5)[0]);
    else
        vortexStrengthModValue.store(-1.0f); // Sentinel for "not connected"

    const bool isVortexSpinMod = isParamInputConnected(paramIdVortexSpinMod);
    if (isVortexSpinMod && numInputChannels > 6 && numSamples > 0)
        vortexSpinModValue.store(inBus.getReadPointer(6)[0]);
    else
        vortexSpinModValue.store(-1.0f); // Sentinel for "not connected"

    const bool isMagnetMod = isParamInputConnected(paramIdMagnetForceMod);
    if (isMagnetMod && numInputChannels > 7 && numSamples > 0)
        magnetForceModValue.store(inBus.getReadPointer(7)[0]);
    else
        magnetForceModValue.store(-1.0f); // Sentinel for "not connected"

    // --- Input Trigger Detection (BEFORE clearing buffer!) ---
    // Read spawn trigger inputs (channels 0-2) and detect rising edges
    if (numInputChannels >= 8 && numSamples > 0)
    {
        for (int i = 0; i < 3; ++i) // 0=Ball, 1=Square, 2=Triangle
        {
            if (numInputChannels > i)
            {
                auto* channelData = inBus.getReadPointer(i);
                bool isTriggerHigh = (channelData[0] > 0.5f); // Check first sample
                
                // Edge detection: Rising edge (LOW → HIGH)?
                if (isTriggerHigh && !lastTriggerStates[i])
                {
                    // Write spawn request to thread-safe queue
                    ShapeType typeToSpawn = static_cast<ShapeType>(i);
                    int start1, size1, start2, size2;
                    spawnQueue.prepareToWrite(1, start1, size1, start2, size2);
                    if (size1 > 0)
                    {
                        spawnQueueBuffer[start1] = { typeToSpawn, currentMass, currentPolarity };
                        spawnQueue.finishedWrite(1);
                    }
                }
                lastTriggerStates[i] = isTriggerHigh;
            }
        }
    }
    
    // Now clear buffer for output rendering
    buffer.clear();
    
    // --- Audio Processing (channels 0 and 1 for L/R) ---
    static int debugCounter = 0;
    bool hasActiveVoice = false;
    
    if (numChannels >= 2)
    {
        // Stereo output with panning
        auto* leftChannel = buffer.getWritePointer(0);
        auto* rightChannel = buffer.getWritePointer(1);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float leftOut = 0.0f;
            float rightOut = 0.0f;
            
            // Sum the output of all active voices with panning
            for (auto& voice : synthVoices)
            {
                if (voice.isActive())
                {
                    hasActiveVoice = true;
                    
                    // Calculate pan gains using equal-power panning
                    float pan = voice.getPan();
                    float leftGain = std::sqrt(1.0f - pan);
                    float rightGain = std::sqrt(pan);
                    
                    float outputSample = voice.getNextSample();
                    leftOut += outputSample * leftGain;
                    rightOut += outputSample * rightGain;
                }
            }
            
            leftChannel[sample] = leftOut;
            rightChannel[sample] = rightOut;
        }
    }
    else if (numChannels == 1)
    {
        // Mono output
        auto* monoChannel = buffer.getWritePointer(0);
        
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float outputSample = 0.0f;
            for (auto& voice : synthVoices)
            {
                if (voice.isActive())
                {
                    outputSample += voice.getNextSample();
                }
            }
            monoChannel[sample] = outputSample;
        }
    }
    
    // --- Trigger Processing (channels 2-5: Main, Ball, Square, Triangle) ---
    // Loop through all our trigger outputs
    for (int i = 0; i < (int)triggerOutputValues.size(); ++i)
    {
        float triggerValue = triggerOutputValues[i].load();
        if (triggerValue > 0.0f)
        {
            // Audio is channels 0 & 1. Triggers start at channel 2.
            int triggerOutputChannel = 2 + i;
            if (numChannels > triggerOutputChannel)
            {
                // Write the trigger value to the first sample of the trigger buffer
                buffer.setSample(triggerOutputChannel, 0, triggerValue);
            }
            // Atomically reset the trigger so it only fires once per collision event
            triggerOutputValues[i] = 0.0f;
        }
    }
    
    // --- CV Output Processing ---
    // Maps shape type to the starting channel index for that shape's CV outputs
    // Audio(2) + Triggers(4) = 6. Ball CVs: 6-9, Square: 10-13, Triangle: 14-17
    std::map<ShapeType, int> shapeToChannelOffset = {
        { ShapeType::Circle, 6 },
        { ShapeType::Square, 10 },
        { ShapeType::Triangle, 14 }
    };
    
    for (const auto& [type, channel_offset] : shapeToChannelOffset)
    {
        // Check if we have enough channels
        if (numChannels > channel_offset + 3)
        {
            // Get the atomic values calculated by the physics thread
            float posX = cvOutputValues[type].posX.load();
            float posY = cvOutputValues[type].posY.load();
            float velX = cvOutputValues[type].velX.load();
            float velY = cvOutputValues[type].velY.load();
            
            // Write the same value to every sample in the block for a smooth CV signal
            for (int sample = 0; sample < numSamples; ++sample)
            {
                buffer.setSample(channel_offset + 0, sample, posX);
                buffer.setSample(channel_offset + 1, sample, posY);
                buffer.setSample(channel_offset + 2, sample, velX);
                buffer.setSample(channel_offset + 3, sample, velY);
            }
        }
    }
    
    // Update output telemetry
    updateOutputTelemetry(buffer);
}

void PhysicsModuleProcessor::timerCallback()
{
    // This is the heart of the simulation
    
    // --- Process Spawn Requests from Audio Thread ---
    // Read from lock-free queue and spawn objects (thread-safe!)
    int start1, size1, start2, size2;
    spawnQueue.prepareToRead(spawnQueue.getNumReady(), start1, size1, start2, size2);
    
    // Process first chunk
    if (size1 > 0)
    {
        for (int i = 0; i < size1; ++i)
        {
            // REPLACE THIS: spawnObject(spawnQueueBuffer[start1 + i]);
            // WITH THIS:
            const auto& request = spawnQueueBuffer[start1 + i];
            spawnObject(request.type, request.mass, request.position, request.velocity, request.polarity);
        }
    }

    // Process second chunk (if queue wrapped around)
    if (size2 > 0)
    {
        for (int i = 0; i < size2; ++i)
        {
            // REPLACE THIS: spawnObject(spawnQueueBuffer[start2 + i]);
            // WITH THIS:
            const auto& request = spawnQueueBuffer[start2 + i];
            spawnObject(request.type, request.mass, request.position, request.velocity, request.polarity);
        }
    }
    
    spawnQueue.finishedRead(size1 + size2);

    // --- Process Stroke Creation Requests from UI Thread ---
    int strokeStart1, strokeSize1, strokeStart2, strokeSize2;
    strokeCreationQueue.prepareToRead(strokeCreationQueue.getNumReady(), strokeStart1, strokeSize1, strokeStart2, strokeSize2);

    auto processStrokeRequest = [&](int startIndex, int numItems) {
        for (int i = 0; i < numItems; ++i)
        {

            const auto& request = strokeCreationQueueBuffer[startIndex + i];

            // Ensure we are adding a valid stroke to the main list
            userStrokes.push_back({ request.points, request.type });

            // Then create the body for the newly added stroke
            createStrokeBody(userStrokes.back());
        }
    };

    if (strokeSize1 > 0) processStrokeRequest(strokeStart1, strokeSize1);
    if (strokeSize2 > 0) processStrokeRequest(strokeStart2, strokeSize2);

    strokeCreationQueue.finishedRead(strokeSize1 + strokeSize2);
    // --- END OF STROKE CREATION PROCESSING ---

    // --- READ ALL CV MODULATION VALUES (must be done before using them) ---
    float currentGravityCV = gravityModValue.load();
    float currentWindCV = windModValue.load();
    float currentVortexStrCV = vortexStrengthModValue.load();
    float currentVortexSpinCV = vortexSpinModValue.load();
    float currentMagnetCV = magnetForceModValue.load();

    // Declare final values (will be calculated in their respective sections)
    float finalGravityValue = 0.0f;
    float finalWindValue = 0.0f;
    float finalVortexStrength = 0.0f;
    float finalVortexSpin = 0.0f;
    float finalMagnetForce = 0.0f;

    // --- EMITTER LOGIC ---
    // Update all emitters and spawn objects based on their timers
    const float timeStep = 1.0f / 60.0f; // 60 FPS
    for (auto& emitter : emitters)
    {
        emitter.timeSinceLastSpawn += timeStep;
        float spawnInterval = 1.0f / emitter.spawnRateHz;

        if (emitter.timeSinceLastSpawn >= spawnInterval)
        {
            spawnObject(emitter.shapeToSpawn, emitter.mass, emitter.position, emitter.initialVelocity, emitter.polarity);
            emitter.timeSinceLastSpawn -= spawnInterval;
        }
    }
    // --- END EMITTER LOGIC ---

    // --- Apply Gravity (with modulation override) ---
    if (currentGravityCV >= 0.0f) // Check if modulated (CV is 0..1)
    {
        auto range = apvts.getParameterRange(paramIdGravity);
        finalGravityValue = range.convertFrom0to1(currentGravityCV);
    }
    else
    {
        finalGravityValue = *apvts.getRawParameterValue(paramIdGravity);
    }
    world->SetGravity({ 0.0f, finalGravityValue });

    // --- Apply Wind (with modulation override) and Inertial Forces ---
    if (currentWindCV >= 0.0f) // Check if modulated (CV is 0..1)
    {
        auto range = apvts.getParameterRange(paramIdWind);
        finalWindValue = range.convertFrom0to1(currentWindCV);
    }
    else
    {
        finalWindValue = *apvts.getRawParameterValue(paramIdWind);
    }

    b2Vec2 windForce(finalWindValue, 0.0f);
    b2Vec2 totalForce = windForce + inertialForce;

    // Apply combined force to all dynamic objects
    for (auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody && objPtr->physicsBody->GetType() == b2_dynamicBody)
        {
            objPtr->physicsBody->ApplyForceToCenter(totalForce, true);
        }
    }

    // --- Apply Magnetic Forces ---
    if (currentMagnetCV >= 0.0f) // Check if modulated (CV is 0..1)
    {
        auto range = apvts.getParameterRange(paramIdMagnetForce);
        finalMagnetForce = range.convertFrom0to1(currentMagnetCV);
    }
    else
    {
        finalMagnetForce = *apvts.getRawParameterValue(paramIdMagnetForce);
    }


    if (std::abs(finalMagnetForce) > 0.01f)
    {
        for (size_t i = 0; i < physicsObjects.size(); ++i)
        {
            for (size_t j = i + 1; j < physicsObjects.size(); ++j)
            {
                auto& objA = physicsObjects[i];
                auto& objB = physicsObjects[j];

                if (!objA || !objB || objA->polarity == Polarity::None || objB->polarity == Polarity::None)
                    continue;

                b2Body* bodyA = objA->physicsBody;
                b2Body* bodyB = objB->physicsBody;

                b2Vec2 posA = bodyA->GetPosition();
                b2Vec2 posB = bodyB->GetPosition();
                b2Vec2 direction = posB - posA;
                float distanceSq = direction.LengthSquared();

                if (distanceSq < 0.1f) continue; // Avoid singularity at very close distances

                // Force = (k * m1 * m2) / d^2. We'll use finalMagnetForce as k.
                float forceMagnitude = finalMagnetForce / distanceSq;

                // Repel if same polarity, attract if different
                if (objA->polarity == objB->polarity)
                    forceMagnitude *= -1.0f; // Repel (negative force)

                direction.Normalize();
                b2Vec2 force = forceMagnitude * direction;

                bodyA->ApplyForceToCenter(force, true);
                bodyB->ApplyForceToCenter(-force, true); // Newton's third law
            }
        }
    }

    // --- Update UI Telemetry for modulated parameters ---
    if (currentGravityCV >= 0.0f) {
        setLiveParamValue("gravity_live", finalGravityValue);
    }
    if (currentWindCV >= 0.0f) {
        setLiveParamValue("wind_live", finalWindValue);
    }
    if (currentVortexStrCV >= 0.0f) {
        setLiveParamValue("vortex_strength_live", finalVortexStrength);
    }
    if (currentVortexSpinCV >= 0.0f) {
        setLiveParamValue("vortex_spin_live", finalVortexSpin);
    }
    if (currentMagnetCV >= 0.0f) {
        setLiveParamValue("magnet_force_live", finalMagnetForce);
    }

    // --- Apply Vortex Forces from All Placed Force Objects ---
    // Get Global Force Parameters (respecting modulation)
    if (currentVortexStrCV >= 0.0f) // Check if modulated (CV is 0..1)
    {
        auto range = apvts.getParameterRange(paramIdVortexStrength);
        finalVortexStrength = range.convertFrom0to1(currentVortexStrCV);
    }
    else
    {
        finalVortexStrength = *apvts.getRawParameterValue(paramIdVortexStrength);
    }

    if (currentVortexSpinCV >= 0.0f) // Check if modulated (CV is 0..1)
    {
        auto range = apvts.getParameterRange(paramIdVortexSpin);
        finalVortexSpin = range.convertFrom0to1(currentVortexSpinCV);
    }
    else
    {
        finalVortexSpin = *apvts.getRawParameterValue(paramIdVortexSpin);
    }

    // Apply forces from all placed force objects
    for (auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody && objPtr->physicsBody->GetType() == b2_dynamicBody)
        {
            // For every dynamic object, loop through all placed forces
            for (const auto& force : forceObjects)
            {
                if (force.type == ForceType::Vortex)
                {
                    b2Vec2 objectPos = objPtr->physicsBody->GetPosition();
                    b2Vec2 vortexPos = force.position;
                    b2Vec2 direction = vortexPos - objectPos;
                    float distance = direction.Length();

                    if (distance < 0.1f) continue; // Avoid singularity at center

                    direction.Normalize();

                    // Apply forces
                    b2Vec2 radialForce = (finalVortexStrength / distance) * direction;
                    b2Vec2 tangentialForce = finalVortexSpin * b2Vec2(-direction.y, direction.x);
                    objPtr->physicsBody->ApplyForceToCenter(radialForce + tangentialForce, true);
                }
            }
        }
    }

    // 1. Step the physics world forward in time
    world->Step(1.0f / 60.0f, 8, 3); // (timeStep, velocityIterations, positionIterations)

    // 2. Clear the list of collisions ready for the next physics step
    contactListener->clearNewCollisions();

    // 3. Handle screen wrapping for dynamic bodies
    const float scale = 50.0f;
    const float canvasWidthMeters = 600.0f / scale;
    const float canvasHeightMeters = 400.0f / scale;

    // Iterate through all bodies in the world
    for (b2Body* b = world->GetBodyList(); b; b = b->GetNext())
    {
        // We only care about dynamic bodies
        if (b->GetType() == b2_dynamicBody)
        {
            b2Vec2 position = b->GetPosition();
            bool needsUpdate = false;

            // Check horizontal bounds
            if (position.x > canvasWidthMeters) { position.x = 0; needsUpdate = true; }
            if (position.x < 0) { position.x = canvasWidthMeters; needsUpdate = true; }

            // Check vertical bounds
            if (position.y > canvasHeightMeters) { position.y = 0; needsUpdate = true; }
            if (position.y < 0) { position.y = canvasHeightMeters; needsUpdate = true; }

            // If the body was out of bounds, update its position
            if (needsUpdate)
            {
                // We also pass the body's current angle to preserve its rotation
                b->SetTransform(position, b->GetAngle());
            }
        }
    }
    
    // 3. Update any internal state needed for rendering or audio

    // --- Process Destruction Requests from UI Thread (FINAL, SAFE version) ---
    { // Use a scope to keep local variables clean
        // 1. Drain all requests from the lock-free queue into a temporary vector.
        std::vector<b2Body*> bodiesToDestroyThisFrame;
        int start1, size1, start2, size2;
        destructionQueue.prepareToRead(destructionQueue.getNumReady(), start1, size1, start2, size2);
        if (size1 > 0)
            bodiesToDestroyThisFrame.insert(bodiesToDestroyThisFrame.end(), destructionQueueBuffer.begin() + start1, destructionQueueBuffer.begin() + start1 + size1);
        if (size2 > 0)
            bodiesToDestroyThisFrame.insert(bodiesToDestroyThisFrame.end(), destructionQueueBuffer.begin() + start2, destructionQueueBuffer.begin() + start2 + size2);
        destructionQueue.finishedRead(size1 + size2);

        // 2. Remove all duplicate pointers. This is the core of the fix.
        std::sort(bodiesToDestroyThisFrame.begin(), bodiesToDestroyThisFrame.end());
        bodiesToDestroyThisFrame.erase(std::unique(bodiesToDestroyThisFrame.begin(), bodiesToDestroyThisFrame.end()), bodiesToDestroyThisFrame.end());

        // 3. Now, iterate through the *unique* list and safely destroy everything.
        for (auto* bodyToDestroy : bodiesToDestroyThisFrame)
        {
            if (bodyToDestroy)
            {
                // Remove from our C++ lists first
                std::erase_if(physicsObjects, [&](const auto& p) { return p && p->physicsBody == bodyToDestroy; });
                std::erase_if(userStrokes,   [&](const auto& s) { return s.physicsBody == bodyToDestroy; });

                // Now, safely destroy the body from the world ONCE.
                world->DestroyBody(bodyToDestroy);
            }
        }
    }

    // --- Handle Clear All Requests ---
    if (clearAllRequested.load())
    {
        // Clear all non-physics data structures
        forceObjects.clear();
        emitters.clear();
        selectedEmitterIndex = -1;

        // Also enqueue any remaining bodies that might not have been caught by the UI
        for (b2Body* b = world->GetBodyList(); b; b = b->GetNext())
        {
            int start1, size1, start2, size2;
            destructionQueue.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0) {
                destructionQueueBuffer[start1] = b;
                destructionQueue.finishedWrite(1);
            }
        }

        clearAllRequested = false; // Reset the flag
    }

    // --- END OF DESTRUCTION PROCESSING ---

    // 6. Calculate CV outputs (median position and velocity for each shape type)
    // Note: scale, canvasWidthMeters, and canvasHeightMeters are already defined above
    const float maxExpectedVelocity = 15.0f; // Sensible limit for velocity normalization
    
    // Temporary storage for each shape's data
    std::map<ShapeType, std::vector<float>> posX_lists, posY_lists, velX_lists, velY_lists;
    
    // Group all object data by shape type
    for (const auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody)
        {
            b2Vec2 pos = objPtr->physicsBody->GetPosition();
            b2Vec2 vel = objPtr->physicsBody->GetLinearVelocity();
            
            posX_lists[objPtr->type].push_back(pos.x);
            posY_lists[objPtr->type].push_back(pos.y);
            velX_lists[objPtr->type].push_back(vel.x);
            velY_lists[objPtr->type].push_back(vel.y);
        }
    }
    
    // Helper lambda to calculate the median of a vector
    auto calculateMedian = [](std::vector<float>& vec) -> float {
        if (vec.empty()) return 0.0f;
        std::sort(vec.begin(), vec.end());
        if (vec.size() % 2 != 0)
            return vec[vec.size() / 2];
        else
            return (vec[(vec.size() - 1) / 2] + vec[vec.size() / 2]) / 2.0f;
    };
    
    // Calculate and store the median for each shape type
    for (auto& [type, data_list] : posX_lists)
    {
        float medianPosX = calculateMedian(posX_lists[type]);
        float medianPosY = calculateMedian(posY_lists[type]);
        float medianVelX = calculateMedian(velX_lists[type]);
        float medianVelY = calculateMedian(velY_lists[type]);
        
        // Normalize and store the results atomically
        cvOutputValues[type].posX = medianPosX / canvasWidthMeters;
        cvOutputValues[type].posY = medianPosY / canvasHeightMeters;
        cvOutputValues[type].velX = juce::jlimit(-1.0f, 1.0f, medianVelX / maxExpectedVelocity);
        cvOutputValues[type].velY = juce::jlimit(-1.0f, 1.0f, medianVelY / maxExpectedVelocity);
    }
}


void PhysicsModuleProcessor::playSound(StrokeType strokeType, float impulse, float collisionX, ShapeType shapeType)
{
    const float scale = 50.0f;
    const float canvasWidthMeters = 600.0f / scale;
    float pan = collisionX / canvasWidthMeters;

    // Find the next available voice and trigger it with the simple signature
    synthVoices[nextVoice].startNote(strokeDatabase[strokeType], impulse, pan);

    nextVoice = (nextVoice + 1) % synthVoices.size();

    triggerOutputValues[0] = 1.0f;

    switch (shapeType)
    {
        case ShapeType::Circle:   triggerOutputValues[1] = 1.0f; break;
        case ShapeType::Square:   triggerOutputValues[2] = 1.0f; break;
        case ShapeType::Triangle: triggerOutputValues[3] = 1.0f; break;
    }
}

// ADD THIS - New helper function to trigger sound directly from MaterialData
void PhysicsModuleProcessor::playSoundWithMaterial(const MaterialData& material, float impulse, float mass, float collisionX)
{
    // Convert physics X-coordinate (meters) to pan value (0-1)
    const float scale = 50.0f;
    const float canvasWidthMeters = 600.0f / scale;
    float pan = collisionX / canvasWidthMeters;

    // Find the next available voice and trigger it
    synthVoices[nextVoice].startNote(material, impulse, pan);

    // Cycle to the next voice for next time (round-robin)
    nextVoice = (nextVoice + 1) % synthVoices.size();

    // Fire the main trigger for these collisions
    triggerOutputValues[0] = 1.0f;
}

#if defined(PRESET_CREATOR_UI)
void PhysicsModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded, itemWidth);
    
    auto& ap = getAPVTS();
    auto* draw_list = ImGui::GetWindowDrawList();
    const auto& theme = ThemeManager::getInstance().getCurrentTheme();
    const auto& physicsColors = theme.modules.physics;
    
    // --- Inertia Calculation ---
    // Track node movement and calculate inertial force for physics objects
    ImVec2 currentNodePosImGui = ImGui::GetCursorScreenPos();
    juce::Point<float> currentNodePos(currentNodePosImGui.x, currentNodePosImGui.y);
    float deltaTime = ImGui::GetIO().DeltaTime;
    
    if (deltaTime > 0.0f && previousNodePos.x != 0.0f) // Avoid jump on first frame
    {
        // Calculate node velocity = (current_pos - last_pos) / time
        juce::Point<float> velocity = (currentNodePos - previousNodePos) / deltaTime;
        
        // Create a force that pushes objects in the opposite direction of node movement
        // This simulates inertia - like objects sliding around when you shake a box
        const float inertiaStrength = -0.1f; // Negative = opposite direction
        const float scale = 50.0f; // Match physics scale (pixels to meters)
        inertialForce.Set(
            velocity.x * inertiaStrength / scale,
            velocity.y * inertiaStrength / scale
        );
    }
    else
    {
        inertialForce.Set(0.0f, 0.0f); // No movement = no force
    }
    previousNodePos = currentNodePos;
    
    // --- Basic UI controls ---
    ThemeText("Physics Sandbox", physicsColors.sandbox_title);
    ImGui::Spacing();
    
    // Helper lambda for color-coded buttons
    auto ColorButton = [](const char* label, bool isSelected, const ImVec4& color) -> bool {
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_Button, color);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x * 0.8f, color.y * 0.8f, color.z * 0.8f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(color.x * 0.3f, color.y * 0.3f, color.z * 0.3f, 0.6f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        }
        bool result = ImGui::Button(label);
        ImGui::PopStyleColor(4);
        return result;
    };
    
    // Stroke type selection buttons with color coding
    ThemeText("Stroke Type:", physicsColors.stroke_label);
    ImGui::SameLine();
    if (ColorButton("Metal", currentStrokeType == StrokeType::Metal, physicsColors.stroke_metal)) { currentStrokeType = StrokeType::Metal; }
    ImGui::SameLine();
    if (ColorButton("Wood", currentStrokeType == StrokeType::Wood, physicsColors.stroke_wood)) { currentStrokeType = StrokeType::Wood; }
    ImGui::SameLine();
    if (ColorButton("Soil", currentStrokeType == StrokeType::Soil, physicsColors.stroke_soil)) { currentStrokeType = StrokeType::Soil; }
    ImGui::SameLine();
    if (ColorButton("Conveyor", currentStrokeType == StrokeType::Conveyor, physicsColors.stroke_conveyor)) { currentStrokeType = StrokeType::Conveyor; }
    ImGui::SameLine();
    if (ColorButton("Bouncy", currentStrokeType == StrokeType::BouncyGoo, physicsColors.stroke_bouncy)) { currentStrokeType = StrokeType::BouncyGoo; }
    ImGui::SameLine();
    if (ColorButton("Sticky", currentStrokeType == StrokeType::StickyMud, physicsColors.stroke_sticky)) { currentStrokeType = StrokeType::StickyMud; }
    ImGui::SameLine();
    if (ColorButton("Emitter", isPlacingEmitter, physicsColors.stroke_emitter)) {
        currentForceTool.reset();
        isPlacingEmitter = true; // Activate emitter placement mode
    }
    
    // Material physics property sliders (context-sensitive based on selected material)
    
    // Helper lambda to create a styled slider for material properties
    auto createMaterialSlider = [&](const char* label, const char* paramID, const ImVec4& color) {
        auto* param = apvts.getRawParameterValue(paramID);
        if (param) {
            float value = param->load();
            ImGui::PushItemWidth(150.0f);
            
            // Style the slider with the material color
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(color.x * 0.3f, color.y * 0.3f, color.z * 0.3f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(color.x * 0.4f, color.y * 0.4f, color.z * 0.4f, 0.7f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(color.x * 0.5f, color.y * 0.5f, color.z * 0.5f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, color);
            ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(color.x * 1.2f, color.y * 1.2f, color.z * 1.2f, 1.0f));
            
            if (ImGui::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f")) {
                if (auto* p = apvts.getParameter(paramID))
                    p->setValueNotifyingHost(apvts.getParameterRange(paramID).convertTo0to1(value));
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
            
            ImGui::PopStyleColor(5);
            ImGui::PopItemWidth();
        }
    };
    
    // Show sliders based on the current stroke type (only for material types)
    if (currentStrokeType == StrokeType::Metal) {
        createMaterialSlider("Friction", "metalFriction", physicsColors.stroke_metal);
        createMaterialSlider("Bounciness", "metalRestitution", physicsColors.stroke_metal);
    } else if (currentStrokeType == StrokeType::Wood) {
        createMaterialSlider("Friction", "woodFriction", physicsColors.stroke_wood);
        createMaterialSlider("Bounciness", "woodRestitution", physicsColors.stroke_wood);
    } else if (currentStrokeType == StrokeType::Soil) {
        createMaterialSlider("Friction", "soilFriction", physicsColors.stroke_soil);
        createMaterialSlider("Bounciness", "soilRestitution", physicsColors.stroke_soil);
    }
    
    
    ImGui::Spacing();
    ThemeText("Physics Parameters", physicsColors.physics_section);
    ImGui::Spacing();
    
    // Gravity slider with modulation indicator (styled in yellow/orange)
    if (auto* gravityParam = apvts.getRawParameterValue(paramIdGravity))
    {
        const bool gravityIsMod = isParamModulated(paramIdGravityMod);
        float gravityValue = gravityIsMod
            ? getLiveParamValueFor(paramIdGravityMod, "gravity_live", gravityParam->load())
            : gravityParam->load();

        if (gravityIsMod) ImGui::BeginDisabled();

        ImGui::PushItemWidth(150.0f);
        
        // Style with gravity color (yellow/orange downward)
        ImVec4 gravityColor = gravityIsMod ? physicsColors.count_warn : physicsColors.stroke_emitter;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(gravityColor.x * 0.3f, gravityColor.y * 0.3f, gravityColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(gravityColor.x * 0.4f, gravityColor.y * 0.4f, gravityColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, gravityColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(gravityColor.x * 1.2f, gravityColor.y * 1.2f, gravityColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Gravity", &gravityValue, 0.0f, 50.0f, "%.1f"))
        {
            if (!gravityIsMod)
                if (auto* param = apvts.getParameter(paramIdGravity))
                    param->setValueNotifyingHost(apvts.getParameterRange(paramIdGravity).convertTo0to1(gravityValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !gravityIsMod) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        if (gravityIsMod)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }

    // Wind slider with modulation indicator (styled in cyan/white)
    if (auto* windParam = apvts.getRawParameterValue(paramIdWind))
    {
        const bool windIsMod = isParamModulated(paramIdWindMod);
        float windValue = windIsMod
            ? getLiveParamValueFor(paramIdWindMod, "wind_live", windParam->load())
            : windParam->load();

        if (windIsMod) ImGui::BeginDisabled();

        ImGui::PushItemWidth(150.0f);
        
        // Style with wind color (cyan/white horizontal)
        ImVec4 windColor = windIsMod ? physicsColors.count_warn : physicsColors.spawn_section;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(windColor.x * 0.3f, windColor.y * 0.3f, windColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(windColor.x * 0.4f, windColor.y * 0.4f, windColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, windColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(windColor.x * 1.2f, windColor.y * 1.2f, windColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Wind", &windValue, -50.0f, 50.0f, "%.1f"))
        {
            if (!windIsMod)
                if (auto* param = apvts.getParameter(paramIdWind))
                    param->setValueNotifyingHost(apvts.getParameterRange(paramIdWind).convertTo0to1(windValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !windIsMod) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        if (windIsMod)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }

    // Vortex Strength slider with modulation indicator (styled in purple)
    if (auto* vortexStrengthParam = apvts.getRawParameterValue(paramIdVortexStrength))
    {
        const bool vortexStrIsMod = isParamModulated(paramIdVortexStrengthMod);
        float vortexStrengthValue = vortexStrIsMod
            ? getLiveParamValueFor(paramIdVortexStrengthMod, "vortex_strength_live", vortexStrengthParam->load())
            : vortexStrengthParam->load();

        if (vortexStrIsMod) ImGui::BeginDisabled();

        ImGui::PushItemWidth(150.0f);
        
        // Style with vortex color (purple spiral)
        ImVec4 vortexColor = vortexStrIsMod ? physicsColors.count_warn : physicsColors.spawn_vortex;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(vortexColor.x * 0.3f, vortexColor.y * 0.3f, vortexColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(vortexColor.x * 0.4f, vortexColor.y * 0.4f, vortexColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, vortexColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(vortexColor.x * 1.2f, vortexColor.y * 1.2f, vortexColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Vortex Strength", &vortexStrengthValue, -100.0f, 100.0f, "%.1f"))
        {
            if (!vortexStrIsMod)
                if (auto* param = apvts.getParameter(paramIdVortexStrength))
                    param->setValueNotifyingHost(apvts.getParameterRange(paramIdVortexStrength).convertTo0to1(vortexStrengthValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !vortexStrIsMod) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        if (vortexStrIsMod)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }

    // Vortex Spin slider with modulation indicator (styled in magenta)
    if (auto* vortexSpinParam = apvts.getRawParameterValue(paramIdVortexSpin))
    {
        const bool vortexSpinIsMod = isParamModulated(paramIdVortexSpinMod);
        float vortexSpinValue = vortexSpinIsMod
            ? getLiveParamValueFor(paramIdVortexSpinMod, "vortex_spin_live", vortexSpinParam->load())
            : vortexSpinParam->load();

        if (vortexSpinIsMod) ImGui::BeginDisabled();

        ImGui::PushItemWidth(150.0f);
        
        // Style with spin color (magenta swirl)
        ImVec4 spinColor = vortexSpinIsMod ? physicsColors.count_warn : physicsColors.spawn_vortex;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(spinColor.x * 0.3f, spinColor.y * 0.3f, spinColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(spinColor.x * 0.4f, spinColor.y * 0.4f, spinColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, spinColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(spinColor.x * 1.2f, spinColor.y * 1.2f, spinColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Vortex Spin", &vortexSpinValue, -50.0f, 50.0f, "%.1f"))
        {
            if (!vortexSpinIsMod)
                if (auto* param = apvts.getParameter(paramIdVortexSpin))
                    param->setValueNotifyingHost(apvts.getParameterRange(paramIdVortexSpin).convertTo0to1(vortexSpinValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !vortexSpinIsMod) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        if (vortexSpinIsMod)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }

    // Magnet Force slider with modulation indicator (styled in red/blue)
    if (auto* magnetForceParam = apvts.getRawParameterValue(paramIdMagnetForce))
    {
        const bool magnetForceIsMod = isParamModulated(paramIdMagnetForceMod);
        float magnetForceValue = magnetForceIsMod
            ? getLiveParamValueFor(paramIdMagnetForceMod, "magnet_force_live", magnetForceParam->load())
            : magnetForceParam->load();

        if (magnetForceIsMod) ImGui::BeginDisabled();

        ImGui::PushItemWidth(150.0f);
        
        // Style with magnet color (red for positive, blue for negative)
        ImVec4 magnetColor = magnetForceIsMod ? physicsColors.count_warn : physicsColors.count_alert;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(magnetColor.x * 0.3f, magnetColor.y * 0.3f, magnetColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(magnetColor.x * 0.4f, magnetColor.y * 0.4f, magnetColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, magnetColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(magnetColor.x * 1.2f, magnetColor.y * 1.2f, magnetColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Magnet Force", &magnetForceValue, -50.0f, 50.0f, "%.1f"))
        {
            if (!magnetForceIsMod)
                if (auto* param = apvts.getParameter(paramIdMagnetForce))
                    param->setValueNotifyingHost(apvts.getParameterRange(paramIdMagnetForce).convertTo0to1(magnetForceValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && !magnetForceIsMod) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();

        if (magnetForceIsMod)
        {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ThemeText("(mod)", theme.text.active);
        }
    }

    // Stroke Size slider (styled in grey/white)
    auto* strokeSizeParam = apvts.getRawParameterValue(paramIdStrokeSize);
    if (strokeSizeParam)
    {
        float strokeSizeValue = strokeSizeParam->load();
        ImGui::PushItemWidth(150.0f);
        
    ImVec4 strokeSizeColor = physicsColors.physics_section;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(strokeSizeColor.x * 0.3f, strokeSizeColor.y * 0.3f, strokeSizeColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(strokeSizeColor.x * 0.4f, strokeSizeColor.y * 0.4f, strokeSizeColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, strokeSizeColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(strokeSizeColor.x * 1.2f, strokeSizeColor.y * 1.2f, strokeSizeColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Stroke Size", &strokeSizeValue, 1.0f, 10.0f, "%.1f"))
        {
            if (auto* param = apvts.getParameter(paramIdStrokeSize))
                param->setValueNotifyingHost(apvts.getParameterRange(paramIdStrokeSize).convertTo0to1(strokeSizeValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        ImGui::PopItemWidth();
    }
    
    ImGui::Spacing();
    ThemeText("Spawning Settings", physicsColors.spawn_section);
    ImGui::Spacing();
    
    // Max Objects, Mass, and Polarity controls (grouped with consistent width)
    ImGui::PushItemWidth(150.0f);
    if (auto* maxObjectsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdMaxObjects)))
    {
        int maxObjectsValue = maxObjectsParam->get();
        
        ImVec4 maxObjColor = physicsColors.count_ok;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(maxObjColor.x * 0.3f, maxObjColor.y * 0.3f, maxObjColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(maxObjColor.x * 0.4f, maxObjColor.y * 0.4f, maxObjColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, maxObjColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(maxObjColor.x * 1.2f, maxObjColor.y * 1.2f, maxObjColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderInt("Max Objects", &maxObjectsValue, 1, 500))
        {
            *maxObjectsParam = maxObjectsValue;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        
        ImGui::PopStyleColor(4);
        
        // Display current object count on the same line
        ImGui::SameLine();
        
        // Color code the count based on how close we are to the limit
        float ratio = (float)physicsObjects.size() / (float)maxObjectsValue;
        const ImVec4& countColor = ratio < 0.7f ? physicsColors.count_ok :
                                   ratio < 0.9f ? physicsColors.count_warn :
                                                  physicsColors.count_alert;
        ThemeText(juce::String::formatted("(%d)", (int)physicsObjects.size()).toRawUTF8(), countColor);
    }

    ImVec4 massColor = physicsColors.spawn_section;
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(massColor.x * 0.3f, massColor.y * 0.3f, massColor.z * 0.3f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(massColor.x * 0.4f, massColor.y * 0.4f, massColor.z * 0.4f, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, massColor);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(massColor.x * 1.2f, massColor.y * 1.2f, massColor.z * 1.2f, 1.0f));
    
    ImGui::SliderFloat("Spawn Mass", &currentMass, 0.1f, 10.0f, "%.2f kg", ImGuiSliderFlags_Logarithmic);
    
    ImGui::PopStyleColor(4);
    ImGui::PopItemWidth();

    ThemeText("Polarity:", physicsColors.stroke_label); 
    ImGui::SameLine();
    
    // Style polarity radio buttons
    ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.count_ok);
    if (ImGui::RadioButton("None", currentPolarity == Polarity::None)) { currentPolarity = Polarity::None; } ImGui::SameLine();
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.count_alert);
    if (ImGui::RadioButton("N", currentPolarity == Polarity::North)) { currentPolarity = Polarity::North; } ImGui::SameLine();
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.magnet_south);
    if (ImGui::RadioButton("S", currentPolarity == Polarity::South)) { currentPolarity = Polarity::South; }
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ThemeText("Spawn:", physicsColors.spawn_section);
    ImGui::SameLine();
    
    // Spawn shape buttons (Thread-Safe) with color coding
    auto enqueueSpawn = [&](ShapeType type) {
        int start1, size1, start2, size2;
        spawnQueue.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 > 0) {
            spawnQueueBuffer[start1] = { type, currentMass, currentPolarity };
            spawnQueue.finishedWrite(1);
        }
    };
    
    if (ColorButton("Ball", false, physicsColors.spawn_ball))     { enqueueSpawn(ShapeType::Circle); }
    ImGui::SameLine();
    if (ColorButton("Square", false, physicsColors.spawn_square))   { enqueueSpawn(ShapeType::Square); }
    ImGui::SameLine();
    if (ColorButton("Triangle", false, physicsColors.spawn_triangle)) { enqueueSpawn(ShapeType::Triangle); }

    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    if (ColorButton("Vortex", currentForceTool.has_value() && *currentForceTool == ForceType::Vortex, physicsColors.spawn_vortex)) {
        currentForceTool = ForceType::Vortex;
        isPlacingEmitter = false;
    }

    ImGui::SameLine();
    
    // Clear All button with warning color
    ImGui::PushStyleColor(ImGuiCol_Button, physicsColors.spawn_clear);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, physicsColors.spawn_clear_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, physicsColors.spawn_clear_active);
    
    if (ImGui::Button("Clear All"))
    {
        // --- THREAD-SAFE CLEAR ---
        // Set the clear all flag - the physics thread will handle the cleanup
        clearAllRequested = true;
    }
    
    ImGui::PopStyleColor(3);
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Get the drawing canvas area ---
    ImVec2 canvas_size(600.0f, 400.0f); // Define a fixed size for our canvas
    ImVec2 canvas_p0 = ImGui::GetCursorScreenPos(); // Top-left corner
    ImVec2 canvas_p1 = ImVec2(canvas_p0.x + canvas_size.x, canvas_p0.y + canvas_size.y);
    
    // Use an InvisibleButton to reserve the space for our canvas
    ImGui::InvisibleButton("##canvas", canvas_size);
    const bool is_hovered = ImGui::IsItemHovered(); // Hovered
    
    // Add a border to visualize the drawing area
    draw_list->AddRectFilled(canvas_p0, canvas_p1, ImGui::ColorConvertFloat4ToU32(physicsColors.canvas_background));
    draw_list->AddRect(canvas_p0, canvas_p1, ImGui::ColorConvertFloat4ToU32(physicsColors.canvas_border));
    
    // --- Mouse Input Handling ---
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos_in_canvas = ImVec2(io.MousePos.x - canvas_p0.x, io.MousePos.y - canvas_p0.y);

    // Common variables for mouse interaction
    const float scale = 50.0f;
    juce::Point<float> spawnPointPixels = { manualSpawnPoint.x * scale, manualSpawnPoint.y * scale };
    bool clickedOnSomething = false;

    // --- SPAWN POINT DRAG-AND-DROP LOGIC ---

    // Handle clicking to start a drag
    juce::Point<float> mousePos(mouse_pos_in_canvas.x, mouse_pos_in_canvas.y);
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mousePos.getDistanceFrom(spawnPointPixels) < 10.0f)
    {
        isDraggingSpawnPoint = true;
        clickedOnSomething = true; // Prevents other actions
    }

    // Handle dragging
    if (isDraggingSpawnPoint && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        manualSpawnPoint.x = mousePos.x / scale;
        manualSpawnPoint.y = mousePos.y / scale;
    }

    // Handle releasing the mouse
    if (isDraggingSpawnPoint && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDraggingSpawnPoint = false;
    }

    // --- Drawing Mode / Force Placement / Emitter Placement ---
    // Is the mouse button clicked within our canvas?
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !clickedOnSomething)
    {
        // --- Emitter Selection Logic ---

        // Check for clicks on emitters first (highest priority)
        for (int i = 0; i < emitters.size(); ++i)
        {
            juce::Point<float> emitterPos = { emitters[i].position.x * scale, emitters[i].position.y * scale };
            float distance = std::sqrt(
                std::pow(mouse_pos_in_canvas.x - emitterPos.x, 2) +
                std::pow(mouse_pos_in_canvas.y - emitterPos.y, 2)
            );
            if (distance < 10.0f) // Click radius
            {
                selectedEmitterIndex = i;
                clickedOnSomething = true;
                // Deselect other tools
                isPlacingEmitter = false;
                currentForceTool.reset();
                break;
            }
        }

        if (clickedOnSomething)
        {
            // Do nothing else if we just selected something
        }
        else if (isPlacingEmitter) // If no emitter was clicked, proceed with placement
        {
            EmitterObject newEmitter;
            newEmitter.position = { mouse_pos_in_canvas.x / scale, mouse_pos_in_canvas.y / scale };
            newEmitter.timeSinceLastSpawn = 1.0f / newEmitter.spawnRateHz; // Start fully charged so first spawn is immediate
            emitters.push_back(newEmitter);
            isPlacingEmitter = false; // Deactivate tool after placing one
            selectedEmitterIndex = (int)emitters.size() - 1; // Select the newly placed emitter
        }
        else if (currentForceTool.has_value()) // A force tool is active
        {
            forceObjects.push_back({
                { mouse_pos_in_canvas.x / scale, mouse_pos_in_canvas.y / scale },
                *currentForceTool
            });
            currentForceTool.reset(); // Deselect tool after placing
            selectedEmitterIndex = -1;
        }
        else // Otherwise, start drawing
        {
            isDrawing = true;
            currentDrawingStroke.points.clear();
            currentDrawingStroke.type = currentStrokeType; // Use the currently selected stroke type
            currentDrawingStroke.points.push_back({mouse_pos_in_canvas.x, mouse_pos_in_canvas.y});
            selectedEmitterIndex = -1;
        }
    }

    // --- EMITTER DRAG-AND-DROP LOGIC ---
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && selectedEmitterIndex != -1)
    {
        if (selectedEmitterIndex < emitters.size())
        {
            ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);

            // Apply the delta (converted from pixels to meters)
            emitters[selectedEmitterIndex].position.x += drag_delta.x / scale;
            emitters[selectedEmitterIndex].position.y += drag_delta.y / scale;

            // IMPORTANT: Reset the delta so movement is not cumulative
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);

            // Visual feedback: show drag indicator
            if (is_hovered)
            {
                ImVec2 mousePos = ImGui::GetMousePos();
        draw_list->AddCircleFilled(mousePos, 8.0f, ImGui::ColorConvertFloat4ToU32(physicsColors.drag_indicator_fill)); // drag indicator
            }
        }
    }

    // Is the user dragging the mouse?
    if (isDrawing && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        juce::Point<float> currentPos = {mouse_pos_in_canvas.x, mouse_pos_in_canvas.y};
        juce::Point<float> lastPos = currentDrawingStroke.points.back();

        // Only add a new point if we've moved a certain distance (polyline simplification)
        if (currentPos.getDistanceFrom(lastPos) > 5.0f)
        {
            currentDrawingStroke.points.push_back(currentPos);
        }
    }

    // Has the user released the mouse button?
    if (isDrawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDrawing = false;
        // If the stroke has at least two points, consider it a valid stroke and add it to our list
        if (currentDrawingStroke.points.size() > 1)
        {
            // --- THREAD-SAFE: Enqueue a request instead of calling directly ---
            int start1, size1, start2, size2;
            strokeCreationQueue.prepareToWrite(1, start1, size1, start2, size2);
            if (size1 > 0)
            {
                strokeCreationQueueBuffer[start1] = { currentDrawingStroke.points, currentDrawingStroke.type };
                strokeCreationQueue.finishedWrite(1);
            }
            // Note: The physics thread will now handle adding the stroke to userStrokes and creating the physics body
        }
        currentDrawingStroke.points.clear();
    }
    
    // --- Eraser Visual Feedback (Right Mouse Button) ---
    if (is_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        // Get absolute mouse position on screen
        ImVec2 mousePos = ImGui::GetMousePos();
        const float eraseRadius = 15.0f;

        // Draw a semi-transparent red circle around the mouse cursor to show eraser size
        draw_list->AddCircleFilled(mousePos, eraseRadius, ImGui::ColorConvertFloat4ToU32(physicsColors.eraser_fill)); // Subtle red fill
        draw_list->AddCircle(mousePos, eraseRadius, ImGui::ColorConvertFloat4ToU32(physicsColors.eraser_outline), 12, 2.0f); // Red outline
    }

    // --- Emitter Visual Feedback ---
    if (isPlacingEmitter && is_hovered)
    {
        // Get absolute mouse position on screen
        ImVec2 mousePos = ImGui::GetMousePos();

        // Draw a semi-transparent yellow square to show emitter placement
        draw_list->AddRectFilled({mousePos.x - 5, mousePos.y - 5}, {mousePos.x + 5, mousePos.y + 5}, ImGui::ColorConvertFloat4ToU32(physicsColors.drag_indicator_fill));
        draw_list->AddRect({mousePos.x - 5, mousePos.y - 5}, {mousePos.x + 5, mousePos.y + 5}, ImGui::ColorConvertFloat4ToU32(physicsColors.vector_outline));
    }
    
    // --- Eraser Mode (Thread-Safe - Right Mouse Drag) ---
    if (is_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right))
    {
        isDrawing = false; // Stop any drawing action
        juce::Point<float> mousePos = { mouse_pos_in_canvas.x, mouse_pos_in_canvas.y };
        const float eraseRadius = 15.0f;
        const float scale = 50.0f;

        // Helper to enqueue a body for destruction
        auto enqueueDestruction = [&](b2Body* body) {
            if (body) {
                int start1, size1, start2, size2;
                destructionQueue.prepareToWrite(1, start1, size1, start2, size2);
                if (size1 > 0) {
                    destructionQueueBuffer[start1] = body;
                    destructionQueue.finishedWrite(1);
                }
            }
        };

        // Enqueue Physics Objects for destruction
        for (const auto& objPtr : physicsObjects) {
            if (objPtr && objPtr->physicsBody) {
                b2Vec2 bodyPos = objPtr->physicsBody->GetPosition();
                juce::Point<float> objPos = { bodyPos.x * scale, bodyPos.y * scale };
                if (mousePos.getDistanceFrom(objPos) < eraseRadius) {
                    enqueueDestruction(objPtr->physicsBody);
                }
            }
        }

        // Enqueue Strokes for destruction
        for (const auto& stroke : userStrokes) {
            for (const auto& point : stroke.points) {
                if (mousePos.getDistanceFrom(point) < eraseRadius) {
                    enqueueDestruction(stroke.physicsBody);
                    goto next_stroke_erase; // Avoid queueing the same stroke multiple times
                }
            }
        next_stroke_erase:;
        }

        // Note: Force objects and emitters are not physics bodies, so they don't need destructionQueue
        // They can be safely erased directly since they don't affect Box2D state

        // Erase Force Objects (Direct removal - no physics bodies involved)
        const float eraseScale = 50.0f;
        for (int i = (int)forceObjects.size() - 1; i >= 0; --i) {
            juce::Point<float> forcePos = { forceObjects[i].position.x * eraseScale, forceObjects[i].position.y * eraseScale };
            if (mousePos.getDistanceFrom(forcePos) < eraseRadius) {
                forceObjects.erase(forceObjects.begin() + i);
            }
        }

        // Erase Emitters (Direct removal - no physics bodies involved)
        for (int i = (int)emitters.size() - 1; i >= 0; --i) {
            juce::Point<float> emitterPos = { emitters[i].position.x * eraseScale, emitters[i].position.y * eraseScale };
            float distance = std::sqrt(
                std::pow(mousePos.x - emitterPos.x, 2) +
                std::pow(mousePos.y - emitterPos.y, 2)
            );
            if (distance < eraseRadius) {
                if (selectedEmitterIndex == i) {
                    selectedEmitterIndex = -1; // Deselect if we're erasing the selected emitter
                } else if (selectedEmitterIndex > i) {
                    selectedEmitterIndex--; // Adjust selection index if we erased an emitter before the selected one
                }
                emitters.erase(emitters.begin() + i);
            }
        }
    }

    // --- Rendering ---
    draw_list->PushClipRect(canvas_p0, canvas_p1, true); // Don't draw outside the canvas

    // 2. Draw all placed force objects
    const float forceScale = 50.0f;
    for (const auto& force : forceObjects) {
        if (force.type == ForceType::Vortex) {
            float vtxX = canvas_p0.x + (force.position.x * forceScale);
            float vtxY = canvas_p0.y + (force.position.y * forceScale);
            float vtxStrength = *apvts.getRawParameterValue(paramIdVortexStrength);
            ImU32 color = (vtxStrength >= 0)
                ? ImGui::ColorConvertFloat4ToU32(physicsColors.magnet_south)
                : ImGui::ColorConvertFloat4ToU32(physicsColors.count_alert);
            draw_list->AddCircleFilled({vtxX, vtxY}, 10.0f, color);
            draw_list->AddCircle({vtxX, vtxY}, 10.0f, ImGui::ColorConvertFloat4ToU32(physicsColors.vector_outline));
        }
    }

    // --- RENDER SPAWN POINT CROSSHAIR ---
    ImVec2 spawnPos = { canvas_p0.x + spawnPointPixels.x, canvas_p0.y + spawnPointPixels.y };
    ImU32 crosshairColor = ImGui::ColorConvertFloat4ToU32(isDraggingSpawnPoint ? physicsColors.crosshair_active : physicsColors.crosshair_idle);
    float crosshairSize = 8.0f;
    draw_list->AddLine({spawnPos.x - crosshairSize, spawnPos.y}, {spawnPos.x + crosshairSize, spawnPos.y}, crosshairColor, 2.0f);
    draw_list->AddLine({spawnPos.x, spawnPos.y - crosshairSize}, {spawnPos.x, spawnPos.y + crosshairSize}, crosshairColor, 2.0f);
    // --- END OF SPAWN POINT RENDERING ---

    // 3. Draw all placed emitters
    const float emitterScale = 50.0f;
    for (int i = 0; i < emitters.size(); ++i)
    {
        const auto& emitter = emitters[i];
        float emitterX = canvas_p0.x + (emitter.position.x * emitterScale);
        float emitterY = canvas_p0.y + (emitter.position.y * emitterScale);

        // Highlight if selected
        ImU32 borderColor = (i == selectedEmitterIndex)
            ? ImGui::ColorConvertFloat4ToU32(physicsColors.stroke_emitter)
            : ImGui::ColorConvertFloat4ToU32(physicsColors.vector_outline);
        float thickness = (i == selectedEmitterIndex) ? 2.0f : 1.0f;

        // Draw emitter as a yellow square with selection highlighting
        draw_list->AddRectFilled({emitterX - 5, emitterY - 5}, {emitterX + 5, emitterY + 5}, ImGui::ColorConvertFloat4ToU32(physicsColors.stroke_emitter));
        draw_list->AddRect({emitterX - 5, emitterY - 5}, {emitterX + 5, emitterY + 5}, borderColor, 0, 0, thickness);

        // Add a small indicator for the shape being spawned
        ImU32 shapeColor;
        switch (emitter.shapeToSpawn)
        {
            case ShapeType::Circle:  shapeColor = ImGui::ColorConvertFloat4ToU32(physicsColors.spawn_ball); break;
            case ShapeType::Square:  shapeColor = ImGui::ColorConvertFloat4ToU32(physicsColors.spawn_square); break;
            case ShapeType::Triangle: shapeColor = ImGui::ColorConvertFloat4ToU32(physicsColors.spawn_triangle); break;
            default: shapeColor = ImGui::ColorConvertFloat4ToU32(physicsColors.vector_outline); break;
        }
        draw_list->AddCircleFilled({emitterX, emitterY - 8}, 2.0f, shapeColor);

        // Add polarity indicator (small bar above the shape indicator)
        if (emitter.polarity != Polarity::None)
        {
            ImU32 polarityColor = (emitter.polarity == Polarity::North) ?
                ImGui::ColorConvertFloat4ToU32(physicsColors.count_alert) : ImGui::ColorConvertFloat4ToU32(physicsColors.magnet_south);
            draw_list->AddRectFilled({emitterX - 2, emitterY - 12}, {emitterX + 2, emitterY - 10}, polarityColor);
        }

        // --- ADD VELOCITY ARROW VISUALIZATION ---
        if (emitter.initialVelocity.LengthSquared() > 0.01f) // Only draw if there's velocity
        {
            const float velocityScale = 5.0f; // Multiplier to make the line visible
            ImVec2 startPos = {emitterX, emitterY};
            ImVec2 endPos = {
                emitterX + emitter.initialVelocity.x * velocityScale,
                emitterY + emitter.initialVelocity.y * velocityScale
            };
            draw_list->AddLine(startPos, endPos, ImGui::ColorConvertFloat4ToU32(physicsColors.magnet_link), 2.0f);
        }
        // --- END OF VELOCITY ARROW ---
    }

    // 1. Draw all the completed strokes
    float strokeThickness = apvts.getRawParameterValue("strokeSize")->load();
    for (const auto& stroke : userStrokes)
    {
        if (stroke.points.size() > 1)
        {
            // Convert juce::Point to ImVec2 for drawing
            std::vector<ImVec2> pointsForImGui;
            for (const auto& p : stroke.points)
            {
                pointsForImGui.emplace_back(canvas_p0.x + p.x, canvas_p0.y + p.y);
            }
            
            // Get the material color and convert it to an ImGui color
            auto juceColour = strokeColourMap[stroke.type];
            ImVec4 strokeColor = ImVec4(juceColour.getFloatRed(), juceColour.getFloatGreen(), juceColour.getFloatBlue(), juceColour.getFloatAlpha());
            ImU32 imColour = ImGui::ColorConvertFloat4ToU32(strokeColor);
            
            draw_list->AddPolyline(pointsForImGui.data(), (int)pointsForImGui.size(), imColour, 0, strokeThickness);

            // Special rendering for conveyor belts - add directional arrows
            if (stroke.type == StrokeType::Conveyor)
            {
                // Draw arrows along the conveyor belt to indicate direction
                const float arrowSpacing = 30.0f; // Distance between arrows in pixels
                const float arrowSize = 8.0f;

                for (size_t i = 0; i < stroke.points.size() - 1; ++i)
                {
                    juce::Point<float> p1 = stroke.points[i];
                    juce::Point<float> p2 = stroke.points[i + 1];
                    juce::Point<float> segment = p2 - p1;
                    float segmentLength = segment.getDistanceFromOrigin();

                    if (segmentLength > 0.0f)
                    {
                        juce::Point<float> direction = segment / segmentLength;
                        int numArrows = static_cast<int>(segmentLength / arrowSpacing);

                        for (int arrow = 0; arrow <= numArrows; ++arrow)
                        {
                            float t = static_cast<float>(arrow) / numArrows;
                            juce::Point<float> arrowPos = p1 + segment * t;

                            // Calculate perpendicular vector for arrow head
                            juce::Point<float> perp(-direction.y, direction.x);

                            // Arrow head points
                            juce::Point<float> tip = arrowPos + direction * arrowSize;
                            juce::Point<float> left = arrowPos + direction * (arrowSize * 0.5f) - perp * (arrowSize * 0.3f);
                            juce::Point<float> right = arrowPos + direction * (arrowSize * 0.5f) + perp * (arrowSize * 0.3f);

                            // Convert to ImVec2
                            ImVec2 tipIm = { canvas_p0.x + tip.x, canvas_p0.y + tip.y };
                            ImVec2 leftIm = { canvas_p0.x + left.x, canvas_p0.y + left.y };
                            ImVec2 rightIm = { canvas_p0.x + right.x, canvas_p0.y + right.y };

                            // Draw arrow as triangle
                            draw_list->AddTriangleFilled(tipIm, leftIm, rightIm, ImGui::ColorConvertFloat4ToU32(physicsColors.vector_fill));
                        }
                    }
                }
            }

            // Special rendering for Bouncy Goo - add spring-like pattern
            if (stroke.type == StrokeType::BouncyGoo)
            {
                // Draw small circles along the stroke to indicate bounciness
                const float circleSpacing = 15.0f;
                const float circleSize = 3.0f;

                for (size_t i = 0; i < stroke.points.size() - 1; ++i)
                {
                    juce::Point<float> p1 = stroke.points[i];
                    juce::Point<float> p2 = stroke.points[i + 1];
                    juce::Point<float> segment = p2 - p1;
                    float segmentLength = segment.getDistanceFromOrigin();

                    if (segmentLength > 0.0f)
                    {
                        int numCircles = static_cast<int>(segmentLength / circleSpacing);

                        for (int circle = 0; circle <= numCircles; ++circle)
                        {
                            float t = static_cast<float>(circle) / numCircles;
                            juce::Point<float> circlePos = p1 + segment * t;

                            ImVec2 circleIm = { canvas_p0.x + circlePos.x, canvas_p0.y + circlePos.y };
                            draw_list->AddCircleFilled(circleIm, circleSize, ImGui::ColorConvertFloat4ToU32(physicsColors.vector_fill));
                        }
                    }
                }
            }

            // Special rendering for Sticky Mud - add texture pattern
            if (stroke.type == StrokeType::StickyMud)
            {
                // Draw small dots along the stroke to indicate stickiness
                const float dotSpacing = 12.0f;
                const float dotSize = 2.0f;

                for (size_t i = 0; i < stroke.points.size() - 1; ++i)
                {
                    juce::Point<float> p1 = stroke.points[i];
                    juce::Point<float> p2 = stroke.points[i + 1];
                    juce::Point<float> segment = p2 - p1;
                    float segmentLength = segment.getDistanceFromOrigin();

                    if (segmentLength > 0.0f)
                    {
                        int numDots = static_cast<int>(segmentLength / dotSpacing);

                        for (int dot = 0; dot <= numDots; ++dot)
                        {
                            float t = static_cast<float>(dot) / numDots;
                            juce::Point<float> dotPos = p1 + segment * t;

                            ImVec2 dotIm = { canvas_p0.x + dotPos.x, canvas_p0.y + dotPos.y };
                            draw_list->AddCircleFilled(dotIm, dotSize, ImGui::ColorConvertFloat4ToU32(physicsColors.soil_detail)); // Soil detail dots
                        }
                    }
                }
            }
        }
    }

    // 2. Draw the physics objects
    const float renderScale = 50.0f;
    for (const auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody)
        {
            b2Vec2 pos = objPtr->physicsBody->GetPosition();
            float angle = objPtr->physicsBody->GetAngle();

            // --- NEW: Calculate color based on mass ---
            const float minMass = 0.1f;
            const float maxMass = 10.0f;

            // Map the mass to a 0-1 range
            float normalizedMass = juce::jmap(objPtr->mass, minMass, maxMass, 0.0f, 1.0f);

            // Interpolate between two colors
            juce::Colour color = juce::Colours::lightblue.interpolatedWith(juce::Colours::red, normalizedMass);

            // Convert to ImGui color format
            ImVec4 overlayColour = ImVec4(color.getFloatRed(), color.getFloatGreen(), color.getFloatBlue(), color.getFloatAlpha());
            ImU32 imColour = ImGui::ColorConvertFloat4ToU32(overlayColour);
            // --- END OF NEW LOGIC ---

            if (objPtr->type == ShapeType::Circle)
            {
                ImVec2 center(canvas_p0.x + pos.x * renderScale, canvas_p0.y + pos.y * renderScale);
                // Use the new dynamic color
                draw_list->AddCircleFilled(center, objPtr->radius, imColour);
            }
            else // It's a polygon
            {
                std::vector<ImVec2> transformedPoints;
                for (const auto& vertex : objPtr->vertices)
                {
                    // Apply rotation and translation
                    float rotatedX = vertex.x * std::cos(angle) - vertex.y * std::sin(angle);
                    float rotatedY = vertex.x * std::sin(angle) + vertex.y * std::cos(angle);
                    transformedPoints.emplace_back(
                        canvas_p0.x + (pos.x + rotatedX) * renderScale,
                        canvas_p0.y + (pos.y + rotatedY) * renderScale
                    );
                }
                // Use the new dynamic color
                draw_list->AddConvexPolyFilled(transformedPoints.data(), (int)transformedPoints.size(), imColour);
            }

            // --- ADD POLARITY SYMBOL VISUALIZATION ---
            if (objPtr->polarity != Polarity::None)
            {
                ImVec2 center(canvas_p0.x + pos.x * renderScale, canvas_p0.y + pos.y * renderScale);
                const char* symbol = (objPtr->polarity == Polarity::North) ? "+" : "-";

                // Center the text
                ImVec2 textSize = ImGui::CalcTextSize(symbol);
                ImVec2 textPos = { center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f };

                draw_list->AddText(textPos, ImGui::ColorConvertFloat4ToU32(physicsColors.overlay_text), symbol);
            }
            // --- END OF POLARITY SYMBOL ---
        }
    }

    // 3. Draw the stroke currently in progress
    if (isDrawing && currentDrawingStroke.points.size() > 1)
    {
        std::vector<ImVec2> pointsForImGui;
        for (const auto& p : currentDrawingStroke.points)
        {
            pointsForImGui.emplace_back(canvas_p0.x + p.x, canvas_p0.y + p.y);
        }
        draw_list->AddPolyline(pointsForImGui.data(), (int)pointsForImGui.size(), ImGui::ColorConvertFloat4ToU32(physicsColors.overlay_line), 0, 1.5f);
    }

    // 4. Draw eraser cursor when right mouse button is down
    if (is_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        ImVec2 eraserCenter(canvas_p0.x + mouse_pos_in_canvas.x, canvas_p0.y + mouse_pos_in_canvas.y);
        const float eraseRadius = 15.0f;
        draw_list->AddCircleFilled(eraserCenter, eraseRadius, ImGui::ColorConvertFloat4ToU32(physicsColors.eraser_fill)); // Subtle fill
        draw_list->AddCircle(eraserCenter, eraseRadius, ImGui::ColorConvertFloat4ToU32(physicsColors.eraser_outline), 0, 2.0f); // Outline
    }

    draw_list->PopClipRect();

    // --- Emitter Editor Panel ---
    if (selectedEmitterIndex != -1 && selectedEmitterIndex < emitters.size())
    {
        ImGui::Spacing();
        
        // Draw a colored line separator
        ImDrawList* separator_draw_list = ImGui::GetWindowDrawList();
        ImVec2 separator_pos = ImGui::GetCursorScreenPos();
        ImVec2 separator_end = ImVec2(separator_pos.x + 600.0f, separator_pos.y);
        separator_draw_list->AddLine(separator_pos, separator_end, ImGui::ColorConvertFloat4ToU32(physicsColors.separator_line), 2.0f);
        ImGui::Dummy(ImVec2(0, 2));
        
        ImGui::Spacing();
        ThemeText("Emitter Settings", physicsColors.stroke_emitter);
        ImGui::Spacing();
        
        auto& emitter = emitters[selectedEmitterIndex];

        // Wrap all sliders in consistent width
        ImGui::PushItemWidth(150.0f);

        // Spawn Rate Slider (styled in yellow)
        ImVec4 rateColor = physicsColors.stroke_emitter;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(rateColor.x * 0.3f, rateColor.y * 0.3f, rateColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(rateColor.x * 0.4f, rateColor.y * 0.4f, rateColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, rateColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(rateColor.x * 1.2f, rateColor.y * 1.2f, rateColor.z * 1.2f, 1.0f));
        
        if (ImGui::SliderFloat("Spawn Rate", &emitter.spawnRateHz, 0.1f, 30.0f, "%.2f Hz", ImGuiSliderFlags_Logarithmic))
        {
            // No need for onModificationEnded since this doesn't affect physics bodies
        }
        
        ImGui::PopStyleColor(4);

        // Shape Type Combo Box (styled)
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.3f, 0.3f, 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(rateColor.x * 0.5f, rateColor.y * 0.5f, rateColor.z * 0.5f, 0.8f));
        
        const char* items[] = { "Ball", "Square", "Triangle" };
        int currentItem = static_cast<int>(emitter.shapeToSpawn);
        if (ImGui::Combo("Shape", &currentItem, items, IM_ARRAYSIZE(items)))
        {
            emitter.shapeToSpawn = static_cast<ShapeType>(currentItem);
        }
        
        ImGui::PopStyleColor(3);

        // Initial Velocity Sliders (styled in cyan)
        ImVec4 velColor = physicsColors.spawn_section;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(velColor.x * 0.3f, velColor.y * 0.3f, velColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(velColor.x * 0.4f, velColor.y * 0.4f, velColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, velColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(velColor.x * 1.2f, velColor.y * 1.2f, velColor.z * 1.2f, 1.0f));
        
        ImGui::SliderFloat("Velocity X", &emitter.initialVelocity.x, -10.0f, 10.0f, "%.2f m/s");
        ImGui::SliderFloat("Velocity Y", &emitter.initialVelocity.y, -10.0f, 10.0f, "%.2f m/s");
        
        ImGui::PopStyleColor(4);

        // Mass Slider (styled in gold)
        ImVec4 emitterMassColor = physicsColors.stroke_label;
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(emitterMassColor.x * 0.3f, emitterMassColor.y * 0.3f, emitterMassColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(emitterMassColor.x * 0.4f, emitterMassColor.y * 0.4f, emitterMassColor.z * 0.4f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, emitterMassColor);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(emitterMassColor.x * 1.2f, emitterMassColor.y * 1.2f, emitterMassColor.z * 1.2f, 1.0f));
        
        ImGui::SliderFloat("Mass", &emitter.mass, 0.1f, 10.0f, "%.2f kg", ImGuiSliderFlags_Logarithmic);
        
        ImGui::PopStyleColor(4);

        // Polarity Radio Buttons (styled)
        ThemeText("Polarity:", physicsColors.stroke_label);
        ImGui::SameLine();
        int polarityInt = static_cast<int>(emitter.polarity);
        
        ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.count_ok);
        if (ImGui::RadioButton("None##emitter", polarityInt == 0)) { emitter.polarity = Polarity::None; }
        ImGui::SameLine();
        ImGui::PopStyleColor();
        
        ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.count_alert);
        if (ImGui::RadioButton("N##emitter", polarityInt == 1)) { emitter.polarity = Polarity::North; }
        ImGui::SameLine();
        ImGui::PopStyleColor();
        
        ImGui::PushStyleColor(ImGuiCol_CheckMark, physicsColors.magnet_south);
        if (ImGui::RadioButton("S##emitter", polarityInt == 2)) { emitter.polarity = Polarity::South; }
        ImGui::PopStyleColor();

        ImGui::PopItemWidth();
    }
}
#endif

// --- State Management Implementation ---

juce::ValueTree PhysicsModuleProcessor::getExtraStateTree() const
{
    // Create a main node for our state
    juce::ValueTree state("PhysicsState");

    // --- 1. Save all user-drawn strokes ---
    juce::ValueTree strokesNode("Strokes");
    for (const auto& stroke : userStrokes)
    {
        juce::ValueTree strokeNode("Stroke");
        strokeNode.setProperty("type", (int)stroke.type, nullptr);
        strokeNode.setProperty("conveyorDirX", stroke.conveyorDirection.x, nullptr);
        strokeNode.setProperty("conveyorDirY", stroke.conveyorDirection.y, nullptr);

        // Convert the vector of points into a single string for easy storage
        juce::String pointsString;
        for (const auto& p : stroke.points)
            pointsString += juce::String(p.x) + "," + juce::String(p.y) + ";";
        strokeNode.setProperty("points", pointsString, nullptr);
        strokesNode.addChild(strokeNode, -1, nullptr);
    }
    state.addChild(strokesNode, -1, nullptr);

    // --- 2. Save all dynamic physics objects ---
    juce::ValueTree objectsNode("PhysicsObjects");
    for (const auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody)
        {
            juce::ValueTree objNode("Object");
            b2Vec2 pos = objPtr->physicsBody->GetPosition();
            b2Vec2 vel = objPtr->physicsBody->GetLinearVelocity();
            float angle = objPtr->physicsBody->GetAngle();

            objNode.setProperty("type", (int)objPtr->type, nullptr);
            objNode.setProperty("posX", pos.x, nullptr);
            objNode.setProperty("posY", pos.y, nullptr);
            objNode.setProperty("velX", vel.x, nullptr);
            objNode.setProperty("velY", vel.y, nullptr);
            objNode.setProperty("angle", angle, nullptr);
            objNode.setProperty("mass", objPtr->mass, nullptr);
            objNode.setProperty("polarity", (int)objPtr->polarity, nullptr);
            
            // Save vertices for polygons
            if (objPtr->type != ShapeType::Circle)
            {
                juce::String verticesString;
                for (const auto& v : objPtr->vertices)
                    verticesString += juce::String(v.x) + "," + juce::String(v.y) + ";";
                objNode.setProperty("vertices", verticesString, nullptr);
            }
            else
            {
                objNode.setProperty("radius", objPtr->radius, nullptr);
            }
            
            objectsNode.addChild(objNode, -1, nullptr);
        }
    }
    state.addChild(objectsNode, -1, nullptr);

    // --- 3. Save all placed force objects ---
    juce::ValueTree forcesNode("ForceObjects");
    for (const auto& force : forceObjects) {
        juce::ValueTree forceNode("Force");
        forceNode.setProperty("type", (int)force.type, nullptr);
        forceNode.setProperty("posX", force.position.x, nullptr);
        forceNode.setProperty("posY", force.position.y, nullptr);
        forcesNode.addChild(forceNode, -1, nullptr);
    }
    state.addChild(forcesNode, -1, nullptr);

    // --- 4. Save all placed emitters ---
    juce::ValueTree emittersNode("Emitters");
    for (const auto& emitter : emitters) {
        juce::ValueTree emitterNode("Emitter");
        emitterNode.setProperty("posX", emitter.position.x, nullptr);
        emitterNode.setProperty("posY", emitter.position.y, nullptr);
        emitterNode.setProperty("rate", emitter.spawnRateHz, nullptr);
        emitterNode.setProperty("shape", (int)emitter.shapeToSpawn, nullptr);
        emitterNode.setProperty("velX", emitter.initialVelocity.x, nullptr);
        emitterNode.setProperty("velY", emitter.initialVelocity.y, nullptr);
        emitterNode.setProperty("mass", emitter.mass, nullptr);
        emitterNode.setProperty("polarity", (int)emitter.polarity, nullptr);
        emittersNode.addChild(emitterNode, -1, nullptr);
    }
    state.addChild(emittersNode, -1, nullptr);

    // --- 5. Save the draggable spawn point position ---
    state.setProperty("spawnPointX", manualSpawnPoint.x, nullptr);
    state.setProperty("spawnPointY", manualSpawnPoint.y, nullptr);

    return state;
}

void PhysicsModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.hasType("PhysicsState"))
        return;

    // --- 1. Clear the current simulation state ---
    // Use robust approach: iterate through world's body list directly
    std::vector<b2Body*> bodiesToDestroy;
    for (b2Body* b = world->GetBodyList(); b; b = b->GetNext())
    {
        bodiesToDestroy.push_back(b);
    }

    for (auto* body : bodiesToDestroy)
    {
        world->DestroyBody(body);
    }

    // Clear our data structures (safe because bodies are already destroyed)
    physicsObjects.clear();
    userStrokes.clear();
    forceObjects.clear();
    emitters.clear();
    selectedEmitterIndex = -1; // Reset selection

    // --- 2. Load all strokes ---
    if (auto strokesNode = state.getChildWithName("Strokes"); strokesNode.isValid())
    {
        for (auto strokeNode : strokesNode)
        {
            Stroke newStroke;
            newStroke.type = (StrokeType)(int)strokeNode.getProperty("type", 0);
            newStroke.conveyorDirection = {
                (float)strokeNode.getProperty("conveyorDirX", 0.0),
                (float)strokeNode.getProperty("conveyorDirY", 0.0)
            };

            // Parse the points string back into a vector of points
            juce::StringArray pairs;
            pairs.addTokens(strokeNode.getProperty("points").toString(), ";", "");
            for (const auto& pair : pairs)
            {
                juce::StringArray xy = juce::StringArray::fromTokens(pair, ",", "");
                if (xy.size() == 2)
                    newStroke.points.emplace_back(xy[0].getFloatValue(), xy[1].getFloatValue());
            }
            
            if (!newStroke.points.empty())
            {
                userStrokes.push_back(newStroke);
                // Recreate the physics body from the loaded data
                createStrokeBody(userStrokes.back());
            }
        }
    }

    // --- 3. Load all physics objects ---
    if (auto objectsNode = state.getChildWithName("PhysicsObjects"); objectsNode.isValid())
    {
        for (auto objNode : objectsNode)
        {
            ShapeType type = (ShapeType)(int)objNode.getProperty("type", 0);
            b2Vec2 pos((float)objNode.getProperty("posX", 0.0),
                       (float)objNode.getProperty("posY", 0.0));
            b2Vec2 vel((float)objNode.getProperty("velX", 0.0),
                       (float)objNode.getProperty("velY", 0.0));
            float angle = (float)objNode.getProperty("angle", 0.0);
            float mass = (float)objNode.getProperty("mass", 1.0);
            Polarity polarity = (Polarity)(int)objNode.getProperty("polarity", 0); // Load polarity from saved data

            spawnObject(type, mass, pos, vel, polarity); // Pass the loaded polarity
            
            // Restore the angle
            if (!physicsObjects.empty() && physicsObjects.back() && physicsObjects.back()->physicsBody)
            {
                physicsObjects.back()->physicsBody->SetTransform(pos, angle);
                physicsObjects.back()->physicsBody->SetLinearVelocity(vel);
            }
        }
    }
    
    // Also support loading old "Balls" format for backward compatibility
    if (auto ballsNode = state.getChildWithName("Balls"); ballsNode.isValid())
    {
        for (auto ballNode : ballsNode)
        {
            b2Vec2 pos((float)ballNode.getProperty("posX", 0.0),
                       (float)ballNode.getProperty("posY", 0.0));
            b2Vec2 vel((float)ballNode.getProperty("velX", 0.0),
                       (float)ballNode.getProperty("velY", 0.0));
            float mass = (float)ballNode.getProperty("mass", 1.0); // Load mass if saved, otherwise default
            spawnObject(ShapeType::Circle, mass, pos, vel, Polarity::None); // Default polarity for loaded balls
        }
    }

    // --- 4. Load all force objects ---
    if (auto forcesNode = state.getChildWithName("ForceObjects"); forcesNode.isValid()) {
        for (auto forceNode : forcesNode) {
            forceObjects.push_back({
                { (float)forceNode.getProperty("posX", 0.0), (float)forceNode.getProperty("posY", 0.0) },
                (ForceType)(int)forceNode.getProperty("type", 0)
            });
        }
    }

    // --- 5. Load all emitters ---
    if (auto emittersNode = state.getChildWithName("Emitters"); emittersNode.isValid()) {
        for (auto emitterNode : emittersNode) {
            EmitterObject loadedEmitter;
            loadedEmitter.position = { (float)emitterNode.getProperty("posX", 0.0), (float)emitterNode.getProperty("posY", 0.0) };
            loadedEmitter.shapeToSpawn = (ShapeType)(int)emitterNode.getProperty("shape", 0);
            loadedEmitter.spawnRateHz = (float)emitterNode.getProperty("rate", 1.0);
            loadedEmitter.initialVelocity = { (float)emitterNode.getProperty("velX", 0.0), (float)emitterNode.getProperty("velY", 0.0) };
            loadedEmitter.mass = (float)emitterNode.getProperty("mass", 1.0);
            loadedEmitter.polarity = (Polarity)(int)emitterNode.getProperty("polarity", 0);
            loadedEmitter.timeSinceLastSpawn = 1.0f / loadedEmitter.spawnRateHz; // Start fully charged
            emitters.push_back(loadedEmitter);
        }
    }

    // --- 6. Load the draggable spawn point position ---
    manualSpawnPoint.x = (float)state.getProperty("spawnPointX", (600.0f / 2.0f) / 50.0f);
    manualSpawnPoint.y = (float)state.getProperty("spawnPointY", 10.0f / 50.0f);
}

// --- Helper Functions ---

void PhysicsModuleProcessor::spawnObject(ShapeType type, float mass, b2Vec2 position, b2Vec2 velocity, Polarity polarity)
{
    // --- Enforce the Max Objects Limit ---
    auto* maxObjectsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter(paramIdMaxObjects));
    if (maxObjectsParam)
    {
        int maxObjects = maxObjectsParam->get();
        // Check if we are at or over the limit
        while (static_cast<int>(physicsObjects.size()) >= maxObjects)
        {
            // Enqueue the oldest object for destruction and remove it from our list
            if (physicsObjects.front() && physicsObjects.front()->physicsBody) {
                int start1, size1, start2, size2;
                destructionQueue.prepareToWrite(1, start1, size1, start2, size2);
                if (size1 > 0) {
                    destructionQueueBuffer[start1] = physicsObjects.front()->physicsBody;
                    destructionQueue.finishedWrite(1);
                }
            }
            physicsObjects.pop_front();
        }
    }
    
    // Create the new object as a unique_ptr (stable memory address)
    auto newObject = std::make_unique<PhysicsObject>();
    newObject->type = type;
    newObject->mass = mass; // Store the mass for later use
    newObject->polarity = polarity; // Store the polarity for future electromagnetic features
    newObject->materialData = shapeMaterialDatabase[type]; // ADD THIS - Assign the default material data

    // If no position is provided (i.e., a manual spawn), use the draggable spawn point.
    if (position.x == 0 && position.y == 0)
    {
        position = manualSpawnPoint;
    }

    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = position;
    bodyDef.linearVelocity = velocity;

    b2Body* body = world->CreateBody(&bodyDef);

    b2PolygonShape polygonShape;
    b2CircleShape circleShape;

    // Calculate area and density based on mass
    const float size = 10.0f; // Size in pixels
    float area = 0.0f;

    if (type == ShapeType::Circle)
    {
        newObject->radius = size;
        float radiusMeters = size / 50.0f; // Convert pixels to meters
        area = b2_pi * radiusMeters * radiusMeters;
    }
    else // It's a polygon
    {
        float scaledSize = size / 50.0f; // Convert pixels to meters
        if (type == ShapeType::Square)
        {
            polygonShape.SetAsBox(scaledSize, scaledSize);
            area = 4.0f * scaledSize * scaledSize; // Area of square
            // Store vertices for rendering (in local space)
            newObject->vertices = {
                {-scaledSize, -scaledSize},
                {scaledSize, -scaledSize},
                {scaledSize, scaledSize},
                {-scaledSize, scaledSize}
            };
        }
        else if (type == ShapeType::Triangle)
        {
            b2Vec2 points[3] = {
                {0, -scaledSize},
                {scaledSize, scaledSize},
                {-scaledSize, scaledSize}
            };
            polygonShape.Set(points, 3);
            area = 0.5f * (2.0f * scaledSize) * scaledSize; // Area of equilateral triangle
            newObject->vertices.assign(points, points + 3);
        }
    }

    // Calculate density from mass and area
    float density = (area > 0.001f) ? (mass / area) : 1.0f;

    b2FixtureDef fixtureDef;
    fixtureDef.density = density;
    fixtureDef.friction = 0.4f;
    fixtureDef.restitution = 0.6f;

    if (type == ShapeType::Circle)
    {
        circleShape.m_radius = size / 50.0f; // Convert pixels to meters
        fixtureDef.shape = &circleShape;
    }
    else
    {
        fixtureDef.shape = &polygonShape;
    }

    body->CreateFixture(&fixtureDef);
    newObject->physicsBody = body;
    
    // Get the raw pointer BEFORE moving the unique_ptr into the vector
    auto* rawPtr = newObject.get();
    
    // Move the unique_ptr into the vector (ownership transfer)
    physicsObjects.push_back(std::move(newObject));
    
    // The raw pointer is stable and can be safely stored in Box2D userData
    rawPtr->physicsBody->GetUserData().pointer = reinterpret_cast<uintptr_t>(rawPtr);
}

void PhysicsModuleProcessor::createStrokeBody(Stroke& stroke)
{
    // A stroke needs at least two points to form a line
    if (stroke.points.size() < 2)
        return;

    // Get the stroke thickness from our parameter
    auto* strokeSizeParam = apvts.getRawParameterValue(paramIdStrokeSize);
    float strokeThickness = strokeSizeParam ? strokeSizeParam->load() : 3.0f;
    
    // Get the physics properties for this stroke type
    const char* frictionParamId = "metalFriction";
    const char* restitutionParamId = "metalRestitution";
    if (stroke.type == StrokeType::Wood) {
        frictionParamId = "woodFriction";
        restitutionParamId = "woodRestitution";
    } else if (stroke.type == StrokeType::Soil) {
        frictionParamId = "soilFriction";
        restitutionParamId = "soilRestitution";
    }

    auto* frictionParam = apvts.getRawParameterValue(frictionParamId);
    auto* restitutionParam = apvts.getRawParameterValue(restitutionParamId);

    float friction = frictionParam ? frictionParam->load() : 0.5f;
    float restitution = restitutionParam ? restitutionParam->load() : 0.3f;

    // Override physics properties for functional stroke types
    if (stroke.type == StrokeType::Conveyor) {
        // Conveyor has smooth, low-friction surface
        friction = 0.1f;
        restitution = 0.3f;
    } else if (stroke.type == StrokeType::BouncyGoo) {
        // Bouncy goo has low friction but high restitution (restitution boosted in PreSolve)
        friction = 0.2f;
        restitution = 0.8f;
    } else if (stroke.type == StrokeType::StickyMud) {
        // Sticky mud has high friction and low restitution (damping handled in contact listener)
        friction = 0.9f;
        restitution = 0.1f;
    }

    // Calculate and store conveyor direction if applicable
    if (stroke.type == StrokeType::Conveyor && stroke.points.size() > 1)
    {
        // Get direction from the first to the last point
        juce::Point<float> direction = stroke.points.back() - stroke.points.front();
        float length = direction.getDistanceFromOrigin();
        if (length > 0.0f)
        {
            stroke.conveyorDirection = direction / length;
        }
        else
        {
            stroke.conveyorDirection = { 1.0f, 0.0f }; // Default to right direction
        }
    }
    
    float halfThickness = (strokeThickness / 2.0f) / 50.0f; // Convert pixels to meters

    // 1. Create a single static body to hold all our shapes
    b2BodyDef bodyDef;
    bodyDef.type = b2_staticBody;
    b2Body* body = world->CreateBody(&bodyDef);
    
    // Attach a pointer to the parent Stroke object so we can identify it in collisions
    body->GetUserData().pointer = reinterpret_cast<uintptr_t>(&stroke);

    // 2. Create rectangular segments between consecutive points
    // This gives the stroke physical thickness and prevents tunneling
    for (size_t i = 0; i < stroke.points.size() - 1; ++i)
    {
        juce::Point<float> p1 = stroke.points[i];
        juce::Point<float> p2 = stroke.points[i + 1];

        // Calculate the center, length, and angle of the segment
        juce::Point<float> center = (p1 + p2) * 0.5f;
        float length = p1.getDistanceFrom(p2);
        float angle = std::atan2(p2.y - p1.y, p2.x - p1.x);

        // Create a rectangular box for this segment
        b2PolygonShape boxShape;
        boxShape.SetAsBox(
            length / 2.0f / 50.0f,                    // Half-width (length of segment, pixels to meters)
            halfThickness,                            // Half-height (half stroke thickness)
            { center.x / 50.0f, center.y / 50.0f },   // Center position in world space (pixels to meters)
            angle                                     // Rotation angle in radians
        );

        // Attach the box to our body with material properties
        b2FixtureDef fixtureDef;
        fixtureDef.shape = &boxShape;
        fixtureDef.friction = friction;
        fixtureDef.restitution = restitution;
        body->CreateFixture(&fixtureDef);
    }

    // 3. Create circular joints at each point to smooth the connections
    // This prevents gaps at corners and makes collisions feel natural
    for (const auto& point : stroke.points)
    {
        b2CircleShape circleShape;
        circleShape.m_radius = halfThickness;
        circleShape.m_p.Set(point.x / 50.0f, point.y / 50.0f); // Convert pixels to meters

        // Attach the circle to our body with material properties
        b2FixtureDef fixtureDef;
        fixtureDef.shape = &circleShape;
        fixtureDef.friction = friction;
        fixtureDef.restitution = restitution;
        body->CreateFixture(&fixtureDef);
    }

    // Store the pointer to the physics body in our stroke object for later reference
    stroke.physicsBody = body;
}
