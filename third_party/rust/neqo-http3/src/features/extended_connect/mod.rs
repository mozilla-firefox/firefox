// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

pub(crate) mod connect_udp_session;
pub mod send_group;
pub mod session;
pub mod stats;
pub(crate) mod webtransport_session;
pub(crate) mod webtransport_streams;

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests;

use std::{cell::RefCell, fmt::Debug, mem, rc::Rc};

use neqo_common::{Bytes, Header, Role};
use neqo_transport::StreamId;
use tracing::debug;

use crate::{
    Http3StreamInfo, HttpRecvStreamEvents, RecvStreamEvents, Res, SendStreamEvents,
    client_events::Http3ClientEvents,
    features::{
        NegotiationState,
        extended_connect::session::{CloseReason, Protocol},
    },
    settings::{HSettingType, HSettings},
};

pub(crate) trait ExtendedConnectEvents: Debug {
    fn session_start(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        status: u16,
        headers: Vec<Header>,
    );
    fn session_end(
        &self,
        connect_type: ExtendedConnectType,
        stream_id: StreamId,
        reason: CloseReason,
        headers: Option<Vec<Header>>,
    );
    fn extended_connect_new_stream(
        &self,
        stream_info: Http3StreamInfo,
        emit_readable: bool,
    ) -> Res<()>;
    fn new_datagram(
        &self,
        session_id: StreamId,
        datagram: Bytes,
        connect_type: ExtendedConnectType,
    );
    /// The control stream of `session_id` has flow-control space again after a
    /// datagram capsule was refused with `FlowControlLimit`, so the sender can
    /// resume.
    ///
    /// This is the HTTP DATAGRAM Capsule path only. On the QUIC datagram path the
    /// resume event instead originates in `neqo-transport` and is forwarded to
    /// the HTTP/3 event by the connection.
    fn capsule_space_available(&self, session_id: StreamId);
}

#[derive(Debug, PartialEq, Copy, Clone, Eq, strum::Display)]
pub(crate) enum ExtendedConnectType {
    #[strum(to_string = "webtransport")]
    WebTransport,
    #[strum(to_string = "connect-udp")]
    ConnectUdp,
}

impl ExtendedConnectType {
    pub(crate) fn new_protocol(self, session_id: StreamId, role: Role) -> Box<dyn Protocol> {
        match self {
            Self::WebTransport => Box::new(webtransport_session::Session::new(session_id, role)),
            Self::ConnectUdp => Box::new(connect_udp_session::Session::new(session_id)),
        }
    }
}

impl From<ExtendedConnectType> for HSettingType {
    fn from(from: ExtendedConnectType) -> Self {
        match from {
            ExtendedConnectType::WebTransport => Self::EnableWebTransport,
            ExtendedConnectType::ConnectUdp => Self::EnableConnect,
        }
    }
}

pub(crate) struct TransportPrerequisites {
    datagrams: bool,
    reliable_reset: bool,
}

impl TransportPrerequisites {
    #[must_use]
    pub const fn new(datagrams: bool, reliable_reset: bool) -> Self {
        Self {
            datagrams,
            reliable_reset,
        }
    }
}

#[derive(Debug)]
pub(crate) struct ExtendedConnectFeature {
    feature_negotiation: NegotiationState,
    connect_type: ExtendedConnectType,
    role: Role,
}

impl ExtendedConnectFeature {
    #[must_use]
    pub fn new(connect_type: ExtendedConnectType, role: Role, enable: bool) -> Self {
        Self {
            feature_negotiation: NegotiationState::new(enable, HSettingType::from(connect_type)),
            connect_type,
            role,
        }
    }

    pub fn set_listener(&mut self, new_listener: Http3ClientEvents) {
        self.feature_negotiation.set_listener(new_listener);
    }

    /// `transport_prereqs` captures the state of transport-level features that might be needed.
    pub fn handle_settings(
        &mut self,
        settings: &HSettings,
        transport_prereqs: &TransportPrerequisites,
    ) {
        // reset_stream_at is also required (draft Section 4.4), but too few servers support it
        // yet, so we don't gate on it for now (falls back to RESET_STREAM). See #3917.
        let conditions_met = match self.connect_type {
            ExtendedConnectType::WebTransport => {
                transport_prereqs.datagrams
                    && settings.get(HSettingType::EnableH3Datagram) == 1
                    && (self.role == Role::Server
                        || (settings.get(HSettingType::EnableConnect) == 1
                            && settings.get(HSettingType::EnableWebTransport) == 1))
            }
            ExtendedConnectType::ConnectUdp => {
                self.role == Role::Server || settings.get(HSettingType::EnableConnect) == 1
            }
        };
        self.feature_negotiation.negotiate(conditions_met);
        if self.connect_type == ExtendedConnectType::WebTransport
            && self.enabled()
            && !transport_prereqs.reliable_reset
        {
            debug!(
                "WebTransport negotiated without peer reliable reset; stream resets use RESET_STREAM"
            );
        }
    }

    #[must_use]
    pub const fn enabled(&self) -> bool {
        self.feature_negotiation.enabled()
    }
}

#[expect(
    clippy::struct_field_names,
    reason = "wrapper type, providing additional info"
)]
#[derive(Debug, Default)]
struct Headers {
    headers: Vec<Header>,
    interim: bool,
    fin: bool,
}

/// Implementation of [`HttpRecvStreamEvents`]. Registered with the underlying
/// [`RecvMessage`] stream. Listening for [`RecvMessage`] to read
/// incoming headers.
///
/// [`RecvMessage`]: crate::recv_message::RecvMessage
#[derive(Debug, Default)]
struct HeaderListener {
    headers: Option<Headers>,
}

impl HeaderListener {
    fn set_headers(&mut self, headers: Vec<Header>, interim: bool, fin: bool) {
        self.headers = Some(Headers {
            headers,
            interim,
            fin,
        });
    }

    pub fn get_headers(&mut self) -> Option<Headers> {
        mem::take(&mut self.headers)
    }
}

impl RecvStreamEvents for Rc<RefCell<HeaderListener>> {}

impl HttpRecvStreamEvents for Rc<RefCell<HeaderListener>> {
    fn header_ready(
        &self,
        _stream_info: &Http3StreamInfo,
        headers: Vec<Header>,
        interim: bool,
        fin: bool,
    ) {
        if !interim || fin {
            self.borrow_mut().set_headers(headers, interim, fin);
        }
    }
}

impl SendStreamEvents for Rc<RefCell<HeaderListener>> {}
