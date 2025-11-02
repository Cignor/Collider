# UI Scroll Event Blocking System

## Overview
This document describes the scroll event blocking system implemented for the experimental UI menu to prevent scroll events from interfering with game controls when the menu is open.

## Problem
When the experimental UI menu is open, mouse scroll events were being processed by both the menu (for scrolling through audio samples/parameters) and the game (for changing brush size, camera zoom, etc.). This caused unwanted side effects like brush size changes while trying to scroll through menu items.

## Solution
Implemented a scroll event consumption system that blocks game scroll processing when the UI menu is active.

## Architecture

### 1. UIManager (`manager.py`)
- **`on_mouse_scroll(x, y, scroll_y)`**: Returns `True` if event was consumed (menu open), `False` otherwise
- When menu is open (`self.menu.opened`), forwards scroll to menu and returns `True`
- When menu is closed, returns `False` to allow game processing

### 2. InputHandler (`input_handler.py`)
- **`handle_mouse_scroll(x, y, scroll_x, scroll_y)`**: Checks if UI consumed the event
- If `ui_manager.on_mouse_scroll()` returns `True`, immediately returns without processing game scroll logic
- If `False`, continues with normal game scroll handling (brush size, camera, etc.)

### 3. AudioSelectionMenu (`ui_audio_selection.py`)
- **`handle_scroll(x, y, scroll_y)`**: Processes scroll for menu-specific actions
- Column 3 scrolling (audio samples list)
- Parameter slider adjustments (noise/frequency presets)

## Event Flow

```
Mouse Scroll Event
       ↓
InputHandler.handle_mouse_scroll()
       ↓
UIManager.on_mouse_scroll()
       ↓
Is menu open?
   ├─ YES → AudioSelectionMenu.handle_scroll() → Return True (consumed)
   └─ NO  → Return False (not consumed)
       ↓
If not consumed:
Game scroll logic (brush size, camera, etc.)
```

## Usage

### For UI Components
```python
# In UIManager
def on_mouse_scroll(self, x, y, scroll_y):
    if self.menu.opened:
        self.menu.handle_scroll(x, y, scroll_y)
        return True  # Event consumed
    return False  # Event not consumed
```

### For Game Input Handlers
```python
# In InputHandler
def handle_mouse_scroll(self, x, y, scroll_x, scroll_y):
    # Check if UI consumed the event
    if hasattr(self.game, 'ui_manager'):
        event_consumed = self.game.ui_manager.on_mouse_scroll(x, y, scroll_y)
        if event_consumed:
            return  # Don't process game scroll logic
    
    # Normal game scroll handling...
```

## Benefits
1. **Clean separation**: UI and game scroll logic don't interfere
2. **Intuitive behavior**: Scroll only affects the active context (menu vs game)
3. **Extensible**: Easy to add more UI components that consume scroll events
4. **Maintainable**: Clear event flow and responsibility boundaries

## Future Extensions
This pattern can be extended to other input events:
- Mouse motion blocking for UI drag operations
- Key press blocking for UI text input
- Mouse click blocking for UI button interactions

## Implementation Notes
- The system uses boolean return values to indicate event consumption
- UI components should only consume events when they're actually active/visible
- Game input handlers should always check for event consumption before processing
- The pattern follows the "chain of responsibility" design pattern
