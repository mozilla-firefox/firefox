// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{mem, time::Instant};

use neqo_common::Datagram;
use tracing::{debug, info};

use crate::crypto::Epoch;

pub struct SavedDatagram {
    /// The datagram.
    pub d: Datagram,
    /// The time that the datagram was received.
    pub t: Instant,
}

#[derive(Default)]
pub struct SavedDatagrams {
    handshake: Vec<SavedDatagram>,
    application_data: Vec<SavedDatagram>,
    available: Option<Epoch>,
}

impl SavedDatagrams {
    /// The number of datagrams that are saved during the handshake when
    /// keys to decrypt them are not yet available.
    pub const CAPACITY: usize = 4;

    fn store(&mut self, epoch: Epoch) -> &mut Vec<SavedDatagram> {
        match epoch {
            Epoch::Handshake => &mut self.handshake,
            Epoch::ApplicationData => &mut self.application_data,
            _ => panic!("unexpected space"),
        }
    }

    /// Return whether either store of datagrams is currently full.
    pub const fn is_either_full(&self) -> bool {
        self.handshake.len() == Self::CAPACITY || self.application_data.len() == Self::CAPACITY
    }

    pub fn save(&mut self, epoch: Epoch, d: Datagram, t: Instant) {
        let store = self.store(epoch);

        if store.len() < Self::CAPACITY {
            debug!("saving {epoch:?} datagram of {} bytes", d.len());
            store.push(SavedDatagram { d, t });
        } else {
            info!("not saving {epoch:?} datagram of {} bytes", d.len());
        }
    }

    pub fn make_available(&mut self, epoch: Epoch) {
        debug_assert_ne!(epoch, Epoch::ZeroRtt);
        debug_assert_ne!(epoch, Epoch::Initial);
        if !self.store(epoch).is_empty() {
            self.available = Some(epoch);
        }
    }

    pub const fn available(&self) -> Option<Epoch> {
        self.available
    }

    pub fn take_saved(&mut self) -> Vec<SavedDatagram> {
        self.available
            .take()
            .map_or_else(Vec::new, |epoch| mem::take(self.store(epoch)))
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::net::SocketAddr;

    use neqo_common::{Tos, datagram::Datagram};
    use test_fixture::now;

    use super::SavedDatagrams;
    use crate::crypto::Epoch;

    fn make_dgram() -> Datagram {
        let addr: SocketAddr = "127.0.0.1:443".parse().unwrap();
        Datagram::new(addr, addr, Tos::default(), vec![0x00])
    }

    #[test]
    #[should_panic(expected = "unexpected space")]
    fn save_panics_for_invalid_epoch() {
        let mut saved = SavedDatagrams::default();
        saved.save(Epoch::Initial, make_dgram(), now());
    }

    #[test]
    fn capacity_is_enforced() {
        let mut saved = SavedDatagrams::default();
        let t = now();

        // Fill to exactly CAPACITY.
        for _ in 0..SavedDatagrams::CAPACITY {
            saved.save(Epoch::ApplicationData, make_dgram(), t);
        }
        assert!(saved.is_either_full());

        // One more should be silently dropped.
        saved.save(Epoch::ApplicationData, make_dgram(), t);

        saved.make_available(Epoch::ApplicationData);
        let taken = saved.take_saved();
        assert_eq!(taken.len(), SavedDatagrams::CAPACITY);
    }
}
