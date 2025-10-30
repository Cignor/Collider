# Pose Estimator Module - Setup Guide

## Overview

The **Pose Estimator Module** uses the OpenPose MPI model to detect 15 human body keypoints in real-time video. It outputs 30 CV signals (x,y coordinates) representing the positions of:

- Head, Neck, Chest
- Left/Right Shoulders, Elbows, Wrists
- Left/Right Hips, Knees, Ankles

These signals can be used to modulate any parameter in your synthesizer, enabling body-tracking musical performance, gesture control, and interactive installations.

---

## Installation

### 1. Download the OpenPose MPI Model Files

You need two files from the OpenPose project:

1. **pose_deploy_linevec_faster_4_stages.prototxt** (network architecture)
2. **pose_iter_160000.caffemodel** (trained weights)

#### Download Links:

- **Prototxt:** [pose_deploy_linevec_faster_4_stages.prototxt](https://raw.githubusercontent.com/CMU-Perceptual-Computing-Lab/openpose/master/models/pose/mpi/pose_deploy_linevec_faster_4_stages.prototxt)
  
- **Caffemodel:** [pose_iter_160000.caffemodel](http://posefs1.perception.cs.cmu.edu/OpenPose/models/pose/mpi/pose_iter_160000.caffemodel)

**Note:** The `.caffemodel` file is approximately **200 MB**. Download may take a few minutes.

---

### 2. Place Files in the Correct Directory

After downloading, place the files in the following directory structure:

```
ColliderAudioEngine/
└── build/
    └── preset_creator/
        └── Debug/  (or Release/)
            └── assets/
                └── openpose_models/
                    └── pose/
                        └── mpi/
                            ├── pose_deploy_linevec_faster_4_stages.prototxt
                            └── pose_iter_160000.caffemodel
```

**If the `assets` folder doesn't exist yet**, create the full directory structure manually:

```bash
cd build/preset_creator/Debug  # or Release
mkdir -p assets/openpose_models/pose/mpi
```

Then move your downloaded files there.

---

## Usage

### Basic Patch Setup

1. **Add a Webcam Loader or Video File Loader**
   - Right-click → Computer Vision → Webcam Loader (or Video File Loader)
   - Select your camera or video file

2. **Add the Pose Estimator**
   - Right-click → Computer Vision → Pose Estimator
   - Connect the **Source ID** output from the webcam/video loader to the **Source In** input of the Pose Estimator

3. **Use the Keypoint Outputs**
   - The Pose Estimator has **30 output pins** (15 keypoints × 2 coordinates)
   - Each keypoint outputs normalized X and Y positions (0.0 - 1.0)
   - Connect these to modulation inputs on any module

### Example: Control an Oscillator with Your Hand

```
Webcam Loader → Pose Estimator
                   ├─ R Wrist X → VCO Frequency
                   └─ R Wrist Y → VCF Cutoff
```

As you move your right hand, the wrist position will control the pitch and filter cutoff!

---

## Module Parameters

### Confidence Threshold
- **Range:** 0.0 - 1.0 (default: 0.1)
- **Purpose:** Filters out weak keypoint detections
- **Lower values** = more sensitive (may produce false positives)
- **Higher values** = more strict (may miss some keypoints)

### Draw Skeleton
- **Toggle:** On/Off (default: On)
- **Purpose:** Overlays the detected skeleton on the video preview
- **Tip:** Turn off for a cleaner preview or if you only care about the CV outputs

### Zoom
- **Buttons:** + / -
- **Purpose:** Doubles the node width for a larger video preview

---

## Keypoint Reference

The 15 MPI keypoints are:

| Index | Keypoint Name | Description |
|-------|---------------|-------------|
| 0     | Head          | Top of head |
| 1     | Neck          | Base of neck |
| 2     | R Shoulder    | Right shoulder |
| 3     | R Elbow       | Right elbow |
| 4     | R Wrist       | Right wrist |
| 5     | L Shoulder    | Left shoulder |
| 6     | L Elbow       | Left elbow |
| 7     | L Wrist       | Left wrist |
| 8     | R Hip         | Right hip |
| 9     | R Knee        | Right knee |
| 10    | R Ankle       | Right ankle |
| 11    | L Hip         | Left hip |
| 12    | L Knee        | Left knee |
| 13    | L Ankle       | Left ankle |
| 14    | Chest         | Center of torso |

---

## Performance Tips

1. **Frame Rate:**
   - The module runs at ~15 FPS by default (pose estimation is computationally expensive)
   - This is ideal for modulation and doesn't strain the audio thread

2. **Lighting:**
   - Good lighting significantly improves detection accuracy
   - Avoid backlighting (standing in front of a bright window)

3. **Background:**
   - Simple backgrounds work best
   - Cluttered backgrounds may produce noise in the detection

4. **Distance:**
   - Stand 1-3 meters from the camera for optimal results
   - Full body should be visible for all keypoints to be detected

---

## Troubleshooting

### "Model: NOT LOADED" appears in the node

**Solution:**
- Check that the model files are in the correct location
- Verify the filenames match exactly (case-sensitive on Linux/Mac)
- Check the console log for the exact path the module is looking for

### No keypoints detected (all values = 0)

**Possible causes:**
1. No video source connected to the "Source In" pin
2. Camera/video file not working
3. Person not visible or too far from camera
4. Poor lighting
5. Confidence threshold set too high

**Solutions:**
- Verify the Source ID cable is connected
- Check the Webcam/Video File Loader is working
- Lower the confidence threshold
- Improve lighting conditions

### Performance is slow / lag in UI

**Solutions:**
- Close other demanding applications
- Reduce the resolution of the webcam (in the Webcam Loader settings)
- Use the "faster" MPI model (already selected by default)

---

## Advanced Usage Ideas

### Gesture-Based Sequencing
Connect keypoint positions to a Step Sequencer's clock input to create body-driven rhythms.

### Dance-Controlled Synthesis
Use the Chest Y position to control global mix levels, and limb positions to control individual oscillator frequencies.

### Installation Art
Track multiple people using separate Pose Estimator nodes (one per webcam) and create spatially-aware soundscapes.

### Accessibility
Map simple gestures (raising hand, bending knee) to trigger samples or presets for hands-free performance.

---

## Technical Details

- **Model:** OpenPose MPI (15 keypoints)
- **Backend:** OpenCV DNN module with Caffe
- **Input Resolution:** 368x368 (resized internally)
- **Processing:** Separate thread, does not block audio
- **Output Range:** 0.0 - 1.0 (normalized to typical video resolution)
- **Thread Safety:** Lock-free FIFO for passing data to audio thread

---

## Credits

This module uses the [OpenPose](https://github.com/CMU-Perceptual-Computing-Lab/openpose) model developed by the Perceptual Computing Lab at Carnegie Mellon University.

**Citation:**
```
@inproceedings{cao2017realtime,
  author = {Zhe Cao and Tomas Simon and Shih-En Wei and Yaser Sheikh},
  booktitle = {CVPR},
  title = {Realtime Multi-Person 2D Pose Estimation using Part Affinity Fields},
  year = {2017}
}
```

---

## License Note

The OpenPose models are available for **non-commercial use only**. If you plan to use this in a commercial product, please review the [OpenPose License](https://github.com/CMU-Perceptual-Computing-Lab/openpose/blob/master/LICENSE) and contact CMU if necessary.

---

**Enjoy creating with body-driven synthesis!**

