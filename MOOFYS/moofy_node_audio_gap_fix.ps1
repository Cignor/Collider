#=============================================================
#       MOOFY: Node Creation/Deletion Audio Gap Fix
#       This script archives the CORE files needed to understand
#       and fix the audio gap issue when creating/deleting nodes.
#       Perfect for AI assistants or developers working on this fix.
#=============================================================

#--- Configuration ---
$projectRoot = Split-Path -Parent $PSScriptRoot
$outputFile = Join-Path $projectRoot "MOOFYS\moofy_node_audio_gap_fix.txt"

#--- Essential Files for Audio Gap Problem ---
$sourceFiles = @(
    #=============================================================
    # SECTION 1: PROBLEM ANALYSIS & PLAN
    # Read this first to understand the issue and proposed solutions
    #=============================================================
    "PLAN\node_create_or_delete_audio_gap_problem.md",
    
    #=============================================================
    # SECTION 2: CORE AUDIO GRAPH & SYNCHRONIZATION
    # The heart of the problem - graph modification and locking
    #=============================================================
    "juce\Source\audio\graph\ModularSynthProcessor.h",
    "juce\Source\audio\graph\ModularSynthProcessor.cpp",
    
    #=============================================================
    # SECTION 3: AUDIO PROCESSING THREAD
    # How audio processing interacts with graph changes
    #=============================================================
    "juce\Source\audio\AudioEngine.h",
    "juce\Source\audio\AudioEngine.cpp",
    
    #=============================================================
    # SECTION 4: MODULE CREATION/DELETION FLOW
    # Where nodes are actually added/removed
    #=============================================================
    # Note: These are in ModularSynthProcessor.cpp, already included above
    # But specifically look for:
    # - addModule()
    # - removeModule()
    # - commitChanges()
    # - processBlock()
    
    #=============================================================
    # SECTION 5: BASE MODULE ARCHITECTURE
    # Understanding how modules are structured
    #=============================================================
    "juce\Source\audio\modules\ModuleProcessor.h",
    "juce\Source\audio\modules\ModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 6: EXAMPLE NODES (for reference)
    # Understanding typical module initialization costs
    #=============================================================
    "juce\Source\audio\modules\BestPracticeNodeProcessor.h",
    "juce\Source\audio\modules\BestPracticeNodeProcessor.cpp",
    
    # Simple node example
    "juce\Source\audio\modules\ValueModuleProcessor.h",
    "juce\Source\audio\modules\ValueModuleProcessor.cpp",
    
    # Complex node example (demonstrates expensive initialization)
    "juce\Source\audio\modules\DelayModuleProcessor.h",
    "juce\Source\audio\modules\DelayModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 7: UI INTEGRATION
    # How UI triggers node creation/deletion
    #=============================================================
    "juce\Source\preset_creator\ImGuiNodeEditorComponent.h",
    "juce\Source\preset_creator\ImGuiNodeEditorComponent.cpp",
    
    #=============================================================
    # SECTION 8: TIMING & TRANSPORT (if relevant)
    # Understanding the timing system
    #=============================================================
    "juce\Source\audio\modules\TempoClockModuleProcessor.h",
    "juce\Source\audio\modules\TempoClockModuleProcessor.cpp",
    
    #=============================================================
    # SECTION 9: BUILD SYSTEM (for understanding dependencies)
    #=============================================================
    "juce\CMakeLists.txt"
)

#--- Script Execution ---

if (Test-Path $outputFile) {
    Remove-Item $outputFile
}

Write-Host "Starting Moofy Node Audio Gap Fix script..." -ForegroundColor Cyan
Write-Host "Archiving essential files to: $outputFile"

# Add header with problem summary
$header = @"
================================================================================
MOOFY: Node Creation/Deletion Audio Gap Fix
================================================================================
Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
Purpose: Archive all essential files needed to understand and fix the audio gap
         issue that occurs when creating or deleting nodes during audio playback.

================================================================================
PROBLEM SUMMARY
================================================================================

ISSUE:
Small audio gaps occur when creating or deleting nodes in the modular synth
graph. This manifests as brief audio dropouts or clicks during node operations.

ROOT CAUSE:
The commitChanges() method in ModularSynthProcessor holds moduleLock (CriticalSection)
while executing expensive operations:
1. internalGraph->rebuild() - Reconstructs graph topology
2. internalGraph->prepareToPlay() - Allocates memory, initializes DSP, resets processors

These operations can take several milliseconds and may block the audio thread indirectly
through JUCE's AudioProcessorGraph internal synchronization.

KEY FILES TO STUDY:
- ModularSynthProcessor.cpp:commitChanges() - Lines 1106-1176
- ModularSynthProcessor.cpp:addModule() - Lines 902-978
- ModularSynthProcessor.cpp:removeModule() - Lines 1037-1067
- ModularSynthProcessor.cpp:processBlock() - Lines 216-375
- AudioEngine.cpp:getNextAudioBlock() - Lines 186-220

PROPOSED SOLUTIONS:
1. LEVEL 1 (Quick Wins): Minimize lock duration, move expensive operations outside lock
2. LEVEL 2 (Deferred Updates): Queue graph changes, apply between audio callbacks
3. LEVEL 3 (Incremental Updates): Avoid full rebuild, update graph incrementally
4. LEVEL 4 (Custom Graph): Replace JUCE graph (NOT RECOMMENDED)

See PLAN\node_create_or_delete_audio_gap_problem.md for full details.

================================================================================
CRITICAL CODE SECTIONS
================================================================================

IMPORTANT: Pay special attention to:
1. Lock scope in commitChanges() - currently holds lock during rebuild/prepareToPlay
2. Double locking at line 1148 in commitChanges()
3. processBlock() accessing internalGraph->getNodes() for MIDI distribution
4. Active processor list atomic snapshot update
5. Connection snapshot atomic update

================================================================================
FILE CONTENTS
================================================================================

"@

Add-Content -Path $outputFile -Value $header

foreach ($file in $sourceFiles) {
    $normalizedFile = $file.Replace('/', '\')
    $fullPath = Join-Path $projectRoot $normalizedFile
    
    # Skip comment lines
    if ($file.StartsWith("#") -or $normalizedFile.StartsWith("#")) {
        continue
    }
    
    if (Test-Path $fullPath) {
        $fileHeader = "`n" + ("=" * 80) + "`n"
        $fileHeader += "FILE: $normalizedFile`n"
        $fileHeader += ("=" * 80) + "`n`n"
        
        Add-Content -Path $outputFile -Value $fileHeader
        
        # For specific files, include only relevant sections
        if ($normalizedFile -like "*ModularSynthProcessor.cpp") {
            # Get the entire file for now (could filter to specific functions)
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        elseif ($normalizedFile -like "*ModularSynthProcessor.h") {
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        elseif ($normalizedFile -like "*AudioEngine.cpp") {
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        elseif ($normalizedFile -like "*AudioEngine.h") {
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        elseif ($normalizedFile -like "*plan*") {
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        else {
            # Include full file for other files
            Get-Content -Raw -Path $fullPath | Add-Content -Path $outputFile
        }
        
        Write-Host " -> Archived: $normalizedFile" -ForegroundColor Green
    } else {
        $warning = "`n" + ("=" * 80) + "`n"
        $warning += "FILE NOT FOUND: $normalizedFile`n"
        $warning += ("=" * 80) + "`n`n"
        Add-Content -Path $outputFile -Value $warning
        Write-Host " -> WARNING: File not found: $normalizedFile" -ForegroundColor Yellow
    }
}

# Add footer with testing checklist
$footer = @"

================================================================================
TESTING CHECKLIST
================================================================================

After implementing a fix, verify:

BASIC FUNCTIONALITY:
[ ] Nodes can be created during audio playback
[ ] Nodes can be deleted during audio playback
[ ] Audio continues playing without gaps/clicks
[ ] No crashes or stability issues
[ ] All existing functionality preserved

STRESS TESTS:
[ ] Rapid node creation/deletion (10+ per second)
[ ] Large graphs (50+ nodes) - add/remove operations
[ ] Complex topologies - add/remove connected nodes
[ ] Long-running stability (30+ minutes)

PERFORMANCE TESTS:
[ ] Measure audio callback timing during node operations
[ ] Verify max latency < 10ms during node operations
[ ] Check CPU usage doesn't spike
[ ] Monitor memory allocation patterns

SPECIFIC SCENARIOS:
[ ] Create node with connections
[ ] Delete node with connections
[ ] Create multiple nodes rapidly
[ ] Delete multiple nodes rapidly
[ ] Create node during heavy audio processing
[ ] MIDI distribution still works correctly

================================================================================
END OF MOOFY
================================================================================
"@

Add-Content -Path $outputFile -Value $footer

Write-Host "`nMoofy script completed!" -ForegroundColor Green
Write-Host "Output file: $outputFile" -ForegroundColor Cyan

