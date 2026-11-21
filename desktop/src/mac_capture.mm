#include "media_engine.h"
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
namespace Cast {
QList<SourceProperty> macCaptureProperties(const QString &kind) {
    if (!CGPreflightScreenCaptureAccess() && !CGRequestScreenCaptureAccess())
        return {};
    if (kind == "window") {
        SourceProperty targets{"window", QObject::tr("Pencere"), OBS_PROPERTY_LIST, {}};
        CFArrayRef windows = CGWindowListCopyWindowInfo(
            kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
        for (NSDictionary *window in (NSArray *)windows) {
            if ([window[(NSString *)kCGWindowLayer] intValue] != 0)
                continue;
            NSString *owner = window[(NSString *)kCGWindowOwnerName];
            NSString *title = window[(NSString *)kCGWindowName];
            NSString *label =
                title.length ? [NSString stringWithFormat:@"%@ — %@", owner, title] : owner;
            if (label.length)
                targets.options.append({QString::fromUtf8(label.UTF8String),
                                        [window[(NSString *)kCGWindowNumber] intValue]});
        }
        if (windows)
            CFRelease(windows);
        return {targets};
    }
    SourceProperty displays{"display_uuid", QObject::tr("Ekran"), OBS_PROPERTY_LIST, {}};
    for (NSScreen *screen in NSScreen.screens) {
        const auto number = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
        CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(number);
        if (!uuid)
            continue;
        CFStringRef string = CFUUIDCreateString(kCFAllocatorDefault, uuid);
        displays.options.append({QString::fromUtf8(screen.localizedName.UTF8String),
                                 QString::fromUtf8([(NSString *)string UTF8String])});
        CFRelease(string);
        CFRelease(uuid);
    }
    if (kind != "application")
        return {displays};
    SourceProperty apps{"application", QObject::tr("Uygulama"), OBS_PROPERTY_LIST, {}};
    for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
        if (app.activationPolicy != NSApplicationActivationPolicyRegular ||
            !app.bundleIdentifier.length)
            continue;
        apps.options.append({QString::fromUtf8(app.localizedName.UTF8String),
                             QString::fromUtf8(app.bundleIdentifier.UTF8String)});
    }
    return {displays, apps};
}
} // namespace Cast
