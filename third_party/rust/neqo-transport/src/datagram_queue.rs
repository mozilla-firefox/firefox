// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! Per-session outgoing-datagram scheduling: byte budget, max-buffered
//! backpressure, send-group/send-order priority, and max-age expiry.
//!
//! [`DatagramQueue`] holds one session's outgoing datagrams, ordered by
//! send-group/send-order priority and bounded by both a byte budget and
//! `outgoingMaxAge`, so a burst is delivered in priority order and anything
//! left stale is shed instead of sent late. [`crate::quic_datagrams::QuicDatagrams`] holds
//! one `DatagramQueue` per session (keyed by an opaque handle) and
//! round-robins between them at packet-build time, so a datagram is never
//! buffered anywhere without this age policy applying to it.

use std::{
    collections::{BTreeMap, VecDeque},
    num::NonZeroUsize,
    time::{Duration, Instant},
};

use neqo_common::to_u64;
use tracing::{debug, trace};

use crate::{
    rtt::{DEFAULT_INITIAL_RTT, GRANULARITY},
    streams::{SendGroupId, SendOrder},
};

/// Byte-budget memory backstop for the outgoing datagram queue.
///
/// A datagram *count* bound does not bound memory - a flood of tiny
/// datagrams would fit comfortably under a count sized for average-sized
/// ones, while a fixed count sized for a burst of large datagrams standing
/// at a much smaller size is needless bufferbloat. A byte budget scales with
/// what is actually queued either way, and is a backstop only:
/// `outgoingMaxAge` is what actually bounds delay in the common case, by
/// shedding stale datagrams before this budget is ever reached.
///
/// 256KB is one [`DEFAULT_MAX_AGE_FLOOR`] (20ms) window at 100 Mbps, so
/// anything deeper would expire before it could plausibly be sent; it is also
/// ~207 datagrams at a 1200B MTU-sized payload once [`PER_DATAGRAM_OVERHEAD`]
/// is charged, comfortably above a 64-datagram GSO-batch floor a single
/// `sendmsg` call can amortize.
///
/// Per session: nothing here caps the sum across a connection's sessions,
/// so a peer opening many WebTransport sessions multiplies this backstop.
/// A connection-wide aggregate cap, if ever needed, belongs in
/// [`crate::quic_datagrams::QuicDatagrams`], not here.
const DEFAULT_MAX_QUEUED_BYTES: usize = 256 * 1024;

/// Conservative per-datagram bookkeeping overhead charged in addition to
/// payload bytes when accounting against a queue's byte budget
/// ([`DatagramQueueCapacity::max_queued_bytes`], settable in tests via
/// `DatagramQueue::set_max_queued_bytes`), so a flood of tiny datagrams is
/// bounded by the same budget as large ones instead of needing a separate
/// count cap. Approximates the queue's own per-entry cost (the
/// [`QueuedDatagram`] struct plus its slot in the group's
/// `VecDeque`/`BTreeMap`), not wire overhead.
const PER_DATAGRAM_OVERHEAD: usize = 64;

/// The byte charge against a queue's byte budget
/// ([`DatagramQueueCapacity::max_queued_bytes`]) for a datagram whose
/// allocated capacity is `allocated` bytes: the allocation itself plus
/// [`PER_DATAGRAM_OVERHEAD`].
const fn charge(allocated: usize) -> usize {
    allocated + PER_DATAGRAM_OVERHEAD
}

/// Numerator/denominator of the multiplier applied to the path's minimum RTT
/// by [`default_max_age`]. Kept as an integer ratio rather than a float so the
/// `Duration` arithmetic stays exact.
const DEFAULT_MAX_AGE_RTT_MULTIPLIER_NUM: u32 = 5;
const DEFAULT_MAX_AGE_RTT_MULTIPLIER_DEN: u32 = 4;

/// Lower bound for [`default_max_age`]. Rooted in app performance targets
/// rather than RTT: roughly one frame at 60fps, the pace a responsive site is
/// already expected to service input at, so a low-RTT path (loopback, LAN)
/// does not expire datagrams faster than that.
const DEFAULT_MAX_AGE_FLOOR: Duration = Duration::from_millis(20);

/// The "[implementation-defined] value" that WebTransport's `sendDatagrams`
/// step 4 substitutes when the application leaves `outgoingMaxAge` unset.
///
/// This is a latency bound, not a delivery guarantee. A datagram still queued
/// after a couple of round trips has been overtaken by whatever the
/// application sent after it, and on a link too slow to drain the queue,
/// holding it only adds delay to everything behind it. Datagrams are
/// best-effort, so the queue sheds it rather than growing without bound.
///
/// The 1.25x multiplier matches Chromium's QUICHE
/// `QuicDatagramQueue::GetMaxTimeInQueue()`. The floor deliberately does not
/// match Chromium's 4ms: a fixed few milliseconds is too tight for an
/// intercontinental or satellite path to plausibly drain a burst - a video
/// key frame, say - before it is shed, so the floor instead comes from how
/// fast a site is expected to service input (one frame at 60fps), and RTT
/// scaling takes over above that on higher-latency paths.
///
/// [implementation-defined]: https://infra.spec.whatwg.org/#implementation-defined
#[must_use]
pub fn default_max_age(min_rtt: Duration) -> Duration {
    // `Connection::min_rtt` seeds its estimate with the initial RTT, so a
    // zero here can only come from a zero-configured initial RTT.  Scaling
    // that would collapse the bound onto the floor, so stand in the default
    // estimate rather than treat the path as instantaneous.
    let rtt = if min_rtt.is_zero() {
        DEFAULT_INITIAL_RTT
    } else {
        min_rtt
    };
    rtt.checked_mul(DEFAULT_MAX_AGE_RTT_MULTIPLIER_NUM)
        .map_or(Duration::MAX, |d| d / DEFAULT_MAX_AGE_RTT_MULTIPLIER_DEN)
        .max(DEFAULT_MAX_AGE_FLOOR)
}

/// The resume charge for an outcome that admitted the datagram: any freed
/// byte is progress for the next write, not a specific charge to wait for.
const ANY_PROGRESS: usize = 1;

/// Floor applied to an explicit `outgoingMaxAge`. Below this, the value stops
/// bounding anything meaningful relative to our own timer granularity. There
/// is deliberately no ceiling: an application that asks for a longer buffer
/// than [`default_max_age`] gets it, bounded only by the byte budget.
const EXPLICIT_MAX_AGE_FLOOR: Duration = GRANULARITY;

/// Caller-supplied identifier used to report the fate of a tracked datagram.
pub type DatagramId = u64;

/// The state of the queue after accepting a datagram, which is what the
/// application needs in order to apply backpressure.
#[must_use]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DatagramQueueOutcome {
    /// The queue was below the max-buffered limit.
    Ok,
    /// The queue had space, but it was at or above the max-buffered limit.
    MaxBufferedReached,
    /// The incoming datagram was itself the lowest-priority thing that
    /// would exist in the queue, so it was refused outright rather than
    /// evicting something that outranks it. Nothing else was disturbed.
    Rejected,
    /// The incoming datagram was accepted, but the byte budget was
    /// exceeded, so one or more of the lowest-priority datagrams already
    /// queued were evicted to make room - a single incoming datagram can be
    /// larger than what any one eviction frees. `dropped` is the total
    /// number evicted, tracked or not, for aggregate stats.
    Overflowed { dropped: usize },
}

/// What removing a session's queue took with it, for the caller to report.
#[derive(Debug, Default, PartialEq, Eq)]
#[must_use]
pub struct DroppedDatagrams {
    /// Datagrams still queued, now discarded unsent.
    pub queued: u64,
    /// Sent datagrams not yet returned by `take_sent_count`.
    pub sent: u64,
    /// Expired datagrams not yet returned by `take_expired_count`.
    pub expired: u64,
    /// Datagrams too big for the path MTU not yet returned by
    /// `take_too_big_count`.
    pub too_big: u64,
}

/// A snapshot of this session's outgoing-datagram queue state, e.g. for
/// sizing the send credit a caller grants its own producer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[non_exhaustive]
pub struct DatagramQueueCapacity {
    /// Bytes free before the queue's byte budget (`max_queued_bytes` below) is
    /// reached. Charged bytes, not payload bytes: each datagram costs the
    /// capacity of the `Vec` handed to `DatagramQueue::enqueue` plus a fixed
    /// per-entry overhead, so this is an upper bound on the payload a grant may
    /// cover, not an exact allowance - an over-reserved buffer costs more than
    /// the payload it carries.
    pub remaining_bytes: usize,
    /// Datagrams currently queued here, awaiting a drain.
    pub queued_datagrams: usize,
    /// The queue's byte budget itself, i.e. `remaining_bytes` when empty.
    /// Exposed so a caller deriving a windowed credit grant from
    /// `remaining_bytes` has the window size to dedupe updates against.
    pub max_queued_bytes: usize,
    /// The fixed overhead each datagram is charged on top of its allocated
    /// capacity, for a caller that charges its own send credit the same way.
    pub per_datagram_overhead: usize,
}

#[derive(Debug)]
pub struct QueuedDatagram {
    pub data: Vec<u8>,
    /// Caller-supplied tracking ID, or `None` if the caller sent the datagram
    /// untracked.
    pub id: Option<DatagramId>,
    pub timestamp: Instant,
}

impl QueuedDatagram {
    #[must_use]
    pub const fn new(data: Vec<u8>, id: Option<DatagramId>, now: Instant) -> Self {
        Self {
            data,
            id,
            timestamp: now,
        }
    }

    #[must_use]
    pub fn age(&self, now: Instant) -> Duration {
        now.saturating_duration_since(self.timestamp)
    }
}

/// Per-send-group priority queue.
///
/// Datagrams are organized by `send_order`; higher order = higher priority.
/// Within the same `send_order`, datagrams are served FIFO (insertion order).
#[derive(Debug, Default)]
struct GroupQueue {
    /// Keyed by `send_order` (ascending). Higher key = higher priority. A
    /// bucket is removed as soon as it becomes empty, so `by_order.is_empty()`
    /// is exactly "this group holds no datagrams" - no separate count needed.
    /// See [`DatagramQueue::groups`] for what that eager pruning costs.
    by_order: BTreeMap<SendOrder, VecDeque<QueuedDatagram>>,
}

impl GroupQueue {
    fn is_empty(&self) -> bool {
        self.by_order.is_empty()
    }

    fn push(&mut self, send_order: SendOrder, dgram: QueuedDatagram) {
        // `now` comes from the embedding application, which neqo trusts to
        // be monotonic everywhere else too.  In release builds a backwards
        // step makes `expire_old`'s prefix cut miss or over-shed within this
        // bucket; nothing else breaks.
        debug_assert!(
            self.by_order
                .get(&send_order)
                .and_then(VecDeque::back)
                .is_none_or(|last| dgram.timestamp >= last.timestamp),
            "expire_old's partition_point needs non-decreasing timestamps per bucket"
        );
        self.by_order
            .entry(send_order)
            .or_default()
            .push_back(dgram);
    }

    fn pop_front(&mut self, order: SendOrder) -> Option<QueuedDatagram> {
        let queue = self.by_order.get_mut(&order)?;
        let dgram = queue.pop_front()?;
        if queue.is_empty() {
            self.by_order.remove(&order);
        }
        Some(dgram)
    }

    /// The lowest `send_order` present in this group (i.e. lowest-priority bucket).
    fn lowest_order(&self) -> Option<SendOrder> {
        self.by_order.keys().next().copied()
    }

    /// Expire every datagram that has reached `max_age`: the boundary is
    /// inclusive, to agree with the deadline [`DatagramQueue::next_expiry`]
    /// hands the timer, so a caller that wakes at exactly
    /// `timestamp + max_age` has to find something to shed, or it
    /// reschedules the same instant forever.  The WebTransport spec's
    /// `sendDatagrams` step reads "If more than duration milliseconds have
    /// passed since timestamp"; departing from that by one instant is
    /// deliberate, for the reason above.
    ///
    /// Returns `(expired_count, bytes_freed)` rather than the expired
    /// datagrams themselves: the caller only ever needs their number and
    /// charged size, so there is no reason to keep their payload data alive
    /// past this call.
    fn expire_old(&mut self, now: Instant, max_age: Duration) -> (usize, usize) {
        let mut expired = 0;
        let mut freed = 0;
        self.by_order.retain(|_, queue| {
            // Ages are non-increasing front-to-back (datagrams are pushed in
            // non-decreasing timestamp order), so the expired ones are
            // exactly a prefix: a binary search for its end plus one bulk
            // removal, rather than one age comparison and pop per datagram.
            let cut = queue.partition_point(|d| d.age(now) >= max_age);
            expired += cut;
            for dgram in queue.drain(..cut) {
                freed += charge(dgram.data.capacity());
            }
            !queue.is_empty()
        });
        (expired, freed)
    }

    /// Remove and return the datagram in the highest-priority bucket (i.e.
    /// the next to send), along with its `send_order`.
    fn pop_highest(&mut self) -> Option<(SendOrder, QueuedDatagram)> {
        let mut entry = self.by_order.last_entry()?;
        let order = *entry.key();
        let dgram = entry.get_mut().pop_front()?;
        if entry.get().is_empty() {
            entry.remove();
        }
        Some((order, dgram))
    }

    /// The timestamp of the longest-queued datagram in this group, across
    /// all `send_order` buckets. Each bucket is FIFO, so its front is its
    /// oldest.
    fn oldest_timestamp(&self) -> Option<Instant> {
        self.by_order
            .values()
            .filter_map(|q| q.front())
            .map(|d| d.timestamp)
            .min()
    }
}

/// Per-session outgoing datagram queue with send-group round-robin, within-group
/// send-order priority, max-buffered limit, and max-age support.
///
/// ## Scheduling
///
/// Datagrams are enqueued with a `send_group_id` and a `send_order`:
///
/// * **Between groups** — groups get an equal number of turns via round-robin: each round takes at
///   most one datagram from every non-empty group, in ascending group-ID order. Analogous to the
///   fair-share stream send scheduler ([`crate::send_stream::SendStreams`]) used for QUIC streams.
///   Turns, not bytes: a group sending MTU-sized datagrams gets more bandwidth than one sending
///   tiny ones at the same rate. Equalizing bytes would need a deficit round-robin, carrying each
///   group's unspent byte credit across calls; datagrams within a session are typically uniform
///   enough that this has not been worth the per-group state.
/// * **Within a group** — the datagram with the highest `send_order` is always sent first.
///   Equal-order datagrams are served FIFO.
///
/// `QuicDatagrams` round-robins *between* sessions at packet-build time
/// the same way this type round-robins between groups *within* a session; see its own doc comment.
///
/// This queue is scheduled independently of [`crate::send_stream::SendStreams`], the stream
/// scheduler. A [`crate::streams::SendGroupId`] shared between a `WebTransportSendStream` and a
/// datagram writable does not get cross-type starvation avoidance, as the WebTransport spec's
/// send-order rules for a shared `SendGroupId` require: each scheduler is fair only among its own
/// kind.
///
/// `send_order` here is a bare [`SendOrder`]: the spec's `sendOrder` on a datagram writable is a
/// non-nullable `long long` defaulting to 0, so there is no null order to model.
/// [`crate::send_stream::SendStreams`] still carries an `Option<SendOrder>` for the older
/// stream-level null semantics; that is a stream concern, not a gap here.
#[derive(Debug)]
pub struct DatagramQueue {
    /// Send groups, keyed by [`SendGroupId`]. [`NULL_GROUP_ID`](crate::streams::NULL_GROUP_ID)
    /// is the sentinel for the null sendGroup (datagrams with no group assigned).
    ///
    /// Ordered by group ID so that round-robin is deterministic. A group is
    /// removed as soon as it becomes empty, so every entry here is non-empty,
    /// which [`Self::is_empty`], [`Self::next_group_id`] and
    /// [`Self::lowest_priority_key`] all lean on.  The price is allocator
    /// traffic: in the common cycle where one datagram arrives and is sent
    /// before the next, each enqueue allocates a group node, a bucket node
    /// and a `VecDeque` buffer, and the send frees all three.  Keeping the
    /// last group and bucket alive would save that at the cost of empty
    /// entries every scan has to skip; not worth it until measured.
    groups: BTreeMap<SendGroupId, GroupQueue>,
    /// Group ID at which the next round-robin round starts. Persisted across
    /// calls so a drain that stops on its budget resumes at the group after
    /// the last one served; restarting at the lowest group ID every time
    /// would starve higher-numbered groups whenever the budget is smaller
    /// than the number of groups.
    ///
    /// Initialized and reset to `SendGroupId::new(0)`: a cursor origin, not
    /// [`NULL_GROUP_ID`](crate::streams::NULL_GROUP_ID) - the two share a
    /// value but mean different things.
    rr_next: SendGroupId,
    /// Total datagram count across all groups. Cached rather than summed
    /// from `groups` on read: [`Self::below_max_buffered`] needs the actual
    /// value, not just emptiness, and runs on every [`Self::enqueue`] call.
    total_count: usize,
    /// Total charged bytes across all groups: the sum of each queued
    /// datagram's allocated capacity plus [`PER_DATAGRAM_OVERHEAD`].
    total_bytes: usize,
    max_queued_bytes: usize,
    max_buffered_datagrams: Option<NonZeroUsize>,
    /// `Some(charge)` once [`Self::enqueue`] returns anything but
    /// [`DatagramQueueOutcome::Ok`], where `charge` is the space a resume
    /// signal has to wait for: `1` for [`DatagramQueueOutcome::MaxBufferedReached`]
    /// and [`DatagramQueueOutcome::Overflowed`], which admit the datagram, so
    /// any freed byte is progress; the refused datagram's actual charge for
    /// [`DatagramQueueOutcome::Rejected`], so a caller retrying that exact
    /// write is not told to resume before the space it needs has actually
    /// freed up. Back to `None` once [`Self::resume_if_unblocked`] reports the
    /// resume signal as due.
    blocked: Option<usize>,
    /// The application's `outgoingMaxAge`, or `None` if it never set one, in
    /// which case [`default_max_age`] applies. Which of the two is in force is
    /// not observable from script: the attribute reports the application's
    /// value, so it stays null.
    max_age: Option<Duration>,
    /// Datagrams shed by [`Self::expire`] since the last
    /// [`Self::take_expired_count`]. Kept here rather than handed back to
    /// whichever caller ran the expiry: the connection's timer sweep and a
    /// caller's own per-session sweep can both shed datagrams, in either
    /// order, and whoever drains this next counts every one of them exactly
    /// once.
    expired: u64,
    /// Datagrams written into a packet, not yet returned by
    /// [`Self::take_sent_count`].
    sent: u64,
    /// Datagrams dropped at packet-build time for not fitting the path MTU,
    /// not yet returned by [`Self::take_too_big_count`].  Enqueueing only
    /// checks the peer's limit, which can exceed the path MTU.
    too_big: u64,
}

impl DatagramQueue {
    /// Whether the total queued count is below the max-buffered limit, i.e.
    /// [`Self::enqueue`] would currently report [`DatagramQueueOutcome::Ok`]
    /// rather than [`DatagramQueueOutcome::MaxBufferedReached`].
    fn below_max_buffered(&self) -> bool {
        self.max_buffered_datagrams
            .is_none_or(|mark| self.total_count < mark.get())
    }

    /// The `max_age` actually in force: the application's explicit
    /// `outgoingMaxAge` if it set one, else `default_max_age`.
    fn effective_max_age(&self, default_max_age: Duration) -> Duration {
        self.max_age.unwrap_or(default_max_age)
    }

    /// Count a datagram from [`Self::take_next`] as written into a packet.
    pub const fn record_sent(&mut self) {
        self.sent += 1;
    }

    /// Return and reset the count of datagrams sent since the last call.
    pub fn take_sent_count(&mut self) -> u64 {
        std::mem::take(&mut self.sent)
    }

    /// Count a datagram from [`Self::take_next`] as dropped for not fitting
    /// the path MTU.
    pub const fn record_too_big(&mut self) {
        self.too_big += 1;
    }

    /// Return and reset the count of datagrams dropped as too big since the
    /// last call.
    pub fn take_too_big_count(&mut self) -> u64 {
        std::mem::take(&mut self.too_big)
    }

    /// `None` disables the count-based mark entirely, leaving only the byte
    /// budget. It is *not* the mapping for `outgoingMaxBufferedDatagrams == 0`,
    /// which means "always above the mark" - a caller converting the `WebIDL`
    /// attribute with `NonZeroUsize::new` inverts that case. Clamp `0` to
    /// `NonZeroUsize::MIN` at the boundary instead.
    ///
    /// The mark is compared against the whole queue's count, i.e. every
    /// datagram queued for this session by any writable. The spec applies
    /// `outgoingMaxBufferedDatagrams` per `WebTransportDatagramsWritable`, so
    /// a caller with several writables on one session has to scale the value
    /// it sets here, or leave the mark `None` and gate per writable itself.
    pub fn set_max_buffered_datagrams(&mut self, mark: Option<NonZeroUsize>) {
        trace!("Setting max-buffered limit to {mark:?}");
        self.max_buffered_datagrams = mark;
    }

    /// Set the byte budget enforced by [`Self::enqueue`]'s eviction and
    /// [`Self::capacity`]'s snapshot. Does not retroactively evict anything
    /// already queued; a lowered budget only takes effect on the next
    /// [`Self::enqueue`]. Test-only: nothing in production tunes the budget
    /// away from [`DEFAULT_MAX_QUEUED_BYTES`].
    #[cfg(test)]
    pub fn set_max_queued_bytes(&mut self, bytes: usize) {
        trace!("Setting max queued bytes to {bytes}");
        self.max_queued_bytes = bytes;
    }

    /// `None` means the application has not set `outgoingMaxAge`, in which case
    /// [`default_max_age`] applies. The distinction is not observable from
    /// script: the attribute reports the application's value, which stays null.
    ///
    /// An explicit value is floored at 1ms, a timer-granularity bound, and
    /// otherwise used as given: there is no ceiling, so an app that asks for
    /// more buffering than [`default_max_age`] gets it, bounded only by the
    /// byte budget.
    ///
    /// Already-queued datagrams the new limit puts past their age are shed on
    /// the spot and counted toward [`Self::take_expired_count`] like any
    /// other expiry.
    pub fn set_max_age(
        &mut self,
        max_age: Option<Duration>,
        now: Instant,
        default_max_age: Duration,
    ) {
        let clamped = max_age.map(|v| v.max(EXPLICIT_MAX_AGE_FLOOR));
        trace!("Setting max age to {max_age:?} (clamped: {clamped:?})");
        self.max_age = clamped;
        _ = self.expire(now, default_max_age);
    }

    /// Remove every datagram that has reached `max_age`; the boundary is
    /// inclusive. Returns how many were removed by this call; the same
    /// number is also added to the running count [`Self::take_expired_count`]
    /// drains.
    pub fn expire(&mut self, now: Instant, default_max_age: Duration) -> usize {
        let max_age = self.effective_max_age(default_max_age);
        let mut expired = 0;
        let mut freed_bytes = 0;
        self.groups.retain(|_, group| {
            let (count, freed) = group.expire_old(now, max_age);
            expired += count;
            freed_bytes += freed;
            !group.is_empty()
        });
        self.total_count -= expired;
        self.total_bytes -= freed_bytes;
        self.expired += to_u64(expired);
        expired
    }

    /// Return and reset the number of datagrams expired since the last call,
    /// by whichever caller ran [`Self::expire`].
    pub fn take_expired_count(&mut self) -> u64 {
        std::mem::take(&mut self.expired)
    }

    /// The `(send_order, group_id)` key of the globally lowest-priority
    /// occupied bucket: lowest `send_order` across all groups, ties broken by
    /// `group_id` (lowest first) for determinism.
    /// [`NULL_GROUP_ID`](crate::streams::NULL_GROUP_ID) therefore sorts first
    /// at equal `send_order`, so null-group datagrams are systematically the
    /// first eviction victims in a tie.
    ///
    /// `send_order` is only defined *within* a group (the WebTransport spec:
    /// "Each `WebTransportSendGroup` also establishes a separate numberspace
    /// for evaluating sendOrder numbers"), so ranking it across groups is a
    /// deliberate simplification: something has to give at the byte budget,
    /// which the spec never reaches since it only sheds by age, and the
    /// application's own ordering is a better signal than group identity.
    /// Picking the victim group first (by occupancy or
    /// age) and only then its lowest order would stop one group's backlog
    /// from being paid for by another, at the cost of evicting traffic the
    /// application marked as more important.
    fn lowest_priority_key(&self) -> Option<(SendOrder, SendGroupId)> {
        // Empty groups are removed eagerly, so `lowest_order` is always `Some`.
        self.groups
            .iter()
            .filter_map(|(gid, g)| Some((g.lowest_order()?, *gid)))
            .min()
    }

    /// Evict the oldest datagram at `group_id`'s `order` bucket specifically,
    /// without re-deriving the global victim. `None` once that bucket - and,
    /// if it was the group's last, the group itself - is exhausted: a caller
    /// looping this to drain a same-priority burst (e.g. one video frame's
    /// datagrams) in one group can tell "nothing left here" from that alone,
    /// and re-derive the next victim via [`Self::lowest_priority_key`] only
    /// then, not once per evicted datagram.
    fn evict_at(&mut self, group_id: SendGroupId, order: SendOrder) -> Option<QueuedDatagram> {
        let (dgram, group_empty) = {
            let group = self.groups.get_mut(&group_id)?;
            let dgram = group.pop_front(order)?;
            (dgram, group.is_empty())
        };
        debug!(
            "Queue at byte budget ({}/{}), dropping datagram {:?} from group {group_id:?}",
            self.total_bytes, self.max_queued_bytes, dgram.id,
        );
        Some(self.finish_removal(group_id, group_empty, dgram))
    }

    /// Returns the outcome for the caller to apply backpressure with.
    ///
    /// Does *not* run expiry, like [`Self::peek_next_len`]: the caller has to
    /// have expired stale entries for `now` first, or a fresh write can be
    /// rejected - or evict a live datagram - to make room for entries that
    /// are already past `max_age`.
    pub fn enqueue(
        &mut self,
        data: Vec<u8>,
        id: Option<DatagramId>,
        now: Instant,
        send_group_id: SendGroupId,
        send_order: SendOrder,
    ) -> DatagramQueueOutcome {
        // Charge the whole allocation, not just the payload: a caller that
        // hands over an over-reserved buffer would otherwise let the queue
        // hold more real memory than `max_queued_bytes` accounts for.
        let new_charge = charge(data.capacity());
        // A single incoming datagram can be larger than what any one eviction
        // frees, so evict until there is room rather than at most once. If
        // eviction empties the queue and the datagram alone still exceeds the
        // budget, let it in anyway: refusing it would need a new error path,
        // and the next enqueue evicts it immediately in turn.
        let mut evicted_count: usize = 0;
        let mut outranked = false;
        'evict: while self.total_bytes + new_charge > self.max_queued_bytes {
            // Re-derived only once per victim bucket, not once per evicted
            // datagram: draining the bucket that lost to the newcomer can
            // leave a next-lowest that outranks it, and then admitting the
            // newcomer over budget beats evicting better traffic.
            let Some((victim_order, victim_group)) = self.lowest_priority_key() else {
                break;
            };
            // Compares `send_order` alone, not the full `(send_order,
            // send_group_id)` tuple `lowest_priority_key` returns: that tuple
            // breaks *victim-selection* ties by group ID for determinism, but
            // reusing it here would make group ID decide *admission* too -
            // under sustained equal-order pressure the lowest-numbered group
            // would always lose and never get to evict anyone.
            if send_order < victim_order {
                outranked = true;
                break;
            }
            // Drain this one bucket without re-deriving the victim per item:
            // a burst of same-priority datagrams (e.g. one video frame) is
            // common, and nothing about global priority changes until this
            // specific bucket runs out.
            while self.evict_at(victim_group, victim_order).is_some() {
                evicted_count += 1;
                if self.total_bytes + new_charge <= self.max_queued_bytes {
                    break 'evict;
                }
            }
        }
        if outranked && evicted_count == 0 {
            debug!(
                "Queue at byte budget ({}/{}), dropping incoming datagram {id:?} \
                 (group={send_group_id:?}, order={send_order}): lower priority than everything queued",
                self.total_bytes, self.max_queued_bytes
            );
            // A full queue is backpressure whatever the max-buffered limit says,
            // and the caller is told to wait for a resume signal on every
            // outcome but `Ok`: arm one, or it waits forever. `new_charge`
            // is the exact charge *this* write needs before a retry can
            // succeed.
            self.block_on(new_charge);
            return DatagramQueueOutcome::Rejected;
        }

        self.groups
            .entry(send_group_id)
            .or_default()
            .push(send_order, QueuedDatagram::new(data, id, now));
        self.total_count += 1;
        self.total_bytes += new_charge;

        // An overflowing queue is full, so backpressure applies regardless of where
        // the max-buffered limit sits.
        let outcome = if evicted_count > 0 {
            self.block_on(ANY_PROGRESS);
            DatagramQueueOutcome::Overflowed {
                dropped: evicted_count,
            }
        } else if self.below_max_buffered() && self.total_bytes <= self.max_queued_bytes {
            DatagramQueueOutcome::Ok
        } else {
            self.block_on(ANY_PROGRESS);
            DatagramQueueOutcome::MaxBufferedReached
        };
        trace!(
            "Enqueued datagram {id:?} (group={send_group_id:?}, order={send_order}), \
             total={} ({} bytes), outcome: {outcome:?}",
            self.total_count, self.total_bytes,
        );

        outcome
    }

    /// The scheduling order's next group, starting at `rr_next` and wrapping.
    /// `None` if the queue is empty.
    fn next_group_id(&self) -> Option<SendGroupId> {
        self.groups
            .range(self.rr_next..)
            .next()
            .or_else(|| self.groups.iter().next())
            .map(|(id, _)| *id)
    }

    /// The length of the datagram that [`Self::take_next`] would return right
    /// now, without removing it. Does *not* run expiry: the caller is
    /// expected to have already expired stale entries for the current
    /// instant (e.g. once per `process_output` call), so that a peek/take
    /// pair can never disagree about what is next.
    #[must_use]
    pub fn peek_next_len(&self) -> Option<usize> {
        let group = self.groups.get(&self.next_group_id()?)?;
        let (_, queue) = group.by_order.last_key_value()?;
        queue.front().map(|d| d.data.len())
    }

    /// Remove and return the next datagram in scheduling order, advancing the
    /// round-robin cursor. Nothing may mutate `self` between a
    /// [`Self::peek_next_len`] and the `take_next` acting on it, or the two
    /// can disagree about which datagram is next.
    pub fn take_next(&mut self) -> Option<QueuedDatagram> {
        let group_id = self.next_group_id()?;
        let group = self.groups.get_mut(&group_id)?;
        let (order, dgram) = group.pop_highest()?;
        let drained = group.is_empty();
        trace!(
            "Datagram {:?} taken (group={group_id:?}, order={order})",
            dgram.id
        );
        self.rr_next = SendGroupId::new(group_id.as_u64().wrapping_add(1));
        Some(self.finish_removal(group_id, drained, dgram))
    }

    /// Update `total_count`/`total_bytes` after removing `dgram` from the queue.
    const fn account_removed(&mut self, dgram: &QueuedDatagram) {
        self.total_count -= 1;
        self.total_bytes -= charge(dgram.data.capacity());
    }

    /// Prune `group_id` from the group map if the removal that produced
    /// `dgram` left it empty, and update the aggregate byte/count
    /// accounting. Shared tail end of every path that removes one datagram
    /// from a specific group ([`Self::evict_at`], [`Self::take_next`]).
    fn finish_removal(
        &mut self,
        group_id: SendGroupId,
        group_now_empty: bool,
        dgram: QueuedDatagram,
    ) -> QueuedDatagram {
        if group_now_empty {
            self.groups.remove(&group_id);
        }
        self.account_removed(&dgram);
        dgram
    }

    /// Arm the resume signal for `charge` bytes, never relaxing a larger
    /// charge an earlier `Rejected` is still waiting on: a smaller write
    /// refused later must not let the larger one be retried too early.
    fn block_on(&mut self, charge: usize) {
        self.blocked = Some(self.blocked.map_or(charge, |c| c.max(charge)));
    }

    /// Call after anything that can unblock the queue: a removal
    /// ([`Self::take_next`], [`Self::expire`], [`Self::set_max_age`]) or a
    /// relaxed bound ([`Self::set_max_buffered_datagrams`], or in tests
    /// `set_max_queued_bytes`). `true` once, the moment a queue that
    /// [`Self::enqueue`] reported as anything but [`DatagramQueueOutcome::Ok`]
    /// drains back below the max-buffered limit and has freed the charge it is
    /// waiting on, for the caller to fire a resume signal. `false` every
    /// other time, including every call while the queue was never blocked.
    ///
    /// An empty queue always counts as unblocked, regardless of the max-buffered
    /// limit or byte budget: `max_queued_bytes == 0` would otherwise
    /// wedge the sender permanently, since a byte total is never below zero,
    /// not even once the queue is empty. `max_buffered_datagrams` cannot itself be
    /// zero - it is a `NonZeroUsize` - so it needs no matching case.
    ///
    /// Exact, not advisory: a [`DatagramQueueOutcome::Rejected`] retry of the
    /// same write is only told to resume once the space it needs has
    /// actually freed up, not merely once *some* space has - otherwise a
    /// write larger than what one removal frees gets rejected again on
    /// retry, for every removal it takes to clear enough room.
    ///
    /// Max-age expiry is the expected way a queue sheds load here, so a
    /// caller that only checks this after a send stalls a blocked
    /// application whenever the queue empties by expiring rather than by
    /// sending.
    #[must_use]
    pub fn resume_if_unblocked(&mut self) -> bool {
        let Some(charge) = self.blocked else {
            return false;
        };
        let unblocked = self.is_empty()
            || (self.below_max_buffered()
                && self.max_queued_bytes.saturating_sub(self.total_bytes) >= charge);
        if unblocked {
            self.blocked = None;
        }
        unblocked
    }

    /// Drain up to `budget` datagrams, expiring old ones first and returning
    /// ready-to-send ones in scheduling order.
    ///
    /// **Scheduling:** groups are served round-robin; within each group the
    /// highest `send_order` is sent first; equal-order datagrams are FIFO. The
    /// round-robin cursor persists across calls, so a drain cut short by its
    /// budget resumes at the next group instead of always starting at the
    /// same one.
    ///
    /// Returns `(expired_count, datagrams_to_send)`.
    ///
    /// Test-only: production code (`QuicDatagrams::write_frames`) pulls one
    /// datagram at a time via [`Self::peek_next_len`]/[`Self::take_next`]
    /// instead, since it needs to check each one against the packet's
    /// remaining space before committing to take it — a multi-item budget
    /// bounded only by count, as `drain` takes, cannot express that.
    #[cfg(test)]
    fn drain(
        &mut self,
        now: Instant,
        budget: usize,
        default_max_age: Duration,
    ) -> (usize, Vec<QueuedDatagram>) {
        // Expiry runs once, up front: after this every remaining datagram is fresh.
        // It is not gated on `budget`, since expiry is not a send: otherwise stale
        // datagrams would pile up while the caller is not ready to take any.
        let expired = self.expire(now, default_max_age);
        let to_send: Vec<_> = std::iter::from_fn(|| self.take_next())
            .take(budget)
            .collect();
        (expired, to_send)
    }

    /// Every remaining queued datagram, regardless of age. Used when the
    /// session is closing and nothing will call [`Self::take_next`] or
    /// [`Self::expire`] again.
    pub fn take_all(&mut self) -> impl Iterator<Item = QueuedDatagram> + use<> {
        self.total_count = 0;
        self.total_bytes = 0;
        self.rr_next = SendGroupId::new(0);
        self.blocked = None;
        std::mem::take(&mut self.groups)
            .into_values()
            .flat_map(|group| group.by_order.into_values().flatten())
    }

    /// The instant at which the oldest queued datagram crosses the
    /// effective max-age, if any datagram is queued.
    ///
    /// `max_age` is applied uniformly across the whole queue at expiry time,
    /// so the next datagram to expire is always whichever one has been
    /// queued the longest - not necessarily the next one [`Self::take_next`]
    /// would return, since scheduling is priority-, not age-, ordered. `None`
    /// if the queue is empty, or if `timestamp + max_age` is not
    /// representable as an `Instant` - not "nothing will ever expire".
    #[must_use]
    pub fn next_expiry(&self, default_max_age: Duration) -> Option<Instant> {
        let max_age = self.effective_max_age(default_max_age);
        self.groups
            .values()
            .filter_map(GroupQueue::oldest_timestamp)
            .min()?
            .checked_add(max_age)
    }

    /// See [`DatagramQueueCapacity`].
    #[must_use]
    pub const fn capacity(&self) -> DatagramQueueCapacity {
        DatagramQueueCapacity {
            remaining_bytes: self.max_queued_bytes.saturating_sub(self.total_bytes),
            queued_datagrams: self.total_count,
            max_queued_bytes: self.max_queued_bytes,
            per_datagram_overhead: PER_DATAGRAM_OVERHEAD,
        }
    }

    #[must_use]
    pub const fn is_empty(&self) -> bool {
        self.total_count == 0
    }

    #[cfg(test)]
    #[must_use]
    pub const fn len(&self) -> usize {
        self.total_count
    }
}

impl Default for DatagramQueue {
    fn default() -> Self {
        Self {
            groups: BTreeMap::new(),
            rr_next: SendGroupId::new(0),
            total_count: 0,
            total_bytes: 0,
            max_queued_bytes: DEFAULT_MAX_QUEUED_BYTES,
            max_buffered_datagrams: None,
            blocked: None,
            max_age: None,
            expired: 0,
            sent: 0,
            too_big: 0,
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use test_fixture::now;

    use super::*;

    /// Tests that set an explicit max age are unaffected by the default, so
    /// they pass one that never expires anything.
    const NO_DEFAULT: Duration = Duration::MAX;

    const fn g(id: u64) -> SendGroupId {
        SendGroupId::new(id)
    }

    fn drain_ids(q: &mut DatagramQueue) -> Vec<u64> {
        let (_, to_send) = q.drain(now(), usize::MAX, NO_DEFAULT);
        to_send
            .into_iter()
            .map(|d| d.id.expect("test datagrams are tracked"))
            .collect()
    }

    /// Enqueue a tracked datagram, discarding the outcome - the assertions in
    /// this suite are on the calls that check it, not on the setup.
    fn enq(q: &mut DatagramQueue, data: Vec<u8>, id: u64, t: Instant, gid: u64, order: SendOrder) {
        _ = q.enqueue(data, Some(id), t, g(gid), order);
    }

    #[test]
    fn queue_basic() {
        let mut q = DatagramQueue::default();
        let t = now();

        let outcome = q.enqueue(vec![1, 2, 3], Some(1), t, g(0), 0);
        assert_eq!(outcome, DatagramQueueOutcome::Ok);
        assert_eq!(q.len(), 1);
    }

    #[test]
    fn enqueue_charges_the_full_allocation_not_just_the_payload() {
        // charge() bills data.capacity(), so a Vec holding unused capacity
        // (e.g. a caller that over-allocates then fills only part of it) is
        // charged for the real memory it holds, not just the payload.
        let mut q = DatagramQueue::default();
        let t = now();
        let mut data = Vec::with_capacity(1000);
        data.extend_from_slice(&[1, 2, 3]);
        let capacity = data.capacity();
        assert!(capacity >= 1000);

        enq(&mut q, data, 1, t, 0, 0);

        assert_eq!(
            q.capacity().remaining_bytes,
            DEFAULT_MAX_QUEUED_BYTES - charge(capacity)
        );
    }

    #[test]
    fn reported_overhead_matches_the_charge() {
        let mut q = DatagramQueue::default();
        let t = now();
        let before = q.capacity();
        let data = vec![0; 1200];
        let allocated = data.capacity();

        enq(&mut q, data, 1, t, 0, 0);

        assert_eq!(
            q.capacity().remaining_bytes,
            before.remaining_bytes - (allocated + before.per_datagram_overhead)
        );
    }

    #[test]
    fn capacity_tracks_bytes_and_count() {
        let mut q = DatagramQueue::default();
        q.set_max_queued_bytes(3 * charge(2)); // room for exactly three 2-byte datagrams
        let t = now();

        assert_eq!(
            q.capacity(),
            DatagramQueueCapacity {
                remaining_bytes: 3 * charge(2),
                queued_datagrams: 0,
                max_queued_bytes: 3 * charge(2),
                per_datagram_overhead: PER_DATAGRAM_OVERHEAD,
            }
        );

        enq(&mut q, vec![0, 1], 1, t, 0, 0);
        assert_eq!(
            q.capacity(),
            DatagramQueueCapacity {
                remaining_bytes: 2 * charge(2),
                queued_datagrams: 1,
                max_queued_bytes: 3 * charge(2),
                per_datagram_overhead: PER_DATAGRAM_OVERHEAD,
            }
        );

        // Draining frees capacity back up.
        q.drain(t, usize::MAX, NO_DEFAULT);
        assert_eq!(
            q.capacity(),
            DatagramQueueCapacity {
                remaining_bytes: 3 * charge(2),
                queued_datagrams: 0,
                max_queued_bytes: 3 * charge(2),
                per_datagram_overhead: PER_DATAGRAM_OVERHEAD,
            }
        );
    }

    #[test]
    fn max_buffered_datagrams() {
        let mut q = DatagramQueue::default();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(2).unwrap()));
        let t = now();

        assert_eq!(
            q.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::Ok
        );
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert_eq!(
            q.enqueue(vec![3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert_eq!(q.len(), 3);
    }

    #[test]
    fn clearing_the_max_buffered_limit_lifts_the_backpressure() {
        let mut q = DatagramQueue::default();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(2).unwrap()));
        let t = now();

        assert_eq!(
            q.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::Ok
        );
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );

        q.set_max_buffered_datagrams(None);
        assert_eq!(
            q.enqueue(vec![3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Ok
        );
    }

    #[test]
    fn raising_the_max_buffered_limit_resumes_without_a_removal() {
        let mut q = DatagramQueue::default();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(1).unwrap()));
        let t = now();

        enq(&mut q, vec![1], 1, t, 0, 0);
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert!(!q.resume_if_unblocked());

        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(3).unwrap()));
        assert!(
            q.resume_if_unblocked(),
            "a raised max-buffered limit unblocks with nothing removed"
        );
    }

    #[test]
    fn raising_the_byte_budget_resumes_without_a_removal() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(charge(1));

        enq(&mut q, vec![1], 1, t, 0, 10);
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );
        assert!(!q.resume_if_unblocked());

        q.set_max_queued_bytes(4 * charge(1));
        assert!(
            q.resume_if_unblocked(),
            "a raised byte budget unblocks with nothing removed"
        );
    }

    #[test]
    fn drain_basic() {
        let mut q = DatagramQueue::default();
        let t = now();

        enq(&mut q, vec![0, 1], 1, t, 0, 0);
        enq(&mut q, vec![0, 2], 2, t, 0, 0);

        let (expired, to_send) = q.drain(now(), usize::MAX, NO_DEFAULT);
        assert_eq!(expired, 0);
        assert_eq!(to_send.len(), 2);
        assert_eq!(to_send[0].id, Some(1));
        assert_eq!(to_send[1].id, Some(2));
        assert!(q.is_empty());
    }

    #[test]
    fn hard_limit_evicts_an_untracked_datagram_without_erroring() {
        let mut queue = DatagramQueue::default();
        let t = now();
        queue.set_max_queued_bytes(charge(1)); // room for exactly one 1-byte datagram

        _ = queue.enqueue(vec![1], None, t, g(0), 0);

        assert_eq!(
            queue.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 },
            "an untracked datagram is as evictable as a tracked one"
        );
        assert_eq!(queue.len(), 1);
    }

    #[test]
    fn byte_budget_zero_does_not_panic() {
        let mut queue = DatagramQueue::default();
        queue.set_max_queued_bytes(0);
        let t = now();

        // Nothing queued yet to evict, so the first datagram is accepted
        // for free rather than panicking on an empty evict_at - but a zero
        // budget is exceeded the moment anything is admitted, so the caller
        // is told to wait rather than getting `Ok`.
        assert_eq!(
            queue.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert_eq!(
            queue.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
    }

    #[test]
    fn zero_byte_budget_resumes_once_the_queue_drains_empty() {
        // `total_bytes < max_queued_bytes` is never true when the budget is
        // zero, even once the queue is empty: `resume_if_unblocked` also has
        // to treat an empty queue as under budget, or a blocked sender waits
        // forever.
        let mut queue = DatagramQueue::default();
        queue.set_max_queued_bytes(0);
        let t = now();

        assert_eq!(
            queue.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert_eq!(
            queue.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );

        queue.take_next().expect("a datagram is queued");
        assert!(
            queue.resume_if_unblocked(),
            "an empty queue must be treated as under budget even when the budget itself is zero"
        );
    }

    #[test]
    fn a_smaller_rejection_does_not_relax_a_larger_outstanding_charge() {
        // A rejected write arms `blocked` with the charge it needed; a later,
        // smaller rejection must not relax that down, or the sender could be
        // resumed with less room freed than the larger write still needs.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(1)); // room for exactly two 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 100);
        enq(&mut q, vec![2], 2, t, 0, 100);
        assert_eq!(
            q.enqueue(vec![9; 3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );
        assert_eq!(
            q.enqueue(vec![9], Some(4), t, g(0), 0),
            DatagramQueueOutcome::Rejected,
            "a second, smaller refused write"
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            !q.resume_if_unblocked(),
            "the larger refused write still needs more room than one removal freed"
        );
    }

    #[test]
    fn a_larger_rejection_raises_a_smaller_outstanding_charge() {
        // The reverse direction of the previous test: a later, larger
        // rejection must raise `blocked`, or a smaller charge recorded first
        // would resume the sender before the larger write actually fits.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(1)); // room for exactly two 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 100);
        enq(&mut q, vec![2], 2, t, 0, 100);
        assert_eq!(
            q.enqueue(vec![9], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Rejected,
            "a smaller refused write first"
        );
        assert_eq!(
            q.enqueue(vec![9; 3], Some(4), t, g(0), 0),
            DatagramQueueOutcome::Rejected,
            "a second, larger refused write"
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            !q.resume_if_unblocked(),
            "the larger refused write needs more room than one removal freed"
        );
    }

    #[test]
    fn max_age_expiration() {
        let mut queue = DatagramQueue::default();
        let t0 = now();
        queue.set_max_age(Some(Duration::from_millis(100)), t0, NO_DEFAULT);
        enq(&mut queue, vec![1], 1, t0, 0, 0);

        // Advance time by 150 ms without sleeping.
        let t1 = t0 + Duration::from_millis(150);

        let expired = queue.expire(t1, NO_DEFAULT);
        assert_eq!(expired, 1);
        assert!(queue.is_empty());
    }

    #[test]
    fn max_age_expiration_untracked_datagram_reports_nothing() {
        let mut queue = DatagramQueue::default();
        let t0 = now();
        queue.set_max_age(Some(Duration::from_millis(100)), t0, NO_DEFAULT);

        _ = queue.enqueue(vec![1], None, t0, g(0), 0);

        let t1 = t0 + Duration::from_millis(150);
        let expired = queue.expire(t1, NO_DEFAULT);
        assert_eq!(expired, 1);
        assert!(queue.is_empty());
    }

    #[test]
    fn next_expiry_is_none_when_empty() {
        let queue = DatagramQueue::default();
        assert_eq!(queue.next_expiry(NO_DEFAULT), None);
    }

    #[test]
    fn next_expiry_is_none_when_the_deadline_overflows() {
        // `NO_DEFAULT` is `Duration::MAX`, so the deadline is unrepresentable
        // rather than absent; with a realistic default there would be one,
        // as `next_expiry_uses_the_default_when_no_explicit_max_age_is_set`
        // shows.
        let mut queue = DatagramQueue::default();
        let t = now();
        enq(&mut queue, vec![1], 1, t, 0, 0);
        assert_eq!(queue.next_expiry(NO_DEFAULT), None);
    }

    #[test]
    fn expiry_is_inclusive_at_the_deadline() {
        // `next_expiry` names `timestamp + max_age`; a caller that wakes at
        // exactly that instant has to find something to shed, or it
        // reschedules the same deadline forever.
        let mut queue = DatagramQueue::default();
        let t0 = now();
        queue.set_max_age(Some(Duration::from_millis(100)), t0, NO_DEFAULT);
        enq(&mut queue, vec![1], 1, t0, 0, 0);

        let deadline = queue.next_expiry(NO_DEFAULT).expect("a datagram is queued");
        assert_eq!(deadline, t0 + Duration::from_millis(100));
        assert_eq!(queue.expire(deadline, NO_DEFAULT), 1);
        assert_eq!(queue.next_expiry(NO_DEFAULT), None);
    }

    #[test]
    fn next_expiry_tracks_the_oldest_datagram_across_groups() {
        let mut queue = DatagramQueue::default();
        let t0 = now();
        queue.set_max_age(Some(Duration::from_millis(50)), t0, NO_DEFAULT);

        enq(&mut queue, vec![1], 1, t0, 0, 0);
        let t1 = t0 + Duration::from_millis(10);
        // A later-enqueued datagram in a different group must not shadow the
        // oldest one: `next_expiry` looks across every group, not just one.
        enq(&mut queue, vec![2], 2, t1, 1, 0);

        assert_eq!(
            queue.next_expiry(NO_DEFAULT),
            Some(t0 + Duration::from_millis(50))
        );
    }

    #[test]
    fn next_expiry_advances_once_the_oldest_datagram_is_gone() {
        let mut queue = DatagramQueue::default();
        let t0 = now();
        queue.set_max_age(Some(Duration::from_millis(50)), t0, NO_DEFAULT);

        enq(&mut queue, vec![1], 1, t0, 0, 0);
        let t1 = t0 + Duration::from_millis(10);
        enq(&mut queue, vec![2], 2, t1, 0, 0);

        let (_, to_send) = queue.drain(t1, 1, NO_DEFAULT);
        assert_eq!(to_send.len(), 1);
        assert_eq!(
            queue.next_expiry(NO_DEFAULT),
            Some(t1 + Duration::from_millis(50))
        );
    }

    #[test]
    fn next_expiry_uses_the_default_when_no_explicit_max_age_is_set() {
        let mut queue = DatagramQueue::default();
        let t0 = now();
        enq(&mut queue, vec![1], 1, t0, 0, 0);

        assert_eq!(
            queue.next_expiry(Duration::from_millis(30)),
            Some(t0 + Duration::from_millis(30))
        );
    }

    #[test]
    fn drain() {
        let mut queue = DatagramQueue::default();
        let t = now();

        enq(&mut queue, vec![0, 1], 1, t, 0, 0);
        _ = queue.enqueue(vec![0, 2], None, t, g(0), 0);

        let (expired, to_send) = queue.drain(t, usize::MAX, NO_DEFAULT);

        assert_eq!(expired, 0);
        assert_eq!(to_send.len(), 2);
        assert_eq!(to_send[0].id, Some(1));
        assert_eq!(to_send[1].id, None);
        assert!(queue.is_empty());
    }

    #[test]
    fn drain_reports_every_expired_datagram() {
        // Untracked datagrams produce no outcome, but the caller still has to be
        // able to count them, so `drain` reports one entry per expired datagram.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_age(Some(Duration::from_millis(50)), t0, NO_DEFAULT);

        enq(&mut q, vec![1], 1, t0, 0, 0);
        _ = q.enqueue(vec![2], None, t0, g(0), 0);

        let (expired, to_send) = q.drain(t0 + Duration::from_millis(80), usize::MAX, NO_DEFAULT);
        assert_eq!(expired, 2);
        assert!(to_send.is_empty());
    }

    #[test]
    fn below_max_buffered_recovers_after_drain() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(2).unwrap()));

        assert_eq!(
            q.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::Ok
        );
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );

        drop(q.drain(t, usize::MAX, NO_DEFAULT));

        assert_eq!(
            q.enqueue(vec![3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Ok,
            "draining the queue must put it back below the max-buffered limit"
        );
    }

    #[test]
    fn priority_order_within_group() {
        // Enqueue low-priority datagrams first, then high-priority.
        // The queue should send highest send_order first.
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 10); // order 10
        enq(&mut q, vec![0, 2], 2, t, 0, 30); // order 30 (highest)
        enq(&mut q, vec![0, 3], 3, t, 0, 20); // order 20

        let sent = drain_ids(&mut q);
        assert_eq!(sent, vec![2, 3, 1], "highest order first");
    }

    #[test]
    fn fifo_within_same_order() {
        let mut q = DatagramQueue::default();
        // All same group, same order → FIFO.
        let t = now();
        enq(&mut q, vec![0, 10], 10, t, 0, 5);
        enq(&mut q, vec![0, 11], 11, t, 0, 5);
        enq(&mut q, vec![0, 12], 12, t, 0, 5);

        assert_eq!(drain_ids(&mut q), vec![10, 11, 12]);
    }

    #[test]
    fn priority_mixed_orders_same_group() {
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 1);
        enq(&mut q, vec![0, 2], 2, t, 0, 3);
        enq(&mut q, vec![0, 3], 3, t, 0, 1); // same as id=1
        enq(&mut q, vec![0, 4], 4, t, 0, 3); // same as id=2

        // Expected: id=2 then id=4 (order 3), then id=1 then id=3 (order 1)
        assert_eq!(drain_ids(&mut q), vec![2, 4, 1, 3]);
    }

    #[test]
    fn round_robin_two_groups() {
        let mut q = DatagramQueue::default();
        // Group A (id 0): 3 datagrams, Group B (id 1): 2 datagrams.
        // Round-robin should interleave: A, B, A, B, A.
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 0); // group A
        enq(&mut q, vec![0, 2], 2, t, 1, 0); // group B
        enq(&mut q, vec![0, 3], 3, t, 0, 0); // group A
        enq(&mut q, vec![0, 4], 4, t, 1, 0); // group B
        enq(&mut q, vec![0, 5], 5, t, 0, 0); // group A

        assert_eq!(drain_ids(&mut q), vec![1, 2, 3, 4, 5]);
    }

    #[test]
    fn round_robin_priority_across_groups() {
        // Each group has datagrams at different send_orders.
        // Group 0: order 10, order 5
        // Group 1: order 20, order 1
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 10);
        enq(&mut q, vec![0, 2], 2, t, 0, 5);
        enq(&mut q, vec![0, 3], 3, t, 1, 20);
        enq(&mut q, vec![0, 4], 4, t, 1, 1);

        // Round 1: group 0 sends id=1 (order 10), group 1 sends id=3 (order 20)
        // Round 2: group 0 sends id=2 (order 5), group 1 sends id=4 (order 1)
        assert_eq!(drain_ids(&mut q), vec![1, 3, 2, 4]);
    }

    #[test]
    fn round_robin_three_groups() {
        let mut q = DatagramQueue::default();
        let t = now();
        // One datagram per group; should all be sent in one round.
        enq(&mut q, vec![0, 1], 1, t, 10, 0);
        enq(&mut q, vec![0, 2], 2, t, 20, 0);
        enq(&mut q, vec![0, 3], 3, t, 30, 0);

        assert_eq!(drain_ids(&mut q), vec![1, 2, 3]);
    }

    #[test]
    fn default_max_age_falls_back_before_the_first_rtt_sample() {
        // min_rtt is zero until the connection measures one; that must not
        // collapse onto the floor.
        assert_eq!(
            default_max_age(Duration::ZERO),
            DEFAULT_INITIAL_RTT * DEFAULT_MAX_AGE_RTT_MULTIPLIER_NUM
                / DEFAULT_MAX_AGE_RTT_MULTIPLIER_DEN
        );
    }

    #[test]
    fn default_max_age_scales_with_rtt() {
        // Above the floor: scales at 1.25x.
        assert_eq!(
            default_max_age(Duration::from_millis(50)),
            Duration::from_micros(62_500)
        );
        // Below the floor: clamped to the 20ms floor rather than following RTT
        // down.
        assert_eq!(
            default_max_age(Duration::from_millis(3)),
            Duration::from_millis(20)
        );
        // Saturates rather than panicking on `Duration`'s multiply overflow.
        assert_eq!(default_max_age(Duration::MAX), Duration::MAX);
    }

    #[test]
    fn unset_max_age_uses_the_default() {
        let mut q = DatagramQueue::default();
        let t0 = now();
        // Never call set_max_age: the application left outgoingMaxAge null.
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let default = Duration::from_millis(20);
        let (expired, to_send) = q.drain(t0 + Duration::from_millis(50), 10, default);
        assert_eq!(expired, 1, "the default bounds an unset max age");
        assert!(to_send.is_empty());
    }

    #[test]
    fn explicit_max_age_overrides_the_default() {
        let mut q = DatagramQueue::default();
        let t0 = now();
        // A longer explicit value must win over a shorter default.
        q.set_max_age(
            Some(Duration::from_millis(100)),
            t0,
            Duration::from_millis(5),
        );
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let (expired, to_send) =
            q.drain(t0 + Duration::from_millis(50), 10, Duration::from_millis(5));
        assert_eq!(expired, 0);
        assert_eq!(to_send.len(), 1);
    }

    #[test]
    fn clearing_max_age_restores_the_default() {
        let mut q = DatagramQueue::default();
        let t0 = now();
        let default = Duration::from_millis(20);

        q.set_max_age(Some(Duration::from_millis(100)), t0, default);
        // outgoingMaxAge = null; the setter also maps 0 to null, so `None`
        // is what both reach here as.
        q.set_max_age(None, t0, default);
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let (expired, _) = q.drain(t0 + Duration::from_millis(50), 10, default);
        assert_eq!(expired, 1, "the default is back in force");
    }

    #[test]
    fn lowering_max_age_sheds_already_queued_datagrams() {
        // Every other call site in this suite sets max_age before anything
        // is queued, so this pins that tightening `outgoingMaxAge` sheds
        // what is already queued, at the setter, not just on the next
        // `expire`, and that the shed datagram is counted like any other
        // expiry.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_age(Some(Duration::from_millis(100)), t0, NO_DEFAULT);
        enq(&mut q, vec![1], 1, t0, 0, 0);

        q.set_max_age(
            Some(Duration::from_millis(10)),
            t0 + Duration::from_millis(50),
            NO_DEFAULT,
        );
        assert!(q.is_empty());
        assert_eq!(q.take_expired_count(), 1);
        assert_eq!(q.capacity().remaining_bytes, DEFAULT_MAX_QUEUED_BYTES);
    }

    #[test]
    fn expired_count_accumulates_across_sweeps_until_taken() {
        // Two separate `expire` calls, as when the connection's timer sweep
        // and a per-session sweep each shed one datagram: the count must
        // survive both and be handed out exactly once.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_age(Some(Duration::from_millis(10)), t0, NO_DEFAULT);
        enq(&mut q, vec![1], 1, t0, 0, 0);
        enq(&mut q, vec![2], 2, t0 + Duration::from_millis(5), 0, 0);

        assert_eq!(q.expire(t0 + Duration::from_millis(10), NO_DEFAULT), 1);
        assert_eq!(q.expire(t0 + Duration::from_millis(15), NO_DEFAULT), 1);

        assert_eq!(q.take_expired_count(), 2);
        assert_eq!(q.take_expired_count(), 0);
    }

    #[test]
    fn explicit_max_age_is_clamped_to_the_1ms_floor() {
        let mut q = DatagramQueue::default();
        let t0 = now();
        let default = Duration::from_millis(20);
        q.set_max_age(Some(Duration::from_micros(1)), t0, default);
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let (expired, to_send) = q.drain(t0 + Duration::from_micros(500), 10, default);
        assert_eq!(
            expired, 0,
            "clamped to the 1ms floor, so 500us old is not yet expired"
        );
        assert_eq!(to_send.len(), 1);
    }

    #[test]
    fn explicit_max_age_above_default_is_not_clamped() {
        let mut q = DatagramQueue::default();
        let t0 = now();
        // A low-RTT path: the default is well under the explicit value below.
        let default = Duration::from_millis(20);
        q.set_max_age(Some(Duration::from_millis(300)), t0, default);
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let (expired, to_send) = q.drain(t0 + Duration::from_millis(150), 10, default);
        assert_eq!(
            expired, 0,
            "there is no ceiling, so 150ms old must not yet expire against a 300ms max age"
        );
        assert_eq!(to_send.len(), 1);
    }

    #[test]
    fn drain_stops_at_budget() {
        let mut q = DatagramQueue::default();
        let t = now();
        for i in 1..=5 {
            enq(&mut q, vec![0, i], u64::from(i), t, 0, 0);
        }

        let (_, to_send) = q.drain(t, 2, NO_DEFAULT);
        assert_eq!(
            to_send.iter().map(|d| d.id).collect::<Vec<_>>(),
            vec![Some(1), Some(2)]
        );
        assert_eq!(q.len(), 3, "the rest stays queued");
    }

    #[test]
    fn drain_budget_zero_keeps_everything() {
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![1], 1, t, 0, 0);

        let (expired, to_send) = q.drain(t, 0, NO_DEFAULT);
        assert_eq!(expired, 0);
        assert!(to_send.is_empty());
        assert_eq!(q.len(), 1);
    }

    #[test]
    fn budgeted_drain_expires_even_with_no_budget() {
        // Expiry is not a send, so it must not be gated on the caller having
        // room; otherwise stale datagrams pile up while there is none.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_age(Some(Duration::from_millis(50)), t0, NO_DEFAULT);
        enq(&mut q, vec![1], 1, t0, 0, 0);

        let (expired, to_send) = q.drain(t0 + Duration::from_millis(80), 0, NO_DEFAULT);
        assert_eq!(expired, 1);
        assert!(to_send.is_empty());
        assert!(q.is_empty());
    }

    #[test]
    fn budgeted_drain_sends_highest_priority_first() {
        // The whole point of budgeting: when only part of a burst fits, the
        // part that goes out is the high-priority part.
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 1);
        enq(&mut q, vec![0, 2], 2, t, 0, 100);
        enq(&mut q, vec![0, 3], 3, t, 0, 50);

        let (_, to_send) = q.drain(t, 1, NO_DEFAULT);
        assert_eq!(
            to_send.iter().map(|d| d.id).collect::<Vec<_>>(),
            vec![Some(2)]
        );
    }

    #[test]
    fn partial_drain_resumes_round_robin_across_calls() {
        // One datagram per group, three groups, one datagram of budget per
        // call: each group must get a turn instead of group 0 taking every one.
        let mut q = DatagramQueue::default();
        let t = now();
        for gid in 0..3_u64 {
            enq(&mut q, vec![0, 1], gid, t, gid, 0);
        }

        let mut served = Vec::new();
        for _ in 0..3 {
            let (_, to_send) = q.drain(t, 1, NO_DEFAULT);
            served.extend(to_send.into_iter().filter_map(|d| d.id));
        }

        assert_eq!(served, vec![0, 1, 2]);
        assert!(q.is_empty());
    }

    #[test]
    fn round_robin_cursor_wraps() {
        let mut q = DatagramQueue::default();
        let t = now();
        // Two datagrams in each of two groups.
        for gid in 0..2_u64 {
            for i in 0..2_u64 {
                enq(&mut q, vec![0, 1], gid * 10 + i, t, gid, 0);
            }
        }

        // Budget of one per call: alternate groups, wrapping back to group 0.
        let mut served = Vec::new();
        for _ in 0..4 {
            let (_, to_send) = q.drain(t, 1, NO_DEFAULT);
            served.extend(to_send.into_iter().filter_map(|d| d.id));
        }

        assert_eq!(served, vec![0, 10, 1, 11]);
        assert!(q.is_empty());
    }

    #[test]
    fn byte_budget_evicts_lowest_priority() {
        let mut q = DatagramQueue::default();
        q.set_max_queued_bytes(3 * charge(2)); // room for exactly three 2-byte datagrams

        // Fill with order-0 datagrams.
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 0);
        enq(&mut q, vec![0, 2], 2, t, 0, 0);
        enq(&mut q, vec![0, 3], 3, t, 0, 0);
        assert_eq!(q.len(), 3);

        // Adding a higher-priority datagram should evict the lowest-priority one (id=1, order 0).
        assert_eq!(
            q.enqueue(vec![0, 4], Some(4), t, g(0), 10),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(q.len(), 3);

        // The high-priority datagram is sent first; id 1 - the evicted one - is gone.
        assert_eq!(drain_ids(&mut q), vec![4, 2, 3]);
    }

    #[test]
    fn byte_budget_evicts_across_groups() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(2)); // room for exactly two 2-byte datagrams

        // Group 0 has order 5, group 1 has order 1 (lower priority).
        enq(&mut q, vec![0, 1], 1, t, 0, 5);
        enq(&mut q, vec![0, 2], 2, t, 1, 1);

        // Adding a third datagram evicts the globally lowest-priority one (id=2, order 1).
        assert_eq!(
            q.enqueue(vec![0, 3], Some(3), t, g(0), 5),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(q.len(), 2);
        assert_eq!(
            drain_ids(&mut q),
            vec![1, 3],
            "group 1's datagram (id 2) was evicted, group 0's are untouched"
        );
    }

    #[test]
    fn byte_budget_rejects_lower_priority_newcomer() {
        let mut q = DatagramQueue::default();
        q.set_max_queued_bytes(3 * charge(2)); // room for exactly three 2-byte datagrams

        // Fill with high-priority (order 10) datagrams.
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 10);
        enq(&mut q, vec![0, 2], 2, t, 0, 10);
        enq(&mut q, vec![0, 3], 3, t, 0, 10);
        assert_eq!(q.len(), 3);

        // A lower-priority newcomer must not evict any of the higher-priority
        // datagrams already queued: it is the one that gets rejected.
        assert_eq!(
            q.enqueue(vec![0, 4], Some(4), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );
        assert_eq!(q.len(), 3);
        assert_eq!(
            drain_ids(&mut q),
            vec![1, 2, 3],
            "the queued high-priority datagrams are untouched"
        );
    }

    #[test]
    fn negative_send_order_sorts_below_zero() {
        // `sendOrder` is a signed `long long`, so negative orders are
        // reachable: they must rank below 0, not above it.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(2)); // room for exactly two 2-byte datagrams
        enq(&mut q, vec![0, 1], 1, t, 0, -5);
        enq(&mut q, vec![0, 2], 2, t, 0, 0);

        // The order-(-5) datagram is the global eviction victim, not the order-0 one.
        assert_eq!(
            q.enqueue(vec![0, 3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(drain_ids(&mut q), vec![2, 3]);
    }

    #[test]
    fn byte_budget_rejects_lower_priority_newcomer_across_groups() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(2)); // room for exactly two 2-byte datagrams

        // Group 0 has order 5, group 1 has order 1 (lower priority already queued).
        enq(&mut q, vec![0, 1], 1, t, 0, 5);
        enq(&mut q, vec![0, 2], 2, t, 1, 1);

        // A newcomer with a lower priority than the current global lowest
        // (group 1, order 1) must be rejected itself, not evict group 1's datagram.
        assert_eq!(
            q.enqueue(vec![0, 3], Some(3), t, g(2), 0),
            DatagramQueueOutcome::Rejected
        );
        assert_eq!(q.len(), 2);
    }

    #[test]
    fn byte_budget_admits_equal_priority_newcomer_across_groups() {
        // Group ID is a determinism tiebreak for *victim selection*, not a
        // policy for *admission*: at equal `send_order`, a newcomer from a
        // lower-numbered group must still be able to evict an
        // equal-priority occupant of a higher-numbered group. Otherwise the
        // lowest-numbered group is starved forever under sustained
        // equal-order pressure from other groups (a naive tuple comparison
        // makes a lower group ID always lose ties, since it sorts first).
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(2)); // room for exactly two 2-byte datagrams

        enq(&mut q, vec![0, 1], 1, t, 2, 1);
        enq(&mut q, vec![0, 2], 2, t, 1, 1);
        assert_eq!(q.len(), 2);

        // Same order as the current global lowest (group 1's, since 1 < 2
        // breaks the tie): admitted by evicting group 1, not rejected.
        assert_eq!(
            q.enqueue(vec![0, 3], Some(3), t, g(0), 1),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(q.len(), 2);
        assert_eq!(
            drain_ids(&mut q),
            vec![3, 1],
            "group 1's equal-priority datagram was evicted, not the newcomer rejected"
        );
    }

    #[test]
    fn byte_budget_evicts_multiple_datagrams_for_one_large_one() {
        // A single incoming datagram can be larger than what any one eviction
        // frees, so eviction must loop rather than run at most once.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(4 * charge(1)); // room for exactly four 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 0);
        enq(&mut q, vec![2], 2, t, 0, 0);
        enq(&mut q, vec![3], 3, t, 0, 0);
        enq(&mut q, vec![4], 4, t, 0, 0);
        assert_eq!(q.len(), 4);

        // Sized to need exactly three evictions whatever the per-datagram
        // overhead `O`: the newcomer costs `2*O + 3`, each eviction frees
        // `O + 1`, and the budget is `4*O + 4`, so two evictions leave it
        // one byte short.
        assert_eq!(
            q.enqueue(vec![9; PER_DATAGRAM_OVERHEAD + 3], Some(5), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 3 }
        );
        assert_eq!(q.len(), 2);
        assert_eq!(
            drain_ids(&mut q),
            vec![4, 5],
            "only the three oldest 1-byte datagrams (1, 2, 3) are evicted"
        );
    }

    #[test]
    fn byte_budget_eviction_burst_moves_to_the_next_group_once_one_drains() {
        // A burst of same-priority datagrams in one group (e.g. one video
        // frame) drains without re-deriving the victim per item, but once
        // that group empties entirely, eviction has to look elsewhere
        // rather than panicking or stalling on the now-gone group.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(4 * charge(1)); // room for exactly four 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 0);
        enq(&mut q, vec![2], 2, t, 0, 0);
        enq(&mut q, vec![3], 3, t, 1, 10);
        enq(&mut q, vec![4], 4, t, 1, 10);
        assert_eq!(q.len(), 4);

        // Needs three evictions to fit (sized as in the previous test): both
        // of group 0's order-0 datagrams (the lower-priority bucket, drained
        // as one burst), then one from group 1's order-10 bucket once group
        // 0 no longer exists.
        assert_eq!(
            q.enqueue(vec![9; PER_DATAGRAM_OVERHEAD + 3], Some(5), t, g(0), 20),
            DatagramQueueOutcome::Overflowed { dropped: 3 }
        );
        assert_eq!(q.len(), 2);
        assert_eq!(
            drain_ids(&mut q),
            vec![5, 4],
            "both of group 0's datagrams (1, 2) and one of group 1's (3) are evicted"
        );
    }

    #[test]
    fn byte_budget_stops_evicting_once_what_is_left_outranks_the_newcomer() {
        // Multi-eviction re-checks priority every round: the newcomer
        // outranks the lowest bucket but not the rest, so it must not
        // displace better traffic merely because one eviction freed too
        // little.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(4 * charge(1)); // room for exactly four 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 0);
        enq(&mut q, vec![2], 2, t, 0, 100);
        enq(&mut q, vec![3], 3, t, 0, 100);
        enq(&mut q, vec![4], 4, t, 0, 100);

        // Evicting the order-0 datagram frees `O + 1` of the `2*O + 3` the
        // newcomer needs; everything still queued outranks it, so it is
        // admitted over budget instead.
        assert_eq!(
            q.enqueue(vec![9; PER_DATAGRAM_OVERHEAD + 3], Some(5), t, g(0), 1),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(
            q.capacity().remaining_bytes,
            0,
            "admitted over budget rather than displacing higher-priority traffic"
        );
        assert!(
            !q.resume_if_unblocked(),
            "over budget is still backpressure"
        );
        assert_eq!(
            drain_ids(&mut q),
            vec![2, 3, 4, 5],
            "only the datagram the newcomer outranked may be evicted"
        );
    }

    #[test]
    fn overflow_arms_the_resume_signal() {
        // An overflowing queue is full whether or not a max-buffered limit is
        // set, and the caller is told to wait on every outcome but `Ok`: a
        // resume signal has to follow.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(charge(1));

        assert_eq!(
            q.enqueue(vec![1], Some(1), t, g(0), 0),
            DatagramQueueOutcome::Ok
        );
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            q.resume_if_unblocked(),
            "an overflowed queue that drained must resume the sender"
        );
        assert!(!q.resume_if_unblocked(), "and only once");
    }

    #[test]
    fn rejection_arms_the_resume_signal() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(charge(1));

        enq(&mut q, vec![1], 1, t, 0, 10);
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            q.resume_if_unblocked(),
            "a refused sender must be told when there is room again"
        );
    }

    #[test]
    fn rejection_does_not_resume_while_still_over_the_byte_budget() {
        // No max-buffered limit set (the default) means `below_max_buffered()` is
        // unconditionally true, so the byte budget itself has to gate resume
        // or a rejected sender spins: told to resume, rejected again.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(charge(1));

        enq(&mut q, vec![1], 1, t, 0, 10);
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );

        assert!(
            !q.resume_if_unblocked(),
            "still over budget until something is taken"
        );
    }

    #[test]
    fn rejection_of_a_large_write_does_not_resume_until_it_would_fit() {
        // The refused write's own charge (67), not merely "some" freed
        // space, has to gate resume: each removal here frees only 65 B, less
        // than the rejected write needs, so one removal must not resume it.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(2 * charge(1)); // room for exactly two 1-byte datagrams

        enq(&mut q, vec![1], 1, t, 0, 100);
        enq(&mut q, vec![2], 2, t, 0, 100);
        assert_eq!(
            q.enqueue(vec![9; 3], Some(3), t, g(0), 0),
            DatagramQueueOutcome::Rejected
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            !q.resume_if_unblocked(),
            "one removal frees less than the refused write needs"
        );

        q.take_next().expect("a datagram is queued");
        assert!(
            q.resume_if_unblocked(),
            "the queue is now empty, so the refused write would fit"
        );
    }

    #[test]
    fn expiry_alone_releases_a_blocked_queue() {
        // Max-age expiry, not a send, is the expected way this queue sheds
        // load, so a queue that empties by expiring must resume its sender
        // too - nothing will ever take from it.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(1).unwrap()));
        q.set_max_age(Some(Duration::from_millis(10)), t0, NO_DEFAULT);

        assert_eq!(
            q.enqueue(vec![1], Some(1), t0, g(0), 0),
            DatagramQueueOutcome::MaxBufferedReached
        );
        assert_eq!(q.expire(t0 + Duration::from_millis(10), NO_DEFAULT), 1);
        assert!(
            q.resume_if_unblocked(),
            "a queue emptied by expiry must resume the sender"
        );
    }

    #[test]
    fn byte_budget_evicts_fifo_within_one_order_bucket() {
        // Equal group and equal order: the oldest is the eviction victim.
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_queued_bytes(3 * charge(1)); // room for exactly three 1-byte datagrams
        enq(&mut q, vec![1], 1, t, 0, 0);
        enq(&mut q, vec![2], 2, t, 0, 0);
        enq(&mut q, vec![3], 3, t, 0, 0);
        assert_eq!(
            q.enqueue(vec![4], Some(4), t, g(0), 0),
            DatagramQueueOutcome::Overflowed { dropped: 1 }
        );
        assert_eq!(drain_ids(&mut q), vec![2, 3, 4]);
    }

    #[test]
    fn max_age_expiry_during_drain() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_age(Some(Duration::from_millis(50)), t, NO_DEFAULT);

        enq(&mut q, vec![0, 1], 1, t, 0, 0);
        let t1 = t + Duration::from_millis(80);
        enq(&mut q, vec![0, 2], 2, t1, 0, 0);

        let (expired, to_send) = q.drain(t1, usize::MAX, NO_DEFAULT);
        let sent_ids: Vec<_> = to_send.iter().map(|d| d.id).collect();
        assert_eq!(expired, 1);
        assert_eq!(sent_ids, vec![Some(2)]);
        assert!(q.is_empty());
    }

    #[test]
    fn max_age_expiry_high_priority_bucket() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_age(Some(Duration::from_millis(50)), t, NO_DEFAULT);

        enq(&mut q, vec![0, 1], 1, t, 0, 100); // high priority, will expire
        let t1 = t + Duration::from_millis(80);
        enq(&mut q, vec![0, 2], 2, t1, 0, 1); // low priority, fresh

        let (_, to_send) = q.drain(t1, usize::MAX, NO_DEFAULT);
        let sent_ids: Vec<_> = to_send.iter().map(|d| d.id).collect();
        assert_eq!(
            sent_ids,
            vec![Some(2)],
            "lower-priority-but-fresh datagram is sent"
        );
    }

    #[test]
    fn expiry_prunes_an_emptied_group_without_stalling_the_others() {
        // A group left behind empty by `expire` is still picked by
        // `next_group_id`, and `take_next`/`peek_next_len` then report the
        // queue as drained even though another group holds a fresh datagram.
        let mut q = DatagramQueue::default();
        let t0 = now();
        q.set_max_age(Some(Duration::from_millis(50)), t0, NO_DEFAULT);
        enq(&mut q, vec![1], 1, t0, 0, 0);
        let t1 = t0 + Duration::from_millis(80);
        enq(&mut q, vec![2], 2, t1, 1, 0);

        assert_eq!(q.expire(t1, NO_DEFAULT), 1);
        assert_eq!(q.len(), 1);
        assert_eq!(q.peek_next_len(), Some(1));
        assert_eq!(q.take_next().expect("group 1 still holds one").id, Some(2));
    }

    #[test]
    fn partial_drain_leaves_remainder_for_a_later_call() {
        let mut q = DatagramQueue::default();
        let t = now();
        for i in 1..=3 {
            enq(&mut q, vec![0, i], u64::from(i), t, 0, 0);
        }

        let mut served = Vec::new();
        for _ in 0..3 {
            let (_, to_send) = q.drain(t, 1, NO_DEFAULT);
            served.extend(to_send.into_iter().filter_map(|d| d.id));
        }

        assert_eq!(served, vec![1, 2, 3]);
        assert!(q.is_empty());
    }

    #[test]
    fn round_robin_cursor_advances_under_partial_drain() {
        // Three groups, two datagrams each, drained two at a time. Without
        // advancing `rr_next` past whatever the budget cut off, the group
        // that missed its turn this round would be pushed to the back of
        // every future round too, instead of just this one.
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 100, t, 0, 0);
        enq(&mut q, vec![0, 2], 110, t, 1, 0);
        enq(&mut q, vec![0, 3], 120, t, 2, 0);
        enq(&mut q, vec![0, 4], 101, t, 0, 0);
        enq(&mut q, vec![0, 5], 111, t, 1, 0);
        enq(&mut q, vec![0, 6], 121, t, 2, 0);

        let mut served = Vec::new();
        for _ in 0..3 {
            let (_, to_send) = q.drain(t, 2, NO_DEFAULT);
            served.extend(to_send.into_iter().filter_map(|d| d.id));
        }

        assert_eq!(
            served,
            vec![100, 110, 120, 101, 111, 121],
            "group 2 gets its first turn in round 2, not pushed back to round 3"
        );
        assert!(q.is_empty());
    }

    #[test]
    fn peek_next_len_matches_take_next() {
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1, 2], 1, t, 0, 0);

        assert_eq!(q.peek_next_len(), Some(3));
        let dgram = q.take_next().expect("peeked Some above");
        assert_eq!(dgram.id, Some(1));
        assert_eq!(dgram.data.len(), 3);
        assert_eq!(q.peek_next_len(), None);
    }

    #[test]
    fn peek_and_take_follow_priority_and_round_robin() {
        let mut q = DatagramQueue::default();
        let t = now();
        enq(&mut q, vec![0, 1], 1, t, 0, 0);
        enq(&mut q, vec![0, 2], 2, t, 1, 0);

        assert_eq!(q.peek_next_len(), Some(2));
        assert_eq!(q.take_next().unwrap().id, Some(1));
        assert_eq!(q.take_next().unwrap().id, Some(2));
        assert_eq!(q.peek_next_len(), None);
    }

    #[test]
    fn take_all_returns_everything_and_resets_the_queue() {
        let mut q = DatagramQueue::default();
        let t = now();
        q.set_max_buffered_datagrams(Some(NonZeroUsize::new(1).unwrap()));
        enq(&mut q, vec![1], 1, t, 0, 0);
        assert_eq!(
            q.enqueue(vec![2], Some(2), t, g(1), 5),
            DatagramQueueOutcome::MaxBufferedReached
        );

        let taken = q.take_all();
        assert_eq!(
            taken.map(|d| d.id).collect::<Vec<_>>(),
            vec![Some(1), Some(2)]
        );
        assert!(q.is_empty());
        assert_eq!(q.capacity().remaining_bytes, DEFAULT_MAX_QUEUED_BYTES);
        assert!(
            !q.resume_if_unblocked(),
            "a closing session needs no resume"
        );
    }
}
