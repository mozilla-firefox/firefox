// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(
    clippy::significant_drop_tightening,
    reason = "Inherent in codspeed criterion_group! macro."
)]

use std::{hint::black_box, iter};

use criterion::{BatchSize, Criterion, criterion_group, criterion_main};
use neqo_common::to_u64;
use neqo_transport::send_stream::RangeTracker;

const CHUNK: usize = 1000;

fn build_coalesce(count: usize) -> RangeTracker {
    let mut used = RangeTracker::default();
    used.mark_acked(0, CHUNK); // frontier at CHUNK
    // One big Sent range covering exactly the gap and all the acked chunks placed below.
    used.mark_sent(to_u64(CHUNK), 2 * count * CHUNK);
    // ACK every *other* chunk so the acked ranges stay separate (a Sent chunk between
    // each prevents merging); the gap at [CHUNK, 2*CHUNK) keeps the frontier blocked.
    for i in 1..=count {
        used.mark_acked(to_u64(2 * i * CHUNK), CHUNK);
    }
    used
}

/// Acked ranges to coalesce per iteration, spread over as many trackers as that takes. A lone
/// small `mark_acked` runs in ~2 µs, so under Simulation each cache line that lands differently
/// moves it by ~2%; batching amortizes that.
const COALESCED_PER_ITER: usize = 1000;

fn coalesce(c: &mut Criterion, count: usize) {
    let trackers = COALESCED_PER_ITER.div_ceil(count);
    c.bench_function(&format!("coalesce_acked_from_zero {count} ranges"), |b| {
        b.iter_batched_ref(
            || {
                iter::repeat_with(|| build_coalesce(count))
                    .take(trackers)
                    .collect::<Vec<_>>()
            },
            // Fill the gap and jump the frontier past all `count` acked ranges in one
            // call; coalesce_acked then walks every entry below the new frontier.
            |trackers: &mut Vec<RangeTracker>| {
                for used in trackers {
                    used.mark_acked(to_u64(CHUNK), 2 * count * CHUNK);
                    black_box(used);
                }
            },
            // Each input is up to ~330 KB; batching them leaves the routine reading cold memory.
            BatchSize::PerIteration,
        );
    });
}

fn benchmark_coalesce(c: &mut Criterion) {
    coalesce(c, 1);
    coalesce(c, 3);
    coalesce(c, 10);
    coalesce(c, 1000);
}

/// Sequential sending — `used` merges into one contiguous Sent entry,
/// so the range scan `[new_off, new_off+len)` always finds nothing.
fn mark_sent_sequential(c: &mut Criterion) {
    const SENDS: usize = 1_000;
    c.bench_function("RangeTracker::mark_sent sequential", |b| {
        b.iter_batched(
            RangeTracker::default,
            |mut rt| {
                for i in 0..SENDS {
                    rt.mark_sent(to_u64(i * CHUNK), CHUNK);
                }
                black_box(rt)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Retransmission — `mark_as_lost` cuts a gap into the coalesced Sent entry for every loss,
/// so each retransmit fills a gap and merges it with the neighbouring Sent entries.
fn mark_sent_retransmit(c: &mut Criterion) {
    const SENDS: usize = 500;
    c.bench_function("RangeTracker::mark_sent retransmit", |b| {
        b.iter_batched(
            || {
                let mut rt = RangeTracker::default();
                for i in 0..SENDS {
                    rt.mark_sent(to_u64(i * CHUNK), CHUNK);
                }
                // Simulate loss: unmark every 10th chunk.
                for i in (0..SENDS).step_by(10) {
                    rt.mark_as_lost(to_u64(i * CHUNK), CHUNK);
                }
                rt
            },
            |mut rt| {
                // Retransmit the lost chunks.
                for i in (0..SENDS).step_by(10) {
                    rt.mark_sent(to_u64(i * CHUNK), CHUNK);
                }
                black_box(rt)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Gap-ACK with a single contiguous Sent entry — `new_off != self.acked`
/// (not at the frontier) but the range scan `[new_off, new_end)` returns empty
/// because the entry key starts *below* the acked range.
fn mark_acked_gap_empty_covered(c: &mut Criterion) {
    const SENDS: usize = 500;
    c.bench_function("RangeTracker::mark_acked gap-ack empty-covered", |b| {
        b.iter_batched(
            || {
                let mut rt = RangeTracker::default();
                // One big contiguous Sent range.
                rt.mark_sent(0, SENDS * CHUNK);
                rt
            },
            |mut rt| {
                // ACK every other chunk starting from chunk 1 (above frontier).
                for i in (1..SENDS).step_by(2) {
                    rt.mark_acked(to_u64(i * CHUNK), CHUNK);
                }
                black_box(rt)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Fragmented `used` map with non-contiguous Sent entries — the range
/// scan finds entries starting inside the acked range (non-empty `covered`).
fn mark_acked_fragmented(c: &mut Criterion) {
    const SENDS: usize = 500;
    c.bench_function("RangeTracker::mark_acked fragmented", |b| {
        b.iter_batched(
            || {
                let mut rt = RangeTracker::default();
                // Send only even chunks, creating a fragmented Sent map.
                for i in (0..SENDS).step_by(2) {
                    rt.mark_sent(to_u64(i * CHUNK), CHUNK);
                }
                rt
            },
            |mut rt| {
                // ACK the sent (even) chunks — covered is non-empty each time.
                for i in (0..SENDS).step_by(2) {
                    rt.mark_acked(to_u64(i * CHUNK), CHUNK);
                }
                black_box(rt)
            },
            BatchSize::SmallInput,
        );
    });
}

criterion_group! {
    name = benches;
    config = { neqo_common::log::init(None); Criterion::default() };
    targets = benchmark_coalesce,
        mark_sent_sequential,
        mark_sent_retransmit,
        mark_acked_gap_empty_covered,
        mark_acked_fragmented
}
criterion_main!(benches);
