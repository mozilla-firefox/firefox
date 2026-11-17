// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Building a stream of ordered bytes to give the application from a series of
// incoming STREAM frames.

use std::{
    cell::RefCell,
    cmp::{max, min},
    collections::{BTreeMap, btree_map::Entry},
    fmt::Debug,
    mem,
    rc::{Rc, Weak},
    time::{Duration, Instant},
};

use neqo_common::{Buffer, Role, expect_usize, to_u64};
use smallvec::SmallVec;
use strum::Display;
use tracing::{trace, warn};

use crate::{
    AppError, Error, Res,
    events::ConnectionEvents,
    fc::ReceiverFlowControl,
    frame::FrameType,
    packet,
    recovery::{self, StreamRecoveryToken},
    send_stream::SendStreams,
    stats::FrameStats,
    stream_id::StreamId,
};

#[derive(Debug)]
pub struct RecvStreams {
    streams: BTreeMap<StreamId, RecvStream>,
    keep_alive: Weak<()>,
    /// Set when any stream has ended; cleared by `remove_ended`.
    has_ended: bool,
    role: Role,
}

impl RecvStreams {
    #[must_use]
    pub const fn new(role: Role) -> Self {
        Self {
            streams: BTreeMap::new(),
            keep_alive: Weak::new(),
            has_ended: false,
            role,
        }
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
        now: Instant,
        rtt: Duration,
    ) {
        for stream in self.streams.values_mut() {
            stream.write_frame(builder, tokens, stats, now, rtt);
            if builder.is_full() {
                return;
            }
        }
    }

    pub fn insert(&mut self, id: StreamId, stream: RecvStream) {
        self.streams.insert(id, stream);
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn get_mut(&mut self, id: StreamId) -> Res<&mut RecvStream> {
        self.streams.get_mut(&id).ok_or(Error::InvalidStreamId)
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn keep_alive(&mut self, id: StreamId, k: bool) -> Res<()> {
        let self_ka = &mut self.keep_alive;
        let s = self.streams.get_mut(&id).ok_or(Error::InvalidStreamId)?;
        s.keep_alive = k.then(|| {
            self_ka.upgrade().unwrap_or_else(|| {
                let r = Rc::new(());
                *self_ka = Rc::downgrade(&r);
                r
            })
        });
        Ok(())
    }

    #[must_use]
    pub fn need_keep_alive(&self) -> bool {
        self.keep_alive.strong_count() > 0
    }

    pub fn clear(&mut self) {
        self.streams.clear();
        self.has_ended = false;
    }

    pub(crate) const fn set_ended(&mut self, ended: bool) {
        self.has_ended |= ended;
    }

    /// Read from a stream, noting when it ends.
    ///
    /// # Errors
    /// When the stream does not exist or has no more data.
    pub fn read(&mut self, stream_id: StreamId, data: &mut [u8]) -> Res<(usize, bool)> {
        let s = self.get_mut(stream_id)?;
        let (n, fin) = s.read(data)?;
        // A read can end the stream cleanly (`fin`) or, for a reliable reset, by draining the
        // reliable prefix and reaching `ResetRecvd`; flag both.
        let ended = s.is_ended();
        self.set_ended(ended);
        Ok((n, fin))
    }

    /// Stop sending on a stream, noting when it ends.
    ///
    /// # Errors
    /// When the stream does not exist.
    pub fn stop_sending(&mut self, stream_id: StreamId, err: AppError) -> Res<()> {
        let ended = self.get_mut(stream_id)?.stop_sending(err);
        self.set_ended(ended);
        Ok(())
    }

    /// Handle a `RESET_STREAM` or `RESET_STREAM_AT` for a stream, noting if it ended. A plain
    /// `RESET_STREAM` is a reliable reset with `reliable_size == 0`.
    ///
    /// # Errors
    /// When flow control or the frame encoding is violated (see [`RecvStream::reset`]).
    pub fn reset(
        &mut self,
        stream_id: StreamId,
        application_error_code: AppError,
        final_size: u64,
        reliable_size: u64,
    ) -> Res<()> {
        if let Ok(rs) = self.get_mut(stream_id) {
            let ended = rs.reset(application_error_code, final_size, reliable_size)?;
            self.set_ended(ended);
        }
        Ok(())
    }

    /// Note whether a stop-sending ack ended the stream.
    pub fn stop_sending_acked(&mut self, stream_id: StreamId) {
        if let Ok(rs) = self.get_mut(stream_id) {
            let ended = rs.stop_sending_acked();
            self.set_ended(ended);
        }
    }

    pub fn remove_ended(&mut self, send_streams: &SendStreams) -> (u64, u64) {
        if !self.has_ended {
            return (0, 0);
        }
        self.has_ended = false;
        // Note: retained ended bidi streams (send counterpart alive) will be re-flagged
        // when their send side is removed via `cleanup_closed_streams`.
        let mut removed_bidi = 0;
        let mut removed_uni = 0;
        self.streams.retain(|id, s| {
            let dead = s.is_ended() && (id.is_uni() || !send_streams.exists(*id));
            if dead && id.is_remote_initiated(self.role) {
                if id.is_bidi() {
                    removed_bidi += 1;
                } else {
                    removed_uni += 1;
                }
            }
            !dead
        });

        (removed_bidi, removed_uni)
    }
}

/// Holds data not yet read by application. Orders and dedupes data ranges
/// from incoming STREAM frames.
#[derive(Debug, Default)]
pub struct RxStreamOrderer {
    data_ranges: BTreeMap<u64, Vec<u8>>, // (start_offset, data)
    retired: u64,                        // Number of bytes the application has read
    received: u64,                       // The number of bytes stored in `data_ranges`
    /// Exclusive end offset of the rightmost received range (the end of the
    /// last entry in `data_ranges`, or `retired` if the map is empty).
    end: u64,
}

impl RxStreamOrderer {
    /// Target maximum length of a buffered range before a new entry is created instead of
    /// extending. This is a target, not a hard maximum: a single frame larger than this value is
    /// still buffered whole. Because the extend check tests the *existing* entry length, an
    /// extended chunk can end up slightly larger than `RANGE_TARGET` (by up to one frame's worth).
    const RANGE_TARGET: usize = 4096;

    /// Maximum number of gaps allowed in the data a stream holds.
    pub(crate) const MAX_GAPS: usize = 4096;

    /// Limit the entries a stream can be made to hold, which flow control does not.
    ///
    /// Adjacent data is coalesced up to `RANGE_TARGET`, so the data itself accounts for one entry
    /// per `RANGE_TARGET` bytes of the span from `retired` to `end`. Every entry in excess of that
    /// exists because of a gap, so capping the excess at `MAX_GAPS` bounds the entries, no matter
    /// how the peer fragments what it sends.
    ///
    /// The span is the input rather than a count of buffered bytes because flow control bounds it.
    /// `received` will not do: it counts overlapping data once per copy, so a peer could grow the
    /// allowance for as long as the connection lasts.
    fn check_gap_limit(&self) -> Res<()> {
        let span = self.end - self.retired;
        let needed = expect_usize(span / to_u64(Self::RANGE_TARGET));
        if self.data_ranges.len() > Self::MAX_GAPS + needed {
            warn!("Too many gaps in the reassembly buffer, closing connection");
            return Err(Error::ProtocolViolation);
        }
        Ok(())
    }

    /// Add a range, which is the only thing that can take the entry count over the limit.
    fn insert_range(&mut self, start: u64, data: Vec<u8>) -> Res<()> {
        self.data_ranges.insert(start, data);
        self.check_gap_limit()
    }

    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Whether every byte of `[offset, offset + len)` has already been received or read.
    /// Call this before [`Self::inbound_frame`], which wipes the ability to determine this.
    #[must_use]
    pub(crate) fn already_received(&self, offset: u64, len: usize) -> bool {
        let end = offset + to_u64(len);
        if end > self.end {
            return false;
        }
        let mut covered = max(offset, self.retired);
        if covered >= end {
            return true;
        }
        // Ranges never overlap, so only the last one starting at or below `covered` can extend it.
        let first = self
            .data_ranges
            .range(..=covered)
            .next_back()
            .map_or(covered, |(&start, _)| start);
        for (&start, data) in self.data_ranges.range(first..end) {
            if start > covered {
                return false;
            }
            covered = start + to_u64(data.len());
        }
        covered >= end
    }

    /// Process an incoming stream frame off the wire. This may result in data
    /// being available to upper layers if frame is not out of order (ooo) or
    /// if the frame fills a gap.
    /// # Errors
    /// `ProtocolViolation` if this leaves the stream holding data with more than `MAX_GAPS` gaps.
    /// # Panics
    /// Only when `u64` values cannot be converted to `usize`, which only
    /// happens on 32-bit machines that hold far too much data at the same time.
    #[expect(clippy::too_many_lines, reason = "Yeah, but splitting it reads worse.")]
    pub fn inbound_frame(&mut self, mut new_start: u64, mut new_data: &[u8]) -> Res<()> {
        trace!("Inbound data offset={new_start} len={}", new_data.len());

        // Get entry before where new entry would go, so we can see if we already
        // have the new bytes.
        // Avoid copies and duplicated data.
        let new_end = new_start + to_u64(new_data.len());

        if new_end <= self.retired {
            // Range already read by application, this frame is very late and unneeded.
            return Ok(());
        }

        if new_start < self.retired {
            // Conversion is safe because this difference is bounded by new_data.len().
            new_data = &new_data[expect_usize(self.retired - new_start)..];
            new_start = self.retired;
        }

        if new_data.is_empty() {
            // No data to insert
            return Ok(());
        }

        // Common case: new_start >= end
        if new_start >= self.end {
            debug_assert_eq!(
                self.end,
                self.data_ranges
                    .last_key_value()
                    .map_or(self.retired, |(&k, v)| k + to_u64(v.len())),
                "end must equal the end of the last range, or retired if empty"
            );
            self.received += to_u64(new_data.len());
            // Adjacent: extend the last entry to avoid a BTreeMap insert, if small enough.
            // Checks existing length, so the stored chunk may grow slightly past RANGE_TARGET
            // (by up to one frame). Gap (new_start > end): falls through to insert.
            let adjacent = new_start == self.end;
            // new_end > new_start >= end, so direct assignment is correct.
            self.end = new_end;
            return if adjacent
                && let Some(mut e) = self
                    .data_ranges
                    .last_entry()
                    .filter(|e| e.get().len() < Self::RANGE_TARGET)
            {
                e.get_mut().extend_from_slice(new_data);
                Ok(())
            } else {
                self.insert_range(new_start, new_data.to_vec())
            };
        }

        // Retransmission/overlap: new_start < end
        let extend = if let Some((&prev_start, prev_vec)) =
            self.data_ranges.range_mut(..=new_start).next_back()
        {
            let prev_len = prev_vec.len();
            let prev_end = prev_start + to_u64(prev_len);
            if new_end > prev_end {
                // PPPPPP    ->  PPPPPP
                //   NNNNNN            NN
                // NNNNNNNN            NN
                // Add a range containing only new data
                let overlap = prev_end.saturating_sub(new_start);
                trace!("New frame {new_start}-{new_end} received, overlap: {overlap}");
                new_start += overlap;
                // This conversion is guaranteed to work because the overlap cannot exceed
                // the size of the new data, which has to fit in usize.
                new_data = &new_data[expect_usize(overlap)..];
                // If it is small enough, extend the previous buffer.
                // Checks existing length, so the chunk may grow slightly past RANGE_TARGET (by up
                // to one frame). This can't always extend, because otherwise the
                // buffer could end up growing indefinitely without being released.
                (prev_len < Self::RANGE_TARGET && prev_end == new_start)
                    .then_some((prev_start, prev_len))
            } else {
                // PPPPPP    ->  PPPPPP
                //   NNNN
                // NNNN
                // Do nothing
                trace!("Dropping frame with already-received range {new_start}-{new_end}");
                return Ok(());
            }
        } else {
            trace!("New frame {new_start}-{new_end} received");
            None
        };

        let mut to_add = new_data;
        if self
            .data_ranges
            .last_entry()
            .is_some_and(|e| *e.key() >= new_start)
        {
            // Is this at the end (common case)?  If so, nothing to do in this block
            // Common case:
            //  PPPPPP        -> PPPPPP
            //        NNNNNNN          NNNNNNN
            // or
            //  PPPPPP             -> PPPPPP
            //             NNNNNNN               NNNNNNN
            //
            // Not the common case, handle possible overlap with next entries
            //  PPPPPP       AAA      -> PPPPPP
            //        NNNNNNN                  NNNNNNN
            // or
            //  PPPPPP     AAAA      -> PPPPPP     AAAA
            //        NNNNNNN                 NNNNN
            // or (this is where to_remove is used)
            //  PPPPPP    AA       -> PPPPPP
            //        NNNNNNN               NNNNNNN

            let mut to_remove = SmallVec::<[_; 8]>::new();

            for (&next_start, next_data) in self.data_ranges.range_mut(new_start..) {
                let next_end = next_start + to_u64(next_data.len());
                let overlap = new_end.saturating_sub(next_start);
                if overlap == 0 {
                    // Fills in the hole, exactly (probably common)
                    break;
                } else if next_end >= new_end {
                    trace!(
                        "New frame {new_start}-{new_end} overlaps with next frame by {overlap}, truncating"
                    );
                    // Safe conversion because any overlap has to be held in a buffer.
                    let truncate_to = new_data.len() - expect_usize(overlap);
                    to_add = &new_data[..truncate_to];
                    break;
                }
                trace!(
                    "New frame {new_start}-{new_end} spans entire next frame {next_start}-{next_end}, replacing"
                );
                to_remove.push(next_start);
                // Continue, since we may have more overlaps
            }

            for start in to_remove {
                self.data_ranges.remove(&start);
            }
        }

        if !to_add.is_empty() {
            self.received += to_u64(to_add.len());
            // new_start was advanced by overlap, so new_end is still the real end.
            // When to_add is empty, a surviving forward entry with next_end >= new_end
            // exists, so self.end is already correct — the max() is a no-op in that case.
            self.end = max(self.end, new_end);
            if let Some((prev_start, prev_len)) = extend {
                // Absorb the range that follows, now that nothing separates them: remove it, then
                // append its data to the range before it.
                let add_end = new_start + to_u64(to_add.len());
                let next = match self.data_ranges.entry(add_end) {
                    Entry::Occupied(n)
                        if prev_len + to_add.len() + n.get().len() <= Self::RANGE_TARGET =>
                    {
                        Some(n.remove())
                    }
                    _ => None,
                };
                if let Some(buf) = self.data_ranges.get_mut(&prev_start) {
                    buf.extend_from_slice(to_add);
                    buf.extend_from_slice(next.as_deref().unwrap_or_default());
                }
            } else {
                return self.insert_range(new_start, to_add.to_vec());
            }
        }

        Ok(())
    }

    #[cfg(test)]
    fn count_runs(&self) -> usize {
        let mut runs = 0;
        let mut prev_end = None;
        for (&start, data) in &self.data_ranges {
            if prev_end != Some(start) {
                runs += 1;
            }
            prev_end = Some(start + to_u64(data.len()));
        }
        runs
    }

    /// Are any bytes readable?
    #[must_use]
    pub fn data_ready(&self) -> bool {
        self.data_ranges
            .keys()
            .next()
            .is_some_and(|&start| start <= self.retired)
    }

    /// How many bytes are readable?
    fn bytes_ready(&self) -> usize {
        let mut prev_end = self.retired;
        self.data_ranges
            .iter()
            .map(|(start_offset, data)| {
                // All ranges don't overlap but we could have partially
                // retired some of the first entry's data.
                // The conversion here works because, by construction, we never hold a span of data
                // that is entirely before self.retired, so the result is less than data.len().
                let data_len =
                    data.len() - expect_usize(self.retired.saturating_sub(*start_offset));
                (start_offset, data_len)
            })
            .take_while(|(start_offset, data_len)| {
                if **start_offset <= prev_end {
                    prev_end += to_u64(*data_len);
                    true
                } else {
                    false
                }
            })
            // Accumulate, but saturate at usize::MAX.
            .fold(0, |acc: usize, (_, data_len)| acc.saturating_add(data_len))
    }

    /// Bytes read by the application.
    #[must_use]
    pub const fn retired(&self) -> u64 {
        self.retired
    }

    #[must_use]
    pub const fn received(&self) -> u64 {
        self.received
    }

    /// Discard any buffered data at or beyond `offset`, truncating a range that straddles it.
    ///
    /// Used by a reliable reset (`RESET_STREAM_AT`) to drop data above the reliable size. The
    /// dropped bytes were already charged to flow control, so this only affects what can be
    /// delivered to the application; `received` (a received-bytes stat) is intentionally left
    /// unchanged.
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn discard_after(&mut self, offset: u64) {
        self.data_ranges.split_off(&offset);
        // Truncate a range that straddles `offset`.
        if let Some(mut e) = self.data_ranges.last_entry() {
            let start = *e.key();
            // No underflow because all ranges that start at or after `offset` are gone.
            // Conversion is safe because this cannot be more than what the entry holds.
            let keep = expect_usize(offset - start);
            let data = e.get_mut();
            data.truncate(keep);

            // No overflow risk: neither start nor offset can exceed 1<<62.
            self.end = start + to_u64(data.len());
        } else {
            self.end = self.retired;
        }
    }

    /// Data bytes buffered. Could be more than `bytes_readable` if there are
    /// ranges missing.
    fn buffered(&self) -> u64 {
        self.data_ranges
            .iter()
            .map(|(&start, data)| to_u64(data.len()) - self.retired.saturating_sub(start))
            .sum()
    }

    /// Copy received data (if any) into the buffer. Returns bytes copied.
    fn read(&mut self, buf: &mut [u8]) -> usize {
        trace!("Reading {} bytes, {} available", buf.len(), self.buffered());
        let mut copied = 0;

        for (&range_start, range_data) in &mut self.data_ranges {
            let mut keep = false;
            if self.retired >= range_start {
                // Frame data has new contiguous bytes.
                // Conversion OK because this is what is held in this buffer.
                let copy_offset = expect_usize(self.retired.saturating_sub(range_start));
                assert!(range_data.len() >= copy_offset);
                let available = range_data.len() - copy_offset;
                let space = buf.len() - copied;
                let copy_bytes = if available > space {
                    keep = true;
                    space
                } else {
                    available
                };

                if copy_bytes > 0 {
                    let copy_slc = &range_data[copy_offset..copy_offset + copy_bytes];
                    buf[copied..copied + copy_bytes].copy_from_slice(copy_slc);
                    copied += copy_bytes;
                    self.retired += to_u64(copy_bytes);
                }
            } else {
                // The data in the buffer isn't contiguous.
                keep = true;
            }
            if keep {
                let mut keep = self.data_ranges.split_off(&range_start);
                mem::swap(&mut self.data_ranges, &mut keep);
                return copied;
            }
        }

        self.data_ranges.clear();
        self.end = self.retired; // All entries were consumed.
        copied
    }

    /// Extend the given Vector with any available data.
    pub fn read_to_end(&mut self, buf: &mut Vec<u8>) -> usize {
        let orig_len = buf.len();
        buf.resize(orig_len + self.bytes_ready(), 0);
        self.read(&mut buf[orig_len..])
    }
}

/// QUIC receiving states, based on -transport 3.2.
#[derive(Debug, Display)]
// Because a dead_code warning is easier than clippy::unused_self, see https://github.com/rust-lang/rust/issues/68408
enum RecvStreamState {
    Recv {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        recv_buf: RxStreamOrderer,
    },
    SizeKnown {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        recv_buf: RxStreamOrderer,
    },
    // A `RESET_STREAM_AT` has been received: the final size is known and the reliable prefix
    // `[0, reliable_size)` must be delivered before the reset is surfaced. Data at or beyond
    // `reliable_size` is dropped. Transition to `ResetRecvd` when data is `read()`.
    SizeKnownAt {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        recv_buf: RxStreamOrderer,
        err: AppError,
        final_size: u64,
        reliable_size: u64,
    },
    DataRecvd {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        recv_buf: RxStreamOrderer,
    },
    DataRead {
        final_received: u64,
        final_read: u64,
    },
    AbortReading {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        final_size_reached: bool,
        frame_needed: bool,
        err: AppError,
        final_received: u64,
        final_read: u64,
    },
    WaitForReset {
        fc: ReceiverFlowControl<StreamId>,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        final_received: u64,
        final_read: u64,
    },
    ResetRecvd {
        final_received: u64,
        final_read: u64,
    },
    // Defined by spec but we don't use it: ResetRead
}

impl RecvStreamState {
    fn new(
        max_bytes: u64,
        stream_id: StreamId,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
    ) -> Self {
        Self::Recv {
            fc: ReceiverFlowControl::new(stream_id, max_bytes),
            recv_buf: RxStreamOrderer::new(),
            session_fc,
        }
    }

    const fn recv_buf(&self) -> Option<&RxStreamOrderer> {
        match self {
            Self::Recv { recv_buf, .. }
            | Self::SizeKnown { recv_buf, .. }
            | Self::SizeKnownAt { recv_buf, .. }
            | Self::DataRecvd { recv_buf, .. } => Some(recv_buf),
            Self::DataRead { .. }
            | Self::AbortReading { .. }
            | Self::WaitForReset { .. }
            | Self::ResetRecvd { .. } => None,
        }
    }

    fn flow_control_consume_data(&mut self, consumed: u64, fin: bool) -> Res<()> {
        let (fc, session_fc, final_size_reached, retire_data) = match self {
            Self::Recv { fc, session_fc, .. } => (fc, session_fc, false, false),
            Self::WaitForReset { fc, session_fc, .. } => (fc, session_fc, false, true),
            Self::SizeKnown { fc, session_fc, .. }
            | Self::SizeKnownAt { fc, session_fc, .. }
            | Self::DataRecvd { fc, session_fc, .. } => (fc, session_fc, true, false),
            Self::AbortReading {
                fc,
                session_fc,
                final_size_reached,
                ..
            } => {
                let old_final_size_reached = *final_size_reached;
                *final_size_reached |= fin;
                (fc, session_fc, old_final_size_reached, true)
            }
            Self::DataRead { .. } | Self::ResetRecvd { .. } => {
                return Ok(());
            }
        };

        // Check final size:
        let final_size_ok = match (fin, final_size_reached) {
            (true, true) => consumed == fc.consumed(),
            (false, true) => consumed <= fc.consumed(),
            (true, false) => consumed >= fc.consumed(),
            (false, false) => true,
        };

        if !final_size_ok {
            return Err(Error::FinalSize);
        }

        let new_bytes_consumed = fc.set_consumed(consumed)?;
        session_fc.borrow_mut().consume(new_bytes_consumed)?;
        if retire_data {
            // Let's also retire this data since the stream has been aborted
            RecvStream::flow_control_retire_data(fc.consumed() - fc.retired(), fc, session_fc);
        }
        Ok(())
    }
}

// See https://www.w3.org/TR/webtransport/#receive-stream-stats
#[derive(Debug, Clone, Copy)]
pub struct Stats {
    // An indicator of progress on how many of the server application’s bytes
    // intended for this stream have been received so far.
    // Only sequential bytes up to, but not including, the first missing byte,
    // are counted. This number can only increase.
    pub bytes_received: u64,
    // The total number of bytes the application has successfully read from this
    // stream. This number can only increase, and is always less than or equal
    // to bytes_received.
    pub bytes_read: u64,
}

impl Stats {
    #[must_use]
    pub const fn new(bytes_received: u64, bytes_read: u64) -> Self {
        Self {
            bytes_received,
            bytes_read,
        }
    }

    #[must_use]
    pub const fn bytes_received(&self) -> u64 {
        self.bytes_received
    }

    #[must_use]
    pub const fn bytes_read(&self) -> u64 {
        self.bytes_read
    }
}

/// Implement a QUIC receive stream.
#[derive(Debug)]
pub struct RecvStream {
    stream_id: StreamId,
    state: RecvStreamState,
    conn_events: ConnectionEvents,
    keep_alive: Option<Rc<()>>,
}

impl RecvStream {
    pub fn new(
        stream_id: StreamId,
        max_stream_data: u64,
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        conn_events: ConnectionEvents,
    ) -> Self {
        Self {
            stream_id,
            state: RecvStreamState::new(max_stream_data, stream_id, session_fc),
            conn_events,
            keep_alive: None,
        }
    }

    fn set_state(&mut self, new_state: RecvStreamState) {
        debug_assert_ne!(
            mem::discriminant(&self.state),
            mem::discriminant(&new_state)
        );
        trace!(
            "RecvStream {} state {} -> {new_state}",
            self.stream_id.as_u64(),
            self.state
        );

        match new_state {
            // Receiving all data, or receiving or requesting RESET_STREAM
            // is cause to stop keepalives.
            RecvStreamState::DataRecvd { .. }
            | RecvStreamState::AbortReading { .. }
            | RecvStreamState::ResetRecvd { .. } => {
                self.keep_alive = None;
            }
            // Once all the data is read, generate an event.
            RecvStreamState::DataRead { .. } => {
                self.conn_events.recv_stream_complete(self.stream_id);
            }
            _ => {}
        }

        self.state = new_state;
    }

    #[must_use]
    pub const fn stats(&self) -> Stats {
        match &self.state {
            RecvStreamState::Recv { recv_buf, .. }
            | RecvStreamState::SizeKnown { recv_buf, .. }
            | RecvStreamState::SizeKnownAt { recv_buf, .. }
            | RecvStreamState::DataRecvd { recv_buf, .. } => {
                let received = recv_buf.received();
                let read = recv_buf.retired();
                Stats::new(received, read)
            }
            RecvStreamState::AbortReading {
                final_received,
                final_read,
                ..
            }
            | RecvStreamState::WaitForReset {
                final_received,
                final_read,
                ..
            }
            | RecvStreamState::DataRead {
                final_received,
                final_read,
            }
            | RecvStreamState::ResetRecvd {
                final_received,
                final_read,
            } => {
                let received = *final_received;
                let read = *final_read;
                Stats::new(received, read)
            }
        }
    }

    /// # Errors
    /// When the incoming data violates flow control limits, or `ProtocolViolation` when it leaves
    /// the reassembly buffer with excessively many ranges.
    /// # Panics
    /// Only when `u64` values are so big that they can't fit in a `usize`, which
    /// only happens on a 32-bit machine that has far too much unread data.
    pub fn inbound_stream_frame(&mut self, fin: bool, offset: u64, data: &[u8]) -> Res<()> {
        // We should post a DataReadable event only once when we change from no-data-ready to
        // data-ready. Therefore remember the state before processing a new frame.
        let already_data_ready = self.data_ready();
        let new_end = offset + u64::try_from(data.len())?;

        self.state.flow_control_consume_data(new_end, fin)?;

        match &mut self.state {
            RecvStreamState::Recv {
                recv_buf,
                fc,
                session_fc,
            } => {
                recv_buf.inbound_frame(offset, data)?;
                if fin {
                    let all_recv =
                        fc.consumed() == recv_buf.retired() + to_u64(recv_buf.bytes_ready());
                    let buf = mem::replace(recv_buf, RxStreamOrderer::new());
                    let fc_copy = mem::take(fc);
                    let session_fc_copy = mem::take(session_fc);
                    if all_recv {
                        self.set_state(RecvStreamState::DataRecvd {
                            fc: fc_copy,
                            session_fc: session_fc_copy,
                            recv_buf: buf,
                        });
                    } else {
                        self.set_state(RecvStreamState::SizeKnown {
                            fc: fc_copy,
                            session_fc: session_fc_copy,
                            recv_buf: buf,
                        });
                    }
                }
            }
            RecvStreamState::SizeKnown {
                recv_buf,
                fc,
                session_fc,
            } => {
                recv_buf.inbound_frame(offset, data)?;
                if fc.consumed() == recv_buf.retired() + to_u64(recv_buf.bytes_ready()) {
                    let buf = mem::replace(recv_buf, RxStreamOrderer::new());
                    let fc_copy = mem::take(fc);
                    let session_fc_copy = mem::take(session_fc);
                    self.set_state(RecvStreamState::DataRecvd {
                        fc: fc_copy,
                        session_fc: session_fc_copy,
                        recv_buf: buf,
                    });
                }
            }
            RecvStreamState::SizeKnownAt {
                recv_buf,
                reliable_size,
                ..
            } => {
                // Buffer the reliable prefix; data at or beyond `reliable_size` is dropped.
                // Completion is driven by `read()`, not by frame arrival.
                let keep = reliable_size.saturating_sub(offset);
                if keep > 0 {
                    let keep = min(data.len(), usize::try_from(keep)?);
                    recv_buf.inbound_frame(offset, &data[..keep])?;
                }
            }
            RecvStreamState::DataRecvd { .. }
            | RecvStreamState::DataRead { .. }
            | RecvStreamState::AbortReading { .. }
            | RecvStreamState::WaitForReset { .. }
            | RecvStreamState::ResetRecvd { .. } => {
                trace!("data received when we are in state {}", self.state);
            }
        }

        if !already_data_ready && (self.data_ready() || self.needs_to_inform_app_about_fin()) {
            self.conn_events.recv_stream_readable(self.stream_id);
        }

        Ok(())
    }

    /// Handle a `RESET_STREAM` or `RESET_STREAM_AT` frame.
    ///
    /// Any reliable prefix `[0, reliable_size)` is delivered before the reset event.
    ///
    /// # Errors
    /// [`Error::FrameEncoding`] if `reliable_size > final_size`, [`Error::FinalSize`] if a
    /// previously-known final size changes, or [`Error::StreamState`] if a later frame changes
    /// the error code.
    ///
    /// # Returns
    /// `true` when the stream reaches `ResetRecvd` (ended); `false` while it remains in
    /// `SizeKnownAt` awaiting delivery of the prefix, or for a no-op in a terminal state.
    pub fn reset(
        &mut self,
        application_error_code: AppError,
        final_size: u64,
        reliable_size: u64,
    ) -> Res<bool> {
        // Defensive: also rejected at frame decode.
        if reliable_size > final_size {
            return Err(Error::FrameEncoding);
        }
        // Catches a changed final size as FINAL_SIZE_ERROR.
        self.state.flow_control_consume_data(final_size, true)?;

        match &mut self.state {
            RecvStreamState::Recv {
                fc,
                session_fc,
                recv_buf,
            }
            | RecvStreamState::SizeKnown {
                fc,
                session_fc,
                recv_buf,
            } => {
                // Keep the buffered reliable prefix; drop anything at or beyond `reliable_size`.
                recv_buf.discard_after(reliable_size);
                // Return credit for the dropped tail immediately; only the still-unread prefix
                // keeps its credit (retired as the application reads it).
                Self::retire_undeliverable(
                    final_size,
                    reliable_size,
                    recv_buf.retired(),
                    fc,
                    session_fc,
                );
                let fc = mem::take(fc);
                let session_fc = mem::take(session_fc);
                let recv_buf = mem::replace(recv_buf, RxStreamOrderer::new());
                self.set_state(RecvStreamState::SizeKnownAt {
                    fc,
                    session_fc,
                    recv_buf,
                    err: application_error_code,
                    final_size,
                    reliable_size,
                });
                Ok(self.complete_reliable_reset_if_drained())
            }
            RecvStreamState::SizeKnownAt {
                fc,
                session_fc,
                recv_buf,
                err,
                final_size,
                reliable_size: stored,
            } => {
                // The final size is already validated above; a changed error code is a state
                // error. `reliable_size` may only be reduced (increases are ignored).
                if application_error_code != *err {
                    return Err(Error::StreamState);
                }
                if reliable_size < *stored {
                    *stored = reliable_size;
                    recv_buf.discard_after(reliable_size);
                    // Return credit for the newly-dropped range immediately.
                    Self::retire_undeliverable(
                        *final_size,
                        reliable_size,
                        recv_buf.retired(),
                        fc,
                        session_fc,
                    );
                    Ok(self.complete_reliable_reset_if_drained())
                } else {
                    Ok(false)
                }
            }
            RecvStreamState::AbortReading {
                final_received,
                final_read,
                ..
            }
            | RecvStreamState::WaitForReset {
                final_received,
                final_read,
                ..
            } => {
                // The application abandoned the read side (via stop_sending). We can discard the
                // reliable and ignore `reliable_size`, which can't be validated here
                // because this is the first `RESET_STREAM[_AT]` we've received.
                // Note: we don't check that subsequent frames contain a correct `reliable_size`.
                let final_received = *final_received;
                let final_read = *final_read;
                Ok(self.finish_reset(
                    final_size,
                    application_error_code,
                    final_received,
                    final_read,
                ))
            }
            // DataRecvd / DataRead / ResetRecvd: nothing to do.
            _ => Ok(false),
        }
    }

    /// Finalize a reset: release any flow control still held up to `final_size` (the dropped tail
    /// is never delivered), surface the reset to the application, and move to `ResetRecvd`. Returns
    /// `true` to signal that the stream has ended.
    fn finish_reset(
        &mut self,
        final_size: u64,
        err: AppError,
        final_received: u64,
        final_read: u64,
    ) -> bool {
        if let RecvStreamState::SizeKnownAt { fc, session_fc, .. }
        | RecvStreamState::AbortReading { fc, session_fc, .. }
        | RecvStreamState::WaitForReset { fc, session_fc, .. } = &mut self.state
        {
            Self::flow_control_retire_data(final_size - fc.retired(), fc, session_fc);
        }
        self.conn_events.recv_stream_reset(self.stream_id, err);
        self.set_state(RecvStreamState::ResetRecvd {
            final_received,
            final_read,
        });
        true
    }

    /// While in `SizeKnownAt`, once the application has read the entire reliable prefix, release
    /// the remaining flow control, surface the reset, and move to `ResetRecvd`. Returns whether
    /// the stream ended.
    fn complete_reliable_reset_if_drained(&mut self) -> bool {
        let RecvStreamState::SizeKnownAt {
            recv_buf,
            err,
            final_size,
            reliable_size,
            ..
        } = &self.state
        else {
            return false;
        };
        if recv_buf.retired() < *reliable_size {
            return false;
        }
        let final_size = *final_size;
        let err = *err;
        let final_received = recv_buf.received();
        let final_read = recv_buf.retired();
        self.finish_reset(final_size, err, final_received, final_read)
    }

    fn flow_control_retire_data(
        new_read: u64,
        fc: &mut ReceiverFlowControl<StreamId>,
        session_fc: &Rc<RefCell<ReceiverFlowControl<()>>>,
    ) {
        if new_read > 0 {
            fc.add_retired(new_read);
            session_fc.borrow_mut().add_retired(new_read);
        }
    }

    /// On a reliable reset, retire the flow control for everything that will never be delivered,
    /// i.e. all but the still-unread reliable prefix `[read, reliable_size)`. This returns
    /// stream- and connection-level credit to the peer immediately, rather than only once the
    /// application has drained the prefix (the prefix's own credit is retired as it is read).
    ///
    /// `read` is the number of bytes the application has read so far (`recv_buf.retired()`).
    fn retire_undeliverable(
        final_size: u64,
        reliable_size: u64,
        read: u64,
        fc: &mut ReceiverFlowControl<StreamId>,
        session_fc: &Rc<RefCell<ReceiverFlowControl<()>>>,
    ) {
        let still_needed = reliable_size.saturating_sub(read);
        let target_retired = final_size - still_needed;
        Self::flow_control_retire_data(target_retired.saturating_sub(fc.retired()), fc, session_fc);
    }

    /// Send a flow control update.
    /// This is used when a peer declares that they are blocked.
    /// This sends `MAX_STREAM_DATA` if there is any increase possible.
    pub const fn send_flowc_update(&mut self) {
        if let RecvStreamState::Recv { fc, .. } = &mut self.state {
            fc.send_flowc_update();
        }
    }

    pub const fn set_stream_max_data(&mut self, max_data: u64) {
        if let RecvStreamState::Recv { fc, .. } = &mut self.state {
            fc.set_max_active(max_data);
        }
    }

    #[must_use]
    pub const fn is_ended(&self) -> bool {
        matches!(
            self.state,
            RecvStreamState::ResetRecvd { .. } | RecvStreamState::DataRead { .. }
        )
    }

    // App got all data but did not get the fin signal.
    const fn needs_to_inform_app_about_fin(&self) -> bool {
        matches!(self.state, RecvStreamState::DataRecvd { .. })
    }

    fn data_ready(&self) -> bool {
        self.state
            .recv_buf()
            .is_some_and(RxStreamOrderer::data_ready)
    }

    /// # Errors
    /// `NoMoreData` if data and fin bit were previously read by the application.
    pub fn read(&mut self, buf: &mut [u8]) -> Res<(usize, bool)> {
        let data_recvd_state = matches!(self.state, RecvStreamState::DataRecvd { .. });
        match &mut self.state {
            RecvStreamState::Recv {
                recv_buf,
                fc,
                session_fc,
            }
            | RecvStreamState::SizeKnown {
                recv_buf,
                fc,
                session_fc,
                ..
            }
            | RecvStreamState::DataRecvd {
                recv_buf,
                fc,
                session_fc,
            } => {
                let bytes_read = recv_buf.read(buf);
                Self::flow_control_retire_data(u64::try_from(bytes_read)?, fc, session_fc);
                let fin_read = data_recvd_state
                    && if recv_buf.buffered() == 0 {
                        let received = recv_buf.received();
                        let read = recv_buf.retired();
                        self.set_state(RecvStreamState::DataRead {
                            final_received: received,
                            final_read: read,
                        });
                        true
                    } else {
                        false
                    };
                Ok((bytes_read, fin_read))
            }
            RecvStreamState::SizeKnownAt {
                recv_buf,
                fc,
                session_fc,
                ..
            } => {
                let bytes_read = recv_buf.read(buf);
                Self::flow_control_retire_data(u64::try_from(bytes_read)?, fc, session_fc);
                // Once the whole reliable prefix has been read, surface the reset. A reliable
                // reset never delivers a FIN, so `fin_read` is always `false`.
                self.complete_reliable_reset_if_drained();
                Ok((bytes_read, false))
            }
            RecvStreamState::DataRead { .. }
            | RecvStreamState::AbortReading { .. }
            | RecvStreamState::WaitForReset { .. }
            | RecvStreamState::ResetRecvd { .. } => Err(Error::NoMoreData),
        }
    }

    /// # Returns
    /// `true` if the stream transitions to `DataRead` (ended).
    /// `false` if the stream transitions to `AbortReading` or was already
    /// in a terminal or aborting state.
    #[must_use]
    pub fn stop_sending(&mut self, err: AppError) -> bool {
        trace!("stop_sending called when in state {}", self.state);
        match &mut self.state {
            RecvStreamState::Recv {
                fc,
                session_fc,
                recv_buf,
            }
            | RecvStreamState::SizeKnown {
                fc,
                session_fc,
                recv_buf,
            } => {
                // Retire data
                Self::flow_control_retire_data(fc.consumed() - fc.retired(), fc, session_fc);
                let fc_copy = mem::take(fc);
                let session_fc_copy = mem::take(session_fc);
                let received = recv_buf.received();
                let read = recv_buf.retired();
                self.set_state(RecvStreamState::AbortReading {
                    fc: fc_copy,
                    session_fc: session_fc_copy,
                    final_size_reached: matches!(self.state, RecvStreamState::SizeKnown { .. }),
                    frame_needed: true,
                    err,
                    final_received: received,
                    final_read: read,
                });
                false
            }
            RecvStreamState::DataRecvd {
                fc,
                session_fc,
                recv_buf,
            } => {
                Self::flow_control_retire_data(fc.consumed() - fc.retired(), fc, session_fc);
                let final_received = recv_buf.received();
                let final_read = recv_buf.retired();
                self.set_state(RecvStreamState::DataRead {
                    final_received,
                    final_read,
                });
                true
            }
            RecvStreamState::SizeKnownAt {
                recv_buf,
                err,
                final_size,
                ..
            } => {
                // The reset is already known; the application is abandoning the (not fully
                // delivered) reliable prefix. Release flow control, surface the reset, and end.
                let final_size = *final_size;
                let err = *err;
                let final_received = recv_buf.received();
                let final_read = recv_buf.retired();
                self.finish_reset(final_size, err, final_received, final_read)
            }
            RecvStreamState::DataRead { .. }
            | RecvStreamState::AbortReading { .. }
            | RecvStreamState::WaitForReset { .. }
            | RecvStreamState::ResetRecvd { .. } => false,
        }
    }

    /// Maybe write a `MAX_STREAM_DATA` frame.
    pub fn write_frame<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
        now: Instant,
        rtt: Duration,
    ) {
        match &mut self.state {
            // Maybe send MAX_STREAM_DATA
            RecvStreamState::Recv { fc, .. } => fc.write_frames(builder, tokens, stats, now, rtt),
            // Maybe send STOP_SENDING
            RecvStreamState::AbortReading {
                frame_needed, err, ..
            } if *frame_needed
                && builder.write_varint_frame(&[
                    FrameType::StopSending.into(),
                    self.stream_id.as_u64(),
                    *err,
                ]) =>
            {
                tokens.push(recovery::Token::Stream(StreamRecoveryToken::StopSending {
                    stream_id: self.stream_id,
                }));
                stats.stop_sending += 1;
                *frame_needed = false;
            }
            _ => {}
        }
    }

    pub const fn max_stream_data_lost(&mut self, maximum_data: u64) {
        if let RecvStreamState::Recv { fc, .. } = &mut self.state {
            fc.frame_lost(maximum_data);
        }
    }

    pub const fn stop_sending_lost(&mut self) {
        if let RecvStreamState::AbortReading { frame_needed, .. } = &mut self.state {
            *frame_needed = true;
        }
    }

    /// # Returns
    /// `true` if the stream transitions to `ResetRecvd` (ended) because
    /// the final size was already known.
    #[must_use]
    pub fn stop_sending_acked(&mut self) -> bool {
        if let RecvStreamState::AbortReading {
            fc,
            session_fc,
            final_size_reached,
            final_received,
            final_read,
            ..
        } = &mut self.state
        {
            let received = *final_received;
            let read = *final_read;
            if *final_size_reached {
                // We already know the final_size of the stream therefore we
                // do not need to wait for RESET.
                self.set_state(RecvStreamState::ResetRecvd {
                    final_received: received,
                    final_read: read,
                });
                return true;
            }
            let fc_copy = mem::take(fc);
            let session_fc_copy = mem::take(session_fc);
            self.set_state(RecvStreamState::WaitForReset {
                fc: fc_copy,
                session_fc: session_fc_copy,
                final_received: received,
                final_read: read,
            });
        }
        false
    }

    #[cfg(test)]
    #[must_use]
    pub const fn has_frames_to_write(&self) -> bool {
        if let RecvStreamState::Recv { fc, .. } = &self.state {
            fc.frame_needed()
        } else {
            false
        }
    }

    #[cfg(test)]
    #[must_use]
    pub const fn fc(&self) -> Option<&ReceiverFlowControl<StreamId>> {
        match &self.state {
            RecvStreamState::Recv { fc, .. }
            | RecvStreamState::SizeKnown { fc, .. }
            | RecvStreamState::SizeKnownAt { fc, .. }
            | RecvStreamState::DataRecvd { fc, .. }
            | RecvStreamState::AbortReading { fc, .. }
            | RecvStreamState::WaitForReset { fc, .. } => Some(fc),
            _ => None,
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{cell::RefCell, fmt::Debug, ops::Range, rc::Rc, time::Duration};

    use neqo_common::{Encoder, event::Provider as _, expect_usize, to_u64};
    use test_fixture::now;
    use tracing::trace;

    use super::{RecvStream, RecvStreamState};
    use crate::{
        ConnectionEvents, Error, INITIAL_LOCAL_MAX_STREAM_DATA, StreamId,
        events::ConnectionEvent,
        fc::{ReceiverFlowControl, WINDOW_UPDATE_FRACTION},
        packet, recovery,
        recv_stream::RxStreamOrderer,
        stats::FrameStats,
    };

    const SESSION_WINDOW: usize = 1024;

    fn recv_ranges(ranges: &[Range<u64>], available: usize) {
        const ZEROES: &[u8] = &[0; 100];
        trace!("recv_ranges {ranges:?}");

        let mut s = RxStreamOrderer::default();
        for r in ranges {
            let data = &ZEROES[..expect_usize(r.end - r.start)];
            s.inbound_frame(r.start, data).unwrap();
        }

        let mut buf = [0xff; 100];
        let mut total_recvd = 0;
        loop {
            let recvd = s.read(&mut buf[..]);
            trace!("recv_ranges read {recvd}");
            total_recvd += recvd;
            if recvd == 0 {
                assert_eq!(total_recvd, available);
                break;
            }
        }
    }

    /// A buffer of exactly 4096 bytes has reached the extension limit and must not be extended.
    #[test]
    fn inbound_frame_no_extend_at_4096() {
        let mut s = RxStreamOrderer::default();
        // Fill to the extend threshold.
        s.inbound_frame(0, &[0u8; 4096]).unwrap();
        assert_eq!(s.data_ranges[&0].len(), 4096);
        // The next byte must not be merged; the threshold has been reached.
        s.inbound_frame(4096, &[1u8]).unwrap();
        assert_eq!(
            s.data_ranges.len(),
            2,
            "a 4096-byte buffer must not be extended further"
        );
    }

    #[test]
    fn already_received_across_chunks() {
        const CHUNK: usize = RxStreamOrderer::RANGE_TARGET;
        const TARGET: u64 = to_u64(CHUNK);
        const LEN: usize = 2 * CHUNK + 10;
        let mut s = RxStreamOrderer::default();
        s.inbound_frame(0, &[0u8; CHUNK]).unwrap();
        s.inbound_frame(TARGET, &[1u8; CHUNK]).unwrap();
        s.inbound_frame(2 * TARGET, &[2u8; 10]).unwrap();
        assert_eq!(s.data_ranges.len(), 3);

        assert!(s.already_received(0, LEN));
        assert!(s.already_received(TARGET - 6, 16), "spans chunks");
        assert!(!s.already_received(0, LEN + 1), "one byte past the end");

        // The same duplicate frame grows `received`, which is why `already_received` exists.
        let received = s.received();
        s.inbound_frame(0, &[0u8; LEN]).unwrap();
        assert!(s.received() > received);
    }

    /// Many gapped ranges, the shape a peer can create with one-byte CRYPTO frames.
    #[test]
    fn already_received_fragmented() {
        const COUNT: u64 = 1000;
        let mut s = RxStreamOrderer::default();
        for i in 0..COUNT {
            s.inbound_frame(i * 4, &[0u8; 1]).unwrap();
        }
        assert_eq!(s.data_ranges.len(), usize::try_from(COUNT).unwrap());

        let last = (COUNT - 1) * 4;
        assert!(s.already_received(last, 1), "the last byte received");
        assert!(s.already_received(0, 1));
        assert!(!s.already_received(last + 1, 1), "past the end");
        assert!(!s.already_received(0, 4), "spans a gap");
    }

    /// Data already read still counts as received; a gap never does.
    #[test]
    fn already_received_includes_read_data() {
        let mut s = RxStreamOrderer::default();
        s.inbound_frame(0, &[0u8; 10]).unwrap();
        s.inbound_frame(20, &[1u8; 10]).unwrap();

        let mut buf = [0u8; 10];
        assert_eq!(s.read(&mut buf), 10);
        assert!(s.already_received(0, 10), "already read");
        assert!(s.already_received(25, 5));
        assert!(!s.already_received(10, 10), "the gap");
        assert!(!s.already_received(5, 10), "spans into the gap");
        assert!(s.already_received(15, 0), "an empty range is covered");
    }

    /// A buffer of 4095 bytes IS extended when the next frame is contiguous.
    #[test]
    fn inbound_frame_extends_below_4096() {
        let mut s = RxStreamOrderer::default();
        s.inbound_frame(0, &[0u8; 4095]).unwrap();
        s.inbound_frame(4095, &[1u8]).unwrap();
        assert_eq!(s.data_ranges.len(), 1);
        assert_eq!(s.data_ranges[&0].len(), 4096);
    }

    /// Reading exactly `available` bytes frees the range so the next read can proceed.
    #[test]
    fn read_exact_available_removes_range() {
        let mut s = RxStreamOrderer::default();
        s.inbound_frame(0, &[1u8; 5]).unwrap();
        s.inbound_frame(5, &[2u8; 5]).unwrap();

        let mut buf = [0u8; 5];
        assert_eq!(s.read(&mut buf), 5);
        assert_eq!(buf, [1u8; 5]);
        assert_eq!(s.read(&mut buf), 5);
        assert_eq!(buf, [2u8; 5]);
    }

    #[test]
    #[expect(
        clippy::single_range_in_vec_init,
        reason = "Because that lint makes no sense here."
    )]
    fn recv_noncontiguous() {
        // Non-contiguous with the start, no data available.
        recv_ranges(&[10..20], 0);
    }

    /// Overlaps with the start of a 10..20 range of bytes.
    #[test]
    fn recv_overlap_start() {
        // Overlap the start, with a larger new value.
        // More overlap than not.
        recv_ranges(&[10..20, 4..18, 0..4], 20);
        // Overlap the start, with a larger new value.
        // Less overlap than not.
        recv_ranges(&[10..20, 2..15, 0..2], 20);
        // Overlap the start, with a smaller new value.
        // More overlap than not.
        recv_ranges(&[10..20, 8..14, 0..8], 20);
        // Overlap the start, with a smaller new value.
        // Less overlap than not.
        recv_ranges(&[10..20, 6..13, 0..6], 20);

        // Again with some of the first range split in two.
        recv_ranges(&[10..11, 11..20, 4..18, 0..4], 20);
        recv_ranges(&[10..11, 11..20, 2..15, 0..2], 20);
        recv_ranges(&[10..11, 11..20, 8..14, 0..8], 20);
        recv_ranges(&[10..11, 11..20, 6..13, 0..6], 20);

        // Again with a gap in the first range.
        recv_ranges(&[10..11, 12..20, 4..18, 0..4], 20);
        recv_ranges(&[10..11, 12..20, 2..15, 0..2], 20);
        recv_ranges(&[10..11, 12..20, 8..14, 0..8], 20);
        recv_ranges(&[10..11, 12..20, 6..13, 0..6], 20);
    }

    /// Overlaps with the end of a 10..20 range of bytes.
    #[test]
    fn recv_overlap_end() {
        // Overlap the end, with a larger new value.
        // More overlap than not.
        recv_ranges(&[10..20, 12..25, 0..10], 25);
        // Overlap the end, with a larger new value.
        // Less overlap than not.
        recv_ranges(&[10..20, 17..33, 0..10], 33);
        // Overlap the end, with a smaller new value.
        // More overlap than not.
        recv_ranges(&[10..20, 15..21, 0..10], 21);
        // Overlap the end, with a smaller new value.
        // Less overlap than not.
        recv_ranges(&[10..20, 17..25, 0..10], 25);

        // Again with some of the first range split in two.
        recv_ranges(&[10..19, 19..20, 12..25, 0..10], 25);
        recv_ranges(&[10..19, 19..20, 17..33, 0..10], 33);
        recv_ranges(&[10..19, 19..20, 15..21, 0..10], 21);
        recv_ranges(&[10..19, 19..20, 17..25, 0..10], 25);

        // Again with a gap in the first range.
        recv_ranges(&[10..18, 19..20, 12..25, 0..10], 25);
        recv_ranges(&[10..18, 19..20, 17..33, 0..10], 33);
        recv_ranges(&[10..18, 19..20, 15..21, 0..10], 21);
        recv_ranges(&[10..18, 19..20, 17..25, 0..10], 25);
    }

    /// Complete overlaps with the start of a 10..20 range of bytes.
    #[test]
    fn recv_overlap_complete() {
        // Complete overlap, more at the end.
        recv_ranges(&[10..20, 9..23, 0..9], 23);
        // Complete overlap, more at the start.
        recv_ranges(&[10..20, 3..23, 0..3], 23);
        // Complete overlap, to end.
        recv_ranges(&[10..20, 5..20, 0..5], 20);
        // Complete overlap, from start.
        recv_ranges(&[10..20, 10..27, 0..10], 27);
        // Complete overlap, from 0 and more.
        recv_ranges(&[10..20, 0..23], 23);

        // Again with the first range split in two.
        recv_ranges(&[10..14, 14..20, 9..23, 0..9], 23);
        recv_ranges(&[10..14, 14..20, 3..23, 0..3], 23);
        recv_ranges(&[10..14, 14..20, 5..20, 0..5], 20);
        recv_ranges(&[10..14, 14..20, 10..27, 0..10], 27);
        recv_ranges(&[10..14, 14..20, 0..23], 23);

        // Again with the a gap in the first range.
        recv_ranges(&[10..13, 14..20, 9..23, 0..9], 23);
        recv_ranges(&[10..13, 14..20, 3..23, 0..3], 23);
        recv_ranges(&[10..13, 14..20, 5..20, 0..5], 20);
        recv_ranges(&[10..13, 14..20, 10..27, 0..10], 27);
        recv_ranges(&[10..13, 14..20, 0..23], 23);
    }

    /// An overlap with no new bytes.
    #[test]
    fn recv_overlap_duplicate() {
        recv_ranges(&[10..20, 11..12, 0..10], 20);
        recv_ranges(&[10..20, 10..15, 0..10], 20);
        recv_ranges(&[10..20, 14..20, 0..10], 20);
        // Now with the first range split.
        recv_ranges(&[10..14, 14..20, 10..15, 0..10], 20);
        recv_ranges(&[10..15, 16..20, 21..25, 10..25, 0..10], 25);
    }

    /// Reading exactly one chunk works, when the next chunk starts immediately.
    #[test]
    fn stop_reading_at_chunk() {
        const CHUNK_SIZE: usize = 10;
        const EXTRA_SIZE: usize = 3;
        let mut s = RxStreamOrderer::new();

        // Add three chunks.
        s.inbound_frame(0, &[0; CHUNK_SIZE]).unwrap();
        let offset = to_u64(CHUNK_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();
        let offset = to_u64(CHUNK_SIZE + EXTRA_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();

        // Read, providing only enough space for the first.
        let mut buf = [0; 100];
        let count = s.read(&mut buf[..CHUNK_SIZE]);
        assert_eq!(count, CHUNK_SIZE);
        let count = s.read(&mut buf[..]);
        assert_eq!(count, EXTRA_SIZE * 2);
    }

    #[test]
    fn recv_overlap_while_reading() {
        let mut s = RxStreamOrderer::new();

        // Add a chunk
        s.inbound_frame(0, &[0; 150]).unwrap();
        assert_eq!(s.data_ranges[&0].len(), 150);
        // Read, providing only enough space for the first 100.
        let mut buf = [0; 100];
        let count = s.read(&mut buf[..]);
        assert_eq!(count, 100);
        assert_eq!(s.retired, 100);

        // Add a second frame that overlaps.
        // This shouldn't truncate the first frame, as we're already
        // Reading from it.
        s.inbound_frame(120, &[0; 60]).unwrap();
        assert_eq!(s.data_ranges[&0].len(), 180);
        // Read second part of first frame and all of the second frame
        let count = s.read(&mut buf[..]);
        assert_eq!(count, 80);
    }

    /// Reading exactly one chunk works, when there is a gap.
    #[test]
    fn stop_reading_at_gap() {
        const CHUNK_SIZE: usize = 10;
        const EXTRA_SIZE: usize = 3;
        let mut s = RxStreamOrderer::new();

        // Add three chunks.
        s.inbound_frame(0, &[0; CHUNK_SIZE]).unwrap();
        let offset = to_u64(CHUNK_SIZE + EXTRA_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();

        // Read, providing only enough space for the first chunk.
        let mut buf = [0; 100];
        let count = s.read(&mut buf[..CHUNK_SIZE]);
        assert_eq!(count, CHUNK_SIZE);

        // Now fill the gap and ensure that everything can be read.
        let offset = to_u64(CHUNK_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();
        let count = s.read(&mut buf[..]);
        assert_eq!(count, EXTRA_SIZE * 2);
    }

    /// Reading exactly one chunk works, when there is a gap.
    #[test]
    fn stop_reading_in_chunk() {
        const CHUNK_SIZE: usize = 10;
        const EXTRA_SIZE: usize = 3;
        let mut s = RxStreamOrderer::new();

        // Add two chunks.
        s.inbound_frame(0, &[0; CHUNK_SIZE]).unwrap();
        let offset = to_u64(CHUNK_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();

        // Read, providing only enough space for some of the first chunk.
        let mut buf = [0; 100];
        let count = s.read(&mut buf[..CHUNK_SIZE - EXTRA_SIZE]);
        assert_eq!(count, CHUNK_SIZE - EXTRA_SIZE);

        let count = s.read(&mut buf[..]);
        assert_eq!(count, EXTRA_SIZE * 2);
    }

    /// Read one byte at a time.
    #[test]
    fn read_byte_at_a_time() {
        const CHUNK_SIZE: usize = 10;
        const EXTRA_SIZE: usize = 3;
        let mut s = RxStreamOrderer::new();

        // Add two chunks.
        s.inbound_frame(0, &[0; CHUNK_SIZE]).unwrap();
        let offset = to_u64(CHUNK_SIZE);
        s.inbound_frame(offset, &[0; EXTRA_SIZE]).unwrap();

        let mut buf = [0; 1];
        for _ in 0..CHUNK_SIZE + EXTRA_SIZE {
            let count = s.read(&mut buf[..]);
            assert_eq!(count, 1);
        }
        assert_eq!(0, s.read(&mut buf[..]));
    }

    fn check_stats(stream: &RecvStream, expected_received: u64, expected_read: u64) {
        let stream_stats = stream.stats();
        assert_eq!(expected_received, stream_stats.bytes_received());
        assert_eq!(expected_read, stream_stats.bytes_read());
    }

    #[test]
    fn stream_rx() {
        let conn_events = ConnectionEvents::default();

        let mut s = RecvStream::new(
            StreamId::from(567),
            1024,
            Rc::new(RefCell::new(ReceiverFlowControl::new((), 1024 * 1024))),
            conn_events,
        );

        // test receiving a contig frame and reading it works
        s.inbound_stream_frame(false, 0, &[1; 10]).unwrap();
        assert!(s.data_ready());
        check_stats(&s, 10, 0);

        let mut buf = vec![0u8; 100];
        assert_eq!(s.read(&mut buf).unwrap(), (10, false));
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 0);

        check_stats(&s, 10, 10);

        // test receiving a noncontig frame
        s.inbound_stream_frame(false, 12, &[2; 12]).unwrap();
        assert!(!s.data_ready());
        assert_eq!(s.read(&mut buf).unwrap(), (0, false));
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 12);

        check_stats(&s, 22, 10);

        // another frame that overlaps the first
        s.inbound_stream_frame(false, 14, &[3; 8]).unwrap();
        assert!(!s.data_ready());
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 12);

        check_stats(&s, 22, 10);

        // fill in the gap, but with a FIN
        s.inbound_stream_frame(true, 10, &[4; 6]).unwrap_err();
        assert!(!s.data_ready());
        assert_eq!(s.read(&mut buf).unwrap(), (0, false));
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 12);

        check_stats(&s, 22, 10);

        // fill in the gap
        s.inbound_stream_frame(false, 10, &[5; 10]).unwrap();
        assert!(s.data_ready());
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 14);

        check_stats(&s, 24, 10);

        // a legit FIN
        s.inbound_stream_frame(true, 24, &[6; 18]).unwrap();
        assert_eq!(s.state.recv_buf().unwrap().retired(), 10);
        assert_eq!(s.state.recv_buf().unwrap().buffered(), 32);
        assert!(s.data_ready());
        assert_eq!(s.read(&mut buf).unwrap(), (32, true));

        check_stats(&s, 42, 42);

        // Stream now no longer readable (is in DataRead state)
        s.read(&mut buf).unwrap_err();
    }

    fn check_chunks(s: &RxStreamOrderer, expected: &[(u64, usize)]) {
        assert_eq!(s.data_ranges.len(), expected.len());
        for ((start, buf), (expected_start, expected_len)) in s.data_ranges.iter().zip(expected) {
            assert_eq!((*start, buf.len()), (*expected_start, *expected_len));
        }
    }

    // Test deduplication when the new data is at the end.
    #[test]
    fn stream_rx_dedupe_tail() {
        let mut s = RxStreamOrderer::new();

        s.inbound_frame(0, &[1; 6]).unwrap();
        check_chunks(&s, &[(0, 6)]);

        // New data that overlaps entirely (starting from the head), is ignored.
        s.inbound_frame(0, &[2; 3]).unwrap();
        check_chunks(&s, &[(0, 6)]);

        // New data that overlaps at the tail has any new data appended.
        s.inbound_frame(2, &[3; 6]).unwrap();
        check_chunks(&s, &[(0, 8)]);

        // New data that overlaps entirely (up to the tail), is ignored.
        s.inbound_frame(4, &[4; 4]).unwrap();
        check_chunks(&s, &[(0, 8)]);

        // New data that overlaps, starting from the beginning is appended too.
        s.inbound_frame(0, &[5; 10]).unwrap();
        check_chunks(&s, &[(0, 10)]);

        // New data that is entirely subsumed is ignored.
        s.inbound_frame(2, &[6; 2]).unwrap();
        check_chunks(&s, &[(0, 10)]);

        let mut buf = [0; 16];
        assert_eq!(s.read(&mut buf[..]), 10);
        assert_eq!(buf[..10], [1, 1, 1, 1, 1, 1, 3, 3, 5, 5]);
    }

    /// When chunks are added before existing data, they aren't merged.
    #[test]
    fn stream_rx_dedupe_head() {
        let mut s = RxStreamOrderer::new();

        s.inbound_frame(1, &[6; 6]).unwrap();
        check_chunks(&s, &[(1, 6)]);

        // Insertion before an existing chunk causes truncation of the new chunk.
        s.inbound_frame(0, &[7; 6]).unwrap();
        check_chunks(&s, &[(0, 1), (1, 6)]);

        // Perfect overlap with existing slices has no effect.
        s.inbound_frame(0, &[8; 7]).unwrap();
        check_chunks(&s, &[(0, 1), (1, 6)]);

        let mut buf = [0; 16];
        assert_eq!(s.read(&mut buf[..]), 7);
        assert_eq!(buf[..7], [7, 6, 6, 6, 6, 6, 6]);
    }

    #[test]
    fn stream_rx_dedupe_new_tail() {
        let mut s = RxStreamOrderer::new();

        s.inbound_frame(1, &[6; 6]).unwrap();
        check_chunks(&s, &[(1, 6)]);

        // Insertion before an existing chunk causes truncation of the new chunk.
        s.inbound_frame(0, &[7; 6]).unwrap();
        check_chunks(&s, &[(0, 1), (1, 6)]);

        // New data at the end causes the tail to be added to the first chunk,
        // replacing later chunks entirely.
        s.inbound_frame(0, &[9; 8]).unwrap();
        check_chunks(&s, &[(0, 8)]);

        let mut buf = [0; 16];
        assert_eq!(s.read(&mut buf[..]), 8);
        assert_eq!(buf[..8], [7, 9, 9, 9, 9, 9, 9, 9]);
    }

    #[test]
    fn stream_rx_dedupe_replace() {
        let mut s = RxStreamOrderer::new();

        s.inbound_frame(2, &[6; 6]).unwrap();
        check_chunks(&s, &[(2, 6)]);

        // Insertion before an existing chunk causes truncation of the new chunk.
        s.inbound_frame(1, &[7; 6]).unwrap();
        check_chunks(&s, &[(1, 1), (2, 6)]);

        // New data at the start and end replaces all the slices.
        s.inbound_frame(0, &[9; 10]).unwrap();
        check_chunks(&s, &[(0, 10)]);

        let mut buf = [0; 16];
        assert_eq!(s.read(&mut buf[..]), 10);
        assert_eq!(buf[..10], [9; 10]);
    }

    #[test]
    fn trim_retired() {
        let mut s = RxStreamOrderer::new();

        let mut buf = [0; 18];
        s.inbound_frame(0, &[1; 10]).unwrap();

        // Partially read slices are retained.
        assert_eq!(s.read(&mut buf[..6]), 6);
        check_chunks(&s, &[(0, 10)]);

        // Partially read slices are kept and so are added to.
        s.inbound_frame(3, &buf[..10]).unwrap();
        check_chunks(&s, &[(0, 13)]);

        // Wholly read pieces are dropped.
        assert_eq!(s.read(&mut buf[..]), 7);
        assert!(s.data_ranges.is_empty());

        // New data that overlaps with retired data is trimmed.
        s.inbound_frame(0, &buf[..]).unwrap();
        check_chunks(&s, &[(13, 5)]);
    }

    #[test]
    fn stream_flowc_update() {
        let mut s = create_stream(1024 * to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
        let mut buf = vec![0u8; INITIAL_LOCAL_MAX_STREAM_DATA + 100]; // Make it overlarge

        assert!(!s.has_frames_to_write());
        let big_buf = vec![0; INITIAL_LOCAL_MAX_STREAM_DATA];
        s.inbound_stream_frame(false, 0, &big_buf).unwrap();
        assert!(!s.has_frames_to_write());
        assert_eq!(
            s.read(&mut buf).unwrap(),
            (INITIAL_LOCAL_MAX_STREAM_DATA, false)
        );
        assert!(!s.data_ready());

        // flow msg generated!
        assert!(s.has_frames_to_write());

        // consume it
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut token = recovery::Tokens::new();
        s.write_frame(
            &mut builder,
            &mut token,
            &mut FrameStats::default(),
            now(),
            Duration::from_millis(100),
        );

        // it should be gone
        assert!(!s.has_frames_to_write());
    }

    fn create_stream(session_fc: u64) -> RecvStream {
        let conn_events = ConnectionEvents::default();
        RecvStream::new(
            StreamId::from(67),
            to_u64(INITIAL_LOCAL_MAX_STREAM_DATA),
            Rc::new(RefCell::new(ReceiverFlowControl::new((), session_fc))),
            conn_events,
        )
    }

    #[test]
    fn stream_max_stream_data() {
        let mut s = create_stream(1024 * to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
        assert!(!s.has_frames_to_write());
        let big_buf = vec![0; INITIAL_LOCAL_MAX_STREAM_DATA];
        s.inbound_stream_frame(false, 0, &big_buf).unwrap();
        s.inbound_stream_frame(false, to_u64(INITIAL_LOCAL_MAX_STREAM_DATA), &[1; 1])
            .unwrap_err();
    }

    #[test]
    fn reject_unbounded_fragmentation() {
        for descending in [false, true] {
            let mut s = create_stream(1024 * to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
            let count = to_u64(RxStreamOrderer::MAX_GAPS) * 4;
            let rejected = (0..count).find_map(|i| {
                let offset = if descending {
                    2 * (count - i)
                } else {
                    2 * i + 1
                };
                s.inbound_stream_frame(false, offset, &[0u8; 1]).err()
            });
            assert_eq!(rejected, Some(Error::ProtocolViolation));
            let buf = s.state.recv_buf().unwrap();
            let span =
                expect_usize((buf.end - buf.retired) / to_u64(RxStreamOrderer::RANGE_TARGET));
            assert_eq!(buf.data_ranges.len(), RxStreamOrderer::MAX_GAPS + span + 1);
        }
    }

    #[test]
    fn overlap_does_not_inflate_allowance() {
        let mut s = RxStreamOrderer::new();
        s.inbound_frame(0, &[1; 1]).unwrap();
        s.inbound_frame(2, &[2; 1]).unwrap();
        // Spans both of the above, so their bytes are counted a second time.
        s.inbound_frame(0, &[3; 4]).unwrap();

        let mut buf = [0; 4];
        assert_eq!(s.read(&mut buf), 4);
        assert!(s.data_ranges.is_empty());
        assert_eq!(s.end - s.retired, 0, "nothing held, so no allowance");
        assert_eq!(s.received - s.retired, 1);
    }

    #[test]
    fn accept_all_adjacent_data() {
        const TARGET: usize = RxStreamOrderer::RANGE_TARGET;
        let mut s = RxStreamOrderer::new();
        let payload = vec![0u8; TARGET];
        for i in 0..RxStreamOrderer::MAX_GAPS + 2 {
            s.inbound_frame(to_u64(i * TARGET), &payload).unwrap();
        }
        // Each frame is already at `RANGE_TARGET`, so none of them extend the one before.
        assert_eq!(s.data_ranges.len(), RxStreamOrderer::MAX_GAPS + 2);
        assert_eq!(s.count_runs(), 1);
    }

    #[test]
    fn gap_fills_are_coalesced() {
        let mut s = RxStreamOrderer::new();
        for i in 0..to_u64(RxStreamOrderer::MAX_GAPS) * 2 {
            s.inbound_frame(2 * i + 1, &[0u8; 1]).unwrap();
            s.inbound_frame(2 * i, &[0u8; 1]).unwrap();
        }
        assert_eq!(s.count_runs(), 1, "all of it is one contiguous run");
        // Each fill absorbs the range that follows it, so a run grows to exactly `RANGE_TARGET`
        // before the next one starts. That is 16384 bytes in 5 entries, not one entry per gap.
        check_chunks(
            &s,
            &[(0, 1), (1, 4096), (4097, 4096), (8193, 4096), (12289, 4095)],
        );
    }

    #[test]
    fn stream_orderer_bytes_ready() {
        let mut rx_ord = RxStreamOrderer::new();

        rx_ord.inbound_frame(0, &[1; 6]).unwrap();
        assert_eq!(rx_ord.bytes_ready(), 6);
        assert_eq!(rx_ord.buffered(), 6);
        assert_eq!(rx_ord.retired(), 0);

        // read some so there's an offset into the first frame
        let mut buf = [0u8; 10];
        rx_ord.read(&mut buf[..2]);
        assert_eq!(rx_ord.bytes_ready(), 4);
        assert_eq!(rx_ord.buffered(), 4);
        assert_eq!(rx_ord.retired(), 2);

        // an overlapping frame
        rx_ord.inbound_frame(5, &[2; 6]).unwrap();
        assert_eq!(rx_ord.bytes_ready(), 9);
        assert_eq!(rx_ord.buffered(), 9);
        assert_eq!(rx_ord.retired(), 2);

        // a noncontig frame
        rx_ord.inbound_frame(20, &[3; 6]).unwrap();
        assert_eq!(rx_ord.bytes_ready(), 9);
        assert_eq!(rx_ord.buffered(), 15);
        assert_eq!(rx_ord.retired(), 2);

        // an old frame
        rx_ord.inbound_frame(0, &[4; 2]).unwrap();
        assert_eq!(rx_ord.bytes_ready(), 9);
        assert_eq!(rx_ord.buffered(), 15);
        assert_eq!(rx_ord.retired(), 2);
    }

    #[test]
    fn no_stream_flowc_event_after_exiting_recv() {
        let mut s = create_stream(1024 * to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
        let mut buf = vec![0; INITIAL_LOCAL_MAX_STREAM_DATA];
        // Write from buf at first.
        s.inbound_stream_frame(false, 0, &buf).unwrap();
        // Then read into it.
        s.read(&mut buf).unwrap();
        assert!(s.has_frames_to_write());
        s.inbound_stream_frame(true, to_u64(INITIAL_LOCAL_MAX_STREAM_DATA), &[])
            .unwrap();
        assert!(!s.has_frames_to_write());
    }

    fn create_stream_with_fc(
        session_fc: Rc<RefCell<ReceiverFlowControl<()>>>,
        fc_limit: u64,
    ) -> RecvStream {
        RecvStream::new(
            StreamId::from(567),
            fc_limit,
            session_fc,
            ConnectionEvents::default(),
        )
    }

    fn create_stream_session_flow_control() -> (RecvStream, Rc<RefCell<ReceiverFlowControl<()>>>) {
        const _: () = assert!(INITIAL_LOCAL_MAX_STREAM_DATA > SESSION_WINDOW);
        let session_fc = Rc::new(RefCell::new(ReceiverFlowControl::new(
            (),
            to_u64(SESSION_WINDOW),
        )));
        (
            create_stream_with_fc(
                Rc::clone(&session_fc),
                to_u64(INITIAL_LOCAL_MAX_STREAM_DATA),
            ),
            session_fc,
        )
    }

    #[test]
    fn session_flow_control() {
        let (mut s, session_fc) = create_stream_session_flow_control();

        s.inbound_stream_frame(false, 0, &[0; SESSION_WINDOW])
            .unwrap();
        assert!(!session_fc.borrow().frame_needed());
        // The buffer is big enough to hold SESSION_WINDOW, this will make sure that we always
        // read everything from he stream.
        let mut buf = [0; 2 * SESSION_WINDOW];
        s.read(&mut buf).unwrap();
        assert!(session_fc.borrow().frame_needed());
        // consume it
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut token = recovery::Tokens::new();
        session_fc.borrow_mut().write_frames(
            &mut builder,
            &mut token,
            &mut FrameStats::default(),
            now(),
            Duration::from_millis(100),
        );

        // Switch to SizeKnown state
        s.inbound_stream_frame(true, 2 * to_u64(SESSION_WINDOW) - 1, &[0])
            .unwrap();
        assert!(!session_fc.borrow().frame_needed());
        // Receive new data that can be read.
        s.inbound_stream_frame(false, to_u64(SESSION_WINDOW), &[0; SESSION_WINDOW / 2 + 1])
            .unwrap();
        assert!(!session_fc.borrow().frame_needed());
        s.read(&mut buf).unwrap();
        assert!(session_fc.borrow().frame_needed());
        // consume it
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut token = recovery::Tokens::new();
        session_fc.borrow_mut().write_frames(
            &mut builder,
            &mut token,
            &mut FrameStats::default(),
            now(),
            Duration::from_millis(100),
        );

        // Test DataRecvd state
        let session_fc = Rc::new(RefCell::new(ReceiverFlowControl::new(
            (),
            to_u64(SESSION_WINDOW),
        )));
        let mut s = RecvStream::new(
            StreamId::from(567),
            to_u64(INITIAL_LOCAL_MAX_STREAM_DATA),
            Rc::clone(&session_fc),
            ConnectionEvents::default(),
        );

        s.inbound_stream_frame(true, 0, &[0; SESSION_WINDOW])
            .unwrap();
        assert!(!session_fc.borrow().frame_needed());
        s.read(&mut buf).unwrap();
        assert!(session_fc.borrow().frame_needed());
    }

    #[test]
    fn session_flow_control_reset() {
        let (mut s, session_fc) = create_stream_session_flow_control();

        s.inbound_stream_frame(false, 0, &[0; SESSION_WINDOW / 2])
            .unwrap();
        assert!(!session_fc.borrow().frame_needed());

        s.reset(Error::None.code(), to_u64(SESSION_WINDOW), 0)
            .unwrap();
        assert!(session_fc.borrow().frame_needed());
    }

    fn check_fc<T: Debug>(fc: &ReceiverFlowControl<T>, consumed: u64, retired: u64) {
        assert_eq!(fc.consumed(), consumed);
        assert_eq!(fc.retired(), retired);
    }

    /// Test consuming the flow control in `RecvStreamState::Recv`
    #[test]
    fn fc_state_recv_1() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();

        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s.fc().unwrap(), SW / 4, 0);
    }

    /// Test consuming the flow control in `RecvStreamState::Recv`
    /// with multiple streams
    #[test]
    fn fc_state_recv_2() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s1 = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        let mut s2 = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s1.fc().unwrap(), 0, 0);
        check_fc(s2.fc().unwrap(), 0, 0);

        s1.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();

        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s1.fc().unwrap(), SW / 4, 0);
        check_fc(s2.fc().unwrap(), 0, 0);

        s2.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();

        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s1.fc().unwrap(), SW / 4, 0);
        check_fc(s2.fc().unwrap(), SW / 4, 0);
    }

    /// Test retiring the flow control in `RecvStreamState::Recv`
    /// with multiple streams
    #[test]
    fn fc_state_recv_3() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s1 = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        let mut s2 = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s1.fc().unwrap(), 0, 0);
        check_fc(s2.fc().unwrap(), 0, 0);

        s1.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();
        s2.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s1.fc().unwrap(), SW / 4, 0);
        check_fc(s2.fc().unwrap(), SW / 4, 0);

        // Read data
        let mut buf = [1; SW_US];
        assert_eq!(s1.read(&mut buf).unwrap(), (SW_US / 4, false));
        check_fc(&fc.borrow(), SW / 2, SW / 4);
        check_fc(s1.fc().unwrap(), SW / 4, SW / 4);
        check_fc(s2.fc().unwrap(), SW / 4, 0);

        assert_eq!(s2.read(&mut buf).unwrap(), (SW_US / 4, false));
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s1.fc().unwrap(), SW / 4, SW / 4);
        check_fc(s2.fc().unwrap(), SW / 4, SW / 4);

        // Read when there is no more date to be read will not change fc.
        assert_eq!(s1.read(&mut buf).unwrap(), (0, false));
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s1.fc().unwrap(), SW / 4, SW / 4);
        check_fc(s2.fc().unwrap(), SW / 4, SW / 4);

        // Receiving more data on a stream.
        s1.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW * 3 / 4, SW / 2);
        check_fc(s1.fc().unwrap(), SW / 2, SW / 4);
        check_fc(s2.fc().unwrap(), SW / 4, SW / 4);

        // Read data
        assert_eq!(s1.read(&mut buf).unwrap(), (SW_US / 4, false));
        check_fc(&fc.borrow(), SW * 3 / 4, SW * 3 / 4);
        check_fc(s1.fc().unwrap(), SW / 2, SW / 2);
        check_fc(s2.fc().unwrap(), SW / 4, SW / 4);
    }

    /// Test consuming the flow control in `RecvStreamState::Recv` - duplicate data
    #[test]
    fn fc_state_recv_4() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(false, 0, &[0; SW_US / 4]).unwrap();

        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s.fc().unwrap(), SW / 4, 0);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(false, 0, &[0; SW_US / 8]).unwrap();
        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s.fc().unwrap(), SW / 4, 0);
    }

    /// Test consuming the flow control in `RecvStreamState::Recv` - filling a gap in the
    /// data stream.
    #[test]
    fn fc_state_recv_5() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        // Receive out of order data.
        s.inbound_stream_frame(false, SW / 8, &[0; SW_US / 8])
            .unwrap();
        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s.fc().unwrap(), SW / 4, 0);

        // Filling in the gap will not change fc.
        s.inbound_stream_frame(false, 0, &[0; SW_US / 8]).unwrap();
        check_fc(&fc.borrow(), SW / 4, 0);
        check_fc(s.fc().unwrap(), SW / 4, 0);
    }

    /// Test consuming the flow control in `RecvStreamState::Recv` - receiving frame past
    /// the flow control will cause an error.
    #[test]
    fn fc_state_recv_6() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));
        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);

        // Receiving frame past the flow control will cause an error.
        assert_eq!(
            s.inbound_stream_frame(false, 0, &[0; SW_US * 3 / 4 + 1]),
            Err(Error::FlowControl)
        );
    }

    /// Test that the flow controls will send updates.
    #[expect(clippy::too_many_lines, reason = "This is test code.")]
    #[test]
    fn fc_state_recv_7() {
        const CONNECTION_WINDOW_US: usize = 1024;
        const CONNECTION_WINDOW: u64 = to_u64(CONNECTION_WINDOW_US);

        const STREAM_WINDOW_US: usize = CONNECTION_WINDOW_US / 2;
        const STREAM_WINDOW: u64 = to_u64(STREAM_WINDOW_US);

        const _: () = assert!(WINDOW_UPDATE_FRACTION <= to_u64(usize::MAX));
        #[expect(
            clippy::cast_possible_truncation,
            reason = "value is statically checked"
        )]
        const WINDOW_UPDATE_FRACTION_US: usize = WINDOW_UPDATE_FRACTION as usize;

        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new(
            (),
            CONNECTION_WINDOW,
        )));
        let mut s = create_stream_with_fc(Rc::clone(&fc), STREAM_WINDOW);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        // Receive data up to but not over the fc update trigger point.
        s.inbound_stream_frame(false, 0, &[0; STREAM_WINDOW_US / WINDOW_UPDATE_FRACTION_US])
            .unwrap();
        let mut buf = [1; CONNECTION_WINDOW_US];
        assert_eq!(
            s.read(&mut buf).unwrap(),
            (STREAM_WINDOW_US / WINDOW_UPDATE_FRACTION_US, false)
        );
        check_fc(
            &fc.borrow(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
        );
        check_fc(
            s.fc().unwrap(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
        );

        // Still no fc update needed.
        assert!(!fc.borrow().frame_needed());
        assert!(!s.fc().unwrap().frame_needed());

        // Receive one more byte that will cause a fc update after it is read.
        s.inbound_stream_frame(false, STREAM_WINDOW / WINDOW_UPDATE_FRACTION, &[0])
            .unwrap();
        check_fc(
            &fc.borrow(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
        );
        check_fc(
            s.fc().unwrap(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
        );
        // Only consuming data does not cause a fc update to be sent.
        assert!(!fc.borrow().frame_needed());
        assert!(!s.fc().unwrap().frame_needed());

        assert_eq!(s.read(&mut buf).unwrap(), (1, false));
        check_fc(
            &fc.borrow(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
        );
        check_fc(
            s.fc().unwrap(),
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION + 1,
        );
        // Data are retired and the stream fc will send an update.
        assert!(!fc.borrow().frame_needed());
        assert!(s.fc().unwrap().frame_needed());

        // Receive more data to increase fc further.
        s.inbound_stream_frame(
            false,
            STREAM_WINDOW / WINDOW_UPDATE_FRACTION,
            &[0; STREAM_WINDOW_US / WINDOW_UPDATE_FRACTION_US],
        )
        .unwrap();
        assert_eq!(
            s.read(&mut buf).unwrap(),
            (STREAM_WINDOW_US / WINDOW_UPDATE_FRACTION_US - 1, false)
        );
        check_fc(
            &fc.borrow(),
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION,
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION,
        );
        check_fc(
            s.fc().unwrap(),
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION,
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION,
        );
        assert!(!fc.borrow().frame_needed());
        assert!(s.fc().unwrap().frame_needed());

        // Write the fc update frame
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut token = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        fc.borrow_mut().write_frames(
            &mut builder,
            &mut token,
            &mut stats,
            now(),
            Duration::from_millis(100),
        );
        assert_eq!(stats.max_data, 0);
        s.write_frame(
            &mut builder,
            &mut token,
            &mut stats,
            now(),
            Duration::from_millis(100),
        );
        assert_eq!(stats.max_stream_data, 1);

        // Receive 1 byte that will cause a session fc update after it is read.
        s.inbound_stream_frame(false, STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION, &[0])
            .unwrap();
        assert_eq!(s.read(&mut buf).unwrap(), (1, false));
        check_fc(
            &fc.borrow(),
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION + 1,
        );
        check_fc(
            s.fc().unwrap(),
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION + 1,
            STREAM_WINDOW * 2 / WINDOW_UPDATE_FRACTION + 1,
        );
        assert!(fc.borrow().frame_needed());
        assert!(!s.fc().unwrap().frame_needed());
        fc.borrow_mut().write_frames(
            &mut builder,
            &mut token,
            &mut stats,
            now(),
            Duration::from_millis(100),
        );
        assert_eq!(stats.max_data, 1);
        s.write_frame(
            &mut builder,
            &mut token,
            &mut stats,
            now(),
            Duration::from_millis(100),
        );
        assert_eq!(stats.max_stream_data, 1);
    }

    /// Test flow control in `RecvStreamState::SizeKnown`
    #[test]
    fn fc_state_size_known() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Receiving frame past the final size of a stream will return an error.
        assert_eq!(
            s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4 + 1]),
            Err(Error::FinalSize)
        );
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Add new data to the gap will not change fc.
        s.inbound_stream_frame(false, SW / 8, &[0; SW_US / 8])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Fill the gap
        s.inbound_stream_frame(false, 0, &[0; SW_US / 8]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Read all data
        let mut buf = [1; SW_US];
        assert_eq!(s.read(&mut buf).unwrap(), (SW_US / 2, true));
        // the stream does not have fc any more. We can only check the session fc.
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        assert!(s.fc().is_none());
    }

    /// Test flow control in `RecvStreamState::DataRecvd`
    #[test]
    fn fc_state_data_recv() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW);

        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(true, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Receiving frame past the final size of a stream will return an error.
        assert_eq!(
            s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4 + 1]),
            Err(Error::FinalSize)
        );
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        // Read all data
        let mut buf = [1; SW_US];
        assert_eq!(s.read(&mut buf).unwrap(), (SW_US / 2, true));
        // the stream does not have fc any more. We can only check the session fc.
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        assert!(s.fc().is_none());
    }

    /// Test flow control in `RecvStreamState::DataRead`
    #[test]
    fn fc_state_data_read() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(true, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        let mut buf = [1; SW_US];
        assert_eq!(s.read(&mut buf).unwrap(), (SW_US / 2, true));
        // the stream does not have fc any more. We can only check the session fc.
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        assert!(s.fc().is_none());

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(true, 0, &[0; SW_US / 2]).unwrap();
        // the stream does not have fc any more. We can only check the session fc.
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        assert!(s.fc().is_none());

        // Receiving frame past the final size of a stream or the stream's fc limit
        // will NOT return an error.
        s.inbound_stream_frame(true, 0, &[0; SW_US / 2 + 1])
            .unwrap();
        s.inbound_stream_frame(true, 0, &[0; SW_US * 3 / 4 + 1])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        assert!(s.fc().is_none());
    }

    /// Test flow control in `RecvStreamState::AbortReading` and final size is known
    #[test]
    fn fc_state_abort_reading_1() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        assert!(!s.stop_sending(Error::None.code()));
        // All data will de retired
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(true, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving frame past the final size of a stream will return an error.
        assert_eq!(
            s.inbound_stream_frame(true, SW / 4, &[0; SW_US / 4 + 1]),
            Err(Error::FinalSize)
        );
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);
    }

    /// Test flow control in `RecvStreamState::AbortReading` and final size is unknown
    #[test]
    fn fc_state_abort_reading_2() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(false, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        assert!(!s.stop_sending(Error::None.code()));
        // All data will de retired
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(false, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving data past the flow control limit will cause an error.
        assert_eq!(
            s.inbound_stream_frame(false, 0, &[0; SW_US * 3 / 4 + 1]),
            Err(Error::FlowControl)
        );

        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving more data will case the data to be retired.
        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 2, &[0; 10]).unwrap();
        check_fc(&fc.borrow(), SW / 2 + 10, SW / 2 + 10);
        check_fc(s.fc().unwrap(), SW / 2 + 10, SW / 2 + 10);

        // We can still receive the final size.
        s.inbound_stream_frame(true, SW / 2, &[0; 20]).unwrap();
        check_fc(&fc.borrow(), SW / 2 + 20, SW / 2 + 20);
        check_fc(s.fc().unwrap(), SW / 2 + 20, SW / 2 + 20);

        // Receiving frame past the final size of a stream will return an error.
        assert_eq!(
            s.inbound_stream_frame(true, SW / 2, &[0; 21]),
            Err(Error::FinalSize)
        );
        check_fc(&fc.borrow(), SW / 2 + 20, SW / 2 + 20);
        check_fc(s.fc().unwrap(), SW / 2 + 20, SW / 2 + 20);
    }

    /// Test flow control in `RecvStreamState::WaitForReset`
    #[test]
    fn fc_state_wait_for_reset() {
        const SW: u64 = 1024;
        const SW_US: usize = 1024;
        let fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), SW)));

        let mut s = create_stream_with_fc(Rc::clone(&fc), SW * 3 / 4);
        check_fc(&fc.borrow(), 0, 0);
        check_fc(s.fc().unwrap(), 0, 0);

        s.inbound_stream_frame(false, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, 0);
        check_fc(s.fc().unwrap(), SW / 2, 0);

        assert!(!s.stop_sending(Error::None.code()));
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        assert!(!s.stop_sending_acked());
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving duplicate frames (already consumed data) will not cause an error or
        // change fc.
        s.inbound_stream_frame(false, 0, &[0; SW_US / 2]).unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving data past the flow control limit will cause an error.
        assert_eq!(
            s.inbound_stream_frame(false, 0, &[0; SW_US * 3 / 4 + 1]),
            Err(Error::FlowControl)
        );

        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 4, &[0; SW_US / 4])
            .unwrap();
        check_fc(&fc.borrow(), SW / 2, SW / 2);
        check_fc(s.fc().unwrap(), SW / 2, SW / 2);

        // Receiving more data will case the data to be retired.
        // The stream can still receive duplicate data without a fin bit.
        s.inbound_stream_frame(false, SW / 2, &[0; 10]).unwrap();
        check_fc(&fc.borrow(), SW / 2 + 10, SW / 2 + 10);
        check_fc(s.fc().unwrap(), SW / 2 + 10, SW / 2 + 10);
    }

    // --- RESET_STREAM_AT (reliable stream reset) receive side ---

    const RR_STREAM: StreamId = StreamId::new(67);

    fn reliable_recv_stream(events: ConnectionEvents) -> RecvStream {
        RecvStream::new(
            RR_STREAM,
            INITIAL_LOCAL_MAX_STREAM_DATA as u64,
            Rc::new(RefCell::new(ReceiverFlowControl::new((), 1024 * 1024))),
            events,
        )
    }

    fn reset_count(events: &mut ConnectionEvents) -> usize {
        events
            .events()
            .filter(|e| {
                matches!(e, ConnectionEvent::RecvStreamReset { stream_id, .. }
                if *stream_id == RR_STREAM)
            })
            .count()
    }

    /// `RxStreamOrderer::discard_after` drops whole ranges beyond the offset and truncates a
    /// straddling range, leaving the `end` invariant intact for later frames.
    #[test]
    fn orderer_discard_after() {
        let mut o = RxStreamOrderer::new();
        o.inbound_frame(0, &[1; 10]).unwrap();
        o.discard_after(4);
        // Only `[0, 4)` remains readable.
        let mut buf = [0; 16];
        assert_eq!(o.read(&mut buf), 4);

        // A later frame entirely beyond the discard point still slots in correctly.
        let mut o = RxStreamOrderer::new();
        o.inbound_frame(0, &[1; 4]).unwrap();
        o.inbound_frame(8, &[2; 4]).unwrap(); // gap at [4,8)
        o.discard_after(6); // drops [8,12), keeps [0,4)
        o.inbound_frame(4, &[3; 2]).unwrap(); // fills [4,6)
        assert_eq!(o.read(&mut buf), 6);

        // The end marker is correctly maintained when the discard empties it out.
        let mut o = RxStreamOrderer::new();
        o.inbound_frame(0, &[1; 4]).unwrap();
        assert_eq!(o.read(&mut buf), 4);
        o.inbound_frame(8, &[2; 4]).unwrap(); // gap at [4,8)
        o.discard_after(6); // drops [8,12), keeps [0,4)
        o.inbound_frame(4, &[3; 2]).unwrap(); // fills [4,6)
        assert_eq!(o.read(&mut buf), 2);
    }

    /// Happy path: receive all data, then `RESET_STREAM_AT`; only the reliable prefix is
    /// delivered, and the reset is surfaced once it has been read.
    #[test]
    fn reset_at_delivers_prefix_then_resets() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        s.inbound_stream_frame(false, 0, &[0x42; 10]).unwrap();

        assert!(s.reset(7, 10, 4).is_ok());
        assert!(!s.is_ended());
        assert!(matches!(s.state, RecvStreamState::SizeKnownAt { .. }));
        assert_eq!(reset_count(&mut events), 0);

        // Only `[0, 4)` is delivered; no FIN, and the bytes beyond `reliable_size` are gone.
        let mut buf = [0; 64];
        assert_eq!(s.read(&mut buf).unwrap(), (4, false));
        // Reading drained the prefix → reset surfaced, stream ended.
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
        assert_eq!(s.read(&mut buf).unwrap_err(), Error::NoMoreData);
    }

    /// `RESET_STREAM` (`reliable_size == 0`) completes immediately.
    #[test]
    fn reset_at_zero_completes_immediately() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        s.inbound_stream_frame(false, 0, &[0x42; 10]).unwrap();
        assert!(s.reset(7, 10, 0).is_ok());
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
    }

    /// The reset waits for the reliable prefix to arrive (reordering) and be read.
    #[test]
    fn reset_at_waits_for_prefix() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        // RESET_STREAM_AT arrives before the committed data.
        assert!(s.reset(7, 8, 8).is_ok());
        assert!(matches!(s.state, RecvStreamState::SizeKnownAt { .. }));

        // Partial prefix: read what's there, not yet complete.
        s.inbound_stream_frame(false, 0, &[0x42; 4]).unwrap();
        let mut buf = [0; 64];
        assert_eq!(s.read(&mut buf).unwrap(), (4, false));
        assert!(!s.is_ended());
        assert_eq!(reset_count(&mut events), 0);

        // Deliver the remainder.
        s.inbound_stream_frame(false, 4, &[0x42; 4]).unwrap();
        assert_eq!(s.read(&mut buf).unwrap(), (4, false));
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
    }

    /// `reliable_size > final_size` is rejected with a frame-encoding error.
    #[test]
    fn reset_at_reliable_exceeds_final() {
        let mut s = reliable_recv_stream(ConnectionEvents::default());
        assert_eq!(s.reset(7, 4, 8).unwrap_err(), Error::FrameEncoding);
    }

    /// A later frame changing the final size is a `FINAL_SIZE_ERROR`.
    #[test]
    fn reset_at_changed_final_size() {
        let mut s = reliable_recv_stream(ConnectionEvents::default());
        assert!(s.reset(7, 10, 4).is_ok());
        assert_eq!(s.reset(7, 12, 4).unwrap_err(), Error::FinalSize);
    }

    /// A later frame changing the error code is a `STREAM_STATE_ERROR`.
    #[test]
    fn reset_at_changed_error_code() {
        let mut s = reliable_recv_stream(ConnectionEvents::default());
        assert!(s.reset(7, 10, 4).is_ok());
        assert_eq!(s.reset(9, 10, 4).unwrap_err(), Error::StreamState);
    }

    /// `reliable_size` may be reduced (dropping newly-excess data) but increases are ignored.
    #[test]
    fn reset_at_reduce_and_ignore_increase() {
        let mut s = reliable_recv_stream(ConnectionEvents::default());
        s.inbound_stream_frame(false, 0, &[0x42; 10]).unwrap();
        assert!(s.reset(7, 10, 8).is_ok());

        // An increase is ignored.
        assert!(s.reset(7, 10, 9).is_ok());
        // A reduction drops the newly-excess data.
        assert!(s.reset(7, 10, 4).is_ok());

        let mut buf = [0; 64];
        // Only `[0, 4)` survives.
        assert_eq!(s.read(&mut buf).unwrap(), (4, false));
        assert!(s.is_ended());
    }

    /// A plain `RESET_STREAM` is handled correctly after receiving `RESET_STREAM_AT`.
    #[test]
    fn reset_at_canceled_by_plain_reset() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        s.inbound_stream_frame(false, 0, &[0x42; 10]).unwrap();
        assert!(s.reset(7, 10, 8).is_ok());
        assert!(matches!(s.state, RecvStreamState::SizeKnownAt { .. }));
        assert_eq!(reset_count(&mut events), 0);

        assert!(s.reset(7, 10, 0).is_ok());
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
    }

    /// After `STOP_SENDING`, a `RESET_STREAM_AT` ignores `reliable_size` and ends promptly.
    #[test]
    fn reset_at_after_stop_sending() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        s.inbound_stream_frame(false, 0, &[0x42; 4]).unwrap();
        assert!(!s.stop_sending(9));
        assert!(s.reset(7, 10, 8).is_ok());
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
    }

    /// `STOP_SENDING` while delivering a reliable prefix abandons it and ends promptly.
    #[test]
    fn stop_sending_in_size_known_at() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_recv_stream(events.clone());
        s.inbound_stream_frame(false, 0, &[0x42; 10]).unwrap();
        assert!(s.reset(7, 10, 8).is_ok());
        assert!(matches!(s.state, RecvStreamState::SizeKnownAt { .. }));

        assert!(s.stop_sending(9)); // ends the stream
        assert!(s.is_ended());
        assert_eq!(reset_count(&mut events), 1);
    }

    /// A reliable reset releases all of the stream's flow control once complete.
    #[test]
    fn reset_at_releases_flow_control() {
        const FC_LIMIT: u64 = 1024;

        let session_fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), FC_LIMIT)));
        let mut s = create_stream_with_fc(Rc::clone(&session_fc), FC_LIMIT);
        s.inbound_stream_frame(false, 0, &[0x42; 100]).unwrap();
        // Reliable size 40, final size 100: the [40,100) tail is dropped.
        assert!(s.reset(7, 100, 40).is_ok());

        let mut buf = [0; 256];
        assert_eq!(s.read(&mut buf).unwrap(), (40, false));
        assert!(s.is_ended());
        // All 100 bytes of session flow control are retired (40 read + 60 dropped tail).
        check_fc(&session_fc.borrow(), 100, 100);

        // Doing this again without reading retires the dropped tail [40,100) immediately; the
        // still-unread prefix is retired only when the read side is later abandoned.
        let mut s = create_stream_with_fc(Rc::clone(&session_fc), FC_LIMIT);
        assert!(s.reset(7, 100, 40).is_ok());
        check_fc(&session_fc.borrow(), 200, 160);
        assert!(s.stop_sending(9));
        check_fc(&session_fc.borrow(), 200, 200);
    }

    /// The undeliverable tail's flow control is returned immediately on a reliable reset, before
    /// the application reads the prefix.
    #[test]
    fn reset_releases_tail_flow_control_immediately() {
        const FC_LIMIT: u64 = 1024;
        let session_fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), FC_LIMIT)));
        let mut s = create_stream_with_fc(Rc::clone(&session_fc), FC_LIMIT);
        s.inbound_stream_frame(false, 0, &[0x42; 100]).unwrap();

        // Reliable size 40, final size 100: the [40,100) tail is retired right away, even though
        // the 40-byte prefix has not been read yet.
        assert!(s.reset(7, 100, 40).is_ok());
        check_fc(&session_fc.borrow(), 100, 60);

        // Reading the prefix retires the rest.
        let mut buf = [0; 256];
        assert_eq!(s.read(&mut buf).unwrap(), (40, false));
        assert!(s.is_ended());
        check_fc(&session_fc.borrow(), 100, 100);
    }

    /// Reducing `reliable_size` with a later frame returns credit for the newly-dropped range.
    #[test]
    fn reset_reduce_releases_more_flow_control() {
        const FC_LIMIT: u64 = 1024;
        let session_fc = Rc::new(RefCell::new(ReceiverFlowControl::new((), FC_LIMIT)));
        let mut s = create_stream_with_fc(Rc::clone(&session_fc), FC_LIMIT);
        s.inbound_stream_frame(false, 0, &[0x42; 100]).unwrap();

        // The difference between reliable (80) and final (100) sizes is retired.
        assert!(s.reset(7, 100, 80).is_ok());
        check_fc(&session_fc.borrow(), 100, 20);

        // Increases are ignored.
        assert!(s.reset(7, 100, 90).is_ok());
        check_fc(&session_fc.borrow(), 100, 20);

        // Only the reduction is retired.
        assert!(s.reset(7, 100, 40).is_ok());
        check_fc(&session_fc.borrow(), 100, 60);

        let mut buf = [0; 256];
        assert_eq!(s.read(&mut buf).unwrap(), (40, false));
        assert!(s.is_ended());
        check_fc(&session_fc.borrow(), 100, 100);
    }
}
