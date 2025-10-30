#pragma once

#include <juce_opengl/juce_opengl.h>
#include <glm/glm.hpp>
#include <vector>

class AnimationRenderer
{
public:
    AnimationRenderer();
    ~AnimationRenderer();

    // Call once to set up OpenGL resources
    void setup(int width, int height);

    // Call every frame to draw the skeleton
    void render(const std::vector<glm::mat4>& finalBoneMatrices);

    // Get the ID of the final texture to display in ImGui
    GLuint getTextureID() const { return fboTextureID; }
    
    // Set zoom level (affects orthographic projection)
    void setZoom(float zoom) { m_zoom = zoom; }
    
    // Set pan offset (affects orthographic projection)
    void setPan(const glm::vec2& pan) { m_pan = pan; }
    
    // Set the view rotation in radians
    void setViewRotation(const glm::vec3& rotation) { m_viewRotation = rotation; }
    
    // Calculate optimal zoom and pan to frame all bones in view
    void frameView(const std::vector<glm::mat4>& boneMatrices, float& outZoom, glm::vec2& outPan, float& outMinY, float& outMaxY);

private:
    void createFramebuffer(int width, int height);
    void createShaders();

    GLuint fboID = 0;
    GLuint fboTextureID = 0;
    GLuint rboDepthID = 0; // Renderbuffer for depth testing
    GLuint shaderProgramID = 0;

    int textureWidth = 0;
    int textureHeight = 0;
    
    bool m_isInitialized = false; // Track if setup has been called
    float m_zoom = 10.0f; // Zoom level for orthographic projection
    glm::vec2 m_pan = { 0.0f, 0.0f }; // Pan offset for orthographic projection
    glm::vec3 m_viewRotation = { 0.0f, 0.0f, 0.0f }; // View rotation angles in radians
};

