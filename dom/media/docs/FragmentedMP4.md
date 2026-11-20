# Sample Times in Fragmented MP4

This document explains how `MoofParser` reads a fragmented MP4 file and gives
each sample a decode time and a composition range. It also gives the reason
for each step. The examples use small numbers.

## Plain and fragmented MP4

A plain MP4 file keeps one sample table for each track in `moov/stbl`.
`MP4Metadata` reads that table through mp4parse. The `MP4SampleIndex`
constructor then copies the samples into `MP4SampleIndex::mIndex`.

A fragmented MP4 file splits the samples into fragments. Each fragment is a
`moof` box and an `mdat` box. The `moof` box has a `traf` box for each track.
The `traf` box has a `tfdt` box and one or more `trun` boxes. When the `moov`
box has no samples, `MP4SampleIndex` creates a `MoofParser`. The parser makes
one `Moof` object for each `moof` box, and each `Moof` has its own `mIndex`.

| File type  | Where the samples are | Index                         |
| ---------- | --------------------- | ----------------------------- |
| Plain      | `moov/stbl`           | `MP4SampleIndex::mIndex`      |
| Fragmented | `trun` in each `moof` | `Moof::mIndex`, one per moof  |

MSE must use fragmented MP4. In the ISO BMFF byte stream format for MSE, a
media segment is a `moof` box and its `mdat` box.

## Where the parser runs

For file playback, the call chain is:

```
MP4TrackDemuxer::EnsureUpToDateIndex
  MP4SampleIndex::UpdateMoofIndex
    MoofParser::RebuildFragmentedIndex
      Moof::Moof           one call for each moof box
        Moof::ParseTraf
          Moof::ParseTrun
```

`SampleIterator::GetNext` then makes a `MediaRawData` from each sample:

| `MediaRawData` field | Value from the sample          |
| -------------------- | ------------------------------ |
| `mTime`              | start of `mCompositionRange`   |
| `mDuration`          | length of `mCompositionRange`  |
| `mTimecode`          | `mDecodeTime`                  |
| `mKeyframe`          | `mSync`                        |

For MSE, `TrackBuffersManager::CreateDemuxerforMIMEType` creates an
`MP4Demuxer` on the appended data. The demuxer uses the same path. Then
`TrackBuffersManager::ProcessFrames` runs the MSE coded frame processing on
the samples.

## Step 1: read each trun entry

`Moof::ParseTraf` starts the decode time at the `tfdt` value. If the `traf`
box has no valid `tfdt`, it continues from the decode end of the previous
moof.

`Moof::ParseTrun` then reads one entry for each sample. For each entry, it
does these steps:

1. It sets the decode time (DTS) of the sample to the current decode time.
2. It sets the composition range to `[DTS + ctsOffset, DTS + ctsOffset +
   duration)`.
3. It adds the entry duration to the current decode time.

The edit list moves both times by the same amount. The examples have no edit
list.

The `trun` duration is a decode delta: the time from this DTS to the next DTS.
It is not the time that the frame stays on the screen.

## Step 2: decode order and display order

The parser stores the samples in decode order. B-frames make the display order
different from the decode order. The `ctsOffset` of each entry moves its
composition time (PTS) away from its DTS.

For example, a 30 fps H.264 stream has two B-frames between its I- and
P-frames. Its timescale is 15360, so one frame is 512 ticks. The encoder puts
three samples in each moof. The table shows the first two moofs in decode
order.

| moof | type | DTS  | PTS  |
| ---- | ---- | ---- | ---- |
| 1    | I    | 0    | 512  |
| 1    | P    | 512  | 2048 |
| 1    | B    | 1024 | 1024 |
| 2    | B    | 1536 | 1536 |
| 2    | P    | 2048 | 3584 |
| 2    | B    | 2560 | 2560 |

In display order, moof 1 has the frames at 512, 1024 and 2048. The frame at
1536 is in moof 2. So moof 1 has a gap in its composition times, and the next
moof fills it.

```
PTS     512   1024  1536  2048  2560
        |-----|-----|-----|-----|
moof 1  [ I ] [ B ]       [ P ]
moof 2              [ B ]       [ B ] ...
```

## Step 3: close the composition gaps

After `ParseTrun`, each sample has the composition range from its `trun`
duration. But the `trun` duration is a decode delta, so it can be different
from the display spacing. For example, a stream has a timescale of 90000, so
one frame is 3000 ticks. In decode order, its first `trun` duration is 3000
ticks. Then the durations alternate between 1 and 5999 ticks, and one of them
is 6149. But its composition times are about 3000 ticks apart. With the `trun`
durations only, the sample at 6000 has a duration of 1 tick, and a gap of one
frame follows it.

So `Moof::Moof` sorts the samples of the moof by composition start. Then it
sets the end of each sample to a new value.

### Why not end at the next start

A simple rule sets each end to the start of the next sample in the moof. This
closes every gap in the moof. But it also closes a gap that the next moof
fills. In the B-frame example, the frame at 1024 gets the end 2048, and it
covers the frame at 1536 in moof 2.

| PTS  | `trun` end | simple rule end | bounded rule end |
| ---- | ---------- | --------------- | ---------------- |
| 512  | 1024       | 1024            | 1024             |
| 1024 | 1536       | 2048            | 1536             |
| 2048 | 2560       | 2560            | 2560             |

The video sink shows a frame until the clock passes its end. So the frame at
1024 stays on the screen too long. Then the sink drops the frames that start
inside its span.

### The bounded rule

`Moof::Moof` limits each end:

```
end = min(start of the next sample, start + longest duration in this moof)
```

The longest `trun` duration in the moof is the bound. In the B-frame example,
every duration is 512. So the frame at 1024 ends at 1536.

In the example with uneven durations, the longest duration is 6149. The sample
at 6000 has a duration of 1 tick, and the next sample starts at 9001. The gap
is smaller than the bound, so the sample ends at 9001.

### Fallbacks

The rule uses the start of the next sample as the end in two cases:

| Case                                  | Why                              |
| ------------------------------------- | -------------------------------- |
| The longest duration is 0             | A zero bound gives empty samples |
| `start + longest` is too large        | The sum is not a valid time      |

### Known limit

One long sample raises the bound for all the samples in its moof. Suppose that
in the B-frame example, the frame at 1024 in moof 1 has a duration of 7680
ticks. The bound becomes 7680, so the gap does not limit the end, and in moof
1 alone the frame at 1024 ends at 2048 again. The next section shows how the
next moof ends it at 1536:

| PTS  | end  |
| ---- | ---- |
| 512  | 1024 |
| 2048 | 2560 |
| 1024 | 2048 |

### Ends across moofs

ISO BMFF stores no display duration. A sample duration is the step to the next
decode time (ISO/IEC 14496-12, 8.6.1.2), and a sample has a composition time
(8.6.1.3) but no composition duration; only the composition duration of the
last sample is called unclear (8.6.1.4). So a sample is shown until the next
composition time in the track, which can be in a later moof. For example, in
variable frame rate content, the next frame can come after a long display gap,
in the next moof.

Suppose that every sample duration is 512 ticks, and that after the frame at
2048, the next frame comes at 17920, in moof 2. Moof 1 alone gives the frame at
2048 its own duration, so it ends at 2560:

```
PTS      512   1024  1536  2048  2560                17920 18432
moof 1   [ I ] [ B ]       [ P ]
moof 2               [ B ]                           [ P ]
```

When moof 2 is added, the next composition time is known, and the frame at
2048 is extended to it:

```
PTS      512   1024  1536  2048                      17920 18432
moof 1   [ I ] [ B ]       [ P ......................]
moof 2               [ B ]                           [ P ]
```

A sample can also come in between later. For example, with one frame per moof
and B-frames, the P-frame comes before the B-frames that are shown before it.
Moof 2 extends the I-frame to the P-frame, and then the B-frames of moofs 3 and
4 start inside the I-frame:

```
PTS      512   1024  1536  2048
moof 1   [ I .............]
moof 2                     [ P ]
moof 3         [ B ]
moof 4               [ B ]
```

So when a new sample starts inside an earlier sample, and the earlier sample
ends at the start of the next earlier sample, that end came from the next
sample, and the earlier sample is ended at the new sample. The new samples are
then extended up to that next earlier sample, so the time stays covered:

```
PTS      512   1024  1536  2048
moof 1   [ I ]
moof 2                     [ P ]
moof 3         [ B ]
moof 4               [ B ]
```

An earlier sample that ends at no sample start keeps its own duration. Only
samples next to the moof boundary in composition order can change. The search
covers up to 16 samples before the new moof, the largest decoded picture buffer
of H.264 and HEVC. A longer run of non-reference frames can put the next frame
further back, and then the search misses it.

## Step 4: rewrite the decode times

MSE coded frame processing checks each DTS. `TrackBuffersManager::ProcessFrames`
finds a discontinuity in two cases:

- The DTS is smaller than the last DTS.
- The DTS is more than 2 * `mLongestFrameDuration` after the last DTS.

After a discontinuity, the track needs a random access point. So
`ProcessFrames` drops each frame until the next keyframe.

The DTS from `ParseTrun` steps by the `trun` durations. But Step 3 changed the
durations, so the two can be different. So `Moof::Moof` rewrites the DTS of
each sample in the moof. It keeps the decode span of the moof, from the first
DTS to the decode end of the `trun` entries. It spreads that span over the
samples in proportion to their durations:

```
adjust = decode span / sum of the sample durations
DTS(n) = first DTS + (sum of the durations before sample n) * adjust
```

The last DTS is then never above the decode end of the `trun` entries. That
end is the start of the next moof only when the next `tfdt` matches the `trun`
durations.

### Normal case

In moof 1 of the B-frame example, the decode span is 3 * 512 = 1536. The sum
of the durations is also 1536. So `adjust` is 1.0, and the DTS values stay 0,
512 and 1024. The next moof starts at 1536.

### Uneven case

In the example with uneven durations, a moof has a decode span of 69150 ticks.
After Step 3, the sum of the durations is 72150 ticks. So `adjust` is
69150 / 72150, about 0.958. The first samples get these values:

| PTS  | end  | DTS  |
| ---- | ---- | ---- |
| 0    | 3001 | 0    |
| 6000 | 9001 | 2876 |
| 3001 | 6000 | 5752 |

### Why the sum and not the composition span

The composition span of the moof could also be the denominator. But the span
includes the gaps that the next moof fills. In moof 1 of the B-frame example,
the span is 2560 - 512 = 2048. The sum of the durations is 1536.

| Denominator      | Value | `adjust` | DTS values   | Step into moof 2 |
| ---------------- | ----- | -------- | ------------ | ---------------- |
| Composition span | 2048  | 0.75     | 0, 384, 768  | 768              |
| Sum of durations | 1536  | 1.0      | 0, 512, 1024 | 512              |

With the span, the DTS steps do not follow the sample durations.

## Step 5: the moof time range

`Moof::mTimeRange` is the composition span of the moof: from the first
composition start to the last composition end. It includes the gaps that the
next moof fills. `Moof::FixRounding` moves its end to the start of the next
moof when the gap between them is small. The limit is 35 ms plus one tick for
each sample.

These functions use `mTimeRange`:

| Function                                        | Use                     |
| ----------------------------------------------- | ----------------------- |
| `MP4SampleIndex::ConvertByteRangesToTimeRanges` | buffered ranges         |
| `MP4SampleIndex::GetEvictionOffset`             | data that it can remove |
| `MoofParser::GetCompositionRange`               | time span of the data   |
