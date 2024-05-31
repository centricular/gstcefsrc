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

#include "gstcefsrc.h"
#include "gstcefdemux.h"
#include "gstcefbin.h"
#ifdef GST_CEF_USE_SANDBOX
#include "gstcefloader.h"
#endif

static gboolean
plugin_init(GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "cefsrc", GST_RANK_NONE, GST_TYPE_CEF_SRC) ||
      !gst_element_register (plugin, "cefdemux", GST_RANK_NONE, GST_TYPE_CEF_DEMUX) ||
      !gst_element_register (plugin, "cefbin", GST_RANK_NONE, GST_TYPE_CEF_BIN))
    return FALSE;

#ifdef GST_CEF_USE_SANDBOX
  return gst_initialize_cef(FALSE);
#else
  return TRUE;
#endif
}

#define PACKAGE "gstcef"

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  cef,
                  "Chromium Embedded src plugin",
                  plugin_init, "1.0", "LGPL", PACKAGE, "centricular.com")
