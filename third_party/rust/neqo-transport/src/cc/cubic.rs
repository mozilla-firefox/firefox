// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! CUBIC congestion control (RFC 9438)

use std::{
    fmt::{self, Display},
    time::{Duration, Instant},
};

use tracing::{debug, trace};

use crate::{
    cc::{
        CongestionTrigger::{self, Ecn},
        classic_cc::WindowAdjustment,
    },
    stats::CongestionControlStats,
};

/// Convert an integer congestion window value into a floating point value.
/// This has the effect of reducing larger values to `1<<53`.
/// If you have a congestion window that large, something is probably wrong.
pub fn convert_to_f64(v: usize) -> f64 {
    let mut f_64 = f64::from(u32::try_from(v >> 21).unwrap_or(u32::MAX));
    f_64 *= 2_097_152.0; // f_64 <<= 21
    #[expect(clippy::cast_possible_truncation, reason = "The mask makes this safe.")]
    let v_trunc = (v & 0x1f_ffff) as u32;
    f_64 += f64::from(v_trunc);
    f_64
}

#[derive(Debug, Default, Clone)]
pub struct State {
    /// > An estimate for the congestion window \[...\] in the Reno-friendly region -- that
    /// > is, an estimate for the congestion window of Reno.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-variables-of-interest>
    ///
    /// > Reno performs well in certain types of networks -- for example, under short RTTs and
    /// > small bandwidths (or small BDPs). In these networks, CUBIC remains in the Reno-friendly
    /// > region to achieve at least the same throughput as Reno.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-reno-friendly-region>
    w_est: f64,
    /// > The time period in seconds it takes to increase the congestion window size
    /// > at the beginning of the current congestion avoidance stage to `w_max`.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-variables-of-interest>
    ///
    /// Formula:
    ///
    /// `k = cubic_root((w_max - cwnd_epoch) / C)`
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-window-increase-function>
    k: f64,
    /// > Size of `cwnd` in \[bytes\] just before `cwnd` was reduced in the last congestion
    /// > event \[...\]. \[With\] fast convergence enabled, `w_max` may be further reduced based on
    /// > the current saturation point.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-variables-of-interest>
    ///
    /// `w_max` acts as the plateau for the cubic function where it switches from the concave to
    /// the convex region.
    ///
    /// It is calculated with the following logic:
    ///
    /// ```pseudo
    /// if (w_max > cwnd) {
    ///     w_max = cwnd * FAST_CONVERGENCE_FACTOR;
    /// } else {
    ///     w_max = cwnd;
    /// }
    /// ```
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-fast-convergence>
    w_max: Option<f64>,
    /// > The time in seconds at which the current congestion avoidance stage started.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-variables-of-interest>
    ///
    /// This is also reset on being application limited.
    t_epoch: Option<Instant>,
    /// New and unused leftover acked bytes for calculating the reno region increases to `w_est`.
    reno_acked_bytes: f64,
}

impl Display for State {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "state [w_max: {:?}, k: {}, t_epoch: {:?}]",
            self.w_max, self.k, self.t_epoch
        )?;
        Ok(())
    }
}

#[derive(Debug)]
pub struct Cubic {
    /// Current CUBIC parameters.
    current: State,
    /// CUBIC parameters that have been stored on a congestion event to restore later in case it
    /// turns out to have been spurious.
    stored: Option<State>,
    /// Whether to apply the alternative backoff factor [`Self::BETA_USIZE_DIVIDEND_ECN`] on ECN-CE
    /// congestion events (RFC 8511).
    abe: bool,
}

impl Default for Cubic {
    fn default() -> Self {
        Self {
            current: State::default(),
            stored: None,
            abe: true,
        }
    }
}

impl Display for Cubic {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "Cubic {}", self.current)
    }
}

impl Cubic {
    /// > Constant that determines the aggressiveness of CUBIC in competing with other congestion
    /// > control algorithms in high-BDP networks.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-constants-of-interest>
    ///
    /// See section 5.1 of RFC9438 for discussion on how to set the concrete value:
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-fairness-to-reno>
    pub const C: f64 = 0.4;

    /// > CUBIC additive increase factor used in the Reno-friendly region \[to achieve approximately
    /// > the same average congestion window size as Reno\].
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-constants-of-interest>
    ///
    /// > The model used to calculate CUBIC_ALPHA is not absolutely precise,
    /// > but analysis and simulation \[...\], as well as over a decade of experience with
    /// > CUBIC in the public Internet, show that this approach produces acceptable
    /// > levels of rate fairness between CUBIC and Reno flows.
    ///
    /// Formula:
    ///
    /// `ALPHA = 3.0 * (1.0 - CUBIC_BETA) / (1.0 + CUBIC_BETA)`
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-reno-friendly-region>
    pub const ALPHA: f64 = 3.0 * (1.0 - 0.7) / (1.0 + 0.7); // with CUBIC_BETA = 0.7

    /// > CUBIC multiplicative decrease factor
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-constants-of-interest>
    ///
    /// > To balance between the scalability and convergence speed, CUBIC sets the multiplicative
    /// > window
    /// > decrease factor to 0.7 while Standard TCP uses 0.5. While this improves the scalability of
    /// > CUBIC, a side effect of this decision is slower convergence, especially under low
    /// > statistical multiplexing environments.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-principle-4-for-the-cubic-d>
    ///
    /// For implementation reasons neqo uses a dividend and divisor approach with `usize` typing.
    /// The divisor is set to `100` to also accommodate the `0.85` beta value for ECN induced
    /// congestion events.
    pub const BETA_USIZE_DIVISOR: usize = 100;

    /// > CUBIC multiplicative decrease factor
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-constants-of-interest>
    ///
    /// > To balance between the scalability and convergence speed, CUBIC sets the multiplicative
    /// > window
    /// > decrease factor to 0.7 while Standard TCP uses 0.5. While this improves the scalability of
    /// > CUBIC, a side effect of this decision is slower convergence, especially under low
    /// > statistical multiplexing environments.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-principle-4-for-the-cubic-d>
    ///
    /// For implementation reasons neqo uses a dividend and divisor approach with `usize` typing to
    /// construct `CUBIC_BETA = 0.7` from `70/100`.
    pub const BETA_USIZE_DIVIDEND: usize = 70;

    /// As per RFC 8511 it makes sense to have a different decrease factor for ECN-CE congestion
    /// events than for loss induced congestion events.
    ///
    /// > CUBIC connections benefit from beta_{ecn} of 0.85.
    ///
    /// <https://www.rfc-editor.org/rfc/rfc8511.html#section-3.1>
    ///
    /// For implementation reasons neqo uses a dividend and divisor approach with `usize` typing to
    /// construct the beta value from `85/100`.
    pub const BETA_USIZE_DIVIDEND_ECN: usize = 85;

    /// Enable or disable Alternative Backoff with ECN (RFC 8511). Enabled by default.
    #[must_use]
    pub const fn abe(mut self, abe: bool) -> Self {
        self.abe = abe;
        self
    }

    /// This is the factor that is used by fast convergence to further reduce the next `W_max` when
    /// a congestion event occurs while `cwnd < W_max`. This speeds up the bandwidth release for
    /// when a new flow joins the network.
    ///
    /// The calculation assumes `CUBIC_BETA = 0.7`.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#name-fast-convergence>
    pub const FAST_CONVERGENCE_FACTOR: f64 = f64::midpoint(1.0, 0.7);

    /// Original equation is:
    ///
    /// `k = cubic_root((w_max - cwnd_epoch)/C)`
    ///
    /// with `cwnd_epoch` being the congestion window at the start of the current congestion
    /// avoidance stage (so at time `t_epoch`).
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#figure-2>
    ///
    /// Taking into account that neqo is using bytes but the formula assumes segments for both
    /// `w_max` and `cwnd_epoch` it becomes:
    ///
    /// `k = cubic_root((w_max - cwnd_epoch)/SMSS/C)`
    fn calc_k(w_max: f64, cwnd_epoch: f64, max_datagram_size: f64) -> f64 {
        ((w_max - cwnd_epoch) / max_datagram_size / Self::C).cbrt()
    }

    /// `w_cubic(t) = C*(t-K)^3 + w_max`
    ///
    /// with `t = t_current - t_epoch`.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#figure-1>
    ///
    /// Taking into account that neqo is using bytes and the formula returns segments and that
    /// `w_max` already is in bytes the formula becomes:
    ///
    /// `w_cubic(t) = (C*(t-K)^3) * SMSS + w_max`
    fn w_cubic(&self, t: f64, w_max: f64, max_datagram_size: f64) -> f64 {
        (Self::C * (t - self.current.k).powi(3)).mul_add(max_datagram_size, w_max)
    }

    /// Sets `w_est`, `k`, `t_epoch` and `reno_acked_bytes` at the start of a new epoch
    /// (new congestion avoidance stage) according to RFC 9438. The `w_max` variable has
    /// been set in `reduce_cwnd()` prior to this call.
    ///
    /// > `w_est` is set equal to `cwnd_epoch` at the start of the congestion avoidance stage.
    ///
    /// <https://datatracker.ietf.org/doc/html/rfc9438#section-4.3-9>
    ///
    /// Also initializes `k` and `w_max` if we start an epoch without having ever had
    /// a congestion event, which can happen upon exiting slow start.
    fn start_epoch(
        &mut self,
        curr_cwnd: f64,
        new_acked_bytes: f64,
        max_datagram_size: f64,
        now: Instant,
    ) {
        self.current.t_epoch = Some(now);
        self.current.reno_acked_bytes = new_acked_bytes;
        self.current.w_est = curr_cwnd;
        // If `w_max < cwnd_epoch` we take the cubic root from a negative value in `calc_k()`. That
        // could only happen if somehow `cwnd` get's increased between calling `reduce_cwnd()` and
        // `start_epoch()`. This could happen if we exit slow start without packet loss, thus never
        // had a congestion event and called `reduce_cwnd()` which means `w_max` was never set
        // (`None`). For those cases we reset/initialize `w_max` here and appropriately set `k` to
        // `0.0` (`k` is the time for `cwnd` to reach `w_max`).
        self.current.k = match self.current.w_max {
            Some(w_max) if w_max > curr_cwnd => Self::calc_k(w_max, curr_cwnd, max_datagram_size),
            _ => {
                self.current.w_max = Some(curr_cwnd);
                0.0
            }
        };
        trace!("[{self}] New epoch");
    }

    #[cfg(test)]
    pub const fn w_max(&self) -> Option<f64> {
        self.current.w_max
    }

    #[cfg(test)]
    pub const fn set_w_max(&mut self, w_max: f64) {
        self.current.w_max = Some(w_max);
    }
}

impl WindowAdjustment for Cubic {
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_sign_loss,
        reason = "Cast from f64 to usize."
    )]
    fn bytes_for_cwnd_increase(
        &mut self,
        curr_cwnd: usize,
        new_acked_bytes: usize,
        min_rtt: Duration,
        max_datagram_size: usize,
        now: Instant,
    ) -> usize {
        let curr_cwnd = convert_to_f64(curr_cwnd);
        let new_acked_bytes = convert_to_f64(new_acked_bytes);
        let max_datagram_size = convert_to_f64(max_datagram_size);

        let t_epoch = if let Some(t) = self.current.t_epoch {
            self.current.reno_acked_bytes += new_acked_bytes;
            t
        } else {
            // If we get here with `self.t_epoch == None` this is a new congestion
            // avoidance stage. It's been set to `None` by
            // [`super::ClassicCongestionController::reduce_cwnd`] or needs to be
            // initialized after slow start. It could also have been reset by
            // [`super::ClassicCongestionController::on_app_limited`] in which case we also start a
            // new congestion avoidance stage for the purpose of resetting
            // timing as per RFC 9438 section 5.8.
            //
            // <https://datatracker.ietf.org/doc/html/rfc9438#app-limited>
            self.start_epoch(curr_cwnd, new_acked_bytes, max_datagram_size, now);
            self.current
                .t_epoch
                .expect("unwrapping `None` value -- it should've been set by `start_epoch`")
        };

        // Calculate `target_cubic` for the concave or convex region
        //
        // > Upon receiving a new ACK during congestion avoidance, CUBIC computes the target
        // > congestion window size after the next RTT [...], where RTT is the
        // > smoothed round-trip time. The lower and upper bounds below ensure that CUBIC's
        // > congestion window increase rate is non-decreasing and is less than the increase rate of
        // > slow start.
        //
        // <https://datatracker.ietf.org/doc/html/rfc9438#section-4.2-10>
        //
        // We are using `min_rtt` rather than `smoothed_rtt` to avoid growth being more aggressive
        // when already under queueing delay. This is in line with what the Linux Kernel does:
        //
        // (https://github.com/torvalds/linux/blob/7e1392a42ea288d70a266f6553fe06f57f8647ea/net/ipv4/tcp_cubic.c#L265)
        //
        // In neqo the target congestion window is in bytes.
        let t = now.saturating_duration_since(t_epoch);
        let w_max = self
            .current
            .w_max
            .expect("w_max must be set in self.start_epoch");
        // cwnd <= target_cubic <= cwnd * 1.5
        let target_cubic = f64::clamp(
            self.w_cubic((t + min_rtt).as_secs_f64(), w_max, max_datagram_size),
            curr_cwnd,
            curr_cwnd * 1.5,
        );

        // Calculate w_est for the Reno-friendly region with a slightly adjusted formula per the
        // below:
        //
        // > Note that this equation uses segments_acked and cwnd is measured in segments. An
        // > implementation that measures cwnd in bytes should adjust the equation accordingly
        // > using the number of acknowledged bytes and the SMSS.
        //
        // Formula: w_est += (alpha * bytes_acked / cwnd) * SMSS
        //
        // <https://datatracker.ietf.org/doc/html/rfc9438#section-4.3-9>

        // We first calculate the increase in segments and floor it to only include whole segments.
        let increase = (Self::ALPHA * self.current.reno_acked_bytes / curr_cwnd).floor();

        // Only apply the increase if it is at least by one segment.
        if increase > 0.0 {
            self.current.w_est = increase.mul_add(max_datagram_size, self.current.w_est);
            // Because we floored the increase to whole segments we cannot just zero
            // `reno_acked_bytes` but have to calculate the actual bytes used.
            let acked_bytes_used = increase * curr_cwnd / Self::ALPHA;
            self.current.reno_acked_bytes -= acked_bytes_used;
        }

        // > When receiving a new ACK in congestion avoidance (where cwnd could be greater than
        // > or less than w_max), CUBIC checks whether W_cubic(t) is less than w_est.  If so, CUBIC
        // > is in the Reno-friendly region and cwnd SHOULD be set to w_est at each reception of a
        // > new ACK.
        //
        // <https://datatracker.ietf.org/doc/html/rfc9438#section-4.3-8>
        //
        // While the RFC specifies that we should compare `w_cubic(t)` with `w_est` we are rather
        // comparing the previously calculated `target` here (`w_cubic(t + min_rtt)` with clamping
        // to `cwnd <= target <= cwnd * 1.5` applied), since that is the value that would actually
        // be used if we are in the cubic region.
        //
        // That is in line with what e.g. the Linux Kernel CUBIC implementation is doing.
        //
        // <https://github.com/torvalds/linux/blob/d7ee5bdce7892643409dea7266c34977e651b479/net/ipv4/tcp_cubic.c#L313>
        let target = target_cubic.max(self.current.w_est);

        let cwnd_increase = target - curr_cwnd;

        // Calculate the number of bytes that would need to be acknowledged for an increase
        // of `max_datagram_size` to match `cwnd_increase`, that is the increase from the current
        // congestion window to `target`.
        // The amount of acked data required therefore reduces asymptotically as the target
        // increases.
        //
        // RFC 9438 tells us to increase cwnd by `cwnd_increase/cwnd` which would amount to the
        // increase in segments per congestion window acked.
        //
        // (https://datatracker.ietf.org/doc/html/rfc9438#section-4.4-2.1)
        //
        // Since we want to know how much we need to ack to increase by 1 segment we need the
        // inverse of that, which would be `cwnd/cwnd_increase`.
        // (E.g. if we'd increase by `1/4 * mss` per cwnd acked then we need to ack `4 * cwnd` to
        // increase by `1 * mss`)
        //
        // The RFC only applies this increase per acked cwnd to the Cubic (concave/convex) region.
        // We also apply it to the Reno region, as that is what the Linux Kernel CUBIC
        // implementation does, too.
        //
        // <https://github.com/torvalds/linux/blob/d7ee5bdce7892643409dea7266c34977e651b479/net/ipv4/tcp_cubic.c#L311-L315>
        //
        // We multiply by `max_datagram_size` as our `curr_cwnd` value is in bytes and prevent
        // division by zero by setting `cwnd_increase` to `1` for the `target == curr_cwnd` case.
        (max_datagram_size * curr_cwnd / cwnd_increase.max(1.0)) as usize
    }

    // CUBIC RFC 9438 changes the logic for multiplicative decrease, most notably setting the
    // minimum congestion window to 1*SMSS under some circumstances while keeping ssthresh at
    // 2*SMSS.
    //
    // <https://datatracker.ietf.org/doc/html/rfc9438#section-4.6>
    //
    // QUIC has a minimum congestion window of 2*SMSS as per RFC 9002.
    //
    // <https://datatracker.ietf.org/doc/html/rfc9002#section-4.8>
    //
    // For that reason we diverge from CUBIC RFC 9438 here retaining the 2*SMSS minimum for the
    // congestion window.
    //
    // This function only returns the value for `cwnd * CUBIC_BETA` and sets some variables for the
    // start of a new congestion avoidance phase. Actually setting the congestion window happens in
    // [`super::ClassicCongestionController::on_congestion_event`] where this function is called.
    fn reduce_cwnd(
        &mut self,
        curr_cwnd: usize,
        acked_bytes: usize,
        max_datagram_size: usize,
        congestion_trigger: CongestionTrigger,
        cc_stats: &mut CongestionControlStats,
    ) -> (usize, usize) {
        let curr_cwnd_f64 = convert_to_f64(curr_cwnd);
        // Fast Convergence
        //
        // > During a congestion event, if the current cwnd is less than w_max, this indicates
        // > that the saturation point experienced by this flow is getting reduced because of
        // > a change in available bandwidth. This flow can then release more bandwidth by
        // > reducing w_max further. This action effectively lengthens the time for this flow
        // > to increase its congestion window, because the reduced w_max forces the flow to
        // > plateau earlier. This allows more time for the new flow to catch up to its
        // > congestion window size.
        //
        // <https://datatracker.ietf.org/doc/html/rfc9438#name-fast-convergence>
        //
        // From the old implementation:
        //
        // "Check cwnd + MAX_DATAGRAM_SIZE instead of cwnd because with cwnd in bytes, cwnd may be
        // slightly off."
        self.current.w_max = Some(
            if self
                .current
                .w_max
                .is_some_and(|w| curr_cwnd_f64 + convert_to_f64(max_datagram_size) < w)
            {
                curr_cwnd_f64 * Self::FAST_CONVERGENCE_FACTOR
            } else {
                curr_cwnd_f64
            },
        );
        cc_stats.w_max = self.current.w_max;

        // Reducing the congestion window and resetting time
        self.current.t_epoch = None;
        let beta_dividend = if self.abe && congestion_trigger == Ecn {
            Self::BETA_USIZE_DIVIDEND_ECN
        } else {
            Self::BETA_USIZE_DIVIDEND
        };
        (
            curr_cwnd * beta_dividend / Self::BETA_USIZE_DIVISOR,
            acked_bytes * beta_dividend / Self::BETA_USIZE_DIVISOR,
        )
    }

    fn on_app_limited(&mut self) {
        // Reset t_epoch. Let it start again when the congestion controller
        // exits the app-limited period.
        self.current.t_epoch = None;
    }

    fn save_undo_state(&mut self) {
        self.stored = Some(self.current.clone());
    }

    fn restore_undo_state(&mut self, cc_stats: &mut CongestionControlStats) {
        let Some(stored) = self.stored.take() else {
            debug_assert!(false, "couldn't restore {self} specific undo state");
            return;
        };

        debug!(
            "Spurious cong event: recovering cubic state from {} to {stored}",
            self.current
        );
        self.current = stored;
        cc_stats.w_max = self.current.w_max;
    }
}
