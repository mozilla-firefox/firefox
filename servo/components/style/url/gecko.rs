/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Gecko-specific logic for CSS url() values.

use crate::derives::*;
use crate::gecko_bindings::bindings;
use crate::gecko_bindings::structs;
use crate::parser::ParserContext;
use crate::stylesheets::{CorsMode, UrlExtraData};
use crate::url::UrlInitiator;
use crate::values::computed::{Context, ToComputedValue};
use hashbrown::HashMap;
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
use nsstring::nsCString;
use servo_arc::Arc;
use std::fmt::{self, Write};
use std::mem::ManuallyDrop;
use std::sync::{LazyLock, RwLock};
use style_traits::{CssWriter, ToCss};
use to_shmem::{SharedMemoryBuilder, ToShmem};

/// A CSS url() value for gecko.
#[derive(Clone, Debug, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
#[css(function = "url")]
#[repr(C)]
pub struct CssUrl(pub Arc<CssUrlData>);

/// Data shared between CssUrls.
///
/// cbindgen:derive-eq=false
/// cbindgen:derive-neq=false
#[derive(Debug, SpecifiedValueInfo, ToCss, ToShmem)]
#[repr(C)]
pub struct CssUrlData {
    /// The URL in unresolved string form.
    serialization: crate::OwnedStr,

    /// The URL extra data.
    #[css(skip)]
    pub extra_data: UrlExtraData,

    /// The CORS mode that will be used for the load.
    #[css(skip)]
    cors_mode: CorsMode,

    /// What caused this URL to be loaded.
    #[css(skip)]
    initiator: UrlInitiator,

    /// Data to trigger a load from Gecko. This is mutable in C++.
    ///
    /// TODO(emilio): Maybe we can eagerly resolve URLs and make this immutable?
    #[css(skip)]
    load_data: LoadDataSource,
}

impl PartialEq for CssUrlData {
    fn eq(&self, other: &Self) -> bool {
        self.serialization == other.serialization
            && self.extra_data == other.extra_data
            && self.cors_mode == other.cors_mode
    }
}

/// How an URI might depend on our base URI.
///
/// TODO(emilio): See if necko can provide this, but for our case local refs or empty URIs are
/// totally fine even though they wouldn't be in general...
#[repr(u8)]
#[derive(PartialOrd, PartialEq)]
pub enum NonLocalUriDependency {
    /// No non-local URI dependencies.
    No = 0,
    /// URI is absolute or not dependent on the base uri otherwise.
    Absolute,
    /// We might depend on our path depth. E.g. `https://example.com/foo` and
    /// `https://example.com/bar` both resolve a relative URI like `baz.css` as
    /// `https://example.com/baz.css`.
    Path,
    /// We might depend on our full URI. This is the case for query strings (and, in the general
    /// case, local refs and empty URIs, but that's not the case for CSS as described below.
    Full,
}

impl NonLocalUriDependency {
    fn scan(specified: &str) -> Self {
        if specified.is_empty() || specified.starts_with('#') || specified.starts_with("data:") {
            // In CSS the empty URL is special / invalid. Local and data uris are also fair game.
            // https://drafts.csswg.org/css-values-4/#url-empty
            return Self::No;
        }
        if specified.starts_with('/')
            || specified.starts_with("http:")
            || specified.starts_with("https:")
        {
            return Self::Absolute;
        }
        if specified.starts_with('?') {
            // Query string resolves differently for any two different base URIs
            return Self::Full;
        }
        // Might be a relative URI, play it safe.
        Self::Path
    }
}

impl CssUrl {
    pub(super) fn new_from_string(
        url: String,
        context: &ParserContext,
        cors_mode: CorsMode,
        initiator: UrlInitiator,
    ) -> Self {
        use crate::use_counters::CustomUseCounter;
        if let Some(counters) = context.use_counters
            && !counters
                .custom
                .recorded(CustomUseCounter::MaybeHasFullBaseUriDependency)
        {
            let dep = NonLocalUriDependency::scan(&url);
            if dep >= NonLocalUriDependency::Absolute {
                counters
                    .custom
                    .record(CustomUseCounter::HasNonLocalUriDependency);
            }
            if dep >= NonLocalUriDependency::Path {
                counters
                    .custom
                    .record(CustomUseCounter::MaybeHasPathBaseUriDependency);
            }
            if dep >= NonLocalUriDependency::Full {
                counters
                    .custom
                    .record(CustomUseCounter::MaybeHasFullBaseUriDependency);
            }
        }
        CssUrl(Arc::new(CssUrlData {
            serialization: url.into(),
            extra_data: context.url_data.clone(),
            cors_mode,
            initiator,
            load_data: LoadDataSource::Owned(LoadData::default()),
        }))
    }

    /// Returns true if the URL is definitely invalid. We don't eagerly resolve
    /// URLs in gecko, so we just return false here.
    /// use its |resolved| status.
    pub fn is_invalid(&self) -> bool {
        false
    }

    /// Returns true if this URL looks like a fragment.
    /// See https://drafts.csswg.org/css-values/#local-urls
    #[inline]
    pub fn is_fragment(&self) -> bool {
        self.0.is_fragment()
    }

    /// Return the unresolved url as string, or the empty string if it's
    /// invalid.
    #[inline]
    pub fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

impl CssUrlData {
    /// Returns true if this URL looks like a fragment.
    /// See https://drafts.csswg.org/css-values/#local-urls
    pub fn is_fragment(&self) -> bool {
        self.as_str()
            .as_bytes()
            .iter()
            .next()
            .is_some_and(|b| *b == b'#')
    }

    /// Return the unresolved url as string, or the empty string if it's
    /// invalid.
    pub fn as_str(&self) -> &str {
        &self.serialization
    }
}

impl Eq for CssUrl {}

impl MallocSizeOf for CssUrl {
    fn size_of(&self, _ops: &mut MallocSizeOfOps) -> usize {
        // XXX: measure `serialization` once bug 1397971 lands

        // We ignore `extra_data`, because RefPtr is tricky, and there aren't
        // many of them in practise (sharing is common).

        0
    }
}

/// A key type for LOAD_DATA_TABLE.
#[derive(Eq, Hash, PartialEq)]
struct LoadDataKey(*const LoadDataSource);

unsafe impl Sync for LoadDataKey {}
unsafe impl Send for LoadDataKey {}

bitflags! {
    /// Various bits of mutable state that are kept for image loads.
    #[derive(Debug)]
    #[repr(C)]
    pub struct LoadDataFlags: u8 {
        /// Whether we tried to resolve the uri at least once.
        const TRIED_TO_RESOLVE_URI = 1 << 0;
        /// Whether we tried to resolve the image at least once.
        const TRIED_TO_RESOLVE_IMAGE = 1 << 1;
    }
}

/// This is usable and movable from multiple threads just fine, as long as it's
/// not cloned (it is not clonable), and the methods that mutate it run only on
/// the main thread (when all the other threads we care about are paused).
unsafe impl Sync for LoadData {}
unsafe impl Send for LoadData {}

/// The load data for a given URL. This is mutable from C++, and shouldn't be
/// accessed from rust for anything.
#[repr(C)]
#[derive(Debug)]
pub struct LoadData {
    /// A strong reference to the imgRequestProxy, if any, that should be
    /// released on drop.
    ///
    /// These are raw pointers because they are not safe to reference-count off
    /// the main thread.
    resolved_image: *mut structs::imgRequestProxy,
    /// A strong reference to the resolved URI of this image.
    resolved_uri: *mut structs::nsIURI,
    /// A few flags that are set when resolving the image or such.
    flags: LoadDataFlags,
}

impl Drop for LoadData {
    fn drop(&mut self) {
        unsafe { bindings::Gecko_LoadData_Drop(self) }
    }
}

impl Default for LoadData {
    fn default() -> Self {
        Self {
            resolved_image: std::ptr::null_mut(),
            resolved_uri: std::ptr::null_mut(),
            flags: LoadDataFlags::empty(),
        }
    }
}

/// The data for a load, or a lazy-loaded, static member that will be stored in
/// LOAD_DATA_TABLE, keyed by the memory location of this object, which is
/// always in the heap because it's inside the CssUrlData object.
///
/// This type is meant not to be used from C++ so we don't derive helper
/// methods.
///
/// cbindgen:derive-helper-methods=false
#[derive(Debug)]
#[repr(u8, C)]
pub enum LoadDataSource {
    /// An owned copy of the load data.
    Owned(LoadData),
    /// A lazily-resolved copy of it.
    Lazy,
}

impl LoadDataSource {
    /// Gets the load data associated with the source.
    ///
    /// This relies on the source on being in a stable location if lazy.
    #[inline]
    pub unsafe fn get(&self) -> *const LoadData {
        match *self {
            LoadDataSource::Owned(ref d) => return d,
            LoadDataSource::Lazy => {},
        }

        let key = LoadDataKey(self);

        {
            let guard = LOAD_DATA_TABLE.read().unwrap();
            if let Some(r) = guard.get(&key) {
                return &**r;
            }
        }
        let mut guard = LOAD_DATA_TABLE.write().unwrap();
        let r = guard.entry(key).or_default();
        &**r
    }
}

impl ToShmem for LoadDataSource {
    fn to_shmem(&self, _builder: &mut SharedMemoryBuilder) -> to_shmem::Result<Self> {
        Ok(ManuallyDrop::new(match self {
            LoadDataSource::Owned(..) => LoadDataSource::Lazy,
            LoadDataSource::Lazy => LoadDataSource::Lazy,
        }))
    }
}

/// A specified non-image `url()` value.
pub type SpecifiedUrl = CssUrl;

/// Clears LOAD_DATA_TABLE.  Entries in this table, which are for specified URL
/// values that come from shared memory style sheets, would otherwise persist
/// until the end of the process and be reported as leaks.
pub fn shutdown() {
    LOAD_DATA_TABLE.write().unwrap().clear();
}

impl ToComputedValue for SpecifiedUrl {
    type ComputedValue = ComputedUrl;

    #[inline]
    fn to_computed_value(&self, _: &Context) -> Self::ComputedValue {
        ComputedUrl(self.clone())
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        computed.0.clone()
    }
}

/// The computed value of a CSS non-image `url()`.
///
/// The only difference between specified and computed URLs is the
/// serialization.
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq)]
#[repr(C)]
pub struct ComputedUrl(pub SpecifiedUrl);

impl ComputedUrl {
    fn serialize_with<W>(
        &self,
        function: unsafe extern "C" fn(*const Self, *mut nsCString),
        dest: &mut CssWriter<W>,
    ) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str("url(")?;
        unsafe {
            let mut string = nsCString::new();
            function(self, &mut string);
            string.as_str_unchecked().to_css(dest)?;
        }
        dest.write_char(')')
    }
}

impl ToCss for ComputedUrl {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.serialize_with(bindings::Gecko_GetComputedURLSpec, dest)
    }
}

/// A table mapping CssUrlData objects to their lazily created LoadData
/// objects.
static LOAD_DATA_TABLE: LazyLock<RwLock<HashMap<LoadDataKey, Box<LoadData>>>> =
    LazyLock::new(Default::default);
