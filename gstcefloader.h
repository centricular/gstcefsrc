// SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
// SPDX-License-Ref: LGPL-2.1-or-later

#pragma once

#include <glib.h>
#include <string>

#ifdef GST_CEF_USE_SANDBOX
gboolean gst_initialize_cef(bool helper);
#endif

std::string gst_cef_get_framework_path(bool helper);
