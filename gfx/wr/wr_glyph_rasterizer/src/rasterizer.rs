/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{FontInstanceData, FontInstanceFlags, FontInstanceKey};
use api::{FontInstanceOptions, FontInstancePlatformOptions};
use api::{FontKey, FontRenderMode, FontSize, FontTemplate, FontVariation};
use api::{ColorU, GlyphIndex, GlyphDimensions, SyntheticItalics};
use api::{IdNamespace, BlobImageResources};
use api::channel::{unbounded_channel, Receiver, Sender};
use api::units::*;
use api::ImageFormat;
use crate::platform::font::FontContext;
use crate::profiler::GlyphRasterizeProfiler;
use crate::types::{FastHashMap, FastHashSet};
use crate::telemetry::Telemetry;
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
use rayon::ThreadPool;
use rayon::prelude::*;
use euclid::approxeq::ApproxEq;
use smallvec::SmallVec;
use std::cmp;
use std::cell::Cell;
use std::hash::{Hash, Hasher};
use std::mem;
use std::ops::Deref;
use std::sync::{Arc, Condvar, Mutex, MutexGuard, Weak};
use std::sync::{RwLock, RwLockReadGuard, RwLockWriteGuard};
use std::sync::atomic::{AtomicBool, Ordering};

pub static GLYPH_FLASHING: AtomicBool = AtomicBool::new(false);
const GLYPH_BATCH_SIZE: usize = 32;

impl FontContexts {
    /// Get access to the font context associated to the current thread.
    pub fn lock_current_context(&self) -> MutexGuard<FontContext> {
        match self.current_worker_id() {
            Some(id) => self.lock_context(id),
            None => self.lock_any_context(),
        }
    }

    pub(in super) fn current_worker_id(&self) -> Option<usize> {
        self.workers.current_thread_index()
    }
}

thread_local! {
    pub static SEED: Cell<u32> = Cell::new(0);
}

// super simple random to avoid dependency on rand
fn random() -> u32 {
    SEED.with(|seed| {
        seed.set(seed.get().wrapping_mul(22695477).wrapping_add(1));
        seed.get()
    })
}

impl GlyphRasterizer {
    pub fn request_glyphs<F>(
        &mut self,
        font: FontInstance,
        glyph_keys: &[GlyphKey],
        mut handle: F,
    )
    where F: FnMut(&GlyphKey) -> bool
    {
        assert!(self.has_font(font.font_key));

        let mut batch_size = 0;

        // select glyphs that have not been requested yet.
        for key in glyph_keys {
            if !handle(key) {
                continue;
            }

            // Increment the total number of glyphs that are pending. This is used to determine
            // later whether to use worker threads for the remaining glyphs during resolve time.
            self.pending_glyph_count += 1;
            self.glyph_request_count += 1;

            // Find a batch container for the font instance for this glyph. Use get_mut to avoid
            // cloning the font instance, since this is the common path.
            match self.pending_glyph_requests.get_mut(&font) {
                Some(container) => {
                    container.push(*key);
                    batch_size = container.len();
                }
                None => {
                    // If no batch exists for this font instance, add the glyph to a new one.
                    self.pending_glyph_requests.insert(
                        font.clone(),
                        smallvec![*key],
                    );
                }
            }
        }

        // If the batch for this font instance is big enough, kick off an async
        // job to start rasterizing these glyphs on other threads now.
        if batch_size >= GLYPH_BATCH_SIZE {
            let container = self.pending_glyph_requests.get_mut(&font).unwrap();
            let glyphs = mem::replace(container, SmallVec::new());
            self.flush_glyph_requests(font, glyphs, true);
        }
    }

    pub fn enable_multithreading(&mut self, enable: bool) {
        self.enable_multithreading = enable;
    }

    /// Internal method to flush a list of glyph requests to a set of worker threads,
    /// or process on this thread if there isn't much work to do (in which case the
    /// overhead of processing these on a thread is unlikely to be a performance win).
    fn flush_glyph_requests(
        &mut self,
        font: FontInstance,
        glyphs: SmallVec<[GlyphKey; 16]>,
        use_workers: bool,
    ) {
        let font = Arc::new(font);
        let font_contexts = Arc::clone(&self.font_contexts);
        self.pending_glyph_jobs += glyphs.len();
        self.pending_glyph_count -= glyphs.len();

        let can_use_r8_format = self.can_use_r8_format;

        // if the number of glyphs is small, do it inline to avoid the threading overhead;
        // send the result into glyph_tx so downstream code can't tell the difference.
        if let Some(thread) = &self.dedicated_thread {
            let tx = self.glyph_tx.clone();
            let _ = thread.tx.send(GlyphRasterMsg::Rasterize { font, glyphs, can_use_r8_format, tx });
        } else if self.enable_multithreading && use_workers {
            // spawn an async task to get off of the render backend thread as early as
            // possible and in that task use rayon's fork join dispatch to rasterize the
            // glyphs in the thread pool.
            profile_scope!("spawning process_glyph jobs");
            let tx = self.glyph_tx.clone();
            self.workers.spawn(move || {
                FontContext::begin_rasterize(&font);
                // If the FontContext supports distributing a font across multiple threads,
                // then use par_iter so different glyphs of the same font are processed on
                // multiple threads.
                if FontContext::distribute_across_threads() {
                    glyphs.par_iter().for_each(|key| {
                        let mut context = font_contexts.lock_current_context();
                        let job_font = font.clone();
                        let job = process_glyph(&mut context, can_use_r8_format, job_font, *key);
                        tx.send(job).unwrap();
                    });
                } else {
                    // For FontContexts that prefer to localize a font to a single thread,
                    // just process all the glyphs on the same worker to avoid contention.
                    for key in glyphs {
                        let mut context = font_contexts.lock_current_context();
                        let job_font = font.clone();
                        let job = process_glyph(&mut context, can_use_r8_format, job_font, key);
                        tx.send(job).unwrap();
                    }
                }
                FontContext::end_rasterize(&font);
            });
        } else {
            FontContext::begin_rasterize(&font);
            for key in glyphs {
                let mut context = font_contexts.lock_current_context();
                let job_font = font.clone();
                let job = process_glyph(&mut context, can_use_r8_format, job_font, key);
            self.glyph_tx.send(job).unwrap();
            }
            FontContext::end_rasterize(&font);
        }
    }

    pub fn resolve_glyphs<F, G>(
        &mut self,
        mut handle: F,
        profile: &mut G,
    )
    where
        F: FnMut(GlyphRasterJob, bool),
        G: GlyphRasterizeProfiler,
    {
        profile.start_time();
        let timer_id = Telemetry::start_rasterize_glyphs_time();

        // Work around the borrow checker, since we call flush_glyph_requests below
        let mut pending_glyph_requests = mem::replace(
            &mut self.pending_glyph_requests,
            FastHashMap::default(),
        );
        // If we have a large amount of remaining work to do, spawn to worker threads,
        // even if that work is shared among a number of different font instances.
        let use_workers = self.pending_glyph_count >= 8;
        for (font, pending_glyphs) in pending_glyph_requests.drain() {
            self.flush_glyph_requests(
                font,
                pending_glyphs,
                use_workers,
            );
        }
        // Restore this so that we don't heap allocate next frame
        self.pending_glyph_requests = pending_glyph_requests;
        debug_assert_eq!(self.pending_glyph_count, 0);
        debug_assert!(self.pending_glyph_requests.is_empty());

        if self.glyph_request_count > 0 {
            profile.set(self.glyph_request_count as f64);
            self.glyph_request_count = 0;
        }

        profile_scope!("resolve_glyphs");
        // TODO: rather than blocking until all pending glyphs are available
        // we could try_recv and steal work from the thread pool to take advantage
        // of the fact that this thread is alive and we avoid the added latency
        // of blocking it.
        let mut jobs = {
            profile_scope!("blocking wait on glyph_rx");
            self.glyph_rx.iter().take(self.pending_glyph_jobs).collect::<Vec<_>>()
        };
        assert_eq!(jobs.len(), self.pending_glyph_jobs, "BUG: Didn't receive all pending glyphs!");
        self.pending_glyph_jobs = 0;

        // Ensure that the glyphs are always processed in the same
        // order for a given text run (since iterating a hash set doesn't
        // guarantee order). This can show up as very small float inaccuracy
        // differences in rasterizers due to the different coordinates
        // that text runs get associated with by the texture cache allocator.
        jobs.sort_by(|a, b| (*a.font).cmp(&*b.font).then(a.key.cmp(&b.key)));

        for job in jobs {
            handle(job, self.can_use_r8_format);
        }

        // Now that we are done with the critical path (rendering the glyphs),
        // we can schedule removing the fonts if needed.
        self.remove_dead_fonts();

        Telemetry::stop_and_accumulate_rasterize_glyphs_time(timer_id);
        profile.end_time();
    }
}

#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct FontTransform {
    pub scale_x: f32,
    pub skew_x: f32,
    pub skew_y: f32,
    pub scale_y: f32,
}

// Floats don't impl Hash/Eq/Ord...
impl Eq for FontTransform {}
impl Ord for FontTransform {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.partial_cmp(other).unwrap_or(cmp::Ordering::Equal)
    }
}
impl Hash for FontTransform {
    fn hash<H: Hasher>(&self, state: &mut H) {
        // Note: this is inconsistent with the Eq impl for -0.0 (don't care).
        self.scale_x.to_bits().hash(state);
        self.skew_x.to_bits().hash(state);
        self.skew_y.to_bits().hash(state);
        self.scale_y.to_bits().hash(state);
    }
}

impl FontTransform {
    const QUANTIZE_SCALE: f32 = 1024.0;

    pub fn new(scale_x: f32, skew_x: f32, skew_y: f32, scale_y: f32) -> Self {
        FontTransform { scale_x, skew_x, skew_y, scale_y }
    }

    pub fn identity() -> Self {
        FontTransform::new(1.0, 0.0, 0.0, 1.0)
    }

    #[allow(dead_code)]
    pub fn is_identity(&self) -> bool {
        *self == FontTransform::identity()
    }

    pub fn quantize(&self) -> Self {
        FontTransform::new(
            (self.scale_x * Self::QUANTIZE_SCALE).round() / Self::QUANTIZE_SCALE,
            (self.skew_x * Self::QUANTIZE_SCALE).round() / Self::QUANTIZE_SCALE,
            (self.skew_y * Self::QUANTIZE_SCALE).round() / Self::QUANTIZE_SCALE,
            (self.scale_y * Self::QUANTIZE_SCALE).round() / Self::QUANTIZE_SCALE,
        )
    }

    #[allow(dead_code)]
    pub fn determinant(&self) -> f64 {
        self.scale_x as f64 * self.scale_y as f64 - self.skew_y as f64 * self.skew_x as f64
    }

    #[allow(dead_code)]
    pub fn compute_scale(&self) -> Option<(f64, f64)> {
        let det = self.determinant();
        if det != 0.0 {
            let x_scale = (self.scale_x as f64).hypot(self.skew_y as f64);
            let y_scale = det.abs() / x_scale;
            Some((x_scale, y_scale))
        } else {
            None
        }
    }

    #[allow(dead_code)]
    pub fn pre_scale(&self, scale_x: f32, scale_y: f32) -> Self {
        FontTransform::new(
            self.scale_x * scale_x,
            self.skew_x * scale_y,
            self.skew_y * scale_x,
            self.scale_y * scale_y,
        )
    }

    #[allow(dead_code)]
    pub fn scale(&self, scale: f32) -> Self { self.pre_scale(scale, scale) }

    #[allow(dead_code)]
    pub fn invert_scale(&self, x_scale: f64, y_scale: f64) -> Self {
        self.pre_scale(x_scale.recip() as f32, y_scale.recip() as f32)
    }

    pub fn synthesize_italics(&self, angle: SyntheticItalics, size: f64, vertical: bool) -> (Self, (f64, f64)) {
        let skew_factor = angle.to_skew();
        if vertical {
          // origin delta to be applied so that we effectively skew around
          // the middle rather than edge of the glyph
          let (tx, ty) = (0.0, -size * 0.5 * skew_factor as f64);
          (FontTransform::new(
              self.scale_x + self.skew_x * skew_factor,
              self.skew_x,
              self.skew_y + self.scale_y * skew_factor,
              self.scale_y,
          ), (self.scale_x as f64 * tx + self.skew_x as f64 * ty,
              self.skew_y as f64 * tx + self.scale_y as f64 * ty))
        } else {
          (FontTransform::new(
              self.scale_x,
              self.skew_x - self.scale_x * skew_factor,
              self.skew_y,
              self.scale_y - self.skew_y * skew_factor,
          ), (0.0, 0.0))
        }
    }

    pub fn swap_xy(&self) -> Self {
        FontTransform::new(self.skew_x, self.scale_x, self.scale_y, self.skew_y)
    }

    pub fn flip_x(&self) -> Self {
        FontTransform::new(-self.scale_x, self.skew_x, -self.skew_y, self.scale_y)
    }

    pub fn flip_y(&self) -> Self {
        FontTransform::new(self.scale_x, -self.skew_x, self.skew_y, -self.scale_y)
    }

    pub fn transform(&self, point: &LayoutPoint) -> DevicePoint {
        DevicePoint::new(
            self.scale_x * point.x + self.skew_x * point.y,
            self.skew_y * point.x + self.scale_y * point.y,
        )
    }

    pub fn get_subpx_dir(&self) -> SubpixelDirection {
        if self.skew_y.approx_eq(&0.0) {
            // The X axis is not projected onto the Y axis
            SubpixelDirection::Horizontal
        } else if self.scale_x.approx_eq(&0.0) {
            // The X axis has been swapped with the Y axis
            SubpixelDirection::Vertical
        } else {
            // Both axes are projected onto each other (rotation / skew), so no
            // single axis can carry the sub-pixel offset.
            SubpixelDirection::Mixed
        }
    }
}

impl<'a> From<&'a LayoutToWorldTransform> for FontTransform {
    fn from(xform: &'a LayoutToWorldTransform) -> Self {
        FontTransform::new(xform.m11, xform.m21, xform.m12, xform.m22)
    }
}

// Some platforms (i.e. Windows) may have trouble rasterizing glyphs above this size.
// Ensure glyph sizes are reasonably limited to avoid that scenario.
pub const FONT_SIZE_LIMIT: f32 = 320.0;

/// Immutable description of a font instance's shared state.
///
/// `BaseFontInstance` can be identified by a `FontInstanceKey` to avoid hashing it.
#[derive(Clone, Debug, Ord, PartialOrd, MallocSizeOf)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct BaseFontInstance {
    ///
    pub instance_key: FontInstanceKey,
    ///
    pub font_key: FontKey,
    ///
    pub size: FontSize,
    ///
    pub options: FontInstanceOptions,
    ///
    #[cfg_attr(any(feature = "capture", feature = "replay"), serde(skip))]
    pub platform_options: Option<FontInstancePlatformOptions>,
    ///
    pub variations: Vec<FontVariation>,
}

impl BaseFontInstance {
    pub fn new(
        instance_key: FontInstanceKey,
        font_key: FontKey,
        size: f32,
        options: Option<FontInstanceOptions>,
        platform_options: Option<FontInstancePlatformOptions>,
        variations: Vec<FontVariation>,
    ) -> Self {
        BaseFontInstance {
            instance_key,
            font_key,
            size: size.into(),
            options: options.unwrap_or_default(),
            platform_options,
            variations,
        }
    }
}

impl Deref for BaseFontInstance {
    type Target = FontInstanceOptions;
    fn deref(&self) -> &FontInstanceOptions {
        &self.options
    }
}

impl Hash for BaseFontInstance {
    fn hash<H: Hasher>(&self, state: &mut H) {
        // Skip the instance key.
        self.font_key.hash(state);
        self.size.hash(state);
        self.options.hash(state);
        self.platform_options.hash(state);
        self.variations.hash(state);
    }
}

impl PartialEq for BaseFontInstance {
    fn eq(&self, other: &BaseFontInstance) -> bool {
        // Skip the instance key.
        self.font_key == other.font_key &&
            self.size == other.size &&
            self.options == other.options &&
            self.platform_options == other.platform_options &&
            self.variations == other.variations
    }
}
impl Eq for BaseFontInstance {}

struct MappedFontKey {
    font_key: FontKey,
    template: FontTemplate,
}

struct FontKeyMapLocked {
    namespace: IdNamespace,
    next_id: u32,
    template_map: FastHashMap<FontTemplate, Arc<MappedFontKey>>,
    key_map: FastHashMap<FontKey, Arc<MappedFontKey>>,
}

/// A shared map from fonts key local to a namespace to shared font keys that
/// can be shared across many namespaces. Local keys are tracked in a hashmap
/// that stores a strong reference per mapping so that their count can be
/// tracked. A map of font templates is used to hash font templates to their
/// final shared key. The shared key will stay alive so long as there are
/// any strong references to the mapping entry. Care must be taken when
/// clearing namespaces of shared keys as this may trigger shared font keys
/// to expire which require individual processing. Shared font keys will be
/// created within the provided unique namespace.
#[derive(Clone)]
pub struct FontKeyMap(Arc<RwLock<FontKeyMapLocked>>);

impl FontKeyMap {
    pub fn new(namespace: IdNamespace) -> Self {
        FontKeyMap(Arc::new(RwLock::new(FontKeyMapLocked {
            namespace,
            next_id: 1,
            template_map: FastHashMap::default(),
            key_map: FastHashMap::default(),
        })))
    }

    fn lock(&self) -> RwLockReadGuard<FontKeyMapLocked> {
        self.0.read().unwrap()
    }

    fn lock_mut(&mut self) -> RwLockWriteGuard<FontKeyMapLocked> {
        self.0.write().unwrap()
    }

    pub fn keys(&self) -> Vec<FontKey> {
        self.lock().key_map.keys().cloned().collect()
    }

    pub fn map_key(&self, font_key: &FontKey) -> FontKey {
        match self.lock().key_map.get(font_key) {
            Some(mapped) => mapped.font_key,
            None => *font_key,
        }
    }

    pub fn add_key(&mut self, font_key: &FontKey, template: &FontTemplate) -> Option<FontKey> {
        let mut locked = self.lock_mut();
        if locked.key_map.contains_key(font_key) {
            return None;
        }
        if let Some(mapped) = locked.template_map.get(template).cloned() {
            locked.key_map.insert(*font_key, mapped);
            return None;
        }
        let shared_key = FontKey::new(locked.namespace, locked.next_id);
        locked.next_id += 1;
        let mapped = Arc::new(MappedFontKey {
            font_key: shared_key,
            template: template.clone(),
        });
        locked.template_map.insert(template.clone(), mapped.clone());
        locked.key_map.insert(*font_key, mapped);
        Some(shared_key)
    }

    pub fn delete_key(&mut self, font_key: &FontKey) -> Option<FontKey> {
        let mut locked = self.lock_mut();
        let mapped = match locked.key_map.remove(font_key) {
            Some(mapped) => mapped,
            None => return Some(*font_key),
        };
        if Arc::strong_count(&mapped) <= 2 {
            // Only the last mapped key and template map point to it.
            locked.template_map.remove(&mapped.template);
            Some(mapped.font_key)
        } else {
            None
        }
    }

    pub fn clear_namespace(&mut self, namespace: IdNamespace) -> Vec<FontKey> {
        let mut locked = self.lock_mut();
        locked.key_map.retain(|key, _| {
            if key.0 == namespace {
                false
            } else {
                true
            }
        });
        let mut deleted_keys = Vec::new();
        locked.template_map.retain(|_, mapped| {
            if Arc::strong_count(mapped) <= 1 {
                // Only the template map points to it.
                deleted_keys.push(mapped.font_key);
                false
            } else {
                true
            }
        });
        deleted_keys
    }
}

type FontTemplateMapLocked = FastHashMap<FontKey, FontTemplate>;

/// A map of font keys to font templates that might hold both namespace-local
/// font templates as well as shared templates.
#[derive(Clone)]
pub struct FontTemplateMap(Arc<RwLock<FontTemplateMapLocked>>);

impl FontTemplateMap {
    pub fn new() -> Self {
        FontTemplateMap(Arc::new(RwLock::new(FastHashMap::default())))
    }

    pub fn lock(&self) -> RwLockReadGuard<FontTemplateMapLocked> {
        self.0.read().unwrap()
    }

    fn lock_mut(&mut self) -> RwLockWriteGuard<FontTemplateMapLocked> {
        self.0.write().unwrap()
    }

    pub fn clear(&mut self) {
        self.lock_mut().clear();
    }

    pub fn len(&self) -> usize {
        self.lock().len()
    }

    pub fn has_font(&self, key: &FontKey) -> bool {
        self.lock().contains_key(key)
    }

    pub fn get_font(&self, key: &FontKey) -> Option<FontTemplate> {
        self.lock().get(key).cloned()
    }

    pub fn add_font(&mut self, key: FontKey, template: FontTemplate) -> bool {
        self.lock_mut().insert(key, template).is_none()
    }

    pub fn delete_font(&mut self, key: &FontKey) -> Option<FontTemplate> {
        self.lock_mut().remove(key)
    }

    pub fn delete_fonts(&mut self, keys: &[FontKey]) {
        if !keys.is_empty() {
            let mut map = self.lock_mut();
            for key in keys {
                map.remove(key);
            }
        }
    }

    pub fn clear_namespace(&mut self, namespace: IdNamespace) -> Vec<FontKey> {
        let mut deleted_keys = Vec::new();
        self.lock_mut().retain(|key, _| {
            if key.0 == namespace {
                deleted_keys.push(*key);
                false
            } else {
                true
            }
        });
        deleted_keys
    }
}

struct FontInstanceKeyMapLocked {
    namespace: IdNamespace,
    next_id: u32,
    instances: FastHashSet<Arc<BaseFontInstance>>,
    key_map: FastHashMap<FontInstanceKey, Weak<BaseFontInstance>>,
}

/// A map of namespace-local font instance keys to shared keys. Weak references
/// are used to track the liveness of each key mapping as other consumers of
/// BaseFontInstance might hold strong references to the entry. A mapping from
/// BaseFontInstance to the shared key is then used to determine which shared
/// key to assign to that instance. When the weak count of the mapping is zero,
/// the entry is allowed to expire. Again, care must be taken when clearing
/// a namespace within the key map as it may cause shared key expirations that
/// require individual processing. Shared instance keys will be created within
/// the provided unique namespace.
#[derive(Clone)]
pub struct FontInstanceKeyMap(Arc<RwLock<FontInstanceKeyMapLocked>>);

impl FontInstanceKeyMap {
    pub fn new(namespace: IdNamespace) -> Self {
        FontInstanceKeyMap(Arc::new(RwLock::new(FontInstanceKeyMapLocked {
            namespace,
            next_id: 1,
            instances: FastHashSet::default(),
            key_map: FastHashMap::default(),
        })))
    }

    fn lock(&self) -> RwLockReadGuard<FontInstanceKeyMapLocked> {
        self.0.read().unwrap()
    }

    fn lock_mut(&mut self) -> RwLockWriteGuard<FontInstanceKeyMapLocked> {
        self.0.write().unwrap()
    }

    pub fn keys(&self) -> Vec<FontInstanceKey> {
        self.lock().key_map.keys().cloned().collect()
    }

    pub fn map_key(&self, key: &FontInstanceKey) -> FontInstanceKey {
        match self.lock().key_map.get(key).and_then(|weak| weak.upgrade()) {
            Some(mapped) => mapped.instance_key,
            None => *key,
        }
    }

    pub fn add_key(&mut self, mut instance: BaseFontInstance) -> Option<Arc<BaseFontInstance>> {
        let mut locked = self.lock_mut();
        if locked.key_map.contains_key(&instance.instance_key) {
            return None;
        }
        if let Some(weak) = locked.instances.get(&instance).map(|mapped| Arc::downgrade(mapped)) {
            locked.key_map.insert(instance.instance_key, weak);
            return None;
        }
        let unmapped_key = instance.instance_key;
        instance.instance_key = FontInstanceKey::new(locked.namespace, locked.next_id);
        locked.next_id += 1;
        let shared_instance = Arc::new(instance);
        locked.instances.insert(shared_instance.clone());
        locked.key_map.insert(unmapped_key, Arc::downgrade(&shared_instance));
        Some(shared_instance)
    }

    pub fn delete_key(&mut self, key: &FontInstanceKey) -> Option<FontInstanceKey> {
        let mut locked = self.lock_mut();
        let mapped = match locked.key_map.remove(key).and_then(|weak| weak.upgrade()) {
            Some(mapped) => mapped,
            None => return Some(*key),
        };
        if Arc::weak_count(&mapped) == 0 {
            // Only the instance set points to it.
            locked.instances.remove(&mapped);
            Some(mapped.instance_key)
        } else {
            None
        }
    }

    pub fn clear_namespace(&mut self, namespace: IdNamespace) -> Vec<FontInstanceKey> {
        let mut locked = self.lock_mut();
        locked.key_map.retain(|key, _| {
            if key.0 == namespace {
                false
            } else {
                true
            }
        });
        let mut deleted_keys = Vec::new();
        locked.instances.retain(|mapped| {
            if Arc::weak_count(mapped) == 0 {
                // Only the instance set points to it.
                deleted_keys.push(mapped.instance_key);
                false
            } else {
                true
            }
        });
        deleted_keys
    }
}

type FontInstanceMapLocked = FastHashMap<FontInstanceKey, Arc<BaseFontInstance>>;

/// A map of font instance data accessed concurrently from multiple threads.
#[derive(Clone)]
pub struct FontInstanceMap(Arc<RwLock<FontInstanceMapLocked>>);

impl FontInstanceMap {
    /// Creates an empty shared map.
    pub fn new() -> Self {
        FontInstanceMap(Arc::new(RwLock::new(FastHashMap::default())))
    }

    /// Acquires a read lock on the shared map.
    pub fn lock(&self) -> RwLockReadGuard<FontInstanceMapLocked> {
        self.0.read().unwrap()
    }

    /// Acquires a read lock on the shared map.
    fn lock_mut(&mut self) -> RwLockWriteGuard<FontInstanceMapLocked> {
        self.0.write().unwrap()
    }

    ///
    pub fn clear(&mut self) {
        self.lock_mut().clear();
    }

    ///
    pub fn get_font_instance_data(&self, key: FontInstanceKey) -> Option<FontInstanceData> {
        match self.lock().get(&key) {
            Some(instance) => Some(FontInstanceData {
                font_key: instance.font_key,
                size: instance.size.into(),
                options: Some(FontInstanceOptions {
                  render_mode: instance.render_mode,
                  flags: instance.flags,
                  synthetic_italics: instance.synthetic_italics,
                  _padding: 0,
                }),
                platform_options: instance.platform_options,
                variations: instance.variations.clone(),
            }),
            None => None,
        }
    }

    ///
    pub fn get_font_instance(&self, instance_key: FontInstanceKey) -> Option<Arc<BaseFontInstance>> {
        let instance_map = self.lock();
        instance_map.get(&instance_key).cloned()
    }

    ///
    pub fn add_font_instance(&mut self, instance: Arc<BaseFontInstance>) {
        self.lock_mut().insert(instance.instance_key, instance);
    }

    ///
    pub fn delete_font_instance(&mut self, instance_key: FontInstanceKey) {
        self.lock_mut().remove(&instance_key);
    }

    ///
    pub fn delete_font_instances(&mut self, keys: &[FontInstanceKey]) {
        if !keys.is_empty() {
            let mut map = self.lock_mut();
            for key in keys {
                map.remove(key);
            }
        }
    }

    ///
    pub fn clear_namespace(&mut self, namespace: IdNamespace) {
        self.lock_mut().retain(|key, _| key.0 != namespace);
    }
}

/// Shared font resources that may need to be passed between multiple threads
/// such as font templates and font instances. They are individually protected
/// by locks to ensure safety.
#[derive(Clone)]
pub struct SharedFontResources {
    pub templates: FontTemplateMap,
    pub instances: FontInstanceMap,
    pub font_keys: FontKeyMap,
    pub instance_keys: FontInstanceKeyMap,
}

impl SharedFontResources {
    pub fn new(namespace: IdNamespace) -> Self {
        SharedFontResources {
            templates: FontTemplateMap::new(),
            instances: FontInstanceMap::new(),
            font_keys: FontKeyMap::new(namespace),
            instance_keys: FontInstanceKeyMap::new(namespace),
        }
    }
}

impl BlobImageResources for SharedFontResources {
    fn get_font_data(&self, key: FontKey) -> Option<FontTemplate> {
        let shared_key = self.font_keys.map_key(&key);
        self.templates.get_font(&shared_key)
    }

    fn get_font_instance_data(&self, key: FontInstanceKey) -> Option<FontInstanceData> {
        let shared_key = self.instance_keys.map_key(&key);
        self.instances.get_font_instance_data(shared_key)
    }
}

/// A mutable font instance description.
///
/// Performance is sensitive to the size of this structure, so it should only contain
/// the fields that we need to modify from the original base font instance.
#[derive(Clone, Debug, Ord, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct FontInstance {
    pub base: Arc<BaseFontInstance>,
    pub transform: FontTransform,
    pub render_mode: FontRenderMode,
    pub flags: FontInstanceFlags,
    pub color: ColorU,
    // The font size is in *device/raster* pixels, not logical pixels.
    // It is stored as an f32 since we need sub-pixel sizes.
    pub size: FontSize,
}

impl Hash for FontInstance {
    fn hash<H: Hasher>(&self, state: &mut H) {
        // Hash only the base instance's key to avoid the cost of hashing
        // the rest.
        self.base.instance_key.hash(state);
        self.transform.hash(state);
        self.render_mode.hash(state);
        self.flags.hash(state);
        self.color.hash(state);
        self.size.hash(state);
    }
}

impl PartialEq for FontInstance {
    fn eq(&self, other: &FontInstance) -> bool {
        // Compare only the base instance's key.
        self.base.instance_key == other.base.instance_key &&
            self.transform == other.transform &&
            self.render_mode == other.render_mode &&
            self.flags == other.flags &&
            self.color == other.color &&
            self.size == other.size
    }
}
impl Eq for FontInstance {}

impl Deref for FontInstance {
    type Target = BaseFontInstance;
    fn deref(&self) -> &BaseFontInstance {
        self.base.as_ref()
    }
}

impl MallocSizeOf for  FontInstance {
    fn size_of(&self, _ops: &mut MallocSizeOfOps) -> usize { 0 }
}

impl FontInstance {
    pub fn new(
        base: Arc<BaseFontInstance>,
        color: ColorU,
        render_mode: FontRenderMode,
        flags: FontInstanceFlags,
    ) -> Self {
        FontInstance {
            transform: FontTransform::identity(),
            color,
            size: base.size,
            base,
            render_mode,
            flags,
        }
    }

    pub fn from_base(
        base: Arc<BaseFontInstance>,
    ) -> Self {
        let color = ColorU::new(0, 0, 0, 255);
        let render_mode = base.render_mode;
        let flags = base.flags;
        Self::new(base, color, render_mode, flags)
    }

    pub fn use_texture_padding(&self) -> bool {
        self.flags.contains(FontInstanceFlags::TEXTURE_PADDING)
    }

    pub fn use_transform_glyphs(&self) -> bool {
        self.flags.contains(FontInstanceFlags::TRANSFORM_GLYPHS)
    }

    pub fn get_alpha_glyph_format(&self) -> GlyphFormat {
        if self.use_transform_glyphs() { GlyphFormat::TransformedAlpha } else { GlyphFormat::Alpha }
    }

    pub fn get_subpixel_glyph_format(&self) -> GlyphFormat {
        if self.use_transform_glyphs() { GlyphFormat::TransformedSubpixel } else { GlyphFormat::Subpixel }
    }

    pub fn disable_subpixel_aa(&mut self) {
        self.render_mode = self.render_mode.limit_by(FontRenderMode::Alpha);
    }

    pub fn disable_subpixel_position(&mut self) {
        self.flags.remove(FontInstanceFlags::SUBPIXEL_POSITION);
    }

    pub fn use_subpixel_position(&self) -> bool {
        self.flags.contains(FontInstanceFlags::SUBPIXEL_POSITION) &&
        self.render_mode != FontRenderMode::Mono
    }

    pub fn get_subpx_dir(&self) -> SubpixelDirection {
        if self.use_subpixel_position() {
            let mut subpx_dir = self.transform.get_subpx_dir();
            if self.flags.contains(FontInstanceFlags::TRANSPOSE) {
                subpx_dir = subpx_dir.swap_xy();
            }
            subpx_dir
        } else {
            SubpixelDirection::None
        }
    }

    #[allow(dead_code)]
    pub fn get_subpx_offset(&self, glyph: &GlyphKey) -> (f64, f64) {
        if self.use_subpixel_position() {
            let (dx, dy) = glyph.subpixel_offset();
            (dx.into(), dy.into())
        } else {
            (0.0, 0.0)
        }
    }

    #[allow(dead_code)]
    pub fn get_glyph_format(&self) -> GlyphFormat {
        match self.render_mode {
            FontRenderMode::Mono | FontRenderMode::Alpha => self.get_alpha_glyph_format(),
            FontRenderMode::Subpixel => self.get_subpixel_glyph_format(),
        }
    }

    #[allow(dead_code)]
    pub fn get_extra_strikes(&self, flags: FontInstanceFlags, x_scale: f64) -> usize {
        if self.flags.intersects(flags) {
            let mut bold_offset = self.size.to_f64_px() / 48.0;
            if bold_offset < 1.0 {
                bold_offset = 0.25 + 0.75 * bold_offset;
            }
            (bold_offset * x_scale).max(1.0).round() as usize
        } else {
            0
        }
    }

    pub fn synthesize_italics(&self, transform: FontTransform, size: f64) -> (FontTransform, (f64, f64)) {
        transform.synthesize_italics(self.synthetic_italics, size, self.flags.contains(FontInstanceFlags::VERTICAL))
    }

    #[allow(dead_code)]
    pub fn get_transformed_size(&self) -> f64 {
        let (_, y_scale) = self.transform.compute_scale().unwrap_or((1.0, 1.0));
        self.size.to_f64_px() * y_scale
    }
}

#[repr(u32)]
#[derive(Copy, Clone, Hash, PartialEq, Eq, Debug, Ord, PartialOrd)]
pub enum SubpixelDirection {
    None = 0,
    Horizontal,
    Vertical,
    /// A rotated or skewed transform, where neither axis alone can carry the
    /// sub-pixel offset. Both axes get quarter-pixel positioning, so the run
    /// snaps to a quarter-pixel grid rather than popping a whole device pixel at
    /// a time (bug 2063377). Costs 16 raster variants per glyph.
    Mixed,
}

impl SubpixelDirection {
    // Limit the subpixel direction to what is supported by the glyph format.
    pub fn limit_by(self, glyph_format: GlyphFormat) -> Self {
        match glyph_format {
            GlyphFormat::Bitmap |
            GlyphFormat::ColorBitmap => SubpixelDirection::None,
            _ => self,
        }
    }

    pub fn swap_xy(self) -> Self {
        match self {
            SubpixelDirection::None | SubpixelDirection::Mixed => self,
            SubpixelDirection::Horizontal => SubpixelDirection::Vertical,
            SubpixelDirection::Vertical => SubpixelDirection::Horizontal,
        }
    }
}

#[repr(u8)]
#[derive(Hash, Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub enum SubpixelOffset {
    Zero = 0,
    Quarter = 1,
    Half = 2,
    ThreeQuarters = 3,
}

impl SubpixelOffset {
    // Skia quantizes subpixel offsets into 1/4 increments.
    // Given the absolute position, return the quantized increment
    fn quantize(pos: f32) -> Self {
        // Following the conventions of Gecko and Skia, we want
        // to quantize the subpixel position, such that abs(pos) gives:
        // [0.0, 0.125) -> Zero
        // [0.125, 0.375) -> Quarter
        // [0.375, 0.625) -> Half
        // [0.625, 0.875) -> ThreeQuarters,
        // [0.875, 1.0) -> Zero
        // The unit tests below check for this.
        let apos = ((pos - pos.floor()) * 8.0) as i32;

        match apos {
            1..=2 => SubpixelOffset::Quarter,
            3..=4 => SubpixelOffset::Half,
            5..=6 => SubpixelOffset::ThreeQuarters,
            _ => SubpixelOffset::Zero,
        }
    }
}

impl Into<f64> for SubpixelOffset {
    fn into(self) -> f64 {
        match self {
            SubpixelOffset::Zero => 0.0,
            SubpixelOffset::Quarter => 0.25,
            SubpixelOffset::Half => 0.5,
            SubpixelOffset::ThreeQuarters => 0.75,
        }
    }
}

impl SubpixelOffset {
    fn to_f32(self) -> f32 {
        match self {
            SubpixelOffset::Zero => 0.0,
            SubpixelOffset::Quarter => 0.25,
            SubpixelOffset::Half => 0.5,
            SubpixelOffset::ThreeQuarters => 0.75,
        }
    }
}

#[derive(Copy, Clone, Hash, PartialEq, Eq, Debug, Ord, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct GlyphKey(u32);

#[derive(Copy, Clone, Hash, PartialEq, Eq, Debug, Ord, PartialOrd)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct GlyphCacheKey(u32);

impl GlyphKey {
    pub fn new(
        index: u32,
        point: DevicePoint,
        subpx_dir: SubpixelDirection,
    ) -> Self {
        let (dx, dy) = match subpx_dir {
            SubpixelDirection::None => (0.0, 0.0),
            SubpixelDirection::Horizontal => (point.x, 0.0),
            SubpixelDirection::Vertical => (0.0, point.y),
            SubpixelDirection::Mixed => (point.x, point.y),
        };
        let sox = SubpixelOffset::quantize(dx);
        let soy = SubpixelOffset::quantize(dy);
        assert_eq!(0, index & 0xFC000000);

        GlyphKey(index | (sox as u32) << 26 | (soy as u32) << 28 | (subpx_dir as u32) << 30)
    }

    pub fn index(&self) -> GlyphIndex {
        self.0 & 0x03FFFFFF
    }

    pub fn subpixel_offset(&self) -> (SubpixelOffset, SubpixelOffset) {
        let x = (self.0 >> 26) as u8 & 3;
        let y = (self.0 >> 28) as u8 & 3;
        unsafe {
            (mem::transmute(x), mem::transmute(y))
        }
    }

    pub fn subpixel_dir(&self) -> SubpixelDirection {
        let dir = (self.0 >> 30) as u8 & 3;
        unsafe {
            mem::transmute(dir as u32)
        }
    }

    pub fn cache_key(&self) -> GlyphCacheKey {
        let index = self.index();
        let subpx_dir = self.subpixel_dir();
        assert_eq!(0, index & 0xFC000000);
        GlyphCacheKey(index | (subpx_dir as u32) << 30)
    }
}

impl GlyphCacheKey {
    pub fn index(&self) -> GlyphIndex {
        self.0 & 0x03FFFFFF
    }

    pub fn subpixel_dir(&self) -> SubpixelDirection {
        let dir = ((self.0 >> 30) & 3) as u32;
        unsafe {
            mem::transmute(dir)
        }
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Hash, Debug)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
#[allow(dead_code)]
pub enum GlyphFormat {
    Alpha,
    TransformedAlpha,
    Subpixel,
    TransformedSubpixel,
    Bitmap,
    ColorBitmap,
}

impl GlyphFormat {
    /// Returns the ImageFormat that a glyph should be stored as in the texture cache.
    /// can_use_r8_format should be set false on platforms where we have encountered
    /// issues with R8 textures, so that we do not use them for glyphs.
    pub fn image_format(&self, can_use_r8_format: bool) -> ImageFormat {
        match *self {
            GlyphFormat::Alpha |
            GlyphFormat::TransformedAlpha |
            GlyphFormat::Bitmap => {
                if can_use_r8_format {
                    ImageFormat::R8
                } else {
                    ImageFormat::BGRA8
                }
            }
            GlyphFormat::Subpixel |
            GlyphFormat::TransformedSubpixel |
            GlyphFormat::ColorBitmap => ImageFormat::BGRA8,
        }
    }
}

#[allow(dead_code)]
#[inline]
fn blend_strike_pixel(dest: u8, src: u32, src_alpha: u32) -> u8 {
    // Assume premultiplied alpha such that src and dest are already multiplied
    // by their respective alpha values and in range 0..=255. The rounded over
    // blend is then (src * 255 + dest * (255 - src_alpha) + 128) / 255.
    // We approximate (x + 128) / 255 as (x + 128 + ((x + 128) >> 8)) >> 8.
    let x = src * 255 + dest as u32 * (255 - src_alpha) + 128;
    ((x + (x >> 8)) >> 8) as u8
}

// Blends a single strike at a given offset into a destination buffer, assuming
// the destination has been allocated with enough extra space to accommodate the
// offset.
#[allow(dead_code)]
fn blend_strike(
    dest_bitmap: &mut [u8],
    src_bitmap: &[u8],
    width: usize,
    height: usize,
    subpixel_mask: bool,
    offset: f64,
) {
    let dest_stride = dest_bitmap.len() / height;
    let src_stride = width * 4;
    let offset_integer = offset.floor() as usize * 4;
    let offset_fract = (offset.fract() * 256.0) as u32;
    for (src_row, dest_row) in src_bitmap.chunks(src_stride).zip(dest_bitmap.chunks_mut(dest_stride)) {
        let mut prev_px = [0u32; 4];
        let dest_row_offset = &mut dest_row[offset_integer .. offset_integer + src_stride];
        for (src, dest) in src_row.chunks(4).zip(dest_row_offset.chunks_mut(4)) {
            let px = [src[0] as u32, src[1] as u32, src[2] as u32, src[3] as u32];
            // Blend current pixel with previous pixel based on fractional offset.
            let next_px = [px[0] * offset_fract,
                           px[1] * offset_fract,
                           px[2] * offset_fract,
                           px[3] * offset_fract];
            let offset_px = [(((px[0] << 8) - next_px[0]) + prev_px[0] + 128) >> 8,
                             (((px[1] << 8) - next_px[1]) + prev_px[1] + 128) >> 8,
                             (((px[2] << 8) - next_px[2]) + prev_px[2] + 128) >> 8,
                             (((px[3] << 8) - next_px[3]) + prev_px[3] + 128) >> 8];
            if subpixel_mask {
                // Subpixel masks assume each component is an independent weight.
                dest[0] = blend_strike_pixel(dest[0], offset_px[0], offset_px[0]);
                dest[1] = blend_strike_pixel(dest[1], offset_px[1], offset_px[1]);
                dest[2] = blend_strike_pixel(dest[2], offset_px[2], offset_px[2]);
                dest[3] = blend_strike_pixel(dest[3], offset_px[3], offset_px[3]);
            } else {
                // Otherwise assume we have a premultiplied alpha BGRA value.
                dest[0] = blend_strike_pixel(dest[0], offset_px[0], offset_px[3]);
                dest[1] = blend_strike_pixel(dest[1], offset_px[1], offset_px[3]);
                dest[2] = blend_strike_pixel(dest[2], offset_px[2], offset_px[3]);
                dest[3] = blend_strike_pixel(dest[3], offset_px[3], offset_px[3]);
            }
            // Save the remainder for blending onto the next pixel.
            prev_px = next_px;
        }
        if offset_fract > 0 {
            // When there is fractional offset, there will be a remaining value
            // from the previous pixel but no next pixel, so just use that.
            let dest = &mut dest_row[offset_integer + src_stride .. ];
            let offset_px = [(prev_px[0] + 128) >> 8,
                             (prev_px[1] + 128) >> 8,
                             (prev_px[2] + 128) >> 8,
                             (prev_px[3] + 128) >> 8];
            if subpixel_mask {
                dest[0] = blend_strike_pixel(dest[0], offset_px[0], offset_px[0]);
                dest[1] = blend_strike_pixel(dest[1], offset_px[1], offset_px[1]);
                dest[2] = blend_strike_pixel(dest[2], offset_px[2], offset_px[2]);
                dest[3] = blend_strike_pixel(dest[3], offset_px[3], offset_px[3]);
            } else {
                dest[0] = blend_strike_pixel(dest[0], offset_px[0], offset_px[3]);
                dest[1] = blend_strike_pixel(dest[1], offset_px[1], offset_px[3]);
                dest[2] = blend_strike_pixel(dest[2], offset_px[2], offset_px[3]);
                dest[3] = blend_strike_pixel(dest[3], offset_px[3], offset_px[3]);
            }
        }
    }
}

// Applies multistrike bold to a source bitmap. This assumes the source bitmap
// is a tighly packed slice of BGRA pixel values of exactly the specified width
// and height. The specified extra strikes and pixel step control where to put
// each strike. The pixel step is allowed to have a fractional offset and does
// not strictly need to be integer.
#[allow(dead_code)]
pub fn apply_multistrike_bold(
    src_bitmap: &[u8],
    width: usize,
    height: usize,
    subpixel_mask: bool,
    extra_strikes: usize,
    pixel_step: f64,
) -> (Vec<u8>, usize) {
    let src_stride = width * 4;
    // The amount of extra width added to the bitmap from the extra strikes.
    let extra_width = (extra_strikes as f64 * pixel_step).ceil() as usize;
    let dest_width = width + extra_width;
    let dest_stride = dest_width * 4;
    // Zero out the initial bitmap so any extra width is cleared.
    let mut dest_bitmap = vec![0u8; dest_stride * height];
    for (src_row, dest_row) in src_bitmap.chunks(src_stride).zip(dest_bitmap.chunks_mut(dest_stride)) {
        // Copy the initial bitmap strike rows directly from the source.
        dest_row[0 .. src_stride].copy_from_slice(src_row);
    }
    // Finally blend each extra strike in turn.
    for i in 1 ..= extra_strikes {
        let offset = i as f64 * pixel_step;
        blend_strike(&mut dest_bitmap, src_bitmap, width, height, subpixel_mask, offset);
    }
    (dest_bitmap, dest_width)
}

pub struct RasterizedGlyph {
    pub top: f32,
    pub left: f32,
    pub width: i32,
    pub height: i32,
    pub scale: f32,
    pub format: GlyphFormat,
    pub bytes: Vec<u8>,
    pub is_packed_glyph: bool,
}

impl RasterizedGlyph {
    #[allow(dead_code)]
    pub fn downscale_bitmap_if_required(&mut self, font: &FontInstance) {
        // Check if the glyph is going to be downscaled in the shader. If the scaling is
        // less than 0.5, that means bilinear filtering can't effectively filter the glyph
        // without aliasing artifacts.
        //
        // Instead of fixing this by mipmapping the glyph cache texture, rather manually
        // produce the appropriate mip level for individual glyphs where bilinear filtering
        // will still produce acceptable results.
        match self.format {
            GlyphFormat::Bitmap | GlyphFormat::ColorBitmap => {},
            _ => return,
        }
        let (x_scale, y_scale) = font.transform.compute_scale().unwrap_or((1.0, 1.0));
        let upscaled = x_scale.max(y_scale) as f32;
        let mut new_scale = self.scale;
        if new_scale * upscaled <= 0.0 {
            return;
        }
        let mut steps = 0;
        while new_scale * upscaled <= 0.5 {
            new_scale *= 2.0;
            steps += 1;
        }
        // If no mipping is necessary, just bail.
        if steps == 0 {
            return;
        }

        // Calculate the actual size of the mip level.
        let new_width = (self.width as usize + (1 << steps) - 1) >> steps;
        let new_height = (self.height as usize + (1 << steps) - 1) >> steps;
        let mut new_bytes: Vec<u8> = Vec::with_capacity(new_width * new_height * 4);

        // Produce destination pixels by applying a box filter to the source pixels.
        // The box filter corresponds to how graphics drivers may generate mipmaps.
        for y in 0 .. new_height {
            for x in 0 .. new_width {
                // Calculate the number of source samples that contribute to the destination pixel.
                let src_y = y << steps;
                let src_x = x << steps;
                let y_samples = (1 << steps).min(self.height as usize - src_y);
                let x_samples = (1 << steps).min(self.width as usize - src_x);
                let num_samples = (x_samples * y_samples) as u32;

                let mut src_idx = (src_y * self.width as usize + src_x) * 4;
                // Initialize the accumulator with half an increment so that when later divided
                // by the sample count, it will effectively round the accumulator to the nearest
                // increment.
                let mut accum = [num_samples / 2; 4];
                // Accumulate all the contributing source sampless.
                for _ in 0 .. y_samples {
                    for _ in 0 .. x_samples {
                        accum[0] += self.bytes[src_idx + 0] as u32;
                        accum[1] += self.bytes[src_idx + 1] as u32;
                        accum[2] += self.bytes[src_idx + 2] as u32;
                        accum[3] += self.bytes[src_idx + 3] as u32;
                        src_idx += 4;
                    }
                    src_idx += (self.width as usize - x_samples) * 4;
                }

                // Finally, divide by the sample count to get the mean value for the new pixel.
                new_bytes.extend_from_slice(&[
                    (accum[0] / num_samples) as u8,
                    (accum[1] / num_samples) as u8,
                    (accum[2] / num_samples) as u8,
                    (accum[3] / num_samples) as u8,
                ]);
            }
        }

        // Fix the bounds for the new glyph data.
        self.top /= (1 << steps) as f32;
        self.left /= (1 << steps) as f32;
        self.width = new_width as i32;
        self.height = new_height as i32;
        self.scale = new_scale;
        self.bytes = new_bytes;
    }
}

pub struct FontContexts {
    // These worker are mostly accessed from their corresponding worker threads.
    // The goal is that there should be no noticeable contention on the mutexes.
    worker_contexts: Vec<Mutex<FontContext>>,
    // Stored here as a convenience to get the current thread index.
    #[allow(dead_code)]
    workers: Arc<ThreadPool>,
    locked_mutex: Mutex<bool>,
    locked_cond: Condvar,
}

impl FontContexts {
    /// Get access to any particular font context.
    ///
    /// The id is an index between 0 and num_worker_contexts for font contexts
    /// associated to the thread pool.
    pub fn lock_context(&self, id: usize) -> MutexGuard<FontContext> {
        self.worker_contexts[id].lock().unwrap()
    }

    // Find a context that is currently unlocked to use, otherwise defaulting
    // to the first context.
    pub fn lock_any_context(&self) -> MutexGuard<FontContext> {
        for context in &self.worker_contexts {
            if let Ok(mutex) = context.try_lock() {
                return mutex;
            }
        }
        self.lock_context(0)
    }

    // number of contexts associated to workers
    pub fn num_worker_contexts(&self) -> usize {
        self.worker_contexts.len()
    }
}

pub trait AsyncForEach<T> {
    fn async_for_each<F: Fn(MutexGuard<T>) + Send + 'static>(&self, f: F);
}

impl AsyncForEach<FontContext> for Arc<FontContexts> {
    fn async_for_each<F: Fn(MutexGuard<FontContext>) + Send + 'static>(&self, f: F) {
        // Reset the locked condition.
        let mut locked = self.locked_mutex.lock().unwrap();
        *locked = false;

        // Arc that can be safely moved into a spawn closure.
        let font_contexts = self.clone();
        // Spawn a new thread on which to run the for-each off the main thread.
        self.workers.spawn(move || {
            // Lock the shared and worker contexts up front.
            let mut locks = Vec::with_capacity(font_contexts.num_worker_contexts());
            for i in 0 .. font_contexts.num_worker_contexts() {
                locks.push(font_contexts.lock_context(i));
            }

            // Signal the locked condition now that all contexts are locked.
            *font_contexts.locked_mutex.lock().unwrap() = true;
            font_contexts.locked_cond.notify_all();

            // Now that everything is locked, proceed to processing each locked context.
            for context in locks {
                f(context);
            }
        });

        // Wait for locked condition before resuming. Safe to proceed thereafter
        // since any other thread that needs to use a FontContext will try to lock
        // it first.
        while !*locked {
            locked = self.locked_cond.wait(locked).unwrap();
        }
    }
}

pub struct GlyphRasterizer {
    #[allow(dead_code)]
    workers: Arc<ThreadPool>,
    font_contexts: Arc<FontContexts>,
    dedicated_thread: Option<GlyphRasterThread>,

    /// The current set of loaded fonts.
    fonts: FastHashSet<FontKey>,

    /// The current number of individual glyphs waiting in pending batches.
    pending_glyph_count: usize,

    /// The current number of glyph request jobs that have been kicked to worker threads.
    pending_glyph_jobs: usize,

    /// The number of glyphs requested this frame.
    glyph_request_count: usize,

    /// A map of current glyph request batches.
    pending_glyph_requests: FastHashMap<FontInstance, SmallVec<[GlyphKey; 16]>>,

    // Receives the rendered glyphs.
    glyph_rx: Receiver<GlyphRasterJob>,
    glyph_tx: Sender<GlyphRasterJob>,

    // We defer removing fonts to the end of the frame so that:
    // - this work is done outside of the critical path,
    // - we don't have to worry about the ordering of events if a font is used on
    //   a frame where it is used (although it seems unlikely).
    fonts_to_remove: Vec<FontKey>,
    // Defer removal of font instances, as for fonts.
    font_instances_to_remove: Vec<FontInstance>,

    // Whether to parallelize glyph rasterization with rayon.
    enable_multithreading: bool,

    // Whether glyphs can be rasterized in r8 format when it makes sense.
    can_use_r8_format: bool,
}

impl GlyphRasterizer {
    pub fn new(workers: Arc<ThreadPool>, dedicated_thread: Option<GlyphRasterThread>, can_use_r8_format: bool) -> Self {
        let (glyph_tx, glyph_rx) = unbounded_channel();

        let num_workers = workers.current_num_threads();
        let mut contexts = Vec::with_capacity(num_workers);

        for _ in 0 .. num_workers {
            contexts.push(Mutex::new(FontContext::new()));
        }

        let font_context = FontContexts {
            worker_contexts: contexts,
            workers: Arc::clone(&workers),
            locked_mutex: Mutex::new(false),
            locked_cond: Condvar::new(),
        };

        GlyphRasterizer {
            font_contexts: Arc::new(font_context),
            fonts: FastHashSet::default(),
            dedicated_thread,
            pending_glyph_jobs: 0,
            pending_glyph_count: 0,
            glyph_request_count: 0,
            glyph_rx,
            glyph_tx,
            workers,
            fonts_to_remove: Vec::new(),
            font_instances_to_remove: Vec::new(),
            enable_multithreading: true,
            pending_glyph_requests: FastHashMap::default(),
            can_use_r8_format,
        }
    }

    pub fn add_font(&mut self, font_key: FontKey, template: FontTemplate) {
        // Only add font to FontContexts if not previously added.
        if self.fonts.insert(font_key.clone()) {
            if let Some(thread) = &self.dedicated_thread {
                let _ = thread.tx.send(GlyphRasterMsg::AddFont { font_key, template });
            } else {
                self.font_contexts.async_for_each(move |mut context| {
                    context.add_font(&font_key, &template);
                });
            }
        }
    }

    pub fn delete_font(&mut self, font_key: FontKey) {
        self.fonts_to_remove.push(font_key);
    }

    pub fn delete_fonts(&mut self, font_keys: &[FontKey]) {
        self.fonts_to_remove.extend_from_slice(font_keys);
    }

    pub fn delete_font_instance(&mut self, instance: &FontInstance) {
        self.font_instances_to_remove.push(instance.clone());
    }

    pub fn prepare_font(&self, font: &mut FontInstance) {
        FontContext::prepare_font(font);

        // Quantize the transform to minimize thrashing of the glyph cache, but
        // only quantize the transform when preparing to access the glyph cache.
        // This way, the glyph subpixel positions, which are calculated before
        // this, can still use the precise transform which is required to match
        // the subpixel positions computed for glyphs in the text run shader.
        font.transform = font.transform.quantize();
    }

    pub fn has_font(&self, font_key: FontKey) -> bool {
        self.fonts.contains(&font_key)
    }

    /// Returns whether the font `template` contains embedded bitmap strikes.
    /// Intended to be called once per font at add time on the render backend
    /// thread so the result can be cached for lock-free lookup during frame
    /// building. The detection consults the shared font cache directly rather
    /// than a worker `FontContext`, so it does not take a worker mutex.
    pub fn template_has_bitmap_strikes(&self, template: &FontTemplate) -> bool {
        FontContext::has_bitmap_strikes(template)
    }

    pub fn get_glyph_dimensions(
        &mut self,
        font: &FontInstance,
        glyph_index: GlyphIndex,
    ) -> Option<GlyphDimensions> {
        let glyph_key = GlyphKey::new(
            glyph_index,
            DevicePoint::zero(),
            SubpixelDirection::None,
        );

        self.font_contexts
            .lock_any_context()
            .get_glyph_dimensions(font, &glyph_key)
    }

    pub fn get_glyph_index(&mut self, font_key: FontKey, ch: char) -> Option<u32> {
        self.font_contexts
            .lock_any_context()
            .get_glyph_index(font_key, ch)
    }

    fn remove_dead_fonts(&mut self) {
        if self.fonts_to_remove.is_empty() && self.font_instances_to_remove.is_empty() {
            return
        }

        profile_scope!("remove_dead_fonts");
        let mut fonts_to_remove = mem::replace(& mut self.fonts_to_remove, Vec::new());
        // Only remove font from FontContexts if previously added.
        fonts_to_remove.retain(|font| self.fonts.remove(font));
        let font_instances_to_remove = mem::replace(& mut self.font_instances_to_remove, Vec::new());
        if let Some(thread) = &self.dedicated_thread {
            for font_key in fonts_to_remove {
                let _ = thread.tx.send(GlyphRasterMsg::DeleteFont { font_key });
            }
            for instance in font_instances_to_remove {
                let _ = thread.tx.send(GlyphRasterMsg::DeleteFontInstance { instance });
            }
        } else {
            self.font_contexts.async_for_each(move |mut context| {
                for font_key in &fonts_to_remove {
                    context.delete_font(font_key);
                }
                for instance in &font_instances_to_remove {
                    context.delete_font_instance(instance);
                }
            });
        }
    }

    #[cfg(feature = "replay")]
    pub fn reset(&mut self) {
        //TODO: any signals need to be sent to the workers?
        self.pending_glyph_jobs = 0;
        self.pending_glyph_count = 0;
        self.glyph_request_count = 0;
        self.fonts_to_remove.clear();
        self.font_instances_to_remove.clear();
    }
}

trait AddFont {
    fn add_font(&mut self, font_key: &FontKey, template: &FontTemplate);
}

impl AddFont for FontContext {
    fn add_font(&mut self, font_key: &FontKey, template: &FontTemplate) {
        match *template {
            FontTemplate::Raw(ref bytes, index) => {
                self.add_raw_font(font_key, bytes.clone(), index);
            }
            FontTemplate::Native(ref native_font_handle) => {
                self.add_native_font(font_key, (*native_font_handle).clone());
            }
        }
    }
}

#[allow(dead_code)]
pub struct GlyphRasterJob {
    pub font: Arc<FontInstance>,
    pub key: GlyphKey,
    pub result: GlyphRasterResult,
}

#[allow(dead_code)]
#[derive(Debug)]
pub enum GlyphRasterError {
    LoadFailed,
}

#[allow(dead_code)]
pub type GlyphRasterResult = Result<RasterizedGlyph, GlyphRasterError>;

#[derive(Debug, Copy, Clone, Eq, Hash, PartialEq)]
#[cfg_attr(feature = "capture", derive(Serialize))]
#[cfg_attr(feature = "replay", derive(Deserialize))]
pub struct GpuGlyphCacheKey(pub u32);

/// Rasterized glyphs larger than this in either dimension are dropped.
///
/// `FONT_SIZE_LIMIT` bounds the size the backends are *asked* for, but not what
/// comes back. A near-degenerate transform passes `has_2d_inverse` - which tests
/// the determinant against exactly zero, while `FontTransform::quantize` puts it
/// on a 1/1024 grid - and decomposes into a tiny minor axis scale. The glyph size
/// handed to the backend is quantized, so that tiny scale rounds away while the
/// compensating shape matrix keeps its full magnitude, and the rasterized glyph
/// grows roughly as the reciprocal of the determinant: at a determinant of
/// 9.8e-4, a 148.5px font rasterizes 1140px wide.
///
/// A glyph rasterized at `FONT_SIZE_LIMIT` under a transform the device path
/// admits measures a few hundred pixels a side, so this leaves a wide margin. It
/// also keeps a packed entry - up to 16 slots of one glyph, see
/// `pack_glyph_variants_horizontal` - well inside the i32 its dimensions are
/// computed in, which is what used to overflow (bug 2072715).
const GLYPH_DIMENSION_LIMIT: i32 = 2048;

/// Rasterize a glyph, treating one that comes back above `GLYPH_DIMENSION_LIMIT`
/// as a glyph that could not be rasterized at all.
fn rasterize_glyph_bounded(
    context: &mut FontContext,
    font: &FontInstance,
    key: &GlyphKey,
) -> GlyphRasterResult {
    let glyph = context.rasterize_glyph(font, key)?;

    if glyph.width > GLYPH_DIMENSION_LIMIT || glyph.height > GLYPH_DIMENSION_LIMIT {
        return Err(GlyphRasterError::LoadFailed);
    }

    Ok(glyph)
}

fn pack_glyph_variants_horizontal(variants: &[RasterizedGlyph]) -> RasterizedGlyph {
    // Pack the glyph variants horizontally into a single texture (4 for a single
    // sub-pixel axis, 16 for a mixed transform's 4x4 grid).
    // Normalize both left and top offsets via padding so all variants can use the same base offsets.
    // The arithmetic below stays in i32: every variant is within
    // `GLYPH_DIMENSION_LIMIT`, which bounds the packed entry well inside i32.

    let min_left = variants.iter().map(|v| v.left.floor()).fold(f32::INFINITY, f32::min);
    let max_top = variants.iter().map(|v| v.top.floor()).fold(f32::NEG_INFINITY, f32::max);

    // Slot width must accommodate the widest variant plus left padding
    let slot_width = variants.iter()
        .map(|v| v.width + (v.left.floor() - min_left) as i32)
        .max().unwrap();

    // Slot height must accommodate the tallest variant plus top padding
    let slot_height = variants.iter()
        .map(|v| v.height + (max_top - v.top.floor()) as i32)
        .max().unwrap();

    let packed_width = slot_width * variants.len() as i32;
    let bpp = 4;

    let mut packed_bytes = vec![0u8; (packed_width * slot_height * bpp) as usize];

    for (variant_idx, variant) in variants.iter().enumerate() {
        // Compute padding needed to normalize both left and top offsets
        let left_pad = (variant.left.floor() - min_left) as i32;
        let top_pad = (max_top - variant.top.floor()) as i32;
        let slot_x = variant_idx as i32 * slot_width;

        for src_y in 0..variant.height {
            let dst_y = src_y + top_pad;
            if dst_y >= slot_height {
                break;
            }

            let dst_x = slot_x + left_pad;
            if dst_x < 0 {
                continue;
            }

            let src_row_start = (src_y * variant.width * bpp) as usize;
            let src_row_end = src_row_start + (variant.width * bpp) as usize;
            let dst_row_start = (dst_y * packed_width * bpp + dst_x * bpp) as usize;
            let dst_row_end = dst_row_start + (variant.width * bpp) as usize;

            if dst_row_end <= packed_bytes.len() && src_row_end <= variant.bytes.len() {
                packed_bytes[dst_row_start..dst_row_end]
                    .copy_from_slice(&variant.bytes[src_row_start..src_row_end]);
            }
        }
    }

    RasterizedGlyph {
        top: max_top,
        left: min_left,
        width: packed_width,
        height: slot_height,
        scale: variants[0].scale,
        format: variants[0].format,
        bytes: packed_bytes,
        is_packed_glyph: true,
    }
}

fn process_glyph(
    context: &mut FontContext,
    can_use_r8_format: bool,
    font: Arc<FontInstance>,
    key: GlyphKey,
) -> GlyphRasterJob {
    profile_scope!("glyph-raster");

    let subpx_dir = key.subpixel_dir();

    let result = if subpx_dir == SubpixelDirection::None {
        rasterize_glyph_bounded(context, &font, &key)
    } else {
        let offsets = [
            SubpixelOffset::Zero,
            SubpixelOffset::Quarter,
            SubpixelOffset::Half,
            SubpixelOffset::ThreeQuarters,
        ];

        // A mixed transform varies on both axes, so it needs the full 4x4 grid
        // of offsets. The shader indexes it as `offset_y * 4 + offset_x`.
        let points: Vec<DevicePoint> = match subpx_dir {
            SubpixelDirection::Horizontal =>
                offsets.iter().map(|o| DevicePoint::new(o.to_f32(), 0.0)).collect(),
            SubpixelDirection::Vertical =>
                offsets.iter().map(|o| DevicePoint::new(0.0, o.to_f32())).collect(),
            SubpixelDirection::Mixed => offsets.iter()
                .flat_map(|oy| offsets.iter().map(move |ox| {
                    DevicePoint::new(ox.to_f32(), oy.to_f32())
                }))
                .collect(),
            SubpixelDirection::None => vec![DevicePoint::zero()],
        };

        let mut variants = Vec::with_capacity(points.len());
        for point in points {
            let variant_key = GlyphKey::new(key.index(), point, subpx_dir);

            match rasterize_glyph_bounded(context, &font, &variant_key) {
                Ok(glyph) => variants.push(glyph),
                Err(e) => return GlyphRasterJob {
                    font: font,
                    key: key.clone(),
                    result: Err(e),
                },
            }
        }

        Ok(pack_glyph_variants_horizontal(&variants))
    };

    let mut job = GlyphRasterJob {
        font: font,
        key: key.clone(),
        result,
    };

    if let Ok(ref mut glyph) = job.result {
        // Sanity check.
        let bpp = 4; // We always render glyphs in 32 bits RGBA format.
        assert_eq!(
            glyph.bytes.len(),
            bpp * (glyph.width * glyph.height) as usize
        );

        // a quick-and-dirty monochrome over
        fn over(dst: u8, src: u8) -> u8 {
            let a = src as u32;
            let a = 256 - a;
            let dst = ((dst as u32 * a) >> 8) as u8;
            src + dst
        }

        if GLYPH_FLASHING.load(Ordering::Relaxed) {
            let color = (random() & 0xff) as u8;
            for i in &mut glyph.bytes {
                *i = over(*i, color);
            }
        }

        assert_eq!((glyph.left.fract(), glyph.top.fract()), (0.0, 0.0));

        // Check if the glyph has a bitmap that needs to be downscaled.
        glyph.downscale_bitmap_if_required(&job.font);

        // Convert from BGRA8 to R8 if required. In the future we can make it the
        // backends' responsibility to output glyphs in the desired format,
        // potentially reducing the number of copies.
        if glyph.format.image_format(can_use_r8_format).bytes_per_pixel() == 1 {
            glyph.bytes = glyph.bytes
                .chunks_mut(4)
                .map(|pixel| pixel[3])
                .collect::<Vec<_>>();
        }
    }

    job
}


pub enum GlyphRasterMsg {
    Rasterize {
        font: Arc<FontInstance>,
        glyphs: SmallVec<[GlyphKey; 16]>,
        can_use_r8_format: bool,
        tx: Sender<GlyphRasterJob>,
    },
    AddFont { font_key: FontKey, template: FontTemplate },
    DeleteFont { font_key: FontKey },
    DeleteFontInstance { instance: FontInstance },
    ShutDown,
}

#[derive(Clone)]
pub struct GlyphRasterThread {
    tx: Sender<GlyphRasterMsg>,
}

impl GlyphRasterThread {
    pub fn new(
        on_start: impl FnOnce() + Send + 'static,
        on_end: impl FnOnce() + Send+ 'static,
    ) -> std::io::Result<Self> {
        let (tx, rx) = unbounded_channel();

        std::thread::Builder::new().name("Glyph rasterizer".to_string()).spawn(move || {
            on_start();

            let mut context = FontContext::new();

            loop {
                match rx.recv() {
                    Ok(GlyphRasterMsg::Rasterize { font, glyphs, can_use_r8_format, tx }) => {
                        for glyph in &glyphs {
                            let job = process_glyph(&mut context, can_use_r8_format, font.clone(), *glyph);
                            let _ = tx.send(job);
                        }
                    }
                    Ok(GlyphRasterMsg::AddFont { font_key, template }) => {
                        context.add_font(&font_key, &template)
                    }
                    Ok(GlyphRasterMsg::DeleteFont { font_key }) => {
                        context.delete_font(&font_key)
                    }
                    Ok(GlyphRasterMsg::DeleteFontInstance { instance }) => {
                        context.delete_font_instance(&instance)
                    }
                    Ok(GlyphRasterMsg::ShutDown) => {
                        break;
                    }
                    Err(..) => {
                        break;
                    }
                }
            }

            on_end();
        })?;

        Ok(GlyphRasterThread {
            tx,
        })
    }

    pub fn shut_down(&self) {
        let _ = self.tx.send(GlyphRasterMsg::ShutDown);
    }
}

#[cfg(test)]
mod test_glyph_rasterizer {
    use crate::profiler::GlyphRasterizeProfiler;

    struct Profiler;
    impl GlyphRasterizeProfiler for Profiler {
        fn start_time(&mut self) {}
        fn end_time(&mut self) -> f64 {
            0.
        }
        fn set(&mut self, _value: f64) {}
    }

    #[test]
    fn rasterize_200_glyphs() {
        // This test loads a font from disc, the renders 4 requests containing
        // 50 glyphs each, deletes the font and waits for the result.

        use rayon::ThreadPoolBuilder;
        use std::fs::File;
        use std::io::Read;
        use api::{FontKey, FontInstanceKey, FontTemplate, IdNamespace};
        use api::units::DevicePoint;
        use std::sync::Arc;
        use crate::rasterizer::{FontInstance, BaseFontInstance, GlyphKey, GlyphRasterizer};

        let worker = ThreadPoolBuilder::new()
            .thread_name(|idx|{ format!("WRWorker#{}", idx) })
            .build();
        let workers = Arc::new(worker.unwrap());
        let mut glyph_rasterizer = GlyphRasterizer::new(workers, None, true);
        let mut font_file =
            File::open("../wrench/reftests/text/VeraBd.ttf").expect("Couldn't open font file");
        let mut font_data = vec![];
        font_file
            .read_to_end(&mut font_data)
            .expect("failed to read font file");

        let font_key = FontKey::new(IdNamespace(0), 0);
        glyph_rasterizer.add_font(font_key, FontTemplate::Raw(Arc::new(font_data), 0));

        let font = FontInstance::from_base(Arc::new(BaseFontInstance::new(
            FontInstanceKey::new(IdNamespace(0), 0),
            font_key,
            32.0,
            None,
            None,
            Vec::new(),
        )));

        let subpx_dir = font.get_subpx_dir();

        let mut glyph_keys = Vec::with_capacity(200);
        for i in 0 .. 200 {
            glyph_keys.push(GlyphKey::new(
                i,
                DevicePoint::zero(),
                subpx_dir,
            ));
        }

        for i in 0 .. 4 {
            glyph_rasterizer.request_glyphs(
                font.clone(),
                &glyph_keys[(50 * i) .. (50 * (i + 1))],
                |_| true,
            );
        }

        glyph_rasterizer.delete_font(font_key);

        glyph_rasterizer.resolve_glyphs(
            |_, _| {},
            &mut Profiler,
        );
    }

    #[test]
    fn rasterize_large_glyphs() {
        // This test loads a font from disc and rasterize a few glyphs with a size of 200px to check
        // that the texture cache handles them properly.
        use rayon::ThreadPoolBuilder;
        use std::fs::File;
        use std::io::Read;
        use api::{FontKey, FontInstanceKey, FontTemplate, IdNamespace};
        use api::units::DevicePoint;
        use std::sync::Arc;
        use crate::rasterizer::{FontInstance, BaseFontInstance, GlyphKey, GlyphRasterizer};

        let worker = ThreadPoolBuilder::new()
            .thread_name(|idx|{ format!("WRWorker#{}", idx) })
            .build();
        let workers = Arc::new(worker.unwrap());
        let mut glyph_rasterizer = GlyphRasterizer::new(workers, None, true);
        let mut font_file =
            File::open("../wrench/reftests/text/VeraBd.ttf").expect("Couldn't open font file");
        let mut font_data = vec![];
        font_file
            .read_to_end(&mut font_data)
            .expect("failed to read font file");

        let font_key = FontKey::new(IdNamespace(0), 0);
        glyph_rasterizer.add_font(font_key, FontTemplate::Raw(Arc::new(font_data), 0));

        let font = FontInstance::from_base(Arc::new(BaseFontInstance::new(
            FontInstanceKey::new(IdNamespace(0), 0),
            font_key,
            200.0,
            None,
            None,
            Vec::new(),
        )));

        let subpx_dir = font.get_subpx_dir();

        let mut glyph_keys = Vec::with_capacity(10);
        for i in 0 .. 10 {
            glyph_keys.push(GlyphKey::new(
                i,
                DevicePoint::zero(),
                subpx_dir,
            ));
        }

        glyph_rasterizer.request_glyphs(
            font.clone(),
            &glyph_keys,
            |_| true,
        );

        glyph_rasterizer.delete_font(font_key);

        glyph_rasterizer.resolve_glyphs(
            |_, _| {},
            &mut Profiler,
        );
    }

    #[test]
    fn test_oversized_glyph_is_dropped() {
        // A glyph that comes back from the backend larger than
        // `GLYPH_DIMENSION_LIMIT` must be dropped rather than rasterized, so
        // that packing its sub-pixel variants cannot overflow (bug 2072715).
        use std::fs::File;
        use std::io::Read;
        use api::{FontKey, FontInstanceKey, IdNamespace};
        use api::units::DevicePoint;
        use std::sync::Arc;
        use crate::rasterizer::{BaseFontInstance, FontInstance, GlyphKey, GlyphRasterError,
                                SubpixelDirection, rasterize_glyph_bounded,
                                GLYPH_DIMENSION_LIMIT};
        use crate::platform::font::FontContext;

        let mut font_file =
            File::open("../wrench/reftests/text/VeraBd.ttf").expect("Couldn't open font file");
        let mut font_data = vec![];
        font_file.read_to_end(&mut font_data).unwrap();

        let font_key = FontKey::new(IdNamespace(0), 0);
        let mut context = FontContext::new();
        context.add_raw_font(&font_key, Arc::new(font_data), 0);

        let instance = |size: f32| {
            FontInstance::from_base(Arc::new(BaseFontInstance::new(
                FontInstanceKey::new(IdNamespace(0), 0),
                font_key,
                size,
                None,
                None,
                Vec::new(),
            )))
        };
        let key = GlyphKey::new(36, DevicePoint::zero(), SubpixelDirection::None);

        // An ordinary glyph is unaffected.
        let ordinary = rasterize_glyph_bounded(&mut context, &instance(32.0), &key).unwrap();
        assert!(ordinary.width <= GLYPH_DIMENSION_LIMIT);
        assert!(ordinary.height <= GLYPH_DIMENSION_LIMIT);

        // Control: the backend does rasterize this size, so the error below is
        // the limit talking and not a rasterization failure.
        let oversized = instance(4000.0);
        let raw = context.rasterize_glyph(&oversized, &key)
                         .expect("backend should rasterize an oversized glyph");
        assert!(raw.width > GLYPH_DIMENSION_LIMIT || raw.height > GLYPH_DIMENSION_LIMIT);

        assert!(matches!(
            rasterize_glyph_bounded(&mut context, &oversized, &key),
            Err(GlyphRasterError::LoadFailed),
        ));
    }

    #[test]
    fn test_packed_glyph_entry_fits_i32() {
        // `pack_glyph_variants_horizontal` sizes the packed entry in i32. The
        // largest entry it can be handed is a 4x4 sub-pixel grid of glyphs at
        // `GLYPH_DIMENSION_LIMIT`, plus a pixel each way of normalizing padding.
        use crate::rasterizer::GLYPH_DIMENSION_LIMIT;

        let slot = GLYPH_DIMENSION_LIMIT as i64 + 1;
        assert!(slot * 16 * slot * 4 < i32::MAX as i64);
    }

    #[test]
    fn test_subpx_quantize() {
        use crate::rasterizer::SubpixelOffset;

        assert_eq!(SubpixelOffset::quantize(0.0), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(-0.0), SubpixelOffset::Zero);

        assert_eq!(SubpixelOffset::quantize(0.1), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.01), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.05), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.12), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.124), SubpixelOffset::Zero);

        assert_eq!(SubpixelOffset::quantize(0.125), SubpixelOffset::Quarter);
        assert_eq!(SubpixelOffset::quantize(0.2), SubpixelOffset::Quarter);
        assert_eq!(SubpixelOffset::quantize(0.25), SubpixelOffset::Quarter);
        assert_eq!(SubpixelOffset::quantize(0.33), SubpixelOffset::Quarter);
        assert_eq!(SubpixelOffset::quantize(0.374), SubpixelOffset::Quarter);

        assert_eq!(SubpixelOffset::quantize(0.375), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(0.4), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(0.5), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(0.58), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(0.624), SubpixelOffset::Half);

        assert_eq!(SubpixelOffset::quantize(0.625), SubpixelOffset::ThreeQuarters);
        assert_eq!(SubpixelOffset::quantize(0.67), SubpixelOffset::ThreeQuarters);
        assert_eq!(SubpixelOffset::quantize(0.7), SubpixelOffset::ThreeQuarters);
        assert_eq!(SubpixelOffset::quantize(0.78), SubpixelOffset::ThreeQuarters);
        assert_eq!(SubpixelOffset::quantize(0.874), SubpixelOffset::ThreeQuarters);

        assert_eq!(SubpixelOffset::quantize(0.875), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.89), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.91), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.967), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(0.999), SubpixelOffset::Zero);

        assert_eq!(SubpixelOffset::quantize(-1.0), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(1.0), SubpixelOffset::Zero);
        assert_eq!(SubpixelOffset::quantize(1.5), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(-1.625), SubpixelOffset::Half);
        assert_eq!(SubpixelOffset::quantize(-4.33), SubpixelOffset::ThreeQuarters);
    }
}
