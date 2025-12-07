# Automation Lane Trigger Debug Notes

## Current Implementation Status

### What's Been Implemented:
1. ✅ Trigger threshold parameter (0.0-1.0, default 0.5)
2. ✅ Trigger edge mode parameter (Rising/Falling/Both)
3. ✅ Output bus expanded to 5 channels (added OUTPUT_TRIGGER)
4. ✅ Line segment crossing detection function
5. ✅ Trigger pulse generation (1ms duration)
6. ✅ Trigger output writing
7. ✅ Threshold line visualization
8. ✅ UI controls (slider + combo)

### Potential Issues:

1. **Initialization Problem**: 
   - `previousValue` starts at -1.0f (uninitialized)
   - First sample initializes it, but no trigger on first sample (correct)
   - Need to ensure it initializes properly when transport starts

2. **Crossing Detection Logic**:
   - Uses line segment intersection (geometric approach)
   - Edge modes: Rising/Falling/Both
   - Logic appears correct but might need testing

3. **Pulse Duration**:
   - 1ms = 0.001 * sampleRate samples
   - Counter decrements each sample
   - Should work correctly

4. **State Management**:
   - `previousValue` is member variable (persists across blocks) ✓
   - `triggerPulseRemaining` is member variable (persists across blocks) ✓
   - Reset on transport stop ✓
   - Reset on loop wrap ✓

### Debug Checklist:

- [ ] Verify trigger output channel is being written to
- [ ] Check if automation curve actually crosses threshold
- [ ] Verify threshold line is visible and positioned correctly
- [ ] Test with simple automation (0.0 → 1.0 ramp) crossing threshold at 0.5
- [ ] Verify edge mode selection works
- [ ] Check if trigger fires when transport starts
- [ ] Verify trigger works in sync mode
- [ ] Verify trigger works in free-running mode
- [ ] Check pulse duration (should be 1ms = ~44 samples at 44.1kHz)
- [ ] Verify trigger resets on loop wrap

### Common Issues to Check:

1. **Automation curve doesn't cross threshold**: 
   - Draw a curve that clearly goes above and below the threshold line
   - Try threshold at 0.25 or 0.75 for easier testing

2. **Trigger output not connected**:
   - Verify the trigger pin exists in the UI
   - Connect it to something visible (like a Scope or Value module)

3. **Edge mode wrong**:
   - If curve goes 0.3 → 0.7 with threshold at 0.5, use "Rising"
   - If curve goes 0.7 → 0.3 with threshold at 0.5, use "Falling"
   - Use "Both" to trigger on either direction

4. **Transport not playing**:
   - Trigger only fires when transport is playing
   - Check transport state

5. **Initialization timing**:
   - First sample after transport starts won't trigger (correct behavior)
   - Need at least 2 samples for crossing detection

### Suggested Test:

1. Set threshold to 0.5
2. Set edge mode to "Both"
3. Draw automation curve from 0.0 (bottom) to 1.0 (top)
4. Start transport
5. Should see trigger fire once when curve crosses 0.5 threshold

### Next Steps:

1. Add diagnostic logging (optional, for debugging)
2. Simplify crossing detection logic if needed
3. Test with known-good automation patterns
4. Verify output pin is visible and connectable

