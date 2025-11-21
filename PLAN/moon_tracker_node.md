# Celestial Horizon Tracker - Detailed Implementation Plan

**Date**: 2025-01-XX  
**Feature**: Celestial Horizon Tracker Node with Astronomy Engine Integration  
**Status**: Planning Phase

---

## Executive Summary

Implement a Celestial Horizon Tracker Node that simulates the apparent motion of celestial objects from Earth's perspective, with the horizon line acting as a trigger boundary. The module tracks the Sun, Moon, planets, and stars, generating CV signals and trigger pulses when objects cross the horizon. Stars rotating around Earth create polyrhythmic patterns, while planetary cycles provide slow modulation. Integration with Astronomy Engine library provides accurate astronomical calculations.

---

## Vision: Celestial Horizon Tracker

### The Vision (User's Direct Description)

**The Horizon as Trigger**
- The horizon line is the trigger boundary
- When any celestial object intersects the horizon, it generates a trigger/beat

**Speed Control**
- A speed slider that can alter the speed of the celestial cycle
- Controls how fast time progresses (0.0 = stopped, 1.0 = real-time, >1.0 = accelerated)

**Stars Rotating (POV from Earth)**
- Stars appear to rotate around Earth (simulating Earth's rotation)
- Stars follow circular paths in the sky
- Each star can intersect the horizon, creating a beat

**Tracked Objects**
- Sun
- Moon
- All planets in the solar system (Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune)
- Stars (rotating around Earth)

**CV Outputs per Object**
- Each planet, moon, and sun has their own CV outputs
- Individual control voltage signals for each celestial object

**Star Filtering (Depth Map)**
- Filter stars from 0.0 to 1.0
- 0.0 = closest/brightest stars only
- 1.0 = all stars (including faint/distant)
- Creates a depth map effect for star selection

**Star Hits = Beats**
- When a star intersects the horizon, it generates a beat/trigger
- Multiple stars can create polyrhythmic patterns
- Star filtering determines which stars can trigger

---

### Conceptualization & Expansion

#### What This Creates

This vision creates a **rhythmic system driven by celestial mechanics**:

1. **Polyrhythmic Star Beats**: As stars rotate around Earth, they cross the horizon at different times, creating complex polyrhythmic patterns. With speed control, you can accelerate this to create musical tempos.

2. **Layered Depth**: The star depth filter (0.0-1.0) acts like a "depth map" - you can choose to only trigger on bright nearby stars (0.0) or include all stars including faint distant ones (1.0). This creates layered rhythmic textures.

3. **Planetary Modulation**: Each planet has its own CV outputs, providing slow, evolving modulation signals. The speed slider can accelerate these cycles for faster changes.

4. **Horizon as Musical Boundary**: The horizon becomes a musical event boundary - every crossing is a beat. Sun rise/set, moon rise/set, planet crossings, and star crossings all contribute to the rhythm.

5. **Time-Lapse Music**: The speed slider enables time-lapse effects - you can compress days, months, or years of celestial motion into seconds or minutes of music.

#### Technical Implications

**Coordinate System**:
- All calculations from Earth's surface (observer POV)
- Altitude: -90° (below horizon) to +90° (zenith), 0° = horizon
- Azimuth: 0-360° (full rotation around horizon)
- Stars rotate due to Earth's rotation (sidereal day: 23h 56m 4s)

**Star System**:
- Stars have fixed positions in celestial sphere
- Earth's rotation makes them appear to rotate
- Each star follows a circular path around celestial poles
- Speed slider affects rotation rate (accelerate/decelerate time)

**Horizon Intersection**:
- Detect when altitude crosses 0° (horizon line)
- Rising: altitude goes from negative → positive
- Setting: altitude goes from positive → negative
- Generate trigger pulse on crossing

**Performance Considerations**:
- Many stars = many calculations
- Need efficient horizon proximity detection
- LOD (Level of Detail): Only calculate stars near horizon
- Pre-calculate star positions (they're fixed in celestial sphere)

#### Creative Possibilities

1. **Star Density Rhythms**: Filter to only bright stars (0.0) = sparse, clean beats. Filter to all stars (1.0) = dense, complex polyrhythms.

2. **Speed-Based Composition**: 
   - Speed = 1.0: Real-time celestial motion (slow, ambient)
   - Speed = 10.0: Compressed day cycle (minutes instead of hours)
   - Speed = 100.0: Compressed year cycle (days instead of months)
   - Speed = 1000.0: Extreme time-lapse (seconds instead of days)

3. **Planetary Layers**: Each planet's CV can control different parameters:
   - Venus altitude → filter cutoff
   - Mars distance → reverb size
   - Jupiter velocity → LFO rate
   - Saturn phase → sequencer pattern

4. **Horizon Event Sequencing**: 
   - Sun rise → trigger sample A
   - Moon set → trigger sample B
   - Star crossings → trigger percussion
   - Planet crossings → trigger modulation changes

5. **Depth-Mapped Star Beats**:
   - Bright stars (0.0-0.3) = kick/snare (strong beats)
   - Medium stars (0.3-0.7) = hi-hats (medium beats)
   - Faint stars (0.7-1.0) = subtle triggers (texture)

#### Core Concept

A celestial tracker module that simulates the apparent motion of celestial objects from Earth's perspective, with the horizon line acting as a trigger boundary. Objects crossing the horizon generate triggers, creating rhythmic patterns based on celestial mechanics.

#### Key Features (Expanded from Vision)

#### 1. **Horizon as Trigger Boundary**
- The horizon line (0° altitude) acts as the primary trigger point
- When any celestial object crosses the horizon (rises or sets), generate a trigger pulse
- Horizon crossing detection: altitude transitions from negative → positive (rise) or positive → negative (set)

#### 2. **Speed Control**
- **Speed Slider**: Controls the rate of celestial cycle progression
  - Range: 0.0 (stopped) to 1.0+ (accelerated)
  - Affects all celestial objects uniformly
  - Can speed up or slow down the apparent motion
  - Enables time-lapse effects and rhythmic patterns

#### 3. **Star System**
- **Stars Rotating**: Stars appear to rotate around Earth (POV from Earth)
  - Simulates Earth's rotation (sidereal day: ~23h 56m)
  - Stars follow circular paths around the celestial poles
  - Each star has a fixed position in the sky that rotates with Earth
  
- **Star Filtering (Depth Map)**
  - Filter stars by depth/distance: 0.0 to 1.0 range
  - 0.0 = closest/brightest stars only
  - 1.0 = all stars (including faint/distant)
  - Allows selective triggering based on star magnitude/brightness
  - Can create layered rhythmic patterns (bright stars = strong beats, faint stars = subtle triggers)

- **Star Hits = Beats**
  - When a star intersects the horizon, generate a trigger/beat
  - Each star crossing creates a discrete event
  - Multiple stars can trigger simultaneously (polyrhythmic patterns)
  - Star magnitude could affect trigger intensity/velocity

#### 4. **Planetary System Tracking**
- **Track Multiple Objects**:
  - Sun (solar day cycle)
  - Moon (lunar cycle + daily motion)
  - Planets: Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune
  - Each object has independent orbital mechanics

- **CV Outputs per Object**
  - Each celestial object (sun, moon, each planet) has dedicated CV outputs:
    - **Altitude CV**: Position above/below horizon (normalized 0-1, where 0.5 = horizon)
    - **Azimuth CV**: Horizontal position (0-1, full rotation)
    - **Phase CV**: For moon (0-1, new to full)
    - **Illumination CV**: Brightness/visibility (0-1)
    - **Velocity CV**: Rate of motion (orbital/rotational speed)
    - **Distance CV**: Distance from Earth (normalized)

#### 5. **Horizon Intersection Triggers**
- **Rise Trigger**: Object crosses from below to above horizon
- **Set Trigger**: Object crosses from above to below horizon
- **Trigger Outputs**:
  - Individual triggers per object (Sun Rise, Moon Set, etc.)
  - Combined trigger bus (any object crossing)
  - Star trigger bus (any star crossing, filtered by depth)

### Use Cases

1. **Rhythmic Patterns**: Stars crossing horizon create complex polyrhythms
2. **Slow Modulation**: Planetary cycles provide long-term CV modulation
3. **Event Triggers**: Horizon crossings trigger samples, envelopes, sequences
4. **Time-lapse Effects**: Speed slider creates accelerated celestial motion
5. **Layered Beats**: Depth-filtered stars create layered rhythmic textures
6. **Astronomical Music**: Music driven by actual celestial mechanics

### Design Questions

1. **Star Density**: How many stars to track? (100? 1000? All visible?)
2. **Trigger Behavior**: Should star triggers have velocity based on magnitude?
3. **Speed Range**: What's the maximum speed multiplier? (1x, 10x, 100x, 1000x?)
4. **Coordinate Input**: Allow user to set observer location (lat/long)?
5. **Time Control**: Real-time only, or allow time-of-day setting?
6. **Star Selection**: Manual star selection, or automatic filtering only?

---

## Architecture Design

### 1. Astronomy Engine Integration

**Library**: Astronomy Engine (https://github.com/cosinekitty/astronomy)
- **License**: MIT (compatible)
- **Language**: C/C++ (header-only or library)
- **Features**: Moon, stars, planets, coordinate transformations

**Integration Method**:
```cmake
# Option A: FetchContent (recommended)
include(FetchContent)
FetchContent_Declare(
    astronomy
    GIT_REPOSITORY https://github.com/cosinekitty/astronomy.git
    GIT_TAG        latest  # or specific version
)
FetchContent_MakeAvailable(astronomy)

# Option B: Git submodule (if we want to pin version)
# Add as submodule in vendor/astronomy
```

**Header Structure**:
- Single header: `astronomy.h` (header-only mode)
- Or library: `libastronomy.a` / `astronomy.lib`

**Dependencies**:
- C++11 or later (we use C++17, compatible)
- Standard math library (already available)
- No external runtime dependencies

### 2. Module Structure

**Class**: `CelestialHorizonTrackerModuleProcessor : public ModuleProcessor`

**Module Name**: `"celestial horizon tracker"` (lowercase, following naming convention)

**Output Channels** (Expanded for multiple objects):

**Per-Object CV Outputs** (Sun, Moon, each Planet):
- **Altitude CV** (0.0-1.0): Position above/below horizon (0.5 = horizon, 0.0 = nadir, 1.0 = zenith)
- **Azimuth CV** (0.0-1.0): Horizontal position (0-360° → 0-1)
- **Phase CV** (0.0-1.0): Moon only (0=new moon, 0.5=full moon)
- **Illumination CV** (0.0-1.0): Brightness/visibility
- **Velocity CV** (0.0-1.0): Rate of motion (orbital/rotational speed)
- **Distance CV** (0.0-1.0): Distance from Earth (normalized)

**Trigger Outputs**:
- **Per-Object Rise/Set Triggers**: Individual triggers for each object (Sun Rise, Moon Set, etc.)
- **Combined Trigger Bus**: Any object crossing horizon
- **Star Trigger Bus**: Any star crossing horizon (filtered by depth)
- **Star Individual Triggers**: Optional per-star triggers (if tracking specific stars)

**Star System Outputs**:
- **Average Star Distance CV** (0.0-1.0): Average distance of visible stars (depth map)
- **Star Count CV** (0.0-1.0): Number of visible stars (normalized)
- **Nearest Star Distance CV** (0.0-1.0): Distance to closest visible star

**Input Channels** (4 channels for modulation):
1. **Time Speed Mod**: CV to speed up/slow down time (0.0=stop, 1.0=normal, 2.0=2x)
2. **Phase Offset Mod**: CV to shift phase (0.0-1.0 maps to 0-360°)
3. **Location Lat Mod**: CV to offset latitude (±90°)
4. **Location Lon Mod**: CV to offset longitude (±180°)

**Parameters**:
- **Location**:
  - Latitude (-90 to 90 degrees)
  - Longitude (-180 to 180 degrees)
  
- **Time Control**:
  - Time Speed (0.0-100.0, default 1.0): Speed multiplier for celestial motion
  - Use System Time (bool, default true)
  - Manual Time Offset (seconds, for testing)
  - Time of Day (optional, for manual setting)
  
- **Star System**:
  - Star Depth Filter (0.0-1.0): Filter stars by distance/brightness (0.0=closest/brightest, 1.0=all)
  - Max Stars to Track (1-10000, default 1000): Limit number of stars for performance
  - Star Magnitude Limit (0.0-10.0, default 6.0): Only track stars brighter than this
  
- **Horizon Triggers**:
  - Horizon Threshold (degrees, default 0.0): Altitude threshold for crossing detection
  - Trigger Pulse Width (ms, default 10.0)
  - Star Trigger Velocity (bool): Use star magnitude for trigger velocity/intensity
  
- **Object Selection**:
  - Enable Sun (bool, default true)
  - Enable Moon (bool, default true)
  - Enable Planets (bool array, one per planet)
  
- **Performance**:
  - Update Rate (samples, default 256): How often to recalculate positions
  - Star LOD (Level of Detail): Only calculate stars near horizon

### 3. Calculation Strategy

**Update Frequency**:
- Celestial object calculations: Every 256-1024 samples (objects move slowly)
- Star calculations: Every 512-2048 samples (many stars, optimize)
- Horizon crossing detection: Check every block for all objects
- Star horizon detection: Only check stars near horizon (LOD optimization)
- Trigger generation: Per-sample edge detection
- All CV outputs: Smooth with `juce::SmoothedValue<float>`

**Time Handling**:
- Use `juce::Time::getCurrentTime()` for real-time mode
- Support manual time offset for testing/presets
- Convert to Julian Day Number for Astronomy Engine
- Cache last calculation to avoid per-sample recomputation

**Coordinate System**:
- Input: Latitude/Longitude (degrees)
- Astronomy Engine: Uses radians internally
- Output: Normalized CV (0.0-1.0) for all outputs

### 4. Star Rotation System

**Concept**: Stars appear to rotate around Earth due to Earth's rotation

**Implementation**:
- Earth rotates once per sidereal day (~23h 56m 4s)
- Stars have fixed positions in celestial sphere
- Calculate star positions in alt-az coordinates over time
- Stars follow circular paths around celestial poles
- Speed slider affects rotation rate (accelerate/decelerate)

**Star Catalog**:
- Use Astronomy Engine star catalog
- Filter by magnitude for depth map
- Limit to visible stars (magnitude < 6.0) or allow all with filtering
- Pre-calculate star positions for efficiency
- Use spatial indexing for horizon proximity queries

**Horizon Crossing Detection**:
- Track altitude of each star over time
- Detect when altitude crosses 0° (horizon)
- Generate trigger on crossing
- Multiple stars can cross simultaneously (polyrhythms)
- Filter by depth map before checking crossings

---

## Implementation Phases

### Phase 1: Astronomy Engine Integration (Difficulty: Easy-Medium)

**Tasks**:
1. Add Astronomy Engine to CMakeLists.txt:
   ```cmake
   include(FetchContent)
   FetchContent_Declare(astronomy ...)
   FetchContent_MakeAvailable(astronomy)
   target_link_libraries(ColliderAudioEngine PRIVATE astronomy)
   ```
2. Test compilation with simple moon phase calculation
3. Verify license compatibility (MIT is fine)
4. Test on Windows/macOS/Linux

**Risks**: 
- Build system integration issues
- Platform-specific compilation problems
- License compatibility (MIT is safe)

**Mitigation**:
- Test in isolated branch first
- Use FetchContent for easy updates
- Verify license terms match project requirements
- Test on all target platforms

**Estimated Time**: 2-3 hours

---

### Phase 2: Basic Module Structure (Difficulty: Easy)

**Tasks**:
1. Create `CelestialHorizonTrackerModuleProcessor.h` and `.cpp`
2. Inherit from `ModuleProcessor`
3. Define parameter IDs (following BestPracticeNodeProcessor pattern)
4. Set up bus configuration (expanded for multiple objects + stars)
5. Implement `createParameterLayout()`
6. Initialize parameter pointers in constructor
7. Implement `getName()` returning `"celestial horizon tracker"` (lowercase)

**Risks**: 
- Parameter naming conflicts
- Bus configuration errors
- Naming convention violations

**Mitigation**:
- Follow BestPracticeNodeProcessor pattern exactly
- Use unique parameter IDs with module prefix
- Test bus configuration matches existing modules
- Verify lowercase naming convention

**Estimated Time**: 3-4 hours

---

### Phase 3: Celestial Object Calculations (Difficulty: Medium)

**Tasks**:
1. Implement time conversion (JUCE Time → Julian Day, with speed multiplier)
2. Implement calculations for all celestial objects:
   - **Sun**: Solar position, altitude/azimuth
   - **Moon**: Phase, illumination, altitude/azimuth
   - **Planets**: Mercury, Venus, Mars, Jupiter, Saturn, Uranus, Neptune
   - Each object: altitude, azimuth, distance, velocity
3. Calculate positions using Astronomy Engine:
   ```cpp
   astro_time_t time = ...; // Convert from JUCE Time (with speed multiplier)
   astro_observer_t observer = Astronomy_MakeObserver(lat, lon, 0.0);
   
   // For each object (Sun, Moon, Planets):
   astro_body_equatorial_t body = Astronomy_Equator(BODY_XXX, &time, observer, EQUATOR_OF_DATE, ABERRATION);
   astro_horizon_t horizon = Astronomy_Horizon(&time, observer, body.ra, body.dec, REFRACTION_NORMAL);
   ```
4. Apply speed multiplier to time calculations
5. Update calculations every 256-1024 samples (not per-sample)
6. Cache results for per-sample output

**Risks**: 
- Astronomy Engine API learning curve
- Coordinate system conversions
- Performance (calculations may be expensive)
- Accuracy validation

**Mitigation**:
- Study Astronomy Engine documentation/examples
- Test calculations against known moon phase dates
- Profile calculation performance
- Update infrequently (moon changes slowly)
- Validate against online moon phase calculators

**Estimated Time**: 6-8 hours

---

### Phase 4: CV Output Generation (Difficulty: Medium)

**Tasks**:
1. Implement Phase CV output (0.0-1.0):
   - Convert Astronomy Engine phase angle to normalized value
   - Smooth with `juce::SmoothedValue<float>`
2. Implement Illumination CV output:
   - Use `Astronomy_Illumination()` result directly
   - Normalize to 0.0-1.0
3. Implement Altitude CV output:
   - Normalize altitude (-90 to 90 degrees) to 0.0-1.0
   - Handle below-horizon case (negative altitude)
4. Implement Azimuth CV output:
   - Normalize azimuth (0-360 degrees) to 0.0-1.0
5. Implement Orbital Velocity CV output:
   - Calculate from orbital mechanics (simplified)
   - Normalize to 0.0-1.0
6. Smooth all outputs to prevent clicks

**Risks**: 
- Output range mapping errors
- Smoothing artifacts
- Below-horizon handling

**Mitigation**:
- Test output ranges with known moon positions
- Use appropriate smoothing times (100-500ms)
- Clamp altitude to valid range
- Handle edge cases (polar regions, extreme times)

**Estimated Time**: 4-5 hours

---

### Phase 5: Horizon Triggers (Difficulty: Medium)

**Tasks**:
1. Detect moonrise: altitude crosses from negative to positive
2. Detect moonset: altitude crosses from positive to negative
3. Implement edge detection (compare current vs previous altitude)
4. Generate trigger pulses with configurable width
5. Handle debouncing (prevent multiple triggers near threshold)
6. Output trigger gates (sustained) and pulses (brief)

**Risks**: 
- False triggers (noise near horizon)
- Multiple triggers per crossing
- Timing accuracy (block-based detection)

**Mitigation**:
- Use hysteresis (threshold ± small offset)
- Debounce with time window
- Check crossing direction (rising vs falling)
- Test with known moonrise/moonset times
- Handle edge case: moon never sets (polar regions)

**Estimated Time**: 4-6 hours

---

### Phase 6: Phase Triggers (Difficulty: Easy-Medium)

**Tasks**:
1. Detect phase transitions (new moon, full moon, quarters)
2. Compare current phase to previous phase
3. Generate trigger on phase change exceeding threshold
4. Configurable sensitivity (how much phase change triggers)
5. Output trigger pulse

**Risks**: 
- False triggers (small phase changes)
- Multiple triggers per phase
- Phase calculation accuracy

**Mitigation**:
- Use phase change threshold (sensitivity parameter)
- Track previous phase value
- Only trigger on significant changes
- Test with known phase dates

**Estimated Time**: 2-3 hours

---

### Phase 7: Time Modulation (Difficulty: Easy)

**Tasks**:
1. Implement Time Speed CV input:
   - Read CV from input channel
   - Map 0.0-1.0 to 0.0-10.0 speed multiplier
   - Apply to time calculation
2. Implement Phase Offset CV input:
   - Read CV from input channel
   - Add offset to phase calculation
3. Implement Location Offset CV inputs:
   - Read lat/lon CV from input channels
   - Add offset to observer location
   - Clamp to valid ranges

**Risks**: 
- CV range mapping errors
- Invalid coordinate ranges
- Performance (per-sample CV reading)

**Mitigation**:
- Clamp CV values to valid ranges
- Validate coordinate ranges
- Smooth CV inputs if needed
- Test with various CV sources

**Estimated Time**: 3-4 hours

---

### Phase 8: Star Rotation System (Difficulty: Medium-Hard)

**Tasks**:
1. Load star catalog from Astronomy Engine
2. Calculate star positions in alt-az coordinates:
   - Convert star catalog positions (RA/Dec) to alt-az
   - Account for Earth's rotation (sidereal day)
   - Apply speed multiplier to rotation rate
3. Implement star rotation over time:
   - Stars rotate around celestial poles
   - Calculate altitude/azimuth for each star
   - Update positions based on time and speed
4. Optimize star calculations:
   - Pre-calculate star positions (they're fixed in celestial sphere)
   - Only update rotation angle (Earth's rotation)
   - Use spatial indexing for horizon proximity queries
5. Implement star depth filtering:
   - Filter by magnitude/distance
   - Apply depth map parameter (0.0-1.0)
   - Only process stars within filter range

**Risks**: 
- Orbital mechanics complexity
- Calculation accuracy
- Performance (iterative Kepler solver)
- Integration with sequencer timing

**Mitigation**:
- Start with simplified velocity calculation
- Use lookup table for Kepler's equation (if needed)
- Profile performance
- Test step timing with external sequencers
- Validate against known orbital data

**Estimated Time**: 8-12 hours

---

### Phase 9: UI Implementation (Difficulty: Medium)

**Tasks**:
1. Implement `drawParametersInNode()`:
   - Location inputs (lat/lon)
   - Time speed control
   - Mode selection (Real-Time / Elliptic Sequencer / Hybrid)
   - Trigger settings
   - Elliptic sequencer parameters
2. Implement visualization:
   - Moon phase icon (crescent/full/etc.)
   - Current phase percentage
   - Altitude indicator (horizon line)
   - Orbital diagram (for sequencer mode)
3. Implement `drawIoPins()`:
   - Label all 8 outputs clearly
   - Label all 4 inputs clearly
4. Implement `getAudioInputLabel()` and `getAudioOutputLabel()`

**Risks**: 
- UI complexity
- Visualization performance
- ImGui integration issues

**Mitigation**:
- Follow existing UI patterns
- Use simple visualizations initially
- Optimize drawing (don't recalculate every frame)
- Test on various screen sizes

**Estimated Time**: 6-8 hours

---

### Phase 10: State Persistence (Difficulty: Easy)

**Tasks**:
1. Save parameters via APVTS (automatic)
2. Save manual time offset (if using extra state)
3. Validate loaded state on preset load
4. Handle version compatibility

**Risks**: 
- Parameter persistence issues
- Version compatibility

**Mitigation**:
- Use APVTS for all parameters (standard approach)
- Test preset save/load
- Add version check for future changes

**Estimated Time**: 1-2 hours

---

### Phase 11: Testing & Validation (Difficulty: Medium)

**Tasks**:
1. Unit tests:
   - Time conversion accuracy
   - Phase calculation accuracy
   - Coordinate transformations
2. Integration tests:
   - CV output ranges
   - Trigger timing
   - Modulation inputs
3. Validation tests:
   - Compare against known moon phase dates
   - Compare against online calculators
   - Test horizon crossings
4. Performance tests:
   - Calculation frequency impact
   - Memory usage
   - CPU usage

**Risks**: 
- Calculation accuracy issues
- Performance problems
- Edge case failures

**Mitigation**:
- Test against multiple sources
- Profile performance
- Test edge cases (polar regions, extreme times)
- Validate with astronomical data

**Estimated Time**: 6-8 hours

---

### Phase 12: Star Tracking (Future Enhancement, Difficulty: Medium-Hard)

**Tasks**:
1. Integrate star catalog from Astronomy Engine
2. Add star selection parameter
3. Calculate star positions (altitude/azimuth)
4. Add star outputs (similar to moon outputs)
5. UI for star selection

**Risks**: 
- Star catalog size
- Performance with many stars
- UI complexity

**Mitigation**:
- Start with bright stars only (~9000 stars)
- Use efficient lookup
- Lazy loading of star data

**Estimated Time**: 8-10 hours (future)

---

### Phase 12b: Star Distance Filtering / Depth Map (Future Enhancement, Difficulty: Medium)

**Concept**: Filter stars by distance from Earth, creating a "depth map" effect where closer stars can be processed differently than distant ones.

**Tasks**:
1. **Distance Data Integration**:
   - Access star distance data from catalog (parallax → distance in light-years/parsecs)
   - Astronomy Engine supports custom stars with distances
   - Hipparcos/Gaia catalogs include parallax measurements
   - Convert parallax to distance: `distance_ly = 1.0 / (parallax_arcsec * 3.26156)`

2. **Distance-Based CV Outputs**:
   - **Distance CV** (0.0-1.0): Normalized distance of selected star(s)
     - Map distance range (e.g., 0-1000 light-years) to 0.0-1.0
     - Closer stars = higher CV, farther stars = lower CV
   - **Average Distance CV**: Average distance of visible stars (within filter range)
   - **Nearest Star Distance CV**: Distance to closest visible star
   - **Farthest Star Distance CV**: Distance to farthest visible star

3. **Distance Filter Parameters**:
   - **Min Distance** (light-years): Only process stars beyond this distance
   - **Max Distance** (light-years): Only process stars within this distance
   - **Distance Range Mode**: "All", "Near Only", "Far Only", "Custom Range"
   - **Distance Smoothing**: Smooth distance CV outputs (stars move slowly)

4. **Distance-Based Modulation**:
   - **Distance → Brightness**: Closer stars appear brighter (inverse square law)
   - **Distance → Color**: Optional color temperature based on distance
   - **Distance → Trigger**: Trigger when star enters/leaves distance range

5. **Depth Map Visualization**:
   - Color-code stars by distance in UI
   - Near stars: brighter/warmer colors
   - Far stars: dimmer/cooler colors
   - Optional: 3D depth visualization

6. **Performance Optimization**:
   - Pre-filter stars by distance before processing
   - Cache distance calculations (stars don't move much)
   - Use spatial indexing for efficient range queries

**Use Cases**:
- **Depth-Based Modulation**: Closer stars modulate parameters more strongly
- **Layered Soundscapes**: Different distance bands control different parameters
- **Spatial Audio**: Distance affects panning, reverb, or spatialization
- **Dynamic Filtering**: Only process stars within certain distance ranges
- **Depth Visualization**: Visual representation of stellar depth

**Implementation Example**:
```cpp
// Filter stars by distance
std::vector<StarInfo> filteredStars;
for (const auto& star : starCatalog) {
    double distanceLy = calculateDistanceFromParallax(star.parallax);
    if (distanceLy >= minDistance && distanceLy <= maxDistance) {
        filteredStars.push_back(star);
    }
}

// Calculate average distance CV
double avgDistance = 0.0;
for (const auto& star : filteredStars) {
    avgDistance += star.distanceLy;
}
avgDistance /= filteredStars.size();
double distanceCV = normalizeDistance(avgDistance, 0.0, 1000.0); // 0-1000 ly → 0.0-1.0
```

**Risks**: 
- Star catalog may not have distance data for all stars
- Parallax measurements have errors (especially for distant stars)
- Performance with large star catalogs
- Distance range selection complexity

**Mitigation**:
- Use stars with reliable parallax data only
- Handle missing distance data gracefully (skip or estimate)
- Pre-filter catalog by distance before loading
- Use efficient data structures (spatial index, distance-sorted lists)
- Make distance filtering optional (can disable if problematic)

**Estimated Time**: 6-8 hours (future, extends Phase 12)

---

## Difficulty Levels

### Easy (1-3 hours each)
- Module structure setup
- Parameter definitions
- State persistence
- Basic UI controls

### Medium (3-6 hours each)
- Astronomy Engine integration
- Moon calculations
- CV output generation
- Horizon triggers
- Phase triggers
- Time modulation
- UI implementation

### Medium-Hard (6-8 hours each)
- Elliptic sequencer mode
- Testing & validation

### Hard (8+ hours)
- Advanced orbital mechanics
- Star tracking (future)
- Complex visualizations

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM** (5/10)

### High-Risk Areas

#### 1. Astronomy Engine Integration (Risk: 6/10)
**Issue**: External library integration, build system complexity, API learning curve

**Mitigation**:
- Use FetchContent for easy integration
- Test in isolated branch first
- Study documentation thoroughly
- Validate on all platforms
- Have fallback plan (simple phase calculation if needed)

**Impact**: Build failures, compilation errors, platform-specific issues

---

#### 2. Calculation Performance (Risk: 5/10)
**Issue**: Astronomical calculations may be expensive, updating too frequently could impact audio thread

**Mitigation**:
- Update calculations infrequently (every 256-1024 samples)
- Cache results between updates
- Profile performance
- Use simplified calculations where possible
- Consider background thread for heavy calculations (future)

**Impact**: Audio dropouts, CPU spikes, performance degradation

---

#### 3. Calculation Accuracy (Risk: 4/10)
**Issue**: Moon position/phase calculations must be accurate, errors could cause incorrect CV outputs

**Mitigation**:
- Use Astronomy Engine (well-tested library)
- Validate against known moon phase dates
- Compare with online calculators
- Test edge cases (polar regions, extreme times)
- Document accuracy expectations

**Impact**: Incorrect CV outputs, wrong trigger timing, user confusion

---

#### 4. Coordinate System Conversions (Risk: 5/10)
**Issue**: Multiple coordinate systems (degrees, radians, normalized CV), conversion errors

**Mitigation**:
- Use consistent conversion functions
- Test coordinate transformations
- Validate output ranges
- Handle edge cases (polar regions, date boundaries)

**Impact**: Incorrect CV outputs, visualization errors

---

#### 5. Horizon Crossing Detection (Risk: 6/10)
**Issue**: Detecting moonrise/moonset accurately, avoiding false triggers, handling edge cases

**Mitigation**:
- Use hysteresis for threshold
- Debounce triggers
- Check crossing direction
- Handle polar regions (moon never sets)
- Test with known moonrise/moonset times

**Impact**: Missed triggers, false triggers, incorrect timing

---

### Medium-Risk Areas

#### 6. Elliptic Sequencer Complexity (Risk: 5/10)
**Issue**: Orbital mechanics calculations are complex, integration with sequencer timing

**Mitigation**:
- Start with simplified calculations
- Use lookup tables if needed
- Test with external sequencers
- Validate against known orbital data
- Make it optional (can disable if problematic)

**Impact**: Incorrect step timing, performance issues

---

#### 7. Time Handling (Risk: 4/10)
**Issue**: Time zone handling, leap years, system time accuracy, manual time offset

**Mitigation**:
- Use UTC for calculations (Astronomy Engine standard)
- Handle time zone conversion properly
- Test with various system times
- Validate manual time offset

**Impact**: Incorrect calculations, time zone confusion

---

#### 8. UI Complexity (Risk: 4/10)
**Issue**: Many parameters, complex visualizations, performance

**Mitigation**:
- Follow existing UI patterns
- Use simple visualizations initially
- Optimize drawing
- Test on various screen sizes

**Impact**: UI lag, poor user experience

---

### Low-Risk Areas

#### 9. Parameter Persistence (Risk: 2/10)
**Issue**: Standard APVTS persistence, well-understood

**Mitigation**: Use standard APVTS approach

---

#### 10. Module Registration (Risk: 2/10)
**Issue**: Standard module registration, follows existing patterns

**Mitigation**: Follow BestPracticeNodeProcessor pattern

---

## Confidence Rating

### Overall Confidence: **MEDIUM-HIGH** (7/10)

### Strong Points

1. **Well-Documented Library**: Astronomy Engine has good documentation and examples
2. **Clear Architecture**: Separation of concerns (calculations, outputs, triggers)
3. **Existing Patterns**: Follows BestPracticeNodeProcessor and other module patterns
4. **Incremental Implementation**: Phases can be tested independently
5. **MIT License**: No license concerns
6. **Proven Library**: Astronomy Engine is used in production software
7. **Modular Design**: Can disable features if problematic (elliptic sequencer optional)

### Weak Points

1. **External Dependency**: Adds new library dependency
2. **API Learning Curve**: Need to learn Astronomy Engine API
3. **Calculation Complexity**: Astronomical calculations are non-trivial
4. **Performance Unknown**: Don't know calculation cost until profiling
5. **Testing Complexity**: Need to validate against astronomical data
6. **Edge Cases**: Many edge cases (polar regions, extreme times, etc.)
7. **Elliptic Sequencer**: Complex feature, may need simplification

### Areas Requiring Extra Attention

1. **Astronomy Engine Integration**: Test thoroughly on all platforms
2. **Calculation Accuracy**: Validate extensively against known data
3. **Performance Profiling**: Measure calculation cost, optimize if needed
4. **Edge Case Testing**: Polar regions, extreme times, invalid coordinates
5. **Trigger Timing**: Validate moonrise/moonset detection accuracy
6. **Elliptic Sequencer**: May need to simplify or make optional

---

## Potential Problems & Solutions

### Problem 1: Astronomy Engine Build Failures
**Scenario**: Library doesn't compile on target platform

**Solution**: 
- Use FetchContent for automatic handling
- Test on all platforms early
- Have fallback: simple phase calculation (no library)
- Consider git submodule if FetchContent fails

---

### Problem 2: Calculation Performance Issues
**Scenario**: Calculations too expensive, cause audio dropouts

**Solution**: 
- Update infrequently (every 256-1024 samples)
- Cache results
- Profile and optimize
- Use simplified calculations where possible
- Consider background thread (future)

---

### Problem 3: Incorrect Moon Positions
**Scenario**: Calculations don't match real moon positions

**Solution**: 
- Validate against known dates
- Compare with online calculators
- Check coordinate system conversions
- Test edge cases
- Use Astronomy Engine's built-in validation

---

### Problem 4: False Horizon Triggers
**Scenario**: Triggers fire when moon doesn't actually cross horizon

**Solution**: 
- Use hysteresis (threshold ± offset)
- Debounce with time window
- Check crossing direction
- Validate against known moonrise/moonset times
- Handle edge cases (polar regions)

---

### Problem 5: Elliptic Sequencer Too Complex
**Scenario**: Orbital mechanics calculations too difficult or inaccurate

**Solution**: 
- Start with simplified version
- Make it optional (can disable)
- Use lookup tables for complex calculations
- Simplify orbital model if needed
- Focus on real-time tracking first

---

### Problem 6: Time Zone Confusion
**Scenario**: Calculations use wrong time zone, incorrect results

**Solution**: 
- Always use UTC for calculations
- Convert from local time to UTC
- Document time zone handling
- Test with various time zones
- Allow manual UTC offset

---

### Problem 7: Coordinate Range Errors
**Scenario**: Invalid latitude/longitude cause crashes or incorrect results

**Solution**: 
- Clamp coordinates to valid ranges
- Validate on parameter change
- Handle edge cases (poles, date line)
- Test with extreme coordinates
- Provide sensible defaults

---

### Problem 8: CV Output Range Issues
**Scenario**: CV outputs outside 0.0-1.0 range, cause downstream issues

**Solution**: 
- Always clamp outputs to 0.0-1.0
- Validate output ranges
- Test with various moon positions
- Document output ranges
- Use appropriate smoothing

---

### Problem 9: Trigger Pulse Width Issues
**Scenario**: Triggers too short/long, miss downstream modules

**Solution**: 
- Make pulse width configurable
- Use appropriate default (10ms)
- Test with various sequencers
- Allow both pulse and gate modes
- Document trigger behavior

---

### Problem 10: Module Registration Failures
**Scenario**: Module not found in factory, pin database issues

**Solution**: 
- Follow BestPracticeNodeProcessor pattern exactly
- Use lowercase naming: `"moon tracker"`
- Register in ModularSynthProcessor
- Add to pin database
- Test module creation/deletion

---

### Problem 11: State Persistence Issues
**Scenario**: Parameters not saved/loaded correctly in presets

**Solution**: 
- Use APVTS for all parameters
- Test preset save/load
- Validate loaded state
- Handle version compatibility
- Test with various parameter values

---

### Problem 12: UI Performance Issues
**Scenario**: Visualizations cause UI lag, poor responsiveness

**Solution**: 
- Optimize drawing (don't recalculate every frame)
- Use simple visualizations initially
- Cache visualization data
- Update infrequently (every N frames)
- Profile UI performance

---

### Problem 13: Elliptic Sequencer Integration
**Scenario**: Step timing CV doesn't work well with external sequencers

**Solution**: 
- Test with various sequencers
- Make CV range configurable
- Document expected behavior
- Provide examples
- Allow disabling if problematic

---

### Problem 14: Star Tracking Complexity (Future)
**Scenario**: Star catalog too large, performance issues

**Solution**: 
- Start with bright stars only
- Use efficient lookup
- Lazy load star data
- Make it optional
- Consider separate module (future)

---

### Problem 16: Star Distance Data Missing (Future)
**Scenario**: Star catalog doesn't have distance/parallax data for all stars

**Solution**: 
- Use stars with reliable parallax data only
- Filter catalog before loading (exclude stars without distance)
- Handle missing data gracefully (skip or use default)
- Provide option to estimate distance from magnitude (rough approximation)
- Document which stars have distance data

---

### Problem 17: Distance Range Performance (Future)
**Scenario**: Filtering stars by distance is too slow with large catalogs

**Solution**: 
- Pre-filter catalog by distance before loading
- Use spatial indexing (octree, k-d tree) for efficient range queries
- Cache filtered results (stars don't move much)
- Limit catalog size (bright stars only)
- Use distance-sorted lists for binary search
- Profile and optimize hot paths

---

### Problem 15: Documentation Gaps
**Scenario**: Users don't understand how to use the module

**Solution**: 
- Document all parameters clearly
- Provide usage examples
- Explain coordinate systems
- Document trigger behavior
- Add tooltips in UI

---

## Testing Strategy

### Unit Tests
- Time conversion (JUCE Time → Julian Day)
- Phase calculation accuracy
- Coordinate transformations
- CV output range mapping
- Trigger detection logic

### Integration Tests
- Moon calculations → CV outputs
- Horizon crossing → triggers
- Phase changes → triggers
- Modulation inputs → calculations
- Elliptic sequencer → step timing

### Validation Tests
- Compare against known moon phase dates
- Compare against online calculators
- Test moonrise/moonset times
- Validate orbital mechanics
- Test edge cases (poles, extreme times)

### Performance Tests
- Calculation frequency impact
- Memory usage
- CPU usage
- UI responsiveness
- Memory leaks

### Stress Tests
- Long playback sessions (hours)
- Rapid parameter changes
- Multiple instances
- High sample rates (96kHz+)
- Many modules in patch

---

## Future Enhancements

### Phase 13 (Optional, Advanced)
1. **Star Tracking**: Full star catalog integration
2. **Star Distance Filtering / Depth Map**: Filter stars by distance, distance-based CV outputs (see Phase 12b)
3. **Planet Tracking**: Track planets (Venus, Mars, etc.)
4. **Eclipse Detection**: Detect solar/lunar eclipses
5. **Tidal Effects**: Calculate tidal forces (for modulation)
6. **Custom Orbits**: User-defined orbital patterns
7. **Multiple Locations**: Track moon from multiple locations simultaneously
8. **Historical Mode**: Calculate past/future moon positions
9. **Visualization Enhancements**: 3D orbital diagrams, sky maps, depth-coded star visualization
10. **Distance-Based Modulation**: Different distance bands control different parameters (layered soundscapes)

---

## Estimated Total Time

**Minimum (Core Features)**: 35-45 hours
- Phases 1-7, basic testing
- Real-time tracking, basic triggers

**Realistic (Complete Feature)**: 50-65 hours
- All phases except star tracking
- Comprehensive testing, edge cases
- Elliptic sequencer mode

**Maximum (With Polish)**: 70-85 hours
- All phases including star tracking
- Extensive testing, optimization
- Advanced features, documentation

---

## Dependencies

### External Dependencies
- **Astronomy Engine**: MIT license, C/C++ library
  - Source: https://github.com/cosinekitty/astronomy
  - Integration: FetchContent or git submodule

### Internal Dependencies
- Existing `ModuleProcessor` base class
- Existing `TransportState` structure (for time sync, optional)
- APVTS parameter system
- UI rendering system (ImGui)
- JUCE Time class

---

## Success Criteria

### Must Have
✅ Moon phase CV output (accurate)  
✅ Moon illumination CV output  
✅ Moon altitude/azimuth CV outputs  
✅ Moonrise/moonset triggers  
✅ Phase change triggers  
✅ Time modulation inputs  
✅ Location parameters (lat/lon)  
✅ State persists in presets  
✅ Basic UI controls  
✅ Edge cases handled gracefully  

### Nice to Have
✅ Elliptic sequencer mode  
✅ Orbital velocity CV output  
✅ Advanced visualizations  
✅ Performance optimization  
✅ Comprehensive documentation  

### Future Enhancements
⏳ Star tracking  
⏳ Planet tracking  
⏳ Eclipse detection  
⏳ Advanced orbital mechanics  

---

## Critical Design Decisions

### 1. Library Choice
**Decision**: Astronomy Engine (MIT license, C/C++)
**Rationale**: Well-documented, proven, MIT license, supports moon and stars
**Alternative**: Custom Meeus implementation (more work, less accurate)

### 2. Update Frequency
**Decision**: Update calculations every 256-1024 samples
**Rationale**: Moon changes slowly, reduces CPU usage
**Alternative**: Per-sample updates (too expensive)

### 3. Output Smoothing
**Decision**: Use `juce::SmoothedValue<float>` for all CV outputs
**Rationale**: Prevents clicks, standard JUCE pattern
**Alternative**: No smoothing (audible clicks)

### 4. Elliptic Sequencer
**Decision**: Make it optional, can disable if problematic
**Rationale**: Complex feature, may need simplification
**Alternative**: Required feature (higher risk)

### 5. Coordinate System
**Decision**: Use degrees for user input, radians internally, normalized CV output
**Rationale**: User-friendly input, standard astronomical calculations, modular CV
**Alternative**: All radians (confusing for users)

---

## Conclusion

This feature is **feasible** with **medium complexity** and **medium risks**. The Astronomy Engine integration is the primary risk, but the library is well-documented and proven. The modular design allows for incremental implementation and testing. The elliptic sequencer mode is the most complex feature and can be made optional if needed.

**Recommendation**: **Proceed with implementation** starting with Phase 1 (Astronomy Engine integration) and Phase 2 (basic module structure). Test each phase before moving to the next. Consider making elliptic sequencer mode optional initially to reduce risk.

---

## Implementation Checklist

- [ ] Phase 1: Astronomy Engine Integration
- [ ] Phase 2: Basic Module Structure
- [ ] Phase 3: Basic Moon Calculations
- [ ] Phase 4: CV Output Generation
- [ ] Phase 5: Horizon Triggers
- [ ] Phase 6: Phase Triggers
- [ ] Phase 7: Time Modulation
- [ ] Phase 8: Elliptic Sequencer Mode
- [ ] Phase 9: UI Implementation
- [ ] Phase 10: State Persistence
- [ ] Phase 11: Testing & Validation
- [ ] Phase 12: Star Tracking (Future)
- [ ] Documentation: Code comments
- [ ] Documentation: User guide
- [ ] Performance: Profiling
- [ ] Performance: Optimization

---

---

**End of Plan**

