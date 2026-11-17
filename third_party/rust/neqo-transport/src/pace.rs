// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Pacer

#![cfg_attr(
    feature = "bench",
    expect(
        clippy::missing_panics_doc,
        clippy::must_use_candidate,
        reason = "`Pacer` is only public API when the `bench` feature is enabled."
    )
)]

use std::{
    cmp::min,
    fmt::{self, Debug, Display, Formatter},
    time::{Duration, Instant},
};

use neqo_common::to_u64;
use tracing::trace;

use crate::rtt::GRANULARITY;

/// A pacer that uses a leaky bucket.
pub struct Pacer {
    /// Whether pacing is enabled.
    enabled: bool,
    /// The last update time.
    t: Instant,
    /// The maximum capacity, or burst size, in bytes.
    m: usize,
    /// The current capacity, in bytes. When negative, represents accumulated debt
    /// from sub-granularity sends that will be paid off in future pacing calculations.
    c: isize,
    /// The packet size or minimum capacity for sending, in bytes.
    p: usize,
}

impl Pacer {
    /// This value determines how much faster the pacer operates than the
    /// congestion window.
    ///
    /// A value of 1 would cause all packets to be spaced over the entire RTT,
    /// which is a little slow and might act as an additional restriction in
    /// the case the congestion controller increases the congestion window.
    /// This value spaces packets over half the congestion window, which matches
    /// our current congestion controller, which double the window every RTT.
    const SPEEDUP: u64 = 2;

    /// Create a new `Pacer`.  This takes the current time, the maximum burst size,
    /// and the packet size.
    ///
    /// The value of `m` is the maximum capacity in bytes.  `m` primes the pacer
    /// with credit and determines the burst size.  `m` must not exceed
    /// the initial congestion window, but it should probably be lower.
    ///
    /// The value of `p` is the packet size in bytes, which determines the minimum
    /// credit needed before a packet is sent.  This should be a substantial
    /// fraction of the maximum packet size, if not the packet size.
    pub fn new(enabled: bool, now: Instant, m: usize, p: usize) -> Self {
        assert!(m >= p, "maximum capacity has to be at least one packet");
        assert!(isize::try_from(p).is_ok(), "p ({p}) exceeds isize::MAX");
        Self {
            enabled,
            t: now,
            m,
            c: isize::try_from(m).expect("maximum capacity fits into isize"),
            p,
        }
    }

    pub const fn mtu(&self) -> usize {
        self.p
    }

    pub const fn set_mtu(&mut self, mtu: usize) {
        self.p = mtu;
    }

    /// Determine when the next packet will be available based on the provided
    /// RTT, provided congestion window and accumulated credit or debt.  This
    /// doesn't update state.  This returns a time, which could be in the past
    /// (this object doesn't know what the current time is).
    pub fn next(&self, rtt: Duration, cwnd: usize) -> Instant {
        let packet = isize::try_from(self.p).expect("packet size fits into isize");

        if self.c >= packet {
            trace!("[{self}] next {cwnd}/{rtt:?} no wait = {:?}", self.t);
            return self.t;
        }

        // This is the inverse of the function in `spend`:
        // self.t + rtt * (self.p - self.c) / (Self::SPEEDUP * cwnd)
        //
        // `deficit` can exceed 2 × MTU when `self.c` carries accumulated debt
        // from consecutive sub-granularity sends.  `saturating_mul` caps the
        // product safely regardless of the actual value.
        let Ok(deficit) = u64::try_from(packet - self.c) else {
            trace!("[{self}] next {cwnd}/{rtt:?} deficit overflow");
            return self.t;
        };
        let rtt_ns = u64::try_from(rtt.as_nanos()).unwrap_or(u64::MAX);
        let divisor = to_u64(cwnd).saturating_mul(Self::SPEEDUP);
        let w_ns = rtt_ns.saturating_mul(deficit) / divisor;

        // If the increment is below the timer granularity, send immediately.
        #[expect(
            clippy::cast_possible_truncation,
            reason = "GRANULARITY is 1ms, fits in u64"
        )]
        if w_ns < GRANULARITY.as_nanos() as u64 {
            trace!("[{self}] next {cwnd}/{rtt:?} below granularity ({w_ns}ns)");
            return self.t;
        }

        let nxt = self.t + Duration::from_nanos(w_ns);
        trace!("[{self}] next {cwnd}/{rtt:?} wait {w_ns}ns = {nxt:?}");
        nxt
    }

    /// Bytes sendable at `SPEEDUP * cwnd / rtt` pace over `elapsed`.
    /// Returns `None` if `rtt` is zero.
    ///
    /// The key product is `elapsed_ns * cwnd * SPEEDUP`.  At 400 Gbps with a
    /// 100 ms RTT the BDP is ~5 GB, so `factor` = cwnd * 2 ≈ 10^10.  The
    /// inter-packet interval at that rate is ~24 ns, giving a product of
    /// ~2.4*10^11, well within u64.  Even a full-RTT elapsed (10^8 ns) gives
    /// 10^8 * 10^10 = 10^18 < `u64::MAX` (1.8*10^19).  Beyond that the
    /// `saturating_mul` caps the value and callers clamp to `self.m`.
    fn bytes_for(cwnd: usize, rtt: Duration, elapsed: Duration) -> Option<u64> {
        let rtt_ns = u64::try_from(rtt.as_nanos()).unwrap_or(u64::MAX);
        let elapsed_ns = u64::try_from(elapsed.as_nanos()).unwrap_or(u64::MAX);
        let factor = to_u64(cwnd).saturating_mul(Self::SPEEDUP);
        elapsed_ns.saturating_mul(factor).checked_div(rtt_ns)
    }

    /// Compute the effective pacing rate in bytes per second.
    ///
    /// Returns `None` if `rtt` is zero.
    pub(crate) fn rate(cwnd: usize, rtt: Duration) -> Option<u64> {
        Self::bytes_for(cwnd, rtt, Duration::from_secs(1))
    }

    /// Spend credit. This cannot fail, but instead may carry debt into the
    /// future (see [`Pacer::c`]). Users of this API are expected to call
    /// [`Pacer::next`] to determine when to spend.
    ///
    /// This function takes the current time (`now`), an estimate of the round
    /// trip time (`rtt`), the estimated congestion window (`cwnd`), and the
    /// number of bytes that were sent (`count`).
    pub fn spend(&mut self, now: Instant, rtt: Duration, cwnd: usize, count: usize) {
        if !self.enabled {
            self.t = now;
            return;
        }

        trace!("[{self}] spend {count} over {cwnd}, {rtt:?}");
        // Increase the capacity by the elapsed fraction of the RTT times the
        // pacing rate, i.e. `(now - self.t) * SPEEDUP * cwnd / rtt`.
        let incr = Self::bytes_for(cwnd, rtt, now.saturating_duration_since(self.t))
            .and_then(|b| usize::try_from(b).ok())
            .unwrap_or(self.m);

        // Add the capacity up to a limit of `self.m`, then subtract `count`.
        self.c = min(
            isize::try_from(self.m).unwrap_or(isize::MAX),
            self.c
                .saturating_add_unsigned(incr)
                .saturating_sub_unsigned(count),
        );
        self.t = now;
    }
}

impl Display for Pacer {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Pacer {}/{}", self.c, self.p)
    }
}

impl Debug for Pacer {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Pacer@{:?} {}/{}..{}", self.t, self.c, self.p, self.m)
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::time::Duration;

    use test_fixture::now;

    use super::Pacer;

    const RTT: Duration = Duration::from_secs(1);
    const PACKET: usize = 1000;
    const CWND: usize = PACKET * 10;

    #[test]
    fn even() {
        let n = now();
        let mut p = Pacer::new(true, n, PACKET, PACKET);
        assert_eq!(p.next(RTT, CWND), n);
        p.spend(n, RTT, CWND, PACKET);
        assert_eq!(p.next(RTT, CWND), n + (RTT / 20));
    }

    #[test]
    fn backwards_in_time() {
        let n = now();
        let mut p = Pacer::new(true, n + RTT, PACKET, PACKET);
        assert_eq!(p.next(RTT, CWND), n + RTT);
        // Now spend some credit in the past using a time machine.
        p.spend(n, RTT, CWND, PACKET);
        assert_eq!(p.next(RTT, CWND), n + (RTT / 20));
    }

    #[test]
    fn pacing_disabled() {
        let n = now();
        let mut p = Pacer::new(false, n, PACKET, PACKET);
        assert_eq!(p.next(RTT, CWND), n);
        p.spend(n, RTT, CWND, PACKET);
        assert_eq!(p.next(RTT, CWND), n);
    }

    #[test]
    fn send_immediately_below_granularity() {
        const SHORT_RTT: Duration = Duration::from_millis(10);
        let n = now();
        let mut p = Pacer::new(true, n, PACKET, PACKET);
        assert_eq!(p.next(SHORT_RTT, CWND), n);
        p.spend(n, SHORT_RTT, CWND, PACKET);
        assert_eq!(
            p.next(SHORT_RTT, CWND),
            n,
            "Expect packet to be sent immediately, instead of being paced below timer granularity"
        );
    }

    #[test]
    fn sends_below_granularity_accumulate_eventually() {
        const RTT: Duration = Duration::from_millis(100);
        const BW: usize = 50 * 1_000_000;
        let bdp = usize::try_from(
            u128::try_from(BW / 8).expect("usize fits in u128") * RTT.as_nanos()
                / Duration::from_secs(1).as_nanos(),
        )
        .expect("cwnd fits in usize");
        let mut n = now();
        let mut p = Pacer::new(true, n, 2 * PACKET, PACKET);
        let start = n;
        let packet_count = 10_000;
        for _ in 0..packet_count {
            n = p.next(RTT, bdp);
            p.spend(n, RTT, bdp, PACKET);
        }
        // We expect _some_ time to have progressed after sending all the packets.
        assert!(n - start > Duration::ZERO);
    }

    #[test]
    fn rate_basic() {
        // 10 KB cwnd, 100 ms RTT → 2 * 10_000 * 1e9 / 100_000_000 = 200_000 B/s
        assert_eq!(
            Pacer::rate(10_000, Duration::from_millis(100)),
            Some(200_000)
        );
    }

    #[test]
    fn rate_zero_rtt() {
        assert_eq!(Pacer::rate(10_000, Duration::ZERO), None);
    }

    /// When the computed wait equals GRANULARITY exactly, pacing should NOT
    /// send immediately; only strictly sub-granularity waits are suppressed.
    #[test]
    fn not_immediately_at_exact_granularity() {
        // Choose RTT and CWND so w = rtt * PACKET / (SPEEDUP * cwnd) = 1ms = GRANULARITY.
        // With PACKET=1000, SPEEDUP=2: cwnd = rtt_ns * 1000 / (2 * 1_000_000).
        // rtt=10ms → cwnd = 10_000_000 * 1000 / 2_000_000 = 5000.
        const SHORT_RTT: Duration = Duration::from_millis(10);
        const CWND_AT_GRANULARITY: usize = 5000; // yields w = 1ms = GRANULARITY
        let n = now();
        let mut p = Pacer::new(true, n, PACKET, PACKET);
        p.spend(n, SHORT_RTT, CWND_AT_GRANULARITY, PACKET);
        // w == GRANULARITY: should schedule for later, not send immediately.
        assert_ne!(
            p.next(SHORT_RTT, CWND_AT_GRANULARITY),
            n,
            "at exactly GRANULARITY should not send immediately"
        );
    }

    #[test]
    fn pacer_display_and_debug() {
        let mut p = Pacer::new(true, now(), PACKET, PACKET);
        assert_eq!(p.mtu(), PACKET);
        p.set_mtu(500);
        assert_eq!(p.mtu(), 500);
        p.set_mtu(PACKET);
        assert_eq!(p.to_string(), "Pacer 1000/1000");
        assert!(format!("{p:?}").starts_with("Pacer@"));
    }
}
