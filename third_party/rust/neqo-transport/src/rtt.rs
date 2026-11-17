// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Tracking of sent packets and detecting their loss.

use std::{
    cmp::{max, min},
    time::{Duration, Instant},
};

use neqo_common::{Buffer, qlog::Qlog};
use tracing::trace;

use crate::{
    ackrate::{AckRate, PeerAckDelay},
    packet, qlog, recovery,
    stats::FrameStats,
};

/// The smallest time that the system timer (via `sleep()`, `nanosleep()`,
/// `select()`, or similar) can reliably deliver; see `neqo_common::hrtime`.
pub const GRANULARITY: Duration = Duration::from_millis(1);
// Defined in -recovery 6.2 as 333ms but using lower value.
pub const DEFAULT_INITIAL_RTT: Duration = Duration::from_millis(100);

/// The source of the RTT measurement.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
pub enum RttSource {
    /// RTT guess from a retry or dropping a packet number space.
    Guesstimate,
    /// Ack on an unconfirmed connection.
    Ack,
    /// Ack on a confirmed connection.
    AckConfirmed,
}

#[derive(Debug)]
pub struct RttEstimate {
    first_sample_time: Option<Instant>,
    latest_rtt: Duration,
    smoothed_rtt: Duration,
    rttvar: Duration,
    min_rtt: Duration,
    ack_delay: PeerAckDelay,
    best_source: RttSource,
}

impl RttEstimate {
    pub fn new(initial_rtt: Duration) -> Self {
        Self {
            first_sample_time: None,
            latest_rtt: initial_rtt,
            smoothed_rtt: initial_rtt,
            rttvar: initial_rtt / 2,
            min_rtt: initial_rtt,
            ack_delay: PeerAckDelay::default(),
            best_source: RttSource::Guesstimate,
        }
    }

    fn init(&mut self, rtt: Duration) {
        // Only allow this when there are no samples.
        debug_assert!(self.first_sample_time.is_none());
        self.latest_rtt = rtt;
        self.min_rtt = rtt;
        self.smoothed_rtt = rtt;
        self.rttvar = rtt / 2;
    }

    pub fn set_initial(&mut self, rtt: Duration) {
        trace!("initial RTT={rtt:?}");
        if rtt >= GRANULARITY {
            // Ignore if the value is too small.
            self.init(rtt);
        }
    }

    /// For a new path, prime the RTT based on the state of another path.
    pub fn prime_rtt(&mut self, other: &Self) {
        self.set_initial(other.smoothed_rtt + other.rttvar);
        self.ack_delay = other.ack_delay.clone();
    }

    pub const fn set_ack_delay(&mut self, ack_delay: PeerAckDelay) {
        self.ack_delay = ack_delay;
    }

    pub fn update_ack_delay(&mut self, cwnd: usize, mtu: usize) {
        self.ack_delay.update(cwnd, mtu, self.smoothed_rtt);
    }

    pub fn is_guesstimate(&self) -> bool {
        self.best_source == RttSource::Guesstimate
    }

    pub fn update(
        &mut self,
        qlog: &mut Qlog,
        rtt_sample: Duration,
        ack_delay: Duration,
        source: RttSource,
        now: Instant,
    ) {
        debug_assert!(source >= self.best_source);
        self.best_source = max(self.best_source, source);

        // Limit ack delay by max_ack_delay if confirmed.
        let mad = self.ack_delay.max();
        let ack_delay = if self.best_source == RttSource::AckConfirmed && ack_delay > mad {
            mad
        } else {
            ack_delay
        };

        if self.first_sample_time.is_none() {
            // The first sample initializes the estimator without ack delay (RFC 9002, Section 5.3).
            self.init(rtt_sample);
            self.first_sample_time = Some(now);
        } else {
            // min_rtt ignores ack delay (RFC 9002, Section 5.2).
            self.min_rtt = min(self.min_rtt, rtt_sample);
            // Adjust for ack delay unless it goes below `min_rtt` (RFC 9002, Section 5.3).
            let mut adjusted_rtt = rtt_sample;
            if rtt_sample >= ack_delay + self.min_rtt {
                adjusted_rtt -= ack_delay;
            }

            // Calculate EWMA RTT (based on {{?RFC6298}}).
            let rttvar_sample = self.smoothed_rtt.abs_diff(adjusted_rtt);

            // latest_rtt ignores ack delay (RFC 9002, Section 5.1 and Appendix A.7).
            self.latest_rtt = rtt_sample;
            self.rttvar = (self.rttvar * 3 + rttvar_sample) / 4;
            self.smoothed_rtt = (self.smoothed_rtt * 7 + adjusted_rtt) / 8;
        }
        trace!(
            "RTT latest={:?} -> estimate={:?}~{:?}",
            self.latest_rtt, self.smoothed_rtt, self.rttvar
        );
        qlog::metrics_updated(
            qlog,
            [
                qlog::Metric::LatestRtt(self.latest_rtt),
                qlog::Metric::MinRtt(self.min_rtt),
                qlog::Metric::SmoothedRtt(self.smoothed_rtt),
                qlog::Metric::RttVariance(self.rttvar),
            ],
            now,
        );
    }

    /// Get the estimated value.
    pub const fn estimate(&self) -> Duration {
        self.smoothed_rtt
    }

    pub fn pto(&self, confirmed: bool) -> Duration {
        let mut t = self.estimate() + max(4 * self.rttvar, GRANULARITY);
        if confirmed {
            t += self.ack_delay.max();
        }
        t
    }

    /// Calculate the loss delay based on the current estimate and the last
    /// RTT measurement received.
    pub fn loss_delay(&self) -> Duration {
        // kTimeThreshold = 9/8
        // loss_delay = kTimeThreshold * max(latest_rtt, smoothed_rtt)
        // loss_delay = max(loss_delay, kGranularity)
        let rtt = max(self.latest_rtt, self.smoothed_rtt);
        max(rtt * 9 / 8, GRANULARITY)
    }

    pub const fn first_sample_time(&self) -> Option<Instant> {
        self.first_sample_time
    }

    pub const fn latest_rtt(&self) -> Duration {
        self.latest_rtt
    }

    pub const fn rttvar(&self) -> Duration {
        self.rttvar
    }

    pub const fn minimum(&self) -> Duration {
        self.min_rtt
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        self.ack_delay.write_frames(builder, tokens, stats);
    }

    pub const fn frame_lost(&mut self, lost: AckRate) {
        self.ack_delay.frame_lost(lost);
    }

    pub const fn frame_acked(&mut self, acked: AckRate) {
        self.ack_delay.frame_acked(acked);
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::time::Duration;

    use neqo_common::qlog::Qlog;
    use test_fixture::now;

    use super::{DEFAULT_INITIAL_RTT, RttEstimate, RttSource};
    use crate::tracking::DEFAULT_REMOTE_ACK_DELAY;

    fn update(rtt: &mut RttEstimate, sample: Duration, ack_delay: Duration, source: RttSource) {
        rtt.update(&mut Qlog::default(), sample, ack_delay, source, now());
    }

    fn update_ack(rtt: &mut RttEstimate, sample: Duration) {
        update(rtt, sample, Duration::ZERO, RttSource::Ack);
    }

    /// A fresh `RttEstimate` after one real sample using the initial RTT.
    fn initialized_rtt() -> RttEstimate {
        let mut rtt = RttEstimate::new(DEFAULT_INITIAL_RTT);
        update_ack(&mut rtt, DEFAULT_INITIAL_RTT);
        rtt
    }

    #[test]
    fn first_sample_initializes_estimate() {
        let mut rtt = RttEstimate::new(DEFAULT_INITIAL_RTT);
        assert!(rtt.first_sample_time().is_none());
        update_ack(&mut rtt, Duration::from_millis(80));
        assert!(rtt.first_sample_time().is_some());
        assert_eq!(rtt.estimate(), Duration::from_millis(80));
        assert_eq!(rtt.minimum(), Duration::from_millis(80));
    }

    #[test]
    fn first_sample_ignores_ack_delay() {
        let sample = DEFAULT_INITIAL_RTT * 5;
        let mut rtt = RttEstimate::new(DEFAULT_INITIAL_RTT);
        update(&mut rtt, sample, DEFAULT_INITIAL_RTT, RttSource::Ack);
        assert_eq!(rtt.estimate(), sample);
        assert_eq!(rtt.minimum(), sample);
    }

    // Compute the expected EWMA smoothed RTT after one initialization sample and one EWMA sample.
    // smoothed = (prev * 7 + sample) / 8
    fn ewma(prev: Duration, sample: Duration) -> Duration {
        (prev * 7 + sample) / 8
    }

    /// With `AckConfirmed` source and `ack_delay > max_ack_delay`, the `ack_delay`
    /// should be capped to `max_ack_delay`.
    #[test]
    fn ack_confirmed_caps_large_ack_delay() {
        let sample = Duration::from_millis(200);
        let large_delay = DEFAULT_REMOTE_ACK_DELAY + Duration::from_millis(25);
        let effective = sample.checked_sub(DEFAULT_REMOTE_ACK_DELAY).unwrap(); // capped to max_ack_delay
        let mut rtt = initialized_rtt();
        update(&mut rtt, sample, large_delay, RttSource::AckConfirmed);
        assert_eq!(rtt.estimate(), ewma(DEFAULT_INITIAL_RTT, effective));
    }

    /// With a non-confirmed source, `ack_delay > max_ack_delay` is NOT capped.
    #[test]
    fn non_confirmed_does_not_cap_ack_delay() {
        let sample = Duration::from_millis(200);
        let large_delay = DEFAULT_REMOTE_ACK_DELAY + Duration::from_millis(25);
        let effective = sample.checked_sub(large_delay).unwrap(); // full delay applied
        let mut rtt = initialized_rtt();
        update(&mut rtt, sample, large_delay, RttSource::Ack);
        assert_eq!(rtt.estimate(), ewma(DEFAULT_INITIAL_RTT, effective));
    }

    #[test]
    fn latest_rtt_ignores_ack_delay() {
        let sample = DEFAULT_INITIAL_RTT * 2;
        let adjusted = sample.checked_sub(DEFAULT_REMOTE_ACK_DELAY).unwrap();
        let mut rtt = initialized_rtt();
        update(&mut rtt, sample, DEFAULT_REMOTE_ACK_DELAY, RttSource::Ack);
        assert_eq!(rtt.latest_rtt(), sample);
        assert_eq!(rtt.estimate(), ewma(DEFAULT_INITIAL_RTT, adjusted));
    }

    #[test]
    fn min_rtt_tracks_minimum() {
        let mut rtt = RttEstimate::new(DEFAULT_INITIAL_RTT);
        update_ack(&mut rtt, Duration::from_millis(100));
        update_ack(&mut rtt, Duration::from_millis(50));
        assert_eq!(rtt.minimum(), Duration::from_millis(50));
        update_ack(&mut rtt, Duration::from_millis(200));
        assert_eq!(rtt.minimum(), Duration::from_millis(50)); // Still 50ms.
    }
}
