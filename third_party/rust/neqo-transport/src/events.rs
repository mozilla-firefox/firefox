// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Collecting a list of events relevant to whoever is using the Connection.

use std::{net::SocketAddr, num::NonZeroU64};

use neqo_common::event::{Provider as EventProvider, Queue as EventQueue};
use nss::ResumptionToken;

use crate::{
    AppError,
    connection::State,
    quic_datagrams::DatagramTracking,
    scone::Bitrate,
    stream_id::{StreamId, StreamType},
};

#[derive(Debug, PartialOrd, Ord, PartialEq, Eq)]
pub enum OutgoingDatagramOutcome {
    DroppedTooBig,
    Lost,
    Acked,
}

#[derive(Debug, PartialOrd, Ord, PartialEq, Eq)]
pub enum ConnectionEvent {
    /// Cert authentication needed
    AuthenticationNeeded,
    /// Encrypted client hello fallback occurred.  The certificate for the
    /// public name needs to be authenticated.
    EchFallbackAuthenticationNeeded {
        public_name: String,
    },
    /// A new uni (read) or bidi stream has been opened by the peer.
    NewStream {
        stream_id: StreamId,
    },
    /// Space available in the buffer for an application write to succeed.
    SendStreamWritable {
        stream_id: StreamId,
    },
    /// New bytes available for reading.
    RecvStreamReadable {
        stream_id: StreamId,
    },
    /// Peer reset the stream.
    RecvStreamReset {
        stream_id: StreamId,
        app_error: AppError,
    },
    /// Peer has sent `STOP_SENDING`
    SendStreamStopSending {
        stream_id: StreamId,
        app_error: AppError,
    },
    /// Peer has acked everything sent on the stream.
    SendStreamComplete {
        stream_id: StreamId,
    },
    /// Peer increased `MAX_STREAMS`
    SendStreamCreatable {
        stream_type: StreamType,
    },
    /// Connection state change.
    StateChange(State),
    /// The server rejected 0-RTT.
    /// This event invalidates all state in streams that has been created.
    /// Any data written to streams needs to be written again.
    ZeroRttRejected,
    ResumptionToken(ResumptionToken),
    Datagram(Vec<u8>),
    OutgoingDatagramOutcome {
        id: u64,
        outcome: OutgoingDatagramOutcome,
    },
    /// The outgoing QUIC datagram queue of session `stream_id` has space again
    /// after having been full.
    OutgoingDatagramSpaceAvailable {
        stream_id: StreamId,
    },
    /// An update was received to SCONE throughput advice.
    /// The value is the approximate rate in bits per second; None = unknown.
    SconeUpdated(Option<NonZeroU64>),
    /// A path migration completed; the connection is now sending on this path.
    PathMigrated {
        local: SocketAddr,
        remote: SocketAddr,
    },
}

#[derive(Debug, Default, Clone)]
pub struct ConnectionEvents {
    events: EventQueue<ConnectionEvent>,
}

impl ConnectionEvents {
    pub fn authentication_needed(&self) {
        self.events.push(ConnectionEvent::AuthenticationNeeded);
    }

    pub fn ech_fallback_authentication_needed(&self, public_name: String) {
        self.events
            .push_unique(ConnectionEvent::EchFallbackAuthenticationNeeded { public_name });
    }

    pub fn new_stream(&self, stream_id: StreamId) {
        self.events.push(ConnectionEvent::NewStream { stream_id });
    }

    pub fn recv_stream_readable(&self, stream_id: StreamId) {
        self.events
            .push_unique(ConnectionEvent::RecvStreamReadable { stream_id });
    }

    pub fn recv_stream_reset(&self, stream_id: StreamId, app_error: AppError) {
        // If reset, no longer readable.
        self.events.remove_matching(|evt| matches!(evt, ConnectionEvent::RecvStreamReadable { stream_id: x } if *x == stream_id.as_u64()));

        // A duplicate is any reset for the stream, even with a different error.
        self.events.push_unique_by(
            ConnectionEvent::RecvStreamReset {
                stream_id,
                app_error,
            },
            |evt| {
                matches!(evt, ConnectionEvent::RecvStreamReset { stream_id: x, .. }
                    if *x == stream_id)
            },
        );
    }

    pub fn send_stream_writable(&self, stream_id: StreamId) {
        self.events
            .push_unique(ConnectionEvent::SendStreamWritable { stream_id });
    }

    pub fn send_stream_stop_sending(&self, stream_id: StreamId, app_error: AppError) {
        // If stopped, no longer writable.
        self.events.remove_matching(|evt| matches!(evt, ConnectionEvent::SendStreamWritable { stream_id: x } if *x == stream_id));

        // A duplicate is any stop for the stream, even with a different error.
        self.events.push_unique_by(
            ConnectionEvent::SendStreamStopSending {
                stream_id,
                app_error,
            },
            |evt| {
                matches!(evt, ConnectionEvent::SendStreamStopSending { stream_id: x, .. }
                    if *x == stream_id)
            },
        );
    }

    pub fn send_stream_complete(&self, stream_id: StreamId) {
        self.events.remove_matching(|evt| {
            matches!(evt,
                ConnectionEvent::SendStreamWritable { stream_id: x } |
                ConnectionEvent::SendStreamStopSending { stream_id: x, .. } |
                ConnectionEvent::SendStreamComplete { stream_id: x }
                if *x == stream_id)
        });

        self.events
            .push(ConnectionEvent::SendStreamComplete { stream_id });
    }

    pub fn send_stream_creatable(&self, stream_type: StreamType) {
        self.events
            .push_unique(ConnectionEvent::SendStreamCreatable { stream_type });
    }

    pub fn connection_state_change(&self, state: State) {
        // If closing, existing events no longer relevant.
        // The session ticket in a queued resumption token cannot be retrieved again.
        if state.closed() {
            self.events
                .remove_matching(|evt| !matches!(evt, ConnectionEvent::ResumptionToken(_)));
        }
        self.events.push_unique(ConnectionEvent::StateChange(state));
    }

    pub fn client_resumption_token(&self, token: ResumptionToken) {
        self.events.push(ConnectionEvent::ResumptionToken(token));
    }

    pub fn client_0rtt_rejected(&self) {
        // If 0rtt rejected, must start over and existing events are no longer
        // relevant.
        self.events.clear();
        self.events.push(ConnectionEvent::ZeroRttRejected);
    }

    pub fn recv_stream_complete(&self, stream_id: StreamId) {
        // If stopped, no longer readable.
        self.events.remove_matching(|evt| matches!(evt, ConnectionEvent::RecvStreamReadable { stream_id: x } if *x == stream_id.as_u64()));
    }

    pub fn scone_updated(&self, scone: Bitrate) {
        self.events
            .remove_matching(|evt| matches!(evt, ConnectionEvent::SconeUpdated(_)));
        self.events
            .push_unique(ConnectionEvent::SconeUpdated(Option::from(scone)));
    }

    pub fn add_datagram(&self, data: &[u8]) {
        self.events.push(ConnectionEvent::Datagram(data.to_vec()));
    }

    pub fn datagram_space_available(&self, stream_id: StreamId) {
        self.events
            .push_unique(ConnectionEvent::OutgoingDatagramSpaceAvailable { stream_id });
    }

    pub fn datagram_outcome(
        &self,
        dgram_tracker: &DatagramTracking,
        outcome: OutgoingDatagramOutcome,
    ) {
        if let DatagramTracking::Id(id) = dgram_tracker {
            self.events
                .push(ConnectionEvent::OutgoingDatagramOutcome { id: *id, outcome });
        }
    }

    pub fn path_migrated(&self, local: SocketAddr, remote: SocketAddr) {
        self.events
            .push(ConnectionEvent::PathMigrated { local, remote });
    }
}

impl EventProvider for ConnectionEvents {
    type Event = ConnectionEvent;

    fn has_events(&self) -> bool {
        !self.events.is_empty()
    }

    fn next_event(&mut self) -> Option<Self::Event> {
        self.events.next_event()
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::event::Provider as _;

    use crate::{CloseReason, ConnectionEvent, ConnectionEvents, Error, State, StreamId};

    #[test]
    fn event_culling() {
        let mut evts = ConnectionEvents::default();
        assert!(!evts.has_events());

        evts.client_0rtt_rejected();
        assert!(evts.has_events());
        evts.client_0rtt_rejected();
        assert_eq!(evts.events().count(), 1);
        assert_eq!(evts.events().count(), 0);

        evts.recv_stream_readable(6.into());
        evts.recv_stream_reset(6.into(), 66);
        evts.recv_stream_reset(6.into(), 65);
        assert_eq!(evts.events().count(), 1);

        evts.send_stream_writable(8.into());
        evts.send_stream_writable(8.into());
        evts.send_stream_stop_sending(8.into(), 55);
        evts.send_stream_stop_sending(8.into(), 56);
        let events = evts.events().collect::<Vec<_>>();
        assert_eq!(events.len(), 1);
        assert_eq!(
            events[0],
            ConnectionEvent::SendStreamStopSending {
                stream_id: StreamId::new(8),
                app_error: 55
            }
        );

        evts.send_stream_writable(8.into());
        evts.send_stream_writable(8.into());
        evts.send_stream_stop_sending(8.into(), 55);
        evts.send_stream_stop_sending(8.into(), 56);
        evts.send_stream_complete(8.into());
        assert_eq!(evts.events().count(), 1);

        evts.send_stream_writable(8.into());
        evts.send_stream_writable(9.into());
        evts.send_stream_stop_sending(10.into(), 55);
        evts.send_stream_stop_sending(11.into(), 56);
        evts.send_stream_complete(12.into());
        assert_eq!(evts.events().count(), 5);

        evts.send_stream_writable(8.into());
        evts.send_stream_writable(9.into());
        evts.send_stream_stop_sending(10.into(), 55);
        evts.send_stream_stop_sending(11.into(), 56);
        evts.send_stream_complete(12.into());
        evts.client_0rtt_rejected();
        assert_eq!(evts.events().count(), 1);

        evts.send_stream_writable(9.into());
        evts.send_stream_stop_sending(10.into(), 55);
        evts.connection_state_change(State::Closed(CloseReason::Transport(Error::StreamState)));
        assert_eq!(evts.events().count(), 1);
    }
}
