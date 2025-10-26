#include "PhysicsModuleProcessor.h"

#if defined(PRESET_CREATOR_UI)
#include "../../preset_creator/ImGuiNodeEditorComponent.h" // For ImGui calls
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

    // Called when two shapes first touch
    void BeginContact(b2Contact* contact) override
    {
        juce::Logger::writeToLog("BeginContact: A new contact has occurred.");

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
                // Store this new collision pair to be processed in PostSolve
                newCollisionsThisStep.push_back({ stroke, object });
                juce::Logger::writeToLog("  -> Stored collision pair for PostSolve.");
            }
        }
    }

    // Called after the physics engine calculates the collision response
    void PostSolve(b2Contact* contact, const b2ContactImpulse* impulse) override
    {
        float totalImpulse = 0.0f;
        for (int i = 0; i < impulse->count; ++i)
        {
            totalImpulse += impulse->normalImpulses[i];
        }

        juce::Logger::writeToLog("PostSolve: totalImpulse = " + juce::String(totalImpulse));

        // We only care about significant impacts
        if (totalImpulse > 0.1f)
        {
            // Check if this contact matches one of the new ones we detected in BeginContact
            for (const auto& collision : newCollisionsThisStep)
            {
                b2Body* objectBody = contact->GetFixtureA()->GetBody()->GetType() == b2_dynamicBody
                                       ? contact->GetFixtureA()->GetBody()
                                       : contact->GetFixtureB()->GetBody();
                
                auto* currentObject = reinterpret_cast<PhysicsObject*>(objectBody->GetUserData().pointer);

                if (currentObject == collision.object)
                {
                    // --- COOLDOWN CHECK: Prevent contact jitter spam ---
                    const juce::uint32 currentTimeMs = juce::Time::getMillisecondCounter();
                    const juce::uint32 cooldownMs = 50; // 50ms cooldown between sounds per object

                    if (currentTimeMs > currentObject->lastSoundTimeMs + cooldownMs)
                    {
                        b2WorldManifold worldManifold;
                        contact->GetWorldManifold(&worldManifold);
                        
                        juce::Logger::writeToLog("PostSolve: Match found! Calling playSound with impulse = " + juce::String(totalImpulse));
                        
                        // Cooldown has passed - trigger the sound
                        processor->playSound(collision.stroke->material,
                                             totalImpulse,
                                             worldManifold.points[0].x, // Collision X position for panning
                                             collision.object->type);
                        
                        // Update the object's timestamp to start the cooldown
                        currentObject->lastSoundTimeMs = currentTimeMs;
                    }
                    else
                    {
                        juce::Logger::writeToLog("PostSolve: Cooldown active, skipping sound.");
                    }
                    
                    // Break to avoid triggering the same sound multiple times if there are multiple contact points
                    break; 
                }
            }
        }
    }

    // Call this every frame to clear the list of new collisions
    void clearNewCollisions()
    {
        newCollisionsThisStep.clear();
    }

private:
    PhysicsModuleProcessor* processor;
    std::vector<CollisionInfo> newCollisionsThisStep;
};

// ==============================================================================
// PhysicsModuleProcessor Implementation
// ==============================================================================

PhysicsModuleProcessor::PhysicsModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withInput("Input", juce::AudioChannelSet::discreteChannels(3), true) // 3x Spawn Triggers
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(6), true)), // L, R, 4x Triggers
      apvts(*this, nullptr, "PhysicsParams", createParameterLayout())
{
    // Define the physics world with some gravity
    b2Vec2 gravity(0.0f, 9.8f);
    world = std::make_unique<b2World>(gravity);
    
    // Create and set the contact listener
    contactListener = std::make_unique<PhysicsContactListener>(this);
    world->SetContactListener(contactListener.get());
    
    // Set up your material sound database
    materialDatabase[MaterialType::Metal] = { {1.0f, 2.76f, 5.4f}, 1.5f, 500.0f };
    materialDatabase[MaterialType::Wood]  = { {1.0f, 1.8f}, 0.2f, 250.0f };
    materialDatabase[MaterialType::Soil]  = { {1.0f}, 0.05f, 100.0f };
    
    // Define the colors for our materials
    materialColourMap[MaterialType::Metal] = juce::Colours::lightblue;
    materialColourMap[MaterialType::Wood]  = juce::Colours::sandybrown;
    materialColourMap[MaterialType::Soil]  = juce::Colours::darkgreen;

    // Initialize trigger outputs (Main, Ball, Square, Triangle)
    for (auto& val : triggerOutputValues)
        val = 0.0f;
    
    // Initialize thread-safe spawn queue buffer (larger size for high trigger rates)
    spawnQueueBuffer.resize(256);

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
    for (auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody && world)
        {
            world->DestroyBody(objPtr->physicsBody);
            objPtr->physicsBody = nullptr;
        }
    }
    
    for (auto& stroke : userStrokes)
    {
        if (stroke.physicsBody && world)
        {
            world->DestroyBody(stroke.physicsBody);
            stroke.physicsBody = nullptr;
        }
    }
    
    // The unique_ptr for the world will handle cleanup
}

juce::AudioProcessorValueTreeState::ParameterLayout PhysicsModuleProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Add parameters here as needed (e.g., gravity, wind, stroke size, etc.)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "gravity", "Gravity",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f), 9.8f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "wind", "Wind",
        juce::NormalisableRange<float>(-20.0f, 20.0f, 0.1f), 0.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "strokeSize", "Stroke Size",
        juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 3.0f));
    
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        "maxObjects", "Max Objects",
        1, 500, 100));  // Min=1, Max=500, Default=100
    
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
    // Define 3 input trigger pins for spawning shapes
    return {
        { "Spawn Ball",     0, PinDataType::Gate },      // Trigger to spawn circle
        { "Spawn Square",   1, PinDataType::Gate },      // Trigger to spawn square
        { "Spawn Triangle", 2, PinDataType::Gate }       // Trigger to spawn triangle
    };
}

std::vector<DynamicPinInfo> PhysicsModuleProcessor::getDynamicOutputPins() const
{
    // Define all 6 output pins for the Physics module
    return {
        { "Out L",          0, PinDataType::Audio },     // Left audio channel
        { "Out R",          1, PinDataType::Audio },     // Right audio channel
        { "Main Trigger",   2, PinDataType::Gate },      // Main trigger (all collisions)
        { "Ball Trigger",   3, PinDataType::Gate },      // Ball/Circle trigger
        { "Square Trigger", 4, PinDataType::Gate },      // Square trigger
        { "Triangle Trig",  5, PinDataType::Gate }       // Triangle trigger
    };
}

void PhysicsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    const int numInputChannels = getTotalNumInputChannels();
    
    // --- Input Trigger Detection (BEFORE clearing buffer!) ---
    // Read spawn trigger inputs (channels 0-2) and detect rising edges
    if (numInputChannels >= 3 && numSamples > 0)
    {
        for (int i = 0; i < 3; ++i) // 0=Ball, 1=Square, 2=Triangle
        {
            if (numChannels > i)
            {
                auto* channelData = buffer.getReadPointer(i);
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
                        spawnQueueBuffer[start1] = typeToSpawn;
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
                    
                    // Debug: Log first non-zero sample (throttled to avoid spam)
                    if (sample == 0 && outputSample != 0.0f && debugCounter++ % 100 == 0)
                    {
                        DBG("Physics: Audio sample = " << outputSample 
                            << " L=" << leftOut << " R=" << rightOut);
                    }
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
            // Debug: Log trigger firing
            static const char* triggerNames[] = { "Main", "Ball", "Square", "Triangle" };
            DBG("Physics: Trigger " << triggerNames[i] << " fired with value " << triggerValue);
            
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
            spawnObject(spawnQueueBuffer[start1 + i]);
        }
    }
    
    // Process second chunk (if queue wrapped around)
    if (size2 > 0)
    {
        for (int i = 0; i < size2; ++i)
        {
            spawnObject(spawnQueueBuffer[start2 + i]);
        }
    }
    
    spawnQueue.finishedRead(size1 + size2);
    
    // Get the gravity parameter from the APVTS and apply it
    auto* gravityParam = apvts.getRawParameterValue("gravity");
    if (gravityParam)
    {
        world->SetGravity({ 0.0f, gravityParam->load() });
    }
    
    // Apply wind and inertial forces to all dynamic objects
    auto* windParam = apvts.getRawParameterValue("wind");
    b2Vec2 windForce(0.0f, 0.0f);
    if (windParam)
    {
        windForce.Set(windParam->load(), 0.0f);
    }
    
    // Combine wind and inertial force (from node movement)
    b2Vec2 totalForce = windForce + inertialForce;
    
    // Apply combined force to all dynamic objects
    for (auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody && objPtr->physicsBody->GetType() == b2_dynamicBody)
        {
            objPtr->physicsBody->ApplyForceToCenter(totalForce, true);
        }
    }
    
    // 1. Step the physics world forward in time
    world->Step(1.0f / 60.0f, 8, 3); // (timeStep, velocityIterations, positionIterations)

    // 2. Handle screen wrapping for dynamic bodies
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
    
    // 4. Clear the list of collisions ready for the next physics step
    contactListener->clearNewCollisions();
    
    // 5. Now it's safe to destroy bodies (world->Step() has finished)
    for (auto* body : bodiesToDestroy)
    {
        if (body && world)
        {
            world->DestroyBody(body);
        }
    }
    bodiesToDestroy.clear();
}

void PhysicsModuleProcessor::playSound(MaterialType material, float impulse, float collisionX, ShapeType shapeType)
{
    // Debug logging to trace collision detection
    DBG("Physics: playSound called! Material=" << (int)material 
        << " Impulse=" << impulse 
        << " CollisionX=" << collisionX 
        << " Shape=" << (int)shapeType);
    
    // Convert physics X-coordinate (meters) to pan value (0-1)
    const float scale = 50.0f;
    const float canvasWidthMeters = 600.0f / scale;
    float pan = collisionX / canvasWidthMeters;
    
    // Find the next available voice and trigger it
    synthVoices[nextVoice].startNote(materialDatabase[material], impulse, pan);

    // Cycle to the next voice for next time (round-robin)
    nextVoice = (nextVoice + 1) % synthVoices.size();
    
    // Fire the triggers (will be reset by the audio thread)
    // Always fire the main "Trigger Out" (index 0)
    triggerOutputValues[0] = 1.0f;
    
    // Fire the shape-specific trigger
    switch (shapeType)
    {
        case ShapeType::Circle:
            triggerOutputValues[1] = 1.0f; // Ball Trigger
            break;
        case ShapeType::Square:
            triggerOutputValues[2] = 1.0f; // Square Trigger
            break;
        case ShapeType::Triangle:
            triggerOutputValues[3] = 1.0f; // Triangle Trigger
            break;
    }
}

#if defined(PRESET_CREATOR_UI)
void PhysicsModuleProcessor::drawParametersInNode(float itemWidth, const std::function<bool(const juce::String& paramId)>& isParamModulated, const std::function<void()>& onModificationEnded)
{
    juce::ignoreUnused(isParamModulated, onModificationEnded, itemWidth);
    
    auto& ap = getAPVTS();
    auto* draw_list = ImGui::GetWindowDrawList();
    
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
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "Physics Sandbox");
    ImGui::Separator();
    ImGui::Spacing();
    
    // Material selection buttons
    ImGui::Text("Material:");
    ImGui::SameLine();
    if (ImGui::Button("Metal")) { currentMaterial = MaterialType::Metal; }
    ImGui::SameLine();
    if (ImGui::Button("Wood"))  { currentMaterial = MaterialType::Wood; }
    ImGui::SameLine();
    if (ImGui::Button("Soil"))  { currentMaterial = MaterialType::Soil; }
    
    // Gravity slider
    auto* gravityParam = apvts.getRawParameterValue("gravity");
    if (gravityParam)
    {
        float gravityValue = gravityParam->load();
        ImGui::PushItemWidth(150.0f);
        if (ImGui::SliderFloat("Gravity", &gravityValue, 0.0f, 50.0f, "%.1f"))
        {
            if (auto* param = apvts.getParameter("gravity"))
                param->setValueNotifyingHost(apvts.getParameterRange("gravity").convertTo0to1(gravityValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        ImGui::PopItemWidth();
    }
    
    // Wind slider
    auto* windParam = apvts.getRawParameterValue("wind");
    if (windParam)
    {
        float windValue = windParam->load();
        ImGui::PushItemWidth(150.0f);
        if (ImGui::SliderFloat("Wind", &windValue, -20.0f, 20.0f, "%.1f"))
        {
            if (auto* param = apvts.getParameter("wind"))
                param->setValueNotifyingHost(apvts.getParameterRange("wind").convertTo0to1(windValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        ImGui::PopItemWidth();
    }
    
    // Stroke Size slider
    auto* strokeSizeParam = apvts.getRawParameterValue("strokeSize");
    if (strokeSizeParam)
    {
        float strokeSizeValue = strokeSizeParam->load();
        ImGui::PushItemWidth(150.0f);
        if (ImGui::SliderFloat("Stroke Size", &strokeSizeValue, 1.0f, 10.0f, "%.1f"))
        {
            if (auto* param = apvts.getParameter("strokeSize"))
                param->setValueNotifyingHost(apvts.getParameterRange("strokeSize").convertTo0to1(strokeSizeValue));
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        ImGui::PopItemWidth();
    }
    
    // Max Objects slider
    auto* maxObjectsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("maxObjects"));
    if (maxObjectsParam)
    {
        int maxObjectsValue = maxObjectsParam->get();
        ImGui::PushItemWidth(150.0f);
        if (ImGui::SliderInt("Max Objects", &maxObjectsValue, 1, 500))
        {
            *maxObjectsParam = maxObjectsValue;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) { onModificationEnded(); }
        ImGui::PopItemWidth();
        
        // Display current object count on the same line
        ImGui::SameLine();
        ImGui::Text("(%d)", (int)physicsObjects.size());
    }
    
    // Spawn shape buttons
    ImGui::Text("Spawn:");
    ImGui::SameLine();
    if (ImGui::Button("Ball"))     { spawnObject(ShapeType::Circle); }
    ImGui::SameLine();
    if (ImGui::Button("Square"))   { spawnObject(ShapeType::Square); }
    ImGui::SameLine();
    if (ImGui::Button("Triangle")) { spawnObject(ShapeType::Triangle); }
    
    ImGui::SameLine();
    
    // Erase Mode checkbox
    ImGui::Checkbox("Erase Mode", &isErasing);
    
    ImGui::SameLine();
    
    // Add a button to clear the drawing
    if (ImGui::Button("Clear All"))
    {
        // Destroy all physics bodies before clearing
        for (auto& objPtr : physicsObjects)
        {
            if (objPtr && objPtr->physicsBody)
            {
                world->DestroyBody(objPtr->physicsBody);
                objPtr->physicsBody = nullptr;
            }
        }
        physicsObjects.clear();
        
        for (auto& stroke : userStrokes)
        {
            if (stroke.physicsBody)
            {
                world->DestroyBody(stroke.physicsBody);
                stroke.physicsBody = nullptr;
            }
        }
        userStrokes.clear();
    }
    
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
    draw_list->AddRect(canvas_p0, canvas_p1, IM_COL32(100, 100, 100, 255));
    
    // --- Mouse Input Handling ---
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse_pos_in_canvas = ImVec2(io.MousePos.x - canvas_p0.x, io.MousePos.y - canvas_p0.y);

    // --- Drawing Mode ---
    // Is the mouse button clicked within our canvas?
    if (!isErasing && is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        isDrawing = true;
        currentDrawingStroke.points.clear();
        currentDrawingStroke.material = currentMaterial; // Use the currently selected material
        currentDrawingStroke.points.push_back({mouse_pos_in_canvas.x, mouse_pos_in_canvas.y});
    }

    // Is the user dragging the mouse?
    if (!isErasing && isDrawing && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
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
    if (!isErasing && isDrawing && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        isDrawing = false;
        // If the stroke has at least two points, consider it a valid stroke and add it to our list
        if (currentDrawingStroke.points.size() > 1)
        {
            userStrokes.push_back(currentDrawingStroke);
            
            // Create the physics body for the stroke we just finished drawing
            createStrokeBody(userStrokes.back());
        }
        currentDrawingStroke.points.clear();
    }
    
    // --- Eraser Mode ---
    if (isErasing && is_hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        juce::Point<float> mousePos = { mouse_pos_in_canvas.x, mouse_pos_in_canvas.y };
        const float eraseRadius = 15.0f; // The "size" of our eraser tip
        const float scale = 50.0f;

        // Erase Physics Objects
        // Loop backwards to safely erase elements from the vector
        for (int i = (int)physicsObjects.size() - 1; i >= 0; --i)
        {
            if (physicsObjects[i] && physicsObjects[i]->physicsBody)
            {
                b2Vec2 bodyPos = physicsObjects[i]->physicsBody->GetPosition();
                juce::Point<float> objPos = { bodyPos.x * scale, bodyPos.y * scale };

                if (mousePos.getDistanceFrom(objPos) < eraseRadius)
                {
                    world->DestroyBody(physicsObjects[i]->physicsBody);
                    physicsObjects.erase(physicsObjects.begin() + i);
                }
            }
        }

        // Erase Strokes
        // Loop backwards here as well
        for (int i = (int)userStrokes.size() - 1; i >= 0; --i)
        {
            bool strokeHit = false;
            // Check if any point in the stroke is close to the mouse
            for (const auto& point : userStrokes[i].points)
            {
                if (mousePos.getDistanceFrom(point) < eraseRadius)
                {
                    if (userStrokes[i].physicsBody)
                    {
                        world->DestroyBody(userStrokes[i].physicsBody);
                    }
                    userStrokes.erase(userStrokes.begin() + i);
                    strokeHit = true;
                    break; // Move to the next stroke
                }
            }
            if (strokeHit) continue;
        }
    }
    
    // --- Rendering ---
    draw_list->PushClipRect(canvas_p0, canvas_p1, true); // Don't draw outside the canvas

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
            auto juceColour = materialColourMap[stroke.material];
            ImU32 imColour = IM_COL32(juceColour.getRed(), juceColour.getGreen(), juceColour.getBlue(), 255);
            
            draw_list->AddPolyline(pointsForImGui.data(), (int)pointsForImGui.size(), imColour, 0, strokeThickness);
        }
    }

    // 2. Draw the physics objects
    const float scale = 50.0f;
    for (const auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody)
        {
            b2Vec2 pos = objPtr->physicsBody->GetPosition();
            float angle = objPtr->physicsBody->GetAngle();

            if (objPtr->type == ShapeType::Circle)
            {
                ImVec2 center(canvas_p0.x + pos.x * scale, canvas_p0.y + pos.y * scale);
                draw_list->AddCircleFilled(center, objPtr->radius, IM_COL32(255, 100, 100, 255));
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
                        canvas_p0.x + (pos.x + rotatedX) * scale,
                        canvas_p0.y + (pos.y + rotatedY) * scale
                    );
                }
                draw_list->AddConvexPolyFilled(transformedPoints.data(), (int)transformedPoints.size(), IM_COL32(100, 255, 100, 255));
            }
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
        draw_list->AddPolyline(pointsForImGui.data(), (int)pointsForImGui.size(), IM_COL32(255, 255, 255, 128), 0, 1.5f);
    }

    // 4. Draw eraser cursor when in erase mode
    if (isErasing && is_hovered)
    {
        ImVec2 eraserCenter(canvas_p0.x + mouse_pos_in_canvas.x, canvas_p0.y + mouse_pos_in_canvas.y);
        const float eraseRadius = 15.0f;
        draw_list->AddCircle(eraserCenter, eraseRadius, IM_COL32(255, 50, 50, 200), 0, 2.0f);
    }

    draw_list->PopClipRect();
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
        strokeNode.setProperty("material", (int)stroke.material, nullptr);

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

    return state;
}

void PhysicsModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    if (!state.hasType("PhysicsState"))
        return;

    // --- 1. Clear the current simulation state ---
    for (auto& objPtr : physicsObjects)
    {
        if (objPtr && objPtr->physicsBody)
            world->DestroyBody(objPtr->physicsBody);
    }
    physicsObjects.clear();
    
    for (auto& stroke : userStrokes)
    {
        if (stroke.physicsBody)
            world->DestroyBody(stroke.physicsBody);
    }
    userStrokes.clear();

    // --- 2. Load all strokes ---
    if (auto strokesNode = state.getChildWithName("Strokes"); strokesNode.isValid())
    {
        for (auto strokeNode : strokesNode)
        {
            Stroke newStroke;
            newStroke.material = (MaterialType)(int)strokeNode.getProperty("material", 0);

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
            
            spawnObject(type, pos, vel);
            
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
            spawnObject(ShapeType::Circle, pos, vel);
        }
    }
}

// --- Helper Functions ---

void PhysicsModuleProcessor::spawnObject(ShapeType type, b2Vec2 position, b2Vec2 velocity)
{
    // --- Enforce the Max Objects Limit ---
    auto* maxObjectsParam = static_cast<juce::AudioParameterInt*>(apvts.getParameter("maxObjects"));
    if (maxObjectsParam)
    {
        int maxObjects = maxObjectsParam->get();
        // Check if we are at or over the limit
        while (static_cast<int>(physicsObjects.size()) >= maxObjects)
        {
            // Get the oldest object (the one at the front of the deque)
            b2Body* bodyToDestroy = physicsObjects.front()->physicsBody;
            
            // Add to deferred destruction list (safer - doesn't modify world during step)
            bodiesToDestroy.push_back(bodyToDestroy);
            
            // Remove from our list using efficient pop_front() (smart pointer automatically cleans up)
            physicsObjects.pop_front();
        }
    }
    
    // Create the new object as a unique_ptr (stable memory address)
    auto newObject = std::make_unique<PhysicsObject>();
    newObject->type = type;
    const float scale = 50.0f;
    const float size = 10.0f; // Size in pixels
    
    // If no position provided, spawn at top-center
    if (position.x == 0 && position.y == 0)
    {
        position.Set((600.0f / 2.0f) / scale, size / scale);
    }

    b2BodyDef bodyDef;
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = position;
    bodyDef.linearVelocity = velocity;

    b2Body* body = world->CreateBody(&bodyDef);

    b2PolygonShape polygonShape;
    b2CircleShape circleShape;

    b2FixtureDef fixtureDef;
    fixtureDef.density = 1.0f;
    fixtureDef.friction = 0.4f;
    fixtureDef.restitution = 0.6f;

    if (type == ShapeType::Circle)
    {
        newObject->radius = size;
        circleShape.m_radius = size / scale;
        fixtureDef.shape = &circleShape;
    }
    else // It's a polygon
    {
        float scaledSize = size / scale;
        if (type == ShapeType::Square)
        {
            polygonShape.SetAsBox(scaledSize, scaledSize);
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
            newObject->vertices.assign(points, points + 3);
        }
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
    auto* strokeSizeParam = apvts.getRawParameterValue("strokeSize");
    float strokeThickness = strokeSizeParam ? strokeSizeParam->load() : 3.0f;
    
    const float scale = 50.0f; // Pixels to meters conversion (matches rendering scale)
    float halfThickness = (strokeThickness / 2.0f) / scale;

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
            length / 2.0f / scale,                    // Half-width (length of segment)
            halfThickness,                            // Half-height (half stroke thickness)
            { center.x / scale, center.y / scale },   // Center position in world space
            angle                                     // Rotation angle in radians
        );

        // Attach the box to our body
        b2FixtureDef fixtureDef;
        fixtureDef.shape = &boxShape;
        fixtureDef.friction = 0.6f;
        body->CreateFixture(&fixtureDef);
    }

    // 3. Create circular joints at each point to smooth the connections
    // This prevents gaps at corners and makes collisions feel natural
    for (const auto& point : stroke.points)
    {
        b2CircleShape circleShape;
        circleShape.m_radius = halfThickness;
        circleShape.m_p.Set(point.x / scale, point.y / scale);

        // Attach the circle to our body
        b2FixtureDef fixtureDef;
        fixtureDef.shape = &circleShape;
        fixtureDef.friction = 0.6f;
        body->CreateFixture(&fixtureDef);
    }

    // Store the pointer to the physics body in our stroke object for later reference
    stroke.physicsBody = body;
}
