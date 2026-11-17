// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
//
//! Slow start Exit At Right CHokepoint (SEARCH) implementation as per
//! <https://datatracker.ietf.org/doc/html/draft-chung-ccwg-search-09>

use std::{
    fmt::Display,
    time::{Duration, Instant},
};

use neqo_common::to_u64;
use tracing::debug;

use crate::{cc::classic_cc::SlowStart, packet, rtt::RttEstimate, stats::CongestionControlStats};

/// The outcome of a single SEARCH evaluation.
#[derive(Debug, PartialEq, Eq)]
pub enum Outcome {
    /// Evaluation ran and slow start should be exited with the provided cwnd.
    Exit(usize),
    /// Evaluation ran and slow start should be continued. Provides the normalized difference
    /// between sent and acked bytes, which can be used in metrics to tune the exit threshold.
    Continue(usize),
    /// Not enough data to run SEARCH evaluation (expected early in the connection or after reset).
    WarmingUp,
    /// Can't run SEARCH evaluation because RTT inflated past the point where there isn't enough
    /// data to look back one RTT. Provides the number of bins that SEARCH tried to look back.
    RttInflated(usize),
    /// Haven't sent data for the last RTT thus can't evaluate.
    ZeroSent,
}

/// Slow start Exit At Right CHokepoint (SEARCH).
///
/// Exits slow start when the delivery rate flattens, indicating the network is
/// near its bottleneck capacity.
///
/// <https://datatracker.ietf.org/doc/html/draft-chung-ccwg-search-09>
#[derive(Debug)]
pub struct Search {
    /// The circular array used to track acked bytes per bin.
    acked_bins: [u64; Self::NUM_ACKED_BINS],
    /// The circular array used to track sent bytes per bin.
    sent_bins: [u64; Self::NUM_SENT_BINS],
    /// The current index of the circular array. `None` if uninitialized.
    curr_idx: Option<usize>,
    /// Time at which the current bin will be passed and the next should start.
    bin_end: Option<Instant>,
    /// The duration of each bin.
    bin_duration: Duration,
    /// Tracking amount of acked bytes on this connection. Is incremented on every ACK.
    acked_bytes: u64,
    /// Tracking amount of sent bytes on this connection. Is incremented on every sent packet.
    sent_bytes: u64,
    /// The RTT used to initialize SEARCH (set in [`Self::initialize`]).
    initial_rtt: Option<Duration>,
}

impl Display for Search {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "SEARCH")
    }
}

impl Search {
    /// Factor for calculating the window size with the initial RTT, as an integer
    /// out of [`Self::SCALE`] (= 3.50).
    const WINDOW_SIZE_FACTOR: u32 = 350;
    /// Number of bins per window.
    const W: usize = 10;
    /// Additional bins needed to allow lookback by the current RTT for getting previously sent
    /// bytes.
    ///
    /// A higher value allows for a bigger difference between `curr_idx` and `prev_idx`, which
    /// allows for bigger RTT inflation before SEARCH stops working.
    const EXTRA_BINS: usize = 15;
    /// Total number of bins in the circular buffer for acked bytes. Needs an extra index so the
    /// buffer can accommodate the whole range of `[i - W, i]`.
    const NUM_ACKED_BINS: usize = Self::W + 1;
    /// Total number of bins in the circular buffer for sent bytes.
    const NUM_SENT_BINS: usize = Self::NUM_ACKED_BINS + Self::EXTRA_BINS;
    /// The upper bound for the permissible normalized difference between previously sent bytes and
    /// current delivered bytes, as an integer out of [`Self::SCALE`] (= 0.26).
    const THRESH: usize = 26;
    /// Scale factor for integer approximation of fractional values.
    const SCALE: u32 = 100;

    /// Creates a new SEARCH slow start instance.
    pub const fn new() -> Self {
        Self {
            acked_bins: [0; Self::NUM_ACKED_BINS],
            sent_bins: [0; Self::NUM_SENT_BINS],
            curr_idx: None,
            bin_end: None,
            bin_duration: Duration::from_millis(0),
            acked_bytes: 0,
            sent_bytes: 0,
            initial_rtt: None,
        }
    }

    /// Initializes SEARCH state on the first ACK using the measured RTT.
    #[expect(
        clippy::cast_possible_truncation,
        reason = "casting small constant usize to u32 for use in Duration::div"
    )]
    fn initialize(&mut self, initial_rtt: Duration, now: Instant) {
        self.initial_rtt = Some(initial_rtt);
        // BIN_DURATION = WINDOW_SIZE / W = initial_rtt * WINDOW_SIZE_FACTOR / W
        self.bin_duration = initial_rtt * Self::WINDOW_SIZE_FACTOR / Self::SCALE / Self::W as u32;
        if self.bin_duration.is_zero() {
            debug!(
                "skipping initialization because bin_duration.is_zero() but bin_duration must be non-zero - initial_rtt: {initial_rtt:?}",
            );
            debug_assert!(
                false,
                "bin_duration must be non-zero for correctness and to guard against div by zero -- initial_rtt was zero or too small"
            );
            return;
        }
        self.bin_end = Some(now + self.bin_duration);
        self.acked_bins[0] = self.acked_bytes;
        self.sent_bins[0] = self.sent_bytes;
        self.curr_idx = Some(0);
    }

    /// Advances bin state when a bin boundary has been crossed.
    ///
    /// Returns the new bin index, or `None` if bins couldn't be updated.
    #[expect(
        clippy::cast_possible_truncation,
        reason = "casting small usize to u32 for use in Duration::saturating_mul"
    )]
    fn update_bins(
        &mut self,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> Option<usize> {
        let mut curr_idx = self.curr_idx?;
        let mut bin_end = self.bin_end?;

        // passed_bins = (now - bin_end) / bin_duration + 1 -- integer division floors implicitly
        let passed_bins = usize::try_from(
            now.saturating_duration_since(bin_end).as_nanos() / self.bin_duration.as_nanos() + 1,
        )
        .unwrap_or(usize::MAX);

        // Reset if more than a full window of bins was skipped (e.g. after being app-limited or
        // flow-control-limited). The bin data is too stale for meaningful SEARCH detection.
        //
        // NOTE: SEARCH draft-09 doesn't implement a reset mechanism for stale data anymore but
        // makes it optional instead. I think it makes sense, especially because it can also happen
        // if the sender is app-limited for a longer period of time, in which case both the data in
        // the bins, as well as the initial RTT value might not be representative anymore due to
        // path changes.
        //
        // <https://datatracker.ietf.org/doc/html/draft-chung-ccwg-search-09#name-handling-missed-bins-option>
        if passed_bins > Self::W {
            debug!(
                "SEARCH: update_bins: resetting because we skipped {passed_bins} bins (limit {})",
                Self::W
            );
            cc_stats.search_reset.count += 1;
            cc_stats.search_reset.max_passed_bins =
                cc_stats.search_reset.max_passed_bins.max(Some(passed_bins));
            self.reset();
            return None;
        }

        // For skipped bins propagate the previous bin value (usually `passed_bins` is just `1`, so
        // this doesn't run)
        for i in curr_idx + 1..curr_idx + passed_bins {
            self.acked_bins[i % Self::NUM_ACKED_BINS] =
                self.acked_bins[curr_idx % Self::NUM_ACKED_BINS];
            self.sent_bins[i % Self::NUM_SENT_BINS] =
                self.sent_bins[curr_idx % Self::NUM_SENT_BINS];
        }

        // Update the index and bin end
        curr_idx += passed_bins;
        bin_end += self.bin_duration.saturating_mul(passed_bins as u32);
        self.curr_idx = Some(curr_idx);
        self.bin_end = Some(bin_end);

        // NOTE: SEARCH draft-09 suggests bit-shifting the values tracked in bins to keep a smaller
        // memory footprint for memory constrained devices at the cost of running some
        // additional computations roughly once per RTT. I suggest not taking on this extra
        // complexity for a minor memory saving (255 bytes going from `usize` to `u16` with
        // `EXTRA_BINS = 15`). That logic could be in this place, if implemented.

        self.acked_bins[curr_idx % Self::NUM_ACKED_BINS] = self.acked_bytes;
        self.sent_bins[curr_idx % Self::NUM_SENT_BINS] = self.sent_bytes;
        Some(curr_idx)
    }

    /// Computes the previous index one RTT ago and the remaining fraction if the previous index
    /// doesn't exactly land on a bin boundary.
    ///
    /// Returns `prev_idx` and `fraction` scaled to `0..[Self::SCALE]`. The `fraction` is returned
    /// as a `u64` because it will be used as such in [`Self::compute_sent`] to avoid `usize`
    /// saturation on 32-bit systems with large bandwidths.
    fn calc_prev_idx(&self, rtt: Duration, curr_idx: usize) -> (usize, u64) {
        let rtt_nanos = rtt.as_nanos();
        let bin_nanos = self.bin_duration.as_nanos();
        let bins_last_rtt = usize::try_from(rtt_nanos / bin_nanos).unwrap_or(usize::MAX);
        let prev_idx = curr_idx.saturating_sub(bins_last_rtt);
        let fraction = u64::try_from((rtt_nanos % bin_nanos) * u128::from(Self::SCALE) / bin_nanos)
            .unwrap_or(0);

        (prev_idx, fraction)
    }

    /// Computes delivered bytes between two bin indices.
    const fn compute_delv(&self, old: usize, new: usize) -> u64 {
        self.acked_bins[new % Self::NUM_ACKED_BINS]
            .saturating_sub(self.acked_bins[old % Self::NUM_ACKED_BINS])
    }

    /// Computes sent bytes between two (previous) bin indices. Interpolates a fraction of each bin
    /// on the ends to get the accurate value when the actual previous timestamp is between two
    /// bins. The fraction is an integer out of [`Self::SCALE`], i.e. a value between `0` and `99`.
    const fn compute_sent(&self, old: usize, new: usize, fraction: u64) -> u64 {
        // NOTE: SEARCH draft-09 does forward interpolation here, i.e. `new + 1` or `old + 1`. That
        // is a mistake in the draft and has been discussed with the SEARCH team. Subtracting is
        // correct.
        let low_idx = self.sent_bins[(new - 1) % Self::NUM_SENT_BINS]
            .saturating_sub(self.sent_bins[(old - 1) % Self::NUM_SENT_BINS]);
        let high_idx = self.sent_bins[new % Self::NUM_SENT_BINS]
            .saturating_sub(self.sent_bins[old % Self::NUM_SENT_BINS]);
        let sent = low_idx * fraction + high_idx * (Self::SCALE as u64 - fraction);
        sent / Self::SCALE as u64
    }

    /// Evaluates whether SEARCH should exit slow start.
    ///
    /// Returns [`Outcome::Exit`] with the current cwnd if the normalized delivery-rate
    /// difference exceeds [`Self::THRESH`], or a non-exit variant explaining why not.
    fn evaluate(&self, rtt: Duration, curr_idx: usize, curr_cwnd: usize) -> Outcome {
        // Compute how many bins fit in the last RTT. Integer division implicitly floors that value,
        // so `prev_idx` might be too recent by a fraction of a bin. Said fraction is scaled to
        // `0..[Self::SCALE]` for interpolation in `compute_sent`.
        let (prev_idx, fraction) = self.calc_prev_idx(rtt, curr_idx);
        debug!("SEARCH: evaluate: prev_idx {prev_idx} curr_idx {curr_idx} fraction {fraction}");

        if prev_idx <= Self::W {
            debug!("SEARCH: evaluate: not enough data for SEARCH evaluation (warming up)");
            return Outcome::WarmingUp;
        }
        if curr_idx - prev_idx >= Self::EXTRA_BINS {
            debug!("SEARCH: evaluate: not enough data for SEARCH evaluation (RTT inflated)");
            return Outcome::RttInflated(curr_idx - prev_idx);
        }

        let curr_delv = self.compute_delv(curr_idx - Self::W, curr_idx);
        let prev_sent = self.compute_sent(prev_idx - Self::W, prev_idx, fraction);
        debug!("SEARCH: evaluate: curr_delv {curr_delv} prev_sent {prev_sent}");

        if prev_sent == 0 {
            debug!("SEARCH: evaluate: prev_sent is zero, can't evaluate");
            return Outcome::ZeroSent;
        }

        let diff = prev_sent.saturating_sub(curr_delv);
        let norm_diff =
            usize::try_from(diff * u64::from(Self::SCALE) / prev_sent).unwrap_or(usize::MAX);

        if norm_diff < Self::THRESH {
            debug!(
                "SEARCH: evaluate: norm_diff {norm_diff} < THRESH {} --> continue",
                Self::THRESH
            );
            return Outcome::Continue(norm_diff);
        }
        debug!(
            "SEARCH: evaluate: norm_diff {norm_diff} >= THRESH {} --> exit",
            Self::THRESH
        );
        Outcome::Exit(curr_cwnd)
    }

    /// Converts an RTT duration to the number of bins it spans (rounded up).
    fn rtt_to_bins(&self, rtt: Duration) -> usize {
        let rtt_ns = u64::try_from(rtt.as_nanos()).unwrap_or(u64::MAX);
        let bin_ns = u64::try_from(self.bin_duration.as_nanos()).unwrap_or(u64::MAX);
        usize::try_from(rtt_ns.div_ceil(bin_ns)).unwrap_or(usize::MAX)
    }

    /// SEARCH suggests a drain-phase to slowly converge towards a BDP estimate. We're currently not
    /// implementing this, but do record the calculated targets for evaluation in this function.
    ///
    /// The draft specifies using the initial rtt to approximate an 'empty-buffer BDP'.
    ///
    /// In addition also record an estimate using the current rtt to approximate BDP with full
    /// buffers. This should estimate the upper end of the CUBIC curve during congestion avoidance.
    fn record_target_cwnd_estimates(
        &self,
        curr_idx: usize,
        rtt: Duration,
        cc_stats: &mut CongestionControlStats,
    ) {
        // Only compute estimates if we have an initial RTT. This is a pure precaution, since we
        // should always have an initial RTT before this method is called.
        let Some(initial_rtt) = self.initial_rtt else {
            return;
        };
        let initial_rtt_bins = self.rtt_to_bins(initial_rtt);
        let initial_rtt_cong_idx = curr_idx.saturating_sub(initial_rtt_bins);
        let empty_buffer_target = self.compute_delv(initial_rtt_cong_idx, curr_idx);

        cc_stats.search_empty_buffer_target = Some(empty_buffer_target);

        // Only compute full_buffer_target when the current RTT fits within the acked_bins
        // circular buffer (W + 1 slots). Beyond that, modulo indexing reads overwritten entries.
        let current_rtt_bins = self.rtt_to_bins(rtt);
        if current_rtt_bins <= Self::W {
            let current_rtt_cong_idx = curr_idx.saturating_sub(current_rtt_bins);
            let full_buffer_target = self.compute_delv(current_rtt_cong_idx, curr_idx);
            cc_stats.search_full_buffer_target = Some(full_buffer_target);
        }
    }

    #[cfg(test)]
    pub const fn curr_idx(&self) -> Option<usize> {
        self.curr_idx
    }

    #[cfg(test)]
    pub const fn bin_end(&self) -> Option<Instant> {
        self.bin_end
    }

    #[cfg(test)]
    pub const fn bin_duration(&self) -> Duration {
        self.bin_duration
    }

    #[cfg(test)]
    pub const fn acked_bin(&self, idx: usize) -> u64 {
        self.acked_bins[idx % Self::NUM_ACKED_BINS]
    }

    #[cfg(test)]
    pub const fn sent_bin(&self, idx: usize) -> u64 {
        self.sent_bins[idx % Self::NUM_SENT_BINS]
    }

    /// Re-exports the internal `calc_prev_idx` function for use in tests.
    #[cfg(test)]
    pub fn calc_prev_idx_test(&self, rtt: Duration, curr_idx: usize) -> (usize, u64) {
        self.calc_prev_idx(rtt, curr_idx)
    }

    /// Re-exports the internal `compute_sent` function for use in tests.
    #[cfg(test)]
    pub const fn compute_sent_test(&self, old: usize, new: usize, fraction: u64) -> u64 {
        self.compute_sent(old, new, fraction)
    }

    /// Re-exports the internal `compute_delv` function for use in tests.
    #[cfg(test)]
    pub const fn compute_delv_test(&self, old: usize, new: usize) -> u64 {
        self.compute_delv(old, new)
    }

    /// Re-exports the internal `evaluate` function for use in tests.
    #[cfg(test)]
    pub fn evaluate_test(&self, rtt: Duration, curr_idx: usize, curr_cwnd: usize) -> Outcome {
        self.evaluate(rtt, curr_idx, curr_cwnd)
    }
}

impl SlowStart for Search {
    fn on_packets_acked(
        &mut self,
        rtt_est: &RttEstimate,
        _largest_acked: packet::Number,
        curr_cwnd: usize,
        cc_stats: &mut CongestionControlStats,
        now: Instant,
    ) -> Option<usize> {
        let rtt = rtt_est.latest_rtt();

        // Guard on the stats fields so post-reset ACKs don't overwrite the initial samples.
        if cc_stats.search_first_rtt.is_none() {
            cc_stats.search_first_rtt = Some(rtt);
        } else {
            cc_stats.search_second_rtt.get_or_insert(rtt);
        }

        // Initialize on first ACK.
        if self.curr_idx.is_none() {
            self.initialize(rtt, now);
        }

        // Early return if we haven't passed the current bin. There is no new data to check.
        if let Some(bin_end) = self.bin_end
            && now <= bin_end
        {
            debug!("SEARCH: on_packets_acked: haven't reached current bin_end");
            return None;
        }

        let curr_idx = self.update_bins(now, cc_stats)?;

        // NOTE: SEARCH draft-09 implements a drain-phase to gradually lower the congestion window
        // towards the approximated empty-buffer BDP. I think that could be counter-intuitive while
        // using CUBIC in our case, as CUBIC tries to keep the buffers full and ideally we'd land
        // somewhere in CUBIC's cwnd-range after slow start. The drain-phase as implemented in
        // draft-09 undershoots CUBIC's cwnd range in my testing.
        //
        // For now I recommend just exiting slow start without the drain-phase and capturing the
        // drain-target in telemetry for further analysis.
        //
        // <https://datatracker.ietf.org/doc/html/draft-chung-ccwg-search-09#section-3.2-17>
        //
        // The match below handles the different outcomes of the SEARCH evaluation, recording stats
        // where applicable and mapping the outcomes to this functions `None` or `Some(cwnd)` return
        // values.
        match self.evaluate(rtt, curr_idx, curr_cwnd) {
            Outcome::Exit(cwnd) => {
                self.record_target_cwnd_estimates(curr_idx, rtt, cc_stats);
                Some(cwnd)
            }
            Outcome::RttInflated(lookback_bins) => {
                cc_stats.search_lookback_bins_needed = cc_stats
                    .search_lookback_bins_needed
                    .max(Some(lookback_bins));
                None
            }
            Outcome::Continue(norm_diff) => {
                cc_stats.search_max_norm_diff = cc_stats.search_max_norm_diff.max(Some(norm_diff));
                None
            }
            Outcome::ZeroSent => {
                cc_stats.search_zero_sent_bytes += 1;
                None
            }
            Outcome::WarmingUp => None,
        }
    }

    fn record_acked_bytes(&mut self, new_acked_bytes: usize) {
        self.acked_bytes = self.acked_bytes.saturating_add(to_u64(new_acked_bytes));
    }

    fn on_packet_sent(&mut self, _sent_pn: packet::Number, sent_bytes: usize) {
        self.sent_bytes = self.sent_bytes.saturating_add(to_u64(sent_bytes));
    }

    fn reset(&mut self) {
        // `curr_idx.is_none()` triggers re-initialization on the next ACK, which overwrites all
        // other relevant fields with fresh data. The cumulative byte counters have to be reset
        // separately so they can still grow while waiting for the first ACK after the reset.
        self.curr_idx = None;
        self.acked_bytes = 0;
        self.sent_bytes = 0;
    }
}
