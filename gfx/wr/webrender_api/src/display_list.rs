/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use euclid::SideOffsets2D;
use peek_poke::{ensure_red_zone, peek_from_slice, poke_extend_vec, strip_red_zone};
use peek_poke::{poke_inplace_slice, poke_into_vec, Poke};
#[cfg(feature = "deserialize")]
use serde::de::Deserializer;
#[cfg(feature = "serialize")]
use serde::ser::Serializer;
use serde::{Deserialize, Serialize};
use std::io::Write;
use std::marker::PhantomData;
use std::ops::Range;
use std::mem;
use std::collections::HashMap;
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
// local imports
use crate::display_item as di;
use crate::{APZScrollGeneration, HasScrollLinkedEffect, PipelineId, PropertyBinding};
use crate::gradient_builder::GradientBuilder;
use crate::color::ColorF;
use crate::font::{FontInstanceKey, GlyphInstance, GlyphOptions};
use crate::image::{ColorDepth, ImageKey};
use crate::units::*;


// We don't want to push a long text-run. If a text-run is too long, split it into several parts.
// This needs to be set to (renderer::MAX_VERTEX_TEXTURE_WIDTH - VECS_PER_TEXT_RUN) * 2
pub const MAX_TEXT_RUN_LENGTH: usize = 2040;

// See ROOT_REFERENCE_FRAME_SPATIAL_ID and ROOT_SCROLL_NODE_SPATIAL_ID
// TODO(mrobinson): It would be a good idea to eliminate the root scroll frame which is only
// used by Servo.
const FIRST_SPATIAL_NODE_INDEX: usize = 2;

// See ROOT_SCROLL_NODE_SPATIAL_ID
const FIRST_CLIP_NODE_INDEX: usize = 1;

#[derive(Debug, Copy, Clone, PartialEq)]
enum BuildState {
    Idle,
    Build,
}

#[repr(C)]
#[derive(Debug, Deserialize, Eq, Hash, PartialEq, Serialize)]
pub struct ItemRange<'a, T> {
    bytes: &'a [u8],
    _boo: PhantomData<T>,
}

impl<'a, T> Copy for ItemRange<'a, T> {}
impl<'a, T> Clone for ItemRange<'a, T> {
    fn clone(&self) -> Self {
        *self
    }
}

impl<'a, T> Default for ItemRange<'a, T> {
    fn default() -> Self {
        ItemRange {
            bytes: Default::default(),
            _boo: PhantomData,
        }
    }
}

impl<'a, T> ItemRange<'a, T> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self {
            bytes,
            _boo: PhantomData
        }
    }

    pub fn is_empty(&self) -> bool {
        // Nothing more than space for a length (0).
        self.bytes.len() <= mem::size_of::<usize>()
    }

    pub fn bytes(&self) -> &[u8] {
        self.bytes
    }
}

impl<'a, T: Default> ItemRange<'a, T> {
    pub fn iter(&self) -> AuxIter<'a, T> {
        AuxIter::new(T::default(), self.bytes)
    }
}

impl<'a, T> IntoIterator for ItemRange<'a, T>
where
    T: Copy + Default + peek_poke::Peek,
{
    type Item = T;
    type IntoIter = AuxIter<'a, T>;
    fn into_iter(self) -> Self::IntoIter {
        self.iter()
    }
}

#[derive(Copy, Clone)]
pub struct TempFilterData<'a> {
    pub func_types: ItemRange<'a, di::ComponentTransferFuncType>,
    pub r_values: ItemRange<'a, f32>,
    pub g_values: ItemRange<'a, f32>,
    pub b_values: ItemRange<'a, f32>,
    pub a_values: ItemRange<'a, f32>,
}

#[derive(Default, Clone)]
pub struct DisplayListPayload {
    /// Serde encoded bytes. Mostly DisplayItems, but some mixed in slices.
    pub items_data: Vec<u8>,

    /// Serde encoded SpatialTreeItem structs
    pub spatial_tree: Vec<u8>,
}

impl DisplayListPayload {
    fn default() -> Self {
        DisplayListPayload {
            items_data: Vec::new(),
            spatial_tree: Vec::new(),
        }
    }

    fn new(capacity: DisplayListCapacity) -> Self {
        let mut payload = Self::default();

        // We can safely ignore the preallocations failing, since we aren't
        // certain about how much memory we need, and this gives a chance for
        // the memory pressure events to run.
        if payload.items_data.try_reserve(capacity.items_size).is_err() {
            return Self::default();
        }
        if payload.spatial_tree.try_reserve(capacity.spatial_tree_size).is_err() {
            return Self::default();
        }
        payload
    }

    fn clear(&mut self) {
        self.items_data.clear();
        self.spatial_tree.clear();
    }

    fn size_in_bytes(&self) -> usize {
        self.items_data.len() +
        self.spatial_tree.len()
    }

    #[cfg(feature = "serialize")]
    fn create_debug_spatial_tree_items(&self) -> Vec<di::SpatialTreeItem> {
        let mut items = Vec::new();

        iter_spatial_tree(&self.spatial_tree, |item| {
            items.push(*item);
        });

        items
    }
}

impl MallocSizeOf for DisplayListPayload {
    fn size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        self.items_data.size_of(ops) +
        self.spatial_tree.size_of(ops)
    }
}

/// A display list.
#[derive(Default, Clone)]
pub struct BuiltDisplayList {
    payload: DisplayListPayload,
    descriptor: BuiltDisplayListDescriptor,
}

impl MallocSizeOf for BuiltDisplayList {
    fn size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        self.payload.size_of(ops)
    }
}

#[repr(C)]
#[derive(Copy, Clone, Default, Deserialize, Serialize)]
pub enum GeckoDisplayListType {
    #[default]
    None,
    Partial(f64),
    Full(f64),
}

/// Describes the memory layout of a display list.
///
/// A display list consists of some number of display list items, followed by a number of display
/// items.
#[repr(C)]
#[derive(Copy, Clone, Default, Deserialize, Serialize)]
pub struct BuiltDisplayListDescriptor {
    /// Gecko specific information about the display list.
    gecko_display_list_type: GeckoDisplayListType,
    /// The first IPC time stamp: before any work has been done
    builder_start_time: u64,
    /// The second IPC time stamp: after serialization
    builder_finish_time: u64,
    /// The third IPC time stamp: just before sending
    send_start_time: u64,
    /// The amount of clipping nodes created while building this display list.
    total_clip_nodes: usize,
    /// The amount of spatial nodes created while building this display list.
    total_spatial_nodes: usize,
    /// Coordinates in this display list that were not whole app units on the grid
    /// the builder was given. Normalization by the accumulated external scroll
    /// offset is exact only for coordinates on that grid, so a non-zero value here
    /// means some of this list's positions will drift with the scroll offset.
    /// Reported as a profiler counter rather than asserted, since it is a producer
    /// bug rather than a WebRender one. See bug 2059570.
    pub off_grid_coords: u32,
}


/// A debug (human-readable) representation of a built display list that
/// can be used for capture and replay.
#[cfg(any(feature = "serialize", feature = "deserialize"))]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "deserialize", derive(Deserialize))]
struct DisplayListCapture {
    display_items: Vec<di::DebugDisplayItem>,
    spatial_tree_items: Vec<di::SpatialTreeItem>,
    descriptor: BuiltDisplayListDescriptor,
}

#[cfg(feature = "serialize")]
impl Serialize for BuiltDisplayList {
    fn serialize<S: Serializer>(
        &self,
        serializer: S
    ) -> Result<S::Ok, S::Error> {
        let display_items = BuiltDisplayList::create_debug_display_items(self.iter());
        let spatial_tree_items = self.payload.create_debug_spatial_tree_items();

        let dl = DisplayListCapture {
            display_items,
            spatial_tree_items,
            descriptor: self.descriptor,
        };

        dl.serialize(serializer)
    }
}

#[cfg(feature = "deserialize")]
impl<'de> Deserialize<'de> for BuiltDisplayList {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: Deserializer<'de>,
    {
        use crate::display_item::DisplayItem as Real;
        use crate::display_item::DebugDisplayItem as Debug;

        let capture = DisplayListCapture::deserialize(deserializer)?;

        let mut spatial_tree = Vec::new();
        for item in capture.spatial_tree_items {
            poke_into_vec(&item, &mut spatial_tree);
        }
        ensure_red_zone::<di::SpatialTreeItem>(&mut spatial_tree);

        let mut items_data = Vec::new();
        let mut temp = Vec::new();
        for complete in capture.display_items {
            let item = match complete {
                Debug::ClipChain(v, clip_chain_ids) => {
                    DisplayListBuilder::push_iter_impl(&mut temp, clip_chain_ids);
                    Real::ClipChain(v)
                }
                Debug::Text(v, glyphs) => {
                    DisplayListBuilder::push_iter_impl(&mut temp, glyphs);
                    Real::Text(v)
                },
                Debug::Iframe(v) => {
                    Real::Iframe(v)
                }
                Debug::PushReferenceFrame(v) => {
                    Real::PushReferenceFrame(v)
                }
                Debug::SetFilterOps(filters) => {
                    DisplayListBuilder::push_iter_impl(&mut temp, filters);
                    Real::SetFilterOps
                },
                Debug::SetFilterData(filter_data) => {
                    let func_types: Vec<di::ComponentTransferFuncType> =
                        [filter_data.func_r_type,
                         filter_data.func_g_type,
                         filter_data.func_b_type,
                         filter_data.func_a_type].to_vec();
                    DisplayListBuilder::push_iter_impl(&mut temp, func_types);
                    DisplayListBuilder::push_iter_impl(&mut temp, filter_data.r_values);
                    DisplayListBuilder::push_iter_impl(&mut temp, filter_data.g_values);
                    DisplayListBuilder::push_iter_impl(&mut temp, filter_data.b_values);
                    DisplayListBuilder::push_iter_impl(&mut temp, filter_data.a_values);
                    Real::SetFilterData
                },
                Debug::SetGradientStops(stops) => {
                    DisplayListBuilder::push_iter_impl(&mut temp, stops);
                    Real::SetGradientStops
                },
                Debug::SetPoints(points) => {
                    DisplayListBuilder::push_iter_impl(&mut temp, points);
                    Real::SetPoints
                },
                Debug::RectClip(v) => Real::RectClip(v),
                Debug::RoundedRectClip(v) => Real::RoundedRectClip(v),
                Debug::ImageMaskClip(v) => Real::ImageMaskClip(v),
                Debug::Rectangle(v) => Real::Rectangle(v),
                Debug::HitTest(v) => Real::HitTest(v),
                Debug::Line(v) => Real::Line(v),
                Debug::Image(v) => Real::Image(v),
                Debug::RepeatingImage(v) => Real::RepeatingImage(v),
                Debug::YuvImage(v) => Real::YuvImage(v),
                Debug::Border(v) => Real::Border(v),
                Debug::BoxShadow(v) => Real::BoxShadow(v),
                Debug::Gradient(v) => Real::Gradient(v),
                Debug::RadialGradient(v) => Real::RadialGradient(v),
                Debug::ConicGradient(v) => Real::ConicGradient(v),
                Debug::PushStackingContext(v) => Real::PushStackingContext(v),
                Debug::BackdropFilter(v) => Real::BackdropFilter(v),

                Debug::PopStackingContext => Real::PopStackingContext,
                Debug::PopReferenceFrame => Real::PopReferenceFrame,
                Debug::DebugMarker(val) => Real::DebugMarker(val),
            };
            poke_into_vec(&item, &mut items_data);
            // the aux data is serialized after the item, hence the temporary
            items_data.extend(temp.drain(..));
        }

        // Add `DisplayItem::max_size` zone of zeroes to the end of display list
        // so there is at least this amount available in the display list during
        // serialization.
        ensure_red_zone::<di::DisplayItem>(&mut items_data);

        Ok(BuiltDisplayList {
            descriptor: capture.descriptor,
            payload: DisplayListPayload {
                items_data,
                spatial_tree,
            },
        })
    }
}

pub struct BuiltDisplayListIter<'a> {
    data: &'a [u8],
    cur_item: di::DisplayItem,
    cur_stops: ItemRange<'a, di::GradientStop>,
    cur_glyphs: ItemRange<'a, GlyphInstance>,
    cur_filters: ItemRange<'a, di::FilterOp>,
    cur_filter_data: Vec<TempFilterData<'a>>,
    cur_clip_chain_items: ItemRange<'a, di::ClipId>,
    cur_points: ItemRange<'a, LayoutPoint>,
    peeking: Peek,
    /// Should just be initialized but never populated in release builds
    debug_stats: DebugStats,
}

/// Internal info used for more detailed analysis of serialized display lists
#[allow(dead_code)]
struct DebugStats {
    /// Last address in the buffer we pointed to, for computing serialized sizes
    last_addr: usize,
    stats: HashMap<&'static str, ItemStats>,
}

impl DebugStats {
    #[cfg(feature = "display_list_stats")]
    fn _update_entry(&mut self, name: &'static str, item_count: usize, byte_count: usize) {
        let entry = self.stats.entry(name).or_default();
        entry.total_count += item_count;
        entry.num_bytes += byte_count;
    }

    /// Computes the number of bytes we've processed since we last called
    /// this method, so we can compute the serialized size of a display item.
    #[cfg(feature = "display_list_stats")]
    fn debug_num_bytes(&mut self, data: &[u8]) -> usize {
        let old_addr = self.last_addr;
        let new_addr = data.as_ptr() as usize;
        let delta = new_addr - old_addr;
        self.last_addr = new_addr;

        delta
    }

    /// Logs stats for the last deserialized display item
    #[cfg(feature = "display_list_stats")]
    fn log_item(&mut self, data: &[u8], item: &di::DisplayItem) {
        let num_bytes = self.debug_num_bytes(data);
        self._update_entry(item.debug_name(), 1, num_bytes);
    }

    /// Logs the stats for the given serialized slice
    #[cfg(feature = "display_list_stats")]
    fn log_slice<T: Copy + Default + peek_poke::Peek>(
        &mut self,
        slice_name: &'static str,
        range: &ItemRange<T>,
    ) {
        // Run this so log_item_stats is accurate, but ignore its result
        // because log_slice_stats may be called after multiple slices have been
        // processed, and the `range` has everything we need.
        self.last_addr = range.bytes.as_ptr() as usize + range.bytes.len();

        self._update_entry(slice_name, range.iter().len(), range.bytes.len());
    }

    #[cfg(not(feature = "display_list_stats"))]
    fn log_slice<T>(&mut self, _slice_name: &str, _range: &ItemRange<T>) {
        /* no-op */
    }
}

/// Stats for an individual item
#[derive(Copy, Clone, Debug, Default)]
pub struct ItemStats {
    /// How many instances of this kind of item we deserialized
    pub total_count: usize,
    /// How many bytes we processed for this kind of item
    pub num_bytes: usize,
}

pub struct DisplayItemRef<'a: 'b, 'b> {
    iter: &'b BuiltDisplayListIter<'a>,
}

// Some of these might just become ItemRanges
impl<'a, 'b> DisplayItemRef<'a, 'b> {
    // Creates a new iterator where this element's iterator is, to hack around borrowck.
    pub fn sub_iter(&self) -> BuiltDisplayListIter<'a> {
        self.iter.sub_iter()
    }

    pub fn item(&self) -> &di::DisplayItem {
       self.iter.current_item()
    }

    pub fn clip_chain_items(&self) -> ItemRange<di::ClipId> {
        self.iter.cur_clip_chain_items
    }

    pub fn points(&self) -> ItemRange<LayoutPoint> {
        self.iter.cur_points
    }

    pub fn glyphs(&self) -> ItemRange<GlyphInstance> {
        self.iter.glyphs()
    }

    pub fn gradient_stops(&self) -> ItemRange<di::GradientStop> {
        self.iter.gradient_stops()
    }

    pub fn filters(&self) -> ItemRange<di::FilterOp> {
        self.iter.cur_filters
    }

    pub fn filter_datas(&self) -> &Vec<TempFilterData> {
        &self.iter.cur_filter_data
    }
}

#[derive(PartialEq)]
enum Peek {
    StartPeeking,
    IsPeeking,
    NotPeeking,
}

#[derive(Clone)]
pub struct AuxIter<'a, T> {
    item: T,
    data: &'a [u8],
    size: usize,
//    _boo: PhantomData<T>,
}

impl BuiltDisplayList {
    pub fn from_data(
        payload: DisplayListPayload,
        descriptor: BuiltDisplayListDescriptor,
    ) -> Self {
        BuiltDisplayList {
            payload,
            descriptor,
        }
    }

    pub fn into_data(self) -> (DisplayListPayload, BuiltDisplayListDescriptor) {
        (self.payload, self.descriptor)
    }

    pub fn items_data(&self) -> &[u8] {
        &self.payload.items_data
    }

    pub fn descriptor(&self) -> &BuiltDisplayListDescriptor {
        &self.descriptor
    }

    pub fn set_send_time_ns(&mut self, time: u64) {
        self.descriptor.send_start_time = time;
    }

    pub fn times(&self) -> (u64, u64, u64) {
        (
            self.descriptor.builder_start_time,
            self.descriptor.builder_finish_time,
            self.descriptor.send_start_time,
        )
    }

    pub fn gecko_display_list_stats(&self) -> (f64, bool) {
        match self.descriptor.gecko_display_list_type {
            GeckoDisplayListType::Full(duration) => (duration, true),
            GeckoDisplayListType::Partial(duration) => (duration, false),
            _ => (0.0, false)
        }
    }

    pub fn total_clip_nodes(&self) -> usize {
        self.descriptor.total_clip_nodes
    }

    pub fn total_spatial_nodes(&self) -> usize {
        self.descriptor.total_spatial_nodes
    }

    /// See `BuiltDisplayListDescriptor::off_grid_coords`.
    pub fn off_grid_coords(&self) -> u32 {
        self.descriptor.off_grid_coords
    }

    pub fn iter(&self) -> BuiltDisplayListIter {
        BuiltDisplayListIter::new(self.items_data())
    }

    pub fn size_in_bytes(&self) -> usize {
        self.payload.size_in_bytes()
    }

    pub fn iter_spatial_tree<F>(&self, f: F) where F: FnMut(&di::SpatialTreeItem) {
        iter_spatial_tree(&self.payload.spatial_tree, f)
    }

    #[cfg(feature = "serialize")]
    pub fn create_debug_display_items(
        mut iterator: BuiltDisplayListIter,
    ) -> Vec<di::DebugDisplayItem> {
        use di::DisplayItem as Real;
        use di::DebugDisplayItem as Debug;
        let mut debug_items = Vec::new();

        while let Some(item) = iterator.next_raw() {
            let serial_di = match *item.item() {
                Real::ClipChain(v) => Debug::ClipChain(
                    v,
                    item.iter.cur_clip_chain_items.iter().collect()
                ),
                Real::Text(v) => Debug::Text(
                    v,
                    item.iter.cur_glyphs.iter().collect()
                ),
                Real::SetFilterOps => Debug::SetFilterOps(
                    item.iter.cur_filters.iter().collect()
                ),
                Real::SetFilterData => {
                    debug_assert!(!item.iter.cur_filter_data.is_empty(),
                        "next_raw should have populated cur_filter_data");
                    let temp_filter_data = &item.iter.cur_filter_data[item.iter.cur_filter_data.len()-1];

                    let func_types: Vec<di::ComponentTransferFuncType> =
                        temp_filter_data.func_types.iter().collect();
                    debug_assert!(func_types.len() == 4,
                        "someone changed the number of filter funcs without updating this code");
                    Debug::SetFilterData(di::FilterData {
                        func_r_type: func_types[0],
                        r_values: temp_filter_data.r_values.iter().collect(),
                        func_g_type: func_types[1],
                        g_values: temp_filter_data.g_values.iter().collect(),
                        func_b_type: func_types[2],
                        b_values: temp_filter_data.b_values.iter().collect(),
                        func_a_type: func_types[3],
                        a_values: temp_filter_data.a_values.iter().collect(),
                    })
                },
                Real::SetGradientStops => Debug::SetGradientStops(
                    item.iter.cur_stops.iter().collect()
                ),
                Real::SetPoints => Debug::SetPoints(
                    item.iter.cur_points.iter().collect()
                ),
                Real::RectClip(v) => Debug::RectClip(v),
                Real::RoundedRectClip(v) => Debug::RoundedRectClip(v),
                Real::ImageMaskClip(v) => Debug::ImageMaskClip(v),
                Real::Rectangle(v) => Debug::Rectangle(v),
                Real::HitTest(v) => Debug::HitTest(v),
                Real::Line(v) => Debug::Line(v),
                Real::Image(v) => Debug::Image(v),
                Real::RepeatingImage(v) => Debug::RepeatingImage(v),
                Real::YuvImage(v) => Debug::YuvImage(v),
                Real::Border(v) => Debug::Border(v),
                Real::BoxShadow(v) => Debug::BoxShadow(v),
                Real::Gradient(v) => Debug::Gradient(v),
                Real::RadialGradient(v) => Debug::RadialGradient(v),
                Real::ConicGradient(v) => Debug::ConicGradient(v),
                Real::Iframe(v) => Debug::Iframe(v),
                Real::PushReferenceFrame(v) => Debug::PushReferenceFrame(v),
                Real::PushStackingContext(v) => Debug::PushStackingContext(v),
                Real::BackdropFilter(v) => Debug::BackdropFilter(v),

                Real::PopReferenceFrame => Debug::PopReferenceFrame,
                Real::PopStackingContext => Debug::PopStackingContext,
                Real::DebugMarker(val) => Debug::DebugMarker(val),
            };
            debug_items.push(serial_di);
        }

        debug_items
    }
}

/// Returns the byte-range the slice occupied.
fn skip_slice<'a, T: peek_poke::Peek>(data: &mut &'a [u8]) -> ItemRange<'a, T> {
    let mut skip_offset = 0usize;
    *data = peek_from_slice(data, &mut skip_offset);
    let (skip, rest) = data.split_at(skip_offset);

    // Adjust data pointer to skip read values
    *data = rest;

    ItemRange {
        bytes: skip,
        _boo: PhantomData,
    }
}

impl<'a> BuiltDisplayListIter<'a> {
    pub fn new(
        data: &'a [u8],
    ) -> Self {
        Self {
            data,
            cur_item: di::DisplayItem::PopStackingContext,
            cur_stops: ItemRange::default(),
            cur_glyphs: ItemRange::default(),
            cur_filters: ItemRange::default(),
            cur_filter_data: Vec::new(),
            cur_clip_chain_items: ItemRange::default(),
            cur_points: ItemRange::default(),
            peeking: Peek::NotPeeking,
            debug_stats: DebugStats {
                last_addr: data.as_ptr() as usize,
                stats: HashMap::default(),
            },
        }
    }

    pub fn sub_iter(&self) -> Self {
        BuiltDisplayListIter::new(self.data)
    }

    pub fn current_item(&self) -> &di::DisplayItem {
        &self.cur_item
    }

    pub fn glyphs(&self) -> ItemRange<GlyphInstance> {
        self.cur_glyphs
    }

    pub fn gradient_stops(&self) -> ItemRange<di::GradientStop> {
        self.cur_stops
    }

    pub fn next<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        use crate::DisplayItem::*;

        match self.peeking {
            Peek::IsPeeking => {
                self.peeking = Peek::NotPeeking;
                return Some(self.as_ref());
            }
            Peek::StartPeeking => {
                self.peeking = Peek::IsPeeking;
            }
            Peek::NotPeeking => { /* do nothing */ }
        }

        // Don't let these bleed into another item
        self.cur_stops = ItemRange::default();
        self.cur_clip_chain_items = ItemRange::default();
        self.cur_points = ItemRange::default();
        self.cur_filters = ItemRange::default();
        self.cur_filter_data.clear();

        loop {
            self.next_raw()?;
            match self.cur_item {
                SetGradientStops |
                SetFilterOps |
                SetFilterData |
                SetPoints => {
                    // These are marker items for populating other display items, don't yield them.
                    continue;
                }
                _ => {
                    break;
                }
            }
        }

        Some(self.as_ref())
    }

    /// Gets the next display item, even if it's a dummy. Also doesn't handle peeking
    /// and may leave irrelevant ranges live (so a Clip may have GradientStops if
    /// for some reason you ask).
    pub fn next_raw<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        use crate::DisplayItem::*;

        // A "red zone" of DisplayItem::max_size() bytes has been added to the
        // end of the serialized display list. If this amount, or less, is
        // remaining then we've reached the end of the display list.
        if self.data.len() <= di::DisplayItem::max_size() {
            return None;
        }

        self.data = peek_from_slice(self.data, &mut self.cur_item);
        self.log_item_stats();

        match self.cur_item {
            SetGradientStops => {
                self.cur_stops = skip_slice::<di::GradientStop>(&mut self.data);
                self.debug_stats.log_slice("set_gradient_stops.stops", &self.cur_stops);
            }
            SetFilterOps => {
                self.cur_filters = skip_slice::<di::FilterOp>(&mut self.data);
                self.debug_stats.log_slice("set_filter_ops.ops", &self.cur_filters);
            }
            SetFilterData => {
                self.cur_filter_data.push(TempFilterData {
                    func_types: skip_slice::<di::ComponentTransferFuncType>(&mut self.data),
                    r_values: skip_slice::<f32>(&mut self.data),
                    g_values: skip_slice::<f32>(&mut self.data),
                    b_values: skip_slice::<f32>(&mut self.data),
                    a_values: skip_slice::<f32>(&mut self.data),
                });

                let data = *self.cur_filter_data.last().unwrap();
                self.debug_stats.log_slice("set_filter_data.func_types", &data.func_types);
                self.debug_stats.log_slice("set_filter_data.r_values", &data.r_values);
                self.debug_stats.log_slice("set_filter_data.g_values", &data.g_values);
                self.debug_stats.log_slice("set_filter_data.b_values", &data.b_values);
                self.debug_stats.log_slice("set_filter_data.a_values", &data.a_values);
            }
            SetPoints => {
                self.cur_points = skip_slice::<LayoutPoint>(&mut self.data);
                self.debug_stats.log_slice("set_points.points", &self.cur_points);
            }
            ClipChain(_) => {
                self.cur_clip_chain_items = skip_slice::<di::ClipId>(&mut self.data);
                self.debug_stats.log_slice("clip_chain.clip_ids", &self.cur_clip_chain_items);
            }
            Text(_) => {
                self.cur_glyphs = skip_slice::<GlyphInstance>(&mut self.data);
                self.debug_stats.log_slice("text.glyphs", &self.cur_glyphs);
            }
            _ => { /* do nothing */ }
        }

        Some(self.as_ref())
    }

    pub fn as_ref<'b>(&'b self) -> DisplayItemRef<'a, 'b> {
        DisplayItemRef {
            iter: self,
        }
    }

    pub fn skip_current_stacking_context(&mut self) {
        let mut depth = 0;
        while let Some(item) = self.next() {
            match *item.item() {
                di::DisplayItem::PushStackingContext(..) => depth += 1,
                di::DisplayItem::PopStackingContext if depth == 0 => return,
                di::DisplayItem::PopStackingContext => depth -= 1,
                _ => {}
            }
        }
    }

    pub fn current_stacking_context_empty(&mut self) -> bool {
        match self.peek() {
            Some(item) => *item.item() == di::DisplayItem::PopStackingContext,
            None => true,
        }
    }

    pub fn peek<'b>(&'b mut self) -> Option<DisplayItemRef<'a, 'b>> {
        if self.peeking == Peek::NotPeeking {
            self.peeking = Peek::StartPeeking;
            self.next()
        } else {
            Some(self.as_ref())
        }
    }

    /// Get the debug stats for what this iterator has deserialized.
    /// Should always be empty in release builds.
    pub fn debug_stats(&mut self) -> Vec<(&'static str, ItemStats)> {
        let mut result = self.debug_stats.stats.drain().collect::<Vec<_>>();
        result.sort_by_key(|stats| stats.0);
        result
    }

    /// Adds the debug stats from another to our own, assuming we are a sub-iter of the other
    /// (so we can ignore where they were in the traversal).
    pub fn merge_debug_stats_from(&mut self, other: &mut Self) {
        for (key, other_entry) in other.debug_stats.stats.iter() {
            let entry = self.debug_stats.stats.entry(key).or_default();

            entry.total_count += other_entry.total_count;
            entry.num_bytes += other_entry.num_bytes;
        }
    }

    /// Logs stats for the last deserialized display item
    #[cfg(feature = "display_list_stats")]
    fn log_item_stats(&mut self) {
        self.debug_stats.log_item(self.data, &self.cur_item);
    }

    #[cfg(not(feature = "display_list_stats"))]
    fn log_item_stats(&mut self) { /* no-op */ }
}

impl<'a, T> AuxIter<'a, T> {
    pub fn new(item: T, mut data: &'a [u8]) -> Self {
        let mut size = 0usize;
        if !data.is_empty() {
            data = peek_from_slice(data, &mut size);
        };

        AuxIter {
            item,
            data,
            size,
//            _boo: PhantomData,
        }
    }
}

impl<'a, T: Copy + peek_poke::Peek> Iterator for AuxIter<'a, T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        if self.size == 0 {
            None
        } else {
            self.size -= 1;
            self.data = peek_from_slice(self.data, &mut self.item);
            Some(self.item)
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.size, Some(self.size))
    }
}

impl<'a, T: Copy + peek_poke::Peek> ::std::iter::ExactSizeIterator for AuxIter<'a, T> {}

#[derive(Clone, Debug)]
pub struct SaveState {
    dl_items_len: usize,
    next_clip_index: usize,
    next_spatial_index: usize,
    next_clip_chain_id: u64,
    shadow_capture_len: usize,
    pending_shadows_len: usize,
    raster_space_stack_len: usize,
}

/// DisplayListSection determines the target buffer for the display items.
pub enum DisplayListSection {
    /// The main/default buffer: contains item data.
    Data,
}

/// Normalizing an item by its accumulated external scroll offset has to be exact,
/// or the coordinates WebRender interns drift with the scroll position even though
/// nothing moved. Doing it as `p + S` in f32 is not exact: Gecko's coordinate is
/// `p_au / appUnitsPerDevPixel`, which is generally not a binary fraction, so both
/// the conversion and the addition round.
///
/// Quantizing to a finer grid does not fix this, because quantization is not
/// additive - `Q(x + S) - Q(S) != Q(x)` - so it merely trades one scroll-dependent
/// residue for another (measured: quantizing the offsets alone nearly doubled the
/// drift). The addition has to happen in the domain the subtraction happened in.
/// Gecko computed `p_au = x_au - S_au` in *integer* app units, so re-adding `S_au`
/// as an integer recovers `x_au` exactly.
///
/// Hence `AuOffset`: accumulated offsets are carried as whole app units and added
/// to app-unit coordinates, never as f32 layout pixels. See bug 2059570.
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
struct AuOffset {
    x: i32,
    y: i32,
}

impl AuOffset {
    const ZERO: Self = AuOffset { x: 0, y: 0 };

    fn is_zero(&self) -> bool {
        self.x == 0 && self.y == 0
    }
}

impl std::ops::Add for AuOffset {
    type Output = Self;
    fn add(self, o: Self) -> Self {
        AuOffset { x: self.x + o.x, y: self.y + o.y }
    }
}

impl std::ops::Sub for AuOffset {
    type Output = Self;
    fn sub(self, o: Self) -> Self {
        AuOffset { x: self.x - o.x, y: self.y - o.y }
    }
}

/// Beyond this many app units an f32 can no longer hold a whole app unit, so the
/// exact path is meaningless. Sentinel geometry (`LayoutRect::max_rect`) is far
/// past it and carries no position to preserve.
const MAX_EXACT_AU: f64 = (1i64 << 24) as f64;

/// Grid state for one display list: app units per device pixel, as
/// `nsPresContext::AppUnitsPerDevPixel`. Not a constant 60 - Gecko emits
/// LayoutDevicePixels, and the divisor is `max(1, lround(60 / dpr))` further
/// divided by full zoom, so 60 at dpr 1.0, 48 at 1.25, 45 at 1.3333, 30 at dpr 2.
/// It also differs *within* one WebRender document, since full zoom applies to
/// content but not chrome, which is why it is per display list rather than global.
#[derive(Copy, Clone, Debug)]
pub struct AuGrid {
    per_px: f32,
    per_px_f64: f64,
}

impl AuGrid {
    pub fn new(au_per_dev_px: f32) -> Self {
        assert!(au_per_dev_px > 0.0, "app units per device pixel must be positive");
        AuGrid { per_px: au_per_dev_px, per_px_f64: au_per_dev_px as f64 }
    }

    /// Convert a coordinate to whole app units. Rounding is exact recovery for
    /// coordinates Gecko authored, which are already whole app units on this grid;
    /// `off_grid` counts any that are not.
    fn to_au(&self, v: f32, off_grid: &mut u32) -> f64 {
        let scaled = v as f64 * self.per_px_f64;
        let rounded = scaled.round();
        if (scaled - rounded).abs() > 1.0e-3 {
            *off_grid += 1;
        }
        rounded
    }

    fn from_au(&self, au: f64) -> f32 {
        if au.abs() <= MAX_EXACT_AU {
            // Match Gecko's own NSAppUnitsToFloatPixels, which divides in f32, so
            // the result is bit-identical to the coordinate we were handed.
            au as f32 / self.per_px
        } else {
            (au / self.per_px_f64) as f32
        }
    }

    fn add(&self, v: f32, off_au: i32, off_grid: &mut u32) -> f32 {
        self.from_au(self.to_au(v, off_grid) + off_au as f64)
    }

    fn point(&self, p: LayoutPoint, off: AuOffset, off_grid: &mut u32) -> LayoutPoint {
        LayoutPoint::new(self.add(p.x, off.x, off_grid), self.add(p.y, off.y, off_grid))
    }

    fn rect(&self, r: LayoutRect, off: AuOffset, off_grid: &mut u32) -> LayoutRect {
        LayoutRect {
            min: self.point(r.min, off, off_grid),
            max: self.point(r.max, off, off_grid),
        }
    }

    /// Convert a vector Gecko supplied (a scroll offset) to whole app units.
    fn vec_to_au(&self, v: LayoutVector2D, off_grid: &mut u32) -> AuOffset {
        AuOffset {
            x: self.to_au(v.x, off_grid) as i32,
            y: self.to_au(v.y, off_grid) as i32,
        }
    }
}

pub struct DisplayListBuilder {
    payload: DisplayListPayload,
    pub pipeline_id: PipelineId,

    next_clip_index: usize,
    next_spatial_index: usize,
    next_clip_chain_id: u64,
    builder_start_time: u64,

    save_state: Option<SaveState>,

    serialized_content_buffer: Option<String>,
    state: BuildState,

    /// Accumulated external scroll offset per spatial node, used to normalize
    /// item coordinates at push time so WebRender interns scroll-invariant
    /// positions. Scroll frames add their `external_scroll_offset`, sticky
    /// frames subtract their `previously_applied_offset`, reference frames
    /// reset to zero. The offset fields are still sent so WebRender can keep
    /// applying them at frame time (APZ reconciliation, sticky math).
    spatial_offsets: HashMap<di::SpatialId, AuOffset>,
    /// Single-entry cache for `spatial_offsets`. Items are typically emitted
    /// grouped by spatial node, so consecutive lookups hit this and skip
    /// hashing (mirrors the scene builder's `ScrollOffsetMapper`).
    last_scroll_offset: Option<(di::SpatialId, AuOffset)>,
    /// App units per device pixel for the display list being built, set by
    /// `begin`. Normalization is exact only on the grid the coordinates were
    /// authored on, so this is per display list: it changes with device scale and
    /// full zoom, and differs between chrome and content in one document.
    au_grid: AuGrid,
    /// Coordinates seen that were not whole app units on `au_grid`. Reported as a
    /// profiler counter; a non-zero value means some producer is emitting
    /// coordinates off the grid it declared, so normalization is not exact for
    /// them.
    off_grid_coords: u32,
    /// Reused buffer for normalized glyph positions, to avoid a per-text-run
    /// allocation when shifting glyphs by the external scroll offset.
    glyph_scratch: Vec<GlyphInstance>,
    /// While a shadow scope is open (between `push_shadow` and
    /// `pop_all_shadows`), the items pushed within it are captured here instead
    /// of being written straight to `payload.items_data`, so `pop_all_shadows`
    /// can desugar them. The buffer is cleared and reused across shadow scopes
    /// rather than reallocated. A scope is open exactly when `pending_shadows`
    /// is non-empty.
    shadow_capture: Vec<u8>,
    /// The shadows declared by `push_shadow` in the current scope, in order.
    /// Held as typed descriptors (not captured markers) and consumed by
    /// `pop_all_shadows`, which desugars them into blur stacking contexts.
    pending_shadows: Vec<PendingShadow>,
    /// Raster space in effect, one entry per open stacking context plus a
    /// `Screen` base. Resolving here rather than in the scene builder means one
    /// stack instead of two that have to agree.
    raster_space_stack: Vec<di::RasterSpace>,
}

/// A shadow declared by `push_shadow`, awaiting desugaring at `pop_all_shadows`.
#[derive(Clone)]
struct PendingShadow {
    space_and_clip: di::SpaceAndClipInfo,
    shadow: di::Shadow,
    should_inflate: bool,
}

#[repr(C)]
struct DisplayListCapacity {
    items_size: usize,
    spatial_tree_size: usize,
}

impl DisplayListCapacity {
    fn empty() -> Self {
        DisplayListCapacity {
            items_size: 0,
            spatial_tree_size: 0,
        }
    }
}

impl DisplayListBuilder {
    pub fn new(pipeline_id: PipelineId) -> Self {
        DisplayListBuilder {
            payload: DisplayListPayload::new(DisplayListCapacity::empty()),
            pipeline_id,

            next_clip_index: FIRST_CLIP_NODE_INDEX,
            next_spatial_index: FIRST_SPATIAL_NODE_INDEX,
            next_clip_chain_id: 0,
            builder_start_time: 0,
            save_state: None,
            serialized_content_buffer: None,
            state: BuildState::Idle,
            spatial_offsets: HashMap::new(),
            last_scroll_offset: None,
            // Replaced by `begin`; 60 is the dpr 1.0 value.
            au_grid: AuGrid::new(60.0),
            off_grid_coords: 0,
            glyph_scratch: Vec::new(),
            shadow_capture: Vec::new(),
            pending_shadows: Vec::new(),
            raster_space_stack: vec![di::RasterSpace::Screen],
        }
    }

    fn reset(&mut self) {
        self.payload.clear();

        self.next_clip_index = FIRST_CLIP_NODE_INDEX;
        self.next_spatial_index = FIRST_SPATIAL_NODE_INDEX;
        self.next_clip_chain_id = 0;

        self.save_state = None;
        self.serialized_content_buffer = None;
        self.spatial_offsets.clear();
        self.last_scroll_offset = None;
        self.off_grid_coords = 0;
        self.shadow_capture.clear();
        self.pending_shadows.clear();

        self.raster_space_stack.clear();
        self.raster_space_stack.push(di::RasterSpace::Screen);
    }

    /// Saves the current display list state, so it may be `restore()`'d.
    ///
    /// # Conditions:
    ///
    /// * Doesn't support popping clips that were pushed before the save.
    /// * Doesn't support nested saves.
    /// * Must call `clear_save()` if the restore becomes unnecessary.
    pub fn save(&mut self) {
        assert!(self.save_state.is_none(), "DisplayListBuilder doesn't support nested saves");

        self.save_state = Some(SaveState {
            dl_items_len: self.payload.items_data.len(),
            next_clip_index: self.next_clip_index,
            next_spatial_index: self.next_spatial_index,
            next_clip_chain_id: self.next_clip_chain_id,
            shadow_capture_len: self.shadow_capture.len(),
            pending_shadows_len: self.pending_shadows.len(),
            raster_space_stack_len: self.raster_space_stack.len(),
        });
    }

    /// Restores the state of the builder to when `save()` was last called.
    pub fn restore(&mut self) {
        let state = self.save_state.take().expect("No save to restore DisplayListBuilder from");

        self.payload.items_data.truncate(state.dl_items_len);
        self.next_clip_index = state.next_clip_index;
        self.next_spatial_index = state.next_spatial_index;
        self.next_clip_chain_id = state.next_clip_chain_id;

        // Roll back any shadow scope opened since the save. Both buffers are
        // append-only within a scope, so truncating to their save-time lengths
        // discards exactly the speculative shadow state (the pre-desugar
        // machinery kept this state in `items_data`, which the truncate above
        // already handled).
        self.shadow_capture.truncate(state.shadow_capture_len);
        self.pending_shadows.truncate(state.pending_shadows_len);

        // Stacking contexts opened since the save go away with their items.
        self.raster_space_stack.truncate(state.raster_space_stack_len);

        // Drop offsets recorded for spatial nodes defined after the save point;
        // those ids will be reused, so the single-entry cache could be stale.
        let next_spatial_index = state.next_spatial_index;
        self.spatial_offsets.retain(|id, _| id.0 < next_spatial_index);
        self.last_scroll_offset = None;
    }

    /// Discards the builder's save (indicating the attempted operation was successful).
    pub fn clear_save(&mut self) {
        self.save_state.take().expect("No save to clear in DisplayListBuilder");
    }

    /// Emits a debug representation of display items in the list, for debugging
    /// purposes. If the range's start parameter is specified, only display
    /// items starting at that index (inclusive) will be printed. If the range's
    /// end parameter is specified, only display items before that index
    /// (exclusive) will be printed. Calling this function with end <= start is
    /// allowed but is just a waste of CPU cycles. The function emits the
    /// debug representation of the selected display items, one per line, with
    /// the given indent, to the provided sink object. The return value is
    /// the total number of items in the display list, which allows the
    /// caller to subsequently invoke this function to only dump the newly-added
    /// items.
    pub fn emit_display_list<W>(
        &mut self,
        indent: usize,
        range: Range<Option<usize>>,
        mut sink: W,
    ) -> usize
    where
        W: Write
    {
        let mut temp = BuiltDisplayList::default();
        ensure_red_zone::<di::DisplayItem>(&mut self.payload.items_data);
        mem::swap(&mut temp.payload, &mut self.payload);

        let mut index: usize = 0;
        {
            let mut iter = temp.iter();
            while let Some(item) = iter.next_raw() {
                if index >= range.start.unwrap_or(0) && range.end.map_or(true, |e| index < e) {
                    writeln!(sink, "{}{:?}", "  ".repeat(indent), item.item()).unwrap();
                }
                index += 1;
            }
        }

        self.payload = temp.payload;
        strip_red_zone::<di::DisplayItem>(&mut self.payload.items_data);
        index
    }

    /// Print the display items in the list to stdout.
    pub fn dump_serialized_display_list(&mut self) {
        self.serialized_content_buffer = Some(String::new());
    }

    fn add_to_display_list_dump<T: std::fmt::Debug>(&mut self, item: T) {
        if let Some(ref mut content) = self.serialized_content_buffer {
            use std::fmt::Write;
            writeln!(content, "{:?}", item).expect("DL dump write failed.");
        }
    }

    /// Returns the default section that DisplayListBuilder will write to,
    /// if no section is specified explicitly.
    fn default_section(&self) -> DisplayListSection {
        DisplayListSection::Data
    }

    fn buffer_from_section(
        &mut self,
        section: DisplayListSection
    ) -> &mut Vec<u8> {
        match section {
            // While a shadow scope is open (a shadow has been pushed but not yet
            // popped), divert item and aux-array writes into the capture buffer
            // so `pop_all_shadows` can desugar them. The capture uses relative
            // sizes/counts (see `push_iter_impl`), so it stays valid when
            // appended back into `items_data`.
            DisplayListSection::Data => if self.pending_shadows.is_empty() {
                &mut self.payload.items_data
            } else {
                &mut self.shadow_capture
            },
        }
    }

    #[inline]
    pub fn push_item_to_section(
        &mut self,
        item: &di::DisplayItem,
        section: DisplayListSection,
    ) {
        debug_assert_eq!(self.state, BuildState::Build);
        poke_into_vec(item, self.buffer_from_section(section));
        self.add_to_display_list_dump(item);
    }

    /// Add an item to the display list.
    ///
    /// NOTE: It is usually preferable to use the specialized methods to push
    /// display items. Pushing unexpected or invalid items here may
    /// result in WebRender panicking or behaving in unexpected ways.
    #[inline]
    pub fn push_item(&mut self, item: &di::DisplayItem) {
        self.push_item_to_section(item, self.default_section());
    }

    #[inline]
    pub fn push_spatial_tree_item(&mut self, item: &di::SpatialTreeItem) {
        debug_assert_eq!(self.state, BuildState::Build);
        poke_into_vec(item, &mut self.payload.spatial_tree);
    }

    fn push_iter_impl<I>(data: &mut Vec<u8>, iter_source: I)
    where
        I: IntoIterator,
        I::IntoIter: ExactSizeIterator,
        I::Item: Poke,
    {
        let iter = iter_source.into_iter();
        let len = iter.len();
        // Format:
        // payload_byte_size: usize, item_count: usize, [I; item_count]

        // Track the the location of where to write byte size with offsets
        // instead of pointers because data may be moved in memory during
        // `serialize_iter_fast`.
        let byte_size_offset = data.len();

        // We write a dummy value so there's room for later
        poke_into_vec(&0usize, data);
        poke_into_vec(&len, data);
        let count = poke_extend_vec(iter, data);
        debug_assert_eq!(len, count, "iterator.len() returned two different values");

        // Add red zone
        ensure_red_zone::<I::Item>(data);

        // Now write the actual byte_size
        let final_offset = data.len();
        debug_assert!(final_offset >= (byte_size_offset + mem::size_of::<usize>()),
            "space was never allocated for this array's byte_size");
        let byte_size = final_offset - byte_size_offset - mem::size_of::<usize>();
        poke_inplace_slice(&byte_size, &mut data[byte_size_offset..]);
    }

    /// Push items from an iterator to the display list.
    ///
    /// NOTE: Pushing unexpected or invalid items to the display list
    /// may result in panic and confusion.
    pub fn push_iter<I>(&mut self, iter: I)
    where
        I: IntoIterator,
        I::IntoIter: ExactSizeIterator,
        I::Item: Poke,
    {
        assert_eq!(self.state, BuildState::Build);

        let buffer = self.buffer_from_section(self.default_section());
        Self::push_iter_impl(buffer, iter);
    }

    pub fn push_rect(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        color: ColorF,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::Rectangle(di::RectangleDisplayItem {
            common,
            color: PropertyBinding::Value(color),
            bounds: self.shift_rect(bounds, offset),
        });
        self.push_item(&item);
    }

    pub fn push_rect_with_animation(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        color: PropertyBinding<ColorF>,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::Rectangle(di::RectangleDisplayItem {
            common,
            color,
            bounds: self.shift_rect(bounds, offset),
        });
        self.push_item(&item);
    }

    pub fn push_hit_test(
        &mut self,
        rect: LayoutRect,
        clip_chain_id: di::ClipChainId,
        spatial_id: di::SpatialId,
        flags: di::PrimitiveFlags,
        tag: di::ItemTag,
    ) {
        let item = di::DisplayItem::HitTest(di::HitTestDisplayItem {
            rect: self.normalize_rect(rect, spatial_id),
            clip_chain_id,
            spatial_id,
            flags,
            tag,
        });
        self.push_item(&item);
    }

    pub fn push_line(
        &mut self,
        common: &di::CommonItemProperties,
        area: &LayoutRect,
        wavy_line_thickness: f32,
        orientation: di::LineOrientation,
        color: &ColorF,
        style: di::LineStyle,
    ) {
        let (common, offset) = self.normalize_common(common);
        let area = self.shift_rect(*area, offset);

        let item = di::DisplayItem::Line(di::LineDisplayItem {
            common,
            area,
            wavy_line_thickness,
            orientation,
            color: *color,
            style,
        });

        self.push_item(&item);
    }

    pub fn push_image(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        image_rendering: di::ImageRendering,
        alpha_type: di::AlphaType,
        key: ImageKey,
        color: ColorF,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::Image(di::ImageDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            image_key: key,
            image_rendering,
            alpha_type,
            color,
        });

        self.push_item(&item);
    }

    pub fn push_repeating_image(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        stretch_size: LayoutSize,
        tile_spacing: LayoutSize,
        image_rendering: di::ImageRendering,
        alpha_type: di::AlphaType,
        key: ImageKey,
        color: ColorF,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::RepeatingImage(di::RepeatingImageDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            image_key: key,
            stretch_size,
            tile_spacing,
            image_rendering,
            alpha_type,
            color,
        });

        self.push_item(&item);
    }

    /// Push a yuv image. All planar data in yuv image should use the same buffer type.
    pub fn push_yuv_image(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        yuv_data: di::YuvData,
        color_depth: ColorDepth,
        color_space: di::YuvColorSpace,
        color_range: di::ColorRange,
        image_rendering: di::ImageRendering,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::YuvImage(di::YuvImageDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            yuv_data,
            color_depth,
            color_space,
            color_range,
            image_rendering,
        });
        self.push_item(&item);
    }

    pub fn push_text(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        glyphs: &[GlyphInstance],
        font_key: FontInstanceKey,
        color: ColorF,
        glyph_options: Option<GlyphOptions>,
    ) {
        let (common, offset) = self.normalize_common(common);
        let item = di::DisplayItem::Text(di::TextDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            color,
            font_key,
            glyph_options,
            shadow: di::GlyphShadowMode::None,
        });

        // Store glyph pen positions relative to the (pre-normalization) bounds
        // origin. This difference is scroll-invariant - both the glyph points
        // and the bounds come from the same local space - so it subsumes the
        // external scroll offset removal that `normalize_common` applies to
        // other coordinates, and it lets a shadow copy be produced by
        // translating just the bounds (the glyphs follow the prim origin at
        // build time). The scene builder previously did this relativization; it
        // now happens here at record time.
        let bounds_origin = bounds.min.to_vector();

        // Take the scratch buffer out so we can hold it while also borrowing
        // `self` mutably for `push_item`/`push_iter`; put it back afterwards to
        // retain its capacity across text runs.
        let mut scratch = mem::take(&mut self.glyph_scratch);
        for split_glyphs in glyphs.chunks(MAX_TEXT_RUN_LENGTH) {
            self.push_item(&item);
            scratch.clear();
            scratch.extend(split_glyphs.iter().map(|g| GlyphInstance {
                index: g.index,
                point: g.point - bounds_origin,
            }));
            self.push_iter(&scratch);
        }
        self.glyph_scratch = scratch;
    }

    /// Normalize `stops` and describe the gradient they make.
    ///
    /// Returns the normalized stops alongside the gradient, for the matching
    /// `push_*` to take: the stops describe the gradient, so they travel with
    /// it rather than being left in the builder. Nothing is recorded here.
    ///
    /// This used to push the stops into the item stream itself, which is why
    /// gradients had to be pushed in the order they were created - and, since a
    /// `SetGradientStops` overwrites the last one, really had to be created and
    /// pushed strictly one at a time. Handing them back removes that hazard.
    pub fn create_gradient(
        &mut self,
        start_point: LayoutPoint,
        end_point: LayoutPoint,
        stops: Vec<di::GradientStop>,
        extend_mode: di::ExtendMode,
    ) -> (di::Gradient, Vec<di::GradientStop>) {
        let mut builder = GradientBuilder::with_stops(stops);
        let gradient = builder.gradient(start_point, end_point, extend_mode);
        (gradient, builder.into_stops())
    }

    /// See [`create_gradient`](#method.create_gradient).
    pub fn create_radial_gradient(
        &mut self,
        center: LayoutPoint,
        radius: LayoutSize,
        stops: Vec<di::GradientStop>,
        extend_mode: di::ExtendMode,
    ) -> (di::RadialGradient, Vec<di::GradientStop>) {
        let mut builder = GradientBuilder::with_stops(stops);
        let gradient = builder.radial_gradient(center, radius, extend_mode);
        (gradient, builder.into_stops())
    }

    /// See [`create_gradient`](#method.create_gradient).
    pub fn create_conic_gradient(
        &mut self,
        center: LayoutPoint,
        angle: f32,
        stops: Vec<di::GradientStop>,
        extend_mode: di::ExtendMode,
    ) -> (di::ConicGradient, Vec<di::GradientStop>) {
        let mut builder = GradientBuilder::with_stops(stops);
        let gradient = builder.conic_gradient(center, angle, extend_mode);
        (gradient, builder.into_stops())
    }

    pub fn push_border(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        widths: LayoutSideOffsets,
        details: di::BorderDetails,
        // Stops for a `NinePatchBorderSource` gradient; empty otherwise.
        gradient_stops: &[di::GradientStop],
    ) {
        let (common, offset) = self.normalize_common(common);
        self.push_stops(gradient_stops);

        let item = di::DisplayItem::Border(di::BorderDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            details,
            widths,
        });

        self.push_item(&item);
    }

    pub fn push_box_shadow(
        &mut self,
        common: &di::CommonItemProperties,
        box_bounds: LayoutRect,
        offset: LayoutVector2D,
        color: ColorF,
        blur_radius: f32,
        spread_radius: f32,
        border_radius: di::BorderRadius,
        shadow_radius: di::BorderRadius,
        clip_mode: di::BoxShadowClipMode,
    ) {
        // Zero-blur box-shadows desugar into a plain rectangle shaped by a
        // ClipOut/Clip rounded-rect pair, so the scene builder sees only
        // ordinary items instead of expanding a BoxShadow via extra clips.
        // Box-shadows with a real blur still use the quad/blur-cache path.
        if blur_radius == 0.0 {
            self.push_zero_blur_box_shadow(
                common,
                box_bounds,
                offset,
                color,
                spread_radius,
                border_radius,
                shadow_radius,
                clip_mode,
            );
            return;
        }

        let (common, eso_offset) = self.normalize_common(common);
        let item = di::DisplayItem::BoxShadow(di::BoxShadowDisplayItem {
            common,
            box_bounds: self.shift_rect(box_bounds, eso_offset),
            offset,
            color,
            blur_radius,
            spread_radius,
            border_radius,
            shadow_radius,
            clip_mode,
        });

        self.push_item(&item);
    }

    /// Desugar a zero-blur box-shadow into a filled rectangle bounded by a
    /// rounded-rect `Clip` and (for the fake-border ring) carved out by an inner
    /// rounded-rect `ClipOut`. This replaces the scene builder's zero-blur fast
    /// path. Rects are left in the caller's layout space; each `define_*`/
    /// `push_rect` call applies the same scroll-offset normalization for
    /// `spatial_id`, so they stay aligned. The inner ClipOut carries the spread
    /// as its snap outset to keep the ring width even under motion (bug 2052033).
    fn push_zero_blur_box_shadow(
        &mut self,
        common: &di::CommonItemProperties,
        box_bounds: LayoutRect,
        offset: LayoutVector2D,
        color: ColorF,
        spread_radius: f32,
        border_radius: di::BorderRadius,
        shadow_radius: di::BorderRadius,
        clip_mode: di::BoxShadowClipMode,
    ) {
        use di::{BoxShadowClipMode, ClipMode, ComplexClipRegion};

        if color.a == 0.0 {
            return;
        }

        // Inset shadows get smaller as spread radius increases.
        let spread_amount = match clip_mode {
            BoxShadowClipMode::Outset => spread_radius,
            BoxShadowClipMode::Inset => -spread_radius,
        };

        // Trivial reject of box-shadows that are not visible.
        if offset == LayoutVector2D::zero() && spread_amount == 0.0 {
            return;
        }

        let shadow_rect = box_bounds
            .translate(offset)
            .inflate(spread_amount, spread_amount);
        let spatial_id = common.spatial_id;

        // A box-shadow's shape is a plain rounded rect, so its radii take the
        // css-backgrounds-3 5.5 overlap reduction. Spread shrinks the rect by
        // twice what it takes off the radii, so a negative spread routinely
        // leaves radii that no longer fit and must be scaled back.
        //
        // Do it here, at the producer. The rounded-rect clips these desugar to
        // are otherwise indistinguishable from the ones `background-clip`
        // produces, and those must *not* be reduced: they trace the inner
        // border edge, which css-backgrounds-3 4.4 defines as concentric with
        // the outer edge (bug 1830603). Chrome draws the same distinction.
        let normalized = |rect: &LayoutRect, radii: di::BorderRadius| {
            let mut radii = radii;
            crate::key_types::ensure_no_corner_overlap(&mut radii, rect.size());
            radii
        };
        let border_radius = normalized(&box_bounds, border_radius);
        let shadow_radius = normalized(&shadow_rect, shadow_radius);

        let shadow_inset = LayoutSideOffsets::new_all_same(-spread_amount);

        let mut clips: Vec<di::ClipId> = Vec::with_capacity(2);
        let (final_prim_rect, clip_radius, clip_inset) = match clip_mode {
            BoxShadowClipMode::Outset => {
                if shadow_rect.is_empty() {
                    return;
                }

                clips.push(self.define_clip_rounded_rect_impl(
                    spatial_id,
                    ComplexClipRegion {
                        rect: box_bounds,
                        radii: border_radius,
                        inset: LayoutSideOffsets::zero(),
                        mode: ClipMode::ClipOut,
                    },
                    spread_radius,
                ));

                (shadow_rect, shadow_radius, shadow_inset)
            }
            BoxShadowClipMode::Inset => {
                if !shadow_rect.is_empty() {
                    clips.push(self.define_clip_rounded_rect_impl(
                        spatial_id,
                        ComplexClipRegion {
                            rect: shadow_rect,
                            radii: shadow_radius,
                            inset: shadow_inset,
                            mode: ClipMode::ClipOut,
                        },
                        spread_radius,
                    ));
                }

                (box_bounds, border_radius, LayoutSideOffsets::zero())
            }
        };

        // Outer Clip matches the rectangle and snaps normally (outset 0).
        clips.push(self.define_clip_rounded_rect_impl(
            spatial_id,
            ComplexClipRegion {
                rect: final_prim_rect,
                radii: clip_radius,
                inset: clip_inset,
                mode: ClipMode::Clip,
            },
            0.0,
        ));

        // Chain the shaping clips on top of the item's own clip chain.
        let parent = (common.clip_chain_id != di::ClipChainId::INVALID)
            .then_some(common.clip_chain_id);
        let clip_chain_id = self.define_clip_chain(parent, clips);

        let rect_common = di::CommonItemProperties {
            clip_rect: common.clip_rect,
            clip_chain_id,
            spatial_id,
            flags: common.flags,
        };
        self.push_rect_with_animation(
            &rect_common,
            final_prim_rect,
            PropertyBinding::Value(color),
        );
    }

    /// Pushes a linear gradient to be displayed.
    ///
    /// The gradient itself is described in the
    /// `gradient` parameter. It is drawn on
    /// a "tile" with the dimensions from `tile_size`.
    /// These tiles are now repeated to the right and
    /// to the bottom infinitely. If `tile_spacing`
    /// is not zero spacers with the given dimensions
    /// are inserted between the tiles as seams.
    ///
    /// The origin of the tiles is given in `layout.rect.origin`.
    /// If the gradient should only be displayed once limit
    /// the `layout.rect.size` to a single tile.
    /// The gradient is only visible within the local clip.
    pub fn push_gradient(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        gradient: di::Gradient,
        tile_size: LayoutSize,
        tile_spacing: LayoutSize,
        stops: &[di::GradientStop],
    ) {
        let (common, offset) = self.normalize_common(common);
        self.push_stops(stops);
        let item = di::DisplayItem::Gradient(di::GradientDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            gradient,
            tile_size,
            tile_spacing,
        });

        self.push_item(&item);
    }

    /// Pushes a radial gradient to be displayed.
    ///
    /// See [`push_gradient`](#method.push_gradient) for explanation.
    pub fn push_radial_gradient(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        gradient: di::RadialGradient,
        tile_size: LayoutSize,
        tile_spacing: LayoutSize,
        stops: &[di::GradientStop],
    ) {
        let (common, offset) = self.normalize_common(common);
        self.push_stops(stops);
        let item = di::DisplayItem::RadialGradient(di::RadialGradientDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            gradient,
            tile_size,
            tile_spacing,
        });

        self.push_item(&item);
    }

    /// Pushes a conic gradient to be displayed.
    ///
    /// See [`push_gradient`](#method.push_gradient) for explanation.
    pub fn push_conic_gradient(
        &mut self,
        common: &di::CommonItemProperties,
        bounds: LayoutRect,
        gradient: di::ConicGradient,
        tile_size: LayoutSize,
        tile_spacing: LayoutSize,
        stops: &[di::GradientStop],
    ) {
        let (common, offset) = self.normalize_common(common);
        self.push_stops(stops);
        let item = di::DisplayItem::ConicGradient(di::ConicGradientDisplayItem {
            common,
            bounds: self.shift_rect(bounds, offset),
            gradient,
            tile_size,
            tile_spacing,
        });

        self.push_item(&item);
    }

    pub fn push_reference_frame(
        &mut self,
        origin: LayoutPoint,
        parent_spatial_id: di::SpatialId,
        transform_style: di::TransformStyle,
        transform: PropertyBinding<LayoutTransform>,
        kind: di::ReferenceFrameKind,
    ) -> di::SpatialId {
        let parent_offset = self.accumulated_scroll_offset(parent_spatial_id);
        let id = self.generate_spatial_index();

        let descriptor = di::SpatialTreeItem::ReferenceFrame(di::ReferenceFrameDescriptor {
            parent_spatial_id,
            origin: self.shift_point(origin, parent_offset),
            reference_frame: di::ReferenceFrame {
                transform_style,
                transform: di::ReferenceTransformBinding::Static {
                    binding: transform,
                },
                kind,
                id,
            },
        });
        self.push_spatial_tree_item(&descriptor);
        // External scroll offset does not propagate across reference frames.
        self.record_scroll_offset(id, AuOffset::ZERO);

        let item = di::DisplayItem::PushReferenceFrame(di::ReferenceFrameDisplayListItem {
        });
        self.push_item(&item);

        id
    }

    pub fn push_computed_frame(
        &mut self,
        origin: LayoutPoint,
        parent_spatial_id: di::SpatialId,
        scale_from: Option<LayoutSize>,
        vertical_flip: bool,
        rotation: di::Rotation,
    ) -> di::SpatialId {
        let parent_offset = self.accumulated_scroll_offset(parent_spatial_id);
        let id = self.generate_spatial_index();

        let descriptor = di::SpatialTreeItem::ReferenceFrame(di::ReferenceFrameDescriptor {
            parent_spatial_id,
            origin: self.shift_point(origin, parent_offset),
            reference_frame: di::ReferenceFrame {
                transform_style: di::TransformStyle::Flat,
                transform: di::ReferenceTransformBinding::Computed {
                    scale_from,
                    vertical_flip,
                    rotation,
                },
                kind: di::ReferenceFrameKind::Transform {
                    is_2d_scale_translation: false,
                    should_snap: false,
                    paired_with_perspective: false,
                },
                id,
            },
        });
        self.push_spatial_tree_item(&descriptor);
        // External scroll offset does not propagate across reference frames.
        self.record_scroll_offset(id, AuOffset::ZERO);

        let item = di::DisplayItem::PushReferenceFrame(di::ReferenceFrameDisplayListItem {
        });
        self.push_item(&item);

        id
    }

    pub fn pop_reference_frame(&mut self) {
        self.push_item(&di::DisplayItem::PopReferenceFrame);
    }

    pub fn push_stacking_context(
        &mut self,
        spatial_id: di::SpatialId,
        prim_flags: di::PrimitiveFlags,
        clip_chain_id: Option<di::ClipChainId>,
        transform_style: di::TransformStyle,
        mix_blend_mode: di::MixBlendMode,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
        raster_space: di::RasterSpace,
        flags: di::StackingContextFlags,
        snapshot: Option<di::SnapshotInfo>
    ) {
        self.push_filters(filters, filter_datas, spatial_id);

        // Resolve this context's raster space against its parent: a `Screen`
        // request inherits the parent, a `Local` request overrides a `Screen`
        // parent, and nested locals take the coarser of the two scales. The
        // resolved value is what goes in the item, so the scene builder does not
        // repeat the walk - see `StackingContext::raster_space`.
        let resolved_raster_space = match (self.raster_space_stack.last(), raster_space) {
            (None, _) => raster_space,
            (Some(parent), di::RasterSpace::Screen) => *parent,
            (Some(di::RasterSpace::Screen), space) => space,
            (Some(di::RasterSpace::Local(parent_scale)), di::RasterSpace::Local(scale)) => {
                di::RasterSpace::Local(parent_scale.max(scale))
            }
        };
        self.raster_space_stack.push(resolved_raster_space);

        let item = di::DisplayItem::PushStackingContext(di::PushStackingContextDisplayItem {
            spatial_id,
            snapshot,
            prim_flags,
            stacking_context: di::StackingContext {
                transform_style,
                mix_blend_mode,
                clip_chain_id,
                raster_space: resolved_raster_space,
                flags,
            },
        });

        self.push_item(&item);
    }

    /// Helper for examples/ code.
    pub fn push_simple_stacking_context(
        &mut self,
        spatial_id: di::SpatialId,
        prim_flags: di::PrimitiveFlags,
    ) {
        self.push_simple_stacking_context_with_filters(
            spatial_id,
            prim_flags,
            &[],
            &[],
        );
    }

    /// Helper for examples/ code.
    pub fn push_simple_stacking_context_with_filters(
        &mut self,
        spatial_id: di::SpatialId,
        prim_flags: di::PrimitiveFlags,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
    ) {
        self.push_stacking_context(
            spatial_id,
            prim_flags,
            None,
            di::TransformStyle::Flat,
            di::MixBlendMode::Normal,
            filters,
            filter_datas,
            di::RasterSpace::Screen,
            di::StackingContextFlags::empty(),
            None,
        );
    }

    pub fn pop_stacking_context(&mut self) {
        self.raster_space_stack.pop().expect("popped more stacking contexts than were pushed");
        self.push_item(&di::DisplayItem::PopStackingContext);
    }

    pub fn push_stops(&mut self, stops: &[di::GradientStop]) {
        if stops.is_empty() {
            return;
        }
        self.push_item(&di::DisplayItem::SetGradientStops);
        self.push_iter(stops);
    }

    pub fn push_backdrop_filter(
        &mut self,
        common: &di::CommonItemProperties,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
    ) {
        // The subregions are normalized against `common.spatial_id`, the same
        // node as the item geometry below. A backdrop filter's picture composites
        // in backdrop-root space instead, but that is a difference of *spatial
        // node*, which frame building maps through the spatial tree
        // (`SurfaceInfo::svgfe_source_map`) - not a reason to author subregion
        // and geometry in different coordinate spaces (bug 1975275).
        self.push_filters(filters, filter_datas, common.spatial_id);

        let (common, _offset) = self.normalize_common(common);
        let item = di::DisplayItem::BackdropFilter(di::BackdropFilterDisplayItem {
            common,
        });
        self.push_item(&item);
    }

    /// Write the filter ops and filter data consumed by the stacking context or
    /// backdrop filter pushed immediately after this.
    ///
    /// SVGFE filter-graph subregions are the only absolutely-positioned filter
    /// geometry, so they are normalized by the accumulated external scroll offset
    /// for `spatial_id`, exactly like the primitives the graph operates on. This
    /// is the only way to push filters, so the normalization cannot be bypassed:
    /// authoring a subregion in a different space from the geometry it applies to
    /// was bug 1975275.
    fn push_filters(
        &mut self,
        filters: &[di::FilterOp],
        filter_datas: &[di::FilterData],
        spatial_id: di::SpatialId,
    ) {
        let offset = self.accumulated_scroll_offset(spatial_id);
        let normalized = if offset.is_zero() {
            // Common case: nothing to shift, so don't clone the filter list.
            None
        } else {
            let grid = self.au_grid;
            let off_grid = &mut self.off_grid_coords;
            let mut filters = filters.to_vec();
            for filter in &mut filters {
                if let Some(node) = filter.svgfe_node_mut() {
                    node.subregion = grid.rect(node.subregion, offset, off_grid);
                }
            }
            Some(filters)
        };
        let filters = normalized.as_deref().unwrap_or(filters);

        if !filters.is_empty() {
            self.push_item(&di::DisplayItem::SetFilterOps);
            self.push_iter(filters);
        }

        for filter_data in filter_datas {
            let func_types = [
                filter_data.func_r_type, filter_data.func_g_type,
                filter_data.func_b_type, filter_data.func_a_type];
            self.push_item(&di::DisplayItem::SetFilterData);
            self.push_iter(func_types);
            self.push_iter(&filter_data.r_values);
            self.push_iter(&filter_data.g_values);
            self.push_iter(&filter_data.b_values);
            self.push_iter(&filter_data.a_values);
        }
    }

    pub fn push_debug(&mut self, val: u32) {
        self.push_item(&di::DisplayItem::DebugMarker(val));
    }

    fn generate_clip_index(&mut self) -> di::ClipId {
        self.next_clip_index += 1;
        di::ClipId(self.next_clip_index - 1, self.pipeline_id)
    }

    fn generate_spatial_index(&mut self) -> di::SpatialId {
        self.next_spatial_index += 1;
        di::SpatialId::new(self.next_spatial_index - 1, self.pipeline_id)
    }

    fn generate_clip_chain_id(&mut self) -> di::ClipChainId {
        self.next_clip_chain_id += 1;
        di::ClipChainId(self.next_clip_chain_id - 1, self.pipeline_id)
    }

    /// Accumulated external scroll offset for `spatial_id` (zero for the
    /// implicit pipeline roots and any untracked node). A single-entry cache
    /// short-circuits the common case of consecutive items sharing a spatial
    /// node, avoiding a hash per item.
    fn accumulated_scroll_offset(&mut self, spatial_id: di::SpatialId) -> AuOffset {
        if let Some((cached_id, cached_offset)) = self.last_scroll_offset {
            if cached_id == spatial_id {
                return cached_offset;
            }
        }
        let offset = self.spatial_offsets
            .get(&spatial_id)
            .copied()
            .unwrap_or(AuOffset::ZERO);
        self.last_scroll_offset = Some((spatial_id, offset));
        offset
    }

    /// Record the accumulated external scroll offset for a freshly-defined
    /// spatial node.
    fn record_scroll_offset(&mut self, spatial_id: di::SpatialId, offset: AuOffset) {
        self.spatial_offsets.insert(spatial_id, offset);
    }

    /// Translate a rect from Gecko's pre-scrolled (painted) coordinates into
    /// the normalized, scroll-invariant space WebRender interns in, by adding
    /// the accumulated external scroll offset for `spatial_id`.
    fn normalize_rect(&mut self, rect: LayoutRect, spatial_id: di::SpatialId) -> LayoutRect {
        let offset = self.accumulated_scroll_offset(spatial_id);
        self.shift_rect(rect, offset)
    }

    /// Apply an accumulated app-unit offset to a rect on this list's grid.
    fn shift_rect(&mut self, rect: LayoutRect, offset: AuOffset) -> LayoutRect {
        if offset.is_zero() {
            return rect;
        }
        let grid = self.au_grid;
        grid.rect(rect, offset, &mut self.off_grid_coords)
    }

    fn shift_point(&mut self, point: LayoutPoint, offset: AuOffset) -> LayoutPoint {
        if offset.is_zero() {
            return point;
        }
        let grid = self.au_grid;
        grid.point(point, offset, &mut self.off_grid_coords)
    }

    /// As `normalize_rect`, but for the common-properties chokepoint: returns a
    /// copy with `clip_rect` normalized, plus the offset to apply to the item's
    /// own geometry (bounds, glyphs, ...).
    fn normalize_common(
        &mut self,
        common: &di::CommonItemProperties,
    ) -> (di::CommonItemProperties, AuOffset) {
        let offset = self.accumulated_scroll_offset(common.spatial_id);
        let mut common = *common;
        common.clip_rect = self.shift_rect(common.clip_rect, offset);
        (common, offset)
    }

    pub fn define_scroll_frame(
        &mut self,
        parent_space: di::SpatialId,
        external_id: di::ExternalScrollId,
        content_rect: LayoutRect,
        frame_rect: LayoutRect,
        external_scroll_offset: LayoutVector2D,
        scroll_offset_generation: APZScrollGeneration,
        has_scroll_linked_effect: HasScrollLinkedEffect,
    ) -> di::SpatialId {
        let parent_offset = self.accumulated_scroll_offset(parent_space);
        let scroll_frame_id = self.generate_spatial_index();
        // Accumulated in app units so the sum down the spatial tree is integral
        // and exact. The offset itself is still sent to WebRender verbatim: it is
        // re-applied at frame time against the *transform*, not against these
        // coordinates, so it must not be altered here (rounding it is what
        // apz.rounded_external_scroll_offset did, and it desynchronised the two
        // halves of the round trip).
        let eso_au = {
            let grid = self.au_grid;
            grid.vec_to_au(external_scroll_offset, &mut self.off_grid_coords)
        };

        // `content_rect`'s origin is discarded by the scene builder (only its
        // size is used), so it needs no normalization.
        let descriptor = di::SpatialTreeItem::ScrollFrame(di::ScrollFrameDescriptor {
            content_rect,
            frame_rect: self.normalize_rect(frame_rect, parent_space),
            parent_space,
            scroll_frame_id,
            external_id,
            external_scroll_offset,
            scroll_offset_generation,
            has_scroll_linked_effect,
        });

        self.push_spatial_tree_item(&descriptor);
        self.record_scroll_offset(scroll_frame_id, parent_offset + eso_au);

        scroll_frame_id
    }

    pub fn define_clip_chain<I>(
        &mut self,
        parent: Option<di::ClipChainId>,
        clips: I,
    ) -> di::ClipChainId
    where
        I: IntoIterator<Item = di::ClipId>,
        I::IntoIter: ExactSizeIterator + Clone,
    {
        let id = self.generate_clip_chain_id();
        self.push_item(&di::DisplayItem::ClipChain(di::ClipChainItem { id, parent }));
        self.push_iter(clips);
        id
    }

    pub fn define_clip_image_mask(
        &mut self,
        spatial_id: di::SpatialId,
        image_mask: di::ImageMask,
        points: &[LayoutPoint],
        fill_rule: di::FillRule,
    ) -> di::ClipId {
        let id = self.generate_clip_index();
        let offset = self.accumulated_scroll_offset(spatial_id);

        let mut image_mask = image_mask;
        image_mask.rect = self.shift_rect(image_mask.rect, offset);

        let item = di::DisplayItem::ImageMaskClip(di::ImageMaskClipDisplayItem {
            id,
            spatial_id,
            image_mask,
            fill_rule,
        });

        // We only need to supply points if there are at least 3, which is the
        // minimum to specify a polygon. BuiltDisplayListIter.next ensures that points
        // are cleared between processing other display items, so we'll correctly get
        // zero points when no SetPoints item has been pushed.
        if points.len() >= 3 {
            self.push_item(&di::DisplayItem::SetPoints);
            if !offset.is_zero() {
                let grid = self.au_grid;
                let off_grid = &mut self.off_grid_coords;
                let shifted: Vec<LayoutPoint> =
                    points.iter().map(|p| grid.point(*p, offset, off_grid)).collect();
                self.push_iter(&shifted);
            } else {
                self.push_iter(points);
            }
        }
        self.push_item(&item);
        id
    }

    pub fn define_clip_rect(
        &mut self,
        spatial_id: di::SpatialId,
        clip_rect: LayoutRect,
    ) -> di::ClipId {
        let id = self.generate_clip_index();

        let item = di::DisplayItem::RectClip(di::RectClipDisplayItem {
            id,
            spatial_id,
            clip_rect: self.normalize_rect(clip_rect, spatial_id),
        });

        self.push_item(&item);
        id
    }

    pub fn define_clip_rounded_rect(
        &mut self,
        spatial_id: di::SpatialId,
        clip: di::ComplexClipRegion,
    ) -> di::ClipId {
        self.define_clip_rounded_rect_impl(spatial_id, clip, 0.0)
    }

    /// As `define_clip_rounded_rect`, but with a `snap_outset` for the internal
    /// zero-blur box-shadow desugar (see `RoundedRectClipDisplayItem`).
    fn define_clip_rounded_rect_impl(
        &mut self,
        spatial_id: di::SpatialId,
        mut clip: di::ComplexClipRegion,
        snap_outset: f32,
    ) -> di::ClipId {
        let id = self.generate_clip_index();

        clip.rect = self.normalize_rect(clip.rect, spatial_id);

        let item = di::DisplayItem::RoundedRectClip(di::RoundedRectClipDisplayItem {
            id,
            spatial_id,
            clip,
            snap_outset,
        });

        self.push_item(&item);
        id
    }

    pub fn define_sticky_frame(
        &mut self,
        parent_spatial_id: di::SpatialId,
        frame_rect: LayoutRect,
        margins: SideOffsets2D<Option<f32>, LayoutPixel>,
        vertical_offset_bounds: di::StickyOffsetBounds,
        horizontal_offset_bounds: di::StickyOffsetBounds,
        previously_applied_offset: LayoutVector2D,
        // TODO: The caller only ever passes an identity transform.
        // Could we pass just an (optional) animation id instead?
        transform: Option<PropertyBinding<LayoutTransform>>
    ) -> di::SpatialId {
        // Fold the sticky frame's already-applied offset into the accumulated
        // offset so the frame rect (and all descendants) are normalized to the
        // item's natural, unstuck position. WebRender then computes the full
        // sticky offset at frame time and no longer needs the applied offset.
        let parent_offset = self.accumulated_scroll_offset(parent_spatial_id);
        // Only used for normalization; not sent to WebRender at all any more.
        let pao_au = {
            let grid = self.au_grid;
            grid.vec_to_au(previously_applied_offset, &mut self.off_grid_coords)
        };
        let node_offset = parent_offset - pao_au;
        let id = self.generate_spatial_index();

        let descriptor = di::SpatialTreeItem::StickyFrame(di::StickyFrameDescriptor {
            parent_spatial_id,
            id,
            bounds: self.shift_rect(frame_rect, node_offset),
            margins,
            vertical_offset_bounds,
            horizontal_offset_bounds,
            transform,
        });

        self.push_spatial_tree_item(&descriptor);
        self.record_scroll_offset(id, node_offset);
        id
    }

    pub fn push_iframe(
        &mut self,
        bounds: LayoutRect,
        clip_rect: LayoutRect,
        space_and_clip: &di::SpaceAndClipInfo,
        pipeline_id: PipelineId,
        ignore_missing_pipeline: bool
    ) {
        let offset = self.accumulated_scroll_offset(space_and_clip.spatial_id);
        let item = di::DisplayItem::Iframe(di::IframeDisplayItem {
            bounds: self.shift_rect(bounds, offset),
            clip_rect: self.shift_rect(clip_rect, offset),
            space_and_clip: *space_and_clip,
            pipeline_id,
            ignore_missing_pipeline,
        });
        self.push_item(&item);
    }

    pub fn push_shadow(
        &mut self,
        space_and_clip: &di::SpaceAndClipInfo,
        shadow: di::Shadow,
        should_inflate: bool,
    ) {
        // Record the shadow as a typed descriptor. The first `push_shadow`
        // opens the scope (makes `pending_shadows` non-empty), so the shadowed
        // content that follows is captured until `pop_all_shadows`, which
        // desugars the shadows into blur stacking contexts. `should_inflate` is
        // carried through to the emitted blur.
        self.pending_shadows.push(PendingShadow {
            space_and_clip: *space_and_clip,
            shadow,
            should_inflate,
        });
    }

    pub fn pop_all_shadows(&mut self) {
        assert!(!self.pending_shadows.is_empty(), "pop_all_shadows without a matching push_shadow");
        self.desugar_shadow_scope();
    }

    /// Desugar the captured shadow scope into standard display items: a blur
    /// stacking context per shadow holding offset/recolored copies of the
    /// shadowable content, followed by the original content unchanged. This
    /// replaces the scene builder's shadow expansion. Subpixel AA is disabled
    /// automatically because a blur is a `Filter` picture, which forces
    /// `SubpixelMode::Deny`, so the stacking context keeps `RasterSpace::Screen`
    /// like the scene builder's shadow picture did.
    fn desugar_shadow_scope(&mut self) {
        // A shadowable/drawable item plus its aux (only text carries glyphs).
        struct DrawEntry {
            item: di::DisplayItem,
            glyphs: Vec<GlyphInstance>,
        }
        enum Parsed {
            // A clip / clip-chain definition. Re-emitted verbatim (preserving
            // its id, which `define_*` would not) *before* the shadows, so both
            // the shadow copies and the originals can reference clips that were
            // defined inside the scope.
            Definition {
                item: di::DisplayItem,
                clip_ids: Vec<di::ClipId>,
                points: Vec<LayoutPoint>,
            },
            Draw(DrawEntry),
        }

        // Take the shadows and the captured content out so `self` is free for
        // re-emission. Emptying `pending_shadows` also closes the scope, so the
        // re-emitted items below go to `items_data` rather than being captured
        // again. The capture buffer is restored (cleared) afterwards for reuse.
        let shadows = mem::take(&mut self.pending_shadows);
        let mut captured = mem::take(&mut self.shadow_capture);
        ensure_red_zone::<di::DisplayItem>(&mut captured);

        let mut parsed: Vec<Parsed> = Vec::new();
        {
            let mut iter = BuiltDisplayListIter::new(&captured);
            while let Some(item) = iter.next() {
                parsed.push(match item.item() {
                    def @ (di::DisplayItem::RectClip(..)
                    | di::DisplayItem::RoundedRectClip(..)
                    | di::DisplayItem::ImageMaskClip(..)
                    | di::DisplayItem::ClipChain(..)) => Parsed::Definition {
                        item: *def,
                        clip_ids: item.clip_chain_items().iter().collect(),
                        points: item.points().iter().collect(),
                    },
                    draw => Parsed::Draw(DrawEntry {
                        item: *draw,
                        glyphs: match draw {
                            di::DisplayItem::Text(..) => item.glyphs().iter().collect(),
                            _ => Vec::new(),
                        },
                    }),
                });
            }
        }

        // 1. Clip / clip-chain definitions, verbatim and in order, before the
        //    shadows so their ids resolve for both copies and originals.
        for p in &parsed {
            if let Parsed::Definition { item, clip_ids, points } = p {
                if !points.is_empty() {
                    self.push_item(&di::DisplayItem::SetPoints);
                    self.push_iter(points);
                }
                self.push_item(item);
                if matches!(item, di::DisplayItem::ClipChain(..)) {
                    self.push_iter(clip_ids);
                }
            }
        }

        // 2. A blur stacking context per shadow, holding offset/recolored copies
        //    of the drawable content.
        for shadow in &shadows {
            let s = &shadow.shadow;
            let std_deviation = s.blur_radius * 0.5;
            let blur = [di::FilterOp::Blur(std_deviation, std_deviation, shadow.should_inflate)];
            let blurred = s.blur_radius > 0.0;
            let filters: &[di::FilterOp] = if blurred { &blur } else { &[] };
            let shadow_mode = if blurred {
                di::GlyphShadowMode::Blurred
            } else {
                di::GlyphShadowMode::Unblurred
            };

            // Clip the blur stacking context (i.e. the picture), not the offset
            // copies inside it. This mirrors the old scene-builder shadow
            // expansion, where the shadow's clip applied to the composited blur
            // picture while the shadowed primitives were rasterized unclipped.
            // For a blurred shadow this is what produces a hard clip edge:
            // clipping the copies *before* the blur would let the blur soften
            // the clip boundary (a coverage seam / bleed), whereas clipping the
            // picture cuts the already-blurred (locally uniform) result.
            let sc_clip = shadow.space_and_clip.clip_chain_id;
            let sc_clip = (sc_clip != di::ClipChainId::INVALID).then_some(sc_clip);

            self.push_stacking_context(
                shadow.space_and_clip.spatial_id,
                di::PrimitiveFlags::default(),
                sc_clip,
                di::TransformStyle::Flat,
                di::MixBlendMode::Normal,
                filters,
                &[],
                di::RasterSpace::Screen,
                di::StackingContextFlags::empty(),
                None,
            );

            for p in &parsed {
                if let Parsed::Draw(entry) = p {
                    if let Some(copy) = Self::shadow_copy_of_item(
                        &entry.item,
                        s.offset,
                        s.color,
                        shadow_mode,
                    ) {
                        self.push_item(&copy);
                        if matches!(copy, di::DisplayItem::Text(..)) {
                            self.push_iter(&entry.glyphs);
                        }
                    }
                }
            }

            self.pop_stacking_context();
        }

        // 3. The original (unshadowed) content, drawn on top of the shadows.
        for p in &parsed {
            if let Parsed::Draw(entry) = p {
                self.push_item(&entry.item);
                if matches!(entry.item, di::DisplayItem::Text(..)) {
                    self.push_iter(&entry.glyphs);
                }
            }
        }

        captured.clear();
        self.shadow_capture = captured;
        // `pending_shadows` was emptied by the take above and is left empty,
        // which closes the shadow scope.
    }

    /// Produce the shadow copy of a shadowable display item: geometry
    /// translated by `offset` and color replaced by `color`. Returns `None` for
    /// item types that cannot cast a shadow (they are dropped from the shadow),
    /// mirroring the scene builder's `CreateShadow` impls.
    ///
    /// Aux data (e.g. text glyphs) is unchanged and re-emitted separately: text
    /// glyphs are stored relative to the bounds origin (see `push_text`), so
    /// they follow the translated bounds without needing to be rewritten here.
    fn shadow_copy_of_item(
        item: &di::DisplayItem,
        offset: LayoutVector2D,
        color: ColorF,
        shadow_mode: di::GlyphShadowMode,
    ) -> Option<di::DisplayItem> {
        use di::DisplayItem::*;

        // Translate the copy by the shadow offset and drop its clip chain (see
        // `desugar_shadow_scope`): the shadow's clip is applied to the enclosing
        // blur picture, so the copy inside must be unclipped, matching the old
        // scene builder (which rasterized the shadowed primitives unclipped and
        // clipped the composited picture).
        let shift = |mut common: di::CommonItemProperties| -> di::CommonItemProperties {
            common.clip_rect = common.clip_rect.translate(offset);
            common.clip_chain_id = di::ClipChainId::INVALID;
            common
        };

        Some(match item {
            Rectangle(info) => Rectangle(di::RectangleDisplayItem {
                common: shift(info.common),
                bounds: info.bounds.translate(offset),
                color: PropertyBinding::Value(color),
            }),
            Text(info) => Text(di::TextDisplayItem {
                common: shift(info.common),
                bounds: info.bounds.translate(offset),
                color,
                shadow: shadow_mode,
                ..*info
            }),
            Image(info) => Image(di::ImageDisplayItem {
                common: shift(info.common),
                bounds: info.bounds.translate(offset),
                color,
                ..*info
            }),
            Line(info) => Line(di::LineDisplayItem {
                common: shift(info.common),
                area: info.area.translate(offset),
                color,
                ..*info
            }),
            Border(info) => {
                // Only normal borders cast a shadow via this path.
                let details = match info.details {
                    di::BorderDetails::Normal(border) => {
                        di::BorderDetails::Normal(border.with_color(color))
                    }
                    di::BorderDetails::NinePatch(_) => return None,
                };
                Border(di::BorderDisplayItem {
                    common: shift(info.common),
                    bounds: info.bounds.translate(offset),
                    details,
                    ..*info
                })
            }
            _ => return None,
        })
    }

    /// Start a display list. `au_per_dev_px` is the caller's app-units-per-device
    /// pixel (Gecko: `nsPresContext::AppUnitsPerDevPixel`), the grid its
    /// coordinates are authored on; scroll offset normalization is done in whole
    /// app units on that grid so it is exact. Taken here rather than at
    /// construction because the builder is reused across paints while the grid
    /// changes with device scale and full zoom.
    pub fn begin(&mut self, au_per_dev_px: f32) {
        assert_eq!(self.state, BuildState::Idle);
        self.state = BuildState::Build;
        self.builder_start_time = zeitstempel::now();
        self.reset();
        self.au_grid = AuGrid::new(au_per_dev_px);
    }

    pub fn end(&mut self) -> (PipelineId, BuiltDisplayList) {
        assert_eq!(self.state, BuildState::Build);
        assert!(self.save_state.is_none(), "Finalized DisplayListBuilder with a pending save");

        if let Some(content) = self.serialized_content_buffer.take() {
            println!("-- WebRender display list for {:?} --\n{}",
                self.pipeline_id, content);
        }

        // Add `DisplayItem::max_size` zone of zeroes to the end of display list
        // so there is at least this amount available in the display list during
        // serialization.
        ensure_red_zone::<di::DisplayItem>(&mut self.payload.items_data);
        ensure_red_zone::<di::SpatialTreeItem>(&mut self.payload.spatial_tree);

        // While the first display list after tab-switch can be large, the
        // following ones are always smaller thanks to interning. We attempt
        // to reserve the same capacity again, although it may fail. Memory
        // pressure events will cause us to release our buffers if we ask for
        // too much. See bug 1531819 for related OOM issues.
        let next_capacity = DisplayListCapacity {
            items_size: self.payload.items_data.len(),
            spatial_tree_size: self.payload.spatial_tree.len(),
        };
        let payload = mem::replace(
            &mut self.payload,
            DisplayListPayload::new(next_capacity),
        );
        let end_time = zeitstempel::now();

        self.state = BuildState::Idle;

        (
            self.pipeline_id,
            BuiltDisplayList {
                descriptor: BuiltDisplayListDescriptor {
                    gecko_display_list_type: GeckoDisplayListType::None,
                    builder_start_time: self.builder_start_time,
                    builder_finish_time: end_time,
                    send_start_time: end_time,
                    total_clip_nodes: self.next_clip_index,
                    total_spatial_nodes: self.next_spatial_index,
                    off_grid_coords: self.off_grid_coords,
                },
                payload,
            },
        )
    }
}

fn iter_spatial_tree<F>(spatial_tree: &[u8], mut f: F) where F: FnMut(&di::SpatialTreeItem) {
    let mut src = spatial_tree;
    let mut item = di::SpatialTreeItem::Invalid;

    while src.len() > di::SpatialTreeItem::max_size() {
        src = peek_from_slice(src, &mut item);
        f(&item);
    }
}
