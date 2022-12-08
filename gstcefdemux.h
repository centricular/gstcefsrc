#ifndef __GST_CEF_DEMUX_H__
#define __GST_CEF_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstflowcombiner.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_CEF_DEMUX \
  (gst_cef_demux_get_type())
#define GST_CEF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEF_DEMUX,GstCefDemux))
#define GST_CEF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEF_DEMUX,GstCefDemuxClass))
#define GST_IS_CEF_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEF_DEMUX))
#define GST_IS_CEF_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEF_DEMUX))

typedef struct _GstCefDemux GstCefDemux;
typedef struct _GstCefDemuxClass GstCefDemuxClass;

struct _GstCefDemux {
  GstElement parent;

  gboolean need_stream_start;
  gboolean need_caps;
  gboolean need_segment;
  gboolean need_discont;
  GstPad *vsrcpad;
  GstPad *asrcpad;
  GList *cef_audio_stream_start_events;
  GstEvent *vcaps_event;
  GstFlowCombiner *flow_combiner;
  GstClockTime last_audio_time;
  GstAudioInfo audio_info;
};

struct _GstCefDemuxClass {
  GstElementClass parent_class;
};

GType gst_cef_demux_get_type (void);

G_END_DECLS

#endif /* __GST_CEF_DEMUX_H__ */
