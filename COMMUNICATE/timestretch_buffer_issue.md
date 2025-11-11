## Context
- TimePitch node (`TimePitchModuleProcessor`) glitches under modulation because it feeds too few samples into `TimePitchProcessor` when playback speed rises or pitch is shifted.
- Video loader module uses the same `TimePitchProcessor` facade without issues; it always scales FIFO consumption by playback speed.
- Goal: align the node’s buffer math with the video loader while keeping RubberBand/Naive mode selection intact.

## Changes Made
- Track the last engine mode in `TimePitchModuleProcessor` to reset the DSP only when switching RubberBand/Naive.
- Convert the smoothed speed value into the correct ratio per engine: RubberBand expects `1 / speed`, Naive takes raw speed.
- Account for pitch semitone offsets when estimating FIFO consumption for the Naive mode so the FIFO stays ahead of the reader.
- Feed `framesToFeed = ceil(numSamples * consumptionRatio)` just like the loader module instead of dividing by speed.
- Refresh live telemetry (`speed_live`, `pitch_live`) only after the smoothed values advance, so UI mirrors the actual DSP inputs.

## Follow-Up / Questions
- Please confirm whether the Naive path should continue coupling pitch and speed (current `FifoEngine` design). If independent pitch is required we will need a different lightweight resampler.
- Stress-test with extreme CV modulation (fast positive pitch sweeps at max speed) to confirm FIFO headroom is sufficient; adjust `fifoSize` if we still underrun.
- RubberBand tuning: we might expose window/phase options if future QA finds artifacts. For now we reuse existing defaults.

## Hand-Off Checklist
- Build both Debug/Release (Ninja presets) after pulling changes.
- Run module in preset creator with: constant speed=4.0, pitch sweeps ±24 st, and alternating engine selection to ensure resets remain glitch-free.
- Keep new Moofy script `MOOFYS/moofy_timestretch_diagnostics.ps1` handy for any further investigations.

