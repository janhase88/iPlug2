# Idle call starvation explanation

Before we added the forced post-paint idle processing, the only way `OnIdle()` could run on Windows was when the hidden timer window received the custom timer message and invoked `IPlugAPIBase::OnTimer()`, which immediately forwards to `ProcessIdleTasks()` and then `OnIdle()`.

That timer message is delivered by `Timer_impl` via `PostMessageW()` to the shared message window on the UI thread. Because Windows only dispatches the timer callback when the UI thread actually drains that message, anything that keeps the queue from reaching it will starve idle processing. A plugin editor that keeps invalidating itself (for example while streaming resources during its first paints) can monopolize the message pump with back-to-back `WM_PAINT` work; if a second instance is open on the same thread the heavy painter can prevent the timer message for the other instance from ever being handled, so `OnIdle()` never fires there.

By explicitly calling `ForceProcessIdleTasks()` as soon as `WM_PAINT` finishes in legacy pacing mode we guarantee that the starvation window closes every frame, even if the timer message is still stuck behind more paint work.

## Ableton duplication state-change issue

Duplicating a track in Live instantiates the new editor, which immediately runs through its first `WM_PAINT`. Because the legacy pacing path used to call `ForceProcessIdleTasks()` synchronously from inside the paint handler, the plug-in hit `OnIdle()` before Live had a chance to deliver the copied chunk via `effSetChunk`. Any plug-in that nudged parameters from `OnIdle()` (for instance to emit host change notifications after async asset loads) would therefore report fresh automation before the incoming state arrived. Live reacts by asking for a serialize of the untouched instance, so you observe `SerializeState()` before `UnserializeState()`, and it also flags the session dirty even right after saving.

The fix keeps the “once per frame” guarantee but defers the work out of `WM_PAINT`: we now post a private `WM_APP` message after each paint, and only when that message runs do we clear the guard and call back into `ForceProcessIdleTasks()`.【F:IGraphics/Platforms/IGraphicsWin.cpp†L2390-L2400】【F:IGraphics/Platforms/IGraphicsWin.cpp†L2457-L2461】【F:IGraphics/Platforms/IGraphicsWin.cpp†L2897-L2900】 That lets the host finish delivering any pending chunk traffic before we process idle callbacks, so duplication and project saves no longer get an unsolicited state change.
