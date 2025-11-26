# Video Draw Impact Node - Detailed Implementation Plan

**Date**: 2025-01-XX  
**Feature**: Video Draw Impact Node for Interactive Video Drawing with Frame Persistence  
**Status**: Planning Phase

---

## Executive Summary

Implement a new `VideoDrawImpactModuleProcessor` node that allows users to draw colored "impact" marks on video frames. These drawings persist for a configurable number of frames (default: 3 frames), creating a visual rhythm that can be tracked by the Color Tracker node. The node includes saturation control to desaturate the input video, making drawn colors stand out more clearly. This enables users to quickly draw rhythmic patterns (footsteps, gunfire, etc.) that can be detected and converted to audio triggers.

---

## Feature Overview

### Core Functionality
1. **Video Input Processing**: Receives video from input pin (Source ID)
2. **Saturation Control**: Slider to desaturate input video (0.0 = grayscale, 1.0 = full color)
3. **Color Selection**: Color wheel picker to choose drawing color
4. **Interactive Drawing**: Click/drag on video preview to draw impact marks
5. **Erase Mode**: Right-click to erase drawings at cursor position
6. **Frame Persistence**: Each drawing persists for N frames (configurable, default: 3)
7. **Timeline View**: Visual timeline showing drawing keyframes (similar to MIDI Player piano roll)
8. **Video Output**: Outputs processed video with drawings to next node (e.g., Color Tracker)
9. **Drawing Lifetime Management**: Automatic cleanup of expired drawings

### Use Cases
- **Rhythm Visualization**: Draw footsteps, gunfire, or other rhythmic events on video
- **Color Tracking Preparation**: Create colored marks that Color Tracker can detect
- **Interactive Video Annotation**: Mark specific moments or areas in video
- **Performance Art**: Create visual rhythms synchronized with audio
- **Game Audio Design**: Visualize and trigger audio events from drawn impacts

---

## Architecture Design

### 1. Module Structure

**Class Name**: `VideoDrawImpactModuleProcessor`  
**Base Class**: `ModuleProcessor`  
**Location**: `juce/Source/audio/modules/VideoDrawImpactModuleProcessor.h/cpp`

**Key Components**:
- Inherits from `ModuleProcessor` (like `VideoFXModule`)
- Uses background thread for video processing (like `VideoFXModule`)
- Uses `VideoFrameManager` for frame I/O
- Uses OpenCV for image processing and drawing
- Uses ImGui for interactive drawing UI

### 2. Parameter Layout

**APVTS Parameters**:
```cpp
// Saturation control (0.0 = grayscale, 1.0 = full color)
"saturation" - AudioParameterFloat: 0.0f to 1.0f, default 0.0f

// Drawing color (RGB stored as normalized floats)
"drawColorR" - AudioParameterFloat: 0.0f to 1.0f, default 1.0f (red)
"drawColorG" - AudioParameterFloat: 0.0f to 1.0f, default 0.0f (green)
"drawColorB" - AudioParameterFloat: 0.0f to 1.0f, default 0.0f (blue)

// Frame persistence (how many frames a drawing stays visible)
"framePersistence" - AudioParameterInt: 1 to 60, default 3

// Brush size (radius in pixels)
"brushSize" - AudioParameterInt: 1 to 50, default 5

// Clear all drawings button (trigger, not persistent)
"clearDrawings" - AudioParameterBool: default false (momentary trigger)
```

**Input Buses**:
- **Bus 0**: Video Source ID (1 channel, mono) - receives Source ID from previous node

**Output Buses**:
- **Bus 0**: Video Output (1 channel, mono) - outputs own Source ID for chaining

### 3. Drawing Data Structure

**Drawing Stroke Structure**:
```cpp
struct DrawingStroke
{
    std::vector<cv::Point2i> points;  // All points in the stroke
    cv::Scalar color;                 // BGR color (0-255)
    int remainingFrames;              // Frames until expiration
    int brushSize;                    // Brush size at time of drawing
    int startFrameNumber;             // Frame number when drawing was created (for timeline)
    bool isErase;                     // true if this is an erase operation
};
```

**Timeline Keyframe Structure**:
```cpp
struct TimelineKeyframe
{
    int frameNumber;                  // Frame number when drawing occurred
    cv::Scalar color;                // Color of the drawing
    int brushSize;                   // Brush size used
    bool isErase;                    // true if erase operation
    float normalizedX;               // X position in frame (0.0-1.0) for timeline display
    float normalizedY;               // Y position in frame (0.0-1.0) for timeline display
};
```

**Internal State**:
```cpp
// Active drawings (thread-safe)
std::vector<DrawingStroke> activeDrawings;
juce::CriticalSection drawingsLock;

// Pending drawing operations (from UI thread)
struct PendingDrawOperation
{
    cv::Point2i point;
    cv::Scalar color;
    int brushSize;
    bool isNewStroke;  // true = start new stroke, false = continue current
    bool isErase;      // true = erase operation (right-click)
};
std::vector<PendingDrawOperation> pendingDrawOps;
juce::CriticalSection pendingOpsLock;

// Timeline keyframes (for UI display)
std::vector<TimelineKeyframe> timelineKeyframes;
juce::CriticalSection timelineLock;
std::atomic<int> currentFrameNumber { 0 };  // Track current frame number

// Current drawing color (from parameters)
cv::Scalar currentDrawColor { 0, 0, 255 };  // BGR: default red

// Source ID (read from input pin)
std::atomic<juce::uint32> currentSourceId { 0 };

// UI preview
juce::Image latestFrameForGui;
juce::CriticalSection imageLock;

// Drawing state (UI thread)
bool isDrawing { false };
cv::Point2i lastDrawPoint { -1, -1 };
```

### 4. Processing Flow

**Background Thread (`run()` method)**:
1. Read Source ID from `currentSourceId` atomic
2. Get frame from `VideoFrameManager`
3. Increment `currentFrameNumber` (for timeline tracking)
4. Apply saturation adjustment to frame
5. Lock `drawingsLock` and process all active drawings:
   - Decrement `remainingFrames` for each drawing
   - Remove drawings where `remainingFrames <= 0`
   - Draw all active strokes onto frame using OpenCV
   - Handle erase operations (draw white/background color to erase)
6. Process pending draw operations from UI thread:
   - Lock `pendingOpsLock`
   - Apply pending operations to active drawings
   - Create timeline keyframes for new strokes
   - Clear pending operations
7. Publish processed frame to `VideoFrameManager`
8. Update GUI preview frame
9. Wait ~33ms (30 FPS)

**Audio Thread (`processBlock()` method)**:
1. Read Source ID from input bus channel 0
2. Store in `currentSourceId` atomic
3. Output own Logical ID on output bus channel 0

**UI Thread (`drawParametersInNode()` method)**:
1. Display saturation slider
2. Display color wheel picker (ImGui::ColorPicker4 with wheel mode)
3. Display frame persistence slider
4. Display brush size slider
5. Display "Clear All" button
6. Render timeline view (similar to MIDI Player piano roll):
   - Show frame numbers on horizontal axis
   - Display keyframes as colored markers
   - Show playhead position (if video is playing)
   - Allow clicking on timeline to seek (if supported)
7. Render video preview with drawing interaction:
   - Check if mouse is over preview area
   - On left mouse down: start new stroke (draw mode)
   - On right mouse down: start new erase stroke
   - On mouse drag: add points to current stroke
   - On mouse up: finalize stroke
   - Queue draw operations to `pendingDrawOps` (thread-safe)
   - Visual feedback: cursor change for draw/erase modes

### 5. Drawing Interaction Implementation

**Mouse-to-Frame Coordinate Conversion**:
```cpp
// In drawParametersInNode(), after rendering preview image:
ImVec2 previewMin = ImGui::GetItemRectMin();
ImVec2 previewMax = ImGui::GetItemRectMax();
ImVec2 mousePos = ImGui::GetMousePos();

if (mousePos.x >= previewMin.x && mousePos.x < previewMax.x &&
    mousePos.y >= previewMin.y && mousePos.y < previewMax.y)
{
    // Convert to frame coordinates
    float frameWidth = latestFrameForGui.getWidth();
    float frameHeight = latestFrameForGui.getHeight();
    float previewWidth = previewMax.x - previewMin.x;
    float previewHeight = previewMax.y - previewMin.y;
    
    int frameX = (int)((mousePos.x - previewMin.x) / previewWidth * frameWidth);
    int frameY = (int)((mousePos.y - previewMin.y) / previewHeight * frameHeight);
    
    // Clamp to frame bounds
    frameX = juce::jlimit(0, (int)frameWidth - 1, frameX);
    frameY = juce::jlimit(0, (int)frameHeight - 1, frameY);
    
    // Handle drawing (left-click = draw, right-click = erase)
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Add point to current stroke (draw mode)
        addDrawPoint(cv::Point2i(frameX, frameY), false);
    }
    else if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        // Add point to current stroke (erase mode)
        addDrawPoint(cv::Point2i(frameX, frameY), true);
    }
}
```

**Thread-Safe Drawing Queue**:
```cpp
void VideoDrawImpactModuleProcessor::addDrawPoint(const cv::Point2i& point, bool isErase)
{
    const juce::ScopedLock lock(pendingOpsLock);
    
    PendingDrawOperation op;
    op.point = point;
    op.color = isErase ? cv::Scalar(255, 255, 255) : currentDrawColor;  // White for erase
    op.brushSize = brushSizeParam ? brushSizeParam->get() : 5;
    op.isNewStroke = !isDrawing || (isErase != lastWasErase);  // New stroke if mode changed
    op.isErase = isErase;
    
    pendingDrawOps.push_back(op);
    isDrawing = true;
    lastDrawPoint = point;
    lastWasErase = isErase;
}
```

// Called in background thread
void VideoDrawImpactModuleProcessor::processPendingDrawOps()
{
    const juce::ScopedLock lock(pendingOpsLock);
    const juce::ScopedLock drawingsLock(activeDrawingsLock);
    const juce::ScopedLock timelineLock(timelineLock);
    
    DrawingStroke* currentStroke = nullptr;
    int currentFrame = currentFrameNumber.load();
    
    for (const auto& op : pendingDrawOps)
    {
        if (op.isNewStroke || currentStroke == nullptr)
        {
            // Start new stroke
            DrawingStroke newStroke;
            newStroke.color = op.color;
            newStroke.brushSize = op.brushSize;
            newStroke.remainingFrames = framePersistenceParam ? framePersistenceParam->get() : 3;
            newStroke.startFrameNumber = currentFrame;
            newStroke.isErase = op.isErase;
            newStroke.points.push_back(op.point);
            activeDrawings.push_back(newStroke);
            currentStroke = &activeDrawings.back();
            
            // Create timeline keyframe for new stroke
            if (op.points.size() > 0)
            {
                TimelineKeyframe keyframe;
                keyframe.frameNumber = currentFrame;
                keyframe.color = op.color;
                keyframe.brushSize = op.brushSize;
                keyframe.isErase = op.isErase;
                keyframe.normalizedX = (float)op.point.x / frameWidth;
                keyframe.normalizedY = (float)op.point.y / frameHeight;
                timelineKeyframes.push_back(keyframe);
            }
        }
        else
        {
            // Continue current stroke
            currentStroke->points.push_back(op.point);
        }
    }
    
    pendingDrawOps.clear();
}
```

### 6. Timeline View Implementation

**Timeline Rendering** (Similar to MIDI Player):
```cpp
void VideoDrawImpactModuleProcessor::drawTimelineView(float width, float height)
{
    // Reserve space for timeline content
    const float pixelsPerFrame = zoomX;  // User-adjustable zoom
    const int maxFrames = currentFrameNumber.load();
    const float totalWidth = maxFrames * pixelsPerFrame;
    
    ImGui::BeginChild("TimelineContent", ImVec2(width, height), true,
                      ImGuiWindowFlags_HorizontalScrollbar);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float scrollX = ImGui::GetScrollX();
    const ImVec2 timelineStartPos = ImGui::GetItemRectMin();
    
    // Draw background
    drawList->AddRectFilled(
        timelineStartPos,
        ImVec2(timelineStartPos.x + width, timelineStartPos.y + height),
        IM_COL32(30, 30, 30, 255)
    );
    
    // Reserve space for entire timeline
    ImGui::Dummy(ImVec2(totalWidth, height));
    
    // Scroll-aware culling: only draw visible keyframes
    const int firstFrame = juce::jmax(0, (int)(scrollX / pixelsPerFrame));
    const int lastFrame = juce::jmin(maxFrames, (int)((scrollX + width) / pixelsPerFrame) + 1);
    
    // Draw frame markers (every 10 frames or so)
    for (int frame = firstFrame; frame <= lastFrame; frame += 10)
    {
        const float x = timelineStartPos.x + (frame * pixelsPerFrame);
        drawList->AddLine(
            ImVec2(x, timelineStartPos.y),
            ImVec2(x, timelineStartPos.y + height),
            IM_COL32(70, 70, 70, 255),
            1.0f
        );
    }
    
    // Draw keyframes
    const juce::ScopedLock lock(timelineLock);
    for (const auto& keyframe : timelineKeyframes)
    {
        if (keyframe.frameNumber < firstFrame || keyframe.frameNumber > lastFrame)
            continue;  // Skip non-visible keyframes
        
        const float x = timelineStartPos.x + (keyframe.frameNumber * pixelsPerFrame);
        const float y = timelineStartPos.y + (keyframe.normalizedY * height);
        
        // Convert BGR to RGB for ImGui
        ImU32 color = IM_COL32(
            (int)(keyframe.color[2] * 255),  // R
            (int)(keyframe.color[1] * 255),  // G
            (int)(keyframe.color[0] * 255),  // B
            255
        );
        
        if (keyframe.isErase)
        {
            // Draw erase keyframe with X marker
            drawList->AddLine(
                ImVec2(x - 5, y - 5), ImVec2(x + 5, y + 5),
                IM_COL32(255, 100, 100, 255), 2.0f
            );
            drawList->AddLine(
                ImVec2(x + 5, y - 5), ImVec2(x - 5, y + 5),
                IM_COL32(255, 100, 100, 255), 2.0f
            );
        }
        else
        {
            // Draw normal keyframe as circle
            drawList->AddCircleFilled(
                ImVec2(x, y),
                (float)keyframe.brushSize / 2.0f,
                color
            );
            drawList->AddCircle(
                ImVec2(x, y),
                (float)keyframe.brushSize / 2.0f,
                IM_COL32(255, 255, 255, 255),
                1.0f
            );
        }
    }
    
    // Draw playhead (if video playback position available)
    // This would require integration with video playback system
    // For now, show current frame number
    const float playheadX = timelineStartPos.x + (currentFrameNumber.load() * pixelsPerFrame);
    drawList->AddLine(
        ImVec2(playheadX, timelineStartPos.y),
        ImVec2(playheadX, timelineStartPos.y + height),
        IM_COL32(255, 255, 0, 200),  // Yellow playhead
        2.0f
    );
    
    ImGui::EndChild();
}
```

**Keyframe Cleanup**:
```cpp
void VideoDrawImpactModuleProcessor::cleanupExpiredKeyframes()
{
    const juce::ScopedLock lock(timelineLock);
    int currentFrame = currentFrameNumber.load();
    
    // Remove keyframes older than max persistence frames
    int maxPersistence = framePersistenceParam ? framePersistenceParam->get() : 3;
    timelineKeyframes.erase(
        std::remove_if(timelineKeyframes.begin(), timelineKeyframes.end(),
            [currentFrame, maxPersistence](const TimelineKeyframe& kf) {
                return (currentFrame - kf.frameNumber) > maxPersistence;
            }),
        timelineKeyframes.end()
    );
}
```

### 7. Drawing Rendering

**OpenCV Drawing**:
```cpp
void VideoDrawImpactModuleProcessor::drawStrokesOnFrame(cv::Mat& frame)
{
    const juce::ScopedLock lock(drawingsLock);
    
    for (auto& stroke : activeDrawings)
    {
        if (stroke.isErase)
        {
            // Erase mode: draw with background color (or use inpainting)
            // Option 1: Draw with white/black (simple)
            cv::Scalar eraseColor(0, 0, 0);  // Black background, or use frame average
            
            if (stroke.points.size() < 2)
            {
                cv::circle(frame, stroke.points[0], stroke.brushSize, eraseColor, -1);
            }
            else
            {
                for (size_t i = 1; i < stroke.points.size(); ++i)
                {
                    cv::line(frame, stroke.points[i-1], stroke.points[i], 
                            eraseColor, stroke.brushSize * 2, cv::LINE_AA);  // Thicker for erase
                }
            }
        }
        else
        {
            // Draw mode: normal drawing
            if (stroke.points.size() < 2)
            {
                // Single point: draw circle
                cv::circle(frame, stroke.points[0], stroke.brushSize, stroke.color, -1);
            }
            else
            {
                // Multiple points: draw connected lines
                for (size_t i = 1; i < stroke.points.size(); ++i)
                {
                    cv::line(frame, stroke.points[i-1], stroke.points[i], 
                            stroke.color, stroke.brushSize, cv::LINE_AA);
                }
            }
        }
    }
}
```

---

## Implementation Phases

### Phase 1: Basic Module Structure (Difficulty: Low)

**Tasks**:
1. Create `VideoDrawImpactModuleProcessor.h` and `.cpp` files
2. Inherit from `ModuleProcessor` and `juce::Thread`
3. Implement basic constructor/destructor
4. Set up APVTS with parameter layout
5. Implement `getName()` returning `"video_draw_impact"`
6. Implement basic `prepareToPlay()` and `releaseResources()`
7. Implement basic `processBlock()` (read Source ID, output own ID)
8. Register module in module factory/registry

**Risk**: Low  
**Estimated Time**: 2-3 hours

**Dependencies**: None

---

### Phase 2: Video Processing Thread (Difficulty: Medium)

**Tasks**:
1. Implement `run()` method (background thread)
2. Set up frame reading from `VideoFrameManager`
3. Implement saturation adjustment (HSV conversion, desaturate, convert back)
4. Implement frame publishing to `VideoFrameManager`
5. Implement GUI frame update (`updateGuiFrame()`)
6. Test video passthrough without modifications

**Risk**: Medium (thread safety, frame timing)  
**Estimated Time**: 3-4 hours

**Dependencies**: Phase 1

---

### Phase 3: Drawing Data Structures (Difficulty: Low-Medium)

**Tasks**:
1. Define `DrawingStroke` struct
2. Define `PendingDrawOperation` struct
3. Implement thread-safe storage (`activeDrawings`, `pendingDrawOps`)
4. Implement `addDrawPoint()` method (queue operations)
5. Implement `processPendingDrawOps()` method (apply to active drawings)
6. Implement drawing expiration logic (decrement frames, remove expired)

**Risk**: Medium (thread synchronization)  
**Estimated Time**: 2-3 hours

**Dependencies**: Phase 2

---

### Phase 4: Drawing Rendering (Difficulty: Medium)

**Tasks**:
1. Implement `drawStrokesOnFrame()` using OpenCV drawing functions
2. Handle single-point strokes (circles)
3. Handle multi-point strokes (connected lines)
4. Integrate drawing rendering into background thread processing
5. Test drawing persistence (create drawing, verify it disappears after N frames)

**Risk**: Medium (OpenCV drawing API, performance)  
**Estimated Time**: 3-4 hours

**Dependencies**: Phase 3

---

### Phase 5: UI Parameters (Difficulty: Low-Medium)

**Tasks**:
1. Implement `drawParametersInNode()` method
2. Add saturation slider with modulation support
3. Add color wheel picker (ImGui::ColorPicker4 with ImGuiColorEditFlags_PickerHueWheel)
4. Add frame persistence slider (1-60 frames)
5. Add brush size slider (1-50 pixels)
6. Add "Clear All Drawings" button
7. Implement parameter change handlers
8. Add erase mode indicator/button (optional UI feedback)

**Risk**: Low-Medium (ImGui integration, parameter synchronization, color wheel API)  
**Estimated Time**: 4-5 hours

**Dependencies**: Phase 1

---

### Phase 6: Interactive Drawing UI (Difficulty: High)

**Tasks**:
1. Render video preview in `drawParametersInNode()` (similar to ColorTrackerModule)
2. Implement mouse coordinate conversion (screen → frame coordinates)
3. Implement left mouse down detection (start new draw stroke)
4. Implement right mouse down detection (start new erase stroke)
5. Implement mouse drag detection (add points to current stroke)
6. Implement mouse up detection (finalize stroke)
7. Handle mode switching (draw ↔ erase)
8. Handle edge cases (mouse leaves preview area, rapid clicks)
9. Visual feedback (cursor change for draw/erase modes, preview of drawing)

**Risk**: High (coordinate conversion, mouse event handling, thread safety, erase logic)  
**Estimated Time**: 6-7 hours

**Dependencies**: Phase 4, Phase 5

---

### Phase 7: State Persistence (Difficulty: Low)

**Tasks**:
1. Implement `getExtraStateTree()` (save current draw color, frame persistence, brush size)
2. Implement `setExtraStateTree()` (restore parameters)
3. Note: Active drawings are NOT persisted (by design - they're transient)
4. Test preset save/load

**Risk**: Low  
**Estimated Time**: 1-2 hours

**Dependencies**: Phase 5

---

### Phase 8: Timeline View (Difficulty: Medium-High)

**Tasks**:
1. Implement frame number tracking in background thread
2. Create timeline keyframe data structure
3. Render timeline view (similar to MIDI Player piano roll):
   - Horizontal axis: frame numbers
   - Vertical axis: normalized Y position (0.0-1.0) or color lanes
   - Draw keyframes as colored markers
   - Show erase keyframes with different visual style
4. Implement timeline scrolling and zoom
5. Add playhead indicator (if video playback position available)
6. Optional: Click on timeline to seek/jump to frame
7. Clean up old keyframes (remove after drawings expire)

**Risk**: Medium-High (timeline rendering complexity, frame tracking accuracy)  
**Estimated Time**: 5-6 hours

**Dependencies**: Phase 3, Phase 6

---

### Phase 9: Module Registration & Testing (Difficulty: Low)

**Tasks**:
1. Register module in `ModularSynthProcessor` factory
2. Add to PinDatabase for UI display
3. Test node creation in Preset Creator
4. Verify I/O pins display correctly (Video type)
5. Test video chain: VideoFileLoader → VideoDrawImpact → ColorTracker
6. Integration testing with Color Tracker node
7. Test timeline view with various drawing patterns
8. Test erase functionality

**Risk**: Low  
**Estimated Time**: 3-4 hours

**Dependencies**: All previous phases

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM-HIGH** (7/10)

**Note**: Risk increased due to timeline view complexity and erase functionality requirements.

### High-Risk Areas

#### 1. Thread Safety for Drawing Operations (Risk: 8/10)
**Issue**: Drawing operations originate from UI thread, but are applied in background thread. Need careful synchronization.

**Mitigation**:
- Use separate locks for `activeDrawings` and `pendingDrawOps`
- Use atomic operations where possible
- Minimize lock hold time
- Test with rapid drawing operations
- Consider lock-free queue for pending operations (if performance issues arise)

**Impact**: Thread deadlocks, data corruption, crashes

---

#### 2. Mouse-to-Frame Coordinate Conversion (Risk: 7/10)
**Issue**: Converting ImGui mouse coordinates to frame coordinates requires accurate preview size and position tracking.

**Mitigation**:
- Use `ImGui::GetItemRectMin()` and `GetItemRectMax()` for preview bounds
- Account for aspect ratio differences (preview may be scaled/stretched)
- Clamp coordinates to frame bounds
- Test with various frame sizes and preview sizes
- Add debug visualization (optional) to verify coordinate accuracy

**Impact**: Drawings appear in wrong location, poor user experience

---

#### 3. Drawing Performance with Many Strokes (Risk: 6/10)
**Issue**: Rendering many active drawings every frame may cause performance issues.

**Mitigation**:
- Limit maximum number of active strokes (e.g., 1000)
- Use efficient OpenCV drawing operations
- Consider GPU acceleration for drawing (future enhancement)
- Profile and optimize hot paths
- Add performance monitoring

**Impact**: Frame rate drops, stuttering video

---

#### 4. Frame Timing and Persistence Accuracy (Risk: 6/10)
**Issue**: Frame persistence depends on consistent frame rate. Variable frame rates may cause inconsistent persistence.

**Mitigation**:
- Use frame-based counting (not time-based) for persistence
- Document that persistence is frame-based, not time-based
- Consider time-based persistence as future enhancement
- Test with various video frame rates

**Impact**: Drawings persist for different durations than expected

---

### Medium-Risk Areas

#### 5. Saturation Implementation (Risk: 4/10)
**Issue**: Saturation adjustment must work correctly with OpenCV color spaces.

**Mitigation**:
- Reuse saturation logic from `VideoFXModule` (proven implementation)
- Test edge cases (0.0 = grayscale, 1.0 = no change)
- Verify color accuracy after saturation adjustment

**Impact**: Incorrect color rendering

---

#### 6. Color Wheel Picker Integration (Risk: 5/10)
**Issue**: ImGui color wheel picker uses RGB (0-1), but OpenCV uses BGR (0-255). Color wheel API may be less familiar.

**Mitigation**:
- Use `ImGui::ColorPicker4()` with `ImGuiColorEditFlags_PickerHueWheel` flag
- Convert RGB to BGR when storing color
- Convert BGR to RGB when displaying in picker
- Test color accuracy (verify drawn color matches picker color)
- Document color space conversions
- Provide fallback to simple color edit if wheel causes issues

**Impact**: Color mismatch between picker and drawing, or poor UX if wheel is confusing

---

#### 7. Drawing Stroke Continuity (Risk: 5/10)
**Issue**: Rapid mouse movements may create gaps in strokes, or strokes may not connect smoothly.

**Mitigation**:
- Interpolate between points if gap is too large
- Use OpenCV's anti-aliased line drawing (`LINE_AA`)
- Consider adding point density control (future enhancement)
- Test with various mouse speeds

**Impact**: Jagged or disconnected strokes

---

#### 8. Erase Functionality (Risk: 6/10)
**Issue**: Erasing requires drawing with background color or using inpainting. May not perfectly erase if background changes.

**Mitigation**:
- Use simple approach: draw with black/white color (matches desaturated background)
- Consider storing original frame pixels for perfect erase (memory intensive)
- Use thicker brush for erase operations
- Test erase accuracy with various backgrounds
- Consider inpainting algorithm for smoother erase (future enhancement)

**Impact**: Erase may not be perfect, leaving artifacts

---

#### 9. Timeline Rendering Performance (Risk: 5/10)
**Issue**: Rendering many keyframes on timeline may cause UI lag.

**Mitigation**:
- Use scroll-aware culling (only render visible keyframes)
- Limit maximum keyframes displayed
- Use efficient ImGui drawing (similar to MIDI Player)
- Consider keyframe aggregation for zoomed-out views
- Profile timeline rendering performance

**Impact**: UI lag when many drawings exist

---

### Low-Risk Areas

#### 10. Module Registration (Risk: 2/10)
**Issue**: Standard module registration process.

**Mitigation**: Follow existing patterns from other modules.

---

#### 11. State Persistence (Risk: 2/10)
**Issue**: Standard APVTS and extra state handling.

**Mitigation**: Follow existing patterns, test save/load.

---

#### 12. Parameter Modulation (Risk: 3/10)
**Issue**: Saturation parameter should support CV modulation (optional).

**Mitigation**: Follow existing modulation patterns from other modules. Can be added in Phase 5 or deferred.

---

## Confidence Rating

### Overall Confidence: **MEDIUM** (6/10)

**Note**: Confidence slightly decreased due to additional complexity of timeline view and erase functionality. However, MIDI Player provides a proven reference implementation for timeline rendering.

### Strong Points

1. **Clear Architecture**: Similar to existing `VideoFXModule` - proven pattern
2. **Existing Patterns**: Drawing interaction similar to `ColorTrackerModule` color picker
3. **Timeline Reference**: MIDI Player provides excellent reference for timeline rendering
4. **Well-Defined Requirements**: Clear use case and feature set
5. **Incremental Implementation**: Phases can be tested independently
6. **OpenCV Support**: Mature library with good drawing APIs
7. **Thread Safety Tools**: JUCE provides `CriticalSection`, atomic types
8. **State Management**: APVTS handles parameter persistence
9. **ImGui Color Wheel**: Built-in color wheel picker available

### Weak Points

1. **Complex Threading**: UI thread → Background thread communication is non-trivial
2. **Coordinate Conversion**: Mouse-to-frame conversion requires careful implementation
3. **Timeline Complexity**: Timeline rendering adds significant UI complexity
4. **Erase Implementation**: Erase functionality may not be perfect (background color matching)
5. **Performance Unknowns**: Drawing performance with many strokes is untested
6. **Frame Rate Dependency**: Persistence accuracy depends on consistent frame rates
7. **Testing Complexity**: Requires video input, drawing interaction, frame counting, and timeline testing
8. **Color Space Conversions**: Multiple conversions (RGB ↔ BGR, HSV for saturation)
9. **Edge Cases**: Many edge cases in drawing interaction (rapid clicks, mouse leave, draw/erase switching, etc.)
10. **Timeline Performance**: Many keyframes may cause UI lag

### Areas Requiring Extra Attention

1. **Thread Safety Testing**: Stress test with rapid drawing operations, many strokes
2. **Coordinate Accuracy Testing**: Verify drawings appear at correct locations
3. **Performance Testing**: Test with maximum number of strokes, various frame sizes
4. **Frame Persistence Testing**: Verify drawings disappear after correct number of frames
5. **Integration Testing**: Test full chain: VideoFileLoader → VideoDrawImpact → ColorTracker
6. **User Experience Testing**: Verify drawing feels responsive and accurate

---

## Potential Problems & Solutions

### Problem 1: Drawings Appear in Wrong Location
**Scenario**: User draws on preview, but drawing appears offset or scaled incorrectly.

**Root Causes**:
- Incorrect coordinate conversion
- Preview scaling not accounted for
- Aspect ratio mismatch

**Solutions**:
- Use `ImGui::GetItemRectMin/Max()` for accurate preview bounds
- Account for aspect ratio: maintain aspect ratio in preview, or scale coordinates proportionally
- Add debug visualization to show mouse position in frame coordinates
- Test with various frame sizes and preview sizes

---

### Problem 2: Drawings Disappear Too Quickly or Too Slowly
**Scenario**: Frame persistence doesn't match user expectation.

**Root Causes**:
- Variable frame rate (video or processing)
- Frame counting logic error
- User misunderstanding (expects time-based, not frame-based)

**Solutions**:
- Document that persistence is frame-based
- Consider adding time-based persistence option (future enhancement)
- Add visual indicator showing remaining frames (optional)
- Test with various video frame rates

---

### Problem 3: Performance Issues with Many Drawings
**Scenario**: Frame rate drops when many strokes are active.

**Root Causes**:
- Too many OpenCV drawing operations per frame
- Inefficient stroke storage/iteration
- Lock contention

**Solutions**:
- Limit maximum number of active strokes
- Optimize drawing operations (batch where possible)
- Use more efficient data structures (spatial indexing for future)
- Consider GPU acceleration for drawing
- Profile and optimize hot paths

---

### Problem 4: Thread Deadlock or Race Condition
**Scenario**: Application hangs or crashes during drawing operations.

**Root Causes**:
- Lock ordering issues
- Lock held too long
- Concurrent access to shared data

**Solutions**:
- Use consistent lock ordering (always lock `pendingOpsLock` before `drawingsLock`)
- Minimize lock hold time
- Use atomic operations where possible
- Add deadlock detection (debug builds)
- Stress test with rapid operations

---

### Problem 5: Color Mismatch Between Picker and Drawing
**Scenario**: Color drawn doesn't match color picker selection.

**Root Causes**:
- RGB ↔ BGR conversion error
- Color space mismatch
- Parameter update timing

**Solutions**:
- Verify RGB to BGR conversion (swap R and B channels)
- Test with known colors (pure red, green, blue)
- Ensure parameter updates propagate correctly
- Add color preview swatch in UI

---

### Problem 6: Drawing Doesn't Feel Responsive
**Scenario**: Delay between mouse action and drawing appearance.

**Root Causes**:
- Drawing queued but not processed quickly enough
- Background thread processing too slow
- Preview update lag

**Solutions**:
- Process pending operations every frame (not every N frames)
- Optimize background thread processing
- Consider immediate preview feedback (draw on preview before background processing)
- Reduce processing overhead

---

### Problem 7: Erase Doesn't Work Properly
**Scenario**: Right-click erase doesn't fully remove drawings or leaves artifacts.

**Root Causes**:
- Background color doesn't match actual frame background
- Erase brush size too small
- Frame changes between draw and erase

**Solutions**:
- Use thicker erase brush (2x normal brush size)
- Store original frame pixels for perfect erase (memory trade-off)
- Use inpainting algorithm for smoother erase
- Consider erase mode that removes strokes entirely (not just draws over)

---

### Problem 8: Timeline View Too Cluttered
**Scenario**: Many keyframes make timeline hard to read.

**Root Causes**:
- Too many drawings
- Keyframes overlap
- No zoom/scroll controls

**Solutions**:
- Implement timeline zoom (like MIDI Player)
- Aggregate keyframes when zoomed out
- Use different visual styles for different colors
- Add filter options (show/hide erase keyframes, filter by color)
- Limit keyframe display density

---

### Problem 9: Strokes Don't Connect Smoothly
**Scenario**: Rapid mouse movements create gaps or jagged lines.

**Root Causes**:
- Points too far apart
- No interpolation between points
- Mouse sampling rate

**Solutions**:
- Interpolate between points if gap is large
- Use OpenCV's anti-aliased line drawing
- Consider adding minimum point density
- Test with various mouse speeds

---

## Alternative Approaches Considered

### Approach 1: Immediate Drawing (No Queue)
**Description**: Draw directly on frame in UI thread, then copy to background thread.

**Pros**:
- Immediate visual feedback
- Simpler threading model

**Cons**:
- Requires frame copy every frame (performance)
- Frame may be modified while background thread is reading
- More complex synchronization

**Verdict**: Rejected - too risky for thread safety

---

### Approach 2: Time-Based Persistence
**Description**: Use time (seconds) instead of frame count for persistence.

**Pros**:
- More intuitive for users
- Consistent across different frame rates

**Cons**:
- Requires frame rate tracking
- More complex implementation
- May not match video frame timing exactly

**Verdict**: Deferred - can be added as future enhancement

---

### Approach 3: GPU-Accelerated Drawing
**Description**: Use CUDA/OpenGL for drawing operations.

**Pros**:
- Better performance with many strokes
- Consistent with existing GPU support

**Cons**:
- Much more complex implementation
- Requires CUDA/OpenGL expertise
- May not be necessary for initial version

**Verdict**: Deferred - can be added as future enhancement if performance issues arise

---

## Testing Strategy

### Unit Tests
1. **Coordinate Conversion**: Test mouse-to-frame conversion with various sizes
2. **Drawing Expiration**: Test frame counting and removal logic
3. **Color Conversion**: Test RGB ↔ BGR conversion accuracy
4. **Saturation**: Test saturation adjustment (0.0, 0.5, 1.0)

### Integration Tests
1. **Video Chain**: VideoFileLoader → VideoDrawImpact → ColorTracker
2. **Drawing Persistence**: Create drawing, verify it disappears after N frames
3. **Multiple Drawings**: Create many drawings, verify all render correctly
4. **Rapid Drawing**: Draw quickly, verify no crashes or missing strokes

### Performance Tests
1. **Many Strokes**: Test with 1000+ active strokes
2. **Large Frames**: Test with 4K video frames
3. **Rapid Operations**: Test rapid drawing operations
4. **Frame Rate**: Verify consistent 30 FPS processing

### User Experience Tests
1. **Drawing Accuracy**: Verify drawings appear at correct locations
2. **Color Accuracy**: Verify drawn color matches picker
3. **Responsiveness**: Verify drawing feels immediate
4. **Persistence**: Verify drawings persist for expected duration

---

## Future Enhancements

### Phase 9 (Optional): Advanced Features
1. **Time-Based Persistence**: Option to use seconds instead of frames
2. **Drawing Modes**: Different brush shapes (circle, square, line)
3. **Undo/Redo**: Undo last stroke, redo functionality
4. **Drawing Layers**: Multiple drawing layers with different colors
5. **Export Drawings**: Save drawings as separate layer/image
6. **GPU Acceleration**: Use CUDA for drawing operations
7. **Pressure Sensitivity**: Support for pressure-sensitive input (if available)
8. **Drawing Templates**: Pre-defined drawing patterns
9. **Animation**: Animated brush effects (pulsing, fading)
10. **Multi-Color Strokes**: Gradient strokes with multiple colors

---

## Conclusion

The Video Draw Impact node is a medium-complexity feature that builds on existing patterns (`VideoFXModule`, `ColorTrackerModule`). The main challenges are thread safety for drawing operations and accurate coordinate conversion. With careful implementation and thorough testing, this feature will enable users to create visual rhythms that can be tracked and converted to audio triggers, opening new creative possibilities in the modular synthesizer.

**Recommended Approach**: Implement in phases, testing each phase thoroughly before proceeding. Pay special attention to thread safety and coordinate conversion accuracy. Consider deferring advanced features (GPU acceleration, time-based persistence) until core functionality is proven.

**Estimated Total Time**: 35-45 hours (including testing and debugging)

**Note**: Timeline view and erase functionality add significant complexity. Consider implementing in phases:
- **Phase 1 (Core)**: Basic drawing, color wheel, saturation (Phases 1-6) - ~20-25 hours
- **Phase 2 (Enhanced)**: Timeline view, erase functionality (Phases 7-8) - ~15-20 hours

**Recommended Team Size**: 1 developer (can be done solo, but code review recommended for thread safety)

---

