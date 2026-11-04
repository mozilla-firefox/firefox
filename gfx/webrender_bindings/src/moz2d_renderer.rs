/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#![deny(missing_docs)]

//! Provides the webrender-side implementation of gecko blob images.
//!
//! Pretty much this is just a shim that calls back into Moz2DImageRenderer, but
//! it also handles merging "partial" blob images (see `merge_blob_images`) and
//! registering fonts found in the blob (see `prepare_request`).

use bindings::{wr_moz2d_render_cb, ArcVecU8, ByteSlice, MutByteSlice};
use gecko_profiler::auto_profiler_marker;
use gecko_profiler::gecko_profiler_label;
use rayon::prelude::*;
use rayon::ThreadPool;
use webrender::api::units::{BlobDirtyRect, BlobToDeviceTranslation, DeviceIntRect};
use webrender::api::*;

use euclid::point2;
use std::collections::btree_map::BTreeMap;
use std::collections::hash_map;
use std::collections::hash_map::HashMap;
use std::collections::Bound::Included;
use std::mem;
use std::os::raw::c_void;
use std::ptr;
use std::sync::Arc;

#[cfg(any(target_os = "macos", target_os = "ios"))]
use core_foundation::string::CFString;
#[cfg(any(target_os = "macos", target_os = "ios"))]
use core_graphics::font::CGFont;
#[cfg(any(target_os = "macos", target_os = "ios"))]
use foreign_types::ForeignType;

#[cfg(target_os = "windows")]
use std::ffi::CStr;
#[cfg(target_os = "windows")]
use std::ffi::OsStr;
#[cfg(target_os = "windows")]
use std::iter::FromIterator;
#[cfg(target_os = "windows")]
use std::os::raw::c_char;
#[cfg(target_os = "windows")]
use std::path::PathBuf;
#[cfg(target_os = "windows")]
use winapi::um::errhandlingapi::GetLastError;

#[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
use std::ffi::CString;
#[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
use std::os::unix::ffi::OsStrExt;

/// Local print-debugging utility
macro_rules! dlog {
    ($($e:expr),*) => { {$(let _ = $e;)*} }
    //($($t:tt)*) => { println!($($t)*) }
}

/// Debug prints a blob's item bounds, indicating whether the bounds are dirty or not.
fn dump_bounds(blob: &[u8], dirty_rect: DeviceIntRect) {
    let mut index = BlobReader::new(blob);
    while index.reader.has_more() {
        let e = index.read_entry();
        dlog!(
            "  {:?} {}",
            e.bounds,
            if dirty_rect.contains_box(&e.bounds) { "*" } else { "" }
        );
    }
}

/// Debug prints a blob's metadata.
fn dump_index(blob: &[u8]) {
    let mut index = BlobReader::new(blob);
    // we might get an empty result here because sub groups are not tightly bound
    // and we'll sometimes have display items that end up with empty bounds in
    // the blob image.
    while index.reader.has_more() {
        let e = index.read_entry();
        dlog!("result bounds: {} {} {:?}", e.end, e.extra_end, e.bounds);
    }
}

/// Handles the interpretation and rasterization of gecko-based (moz2d) WR blob images.
pub struct Moz2dBlobImageHandler {
    workers: Arc<ThreadPool>,
    workers_low_priority: Arc<ThreadPool>,
    blob_commands: HashMap<BlobImageKey, BlobCommand>,
    enable_multithreading: bool,
}

/// Transmute some bytes into a value.
///
/// FIXME: kill this with fire and/or do a super robust security audit
unsafe fn convert_from_bytes<T: Copy>(slice: &[u8]) -> T {
    assert!(mem::size_of::<T>() <= slice.len());
    ptr::read_unaligned(slice.as_ptr() as *const T)
}

/// Transmute a value into some bytes.
fn convert_to_bytes<T>(x: &T) -> &[u8] {
    unsafe {
        let ip: *const T = x;
        let bp: *const u8 = ip as *const _;
        ::std::slice::from_raw_parts(bp, mem::size_of::<T>())
    }
}

/// A simple helper for deserializing a bunch of transmuted POD data from bytes.
struct BufReader<'a> {
    /// The buffer to read from.
    buf: &'a [u8],
    /// Where we currently are reading from.
    pos: usize,
}

impl<'a> BufReader<'a> {
    /// Creates a reader over the given input.
    fn new(buf: &'a [u8]) -> BufReader<'a> {
        BufReader { buf, pos: 0 }
    }

    /// Transmute-deserializes a value of type T from the stream.
    ///
    /// !!! SUPER DANGEROUS !!!
    ///
    /// To limit the scope of this unsafety, please don't call this directly.
    /// Make a helper method for each whitelisted type.
    unsafe fn read<T: Copy>(&mut self) -> T {
        let ret = convert_from_bytes(&self.buf[self.pos..]);
        self.pos += mem::size_of::<T>();
        ret
    }

    /// Deserializes a BlobFont.
    fn read_blob_font(&mut self) -> BlobFont {
        unsafe { self.read::<BlobFont>() }
    }

    /// Deserializes a usize.
    fn read_usize(&mut self) -> usize {
        unsafe { self.read::<usize>() }
    }

    /// Deserializes a rectangle.
    fn read_box(&mut self) -> DeviceIntRect {
        unsafe { self.read::<DeviceIntRect>() }
    }

    /// Returns whether the buffer has more data to deserialize.
    fn has_more(&self) -> bool {
        self.pos < self.buf.len()
    }
}

/// Reads the metadata of a blob image.
///
/// Blob stream format:
/// { data[..], index[..], offset in the stream of the index array }
///
/// An 'item' has 'data' and 'extra_data'
///  - In our case the 'data' is the stream produced by DrawTargetRecording
///    and the 'extra_data' includes things like webrender font keys
///
/// The index is an array of entries of the following form:
/// { end, extra_end, bounds }
///
/// - end is the offset of the end of an item's data
///   an item's data goes from the begining of the stream or
///   the begining of the last item til end
/// - extra_end is the offset of the end of an item's extra data
///   an item's extra data goes from 'end' until 'extra_end'
/// - bounds is a set of 4 ints { min.x, min.y, max.x, max.y }
///
/// The offsets in the index should be monotonically increasing.
///
/// Design rationale:
///  - the index is smaller so we append it to the end of the data array
///  during construction. This makes it more likely that we'll fit inside
///  the data Vec
///  - we use indices/offsets instead of sizes to avoid having to deal with any
///  arithmetic that might overflow.
struct BlobReader<'a> {
    /// The buffer of the blob.
    reader: BufReader<'a>,
    /// Where the buffer head is.
    begin: usize,
}

/// The metadata for each display item in a blob image (doesn't match the serialized layout).
///
/// See BlobReader above for detailed docs of the blob image format.
struct Entry {
    /// The bounds of the display item.
    bounds: DeviceIntRect,
    /// Where the item's recorded drawing commands start.
    begin: usize,
    /// Where the item's recorded drawing commands end, and its extra data starts.
    end: usize,
    /// Where the item's extra data ends, and the next item's `begin`.
    extra_end: usize,
}

impl<'a> BlobReader<'a> {
    /// Creates a new BlobReader for the given buffer.
    fn new(buf: &'a [u8]) -> BlobReader<'a> {
        // The offset of the index is at the end of the buffer.
        let index_offset_pos = buf.len() - mem::size_of::<usize>();
        assert!(index_offset_pos < buf.len());
        let index_offset = unsafe { convert_from_bytes::<usize>(&buf[index_offset_pos..]) };

        BlobReader {
            reader: BufReader::new(&buf[index_offset..index_offset_pos]),
            begin: 0,
        }
    }

    /// Reads the next display item's metadata.
    fn read_entry(&mut self) -> Entry {
        let end = self.reader.read_usize();
        let extra_end = self.reader.read_usize();
        let bounds = self.reader.read_box();
        let ret = Entry {
            begin: self.begin,
            end,
            extra_end,
            bounds,
        };
        self.begin = extra_end;
        ret
    }
}

/// Writes new blob images.
///
/// In our case this is the result of merging an old one and a new one
struct BlobWriter {
    /// The buffer that the data and extra data for the items is accumulated.
    data: Vec<u8>,
    /// The buffer that the metadata for the items is accumulated.
    index: Vec<u8>,
}

impl BlobWriter {
    /// Creates an empty BlobWriter.
    fn new() -> BlobWriter {
        BlobWriter {
            data: Vec::new(),
            index: Vec::new(),
        }
    }

    /// Writes a display item to the blob.
    fn new_entry(&mut self, extra_size: usize, bounds: DeviceIntRect, data: &[u8]) {
        self.data.extend_from_slice(data);
        // Write 'end' to the index: the offset where the regular data ends and the extra data starts.
        self.index
            .extend_from_slice(convert_to_bytes(&(self.data.len() - extra_size)));
        // Write 'extra_end' to the index: the offset where the extra data ends.
        self.index.extend_from_slice(convert_to_bytes(&self.data.len()));
        // XXX: we can aggregate these writes
        // Write the bounds to the index.
        self.index.extend_from_slice(convert_to_bytes(&bounds.min.x));
        self.index.extend_from_slice(convert_to_bytes(&bounds.min.y));
        self.index.extend_from_slice(convert_to_bytes(&bounds.max.x));
        self.index.extend_from_slice(convert_to_bytes(&bounds.max.y));
    }

    /// Completes the blob image, producing a single buffer containing it.
    fn finish(mut self) -> Vec<u8> {
        // Append the index to the end of the buffer
        // and then append the offset to the beginning of the index.
        let index_begin = self.data.len();
        self.data.extend_from_slice(&self.index);
        self.data.extend_from_slice(convert_to_bytes(&index_begin));
        self.data
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq, PartialOrd, Ord)]
struct CacheKey {
    x1: i32,
    y1: i32,
    x2: i32,
    y2: i32,
    cache_order: u32,
}

impl CacheKey {
    pub fn new(bounds: DeviceIntRect, cache_order: u32) -> Self {
        CacheKey {
            x1: bounds.min.x,
            y1: bounds.min.y,
            x2: bounds.max.x,
            y2: bounds.max.y,
            cache_order,
        }
    }
}

/// Provides an API for looking up the display items in a blob image by bounds, yielding items
/// with equal bounds in their original relative ordering.
///
/// This is used to implement `merge_blobs_images`.
///
/// We use a BTree as a kind of multi-map, by appending an integer "cache_order" to the key.
/// This lets us use multiple items with matching bounds in the map and allows
/// us to fetch and remove them while retaining the ordering of the original list.
struct CachedReader<'a> {
    /// Wrapped reader.
    reader: BlobReader<'a>,
    /// Cached entries that have been read but not yet requested by our consumer.
    cache: BTreeMap<CacheKey, Entry>,
    /// The current number of internally read display items, used to preserve list order.
    cache_index_counter: u32,
}

impl<'a> CachedReader<'a> {
    /// Creates a new CachedReader.
    pub fn new(buf: &'a [u8]) -> Self {
        CachedReader {
            reader: BlobReader::new(buf),
            cache: BTreeMap::new(),
            cache_index_counter: 0,
        }
    }

    /// Tries to find the given bounds in the cache of internally read items, removing it if found.
    fn take_entry_with_bounds_from_cache(&mut self, bounds: &DeviceIntRect) -> Option<Entry> {
        if self.cache.is_empty() {
            return None;
        }

        let key_to_delete = match self
            .cache
            .range((
                Included(CacheKey::new(*bounds, 0u32)),
                Included(CacheKey::new(*bounds, u32::MAX)),
            ))
            .next()
        {
            Some((&key, _)) => key,
            None => return None,
        };

        Some(
            self.cache
                .remove(&key_to_delete)
                .expect("We just got this key from range, it needs to be present"),
        )
    }

    /// Yields the next item in the blob image with the given bounds.
    ///
    /// If the given bounds aren't found in the blob, this panics. `merge_blob_images` should
    /// avoid this by construction if the blob images are well-formed.
    pub fn next_entry_with_bounds(&mut self, bounds: &DeviceIntRect, ignore_rect: &DeviceIntRect) -> Entry {
        if let Some(entry) = self.take_entry_with_bounds_from_cache(bounds) {
            return entry;
        }

        loop {
            // This will panic if we run through the whole list without finding our bounds.
            let old = self.reader.read_entry();
            if old.bounds == *bounds {
                return old;
            } else if !ignore_rect.contains_box(&old.bounds) {
                self.cache
                    .insert(CacheKey::new(old.bounds, self.cache_index_counter), old);
                self.cache_index_counter += 1;
            }
        }
    }
}

/// Merges a new partial blob image into an existing complete one.
///
/// A blob image represents a recording of the drawing commands needed to render
/// (part of) a display list. A partial blob image is a diff between the old display
/// list and a new one. It contains an entry for every display item in the new list, but
/// the actual drawing commands are missing for any item that isn't strictly contained
/// in the dirty rect. This is possible because not being contained in the dirty
/// rect implies that the item is unchanged between the old and new list, so we can
/// just grab the drawing commands from the old list.
///
/// The dirty rect strictly contains the bounds of every item that has been inserted
/// into or deleted from the old list to create the new list. (For simplicity
/// you may think of any other update as deleting and reinserting the item).
///
/// Partial blobs are based on gecko's "retained display list" system, and
/// in particular rely on one key property: if two items have overlapping bounds
/// and *aren't* contained in the dirty rect, then their relative order in both
/// the old and new list will not change. This lets us uniquely identify a display
/// item using only its bounds and relative order in the list.
///
/// That is, the first non-dirty item in the new list with bounds (10, 15, 100, 100)
/// is *also* the first non-dirty item in the old list with those bounds.
///
/// Note that *every* item contained inside the dirty rect will be fully recorded in
/// the new list, even if it is actually unchanged from the old list.
///
/// All of this together gives us a fairly simple merging algorithm: all we need
/// to do is walk through the new (partial) list, determine which of the two lists
/// has the recording for that item, and copy the recording into the result.
///
/// If an item is contained in the dirty rect, then the new list contains the
/// correct recording for that item, so we always copy it from there. Otherwise, we find
/// the first not-yet-copied item with those bounds in the old list and copy that.
/// Any items found in the old list but not the new one can be safely assumed to
/// have been deleted.
fn merge_blob_images(
    old_buf: &[u8],
    new_buf: &[u8],
    dirty_rect: DeviceIntRect,
    old_visible_rect: DeviceIntRect,
    new_visible_rect: DeviceIntRect,
) -> Vec<u8> {
    let mut result = BlobWriter::new();
    dlog!("dirty rect: {:?}", dirty_rect);
    dlog!("old:");
    dump_bounds(old_buf, dirty_rect);
    dlog!("new:");
    dump_bounds(new_buf, dirty_rect);
    dlog!("old visibile rect: {:?}", old_visible_rect);
    dlog!("new visibile rect: {:?}", new_visible_rect);

    let mut old_reader = CachedReader::new(old_buf);
    let mut new_reader = BlobReader::new(new_buf);
    let preserved_rect = old_visible_rect.intersection_unchecked(&new_visible_rect);

    // Loop over both new and old entries merging them.
    // Both new and old must have the same number of entries that
    // overlap but are not contained by the dirty rect, and they
    // must be in the same order.
    while new_reader.reader.has_more() {
        let new = new_reader.read_entry();
        dlog!("bounds: {} {} {:?}", new.end, new.extra_end, new.bounds);
        let preserved_bounds = new.bounds.intersection_unchecked(&preserved_rect);
        if dirty_rect.contains_box(&preserved_bounds) {
            result.new_entry(new.extra_end - new.end, new.bounds, &new_buf[new.begin..new.extra_end]);
        } else {
            let old = old_reader.next_entry_with_bounds(&new.bounds, &dirty_rect);
            result.new_entry(old.extra_end - old.end, new.bounds, &old_buf[old.begin..old.extra_end])
        }
    }

    // XXX: future work: ensure that items that have been deleted but aren't in the blob's visible
    // rect don't affect the dirty rect -- this allows us to scroll content out of view while only
    // updating the areas where items have been scrolled *into* view. This is very important for
    // the performance of blobs that are larger than the viewport. When this is done this
    // assertion will need to be modified to factor in the visible rect, or removed.

    // Ensure all remaining items will be discarded
    while old_reader.reader.reader.has_more() {
        let old = old_reader.reader.read_entry();
        dlog!("new bounds: {} {} {:?}", old.end, old.extra_end, old.bounds);
        //assert!(dirty_rect.contains_box(&old.bounds));
    }

    //assert!(old_reader.cache.is_empty());

    let result = result.finish();
    dump_index(&result);
    result
}

/// A font used by a blob image.
#[repr(C)]
#[derive(Copy, Clone)]
struct BlobFont {
    /// The font key.
    font_instance_key: FontInstanceKey,
    /// A pointer to the scaled font.
    scaled_font_ptr: u64,
}

/// A blob image and extra data provided by webrender on how to rasterize it.
#[derive(Clone)]
struct BlobCommand {
    /// The blob.
    data: Arc<BlobImageData>,
    /// What part of the blob should be rasterized (visible_rect's top-left corresponds to
    /// (0,0) in the blob's rasterization)
    visible_rect: DeviceIntRect,
    /// The size of the tiles to use in rasterization.
    tile_size: TileSize,
}

struct Job {
    request: BlobImageRequest,
    descriptor: BlobImageDescriptor,
    commands: Arc<BlobImageData>,
    dirty_rect: BlobDirtyRect,
    visible_rect: DeviceIntRect,
    tile_size: TileSize,
    output: MutableTileBuffer,
}

/// Rasterizes gecko blob images.
struct Moz2dBlobRasterizer {
    /// Pool of rasterizers.
    workers: Arc<ThreadPool>,
    /// Pool of low priority rasterizers.
    workers_low_priority: Arc<ThreadPool>,
    /// Blobs to rasterize.
    blob_commands: HashMap<BlobImageKey, BlobCommand>,
    ///
    enable_multithreading: bool,
}

impl AsyncBlobImageRasterizer for Moz2dBlobRasterizer {
    fn rasterize(
        &mut self,
        requests: &[BlobImageParams],
        low_priority: bool,
        tile_pool: &mut BlobTilePool,
    ) -> Vec<(BlobImageRequest, BlobImageResult)> {
        // All we do here is spin up our workers to callback into gecko to replay the drawing commands.
        gecko_profiler_label!(Graphics, Rasterization);
        auto_profiler_marker!(
            "BlobRasterization",
            gecko_profiler::gecko_profiler_category!(Graphics),
            Default::default()
        );

        let requests: Vec<Job> = requests
            .iter()
            .map(|params| {
                let command = &self.blob_commands[&params.request.key];
                let blob = Arc::clone(&command.data);
                assert!(!params.descriptor.rect.is_empty());

                let buf_size = (params.descriptor.rect.area() * params.descriptor.format.bytes_per_pixel()) as usize;

                Job {
                    request: params.request,
                    descriptor: params.descriptor,
                    commands: blob,
                    visible_rect: command.visible_rect,
                    dirty_rect: params.dirty_rect,
                    tile_size: command.tile_size,
                    output: tile_pool.get_buffer(buf_size),
                }
            })
            .collect();

        // If we don't have a lot of blobs it is probably not worth the initial cost
        // of installing work on rayon's thread pool so we do it serially on this thread.
        let should_parallelize = if !self.enable_multithreading {
            false
        } else if low_priority {
            requests.len() > 2
        } else {
            // For high priority requests we don't "risk" the potential priority inversion of
            // dispatching to a thread pool full of low priority jobs unless it is really
            // appealing.
            requests.len() > 4
        };

        let result = if should_parallelize {
            // Parallel version synchronously installs a job on the thread pool which will
            // try to do the work in parallel.
            // This thread is blocked until the thread pool is done doing the work.
            let lambda = || requests.into_par_iter().map(rasterize_blob).collect();
            if low_priority {
                //TODO --bpe runtime flag to A/B test these two
                self.workers_low_priority.install(lambda)
            //self.workers.install(lambda)
            } else {
                self.workers.install(lambda)
            }
        } else {
            requests.into_iter().map(rasterize_blob).collect()
        };

        result
    }
}

// a cross platform wrapper that creates an autorelease pool
// on macOS
fn autoreleasepool<T, F: FnOnce() -> T>(f: F) -> T {
    #[cfg(target_os = "macos")]
    {
        objc::rc::autoreleasepool(f)
    }
    #[cfg(not(target_os = "macos"))]
    {
        f()
    }
}

fn rasterize_blob(mut job: Job) -> (BlobImageRequest, BlobImageResult) {
    gecko_profiler_label!(Graphics, Rasterization);
    let descriptor = job.descriptor;

    let dirty_rect = match job.dirty_rect {
        DirtyRect::Partial(rect) => Some(rect),
        DirtyRect::All => None,
    };
    assert!(!descriptor.rect.is_empty());

    let request = job.request;

    let result = autoreleasepool(|| {
        unsafe {
            if wr_moz2d_render_cb(
                ByteSlice::new(&job.commands[..]),
                request.key.0 .0,
                descriptor.format,
                &descriptor.rect,
                &job.visible_rect,
                job.tile_size,
                &request.tile,
                dirty_rect.as_ref(),
                MutByteSlice::new(job.output.as_mut_slice()),
            ) {
                // We want the dirty rect local to the tile rather than the whole image.
                // TODO(nical): move that up and avoid recomupting the tile bounds in the callback
                let dirty_rect = job.dirty_rect.to_subrect_of(&descriptor.rect);
                let tx: BlobToDeviceTranslation = (-descriptor.rect.min.to_vector()).into();
                let rasterized_rect = tx.transform_box(&dirty_rect);

                Ok(RasterizedBlobImage {
                    rasterized_rect,
                    data: job.output.into_arc(),
                })
            } else {
                panic!("Moz2D replay problem");
            }
        }
    });

    (request, result)
}

impl BlobImageHandler for Moz2dBlobImageHandler {
    fn create_similar(&self) -> Box<dyn BlobImageHandler> {
        Box::new(Self::new(
            Arc::clone(&self.workers),
            Arc::clone(&self.workers_low_priority),
        ))
    }

    fn add(&mut self, key: BlobImageKey, data: Arc<BlobImageData>, visible_rect: &DeviceIntRect, tile_size: TileSize) {
        {
            let index = BlobReader::new(&data);
            assert!(index.reader.has_more());
        }
        self.blob_commands.insert(
            key,
            BlobCommand {
                data: Arc::clone(&data),
                visible_rect: *visible_rect,
                tile_size,
            },
        );
    }

    fn update(
        &mut self,
        key: BlobImageKey,
        data: Arc<BlobImageData>,
        visible_rect: &DeviceIntRect,
        dirty_rect: &BlobDirtyRect,
    ) {
        match self.blob_commands.entry(key) {
            hash_map::Entry::Occupied(mut e) => {
                let command = e.get_mut();
                let dirty_rect = if let DirtyRect::Partial(rect) = *dirty_rect {
                    rect.cast_unit()
                } else {
                    DeviceIntRect {
                        min: point2(i32::MIN, i32::MIN),
                        max: point2(i32::MAX, i32::MAX),
                    }
                };
                command.data = Arc::new(merge_blob_images(
                    &command.data,
                    &data,
                    dirty_rect,
                    command.visible_rect,
                    *visible_rect,
                ));
                command.visible_rect = *visible_rect;
            },
            _ => {
                panic!("missing image key");
            },
        }
    }

    fn delete(&mut self, key: BlobImageKey) {
        self.blob_commands.remove(&key);
    }

    fn create_blob_rasterizer(&mut self) -> Box<dyn AsyncBlobImageRasterizer> {
        Box::new(Moz2dBlobRasterizer {
            workers: Arc::clone(&self.workers),
            workers_low_priority: Arc::clone(&self.workers_low_priority),
            blob_commands: self.blob_commands.clone(),
            enable_multithreading: self.enable_multithreading,
        })
    }

    fn delete_font(&mut self, font: FontKey) {
        unsafe {
            DeleteFontData(font);
        }
    }

    fn delete_font_instance(&mut self, key: FontInstanceKey) {
        unsafe {
            DeleteBlobFont(key);
        }
    }

    fn clear_namespace(&mut self, namespace: IdNamespace) {
        unsafe {
            ClearBlobImageResources(namespace);
        }
    }

    fn prepare_resources(&mut self, resources: &dyn BlobImageResources, requests: &[BlobImageParams]) {
        for params in requests {
            let commands = &self.blob_commands[&params.request.key];
            let blob = Arc::clone(&commands.data);
            self.prepare_request(&blob, resources);
        }
    }

    fn enable_multithreading(&mut self, enable: bool) {
        self.enable_multithreading = enable;
    }
}

use bindings::{WrFontInstanceKey, WrFontKey, WrIdNamespace};

#[allow(improper_ctypes)] // this is needed so that rustc doesn't complain about passing the &Arc<Vec> to an extern function
extern "C" {
    fn HasFontData(key: WrFontKey) -> bool;
    fn AddFontData(key: WrFontKey, data: *const u8, size: usize, index: u32, vec: &ArcVecU8);
    fn AddNativeFontHandle(key: WrFontKey, handle: *mut c_void, size: usize, index: u32);
    fn DeleteFontData(key: WrFontKey);
    fn AddBlobFont(
        instance_key: WrFontInstanceKey,
        font_key: WrFontKey,
        size: f32,
        options: Option<&FontInstanceOptions>,
        platform_options: Option<&FontInstancePlatformOptions>,
        variations: *const FontVariation,
        num_variations: usize,
    );
    fn DeleteBlobFont(key: WrFontInstanceKey);
    fn ClearBlobImageResources(namespace: WrIdNamespace);
    #[cfg(target_os = "windows")]
    fn gfx_wr_set_crash_annotation(annotation: CrashAnnotation, value: *const c_char);
}

impl Moz2dBlobImageHandler {
    /// Create a new BlobImageHandler with the given thread pool.
    pub fn new(workers: Arc<ThreadPool>, workers_low_priority: Arc<ThreadPool>) -> Self {
        Moz2dBlobImageHandler {
            blob_commands: HashMap::new(),
            workers,
            workers_low_priority,
            enable_multithreading: true,
        }
    }

    /// Does early preprocessing of a blob's resources.
    ///
    /// Currently just sets up fonts found in the blob.
    fn prepare_request(&self, blob: &[u8], resources: &dyn BlobImageResources) {
        #[cfg(target_os = "windows")]
        fn maybe_crash_on_no_font_file(font_path: &PathBuf) {
            if !mozbuild::config::NIGHTLY_BUILD {
                return;
            }

            // On Nightly add annotation of the error and font file path then crash. We strip
            // the user's dir if necessary to try to prevent capturing personal information.
            let end_of_path = PathBuf::from_iter(
                font_path
                    .components()
                    .skip_while(|c| !c.as_os_str().eq_ignore_ascii_case(OsStr::new("users")))
                    .skip(2),
            );
            // end_of_path will be empty if we don't find the Users dir.
            let annotation_path = if end_of_path.as_os_str().is_empty() {
                font_path.as_os_str()
            } else {
                end_of_path.as_os_str()
            };
            let annotation_string = format!(
                "Error: {:x} loading: {}",
                unsafe { GetLastError() },
                annotation_path.to_string_lossy()
            );
            unsafe {
                gfx_wr_set_crash_annotation(
                    CrashAnnotation::FontFile,
                    CStr::from_bytes_with_nul(annotation_string.as_bytes())
                        .unwrap()
                        .as_ptr(),
                );
            }
            panic!("Moz2D font file not found");
        }

        #[cfg(target_os = "windows")]
        fn process_native_font_handle(key: FontKey, handle: &NativeFontHandle) {
            if let Some(file) = dwrote::FontFile::new_from_path(&handle.path) {
                if let Ok(face) = file.create_face(handle.index, dwrote::DWRITE_FONT_SIMULATIONS_NONE) {
                    unsafe { AddNativeFontHandle(key, face.as_ptr() as *mut c_void, 0, 0) };
                    return;
                }
            }

            maybe_crash_on_no_font_file(&handle.path);

            // Failed to open the font file? Try to set up a fallback font so that
            // we don't simply crash, although text will be garbage.
            let desc = dwrote::FontDescriptor {
                family_name: "Arial".to_string(),
                weight: dwrote::FontWeight::Regular,
                stretch: dwrote::FontStretch::Normal,
                style: dwrote::FontStyle::Normal,
            };
            // If the returned font is None, give up.
            // (TODO: try other font names? get an arbitrary font by index?)
            let font = dwrote::FontCollection::system()
                .font_from_descriptor(&desc)
                .unwrap()
                .unwrap();
            let face = font.create_font_face();
            unsafe { AddNativeFontHandle(key, face.as_ptr() as *mut c_void, 0, 0) };
        }

        #[cfg(any(target_os = "macos", target_os = "ios"))]
        fn process_native_font_handle(key: FontKey, handle: &NativeFontHandle) {
            let font = match CGFont::from_name(&CFString::new(&handle.name)) {
                Ok(font) => font,
                Err(_) => {
                    // If for some reason we failed to load a font descriptor, then our
                    // only options are to either abort or substitute a fallback font.
                    // It is preferable to use a fallback font instead so that rendering
                    // can at least still proceed in some fashion without erroring.
                    // Lucida Grande is the fallback font in Gecko, so use that here.
                    CGFont::from_name(&CFString::from_static_string("Lucida Grande"))
                        .expect("Failed reading font descriptor and could not load fallback font")
                },
            };
            unsafe { AddNativeFontHandle(key, font.as_ptr() as *mut c_void, 0, 0) };
        }

        #[cfg(not(any(target_os = "macos", target_os = "ios", target_os = "windows")))]
        fn process_native_font_handle(key: FontKey, handle: &NativeFontHandle) {
            let cstr = CString::new(handle.path.as_os_str().as_bytes()).unwrap();
            let size = cstr.to_bytes_with_nul().len();
            unsafe { AddNativeFontHandle(key, cstr.as_ptr() as *mut c_void, size, handle.index) };
        }

        fn process_fonts(
            mut extra_data: BufReader,
            resources: &dyn BlobImageResources,
            unscaled_fonts: &mut Vec<FontKey>,
            scaled_fonts: &mut Vec<FontInstanceKey>,
        ) {
            let font_count = extra_data.read_usize();
            for _ in 0..font_count {
                let font = extra_data.read_blob_font();
                if scaled_fonts.contains(&font.font_instance_key) {
                    continue;
                }
                scaled_fonts.push(font.font_instance_key);
                if let Some(instance) = resources.get_font_instance_data(font.font_instance_key) {
                    if !unscaled_fonts.contains(&instance.font_key) {
                        unscaled_fonts.push(instance.font_key);
                        if !unsafe { HasFontData(instance.font_key) } {
                            let template = resources.get_font_data(instance.font_key).unwrap();
                            match template {
                                FontTemplate::Raw(ref data, ref index) => unsafe {
                                    AddFontData(instance.font_key, data.as_ptr(), data.len(), *index, data);
                                },
                                FontTemplate::Native(ref handle) => {
                                    process_native_font_handle(instance.font_key, handle);
                                },
                            }
                        }
                    }
                    unsafe {
                        AddBlobFont(
                            font.font_instance_key,
                            instance.font_key,
                            instance.size,
                            instance.options.as_ref(),
                            instance.platform_options.as_ref(),
                            instance.variations.as_ptr(),
                            instance.variations.len(),
                        );
                    }
                }
            }
        }

        {
            let mut index = BlobReader::new(blob);
            let mut unscaled_fonts = Vec::new();
            let mut scaled_fonts = Vec::new();
            while index.reader.pos < index.reader.buf.len() {
                let e = index.read_entry();
                process_fonts(
                    BufReader::new(&blob[e.end..e.extra_end]),
                    resources,
                    &mut unscaled_fonts,
                    &mut scaled_fonts,
                );
            }
        }
    }
}
