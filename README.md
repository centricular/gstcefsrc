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
GST_PLUGIN_PATH=Release:$GST_PLUGIN_PATH Release/gst-launch-1.0 \
    cefsrc url="https://soundcloud.com/platform/sama" ! \
    video/x-raw, width=1920, height=1080, framerate=60/1 ! cefdemux name=d d.video ! queue ! videoconvert ! \
    xvimagesink d.audio ! queue ! audioconvert ! pulsesink
```
