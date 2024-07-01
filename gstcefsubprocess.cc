/* gstcef
 * Copyright (C) <2018> Mathieu Duponchelle <mathieu@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <include/cef_app.h>
#include <glib.h>

#ifdef GST_CEF_USE_SANDBOX
#include "gstcefloader.h"
#endif

#if !defined(__APPLE__) && defined(GST_CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

int main(int argc, char * argv[])
{
#if !defined(__APPLE__) && defined(GST_CEF_USE_SANDBOX)
  // Initialize the macOS sandbox for this helper process.
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) {
    fprintf(stderr, "Cannot initialize CEF sandbox for gstcefsubprocess.");
    return -1;
  }
#endif

  CefSettings settings;

#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(argc, argv);
#endif

#ifdef GST_CEF_USE_SANDBOX
  // Try loading like an app bundle (1) or as
  // a sibling (2, at development time).
  if (!gst_initialize_cef(TRUE) && !gst_initialize_cef(FALSE)) {
    fprintf(stderr, "Cannot load CEF into gstcefsubprocess.");
    return -1;
  }
#endif

  return CefExecuteProcess(args, nullptr, nullptr);
}
