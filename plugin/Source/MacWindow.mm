// macOS window state for the skin view: whether the window holding a view can be seen at all.
#import <AppKit/AppKit.h>

void nd2SystemLog(const char* line) { NSLog(@"%s", line); }

// False when the window is not visible, minimised, or fully covered by other windows (occlusion state).
bool nd2WindowVisible(void* nsView) {
    NSView* view = (__bridge NSView*) nsView;
    NSWindow* window = [view window];
    if (window == nil || ![window isVisible] || [window isMiniaturized]) return false;
    return ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}
