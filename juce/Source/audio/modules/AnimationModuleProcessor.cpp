#include "AnimationModuleProcessor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

AnimationModuleProcessor::AnimationModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(MAX_TRACKED_BONES * 3), true)), // Up to 10 bones * 3 outputs each
      apvts(*this, nullptr, "AnimationParams", {})
{
    // Constructor: m_AnimationData and m_Animator are nullptrs initially.
    m_Renderer = std::make_unique<AnimationRenderer>();
    
    // Register this class to listen for changes from our file loader
    m_fileLoader.addChangeListener(this);
    
    // Tracked bones start empty - they will be added when an animation is loaded
    
    // Initialize with one default ground plane at Y=0
    m_groundPlanes.push_back(180.0f);
    
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

    // --- OUTPUT KINEMATIC DATA FOR ALL TRACKED BONES (DYNAMIC) ---
    const juce::ScopedTryLock tryLock(m_trackedBonesLock);
    if (!tryLock.isLocked())
    {
        // Failed to get the lock, means the UI thread is modifying the map.
        // Output silence this block and try again next time.
        return;
    }

    if (buffer.getNumSamples() > 0 && !m_trackedBones.empty())
    {
        int channelIndex = 0;
        for (auto& pair : m_trackedBones)
        {
            // Ensure we don't write past the buffer's channel count
            if (channelIndex + 2 >= buffer.getNumChannels())
                break;

            TrackedBone& bone = pair.second;
            float velX = bone.velX.load();
            float velY = bone.velY.load();
            bool hit = bone.triggerState.load();

            if (hit)
            {
                // Write trigger pulse on the first sample
                buffer.setSample(channelIndex + 2, 0, 1.0f);
                bone.triggerState.store(false); // Reset atomic flag
            }

            // Write DC velocity signals and clear trigger pulses for remaining samples
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                buffer.setSample(channelIndex + 0, sample, velX);
                buffer.setSample(channelIndex + 1, sample, velY);
                if (sample > 0)
                {
                    buffer.setSample(channelIndex + 2, sample, 0.0f);
                }
            }
            
            channelIndex += 3;
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

void AnimationModuleProcessor::addTrackedBone(const std::string& boneName)
{
    if (boneName == "None" || boneName.empty())
        return;

    const juce::ScopedLock lock(m_trackedBonesLock);

    if (m_trackedBones.size() >= MAX_TRACKED_BONES)
    {
        juce::Logger::writeToLog("AnimationModule: Cannot add more than " + juce::String(MAX_TRACKED_BONES) + " tracked bones.");
        return;
    }

    if (m_trackedBones.find(boneName) == m_trackedBones.end())
    {
        m_trackedBones[boneName];
        m_trackedBones[boneName].name = boneName;
        
        // Find the bone ID if an animation is already loaded
        if (m_activeData)
        {
            if (m_activeData->boneInfoMap.count(boneName))
            {
                m_trackedBones[boneName].boneId = m_activeData->boneInfoMap.at(boneName).id;
                juce::Logger::writeToLog("AnimationModule: Added tracked bone '" + juce::String(boneName) + "' with ID " + juce::String(m_trackedBones[boneName].boneId));
            }
        }
        
        // Note: Pins will update on next module reload/patch load
    }
}

void AnimationModuleProcessor::removeTrackedBone(const std::string& boneName)
{
    if (boneName == "None" || boneName.empty())
        return;

    const juce::ScopedLock lock(m_trackedBonesLock);

    if (m_trackedBones.find(boneName) != m_trackedBones.end())
    {
        m_trackedBones.erase(boneName);
        juce::Logger::writeToLog("AnimationModule: Removed tracked bone '" + juce::String(boneName) + "'");

        // Note: Pins will update on next module reload/patch load
    }
}

void AnimationModuleProcessor::addGroundPlane(float initialY)
{
    const juce::ScopedLock lock(m_groundPlanesLock);
    m_groundPlanes.push_back(initialY);
    juce::Logger::writeToLog("AnimationModule: Added ground plane at Y=" + juce::String(initialY));
}

void AnimationModuleProcessor::removeGroundPlane(int index)
{
    const juce::ScopedLock lock(m_groundPlanesLock);
    if (m_groundPlanes.empty()) return;

    if (index < 0 || index >= (int)m_groundPlanes.size())
    {
        m_groundPlanes.pop_back(); // Default to removing the last one
    }
    else
    {
        m_groundPlanes.erase(m_groundPlanes.begin() + index);
    }
    juce::Logger::writeToLog("AnimationModule: Removed ground plane (count now: " + juce::String((int)m_groundPlanes.size()) + ")");
}

std::vector<float> AnimationModuleProcessor::getGroundPlanes() const
{
    const juce::ScopedLock lock(m_groundPlanesLock);
    return m_groundPlanes;
}

void AnimationModuleProcessor::updateTrackedBoneIDs()
{
    // No animation loaded, nothing to do
    if (m_activeData == nullptr)
        return;

    const juce::ScopedLock lock(m_trackedBonesLock);
    
    // Go through all currently tracked bones and find matching bone IDs in the animation
    for (auto& trackedBonePair : m_trackedBones)
    {
        const std::string& trackedBoneName = trackedBonePair.first;
        int foundId = -1;

        // Search for this bone name in the animation's bone info map
        if (m_activeData->boneInfoMap.count(trackedBoneName))
        {
            foundId = m_activeData->boneInfoMap.at(trackedBoneName).id;
            juce::Logger::writeToLog("AnimationModule: Mapped tracked bone '" + juce::String(trackedBoneName) + 
                                     "' to ID " + juce::String(foundId));
        }
        else
        {
            juce::Logger::writeToLog("AnimationModule: WARNING - Tracked bone '" + juce::String(trackedBoneName) + 
                                     "' not found in animation");
        }
        
        trackedBonePair.second.boneId = foundId;
    }
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
    
    // Update tracked bone IDs for the new animation (NON-DESTRUCTIVE)
    // This does NOT clear the list - it only refreshes the IDs
    {
        const juce::ScopedLock lock(m_trackedBonesLock);
        
        // If this is a completely fresh load (no bones tracked yet), add defaults
        if (m_trackedBones.empty())
        {
            m_trackedBones["LeftFoot"];
            m_trackedBones["LeftFoot"].name = "LeftFoot";
            m_trackedBones["RightFoot"];
            m_trackedBones["RightFoot"].name = "RightFoot";
            juce::Logger::writeToLog("AnimationModule: Initialized default tracked bones (LeftFoot, RightFoot)");
        }
        
        // Update bone IDs for ALL currently tracked bones from the new animation
        for (auto& trackedBonePair : m_trackedBones)
        {
            const std::string& trackedBoneName = trackedBonePair.first;
            int foundId = -1;

            // Search for a matching bone in the animation
            for (const auto& boneInfoPair : m_stagedAnimationData->boneInfoMap)
            {
                const std::string& boneNameFromFile = boneInfoPair.first;
                
                if (juce::String(boneNameFromFile).endsWithIgnoreCase(trackedBoneName))
                {
                    foundId = boneInfoPair.second.id;
                    juce::Logger::writeToLog("AnimationModule: Refreshed tracked bone '" + juce::String(trackedBoneName) + 
                                             "' with ID " + juce::String(foundId));
                    break;
                }
            }
            
            trackedBonePair.second.boneId = foundId;
            
            if (foundId == -1)
            {
                juce::Logger::writeToLog("AnimationModule: WARNING - Tracked bone '" + juce::String(trackedBoneName) + 
                                         "' not found in new animation");
            }
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

// Helper function for tooltip with help marker
static void HelpMarker(const char* desc)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
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
            
            // Add/Remove Bone Output Buttons (with Undo/Redo support)
            bool isSelected = (m_selectedBoneName != "None" && !m_selectedBoneName.empty());
            bool isAlreadyTracked = false;
            if (isSelected)
            {
                const juce::ScopedLock lock(m_trackedBonesLock);
                isAlreadyTracked = m_trackedBones.count(m_selectedBoneName);
            }

            // "Add Bone Output" button
            if (!isSelected || isAlreadyTracked) ImGui::BeginDisabled();
            if (ImGui::Button("Add Bone Output", ImVec2(itemWidth / 2 - 2, 0)))
            {
                addTrackedBone(m_selectedBoneName);
                onModificationEnded(); // Trigger undo/redo snapshot
            }
            if (!isSelected || isAlreadyTracked) ImGui::EndDisabled();

            ImGui::SameLine();

            // "Remove Bone Output" button
            if (!isSelected || !isAlreadyTracked) ImGui::BeginDisabled();
            if (ImGui::Button("Remove Bone Output", ImVec2(itemWidth / 2 - 2, 0)))
            {
                removeTrackedBone(m_selectedBoneName);
                onModificationEnded(); // Trigger undo/redo snapshot
            }
            if (!isSelected || !isAlreadyTracked) ImGui::EndDisabled();
        }
    }
    
    // Build Triggers Audio Quick-Connect Button (80s blue style)
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.6f, 0.85f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.7f, 0.95f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.8f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    if (ImGui::Button("BUILD TRIGGERS AUDIO", ImVec2(itemWidth, 0)))
    {
        autoBuildTriggersAudioTriggered = true;
    }
    ImGui::PopStyleColor(4);
    HelpMarker("Auto-create samplers + mixer, wire triggers to pads.");
    
    
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
        
        
        // === GROUND PLANE CONTROLS ===
        ImGui::Separator();
        ImGui::Text("Ground Planes:");
        
        // Add/Remove Buttons
        if (ImGui::Button("Add Ground Plane", ImVec2(itemWidth / 2 - 2, 0)))
        {
            addGroundPlane(180.0f);
            onModificationEnded(); // Trigger undo/redo snapshot
        }
        ImGui::SameLine();
        
        bool canRemove = false;
        {
            const juce::ScopedLock lock(m_groundPlanesLock);
            canRemove = m_groundPlanes.size() > 1;
        }
        if (!canRemove) ImGui::BeginDisabled();
        if (ImGui::Button("Remove Ground Plane", ImVec2(itemWidth / 2 - 2, 0)))
        {
            removeGroundPlane();
            onModificationEnded(); // Trigger undo/redo snapshot
        }
        if (!canRemove) ImGui::EndDisabled();

        // Colored Sliders for each ground plane
        {
            const juce::ScopedLock lock(m_groundPlanesLock);
            for (int i = 0; i < (int)m_groundPlanes.size(); ++i)
            {
                ImGui::PushID(i); // Unique ID for each slider
                
                // Generate a distinct color for each slider
                float hue = fmodf((float)i * 0.2f, 1.0f); // 0.2 step for distinct hues
                ImGui::PushStyleColor(ImGuiCol_FrameBg, (ImVec4)ImColor::HSV(hue, 0.5f, 0.5f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, (ImVec4)ImColor::HSV(hue, 0.6f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, (ImVec4)ImColor::HSV(hue, 0.7f, 0.7f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, (ImVec4)ImColor::HSV(hue, 0.9f, 0.9f));

                ImGui::SliderFloat("Ground Y", &m_groundPlanes[i], 0.0f, 400.0f, "%.0f");

                // Trigger snapshot only when slider is released (best practice)
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    onModificationEnded();
                }

                ImGui::PopStyleColor(4);
                ImGui::PopID();
            }
        }
        ImGui::Separator();
        
        
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
        
        // --- DRAW ALL GROUND LINES (colored) ---
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p1 = ImGui::GetItemRectMin();
        ImVec2 p2 = ImGui::GetItemRectMax();
        
        // Draw each ground plane with its corresponding color
        std::vector<float> groundPlanesToDraw = getGroundPlanes();
        for (int i = 0; i < (int)groundPlanesToDraw.size(); ++i)
        {
            float groundY = groundPlanesToDraw[i];
            float hue = fmodf((float)i * 0.2f, 1.0f);
            ImVec4 colorVec = (ImVec4)ImColor::HSV(hue, 0.9f, 0.9f);
            ImU32 color = IM_COL32((int)(colorVec.x * 255), (int)(colorVec.y * 255), (int)(colorVec.z * 255), 255);
            drawList->AddLine(ImVec2(p1.x, p1.y + groundY), ImVec2(p2.x, p1.y + groundY), color, 2.0f);
        }
        
        // --- KINEMATIC CALCULATION BLOCK FOR ALL TRACKED BONES ---
        const juce::ScopedLock lock(m_trackedBonesLock);
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

                    // Ground trigger detection - check against ALL ground planes
                    bool hitDetected = false;
                    for (const float groundY : groundPlanesToDraw)
                    {
                        bool isBoneBelowGround = currentScreenPos.y > (viewportPos.y + groundY);
                        bool wasBoneBelowGround = bone.lastScreenPos.y > (viewportPos.y + groundY);
                        
                        // Trigger on downward crossing of this plane
                        if (isBoneBelowGround && !wasBoneBelowGround)
                        {
                            hitDetected = true;
                            break; // One hit is enough, no need to check other planes
                        }
                    }
                    bone.triggerState.store(hitDetected);
                    bone.wasBelowGround = false; // Not used anymore, but kept for compatibility

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
    // Support up to our max channels (dynamic bone outputs), and no inputs.
    const int maxChannels = MAX_TRACKED_BONES * 3;
    return layouts.getMainOutputChannelSet().size() <= maxChannels
           && layouts.getMainInputChannelSet() == juce::AudioChannelSet::disabled();
}

std::vector<DynamicPinInfo> AnimationModuleProcessor::getDynamicOutputPins() const
{
    // Dynamically generate output pins based on currently tracked bones
    const juce::ScopedLock lock(m_trackedBonesLock);
    
    std::vector<DynamicPinInfo> pins;
    pins.reserve(m_trackedBones.size() * 3);
    
    int channelIndex = 0;
    for (const auto& pair : m_trackedBones)
    {
        const std::string& boneName = pair.first;
        
        pins.push_back({ juce::String(boneName) + " Vel X", channelIndex++, PinDataType::CV });
        pins.push_back({ juce::String(boneName) + " Vel Y", channelIndex++, PinDataType::CV });
        pins.push_back({ juce::String(boneName) + " Hit",   channelIndex++, PinDataType::Gate });
    }
    
    return pins;
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
    state.setProperty("viewRotationX", m_viewRotationX, nullptr);
    state.setProperty("viewRotationY", m_viewRotationY, nullptr);
    state.setProperty("viewRotationZ", m_viewRotationZ, nullptr);

    // 3. Save the list of ground planes
    juce::ValueTree groundPlanesNode("GroundPlanes");
    {
        const juce::ScopedLock lock(m_groundPlanesLock);
        for (const float y : m_groundPlanes)
        {
            juce::ValueTree planeNode("Plane");
            planeNode.setProperty("y", y, nullptr);
            groundPlanesNode.addChild(planeNode, -1, nullptr);
        }
    }
    state.addChild(groundPlanesNode, -1, nullptr);

    // 4. Save the list of tracked bones
    juce::ValueTree trackedBonesNode("TrackedBones");
    {
        const juce::ScopedLock lock(m_trackedBonesLock);
        for (const auto& pair : m_trackedBones)
        {
            juce::ValueTree boneNode("Bone");
            boneNode.setProperty("name", juce::String(pair.first), nullptr);
            trackedBonesNode.addChild(boneNode, -1, nullptr);
        }
    }
    state.addChild(trackedBonesNode, -1, nullptr);

    // 5. Save the name of the currently selected bone.
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

    // === CRITICAL: ORDER OF OPERATIONS FOR PRESET LOADING ===
    // We must load the animation file FIRST so that bone names can be matched to IDs afterward.
    
    // 1. Restore the viewport/camera settings.
    m_zoom = state.getProperty("zoom", 10.0f);
    m_panX = state.getProperty("panX", 0.0f);
    m_panY = state.getProperty("panY", 0.0f);
    m_viewRotationX = state.getProperty("viewRotationX", 0.0f);
    m_viewRotationY = state.getProperty("viewRotationY", 0.0f);
    m_viewRotationZ = state.getProperty("viewRotationZ", 0.0f);

    // 2. Restore the selected bone name (for UI dropdown).
    m_selectedBoneName = state.getProperty("selectedBoneName", "None").toString().toStdString();

    // 3. IMPORTANT: Load the animation file BEFORE restoring tracked bones.
    juce::String filePath = state.getProperty("animationFilePath", "").toString();
    
    if (filePath.isNotEmpty())
    {
        juce::File fileToLoad(filePath);
        
        if (fileToLoad.existsAsFile())
        {
            juce::Logger::writeToLog("[AnimationModule] Restoring animation from preset: " + fileToLoad.getFullPathName());
            m_fileLoader.startLoadingFile(fileToLoad);
            // Note: This is async, but setupAnimationFromRawData is now non-destructive, so the
            // tracked bones we restore next won't be cleared.
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

    // 4. Restore the list of tracked bones from the preset.
    // This happens AFTER we've started loading the file (which is async).
    // Since setupAnimationFromRawData is now non-destructive, it won't clear this list.
    if (auto trackedBonesNode = state.getChildWithName("TrackedBones"); trackedBonesNode.isValid())
    {
        const juce::ScopedLock lock(m_trackedBonesLock);
        m_trackedBones.clear(); // Clear the current list
        for (const auto& boneNode : trackedBonesNode)
        {
            if (boneNode.hasType("Bone"))
            {
                juce::String boneName = boneNode.getProperty("name").toString();
                if (boneName.isNotEmpty())
                {
                    m_trackedBones[boneName.toStdString()];
                    m_trackedBones[boneName.toStdString()].name = boneName.toStdString();
                    juce::Logger::writeToLog("[AnimationModule] Restored tracked bone: " + boneName);
                }
            }
        }
    }

    // 5. Restore the list of ground planes.
    if (auto groundPlanesNode = state.getChildWithName("GroundPlanes"); groundPlanesNode.isValid())
    {
        const juce::ScopedLock lock(m_groundPlanesLock);
        m_groundPlanes.clear();
        for (const auto& planeNode : groundPlanesNode)
        {
            if (planeNode.hasType("Plane"))
            {
                m_groundPlanes.push_back(planeNode.getProperty("y", 180.0f));
            }
        }
        // Fallback: if loading results in no ground planes, add one as a safety
        if (m_groundPlanes.empty())
        {
            m_groundPlanes.push_back(180.0f);
        }
    }
    else
    {
        // Legacy support: try to load single groundY value
        float legacyGroundY = state.getProperty("groundY", 180.0f);
        const juce::ScopedLock lock(m_groundPlanesLock);
        m_groundPlanes.clear();
        m_groundPlanes.push_back(legacyGroundY);
    }
    
    juce::Logger::writeToLog("[AnimationModule] Preset loading complete.");
}

