// SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
// SPDX-License-Ref: LGPL-2.1-or-later

#include "gstcefsrc.h"
#include "gstcefnsapplication.h"

#include <AppKit/AppKit.h>
#import <Appkit/AppKit.h>
#import <Cocoa/Cocoa.h>
#include <CoreFoundation/CFRunLoop.h>
#include <include/base/cef_lock.h>
#include <include/cef_app.h>
#include <include/cef_application_mac.h>
#include <objc/runtime.h>
#include <syslog.h>

#include <cinttypes>
#include <set>

namespace {
bool g_handling_send_event = false;
} // namespace

@interface NSApplication (GstCEFApplication) <CefAppProtocol>
- (void)runCefEventLoop;
- (BOOL)isHandlingSendEvent;
- (void)setHandlingSendEvent:(BOOL)handlingSendEvent;
- (void)_swizzled_sendEvent:(NSEvent *)event;
- (void)applicationWillTerminate:(NSNotification *)notification;
@end

void gst_cef_loop() {
  auto *app = [NSApplication sharedApplication];
  [app performSelectorOnMainThread:@selector(runCefEventLoop)
                        withObject:app
                     waitUntilDone:NO];
}

CFRunLoopTimerRef gst_cef_domessagework(CFTimeInterval interval) {
  // syslog(LOG_DEBUG, "Scheduling work... %f", interval);
  return (CFRunLoopTimerRef)
      [NSTimer timerWithTimeInterval:(NSTimeInterval)interval
                              target:[NSApplication sharedApplication]
                            selector:@selector(runCefEventLoop)
                            userInfo:NULL
                             repeats:YES];
}

@implementation NSApplication (GstCEFApplication)

- (void)runCefEventLoop {
  CefDoMessageLoopWork();
}

// This selector is called very early during the application initialization.
+ (void)load {
  if ([NSThread currentThread] != [NSThread mainThread]) {
    syslog(LOG_ERR, "Swizzling from outside the main thread, relocating...");
    [NSApplication performSelectorOnMainThread:@selector(load)
                                    withObject:self
                                 waitUntilDone:NO];
    return;
  }
  syslog(LOG_NOTICE, "Swizzling NSApplication for use with CEF...");

  auto swizzle = [&](auto src, auto dst) {
    Method originalTerm = class_getInstanceMethod(self, src);
    Method swizzledTerm = class_getInstanceMethod(self, dst);
    assert(originalTerm != nullptr && swizzledTerm != nullptr);
    method_exchangeImplementations(originalTerm, swizzledTerm);
  };

  swizzle(@selector(sendEvent:), @selector(_swizzled_sendEvent:));
  syslog(LOG_NOTICE, "Swizzling complete.");
}

- (BOOL)isHandlingSendEvent {
  // syslog(LOG_DEBUG, "Is handling [NSEvent sendEvent]: %i",
  //        g_handling_send_event);
  return g_handling_send_event;
}

- (void)setHandlingSendEvent:(BOOL)handlingSendEvent {
  // syslog(LOG_DEBUG, "Set handling [NSEvent sendEvent]: %i",
  // handlingSendEvent);
  g_handling_send_event = handlingSendEvent;
}

- (void)_swizzled_sendEvent:(NSEvent *)event {
  CefScopedSendingEvent sendingEventScoper;
  // syslog(LOG_DEBUG, "[NSEvent sendEvent]: %p", event);
  // Calls NSApplication::sendEvent due to the swizzling.
  [self _swizzled_sendEvent:event];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  gst_cef_unload();
}
@end

void gst_cef_set_shutdown_observer() {
  assert([[NSApplication sharedApplication]
      respondsToSelector:@selector(applicationWillTerminate:)]);
  [[NSNotificationCenter defaultCenter]
      addObserver:[NSApplication sharedApplication]
         selector:@selector(applicationWillTerminate:)
             name:NSApplicationWillTerminateNotification
           object:[NSApplication sharedApplication]];
  [NSEvent
      addLocalMonitorForEventsMatchingMask:NSEventMaskApplicationDefined
                                   handler:^(NSEvent *_Nonnull event) {
                                     if ([event subtype] ==
                                             NSEventSubtypeApplicationActivated &&
                                         NSEqualPoints([event locationInWindow],
                                                       NSZeroPoint))
                                       gst_cef_unload();
                                     return event;
                                   }];
}
