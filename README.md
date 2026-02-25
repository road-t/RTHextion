# RTHextion v0.8

Hex editor for ROMHacking, a tribute to 00's Translhextion from Januschan based on Dax89's [QHexView](https://github.com/Dax89/QHexView).

**RTHextion** supports tables, pointers search, dump export/import (with automatic pointers update).

<img src="assets/general_view.png" alt="General view" width="600" />
<img src="assets/pointers.png" alt="Pointers search window" width="300" />

**N.B.:** The software is under development, some features can be glitchy.

## Installation

### macOS (Intel & Apple Silicon)

Download the `.dmg` file from [Releases](https://github.com/road-t/RTHextion/releases).

**If you get "damaged file" error** (especially on Apple Silicon/M1+):
- Right-click the DMG → Open With → Finder
- Or open Terminal and run: `sudo xattr -rd com.apple.quarantine /Applications/RTHextion.app`

Then drag **RTHextion.app** to the Applications folder as usual.

### Windows

Download `RTHextion-Windows-x64.zip` from [Releases](https://github.com/road-t/RTHextion/releases), extract and run `rthextion.exe`.

### Linux

1. Download `RTHextion-Linux-x86_64.tar.gz` from [Releases](https://github.com/road-t/RTHextion/releases)
2. Extract: `tar -xzf RTHextion-Linux-x86_64.tar.gz -C ~/.local/opt/` (or any preferred location)
3. Run: `~/.local/opt/usr/bin/rthextion`

**Or use the install script** (from release asset `install-linux.sh`):
```bash
bash install-linux.sh
```

Note: GitHub Actions "Artifacts" are always downloaded as `.zip` containers. The actual Linux package inside release assets is `RTHextion-Linux-x86_64.tar.gz`.