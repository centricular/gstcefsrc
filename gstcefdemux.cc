#include <gst/audio/audio.h>

#include "gstcefdemux.h"
#include "gstcefaudiometa.h"

#define CEF_VIDEO_CAPS "video/x-raw, format=BGRA, width=[1, 2147483647], height=[1, 2147483647], framerate=[1/1, 60/1], pixel-aspect-ratio=1/1"
#define CEF_AUDIO_CAPS "audio/x-raw, format=F32LE, rate=[1, 2147483647], channels=[1, 2147483647], layout=interleaved"

#define GST_CAT_DEFAULT gst_cef_demux_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define gst_cef_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCefDemux, gst_cef_demux, GST_TYPE_ELEMENT,
                         GST_DEBUG_CATEGORY_INIT (gst_cef_demux_debug, "cefdemux", 0,
                                                  "cefdemux element"););

static GstStaticPadTemplate gst_cef_demux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_VIDEO_CAPS)
    );

static GstStaticPadTemplate gst_cef_demux_video_src_template =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_VIDEO_CAPS)
);

static GstStaticPadTemplate gst_cef_demux_audio_src_template =
GST_STATIC_PAD_TEMPLATE ("audio",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CEF_AUDIO_CAPS)
);

static gboolean
gst_cef_demux_push_events (GstCefDemux *demux)
{
  GstSegment segment;
  GstEvent *event;

  if (demux->need_stream_start) {
    event = gst_event_new_stream_start ("cefvideo");
    gst_pad_push_event (demux->vsrcpad, event);
    event = gst_event_new_stream_start ("cefaudio");
    gst_pad_push_event (demux->asrcpad, event);
    demux->need_stream_start = FALSE;
  }

  if (demux->need_caps) {
    GstCaps *audio_caps;

    g_assert (demux->vcaps_event);
    gst_pad_push_event (demux->vsrcpad, demux->vcaps_event);
    demux->vcaps_event = NULL;

    /* Push some dummy caps so that our initial gap events don't
     * get refused */
    audio_caps = gst_caps_new_simple ("audio/x-raw",
        "format", G_TYPE_STRING, "F32LE",
        "rate", G_TYPE_INT, 44100,
        "channels", G_TYPE_INT, 2,
        "layout", G_TYPE_STRING, "interleaved",
        NULL);
    gst_pad_push_event (demux->asrcpad, gst_event_new_caps (audio_caps));
    gst_caps_unref (audio_caps);

    demux->need_caps = FALSE;
  }

  if (demux->need_segment) {
    gst_segment_init (&segment, GST_FORMAT_TIME);
    event = gst_event_new_segment (&segment);
    gst_pad_push_event (demux->vsrcpad, event);
    event = gst_event_new_segment (&segment);
    gst_pad_push_event (demux->asrcpad, event);
    demux->need_segment = FALSE;
  }

  return TRUE;
}

typedef struct
{
  GstCefDemux *demux;
  GstFlowCombiner *flow_combiner;
  GstFlowReturn combined;
} AudioPushData;

#if !GST_CHECK_VERSION(1, 18, 0)
static GstClockTime
gst_element_get_current_clock_time (GstElement * element)
{
  GstClock *clock = NULL;
  GstClockTime ret;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_CLOCK_TIME_NONE);

  clock = gst_element_get_clock (element);

  if (!clock) {
    GST_DEBUG_OBJECT (element, "Element has no clock");
    return GST_CLOCK_TIME_NONE;
  }

  ret = gst_clock_get_time (clock);
  gst_object_unref (clock);

  return ret;
}

static GstClockTime
gst_element_get_current_running_time (GstElement * element)
{
  GstClockTime base_time, clock_time;

  g_return_val_if_fail (GST_IS_ELEMENT (element), GST_CLOCK_TIME_NONE);

  base_time = gst_element_get_base_time (element);

  if (!GST_CLOCK_TIME_IS_VALID (base_time)) {
    GST_DEBUG_OBJECT (element, "Could not determine base time");
    return GST_CLOCK_TIME_NONE;
  }

  clock_time = gst_element_get_current_clock_time (element);

  if (!GST_CLOCK_TIME_IS_VALID (clock_time)) {
    return GST_CLOCK_TIME_NONE;
  }

  if (clock_time < base_time) {
    GST_DEBUG_OBJECT (element, "Got negative current running time");
    return GST_CLOCK_TIME_NONE;
  }

  return clock_time - base_time;
}
#endif


static gboolean
gst_cef_demux_push_audio_buffer (GstBuffer **buffer, guint idx, AudioPushData *push_data)
{
  GST_BUFFER_PTS (*buffer) += push_data->demux->ts_offset;
  push_data->combined = gst_flow_combiner_update_pad_flow (push_data->flow_combiner, push_data->demux->asrcpad,
      gst_pad_push (push_data->demux->asrcpad, *buffer));
  push_data->demux->last_audio_time = GST_BUFFER_PTS (*buffer) + GST_BUFFER_DURATION (*buffer);
  *buffer = NULL;
  return TRUE;
}

static void
gst_cef_demux_update_audio_caps (GstCefDemux *demux, const GstStructure *s)
{
  GstCaps *caps;
  GstEvent *event;
  gint channels, rate;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);

  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, "F32LE",
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels,
      "channel-mask", GST_TYPE_BITMASK, gst_audio_channel_get_fallback_mask (channels),
      "layout", G_TYPE_STRING, "interleaved",
      NULL);
  event = gst_event_new_caps (caps);
  gst_pad_push_event (demux->asrcpad, event);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_cef_demux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstCefDemux *demux = (GstCefDemux *) parent;
  GstMeta *meta;
  gpointer state = NULL;
  GList *tmp;
  GstFlowReturn ret = GST_FLOW_OK;

  gst_cef_demux_push_events (demux);


  if (!GST_CLOCK_TIME_IS_VALID (demux->ts_offset)) {
    demux->ts_offset = GST_BUFFER_PTS (buffer);
  }

  for (tmp = demux->cef_audio_stream_start_events; tmp; tmp = tmp->next) {
    const GstStructure *s = gst_event_get_structure ((GstEvent *) tmp->data);

    gst_cef_demux_update_audio_caps (demux, s);
  }

  g_list_free_full (demux->cef_audio_stream_start_events, (GDestroyNotify) gst_event_unref);
  demux->cef_audio_stream_start_events = NULL;

  while ((meta = gst_buffer_iterate_meta_filtered (buffer, &state, GST_CEF_AUDIO_META_API_TYPE)) != NULL) {
    AudioPushData push_data;
    GstCefAudioMeta *ameta = (GstCefAudioMeta *) meta;

    push_data.demux = demux;
    push_data.flow_combiner = demux->flow_combiner;
    gst_buffer_list_foreach (ameta->buffers, (GstBufferListFunc) gst_cef_demux_push_audio_buffer, &push_data);
    if (push_data.combined != GST_FLOW_OK) {
      ret = push_data.combined;
      goto done;
    }
  }

  ret = gst_flow_combiner_update_pad_flow (demux->flow_combiner, demux->vsrcpad,
      gst_pad_push (demux->vsrcpad, buffer));

  if (demux->last_audio_time < GST_BUFFER_PTS (buffer)) {
    GstEvent *gap;

    gap = gst_event_new_gap (demux->last_audio_time, GST_BUFFER_PTS (buffer) - demux->last_audio_time);

    gst_pad_push_event (demux->asrcpad, gap);

    demux->last_audio_time = GST_BUFFER_PTS (buffer);
  }

  if (ret != GST_FLOW_OK)
    goto done;

done:
  return ret;
}

static gboolean
gst_cef_demux_sink_event (GstPad *pad, GstObject *parent, GstEvent *event)
{
  GstCefDemux *demux = (GstCefDemux *) parent;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *s = gst_event_get_structure (event);

      if (gst_structure_has_name (s, "cef-audio-stream-start")) {
        demux->cef_audio_stream_start_events = g_list_append (demux->cef_audio_stream_start_events, event);
        event = NULL;
      }
      break;
    }
    case GST_EVENT_CAPS:
      demux->vcaps_event = event;
      demux->need_caps = TRUE;
      event = NULL;
    /* We send our own */
    case GST_EVENT_SEGMENT:
    case GST_EVENT_STREAM_START:
      gst_event_replace (&event, NULL);
      break;
    default:
      break;
  }

  if (event) {
    gst_pad_event_default(pad, parent, event);
  }

  return TRUE;
}

static gboolean
gst_cef_demux_sink_query (GstPad *pad, GstObject *parent, GstQuery *query)
{
  GstCefDemux *demux = (GstCefDemux *) parent;
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      ret = gst_pad_peer_query(demux->vsrcpad, query);
      break;
    default:
      ret = gst_pad_query_default(pad, parent, query);
      break;
  }

  return ret;
}

static void
gst_cef_demux_init (GstCefDemux * demux)
{
  GstPad *sinkpad = gst_pad_new_from_static_template (&gst_cef_demux_sink_template, "sink");;

  demux->vsrcpad = gst_pad_new_from_static_template (&gst_cef_demux_video_src_template, "video");
  demux->asrcpad = gst_pad_new_from_static_template (&gst_cef_demux_audio_src_template, "audio");

  gst_pad_set_chain_function (sinkpad, gst_cef_demux_chain);
  gst_pad_set_event_function (sinkpad, gst_cef_demux_sink_event);
  gst_pad_set_query_function (sinkpad, gst_cef_demux_sink_query);
  gst_element_add_pad (GST_ELEMENT (demux), sinkpad);

  demux->flow_combiner = gst_flow_combiner_new ();

  gst_element_add_pad (GST_ELEMENT (demux), demux->vsrcpad);
  gst_flow_combiner_add_pad (demux->flow_combiner, demux->vsrcpad);
  gst_element_add_pad (GST_ELEMENT (demux), demux->asrcpad);
  gst_flow_combiner_add_pad (demux->flow_combiner, demux->asrcpad);

  demux->need_stream_start = TRUE;
  demux->need_caps = TRUE;
  demux->need_segment = TRUE;
  demux->last_audio_time = 0;
  demux->ts_offset = GST_CLOCK_TIME_NONE;
}

static void
gst_cef_demux_finalize (GObject *object)
{
  GstCefDemux *demux = GST_CEF_DEMUX (object);

  g_list_free_full (demux->cef_audio_stream_start_events, (GDestroyNotify) gst_event_unref);
  demux->cef_audio_stream_start_events = NULL;
  gst_flow_combiner_free (demux->flow_combiner);
}

static void
gst_cef_demux_class_init (GstCefDemuxClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);

  gobject_class->finalize = gst_cef_demux_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "Chromium Embedded Framework demuxer", "Demuxer/Audio/Video",
      "Demuxes audio and video from cefsrc", "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_demux_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_demux_video_src_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_cef_demux_audio_src_template);
}
