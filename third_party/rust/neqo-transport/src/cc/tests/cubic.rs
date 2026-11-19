// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss,
    reason = "OK in tests."
)]

use std::{
    ops::Sub,
    time::{Duration, Instant},
};

use neqo_common::to_u64;
use test_fixture::now;

use super::{RTT, make_cc_cubic, make_cc_cubic_with};
use crate::{
    cc::{
        ClassicSlowStart, CongestionController as _,
        classic_cc::ClassicCongestionController,
        cubic::{Cubic, convert_to_f64},
    },
    recovery::sent,
    rtt::RttEstimate,
    stats::CongestionControlStats,
};

const fn cwnd_after_loss(cwnd: usize) -> usize {
    cwnd * Cubic::BETA_USIZE_DIVIDEND / Cubic::BETA_USIZE_DIVISOR
}

const fn cwnd_after_loss_slow_start(cwnd: usize, mtu: usize) -> usize {
    (cwnd + mtu) * Cubic::BETA_USIZE_DIVIDEND / Cubic::BETA_USIZE_DIVISOR
}

/// Sets up a Cubic congestion controller in congestion avoidance phase.
///
/// If `fast_convergence` is true, sets `w_max` higher than cwnd to trigger fast convergence.
/// If false, sets `w_max` lower than cwnd to prevent fast convergence.
fn setup_congestion_avoidance(
    fast_convergence: bool,
) -> (
    ClassicCongestionController<ClassicSlowStart, Cubic>,
    CongestionControlStats,
) {
    setup_congestion_avoidance_with(Cubic::default(), fast_convergence)
}

/// Like [`setup_congestion_avoidance`], but with the given Cubic instance.
fn setup_congestion_avoidance_with(
    cubic: Cubic,
    fast_convergence: bool,
) -> (
    ClassicCongestionController<ClassicSlowStart, Cubic>,
    CongestionControlStats,
) {
    let mut cc = make_cc_cubic_with(cubic);
    let mut cc_stats = CongestionControlStats::default();
    // Enter congestion avoidance phase.
    cc.set_ssthresh(1);
    // Configure fast convergence behavior.
    let cwnd_f64 = convert_to_f64(cc.cwnd_initial());
    let w_max = if fast_convergence {
        cwnd_f64 * 10.0
    } else {
        convert_to_f64(cc.max_datagram_size()) * 3.0
    };
    cc.congestion_control_mut().set_w_max(w_max);
    // Fill cwnd and ack one packet to establish baseline.
    _ = fill_cwnd(&mut cc, 0, now());
    ack_packet(&mut cc, 0, now(), &mut cc_stats);
    (cc, cc_stats)
}

fn fill_cwnd(
    cc: &mut ClassicCongestionController<ClassicSlowStart, Cubic>,
    mut next_pn: u64,
    now: Instant,
) -> u64 {
    while cc.bytes_in_flight() < cc.cwnd() {
        let sent = sent::make_packet(next_pn, now, cc.max_datagram_size());
        cc.on_packet_sent(&sent, now);
        next_pn += 1;
    }
    next_pn
}

fn ack_packet(
    cc: &mut ClassicCongestionController<ClassicSlowStart, Cubic>,
    pn: u64,
    now: Instant,
    cc_stats: &mut CongestionControlStats,
) {
    let acked = sent::make_packet(pn, now, cc.max_datagram_size());
    cc.on_packets_acked(&[acked], &RttEstimate::new(RTT), now, cc_stats);
}

fn packet_lost(
    cc: &mut ClassicCongestionController<ClassicSlowStart, Cubic>,
    pn: u64,
    cc_stats: &mut CongestionControlStats,
) {
    const PTO: Duration = Duration::from_millis(120);
    let now = now();
    let p_lost = sent::make_packet(pn, now, cc.max_datagram_size());
    cc.on_packets_lost(None, None, PTO, &[p_lost], now, cc_stats);
}

fn ecn_ce(
    cc: &mut ClassicCongestionController<ClassicSlowStart, Cubic>,
    pn: u64,
    now: Instant,
    cc_stats: &mut CongestionControlStats,
) {
    let pkt = sent::make_packet(pn, now, cc.max_datagram_size());
    cc.on_ecn_ce_received(&pkt, now, cc_stats);
}

fn expected_tcp_acks(cwnd_rtt_start: usize, mtu: usize) -> u64 {
    (f64::from(i32::try_from(cwnd_rtt_start).unwrap())
        / f64::from(i32::try_from(mtu).unwrap())
        / Cubic::ALPHA)
        .round() as u64
}

#[test]
fn tcp_phase() {
    let mut cubic = make_cc_cubic();
    let mut cc_stats = CongestionControlStats::default();

    // change to congestion avoidance state.
    cubic.set_ssthresh(1);

    let mut now = now();
    let start_time = now;
    // helper variables to remember the next packet number to be sent/acked.
    let mut next_pn_send = 0;
    let mut next_pn_ack = 0;

    next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);

    // This will start with TCP phase.
    // in this phase cwnd is increase by CUBIC_ALPHA every RTT. We can look at it as
    // increase of MAX_DATAGRAM_SIZE every 1 / CUBIC_ALPHA RTTs.
    // The phase will end when cwnd calculated with cubic equation is equal to TCP estimate:
    // Cubic::C * (n * RTT / Cubic::ALPHA)^3 * MAX_DATAGRAM_SIZE = n * MAX_DATAGRAM_SIZE
    // from this n = sqrt(Cubic::ALPHA^3/ (Cubic::C * RTT^3)).
    let num_tcp_increases = (Cubic::ALPHA.powi(3) / (Cubic::C * RTT.as_secs_f64().powi(3)))
        .sqrt()
        .floor() as u64;

    for _ in 0..num_tcp_increases {
        let cwnd_rtt_start = cubic.cwnd();
        // Expected acks during a period of RTT / Cubic::ALPHA.
        let acks = expected_tcp_acks(cwnd_rtt_start, cubic.max_datagram_size());
        // The time between acks if they are ideally paced over a RTT.
        let time_increase =
            RTT / u32::try_from(cwnd_rtt_start / cubic.max_datagram_size()).unwrap();

        for _ in 0..acks {
            now += time_increase;
            ack_packet(&mut cubic, next_pn_ack, now, &mut cc_stats);
            next_pn_ack += 1;
            next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);
        }

        assert_eq!(cubic.cwnd() - cwnd_rtt_start, cubic.max_datagram_size());
    }

    // The next increase will be according to the cubic equation.

    let cwnd_rtt_start = cubic.cwnd();
    // cwnd_rtt_start has change, therefore calculate new time_increase (the time
    // between acks if they are ideally paced over a RTT).
    let time_increase = RTT / u32::try_from(cwnd_rtt_start / cubic.max_datagram_size()).unwrap();
    let mut num_acks = 0; // count the number of acks. until cwnd is increased by cubic.max_datagram_size().

    while cwnd_rtt_start == cubic.cwnd() {
        num_acks += 1;
        now += time_increase;
        ack_packet(&mut cubic, next_pn_ack, now, &mut cc_stats);
        next_pn_ack += 1;
        next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);
    }

    // Make sure that the increase is not according to TCP equation, i.e., that it took
    // less than RTT / Cubic::ALPHA.
    let expected_ack_tcp_increase = expected_tcp_acks(cwnd_rtt_start, cubic.max_datagram_size());
    assert!(num_acks < expected_ack_tcp_increase);

    // This first increase after a TCP phase may be shorter than what it would take by a regular
    // cubic phase, because of the proper byte counting and the credit it already had before
    // entering this phase. Therefore We will perform another round and compare it to expected
    // increase using the cubic equation.

    let cwnd_rtt_start_after_tcp = cubic.cwnd();
    let elapsed_time = now - start_time;

    // calculate new time_increase.
    let time_increase =
        RTT / u32::try_from(cwnd_rtt_start_after_tcp / cubic.max_datagram_size()).unwrap();
    let mut num_acks2 = 0; // count the number of acks. until cwnd is increased by MAX_DATAGRAM_SIZE.

    while cwnd_rtt_start_after_tcp == cubic.cwnd() {
        num_acks2 += 1;
        now += time_increase;
        ack_packet(&mut cubic, next_pn_ack, now, &mut cc_stats);
        next_pn_ack += 1;
        next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);
    }

    let expected_ack_tcp_increase2 =
        expected_tcp_acks(cwnd_rtt_start_after_tcp, cubic.max_datagram_size());
    assert!(num_acks2 < expected_ack_tcp_increase2);

    // The time needed to increase cwnd by MAX_DATAGRAM_SIZE using the cubic equation will be
    // calculated from: W_cubic(elapsed_time + t_to_increase) - W_cubic(elapsed_time) =
    // MAX_DATAGRAM_SIZE => Cubic::C * (elapsed_time + t_to_increase)^3 * MAX_DATAGRAM_SIZE +
    // CWND_INITIAL - Cubic::C * elapsed_time^3 * MAX_DATAGRAM_SIZE + CWND_INITIAL =
    // MAX_DATAGRAM_SIZE => t_to_increase = cbrt((1 + Cubic::C * elapsed_time^3) / Cubic::C) -
    // elapsed_time (t_to_increase is in seconds)
    // number of ack needed is t_to_increase / time_increase.
    let expected_ack_cubic_increase =
        (((Cubic::C.mul_add((elapsed_time).as_secs_f64().powi(3), 1.0) / Cubic::C).cbrt()
            - elapsed_time.as_secs_f64())
            / time_increase.as_secs_f64())
        .ceil() as u64;
    // num_acks is very close to the calculated value. The exact value is hard to calculate
    // because the proportional increase (i.e. curr_cwnd_f64 / (target - curr_cwnd_f64) *
    // MAX_DATAGRAM_SIZE_F64) and the byte counting.
    assert_eq!(num_acks2, expected_ack_cubic_increase + 2);
}

#[test]
fn cubic_phase() {
    let mut cubic = make_cc_cubic();
    let mut cc_stats = CongestionControlStats::default();
    let cwnd_initial_f64 = convert_to_f64(cubic.cwnd_initial());
    // Set w_max to a higher number make sure that cc is the cubic phase (cwnd is calculated
    // by the cubic equation).
    cubic
        .congestion_control_mut()
        .set_w_max(cwnd_initial_f64 * 10.0);
    // Set ssthresh to something small to make sure that cc is in the congection avoidance phase.
    cubic.set_ssthresh(1);
    let mut now = now();
    let mut next_pn_send = 0;
    let mut next_pn_ack = 0;

    next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);

    let k = (cwnd_initial_f64.mul_add(10.0, -cwnd_initial_f64)
        / Cubic::C
        / convert_to_f64(cubic.max_datagram_size()))
    .cbrt();
    let epoch_start = now;

    // The number of RTT until W_max is reached.
    let num_rtts_w_max = (k / RTT.as_secs_f64()).round() as u64;
    for _ in 0..num_rtts_w_max {
        let cwnd_rtt_start = cubic.cwnd();
        // Expected acks
        let acks = cwnd_rtt_start / cubic.max_datagram_size();
        let time_increase = RTT / u32::try_from(acks).unwrap();
        for _ in 0..acks {
            now += time_increase;
            ack_packet(&mut cubic, next_pn_ack, now, &mut cc_stats);
            next_pn_ack += 1;
            next_pn_send = fill_cwnd(&mut cubic, next_pn_send, now);
        }

        let expected = (Cubic::C * ((now - epoch_start).as_secs_f64() - k).powi(3))
            .mul_add(
                convert_to_f64(cubic.max_datagram_size()),
                cwnd_initial_f64 * 10.0,
            )
            .round() as usize;

        assert_within(cubic.cwnd(), expected, cubic.max_datagram_size());
    }
    assert_eq!(cubic.cwnd(), cubic.cwnd_initial() * 10);
}

fn assert_within<T: Sub<Output = T> + PartialOrd + Copy>(value: T, expected: T, margin: T) {
    if value >= expected {
        assert!(value - expected < margin);
    } else {
        assert!(expected - value < margin);
    }
}

#[test]
fn congestion_event_slow_start() {
    let mut cubic = make_cc_cubic();
    let mut cc_stats = CongestionControlStats::default();

    _ = fill_cwnd(&mut cubic, 0, now());
    ack_packet(&mut cubic, 0, now(), &mut cc_stats);

    assert_eq!(cubic.congestion_control().w_max(), None);

    // cwnd is increased by 1 in slow start phase, after an ack.
    assert_eq!(
        cubic.cwnd(),
        cubic.cwnd_initial() + cubic.max_datagram_size()
    );

    // Trigger a congestion_event in slow start phase
    packet_lost(&mut cubic, 1, &mut cc_stats);

    // w_max is equal to cwnd before decrease.
    let cwnd_initial_f64 = convert_to_f64(cubic.cwnd_initial());
    assert_within(
        cubic.congestion_control().w_max().unwrap(),
        cwnd_initial_f64 + convert_to_f64(cubic.max_datagram_size()),
        f64::EPSILON,
    );
    assert_eq!(
        cubic.cwnd(),
        cwnd_after_loss_slow_start(cubic.cwnd_initial(), cubic.max_datagram_size())
    );
    assert_eq!(cc_stats.congestion_events.loss, 1);
}

#[test]
fn congestion_event_congestion_avoidance() {
    let (mut cubic, mut cc_stats) = setup_congestion_avoidance(false);

    assert_eq!(cubic.cwnd(), cubic.cwnd_initial());

    // Trigger a congestion_event in congestion avoidance phase.
    packet_lost(&mut cubic, 1, &mut cc_stats);

    let cwnd_initial_f64 = convert_to_f64(cubic.cwnd_initial());
    assert_within(
        cubic.congestion_control().w_max().unwrap(),
        cwnd_initial_f64,
        f64::EPSILON,
    );
    assert_eq!(cubic.cwnd(), cwnd_after_loss(cubic.cwnd_initial()));
    assert_eq!(cc_stats.congestion_events.loss, 1);
}

/// Verify that `cwnd` and `acked_bytes` are correctly reduced on a congestion event.
fn cwnd_and_acked_bytes_reduced_on_congestion_event(
    cubic: Cubic,
    trigger: impl FnOnce(
        &mut ClassicCongestionController<ClassicSlowStart, Cubic>,
        Instant,
        &mut CongestionControlStats,
    ),
    beta: usize,
) {
    let (mut cubic, mut cc_stats) = setup_congestion_avoidance_with(cubic, false);

    // The helper acked packet 0. Ack one more to accumulate acked_bytes.
    let now = now() + RTT / 10;
    _ = fill_cwnd(&mut cubic, 10, now);
    ack_packet(&mut cubic, 1, now, &mut cc_stats);

    // Verify cwnd hasn't increased (so acked_bytes wasn't reset).
    assert_eq!(cubic.cwnd(), cubic.cwnd_initial());

    let acked_bytes_before = cubic.acked_bytes();
    assert!(acked_bytes_before > 0);

    // Trigger the congestion event.
    trigger(&mut cubic, now, &mut cc_stats);

    // Verify cwnd and acked_bytes were reduced by the correct factor.
    assert_eq!(
        cubic.cwnd(),
        cubic.cwnd_initial() * beta / Cubic::BETA_USIZE_DIVISOR
    );
    assert_eq!(
        cubic.acked_bytes(),
        acked_bytes_before * beta / Cubic::BETA_USIZE_DIVISOR
    );

    // Acking a packet sent before recovery started must not grow cwnd or start a new epoch.
    let cwnd_reduced = cubic.cwnd();
    ack_packet(&mut cubic, 3, now, &mut cc_stats);
    assert_eq!(cubic.cwnd(), cwnd_reduced);
    assert_eq!(cubic.congestion_control().t_epoch(), None);
}

#[test]
fn cwnd_and_acked_bytes_reduced_on_loss() {
    cwnd_and_acked_bytes_reduced_on_congestion_event(
        Cubic::default(),
        |cc, _, stats| packet_lost(cc, 2, stats),
        Cubic::BETA_USIZE_DIVIDEND,
    );
}

#[test]
fn cwnd_and_acked_bytes_reduced_on_ecn_ce() {
    cwnd_and_acked_bytes_reduced_on_congestion_event(
        Cubic::default(),
        |cc, now, stats| ecn_ce(cc, 2, now, stats),
        Cubic::BETA_USIZE_DIVIDEND_ECN,
    );
}

#[test]
fn cwnd_and_acked_bytes_reduced_on_ecn_ce_without_abe() {
    cwnd_and_acked_bytes_reduced_on_congestion_event(
        Cubic::default().abe(false),
        |cc, now, stats| ecn_ce(cc, 2, now, stats),
        Cubic::BETA_USIZE_DIVIDEND,
    );
}

#[test]
fn congestion_event_congestion_avoidance_fast_convergence() {
    let (mut cubic, mut cc_stats) = setup_congestion_avoidance(true);

    let cwnd_initial_f64 = convert_to_f64(cubic.cwnd_initial());
    assert_within(
        cubic.congestion_control().w_max().unwrap(),
        cwnd_initial_f64 * 10.0,
        f64::EPSILON,
    );
    assert_eq!(cubic.cwnd(), cubic.cwnd_initial());

    // Trigger a congestion_event.
    packet_lost(&mut cubic, 1, &mut cc_stats);

    assert_within(
        cubic.congestion_control().w_max().unwrap(),
        cwnd_initial_f64 * Cubic::FAST_CONVERGENCE_FACTOR,
        f64::EPSILON,
    );
    assert_eq!(cubic.cwnd(), cwnd_after_loss(cubic.cwnd_initial()));
    assert_eq!(cc_stats.congestion_events.loss, 1);
}

#[test]
fn congestion_event_congestion_avoidance_no_overflow() {
    const PTO: Duration = Duration::from_millis(120);
    let mut cubic = make_cc_cubic();
    let mut cc_stats = CongestionControlStats::default();

    // Set ssthresh to something small to make sure that cc is in the congection avoidance phase.
    cubic.set_ssthresh(1);

    // Set w_max to something higher than cwnd so that the fast convergence is triggered.
    let cwnd_initial_f64 = convert_to_f64(cubic.cwnd_initial());
    cubic
        .congestion_control_mut()
        .set_w_max(cwnd_initial_f64 * 10.0);

    _ = fill_cwnd(&mut cubic, 0, now());
    ack_packet(&mut cubic, 1, now(), &mut cc_stats);

    assert_within(
        cubic.congestion_control().w_max().unwrap(),
        cwnd_initial_f64 * 10.0,
        f64::EPSILON,
    );
    assert_eq!(cubic.cwnd(), cubic.cwnd_initial());

    // Now ack packet that was send earlier.
    ack_packet(
        &mut cubic,
        0,
        now().checked_sub(PTO).unwrap(),
        &mut cc_stats,
    );
}

/// Regression test for the Cubic issue discussed on the ccwg mailing list.
///
/// > TL;DR: Sections 4.4 and 4.5 of RFC 9438 do not account for segments_acked
/// > when increasing CWND outside the Reno-friendly region. As a result, a
/// > literal implementation makes CUBIC's growth rate depend on the receiver's
/// > ACK ratio, and causes measurable unfairness. It gets increasingly worse as
/// > ACKs become less frequent.
///
/// <https://mailarchive.ietf.org/arch/msg/ccwg/ZTYECT1NQijwwxq2sDLE0scbQAg/>
///
/// Our implementation is not affected today, as we correctly account for bytes
/// acked in [`ClassicCongestionController::on_packets_acked`].
#[test]
fn cwnd_growth_independent_of_ack_ratio() {
    /// Divisible by every ACK ratio, so all of them ack the same number of bytes.
    const PACKETS: usize = 40;

    /// `cwnd` growth in bytes after `PACKETS` are acked `ack_ratio` at a time.
    fn growth(ack_ratio: u32) -> usize {
        let mut cc = make_cc_cubic();
        let mut cc_stats = CongestionControlStats::default();
        let mtu = cc.max_datagram_size();
        let cwnd_initial_f64 = convert_to_f64(cc.cwnd_initial());
        // Low ssthresh to put us into congestion avoidance phase.
        cc.set_ssthresh(1);
        // Put `w_max` out of reach, so that `w_cubic(t + min_rtt)` always exceeds the
        // `cwnd * 1.5` upper bound of RFC 9438 section 4.2 and `target` is pinned to that
        // bound, which makes the expected growth below exact. This also keeps Cubic out
        // of the Reno-friendly region.
        cc.congestion_control_mut()
            .set_w_max(cwnd_initial_f64 * 1000.0);

        let mut now = now();
        let mut next_pn_send = fill_cwnd(&mut cc, 0, now);
        let cwnd_before = cc.cwnd();

        let packets_per_ack = u64::from(ack_ratio);
        for ack in 0..to_u64(PACKETS) / packets_per_ack {
            now += RTT * ack_ratio / 10;
            let first_pn = ack * packets_per_ack;
            assert!(
                first_pn + packets_per_ack <= next_pn_send,
                "can only ack packets that were sent"
            );
            // `on_packets_acked` expects the largest acked packet first.
            let acked = (first_pn..first_pn + packets_per_ack)
                .rev()
                .map(|pn| sent::make_packet(pn, now, mtu))
                .collect::<Vec<_>>();
            cc.on_packets_acked(&acked, &RttEstimate::new(RTT), now, &mut cc_stats);
            next_pn_send = fill_cwnd(&mut cc, next_pn_send, now);
        }

        cc.cwnd() - cwnd_before
    }

    let mtu = make_cc_cubic().max_datagram_size();
    for ack_ratio in [1, 2, 10] {
        // We operate at the `cwnd * 1.5` upper bound, so we should grow by `PACKETS / 2` segments.
        assert_eq!(
            growth(ack_ratio),
            PACKETS / 2 * mtu,
            "unexpected CWND growth for ACK ratio {ack_ratio}"
        );
    }
}

#[test]
fn cubic_display() {
    let cubic = Cubic::default();
    assert_eq!(
        cubic.to_string(),
        "Cubic state [w_max: None, k: 0, t_epoch: None]"
    );
}
