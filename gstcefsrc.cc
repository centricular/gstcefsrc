#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstinfo.h"
#include "gst/gstobject.h"
#include <cstdio>
#include <glib-object.h>
#include <glib.h>
#include <sstream>
#include <string>

#include <ctime>

#ifdef __APPLE__
#include <memory>
#include <string>
#include <vector>
#include <mach-o/dyld.h>
#include <dispatch/dispatch.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <include/base/cef_bind.h>
#include <include/base/cef_callback_helpers.h>
#include <include/wrapper/cef_closure_task.h>
#include <include/wrapper/cef_message_router.h>

#include "gstcefsrc.h"
#include "gstcefaudiometa.h"
#ifdef __APPLE__
#include "gstcefloader.h"
#include "gstcefnsapplication.h"
#endif

#define GST_ELEMENT_PROGRESS(el, type, code, text)      \
G_STMT_START {                                          \
  gchar *__txt = _gst_element_error_printf text;        \
  gst_element_post_message (GST_ELEMENT_CAST (el),      \
      gst_message_new_progress (GST_OBJECT_CAST (el),   \
          GST_PROGRESS_TYPE_ ##type, code, __txt));     \
  g_free (__txt);                                       \
} G_STMT_END


GST_DEBUG_CATEGORY_STATIC (cef_src_debug);
#define GST_CAT_DEFAULT cef_src_debug

GST_DEBUG_CATEGORY_STATIC (cef_console_debug);

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1
#define DEFAULT_URL "https://www.google.com"
#define DEFAULT_GPU FALSE
#define DEFAULT_CHROMIUM_DEBUG_PORT -1
#define DEFAULT_LOG_SEVERITY LOGSEVERITY_INFO
#if !defined(__APPLE__) && defined(GST_CEF_USE_SANDBOX)
#define DEFAULT_SANDBOX TRUE
#else
#define DEFAULT_SANDBOX FALSE
#endif
#define DEFAULT_LISTEN_FOR_JS_SIGNALS FALSE

using CefStatus = enum : guint8 {
  // CEF was either unloaded successfully or not yet loaded.
  CEF_STATUS_NOT_LOADED = 0U,
  // Blocks other elements from initializing CEF is it's already in progress.
  CEF_STATUS_INITIALIZING = 1U << 1U,
  // CEF's initialization process has completed successfully.
  CEF_STATUS_INITIALIZED = 1U << 2U,
  // No CEF elements will be allowed to complete initialization.
  CEF_STATUS_FAILURE = 1U << 3U,
};

static CefStatus cef_status = CEF_STATUS_NOT_LOADED;
static const guint8 CEF_STATUS_MASK_INITIALIZED = CEF_STATUS_FAILURE | CEF_STATUS_INITIALIZED;
static const guint8 CEF_STATUS_MASK_TRANSITIONING = CEF_STATUS_INITIALIZING;

static GMutex init_lock;
static GCond init_cond;

#ifdef __APPLE__
// On every timeout, the CEF event handler will be run in the context
// of the main thread's Cocoa event loop.
static CFRunLoopTimerRef workTimer_ = nullptr;
#else
static GThread *thread = nullptr;
#endif

#define GST_TYPE_CEF_LOG_SEVERITY_MODE \
  (gst_cef_log_severity_mode_get_type ())


static const GEnumValue log_severity_values[] = {
  {LOGSEVERITY_DEBUG, "debug / verbose cef log severity", "debug"},
  {LOGSEVERITY_INFO, "info cef log severity", "info"},
  {LOGSEVERITY_WARNING, "warning cef log severity", "warning"},
  {LOGSEVERITY_ERROR, "error cef log severity", "error"},
  {LOGSEVERITY_FATAL, "fatal cef log severity", "fatal"},
  {LOGSEVERITY_DISABLE, "disable cef log severity", "disable"},
  {0, NULL, NULL},
};

static GType
gst_cef_log_severity_mode_get_type (void)
{
  static GType type = 0;
  if (!type) {
    type = g_enum_register_static ("GstCefLogSeverityMode", log_severity_values);
  }
  return type;
}

static gint gst_cef_log_severity_from_str (const gchar *str)
{
  for (guint i = 0; i < sizeof(log_severity_values) / sizeof(GEnumValue); i++) {
    const gchar *nick = log_severity_values[i].value_nick;
    if (!nick) break;
    if (g_str_equal(str, nick)) {
      return log_severity_values[i].value;
    }
  }

  return -1;
}

enum
{
  PROP_0,
  PROP_URL,
  PROP_GPU,
  PROP_CHROMIUM_DEBUG_PORT,
  PROP_CHROME_EXTRA_FLAGS,
  PROP_SANDBOX,
  PROP_LISTEN_FOR_JS_SIGNAL,
  PROP_JS_FLAGS,
  PROP_LOG_SEVERITY,
  PROP_CEF_CACHE_LOCATION,
};

#define gst_cef_src_parent_class parent_class
G_DEFINE_TYPE (GstCefSrc, gst_cef_src, GST_TYPE_PUSH_SRC);

#define CEF_VIDEO_CAPS "video/x-raw, format=BGRA, width=[1, 2147483647], height=[1, 2147483647], framerate=[1/1, 60/1], pixel-aspect-ratio=1/1"
#define CEF_AUDIO_CAPS "audio/x-raw, format=F32LE, rate=[1, 2147483647], channels=[1, 2147483647], layout=interleaved"

static GstStaticPadTemplate gst_cef_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_VIDEO_CAPS)
    );

gchar* get_plugin_base_path () {
  GstPlugin *plugin = gst_registry_find_plugin(gst_registry_get(), "cef");
  gchar* base_path = g_path_get_dirname(gst_plugin_get_filename(plugin));
  gst_object_unref(plugin);
  return base_path;
}


/** Cef Client */

/** Handlers */

// see https://bitbucket.org/chromiumembedded/cef-project/src/master/examples/message_router
// and https://bitbucket.org/chromiumembedded/cef/src/master/include/wrapper/cef_message_router.h
// for details of the message passing infrastructure in CEF
// Handle messages in the browser process.
class MessageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit MessageHandler(GstCefSrc* src)
      : src(src) {}

  // Called due to gstSendMsg execution in ready_test.html.
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override
  {
    if (!src) return false;

    // TODO: do we want to make the incoming payload json??
    bool success = false;

    if (request == "ready") {
      g_mutex_lock (&src->state_lock);
      if (src->state == CEF_SRC_WAITING_FOR_READY) {
        src->state = CEF_SRC_READY;
        g_cond_broadcast (&src->state_cond);
        success = true;
      } else {
        std::ostringstream error_msg;
        error_msg << "error: (" << request << ") - " <<
          "js ready signal sent with invalid cef state: " << cef_status;
        GST_WARNING_OBJECT(src, "%s", error_msg.str().c_str());
        success = false;
      }
      g_mutex_unlock (&src->state_lock);
    } else if (request == "eos") {
      if (src) {
        gst_element_send_event(GST_ELEMENT(src), gst_event_new_eos());
        success = true;
      }
    }

    // send json response back to js
    std::ostringstream response;
    response <<
      "{ " <<
        "\"success\": \"" << (success ? "true" : "false") << "\", " <<
        "\"cmd\": \"" << request << "\"" <<
      " }";
    if (success) {
      GST_INFO_OBJECT(
        src, "js signal processed successfully: %s", request.ToString().c_str()
      );
      callback->Success(response.str());
    } else {
      GST_WARNING_OBJECT(
        src, "js signal processing error: %s", request.ToString().c_str()
      );
      callback->Failure(0, response.str());
    }

    return true;
  }

 private:
  GstCefSrc* src;

  DISALLOW_COPY_AND_ASSIGN(MessageHandler);
};

class RenderHandler : public CefRenderHandler
{
  public:

    RenderHandler(GstCefSrc *src) :
        src (src)
    {
    }

    ~RenderHandler()
    {
    }

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override
    {
	  GST_LOG_OBJECT(src, "getting view rect");
      GST_OBJECT_LOCK (src);
      rect = CefRect(0, 0, src->vinfo.width ? src->vinfo.width : DEFAULT_WIDTH, src->vinfo.height ? src->vinfo.height : DEFAULT_HEIGHT);
      GST_OBJECT_UNLOCK (src);
    }

    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void * buffer, int w, int h) override
    {
      GstBuffer *new_buffer;

      GST_LOG_OBJECT (src, "painting, width / height: %d %d", w, h);

      new_buffer = gst_buffer_new_allocate (NULL, src->vinfo.width * src->vinfo.height * 4, NULL);
      gst_buffer_fill (new_buffer, 0, buffer, w * h * 4);

      GST_OBJECT_LOCK (src);
      gst_buffer_replace (&(src->current_buffer), new_buffer);
      gst_buffer_unref (new_buffer);
      GST_OBJECT_UNLOCK (src);

      GST_LOG_OBJECT (src, "done painting");
    }

  private:

    GstCefSrc *src;

    IMPLEMENT_REFCOUNTING(RenderHandler);
};

class AudioHandler : public CefAudioHandler
{
  public:

    AudioHandler(GstCefSrc *src) :
        src (src)
    {
    }

    ~AudioHandler()
    {
    }

  void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
                            const CefAudioParameters& params,
                            int channels) override
  {
    GstStructure *s = gst_structure_new ("cef-audio-stream-start",
        "channels", G_TYPE_INT, channels,
        "rate", G_TYPE_INT, params.sample_rate,
        nullptr);
    GstEvent *event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    GST_LOG_OBJECT (src, "Audio stream starting");

    mRate = params.sample_rate;
    mChannels = channels;

    GST_OBJECT_LOCK (src);
    src->audio_events = g_list_append (src->audio_events, event);
    GST_OBJECT_UNLOCK (src);
  }

  void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
                           const float** data,
                           int frames,
                           int64_t pts) override
  {
    GstBuffer *buf;
    GstMapInfo info;
    gint i, j;

    buf = gst_buffer_new_allocate (NULL, mChannels * frames * 4, NULL);

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    for (i = 0; i < mChannels; i++) {
      gfloat *cdata = (gfloat *) data[i];

      for (j = 0; j < frames; j++) {
        memcpy (info.data + j * 4 * mChannels + i * 4, &cdata[j], 4);
      }
    }
    gst_buffer_unmap (buf, &info);

    GstClockTimeDiff dt = gst_util_uint64_scale (frames, GST_SECOND, mRate);

    // map cef pts to gstreamer pts
    GstClockTime cef_pts = pts * GST_MSECOND;
    GstClockTime gst_pts = GST_CLOCK_TIME_NONE;

    GST_OBJECT_LOCK (src);

    // calculate offset between cef clock and gst clock
    if (cefGstOffset == 0) {
      // add latency
      // NB: audio latency is required because of the way the audio data is muxed into the video
      //     buffers
      // TODO: is there a better way to determine the required audio latency here?
      GstClockTimeDiff audio_latency = (dt * 4);

      GstClockTime vpts = gst_util_uint64_scale (src->n_frames + 1, src->vinfo.fps_d * GST_SECOND, src->vinfo.fps_n);
      cefGstOffset = GST_CLOCK_DIFF (cef_pts, vpts) + audio_latency;

      GST_LOG_OBJECT(src,
                     "calculated  pts offset from cef_pts: %"
                     GST_TIME_FORMAT " to vpts %" GST_TIME_FORMAT
                     " = %li ns",
                     GST_TIME_ARGS (cef_pts),
                     GST_TIME_ARGS (vpts),
                     cefGstOffset);
    }

    cef_pts += cefGstOffset;

    GST_LOG_OBJECT (src,
                    "Handling audio stream packet with %d frames @pts %lu (cef_pts: %" GST_TIME_FORMAT ") %i",
                    frames,
                    pts,
                    GST_TIME_ARGS (cef_pts),
                    mRate);

    if (nextBufTimeGuess == GST_CLOCK_TIME_NONE) {
      // use pts from cef * 1_000_000
      gst_pts = cef_pts;
    } else {
      GstClockTimeDiff abs_error = cef_pts > nextBufTimeGuess
        ? cef_pts - nextBufTimeGuess
        : nextBufTimeGuess - cef_pts;
      // allow for a drift of 1ms
      if (abs_error < GST_MSECOND) {
        gst_pts = nextBufTimeGuess;
      } else {
        // use cef_pts
        gst_pts = cef_pts;
      }
    }

    prevBufTime = gst_pts;
    nextBufTimeGuess = gst_pts + dt;

    GST_BUFFER_DURATION (buf) = dt;
    GST_BUFFER_PTS (buf) = gst_pts;
    GST_BUFFER_DTS (buf) = gst_pts;

    if (!src->audio_buffers) {
      src->audio_buffers = gst_buffer_list_new();
    }

    gst_buffer_list_add (src->audio_buffers, buf);
    GST_OBJECT_UNLOCK (src);

    GST_LOG_OBJECT (src, "Handled audio stream packet");
  }

  void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser) override
  {
    GST_LOG_OBJECT (src, "Audio stream stopping");
  }

  void OnAudioStreamError(CefRefPtr<CefBrowser> browser,
                          const CefString& message) override {
    GST_WARNING_OBJECT (src, "Audio stream error: %s", message.ToString().c_str());
  }

  private:

    GstCefSrc *src;
    gint mRate;
    gint mChannels;

    GstClockTime nextBufTimeGuess = GST_CLOCK_TIME_NONE;
    GstClockTime prevBufTime = GST_CLOCK_TIME_NONE;
    GstClockTimeDiff cefGstOffset = 0;

    IMPLEMENT_REFCOUNTING(AudioHandler);
};

class DisplayHandler : public CefDisplayHandler {
public:
  DisplayHandler(GstCefSrc *src) : src(src) {}

  ~DisplayHandler() = default;

  virtual bool OnConsoleMessage(CefRefPtr<CefBrowser>, cef_log_severity_t level, const CefString &message, const CefString &source, int line) override {
    GstDebugLevel gst_level = GST_LEVEL_NONE;
    switch (level) {
    case LOGSEVERITY_DEFAULT:
    case LOGSEVERITY_INFO:
      gst_level = GST_LEVEL_INFO;
      break;
    case LOGSEVERITY_DEBUG:
      gst_level = GST_LEVEL_DEBUG;
      break;
    case LOGSEVERITY_WARNING:
      gst_level = GST_LEVEL_WARNING;
      break;
    case LOGSEVERITY_ERROR:
    case LOGSEVERITY_FATAL:
      gst_level = GST_LEVEL_ERROR;
      break;
    case LOGSEVERITY_DISABLE:
      gst_level = GST_LEVEL_NONE;
      break;
    };
    GST_CAT_LEVEL_LOG (cef_console_debug, gst_level, src, "%s:%d %s", source.ToString().c_str(), line,
      message.ToString().c_str());
    return false;
  }

private:
  GstCefSrc *src;
  IMPLEMENT_REFCOUNTING(DisplayHandler);
};

class BrowserClient :
  public CefClient,
  public CefLifeSpanHandler,
  public CefRequestHandler
{
  public:

    BrowserClient(GstCefSrc *src) : src(src)
    {

      this->render_handler = new RenderHandler(src);
      this->audio_handler = new AudioHandler(src);
      this->display_handler = new DisplayHandler(src);
    }

    // CefClient Methods:
    virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
    {
      return this;
    }

    virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override
    {
      return render_handler;
    }

    virtual CefRefPtr<CefAudioHandler> GetAudioHandler() override
    {
      return audio_handler;
    }

    virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override
    {
      return this;
    }

    virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
    {
      return display_handler;
    }

    bool OnProcessMessageReceived(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefProcessId source_process,
      CefRefPtr<CefProcessMessage> message
    ) override
    {
      CEF_REQUIRE_UI_THREAD();

      return browser_msg_router_
        ? browser_msg_router_->OnProcessMessageReceived(
          browser,
          frame,
          source_process,
          message
        )
        : false;
    }

    // CefLifeSpanHandler Methods:
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
      CEF_REQUIRE_UI_THREAD();

      if (src->listen_for_js_signals && !browser_msg_router_) {
        // Create the browser-side router for query handling.
        CefMessageRouterConfig config;
        config.js_query_function = "gstSendMsg";
        config.js_cancel_function = "gstCancelMsg";
        browser_msg_router_ = CefMessageRouterBrowserSide::Create(config);

        // Register handlers with the router.
        browser_msg_handler_.reset(new MessageHandler(src));
        browser_msg_router_->AddHandler(browser_msg_handler_.get(), false);
      }
    }

    virtual void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
      src->browser = nullptr;
      g_mutex_lock (&src->state_lock);
      src->state = CEF_SRC_CLOSED;
      g_cond_signal (&src->state_cond);
      g_mutex_unlock(&src->state_lock);
    }

    // CefRequestHandler methods:
    bool OnBeforeBrowse(
      CefRefPtr<CefBrowser> browser,
      CefRefPtr<CefFrame> frame,
      CefRefPtr<CefRequest> request,
      bool user_gesture,
      bool is_redirect
    ) override
    {
      CEF_REQUIRE_UI_THREAD();

      if (browser_msg_router_) browser_msg_router_->OnBeforeBrowse(browser, frame);
      return false;
    }

  virtual void OnRenderProcessTerminated(
    CefRefPtr<CefBrowser> browser,
    TerminationStatus status,
    int error_code,
    const CefString& error_string) override
    {
      CEF_REQUIRE_UI_THREAD();
      GST_WARNING_OBJECT (src, "Render subprocess terminated, reloading URL!");
      if (browser_msg_router_) browser_msg_router_->OnRenderProcessTerminated(browser);
      browser->Reload();
    }

    // Custom methods:
    void MakeBrowser(int)
    {
      CefWindowInfo window_info;
      CefRefPtr<CefBrowser> browser;
      CefBrowserSettings browser_settings;

      window_info.SetAsWindowless(0);
      browser = CefBrowserHost::CreateBrowserSync(
        window_info,
        this,
        std::string(src->url),
        browser_settings,
        nullptr,
        nullptr
      );

      browser->GetHost()->SetAudioMuted(true);

      src->browser = browser;

      g_mutex_lock (&src->state_lock);
      src->state = src->listen_for_js_signals ? CEF_SRC_WAITING_FOR_READY : CEF_SRC_OPEN;
      g_cond_signal (&src->state_cond);
      g_mutex_unlock(&src->state_lock);
    }

  private:
    // Handles the browser side of query routing.
    CefRefPtr<CefMessageRouterBrowserSide> browser_msg_router_;
    std::unique_ptr<CefMessageRouterBrowserSide::Handler> browser_msg_handler_;

    CefRefPtr<CefRenderHandler> render_handler;
    CefRefPtr<CefAudioHandler> audio_handler;
    CefRefPtr<CefDisplayHandler> display_handler;

  public:
    GstCefSrc *src;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};


/** Browser App methods */

BrowserApp::BrowserApp(GstCefSrc *src) : src(src)
{
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler()
{
  return this;
}

#ifdef __APPLE__
void BrowserApp::OnScheduleMessagePumpWork(int64_t delay_ms)
{
  static const int64_t kMaxTimerDelay = 1000.0 / 60.0;

  if (workTimer_ != nullptr) {
    CFRunLoopTimerInvalidate(workTimer_);
    workTimer_ = nullptr;
  }

  if (delay_ms <= 0) {
    // Execute the work immediately.
    gst_cef_loop();

    // Schedule more work later.
    OnScheduleMessagePumpWork(kMaxTimerDelay);
  } else {
      int64_t timer_delay_ms = delay_ms;
      // Never wait longer than the maximum allowed time.
      if (timer_delay_ms > kMaxTimerDelay) timer_delay_ms = kMaxTimerDelay;

      workTimer_ = gst_cef_domessagework((double)timer_delay_ms * (1.0 / 1000.0));

      CFRunLoopAddTimer(CFRunLoopGetMain(), workTimer_, kCFRunLoopDefaultMode);
  }
}
#endif

void BrowserApp::OnBeforeCommandLineProcessing(const CefString &process_type,
                                               CefRefPtr<CefCommandLine> command_line)
{
    command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitch("enable-media-stream");
    command_line->AppendSwitch("disable-dev-shm-usage"); /* https://github.com/GoogleChrome/puppeteer/issues/1834 */
    command_line->AppendSwitch("enable-begin-frame-scheduling"); /* https://bitbucket.org/chromiumembedded/cef/issues/1368 */

    bool gpu = src->gpu || (!!g_getenv ("GST_CEF_GPU_ENABLED"));

#ifdef __APPLE__
    command_line->AppendSwitch("off-screen-rendering-enabled");
    if (gpu) {
      GST_WARNING_OBJECT(src, "GPU rendering is known not to work on macOS. Disabling it now. See https://github.com/chromiumembedded/cef/issues/3322 and https://magpcss.org/ceforum/viewtopic.php?f=6&t=19397");
      gpu = FALSE;
    }
#endif

    if (!gpu) {
      // Optimize for no gpu usage
      command_line->AppendSwitch("disable-gpu");
      command_line->AppendSwitch("disable-gpu-compositing");
    }

    if (src->chromium_debug_port >= 0) {
      command_line->AppendSwitchWithValue("remote-debugging-port", g_strdup_printf ("%i", src->chromium_debug_port));
    }

    const gchar *extra_flags = src->chrome_extra_flags;
    if (!extra_flags) {
      extra_flags = g_getenv ("GST_CEF_CHROME_EXTRA_FLAGS");
    }

    if (extra_flags) {
      gchar **flags_list = g_strsplit (extra_flags, ",", -1);
      guint i;

      for (i = 0; i < g_strv_length (flags_list); i++) {
        gchar **switch_value = g_strsplit ((const gchar *) flags_list[i], "=", -1);

        if (g_strv_length (switch_value) > 1) {
          GST_INFO_OBJECT (src, "Adding switch with value %s=%s", switch_value[0], switch_value[1]);
          command_line->AppendSwitchWithValue (switch_value[0], switch_value[1]);
        } else {
          GST_INFO_OBJECT (src, "Adding flag %s", flags_list[i]);
          command_line->AppendSwitch (flags_list[i]);
        }

        g_strfreev (switch_value);
      }

      g_strfreev (flags_list);
    }
}


/** cefsrc (Gstreamer) methods */

static GstFlowReturn gst_cef_src_create(GstPushSrc *push_src, GstBuffer **buf)
{
  GstCefSrc *src = GST_CEF_SRC (push_src);
  GList *tmp;

  GstElement *elem = GST_ELEMENT(push_src);
  GstClockTime base_time = gst_element_get_base_time(elem);
  GstClock *clock = gst_element_get_clock(elem);
  time_t now = time(0);
  GstClockTime gst_now = gst_clock_get_time(clock);

  GST_OBJECT_LOCK (src);

  if (src->audio_events) {
    for (tmp = src->audio_events; tmp; tmp = tmp->next) {
      gst_pad_push_event (GST_BASE_SRC_PAD (src), (GstEvent *) tmp->data);
    }

    g_list_free (src->audio_events);
    src->audio_events = NULL;
  }

  g_assert (src->current_buffer);
  *buf = gst_buffer_copy (src->current_buffer);

  if (src->audio_buffers) {
    gst_buffer_add_cef_audio_meta (*buf, src->audio_buffers);
    src->audio_buffers = NULL;
  }


  GST_BUFFER_PTS (*buf) = gst_util_uint64_scale (src->n_frames, src->vinfo.fps_d * GST_SECOND, src->vinfo.fps_n);
  GST_BUFFER_DURATION (*buf) = gst_util_uint64_scale (GST_SECOND, src->vinfo.fps_d, src->vinfo.fps_n);

  GST_DEBUG_OBJECT(src,
                   "creating v_buffer pts: %" GST_TIME_FORMAT
                   " (base_time: %" GST_TIME_FORMAT " w/%s) %lu (gst: %" GST_TIME_FORMAT ")",
                   GST_TIME_ARGS (GST_BUFFER_PTS (*buf)),
                   GST_TIME_ARGS (base_time),
                   clock->object.name,
                   now,
                   GST_TIME_ARGS (gst_now));

  src->n_frames++;
  GST_OBJECT_UNLOCK (src);

  return GST_FLOW_OK;
}

/* Once we have started a first cefsrc for this process, we start
 * a UI thread and never shut it down. We could probably refine this
 * to stop and restart the thread as needed, but this updated approach
 * now no longer requires a main loop to be running, doesn't crash
 * when one is running either with CEF 86+, and allows for multiple
 * concurrent cefsrc instances.
 */
static gpointer
init_cef (GstCefSrc *src)
{
#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(0, NULL);
#endif

  CefSettings settings;
  CefRefPtr<BrowserApp> app;
  CefWindowInfo window_info;
  CefBrowserSettings browserSettings;

  // pull in parameters from gst properties (deprecated) or environment
  cef_log_severity_t log_severity = src->log_severity;
  const gchar* log_severity_env = g_getenv ("GST_CEF_LOG_SEVERITY");
  if (log_severity_env) {
    gint severity = gst_cef_log_severity_from_str(log_severity_env);
    if (severity >= 0) {
      log_severity = (cef_log_severity_t) severity;
    }
  }

  const gchar *js_flags = src->js_flags;
  if (!js_flags) {
    js_flags = g_getenv ("GST_CEF_JS_FLAGS");
  }

  const gchar *cef_cache_location = src->cef_cache_location;
  if (!cef_cache_location) {
    cef_cache_location = g_getenv ("GST_CEF_CACHE_LOCATION");
  }

  bool sandbox = src->gpu || (!!g_getenv ("GST_CEF_SANDBOX"));

  settings.no_sandbox = !sandbox;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = log_severity;
  settings.multi_threaded_message_loop = false;
#ifdef __APPLE__
  settings.external_message_pump = true;
#endif

  GST_INFO_OBJECT(src, "Initializing CEF");

  gchar* base_path = get_plugin_base_path();

  // If not absolute path append to current_dir
  if (!g_path_is_absolute(base_path)) {
    gchar* current_dir = g_get_current_dir();

    gchar* old_base_path = base_path;
    base_path = g_build_filename(current_dir, base_path, nullptr);

    g_free(current_dir);
    g_free(old_base_path);
  }

#ifdef __APPLE__
  gchar* browser_subprocess_path = g_build_filename(base_path, "gstcefsubprocess.app/Contents/MacOS/gstcefsubprocess", nullptr);
#else
  gchar* browser_subprocess_path = g_build_filename(base_path, "gstcefsubprocess", nullptr);
#endif
  if (const gchar *custom_subprocess_path = g_getenv ("GST_CEF_SUBPROCESS_PATH")) {
    g_setenv ("CEF_SUBPROCESS_PATH", browser_subprocess_path, TRUE);
    g_free (browser_subprocess_path);
    browser_subprocess_path = g_strdup (custom_subprocess_path);
  }

  GST_DEBUG_OBJECT(src, "CEF subprocess: %s", browser_subprocess_path);
  CefString(&settings.browser_subprocess_path).FromASCII(browser_subprocess_path);
  g_free(browser_subprocess_path);

#ifdef __APPLE__
  const std::string framework_folder = []() {
    std::string framework = gst_cef_get_framework_path(false);
    const auto split = framework.find_last_of('/');
    return framework.substr(0, split);
  }();
  GST_DEBUG_OBJECT(src, "CEF framework_dir_path: %s", framework_folder.c_str());
  CefString(&settings.framework_dir_path).FromString(framework_folder);
  const std::string main_bundle_folder = [&](){
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> host(size);
    _NSGetExecutablePath(host.data(), &size);
    auto host2 = std::unique_ptr<char>(realpath(host.data(), nullptr));
    GST_DEBUG_OBJECT(src, "Main executable path: %s", host2.get());
    assert(size != 0);
    std::string host3(host2.get());
    const auto split = host3.find("Contents/MacOS");
    assert(split != std::string::npos);
    return host3.substr(0, split);
  }();
  CefString(&settings.main_bundle_path).FromString(main_bundle_folder);
#endif

  gchar *locales_dir_path = g_build_filename(base_path, "locales", nullptr);
  CefString(&settings.locales_dir_path).FromASCII(locales_dir_path);

  if (js_flags != NULL) {
    CefString(&settings.javascript_flags).FromASCII(js_flags);
  }

  if (cef_cache_location != NULL) {
    CefString(&settings.cache_path).FromASCII(cef_cache_location);
  }

  g_free(base_path);
  g_free(locales_dir_path);

  app = new BrowserApp(src);

  if (!CefInitialize(args, settings, app, nullptr)) {
    GST_ERROR ("Failed to initialize CEF");

    /* unblock start () */
    g_mutex_lock (&init_lock);
    cef_status = CEF_STATUS_FAILURE;
    g_cond_broadcast (&init_cond);
    g_mutex_unlock (&init_lock);

    goto done;
  }

  g_mutex_lock (&init_lock);
  cef_status = CEF_STATUS_INITIALIZED;
  g_cond_broadcast (&init_cond);
  g_mutex_unlock (&init_lock);
#ifndef __APPLE__
  CefRunMessageLoop();
#endif

done:
  return NULL;
}

static GstStateChangeReturn
gst_cef_src_change_state(GstElement *src, GstStateChange transition)
{
  GstStateChangeReturn result = GST_STATE_CHANGE_SUCCESS;

  switch(transition)
  {
  case GST_STATE_CHANGE_NULL_TO_READY:
  {
    g_mutex_lock (&init_lock);
    // Wait till a previous CEF is dismantled or completes initialization
    while (cef_status & CEF_STATUS_MASK_TRANSITIONING)
      g_cond_wait (&init_cond, &init_lock);
    if (cef_status == CEF_STATUS_FAILURE) {
      // BAIL OUT, CEF is not loaded.
      result = GST_STATE_CHANGE_FAILURE;
    } else if (cef_status == CEF_STATUS_NOT_LOADED) {
      cef_status = CEF_STATUS_INITIALIZING;
      /* Initialize Chromium Embedded Framework */
#ifdef __APPLE__
      /* in the main thread as per Cocoa */
      if (pthread_main_np()) {
        g_mutex_unlock (&init_lock);
        init_cef ((GstCefSrc*) src);
        g_mutex_lock (&init_lock);
      } else {
        dispatch_async_f(dispatch_get_main_queue(), (GstCefSrc*)src, (dispatch_function_t)&init_cef);
        while (cef_status == CEF_STATUS_INITIALIZING)
          g_cond_wait (&init_cond, &init_lock);
      }
#else
        /* in a separate UI thread */
      thread = g_thread_new("cef-ui-thread", (GThreadFunc) init_cef, (GstCefSrc*)src);
      while (cef_status == CEF_STATUS_INITIALIZING)
        g_cond_wait (&init_cond, &init_lock);
#endif
      if (cef_status & ~CEF_STATUS_MASK_INITIALIZED) {
        // BAIL OUT, CEF is not loaded.
        result = GST_STATE_CHANGE_FAILURE;
#ifndef __APPLE__
        g_thread_join(thread);
        thread = nullptr;
#endif
      }
    }
    g_mutex_unlock(&init_lock);

    GstCefSrc *cefsrc = GST_CEF_SRC (src);
    gst_buffer_replace (&cefsrc->current_buffer, NULL);

    break;
  }
  default:
    break;
  }

  if (result == GST_STATE_CHANGE_FAILURE) return result;
  result = GST_ELEMENT_CLASS(parent_class)->change_state(src, transition);

  return result;
}

static gboolean
gst_cef_src_start(GstBaseSrc *base_src)
{
  gboolean ret = FALSE;
  GstCefSrc *src = GST_CEF_SRC (base_src);

  GST_ELEMENT_PROGRESS(src, START, "open", ("Creating CEF browser client"));

  CefRefPtr<BrowserClient> browserClient = new BrowserClient(src);

  /* Make sure CEF is initialized before posting a task */
  g_mutex_lock (&init_lock);
  while (cef_status & ~CEF_STATUS_MASK_INITIALIZED)
    g_cond_wait (&init_cond, &init_lock);
  g_mutex_unlock (&init_lock);

  if (cef_status == CEF_STATUS_FAILURE) {
    GST_ELEMENT_PROGRESS(src, ERROR, "open", ("CEF in failed state"));
    goto done;
  }

  GST_OBJECT_LOCK (src);
  src->n_frames = 0;
  GST_OBJECT_UNLOCK (src);

  GST_ELEMENT_PROGRESS(src, CONTINUE, "open", ("Creating CEF browser ..."));

#ifdef __APPLE__
  if (pthread_main_np()) {
    /* in the main thread as per Cocoa */
    browserClient->MakeBrowser(0);
  } else {
#endif
    CefPostTask(TID_UI, base::BindOnce(&BrowserClient::MakeBrowser, browserClient.get(), 0));

    /* And wait for this src's browser to have been created */
    GST_ELEMENT_PROGRESS(src, CONTINUE, "open", ("Waiting for CEF browser initialization..."));

    g_mutex_lock(&src->state_lock);
    while (!CefSrcStateIsOpen(src->state))
      g_cond_wait (&src->state_cond, &src->state_lock);
    g_mutex_unlock (&src->state_lock);
#ifdef __APPLE__
  }
#endif


  if (src->listen_for_js_signals) {
    g_mutex_lock (&src->state_lock);
    while (src->state == CEF_SRC_WAITING_FOR_READY)
      g_cond_wait (&src->state_cond, &src->state_lock);
    g_mutex_unlock (&src->state_lock);
  }

  ret = src->browser != NULL;

  if (ret) {
    GST_ELEMENT_PROGRESS(
      src, COMPLETE, "open",
      ("CEF browser created")
    );
  } else {
    GST_ELEMENT_PROGRESS(
      src, ERROR, "open",
      ("CEF browser failed to create")
    );
  }

done:
  return ret;
}

static void
gst_cef_src_close_browser(GstCefSrc *src)
{
  src->browser->GetHost()->CloseBrowser(true);
}

static gboolean
gst_cef_src_stop (GstBaseSrc *base_src)
{
  GstCefSrc *src = GST_CEF_SRC (base_src);

  GST_INFO_OBJECT (src, "Stopping");

  if (src->browser) {
    gst_cef_src_close_browser(src);
#ifdef __APPLE__
    if (!pthread_main_np()) {
#endif
      /* And wait for this src's browser to have been closed */
      g_mutex_lock(&src->state_lock);
      while (CefSrcStateIsOpen(src->state))
        g_cond_wait (&src->state_cond, &src->state_lock);
      g_mutex_unlock (&src->state_lock);
#ifdef __APPLE__
    }
#endif
  }

  gst_buffer_replace (&src->current_buffer, NULL);

  return TRUE;
}

static void
gst_cef_src_get_times (GstBaseSrc * base_src, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstClockTime timestamp = GST_BUFFER_PTS (buffer);
  GstClockTime duration = GST_BUFFER_DURATION (buffer);

  *end = timestamp + duration;
  *start = timestamp;

  GST_LOG_OBJECT (base_src, "Got times start: %" GST_TIME_FORMAT " end: %" GST_TIME_FORMAT, GST_TIME_ARGS (*start), GST_TIME_ARGS (*end));
}

static gboolean
gst_cef_src_query (GstBaseSrc * base_src, GstQuery * query)
{
  gboolean res = FALSE;
  GstCefSrc *src = GST_CEF_SRC (base_src);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      GstClockTime latency;

      if (src->vinfo.fps_n) {
        latency = gst_util_uint64_scale (GST_SECOND, src->vinfo.fps_d, src->vinfo.fps_n);
        GST_DEBUG_OBJECT (src, "Reporting latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));
        gst_query_set_latency (query, TRUE, latency, GST_CLOCK_TIME_NONE);
      }
      res = TRUE;
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (base_src, query);
      break;
  }

  return res;
}

static GstCaps *
gst_cef_src_fixate (GstBaseSrc * base_src, GstCaps * caps)
{
  GstStructure *structure;

  caps = gst_caps_make_writable (caps);
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);

  if (gst_structure_has_field (structure, "framerate"))
    gst_structure_fixate_field_nearest_fraction (structure, "framerate", DEFAULT_FPS_N, DEFAULT_FPS_D);
  else
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N, DEFAULT_FPS_D, nullptr);


  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (base_src, caps);

  GST_INFO_OBJECT (base_src, "Fixated caps to %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_cef_src_set_caps (GstBaseSrc * base_src, GstCaps * caps)
{
  GstCefSrc *src = GST_CEF_SRC (base_src);
  gboolean ret = TRUE;
  GstBuffer *new_buffer;

  GST_INFO_OBJECT (base_src, "Caps set to %" GST_PTR_FORMAT, caps);

  GST_OBJECT_LOCK (src);
  gst_video_info_from_caps (&src->vinfo, caps);
  new_buffer = gst_buffer_new_allocate (NULL, src->vinfo.width * src->vinfo.height * 4, NULL);
  gst_buffer_replace (&(src->current_buffer), new_buffer);
  gst_buffer_unref (new_buffer);
  src->browser->GetHost()->SetWindowlessFrameRate(gst_util_uint64_scale (1, src->vinfo.fps_n, src->vinfo.fps_d));
  src->browser->GetHost()->WasResized();
  GST_OBJECT_UNLOCK (src);

  return ret;
}

static void
gst_cef_src_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstCefSrc *src = GST_CEF_SRC (object);

  switch (prop_id) {
    case PROP_URL:
    {
      const gchar *url;

      url = g_value_get_string (value);
      g_free (src->url);
      src->url = g_strdup (url);

      g_mutex_lock(&src->state_lock);
      if (CefSrcStateIsOpen(src->state)) {
        src->browser->GetMainFrame()->LoadURL(src->url);
      }
      g_mutex_unlock(&src->state_lock);

      break;
    }
    case PROP_CHROME_EXTRA_FLAGS: {
      GST_WARNING_OBJECT(
        src,
        "cefsrc chrome-extra-flags property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_CHROME_EXTRA_FLAGS instead"
      );
      g_free (src->chrome_extra_flags);
      src->chrome_extra_flags = g_value_dup_string (value);
      break;
    }
    case PROP_GPU:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc gpu property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_GPU_ENABLED instead"
      );
      src->gpu = g_value_get_boolean (value);
      break;
    }
    case PROP_CHROMIUM_DEBUG_PORT:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc chromium-debug-port property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_CHROME_EXTRA_FLAGS instead"
      );
      src->chromium_debug_port = g_value_get_int (value);
      break;
    }
    case PROP_SANDBOX:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc sandbox property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_SANDBOX instead"
      );
      src->sandbox = g_value_get_boolean (value);
      break;
    }
    case PROP_LISTEN_FOR_JS_SIGNAL:
    {
      src->listen_for_js_signals = g_value_get_boolean (value);
      break;
    }
    case PROP_JS_FLAGS:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc js-flags property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_JS_FLAGS instead"
      );
      g_free (src->js_flags);
      src->js_flags = g_value_dup_string (value);
      break;
    }
    case PROP_LOG_SEVERITY:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc log-severity property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_LOG_SEVERITY instead"
      );
      src->log_severity = (cef_log_severity_t) g_value_get_enum (value);
      break;
    }
    case PROP_CEF_CACHE_LOCATION:
    {
      GST_WARNING_OBJECT(
        src,
        "cefsrc cef-cache-location property is deprecated and is global across all cefsrc instances - "
        "set GST_CEF_CACHE_LOCATION instead"
      );
      g_free (src->cef_cache_location);
      src->cef_cache_location = g_value_dup_string (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cef_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstCefSrc *src = GST_CEF_SRC (object);

  switch (prop_id) {
    case PROP_URL:
      g_value_set_string (value, src->url);
      break;
    case PROP_CHROME_EXTRA_FLAGS:
      g_value_set_string (value, src->chrome_extra_flags);
      break;
    case PROP_GPU:
      g_value_set_boolean (value, src->gpu);
      break;
    case PROP_CHROMIUM_DEBUG_PORT:
      g_value_set_int (value, src->chromium_debug_port);
      break;
    case PROP_SANDBOX:
      g_value_set_boolean (value, src->sandbox);
      break;
    case PROP_LISTEN_FOR_JS_SIGNAL:
      g_value_set_boolean (value, src->listen_for_js_signals);
      break;
    case PROP_JS_FLAGS:
      g_value_set_string (value, src->js_flags);
      break;
    case PROP_LOG_SEVERITY:
      g_value_set_enum (value, src->log_severity);
      break;
    case PROP_CEF_CACHE_LOCATION:
      g_value_set_string (value, src->cef_cache_location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cef_src_finalize (GObject *object)
{
  GstCefSrc *src = GST_CEF_SRC (object);

  if (src->audio_buffers) {
    gst_buffer_list_unref (src->audio_buffers);
    src->audio_buffers = NULL;
  }

  g_list_free_full (src->audio_events, (GDestroyNotify) gst_event_unref);
  src->audio_events = NULL;

  g_free (src->js_flags);
  g_free (src->cef_cache_location);

  g_cond_clear(&src->state_cond);
  g_mutex_clear(&src->state_lock);
}

static void
gst_cef_src_init (GstCefSrc * src)
{
  GstBaseSrc *base_src = GST_BASE_SRC (src);

  src->n_frames = 0;
  src->current_buffer = NULL;
  src->audio_buffers = NULL;
  src->audio_events = NULL;
  src->state = CEF_SRC_CLOSED;
  src->chromium_debug_port = DEFAULT_CHROMIUM_DEBUG_PORT;
  src->sandbox = DEFAULT_SANDBOX;
  src->listen_for_js_signals = DEFAULT_LISTEN_FOR_JS_SIGNALS;
  src->js_flags = NULL;
  src->log_severity = DEFAULT_LOG_SEVERITY;
  src->cef_cache_location = NULL;

  gst_base_src_set_format (base_src, GST_FORMAT_TIME);
  gst_base_src_set_live (base_src, TRUE);

  g_cond_init (&src->state_cond);
  g_mutex_init (&src->state_lock);
}

static void
gst_cef_src_class_init (GstCefSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
  GstPushSrcClass *push_src_class = GST_PUSH_SRC_CLASS(klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS(klass);

  gobject_class->set_property = gst_cef_src_set_property;
  gobject_class->get_property = gst_cef_src_get_property;
  gobject_class->finalize = gst_cef_src_finalize;

  g_object_class_install_property (gobject_class, PROP_URL,
      g_param_spec_string ("url", "url",
          "The URL to display",
          DEFAULT_URL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT)));

  g_object_class_install_property (gobject_class, PROP_GPU,
    g_param_spec_boolean ("gpu", "gpu",
          "Enable GPU usage in chromium (Improves performance if you have GPU) - "
          "deprecated: set GST_CEF_GPU_ENABLED in the environment instead",
          DEFAULT_GPU, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_CHROMIUM_DEBUG_PORT,
    g_param_spec_int ("chromium-debug-port", "chromium-debug-port",
          "Set chromium debug port (-1 = disabled) - "
          "deprecated: set GST_CEF_CHROME_EXTRA_FLAGS in the environment instead", -1, G_MAXUINT16,
          DEFAULT_CHROMIUM_DEBUG_PORT, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_CHROME_EXTRA_FLAGS,
    g_param_spec_string ("chrome-extra-flags", "chrome-extra-flags",
          "Comma delimiter flags to be passed into chrome "
          "(Example: show-fps-counter,remote-debugging-port=9222) - "
          "deprecated: set GST_CEF_CHROME_EXTRA_FLAGS in the environment instead",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_SANDBOX,
    g_param_spec_boolean ("sandbox", "sandbox",
          "Toggle chromium sandboxing capabilities - "
          "deprecated: set GST_CEF_SANDBOX in the environment instead",
          DEFAULT_SANDBOX, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));
  g_object_class_install_property (gobject_class, PROP_LISTEN_FOR_JS_SIGNAL,
    g_param_spec_boolean ("listen-for-js-signals", "listen-for-js-signals",
          "Listen and respond to signals sent from javascript: "
          "window.gstSendMsg({request: \"ready|eos\", ...}) - "
          "see [README](https://github.com/centricular/gstcefsrc?tab=readme-ov-file#javascript-signals) "
          "for more detail",
          DEFAULT_LISTEN_FOR_JS_SIGNALS, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_JS_FLAGS,
    g_param_spec_string ("js-flags", "js-flags",
          "Space delimited JavaScript flags to be passed to Chromium "
          "(Example: --noexpose_wasm --expose-gc) - "
          "deprecated: set GST_CEF_JS_FLAGS in the environment instead",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_LOG_SEVERITY,
      g_param_spec_enum ("log-severity", "log-severity",
          "CEF log severity level - "
          "deprecated: set GST_CEF_LOG_SEVERITY in the environment instead",
          GST_TYPE_CEF_LOG_SEVERITY_MODE, DEFAULT_LOG_SEVERITY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_CEF_CACHE_LOCATION,
    g_param_spec_string ("cef-cache-location", "cef-cache-location",
          "Cache location for CEF. Defaults to in memory cache. "
          "(Example: /tmp/cef-cache/) - "
          "deprecated: set GST_CEF_CACHE_LOCATION in the environment instead",
          NULL, (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  gst_element_class_set_static_metadata (gstelement_class,
      "Chromium Embedded Framework source", "Source/Video",
      "Creates a video stream from an embedded Chromium browser",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_src_template);

  base_src_class->fixate = GST_DEBUG_FUNCPTR(gst_cef_src_fixate);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR(gst_cef_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR(gst_cef_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR(gst_cef_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR(gst_cef_src_get_times);
  base_src_class->query = GST_DEBUG_FUNCPTR(gst_cef_src_query);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR(gst_cef_src_change_state);

  push_src_class->create = GST_DEBUG_FUNCPTR(gst_cef_src_create);

  GST_DEBUG_CATEGORY_INIT (cef_src_debug, "cefsrc", 0,
      "Chromium Embedded Framework Source");
  GST_DEBUG_CATEGORY_INIT (cef_console_debug, "cefconsole", 0,
      "Chromium Embedded Framework JS Console");
}
