// SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
// SPDX-License-Ref: LGPL-2.1-or-later

#pragma once

#import <CoreFoundation/CoreFoundation.h>

/// Schedules a job to handle CEF's work for immediate execution
/// on the main thread. The function does not block.
void gst_cef_loop();
/// Schedules a timer with the given timeout to trigger handling the next
/// slice of work. The handling will take place in the main thread.
/// The function does not block.
CFRunLoopTimerRef gst_cef_domessagework(CFTimeInterval interval);
/// Hook on different lifecycle events to trigger CefShutdown.
/// These are:
/// - NSApplicationWillTerminateNotification
/// - NSEvent.NSEventMaskApplicationDefined as sent by gstmacos.mm
/// The shutdown executes only once.
void gst_cef_set_shutdown_observer();
