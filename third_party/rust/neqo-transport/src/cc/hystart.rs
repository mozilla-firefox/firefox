// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cmp::{max, min},
    fmt::Display,
    time::{Duration, Instant},
};

use tracing::{debug, trace};

use crate::{cc::classic_cc::SlowStart, packet, rtt::RttEstimate, stats::CongestionControlStats};

/// Controls how the CSS baseline RTT is set when entering Conservative Slow Start (CSS).
///
/// The baseline determines when CSS is considered spurious: if the RTT drops below it,
/// CSS is exited and normal slow start resumes.
#[derive(Debug, Copy, Clone, Default, PartialEq, Eq)]
pub enum HyStartCssBaseline {
    /// RFC 9406 behavior: baseline = `currentRoundMinRTT` at CSS entry.
    ///
    /// Can cause oscillation: values slightly below the entry RTT trigger CSS exit even
    /// though they would still have triggered CSS entry (and might do so on the next ACK).
    #[default]
    CurrentRoundMinRtt,
    /// Alternative behavior: baseline = `lastRoundMinRTT + rttThresh`.
    ///
    /// CSS is only exited when the RTT drops below the level that would have triggered
    /// CSS entry, avoiding oscillation between CSS entry and exit.
    EntryThreshold,
}

#[derive(Debug)]
pub struct HyStart {
    /// > While an arriving ACK may newly acknowledge an arbitrary number of bytes, the HyStart++
    /// > algorithm limits the number of those bytes applied to increase the cwnd to `L*SMSS`
    /// > bytes.
    ///
    ///  <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-1>
    ///
    /// > A paced TCP implementation SHOULD use `L = infinity`. Burst concerns are mitigated by
    /// > pacing, and this setting allows for optimal cwnd growth on modern networks.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9406#section-4.3-9>
    limit: usize,
    css_baseline_mode: HyStartCssBaseline,
    last_round_min_rtt: Option<Duration>,
    current_round_min_rtt: Option<Duration>,
    rtt_sample_count: u64,
    window_end: Option<packet::Number>,
    css_baseline_min_rtt: Option<Duration>,
    css_round_count: usize,
}

impl Display for HyStart {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "HyStart++")
    }
}

impl HyStart {
    pub const MIN_RTT_THRESH: Duration = Duration::from_millis(4);

    pub const MAX_RTT_THRESH: Duration = Duration::from_millis(16);

    pub const MIN_RTT_DIVISOR: u32 = 8;

    pub const N_RTT_SAMPLE: u64 = 8;

    pub const CSS_GROWTH_DIVISOR: usize = 4;

    pub const CSS_ROUNDS: usize = 5;

    pub const NON_PACED_L: usize = 8;

    pub const fn new(pacing: bool, css_baseline_mode: HyStartCssBaseline) -> Self {
        let limit = if pacing {
            usize::MAX
        } else {
            Self::NON_PACED_L
        };
        Self {
            limit,
            css_baseline_mode,
            last_round_min_rtt: None,
            current_round_min_rtt: None,
            rtt_sample_count: 0,
            window_end: None,
            css_baseline_min_rtt: None,
            css_round_count: 0,
        }
    }

    /// > For each arriving ACK in slow start \[...\] keep track of the minimum observed RTT:
    /// >
    /// > ```pseudo
    /// > currentRoundMinRTT = min(currentRoundMinRTT, currRTT)
    /// > rttSampleCount += 1
    /// > ```
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-11>
    fn collect_rtt_sample(&mut self, rtt: Duration) {
        self.current_round_min_rtt = Some(
            self.current_round_min_rtt
                .map_or(rtt, |current| min(current, rtt)),
        );
        self.rtt_sample_count += 1;
    }

    /// > HyStart++ measures rounds using sequence numbers, as follows:
    /// >
    /// > - Define windowEnd as a sequence number initialized to SND.NXT.
    /// > - When windowEnd is ACKed, the current round ends and windowEnd is set to SND.NXT.
    /// >
    /// > At the start of each round during standard slow start and CSS, initialize the
    /// > variables used to compute the last round's and current round's minimum RTT:
    /// >
    /// > ```pseudo
    /// > lastRoundMinRTT = currentRoundMinRTT
    /// > currentRoundMinRTT = infinity
    /// > rttSampleCount = 0
    /// > ```
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-4>
    ///
    /// Called immediately when a round ends so that ACKs arriving before the next packet is sent
    /// are attributed to the new round rather than being in limbo.
    fn start_next_round(&mut self) {
        self.window_end = None;
        self.last_round_min_rtt = self.current_round_min_rtt;
        self.current_round_min_rtt = None;
        self.rtt_sample_count = 0;
        debug!(
            "HyStart: start_next_round -> started new round with last_min_rtt: {:?}",
            self.last_round_min_rtt
        );
    }

    /// Sets `window_end` to `sent_pn` to mark when the current round will end.
    ///
    /// Called from [`SlowStart::on_packet_sent`]. Only sets `window_end` if it is `None`,
    /// i.e., the previous round ended and the end marker for the current round hasn't been set yet.
    fn maybe_set_window_end(&mut self, sent_pn: packet::Number) {
        if self.window_end.is_some() {
            return;
        }
        self.window_end = Some(sent_pn);
        debug!(
            "HyStart: maybe_set_window_end -> set window_end to {:?}",
            self.window_end
        );
    }

    /// Checks if HyStart is in Conservative Slow Start. Is `pub` for use in tests.
    pub const fn in_css(&self) -> bool {
        self.css_baseline_min_rtt.is_some()
    }

    const fn enough_samples(&self) -> bool {
        self.rtt_sample_count >= Self::N_RTT_SAMPLE
    }

    #[cfg(test)]
    pub const fn window_end(&self) -> Option<packet::Number> {
        self.window_end
    }

    #[cfg(test)]
    pub const fn rtt_sample_count(&self) -> u64 {
        self.rtt_sample_count
    }

    #[cfg(test)]
    pub const fn current_round_min_rtt(&self) -> Option<Duration> {
        self.current_round_min_rtt
    }

    #[cfg(test)]
    pub const fn last_round_min_rtt(&self) -> Option<Duration> {
        self.last_round_min_rtt
    }

    #[cfg(test)]
    pub const fn css_round_count(&self) -> usize {
        self.css_round_count
    }
}

impl SlowStart for HyStart {
    fn on_packet_sent(&mut self, sent_pn: packet::Number, _sent_bytes: usize) {
        self.maybe_set_window_end(sent_pn);
    }

    fn reset(&mut self) {
        self.last_round_min_rtt = None;
        self.current_round_min_rtt = None;
        self.rtt_sample_count = 0;
        self.window_end = None;
        self.css_baseline_min_rtt = None;
        self.css_round_count = 0;
    }

    // The HyStart++ RFC recommends only running HyStart++ in initial slow start.
    //
    // > An implementation SHOULD use HyStart++ only for the initial slow start (when the ssthresh
    // > is at its initial value of arbitrarily high per [RFC5681]) and fall back to using standard
    // > slow start for the remainder of the connection lifetime. This is acceptable because
    // > subsequent slow starts will use the discovered ssthresh value to exit slow start and avoid
    // > the overshoot problem.
    //
    // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.3-11>
    //
    // We ignore this SHOULD and run HyStart++ every slow start if it is enabled. That is for the
    // following reasons:
    // - reduces code complexity in [`ClassicCongestionController::on_packets_acked`] because there
    //   is one less state to distinguish
    // - the RFC is only stating this as a SHOULD, so we are not in direct conflict with it
    // - in QUIC we only have non-initial slow start after persistent congestion, so it is quite
    //   rare, as opposed to TCP where it also happens after an RTO timeout (see RFC 5681 3.1)
    // - after persistent congestion the established `ssthresh` might be still too high, so we could
    //   still overshoot and induce loss before reaching it, thus no harm in still using HyStart++
    //   to exit based on RTT if necessary
    // - if we do reach the previously established `ssthresh` we still exit slow start and continue
    //   with congestion avoidance
    fn on_packets_acked(
        &mut self,
        rtt_est: &RttEstimate,
        largest_acked: packet::Number,
        curr_cwnd: usize,
        cc_stats: &mut CongestionControlStats,
        _now: Instant,
    ) -> Option<usize> {
        self.collect_rtt_sample(rtt_est.latest_rtt());

        trace!(
            "HyStart: on_packets_acked -> pn={largest_acked}, rtt={:?}, cur_min={:?}, last_min={:?}, samples={}, in_css={}, css_rounds={}, window_end={:?}",
            rtt_est.latest_rtt(),
            self.current_round_min_rtt,
            self.last_round_min_rtt,
            self.rtt_sample_count,
            self.in_css(),
            self.css_round_count,
            self.window_end
        );

        // > For rounds where at least N_RTT_SAMPLE RTT samples have been obtained and
        // > currentRoundMinRTT and lastRoundMinRTT are valid, check to see if delay increase
        // > triggers slow start exit.
        //
        // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-13>
        if !self.in_css()
            && self.enough_samples()
            && let Some(current) = self.current_round_min_rtt
            && let Some(last) = self.last_round_min_rtt
        {
            let rtt_thresh = max(
                Self::MIN_RTT_THRESH,
                min(last / Self::MIN_RTT_DIVISOR, Self::MAX_RTT_THRESH),
            );
            if current >= last + rtt_thresh {
                self.rtt_sample_count = 0;
                self.css_baseline_min_rtt = Some(match self.css_baseline_mode {
                    HyStartCssBaseline::CurrentRoundMinRtt => current,
                    HyStartCssBaseline::EntryThreshold => last + rtt_thresh,
                });
                cc_stats.hystart_css_entries += 1;
                debug!(
                    "HyStart: on_packets_acked -> entered CSS because cur_min={current:?} >= last_min={last:?} + thresh={rtt_thresh:?}"
                );
            }
        // > For CSS rounds where at least N_RTT_SAMPLE RTT samples have been obtained, check to see
        // > if the current round's minRTT drops below baseline (cssBaselineMinRtt) indicating that
        // > slow start exit was spurious:
        // >
        // > ```
        // > if (currentRoundMinRTT < cssBaselineMinRtt)
        // > cssBaselineMinRtt = infinity
        // > resume slow start including HyStart++
        // > ```
        //
        // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-20>
        } else if self.enough_samples()
            && let Some(current) = self.current_round_min_rtt
            && let Some(baseline) = self.css_baseline_min_rtt
            && current < baseline
        {
            debug!(
                "HyStart: on_packets_acked -> exiting CSS after {} rounds because cur_min={:?} < baseline_min={:?}",
                self.css_round_count, self.current_round_min_rtt, self.css_baseline_min_rtt
            );

            self.css_baseline_min_rtt = None;
            self.css_round_count = 0;
        }

        // Check for end of round. If `window_end` is acked, call `start_next_round` to immediately
        // begin the next round's RTT tracking. [`SlowStart::on_packet_sent`] will then call
        // `maybe_set_window_end` to mark when the new round will end.
        if self
            .window_end
            .is_none_or(|window_end| largest_acked < window_end)
        {
            return None;
        }

        trace!(
            "HyStart: on_packets_acked -> round ended because largest_acked={largest_acked} >= window_end={:?}",
            self.window_end
        );
        self.start_next_round();

        if !self.in_css() {
            return None;
        }

        // If a round ends while in CSS increase the counter and do a check if enough rounds
        // to exit to congestion avoidance have been completed.
        self.css_round_count += 1;
        cc_stats.hystart_css_rounds_finished += 1;
        let exit_slow_start = self.css_round_count >= Self::CSS_ROUNDS;
        debug!(
            "HyStart: on_packets_acked -> exit={exit_slow_start} because css_rounds={} >= {}",
            self.css_round_count,
            Self::CSS_ROUNDS
        );
        if !exit_slow_start {
            return None;
        }
        // > If CSS_ROUNDS rounds are complete, enter congestion avoidance by setting the ssthresh
        // > to
        // > the current cwnd.
        //
        // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-23>
        Some(curr_cwnd)
    }

    fn calc_cwnd_increase(&self, new_acked: usize, max_datagram_size: usize) -> usize {
        // > For each arriving ACK in slow start, where N is the number of previously unacknowledged
        // > bytes acknowledged in the arriving ACK:
        // >
        // > Update the cwnd:
        // >
        // > `cwnd = cwnd + min(N, L*SMSS)`
        //
        // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-8>
        let mut cwnd_increase = min(self.limit.saturating_mul(max_datagram_size), new_acked);

        // > For each arriving ACK in CSS, where N is the number of previously unacknowledged
        // > bytes acknowledged in the arriving ACK:
        // >
        // > Update the cwnd:
        // >
        // > `cwnd = cwnd + (min(N, L*SMSS) / CSS_GROWTH_DIVISOR)`
        //
        // <https://datatracker.ietf.org/doc/html/rfc9406#section-4.2-15>
        if self.in_css() {
            cwnd_increase /= Self::CSS_GROWTH_DIVISOR;
        }
        cwnd_increase
    }
}
