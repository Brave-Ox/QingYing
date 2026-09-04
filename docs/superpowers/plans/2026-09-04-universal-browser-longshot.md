# Chromium Browser LongShot Implementation Plan

**Goal:** Provide native long screenshots for Chrome, Edge, Brave, and other
Chromium browsers without installing an extension or communicating with a web
page.

## Current Scope

```text
Chrome / Edge / Brave / Chromium browser
                ↓
Chrome_WidgetWin_1 root window
                ↓
Chrome_RenderWidgetHostHWND
or Intermediate D3D Window viewport
                ↓
Existing LongShotEngine
(capture → wheel → overlap stitch → safe stop)
```

- One `builtin.browser` DLL handles the Chromium family.
- The adapter finds the largest visible webpage renderer child and rejects a
  selection outside that viewport, so tabs and the address bar are excluded.
- The existing long-shot engine owns capture, wheel dispatch, frame overlap,
  maximum-frame/output limits, cancellation, and result stitching.
- The DLL does not claim native scroll-state support. Browsers therefore end
  through existing repeated-frame and safety-limit logic.
- No browser extension, DOM injection, Native Messaging, named pipe, or
  network dependency is used.

## Deferred Scope

- Firefox: requires a dedicated Firefox viewport resolver.
- Other browsers: requires an explicit user-facing generic-browser mode and
  conservative failure handling.

Both are explicitly rejected by the Chromium resolver in this increment, so
the program will not claim support that has not been implemented.

## Delivered Files

- `src/longshot/browser_longshot_resolver.*`: Chromium root/renderer matching.
- `src/longshot/browser_longshot_plugin.cpp`: built-in plugin entry point.
- `src/longshot/CMakeLists.txt`: builds and deploys the browser DLL.
- `tests/browser_longshot_profile_test.cpp`: Chromium acceptance plus Firefox,
  generic-browser, and ordinary-window rejection cases.
- `tests/builtin_longshot_plugin_test.cpp`: verifies the packaged browser DLL
  and its no-native-scroll-state capability.

## Validation

`build.bat Release test` with Visual Studio 2019 x64 completes successfully:
343 enabled tests pass; one existing smoke test remains disabled.
