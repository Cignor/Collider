#include "AnimationModuleProcessor.h"
#include "../../animation/FbxLoader.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

AnimationModuleProcessor::AnimationModuleProcessor()
    : ModuleProcessor(BusesProperties()
                        .withOutput("Output", juce::AudioChannelSet::discreteChannels(5), true)), // X Pos, Y Pos, X Vel, Y Vel, Ground Trigger
      apvts(*this, nullptr, "AnimationParams", {})
{
    // Constructor: m_AnimationData and m_Animator are nullptrs initially.
    m_Renderer = std::make_unique<AnimationRenderer>();
    
    // DEBUG: Verify output channel count
    juce::Logger::writeToLog("[AnimationModule] Constructor: getTotalNumOutputChannels() = " + 
                             juce::String(getTotalNumOutputChannels()));
}

AnimationModuleProcessor::~AnimationModuleProcessor()
{
    // Destructor: unique_ptrs will automatically clean up the animator and data.
}

void AnimationModuleProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // We don't need to do anything special here for this module,
    // but the override is required.
}

void AnimationModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Calculate the time elapsed for this audio block.
    const float deltaTime = buffer.getNumSamples() / getSampleRate();

    // Lock the mutex to ensure the UI thread doesn't access the animator while we update it.
    const juce::ScopedLock scopedLock(m_AnimatorLock);

    if (m_Animator)
    {
        m_Animator->Update(deltaTime);
    }
    
    // Clear the output buffer first
    buffer.clear();
    
    // --- OUTPUT KINEMATIC DATA ---
    // Load the thread-safe atomic values
    float posX = m_outputPosX.load();
    float posY = m_outputPosY.load();
    float velX = m_outputVelX.load();
    float velY = m_outputVelY.load();
    
    // Debug: Log values occasionally
    static int debugCounter = 0;
    if (++debugCounter % 480 == 0) // Every ~0.1 seconds at 48kHz
    {
        DBG("Animation outputs: PosX=" << posX << " PosY=" << posY << " VelX=" << velX << " VelY=" << velY);
    }

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

void AnimationModuleProcessor::loadFile(const juce::File& file)
{
    std::string path = file.getFullPathName().toStdString();
    std::unique_ptr<AnimationData> loadedData;

    // Choose the correct loader based on file extension
    if (file.hasFileExtension(".fbx"))
    {
        juce::Logger::writeToLog("[AnimationModule] Loading FBX file: " + file.getFullPathName());
        loadedData = FbxLoader::LoadFromFile(path);
    }
    else if (file.hasFileExtension(".gltf") || file.hasFileExtension(".glb"))
    {
        juce::Logger::writeToLog("[AnimationModule] Loading glTF file: " + file.getFullPathName());
        loadedData = GltfLoader::LoadFromFile(path);
    }
    else
    {
        juce::Logger::writeToLog("[AnimationModule] ERROR: Unsupported file format: " + file.getFullPathName());
        return;
    }

    if (!loadedData)
    {
        // Handle error: file failed to load
        juce::Logger::writeToLog("[AnimationModule] ERROR: Failed to load file: " + file.getFullPathName());
        return;
    }

    // Lock the mutex before swapping out the animation data and animator.
    const juce::ScopedLock scopedLock(m_AnimatorLock);

    m_AnimationData = std::move(loadedData);
    m_Animator = std::make_unique<Animator>(m_AnimationData.get());

    // Automatically play the first animation clip, if one exists.
    if (!m_AnimationData->animationClips.empty())
    {
        m_Animator->PlayAnimation(m_AnimationData->animationClips[0].name);
    }

    juce::Logger::writeToLog("[AnimationModule] Successfully loaded animation file");
}

const std::vector<glm::mat4>& AnimationModuleProcessor::getFinalBoneMatrices() const
{
    // Lock the mutex to ensure the audio thread doesn't change the data while we are reading it.
    const juce::ScopedLock scopedLock(m_AnimatorLock);

    if (m_Animator)
    {
        return m_Animator->GetFinalBoneMatrices();
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
    if (m_AnimationData)
    {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Loaded");
        ImGui::Text("Bones: %zu", m_AnimationData->boneInfoMap.size());
        ImGui::Text("Clips: %zu", m_AnimationData->animationClips.size());
    }
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No file loaded");
    }
    
    if (ImGui::Button("Load Animation File...", ImVec2(itemWidth, 0)))
    {
        // Create a JUCE file chooser to select a glTF, GLB, or FBX file.
        // Store it as a member to keep it alive during the async operation
        m_FileChooser = std::make_unique<juce::FileChooser>(
            "Select an animation file (glTF/FBX)...",
            juce::File{},
            "*.gltf;*.glb;*.fbx");

        auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        // Launch the file chooser asynchronously.
        // The lambda function will be called when the user makes a selection.
        m_FileChooser->launchAsync(flags, [this](const juce::FileChooser& chooser)
        {
            if (chooser.getResults().isEmpty())
                return; // User cancelled

            juce::File file = chooser.getResult();
            
            // Call the load function on our module.
            this->loadFile(file);
        });
    }
    
    
    // --- BONE SELECTION ---
    if (m_AnimationData && !m_AnimationData->boneInfoMap.empty())
    {
        if (ImGui::BeginCombo("Selected Bone", m_selectedBoneName.c_str()))
        {
            // Add a "None" option
            bool isNoneSelected = (m_selectedBoneIndex == -1);
            if (ImGui::Selectable("None", isNoneSelected))
            {
                m_selectedBoneIndex = -1;
                m_selectedBoneName = "None";
            }

            // Iterate through all bones in the map
            int currentIndex = 0;
            for (const auto& pair : m_AnimationData->boneInfoMap)
            {
                const std::string& boneName = pair.first;
                bool isSelected = (m_selectedBoneName == boneName);

                if (ImGui::Selectable(boneName.c_str(), isSelected))
                {
                    m_selectedBoneName = boneName;
                    m_selectedBoneIndex = currentIndex;
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
    
    
    // Animation playback controls
    if (m_Animator && m_AnimationData)
    {
        ImGui::Text("Animation Controls:");
        
        // List available clips
        if (!m_AnimationData->animationClips.empty())
        {
            ImGui::Text("Available Clips:");
            for (size_t i = 0; i < m_AnimationData->animationClips.size(); ++i)
            {
                const auto& clip = m_AnimationData->animationClips[i];
                if (ImGui::Button(clip.name.c_str(), ImVec2(itemWidth, 0)))
                {
                    const juce::ScopedLock lock(m_AnimatorLock);
                    m_Animator->PlayAnimation(clip.name);
                }
            }
        }
        
        
        // Speed control
        static float speed = 1.0f;
        if (ImGui::SliderFloat("Speed", &speed, 0.1f, 3.0f, "%.2f"))
        {
            const juce::ScopedLock lock(m_AnimatorLock);
            m_Animator->SetAnimationSpeed(speed);
        }
        
        
        // --- RENDERING VIEWPORT ---
        
        ImGui::Text("Animation Viewport:");
        
        // Camera controls
        ImGui::SliderFloat("Zoom", &m_zoom, 1.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Pan X", &m_panX, -20.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Pan Y", &m_panY, -20.0f, 20.0f, "%.1f");
        
        // Frame view button - auto-calculates optimal zoom and pan
        if (ImGui::Button("Frame View", ImVec2(itemWidth, 0)))
        {
            glm::vec2 newPan;
            m_Renderer->frameView(getFinalBoneMatrices(), m_zoom, newPan);
            m_panX = newPan.x;
            m_panY = newPan.y;
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
        
        // Render the animation for this frame
        m_Renderer->render(getFinalBoneMatrices());
        
        // Display the texture from the FBO (flipped vertically)
        ImGui::Image((void*)(intptr_t)m_Renderer->getTextureID(), viewportSize, ImVec2(0, 1), ImVec2(1, 0));
        
        // --- DRAW GROUND LINE ---
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 p1 = ImGui::GetItemRectMin();
        ImVec2 p2 = ImGui::GetItemRectMax();
        drawList->AddLine(ImVec2(p1.x, p1.y + m_groundY), ImVec2(p2.x, p1.y + m_groundY), IM_COL32(255, 0, 0, 255), 2.0f);
        
        // --- KINEMATIC CALCULATION BLOCK ---
        if (m_selectedBoneIndex != -1 && m_Animator && m_AnimationData)
        {
            // 1. Get the bone's world matrix
            const auto& boneInfoMap = m_AnimationData->boneInfoMap;
            auto it = std::find_if(boneInfoMap.begin(), boneInfoMap.end(),
                [this](const auto& pair){ return pair.second.name == m_selectedBoneName; });

            if (it != boneInfoMap.end())
            {
                int boneId = it->second.id;
                const auto& finalMatrices = getFinalBoneMatrices();
                if (boneId < finalMatrices.size())
                {
                    glm::mat4 worldMatrix = finalMatrices[boneId];
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

