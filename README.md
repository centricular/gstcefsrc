# gstcefsrc

## Build

```
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
make
```

### Optional CMake variables

- `CEF_VERSION` allows to override the default CEF version
- `CEF_BUILDS_HOMEPAGE_URL` allows to configure the URL of the website hosting
  CEF binaries. Default is https://cef-builds.spotifycdn.com

### Third-party CEF builds

The official CEF builds hosted by Spotify do not support patent-encumbered
codecs such as H.264. If that is a feature you need in your project or for some
reason you need to customize the Chromium build flags, you need to rebuild CEF
yourself. Thankfully at least two projects can help you with that daunting task:

- https://github.com/reinismu/cefbuild : Docker to build cef with all codecs
- https://github.com/philn/cef-build-box : Semi-automatic and reproducible CEF build

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
GST_PLUGIN_PATH=Release:$GST_PLUGIN_PATH gst-launch-1.0 -e \
    cefsrc url="https://soundcloud.com/platform/sama" ! \
    video/x-raw, width=1920, height=1080, framerate=60/1 ! \
    cefdemux name=demux ! queue ! videoconvert ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! x264enc ! queue ! \
    mp4mux name=muxer ! filesink location='test.mp4' \
    audiotestsrc do-timestamp=true is-live=true  volume=0.0 ! audiomixer name=mix ! \
    queue max-size-bytes=0 max-size-buffers=0 max-size-time=3000000000 ! audioconvert ! \
    audiorate ! audioresample ! faac bitrate=128000 ! queue ! muxer. \
    demux. ! queue ! mix.
```

This will work with sites with no audio as well

`cefsrc` requires an X server environment on Linux, if none is available you can
run the previous commands with `xvfb-run`:

`xvfb-run --server-args="-screen 0 1920x1080x60" gst-launch-1.0 ...`

In addition, a wrapper bin is exposed, wrapping cefsrc and cefdemux, and
handling `web+http`, `web+https` and `web+file` protocols:

``` shell
GST_PLUGIN_PATH=Release:$GST_PLUGIN_PATH gst-launch-1.0 \
    cefbin name=cef cefsrc::url="https://soundcloud.com/platform/sama" \
    cef.video ! video/x-raw, width=1920, height=1080, framerate=60/1 ! videoconvert ! xvimagesink \
    cef.audio ! audioconvert ! audiomixer ! autoaudiosink
```

``` shell
gst-launch-1.0 playbin uri=web+https://www.soundcloud.com/platform/sama
```

## Docker GPU Acceleration

This is simply a hint/note for those who want to use this plugin in a docker container with GPU acceleration. Your particular setup may vary. The following was tested on Ubuntu 22.04 with a Nvidia GPU. This assumes you have installed the Nvidia drivers, docker, and the Nvidia Container Toolkit. You may also need to configure your xorg.conf within the container to use the Nvidia GPU. 

- xserver-xorg-video-dummy is required in the container

Running docker with the following flags will allow the container to access the host's GPU:

``` shell
docker run --gpus all -v /usr/local/cuda:/usr/local/cuda --device=/dev/dri/card0 --rm \
-e DISPLAY=:1 -v /tmp/X11-unix:/tmp/.X11 -it <image> /bin/bash
```

Inside the container run:
    
``` shell
Xorg -noreset +extension GLX +extension RANDR \+extension RENDER -logfile ./xserver.log vt1 :1 &
```

Test that the GPU is accessible by running:

``` shell
gst-launch-1.0 -e cefsrc url="chrome://gpu" gpu=true \
chrome-extra-flags="use-gl=egl, enable-gpu-rasterization,ignore-gpu-blocklist" \
! video/x-raw, width=1920, height=8080, framerate=1/1 \
! cefdemux name=demux ! queue ! videoconvert \
! pngenc ! multifilesink location="frame%d.png"
```

It is also helpful to run nvidia-smi or nvtop to verify the GPU is being used. Note it is possible to use the GPU within a kube pod as well. This has been tested and runs in a production environment on GKE using Container Optimized OS.