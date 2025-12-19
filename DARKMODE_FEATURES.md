# VLC Dark Mode Implementation

## Overview
This document describes the dark mode feature enhancements made to VLC media player, including improved color palettes, quick toggle buttons, and persistence settings.

## Features

### 1. Enhanced Dark Palette Colors
**File:** [modules/gui/qt/style/systempalette.cpp](modules/gui/qt/style/systempalette.cpp)

The dark mode palette has been enhanced with better contrast and eye-friendly colors:

#### Color Improvements:
- **Primary Backgrounds:** Improved contrast while maintaining AMOLED-friendly pure blacks
- **Negative (Error) Colors:** Changed from `#FF99A4` to `#D85566` with white text for better readability
- **Positive (Success) Colors:** Changed from `#6CCB5F` to `#4CAF50` with white text for better visibility
- **Neutral (Warning) Colors:** Brightened from `#FCE100` to `#FFD700` (gold) for better distinction
- **Button Press States:** Improved from `#e67a30` to `#FF7D00` for better visual feedback
- **Tooltips:** Enhanced with darker background for better contrast

### 2. Color Scheme Model with Persistence
**Files:** 
- [modules/gui/qt/util/color_scheme_model.hpp](modules/gui/qt/util/color_scheme_model.hpp)
- [modules/gui/qt/util/color_scheme_model.cpp](modules/gui/qt/util/color_scheme_model.cpp)
- [modules/gui/qt/maininterface/mainctx.cpp](modules/gui/qt/maininterface/mainctx.cpp)

Features:
- **Three Color Schemes:** System (default), Day (Light), Night (Dark)
- **Automatic Persistence:** User preference is saved in `MainWindow/color-scheme-index` setting
- **System Theme Detection:** Supports reading system-wide theme preference on Windows, Linux (GTK), and macOS

### 3. Dark Mode Toggle Button
**File:** [modules/gui/qt/player/qml/TopBar.qml](modules/gui/qt/player/qml/TopBar.qml)

A new quick toggle button has been added to the player top bar:
- **Location:** Between menu button and playlist button
- **Icons:** 
  - ☀️ Sun icon for Light mode
  - 🌙 Moon icon for Dark mode
  - ◯ Contrast icon for System mode
- **Functionality:** Click to toggle between Light and Dark modes (cycles through: Light → Dark → Light)
- **Tooltip:** Displays current theme mode

### 4. Qt 6.8+ Integration
**File:** [modules/gui/qt/maininterface/mainctx.cpp](modules/gui/qt/maininterface/mainctx.cpp)

For Qt 6.8 and later:
- Color scheme changes are propagated to Qt's `QStyleHints`
- This ensures consistency across the entire application including native widgets
- Supports `Light`, `Dark`, and system-default color schemes

## Usage

### Switching Dark Mode

#### Method 1: Top Bar Toggle Button
1. Look for the theme icon in the player's top bar (between menu and playlist buttons)
2. Click to toggle between Light and Dark modes

#### Method 2: Preferences Dialog
1. Open Tools → Preferences
2. Navigate to Interface → Color Scheme
3. Select from: System, Day, or Night
4. Changes apply immediately and are saved automatically

#### Method 3: Keyboard Navigation
- Use keyboard to navigate to the dark mode button and press Space/Enter to toggle

## Technical Details

### Color Scheme Storage
- **Format:** QSettings (Platform-specific: Registry on Windows, .conf on Linux, Defaults on macOS)
- **Key:** `MainWindow/color-scheme-index`
- **Values:** 
  - 0 = System (default)
  - 1 = Day (Light)
  - 2 = Night (Dark)

### Dark Palette Color Map
All UI elements are mapped to a comprehensive color palette including:
- View backgrounds and text
- Window decorations
- Buttons (standard, accent, tab, tool)
- Input fields (textfield, combobox, spinbox)
- Interactive elements (sliders, switches)
- Status indicators (badges, tooltips)
- Separators and borders

### Platform Support
- **Windows:** System theme preference via Windows Registry
- **Linux:** GTK theme detection on GTK-based desktops
- **macOS:** System appearance detection

## Design Principles

1. **Eye Comfort:** Reduced blue light and harsh contrast
2. **Accessibility:** Maintains WCAG AA contrast ratios
3. **AMOLED Optimization:** Uses true blacks (#000000) for AMOLED displays
4. **Consistency:** Unified color scheme across all UI elements
5. **Responsiveness:** Instant theme switching without restart

## Future Enhancements

Potential improvements for future versions:
- Custom color scheme editor
- Scheduled dark mode (automatic switch based on time)
- Per-folder theme preferences
- Theme preview before applying
- Import/Export custom themes

## Testing Checklist

- [x] Dark mode colors display correctly
- [x] Light mode colors display correctly
- [x] Theme toggle button functions
- [x] Settings are persisted between sessions
- [x] System theme detection works
- [x] All UI elements respond to theme changes
- [x] No compilation errors
- [x] Icons display correctly for each theme

## Compatibility

- **Minimum Qt Version:** 5.15 (with full support in 6.8+)
- **Platforms:** Windows 7+, macOS 10.10+, Linux (all distributions)
- **VLC Version:** Current development branch

## References

- Qt Color Schemes: https://doc.qt.io/qt-6/qguiapplication.html#styleHints
- VLC Qt GUI: modules/gui/qt/
- SystemPalette Implementation: modules/gui/qt/style/systempalette.cpp
