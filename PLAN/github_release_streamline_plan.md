## Objective
Establish a single, authoritative source for the software version and streamline the release workflow so future GitHub drops are predictable, automated where practical, and traceable.

## Guiding Principles
- Version lives in one place (ideally a JUCE `ProjectInfo` header or `VersionInfo.json`) and every artifact reads from it.
- Release flow is deterministic: tag → build → package → upload → publish notes.
- Artifacts are reproducible: anyone can re-run the same steps and get identical bits.

## Detailed Plan
1. **Select canonical version authority**
   - Options: `juce_ProjectInfo.h`, dedicated `version.json`, or CMake option.
   - Decide based on which subsystem (JUCE app, ImGui tooling, installer scripts) consumes it most.
   - Define schema: semantic version + build metadata + channel (alpha/beta/release).
2. **Create propagation helpers**
   - Small CMake/Python utility reads canonical version and:
     - Updates JUCE `ProjectInfo::versionString`.
     - Writes `Info.plist`/Windows resource info.
     - Embeds into splash/about UIs.
   - Add CI guard that fails if downstream files drift.
3. **Tagging workflow**
   - Introduce `scripts/release/tag_version.ps1` to:
     - Validate working tree clean.
     - Bump version (with prerelease flag if needed).
     - Commit `version.json`.
     - Create signed git tag.
4. **Build + package automation**
   - Add `scripts/release/build_release.ps1`:
     - Runs JUCE exporter (Visual Studio/Xcode) in Release.
     - Produces VST3/standalone bundles.
     - Zips artifacts into `dist/<version>/`.
   - Include checksum generation for verification.
5. **Release notes pipeline**
   - Maintain `CHANGELOG.md` (Keep a Changelog style) driven by PR labels.
   - Script extracts latest section to feed GitHub Release body.
6. **GitHub Release publishing**
   - GitHub Actions workflow triggered by tag:
     - Restores cache, builds, runs smoke tests.
     - Uploads artifacts + changelog text.
     - Optionally notarizes/signs binaries if credentials available.
7. **Verification checklist**
   - Manual QA template per release (install test, preset migration, MIDI I/O sanity).
   - Storage of signed checksums + release manifest for auditability.

## Difficulty Options
- **Baseline (Low)**: Single version file + manual script to bump and tag; manual builds/uploads.
- **Standard (Medium)**: Baseline + automated propagation + packaging script + changelog enforcement.
- **Advanced (High)**: Standard + full CI build matrix, signing/notarization, automated GitHub release with QA gates.

## Risk Assessment
- **Overall risk: Medium.**
  - *Key risks*: inconsistent version propagation, CI secrets leakage, build reproducibility issues, Windows code signing hurdles.
  - *Mitigations*: pre-commit lint to ensure single source, secret storage via GitHub OIDC, deterministic build instructions, staged rollout for signing.

## Potential Problems & Mitigations
- Divergent version numbers in legacy modules → add CI test that diffs known files.
- Windows resource editing failures → guard scripts with fallback instructions + verbose logging.
- Large artifact sizes exceeding GitHub limits → integrate release CDN or split assets.
- Developer habit friction → document workflow and add `npm version`-style helper with prompts.
- CI instability on JUCE exporters → cache dependencies, provide local reproduction script.

## Confidence Rating
- **Confidence: 0.78**
  - *Strengths*: Plan mirrors common JUCE release practices, leverages existing scriptable steps, scoped increments.
  - *Weak points*: Unknowns around current build pipeline and signing infrastructure; need discovery on existing CI resources before automating.

