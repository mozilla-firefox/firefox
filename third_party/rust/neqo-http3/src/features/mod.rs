// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{fmt::Debug, mem};

use tracing::trace;

use crate::{
    client_events::Http3ClientEvents, features::extended_connect::ExtendedConnectType,
    settings::HSettingType,
};

pub mod extended_connect;

/// States:
/// - `Disable` - it is not turned on for this connection.
/// - `Negotiating` - the feature is enabled locally, but settings from the peer have not been
///   received yet.
/// - `Negotiated` - the settings have been received and both sides support the feature.
/// - `NegotiationFailed` - the settings have been received and the peer does not support the
///   feature.
#[derive(Debug)]
pub enum NegotiationState {
    Disabled,
    Negotiating {
        feature_type: HSettingType,
        listener: Option<Http3ClientEvents>,
    },
    Negotiated,
    Failed,
}

impl NegotiationState {
    #[must_use]
    pub const fn new(enable: bool, feature_type: HSettingType) -> Self {
        if enable {
            Self::Negotiating {
                feature_type,
                listener: None,
            }
        } else {
            Self::Disabled
        }
    }

    pub fn set_listener(&mut self, new_listener: Http3ClientEvents) {
        if let Self::Negotiating { listener, .. } = self {
            *listener = Some(new_listener);
        }
    }

    /// Enable the feature; triggered by the receipt of settings from the peer.
    /// `conditions_met` determines whether the feature can be enabled.
    pub fn negotiate(&mut self, conditions_met: bool) {
        let Self::Negotiating {
            feature_type,
            listener,
        } = self
        else {
            return;
        };

        let ft = *feature_type;
        let cb = mem::take(listener);
        trace!("negotiate {ft:?}: {conditions_met}");
        *self = if conditions_met {
            Self::Negotiated
        } else {
            Self::Failed
        };
        if let Some(l) = cb {
            l.negotiation_done(ft, conditions_met);
        }
    }

    #[must_use]
    pub const fn enabled(&self) -> bool {
        matches!(self, &Self::Negotiated)
    }

    #[must_use]
    pub const fn locally_enabled(&self) -> bool {
        !matches!(self, &Self::Disabled)
    }
}

/// The type of an HTTP CONNECT.
#[derive(Debug, PartialEq, Copy, Clone, Eq)]
pub(crate) enum ConnectType {
    /// Classic HTTP CONNECT see
    /// <https://datatracker.ietf.org/doc/html/rfc9114#name-the-connect-method>.
    Classic,
    /// Extended CONNECT see <https://www.rfc-editor.org/rfc/rfc9220>.
    Extended(ExtendedConnectType),
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use neqo_common::Role;

    use crate::{
        features::{
            NegotiationState,
            extended_connect::{
                ExtendedConnectFeature, ExtendedConnectType, TransportPrerequisites,
            },
        },
        settings::{HSetting, HSettingType, HSettings},
    };

    #[test]
    fn negotiation_state_locally_enabled() {
        let disabled = NegotiationState::new(false, HSettingType::EnableWebTransport);
        assert!(!disabled.locally_enabled());

        let negotiating = NegotiationState::new(true, HSettingType::EnableWebTransport);
        assert!(negotiating.locally_enabled());

        assert!(NegotiationState::Negotiated.locally_enabled());
        assert!(NegotiationState::Failed.locally_enabled());
    }

    fn negotiate(
        role: Role,
        feature: ExtendedConnectType,
        settings: &HSettings,
        peer_ok: bool,
    ) -> bool {
        let mut f = ExtendedConnectFeature::new(feature, role, true);
        let prereqs = TransportPrerequisites::new(peer_ok, peer_ok);
        f.handle_settings(settings, &prereqs);
        f.enabled()
    }

    /// A WebTransport client only negotiates when the server advertises everything it needs
    /// (extended CONNECT, HTTP/3 datagrams, and the datagram transport parameter); a server
    /// has no such requirements on the peer.
    #[test]
    fn webtransport_feature_checks() {
        // Everything the server must advertise via SETTINGS for a client to use WebTransport.
        let full = HSettings::new(&[
            HSetting::new(HSettingType::EnableWebTransport, 1),
            HSetting::new(HSettingType::EnableH3Datagram, 1),
            HSetting::new(HSettingType::EnableConnect, 1),
        ]);
        // Missing extended CONNECT.
        let no_connect = HSettings::new(&[
            HSetting::new(HSettingType::EnableWebTransport, 1),
            HSetting::new(HSettingType::EnableH3Datagram, 1),
        ]);
        // Missing HTTP/3 datagrams.
        let no_h3_datagram = HSettings::new(&[
            HSetting::new(HSettingType::EnableWebTransport, 1),
            HSetting::new(HSettingType::EnableConnect, 1),
        ]);
        // Everything but WebTransport.
        let no_wt = HSettings::new(&[
            HSetting::new(HSettingType::EnableH3Datagram, 1),
            HSetting::new(HSettingType::EnableConnect, 1),
        ]);

        // Client: needs all SETTINGS and the peer's transport parameters.
        assert!(negotiate(
            Role::Client,
            ExtendedConnectType::WebTransport,
            &full,
            true
        ));
        assert!(!negotiate(
            Role::Client,
            ExtendedConnectType::WebTransport,
            &no_connect,
            true
        ));
        assert!(!negotiate(
            Role::Client,
            ExtendedConnectType::WebTransport,
            &no_h3_datagram,
            true
        ));
        assert!(!negotiate(
            Role::Client,
            ExtendedConnectType::WebTransport,
            &no_wt,
            true
        ));
        assert!(!negotiate(
            Role::Client,
            ExtendedConnectType::WebTransport,
            &full,
            false
        ));

        // Server: the prerequisites matter, as does the datagram setting.
        assert!(!negotiate(
            Role::Server,
            ExtendedConnectType::WebTransport,
            &HSettings::default(),
            true
        ));
        assert!(negotiate(
            Role::Server,
            ExtendedConnectType::WebTransport,
            &HSettings::new(&[HSetting::new(HSettingType::EnableH3Datagram, 1)]),
            true,
        ));
        assert!(!negotiate(
            Role::Server,
            ExtendedConnectType::WebTransport,
            &full,
            false
        ));
    }

    /// WebTransport negotiates whether or not the peer advertises the `reset_stream_at`
    /// transport parameter (reliable reset). The draft (Section 4.4) requires reliable reset,
    /// but the extension is new and rarely deployed, so neqo tolerates peers without it,
    /// resetting such streams with a plain `RESET_STREAM` rather than refusing WebTransport.
    #[test]
    fn webtransport_ignores_peer_reliable_reset() {
        let full = HSettings::new(&[
            HSetting::new(HSettingType::EnableWebTransport, 1),
            HSetting::new(HSettingType::EnableH3Datagram, 1),
            HSetting::new(HSettingType::EnableConnect, 1),
        ]);
        for reliable_reset in [false, true] {
            let mut f =
                ExtendedConnectFeature::new(ExtendedConnectType::WebTransport, Role::Client, true);
            f.handle_settings(&full, &TransportPrerequisites::new(true, reliable_reset));
            assert!(
                f.enabled(),
                "WebTransport must negotiate regardless of peer reliable reset support"
            );
        }
    }

    /// Confirm that the necessary features for `CONNECT_UDP` are validated.
    #[test]
    fn connect_udp_feature_checks() {
        // The client needs the server setting, but doesn't care about the transport features.
        assert!(negotiate(
            Role::Client,
            ExtendedConnectType::ConnectUdp,
            &HSettings::new(&[HSetting::new(HSettingType::EnableConnect, 1),]),
            true
        ));
        assert!(!negotiate(
            Role::Client,
            ExtendedConnectType::ConnectUdp,
            &HSettings::default(),
            true
        ));
        assert!(negotiate(
            Role::Client,
            ExtendedConnectType::ConnectUdp,
            &HSettings::new(&[HSetting::new(HSettingType::EnableConnect, 1),]),
            false
        ));

        // The server simply doesn't care.
        assert!(negotiate(
            Role::Server,
            ExtendedConnectType::ConnectUdp,
            &HSettings::default(),
            false
        ));
    }
}
