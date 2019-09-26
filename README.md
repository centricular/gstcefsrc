# gstcefsrc

## Build

```
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
make
```

## Run

The element can then be tested with:

```
GST_PLUGIN_PATH=$PWD/Release:$GST_PLUGIN_PATH gst-launch-1.0 cefsrc url="https://soundcloud.com/platform/sama" ! queue ! cefdemux name=d \
  d.video ! video/x-raw ! queue ! videoconvert ! autovideosink \
  d. ! audio/x-raw ! queue ! audioconvert ! autoaudiosink async-handling=true
```
