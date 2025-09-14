# Skia Vulkan on Windows

This guide explains how to enable the Skia renderer using Vulkan on Windows.

## Prerequisites
- Install the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
- Set the `VULKAN_SDK` environment variable to the SDK's root directory.
- Build Skia with Vulkan support.

## Enabling the backend
- Define `IGRAPHICS_SKIA;IGRAPHICS_VULKAN` in your project settings or `.props` file.
- When `VULKAN_SDK` is set, the shared property sheet `common-win.props` automatically adds the required include and library paths.

## Notes
- The Vulkan backend is experimental and may have limitations.
- If swap-chain creation fails, verify that your GPU drivers support Vulkan and that the SDK is correctly installed.
