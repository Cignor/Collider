# OSC User-Friendly Features Plan

## Current Issues
- **Excessive Logging**: 80,000+ lines in 2 minutes from motion sensor data
- **No Visual Feedback**: Users can't see when OSC is active or what addresses are being received
- **Manual Pattern Entry**: Users must manually type OSC address patterns
- **No Discovery**: Users don't know what OSC addresses are available

## Implemented Features

### 1. Reduced Logging Verbosity ✅
- Removed per-message logging in `OscDeviceManager::handleOscMessage`
- Removed verbose buffer size logging
- Removed per-message logging in `ModularSynthProcessor::processOscWithSourceInfo`
- Removed per-message logging in `OSCCVModuleProcessor::handleOscSignal`
- Only log warnings (buffer overflow) and summaries

### 2. Address Monitoring System ✅
- Added `lastSeenAddresses` map to track recently received OSC addresses
- Addresses expire after 2 seconds of inactivity
- Thread-safe access with `addressMonitorLock`
- `getRecentAddresses()` method to retrieve active addresses for UI

## Planned User-Friendly Features

### Priority 1: Visual Feedback & Discovery

#### A. OSC Activity Indicator in Status Bar
- **Location**: Top status bar (next to MIDI activity)
- **Visual Design**:
  - Green pulsing dot when receiving messages
  - Gray dot when device enabled but idle
  - Show device name and port
  - Tooltip: "OSC: [DeviceName] - Last: /address" or "Waiting for messages..."
- **Implementation**: Already partially done, needs enhancement

#### B. OSC CV Module - Address Monitor Tab
- **New Tab/Section**: "Monitor" or "Received Addresses"
- **Features**:
  - Real-time list of all OSC addresses being received
  - Show address, data type, and last value
  - Color coding: Green = active (received in last 500ms), Gray = stale
  - Auto-scroll to newest addresses
  - Click address to copy to pattern field
  - Double-click to set as active pattern
- **Layout**:
  ```
  [Monitor Tab]
  ┌─────────────────────────────┐
  │ /data/motion/accelerometer/x │ ← Green (active)
  │ /data/motion/accelerometer/y │ ← Green (active)
  │ /data/motion/gyroscope/z    │ ← Gray (stale)
  └─────────────────────────────┘
  ```

#### C. Click-to-Map Interface
- **Primary Feature**: Click any address in monitor to set it as pattern
- **Visual Feedback**:
  - Highlight clicked address
  - Show "✓ Mapped" indicator
  - Pattern field updates immediately
- **Secondary Actions**:
  - Right-click: Copy address to clipboard
  - Double-click: Set pattern + show preview
  - Middle-click: Open address details

### Priority 2: Pattern Management

#### D. Pattern Presets
- **Quick Patterns**: Common patterns like `/cv/*`, `/synth/*`, `/data/motion/*`
- **Saved Patterns**: User can save custom patterns with names
- **Pattern Library**: Pre-defined patterns for common OSC apps:
  - TouchOSC: `/1/fader*`, `/1/rotary*`
  - Lemur: `/lemur/*`
  - Motion sensors: `/data/motion/*`
  - Custom: User-defined

#### E. Pattern Builder
- **Visual Pattern Editor**:
  - Wildcard buttons: `*`, `?`, `[...]`
  - Address segments as draggable blocks
  - Preview: Shows which addresses match
  - Test mode: Highlight matching addresses in monitor

#### F. Multi-Pattern Support
- **Multiple Patterns**: Allow multiple address patterns per module
- **Pattern Groups**: Group patterns (e.g., "Motion", "Control", "Audio")
- **Pattern Priority**: Order matters for matching
- **Visual**: Show all active patterns in a list with enable/disable toggles

### Priority 3: Data Visualization

#### G. Value Display
- **Current Values**: Show last received value for each address
- **Data Types**: Icon/color for int32, float32, string, blob
- **Value History**: Mini graph showing value over time (last 2 seconds)
- **Min/Max**: Show value range for numeric types

#### H. Address Statistics
- **Message Count**: How many messages received per address
- **Rate**: Messages per second
- **Last Seen**: Timestamp of last message
- **Data Range**: Min/max values for numeric addresses

#### I. Value Preview
- **Live Preview**: Show current value next to address in monitor
- **Format**: 
  - Float: `0.523` or `52.3%`
  - Int: `42`
  - String: `"hello"`
  - Blob: `[12 bytes]`

### Priority 4: Mapping & Routing

#### J. Smart Mapping
- **Auto-Detect**: Suggest CV outputs based on address name:
  - `/cv/pitch` → Pitch CV output
  - `/cv/velocity` → Velocity output
  - `/gate` or `/trigger` → Gate output
  - `/data/motion/*` → Suggest motion-to-CV mapping
- **Mapping Presets**: One-click mapping for common patterns

#### K. Multi-Output Mapping
- **One-to-Many**: Map one OSC address to multiple CV outputs
- **Many-to-One**: Map multiple OSC addresses to one CV output (with blending)
- **Visual Mapping Editor**: Drag-and-drop interface

#### L. Value Transformation
- **Scaling**: Map OSC value range to CV range
- **Offset**: Add/subtract from OSC value
- **Invert**: Invert the value
- **Smoothing**: Apply smoothing filter to values
- **Quantization**: Quantize to specific steps

### Priority 5: Advanced Features

#### M. Address Filtering
- **Source Filter**: Already implemented, needs UI enhancement
- **Type Filter**: Show only float32, only int32, etc.
- **Value Filter**: Show only addresses with values in specific range
- **Regex Filter**: Advanced pattern matching

#### N. Address Groups
- **Folders**: Organize addresses into groups
- **Tags**: Tag addresses (e.g., "motion", "control", "audio")
- **Search**: Filter monitor by tag or group

#### O. Export/Import
- **Export Mapping**: Save OSC-to-CV mappings to file
- **Import Mapping**: Load saved mappings
- **Share Presets**: Share OSC configurations with others

#### P. OSC Sender (Future)
- **Output OSC**: Send OSC messages from CV inputs
- **CV-to-OSC**: Convert CV signals to OSC messages
- **Bidirectional**: Full OSC communication

### Priority 6: User Experience

#### Q. Help & Documentation
- **Tooltips**: Explain every UI element
- **Help Button**: Link to OSC documentation
- **Examples**: Built-in example configurations
- **Tutorial Mode**: Step-by-step guide for first-time users

#### R. Visual Polish
- **Icons**: Icons for different address types
- **Colors**: Color-coded by data type or activity
- **Animations**: Smooth transitions and highlights
- **Themes**: Support for dark/light themes

#### S. Performance
- **Throttling**: Limit update rate for high-frequency addresses
- **Grouping**: Group similar addresses to reduce UI updates
- **Lazy Loading**: Only render visible addresses in monitor
- **Virtual Scrolling**: Handle thousands of addresses efficiently

#### T. Accessibility
- **Keyboard Navigation**: Full keyboard control
- **Screen Reader**: Support for screen readers
- **High Contrast**: High contrast mode
- **Font Scaling**: Adjustable font sizes

## Implementation Order

### Phase 1: Core UX (Current)
1. ✅ Reduce logging verbosity
2. ✅ Add address monitoring system
3. 🔄 Add address monitor UI in OSC CV module
4. 🔄 Implement click-to-map functionality

### Phase 2: Discovery & Mapping
5. Add value display in monitor
6. Add pattern presets
7. Add smart mapping suggestions
8. Add value transformation controls

### Phase 3: Advanced Features
9. Multi-pattern support
10. Pattern builder
11. Address statistics
12. Export/import mappings

### Phase 4: Polish
13. Visual enhancements
14. Performance optimization
15. Documentation and help
16. Accessibility features

## Technical Notes

### Thread Safety
- Address monitoring uses separate lock (`addressMonitorLock`) to avoid blocking audio thread
- UI updates should be throttled (max 30 FPS)
- Use atomic operations for simple flags

### Performance Considerations
- Limit address history to last 2 seconds
- Use efficient data structures (std::map for O(log n) lookup)
- Virtual scrolling for large address lists
- Debounce UI updates

### UI Framework
- Use ImGui for all UI elements
- Follow existing node editor patterns
- Use theme colors for consistency
- Implement tooltips for all interactive elements


