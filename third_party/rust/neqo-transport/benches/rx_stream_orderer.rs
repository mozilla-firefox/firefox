// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(
    clippy::significant_drop_tightening,
    reason = "Inherent in codspeed criterion_group! macro."
)]
#![expect(clippy::unwrap_used, reason = "OK in a bench.")]

use std::{collections::VecDeque, hint::black_box};

use criterion::{BatchSize, Criterion, criterion_group, criterion_main};
use neqo_common::to_u64;
use neqo_transport::recv_stream::RxStreamOrderer;

const CHUNK: usize = 1_350;
const FRAMES: u64 = 1_000;

static PAYLOAD: [u8; CHUNK] = [0u8; CHUNK];

/// In-order delivery with the app reading data after every frame.
fn inbound_in_order(c: &mut Criterion) {
    c.bench_function("RxStreamOrderer::inbound_frame in-order", |b| {
        b.iter_batched(
            RxStreamOrderer::new,
            |mut rx| {
                let mut drain = Vec::new();
                for i in 0..FRAMES {
                    rx.inbound_frame(i * to_u64(CHUNK), &PAYLOAD).unwrap();
                    rx.read_to_end(&mut drain);
                    drain.clear();
                }
                black_box(rx)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Every 50th frame (= 2%) is dropped and delivered ~20 frames later as a retransmission.
fn inbound_with_loss(c: &mut Criterion) {
    c.bench_function("RxStreamOrderer::inbound_frame 2%-loss", |b| {
        b.iter_batched(
            RxStreamOrderer::new,
            |mut rx| {
                let mut drain = Vec::new();
                let mut missing: VecDeque<u64> = VecDeque::new();
                for i in 0..FRAMES {
                    if i.is_multiple_of(50) {
                        missing.push_back(i); // "drop" this frame
                    } else {
                        rx.inbound_frame(i * to_u64(CHUNK), &PAYLOAD).unwrap();
                    }
                    // Deliver a retransmission ~20 frames later.
                    if i % 20 == 19
                        && let Some(lost) = missing.pop_front()
                    {
                        rx.inbound_frame(lost * to_u64(CHUNK), &PAYLOAD).unwrap();
                    }
                    rx.read_to_end(&mut drain);
                    drain.clear();
                }
                // Flush any remaining retransmits.
                for lost in missing {
                    rx.inbound_frame(lost * to_u64(CHUNK), &PAYLOAD).unwrap();
                    rx.read_to_end(&mut drain);
                    drain.clear();
                }
                black_box(rx)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Frames arrive reversed within each group of 4. Roughly 75 % of frames create temporary gaps.
fn inbound_reordered(c: &mut Criterion) {
    // Pre-compute the delivery order: reverse each window of 4.
    let order: Vec<u64> = (0..FRAMES)
        .collect::<Vec<_>>()
        .chunks(4)
        .flat_map(|w| w.iter().copied().rev())
        .collect();

    c.bench_function("RxStreamOrderer::inbound_frame reordered-4", |b| {
        b.iter_batched(
            RxStreamOrderer::new,
            |mut rx| {
                let mut drain = Vec::new();
                for &i in &order {
                    rx.inbound_frame(i * to_u64(CHUNK), &PAYLOAD).unwrap();
                    rx.read_to_end(&mut drain);
                    drain.clear();
                }
                black_box(rx)
            },
            BatchSize::SmallInput,
        );
    });
}

/// Every 20th frame is also delivered a second time (retransmission of already-received data).
fn inbound_duplicates(c: &mut Criterion) {
    c.bench_function("RxStreamOrderer::inbound_frame 5%-dup", |b| {
        b.iter_batched(
            RxStreamOrderer::new,
            |mut rx| {
                let mut drain = Vec::new();
                for i in 0..FRAMES {
                    rx.inbound_frame(i * to_u64(CHUNK), &PAYLOAD).unwrap();
                    if i.is_multiple_of(20) && i > 0 {
                        // Duplicate of the previous frame.
                        rx.inbound_frame((i - 1) * to_u64(CHUNK), &PAYLOAD).unwrap();
                    }
                    rx.read_to_end(&mut drain);
                    drain.clear();
                }
                black_box(rx)
            },
            BatchSize::SmallInput,
        );
    });
}

criterion_group! {
    name = benches;
    config = { neqo_common::log::init(None); Criterion::default() };
    targets = inbound_in_order, inbound_with_loss, inbound_reordered, inbound_duplicates
}
criterion_main!(benches);
