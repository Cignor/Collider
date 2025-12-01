## Feature Plan: Per-File Selection for Auto-Updater Downloads

### 1. Goal and Scope

**Goal**: Allow the user to choose which files to download/apply in the software updater, via checkboxes in the update dialog, while keeping the system:
- Safe (critical files can be constrained),
- Predictable (clear consequences of deselecting files),
- Consistent with existing UX patterns (e.g. `VoiceDownloadDialog`).

**Out of scope (for now)**:
- Per-file scheduling or priority downloads.
- Persisting per-file ignore rules across versions (we’ll treat selection as “for this update run only”).

---

### 2. Current Behaviour (Baseline)

**Data flow**:
- `UpdateChecker` produces `UpdateInfo` with:
  - `filesToDownload` (`juce::Array<FileInfo>`)
  - `requiresRestart`
- `UpdateManager` holds `currentUpdateInfo` and:
  - Opens `UpdateDownloadDialog` with `updateDownloadDialog.open(info);`
  - On "Update Now", it calls `startDownload()`.
- `startDownload()`:
  - Always calls `fileDownloader->downloadFiles(currentUpdateInfo.filesToDownload, tempDir, ...)`
  - So currently **all** pending files are downloaded.
- `FileDownloader`:
  - Downloads every file in the list to a temp folder.
  - After our recent changes, continues when some files fail and tracks successful ones.
- `UpdateApplier`:
  - Applies only successfully downloaded files (after our refactor).
  - Continues on failures, tracks failed/successful applies.

**UI (`UpdateDownloadDialog`)**:
- Shows a table of files: name, type, size, hash (local | remote), status.
- No notion of user selection; everything pending is treated as mandatory.

**Important**: Our new safety behaviour already supports partial success in download/apply, but **not** user-driven selection.

---

### 3. Desired Behaviour

#### 3.1 UX Requirements

- **Checkbox per file** (row-level), similar to `VoiceDownloadDialog`:
  - User can enable/disable each non-critical file.
  - Critical files (e.g. `Pikon Raditsz.exe` and maybe key DLLs) should:
    - Either be forced **ON** (always downloaded/applied),
    - Or be deselectable but with a **clear warning** that some features may break.
- **Bulk controls**:
  - "Select All Pending" / "Select None" buttons.
  - Possibly "Select Only Critical" or "Select Only Recommended".
- **Summary text** updates to reflect:
  - Total files selected vs total pending,
  - Total selected download size.
- **Respect selection**:
  - `UpdateManager::startDownload()` uses only the **selected subset**.
- **Status visibility**:
  - Unselected files should stay visible with status such as:
    - `Skipped by user` (if pending but deselected),
    - `Up-to-date` for installed files (current behaviour).
- **Restart semantics**:
  - `requiresRestart` should reflect **selected files**, not the entire manifest.
  - If only non-exe/non-critical files are selected, we should avoid asserting "Requires Restart".

#### 3.2 Data / Logic Requirements

- `UpdateDownloadDialog` must keep track of selection state per `FileInfo`.
- Selection must be communicated back to `UpdateManager` **before** download starts.
- The selection logic should be **per run**, not persisted globally (at least initially).

---

### 4. Design Inspired by `VoiceDownloadDialog`

`VoiceDownloadDialog` patterns we can reuse:

- Maintains:
  - `availableVoices` vector.
  - `voiceSelected` vector<bool> parallel to data.
- UI:
  - Table with per-row checkbox:
    - Disabled checkboxes for already installed / actively downloading items.
  - "Download Selected" button that:
    - Gathers `selectedVoices` and passes to the download thread.
  - "Select All Missing" / "Deselect All" helpers.
  - Status column that shows `Installed`, `Partial`, `Error`, etc.

**Analogue for updater**:
- Replace voices with `FileInfo`s.
- Replace voice statuses with `Installed / Pending / Failed / Skipped`.
- Replace `downloadThread.downloadBatch(selectedVoices)` with:
  - `UpdateManager::startDownload(selectedFiles)` or similar.

---

### 5. Concrete API / Structure Changes

#### 5.1 UpdateDownloadDialog

**New state:**
- `juce::Array<FileInfo> files;` (already present in some form).
- `juce::Array<bool> fileSelected;` aligned with `files`.
- Possibly `juce::HashMap<juce::String, bool> selectionByPath;` for robustness when filtering/sorting.

**New responsibilities:**
- Provide **selection info** back to `UpdateManager`:
  - Option A (simpler): `UpdateDownloadDialog` keeps a pointer / callback into `UpdateManager` and calls `onStartDownload(const juce::Array<FileInfo>& selectedFiles);`
  - Option B (cleaner): extend the existing `onStartDownload` callback signature:
    - From: `std::function<void()> onStartDownload;`
    - To: `std::function<void(const juce::Array<FileInfo>&)> onStartDownload;`
  - Given you’re ok with refactors, **Option B** is cleaner and closer to `VoiceDownloadDialog::renderDownloadControls()`, which computes the selection when "Download Selected" is clicked.

**UI changes:**
- In the table renderer:
  - Add a first column "Select":
    - Checkbox bound to `fileSelected[index]`.
    - Disable checkbox if:
      - The file is already installed & up-to-date (status "Installed").
      - Or we decide to force critical files (discussed below).
  - When filtering/searching/sorting, access the canonical selection by index into the underlying `files` array (not the filtered vector’s local index), similarly to how `VoiceDownloadDialog` maps `filtered` entries back to `availableVoices`.

- Above the table:
  - Add two buttons:
    - "Select All Pending" → set `fileSelected[i] = true` for all files with status `Pending`.
    - "Deselect All" → set all `fileSelected[i] = false` **except** those we want to enforce (e.g. critical).

- Summary line:
  - Instead of:
    - `Summary: N files to update | Total Download Size: X MB`
  - Use:
    - `Summary: N selected of M pending | Total Selected Size: X MB`
  - Where N and X are computed over `fileSelected[i] == true`.

**Critical file policy** (configurable):
- We can define "critical" as `fileInfo.critical == true`.
- Options:
  - **Option A (Strict)**:
    - Critical files are **always selected & non-deselectable** (checkbox disabled, forced true).
    - Pros: avoids weird states where main exe is outdated but other assets are new.
    - Cons: removes flexibility; cannot patch only some DLLs/assets.
  - **Option B (Warn but allow)**:
    - Critical files are initially selected, but checkbox is enabled.
    - If user deselects any critical file, show tooltip or a one-time warning like:
      - "Warning: Skipping critical files may leave your installation in an inconsistent state."
  - Given your typical workflows, **Option B** is probably more ergonomic, with good logging and the hash view already helping identify inconsistent states.

We can parameterise this via a constexpr or small setting inside `UpdateDownloadDialog`.

#### 5.2 UpdateManager

**Callback wiring:**
- Currently:
  - `updateDownloadDialog.onStartDownload = [this]() { startDownload(); };`
- After change:
  - `onStartDownload` becomes `std::function<void(const juce::Array<FileInfo>&)>`.
  - Wiring:
    - `updateDownloadDialog.onStartDownload = [this](const juce::Array<FileInfo>& selected) { startDownload(selected); };`

**New overload:**
- `void startDownload(const juce::Array<FileInfo>& selectedFiles);`
  - This becomes the main variant.
  - Optional: keep the no-arg `startDownload()` for backward compatibility, which just forwards:
    - `startDownload(currentUpdateInfo.filesToDownload);`

**Safety checks in `startDownload(selectedFiles)`**:
- If `selectedFiles` is empty:
  - Show a message box:
    - "No files selected for download."
  - Do **not** start the downloader.
- Recompute `requiresRestart` for this run:
  - Example:
    - `bool requiresRestartForThisRun = std::any_of(selectedFiles.begin(), selectedFiles.end(), [](auto& f){ return f.critical; });`
  - Store this in a field inside `UpdateManager` separate from the manifest’s `currentUpdateInfo.requiresRestart`, or override the `currentUpdateInfo.requiresRestart` for this session only (with clear comments).
  - This ensures we don’t force restart when the exe isn’t part of the selection.

**Downloader usage:**
- Change:
  - `fileDownloader->downloadFiles(currentUpdateInfo.filesToDownload, ...)`
  - To:
  - `fileDownloader->downloadFiles(selectedFiles, ...)`

**PikonUpdater manifest creation:**
- Currently:
  - `auto updateManifest = createUpdateManifest(currentUpdateInfo.filesToDownload, tempDir);`
  - After our partial-success improvements, we already switched to using `successfulDownloads`.
  - With selection, that stays correct: the manifest is built from **those files we actually downloaded successfully**, which are a subset of the selection.

#### 5.3 UpdateInfo / UpdateChecker

No structural changes are strictly necessary:
- `UpdateInfo::filesToDownload` remains "all files that *could* be updated".
- Selection is applied later in the UI/`UpdateManager`, not in the checker.

We should **not** persist deselections in `UpdateInfo` or `installed_files.json` initially to avoid edge-case complexity. If you later want "ignore this file permanently", that’s another phase.

---

### 6. Edge Cases and Behavioural Details

#### 6.1 User deselects only the main exe

Scenario:
- Manifest says `requiresRestart = true` because exe changed.
- User only selects non-critical files (DLLs/assets), deselecting the exe.

Planned behaviour:
- `requiresRestartForThisRun` is recomputed from the **selected** set:
  - If no critical files selected → no restart triggered.
- The exe remains outdated, but:
  - Our hash-based logic will continue to flag it as `Pending` next time, as long as manifest hash != installed hash.

Risk:
- Some assets might expect newer exe logic.
Mitigation:
- This is *already* a risk any time we ship mismatched artifacts; with strict versioning and proper asset compatibility, this should be rare.
- We can optionally:
  - Show a small note in the UI when exe is pending but not selected:
    - "The main application executable is not selected; some features may not work until it is updated."

#### 6.2 User deselects a critical DLL but updates exe

This can create ABI/API mismatches.

Mitigation options:
- Keep `critical` set only for files that must move in lockstep with exe.
- Or treat all criticals as a group:
  - If exe is selected:
    - Either force-select all other criticals,
    - Or show a warning "Recommended to also update X critical modules".

For a first implementation, we can:
- Keep the policy simple:
  - Exe and a small, curated subset of DLLs share the `critical` flag.
  - **Option A for this subset**: force them all selected when any is selected (implementation: when toggling such a file, auto-toggle siblings and show a small info text).
  - Or **Option B**: allow full freedom but rely on your familiarity + debug logging.

Given your expert workflow, **Option B** may be acceptable, with ample logs and the existing "Hash (Local | Remote)" visibility; we can document that mixing versions is unsupported.

#### 6.3 Manifest changes between sessions

If the manifest changes (e.g., you regenerate it with a different set of files) while the dialog is open, we could have index mismatches.

Mitigation:
- We don’t reload the manifest while the dialog is visible; a new manifest only arrives when you trigger a new "Check for Updates".
- Selection is per dialog session; when you close and re-open, we rebuild selection arrays from scratch.

#### 6.4 Download failure for selected subset

We’ve already improved this:
- Some files can fail to download/apply without invalidating everything.
- With selection, the summary shows:
  - How many selected files failed.

Potential refinement:
- In the table, after a run, we can mark statuses per file:
  - `Downloaded & Applied`, `Downloaded but failed to apply`, `Download Failed`, `Skipped`.
- That would require storing per-file result in `UpdateManager`/`UpdateDownloadDialog`; this is a **Phase 2 visual enhancement**, not strictly necessary for MVP.

---

### 7. Implementation Phases and Difficulty Levels

#### Phase 1 (Medium Difficulty): Basic Per-File Selection

**Steps:**
1. Extend `UpdateDownloadDialog`:
   - Add `fileSelected` parallel to `filesToShow` / underlying file list.
   - Add checkbox column + bulk select/deselect controls.
2. Change `onStartDownload` callback signature to take `const juce::Array<FileInfo>& selectedFiles`.
3. Modify `UpdateManager`:
   - Add `startDownload(const juce::Array<FileInfo>& selectedFiles)`.
   - Wire dialog’s `onStartDownload` to pass the selected subset.
   - Recompute `requiresRestart` from `selectedFiles`.
4. Ensure `FileDownloader` still works with arbitrary subsets (it already does).
5. Keep critical policy simple:
   - Critical files are selectable but pre-selected by default.

**Risk rating**: **Low–Medium**
- Mostly UI and plumbing changes; core download/apply logic remains the same.

#### Phase 2 (Medium–High Difficulty): Critical Policy & Per-File Result Status

**Additions:**
- Grouping of critical files (exe + friends).
- Auto-selection and/or warning when toggling them.
- UI status column enriched after a run to show:
  - `Applied`, `Failed`, `Skipped`, `Already up-to-date`.

**Risk rating**: **Medium**
- More branching paths, more UX states to test.

#### Phase 3 (High Difficulty): Persistent Ignore Rules

**Future-only**:
- Let the user "ignore this file for future updates".
- Requires:
  - Storing ignore list in a preferences file,
  - Merging ignore state with new manifests safely.

**Risk rating**: **High**
- Increases long-term state complexity and can cause surprising behaviour if not carefully surfaced.

---

### 8. Confidence and Trade-offs

**Confidence: 8.5 / 10**

**Strong points:**
- Leverages existing, proven pattern from `VoiceDownloadDialog` (per-item selection, bulk controls, clear statuses).
- Minimal disturbance to core updater pipeline:
  - Checker → Manifest → Selection → Downloader → Applier still flows linearly.
- Our recent improvements (partial success, hash verification, PikonUpdater separation) already make partial updates safe, so selection becomes primarily a UX layer.

**Weak points / Areas to watch:**
- Critical components policy:
  - If we’re too permissive, users can create inconsistent states.
  - If we’re too strict, selection loses much of its usefulness.
- Restart semantics:
  - Need careful recomputation from the **selected** set, not just from manifest-level metadata.
- UI complexity:
  - We must keep the dialog readable and fast despite more states (Pending/Installed/Skipped/Failed/Partial + selection).

---

### 9. Potential Problems and Mitigations

1. **User confusion about what will actually happen**
   - Mitigation:
     - Clear summary line ("N of M selected, X MB").
     - Use colour cues in status column (already in place for hash column).
     - Optionally, hover tooltip on the "Update Now" button describing the effect.

2. **Inconsistent installation (exe old, assets new or vice versa)**
   - Mitigation:
     - Keep `critical` small and intentional.
     - Provide clear warnings when deselecting critical files.
     - Rely on hash-based detection to keep flagging outdated components.

3. **Bugs in index mapping due to filtering/sorting**
   - Mitigation:
     - Use a stable backing array and map filtered indices back to that array, like `VoiceDownloadDialog` does with `availableVoices` and `filtered`.
     - Add asserts/logs when an index/lookup fails.

4. **Edge-case with partial downloads**
   - Mitigation:
     - Already handled via partial success logic.
     - Just ensure selection code *only* influences what we request from `FileDownloader`.

5. **Future persistence of ignore rules**
   - Not part of this implementation, but:
     - If/when added, ensure separation between:
       - "Skipped this run" vs "Ignore this file permanently".
     - Surface ignore rules clearly in the UI.

---

### 10. Recommended Implementation Path (MVP)

1. Implement **Phase 1**:
   - Per-file checkbox selection in `UpdateDownloadDialog`.
   - Adjust `onStartDownload` to provide selected files.
   - Adjust `UpdateManager::startDownload` to use selection and recompute restart requirement.
2. Keep **critical files selectable** but **pre-selected**, with logging and possibly a small warning tooltip.
3. After MVP works:
   - Evaluate whether you experience real-world issues from skipping criticals.
   - If yes, tighten the policy (force-select or group-select).

This approach gives you quick, practical control over what is downloaded/applied, with relatively low risk and good alignment with patterns already used in your voice downloader UI.


