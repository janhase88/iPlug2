# Skia Vulkan on Windows

This guide explains how to enable the Skia renderer using Vulkan on Windows.

## Prerequisites
- Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
- Set the `VULKAN_SDK` environment variable to the SDK's root directory.
- Build Skia with Vulkan support.

### Building Skia with Vulkan

Skia uses the [GN](https://gn.googlesource.com/gn/) and `ninja` build tools. After cloning the Skia source tree run:

```
python tools/git-sync-deps
gn gen out/Release --args="is_official_build=true is_component_build=false skia_use_vulkan=true"
ninja -C out/Release skia modules
```

The resulting `out/Release` directory will contain the libraries needed by iPlug2. Point `SKIA_PATH` in your project settings to this folder.

## Enabling the backend
- Define `IGRAPHICS_SKIA;IGRAPHICS_VULKAN` in your project settings or `.props` file.
- When `VULKAN_SDK` is set, the shared property sheet `common-win.props` automatically adds the required include and library paths.

## Limitations and troubleshooting
- Currently only the Windows platform is supported.
- The backend is considered experimental and uses a single in-flight frame guarded by a fence instead of a device-wide stall.
- Ensure that your GPU drivers support Vulkan 1.0 and are up to date.
- To prefer an integrated GPU set the environment variable `IGRAPHICS_VK_GPU=integrated`; by default discrete devices are preferred.
- Swap-chain or presentation failures are often driver related. Verify that no other application holds exclusive fullscreen access.
- Device loss will trigger an internal context rebuild which may momentarily pause rendering.

### Troubleshooting

- `VK_ERROR_INITIALIZATION_FAILED`: confirm `VULKAN_SDK` is set and `vkInstance` creation succeeds.
- `VK_ERROR_DEVICE_LOST`: outdated drivers or GPU resets can trigger this; restarting the host process usually recovers.
- Validation layer messages referencing swap-chain re-creation often indicate a mismatch between surface size and the acquired image. Resize the window or call `DrawResize()` explicitly.
