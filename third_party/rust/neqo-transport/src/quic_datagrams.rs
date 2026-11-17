// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Outbound QUIC datagram queueing and backpressure: see
//! [`crate::datagram_queue`] for the per-session queue's own byte-budget and
//! max-buffered contract. [`QuicDatagrams`] holds one such queue per
//! session and round-robins between them at packet-build time.

// https://datatracker.ietf.org/doc/html/draft-ietf-quic-datagram

use std::{
    cmp::min,
    collections::BTreeMap,
    num::NonZeroUsize,
    time::{Duration, Instant},
};

use neqo_common::{Buffer, Encoder, to_u64};
use tracing::debug;

use crate::{
    ConnectionEvents, Error, MAX_DATAGRAM_FRAME_SIZE, Res, Stats,
    datagram_queue::{
        DatagramId, DatagramQueue, DatagramQueueCapacity, DatagramQueueOutcome, DroppedDatagrams,
        QueuedDatagram, default_max_age,
    },
    events::OutgoingDatagramOutcome,
    frame::{FrameEncoder as _, FrameType},
    packet, recovery,
    stream_id::StreamId,
    streams::{SendGroupId, SendOrder},
};

/// Length of a [`FrameType::Datagram`] or [`FrameType::DatagramWithLen`] in
/// QUIC varint encoding.
pub const DATAGRAM_FRAME_TYPE_VARINT_LEN: usize = 1;
const _: () = {
    assert!(Encoder::varint_len(FrameType::Datagram as u64) == DATAGRAM_FRAME_TYPE_VARINT_LEN);
    assert!(
        Encoder::varint_len(FrameType::DatagramWithLen as u64) == DATAGRAM_FRAME_TYPE_VARINT_LEN
    );
};

#[derive(Debug, Clone, Copy)]
pub enum DatagramTracking {
    None,
    Id(u64),
}

impl From<Option<u64>> for DatagramTracking {
    fn from(v: Option<u64>) -> Self {
        v.map_or(Self::None, Self::Id)
    }
}

/// What [`QuicDatagrams::take_from_session_queue`]'s caller does with the
/// datagram it takes.
#[derive(Clone, Copy)]
enum Disposition {
    /// Handed to the packet builder: counts as sent.
    Send,
    /// Dropped for not fitting an empty full-MTU packet.
    DropTooBig,
}

pub struct QuicDatagrams {
    /// The max size of a datagram that would be acceptable.
    local_datagram_size: u64,
    /// The max size of a datagram that would be acceptable by the peer.
    remote_datagram_size: u64,
    /// Per-session outgoing-datagram queues (byte budget, max-buffered,
    /// send-group/send-order priority, max-age), keyed by the session's control stream ID.
    ///
    /// An entry is created lazily on first use and only removed by
    /// [`Self::drop_session_datagrams`] (session teardown) — *not* whenever
    /// it happens to drain empty, since a temporarily empty queue can still
    /// carry an application-set max-buffered/max-age that must survive
    /// until the session closes.
    ///
    /// Sized for one session per connection, which is what WebTransport in
    /// Firefox does today: [`Self::write_frames`] looks a session up twice
    /// per datagram and rescans from the cursor each time, and
    /// [`Self::next_datagram_expiry`] walks every session's buckets on every
    /// `next_delay`. With one entry all of that is a handful of operations.
    /// If several sessions per connection become the norm, fold the lookups
    /// into one `get_mut` and cache each queue's oldest timestamp.
    queues: BTreeMap<StreamId, DatagramQueue>,
    /// Session at which the next cross-session round-robin round starts.
    /// See [`Self::next_active_session_from`].
    queue_rr_next: StreamId,
    conn_events: ConnectionEvents,
}

impl QuicDatagrams {
    pub const fn new(local_datagram_size: u64, conn_events: ConnectionEvents) -> Self {
        Self {
            local_datagram_size,
            remote_datagram_size: 0,
            queues: BTreeMap::new(),
            queue_rr_next: StreamId::new(0),
            conn_events,
        }
    }

    pub const fn remote_datagram_size(&self) -> u64 {
        self.remote_datagram_size
    }

    pub fn set_remote_datagram_size(&mut self, v: u64) {
        self.remote_datagram_size = min(v, MAX_DATAGRAM_FRAME_SIZE);
    }

    /// Encode a single datagram payload that is already known to fit
    /// (`len + DATAGRAM_FRAME_TYPE_VARINT_LEN <= builder.remaining()`).
    fn encode_datagram<B: Buffer>(
        data: &[u8],
        tracking: DatagramTracking,
        frame_type: FrameType,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut Stats,
    ) {
        if frame_type == FrameType::DatagramWithLen {
            builder.encode_frame(frame_type, |b| {
                b.encode_vvec(data);
            });
        } else {
            builder.pad_before(DATAGRAM_FRAME_TYPE_VARINT_LEN + data.len());
            builder.encode_frame(frame_type, |b| {
                b.encode(data);
            });
            builder.mark_full();
        }
        debug_assert!(builder.len() <= builder.limit());
        stats.frame_tx.datagram += 1;
        tokens.push(recovery::Token::Datagram(tracking));
    }

    /// This function tries to write a datagram frame into a packet. If the
    /// frame does not fit into the packet, the datagram will be dropped and a
    /// [`OutgoingDatagramOutcome::DroppedTooBig`] event will be posted.
    ///
    /// Round-robins across the per-session [`DatagramQueue`]s in `queues`,
    /// one datagram at a time, expiry-and-priority-ordered within each,
    /// stopping without consuming anything further the moment something
    /// does not fit into an otherwise non-empty packet: that datagram stays
    /// queued for the next `process_output` call rather than being
    /// round-tripped out and back in.
    ///
    /// "Too big" means it would not fit an *empty full-MTU* packet:
    /// `full_mtu` says whether this packet is one (rather than cut down by
    /// the congestion window or amplification limit), and
    /// `builder.packet_empty()` whether nothing else took room first.
    /// Anything that fails to fit for any other reason stays queued.
    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut Stats,
        full_mtu: bool,
    ) {
        loop {
            let Some(session) = self.next_active_session_from(self.queue_rr_next) else {
                return;
            };
            let queue = self
                .queues
                .get(&session)
                .expect("next_active_session_from only returns known sessions");
            let Some(len) = queue.peek_next_len() else {
                unreachable!("next_active_session_from only returns non-empty sessions")
            };
            let frame_len = len + DATAGRAM_FRAME_TYPE_VARINT_LEN;

            if frame_len > builder.remaining() && !(full_mtu && builder.packet_empty()) {
                // Leave it queued; try again on a later, emptier packet. This
                // stops at the first session whose head does not fit rather
                // than trying the others: the cursor stays on it, so it goes
                // first next time, and a datagram that fits is rarely more
                // than a packet away. Skipping ahead would trade that
                // fairness for a fuller packet.
                return;
            }

            if to_u64(frame_len) > min(to_u64(builder.remaining()), self.remote_datagram_size) {
                let dgram = self
                    .take_from_session_queue(session, Disposition::DropTooBig)
                    .expect("just peeked Some above, with no intervening mutation");
                debug!(
                    "QUIC datagram ({}) exceeds remote limit ({}) or does not fit MTU.",
                    dgram.data.len(),
                    self.remote_datagram_size
                );
                self.conn_events
                    .datagram_outcome(&dgram.id.into(), OutgoingDatagramOutcome::DroppedTooBig);
                stats.datagram_tx.dropped_too_big += 1;
                continue;
            }

            let dgram = self
                .take_from_session_queue(session, Disposition::Send)
                .expect("just peeked Some above, with no intervening mutation");

            let frame_len_with_len = frame_len + Encoder::varint_len(to_u64(len));

            // Include a length if there is space for another frame after this one
            // and adding a length won't exceed the datagram frame size limit.
            // We accept datagrams based on the encoded size of a frame without a length,
            // so the varint length could cause the limit to be exceeded.
            let frame_type = if to_u64(frame_len_with_len) <= self.remote_datagram_size
                && frame_len_with_len + packet::Builder::MINIMUM_FRAME_SIZE <= builder.remaining()
            {
                FrameType::DatagramWithLen
            } else {
                FrameType::Datagram
            };

            Self::encode_datagram(
                &dgram.data,
                dgram.id.into(),
                frame_type,
                builder,
                tokens,
                stats,
            );
        }
    }

    /// Starting at `start` and wrapping around, the first session with a
    /// non-empty queue. Unlike the inner per-session `DatagramQueue`'s own
    /// group map, `queues` keeps empty entries around (see its doc comment),
    /// so this filters emptiness explicitly rather than relying on every
    /// entry present being non-empty.
    fn next_active_session_from(&self, start: StreamId) -> Option<StreamId> {
        self.queues
            .range(start..)
            .chain(self.queues.range(..start))
            .find(|(_, q)| !q.is_empty())
            .map(|(id, _)| *id)
    }

    /// Take the next datagram off `session`'s queue, resuming a blocked
    /// sender and advancing the round-robin cursor past it; `disposition`
    /// says which count it goes toward.  Returns `None` if `session`'s queue has nothing
    /// to take; callers that already know it does (e.g. via a preceding
    /// [`DatagramQueue::peek_next_len`] on the same session, with no other
    /// mutation in between) can `expect` it.
    #[expect(
        clippy::unwrap_in_result,
        reason = "the internal expect() below guards an invariant unrelated to this fn's own \
                  None case: session is always in queues, since next_active_session_from only \
                  ever returns sessions it found there"
    )]
    fn take_from_session_queue(
        &mut self,
        session: StreamId,
        disposition: Disposition,
    ) -> Option<QueuedDatagram> {
        let queue = self
            .queues
            .get_mut(&session)
            .expect("next_active_session_from only returns known sessions");
        let dgram = queue.take_next()?;
        match disposition {
            Disposition::Send => queue.record_sent(),
            Disposition::DropTooBig => queue.record_too_big(),
        }
        if queue.resume_if_unblocked() {
            self.conn_events.datagram_space_available(session);
        }
        self.queue_rr_next = StreamId::new(session.as_u64().wrapping_add(1));
        Some(dgram)
    }

    /// Get or lazily create `session`'s queue.
    fn queue_mut(&mut self, session: StreamId) -> &mut DatagramQueue {
        self.queues.entry(session).or_default()
    }

    /// Enqueue a datagram on `session`'s outgoing queue. See
    /// [`DatagramQueue::enqueue`]. Expires anything already past its
    /// max-age first, so a stale entry cannot hold the max-buffered limit or the byte
    /// budget against the new one; `min_rtt` supplies the default max-age.
    ///
    /// # Errors
    ///
    /// Returns `TooMuchData` if the frame size would exceed the allowed remote
    /// max datagram frame size.
    #[expect(
        clippy::too_many_arguments,
        reason = "Connection::enqueue_datagram's parameters plus the RTT the default max-age derives from"
    )]
    pub fn enqueue_datagram(
        &mut self,
        session: StreamId,
        data: Vec<u8>,
        id: Option<DatagramId>,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
        min_rtt: Duration,
    ) -> Res<DatagramQueueOutcome> {
        let frame_len = data.len().saturating_add(DATAGRAM_FRAME_TYPE_VARINT_LEN);
        if to_u64(frame_len) > self.remote_datagram_size {
            debug!(
                "QUIC DATAGRAM frame exceeds remote limit, dropping it, frame size {frame_len}, remote DATAGRAM frame size limit {}.",
                self.remote_datagram_size
            );
            return Err(Error::TooMuchData);
        }
        let queue = self.queues.entry(session).or_default();
        Self::expire_queue(
            session,
            queue,
            &self.conn_events,
            now,
            default_max_age(min_rtt),
        );
        Ok(queue.enqueue(data, id, now, send_group_id, send_order))
    }

    /// See [`DatagramQueue::set_max_buffered_datagrams`].
    pub fn set_max_buffered_datagrams(&mut self, session: StreamId, mark: Option<NonZeroUsize>) {
        let queue = self.queue_mut(session);
        queue.set_max_buffered_datagrams(mark);
        if queue.resume_if_unblocked() {
            self.conn_events.datagram_space_available(session);
        }
    }

    /// See [`DatagramQueue::set_max_age`]. `min_rtt` is the connection's
    /// current estimate, from which the default max-age is derived; likewise
    /// for the other methods here that take it.
    pub fn set_datagram_max_age(
        &mut self,
        session: StreamId,
        max_age: Option<Duration>,
        now: Instant,
        min_rtt: Duration,
    ) {
        let queue = self.queue_mut(session);
        queue.set_max_age(max_age, now, default_max_age(min_rtt));
        if queue.resume_if_unblocked() {
            self.conn_events.datagram_space_available(session);
        }
    }

    /// See [`DatagramQueue::capacity`].
    #[must_use]
    pub fn datagram_queue_capacity(&self, session: StreamId) -> DatagramQueueCapacity {
        self.queues.get(&session).map_or_else(
            || DatagramQueue::default().capacity(),
            DatagramQueue::capacity,
        )
    }

    /// See [`DatagramQueue::take_sent_count`].
    pub fn take_session_sent_count(&mut self, session: StreamId) -> u64 {
        self.queues
            .get_mut(&session)
            .map_or(0, DatagramQueue::take_sent_count)
    }

    /// See [`DatagramQueue::take_too_big_count`].
    pub fn take_session_too_big_count(&mut self, session: StreamId) -> u64 {
        self.queues
            .get_mut(&session)
            .map_or(0, DatagramQueue::take_too_big_count)
    }

    /// Remove `session`'s queue, e.g. because the session is closing, with
    /// every datagram still on it and the counts not yet taken, so the
    /// caller cannot lose them by dropping first.
    pub fn drop_session_datagrams(&mut self, session: StreamId) -> DroppedDatagrams {
        self.queues
            .remove(&session)
            .map_or_else(DroppedDatagrams::default, |mut q| DroppedDatagrams {
                sent: q.take_sent_count(),
                expired: q.take_expired_count(),
                too_big: q.take_too_big_count(),
                queued: to_u64(q.take_all().count()),
            })
    }

    /// The instant at which the oldest datagram queued on any session
    /// crosses its effective max-age, if any session has one queued.
    #[must_use]
    pub fn next_datagram_expiry(&self, min_rtt: Duration) -> Option<Instant> {
        let default_max_age = default_max_age(min_rtt);
        self.queues
            .values()
            .filter_map(|q| q.next_expiry(default_max_age))
            .min()
    }

    /// Expire stale datagrams on every session's queue. Not gated on there
    /// being anything else to do: expiry is not a send, so it must happen
    /// even when nothing else is scheduled (see [`DatagramQueue::expire`]'s
    /// doc comment). Called by `Connection::process_timer` on its own
    /// schedule.
    pub fn expire_datagrams(&mut self, now: Instant, min_rtt: Duration) {
        let default_max_age = default_max_age(min_rtt);
        for (session, queue) in &mut self.queues {
            Self::expire_queue(*session, queue, &self.conn_events, now, default_max_age);
        }
    }

    /// [`Self::expire_datagrams`] for a single session, returning how many of
    /// its datagrams have expired since the last call: those shed here, plus
    /// any the connection-wide sweep or an enqueue shed in the meantime.  The
    /// count lives on the queue (see [`DatagramQueue::take_expired_count`]),
    /// so which sweep ran first does not matter, and a caller that counts
    /// per session never picks up another session's.
    pub fn expire_session_datagrams(
        &mut self,
        session: StreamId,
        now: Instant,
        min_rtt: Duration,
    ) -> u64 {
        self.queues.get_mut(&session).map_or(0, |queue| {
            Self::expire_queue(
                session,
                queue,
                &self.conn_events,
                now,
                default_max_age(min_rtt),
            );
            queue.take_expired_count()
        })
    }

    /// See [`DatagramQueue::take_expired_count`]: the drain half of
    /// [`Self::expire_session_datagrams`], for a caller that has no `now`
    /// to sweep with (e.g. at session teardown).
    pub fn take_session_expired_count(&mut self, session: StreamId) -> u64 {
        self.queues
            .get_mut(&session)
            .map_or(0, DatagramQueue::take_expired_count)
    }

    /// Expire `queue`'s stale entries and, if that unblocks it, fire the
    /// resume event. Shared by [`Self::expire_datagrams`] (every session),
    /// [`Self::expire_session_datagrams`] (a single one) and
    /// [`Self::enqueue_datagram`].
    fn expire_queue(
        session: StreamId,
        queue: &mut DatagramQueue,
        conn_events: &ConnectionEvents,
        now: Instant,
        default_max_age: Duration,
    ) {
        _ = queue.expire(now, default_max_age);
        if queue.resume_if_unblocked() {
            conn_events.datagram_space_available(session);
        }
    }

    pub fn handle_datagram(&self, data: &[u8], len_field_len: Option<NonZeroUsize>) -> Res<()> {
        let frame_len =
            data.len() + DATAGRAM_FRAME_TYPE_VARINT_LEN + len_field_len.map_or(0, Into::into);

        // Because `DATAGRAM_FRAME_TYPE_VARINT_LEN`is > 0, this implicitly also checks that
        // `local_datagram_size` is not 0, i.e., not no DATAGRAM frame support
        // (RFC 9221, Section 3).
        if self.local_datagram_size < to_u64(frame_len) {
            return Err(Error::ProtocolViolation);
        }
        self.conn_events.add_datagram(data);
        Ok(())
    }
}
