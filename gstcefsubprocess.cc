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

#include "include/wrapper/cef_message_router.h"
#include <include/cef_app.h>
#include <glib.h>

#if defined(__APPLE__)
#include "gstcefloader.h"
#if defined(GST_CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif
#endif


enum ProcessType {
  PROCESS_TYPE_BROWSER,
  PROCESS_TYPE_RENDERER,
  PROCESS_TYPE_OTHER,
};

// These flags must match the Chromium values.
const char kProcessType[] = "type";
const char kRendererProcess[] = "renderer";
#if defined(OS_LINUX)
const char kZygoteProcess[] = "zygote";
#endif

CefRefPtr<CefCommandLine> CreateCommandLine(const CefMainArgs& main_args) {
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
#if defined(OS_WIN)
  command_line->InitFromString(::GetCommandLineW());
#else
  command_line->InitFromArgv(main_args.argc, main_args.argv);
#endif
  return command_line;
}

ProcessType GetProcessType(const CefMainArgs& main_args) {
  // Create a temporary CommandLine object.
  CefRefPtr<CefCommandLine> command_line = CreateCommandLine(main_args);
  // The command-line flag won't be specified for the browser process.
  if (!command_line->HasSwitch(kProcessType))
    return PROCESS_TYPE_BROWSER;

  const std::string& process_type = command_line->GetSwitchValue(kProcessType);
  if (process_type == kRendererProcess)
    return PROCESS_TYPE_RENDERER;

#if defined(OS_LINUX)
  // On Linux the zygote process is used to spawn other process types. Since we
  // don't know what type of process it will be we give it the renderer app.
  if (process_type == kZygoteProcess)
    return PROCESS_TYPE_RENDERER;
#endif

  return PROCESS_TYPE_OTHER;
}

// Implementation of CefApp for the renderer process.
class RendererApp : public CefApp, public CefRenderProcessHandler {
 public:
  RendererApp() {}

  // CefApp methods:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefRenderProcessHandler methods:
  void OnWebKitInitialized() override {
    // Create the renderer-side router for query handling.
    CefMessageRouterConfig config;
    config.js_query_function = "gstSendMsg";
    config.js_cancel_function = "gstCancelMsg";
    renderer_msg_router_ = CefMessageRouterRendererSide::Create(config);
  }

  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    renderer_msg_router_->OnContextCreated(browser, frame, context);
  }

  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    renderer_msg_router_->OnContextReleased(browser, frame, context);
  }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override {
    return renderer_msg_router_->OnProcessMessageReceived(
      browser, frame, source_process, message);
  }

 private:
  // Handles the renderer side of query routing.
  CefRefPtr<CefMessageRouterRendererSide> renderer_msg_router_;

  IMPLEMENT_REFCOUNTING(RendererApp);
  DISALLOW_COPY_AND_ASSIGN(RendererApp);
};

int main(int argc, char * argv[])
{
#if defined(__APPLE__) && defined(GST_CEF_USE_SANDBOX)
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

#if defined(__APPLE__) && defined(GST_CEF_USE_SANDBOX)
  // Try loading like an app bundle (1) or as
  // a sibling (2, at development time).
  if (!gst_initialize_cef(TRUE) && !gst_initialize_cef(FALSE)) {
    fprintf(stderr, "Cannot load CEF into gstcefsubprocess.");
    return -1;
  }
#endif

  // NB: this function is executed on new thread per new CEF "process" / app
  CefRefPtr<CefApp> app = nullptr;
  switch (GetProcessType(args)) {
    case PROCESS_TYPE_RENDERER:
      app = new RendererApp();
      break;
    case PROCESS_TYPE_BROWSER:
      // browser app created in main thread / executable
      break;
    case PROCESS_TYPE_OTHER:
      // this is unused??
      break;
  }

  return CefExecuteProcess(args, app, nullptr);
}
