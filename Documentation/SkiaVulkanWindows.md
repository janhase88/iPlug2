# Skia Vulkan on Windows

This guide explains how to enable the Skia renderer using Vulkan on Windows.

## Prerequisites
- Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
- Set the `VULKAN_SDK` environment variable to the SDK's root directory.
- Build Skia with Vulkan support.

## Enabling the backend
- Define `IGRAPHICS_SKIA;IGRAPHICS_VULKAN` in your project settings or `.props` file.
- When `VULKAN_SDK` is set, the shared property sheet `common-win.props` automatically adds the required include and library paths.

## Limitations and troubleshooting
- Ensure that your GPU drivers support Vulkan 1.0 and are up to date.
- To prefer an integrated GPU set the environment variable `IGRAPHICS_VK_GPU=integrated`; by default discrete devices are preferred.
- Swap-chain or presentation failures are often driver related. Verify that no other application holds exclusive fullscreen access.
- Device loss will trigger an internal context rebuild which may momentarily pause rendering.
