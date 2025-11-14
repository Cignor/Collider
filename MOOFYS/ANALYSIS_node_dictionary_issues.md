# Analysis: Node Dictionary Display Issues

## Critical Problems Identified

### Problem 1: Table of Contents is Being Parsed as Content
**Location:** `USER_MANUAL/Nodes_Dictionary.md` lines 8-118

The markdown file has a "Table of Contents" section at the top that uses:
- `## Table of Contents` (Level 1)
- `### Quick Reference Index` (Level 2)  
- `#### 1. SOURCE NODES` (Level 3) - These are just index links
- Then actual content starts at line 120: `## 1. SOURCE NODES` (Level 1)

**Impact:** The parser treats the TOC as real content, creating duplicate/misplaced sections in the hierarchy.

### Problem 2: Navigation Sidebar Includes Categories
**Location:** `HelpManagerComponent::buildNavigationList()` line 1529

Current behavior: Includes Level 1 (categories like "1. SOURCE NODES") and Level 2 (nodes like "vco")

**User wants:** ONLY node names (Level 2) in the left sidebar, no categories.

### Problem 3: Content Structure Mismatch
**Location:** `HelpManagerComponent::parseMarkdown()` line 931

The parser correctly creates the hierarchy:
- Level 1: `## 1. SOURCE NODES` (Category)
- Level 2: `### vco` (Node)
- Level 3: `#### Inputs` (Section)

But the TOC section is interfering, and the navigation is showing too much.

### Problem 4: Content Rendering May Show TOC
**Location:** `HelpManagerComponent::renderNodeDictionaryContent()` line 1667

The content renderer processes ALL top-level sections, including the TOC section, which shouldn't be displayed.

## Root Cause Analysis

1. **Parser doesn't skip TOC:** The `parseMarkdown()` function processes every `##` header, including the TOC section.

2. **Navigation includes wrong levels:** `buildNavigationList()` includes Level 1 (categories) when user only wants Level 2 (nodes).

3. **No filtering of TOC:** There's no logic to skip or filter out the "Table of Contents" section.

## Expected Structure

**Left Sidebar (Navigation):**
- vco
- polyvco
- noise
- audio_input
- sample_loader
- value
- vcf
- delay
- ... (only node names, no categories)

**Right Window (Content):**
- Category headers (collapsible): "1. SOURCE NODES", "2. EFFECT NODES", etc.
- Node entries with:
  - Node name (e.g., "vco")
  - Description (colored background)
  - Inputs section (colored background)
  - Outputs section (colored background)
  - Parameters section (colored background)
  - How to Use section (colored background)

## Solutions Required

1. **Skip TOC section during parsing or rendering**
   - Option A: Skip lines 8-118 during parsing
   - Option B: Filter out sections with title "Table of Contents" or "Quick Reference Index"

2. **Update `buildNavigationList()` to ONLY include Level 2 (nodes)**
   - Change condition from `if (section.level < 3)` to `if (section.level == 2)`
   - This will exclude categories (Level 1) and sections (Level 3)

3. **Filter TOC from content rendering**
   - Skip sections where `title.containsIgnoreCase("Table of Contents")` or `title.containsIgnoreCase("Quick Reference Index")`

4. **Verify markdown structure**
   - Ensure actual content sections start with `## 1. SOURCE NODES` (not `####`)

