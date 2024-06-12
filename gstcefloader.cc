// SPDX-FileCopyrightText: 2024 L. E. Segovia <amy@centricular.com>
// SPDX-License-Ref: LGPL-2.1-or-later

#ifdef GST_CEF_USE_SANDBOX
#include "gstcefloader.h"

#include <include/wrapper/cef_library_loader.h>
#include <dlfcn.h>
#include <glib/gstdio.h>
#include <libgen.h>
#include <mach-o/dyld.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <sstream>
#include <vector>

namespace {
const char kFrameworkPath[] =
    "Chromium Embedded Framework.framework/Chromium Embedded Framework";
const char kPathFromHelperExe[] = "../../..";
const char kPathFromMainExe[] = "../Frameworks";

std::string GetFrameworkFromExecutablePath(bool helper)
{
  // First alternative: try a path relative to the current executable.
  uint32_t exec_path_size = 0;
  int rv = _NSGetExecutablePath(NULL, &exec_path_size);
  if (rv != -1) {
    return {};
  }

  std::vector<char> exec_path(exec_path_size);
  rv = _NSGetExecutablePath(exec_path.data(), &exec_path_size);
  if (rv != 0) {
    return {};
  }

  // Get the directory path of the executable.
  std::unique_ptr<char> parent_dir(g_path_get_dirname(exec_path.data()));
  if (!parent_dir) {
    return {};
  }

  // Append the relative path to the framework.
  std::ostringstream ss;
  ss << parent_dir.get() << "/"
     << (helper ? kPathFromHelperExe : kPathFromMainExe) << "/"
     << kFrameworkPath;
  return ss.str();
}

std::string GetFrameworkAsSibling(bool helper)
{
  Dl_info function_info;
  if (dladdr(reinterpret_cast<const void *>(&GetFrameworkFromExecutablePath),
             &function_info) == 0) {
    return {};
  }

  std::unique_ptr<char> parent_dir(g_path_get_dirname(function_info.dli_fname));
  if (!parent_dir) {
    return {};
  }

  std::ostringstream ss;
  ss << parent_dir.get() << "/" << kFrameworkPath;
  return ss.str();
}
} // namespace

std::string gst_cef_get_framework_path(bool helper)
{
  std::string framework = GetFrameworkFromExecutablePath(helper);
  if (g_access(framework.c_str(), F_OK) == 0) {
    return framework;
  }

  framework = GetFrameworkAsSibling(helper);

  if (g_access(framework.c_str(), F_OK) == 0) {
    return framework;
  }

  return {};
}

gboolean gst_initialize_cef(bool helper)
{
  const std::string framework_path = gst_cef_get_framework_path(helper);
  if (framework_path.empty()) {
    fprintf(stderr, "Cannot initialize CEF: App does not have the expected "
                    "bundle structure.\n");
    return FALSE;
  }

  // Load the CEF framework library.
  if (!cef_load_library(framework_path.c_str())) {
    fprintf(stderr,
            "Cannot initialize CEF: Failed to load the CEF framework.\n");
    return FALSE;
  }

  return TRUE;
}
#endif
