// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::ops::{AddAssign, Deref, DerefMut, Sub};

use enum_map::{Enum, EnumMap};
use neqo_common::{Ecn, to_u64};
use serde::{Serialize, Serializer};
use tracing::{debug, info};

use crate::{Stats, packet, recovery::sent};

/// The number of packets to use for testing a path for ECN capability.
pub(crate) const TEST_COUNT: usize = 10;

/// The number of packets to use for testing a path for ECN capability when exchanging
/// Initials during the handshake. This is a lower number than [`TEST_COUNT`] to avoid
/// unnecessarily delaying the handshake; we would otherwise double the PTO [`TEST_COUNT`]
/// times.
const TEST_COUNT_INITIAL_PHASE: usize = 3;

/// The state information related to testing a path for ECN capability.
/// See RFC9000, Appendix A.4.
#[derive(Debug, PartialEq, Clone, Copy, Default)]
enum ValidationState {
    /// ECN validation not started yet. Reason might e.g. be still handshaking
    /// or not being the primary path.
    #[default]
    NotStarted,
    /// The path is currently being tested for ECN capability, with the number of probes sent so
    /// far on the path during the ECN validation.
    Testing {
        probes_sent: usize,
        initial_probes_acked: usize,
        initial_probes_lost: usize,
    },
    /// The validation test has concluded but the path's ECN capability is not yet known.
    Unknown,
    /// The path is known to **not** be ECN capable.
    Failed(ValidationError),
    /// The path is known to be ECN capable.
    Capable,
}

impl ValidationState {
    fn set(&mut self, new: Self, stats: &mut Stats) {
        let old = std::mem::replace(self, new);

        match old {
            Self::NotStarted | Self::Testing { .. } | Self::Unknown => {}
            Self::Failed(_) => debug_assert!(false, "Failed is a terminal state"),
            Self::Capable => stats.ecn_path_validation[ValidationOutcome::Capable] -= 1,
        }
        match new {
            Self::NotStarted | Self::Testing { .. } | Self::Unknown => {}
            Self::Failed(error) => {
                stats.ecn_path_validation[ValidationOutcome::NotCapable(error)] += 1;
            }
            Self::Capable => stats.ecn_path_validation[ValidationOutcome::Capable] += 1,
        }
    }
}

/// The counts for different ECN marks.
///
/// Note: [`Count`] is used both for outgoing UDP datagrams, returned by
/// remote through QUIC ACKs and for incoming UDP datagrams, read from IP TOS
/// header. In the former case, given that QUIC ACKs only carry
/// [`Ecn::Ect0`], [`Ecn::Ect1`] and [`Ecn::Ce`], but never
/// [`Ecn::NotEct`], the [`Ecn::NotEct`] value will always be 0.
///
/// See also <https://www.rfc-editor.org/rfc/rfc9000.html#section-19.3.2>.
#[derive(PartialEq, Eq, Debug, Clone, Copy, Default)]
pub struct Count(EnumMap<Ecn, u64>);

impl Serialize for Count {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        crate::stats::serialize_counts(self.0, serializer)
    }
}

impl Deref for Count {
    type Target = EnumMap<Ecn, u64>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Count {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl Count {
    #[must_use]
    pub const fn new(not_ect: u64, ect0: u64, ect1: u64, ce: u64) -> Self {
        // Yes, the enum array order is different from the argument order.
        Self(EnumMap::from_array([not_ect, ect1, ect0, ce]))
    }

    /// Whether any of the ECT(0), ECT(1) or CE counts are non-zero.
    #[must_use]
    pub fn is_some(&self) -> bool {
        self[Ecn::Ect0] > 0 || self[Ecn::Ect1] > 0 || self[Ecn::Ce] > 0
    }

    /// Whether all of the ECN counts are zero (including Not-ECT.)
    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.iter().all(|(_, count)| *count == 0)
    }
}

impl Sub<Self> for Count {
    type Output = Self;

    /// Subtract the ECN counts in `other` from `self`.
    fn sub(self, rhs: Self) -> Self {
        let mut diff = Self::default();
        for (ecn, count) in &mut *diff {
            *count = self[ecn].saturating_sub(rhs[ecn]);
        }
        diff
    }
}

impl AddAssign<Ecn> for Count {
    fn add_assign(&mut self, rhs: Ecn) {
        self[rhs] += 1;
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy, Default)]
pub struct ValidationCount(EnumMap<ValidationOutcome, u64>);

impl Serialize for ValidationCount {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        crate::stats::serialize_counts(self.0, serializer)
    }
}

impl Deref for ValidationCount {
    type Target = EnumMap<ValidationOutcome, u64>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for ValidationCount {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

#[derive(Debug, Clone, Copy, Enum, PartialEq, Eq)]
pub enum ValidationError {
    BlackHole,
    Bleaching,
    ReceivedUnsentECT1,
}

#[derive(Debug, Clone, Copy, Enum, PartialEq, Eq)]
pub enum ValidationOutcome {
    Capable,
    NotCapable(ValidationError),
}

#[derive(Debug, Default)]
pub(crate) struct Info {
    /// The current state of ECN validation on this path.
    state: ValidationState,

    /// The largest ACK seen so far.
    largest_acked: packet::Number,

    /// The ECN counts from the last ACK frame that increased `largest_acked`.
    baseline: Count,
}

impl Info {
    pub(crate) fn start(&mut self, stats: &mut Stats) {
        if !matches!(self.state, ValidationState::NotStarted) {
            return;
        }

        self.state.set(
            ValidationState::Testing {
                probes_sent: 0,
                initial_probes_acked: 0,
                initial_probes_lost: 0,
            },
            stats,
        );
    }

    /// Set the baseline (= the ECN counts from the last ACK Frame).
    pub(crate) const fn set_baseline(&mut self, baseline: Count) {
        self.baseline = baseline;
    }

    /// Expose the current baseline.
    pub(crate) const fn baseline(&self) -> Count {
        self.baseline
    }

    /// Count the number of packets sent out on this path during ECN validation.
    /// Exit ECN validation if the number of packets sent exceeds `TEST_COUNT`.
    /// We do not implement the part of the RFC that says to exit ECN validation if the time since
    /// the start of ECN validation exceeds 3 * PTO, since this seems to happen much too quickly.
    pub(crate) fn on_packet_sent(&mut self, num_datagrams: usize, stats: &mut Stats) {
        if let ValidationState::Testing { probes_sent, .. } = &mut self.state {
            *probes_sent += num_datagrams;
            debug!("ECN probing: sent {probes_sent} probes");
            if *probes_sent >= TEST_COUNT {
                debug!("ECN probing concluded with {probes_sent} probes sent");
                self.state.set(ValidationState::Unknown, stats);
            }
        }
    }

    /// Disable ECN.
    pub(crate) fn disable_ecn(&mut self, stats: &mut Stats, reason: ValidationError) {
        self.state.set(ValidationState::Failed(reason), stats);
    }

    /// Process ECN counts from an ACK frame.
    ///
    /// Returns whether ECN counts contain new valid ECN CE marks.
    pub(crate) fn on_packets_acked(
        &mut self,
        acked_packets: &[sent::Packet],
        ack_ecn: Option<&Count>,
        stats: &mut Stats,
    ) -> bool {
        let prev_baseline = self.baseline;

        self.validate_ack_ecn_and_update(acked_packets, ack_ecn, stats);

        matches!(self.state, ValidationState::Capable)
            && (self.baseline - prev_baseline)[Ecn::Ce] > 0
    }

    /// An [`Ecn::Ect0`] marked packet has been acked.
    pub(crate) const fn acked_ecn(&mut self) {
        if let ValidationState::Testing {
            initial_probes_acked: probes_acked,
            ..
        } = &mut self.state
        {
            *probes_acked += 1;
        }
    }

    /// An [`Ecn::Ect0`] marked packet has been declared lost.
    pub(crate) fn lost_ecn(&mut self, stats: &mut Stats) {
        if let ValidationState::Testing {
            initial_probes_acked: probes_acked,
            initial_probes_lost: probes_lost,
            ..
        } = &mut self.state
        {
            *probes_lost += 1;
            // If we have lost all initial probes a bunch of times, we can conclude that the path
            // is not ECN capable and likely drops all ECN marked packets.
            if *probes_acked == 0 && *probes_lost == TEST_COUNT_INITIAL_PHASE {
                debug!("ECN validation failed, all {probes_lost} initial marked packets were lost");
                self.disable_ecn(stats, ValidationError::BlackHole);
            }
        }
    }

    /// After the ECN validation test has ended, check if the path is ECN capable.
    fn validate_ack_ecn_and_update(
        &mut self,
        acked_packets: &[sent::Packet],
        ack_ecn: Option<&Count>,
        stats: &mut Stats,
    ) {
        // RFC 9000, Section 13.4.2.1:
        //
        // > Validating ECN counts from reordered ACK frames can result in failure. An endpoint MUST
        // > NOT fail ECN validation as a result of processing an ACK frame that does not increase
        // > the largest acknowledged packet number.
        let largest_acked = acked_packets.first().expect("must be there");
        if largest_acked.pn() <= self.largest_acked {
            return;
        }

        // RFC 9000, Appendix A.4:
        //
        // > From the "unknown" state, successful validation of the ECN counts in an ACK frame
        // > (see Section 13.4.2.1) causes the ECN state for the path to become "capable", unless
        // > no marked packet has been acknowledged.
        match self.state {
            ValidationState::NotStarted
            | ValidationState::Testing { .. }
            | ValidationState::Failed(_) => return,
            ValidationState::Unknown | ValidationState::Capable => {}
        }
        self.largest_acked = largest_acked.pn();

        // RFC 9000, Section 13.4.2.1:
        //
        // > An endpoint that receives an ACK frame with ECN counts therefore validates
        // > the counts before using them. It performs this validation by comparing newly
        // > received counts against those from the last successfully processed ACK frame.
        //
        // > If an ACK frame newly acknowledges a packet that the endpoint sent with
        // > either the ECT(0) or ECT(1) codepoint set, ECN validation fails if the
        // > corresponding ECN counts are not present in the ACK frame.
        let newly_acked_sent_with_ect0 =
            to_u64(acked_packets.iter().filter(|p| p.ecn_marked_ect0()).count());
        let Some(ack_ecn) = ack_ecn else {
            if newly_acked_sent_with_ect0 > 0 {
                info!("ECN validation failed, no ECN counts in ACK frame");
                self.disable_ecn(stats, ValidationError::Bleaching);
            }
            return;
        };
        let ack_ecn = *ack_ecn;
        stats.ecn_tx_acked[largest_acked.packet_type()] = ack_ecn;

        // > ECN validation also fails if the sum of the increase in ECT(0) and ECN-CE counts is
        // > less than the number of newly acknowledged packets that were originally sent with an
        // > ECT(0) marking.
        let ecn_diff = ack_ecn - self.baseline;
        let sum_inc = ecn_diff[Ecn::Ect0] + ecn_diff[Ecn::Ce];
        if sum_inc < newly_acked_sent_with_ect0 {
            info!(
                "ECN validation failed, ACK counted {sum_inc} new marks, but {newly_acked_sent_with_ect0} of newly acked packets were sent with ECT(0)"
            );
            self.disable_ecn(stats, ValidationError::Bleaching);
        } else if ecn_diff[Ecn::Ect1] > 0 {
            info!("ECN validation failed, ACK counted ECT(1) marks that were never sent");
            self.disable_ecn(stats, ValidationError::ReceivedUnsentECT1);
        } else if self.state != ValidationState::Capable && newly_acked_sent_with_ect0 > 0 {
            info!("ECN validation succeeded, path is capable");
            self.state.set(ValidationState::Capable, stats);
        }
        self.baseline = ack_ecn;
    }

    pub(crate) const fn is_marking(&self) -> bool {
        match self.state {
            ValidationState::Testing { .. } | ValidationState::Capable => true,
            ValidationState::NotStarted | ValidationState::Failed(_) | ValidationState::Unknown => {
                false
            }
        }
    }

    /// The ECN mark to use for an outgoing UDP datagram.
    pub(crate) const fn ecn_mark(&self) -> Ecn {
        if self.is_marking() {
            Ecn::Ect0
        } else {
            Ecn::NotEct
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use test_fixture::now;

    use super::{Count, Info, TEST_COUNT, ValidationError, ValidationOutcome, ValidationState};
    use crate::{
        packet,
        recovery::{
            Token,
            sent::{Packet, make_packet},
        },
        stats::Stats,
    };

    fn make_unknown() -> (ValidationState, Stats) {
        (ValidationState::Unknown, Stats::default())
    }

    fn make_unknown_info() -> (Info, Stats) {
        let mut stats = Stats::default();
        let mut info = Info::default();
        info.start(&mut stats);
        info.on_packet_sent(TEST_COUNT, &mut stats);
        (info, stats)
    }

    fn make_marked_packet(pn: packet::Number) -> Packet {
        Packet::new(
            packet::Type::Short,
            pn,
            now(),
            true,
            vec![Token::EcnEct0],
            100,
        )
    }

    #[test]
    fn validation_state_capable_decrements_on_leave() {
        let (mut vs, mut stats) = make_unknown();
        vs.set(ValidationState::Capable, &mut stats);
        assert_eq!(stats.ecn_path_validation[ValidationOutcome::Capable], 1);

        vs.set(ValidationState::Unknown, &mut stats);
        assert_eq!(
            stats.ecn_path_validation[ValidationOutcome::Capable],
            0,
            "leaving Capable must decrement the Capable counter"
        );
    }

    #[test]
    fn validation_state_failed_increments_not_capable() {
        let (mut vs, mut stats) = make_unknown();
        vs.set(
            ValidationState::Failed(ValidationError::BlackHole),
            &mut stats,
        );
        assert_eq!(
            stats.ecn_path_validation[ValidationOutcome::NotCapable(ValidationError::BlackHole)],
            1,
            "transitioning to Failed must increment the NotCapable counter"
        );
    }

    #[test]
    fn count_predicates() {
        assert!(Count::default().is_empty());
        assert!(!Count::default().is_some());
        assert!(!Count::new(0, 1, 0, 0).is_empty());
        assert!(Count::new(0, 1, 0, 0).is_some()); // ect1 > 0
        assert!(Count::new(0, 0, 1, 0).is_some()); // ect0 > 0
        assert!(Count::new(0, 0, 0, 1).is_some()); // ce > 0
        assert!(!Count::new(1, 0, 0, 0).is_empty());
        assert!(!Count::new(1, 0, 0, 0).is_some()); // not_ect alone
    }

    #[test]
    fn unknown_stays_unknown_without_marked_ack() {
        let (mut info, mut stats) = make_unknown_info();
        let unmarked = [make_packet(1, now(), 100)];
        info.on_packets_acked(&unmarked, Some(&Count::default()), &mut stats);
        assert_eq!(info.state, ValidationState::Unknown);
    }

    #[test]
    fn unknown_stays_unknown_without_counts_or_marked_ack() {
        let (mut info, mut stats) = make_unknown_info();
        info.on_packets_acked(&[make_packet(1, now(), 100)], None, &mut stats);
        assert_eq!(info.state, ValidationState::Unknown);
    }

    #[test]
    fn marked_ack_without_counts_fails() {
        let (mut info, mut stats) = make_unknown_info();
        info.on_packets_acked(&[make_marked_packet(1)], None, &mut stats);
        assert_eq!(
            info.state,
            ValidationState::Failed(ValidationError::Bleaching)
        );
    }

    #[test]
    fn reordered_ack_after_ack_without_counts() {
        let (mut info, mut stats) = make_unknown_info();
        info.on_packets_acked(&[make_packet(2, now(), 100)], None, &mut stats);
        let marked = [make_marked_packet(1)];
        info.on_packets_acked(&marked, Some(&Count::default()), &mut stats);
        assert_eq!(info.state, ValidationState::Unknown);
    }
}
