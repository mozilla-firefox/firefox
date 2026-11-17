// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Buffering data to send until it is acked.

use std::{
    cell::RefCell,
    cmp::{Ordering, max, min},
    collections::{BTreeMap, VecDeque, btree_map::Entry},
    fmt::{self, Display, Formatter},
    mem,
    num::NonZeroUsize,
    ops::Add,
    rc::Rc,
};

use indexmap::IndexMap;
use neqo_common::{Buffer, Encoder, Role, expect_usize, to_u64};
use rustc_hash::FxBuildHasher;
use smallvec::SmallVec;
use tracing::{debug, error, trace};

use crate::{
    AppError, Error, MAX_LOCAL_MAX_STREAM_DATA, Res,
    events::ConnectionEvents,
    fc::SenderFlowControl,
    frame::{Frame, FrameEncoder as _, FrameType},
    packet,
    recovery::{self, StreamRecoveryToken},
    stats::FrameStats,
    stream_id::StreamId,
    streams::{NULL_GROUP_ID, SendGroupId, SendOrder},
    tparams::{
        TransportParameterId::{InitialMaxStreamDataBidiRemote, InitialMaxStreamDataUni},
        TransportParameters,
    },
};

/// The priority that is assigned to sending data for the stream.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, PartialOrd, Ord)]
pub enum TransmissionPriority {
    /// This stream is more important than the functioning of the connection.
    /// Don't use this priority unless the stream really is that important.
    /// A stream at this priority can starve out other connection functions,
    /// including flow control, which could be very bad.
    Critical,
    /// The stream is very important.  Stream data will be written ahead of
    /// some of the less critical connection functions, like path validation,
    /// connection ID management, and session tickets.
    Important,
    /// High priority streams are important, but not enough to disrupt
    /// connection operation.  They go ahead of session tickets though.
    High,
    /// The default priority.
    #[default]
    Normal,
    /// Low priority streams get sent last.
    Low,
}

impl Add<RetransmissionPriority> for TransmissionPriority {
    type Output = Self;
    fn add(self, rhs: RetransmissionPriority) -> Self::Output {
        match rhs {
            RetransmissionPriority::Fixed(fixed) => fixed,
            RetransmissionPriority::Same => self,
            RetransmissionPriority::Higher => match self {
                Self::Critical => Self::Critical,
                Self::Important | Self::High => Self::Important,
                Self::Normal => Self::High,
                Self::Low => Self::Normal,
            },
            RetransmissionPriority::MuchHigher => match self {
                Self::Critical | Self::Important => Self::Critical,
                Self::High | Self::Normal => Self::Important,
                Self::Low => Self::High,
            },
        }
    }
}

/// If data is lost, this determines the priority that applies to retransmissions
/// of that data.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub enum RetransmissionPriority {
    /// Prioritize retransmission at a fixed priority.
    /// With this, it is possible to prioritize retransmissions lower than transmissions.
    /// Doing that can create a deadlock with flow control which might cause the connection
    /// to stall unless new data stops arriving fast enough that retransmissions can complete.
    Fixed(TransmissionPriority),
    /// Don't increase priority for retransmission.  This is probably not a good idea
    /// as it could mean starving flow control.
    Same,
    /// Increase the priority of retransmissions (the default).
    /// Retransmissions of `Critical` or `Important` aren't elevated at all.
    #[default]
    Higher,
    /// Increase the priority of retransmissions a lot.
    /// This is useful for streams that are particularly exposed to head-of-line blocking.
    MuchHigher,
}

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
enum RangeState {
    Sent,
    Acked,
}

/// Track ranges in the stream as sent or acked. Acked implies sent. Not in a
/// range implies needing-to-be-sent, either initially or as a retransmission.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct RangeTracker {
    /// The number of bytes that have been acknowledged starting from offset 0.
    acked: u64,
    /// A map that tracks the state of ranges.
    /// Keys are the offset of the start of the range.
    /// Values is a tuple of the range length and its state.
    used: BTreeMap<u64, (u64, RangeState)>,
    /// This is a cache for the output of `first_unmarked_range`, which we check a lot.
    first_unmarked: Option<(u64, Option<u64>)>,
}

impl RangeTracker {
    fn highest_offset(&self) -> u64 {
        self.used
            .last_key_value()
            .map_or(self.acked, |(&k, &(v, _))| k + v)
    }

    const fn acked_from_zero(&self) -> u64 {
        self.acked
    }

    /// Find the first unmarked range. If all are contiguous, this will return
    /// (`highest_offset()`, None).
    fn first_unmarked_range(&mut self) -> (u64, Option<u64>) {
        if let Some(first_unmarked) = self.first_unmarked {
            return first_unmarked;
        }

        let mut prev_end = self.acked;

        for (&cur_off, &(cur_len, _)) in &self.used {
            if prev_end == cur_off {
                prev_end = cur_off + cur_len;
            } else {
                let res = (prev_end, Some(cur_off - prev_end));
                self.first_unmarked = Some(res);
                return res;
            }
        }
        self.first_unmarked = Some((prev_end, None));
        (prev_end, None)
    }

    /// When the range of acknowledged bytes from zero increases, we need to drop any
    /// ranges within that span AND maybe extend it to include any adjacent acknowledged ranges.
    fn coalesce_acked(&mut self) {
        while let Some(e) = self.used.first_entry() {
            match self.acked.cmp(e.key()) {
                Ordering::Greater => {
                    let (off, (len, state)) = e.remove_entry();
                    let overflow = (off + len).saturating_sub(self.acked);
                    if overflow > 0 {
                        if state == RangeState::Acked {
                            self.acked += overflow;
                        } else {
                            self.used.insert(self.acked, (overflow, state));
                        }
                        break;
                    }
                }
                Ordering::Equal => {
                    if e.get().1 == RangeState::Acked {
                        let (len, _) = e.remove();
                        self.acked += len;
                    }
                    break;
                }
                Ordering::Less => break,
            }
        }
    }

    /// Mark a range as acknowledged.  This is simpler than marking a range as sent
    /// because an acknowledged range can never turn back into a sent range, so
    /// this function can just override the entire range.
    ///
    /// The only tricky parts are making sure that we maintain `self.acked`,
    /// which is the first acknowledged range.  And making sure that we don't create
    /// ranges of the same type that are adjacent; these need to be merged.
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_acked(&mut self, new_off: u64, new_len: usize) {
        let end = new_off + to_u64(new_len);
        let new_off = max(self.acked, new_off);
        let mut new_len = end.saturating_sub(new_off);
        if new_len == 0 {
            return;
        }

        self.first_unmarked = None;
        if new_off == self.acked {
            self.acked += new_len;
            self.coalesce_acked();
            return;
        }
        let mut new_end = new_off + new_len;

        // Get all existing ranges that start within this new range.
        let mut covered = self
            .used
            .range(new_off..new_end)
            .map(|(&k, _)| k)
            .collect::<SmallVec<[_; 8]>>();

        if let Entry::Occupied(next_entry) = self.used.entry(new_end) {
            // Check if the very next entry is the same type as this.
            if next_entry.get().1 == RangeState::Acked {
                // If is is acked, drop it and extend this new range.
                let (extra_len, _) = next_entry.remove();
                new_len += extra_len;
                new_end += extra_len;
            }
        } else if let Some(last) = covered.pop() {
            // Otherwise, the last of the existing ranges might overhang this one by some.
            let (old_off, (old_len, old_state)) =
                self.used.remove_entry(&last).expect("entry exists"); // can't fail
            let remainder = (old_off + old_len).saturating_sub(new_end);
            if remainder > 0 {
                if old_state == RangeState::Acked {
                    // Just extend the current range.
                    new_len += remainder;
                    new_end += remainder;
                } else {
                    self.used.insert(new_end, (remainder, RangeState::Sent));
                }
            }
        }
        // All covered ranges can just be trashed.
        for k in covered {
            self.used.remove(&k);
        }

        // Now either merge with a preceding acked range
        // or cut a preceding sent range as needed.
        let prev = self.used.range_mut(..new_off).next_back();
        if let Some((prev_off, (prev_len, prev_state))) = prev {
            let prev_end = *prev_off + *prev_len;
            if prev_end >= new_off {
                if *prev_state == RangeState::Sent {
                    *prev_len = new_off - *prev_off;
                    if prev_end > new_end {
                        // There is some extra sent range after the new acked range.
                        self.used
                            .insert(new_end, (prev_end - new_end, RangeState::Sent));
                    }
                } else {
                    *prev_len = max(prev_end, new_end) - *prev_off;
                    return;
                }
            }
        }
        self.used.insert(new_off, (new_len, RangeState::Acked));
    }

    /// Turn a single sent range into a list of subranges that align with existing
    /// acknowledged ranges.
    ///
    /// This is more complicated than adding acked ranges because any acked ranges
    /// need to be kept in place, with sent ranges filling the gaps.
    ///
    /// This means:
    /// ```ignore
    ///   AAA S AAAS AAAAA
    /// +  SSSSSSSSSSSSS
    /// = AAASSSAAASSAAAAA
    /// ```
    ///
    /// But we also have to ensure that:
    /// ```ignore
    ///     SSSS
    /// + SS
    /// = SSSSSS
    /// ```
    /// and
    /// ```ignore
    ///   SSSSS
    /// +     SS
    /// = SSSSSS
    /// ```
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_sent(&mut self, mut new_off: u64, new_len: usize) {
        let new_end = new_off + to_u64(new_len);
        new_off = max(self.acked, new_off);
        let mut new_len = new_end.saturating_sub(new_off);
        if new_len == 0 {
            return;
        }

        self.first_unmarked = None;

        // Get all existing ranges that start within this new range.
        let covered = self
            .used
            .range(new_off..(new_off + new_len))
            .map(|(&k, _)| k)
            .collect::<SmallVec<[u64; 8]>>();

        if let Entry::Occupied(next_entry) = self.used.entry(new_end)
            && next_entry.get().1 == RangeState::Sent
        {
            // Check if the very next entry is the same type as this, so it can be merged.
            let (extra_len, _) = next_entry.remove();
            new_len += extra_len;
        }

        // Merge with any preceding sent range that might overlap,
        // or cut the head of this if the preceding range is acked.
        let prev = self.used.range(..new_off).next_back();
        if let Some((&prev_off, &(prev_len, prev_state))) = prev
            && prev_off + prev_len >= new_off
        {
            let overlap = prev_off + prev_len - new_off;
            new_len = new_len.saturating_sub(overlap);
            if new_len == 0 {
                // The previous range completely covers this one (no more to do).
                return;
            }

            if prev_state == RangeState::Acked {
                // The previous range is acked, so it cuts this one.
                new_off += overlap;
            } else {
                // Extend the current range backwards.
                new_off = prev_off;
                new_len += prev_len;
                // The previous range will be updated below.
                // It might need to be cut because of a covered acked range.
            }
        }

        // Now interleave new sent chunks with any existing acked chunks.
        for old_off in covered {
            let Entry::Occupied(e) = self.used.entry(old_off) else {
                unreachable!();
            };
            let &(old_len, old_state) = e.get();
            if old_state == RangeState::Acked {
                // Now we have to insert a chunk ahead of this acked chunk.
                let chunk_len = old_off - new_off;
                if chunk_len > 0 {
                    self.used.insert(new_off, (chunk_len, RangeState::Sent));
                }
                let included = chunk_len + old_len;
                new_len = new_len.saturating_sub(included);
                if new_len == 0 {
                    return;
                }
                new_off += included;
            } else {
                let overhang = (old_off + old_len).saturating_sub(new_off + new_len);
                new_len += overhang;
                if *e.key() != new_off {
                    // Retain a sent entry at `new_off`.
                    // This avoids the work of removing and re-creating an entry.
                    // The value will be overwritten when the next insert occurs,
                    // either when this loop hits an acked range (above)
                    // or for any remainder (below).
                    e.remove();
                }
            }
        }

        self.used.insert(new_off, (new_len, RangeState::Sent));
    }

    /// Returns whether any range was actually unmarked.
    fn unmark_range(&mut self, off: u64, len: usize) -> bool {
        if len == 0 {
            debug!("unmark 0-length range at {off}");
            return false;
        }

        self.first_unmarked = None;
        let len = to_u64(len);
        let end_off = off + len;

        let mut to_remove = SmallVec::<[_; 8]>::new();
        let mut to_add = None;
        let mut unmarked = false;

        // Walk backwards through possibly affected existing ranges
        for (cur_off, (cur_len, cur_state)) in self.used.range_mut(..end_off).rev() {
            let cur_end_off = *cur_off + *cur_len;
            // Only the last range examined can start below `off`, and it keeps its head.
            let head = *cur_off < off;
            if head && cur_end_off <= off {
                break; // It does not even overlap.
            }

            if *cur_state == RangeState::Acked {
                debug!(
                    "Attempted to unmark Acked range {cur_off}-{cur_len} with unmark_range {off}-{end_off}"
                );
            } else {
                if cur_end_off > end_off {
                    // The range spans the unmarked one, so keep its tail.
                    assert_eq!(to_add, None);
                    to_add = Some((end_off, cur_end_off - end_off, *cur_state));
                }
                if head {
                    *cur_len = off - cur_off; // Cut, rather than remove.
                    unmarked = true;
                } else {
                    to_remove.push(*cur_off);
                }
            }

            if head {
                break;
            }
        }

        unmarked |= !to_remove.is_empty();
        for remove_off in to_remove {
            self.used.remove(&remove_off);
        }

        if let Some((new_cur_off, new_cur_len, cur_state)) = to_add {
            self.used.insert(new_cur_off, (new_cur_len, cur_state));
        }
        unmarked
    }

    /// Unmark all sent ranges. Returns whether any range was actually unmarked.
    /// # Panics
    /// On 32-bit machines where far too much is sent before calling this.
    /// That should not happen because this should only be called for handshakes,
    /// which never exceed that limit.
    #[must_use]
    pub fn unmark_sent(&mut self) -> bool {
        self.unmark_range(0, expect_usize(self.highest_offset()))
    }

    #[cfg(feature = "bench")]
    pub fn mark_as_lost(&mut self, off: u64, len: usize) {
        self.unmark_range(off, len);
    }
}

/// Buffer to contain queued bytes and track their state.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct TxBuffer {
    send_buf: VecDeque<u8>, // buffer of not-acked bytes
    ranges: RangeTracker,   // ranges in buffer that have been sent or acked
}

const _: () = assert!(MAX_LOCAL_MAX_STREAM_DATA <= to_u64(usize::MAX));

impl TxBuffer {
    /// The maximum stream send buffer size.
    ///
    /// See [`MAX_LOCAL_MAX_STREAM_DATA`] for an explanation of this
    /// concrete value.
    #[expect(
        clippy::cast_possible_truncation,
        reason = "the value is checked above"
    )]
    pub const MAX_SIZE: usize = MAX_LOCAL_MAX_STREAM_DATA as usize;

    #[must_use]
    pub fn new() -> Self {
        Self::default()
    }

    /// Attempt to add some or all of the passed-in buffer to the `TxBuffer`.
    pub fn send(&mut self, buf: &[u8]) -> usize {
        let can_buffer = min(Self::MAX_SIZE - self.buffered(), buf.len());
        if can_buffer > 0 {
            self.send_buf.extend(&buf[..can_buffer]);
            debug_assert!(self.send_buf.len() <= Self::MAX_SIZE);
        }
        can_buffer
    }

    fn first_unmarked_range(&mut self) -> Option<(u64, Option<u64>)> {
        let (start, maybe_len) = self.ranges.first_unmarked_range();
        let buffered = to_u64(self.buffered());
        (start != self.retired() + buffered).then_some((start, maybe_len))
    }

    pub fn is_empty(&mut self) -> bool {
        self.first_unmarked_range().is_none()
    }

    pub fn has_next_bytes(&mut self) -> bool {
        !self.is_empty()
    }

    /// Returns `true` if there are unsent bytes before `limit`.
    pub fn has_next_bytes_before(&mut self, limit: u64) -> bool {
        self.first_unmarked_range()
            .is_some_and(|(start, _)| start < limit)
    }

    pub fn next_bytes(&mut self) -> Option<(u64, &[u8])> {
        let (start, maybe_len) = self.first_unmarked_range()?;

        // Convert from ranges-relative-to-zero to
        // ranges-relative-to-buffer-start
        // Conversion is safe because the delta is bounded by send_buf size.
        let buff_off = expect_usize(start - self.retired());

        // Deque returns two slices. Create a subslice from whichever
        // one contains the first unmarked data.
        let slc = if buff_off < self.send_buf.as_slices().0.len() {
            &self.send_buf.as_slices().0[buff_off..]
        } else {
            &self.send_buf.as_slices().1[buff_off - self.send_buf.as_slices().0.len()..]
        };

        let len = maybe_len.map_or(slc.len(), |range_len| {
            // Safe conversion because of a min over a usize value.
            expect_usize(min(range_len, to_u64(slc.len())))
        });

        debug_assert!(len > 0);
        debug_assert!(len <= slc.len());

        Some((start, &slc[..len]))
    }

    pub fn mark_as_sent(&mut self, offset: u64, len: usize) {
        self.ranges.mark_sent(offset, len);
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_as_acked(&mut self, offset: u64, len: usize) {
        let prev_retired = self.retired();
        self.ranges.mark_acked(offset, len);

        // Any newly-retired bytes can be dropped from the buffer.
        // No way this can fail because we have to hold this range in our buffer.
        let new_retirable = expect_usize(self.retired() - prev_retired);
        debug_assert!(new_retirable <= self.buffered());
        self.send_buf.drain(..new_retirable);
    }

    pub fn mark_as_lost(&mut self, offset: u64, len: usize) {
        self.ranges.unmark_range(offset, len);
    }

    /// Forget about anything that was marked as sent. Returns whether there was any.
    #[must_use]
    pub fn unmark_sent(&mut self) -> bool {
        self.ranges.unmark_sent()
    }

    #[must_use]
    pub const fn retired(&self) -> u64 {
        self.ranges.acked_from_zero()
    }

    fn buffered(&self) -> usize {
        self.send_buf.len()
    }

    fn avail(&self) -> usize {
        Self::MAX_SIZE - self.buffered()
    }

    #[must_use]
    pub fn used(&self) -> u64 {
        self.retired() + to_u64(self.buffered())
    }
}

/// QUIC sending stream states, based on -transport 3.1.
#[derive(Debug)]
pub enum State {
    Ready {
        fc: SenderFlowControl<StreamId>,
        conn_fc: Rc<RefCell<SenderFlowControl<()>>>,
    },
    Send {
        fc: SenderFlowControl<StreamId>,
        conn_fc: Rc<RefCell<SenderFlowControl<()>>>,
        send_buf: TxBuffer,
        /// The committed (reliable) offset, set via [`SendStream::commit`].
        committed: u64,
    },
    // Note: `DataSent` is entered when the stream is closed, not when all data has been
    // sent for the first time.
    DataSent {
        send_buf: TxBuffer,
        fin_sent: bool,
        fin_acked: bool,
        /// See [`State::Send::committed`].
        committed: u64,
    },
    DataRecvd {
        retired: u64,
        written: u64,
    },
    // A reset has been sent and no committed data below `reliable_size` is still outstanding
    // (either `reliable_size == 0`, i.e. a plain `RESET_STREAM`, or all committed data has
    // already been acked). The send buffer is dropped.
    ResetSent {
        err: AppError,
        final_size: u64,
        /// The reliable size. `0` ⇒ emit `RESET_STREAM`; `> 0` ⇒ emit `RESET_STREAM_AT`.
        reliable_size: u64,
        priority: Option<TransmissionPriority>,
        final_retired: u64,
        final_written: u64,
    },
    // A `RESET_STREAM_AT` has been sent, but committed data below `reliable_size` is still in
    // flight, so the `TxBuffer` is retained to (re)transmit it.
    ResetSentReliable {
        send_buf: TxBuffer,
        err: AppError,
        final_size: u64,
        reliable_size: u64,
        priority: Option<TransmissionPriority>,
        /// Whether the `RESET_STREAM_AT` frame itself has been acked.
        reset_acked: bool,
    },
    ResetRecvd {
        final_retired: u64,
        final_written: u64,
    },
}

impl State {
    const fn tx_buf_mut(&mut self) -> Option<&mut TxBuffer> {
        match self {
            Self::Send { send_buf, .. }
            | Self::DataSent { send_buf, .. }
            | Self::ResetSentReliable { send_buf, .. } => Some(send_buf),
            Self::Ready { .. }
            | Self::DataRecvd { .. }
            | Self::ResetSent { .. }
            | Self::ResetRecvd { .. } => None,
        }
    }

    fn tx_avail(&self) -> usize {
        match self {
            // In Ready, TxBuffer not yet allocated but size is known
            Self::Ready { .. } => TxBuffer::MAX_SIZE,
            Self::Send { send_buf, .. } | Self::DataSent { send_buf, .. } => send_buf.avail(),
            // No application data can be added after a reset.
            Self::DataRecvd { .. }
            | Self::ResetSent { .. }
            | Self::ResetSentReliable { .. }
            | Self::ResetRecvd { .. } => 0,
        }
    }

    fn transition(&mut self, new_state: Self) {
        trace!("SendStream state {self:?} -> {new_state:?}");
        *self = new_state;
    }
}

// See https://www.w3.org/TR/webtransport/#send-stream-stats.
#[derive(Debug, Clone, Copy)]
pub struct Stats {
    // The total number of bytes the consumer has successfully written to
    // this stream. This number can only increase.
    pub written: u64,
    // An indicator of progress on how many of the consumer bytes written to
    // this stream has been sent at least once. This number can only increase,
    // and is always less than or equal to bytes_written.
    pub sent: u64,
    // An indicator of progress on how many of the consumer bytes written to
    // this stream have been sent and acknowledged as received by the server
    // using QUIC’s ACK mechanism. Only sequential bytes up to,
    // but not including, the first non-acknowledged byte, are counted.
    // This number can only increase and is always less than or equal to
    // bytes_sent.
    pub acked: u64,
}

impl Stats {
    #[must_use]
    pub const fn new(written: u64, sent: u64, acked: u64) -> Self {
        Self {
            written,
            sent,
            acked,
        }
    }

    #[must_use]
    pub const fn bytes_written(&self) -> u64 {
        self.written
    }

    #[must_use]
    pub const fn bytes_sent(&self) -> u64 {
        self.sent
    }

    #[must_use]
    pub const fn bytes_acked(&self) -> u64 {
        self.acked
    }
}

/// Implement a QUIC send stream.
#[derive(Debug)]
pub struct SendStream {
    stream_id: StreamId,
    state: State,
    conn_events: ConnectionEvents,
    priority: TransmissionPriority,
    /// Cached result of `priority + retransmission`, recomputed in `set_priority`.
    effective_priority: TransmissionPriority,
    retransmission_offset: u64,
    sendorder: Option<SendOrder>,
    bytes_sent: u64,
    fair: bool,
    send_group: Option<SendGroupId>,
    writable_event_low_watermark: NonZeroUsize,
}

impl SendStream {
    pub fn new(
        stream_id: StreamId,
        max_stream_data: u64,
        conn_fc: Rc<RefCell<SenderFlowControl<()>>>,
        conn_events: ConnectionEvents,
    ) -> Self {
        let ss = Self {
            stream_id,
            state: State::Ready {
                fc: SenderFlowControl::new(stream_id, max_stream_data),
                conn_fc,
            },
            conn_events,
            priority: TransmissionPriority::default(),
            effective_priority: TransmissionPriority::default() + RetransmissionPriority::default(),
            retransmission_offset: 0,
            sendorder: None,
            bytes_sent: 0,
            fair: false,
            send_group: None,
            writable_event_low_watermark: NonZeroUsize::MIN,
        };
        if ss.avail() > 0 {
            ss.conn_events.send_stream_writable(stream_id);
        }
        ss
    }

    /// Returns `true` if [`Self::write_frames`] at this priority has a frame queued:
    /// a pending `RESET_STREAM`, `STREAM_DATA_BLOCKED`, or `STREAM` frame.
    ///
    /// Must mirror every frame-emission path in [`Self::write_frames`]; any new
    /// frame type added there must also be reflected here.
    fn has_data_at(&mut self, priority: TransmissionPriority) -> bool {
        // RESET_STREAM / RESET_STREAM_AT pending?
        match self.state {
            // A plain reset emits no other frames, so this is the only thing to check.
            State::ResetSent {
                priority: reset_priority,
                ..
            } => return reset_priority == Some(priority),
            // A reliable reset may also have committed STREAM data pending, so only short-circuit
            // when the reset frame itself is queued at this priority; otherwise fall through.
            State::ResetSentReliable {
                priority: Some(p), ..
            } if p == priority => return true,
            _ => {}
        }
        // STREAM_DATA_BLOCKED pending?
        if priority == self.priority
            && let State::Ready { fc, .. } | State::Send { fc, .. } = &self.state
            && fc.is_blocked()
        {
            return true;
        }
        // STREAM pending?
        let retransmission = if priority == self.priority {
            false
        } else if priority == self.effective_priority {
            true
        } else {
            return false;
        };
        self.has_next_bytes(retransmission)
    }

    /// Return `false` if the builder is full and the caller should stop iterating.
    ///
    /// Any new frame type added here must also be reflected in `has_data_at`.
    pub fn write_frames<B: Buffer>(
        &mut self,
        priority: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) -> bool {
        if !self.write_reset_frame(priority, builder, tokens, stats) {
            self.write_blocked_frame(priority, builder, tokens, stats);
            if builder.is_full() {
                return false;
            }
            self.write_stream_frame(priority, builder, tokens, stats);
            if builder.is_full() {
                return false;
            }
        }
        true
    }

    pub const fn set_fairness(&mut self, make_fair: bool) {
        self.fair = make_fair;
    }

    #[must_use]
    pub const fn is_fair(&self) -> bool {
        self.fair
    }

    #[must_use]
    pub const fn send_group(&self) -> Option<SendGroupId> {
        self.send_group
    }

    pub(crate) const fn set_send_group(&mut self, group_id: Option<SendGroupId>) {
        self.send_group = group_id;
    }

    pub fn set_priority(
        &mut self,
        transmission: TransmissionPriority,
        retransmission: RetransmissionPriority,
    ) {
        self.priority = transmission;
        self.effective_priority = transmission + retransmission;
    }

    #[must_use]
    pub const fn sendorder(&self) -> Option<SendOrder> {
        self.sendorder
    }

    pub const fn set_sendorder(&mut self, sendorder: Option<SendOrder>) {
        self.sendorder = sendorder;
    }

    /// If the stream's final size is established, what it is. This is known once the stream is
    /// closed (`DataSent`) or reset (`ResetSent`/`ResetSentReliable`).
    #[must_use]
    pub fn final_size(&self) -> Option<u64> {
        match &self.state {
            State::DataSent { send_buf, .. } => Some(send_buf.used()),
            State::ResetSent { final_size, .. } | State::ResetSentReliable { final_size, .. } => {
                Some(*final_size)
            }
            _ => None,
        }
    }

    #[must_use]
    pub fn stats(&self) -> Stats {
        Stats::new(self.bytes_written(), self.bytes_sent, self.bytes_acked())
    }

    #[must_use]
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn bytes_written(&self) -> u64 {
        match &self.state {
            State::Send { send_buf, .. }
            | State::DataSent { send_buf, .. }
            | State::ResetSentReliable { send_buf, .. } => {
                send_buf.retired() + to_u64(send_buf.buffered())
            }
            State::DataRecvd {
                retired, written, ..
            } => *retired + *written,
            State::ResetSent {
                final_retired,
                final_written,
                ..
            }
            | State::ResetRecvd {
                final_retired,
                final_written,
                ..
            } => *final_retired + *final_written,
            State::Ready { .. } => 0,
        }
    }

    #[must_use]
    pub const fn bytes_acked(&self) -> u64 {
        match &self.state {
            State::Send { send_buf, .. }
            | State::DataSent { send_buf, .. }
            | State::ResetSentReliable { send_buf, .. } => send_buf.retired(),
            State::DataRecvd { retired, .. } => *retired,
            State::ResetSent { final_retired, .. } | State::ResetRecvd { final_retired, .. } => {
                *final_retired
            }
            State::Ready { .. } => 0,
        }
    }

    /// Returns whether [`Self::next_bytes`] would yield data, without locating the exact range.
    fn has_next_bytes(&mut self, retransmission_only: bool) -> bool {
        match self.state {
            State::Send {
                ref mut send_buf, ..
            } => {
                if retransmission_only {
                    send_buf.has_next_bytes_before(self.retransmission_offset)
                } else {
                    send_buf.has_next_bytes()
                }
            }
            State::DataSent {
                ref mut send_buf,
                fin_sent,
                ..
            } => send_buf.has_next_bytes() || !fin_sent,
            // Only committed data below `reliable_size` is (re)transmitted (and on a
            // retransmission, only data below the retransmission offset); no FIN.
            State::ResetSentReliable {
                ref mut send_buf,
                reliable_size,
                ..
            } => {
                let limit = if retransmission_only {
                    min(self.retransmission_offset, reliable_size)
                } else {
                    reliable_size
                };
                send_buf.has_next_bytes_before(limit)
            }
            _ => false,
        }
    }

    /// Return the next range to be sent, if any.
    /// If this is a retransmission, cut off what is sent at the retransmission
    /// offset.
    fn next_bytes(&mut self, retransmission_only: bool) -> Option<(u64, &[u8])> {
        match self.state {
            State::Send {
                ref mut send_buf, ..
            } => {
                let (offset, slice) = send_buf.next_bytes()?;
                if retransmission_only {
                    trace!(
                        "next_bytes apply retransmission limit at {}",
                        self.retransmission_offset
                    );
                    (self.retransmission_offset > offset).then(|| {
                        let delta = usize::try_from(self.retransmission_offset - offset)
                            .unwrap_or(usize::MAX);
                        let len = min(delta, slice.len());
                        (offset, &slice[..len])
                    })
                } else {
                    Some((offset, slice))
                }
            }
            State::DataSent {
                ref mut send_buf,
                fin_sent,
                ..
            } => {
                let used = send_buf.used(); // immutable first
                let bytes = send_buf.next_bytes();
                if bytes.is_some() {
                    bytes
                } else if fin_sent {
                    None
                } else {
                    // Send empty stream frame with fin set
                    Some((used, &[]))
                }
            }
            // (Re)transmit committed data, truncated so `offset + len <= reliable_size`, and
            // never a FIN (the end is signalled by the RESET_STREAM_AT frame). This caps fresh
            // sends at `reliable_size` and, on a retransmission, additionally caps at the
            // retransmission offset so only lost data below `reliable_size` is resent.
            State::ResetSentReliable {
                ref mut send_buf,
                reliable_size,
                ..
            } => {
                let limit = if retransmission_only {
                    min(self.retransmission_offset, reliable_size)
                } else {
                    reliable_size
                };
                let (offset, slice) = send_buf.next_bytes()?;
                (offset < limit).then(|| {
                    let cap = usize::try_from(limit - offset).unwrap_or(usize::MAX);
                    let len = min(cap, slice.len());
                    (offset, &slice[..len])
                })
            }
            State::Ready { .. }
            | State::DataRecvd { .. }
            | State::ResetSent { .. }
            | State::ResetRecvd { .. } => None,
        }
    }

    /// Calculate how many bytes (length) can fit into available space and whether
    /// the remainder of the space can be filled (or if a length field is needed).
    fn length_and_fill(data_len: usize, space: usize) -> (usize, bool) {
        if data_len >= space {
            // More data than space allows, or an exact fit => fast path.
            trace!("SendStream::length_and_fill fill {space}");
            return (space, true);
        }

        // Estimate size of the length field based on the available space,
        // less 1, which is the worst case.
        let length = min(space.saturating_sub(1), data_len);
        let length_len = Encoder::varint_len(to_u64(length));
        debug_assert!(length_len <= space); // We don't depend on this being true, but it is true.

        // From here we can always fit `data_len`, but we might as well fill
        // if there is no space for the length field plus another frame.
        let fill = data_len + length_len + packet::Builder::MINIMUM_FRAME_SIZE > space;
        trace!("SendStream::length_and_fill {data_len} fill {fill}");
        (data_len, fill)
    }

    /// Maybe write a `STREAM` frame.
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn write_stream_frame<B: Buffer>(
        &mut self,
        priority: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        let retransmission = if priority == self.priority {
            false
        } else if priority == self.effective_priority {
            true
        } else {
            return;
        };

        let id = self.stream_id;
        // Avoid `Self::final_size`, because we don't want to send the FIN flag
        // after `RESET_STREAM_AT`, even if we have all the data.
        // If we did, packet loss or reordering could drop the reset being delivered.
        let fin_offset = match &self.state {
            State::DataSent { send_buf, .. } => Some(send_buf.used()),
            _ => None,
        };
        if let Some((offset, data)) = self.next_bytes(retransmission) {
            let overhead = 1 // Frame type
                + Encoder::varint_len(id.as_u64())
                + if offset > 0 {
                    Encoder::varint_len(offset)
                } else {
                    0
                };
            if overhead > builder.remaining() {
                trace!("[{self}] write_frame no space for header");
                return;
            }

            let (length, fill) = Self::length_and_fill(data.len(), builder.remaining() - overhead);
            let fin = fin_offset.is_some_and(|fo| fo == offset + to_u64(length));
            if length == 0 && !fin {
                trace!("[{self}] write_frame no data, no fin");
                return;
            }

            if fill {
                builder.pad_before(overhead + length);
            }

            // Write the stream out.
            let frame_type = Frame::stream_type(fin, offset > 0, fill);
            builder.encode_frame(frame_type, |b| {
                b.encode_varint(id.as_u64());
                if offset > 0 {
                    b.encode_varint(offset);
                }
                if fill {
                    b.encode(&data[..length]);
                } else {
                    b.encode_vvec(&data[..length]);
                }
            });
            if fill {
                builder.mark_full();
            }
            debug_assert!(builder.len() <= builder.limit());

            self.mark_as_sent(offset, length, fin);
            tokens.push(recovery::Token::Stream(StreamRecoveryToken::Stream(
                RecoveryToken {
                    id,
                    offset,
                    length: u32::try_from(length).expect("STREAM frame fits in a datagram"),
                    fin,
                },
            )));
            stats.stream += 1;
        }
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn reset_acked(&mut self) {
        match self.state {
            State::Ready { .. }
            | State::Send { .. }
            | State::DataSent { .. }
            | State::DataRecvd { .. } => {
                trace!("[{self}] Reset acked while in {:?} state?", self.state);
            }
            State::ResetSent {
                final_retired,
                final_written,
                ..
            } => {
                // Reaching `ResetRecvd` does not signal stream completion, even for a reliable
                // reset that delivered committed data.
                self.state.transition(State::ResetRecvd {
                    final_retired,
                    final_written,
                });
            }
            State::ResetSentReliable {
                ref mut send_buf,
                reliable_size,
                reset_acked: ref mut frame_acked,
                ..
            } => {
                // Complete only once all committed data has also been acked.
                if send_buf.retired() >= reliable_size {
                    let final_retired = send_buf.retired();
                    let final_written = to_u64(send_buf.buffered());
                    self.state.transition(State::ResetRecvd {
                        final_retired,
                        final_written,
                    });
                } else {
                    *frame_acked = true;
                }
            }
            State::ResetRecvd { .. } => trace!("[{self}] already in ResetRecvd state"),
        }
    }

    pub fn reset_lost(&mut self) {
        match self.state {
            State::ResetSent {
                ref mut priority, ..
            }
            | State::ResetSentReliable {
                ref mut priority, ..
            } => {
                *priority = Some(self.effective_priority);
            }
            State::ResetRecvd { .. } => (),
            _ => unreachable!(),
        }
    }

    /// Maybe write a `RESET_STREAM` or `RESET_STREAM_AT` frame.
    /// Returns true if the reset is successfully sent
    /// and that is the only data that needs sending.
    /// Returns false when there is no reset to send
    /// or when a `RESET_STREAM_AT` frame needs to be followed by stream data.
    pub fn write_reset_frame<B: Buffer>(
        &mut self,
        p: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) -> bool {
        let (State::ResetSent {
            err,
            final_size,
            reliable_size,
            priority,
            ..
        }
        | State::ResetSentReliable {
            err,
            final_size,
            reliable_size,
            priority,
            ..
        }) = &mut self.state
        else {
            return false;
        };
        if *priority != Some(p) {
            return false;
        }
        // `reliable_size == 0` ⇒ plain `RESET_STREAM`; otherwise `RESET_STREAM_AT`.
        let written = if *reliable_size == 0 {
            builder.write_varint_frame(&[
                FrameType::ResetStream.into(),
                self.stream_id.as_u64(),
                *err,
                *final_size,
            ])
        } else {
            builder.write_varint_frame(&[
                FrameType::ResetStreamAt.into(),
                self.stream_id.as_u64(),
                *err,
                *final_size,
                *reliable_size,
            ])
        };
        if written {
            tokens.push(recovery::Token::Stream(StreamRecoveryToken::ResetStream {
                stream_id: self.stream_id,
            }));
            if *reliable_size == 0 {
                stats.reset_stream += 1;
            } else {
                stats.reset_stream_at += 1;
            }
            *priority = None;
        }
        // Even if the write was successful, if we have reliable data pending, return false.
        written && !matches!(self.state, State::ResetSentReliable { .. })
    }

    pub fn blocked_lost(&mut self, limit: u64) {
        if let State::Ready { fc, .. } | State::Send { fc, .. } = &mut self.state {
            fc.frame_lost(limit);
        } else {
            trace!("[{self}] Ignoring lost STREAM_DATA_BLOCKED({limit})");
        }
    }

    /// Maybe write a `STREAM_DATA_BLOCKED` frame.
    pub fn write_blocked_frame<B: Buffer>(
        &mut self,
        priority: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        // Send STREAM_DATA_BLOCKED at normal priority always.
        if priority == self.priority
            && let State::Ready { fc, .. } | State::Send { fc, .. } = &mut self.state
        {
            fc.write_frames(builder, tokens, stats);
        }
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_as_sent(&mut self, offset: u64, len: usize, fin: bool) {
        self.bytes_sent = max(self.bytes_sent, offset + to_u64(len));

        if let Some(buf) = self.state.tx_buf_mut() {
            buf.mark_as_sent(offset, len);
            self.send_blocked_if_space_needed(0);
        }

        if fin && let State::DataSent { fin_sent, .. } = &mut self.state {
            *fin_sent = true;
        }
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_as_acked(&mut self, offset: u64, len: usize, fin: bool) {
        match self.state {
            State::Send {
                ref mut send_buf, ..
            } => {
                let previous_limit = send_buf.avail();
                send_buf.mark_as_acked(offset, len);
                let current_limit = send_buf.avail();
                self.maybe_emit_writable_event(previous_limit, current_limit);
            }
            State::DataSent {
                ref mut send_buf,
                ref mut fin_acked,
                ..
            } => {
                send_buf.mark_as_acked(offset, len);
                if fin {
                    *fin_acked = true;
                }
                if *fin_acked && send_buf.buffered() == 0 {
                    self.conn_events.send_stream_complete(self.stream_id);
                    let retired = send_buf.retired();
                    let buffered = to_u64(send_buf.buffered());
                    self.state.transition(State::DataRecvd {
                        retired,
                        written: buffered,
                    });
                }
            }
            State::ResetSentReliable {
                ref mut send_buf,
                reliable_size,
                reset_acked,
                err,
                final_size,
                priority,
            } => {
                send_buf.mark_as_acked(offset, len);
                // Wait until all committed data (`< reliable_size`) is acked.
                if send_buf.retired() >= reliable_size {
                    let final_retired = send_buf.retired();
                    let final_written = to_u64(send_buf.buffered());
                    if reset_acked {
                        // Both the frame and the committed data are acked: reach `ResetRecvd`.
                        self.state.transition(State::ResetRecvd {
                            final_retired,
                            final_written,
                        });
                    } else {
                        // Committed data is acked but the frame is not: drop the buffer and
                        // await the frame ack in `ResetSent` (which keeps `reliable_size` so
                        // that the frame is still retransmitted as RESET_STREAM_AT).
                        self.state.transition(State::ResetSent {
                            err,
                            final_size,
                            reliable_size,
                            priority,
                            final_retired,
                            final_written,
                        });
                    }
                }
            }
            _ => trace!("[{self}] mark_as_acked called from state {:?}", self.state),
        }
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn mark_as_lost(&mut self, offset: u64, len: usize, fin: bool) {
        self.retransmission_offset = max(self.retransmission_offset, offset + to_u64(len));
        trace!(
            "[{self}] mark_as_lost retransmission offset={}",
            self.retransmission_offset
        );
        if let Some(buf) = self.state.tx_buf_mut() {
            buf.mark_as_lost(offset, len);
        }

        if fin
            && let State::DataSent {
                fin_sent,
                fin_acked,
                ..
            } = &mut self.state
        {
            *fin_sent = *fin_acked;
        }
    }

    /// Bytes sendable on stream. Constrained by stream credit available,
    /// connection credit available, and space in the tx buffer.
    #[must_use]
    pub fn avail(&self) -> usize {
        if let State::Ready { fc, conn_fc } | State::Send { fc, conn_fc, .. } = &self.state {
            min(
                min(fc.available(), conn_fc.borrow().available()),
                self.state.tx_avail(),
            )
        } else {
            0
        }
    }

    /// Set low watermark for [`crate::ConnectionEvent::SendStreamWritable`]
    /// event.
    ///
    /// See [`crate::Connection::stream_set_writable_event_low_watermark`].
    pub(crate) fn set_writable_event_low_watermark(&mut self, watermark: NonZeroUsize) {
        debug!("[{self}] low watermark {watermark}, avail {}", self.avail());
        self.writable_event_low_watermark = watermark;
    }

    pub fn set_max_stream_data(&mut self, limit: u64) {
        debug!("setting max_stream_data to {limit}");
        if let State::Ready { fc, .. } | State::Send { fc, .. } = &mut self.state {
            let previous_limit = fc.available();
            if let Some(current_limit) = fc.update(limit) {
                self.maybe_emit_writable_event(previous_limit, current_limit);
            }
        }
    }

    #[must_use]
    pub const fn is_ended(&self) -> bool {
        matches!(
            self.state,
            State::DataRecvd { .. } | State::ResetRecvd { .. }
        )
    }

    /// # Errors
    /// When `buf` is empty or when the stream is already closed.
    pub fn send(&mut self, buf: &[u8]) -> Res<usize> {
        self.send_internal(buf, false)
    }

    /// # Errors
    /// When `buf` is empty or when the stream is already closed.
    pub fn send_atomic(&mut self, buf: &[u8]) -> Res<usize> {
        self.send_internal(buf, true)
    }

    fn send_blocked_if_space_needed(&mut self, needed_space: usize) {
        if let State::Ready { fc, conn_fc } | State::Send { fc, conn_fc, .. } = &mut self.state {
            if fc.available() <= needed_space {
                fc.blocked();
            }

            if conn_fc.borrow().available() <= needed_space {
                conn_fc.borrow_mut().blocked();
            }
        }
    }

    fn send_internal(&mut self, buf: &[u8], atomic: bool) -> Res<usize> {
        if buf.is_empty() {
            error!("[{self}] zero-length send on stream");
            return Err(Error::InvalidInput);
        }

        if let State::Ready { fc, conn_fc } = &mut self.state {
            let owned_fc = mem::replace(fc, SenderFlowControl::new(self.stream_id, 0));
            let owned_conn_fc = Rc::clone(conn_fc);
            self.state.transition(State::Send {
                fc: owned_fc,
                conn_fc: owned_conn_fc,
                send_buf: TxBuffer::new(),
                committed: 0,
            });
        }

        if !matches!(self.state, State::Send { .. }) {
            return Err(Error::FinalSize);
        }

        let buf = if self.avail() == 0 {
            return Ok(0);
        } else if self.avail() < buf.len() {
            if atomic {
                self.send_blocked_if_space_needed(buf.len());
                return Ok(0);
            }

            &buf[..self.avail()]
        } else {
            buf
        };

        match &mut self.state {
            State::Ready { .. } => unreachable!(),
            State::Send {
                fc,
                conn_fc,
                send_buf,
                ..
            } => {
                let sent = send_buf.send(buf);
                fc.consume(sent);
                conn_fc.borrow_mut().consume(sent);
                Ok(sent)
            }
            _ => Err(Error::FinalSize),
        }
    }

    pub fn close(&mut self) {
        match &mut self.state {
            State::Ready { .. } => {
                self.state.transition(State::DataSent {
                    send_buf: TxBuffer::new(),
                    fin_sent: false,
                    fin_acked: false,
                    committed: 0,
                });
            }
            State::Send {
                send_buf,
                committed,
                ..
            } => {
                let owned_buf = mem::replace(send_buf, TxBuffer::new());
                let committed = *committed;
                self.state.transition(State::DataSent {
                    send_buf: owned_buf,
                    fin_sent: false,
                    fin_acked: false,
                    committed,
                });
            }
            State::DataSent { .. } => trace!("[{self}] already in DataSent state"),
            State::DataRecvd { .. } => trace!("[{self}] already in DataRecvd state"),
            State::ResetSent { .. } => trace!("[{self}] already in ResetSent state"),
            State::ResetSentReliable { .. } => {
                trace!("[{self}] already in ResetSentReliable state");
            }
            State::ResetRecvd { .. } => trace!("[{self}] already in ResetRecvd state"),
        }
    }

    /// Commit to reliably delivering all data buffered so far: that prefix is delivered even if
    /// the stream is later reset (via `RESET_STREAM_AT`). Call this *after* writing the data to
    /// be protected. The committed offset only ever grows (the buffered total never shrinks).
    /// The caller is responsible for ensuring that their peer supports this feature.
    ///
    /// # Errors
    /// [`Error::StreamState`] when the stream has already been reset.
    pub fn commit(&mut self) -> Res<()> {
        match &mut self.state {
            // Nothing has been buffered yet, so the implicit committed offset stays 0; and once
            // all data has been received there is nothing left to commit. Both are no-ops.
            State::Ready { .. } | State::DataRecvd { .. } => Ok(()),
            State::Send {
                send_buf,
                committed,
                ..
            }
            | State::DataSent {
                send_buf,
                committed,
                ..
            } => {
                *committed = send_buf.used();
                Ok(())
            }
            State::ResetSent { .. }
            | State::ResetSentReliable { .. }
            | State::ResetRecvd { .. } => Err(Error::StreamState),
        }
    }

    /// Reset the stream. When a non-zero commitment exists (set via [`Self::commit`], which is
    /// only reachable when the peer supports the feature), a `RESET_STREAM_AT` is emitted
    /// (reliably delivering `[0, reliable_size)`); otherwise a plain `RESET_STREAM` is sent.
    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn reset(&mut self, err: AppError) {
        /// Build the reset state for a stream that has a `send_buf`, choosing between a buffer-less
        /// `ResetSent` and a `ResetSentReliable` that retains the buffer for committed data.
        fn make_reset_state(
            err: AppError,
            priority: TransmissionPriority,
            send_buf: &mut TxBuffer,
            final_size: u64,
            committed: u64,
        ) -> State {
            // A non-zero committed offset implies the peer supports reliable reset (`commit`
            // enforces that), so it is safe to emit `RESET_STREAM_AT`.
            let reliable_size = min(committed, final_size);
            let final_retired = send_buf.retired();
            let final_written = to_u64(send_buf.buffered());
            if reliable_size == 0 || final_retired >= reliable_size {
                // No committed data is still outstanding: drop the buffer.
                State::ResetSent {
                    err,
                    final_size,
                    reliable_size,
                    priority: Some(priority),
                    final_retired,
                    final_written,
                }
            } else {
                // Committed data below `reliable_size` is still in flight: keep the buffer.
                State::ResetSentReliable {
                    send_buf: mem::take(send_buf),
                    err,
                    final_size,
                    reliable_size,
                    reset_acked: false,
                    priority: Some(priority),
                }
            }
        }

        let priority = self.priority;
        let new_state = match &mut self.state {
            State::Ready { fc, .. } => State::ResetSent {
                err,
                final_size: fc.used(),
                reliable_size: 0,
                priority: Some(priority),
                final_retired: 0,
                final_written: 0,
            },
            State::Send {
                fc,
                send_buf,
                committed,
                ..
            } => {
                let final_size = fc.used();
                make_reset_state(err, priority, send_buf, final_size, *committed)
            }
            State::DataSent {
                send_buf,
                committed,
                ..
            } => {
                let final_size = send_buf.used();
                make_reset_state(err, priority, send_buf, final_size, *committed)
            }
            State::DataRecvd { .. }
            | State::ResetSent { .. }
            | State::ResetSentReliable { .. }
            | State::ResetRecvd { .. } => {
                trace!("[{}] reset called in terminal state", self.stream_id);
                return;
            }
        };
        self.state.transition(new_state);
    }

    /// Drop any commitment made via [`Self::commit`] in response to a `STOP_SENDING`: the peer has
    /// no interest in the data, so there is nothing left to deliver reliably.
    ///
    /// Before a reset is sent, this just clears the committed offset so that a subsequent
    /// [`Self::reset`] emits a plain `RESET_STREAM` (`reliable_size == 0`). If a `RESET_STREAM_AT`
    /// has already been sent (`ResetSentReliable`), the buffer of still-in-flight committed data is
    /// dropped and the stream moves to `ResetSent` with `reliable_size` reset to 0 (so any
    /// retransmission of the reset frame is a plain `RESET_STREAM`), or straight to `ResetRecvd` if
    /// the reset frame is already acked.
    pub(crate) fn drop_commitment(&mut self) {
        match &mut self.state {
            State::Send { committed, .. } | State::DataSent { committed, .. } => {
                *committed = 0;
            }
            State::ResetSentReliable {
                send_buf,
                err,
                final_size,
                reset_acked,
                priority,
                ..
            } => {
                let final_retired = send_buf.retired();
                let final_written = to_u64(send_buf.buffered());
                let new_state = if *reset_acked {
                    // The reset is already acked, so abandoning the data completes the reset.
                    State::ResetRecvd {
                        final_retired,
                        final_written,
                    }
                } else {
                    // Drop the buffer and await the frame ack in `ResetSent`. Clearing
                    // `reliable_size` makes any retransmission of the reset frame a plain
                    // `RESET_STREAM`.
                    State::ResetSent {
                        err: *err,
                        final_size: *final_size,
                        reliable_size: 0,
                        priority: *priority,
                        final_retired,
                        final_written,
                    }
                };
                self.state.transition(new_state);
            }
            _ => {}
        }
    }

    #[cfg(test)]
    pub(crate) const fn state(&mut self) -> &mut State {
        &mut self.state
    }

    pub(crate) fn maybe_emit_writable_event(&self, previous_limit: usize, current_limit: usize) {
        let low_watermark = self.writable_event_low_watermark.get();

        // Skip if:
        // - stream was not constrained by limit before,
        // - or stream is still constrained by limit,
        // - or stream is constrained by different limit.
        if low_watermark < previous_limit
            || current_limit < low_watermark
            || self.avail() < low_watermark
        {
            return;
        }

        trace!(
            "[{self}] writable, low watermark {low_watermark}, avail {}",
            self.avail()
        );
        self.conn_events.send_stream_writable(self.stream_id);
    }
}

impl Display for SendStream {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(f, "SendStream {}", self.stream_id)
    }
}

#[derive(Debug, Default)]
pub struct OrderGroup {
    // This vector is sorted by StreamId
    vec: Vec<StreamId>,

    // Since we need to remember where we were, we'll store the iterator next
    // position in the object.  This means there can only be a single iterator active
    // at a time!
    next: usize,
    // This is used when an iterator is created to set the start/stop point for the
    // iteration.  The iterator must iterate from this entry to the end, and then
    // wrap and iterate from 0 until before the initial value of next.
    // This value may need to be updated after insertion and removal; in theory we should
    // track the target entry across modifications, but in practice it should be good
    // enough to simply leave it alone unless it points past the end of the
    // Vec, and re-initialize to 0 in that case.
}

pub struct OrderGroupIter<'a> {
    group: &'a mut OrderGroup,
    // We store the next position in the OrderGroup.
    // Otherwise we'd need an explicit "done iterating" call to be made, or implement Drop to
    // copy the value back.
    // This is where next was when we iterated for the first time; when we get back to that we
    // stop.
    started_at: Option<usize>,
}

impl OrderGroup {
    pub const fn iter(&mut self) -> OrderGroupIter<'_> {
        // Ids may have been deleted since we last iterated
        if self.next >= self.vec.len() {
            self.next = 0;
        }
        OrderGroupIter {
            started_at: None,
            group: self,
        }
    }

    #[must_use]
    pub fn stream_ids(&self) -> &[StreamId] {
        &self.vec
    }

    pub fn clear(&mut self) {
        self.vec.clear();
    }

    pub fn push(&mut self, stream_id: StreamId) {
        self.vec.push(stream_id);
    }

    #[cfg(test)]
    pub fn truncate(&mut self, position: usize) {
        self.vec.truncate(position);
    }

    const fn update_next(&mut self) -> usize {
        let next = self.next;
        self.next = (self.next + 1) % self.vec.len();
        next
    }

    /// # Panics
    /// If the stream ID is already present.
    pub fn insert(&mut self, stream_id: StreamId) {
        let Err(pos) = self.vec.binary_search(&stream_id) else {
            // element already in vector @ `pos`
            panic!("Duplicate stream_id {stream_id}");
        };
        self.vec.insert(pos, stream_id);
    }

    /// # Panics
    /// If the stream ID is not present.
    pub fn remove(&mut self, stream_id: StreamId) {
        let Ok(pos) = self.vec.binary_search(&stream_id) else {
            // element already in vector @ `pos`
            panic!("Missing stream_id {stream_id}");
        };
        self.vec.remove(pos);
    }
}

impl Iterator for OrderGroupIter<'_> {
    type Item = StreamId;
    fn next(&mut self) -> Option<Self::Item> {
        // Stop when we would return the started_at element on the next
        // call.  Note that this must take into account wrapping.
        if self.started_at == Some(self.group.next) || self.group.vec.is_empty() {
            return None;
        }
        self.started_at = self.started_at.or(Some(self.group.next));
        let orig = self.group.update_next();
        Some(self.group.vec[orig])
    }
}

/// Per-send-group scheduling queues, mirroring the ungrouped `regular`/`sendordered` structure.
///
/// Streams in a send group use their own sendOrder namespace: sendOrder values in different
/// groups are not compared against each other (spec requirement).  Within the group, higher
/// sendOrder starves lower sendOrder, same as for ungrouped streams.
#[derive(Debug, Default)]
struct PerGroupQueues {
    sendordered: BTreeMap<SendOrder, OrderGroup>,
    regular: OrderGroup,
}

impl PerGroupQueues {
    fn group_mut(&mut self, sendorder: Option<SendOrder>) -> &mut OrderGroup {
        if let Some(order) = sendorder {
            self.sendordered.entry(order).or_default()
        } else {
            &mut self.regular
        }
    }

    fn remove_stream(&mut self, stream_id: StreamId, sendorder: Option<SendOrder>) {
        if let Some(order) = sendorder {
            if let Some(grp) = self.sendordered.get_mut(&order) {
                grp.remove(stream_id);
                if grp.stream_ids().is_empty() {
                    self.sendordered.remove(&order);
                }
            }
        } else {
            self.regular.remove(stream_id);
        }
    }

    fn is_empty(&self) -> bool {
        // `remove_stream` prunes empty `sendordered` entries, so any remaining entry is
        // non-empty: the map being empty is equivalent to having no sendordered streams.
        self.regular.stream_ids().is_empty() && self.sendordered.is_empty()
    }
}

#[derive(Debug, Default)]
pub struct SendStreams {
    map: IndexMap<StreamId, SendStream, FxBuildHasher>,

    // What we really want is a Priority Queue that we can do arbitrary
    // removes from (so we can reprioritize). BinaryHeap doesn't work,
    // because there's no remove().  BTreeMap doesn't work, since you can't
    // duplicate keys.  PriorityQueue does have what we need, except for an
    // ordered iterator that doesn't consume the queue.  So we roll our own.

    // Added complication: We want to have Fairness for streams of the same
    // 'group' (for WebTransport), but for H3 (and other non-WT streams) we
    // tend to get better pageload performance by prioritizing by creation order.
    //
    // Two options are to walk the 'map' first, ignoring WebTransport
    // streams, then process the unordered and ordered WebTransport
    // streams.  The second is to have a sorted Vec for unfair streams (and
    // use a normal iterator for that), and then chain the iterators for
    // the unordered and ordered WebTranport streams.  The first works very
    // well for H3, and for WebTransport nodes are visited twice on every
    // processing loop.  The second adds insertion and removal costs, but
    // avoids a CPU penalty for WebTransport streams.  For now we'll do #1.
    //
    // Per-send-group queues, including NULL_GROUP_ID for ungrouped fair streams.
    // Groups are served round-robin; within a group sendOrder determines priority.
    /// Set when any stream has ended; cleared by `remove_ended`.
    has_ended: bool,

    per_group: IndexMap<SendGroupId, PerGroupQueues>,
    per_group_next: usize, // round-robin cursor over per_group entries

    // Round-robin cursor (index into `map`) for the single-group no-sendOrder fast
    // path.  Lets that path iterate `map` by index (cache-friendly, no per-stream
    // hash lookup) while still resuming after the last-served stream when the packet
    // builder fills mid-pass, preserving fairness.
    fair_rr_next: usize,

    max_low_watermark: Option<NonZeroUsize>, // Upper bound of the streams' low watermarks.
}

impl SendStreams {
    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn get(&self, id: StreamId) -> Res<&SendStream> {
        self.map.get(&id).ok_or(Error::InvalidStreamId)
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn get_mut(&mut self, id: StreamId) -> Res<&mut SendStream> {
        self.map.get_mut(&id).ok_or(Error::InvalidStreamId)
    }

    #[must_use]
    pub fn exists(&self, id: StreamId) -> bool {
        self.map.contains_key(&id)
    }

    pub fn insert(&mut self, id: StreamId, stream: SendStream) {
        self.map.insert(id, stream);
    }

    /// Insert `stream_id` into group `gid`'s queue for `sendorder`, creating the group
    /// if it does not exist yet.
    fn insert_into_group(
        &mut self,
        gid: SendGroupId,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) {
        self.per_group
            .entry(gid)
            .or_default()
            .group_mut(sendorder)
            .insert(stream_id);
    }

    /// Remove `stream_id` (queued at `sendorder`) from group `gid`, dropping the group
    /// once it becomes empty and keeping the round-robin cursor in bounds.
    fn remove_from_group(
        &mut self,
        gid: SendGroupId,
        stream_id: StreamId,
        sendorder: Option<SendOrder>,
    ) {
        if let Some(grp_queues) = self.per_group.get_mut(&gid) {
            grp_queues.remove_stream(stream_id, sendorder);
            if grp_queues.is_empty() {
                self.per_group.shift_remove(&gid);
                if self.per_group_next >= self.per_group.len() {
                    self.per_group_next = 0;
                }
            }
        }
    }

    /// Assign `stream_id` to a send group, or pass `None` to move it back to the
    /// ungrouped queues.  The group is created implicitly if it doesn't exist yet;
    /// empty groups are removed automatically.
    ///
    /// # Errors
    /// Returns [`Error::InvalidStreamId`] if the stream does not exist. Returns
    /// [`Error::InvalidInput`] if `group_id` is the reserved `NULL_GROUP_ID` (0), or if
    /// `group_id` is `Some` for a stream that is not fair (a send group only applies to
    /// fair streams).
    pub fn set_sendgroup(&mut self, stream_id: StreamId, group_id: Option<SendGroupId>) -> Res<()> {
        // Extract the info we need before any other mutable borrows.
        let (was_fair, old_sendorder, old_group) = {
            let stream = self.map.get(&stream_id).ok_or(Error::InvalidStreamId)?;
            (stream.is_fair(), stream.sendorder(), stream.send_group())
        };

        // NULL_GROUP_ID (0) is the internal sentinel for ungrouped fair streams; accepting it
        // as an explicit group would conflate the two and corrupt the per-group queues.
        if group_id == Some(NULL_GROUP_ID) {
            return Err(Error::InvalidInput);
        }

        if old_group == group_id {
            return Ok(());
        }

        // A send group only applies to fair streams: a non-fair stream is served by
        // the unfair loop in `write_frames`, so also placing it in a per-group queue
        // would serve it twice (double bandwidth). Reject this at the API boundary --
        // both this and `Connection::stream_sendgroup` are public and callers must set
        // fairness first.
        if group_id.is_some() && !was_fair {
            return Err(Error::InvalidInput);
        }

        // Remove from current location: an explicit group, or the null-group
        // (ungrouped) slot if the stream was fair but ungrouped.
        if let Some(gid) = old_group.or_else(|| was_fair.then_some(NULL_GROUP_ID)) {
            self.remove_from_group(gid, stream_id, old_sendorder);
        }

        // Update the stream record.
        if let Some(stream) = self.map.get_mut(&stream_id) {
            stream.set_send_group(group_id);
        }

        // Insert into the new location: an explicit group, or the null-group
        // (ungrouped) slot if the stream is fair but ungrouped.
        if let Some(gid) = group_id.or_else(|| was_fair.then_some(NULL_GROUP_ID)) {
            self.insert_into_group(gid, stream_id, old_sendorder);
        }
        Ok(())
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn set_sendorder(&mut self, stream_id: StreamId, sendorder: Option<SendOrder>) -> Res<()> {
        self.set_fairness(stream_id, true)?;
        // Extract what we need before any further borrows.
        let (old_sendorder, send_group) = {
            let stream = self.map.get(&stream_id).ok_or(Error::InvalidStreamId)?;
            (stream.sendorder(), stream.send_group())
        };
        if old_sendorder != sendorder {
            // Grouped and ungrouped fair streams both live in `per_group` (ungrouped
            // under NULL_GROUP_ID), and the prior `set_fairness` ensures the group
            // exists. Move the stream between sendOrder buckets within its group; we
            // re-insert immediately, so skip the empty-group cleanup on removal.
            let gid = send_group.unwrap_or(NULL_GROUP_ID);
            if let Some(grp_queues) = self.per_group.get_mut(&gid) {
                grp_queues.remove_stream(stream_id, old_sendorder);
            }
            if let Some(stream) = self.map.get_mut(&stream_id) {
                stream.set_sendorder(sendorder);
            }
            self.insert_into_group(gid, stream_id, sendorder);
            trace!("stream {stream_id} sendorder -> {sendorder:?} in group {gid:?}");
        }
        Ok(())
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_errors_doc,
        reason = "OK here."
    )]
    pub fn set_fairness(&mut self, stream_id: StreamId, make_fair: bool) -> Res<()> {
        let stream: &mut SendStream = self.map.get_mut(&stream_id).ok_or(Error::InvalidStreamId)?;
        let was_fair = stream.fair;
        let send_group = stream.send_group();
        let sendorder = stream.sendorder;
        stream.set_fairness(make_fair);
        if !was_fair && make_fair {
            // A newly fair stream with no send_group goes to the null-group slot in per_group,
            // so it participates in round-robin alongside all explicit sendGroups (spec: equal).
            // Streams with a send_group are managed by set_sendgroup; don't add here.
            if send_group.is_none() {
                // This normally is only called when a new stream is created.  If
                // so, because of how we allocate StreamIds, it should always have
                // the largest value.  This means we can just append it.  However,
                // if we were ever to change this invariant, things would break subtly.

                // To be safe we can try to insert at the end and if not
                // fall back to binary-search insertion.
                let null_grp = self.per_group.entry(NULL_GROUP_ID).or_default();
                let grp = null_grp.group_mut(sendorder);
                if grp
                    .stream_ids()
                    .last()
                    .is_some_and(|last| stream_id > *last)
                {
                    grp.push(stream_id);
                } else {
                    grp.insert(stream_id);
                }
            }
        } else if was_fair && !make_fair {
            // Remove from whichever queue currently owns this stream: an explicit
            // group, or the null-group (ungrouped) slot.
            let gid = send_group.unwrap_or(NULL_GROUP_ID);
            self.remove_from_group(gid, stream_id, sendorder);
            // A send group applies only to fair streams (see `set_sendgroup`). Clear it
            // so a later `set_fairness(true)` re-queues the stream in the null group;
            // otherwise it stays recorded in a group it is no longer queued in and is
            // never scheduled again.
            if let Some(stream) = self.map.get_mut(&stream_id) {
                stream.set_send_group(None);
            }
        }
        Ok(())
    }

    pub fn acked(&mut self, token: &RecoveryToken) {
        if let Some(ss) = self.map.get_mut(&token.id) {
            ss.mark_as_acked(token.offset, expect_usize(token.length), token.fin);
            self.has_ended |= ss.is_ended();
        }
    }

    pub fn reset_acked(&mut self, id: StreamId) {
        if let Some(ss) = self.map.get_mut(&id) {
            ss.reset_acked();
            self.has_ended |= ss.is_ended();
        }
    }

    pub fn lost(&mut self, token: &RecoveryToken) {
        if let Some(ss) = self.map.get_mut(&token.id) {
            ss.mark_as_lost(token.offset, expect_usize(token.length), token.fin);
        }
    }

    pub fn reset_lost(&mut self, stream_id: StreamId) {
        if let Some(ss) = self.map.get_mut(&stream_id) {
            ss.reset_lost();
        }
    }

    pub fn blocked_lost(&mut self, stream_id: StreamId, limit: u64) {
        if let Some(ss) = self.map.get_mut(&stream_id) {
            ss.blocked_lost(limit);
        }
    }

    pub fn clear(&mut self) {
        self.map.clear();
        self.has_ended = false;
        self.per_group.clear();
        self.per_group_next = 0;
        self.fair_rr_next = 0;
    }

    /// Remove ended streams. Returns `true` if any were removed.
    #[must_use]
    pub fn remove_ended(&mut self) -> bool {
        if !self.has_ended {
            return false;
        }
        self.has_ended = false;
        let mut removed = false;
        for (stream_id, stream) in self.map.extract_if(.., |_, s| s.is_ended()) {
            removed = true;
            if stream.is_fair() {
                let group_id = stream.send_group().unwrap_or(NULL_GROUP_ID);
                if let Some(grp_queues) = self.per_group.get_mut(&group_id) {
                    grp_queues.remove_stream(stream_id, stream.sendorder());
                }
            }
        }
        // Clean up now-empty groups.
        self.per_group.retain(|_, grp| !grp.is_empty());
        if self.per_group_next >= self.per_group.len() {
            self.per_group_next = 0;
        }
        // `extract_if` shifts `map` indices, so the round-robin cursor may now be past
        // the end; clamp it rather than resetting to 0, which would give the first fair
        // stream an extra turn after every removal.
        if self.fair_rr_next >= self.map.len() {
            self.fair_rr_next = 0;
        }
        removed
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        priority: TransmissionPriority,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        // WebTransport data (which is Normal) may have a SendOrder
        // priority attached.  The spec states (6.3 write-chunk 6.1):

        // First, we send any streams without Fairness defined, with
        // ordering defined by StreamId.  (Http3 streams used for
        // e.g. pageload benefit from being processed in order of creation
        // so the far side can start acting on a datum/request sooner. All
        // WebTransport streams MUST have fairness set.)  Then we send
        // streams with fairness set (including all WebTransport streams)
        // as follows:

        // If stream.[[SendOrder]] is null then this sending MUST NOT
        // starve except for flow control reasons or error.  If
        // stream.[[SendOrder]] is not null then this sending MUST starve
        // until all bytes queued for sending on WebTransportSendStreams
        // with a non-null and higher [[SendOrder]], that are neither
        // errored nor blocked by flow control, have been sent.

        // So data without SendOrder goes first.   Then the highest priority
        // SendOrdered streams.
        //
        // Fairness is implemented by a round-robining or "statefully
        // iterating" within a single sendorder/unordered vector.  We do
        // this by recording where we stopped in the previous pass, and
        // starting there the next pass.  If we store an index into the
        // vec, this means we can't use a chained iterator, since we want
        // to retain our place-in-the-vector.  If we rotate the vector,
        // that would let us use the chained iterator, but would require
        // more expensive searches for insertion and removal (since the
        // sorted order would be lost).

        // First: unfair streams (non-WebTransport H3 streams, by creation order).
        // Then: all fair streams via per-group round-robin (includes null sendGroup).
        trace!("processing streams...  unfair:");
        for stream in self.map.values_mut() {
            if stream.is_fair() || !stream.has_data_at(priority) {
                continue;
            }
            trace!("   {stream}");
            if !stream.write_frames(priority, builder, tokens, stats) {
                break;
            }
        }
        // Send groups: round-robin between all groups, including NULL_GROUP_ID (ungrouped
        // fair streams).  The null sendGroup is bandwidth-equal to all explicit sendGroups
        // (spec: "The user agent considers WebTransportSendGroups as equals when allocating
        // bandwidth.").  Each group contributes one stream attempt per scheduler pass so
        // groups get equal bandwidth share regardless of differing sendOrder values between
        // groups.  Within a group, the highest-sendOrder stream is served first (starving
        // lower sendOrder within the same group), matching the spec starvation requirement.
        let num_groups = self.per_group.len();
        if num_groups == 0 {
            // No fair streams are registered (the common non-WebTransport case), so
            // there is nothing left to send.  Returning here avoids a wasteful second
            // walk of `map` searching for fair streams that don't exist.
            return;
        }
        let single_group_no_sendorder = num_groups == 1
            && self
                .per_group
                .first()
                .is_some_and(|(_, grp)| grp.sendordered.is_empty());
        if single_group_no_sendorder {
            // Fast path: a single group with no sendOrder set (typical case).
            //
            // Walk `map` by index rather than via the group's `regular` OrderGroup:
            // `get_index_mut` is direct Vec access (no per-stream hash lookup), giving
            // the same cache-friendly cost as the original `values_mut()` walk.
            // `fair_rr_next` is a round-robin cursor into `map`; when the builder fills
            // mid-pass we resume at the stream *after* the one that filled, so later
            // fair streams are not starved (WebTransport spec write-chunk 6.3 step 6.1:
            // null-sendOrder streams MUST NOT starve).
            let n = self.map.len();
            if self.fair_rr_next >= n {
                self.fair_rr_next = 0;
            }
            let start = self.fair_rr_next;
            for off in 0..n {
                let idx = (start + off) % n;
                // `idx < n`, so this always succeeds; `else` is just to avoid a panic.
                let Some((_, stream)) = self.map.get_index_mut(idx) else {
                    continue;
                };
                if !stream.is_fair() || !stream.has_data_at(priority) {
                    continue;
                }
                if !stream.write_frames(priority, builder, tokens, stats) {
                    // Resume after this stream next call so it can't monopolise.
                    self.fair_rr_next = (idx + 1) % n;
                    return;
                }
            }
        } else {
            if self.per_group_next >= num_groups {
                self.per_group_next = 0;
            }
            let start = self.per_group_next;
            // Split borrows on disjoint fields so we can access both per_group (for
            // priority ordering) and map (for the stream itself) in the same loop body
            // without an intermediate Vec.
            let (per_group, map, per_group_next) =
                (&mut self.per_group, &mut self.map, &mut self.per_group_next);
            // Repeat the round-robin pass until no group can write any more (or the
            // builder fills, which returns directly).  A single pass gives each group at
            // most one STREAM frame, which would leave most of the packet empty when there
            // are fewer groups than fit; looping fills the remaining capacity while keeping
            // each group's per-pass turn equal (inter-group fairness) and advancing each
            // group's within-group round-robin cursor between passes.
            //
            // Each pass re-scans a group's higher-sendOrder buckets from the top, so a
            // drained-but-open higher bucket is walked again on every pass before a lower
            // bucket with data is reached -- O(buckets^2) bucket visits per packet when
            // many buckets drain. An alternative would remember exhausted buckets between
            // passes and skip them, but it isn't used: the re-scan is bounded by
            // frames-per-packet * buckets, each visit is a cheap map lookup, and it only
            // affects the WebTransport sendOrder path, so the cost is sub-microsecond next
            // to the per-packet crypto and I/O that filling the packet saves.
            loop {
                let mut any_wrote = false;
                'groups: for i in 0..num_groups {
                    let idx = (start + i) % num_groups;
                    let Some((_, grp)) = per_group.get_index_mut(idx) else {
                        continue;
                    };
                    // Serve streams in strict sendOrder priority within the group: the
                    // highest [[SendOrder]] bucket first, then lower buckets, and the regular
                    // (null-sendOrder) bucket last. A stream must starve until all bytes queued
                    // on same-group streams with a higher [[SendOrder]] -- that are neither
                    // errored nor blocked by flow control -- have been sent (WebTransport
                    // send-order rules). A stream blocked by flow control emits only a
                    // STREAM_DATA_BLOCKED frame (no STREAM progress) and must not starve its
                    // lower-priority peers, so fall through to the next bucket in that case.
                    for order_grp in grp.sendordered.values_mut().rev() {
                        // Scan the bucket until a stream actually writes data. A drained-but-open
                        // or flow-control-blocked stream at the round-robin cursor must not let a
                        // lower-sendOrder bucket jump ahead while a same-bucket peer still has
                        // sendable data (WebTransport send-order rules).
                        for stream_id in order_grp.iter() {
                            trace!("send group {idx}: stream {stream_id}");
                            // End the group's turn only if an actual STREAM frame was written,
                            // not on any builder growth (see flow-control note above).
                            let before = stats.stream;
                            if let Some(stream) = map.get_mut(&stream_id)
                                && !stream.write_frames(priority, builder, tokens, stats)
                            {
                                *per_group_next = (idx + 1) % num_groups;
                                return;
                            }
                            if stats.stream > before {
                                any_wrote = true;
                                continue 'groups;
                            }
                        }
                    }
                    // Lowest priority in the group: the null-sendOrder bucket, reached only when
                    // no higher-sendOrder stream had sendable data this pass. Scan it rather than
                    // attempting only the cursor stream so a drained-but-open or flow-blocked
                    // stream doesn't waste the group's turn while a sendable peer waits. Null-
                    // sendOrder streams have no priority among themselves, so this only affects
                    // latency, not the WebTransport "MUST NOT starve" guarantee.
                    for stream_id in grp.regular.iter() {
                        trace!("send group {idx}: stream {stream_id}");
                        let before = stats.stream;
                        if let Some(stream) = map.get_mut(&stream_id)
                            && !stream.write_frames(priority, builder, tokens, stats)
                        {
                            *per_group_next = (idx + 1) % num_groups;
                            return;
                        }
                        if stats.stream > before {
                            any_wrote = true;
                            continue 'groups;
                        }
                    }
                }
                // A full pass wrote nothing: every group is drained, errored, or
                // flow-control blocked, so further passes can't make progress.
                if !any_wrote {
                    break;
                }
            }
            // All groups had a chance this pass; advance the cursor for the next call.
            *per_group_next = (start + 1) % num_groups;
        }
    }

    #[allow(
        clippy::allow_attributes,
        clippy::missing_panics_doc,
        reason = "OK here."
    )]
    pub fn update_initial_limit(&mut self, remote: &TransportParameters) {
        for (id, ss) in &mut self.map {
            let limit = if id.is_bidi() {
                assert!(!id.is_remote_initiated(Role::Client));
                remote.get_integer(InitialMaxStreamDataBidiRemote)
            } else {
                remote.get_integer(InitialMaxStreamDataUni)
            };
            ss.set_max_stream_data(limit);
        }
    }

    pub(crate) fn set_writable_event_low_watermark(
        &mut self,
        stream_id: StreamId,
        watermark: NonZeroUsize,
    ) -> Res<()> {
        self.get_mut(stream_id)?
            .set_writable_event_low_watermark(watermark);
        self.max_low_watermark = max(self.max_low_watermark, Some(watermark));
        Ok(())
    }

    pub(crate) fn maybe_emit_writable_events(
        &mut self,
        previous_limit: usize,
        current_limit: usize,
    ) {
        if self.max_low_watermark.unwrap_or(NonZeroUsize::MIN).get() < previous_limit {
            return; // No stream was waiting for connection flow control.
        }
        let mut max_low_watermark = NonZeroUsize::MIN;
        for ss in self.map.values() {
            ss.maybe_emit_writable_event(previous_limit, current_limit);
            max_low_watermark = max(max_low_watermark, ss.writable_event_low_watermark);
        }
        self.max_low_watermark = Some(max_low_watermark);
    }
}

#[allow(
    clippy::allow_attributes,
    clippy::into_iter_without_iter,
    reason = "OK here."
)]
impl<'a> IntoIterator for &'a mut SendStreams {
    type Item = (&'a StreamId, &'a mut SendStream);
    type IntoIter = indexmap::map::IterMut<'a, StreamId, SendStream>;

    fn into_iter(self) -> indexmap::map::IterMut<'a, StreamId, SendStream> {
        self.map.iter_mut()
    }
}

#[derive(Debug, Clone)]
pub struct RecoveryToken {
    id: StreamId,
    offset: u64,
    length: u32, // `u32`, because a STREAM frame cannot be larger than an IP datagram
    fin: bool,
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{cell::RefCell, collections::VecDeque, num::NonZeroUsize, rc::Rc};

    use neqo_common::{
        Encoder, MAX_VARINT, event::Provider as _, expect_usize, hex::HexWithLen, to_u64,
    };
    use tracing::trace;

    use super::RecoveryToken;
    use crate::{
        ConnectionEvents, Error, INITIAL_LOCAL_MAX_STREAM_DATA, StreamId,
        connection::{RetransmissionPriority, TransmissionPriority},
        events::ConnectionEvent,
        fc::SenderFlowControl,
        packet,
        recovery::{self, StreamRecoveryToken},
        send_stream::{
            NULL_GROUP_ID, RangeState, RangeTracker, SendStream, SendStreams, State, TxBuffer,
        },
        stats::FrameStats,
        streams::SendGroupId,
    };

    fn connection_fc(limit: u64) -> Rc<RefCell<SenderFlowControl<()>>> {
        Rc::new(RefCell::new(SenderFlowControl::new((), limit)))
    }

    /// A send group only applies to fair streams: assigning one to a non-fair stream
    /// would let it be served by both the unfair loop and the per-group round-robin
    /// (double bandwidth), so `set_sendgroup` must reject it. Once the stream is fair,
    /// the assignment succeeds.
    #[test]
    fn set_sendgroup_requires_fair_stream() {
        let id = StreamId::from(0);
        let mut ss = SendStreams::default();
        ss.insert(
            id,
            SendStream::new(id, 100, connection_fc(100), ConnectionEvents::default()),
        );

        assert!(ss.set_sendgroup(id, Some(SendGroupId::new(1))).is_err());

        ss.set_fairness(id, true).unwrap();
        ss.set_sendgroup(id, Some(SendGroupId::new(1))).unwrap();
    }

    /// `SendGroupId(0)` is the internal sentinel for ungrouped fair streams, so passing it
    /// as an explicit group must be rejected rather than corrupt the per-group queues.
    #[test]
    fn set_sendgroup_rejects_null_group_id() {
        let id = StreamId::from(0);
        let mut ss = SendStreams::default();
        ss.insert(
            id,
            SendStream::new(id, 100, connection_fc(100), ConnectionEvents::default()),
        );
        ss.set_fairness(id, true).unwrap();

        assert!(ss.set_sendgroup(id, Some(NULL_GROUP_ID)).is_err());
    }

    /// A group containing both a regular (null-sendOrder) and a sendordered stream,
    /// both with data: the higher-sendOrder stream is served first, and once it has
    /// drained the regular stream gets its turn. The regular stream must not be
    /// *permanently* starved (WebTransport send-order rules: a stream starves only
    /// until higher-sendOrder same-group bytes have been sent, and must not starve
    /// otherwise). This is the complement of
    /// `regular_stream_must_not_starve_sendordered_in_group`, which checks the other
    /// direction (the regular stream must not starve the sendordered one).
    #[test]
    fn round_robin_serves_regular_and_sendordered_in_group() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        let regular = StreamId::from(0);
        let sendordered = StreamId::from(4);
        for id in [regular, sendordered] {
            let mut s = SendStream::new(id, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
            s.send(&[0; 8]).unwrap();
            ss.insert(id, s);
            ss.set_fairness(id, true).unwrap();
            ss.set_sendgroup(id, Some(SendGroupId::new(1))).unwrap();
        }
        ss.set_sendorder(sendordered, Some(100)).unwrap();

        // Strict priority means the sendordered stream drains first, then the regular
        // stream gets a turn in a later pass; collect the order across several passes.
        let mut order = Vec::new();
        for _ in 0..4 {
            let mut tokens = recovery::Tokens::new();
            let mut builder =
                packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
            ss.write_frames(
                TransmissionPriority::default(),
                &mut builder,
                &mut tokens,
                &mut FrameStats::default(),
            );
            while !tokens.is_empty() {
                let id = as_stream_token(&tokens.remove(0)).id;
                if !order.contains(&id) {
                    order.push(id);
                }
            }
        }

        assert_eq!(
            order.first(),
            Some(&sendordered),
            "higher-sendOrder stream should be served before the regular stream"
        );
        assert!(
            order.contains(&regular),
            "regular stream permanently starved by the sendordered stream in the same group"
        );
    }

    /// Within a group, a null-sendOrder (regular) stream with enough data to fill a
    /// packet must not starve a higher-priority sendordered stream in the same group.
    /// A regular stream that fills the packet must still leave the group's turn to the
    /// higher-sendOrder stream first (the sendordered stream must get served).
    ///
    /// This mirrors `round_robin_serves_regular_and_sendordered_in_group`, but with a
    /// realistic payload: that test sends only 8 bytes, so the regular stream never fills
    /// the packet and the inversion stays hidden.
    #[test]
    fn regular_stream_must_not_starve_sendordered_in_group() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        let regular = StreamId::from(0);
        let sendordered = StreamId::from(4);

        // Regular (null-sendOrder) stream with more data than fits in one packet.
        let mut r = SendStream::new(regular, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
        r.send(&[0; 4096]).unwrap();
        ss.insert(regular, r);
        ss.set_fairness(regular, true).unwrap();
        ss.set_sendgroup(regular, Some(SendGroupId::new(1)))
            .unwrap();

        // Higher-priority sendordered stream with only a few bytes: it easily fits
        // alongside the regular stream if the scheduler gives it a turn.
        let mut s = SendStream::new(sendordered, 1 << 20, conn_fc, conn_events);
        s.send(&[0; 8]).unwrap();
        ss.insert(sendordered, s);
        ss.set_fairness(sendordered, true).unwrap();
        ss.set_sendgroup(sendordered, Some(SendGroupId::new(1)))
            .unwrap();
        ss.set_sendorder(sendordered, Some(100)).unwrap();

        // Constrain the packet so a single stream's frame fills it, forcing the
        // scheduler to choose which stream to serve first.
        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        builder.set_limit(builder.len() + 30);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );

        let mut served = std::collections::HashSet::new();
        while !tokens.is_empty() {
            served.insert(as_stream_token(&tokens.remove(0)).id);
        }
        assert!(
            served.contains(&sendordered),
            "higher-priority sendordered stream starved by the regular stream in the same group"
        );
    }

    /// A highest-sendOrder stream blocked by flow control emits only `STREAM_DATA_BLOCKED`,
    /// which must not end its group's turn and starve a lower-sendOrder peer with sendable
    /// data in the same group (WebTransport spec write-chunk 6.3 step 6.1). A second group
    /// forces the multi-group scheduler path rather than the single-group fast path.
    #[test]
    fn flow_control_blocked_stream_does_not_starve_sendorder_peer() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        // Group 1: a high-sendOrder stream blocked by stream flow control (an atomic write
        // larger than its credit marks it blocked without buffering any data, so it emits
        // only STREAM_DATA_BLOCKED), plus a lower-sendOrder stream that can send.
        let blocked_high = StreamId::from(0);
        let mut s = SendStream::new(blocked_high, 2, Rc::clone(&conn_fc), conn_events.clone());
        assert_eq!(s.send_atomic(&[0; 8]).unwrap(), 0);
        ss.insert(blocked_high, s);
        ss.set_fairness(blocked_high, true).unwrap();
        ss.set_sendgroup(blocked_high, Some(SendGroupId::new(1)))
            .unwrap();
        ss.set_sendorder(blocked_high, Some(100)).unwrap();

        let low = StreamId::from(4);
        let mut s = SendStream::new(low, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
        s.send(&[0; 8]).unwrap();
        ss.insert(low, s);
        ss.set_fairness(low, true).unwrap();
        ss.set_sendgroup(low, Some(SendGroupId::new(1))).unwrap();
        ss.set_sendorder(low, Some(50)).unwrap();

        // Group 2: a separate group so the multi-group scheduler path is taken.
        let other = StreamId::from(8);
        let mut s = SendStream::new(other, 1 << 20, conn_fc, conn_events);
        s.send(&[0; 8]).unwrap();
        ss.insert(other, s);
        ss.set_fairness(other, true).unwrap();
        ss.set_sendgroup(other, Some(SendGroupId::new(2))).unwrap();

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );

        // Only STREAM frames produce `Stream` tokens; the blocked stream's
        // STREAM_DATA_BLOCKED token is a different variant and is skipped.
        let mut served = std::collections::HashSet::new();
        while !tokens.is_empty() {
            if let recovery::Token::Stream(StreamRecoveryToken::Stream(rt)) = &tokens.remove(0) {
                served.insert(rt.id);
            }
        }
        assert!(
            served.contains(&low),
            "lower-sendOrder stream starved by a flow-control-blocked higher-sendOrder peer"
        );
    }

    /// Two streams share the highest sendOrder bucket; the round-robin cursor starts on a
    /// drained-but-open stream. Serving a single stream per bucket would skip the bucket
    /// entirely (the cursor stream has no data) and fall through to a lower-sendOrder
    /// stream -- even though the bucket's other stream has sendable data. Per the
    /// WebTransport send-order rules a lower-sendOrder stream MUST starve until all bytes
    /// on higher-sendOrder same-group streams (that are neither errored nor flow-blocked)
    /// have been sent, so the bucket must be scanned until a data-bearing stream is found.
    #[test]
    fn drained_stream_must_not_starve_sendorder_peer_in_bucket() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        // Lowest StreamId in the highest bucket, so the round-robin cursor lands here
        // first -- but it has no queued data.
        let drained_high = StreamId::from(0);
        let data_high = StreamId::from(4);
        let low = StreamId::from(8);

        let s = SendStream::new(
            drained_high,
            1 << 20,
            Rc::clone(&conn_fc),
            conn_events.clone(),
        );
        ss.insert(drained_high, s);
        ss.set_sendorder(drained_high, Some(100)).unwrap();

        // More data than fits the packet below, so the higher bucket still has unsent
        // bytes after this stream is served -- the lower bucket must keep starving.
        let mut s = SendStream::new(data_high, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
        s.send(&[0; 256]).unwrap();
        ss.insert(data_high, s);
        ss.set_sendorder(data_high, Some(100)).unwrap();

        let mut s = SendStream::new(low, 1 << 20, conn_fc, conn_events);
        s.send(&[0; 8]).unwrap();
        ss.insert(low, s);
        ss.set_sendorder(low, Some(50)).unwrap();

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        builder.set_limit(builder.len() + 30);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );

        let mut served = std::collections::HashSet::new();
        while !tokens.is_empty() {
            served.insert(as_stream_token(&tokens.remove(0)).id);
        }
        assert!(
            served.contains(&data_high),
            "data-bearing stream in the highest sendOrder bucket was starved"
        );
        assert!(
            !served.contains(&low),
            "lower-sendOrder stream served while a higher-sendOrder peer had sendable data"
        );
    }

    /// In the null-sendOrder (regular) bucket the round-robin cursor may land on a
    /// drained-but-open stream. The group must still serve another regular stream that has
    /// data this pass rather than wasting its turn (null-sendOrder streams have no priority
    /// among themselves, so this is a latency property, not strict ordering). A drained
    /// sendordered stream keeps `sendordered` non-empty, forcing the general scheduler path
    /// rather than the single-group fast path so the regular bucket is reached here.
    #[test]
    fn drained_regular_stream_does_not_waste_group_turn() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        // Regular (null-sendOrder) bucket: a drained stream at the cursor (lowest StreamId)
        // plus a stream with data.
        let drained = StreamId::from(0);
        let data = StreamId::from(4);
        let s = SendStream::new(drained, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
        ss.insert(drained, s);
        ss.set_fairness(drained, true).unwrap();

        let mut s = SendStream::new(data, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
        s.send(&[0; 8]).unwrap();
        ss.insert(data, s);
        ss.set_fairness(data, true).unwrap();

        // A drained sendordered stream makes `sendordered` non-empty so write_frames takes
        // the general path and reaches the regular bucket after the (empty) ordered pass.
        let ordered = StreamId::from(8);
        let s = SendStream::new(ordered, 1 << 20, conn_fc, conn_events);
        ss.insert(ordered, s);
        ss.set_sendorder(ordered, Some(100)).unwrap();

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        builder.set_limit(builder.len() + 30);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );

        let mut served = std::collections::HashSet::new();
        while !tokens.is_empty() {
            served.insert(as_stream_token(&tokens.remove(0)).id);
        }
        assert!(
            served.contains(&data),
            "data-bearing regular stream starved by a drained peer at the cursor"
        );
    }

    /// A grouped stream made non-fair and then fair again must be re-queued, not left
    /// recorded in a group it was removed from. Otherwise the general (multi-group)
    /// scheduler never serves it (permanent starvation).
    #[test]
    fn set_fairness_false_then_true_requeues_grouped_stream() {
        let conn_fc = connection_fc(u64::MAX);
        let conn_events = ConnectionEvents::default();
        let mut ss = SendStreams::default();

        let groups = [
            (StreamId::from(0), SendGroupId::new(1)),
            (StreamId::from(4), SendGroupId::new(2)),
            (StreamId::from(8), SendGroupId::new(3)),
        ];
        let toggled = groups[0].0;
        for (id, gid) in groups {
            let mut s = SendStream::new(id, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
            s.send(&[0; 8]).unwrap();
            ss.insert(id, s);
            ss.set_fairness(id, true).unwrap();
            ss.set_sendgroup(id, Some(gid)).unwrap();
        }

        // Two other groups remain after `toggled` leaves its group, forcing the general
        // multi-group scheduler path rather than the single-group fast path.
        ss.set_fairness(toggled, false).unwrap();
        ss.set_fairness(toggled, true).unwrap();

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );

        let mut served = std::collections::HashSet::new();
        while !tokens.is_empty() {
            served.insert(as_stream_token(&tokens.remove(0)).id);
        }
        assert!(
            served.contains(&toggled),
            "stream starved after fair -> non-fair -> fair transition"
        );
    }

    #[test]
    fn mark_acked_from_zero() {
        let mut rt = RangeTracker::default();

        // ranges can go from nothing->Sent if queued for retrans and then
        // acks arrive
        rt.mark_acked(5, 5);
        assert_eq!(rt.highest_offset(), 10);
        assert_eq!(rt.acked_from_zero(), 0);
        rt.mark_acked(10, 4);
        assert_eq!(rt.highest_offset(), 14);
        assert_eq!(rt.acked_from_zero(), 0);

        rt.mark_sent(0, 5);
        assert_eq!(rt.highest_offset(), 14);
        assert_eq!(rt.acked_from_zero(), 0);
        rt.mark_acked(0, 5);
        assert_eq!(rt.highest_offset(), 14);
        assert_eq!(rt.acked_from_zero(), 14);

        rt.mark_acked(12, 20);
        assert_eq!(rt.highest_offset(), 32);
        assert_eq!(rt.acked_from_zero(), 32);

        // ack the lot
        rt.mark_acked(0, 400);
        assert_eq!(rt.highest_offset(), 400);
        assert_eq!(rt.acked_from_zero(), 400);

        // acked trumps sent
        rt.mark_sent(0, 200);
        assert_eq!(rt.highest_offset(), 400);
        assert_eq!(rt.acked_from_zero(), 400);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///   SSS  SSSAAASSS
    /// +    AAAAAAAAA
    /// = SSSAAAAAAAAASS
    /// ```
    #[test]
    fn mark_acked_1() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 3);
        rt.mark_sent(6, 3);
        rt.mark_acked(9, 3);
        rt.mark_sent(12, 3);

        rt.mark_acked(3, 10);

        let mut canon = RangeTracker::default();
        canon.used.insert(0, (3, RangeState::Sent));
        canon.used.insert(3, (10, RangeState::Acked));
        canon.used.insert(13, (2, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///   SSS  SSS   AAA
    /// +   AAAAAAAAA
    /// = SSAAAAAAAAAAAA
    /// ```
    #[test]
    fn mark_acked_2() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 3);
        rt.mark_sent(6, 3);
        rt.mark_acked(12, 3);

        rt.mark_acked(2, 10);

        let mut canon = RangeTracker::default();
        canon.used.insert(0, (2, RangeState::Sent));
        canon.used.insert(2, (13, RangeState::Acked));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///    AASSS  AAAA
    /// + AAAAAAAAA
    /// = AAAAAAAAAAAA
    /// ```
    #[test]
    fn mark_acked_3() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(1, 2);
        rt.mark_sent(3, 3);
        rt.mark_acked(8, 4);

        rt.mark_acked(0, 9);

        let canon = RangeTracker {
            acked: 12,
            ..RangeTracker::default()
        };
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///      SSS
    /// + AAAA
    /// = AAAASS
    /// ```
    #[test]
    fn mark_acked_4() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(3, 3);

        rt.mark_acked(0, 4);

        let mut canon = RangeTracker {
            acked: 4,
            ..Default::default()
        };
        canon.used.insert(4, (2, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///   AAAAAASSS
    /// +    AAA
    /// = AAAAAASSS
    /// ```
    #[test]
    fn mark_acked_5() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(0, 6);
        rt.mark_sent(6, 3);

        rt.mark_acked(3, 3);

        let mut canon = RangeTracker {
            acked: 6,
            ..RangeTracker::default()
        };
        canon.used.insert(6, (3, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///      AAA  AAA  AAA
    /// +       AAAAAAA
    /// =    AAAAAAAAAAAAA
    /// ```
    #[test]
    fn mark_acked_6() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(3, 3);
        rt.mark_acked(8, 3);
        rt.mark_acked(13, 3);

        rt.mark_acked(6, 7);

        let mut canon = RangeTracker::default();
        canon.used.insert(3, (13, RangeState::Acked));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///      AAA  AAA
    /// +       AAA
    /// =    AAAAAAAA
    /// ```
    #[test]
    fn mark_acked_7() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(3, 3);
        rt.mark_acked(8, 3);

        rt.mark_acked(6, 3);

        let mut canon = RangeTracker::default();
        canon.used.insert(3, (8, RangeState::Acked));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///   SSSSSSSS
    /// +   AAAA
    /// = SSAAAASS
    /// ```
    #[test]
    fn mark_acked_8() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 8);

        rt.mark_acked(2, 4);

        let mut canon = RangeTracker::default();
        canon.used.insert(0, (2, RangeState::Sent));
        canon.used.insert(2, (4, RangeState::Acked));
        canon.used.insert(6, (2, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_acked` correctly handles all paths.
    /// ```ignore
    ///        SSS
    /// + AAA
    /// = AAA  SSS
    /// ```
    #[test]
    fn mark_acked_9() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(5, 3);

        rt.mark_acked(0, 3);

        let mut canon = RangeTracker {
            acked: 3,
            ..Default::default()
        };
        canon.used.insert(5, (3, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///   AAA   AAA   SSS
    /// + SSSSSSSSSSSS
    /// = AAASSSAAASSSSSS
    /// ```
    #[test]
    fn mark_sent_1() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(0, 3);
        rt.mark_acked(6, 3);
        rt.mark_sent(12, 3);

        rt.mark_sent(0, 12);

        let mut canon = RangeTracker {
            acked: 3,
            ..RangeTracker::default()
        };
        canon.used.insert(3, (3, RangeState::Sent));
        canon.used.insert(6, (3, RangeState::Acked));
        canon.used.insert(9, (6, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///   AAASS AAA S SSSS
    /// + SSSSSSSSSSSSS
    /// = AAASSSAAASSSSSSS
    /// ```
    #[test]
    fn mark_sent_2() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(0, 3);
        rt.mark_sent(3, 2);
        rt.mark_acked(6, 3);
        rt.mark_sent(10, 1);
        rt.mark_sent(12, 4);

        rt.mark_sent(0, 13);

        let mut canon = RangeTracker {
            acked: 3,
            ..RangeTracker::default()
        };
        canon.used.insert(3, (3, RangeState::Sent));
        canon.used.insert(6, (3, RangeState::Acked));
        canon.used.insert(9, (7, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///   AAA  AAA
    /// +   SSSS
    /// = AAASSAAA
    /// ```
    #[test]
    fn mark_sent_3() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(0, 3);
        rt.mark_acked(5, 3);

        rt.mark_sent(2, 4);

        let mut canon = RangeTracker {
            acked: 3,
            ..RangeTracker::default()
        };
        canon.used.insert(3, (2, RangeState::Sent));
        canon.used.insert(5, (3, RangeState::Acked));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///   SSS  AAA  SS
    /// +   SSSSSSSS
    /// = SSSSSAAASSSS
    /// ```
    #[test]
    fn mark_sent_4() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 3);
        rt.mark_acked(5, 3);
        rt.mark_sent(10, 2);

        rt.mark_sent(2, 8);

        let mut canon = RangeTracker::default();
        canon.used.insert(0, (5, RangeState::Sent));
        canon.used.insert(5, (3, RangeState::Acked));
        canon.used.insert(8, (4, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///     AAA
    /// +   SSSSSS
    /// =   AAASSS
    /// ```
    #[test]
    fn mark_sent_5() {
        let mut rt = RangeTracker::default();
        rt.mark_acked(3, 3);

        rt.mark_sent(3, 6);

        let mut canon = RangeTracker::default();
        canon.used.insert(3, (3, RangeState::Acked));
        canon.used.insert(6, (3, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    /// Check that `marked_sent` correctly handles all paths.
    /// ```ignore
    ///   SSSSS
    /// +  SSS
    /// = SSSSS
    /// ```
    #[test]
    fn mark_sent_6() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 5);

        rt.mark_sent(1, 3);

        let mut canon = RangeTracker::default();
        canon.used.insert(0, (5, RangeState::Sent));
        assert_eq!(rt, canon);
    }

    #[test]
    fn unmark_sent_start() {
        let mut rt = RangeTracker::default();

        rt.mark_sent(0, 5);
        assert_eq!(rt.highest_offset(), 5);
        assert_eq!(rt.acked_from_zero(), 0);

        assert!(rt.unmark_sent());
        assert_eq!(rt.highest_offset(), 0);
        assert_eq!(rt.acked_from_zero(), 0);
        assert_eq!(rt.first_unmarked_range(), (0, None));
    }

    #[test]
    fn unmark_sent_middle() {
        let mut rt = RangeTracker::default();

        rt.mark_acked(0, 5);
        assert_eq!(rt.highest_offset(), 5);
        assert_eq!(rt.acked_from_zero(), 5);
        rt.mark_sent(5, 5);
        assert_eq!(rt.highest_offset(), 10);
        assert_eq!(rt.acked_from_zero(), 5);
        rt.mark_acked(10, 5);
        assert_eq!(rt.highest_offset(), 15);
        assert_eq!(rt.acked_from_zero(), 5);
        assert_eq!(rt.first_unmarked_range(), (15, None));

        assert!(rt.unmark_sent());
        assert_eq!(rt.highest_offset(), 15);
        assert_eq!(rt.acked_from_zero(), 5);
        assert_eq!(rt.first_unmarked_range(), (5, Some(5)));
    }

    #[test]
    fn unmark_sent_end() {
        let mut rt = RangeTracker::default();

        rt.mark_acked(0, 5);
        assert_eq!(rt.highest_offset(), 5);
        assert_eq!(rt.acked_from_zero(), 5);
        rt.mark_sent(5, 5);
        assert_eq!(rt.highest_offset(), 10);
        assert_eq!(rt.acked_from_zero(), 5);
        assert_eq!(rt.first_unmarked_range(), (10, None));

        assert!(rt.unmark_sent());
        assert_eq!(rt.highest_offset(), 5);
        assert_eq!(rt.acked_from_zero(), 5);
        assert_eq!(rt.first_unmarked_range(), (5, None));
    }

    /// Nothing to unmark when nothing is outstanding, which preserves the resend allowance.
    #[test]
    fn unmark_sent_nothing_outstanding() {
        let mut rt = RangeTracker::default();
        assert!(!rt.unmark_sent(), "nothing sent");
        rt.mark_acked(0, 5);
        assert!(!rt.unmark_sent(), "all acked");
        assert_eq!(rt.acked_from_zero(), 5);
    }

    #[test]
    fn truncate_front() {
        let mut v = VecDeque::new();
        v.push_back(5);
        v.push_back(6);
        v.push_back(7);
        v.push_front(4usize);

        v.rotate_left(1);
        v.truncate(3);
        assert_eq!(*v.front().unwrap(), 5);
        assert_eq!(*v.back().unwrap(), 7);
    }

    #[test]
    fn unmark_range() {
        let mut rt = RangeTracker::default();

        rt.mark_acked(5, 5);
        rt.mark_sent(10, 5);

        // Should unmark sent but not acked range
        rt.unmark_range(7, 6);

        let res = rt.first_unmarked_range();
        assert_eq!(res, (0, Some(5)));
        assert_eq!(
            rt.used.first_key_value().unwrap(),
            (&5, &(5, RangeState::Acked))
        );
        assert_eq!(
            rt.used.iter().nth(1).unwrap(),
            (&13, &(2, RangeState::Sent))
        );
        assert!(rt.used.iter().nth(2).is_none());
        rt.mark_sent(0, 5);

        let res = rt.first_unmarked_range();
        assert_eq!(res, (10, Some(3)));
        rt.mark_sent(10, 3);

        let res = rt.first_unmarked_range();
        assert_eq!(res, (15, None));
    }

    /// Unmarking inside a range keeps the tail beyond it, which is still `Sent`.
    #[test]
    fn unmark_range_keeps_tail_of_spanning_range() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 30);

        rt.unmark_range(10, 10);

        assert_eq!(rt.first_unmarked_range(), (10, Some(10)));
        assert_eq!(rt.highest_offset(), 30);
    }

    /// Losing a frame inside a coalesced range retransmits only that frame.
    #[test]
    fn tx_buffer_lost_frame_retransmits_only_that_frame() {
        let mut txb = TxBuffer::new();
        assert_eq!(txb.send(&[1; 3000]), 3000);
        // Three frames coalesce into one `Sent` range.
        txb.mark_as_sent(0, 1000);
        txb.mark_as_sent(1000, 1000);
        txb.mark_as_sent(2000, 1000);

        txb.mark_as_lost(1000, 1000);

        assert_eq!(
            txb.next_bytes().map(|(off, data)| (off, data.len())),
            Some((1000, 1000))
        );
        // Nothing is pending after retransmitting it.
        txb.mark_as_sent(1000, 1000);
        assert!(txb.next_bytes().is_none());
    }

    #[test]
    fn tx_buffer_next_bytes_1() {
        let mut txb = TxBuffer::new();

        // Fill the buffer
        let big_buf = vec![1; INITIAL_LOCAL_MAX_STREAM_DATA];
        assert_eq!(txb.send(&big_buf), INITIAL_LOCAL_MAX_STREAM_DATA);
        assert!(matches!(txb.next_bytes(),
                         Some((0, x)) if x.len() == INITIAL_LOCAL_MAX_STREAM_DATA
                         && x.iter().all(|ch| *ch == 1)));

        // Mark almost all as sent. Get what's left
        let one_byte_from_end = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) - 1;
        txb.mark_as_sent(0, expect_usize(one_byte_from_end));
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 1
                         && start == one_byte_from_end
                         && x.iter().all(|ch| *ch == 1)));

        // Mark all as sent. Get nothing
        txb.mark_as_sent(0, INITIAL_LOCAL_MAX_STREAM_DATA);
        assert!(txb.next_bytes().is_none());

        // Mark as lost. Get it again
        txb.mark_as_lost(one_byte_from_end, 1);
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 1
                         && start == one_byte_from_end
                         && x.iter().all(|ch| *ch == 1)));

        // Mark a larger range lost, including beyond what's in the buffer even.
        // Get a little more
        let five_bytes_from_end = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) - 5;
        txb.mark_as_lost(five_bytes_from_end, 100);
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 5
                         && start == five_bytes_from_end
                         && x.iter().all(|ch| *ch == 1)));

        // Contig acked range at start means it can be removed from buffer
        // Impl of vecdeque should now result in a split buffer when more data
        // is sent
        txb.mark_as_acked(0, expect_usize(five_bytes_from_end));
        assert_eq!(txb.send(&[2; 30]), 30);
        // Just get 5 even though there is more
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 5
                         && start == five_bytes_from_end
                         && x.iter().all(|ch| *ch == 1)));
        assert_eq!(txb.retired(), five_bytes_from_end);
        assert_eq!(txb.buffered(), 35);

        // Marking that bit as sent should let the last contig bit be returned
        // when called again
        txb.mark_as_sent(five_bytes_from_end, 5);
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 30
                         && start == to_u64(INITIAL_LOCAL_MAX_STREAM_DATA)
                         && x.iter().all(|ch| *ch == 2)));
    }

    #[test]
    fn tx_buffer_next_bytes_2() {
        let mut txb = TxBuffer::new();

        // Fill the buffer
        let big_buf = vec![1; INITIAL_LOCAL_MAX_STREAM_DATA];
        assert_eq!(txb.send(&big_buf), INITIAL_LOCAL_MAX_STREAM_DATA);
        assert!(matches!(txb.next_bytes(),
                         Some((0, x)) if x.len()==INITIAL_LOCAL_MAX_STREAM_DATA
                         && x.iter().all(|ch| *ch == 1)));

        // As above
        let forty_bytes_from_end = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) - 40;

        txb.mark_as_acked(0, expect_usize(forty_bytes_from_end));
        assert!(matches!(txb.next_bytes(),
                 Some((start, x)) if x.len() == 40
                 && start == forty_bytes_from_end
        ));

        // Valid new data placed in split locations
        assert_eq!(txb.send(&[2; 100]), 100);

        // Mark a little more as sent
        txb.mark_as_sent(forty_bytes_from_end, 10);
        let thirty_bytes_from_end = forty_bytes_from_end + 10;
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 30
                         && start == thirty_bytes_from_end
                         && x.iter().all(|ch| *ch == 1)));

        // Mark a range 'A' in second slice as sent. Should still return the same
        let range_a_start = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) + 30;
        let range_a_end = range_a_start + 10;
        txb.mark_as_sent(range_a_start, 10);
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 30
                         && start == thirty_bytes_from_end
                         && x.iter().all(|ch| *ch == 1)));

        // Ack entire first slice and into second slice
        let ten_bytes_past_end = to_u64(INITIAL_LOCAL_MAX_STREAM_DATA) + 10;
        txb.mark_as_acked(0, expect_usize(ten_bytes_past_end));

        // Get up to marked range A
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 20
                         && start == ten_bytes_past_end
                         && x.iter().all(|ch| *ch == 2)));

        txb.mark_as_sent(ten_bytes_past_end, 20);

        // Get bit after earlier marked range A
        assert!(matches!(txb.next_bytes(),
                         Some((start, x)) if x.len() == 60
                         && start == range_a_end
                         && x.iter().all(|ch| *ch == 2)));

        // No more bytes.
        txb.mark_as_sent(range_a_end, 60);
        assert!(txb.next_bytes().is_none());
    }

    #[test]
    fn stream_tx() {
        let conn_fc = connection_fc(4096);
        let conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(4.into(), 1024, Rc::clone(&conn_fc), conn_events);
        assert_eq!(s.to_string(), "SendStream 4");

        let res = s.send(&[4; 100]).unwrap();
        assert_eq!(res, 100);
        s.mark_as_sent(0, 50, false);
        if let State::Send { fc, .. } = s.state() {
            assert_eq!(fc.used(), 100);
        } else {
            panic!("unexpected stream state");
        }

        // Should hit stream flow control limit before filling up send buffer
        let big_buf = vec![4; INITIAL_LOCAL_MAX_STREAM_DATA + 100];
        let res = s.send(&big_buf[..INITIAL_LOCAL_MAX_STREAM_DATA]).unwrap();
        assert_eq!(res, 1024 - 100);

        // should do nothing, max stream data already 1024
        s.set_max_stream_data(1024);
        let res = s.send(&big_buf[..INITIAL_LOCAL_MAX_STREAM_DATA]).unwrap();
        assert_eq!(res, 0);

        // should now hit the conn flow control (4096)
        s.set_max_stream_data(1_048_576);
        let res = s.send(&big_buf[..INITIAL_LOCAL_MAX_STREAM_DATA]).unwrap();
        assert_eq!(res, 3072);

        // should now hit the tx buffer size
        conn_fc
            .borrow_mut()
            .update(to_u64(INITIAL_LOCAL_MAX_STREAM_DATA));
        let res = s.send(&big_buf).unwrap();
        assert_eq!(res, INITIAL_LOCAL_MAX_STREAM_DATA - 4096);

        // TODO(agrover@mozilla.com): test ooo acks somehow
        s.mark_as_acked(0, 40, false);
    }

    #[test]
    fn tx_buffer_acks() {
        let mut tx = TxBuffer::new();
        assert_eq!(tx.send(&[4; 100]), 100);
        let res = tx.next_bytes().unwrap();
        assert_eq!(res.0, 0);
        assert_eq!(res.1.len(), 100);
        tx.mark_as_sent(0, 100);
        let res = tx.next_bytes();
        assert_eq!(res, None);

        tx.mark_as_acked(0, 100);
        let res = tx.next_bytes();
        assert_eq!(res, None);
    }

    #[test]
    fn send_stream_writable_event_gen() {
        let conn_fc = connection_fc(2);
        let mut conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(4.into(), 0, Rc::clone(&conn_fc), conn_events.clone());

        // Stream is initially blocked (conn:2, stream:0)
        // and will not accept data.
        assert_eq!(s.send(b"hi").unwrap(), 0);

        // increasing to (conn:2, stream:2) will allow 2 bytes, and also
        // generate a SendStreamWritable event.
        s.set_max_stream_data(2);
        let evts = conn_events.events().collect::<Vec<_>>();
        assert_eq!(evts.len(), 1);
        assert!(matches!(
            evts[0],
            ConnectionEvent::SendStreamWritable { .. }
        ));
        assert_eq!(s.send(b"hello").unwrap(), 2);

        // increasing to (conn:2, stream:4) will not generate an event or allow
        // sending anything.
        s.set_max_stream_data(4);
        assert_eq!(conn_events.events().count(), 0);
        assert_eq!(s.send(b"hello").unwrap(), 0);

        // Increasing conn max (conn:4, stream:4) will unblock but not emit
        // event b/c that happens in Connection::emit_frame() (tested in
        // connection.rs)
        assert!(conn_fc.borrow_mut().update(4).is_some());
        assert_eq!(conn_events.events().count(), 0);
        assert_eq!(s.avail(), 2);
        assert_eq!(s.send(b"hello").unwrap(), 2);

        // No event because still blocked by conn
        s.set_max_stream_data(1_000_000_000);
        assert_eq!(conn_events.events().count(), 0);

        // No event because happens in emit_frame()
        conn_fc.borrow_mut().update(1_000_000_000);
        assert_eq!(conn_events.events().count(), 0);

        let big_buf = vec![b'a'; INITIAL_LOCAL_MAX_STREAM_DATA];
        assert_eq!(s.send(&big_buf).unwrap(), INITIAL_LOCAL_MAX_STREAM_DATA);
    }

    #[test]
    fn send_stream_writable_event_gen_with_watermark() {
        let conn_fc = connection_fc(0);
        let mut conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(4.into(), 0, Rc::clone(&conn_fc), conn_events.clone());
        // Set watermark at 3.
        s.set_writable_event_low_watermark(NonZeroUsize::new(3).unwrap());

        // Stream is initially blocked (conn:0, stream:0, watermark: 3) and will
        // not accept data.
        assert_eq!(s.avail(), 0);
        assert_eq!(s.send(b"hi!").unwrap(), 0);

        // Increasing the connection limit (conn:10, stream:0, watermark: 3) will not generate
        // event or allow sending anything. Stream is constrained by stream limit.
        assert!(conn_fc.borrow_mut().update(10).is_some());
        assert_eq!(s.avail(), 0);
        assert_eq!(conn_events.events().count(), 0);

        // Increasing the connection limit further (conn:11, stream:0, watermark: 3) will not
        // generate event or allow sending anything. Stream wasn't constrained by connection
        // limit before.
        assert!(conn_fc.borrow_mut().update(11).is_some());
        assert_eq!(s.avail(), 0);
        assert_eq!(conn_events.events().count(), 0);

        // Increasing to (conn:11, stream:2, watermark: 3) will allow 2 bytes
        // but not generate a SendStreamWritable event as it is still below the
        // configured watermark.
        s.set_max_stream_data(2);
        assert_eq!(conn_events.events().count(), 0);
        assert_eq!(s.avail(), 2);

        // Increasing to (conn:11, stream:3, watermark: 3) will generate an
        // event as available sendable bytes are >= watermark.
        s.set_max_stream_data(3);
        let evts = conn_events.events().collect::<Vec<_>>();
        assert_eq!(evts.len(), 1);
        assert!(matches!(
            evts[0],
            ConnectionEvent::SendStreamWritable { .. }
        ));

        assert_eq!(s.send(b"hi!").unwrap(), 3);
    }

    #[test]
    fn send_stream_writable_event_new_stream() {
        let conn_fc = connection_fc(2);
        let mut conn_events = ConnectionEvents::default();

        let _s = SendStream::new(4.into(), 100, conn_fc, conn_events.clone());

        // Creating a new stream with conn and stream credits should result in
        // an event.
        let evts = conn_events.events().collect::<Vec<_>>();
        assert_eq!(evts.len(), 1);
        assert!(matches!(
            evts[0],
            ConnectionEvent::SendStreamWritable { .. }
        ));
    }

    const fn as_stream_token(t: &recovery::Token) -> &RecoveryToken {
        if let recovery::Token::Stream(StreamRecoveryToken::Stream(rt)) = &t {
            rt
        } else {
            panic!();
        }
    }

    #[test]
    // Verify lost frames handle fin properly
    fn send_stream_get_frame_data() {
        let conn_fc = connection_fc(100);
        let conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(0.into(), 100, conn_fc, conn_events);
        s.send(&[0; 10]).unwrap();
        s.close();

        let mut ss = SendStreams::default();
        assert!(!ss.exists(StreamId::from(0)));
        ss.insert(StreamId::from(0), s);
        assert!(ss.exists(StreamId::from(0)));

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);

        // Write a small frame: no fin.
        let written = builder.len();
        builder.set_limit(written + 6);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert_eq!(builder.len(), written + 6);
        assert_eq!(tokens.len(), 1);
        let f1_token = tokens.remove(0);
        assert!(!as_stream_token(&f1_token).fin);

        // Write the rest: fin.
        let written = builder.len();
        builder.set_limit(written + 200);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert_eq!(builder.len(), written + 10);
        assert_eq!(tokens.len(), 1);
        let f2_token = tokens.remove(0);
        assert!(as_stream_token(&f2_token).fin);

        // Should be no more data to frame.
        let written = builder.len();
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert_eq!(builder.len(), written);
        assert!(tokens.is_empty());

        // Mark frame 1 as lost
        ss.lost(as_stream_token(&f1_token));

        // Next frame should not set fin even though stream has fin but frame
        // does not include end of stream
        let written = builder.len();
        ss.write_frames(
            TransmissionPriority::default() + RetransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert_eq!(builder.len(), written + 7); // Needs a length this time.
        assert_eq!(tokens.len(), 1);
        let f4_token = tokens.remove(0);
        assert!(!as_stream_token(&f4_token).fin);

        // Mark frame 2 as lost
        ss.lost(as_stream_token(&f2_token));

        // Next frame should set fin because it includes end of stream
        let written = builder.len();
        ss.write_frames(
            TransmissionPriority::default() + RetransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert_eq!(builder.len(), written + 10);
        assert_eq!(tokens.len(), 1);
        let f5_token = tokens.remove(0);
        assert!(as_stream_token(&f5_token).fin);
    }

    /// Several fair streams in the single null sendGroup with no sendOrder, each
    /// with more data than a (tightly limited) packet can carry.  When the builder
    /// fills after one stream per call, the single-group fast path must round-robin
    /// so every stream makes progress: per the WebTransport spec (write-chunk 6.3
    /// step 6.1) null-sendOrder streams "MUST NOT starve".  The earlier map-order
    /// fast path always re-served the first stream, starving the rest -- this test
    /// fails against that behaviour.
    #[test]
    fn write_frames_fair_round_robin_no_starvation() {
        const STREAMS: u64 = 4;
        let conn_fc = connection_fc(1 << 20);
        let conn_events = ConnectionEvents::default();

        let mut ss = SendStreams::default();
        for i in 0..STREAMS {
            let id = StreamId::from(i * 4);
            let mut s = SendStream::new(id, 1 << 20, Rc::clone(&conn_fc), conn_events.clone());
            s.send(&[0; 4096]).unwrap();
            ss.insert(id, s);
            ss.set_fairness(id, true).unwrap();
        }

        // Each call uses a fresh, tightly limited builder so only one stream's
        // frame fits, exercising the round-robin cursor across packets.
        let mut served = std::collections::HashSet::new();
        for _ in 0..STREAMS {
            let mut tokens = recovery::Tokens::new();
            let mut builder =
                packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
            builder.set_limit(builder.len() + 30);
            ss.write_frames(
                TransmissionPriority::default(),
                &mut builder,
                &mut tokens,
                &mut FrameStats::default(),
            );
            assert_eq!(tokens.len(), 1, "exactly one stream served per packet");
            let token = tokens.remove(0);
            served.insert(as_stream_token(&token).id);
        }

        assert_eq!(
            u64::try_from(served.len()).expect("count fits in u64"),
            STREAMS,
            "every fair stream must make progress (no starvation); served {served:?}"
        );
    }

    #[test]
    // Verify lost frames handle fin properly with zero length fin
    fn send_stream_get_frame_zerolength_fin() {
        let conn_fc = connection_fc(100);
        let conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(0.into(), 100, conn_fc, conn_events);
        s.send(&[0; 10]).unwrap();

        let mut ss = SendStreams::default();
        ss.insert(StreamId::from(0), s);

        let mut tokens = recovery::Tokens::new();
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        let f1_token = tokens.remove(0);
        assert_eq!(as_stream_token(&f1_token).offset, 0);
        assert_eq!(as_stream_token(&f1_token).length, 10);
        assert!(!as_stream_token(&f1_token).fin);

        // Should be no more data to frame
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        assert!(tokens.is_empty());

        ss.get_mut(StreamId::from(0)).unwrap().close();

        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        let f2_token = tokens.remove(0);
        assert_eq!(as_stream_token(&f2_token).offset, 10);
        assert_eq!(as_stream_token(&f2_token).length, 0);
        assert!(as_stream_token(&f2_token).fin);

        // Mark frame 2 as lost
        ss.lost(as_stream_token(&f2_token));

        // Next frame should set fin
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        let f3_token = tokens.remove(0);
        assert_eq!(as_stream_token(&f3_token).offset, 10);
        assert_eq!(as_stream_token(&f3_token).length, 0);
        assert!(as_stream_token(&f3_token).fin);

        // Mark frame 1 as lost
        ss.lost(as_stream_token(&f1_token));

        // Next frame should set fin and include all data
        ss.write_frames(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut FrameStats::default(),
        );
        let f4_token = tokens.remove(0);
        assert_eq!(as_stream_token(&f4_token).offset, 0);
        assert_eq!(as_stream_token(&f4_token).length, 10);
        assert!(as_stream_token(&f4_token).fin);
    }

    #[test]
    fn data_blocked() {
        let conn_fc = connection_fc(5);
        let conn_events = ConnectionEvents::default();

        let stream_id = StreamId::from(4);
        let mut s = SendStream::new(stream_id, 2, Rc::clone(&conn_fc), conn_events);

        // Only two bytes can be sent due to the stream limit.
        assert_eq!(s.send(b"abc").unwrap(), 2);
        assert_eq!(s.next_bytes(false), Some((0, &b"ab"[..])));

        // This doesn't report blocking yet.
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_blocked_frame(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.stream_data_blocked, 0);

        // Blocking is reported after sending the last available credit.
        s.mark_as_sent(0, 2, false);
        s.write_blocked_frame(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.stream_data_blocked, 1);

        // Now increase the stream limit and test the connection limit.
        s.set_max_stream_data(10);

        assert_eq!(s.send(b"abcd").unwrap(), 3);
        assert_eq!(s.next_bytes(false), Some((2, &b"abc"[..])));
        // DATA_BLOCKED is not sent yet.
        conn_fc
            .borrow_mut()
            .write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(stats.data_blocked, 0);

        // DATA_BLOCKED is queued once bytes using all credit are sent.
        s.mark_as_sent(2, 3, false);
        conn_fc
            .borrow_mut()
            .write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(stats.data_blocked, 1);
    }

    #[test]
    fn max_send_buffer_size() {
        // Huge FC limit. Thus buffer size limited only.
        const FC_LIMIT: u64 = 1024 * 1024 * 1024;
        let s = SendStream::new(
            StreamId::from(4),
            FC_LIMIT,
            connection_fc(FC_LIMIT),
            ConnectionEvents::default(),
        );
        assert_eq!(s.avail(), TxBuffer::MAX_SIZE);
    }

    #[test]
    fn data_blocked_atomic() {
        let conn_fc = connection_fc(5);
        let conn_events = ConnectionEvents::default();

        let stream_id = StreamId::from(4);
        let mut s = SendStream::new(stream_id, 2, Rc::clone(&conn_fc), conn_events);

        // Stream is initially blocked (conn:5, stream:2)
        // and will not accept atomic write of 3 bytes.
        assert_eq!(s.send_atomic(b"abc").unwrap(), 0);

        // Assert that STREAM_DATA_BLOCKED is sent.
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_blocked_frame(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.stream_data_blocked, 1);

        // Assert that a non-atomic write works.
        assert_eq!(s.send(b"abc").unwrap(), 2);
        assert_eq!(s.next_bytes(false), Some((0, &b"ab"[..])));
        s.mark_as_sent(0, 2, false);

        // Set limits to (conn:5, stream:10).
        s.set_max_stream_data(10);

        // An atomic write of 4 bytes exceeds the remaining limit of 3.
        assert_eq!(s.send_atomic(b"abcd").unwrap(), 0);

        // Assert that DATA_BLOCKED is sent.
        conn_fc
            .borrow_mut()
            .write_frames(&mut builder, &mut tokens, &mut stats);
        assert_eq!(stats.data_blocked, 1);

        // Check that a non-atomic write works.
        assert_eq!(s.send(b"abcd").unwrap(), 3);
        assert_eq!(s.next_bytes(false), Some((2, &b"abc"[..])));
        s.mark_as_sent(2, 3, false);

        // Increase limits to (conn:15, stream:15).
        s.set_max_stream_data(15);
        conn_fc.borrow_mut().update(15);

        // Check that atomic writing right up to the limit works.
        assert_eq!(s.send_atomic(b"abcdefghij").unwrap(), 10);
    }

    #[test]
    fn ack_fin_first() {
        const MESSAGE: &[u8] = b"hello";
        let len_u64 = u64::try_from(MESSAGE.len()).unwrap();

        let conn_fc = connection_fc(len_u64);
        let conn_events = ConnectionEvents::default();

        let mut s = SendStream::new(StreamId::new(100), 0, conn_fc, conn_events);
        s.set_max_stream_data(len_u64);

        // Send all the data, then the fin.
        _ = s.send(MESSAGE).unwrap();
        s.mark_as_sent(0, MESSAGE.len(), false);
        s.close();
        s.mark_as_sent(len_u64, 0, true);

        // Ack the fin, then the data.
        s.mark_as_acked(len_u64, 0, true);
        s.mark_as_acked(0, MESSAGE.len(), false);
        assert!(s.is_ended());
    }

    #[test]
    fn ack_then_lose_fin() {
        const MESSAGE: &[u8] = b"hello";
        let len_u64 = u64::try_from(MESSAGE.len()).unwrap();

        let conn_fc = connection_fc(len_u64);
        let conn_events = ConnectionEvents::default();

        let id = StreamId::new(100);
        let mut s = SendStream::new(id, 0, conn_fc, conn_events);
        s.set_max_stream_data(len_u64);

        // Send all the data, then the fin.
        _ = s.send(MESSAGE).unwrap();
        s.mark_as_sent(0, MESSAGE.len(), false);
        s.close();
        s.mark_as_sent(len_u64, 0, true);

        // Ack the fin, then mark it lost.
        s.mark_as_acked(len_u64, 0, true);
        s.mark_as_lost(len_u64, 0, true);

        // No frame should be sent here.
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_stream_frame(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.stream, 0);
    }

    /// Create a `SendStream` and force it into a state where it believes that
    /// `offset` bytes have already been sent and acknowledged.
    fn stream_with_sent(stream: u64, offset: usize) -> SendStream {
        let conn_fc = connection_fc(MAX_VARINT);
        let mut s = SendStream::new(
            StreamId::from(stream),
            MAX_VARINT,
            conn_fc,
            ConnectionEvents::default(),
        );

        let mut send_buf = TxBuffer::new();
        send_buf.ranges.mark_acked(0, offset);
        let mut fc = SenderFlowControl::new(StreamId::from(stream), MAX_VARINT);
        fc.consume(offset);
        let conn_fc = Rc::new(RefCell::new(SenderFlowControl::new((), MAX_VARINT)));
        s.state = State::Send {
            fc,
            conn_fc,
            send_buf,
            committed: 0,
        };
        s
    }

    fn frame_sent_sid(stream: u64, offset: usize, len: usize, fin: bool, space: usize) -> bool {
        const BUF: &[u8] = &[0x42; 128];

        trace!("frame_sent stream={stream} offset={offset} len={len} fin={fin}, space={space}");

        let mut s = stream_with_sent(stream, offset);

        // Now write out the proscribed data and maybe close.
        if len > 0 {
            s.send(&BUF[..len]).unwrap();
        }
        if fin {
            s.close();
        }

        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let header_len = builder.len();
        builder.set_limit(header_len + space);

        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_stream_frame(
            TransmissionPriority::default(),
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        trace!(
            "STREAM frame: {}",
            HexWithLen::new(&builder.as_ref()[header_len..])
        );
        stats.stream > 0
    }

    fn frame_sent(offset: usize, len: usize, fin: bool, space: usize) -> bool {
        frame_sent_sid(0, offset, len, fin, space)
    }

    #[test]
    fn stream_frame_empty() {
        // Stream frames with empty data and no fin never work.
        assert!(!frame_sent(10, 0, false, 2));
        assert!(!frame_sent(10, 0, false, 3));
        assert!(!frame_sent(10, 0, false, 4));
        assert!(!frame_sent(10, 0, false, 5));
        assert!(!frame_sent(10, 0, false, 100));

        // Empty data with fin is only a problem if there is no space.
        assert!(!frame_sent(0, 0, true, 1));
        assert!(frame_sent(0, 0, true, 2));
        assert!(!frame_sent(10, 0, true, 2));
        assert!(frame_sent(10, 0, true, 3));
        assert!(frame_sent(10, 0, true, 4));
        assert!(frame_sent(10, 0, true, 5));
        assert!(frame_sent(10, 0, true, 100));
    }

    #[test]
    fn stream_frame_minimum() {
        // Add minimum data
        assert!(!frame_sent(10, 1, false, 3));
        assert!(!frame_sent(10, 1, true, 3));
        assert!(frame_sent(10, 1, false, 4));
        assert!(frame_sent(10, 1, true, 4));
        assert!(frame_sent(10, 1, false, 5));
        assert!(frame_sent(10, 1, true, 5));
        assert!(frame_sent(10, 1, false, 100));
        assert!(frame_sent(10, 1, true, 100));
    }

    #[test]
    fn stream_frame_more() {
        // Try more data
        assert!(!frame_sent(10, 100, false, 3));
        assert!(!frame_sent(10, 100, true, 3));
        assert!(frame_sent(10, 100, false, 4));
        assert!(frame_sent(10, 100, true, 4));
        assert!(frame_sent(10, 100, false, 5));
        assert!(frame_sent(10, 100, true, 5));
        assert!(frame_sent(10, 100, false, 100));
        assert!(frame_sent(10, 100, true, 100));

        assert!(frame_sent(10, 100, false, 1000));
        assert!(frame_sent(10, 100, true, 1000));
    }

    #[test]
    fn stream_frame_big_id() {
        // A value that encodes to the largest varint.
        const BIG: u64 = 1 << 30;
        const BIGSZ: usize = 1 << 30;

        assert!(!frame_sent_sid(BIG, BIGSZ, 0, false, 16));
        assert!(!frame_sent_sid(BIG, BIGSZ, 0, true, 16));
        assert!(!frame_sent_sid(BIG, BIGSZ, 0, false, 17));
        assert!(frame_sent_sid(BIG, BIGSZ, 0, true, 17));
        assert!(!frame_sent_sid(BIG, BIGSZ, 0, false, 18));
        assert!(frame_sent_sid(BIG, BIGSZ, 0, true, 18));

        assert!(!frame_sent_sid(BIG, BIGSZ, 1, false, 17));
        assert!(!frame_sent_sid(BIG, BIGSZ, 1, true, 17));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, false, 18));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, true, 18));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, false, 19));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, true, 19));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, false, 100));
        assert!(frame_sent_sid(BIG, BIGSZ, 1, true, 100));
    }

    fn stream_frame_at_boundary(data: &[u8]) {
        fn send_with_extra_capacity(data: &[u8], extra: usize, expect_full: bool) -> Vec<u8> {
            trace!("send_with_extra_capacity {} + {extra}", data.len());
            let mut s = stream_with_sent(0, 0);
            s.send(data).unwrap();
            s.close();

            let mut builder =
                packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
            let header_len = builder.len();
            // Add 2 for the frame type and stream ID, then add the extra.
            builder.set_limit(header_len + data.len() + 2 + extra);
            let mut tokens = recovery::Tokens::new();
            let mut stats = FrameStats::default();
            s.write_stream_frame(
                TransmissionPriority::default(),
                &mut builder,
                &mut tokens,
                &mut stats,
            );
            assert_eq!(stats.stream, 1);
            assert_eq!(builder.is_full(), expect_full);
            Vec::from(Encoder::from(builder)).split_off(header_len)
        }

        // The minimum amount of extra space for getting another frame in.
        let mut enc = Encoder::default();
        enc.encode_len(data.len());
        let len_buf = Vec::from(enc);
        let minimum_extra = len_buf.len() + packet::Builder::MINIMUM_FRAME_SIZE;

        // For anything short of the minimum extra, the frame should fill the packet.
        for i in 0..minimum_extra {
            let frame = send_with_extra_capacity(data, i, true);
            let (header, body) = frame.split_at(2);
            assert_eq!(header, &[0b1001, 0]);
            assert_eq!(body, data);
        }

        // Once there is space for another packet AND a length field,
        // then a length will be added.
        let frame = send_with_extra_capacity(data, minimum_extra, false);
        let (header, rest) = frame.split_at(2);
        assert_eq!(header, &[0b1011, 0]);
        let (len, body) = rest.split_at(len_buf.len());
        assert_eq!(len, &len_buf);
        assert_eq!(body, data);
    }

    /// 16383/16384 is an odd boundary in STREAM frame construction.
    /// That is the boundary where a length goes from 2 bytes to 4 bytes.
    /// Test that we correctly add a length field to the frame; and test
    /// that if we don't, then we don't allow other frames to be added.
    #[test]
    fn stream_frame_16384() {
        stream_frame_at_boundary(&[4; 16383]);
        stream_frame_at_boundary(&[4; 16384]);
    }

    /// 63/64 is the other odd boundary.
    #[test]
    fn stream_frame_64() {
        stream_frame_at_boundary(&[2; 63]);
        stream_frame_at_boundary(&[2; 64]);
    }

    fn check_stats(
        stream: &SendStream,
        expected_written: u64,
        expected_sent: u64,
        expected_acked: u64,
    ) {
        let stream_stats = stream.stats();
        assert_eq!(stream_stats.bytes_written(), expected_written);
        assert_eq!(stream_stats.bytes_sent(), expected_sent);
        assert_eq!(stream_stats.bytes_acked(), expected_acked);
    }

    /// The writable event fires when the low watermark equals the previous limit and
    /// the current limit and available space now meet or exceed the watermark.
    #[test]
    fn writable_event_fires_at_watermark_equals_previous_limit() {
        let mut conn_events = ConnectionEvents::default();
        let id = StreamId::new(4);
        let limit = 100u64;
        let conn_fc = connection_fc(limit * 2);
        let mut s = SendStream::new(id, 0, conn_fc, conn_events.clone());
        s.set_max_stream_data(limit); // initial limit

        // Set watermark == previous_limit (= avail() after setting limit).
        s.set_writable_event_low_watermark(NonZeroUsize::new(s.avail()).unwrap());

        // Increase limit so current_limit > watermark and avail() > watermark.
        s.set_max_stream_data(limit * 2);

        assert!(
            conn_events
                .events()
                .any(|e| matches!(e, ConnectionEvent::SendStreamWritable { stream_id } if stream_id == id)),
            "writable event must fire when watermark == previous_limit"
        );
    }

    /// `TxBuffer::avail` equals `MAX_SIZE - buffered`.
    #[test]
    fn tx_buffer_avail_exact() {
        let mut txb = TxBuffer::new();
        assert_eq!(txb.avail(), TxBuffer::MAX_SIZE);
        txb.send(&[0; 100]);
        assert_eq!(txb.avail(), TxBuffer::MAX_SIZE - 100);
    }

    /// `TxBuffer::send` accepts at most `avail()` bytes; a full buffer rejects further data.
    #[test]
    fn tx_buffer_send_fills_exactly() {
        let mut txb = TxBuffer::new();
        // Fill to exactly MAX_SIZE.
        let avail = txb.avail();
        let sent = txb.send(&vec![0xab; avail]);
        assert_eq!(sent, avail);
        assert_eq!(txb.avail(), 0);
        // No more room.
        assert_eq!(txb.send(&[0x01]), 0);
    }

    fn make_send_stream(data: &[u8]) -> (SendStream, u64) {
        let len = to_u64(data.len());
        let mut s = SendStream::new(
            StreamId::new(100),
            0,
            connection_fc(len * 2),
            ConnectionEvents::default(),
        );
        s.set_max_stream_data(len * 2);
        (s, len)
    }

    #[test]
    fn bytes_written_data_recvd() {
        const DATA: &[u8] = b"hello world";
        let (mut s, len) = make_send_stream(DATA);
        s.send(DATA).unwrap();
        s.close();
        s.mark_as_sent(0, DATA.len(), true);
        s.mark_as_acked(0, DATA.len(), true);
        assert_eq!(s.bytes_written(), len);
    }

    #[test]
    fn bytes_written_reset_sent() {
        const DATA: &[u8] = b"hello";
        let (mut s, len) = make_send_stream(DATA);
        s.send(DATA).unwrap();
        s.reset(0);
        assert_eq!(s.bytes_written(), len);
    }

    /// `mark_acked` when newly acked range exactly extends a prior acked range.
    #[test]
    fn mark_acked_extends_exact_boundary() {
        let mut rt = RangeTracker::default();
        rt.mark_sent(0, 10);
        rt.mark_acked(0, 5); // Ack first half.
        rt.mark_acked(5, 5); // Ack exactly from where first ack ends.
        assert_eq!(rt.acked_from_zero(), 10);
    }

    #[test]
    fn send_stream_stats() {
        const MESSAGE: &[u8] = b"hello";
        let len_u64 = u64::try_from(MESSAGE.len()).unwrap();

        let conn_fc = connection_fc(len_u64);
        let conn_events = ConnectionEvents::default();

        let id = StreamId::new(100);
        let mut s = SendStream::new(id, 0, conn_fc, conn_events);
        s.set_max_stream_data(len_u64);

        // Initial stats should be all 0.
        check_stats(&s, 0, 0, 0);
        // Adter sending the data, bytes_written should be increased.
        _ = s.send(MESSAGE).unwrap();
        check_stats(&s, len_u64, 0, 0);

        // Adter calling mark_as_sent, bytes_sent should be increased.
        s.mark_as_sent(0, MESSAGE.len(), false);
        check_stats(&s, len_u64, len_u64, 0);

        s.close();
        s.mark_as_sent(len_u64, 0, true);

        // In the end, check bytes_acked.
        s.mark_as_acked(0, MESSAGE.len(), false);
        check_stats(&s, len_u64, len_u64, len_u64);

        s.mark_as_acked(len_u64, 0, true);
        assert!(s.is_ended());
    }

    fn stream_with_priority(tx: TransmissionPriority, rx: RetransmissionPriority) -> SendStream {
        let mut s = SendStream::new(
            StreamId::from(0),
            100,
            connection_fc(100),
            ConnectionEvents::default(),
        );
        s.set_priority(tx, rx);
        s
    }

    fn stream_frames_written(s: &mut SendStream, priority: TransmissionPriority) -> usize {
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_stream_frame(priority, &mut builder, &mut tokens, &mut stats);
        stats.stream
    }

    fn reset_frame_written(s: &mut SendStream, priority: TransmissionPriority) -> bool {
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_reset_frame(priority, &mut builder, &mut tokens, &mut stats);
        stats.reset_stream + stats.reset_stream_at == 1
    }

    #[test]
    fn set_priority_updates_effective_priority() {
        let mut s = stream_with_priority(
            TransmissionPriority::Low,
            RetransmissionPriority::MuchHigher,
        );
        s.send(&[0x42; 10]).unwrap();

        assert_eq!(stream_frames_written(&mut s, TransmissionPriority::Low), 1);
        s.mark_as_lost(0, 10, false);
        assert_eq!(
            stream_frames_written(&mut s, TransmissionPriority::Normal),
            0
        );
        assert_eq!(
            stream_frames_written(
                &mut s,
                TransmissionPriority::Low + RetransmissionPriority::MuchHigher,
            ),
            1,
        );
    }

    #[test]
    fn reset_lost_uses_effective_priority() {
        let mut s = stream_with_priority(
            TransmissionPriority::Normal,
            RetransmissionPriority::MuchHigher,
        );
        s.send(b"hello").unwrap();
        s.reset(0);

        assert!(reset_frame_written(&mut s, TransmissionPriority::Normal));
        s.reset_lost();
        assert!(!reset_frame_written(&mut s, TransmissionPriority::Normal));
        assert!(reset_frame_written(
            &mut s,
            TransmissionPriority::Normal + RetransmissionPriority::MuchHigher,
        ));
    }

    const ALL_PRIORITIES: [TransmissionPriority; 5] = [
        TransmissionPriority::Critical,
        TransmissionPriority::Important,
        TransmissionPriority::High,
        TransmissionPriority::Normal,
        TransmissionPriority::Low,
    ];

    fn assert_has_data_only_at(s: &mut SendStream, expected: &[TransmissionPriority]) {
        for &prio in &ALL_PRIORITIES {
            assert_eq!(
                s.has_data_at(prio),
                expected.contains(&prio),
                "has_data_at({prio:?})",
            );
        }
    }

    // An idle stream (no data, no pending frames) reports false at every priority.
    #[test]
    fn has_data_at_idle() {
        let mut s =
            stream_with_priority(TransmissionPriority::Normal, RetransmissionPriority::Higher);
        assert_has_data_only_at(&mut s, &[]);
    }

    // A stream with buffered data reports true only at its transmission priority.
    #[test]
    fn has_data_at_with_data() {
        let mut s =
            stream_with_priority(TransmissionPriority::Normal, RetransmissionPriority::Higher);
        s.send(b"hello").unwrap();
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Normal]);
    }

    // A stream with lost data reports true at effective_priority.
    // Lost bytes are also visible at the transmission priority because mark_as_lost
    // marks them as unsent in TxBuffer, making them discoverable by both paths.
    #[test]
    fn has_data_at_retransmission() {
        let mut s = stream_with_priority(
            TransmissionPriority::Low,
            RetransmissionPriority::MuchHigher,
        );
        // Low + MuchHigher = High
        let eff = TransmissionPriority::Low + RetransmissionPriority::MuchHigher;
        assert_eq!(eff, TransmissionPriority::High);

        s.send(&[0u8; 10]).unwrap();
        // Before sending: new data is only visible at the transmission priority.
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Low]);

        // After writing (mark_as_sent), all data is in-flight — nothing to write.
        assert_eq!(stream_frames_written(&mut s, TransmissionPriority::Low), 1);
        assert_has_data_only_at(&mut s, &[]);

        // After loss: bytes are back in the unsent range, visible at both priorities.
        s.mark_as_lost(0, 10, false);
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Low, eff]);
    }

    // A flow-control-blocked stream reports true at its transmission priority.
    #[test]
    fn has_data_at_blocked() {
        let conn_fc = connection_fc(100);
        let mut s = SendStream::new(
            StreamId::from(0),
            2, // stream FC limit: only 2 bytes
            Rc::clone(&conn_fc),
            ConnectionEvents::default(),
        );
        // Atomic write of 5 bytes exceeds the 2-byte credit → triggers blocking.
        assert_eq!(s.send_atomic(b"hello").unwrap(), 0);
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Normal]);
    }

    // A stream in ResetSent state reports true only at the stored reset priority.
    #[test]
    fn has_data_at_reset_pending() {
        let mut s =
            stream_with_priority(TransmissionPriority::Normal, RetransmissionPriority::Higher);
        s.send(b"hello").unwrap();
        s.reset(0); // ResetSent { priority: Some(Normal) }
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Normal]);
    }

    // A closed stream (DataSent) with a pending FIN reports true at its transmission priority.
    // Use RetransmissionPriority::Same so effective_priority == priority == Normal,
    // giving an unambiguous single-element expectation.
    #[test]
    fn has_data_at_data_sent_fin_pending() {
        let mut s =
            stream_with_priority(TransmissionPriority::Normal, RetransmissionPriority::Same);
        s.close(); // Ready → DataSent { fin_sent: false }
        assert_has_data_only_at(&mut s, &[TransmissionPriority::Normal]);
    }

    // After a reset frame is sent and then lost, the stream reports true at
    // effective_priority and false at transmission priority.
    #[test]
    fn has_data_at_reset_lost() {
        let mut s =
            stream_with_priority(TransmissionPriority::Normal, RetransmissionPriority::Higher);
        // Normal + Higher = High
        let eff = TransmissionPriority::Normal + RetransmissionPriority::Higher;
        assert_eq!(eff, TransmissionPriority::High);

        s.send(b"hello").unwrap();
        s.reset(0);
        // Send the reset frame (clears priority to None).
        assert!(reset_frame_written(&mut s, TransmissionPriority::Normal));
        // Reset is in flight: no frames pending.
        assert_has_data_only_at(&mut s, &[]);
        // Reset lost: priority set back to Some(effective_priority).
        s.reset_lost();
        assert_has_data_only_at(&mut s, &[eff]);
    }

    // --- RESET_STREAM_AT (reliable stream reset) ---

    const RR_STREAM: StreamId = StreamId::new(100);

    /// A `Send`-state stream with `data` buffered and a generous flow-control window, sharing the
    /// caller's `ConnectionEvents` so emitted events (e.g. `SendStreamComplete`) can be observed.
    fn reliable_stream(data: &[u8], events: ConnectionEvents) -> SendStream {
        let len = data.len() as u64;
        let mut s = SendStream::new(RR_STREAM, 0, connection_fc(len * 2), events);
        s.set_max_stream_data(len * 2);
        s.send(data).unwrap();
        s
    }

    /// A `Send`-state stream that buffers `prefix`, commits it (so the committed offset is
    /// `prefix.len()`), then buffers `rest`. Used to test a committed prefix smaller than the
    /// final size.
    fn reliable_stream_committed(
        prefix: &[u8],
        rest: &[u8],
        events: ConnectionEvents,
    ) -> SendStream {
        let total = (prefix.len() + rest.len()) as u64;
        let mut s = SendStream::new(RR_STREAM, 0, connection_fc(total * 2), events);
        s.set_max_stream_data(total * 2);
        s.send(prefix).unwrap();
        s.commit().unwrap();
        if !rest.is_empty() {
            s.send(rest).unwrap();
        }
        s
    }

    /// Write the pending reset frame at `Normal` priority and return the frame stats.
    fn send_reset_frame(s: &mut SendStream) -> FrameStats {
        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_reset_frame(
            TransmissionPriority::Normal,
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.reset_stream + stats.reset_stream_at, 1);
        stats
    }

    /// `commit` is a no-op in `Ready`, succeeds once data is buffered, and fails after reset.
    #[test]
    fn commit_validation() {
        let mut s = SendStream::new(
            RR_STREAM,
            1024,
            connection_fc(1024),
            ConnectionEvents::default(),
        );
        // In `Ready` there is nothing to commit, but it is not an error.
        assert!(matches!(s.state(), State::Ready { .. }));
        assert!(s.commit().is_ok());

        // In `Send`, commit captures the buffered data.
        s.send(&[0x42; 10]).unwrap();
        assert!(matches!(s.state(), State::Send { .. }));
        assert!(s.commit().is_ok());

        // After reset, commit fails with a stream-state error.
        s.reset(0);
        assert!(matches!(s.state(), State::ResetSentReliable { .. }));
        assert_eq!(s.commit().unwrap_err(), Error::StreamState);
    }

    /// `commit` after the stream has been fully received (`DataRecvd`) is a no-op.
    #[test]
    fn commit_after_received_is_noop() {
        let mut s = reliable_stream(&[0x42; 5], ConnectionEvents::default());
        s.close();
        s.mark_as_sent(0, 5, true);
        s.mark_as_acked(0, 5, true);
        assert!(matches!(s.state(), State::DataRecvd { .. }));
        assert!(s.commit().is_ok());
    }

    /// Without a commitment, a reset emits a plain `RESET_STREAM`.
    #[test]
    fn reset_without_commit_is_plain() {
        let mut s = reliable_stream(&[0x42; 10], ConnectionEvents::default());
        s.reset(0);
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 0,
                ..
            }
        ));
        let stats = send_reset_frame(&mut s);
        assert_eq!(stats.reset_stream, 1);
        assert_eq!(stats.reset_stream_at, 0);
    }

    /// A commitment with the peer's support and data still in flight enters `ResetSentReliable`
    /// and emits `RESET_STREAM_AT`.
    #[test]
    fn reset_with_commit_emits_reset_stream_at() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.reset(0);
        assert!(matches!(
            s.state(),
            State::ResetSentReliable {
                reliable_size: 5,
                ..
            }
        ));
        let stats = send_reset_frame(&mut s);
        assert_eq!(stats.reset_stream, 0);
        assert_eq!(stats.reset_stream_at, 1);
    }

    /// A `STOP_SENDING` drops any commitment, so the reset is a plain `RESET_STREAM` even though
    /// data was committed.
    #[test]
    fn stop_sending_drops_commitment_emits_reset_stream() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.drop_commitment();
        s.reset(0);
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 0,
                ..
            }
        ));
        let stats = send_reset_frame(&mut s);
        assert_eq!(stats.reset_stream, 1);
        assert_eq!(stats.reset_stream_at, 0);
    }

    /// A `STOP_SENDING` after a `RESET_STREAM_AT` was already sent drops the in-flight committed
    /// data (moving to `ResetSent`) but leaves `reliable_size` unchanged.
    #[test]
    fn stop_sending_after_reset_stream_at_drops_to_reset_sent() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        // Keep effective priority == Normal so the post-`reset_lost` retransmission is written at
        // the same priority as the initial send.
        s.set_priority(TransmissionPriority::Normal, RetransmissionPriority::Same);
        s.reset(0);
        s.mark_as_sent(0, 5, false);
        _ = send_reset_frame(&mut s);
        assert!(matches!(
            s.state(),
            State::ResetSentReliable {
                reliable_size: 5,
                reset_acked: false,
                ..
            }
        ));

        // STOP_SENDING: stop delivering the committed prefix, dropping to `ResetSent` with
        // `reliable_size` cleared to 0 (any frame retransmission is now a plain `RESET_STREAM`).
        s.drop_commitment();
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 0,
                ..
            }
        ));
        assert!(!s.is_ended());

        // A retransmission of the reset frame is now a plain RESET_STREAM.
        s.reset_lost();
        let stats = send_reset_frame(&mut s);
        assert_eq!(stats.reset_stream, 1);
        assert_eq!(stats.reset_stream_at, 0);

        // Once the frame is acked, the stream ends without waiting for the data.
        s.reset_acked();
        assert!(s.is_ended());
    }

    /// A `STOP_SENDING` after a `RESET_STREAM_AT` whose frame is already acked completes the reset
    /// immediately, abandoning the still-in-flight committed data.
    #[test]
    fn stop_sending_after_reset_stream_at_acked_completes() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.reset(0);
        s.mark_as_sent(0, 5, false);
        _ = send_reset_frame(&mut s);
        // Frame acked, committed data still in flight.
        s.reset_acked();
        assert!(matches!(s.state(), State::ResetSentReliable { .. }));
        assert!(!s.is_ended());

        s.drop_commitment();
        assert!(s.is_ended());
    }

    /// When all committed data is already acked at reset time, the buffer is dropped (the
    /// stream uses `ResetSent`) but `RESET_STREAM_AT` is still emitted.
    #[test]
    fn reset_with_commit_already_acked_drops_buffer() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.mark_as_sent(0, 10, false);
        s.mark_as_acked(0, 5, false); // retire up to the committed offset
        s.reset(0);
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 5,
                ..
            }
        ));
        let stats = send_reset_frame(&mut s);
        assert_eq!(stats.reset_stream_at, 1);
    }

    /// A reliable reset commits at most `final_size` and never emits a STREAM FIN, capping
    /// (re)transmission at the reliable offset.
    #[test]
    fn reliable_reset_caps_data_and_omits_fin() {
        let mut s = reliable_stream_committed(&[0x42; 4], &[0x42; 6], ConnectionEvents::default());
        s.reset(0);

        // The final size is reported accurately even while reliably resetting.
        assert_eq!(s.final_size(), Some(10));

        // Only `[0, 4)` is offered.
        let (offset, data) = s.next_bytes(false).expect("committed data");
        assert_eq!(offset, 0);
        assert_eq!(data.len(), 4);
        s.mark_as_sent(0, 4, false);
        assert!(!s.has_next_bytes(false));

        // A loss below the reliable offset is retransmitted; nothing above it ever is.
        s.mark_as_lost(0, 4, false);
        let (offset, data) = s.next_bytes(false).expect("retransmit committed data");
        assert_eq!(offset, 0);
        assert_eq!(data.len(), 4);
    }

    /// Even when the committed prefix equals the final size (so the written data offset reaches
    /// `final_size`), a reliable reset's STREAM frame carries no FIN.
    #[test]
    fn reliable_reset_omits_fin_at_final_size() {
        // Commit all 5 bytes, so reliable_size == final_size == 5.
        let mut s = reliable_stream_committed(&[0x42; 5], &[], ConnectionEvents::default());
        s.reset(0);
        assert_eq!(s.final_size(), Some(5));

        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        s.write_stream_frame(
            TransmissionPriority::Normal,
            &mut builder,
            &mut tokens,
            &mut stats,
        );
        assert_eq!(stats.stream, 1);
        assert_eq!(tokens.len(), 1);
        assert!(
            !as_stream_token(&tokens.remove(0)).fin,
            "reliable reset must not emit a FIN"
        );
    }

    /// On a retransmission, only lost data below the retransmission offset is offered; fresh
    /// data (still below `reliable_size`) is not pulled forward to the retransmission priority.
    #[test]
    fn reliable_reset_retransmission_respects_offset() {
        // Commit all 8 bytes, so `reliable_size == 8`.
        let mut s = reliable_stream_committed(&[0x42; 8], &[], ConnectionEvents::default());
        s.reset(0);

        // Send `[0, 4)`; `[4, 8)` remains unsent. Then lose `[0, 2)` (retransmission offset = 2).
        s.mark_as_sent(0, 4, false);
        s.mark_as_lost(0, 2, false);

        // Retransmission only offers the lost `[0, 2)`.
        assert!(s.has_next_bytes(true));
        let (offset, data) = s.next_bytes(true).expect("retransmit lost data");
        assert_eq!(offset, 0);
        assert_eq!(data.len(), 2);
        s.mark_as_sent(0, 2, false);

        // Nothing more to retransmit: fresh `[4, 8)` is above the retransmission offset.
        assert!(!s.has_next_bytes(true));
        assert!(s.next_bytes(true).is_none());

        // But it is still available as a fresh send (below `reliable_size`).
        assert!(s.has_next_bytes(false));
        let (offset, data) = s.next_bytes(false).expect("fresh committed data");
        assert_eq!(offset, 4);
        assert_eq!(data.len(), 4);
    }

    /// Data-then-frame ack order reaches `ResetRecvd` without firing `SendStreamComplete`.
    #[test]
    fn reliable_reset_completion_data_then_frame() {
        let events = ConnectionEvents::default();
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], events);
        s.reset(0);
        s.mark_as_sent(0, 5, false);
        _ = send_reset_frame(&mut s);
        assert!(matches!(
            s.state(),
            State::ResetSentReliable {
                reliable_size: 5,
                reset_acked: false,
                ..
            }
        ));

        // Ack the committed data first: collapses to ResetSent, not yet ended.
        s.mark_as_acked(0, 5, false);
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 5,
                ..
            }
        ));
        assert!(!s.is_ended());

        s.reset_acked();
        assert!(s.is_ended());
    }

    /// Frame-then-data ack order reaches `ResetRecvd` without firing `SendStreamComplete`.
    #[test]
    fn reliable_reset_completion_frame_then_data() {
        let events = ConnectionEvents::default();
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], events);
        s.reset(0);
        s.mark_as_sent(0, 5, false);
        _ = send_reset_frame(&mut s);

        // Ack the frame first: stays in ResetSentReliable awaiting the data.
        s.reset_acked();
        assert!(matches!(s.state(), State::ResetSentReliable { .. }));
        assert!(!s.is_ended());

        s.mark_as_acked(0, 5, false);
        assert!(s.is_ended());
    }

    /// When the committed data is already acked at reset time, the eventual frame ack reaches
    /// `ResetRecvd` without firing `SendStreamComplete`.
    #[test]
    fn reliable_reset_completion_preacked() {
        let events = ConnectionEvents::default();
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], events);
        s.mark_as_sent(0, 10, false);
        s.mark_as_acked(0, 5, false);
        s.reset(0);
        _ = send_reset_frame(&mut s);
        assert!(matches!(
            s.state(),
            State::ResetSent {
                reliable_size: 5,
                ..
            }
        ));

        s.reset_acked();
        assert!(s.is_ended());
    }

    /// Neither a plain reset nor a reliable reset fires `SendStreamComplete`.
    #[test]
    fn reset_does_not_complete() {
        let mut events = ConnectionEvents::default();
        let mut s = reliable_stream(&[0x42; 10], events.clone());
        s.reset(0);
        _ = send_reset_frame(&mut s);
        s.reset_acked();
        assert!(s.is_ended());

        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], events.clone());
        s.mark_as_sent(0, 10, false);
        s.mark_as_acked(0, 5, false);
        s.reset(0);
        _ = send_reset_frame(&mut s);
        s.reset_acked();
        assert!(s.is_ended());

        let completions = events
            .events()
            .filter(|e| matches!(e, ConnectionEvent::SendStreamComplete { .. }))
            .count();
        assert_eq!(completions, 0);
    }

    /// A lost reliable reset frame is re-armed for retransmission.
    #[test]
    fn reliable_reset_frame_lost_rearms() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.reset(0);
        assert!(reset_frame_written(&mut s, TransmissionPriority::Normal));
        // In flight: nothing pending at Normal.
        assert!(!reset_frame_written(&mut s, TransmissionPriority::Normal));
        // Lost: re-armed at effective priority (Normal + default retransmission).
        s.reset_lost();
        let eff = TransmissionPriority::Normal + RetransmissionPriority::default();
        assert!(reset_frame_written(&mut s, eff));
    }

    /// A reliably-reset stream with committed data still in flight has both a `RESET_STREAM_AT`
    /// frame and the committed STREAM data pending at the same priority. A single `write_frames`
    /// call into a packet with ample room should emit both, so the reset and the data it protects
    /// travel together in one packet.
    #[test]
    fn reset_and_committed_data_are_coalesced() {
        let mut s = reliable_stream_committed(&[0x42; 5], &[0x42; 5], ConnectionEvents::default());
        s.reset(0);
        assert!(matches!(
            s.state(),
            State::ResetSentReliable {
                reliable_size: 5,
                ..
            }
        ));
        assert!(s.has_data_at(TransmissionPriority::Normal));

        let mut builder =
            packet::Builder::short(Encoder::default(), false, None::<&[u8]>, packet::LIMIT);
        let mut tokens = recovery::Tokens::new();
        let mut stats = FrameStats::default();
        assert!(s.write_frames(
            TransmissionPriority::Normal,
            &mut builder,
            &mut tokens,
            &mut stats
        ));
        assert!(!builder.is_full());

        // Both frames should be in this one packet.
        assert_eq!(stats.reset_stream_at, 1);
        assert_eq!(stats.stream, 1);
    }
}
