# AudioStream

`AudioStream` owns a single cubeb stream and turns the backend's raw frame
counter into a media-time clock. It is owned by `AudioSink`, which is wrapped by
[AudioSinkWrapper](AudioSinkWrapper.md), and that wrapper is what
`MediaDecoderStateMachine` reads the playback position from. So the clock
described here is what the video sink synchronises against.

Two responsibilities live here and are worth keeping apart: handing decoded
audio to the backend, on the backend's realtime callback thread, and reporting a
media position, on the state machine's thread. `AudioClock` and its
`FrameHistory` are the bridge between them.

## The two frame cursors

A backend exposes two frame counts, and they do not mean the same thing.

- The **write cursor** is what the data callback has handed over. It moves in
  one jump per callback, then sits still.
- The **play cursor** is what the device has actually output, read through
  `cubeb_stream_get_position`. It rises continuously, driven by the hardware
  clock.

The gap between them is audio handed over but not yet heard. This document calls
it the **unplayed** audio.

```
frames ---------------------------------------------------------------->

                     P                                W
                     |                                |
  ..... played ......|........... unplayed ...........|
                     |<------------------------------>|
                     |  handed to the backend,        |
                     |  not yet heard                 |

  P = play cursor    (cubeb_stream_get_position)
  W = write cursor   (what the data callback has handed over)
  unplayed = W - P
```

The unplayed audio is the cushion that lets a slightly late callback still find
audio queued ahead of it, so it does not reach zero during healthy playback. Its
size is the device's output latency plus the part of the last callback the
backend has not yet interpolated past, so it jumps up by one callback on each
callback and slides back down to the latency in between.

Two consequences matter below:

- The play cursor normally stays at or below the write cursor, because it
  reports what the device has consumed and a device cannot consume what nobody
  handed it. How it is derived varies: some backends compute it as the frames
  handed over minus the latency they report, others return an OS or device
  counter. Either way it is an estimate rather than a hardware readout, and some
  backends extrapolate from the wall clock since the last callback, so a late
  callback or a device xrun can report it above the write cursor. The frame
  history can also account for less than the device has played, because a
  callback that found the queue full keeps its frames on the audio thread until
  a later call collects them.
- The unplayed audio belongs to the position that was current when it was handed
  over, not to anything that happens afterwards.

## The frame history

`FrameHistory` maps a play-cursor value to a media time. It is a piecewise
mapping rather than a single division, because silence carries no media time and
the playback rate can change mid-stream.

| Member          | Unit         | Meaning                            |
|-----------------|--------------|------------------------------------|
| `mBaseOffset`   | frames       | Where the retained history starts  |
| `mBasePosition` | microseconds | The media time at `mBaseOffset`    |
| `mChunks`       | both         | Segments from `mBaseOffset` upward |

The first two are one anchor in two units: when the device had output
`mBaseOffset` frames, the media was at `mBasePosition`. `GetPosition` starts
there and walks the chunks up to the play cursor.

Per chunk, `servicedFrames` counts frames carrying media time and `totalFrames`
counts every frame output, so the difference is silence inserted on an underrun.
The clamp `min(delta, servicedFrames)` holds the clock still while that silence
is audible, and each chunk's `rate` is the output rate in force when it was
recorded, so a rate change maps correctly on both sides of it.

### The conserved identity

`mBaseOffset` starts at zero and folding a chunk away adds its `totalFrames` to
it, so this sum is invariant:

```
mBaseOffset + sum of every retained chunk's totalFrames  ==  the write cursor
```

A query normally lands inside the retained chunks, because the play cursor stays
at or below the write cursor. The exception is a callback still stranded on the
audio thread, which leaves the history short. Keeping the identity true is the
whole job: break it and the walk runs off the end, at which point `GetPosition`
falls back to `mBasePosition` alone, which is the serviced total, so the
reported position stops following the device between callbacks and jumps
instead.

## Rebasing a stream reused across a seek

When the stream is kept alive across a seek rather than destroyed, its frame
counter keeps climbing while media time has to restart, so `RebaseLive` moves
the anchor instead of resetting the counter.

Media time zero belongs at the write cursor, not the play cursor, because
everything below the write cursor was handed over before the seek. The anchor
cannot simply be set to the write cursor: the history stores `playCursor -
mBaseOffset` in a `uint32_t` and asserts the difference is not negative, so an
anchor above the play cursor trips that assertion in a debug build and wraps in
an opt one. The anchor therefore stays at the play cursor and the unplayed audio
is carried across as a chunk that services nothing:

```
frames:       P                                W
              |                                |
              |<---------- unplayed ---------->|
              |     one chunk, totalFrames =   |
              |     unplayed, servicedFrames   |
              |     = 0                        |
media time:   0 .............................. 0 ------> counts up from here
```

`totalFrames` keeps the identity intact so queries stay inside the chunks, and
`servicedFrames` of zero means draining that audio advances no media time, so
the clock holds at the seek target until the first post-seek frame is heard.
Both come from one subtraction, `W - P`.

`W` is a counter the clock keeps itself, bumped on the audio thread, not a sum
over the retained chunks: the history is only fed when the owner thread drains
the callback queue, and nothing drains it during a seek. It is read after
synchronising with that thread, since an early read understates the gap and lets
the position lead the audio for the rest of playback.

The subtraction needs both cursors, and the audio it describes has to be audio
that will still be played:

| Situation                             | Carried  |
|---------------------------------------|----------|
| Stream kept running across the seek   | `W - P`  |
| Position query failed, no `P`         | nothing  |
| `P` reported above `W`                | nothing  |
| Backend drained since the resume      | nothing  |

Carrying nothing is how the clock behaved before the carry existed. On the rows
where the anchor is still a real play cursor, the identity then holds against
the frames the device will play rather than against the counter, which still
counts the discarded ones.

The failed-query row is weaker, and deliberately so. With no cursor to anchor
at, the rebase anchors at zero, which is neither cursor, so the identity is left
broken and the reported position degrades to the serviced total: it stops
following the device between callbacks. That is tolerated because the query only
fails on a stream that is already dead, after which the sink pins the position
to the last good value and this mapping is never consulted.

A rebase also has to neutralise callback information that has not reached the
history yet. The reader counts the frames of every item it takes off the queue,
and a rebase records the count it accounted for, so an item landing at or below
that point is dropped rather than appended. Without that, an item stranded on
the audio thread during the seek would be applied afterwards and, being
underrun-only, would merge into the carried chunk and extend the window that
advances no media time by the whole stranded silence.

The unplayed audio may be leftover pre-seek audio, silence produced during the
seek, or a mixture. Either way it was handed over before the seek and is not
part of the new position.

## Threading

- The data callback runs on the backend's realtime thread and must not block or
  allocate.
- `UpdateFrameHistory` is called from that callback; `GetPosition` from the
  state machine's thread.
- On macOS the position read is deliberately lock free. Callback information
  reaches the reader through a single-producer single-consumer queue, applied by
  `AudioClock::ApplyQueuedCallbackInfo`, which both `GetPosition` and `Rebase`
  call before touching the history. Anything mutating the history must run on
  that reader thread and touch only reader-side state; the callback may still be
  running.
- Elsewhere a mutex guards the history on both sides.
