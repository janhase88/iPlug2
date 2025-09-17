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

## Observability and debug controls

- Structured telemetry is routed through `IGRAPHICS_VK_LOG` (defined in `VulkanLogging.h`). Set `IGRAPHICS_VULKAN_LOG_VERBOSITY` to:
  - `0` to disable emission.
  - `1` (default) for errors only.
  - `2` for verbose per-stage payloads covering swap-chain creation, image acquisition, command recording, and presentation.
- The logging helper publishes newline-delimited JSON objects. Point `VulkanLogging::SetLogSinkForTesting` at a custom sink to persist payloads or surface them in tooling.
- `WinVulkanDeviceCoordinator` encapsulates adapter selection and resource lifetime. Watch for `CreateVulkanDevice`/`EnumeratePhysicalDevices` events when debugging initialization failures.
- `IGraphicsSkia::EnsureSwapchainSurface` caches one Skia surface per swap-chain image. Reuse avoids redundant `SkSurface` allocation during frame playback; look for the `EnsureSwapchainSurface` event when diagnosing cache eviction.
- `BeginFrame`/`EndFrame` reuse a single command pool and primary command buffer. Barriers and submissions are annotated with image indices to make it clear when the frame scheduler resets the pool.

## Limitations and troubleshooting
- Currently only the Windows platform is supported.
- The backend is considered experimental and uses a single in-flight frame guarded by a fence instead of a device-wide stall.
- Ensure that your GPU drivers support Vulkan 1.0 and are up to date.
- To prefer an integrated GPU set the environment variable `IGRAPHICS_VK_GPU=integrated`; by default discrete devices are preferred.
- Swap-chain or presentation failures are often driver related. Verify that no other application holds exclusive fullscreen access.
- Device loss will trigger an internal context rebuild which may momentarily pause rendering.

## Validation playbook (Windows host)

Use the following checklist when validating the Skia–Vulkan path on a Windows workstation after making code changes:

1. **Clean and rebuild** the target project with `IGRAPHICS_SKIA;IGRAPHICS_VULKAN` defined. Confirm that `common-win.props` finds the Vulkan SDK and that no warnings mention missing Skia libraries.
2. **Enable verbose telemetry** for a single run by defining `IGRAPHICS_VULKAN_LOG_VERBOSITY=2` in the project’s preprocessor definitions or by passing `/DIGRAPHICS_VULKAN_LOG_VERBOSITY=2` to MSBuild. This routes JSON logs through the shared `VulkanLogging` sink.
3. **Launch the plugin/standalone** and resize the UI repeatedly. Watch the debugger output (or your custom sink) for `CreateOrResizeVulkanSwapchain` and `DrawResize` events. Verify that swap-chain dimensions track the window size and that no `severity:error` payloads are emitted.
4. **Exercise rendering** by dragging controls, forcing repaints, and toggling animations. Inspect `BeginFrame`/`EndFrame` logs to ensure image indices rotate and that `submissionPending` flips back to `false` after each present.
5. **Trigger device loss recovery** by toggling GPU overclock utilities or by changing the preferred adapter via `IGRAPHICS_VK_GPU`. Confirm that the context recreates cleanly and that the log stream reports `RecreateVulkanContext` without subsequent errors.
6. **Capture artifacts**: if validation fails, save the JSON log output and include it in bug reports—each payload is intentionally structured for diff-friendly diagnostics.

### Troubleshooting

- `VK_ERROR_INITIALIZATION_FAILED`: confirm `VULKAN_SDK` is set and `vkInstance` creation succeeds.
- `VK_ERROR_DEVICE_LOST`: outdated drivers or GPU resets can trigger this; restarting the host process usually recovers.
- Validation layer messages referencing swap-chain re-creation often indicate a mismatch between surface size and the acquired image. Resize the window or call `DrawResize()` explicitly.
