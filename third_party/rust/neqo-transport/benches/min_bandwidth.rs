// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! A simulated transfer benchmark, asserting a minimum bandwidth.
//!
//! This is using [`test_fixture::sim`], i.e. does no I/O beyond the process
//! boundary and runs in simulated time. Given that [`test_fixture::sim`] is
//! deterministic, there is no need for multiple benchmark iterations. Still it
//! is a Rust benchmark instead of a unit test due to its runtime (> 10s) even
//! in Rust release mode.

use std::{env, time::Duration};

use neqo_common::log::init as init_log;
use neqo_transport::{ConnectionParameters, State};
use test_fixture::{
    boxed,
    sim::{
        Simulator,
        connection::{Node, ReachState, ReceiveData, SendData},
        network::{Aqm, Mtu, TailDrop},
    },
};

/// Run a transfer of a gigabyte over a gigabit link.
/// Check to see that the achieved transfer rate matches expectations.
#[expect(clippy::cast_precision_loss, reason = "OK in a bench.")]
fn gbit_bandwidth(ecn: bool) {
    const MIB: usize = 1_024 * 1_024;
    const GIB: usize = 1_024 * MIB;

    const MBIT: usize = 1_000 * 1_000;
    const GBIT: usize = 1_000 * MBIT;

    const TRANSFER_AMOUNT: usize = GIB;
    const LINK_BANDWIDTH: usize = GBIT;
    const LINK_RTT_MS: u64 = 40;
    /// The amount of delay that the link buffer will add when full.
    const BUFFER_LATENCY_MS: usize = 4;
    /// How much of the theoretical bandwidth we will expect to deliver.
    /// Because we're not transferring a whole lot relative to the bandwidth,
    /// this ratio is relatively low.
    const MINIMUM_EXPECTED_UTILIZATION: f64 = 0.4;

    let gbit_link = || {
        let rate_byte = LINK_BANDWIDTH / 8;
        // Set capacity to double when ECN is enabled
        // so that the overall throughput remains roughly consistent.
        let capacity_byte = (1 + usize::from(ecn)) * rate_byte * BUFFER_LATENCY_MS / 1000;
        let delay = Duration::from_millis(LINK_RTT_MS) / 2;
        TailDrop::new(
            rate_byte,
            capacity_byte,
            if ecn { Aqm::codel() } else { Aqm::None },
            delay,
        )
    };

    init_log(None);

    let name = format!("gbit-bandwidth{}", if ecn { "-ecn" } else { "-noecn" });
    let simulated_time = Simulator::new(
        &name,
        boxed![
            Node::new_client(
                ConnectionParameters::default().ack_ratio(200),
                boxed![ReachState::new(State::Confirmed)],
                boxed![ReceiveData::new(TRANSFER_AMOUNT)]
            ),
            Mtu::new(1500),
            gbit_link(),
            Node::new_server(
                ConnectionParameters::default().ack_ratio(200),
                boxed![ReachState::new(State::Confirmed)],
                boxed![SendData::new(TRANSFER_AMOUNT)]
            ),
            Mtu::new(1500),
            gbit_link(),
        ],
    )
    .setup()
    .run();

    let achieved_bandwidth = TRANSFER_AMOUNT as f64 * 8.0 / simulated_time.as_secs_f64();
    eprintln!(
        // Not `info!`: `--features bench` drops that level. Stderr keeps it out of `results.txt`.
        "{name} achieved {a} Mb/s bandwidth (link rate {t})",
        a = achieved_bandwidth / MBIT as f64,
        t = LINK_BANDWIDTH / MBIT
    );

    assert!(
        LINK_BANDWIDTH as f64 * MINIMUM_EXPECTED_UTILIZATION < achieved_bandwidth,
        "{name} expected to reach {MINIMUM_EXPECTED_UTILIZATION} of maximum bandwidth ({t} Mbit/s) but got {a} Mbit/s",
        t = LINK_BANDWIDTH / MBIT,
        a = achieved_bandwidth / MBIT as f64,
    );
}

fn main() {
    // `harness = false`, so answer criterion's enumeration instead of running the simulations.
    if env::args().any(|a| a == "--list") {
        return;
    }
    gbit_bandwidth(false);
    gbit_bandwidth(true);
}
