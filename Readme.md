# Simple OpenXR C Example

This application is neither complex nor optimized for performance.
It is intended to showcase OpenXR and how it is to be used for a simple VR application.

It is split into two parts. `main.c` contains almost only the interaction with OpenXR.
The OpenGL rendering is contained as much as possible in `glimpl.c` so it does not clutter the main application.

Unless the OpenXR runtime is installed in the file system, the `XR_RUNTIME_JSON` variable has to be set for the loader to know where to look for the runtime and how the runtime is named

    XR_RUNTIME_JSON=~/monado/build/openxr_monado-dev.json

then, you should be ready to run `./openxr-example`.

If you want to use API layers that are not installed in the default path, set the variable `XR_API_LAYER_PATH`

    XR_API_LAYER_PATH=/path/to/api_layers/

This will enable to loader to find api layers at this path and enumerate them with `xrEnumerateApiLayerProperties()`

API Layers can be enabled either with code or the loader can be told to enable an API layer with `XR_ENABLE_API_LAYERS`

    XR_ENABLE_API_LAYERS=XR_APILAYER_LUNARG_core_validation
