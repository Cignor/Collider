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

    out VS_OUT {
        vec3 color;
    } vs_out;

    uniform mat4 boneMatrices[100];
    uniform vec3 boneColors[100];

    void main()
    {
        // Pass the bone's world position and color directly to the geometry shader
        mat4 boneTransform = boneMatrices[gl_VertexID];
        gl_Position = boneTransform * vec4(0.0, 0.0, 0.0, 1.0);
        vs_out.color = boneColors[gl_VertexID];
    }
)glsl";

const char* geometryShaderSource = R"glsl(
    #version 330 core
    layout (points) in;
    layout (triangle_strip, max_vertices = 4) out;

    in VS_OUT {
        vec3 color;
    } gs_in[];

    out vec3 fColor;
    out vec2 quadCoord;

    uniform mat4 projection;
    uniform float pointRadius; // New uniform to control size in world units

    void main() {
        fColor = gs_in[0].color;
        
        // Get the world position from the vertex shader
        vec3 worldPos = gl_in[0].gl_Position.xyz;

        // Calculate billboard corner offsets that always face the camera
        // We get these from the inverse of the projection matrix
        vec3 camRight_worldspace = vec3(1.0, 0.0, 0.0);
        vec3 camUp_worldspace = vec3(0.0, 1.0, 0.0);
        
        float radius = pointRadius;

        // Bottom-left
        quadCoord = vec2(-1.0, -1.0);
        gl_Position = projection * vec4(worldPos - camRight_worldspace * radius - camUp_worldspace * radius, 1.0);
        EmitVertex();

        // Top-left
        quadCoord = vec2(-1.0, 1.0);
        gl_Position = projection * vec4(worldPos - camRight_worldspace * radius + camUp_worldspace * radius, 1.0);
        EmitVertex();

        // Bottom-right
        quadCoord = vec2(1.0, -1.0);
        gl_Position = projection * vec4(worldPos + camRight_worldspace * radius - camUp_worldspace * radius, 1.0);
        EmitVertex();

        // Top-right
        quadCoord = vec2(1.0, 1.0);
        gl_Position = projection * vec4(worldPos + camRight_worldspace * radius + camUp_worldspace * radius, 1.0);
        EmitVertex();

        EndPrimitive();
    }
)glsl";

const char* fragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;

    in vec3 fColor;
    in vec2 quadCoord;

    void main()
    {
        // Create a circular shape instead of a square
        if (dot(quadCoord, quadCoord) > 1.0) {
            discard;
        }
        FragColor = vec4(fColor, 1.0);
    }
)glsl";

const char* lineVertexShaderSource = R"glsl(
    #version 330 core
    layout (location = 0) in vec3 aPos;

    uniform mat4 projectionView;

    void main()
    {
        gl_Position = projectionView * vec4(aPos, 1.0);
    }
)glsl";

const char* lineFragmentShaderSource = R"glsl(
    #version 330 core
    out vec4 FragColor;

    void main()
    {
        // Light, semi-transparent grey for the edges
        FragColor = vec4(0.8, 0.8, 0.8, 0.6);
    }
)glsl";

AnimationRenderer::AnimationRenderer()
{
}

AnimationRenderer::~AnimationRenderer()
{
    if (pointShaderProgramID != 0) glDeleteProgram(pointShaderProgramID);
    if (lineShaderProgramID != 0) glDeleteProgram(lineShaderProgramID);

    if (fboTextureID != 0)
        glDeleteTextures(1, &fboTextureID);
    if (rboDepthID != 0)
        glDeleteRenderbuffers(1, &rboDepthID);
    if (fboID != 0)
        glDeleteFramebuffers(1, &fboID);

    if (lineVBO != 0) glDeleteBuffers(1, &lineVBO);
    if (lineVAO != 0) glDeleteVertexArrays(1, &lineVAO);
}

void AnimationRenderer::setup(int width, int height)
{
    if (m_isInitialized)
        return;

    createShaders();
    createFramebuffer(width, height);

    // NEW: Setup VAO/VBO for line drawing
    glGenVertexArrays(1, &lineVAO);
    glGenBuffers(1, &lineVBO);

    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    // The data will be uploaded each frame, so we just set up the attribute pointer here
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_isInitialized = true;
}

void AnimationRenderer::render(const std::vector<glm::mat4>& finalBoneMatrices, const std::vector<glm::vec3>& boneColors, const std::vector<glm::vec3>& boneEdges)
{
    if (finalBoneMatrices.empty() || pointShaderProgramID == 0)
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
    glEnable(GL_BLEND); // Enable blending for semi-transparent lines
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Clear the framebuffer
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // --- CREATE PROJECTION-VIEW MATRIX ---
    // 1. Set up orthographic projection matrix (zoom and pan controlled by m_zoom and m_pan)
    glm::mat4 projection = glm::ortho(-m_zoom + m_pan.x, m_zoom + m_pan.x, -m_zoom + m_pan.y, m_zoom + m_pan.y, -10.0f, 10.0f);

    // 2. Create the view matrix from our rotation angles
    glm::mat4 view = glm::mat4(1.0f);
    view = glm::rotate(view, m_viewRotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    view = glm::rotate(view, m_viewRotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    view = glm::rotate(view, m_viewRotation.z, glm::vec3(0.0f, 0.0f, 1.0f));

    // 3. Combine them into a final projection-view matrix
    glm::mat4 projectionView = projection * view;

    // --- PASS 1: DRAW LINES (Edges) ---
    if (!boneEdges.empty())
    {
        glUseProgram(lineShaderProgramID);
        glUniformMatrix4fv(glGetUniformLocation(lineShaderProgramID, "projectionView"), 1, GL_FALSE, &projectionView[0][0]);

        glBindVertexArray(lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
        // Upload vertex data for the lines for this frame
        glBufferData(GL_ARRAY_BUFFER, boneEdges.size() * sizeof(glm::vec3), boneEdges.data(), GL_DYNAMIC_DRAW);
        
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(boneEdges.size()));
        
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }
    
    // --- PASS 2: DRAW POINTS (Bones) ---
    glEnable(GL_PROGRAM_POINT_SIZE); // Required for gl_PointSize in shader to work
    glUseProgram(pointShaderProgramID);
    
    // 4. Send matrices and radius to the shader uniforms
    glUniformMatrix4fv(glGetUniformLocation(pointShaderProgramID, "projection"), 1, GL_FALSE, &projectionView[0][0]);
    glUniform1f(glGetUniformLocation(pointShaderProgramID, "pointRadius"), 0.02f);

    // Send the bone matrices to the shader
    glUniformMatrix4fv(glGetUniformLocation(pointShaderProgramID, "boneMatrices"), 
                      static_cast<GLsizei>(finalBoneMatrices.size()), 
                      GL_FALSE, 
                      &finalBoneMatrices[0][0][0]);
    
    // Send the bone colors to the shader (default to white if no colors provided)
    if (!boneColors.empty() && boneColors.size() >= finalBoneMatrices.size())
    {
        glUniform3fv(glGetUniformLocation(pointShaderProgramID, "boneColors"),
                     static_cast<GLsizei>(finalBoneMatrices.size()),
                     &boneColors[0][0]);
    }
    else
    {
        // Default all bones to white if no color data provided
        std::vector<glm::vec3> defaultColors(finalBoneMatrices.size(), glm::vec3(1.0f, 1.0f, 1.0f));
        glUniform3fv(glGetUniformLocation(pointShaderProgramID, "boneColors"),
                     static_cast<GLsizei>(defaultColors.size()),
                     &defaultColors[0][0]);
    }

    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(finalBoneMatrices.size()));
    glDisable(GL_PROGRAM_POINT_SIZE);

    // --- RESTORE IMGUI'S OPENGL STATE ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind FBO first
    glDisable(GL_BLEND);
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

// Shader helper function to avoid duplicating compilation/linking code
GLuint AnimationRenderer::createShaderProgram(const char* vsSource, const char* gsSource, const char* fsSource)
{
    GLint success;
    GLchar infoLog[512];

    // Vertex Shader
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSource, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vs, 512, NULL, infoLog);
        DBG("ERROR::SHADER::VERTEX::COMPILATION_FAILED: " + juce::String(infoLog));
    }

    // Geometry Shader (optional)
    GLuint gs = 0;
    if (gsSource != nullptr) {
        gs = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(gs, 1, &gsSource, NULL);
        glCompileShader(gs);
        glGetShaderiv(gs, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(gs, 512, NULL, infoLog);
            DBG("ERROR::SHADER::GEOMETRY::COMPILATION_FAILED: " + juce::String(infoLog));
        }
    }

    // Fragment Shader
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSource, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fs, 512, NULL, infoLog);
        DBG("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED: " + juce::String(infoLog));
    }

    // Shader Program
    GLuint programID = glCreateProgram();
    glAttachShader(programID, vs);
    if (gs != 0) glAttachShader(programID, gs);
    glAttachShader(programID, fs);
    glLinkProgram(programID);
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(programID, 512, NULL, infoLog);
        DBG("ERROR::SHADER::PROGRAM::LINKING_FAILED: " + juce::String(infoLog));
    }

    // Delete shaders as they're now linked into our program
    glDeleteShader(vs);
    if (gs != 0) glDeleteShader(gs);
    glDeleteShader(fs);
    
    return programID;
}

void AnimationRenderer::createShaders()
{
    // Create the shader program for drawing points
    pointShaderProgramID = createShaderProgram(vertexShaderSource, geometryShaderSource, fragmentShaderSource);
    
    // Create the shader program for drawing lines
    lineShaderProgramID = createShaderProgram(lineVertexShaderSource, nullptr, lineFragmentShaderSource);
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

