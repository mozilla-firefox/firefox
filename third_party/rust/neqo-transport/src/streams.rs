// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Stream management for a connection.
use std::{
    cell::RefCell,
    cmp::Ordering,
    num::NonZeroUsize,
    rc::Rc,
    time::{Duration, Instant},
};

use neqo_common::{Buffer, Role};
use tracing::{debug, trace};

use crate::{
    AppError, ConnectionEvents, Error, Res,
    fc::{LocalStreamLimits, ReceiverFlowControl, RemoteStreamLimits, SenderFlowControl},
    frame::Frame,
    packet,
    recovery::{self, StreamRecoveryToken},
    recv_stream::{RecvStream, RecvStreams},
    send_stream::{SendStream, SendStreams, TransmissionPriority},
    stats::{FrameStats, Stats},
    stream_id::{MAX_STREAMS_LIMIT, StreamId, StreamType},
    tparams::{
        TransportParameterId::{
            InitialMaxData, InitialMaxStreamDataBidiLocal, InitialMaxStreamDataBidiRemote,
            InitialMaxStreamDataUni, InitialMaxStreamsBidi, InitialMaxStreamsUni, ResetStreamAt,
        },
        TransportParametersHandler,
    },
};

pub type SendOrder = i64;

/// Identifier for a send group, unique within a connection.
///
/// A newtype around `u64` rather than a bare alias, so a raw integer (or a `SendOrder`)
/// cannot be passed where a send-group id is expected.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct SendGroupId(u64);

impl SendGroupId {
    /// Creates a new `SendGroupId`. Note: `0` is reserved as a sentinel
    /// (`NULL_GROUP_ID`) and will be rejected
    /// by [`SendStreams::set_sendgroup`](crate::send_stream::SendStreams::set_sendgroup).
    #[must_use]
    pub const fn new(id: u64) -> Self {
        Self(id)
    }

    /// The underlying integer, e.g. for wire encoding.
    #[must_use]
    pub const fn as_u64(self) -> u64 {
        self.0
    }
}

/// Sentinel for the null sendGroup: ungrouped fair streams in
/// [`SendStreams`], datagrams with no group assigned in `DatagramQueue`.
/// Real `SendGroupId` values start at 1 (see `neqo-http3`'s `send_group.rs`),
/// so 0 is safe as a shared sentinel. Rejected by
/// [`SendStreams::set_sendgroup`](crate::send_stream::SendStreams::set_sendgroup).
pub(crate) const NULL_GROUP_ID: SendGroupId = SendGroupId::new(0);

#[derive(Copy, Clone, Eq, PartialEq)]
pub struct StreamOrder {
    pub sendorder: Option<SendOrder>,
}

// We want highest to lowest, with None being higher than any value
impl Ord for StreamOrder {
    fn cmp(&self, other: &Self) -> Ordering {
        if self.sendorder.is_some() && other.sendorder.is_some() {
            // We want reverse order (high to low) when both values are specified.
            other.sendorder.cmp(&self.sendorder)
        } else {
            self.sendorder.cmp(&other.sendorder)
        }
    }
}

impl PartialOrd for StreamOrder {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

pub struct Streams {
    role: Role,
    tps: Rc<RefCell<TransportParametersHandler>>,
    events: ConnectionEvents,
    sender_fc: Rc<RefCell<SenderFlowControl<()>>>,
    receiver_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
    remote_stream_limits: RemoteStreamLimits,
    local_stream_limits: LocalStreamLimits,
    send: SendStreams,
    recv: RecvStreams,
}

impl Streams {
    pub fn new(
        tps: Rc<RefCell<TransportParametersHandler>>,
        role: Role,
        events: ConnectionEvents,
    ) -> Self {
        let limit_bidi = tps.borrow().local().get_integer(InitialMaxStreamsBidi);
        let limit_uni = tps.borrow().local().get_integer(InitialMaxStreamsUni);
        let max_data = tps.borrow().local().get_integer(InitialMaxData);
        Self {
            role,
            tps,
            events,
            sender_fc: Rc::new(RefCell::new(SenderFlowControl::new((), 0))),
            receiver_fc: Rc::new(RefCell::new(ReceiverFlowControl::new((), max_data))),
            remote_stream_limits: RemoteStreamLimits::new(limit_bidi, limit_uni, role),
            local_stream_limits: LocalStreamLimits::new(role),
            send: SendStreams::default(),
            recv: RecvStreams::new(role),
        }
    }

    #[must_use]
    pub fn is_stream_id_allowed(&self, stream_id: StreamId) -> bool {
        self.remote_stream_limits[stream_id.stream_type()].is_allowed(stream_id)
    }

    pub fn zero_rtt_rejected(&mut self) {
        self.clear_streams();
        // Rejected data does not count against the new 1-RTT flow-control limit.
        *self.sender_fc.borrow_mut() = SenderFlowControl::new((), 0);
        // `set_remote_max_streams` may have raised these above the configured transport
        // parameter; they are never reduced.
        debug_assert!(
            self.remote_stream_limits[StreamType::BiDi].max_active()
                >= self.tps.borrow().local().get_integer(InitialMaxStreamsBidi)
        );
        debug_assert!(
            self.remote_stream_limits[StreamType::UniDi].max_active()
                >= self.tps.borrow().local().get_integer(InitialMaxStreamsUni)
        );
        self.local_stream_limits = LocalStreamLimits::new(self.role);
    }

    /// # Errors
    /// When the frame is invalid.
    #[expect(clippy::too_many_lines, reason = "Yep, but it's a nice big match.")]
    pub fn input_frame(&mut self, frame: &Frame, stats: &mut FrameStats) -> Res<()> {
        match frame {
            Frame::ResetStream {
                stream_id,
                application_error_code,
                final_size,
            } => {
                stats.reset_stream += 1;
                // Terminate connection with STREAM_STATE_ERROR if send-only
                // stream (-transport 19.4)
                if stream_id.is_send_only(self.role) {
                    return Err(Error::StreamState);
                }
                if self.obtain_stream(*stream_id)?.1.is_some() {
                    // A plain RESET_STREAM is a reliable reset with `reliable_size == 0`.
                    self.recv
                        .reset(*stream_id, *application_error_code, *final_size, 0)?;
                }
            }
            Frame::ResetStreamAt {
                stream_id,
                application_error_code,
                final_size,
                reliable_size,
            } => {
                stats.reset_stream_at += 1;
                // We must have advertised support to legitimately receive this frame.
                if !self.tps.borrow().local().get_empty(ResetStreamAt) {
                    return Err(Error::ProtocolViolation);
                }
                if stream_id.is_send_only(self.role) {
                    return Err(Error::StreamState);
                }
                if self.obtain_stream(*stream_id)?.1.is_some() {
                    self.recv.reset(
                        *stream_id,
                        *application_error_code,
                        *final_size,
                        *reliable_size,
                    )?;
                }
            }
            Frame::StopSending {
                stream_id,
                application_error_code,
            } => {
                stats.stop_sending += 1;
                // Terminate connection with STREAM_STATE_ERROR if receive-only
                // stream (-transport 19.5)
                if stream_id.is_recv_only(self.role) {
                    return Err(Error::StreamState);
                }
                self.events
                    .send_stream_stop_sending(*stream_id, *application_error_code);
                if let (Some(ss), _) = self.obtain_stream(*stream_id)? {
                    // The peer has no interest in this data, so drop any commitment and send a
                    // plain RESET_STREAM rather than reliably delivering the committed prefix.
                    ss.drop_commitment();
                    ss.reset(*application_error_code);
                }
            }
            Frame::Stream {
                fin,
                stream_id,
                offset,
                data,
                ..
            } => {
                stats.stream += 1;
                // Terminate connection with STREAM_STATE_ERROR if send-only
                // stream (-transport 19.8)
                if stream_id.is_send_only(self.role) {
                    return Err(Error::StreamState);
                }
                if let (_, Some(rs)) = self.obtain_stream(*stream_id)? {
                    rs.inbound_stream_frame(*fin, *offset, data)?;
                }
            }
            Frame::MaxData { maximum_data } => {
                stats.max_data += 1;
                self.handle_max_data(*maximum_data);
            }
            Frame::MaxStreamData {
                stream_id,
                maximum_stream_data,
            } => {
                trace!(
                    "Stream {} Received MaxStreamData {}",
                    *stream_id, *maximum_stream_data
                );
                stats.max_stream_data += 1;
                // Terminate connection with STREAM_STATE_ERROR if receive-only
                // stream (-transport 19.10)
                if stream_id.is_recv_only(self.role) {
                    return Err(Error::StreamState);
                }
                if let (Some(ss), _) = self.obtain_stream(*stream_id)? {
                    ss.set_max_stream_data(*maximum_stream_data);
                }
            }
            Frame::MaxStreams {
                stream_type,
                maximum_streams,
            } => {
                stats.max_streams += 1;
                self.handle_max_streams(*stream_type, *maximum_streams);
            }
            Frame::DataBlocked { data_limit } => {
                debug!("Received DataBlocked with data limit {data_limit}");
                stats.data_blocked += 1;
                self.handle_data_blocked();
            }
            Frame::StreamDataBlocked { stream_id, .. } => {
                trace!("Received StreamDataBlocked");
                stats.stream_data_blocked += 1;
                // Terminate connection with STREAM_STATE_ERROR if send-only
                // stream (-transport 19.13)
                if stream_id.is_send_only(self.role) {
                    return Err(Error::StreamState);
                }

                if let (_, Some(rs)) = self.obtain_stream(*stream_id)? {
                    rs.send_flowc_update();
                }
            }
            Frame::StreamsBlocked { .. } => {
                stats.streams_blocked += 1;
                // We send an update every time we retire a stream. There is no need to
                // trigger flow updates here.
            }
            _ => return Err(Error::Internal), // This is not a stream frame.
        }
        Ok(())
    }

    pub fn write_maintenance_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
        now: Instant,
        rtt: Duration,
    ) {
        // Send `DATA_BLOCKED` as necessary.
        self.sender_fc
            .borrow_mut()
            .write_frames(builder, tokens, stats);
        if builder.is_full() {
            return;
        }

        // Send `MAX_DATA` as necessary.
        self.receiver_fc
            .borrow_mut()
            .write_frames(builder, tokens, stats, now, rtt);
        if builder.is_full() {
            return;
        }

        self.recv.write_frames(builder, tokens, stats, now, rtt);

        self.remote_stream_limits[StreamType::BiDi].write_frames(builder, tokens, stats);
        if builder.is_full() {
            return;
        }
        self.remote_stream_limits[StreamType::UniDi].write_frames(builder, tokens, stats);
        if builder.is_full() {
            return;
        }

        self.local_stream_limits[StreamType::BiDi].write_frames(builder, tokens, stats);
        if builder.is_full() {
            return;
        }

        self.local_stream_limits[StreamType::UniDi].write_frames(builder, tokens, stats);
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        priority: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        self.send.write_frames(priority, builder, tokens, stats);
    }

    pub fn lost(&mut self, token: &StreamRecoveryToken) {
        match token {
            StreamRecoveryToken::Stream(st) => self.send.lost(st),
            StreamRecoveryToken::ResetStream { stream_id } => self.send.reset_lost(*stream_id),
            StreamRecoveryToken::StreamDataBlocked { stream_id, limit } => {
                self.send.blocked_lost(*stream_id, *limit);
            }
            StreamRecoveryToken::MaxStreamData {
                stream_id,
                max_data,
            } => {
                if let Ok((_, Some(rs))) = self.obtain_stream(*stream_id) {
                    rs.max_stream_data_lost(*max_data);
                }
            }
            StreamRecoveryToken::StopSending { stream_id } => {
                if let Ok((_, Some(rs))) = self.obtain_stream(*stream_id) {
                    rs.stop_sending_lost();
                }
            }
            StreamRecoveryToken::StreamsBlocked { stream_type, limit } => {
                self.local_stream_limits[*stream_type].frame_lost(*limit);
            }
            StreamRecoveryToken::MaxStreams {
                stream_type,
                max_streams,
            } => {
                self.remote_stream_limits[*stream_type].frame_lost(*max_streams);
            }
            StreamRecoveryToken::DataBlocked(limit) => {
                self.sender_fc.borrow_mut().frame_lost(*limit);
            }
            StreamRecoveryToken::MaxData(maximum_data) => {
                self.receiver_fc.borrow_mut().frame_lost(*maximum_data);
            }
        }
    }

    pub fn acked(&mut self, token: &StreamRecoveryToken) {
        match token {
            StreamRecoveryToken::Stream(st) => self.send.acked(st),
            StreamRecoveryToken::ResetStream { stream_id } => self.send.reset_acked(*stream_id),
            StreamRecoveryToken::StopSending { stream_id } => {
                self.recv.stop_sending_acked(*stream_id);
            }
            // We only worry when these are lost
            StreamRecoveryToken::DataBlocked(_)
            | StreamRecoveryToken::StreamDataBlocked { .. }
            | StreamRecoveryToken::MaxStreamData { .. }
            | StreamRecoveryToken::StreamsBlocked { .. }
            | StreamRecoveryToken::MaxStreams { .. }
            | StreamRecoveryToken::MaxData(_) => (),
        }
    }

    pub fn clear_streams(&mut self) {
        self.send.clear();
        self.recv.clear();
    }

    pub fn update_stats(&self, stats: &mut Stats) {
        stats.fc_max_active = self.receiver_fc.borrow().max_active();
    }

    /// # Errors
    /// When the stream does not exist or has no more data.
    ///
    /// # Returns
    /// `(bytes_read, fin)` where `fin` is `true` when the stream has ended.
    pub fn recv(&mut self, stream_id: StreamId, data: &mut [u8]) -> Res<(usize, bool)> {
        self.recv.read(stream_id, data)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn stop_sending(&mut self, stream_id: StreamId, err: AppError) -> Res<()> {
        self.recv.stop_sending(stream_id, err)
    }

    pub fn cleanup_closed_streams(&mut self) {
        // Remove ended send streams. If any were removed, bidi recv streams whose
        // send counterpart just disappeared may now be clearable too.
        self.recv.set_ended(self.send.remove_ended());

        let (removed_bidi, removed_uni) = self.recv.remove_ended(&self.send);

        // Send max_streams updates if we removed remote-initiated recv streams.
        // The updates will be send if any streams has been removed.
        self.remote_stream_limits[StreamType::BiDi].add_retired(removed_bidi);
        self.remote_stream_limits[StreamType::UniDi].add_retired(removed_uni);
    }

    fn ensure_created_if_remote(&mut self, stream_id: StreamId) -> Res<()> {
        if !stream_id.is_remote_initiated(self.role)
            || !self.remote_stream_limits[stream_id.stream_type()].is_new_stream(stream_id)?
        {
            // If it is not a remote stream and stream already exist.
            return Ok(());
        }

        let tp = match stream_id.stream_type() {
            // From the local perspective, this is a remote- originated BiDi stream. From
            // the remote perspective, this is a local-originated BiDi stream. Therefore,
            // look at the local transport parameters for the
            // INITIAL_MAX_STREAM_DATA_BIDI_REMOTE value to decide how much this endpoint
            // will allow its peer to send.
            StreamType::BiDi => InitialMaxStreamDataBidiRemote,
            StreamType::UniDi => InitialMaxStreamDataUni,
        };
        let recv_initial_max_stream_data = self.tps.borrow().local().get_integer(tp);

        while self.remote_stream_limits[stream_id.stream_type()].is_new_stream(stream_id)? {
            let next_stream_id =
                self.remote_stream_limits[stream_id.stream_type()].take_stream_id();
            self.events.new_stream(next_stream_id);

            self.recv.insert(
                next_stream_id,
                RecvStream::new(
                    next_stream_id,
                    recv_initial_max_stream_data,
                    Rc::clone(&self.receiver_fc),
                    self.events.clone(),
                ),
            );

            if next_stream_id.is_bidi() {
                // From the local perspective, this is a remote- originated BiDi stream.
                // From the remote perspective, this is a local-originated BiDi stream.
                // Therefore, look at the remote's transport parameters for the
                // INITIAL_MAX_STREAM_DATA_BIDI_LOCAL value to decide how much this endpoint
                // is allowed to send its peer.
                let send_initial_max_stream_data = self
                    .tps
                    .borrow()
                    .remote()
                    .get_integer(InitialMaxStreamDataBidiLocal);
                self.send.insert(
                    next_stream_id,
                    SendStream::new(
                        next_stream_id,
                        send_initial_max_stream_data,
                        Rc::clone(&self.sender_fc),
                        self.events.clone(),
                    ),
                );
            }
        }
        Ok(())
    }

    /// Get or make a stream, and implicitly open additional streams as
    /// indicated by its stream id.
    /// # Errors
    /// When the stream cannot be created due to stream limits.
    /// When the stream is locally-initiated and has not existed.
    pub fn obtain_stream(
        &mut self,
        stream_id: StreamId,
    ) -> Res<(Option<&mut SendStream>, Option<&mut RecvStream>)> {
        self.ensure_created_if_remote(stream_id)?;
        let ss = self.send.get_mut(stream_id).ok();
        let rs = self.recv.get_mut(stream_id).ok();
        // If it is:
        // - neither a known send nor receive stream,
        // - and it must be locally initiated,
        // - and its index is larger than the local used stream limit,
        // then it is an illegal stream.
        if ss.is_none()
            && rs.is_none()
            && !stream_id.is_remote_initiated(self.role)
            && self.local_stream_limits[stream_id.stream_type()].used() <= stream_id.index()
        {
            return Err(Error::StreamState);
        }
        Ok((ss, rs))
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn set_sendorder(&mut self, stream_id: StreamId, sendorder: Option<SendOrder>) -> Res<()> {
        self.send.set_sendorder(stream_id, sendorder)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn set_fairness(&mut self, stream_id: StreamId, fairness: bool) -> Res<()> {
        self.send.set_fairness(stream_id, fairness)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn set_sendgroup(&mut self, stream_id: StreamId, group_id: Option<SendGroupId>) -> Res<()> {
        self.send.set_sendgroup(stream_id, group_id)
    }

    /// # Errors
    /// When a stream cannot be created, which might be temporary.
    pub fn stream_create(&mut self, st: StreamType) -> Res<StreamId> {
        match self.local_stream_limits.take_stream_id(st) {
            None => Err(Error::StreamLimit),
            Some(new_id) => {
                let send_limit_tp = match st {
                    StreamType::UniDi => InitialMaxStreamDataUni,
                    StreamType::BiDi => InitialMaxStreamDataBidiRemote,
                };
                let send_limit = self.tps.borrow().remote().get_integer(send_limit_tp);
                let stream = SendStream::new(
                    new_id,
                    send_limit,
                    Rc::clone(&self.sender_fc),
                    self.events.clone(),
                );
                self.send.insert(new_id, stream);

                if st == StreamType::BiDi {
                    // From the local perspective, this is a local- originated BiDi stream. From the
                    // remote perspective, this is a remote-originated BiDi stream. Therefore, look
                    // at the local transport parameters for the
                    // INITIAL_MAX_STREAM_DATA_BIDI_LOCAL value to decide how
                    // much this endpoint will allow its peer to send.
                    let recv_initial_max_stream_data = self
                        .tps
                        .borrow()
                        .local()
                        .get_integer(InitialMaxStreamDataBidiLocal);

                    self.recv.insert(
                        new_id,
                        RecvStream::new(
                            new_id,
                            recv_initial_max_stream_data,
                            Rc::clone(&self.receiver_fc),
                            self.events.clone(),
                        ),
                    );
                }
                Ok(new_id)
            }
        }
    }

    /// See `Connection::set_remote_max_streams`.
    pub fn set_remote_max_streams(&mut self, stream_type: StreamType, max: u64) {
        let max = max.min(MAX_STREAMS_LIMIT);
        if max > self.remote_stream_limits[stream_type].max_active() {
            self.remote_stream_limits[stream_type].set_max_active(max);
        }
    }

    pub fn handle_max_data(&mut self, maximum_data: u64) {
        let previous_limit = self.sender_fc.borrow().available();
        let Some(current_limit) = self.sender_fc.borrow_mut().update(maximum_data) else {
            return;
        };

        self.send
            .maybe_emit_writable_events(previous_limit, current_limit);
    }

    pub fn handle_data_blocked(&self) {
        self.receiver_fc.borrow_mut().send_flowc_update();
    }

    pub fn set_initial_limits(&mut self) {
        _ = self.local_stream_limits[StreamType::BiDi].update(
            self.tps
                .borrow()
                .remote()
                .get_integer(InitialMaxStreamsBidi),
        );
        _ = self.local_stream_limits[StreamType::UniDi]
            .update(self.tps.borrow().remote().get_integer(InitialMaxStreamsUni));

        // As a client, there are two sets of initial limits for sending stream data.
        // If the second limit is higher and streams have been created, then
        // ensure that streams are not blocked on the lower limit.
        if self.role == Role::Client {
            self.send.update_initial_limit(self.tps.borrow().remote());
        }

        self.sender_fc
            .borrow_mut()
            .update(self.tps.borrow().remote().get_integer(InitialMaxData));

        if self.local_stream_limits[StreamType::BiDi].available() > 0 {
            self.events.send_stream_creatable(StreamType::BiDi);
        }
        if self.local_stream_limits[StreamType::UniDi].available() > 0 {
            self.events.send_stream_creatable(StreamType::UniDi);
        }
    }

    pub fn handle_max_streams(&mut self, stream_type: StreamType, maximum_streams: u64) {
        let increased = self.local_stream_limits[stream_type]
            .update(maximum_streams)
            .is_some();
        if increased {
            self.events.send_stream_creatable(stream_type);
        }
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn get_send_stream_mut(&mut self, stream_id: StreamId) -> Res<&mut SendStream> {
        self.send.get_mut(stream_id)
    }

    pub(crate) fn set_writable_event_low_watermark(
        &mut self,
        stream_id: StreamId,
        watermark: NonZeroUsize,
    ) -> Res<()> {
        self.send
            .set_writable_event_low_watermark(stream_id, watermark)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn get_send_stream(&self, stream_id: StreamId) -> Res<&SendStream> {
        self.send.get(stream_id)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn get_recv_stream_mut(&mut self, stream_id: StreamId) -> Res<&mut RecvStream> {
        self.recv.get_mut(stream_id)
    }

    /// # Errors
    /// When the stream does not exist.
    pub fn keep_alive(&mut self, stream_id: StreamId, keep: bool) -> Res<()> {
        self.recv.keep_alive(stream_id, keep)
    }

    #[must_use]
    pub fn need_keep_alive(&self) -> bool {
        self.recv.need_keep_alive()
    }
}
