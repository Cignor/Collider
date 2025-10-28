#include "AnimationModuleProcessor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

AnimationModuleProcessor::AnimationModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)), // X Pos, Y Pos, X Vel, Y Vel, Ground Trigger
      apvts(*this, nullptr, "AnimationParams", {})
{
    // Constructor: m_AnimationData and m_Animator are nullptrs initially.
    m_Renderer = std::make_unique<AnimationRenderer>();
    
    // Register this class to listen for changes from our file loader
    m_fileLoader.addChangeListener(this);
    
    // DEBUG: Verify output channel count
    juce::Logger::writeToLog("[AnimationModule] Constructor: getTotalNumOutputChannels() = " + 
                             juce::String(getTotalNumOutputChannels()));
}

AnimationModuleProcessor::~AnimationModuleProcessor()
{
    // Remove listener before destruction
    m_fileLoader.removeChangeListener(this);
    
    // Safely clean up the active animator
    Animator* oldAnimator = m_activeAnimator.exchange(nullptr);
    if (oldAnimator)
    {
        // Move to deletion queue to be freed safely
        const juce::ScopedLock lock(m_freeingLock);
        m_animatorsToFree.push_back(std::unique_ptr<Animator>(oldAnimator));
    }
    
    // Clear all pending deletions
    {
        const juce::ScopedLock lock(m_freeingLock);
        m_animatorsToFree.clear();
        m_dataToFree.clear();
    }
}

void AnimationModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // We don't need to do anything special here for this module,
    // but the override is required.
}

void AnimationModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // This is the REAL-TIME AUDIO THREAD - must NOT block!
    
    // === STEP 1: Clean up old data (non-blocking try-lock) ===
    // This is a safe place to delete old animation data that was swapped out.
    {
        const juce::ScopedTryLock tryLock(m_freeingLock);
        if (tryLock.isLocked())
        {
            // We got the lock without blocking - safe to clear old data
            m_animatorsToFree.clear();
            m_dataToFree.clear();
        }
        // If we didn't get the lock, that's fine - we'll try again next block
    }
    
    // === STEP 2: Get current animator (LOCK-FREE atomic load) ===
    // Load the active animator pointer atomically.
    // This is lock-free and safe - even if the main thread swaps in new data,
    // our local pointer remains valid for this entire block.
    Animator* currentAnimator = m_activeAnimator.load(std::memory_order_acquire);
    
    // === STEP 3: Update animation if we have one ===
    if (currentAnimator != nullptr)
    {
        // Calculate the time elapsed for this audio block.
        const float deltaTime = buffer.getNumSamples() / getSampleRate();
        
        // Update the animation - this is now completely lock-free!
        currentAnimator->Update(deltaTime);
    }
    
    // Clear the output buffer first
    buffer.clear();
    
    // --- OUTPUT KINEMATIC DATA ---
    // Load the thread-safe atomic values
    float posX = m_outputPosX.load();
    float posY = m_outputPosY.load();
    float velX = m_outputVelX.load();
    float velY = m_outputVelY.load();
    
    // Debug logging removed - was causing "string too long" exceptions in audio thread

    // Write the values to the corresponding output buffers (first sample of each channel)
    if (buffer.getNumChannels() >= 5 && buffer.getNumSamples() > 0)
    {
        buffer.setSample(0, 0, posX); // Output 0: X Position
        buffer.setSample(1, 0, posY); // Output 1: Y Position
        buffer.setSample(2, 0, velX); // Output 2: X Velocity
        buffer.setSample(3, 0, velY); // Output 3: Y Velocity
        
        // Output 4: Ground Trigger (single sample pulse)
        if (m_triggerState.load())
        {
            buffer.setSample(4, 0, 1.0f); // Send trigger pulse
            m_triggerState.store(false);  // Reset immediately
        }
        else
        {
            buffer.setSample(4, 0, 0.0f);
        }
        
        // Fill the rest of the buffer with the same values (DC signals for channels 0-3, silence for trigger)
        for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample(0, sample, posX);
            buffer.setSample(1, sample, posY);
            buffer.setSample(2, sample, velX);
            buffer.setSample(3, sample, velY);
            buffer.setSample(4, sample, 0.0f); // Trigger only on first sample
        }
    }
}

bool AnimationModuleProcessor::isCurrentlyLoading() const
{
    return m_fileLoader.isLoading();
}

void AnimationModuleProcessor::openAnimationFile()
{
    // If already loading, ignore the request
    if (isCurrentlyLoading())
    {
        juce::Logger::writeToLog("AnimationModule: Already loading a file. Ignoring new request.");
        return;
    }

    // Create a file chooser to let the user select an animation file
    // Store it as a member to keep it alive during the async operation
    m_FileChooser = std::make_unique<juce::FileChooser>(
        "Select an animation file (glTF/FBX)...",
        juce::File{},
        "*.gltf;*.glb;*.fbx");

    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    // Launch the file chooser asynchronously
    m_FileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
    {
        if (chooser.getResults().isEmpty())
        {
            juce::Logger::writeToLog("AnimationModule: File selection cancelled.");
            return; // User cancelled
        }

        juce::File file = chooser.getResult();
        
        if (!file.existsAsFile())
        {
            juce::Logger::writeToLog("AnimationModule: Selected file does not exist.");
            return;
        }
        
        juce::Logger::writeToLog("AnimationModule: Starting background load of: " + file.getFullPathName());
        
        // Start the background loading process
        // The UI will remain responsive while this happens!
        m_fileLoader.startLoadingFile(file);
    });
}

// THIS IS THE MOST IMPORTANT PART
// This function will be called on the MESSAGE THREAD when the background thread finishes
void AnimationModuleProcessor::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Make sure the notification is coming from our file loader
    if (source == &m_fileLoader)
    {
        juce::Logger::writeToLog("AnimationModule: Background loading complete. Processing data...");
        
        // Get the loaded data from the loader (transfers ownership)
        std::unique_ptr<RawAnimationData> rawData = m_fileLoader.getLoadedData();

        if (rawData != nullptr)
        {
            // Success! The file was loaded and parsed in the background.
            // Now we can do the binding and setup work on the message thread.
            juce::String filePath = m_fileLoader.getLoadedFilePath();
            juce::Logger::writeToLog("AnimationModule: File loaded successfully: " + filePath);
            juce::Logger::writeToLog("   Raw Nodes: " + juce::String(rawData->nodes.size()));
            juce::Logger::writeToLog("   Raw Bones: " + juce::String(rawData->bones.size()));
            juce::Logger::writeToLog("   Raw Clips: " + juce::String(rawData->clips.size()));
            
            setupAnimationFromRawData(std::move(rawData));
        }
        else
        {
            // Failure - the loader returned nullptr
            juce::Logger::writeToLog("AnimationModule ERROR: Failed to load animation file. Check logs for details.");
            
            // Show error message to the user
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Loading Failed",
                "The selected animation file could not be loaded.\nCheck the console logs for details.",
                "OK");
        }
    }
}

void AnimationModuleProcessor::setupAnimationFromRawData(std::unique_ptr<RawAnimationData> rawData)
{
    // This is called on the MESSAGE THREAD after background loading completes
    
    juce::Logger::writeToLog("AnimationModule: Binding raw data to create AnimationData...");
    auto finalData = AnimationBinder::Bind(*rawData);

    if (!finalData)
    {
        juce::Logger::writeToLog("AnimationModule ERROR: AnimationBinder failed to create final AnimationData.");
        
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Binding Failed",
            "The animation data could not be processed after loading.",
            "OK");
        return;
    }
    
    juce::Logger::writeToLog("AnimationModule: Binder SUCCESS - Final data created.");
    juce::Logger::writeToLog("   Final Bones: " + juce::String(finalData->boneInfoMap.size()));
    juce::Logger::writeToLog("   Final Clips: " + juce::String(finalData->animationClips.size()));

    // === THREAD-SAFE DATA SWAP ===
    // Prepare the new animator and data in "staging" area (not visible to audio thread yet)
    m_stagedAnimationData = std::move(finalData);
    m_stagedAnimator = std::make_unique<Animator>(m_stagedAnimationData.get());
    
    // Play the first animation clip if available
    if (!m_stagedAnimationData->animationClips.empty())
    {
        juce::Logger::writeToLog("AnimationModule: Playing first animation clip: " + 
                               juce::String(m_stagedAnimationData->animationClips[0].name));
        m_stagedAnimator->PlayAnimation(m_stagedAnimationData->animationClips[0].name);
    }
    
    // Cache bone names for thread-safe UI access (on main thread, before audio thread gets it)
    m_cachedBoneNames.clear();
    for (const auto& pair : m_stagedAnimationData->boneInfoMap)
    {
        m_cachedBoneNames.push_back(pair.first);
    }
    juce::Logger::writeToLog("AnimationModule: Cached " + juce::String((int)m_cachedBoneNames.size()) + " bone names for UI.");
    
    juce::Logger::writeToLog("AnimationModule: Preparing to swap animation data...");
    
    // 1. Release the raw pointer for the new animator from its unique_ptr.
    Animator* newAnimator = m_stagedAnimator.release();
    
    // 2. Atomically swap the new animator into the 'active' slot.
    // The audio thread will pick this up on its next processBlock().
    Animator* oldAnimator = m_activeAnimator.exchange(newAnimator, std::memory_order_release);
    
    // 3. Now, swap the unique_ptr that owns the AnimationData.
    // m_stagedAnimationData (holding the NEW data) is moved into m_activeData.
    // The previous m_activeData (holding the OLD data) is moved into a temporary.
    std::unique_ptr<AnimationData> oldDataToFree = std::move(m_activeData);
    m_activeData = std::move(m_stagedAnimationData);

    juce::Logger::writeToLog("AnimationModule: New animator is now active.");

    // 4. Queue the OLD animator and OLD data for safe deletion.
    // We can't delete them immediately, as the audio thread might still be using them.
    {
        const juce::ScopedLock lock(m_freeingLock);
        if (oldAnimator)
        {
            m_animatorsToFree.push_back(std::unique_ptr<Animator>(oldAnimator));
            juce::Logger::writeToLog("AnimationModule: Old animator queued for safe deletion.");
        }
        if (oldDataToFree)
        {
            m_dataToFree.push_back(std::move(oldDataToFree));
            juce::Logger::writeToLog("AnimationModule: Old animation data queued for safe deletion.");
        }
    }
    
    // Reset UI state now that new data is active
    m_selectedBoneIndex = -1;
    m_selectedBoneName = "None";
    m_isFirstFrame = true;
    
    juce::Logger::writeToLog("AnimationModule: Animation atomically swapped and ready for audio thread!");
}

const std::vector<glm::mat4>& AnimationModuleProcessor::getFinalBoneMatrices() const
{
    // This is called from the UI/message thread to get bone matrices for rendering.
    // We use the same atomic pointer the audio thread uses - this is safe and lock-free!
    
    Animator* currentAnimator = m_activeAnimator.load(std::memory_order_acquire);

    if (currentAnimator != nullptr)
    {
        return currentAnimator->GetFinalBoneMatrices();
    }

    // If there's no animator, return a static empty vector to avoid crashes.
    static const std::vector<glm::mat4> empty;
    return empty;
}

#if defined(PRESET_CREATOR_UI)

// Helper function to project a 3D point to 2D screen space
static glm::vec2 worldToScreen(const glm::vec3& worldPos, const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix, ImVec2 viewportPos, ImVec2 viewportSize)
{
    glm::vec4 clipSpacePos = projectionMatrix * viewMatrix * glm::vec4(worldPos, 1.0f);
    if (clipSpacePos.w == 0.0f) return {0,0};
    glm::vec3 ndcSpacePos = glm::vec3(clipSpacePos.x, clipSpacePos.y, clipSpacePos.z) / clipSpacePos.w;
    glm::vec2 screenPos;
    screenPos.x = (ndcSpacePos.x + 1.0f) / 2.0f * viewportSize.x + viewportPos.x;
    screenPos.y = (1.0f - ndcSpacePos.y) / 2.0f * viewportSize.y + viewportPos.y;
    return screenPos;
}

void AnimationModuleProcessor::drawParametersInNode(float itemWidth,
                                                     const std::function<bool(const juce::String& paramId)>& isParamModulated,
                                                     const std::function<void()>& onModificationEnded)
{
    ImGui::PushItemWidth(itemWidth);
    
    // File loading section
    ImGui::TextWrapped("glTF File:");
    
    // Show loading status or loaded file info
    // Get current animator atomically (lock-free)
    Animator* currentAnimator = m_activeAnimator.load(std::memory_order_acquire);
    
    if (isCurrentlyLoading())
    {
        // Show a loading indicator while file is being loaded in the background
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Loading...");
        ImGui::SameLine();
        // Simple animated spinner
        static float spinnerAngle = 0.0f;
        spinnerAngle += ImGui::GetIO().DeltaTime * 10.0f;
        ImGui::Text("%.1f", spinnerAngle); // Simple animation placeholder
    }
    else if (currentAnimator != nullptr && currentAnimator->GetAnimationData() != nullptr)
    {
        auto* animData = currentAnimator->GetAnimationData();
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded");
        ImGui::Text("Bones: %zu", animData->boneInfoMap.size());
        ImGui::Text("Clips: %zu", animData->animationClips.size());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No file loaded");
    }
    
    // Disable button while loading
    if (isCurrentlyLoading())
        ImGui::BeginDisabled();
    
    if (ImGui::Button("Load Animation File...", ImVec2(itemWidth, 0)))
    {
        // Use the new async loading method!
        // This will not block the UI - the file chooser and loading happen in the background
        openAnimationFile();
    }
    
    if (isCurrentlyLoading())
        ImGui::EndDisabled();
    
    
    // --- BONE SELECTION ---
    if (currentAnimator != nullptr && currentAnimator->GetAnimationData() != nullptr)
    {
        auto* animData = currentAnimator->GetAnimationData();
        if (!animData->boneInfoMap.empty())
        {
            if (ImGui::BeginCombo("Selected Bone", m_selectedBoneName.c_str()))
            {
                // Add a "None" option
                bool isNoneSelected = (m_selectedBoneIndex == -1);
                if (ImGui::Selectable("None", isNoneSelected))
                {
                    m_selectedBoneIndex = -1;
                    m_selectedBoneName = "None";
                    m_selectedBoneID = -1;
                }

                // Iterate through cached bone names (thread-safe)
                int currentIndex = 0;
                for (const auto& boneName : m_cachedBoneNames)
                {
                    bool isSelected = (m_selectedBoneName == boneName);

                    if (ImGui::Selectable(boneName.c_str(), isSelected))
                    {
                        m_selectedBoneName = boneName;
                        m_selectedBoneIndex = currentIndex;
                        
                        // Cache the bone ID to avoid map lookups every frame
                        if (animData->boneInfoMap.count(boneName))
                        {
                            m_selectedBoneID = animData->boneInfoMap.at(boneName).id;
                        }
                        else
                        {
                            m_selectedBoneID = -1;
                        }
                    }
                    if (isSelected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                    currentIndex++;
                }
                ImGui::EndCombo();
            }
        }
    }
    
    
    // Animation playback controls
    if (currentAnimator != nullptr && currentAnimator->GetAnimationData() != nullptr)
    {
        auto* animData = currentAnimator->GetAnimationData();
        ImGui::Text("Animation Controls:");
        
        // List available clips
        if (!animData->animationClips.empty())
        {
            ImGui::Text("Available Clips:");
            for (size_t i = 0; i < animData->animationClips.size(); ++i)
            {
                const auto& clip = animData->animationClips[i];
                if (ImGui::Button(clip.name.c_str(), ImVec2(itemWidth, 0)))
                {
                    // Safe to call directly - the animator pointer is valid for this frame
                    currentAnimator->PlayAnimation(clip.name);
                }
            }
        }
        
        
        // Speed control
        static float speed = 1.0f;
        if (ImGui::SliderFloat("Speed", &speed, 0.1f, 3.0f, "%.2f"))
        {
            // Safe to call directly - the animator pointer is valid for this frame
            currentAnimator->SetAnimationSpeed(speed);
        }
        
        // DEBUG: Display basic info (accessing animator state directly is unsafe due to audio thread)
        ImGui::Separator();
        ImGui::Text("Debug Info:");
        ImGui::Text("Bones: %d", (int)animData->boneInfoMap.size());
        ImGui::Text("Clips: %d", (int)animData->animationClips.size());
        ImGui::Separator();
        
        
        // --- RENDERING VIEWPORT ---
        
        ImGui::Text("Animation Viewport:");
        
        // Camera controls
        ImGui::SliderFloat("Zoom", &m_zoom, 1.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Pan X", &m_panX, -20.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Pan Y", &m_panY, -20.0f, 20.0f, "%.1f");
        
        // Frame view button - auto-calculates optimal zoom and pan
        if (ImGui::Button("Frame View", ImVec2(itemWidth, 0)))
        {
            if (currentAnimator != nullptr)
            {
                glm::vec2 newPan;
                m_Renderer->frameView(currentAnimator->GetBoneWorldTransforms(), m_zoom, newPan);
                m_panX = newPan.x;
                m_panY = newPan.y;
            }
        }
        
        
        // Ground trigger line position
        ImGui::SliderFloat("Ground Y", &m_groundY, 0.0f, 200.0f, "%.0f");
        
        
        // Pass the latest values to the renderer before drawing
        m_Renderer->setZoom(m_zoom);
        m_Renderer->setPan({m_panX, m_panY});
        
        // Define the size of our viewport
        const ImVec2 viewportSize(200, 200);
        
        // Setup the renderer (it will only run once internally)
        m_Renderer->setup(static_cast<int>(viewportSize.x), static_cast<int>(viewportSize.y));
        
        // Get world transforms for visualization (NOT skinning matrices!)
        const auto& worldTransforms = currentAnimator->GetBoneWorldTransforms();
        
        // --- DEBUG: Log bone positions to diagnose rendering issues ---
        static int debugFrameCounter = 0;
        if (++debugFrameCounter % 60 == 0 && !worldTransforms.empty()) // Log once per second at 60fps
        {
            juce::Logger::writeToLog("=== Animation Frame Debug ===");
            juce::Logger::writeToLog("Total bones: " + juce::String(worldTransforms.size()));
            
            // Log the first 3 bone positions to see if they're all at origin or varying
            for (size_t i = 0; i < std::min(size_t(3), worldTransforms.size()); ++i)
            {
                glm::vec3 pos = worldTransforms[i][3];
                juce::Logger::writeToLog("Bone[" + juce::String(i) + "] Position: (" + 
                    juce::String(pos.x, 2) + ", " + 
                    juce::String(pos.y, 2) + ", " + 
                    juce::String(pos.z, 2) + ")");
            }
        }
        
        m_Renderer->render(worldTransforms);
        
        // Display the texture from the FBO (flipped vertically)
        ImGui::Image((void*)(intptr_t)m_Renderer->getTextureID(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
        
        // --- DRAW GROUND LINE ---
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p1 = ImGui::GetItemRectMin();
        ImVec2 p2 = ImGui::GetItemRectMax();
        drawList->AddLine(ImVec2(p1.x, p1.y + m_groundY), ImVec2(p2.x, p1.y + m_groundY), IM_COL32(255, 0, 0, 255), 2.0f);
        
        // --- KINEMATIC CALCULATION BLOCK ---
        if (m_selectedBoneID != -1 && currentAnimator != nullptr)
        {
            // 1. Get the bone's world matrix using the cached bone ID (thread-safe!)
            const auto& worldTransforms = currentAnimator->GetBoneWorldTransforms();
            
            if (m_selectedBoneID >= 0 && m_selectedBoneID < worldTransforms.size())
            {
                glm::mat4 worldMatrix = worldTransforms[m_selectedBoneID];
                glm::vec3 worldPos = worldMatrix[3];

                // 2. Recreate the same projection used by the renderer
                glm::mat4 projection = glm::ortho(-m_zoom + m_panX, m_zoom + m_panX, -m_zoom + m_panY, m_zoom + m_panY, -10.0f, 10.0f);
                glm::mat4 view = glm::mat4(1.0f); // Identity view matrix

                // 3. Project to screen space
                ImVec2 viewportPos = ImGui::GetItemRectMin();
                glm::vec2 currentScreenPos = worldToScreen(worldPos, view, projection, viewportPos, viewportSize);

                // 4. Ground trigger detection
                bool isBoneBelowGround = currentScreenPos.y > (viewportPos.y + m_groundY);
                if (isBoneBelowGround && !m_wasBoneBelowGround)
                {
                    // The bone just crossed the line from above, send a trigger
                    m_triggerState.store(true);
                }
                m_wasBoneBelowGround = isBoneBelowGround;

                // 5. Calculate velocity
                if (m_isFirstFrame)
                {
                    m_lastScreenPos = currentScreenPos;
                    m_isFirstFrame = false;
                }
                float deltaTime = ImGui::GetIO().DeltaTime;
                glm::vec2 velocity(0.0f);
                if (deltaTime > 0.0f)
                {
                    velocity = (currentScreenPos - m_lastScreenPos) / deltaTime;
                }
                m_lastScreenPos = currentScreenPos;

                // 6. Store results in atomics for the audio thread
                m_outputPosX.store(currentScreenPos.x);
                m_outputPosY.store(currentScreenPos.y);
                m_outputVelX.store(velocity.x);
                m_outputVelY.store(velocity.y);
            }
        }
        else {
            m_wasBoneBelowGround = false; // Reset trigger state
            m_isFirstFrame = true; // Reset when no bone is selected
        }
    }
    else
    {
        // Show a placeholder when no animation is loaded to maintain consistent node size
        ImGui::TextDisabled("Load an animation file to see animation");
        ImGui::Dummy(ImVec2(200, 200)); // Reserve space for the viewport
    }
    
    ImGui::PopItemWidth();
}
#endif

bool AnimationModuleProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // We support 5 discrete output channels (X/Y position, X/Y velocity, and ground trigger), and no inputs.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels(5)
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled();
}

std::vector<DynamicPinInfo> AnimationModuleProcessor::getDynamicOutputPins() const
{
    // Define our 5 output pins for the Animation module
    return {
        { "Pos X",      0, PinDataType::CV },    // Selected bone X position
        { "Pos Y",      1, PinDataType::CV },    // Selected bone Y position
        { "Vel X",      2, PinDataType::CV },    // Selected bone X velocity
        { "Vel Y",      3, PinDataType::CV },    // Selected bone Y velocity
        { "Ground Hit", 4, PinDataType::Gate }   // Ground trigger (pulse when bone crosses ground line)
    };
}

