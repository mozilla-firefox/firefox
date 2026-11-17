// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Congestion control

use std::{
    fmt::{Debug, Display},
    time::{Duration, Instant},
};

use neqo_common::qlog::Qlog;
use serde::Serialize;

use crate::{Pmtud, recovery::sent, rtt::RttEstimate, stats::CongestionControlStats};

mod classic_cc;
mod classic_slow_start;
mod cubic;
mod hystart;
mod new_reno;
mod search;

pub use classic_cc::{
    CWND_INITIAL_PKTS, ClassicCongestionController, PERSISTENT_CONG_THRESH, Phase,
};
pub use classic_slow_start::ClassicSlowStart;
pub use cubic::Cubic;
pub use hystart::{HyStart, HyStartCssBaseline};
pub use new_reno::NewReno;
#[cfg(test)]
pub use search::Outcome;
pub use search::Search;

/// How a congestion event was triggered.
#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize)]
pub enum CongestionTrigger {
    /// Triggered by packet loss. Carries the number of lost packets.
    Loss(usize),
    /// Triggered by an ECN CE mark.
    Ecn,
}

pub trait CongestionController: Display + Debug {
    fn set_qlog(&mut self, qlog: Qlog);

    #[must_use]
    fn cwnd(&self) -> usize;

    #[must_use]
    fn bytes_in_flight(&self) -> usize;

    #[must_use]
    fn cwnd_avail(&self) -> usize;

    #[must_use]
    fn cwnd_min(&self) -> usize;

    #[must_use]
    fn pmtud(&self) -> &Pmtud;

    #[must_use]
    fn pmtud_mut(&mut self) -> &mut Pmtud;

    fn on_packets_acked(
        &mut self,
        acked_pkts: &[sent::Packet],
        rtt_est: &RttEstimate,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    );

    /// Returns true if the congestion window was reduced.
    fn on_packets_lost(
        &mut self,
        first_rtt_sample_time: Option<Instant>,
        prev_largest_acked_sent: Option<Instant>,
        pto: Duration,
        lost_packets: &[sent::Packet],
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool;

    /// Returns true if the congestion window was reduced.
    fn on_ecn_ce_received(
        &mut self,
        largest_acked_pkt: &sent::Packet,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool;

    #[must_use]
    fn recovery_packet(&self) -> bool;

    fn discard(&mut self, pkt: &sent::Packet, now: Instant);

    fn on_packet_sent(&mut self, pkt: &sent::Packet, now: Instant);

    fn discard_in_flight(&mut self, now: Instant);
}

#[derive(Debug, Copy, Clone, Default, PartialEq, Eq, strum::EnumString, strum::VariantNames)]
#[strum(ascii_case_insensitive)]
pub enum CongestionControl {
    #[strum(serialize = "newreno", serialize = "reno")]
    NewReno,
    #[strum(serialize = "cubic")]
    #[default]
    Cubic,
}

#[derive(Debug, Copy, Clone, Default, PartialEq, Eq, strum::EnumString, strum::VariantNames)]
#[strum(ascii_case_insensitive)]
pub enum SlowStart {
    #[strum(serialize = "classic")]
    #[default]
    Classic,
    #[strum(serialize = "hystart")]
    HyStart,
    #[strum(serialize = "search")]
    Search,
}

/// A concrete congestion controller, dispatching across all combinations of
/// algorithm and slow-start strategy.
///
/// This enum avoids the heap allocation and vtable indirection of `Box<dyn CongestionController>`
/// on the per-packet hot path.
#[derive(Debug, strum::Display)]
pub enum CongestionControlImplementation {
    #[strum(to_string = "{0}")]
    ClassicNewReno(ClassicCongestionController<ClassicSlowStart, NewReno>),
    #[strum(to_string = "{0}")]
    HyStartNewReno(ClassicCongestionController<HyStart, NewReno>),
    #[strum(to_string = "{0}")]
    SearchNewReno(ClassicCongestionController<Search, NewReno>),
    #[strum(to_string = "{0}")]
    ClassicCubic(ClassicCongestionController<ClassicSlowStart, Cubic>),
    #[strum(to_string = "{0}")]
    HyStartCubic(ClassicCongestionController<HyStart, Cubic>),
    #[strum(to_string = "{0}")]
    SearchCubic(ClassicCongestionController<Search, Cubic>),
}

macro_rules! dispatch {
    ($self:ident . $method:ident $args:tt) => {
        neqo_common::dispatch!(
            [ClassicNewReno, HyStartNewReno, SearchNewReno, ClassicCubic, HyStartCubic, SearchCubic]
            $self . $method $args
        )
    };
}

impl CongestionController for CongestionControlImplementation {
    fn set_qlog(&mut self, qlog: Qlog) {
        dispatch!(self.set_qlog(qlog));
    }

    fn cwnd(&self) -> usize {
        dispatch!(self.cwnd())
    }

    fn bytes_in_flight(&self) -> usize {
        dispatch!(self.bytes_in_flight())
    }

    fn cwnd_avail(&self) -> usize {
        dispatch!(self.cwnd_avail())
    }

    fn cwnd_min(&self) -> usize {
        dispatch!(self.cwnd_min())
    }

    fn pmtud(&self) -> &Pmtud {
        dispatch!(self.pmtud())
    }

    fn pmtud_mut(&mut self) -> &mut Pmtud {
        dispatch!(self.pmtud_mut())
    }

    fn on_packets_acked(
        &mut self,
        acked_pkts: &[sent::Packet],
        rtt_est: &RttEstimate,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) {
        dispatch!(self.on_packets_acked(acked_pkts, rtt_est, now, cc_stats));
    }

    fn on_packets_lost(
        &mut self,
        first_rtt_sample_time: Option<Instant>,
        prev_largest_acked_sent: Option<Instant>,
        pto: Duration,
        lost_packets: &[sent::Packet],
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool {
        dispatch!(self.on_packets_lost(
            first_rtt_sample_time,
            prev_largest_acked_sent,
            pto,
            lost_packets,
            now,
            cc_stats,
        ))
    }

    fn on_ecn_ce_received(
        &mut self,
        largest_acked_pkt: &sent::Packet,
        now: Instant,
        cc_stats: &mut CongestionControlStats,
    ) -> bool {
        dispatch!(self.on_ecn_ce_received(largest_acked_pkt, now, cc_stats))
    }

    fn recovery_packet(&self) -> bool {
        dispatch!(self.recovery_packet())
    }

    fn discard(&mut self, pkt: &sent::Packet, now: Instant) {
        dispatch!(self.discard(pkt, now));
    }

    fn on_packet_sent(&mut self, pkt: &sent::Packet, now: Instant) {
        dispatch!(self.on_packet_sent(pkt, now));
    }

    fn discard_in_flight(&mut self, now: Instant) {
        dispatch!(self.discard_in_flight(now));
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
pub mod tests;
