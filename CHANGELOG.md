# Changelog

All notable changes to this project will be documented in this file.

## [v1.0] - 2026-03-06

### Added
- Expanded ROM format auto-detection for major retro platforms:
  - Nintendo: N64 (all three formats: z64/v64/n64 with format-specific byte orders), GB/GBC/GBA.
  - Sega: Genesis/Mega Drive (with header signature detection), Master System, Game Gear, SG-1000.
  - Bandai: WonderSwan and WonderSwan Color.
  - Coleco: ColecoVision.
  - Atari: 2600, 5200, 7800, and 8-bit computer formats (XFD, ATR, ATX, CAS, XEX, CDM).
- Format-specific byte order defaults: LE for most systems, BE for Genesis/MD and N64 z64 format, word-swapped for N64 v64 format.
- N64 ROM header magic detection: auto-identifies format (z64/v64/n64) even when extension is ambiguous.
- Heuristic detection for ambiguous `.bin`/`.rom` files (N64 header and Mega Drive signature checks).
- Recent files list for files and translation tables.
- "Auto-load last opened file at startup" option.
- New interface customization options including custom non-printable characters selection
- Pointer/offset minimap in the right sidebar with clickable entries for quick navigation.
- File navigation history with back/forward buttons and keyboard shortcuts.
- Signed byte/word/dword value display toggle for easier debugging of signed data.
- New "View" menu with options for toggling hex/text panes, showing/hiding minimap, and customizing non-printable character display.
- New icons for all toolbar and menu actions, including translation table features.

### Changed
- Window area options moved to a dedicated "View" menu.
- Selection pointer centered
- Byte order preference now set automatically based on detected ROM type when file is opened.
- Script dump/insertion without table enabled

### Fixed
- UI translations
- ASCII area rendering
- Pointer frame rendering for non-dword-aligned pointers
- Font fallback warnings on macOS by replacing Helvetica with system default font
- Fixed various minor bugs and edge cases in pointer handling and translation table application

## [v0.9] - 2026-02-28

### Added
- Implemented semi-automatic translation table generation by searching known text fragments.
- Added UI flow for creating new empty translation tables and saving translation tables to file.
- Lanugage system updated to switch interface language on the fly.
– Added Spanish, French, Portuguese, Chinese and Japanese translations for the UI.

### Changed
– Toolbar and menu actions updated to reflect new translation table features and UI language switching.
– New icons added for translation table actions and updated existing icons for better visual consistency.
- Updated bundle/app metadata and packaging-related configuration for macOS app presentation.
- Updated resource and project naming consistency for case-sensitive environments (`rthextion.qrc` / `rthextion.pro`).
- Improved translation and table-generation UX messaging in dialogs.

### Fixed
- Fixed incorrect rendering of bytes `> 0x7F` in text/ASCII pane with translation table mapping.
- Fixed runtime warnings related to invalid Qt stylesheet properties and action wiring.
- Fixed font fallback warnings by replacing missing font family usage on macOS.
- Fixed signed-byte debug/key display issues for translation table diagnostics.
- Fixed Qt6 compatibility issues:
  - Replaced Qt5-only stream codec usage with Qt6-compatible UTF-8 stream encoding path.
  - Replaced resource init name mismatch causing linker failure (`Q_INIT_RESOURCE(rthextion)`).
  - Replaced problematic `QChar` byte-construction patterns with Qt6-safe handling.

### CI / Release
- Fixed GitHub Actions trigger alignment with release tagging (`v*` tags).
- Removed invalid `.qm` generation steps from CI for this `.lang`-based translation workflow.
- Fixed case-sensitive project path usage in workflow build steps.
- Updated release build flow to align with successful local Qt6 build behavior.

---

## [v0.8.17] - 2026-02-xx
- Previous release baseline.
