#pragma once

// Sets the application-level NSAppearance on macOS.
// enabled=true  → NSAppearanceNameDarkAqua (always dark)
// enabled=false → nil (follow the system preference)
void setMacOSDarkMode(bool enabled);
