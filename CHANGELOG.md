# Changelog

All notable changes to this project will be documented in this file.

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
