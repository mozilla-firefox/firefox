// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Tracking of received packets and generating ACKs thereof.

use std::{
    cmp::min,
    collections::VecDeque,
    fmt::{self, Display, Formatter},
    time::{Duration, Instant},
};

use enum_map::{Enum, EnumMap};
use enumset::{EnumSet, EnumSetType};
use neqo_common::{Buffer, Ecn, Encoder, MAX_VARINT, Reservation};
use nss::Epoch;
use strum::{Display, EnumIter};
use tracing::{debug, trace, warn};

use crate::{
    Error, Res, Stats, ecn,
    frame::{FrameEncoder as _, FrameType},
    packet, recovery,
    stats::FrameStats,
};

#[derive(Debug, PartialOrd, Ord, EnumSetType, Enum, EnumIter, Display)]
pub enum PacketNumberSpace {
    #[strum(to_string = "in")]
    Initial,
    #[strum(to_string = "hs")]
    Handshake,
    #[strum(to_string = "ap")]
    ApplicationData,
}

impl From<Epoch> for PacketNumberSpace {
    fn from(epoch: Epoch) -> Self {
        match epoch {
            Epoch::Initial => Self::Initial,
            Epoch::Handshake => Self::Handshake,
            Epoch::ApplicationData | Epoch::ZeroRtt => Self::ApplicationData,
        }
    }
}

impl From<PacketNumberSpace> for Epoch {
    fn from(val: PacketNumberSpace) -> Self {
        match val {
            PacketNumberSpace::Initial => Self::Initial,
            PacketNumberSpace::Handshake => Self::Handshake,
            PacketNumberSpace::ApplicationData => Self::ApplicationData,
        }
    }
}

#[expect(clippy::fallible_impl_from, reason = "OK here.")]
impl From<packet::Type> for PacketNumberSpace {
    fn from(pt: packet::Type) -> Self {
        match pt {
            packet::Type::Initial => Self::Initial,
            packet::Type::Handshake => Self::Handshake,
            packet::Type::ZeroRtt | packet::Type::Short => Self::ApplicationData,
            _ => panic!("Attempted to get space from wrong packet type"),
        }
    }
}

pub type PacketNumberSpaceSet = EnumSet<PacketNumberSpace>;

/// `InsertionResult` tracks whether something was inserted for `PacketRange::add()`.
pub enum InsertionResult {
    Largest,
    Smallest,
    NotInserted,
}

#[derive(Clone, Debug, Default)]
pub struct PacketRange {
    largest: packet::Number,
    smallest: packet::Number,
    ack_needed: bool,
}

impl PacketRange {
    /// Make a single packet range.
    pub const fn new(pn: packet::Number) -> Self {
        Self {
            largest: pn,
            smallest: pn,
            ack_needed: true,
        }
    }

    /// Get the number of acknowledged packets in the range.
    pub const fn len(&self) -> u64 {
        self.largest - self.smallest + 1
    }

    /// Returns whether this needs to be sent.
    pub const fn ack_needed(&self) -> bool {
        self.ack_needed
    }

    /// Return whether the given number is in the range.
    pub const fn contains(&self, pn: packet::Number) -> bool {
        (pn >= self.smallest) && (pn <= self.largest)
    }

    /// Maybe add a packet number to the range.  Returns true if it was added
    /// at the small end (which indicates that this might need merging with a
    /// preceding range).
    pub fn add(&mut self, pn: packet::Number) -> InsertionResult {
        assert!(!self.contains(pn));
        // Only insert if this is adjacent the current range.
        if (self.largest + 1) == pn {
            trace!("[{self}] Adding largest {pn}");
            self.largest += 1;
            self.ack_needed = true;
            InsertionResult::Largest
        } else if self.smallest == (pn + 1) {
            trace!("[{self}] Adding smallest {pn}");
            self.smallest -= 1;
            self.ack_needed = true;
            InsertionResult::Smallest
        } else {
            InsertionResult::NotInserted
        }
    }

    /// Maybe merge a higher-numbered range into this.
    fn merge_larger(&mut self, other: &Self) {
        debug!("[{self}] Merging {other}");
        // This only works if they are immediately adjacent.
        assert_eq!(self.largest + 1, other.smallest);

        self.largest = other.largest;
        self.ack_needed = self.ack_needed || other.ack_needed;
    }

    /// When a packet containing the range `other` is acknowledged,
    /// clear the `ack_needed` attribute on this.
    /// Requires that other is equal to this, or a larger range.
    pub const fn acknowledged(&mut self, other: &Self) {
        if (other.smallest <= self.smallest) && (other.largest >= self.largest) {
            self.ack_needed = false;
        }
    }
}

impl Display for PacketRange {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "{}->{}", self.largest, self.smallest)
    }
}

/// The default maximum ACK delay we use locally and advertise to the remote.
pub const DEFAULT_LOCAL_ACK_DELAY: Duration = Duration::from_millis(20);
/// The default maximum ACK delay we assume the remote uses.
///
/// > If this value is absent, a default of 25 milliseconds is assumed.
///
/// <https://datatracker.ietf.org/doc/html/rfc9000#section-18.2>
pub const DEFAULT_REMOTE_ACK_DELAY: Duration = Duration::from_millis(25);
/// The default number of in-order packets we will receive after
/// largest acknowledged without sending an immediate acknowledgment.
pub const DEFAULT_ACK_PACKET_TOLERANCE: packet::Number = 1;
// An ACK-only packet fits about 123 single-packet ranges. Tracking 128 keeps
// those gaps available to report. `add` walks the list on reorder; that walk
// stays small at this bound.
const MAX_TRACKED_RANGES: usize = 128;

/// A structure that tracks what packets have been received,
/// and what needs acknowledgement for a packet number space.
#[derive(Debug)]
pub struct RecvdPackets {
    space: PacketNumberSpace,
    ranges: VecDeque<PacketRange>,
    /// The packet number of the lowest number packet that we are tracking.
    min_tracked: packet::Number,
    /// The time we got the largest acknowledged.
    largest_pn_time: Option<Instant>,
    /// The time that we should be sending an ACK.
    ack_time: Option<Instant>,
    /// The time we last sent an ACK.
    last_ack_time: Option<Instant>,
    /// The current ACK frequency sequence number.
    ack_frequency_seqno: u64,
    /// The time to delay after receiving the first packet that is
    /// not immediately acknowledged.
    ack_delay: Duration,
    /// The number of ack-eliciting packets that have been received, but
    /// not acknowledged.
    unacknowledged_count: packet::Number,
    /// The number of contiguous packets that can be received without
    /// acknowledging immediately.
    unacknowledged_tolerance: packet::Number,
    /// Whether we are ignoring packets that arrive out of order
    /// for the purposes of generating immediate acknowledgment.
    ignore_order: bool,
    // The counts of different ECN marks that have been received.
    ecn_count: ecn::Count,
}

impl RecvdPackets {
    /// Make a new `RecvdPackets` for the indicated packet number space.
    pub fn new(space: PacketNumberSpace) -> Self {
        Self {
            space,
            ranges: VecDeque::new(),
            min_tracked: 0,
            largest_pn_time: None,
            ack_time: None,
            last_ack_time: None,
            ack_frequency_seqno: 0,
            ack_delay: DEFAULT_LOCAL_ACK_DELAY,
            unacknowledged_count: 0,
            unacknowledged_tolerance: if space == PacketNumberSpace::ApplicationData {
                DEFAULT_ACK_PACKET_TOLERANCE
            } else {
                // ACK more aggressively
                0
            },
            ignore_order: false,
            ecn_count: ecn::Count::default(),
        }
    }

    /// Get the ECN counts.
    pub const fn ecn_marks(&mut self) -> &mut ecn::Count {
        &mut self.ecn_count
    }

    /// Get the time at which the next ACK should be sent.
    pub const fn ack_time(&self) -> Option<Instant> {
        self.ack_time
    }

    /// Update acknowledgment delay parameters.
    pub const fn ack_freq(
        &mut self,
        seqno: u64,
        tolerance: packet::Number,
        delay: Duration,
        ignore_order: bool,
    ) {
        // Yes, this means that we will overwrite values if a sequence number is
        // reused, but that is better than using an `Option<packet::Number>`
        // when it will always be `Some`.
        if seqno >= self.ack_frequency_seqno {
            self.ack_frequency_seqno = seqno;
            self.unacknowledged_tolerance = tolerance;
            self.ack_delay = delay;
            self.ignore_order = ignore_order;
        }
    }

    /// Returns true if an ACK frame should be sent now.
    fn ack_now(&self, now: Instant, rtt: Duration) -> bool {
        // If ack_time is Some, then we have something to acknowledge.
        // In that case, either ack because `now >= ack_time`, or
        // because it is more than an RTT since the last time we sent an ack.
        self.ack_time.is_some_and(|next| {
            next <= now || self.last_ack_time.is_some_and(|last| last + rtt <= now)
        })
    }

    // A simple addition of a packet number to the tracked set.
    // This doesn't do a binary search on the assumption that
    // new packets will generally be added to the start of the list.
    fn add(&mut self, pn: packet::Number) -> Res<()> {
        for i in 0..self.ranges.len() {
            match self.ranges[i].add(pn) {
                InsertionResult::Largest => return Ok(()),
                InsertionResult::Smallest => {
                    // If this was the smallest, it might have filled a gap.
                    let nxt = i + 1;
                    if (nxt < self.ranges.len()) && (pn - 1 == self.ranges[nxt].largest) {
                        let larger = self.ranges.remove(i).ok_or(Error::Internal)?;
                        self.ranges[i].merge_larger(&larger);
                    }
                    return Ok(());
                }
                InsertionResult::NotInserted => {
                    if self.ranges[i].largest < pn {
                        self.ranges.insert(i, PacketRange::new(pn));
                        return Ok(());
                    }
                }
            }
        }
        self.ranges.push_back(PacketRange::new(pn));
        Ok(())
    }

    fn trim_ranges(&mut self, stats: &mut Stats) -> Res<()> {
        // Limit the number of ranges that are tracked to MAX_TRACKED_RANGES.
        if self.ranges.len() > MAX_TRACKED_RANGES {
            let oldest = self.ranges.pop_back().ok_or(Error::Internal)?;
            if oldest.ack_needed {
                warn!("[{self}] Dropping unacknowledged ACK range: {oldest}");
                stats.unacked_range_dropped += 1;
            } else {
                debug!("[{self}] Drop ACK range: {oldest}");
            }
            self.min_tracked = oldest.largest + 1;
        }
        Ok(())
    }

    /// Add the packet to the tracked set.
    /// Return true if the packet was the largest received so far.
    pub fn set_received(
        &mut self,
        now: Instant,
        pn: packet::Number,
        ack_eliciting: bool,
        stats: &mut Stats,
    ) -> Res<bool> {
        let next_in_order_pn = self.ranges.front().map_or(0, |r| r.largest + 1);
        trace!("[{self}] received {pn}, next: {next_in_order_pn}");

        self.add(pn)?;
        self.trim_ranges(stats)?;

        // The new addition was the largest, so update the time we use for calculating ACK delay.
        let largest = if pn >= next_in_order_pn {
            self.largest_pn_time = Some(now);
            true
        } else {
            false
        };

        if ack_eliciting {
            self.unacknowledged_count += 1;

            let immediate_ack = self.space != PacketNumberSpace::ApplicationData
                || (pn != next_in_order_pn && !self.ignore_order)
                || self.unacknowledged_count > self.unacknowledged_tolerance;

            let ack_time = if immediate_ack {
                now
            } else {
                // Note that `ack_delay` can change and that won't take effect if
                // we are waiting on the previous delay timer.
                // If ACK delay increases, we might send an ACK a bit early;
                // if ACK delay decreases, we might send an ACK a bit later.
                // We could use min() here, but change is rare and the size
                // of the change is very small.
                self.ack_time.unwrap_or_else(|| now + self.ack_delay)
            };
            debug!("[{self}] Set ACK timer to {ack_time:?}");
            self.ack_time = Some(ack_time);
        }
        Ok(largest)
    }

    /// If we just received a PING frame, we should immediately acknowledge.
    pub fn immediate_ack(&mut self, now: Instant) {
        self.ack_time = Some(now);
        debug!("[{self}] immediate_ack at {now:?}");
    }

    /// Check if the packet is a duplicate.
    pub fn is_duplicate(&self, pn: packet::Number) -> bool {
        if pn < self.min_tracked {
            return true;
        }
        self.ranges
            .iter()
            .take_while(|r| pn <= r.largest)
            .any(|r| r.contains(pn))
    }

    /// Mark the given range as having been acknowledged.
    pub fn acknowledged(&mut self, acked: &[PacketRange]) {
        let mut range_iter = self.ranges.iter_mut();
        let mut cur = range_iter.next().expect("should have at least one range");
        for ack in acked {
            while cur.smallest > ack.largest {
                let Some(next) = range_iter.next() else {
                    return;
                };
                cur = next;
            }
            cur.acknowledged(ack);
        }
    }

    /// Length of the worst possible ACK frame, assuming only one range and ECN counts.
    /// Note that this assumes one byte for the type and count of extra ranges.
    pub const USEFUL_ACK_LEN: usize = 1 + 8 + 8 + 1 + 8 + 3 * 8;
    /// Cap on bytes used for ACK ranges after the first.
    ///
    /// The range count is one byte ([`Self::MAX_EXTRA_ACK_RANGES`]). This cap keeps a
    /// few very large ranges from filling the packet. Each extra range is assumed to
    /// occupy [`Self::ASSUMED_ACK_RANGE_LEN`] bytes, so the loop does not measure it.
    const RANGE_FRAME_SPACE: usize = 512;
    /// Worst case for one ACK Range: a gap and a length, each a maximum-width varint.
    const ASSUMED_ACK_RANGE_LEN: usize = 16;
    /// Largest ACK Range Count that fits in a one-byte varint.
    const MAX_EXTRA_ACK_RANGES: usize = 63;

    /// Largest, delay, a reserved range count, and the first range.
    fn encode_ack_fixed<B: Buffer>(
        encoder: &mut Encoder<B>,
        first: &PacketRange,
        ack_delay: u64,
    ) -> Reservation<1> {
        encoder.encode_varint(first.largest);
        encoder.encode_varint(ack_delay);
        let reservation = encoder.reserve::<1>();
        encoder.encode_varint(first.len() - 1);
        reservation
    }

    /// Additional ACK ranges that fit in `space`, then the reserved range count.
    ///
    /// `pending` is the ack-needed ranges after `first`, newest-first. The returned
    /// list is `first` followed by each extra range that was written.
    fn encode_ack_ranges<'a, B, I>(
        pending: I,
        encoder: &mut Encoder<B>,
        first: PacketRange,
        reservation: Reservation<1>,
        space: usize,
    ) -> Vec<PacketRange>
    where
        B: Buffer,
        I: Iterator<Item = &'a PacketRange>,
    {
        let mut last = first.smallest;
        let mut acked = vec![first];
        let start = encoder.len();
        let range_budget = min(Self::RANGE_FRAME_SPACE, space);
        for range in pending.take(Self::MAX_EXTRA_ACK_RANGES) {
            // Leave room for a full-sized gap plus range instead of measuring it.
            if encoder.len() + Self::ASSUMED_ACK_RANGE_LEN > start + range_budget {
                break;
            }
            // The difference must be at least 2 because 0-length gaps
            // (difference 1) are illegal.
            debug_assert!(last >= range.largest + 2);
            encoder.encode_varint(last - range.largest - 2);
            encoder.encode_varint(range.len() - 1);
            last = range.smallest;
            acked.push(range.clone());
        }
        debug_assert!(encoder.len() - start <= range_budget);
        let count = u8::try_from(acked.len() - 1).expect("ACK range count is one byte");
        encoder.overwrite(reservation, [count]);
        acked
    }

    /// Generate an ACK frame for this packet number space.
    ///
    /// Unlike other frame generators this doesn't modify the underlying instance
    /// to track what has been sent. This only clears the delayed ACK timer.
    ///
    /// When sending ACKs, we want to always send the most recent ranges,
    /// even if they have been sent in other packets.
    ///
    /// We don't send ranges that have been acknowledged, but they still need
    /// to be tracked so that duplicates can be detected. Ranges that do not fit
    /// are left for a later ACK.
    fn write_frame<B: Buffer>(
        &mut self,
        now: Instant,
        rtt: Duration,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        // Check that we aren't delaying ACKs.
        if !self.ack_now(now, rtt) {
            return;
        }

        let mut ack_needed = self.ranges.iter().filter(|range| range.ack_needed());
        let Some(first) = ack_needed.next().cloned() else {
            return;
        };
        let Some(largest_pn_time) = self.largest_pn_time else {
            return;
        };
        let elapsed = now.duration_since(largest_pn_time);
        // We use the default exponent, so delay is in multiples of 8 microseconds.
        let ack_delay = u64::try_from(elapsed.as_micros() / 8).unwrap_or(u64::MAX);
        let ack_delay = min(MAX_VARINT, ack_delay);

        let ecn = self.ecn_count.is_some().then_some((
            self.ecn_count[Ecn::Ect0],
            self.ecn_count[Ecn::Ect1],
            self.ecn_count[Ecn::Ce],
        ));
        let frame_type = if ecn.is_some() {
            FrameType::AckEcn
        } else {
            FrameType::Ack
        };
        // Count is one reserved byte. ECN counts are written after the ranges.
        let fixed_len = Encoder::varint_len(u64::from(frame_type))
            + Encoder::varint_len(first.largest)
            + Encoder::varint_len(ack_delay)
            + 1 // one byte for range count (see RANGE_FRAME_SPACE)
            + Encoder::varint_len(first.len() - 1)
            + ecn.map_or(0, |(ect0, ect1, ce)| {
                Encoder::varint_len(ect0) + Encoder::varint_len(ect1) + Encoder::varint_len(ce)
            });
        let Some(space) = builder.remaining().checked_sub(fixed_len) else {
            return;
        };

        stats.largest_acknowledged = first.largest;
        stats.ack += 1;

        let mut acked = Vec::new();
        builder.encode_frame(frame_type, |encoder| {
            let reservation = Self::encode_ack_fixed(encoder, &first, ack_delay);
            acked = Self::encode_ack_ranges(ack_needed, encoder, first, reservation, space);
            if let Some((ect0, ect1, ce)) = ecn {
                encoder.encode_varint(ect0);
                encoder.encode_varint(ect1);
                encoder.encode_varint(ce);
            }
        });
        debug_assert!(builder.len() <= builder.limit());

        // We've sent an ACK, reset the timer.
        self.ack_time = None;
        self.last_ack_time = Some(now);
        self.unacknowledged_count = 0;

        tokens.push(recovery::Token::Ack(acked.into_boxed_slice()));
    }
}

impl Display for RecvdPackets {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "Recvd-{}", self.space)
    }
}

pub struct AckTracker {
    spaces: EnumMap<PacketNumberSpace, Option<RecvdPackets>>,
}

impl AckTracker {
    pub fn drop_space(&mut self, space: PacketNumberSpace) {
        assert_ne!(
            space,
            PacketNumberSpace::ApplicationData,
            "discarding application space"
        );
        if space == PacketNumberSpace::Handshake {
            assert!(self.spaces[PacketNumberSpace::Initial].is_none());
        }
        self.spaces[space].take();
    }

    pub fn get_mut(&mut self, space: PacketNumberSpace) -> Option<&mut RecvdPackets> {
        self.spaces[space].as_mut()
    }

    pub fn ack_freq(
        &mut self,
        seqno: u64,
        tolerance: packet::Number,
        delay: Duration,
        ignore_order: bool,
    ) {
        // Only ApplicationData ever delays ACK.
        if let Some(space) = self.get_mut(PacketNumberSpace::ApplicationData) {
            space.ack_freq(seqno, tolerance, delay, ignore_order);
        }
    }

    /// Force an ACK to be generated immediately.
    pub fn immediate_ack(&mut self, space: PacketNumberSpace, now: Instant) {
        if let Some(space) = self.get_mut(space) {
            space.immediate_ack(now);
        }
    }

    /// Determine the earliest time that an ACK might be needed.
    pub fn ack_time(&self, now: Instant) -> Option<Instant> {
        if tracing::enabled!(tracing::Level::TRACE) {
            for (space, recvd) in &self.spaces {
                if let Some(recvd) = recvd {
                    trace!("ack_time for {space} = {:?}", recvd.ack_time());
                }
            }
        }
        if self.spaces[PacketNumberSpace::Initial].is_none()
            && self.spaces[PacketNumberSpace::Handshake].is_none()
            && let Some(recvd) = &self.spaces[PacketNumberSpace::ApplicationData]
        {
            return recvd.ack_time();
        }

        // Ignore any time that is in the past relative to `now`.
        // That is something of a hack, but there are cases where we can't send ACK
        // frames for all spaces, which can mean that one space is stuck in the past.
        // That isn't a problem because we guarantee that earlier spaces will always
        // be able to send ACK frames.
        self.spaces
            .values()
            .flatten()
            .filter_map(|recvd| recvd.ack_time().filter(|t| *t > now))
            .min()
    }

    pub fn acked(&mut self, pn_space: PacketNumberSpace, acked: &[PacketRange]) {
        if let Some(space) = self.get_mut(pn_space) {
            space.acknowledged(acked);
        }
    }

    pub(crate) fn write_frame<B: Buffer>(
        &mut self,
        pn_space: PacketNumberSpace,
        now: Instant,
        rtt: Duration,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Some(space) = self.get_mut(pn_space) {
            space.write_frame(now, rtt, builder, tokens, stats);
        }
    }
}

impl Default for AckTracker {
    fn default() -> Self {
        Self {
            spaces: EnumMap::from_array([
                Some(RecvdPackets::new(PacketNumberSpace::Initial)),
                Some(RecvdPackets::new(PacketNumberSpace::Handshake)),
                Some(RecvdPackets::new(PacketNumberSpace::ApplicationData)),
            ]),
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::collections::HashSet;

    use neqo_common::{Decoder, Ecn, Encoder, to_u64};
    use test_fixture::now;

    use super::{
        AckTracker, Duration, Instant, MAX_TRACKED_RANGES, PacketNumberSpace, PacketRange,
        RecvdPackets,
    };
    use crate::{
        Stats,
        frame::{Frame, FrameType},
        packet, recovery,
        stats::FrameStats,
    };

    const RTT: Duration = Duration::from_millis(100);

    fn test_ack_range(pns: &[packet::Number], nranges: usize) {
        let mut rp = RecvdPackets::new(PacketNumberSpace::Initial); // Any space will do.
        assert_eq!(rp.to_string(), "Recvd-in");
        let mut packets = HashSet::new();

        for pn in pns {
            rp.set_received(now(), *pn, true, &mut Stats::default())
                .unwrap();
            packets.insert(*pn);
        }

        assert_eq!(rp.ranges.len(), nranges);

        // Check that all these packets will be detected as duplicates.
        for pn in pns {
            assert!(rp.is_duplicate(*pn));
        }

        // Check that the ranges decrease monotonically and don't overlap.
        let mut iter = rp.ranges.iter();
        let mut last = iter.next().expect("should have at least one");
        for n in iter {
            assert!(n.largest + 1 < last.smallest);
            last = n;
        }

        // Check that the ranges include the right values.
        let mut in_ranges = HashSet::new();
        for range in &rp.ranges {
            for included in range.smallest..=range.largest {
                in_ranges.insert(included);
            }
        }
        assert_eq!(packets, in_ranges);
    }

    #[test]
    fn pn0() {
        test_ack_range(&[0], 1);
    }

    #[test]
    fn pn1() {
        test_ack_range(&[1], 1);
    }

    #[test]
    fn two_ranges() {
        test_ack_range(&[0, 1, 2, 5, 6, 7], 2);
    }

    #[test]
    fn fill_in_range() {
        test_ack_range(&[0, 1, 2, 5, 6, 7, 3, 4], 1);
    }

    #[test]
    fn too_many_ranges() {
        let mut rp = RecvdPackets::new(PacketNumberSpace::Initial); // Any space will do.
        let mut stats = Stats::default();

        // This will add one too many disjoint ranges.
        for i in 0..=MAX_TRACKED_RANGES {
            rp.set_received(now(), to_u64(i * 2), true, &mut stats)
                .unwrap();
        }

        assert_eq!(rp.ranges.len(), MAX_TRACKED_RANGES);
        assert_eq!(rp.ranges.back().unwrap().largest, 2);

        // Even though the range was dropped, we still consider it a duplicate.
        assert!(rp.is_duplicate(0));
        assert!(!rp.is_duplicate(1));
        assert!(rp.is_duplicate(2));
    }

    #[test]
    fn ack_delay() {
        const COUNT: packet::Number = 9;
        const DELAY: Duration = Duration::from_millis(7);
        let mut stats = Stats::default();
        // Only application data packets are delayed.
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        assert!(rp.ack_time().is_none());
        assert!(!rp.ack_now(now(), RTT));

        rp.ack_freq(0, COUNT, DELAY, false);

        // Some packets won't cause an ACK to be needed.
        for i in 0..COUNT {
            rp.set_received(now(), i, true, &mut stats).unwrap();
            assert_eq!(Some(now() + DELAY), rp.ack_time());
            assert!(!rp.ack_now(now(), RTT));
            assert!(rp.ack_now(now() + DELAY, RTT));
        }

        // Exceeding COUNT will move the ACK time to now.
        rp.set_received(now(), COUNT, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
        assert!(rp.ack_now(now(), RTT));
    }

    #[test]
    fn no_ack_delay() {
        let mut stats = Stats::default();
        for space in &[PacketNumberSpace::Initial, PacketNumberSpace::Handshake] {
            let mut rp = RecvdPackets::new(*space);
            assert!(rp.ack_time().is_none());
            assert!(!rp.ack_now(now(), RTT));

            // Any packet in these spaces is acknowledged straight away.
            rp.set_received(now(), 0, true, &mut stats).unwrap();
            assert_eq!(Some(now()), rp.ack_time());
            assert!(rp.ack_now(now(), RTT));
        }
    }

    #[test]
    fn ooo_no_ack_delay_new() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        assert!(rp.ack_time().is_none());
        assert!(!rp.ack_now(now(), RTT));

        // Anything other than packet 0 is acknowledged immediately.
        rp.set_received(now(), 1, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
        assert!(rp.ack_now(now(), RTT));
    }

    fn write_frame_at(rp: &mut RecvdPackets, now: Instant) {
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut stats = FrameStats::default();
        let mut tokens = recovery::Tokens::new();
        rp.write_frame(now, RTT, &mut builder, &mut tokens, &mut stats);
        assert!(!tokens.is_empty());
        assert_eq!(stats.ack, 1);
    }

    fn write_frame(rp: &mut RecvdPackets) {
        write_frame_at(rp, now());
    }

    #[test]
    fn ooo_no_ack_delay_fill() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        rp.set_received(now(), 1, true, &mut stats).unwrap();
        write_frame(&mut rp);

        // Filling in behind the largest acknowledged causes immediate ACK.
        rp.set_received(now(), 0, true, &mut stats).unwrap();
        write_frame(&mut rp);

        // Receiving the next packet won't elicit an ACK.
        rp.set_received(now(), 2, true, &mut stats).unwrap();
        assert!(!rp.ack_now(now(), RTT));
    }

    #[test]
    fn immediate_ack_after_rtt() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        rp.set_received(now(), 1, true, &mut stats).unwrap();
        write_frame(&mut rp);

        // Filling in behind the largest acknowledged causes immediate ACK.
        rp.set_received(now(), 0, true, &mut stats).unwrap();
        write_frame(&mut rp);

        // A new packet ordinarily doesn't result in an ACK, but this time it does.
        rp.set_received(now() + RTT, 2, true, &mut stats).unwrap();
        write_frame_at(&mut rp, now() + RTT);
    }

    #[test]
    fn ooo_no_ack_delay_threshold_new() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);

        // Set tolerance to 2 and then it takes three packets.
        rp.ack_freq(0, 2, Duration::from_millis(10), true);

        rp.set_received(now(), 1, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 2, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 3, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
    }

    #[test]
    fn ooo_no_ack_delay_threshold_gap() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        rp.set_received(now(), 1, true, &mut stats).unwrap();
        write_frame(&mut rp);

        // Set tolerance to 2 and then it takes three packets.
        rp.ack_freq(0, 2, Duration::from_millis(10), true);

        rp.set_received(now(), 3, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 4, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 5, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
    }

    /// Test that an in-order packet that is not ack-eliciting doesn't
    /// increase the number of packets needed to cause an ACK.
    #[test]
    fn non_ack_eliciting_skip() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        rp.ack_freq(0, 1, Duration::from_millis(10), true);

        // This should be ignored.
        rp.set_received(now(), 0, false, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        // Skip 1 (it has no effect).
        rp.set_received(now(), 2, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 3, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
    }

    /// If a packet that is not ack-eliciting is reordered, that's fine too.
    #[test]
    fn non_ack_eliciting_reorder() {
        let mut stats = Stats::default();
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        rp.ack_freq(0, 1, Duration::from_millis(10), false);

        // These are out of order, but they are not ack-eliciting.
        rp.set_received(now(), 1, false, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 0, false, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());

        // These are in order.
        rp.set_received(now(), 2, true, &mut stats).unwrap();
        assert_ne!(Some(now()), rp.ack_time());
        rp.set_received(now(), 3, true, &mut stats).unwrap();
        assert_eq!(Some(now()), rp.ack_time());
    }

    #[test]
    fn aggregate_ack_time() {
        const DELAY: Duration = Duration::from_millis(17);
        let mut stats = Stats::default();
        let mut tracker = AckTracker::default();
        tracker.ack_freq(0, 1, DELAY, false);
        // This packet won't trigger an ACK.
        tracker
            .get_mut(PacketNumberSpace::Handshake)
            .unwrap()
            .set_received(now(), 0, false, &mut stats)
            .unwrap();
        assert_eq!(None, tracker.ack_time(now()));

        // This should be delayed.
        tracker
            .get_mut(PacketNumberSpace::ApplicationData)
            .unwrap()
            .set_received(now(), 0, true, &mut stats)
            .unwrap();
        assert_eq!(Some(now() + DELAY), tracker.ack_time(now()));

        // This should move the time forward.
        let later = now() + (DELAY / 2);
        tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .set_received(later, 0, true, &mut stats)
            .unwrap();
        assert_eq!(Some(later), tracker.ack_time(now()));
    }

    #[test]
    #[should_panic(expected = "discarding application space")]
    fn drop_app() {
        let mut tracker = AckTracker::default();
        tracker.drop_space(PacketNumberSpace::ApplicationData);
    }

    #[test]
    fn drop_spaces() {
        let mut stats = Stats::default();
        let mut tracker = AckTracker::default();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .set_received(now(), 0, true, &mut stats)
            .unwrap();
        // The reference time for `ack_time` has to be in the past or we filter out the timer.
        assert!(
            tracker
                .ack_time(now().checked_sub(Duration::from_millis(1)).unwrap())
                .is_some()
        );

        let mut tokens = recovery::Tokens::new();
        let mut frame_stats = FrameStats::default();
        tracker.write_frame(
            PacketNumberSpace::Initial,
            now(),
            RTT,
            &mut builder,
            &mut tokens,
            &mut frame_stats,
        );
        assert_eq!(frame_stats.ack, 1);

        // Mark another packet as received so we have cause to send another ACK in that space.
        tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .set_received(now(), 1, true, &mut stats)
            .unwrap();
        assert!(
            tracker
                .ack_time(now().checked_sub(Duration::from_millis(1)).unwrap())
                .is_some()
        );

        // Now drop that space.
        tracker.drop_space(PacketNumberSpace::Initial);

        assert!(tracker.get_mut(PacketNumberSpace::Initial).is_none());
        assert!(
            tracker
                .ack_time(now().checked_sub(Duration::from_millis(1)).unwrap())
                .is_none()
        );
        tracker.write_frame(
            PacketNumberSpace::Initial,
            now(),
            RTT,
            &mut builder,
            &mut tokens,
            &mut frame_stats,
        );
        assert_eq!(frame_stats.ack, 1);
        if let recovery::Token::Ack(tok) = &tokens[0] {
            tracker.acked(PacketNumberSpace::Initial, tok); // Should be a noop.
        } else {
            panic!("not an ACK token");
        }
    }

    /// ACK delay encodes elapsed microseconds divided by 8 (the default ACK delay exponent).
    #[test]
    fn ack_delay_encoding() {
        let t = now();
        // 16µs → ack_delay = 16/8 = 2.
        let elapsed = Duration::from_micros(16);

        let mut tracker = AckTracker::default();
        tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .set_received(t, 0, true, &mut Stats::default())
            .unwrap();

        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut stats = FrameStats::default();
        tracker.write_frame(
            PacketNumberSpace::Initial,
            t + elapsed,
            RTT,
            &mut builder,
            &mut recovery::Tokens::new(),
            &mut stats,
        );
        assert_eq!(stats.ack, 1, "ACK frame should have been written");

        // Decode the ACK frame from the builder output and check ack_delay.
        let enc: Encoder = builder.into();
        let bytes = Vec::from(enc);
        // Skip the 1-byte packet header; remainder is the ACK frame.
        let mut dec = Decoder::from(&bytes[1..]);
        let frame = Frame::decode(&mut dec).unwrap();
        let Frame::Ack { ack_delay, .. } = frame else {
            panic!("expected ACK frame, got {frame:?}");
        };
        assert_eq!(ack_delay, 2, "ack_delay must be 16\u{b5}s / 8 = 2");
    }

    #[test]
    fn no_room_for_ack() {
        let fixed_len = {
            let mut tracker = tracker_with_initial_ranges(1);
            let (builder, _) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
            builder.len()
        };

        let mut tracker = AckTracker::default();
        tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .set_received(now(), 0, true, &mut Stats::default())
            .unwrap();
        assert!(
            tracker
                .ack_time(now().checked_sub(Duration::from_millis(1)).unwrap())
                .is_some()
        );

        let mut builder = ack_builder(packet::LIMIT);
        // One byte short of the fixed ACK fields.
        builder.set_limit(fixed_len - 1);

        let mut stats = FrameStats::default();
        let mut tokens = recovery::Tokens::new();
        tracker.write_frame(
            PacketNumberSpace::Initial,
            now(),
            RTT,
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.ack, 0);
        assert!(tokens.is_empty());
        assert_eq!(builder.len(), 0);
        assert!(
            tracker
                .ack_time(now().checked_sub(Duration::from_millis(1)).unwrap())
                .is_some()
        );
    }

    #[test]
    fn no_room_for_extra_range() {
        // Packet numbers 0 and 2 encode in the same width, so a one-range frame is the
        // fixed part of a two-range frame.
        let fixed_len = {
            let mut tracker = tracker_with_initial_ranges(1);
            let (builder, _) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
            builder.len()
        };

        let mut tracker = tracker_with_initial_ranges(2);
        let mut builder = ack_builder(packet::LIMIT);
        // Short of the 16 bytes assumed for another range: truncate, do not drop the frame.
        builder.set_limit(fixed_len + RecvdPackets::ASSUMED_ACK_RANGE_LEN - 1);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, builder);
        assert_eq!(builder.len(), fixed_len);
        assert_ack_token_range_count(&tokens, 1);
        assert_initial_ack_ranges(&builder, 1, 1);

        let mut tracker = tracker_with_initial_ranges(2);
        let mut builder = ack_builder(packet::LIMIT);
        builder.set_limit(fixed_len + RecvdPackets::ASSUMED_ACK_RANGE_LEN);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, builder);
        assert_ack_token_range_count(&tokens, 2);
        assert_initial_ack_ranges(&builder, 0, 2);
        assert!(builder.len() <= builder.limit());
    }

    fn tracker_with_initial_ranges(range_count: usize) -> AckTracker {
        let mut stats = Stats::default();
        let mut tracker = AckTracker::default();
        let recvd = tracker.get_mut(PacketNumberSpace::Initial).unwrap();
        for i in 0..range_count {
            recvd
                .set_received(now(), to_u64(i * 2), true, &mut stats)
                .unwrap();
        }
        tracker
    }

    fn ack_builder(limit: usize) -> packet::Builder<Vec<u8>> {
        let mut builder = packet::Builder::short(Encoder::default(), false, None::<&[u8]>, limit);
        builder.truncate(0);
        builder
    }

    fn write_initial_ack_frame(
        tracker: &mut AckTracker,
        mut builder: packet::Builder<Vec<u8>>,
    ) -> (packet::Builder<Vec<u8>>, recovery::Tokens) {
        let mut frame_stats = FrameStats::default();
        let mut tokens = recovery::Tokens::new();
        tracker.write_frame(
            PacketNumberSpace::Initial,
            now(),
            RTT,
            &mut builder,
            &mut tokens,
            &mut frame_stats,
        );
        assert_eq!(frame_stats.ack, 1);
        (builder, tokens)
    }

    fn ack_token_range_count(tokens: &recovery::Tokens) -> usize {
        let [recovery::Token::Ack(tok)] = tokens.as_slice() else {
            panic!("expected one ACK token");
        };
        tok.len()
    }

    fn assert_ack_token_range_count(tokens: &recovery::Tokens, expected: usize) {
        assert_eq!(ack_token_range_count(tokens), expected);
    }

    fn decode_ack_from_builder(builder: &packet::Builder<Vec<u8>>) -> Frame<'_> {
        let mut dec = builder.as_decoder();
        Frame::decode(&mut dec).unwrap()
    }

    /// `start` is the index into [`tracker_with_initial_ranges`] of the oldest encoded range.
    fn assert_initial_ack_ranges(builder: &packet::Builder<Vec<u8>>, start: usize, count: usize) {
        let frame = decode_ack_from_builder(builder);
        let Frame::Ack {
            largest_acknowledged,
            first_ack_range,
            ack_ranges,
            ..
        } = frame
        else {
            panic!("not an ACK!");
        };
        assert_eq!(ack_ranges.len() + 1, count);
        let acked_ranges =
            Frame::decode_ack_frame(largest_acknowledged, first_ack_range, &ack_ranges).unwrap();
        let expected = (start..start + count)
            .rev()
            .map(|i| {
                let pn = to_u64(i * 2);
                pn..=pn
            })
            .collect::<Vec<_>>();
        assert_eq!(acked_ranges, expected);
    }

    #[test]
    fn write_frame_more_than_32_ranges() {
        const RANGE_COUNT: usize = 40;

        let mut tracker = tracker_with_initial_ranges(RANGE_COUNT);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
        assert_ack_token_range_count(&tokens, RANGE_COUNT);
        assert_initial_ack_ranges(&builder, 0, RANGE_COUNT);
    }

    #[test]
    fn small_remaining_truncates_ack_ranges() {
        const RANGE_COUNT: usize = 40;
        let full_len = {
            let mut tracker = tracker_with_initial_ranges(RANGE_COUNT);
            let (builder, tokens) =
                write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
            assert_ack_token_range_count(&tokens, RANGE_COUNT);
            builder.len()
        };

        let mut tracker = tracker_with_initial_ranges(RANGE_COUNT);
        let mut builder = ack_builder(packet::LIMIT);
        builder.set_limit(full_len - 1);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, builder);
        let encoded = ack_token_range_count(&tokens);
        assert!((1..RANGE_COUNT).contains(&encoded), "{encoded}");
        assert!(builder.len() <= builder.limit());
        assert_initial_ack_ranges(&builder, RANGE_COUNT - encoded, encoded);
    }

    #[test]
    fn ack_only_packet_sends_more_than_32_ranges() {
        assert!(recovery::SendProfile::new_limited(0, 1200).ack_only());
        assert_eq!(
            recovery::SendProfile::new_limited(0, 1200).limit(),
            recovery::ACK_ONLY_SIZE_LIMIT - 1
        );

        let mut tracker = tracker_with_initial_ranges(MAX_TRACKED_RANGES);
        let (builder, tokens) =
            write_initial_ack_frame(&mut tracker, ack_builder(recovery::ACK_ONLY_SIZE_LIMIT - 1));
        let encoded = ack_token_range_count(&tokens);
        assert!(encoded > 32, "{encoded}");
        assert!(builder.len() < recovery::ACK_ONLY_SIZE_LIMIT);
        assert!(builder.len() <= builder.limit());
        assert_initial_ack_ranges(&builder, MAX_TRACKED_RANGES - encoded, encoded);
    }

    #[test]
    fn one_byte_count_caps_extra_ranges() {
        let encoded = RecvdPackets::MAX_EXTRA_ACK_RANGES + 1;
        assert!(encoded > 32);

        let mut tracker = tracker_with_initial_ranges(MAX_TRACKED_RANGES);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
        assert_ack_token_range_count(&tokens, encoded);
        assert_initial_ack_ranges(&builder, MAX_TRACKED_RANGES - encoded, encoded);
        assert!(builder.len() <= builder.limit());
    }

    #[test]
    fn ack_respects_bytes_already_in_the_packet() {
        const RANGE_COUNT: usize = 40;
        let full_len = {
            let mut tracker = tracker_with_initial_ranges(RANGE_COUNT);
            let (builder, _) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
            builder.len()
        };

        let mut tracker = tracker_with_initial_ranges(RANGE_COUNT);
        let mut builder = ack_builder(packet::LIMIT);
        builder.encode_varint(u64::from(FrameType::Ping));
        let used = builder.len();
        // One byte less than the ACK that fit when it was the first frame.
        builder.set_limit(used + (full_len - 1) - 1);

        let (builder, tokens) = write_initial_ack_frame(&mut tracker, builder);
        let encoded = ack_token_range_count(&tokens);
        assert!((1..RANGE_COUNT).contains(&encoded), "{encoded}");
        assert!(builder.len() <= builder.limit());

        let mut dec = builder.as_decoder();
        dec.skip(used);
        let frame = Frame::decode(&mut dec).unwrap();
        let Frame::Ack { ack_ranges, .. } = frame else {
            panic!("not an ACK");
        };
        assert_eq!(ack_ranges.len() + 1, encoded);
    }

    #[test]
    fn ecn_counts_reduce_range_space() {
        let fixed_len = {
            let mut tracker = tracker_with_initial_ranges(1);
            let (builder, _) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
            builder.len()
        };

        let mut tracker = tracker_with_initial_ranges(2);
        let mut builder = ack_builder(packet::LIMIT);
        builder.set_limit(fixed_len + RecvdPackets::ASSUMED_ACK_RANGE_LEN);
        let (_, tokens) = write_initial_ack_frame(&mut tracker, builder);
        assert_ack_token_range_count(&tokens, 2);

        let mut tracker = tracker_with_initial_ranges(2);
        *tracker
            .get_mut(PacketNumberSpace::Initial)
            .unwrap()
            .ecn_marks() += Ecn::Ect0;
        let mut builder = ack_builder(packet::LIMIT);
        builder.set_limit(fixed_len + RecvdPackets::ASSUMED_ACK_RANGE_LEN);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, builder);
        assert_ack_token_range_count(&tokens, 1);
        assert!(builder.len() <= builder.limit());
        let Frame::Ack { ecn_count, .. } = decode_ack_from_builder(&builder) else {
            panic!("not an ACK");
        };
        assert!(ecn_count.is_some());
    }

    #[test]
    fn large_ranges_stop_at_byte_cap() {
        const RANGE_COUNT: usize = 80;
        // Gap value is 2^30, which is an 8-byte varint, so each extra range is 9 bytes.
        const SPACING: u64 = (1 << 30) + 2;
        // Worst-case fixed ACK after the header: type, largest, delay, count, first range.
        const WORST_FIXED: usize = 1 + 8 + 8 + 1 + 8;
        let mut stats = Stats::default();
        let mut tracker = AckTracker::default();
        let recvd = tracker.get_mut(PacketNumberSpace::Initial).unwrap();
        for i in 0..to_u64(RANGE_COUNT) {
            recvd
                .set_received(now(), i * SPACING, true, &mut stats)
                .unwrap();
        }

        let (builder, tokens) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
        let encoded = ack_token_range_count(&tokens);
        const {
            assert!(RANGE_COUNT > RecvdPackets::MAX_EXTRA_ACK_RANGES);
        };
        assert!(
            (33..=RecvdPackets::MAX_EXTRA_ACK_RANGES).contains(&encoded),
            "{encoded}"
        );
        assert!(builder.len() <= WORST_FIXED + RecvdPackets::RANGE_FRAME_SPACE);
        assert!(builder.len() <= builder.limit());

        let Frame::Ack {
            largest_acknowledged,
            ack_ranges,
            ..
        } = decode_ack_from_builder(&builder)
        else {
            panic!("not an ACK");
        };
        assert_eq!(largest_acknowledged, to_u64(RANGE_COUNT - 1) * SPACING);
        assert_eq!(ack_ranges.len() + 1, encoded);
    }

    #[test]
    fn single_range_ack_keeps_one_byte_count() {
        let mut tracker = tracker_with_initial_ranges(1);
        let (builder, tokens) = write_initial_ack_frame(&mut tracker, ack_builder(packet::LIMIT));
        assert_ack_token_range_count(&tokens, 1);
        assert_initial_ack_ranges(&builder, 0, 1);
        assert!(builder.len() <= builder.limit());
    }

    #[test]
    fn ack_time_elapsed() {
        let mut tracker = AckTracker::default();

        // While we have multiple PN spaces, we ignore ACK timers from the past.
        // Send out of order to cause the delayed ack timer to be set to `now()`.
        tracker
            .get_mut(PacketNumberSpace::ApplicationData)
            .unwrap()
            .set_received(now(), 3, true, &mut Stats::default())
            .unwrap();
        assert!(tracker.ack_time(now() + Duration::from_millis(1)).is_none());

        // When we are reduced to one space, that filter is off.
        tracker.drop_space(PacketNumberSpace::Initial);
        tracker.drop_space(PacketNumberSpace::Handshake);
        assert_eq!(
            tracker.ack_time(now() + Duration::from_millis(1)),
            Some(now())
        );
    }

    #[test]
    fn from_packet_type() {
        assert_eq!(
            PacketNumberSpace::from(packet::Type::Initial),
            PacketNumberSpace::Initial
        );
        assert_eq!(
            PacketNumberSpace::from(packet::Type::Handshake),
            PacketNumberSpace::Handshake
        );
        assert_eq!(
            PacketNumberSpace::from(packet::Type::ZeroRtt),
            PacketNumberSpace::ApplicationData
        );
        assert_eq!(
            PacketNumberSpace::from(packet::Type::Short),
            PacketNumberSpace::ApplicationData
        );
        assert!(
            std::panic::catch_unwind(|| {
                PacketNumberSpace::from(packet::Type::VersionNegotiation)
            })
            .is_err()
        );
    }

    #[test]
    fn packet_range_acknowledged() {
        use super::PacketRange;
        let mut r = PacketRange::new(5);
        assert!(r.ack_needed());
        assert_eq!(r.to_string(), "5->5");
        assert_eq!(r.len(), 1);
        assert!(r.contains(5));
        assert!(!r.contains(4));
        r.acknowledged(&PacketRange {
            largest: 10,
            smallest: 0,
            ack_needed: false,
        });
        assert!(!r.ack_needed());

        // Test partial overlap: other.smallest <= self.smallest but other.largest < self.largest.
        // Should NOT clear ack_needed.
        let mut r2 = PacketRange::new(5);
        r2.add(6); // range is now 5..=6
        assert_eq!(r2.to_string(), "6->5");
        assert_eq!(r2.len(), 2);
        r2.acknowledged(&PacketRange {
            largest: 5,
            smallest: 0,
            ack_needed: false,
        });
        assert!(r2.ack_needed()); // Should still need ack
    }

    #[test]
    fn useful_ack_len() {
        // 1 (type) + 8 (largest) + 8 (delay) + 1 (count) + 8 (first range) + 24 (3 ECN counts)
        assert_eq!(RecvdPackets::USEFUL_ACK_LEN, 50);
    }

    #[test]
    fn trim_ranges_increments_stat() {
        // Each dropped range increments the stat by 1.
        let mut rp = RecvdPackets::new(PacketNumberSpace::Initial);
        let mut stats = Stats::default();
        // Fill with MAX_TRACKED_RANGES + 2 disjoint ack-eliciting packets.
        for i in 0..=(MAX_TRACKED_RANGES + 1) {
            rp.set_received(now(), to_u64(i * 2), true, &mut stats)
                .unwrap();
        }
        // Two ranges should have been dropped.
        assert_eq!(stats.unacked_range_dropped, 2);
    }

    #[test]
    fn acknowledged_tracks_duplicates() {
        let mut rp = RecvdPackets::new(PacketNumberSpace::ApplicationData);
        let mut stats = Stats::default();
        // Receive packets 0, 1, 2 — one contiguous range, ACK needed.
        for pn in 0u64..3 {
            rp.set_received(now(), pn, true, &mut stats).unwrap();
        }
        assert!(
            rp.ack_time().is_some(),
            "ACK should be needed before acknowledging"
        );

        // Simulate peer acknowledging our ACK by calling acknowledged.
        let acked = [PacketRange::new(0)]; // covers packet 0
        rp.acknowledged(&acked);
        // Ranges are still tracked for duplicate detection after acknowledgement.
        assert!(rp.is_duplicate(0));
        assert!(rp.is_duplicate(2));
    }
}
