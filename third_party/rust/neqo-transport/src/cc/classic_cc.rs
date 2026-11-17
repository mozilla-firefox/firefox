// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Congestion control

use std::{
    cmp::{max, min},
    fmt::{Debug, Display},
    time::{Duration, Instant},
};

use neqo_common::{const_max, const_min, qlog::Qlog};
use rustc_hash::FxHashMap as HashMap;
use tracing::{debug, info, trace};

use super::CongestionController;
use crate::{
    Pmtud,
    cc::CongestionTrigger::{self, Ecn, Loss},
    packet, qlog,
    recovery::sent,
    rtt::RttEstimate,
    sender::PACING_BURST_SIZE,
    stats::{CongestionControlStats, SlowStartExitReason, SlowStartExitStats},
};

pub const CWND_INITIAL_PKTS: usize = 10;
pub const PERSISTENT_CONG_THRESH: u32 = 3;

#[derive(Debug, Clone, Copy, PartialEq, Eq, strum::IntoStaticStr)]
pub enum Phase {
    /// In either slow start or congestion avoidance, not recovery.
    #[strum(to_string = "slow_start")]
    SlowStart,
    /// In congestion avoidance.
    #[strum(to_string = "congestion_avoidance")]
    CongestionAvoidance,
    /// In a recovery period, but no packets have been sent yet.  This is a
    /// transient phase because we want to exempt the first packet sent after
    /// entering recovery from the congestion window.
    #[strum(to_string = "recovery")]
    RecoveryStart,
    /// In a recovery period, with the first packet sent at this time.
    #[strum(to_string = "recovery")]
    Recovery,
    /// Start of persistent congestion, which is transient, like `RecoveryStart`.
    #[strum(to_string = "slow_start")]
    PersistentCongestion,
}

impl Phase {
    pub const fn in_recovery(self) -> bool {
        matches!(self, Self::RecoveryStart | Self::Recovery)
    }

    pub fn in_slow_start(self) -> bool {
        self == Self::SlowStart
    }

    /// These states are transient, we tell qlog on entry, but not on exit.
    pub const fn transient(self) -> bool {
        matches!(self, Self::RecoveryStart | Self::PersistentCongestion)
    }

    /// Update a transient phase to the actual phase.
    pub fn update(&mut self) {
        *self = match self {
            Self::PersistentCongestion => Self::SlowStart,
            Self::RecoveryStart => Self::Recovery,
            _ => unreachable!(),
        };
    }
}

pub trait WindowAdjustment: Display + Debug {
    /// This is called when an ack is received.
    /// The function calculates the amount of acked bytes congestion controller needs
    /// to collect before increasing its cwnd by `MAX_DATAGRAM_SIZE`.
    fn bytes_for_cwnd_increase(
        &mut self,
        curr_cwnd: usize,
        new_acked_bytes: usize,
        min_rtt: Duration,
        max_datagram_size: usize,
        now: Instant,
    ) -> usize;
    /// This function is called when a congestion event has been detected and it
    /// returns new (decreased) values of `curr_cwnd` and `acked_bytes`.
    /// This value can be very small; the calling code is responsible for ensuring that the
    /// congestion window doesn't drop below the minimum of `CWND_MIN`.
    fn reduce_cwnd(
        &mut self,
        curr_cwnd: usize,
        acked_bytes: usize,
        max_datagram_size: usize,
        congestion_trigger: CongestionTrigger,
        cc_stats: &mut CongestionControlStats,
    ) -> (usize, usize);
    /// Cubic needs this signal to reset its epoch.
    fn on_app_limited(&mut self);
    /// Store the current congestion controller state, to be recovered in the case of a spurious
    /// congestion event.
    fn save_undo_state(&mut self);

    /// Restore the previously stored congestion controller state, to recover from a spurious
    /// congestion event.
    fn restore_undo_state(&mut self, cc_stats: &mut CongestionControlStats);
}

/// Trait for slow start exit algorithms.
///
/// Implementations define when and if to exit from slow start, how the slow start threshold
/// (`ssthresh`) is set on exit and they can influence how fast the exponential congestion window
/// growth rate during slow start is.
pub trait SlowStart: Display + Debug {
    /// Enables a trait implementor to ingest info about sent packets.
    fn on_packet_sent(&mut self, _sent_pn: packet::Number, _sent_bytes: usize) {}

    /// This is needed by SEARCH to keep its cumulative byte counters in sync during app-limited
    /// periods, when [`SlowStart::on_packets_acked`] is not called.
    fn record_acked_bytes(&mut self, _new_acked_bytes: usize) {}

    /// Handle packets being acknowledged during slow start. Returns the congestion window in bytes
    /// that slow start should be exited with. If slow start isn't exited returns `None`.
    fn on_packets_acked(
        &mut self,
        rtt_est: &RttEstimate,
        largest_acked: packet::Number,
        curr_cwnd: usize,
        cc_stats: &mut CongestionControlStats,
        now: Instant,
    ) -> Option<usize>;

    /// Calculates the congestion window increase in bytes during slow start. The default
    /// implementation returns `new_acked`, i.e. classic exponential slow start growth.
    fn calc_cwnd_increase(&self, new_acked: usize, _max_datagram_size: usize) -> usize {
        new_acked
    }

    /// Resets slow start state. Is used after persistent congestion so slow start algorithms
    /// perform cleanly in non-initial slow starts. Can also be used by the implementing algorithm
    /// for internal state reset when needed.
    fn reset(&mut self) {}
}

#[derive(Debug)]
struct MaybeLostPacket {
    time_sent: Instant,
}

#[derive(Debug, Clone)]
struct State {
    phase: Phase,
    congestion_window: usize,
    acked_bytes: usize,
    ssthresh: Option<usize>,
    /// Packet number of the first packet that was sent after a congestion event. When this one is
    /// acked we will exit [`Phase::Recovery`] and enter [`Phase::CongestionAvoidance`].
    recovery_start: Option<packet::Number>,
}

impl Display for State {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "State [phase: {:?}, cwnd: {}, ssthresh: {:?}, recovery_start: {:?}]",
            self.phase, self.congestion_window, self.ssthresh, self.recovery_start
        )
    }
}

impl State {
    pub const fn new(mtu: usize) -> Self {
        Self {
            phase: Phase::SlowStart,
            congestion_window: cwnd_initial(mtu),
            acked_bytes: 0,
            ssthresh: None,
            recovery_start: None,
        }
    }
}

#[derive(Debug)]
pub struct ClassicCongestionController<S, T> {
    slow_start: S,
    congestion_control: T,
    bytes_in_flight: usize,
    /// Packets that have supposedly been lost. These are used for spurious congestion event
    /// detection. Gets drained when the same packets are later acked and regularly purged from too
    /// old packets in [`Self::cleanup_maybe_lost_packets`]. Needs a tuple of `(packet::Number,
    /// packet::Type)` to identify packets across packet number spaces.
    maybe_lost_packets: HashMap<(packet::Number, packet::Type), MaybeLostPacket>,
    /// `first_app_limited` indicates the packet number after which the application might be
    /// underutilizing the congestion window. When underutilizing the congestion window due to not
    /// sending out enough data, we SHOULD NOT increase the congestion window.[1] Packets sent
    /// before this point are deemed to fully utilize the congestion window and count towards
    /// increasing the congestion window.
    ///
    /// [1]: https://datatracker.ietf.org/doc/html/rfc9002#section-7.8
    first_app_limited: Option<packet::Number>,
    pmtud: Pmtud,
    qlog: Qlog,
    /// Current congestion controller parameters.
    current: State,
    /// Congestion controller parameters that were stored on a congestion event to restore prior
    /// state in case the congestion event turns out to be spurious.
    ///
    /// For reference:
    /// - [`State::acked_bytes`] is stored because that is where we accumulate our window increase
    ///   credit and it is also reduced on a congestion event.
    /// - [`Self::bytes_in_flight`] is not stored because if it was to be restored it might get
    ///   out-of-sync with the actual number of bytes-in-flight on the path.
    stored: Option<State>,
    /// Whether to recover from spurious congestion events by restoring prior state.
    spurious_recovery: bool,
}

impl<S: Display, T: Display> Display for ClassicCongestionController<S, T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{}/{} CongCtrl [bif: {}, {}]",
            self.slow_start, self.congestion_control, self.bytes_in_flight, self.current
        )
    }
}

impl<S, T> ClassicCongestionController<S, T> {
    pub const fn max_datagram_size(&self) -> usize {
        self.pmtud.plpmtu()
    }

    #[cfg(test)]
    pub const fn cwnd_initial(&self) -> usize {
        cwnd_initial(self.pmtud.plpmtu())
    }
}

impl<S, T> CongestionController for ClassicCongestionController<S, T>
where
    S: SlowStart,
    T: WindowAdjustment,
{
    fn set_qlog(&mut self, qlog: Qlog) {
        self.pmtud.set_qlog(qlog.clone());
        self.qlog = qlog;
    }

    fn cwnd(&self) -> usize {
        self.current.congestion_window
    }

    fn bytes_in_flight(&self) -> usize {
        self.bytes_in_flight
    }

    fn cwnd_avail(&self) -> usize {
        // BIF can be higher than cwnd due to PTO packets, which are sent even
        // if avail is 0, but still count towards BIF.
        self.current
            .congestion_window
            .saturating_sub(self.bytes_in_flight)
    }

    fn cwnd_min(&self) -> usize {
        self.max_datagram_size() * 2
    }

    fn pmtud(&self) -> &Pmtud {
        &self.pmtud
    }

    fn pmtud_mut(&mut self) -> &mut Pmtud {
        &mut self.pmtud
    }

    #[expect(
        clippy::too_many_lines,
        reason = "The main congestion control function contains a lot of logic."
    )]
    fn on_packets_acked(
        &mut self,
        acked_pkts: &[sent::Packet],
        rtt_est: &RttEstimate,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) {
        let mut is_app_limited = true;
        let mut new_acked = 0;
        let largest_packet_acked = acked_pkts
            .first()
            .expect("`acked_pkts.first().is_some()` is checked in `Loss::on_ack_received`");

        // Supplying `true` for `rtt_est.pto(true)` here is best effort not to have to track
        // `recovery::Loss::confirmed()` all the way down to the congestion controller. Having too
        // big a PTO does no harm here.
        self.cleanup_maybe_lost_packets(now, rtt_est.pto(true));

        self.detect_spurious_congestion_event(acked_pkts, cc_stats);

        for pkt in acked_pkts {
            trace!(
                "packet_acked this={self:p}, pn={}, ps={}, ignored={}, lost={}, rtt_est={rtt_est:?}",
                pkt.pn(),
                pkt.len(),
                i32::from(!pkt.cc_outstanding()),
                i32::from(pkt.lost()),
            );
            if !pkt.cc_outstanding() {
                continue;
            }
            if self.first_app_limited.is_some_and(|f| pkt.pn() < f) {
                is_app_limited = false;
            }
            // BIF is set to 0 on a path change, but in case that was because of a simple rebinding
            // event, we may still get ACKs for packets sent before the rebinding.
            self.bytes_in_flight = self.bytes_in_flight.saturating_sub(pkt.len());

            if !self.after_recovery_start(pkt) {
                // Do not increase congestion window for packets sent before
                // recovery last started.
                continue;
            }

            if self.current.phase.in_recovery() {
                self.set_phase(Phase::CongestionAvoidance, None, now);
            }

            new_acked += pkt.len();
        }

        if self.current.phase.in_slow_start() {
            self.slow_start.record_acked_bytes(new_acked);
        }

        if is_app_limited {
            self.congestion_control.on_app_limited();
            debug!(
                "on_packets_acked this={self:p}, limited=1, bytes_in_flight={}, cwnd={}, phase={:?}, new_acked={new_acked}",
                self.bytes_in_flight, self.current.congestion_window, self.current.phase
            );
            qlog::metrics_updated(
                &mut self.qlog,
                [qlog::Metric::BytesInFlight(self.bytes_in_flight)],
                now,
            );
            return;
        }

        // Slow start: grow up to ssthresh.
        if self
            .current
            .ssthresh
            .is_none_or(|s| self.current.congestion_window < s)
        {
            // Check if the slow start algorithm wants to exit.
            if let Some(exit_cwnd) = self.slow_start.on_packets_acked(
                rtt_est,
                largest_packet_acked.pn(),
                self.current.congestion_window,
                cc_stats,
                now,
            ) {
                debug!("Exited slow start by algorithm");
                cc_stats.slow_start_exit = Some(SlowStartExitStats {
                    reason: SlowStartExitReason::Heuristic,
                    detection_cwnd: self.current.congestion_window,
                    exit_cwnd,
                    bytes_in_flight: self.bytes_in_flight,
                });
                self.current.congestion_window = exit_cwnd;
                self.current.ssthresh = Some(exit_cwnd);
                self.set_phase(Phase::CongestionAvoidance, None, now);
            } else {
                let cwnd_increase = self
                    .slow_start
                    .calc_cwnd_increase(new_acked, self.max_datagram_size());
                self.current.congestion_window += cwnd_increase;
                trace!("[{self}] slow start += {cwnd_increase}");

                // This can only happen after persistent congestion when we re-enter slow start
                // while having a previously established `ssthresh` which has now
                // been reached.
                if let Some(ssthresh) = self.current.ssthresh
                    && self.current.congestion_window >= ssthresh
                {
                    debug!(
                        "Exited slow start because the threshold was reached, ssthresh: {ssthresh}",
                    );
                    // Clamp congestion window to ssthresh.
                    self.current.congestion_window = ssthresh;
                    self.set_phase(Phase::CongestionAvoidance, None, now);
                }
            }
        }

        // Congestion avoidance, above the slow start threshold.
        if self
            .current
            .ssthresh
            .is_some_and(|s| self.current.congestion_window >= s)
        {
            // The following function returns the amount of acked bytes a controller
            // needs to collect to be allowed to increase its cwnd by one datagram.
            let bytes_for_increase = self.congestion_control.bytes_for_cwnd_increase(
                self.current.congestion_window,
                new_acked,
                rtt_est.minimum(),
                self.max_datagram_size(),
                now,
            );
            debug_assert!(bytes_for_increase > 0);
            // Cap prior credit to one increment (in case bytes_for_increase dropped between
            // calls), then apply all increments earned, allowing >1 MSS of growth when a large
            // burst is acknowledged (RFC 9002 has no 2-MSS cap; RFC 3465 §2.3 is TCP-only).
            let acked = min(self.current.acked_bytes, bytes_for_increase) + new_acked;
            let n = acked / bytes_for_increase;
            self.current.acked_bytes = acked % bytes_for_increase;
            self.current.congestion_window += n * self.max_datagram_size();
        }

        qlog::metrics_updated(
            &mut self.qlog,
            [
                qlog::Metric::CongestionWindow(self.current.congestion_window),
                qlog::Metric::BytesInFlight(self.bytes_in_flight),
            ]
            .into_iter()
            .chain(self.current.ssthresh.map(qlog::Metric::SsThresh)),
            now,
        );

        debug!(
            "[{self}] on_packets_acked this={self:p}, limited=0, bytes_in_flight={}, cwnd={}, phase={:?}, new_acked={new_acked}",
            self.bytes_in_flight, self.current.congestion_window, self.current.phase
        );
    }

    /// Update congestion controller state based on lost packets.
    fn on_packets_lost(
        &mut self,
        first_rtt_sample_time: Option<Instant>,
        prev_largest_acked_sent: Option<Instant>,
        pto: Duration,
        lost_packets: &[sent::Packet],
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool {
        if lost_packets.is_empty() {
            return false;
        }

        let bif_at_detection = self.bytes_in_flight;
        let maybe_lost_before = self.maybe_lost_packets.len();

        for pkt in lost_packets {
            if pkt.cc_in_flight() {
                debug!(
                    "packet_lost this={self:p}, pn={}, ps={}",
                    pkt.pn(),
                    pkt.len()
                );
                // bytes_in_flight is set to 0 on a path change, but in case that was because of a
                // simple rebinding event, we may still declare packets lost that
                // were sent before the rebinding.
                self.bytes_in_flight = self.bytes_in_flight.saturating_sub(pkt.len());
                if !pkt.is_pmtud_probe() {
                    let present = self.maybe_lost_packets.insert(
                        (pkt.pn(), pkt.packet_type()),
                        MaybeLostPacket {
                            time_sent: pkt.time_sent(),
                        },
                    );
                    debug!(
                        "Spurious detection: added MaybeLostPacket: pn {}, type {:?}, time_sent {:?}",
                        pkt.pn(),
                        pkt.packet_type(),
                        pkt.time_sent()
                    );
                    debug_assert!(present.is_none());
                }
            }
        }

        let lost_packets_count = self.maybe_lost_packets.len() - maybe_lost_before;

        qlog::metrics_updated(
            &mut self.qlog,
            [qlog::Metric::BytesInFlight(self.bytes_in_flight)],
            now,
        );

        // Lost PMTUD probes do not elicit congestion control reactions, so this closure filters
        // them out.
        let lost_packets_no_pmtud = || lost_packets.iter().filter(|pkt| !pkt.is_pmtud_probe());

        // Packets not in flight don't elicit congestion control reactions either, so we early
        // return if there is no lost in-flight packet left.
        let Some(last_lost_packet) = lost_packets_no_pmtud().rfind(|pkt| pkt.cc_in_flight()) else {
            return false;
        };

        let congestion = self.on_congestion_event(
            last_lost_packet,
            Loss(lost_packets_count),
            bif_at_detection,
            now,
            cc_stats,
        );
        // Persistent congestion checks still need to see lost packets that are not in-flight for
        // continuity checks. That is why only the closure to filter out lost PMTUD probes is used.
        let persistent_congestion = self.detect_persistent_congestion(
            first_rtt_sample_time,
            prev_largest_acked_sent,
            pto,
            lost_packets_no_pmtud(),
            now,
        );
        debug!(
            "on_packets_lost this={self:p}, bytes_in_flight={}, cwnd={}, phase={:?}",
            self.bytes_in_flight, self.current.congestion_window, self.current.phase
        );
        congestion || persistent_congestion
    }

    /// Report received ECN CE mark(s) to the congestion controller as a
    /// congestion event.
    ///
    /// See <https://datatracker.ietf.org/doc/html/rfc9002#section-b.7>.
    fn on_ecn_ce_received(
        &mut self,
        largest_acked_pkt: &sent::Packet,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool {
        self.on_congestion_event(largest_acked_pkt, Ecn, self.bytes_in_flight, now, cc_stats)
    }

    fn discard(&mut self, pkt: &sent::Packet, now: Instant) {
        if pkt.cc_outstanding() {
            assert!(self.bytes_in_flight >= pkt.len());
            self.bytes_in_flight -= pkt.len();
            qlog::metrics_updated(
                &mut self.qlog,
                [qlog::Metric::BytesInFlight(self.bytes_in_flight)],
                now,
            );
            trace!("[{self}] Ignore pkt with size {}", pkt.len());
        }
    }

    fn discard_in_flight(&mut self, now: Instant) {
        self.bytes_in_flight = 0;
        qlog::metrics_updated(
            &mut self.qlog,
            [qlog::Metric::BytesInFlight(self.bytes_in_flight)],
            now,
        );
    }

    fn on_packet_sent(&mut self, pkt: &sent::Packet, now: Instant) {
        // Record the recovery time and exit any transient phase.
        if self.current.phase.transient() {
            self.current.recovery_start = Some(pkt.pn());
            debug!("set recovery_start to pn={}", pkt.pn());
            self.current.phase.update();
        }

        if !pkt.cc_in_flight() {
            return;
        }

        // Pass next packet number to send into slow start algorithm during slow start.
        if self.current.phase.in_slow_start() {
            self.slow_start.on_packet_sent(pkt.pn(), pkt.len());
        }

        // Update bytes in flight first; a fully consumed congestion window is not app-limited.
        self.bytes_in_flight += pkt.len();

        if !self.app_limited() {
            // Given the current non-app-limited condition, we're fully utilizing the congestion
            // window. Assume that all in-flight packets up to this one are NOT app-limited.
            // However, subsequent packets might be app-limited. Set `first_app_limited` to the
            // next packet number.
            self.first_app_limited = Some(pkt.pn() + 1);
        }

        debug!(
            "packet_sent this={self:p}, pn={}, ps={}",
            pkt.pn(),
            pkt.len()
        );
        qlog::metrics_updated(
            &mut self.qlog,
            [qlog::Metric::BytesInFlight(self.bytes_in_flight)],
            now,
        );
    }

    /// Whether a packet can be sent immediately as a result of entering recovery.
    fn recovery_packet(&self) -> bool {
        self.current.phase == Phase::RecoveryStart
    }
}

const fn cwnd_initial(mtu: usize) -> usize {
    const_min(CWND_INITIAL_PKTS * mtu, const_max(2 * mtu, 14_720))
}

impl<S, T> ClassicCongestionController<S, T>
where
    S: SlowStart,
    T: WindowAdjustment,
{
    pub fn new(
        slow_start: S,
        congestion_control: T,
        pmtud: Pmtud,
        spurious_recovery: bool,
    ) -> Self {
        let mtu = pmtud.plpmtu();
        Self {
            slow_start,
            congestion_control,
            bytes_in_flight: 0,
            maybe_lost_packets: HashMap::default(),
            qlog: Qlog::disabled(),
            first_app_limited: None,
            pmtud,
            current: State::new(mtu),
            stored: None,
            spurious_recovery,
        }
    }

    #[cfg(test)]
    #[must_use]
    pub const fn ssthresh(&self) -> Option<usize> {
        self.current.ssthresh
    }

    #[cfg(test)]
    pub const fn set_ssthresh(&mut self, v: usize) {
        self.current.ssthresh = Some(v);
    }

    /// Accessor for [`ClassicCongestionController::congestion_control`]. Is used to call Cubic
    /// getters in tests.
    #[cfg(test)]
    pub const fn congestion_control(&self) -> &T {
        &self.congestion_control
    }

    /// Mutable accessor for [`ClassicCongestionController::congestion_control`]. Is used to call
    /// Cubic setters in tests.
    #[cfg(test)]
    pub const fn congestion_control_mut(&mut self) -> &mut T {
        &mut self.congestion_control
    }

    #[cfg(test)]
    pub const fn acked_bytes(&self) -> usize {
        self.current.acked_bytes
    }

    fn set_phase(
        &mut self,
        phase: Phase,
        trigger: Option<qlog::CongestionStateTrigger>,
        now: Instant,
    ) {
        if self.current.phase == phase {
            return;
        }
        debug!("[{self}] phase -> {phase:?}");
        let old_state = self.current.phase;
        // Only emit a qlog event when a transition changes the qlog state.
        if !str::eq(old_state.into(), phase.into()) {
            qlog::congestion_state_updated(
                &mut self.qlog,
                Some(old_state.into()),
                phase.into(),
                trigger,
                now,
            );
        }
        self.current.phase = phase;
    }

    // NOTE: Maybe do tracking of lost packets per congestion epoch. Right now if we get a spurious
    // event and then before the first was recovered get another (or even a real congestion event
    // because of random loss, path change, ...), it will only be detected as spurious once the old
    // and new lost packets are recovered. This means we'd have two spurious events counted as one
    // and would also only be able to recover to the cwnd prior to the second event.
    fn detect_spurious_congestion_event(
        &mut self,
        acked_packets: &[sent::Packet],
        cc_stats: &mut CongestionControlStats,
    ) {
        if self.maybe_lost_packets.is_empty() {
            return;
        }

        // Removes all newly acked packets that are late acks from `maybe_lost_packets`.
        for acked_packet in acked_packets {
            if self
                .maybe_lost_packets
                .remove(&(acked_packet.pn(), acked_packet.packet_type()))
                .is_some()
            {
                debug!(
                    "Spurious detection: removed MaybeLostPacket with pn {}, type {:?}",
                    acked_packet.pn(),
                    acked_packet.packet_type(),
                );
            }
        }

        // If all of them have been removed we detected a spurious congestion event.
        if self.maybe_lost_packets.is_empty() {
            debug!(
                "Spurious detection: maybe_lost_packets emptied -> calling on_spurious_congestion_event"
            );
            self.on_spurious_congestion_event(cc_stats);
        }
    }

    /// Cleanup lost packets that we are fairly sure will never be getting a late acknowledgment
    /// for.
    fn cleanup_maybe_lost_packets(&mut self, now: Instant, pto: Duration) {
        // The `pto * 2` maximum age of the lost packets is taken from msquic's implementation:
        // <https://github.com/microsoft/msquic/blob/2623c07df62b4bd171f469fb29c2714b6735b676/src/core/loss_detection.c#L939-L943>
        let max_age = pto * 2;
        for ((pn, pt), _) in self
            .maybe_lost_packets
            .extract_if(|_, packet| now.saturating_duration_since(packet.time_sent) > max_age)
        {
            debug!("Spurious detection: cleaned up old MaybeLostPacket with pn {pn}, type {pt:?}");
        }
    }

    fn on_spurious_congestion_event(&mut self, cc_stats: &mut CongestionControlStats) {
        let Some(stored) = self.stored.take() else {
            debug!("[{self}] Spurious cong event -> ABORT, no stored params to restore available.");
            return;
        };

        // The stat is recorded for all cases below.
        cc_stats.congestion_events.spurious += 1;

        if stored.congestion_window <= self.current.congestion_window {
            info!(
                "[{self}] Spurious cong event -> IGNORED because stored.cwnd {} < self.cwnd {};",
                stored.congestion_window, self.current.congestion_window
            );
            return;
        }

        if !self.spurious_recovery {
            info!("[{self}] Spurious cong event detected -> recovery disabled;");
            return;
        }

        self.congestion_control.restore_undo_state(cc_stats);
        debug!(
            "Spurious cong event: recovering cc params from {} to {stored}",
            self.current
        );
        self.current = stored;

        // If we are restoring back to slow start then we should undo the stat recording.
        if self.current.phase.in_slow_start() {
            cc_stats.slow_start_exit = None;
        }
        info!("[{self}] Spurious cong event -> RESTORED;");
    }

    fn detect_persistent_congestion<'a>(
        &mut self,
        first_rtt_sample_time: Option<Instant>,
        prev_largest_acked_sent: Option<Instant>,
        pto: Duration,
        lost_packets: impl IntoIterator<Item = &'a sent::Packet>,
        now: Instant,
    ) -> bool {
        if first_rtt_sample_time.is_none() {
            return false;
        }

        let pc_period = pto * PERSISTENT_CONG_THRESH;

        let mut last_pn: Option<packet::Number> = None;
        let mut start = None;

        // Look for the first lost packet after the previous largest acknowledged.
        // Ignore packets that weren't ack-eliciting for the start of this range.
        // Also, make sure to ignore any packets sent before we got an RTT estimate
        // as we might not have sent PTO packets soon enough after those.
        let cutoff = max(first_rtt_sample_time, prev_largest_acked_sent);
        for p in lost_packets
            .into_iter()
            .skip_while(|p| Some(p.time_sent()) < cutoff)
        {
            if last_pn.is_none_or(|l| p.pn() != l + 1) {
                // Not a contiguous range of lost packets, start over.
                start = None;
            }
            last_pn = Some(p.pn());
            if !p.ack_eliciting() || !p.on_primary_path() {
                // Not interesting, keep looking.
                continue;
            }
            if let Some(t) = start {
                let elapsed = p
                    .time_sent()
                    .checked_duration_since(t)
                    .expect("time is monotonic");
                if elapsed > pc_period {
                    info!("[{self}] persistent congestion");
                    self.current.congestion_window = self.cwnd_min();
                    self.current.acked_bytes = 0;
                    self.set_phase(
                        Phase::PersistentCongestion,
                        Some(qlog::CongestionStateTrigger::PersistentCongestion),
                        now,
                    );
                    // We re-enter slow start after persistent congestion, so we need to reset any
                    // state leftover from initial slow start to have it perform correctly.
                    self.slow_start.reset();

                    qlog::metrics_updated(
                        &mut self.qlog,
                        [
                            Some(qlog::Metric::CongestionWindow(
                                self.current.congestion_window,
                            )),
                            self.current.ssthresh.map(qlog::Metric::SsThresh),
                        ]
                        .into_iter()
                        .flatten(),
                        now,
                    );

                    return true;
                }
            } else {
                start = Some(p.time_sent());
            }
        }
        false
    }

    #[must_use]
    fn after_recovery_start(&self, packet: &sent::Packet) -> bool {
        // At the start of the recovery period, the phase is transient and
        // all packets will have been sent before recovery. When sending out
        // the first packet we transition to the non-transient `Recovery`
        // phase and update the variable `self.recovery_start`. Before the
        // first recovery, all packets were sent after the recovery event,
        // allowing to reduce the cwnd on congestion events.
        !self.current.phase.transient()
            && self
                .current
                .recovery_start
                .is_none_or(|pn| packet.pn() >= pn)
    }

    /// Handle a congestion event.
    /// Returns true if this was a true congestion event.
    fn on_congestion_event(
        &mut self,
        last_packet: &sent::Packet,
        congestion_trigger: CongestionTrigger,
        bif_at_detection: usize,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool {
        // Start a new congestion event if lost or ECN CE marked packet was sent
        // after the start of the previous congestion recovery period.
        if !self.after_recovery_start(last_packet) {
            debug!(
                "Called on_congestion_event during recovery -> don't react; last_packet {}, recovery_start {}",
                last_packet.pn(),
                self.current.recovery_start.unwrap_or(0)
            );
            return false;
        }

        if congestion_trigger != Ecn {
            self.stored = Some(self.current.clone());
            self.congestion_control.save_undo_state();
        }

        let detection_cwnd = self.current.congestion_window;
        let (cwnd, acked_bytes) = self.congestion_control.reduce_cwnd(
            self.current.congestion_window,
            self.current.acked_bytes,
            self.max_datagram_size(),
            congestion_trigger,
            cc_stats,
        );
        self.current.congestion_window = max(cwnd, self.cwnd_min());
        self.current.acked_bytes = acked_bytes;
        self.current.ssthresh = Some(self.current.congestion_window);
        info!(
            "[{self}] Cong event -> recovery; cwnd {}, ssthresh {:?}",
            self.current.congestion_window, self.current.ssthresh
        );

        match congestion_trigger {
            Loss(_) => cc_stats.congestion_events.loss += 1,
            Ecn => cc_stats.congestion_events.ecn += 1,
        }
        // If we were in slow start when `on_congestion_event` was called we will exit slow start
        // and should record the exit stats.
        if self.current.phase.in_slow_start() {
            cc_stats.slow_start_exit = Some(SlowStartExitStats {
                reason: SlowStartExitReason::CongestionEvent(congestion_trigger),
                detection_cwnd,
                exit_cwnd: self.current.congestion_window,
                bytes_in_flight: bif_at_detection,
            });
        }

        qlog::metrics_updated(
            &mut self.qlog,
            [
                Some(qlog::Metric::CongestionWindow(
                    self.current.congestion_window,
                )),
                self.current.ssthresh.map(qlog::Metric::SsThresh),
            ]
            .into_iter()
            .flatten(),
            now,
        );
        let trigger = (congestion_trigger == Ecn).then_some(qlog::CongestionStateTrigger::Ecn);
        self.set_phase(Phase::RecoveryStart, trigger, now);
        true
    }

    fn app_limited(&self) -> bool {
        if self.bytes_in_flight >= self.current.congestion_window {
            false
        } else if self.current.phase.in_slow_start() {
            // Allow for potential doubling of the congestion window during slow start.
            // That is, the application might not have been able to send enough to respond
            // to increases to the congestion window.
            self.bytes_in_flight < self.current.congestion_window / 2
        } else {
            // We're not limited if the in-flight data is within a single burst of the
            // congestion window.
            (self.bytes_in_flight + self.max_datagram_size() * PACING_BURST_SIZE)
                < self.current.congestion_window
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::time::{Duration, Instant};

    use neqo_common::to_u64;
    use test_fixture::{new_neqo_qlog, now};
    use tracing::info;

    use super::{
        ClassicCongestionController, PACING_BURST_SIZE, PERSISTENT_CONG_THRESH, SlowStart,
        WindowAdjustment,
    };
    use crate::{
        MIN_INITIAL_PACKET_SIZE,
        cc::{
            CWND_INITIAL_PKTS, ClassicSlowStart, CongestionController,
            CongestionTrigger::{self, Ecn, Loss},
            classic_cc::Phase,
            cubic::Cubic,
            new_reno::NewReno,
            tests::{RTT, make_cc_cubic, make_cc_hystart, make_cc_newreno, pmtud},
        },
        packet,
        recovery::{self, sent},
        rtt::RttEstimate,
        stats::{CongestionControlStats, SlowStartExitReason},
    };

    const PTO: Duration = RTT;
    const ZERO: Duration = Duration::from_secs(0);
    const EPSILON: Duration = Duration::from_nanos(1);
    const GAP: Duration = Duration::from_secs(1);
    /// The largest time between packets without causing persistent congestion.
    const SUB_PC: Duration = Duration::from_millis(100 * to_u64(PERSISTENT_CONG_THRESH as usize));
    /// The minimum time between packets to cause persistent congestion.
    /// Uses an odd expression because `Duration` arithmetic isn't `const`.
    const PC: Duration =
        Duration::from_nanos(100_000_000 * to_u64(PERSISTENT_CONG_THRESH as usize) + 1);

    fn cwnd_is_default(cc: &ClassicCongestionController<ClassicSlowStart, NewReno>) {
        assert_eq!(cc.cwnd(), cc.cwnd_initial());
        assert_eq!(cc.ssthresh(), None);
    }

    fn cwnd_is_halved(cc: &ClassicCongestionController<ClassicSlowStart, NewReno>) {
        assert_eq!(cc.cwnd(), cc.cwnd_initial() / 2);
        assert_eq!(cc.ssthresh(), Some(cc.cwnd_initial() / 2));
    }

    fn lost(pn: packet::Number, ack_eliciting: bool, t: Duration) -> sent::Packet {
        sent::Packet::new(
            packet::Type::Short,
            pn,
            now() + t,
            ack_eliciting,
            recovery::Tokens::new(),
            100,
        )
    }

    fn persistent_congestion_by_algorithm(
        mut cc: impl CongestionController,
        reduced_cwnd: usize,
        lost_packets: &[sent::Packet],
        persistent_expected: bool,
    ) {
        let mut cc_stats = CongestionControlStats::default();

        for p in lost_packets {
            cc.on_packet_sent(p, now());
        }

        cc.on_packets_lost(Some(now()), None, PTO, lost_packets, now(), &mut cc_stats);

        let persistent = if cc.cwnd() == reduced_cwnd {
            false
        } else if cc.cwnd() == cc.cwnd_min() {
            true
        } else {
            panic!("unexpected cwnd");
        };
        assert_eq!(persistent, persistent_expected);
    }

    fn persistent_congestion(lost_packets: &[sent::Packet], persistent_expected: bool) {
        let cc = make_cc_newreno();
        let cwnd_initial = cc.cwnd_initial();
        persistent_congestion_by_algorithm(cc, cwnd_initial / 2, lost_packets, persistent_expected);

        let cc = make_cc_cubic();
        let cwnd_initial = cc.cwnd_initial();
        persistent_congestion_by_algorithm(
            cc,
            cwnd_initial * Cubic::BETA_USIZE_DIVIDEND / Cubic::BETA_USIZE_DIVISOR,
            lost_packets,
            persistent_expected,
        );
    }

    /// A span of exactly the PC threshold only reduces the window on loss.
    #[test]
    fn persistent_congestion_none() {
        persistent_congestion(&[lost(1, true, ZERO), lost(2, true, SUB_PC)], false);
    }

    /// A span of just more than the PC threshold causes persistent congestion.
    #[test]
    fn persistent_congestion_simple() {
        persistent_congestion(&[lost(1, true, ZERO), lost(2, true, PC)], true);
    }

    /// Both packets need to be ack-eliciting.
    #[test]
    fn persistent_congestion_non_ack_eliciting() {
        persistent_congestion(&[lost(1, false, ZERO), lost(2, true, PC)], false);
        persistent_congestion(&[lost(1, true, ZERO), lost(2, false, PC)], false);
    }

    /// Padding doesn't make packets ack-eliciting.
    #[test]
    fn persistent_congestion_padded_non_ack_eliciting() {
        let padded = |pn, t| lost(pn, false, t).with_padding(true);
        persistent_congestion(&[padded(1, ZERO), lost(2, true, PC)], false);
        persistent_congestion(&[lost(1, true, ZERO), padded(2, PC)], false);
    }

    /// Packets in the middle, of any type, are OK.
    #[test]
    fn persistent_congestion_middle() {
        persistent_congestion(
            &[lost(1, true, ZERO), lost(2, false, RTT), lost(3, true, PC)],
            true,
        );
        persistent_congestion(
            &[lost(1, true, ZERO), lost(2, true, RTT), lost(3, true, PC)],
            true,
        );
    }

    /// Leading non-ack-eliciting packets are skipped.
    #[test]
    fn persistent_congestion_leading_non_ack_eliciting() {
        persistent_congestion(
            &[lost(1, false, ZERO), lost(2, true, RTT), lost(3, true, PC)],
            false,
        );
        persistent_congestion(
            &[
                lost(1, false, ZERO),
                lost(2, true, RTT),
                lost(3, true, RTT + PC),
            ],
            true,
        );
    }

    /// Trailing non-ack-eliciting packets aren't relevant.
    #[test]
    fn persistent_congestion_trailing_non_ack_eliciting() {
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PC),
                lost(3, false, PC + EPSILON),
            ],
            true,
        );
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, SUB_PC),
                lost(3, false, PC),
            ],
            false,
        );
    }

    /// Gaps in the middle, of any type, restart the count.
    #[test]
    fn persistent_congestion_gap_reset() {
        persistent_congestion(&[lost(1, true, ZERO), lost(3, true, PC)], false);
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, RTT),
                lost(4, true, GAP),
                lost(5, true, GAP + PTO * PERSISTENT_CONG_THRESH),
            ],
            false,
        );
    }

    /// A span either side of a gap will cause persistent congestion.
    #[test]
    fn persistent_congestion_gap_or() {
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PC),
                lost(4, true, GAP),
                lost(5, true, GAP + PTO),
            ],
            true,
        );
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PTO),
                lost(4, true, GAP),
                lost(5, true, GAP + PC),
            ],
            true,
        );
    }

    /// A gap only restarts after an ack-eliciting packet.
    #[test]
    fn persistent_congestion_gap_non_ack_eliciting() {
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PTO),
                lost(4, false, GAP),
                lost(5, true, GAP + PC),
            ],
            false,
        );
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PTO),
                lost(4, false, GAP),
                lost(5, true, GAP + RTT),
                lost(6, true, GAP + RTT + SUB_PC),
            ],
            false,
        );
        persistent_congestion(
            &[
                lost(1, true, ZERO),
                lost(2, true, PTO),
                lost(4, false, GAP),
                lost(5, true, GAP + RTT),
                lost(6, true, GAP + RTT + PC),
            ],
            true,
        );
    }

    /// Get a time, in multiples of `PTO`, relative to `now()`.
    fn by_pto(t: u32) -> Instant {
        now() + (PTO * t)
    }

    /// Make packets that will be made lost.
    /// `times` is the time of sending, in multiples of `PTO`, relative to `now()`.
    fn make_lost(times: &[u32]) -> Vec<sent::Packet> {
        times
            .iter()
            .enumerate()
            .map(|(i, &t)| {
                sent::Packet::new(
                    packet::Type::Short,
                    to_u64(i),
                    by_pto(t),
                    true,
                    recovery::Tokens::new(),
                    1000,
                )
            })
            .collect::<Vec<_>>()
    }

    /// Call `detect_persistent_congestion` using times relative to now and the fixed PTO time.
    /// `last_ack` and `rtt_time` are times in multiples of `PTO`, relative to `now()`,
    /// for the time of the largest acknowledged and the first RTT sample, respectively.
    fn persistent_congestion_by_pto<S: SlowStart, T: WindowAdjustment>(
        mut cc: ClassicCongestionController<S, T>,
        last_ack: u32,
        rtt_time: u32,
        lost: &[sent::Packet],
    ) -> bool {
        let now = now();
        assert_eq!(cc.cwnd(), cc.cwnd_initial());

        let last_ack = Some(by_pto(last_ack));
        let rtt_time = Some(by_pto(rtt_time));

        // Persistent congestion is never declared if the RTT time is `None`.
        cc.detect_persistent_congestion(None, None, PTO, lost.iter(), now);
        assert_eq!(cc.cwnd(), cc.cwnd_initial());
        cc.detect_persistent_congestion(None, last_ack, PTO, lost.iter(), now);
        assert_eq!(cc.cwnd(), cc.cwnd_initial());

        cc.detect_persistent_congestion(rtt_time, last_ack, PTO, lost.iter(), now);
        cc.cwnd() == cc.cwnd_min()
    }

    /// No persistent congestion can be had if there are no lost packets.
    #[test]
    fn persistent_congestion_no_lost() {
        let lost = make_lost(&[]);
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            0,
            0,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 0, 0, &lost));
    }

    /// No persistent congestion can be had if there is only one lost packet.
    #[test]
    fn persistent_congestion_one_lost() {
        let lost = make_lost(&[1]);
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            0,
            0,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 0, 0, &lost));
    }

    /// Persistent congestion can't happen based on old packets.
    #[test]
    fn persistent_congestion_past() {
        // Packets sent prior to either the last acknowledged or the first RTT
        // sample are not considered.  So 0 is ignored.
        let lost = make_lost(&[0, PERSISTENT_CONG_THRESH + 1, PERSISTENT_CONG_THRESH + 2]);
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            1,
            1,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            0,
            1,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            1,
            0,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 1, 1, &lost));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 0, 1, &lost));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 1, 0, &lost));
    }

    /// Persistent congestion doesn't start unless the packet is ack-eliciting.
    #[test]
    fn persistent_congestion_ack_eliciting() {
        let mut lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        lost[0] = sent::Packet::new(
            lost[0].packet_type(),
            lost[0].pn(),
            lost[0].time_sent(),
            false,
            lost[0].tokens().clone(),
            lost[0].len(),
        );
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            0,
            0,
            &lost
        ));
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 0, 0, &lost));
    }

    /// Detect persistent congestion.  Note that the first lost packet needs to have a time
    /// greater than the previously acknowledged packet AND the first RTT sample.  And the
    /// difference in times needs to be greater than the persistent congestion threshold.
    #[test]
    fn persistent_congestion_min() {
        let lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        assert!(persistent_congestion_by_pto(make_cc_newreno(), 0, 0, &lost));
        assert!(persistent_congestion_by_pto(make_cc_cubic(), 0, 0, &lost));
    }

    /// Make sure that not having a previous largest acknowledged also results
    /// in detecting persistent congestion.  (This is not expected to happen, but
    /// the code permits it).
    #[test]
    fn persistent_congestion_no_prev_ack_newreno() {
        let lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        let mut cc = make_cc_newreno();
        cc.detect_persistent_congestion(Some(by_pto(0)), None, PTO, lost.iter(), now());
        assert_eq!(cc.cwnd(), cc.cwnd_min());
    }

    #[test]
    fn persistent_congestion_no_prev_ack_cubic() {
        let lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        let mut cc = make_cc_cubic();
        cc.detect_persistent_congestion(Some(by_pto(0)), None, PTO, lost.iter(), now());
        assert_eq!(cc.cwnd(), cc.cwnd_min());
    }

    /// The code asserts on ordering errors.
    #[test]
    #[should_panic(expected = "time is monotonic")]
    fn persistent_congestion_unsorted_newreno() {
        let lost = make_lost(&[PERSISTENT_CONG_THRESH + 2, 1]);
        assert!(!persistent_congestion_by_pto(
            make_cc_newreno(),
            0,
            0,
            &lost
        ));
    }

    /// The code asserts on ordering errors.
    #[test]
    #[should_panic(expected = "time is monotonic")]
    fn persistent_congestion_unsorted_cubic() {
        let lost = make_lost(&[PERSISTENT_CONG_THRESH + 2, 1]);
        assert!(!persistent_congestion_by_pto(make_cc_cubic(), 0, 0, &lost));
    }

    #[test]
    fn app_limited_slow_start() {
        // The threshold is half the window, so the largest app-limited burst is one short.
        const BELOW_APP_LIMIT_PKTS: usize = CWND_INITIAL_PKTS / 2 - 1;
        const ABOVE_APP_LIMIT_PKTS: usize = BELOW_APP_LIMIT_PKTS + 1;
        let mut cc = make_cc_newreno();
        let cwnd = cc.current.congestion_window;
        let mut now = now();
        let mut next_pn = 0;
        let mut cc_stats = CongestionControlStats::default();

        // simulate packet bursts below app_limit
        for packet_burst_size in 1..=BELOW_APP_LIMIT_PKTS {
            // always stay below app_limit during sent.
            let mut pkts = Vec::new();
            for _ in 0..packet_burst_size {
                let p = sent::make_packet(next_pn, now, cc.max_datagram_size());
                next_pn += 1;
                cc.on_packet_sent(&p, now);
                pkts.push(p);
            }
            assert_eq!(
                cc.bytes_in_flight(),
                packet_burst_size * cc.max_datagram_size()
            );
            assert!(cc.app_limited(), "burst must stay below the threshold");
            now += RTT;
            cc.on_packets_acked(
                &pkts,
                &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
                now,
                &mut cc_stats,
            );
            assert_eq!(cc.bytes_in_flight(), 0);
            assert_eq!(cc.acked_bytes(), 0);
            // CWND doesn't grow because we're app-limited.
            assert_eq!(cwnd, cc.current.congestion_window);
        }

        // Fully utilize the congestion window by sending enough packets to
        // have `bytes_in_flight` above the `app_limited` threshold.
        let mut pkts = Vec::new();
        for _ in 0..ABOVE_APP_LIMIT_PKTS {
            let p = sent::make_packet(next_pn, now, cc.max_datagram_size());
            next_pn += 1;
            cc.on_packet_sent(&p, now);
            pkts.push(p);
        }
        assert_eq!(
            cc.bytes_in_flight(),
            ABOVE_APP_LIMIT_PKTS * cc.max_datagram_size()
        );
        assert!(!cc.app_limited(), "burst must fill `cwnd`");
        now += RTT;
        // Check if congestion window gets increased for all packets currently in flight
        for (i, pkt) in pkts.into_iter().enumerate() {
            cc.on_packets_acked(
                &[pkt],
                &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
                now,
                &mut cc_stats,
            );

            assert_eq!(
                cc.bytes_in_flight(),
                (ABOVE_APP_LIMIT_PKTS - i - 1) * cc.max_datagram_size()
            );
            // increase acked_bytes with each packet
            info!(
                "{} {}",
                cc.current.congestion_window,
                cwnd + i * cc.max_datagram_size()
            );
            assert_eq!(
                cc.current.congestion_window,
                cwnd + (i + 1) * cc.max_datagram_size()
            );
            assert_eq!(cc.acked_bytes(), 0);
        }
    }

    /// Filling `cwnd` is congestion-limited, not app-limited, so `cwnd` must still grow.
    #[test]
    fn not_app_limited_when_one_packet_fills_cwnd() {
        let mut cc = make_cc_newreno();
        let cwnd = cc.cwnd();
        let mut cc_stats = CongestionControlStats::default();
        let now = now();

        // Fill `cwnd` in a single send.
        let p = sent::make_packet(0, now, cwnd);
        cc.on_packet_sent(&p, now);
        assert_eq!(cc.bytes_in_flight(), cwnd);

        cc.on_packets_acked(
            &[p],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now + RTT,
            &mut cc_stats,
        );

        assert_eq!(cc.bytes_in_flight(), 0);
        assert_eq!(cc.cwnd(), 2 * cwnd, "cwnd must double when filled");
    }

    #[test]
    fn app_limited_congestion_avoidance() {
        const CWND_PKTS_CA: usize = CWND_INITIAL_PKTS / 2;
        // The threshold adds a pacing burst, so the largest app-limited burst is one short.
        const BELOW_APP_LIMIT_PKTS: usize = CWND_PKTS_CA - PACING_BURST_SIZE - 1;
        const ABOVE_APP_LIMIT_PKTS: usize = BELOW_APP_LIMIT_PKTS + 1;

        let mut cc = make_cc_newreno();
        let mut now = now();
        let mut cc_stats = CongestionControlStats::default();

        // Change phase to congestion avoidance by introducing loss.

        let p_lost = sent::make_packet(1, now, cc.max_datagram_size());
        cc.on_packet_sent(&p_lost, now);
        cwnd_is_default(&cc);
        now += PTO;
        cc.on_packets_lost(Some(now), None, PTO, &[p_lost], now, &mut cc_stats);
        cwnd_is_halved(&cc);
        let p_not_lost = sent::make_packet(2, now, cc.max_datagram_size());
        cc.on_packet_sent(&p_not_lost, now);
        now += RTT;
        cc.on_packets_acked(
            &[p_not_lost],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        cwnd_is_halved(&cc);
        // cc is app limited therefore cwnd in not increased.
        assert_eq!(cc.acked_bytes(), 0);

        // Now we are in the congestion avoidance phase.
        assert_eq!(cc.current.phase, Phase::CongestionAvoidance);
        // simulate packet bursts below app_limit
        let mut next_pn = 3;
        for packet_burst_size in 1..=BELOW_APP_LIMIT_PKTS {
            // always stay below app_limit during sent.
            let mut pkts = Vec::new();
            for _ in 0..packet_burst_size {
                let p = sent::make_packet(next_pn, now, cc.max_datagram_size());
                next_pn += 1;
                cc.on_packet_sent(&p, now);
                pkts.push(p);
            }
            assert_eq!(
                cc.bytes_in_flight(),
                packet_burst_size * cc.max_datagram_size()
            );
            assert!(cc.app_limited(), "burst must stay below the threshold");
            now += RTT;
            for (i, pkt) in pkts.into_iter().enumerate() {
                cc.on_packets_acked(
                    &[pkt],
                    &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
                    now,
                    &mut cc_stats,
                );

                assert_eq!(
                    cc.bytes_in_flight(),
                    (packet_burst_size - i - 1) * cc.max_datagram_size()
                );
                cwnd_is_halved(&cc); // CWND doesn't grow because we're app limited
                assert_eq!(cc.acked_bytes(), 0);
            }
        }

        // Fully utilize the congestion window by sending enough packets to
        // have `bytes_in_flight` above the `app_limited` threshold.
        let mut pkts = Vec::new();
        for _ in 0..ABOVE_APP_LIMIT_PKTS {
            let p = sent::make_packet(next_pn, now, cc.max_datagram_size());
            next_pn += 1;
            cc.on_packet_sent(&p, now);
            pkts.push(p);
        }
        assert_eq!(
            cc.bytes_in_flight(),
            ABOVE_APP_LIMIT_PKTS * cc.max_datagram_size()
        );
        assert!(!cc.app_limited(), "burst must fill `cwnd`");
        now += RTT;
        let mut last_acked_bytes = 0;
        // Check if congestion window gets increased for all packets currently in flight
        for (i, pkt) in pkts.into_iter().enumerate() {
            cc.on_packets_acked(
                &[pkt],
                &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
                now,
                &mut cc_stats,
            );

            assert_eq!(
                cc.bytes_in_flight(),
                (ABOVE_APP_LIMIT_PKTS - i - 1) * cc.max_datagram_size()
            );
            // The cwnd doesn't increase, but the acked_bytes do, which will eventually lead to an
            // increase, once the number of bytes reaches the necessary level
            cwnd_is_halved(&cc);
            // increase acked_bytes with each packet
            assert_ne!(cc.acked_bytes(), last_acked_bytes);
            last_acked_bytes = cc.acked_bytes();
        }
    }

    #[test]
    fn ecn_ce() {
        let now = now();
        let mut cc = make_cc_cubic();
        let mut cc_stats = CongestionControlStats::default();
        let p_ce = sent::Packet::new(
            packet::Type::Short,
            1,
            now,
            true,
            recovery::Tokens::new(),
            cc.max_datagram_size(),
        );
        cc.on_packet_sent(&p_ce, now);
        assert_eq!(cc.cwnd(), cc.cwnd_initial());
        assert_eq!(cc.ssthresh(), None);
        assert_eq!(cc.current.phase, Phase::SlowStart);
        assert_eq!(cc_stats.congestion_events.ecn, 0);

        // Signal congestion (ECN CE) and thus change phase to recovery start.
        cc.on_ecn_ce_received(&p_ce, now, &mut cc_stats);
        assert_eq!(cc.cwnd(), cc.cwnd_initial() * 85 / 100);
        assert_eq!(cc.ssthresh(), Some(cc.cwnd_initial() * 85 / 100));
        assert_eq!(cc.current.phase, Phase::RecoveryStart);
        assert_eq!(cc_stats.congestion_events.ecn, 1);
    }

    /// This tests spurious congestion event detection, stat counting and the recovery mechanism.
    ///
    /// 1. Send packets (1, 2) --> `SlowStart`, no events
    /// 2. Lose packets (1, 2) --> `RecoveryStart`, 1 event
    /// 3. Send packet (3)     --> `Recovery`, 1 event
    /// 4. Ack packet (3)      --> `CongestionAvoidance`, 1 event
    /// 5. Ack packet (1)      --> `CongestionAvoidance`, 1 event, not a spurious event as not all
    ///    lost packets were recovered
    /// 6. Ack packet (2)      --> all lost packets have been recovered so now we've detected a
    ///    spurious congestion event
    #[test]
    fn spurious_congestion_event_detection_and_undo() {
        let mut cc = make_cc_cubic();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();

        // 1. Send packets (1, 2) --> `SlowStart`, no events
        let pkt1 = sent::make_packet(1, now, 1000);
        let pkt2 = sent::make_packet(2, now, 1000);
        cc.on_packet_sent(&pkt1, now);
        cc.on_packet_sent(&pkt2, now);
        assert_eq!(cc.current.phase, Phase::SlowStart);
        assert_eq!(cc_stats.congestion_events.loss, 0);
        assert_eq!(cc_stats.congestion_events.spurious, 0);

        // 2. Lose packets (1, 2) --> `RecoveryStart`, 1 event, reduced cwnd
        let cwnd_before_loss = cc.cwnd();
        assert_eq!(cc_stats.w_max, None);
        let mut lost_pkt1 = pkt1.clone();
        let mut lost_pkt2 = pkt2.clone();
        lost_pkt1.declare_lost(now, sent::LossTrigger::TimeThreshold);
        lost_pkt2.declare_lost(now, sent::LossTrigger::TimeThreshold);
        cc.on_packets_lost(
            Some(now),
            None,
            PTO,
            &[lost_pkt1, lost_pkt2],
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::RecoveryStart);
        assert!(cc_stats.slow_start_exit.is_some());
        assert_eq!(cc_stats.congestion_events.loss, 1);
        #[expect(
            clippy::cast_sign_loss,
            clippy::cast_possible_truncation,
            reason = "w_max is non-negative and represents whole bytes"
        )]
        let w_max_stat = cc_stats.w_max.unwrap() as usize;
        assert_eq!(w_max_stat, cwnd_before_loss);
        assert_eq!(
            cc.cwnd(),
            cc.cwnd_initial() * Cubic::BETA_USIZE_DIVIDEND / Cubic::BETA_USIZE_DIVISOR
        );

        // 3. Send packet (3)     --> `Recovery`, 1 event
        let pkt3 = sent::make_packet(3, now, 1000);
        cc.on_packet_sent(&pkt3, now);
        assert_eq!(cc.current.phase, Phase::Recovery);
        assert_eq!(cc_stats.congestion_events.loss, 1);

        // 4. Ack packet (3)      --> `CongestionAvoidance`, 1 event
        cc.on_packets_acked(
            &[pkt3],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::CongestionAvoidance);
        assert_eq!(cc_stats.congestion_events.loss, 1);

        // 5. Ack packet (1)      --> `CongestionAvoidance`, 1 event, not a spurious event as not
        //    all lost packets were recovered
        cc.on_packets_acked(
            &[pkt1],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::CongestionAvoidance);
        assert_eq!(cc_stats.congestion_events.loss, 1);
        assert_eq!(cc_stats.congestion_events.spurious, 0);

        // 6. Ack packet (2)      --> all lost packets have been recovered so now we've detected a
        //    spurious congestion event and reset to previous state
        cc.on_packets_acked(
            &[pkt2],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::SlowStart);
        assert_eq!(cc_stats.slow_start_exit, None);
        assert_eq!(cc_stats.congestion_events.loss, 1);
        assert_eq!(cc_stats.congestion_events.spurious, 1);
        assert_eq!(cc.cwnd(), cc.cwnd_initial());
        assert_eq!(cc_stats.w_max, None);
    }

    /// Same scenario as `spurious_congestion_event_detection_and_undo` but with recovery disabled.
    /// Detection still fires and the spurious counter is incremented, but cc parameters are not
    /// restored.
    #[test]
    fn spurious_congestion_event_detection_recovery_disabled() {
        let mut cc = ClassicCongestionController::new(
            ClassicSlowStart::default(),
            Cubic::default(),
            pmtud(),
            false,
        );
        let now = now();
        let mut cc_stats = CongestionControlStats::default();

        // 1. Send packets (1, 2) --> `SlowStart`
        let pkt1 = sent::make_packet(1, now, 1000);
        let pkt2 = sent::make_packet(2, now, 1000);
        cc.on_packet_sent(&pkt1, now);
        cc.on_packet_sent(&pkt2, now);
        assert_eq!(cc.current.phase, Phase::SlowStart);

        // 2. Lose packets (1, 2) --> `RecoveryStart` and reduced cwnd
        let mut lost_pkt1 = pkt1.clone();
        let mut lost_pkt2 = pkt2.clone();
        lost_pkt1.declare_lost(now, sent::LossTrigger::TimeThreshold);
        lost_pkt2.declare_lost(now, sent::LossTrigger::TimeThreshold);
        cc.on_packets_lost(
            Some(now),
            None,
            PTO,
            &[lost_pkt1, lost_pkt2],
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::RecoveryStart);
        assert_eq!(cc_stats.congestion_events.loss, 1);
        let cwnd_after_loss = cc.cwnd();
        assert!(cc_stats.slow_start_exit.is_some());

        // 3. Send packet (3) --> `Recovery`
        let pkt3 = sent::make_packet(3, now, 1000);
        cc.on_packet_sent(&pkt3, now);
        assert_eq!(cc.current.phase, Phase::Recovery);

        // 4. Ack packet (3) --> `CongestionAvoidance`
        cc.on_packets_acked(
            &[pkt3],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc.current.phase, Phase::CongestionAvoidance);

        // 5. Ack packet (1) --> not all lost packets recovered yet
        cc.on_packets_acked(
            &[pkt1],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc_stats.congestion_events.spurious, 0);

        // 6. Ack packet (2) --> spurious event detected, counter incremented, but NO recovery
        //    because pref is turned off. Assert that nothing is reset.
        cc.on_packets_acked(
            &[pkt2],
            &RttEstimate::new(crate::DEFAULT_INITIAL_RTT),
            now,
            &mut cc_stats,
        );
        assert_eq!(cc_stats.congestion_events.spurious, 1);
        assert_eq!(cc.cwnd(), cwnd_after_loss);
        assert_eq!(cc.current.phase, Phase::CongestionAvoidance);
        assert!(cc_stats.slow_start_exit.is_some());
        assert!(cc_stats.w_max.is_some());
    }

    /// This tests a scenario where spurious detection happens late, after cwnd has recovered and
    /// surpassed the previous cwnd naturally. In that case the spurious congestion event shouldn't
    /// be undone.
    #[test]
    fn late_spurious_congestion_event_without_undo() {
        let mut cc = make_cc_newreno();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(crate::DEFAULT_INITIAL_RTT);

        // Cause congestion event
        let pkt = sent::make_packet(1, now, 1000);
        cc.on_packet_sent(&pkt, now);
        let pkt_lost = pkt.clone();
        cc.on_packets_lost(Some(now), None, PTO, &[pkt_lost], now, &mut cc_stats);
        assert!(cc.cwnd() < cc.cwnd_initial(), "cwnd should have decreased");

        // Send recovery packet
        let pkt_recovery = sent::make_packet(2, now, 1000);
        cc.on_packet_sent(&pkt_recovery, now);
        cc.on_packets_acked(&[pkt_recovery], &rtt_estimate, now, &mut cc_stats);

        // Grow cwnd back naturally.
        let mut next_pn_to_send = 3;
        loop {
            let mut sent_packets = Vec::new();
            while cc.bytes_in_flight < cc.cwnd() {
                let pkt = sent::make_packet(next_pn_to_send, now, cc.max_datagram_size());
                cc.on_packet_sent(&pkt, now);
                sent_packets.push(pkt);
                next_pn_to_send += 1;
            }

            cc.on_packets_acked(&sent_packets, &rtt_estimate, now, &mut cc_stats);

            if cc.cwnd() >= cc.cwnd_initial() {
                break;
            }
        }

        let cwnd_recovered = cc.cwnd();
        assert!(
            cwnd_recovered >= cc.cwnd_initial(),
            "cwnd should have grown back, but cwnd_recovered is less than cwnd_initial {cwnd_recovered} < {}",
            cc.cwnd_initial()
        );

        // Now detect spurious (late)
        cc.on_packets_acked(&[pkt], &rtt_estimate, now, &mut cc_stats);

        // Detects the spurious congestion event but should NOT restore old params because cwnd has
        // recovered naturally.
        assert_eq!(cc.cwnd(), cwnd_recovered, "cwnd should not be restored");
        assert_eq!(cc_stats.congestion_events.spurious, 1);
    }

    /// Test that losses during recovery don't cause double-counting of spurious events.
    /// This happened when detection was implemented but the recovery mechanism wasn't, as that
    /// meant we weren't leaving recovery when detecting a spurious event. The test confirms
    /// that the bug doesn't occur anymore now that the recovery is implemented.
    ///
    /// Scenario:
    /// 1. Send packets 1,2
    /// 2. Lose packet 1 → congestion event #1
    /// 3. Send packet 3 → enter Recovery phase
    /// 4. Late ack packet 1 → spurious event #1 detected (we would not leave recovery here, thus
    ///    5. wouldn't trigger a congestion event)
    /// 5. Lose packet 2 → congestion event #2
    /// 6. Ack packet 2 → should trigger spurious event #2 (but not without also having an actual
    ///    congestion event in 4.)
    #[test]
    fn spurious_no_double_detection_in_recovery() {
        let mut cc = make_cc_newreno();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(RTT);

        // Step 1: Send packets 1,2
        let pkt1 = sent::make_packet(1, now, 1000);
        let pkt2 = sent::make_packet(2, now, 1000);

        cc.on_packet_sent(&pkt1, now);
        cc.on_packet_sent(&pkt2, now);

        assert_eq!(cc.current.phase, Phase::SlowStart);
        assert_eq!(cc_stats.congestion_events.loss, 0);
        assert_eq!(cc_stats.congestion_events.spurious, 0);

        let mut lost_pkt1 = pkt1.clone();
        lost_pkt1.declare_lost(now, sent::LossTrigger::TimeThreshold);

        // Step 2: Lose packet 1 → congestion event #1
        cc.on_packets_lost(
            Some(now),
            None,
            rtt_estimate.pto(true),
            &[lost_pkt1],
            now,
            &mut cc_stats,
        );

        assert_eq!(cc.current.phase, Phase::RecoveryStart);
        assert_eq!(cc_stats.congestion_events.loss, 1);
        assert_eq!(cc_stats.congestion_events.spurious, 0);

        // Step 3: Send packet 3 → enter Recovery phase
        let pkt3 = sent::make_packet(3, now, 1000);
        cc.on_packet_sent(&pkt3, now);
        assert_eq!(cc.current.phase, Phase::Recovery);

        // Step 4: Ack packet 1 → spurious event #1 detected
        cc.on_packets_acked(&[pkt1], &rtt_estimate, now, &mut cc_stats);

        assert_eq!(cc_stats.congestion_events.loss, 1);
        assert_eq!(cc_stats.congestion_events.spurious, 1);

        let mut lost_pkt2 = pkt2.clone();
        lost_pkt2.declare_lost(now, sent::LossTrigger::TimeThreshold);

        // Step 5. Lose packet 2 → New congestion event as we left recovery when restoring the
        // previous params.
        cc.on_packets_lost(
            Some(now),
            None,
            rtt_estimate.pto(true),
            &[lost_pkt2],
            now,
            &mut cc_stats,
        );

        // Still only 1 spurious event (but a new loss event)
        assert_eq!(cc_stats.congestion_events.loss, 2);
        assert_eq!(cc_stats.congestion_events.spurious, 1);

        // 6. Ack packet 2 → should trigger spurious event #2 because we left recovery when
        //    recovering from spurious event #1
        cc.on_packets_acked(&[pkt2], &rtt_estimate, now, &mut cc_stats);

        // Should now be 2 loss events and 2 spurious events, no double counting occured
        assert_eq!(cc_stats.congestion_events.loss, 2);
        assert_eq!(cc_stats.congestion_events.spurious, 2,);
    }

    #[test]
    fn spurious_congestion_event_detection_cleanup() {
        let mut cc = make_cc_newreno();
        let mut now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(crate::DEFAULT_INITIAL_RTT);

        let pkt1 = sent::make_packet(1, now, 1000);
        cc.on_packet_sent(&pkt1, now);

        cc.on_packets_lost(
            Some(now),
            None,
            rtt_estimate.pto(true),
            &[pkt1],
            now,
            &mut cc_stats,
        );

        // The lost should be added now.
        assert!(!cc.maybe_lost_packets.is_empty());

        // Packets older than 2 * PTO are removed, so we increase by exactly that.
        now += 2 * rtt_estimate.pto(true);

        // The cleanup is called when we ack packets, so we send and ack a new one.
        let pkt2 = sent::make_packet(2, now, 1000);
        cc.on_packet_sent(&pkt2, now);
        cc.on_packets_acked(&[pkt2], &rtt_estimate, now, &mut cc_stats);

        // The packet is exactly the maximum age, so it shouldn't be removed yet. This assert makes
        // sure we don't clean up too early.
        assert!(!cc.maybe_lost_packets.is_empty());

        // Increase by 1ms to get over the maximum age.
        now += Duration::from_millis(1);

        // Send and ack another packet to trigger cleanup.
        let pkt3 = sent::make_packet(3, now, 1000);
        cc.on_packet_sent(&pkt3, now);
        cc.on_packets_acked(&[pkt3], &rtt_estimate, now, &mut cc_stats);

        // Now the packet should be removed.
        assert!(cc.maybe_lost_packets.is_empty());
    }

    fn slow_start_exit_stats(congestion_trigger: CongestionTrigger) {
        let mut cc = make_cc_newreno();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(RTT);

        let initial_cwnd = cc.cwnd();

        assert!(cc.current.phase.in_slow_start());
        assert_eq!(cc_stats.slow_start_exit, None);

        let pkt1 = sent::make_packet(1, now, 1000);
        cc.on_packet_sent(&pkt1, now);

        match congestion_trigger {
            Ecn => {
                cc.on_ecn_ce_received(&pkt1, now, &mut cc_stats);
            }
            Loss(_) => {
                cc.on_packets_lost(
                    Some(now),
                    None,
                    PTO,
                    std::slice::from_ref(&pkt1),
                    now,
                    &mut cc_stats,
                );
            }
        }

        // Should have exited slow start and recorded stats.
        assert!(!cc.current.phase.in_slow_start());
        let exit_stats = cc_stats
            .slow_start_exit
            .as_ref()
            .expect("slow_start_exit must be set after congestion exit");
        assert_eq!(exit_stats.exit_cwnd, cc.cwnd());
        assert_eq!(exit_stats.detection_cwnd, initial_cwnd);
        assert_eq!(exit_stats.bytes_in_flight, 1000);
        assert_eq!(
            exit_stats.reason,
            SlowStartExitReason::CongestionEvent(congestion_trigger)
        );

        // For loss, test that a spurious congestion event resets the stats.
        if matches!(congestion_trigger, Loss(_)) {
            // Send recovery packet and ack it to exit recovery.
            let pkt2 = sent::make_packet(2, now, 1000);
            cc.on_packet_sent(&pkt2, now);
            cc.on_packets_acked(&[pkt2], &rtt_estimate, now, &mut cc_stats);

            // Late ack of pkt1 triggers spurious congestion detection - should reset to None.
            cc.on_packets_acked(&[pkt1], &rtt_estimate, now, &mut cc_stats);

            assert!(cc.current.phase.in_slow_start());
            assert_eq!(cc_stats.slow_start_exit, None);
        }
    }

    #[test]
    fn slow_start_exit_stats_loss() {
        slow_start_exit_stats(Loss(1));
    }

    #[test]
    fn slow_start_exit_stats_ecn_ce() {
        slow_start_exit_stats(Ecn);
    }

    #[test]
    fn state_to_qlog() {
        use super::Phase;
        let qlog = |p: Phase| -> &str { p.into() };
        assert_eq!(qlog(Phase::SlowStart), "slow_start");
        assert_eq!(qlog(Phase::PersistentCongestion), "slow_start");
        assert_eq!(qlog(Phase::CongestionAvoidance), "congestion_avoidance");
        assert_eq!(qlog(Phase::Recovery), "recovery");
        assert_eq!(qlog(Phase::RecoveryStart), "recovery");
    }

    #[test]
    fn cwnd_stat() {
        let mut cc = make_cc_newreno();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(crate::DEFAULT_INITIAL_RTT);

        let cwnd_initial = cc.cwnd();

        // Grow cwnd in slow start by filling the congestion window
        let mut next_pn = 0;
        let mut sent_packets = Vec::new();
        while cc.bytes_in_flight < cc.cwnd() {
            let pkt = sent::make_packet(next_pn, now, cc.max_datagram_size());
            cc.on_packet_sent(&pkt, now);
            sent_packets.push(pkt);
            next_pn += 1;
        }
        cc.on_packets_acked(&sent_packets, &rtt_estimate, now, &mut cc_stats);
        let cwnd_after_growth = cc.cwnd();
        assert!(cwnd_after_growth > cwnd_initial);

        // cwnd shrinks after congestion event
        let pkt_lost = sent::make_packet(next_pn, now, 1000);
        cc.on_packet_sent(&pkt_lost, now);
        cc.on_packets_lost(Some(now), None, PTO, &[pkt_lost], now, &mut cc_stats);
        assert!(cc.cwnd() < cwnd_after_growth);

        // cwnd resets to minimum after persistent congestion
        let lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        cc.detect_persistent_congestion(Some(now), None, PTO, lost.iter(), now);
        assert_eq!(cc.cwnd(), cc.cwnd_min());
    }

    #[test]
    // There was a bug in the stat logic that it never got initialized if a connection never made it
    // past the point of being app-limited, i.e. it returned `0` if a connection never grew the
    // congestion window. This test asserts that it is getting initialized to the initial window
    // size on the first ack, even if the congestion window doesn't grow.
    fn cwnd_stat_app_limited() {
        let mut cc = make_cc_cubic();
        let now = now();
        let mut cc_stats = CongestionControlStats::default();
        let rtt_estimate = RttEstimate::new(crate::DEFAULT_INITIAL_RTT);

        let cwnd_initial = cc.cwnd();

        // Send and ack a single packet — not enough to fill cwnd, so app-limited.
        let pkt = sent::make_packet(0, now, cc.max_datagram_size());
        cc.on_packet_sent(&pkt, now);
        cc.on_packets_acked(&[pkt], &rtt_estimate, now, &mut cc_stats);

        assert_eq!(cc.cwnd(), cwnd_initial);
    }

    #[test]
    fn slow_start_state_reset_after_persistent_congestion() {
        let lost = make_lost(&[1, PERSISTENT_CONG_THRESH + 2]);
        let mut cc = make_cc_hystart(true);
        let mut cc_stats = CongestionControlStats::default();

        // Dirty HyStart state so current_round_min_rtt is non-None.
        cc.slow_start
            .on_packets_acked(&RttEstimate::new(RTT), 0, cc.cwnd(), &mut cc_stats, now());
        assert!(cc.slow_start.current_round_min_rtt().is_some());

        cc.detect_persistent_congestion(Some(by_pto(0)), None, PTO, lost.iter(), now());
        assert_eq!(cc.cwnd(), cc.cwnd_min());

        // HyStart state should be reset, so current_round_min_rtt is None again.
        assert!(cc.slow_start.current_round_min_rtt().is_none());
    }

    /// Set up a `ClassicCongestionController` with qlog enabled, run `f`, then assert
    /// that the qlog output contains the given `trigger` string in a
    /// `CongestionStateUpdated` event.
    fn assert_congestion_state_trigger(
        trigger: &str,
        f: impl FnOnce(
            &mut ClassicCongestionController<ClassicSlowStart, NewReno>,
            &mut CongestionControlStats,
        ),
    ) {
        let (log, contents) = new_neqo_qlog();
        let mut cc = make_cc_newreno();
        cc.set_qlog(log);
        let mut cc_stats = CongestionControlStats::default();
        f(&mut cc, &mut cc_stats);
        drop(cc);
        assert!(
            contents
                .to_string()
                .contains(&format!(r#""trigger":"{trigger}""#)),
            "Expected {trigger} trigger in qlog"
        );
    }

    /// An ECN congestion event should log `CongestionStateUpdated` with `trigger = "ecn"`.
    #[test]
    fn congestion_state_updated_ecn_trigger() {
        assert_congestion_state_trigger("ecn", |cc, stats| {
            let now = now();
            let p_ce = sent::Packet::new(
                packet::Type::Short,
                1,
                now,
                true,
                recovery::Tokens::new(),
                cc.max_datagram_size(),
            );
            cc.on_packet_sent(&p_ce, now);
            cc.on_ecn_ce_received(&p_ce, now, stats);
        });
    }

    /// Persistent congestion should log `CongestionStateUpdated` with
    /// `trigger = "persistent_congestion"`, even though a preceding
    /// `on_congestion_event` left the phase in the transient `RecoveryStart`.
    #[test]
    fn congestion_state_updated_persistent_congestion_trigger() {
        assert_congestion_state_trigger("persistent_congestion", |cc, stats| {
            let lost_pkts = [lost(1, true, ZERO), lost(2, true, PC)];
            for p in &lost_pkts {
                cc.on_packet_sent(p, now());
            }
            assert_ne!(cc.cwnd(), cc.cwnd_min());
            cc.on_packets_lost(Some(now()), None, PTO, &lost_pkts, now(), stats);
            assert_eq!(
                cc.cwnd(),
                cc.cwnd_min(),
                "persistent congestion should have been detected"
            );
        });
    }
    /// RFC 9002 6.1: only in-flight (ack-eliciting) packets should be declared lost.
    /// Non-ack-eliciting packets (e.g., only containing ACK-frames) must not trigger loss detection
    /// or congestion events. This reproduces a bug where ACK-only packets sent after a
    /// 0-RTT handshake were declared lost because the server (rightfully so) didn't send an ACK,
    /// causing early slow start exit and setting ssthresh to `initial_cwnd * 0.7`.
    #[test]
    fn no_congestion_event_if_lost_packet_not_in_flight() {
        let mut cc = make_cc_cubic();
        let cc_stats = &mut CongestionControlStats::default();

        // create a packet that is not ack-eliciting, so won't count as in flight
        let lost_pkt = sent::Packet::new(
            packet::Type::Short,
            0,
            now(),
            false,
            recovery::Tokens::new(),
            MIN_INITIAL_PACKET_SIZE,
        );

        let initial_cwnd = cc.cwnd();

        // call `on_packets_lost` on the non ack-eliciting packet
        cc.on_packets_lost(Some(now()), None, PTO, &[lost_pkt], now(), cc_stats);

        // there should be no reaction to the non-ack-eliciting packet
        assert_eq!(cc.cwnd(), initial_cwnd);
        assert_eq!(cc_stats.slow_start_exit, None);
    }
}
