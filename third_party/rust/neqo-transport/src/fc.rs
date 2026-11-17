// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Tracks possibly-redundant flow control signals from other code and converts
// into flow control frames needing to be sent to the remote.

use std::{
    cmp::min,
    fmt::{Debug, Display},
    num::NonZeroU64,
    ops::{Deref, DerefMut, Index, IndexMut},
    time::{Duration, Instant},
};

use enum_map::EnumMap;
use neqo_common::{Buffer, Length, MAX_VARINT, Role, const_min_u64, to_u64};
use tracing::{debug, trace};

use crate::{
    Error, Res,
    connection::params::{MAX_LOCAL_MAX_DATA, MAX_LOCAL_MAX_STREAM_DATA},
    frame::FrameType,
    packet,
    recovery::{self, StreamRecoveryToken},
    stats::FrameStats,
    stream_id::{MAX_STREAMS_LIMIT, StreamId, StreamType},
};

/// Fraction of a flow control window after which a receiver sends a window
/// update.
///
/// In steady-state and max utilization, a value of 4 leads to 4 window updates
/// per RTT.
///
/// Value aligns with [`crate::connection::params::ConnectionParameters::DEFAULT_ACK_RATIO`].
pub const WINDOW_UPDATE_FRACTION: u64 = 4;

/// Multiplier for auto-tuning the stream receive window.
///
/// See [`ReceiverFlowControl::auto_tune`].
///
/// Note that the flow control window should grow at least as fast as the
/// congestion control window, in order to not unnecessarily limit throughput.
const WINDOW_INCREASE_MULTIPLIER: u64 = 4;

/// Subject for flow control auto-tuning, used to avoid heap allocations
/// when logging.
#[derive(Debug, Clone, Copy)]
enum AutoTuneSubject {
    Connection,
    Stream(StreamId),
}

impl Display for AutoTuneSubject {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Connection => write!(f, "connection"),
            Self::Stream(id) => write!(f, "stream {id}"),
        }
    }
}

#[derive(Debug)]
pub struct SenderFlowControl<T>
where
    T: Debug + Sized,
{
    /// The thing that we're counting for.
    subject: T,
    /// The limit.
    limit: u64,
    /// How much of that limit we've used.
    used: u64,
    /// The limit at which blocking was last reported, or `None` if never blocked.
    /// Updated each time the sender decides it is blocked, ensuring that blocking
    /// at any given limit is only reported once.
    blocked_at: Option<u64>,
    /// Whether a blocked frame should be sent.
    blocked_frame: bool,
}

impl<T> SenderFlowControl<T>
where
    T: Debug + Sized,
{
    /// Make a new instance with the initial value and subject.
    pub const fn new(subject: T, initial: u64) -> Self {
        Self {
            subject,
            limit: initial,
            used: 0,
            blocked_at: None,
            blocked_frame: false,
        }
    }

    /// Update the maximum. Returns `Some` with the updated available flow
    /// control if the change was an increase and `None` otherwise.
    pub fn update(&mut self, limit: u64) -> Option<usize> {
        (limit > self.limit).then(|| {
            self.limit = limit;
            self.blocked_frame = false;
            self.available()
        })
    }

    /// Consume flow control.
    pub fn consume<L: Length>(&mut self, count: L) {
        let amt = count.as_u64();
        debug_assert!(self.used + amt <= self.limit);
        self.used += amt;
    }

    /// Get available flow control.
    #[expect(clippy::cast_possible_truncation, reason = "value is capped")]
    pub const fn available(&self) -> usize {
        const_min_u64(self.limit - self.used, to_u64(usize::MAX)) as usize
    }

    /// How much data has been written.
    pub const fn used(&self) -> u64 {
        self.used
    }

    /// Mark flow control as blocked.
    /// This only does something if the current limit exceeds the last reported blocking limit.
    pub const fn blocked(&mut self) {
        if let Some(block) = self.blocked_at
            && self.limit <= block
        {
            return;
        }
        self.blocked_at = Some(self.limit);
        self.blocked_frame = true;
    }

    /// Return whether a blocking frame needs to be sent.
    /// This is `Some` with the active limit if `blocked` has been called,
    /// if a blocking frame has not been sent (or it has been lost), and
    /// if the blocking condition remains.
    fn blocked_needed(&self) -> Option<u64> {
        self.blocked_at
            .filter(|&l| self.blocked_frame && self.limit <= l)
    }

    /// Returns whether a blocking frame needs to be sent.
    pub(crate) fn is_blocked(&self) -> bool {
        self.blocked_needed().is_some()
    }

    /// Clear the need to send a blocked frame.
    const fn blocked_sent(&mut self) {
        self.blocked_frame = false;
    }

    /// Mark a blocked frame as having been lost.
    /// Only send again if value of `self.blocked_at` hasn't increased since sending.
    /// That would imply that the limit has since increased.
    pub const fn frame_lost(&mut self, limit: u64) {
        if let Some(block) = self.blocked_at
            && block == limit
        {
            self.blocked_frame = true;
        }
    }
}

impl SenderFlowControl<()> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Some(limit) = self.blocked_needed()
            && builder.write_varint_frame(&[FrameType::DataBlocked.into(), limit])
        {
            stats.data_blocked += 1;
            tokens.push(recovery::Token::Stream(StreamRecoveryToken::DataBlocked(
                limit,
            )));
            self.blocked_sent();
        }
    }
}

impl SenderFlowControl<StreamId> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Some(limit) = self.blocked_needed()
            && builder.write_varint_frame(&[
                FrameType::StreamDataBlocked.into(),
                self.subject.as_u64(),
                limit,
            ])
        {
            stats.stream_data_blocked += 1;
            tokens.push(recovery::Token::Stream(
                StreamRecoveryToken::StreamDataBlocked {
                    stream_id: self.subject,
                    limit,
                },
            ));
            self.blocked_sent();
        }
    }
}

impl SenderFlowControl<StreamType> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Some(limit) = self.blocked_needed() {
            let frame = match self.subject {
                StreamType::BiDi => FrameType::StreamsBlockedBiDi,
                StreamType::UniDi => FrameType::StreamsBlockedUniDi,
            };
            if builder.write_varint_frame(&[frame.into(), limit]) {
                stats.streams_blocked += 1;
                tokens.push(recovery::Token::Stream(
                    StreamRecoveryToken::StreamsBlocked {
                        stream_type: self.subject,
                        limit,
                    },
                ));
                self.blocked_sent();
            }
        }
    }
}

#[derive(Debug, Default)]
pub struct ReceiverFlowControl<T>
where
    T: Debug + Sized,
{
    /// The thing that we're counting for.
    subject: T,
    /// The maximum amount of items that can be active (e.g., the size of the receive buffer).
    max_active: u64,
    /// Last max allowed sent.
    max_allowed: u64,
    /// Last time a flow control update was sent.
    ///
    /// Used by auto-tuning logic to estimate sending rate between updates.
    /// This is active for both stream-level
    /// ([`ReceiverFlowControl<StreamId>`]) and connection-level
    /// ([`ReceiverFlowControl<()>`]) flow control.
    last_update: Option<Instant>,
    /// Item received, but not retired yet.
    /// This will be used for byte flow control: each stream will remember its largest byte
    /// offset received and session flow control will remember the sum of all bytes consumed
    /// by all streams.
    consumed: u64,
    /// Retired items.
    retired: u64,
    frame_pending: bool,
}

impl<T> ReceiverFlowControl<T>
where
    T: Debug + Sized,
{
    /// Make a new instance with the initial value and subject.
    pub const fn new(subject: T, max: u64) -> Self {
        Self {
            subject,
            max_active: max,
            max_allowed: max,
            last_update: None,
            consumed: 0,
            retired: 0,
            frame_pending: false,
        }
    }

    /// Retire some items and maybe send flow control
    /// update.
    pub const fn retire(&mut self, retired: u64) {
        if retired <= self.retired {
            return;
        }

        self.retired = retired;
        if self.should_send_update() {
            self.frame_pending = true;
        }
    }

    /// This function is called when `STREAM_DATA_BLOCKED` frame is received.
    /// The flow control will try to send an update if possible.
    pub const fn send_flowc_update(&mut self) {
        if self.retired + self.max_active > self.max_allowed {
            self.frame_pending = true;
        }
    }

    const fn should_send_update(&self) -> bool {
        let window_bytes_unused = self.max_allowed - self.retired;
        window_bytes_unused < self.max_active - self.max_active / WINDOW_UPDATE_FRACTION
    }

    pub const fn frame_needed(&self) -> bool {
        self.frame_pending
    }

    pub fn next_limit(&self) -> u64 {
        min(
            self.retired + self.max_active,
            // Flow control limits are encoded as QUIC varints and are thus
            // limited to the maximum QUIC varint value.
            MAX_VARINT,
        )
    }

    pub const fn max_active(&self) -> u64 {
        self.max_active
    }

    pub const fn frame_lost(&mut self, maximum_data: u64) {
        if maximum_data == self.max_allowed {
            self.frame_pending = true;
        }
    }

    const fn frame_sent(&mut self, new_max: u64) {
        self.max_allowed = new_max;
        self.frame_pending = false;
    }

    pub const fn set_max_active(&mut self, max: u64) {
        // If max_active has been increased, send an update immediately.
        self.frame_pending |= self.max_active < max;
        self.max_active = max;
    }

    pub const fn retired(&self) -> u64 {
        self.retired
    }

    pub const fn consumed(&self) -> u64 {
        self.consumed
    }

    /// Core auto-tuning logic for adjusting the maximum flow control window.
    ///
    /// This method is called by both connection-level and stream-level
    /// implementations. It increases `max_active` when the sending rate exceeds
    /// what the current window and RTT would allow, capping at `max_window`.
    fn auto_tune_inner(
        &mut self,
        now: Instant,
        rtt: Duration,
        max_window: u64,
        subject: AutoTuneSubject,
    ) {
        let Some(max_allowed_sent_at) = self.last_update else {
            return;
        };

        let Ok(elapsed): Result<u64, _> = now
            .duration_since(max_allowed_sent_at)
            .as_micros()
            .try_into()
        else {
            return;
        };

        let Ok(rtt): Result<NonZeroU64, _> = rtt
            .as_micros()
            .try_into()
            .and_then(|rtt: u64| NonZeroU64::try_from(rtt))
        else {
            // RTT is zero, no need for tuning.
            return;
        };

        // Scale the max_active window down by
        // [(F-1) / F]; where F=WINDOW_UPDATE_FRACTION.
        //
        // In the ideal case, each byte sent would trigger a flow control
        // update.  However, in practice we only send updates every
        // WINDOW_UPDATE_FRACTION of the window.  Thus, when not application
        // limited, in a steady state transfer it takes 1 RTT after sending 1 /
        // F bytes for the sender to receive the next update. The sender is
        // effectively limited to [(F-1) / F] bytes per RTT.
        //
        // By calculating with this effective window instead of the full
        // max_active, we account for the inherent delay between when the sender
        // would ideally receive flow control updates and when they actually
        // arrive due to our batched update strategy.
        //
        // Example with F=4 without adjustment:
        //
        // t=0         start sending
        // t=RTT/4     sent 1/4 of window total
        // t=RTT       sent 1 window total
        //             sender blocked for RTT/4
        // t=RTT+RTT/4 receive update for 1/4 of window
        //
        // Example with F=4 with adjustment:
        //
        // t=0         start sending
        // t=RTT/4     sent 1/4 of window total
        // t=RTT       sent 1 window total
        // t=RTT+RTT/4 sent 1+1/4 window total; receive update for 1/4 of window (just in time)
        let effective_window =
            (self.max_active * (WINDOW_UPDATE_FRACTION - 1)) / (WINDOW_UPDATE_FRACTION);

        // Compute the amount of bytes we have received in excess
        // of what `max_active` might allow.
        let window_bytes_expected = (effective_window * elapsed) / (rtt);

        let window_bytes_used = self.max_active - (self.max_allowed - self.retired);
        let Some(excess) = window_bytes_used.checked_sub(window_bytes_expected) else {
            // Used below expected. No auto-tuning needed.
            return;
        };

        let prev_max_active = self.max_active;
        let new_max_active = min(
            self.max_active + excess * WINDOW_INCREASE_MULTIPLIER,
            max_window,
        );

        if new_max_active <= prev_max_active {
            // Never decrease max_active, even if max_window is smaller.  This
            // can happen if max_active was set manually.
            return;
        }

        self.max_active = new_max_active;
        debug!(
            "Increasing max {subject} receive window by {} B, \
                previous max_active: {} MiB, \
                new max_active: {} MiB, \
                last update: {:?}, \
                rtt: {rtt:?}",
            new_max_active - prev_max_active,
            prev_max_active / 1024 / 1024,
            self.max_active / 1024 / 1024,
            now - max_allowed_sent_at,
        );
    }
}

impl ReceiverFlowControl<()> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
        now: Instant,
        rtt: Duration,
    ) {
        if !self.frame_needed() {
            return;
        }

        self.auto_tune(now, rtt);

        let max_allowed = self.next_limit();
        if builder.write_varint_frame(&[FrameType::MaxData.into(), max_allowed]) {
            stats.max_data += 1;
            tokens.push(recovery::Token::Stream(StreamRecoveryToken::MaxData(
                max_allowed,
            )));
            self.frame_sent(max_allowed);
            self.last_update = Some(now);
        }
    }

    /// Auto-tune [`ReceiverFlowControl::max_active`], i.e. the connection flow
    /// control window.
    ///
    /// If the sending rate (`window_bytes_used`) exceeds the rate allowed by
    /// the maximum flow control window and the current rtt
    /// (`window_bytes_expected`), try to increase the maximum flow control
    /// window ([`ReceiverFlowControl::max_active`]).
    fn auto_tune(&mut self, now: Instant, rtt: Duration) {
        self.auto_tune_inner(now, rtt, MAX_LOCAL_MAX_DATA, AutoTuneSubject::Connection);
    }

    pub fn add_retired(&mut self, count: u64) {
        debug_assert!(self.retired + count <= self.consumed);
        self.retired += count;
        if self.should_send_update() {
            self.frame_pending = true;
        }
    }

    pub fn consume(&mut self, count: u64) -> Res<()> {
        if self.consumed + count > self.max_allowed {
            trace!(
                "Session RX window exceeded: consumed:{} new:{count} limit:{}",
                self.consumed, self.max_allowed
            );
            return Err(Error::FlowControl);
        }
        self.consumed += count;
        Ok(())
    }
}

impl ReceiverFlowControl<StreamId> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
        now: Instant,
        rtt: Duration,
    ) {
        if !self.frame_needed() {
            return;
        }

        self.auto_tune(now, rtt);

        let max_allowed = self.next_limit();
        if builder.write_varint_frame(&[
            FrameType::MaxStreamData.into(),
            self.subject.as_u64(),
            max_allowed,
        ]) {
            stats.max_stream_data += 1;
            tokens.push(recovery::Token::Stream(
                StreamRecoveryToken::MaxStreamData {
                    stream_id: self.subject,
                    max_data: max_allowed,
                },
            ));
            self.frame_sent(max_allowed);
            self.last_update = Some(now);
        }
    }

    /// Auto-tune [`ReceiverFlowControl::max_active`], i.e. the stream flow
    /// control window.
    ///
    /// If the sending rate (`window_bytes_used`) exceeds the rate allowed by
    /// the maximum flow control window and the current rtt
    /// (`window_bytes_expected`), try to increase the maximum flow control
    /// window ([`ReceiverFlowControl::max_active`]).
    fn auto_tune(&mut self, now: Instant, rtt: Duration) {
        self.auto_tune_inner(
            now,
            rtt,
            MAX_LOCAL_MAX_STREAM_DATA,
            AutoTuneSubject::Stream(self.subject),
        );
    }

    pub fn add_retired(&mut self, count: u64) {
        debug_assert!(self.retired + count <= self.consumed);
        self.retired += count;
        if self.should_send_update() {
            self.frame_pending = true;
        }
    }

    pub fn set_consumed(&mut self, consumed: u64) -> Res<u64> {
        if consumed <= self.consumed {
            return Ok(0);
        }

        if consumed > self.max_allowed {
            trace!("Stream RX window exceeded: {consumed}");
            return Err(Error::FlowControl);
        }
        let new_consumed = consumed - self.consumed;
        self.consumed = consumed;
        Ok(new_consumed)
    }
}

impl ReceiverFlowControl<StreamType> {
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if !self.frame_needed() {
            return;
        }
        // RFC 9000, Section 19.11: a stream count above 2^60 is a frame encoding error.
        let max_streams = min(self.next_limit(), MAX_STREAMS_LIMIT);
        let frame = match self.subject {
            StreamType::BiDi => FrameType::MaxStreamsBiDi,
            StreamType::UniDi => FrameType::MaxStreamsUniDi,
        };
        if builder.write_varint_frame(&[frame.into(), max_streams]) {
            stats.max_streams += 1;
            tokens.push(recovery::Token::Stream(StreamRecoveryToken::MaxStreams {
                stream_type: self.subject,
                max_streams,
            }));
            self.frame_sent(max_streams);
        }
    }

    /// Check if received item exceeds the allowed flow control limit.
    pub const fn check_allowed(&self, new_end: u64) -> bool {
        new_end < self.max_allowed
    }

    /// Retire given amount of additional data.
    /// This function will send flow updates immediately.
    pub const fn add_retired(&mut self, count: u64) {
        self.retired += count;
        if count > 0 {
            self.send_flowc_update();
        }
    }
}

pub struct RemoteStreamLimit {
    streams_fc: ReceiverFlowControl<StreamType>,
    next_stream: StreamId,
}

impl RemoteStreamLimit {
    pub const fn new(stream_type: StreamType, max_streams: u64, role: Role) -> Self {
        Self {
            streams_fc: ReceiverFlowControl::new(stream_type, max_streams),
            // // This is for a stream created by a peer, therefore we use role.remote().
            next_stream: StreamId::init(stream_type, role.remote()),
        }
    }

    pub const fn is_allowed(&self, stream_id: StreamId) -> bool {
        let stream_idx = stream_id.as_u64() >> 2;
        self.streams_fc.check_allowed(stream_idx)
    }

    pub fn is_new_stream(&self, stream_id: StreamId) -> Res<bool> {
        if !self.is_allowed(stream_id) {
            return Err(Error::StreamLimit);
        }
        Ok(stream_id >= self.next_stream)
    }

    pub fn take_stream_id(&mut self) -> StreamId {
        let new_stream = self.next_stream;
        self.next_stream.next();
        assert!(self.is_allowed(new_stream));
        new_stream
    }
}

impl Deref for RemoteStreamLimit {
    type Target = ReceiverFlowControl<StreamType>;
    fn deref(&self) -> &Self::Target {
        &self.streams_fc
    }
}

impl DerefMut for RemoteStreamLimit {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.streams_fc
    }
}

pub struct RemoteStreamLimits(EnumMap<StreamType, RemoteStreamLimit>);

impl RemoteStreamLimits {
    pub const fn new(local_max_stream_bidi: u64, local_max_stream_uni: u64, role: Role) -> Self {
        // Array order must match StreamType enum order: BiDi, UniDi
        Self(EnumMap::from_array([
            RemoteStreamLimit::new(StreamType::BiDi, local_max_stream_bidi, role),
            RemoteStreamLimit::new(StreamType::UniDi, local_max_stream_uni, role),
        ]))
    }
}

impl Index<StreamType> for RemoteStreamLimits {
    type Output = RemoteStreamLimit;

    fn index(&self, index: StreamType) -> &Self::Output {
        &self.0[index]
    }
}

impl IndexMut<StreamType> for RemoteStreamLimits {
    fn index_mut(&mut self, index: StreamType) -> &mut Self::Output {
        &mut self.0[index]
    }
}

pub struct LocalStreamLimits {
    limits: EnumMap<StreamType, SenderFlowControl<StreamType>>,
    role_bit: u64,
}

impl LocalStreamLimits {
    pub const fn new(role: Role) -> Self {
        Self {
            // Array order must match StreamType enum order: BiDi, UniDi
            limits: EnumMap::from_array([
                SenderFlowControl::new(StreamType::BiDi, 0),
                SenderFlowControl::new(StreamType::UniDi, 0),
            ]),
            role_bit: StreamId::role_bit(role),
        }
    }

    pub fn take_stream_id(&mut self, stream_type: StreamType) -> Option<StreamId> {
        let fc = &mut self.limits[stream_type];
        if fc.available() > 0 {
            let new_stream = fc.used();
            fc.consume(1_u64);
            Some(StreamId::from(
                (new_stream << 2) + stream_type as u64 + self.role_bit,
            ))
        } else {
            fc.blocked();
            None
        }
    }
}

impl Index<StreamType> for LocalStreamLimits {
    type Output = SenderFlowControl<StreamType>;

    fn index(&self, index: StreamType) -> &Self::Output {
        &self.limits[index]
    }
}

impl IndexMut<StreamType> for LocalStreamLimits {
    fn index_mut(&mut self, index: StreamType) -> &mut Self::Output {
        &mut self.limits[index]
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod test {
    #![allow(
        clippy::allow_attributes,
        clippy::unwrap_in_result,
        reason = "OK in tests."
    )]

    use std::{
        cmp::min,
        collections::VecDeque,
        time::{Duration, Instant},
    };

    use neqo_common::{Encoder, Role, to_u64};
    use nss::random;
    use tracing::debug;

    use super::{
        AutoTuneSubject, LocalStreamLimits, ReceiverFlowControl, RemoteStreamLimits,
        SenderFlowControl,
    };
    use crate::{
        ConnectionParameters, Error, INITIAL_LOCAL_MAX_DATA, INITIAL_LOCAL_MAX_STREAM_DATA, Res,
        connection::params::{MAX_LOCAL_MAX_DATA, MAX_LOCAL_MAX_STREAM_DATA},
        fc::WINDOW_UPDATE_FRACTION,
        packet, recovery,
        stats::FrameStats,
        stream_id::{MAX_STREAMS_LIMIT, StreamId, StreamType},
    };

    #[test]
    fn blocked_at_zero() {
        let mut fc = SenderFlowControl::new((), 0);
        fc.blocked();
        assert_eq!(fc.blocked_needed(), Some(0));
    }

    #[test]
    fn blocked() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.blocked();
        assert_eq!(fc.blocked_needed(), Some(10));
    }

    #[test]
    fn update_consume() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.consume(10_u64);
        assert_eq!(fc.available(), 0);
        fc.update(5); // An update lower than the current limit does nothing.
        assert_eq!(fc.available(), 0);
        fc.update(15);
        assert_eq!(fc.available(), 5);
        fc.consume(3_u64);
        assert_eq!(fc.available(), 2);
    }

    #[test]
    fn update_clears_blocked() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.blocked();
        assert_eq!(fc.blocked_needed(), Some(10));
        fc.update(5); // An update lower than the current limit does nothing.
        assert_eq!(fc.blocked_needed(), Some(10));
        fc.update(11);
        assert_eq!(fc.blocked_needed(), None);
    }

    #[test]
    fn lost_blocked_resent() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.blocked();
        fc.blocked_sent();
        assert_eq!(fc.blocked_needed(), None);
        fc.frame_lost(10);
        assert_eq!(fc.blocked_needed(), Some(10));
    }

    #[test]
    fn lost_after_increase() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.blocked();
        fc.blocked_sent();
        assert_eq!(fc.blocked_needed(), None);
        fc.update(11);
        fc.frame_lost(10);
        assert_eq!(fc.blocked_needed(), None);
    }

    #[test]
    fn lost_after_higher_blocked() {
        let mut fc = SenderFlowControl::new((), 10);
        fc.blocked();
        fc.blocked_sent();
        fc.update(11);
        fc.blocked();
        assert_eq!(fc.blocked_needed(), Some(11));
        fc.blocked_sent();
        fc.frame_lost(10);
        assert_eq!(fc.blocked_needed(), None);
    }

    #[test]
    fn do_no_need_max_allowed_frame_at_start() {
        let fc = ReceiverFlowControl::new((), 0);
        assert!(!fc.frame_needed());
    }

    #[test]
    fn max_allowed_after_items_retired() {
        let window = 100;
        let trigger = window / WINDOW_UPDATE_FRACTION;
        let mut fc = ReceiverFlowControl::new((), window);
        fc.retire(trigger);
        assert!(!fc.frame_needed());
        fc.retire(trigger + 1);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), window + trigger + 1);
    }

    #[test]
    fn need_max_allowed_frame_after_loss() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.retire(100);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 200);
        fc.frame_sent(200);
        assert!(!fc.frame_needed());
        fc.frame_lost(200);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 200);
    }

    #[test]
    fn no_max_allowed_frame_after_old_loss() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.retire(51);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 151);
        fc.frame_sent(151);
        assert!(!fc.frame_needed());
        fc.retire(102);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 202);
        fc.frame_sent(202);
        assert!(!fc.frame_needed());
        fc.frame_lost(151);
        assert!(!fc.frame_needed());
    }

    #[test]
    fn force_send_max_allowed() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.retire(10);
        assert!(!fc.frame_needed());
    }

    #[test]
    fn multiple_retries_after_frame_pending_is_set() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.retire(51);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 151);
        fc.retire(61);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 161);
        fc.retire(88);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 188);
        fc.retire(90);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 190);
        fc.frame_sent(190);
        assert!(!fc.frame_needed());
        fc.retire(141);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 241);
        fc.frame_sent(241);
        assert!(!fc.frame_needed());
    }

    #[test]
    fn new_retired_before_loss() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.retire(51);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 151);
        fc.frame_sent(151);
        assert!(!fc.frame_needed());
        fc.retire(62);
        assert!(!fc.frame_needed());
        fc.frame_lost(151);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 162);
    }

    #[test]
    fn changing_max_active() {
        let mut fc = ReceiverFlowControl::new((), 100);
        fc.set_max_active(50);

        // There is no MAX_STREAM_DATA frame needed.
        assert!(!fc.frame_needed());

        // We can still retire more than 50.
        fc.consume(60).unwrap();
        fc.retire(60);

        // There is no MAX_STREAM_DATA frame needed yet.
        assert!(!fc.frame_needed());
        fc.consume(16).unwrap();
        fc.retire(76);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 126);

        // Increase max_active.
        fc.set_max_active(60);
        assert!(fc.frame_needed());
        let new_max = fc.next_limit();
        assert_eq!(new_max, 136);

        // Sent update, accounting for the new `max_active`.
        fc.frame_sent(new_max);

        // We can retire more than 60.
        fc.consume(60).unwrap();
        fc.retire(136);
        assert!(fc.frame_needed());
        assert_eq!(fc.next_limit(), 196);
    }

    fn remote_stream_limits(role: Role, bidi: u64, unidi: u64) {
        let mut fc = RemoteStreamLimits::new(2, 1, role);
        assert!(
            fc[StreamType::BiDi]
                .is_new_stream(StreamId::from(bidi))
                .unwrap()
        );
        assert!(
            fc[StreamType::BiDi]
                .is_new_stream(StreamId::from(bidi + 4))
                .unwrap()
        );
        assert!(
            fc[StreamType::UniDi]
                .is_new_stream(StreamId::from(unidi))
                .unwrap()
        );

        // Exceed limits
        assert_eq!(
            fc[StreamType::BiDi].is_new_stream(StreamId::from(bidi + 8)),
            Err(Error::StreamLimit)
        );
        assert_eq!(
            fc[StreamType::UniDi].is_new_stream(StreamId::from(unidi + 4)),
            Err(Error::StreamLimit)
        );

        assert_eq!(fc[StreamType::BiDi].take_stream_id(), StreamId::from(bidi));
        assert_eq!(
            fc[StreamType::BiDi].take_stream_id(),
            StreamId::from(bidi + 4)
        );
        assert_eq!(
            fc[StreamType::UniDi].take_stream_id(),
            StreamId::from(unidi)
        );

        fc[StreamType::BiDi].add_retired(1);
        fc[StreamType::BiDi].send_flowc_update();
        // consume the frame
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        fc[StreamType::BiDi].write_frames(&mut builder, &mut tokens, &mut FrameStats::default());
        assert_eq!(tokens.len(), 1);

        // Now 9 can be a new StreamId.
        assert!(
            fc[StreamType::BiDi]
                .is_new_stream(StreamId::from(bidi + 8))
                .unwrap()
        );
        assert_eq!(
            fc[StreamType::BiDi].take_stream_id(),
            StreamId::from(bidi + 8)
        );
        // 13 still exceeds limits
        assert_eq!(
            fc[StreamType::BiDi].is_new_stream(StreamId::from(bidi + 12)),
            Err(Error::StreamLimit)
        );

        fc[StreamType::UniDi].add_retired(1);
        fc[StreamType::UniDi].send_flowc_update();
        // consume the frame
        fc[StreamType::UniDi].write_frames(&mut builder, &mut tokens, &mut FrameStats::default());
        assert_eq!(tokens.len(), 2);

        // Now 7 can be a new StreamId.
        assert!(
            fc[StreamType::UniDi]
                .is_new_stream(StreamId::from(unidi + 4))
                .unwrap()
        );
        assert_eq!(
            fc[StreamType::UniDi].take_stream_id(),
            StreamId::from(unidi + 4)
        );
        // 11 exceeds limits
        assert_eq!(
            fc[StreamType::UniDi].is_new_stream(StreamId::from(unidi + 8)),
            Err(Error::StreamLimit)
        );
    }

    #[test]
    fn remote_stream_limits_new_stream_client() {
        remote_stream_limits(Role::Client, 1, 3);
    }

    #[test]
    fn remote_stream_limits_new_stream_server() {
        remote_stream_limits(Role::Server, 0, 2);
    }

    #[should_panic(expected = ".is_allowed")]
    #[test]
    fn remote_stream_limits_asserts_if_limit_exceeded() {
        let mut fc = RemoteStreamLimits::new(2, 1, Role::Client);
        assert_eq!(fc[StreamType::BiDi].take_stream_id(), StreamId::from(1));
        assert_eq!(fc[StreamType::BiDi].take_stream_id(), StreamId::from(5));
        _ = fc[StreamType::BiDi].take_stream_id();
    }

    fn local_stream_limits(role: Role, bidi: u64, unidi: u64) {
        let mut fc = LocalStreamLimits::new(role);

        fc[StreamType::BiDi].update(2);
        fc[StreamType::UniDi].update(1);

        // Add streams
        assert_eq!(
            fc.take_stream_id(StreamType::BiDi).unwrap(),
            StreamId::from(bidi)
        );
        assert_eq!(
            fc.take_stream_id(StreamType::BiDi).unwrap(),
            StreamId::from(bidi + 4)
        );
        assert_eq!(fc.take_stream_id(StreamType::BiDi), None);
        assert_eq!(
            fc.take_stream_id(StreamType::UniDi).unwrap(),
            StreamId::from(unidi)
        );
        assert_eq!(fc.take_stream_id(StreamType::UniDi), None);

        // Increase limit
        fc[StreamType::BiDi].update(3);
        fc[StreamType::UniDi].update(2);
        assert_eq!(
            fc.take_stream_id(StreamType::BiDi).unwrap(),
            StreamId::from(bidi + 8)
        );
        assert_eq!(fc.take_stream_id(StreamType::BiDi), None);
        assert_eq!(
            fc.take_stream_id(StreamType::UniDi).unwrap(),
            StreamId::from(unidi + 4)
        );
        assert_eq!(fc.take_stream_id(StreamType::UniDi), None);
    }

    #[test]
    fn local_stream_limits_new_stream_client() {
        local_stream_limits(Role::Client, 0, 2);
    }

    #[test]
    fn local_stream_limits_new_stream_server() {
        local_stream_limits(Role::Server, 1, 3);
    }

    fn write_frames(fc: &mut ReceiverFlowControl<StreamId>, rtt: Duration, now: Instant) -> usize {
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        fc.write_frames(
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
            now,
            rtt,
        );
        tokens.len()
    }

    #[test]
    fn trigger_factor() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let now = test_fixture::now();
        let mut fc =
            ReceiverFlowControl::new(StreamId::new(0), to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));

        let fraction = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) / WINDOW_UPDATE_FRACTION;

        let consumed = fc.set_consumed(fraction)?;
        fc.add_retired(consumed);
        assert_eq!(write_frames(&mut fc, rtt, now), 0);

        let consumed = fc.set_consumed(fraction + 1)?;
        assert_eq!(write_frames(&mut fc, rtt, now), 0);

        fc.add_retired(consumed);
        assert_eq!(write_frames(&mut fc, rtt, now), 1);

        Ok(())
    }

    #[test]
    fn auto_tuning_increase_no_decrease() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let mut now = test_fixture::now();
        let mut fc =
            ReceiverFlowControl::new(StreamId::new(0), to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
        let initial_max_active = fc.max_active();

        // Consume and retire multiple receive windows without increasing time.
        for _ in 1..11 {
            let consumed = fc.set_consumed(fc.next_limit())?;
            fc.add_retired(consumed);
            write_frames(&mut fc, rtt, now);
        }
        let increased_max_active = fc.max_active();

        assert!(
            initial_max_active < increased_max_active,
            "expect receive window auto-tuning to increase max_active on full utilization of high bdp connection"
        );

        // Huge idle time.
        now += Duration::from_hours(1);
        let consumed = fc.set_consumed(fc.next_limit()).unwrap();
        fc.add_retired(consumed);

        assert_eq!(write_frames(&mut fc, rtt, now), 1);
        assert_eq!(
            increased_max_active,
            fc.max_active(),
            "expect receive window auto-tuning never to decrease max_active on low utilization"
        );

        Ok(())
    }

    #[test]
    fn stream_data_blocked_triggers_auto_tuning() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let now = test_fixture::now();
        let mut fc =
            ReceiverFlowControl::new(StreamId::new(0), to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));

        // Send first window update to give auto-tuning algorithm a baseline.
        let consumed = fc.set_consumed(fc.next_limit())?;
        fc.add_retired(consumed);
        assert_eq!(write_frames(&mut fc, rtt, now), 1);

        // Use up a single byte only, i.e. way below WINDOW_UPDATE_FRACTION.
        let consumed = fc.set_consumed(fc.retired + 1)?;
        fc.add_retired(consumed);
        assert_eq!(
            write_frames(&mut fc, rtt, now),
            0,
            "expect receiver to not send window update unprompted"
        );

        // Receive STREAM_DATA_BLOCKED frame.
        fc.send_flowc_update();
        let previous_max_active = fc.max_active();
        assert_eq!(
            write_frames(&mut fc, rtt, now),
            1,
            "expect receiver to send window update"
        );
        assert!(
            previous_max_active < fc.max_active(),
            "expect receiver to auto-tune (i.e. increase) max_active"
        );

        Ok(())
    }

    #[expect(clippy::cast_precision_loss, reason = "This is test code.")]
    #[test]
    fn auto_tuning_approximates_bandwidth_delay_product() -> Res<()> {
        const DATA_FRAME_SIZE: u64 = 1_500;
        /// Allow auto-tuning algorithm to be off from actual bandwidth-delay
        /// product by up to 1KiB.
        const TOLERANCE: u64 = 1024;
        const BW_TOLERANCE: f64 = 0.6;

        test_fixture::fixture_init();

        // Run multiple iterations with randomized bandwidth and rtt.
        for _ in 0..100 {
            // Random bandwidth between 12 Mbit/s and 1 Gbit/s. Minimum 12
            // Mbit/s to ensure bdp stays above DATA_FRAME_SIZE, see `assert!`
            // below.
            let bandwidth =
                u64::from(u16::from_be_bytes(random::<2>()) % 1_000 + 12) * 1_000 * 1_000;
            // Random delay between 1 ms and 256 ms.
            let rtt_int = u64::from(random::<1>()[0]) + 1;
            let rtt = Duration::from_millis(rtt_int);
            let half_rtt = rtt / 2;
            let bdp = bandwidth * rtt_int / 1_000 / 8;
            assert!(
                DATA_FRAME_SIZE <= bdp,
                "BDP must be larger than DATA_FRAME_SIZE. Latency calculations
                in test assume it can transfer DATA_FRAME_SIZE bytes in 1 RTT."
            );

            let mut now = test_fixture::now();

            let mut send_to_recv = VecDeque::new();
            let mut recv_to_send = VecDeque::new();

            let mut last_max_active = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA);
            let mut last_max_active_changed = now;

            let mut sender_window = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA);
            let mut fc =
                ReceiverFlowControl::new(StreamId::new(0), to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));

            let mut bytes_received: u64 = 0;
            let start_time = now;

            // Track when sender can next send.
            let mut next_send_time = now;
            loop {
                // Sender receives window updates.
                if let Some((_, update)) = recv_to_send.pop_front_if(|(at, _)| *at <= now) {
                    sender_window += update;
                }

                // Sender sends data frames.
                let sender_progressed = if sender_window > 0 {
                    let to_send = min(DATA_FRAME_SIZE, sender_window);
                    sender_window -= to_send;
                    let time_to_send =
                        Duration::from_secs_f64(to_send as f64 * 8.0 / bandwidth as f64);

                    let send_start = next_send_time.max(now);
                    next_send_time = send_start + time_to_send;

                    send_to_recv.push_back((send_start + time_to_send + half_rtt, to_send));
                    true
                } else {
                    false
                };

                // Receiver receives data frames.
                let mut receiver_progressed = false;
                if let Some((_, data)) = send_to_recv.pop_front_if(|(at, _)| *at <= now) {
                    bytes_received += data;
                    let consumed = fc.set_consumed(fc.retired() + data)?;
                    fc.add_retired(consumed);

                    // Receiver sends window updates.
                    let prev_max_allowed = fc.max_allowed;
                    if write_frames(&mut fc, rtt, now) == 1 {
                        recv_to_send.push_back((now + half_rtt, fc.max_allowed - prev_max_allowed));
                        receiver_progressed = true;
                        if last_max_active < fc.max_active() {
                            last_max_active = fc.max_active();
                            last_max_active_changed = now;
                        }
                    }
                }

                // When idle, travel in (simulated) time.
                if !sender_progressed && !receiver_progressed {
                    now = [recv_to_send.front(), send_to_recv.front()]
                        .into_iter()
                        .flatten()
                        .map(|(at, _)| *at)
                        .min()
                        .expect("both are None");
                }

                // Consider auto-tuning done once receive window hasn't changed for 8 RTT.
                // A large amount to allow the observed bandwidth average to stabilize.
                if now.duration_since(last_max_active_changed) > 8 * rtt {
                    break;
                }
            }

            // See comment in [`ReceiverFlowControl::auto_tune_inner`] for an
            // explanation of the effective window.
            let effective_window =
                (fc.max_active() * (WINDOW_UPDATE_FRACTION - 1)) / WINDOW_UPDATE_FRACTION;
            let at_max_stream_data = fc.max_active() == MAX_LOCAL_MAX_STREAM_DATA;

            let observed_bw =
                (8 * bytes_received) as f64 / now.duration_since(start_time).as_secs_f64();
            let summary = format!(
                "Got receive window of {} KiB (effectively {} KiB) on connection with observed bandwidth {} MBit/s. Expected: bandwidth {} MBit/s ({bandwidth} Bit/s), rtt {rtt:?}, bdp {} KiB.",
                fc.max_active() / 1024,
                effective_window / 1024,
                observed_bw / 1_000.0 / 1_000.0,
                bandwidth / 1_000 / 1_000,
                bdp / 1024,
            );

            assert!(
                effective_window + TOLERANCE >= bdp || at_max_stream_data,
                "{summary} Receive window is smaller than the bdp."
            );

            assert!(
                effective_window - TOLERANCE <= bdp
                    || fc.max_active == to_u64(INITIAL_LOCAL_MAX_STREAM_DATA),
                "{summary} Receive window is larger than the bdp."
            );

            assert!(
                (bandwidth as f64) * BW_TOLERANCE <= observed_bw || at_max_stream_data,
                "{summary} Observed bandwidth is smaller than the link rate."
            );

            debug!("{summary}");
        }

        Ok(())
    }

    #[test]
    fn connection_flow_control_initial_window() {
        let max_data = ConnectionParameters::default().get_max_data();
        assert_eq!(max_data, INITIAL_LOCAL_MAX_DATA);
    }

    #[test]
    fn connection_flow_control_auto_tune() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let now = test_fixture::now();
        let initial_window = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA * 16);
        let mut fc = ReceiverFlowControl::new((), initial_window);
        let initial_max_active = fc.max_active();

        // Helper to write frames
        let write_conn_frames = |fc: &mut ReceiverFlowControl<()>, now: Instant| {
            let mut builder =
                packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
            let mut tokens = recovery::Tokens::new();
            fc.write_frames(
                &mut builder,
                &mut tokens,
                &mut FrameStats::default(),
                now,
                rtt,
            );
            tokens.len()
        };

        // Consume and retire multiple windows to trigger auto-tuning.
        // Each iteration: consume a full window, retire it, send update.
        for _ in 1..11 {
            let to_consume = fc.max_active();
            fc.consume(to_consume)?;
            fc.add_retired(to_consume);
            write_conn_frames(&mut fc, now);
        }
        let increased_max_active = fc.max_active();

        assert!(
            initial_max_active < increased_max_active,
            "expect connection-level receive window auto-tuning to increase max_active on full utilization"
        );

        Ok(())
    }

    #[test]
    fn connection_flow_control_respects_max_window() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let now = test_fixture::now();
        let initial_window = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA * 16);
        let mut fc = ReceiverFlowControl::new((), initial_window);

        // Helper to write frames
        let write_conn_frames = |fc: &mut ReceiverFlowControl<()>| {
            let mut builder =
                packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
            let mut tokens = recovery::Tokens::new();
            fc.write_frames(
                &mut builder,
                &mut tokens,
                &mut FrameStats::default(),
                now,
                rtt,
            );
            tokens.len()
        };

        // Consume and retire many full windows to push window to the limit.
        // Keep consuming without advancing time to create maximum pressure.
        for _ in 0..1000 {
            let prev_max = fc.max_active();
            let to_consume = fc.max_active();
            fc.consume(to_consume)?;
            fc.add_retired(to_consume);
            write_conn_frames(&mut fc);

            // Stop if we've reached the maximum and it's not growing anymore
            if fc.max_active() == MAX_LOCAL_MAX_DATA && fc.max_active() == prev_max {
                debug!(
                    "Reached and stabilized at max window: {} MiB",
                    fc.max_active() / 1024 / 1024
                );
                break;
            }
        }

        assert_eq!(
            fc.max_active(),
            MAX_LOCAL_MAX_DATA,
            "expect connection-level receive window to cap at MAX_LOCAL_MAX_DATA (100 MiB), got {} MiB",
            fc.max_active() / 1024 / 1024
        );

        debug!(
            "Connection flow control window reached max: {} MiB",
            fc.max_active() / 1024 / 1024
        );

        Ok(())
    }

    #[test]
    fn auto_tune_subject_display() {
        assert_eq!(AutoTuneSubject::Connection.to_string(), "connection");
        assert_eq!(
            AutoTuneSubject::Stream(StreamId::new(4)).to_string(),
            "stream 4"
        );
    }

    #[test]
    fn update_same_limit_returns_none() {
        // `update` returns None when the new limit equals the current limit.
        let mut fc = SenderFlowControl::new((), 10);
        assert!(fc.update(10).is_none()); // Equal — no change.
        assert!(fc.update(11).is_some()); // Strictly greater — update.
    }

    #[test]
    fn set_max_active_equal_does_not_set_frame_pending() {
        // `set_max_active` does not mark frame pending when the value is unchanged.
        let mut fc = ReceiverFlowControl::new(StreamId::new(0), 100);
        fc.set_max_active(100); // Same value — should not set frame_pending.
        assert!(!fc.frame_needed());
        fc.set_max_active(101); // Increase — should set frame_pending.
        assert!(fc.frame_needed());
    }

    #[test]
    fn retire_no_op_when_not_increasing() {
        // `retire` is a no-op when the new value does not exceed the current retired count.
        // Retire 80/100 bytes to exceed the 75% threshold and trigger a flow control update.
        let mut fc = ReceiverFlowControl::new(StreamId::new(0), 100);
        fc.set_consumed(80).unwrap();
        fc.retire(80); // 20 bytes unused < 25 (25% threshold) → triggers update
        assert!(fc.frame_needed());
        fc.frame_sent(fc.next_limit()); // mark frame as sent, clearing pending
        fc.retire(80); // same value — no-op
        assert!(!fc.frame_needed());
    }

    #[test]
    fn add_retired_zero_does_not_trigger_update() {
        // `add_retired(0)` must not trigger a flow control update when no data was retired.
        let mut fc = ReceiverFlowControl::new(StreamType::UniDi, 100);
        fc.add_retired(0); // count == 0: no update.
        assert!(!fc.frame_needed());
        fc.add_retired(1); // count > 0: retired+max_active > max_allowed → triggers update.
        assert!(fc.frame_needed());
    }

    #[test]
    fn max_streams_frame_capped_at_2_pow_60() {
        let mut fc = ReceiverFlowControl::new(StreamType::UniDi, MAX_STREAMS_LIMIT);
        fc.add_retired(1);
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        fc.write_frames(&mut builder, &mut tokens, &mut FrameStats::default());
        assert_eq!(tokens.len(), 1);
        assert_eq!(fc.max_allowed, MAX_STREAMS_LIMIT);
    }

    #[test]
    fn auto_tune_never_decreases_large_manually_set_max_active() -> Res<()> {
        let rtt = Duration::from_millis(40);
        let now = test_fixture::now();
        let mut fc = ReceiverFlowControl::new(
            StreamId::new(0),
            // Very large manually configured window beyond the maximum auto-tuned window.
            MAX_LOCAL_MAX_STREAM_DATA * 10,
        );
        let initial_max_active = fc.max_active();

        // Consume and retire multiple windows to trigger auto-tuning.
        // Each iteration: consume a full window, retire it, send update.
        for _ in 1..11 {
            let consumed = fc.set_consumed(fc.next_limit())?;
            fc.add_retired(consumed);
            write_frames(&mut fc, rtt, now);
        }
        let increased_max_active = fc.max_active();

        assert!(
            initial_max_active == increased_max_active,
            "expect receive window auto-tuning to not decrease max_active below manually set initial value."
        );

        Ok(())
    }
}
