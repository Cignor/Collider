## Moofy: Video I/O, Audio Sync, and FX Context (JUCE + OpenCV + FFmpeg)

### Purpose
Give an external helper precise, self-contained context to work on video playback + audio extraction/sync and video FX processing in this project. Focus files: `juce/Source/audio/modules/VideoFileLoaderModule.cpp`, `juce/Source/audio/modules/VideoFXModule.cpp`. This brief explains responsibilities, threading, parameters, data flow, dependencies, and concrete asks.

### Tech Stack
- **Framework**: JUCE (Audio/MIDI, APVTS parameters, processor graph)
- **Video**: OpenCV (cv::VideoCapture; optional CUDA path in FX)
- **Demux/Decode (Audio)**: FFmpeg (via custom `FFmpegAudioReader`)
- **UI**: ImGui controls when `PRESET_CREATOR_UI` is defined

### High-Level Data Flow
1) `VideoFileLoaderModule` loads a video file via OpenCV, extracts audio via FFmpeg, and publishes frames to `VideoFrameManager` keyed by the module’s logical ID. It also outputs stereo audio to the audio graph and a mono CV lane carrying the source ID for downstream modules.
2) `VideoFXModule` reads frames from `VideoFrameManager` using a Source ID coming in on its input pin, applies CPU/CUDA image effects, republishes processed frames under its own logical ID, and forwards that ID via its output mono lane for chaining.

### Core Responsibilities
- **VideoFileLoaderModule**
  - Open video (prefers FFmpeg backend; falls back) and fetch metadata (FPS, FOURCC, frame count).
  - Extract audio via `FFmpegAudioReader` into a producer FIFO. Audio thread consumes from FIFO, with optional time-stretch/speed using `TimePitchProcessor` in two modes: `RubberBand` and `Fifo` (naive).
  - Maintain a master audio sample clock (`currentAudioSamplePosition`) that drives video sync. Video is a slave seeking to target frame each iteration based on current audio time.
  - Handle unified seeking (normalized 0..1), IN/OUT trim, loop behavior, and transport sync.
  - Publish frames to `VideoFrameManager`; provide an ARGB `juce::Image` snapshot for UI preview.

- **VideoFXModule**
  - Pull frames by Source ID from `VideoFrameManager`.
  - Apply ordered effects chain (color, temperature/sepia/HSV adjust, posterize, mono/edges/threshold, invert, flips, vignette, pixelate, blur, sharpen). CUDA fast-path when available.
  - Optional kaleidoscope (4- or 8-way) applied last.
  - Publish processed frames under its own logical ID; forward that ID downstream via mono output.

### Threading & Sync Model
- `VideoFileLoaderModule` runs a background thread that:
  - Produces decoded audio chunks into a lock-free style `AbstractFifo` + channel buffers.
  - Reads the master audio position, converts to target frame index via FPS, performs loop reset if beyond OUT point, seeks capture to target frame, reads one frame, publishes it.
  - Performs on-demand seek handling (normalized, frame, or on play edge), resetting audio/video/FIFO/time stretcher as needed.
- Audio processing callback (`processBlock`) in `VideoFileLoaderModule` consumes from FIFO, applies time stretch/speed, writes stereo to `Audio Out`, and advances the master audio sample clock by the number of source samples consumed.
- `VideoFXModule` runs a separate background thread pulling frames at ~30 FPS and processing them.

### Key Parameters (APVTS)
- Video loader: `loop`, `sync` (to transport), `zoomLevel`, `speed` (0.25–4.0), `in`, `out`, `engine` ("RubberBand" | "Naive").
- Video FX: `useGpu`, `zoomLevel`, color/gain sliders, `sepia`, `temperature`, `sharpen`, `blur`, `grayscale`, `invert`, `flipH/V`, `thresholdEnable/Level`, `posterizeLevels`, `vignetteAmount/Size`, `pixelateSize`, `cannyEnable/Thresh1/Thresh2`, `kaleidoscope`.

### Important Types & Connected Files
- `VideoFrameManager` (frame pub/sub by logical ID). Provides `setFrame(id, cv::Mat)` and `getFrame(id)`.
- `FFmpegAudioReader` (audio demux/decode from container; exposes `lengthInSamples`, `sampleRate`, `readSamples`, `resetPosition`).
- `TimePitchProcessor` (dual-mode: `RubberBand` high quality ratio = 1/speed, and `Fifo` naive speed = speed). Methods: `prepare`, `reset`, `setMode`, `setTimeStretchRatio`, `putInterleaved`, `receiveInterleaved`.
- `ModuleProcessor` base: provides bus layouts, `getLogicalId()`, APVTS integration, and node pin helpers.

### Entry Points & Hotspots
- Video open/reopen, metadata, audio load:
  - `VideoFileLoaderModule::run()` open block and `loadAudioFromVideo()`
  - Unified seek handling: normalized (`pendingSeekNormalized`), frame-based fallback (`pendingSeekFrame`)
  - Loop reset hotspot where both audio and video are atomically reset around IN point
- Audio render path:
  - `VideoFileLoaderModule::processBlock()` configures engine, computes ratio, pulls from FIFO, time-stretches, de-interleaves, advances master clock
- FX path:
  - `VideoFXModule::run()` CPU/CUDA bifurcation, ordered effect chain, kaleidoscope last

### Logging & Diagnostics
- Logs backend name, FFmpeg integration, FOURCC, FPS, frame count, and reopen results.
- On preview/pause first frame read, may refresh `CAP_PROP_FRAME_COUNT` when unknown initially.

### Known Behaviors / Invariants
- Master clock is audio-source-sample-based (source rate), not host sample rate. Video derives target frame via `currentAudioSamplePosition / sourceAudioSampleRate * videoFps`.
- Looping occurs when target frame >= OUT frame. Reset is atomic: set audio master clock and read head to IN sample, seek video to IN frame, clear FIFO, reset time stretcher.
- RubberBand engine uses inverse ratio (1/speed), naive engine uses direct speed.

### Potential Edge Cases to Watch
- Variable FPS/containers reporting 0 FPS or frame count → fallback to 30 FPS and ratio-based seeks until known.
- Backend differences (FFmpeg vs MSMF) re: precise frame seek and `CAP_PROP_POS_FRAMES` rounding.
- Audio/video drift if master clock advancement (`readCount`) mismatches what time stretcher actually consumes/produces.
- Large seeks while playing: ensuring FIFO flush and stretcher reset occur reliably.
- Mono videos or >2 channel audio: current code assumes stereo in/out.

### Concrete Questions / Tasks for Helper
1) Verify A/V sync correctness across variable speeds (0.25–4.0) for both engines. If drift occurs, propose a correction strategy (e.g., advance clock by exact samples consumed, or resample alignment logic).
2) Improve seek precision across backends: best practices to ensure exact-frame hits (FFmpeg capture flags, alternative APIs, or timestamp-based seeks with tolerance).
3) Suggest robust handling when FPS or frame-count are initially unknown and become known after first reads.
4) Evaluate FIFO sizing and read chunk size for latency vs stability trade-offs; propose adaptive policy.
5) CUDA path: which effects are worth porting to CUDA to significantly reduce CPU load while preserving visual parity?
6) Any race conditions around `captureLock` and `audioLock` ordering (deadlock risk) and recommended lock ordering/partitioning.
7) Recommend test methodology to automatically validate IN/OUT trim, loop boundaries, and sync across long durations.

### Minimal Repro & How-To
1) Load a video via the Video File Loader node UI (“Load Video File…”).
2) Press Play or enable transport sync. Adjust `speed`, `in`, `out`, and `loop`.
3) Insert a Video FX node; wire the loader’s CV output (Source ID) into FX input; monitor processed output in UI.
4) Toggle engine (RubberBand/Naive) and vary speed; watch sync and loop boundary behavior.

### Key Snippets (references)
Target frame computed from master audio clock:
```399:407:juce/Source/audio/modules/VideoFileLoaderModule.cpp
const juce::int64 audioMasterPosition = currentAudioSamplePosition.load();
const double sourceRate = sourceAudioSampleRate.load();
const double currentTimeInSeconds = (double)audioMasterPosition / sourceRate;
int targetVideoFrame = (int)(currentTimeInSeconds * videoFps);
```

Loop reset when passing OUT boundary:
```411:434:juce/Source/audio/modules/VideoFileLoaderModule.cpp
if (targetVideoFrame >= endFrame)
{
    if (isLoopingEnabled)
    {
        const juce::ScopedLock audioLk(audioLock);
        const juce::ScopedLock capLock(captureLock);
        const juce::int64 startSample = (juce::int64)(startNormalized * audioReader->lengthInSamples);
        currentAudioSamplePosition.store(startSample);
        audioReadPosition = (double)startSample;
        if (videoCapture.isOpened()) videoCapture.set(cv::CAP_PROP_POS_FRAMES, startFrame);
        abstractFifo.reset();
        timePitch.reset();
        targetVideoFrame = startFrame;
    }
    else {
        playing.store(false);
    }
}
```

Engine ratio selection in audio path:
```560:577:juce/Source/audio/modules/VideoFileLoaderModule.cpp
const float speed = juce::jlimit(0.25f, 4.0f, speedParam ? speedParam->load() : 1.0f);
const int engineIdx = engineParam ? engineParam->getIndex() : 1;
auto requestedMode = (engineIdx == 0) ? TimePitchProcessor::Mode::RubberBand : TimePitchProcessor::Mode::Fifo;
timePitch.setMode(requestedMode);
double ratioForEngine = (double)speed;
if (requestedMode == TimePitchProcessor::Mode::RubberBand)
    ratioForEngine = 1.0 / juce::jmax(0.01, (double)speed);
timePitch.setTimeStretchRatio(ratioForEngine);
```

Advancing master clock by consumed source samples:
```628:631:juce/Source/audio/modules/VideoFileLoaderModule.cpp
// Advance the master clock by the number of samples consumed
currentAudioSamplePosition.store(currentAudioSamplePosition.load() + readCount);
```

FX GPU fast-path gate:
```146:154:juce/Source/audio/modules/VideoFXModule.cpp
if (useGpu && cv::cuda::getCudaEnabledDeviceCount() > 0)
{
    cv::cuda::GpuMat gpuFrame;
    gpuFrame.upload(frame);
    // basic ops then download and continue CPU for complex ops
}
```

### Build/Run Requirements
- OpenCV built with FFmpeg support for robust container/codec handling; CUDA optional for FX.
- FFmpeg libraries available for `FFmpegAudioReader`.
- JUCE project configured with two audio output buses for `VideoFileLoaderModule` (CV mono + Audio stereo) and mono I/O for `VideoFXModule`.

### What “Done” Looks Like for a Sync Audit
- No visible drift after 10+ minutes at 0.5x, 1.0x, 2.0x across both engines.
- Exact-frame hits on seek; loop boundaries are seamless; IN/OUT honored.
- FIFO never underruns at typical block sizes; CPU usage acceptable; CUDA path stable.


