# Grindhouse Mode Implementation - Update

## Summary

Added a new **Grindhouse Mode (GH)** - an exploitation film aesthetic with oversaturated, high-contrast colors featuring bold reds, magentas, neon yellows, and sickly greens.

### What is Grindhouse Mode?

Grindhouse mode provides a retro exploitation film aesthetic with:
- **Oversaturated colors** - Vivid reds, magentas, yellows, and cyans
- **High contrast** - Deep blacks paired with neon brights
- **Bold primary colors** - Saturated red, bright yellow, bright green
- **Accent colors** - Hot pink, cyan links, orange hover states
- **Gritty appearance** - Film grain-like color choices suggesting low-budget exploitation films

Perfect for users who want a bold, intentionally "trashy" retro aesthetic inspired by 1970s-80s exploitation cinema.

## Technical Implementation

### Files Modified

#### 1. **color_scheme_model.hpp**
Added `Grindhouse` enum value:
```cpp
enum ColorScheme { System, Day, Night, Noir, Grindhouse };
```

#### 2. **color_scheme_model.cpp**
Added grindhouse entry to scheme list:
```cpp
{qtr("GH"), ColorScheme::Grindhouse}
```

#### 3. **systempalette.hpp**
Added function declaration:
```cpp
void makeGrindhousePalette();
```

#### 4. **systempalette.cpp**
- Updated `updatePalette()` switch statement with Grindhouse case
- Implemented `makeGrindhousePalette()` function (~300 lines) with:
  - Deep black backgrounds (#0a0a0a, #1a1a1a)
  - Saturated red (#DD0000), bright yellow (#FFDD00), bright green (#00DD00)
  - Hot pink (#FF0099), cyan (#00FFFF), orange (#FF6600)
  - All color sets styled for grindhouse aesthetic

#### 5. **TopBar.qml**
Updated theme cycling and icon logic:
- Icon for Grindhouse mode: `VLCIcons.fire` 🔥
- Cycling: Light (1) → Dark (2) → Noir (3) → Grindhouse (4) → Light (1)
- Description: "Grindhouse Mode"

#### 6. **README.md**
- Added Grindhouse Mode to feature list
- Updated usage instructions to include GH in cycling
- Added description of Grindhouse aesthetic

## Color Palette

### Primary Colors
| Element | Color | Hex |
|---------|-------|-----|
| Background | Deep Black | #0a0a0a |
| Accents | Hot Pink | #FF0099 |
| Links | Cyan | #00FFFF |
| Focus | Magenta | #FF00FF |
| Positive | Bright Green | #00DD00 |
| Negative | Saturated Red | #DD0000 |
| Neutral | Bright Yellow | #FFDD00 |
| Hover | Orange | #FF6600 |

### Color Sets Configured
- **View** - Main interface colors with oversaturated primaries
- **Window** - Hot pink borders and black backgrounds
- **Badge** - Yellow badges with black text
- **TabButton** - Magenta focus, hot pink/red hover
- **ToolButton** - Magenta text, yellow focus, cyan hover
- **MenuBar** - Hot pink focus, red hover
- **Item** - Hot pink/red/yellow highlights
- **ButtonAccent** - Red background with magenta press state
- **ButtonStandard** - Magenta text with cyan/yellow interactions
- **Tooltip** - Black bg with yellow text
- **Slider** - Pink track with yellow/green/orange/red fills
- **ComboBox** - White text on dark backgrounds
- **TextField** - Magenta/yellow/cyan borders
- **Switch** - Magenta accents, red active state
- **SpinBox** - Magenta/yellow/cyan borders

## Usage

### Quick Access
1. Click the theme toggle button (fire icon 🔥) in the player's top bar
2. Cycles through: Light → Dark → Noir → Grindhouse → Light

### Preferences
1. Go to **Tools** → **Preferences** → **Interface** → **Color Scheme**
2. Select **"GH"** from the list
3. Click **Apply** or **OK**

### Settings Storage
Grindhouse preference is saved to:
- **Location:** `~/.config/vlc/vlcrc` (Linux/macOS) or Registry (Windows)
- **Key:** `MainWindow/color-scheme-index`
- **Value:** `4` (for Grindhouse mode)

## Features

### Benefits of Grindhouse Mode
- **Bold Visual Style** - Stands out with vivid, intentional color choices
- **Retro Aesthetic** - Captures 1970s-80s exploitation film look
- **High Accessibility** - Extreme contrast makes UI elements very visible
- **Fun Factor** - Adds personality and style to VLC interface
- **Persistent** - Automatically saves and restores your theme choice

### Characteristics
- Deep blacks (AMOLED-friendly)
- Neon-bright accents
- Oversaturated primaries
- High contrast throughout
- Playful, intentionally "cheap" aesthetic
- Film grain-inspired colors

## Performance Impact
- **Minimal** - No additional processing, just color values
- **Instant switching** - Theme changes immediately
- **No memory overhead** - Single palette set loaded once

## Compatibility
- ✅ Qt 6.8+
- ✅ Windows (with System theme detection)
- ✅ macOS (with System theme detection)
- ✅ Linux (GTK integration)
- ✅ All color-aware UI elements

## Future Enhancements
Possible future additions:
- Film grain shader for authentic grindhouse effect
- Adjustable saturation levels
- Custom color palette editor
- Movie title font styles (grindhouse-inspired typography)

## Related Documentation
- [DARKMODE_FEATURES.md](DARKMODE_FEATURES.md) - Overall dark mode system
- [README.md](README.md) - Main project documentation
- [NOIR_MODE_UPDATE.md](NOIR_MODE_UPDATE.md) - Noir mode details
- [DARKMODE_IMPLEMENTATION.md](DARKMODE_IMPLEMENTATION.md) - Implementation reference

---

**Grindhouse Mode** brings bold, trashy retro style to VLC - perfect for users who want a fun, intentionally garish exploitation film aesthetic! 🔥🎬
