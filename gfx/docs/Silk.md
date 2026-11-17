# Silk Overview

```{image} SilkArchitecture.png
```

## Architecture

Our current architecture is to align three components to hardware vsync
timers:

1. Compositor
2. RefreshDriver / Painting
3. Input Events

The flow of our rendering engine is as follows:

1. Hardware Vsync event occurs on an OS specific *Hardware Vsync Thread*
   on a per monitor basis.
2. The *Hardware Vsync Thread* attached to the monitor notifies the
   `CompositorVsyncDispatchers` and `VsyncDispatcher`.
3. For every Firefox window on the specific monitor, notify a
   `CompositorVsyncDispatcher`. The `CompositorVsyncDispatcher` is
   specific to one window.
4. The `CompositorVsyncDispatcher` notifies a
   `CompositorWidgetVsyncObserver` when remote compositing, or a
   `CompositorVsyncScheduler::Observer` when compositing in-process.
5. If remote compositing, a vsync notification is sent from the
   `CompositorWidgetVsyncObserver` to the `VsyncBridgeChild` on the
   UI process, which sends an IPDL message to the `VsyncBridgeParent`
   on the compositor thread of the GPU process, which then dispatches to
   `CompositorVsyncScheduler::Observer`.
6. The `VsyncDispatcher` notifies the Chrome
   `RefreshTimer` that a vsync has occurred.
7. The `VsyncDispatcher` sends IPC messages to all content
   processes to tick their respective active `RefreshTimer`.
8. The `Compositor` dispatches input events on the *Compositor
   Thread*, then composites. Input events are only dispatched on the
   *Compositor Thread* on b2g.
9. The `RefreshDriver` paints on the *Main Thread*.

## Hardware Vsync

Hardware vsync events from (1), occur on a specific `Display` Object.
The `Display` object is responsible for enabling / disabling vsync on
a per connected display basis. For example, if two monitors are
connected, two `Display` objects will be created, each listening to
vsync events for their respective displays. We require one `Display`
object per monitor as each monitor may have different vsync rates. As a
fallback solution, we have one global `Display` object that can
synchronize across all connected displays. The global `Display` is
useful if a window is positioned halfway between the two monitors. Each
platform will have to implement a specific `Display` object to hook
and listen to vsync events. As of this writing, both Firefox OS and OS X
create their own hardware specific *Hardware Vsync Thread* that executes
after a vsync has occurred. OS X creates one *Hardware Vsync Thread* per
`CVDisplayLinkRef`. We do not currently support multiple displays, so
we use one global `CVDisplayLinkRef` that works across all active
displays. On Windows, we have to create a new platform `thread` that
waits for DwmFlush(), which works across all active displays. Once the
thread wakes up from DwmFlush(), the actual vsync timestamp is retrieved
from DwmGetCompositionTimingInfo(), which is the timestamp that is
actually passed into the compositor and refresh driver.

When a vsync occurs on a `Display`, the *Hardware Vsync Thread*
callback fetches all `CompositorVsyncDispatchers` associated with the
`Display`. Each `CompositorVsyncDispatcher` is notified that a vsync
has occurred with the vsync’s timestamp. It is the responsibility of the
`CompositorVsyncDispatcher` to notify the `Compositor` that is
awaiting vsync notifications. The `Display` will then notify the
associated `VsyncDispatcher`, which should notify all
active `RefreshDrivers` to tick.

All `Display` objects are encapsulated in a `VsyncSource` object.
The `VsyncSource` object lives in `gfxPlatform` and is instantiated
only on the parent process when `gfxPlatform` is created. The
`VsyncSource` is destroyed when `gfxPlatform` is destroyed. It can
also be destroyed when the layout frame rate pref (or other prefs that
influence frame rate) are changed. This may mean we switch from hardware
to software vsync (or vice versa) at runtime. During the switch, there
may briefly be 2 vsync sources. Otherwise, there is only one
`VsyncSource` object throughout the entire lifetime of Firefox. Each
platform is expected to implement their own `VsyncSource` to manage
vsync events. On OS X, this is through `CVDisplayLinkRef`. On
Windows, it should be through `DwmGetCompositionTimingInfo`.

## Compositor

When the `CompositorVsyncDispatcher` is notified of the vsync event,
the `CompositorVsyncScheduler::Observer` associated with the
`CompositorVsyncDispatcher` begins execution. Since the
`CompositorVsyncDispatcher` executes on the *Hardware Vsync Thread*
and the `Compositor` composites on the `CompositorThread`, the
`CompositorVsyncScheduler::Observer` posts a task to the
`CompositorThread`. The `CompositorBridgeParent` then composites.
The model where the `CompositorVsyncDispatcher` notifies components on
the *Hardware Vsync Thread*, and the component schedules the task on the
appropriate thread is used everywhere.

The `CompositorVsyncScheduler::Observer` listens to vsync events as
needed and stops listening to vsync when composites are no longer
scheduled or required. Every `CompositorBridgeParent` is associated
and tied to one `CompositorVsyncScheduler::Observer`, which is
associated with the `CompositorVsyncDispatcher`. Each
`CompositorBridgeParent` is associated with one widget and is created
when a new platform window or `nsIWidget` is created. The
`CompositorBridgeParent`, `CompositorVsyncDispatcher`,
`CompositorVsyncScheduler::Observer`, and `nsIWidget` all have
the same lifetimes, which are created and destroyed together.

## Out-of-process Compositors

When compositing out-of-process, this model changes slightly. In this
case there are effectively two observers: a UI process observer
(`CompositorWidgetVsyncObserver`), and the
`CompositorVsyncScheduler::Observer` in the GPU process. There are
also two dispatchers: the widget dispatcher in the UI process
(`CompositorVsyncDispatcher`), and the IPDL-based dispatcher in the
GPU process (`CompositorBridgeParent::NotifyVsync`). The UI process
observer and the GPU process dispatcher are linked via an IPDL protocol
called PVsyncBridge. `PVsyncBridge` is a top-level protocol for
sending vsync notifications to the compositor thread in the GPU process.
The compositor controls vsync observation through a separate actor,
`PCompositorWidget`, which (as a subactor for
`CompositorBridgeChild`) links the compositor thread in the GPU
process to the main thread in the UI process.

Out-of-process compositors do not go through
`CompositorVsyncDispatcher` directly. Instead, the
`CompositorWidgetDelegate` in the UI process creates one, and gives it
a `CompositorWidgetVsyncObserver`. This observer forwards
notifications to a Vsync I/O thread, where `VsyncBridgeChild` then
forwards the notification again to the compositor thread in the GPU
process. The notification is received by a `VsyncBridgeParent`. The
GPU process uses the layers ID in the notification to find the correct
compositor to dispatch the notification to.

## CompositorVsyncDispatcher

The `CompositorVsyncDispatcher` executes on the *Hardware Vsync
Thread*. It contains references to the `nsIWidget` it is associated
with and has a lifetime equal to the `nsIWidget`. The
`CompositorVsyncDispatcher` is responsible for notifying the
`CompositorBridgeParent` that a vsync event has occurred. There can be
multiple `CompositorVsyncDispatchers` per `Display`, one
`CompositorVsyncDispatcher` per window. The only responsibility of the
`CompositorVsyncDispatcher` is to notify components when a vsync event
has occurred, and to stop listening to vsync when no components require
vsync events. We require one `CompositorVsyncDispatcher` per window so
that we can handle multiple `Displays`. When compositing in-process,
the `CompositorVsyncDispatcher` is attached to the CompositorWidget
for the window. When out-of-process, it is attached to the
CompositorWidgetDelegate, which forwards observer notifications over
IPDL. In the latter case, its lifetime is tied to a CompositorSession
rather than the nsIWidget.

## Multiple Displays

The `VsyncSource` has an API to switch a `CompositorVsyncDispatcher`
from one `Display` to another `Display`. For example, when one
window either goes into full screen mode or moves from one connected
monitor to another. When one window moves to another monitor, we expect
a platform specific notification to occur. The detection of when a
window enters full screen mode or moves is not covered by Silk itself,
but the framework is built to support this use case. The expected flow
is that the OS notification occurs on `nsIWidget`, which retrieves the
associated `CompositorVsyncDispatcher`. The
`CompositorVsyncDispatcher` then notifies the `VsyncSource` to
switch to the correct `Display` the `CompositorVsyncDispatcher` is
connected to. Because the notification works through the `nsIWidget`,
the actual switching of the `CompositorVsyncDispatcher` to the correct
`Display` should occur on the *Main Thread*. The current
implementation of Silk does not handle this case and needs to be built
out.

## CompositorVsyncScheduler::Observer

The `CompositorVsyncScheduler::Observer` handles the vsync
notifications and interactions with the `CompositorVsyncDispatcher`.
When the `Compositor` requires a scheduled composite, it notifies the
`CompositorVsyncScheduler::Observer` that it needs to listen to vsync.
The `CompositorVsyncScheduler::Observer` then observes / unobserves
vsync as needed from the `CompositorVsyncDispatcher` to enable
composites.

## GeckoTouchDispatcher

The `GeckoTouchDispatcher` is a singleton that resamples touch events
to smooth out jank while tracking a user’s finger. Because input and
composite are linked together, the
`CompositorVsyncScheduler::Observer` has a reference to the
`GeckoTouchDispatcher` and vice versa.

## Input Events

One large goal of Silk is to align touch events with vsync events. On
Firefox OS, touchscreens often have different touch scan rates than the
display refreshes. A Flame device has a touch refresh rate of 75 HZ,
while a Nexus 4 has a touch refresh rate of 100 HZ, while the device’s
display refresh rate is 60HZ. When a vsync event occurs, we resample
touch events, and then dispatch the resampled touch event to APZ. Touch
events on Firefox OS occur on a *Touch Input Thread* whereas they are
processed by APZ on the *APZ Controller Thread*. We use [Google
Android’s touch
resampling](https://web.archive.org/web/20200909082458/http://www.masonchang.com/blog/2014/8/25/androids-touch-resampling-algorithm)
algorithm to resample touch events.

Currently, we have a strict ordering between Composites and touch
events. When a touch event occurs on the *Touch Input Thread*, we store
the touch event in a queue. When a vsync event occurs, the
`CompositorVsyncDispatcher` notifies the `Compositor` of a vsync
event, which notifies the `GeckoTouchDispatcher`. The
`GeckoTouchDispatcher` processes the touch event first on the *APZ
Controller Thread*, which is the same as the *Compositor Thread* on b2g,
then the `Compositor` finishes compositing. We require this strict
ordering because if a vsync notification is dispatched to both the
`Compositor` and `GeckoTouchDispatcher` at the same time, a race
condition occurs between processing the touch event and therefore
position versus compositing. In practice, this creates very janky
scrolling. As of this writing, we have not analyzed input events on
desktop platforms.

One slight quirk is that input events can start a composite, for example
during a scroll and after the `Compositor` is no longer listening to
vsync events. In these cases, we notify the `Compositor` to observe
vsync so that it dispatches touch events. If touch events were not
dispatched, and since the `Compositor` is not listening to vsync
events, the touch events would never be dispatched. The
`GeckoTouchDispatcher` handles this case by always forcing the
`Compositor` to listen to vsync events while touch events are
occurring.

## Widget, Compositor, CompositorVsyncDispatcher, GeckoTouchDispatcher Shutdown Procedure

When the [nsIWidget shuts
down](https://github.com/mozilla-firefox/firefox/blob/1741f5a87fb42eeb5a047eea64574a960a2a0b0e/widget/nsBaseWidget.cpp#L182)
\- It calls nsIWidget::DestroyCompositor on the *Gecko Main Thread*.
During nsIWidget::DestroyCompositor, it first destroys the
CompositorBridgeChild. CompositorBridgeChild sends a sync IPC call to
CompositorBridgeParent::RecvStop, which calls
[CompositorBridgeParent::Destroy](https://github.com/mozilla-firefox/firefox/blob/08c73afb6e8f6c0f6bbe754e93f33059d7085910/gfx/layers/ipc/CompositorParent.cpp#L509).
During this time, the *main thread* is blocked on the parent process.
CompositorBridgeParent::RecvStop runs on the *Compositor thread* and
cleans up some resources, including setting the
`CompositorVsyncScheduler::Observer` to nullptr.
CompositorBridgeParent::RecvStop also explicitly keeps the
CompositorBridgeParent alive and posts another task to run
CompositorBridgeParent::DeferredDestroy on the Compositor loop so that
all ipdl code can finish executing. The
`CompositorVsyncScheduler::Observer` also unobserves from vsync and
cancels any pending composite tasks. Once
CompositorBridgeParent::RecvStop finishes, the *main thread* in the
parent process continues shutting down the nsIWidget.

At the same time, the *Compositor thread* is executing tasks until
CompositorBridgeParent::DeferredDestroy runs, which flushes the
compositor message loop. Now we have two tasks as both the nsIWidget
releases a reference to the Compositor on the *main thread* during
destruction and the CompositorBridgeParent::DeferredDestroy releases a
reference to the CompositorBridgeParent on the *Compositor Thread*.
Finally, the CompositorBridgeParent itself is destroyed on the *main
thread* once both references are gone due to explicit [main thread
destruction](https://github.com/mozilla-firefox/firefox/blob/be419eafa72df858a3a1ac73e0794782a70abd04/gfx/layers/ipc/CompositorParent.h#L148).

With the `CompositorVsyncScheduler::Observer`, any accesses to the
widget after nsIWidget::DestroyCompositor executes are invalid. Any
accesses to the compositor between the time the
nsIWidget::DestroyCompositor runs and the
CompositorVsyncScheduler::Observer’s destructor runs aren’t safe yet a
hardware vsync event could occur between these times. Since any tasks
posted on the Compositor loop after
CompositorBridgeParent::DeferredDestroy is posted are invalid, we make
sure that no vsync tasks can be posted once
CompositorBridgeParent::RecvStop executes and DeferredDestroy is posted
on the Compositor thread. When the sync call to
CompositorBridgeParent::RecvStop executes, we explicitly set the
CompositorVsyncScheduler::Observer to null to prevent vsync
notifications from occurring. If vsync notifications were allowed to
occur, since the `CompositorVsyncScheduler::Observer`’s vsync
notification executes on the *hardware vsync thread*, it would post a
task to the Compositor loop and may execute after
CompositorBridgeParent::DeferredDestroy. Thus, we explicitly shut down
vsync events in the `CompositorVsyncDispatcher` and
`CompositorVsyncScheduler::Observer` during nsIWidget::Shutdown to
prevent any vsync tasks from executing after
CompositorBridgeParent::DeferredDestroy.

The `CompositorVsyncDispatcher` may be destroyed on either the *main
thread* or *Compositor Thread*, since both the nsIWidget and
`CompositorVsyncScheduler::Observer` race to destroy on different
threads. nsIWidget is destroyed on the *main thread* and releases a
reference to the `CompositorVsyncDispatcher` during destruction. The
`CompositorVsyncScheduler::Observer` has a race to be destroyed either
during CompositorBridgeParent shutdown or from the
`GeckoTouchDispatcher` which is destroyed on the main thread with
[ClearOnShutdown](https://github.com/mozilla-firefox/firefox/blob/a6f3234490eb65fa2c29195205b2579bbf621020/xpcom/base/ClearOnShutdown.h#L15).
Whichever object, the CompositorBridgeParent or the
`GeckoTouchDispatcher` is destroyed last will hold the last reference
to the `CompositorVsyncDispatcher`, which destroys the object.

## Refresh Driver

The Refresh Driver is ticked from a [single active
timer](https://github.com/mozilla-firefox/firefox/blob/08c73afb6e8f6c0f6bbe754e93f33059d7085910/layout/base/nsRefreshDriver.cpp#L11).
The assumption is that there are multiple `RefreshDrivers` connected
to a single `RefreshTimer`. There are two `RefreshTimers`: an active
and an inactive `RefreshTimer`. Each Tab has its own
`RefreshDriver`, which connects to one of the global
`RefreshTimers`. The `RefreshTimers` execute on the *Main Thread*
and tick their connected `RefreshDrivers`. We do not want to break
this model of multiple `RefreshDrivers` per a set of two global
`RefreshTimers`. Each `RefreshDriver` switches between the active
and inactive `RefreshTimer`.

Instead, we create a new `RefreshTimer`, the `VsyncRefreshTimer`
which ticks based on vsync messages. We replace the current active timer
with a `VsyncRefreshTimer`. All tabs will then tick based on this new
active timer. Since the `RefreshTimer` has a lifetime of the process,
we only need to create a single `VsyncDispatcher` per
`Display` when Firefox starts. Even if we do not have any content
processes, the Chrome process will still need a `VsyncRefreshTimer`,
thus we can associate the `VsyncDispatcher` with each
`Display`.

When Firefox starts, we initially create a new `VsyncRefreshTimer` in
the Chrome process. The `VsyncRefreshTimer` will listen to vsync
notifications from `VsyncDispatcher` on the global
`Display`. When nsRefreshDriver::Shutdown executes, it will delete the
`VsyncRefreshTimer`. This creates a problem as all the
`RefreshTimers` are currently manually memory managed whereas
`VsyncObservers` are ref counted. To work around this problem, we
create a new `RefreshDriverVsyncObserver` as an inner class to
`VsyncRefreshTimer`, which actually receives vsync notifications. It
then ticks the `RefreshDrivers` inside `VsyncRefreshTimer`.

With Content processes, the start up process is more complicated. We
send vsync IPC messages via the use of the PBackground thread on the
parent process, which allows us to send messages from the Parent
process’ without waiting on the *main thread*. This sends messages from
the Parent::*PBackground Thread* to the Child::*Main Thread*. The
*main thread* receiving IPC messages on the content process is
acceptable because `RefreshDrivers` must execute on the *main thread*.
However, there is some amount of time required to setup the IPC
connection upon process creation and during this time, the
`RefreshDrivers` must tick to set up the process. To get around this,
we initially use software `RefreshTimers` that already exist during
content process startup and swap in the `VsyncRefreshTimer` once the
IPC connection is created.

During nsRefreshDriver::ChooseTimer, we create an async PBackground IPC
open request to create a `VsyncParent` and `VsyncChild`. At the same
time, we create a software `RefreshTimer` and tick the
`RefreshDrivers` as normal. Once the PBackground callback is executed
and an IPC connection exists, we swap all `RefreshDrivers` currently
associated with the active `RefreshTimer` and swap the
`RefreshDrivers` to use the `VsyncRefreshTimer`. Since all
interactions on the content process occur on the main thread, there are
no need for locks. The `VsyncParent` listens to vsync events through
the `VsyncRefreshTimerDispatcher` on the parent side and sends vsync
IPC messages to the `VsyncChild`. The `VsyncChild` notifies the
`VsyncRefreshTimer` on the content process.

During the shutdown process of the content process, ActorDestroy is
called on the `VsyncChild` and `VsyncParent` due to the normal
PBackground shutdown process. Once ActorDestroy is called, no IPC
messages should be sent across the channel. After ActorDestroy is
called, the IPDL machinery will delete the **VsyncParent/Child** pair.
The `VsyncParent`, due to being a `VsyncObserver`, is ref counted.
After `VsyncParent::ActorDestroy` is called, it unregisters itself
from the `VsyncDispatcher`, which holds the last reference
to the `VsyncParent`, and the object will be deleted.

Thus the overall flow during normal execution is:

1. VsyncSource::Display::VsyncDispatcher receives a Vsync
   notification from the OS in the parent process.
2. VsyncDispatcher notifies
   VsyncRefreshTimer::RefreshDriverVsyncObserver that a vsync occurred on
   the parent process on the hardware vsync thread.
3. VsyncDispatcher notifies the VsyncParent on the hardware
   vsync thread that a vsync occurred.
4. The VsyncRefreshTimer::RefreshDriverVsyncObserver in the parent
   process posts a task to the main thread that ticks the refresh
   drivers.
5. VsyncParent posts a task to the PBackground thread to send a vsync
   IPC message to VsyncChild.
6. VsyncChild receive a vsync notification on the content process on the
   main thread and ticks their respective RefreshDrivers.

## Compressing Vsync Messages

Vsync messages occur quite often and the *main thread* can be busy for
long periods of time due to JavaScript. Consistently sending vsync
messages to the refresh driver timer can flood the *main thread* with
refresh driver ticks, causing even more delays. To avoid this problem,
we compress vsync messages on both the parent and child processes.

On the parent process, newer vsync messages update a vsync timestamp but
do not actually queue any tasks on the *main thread*. Once the parent
process’ *main thread* executes the refresh driver tick, it uses the
most updated vsync timestamp to tick the refresh driver. After the
refresh driver has ticked, one single vsync message is queued for
another refresh driver tick task. On the content process, the IPDL
`compress` keyword automatically compresses IPC messages.

## Multiple Monitors

In order to have multiple monitor support for the `RefreshDrivers`, we
have multiple active `RefreshTimers`. Each `RefreshTimer` is
associated with a specific `Display` via an id and tick when it’s
respective `Display` vsync occurs. We have **N RefreshTimers**, where
N is the number of connected displays. Each `RefreshTimer` still has
multiple `RefreshDrivers`.

When a tab or window changes monitors, the `nsIWidget` receives a
display changed notification. Based on which display the window is on,
the window switches to the correct `VsyncDispatcher` and
`CompositorVsyncDispatcher` on the parent process based on the display
id. Each `TabParent` should also send a notification to their child.
Each `TabChild`, given the display ID, switches to the correct
`RefreshTimer` associated with the display ID. When each display vsync
occurs, it sends one IPC message to notify vsync. The vsync message
contains a display ID, to tick the appropriate `RefreshTimer` on the
content process. There is still only one **VsyncParent/VsyncChild**
pair, just each vsync notification will include a display ID, which maps
to the correct `RefreshTimer`.

## Object Lifetime

1. CompositorVsyncDispatcher - Lives as long as the nsIWidget
   associated with the VsyncDispatcher
2. CompositorVsyncScheduler::Observer - Lives and dies the same time as
   the CompositorBridgeParent.
3. VsyncDispatcher - As long as the associated display
   object, which is the lifetime of Firefox.
4. VsyncSource - Lives as long as the gfxPlatform on the chrome process,
   which is the lifetime of Firefox.
5. VsyncParent/VsyncChild - Lives as long as the content process
6. RefreshTimer - Lives as long as the process

## Threads

All `VsyncObservers` are notified on the *Hardware Vsync Thread*. It
is the responsibility of the `VsyncObservers` to post tasks to their
respective correct thread. For example, the
`CompositorVsyncScheduler::Observer` will be notified on the *Hardware
Vsync Thread*, and post a task to the *Compositor Thread* to do the
actual composition.

1. Compositor Thread - Nothing changes
2. Main Thread - PVsyncChild receives IPC messages on the main thread.
   We also enable/disable vsync on the main thread.
3. PBackground Thread - Creates a connection from the PBackground thread
   on the parent process to the main thread in the content process.
4. Hardware Vsync Thread - Every platform is different, but we always
   have the concept of a hardware vsync thread. Sometimes this is
   actually created by the host OS. On Windows, we have to create a
   separate platform thread that blocks on DwmFlush().
