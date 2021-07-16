#ifndef __GST_CEF_BIN_H__
#define __GST_CEF_BIN_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_CEF_BIN \
  (gst_cef_bin_get_type())
#define GST_CEF_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CEF_BIN,GstCefBin))
#define GST_CEF_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CEF_BIN,GstCefBinClass))
#define GST_IS_CEF_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CEF_BIN))
#define GST_IS_CEF_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CEF_BIN))

typedef struct _GstCefBin GstCefBin;
typedef struct _GstCefBinClass GstCefBinClass;

struct _GstCefBin {
  GstBin parent;
  GstElement *cefsrc;
  GstPad *asrcpad;
  GstPad *vsrcpad;
};

struct _GstCefBinClass {
  GstBinClass parent_class;
};

GType gst_cef_bin_get_type (void);

G_END_DECLS

#endif /* __GST_CEF_BIN_H__ */
