# Timeline Scroll-to-Zoom Implementation Guide

## Overview

This guide explains how to implement scroll-to-zoom functionality for timeline views in Collider's node editor. The feature allows users to zoom in/out on a timeline by scrolling the mouse wheel, with zooming centered on the playhead position. This provides an intuitive way to navigate long timelines without manually adjusting zoom sliders.

## Reference Implementation

The scroll-to-zoom feature was first implemented in `MIDIPlayerModuleProcessor` and serves as the reference implementation. This guide explains how to adapt it to other modules with timeline views, specifically `VideoDrawImpactModuleProcessor`.

## What It Does

### Scroll-to-Zoom Behavior

When a user hovers over a timeline child window and scrolls the mouse wheel:
1. **Zoom in/out**: Scrolling up increases zoom (more detail), scrolling down decreases zoom (wider view)
2. **Playhead-centered**: The zoom operation maintains the playhead's visual position on screen
3. **Scroll adjustment**: The timeline's scroll position is automatically adjusted to keep the playhead in the same visual location
4. **Clamping**: Zoom values are clamped to valid ranges (e.g., 20-400px/beat for MIDI, 10-500px/s for video)

### Scroll-Edit for Zoom Slider

Additionally, the zoom slider itself supports scroll-edit:
- Hovering the zoom slider and scrolling adjusts the zoom value directly
- This provides an alternative to dragging the slider handle
- Works independently of timeline scroll-to-zoom

## Implementation Pattern

### 1. Zoom Slider with Scroll-Edit

The zoom slider should support scroll-edit for direct value adjustment:

```cpp
// === TIMELINE ZOOM SECTION ===
ImGui::Text("Timeline Zoom:");
ImGui::SameLine();
ImGui::PushItemWidth(120);
if (ImGui::SliderFloat("##zoom", &zoomX, 20.0f, 400.0f, "%.0fpx/beat"))
{
    // Zoom changed via slider drag
}
// Scroll-edit for zoom slider (manual handling since zoomX is not a JUCE parameter)
if (ImGui::IsItemHovered())
{
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f)
    {
        const float zoomStep = 5.0f; // 5 pixels per beat per scroll step
        const float newZoom = juce::jlimit(20.0f, 400.0f, zoomX + (wheel > 0.0f ? zoomStep : -zoomStep));
        if (newZoom != zoomX)
        {
            zoomX = newZoom;
        }
    }
}
ImGui::PopItemWidth();
```

**Key Points:**
- `zoomX` is a UI state variable (not a JUCE parameter), so manual wheel handling is required
- Step size should be appropriate for the zoom range (5px for 20-400 range, adjust proportionally)
- Clamping ensures zoom stays within valid bounds

### 2. Timeline Scroll-to-Zoom (Inside BeginChild)

The scroll-to-zoom logic must be placed **inside** the `BeginChild` context, right after getting the scroll position:

```cpp
ImGui::BeginChild("TimelineView", graphSize, false, childFlags))
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    float scrollX = ImGui::GetScrollX();
    
    // --- SCROLL-TO-ZOOM ON TIMELINE (centered on playhead) ---
    // Handle scroll wheel for zooming (must be inside BeginChild context)
    if (ImGui::IsWindowHovered())
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f && !ImGui::IsAnyItemActive()) // Don't zoom while dragging sliders
        {
            // Calculate playhead position in content space (before zoom change)
            const double visualTempo = fileBpm; // Or timeline duration for video
            const float oldPixelsPerBeat = zoomX; // Or zoomPixelsPerSecond for video
            const float playheadX_content = (float)(currentPlaybackTime / (60.0 / visualTempo)) * oldPixelsPerBeat;
            
            // Get current scroll position
            const float oldScrollX = scrollX;
            
            // Calculate playhead position relative to visible window
            const float playheadX_visible = playheadX_content - oldScrollX;
            
            // Apply zoom (zoom in when scrolling up, zoom out when scrolling down)
            const float zoomStep = 10.0f; // Adjust based on zoom range
            const float newZoom = juce::jlimit(20.0f, 400.0f, zoomX + (wheel > 0.0f ? zoomStep : -zoomStep));
            
            if (newZoom != zoomX)
            {
                // Calculate new playhead position in content space (after zoom change)
                const float newPixelsPerBeat = newZoom;
                const float newPlayheadX_content = (float)(currentPlaybackTime / (60.0 / visualTempo)) * newPixelsPerBeat;
                
                // Adjust scroll to keep playhead at the same visible position
                const float newScrollX = newPlayheadX_content - playheadX_visible;
                
                // Update zoom
                zoomX = newZoom;
                
                // Set new scroll position (clamped to valid range)
                const int numBars = (int)std::ceil(totalDuration / (60.0 / visualTempo * 4.0));
                const float totalWidth = numBars * 4.0f * newZoom;
                const float maxScroll = std::max(0.0f, totalWidth - nodeWidth);
                const float clampedScroll = juce::jlimit(0.0f, maxScroll, newScrollX);
                
                ImGui::SetScrollX(clampedScroll);
                scrollX = clampedScroll; // Update local scrollX for drawing calculations
            }
        }
    }
    
    // ... rest of timeline drawing code ...
}
ImGui::EndChild();
```

**Critical Implementation Details:**

1. **Context**: Must be inside `BeginChild` to access `ImGui::GetScrollX()` and `ImGui::SetScrollX()`
2. **Hover Check**: Use `ImGui::IsWindowHovered()` to detect when mouse is over the timeline
3. **Active Item Guard**: `!ImGui::IsAnyItemActive()` prevents zooming while dragging sliders or other controls
4. **Playhead Calculation**: Calculate playhead position in content space using current zoom and playback time
5. **Scroll Preservation**: Calculate new scroll position to maintain playhead's visual position
6. **Clamping**: Clamp scroll position to valid range (0 to maxScroll)

## Adapting to VideoDrawImpactModuleProcessor

### Current State

`VideoDrawImpactModuleProcessor` already has:
- A zoom slider (`zoomPixelsPerSecond`) at line 977
- A timeline child window with horizontal scrolling (line 1003)
- Playhead drawing (line 1256)
- Scroll-aware keyframe rendering

### Required Changes

#### 1. Add Scroll-Edit to Zoom Slider

Replace the zoom slider section (lines 973-981) with:

```cpp
// === TIMELINE ZOOM SECTION ===
ImGui::Text("Timeline Zoom:");
ImGui::SameLine();
ImGui::PushItemWidth(120);
if (ImGui::SliderFloat("##zoom", &zoomPixelsPerSecond, 10.0f, 500.0f, "%.0f px/s"))
{
    // Zoom changed - no action needed, just update the variable
}
// Scroll-edit for zoom slider (manual handling since zoomPixelsPerSecond is not a JUCE parameter)
if (ImGui::IsItemHovered())
{
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f)
    {
        const float zoomStep = 10.0f; // 10 pixels per second per scroll step
        const float newZoom = juce::jlimit(10.0f, 500.0f, zoomPixelsPerSecond + (wheel > 0.0f ? zoomStep : -zoomStep));
        if (newZoom != zoomPixelsPerSecond)
        {
            zoomPixelsPerSecond = newZoom;
        }
    }
}
ImGui::PopItemWidth();
```

#### 2. Add Scroll-to-Zoom in Timeline Child Window

Add the scroll-to-zoom logic right after line 1006 (after `const float scrollX = ImGui::GetScrollX();`):

```cpp
ImDrawList* drawList = ImGui::GetWindowDrawList();
const float scrollX = ImGui::GetScrollX();

// --- SCROLL-TO-ZOOM ON TIMELINE (centered on playhead) ---
// Handle scroll wheel for zooming (must be inside BeginChild context)
if (ImGui::IsWindowHovered())
{
    const float wheel = ImGui::GetIO().MouseWheel;
    if (wheel != 0.0f && !ImGui::IsAnyItemActive()) // Don't zoom while dragging sliders
    {
        // Calculate playhead position in content space (before zoom change)
        double playheadTime = hasTimeline ? timelineStateUi.positionSeconds : (currentFrame / 30.0);
        const float oldPixelsPerSecond = zoomPixelsPerSecond;
        const float playheadX_content = (float)(playheadTime * oldPixelsPerSecond);
        
        // Get current scroll position
        const float oldScrollX = scrollX;
        
        // Calculate playhead position relative to visible window
        const float playheadX_visible = playheadX_content - oldScrollX;
        
        // Apply zoom (zoom in when scrolling up, zoom out when scrolling down)
        const float zoomStep = 10.0f; // 10 pixels per second per scroll step
        const float newZoom = juce::jlimit(10.0f, 500.0f, zoomPixelsPerSecond + (wheel > 0.0f ? zoomStep : -zoomStep));
        
        if (newZoom != zoomPixelsPerSecond)
        {
            // Calculate new playhead position in content space (after zoom change)
            const float newPixelsPerSecond = newZoom;
            const float newPlayheadX_content = (float)(playheadTime * newPixelsPerSecond);
            
            // Adjust scroll to keep playhead at the same visible position
            const float newScrollX = newPlayheadX_content - playheadX_visible;
            
            // Update zoom
            zoomPixelsPerSecond = newZoom;
            
            // Set new scroll position (clamped to valid range)
            const float totalWidth = (float)(totalDuration * newZoom);
            const float maxScroll = std::max(0.0f, totalWidth - graphSize.x);
            const float clampedScroll = juce::jlimit(0.0f, maxScroll, newScrollX);
            
            ImGui::SetScrollX(clampedScroll);
            // Note: scrollX is const, so we can't update it here, but SetScrollX() handles the actual scroll
        }
    }
}
```

**Key Differences from MIDI Player:**
- Uses `zoomPixelsPerSecond` instead of `zoomX` (pixels per beat)
- Uses `playheadTime` (seconds) instead of `currentPlaybackTime` (beats)
- Uses `totalDuration` (seconds) instead of `numBars * 4.0` (beats)
- Uses `graphSize.x` instead of `nodeWidth` for max scroll calculation

## Testing Checklist

After implementing scroll-to-zoom:

1. **Zoom Slider Scroll-Edit**:
   - [ ] Hover over zoom slider and scroll → value changes
   - [ ] Values clamp correctly at min/max bounds
   - [ ] Step size feels appropriate (not too fast/slow)

2. **Timeline Scroll-to-Zoom**:
   - [ ] Hover over timeline and scroll → zoom changes
   - [ ] Playhead stays in same visual position during zoom
   - [ ] Scroll position adjusts correctly to maintain playhead position
   - [ ] Zoom doesn't trigger while dragging sliders or keyframes
   - [ ] Zoom clamps correctly at min/max bounds
   - [ ] Works in both timeline-synced and frame-based modes

3. **Edge Cases**:
   - [ ] Zoom at start of timeline (scroll = 0)
   - [ ] Zoom at end of timeline (scroll = max)
   - [ ] Rapid scrolling doesn't cause jitter
   - [ ] Zoom while playhead is off-screen (should still work)

## Common Pitfalls

1. **Wrong Context**: Placing scroll-to-zoom logic outside `BeginChild` → `GetScrollX()`/`SetScrollX()` won't work
2. **Missing Active Check**: Not checking `!ImGui::IsAnyItemActive()` → zoom triggers while dragging controls
3. **Wrong Scroll Calculation**: Not accounting for playhead position → playhead jumps during zoom
4. **No Clamping**: Forgetting to clamp scroll position → negative or excessive scroll values
5. **Const Variable**: Trying to modify `const float scrollX` → use `ImGui::SetScrollX()` instead

## Summary

Scroll-to-zoom provides an intuitive way to navigate timelines by:
- Allowing direct zoom adjustment via mouse wheel on the timeline
- Maintaining visual context by keeping the playhead centered
- Providing scroll-edit for the zoom slider as an alternative input method

The implementation requires careful calculation of playhead positions and scroll adjustments, but follows a consistent pattern that can be adapted to any timeline view in Collider's node editor.

