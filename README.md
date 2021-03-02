# gstcefsrc

## Build

```
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
make
```

## Run

The element can then be tested with:

``` shell
GST_PLUGIN_PATH=Release:$GST_PLUGIN_PATH gst-launch-1.0 \
    cefsrc url="https://soundcloud.com/platform/sama" ! \
    video/x-raw, width=1920, height=1080, framerate=60/1 ! cefdemux name=d d.video ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! videoconvert ! \
    xvimagesink audiotestsrc do-timestamp=true is-live=true  volume=0.00 ! audiomixer name=mix ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! audioconvert ! pulsesink \
    d.audio ! mix.
```

Record website video + audio (with audiomixer)

``` shell
GST_PLUGIN_PATH=Release:$GST_PLUGIN_PATH gst-launch-1.0 \
    cefsrc url="https://soundcloud.com/platform/sama" ! \
    video/x-raw, width=1920, height=1080, framerate=60/1 ! \
    cefdemux name=demux ! videoconvert ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! x264enc ! \
    mp4mux name=muxer fragment-duration=1000 ! filesink location='test.mp4' \
    audiotestsrc do-timestamp=true is-live=true  volume=0.00 ! audiomixer name=mix ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! audioconvert ! \
    audiorate ! audioresample ! faac bitrate=128000 ! muxer. \
    demux. ! mix.
```

This will work with sites with no audio as well