// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// A collection for sent packets.

use std::{
    collections::VecDeque,
    ops::RangeInclusive,
    time::{Duration, Instant},
};

use crate::{packet, recovery, tracking::PacketNumberSpace};

/// The reason a packet was declared lost.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LossTrigger {
    TimeThreshold,
    ReorderingThreshold,
}

/// Information recorded when a packet is declared lost.
#[derive(Debug, Clone, Copy)]
pub struct LossInfo {
    pub time: Instant,
    pub trigger: LossTrigger,
}

#[derive(Debug, Clone)]
#[expect(
    clippy::struct_excessive_bools,
    reason = "Independent per-packet flags."
)]
pub struct Packet {
    pt: packet::Type,
    pn: packet::Number,
    ack_eliciting: bool,
    padded: bool,
    time_sent: Instant,
    primary_path: bool,
    tokens: recovery::Tokens,

    loss_info: Option<LossInfo>,
    /// After a PTO, this is true when the packet has been released.
    pto: bool,

    len: usize,
}

impl Packet {
    #[must_use]
    pub const fn new(
        pt: packet::Type,
        pn: packet::Number,
        time_sent: Instant,
        ack_eliciting: bool,
        tokens: recovery::Tokens,
        len: usize,
    ) -> Self {
        Self {
            pt,
            pn,
            time_sent,
            ack_eliciting,
            padded: false,
            primary_path: true,
            tokens,
            loss_info: None,
            pto: false,
            len,
        }
    }

    /// Mark the packet as containing PADDING, which puts it in flight.
    #[must_use]
    pub const fn with_padding(mut self, padded: bool) -> Self {
        self.padded = padded;
        self
    }

    /// The type of this packet.
    #[must_use]
    pub const fn packet_type(&self) -> packet::Type {
        self.pt
    }

    /// The packet number space of this packet.
    #[must_use]
    pub fn space(&self) -> PacketNumberSpace {
        PacketNumberSpace::from(self.pt)
    }

    /// The number of the packet.
    #[must_use]
    pub const fn pn(&self) -> packet::Number {
        self.pn
    }

    /// The ECN mark of the packet.
    #[must_use]
    pub fn ecn_marked_ect0(&self) -> bool {
        self.tokens
            .iter()
            .any(|t| matches!(t, recovery::Token::EcnEct0))
    }

    /// Returns `true` if this packet is a PMTUD probe for the given IP MTU.
    #[must_use]
    pub fn is_pmtud_probe_of(&self, mtu: usize) -> bool {
        self.tokens
            .iter()
            .any(|t| matches!(t, recovery::Token::PmtudProbe(probe_mtu) if *probe_mtu == mtu))
    }

    /// Returns `true` if this packet is a PMTUD probe.
    #[must_use]
    pub fn is_pmtud_probe(&self) -> bool {
        self.tokens
            .iter()
            .any(|t| matches!(t, recovery::Token::PmtudProbe(_)))
    }

    /// The time that this packet was sent.
    #[must_use]
    pub const fn time_sent(&self) -> Instant {
        self.time_sent
    }

    /// Returns `true` if the packet will elicit an ACK.
    #[must_use]
    pub const fn ack_eliciting(&self) -> bool {
        self.ack_eliciting
    }

    /// Returns `true` if the packet was sent on the primary path.
    #[must_use]
    pub const fn on_primary_path(&self) -> bool {
        self.primary_path
    }

    /// The length of the packet that was sent.
    #[allow(
        clippy::allow_attributes,
        clippy::len_without_is_empty,
        reason = "OK here."
    )]
    #[must_use]
    pub const fn len(&self) -> usize {
        self.len
    }

    /// Access the recovery tokens that this holds.
    #[must_use]
    pub const fn tokens(&self) -> &recovery::Tokens {
        &self.tokens
    }

    /// Clears the flag that had this packet on the primary path.
    /// Used when migrating to clear out state.
    pub const fn clear_primary_path(&mut self) {
        self.primary_path = false;
    }

    /// For Initial packets, it is possible that the packet builder needs to amend the length.
    pub fn track_padding(&mut self, padding: usize) {
        debug_assert_eq!(self.pt, packet::Type::Initial);
        self.len += padding;
    }

    /// Whether the packet has been declared lost.
    #[must_use]
    pub const fn lost(&self) -> bool {
        self.loss_info.is_some()
    }

    /// Whether accounting for the loss or acknowledgement in the
    /// congestion controller is pending.
    /// Returns `true` if the packet counts as being "in flight",
    /// and has not previously been declared lost.
    #[must_use]
    pub const fn cc_outstanding(&self) -> bool {
        self.cc_in_flight() && !self.lost()
    }

    /// Whether the packet should be tracked as in-flight.
    #[must_use]
    pub const fn cc_in_flight(&self) -> bool {
        (self.ack_eliciting() || self.padded) && self.on_primary_path()
    }

    /// Declare the packet as lost with the given trigger.  Returns `true` if
    /// this is the first time.
    pub const fn declare_lost(&mut self, now: Instant, trigger: LossTrigger) -> bool {
        if self.lost() {
            false
        } else {
            self.loss_info = Some(LossInfo { time: now, trigger });
            true
        }
    }

    /// Ask whether this tracked packet has been declared lost for long enough
    /// that it can be expired and no longer tracked.
    #[must_use]
    pub fn expired(&self, now: Instant, expiration_period: Duration) -> bool {
        self.loss_info
            .is_some_and(|info| (info.time + expiration_period) <= now)
    }

    /// Whether the packet contents were cleared out after a PTO.
    #[must_use]
    pub const fn pto_fired(&self) -> bool {
        self.pto
    }

    /// Loss information recorded when this packet was declared lost.
    #[must_use]
    pub const fn loss_info(&self) -> Option<LossInfo> {
        self.loss_info
    }

    /// On PTO, we need to get the recovery tokens so that we can ensure that
    /// the frames we sent can be sent again in the PTO packet(s).  Do that just once.
    #[must_use]
    pub const fn pto(&mut self) -> bool {
        if self.pto || self.lost() {
            false
        } else {
            self.pto = true;
            true
        }
    }
}

/// A collection for packets that we have sent that haven't been acknowledged.
#[derive(Debug, Default)]
pub struct Packets {
    /// The collection.
    packets: VecDeque<Packet>,
}

impl Packets {
    #[allow(
        clippy::allow_attributes,
        clippy::len_without_is_empty,
        reason = "OK here."
    )]
    #[must_use]
    pub fn len(&self) -> usize {
        self.packets.len()
    }

    pub fn track(&mut self, packet: Packet) {
        debug_assert!(
            self.packets.back().is_none_or(|last| last.pn < packet.pn),
            "packet numbers must be monotonically increasing"
        );
        self.packets.push_back(packet);
    }

    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut Packet> {
        self.packets.iter_mut()
    }

    /// Take values from specified ranges of packet numbers.
    /// The values returned will be reversed, so that the most recent packet appears first.
    /// This is because ACK frames arrive with ranges starting from the largest acknowledged
    /// and we want to match that.
    pub fn take_ranges<R>(&mut self, acked_ranges: R) -> Vec<Packet>
    where
        R: IntoIterator<Item = RangeInclusive<packet::Number>>,
    {
        let mut result = Vec::new();

        // According to RFC 9000 §19.3.1 ACK ranges are in descending order:
        //
        // > Each ACK Range consists of alternating Gap and ACK Range Length
        // > values in **descending packet number order**.
        //
        // <https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.1>
        let mut previous_range_start: Option<packet::Number> = None;

        for range in acked_ranges {
            debug_assert!(
                previous_range_start.is_none_or(|s| s > *range.end()),
                "ACK ranges must be in descending order per RFC 9000 \u{a7}19.3.1"
            );
            previous_range_start = Some(*range.start());

            let start_idx = self.packets.partition_point(|p| p.pn < *range.start());
            let end_idx = self.packets.partition_point(|p| p.pn <= *range.end());
            if start_idx == end_idx {
                continue;
            }
            result.extend(self.packets.drain(start_idx..end_idx).rev());
        }
        result
    }

    /// Empty out all tracked packets.
    pub fn drain_all(&mut self) -> impl Iterator<Item = Packet> + use<> {
        std::mem::take(&mut self.packets).into_iter()
    }

    /// See `LossRecoverySpace::remove_old_lost` for details on `now` and `cd`.
    /// Returns the number of ack-eliciting packets removed.
    pub fn remove_expired(&mut self, now: Instant, cd: Duration) -> usize {
        if self.packets.front().is_none_or(|p| !p.expired(now, cd)) {
            return 0;
        }
        let keep_from = self.packets.partition_point(|p| p.expired(now, cd));
        debug_assert!(self.packets.range(keep_from..).all(|p| !p.expired(now, cd)));
        self.packets
            .drain(..keep_from)
            .filter(Packet::ack_eliciting)
            .count()
    }
}

/// Test helper to create a sent packet.
#[cfg(test)]
#[must_use]
pub const fn make_packet(pn: packet::Number, sent_time: Instant, len: usize) -> Packet {
    Packet::new(
        packet::Type::Short,
        pn,
        sent_time,
        true,
        recovery::Tokens::new(),
        len,
    )
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
        cell::OnceCell,
        time::{Duration, Instant},
    };

    use super::{LossTrigger, Packet, Packets};
    use crate::{packet, recovery};

    const PACKET_GAP: Duration = Duration::from_secs(1);
    fn start_time() -> Instant {
        thread_local!(static STARTING_TIME: OnceCell<Instant> = const { OnceCell::new() });
        #[expect(
            clippy::disallowed_methods,
            reason = "Special time handling in this test"
        )]
        STARTING_TIME.with(|t| *t.get_or_init(Instant::now))
    }

    fn pkt(n: u32) -> Packet {
        Packet::new(
            packet::Type::Short,
            packet::Number::from(n),
            start_time() + (PACKET_GAP * n),
            true,
            recovery::Tokens::new(),
            100,
        )
    }

    fn pkts() -> Packets {
        let mut pkts = Packets::default();
        pkts.track(pkt(0));
        pkts.track(pkt(1));
        pkts.track(pkt(2));
        assert_eq!(pkts.len(), 3);
        pkts
    }

    trait HasPacketNumber {
        fn pn(&self) -> packet::Number;
    }
    impl HasPacketNumber for Packet {
        fn pn(&self) -> packet::Number {
            self.pn
        }
    }
    impl HasPacketNumber for &'_ mut Packet {
        fn pn(&self) -> packet::Number {
            self.pn
        }
    }

    fn remove_one(pkts: &mut Packets, idx: packet::Number) {
        let store = pkts.take_ranges([idx..=idx]);
        let mut it = store.into_iter();
        assert_eq!(idx, it.next().unwrap().pn());
        assert!(it.next().is_none());
        drop(it);
    }

    fn assert_zero_and_two<'a, 'b: 'a>(
        mut it: impl Iterator<Item = impl HasPacketNumber + 'b> + 'a,
    ) {
        assert_eq!(it.next().unwrap().pn(), 0);
        assert_eq!(it.next().unwrap().pn(), 2);
        assert!(it.next().is_none());
    }

    #[test]
    fn iterate() {
        let mut pkts = pkts();
        for (i, p) in pkts.iter_mut().enumerate() {
            assert_eq!(i, usize::try_from(p.pn).unwrap());
        }
        remove_one(&mut pkts, 1);

        assert_zero_and_two(pkts.iter_mut());
    }

    #[test]
    fn drain() {
        let mut pkts = pkts();
        remove_one(&mut pkts, 1);

        assert_zero_and_two(pkts.drain_all());
        assert_eq!(pkts.len(), 0);
    }

    #[test]
    fn remove_expired() {
        let mut pkts = pkts();
        remove_one(&mut pkts, 0);

        for p in pkts.iter_mut() {
            p.declare_lost(p.time_sent, LossTrigger::TimeThreshold); // just to keep things simple.
        }

        // Expire up to pkt(1).
        let count = pkts.remove_expired(start_time() + PACKET_GAP, Duration::new(0, 0));
        assert_eq!(count, 1);
        assert_eq!(pkts.len(), 1);
    }

    #[test]
    fn first_skipped_ok() {
        let mut pkts = Packets::default();
        pkts.track(pkt(4)); // This is fine.
        assert_eq!(pkts.len(), 1);
    }

    #[test]
    fn ignore_unknown() {
        let mut pkts = Packets::default();
        pkts.track(pkt(0));
        assert!(pkts.take_ranges([1..=1]).is_empty());
    }

    /// Verify `take_ranges` with multiple non-contiguous ranges and multi-packet
    /// spans.  This exercises the trickiest code path: elements from several
    /// disjoint intervals must be removed and returned in descending pn order
    /// while the remaining elements stay in order.
    #[test]
    fn take_ranges_multi() {
        // Build pkt(0)..=pkt(5).
        let mut pkts = Packets::default();
        for i in 0..6 {
            pkts.track(pkt(i));
        }
        // ACK ranges [4..=5, 1..=2] in descending order (as per RFC 9000 §19.3.1).
        let acked = pkts.take_ranges([4..=5, 1..=2]);

        // Returned in largest-pn-first order: 5, 4, 2, 1.
        let pns: Vec<u32> = acked.iter().map(|p| u32::try_from(p.pn).unwrap()).collect();
        assert_eq!(pns, [5, 4, 2, 1]);

        // Remaining tracked: 0 and 3, in order.
        let remaining: Vec<u32> = pkts
            .iter_mut()
            .map(|p| u32::try_from(p.pn).unwrap())
            .collect();
        assert_eq!(remaining, [0, 3]);
    }

    #[test]
    fn pto() {
        let mut p = pkt(0);
        assert!(!p.pto_fired());
        assert!(p.pto()); // First call returns true
        assert!(p.pto_fired());
        assert!(!p.pto()); // Second call returns false
    }

    #[test]
    fn pto_after_lost() {
        let mut p = pkt(0);
        p.declare_lost(start_time(), LossTrigger::TimeThreshold);
        assert!(!p.pto()); // Lost packet returns false
    }

    #[test]
    fn padding_counts_in_flight() {
        let p = Packet::new(
            packet::Type::Short,
            0,
            start_time(),
            false,
            recovery::Tokens::new(),
            100,
        );
        assert!(!p.cc_in_flight());
        let p = p.with_padding(true);
        assert!(p.cc_in_flight());
        assert!(p.cc_outstanding());
    }

    #[test]
    fn loss_info_default() {
        let p = pkt(0);
        assert!(p.loss_info().is_none());
    }

    #[test]
    fn loss_info_declared() {
        let t = start_time();
        let mut p = pkt(0);
        assert!(p.declare_lost(t, LossTrigger::TimeThreshold));
        let info = p.loss_info().unwrap();
        assert_eq!(info.time, t);
        assert_eq!(info.trigger, LossTrigger::TimeThreshold);

        // Second declaration is ignored.
        assert!(!p.declare_lost(t, LossTrigger::ReorderingThreshold));
        assert_eq!(p.loss_info().unwrap().trigger, LossTrigger::TimeThreshold);
    }
}
