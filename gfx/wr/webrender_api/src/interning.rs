/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Content-side interning for the display list builder.
//!
//! An [`Interner`] lives in the `DisplayListBuilder` and is *retained across
//! builds*: it is deliberately not touched by `DisplayListBuilder::reset`. That
//! gives two kinds of de-duplication for free:
//!
//! * **Within one build** - pushing the same item twice hashes to the same
//!   entry and yields the same [`Handle`], so the item's data is written into
//!   the display list once instead of once per occurrence.
//! * **Across builds** - an item that survives into the next display list is
//!   still in the map, so it keeps the same handle and its data is not
//!   re-transmitted at all.
//!
//! Instead of the data, the display list carries a [`Handle`]: a slot index
//! plus the build in which the item was first interned. Because the handle is
//! stable for as long as the entry lives, an unchanged item produces identical
//! display list bytes from one build to the next.
//!
//! The receiver keeps a slot-indexed store of the item data. It is fed by the
//! [`InternOps`] delta that [`Interner::end_build`] returns at the end of each
//! build: the adds minted during that build, and the removes produced by that
//! build's garbage collection. The receiver is a pure follower and never talks
//! back. Because it only ever follows, the deltas form a strict sequence -
//! every one has to be delivered, in order, or the two sides are out of step
//! for good.
//!
//! A builder holds one interner per interned type, gathered in [`DlInterners`].
//! They are begun and ended together, bracketed by [`DlInterners::begin_build`]
//! and [`DlInterners::end_build`], and share one build number, so what crosses
//! the IPC boundary is a single [`DlDelta`] per display list: the builder's
//! identity, the build it closes, and one [`InternOps`] per type. The set of
//! types is the list in [`enumerate_dl_interned_types!`], which the receiver
//! mirrors field for field.
//!
//! Garbage collection runs once per build, in `end_build`. An entry is dropped
//! once it has been absent from [`RETAIN_BUILDS`] consecutive display lists,
//! which frees its slot for re-use and emits a remove op. The delay is not
//! what keeps the receiver safe - it applies a remove only together with the
//! scene built from the list that no longer references the entry - it is
//! there so that content flickering an item in and out does not re-send it.
//!
//! Garbage collection frees entries but not the capacity they occupied, so
//! after one very large scene the containers would otherwise stay sized for it
//! for the life of the builder. `end_build` therefore also counts the builds
//! since the interner last filled more than half its capacity, and once that
//! reaches [`SHRINK_AFTER_BUILDS`] it reallocates the containers down to what
//! is live. The wait is hysteresis: a scene that alternates between large and
//! small should not pay for a reallocation each way.

use crate::serde::{Deserialize, Serialize};
use malloc_size_of::MallocSizeOf;
use std::collections::HashMap;
use std::hash::Hash;
use std::marker::PhantomData;

/// How many consecutive display lists an entry may be absent from before it is
/// garbage collected. One would be correct - the receiver never reads an entry
/// after the swap to a scene that stopped referencing it - but it would re-send
/// every item that leaves the list for a build and comes back, such as a hover
/// state or a blinking caret. Ten builds is cheap (entries are small) and
/// covers that kind of churn.
pub const RETAIN_BUILDS: u32 = 10;

/// How many consecutive builds an interner may sit below half its allocated
/// capacity before the capacity is given back. Long enough that a scene
/// growing and shrinking within a few frames does not thrash the allocator,
/// short enough that a tab that has moved on from a heavy page stops paying
/// for it within a second or so of steady painting.
pub const SHRINK_AFTER_BUILDS: u32 = 30;

/// Monotonic counter of display list builds, used both to age entries for
/// garbage collection and as the generation stamp in a [`Handle`].
#[derive(Debug, Copy, Clone, Default, Eq, Hash, MallocSizeOf, Ord, PartialEq, PartialOrd)]
#[derive(Deserialize, Serialize)]
pub struct BuildId(pub u32);

/// Identifies one builder's interners, so a receiver can tell apart two
/// builders writing display lists for the same pipeline. Slot numbering is per
/// builder and starts at zero, so a second builder silently collides with the
/// first; carrying this makes that a detectable error rather than corruption.
///
/// It has to differ from every other builder that ever wrote to the same
/// pipeline, for as long as the receiver remembers one: a receiver that sees a
/// displaced builder's id come back reports two builders alternating, so an id
/// re-used by an unrelated builder (an address, say) would trip that. That
/// rules out deriving it from the builder itself, and there is no owner on the
/// content side to hand ids out the way the backend hands out `IdNamespace`s,
/// so it is a process-wide counter. The process id on top is belt and braces:
/// pipeline ids are already per process.
#[derive(Debug, Copy, Clone, Default, Eq, Hash, MallocSizeOf, PartialEq)]
#[derive(Deserialize, Serialize)]
pub struct BuilderId(pub u64);

impl BuilderId {
    fn next() -> Self {
        use std::sync::atomic::{AtomicU32, Ordering};
        static NEXT: AtomicU32 = AtomicU32::new(0);

        let counter = NEXT.fetch_add(1, Ordering::Relaxed);
        BuilderId(((std::process::id() as u64) << 32) | counter as u64)
    }
}

/// Identifies an interned item. This is what the display list carries in place
/// of the item's data.
///
/// `slot` indexes the receiver's store. `build` is the build the item was
/// *first* interned in, not the last one that used it, so the handle stays
/// byte-identical for as long as the entry lives. Together they form a value
/// that is unique for the lifetime of the builder even though slots are
/// recycled, which lets the receiver detect a desynchronised stream.
// `K` only appears in the marker, which serializes to nothing, so the derive's
// inferred `K: Serialize + Deserialize` bound is dropped; same reason as the
// hand-written impls below.
#[derive(MallocSizeOf, Deserialize, Serialize)]
#[serde(bound = "")]
pub struct Handle<K> {
    slot: u32,
    build: BuildId,
    _marker: PhantomData<K>,
}

// Hand-written rather than derived: deriving would add a spurious `K: Trait`
// bound for the `PhantomData<K>`, and `K` is never actually held here.
impl<K> Clone for Handle<K> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<K> Copy for Handle<K> {}

impl<K> PartialEq for Handle<K> {
    fn eq(&self, other: &Self) -> bool {
        self.slot == other.slot && self.build == other.build
    }
}

impl<K> Eq for Handle<K> {}

impl<K> Hash for Handle<K> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.slot.hash(state);
        self.build.hash(state);
    }
}

impl<K> Default for Handle<K> {
    fn default() -> Self {
        Handle::INVALID
    }
}

impl<K> std::fmt::Debug for Handle<K> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        if *self == Handle::INVALID {
            write!(f, "<invalid>")
        } else {
            write!(f, "#{}:{}", self.slot, self.build.0)
        }
    }
}

// A handle rides the peek-poke display item stream, so it needs the impls the
// item types get from `#[derive(PeekPoke)]`. Written out by hand because the
// derive would demand `K: Peek + Poke` for the `PhantomData<K>`.
unsafe impl<K> peek_poke::Poke for Handle<K> {
    fn max_size() -> usize {
        <u32>::max_size() + <u32>::max_size()
    }

    unsafe fn poke_into(&self, bytes: *mut u8) -> *mut u8 {
        let bytes = self.slot.poke_into(bytes);
        self.build.0.poke_into(bytes)
    }
}

impl<K> peek_poke::Peek for Handle<K> {
    unsafe fn peek_from(bytes: *const u8, output: *mut Self) -> *const u8 {
        let bytes = <u32>::peek_from(bytes, std::ptr::addr_of_mut!((*output).slot));
        <u32>::peek_from(bytes, std::ptr::addr_of_mut!((*output).build.0))
    }
}

impl<K> Handle<K> {
    pub const INVALID: Self = Handle {
        slot: !0,
        build: BuildId(!0),
        _marker: PhantomData,
    };

    /// Index of this item in the receiver's store.
    pub fn slot(&self) -> u32 {
        self.slot
    }

    /// The build this item was first interned in.
    pub fn build(&self) -> BuildId {
        self.build
    }
}

/// An item newly interned during a build, to be inserted into the receiver's
/// store at `slot`.
#[derive(Debug, Clone, MallocSizeOf, Deserialize, Serialize)]
pub struct InternAdd<K> {
    pub slot: u32,
    pub build: BuildId,
    pub key: K,
}

/// What one build did to one interner. The per-type part of [`DlDelta`], which
/// is what actually crosses IPC.
///
/// Apply `adds` and `removes` in that order. They cannot conflict within one
/// delta - adds are minted while the display list is being built and removes
/// are produced by the garbage collection that follows it, so a slot freed by
/// this build is only ever re-used by a *later* one.
#[derive(Debug, Clone, MallocSizeOf, Deserialize, Serialize)]
pub struct InternOps<K> {
    pub adds: Vec<InternAdd<K>>,
    /// Slots to clear, in no particular order.
    pub removes: Vec<u32>,
}

impl<K> InternOps<K> {
    /// Whether this delta asks the receiver to do anything.
    pub fn is_empty(&self) -> bool {
        self.adds.is_empty() && self.removes.is_empty()
    }
}

impl<K> Default for InternOps<K> {
    fn default() -> Self {
        InternOps {
            adds: Vec::new(),
            removes: Vec::new(),
        }
    }
}

/// What the interner tracks per unique item.
#[derive(Debug, MallocSizeOf)]
struct Entry {
    /// Slot in the receiver's store.
    slot: u32,
    /// Build this item was first interned in. Stamped into every handle handed
    /// out for it, so the handle is stable across builds.
    interned_in: BuildId,
    /// Most recent build that referenced this item. Drives garbage collection.
    last_used: BuildId,
}

/// Interns values of type `K`, handing out a stable [`Handle`] per unique
/// value and accumulating the [`InternOps`] delta the receiver needs.
///
/// See the module docs for the lifecycle. In short: `intern` during a build,
/// `end_build` once at the end of it. The build number comes from the owning
/// [`DlInterners`], since every interner in a builder shares it.
#[derive(Debug, MallocSizeOf)]
pub struct Interner<K: Eq + Hash + MallocSizeOf> {
    /// The interned set. Also the authority on which slots are live.
    entries: HashMap<K, Entry>,
    /// Slots freed by garbage collection, available for re-use. Handed out in
    /// arbitrary order; nothing depends on which free slot a new item lands in.
    free_slots: Vec<u32>,
    /// Number of slots ever handed out; the next slot to use when `free_slots`
    /// is empty.
    slot_count: u32,
    /// Adds minted during the current build, drained by `end_build`.
    pending_adds: Vec<InternAdd<K>>,
    /// How many builds an entry may go unreferenced before collection.
    retain_builds: u32,
    /// Consecutive builds that ended with `entries` less than half full. Drives
    /// the capacity give-back described in the module docs.
    builds_under_half_capacity: u32,
}

impl<K: Eq + Hash + MallocSizeOf> Default for Interner<K> {
    fn default() -> Self {
        Interner::new(RETAIN_BUILDS)
    }
}

impl<K: Eq + Hash + MallocSizeOf> Interner<K> {
    pub fn new(retain_builds: u32) -> Self {
        assert!(retain_builds > 0, "an entry must survive the build that used it");

        Interner {
            entries: HashMap::new(),
            free_slots: Vec::new(),
            slot_count: 0,
            pending_adds: Vec::new(),
            retain_builds,
            builds_under_half_capacity: 0,
        }
    }

    /// Number of live interned items.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

impl<K: Clone + Eq + Hash + MallocSizeOf> Interner<K> {
    /// Intern `key` during `build`, returning the handle to write into the
    /// display list.
    ///
    /// Repeat calls with an equal key - whether later in this build or in a
    /// subsequent one - return the same handle and emit no further ops.
    ///
    /// Takes the key by reference so the common case (already interned) does no
    /// work beyond the lookup; only a genuinely new item is cloned.
    ///
    /// `DisplayListBuilder::restore` rolls back items, not interning, so a
    /// handle minted inside a rolled-back region is simply dropped along with
    /// the item that held it - nothing may go looking for it afterwards. The
    /// entry survives, which costs the receiver one slot of data that nothing
    /// references until garbage collection reclaims it, and means re-pushing
    /// the same item after the rollback gets the same handle for free.
    pub fn intern(&mut self, build: BuildId, key: &K) -> Handle<K> {
        if let Some(entry) = self.entries.get_mut(key) {
            entry.last_used = build;

            return Handle {
                slot: entry.slot,
                build: entry.interned_in,
                _marker: PhantomData,
            };
        }

        let slot = match self.free_slots.pop() {
            Some(slot) => slot,
            None => {
                let slot = self.slot_count;
                self.slot_count += 1;
                slot
            }
        };

        self.pending_adds.push(InternAdd {
            slot,
            build,
            key: key.clone(),
        });

        self.entries.insert(
            key.clone(),
            Entry {
                slot,
                interned_in: build,
                last_used: build,
            },
        );

        Handle {
            slot,
            build,
            _marker: PhantomData,
        }
    }

    /// Close `build`: garbage collect stale entries, then hand back the delta
    /// for the build that just finished.
    ///
    /// Exactly one call per build, and the caller **must** ship the delta it
    /// returns. Handles minted this build are already written into the display
    /// list, and re-interning the same key in a later build hits the surviving
    /// entry and mints no replacement add - so a dropped delta leaves the
    /// receiver permanently missing those slots. A caller that discards a built
    /// display list must discard the interner with it.
    ///
    /// The interner itself cannot tell a build that was started and abandoned
    /// from one that is still going - it only sees the build number, and that
    /// only advances here - so that check lives in [`DlInterners`], which
    /// brackets every build with `begin_build` and `end_build`.
    pub fn end_build(&mut self, build: BuildId) -> InternOps<K> {
        let current = build.0;
        let retain_builds = self.retain_builds;
        let free_slots = &mut self.free_slots;
        let mut removes = Vec::new();

        self.entries.retain(|_, entry| {
            // `last_used` can never run ahead of `current`; saturating keeps a
            // bug from silently wrapping into "collect everything" in release.
            debug_assert!(entry.last_used.0 <= current);
            if current.saturating_sub(entry.last_used.0) >= retain_builds {
                free_slots.push(entry.slot);
                removes.push(entry.slot);
                return false;
            }

            true
        });

        self.maybe_shrink();

        InternOps {
            adds: std::mem::take(&mut self.pending_adds),
            removes,
        }
    }

    /// Give back capacity left over from a larger scene, once it has gone
    /// unused for `SHRINK_AFTER_BUILDS` consecutive builds. Both shrinks are
    /// no-ops when nothing would be freed, so the counter is the only cost of
    /// asking every time.
    fn maybe_shrink(&mut self) {
        if self.entries.len() * 2 < self.entries.capacity() {
            self.builds_under_half_capacity += 1;
        } else {
            self.builds_under_half_capacity = 0;
        }

        if self.builds_under_half_capacity >= SHRINK_AFTER_BUILDS {
            self.entries.shrink_to_fit();
            self.free_slots.shrink_to_fit();
            self.builds_under_half_capacity = 0;
        }
    }
}

/// A key type the display list builder interns. Implemented for every entry of
/// [`enumerate_dl_interned_types!`]; it is what lets [`DlInterners::intern`]
/// pick the right interner from the key's type alone.
pub trait DlInterned: Clone + Eq + Hash + MallocSizeOf + Sized {
    fn interner(interners: &mut DlInterners) -> &mut Interner<Self>;
}

/// Every type the display list builder interns, one line each: the field
/// name, which [`DlInterners`], [`DlDelta`] and the receiver's stores all
/// share, and the key type.
///
/// This is the one place a type is added on the content side; the receiver's
/// lists live in `webrender/src/dl_interner.rs`. The primitive types move
/// over one at a time, see `webrender/doc/dl-builder-interning.md`.
#[macro_export]
macro_rules! enumerate_dl_interned_types {
    ($macro_name: ident) => {
        $macro_name! {
            rectangles: $crate::interned_prims::RectangleKey,
            path: $crate::path::Path,
            box_shadows: $crate::interned_prims::BoxShadowKey,
            line_decorations: $crate::interned_prims::LineDecorationKey,
            images: $crate::interned_prims::ImagePrimKey,
            yuv_images: $crate::interned_prims::YuvImagePrimKey,
        }
    }
}

macro_rules! declare_dl_interners {
    ( $( $field:ident : $key:ty, )* ) => {
        /// Every interner one display list builder holds, plus the identity a
        /// receiver checks its delta stream with.
        ///
        /// The build number lives here rather than in each [`Interner`] because
        /// the interners are begun and ended together: a per-interner number
        /// would be the same number repeated, and a receiver would have to
        /// reconcile several copies of it to spot a lost delta. One counter
        /// also means an interner that saw no items in a build still advances
        /// with the rest, which is what keeps the numbering contiguous.
        #[derive(Debug, MallocSizeOf)]
        pub struct DlInterners {
            /// Stamped on every delta so a receiver can tell this builder's
            /// stream from another one's for the same pipeline.
            id: BuilderId,
            /// The build currently being accumulated. Advances on every
            /// `end_build`.
            build: BuildId,
            /// Whether `begin_build` has been called without a matching
            /// `end_build` yet. The interners only see build numbers, and
            /// those only advance in `end_build`, so this is the one thing
            /// that can tell an abandoned build from one still in progress.
            open: bool,
            $( $field: Interner<$key>, )*
        }

        impl Default for DlInterners {
            fn default() -> Self {
                DlInterners {
                    id: BuilderId::next(),
                    build: BuildId(0),
                    open: false,
                    $( $field: Interner::default(), )*
                }
            }
        }

        $(
            impl DlInterned for $key {
                fn interner(interners: &mut DlInterners) -> &mut Interner<Self> {
                    &mut interners.$field
                }
            }
        )*

        /// What one display list build did to its builder's interners. This
        /// is the payload that goes over IPC, one per display list.
        ///
        /// Every delta must be delivered, exactly once, in order: the stores
        /// are pure followers with no acknowledgement, so a gap in the stream
        /// leaves the two sides out of step for good. `builder` and `build`
        /// are what let a receiver notice.
        #[derive(Debug, Clone, MallocSizeOf, Deserialize, Serialize)]
        pub struct DlDelta {
            /// Which builder produced this, so a receiver can spot a second
            /// builder writing to the same pipeline's slot space.
            pub builder: BuilderId,
            /// The build this delta closes. Consecutive per builder, including
            /// builds that interned nothing, so a receiver can tell a lost,
            /// repeated or reordered delta from a contiguous stream.
            pub build: BuildId,
            $( pub $field: InternOps<$key>, )*
        }

        impl DlDelta {
            /// Whether this delta asks the receiver to do anything. `builder`
            /// and `build` are metadata, so they do not count.
            pub fn is_empty(&self) -> bool {
                true $( && self.$field.is_empty() )*
            }
        }

        impl Default for DlDelta {
            fn default() -> Self {
                DlDelta {
                    // Not any real builder: `BuilderId::next` always sets a
                    // process id in the high word. Display lists reconstructed
                    // without a delta (deserialization) land here, and their
                    // empty delta is ignored rather than taken for a stream of
                    // its own.
                    builder: BuilderId(0),
                    build: BuildId(0),
                    $( $field: InternOps::default(), )*
                }
            }
        }

        impl DlInterners {
            /// Open a build. Every `intern` until the matching `end_build` is
            /// stamped with the current build number.
            ///
            /// Panics if the previous build was never closed. Its adds are
            /// still pending and would otherwise ride this build's delta,
            /// describing entries the receiver was never told about in a
            /// display list it never saw.
            pub fn begin_build(&mut self) {
                assert!(!self.open, "a display list build was abandoned without end_build");
                self.open = true;
            }

            /// Close the current build on every interner at once and advance
            /// to the next. See [`Interner::end_build`] for the obligation the
            /// returned delta puts on the caller.
            pub fn end_build(&mut self) -> DlDelta {
                assert!(self.open, "end_build without a matching begin_build");
                self.open = false;

                let build = self.build;
                self.build = BuildId(build.0 + 1);

                DlDelta {
                    builder: self.id,
                    build,
                    $( $field: self.$field.end_build(build), )*
                }
            }
        }
    }
}

enumerate_dl_interned_types!(declare_dl_interners);

impl DlInterners {
    /// Intern `key` in the current build. See [`Interner::intern`].
    pub fn intern<K: DlInterned>(&mut self, key: &K) -> Handle<K> {
        debug_assert!(self.open, "intern outside begin_build / end_build");
        let build = self.build;
        K::interner(self).intern(build, key)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// An interner plus the build counter a `DlInterners` would drive it with,
    /// so the tests read the way the calling code does.
    struct TestInterner {
        interner: Interner<u32>,
        build: BuildId,
    }

    impl TestInterner {
        fn intern(&mut self, key: u32) -> Handle<u32> {
            self.interner.intern(self.build, &key)
        }

        fn end_build(&mut self) -> InternOps<u32> {
            let ops = self.interner.end_build(self.build);
            self.build = BuildId(self.build.0 + 1);
            ops
        }

        fn len(&self) -> usize {
            self.interner.len()
        }
    }

    fn interner(retain_builds: u32) -> TestInterner {
        TestInterner {
            interner: Interner::new(retain_builds),
            build: BuildId(0),
        }
    }

    #[test]
    fn dedups_within_a_single_build() {
        let mut i = interner(RETAIN_BUILDS);

        let a = i.intern(10);
        let b = i.intern(10);
        let c = i.intern(20);

        assert_eq!(a, b);
        assert_ne!(a, c);
        assert_eq!(i.len(), 2);

        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 2, "one add per unique item, not per intern");
        assert!(ops.removes.is_empty());
    }

    #[test]
    fn dedups_across_builds_without_resending() {
        let mut i = interner(RETAIN_BUILDS);

        let first = i.intern(10);
        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 1);

        // Same item in the next build: same handle, nothing on the wire.
        let second = i.intern(10);
        assert_eq!(first, second);

        let ops = i.end_build();
        assert!(ops.is_empty(), "an unchanged item must not be re-transmitted");
    }

    #[test]
    fn handle_is_stable_across_builds() {
        let mut i = interner(RETAIN_BUILDS);

        let first = i.intern(10);
        for _ in 0..5 {
            i.end_build();
            assert_eq!(i.intern(10), first, "handle bytes must not churn");
        }
    }

    #[test]
    fn collects_after_the_retain_window() {
        let mut i = interner(3);

        let handle = i.intern(10);
        i.end_build();

        // Absent for two builds: still retained, so still no ops.
        for _ in 0..2 {
            assert!(i.end_build().is_empty());
            assert_eq!(i.len(), 1);
        }

        // Third build absent: collected.
        let ops = i.end_build();
        assert_eq!(ops.removes, vec![handle.slot()]);
        assert_eq!(i.len(), 0);
    }

    #[test]
    fn touching_an_entry_resets_its_age() {
        let mut i = interner(2);

        let handle = i.intern(10);

        // Referenced every other build, so it sits at the edge of the retain
        // window forever without ever falling out of it.
        for _ in 0..5 {
            assert!(i.end_build().removes.is_empty());
            assert!(i.end_build().removes.is_empty());
            assert_eq!(i.len(), 1, "entry collected despite being referenced");
            assert_eq!(i.intern(10), handle);
        }
    }

    #[test]
    fn reuses_collected_slots() {
        let mut i = interner(1);

        let first = i.intern(10);
        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 1);

        let ops = i.end_build();
        assert_eq!(ops.removes, vec![first.slot()]);

        // The freed slot is handed to the next new item, but with a later
        // build stamp so the handle is distinguishable from the old one.
        let second = i.intern(20);
        assert_eq!(second.slot(), first.slot());
        assert_ne!(second, first);

        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 1);
        assert_eq!(ops.adds[0].slot, first.slot());
        assert!(ops.removes.is_empty());
    }

    #[test]
    fn reuses_freed_slots_before_growing() {
        let mut i = interner(1);

        for key in 0..4 {
            i.intern(key);
        }
        i.end_build();

        // Drop 1 and 2, keep 0 and 3. Which slot a new item lands in is not
        // specified, so compare as sets.
        i.intern(0);
        i.intern(3);
        let mut removed = i.end_build().removes;
        removed.sort_unstable();
        assert_eq!(removed, vec![1, 2]);

        i.intern(0);
        i.intern(3);
        let mut reused = vec![i.intern(100).slot(), i.intern(101).slot()];
        reused.sort_unstable();
        assert_eq!(reused, vec![1, 2], "both freed slots are handed out again");
        assert_eq!(i.intern(102).slot(), 4, "grow only once the free list is spent");
    }

    #[test]
    fn a_rolled_back_item_keeps_its_handle_and_adds_once() {
        let mut i = interner(1);

        // Interned inside a region the caller then rolls back with
        // `DisplayListBuilder::restore`. The item bytes go, the entry stays.
        let handle = i.intern(10);

        // Re-pushing the same item after the rollback hits that entry, so it
        // costs no second slot and no second add.
        assert_eq!(i.intern(10), handle);

        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 1);
        assert_eq!(ops.adds[0].slot, handle.slot());
    }

    #[test]
    fn a_rolled_back_item_that_is_never_re_pushed_is_reclaimed() {
        let mut i = interner(1);

        // Rolled back and not re-pushed: the receiver is still told to fill the
        // slot, because the add was minted before the rollback.
        let handle = i.intern(10);
        assert_eq!(i.end_build().adds.len(), 1);

        // Nothing references it, so the next collection clears it again.
        assert_eq!(i.end_build().removes, vec![handle.slot()]);
    }

    #[test]
    fn ops_are_incremental() {
        let mut i = interner(RETAIN_BUILDS);

        i.intern(10);
        assert_eq!(i.end_build().adds.len(), 1);

        i.intern(10);
        i.intern(20);
        let ops = i.end_build();
        assert_eq!(ops.adds.len(), 1, "only the newly interned item");
        assert_eq!(ops.adds[0].key, 20);
    }

    #[test]
    fn invalid_handle_is_distinguishable() {
        let mut i = interner(RETAIN_BUILDS);

        let handle = i.intern(10);
        assert_ne!(handle, Handle::INVALID);
        assert_eq!(Handle::<u32>::default(), Handle::INVALID);
    }

    #[test]
    fn deltas_are_numbered_consecutively() {
        let mut interners = DlInterners::default();

        // Including the empty ones: a receiver checking contiguity must be able
        // to account for a build that interned nothing.
        for expected in 0..3 {
            interners.begin_build();
            assert_eq!(interners.end_build().build, BuildId(expected));
        }
    }

    #[test]
    #[should_panic(expected = "abandoned")]
    fn an_abandoned_build_is_caught_by_the_next_begin() {
        let mut interners = DlInterners::default();

        interners.begin_build();
        interners.begin_build();
    }

    #[test]
    #[should_panic(expected = "without a matching begin_build")]
    fn ending_a_build_that_was_never_begun_is_caught() {
        let mut interners = DlInterners::default();
        interners.end_build();
    }

    #[test]
    fn shrinks_once_a_spike_has_passed() {
        let mut i = interner(1);

        for key in 0..1024 {
            i.intern(key);
        }
        i.end_build();
        let peak = i.interner.entries.capacity();
        assert!(peak >= 1024);

        // One live entry from here on. The first build below collects the
        // rest and is the first one counted; the shrink happens on the
        // SHRINK_AFTER_BUILDS-th.
        // `HashMap::capacity` drifts down a little as removals leave
        // tombstones, so test the bound rather than the exact number.
        for _ in 1..SHRINK_AFTER_BUILDS {
            i.intern(0);
            i.end_build();
            assert!(i.interner.entries.capacity() >= 1024, "shrank before the wait was up");
        }

        i.intern(0);
        i.end_build();
        assert!(i.interner.entries.capacity() < peak / 2, "capacity was not given back");
        assert_eq!(i.len(), 1);
        assert_eq!(i.intern(0).slot(), 0, "the surviving entry is intact");
    }

    #[test]
    fn a_steadily_large_scene_keeps_its_capacity() {
        let mut i = interner(1);

        for key in 0..1024 {
            i.intern(key);
        }
        i.end_build();
        let peak = i.interner.entries.capacity();

        for _ in 0..SHRINK_AFTER_BUILDS * 2 {
            for key in 0..1024 {
                i.intern(key);
            }
            i.end_build();
        }

        assert!(i.interner.entries.capacity() >= peak);
    }

    #[test]
    fn every_delta_from_one_builder_carries_its_id() {
        let mut first = DlInterners::default();
        let mut second = DlInterners::default();

        first.begin_build();
        let id = first.end_build().builder;
        first.begin_build();
        assert_eq!(first.end_build().builder, id, "a builder's id is fixed");
        second.begin_build();
        assert_ne!(second.end_build().builder, id, "two builders must differ");
        assert_ne!(id, DlDelta::default().builder, "the sentinel is not a builder");
    }

    #[test]
    fn a_delta_with_no_types_is_empty() {
        let mut interners = DlInterners::default();
        interners.begin_build();
        assert!(interners.end_build().is_empty());
    }
}
