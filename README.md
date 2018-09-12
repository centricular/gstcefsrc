# gstcefsrc

## Build

```
mkdir build && cd build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ..
make
```

## Run

Because of [this issue], the main executable must be located in the
same directory as the resources in the build folder. This makes things
pretty awkward with gst-launch, but can be worked around as follows:

```
cp `which gst-launch-1.0` Release/
```

The element can then be tested with:

```
GST_PLUGIN_PATH=$PWD/Release:$GST_PLUGIN_PATH Release/gst-launch-1.0 -v cefsrc url="https://webglsamples.org/aquarium/aquarium.html" ! queue ! videoconvert ! xvimagesink
```

[this issue]: https://bitbucket.org/chromiumembedded/cef/issues/1936/override-paths-dir_exe-dir_module-on-linux
