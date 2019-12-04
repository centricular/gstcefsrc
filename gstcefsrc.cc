#include <stdio.h>
#include <iostream>
#include <sstream>

#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/wrapper/cef_helpers.h>

#include "gstcefsrc.h"
#include "gstcefaudiometa.h"

GST_DEBUG_CATEGORY_STATIC (cef_src_debug);
#define GST_CAT_DEFAULT cef_src_debug

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1
#define DEFAULT_URL "https://www.google.com"

enum
{
  PROP_0,
  PROP_URL,
};

#define gst_cef_src_parent_class parent_class
G_DEFINE_TYPE (GstCefSrc, gst_cef_src, GST_TYPE_PUSH_SRC);

#define CEF_VIDEO_CAPS "video/x-raw, format=BGRA, width=[1, 2147483647], height=[1, 2147483647], framerate=[1/1, 60/1]"
#define CEF_AUDIO_CAPS "audio/x-raw, format=F32BE, rate=[1, 2147483647], channels=[1, 2147483647], layout=interleaved"

static GstStaticPadTemplate gst_cef_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_VIDEO_CAPS)
    );

class RenderHandler : public CefRenderHandler
{
  public:

    RenderHandler(GstCefSrc *element) :
        element (element)
    {
    }

    ~RenderHandler()
    {
    }

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override
    {
	  GST_LOG_OBJECT(element, "getting view rect");
      GST_OBJECT_LOCK (element);
      rect = CefRect(0, 0, element->vinfo.width ? element->vinfo.width : DEFAULT_WIDTH, element->vinfo.height ? element->vinfo.height : DEFAULT_HEIGHT);
      GST_OBJECT_UNLOCK (element);
    }

    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void * buffer, int w, int h) override
    {
      GstBuffer *new_buffer;

      GST_LOG_OBJECT (element, "painting, width / height: %d %d", w, h);

      new_buffer = gst_buffer_new_allocate (NULL, element->vinfo.width * element->vinfo.height * 4, NULL);
      gst_buffer_fill (new_buffer, 0, buffer, w * h * 4);

      GST_OBJECT_LOCK (element);
      gst_buffer_replace (&(element->current_buffer), new_buffer);
      gst_buffer_unref (new_buffer);
      GST_OBJECT_UNLOCK (element);

      GST_LOG_OBJECT (element, "done painting");
    }

  private:

    GstCefSrc *element;

    IMPLEMENT_REFCOUNTING(RenderHandler);
};

class AudioHandler : public CefAudioHandler
{
  public:

    AudioHandler(GstCefSrc *element) :
        element (element)
    {
    }

    ~AudioHandler()
    {
    }

  void OnAudioStreamStarted(CefRefPtr<CefBrowser> browser,
                            int audio_stream_id,
                            int channels,
                            ChannelLayout channel_layout,
                            int sample_rate,
                            int frames_per_buffer) override
  {
    GstStructure *s = gst_structure_new ("cef-audio-stream-start",
        "id", G_TYPE_INT, audio_stream_id,
        "channels", G_TYPE_INT, channels,
        "rate", G_TYPE_INT, sample_rate,
        NULL);
    GstEvent *event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    GST_OBJECT_LOCK (element);
    element->audio_events = g_list_append (element->audio_events, event);
    GST_OBJECT_UNLOCK (element);

    streamChannels.insert(std::make_pair(audio_stream_id, channels));
  }

  void OnAudioStreamPacket(CefRefPtr<CefBrowser> browser,
                           int audio_stream_id,
                           const float** data,
                           int frames,
                           int64_t pts) override
  {
    GstBuffer *buf;
    GstMapInfo info;
    gint i, j;
    int channels;
    GstBufferList *buffers;

    channels = streamChannels[audio_stream_id];

    GST_LOG_OBJECT (element, "Handling audio stream packet with %d frames", frames);

    buf = gst_buffer_new_allocate (NULL, channels * frames * 4, NULL);

    gst_buffer_map (buf, &info, GST_MAP_WRITE);
    for (i = 0; i < channels; i++) {
      gfloat *cdata = (gfloat *) data[i];

      for (j = 0; j < frames; j++) {
        memcpy (info.data + j * 4 * channels + i * 4, &cdata[j], 4);
      }
    }
    gst_buffer_unmap (buf, &info);

    GST_OBJECT_LOCK (element);
    if (!g_hash_table_contains (element->audio_buffers, GINT_TO_POINTER (audio_stream_id))) {
      buffers = gst_buffer_list_new ();
      g_hash_table_insert (element->audio_buffers, GINT_TO_POINTER (audio_stream_id), buffers);
    } else {
      buffers = (GstBufferList *) g_hash_table_lookup (element->audio_buffers, GINT_TO_POINTER (audio_stream_id));
    }

    gst_buffer_list_add (buffers, buf);
    GST_OBJECT_UNLOCK (element);

    GST_LOG_OBJECT (element, "Handled audio stream packet");
  }

  void OnAudioStreamStopped(CefRefPtr<CefBrowser> browser,
                            int audio_stream_id) override
  {
    GstStructure *s = gst_structure_new ("cef-audio-stream-stop",
        "id", G_TYPE_INT, audio_stream_id,
        NULL);
    GstEvent *event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s);

    GST_OBJECT_LOCK (element);
    element->audio_events = g_list_append (element->audio_events, event);
    GST_OBJECT_UNLOCK (element);
  }

  private:

    GstCefSrc *element;
    std::map<int, int> streamChannels;
    IMPLEMENT_REFCOUNTING(AudioHandler);
};

class BrowserClient : public CefClient
{
  public:

    BrowserClient(CefRefPtr<CefRenderHandler> rptr, CefRefPtr<CefAudioHandler> aptr) :
        render_handler(rptr),
        audio_handler(aptr)
    {
    }

    virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override
    {
      return render_handler;
    }

    virtual CefRefPtr<CefAudioHandler> GetAudioHandler() override
    {
      return audio_handler;
    }

  private:

    CefRefPtr<CefRenderHandler> render_handler;
    CefRefPtr<CefAudioHandler> audio_handler;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

class App : public CefApp
{
  public:
    App() {}

  virtual void OnBeforeCommandLineProcessing(const CefString &process_type,
                                             CefRefPtr<CefCommandLine> command_line) override
  {
    command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    command_line->AppendSwitch("enable-media-stream");
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-dev-shm-usage"); /* https://github.com/GoogleChrome/puppeteer/issues/1834 */
    command_line->AppendSwitch("disable-gpu-compositing");
  }

 private:
  IMPLEMENT_REFCOUNTING(App);
};

static gboolean
cef_do_work_func(GstCefSrc *src)
{
  GST_LOG_OBJECT (src, "Making CEF work");
  CefDoMessageLoopWork();
  GST_LOG_OBJECT (src, "Made CEF work");

  return G_SOURCE_CONTINUE;
}

static gboolean
gst_cef_src_add_audio_meta (gint stream_id, GstBufferList *buffers, GstBuffer *buf)
{
  gst_buffer_add_cef_audio_meta (buf, gst_buffer_list_ref (buffers), stream_id);

  return TRUE;
}

static GstFlowReturn gst_cef_src_create(GstPushSrc *push_src, GstBuffer **buf)
{
  GstCefSrc *src = GST_CEF_SRC (push_src);
  GList *tmp;

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

  g_hash_table_foreach_remove (src->audio_buffers, (GHRFunc) gst_cef_src_add_audio_meta, *buf);

  GST_BUFFER_PTS (*buf) = gst_util_uint64_scale (src->n_frames, src->vinfo.fps_d * GST_SECOND, src->vinfo.fps_n);
  GST_BUFFER_DURATION (*buf) = gst_util_uint64_scale (GST_SECOND, src->vinfo.fps_d, src->vinfo.fps_n);
  src->n_frames++;
  GST_OBJECT_UNLOCK (src);

  return GST_FLOW_OK;
}

static gboolean
gst_cef_src_start(GstBaseSrc *base_src)
{
  gboolean ret = FALSE;
  GstCefSrc *src = GST_CEF_SRC (base_src);

#ifdef G_OS_WIN32
  HINSTANCE hInstance = GetModuleHandle(NULL);
  CefMainArgs args(hInstance);
#else
  CefMainArgs args(0, NULL);
#endif

  CefSettings settings;
  CefRefPtr<RenderHandler> renderHandler = new RenderHandler(src);
  CefRefPtr<AudioHandler> audioHandler = new AudioHandler(src);
  CefRefPtr<App> app;
  CefRefPtr<BrowserClient> browserClient;
  CefRefPtr<CefBrowser> browser;
  CefWindowInfo window_info;
  CefBrowserSettings browserSettings;

  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  settings.log_severity = LOGSEVERITY_DISABLE;

  GST_INFO_OBJECT  (src, "Starting up");

  /* FIXME: won't work installed */
  CefString(&settings.browser_subprocess_path).FromASCII(CEF_SUBPROCESS_PATH);

  app = new App();

  if (!CefInitialize(args, settings, app, nullptr)) {
    GST_ERROR_OBJECT (src, "Failed to initialize CEF");
    goto done;
  }

  window_info.SetAsWindowless(0);
  browserClient = new BrowserClient(renderHandler, audioHandler);

  /* We create the browser outside of the lock because it will call the paint
   * callback synchronously */
  GST_INFO_OBJECT (src, "Creating browser with URL %s", src->url);
  browser = CefBrowserHost::CreateBrowserSync(window_info, browserClient.get(), src->url, browserSettings, nullptr, nullptr);

  browser->GetHost()->SetAudioMuted(true);

  GST_OBJECT_LOCK (src);
  src->browser = browser;
  src->cef_work_id = g_timeout_add (5, (GSourceFunc) cef_do_work_func, src);
  src->n_frames = 0;
  GST_OBJECT_UNLOCK (src);

  ret = TRUE;

done:
  return ret;
}

static gboolean
gst_cef_src_stop (GstBaseSrc *base_src)
{
  GstCefSrc *src = GST_CEF_SRC (base_src);

  GST_INFO_OBJECT (src, "Stopping");

  GST_OBJECT_LOCK (src);
  if (src->cef_work_id) {
    g_source_remove (src->cef_work_id);
    src->cef_work_id = 0;
    CefShutdown();
  }
  GST_OBJECT_UNLOCK (src);

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
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N, DEFAULT_FPS_D, NULL);


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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cef_src_finalize (GObject *object)
{
  GstCefSrc *src = GST_CEF_SRC (object);

  g_hash_table_unref (src->audio_buffers);
  src->audio_buffers = NULL;

  g_list_free_full (src->audio_events, (GDestroyNotify) gst_event_unref);
  src->audio_events = NULL;
}

static void
gst_cef_src_init (GstCefSrc * src)
{
  GstBaseSrc *base_src = GST_BASE_SRC (src);

  src->n_frames = 0;
  src->current_buffer = NULL;
  src->audio_buffers = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) gst_buffer_list_unref);
  src->audio_events = NULL;

  gst_base_src_set_format (base_src, GST_FORMAT_TIME);
  gst_base_src_set_live (base_src, TRUE);
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

  gst_element_class_set_static_metadata (gstelement_class,
      "Chromium Embedded Framework source", "Source/Video",
      "Creates a video stream from an embedded Chromium browser", "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_src_template);

  base_src_class->fixate = GST_DEBUG_FUNCPTR(gst_cef_src_fixate);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR(gst_cef_src_set_caps);
  base_src_class->start = GST_DEBUG_FUNCPTR(gst_cef_src_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR(gst_cef_src_stop);
  base_src_class->get_times = GST_DEBUG_FUNCPTR(gst_cef_src_get_times);
  base_src_class->query = GST_DEBUG_FUNCPTR(gst_cef_src_query);

  push_src_class->create = GST_DEBUG_FUNCPTR(gst_cef_src_create);

  GST_DEBUG_CATEGORY_INIT (cef_src_debug, "cefsrc", 0,
      "Chromium Embedded Framework Source");
}
