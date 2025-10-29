#include "AnimationModuleProcessor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

AnimationModuleProcessor::AnimationModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(6), true)), // L Foot Vel X/Y, L Hit, R Foot Vel X/Y, R Hit
      apvts(*this, nullptr, "AnimationParams", {})
{
    // Constructor: m_AnimationData and m_Animator are nullptrs initially.
    m_Renderer = std::make_unique<AnimationRenderer>();
    
    // Register this class to listen for changes from our file loader
    m_fileLoader.addChangeListener(this);
    
    // Initialize tracked bones (using operator[] which default-constructs the struct)
    m_trackedBones["LeftFoot"];
    m_trackedBones["LeftFoot"].name = "LeftFoot";
    
    m_trackedBones["RightFoot"];
    m_trackedBones["RightFoot"].name = "RightFoot";
    
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

    // --- OUTPUT KINEMATIC DATA FOR BOTH FEET ---
    if (buffer.getNumChannels() >= 6 && buffer.getNumSamples() > 0)
    {
        // Load atomic values for both feet
        float lVelX = m_trackedBones.at("LeftFoot").velX.load();
        float lVelY = m_trackedBones.at("LeftFoot").velY.load();
        float rVelX = m_trackedBones.at("RightFoot").velX.load();
        float rVelY = m_trackedBones.at("RightFoot").velY.load();

        // Handle ground triggers (single sample pulses)
        bool lFootHit = m_trackedBones.at("LeftFoot").triggerState.load();
        bool rFootHit = m_trackedBones.at("RightFoot").triggerState.load();
        
        if (lFootHit)
        {
            buffer.setSample(2, 0, 1.0f); // Left foot trigger
            m_trackedBones.at("LeftFoot").triggerState.store(false); // Reset
        }
        
        if (rFootHit)
        {
            buffer.setSample(5, 0, 1.0f); // Right foot trigger
            m_trackedBones.at("RightFoot").triggerState.store(false); // Reset
        }

        // Write DC velocity signals and clear trigger pulses for remaining samples
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            buffer.setSample(0, sample, lVelX);
            buffer.setSample(1, sample, lVelY);
            buffer.setSample(3, sample, rVelX);
            buffer.setSample(4, sample, rVelY);
            
            // Triggers only on first sample, then silence
            if (sample > 0)
            {
                buffer.setSample(2, sample, 0.0f);
                buffer.setSample(5, sample, 0.0f);
            }
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
    
    // Find and cache foot bone IDs for dedicated outputs
    for (auto& pair : m_trackedBones)
    {
        pair.second.boneId = -1; // Reset
    }
    
    for (const auto& pair : m_stagedAnimationData->boneInfoMap)
    {
        const std::string& boneName = pair.first;
        const BoneInfo& boneInfo = pair.second;

        if (juce::String(boneName).endsWithIgnoreCase("LeftFoot"))
        {
            m_trackedBones["LeftFoot"].boneId = boneInfo.id;
            juce::Logger::writeToLog("AnimationModule: Found Left Foot bone: '" + juce::String(boneName) + "' with ID " + juce::String(boneInfo.id));
        }
        else if (juce::String(boneName).endsWithIgnoreCase("RightFoot"))
        {
            m_trackedBones["RightFoot"].boneId = boneInfo.id;
            juce::Logger::writeToLog("AnimationModule: Found Right Foot bone: '" + juce::String(boneName) + "' with ID " + juce::String(boneInfo.id));
        }
    }
    
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
        
        // View rotation controls
        ImGui::Text("View Rotation:");
        ImGui::PushItemWidth(itemWidth / 3.0f - 5.0f); // Adjust width for 3 buttons side-by-side
        if (ImGui::Button("Rot X")) { m_viewRotationX += glm::radians(90.0f); }
        ImGui::SameLine();
        if (ImGui::Button("Rot Y")) { m_viewRotationY += glm::radians(90.0f); }
        ImGui::SameLine();
        if (ImGui::Button("Rot Z")) { m_viewRotationZ += glm::radians(90.0f); }
        ImGui::PopItemWidth();

        // Reset view button - resets rotation and frames the animation
        if (ImGui::Button("Reset View", ImVec2(itemWidth, 0)))
        {
            m_viewRotationX = 0.0f;
            m_viewRotationY = 0.0f;
            m_viewRotationZ = 0.0f;
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
        m_Renderer->setViewRotation({m_viewRotationX, m_viewRotationY, m_viewRotationZ});
        
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
        
        // --- KINEMATIC CALCULATION BLOCK FOR BOTH FEET ---
        for (auto& pair : m_trackedBones)
        {
            TrackedBone& bone = pair.second;

            if (bone.boneId != -1 && currentAnimator != nullptr)
            {
                const auto& worldTransforms = currentAnimator->GetBoneWorldTransforms();
                if (bone.boneId >= 0 && bone.boneId < worldTransforms.size())
                {
                    glm::mat4 worldMatrix = worldTransforms[bone.boneId];
                    glm::vec3 worldPos = worldMatrix[3];

                    // Recreate the projection and view matrices used by the renderer
                    glm::mat4 projection = glm::ortho(-m_zoom + m_panX, m_zoom + m_panX, -m_zoom + m_panY, m_zoom + m_panY, -10.0f, 10.0f);
                    glm::mat4 view = glm::mat4(1.0f);
                    view = glm::rotate(view, m_viewRotationX, glm::vec3(1.0f, 0.0f, 0.0f));
                    view = glm::rotate(view, m_viewRotationY, glm::vec3(0.0f, 1.0f, 0.0f));
                    view = glm::rotate(view, m_viewRotationZ, glm::vec3(0.0f, 0.0f, 1.0f));

                    // Project to screen space
                    ImVec2 viewportPos = ImGui::GetItemRectMin();
                    glm::vec2 currentScreenPos = worldToScreen(worldPos, view, projection, viewportPos, viewportSize);

                    // Ground trigger detection
                    bool isBoneBelowGround = currentScreenPos.y > (viewportPos.y + m_groundY);
                    if (isBoneBelowGround && !bone.wasBelowGround)
                    {
                        bone.triggerState.store(true);
                    }
                    bone.wasBelowGround = isBoneBelowGround;

                    // Calculate velocity
                    if (bone.isFirstFrame)
                    {
                        bone.lastScreenPos = currentScreenPos;
                        bone.isFirstFrame = false;
                    }
                    float deltaTime = ImGui::GetIO().DeltaTime;
                    glm::vec2 velocity(0.0f);
                    if (deltaTime > 0.0f)
                    {
                        velocity = (currentScreenPos - bone.lastScreenPos) / deltaTime;
                    }
                    bone.lastScreenPos = currentScreenPos;

                    // Store results in this bone's specific atomics
                    bone.velX.store(velocity.x);
                    bone.velY.store(velocity.y);
                }
            }
            else
            {
                // Reset state if this bone isn't found in the current animation
                bone.wasBelowGround = false;
                bone.isFirstFrame = true;
            }
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
    // We support 6 discrete output channels (L/R foot velocities and ground triggers), and no inputs.
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::discreteChannels(6)
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled();
}

std::vector<DynamicPinInfo> AnimationModuleProcessor::getDynamicOutputPins() const
{
    // Define our 6 output pins for the Animation module
    return {
        { "L Foot Vel X",  0, PinDataType::CV },    // Left foot X velocity
        { "L Foot Vel Y",  1, PinDataType::CV },    // Left foot Y velocity
        { "L Foot Hit",    2, PinDataType::Gate },  // Left foot ground trigger
        { "R Foot Vel X",  3, PinDataType::CV },    // Right foot X velocity
        { "R Foot Vel Y",  4, PinDataType::CV },    // Right foot Y velocity
        { "R Foot Hit",    5, PinDataType::Gate }   // Right foot ground trigger
    };
}

// === State Management (for saving/loading presets) ===

juce::ValueTree AnimationModuleProcessor::getExtraStateTree() const
{
    // This function is called by the synth when saving a preset.
    // We create a ValueTree to hold our module's unique state.
    juce::ValueTree state("AnimationModuleState");

    // 1. Save the absolute path of the currently loaded animation file.
    state.setProperty("animationFilePath", m_fileLoader.getLoadedFilePath(), nullptr);

    // 2. Save the viewport/camera settings.
    state.setProperty("zoom", m_zoom, nullptr);
    state.setProperty("panX", m_panX, nullptr);
    state.setProperty("panY", m_panY, nullptr);
    state.setProperty("groundY", m_groundY, nullptr);
    state.setProperty("viewRotationX", m_viewRotationX, nullptr);
    state.setProperty("viewRotationY", m_viewRotationY, nullptr);
    state.setProperty("viewRotationZ", m_viewRotationZ, nullptr);

    // 3. Save the name of the currently selected bone.
    state.setProperty("selectedBoneName", juce::String(m_selectedBoneName), nullptr);

    juce::Logger::writeToLog("[AnimationModule] Saving state: file='" + 
                              m_fileLoader.getLoadedFilePath() + 
                              "', bone='" + juce::String(m_selectedBoneName) + "'");
    
    return state;
}

void AnimationModuleProcessor::setExtraStateTree(const juce::ValueTree& state)
{
    // This function is called by the synth when loading a preset.
    // We restore our state from the provided ValueTree.
    
    if (!state.hasType("AnimationModuleState"))
        return;

    juce::Logger::writeToLog("[AnimationModule] Loading state from preset...");

    // 1. Restore the viewport/camera settings.
    m_zoom = state.getProperty("zoom", 10.0f);
    m_panX = state.getProperty("panX", 0.0f);
    m_panY = state.getProperty("panY", 0.0f);
    m_groundY = state.getProperty("groundY", 180.0f);
    m_viewRotationX = state.getProperty("viewRotationX", 0.0f);
    m_viewRotationY = state.getProperty("viewRotationY", 0.0f);
    m_viewRotationZ = state.getProperty("viewRotationZ", 0.0f);

    // 2. Restore the selected bone name. The UI will pick this up on the next frame.
    m_selectedBoneName = state.getProperty("selectedBoneName", "None").toString().toStdString();
    
    // 3. Load the animation file from the saved path.
    juce::String filePath = state.getProperty("animationFilePath", "").toString();
    
    if (filePath.isNotEmpty())
    {
        juce::File fileToLoad(filePath);
        
        if (fileToLoad.existsAsFile())
        {
            // We use our existing non-blocking loader to load the file in the background.
            // The UI will remain responsive while this happens.
            juce::Logger::writeToLog("[AnimationModule] Restoring animation from preset: " + fileToLoad.getFullPathName());
            m_fileLoader.startLoadingFile(fileToLoad);
        }
        else
        {
            juce::Logger::writeToLog("[AnimationModule] Warning: Animation file not found at: " + filePath);
        }
    }
    else
    {
        juce::Logger::writeToLog("[AnimationModule] No animation file path in preset.");
    }
}

