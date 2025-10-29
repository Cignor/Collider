#include "AnimationRenderer.h"
#include <juce_core/juce_core.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>

// Use JUCE's OpenGL extension wrapper - this provides all the modern OpenGL functions
using namespace juce::gl;

// --- GLSL Shader Source Code ---

const char* vertexShaderSource = R"glsl(
    #version 330 core
    layout (location = 0) in vec3 aPos;

    uniform mat4 projection;
    uniform mat4 boneMatrices[100]; // Max 100 bones

    void main()
    {
        // For simplicity, we assume the input position is the bone's position
        // and its index is passed via gl_VertexID.
        mat4 boneTransform = boneMatrices[gl_VertexID];
        gl_Position = projection * boneTransform * vec4(0.0, 0.0, 0.0, 1.0);
        gl_PointSize = 10.0;  // Set point size in shader
    }
)glsl";

const char* fragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;

    void main()
    {
        FragColor = vec4(1.0, 1.0, 1.0, 1.0); // White
    }
)glsl";

AnimationRenderer::AnimationRenderer()
{
}

AnimationRenderer::~AnimationRenderer()
{
    if (shaderProgramID != 0)
        glDeleteProgram(shaderProgramID);
    if (fboTextureID != 0)
        glDeleteTextures(1, &fboTextureID);
    if (rboDepthID != 0)
        glDeleteRenderbuffers(1, &rboDepthID);
    if (fboID != 0)
        glDeleteFramebuffers(1, &fboID);
}

void AnimationRenderer::setup(int width, int height)
{
    if (m_isInitialized)
        return;

    createShaders();
    createFramebuffer(width, height);
    m_isInitialized = true;
}

void AnimationRenderer::render(const std::vector<glm::mat4>& finalBoneMatrices)
{
    if (finalBoneMatrices.empty() || shaderProgramID == 0)
        return;

    // --- SAVE IMGUI'S OPENGL STATE ---
    GLint last_program;
    glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_array_buffer;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    GLint last_viewport[4];
    glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
    GLboolean last_enable_scissor_test = glIsEnabled(GL_SCISSOR_TEST);

    // --- CONFIGURE OPENGL FOR OUR FBO ---
    glBindFramebuffer(GL_FRAMEBUFFER, fboID);
    glViewport(0, 0, textureWidth, textureHeight);
    glDisable(GL_SCISSOR_TEST); // We want to clear and draw to the whole FBO

    // Clear the framebuffer
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- DRAW OUR SCENE ---
    glUseProgram(shaderProgramID);

    // Set up projection matrix (zoom and pan controlled by m_zoom and m_pan)
    glm::mat4 projection = glm::ortho(
        -m_zoom + m_pan.x, m_zoom + m_pan.x,
        -m_zoom + m_pan.y, m_zoom + m_pan.y,
        -10.0f, 10.0f
    );
    glUniformMatrix4fv(glGetUniformLocation(shaderProgramID, "projection"), 1, GL_FALSE, &projection[0][0]);

    // Send the bone matrices to the shader
    glUniformMatrix4fv(glGetUniformLocation(shaderProgramID, "boneMatrices"), 
                      static_cast<GLsizei>(finalBoneMatrices.size()), 
                      GL_FALSE, 
                      &finalBoneMatrices[0][0][0]);

    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(finalBoneMatrices.size()));


    // --- RESTORE IMGUI'S OPENGL STATE ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind FBO first
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor_box[0], last_scissor_box[1], (GLsizei)last_scissor_box[2], (GLsizei)last_scissor_box[3]);
    if (last_enable_scissor_test) {
        glEnable(GL_SCISSOR_TEST);
    }
}

void AnimationRenderer::createFramebuffer(int width, int height)
{
    textureWidth = width;
    textureHeight = height;

    glGenFramebuffers(1, &fboID);
    glBindFramebuffer(GL_FRAMEBUFFER, fboID);

    // Create Color Texture Attachment
    glGenTextures(1, &fboTextureID);
    glBindTexture(GL_TEXTURE_2D, fboTextureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTextureID, 0);

    // Create Depth Renderbuffer Attachment
    glGenRenderbuffers(1, &rboDepthID);
    glBindRenderbuffer(GL_RENDERBUFFER, rboDepthID);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rboDepthID);

    // Check if framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        DBG("ERROR::FRAMEBUFFER:: Framebuffer is not complete!");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AnimationRenderer::createShaders()
{
    // Compile Vertex Shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);
    
    // Check for vertex shader compile errors
    GLint success;
    GLchar infoLog[512];
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vs, 512, NULL, infoLog);
        DBG("ERROR::SHADER::VERTEX::COMPILATION_FAILED: " + juce::String(infoLog));
    }

    // Compile Fragment Shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);
    
    // Check for fragment shader compile errors
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fs, 512, NULL, infoLog);
        DBG("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED: " + juce::String(infoLog));
    }

    // Link Shaders into a Program
    shaderProgramID = glCreateProgram();
    glAttachShader(shaderProgramID, vs);
    glAttachShader(shaderProgramID, fs);
    glLinkProgram(shaderProgramID);
    
    // Check for linking errors
    glGetProgramiv(shaderProgramID, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgramID, 512, NULL, infoLog);
        DBG("ERROR::SHADER::PROGRAM::LINKING_FAILED: " + juce::String(infoLog));
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
}

void AnimationRenderer::frameView(const std::vector<glm::mat4>& boneMatrices, float& outZoom, glm::vec2& outPan)
{
    if (boneMatrices.empty())
        return;

    // Find the bounding box of all bone positions
    glm::vec2 minPoint(std::numeric_limits<float>::max());
    glm::vec2 maxPoint(std::numeric_limits<float>::lowest());

    for (const auto& matrix : boneMatrices)
    {
        glm::vec3 position = matrix[3]; // Position is in the 4th column

        // === START FIX: Ignore bones at or very near the origin ===
        // This prevents non-skeleton helper nodes from ruining the auto-frame.
        if (glm::length(position) < 0.001f)
        {
            continue; // Skip this bone
        }
        // === END FIX ===

        minPoint.x = std::min(minPoint.x, position.x);
        minPoint.y = std::min(minPoint.y, position.y);
        maxPoint.x = std::max(maxPoint.x, position.x);
        maxPoint.y = std::max(maxPoint.y, position.y);
    }

    // Calculate the center of the bounding box
    outPan = (minPoint + maxPoint) * 0.5f;

    // Calculate the size needed to contain the bounding box
    glm::vec2 size = maxPoint - minPoint;
    float requiredZoom = glm::max(size.x, size.y) * 0.5f;

    // Set the zoom with a little padding
    outZoom = requiredZoom * 1.1f;
}

