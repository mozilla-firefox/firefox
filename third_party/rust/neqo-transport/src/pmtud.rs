// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    net::IpAddr,
    time::{Duration, Instant},
};

use neqo_common::{Buffer, qlog::Qlog};
use tracing::{debug, info};

use crate::{
    Stats,
    frame::{FrameEncoder as _, FrameType},
    packet, qlog,
    recovery::{self, sent},
};

// Values <= 1500 based on: A. Custura, G. Fairhurst and I. Learmonth, "Exploring Usable Path MTU in
// the Internet," 2018 Network Traffic Measurement and Analysis Conference (TMA), Vienna, Austria,
// 2018, pp. 1-8, doi: 10.23919/TMA.2018.8506538. keywords:
// {Servers;Probes;Tools;Clamps;Middleboxes;Standards},
const MTU_SIZES_V4: &[usize] = &[
    1280, 1380, 1420, 1472, 1500, 2047, 4095, 8191, 16383, 32767, 65535,
];
const MTU_SIZES_V6: &[usize] = &[
    1280, 1380,
    1420, // 1420 is not in the paper for v6, but adding it makes the arrays the same length
    1470, 1500, 2047, 4095, 8191, 16383, 32767, 65535,
];
const _: () = assert!(MTU_SIZES_V4.len() == MTU_SIZES_V6.len());
const SEARCH_TABLE_LEN: usize = MTU_SIZES_V4.len();

// From https://datatracker.ietf.org/doc/html/rfc8899#section-5.1
const MAX_PROBES: usize = 3;
const PMTU_RAISE_TIMER: Duration = Duration::from_mins(10);

#[derive(Debug, PartialEq, Clone, Copy)]
enum Probe {
    NotNeeded,
    Needed,
    Sent,
}

#[derive(Debug)]
pub struct Pmtud {
    search_table: &'static [usize],
    header_size: usize,
    mtu: usize,
    iface_mtu: usize,
    /// The peer's [`max_udp_payload_size`](https://www.rfc-editor.org/rfc/rfc9000#section-18.2)
    /// transport parameter, i.e., the maximum UDP payload (not including IP and UDP headers)
    /// the peer is willing to receive.
    peer_max_udp_payload: Option<usize>,
    probe_index: usize,
    probe_count: usize,
    probe_state: Probe,
    raise_timer: Option<Instant>,
    qlog: Qlog,
}

impl Pmtud {
    /// Returns the MTU search table for the given remote IP address family.
    const fn search_table(remote_ip: IpAddr) -> &'static [usize] {
        match remote_ip {
            IpAddr::V4(_) => MTU_SIZES_V4,
            IpAddr::V6(_) => MTU_SIZES_V6,
        }
    }

    /// Size of the IPv4/IPv6 and UDP headers, in bytes.
    #[must_use]
    pub const fn header_size(remote_ip: IpAddr) -> usize {
        match remote_ip {
            IpAddr::V4(_) => 20 + 8,
            IpAddr::V6(_) => 40 + 8,
        }
    }

    #[must_use]
    pub fn new(remote_ip: IpAddr) -> Self {
        let search_table = Self::search_table(remote_ip);
        let probe_index = 0;
        Self {
            search_table,
            header_size: Self::header_size(remote_ip),
            mtu: search_table[probe_index],
            iface_mtu: usize::MAX,
            peer_max_udp_payload: None,
            probe_index,
            probe_count: 0,
            probe_state: Probe::NotNeeded,
            raise_timer: None,
            qlog: Qlog::disabled(),
        }
    }

    pub fn set_qlog(&mut self, qlog: Qlog) {
        self.qlog = qlog;
    }

    fn set_mtu(&mut self, idx: usize, stats: &mut Stats, now: Instant) {
        let old_mtu = self.plpmtu();
        self.mtu = self.search_table[idx];
        stats.pmtud_pmtu = self.mtu;
        let new_mtu = self.plpmtu();
        if old_mtu != new_mtu {
            let done = !self.needs_probe();
            qlog::mtu_updated(&mut self.qlog, old_mtu, new_mtu, done, now);
        }
    }

    /// Set the local interface MTU as an upper bound for probing.
    pub const fn set_iface_mtu(&mut self, iface_mtu: usize) {
        self.iface_mtu = iface_mtu;
    }

    /// Set the peer's `max_udp_payload_size` transport parameter as an upper bound for probing.
    pub const fn set_peer_max_udp_payload(&mut self, peer_max_udp_payload: usize) {
        self.peer_max_udp_payload = Some(peer_max_udp_payload);
    }

    /// Returns the peer's `max_udp_payload_size`, if known.
    #[must_use]
    pub const fn peer_max_udp_payload(&self) -> Option<usize> {
        self.peer_max_udp_payload
    }

    /// Checks whether the PMTUD raise timer should be fired, and does so if needed.
    pub fn maybe_fire_raise_timer(&mut self, now: Instant, stats: &mut Stats) {
        if self.probe_state == Probe::NotNeeded && self.raise_timer.is_some_and(|t| now >= t) {
            debug!("PMTUD raise timer fired");
            self.raise_timer = None;
            self.next(now, stats);
        }
    }

    /// Returns the current Packetization Layer Path MTU, i.e., the maximum UDP payload that can be
    /// sent. During probing, this may be larger than the actual path MTU.
    #[must_use]
    pub const fn plpmtu(&self) -> usize {
        self.mtu - self.header_size
    }

    /// Returns true if a PMTUD probe should be sent.
    #[must_use]
    pub fn needs_probe(&self) -> bool {
        self.probe_state == Probe::Needed
    }

    /// Returns the size of the current PMTUD probe.
    #[must_use]
    pub const fn probe_size(&self) -> usize {
        self.search_table[self.probe_index] - self.header_size
    }

    /// Sends a PMTUD probe.
    pub fn send_probe<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut Stats,
    ) {
        // The packet may include ACK-eliciting data already, but rather than check for that, it
        // seems OK to burn one byte here to simply include a PING.
        builder.encode_frame(FrameType::Ping, |_| {});
        tokens.push(recovery::Token::PmtudProbe(
            self.search_table[self.probe_index],
        ));
        stats.frame_tx.ping += 1;
        stats.pmtud_tx += 1;
        self.probe_count += 1;
        self.probe_state = Probe::Sent;
        debug!(
            "Sending PMTUD probe of size {}, count {}",
            self.search_table[self.probe_index], self.probe_count
        );
    }

    /// Returns the maximum Packetization Layer Path MTU for the configured
    /// address family. Note that this ignores the interface MTU.
    #[expect(clippy::missing_panics_doc, reason = "search table is never empty")]
    #[must_use]
    pub const fn address_family_max_mtu(&self) -> usize {
        *self.search_table.last().expect("search table is empty")
    }

    /// Count the PMTUD probes included in `pkts`.
    fn count_probes(pkts: &[sent::Packet]) -> usize {
        pkts.iter().filter(|p| p.is_pmtud_probe()).count()
    }

    /// Checks whether a PMTUD probe has been acknowledged, and if so, updates the PMTUD state.
    /// May also initiate a new probe process for a larger MTU.
    pub fn on_packets_acked(
        &mut self,
        acked_pkts: &[sent::Packet],
        now: Instant,
        stats: &mut Stats,
    ) {
        // FIXME: ACKs of stale probes are not counted in `pmtud_ack`.
        if self.probe_state == Probe::NotNeeded {
            return;
        }

        // Only the ACK of a probe for the size we are currently searching for confirms that size.
        // Other probes are stale, e.g., the original of a probe that was spuriously declared lost
        // and then retried. They say nothing about the size we are searching for, which may not
        // have been probed yet.
        let probe_mtu = self.search_table[self.probe_index];
        let acked = acked_pkts
            .iter()
            .filter(|p| p.is_pmtud_probe_of(probe_mtu))
            .count();
        if acked == 0 {
            return;
        }
        stats.pmtud_ack += acked;

        // A probe was ACKed, confirm the new MTU and try to probe upwards further.
        let confirmed_idx = self.probe_index;
        debug!(
            "PMTUD probe of size {} succeeded",
            self.search_table[confirmed_idx]
        );
        self.next(now, stats);
        self.set_mtu(confirmed_idx, stats, now);
    }

    /// Stops the PMTUD process, setting the MTU to the largest successful probe size.
    fn stop(&mut self, idx: usize, now: Instant, stats: &mut Stats) {
        self.probe_state = Probe::NotNeeded; // We don't need to send any more probes
        self.probe_index = idx; // Index of the last successful probe
        self.set_mtu(idx, stats, now); // Leading to this MTU
        self.probe_count = 0; // Reset the count
        self.raise_timer = Some(now + PMTU_RAISE_TIMER);
        info!(
            "PMTUD stopped, PLPMTU is now {}, raise timer {:?}",
            self.mtu, self.raise_timer
        );
    }

    /// Checks whether a PMTUD probe has been lost. If it has been lost more than `MAX_PROBES`
    /// times, the PMTUD process is stopped at the current MTU.
    pub fn on_packets_lost(
        &mut self,
        lost_packets: &[sent::Packet],
        stats: &mut Stats,
        now: Instant,
    ) {
        let lost = Self::count_probes(lost_packets);
        if lost == 0 {
            return;
        }
        stats.pmtud_lost += lost;

        if self.probe_count >= MAX_PROBES {
            // We've sent MAX_PROBES probes and they were all lost. Stop probing at the
            // previous successful MTU.
            let ok_idx = self.probe_index.saturating_sub(1);
            debug!(
                "PMTUD probe of size {} failed after {MAX_PROBES} attempts",
                self.search_table[self.probe_index]
            );
            self.stop(ok_idx, now, stats);
        } else {
            // Probe was lost but we haven't exhausted retries yet.
            self.probe_state = Probe::Needed;
        }
    }

    /// Starts PMTUD from the minimum MTU, probing upward.
    pub fn start(&mut self, now: Instant, stats: &mut Stats) {
        self.probe_index = 0;
        self.raise_timer = None;
        self.next(now, stats);
        self.set_mtu(0, stats, now);
        debug!("PMTUD started, PLPMTU is now {}", self.mtu);
    }

    /// Starts the next upward PMTUD probe.
    pub fn next(&mut self, now: Instant, stats: &mut Stats) {
        if self.probe_index == SEARCH_TABLE_LEN - 1 {
            debug!(
                "PMTUD reached end of search table, i.e. {}, stopping upwards search",
                self.mtu,
            );
            self.stop(self.probe_index, now, stats);
            return;
        }

        let mtu_limit = self.peer_max_udp_payload.map_or(self.iface_mtu, |p| {
            self.iface_mtu.min(p.saturating_add(self.header_size))
        });
        if self.search_table[self.probe_index + 1] > mtu_limit {
            debug!(
                "PMTUD reached MTU limit {mtu_limit}, stopping upwards search at {}",
                self.mtu
            );
            self.stop(self.probe_index, now, stats);
            return;
        }

        self.probe_state = Probe::Needed; // We need to send a probe
        self.probe_count = 0; // For the first time
        self.probe_index += 1; // At this size
        debug!(
            "PMTUD started with probe size {}",
            self.search_table[self.probe_index],
        );
    }

    /// Returns the default PLPMTU for the given remote IP address.
    #[must_use]
    pub const fn default_plpmtu(remote_ip: IpAddr) -> usize {
        let search_table = Self::search_table(remote_ip);
        search_table[0] - Self::header_size(remote_ip)
    }
}

#[cfg(all(not(feature = "disable-encryption"), test))]
mod tests {
    use std::{
        cmp::min,
        net::{IpAddr, Ipv4Addr, Ipv6Addr},
        time::Instant,
    };

    use neqo_common::Encoder;
    use test_fixture::{fixture_init, now};
    use tracing::{debug, info};

    use super::MAX_PROBES;
    use crate::{
        Pmtud, Stats,
        crypto::CryptoDxState,
        packet,
        pmtud::{PMTU_RAISE_TIMER, Probe, SEARCH_TABLE_LEN},
        recovery::{self, SendProfile, sent},
    };

    /// Test helper that sends a PMTUD probe and returns it as loss recovery would track it.
    fn send_probe_packet(
        pmtud: &mut Pmtud,
        stats: &mut Stats,
        pn: packet::Number,
        now: Instant,
    ) -> sent::Packet {
        assert!(pmtud.needs_probe());
        let len = pmtud.probe_size();
        let mut builder = packet::Builder::short(Encoder::default(), false, None::<&[u8]>, len);
        builder.pn(pn, 4);
        let mut tokens = recovery::Tokens::new();
        pmtud.send_probe(&mut builder, &mut tokens, stats);
        sent::Packet::new(packet::Type::Short, pn, now, true, tokens, len)
    }

    const V4: IpAddr = IpAddr::V4(Ipv4Addr::UNSPECIFIED);
    const V6: IpAddr = IpAddr::V6(Ipv6Addr::UNSPECIFIED);
    const IFACE_MTUS: &[Option<usize>] = &[
        None,
        Some(1300),
        Some(1500),
        Some(5000),
        Some(u16::MAX as usize),
    ];

    fn new_pmtud(addr: IpAddr, iface_mtu: Option<usize>) -> Pmtud {
        let mut pmtud = Pmtud::new(addr);
        if let Some(iface_mtu) = iface_mtu {
            pmtud.set_iface_mtu(iface_mtu);
        }
        pmtud
    }

    /// Asserts that the PMTUD process has stopped at the given MTU.
    #[cfg(test)]
    fn assert_mtu(pmtud: &Pmtud, mtu: usize) {
        let idx = pmtud
            .search_table
            .iter()
            .position(|mtu| *mtu == pmtud.mtu)
            .unwrap();
        assert!((idx == 0 && mtu <= pmtud.search_table[idx]) || (mtu >= pmtud.search_table[idx]));
        if idx < SEARCH_TABLE_LEN - 1 {
            assert!(mtu < pmtud.search_table[idx + 1]);
        }
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);
    }

    #[cfg(test)]
    fn pmtud_step(
        pmtud: &mut Pmtud,
        stats: &mut Stats,
        prot: &mut CryptoDxState,
        addr: IpAddr,
        mtu: usize,
        now: Instant,
    ) {
        let stats_before = stats.clone();

        // Fake a packet number, so the builder logic works.
        let profile = SendProfile::new_limited(pmtud.plpmtu(), pmtud.plpmtu());
        let expansion = prot.expansion();
        let limit = if pmtud.needs_probe() {
            pmtud.probe_size() - expansion
        } else {
            profile.limit() - expansion
        };
        let mut builder = packet::Builder::short(Encoder::default(), false, None::<&[u8]>, limit);
        let pn = prot.next_pn();
        builder.pn(pn, 4);
        builder.enable_padding(true);
        let mut tokens = recovery::Tokens::new();
        pmtud.send_probe(&mut builder, &mut tokens, stats);
        builder.pad();
        let encoder = builder.build(prot).unwrap();
        assert_eq!(encoder.len(), pmtud.probe_size());
        assert!(!pmtud.needs_probe());
        assert_eq!(stats_before.pmtud_tx + 1, stats.pmtud_tx);

        let packet = sent::Packet::new(packet::Type::Short, pn, now, true, tokens, encoder.len());
        if encoder.len() + Pmtud::header_size(addr) <= mtu {
            pmtud.on_packets_acked(&[packet], now, stats);
            assert_eq!(stats_before.pmtud_ack + 1, stats.pmtud_ack);
        } else {
            pmtud.on_packets_lost(&[packet], stats, now);
            assert_eq!(stats_before.pmtud_lost + 1, stats.pmtud_lost);
        }
    }

    fn find_pmtu(
        addr: IpAddr,
        mtu: usize,
        iface_mtu: Option<usize>,
    ) -> (Pmtud, Stats, CryptoDxState, Instant) {
        fixture_init();
        let now = now();
        let mut pmtud = new_pmtud(addr, iface_mtu);
        let mut stats = Stats::default();
        let mut prot = CryptoDxState::test_default_write();

        pmtud.next(now, &mut stats);

        if let Some(iface_mtu) = iface_mtu {
            assert!(iface_mtu <= pmtud.search_table[1] || pmtud.needs_probe());
        } else {
            assert!(pmtud.needs_probe());
        }

        while pmtud.needs_probe() {
            pmtud_step(&mut pmtud, &mut stats, &mut prot, addr, mtu, now);
        }

        let final_mtu = iface_mtu.map_or(mtu, |iface_mtu| min(mtu, iface_mtu));
        assert_mtu(&pmtud, final_mtu);

        (pmtud, stats, prot, now)
    }

    /// Tests that when the path MTU decreases, PMTUD does not automatically reprobe downward.
    /// The raise timer only triggers probing for *larger* MTUs. MTU reductions are not
    /// automatically detected by PMTUD; the connection will continue using the old MTU
    /// and packets will be lost until the raise timer fires and probing completes at
    /// the same or a higher MTU (depending on path conditions).
    fn find_pmtu_no_reduction_detection(addr: IpAddr, mtu: usize) {
        let (mut pmtud, mut stats, _prot, now) = find_pmtu(addr, mtu, None);

        // The current MTU is set.
        let current_mtu = pmtud.mtu;
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        // Fire the raise timer - this only triggers probing for *higher* MTUs.
        debug!("Firing raise timer after reaching MTU {current_mtu}");
        let now = now + PMTU_RAISE_TIMER;
        pmtud.maybe_fire_raise_timer(now, &mut stats);

        // If we're not at the max MTU, the timer should trigger a probe for a higher MTU.
        // If we're at the max MTU (or interface limit), no probe is needed.
        if pmtud.probe_index < SEARCH_TABLE_LEN - 1
            && pmtud.search_table[pmtud.probe_index + 1] <= pmtud.iface_mtu
        {
            // Timer should have started probing for a larger MTU.
            assert_eq!(Probe::Needed, pmtud.probe_state);
        } else {
            // At max MTU, timer doesn't change state.
            assert_eq!(Probe::NotNeeded, pmtud.probe_state);
        }

        // Regardless, the current MTU should be unchanged.
        assert_eq!(current_mtu, pmtud.mtu);
    }

    fn find_pmtu_with_increase(addr: IpAddr, mtu: usize, larger_mtu: usize) {
        assert!(mtu < larger_mtu);
        let (mut pmtud, mut stats, mut prot, now) = find_pmtu(addr, mtu, None);

        assert!(larger_mtu >= pmtud.search_table[0]);
        pmtud.next(now, &mut stats);
        assert!(pmtud.needs_probe());

        while pmtud.needs_probe() {
            pmtud_step(&mut pmtud, &mut stats, &mut prot, addr, mtu, now);
        }
        assert_mtu(&pmtud, mtu);

        debug!("Increasing MTU to {larger_mtu}");
        let now = now + PMTU_RAISE_TIMER;
        pmtud.maybe_fire_raise_timer(now, &mut stats);
        while pmtud.needs_probe() {
            pmtud_step(&mut pmtud, &mut stats, &mut prot, addr, larger_mtu, now);
        }
        assert_mtu(&pmtud, larger_mtu);
    }

    fn path_mtus() -> Vec<usize> {
        IFACE_MTUS.iter().flatten().copied().collect()
    }

    #[test]
    fn pmtud() {
        for &addr in &[V4, V6] {
            for path_mtu in path_mtus() {
                for &iface_mtu in IFACE_MTUS {
                    info!("PMTUD for {addr}, path MTU {path_mtu}, iface MTU {iface_mtu:?}");
                    find_pmtu(addr, path_mtu, iface_mtu);
                }
            }
        }
    }

    /// Tests that the raise timer only probes upward, not downward.
    #[test]
    fn raise_timer_probes_upward_only() {
        for &addr in &[V4, V6] {
            for path_mtu in path_mtus() {
                info!("Testing raise timer behavior for {addr}, path MTU {path_mtu}");
                find_pmtu_no_reduction_detection(addr, path_mtu);
            }
        }
    }

    #[test]
    fn pmtud_with_increase() {
        for &addr in &[V4, V6] {
            for path_mtu in path_mtus() {
                let path_mtus = path_mtus();
                let larger_mtus = path_mtus.iter().filter(|&mtu| *mtu > path_mtu);
                for &larger_mtu in larger_mtus {
                    info!("PMTUD for {addr}, path MTU {path_mtu}, larger path MTU {larger_mtu}");
                    find_pmtu_with_increase(addr, path_mtu, larger_mtu);
                }
            }
        }
    }

    /// Tests that losing non-probe packets does not affect PMTUD state.
    #[test]
    fn non_probe_loss_ignored() {
        const MTU: usize = 1500;
        let now = now();
        let mut pmtud = new_pmtud(V4, Some(MTU));
        let mut stats = Stats::default();

        // Complete PMTUD at MTU 1500.
        pmtud.stop(
            pmtud
                .search_table
                .iter()
                .position(|&mtu| mtu == MTU)
                .unwrap(),
            now,
            &mut stats,
        );
        assert_mtu(&pmtud, MTU);
        let initial_lost = stats.pmtud_lost;

        // Lose various non-probe packets - should not change PMTUD state.
        pmtud.on_packets_lost(&[], &mut stats, now);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        pmtud.on_packets_lost(&[sent::make_packet(0, now, 100)], &mut stats, now);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        pmtud.on_packets_lost(&[sent::make_packet(1, now, 1000)], &mut stats, now);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        // No probe losses should have been recorded.
        assert_eq!(initial_lost, stats.pmtud_lost);
    }

    /// Tests that PMTUD respects the peer's `max_udp_payload_size` transport parameter
    /// as an upper bound for probing.
    fn find_pmtu_with_peer_max(
        addr: IpAddr,
        mtu: usize,
        iface_mtu: Option<usize>,
        peer_max_udp_payload: usize,
    ) -> Pmtud {
        fixture_init();
        let now = now();
        let mut pmtud = new_pmtud(addr, iface_mtu);
        pmtud.set_peer_max_udp_payload(peer_max_udp_payload);
        let mut stats = Stats::default();
        let mut prot = CryptoDxState::test_default_write();

        pmtud.start(now, &mut stats);

        while pmtud.needs_probe() {
            pmtud_step(&mut pmtud, &mut stats, &mut prot, addr, mtu, now);
        }

        // The effective upper limit is the minimum of:
        // - the actual path MTU
        // - the interface MTU (if set)
        // - the peer's max_udp_payload_size + header_size
        let peer_limit = peer_max_udp_payload + Pmtud::header_size(addr);
        let effective = mtu.min(iface_mtu.unwrap_or(usize::MAX)).min(peer_limit);
        assert_mtu(&pmtud, effective);

        pmtud
    }

    #[test]
    fn pmtud_respects_peer_max_udp_payload_size() {
        let pmtud = find_pmtu_with_peer_max(V4, 9000, None, 1452);
        assert_eq!(pmtud.mtu, 1472);
    }

    #[test]
    fn pmtud_peer_max_smaller_than_iface_mtu() {
        let pmtud = find_pmtu_with_peer_max(V4, 9000, Some(9000), 1452);
        assert_eq!(pmtud.mtu, 1472);
    }

    #[test]
    fn pmtud_iface_mtu_smaller_than_peer_max() {
        let pmtud = find_pmtu_with_peer_max(V4, 9000, Some(1400), 9000);
        assert_eq!(pmtud.mtu, 1380);
    }

    #[test]
    fn pmtud_peer_max_v6() {
        let pmtud = find_pmtu_with_peer_max(V6, 9000, None, 1452);
        assert_eq!(pmtud.mtu, 1500);
    }

    #[test]
    fn peer_max_udp_payload_accessor() {
        let mut pmtud = Pmtud::new(V4);
        assert_eq!(pmtud.peer_max_udp_payload(), None);

        pmtud.set_peer_max_udp_payload(1452);
        assert_eq!(pmtud.peer_max_udp_payload(), Some(1452));
    }

    /// `probe_count` is 0 before sending and 1 after the first probe.
    #[test]
    fn send_probe_increments_count() {
        fixture_init();
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        pmtud.next(now, &mut stats);
        assert!(pmtud.needs_probe());
        assert_eq!(pmtud.probe_count, 0);

        let limit = pmtud.probe_size() - CryptoDxState::test_default_write().expansion();
        let mut builder = packet::Builder::short(Encoder::default(), false, None::<&[u8]>, limit);
        pmtud.send_probe(&mut builder, &mut Vec::new(), &mut stats);
        assert_eq!(
            pmtud.probe_count, 1,
            "probe_count must be 1 after first probe"
        );
    }

    /// PMTUD gives up after exactly `MAX_PROBES` consecutive probe failures, not fewer.
    #[test]
    fn max_probes_required_before_giving_up() {
        // Use a path MTU smaller than the first probe so every probe fails.
        const PATH_MTU: usize = 1200; // Below all probes in the search table
        fixture_init();
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        let mut prot = CryptoDxState::test_default_write();
        pmtud.next(now, &mut stats);

        while pmtud.needs_probe() {
            pmtud_step(&mut pmtud, &mut stats, &mut prot, V4, PATH_MTU, now);
        }

        // With probe_count >= MAX_PROBES, it takes exactly MAX_PROBES losses per probe size
        // before giving up. The first (minimum) probe size always fails MAX_PROBES times.
        assert!(
            stats.pmtud_lost >= MAX_PROBES,
            "must lose at least MAX_PROBES ({MAX_PROBES}) probes, got {}",
            stats.pmtud_lost
        );
    }

    #[test]
    fn non_probe_ack_ignored() {
        const MTU: usize = 1500;
        let now = now();
        let mut pmtud = new_pmtud(V4, Some(MTU));
        let mut stats = Stats::default();

        // Complete PMTUD at MTU 1500.
        pmtud.stop(
            pmtud
                .search_table
                .iter()
                .position(|&mtu| mtu == MTU)
                .unwrap(),
            now,
            &mut stats,
        );
        assert_mtu(&pmtud, MTU);
        let initial_ack = stats.pmtud_ack;

        // ACK various non-probe packets - should not change PMTUD state.
        pmtud.on_packets_acked(&[], now, &mut stats);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        pmtud.on_packets_acked(&[sent::make_packet(0, now, 100)], now, &mut stats);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        pmtud.on_packets_acked(&[sent::make_packet(1, now, 5000)], now, &mut stats);
        assert_eq!(Probe::NotNeeded, pmtud.probe_state);

        // No probe ACKs should have been recorded.
        assert_eq!(initial_ack, stats.pmtud_ack);
    }

    /// A probe that is spuriously declared lost gets retried. When both the original and the retry
    /// are acknowledged, only the first ACK confirms the size under test. The second one is for a
    /// probe of the same size, so it says nothing about the next size, which has not been probed.
    #[test]
    fn retried_probe_ack_does_not_confirm_next_size() {
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        pmtud.next(now, &mut stats);
        let first = pmtud.probe_size();

        let original = send_probe_packet(&mut pmtud, &mut stats, 0, now);
        pmtud.on_packets_lost(std::slice::from_ref(&original), &mut stats, now);
        assert!(pmtud.needs_probe());
        let retry = send_probe_packet(&mut pmtud, &mut stats, 1, now);

        // The late ACK of the original confirms its size and moves on to the next one.
        pmtud.on_packets_acked(&[original], now, &mut stats);
        assert_eq!(pmtud.plpmtu(), first);
        assert!(pmtud.needs_probe());
        let second = pmtud.probe_size();
        assert!(second > first);

        // The ACK of the retry must not confirm the next size.
        pmtud.on_packets_acked(&[retry], now, &mut stats);
        assert_eq!(pmtud.plpmtu(), first, "an untested size was confirmed");
        assert!(pmtud.needs_probe());
        assert_eq!(pmtud.probe_size(), second);
        assert_eq!(stats.pmtud_ack, 1, "stale probe ACK must not be counted");
    }

    /// The late ACK of a probe for an earlier size must neither confirm the size of the probe that
    /// is currently in flight, nor make PMTUD discard that probe.
    #[test]
    fn stale_probe_ack_does_not_disturb_probe_in_flight() {
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        pmtud.next(now, &mut stats);
        let first = pmtud.probe_size();

        let original = send_probe_packet(&mut pmtud, &mut stats, 0, now);
        pmtud.on_packets_lost(std::slice::from_ref(&original), &mut stats, now);
        let retry = send_probe_packet(&mut pmtud, &mut stats, 1, now);
        pmtud.on_packets_acked(&[retry], now, &mut stats);
        assert_eq!(pmtud.plpmtu(), first);

        // Probe for the next size is now in flight.
        let second = pmtud.probe_size();
        assert!(second > first);
        let next = send_probe_packet(&mut pmtud, &mut stats, 2, now);
        assert_eq!(pmtud.probe_state, Probe::Sent);

        // The original of the earlier size is ACKed very late. This changes nothing.
        pmtud.on_packets_acked(&[original], now, &mut stats);
        assert_eq!(pmtud.plpmtu(), first, "an untested size was confirmed");
        assert_eq!(pmtud.probe_state, Probe::Sent, "probe in flight dropped");
        assert_eq!(pmtud.probe_size(), second);
        assert_eq!(stats.pmtud_ack, 1, "stale probe ACK must not be counted");

        // Only the ACK of the probe in flight confirms the next size.
        pmtud.on_packets_acked(&[next], now, &mut stats);
        assert_eq!(pmtud.plpmtu(), second);
        assert_eq!(stats.pmtud_ack, 2);
    }

    /// An ACK batch can contain stale probes, current probes and non-probe packets.
    #[test]
    fn mixed_probe_acks_only_count_current_size() {
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        pmtud.next(now, &mut stats);
        let original_size = pmtud.probe_size();

        let original = send_probe_packet(&mut pmtud, &mut stats, 0, now);
        pmtud.on_packets_lost(std::slice::from_ref(&original), &mut stats, now);
        let stale = send_probe_packet(&mut pmtud, &mut stats, 1, now);
        pmtud.on_packets_acked(&[original], now, &mut stats);
        assert_eq!(stats.pmtud_ack, 1);

        let size = pmtud.probe_size();
        assert!(size > original_size);
        let current = send_probe_packet(&mut pmtud, &mut stats, 2, now);
        pmtud.on_packets_lost(std::slice::from_ref(&current), &mut stats, now);
        let retry = send_probe_packet(&mut pmtud, &mut stats, 3, now);
        let large_non_probe = sent::make_packet(4, now, size);
        pmtud.on_packets_acked(&[stale, large_non_probe, current, retry], now, &mut stats);

        assert_eq!(pmtud.plpmtu(), size);
        assert!(pmtud.needs_probe());
        assert!(pmtud.probe_size() > size);
        assert_eq!(
            stats.pmtud_ack, 3,
            "only current probe ACKs must be counted"
        );
    }

    /// Once the search has stopped, the late ACK of a probe that is still outstanding must not
    /// start it again before the raise timer fires.
    #[test]
    fn stale_probe_ack_after_search_stopped_ignored() {
        let now = now();
        let mut pmtud = Pmtud::new(V4);
        let mut stats = Stats::default();
        pmtud.next(now, &mut stats);
        let first = pmtud.search_table[1];

        // Two probes of the first size are in flight. The ACK of one of them confirms the size.
        let original = send_probe_packet(&mut pmtud, &mut stats, 0, now);
        pmtud.on_packets_lost(std::slice::from_ref(&original), &mut stats, now);
        let retry = send_probe_packet(&mut pmtud, &mut stats, 1, now);
        pmtud.on_packets_acked(&[original], now, &mut stats);
        assert_eq!(pmtud.mtu, first);

        // All probes of the next size are lost, which stops the search at the first size.
        let mut pn = 2;
        while pmtud.needs_probe() {
            let probe = send_probe_packet(&mut pmtud, &mut stats, pn, now);
            pmtud.on_packets_lost(&[probe], &mut stats, now);
            pn += 1;
        }
        assert_mtu(&pmtud, first);
        let raise_timer = pmtud.raise_timer;
        assert!(raise_timer.is_some());

        // The late ACK of the retry has the size the search stopped at. It must not restart it.
        pmtud.on_packets_acked(&[retry], now, &mut stats);
        assert_mtu(&pmtud, first);
        assert_eq!(pmtud.raise_timer, raise_timer);
        assert!(!pmtud.needs_probe());
        assert_eq!(stats.pmtud_ack, 1, "stale probe ACK must not be counted");
    }
}
