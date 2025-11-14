# Analysis: Parser Bug - All Content Under VCO

## Problem
All inputs/outputs/parameters are appearing under `vco`, and other node definitions are empty.

## Expected Structure
```
## 1. SOURCE NODES (Level 1)
  content: "Source nodes generate..."
  children:
    ### vco (Level 2)
      content: "**Voltage-Controlled Oscillator**\n\nA standard..."
      children:
        #### Inputs (Level 3)
          content: "- `Frequency` (CV)..."
        #### Outputs (Level 3)
          content: "- `Out` (Audio)..."
        #### Parameters (Level 3)
          content: "- `Frequency` (20 Hz - 20 kHz)..."
        #### How to Use (Level 3)
          content: "1. Connect the audio output..."
    ### polyvco (Level 2)
      content: "**Multi-Voice Oscillator Bank**\n\nA polyphonic..."
      children:
        #### Inputs (Level 3)
          content: "- `Num Voices Mod` (Raw)..."
        ...
```

## Current Bug
The parser is incorrectly assigning content, causing:
- All Level 3 sections (Inputs, Outputs, etc.) to appear as children of `vco`
- Other nodes (polyvco, noise, etc.) to have empty content
- Content from later nodes to be assigned to earlier nodes

## Root Cause Hypothesis
The stack management in `parseMarkdown()` is not correctly maintaining the hierarchy when:
1. Encountering a new Level 2 node (e.g., `### polyvco`) after Level 3 sections
2. The stack should pop Level 3 sections AND the previous Level 2 node
3. But content might be getting assigned to the wrong parent during this process

## Fix Strategy
1. Ensure stack is correctly popped when encountering new sections
2. Verify that `getCurrentSection()` always returns a valid pointer
3. Add bounds checking to prevent accessing invalid indices
4. Ensure content is only added to the top of the stack (most recent section)

