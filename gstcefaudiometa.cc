#include "gstcefaudiometa.h"

static gboolean
gst_cef_audio_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstCefAudioMeta *ameta = (GstCefAudioMeta *) meta;

  ameta->buffers = NULL;

  return TRUE;
}

static void
gst_cef_audio_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstCefAudioMeta *ameta = (GstCefAudioMeta *) meta;

  if (ameta->buffers) {
    gst_buffer_list_unref (ameta->buffers);
    ameta->buffers = NULL;
  }
}

GstCefAudioMeta *
gst_buffer_add_cef_audio_meta (GstBuffer * buffer, GstBufferList *buffers, gint stream_id)
{
  GstCefAudioMeta *ameta;

  ameta =
      (GstCefAudioMeta *) gst_buffer_add_meta (buffer, GST_CEF_AUDIO_META_INFO, NULL);

  ameta->buffers = buffers;
  ameta->stream_id = stream_id;

  return ameta;
}

GType
gst_cef_audio_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstCefAudioMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_cef_audio_meta_get_info (void)
{
  static const GstMetaInfo *gst_cef_audio_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) &gst_cef_audio_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_CEF_AUDIO_META_API_TYPE,
        "GstCefAudioMeta", sizeof (GstCefAudioMeta),
        gst_cef_audio_meta_init, gst_cef_audio_meta_free,
        NULL);
    g_once_init_leave ((GstMetaInfo **) &gst_cef_audio_meta_info,
        (GstMetaInfo *) meta);
  }
  return gst_cef_audio_meta_info;
}

