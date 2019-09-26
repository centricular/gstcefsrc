#ifndef __GST_CEF_AUDIO_META_H__
#define __GST_CEF_AUDIO_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_CEF_AUDIO_META_API_TYPE (gst_cef_audio_meta_api_get_type())
#define GST_CEF_AUDIO_META_INFO  (gst_cef_audio_meta_get_info())

#define GST_CEF_AUDIO_META_RTP_EXTENSION_ID 1

typedef struct _GstCefAudioMeta GstCefAudioMeta;

struct _GstCefAudioMeta {
  GstMeta      meta;
  GstBufferList *buffers;
  gint stream_id;
};

GST_EXPORT
GType gst_cef_audio_meta_api_get_type (void);

GST_EXPORT
const GstMetaInfo * gst_cef_audio_meta_get_info (void);

#define gst_buffer_get_cef_audio_meta(b) ((GstCefAudioMeta*)gst_buffer_get_meta((b), GST_CEF_AUDIO_META_API_TYPE))

GST_EXPORT
GstCefAudioMeta * gst_buffer_add_cef_audio_meta (GstBuffer *buffer, GstBufferList *buffers, gint stream_id);

G_END_DECLS

#endif /* __GST_CEF_AUDIO_META_H__ */
