# Cooperative scripts and GameType events

This implements priorities 1 and 3 of the native inventory. The loading-reset
and loader-priority natives are unchanged.

## Threads

`Thread.init`, `start`, `stop`, `isAlive`, `sleep`, `suspend` and `resume` now
manage real cooperative executions. Windows fibers retain the C++ interpreter
and native-call stacks, allowing nested Java calls to resume at the exact
instruction after a sleep or wait. All VM execution stays on the host OS
thread; there are no concurrent Java worker threads.

- `start` queues `target.run()` for a host frame. It does not execute it inline.
- `isAlive` covers runnable, sleeping, waiting and suspended threads. It becomes
  false on completion or a stop request.
- `sleep` uses milliseconds and a steady-clock deadline. Zero yields until a
  later scheduling pass. Outside a script thread it waits through the existing
  host pump when available.
- `suspend` prevents scheduling without losing the stack; `resume` restores
  eligibility without overriding an outstanding sleep deadline.
- `stop` requests cancellation and wakes a suspended/sleeping thread. The next
  resume/checkpoint unwinds interpreter scopes before releasing the fiber.
- TREE checks its time slice every 2,048 operations. Each resumption gets about
  1-16 ms according to `Thread.setPriority` (-10 to 10; nonpositive priorities
  retain a 1 ms slice). The scheduling pass has an 8 ms soft budget, raised
  to 16 ms while a MAX_PRIORITY thread is ready, and rotates eligible threads.
  Ordinary native C++ work cannot be preempted mid-call.

`Object.wait` yields within a script fiber and uses the existing modal pump on
the host context. Waiters are tracked separately: `notify` wakes one waiting
execution, `notifyAll` wakes all. MessagePort waits return immediately if a
message is already queued. Waking a message port never clears dialog modal
flags; dialog completion removes only its own modal shield. VM/GC depth counters are switched with each stack;
scoped roots retain suspended locals, operands, arguments and construction
state. See [VM_GC.md](VM_GC.md).

The mechanic/time-display compatibility ticks are skipped while their Java
threads run. The showroom retains host camera selection, but the pose comes
from `CarSelect.run` when its thread is alive. Sections with a running message
thread no longer also drain that port through the host fallback. Tuning slider
events are queued to a live part thread, with synchronous delivery retained
only when no script thread consumes its port.

## Notifications and callbacks

Both `addNotification` overloads, `remNotification`, `registerCallback`,
`unregisterCallback` and `unregisterCallbacks` now have implementations.

Notifications identify their source by its canonical GameRef instance, so
aliases match. Adding the same listener/source/type replaces the old watch;
removal matches the exact type. Dispatch supports event-mask filtering,
low-28-bit event aliases, custom messages (`0x10000000`), custom method names,
skip-related (`0x40000000`) and one-shot (`0x80000000`) flags. One-shot watches
are removed before invoking script code, including on a mask miss. Named
methods take the stock GameRef/event/int or GameRef/event/String signatures;
the default selects `handleEventAsync` when the listener enables async mode.

The event emitter is connected to existing timer and mechanic part-pointer
events, and to trigger volumes: `scene_triggers_tick` emits
`EVENT_TRIGGER_ON` (0x20) and `EVENT_TRIGGER_OFF` (0x40) from a
`GameRef.RID_TRIGGER` source when a car enters or leaves it, with the car's
`id()` as the String param that stock handlers read with `token(0)`. Missing
collision and traffic producers must still emit their events when
implemented. Legacy UI input paths remain.

Callback modes 8, 9 and 28 correspond to `control(float)`, `drive()` and
`animate()`. They have separate ordered lists, idempotent registration and
per-object unregistration. Snapshot iteration plus registration IDs permits
callbacks to remove/re-register entries without invoking stale registrations
later in the same pass. Nested modal pumps do not re-enter callback dispatch.
The host's existing frame delta is passed to control; this is not a port of
the original engine's complete simulation/substep pipeline.

Live thread objects, notification endpoints and callback receivers are GC
roots. Resource destruction removes associated watches/hooks; VM teardown
cancels suspended executions before freeing their stacks and registries.

## Section presentation

`GameLogic.changeActiveSection` and GameState `exit` hold world presentation
while their cooperative stacks are suspended. The last complete frame stays
visible until the transition finishes; explicit loading overlays can still
be presented. This prevents a destroyed CarLot camera from exposing the free
camera and partially detached cars. The scope unwinds on cancellation as well.

## Validation

Temporary headless diagnostics exercised:

- A real TREE `GameType.run` message loop, sleep, suspension, resumption,
  cancellation and collections while its frames are suspended.
- Finite-thread completion, zero-duration yields, separate waiters and a TREE
  loop without sleep that remains preemptible, collectable and stoppable.
- Callback order, duplicate registration and removal during dispatch;
  notification aliases, custom messages, replacement, one-shot/removal and
  native registration entry points.
- Stock Init followed by cooperative frames and repeated collections.
- The actual `Mechanic.handleEvent` tuning path with stock styles and
  rectangle synchronization: slider clicks reach the part, and only explicit
  OK closes the dialog. Assignment paths such as `dlg.osd.globalHandler = part`
  evaluate the intermediate instance receiver before writing the field.
- Cold CarLot navigation using the six local saved vehicles: the first
  transition dropped from about 1.7 s to 0.84 s in the headless probe. This
  excludes GPU uploads and does not imply instantaneous cold loading.
- Fuel-rail pointer hit-testing and dragging through a live part message
  thread while a modal dialog stays open until explicit OK.

These checks do not launch a game window. Full interactive garage/showroom
behavior and compatibility with mods still require in-game verification.

## References

`repos/slrr-reverse/engine/Runtime/System/GameType.cpp` documents watch flags,
replacement/removal and callback modes. `System_natives.cpp` documents the
budgeted cooperative pump and sleeping VM threads. The source declarations and
actual callers in SLRR_rev/the installed TUFA classes supply the script
contracts. The fiber scheduler is the host implementation, not a literal
copy of the original VM's internal stack layout.

## Boot section adoption (#76)

The stock Init/GameLogic chain already enters SplashScreen in Java. After Init,
boot uses `mainloop_adopt_section`, as the frame loop does, to synchronize the
host without invoking enter again. Replaying enter replaces the OSD reference
and schedules a second timer; adoption preserves the existing presentation.

Windowed startup still removes/re-arms only the splash timer after startup
work. GameRef timers use steady wall-clock deadlines, so loading may otherwise
consume the three-second display interval even with one enter. The initial
splash frame is still presented immediately after adoption; FMV/loading-frame
handling is unchanged. No Java or native-signature change is required.

`run_save_io_tests.ps1 -Suite splash_boot` runs real shipped Init/Splash code
with the Null renderer and checks one pending timer and stable OSD identity
across repeated adoption. Requires local `game` assets; it opens no window
and does not request a new career. With `-BaselineRef a5184d2`, the same test
fails with two timers; the corrected build passes with one. Existing
`inventory_transition` tests pass 4/4. Windowed FMV/splash visual continuity
still requires user validation. See [evidence](../assets/validation/splash-boot/results.txt).

### Track presentation without a menu plate

A driving HUD normally has no fullscreen menu background. The renderer's
missing-menu backdrop must therefore be suppressed when
`fullscreen_script_camera` selects an active world camera. Without that check,
Select Track can display a solid white mask above the map but below the HUD;
opening a pause menu supplies a background and temporarily removes the mask.
The fallback's dark tint must also be passed to `draw_osd_quad`, which writes
its own color uniform; setting that uniform before the call is overwritten.

Release build and `map_views`, `ui_lifetime`, `splash_boot` checks pass. A local
headless TestTrack investigation confirmed an active scene camera and no
remaining loading overlay; it did not validate pixels. Test normal Select Track
entry, Escape/Continue, another track, Valo, and FMV/menu backgrounds in game.

### Menu input debounce

Menu-toggle and cancel hotkey presses share a 250 ms cooldown across dialog
commands: virtual axes 36/35 and physical Escape use the same gate. Select
(axis 34, physical Enter/numpad Enter) has a separate gate. A suppressed
press still updates held state, so it is not delivered later when the timeout
expires; release and a new press are required. Release events and driving axes
are not delayed. The existing command gate for directional UI navigation stays
unchanged. This is a UI-action guard, not a global keyboard or simulation delay.

`run_save_io_tests.ps1 -Suite menu_debounce` exercises the actual hotkey
dispatcher through a compiled Java native handler. Baseline `c6e563f` fails
when MENU is followed immediately by CANCEL; the corrected build passes held
input, cross-command cancellation, physical Escape, release, independent
accept and driving checks. Repressing after 275 ms succeeds. `pad_fallback`
also passes. In-game pause/Continue behavior still needs user validation.

### UI rectangles have one render owner

RenderRef.setMatrix projects script Rectangle instances into the OSD registry.
Once `_rect` and `_osd_wi` identify that handoff, scene collection must not also
submit the unscaled template mesh through a cursor or OSD preview camera. That
second submission produced a large black tooltip panel behind the compact
2D tooltip. Hidden/retired UI rectangles stay excluded while their resource
instance survives; ordinary 3D meshes are unaffected. No texture-ID or
particular-tooltip special case is used.

`run_save_io_tests.ps1 -Suite tooltip_render` uses the real frontend rectangle
mesh beneath a cursor world. Baseline `d988359` fails the one-owner assertion;
the corrected build passes visible/hidden/retired rectangle suppression and
ordinary-mesh preservation. `ui_lifetime` and `map_views` also pass. Requires
read-only local frontend assets. Pixel validation remains an in-game check.
