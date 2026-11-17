// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use neqo_common::{
    Bytes, Header,
    event::{Provider as EventProvider, Queue as EventQueue},
};
use neqo_transport::{AppError, StreamId, StreamType};
use nss::ResumptionToken;
use tracing::trace;

use crate::{
    CloseType, Error, Http3StreamInfo, HttpRecvStreamEvents, RecvStreamEvents, Res,
    SendStreamEvents,
    connection::Http3State,
    features::extended_connect::{self, ExtendedConnectEvents, ExtendedConnectType},
    settings::HSettingType,
};

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum WebTransportEvent {
    Negotiated(
        /// Whether WebTransport was negotiated.
        bool,
    ),
    NewSession {
        stream_id: StreamId,
        status: u16,
        headers: Vec<Header>,
    },
    SessionClosed {
        stream_id: StreamId,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    },
    Draining {
        stream_id: StreamId,
    },
    NewStream {
        stream_id: StreamId,
        session_id: StreamId,
    },
    Datagram {
        session_id: StreamId,
        datagram: Bytes,
    },
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum ConnectUdpEvent {
    Negotiated(
        /// Whether CONNECT-UDP was negotiated.
        bool,
    ),
    NewSession {
        stream_id: StreamId,
        status: u16,
        headers: Vec<Header>,
    },
    SessionClosed {
        stream_id: StreamId,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    },
    Datagram {
        session_id: StreamId,
        datagram: Bytes,
    },
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum Http3ClientEvent {
    /// Response headers are received.
    HeaderReady {
        stream_id: StreamId,
        headers: Vec<Header>,
        interim: bool,
        fin: bool,
    },
    /// A stream can accept new data.
    DataWritable { stream_id: StreamId },
    /// New bytes available for reading.
    DataReadable { stream_id: StreamId },
    /// Peer reset the stream or there was an parsing error.
    Reset {
        stream_id: StreamId,
        error: AppError,
        local: bool,
    },
    /// Peer has sent a `STOP_SENDING`.
    StopSending {
        stream_id: StreamId,
        error: AppError,
    },
    /// New stream can be created
    RequestsCreatable,
    /// Cert authentication needed
    AuthenticationNeeded,
    /// Encrypted client hello fallback occurred.  The certificate for the
    /// name `public_name` needs to be authenticated in order to get
    /// an updated ECH configuration.
    EchFallbackAuthenticationNeeded { public_name: String },
    /// A new resumption token.
    ResumptionToken(ResumptionToken),
    /// Zero Rtt has been rejected.
    ZeroRttRejected,
    /// Client has received a GOAWAY frame
    GoawayReceived,
    /// Connection state change.
    StateChange(Http3State),
    /// `WebTransport` events
    WebTransport(WebTransportEvent),
    /// `ConnectUdp` events
    ConnectUdp(ConnectUdpEvent),
    /// The outgoing QUIC datagram queue of session `session_id` has space again after having been
    /// full.
    OutgoingDatagramSpaceAvailable { session_id: StreamId },
}

#[derive(Debug, Default, Clone)]
pub struct Http3ClientEvents {
    events: EventQueue<Http3ClientEvent>,
}

impl RecvStreamEvents for Http3ClientEvents {
    /// Add a new `DataReadable` event
    fn data_readable(&self, stream_info: &Http3StreamInfo) {
        self.events.push(Http3ClientEvent::DataReadable {
            stream_id: stream_info.stream_id(),
        });
    }

    /// Add a new `Reset` event.
    fn recv_closed(&self, stream_info: &Http3StreamInfo, close_type: CloseType) {
        let stream_id = stream_info.stream_id();
        let (local, error) = match close_type {
            CloseType::ResetApp(_) => {
                self.remove_recv_stream_events(stream_id, false);
                return;
            }
            CloseType::Done => return,
            CloseType::ResetRemote(e) => {
                // Preserve a committed header block delivered by a reliable reset.
                self.remove_recv_stream_events(stream_id, true);
                (false, e)
            }
            CloseType::LocalError(e) => {
                self.remove_recv_stream_events(stream_id, false);
                (true, e)
            }
        };

        self.events.push(Http3ClientEvent::Reset {
            stream_id,
            error,
            local,
        });
    }
}

impl HttpRecvStreamEvents for Http3ClientEvents {
    /// Add a new `HeaderReady` event.
    fn header_ready(
        &self,
        stream_info: &Http3StreamInfo,
        headers: Vec<Header>,
        interim: bool,
        fin: bool,
    ) {
        self.events.push(Http3ClientEvent::HeaderReady {
            stream_id: stream_info.stream_id(),
            headers,
            interim,
            fin,
        });
    }
}

impl SendStreamEvents for Http3ClientEvents {
    /// Add a new `DataWritable` event.
    fn data_writable(&self, stream_info: &Http3StreamInfo) {
        self.events.push(Http3ClientEvent::DataWritable {
            stream_id: stream_info.stream_id(),
        });
    }

    fn send_closed(&self, stream_info: &Http3StreamInfo, close_type: CloseType) {
        let stream_id = stream_info.stream_id();
        self.remove_send_stream_events(stream_id);
        if let CloseType::ResetRemote(error) = close_type {
            self.events
                .push(Http3ClientEvent::StopSending { stream_id, error });
        }
    }
}

impl ExtendedConnectEvents for Http3ClientEvents {
    fn session_start(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        status: u16,
        headers: Vec<Header>,
    ) {
        match connect_type {
            ExtendedConnectType::WebTransport => {
                self.events.push(Http3ClientEvent::WebTransport(
                    WebTransportEvent::NewSession {
                        stream_id,
                        status,
                        headers,
                    },
                ));
            }
            ExtendedConnectType::ConnectUdp => {
                self.events
                    .push(Http3ClientEvent::ConnectUdp(ConnectUdpEvent::NewSession {
                        stream_id,
                        status,
                        headers,
                    }));
            }
        }
    }

    fn session_end(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        reason: extended_connect::session::CloseReason,
        headers: Option<Vec<Header>>,
    ) {
        let event = match connect_type {
            ExtendedConnectType::WebTransport => {
                Http3ClientEvent::WebTransport(WebTransportEvent::SessionClosed {
                    stream_id,
                    reason,
                    headers,
                })
            }
            ExtendedConnectType::ConnectUdp => {
                Http3ClientEvent::ConnectUdp(ConnectUdpEvent::SessionClosed {
                    stream_id,
                    reason,
                    headers,
                })
            }
        };
        self.events.push(event);
    }

    fn extended_connect_new_stream(
        &self,
        stream_info: Http3StreamInfo,
        emit_readable: bool,
    ) -> Res<()> {
        self.events.push(Http3ClientEvent::WebTransport(
            WebTransportEvent::NewStream {
                stream_id: stream_info.stream_id(),
                session_id: stream_info.session_id().ok_or(Error::Internal)?,
            },
        ));
        if emit_readable {
            self.events.push(Http3ClientEvent::DataReadable {
                stream_id: stream_info.stream_id(),
            });
        }
        Ok(())
    }

    fn new_datagram(
        &self,
        session_id: StreamId,
        datagram: Bytes,
        connect_type: ExtendedConnectType,
    ) {
        let event = match connect_type {
            ExtendedConnectType::WebTransport => {
                Http3ClientEvent::WebTransport(WebTransportEvent::Datagram {
                    session_id,
                    datagram,
                })
            }
            ExtendedConnectType::ConnectUdp => {
                Http3ClientEvent::ConnectUdp(ConnectUdpEvent::Datagram {
                    session_id,
                    datagram,
                })
            }
        };
        self.events.push(event);
    }

    fn capsule_space_available(&self, session_id: StreamId) {
        self.datagram_space_available(session_id);
    }
}

impl Http3ClientEvents {
    /// Add a new `RequestCreatable` event
    pub(crate) fn new_requests_creatable(&self, stream_type: StreamType) {
        if stream_type == StreamType::BiDi {
            self.events.push(Http3ClientEvent::RequestsCreatable);
        }
    }

    /// Emit the outgoing QUIC datagram queue's resume event, forwarding
    /// `neqo-transport`'s [`ConnectionEvent::OutgoingDatagramSpaceAvailable`].
    ///
    /// [`ConnectionEvent::OutgoingDatagramSpaceAvailable`]: neqo_transport::ConnectionEvent::OutgoingDatagramSpaceAvailable
    pub(crate) fn datagram_space_available(&self, session_id: StreamId) {
        self.events
            .push_unique(Http3ClientEvent::OutgoingDatagramSpaceAvailable { session_id });
    }

    /// Add a new `AuthenticationNeeded` event
    pub(crate) fn authentication_needed(&self) {
        self.events.push(Http3ClientEvent::AuthenticationNeeded);
    }

    /// Add a new `AuthenticationNeeded` event
    pub(crate) fn ech_fallback_authentication_needed(&self, public_name: String) {
        self.events
            .push(Http3ClientEvent::EchFallbackAuthenticationNeeded { public_name });
    }

    /// Add a new resumption token event.
    pub(crate) fn resumption_token(&self, token: ResumptionToken) {
        self.events.push(Http3ClientEvent::ResumptionToken(token));
    }

    /// Add a new `ZeroRttRejected` event.
    pub(crate) fn zero_rtt_rejected(&self) {
        self.events.push(Http3ClientEvent::ZeroRttRejected);
    }

    /// Add a new `GoawayReceived` event.
    pub(crate) fn goaway_received(&self) {
        self.events
            .remove_matching(|evt| matches!(evt, Http3ClientEvent::RequestsCreatable));
        self.events.push(Http3ClientEvent::GoawayReceived);
    }

    /// Append an event, allowing duplicates.
    pub(crate) fn push(&self, event: Http3ClientEvent) {
        self.events.push(event);
    }

    /// Add a new `StateChange` event.
    pub(crate) fn connection_state_change(&self, state: Http3State) {
        match state {
            // If closing, existing events no longer relevant.
            // The session ticket in a queued resumption token cannot be retrieved again.
            Http3State::Closing { .. } | Http3State::Closed(_) => self
                .events
                .remove_matching(|evt| !matches!(evt, Http3ClientEvent::ResumptionToken(_))),
            Http3State::Connected => {
                self.events.remove_matching(|evt| {
                    matches!(evt, Http3ClientEvent::StateChange(Http3State::ZeroRtt))
                });
            }
            _ => (),
        }
        self.events.push(Http3ClientEvent::StateChange(state));
    }

    /// Remove events for a stream that is being torn down.
    ///
    /// When `keep_header_ready` is set, events carrying headers are preserved.
    /// This is used when receiving a `RESET_STREAM[_AT]`, because a reliable reset might want
    /// to preserve the headers and the only place those headers exist is in these events.
    ///
    /// Data-bearing events are dropped. If the reliable data includes DATA frames, the
    /// transport will delay delivery of the reset event, so the only case where we get
    /// a reset is where existing data is not intended to be reliably delivered.
    fn remove_recv_stream_events(&self, stream_id: StreamId, keep_header_ready: bool) {
        self.events.remove_matching(|evt| match evt {
            Http3ClientEvent::HeaderReady { stream_id: x, .. } if *x == stream_id => {
                !keep_header_ready
            }
            Http3ClientEvent::DataReadable { stream_id: x }
            | Http3ClientEvent::Reset { stream_id: x, .. }
                if *x == stream_id =>
            {
                true
            }
            _ => false,
        });
    }

    fn remove_send_stream_events(&self, stream_id: StreamId) {
        self.events.remove_matching(|evt| {
            matches!(evt,
                Http3ClientEvent::DataWritable { stream_id: x }
                | Http3ClientEvent::StopSending { stream_id: x, .. } if *x == stream_id)
        });
    }

    pub fn negotiation_done(&self, feature_type: HSettingType, succeeded: bool) {
        match feature_type {
            HSettingType::EnableWebTransport => {
                self.events.push(Http3ClientEvent::WebTransport(
                    WebTransportEvent::Negotiated(succeeded),
                ));
            }
            HSettingType::EnableConnect => {
                self.events
                    .push(Http3ClientEvent::ConnectUdp(ConnectUdpEvent::Negotiated(
                        succeeded,
                    )));
            }
            _ => trace!("HSetting {feature_type:?} {succeeded} not handled"),
        }
    }
}

impl EventProvider for Http3ClientEvents {
    type Event = Http3ClientEvent;

    /// Check if there is any event present.
    fn has_events(&self) -> bool {
        !self.events.is_empty()
    }

    /// Take the first event.
    fn next_event(&mut self) -> Option<Self::Event> {
        self.events.next_event()
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::event::Provider as _;

    use super::{Http3ClientEvent, Http3ClientEvents};

    #[test]
    fn has_events() {
        let events = Http3ClientEvents::default();
        assert!(!events.has_events());
        events.push(Http3ClientEvent::GoawayReceived);
        assert!(events.has_events());
    }
}
