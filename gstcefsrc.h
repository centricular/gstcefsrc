#ifndef __GST_CEF_SRC_H__
#define __GST_CEF_SRC_H__

#include "gst/gstclock.h"
#include "include/cef_browser_process_handler.h"
#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/wrapper/cef_helpers.h>


G_BEGIN_DECLS

#define GST_TYPE_CEF_SRC \
  (gst_cef_src_get_type())
#define GST_CEF_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEF_SRC,GstCefSrc))
#define GST_CEF_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEF_SRC,GstCefSrcClass))
#define GST_IS_CEF_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEF_SRC))
#define GST_IS_CEF_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEF_SRC))

typedef struct _GstCefSrc GstCefSrc;
typedef struct _GstCefSrcClass GstCefSrcClass;

typedef enum : guint8 {
  // browser app not yet initialized
  CEF_SRC_CLOSED            = 0,
  // browser app initialized
  CEF_SRC_OPEN              = 1,
  // following states only possible if `listen_for_js_signals`:
  // waiting for CEF browser to send "ready" message (using window.gstSendMsg)
  CEF_SRC_WAITING_FOR_READY = 2,
  // received ready signal from webpage
  CEF_SRC_READY             = 3,
} CefSrcState;

#define CefSrcStateIsOpen(state) (state >= CEF_SRC_OPEN)

struct _GstCefSrc {
  GstPushSrc parent;
  GstBuffer *current_buffer;
  GstBufferList *audio_buffers;
  GList *audio_events;
  GstVideoInfo vinfo;
  guint64 n_frames;
  gulong cef_work_id;
  gchar *url;
  gchar *chrome_extra_flags;
  gchar *js_flags;
  cef_log_severity_t log_severity;
  gchar *cef_cache_location;
  gboolean gpu;
  gboolean sandbox;
  gboolean listen_for_js_signals;
  gint chromium_debug_port;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefApp> app;

  GCond state_cond;
  GMutex state_lock;
  CefSrcState state;
};

struct _GstCefSrcClass {
  GstPushSrcClass parent_class;
};

class BrowserApp : public CefApp, public CefBrowserProcessHandler {
public:
  BrowserApp(GstCefSrc *src);

  void OnBeforeCommandLineProcessing(const CefString &process_type,
                                     CefRefPtr<CefCommandLine> command_line) override;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;
#ifdef __APPLE__
  void OnScheduleMessagePumpWork(int64_t delay_ms) override;
#endif
private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
  GstCefSrc *src;
};

GType gst_cef_src_get_type (void);

G_END_DECLS

#endif /* __GST_CEF_SRC_H__ */
