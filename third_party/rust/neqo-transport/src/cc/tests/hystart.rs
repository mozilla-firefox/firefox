// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! HyStart++ (RFC 9406) test suite

use std::time::Duration;

use neqo_common::to_u64;
use test_fixture::now;
use tracing::debug;

use super::make_cc_hystart;
use crate::{
    cc::{
        CongestionController as _, HyStartCssBaseline, classic_cc::SlowStart as _,
        hystart::HyStart, tests::INITIAL_CWND,
    },
    packet::MIN_INITIAL_PACKET_SIZE,
    recovery::sent,
    rtt::RttEstimate,
    stats::{CongestionControlStats, SlowStartExitReason},
};

const BASE_RTT: Duration = Duration::from_millis(100);
const HIGH_RTT: Duration = Duration::from_millis(120);
const LOW_RTT: Duration = Duration::from_millis(80);

/// Helper to create a HyStart instance with pacing enabled (L=infinity).
fn make_hystart_paced() -> HyStart {
    HyStart::new(true, HyStartCssBaseline::default())
}

/// Helper to create a HyStart instance with pacing disabled (L=8).
fn make_hystart_unpaced() -> HyStart {
    HyStart::new(false, HyStartCssBaseline::default())
}

/// Helper to set up HyStart state through two rounds with the given RTT values.
/// Can be used to test CSS entry (when `new_rtt` triggers it) or non-entry (when it doesn't).
fn maybe_enter_css(
    hystart: &mut HyStart,
    base_rtt: Duration,
    new_rtt: Duration,
    cc_stats: &mut CongestionControlStats,
) {
    // First round with base RTT
    let window_end = HyStart::N_RTT_SAMPLE;
    hystart.on_packet_sent(window_end, MIN_INITIAL_PACKET_SIZE);

    assert!(hystart.window_end().is_some_and(|pn| pn == window_end));

    // Collect N_RTT_SAMPLE samples with base RTT and end first round
    for i in 0..=window_end {
        hystart.on_packets_acked(
            &RttEstimate::new(base_rtt),
            i,
            INITIAL_CWND,
            cc_stats,
            now(),
        );
    }

    assert!(hystart.window_end().is_none());

    // Second round with new RTT value
    let window_end2 = 2 * HyStart::N_RTT_SAMPLE;
    hystart.on_packet_sent(window_end2, MIN_INITIAL_PACKET_SIZE);

    assert!(hystart.window_end().is_some_and(|pn| pn == window_end2));

    // Collect N_RTT_SAMPLE samples with new RTT and end second round
    for i in window_end + 1..=window_end2 {
        hystart.on_packets_acked(&RttEstimate::new(new_rtt), i, INITIAL_CWND, cc_stats, now());
    }

    assert!(hystart.window_end().is_none());
}

/// Tests that rounds are started and finished correctly and that `window_end` is set accordingly.
#[test]
fn round_tracking_lifecycle() {
    let mut hystart = make_hystart_paced();

    // Before any packet is sent, window_end should be None
    assert!(hystart.window_end().is_none());

    // Start a round with window_end = 10
    let window_end = 10;
    hystart.on_packet_sent(window_end, MIN_INITIAL_PACKET_SIZE);
    assert_eq!(
        hystart.window_end(),
        Some(window_end),
        "First send should start round with window_end"
    );

    // Send more packets - window_end should not change during the round
    hystart.on_packet_sent(11, MIN_INITIAL_PACKET_SIZE);
    hystart.on_packet_sent(12, MIN_INITIAL_PACKET_SIZE);
    assert_eq!(
        hystart.window_end(),
        Some(window_end),
        "window_end should not change during round"
    );

    // Ack packets less than window_end - round should continue
    for pn in 0..(window_end - 1) {
        hystart.on_packets_acked(
            &RttEstimate::new(BASE_RTT),
            pn, // All < window_end
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
        assert_eq!(
            hystart.window_end(),
            Some(window_end),
            "Round should continue while largest_acked < window_end"
        );
    }
    assert!(
        hystart.rtt_sample_count() > 0,
        "We should have collected RTT samples in this round."
    );
    let min_rtt = hystart.current_round_min_rtt().unwrap();

    // Now ack window_end - this should end the round and switch over RTT tracking to the next.
    hystart.on_packets_acked(
        &RttEstimate::new(BASE_RTT),
        window_end, // largest_acked=window_end, round ends
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );
    assert!(
        hystart.window_end().is_none(),
        "Round should end when largest_acked >= window_end"
    );
    assert_eq!(
        hystart.rtt_sample_count(),
        0,
        "RTT sample count should have been reset for the next round"
    );
    assert_eq!(
        hystart.last_round_min_rtt(),
        Some(min_rtt),
        "RTT tracking should have been updated for the next round"
    );

    // Set next window_end
    let window_end2 = 100;
    hystart.on_packet_sent(window_end2, MIN_INITIAL_PACKET_SIZE);
    assert_eq!(
        hystart.window_end(),
        Some(window_end2),
        "New round should now have set new window_end"
    );
}

/// Tests that `current_round_min_rtt` is tracked correctly when packets are acked.
#[test]
fn rtt_sample_collection_tracks_minimum() {
    let mut hystart = make_hystart_paced();

    // First ACK with RTT of 100ms
    hystart.on_packets_acked(
        &RttEstimate::new(BASE_RTT),
        0,
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );
    assert_eq!(hystart.rtt_sample_count(), 1);
    assert_eq!(
        hystart.current_round_min_rtt(),
        Some(BASE_RTT),
        "First sample should set the minimum"
    );

    // Second ACK with RTT of 80ms (lower) - should update minimum
    hystart.on_packets_acked(
        &RttEstimate::new(LOW_RTT),
        1,
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );
    assert_eq!(hystart.rtt_sample_count(), 2);
    assert_eq!(
        hystart.current_round_min_rtt(),
        Some(LOW_RTT),
        "Lower RTT should update the minimum"
    );

    // Third ACK with RTT of 120ms (higher) - should NOT update minimum
    hystart.on_packets_acked(
        &RttEstimate::new(HIGH_RTT),
        2,
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );
    assert_eq!(hystart.rtt_sample_count(), 3);
    assert_eq!(
        hystart.current_round_min_rtt(),
        Some(LOW_RTT),
        "Higher RTT should not update the minimum"
    );
}

#[test]
fn rtt_sample_count_increments_per_ack() {
    let mut hystart = make_hystart_paced();

    assert_eq!(hystart.rtt_sample_count(), 0);

    for i in 0..10 {
        hystart.on_packets_acked(
            &RttEstimate::new(BASE_RTT),
            i,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
        assert_eq!(hystart.rtt_sample_count(), i + 1);
    }
}

#[test]
fn css_entry_not_triggered_with_insufficient_samples() {
    let mut hystart = make_hystart_paced();

    // First round to set baseline RTT
    let window_end1 = HyStart::N_RTT_SAMPLE;
    hystart.on_packet_sent(window_end1, MIN_INITIAL_PACKET_SIZE);

    for i in 0..=window_end1 {
        hystart.on_packets_acked(
            &RttEstimate::new(BASE_RTT),
            i,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    // Second round with increased RTT but insufficient samples
    let window_end2 = window_end1 + HyStart::N_RTT_SAMPLE;
    hystart.on_packet_sent(window_end2, MIN_INITIAL_PACKET_SIZE);

    // Collect only N_RTT_SAMPLE - 1 samples, not enough to enter CSS with
    for i in (window_end1 + 1)..window_end2 {
        hystart.on_packets_acked(
            &RttEstimate::new(HIGH_RTT),
            i,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(
        !hystart.in_css(),
        "CSS should not be entered with insufficient samples"
    );
}

#[test]
fn css_entry_triggered_on_rtt_increase() {
    let mut hystart = make_hystart_paced();
    let mut cc_stats = CongestionControlStats::default();

    // Use helper to set up two rounds with RTT increase
    // rtt_thresh = max(4ms, min(100ms / 8, 16ms)) = max(4ms, min(12.5ms, 16ms)) = 12.5ms
    // Since 120ms >= 100ms + 12.5ms, CSS should be entered
    maybe_enter_css(&mut hystart, BASE_RTT, HIGH_RTT, &mut cc_stats);

    assert!(hystart.in_css(), "CSS should be entered on RTT increase");
    assert_eq!(cc_stats.hystart_css_entries, 1);
}

#[test]
fn css_entry_not_triggered_on_small_rtt_increase() {
    let mut hystart = make_hystart_paced();

    // Use helper with small RTT increase that's below threshold
    // rtt_thresh = 12.5ms, but increase is only 5ms
    maybe_enter_css(
        &mut hystart,
        BASE_RTT,
        Duration::from_millis(105),
        &mut CongestionControlStats::default(),
    );

    assert!(
        !hystart.in_css(),
        "CSS should not be entered on small RTT increase"
    );
}

#[test]
fn css_entry_triggered_on_min_rtt_thresh() {
    let mut hystart = make_hystart_paced();
    let mut cc_stats = CongestionControlStats::default();

    // Test MIN_RTT_THRESH bound: very small base RTT
    // rtt_thresh = max(MIN_RTT_THRESH, min(10ms / 8, MAX_RTT_THRESH))
    //            = max(4ms, 1.25ms) = 4ms
    // 15ms >= 10ms + 4ms, so CSS should be entered
    maybe_enter_css(
        &mut hystart,
        Duration::from_millis(10),
        Duration::from_millis(15),
        &mut cc_stats,
    );

    assert!(
        hystart.in_css(),
        "CSS should be entered with MIN_RTT_THRESH"
    );
    assert_eq!(cc_stats.hystart_css_entries, 1);
}

#[test]
fn css_entry_triggered_on_max_rtt_thresh() {
    let mut hystart = make_hystart_paced();
    let mut cc_stats = CongestionControlStats::default();

    // Test MAX_RTT_THRESH bound: large base RTT
    // rtt_thresh = max(MIN_RTT_THRESH, min(200ms / 8, MAX_RTT_THRESH))
    //            = max(4ms, min(25ms, 16ms)) = 16ms
    // 218ms >= 200ms + 16ms, so CSS should be entered
    maybe_enter_css(
        &mut hystart,
        Duration::from_millis(200),
        Duration::from_millis(218),
        &mut cc_stats,
    );

    assert!(
        hystart.in_css(),
        "CSS should be entered with MAX_RTT_THRESH"
    );
    assert_eq!(cc_stats.hystart_css_entries, 1);
}

#[test]
fn css_growth_rate_is_one_quarter() {
    const NEW_ACKED: usize = 4 * MIN_INITIAL_PACKET_SIZE;
    let mut hystart = make_hystart_paced();

    maybe_enter_css(
        &mut hystart,
        BASE_RTT,
        HIGH_RTT,
        &mut CongestionControlStats::default(),
    );
    assert!(hystart.in_css(), "Should have entered CSS");

    let cwnd_increase = hystart.calc_cwnd_increase(NEW_ACKED, MIN_INITIAL_PACKET_SIZE);

    // In CSS, growth is divided by CSS_GROWTH_DIVISOR
    assert_eq!(
        cwnd_increase,
        NEW_ACKED / HyStart::CSS_GROWTH_DIVISOR,
        "CSS growth should be 1/{} of new_acked",
        HyStart::CSS_GROWTH_DIVISOR
    );
}

#[test]
fn css_exit_after_n_rounds() {
    let mut hystart = make_hystart_paced();
    let mut cc_stats = CongestionControlStats::default();
    maybe_enter_css(&mut hystart, BASE_RTT, HIGH_RTT, &mut cc_stats);
    assert!(hystart.in_css(), "Should have entered CSS");
    assert_eq!(hystart.css_round_count(), 1);

    // Note: maybe_enter_css already completed a partial round in CSS (count = 1) so we start with
    // round 2.
    for round in 2..=HyStart::CSS_ROUNDS {
        // Start a new round
        let new_window_end = to_u64(round) * 100;
        hystart.on_packet_sent(new_window_end, MIN_INITIAL_PACKET_SIZE);

        // Collect samples
        for i in 0..HyStart::N_RTT_SAMPLE {
            hystart.on_packets_acked(
                &RttEstimate::new(HIGH_RTT),
                i,
                INITIAL_CWND,
                &mut cc_stats,
                now(),
            );
        }

        // End round by acking window_end
        let exit_slow_start = hystart.on_packets_acked(
            &RttEstimate::new(HIGH_RTT),
            new_window_end,
            INITIAL_CWND,
            &mut cc_stats,
            now(),
        );

        if round < HyStart::CSS_ROUNDS {
            assert!(
                exit_slow_start.is_none(),
                "Should not exit before {} rounds completed but exited after round {round}",
                HyStart::CSS_ROUNDS,
            );
            assert!(hystart.in_css(), "Should still be in CSS");
            assert_eq!(cc_stats.hystart_css_rounds_finished, round);
        } else {
            assert!(
                exit_slow_start.is_some(),
                "Should exit after {} rounds have completed",
                HyStart::CSS_ROUNDS
            );
            assert_eq!(cc_stats.hystart_css_rounds_finished, HyStart::CSS_ROUNDS);
        }
    }
}

#[test]
fn css_back_to_slow_start_on_rtt_decrease() {
    const CSS_BASELINE_RTT: Duration = HIGH_RTT;
    const LOWER_RTT: Duration = Duration::from_millis(110);
    let mut hystart = make_hystart_paced();
    let mut cc_stats = CongestionControlStats::default();
    maybe_enter_css(&mut hystart, BASE_RTT, CSS_BASELINE_RTT, &mut cc_stats);
    assert!(hystart.in_css(), "Should have entered CSS");
    assert_eq!(cc_stats.hystart_css_entries, 1);

    // Start a new round in CSS
    let new_window_end = 300;
    hystart.on_packet_sent(new_window_end, MIN_INITIAL_PACKET_SIZE);

    // RTT decreases below baseline - should exit CSS
    for i in 0..HyStart::N_RTT_SAMPLE {
        hystart.on_packets_acked(
            &RttEstimate::new(LOWER_RTT),
            i, // Less than window_end
            INITIAL_CWND,
            &mut cc_stats,
            now(),
        );
    }

    assert!(
        !hystart.in_css(),
        "Should exit CSS when RTT decreases below baseline"
    );
    assert_eq!(
        hystart.css_round_count(),
        0,
        "CSS round count should be reset"
    );
}

#[test]
fn css_exit_only_with_new_samples() {
    let mut hystart = make_hystart_paced();

    // First round with base RTT to set a baseline that we can compare RTT against
    let window_end = HyStart::N_RTT_SAMPLE;
    hystart.on_packet_sent(window_end, MIN_INITIAL_PACKET_SIZE);

    assert!(hystart.window_end().is_some_and(|pn| pn == window_end));

    // Collect N_RTT_SAMPLE samples with base RTT and end first round
    for i in 0..=window_end {
        hystart.on_packets_acked(
            &RttEstimate::new(BASE_RTT),
            i,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(hystart.window_end().is_none());

    // Start second round with a high window end
    let window_end2 = 300;
    hystart.on_packet_sent(window_end2, MIN_INITIAL_PACKET_SIZE);

    // Collect N_RTT_SAMPLE samples with higher RTT to enter CSS
    for _i in 0..HyStart::N_RTT_SAMPLE {
        hystart.on_packets_acked(
            &RttEstimate::new(HIGH_RTT),
            0,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(hystart.in_css(), "Should have entered CSS");

    // ACK with low RTT should not exit CSS without new samples
    hystart.on_packets_acked(
        &RttEstimate::new(LOW_RTT),
        0,
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );

    assert!(
        hystart.in_css(),
        "Should still be in CSS after one low RTT ACK"
    );

    // Collect N_RTT_SAMPLE-1 more samples with low RTT
    for _i in 1..HyStart::N_RTT_SAMPLE {
        hystart.on_packets_acked(
            &RttEstimate::new(LOW_RTT),
            0,
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(
        !hystart.in_css(),
        "Should exit CSS after having enough samples"
    );
}

#[test]
fn css_alternative_baseline() {
    const CSS_ENTRY_RTT: Duration = HIGH_RTT;
    let mut hystart = HyStart::new(true, HyStartCssBaseline::EntryThreshold);
    maybe_enter_css(
        &mut hystart,
        BASE_RTT,
        CSS_ENTRY_RTT,
        &mut CongestionControlStats::default(),
    );
    assert!(hystart.in_css(), "Should have entered CSS");

    // Start a new round in CSS
    let new_window_end = 300;
    hystart.on_packet_sent(new_window_end, MIN_INITIAL_PACKET_SIZE);

    // RTT decreases just below CSS_ENTRY_RTT - should not exit CSS if using alternative baseline.
    // When using the RFC default then this would've exited.
    for i in 0..HyStart::N_RTT_SAMPLE {
        hystart.on_packets_acked(
            &RttEstimate::new(CSS_ENTRY_RTT.checked_sub(Duration::from_micros(1)).unwrap()),
            i, // Less than window_end
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(
        hystart.in_css(),
        "Should still be in CSS after RTT decrease just below CSS_ENTRY_RTT"
    );

    // RTT decreases below the threshold that we entered CSS with - should exit CSS.
    // With BASE_RTT = 100ms we have a threshold of 112.5ms, so let's take a value of 112.499ms.
    let below_threshold_rtt = BASE_RTT + Duration::from_micros(12_499);
    hystart.on_packets_acked(
        &RttEstimate::new(below_threshold_rtt),
        0, // Less than window_end
        INITIAL_CWND,
        &mut CongestionControlStats::default(),
        now(),
    );

    assert!(
        !hystart.in_css(),
        "Should exit CSS when RTT decreases below alternative baseline"
    );
}

#[test]
fn css_exit_to_slow_start_restores_normal_growth() {
    const CSS_BASELINE_RTT: Duration = HIGH_RTT;
    const LOWER_RTT: Duration = Duration::from_millis(110);
    const NEW_ACKED: usize = 4 * MIN_INITIAL_PACKET_SIZE;
    let mut hystart = make_hystart_paced();
    maybe_enter_css(
        &mut hystart,
        BASE_RTT,
        CSS_BASELINE_RTT,
        &mut CongestionControlStats::default(),
    );
    assert!(hystart.in_css(), "Should have entered CSS");

    // Test CSS growth (1/CSS_GROWTH_DIVISOR rate)
    let cwnd_increase = hystart.calc_cwnd_increase(NEW_ACKED, MIN_INITIAL_PACKET_SIZE);
    assert_eq!(
        cwnd_increase,
        NEW_ACKED / HyStart::CSS_GROWTH_DIVISOR,
        "CSS growth should be 1/{}",
        HyStart::CSS_GROWTH_DIVISOR
    );

    // Start new round with lower RTT
    let new_window_end = 400;
    hystart.on_packet_sent(new_window_end, MIN_INITIAL_PACKET_SIZE);
    for i in 0..HyStart::N_RTT_SAMPLE {
        hystart.on_packets_acked(
            &RttEstimate::new(LOWER_RTT),
            i, // Less than window_end
            INITIAL_CWND,
            &mut CongestionControlStats::default(),
            now(),
        );
    }

    assert!(!hystart.in_css(), "Should have exited CSS");

    // Test normal slow start growth (1:1 rate)
    let cwnd_increase = hystart.calc_cwnd_increase(NEW_ACKED, MIN_INITIAL_PACKET_SIZE);
    assert_eq!(cwnd_increase, NEW_ACKED, "Normal SS growth should be 1:1");
}

#[test]
fn l_limit_paced_no_cap() {
    let mut hystart = make_hystart_paced(); // L = infinity
    hystart.on_packet_sent(0, MIN_INITIAL_PACKET_SIZE);

    // Try to increase by more than NON_PACED_L * SMSS
    let cwnd_increase =
        hystart.calc_cwnd_increase(100 * MIN_INITIAL_PACKET_SIZE, MIN_INITIAL_PACKET_SIZE);

    assert_eq!(
        cwnd_increase,
        100 * MIN_INITIAL_PACKET_SIZE,
        "Paced should have no cap"
    );
}

#[test]
fn l_limit_unpaced_is_capped() {
    let mut hystart = make_hystart_unpaced(); // L = NON_PACED_L
    hystart.on_packet_sent(0, MIN_INITIAL_PACKET_SIZE);

    // Try to increase by more than NON_PACED_L * SMSS
    let cwnd_increase =
        hystart.calc_cwnd_increase(100 * MIN_INITIAL_PACKET_SIZE, MIN_INITIAL_PACKET_SIZE);

    assert_eq!(
        cwnd_increase,
        HyStart::NON_PACED_L * MIN_INITIAL_PACKET_SIZE,
        "Unpaced should cap at L * SMSS"
    );
}

/// Integration test that is run through a `ClassicCongestionControl` instance and moves through the
/// full slowstart -> CSS -> congestion avoidance lifetime while continuously ACK'ing and sending
/// packets.
#[test]
fn integration_full_slow_start_to_css_to_ca() {
    let mut cc = make_cc_hystart(true);
    let mut stats = CongestionControlStats::default();
    let mut now = now();

    let base_rtt = BASE_RTT;
    let increased_rtt = HIGH_RTT;
    let base_rtt_est = RttEstimate::new(base_rtt);
    let increased_rtt_est = RttEstimate::new(increased_rtt);

    assert_eq!(cc.ssthresh(), None, "Should start in slow start");

    let mut next_send: u64 = 0;
    let mut next_ack: u64 = 0;
    let mut css_detected = false;
    let mut ca_detected = false;

    // Send initial cwnd worth of packets.
    let initial_cwnd_packets = cc.cwnd() / MIN_INITIAL_PACKET_SIZE;
    for _ in 0..initial_cwnd_packets {
        let pkt = sent::make_packet(next_send, now, MIN_INITIAL_PACKET_SIZE);
        cc.on_packet_sent(&pkt, now);
        next_send += 1;
    }

    // Wait 1 `base_rtt` for first ACK to arrive.
    now += base_rtt;

    // Continuous send/ACK alternation:
    // ACK'ing packet 0 ends round 1, next send starts round 2 and so on.
    let max_iterations = 1000; // Enough for multiple CSS rounds and CA entry
    for iteration in 0..max_iterations {
        // ACK the next packet
        let ack_pn = next_ack;

        // Have `base_rtt` for the first cwnd that was sent before the loop. Have `increased_rtt`
        // for all subsequent packets to trigger and go through CSS.
        let rtt_to_use = if ack_pn < to_u64(initial_cwnd_packets) {
            base_rtt
        } else {
            increased_rtt
        };
        let rtt_est = if ack_pn < to_u64(initial_cwnd_packets) {
            &base_rtt_est
        } else {
            &increased_rtt_est
        };

        let pkt = sent::make_packet(
            ack_pn,
            now.checked_sub(rtt_to_use).unwrap(),
            MIN_INITIAL_PACKET_SIZE,
        );
        let cwnd_before = cc.cwnd();
        let ssthresh_before = cc.ssthresh();
        cc.on_packets_acked(&[pkt], rtt_est, now, &mut stats);
        let cwnd_after = cc.cwnd();
        let ssthresh_after = cc.ssthresh();
        let growth = cwnd_after - cwnd_before;
        next_ack += 1;

        // Detect CSS: growth becomes 1/4
        if growth > 0
            && growth == MIN_INITIAL_PACKET_SIZE / HyStart::CSS_GROWTH_DIVISOR
            && !css_detected
        {
            css_detected = true;
            debug!("CSS entered at ack_pn={ack_pn}, iteration={iteration}");
        }

        // Detect CA: ssthresh has been set
        if ssthresh_before.is_none() && ssthresh_after.is_some() {
            ca_detected = true;
            debug!("CA entered at ack_pn={ack_pn}, iteration={iteration}");
            // This assert makes sure that the ACK that we decided to move to CA on does not apply
            // exponential growth from slow start/CSS anymore.
            assert!(
                growth < MIN_INITIAL_PACKET_SIZE / HyStart::CSS_GROWTH_DIVISOR,
                "We should be using CA growth once we detected exit to CA."
            );
            assert_eq!(
                stats.slow_start_exit.map(|e| e.reason),
                Some(SlowStartExitReason::Heuristic)
            );
            assert_eq!(stats.hystart_css_rounds_finished, HyStart::CSS_ROUNDS);
            break;
        }

        // As the cwnd grows during the loop above we cannot keep alternating ACK'ing and sending
        // just one packet. This sends more packets until the cwnd is full so we don't become
        // app-limited.
        while cc.bytes_in_flight() < cc.cwnd() {
            let send_pn = next_send;
            let pkt = sent::make_packet(send_pn, now, MIN_INITIAL_PACKET_SIZE);
            cc.on_packet_sent(&pkt, now);
            next_send += 1;
        }

        // Advance time by a small increment to simulate continuous operation.
        now += increased_rtt / 10;
    }

    assert!(css_detected, "Should have entered CSS");
    assert!(ca_detected, "Should have entered CA after CSS rounds");
    assert_eq!(
        cc.ssthresh(),
        Some(cc.cwnd()),
        "ssthresh should be set in CA"
    );
}
