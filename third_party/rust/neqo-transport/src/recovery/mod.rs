// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Tracking of sent packets and detecting their loss.

pub mod sent;
mod token;

use std::{
    cmp::{max, min},
    fmt::{self, Display, Formatter},
    ops::RangeInclusive,
    time::{Duration, Instant},
};

use enum_map::EnumMap;
use enumset::enum_set;
use neqo_common::qlog::Qlog;
use strum::IntoEnumIterator as _;
pub use token::{StreamRecoveryToken, Token, Tokens};
use tracing::{debug, info, trace, warn};

use crate::{
    ecn, packet,
    path::{Path, PathRef},
    qlog,
    rtt::{RttEstimate, RttSource},
    stats::{Stats, StatsCell},
    tracking::{PacketNumberSpace, PacketNumberSpaceSet},
};

pub const PACKET_THRESHOLD: u64 = 3;
/// `ACK_ONLY_SIZE_LIMIT` is the minimum size of the congestion window.
/// If the congestion window is this small, we will only send ACK frames.
pub const ACK_ONLY_SIZE_LIMIT: usize = 256;
/// The maximum number of packets we send on a PTO.
pub const MAX_PTO_PACKET_COUNT: usize = 2;
/// The preferred limit on the number of packets that are tracked.
/// If we exceed this number, we start sending `PING` frames sooner to
/// force the peer to acknowledge some of them.
pub const MAX_OUTSTANDING_UNACK: usize = 200;
/// Disable PING until this many packets are outstanding.
pub const MIN_OUTSTANDING_UNACK: usize = 16;
/// The scale we use for the fast PTO feature.
pub const FAST_PTO_SCALE: u8 = 100;

/// `SendProfile` tells a sender how to send packets.
#[derive(Debug)]
pub struct SendProfile {
    /// The limit on the size of the packet.
    limit: usize,
    /// What spaces should be probed.
    probe: PacketNumberSpaceSet,
    /// Whether pacing is active.
    paced: bool,
    /// Whether the limit is the full path MTU.
    full_mtu: bool,
}

impl SendProfile {
    #[must_use]
    pub fn new_limited(limit: usize, mtu: usize) -> Self {
        // When the limit is too low, we only send ACK frames.
        // Set the limit to `ACK_ONLY_SIZE_LIMIT - 1` to ensure that
        // ACK-only packets are still limited in size.
        Self {
            limit: max(ACK_ONLY_SIZE_LIMIT - 1, limit),
            probe: PacketNumberSpaceSet::empty(),
            paced: false,
            full_mtu: limit >= mtu,
        }
    }

    #[must_use]
    pub fn new_paced() -> Self {
        // When pacing, we still allow ACK frames to be sent.
        Self {
            limit: ACK_ONLY_SIZE_LIMIT - 1,
            probe: PacketNumberSpaceSet::empty(),
            paced: true,
            full_mtu: false,
        }
    }

    #[must_use]
    pub fn new_pto(mtu: usize, probe: PacketNumberSpaceSet) -> Self {
        debug_assert!(mtu > ACK_ONLY_SIZE_LIMIT);
        Self {
            limit: mtu,
            probe,
            paced: false,
            full_mtu: true,
        }
    }

    /// Whether probing this space is helpful.  This isn't necessarily the space
    /// that caused the timer to pop, but it is helpful to send a PING in a space
    /// that has the PTO timer armed.
    #[must_use]
    pub fn should_probe(&self, space: PacketNumberSpace) -> bool {
        self.probe.contains(space)
    }

    /// Determine whether an ACK-only packet should be sent. Returns true if the congestion window
    /// is too small to send data frames.
    #[must_use]
    pub const fn ack_only(&self) -> bool {
        self.limit < ACK_ONLY_SIZE_LIMIT
    }

    #[must_use]
    pub const fn paced(&self) -> bool {
        self.paced
    }

    #[must_use]
    pub const fn limit(&self) -> usize {
        self.limit
    }

    /// Whether the next packet can be `plpmtu`-sized.
    #[must_use]
    pub const fn full_mtu(&self) -> bool {
        self.full_mtu
    }

    /// Lower the limit, even below the one for ACK-only packets.
    pub(crate) fn cap(&mut self, limit: usize) {
        self.limit = min(self.limit, limit);
    }
}

#[derive(Debug)]
pub struct LossRecoverySpace {
    space: PacketNumberSpace,
    largest_acked: Option<packet::Number>,
    largest_acked_sent_time: Option<Instant>,
    /// The time used to calculate the PTO timer for this space.
    /// This is the time that the last ACK-eliciting packet in this space
    /// was sent.  This might be the time that a probe was sent.
    /// For Initial and Handshake spaces, this may also be set when we haven't
    /// sent any packets yet but need a PTO baseline (see `on_packet_sent` and
    /// `on_packets_acked` for how this is established).
    last_ack_eliciting: Option<Instant>,
    /// The number of outstanding packets in this space that are in flight.
    /// This might be less than the number of ACK-eliciting packets,
    /// because PTO packets don't count.
    in_flight_outstanding: usize,
    /// The packets that we have sent and are tracking.
    sent_packets: sent::Packets,
    /// The time that the first out-of-order packet was sent.
    /// This is `None` if there were no out-of-order packets detected.
    /// When set to `Some(T)`, time-based loss detection should be enabled.
    first_ooo_time: Option<Instant>,
}

impl LossRecoverySpace {
    #[must_use]
    pub fn new(space: PacketNumberSpace) -> Self {
        Self {
            space,
            largest_acked: None,
            largest_acked_sent_time: None,
            last_ack_eliciting: None,
            in_flight_outstanding: 0,
            sent_packets: sent::Packets::default(),
            first_ooo_time: None,
        }
    }

    /// Find the time we sent the first packet that is lower than the
    /// largest acknowledged and that isn't yet declared lost.
    /// Use the value we prepared earlier in `detect_lost_packets`.
    #[must_use]
    pub const fn loss_recovery_timer_start(&self) -> Option<Instant> {
        self.first_ooo_time
    }

    #[must_use]
    pub const fn in_flight_outstanding(&self) -> bool {
        self.in_flight_outstanding > 0
    }

    pub fn pto_packets(&mut self) -> impl Iterator<Item = &sent::Packet> {
        self.sent_packets.iter_mut().filter_map(|sent| {
            sent.pto().then(|| {
                trace!("PTO: marking packet {} lost ", sent.pn());
                &*sent
            })
        })
    }

    #[must_use]
    pub fn pto_base_time(&self) -> Option<Instant> {
        if self.in_flight_outstanding() {
            debug_assert!(self.last_ack_eliciting.is_some());
            self.last_ack_eliciting
        } else if self.space == PacketNumberSpace::ApplicationData {
            None
        } else {
            // Nasty special case to prevent handshake deadlocks.
            // A client needs to keep the PTO timer armed to prevent a stall
            // of the handshake.  Technically, this has to stop once we receive
            // an ACK of Handshake or 1-RTT, or when we receive HANDSHAKE_DONE,
            // but a few extra probes won't hurt.
            //
            // RFC 9002 Section 6.2.4 requires sending probes in packet number spaces
            // with in-flight data. When we have keys for a space but haven't sent
            // anything ack-eliciting yet (e.g., waiting for peer's Handshake flight),
            // we still need to arm the PTO timer to probe and elicit retransmission.
            //
            // If no ack-eliciting packets have been sent in this space yet,
            // last_ack_eliciting may be set as a PTO baseline in two ways:
            // 1. When we send ANY packet in Initial/Handshake (see on_packet_sent)
            // 2. When we receive ACKs in Initial and prime Handshake (see on_packets_acked)
            //
            // This ensures the PTO timer arms when we have keys for a space but
            // nothing to send yet, allowing us to probe and elicit peer retransmission.
            // RFC 9002 Section 6.2.4 requires probing packet number spaces.
            self.last_ack_eliciting
        }
    }

    pub fn on_packet_sent(&mut self, sent_packet: sent::Packet) {
        if sent_packet.ack_eliciting() {
            self.last_ack_eliciting = Some(sent_packet.time_sent());
            self.in_flight_outstanding += 1;
        } else if self.space != PacketNumberSpace::ApplicationData
            && self.last_ack_eliciting.is_none()
        {
            // For Initial and Handshake spaces, make sure that we have a PTO baseline
            // always. See `LossRecoverySpace::pto_base_time()` for details.
            self.last_ack_eliciting = Some(sent_packet.time_sent());
        }
        self.sent_packets.track(sent_packet);
    }

    /// If we are only sending ACK frames, send a PING frame after 2 PTOs so that
    /// the peer sends an ACK frame.  If we have received lots of packets and no ACK,
    /// send a PING frame after 1 PTO.  Note that this can't be within a PTO, or
    /// we would risk setting up a feedback loop; having this many packets
    /// outstanding can be normal and we don't want to PING too often.
    #[must_use]
    pub fn should_probe(&self, pto: Duration, now: Instant) -> bool {
        let n_pto = if self.sent_packets.len() >= MAX_OUTSTANDING_UNACK {
            1
        } else if self.sent_packets.len() >= MIN_OUTSTANDING_UNACK {
            2
        } else {
            return false;
        };
        self.last_ack_eliciting
            .is_some_and(|t| now > t + (pto * n_pto))
    }

    fn remove_outstanding(&mut self, count: usize) {
        debug_assert!(self.in_flight_outstanding >= count);
        self.in_flight_outstanding -= count;
        if self.in_flight_outstanding == 0 {
            trace!("remove_packet outstanding == 0 for space {}", self.space);
        }
    }

    fn remove_packet(&mut self, p: &sent::Packet) {
        if p.ack_eliciting() {
            self.remove_outstanding(1);
        }
    }

    /// Remove all newly acknowledged packets.
    /// Returns all the acknowledged packets, with the largest packet number first.
    /// ...and a boolean indicating if any of those packets were ack-eliciting.
    /// This operates more efficiently because it assumes that the input is sorted
    /// in the order that an ACK frame is (from the top).
    fn remove_acked<R>(&mut self, acked_ranges: R, stats: &mut Stats) -> (Vec<sent::Packet>, bool)
    where
        R: IntoIterator<Item = RangeInclusive<packet::Number>>,
    {
        let acked = self.sent_packets.take_ranges(acked_ranges);
        let mut eliciting = false;
        for p in &acked {
            self.remove_packet(p);
            eliciting |= p.ack_eliciting();
            if p.lost() {
                stats.late_ack += 1;
                if let Some(reduced) = stats.lost.checked_sub(1) {
                    stats.lost = reduced;
                } else {
                    debug_assert!(false, "spurious losses should have been lost already");
                }
                if let Some(reduced) = stats.bytes_lost.checked_sub(p.len()) {
                    stats.bytes_lost = reduced;
                } else {
                    debug_assert!(
                        false,
                        "spurious lost bytes should have been counted already"
                    );
                }
            }
            if p.pto_fired() {
                stats.pto_ack += 1;
            }
        }
        (acked, eliciting)
    }

    /// Remove all tracked packets from the space.
    /// This is called by a client when 0-RTT packets are dropped, when a Retry is received
    /// and when keys are dropped.
    fn remove_ignored(&mut self) -> impl Iterator<Item = sent::Packet> + use<> {
        self.in_flight_outstanding = 0;
        std::mem::take(&mut self.sent_packets).drain_all()
    }

    /// Remove the primary path marking on any packets this is tracking.
    fn migrate(&mut self) {
        for pkt in self.sent_packets.iter_mut() {
            pkt.clear_primary_path();
        }
    }

    /// Remove old packets that we've been tracking in case they get acknowledged.
    /// We try to keep these around until a probe is sent for them, so it is
    /// important that `cd` is set to at least the current PTO time; otherwise we
    /// might remove all in-flight packets and stop sending probes.
    fn remove_old_lost(&mut self, now: Instant, cd: Duration) {
        let removed = self.sent_packets.remove_expired(now, cd);
        self.remove_outstanding(removed);
    }

    /// Detect lost packets.
    /// `loss_delay` is the time we will wait before declaring something lost.
    /// `cleanup_delay` is the time we will wait before cleaning up a lost packet.
    pub fn detect_lost_packets(
        &mut self,
        now: Instant,
        loss_delay: Duration,
        cleanup_delay: Duration,
        lost_packets: &mut Vec<sent::Packet>,
    ) {
        // Housekeeping.
        self.remove_old_lost(now, cleanup_delay);

        trace!(
            "detect lost {}: now={now:?} delay={loss_delay:?}",
            self.space,
        );
        self.first_ooo_time = None;

        let largest_acked = self.largest_acked;

        for packet in self
            .sent_packets
            .iter_mut()
            .take_while(|p| largest_acked.is_some_and(|largest_ack| p.pn() < largest_ack))
        {
            // Packets sent before now - loss_delay are deemed lost.
            let trigger = if packet.time_sent() + loss_delay <= now {
                trace!(
                    "lost={}, time sent {:?} is before lost_delay {loss_delay:?}",
                    packet.pn(),
                    packet.time_sent()
                );
                sent::LossTrigger::TimeThreshold
            } else if largest_acked >= Some(packet.pn() + PACKET_THRESHOLD) {
                trace!(
                    "lost={}, is >= {PACKET_THRESHOLD} from largest acked {largest_acked:?}",
                    packet.pn()
                );
                sent::LossTrigger::ReorderingThreshold
            } else {
                if largest_acked.is_some() {
                    self.first_ooo_time = Some(packet.time_sent());
                }
                // No more packets can be declared lost after this one.
                break;
            };

            if packet.declare_lost(now, trigger) {
                lost_packets.push(packet.clone());
            }
        }
    }
}

#[derive(Debug)]
pub struct LossRecoverySpaces {
    spaces: EnumMap<PacketNumberSpace, Option<LossRecoverySpace>>,
}

impl LossRecoverySpaces {
    /// Drop a packet number space and return all the packets that were
    /// outstanding, so that those can be marked as lost.
    ///
    /// # Panics
    ///
    /// If the space has already been removed.
    pub fn drop_space(
        &mut self,
        space: PacketNumberSpace,
    ) -> impl IntoIterator<Item = sent::Packet> + use<> {
        let sp = self.spaces[space].take();
        assert_ne!(
            space,
            PacketNumberSpace::ApplicationData,
            "discarding application space"
        );
        sp.expect("has not been removed").remove_ignored()
    }

    #[must_use]
    pub fn get(&self, space: PacketNumberSpace) -> Option<&LossRecoverySpace> {
        self.spaces[space].as_ref()
    }

    pub fn get_mut(&mut self, space: PacketNumberSpace) -> Option<&mut LossRecoverySpace> {
        self.spaces[space].as_mut()
    }

    fn iter(&self) -> impl Iterator<Item = &LossRecoverySpace> {
        self.spaces.iter().filter_map(|(_, recvd)| recvd.as_ref())
    }

    fn iter_mut(&mut self) -> impl Iterator<Item = &mut LossRecoverySpace> {
        self.spaces
            .iter_mut()
            .filter_map(|(_, recvd)| recvd.as_mut())
    }
}

impl Default for LossRecoverySpaces {
    fn default() -> Self {
        Self {
            spaces: EnumMap::from_array([
                Some(LossRecoverySpace::new(PacketNumberSpace::Initial)),
                Some(LossRecoverySpace::new(PacketNumberSpace::Handshake)),
                Some(LossRecoverySpace::new(PacketNumberSpace::ApplicationData)),
            ]),
        }
    }
}

#[derive(Debug)]
struct PtoState {
    /// The packet number space that caused the PTO to fire.
    space: PacketNumberSpace,
    /// The number of probes that we have sent.
    count: usize,
    packets: usize,
    /// The complete set of packet number spaces that can have probes sent.
    probe: PacketNumberSpaceSet,
}

impl PtoState {
    pub fn new(space: PacketNumberSpace, probe: PacketNumberSpaceSet) -> Self {
        debug_assert!(probe.contains(space));
        Self {
            space,
            count: 1,
            packets: MAX_PTO_PACKET_COUNT,
            probe,
        }
    }

    pub fn pto(&mut self, space: PacketNumberSpace, probe: PacketNumberSpaceSet) {
        debug_assert!(probe.contains(space));
        self.space = min(space, self.space);
        self.count += 1;
        self.packets = MAX_PTO_PACKET_COUNT;
        self.probe |= probe;
    }

    pub const fn count(&self) -> usize {
        self.count
    }

    pub fn count_pto(&self, stats: &mut Stats) {
        stats.add_pto_count(self.count);
    }

    /// Generate a sending profile, indicating what space it should be from.
    /// This takes a packet from the supply if one remains, or returns `None`.
    pub fn send_profile(&mut self, mtu: usize) -> Option<SendProfile> {
        (self.packets > 0).then(|| {
            self.packets -= 1;
            // This is a PTO, so ignore the limit.
            SendProfile::new_pto(mtu, self.probe)
        })
    }

    pub fn pto_sent(&mut self, space: PacketNumberSpace) {
        // For Initial and Handshake packets, don't force probes after the first packet.
        // Probing forces the inclusion of frames, even when there is nothing to send.
        // We do want to send subsequent packets if there is something there,
        // but, if we force a probe, we end up sending useless packets with just PING.
        if self.packets < MAX_PTO_PACKET_COUNT && space != PacketNumberSpace::ApplicationData {
            self.probe -= space;
        }
    }
}

pub struct Loss {
    /// When the handshake was confirmed, if it has been.
    confirmed_time: Option<Instant>,
    pto_state: Option<PtoState>,
    spaces: LossRecoverySpaces,
    qlog: Qlog,
    stats: StatsCell,
    /// The factor by which the PTO period is reduced.
    /// This enables faster probing at a cost in additional lost packets.
    fast_pto: u8,
    /// Snapshotted before input processing; see [`Self::note_timeout_type`].
    pending_timer_type: Option<qlog::LossTimerType>,
}

impl Loss {
    #[must_use]
    pub fn new(stats: StatsCell, fast_pto: u8) -> Self {
        Self {
            confirmed_time: None,
            pto_state: None,
            spaces: LossRecoverySpaces::default(),
            qlog: Qlog::default(),
            stats,
            fast_pto,
            pending_timer_type: None,
        }
    }

    #[must_use]
    pub fn largest_acknowledged_pn(&self, pn_space: PacketNumberSpace) -> Option<packet::Number> {
        self.spaces.get(pn_space)?.largest_acked
    }

    pub fn set_qlog(&mut self, qlog: Qlog) {
        self.qlog = qlog;
    }

    fn count_lost(&self, lost: &[sent::Packet]) {
        let mut stats = self.stats.borrow_mut();
        stats.lost += lost.len();
        stats.bytes_lost += lost.iter().map(sent::Packet::len).sum::<usize>();
    }

    /// Drop all 0rtt packets.
    pub fn drop_0rtt(&mut self, primary_path: &PathRef, now: Instant) -> Vec<sent::Packet> {
        let Some(sp) = self.spaces.get_mut(PacketNumberSpace::ApplicationData) else {
            return Vec::new();
        };
        if sp.largest_acked.is_some() {
            warn!("0-RTT packets already acknowledged, not dropping");
            return Vec::new();
        }
        let mut dropped = sp.remove_ignored().collect::<Vec<_>>();
        let mut path = primary_path.borrow_mut();
        for p in &mut dropped {
            path.discard_packet(p, now, &mut self.stats.borrow_mut());
        }
        dropped
    }

    pub fn on_packet_sent(&mut self, path: &PathRef, mut sent_packet: sent::Packet, now: Instant) {
        let pn_space = sent_packet.space();
        trace!("[{self}] packet {pn_space}-{} sent", sent_packet.pn());
        if let Some(pto) = self.pto_state.as_mut() {
            pto.pto_sent(pn_space);
        }
        if let Some(space) = self.spaces.get_mut(pn_space) {
            path.borrow_mut().packet_sent(&mut sent_packet, now);
            space.on_packet_sent(sent_packet);
        } else {
            info!(
                "[{self}] ignoring packet {} from dropped space {pn_space}",
                sent_packet.pn()
            );
        }
    }

    /// Whether to probe the path.
    #[must_use]
    pub fn should_probe(&self, pto: Duration, now: Instant) -> bool {
        self.spaces
            .get(PacketNumberSpace::ApplicationData)
            .is_some_and(|sp| sp.should_probe(pto, now))
    }

    /// Record an RTT sample.
    fn rtt_sample(
        &mut self,
        rtt: &mut RttEstimate,
        send_time: Instant,
        now: Instant,
        ack_delay: Duration,
    ) {
        let source = if self.confirmed_time.is_some_and(|t| t < send_time) {
            RttSource::AckConfirmed
        } else {
            RttSource::Ack
        };
        if let Some(sample) = now.checked_duration_since(send_time) {
            rtt.update(&mut self.qlog, sample, ack_delay, source, now);
        }
    }

    const fn confirmed(&self) -> bool {
        self.confirmed_time.is_some()
    }

    /// Prime the Handshake space PTO timer when stuck in Initial space.
    fn maybe_prime_handshake_pto(&mut self, now: Instant, has_handshake_keys: bool) {
        // Only prime if we actually have Handshake TX keys to send probes.
        if !has_handshake_keys {
            return;
        }

        // Only prime if we're in Initial space.
        let Some(pto) = self
            .pto_state
            .as_ref()
            .filter(|pto| pto.space == PacketNumberSpace::Initial)
        else {
            return;
        };

        // Only prime if we've received Initial ACKs (proving the peer is alive).
        if self
            .spaces
            .get(PacketNumberSpace::Initial)
            .is_none_or(|space| space.largest_acked.is_none())
        {
            return;
        }

        let Some(hs_space) = self.spaces.get_mut(PacketNumberSpace::Handshake) else {
            return;
        };

        // Only prime if we haven't sent or received anything in Handshake space yet.
        if hs_space.last_ack_eliciting.is_none() && hs_space.largest_acked.is_none() {
            trace!(
                "Priming Handshake PTO baseline (no HS packets after {} Initial PTOs)",
                pto.count()
            );
            hs_space.last_ack_eliciting = Some(now);
        }
    }

    /// Returns (acked packets, lost packets)
    pub fn on_ack_received<R>(
        &mut self,
        primary_path: &PathRef,
        pn_space: PacketNumberSpace,
        acked_ranges: R,
        ack_ecn: Option<&ecn::Count>,
        ack_delay: Duration,
        now: Instant,
    ) -> (Vec<sent::Packet>, Vec<sent::Packet>)
    where
        R: IntoIterator<Item = RangeInclusive<packet::Number>>,
    {
        let Some(space) = self.spaces.get_mut(pn_space) else {
            info!("ACK on discarded space");
            return (Vec::new(), Vec::new());
        };

        let (acked_packets, any_ack_eliciting) =
            space.remove_acked(acked_ranges, &mut self.stats.borrow_mut());
        let Some(largest_acked_pkt) = acked_packets.first() else {
            // No new information.
            return (Vec::new(), Vec::new());
        };

        // Track largest PN acked per space
        let prev_largest_acked = space.largest_acked_sent_time;
        if Some(largest_acked_pkt.pn()) > space.largest_acked {
            space.largest_acked = Some(largest_acked_pkt.pn());

            // If the largest acknowledged is newly acked and any newly acked
            // packet was ack-eliciting, update the RTT. (-recovery 5.1)
            space.largest_acked_sent_time = Some(largest_acked_pkt.time_sent());
            if any_ack_eliciting && largest_acked_pkt.on_primary_path() {
                self.rtt_sample(
                    primary_path.borrow_mut().rtt_mut(),
                    largest_acked_pkt.time_sent(),
                    now,
                    ack_delay,
                );
            }
        }

        debug!(
            "[{self}] ACK for {pn_space:?} - largest_acked={}",
            largest_acked_pkt.pn()
        );

        // Perform loss detection.
        // PTO is used to remove lost packets from in-flight accounting.
        // We need to ensure that we have sent any PTO probes before they are removed
        // as we rely on the count of in-flight packets to determine whether to send
        // another probe.  Removing them too soon would result in not sending on PTO.
        let cleanup_delay = self.pto_period(primary_path.borrow().rtt());
        let Some(sp) = self.spaces.get_mut(pn_space) else {
            return (Vec::new(), Vec::new());
        };
        let loss_delay = primary_path.borrow().rtt().loss_delay();
        let mut lost = Vec::new();
        sp.detect_lost_packets(now, loss_delay, cleanup_delay, &mut lost);
        self.count_lost(&lost);

        // Tell the congestion controller about any lost packets.
        // The PTO for congestion control is the raw number, without exponential
        // backoff, so that we can determine persistent congestion.
        primary_path.borrow_mut().on_packets_lost(
            prev_largest_acked,
            self.confirmed(),
            &lost,
            &mut self.stats.borrow_mut(),
            now,
        );

        // This must happen after on_packets_lost. If in recovery, this could
        // take us out, and then lost packets will start a new recovery period
        // when it shouldn't.
        primary_path.borrow_mut().on_packets_acked(
            &acked_packets,
            ack_ecn,
            now,
            &mut self.stats.borrow_mut(),
        );

        if self.pto_state.is_some() {
            qlog::loss_timer_cancelled(&mut self.qlog, now);
        }
        self.pto_state = None;

        (acked_packets, lost)
    }

    /// When receiving a retry, get all the sent packets so that they can be flushed.
    /// We also need to pretend that they never happened for the purposes of congestion control.
    pub fn retry(&mut self, primary_path: &PathRef, now: Instant) -> Vec<sent::Packet> {
        if self.pto_state.is_some() {
            qlog::loss_timer_cancelled(&mut self.qlog, now);
        }
        self.pto_state = None;
        let mut dropped = self
            .spaces
            .iter_mut()
            .flat_map(LossRecoverySpace::remove_ignored)
            .collect::<Vec<_>>();
        let mut path = primary_path.borrow_mut();
        for p in &mut dropped {
            path.discard_packet(p, now, &mut self.stats.borrow_mut());
        }
        dropped
    }

    fn confirm(&mut self, rtt: &RttEstimate, now: Instant) {
        debug_assert!(self.confirmed_time.is_none());
        self.confirmed_time = Some(now);
        // Up until now, the ApplicationData space has been ignored for PTO.
        // So maybe fire a PTO.
        if let Some(pto) = self.pto_time(rtt, PacketNumberSpace::ApplicationData)
            && pto < now
        {
            let probes = enum_set!(PacketNumberSpace::ApplicationData);
            self.fire_pto(PacketNumberSpace::ApplicationData, probes, rtt, now);
        }
    }

    /// This function is called when the connection migrates.
    /// It marks all packets that are outstanding as having being sent on a non-primary path.
    /// This way failure to deliver on the old path doesn't count against the congestion
    /// control state on the new path and the RTT measurements don't apply either.
    pub fn migrate(&mut self) {
        for space in self.spaces.iter_mut() {
            space.migrate();
        }
    }

    /// Discard state for a given packet number space.
    pub fn discard(&mut self, primary_path: &PathRef, space: PacketNumberSpace, now: Instant) {
        debug!("[{self}] Reset loss recovery state for {space:?}");
        let mut path = primary_path.borrow_mut();
        for p in self.spaces.drop_space(space) {
            path.discard_packet(&p, now, &mut self.stats.borrow_mut());
        }

        // We just made progress, so discard PTO count.
        // The spec says that clients should not do this until confirming that
        // the server has completed address validation, but ignore that.
        if self.pto_state.is_some() {
            qlog::loss_timer_cancelled(&mut self.qlog, now);
        }
        self.pto_state = None;

        if space == PacketNumberSpace::Handshake {
            self.confirm(path.rtt(), now);
        }
    }

    /// Calculate when the next timeout is likely to be.  This is the earlier of the loss timer
    /// and the PTO timer; either or both might be disabled, so this can return `None`.
    #[must_use]
    pub fn next_timeout(&self, path: &Path) -> Option<Instant> {
        let rtt = path.rtt();
        let loss_time = self.earliest_loss_time(rtt);
        let pto_time = if path.pto_possible() {
            self.earliest_pto(rtt)
        } else {
            None
        };
        trace!("[{self}] next_timeout loss={loss_time:?} pto={pto_time:?}");
        match (loss_time, pto_time) {
            (Some(loss_time), Some(pto_time)) => Some(min(loss_time, pto_time)),
            (Some(loss_time), None) => Some(loss_time),
            (None, Some(pto_time)) => Some(pto_time),
            (None, None) => None,
        }
    }

    /// Snapshot which timer type is due before input processing, so that ACKs
    /// in the same `process()` call cannot clear loss candidates and cause
    /// [`Self::timeout`] to misattribute the expiry as PTO.
    pub(crate) fn note_timeout_type(&mut self, path: &Path, now: Instant) {
        if self.qlog.is_enabled() && self.pending_timer_type.is_none() {
            self.pending_timer_type = self.expired_timer_type(path.rtt(), now);
        }
    }

    fn expired_timer_type(&self, rtt: &RttEstimate, now: Instant) -> Option<qlog::LossTimerType> {
        if self.earliest_loss_time(rtt).is_some_and(|t| t <= now) {
            Some(qlog::LossTimerType::Ack)
        } else if self.earliest_pto(rtt).is_some_and(|t| t <= now) {
            Some(qlog::LossTimerType::Pto)
        } else {
            None
        }
    }

    /// Find when the earliest sent packet should be considered lost.
    fn earliest_loss_time(&self, rtt: &RttEstimate) -> Option<Instant> {
        self.spaces
            .iter()
            .filter_map(LossRecoverySpace::loss_recovery_timer_start)
            .min()
            .map(|val| val + rtt.loss_delay())
    }

    /// Simple wrapper for the PTO calculation that avoids borrow check rules.
    fn pto_period_inner(
        rtt: &RttEstimate,
        pto_state: Option<&PtoState>,
        confirmed: bool,
        fast_pto: u8,
    ) -> Duration {
        // This is a complicated (but safe) way of calculating:
        //   base_pto * F * 2^pto_count
        // where F = fast_pto / FAST_PTO_SCALE (== 1 by default)
        let pto_count = pto_state.map_or(0, |p| u32::try_from(p.count).unwrap_or(0));
        rtt.pto(confirmed)
            .checked_mul(u32::from(fast_pto) << min(pto_count, u32::BITS - u8::BITS))
            .map_or(Duration::from_hours(1), |p| p / u32::from(FAST_PTO_SCALE))
    }

    /// Get the current PTO period for the given packet number space.
    /// Unlike calling `RttEstimate::pto` directly, this includes exponential backoff.
    fn pto_period(&self, rtt: &RttEstimate) -> Duration {
        Self::pto_period_inner(
            rtt,
            self.pto_state.as_ref(),
            self.confirmed(),
            self.fast_pto,
        )
    }

    /// The number of consecutive PTOs that have fired without being acknowledged.
    /// The value is reset to `0` whenever an acknowledgement is received.
    pub(crate) fn pto_count(&self) -> usize {
        self.pto_state.as_ref().map_or(0, PtoState::count)
    }

    // Calculate PTO time for the given space.
    fn pto_time(&self, rtt: &RttEstimate, pn_space: PacketNumberSpace) -> Option<Instant> {
        self.spaces
            .get(pn_space)?
            .pto_base_time()
            .map(|t| t + self.pto_period(rtt))
    }

    /// Find the earliest PTO time for all active packet number spaces.
    /// Ignore Application if either Initial or Handshake have an active PTO.
    fn earliest_pto(&self, rtt: &RttEstimate) -> Option<Instant> {
        if self.confirmed() {
            self.pto_time(rtt, PacketNumberSpace::ApplicationData)
        } else {
            self.pto_time(rtt, PacketNumberSpace::Initial)
                .iter()
                .chain(self.pto_time(rtt, PacketNumberSpace::Handshake).iter())
                .min()
                .copied()
        }
    }

    fn fire_pto(
        &mut self,
        pn_space: PacketNumberSpace,
        allow_probes: PacketNumberSpaceSet,
        rtt: &RttEstimate,
        now: Instant,
    ) {
        if let Some(st) = &mut self.pto_state {
            st.pto(pn_space, allow_probes);
        } else {
            self.pto_state = Some(PtoState::new(pn_space, allow_probes));
        }

        if let Some(st) = &mut self.pto_state {
            st.count_pto(&mut self.stats.borrow_mut());
            qlog::metrics_updated(&mut self.qlog, [qlog::Metric::PtoCount(st.count())], now);
        }
        let pto = self.pto_period(rtt);
        qlog::loss_timer_set(&mut self.qlog, pn_space, pto, now);
    }

    /// This checks whether the PTO timer has fired and fires it if needed.
    /// When it has, mark packets as "lost" for the purposes of having frames
    /// regenerated in subsequent packets.  The packets aren't truly lost, so
    /// we have to clone the `sent::Packet` instance.
    fn maybe_fire_pto(
        &mut self,
        primary_path: &PathRef,
        now: Instant,
        lost: &mut Vec<sent::Packet>,
        has_handshake_keys: bool,
    ) {
        // As in `next_timeout`, which does not arm the timer then (RFC 9002, Section 6.2.2.1).
        if !primary_path.borrow().pto_possible() {
            return;
        }
        let mut pto_space = None;
        // The spaces in which we will allow probing.
        let mut allow_probes = PacketNumberSpaceSet::default();
        // The spaces for which packets should be marked for retransmission.
        let mut retransmit = PacketNumberSpaceSet::default();
        for pn_space in PacketNumberSpace::iter() {
            let Some(t) = self.pto_time(primary_path.borrow().rtt(), pn_space) else {
                continue;
            };
            allow_probes.insert(pn_space);
            if t > now {
                continue;
            }
            debug!("[{self}] PTO timer fired for {pn_space:?}");
            retransmit.insert(pn_space);
            // When Handshake PTO fires, also retransmit Initial CRYPTO data.
            // This handles lost Initial CRYPTO that hasn't triggered its own
            // PTO because `last_ack_eliciting` keeps advancing with each new
            // Initial send.
            if pn_space == PacketNumberSpace::Handshake {
                retransmit.insert(PacketNumberSpace::Initial);
            }
            pto_space = pto_space.or(Some(pn_space));
        }

        // This has to happen outside the loop. Increasing the PTO count here causes the
        // pto_time to increase which might cause PTO for later packet number spaces to not fire.
        let Some(pn_space) = pto_space else {
            return;
        };

        // Collect packets for retransmission.
        let mtu = primary_path.borrow().plpmtu();
        let mut size = 0;
        for space in PacketNumberSpace::iter().filter(|s| retransmit.contains(*s)) {
            let Some(s) = self.spaces.get_mut(space) else {
                continue;
            };
            lost.extend(
                s.pto_packets()
                    .take_while(|p| {
                        size += p.len();
                        size <= MAX_PTO_PACKET_COUNT * mtu
                    })
                    .cloned(),
            );
        }

        trace!("[{self}] PTO {pn_space}, probing {allow_probes:?}");
        self.fire_pto(pn_space, allow_probes, primary_path.borrow().rtt(), now);

        // Maybe prime the Handshake PTO when PTO fires in Initial space.
        if pn_space == PacketNumberSpace::Initial {
            self.maybe_prime_handshake_pto(now, has_handshake_keys);
        }
    }

    pub fn timeout(
        &mut self,
        primary_path: &PathRef,
        now: Instant,
        has_handshake_keys: bool,
    ) -> Vec<sent::Packet> {
        trace!("[{self}] timeout {now:?}");
        if let Some(timer_type) = self
            .pending_timer_type
            .take()
            .or_else(|| self.expired_timer_type(primary_path.borrow().rtt(), now))
        {
            qlog::loss_timer_expired(&mut self.qlog, timer_type, now);
        }

        let loss_delay = primary_path.borrow().rtt().loss_delay();
        let confirmed = self.confirmed();

        let mut lost_packets = Vec::new();
        for space in self.spaces.iter_mut() {
            let first = lost_packets.len(); // The first packet lost in this space.
            let pto = Self::pto_period_inner(
                primary_path.borrow().rtt(),
                self.pto_state.as_ref(),
                confirmed,
                self.fast_pto,
            );
            space.detect_lost_packets(now, loss_delay, pto, &mut lost_packets);

            primary_path.borrow_mut().on_packets_lost(
                space.largest_acked_sent_time,
                confirmed,
                &lost_packets[first..],
                &mut self.stats.borrow_mut(),
                now,
            );
        }
        self.count_lost(&lost_packets);

        self.maybe_fire_pto(primary_path, now, &mut lost_packets, has_handshake_keys);
        lost_packets
    }

    /// Check how packets should be sent, based on whether there is a PTO,
    /// what the current congestion window is, and what the pacer says.
    pub fn send_profile(&mut self, path: &Path, now: Instant) -> SendProfile {
        trace!("[{self}] get send profile {now:?}");
        let sender = path.sender();
        let mtu = path.plpmtu();
        // Probes count against the anti-amplification limit (RFC 9002, Section 6.2.2.1).
        let pto_possible = path.pto_possible();
        if pto_possible
            && let Some(profile) = self
                .pto_state
                .as_mut()
                .and_then(|pto| pto.send_profile(mtu))
        {
            profile
        } else {
            let limit = path.send_budget();
            if limit > mtu {
                // More than an MTU available; we might need to pace.
                if sender
                    .next_paced(path.rtt().estimate())
                    .is_some_and(|t| t > now)
                {
                    SendProfile::new_paced()
                } else {
                    SendProfile::new_limited(mtu, mtu)
                }
            } else if sender.recovery_packet() && pto_possible {
                // After entering recovery, allow a packet to be sent immediately.
                // This uses the PTO machinery, probing in all spaces. This will
                // result in a PING being sent in every active space.
                SendProfile::new_pto(mtu, PacketNumberSpaceSet::all())
            } else {
                SendProfile::new_limited(limit, mtu)
            }
        }
    }
}

impl Display for Loss {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "recovery::Loss")
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
#[allow(
    clippy::allow_attributes,
    clippy::single_range_in_vec_init,
    reason = "Single-range ACK lists are intended."
)]
mod tests {
    use std::{
        cell::RefCell,
        ops::{Deref, DerefMut, RangeInclusive},
        rc::Rc,
        time::{Duration, Instant},
    };

    use neqo_common::{qlog::Qlog, to_u64};
    use test_fixture::{DEFAULT_ADDR, now};

    use super::{
        ACK_ONLY_SIZE_LIMIT, FAST_PTO_SCALE, LossRecoverySpace, MIN_OUTSTANDING_UNACK,
        PacketNumberSpace, PtoState, SendProfile,
    };
    use crate::{
        ConnectionParameters, Token as Srt,
        cid::{ConnectionId, ConnectionIdEntry},
        ecn, packet,
        path::{Path, PathRef},
        recovery::{self, MAX_PTO_PACKET_COUNT, sent},
        stats::{Stats, StatsCell},
        tracking::PacketNumberSpaceSet,
    };

    // Shorthand for a time in milliseconds.
    const fn ms(t: u64) -> Duration {
        Duration::from_millis(t)
    }

    const ON_SENT_SIZE: usize = 100;
    /// An initial RTT for using with `setup_lr`.
    const TEST_RTT: Duration = ms(7000);
    const TEST_RTTVAR: Duration = ms(3500);

    struct Fixture {
        lr: recovery::Loss,
        path: PathRef,
    }

    // This shadows functions on the base object so that the path and RTT estimator
    // is used consistently in the tests.  It also simplifies the function signatures.
    impl Fixture {
        pub fn on_ack_received(
            &mut self,
            pn_space: PacketNumberSpace,
            acked_ranges: Vec<RangeInclusive<packet::Number>>,
            ack_ecn: Option<&ecn::Count>,
            ack_delay: Duration,
            now: Instant,
        ) -> (Vec<sent::Packet>, Vec<sent::Packet>) {
            self.lr
                .on_ack_received(&self.path, pn_space, acked_ranges, ack_ecn, ack_delay, now)
        }

        pub fn on_packet_sent(&mut self, sent_packet: sent::Packet, now: Instant) {
            self.lr.on_packet_sent(&self.path, sent_packet, now);
        }

        pub fn timeout(&mut self, now: Instant) -> Vec<sent::Packet> {
            self.lr.timeout(&self.path, now, true)
        }

        pub fn note_timeout_type(&mut self, now: Instant) {
            self.lr.note_timeout_type(&self.path.borrow(), now);
        }

        pub fn next_timeout(&self) -> Option<Instant> {
            self.lr.next_timeout(&self.path.borrow())
        }

        pub fn discard(&mut self, space: PacketNumberSpace, now: Instant) {
            self.lr.discard(&self.path, space, now);
        }

        pub fn pto_time(&self, space: PacketNumberSpace) -> Option<Instant> {
            self.lr.pto_time(self.path.borrow().rtt(), space)
        }

        pub fn send_profile(&mut self, now: Instant) -> SendProfile {
            self.lr.send_profile(&self.path.borrow(), now)
        }
    }

    impl Default for Fixture {
        fn default() -> Self {
            let stats = StatsCell::default();
            let mut path = Path::temporary(
                DEFAULT_ADDR,
                DEFAULT_ADDR,
                &ConnectionParameters::default(),
                Qlog::default(),
                now(),
            );
            path.make_permanent(
                None,
                ConnectionIdEntry::new(0, ConnectionId::from(&[1, 2, 3]), Srt::default()),
            );
            path.set_primary(true, now());
            path.rtt_mut().set_initial(TEST_RTT);
            Self {
                lr: recovery::Loss::new(stats, FAST_PTO_SCALE),
                path: Rc::new(RefCell::new(path)),
            }
        }
    }

    // Most uses of the fixture only care about the loss recovery piece,
    // but the internal functions need the other bits.
    impl Deref for Fixture {
        type Target = recovery::Loss;
        fn deref(&self) -> &Self::Target {
            &self.lr
        }
    }

    impl DerefMut for Fixture {
        fn deref_mut(&mut self) -> &mut Self::Target {
            &mut self.lr
        }
    }

    fn assert_rtts(
        lr: &Fixture,
        latest_rtt: Duration,
        smoothed_rtt: Duration,
        rttvar: Duration,
        min_rtt: Duration,
    ) {
        let p = lr.path.borrow();
        let rtt = p.rtt();
        println!(
            "rtts: {:?} {:?} {:?} {:?}",
            rtt.latest_rtt(),
            rtt.estimate(),
            rtt.rttvar(),
            rtt.minimum(),
        );
        assert_eq!(rtt.latest_rtt(), latest_rtt, "latest RTT");
        assert_eq!(rtt.estimate(), smoothed_rtt, "smoothed RTT");
        assert_eq!(rtt.rttvar(), rttvar, "RTT variance");
        assert_eq!(rtt.minimum(), min_rtt, "min RTT");
    }

    fn assert_sent_times(
        lr: &Fixture,
        initial: Option<Instant>,
        handshake: Option<Instant>,
        app_data: Option<Instant>,
    ) {
        let est = |sp| {
            lr.spaces
                .get(sp)
                .and_then(LossRecoverySpace::loss_recovery_timer_start)
        };
        println!(
            "loss times: {:?} {:?} {:?}",
            est(PacketNumberSpace::Initial),
            est(PacketNumberSpace::Handshake),
            est(PacketNumberSpace::ApplicationData),
        );
        assert_eq!(
            est(PacketNumberSpace::Initial),
            initial,
            "Initial earliest sent time"
        );
        assert_eq!(
            est(PacketNumberSpace::Handshake),
            handshake,
            "Handshake earliest sent time"
        );
        assert_eq!(
            est(PacketNumberSpace::ApplicationData),
            app_data,
            "AppData earliest sent time"
        );
    }

    fn assert_no_sent_times(lr: &Fixture) {
        assert_sent_times(lr, None, None, None);
    }

    // In most of the tests below, packets are sent at a fixed cadence, with PACING between each.
    const PACING: Duration = ms(7);
    fn pn_time(pn: u64) -> Instant {
        now() + (PACING * pn.try_into().unwrap())
    }

    fn pace(lr: &mut Fixture, count: u64) {
        for pn in 0..count {
            lr.on_packet_sent(
                sent::Packet::new(
                    packet::Type::Short,
                    pn,
                    pn_time(pn),
                    true,
                    recovery::Tokens::new(),
                    ON_SENT_SIZE,
                ),
                now(),
            );
        }
    }

    const ACK_DELAY: Duration = ms(24);
    /// Acknowledge PN with the identified delay.
    fn ack(lr: &mut Fixture, pn: u64, delay: Duration) {
        lr.on_ack_received(
            PacketNumberSpace::ApplicationData,
            vec![pn..=pn],
            None,
            ACK_DELAY,
            pn_time(pn) + delay,
        );
    }

    fn add_sent(lrs: &mut LossRecoverySpace, max_pn: packet::Number) {
        for pn in 0..=max_pn {
            lrs.on_packet_sent(sent::Packet::new(
                packet::Type::Short,
                pn,
                pn_time(pn),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ));
        }
    }

    fn match_acked(acked: &[sent::Packet], expected: &[packet::Number]) {
        assert_eq!(
            acked.iter().map(sent::Packet::pn).collect::<Vec<_>>(),
            expected
        );
    }

    #[test]
    fn remove_acked() {
        let mut lrs = LossRecoverySpace::new(PacketNumberSpace::ApplicationData);
        let mut stats = Stats::default();
        add_sent(&mut lrs, 10);
        let (acked, _) = lrs.remove_acked(vec![], &mut stats);
        assert!(acked.is_empty());
        let (acked, _) = lrs.remove_acked(vec![7..=8, 2..=4], &mut stats);
        match_acked(&acked, &[8, 7, 4, 3, 2]);
        let (acked, _) = lrs.remove_acked(vec![8..=11], &mut stats);
        match_acked(&acked, &[10, 9]);
        let (acked, _) = lrs.remove_acked(vec![0..=2], &mut stats);
        match_acked(&acked, &[1, 0]);
        let (acked, _) = lrs.remove_acked(vec![5..=6], &mut stats);
        match_acked(&acked, &[6, 5]);
    }

    #[test]
    fn initial_rtt() {
        let mut lr = Fixture::default();
        pace(&mut lr, 1);
        let rtt = ms(100);
        ack(&mut lr, 0, rtt);
        assert_rtts(&lr, rtt, rtt, rtt / 2, rtt);
        assert_no_sent_times(&lr);
    }

    /// Send `n` packets (using PACING), then acknowledge the first.
    fn setup_lr(n: u64) -> Fixture {
        let mut lr = Fixture::default();
        pace(&mut lr, n);
        ack(&mut lr, 0, TEST_RTT);
        assert_rtts(&lr, TEST_RTT, TEST_RTT, TEST_RTTVAR, TEST_RTT);
        assert_no_sent_times(&lr);
        lr
    }

    // The ack delay is removed from the smoothed RTT and RTTVAR, but not from the latest RTT.
    #[test]
    fn ack_delay_adjusted() {
        let mut lr = setup_lr(2);
        ack(&mut lr, 1, TEST_RTT + ACK_DELAY);
        // RTT stays the same, but the RTTVAR is adjusted downwards.
        assert_rtts(
            &lr,
            TEST_RTT + ACK_DELAY,
            TEST_RTT,
            TEST_RTTVAR * 3 / 4,
            TEST_RTT,
        );
        assert_no_sent_times(&lr);
    }

    // The ack delay is ignored when it would cause a sample to be less than min_rtt.
    #[test]
    fn ack_delay_ignored() {
        let mut lr = setup_lr(2);
        let extra = ms(8);
        assert!(extra < ACK_DELAY);
        ack(&mut lr, 1, TEST_RTT + extra);
        let expected_rtt = TEST_RTT + (extra / 8);
        let expected_rttvar = (TEST_RTTVAR * 3 + extra) / 4;
        assert_rtts(
            &lr,
            TEST_RTT + extra,
            expected_rtt,
            expected_rttvar,
            TEST_RTT,
        );
        assert_no_sent_times(&lr);
    }

    // A lower observed RTT is used as min_rtt (and ack delay is ignored).
    #[test]
    fn reduce_min_rtt() {
        let mut lr = setup_lr(2);
        let delta = ms(4);
        let reduced_rtt = TEST_RTT.checked_sub(delta).unwrap();
        ack(&mut lr, 1, reduced_rtt);
        let expected_rtt = TEST_RTT.checked_sub(delta / 8).unwrap();
        let expected_rttvar = (TEST_RTTVAR * 3 + delta) / 4;
        assert_rtts(&lr, reduced_rtt, expected_rtt, expected_rttvar, reduced_rtt);
        assert_no_sent_times(&lr);
    }

    // Acknowledging something again has no effect.
    #[test]
    fn no_new_acks() {
        let mut lr = setup_lr(1);
        let check = |lr: &Fixture| {
            assert_rtts(lr, TEST_RTT, TEST_RTT, TEST_RTTVAR, TEST_RTT);
            assert_no_sent_times(lr);
        };
        check(&lr);

        ack(&mut lr, 0, ms(1339)); // much delayed ACK
        check(&lr);

        ack(&mut lr, 0, ms(3)); // time travel!
        check(&lr);
    }

    // Test time loss detection as part of handling a regular ACK.
    #[test]
    fn time_loss_detection_gap() {
        let mut lr = Fixture::default();
        // Create a single packet gap, and have pn 0 time out.
        // This can't use the default pacing, which is too tight.
        // So send two packets with 1/4 RTT between them.  Acknowledge pn 1 after 1 RTT.
        // pn 0 should then be marked lost because it is then outstanding for 5RTT/4
        // the loss time for packets is 9RTT/8.
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Short,
                0,
                pn_time(0),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Short,
                1,
                pn_time(0) + TEST_RTT / 4,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        let (_, lost) = lr.on_ack_received(
            PacketNumberSpace::ApplicationData,
            vec![1..=1],
            None,
            ACK_DELAY,
            pn_time(0) + (TEST_RTT * 5 / 4),
        );
        assert_eq!(lost.len(), 1);
        assert_no_sent_times(&lr);
    }

    // Test time loss detection as part of an explicit timeout.
    #[test]
    fn time_loss_detection_timeout() {
        let mut lr = setup_lr(3);

        // We want to declare PN 2 as acknowledged before we declare PN 1 as lost.
        // For this to work, we need PACING above to be less than 1/8 of an RTT.
        let pn1_sent_time = pn_time(1);
        let pn1_loss_time = pn1_sent_time + (TEST_RTT * 9 / 8);
        let pn2_ack_time = pn_time(2) + TEST_RTT;
        assert!(pn1_loss_time > pn2_ack_time);

        let (_, lost) = lr.on_ack_received(
            PacketNumberSpace::ApplicationData,
            vec![2..=2],
            None,
            ACK_DELAY,
            pn2_ack_time,
        );
        assert!(lost.is_empty());
        // Run the timeout function here to force time-based loss recovery to be enabled.
        let lost = lr.timeout(pn2_ack_time);
        assert!(lost.is_empty());
        assert_sent_times(&lr, None, None, Some(pn1_sent_time));

        // After time elapses, pn 1 is marked lost.
        let callback_time = lr.next_timeout();
        assert_eq!(callback_time, Some(pn1_loss_time));
        let packets = lr.timeout(pn1_loss_time);
        assert_eq!(packets.len(), 1);
        // Checking for expiration with zero delay lets us check the loss time.
        assert!(packets[0].expired(pn1_loss_time, Duration::new(0, 0)));
        assert_no_sent_times(&lr);
    }

    #[test]
    fn big_gap_loss() {
        let mut lr = setup_lr(5); // This sends packets 0-4 and acknowledges pn 0.

        // Acknowledge just 2-4, which will cause pn 1 to be marked as lost.
        assert_eq!(super::PACKET_THRESHOLD, 3);
        let (_, lost) = lr.on_ack_received(
            PacketNumberSpace::ApplicationData,
            vec![2..=4],
            None,
            ACK_DELAY,
            pn_time(4),
        );
        assert_eq!(lost.len(), 1);
    }

    #[test]
    #[should_panic(expected = "discarding application space")]
    fn drop_app() {
        let mut lr = Fixture::default();
        lr.discard(PacketNumberSpace::ApplicationData, now());
    }

    #[test]
    fn ack_after_drop() {
        let mut lr = Fixture::default();
        lr.discard(PacketNumberSpace::Initial, now());
        let (acked, lost) = lr.on_ack_received(
            PacketNumberSpace::Initial,
            vec![],
            None,
            Duration::from_millis(0),
            pn_time(0),
        );
        assert!(acked.is_empty());
        assert!(lost.is_empty());
    }

    #[test]
    fn drop_spaces() {
        let mut lr = Fixture::default();
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                pn_time(0),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Handshake,
                0,
                pn_time(1),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Short,
                0,
                pn_time(2),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );

        // Now put all spaces on the LR timer so we can see them.
        for sp in &[
            packet::Type::Initial,
            packet::Type::Handshake,
            packet::Type::Short,
        ] {
            let sent_pkt = sent::Packet::new(
                *sp,
                1,
                pn_time(3),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            );
            let pn_space = sent_pkt.space();
            lr.on_packet_sent(sent_pkt, now());
            lr.on_ack_received(
                pn_space,
                vec![1..=1],
                None,
                Duration::from_secs(0),
                pn_time(3),
            );
            let mut lost = Vec::new();
            lr.spaces.get_mut(pn_space).unwrap().detect_lost_packets(
                pn_time(3),
                TEST_RTT,
                TEST_RTT * 3, // unused
                &mut lost,
            );
            assert!(lost.is_empty());
        }

        lr.discard(PacketNumberSpace::Initial, pn_time(3));
        assert_sent_times(&lr, None, Some(pn_time(1)), Some(pn_time(2)));

        lr.discard(PacketNumberSpace::Handshake, pn_time(3));
        assert_sent_times(&lr, None, None, Some(pn_time(2)));

        // There are cases where we send a packet that is not subsequently tracked.
        // So check that this works.
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                pn_time(3),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        assert_sent_times(&lr, None, None, Some(pn_time(2)));
    }

    #[test]
    fn rearm_pto_after_confirmed() {
        let mut lr = Fixture::default();
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                now(),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        // Set the RTT to the initial value so that discarding doesn't
        // alter the estimate.
        let rtt = lr.path.borrow().rtt().estimate();
        lr.on_ack_received(
            PacketNumberSpace::Initial,
            vec![0..=0],
            None,
            Duration::new(0, 0),
            now() + rtt,
        );

        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Handshake,
                0,
                now(),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Short,
                0,
                now(),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );

        assert!(lr.pto_time(PacketNumberSpace::ApplicationData).is_some());
        lr.discard(PacketNumberSpace::Initial, pn_time(1));
        assert!(lr.pto_time(PacketNumberSpace::ApplicationData).is_some());

        // Expiring state after the PTO on the ApplicationData space has
        // expired should result in setting a PTO state.
        let default_pto = lr.path.borrow().rtt().pto(true);
        let expected_pto = pn_time(2) + default_pto;
        lr.discard(PacketNumberSpace::Handshake, expected_pto);
        let profile = lr.send_profile(now());
        assert!(!profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(profile.should_probe(PacketNumberSpace::ApplicationData));
    }

    #[test]
    fn no_pto_if_amplification_limited() {
        let mut lr = Fixture::default();
        // Eat up the amplification limit by telling the path that we've sent a giant packet.
        {
            const SPARE: usize = 10;
            let mut path = lr.path.borrow_mut();
            let limit = path.amplification_limit();
            path.add_sent(limit - SPARE);
            assert_eq!(path.amplification_limit(), SPARE);
        }

        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                now(),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );

        let handshake_pto = lr.path.borrow().rtt().pto(false);
        let expected_pto = now() + handshake_pto;
        assert_eq!(lr.pto_time(PacketNumberSpace::Initial), Some(expected_pto));
        let profile = lr.send_profile(now());
        assert!(profile.ack_only());
        assert!(!profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));
    }

    /// Confirm that a PTO in two spaces leads to probes in both.
    #[test]
    fn pto_two_spaces() {
        let mut lr = Fixture::default();
        let now = now();
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                now,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now,
        );
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Handshake,
                0,
                now,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now,
        );

        let handshake_pto = lr.path.borrow().rtt().pto(false);
        let expected_pto = now + handshake_pto;
        assert_eq!(lr.pto_time(PacketNumberSpace::Initial), Some(expected_pto));
        assert_eq!(
            lr.pto_time(PacketNumberSpace::Handshake),
            Some(expected_pto)
        );

        // After a PTO, sent packet should be marked "lost" (not really)
        // so that they can be sent again.
        let now = expected_pto;
        let lost = lr.timeout(now);
        assert_eq!(2, lost.len());
        assert!(
            lost.iter()
                .any(|x| x.packet_type() == packet::Type::Initial)
        );
        assert!(
            lost.iter()
                .any(|x| x.packet_type() == packet::Type::Handshake)
        );

        // The resulting send profile should probe spaces where packets were "lost".
        let profile = lr.send_profile(now);
        assert!(profile.should_probe(PacketNumberSpace::Initial));
        assert!(profile.should_probe(PacketNumberSpace::Handshake));
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));

        // Sending a packet clears the probe bit for that space.
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Handshake,
                1,
                now,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now,
        );
        let profile = lr.send_profile(now);
        assert!(profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake)); // changed
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));

        assert_eq!(2, MAX_PTO_PACKET_COUNT); // because we're relying on that...
        let profile = lr.send_profile(now);
        // After probing enough, all probe bits should be cleared.
        assert!(!profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));
    }

    /// Confirm that a PTO in two spaces leads to probes in both, staggered.
    #[test]
    fn pto_two_spaces_staggered() {
        let mut lr = Fixture::default();
        let start_time = now();
        let now = start_time;
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                now,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now,
        );

        let initial_pto = now + lr.path.borrow().rtt().pto(false);
        assert_eq!(lr.pto_time(PacketNumberSpace::Initial), Some(initial_pto));
        assert!(lr.pto_time(PacketNumberSpace::ApplicationData).is_none());

        // A PTO results in the profile including Initial.
        let now = initial_pto;
        let _lost = lr.timeout(now);
        let profile = lr.send_profile(now);
        assert!(profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));

        // Sending and timing out a short header packet...
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Short,
                0,
                now,
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now,
        );

        // The PTO time is doubled.  But the app PTO is relative to its send time.
        let two_pto = 2 * lr.path.borrow().rtt().pto(false);
        let initial_pto2 = start_time + two_pto;
        let app_pto = now + two_pto;
        assert_eq!(lr.pto_time(PacketNumberSpace::Initial), Some(initial_pto2));
        assert_eq!(
            lr.pto_time(PacketNumberSpace::ApplicationData),
            Some(app_pto)
        );

        // A second PTO resets the count.
        let now = app_pto;
        let _lost = lr.timeout(now);
        let profile = lr.send_profile(now);
        assert!(profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(profile.should_probe(PacketNumberSpace::ApplicationData));

        // This is the second and the Initial space still hasn't been probed.
        let profile = lr.send_profile(now);
        assert!(profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(profile.should_probe(PacketNumberSpace::ApplicationData));

        // The PTO is now done.
        assert_eq!(2, MAX_PTO_PACKET_COUNT); // because we're relying on that...
        let profile = lr.send_profile(now);
        // After probing enough, all probe bits should be cleared.
        assert!(!profile.should_probe(PacketNumberSpace::Initial));
        assert!(!profile.should_probe(PacketNumberSpace::Handshake));
        assert!(!profile.should_probe(PacketNumberSpace::ApplicationData));
    }

    fn assert_no_handshake_last_ack_eliciting(lr: &Fixture) {
        assert!(
            lr.spaces
                .get(PacketNumberSpace::Handshake)
                .and_then(|s| s.last_ack_eliciting)
                .is_none()
        );
    }

    #[test]
    fn maybe_prime_handshake_pto_no_keys() {
        let mut lr = Fixture::default();
        let probe_set = PacketNumberSpaceSet::only(PacketNumberSpace::Initial);
        lr.pto_state = Some(PtoState::new(PacketNumberSpace::Initial, probe_set));
        lr.spaces
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .largest_acked = Some(0);

        lr.maybe_prime_handshake_pto(now(), false);
        assert_no_handshake_last_ack_eliciting(&lr);
    }

    #[test]
    fn maybe_prime_handshake_pto_no_pto_state() {
        let mut lr = Fixture::default();
        assert!(lr.pto_state.is_none());

        // Verify nothing changes - the Handshake space should not be primed afterwards.
        lr.maybe_prime_handshake_pto(now(), true);
        assert_no_handshake_last_ack_eliciting(&lr);
    }

    #[test]
    fn maybe_prime_handshake_pto_wrong_space() {
        // Create a PTO state in Handshake space.
        let mut lr = Fixture::default();
        let probe_set = PacketNumberSpaceSet::only(PacketNumberSpace::Handshake);
        lr.pto_state = Some(PtoState::new(PacketNumberSpace::Handshake, probe_set));

        // Verify nothing changes - the Handshake space should not be primed afterwards.
        lr.maybe_prime_handshake_pto(now(), true);
        assert_no_handshake_last_ack_eliciting(&lr);
    }

    #[test]
    fn maybe_prime_handshake_pto_no_handshake_space() {
        // Create a PTO state in Initial space.
        let mut lr = Fixture::default();
        let probe_set = PacketNumberSpaceSet::only(PacketNumberSpace::Initial);
        lr.pto_state = Some(PtoState::new(PacketNumberSpace::Initial, probe_set));

        // Set up Initial space with an ACK and drop Handshake space.
        lr.spaces
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .largest_acked = Some(0);
        lr.spaces.drop_space(PacketNumberSpace::Handshake);

        // Verify Handshake space still doesn't exist afterwards.
        lr.maybe_prime_handshake_pto(now(), true);
        assert!(lr.spaces.get(PacketNumberSpace::Handshake).is_none());
    }

    #[test]
    fn loss_display() {
        let lr = Fixture::default();
        assert_eq!(lr.to_string(), "recovery::Loss");
    }

    #[test]
    fn pto_state_count() {
        let probe_set = PacketNumberSpaceSet::only(PacketNumberSpace::Initial);
        let mut pto = PtoState::new(PacketNumberSpace::Initial, probe_set);
        assert_eq!(pto.count(), 1);
        pto.pto(PacketNumberSpace::Initial, probe_set);
        assert_eq!(pto.count(), 2);
    }

    #[test]
    fn send_profile_ack_only() {
        let profile = SendProfile::new_limited(1200, 1200);
        assert!(!profile.ack_only());
        assert_eq!(profile.limit(), 1200);
        assert!(!profile.paced());
        assert!(profile.full_mtu());
        assert!(!SendProfile::new_limited(1199, 1200).full_mtu());

        let paced = SendProfile::new_paced();
        assert!(paced.ack_only());
        assert!(paced.paced());
        assert!(!paced.full_mtu());

        let pto = SendProfile::new_pto(
            1200,
            PacketNumberSpaceSet::only(PacketNumberSpace::Handshake),
        );
        // All spaces can send data frames during PTO (not just ACKs).
        // This allows retransmission of lost CRYPTO in earlier spaces.
        assert!(!pto.ack_only());
        assert!(pto.full_mtu());
        assert!(pto.should_probe(PacketNumberSpace::Handshake));
        assert!(!pto.should_probe(PacketNumberSpace::Initial));
    }

    /// Test that Initial space can retransmit CRYPTO even when PTO fires for Handshake.
    ///
    /// RFC 9002 Section 6.2.4 requires sending probes in packet number spaces with
    /// in-flight data. When the client has lost Initial CRYPTO data and PTO fires
    /// for Handshake space (to prevent deadlocks), the client must still be able
    /// to retransmit the lost Initial CRYPTO frames.
    ///
    /// Bug scenario (from QNS L1/C1 test failures):
    /// 1. Client sends `ClientHello` split across Initial packets (e.g., pn=8, pn=9)
    /// 2. Server receives pn=8 but pn=9 is lost/corrupted
    /// 3. Server ACKs pn=8; client detects pn=9 as lost
    /// 4. PTO fires for Handshake (primed to prevent deadlocks)
    /// 5. BUG: `ack_only(Initial)` returns true, blocking CRYPTO retransmission
    /// 6. Client cannot complete handshake, times out
    #[test]
    fn initial_crypto_retransmit_allowed_during_handshake_pto() {
        // When PTO fires for Handshake but Initial space has lost CRYPTO data,
        // the Initial space should NOT be restricted to ACK-only.
        let pto = SendProfile::new_pto(
            1200,
            PacketNumberSpaceSet::only(PacketNumberSpace::Handshake),
        );
        assert!(
            !pto.ack_only(),
            "Initial space must be able to send CRYPTO frames even when PTO is for Handshake"
        );
    }

    /// Set up a qlog-instrumented fixture with a PTO already fired.
    /// Returns the log contents and the PTO expiry time for use in follow-on
    /// operations (e.g., acknowledging packets to trigger Cancelled).
    fn fire_pto_log() -> (Fixture, test_fixture::SharedVec, Instant) {
        let (log, contents) = test_fixture::new_neqo_qlog();
        let mut lr = Fixture::default();
        lr.lr.set_qlog(log);
        lr.on_packet_sent(
            sent::Packet::new(
                packet::Type::Initial,
                0,
                now(),
                true,
                recovery::Tokens::new(),
                ON_SENT_SIZE,
            ),
            now(),
        );
        let pto = lr.next_timeout().expect("PTO timer armed");
        lr.timeout(pto);
        (lr, contents, pto)
    }

    fn send_non_ack_eliciting(lrs: &mut LossRecoverySpace, pkt_type: packet::Type) {
        lrs.on_packet_sent(sent::Packet::new(
            pkt_type,
            0,
            now(),
            false,
            recovery::Tokens::new(),
            ON_SENT_SIZE,
        ));
    }

    /// Non-ACK-eliciting packets in Initial/Handshake spaces set the PTO baseline,
    /// but non-ACK-eliciting packets in `ApplicationData` space do not.
    #[test]
    fn pto_baseline_set_for_non_app_data_only() {
        let mut lrs_init = LossRecoverySpace::new(PacketNumberSpace::Initial);
        assert!(lrs_init.last_ack_eliciting.is_none());
        send_non_ack_eliciting(&mut lrs_init, packet::Type::Initial);
        assert!(
            lrs_init.last_ack_eliciting.is_some(),
            "Initial space must set PTO baseline for non-ack-eliciting packet"
        );

        let mut lrs_app = LossRecoverySpace::new(PacketNumberSpace::ApplicationData);
        send_non_ack_eliciting(&mut lrs_app, packet::Type::Short);
        assert!(
            lrs_app.last_ack_eliciting.is_none(),
            "ApplicationData must not set PTO baseline for non-ack-eliciting packet"
        );
    }

    fn app_data_largest_acked_sent_time(lr: &Fixture) -> Option<Instant> {
        lr.spaces
            .get(PacketNumberSpace::ApplicationData)?
            .largest_acked_sent_time
    }

    /// A duplicate ACK for the current largest acknowledged packet must not update the sent-time.
    #[test]
    fn duplicate_ack_does_not_update_largest_acked_sent_time() {
        let mut lr = setup_lr(3); // sends 0..=2 and acks 0

        ack(&mut lr, 2, TEST_RTT);
        let first_sent_time = app_data_largest_acked_sent_time(&lr);
        assert!(first_sent_time.is_some());

        ack(&mut lr, 2, TEST_RTT);
        assert_eq!(
            app_data_largest_acked_sent_time(&lr),
            first_sent_time,
            "duplicate ACK must not update largest_acked_sent_time"
        );
    }

    /// At the exact PTO expiry deadline, probing should not yet fire; one nanosecond past it
    /// should.
    #[test]
    fn should_probe_exact_boundary() {
        let mut lrs = LossRecoverySpace::new(PacketNumberSpace::ApplicationData);
        let t = now();
        let pto = ms(100);

        // Add exactly MIN_OUTSTANDING_UNACK packets → n_pto = 2.
        add_sent(&mut lrs, to_u64(MIN_OUTSTANDING_UNACK - 1));
        assert_eq!(lrs.sent_packets.len(), MIN_OUTSTANDING_UNACK);

        lrs.last_ack_eliciting = Some(t);

        // At exactly t + pto*2: not yet past the deadline, should NOT probe.
        assert!(!lrs.should_probe(pto, t + pto * 2));
        // One nanosecond past the deadline: should probe.
        assert!(lrs.should_probe(pto, t + pto * 2 + Duration::from_nanos(1)));
    }

    /// `ack_only` is true only when `limit < ACK_ONLY_SIZE_LIMIT`, not at the limit itself.
    #[test]
    fn ack_only_boundary() {
        assert!(SendProfile::new_limited(ACK_ONLY_SIZE_LIMIT - 1, 1200).ack_only());
        // At the limit itself: limit == ACK_ONLY_SIZE_LIMIT → NOT ack_only.
        assert!(!SendProfile::new_limited(ACK_ONLY_SIZE_LIMIT, 1200).ack_only());
        assert!(!SendProfile::new_limited(ACK_ONLY_SIZE_LIMIT + 1, 1200).ack_only());
    }

    /// `drop_0rtt` returns packets that were in-flight in the `ApplicationData` space.
    #[test]
    fn drop_0rtt_returns_sent_packets() {
        let mut lr = Fixture::default();
        pace(&mut lr, 2);
        let path = Rc::clone(&lr.path);
        let dropped = lr.drop_0rtt(&path, now());
        assert_eq!(
            dropped.len(),
            2,
            "drop_0rtt must return all in-flight ApplicationData-space packets"
        );
    }

    #[test]
    #[should_panic(expected = "discarding application space")]
    fn discard_application_data_panics() {
        let mut lr = Fixture::default();
        lr.discard(PacketNumberSpace::ApplicationData, now());
    }

    /// `drop_0rtt` returns empty when packets have already been acknowledged.
    #[test]
    fn drop_0rtt_already_acked() {
        let mut lr = setup_lr(2); // sends packets 0..=1 and ACKs packet 0
        ack(&mut lr, 1, TEST_RTT); // ACK packet 1 — sets largest_acked
        let path = Rc::clone(&lr.path);
        assert!(lr.drop_0rtt(&path, now()).is_empty());
    }

    #[test]
    fn loss_timer_set_on_pto() {
        let (_, contents, _) = fire_pto_log();
        let log = contents.to_string();
        assert!(
            log.contains(r#""event_type":"set""#),
            "Expected loss_timer_updated Set event in qlog: {log}"
        );
        assert!(
            log.contains(r#""timer_type":"pto""#),
            "Expected timer_type pto in qlog: {log}"
        );
    }

    #[test]
    fn loss_timer_expired_on_timeout() {
        let (_, contents, _) = fire_pto_log();
        let log = contents.to_string();
        assert!(
            log.contains(r#""event_type":"expired""#),
            "Expected loss_timer_updated Expired event in qlog: {log}"
        );
    }

    #[test]
    fn loss_timer_cancelled_on_ack() {
        let (mut lr, contents, pto) = fire_pto_log();
        lr.on_ack_received(
            PacketNumberSpace::Initial,
            vec![0..=0],
            None,
            Duration::ZERO,
            pto + TEST_RTT,
        );
        let log = contents.to_string();
        assert!(
            log.contains(r#""event_type":"cancelled""#),
            "Expected loss_timer_updated Cancelled event in qlog: {log}"
        );
    }

    #[test]
    fn note_timeout_type_survives_ack() {
        let (log, contents) = test_fixture::new_neqo_qlog();
        let mut lr = Fixture::default();
        lr.lr.set_qlog(log);

        pace(&mut lr, 3);

        // ACK PN 0 to establish RTT, then ACK PN 2 — PN 1 becomes a loss
        // candidate with a time-based loss timer.
        ack(&mut lr, 0, TEST_RTT);
        ack(&mut lr, 2, TEST_RTT);
        lr.timeout(pn_time(2) + TEST_RTT);

        let pn1_loss_time = pn_time(1) + (TEST_RTT * 9 / 8);
        assert_eq!(lr.next_timeout(), Some(pn1_loss_time));

        // Snapshot the Ack timer type, then ACK PN 1 to clear the loss candidate.
        lr.note_timeout_type(pn1_loss_time);
        ack(&mut lr, 1, TEST_RTT * 9 / 8);

        // timeout() should use the snapshot (Ack), not recompute (would be Pto).
        lr.timeout(pn1_loss_time);
        drop(lr);

        let log = contents.to_string();
        assert!(
            log.contains(r#""timer_type":"ack""#),
            "Expected timer_type ack from snapshot, got: {log}"
        );
    }
}
