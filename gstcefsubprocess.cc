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

#include "gstcefloader.h"

int main(int argc, char * argv[])
{

  CefSettings settings;

#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(argc, argv);
#endif

#ifdef GST_CEF_USE_SANDBOX
  if (!gst_initialize_cef(TRUE)) {
    return -1;
  }
#endif

  return CefExecuteProcess(args, nullptr, nullptr);
}
