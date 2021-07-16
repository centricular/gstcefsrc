#include "gstcefbin.h"
#include "gstcefaudiometa.h"

#define CEF_VIDEO_CAPS "video/x-raw, format=BGRA, width=[1, 2147483647], height=[1, 2147483647], framerate=[1/1, 60/1], pixel-aspect-ratio=1/1"
#define CEF_AUDIO_CAPS "audio/x-raw, format=F32LE, rate=[1, 2147483647], channels=[1, 2147483647], layout=interleaved"

static GstURIType
gst_cef_bin_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_cef_bin_get_protocols (GType type)
{
  static const gchar *protocols[] = { "web", "cef", NULL };

  return protocols;
}

static gchar *
gst_cef_bin_get_uri (GstURIHandler * handler)
{
  GstCefBin *self = GST_CEF_BIN (handler);
  gchar *ret = NULL;

  g_object_get (self->cefsrc, "url", &ret, NULL);

  return ret;
}

static gboolean
gst_cef_bin_set_uri (GstURIHandler * handler, const gchar * uristr,
    GError ** error)
{
  gboolean res = TRUE;
  gchar *location;
  GstCefBin *self = GST_CEF_BIN (handler);

  location = gst_uri_get_location (uristr);

  g_object_set (self->cefsrc, "url", location, NULL);

  g_free (location);

  return res;
}

static void
gst_cef_bin_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_cef_bin_uri_get_type;
  iface->get_protocols = gst_cef_bin_get_protocols;
  iface->get_uri = gst_cef_bin_get_uri;
  iface->set_uri = gst_cef_bin_set_uri;
}

G_DEFINE_TYPE_WITH_CODE (GstCefBin, gst_cef_bin, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER, gst_cef_bin_uri_handler_init));

static GstStaticPadTemplate gst_cef_bin_video_src_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_VIDEO_CAPS)
);

static GstStaticPadTemplate gst_cef_bin_audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_AUDIO_CAPS)
);

static void
gst_cef_bin_init (GstCefBin * self)
{
  GstPadTemplate *padtemplate;

  padtemplate = gst_static_pad_template_get(&gst_cef_bin_video_src_template);
  self->vsrcpad = gst_ghost_pad_new_no_target_from_template("video", padtemplate);
  gst_object_unref (padtemplate);

  padtemplate = gst_static_pad_template_get(&gst_cef_bin_audio_src_template);
  self->asrcpad = gst_ghost_pad_new_no_target_from_template("audio", padtemplate);
  gst_object_unref (padtemplate);

  gst_element_add_pad (GST_ELEMENT (self), self->vsrcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->asrcpad);
}

static void
gst_cef_bin_finalize (GObject *object)
{
  //GstCefBin *self = GST_CEF_BIN (object);
}

static void
gst_cef_bin_constructed (GObject *object)
{
  GstCefBin *self = GST_CEF_BIN (object);
  GstElement *cefsrc, *cefdemux, *vqueue, *aqueue;
  GstPad *srcpad;

  cefsrc = gst_element_factory_make("cefsrc", "cefsrc");
  cefdemux = gst_element_factory_make("cefdemux", "cefdemux");
  vqueue = gst_element_factory_make("queue", "video-queue");
  aqueue = gst_element_factory_make("queue", "audio-queue");

  g_assert (cefsrc && cefdemux && vqueue && aqueue);

  gst_bin_add_many(GST_BIN (self), cefsrc, cefdemux, aqueue, vqueue, NULL);

  gst_element_link (cefsrc, cefdemux);

  gst_element_link_pads(cefdemux, "video", vqueue, "sink");
  gst_element_link_pads(cefdemux, "audio", aqueue, "sink");

  srcpad = gst_element_get_static_pad(vqueue, "src");
  gst_ghost_pad_set_target(GST_GHOST_PAD (self->vsrcpad), srcpad);
  gst_object_unref (srcpad);

  srcpad = gst_element_get_static_pad(aqueue, "src");
  gst_ghost_pad_set_target(GST_GHOST_PAD (self->asrcpad), srcpad);
  gst_object_unref (srcpad);

  g_object_set(vqueue,
      "max-size-buffers", 0,
      "max-size-bytes", 0,
      "max-size-time", GST_SECOND,
      NULL);

  g_object_set(aqueue,
      "max-size-buffers", 0,
      "max-size-bytes", 0,
      "max-size-time", GST_SECOND,
      NULL);

  self->cefsrc = cefsrc;
}

static void
gst_cef_bin_class_init (GstCefBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

  gobject_class->constructed = gst_cef_bin_constructed;
  gobject_class->finalize = gst_cef_bin_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Chromium Embedded Framework source bin", "Source/Audio/Video",
      "Wraps cefsrc and cefdemux", "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_bin_video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_bin_audio_src_template);
}
