// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    net::{IpAddr, Ipv4Addr},
    time::Duration,
};

use crate::{
    MIN_INITIAL_PACKET_SIZE, Pmtud,
    cc::{
        CWND_INITIAL_PKTS, ClassicSlowStart, classic_cc::ClassicCongestionController, cubic::Cubic,
        hystart::HyStart, new_reno::NewReno,
    },
};

mod cubic;
mod hystart;
mod new_reno;
mod search;

pub const RTT: Duration = Duration::from_millis(100);
pub const INITIAL_CWND: usize = CWND_INITIAL_PKTS * MIN_INITIAL_PACKET_SIZE;

pub fn pmtud() -> Pmtud {
    let mut pmtud = Pmtud::new(IpAddr::V4(Ipv4Addr::UNSPECIFIED));
    pmtud.set_iface_mtu(1_500);
    pmtud
}

/// Helper to create `ClassicCongestionController` with New Reno for tests.
pub fn make_cc_newreno() -> ClassicCongestionController<ClassicSlowStart, NewReno> {
    ClassicCongestionController::new(
        ClassicSlowStart::default(),
        NewReno::default(),
        pmtud(),
        true,
    )
}

/// Helper to create `ClassicCongestionController` with Cubic for tests.
pub fn make_cc_cubic() -> ClassicCongestionController<ClassicSlowStart, Cubic> {
    make_cc_cubic_with(Cubic::default())
}

/// Helper to create `ClassicCongestionController` with the given Cubic instance for tests.
pub fn make_cc_cubic_with(cubic: Cubic) -> ClassicCongestionController<ClassicSlowStart, Cubic> {
    ClassicCongestionController::new(ClassicSlowStart::default(), cubic, pmtud(), true)
}

/// Helper to create `ClassicCongestionController` with HyStart++ for tests.
pub fn make_cc_hystart(paced: bool) -> ClassicCongestionController<HyStart, Cubic> {
    ClassicCongestionController::new(
        HyStart::new(paced, crate::cc::HyStartCssBaseline::default()),
        Cubic::default(),
        pmtud(),
        true,
    )
}
