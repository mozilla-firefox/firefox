// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    cmp::{max, min},
    time::{Duration, Instant},
};

use tracing::trace;

use crate::recovery;

#[derive(Debug, Clone)]
/// There's a little bit of different behavior for resetting idle timeout. See
/// -transport 10.2 ("Idle Timeout").
enum IdleTimeoutState {
    Init,
    PacketReceived(Instant),
    AckElicitingPacketSent(Instant),
}

#[derive(Debug, Clone)]
/// There's a little bit of different behavior for resetting idle timeout. See
/// -transport 10.1 ("Idle Timeout").
///
/// <https://datatracker.ietf.org/doc/html/rfc9000#section-10.1>
pub struct IdleTimeout {
    timeout: Duration,
    state: IdleTimeoutState,
    keep_alive_outstanding: bool,
}

impl IdleTimeout {
    pub const fn new(timeout: Duration) -> Self {
        Self {
            timeout,
            state: IdleTimeoutState::Init,
            keep_alive_outstanding: false,
        }
    }
    pub fn set_peer_timeout(&mut self, peer_timeout: Duration) {
        self.timeout = min(self.timeout, peer_timeout);
    }

    const fn start(&self, now: Instant) -> Instant {
        match self.state {
            IdleTimeoutState::Init => now,
            IdleTimeoutState::PacketReceived(t) | IdleTimeoutState::AckElicitingPacketSent(t) => t,
        }
    }

    pub fn expiry(&self, now: Instant, pto: Duration) -> Instant {
        let delay = max(self.timeout, pto * 3);
        let t = self.start(now) + delay;
        trace!("IdleTimeout::expiry@{now:?} pto={pto:?} => {t:?}");
        t
    }

    pub const fn on_packet_sent(&mut self, now: Instant) {
        // Only reset idle timeout if we've received a packet since the last
        // time we reset the timeout here.
        match self.state {
            // > An endpoint also restarts its idle timer when sending an
            // > ack-eliciting packet if no other ack-eliciting packets have
            // > been sent since last receiving and processing a packet.
            //
            // <https://datatracker.ietf.org/doc/html/rfc9000#section-10.1>
            //
            // Conversely, given that a packet has been sent since last
            // receival, don't reset idle timer.
            IdleTimeoutState::AckElicitingPacketSent(_) => {}
            IdleTimeoutState::Init | IdleTimeoutState::PacketReceived(_) => {
                self.state = IdleTimeoutState::AckElicitingPacketSent(now);
            }
        }
    }

    pub fn on_packet_received(&mut self, now: Instant) {
        // Only update if this doesn't rewind the idle timeout.
        // We sometimes process packets after caching them, which uses
        // the time the packet was received.  That could be in the past.
        let update = match self.state {
            IdleTimeoutState::Init => true,
            IdleTimeoutState::AckElicitingPacketSent(t) | IdleTimeoutState::PacketReceived(t) => {
                t <= now
            }
        };
        if update {
            self.state = IdleTimeoutState::PacketReceived(now);
        }
    }

    pub fn expired(&self, now: Instant, pto: Duration) -> bool {
        now >= self.expiry(now, pto)
    }

    fn keep_alive_timeout(&self, now: Instant, pto: Duration) -> Instant {
        // For a keep-alive timer, wait for half the timeout interval, but be sure
        // not to wait too little or we will send many unnecessary probes.
        self.start(now) + max(self.timeout / 2, pto)
    }

    pub fn next_keep_alive(&self, now: Instant, pto: Duration) -> Option<Instant> {
        if self.keep_alive_outstanding {
            return None;
        }

        let timeout = self.keep_alive_timeout(now, pto);
        // Timer is in the past, i.e. we should have sent a keep alive,
        // but we were unable to, e.g. due to CC.
        if timeout <= now {
            return None;
        }

        Some(timeout)
    }

    pub fn send_keep_alive(
        &mut self,
        now: Instant,
        pto: Duration,
        tokens: &mut recovery::Tokens,
    ) -> bool {
        if !self.keep_alive_outstanding && now >= self.keep_alive_timeout(now, pto) {
            self.keep_alive_outstanding = true;
            tokens.push(recovery::Token::KeepAlive);
            true
        } else {
            false
        }
    }

    pub const fn lost_keep_alive(&mut self) {
        self.keep_alive_outstanding = false;
    }

    pub const fn ack_keep_alive(&mut self) {
        self.keep_alive_outstanding = false;
    }
}
