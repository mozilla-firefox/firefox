/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Follower storage for items interned by the content process display list
//! builder. See `doc/dl-builder-interning.md` for the whole design.
//!
//! Content mints slot numbers per display list builder, starting at zero, so
//! two pipelines collide in a single store. The scene builder hands each
//! pipeline a dense `DlNamespace` the first time it sees one of that
//! pipeline's display lists, and a `DlHandle` is that namespace plus the
//! content slot. Because the handle names its namespace, one batch can
//! reference items from several pipelines.
//!
//! A `DlStore` is a pure follower: content allocates every slot and the store
//! only replays the deltas it is handed. Nothing here ever picks a slot, and
//! the inner level deliberately has no free list - if a store allocated for
//! itself the two sides would diverge with nothing able to notice.

use crate::intern::ItemUid;
use crate::internal_types::FastHashMap;
use api::interning::{BuildId, BuilderId};
use api::{DocumentId, FontRenderMode, IdNamespace, PipelineId};
use glyph_rasterizer::SharedFontResources;
use std::marker::PhantomData;
use std::{fmt, ops};

/// Every follower store the render backend holds, one line each: the field
/// name (the same one `enumerate_dl_interned_types!` in `webrender_api` uses
/// for the delta), the content key type, the value the store holds for it,
/// the profiler gauge set from the store's size, and the field of the
/// interning memory report it is accounted under.
///
/// Adding a type here and to the api-side list is all the plumbing a
/// primitive needs on the receiving side; see `doc/dl-builder-interning.md`.
#[macro_export]
macro_rules! enumerate_dl_stores {
    ($macro_name: ident) => {
        $macro_name! {
            rectangles: api::interned_prims::RectangleKey => crate::prim_store::rectangle::RectangleTemplate, INTERNED_PRIMITIVES, prim,
            path: api::Path => api::Path, INTERNED_PATHS, path,
            box_shadows: api::interned_prims::BoxShadowKey => crate::box_shadow::BoxShadowTemplate, INTERNED_BOX_SHADOWS, box_shadow,
            line_decorations: api::interned_prims::LineDecorationKey => crate::prim_store::line_dec::LineDecorationTemplate, INTERNED_LINE_DECORATIONS, line_decoration,
            images: api::interned_prims::ImagePrimKey => crate::prim_store::image::ImageTemplate, INTERNED_IMAGES, image,
            yuv_images: api::interned_prims::YuvImagePrimKey => crate::prim_store::image::YuvImageTemplate, INTERNED_YUV_IMAGES, yuv_image,
        }
    }
}

/// The follower stores the scene builder itself reads, in the same shape as
/// `enumerate_dl_stores!` minus the gauge. Every primitive type has one
/// holding its `PrimPlacement`, since a primitive's display item carries only
/// the handle, the spatial id and the clip chain. Clips will need their key,
/// since clip chain building, slice partitioning and hit testing read it.
#[macro_export]
macro_rules! enumerate_scene_dl_stores {
    ($macro_name: ident) => {
        $macro_name! {
            rectangles: api::interned_prims::RectangleKey => crate::prim_store::PrimPlacement, prim,
            path: api::Path => api::Path, path,
            box_shadows: api::interned_prims::BoxShadowKey => crate::prim_store::PrimPlacement, box_shadow,
            line_decorations: api::interned_prims::LineDecorationKey => crate::prim_store::PrimPlacement, line_decoration,
            images: api::interned_prims::ImagePrimKey => crate::prim_store::PrimPlacement, image,
            yuv_images: api::interned_prims::YuvImagePrimKey => crate::prim_store::PrimPlacement, yuv_image,
        }
    }
}

/// What a store needs to turn a content key into the value it holds.
// Rectangles resolve from the key alone; the fields are for fonts and images.
#[allow(dead_code)]
pub struct DlResolveContext<'a> {
    /// The id namespace the display list carrying the delta was submitted
    /// with, for validating any resource key the interned item names.
    pub id_namespace: IdNamespace,
    pub fonts: &'a SharedFontResources,
    pub default_font_render_mode: FontRenderMode,
}

/// A store value that can be built from its content key. Done where the delta
/// is applied, once per interned entry rather than once per scene build.
pub trait DlResolve<K>: Sized {
    fn resolve(key: &K, ctx: &DlResolveContext) -> Self;
}

impl DlResolve<api::Path> for api::Path {
    fn resolve(key: &api::Path, _ctx: &DlResolveContext) -> Self {
        key.clone()
    }
}

/// Dense index identifying one content display list builder's slot space.
/// Recycled when its pipeline goes away, so it stays small enough to index an
/// array with - unlike `IdNamespace`, which never recycles.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Debug, Copy, Clone, PartialEq, Eq, Hash, MallocSizeOf)]
pub struct DlNamespace(pub u32);

/// A reference to one item interned by content: the slot content assigned it,
/// qualified by the namespace its builder was given.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[cfg_attr(any(feature = "capture", feature = "replay"), serde(bound = ""))]
pub struct DlHandle<K> {
    pub namespace: DlNamespace,
    pub slot: u32,
    /// Which occupant of `namespace` this handle was minted against. Namespaces
    /// are recycled, so a handle that outlives a retained scene into one built
    /// after the recycle would otherwise read another pipeline's data with no
    /// sign of anything wrong (invariant 4).
    ///
    /// Debug only: it exists to catch that, and carrying it always would widen
    /// the handle past two words for a case that should never happen.
    #[cfg(debug_assertions)]
    generation: u32,
    _marker: PhantomData<K>,
}

// Hand-written rather than derived: the derives would demand the same trait of
// `K`, which is only ever a marker here.
impl<K> Copy for DlHandle<K> {}

impl<K> Clone for DlHandle<K> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<K> PartialEq for DlHandle<K> {
    fn eq(&self, other: &Self) -> bool {
        self.namespace == other.namespace && self.slot == other.slot
    }
}

impl<K> Eq for DlHandle<K> {}

impl<K> std::hash::Hash for DlHandle<K> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.namespace.hash(state);
        self.slot.hash(state);
    }
}

impl<K> fmt::Debug for DlHandle<K> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "DlHandle({}, {})", self.namespace.0, self.slot)
    }
}

impl<K> malloc_size_of::MallocSizeOf for DlHandle<K> {
    fn size_of(&self, _ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        0
    }
}

impl<K> DlHandle<K> {
    /// Names no entry; a lookup through it panics. Used where a handle field
    /// has to exist but is never read, such as the clip tree's root sentinel.
    pub const INVALID: Self = DlHandle {
        namespace: DlNamespace(u32::MAX),
        slot: u32::MAX,
        #[cfg(debug_assertions)]
        generation: 0,
        _marker: PhantomData,
    };

    /// `generation` is what `DlBuilderMap::expect` returned alongside the
    /// namespace. Taken even in release, where it is discarded, so that callers
    /// do not have to know which build they are in.
    pub fn new(namespace: DlNamespace, generation: u32, slot: u32) -> Self {
        let _ = generation;
        DlHandle {
            namespace,
            slot,
            #[cfg(debug_assertions)]
            generation,
            _marker: PhantomData,
        }
    }

    /// A handle into another store for an entry minted by the same builder:
    /// same namespace (and generation), a slot of the other type. This is how
    /// a content-side handle held *inside* an interned value - a clip's polygon
    /// - is qualified, since it shares its owner's builder.
    pub fn sibling<K2>(&self, slot: u32) -> DlHandle<K2> {
        DlHandle {
            namespace: self.namespace,
            slot,
            #[cfg(debug_assertions)]
            generation: self.generation,
            _marker: PhantomData,
        }
    }
}

/// What the scene builder tracks for each content display list builder.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
struct BuilderState {
    namespace: DlNamespace,
    /// The builder whose stream we are following, once one has claimed this
    /// pipeline. `None` until the first delta arrives.
    builder: Option<BuilderId>,
    /// Build number the next delta from that builder must carry.
    expected_build: BuildId,
    /// A builder that used to own this pipeline and was displaced by the
    /// current one. Kept only to recognise it if it comes back, which is the
    /// one shape of builder change that cannot be handled - see
    /// `DeltaAction::Reset`.
    displaced: Option<BuilderId>,
}

/// What the arrival of a display list's delta means for the stores.
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum DeltaAction {
    /// The next delta in the stream we are following. Apply it.
    Apply,
    /// A different builder has taken over this pipeline and its slot numbering
    /// starts again at zero, so the previous builder's entries have to go before
    /// this delta lands: close the namespace, reopen it, then apply.
    ///
    /// This is what a new content process, or any client that builds each
    /// display list with a fresh builder, looks like from here. It is only sound
    /// because the displaced builder never returns: its surviving entries are
    /// dropped, and it would not re-mint them. Two builders alternating on one
    /// pipeline is therefore still an error, and `check_delta` reports it as one.
    Reset,
    /// Not from the builder we follow, and interns nothing, so it can neither
    /// disturb slot state nor be disturbed by it. Leave the stream alone.
    Ignore,
}

/// A pipeline as seen through one document. One scene builder serves every
/// document on its backend, and a pipeline can be in two of them at once: a
/// tab moved to another window sends display lists to the new window's
/// document while the old one has yet to remove it. Each gets its own
/// namespace, so that a namespace's open and close always travel with the same
/// document's transactions, which the render backend applies in order.
pub type DlPipelineKey = (DocumentId, PipelineId);

/// Maps each pipeline to the state of the builder producing its display lists:
/// the namespace its slots live in, and where its delta stream has got to.
/// Lives on the scene builder; content knows nothing about either, so there is
/// no allocation round trip.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Default)]
pub struct DlBuilderMap {
    by_pipeline: FastHashMap<DlPipelineKey, BuilderState>,
    free: Vec<DlNamespace>,
    /// Pipelines removed but not yet released, and whether their document is
    /// gone, in which case any document's scene build releases them; see
    /// `remove_pipeline` and `remove_document`.
    pending_removals: FastHashMap<DlPipelineKey, bool>,
    next: u32,
    /// How many times each namespace index has been handed to a builder. Stays
    /// in step with the store's count, which advances on `open`, because every
    /// bump here emits exactly one `Open`. See `DlHandle::generation`.
    generations: Vec<u32>,
}

impl DlBuilderMap {
    pub fn get(&self, document_id: DocumentId, pipeline_id: PipelineId) -> Option<DlNamespace> {
        self.by_pipeline.get(&(document_id, pipeline_id)).map(|state| state.namespace)
    }

    /// The map as seen by a scene build of `document_id`.
    pub fn document(&self, document_id: DocumentId) -> DlDocumentBuilders {
        DlDocumentBuilders { map: self, document_id }
    }

    /// Every namespace currently assigned to a pipeline, with its generation,
    /// including those whose release `take_removals` still owes.
    pub fn live_namespaces(&self) -> impl Iterator<Item = (DlNamespace, u32)> + '_ {
        self.by_pipeline.values().map(move |state| {
            (state.namespace, self.generations[state.namespace.0 as usize])
        })
    }

    fn bump_generation(&mut self, namespace: DlNamespace) {
        let index = namespace.0 as usize;
        if index >= self.generations.len() {
            self.generations.resize(index + 1, 0);
        }
        self.generations[index] += 1;
    }

    /// The namespace for this pipeline, allocating one if this is the first
    /// display list seen for it. A `true` second element means it was just
    /// allocated and the caller must open it on every store before applying
    /// anything to it.
    pub fn get_or_alloc(
        &mut self,
        document_id: DocumentId,
        pipeline_id: PipelineId,
    ) -> (DlNamespace, bool) {
        let key = (document_id, pipeline_id);

        // Live again, so any release owed for an earlier removal is off.
        self.pending_removals.remove(&key);

        if let Some(state) = self.by_pipeline.get(&key) {
            return (state.namespace, false);
        }

        let namespace = self.free.pop().unwrap_or_else(|| {
            let namespace = DlNamespace(self.next);
            self.next += 1;
            namespace
        });

        self.by_pipeline.insert(
            key,
            BuilderState {
                namespace,
                builder: None,
                expected_build: BuildId(0),
                displaced: None,
            },
        );
        self.bump_generation(namespace);
        (namespace, true)
    }

    /// Work out what this display list's delta means for the stores, and record
    /// that it arrived. A couple of comparisons per display list, which is what
    /// stands between us and the failure modes in
    /// `doc/dl-builder-interning.md`: a lost add panics far from the cause, a
    /// lost remove leaks silently, and a duplicate add corrupts a live slot.
    ///
    /// Builds are numbered consecutively even when they intern nothing, so a gap
    /// in the stream we are following means a delta was dropped, repeated or
    /// reordered. That is always a bug, so it panics.
    ///
    /// A delta from a *different* builder means something else: slot numbering
    /// is per builder and restarts at zero, so the two cannot share a slot
    /// space. See `DeltaAction` for the three ways that resolves.
    pub fn check_delta(
        &mut self,
        document_id: DocumentId,
        pipeline_id: PipelineId,
        builder: BuilderId,
        build: BuildId,
        is_empty: bool,
    ) -> DeltaAction {
        let state = self
            .by_pipeline
            .get_mut(&(document_id, pipeline_id))
            .expect("delta for a pipeline with no namespace");

        // Set when the arm below decides this is a takeover; the generation bump
        // needs `self` again, which `state` is still borrowing.
        let mut reset_namespace = None;

        let action = match state.builder {
            Some(known) if known != builder => {
                // An empty delta claims nothing, so leave the stream with the
                // builder that owns it: this is Gecko clearing a pipeline
                // through a throwaway builder.
                if is_empty {
                    return DeltaAction::Ignore;
                }

                assert_ne!(
                    state.displaced,
                    Some(builder),
                    "two display list builders alternating on {:?}",
                    pipeline_id,
                );

                debug!(
                    "dl interning: builder change on {:?}, dropping its interned items",
                    pipeline_id,
                );
                state.displaced = Some(known);
                state.builder = Some(builder);
                reset_namespace = Some(state.namespace);
                DeltaAction::Reset
            }
            Some(_) => {
                assert_eq!(
                    build, state.expected_build,
                    "display list interning delta out of sequence for {:?}",
                    pipeline_id,
                );
                DeltaAction::Apply
            }
            None => {
                // Nothing to be contiguous with yet, so adopt this builder and
                // take its number as the start of the stream rather than
                // demanding zero - its earlier builds may have interned
                // nothing. An empty delta claims nothing, since it might be a
                // throwaway builder's.
                if is_empty {
                    return DeltaAction::Ignore;
                }
                state.builder = Some(builder);
                DeltaAction::Apply
            }
        };

        state.expected_build = BuildId(build.0 + 1);

        if let Some(namespace) = reset_namespace {
            self.bump_generation(namespace);
        }

        action
    }

    /// Note that `document_id` has removed a pipeline. Its namespace is *not*
    /// released yet: removing a pipeline does not rebuild the scene, so the
    /// render backend goes on drawing one whose primitives still reference it.
    /// Calling `take_removals` at that document's next scene build is what
    /// releases it.
    ///
    /// Only that document's build will do. Another document's scene says
    /// nothing about what this one still references, and the render backend
    /// may be holding this document's ops - including the namespace's open -
    /// until it next swaps a scene, which a release from elsewhere would
    /// overtake.
    pub fn remove_pipeline(&mut self, document_id: DocumentId, pipeline_id: PipelineId) {
        self.pending_removals.insert((document_id, pipeline_id), false);
    }

    /// A document is gone, along with every pipeline it had a namespace for.
    /// As it will build no more scenes, these are released by whichever
    /// document builds next. That is sound because the render backend learns
    /// of the deletion before that build's results, and flushes the ops it was
    /// holding for the document first.
    pub fn remove_document(&mut self, document_id: DocumentId) {
        for key in self.by_pipeline.keys() {
            if key.0 == document_id {
                self.pending_removals.insert(*key, true);
            }
        }
    }

    /// Release what `document_id` has removed since its last call, along with
    /// the pipelines of deleted documents, and return the namespaces to close.
    ///
    /// The caller must have just built a scene for `document_id`, and that
    /// scene references none of them. Holding the ids until then is also what
    /// keeps invariant 4: a namespace cannot be handed to another pipeline
    /// while a live scene still points into it.
    pub fn take_removals(&mut self, document_id: DocumentId) -> Vec<DlNamespace> {
        let mut namespaces = Vec::new();

        let mut released = Vec::new();
        self.pending_removals.retain(|key, orphaned| {
            let release = *orphaned || key.0 == document_id;
            if release {
                released.push(*key);
            }
            !release
        });

        for key in released {
            if let Some(state) = self.by_pipeline.remove(&key) {
                self.free.push(state.namespace);
                namespaces.push(state.namespace);
            }
        }

        namespaces
    }
}

/// The builder map restricted to one document, for that document's scene
/// builds.
#[derive(Copy, Clone)]
pub struct DlDocumentBuilders<'a> {
    map: &'a DlBuilderMap,
    document_id: DocumentId,
}

impl<'a> DlDocumentBuilders<'a> {
    /// The namespace of a pipeline that is being built into a scene, and its
    /// generation, so it must have one: a namespace is allocated on the
    /// pipeline's first display list and released at the same point the scene
    /// stops referencing it.
    pub fn expect(&self, pipeline_id: PipelineId) -> (DlNamespace, u32) {
        let namespace = self
            .map
            .get(self.document_id, pipeline_id)
            .unwrap_or_else(|| panic!("no namespace for {:?} in {:?}", pipeline_id, self.document_id));
        (namespace, self.map.generations[namespace.0 as usize])
    }
}

/// One step of the resolved delta a scene builder hands to a store.
///
/// A namespace's lifetime rides the same op list as its contents so a store
/// cannot see the two out of order: `Open` before anything is put in a freshly
/// allocated namespace, `Close` at the point the scene stops referencing the
/// pipeline. `T` is the store's own view of an interned item, derived from the
/// content key by whoever built the list.
#[derive(Clone)]
pub enum DlOp<T> {
    Open(DlNamespace),
    Insert {
        namespace: DlNamespace,
        slot: u32,
        value: T,
    },
    Remove {
        namespace: DlNamespace,
        slot: u32,
    },
    Close(DlNamespace),
}

/// Turn one type's delta into store ops. Adds before removes, which cannot
/// conflict within a delta: a slot this build frees is only ever handed out
/// again by a later one.
pub fn resolve_into<K, T>(
    ops: &mut Vec<DlOp<T>>,
    namespace: DlNamespace,
    delta: &api::interning::InternOps<K>,
    mut resolve: impl FnMut(&K) -> T,
) {
    for add in &delta.adds {
        ops.push(DlOp::Insert {
            namespace,
            slot: add.slot,
            value: resolve(&add.key),
        });
    }

    for &slot in &delta.removes {
        ops.push(DlOp::Remove { namespace, slot });
    }
}

/// Adds and removes in one resolved op list. A close is not counted: it drops a
/// whole namespace at once, and totalling its slots would cost a lookup per op
/// for a number nothing acts on.
pub fn count_dl_ops<T>(ops: &[DlOp<T>]) -> (usize, usize) {
    let mut insertions = 0;
    let mut removals = 0;

    for op in ops {
        match op {
            DlOp::Insert { .. } => insertions += 1,
            DlOp::Remove { .. } => removals += 1,
            DlOp::Open(..) | DlOp::Close(..) => {}
        }
    }

    (insertions, removals)
}

/// Mints the `ItemUid`s for one set of follower stores. Shared by every store
/// in the set rather than kept per store, because the tile cache compares uids
/// without regard to which store they came from; see `DlStore::uid`.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(Clone, Default)]
pub struct DlUids {
    next: u64,
}

impl DlUids {
    /// Set on every minted uid. `intern::Handle::uid` packs a 32-bit index above
    /// a 32-bit epoch, so it only reaches this bit past 2^31 entries, which
    /// keeps the two schemes apart where uids from both meet in a tile.
    const TAG: u64 = 1 << 63;

    fn mint(&mut self) -> ItemUid {
        let uid = ItemUid::from_counter(Self::TAG | self.next);
        self.next += 1;
        uid
    }
}

/// Two-level store fed by the content interner's delta: an outer array indexed
/// by namespace, an inner array indexed by content slot.
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[cfg_attr(
    any(feature = "capture", feature = "replay"),
    serde(bound(serialize = "T: serde::Serialize", deserialize = "T: serde::Deserialize<'de>"))
)]
pub struct DlStore<K, T> {
    /// `None` is a namespace that was never opened or has been closed. Closing
    /// drops a pipeline's entire slot array at once, which is what handles
    /// teardown - a dead content process never sends removes of its own.
    namespaces: Vec<Option<Vec<Option<Entry<T>>>>>,
    /// How many times each namespace index has been opened. Kept in step with
    /// the allocator's count so a handle's generation can be checked against
    /// it; see `DlHandle::generation`. Debug only, like the field it checks.
    #[cfg(debug_assertions)]
    generations: Vec<u32>,
    _marker: PhantomData<K>,
}

#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[derive(MallocSizeOf, Clone)]
struct Entry<T> {
    /// See `DlStore::uid`. Assigned when the entry is inserted and fixed for as
    /// long as it lives.
    uid: ItemUid,
    value: T,
}

// Hand-written rather than derived, which would demand `K: Clone` for the
// marker.
impl<K, T: Clone> Clone for DlStore<K, T> {
    fn clone(&self) -> Self {
        DlStore {
            namespaces: self.namespaces.clone(),
            #[cfg(debug_assertions)]
            generations: self.generations.clone(),
            _marker: PhantomData,
        }
    }
}

impl<K, T> Default for DlStore<K, T> {
    fn default() -> Self {
        DlStore {
            namespaces: Vec::new(),
            #[cfg(debug_assertions)]
            generations: Vec::new(),
            _marker: PhantomData,
        }
    }
}

impl<K, T: malloc_size_of::MallocSizeOf> malloc_size_of::MallocSizeOf for DlStore<K, T> {
    fn size_of(&self, ops: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        // Hand-written rather than derived, which would demand `K: MallocSizeOf`
        // for the marker.
        self.namespaces.size_of(ops)
    }
}

impl<K, T> DlStore<K, T> {
    /// Number of live entries across every open namespace.
    pub fn len(&self) -> usize {
        self.namespaces
            .iter()
            .filter_map(|slots| slots.as_ref())
            .map(|slots| slots.iter().filter(|slot| slot.is_some()).count())
            .sum()
    }

    /// Replay a resolved delta. The only way a store changes: it is applied in
    /// the order given and never consulted about what it is asked to do.
    pub fn apply(&mut self, ops: Vec<DlOp<T>>, uids: &mut DlUids) {
        for op in ops {
            match op {
                DlOp::Open(namespace) => self.open(namespace),
                DlOp::Insert { namespace, slot, value } => {
                    self.insert(namespace, slot, value, uids)
                }
                DlOp::Remove { namespace, slot } => self.remove(namespace, slot),
                DlOp::Close(namespace) => self.close(namespace),
            }
        }
    }

    pub fn open(&mut self, namespace: DlNamespace) {
        let index = namespace.0 as usize;
        if index >= self.namespaces.len() {
            self.namespaces.resize_with(index + 1, || None);
        }
        assert!(
            self.namespaces[index].is_none(),
            "namespace {} opened twice",
            namespace.0,
        );
        self.namespaces[index] = Some(Vec::new());

        #[cfg(debug_assertions)]
        {
            if index >= self.generations.len() {
                self.generations.resize(index + 1, 0);
            }
            self.generations[index] += 1;
        }
    }

    pub fn close(&mut self, namespace: DlNamespace) {
        let slots = self
            .namespaces
            .get_mut(namespace.0 as usize)
            .filter(|slots| slots.is_some())
            .unwrap_or_else(|| panic!("namespace {} is not open", namespace.0));
        *slots = None;
    }

    /// Bring the set of open namespaces into line with a builder map restored
    /// from the same capture. The two are snapshotted by different threads at
    /// different moments, so the store can lag the map by the display lists
    /// that were in flight: namespaces the map has and the store lacks are
    /// opened empty, ones only the store has are closed, and the generations
    /// are taken from the map. Entries cannot be recovered the same way, since
    /// their keys live in the content process.
    pub fn reconcile(&mut self, builders: &DlBuilderMap) {
        let live: Vec<(DlNamespace, u32)> = builders.live_namespaces().collect();

        for (index, slots) in self.namespaces.iter_mut().enumerate() {
            if slots.is_some() && !live.iter().any(|(ns, _)| ns.0 as usize == index) {
                *slots = None;
            }
        }

        for (namespace, generation) in live {
            let index = namespace.0 as usize;
            if index >= self.namespaces.len() {
                self.namespaces.resize_with(index + 1, || None);
            }
            if self.namespaces[index].is_none() {
                self.namespaces[index] = Some(Vec::new());
            }

            #[cfg(debug_assertions)]
            {
                if index >= self.generations.len() {
                    self.generations.resize(index + 1, 0);
                }
                self.generations[index] = generation;
            }
            #[cfg(not(debug_assertions))]
            let _ = generation;
        }
    }

    pub fn insert(&mut self, namespace: DlNamespace, slot: u32, value: T, uids: &mut DlUids) {
        let uid = uids.mint();

        let slots = self.slots_mut(namespace);
        let slot = slot as usize;
        if slot >= slots.len() {
            slots.resize_with(slot + 1, || None);
        }
        assert!(
            slots[slot].is_none(),
            "add for an occupied slot {}:{}",
            namespace.0,
            slot,
        );
        slots[slot] = Some(Entry { uid, value });
    }

    pub fn remove(&mut self, namespace: DlNamespace, slot: u32) {
        let slots = self.slots_mut(namespace);
        let removed = slots
            .get_mut(slot as usize)
            .and_then(|entry| entry.take());
        assert!(
            removed.is_some(),
            "remove for an empty slot {}:{}",
            namespace.0,
            slot,
        );
    }

    pub fn get(&self, handle: DlHandle<K>) -> Option<&T> {
        self.entry(handle).map(|entry| &entry.value)
    }

    /// Identity of an entry for tile cache dependency tracking, which needs two
    /// things of it. It must be **stable**: an entry has to keep the same value
    /// for as long as it lives, or every tile invalidates for nothing. And it
    /// must be **distinct**: no two entries may share one, or a tile keeps
    /// rendering what used to be in a recycled slot.
    ///
    /// Nothing about the handle supplies both. `{namespace, slot}` repeats when a
    /// slot is recycled, and adding the content build stamp does not fix it
    /// either: a replacement builder (see `DeltaAction::Reset`) restarts slot and
    /// build numbering, so the first entry of an unrelated display list reuses
    /// the pair. An interner can pack an index with an epoch because it owns the
    /// whole index space; a follower of per-builder slot numbering cannot.
    ///
    /// So the store mints its own instead: a counter, advanced per insert and
    /// never reused. Injective by construction, which a hash of the content would
    /// not be - a collision there would silently leave a tile showing stale
    /// content, and this is not a place to accept that. The cost is that a
    /// takeover re-mints every uid, so its tiles invalidate; that is correct, just
    /// conservative, and a takeover means the content process was replaced.
    ///
    /// Distinct has to hold across stores too, not only within one. A tile's
    /// primitive dependencies carry the uid but not the primitive kind, so a
    /// rectangle that takes the uid of a gradient last drawn in the same place
    /// leaves the gradient on screen. The counter is therefore a `DlUids` that
    /// every store in a set draws from.
    ///
    /// Panics on a handle with no entry, like `Index`: a primitive referencing a
    /// slot no delta has filled is the failure this is meant to surface.
    pub fn uid(&self, handle: DlHandle<K>) -> ItemUid {
        self.entry(handle)
            .unwrap_or_else(|| panic!("bad dl store lookup {:?}", handle))
            .uid
    }

    /// Current occupant count for a namespace; see `DlHandle::generation`.
    #[cfg(debug_assertions)]
    pub fn generation(&self, namespace: DlNamespace) -> u32 {
        self.generations.get(namespace.0 as usize).copied().unwrap_or(0)
    }

    fn entry(&self, handle: DlHandle<K>) -> Option<&Entry<T>> {
        // A handle from before this namespace was recycled would otherwise read
        // whatever now occupies the slot. See `DlHandle::generation`.
        #[cfg(debug_assertions)]
        debug_assert_eq!(
            self.generations.get(handle.namespace.0 as usize).copied(),
            Some(handle.generation),
            "stale handle into a recycled namespace: {:?}",
            handle,
        );

        self.namespaces
            .get(handle.namespace.0 as usize)?
            .as_ref()?
            .get(handle.slot as usize)?
            .as_ref()
    }

    fn slots_mut(&mut self, namespace: DlNamespace) -> &mut Vec<Option<Entry<T>>> {
        self.namespaces
            .get_mut(namespace.0 as usize)
            .and_then(|slots| slots.as_mut())
            .unwrap_or_else(|| panic!("namespace {} is not open", namespace.0))
    }
}

impl<K, T> ops::Index<DlHandle<K>> for DlStore<K, T> {
    type Output = T;
    fn index(&self, handle: DlHandle<K>) -> &T {
        self.get(handle)
            .unwrap_or_else(|| panic!("bad dl store lookup {:?}", handle))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use malloc_size_of::MallocSizeOf;
    use std::mem;

    struct Key;

    fn pipeline(id: u32) -> PipelineId {
        PipelineId(1, id)
    }

    fn doc(id: u32) -> DocumentId {
        DocumentId::new(IdNamespace(1), id)
    }

    fn store() -> DlStore<Key, u32> {
        DlStore::default()
    }

    /// A handle naming the namespace's current occupant, which is what a scene
    /// built now would carry.
    fn h(store: &DlStore<Key, u32>, namespace: DlNamespace, slot: u32) -> DlHandle<Key> {
        DlHandle::new(namespace, store.generation(namespace), slot)
    }

    #[test]
    fn handle_is_two_words() {
        // Two words in release, where it is on the per-primitive path; a debug
        // build carries the namespace generation as well.
        let expected = if cfg!(debug_assertions) { 12 } else { 8 };
        assert_eq!(mem::size_of::<DlHandle<Key>>(), expected, "DlHandle size changed");
    }

    #[test]
    #[should_panic(expected = "stale handle")]
    fn a_handle_into_a_recycled_namespace_panics() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        let stale = h(&store, DlNamespace(0), 0);

        // The pipeline goes away and the index is handed to another one. A
        // retained scene still holding `stale` would otherwise read this
        // pipeline's data as if it were its own.
        store.close(DlNamespace(0));
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 200, &mut uids);

        store.get(stale);
    }

    #[test]
    fn reconcile_matches_open_namespaces_to_the_builder_map() {
        // The store snapshot lags the map by one pipeline's arrival and one
        // pipeline's release.
        let mut alloc = DlBuilderMap::default();
        let stale = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        let kept = alloc.get_or_alloc(doc(1), pipeline(2)).0;

        let mut store = store();
        let mut uids = DlUids::default();
        store.open(stale);
        store.open(kept);
        store.insert(kept, 0, 100, &mut uids);
        let handle = h(&store, kept, 0);

        alloc.remove_pipeline(doc(1), pipeline(1));
        alloc.take_removals(doc(1));
        let fresh = alloc.get_or_alloc(doc(1), pipeline(3)).0;
        assert_eq!(fresh, stale, "expected the released namespace to be reused");
        let late = alloc.get_or_alloc(doc(1), pipeline(4)).0;

        store.reconcile(&alloc);

        assert_eq!(store.get(handle), Some(&100), "a surviving entry was lost");
        assert_eq!(store.len(), 1);
        store.insert(late, 0, 200, &mut uids);
        store.insert(fresh, 0, 300, &mut uids);
        store.close(late);
        store.close(fresh);
        store.close(kept);
        assert_eq!(store.len(), 0);
    }

    #[test]
    fn a_pipeline_keeps_its_namespace_across_display_lists() {
        let mut alloc = DlBuilderMap::default();

        let (first, allocated) = alloc.get_or_alloc(doc(1), pipeline(1));
        assert!(allocated);
        let (second, allocated) = alloc.get_or_alloc(doc(1), pipeline(1));
        assert!(!allocated, "the second display list reallocated the namespace");
        assert_eq!(first, second);
    }

    #[test]
    fn namespaces_are_dense_and_recycled() {
        let mut alloc = DlBuilderMap::default();

        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        let b = alloc.get_or_alloc(doc(1), pipeline(2)).0;
        assert_ne!(a, b);
        assert!(a.0 < 2 && b.0 < 2, "namespaces are not dense: {:?} {:?}", a, b);

        alloc.remove_pipeline(doc(1), pipeline(1));
        assert_eq!(alloc.take_removals(doc(1)), vec![a]);
        assert!(alloc.take_removals(doc(1)).is_empty(), "released twice");

        let c = alloc.get_or_alloc(doc(1), pipeline(3)).0;
        assert_eq!(c, a, "the released namespace was not reused");
    }

    #[test]
    fn a_removed_pipeline_holds_its_namespace_until_a_scene_is_built() {
        let mut alloc = DlBuilderMap::default();

        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        alloc.remove_pipeline(doc(1), pipeline(1));

        // Removing a pipeline does not rebuild the scene, so until one is built
        // the render backend is still drawing primitives that point into `a`.
        // Handing it to another pipeline now would alias them.
        let b = alloc.get_or_alloc(doc(1), pipeline(2)).0;
        assert_ne!(b, a, "a namespace was reused while a live scene referenced it");

        assert_eq!(alloc.take_removals(doc(1)), vec![a]);
        let c = alloc.get_or_alloc(doc(1), pipeline(3)).0;
        assert_eq!(c, a, "the released namespace was not reused");
    }

    #[test]
    fn a_pipeline_that_comes_back_keeps_its_namespace() {
        let mut alloc = DlBuilderMap::default();

        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        alloc.remove_pipeline(doc(1), pipeline(1));

        // A new display list before the release lands means the pipeline is
        // live again on the same namespace, so the release is off - closing it
        // would drop entries the new list still references.
        let (again, allocated) = alloc.get_or_alloc(doc(1), pipeline(1));
        assert_eq!(again, a);
        assert!(!allocated, "the namespace was reallocated rather than kept");

        assert!(alloc.take_removals(doc(1)).is_empty(), "a live pipeline was released");
    }

    #[test]
    fn only_the_removing_document_releases_a_pipeline() {
        let mut alloc = DlBuilderMap::default();

        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        alloc.remove_pipeline(doc(1), pipeline(1));

        // Another document's scene says nothing about what doc(1) still draws,
        // and the render backend may still be holding doc(1)'s open of `a`.
        assert!(alloc.take_removals(doc(2)).is_empty(), "released by another document");
        assert_eq!(alloc.take_removals(doc(1)), vec![a]);
    }

    #[test]
    fn a_deleted_documents_removals_are_released_by_any_document() {
        let mut alloc = DlBuilderMap::default();

        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        let b = alloc.get_or_alloc(doc(1), pipeline(2)).0;
        alloc.remove_pipeline(doc(1), pipeline(1));
        alloc.remove_document(doc(1));

        let mut released = alloc.take_removals(doc(2));
        released.sort_by_key(|namespace| namespace.0);
        assert_eq!(released, vec![a, b]);
    }

    #[test]
    fn a_pipeline_moved_to_another_document_gets_its_own_namespace() {
        let mut alloc = DlBuilderMap::default();

        let old = alloc.get_or_alloc(doc(1), pipeline(1)).0;

        // A tab moved to another window: its display lists go to doc(2) while
        // doc(1)'s removal, sent at low priority, arrives after them. The
        // render backend may be holding doc(2)'s open, so doc(1)'s release
        // must not touch the namespace doc(2) uses.
        let (new, allocated) = alloc.get_or_alloc(doc(2), pipeline(1));
        assert!(allocated);
        assert_ne!(new, old, "two documents share a namespace");

        alloc.remove_pipeline(doc(1), pipeline(1));
        assert_eq!(alloc.take_removals(doc(1)), vec![old]);
        assert_eq!(alloc.document(doc(2)).expect(pipeline(1)).0, new);
        assert!(alloc.take_removals(doc(2)).is_empty(), "the moved pipeline was released");
    }

    #[test]
    fn a_deleted_document_releases_pipelines_outside_its_scene() {
        let mut alloc = DlBuilderMap::default();

        // A display list that never made it into a scene, such as one for a
        // document without a root pipeline yet.
        let a = alloc.get_or_alloc(doc(1), pipeline(1)).0;
        let b = alloc.get_or_alloc(doc(2), pipeline(1)).0;
        alloc.remove_document(doc(1));

        assert_eq!(alloc.take_removals(doc(3)), vec![a]);
        assert_eq!(alloc.document(doc(2)).expect(pipeline(1)).0, b);
    }

    fn builder(id: u64) -> BuilderId {
        BuilderId(id)
    }

    /// A map with one pipeline whose stream has reached the given build.
    fn tracking(build: u32) -> DlBuilderMap {
        let mut map = DlBuilderMap::default();
        map.get_or_alloc(doc(1), pipeline(1));
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(build), false), DeltaAction::Apply);
        map
    }

    #[test]
    fn a_contiguous_stream_is_accepted() {
        let mut map = tracking(0);

        // Including builds that interned nothing: those still advance the
        // builder's counter, so they must advance ours.
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(1), true), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(2), false), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(3), true), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(4), false), DeltaAction::Apply);
    }

    #[test]
    fn a_stream_may_start_partway_in() {
        // Only a delta that interns something claims the stream, so the first
        // one we track need not be build zero.
        let mut map = tracking(9);
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(10), false), DeltaAction::Apply);
    }

    #[test]
    #[should_panic(expected = "out of sequence")]
    fn a_dropped_delta_panics() {
        let mut map = tracking(0);
        map.check_delta(doc(1), pipeline(1), builder(7), BuildId(2), false);
    }

    #[test]
    #[should_panic(expected = "out of sequence")]
    fn a_repeated_delta_panics() {
        let mut map = tracking(0);
        map.check_delta(doc(1), pipeline(1), builder(7), BuildId(1), false);
        map.check_delta(doc(1), pipeline(1), builder(7), BuildId(1), false);
    }

    #[test]
    fn a_second_builder_with_content_takes_over() {
        let mut map = tracking(0);

        // A client that builds every display list with a fresh builder, or a
        // replaced content process. The new builder's slot numbering starts
        // again, so the old entries have to be dropped rather than merged.
        assert_eq!(
            map.check_delta(doc(1), pipeline(1), builder(8), BuildId(0), false),
            DeltaAction::Reset,
        );
        assert_eq!(
            map.check_delta(doc(1), pipeline(1), builder(8), BuildId(1), false),
            DeltaAction::Apply,
        );
    }

    #[test]
    #[should_panic(expected = "alternating")]
    fn two_builders_alternating_panics() {
        let mut map = tracking(0);

        // Taking over is only sound because the displaced builder is gone. One
        // that comes back would reference entries the takeover dropped and never
        // re-mint them, so the store lookup would fail far from here.
        map.check_delta(doc(1), pipeline(1), builder(8), BuildId(0), false);
        map.check_delta(doc(1), pipeline(1), builder(7), BuildId(1), false);
    }

    #[test]
    fn a_second_builder_sending_nothing_is_ignored() {
        let mut map = tracking(0);

        // Gecko's clear-display-list paths: a throwaway builder, an empty list,
        // and so an empty delta. It must not disturb the real stream.
        assert_eq!(
            map.check_delta(doc(1), pipeline(1), builder(8), BuildId(0), true),
            DeltaAction::Ignore,
        );
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(1), false), DeltaAction::Apply);
    }

    #[test]
    fn streams_are_tracked_per_pipeline() {
        let mut map = DlBuilderMap::default();
        map.get_or_alloc(doc(1), pipeline(1));
        map.get_or_alloc(doc(1), pipeline(2));

        // Two builders at different points in their own streams.
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(4), false), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(2), builder(8), BuildId(0), false), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(1), builder(7), BuildId(5), false), DeltaAction::Apply);
        assert_eq!(map.check_delta(doc(1), pipeline(2), builder(8), BuildId(1), false), DeltaAction::Apply);
    }

    #[test]
    fn applying_a_delta_replays_it_in_order() {
        let mut store = store();
        let mut uids = DlUids::default();

        // One pipeline's whole life: opened on its first display list, filled,
        // a slot recycled by a later build, then closed when it goes away.
        store.apply(vec![
            DlOp::Open(DlNamespace(0)),
            DlOp::Insert { namespace: DlNamespace(0), slot: 0, value: 100 },
            DlOp::Insert { namespace: DlNamespace(0), slot: 1, value: 200 },
        ], &mut uids);
        assert_eq!(store[h(&store, DlNamespace(0), 1)], 200);

        store.apply(vec![
            DlOp::Remove { namespace: DlNamespace(0), slot: 1 },
            DlOp::Close(DlNamespace(0)),
            DlOp::Open(DlNamespace(0)),
            DlOp::Insert { namespace: DlNamespace(0), slot: 1, value: 300 },
        ], &mut uids);
        assert_eq!(store.get(h(&store, DlNamespace(0), 0)), None, "close kept a slot");
        assert_eq!(store[h(&store, DlNamespace(0), 1)], 300);
    }

    #[test]
    fn slots_are_addressed_per_namespace() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.open(DlNamespace(1));

        // The same slot number in two namespaces is two different items.
        store.insert(DlNamespace(0), 3, 100, &mut uids);
        store.insert(DlNamespace(1), 3, 200, &mut uids);

        assert_eq!(store[h(&store, DlNamespace(0), 3)], 100);
        assert_eq!(store[h(&store, DlNamespace(1), 3)], 200);
    }

    #[test]
    fn closing_drops_every_slot_in_the_namespace() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        store.insert(DlNamespace(0), 7, 100, &mut uids);

        store.close(DlNamespace(0));
        assert_eq!(store.get(h(&store, DlNamespace(0), 0)), None);

        // Reopening is a fresh, empty slot space, not the old one.
        store.open(DlNamespace(0));
        assert_eq!(store.get(h(&store, DlNamespace(0), 7)), None);
    }

    /// Drive the allocator and a store together through a pipeline going away
    /// and its namespace being handed to a different one, which is the sequence
    /// the two halves have to agree on.
    fn recycle_across_pipelines() -> (DlBuilderMap, DlStore<Key, u32>, DlNamespace) {
        let mut map = DlBuilderMap::default();
        let mut store = store();
        let mut uids = DlUids::default();

        let (ns_a, allocated) = map.get_or_alloc(doc(1), pipeline(1));
        assert!(allocated);
        store.open(ns_a);
        store.insert(ns_a, 0, 100, &mut uids);

        // Removed, then released at the next scene build.
        map.remove_pipeline(doc(1), pipeline(1));
        for namespace in map.take_removals(doc(1)) {
            store.close(namespace);
        }

        let (ns_b, allocated) = map.get_or_alloc(doc(1), pipeline(2));
        assert_eq!(ns_b, ns_a, "the index was not recycled");
        assert!(allocated, "a recycled namespace must still be opened");
        store.open(ns_b);
        store.insert(ns_b, 0, 200, &mut uids);

        (map, store, ns_b)
    }

    #[test]
    fn a_recycled_namespace_serves_its_new_pipeline() {
        let (map, store, namespace) = recycle_across_pipelines();

        // Same index, same slot, but the second pipeline's data - the first
        // pipeline's entry went with the close.
        let (_, generation) = map.document(doc(1)).expect(pipeline(2));
        assert_eq!(store[DlHandle::new(namespace, generation, 0)], 200);
    }

    #[test]
    #[should_panic(expected = "stale handle")]
    fn a_handle_from_the_previous_occupant_panics() {
        let mut map = DlBuilderMap::default();
        let mut store = store();
        let mut uids = DlUids::default();

        let (ns_a, _) = map.get_or_alloc(doc(1), pipeline(1));
        let stale = DlHandle::new(ns_a, map.document(doc(1)).expect(pipeline(1)).1, 0);
        store.open(ns_a);
        store.insert(ns_a, 0, 100, &mut uids);

        map.remove_pipeline(doc(1), pipeline(1));
        for namespace in map.take_removals(doc(1)) {
            store.close(namespace);
        }

        let (ns_b, _) = map.get_or_alloc(doc(1), pipeline(2));
        store.open(ns_b);
        store.insert(ns_b, 0, 200, &mut uids);

        // A scene retained from before the swap would read pipeline 2's run as
        // if it were pipeline 1's.
        store.get(stale);
    }

    #[test]
    fn a_uid_is_fixed_for_an_entry_and_never_reused() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.open(DlNamespace(1));

        store.insert(DlNamespace(0), 0, 100, &mut uids);
        let first = store.uid(h(&store, DlNamespace(0), 0));

        // Fixed while the entry lives: a tile must not invalidate for an entry
        // that has not changed.
        store.insert(DlNamespace(0), 1, 200, &mut uids);
        assert_eq!(store.uid(h(&store, DlNamespace(0), 0)), first);

        // Never handed out twice, whether the slot is recycled or the whole
        // namespace is. Either would otherwise leave a tile rendering what used
        // to be there.
        store.remove(DlNamespace(0), 0);
        store.insert(DlNamespace(0), 0, 300, &mut uids);
        assert_ne!(store.uid(h(&store, DlNamespace(0), 0)), first);

        store.close(DlNamespace(0));
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        assert_ne!(store.uid(h(&store, DlNamespace(0), 0)), first);

        // And not shared across namespaces, which per-builder slot numbering
        // cannot distinguish on its own.
        store.insert(DlNamespace(1), 0, 100, &mut uids);
        assert_ne!(
            store.uid(h(&store, DlNamespace(1), 0)),
            store.uid(h(&store, DlNamespace(0), 0)),
        );
    }

    #[test]
    fn a_uid_is_not_shared_across_stores() {
        struct OtherKey;
        let mut rects: DlStore<Key, u32> = DlStore::default();
        let mut gradients: DlStore<OtherKey, u32> = DlStore::default();
        let mut uids = DlUids::default();
        rects.open(DlNamespace(0));
        gradients.open(DlNamespace(0));

        // The same namespace and slot in two stores, as a gradient followed in
        // the next display list by a rectangle in its place. A shared uid would
        // let the tile keep the gradient.
        gradients.insert(DlNamespace(0), 0, 100, &mut uids);
        rects.insert(DlNamespace(0), 0, 200, &mut uids);

        assert_ne!(
            gradients.uid(DlHandle::new(DlNamespace(0), gradients.generation(DlNamespace(0)), 0)),
            rects.uid(h(&rects, DlNamespace(0), 0)),
        );
    }

    #[test]
    fn a_uid_is_apart_from_interner_handle_uids() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);

        // An interner's first handle has a uid equal to the epoch it was
        // interned in, which is where a bare counter starts too.
        let uid = store.uid(h(&store, DlNamespace(0), 0)).get_uid();
        assert_ne!(uid & DlUids::TAG, 0, "{:#x} can collide with an interner uid", uid);
    }

    #[test]
    fn a_store_reports_its_live_entries_and_its_memory() {
        use malloc_size_of::MallocSizeOfOps;

        // Stand-in for the platform's malloc_usable_size: any non-zero answer
        // for a live block is enough to tell a real measurement from a no-op.
        extern "C" fn block_size(_ptr: *const std::os::raw::c_void) -> usize {
            8
        }

        let mut store = store();
        let mut uids = DlUids::default();
        let mut ops = MallocSizeOfOps::new(block_size, None);
        assert_eq!(store.len(), 0);

        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        store.insert(DlNamespace(0), 4, 200, &mut uids);
        assert_eq!(store.len(), 2, "gaps between slots are not entries");

        // The heap the entries sit in has to show up, or the memory report is a
        // constant zero - which is what a hand-written `size_of` gets wrong.
        assert!(store.size_of(&mut ops) > 0, "store reported no memory");

        store.remove(DlNamespace(0), 0);
        assert_eq!(store.len(), 1);

        store.close(DlNamespace(0));
        assert_eq!(store.len(), 0, "a closed namespace still counted");
    }

    #[test]
    fn a_removed_slot_is_reusable_but_not_readable() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 2, 100, &mut uids);
        store.remove(DlNamespace(0), 2);

        assert_eq!(store.get(h(&store, DlNamespace(0), 2)), None);
        store.insert(DlNamespace(0), 2, 200, &mut uids);
        assert_eq!(store[h(&store, DlNamespace(0), 2)], 200);
    }

    #[test]
    #[should_panic(expected = "add for an occupied slot")]
    fn a_duplicate_add_panics() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        store.insert(DlNamespace(0), 0, 200, &mut uids);
    }

    #[test]
    #[should_panic(expected = "remove for an empty slot")]
    fn a_duplicate_remove_panics() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
        store.remove(DlNamespace(0), 0);
        store.remove(DlNamespace(0), 0);
    }

    #[test]
    #[should_panic(expected = "is not open")]
    fn adding_to_a_closed_namespace_panics() {
        let mut store = store();
        let mut uids = DlUids::default();
        store.open(DlNamespace(0));
        store.close(DlNamespace(0));
        store.insert(DlNamespace(0), 0, 100, &mut uids);
    }
}
