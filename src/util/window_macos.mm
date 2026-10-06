#include "window_macos.h"

#import <Cocoa/Cocoa.h>

void ConfigureMacWindowFrameAutosave(void *window_handle)
{
    if (!window_handle)
        return;

    NSWindow *window = (NSWindow *)window_handle;
    NSString *name = @"TLEscopeMainWindow";
    [window setFrameUsingName:name];
    [window setFrameAutosaveName:name];
}
