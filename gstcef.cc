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

#include <stdio.h>
#include <iostream>
#include <sstream>

#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/cef_render_handler.h>
#include <include/cef_life_span_handler.h>
#include <include/cef_load_handler.h>
#include <include/wrapper/cef_helpers.h>

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>

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

GST_DEBUG_CATEGORY_STATIC (cef_src_debug);
#define GST_CAT_DEFAULT cef_src_debug

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1
#define DEFAULT_URL "https://www.google.com"

typedef struct _GstCefSrc GstCefSrc;
typedef struct _GstCefSrcClass GstCefSrcClass;

enum
{
  PROP_0,
  PROP_URL,
};

struct _GstCefSrc {
  GstPushSrc parent;
  GstBuffer *current_buffer;
  GstVideoInfo info;
  guint64 n_frames;
  gulong cef_work_id;
  gchar *url;
  CefRefPtr<CefBrowser> browser;
};

struct _GstCefSrcClass {
  GstPushSrcClass parent_class;
};

GType gst_cef_src_get_type (void);

#define gst_cef_src_parent_class parent_class
G_DEFINE_TYPE (GstCefSrc, gst_cef_src, GST_TYPE_PUSH_SRC);

#define CEF_CAPS "video/x-raw, format=BGRA, framerate=[1/1, 60/1]"

static GstStaticPadTemplate gst_cef_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_CAPS)
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

    bool GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect)
    {
      GST_OBJECT_LOCK (element);
      rect = CefRect(0, 0, element->info.width, element->info.height);
      GST_OBJECT_UNLOCK (element);
      return true;
    }

    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void * buffer, int w, int h)
    {
      GstBuffer *new_buffer;

      GST_DEBUG_OBJECT (element, "Painting, buffer address: %p, width / height: %d  / %d ", buffer, w,  h);

      GST_OBJECT_LOCK (element);
      new_buffer = gst_buffer_new_allocate (NULL, element->info.width * element->info.height * 4, NULL);
      gst_buffer_fill (new_buffer, 0, buffer, w * h * 4);
      gst_buffer_replace (&(element->current_buffer), new_buffer);
      gst_buffer_unref (new_buffer);
      GST_OBJECT_UNLOCK (element);
    }

  private:

    GstCefSrc *element;

    IMPLEMENT_REFCOUNTING(RenderHandler);
};

class BrowserClient : public CefClient
{
  public:

    BrowserClient(CefRefPtr<CefRenderHandler> ptr) :
        handler(ptr)
    {
    }

    virtual CefRefPtr<CefRenderHandler> GetRenderHandler()
    {
      return handler;
    }

  private:

    CefRefPtr<CefRenderHandler> handler;

    IMPLEMENT_REFCOUNTING(BrowserClient);
};

static gboolean
cef_do_work_func(GstCefSrc *src)
{
  GST_LOG_OBJECT (src, "Making CEF work");
  CefDoMessageLoopWork();
  GST_LOG_OBJECT (src, "Made CEF work");

  return G_SOURCE_CONTINUE;
}

static GstFlowReturn gst_cef_src_create(GstPushSrc *push_src, GstBuffer **buf)
{
  GstCefSrc *src = GST_CEF_SRC (push_src);

  GST_OBJECT_LOCK (src);
  g_assert (src->current_buffer);
  *buf = gst_buffer_copy (src->current_buffer);
  GST_BUFFER_PTS (*buf) = gst_util_uint64_scale (src->n_frames, src->info.fps_d * GST_SECOND, src->info.fps_n);
  GST_BUFFER_DURATION (*buf) = gst_util_uint64_scale (GST_SECOND, src->info.fps_d, src->info.fps_n);
  GST_DEBUG_OBJECT (src, "Created buffer %" GST_PTR_FORMAT, *buf);
  src->n_frames++;
  GST_OBJECT_UNLOCK (src);

  return  GST_FLOW_OK;
}

static gboolean
gst_cef_src_start(GstBaseSrc *base_src)
{
  gboolean ret = FALSE;
  GstCefSrc *src = GST_CEF_SRC (base_src);
  CefMainArgs args(0, NULL);
  CefSettings settings;
  CefRefPtr<RenderHandler> renderHandler = new RenderHandler(src);
  CefRefPtr<BrowserClient> browserClient;
  CefRefPtr<CefBrowser> browser;
  CefWindowInfo window_info;
  CefBrowserSettings browserSettings;

  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;

  GST_INFO_OBJECT  (src, "Starting up");

  /* FIXME: won't work installed */
  CefString(&settings.browser_subprocess_path).FromASCII(CEF_SUBPROCESS_PATH);

  if (!CefInitialize(args, settings, nullptr, nullptr)) {
    GST_ERROR_OBJECT (src, "Failed to initialize CEF");
    goto done;
  }

  window_info.SetAsWindowless(0);
  browserClient = new BrowserClient(renderHandler);

  /* We create the browser outside of the lock because it will call the paint
   * callback synchronously */
  browser = CefBrowserHost::CreateBrowserSync(window_info, browserClient.get(), src->url, browserSettings, nullptr);

  GST_OBJECT_LOCK (src);
  src->browser = browser;
  src->cef_work_id = g_timeout_add (10, (GSourceFunc) cef_do_work_func, src);
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

      latency = gst_util_uint64_scale (GST_SECOND, src->info.fps_d, src->info.fps_n);
      GST_DEBUG_OBJECT (src, "Reporting latency: %" GST_TIME_FORMAT, GST_TIME_ARGS (latency));
      gst_query_set_latency (query, TRUE, latency, GST_CLOCK_TIME_NONE);
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
  gst_video_info_from_caps (&src->info, caps);
  new_buffer = gst_buffer_new_allocate (NULL, src->info.width * src->info.height * 4, NULL);
  gst_buffer_replace (&(src->current_buffer), new_buffer);
  gst_buffer_unref (new_buffer);
  src->browser->GetHost()->SetWindowlessFrameRate(gst_util_uint64_scale (1, src->info.fps_n, src->info.fps_d));
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
gst_cef_src_init (GstCefSrc * src)
{
  GstBaseSrc *base_src = GST_BASE_SRC (src);

  src->n_frames = 0;
  src->current_buffer = NULL;

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
}

static gboolean
plugin_init(GstPlugin *plugin)
{
  GST_DEBUG_CATEGORY_INIT (cef_src_debug, "cefsrc", 0,
      "Chromium Embedded Framework Source");

  return gst_element_register (plugin, "cefsrc", GST_RANK_NONE,
      GST_TYPE_CEF_SRC);
}

#define PACKAGE "gstcef"

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
                  GST_VERSION_MINOR,
                  cef,
                  "Chromium Embedded src plugin",
                  plugin_init, "1.0", "LGPL", PACKAGE, "centricular.com")
