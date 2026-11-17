// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Functions that handle capturing QLOG traces.

use std::{
    ops::Deref as _,
    time::{Duration, Instant},
};

use neqo_common::{Decoder, Ecn, hex::Hex, qlog::Qlog, to_u64};
use qlog::events::{
    ApplicationError, EventData, RawInfo, TupleEndpointInfo,
    quic::{
        AckRange, CongestionStateUpdated, CongestionStateUpdatedTrigger, ConnectionClosed,
        ConnectionClosedTrigger, ConnectionStarted, ConnectionState, ConnectionStateUpdated,
        ErrorSpace, MtuUpdated, PacketDropped, PacketDroppedTrigger, PacketHeader, PacketLost,
        PacketLostTrigger, PacketNumberSpace as QlogPacketNumberSpace, PacketReceived, PacketSent,
        PacketType, PacketsAcked, ParametersSet, PreferredAddress, QuicFrame,
        QuicVersionInformation, RecoveryMetricsUpdated, RecoveryParametersSet, StreamType,
        TimerEventType, TimerType, TimerUpdated, TransportInitiator,
    },
};
use tracing::info;

use crate::{
    CloseReason,
    cc::{CWND_INITIAL_PKTS, CongestionControl, Cubic, PERSISTENT_CONG_THRESH},
    connection::State,
    frame::{CloseError, Frame},
    packet::{self, metadata::Direction},
    path::PathRef,
    recovery::sent,
    rtt::{DEFAULT_INITIAL_RTT, GRANULARITY},
    stream_id::StreamType as NeqoStreamType,
    tparams::{
        TransportParameterId::{
            self, AckDelayExponent, ActiveConnectionIdLimit, DisableMigration, InitialMaxData,
            InitialMaxStreamDataBidiLocal, InitialMaxStreamDataBidiRemote, InitialMaxStreamDataUni,
            InitialMaxStreamsBidi, InitialMaxStreamsUni, MaxAckDelay, MaxUdpPayloadSize,
            OriginalDestinationConnectionId, StatelessResetToken,
        },
        TransportParametersHandler,
    },
    tracking::PacketNumberSpace,
    version::{self, Version},
};

/// Allocation-performing hex conversion for qlog only.
fn to_hex<T: AsRef<[u8]>>(v: T) -> String {
    Hex::new(v).to_string()
}

pub fn connection_tparams_set(qlog: &mut Qlog, tph: &TransportParametersHandler, now: Instant) {
    qlog.add_event_at(
        || {
            let remote = tph.remote();
            let ev_data = EventData::QuicParametersSet(Box::new(ParametersSet {
                initiator: Some(TransportInitiator::Remote),
                original_destination_connection_id: remote
                    .get_bytes(OriginalDestinationConnectionId)
                    .map(to_hex),
                stateless_reset_token: remote.get_bytes(StatelessResetToken).map(to_hex),
                disable_active_migration: remote.get_empty(DisableMigration).then_some(true),
                max_idle_timeout: Some(remote.get_integer(TransportParameterId::IdleTimeout)),
                max_udp_payload_size: Some(remote.get_integer(MaxUdpPayloadSize)),
                ack_delay_exponent: Some(remote.get_integer(AckDelayExponent)),
                max_ack_delay: Some(remote.get_integer(MaxAckDelay)),
                active_connection_id_limit: Some(remote.get_integer(ActiveConnectionIdLimit)),
                initial_max_data: Some(remote.get_integer(InitialMaxData)),
                initial_max_stream_data_bidi_local: Some(
                    remote.get_integer(InitialMaxStreamDataBidiLocal),
                ),
                initial_max_stream_data_bidi_remote: Some(
                    remote.get_integer(InitialMaxStreamDataBidiRemote),
                ),
                initial_max_stream_data_uni: Some(remote.get_integer(InitialMaxStreamDataUni)),
                initial_max_streams_bidi: Some(remote.get_integer(InitialMaxStreamsBidi)),
                initial_max_streams_uni: Some(remote.get_integer(InitialMaxStreamsUni)),
                preferred_address: remote.get_preferred_address().and_then(|(paddr, cid)| {
                    Some(PreferredAddress {
                        ip_v4: paddr.ipv4()?.ip().to_string(),
                        ip_v6: paddr.ipv6()?.ip().to_string(),
                        port_v4: paddr.ipv4()?.port(),
                        port_v6: paddr.ipv6()?.port(),
                        connection_id: cid.connection_id().to_string(),
                        stateless_reset_token: to_hex(cid.reset_token()),
                    })
                }),
                ..Default::default()
            }));

            Some(ev_data)
        },
        now,
    );
}

fn addr_info(addr: std::net::SocketAddr, cid: Option<&crate::ConnectionId>) -> TupleEndpointInfo {
    use std::net::IpAddr;
    let port = addr.port();
    let (ip_v4, ip_v6, port_v4, port_v6) = match addr.ip() {
        IpAddr::V4(v4) => (Some(v4.to_string()), None, Some(port), None),
        IpAddr::V6(v6) => (None, Some(v6.to_string()), None, Some(port)),
    };
    TupleEndpointInfo {
        ip_v4,
        ip_v6,
        port_v4,
        port_v6,
        connection_ids: cid.map(|c| vec![c.to_string()]),
    }
}

pub fn connection_started(qlog: &mut Qlog, path: &PathRef, now: Instant) {
    qlog.add_event_at(
        || {
            let p = path.deref().borrow();
            let ev_data = EventData::QuicConnectionStarted(ConnectionStarted {
                local: addr_info(p.local_address(), p.local_cid()),
                remote: addr_info(p.remote_address(), p.remote_cid()),
            });
            Some(ev_data)
        },
        now,
    );
}

pub fn connection_state_updated(
    qlog: &mut Qlog,
    old_state: &State,
    new_state: &State,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicConnectionStateUpdated(
                ConnectionStateUpdated {
                    old: Some(old_state.into()),
                    new: new_state.into(),
                },
            ))
        },
        now,
    );
}

pub fn client_version_information_initiated(
    qlog: &mut Qlog,
    version_config: &version::Config,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicVersionInformation(QuicVersionInformation {
                client_versions: Some(
                    version_config
                        .all()
                        .iter()
                        .map(|v| format!("{:02x}", v.wire_version()))
                        .collect(),
                ),
                chosen_version: Some(format!("{:02x}", version_config.initial().wire_version())),
                ..Default::default()
            }))
        },
        now,
    );
}

pub fn client_version_information_negotiated(
    qlog: &mut Qlog,
    client: &[Version],
    server: &[version::Wire],
    chosen: Version,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicVersionInformation(QuicVersionInformation {
                client_versions: Some(
                    client
                        .iter()
                        .map(|v| format!("{:02x}", v.wire_version()))
                        .collect(),
                ),
                server_versions: Some(server.iter().map(|v| format!("{v:02x}")).collect()),
                chosen_version: Some(format!("{:02x}", chosen.wire_version())),
            }))
        },
        now,
    );
}

pub fn server_version_information_failed(
    qlog: &mut Qlog,
    server: &[Version],
    client: version::Wire,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicVersionInformation(QuicVersionInformation {
                client_versions: Some(vec![format!("{client:02x}")]),
                server_versions: Some(
                    server
                        .iter()
                        .map(|v| format!("{:02x}", v.wire_version()))
                        .collect(),
                ),
                ..Default::default()
            }))
        },
        now,
    );
}

pub fn packet_io(qlog: &mut Qlog, meta: packet::MetaData, now: Instant) {
    qlog.add_event_at(
        || {
            let mut d = Decoder::from(meta.payload());
            let raw = RawInfo {
                length: Some(to_u64(meta.length())),
                payload_length: None,
                data: None,
            };

            let mut frames = Vec::new();
            while d.remaining() > 0 {
                if let Ok(f) = Frame::decode(&mut d) {
                    frames.push(QuicFrame::from(f));
                } else {
                    info!("qlog: invalid frame");
                    break;
                }
            }

            match meta.direction() {
                Direction::Tx => Some(EventData::QuicPacketSent(PacketSent {
                    header: meta.into(),
                    frames: Some(frames),
                    raw: Some(raw),
                    ..Default::default()
                })),
                Direction::Rx => Some(EventData::QuicPacketReceived(PacketReceived {
                    header: meta.into(),
                    frames: Some(frames),
                    raw: Some(raw),
                    ..Default::default()
                })),
            }
        },
        now,
    );
}
pub fn packet_dropped(qlog: &mut Qlog, decrypt_err: &packet::DecryptionError, now: Instant) {
    qlog.add_event_at(
        || {
            let header =
                PacketHeader::with_type(decrypt_err.packet_type().into(), None, None, None, None);
            let raw = RawInfo {
                length: Some(to_u64(decrypt_err.len())),
                ..Default::default()
            };

            let ev_data = EventData::QuicPacketDropped(PacketDropped {
                header: Some(header),
                raw: Some(raw),
                details: Some(decrypt_err.error.to_string()),
                trigger: Some(PacketDroppedTrigger::DecryptionFailure),
                ..Default::default()
            });

            Some(ev_data)
        },
        now,
    );
}

pub fn packets_lost(qlog: &mut Qlog, pkts: &[sent::Packet], now: Instant) {
    qlog.add_event_with_stream(|stream| {
        for pkt in pkts {
            let header =
                PacketHeader::with_type(pkt.packet_type().into(), Some(pkt.pn()), None, None, None);

            let trigger = pkt
                .loss_info()
                .map(|info| PacketLostTrigger::from(info.trigger))
                .or_else(|| pkt.pto_fired().then_some(PacketLostTrigger::PtoExpired));

            let ev_data = EventData::QuicPacketLost(PacketLost {
                header: Some(header),
                trigger,
                ..Default::default()
            });

            stream.add_event_data_with_instant(ev_data, now)?;
        }
        Ok(())
    });
}

pub fn recovery_parameters_set(
    qlog: &mut Qlog,
    plpmtu: usize,
    cc: CongestionControl,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            let loss_reduction_factor = match cc {
                CongestionControl::NewReno => 0.5,
                CongestionControl::Cubic => {
                    f32::from(u8::try_from(Cubic::BETA_USIZE_DIVIDEND).expect("fits"))
                        / f32::from(u8::try_from(Cubic::BETA_USIZE_DIVISOR).expect("fits"))
                }
            };
            Some(EventData::QuicRecoveryParametersSet(
                RecoveryParametersSet {
                    reordering_threshold: Some(
                        u16::try_from(crate::recovery::PACKET_THRESHOLD).expect("fits"),
                    ),
                    time_threshold: Some(9.0 / 8.0),
                    timer_granularity: Some(u16::try_from(GRANULARITY.as_millis()).expect("fits")),
                    initial_rtt: Some(DEFAULT_INITIAL_RTT.as_secs_f32() * 1000.0),
                    max_datagram_size: Some(u32::try_from(plpmtu).expect("MTU fits in u32")),
                    initial_congestion_window: Some(to_u64(CWND_INITIAL_PKTS * plpmtu)),
                    minimum_congestion_window: Some(
                        u32::try_from(2 * plpmtu).expect("MTU fits in u32"),
                    ),
                    loss_reduction_factor: Some(loss_reduction_factor),
                    persistent_congestion_threshold: Some(
                        u16::try_from(PERSISTENT_CONG_THRESH).expect("fits"),
                    ),
                },
            ))
        },
        now,
    );
}

pub fn connection_closed(qlog: &mut Qlog, close_reason: &CloseReason, now: Instant) {
    qlog.add_event_at(
        || Some(EventData::QuicConnectionClosed(close_reason.into())),
        now,
    );
}

pub fn packets_acked(
    qlog: &mut Qlog,
    space: PacketNumberSpace,
    acked_pkts: &[sent::Packet],
    now: Instant,
) {
    if acked_pkts.is_empty() {
        return;
    }
    qlog.add_event_at(
        || {
            let packet_number_space = Some(QlogPacketNumberSpace::from(space));
            let packet_numbers = Some(acked_pkts.iter().map(sent::Packet::pn).collect::<Vec<_>>());
            Some(EventData::QuicPacketsAcked(PacketsAcked {
                packet_number_space,
                packet_numbers,
            }))
        },
        now,
    );
}

pub fn mtu_updated(qlog: &mut Qlog, old_mtu: usize, new_mtu: usize, done: bool, now: Instant) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicMtuUpdated(MtuUpdated {
                old: Some(u32::try_from(old_mtu).expect("MTU fits in u32")),
                new: u32::try_from(new_mtu).expect("MTU fits in u32"),
                done: Some(done),
            }))
        },
        now,
    );
}

#[derive(Clone, Copy)]
#[expect(dead_code, reason = "TODO: Construct all variants.")]
pub enum Metric {
    MinRtt(Duration),
    SmoothedRtt(Duration),
    LatestRtt(Duration),
    RttVariance(Duration),
    PtoCount(usize),
    CongestionWindow(usize),
    BytesInFlight(usize),
    SsThresh(usize),
    PacketsInFlight(u64),
    PacingRate(u64),
}

pub fn metrics_updated<M: IntoIterator<Item = Metric>>(
    qlog: &mut Qlog,
    updated_metrics: M,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            let mut min_rtt: Option<f32> = None;
            let mut smoothed_rtt: Option<f32> = None;
            let mut latest_rtt: Option<f32> = None;
            let mut rtt_variance: Option<f32> = None;
            let mut pto_count: Option<u16> = None;
            let mut congestion_window: Option<u64> = None;
            let mut bytes_in_flight: Option<u64> = None;
            let mut ssthresh: Option<u64> = None;
            let mut packets_in_flight: Option<u64> = None;
            let mut pacing_rate: Option<u64> = None;

            for metric in updated_metrics {
                match metric {
                    Metric::MinRtt(v) => min_rtt = Some(v.as_secs_f32() * 1000.0),
                    Metric::SmoothedRtt(v) => smoothed_rtt = Some(v.as_secs_f32() * 1000.0),
                    Metric::LatestRtt(v) => latest_rtt = Some(v.as_secs_f32() * 1000.0),
                    Metric::RttVariance(v) => rtt_variance = Some(v.as_secs_f32() * 1000.0),
                    Metric::PtoCount(v) => {
                        pto_count = Some(u16::try_from(v).expect("fits in u16"));
                    }
                    Metric::CongestionWindow(v) => {
                        congestion_window = Some(to_u64(v));
                    }
                    Metric::BytesInFlight(v) => {
                        bytes_in_flight = Some(to_u64(v));
                    }
                    Metric::SsThresh(v) => {
                        ssthresh = Some(to_u64(v));
                    }
                    Metric::PacketsInFlight(v) => packets_in_flight = Some(v),
                    Metric::PacingRate(v) => pacing_rate = Some(v),
                }
            }

            debug_assert!(
                min_rtt.is_some()
                    || smoothed_rtt.is_some()
                    || latest_rtt.is_some()
                    || rtt_variance.is_some()
                    || pto_count.is_some()
                    || congestion_window.is_some()
                    || bytes_in_flight.is_some()
                    || ssthresh.is_some()
                    || packets_in_flight.is_some()
                    || pacing_rate.is_some(),
                "metrics_updated called with no metrics"
            );

            let ev_data = EventData::QuicMetricsUpdated(RecoveryMetricsUpdated {
                min_rtt,
                smoothed_rtt,
                latest_rtt,
                rtt_variance,
                pto_count,
                congestion_window,
                bytes_in_flight,
                ssthresh,
                packets_in_flight,
                pacing_rate,
                ..Default::default()
            });

            Some(ev_data)
        },
        now,
    );
}

/// Trigger for a `recovery:congestion_state_updated` qlog event.
#[derive(Clone, Copy)]
pub enum CongestionStateTrigger {
    /// The congestion state change was triggered by an ECN mark.
    Ecn,
    /// The congestion state change was triggered by persistent congestion.
    PersistentCongestion,
}

impl From<CongestionStateTrigger> for CongestionStateUpdatedTrigger {
    fn from(value: CongestionStateTrigger) -> Self {
        match value {
            CongestionStateTrigger::Ecn => Self::Ecn,
            CongestionStateTrigger::PersistentCongestion => Self::PersistentCongestion,
        }
    }
}

pub fn congestion_state_updated(
    qlog: &mut Qlog,
    old_state: Option<&'static str>,
    new_state: &'static str,
    trigger: Option<CongestionStateTrigger>,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicCongestionStateUpdated(
                CongestionStateUpdated {
                    old: old_state.map(ToOwned::to_owned),
                    new: new_state.to_owned(),
                    trigger: trigger.map(Into::into),
                },
            ))
        },
        now,
    );
}

/// The type of loss recovery timer that fired or was updated.
#[derive(Clone, Copy, Debug)]
pub enum LossTimerType {
    /// The reordering/loss-detection timer (ACK-based).
    Ack,
    /// The Probe Timeout timer.
    Pto,
}

/// Emit a `timer_updated` Set event.
///
/// Only the PTO timer has explicit set/cancel lifecycle in neqo. The
/// loss-detection (Ack) timer is derived lazily from packet state on every
/// call to [`crate::recovery::Loss::next_timeout`] and has no single arm or
/// cancel point to instrument.
pub fn loss_timer_set(qlog: &mut Qlog, pn_space: PacketNumberSpace, pto: Duration, now: Instant) {
    loss_timer_updated(
        qlog,
        TimerEventType::Set,
        Some(TimerType::Pto),
        Some(QlogPacketNumberSpace::from(pn_space)),
        Some(pto.as_secs_f32() * 1000.0),
        now,
    );
}

pub fn loss_timer_expired(qlog: &mut Qlog, timer_type: LossTimerType, now: Instant) {
    loss_timer_updated(
        qlog,
        TimerEventType::Expired,
        Some(timer_type.into()),
        None,
        None,
        now,
    );
}

/// Emit a `timer_updated` Cancelled event.
///
/// See [`loss_timer_set`] for why only `TimerType::Pto` is used here.
pub fn loss_timer_cancelled(qlog: &mut Qlog, now: Instant) {
    loss_timer_updated(
        qlog,
        TimerEventType::Cancelled,
        Some(TimerType::Pto),
        None,
        None,
        now,
    );
}

fn loss_timer_updated(
    qlog: &mut Qlog,
    event_type: TimerEventType,
    timer_type: Option<TimerType>,
    packet_number_space: Option<QlogPacketNumberSpace>,
    delta: Option<f32>,
    now: Instant,
) {
    qlog.add_event_at(
        || {
            Some(EventData::QuicTimerUpdated(TimerUpdated {
                timer_type,
                timer_id: None,
                packet_number_space,
                event_type,
                delta,
            }))
        },
        now,
    );
}

#[expect(clippy::too_many_lines, reason = "Yeah, but it's a nice match.")]
#[expect(
    clippy::cast_precision_loss,
    clippy::cast_possible_truncation,
    reason = "We need to truncate here."
)]
impl From<Frame<'_>> for QuicFrame {
    fn from(frame: Frame) -> Self {
        match frame {
            Frame::Padding(len) => Self::Padding {
                raw: Some(Box::new(RawInfo {
                    length: None,
                    payload_length: Some(u64::from(len)),
                    data: None,
                })),
            },
            Frame::Ping => Self::Ping { raw: None },
            Frame::Ack {
                largest_acknowledged,
                ack_delay,
                first_ack_range,
                ack_ranges,
                ecn_count,
            } => {
                let ranges =
                    Frame::decode_ack_frame(largest_acknowledged, first_ack_range, &ack_ranges)
                        .ok();
                let acked_ranges = ranges.map(|all| {
                    all.into_iter()
                        .map(|r| {
                            let (start, end) = r.into_inner();
                            AckRange { start, end }
                        })
                        .collect::<Vec<_>>()
                });
                Self::Ack {
                    ack_delay: Some(ack_delay as f32 / 1000.0),
                    acked_ranges,
                    ect1: ecn_count.map(|c| c[Ecn::Ect1]),
                    ect0: ecn_count.map(|c| c[Ecn::Ect0]),
                    ce: ecn_count.map(|c| c[Ecn::Ce]),
                    raw: None,
                }
            }
            // Note that qlog doesn't currently support `RESET_STREAM_AT`,
            // so map it into `RESET_STREAM` for now.
            Frame::ResetStream {
                stream_id,
                application_error_code,
                final_size,
            }
            | Frame::ResetStreamAt {
                stream_id,
                application_error_code,
                final_size,
                ..
            } => Self::ResetStream {
                stream_id: stream_id.as_u64(),
                error: ApplicationError::Unknown,
                error_code: Some(application_error_code),
                final_size,
                raw: None,
            },
            Frame::StopSending {
                stream_id,
                application_error_code,
            } => Self::StopSending {
                stream_id: stream_id.as_u64(),
                error: ApplicationError::Unknown,
                error_code: Some(application_error_code),
                raw: None,
            },
            Frame::Crypto { offset, data, .. } => Self::Crypto {
                offset,
                raw: Some(Box::new(RawInfo {
                    length: None,
                    payload_length: Some(to_u64(data.len())),
                    data: None,
                })),
            },
            Frame::NewToken { token } => Self::NewToken {
                token: qlog::Token {
                    ty: None,
                    details: None,
                    raw: Some(RawInfo {
                        length: Some(to_u64(token.len())),
                        payload_length: None,
                        data: None,
                    }),
                },
                raw: None,
            },
            Frame::Stream {
                fin,
                stream_id,
                offset,
                data,
                ..
            } => Self::Stream {
                stream_id: stream_id.as_u64(),
                offset: Some(offset),
                fin: Some(fin),
                raw: Some(Box::new(RawInfo {
                    length: None,
                    payload_length: Some(to_u64(data.len())),
                    data: None,
                })),
            },
            Frame::MaxData { maximum_data } => Self::MaxData {
                maximum: maximum_data,
                raw: None,
            },
            Frame::MaxStreamData {
                stream_id,
                maximum_stream_data,
            } => Self::MaxStreamData {
                stream_id: stream_id.as_u64(),
                maximum: maximum_stream_data,
                raw: None,
            },
            Frame::MaxStreams {
                stream_type,
                maximum_streams,
            } => Self::MaxStreams {
                stream_type: stream_type.into(),
                maximum: maximum_streams,
                raw: None,
            },
            Frame::DataBlocked { data_limit } => Self::DataBlocked {
                limit: data_limit,
                raw: None,
            },
            Frame::StreamDataBlocked {
                stream_id,
                stream_data_limit,
            } => Self::StreamDataBlocked {
                stream_id: stream_id.as_u64(),
                limit: stream_data_limit,
                raw: None,
            },
            Frame::StreamsBlocked {
                stream_type,
                stream_limit,
            } => Self::StreamsBlocked {
                stream_type: stream_type.into(),
                limit: stream_limit,
                raw: None,
            },
            Frame::NewConnectionId {
                sequence_number,
                retire_prior,
                connection_id,
                stateless_reset_token,
            } => Self::NewConnectionId {
                sequence_number,
                retire_prior_to: retire_prior,
                connection_id_length: Some(connection_id.len() as u8),
                connection_id: to_hex(connection_id),
                stateless_reset_token: Some(to_hex(stateless_reset_token)),
                raw: None,
            },
            Frame::RetireConnectionId { sequence_number } => Self::RetireConnectionId {
                sequence_number,
                raw: None,
            },
            Frame::PathChallenge { data } => Self::PathChallenge {
                data: Some(to_hex(data)),
                raw: None,
            },
            Frame::PathResponse { data } => Self::PathResponse {
                data: Some(to_hex(data)),
                raw: None,
            },
            Frame::ConnectionClose {
                error_code,
                frame_type,
                reason_phrase,
            } => Self::ConnectionClose {
                error_space: Some((&error_code).into()),
                error: None,
                error_code: Some(error_code.code()),
                reason: Some(reason_phrase),
                reason_bytes: None,
                trigger_frame_type: Some(frame_type),
            },
            Frame::HandshakeDone => Self::HandshakeDone { raw: None },
            Frame::AckFrequency { .. } => Self::Unknown {
                frame_type_bytes: Some(frame.get_type().into()),
                raw: None,
            },
            Frame::Datagram { data, .. } => Self::Datagram {
                raw: Some(Box::new(RawInfo {
                    length: None,
                    payload_length: Some(to_u64(data.len())),
                    data: None,
                })),
            },
        }
    }
}

impl From<&State> for ConnectionState {
    fn from(state: &State) -> Self {
        match state {
            State::Init | State::WaitInitial => Self::Attempted,
            State::WaitVersion | State::Handshaking => Self::HandshakeStarted,
            State::Connected => Self::HandshakeCompleted,
            State::Confirmed => Self::HandshakeConfirmed,
            State::Closing { .. } => Self::Closing,
            State::Draining { .. } => Self::Draining,
            State::Closed { .. } => Self::Closed,
        }
    }
}

impl From<PacketNumberSpace> for QlogPacketNumberSpace {
    fn from(space: PacketNumberSpace) -> Self {
        match space {
            PacketNumberSpace::Initial => Self::Initial,
            PacketNumberSpace::Handshake => Self::Handshake,
            PacketNumberSpace::ApplicationData => Self::ApplicationData,
        }
    }
}

impl From<NeqoStreamType> for StreamType {
    fn from(stream_type: NeqoStreamType) -> Self {
        match stream_type {
            NeqoStreamType::BiDi => Self::Bidirectional,
            NeqoStreamType::UniDi => Self::Unidirectional,
        }
    }
}

impl From<&CloseError> for ErrorSpace {
    fn from(error: &CloseError) -> Self {
        match error {
            CloseError::Transport(_) => Self::Transport,
            CloseError::Application(_) => Self::Application,
        }
    }
}

impl From<&CloseReason> for ConnectionClosed {
    fn from(close_reason: &CloseReason) -> Self {
        let (error_code, trigger, initiator) = match close_reason {
            CloseReason::Transport(e) if *e == crate::Error::IdleTimeout => (
                None,
                Some(ConnectionClosedTrigger::IdleTimeout),
                Some(TransportInitiator::Local),
            ),
            CloseReason::Transport(e) if *e == crate::Error::StatelessReset => (
                None,
                Some(ConnectionClosedTrigger::StatelessReset),
                Some(TransportInitiator::Remote),
            ),
            CloseReason::Transport(e) if *e == crate::Error::VersionNegotiation => (
                None,
                Some(ConnectionClosedTrigger::VersionMismatch),
                Some(TransportInitiator::Remote),
            ),
            CloseReason::Transport(e) if *e == crate::Error::None => (
                None,
                Some(ConnectionClosedTrigger::Clean),
                Some(TransportInitiator::Local),
            ),
            CloseReason::Transport(crate::Error::Peer(code)) => (
                Some(*code),
                Some(ConnectionClosedTrigger::Error),
                Some(TransportInitiator::Remote),
            ),
            CloseReason::Application(code) => (
                Some(*code),
                Some(ConnectionClosedTrigger::Application),
                Some(TransportInitiator::Local),
            ),
            CloseReason::Transport(crate::Error::PeerApplication(code)) => (
                Some(*code),
                Some(ConnectionClosedTrigger::Application),
                Some(TransportInitiator::Remote),
            ),
            CloseReason::Transport(e) => (
                Some(e.code()),
                Some(ConnectionClosedTrigger::Error),
                Some(TransportInitiator::Local),
            ),
        };
        Self {
            initiator,
            connection_error: None,
            application_error: None,
            error_code,
            internal_code: None,
            reason: None,
            trigger,
        }
    }
}

impl From<LossTimerType> for TimerType {
    fn from(value: LossTimerType) -> Self {
        match value {
            LossTimerType::Ack => Self::Ack,
            LossTimerType::Pto => Self::Pto,
        }
    }
}

impl From<sent::LossTrigger> for PacketLostTrigger {
    fn from(value: sent::LossTrigger) -> Self {
        match value {
            sent::LossTrigger::TimeThreshold => Self::TimeThreshold,
            sent::LossTrigger::ReorderingThreshold => Self::ReorderingThreshold,
        }
    }
}

impl From<packet::Type> for PacketType {
    fn from(value: packet::Type) -> Self {
        match value {
            packet::Type::Initial => Self::Initial,
            packet::Type::Handshake => Self::Handshake,
            packet::Type::ZeroRtt => Self::ZeroRtt,
            packet::Type::Short => Self::OneRtt,
            packet::Type::Retry => Self::Retry,
            packet::Type::VersionNegotiation => Self::VersionNegotiation,
            packet::Type::OtherVersion => Self::Unknown,
        }
    }
}

#[cfg(test)]
mod tests {
    use qlog::events::{
        RawInfo,
        quic::{ConnectionClosed, ConnectionClosedTrigger, QuicFrame, TransportInitiator},
    };
    use test_fixture::new_neqo_qlog;

    use super::{
        Metric, client_version_information_negotiated, metrics_updated,
        server_version_information_failed,
    };
    use crate::{CloseReason, Error, frame::Frame, version::Version};

    #[test]
    fn padding_length() {
        assert_eq!(
            QuicFrame::from(Frame::Padding(5)),
            QuicFrame::Padding {
                raw: Some(Box::new(RawInfo {
                    length: None,
                    payload_length: Some(5),
                    data: None,
                })),
            }
        );
    }

    #[test]
    fn connection_closed_triggers() {
        for (reason, error_code, trigger, initiator) in [
            (
                CloseReason::Transport(Error::StatelessReset),
                None,
                ConnectionClosedTrigger::StatelessReset,
                TransportInitiator::Remote,
            ),
            (
                CloseReason::Transport(Error::VersionNegotiation),
                None,
                ConnectionClosedTrigger::VersionMismatch,
                TransportInitiator::Remote,
            ),
            (
                CloseReason::Transport(Error::None),
                None,
                ConnectionClosedTrigger::Clean,
                TransportInitiator::Local,
            ),
            (
                CloseReason::Transport(Error::Peer(7)),
                Some(7),
                ConnectionClosedTrigger::Error,
                TransportInitiator::Remote,
            ),
        ] {
            let closed = ConnectionClosed::from(&reason);
            assert_eq!(closed.error_code, error_code);
            assert_eq!(closed.trigger, Some(trigger));
            assert_eq!(closed.initiator, Some(initiator));
        }
    }

    #[test]
    fn version_information() {
        let (mut qlog, contents) = new_neqo_qlog();
        let now = test_fixture::now();
        client_version_information_negotiated(
            &mut qlog,
            &[Version::Version1],
            &[Version::Version2.wire_version()],
            Version::Version2,
            now,
        );
        server_version_information_failed(&mut qlog, &[Version::Version2], 0x1a2a_3a4a, now);
        drop(qlog);
        let output = contents.to_string();
        assert!(output.contains("\"6b3343cf\""), "missing server version");
        assert!(output.contains("\"1a2a3a4a\""), "missing client version");
    }

    /// Verify that `metrics_updated` records all metric variants, including
    /// `SsThresh`, when qlog is enabled.
    #[test]
    fn metrics_updated_all_variants() {
        let (mut qlog, contents) = new_neqo_qlog();
        let now = test_fixture::now();
        metrics_updated(
            &mut qlog,
            [Metric::CongestionWindow(10_000), Metric::SsThresh(5_000)],
            now,
        );
        drop(qlog);
        let output = contents.to_string();
        assert!(
            output.contains("congestion_window"),
            "missing congestion_window"
        );
        assert!(output.contains("ssthresh"), "missing ssthresh");
    }
}
