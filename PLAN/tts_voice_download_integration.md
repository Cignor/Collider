# TTS Voice Download Integration Plan

## Overview

Implement a native C++/JUCE voice download system as a **global application-level service**, accessible via **Settings > Download Piper Voices...** menu. This allows users to manage Piper TTS voices for all TTS Performer nodes from a centralized location. This will replace reliance on external PowerShell scripts and provide seamless integration with the existing application architecture.

## Objectives

1. Create a native C++ voice download system using JUCE's networking capabilities
2. Add "Download Piper Voices..." menu item in **Settings menu** (top menu bar)
3. Create a standalone modal window/dialog for voice management (similar to Audio Settings dialog)
4. Display available voices with download status (installed/missing)
5. Allow users to download individual voices or batch downloads
6. Show real-time download progress and handle errors gracefully
7. Maintain thread safety and avoid blocking the audio thread
8. Distribute application with 3-4 default voices, allowing on-demand expansion
9. **Keep TTS Performer node focused on audio processing** (no download UI in node)

---

## Risk Assessment

### Overall Risk Rating: **MEDIUM** (5/10) ⬇️ **REDUCED from previous assessment**

**Reasoning:**
- **Better Architecture**: Separating download feature from TTS node reduces complexity
- **Less Module Risk**: TTS Performer module remains unchanged - only status checking added
- **Isolated Implementation**: Download system can be implemented and tested independently
- **Existing Patterns**: Settings menu and modal dialogs already exist in codebase
- **Still Complex**: Networking, threading, and file I/O remain challenging but more manageable

### Risk Factors

1. **Audio Thread Safety** (HIGH RISK)
   - Download operations must NEVER block audio processing
   - Thread synchronization between download thread and UI updates
   - Memory allocation during downloads must not cause audio glitches

2. **File System Conflicts** (MEDIUM RISK)
   - Simultaneous downloads/reads could corrupt files
   - Need proper file locking or atomic writes
   - Directory structure creation race conditions

3. **Network Reliability** (MEDIUM RISK)
   - Downloads may fail due to network issues
   - HuggingFace CDN may be slow or unavailable
   - Large files (10-50MB per voice) require resumable downloads or clear error messages

4. **UI Thread Blocking** (LOW-MEDIUM RISK)
   - Progress updates must not freeze UI
   - Modal dialogs must remain responsive
   - Need proper async progress reporting

5. **Backward Compatibility** (LOW RISK)
   - Existing voice detection and loading logic should remain unchanged
   - New download system should be additive, not modify existing paths

---

## Difficulty Levels

### Level 1: Basic Implementation (EASIEST)
- Simple download of single voice with basic progress bar
- No cancellation, no batch downloads
- Basic error messages
- **Estimated Time**: 8-12 hours

### Level 2: Standard Implementation (RECOMMENDED)
- Single and batch downloads
- Cancellation support
- Detailed progress (percentage, speed, ETA)
- Error handling with retry logic
- Voice status checking (installed/missing)
- **Estimated Time**: 16-24 hours

### Level 3: Advanced Implementation (MOST COMPLETE)
- Resume interrupted downloads
- Parallel downloads with queue management
- Background downloads (continue after modal closes)
- Download verification (checksums)
- Automatic voice updates/version checking
- Voice size estimation before download
- **Estimated Time**: 32-48 hours

---

## Implementation Strategy

### Recommended Approach: **Level 2 (Standard Implementation)**

**Rationale:**
- Provides essential features without excessive complexity
- Balances user experience with development time
- Can be extended to Level 3 later if needed
- Minimizes risk of breaking existing functionality

---

## Technical Architecture

### Core Components

#### 1. Voice Download Thread (`VoiceDownloadThread`)
- **Location**: New class within `TTSPerformerModuleProcessor.h/cpp`
- **Pattern**: Similar to existing `SynthesisThread` pattern
- **Responsibility**: Handle all network I/O off the audio thread
- **Key Features**:
  - Download one or multiple voices sequentially
  - Report progress via thread-safe atomic variables
  - Handle cancellation via `threadShouldExit()`
  - Retry failed downloads (configurable attempts)

#### 2. Voice Manifest System (`VoiceManifest`)
- **Location**: New helper class or static data structure
- **Responsibility**: Store list of available voices with metadata
- **Structure**:
  ```cpp
  struct VoiceEntry {
      juce::String name;           // e.g., "en_US-lessac-medium"
      juce::String language;       // e.g., "English (US)"
      juce::String accent;         // e.g., "General American"
      juce::String gender;         // e.g., "Female"
      juce::String quality;        // e.g., "Medium"
      juce::String onnxPath;       // Relative path in piper-voices/
      bool isIncluded;             // Is this one of the 3-4 default voices?
  };
  ```

#### 3. Voice Download Manager (`VoiceDownloadManager`)
- **Location**: Member of `TTSPerformerModuleProcessor`
- **Responsibility**: Coordinate downloads and status checking
- **Key Methods**:
  - `checkVoiceStatus()` - Scan models directory
  - `startDownload(const juce::String& voiceName)`
  - `startBatchDownload(const std::vector<juce::String>& voices)`
  - `cancelDownload()`
  - `getProgress()` - Return download progress (0.0-1.0)
  - `getCurrentVoice()` - Currently downloading voice name
  - `getStatusMessage()` - Human-readable status

#### 4. UI Integration (`VoiceDownloadDialog` Component)
- **Location**: New standalone component class (similar to `HelpManagerComponent` or audio settings dialog)
- **Pattern**: ImGui modal window opened from Settings menu
- **Access Point**: `Settings > Download Piper Voices...` menu item
- **UI Elements**:
  - Modal window with voice list (grouped by language)
  - Checkboxes/buttons for each voice
  - Progress bar for active downloads
  - Status messages (installed/missing/downloading)
  - Batch download buttons
  - Cancel button during downloads
  - Voice metadata display (language, gender, quality, size)

---

## Detailed Implementation Steps

### Step 1: Create Voice Manifest Data Structure

**File**: `juce/Source/audio/modules/TTSPerformerModuleProcessor.h`

**Changes**:
- Add `VoiceEntry` struct (public or private)
- Add static method `getAvailableVoices()` returning `std::vector<VoiceEntry>`
- Include the 40+ voices from PowerShell script
- Mark 3-4 voices as `isIncluded = true` (default distribution voices)

**Voice Selection for Distribution**:
- `en_US-lessac-medium` (English, Female, Medium quality - good default)
- `en_US-libritts-medium` (English, Male, Medium quality - alternative)
- `de_DE-thorsten-medium` (German, Male, Medium quality - international support)
- `fr_FR-siwis-medium` (French, Male, Medium quality - international support)

**Estimated Complexity**: LOW
**Risk**: LOW
**Time**: 1-2 hours

---

### Step 2: Implement Voice Status Checking

**Files**: 
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.h`
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`

**New Methods**:
```cpp
enum class VoiceStatus { NotInstalled, Installed, Downloading, Error };

VoiceStatus checkVoiceStatus(const juce::String& voiceName) const;
std::map<juce::String, VoiceStatus> checkAllVoiceStatuses() const;
```

**Implementation Logic**:
1. Resolve voice path: `resolveModelsBaseDir().getChildFile("piper-voices/.../...onnx")`
2. Check if `.onnx` and `.onnx.json` files exist
3. Verify files are not empty (basic sanity check)
4. Return appropriate status

**Integration Points**:
- Uses existing `resolveModelsBaseDir()` method
- Compatible with existing `refreshModelChoices()` logic

**Estimated Complexity**: LOW-MEDIUM
**Risk**: LOW (read-only operation)
**Time**: 2-3 hours

---

### Step 3: Create Voice Download Thread

**Files**:
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.h`
- `juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`

**New Class** (similar to `SynthesisThread`):
```cpp
class VoiceDownloadThread : public juce::Thread
{
public:
    VoiceDownloadThread(TTSPerformerModuleProcessor& owner);
    ~VoiceDownloadThread() override;
    
    void run() override;
    void downloadVoice(const juce::String& voiceName);
    void downloadBatch(const std::vector<juce::String>& voices);
    void cancelCurrentDownload();
    
    // Thread-safe progress reporting
    float getProgress() const { return progress.load(); }
    juce::String getCurrentVoice() const;
    juce::String getStatusMessage() const;
    
private:
    TTSPerformerModuleProcessor& owner;
    std::atomic<float> progress{0.0f};
    std::atomic<bool> shouldCancel{false};
    juce::String currentVoice;
    juce::String statusMessage;
    juce::CriticalSection statusLock;
    
    bool downloadSingleVoice(const juce::String& voiceName);
    juce::URL buildVoiceUrl(const juce::String& voiceName, bool isOnnx);
};
```

**Download Implementation** (using JUCE):
```cpp
bool VoiceDownloadThread::downloadSingleVoice(const juce::String& voiceName)
{
    // Build URLs
    juce::URL onnxUrl = buildVoiceUrl(voiceName, true);
    juce::URL jsonUrl = buildVoiceUrl(voiceName, false);
    
    // Determine save paths
    juce::File modelsDir = owner.resolveModelsBaseDir();
    juce::File targetDir = modelsDir.getChildFile("piper-voices/...");
    targetDir.createDirectory();
    
    juce::File onnxFile = targetDir.getChildFile(voiceName + ".onnx");
    juce::File jsonFile = targetDir.getChildFile(voiceName + ".onnx.json");
    
    // Download ONNX file with progress
    {
        std::unique_ptr<juce::WebInputStream> stream(onnxUrl.createInputStream(false));
        if (!stream) return false;
        
        juce::FileOutputStream out(onnxFile);
        if (!out.openedOk()) return false;
        
        char buffer[8192];
        int64 totalBytes = stream->getTotalLength();
        int64 downloaded = 0;
        
        while (!stream->isExhausted() && !shouldCancel.load())
        {
            int bytesRead = stream->read(buffer, sizeof(buffer));
            if (bytesRead <= 0) break;
            
            out.write(buffer, bytesRead);
            downloaded += bytesRead;
            
            // Update progress (0.0 = ONNX at 0%, 0.5 = ONNX at 100%)
            if (totalBytes > 0)
                progress.store((float)downloaded / (float)totalBytes * 0.5f);
        }
    }
    
    // Download JSON file (similar logic)
    // Progress from 0.5 to 1.0
    
    return !shouldCancel.load();
}
```

**Key Considerations**:
- Use `juce::WebInputStream` for HTTP downloads
- Report progress atomically for UI updates
- Handle cancellation gracefully
- Retry logic for transient failures (3 attempts with exponential backoff)
- Verify downloaded files are not empty

**Error Handling**:
- Network errors: Log and report to UI
- Disk errors: Check available space, permissions
- Interrupted downloads: Delete partial files
- Invalid URLs: Skip and continue with next voice

**Estimated Complexity**: HIGH
**Risk**: MEDIUM-HIGH
**Time**: 6-10 hours

---

### Step 4: Create Application-Level Download Manager

**Files**:
- `juce/Source/audio/voices/VoiceDownloadManager.h` (or in VoiceDownloadDialog.h)
- `juce/Source/audio/voices/VoiceDownloadManager.cpp` (or in VoiceDownloadDialog.cpp)

**Architecture**: Application-level singleton or member of `VoiceDownloadDialog`

**Implementation Options**:

**Option A: Singleton Pattern** (Recommended)
```cpp
class VoiceDownloadManager : public juce::DeletedAtShutdown
{
public:
    static VoiceDownloadManager& getInstance()
    {
        static VoiceDownloadManager instance;
        return instance;
    }
    
    VoiceDownloadThread downloadThread;
    // ... download methods ...
    
private:
    VoiceDownloadManager() { downloadThread.startThread(); }
    ~VoiceDownloadManager() { downloadThread.stopThread(5000); }
};
```

**Option B: Member of Dialog**
```cpp
class VoiceDownloadDialog
{
    VoiceDownloadThread downloadThread;
    // Initialize in constructor, cleanup in destructor
};
```

**Recommendation**: Start with Option B (simpler), refactor to singleton later if needed from multiple places.

**Thread Lifecycle**:
- Start thread in VoiceDownloadDialog constructor
- Thread waits for download requests via FIFO or condition variable
- Clean shutdown in destructor

**Estimated Complexity**: MEDIUM
**Risk**: LOW-MEDIUM (thread management)
**Time**: 2-3 hours

---

### Step 5: Create Settings Menu Integration

**File**: `juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`

**Location**: In Settings menu section (around line 1265, after "Help Manager..." menu item)

**Changes**:
```cpp
// In Settings menu
if (ImGui::BeginMenu("Settings"))
{
    // ... existing menu items ...
    
    if (ImGui::MenuItem("Help Manager..."))
    {
        m_helpManager.open();
        m_helpManager.setActiveTab(0);
    }
    
    // NEW: Add voice download menu item
    if (ImGui::MenuItem("Download Piper Voices..."))
    {
        showVoiceDownloadDialog = !showVoiceDownloadDialog;
    }
    
    ImGui::Separator();
    // ... rest of menu ...
}
```

**Add Member Variable** (in `ImGuiNodeEditorComponent.h`):
```cpp
bool showVoiceDownloadDialog { false };
```

---

### Step 6: Create Voice Download Dialog Component

**Files**:
- `juce/Source/preset_creator/VoiceDownloadDialog.h` (new file)
- `juce/Source/preset_creator/VoiceDownloadDialog.cpp` (new file)

**New Component Class**:
```cpp
class VoiceDownloadDialog
{
public:
    VoiceDownloadDialog();
    ~VoiceDownloadDialog();
    
    void open() { isOpen = true; }
    void close() { isOpen = false; }
    bool isOpen { false };
    
    void render(); // Called from ImGuiNodeEditorComponent
    
private:
    VoiceDownloadManager downloadManager; // Application-level singleton
    // UI state variables...
};
```

**Integration Point** (in `ImGuiNodeEditorComponent::renderFrame()`):
```cpp
// In renderFrame() method, after main menu bar
if (showVoiceDownloadDialog)
{
    voiceDownloadDialog.render();
}
```

**UI Implementation**:
```cpp
void VoiceDownloadDialog::render()
{
    if (!isOpen) return;
    
    // Modal window
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Download Piper Voices", &isOpen, 
                     ImGuiWindowFlags_AlwaysAutoResize))
    {
        // Voice list with checkboxes
        ImGui::Text("Available Voices:");
        ImGui::Separator();
        
        auto voices = getAvailableVoices();
        auto statuses = checkAllVoiceStatuses();
        
        for (const auto& voice : voices)
        {
            auto status = statuses[voice.name];
            bool isInstalled = (status == VoiceStatus::Installed);
            bool isDownloading = (status == VoiceStatus::Downloading);
            
            // Checkbox or button based on status
            if (isInstalled)
            {
                ImGui::Checkbox(voice.name.toRawUTF8(), &isInstalled);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "✓ Installed");
            }
            else if (isDownloading)
            {
                ImGui::ProgressBar(voiceDownloadThread.getProgress());
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    voiceDownloadThread.cancelCurrentDownload();
            }
            else
            {
                if (ImGui::Button(("Download##" + voice.name).toRawUTF8()))
                    voiceDownloadThread.downloadVoice(voice.name);
            }
            
            // Voice metadata (tooltip or inline)
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("Language: %s", voice.language.toRawUTF8());
                ImGui::Text("Gender: %s", voice.gender.toRawUTF8());
                ImGui::Text("Quality: %s", voice.quality.toRawUTF8());
                ImGui::EndTooltip();
            }
        }
        
        // Batch download button
        if (ImGui::Button("Download Missing Voices"))
        {
            std::vector<juce::String> missing;
            for (const auto& [name, status] : statuses)
            {
                if (status == VoiceStatus::NotInstalled)
                    missing.push_back(name);
            }
            if (!missing.empty())
                voiceDownloadThread.downloadBatch(missing);
        }
        
        // Close button
        if (ImGui::Button("Close", ImVec2(120, 0)))
        {
            showVoiceManagerModal = false;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
}
```

**UI State Variables** (add to header):
```cpp
bool showVoiceManagerModal { false };
```

**Integration Point**:
- Add "Manage Voices..." button in `drawParametersInNode()` near model selection dropdowns
- Call `drawVoiceManager(itemWidth)` after model selection UI

**Estimated Complexity**: MEDIUM
**Risk**: LOW
**Time**: 4-6 hours

---

### Step 7: Progress Reporting and Status Updates

**Implementation Details**:

**Thread-Safe Progress Updates**:
- Use `std::atomic<float>` for progress (0.0-1.0)
- Update progress in download thread after each buffer read
- UI reads progress in `drawParametersInNode()` (called on UI thread)

**Status Message Updates**:
- Use `juce::CriticalSection` to protect status message string
- Update message at key points: "Connecting...", "Downloading...", "Saving...", "Complete"
- Include voice name and file being downloaded

**UI Refresh**:
- ImGui will automatically refresh on next frame
- Progress bar updates smoothly
- Status text updates in real-time

**Estimated Complexity**: LOW-MEDIUM
**Risk**: LOW
**Time**: 2-3 hours

---

### Step 8: Error Handling and User Feedback

**Error Categories**:

1. **Network Errors**
   - Connection timeout
   - DNS resolution failure
   - HTTP errors (404, 500, etc.)
   - Partial download (connection lost)

2. **File System Errors**
   - Insufficient disk space
   - Permission denied
   - Invalid path
   - File already in use

3. **Data Integrity Errors**
   - Empty file downloaded
   - Corrupted download
   - Invalid file format

**Error Reporting**:
- Display error message in UI modal
- Log detailed error to `juce::Logger`
- Allow retry for failed downloads
- Show which voices failed in batch operations

**User Feedback**:
```cpp
// In download thread
if (downloadFailed)
{
    const juce::ScopedLock lock(statusLock);
    statusMessage = "Error: " + errorDescription;
    progress.store(-1.0f); // Negative = error state
}

// In UI
if (progress < 0.0f)
{
    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", 
                       voiceDownloadThread.getStatusMessage().toRawUTF8());
    if (ImGui::Button("Retry"))
        voiceDownloadThread.downloadVoice(currentVoice);
}
```

**Estimated Complexity**: MEDIUM
**Risk**: MEDIUM (user experience depends on good error handling)
**Time**: 3-4 hours

---

### Step 9: Testing and Refinement

**Test Scenarios**:

1. **Single Voice Download**
   - [ ] Download succeeds
   - [ ] Progress updates smoothly
   - [ ] Voice appears in model selection after download
   - [ ] Can use downloaded voice immediately

2. **Batch Download**
   - [ ] Multiple voices download sequentially
   - [ ] Progress resets for each voice
   - [ ] Can cancel during batch
   - [ ] Failed voices don't block others

3. **Cancellation**
   - [ ] Cancel stops download immediately
   - [ ] Partial files are cleaned up
   - [ ] Status updates correctly
   - [ ] Can restart download after cancel

4. **Error Handling**
   - [ ] Network failure displays error
   - [ ] Retry works correctly
   - [ ] Disk full error is handled
   - [ ] Invalid voice name is rejected

5. **Edge Cases**
   - [ ] Download same voice twice (skip if exists)
   - [ ] Download while synthesis is running
   - [ ] Close modal during download
   - [ ] Application exit during download

6. **Performance**
   - [ ] No audio glitches during download
   - [ ] UI remains responsive
   - [ ] Memory usage is reasonable
   - [ ] No thread deadlocks

**Estimated Complexity**: HIGH (thorough testing)
**Risk**: LOW (if testing is comprehensive)
**Time**: 6-8 hours

---

## Files to Create

1. **`juce/Source/preset_creator/VoiceDownloadDialog.h`** - Dialog component header
2. **`juce/Source/preset_creator/VoiceDownloadDialog.cpp`** - Dialog component implementation
3. **`juce/Source/audio/voices/VoiceDownloadManager.h`** - Download manager header (optional, could be in dialog)
4. **`juce/Source/audio/voices/VoiceDownloadManager.cpp`** - Download manager implementation (optional)

**Note**: Download thread can be part of `VoiceDownloadManager` class or separate. Recommend starting with everything in `VoiceDownloadDialog` for simplicity, extract later if needed.

---

## Files to Modify

1. **`juce/Source/preset_creator/ImGuiNodeEditorComponent.h`**
   - Add `VoiceDownloadDialog` member variable
   - Add `showVoiceDownloadDialog` boolean flag

2. **`juce/Source/preset_creator/ImGuiNodeEditorComponent.cpp`**
   - Add "Download Piper Voices..." menu item in Settings menu
   - Initialize `VoiceDownloadDialog` in constructor
   - Call `voiceDownloadDialog.render()` in render loop
   - Handle dialog open/close state

3. **`juce/Source/audio/modules/TTSPerformerModuleProcessor.h`** (MINIMAL CHANGES)
   - Add `VoiceStatus` enum (for status checking only)
   - Add helper method `checkVoiceStatus()` - **read-only, no download code**

4. **`juce/Source/audio/modules/TTSPerformerModuleProcessor.cpp`** (MINIMAL CHANGES)
   - Implement `checkVoiceStatus()` helper method
   - **NO download thread, NO UI code in this file**
   - Keep existing functionality completely untouched

---

## Potential Problems and Solutions

### Problem 1: Network Downloads Block Audio Thread
**Solution**: All downloads run in separate `VoiceDownloadThread`, never touch audio thread.

### Problem 2: Simultaneous File Access Conflicts
**Solution**: 
- Downloads write to temporary files, then atomically rename
- Or use file locking (platform-specific, complex)
- Or serialize all file operations through single thread

### Problem 3: Large Files Cause Memory Issues
**Solution**: 
- Stream downloads directly to disk (already planned)
- Use fixed-size buffers (8KB chunks)
- Never load entire file into memory

### Problem 4: Partial Downloads Corrupt Voice Database
**Solution**:
- Download to temp file first
- Verify file integrity (check file size matches expected)
- Only move to final location after successful download
- Delete temp files on failure

### Problem 5: UI Modal Blocks User from Other Operations
**Solution**:
- Make modal non-blocking (ImGui popup modal)
- Downloads continue in background even if modal closed
- Add status indicator in main parameter panel

### Problem 6: Network Timeouts on Slow Connections
**Solution**:
- Set reasonable timeout (60 seconds per file)
- Implement retry with exponential backoff
- Show clear error message with retry button

### Problem 7: HuggingFace CDN Rate Limiting
**Solution**:
- Add delays between downloads (1-2 seconds)
- Respect server response headers if provided
- Limit concurrent downloads (already sequential in design)

### Problem 8: Cross-Platform Path Issues
**Solution**:
- Use `juce::File` for all path operations (JUCE handles platform differences)
- Test on Windows, macOS, Linux
- Handle case-sensitive filesystems on Linux/macOS

### Problem 9: Existing Voice Loading Logic Breaks
**Solution**:
- Keep existing `resolveModelsBaseDir()` and `refreshModelChoices()` unchanged
- New downloads use same directory structure
- Add downloaded voices to `modelEntries` after download completes
- Call `refreshModelChoices()` after successful download

### Problem 10: Thread Synchronization Issues
**Solution**:
- Use `juce::CriticalSection` for shared state
- Use `std::atomic` for progress values
- Follow existing `SynthesisThread` pattern exactly
- Avoid locks in audio thread (download thread only)

---

## Confidence Rating

### Overall Confidence: **HIGH** (8/10)

### Strong Points

1. **Clear Pattern to Follow**
   - Existing `SynthesisThread` provides excellent template
   - Same threading model, same synchronization patterns
   - Proven to work in this codebase

2. **JUCE Provides Solid Foundation**
   - `juce::URL` and `juce::WebInputStream` handle networking
   - `juce::File` handles cross-platform file operations
   - `juce::Thread` provides robust threading primitives

3. **Additive Implementation**
   - Doesn't modify existing voice loading logic
   - New functionality is separate from existing code
   - Easy to disable/remove if issues arise

4. **Incremental Development Possible**
   - Can implement Level 1 first, test, then add features
   - Each step is relatively independent
   - Can test download thread separately from UI

5. **Existing UI Patterns**
   - ImGui modal dialogs already used (rename/delete clip)
   - Progress bars and status messages are straightforward
   - Follows established code style

### Weak Points

1. **Limited JUCE Download Experience**
   - Haven't seen JUCE download code in this codebase
   - May need to research `WebInputStream` API details
   - Progress reporting might need custom implementation

2. **Network Reliability Unknown**
   - HuggingFace CDN stability untested
   - Large file downloads (10-50MB) may have issues
   - No experience with their rate limiting

3. **Complex Module to Modify**
   - TTS Performer is already very complex (3294 lines)
   - Many interdependencies
   - Risk of introducing subtle bugs

4. **Testing Challenges**
   - Need to test network scenarios (slow, failed, interrupted)
   - Hard to simulate all error conditions
   - May need mock HTTP server for testing

5. **Platform-Specific Concerns**
   - File permissions on different platforms
   - Network stack differences
   - Path separators and case sensitivity

---

## Mitigation Strategies

1. **Start Simple**: Implement Level 1 first, validate, then add features
2. **Extensive Logging**: Log every download step for debugging
3. **Unit Testing**: Test download thread separately before UI integration
4. **Error Handling First**: Implement robust error handling from the start
5. **User Testing**: Get early feedback on UI/UX before polishing

---

## Success Criteria

### Minimum Viable Product (MVP)
- [ ] User can open "Manage Voices" modal
- [ ] Voice list displays with installed/missing status
- [ ] User can download single voice
- [ ] Progress bar shows download progress
- [ ] Downloaded voice appears in model selection
- [ ] Basic error messages on failure

### Full Implementation (Level 2)
- [ ] All MVP features working
- [ ] Batch download functionality
- [ ] Cancellation support
- [ ] Detailed progress (percentage, speed)
- [ ] Retry failed downloads
- [ ] Voice metadata displayed (language, gender, quality)
- [ ] Download continues in background if modal closed

### Polish (Future Enhancements)
- [ ] Resume interrupted downloads
- [ ] Download verification (checksums)
- [ ] Automatic updates notification
- [ ] Download queue management
- [ ] Voice preview/audio samples

---

## Implementation Timeline

### Phase 1: Foundation (4-6 hours)
- Step 1: Voice manifest data structure
- Step 2: Voice status checking (in TTS module, minimal changes)
- **Deliverable**: Can check which voices are installed

### Phase 2: Download Engine (8-12 hours)
- Step 3: Voice download thread
- Step 4: Application-level download manager
- **Deliverable**: Can download voices programmatically

### Phase 3: Settings Menu Integration (2-3 hours)
- Step 5: Add menu item to Settings menu
- **Deliverable**: Menu item opens dialog (dialog empty initially)

### Phase 4: UI Dialog (6-8 hours)
- Step 6: Create VoiceDownloadDialog component
- Step 7: Progress reporting integration
- **Deliverable**: User can download voices via Settings menu

### Phase 5: Error Handling (4-6 hours)
- Step 8: Error handling and feedback
- **Deliverable**: Robust error handling and user feedback

### Phase 6: Testing (6-8 hours)
- Step 9: Testing and refinement
- **Deliverable**: Fully tested, production-ready feature

### Total Estimated Time: **30-43 hours** (3.75-5.4 working days)

**Note**: Slightly longer due to separate dialog component, but much cleaner architecture with lower risk to TTS module.

---

## Alternative Approaches Considered

### Alternative 1: Keep PowerShell Script, Launch from C++
**Pros**: Reuse existing script, faster initial implementation  
**Cons**: External dependency, execution policy issues, harder to integrate progress  
**Verdict**: REJECTED - Native implementation is better long-term

### Alternative 2: Embed Python Script for Downloads
**Pros**: Easy HTTP requests, good library support  
**Cons**: Python dependency, complex integration, performance overhead  
**Verdict**: REJECTED - JUCE native is cleaner

### Alternative 3: Separate Downloader Application
**Pros**: Isolated, can update independently  
**Cons**: Poor user experience, complex distribution  
**Verdict**: REJECTED - Integrated solution is better UX

---

## Future Enhancements (Post-Implementation)

1. **Resume Downloads**: Save progress and resume after interruption
2. **Parallel Downloads**: Download multiple voices simultaneously (with rate limiting)
3. **Voice Updates**: Check for updated voice models and offer updates
4. **Voice Previews**: Play audio samples before downloading
5. **Custom Voice Sources**: Allow users to add custom voice repositories
6. **Download Scheduling**: Schedule downloads for off-peak hours
7. **Bandwidth Management**: Limit download speed to avoid impacting other apps

---

## Architecture Benefits of Settings Menu Approach

### ✅ Advantages

1. **Separation of Concerns**
   - TTS Performer node stays focused on audio processing
   - Voice management is a system-level feature, not node-specific

2. **Single Point of Access**
   - All TTS nodes share the same voices
   - One place to manage voices for entire application
   - No duplication across multiple TTS nodes

3. **Reduced Complexity in TTS Module**
   - TTS Performer already complex (3294 lines)
   - Download UI removed from node = cleaner code
   - Easier to maintain and test

4. **Better UX**
   - Settings menu is expected location for system configuration
   - Consistent with other global features (Audio Settings, MIDI Manager)
   - Cleaner node UI (less cluttered parameter panel)

5. **Multi-Instance Friendly**
   - If user has multiple TTS nodes, configure once in Settings
   - Changes affect all nodes automatically

6. **Independent Testing**
   - Dialog component can be tested separately
   - Download system isolated from audio processing
   - Easier to debug and maintain

### ⚠️ Considerations

1. **Slightly Less Convenient**
   - Need to navigate to Settings menu (but this is standard UX pattern)
   - Not immediately visible in node (but status checking still available)

2. **Code Organization**
   - New dialog component needed (but follows existing patterns)
   - Download manager needs to access TTS module's `resolveModelsBaseDir()` method
   - Can use static helper or pass reference to dialog

---

## Notes

- Keep existing PowerShell script as developer tool (not removed)
- Voice manifest should be easily updatable (consider JSON file in future)
- Consider making voice list downloadable from server (future enhancement)
- Test thoroughly on slow network connections
- Document download locations and file structure for users
- Consider adding download size information to UI
- Dialog should refresh voice list after download completes
- Consider showing notification when download completes (using NotificationManager)

---

## Approval Checklist

Before starting implementation, verify:
- [ ] User understands risk level and complexity
- [ ] Recommended Level 2 implementation is approved
- [ ] Timeline and effort estimates are acceptable
- [ ] Testing strategy is adequate
- [ ] Rollback plan is understood (can disable feature via #ifdef)

---

## Revision History

- **2024-01-XX**: Initial plan created
- **Future**: Update as implementation progresses and learnings emerge

