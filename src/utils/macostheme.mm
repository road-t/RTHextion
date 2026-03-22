#include "macostheme.h"
#import <AppKit/AppKit.h>

void setMacOSDarkMode(bool enabled)
{
    if (enabled)
        [NSApp setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameDarkAqua]];
    else
        [NSApp setAppearance:nil]; // follow system setting
}
