# MIDI File Format Compliance Audit

## 🔴 **CRITICAL FINDINGS**

### **MIDI Logger Export - MISSING TEMPO META EVENT**

The MIDI Logger exports MIDI files **WITHOUT tempo information**! This causes all exported files to default to 120 BPM in other software.

---

## 📊 **Current Implementation Audit**

### **1. MIDI Logger Export (`MidiLoggerModuleProcessor::exportToMidiFile()`)**

#### ✅ **What it DOES write:**
- ✅ PPQ (Pulses Per Quarter Note): 960 ticks
- ✅ Note On/Off events with correct timing
- ✅ Note pitch (0-127)
- ✅ Note velocity (0-127)
- ✅ Multiple tracks

#### ❌ **What it DOESN'T write:**
- ❌ **Tempo Meta Event** (Critical!)
- ❌ Track names
- ❌ Time signature
- ❌ Key signature
- ❌ Copyright/text events
- ❌ Instrument names (Program Change)
- ❌ Pan/Volume (Controller events)

#### **Current Code (Lines 651-676):**
```cpp
juce::MidiFile midiFile;
midiFile.setTicksPerQuarterNote(960);

// Iterate through tracks and add notes
for (const auto& track : tracks)
{
    juce::MidiMessageSequence sequence;
    
    // Add note events
    for (const auto& ev : events)
    {
        sequence.addEvent(juce::MidiMessage::noteOn(...), tick);
        sequence.addEvent(juce::MidiMessage::noteOff(...), tick);
    }
    
    sequence.updateMatchedPairs();
    midiFile.addTrack(sequence);
}

midiFile.writeTo(stream);  // NO TEMPO WRITTEN!
```

---

### **2. MIDI Player Import (`MIDIPlayerModuleProcessor::loadMIDIFile()`)**

#### ✅ **What it DOES read:**
- ✅ Tempo from first track (lines 587-606)
- ✅ Note On/Off events
- ✅ Note pitch, velocity, timing
- ✅ Track count
- ✅ PPQ (Ticks Per Quarter Note)

#### ❌ **What it DOESN'T read:**
- ❌ Time signature changes
- ❌ Tempo changes mid-file
- ❌ Track names (for display)
- ❌ Key signature
- ❌ Program changes (instruments)
- ❌ Controller events (CC)
- ❌ Text/Copyright meta events

#### **Current Tempo Reading (Lines 589-606):**
```cpp
fileBpm = 120.0; // Default fallback
if (midiFile && midiFile->getNumTracks() > 0)
{
    const auto* firstTrack = midiFile->getTrack(0);
    for (int i = 0; i < firstTrack->getNumEvents(); ++i)
    {
        const auto& event = firstTrack->getEventPointer(i)->message;
        if (event.isTempoMetaEvent())
        {
            fileBpm = 60.0 / event.getTempoSecondsPerQuarterNote();
            break; // Only reads FIRST tempo
        }
    }
}
```

---

## 🎯 **Standard MIDI File Format (Type 1)**

### **Required Meta Events for Professional MIDI Files:**

| Meta Event | Hex | Purpose | Priority |
|------------|-----|---------|----------|
| **Set Tempo** | `FF 51 03` | Microseconds per quarter note | **CRITICAL** |
| Time Signature | `FF 58 04` | Beats per measure, beat value | High |
| Track Name | `FF 03` | Human-readable track name | Medium |
| End of Track | `FF 2F 00` | Marks track end | **CRITICAL** |
| Key Signature | `FF 59 02` | Key and scale | Low |
| Text Event | `FF 01` | Copyright, lyrics, etc | Low |

### **Common Channel Events:**

| Event | Purpose | Priority |
|-------|---------|----------|
| Program Change | Instrument selection (0-127) | Medium |
| Controller #7 | Volume (0-127) | Medium |
| Controller #10 | Pan (0=left, 64=center, 127=right) | Low |
| Controller #64 | Sustain pedal | Low |

---

## 🔧 **THE FIX: Add Tempo Meta Event to Logger Export**

### **Updated `exportToMidiFile()` function:**

```cpp
void MidiLoggerModuleProcessor::exportToMidiFile()
{
    fileChooser = std::make_unique<juce::FileChooser>(...);
    
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        juce::File file = fc.getResult();
        if (file == juce::File{}) return;
        
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(960);
        
        // === ADD THIS: Tempo Track (Track 0) ===
        juce::MidiMessageSequence tempoTrack;
        
        // 1. Add track name
        tempoTrack.addEvent(juce::MidiMessage::textMetaEvent(3, "Tempo Track"), 0.0);
        
        // 2. Add time signature (4/4)
        tempoTrack.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
        
        // 3. **CRITICAL: Add tempo meta event**
        // Convert BPM to microseconds per quarter note
        const double microsecondsPerQuarterNote = 60'000'000.0 / currentBpm;
        tempoTrack.addEvent(
            juce::MidiMessage::tempoMetaEvent(static_cast<int>(microsecondsPerQuarterNote)), 
            0.0
        );
        
        // 4. Add end-of-track marker
        tempoTrack.addEvent(juce::MidiMessage::endOfTrack(), 0.0);
        
        // Add tempo track FIRST (Track 0)
        midiFile.addTrack(tempoTrack);
        // === END TEMPO TRACK ===
        
        // Now add note tracks (Track 1, 2, 3...)
        for (size_t trackIdx = 0; trackIdx < tracks.size(); ++trackIdx)
        {
            if (!tracks[trackIdx]) continue;
            auto events = tracks[trackIdx]->getEventsCopy();
            if (events.empty()) continue;
            
            juce::MidiMessageSequence sequence;
            
            // Add track name
            sequence.addEvent(
                juce::MidiMessage::textMetaEvent(3, tracks[trackIdx]->name.toStdString()), 
                0.0
            );
            
            // Add notes
            for (const auto& ev : events)
            {
                sequence.addEvent(
                    juce::MidiMessage::noteOn(1, ev.pitch, (juce::uint8)(ev.velocity * 127.0f)),
                    samplesToMidiTicks(ev.startTimeInSamples)
                );
                sequence.addEvent(
                    juce::MidiMessage::noteOff(1, ev.pitch),
                    samplesToMidiTicks(ev.startTimeInSamples + ev.durationInSamples)
                );
            }
            
            // Add end-of-track marker
            double lastTick = events.empty() ? 0.0 : 
                samplesToMidiTicks(events.back().startTimeInSamples + events.back().durationInSamples);
            sequence.addEvent(juce::MidiMessage::endOfTrack(), lastTick + 100);
            
            sequence.updateMatchedPairs();
            midiFile.addTrack(sequence);
        }
        
        // Write file
        juce::FileOutputStream stream(file);
        if (stream.openedOk())
        {
            midiFile.writeTo(stream);
            juce::Logger::writeToLog("[MIDI Logger] Exported " + juce::String(midiFile.getNumTracks()) + 
                                    " tracks at " + juce::String(currentBpm, 1) + " BPM to: " + 
                                    file.getFullPathName());
        }
    });
}
```

---

## 📊 **MIDI Player UI: Show More Information**

### **Current Display (Minimal):**
```
Time: 2.34s / 45.67s | Track: 1/8
```

### **Proposed Enhanced Display:**

```
┌────────────────────────────────────────────────────────┐
│ File: "song.mid"                            [Load .mid] │
├────────────────────────────────────────────────────────┤
│ Tempo: 120.0 BPM (from file)                           │
│ Format: Type 1 • PPQ: 480 • Tracks: 8 (5 with notes)  │
│ Duration: 0:45.6 • Time Sig: 4/4 • Key: C Major       │
├────────────────────────────────────────────────────────┤
│ Track: [Show All Tracks ▼]                            │
│ Speed: [━━━●━━━━] 1.0x  (Playback: 120 BPM)          │
│ Zoom:  [━━━●━━━━] 93 px/beat                          │
├────────────────────────────────────────────────────────┤
│ [Quick Connect: → PolyVCO] [→ Samplers] [→ Both]     │
├────────────────────────────────────────────────────────┤
│ ┌──────────────[ Piano Roll ]──────────────┐          │
│ │ [Timeline with notes]                     │          │
│ └───────────────────────────────────────────┘          │
└────────────────────────────────────────────────────────┘
```

### **Information to Add:**

1. **File Info Section:**
   - File name (already shown)
   - ✅ Original file tempo (already read)
   - ⭐ MIDI format type (0, 1, or 2)
   - ⭐ PPQ/Ticks per quarter note
   - ⭐ Total track count vs tracks with notes
   - ⭐ Time signature (if available)
   - ⭐ Key signature (if available)

2. **Playback Info:**
   - ✅ Current time / duration (already shown)
   - ⭐ Effective playback BPM (with multiplier applied)
   - ⭐ Tempo source indicator (File vs Host)
   - ⭐ Current measure:beat position (e.g., "Bar 12, Beat 3")

3. **Track Info (when single track selected):**
   - ⭐ Track name (from meta event)
   - ⭐ Note count
   - ⭐ Note range (lowest to highest)
   - ⭐ Program change (instrument)

---

## 📝 **Implementation Checklist**

### **Phase 1: Fix Logger Export (CRITICAL)**
- [ ] Add tempo track as Track 0
- [ ] Write tempo meta event with `currentBpm`
- [ ] Add track name meta events for each track
- [ ] Add time signature meta event (default 4/4)
- [ ] Add end-of-track markers
- [ ] Test: Export file, load in DAW, verify tempo

### **Phase 2: Enhance Player Import**
- [ ] Read time signature from file
- [ ] Read track names from meta events
- [ ] Store PPQ value for display
- [ ] Read MIDI format type
- [ ] Count tracks vs tracks-with-notes

### **Phase 3: Improve Player UI**
- [ ] Add "File Info" section showing:
  - Format type, PPQ, track counts
  - Time signature, key signature
- [ ] Show effective playback BPM with multiplier
- [ ] Add measure:beat position display
- [ ] Show track name when single track selected
- [ ] Color-code tempo source (File=blue, Host=green)

### **Phase 4: Advanced Features (Future)**
- [ ] Handle tempo changes mid-file
- [ ] Support time signature changes
- [ ] Read/display program changes
- [ ] Export controller events (CC) from Logger
- [ ] Export tempo automation

---

## 🧪 **Testing Protocol**

### **Test 1: Logger → Player Round-Trip**
1. Record CV/Gate in MIDI Logger at 140 BPM
2. Export to .mid file
3. Load exported file in MIDI Player
4. **Expected**: Player shows "140.0 BPM (from file)"
5. **Current**: Shows "120.0 BPM (from file)" ❌

### **Test 2: DAW Compatibility**
1. Export file from Logger
2. Import into Ableton/FL Studio/Reaper
3. **Expected**: DAW shows correct tempo
4. **Current**: DAW defaults to 120 BPM ❌

### **Test 3: Tempo Multiplier with Logger Files**
1. Export file at 120 BPM
2. Load in Player
3. Set multiplier to 2.0x
4. **Expected**: Plays at 240 BPM
5. **Current**: (needs core tempo fix first)

---

## 💡 **Priority Ranking**

| Priority | Task | Impact | Effort |
|----------|------|--------|--------|
| 🔴 **P0** | Add tempo meta event to Logger export | **CRITICAL** | 30 min |
| 🔴 **P0** | Fix Player tempo calculation (use finalBpm) | **CRITICAL** | 10 min |
| 🟡 **P1** | Add File Info section to Player UI | High | 2 hours |
| 🟡 **P1** | Show effective BPM in real-time | High | 30 min |
| 🟢 **P2** | Add track names to export/import | Medium | 1 hour |
| 🟢 **P2** | Add time signature support | Medium | 2 hours |
| 🔵 **P3** | Handle tempo changes mid-file | Low | 4 hours |

---

## 🚀 **Quick Win: 40-Minute Fix**

### **Step 1: Fix Logger Export (30 min)**
Add tempo meta event to `exportToMidiFile()` function.

### **Step 2: Fix Player Tempo (10 min)**
Replace `speed` with `finalBpm / fileBpm` ratio.

**Result**: Professional MIDI file export/import with correct tempo! 🎵

---

## 📚 **JUCE API Reference**

### **Creating Meta Events:**
```cpp
// Tempo (microseconds per quarter note)
juce::MidiMessage::tempoMetaEvent(int microsecondsPerQuarterNote);

// Time Signature (numerator, denominator as power of 2)
juce::MidiMessage::timeSignatureMetaEvent(int numerator, int denominator);

// Track Name
juce::MidiMessage::textMetaEvent(int textType, const juce::String& text);
// textType: 1=text, 2=copyright, 3=track name, 4=instrument, 5=lyric

// End of Track
juce::MidiMessage::endOfTrack();

// Key Signature
juce::MidiMessage::keySignatureMetaEvent(int numberOfSharpsOrFlats, bool isMinor);
```

### **Reading Meta Events:**
```cpp
if (message.isTempoMetaEvent())
{
    double secondsPerQuarterNote = message.getTempoSecondsPerQuarterNote();
    double bpm = 60.0 / secondsPerQuarterNote;
}

if (message.isTimeSignatureMetaEvent())
{
    int numerator, denominator;
    message.getTimeSignatureInfo(numerator, denominator);
}
```

---

## ✅ **Conclusion**

**The MIDI Logger writes valid MIDI files, but they're missing critical tempo information.**  
**The MIDI Player reads tempo correctly, but doesn't display enough information.**

**Fix Priority**: Logger Export > Player Tempo Calculation > UI Enhancements

**Expected Result**: Professional-grade MIDI export/import with full metadata support.

