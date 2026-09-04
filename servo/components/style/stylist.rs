/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Selector matching.

use crate::applicable_declarations::{
    ApplicableDeclarationBlock, ApplicableDeclarationList, CascadePriority, ScopeProximity,
};
use crate::computed_value_flags::ComputedValueFlags;
use crate::context::{CascadeInputs, QuirksMode, TreeCountingCaches};
use crate::custom_properties::ComputedCustomProperties;
use crate::custom_properties::{parse_name, SpecifiedValue};
use crate::derives::*;
use crate::device::Device;
use crate::dom::TElement;
#[cfg(feature = "gecko")]
use crate::dom::TShadowRoot;
#[cfg(feature = "gecko")]
use crate::gecko_bindings::structs::{ServoStyleSetSizes, StyleRuleInclusion};
use crate::invalidation::element::invalidation_map::{
    note_selector_for_invalidation, AdditionalRelativeSelectorInvalidationMap, Dependency,
    DependencyInvalidationKind, InvalidationMap, ScopeDependencyInvalidationKind,
};
use crate::invalidation::media_queries::{
    EffectiveMediaQueryResults, MediaListKey, ToMediaListKey,
};
use crate::invalidation::stylesheets::{RuleChangeKind, StylesheetInvalidationSet};
#[cfg(feature = "gecko")]
use crate::properties::StyleBuilder;
use crate::properties::{
    self, AnimationDeclarations, CascadeMode, ComputedValues, FirstLineReparenting,
    PropertyDeclarationBlock,
};
use crate::properties_and_values::registry::{
    PropertyRegistration, ScriptRegistry as CustomPropertyScriptRegistry,
};
use crate::properties_and_values::rule::{
    Descriptors as PropertyDescriptors, Inherits, PropertyRegistrationError, PropertyRuleName,
};
use crate::properties_and_values::syntax::Descriptor;
use crate::rule_cache::{RuleCache, RuleCacheConditions};
use crate::rule_collector::RuleCollector;
use crate::rule_tree::{
    CascadeLevel, CascadeOrigin, RuleCascadeFlags, RuleTree, StrongRuleNode, StyleSource,
    StyleSourceBorrow,
};
use crate::selector_map::{
    BucketMatches, PrecomputedHashMap, PrecomputedHashSet, SelectorMap, SelectorMapEntry,
};
use crate::selector_parser::{NonTSPseudoClass, PerPseudoElementMap, PseudoElement, SelectorImpl};
use crate::shared_lock::{Locked, SharedRwLockReadGuard, StylesheetGuards};
use crate::sharing::{RevalidationResult, ScopeRevalidationResult};
use crate::stylesheet_set::{DataValidity, DocumentStylesheetSet, SheetRebuildKind};
use crate::stylesheet_set::{DocumentStylesheetFlusher, SheetCollectionFlusher};
use crate::stylesheets::container_rule::{ContainerAttributeDependencyKind, ContainerCondition};
use crate::stylesheets::import_rule::ImportLayer;
use crate::stylesheets::keyframes_rule::KeyframesAnimation;
use crate::stylesheets::layer_rule::{LayerName, LayerOrder};
use crate::stylesheets::scope_rule::{
    collect_scope_roots, element_is_outside_of_scope, scope_selector_list_is_trivial,
    ImplicitScopeRoot, ScopeRootCandidate, ScopeSubjectMap, ScopeTarget,
};
use crate::stylesheets::UrlExtraData;
use crate::stylesheets::{
    CounterStyleRule, CssRule, CssRuleRef, EffectiveRulesIterator, FontFaceRule,
    FontFeatureValuesRule, FontPaletteValuesRule, Origin, OriginSet, PagePseudoClassFlags,
    PageRule, PerOrigin, PerOriginIter, PositionTryRule, StylesheetContents, StylesheetInDocument,
    ViewTransitionRule,
};
use crate::stylesheets::{CustomMediaEvaluator, CustomMediaMap};
#[cfg(feature = "gecko")]
use crate::values::specified::position::PositionTryFallbacksItem;
use crate::values::specified::position::PositionTryFallbacksTryTactic;
use crate::values::{computed, AtomIdent, Parser, SourceLocation};
use crate::AllocErr;
use crate::ArcSlice;
use crate::{Atom, LocalName, Namespace, ShrinkIfNeeded, WeakAtom};
use dom::{DocumentState, ElementState};
#[cfg(feature = "gecko")]
use malloc_size_of::MallocUnconditionalShallowSizeOf;
use malloc_size_of::{MallocShallowSizeOf, MallocSizeOf, MallocSizeOfOps};
use rustc_hash::FxHashMap;
use selectors::attr::{CaseSensitivity, NamespaceConstraint};
use selectors::bloom::BloomFilter;
use selectors::matching::{
    matches_complex_selector, matches_selector, selector_may_match, MatchingContext, MatchingMode,
    NeedsSelectorFlags, SelectorCaches, SubjectOrPseudoElement,
};
use selectors::matching::{MatchingForInvalidation, VisitedHandlingMode};
use selectors::parser::{
    AncestorHashes, Combinator, Component, MatchesFeaturelessHost, Selector, SelectorIter,
    SelectorList,
};
use selectors::visitor::{SelectorListKind, SelectorVisitor};
use servo_arc::{Arc, ArcBorrow, ThinArc};
use smallvec::SmallVec;
use std::cmp::Ordering;
use std::hash::{Hash, Hasher};
use std::mem;
use std::sync::{LazyLock, Mutex};

/// The type of the stylesheets that the stylist contains.
#[cfg(feature = "servo")]
pub type StylistSheet = crate::stylesheets::DocumentStyleSheet;

/// The type of the stylesheets that the stylist contains.
#[cfg(feature = "gecko")]
pub type StylistSheet = crate::gecko::data::GeckoStyleSheet;

#[derive(Debug, Clone)]
struct StylesheetContentsPtr(Arc<StylesheetContents>);

impl PartialEq for StylesheetContentsPtr {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}

impl Eq for StylesheetContentsPtr {}

impl Hash for StylesheetContentsPtr {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let contents: &StylesheetContents = &self.0;
        (contents as *const StylesheetContents).hash(state)
    }
}

type StyleSheetContentList = Vec<StylesheetContentsPtr>;

/// The @position-try rules that have changed.
#[derive(Default, Debug, MallocSizeOf)]
pub struct CascadeDataDifference {
    /// The set of changed @position-try rule names.
    pub changed_position_try_names: PrecomputedHashSet<Atom>,
}

impl CascadeDataDifference {
    /// Merges another difference into `self`.
    pub fn merge_with(&mut self, other: Self) {
        self.changed_position_try_names
            .extend(other.changed_position_try_names)
    }

    /// Returns whether we're empty.
    pub fn is_empty(&self) -> bool {
        self.changed_position_try_names.is_empty()
    }

    fn update(&mut self, old_data: &PositionTryMap, new_data: &PositionTryMap) {
        let mut any_different_key = false;
        let different_len = old_data.len() != new_data.len();
        for (name, rules) in old_data.iter() {
            let changed = match new_data.get(name) {
                Some(new_rule) => !Arc::ptr_eq(&rules.last().unwrap().0, new_rule),
                None => {
                    any_different_key = true;
                    true
                },
            };
            if changed {
                self.changed_position_try_names.insert(name.clone());
            }
        }

        if any_different_key || different_len {
            for name in new_data.keys() {
                // If the key exists in both, we've already checked it above.
                if !old_data.contains_key(name) {
                    self.changed_position_try_names.insert(name.clone());
                }
            }
        }
    }
}

/// A key in the cascade data cache.
#[derive(Debug, Hash, Default, PartialEq, Eq)]
struct CascadeDataCacheKey {
    media_query_results: Vec<MediaListKey>,
    contents: StyleSheetContentList,
}

unsafe impl Send for CascadeDataCacheKey {}
unsafe impl Sync for CascadeDataCacheKey {}

trait CascadeDataCacheEntry: Sized {
    /// Rebuilds the cascade data for the new stylesheet collection. The
    /// collection is guaranteed to be dirty.
    fn rebuild<S>(
        device: &Device,
        quirks_mode: QuirksMode,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        old_entry: &Self,
        difference: &mut CascadeDataDifference,
    ) -> Result<Arc<Self>, AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static;
    /// Measures heap memory usage.
    #[cfg(feature = "gecko")]
    fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes);
}

struct CascadeDataCache<Entry> {
    entries: FxHashMap<CascadeDataCacheKey, Arc<Entry>>,
}

impl<Entry> CascadeDataCache<Entry>
where
    Entry: CascadeDataCacheEntry,
{
    fn new() -> Self {
        Self {
            entries: Default::default(),
        }
    }

    fn len(&self) -> usize {
        self.entries.len()
    }

    // FIXME(emilio): This may need to be keyed on quirks-mode too, though for
    // UA sheets there aren't class / id selectors on those sheets, usually, so
    // it's probably ok... For the other cache the quirks mode shouldn't differ
    // so also should be fine.
    fn lookup<S>(
        &mut self,
        device: &Device,
        quirks_mode: QuirksMode,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        old_entry: &Entry,
        difference: &mut CascadeDataDifference,
    ) -> Result<Option<Arc<Entry>>, AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        use std::collections::hash_map::Entry as HashMapEntry;
        debug!("StyleSheetCache::lookup({})", self.len());

        if !collection.dirty() {
            return Ok(None);
        }

        let mut key = CascadeDataCacheKey::default();
        let mut custom_media_map = CustomMediaMap::default();
        for sheet in collection.sheets() {
            CascadeData::collect_applicable_media_query_results_into(
                device,
                sheet,
                guard,
                &mut key.media_query_results,
                &mut key.contents,
                &mut custom_media_map,
            )
        }

        let new_entry;
        match self.entries.entry(key) {
            HashMapEntry::Vacant(e) => {
                debug!("> Picking the slow path (not in the cache)");
                new_entry = Entry::rebuild(
                    device,
                    quirks_mode,
                    collection,
                    guard,
                    old_entry,
                    difference,
                )?;
                e.insert(new_entry.clone());
            },
            HashMapEntry::Occupied(mut e) => {
                // Avoid reusing our old entry (this can happen if we get
                // invalidated due to CSSOM mutations and our old stylesheet
                // contents were already unique, for example).
                if !std::ptr::eq(&**e.get(), old_entry) {
                    if log_enabled!(log::Level::Debug) {
                        debug!("cache hit for:");
                        for sheet in collection.sheets() {
                            debug!(" > {:?}", sheet);
                        }
                    }
                    // The line below ensures the "committed" bit is updated
                    // properly.
                    collection.each(|_, _, _| true);
                    return Ok(Some(e.get().clone()));
                }

                debug!("> Picking the slow path due to same entry as old");
                new_entry = Entry::rebuild(
                    device,
                    quirks_mode,
                    collection,
                    guard,
                    old_entry,
                    difference,
                )?;
                e.insert(new_entry.clone());
            },
        }

        Ok(Some(new_entry))
    }

    /// Returns all the cascade datas that are not being used (that is, that are
    /// held alive just by this cache).
    ///
    /// We return them instead of dropping in place because some of them may
    /// keep alive some other documents (like the SVG documents kept alive by
    /// URL references), and thus we don't want to drop them while locking the
    /// cache to not deadlock.
    fn take_unused(&mut self) -> SmallVec<[Arc<Entry>; 3]> {
        let mut unused = SmallVec::new();
        self.entries.retain(|_key, value| {
            // is_unique() returns false for static references, but we never
            // have static references to UserAgentCascadeDatas.  If we did, it
            // may not make sense to put them in the cache in the first place.
            if !value.is_unique() {
                return true;
            }
            unused.push(value.clone());
            false
        });
        unused
    }

    fn take_all(&mut self) -> FxHashMap<CascadeDataCacheKey, Arc<Entry>> {
        mem::take(&mut self.entries)
    }

    #[cfg(feature = "gecko")]
    fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        sizes.mOther += self.entries.shallow_size_of(ops);
        for (_key, arc) in self.entries.iter() {
            // These are primary Arc references that can be measured
            // unconditionally.
            sizes.mOther += arc.unconditional_shallow_size_of(ops);
            arc.add_size_of(ops, sizes);
        }
    }
}

/// Measure heap usage of UA_CASCADE_DATA_CACHE.
#[cfg(feature = "gecko")]
pub fn add_size_of_ua_cache(ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
    UA_CASCADE_DATA_CACHE
        .lock()
        .unwrap()
        .add_size_of(ops, sizes);
}

/// A cache of computed user-agent data, to be shared across documents.
static UA_CASCADE_DATA_CACHE: LazyLock<Mutex<UserAgentCascadeDataCache>> =
    LazyLock::new(|| Mutex::new(UserAgentCascadeDataCache::new()));

impl CascadeDataCacheEntry for UserAgentCascadeData {
    fn rebuild<S>(
        device: &Device,
        quirks_mode: QuirksMode,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        old: &Self,
        difference: &mut CascadeDataDifference,
    ) -> Result<Arc<Self>, AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        // TODO: Maybe we should support incremental rebuilds, though they seem uncommon and
        // rebuild() doesn't deal with precomputed_pseudo_element_decls for now so...
        let mut new_data = servo_arc::UniqueArc::new(Self {
            cascade_data: CascadeData::new(),
            precomputed_pseudo_element_decls: PrecomputedPseudoElementDeclarations::default(),
        });

        for (index, sheet) in collection.sheets().enumerate() {
            let new_data = &mut *new_data;
            new_data.cascade_data.add_stylesheet(
                device,
                quirks_mode,
                sheet,
                index,
                guard,
                SheetRebuildKind::Full,
                Some(&mut new_data.precomputed_pseudo_element_decls),
                None,
            )?;
        }

        new_data.cascade_data.did_finish_rebuild();
        difference.update(
            &old.cascade_data.extra_data.position_try_rules,
            &new_data.cascade_data.extra_data.position_try_rules,
        );

        Ok(new_data.shareable())
    }

    #[cfg(feature = "gecko")]
    fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        self.cascade_data.add_size_of(ops, sizes);
        sizes.mPrecomputedPseudos += self.precomputed_pseudo_element_decls.size_of(ops);
    }
}

type UserAgentCascadeDataCache = CascadeDataCache<UserAgentCascadeData>;

/// The declarations for the precomputed pseudo-elements.
///
/// We don't need the rest of the fields of `ApplicableDeclarationBlock`: These are never sorted
/// (they're in document order, and their selectors are all universal), and they're all UA rules in
/// the root layer.
type PrecomputedPseudoElementDeclarations = PerPseudoElementMap<Vec<StyleSource>>;

#[derive(Default)]
struct UserAgentCascadeData {
    cascade_data: CascadeData,

    /// Applicable declarations for a given non-eagerly cascaded pseudo-element.
    ///
    /// These are eagerly computed once, and then used to resolve the new
    /// computed values on the fly on layout.
    ///
    /// These are only filled from UA stylesheets.
    precomputed_pseudo_element_decls: PrecomputedPseudoElementDeclarations,
}

/// The empty UA cascade data for un-filled stylists.
static EMPTY_UA_CASCADE_DATA: LazyLock<Arc<UserAgentCascadeData>> = LazyLock::new(|| {
    let arc = Arc::new(UserAgentCascadeData::default());
    arc.mark_as_intentionally_leaked();
    arc
});

/// All the computed information for all the stylesheets that apply to the
/// document.
#[derive(MallocSizeOf)]
pub struct DocumentCascadeData {
    #[ignore_malloc_size_of = "Arc, owned by UserAgentCascadeDataCache or empty"]
    user_agent: Arc<UserAgentCascadeData>,
    user: CascadeData,
    author: CascadeData,
    per_origin: PerOrigin<()>,
}

impl Default for DocumentCascadeData {
    fn default() -> Self {
        Self {
            user_agent: EMPTY_UA_CASCADE_DATA.clone(),
            user: Default::default(),
            author: Default::default(),
            per_origin: Default::default(),
        }
    }
}

/// An iterator over the cascade data of a given document.
pub struct DocumentCascadeDataIter<'a> {
    iter: PerOriginIter<'a, ()>,
    cascade_data: &'a DocumentCascadeData,
}

impl<'a> Iterator for DocumentCascadeDataIter<'a> {
    type Item = (&'a CascadeData, Origin);

    fn next(&mut self) -> Option<Self::Item> {
        let (_, origin) = self.iter.next()?;
        Some((self.cascade_data.borrow_for_origin(origin), origin))
    }
}

impl DocumentCascadeData {
    /// Borrows the cascade data for a given origin.
    #[inline]
    pub fn borrow_for_origin(&self, origin: Origin) -> &CascadeData {
        match origin {
            Origin::UserAgent => &self.user_agent.cascade_data,
            Origin::Author => &self.author,
            Origin::User => &self.user,
        }
    }

    fn iter_origins(&self) -> DocumentCascadeDataIter<'_> {
        DocumentCascadeDataIter {
            iter: self.per_origin.iter_origins(),
            cascade_data: self,
        }
    }

    fn iter_origins_rev(&self) -> DocumentCascadeDataIter<'_> {
        DocumentCascadeDataIter {
            iter: self.per_origin.iter_origins_rev(),
            cascade_data: self,
        }
    }

    fn custom_media_for_sheet(
        &self,
        s: &StylistSheet,
        guard: &SharedRwLockReadGuard,
    ) -> &CustomMediaMap {
        let origin = s.contents(guard).origin;
        &self.borrow_for_origin(origin).custom_media
    }

    /// Rebuild the cascade data for the given document stylesheets, and
    /// optionally with a set of user agent stylesheets.  Returns Err(..)
    /// to signify OOM.
    fn rebuild<'a, S>(
        &mut self,
        device: &Device,
        quirks_mode: QuirksMode,
        mut flusher: DocumentStylesheetFlusher<'a, S>,
        guards: &StylesheetGuards,
        difference: &mut CascadeDataDifference,
    ) -> Result<(), AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        // First do UA sheets.
        {
            let origin_flusher = flusher.flush_origin(Origin::UserAgent);
            // Dirty check is just a minor optimization (no need to grab the
            // lock if nothing has changed).
            if origin_flusher.dirty() {
                let mut ua_cache = UA_CASCADE_DATA_CACHE.lock().unwrap();
                let new_data = ua_cache.lookup(
                    device,
                    quirks_mode,
                    origin_flusher,
                    guards.ua_or_user,
                    &self.user_agent,
                    difference,
                )?;
                if let Some(new_data) = new_data {
                    self.user_agent = new_data;
                }
                let _unused_entries = ua_cache.take_unused();
                // See the comments in take_unused() as for why the following line.
                std::mem::drop(ua_cache);
            }
        }

        // Now do the user sheets.
        self.user.rebuild(
            device,
            quirks_mode,
            flusher.flush_origin(Origin::User),
            guards.ua_or_user,
            difference,
        )?;

        // And now the author sheets.
        self.author.rebuild(
            device,
            quirks_mode,
            flusher.flush_origin(Origin::Author),
            guards.author,
            difference,
        )?;

        Ok(())
    }

    /// Measures heap usage.
    #[cfg(feature = "gecko")]
    pub fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        self.user.add_size_of(ops, sizes);
        self.author.add_size_of(ops, sizes);
    }
}

/// Whether author styles are enabled.
///
/// This is used to support Gecko.
#[allow(missing_docs)]
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq)]
pub enum AuthorStylesEnabled {
    Yes,
    No,
}

/// A wrapper over a DocumentStylesheetSet that can be `Sync`, since it's only
/// used and exposed via mutable methods in the `Stylist`.
#[cfg_attr(feature = "servo", derive(MallocSizeOf))]
#[derive(Deref, DerefMut)]
struct StylistStylesheetSet(DocumentStylesheetSet<StylistSheet>);
// Read above to see why this is fine.
unsafe impl Sync for StylistStylesheetSet {}

impl StylistStylesheetSet {
    fn new() -> Self {
        StylistStylesheetSet(DocumentStylesheetSet::new())
    }
}

/// This structure holds all the selectors and device characteristics
/// for a given document. The selectors are converted into `Rule`s
/// and sorted into `SelectorMap`s keyed off stylesheet origin and
/// pseudo-element (see `CascadeData`).
///
/// This structure is effectively created once per pipeline, in the
/// LayoutThread corresponding to that pipeline.
#[cfg_attr(feature = "servo", derive(MallocSizeOf))]
pub struct Stylist {
    /// Device that the stylist is currently evaluating against.
    ///
    /// This field deserves a bigger comment due to the different use that Gecko
    /// and Servo give to it (that we should eventually unify).
    ///
    /// With Gecko, the device is never changed. Gecko manually tracks whether
    /// the device data should be reconstructed, and "resets" the state of the
    /// device.
    ///
    /// On Servo, on the other hand, the device is a really cheap representation
    /// that is recreated each time some constraint changes and calling
    /// `set_device`.
    device: Device,

    /// The list of stylesheets.
    stylesheets: StylistStylesheetSet,

    /// A cache of CascadeDatas for AuthorStylesheetSets (i.e., shadow DOM).
    #[cfg_attr(feature = "servo", ignore_malloc_size_of = "XXX: how to handle this?")]
    author_data_cache: CascadeDataCache<CascadeData>,

    /// If true, the quirks-mode stylesheet is applied.
    #[cfg_attr(feature = "servo", ignore_malloc_size_of = "defined in selectors")]
    quirks_mode: QuirksMode,

    /// Selector maps for all of the style sheets in the stylist, after
    /// evalutaing media rules against the current device, split out per
    /// cascade level.
    cascade_data: DocumentCascadeData,

    /// Whether author styles are enabled.
    author_styles_enabled: AuthorStylesEnabled,

    /// The rule tree, that stores the results of selector matching.
    rule_tree: RuleTree,

    /// The set of registered custom properties from script.
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#dom-window-registeredpropertyset-slot>
    script_custom_properties: CustomPropertyScriptRegistry,

    /// Initial values for registered custom properties.
    #[cfg_attr(feature = "servo", ignore_malloc_size_of = "Arc")]
    initial_values_for_custom_properties: ComputedCustomProperties,

    /// Flags set from computing registered custom property initial values.
    initial_values_for_custom_properties_flags: ComputedValueFlags,

    /// The total number of times the stylist has been rebuilt.
    num_rebuilds: usize,
}

/// What cascade levels to include when styling elements.
#[derive(Clone, Copy, PartialEq)]
pub enum RuleInclusion {
    /// Include rules for style sheets at all cascade levels.  This is the
    /// normal rule inclusion mode.
    All,
    /// Only include rules from UA and user level sheets.  Used to implement
    /// `getDefaultComputedStyle`.
    DefaultOnly,
}

#[cfg(feature = "gecko")]
impl From<StyleRuleInclusion> for RuleInclusion {
    fn from(value: StyleRuleInclusion) -> Self {
        match value {
            StyleRuleInclusion::All => RuleInclusion::All,
            StyleRuleInclusion::DefaultOnly => RuleInclusion::DefaultOnly,
        }
    }
}

/// `:scope` selector, depending on the use case, can match a shadow host.
/// If used outside of `@scope`, it cannot possibly match the host.
/// Even when inside of `@scope`, it's conditional if the selector will
/// match the shadow host.
#[derive(Clone, Copy, Eq, PartialEq)]
enum ScopeMatchesShadowHost {
    NotApplicable,
    No,
    Yes,
}

impl Default for ScopeMatchesShadowHost {
    fn default() -> Self {
        Self::NotApplicable
    }
}

impl ScopeMatchesShadowHost {
    fn nest_for_scope(&mut self, matches_shadow_host: bool) {
        match *self {
            Self::NotApplicable => {
                // We're at the outermost `@scope`.
                *self = if matches_shadow_host {
                    Self::Yes
                } else {
                    Self::No
                };
            },
            Self::Yes if !matches_shadow_host => {
                // Inner `@scope` will not be able to match the shadow host.
                *self = Self::No;
            },
            _ => (),
        }
    }
}

/// Nested declarations have effectively two behaviors:
///  * Inside style rules (where they behave as the containing selector).
///  * Inside @scope (where they behave as :where(:scope)).
/// It is a bit unfortunate ideally we wouldn't need this, because scope also pushes to the
/// ancestor_selector_lists, but the behavior isn't quite the same as wrapping in `&`, see
/// https://github.com/w3c/csswg-drafts/issues/10431
#[derive(Copy, Clone)]
enum NestedDeclarationsContext {
    Style,
    Scope,
}

/// A struct containing state related to scope rules
struct ContainingScopeRuleState {
    id: ScopeConditionId,
    inner_dependencies: Vec<Dependency>,
    matches_shadow_host: ScopeMatchesShadowHost,
}

impl Default for ContainingScopeRuleState {
    fn default() -> Self {
        Self {
            id: ScopeConditionId::none(),
            inner_dependencies: Vec::new(),
            matches_shadow_host: Default::default(),
        }
    }
}

impl ContainingScopeRuleState {
    fn save(&self) -> SavedContainingScopeRuleState {
        SavedContainingScopeRuleState {
            id: self.id,
            matches_shadow_host: self.matches_shadow_host,
            inner_dependencies_len: self.inner_dependencies.len(),
        }
    }

    fn restore(
        &mut self,
        saved: &SavedContainingScopeRuleState,
    ) -> Option<(Vec<Dependency>, ScopeConditionId)> {
        debug_assert!(self.inner_dependencies.len() >= saved.inner_dependencies_len);

        if self.id == saved.id {
            return None;
        }

        let scope_id = self.id;
        let inner_deps = self
            .inner_dependencies
            .drain(saved.inner_dependencies_len..)
            .collect();

        self.id = saved.id;
        self.matches_shadow_host = saved.matches_shadow_host;

        Some((inner_deps, scope_id))
    }
}

struct SavedContainingScopeRuleState {
    id: ScopeConditionId,
    matches_shadow_host: ScopeMatchesShadowHost,
    inner_dependencies_len: usize,
}

/// A struct containing state from ancestor rules like @layer / @import /
/// @container / nesting / @scope.
struct ContainingRuleState {
    layer_name: LayerName,
    layer_id: LayerId,
    container_condition_id: ContainerConditionId,
    cascade_flags: RuleCascadeFlags,
    containing_scope_rule_state: ContainingScopeRuleState,
    ancestor_selector_lists: SmallVec<[SelectorList<SelectorImpl>; 2]>,
    nested_declarations_context: NestedDeclarationsContext,
}

impl Default for ContainingRuleState {
    fn default() -> Self {
        Self {
            layer_name: LayerName::new_empty(),
            layer_id: LayerId::root(),
            container_condition_id: ContainerConditionId::none(),
            cascade_flags: RuleCascadeFlags::empty(),
            ancestor_selector_lists: Default::default(),
            containing_scope_rule_state: Default::default(),
            nested_declarations_context: NestedDeclarationsContext::Style,
        }
    }
}

struct SavedContainingRuleState {
    ancestor_selector_lists_len: usize,
    layer_name_len: usize,
    layer_id: LayerId,
    container_condition_id: ContainerConditionId,
    cascade_flags: RuleCascadeFlags,
    saved_containing_scope_rule_state: SavedContainingScopeRuleState,
    nested_declarations_context: NestedDeclarationsContext,
}

impl ContainingRuleState {
    fn save(&self) -> SavedContainingRuleState {
        SavedContainingRuleState {
            ancestor_selector_lists_len: self.ancestor_selector_lists.len(),
            layer_name_len: self.layer_name.0.len(),
            layer_id: self.layer_id,
            container_condition_id: self.container_condition_id,
            cascade_flags: self.cascade_flags,
            saved_containing_scope_rule_state: self.containing_scope_rule_state.save(),
            nested_declarations_context: self.nested_declarations_context,
        }
    }

    fn restore(
        &mut self,
        saved: &SavedContainingRuleState,
    ) -> Option<(Vec<Dependency>, ScopeConditionId)> {
        debug_assert!(self.layer_name.0.len() >= saved.layer_name_len);
        debug_assert!(self.ancestor_selector_lists.len() >= saved.ancestor_selector_lists_len);

        self.ancestor_selector_lists
            .truncate(saved.ancestor_selector_lists_len);
        self.layer_name.0.truncate(saved.layer_name_len);
        self.layer_id = saved.layer_id;
        self.container_condition_id = saved.container_condition_id;
        self.cascade_flags = saved.cascade_flags;
        self.nested_declarations_context = saved.nested_declarations_context;

        self.containing_scope_rule_state
            .restore(&saved.saved_containing_scope_rule_state)
    }

    fn scope_is_effective(&self) -> bool {
        self.containing_scope_rule_state.id != ScopeConditionId::none()
    }

    fn cascade_flags(&self) -> RuleCascadeFlags {
        self.cascade_flags
    }
}

type ReplacedSelectors = SmallVec<[Selector<SelectorImpl>; 4]>;

impl Stylist {
    /// Construct a new `Stylist`, using given `Device` and `QuirksMode`.
    /// If more members are added here, think about whether they should
    /// be reset in clear().
    #[inline]
    pub fn new(device: Device, quirks_mode: QuirksMode) -> Self {
        Self {
            device,
            quirks_mode,
            stylesheets: StylistStylesheetSet::new(),
            author_data_cache: CascadeDataCache::new(),
            cascade_data: Default::default(),
            author_styles_enabled: AuthorStylesEnabled::Yes,
            rule_tree: RuleTree::new(),
            script_custom_properties: Default::default(),
            initial_values_for_custom_properties: Default::default(),
            initial_values_for_custom_properties_flags: Default::default(),
            num_rebuilds: 0,
        }
    }

    /// Returns the document cascade data.
    #[inline]
    pub fn cascade_data(&self) -> &DocumentCascadeData {
        &self.cascade_data
    }

    /// Returns whether author styles are enabled or not.
    #[inline]
    pub fn author_styles_enabled(&self) -> AuthorStylesEnabled {
        self.author_styles_enabled
    }

    /// Iterate through all the cascade datas from the document.
    #[inline]
    pub fn iter_origins(&self) -> DocumentCascadeDataIter<'_> {
        self.cascade_data.iter_origins()
    }

    /// Does what the name says, to prevent author_data_cache to grow without
    /// bound.
    pub fn remove_unique_author_data_cache_entries(&mut self) {
        self.author_data_cache.take_unused();
    }

    /// Returns the custom property registration for this property's name.
    /// https://drafts.css-houdini.org/css-properties-values-api-1/#determining-registration
    pub fn get_custom_property_registration(&self, name: &Atom) -> &PropertyDescriptors {
        if let Some(registration) = self.custom_property_script_registry().get(name) {
            return &registration.descriptors;
        }
        for (data, _) in self.iter_origins() {
            if let Some(registration) = data.custom_property_registrations.get(name) {
                return &registration.descriptors;
            }
        }
        PropertyDescriptors::unregistered()
    }

    /// Returns custom properties with their registered initial values.
    pub fn get_custom_property_initial_values(&self) -> &ComputedCustomProperties {
        &self.initial_values_for_custom_properties
    }

    /// Returns flags set from computing the registered custom property initial values.
    pub fn get_custom_property_initial_values_flags(&self) -> ComputedValueFlags {
        self.initial_values_for_custom_properties_flags
    }

    /// Rebuild custom properties with their registered initial values.
    /// https://drafts.css-houdini.org/css-properties-values-api-1/#determining-registration
    pub fn rebuild_initial_values_for_custom_properties(&mut self) {
        let mut initial_values = ComputedCustomProperties::default();
        let initial_values_flags;
        {
            let mut seen_names = PrecomputedHashSet::default();
            let mut rule_cache_conditions = RuleCacheConditions::default();
            let mut tree_counting_caches = TreeCountingCaches::default();
            let context = computed::Context::new_for_initial_at_property_value(
                self,
                &mut rule_cache_conditions,
                &mut tree_counting_caches,
            );

            for (k, v) in self.custom_property_script_registry().properties().iter() {
                seen_names.insert(k.clone());
                let Ok(value) = v.compute_initial_value(&context) else {
                    continue;
                };
                let map = if v.descriptors.inherits() {
                    &mut initial_values.inherited
                } else {
                    &mut initial_values.non_inherited
                };
                map.insert(k, value);
            }
            for (data, _) in self.iter_origins() {
                for (k, v) in data.custom_property_registrations.iter() {
                    if seen_names.insert(k.clone()) {
                        let last_value = &v.last().unwrap().0;
                        let Ok(value) = last_value.compute_initial_value(&context) else {
                            continue;
                        };
                        let map = if last_value.descriptors.inherits() {
                            &mut initial_values.inherited
                        } else {
                            &mut initial_values.non_inherited
                        };
                        map.insert(k, value);
                    }
                }
            }
            initial_values_flags = context.builder.flags();
        }
        self.initial_values_for_custom_properties_flags = initial_values_flags;
        self.initial_values_for_custom_properties = initial_values;
    }

    /// Rebuilds (if needed) the CascadeData given a sheet collection.
    pub fn rebuild_author_data<S>(
        &mut self,
        old_data: &CascadeData,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        difference: &mut CascadeDataDifference,
    ) -> Result<Option<Arc<CascadeData>>, AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        self.author_data_cache.lookup(
            &self.device,
            self.quirks_mode,
            collection,
            guard,
            old_data,
            difference,
        )
    }

    /// Iterate over the extra data in origin order.
    #[inline]
    pub fn iter_extra_data_origins(&self) -> ExtraStyleDataIterator<'_> {
        ExtraStyleDataIterator(self.cascade_data.iter_origins())
    }

    /// Iterate over the extra data in reverse origin order.
    #[inline]
    pub fn iter_extra_data_origins_rev(&self) -> ExtraStyleDataIterator<'_> {
        ExtraStyleDataIterator(self.cascade_data.iter_origins_rev())
    }

    /// Returns the number of selectors.
    pub fn num_selectors(&self) -> usize {
        self.cascade_data
            .iter_origins()
            .map(|(d, _)| d.num_selectors)
            .sum()
    }

    /// Returns the number of declarations.
    pub fn num_declarations(&self) -> usize {
        self.cascade_data
            .iter_origins()
            .map(|(d, _)| d.num_declarations)
            .sum()
    }

    /// Returns the number of times the stylist has been rebuilt.
    pub fn num_rebuilds(&self) -> usize {
        self.num_rebuilds
    }

    /// Returns the number of revalidation_selectors.
    pub fn num_revalidation_selectors(&self) -> usize {
        self.cascade_data
            .iter_origins()
            .map(|(data, _)| data.selectors_for_cache_revalidation.len())
            .sum()
    }

    /// Returns the number of entries in invalidation maps.
    pub fn num_invalidations(&self) -> usize {
        self.cascade_data
            .iter_origins()
            .map(|(data, _)| {
                data.invalidation_map.len() + data.relative_selector_invalidation_map.len()
            })
            .sum()
    }

    /// Returns whether the given DocumentState bit is relied upon by a selector
    /// of some rule.
    pub fn has_document_state_dependency(&self, state: DocumentState) -> bool {
        self.cascade_data
            .iter_origins()
            .any(|(d, _)| d.document_state_dependencies.intersects(state))
    }

    /// Flush the list of stylesheets if they changed, ensuring the stylist is
    /// up-to-date.
    pub fn flush(&mut self, guards: &StylesheetGuards) -> StylesheetInvalidationSet {
        if !self.stylesheets.has_changed() {
            return Default::default();
        }

        self.num_rebuilds += 1;

        let cascade_data = &self.cascade_data;
        let (flusher, mut invalidations) = self.stylesheets.flush(&self.device, guards, |origin| {
            &cascade_data.borrow_for_origin(origin).custom_media
        });

        self.cascade_data
            .rebuild(
                &self.device,
                self.quirks_mode,
                flusher,
                guards,
                &mut invalidations.cascade_data_difference,
            )
            .unwrap_or_else(|_| {
                warn!("OOM in Stylist::flush");
            });

        self.rebuild_initial_values_for_custom_properties();
        invalidations
    }

    /// Marks a given stylesheet origin as dirty, due to, for example, changes
    /// in the declarations that affect a given rule.
    ///
    /// FIXME(emilio): Eventually it'd be nice for this to become more
    /// fine-grained.
    pub fn force_stylesheet_origins_dirty(&mut self, origins: OriginSet) {
        self.stylesheets.force_dirty(origins)
    }

    /// Sets whether author style is enabled or not.
    pub fn set_author_styles_enabled(&mut self, enabled: AuthorStylesEnabled) {
        self.author_styles_enabled = enabled;
    }

    /// Returns whether we've recorded any stylesheet change so far.
    pub fn stylesheets_have_changed(&self) -> bool {
        self.stylesheets.has_changed()
    }

    /// Insert a given stylesheet before another stylesheet in the document.
    pub fn insert_stylesheet_before(
        &mut self,
        sheet: StylistSheet,
        before_sheet: StylistSheet,
        guard: &SharedRwLockReadGuard,
    ) {
        self.stylesheets
            .insert_stylesheet_before(sheet, before_sheet, guard)
    }

    /// Appends a new stylesheet to the current set.
    pub fn append_stylesheet(&mut self, sheet: StylistSheet, guard: &SharedRwLockReadGuard) {
        self.stylesheets.append_stylesheet(sheet, guard)
    }

    /// Remove a given stylesheet to the current set.
    pub fn remove_stylesheet(&mut self, sheet: StylistSheet, guard: &SharedRwLockReadGuard) {
        let custom_media = self.cascade_data.custom_media_for_sheet(&sheet, guard);
        self.stylesheets
            .remove_stylesheet(Some(&self.device), custom_media, sheet, guard)
    }

    /// Notify of a change of a given rule.
    pub fn rule_changed(
        &mut self,
        sheet: &StylistSheet,
        rule: &CssRule,
        guard: &SharedRwLockReadGuard,
        change_kind: RuleChangeKind,
        ancestors: &[CssRuleRef],
    ) {
        let custom_media = self.cascade_data.custom_media_for_sheet(sheet, guard);
        self.stylesheets.rule_changed(
            Some(&self.device),
            custom_media,
            sheet,
            rule,
            guard,
            change_kind,
            ancestors,
        )
    }

    /// Get the total stylesheet count for a given origin.
    #[inline]
    pub fn sheet_count(&self, origin: Origin) -> usize {
        self.stylesheets.sheet_count(origin)
    }

    /// Get the index-th stylesheet for a given origin.
    #[inline]
    pub fn sheet_at(&self, origin: Origin, index: usize) -> Option<&StylistSheet> {
        self.stylesheets.get(origin, index)
    }

    /// Returns whether for any of the applicable style rule data a given
    /// condition is true.
    pub fn any_applicable_rule_data<E, F>(&self, element: E, mut f: F) -> bool
    where
        E: TElement,
        F: FnMut(&CascadeData) -> bool,
    {
        if f(&self.cascade_data.user_agent.cascade_data) {
            return true;
        }

        let mut maybe = false;

        let doc_author_rules_apply =
            element.each_applicable_non_document_style_rule_data(|data, _| {
                maybe = maybe || f(data);
            });

        if maybe || f(&self.cascade_data.user) {
            return true;
        }

        doc_author_rules_apply && f(&self.cascade_data.author)
    }

    /// Execute callback for all applicable style rule data.
    pub fn for_each_cascade_data_with_scope<'a, E, F>(&'a self, element: E, mut f: F)
    where
        E: TElement + 'a,
        F: FnMut(&'a CascadeData, Option<E>),
    {
        f(&self.cascade_data.user_agent.cascade_data, None);
        element.each_applicable_non_document_style_rule_data(|data, scope| {
            f(data, Some(scope));
        });
        f(&self.cascade_data.user, None);
        f(&self.cascade_data.author, None);
    }

    /// Computes the style for a given "precomputed" pseudo-element, taking the
    /// universal rules and applying them.
    pub fn precomputed_values_for_pseudo<E>(
        &self,
        guards: &StylesheetGuards,
        pseudo: &PseudoElement,
        parent: Option<&ComputedValues>,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        debug_assert!(pseudo.is_precomputed());

        let rule_node = self.rule_node_for_precomputed_pseudo(guards, pseudo, &[]);

        self.precomputed_values_for_pseudo_with_rule_node::<E>(guards, pseudo, parent, rule_node)
    }

    /// Computes the style for a given "precomputed" pseudo-element with
    /// given rule node.
    ///
    /// TODO(emilio): The type parameter could go away with a void type
    /// implementing TElement.
    pub fn precomputed_values_for_pseudo_with_rule_node<E>(
        &self,
        guards: &StylesheetGuards,
        pseudo: &PseudoElement,
        parent: Option<&ComputedValues>,
        rules: StrongRuleNode,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        self.compute_pseudo_element_style_with_inputs::<E>(
            CascadeInputs {
                rules: Some(rules),
                visited_rules: None,
                flags: Default::default(),
                included_cascade_flags: RuleCascadeFlags::empty(),
            },
            pseudo,
            guards,
            parent,
            /* element */ None,
        )
    }

    /// Returns the rule node for a given precomputed pseudo-element.
    ///
    /// If we want to include extra declarations to this precomputed
    /// pseudo-element, we can provide a vector of ApplicableDeclarationBlocks
    /// to extra_declarations. This is useful for @page rules.
    pub fn rule_node_for_precomputed_pseudo(
        &self,
        guards: &StylesheetGuards,
        pseudo: &PseudoElement,
        extra_declarations: &[ApplicableDeclarationBlock],
    ) -> StrongRuleNode {
        let declarations = match self
            .cascade_data
            .user_agent
            .precomputed_pseudo_element_decls
            .get(pseudo)
        {
            Some(declarations) => &**declarations,
            None => &[],
        };

        let cascade_priority = CascadePriority::new(
            CascadeLevel::new(CascadeOrigin::UA),
            LayerOrder::root(),
            RuleCascadeFlags::empty(),
        );
        self.rule_tree.insert_ordered_rules_with_important(
            declarations
                .iter()
                .map(|source| (source.borrow(), cascade_priority))
                .chain(extra_declarations.iter().map(|d| d.for_rule_tree())),
            guards,
        )
    }

    /// Returns the style for an anonymous box of the given type.
    ///
    /// TODO(emilio): The type parameter could go away with a void type
    /// implementing TElement.
    #[cfg(feature = "servo")]
    pub fn style_for_anonymous<E>(
        &self,
        guards: &StylesheetGuards,
        pseudo: &PseudoElement,
        parent_style: &ComputedValues,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        self.precomputed_values_for_pseudo::<E>(guards, &pseudo, Some(parent_style))
    }

    /// Computes a pseudo-element style lazily during layout.
    ///
    /// This can only be done for a certain set of pseudo-elements, like
    /// :selection.
    ///
    /// Check the documentation on lazy pseudo-elements in
    /// docs/components/style.md
    pub fn lazily_compute_pseudo_element_style<E>(
        &self,
        guards: &StylesheetGuards,
        element: E,
        pseudo: &PseudoElement,
        rule_inclusion: RuleInclusion,
        originating_element_style: &ComputedValues,
        is_probe: bool,
        matching_fn: Option<&dyn Fn(&PseudoElement) -> bool>,
    ) -> Option<Arc<ComputedValues>>
    where
        E: TElement,
    {
        let cascade_inputs = self.lazy_pseudo_rules(
            guards,
            element,
            originating_element_style,
            pseudo,
            is_probe,
            rule_inclusion,
            matching_fn,
        )?;

        Some(self.compute_pseudo_element_style_with_inputs(
            cascade_inputs,
            pseudo,
            guards,
            Some(originating_element_style),
            Some(element),
        ))
    }

    /// Computes a pseudo-element style lazily using the given CascadeInputs.
    /// This can be used for truly lazy pseudo-elements or to avoid redoing
    /// selector matching for eager pseudo-elements when we need to recompute
    /// their style with a new parent style.
    pub fn compute_pseudo_element_style_with_inputs<E>(
        &self,
        inputs: CascadeInputs,
        pseudo: &PseudoElement,
        guards: &StylesheetGuards,
        parent_style: Option<&ComputedValues>,
        element: Option<E>,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        // FIXME(emilio): The lack of layout_parent_style here could be
        // worrying, but we're probably dropping the display fixup for
        // pseudos other than before and after, so it's probably ok.
        //
        // (Though the flags don't indicate so!)
        //
        // It'd be fine to assert that this isn't called with a parent style
        // where display contents is in effect, but in practice this is hard to
        // do for stuff like :-moz-fieldset-content with a
        // <fieldset style="display: contents">. That is, the computed value of
        // display for the fieldset is "contents", even though it's not the used
        // value, so we don't need to adjust in a different way anyway.
        self.cascade_style_and_visited(
            element,
            Some(pseudo),
            &inputs,
            guards,
            parent_style,
            parent_style,
            FirstLineReparenting::No,
            &PositionTryFallbacksTryTactic::default(),
            /* rule_cache = */ None,
            &mut RuleCacheConditions::default(),
            &mut TreeCountingCaches::default(),
        )
    }

    /// Computes a fallback style lazily given the current and parent styles, and name.
    #[cfg(feature = "gecko")]
    pub fn resolve_position_try<E>(
        &self,
        style: &ComputedValues,
        guards: &StylesheetGuards,
        scope: CascadeLevel,
        element: E,
        fallback_item: &PositionTryFallbacksItem,
    ) -> Option<Arc<ComputedValues>>
    where
        E: TElement,
    {
        let name_and_try_tactic = match *fallback_item {
            PositionTryFallbacksItem::PositionArea(area) => {
                // We don't bother passing the parent_style argument here since
                // we probably don't need it. If we do, we could wrap this up in
                // a style_resolver::with_default_parent_styles call, as below.
                let mut builder =
                    StyleBuilder::for_derived_style(&self.device, Some(self), style, None);
                builder.rules = style.rules.clone();
                builder.mutate_position().set_position_area(area);
                return Some(builder.build());
            },
            PositionTryFallbacksItem::IdentAndOrTactic(ref name_and_try_tactic) => {
                name_and_try_tactic
            },
        };

        let fallback_rule = if !name_and_try_tactic.ident.is_empty() {
            Some(self.lookup_position_try(&name_and_try_tactic.ident.0, scope, element)?)
        } else {
            None
        };
        let fallback_block = fallback_rule
            .as_ref()
            .map(|r| &r.read_with(guards.author).block);
        let pseudo = style
            .pseudo()
            .or_else(|| element.implemented_pseudo_element());
        let inputs = {
            let mut inputs = CascadeInputs::new_from_style(style);
            // @position-try doesn't care about any :visited-dependent property.
            inputs.visited_rules = None;
            let rules = inputs.rules.as_ref().unwrap_or(self.rule_tree.root());
            let mut important_rules_changed = false;
            if let Some(fallback_block) = fallback_block {
                let new_rules = self.rule_tree.update_rule_at_level(
                    CascadeLevel::new(CascadeOrigin::PositionFallback),
                    LayerOrder::root(),
                    Some(fallback_block.borrow_arc()),
                    rules,
                    guards,
                    &mut important_rules_changed,
                );
                if new_rules.is_some() {
                    inputs.rules = new_rules;
                } else {
                    // This will return an identical style to `style`. We could consider optimizing
                    // this a bit more but for now just perform the cascade, this can only happen with
                    // the same position-try name repeated multiple times anyways.
                }
            }
            inputs
        };
        crate::style_resolver::with_default_parent_styles(
            element,
            |parent_style, layout_parent_style| {
                Some(self.cascade_style_and_visited(
                    Some(element),
                    pseudo.as_ref(),
                    &inputs,
                    guards,
                    parent_style,
                    layout_parent_style,
                    FirstLineReparenting::No,
                    &name_and_try_tactic.try_tactic,
                    /* rule_cache = */ None,
                    &mut RuleCacheConditions::default(),
                    &mut TreeCountingCaches::default(),
                ))
            },
        )
    }

    /// Computes a style using the given CascadeInputs.  This can be used to
    /// compute a style any time we know what rules apply and just need to use
    /// the given parent styles.
    ///
    /// parent_style is the style to inherit from for properties affected by
    /// first-line ancestors.
    ///
    /// parent_style_ignoring_first_line is the style to inherit from for
    /// properties not affected by first-line ancestors.
    ///
    /// layout_parent_style is the style used for some property fixups.  It's
    /// the style of the nearest ancestor with a layout box.
    pub fn cascade_style_and_visited<E>(
        &self,
        element: Option<E>,
        pseudo: Option<&PseudoElement>,
        inputs: &CascadeInputs,
        guards: &StylesheetGuards,
        parent_style: Option<&ComputedValues>,
        layout_parent_style: Option<&ComputedValues>,
        first_line_reparenting: FirstLineReparenting,
        try_tactic: &PositionTryFallbacksTryTactic,
        rule_cache: Option<&RuleCache>,
        rule_cache_conditions: &mut RuleCacheConditions,
        tree_counting_caches: &mut TreeCountingCaches,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        debug_assert!(pseudo.is_some() || element.is_some(), "Huh?");

        // We need to compute visited values if we have visited rules or if our
        // parent has visited values.
        let visited_rules = match inputs.visited_rules.as_ref() {
            Some(rules) => Some(rules),
            None => {
                if parent_style.and_then(|s| s.visited_style()).is_some() {
                    Some(inputs.rules.as_ref().unwrap_or(self.rule_tree.root()))
                } else {
                    None
                }
            },
        };

        let mut implemented_pseudo = None;
        // Read the comment on `precomputed_values_for_pseudo` to see why it's
        // difficult to assert that display: contents nodes never arrive here
        // (tl;dr: It doesn't apply for replaced elements and such, but the
        // computed value is still "contents").
        //
        // FIXME(emilio): We should assert that it holds if pseudo.is_none()!
        properties::cascade::<E>(
            self,
            pseudo.or_else(|| {
                implemented_pseudo = element.unwrap().implemented_pseudo_element();
                implemented_pseudo.as_ref()
            }),
            inputs.rules.as_ref().unwrap_or(self.rule_tree.root()),
            guards,
            parent_style,
            layout_parent_style,
            first_line_reparenting,
            try_tactic,
            visited_rules,
            inputs.flags,
            inputs.included_cascade_flags,
            rule_cache,
            rule_cache_conditions,
            element,
            tree_counting_caches,
        )
    }

    /// Computes the cascade inputs for a lazily-cascaded pseudo-element.
    ///
    /// See the documentation on lazy pseudo-elements in
    /// docs/components/style.md
    fn lazy_pseudo_rules<E>(
        &self,
        guards: &StylesheetGuards,
        element: E,
        originating_element_style: &ComputedValues,
        pseudo: &PseudoElement,
        is_probe: bool,
        rule_inclusion: RuleInclusion,
        matching_fn: Option<&dyn Fn(&PseudoElement) -> bool>,
    ) -> Option<CascadeInputs>
    where
        E: TElement,
    {
        debug_assert!(pseudo.is_lazy());

        let mut selector_caches = SelectorCaches::default();
        // No need to bother setting the selector flags when we're computing
        // default styles.
        let needs_selector_flags = if rule_inclusion == RuleInclusion::DefaultOnly {
            NeedsSelectorFlags::No
        } else {
            NeedsSelectorFlags::Yes
        };

        let animation_declarations = AnimationDeclarations::default();
        let mut declarations = ApplicableDeclarationList::new();
        let mut matching_context = MatchingContext::<'_, E::Impl>::new(
            MatchingMode::ForStatelessPseudoElement,
            None,
            &mut selector_caches,
            self.quirks_mode,
            needs_selector_flags,
            MatchingForInvalidation::No,
        );

        matching_context.pseudo_element_matching_fn = matching_fn;
        matching_context.extra_data.originating_element_style = Some(originating_element_style);

        self.push_applicable_declarations(
            element,
            Some(pseudo),
            None,
            None,
            &animation_declarations,
            rule_inclusion,
            &mut declarations,
            &mut matching_context,
        );

        if declarations.is_empty() && is_probe {
            return None;
        }

        let rules = self.rule_tree.compute_rule_node(&mut declarations, guards);

        let mut visited_rules = None;
        if originating_element_style.visited_style().is_some() {
            let mut declarations = ApplicableDeclarationList::new();
            let mut selector_caches = SelectorCaches::default();

            let mut matching_context = MatchingContext::<'_, E::Impl>::new_for_visited(
                MatchingMode::ForStatelessPseudoElement,
                None,
                &mut selector_caches,
                VisitedHandlingMode::RelevantLinkVisited,
                self.quirks_mode,
                needs_selector_flags,
                MatchingForInvalidation::No,
            );
            matching_context.pseudo_element_matching_fn = matching_fn;
            matching_context.extra_data.originating_element_style = Some(originating_element_style);

            self.push_applicable_declarations(
                element,
                Some(pseudo),
                None,
                None,
                &animation_declarations,
                rule_inclusion,
                &mut declarations,
                &mut matching_context,
            );
            if !declarations.is_empty() {
                let rule_node = self.rule_tree.insert_ordered_rules_with_important(
                    declarations.drain(..).map(|a| a.for_rule_tree()),
                    guards,
                );
                if rule_node != *self.rule_tree.root() {
                    visited_rules = Some(rule_node);
                }
            }
        }

        Some(CascadeInputs {
            rules: Some(rules),
            visited_rules,
            flags: matching_context.extra_data.cascade_input_flags,
            included_cascade_flags: RuleCascadeFlags::empty(),
        })
    }

    /// Set a given device, which may change the styles that apply to the
    /// document.
    ///
    /// Returns the sheet origins that were actually affected.
    ///
    /// This means that we may need to rebuild style data even if the
    /// stylesheets haven't changed.
    ///
    /// Also, the device that arrives here may need to take the viewport rules
    /// into account.
    pub fn set_device(&mut self, device: Device, guards: &StylesheetGuards) -> OriginSet {
        self.device = device;
        self.media_features_change_changed_style(guards, &self.device)
    }

    /// Returns whether, given a media feature change, any previously-applicable
    /// style has become non-applicable, or vice-versa for each origin, using
    /// `device`.
    pub fn media_features_change_changed_style(
        &self,
        guards: &StylesheetGuards,
        device: &Device,
    ) -> OriginSet {
        debug!("Stylist::media_features_change_changed_style {:?}", device);

        let mut origins = OriginSet::empty();
        let stylesheets = self.stylesheets.iter();

        for (stylesheet, origin) in stylesheets {
            if origins.contains(origin.into()) {
                continue;
            }

            let guard = guards.for_origin(origin);
            let origin_cascade_data = self.cascade_data.borrow_for_origin(origin);

            let affected_changed = !origin_cascade_data.media_feature_affected_matches(
                stylesheet,
                guard,
                device,
                self.quirks_mode,
            );

            if affected_changed {
                origins |= origin;
            }
        }

        origins
    }

    /// Returns the Quirks Mode of the document.
    pub fn quirks_mode(&self) -> QuirksMode {
        self.quirks_mode
    }

    /// Sets the quirks mode of the document.
    pub fn set_quirks_mode(&mut self, quirks_mode: QuirksMode) {
        if self.quirks_mode == quirks_mode {
            return;
        }
        self.quirks_mode = quirks_mode;
        self.force_stylesheet_origins_dirty(OriginSet::all());
    }

    /// Returns the applicable CSS declarations for the given element.
    pub fn push_applicable_declarations<'a, E>(
        &'a self,
        element: E,
        pseudo_element: Option<&PseudoElement>,
        style_attribute: Option<ArcBorrow<'a, Locked<PropertyDeclarationBlock>>>,
        smil_override: Option<ArcBorrow<'a, Locked<PropertyDeclarationBlock>>>,
        animation_declarations: &'a AnimationDeclarations,
        rule_inclusion: RuleInclusion,
        applicable_declarations: &mut ApplicableDeclarationList<'a>,
        context: &mut MatchingContext<E::Impl>,
    ) where
        E: TElement + 'a,
    {
        let mut cur = element;
        let mut pseudos = SmallVec::<[_; 2]>::new();
        if let Some(pseudo) = pseudo_element {
            pseudos.push(pseudo.clone());
        }
        while let Some(p) = cur.implemented_pseudo_element() {
            pseudos.push(p);
            let Some(parent_pseudo) = cur.pseudo_element_originating_element() else {
                break;
            };
            cur = parent_pseudo;
        }
        RuleCollector::new(
            self,
            element,
            cur,
            &pseudos,
            style_attribute,
            smil_override,
            animation_declarations,
            rule_inclusion,
            applicable_declarations,
            context,
        )
        .collect_all();
    }

    /// Given an id, returns whether there might be any rules for that id in any
    /// of our rule maps.
    #[inline]
    pub fn may_have_rules_for_id<E>(&self, id: &WeakAtom, element: E) -> bool
    where
        E: TElement,
    {
        // If id needs to be compared case-insensitively, the logic below
        // wouldn't work. Just conservatively assume it may have such rules.
        match self.quirks_mode().classes_and_ids_case_sensitivity() {
            CaseSensitivity::AsciiCaseInsensitive => return true,
            CaseSensitivity::CaseSensitive => {},
        }

        self.any_applicable_rule_data(element, |data| data.mapped_ids.contains(id))
    }

    /// Looks up a CascadeData-dependent rule for a given element.
    ///
    /// NOTE(emilio): This is a best-effort thing, the right fix is a bit TBD because it involves
    /// "recording" which tree the name came from, see [1][2].
    ///
    /// [1]: https://github.com/w3c/csswg-drafts/issues/1995
    /// [2]: https://bugzil.la/1458189
    #[inline]
    fn lookup_element_dependent_at_rule<'a, T, F, E>(
        &'a self,
        element: E,
        find_in: F,
    ) -> Option<&'a T>
    where
        E: TElement + 'a,
        F: Fn(&'a CascadeData) -> Option<&'a T>,
    {
        macro_rules! try_find_in {
            ($data:expr) => {
                if let Some(thing) = find_in(&$data) {
                    return Some(thing);
                }
            };
        }

        let mut result = None;
        let doc_rules_apply =
            element.each_applicable_non_document_style_rule_data(|data, _host| {
                if result.is_none() {
                    result = find_in(data);
                }
            });

        if result.is_some() {
            return result;
        }

        if doc_rules_apply {
            try_find_in!(self.cascade_data.author);
        }
        try_find_in!(self.cascade_data.user);
        try_find_in!(self.cascade_data.user_agent.cascade_data);

        None
    }

    /// Returns the registered `@keyframes` animation for the specified name.
    #[inline]
    pub fn lookup_keyframes<'a, E>(
        &'a self,
        name: &Atom,
        element: E,
    ) -> Option<&'a KeyframesAnimation>
    where
        E: TElement + 'a,
    {
        self.lookup_element_dependent_at_rule(element, |data| data.animations.get(name))
    }

    /// Returns the last @view-transition rule
    /// <https://drafts.csswg.org/css-view-transitions-2/#resolve-view-transition-rule>
    #[inline]
    pub fn last_view_transition_rule(&self) -> Option<&Arc<ViewTransitionRule>> {
        // Iterate the effective rules sorted by origin and level
        self.iter_extra_data_origins()
            .flat_map(|(d, _)| d.view_transitions.iter())
            .last()
            .map(|(rule, _)| rule)
    }

    /// Returns the registered `@position-try-rule` animation for the specified name.
    #[inline]
    #[cfg(feature = "gecko")]
    fn lookup_position_try<'a, E>(
        &'a self,
        name: &Atom,
        scope: CascadeLevel,
        element: E,
    ) -> Option<&'a Arc<Locked<PositionTryRule>>>
    where
        E: TElement + 'a,
    {
        let mut shadow_root = scope.get_shadow_root_for_scoped(element);
        // https://drafts.csswg.org/css-shadow/#tree-scoped-name-global
        // "First search only the tree-scoped names associated with the same root as the tree-scoped reference."
        while let Some(r) = shadow_root {
            if let Some(rule) = r
                .style_data()
                .and_then(|data| data.extra_data.position_try_rules.get(name))
            {
                return Some(rule);
            }
            // "If no relevant tree-scoped name is found, and the root is a shadow root, then repeat this search in the root’s host’s node tree (recursively)."
            shadow_root = r.host().containing_shadow();
        }

        for (data, _) in self.iter_extra_data_origins() {
            if let Some(r) = data.position_try_rules.get(name) {
                return Some(r);
            }
        }
        None
    }

    /// Computes the match results of a given element against the set of
    /// revalidation selectors.
    pub fn match_revalidation_selectors<E>(
        &self,
        element: E,
        bloom: Option<&BloomFilter>,
        selector_caches: &mut SelectorCaches,
        needs_selector_flags: NeedsSelectorFlags,
    ) -> RevalidationResult
    where
        E: TElement,
    {
        let mut matching_context = MatchingContext::new_for_revalidation(
            bloom,
            selector_caches,
            self.quirks_mode,
            needs_selector_flags,
        );

        // Note that, by the time we're revalidating, we're guaranteed that the
        // candidate and the entry have the same id, classes, and local name.
        // This means we're guaranteed to get the same rulehash buckets for all
        // the lookups, which means that the bitvecs are comparable. We verify
        // this in the caller by asserting that the bitvecs are same-length.
        let mut result = RevalidationResult::default();
        let mut relevant_attributes = &mut result.relevant_attributes;
        let selectors_matched = &mut result.selectors_matched;

        let matches_document_rules =
            element.each_applicable_non_document_style_rule_data(|data, host| {
                matching_context.with_shadow_host(Some(host), |matching_context| {
                    data.selectors_for_cache_revalidation.lookup(
                        element,
                        self.quirks_mode,
                        Some(&mut relevant_attributes),
                        |selector_and_hashes| {
                            selectors_matched.push(matches_selector(
                                &selector_and_hashes.selector,
                                selector_and_hashes.selector_offset,
                                Some(&selector_and_hashes.hashes),
                                &element,
                                matching_context,
                            ));
                            true
                        },
                    );
                })
            });

        for (data, origin) in self.cascade_data.iter_origins() {
            if origin == Origin::Author && !matches_document_rules {
                continue;
            }

            data.selectors_for_cache_revalidation.lookup(
                element,
                self.quirks_mode,
                Some(&mut relevant_attributes),
                |selector_and_hashes| {
                    selectors_matched.push(matches_selector(
                        &selector_and_hashes.selector,
                        selector_and_hashes.selector_offset,
                        Some(&selector_and_hashes.hashes),
                        &element,
                        &mut matching_context,
                    ));
                    true
                },
            );
        }

        result
    }

    /// Computes currently active scopes for the given element for revalidation purposes.
    pub fn revalidate_scopes<E: TElement>(
        &self,
        element: &E,
        selector_caches: &mut SelectorCaches,
        needs_selector_flags: NeedsSelectorFlags,
    ) -> ScopeRevalidationResult {
        let mut matching_context = MatchingContext::new(
            MatchingMode::Normal,
            None,
            selector_caches,
            self.quirks_mode,
            needs_selector_flags,
            MatchingForInvalidation::No,
        );

        let mut result = ScopeRevalidationResult::default();
        let matches_document_rules =
            element.each_applicable_non_document_style_rule_data(|data, host| {
                matching_context.with_shadow_host(Some(host), |matching_context| {
                    data.revalidate_scopes(element, matching_context, &mut result);
                })
            });

        for (data, origin) in self.cascade_data.iter_origins() {
            if origin == Origin::Author && !matches_document_rules {
                continue;
            }

            data.revalidate_scopes(element, &mut matching_context, &mut result);
        }

        result
    }

    /// Computes styles for a given declaration with parent_style.
    ///
    /// FIXME(emilio): the lack of pseudo / cascade flags look quite dubious,
    /// hopefully this is only used for some canvas font stuff.
    ///
    /// TODO(emilio): The type parameter can go away when
    /// https://github.com/rust-lang/rust/issues/35121 is fixed.
    pub fn compute_for_declarations<E>(
        &self,
        guards: &StylesheetGuards,
        parent_style: &ComputedValues,
        declarations: Arc<Locked<PropertyDeclarationBlock>>,
    ) -> Arc<ComputedValues>
    where
        E: TElement,
    {
        let block = declarations.read_with(guards.author);

        // We don't bother inserting these declarations in the rule tree, since
        // it'd be quite useless and slow.
        //
        // TODO(emilio): Now that we fixed bug 1493420, we should consider
        // reversing this as it shouldn't be slow anymore, and should avoid
        // generating two instantiations of apply_declarations.
        properties::apply_declarations::<E, _>(
            self,
            /* pseudo = */ None,
            self.rule_tree.root(),
            guards,
            block.declaration_importance_iter().map(|(declaration, _)| {
                (
                    declaration,
                    CascadePriority::new(
                        CascadeLevel::same_tree_author_normal(),
                        LayerOrder::root(),
                        RuleCascadeFlags::empty(),
                    ),
                )
            }),
            Some(parent_style),
            Some(parent_style),
            FirstLineReparenting::No,
            &PositionTryFallbacksTryTactic::default(),
            CascadeMode::Unvisited {
                visited_rules: None,
            },
            Default::default(),
            RuleCascadeFlags::empty(),
            /* rule_cache = */ None,
            &mut Default::default(),
            /* element = */ None,
            &mut TreeCountingCaches::default(),
        )
    }

    /// Accessor for a shared reference to the device.
    #[inline]
    pub fn device(&self) -> &Device {
        &self.device
    }

    /// Accessor for a mutable reference to the device.
    #[inline]
    pub fn device_mut(&mut self) -> &mut Device {
        &mut self.device
    }

    /// Accessor for a shared reference to the rule tree.
    #[inline]
    pub fn rule_tree(&self) -> &RuleTree {
        &self.rule_tree
    }

    /// Returns the script-registered custom property registry.
    #[inline]
    pub fn custom_property_script_registry(&self) -> &CustomPropertyScriptRegistry {
        &self.script_custom_properties
    }

    /// Returns the script-registered custom property registry, as a mutable ref.
    #[inline]
    pub fn custom_property_script_registry_mut(&mut self) -> &mut CustomPropertyScriptRegistry {
        &mut self.script_custom_properties
    }

    /// Measures heap usage.
    #[cfg(feature = "gecko")]
    pub fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        self.cascade_data.add_size_of(ops, sizes);
        self.author_data_cache.add_size_of(ops, sizes);
        sizes.mRuleTree += self.rule_tree.size_of(ops);

        // We may measure other fields in the future if DMD says it's worth it.
    }

    /// Shutdown the static data that this module stores.
    pub fn shutdown() {
        let _entries = UA_CASCADE_DATA_CACHE.lock().unwrap().take_all();
    }
}

#[allow(missing_docs)]
#[repr(u8)]
pub enum RegisterCustomPropertyResult {
    SuccessfullyRegistered,
    InvalidName,
    AlreadyRegistered,
    InvalidSyntax,
    NoInitialValue,
    InvalidInitialValue,
    InitialValueNotComputationallyIndependent,
}

impl Stylist {
    /// <https://drafts.css-houdini.org/css-properties-values-api-1/#the-registerproperty-function>
    pub fn register_custom_property(
        &mut self,
        url_data: &UrlExtraData,
        name: &str,
        syntax: &str,
        inherits: bool,
        initial_value: Option<&str>,
    ) -> RegisterCustomPropertyResult {
        use RegisterCustomPropertyResult::*;

        // If name is not a custom property name string, throw a SyntaxError and exit this algorithm.
        let Ok(name) = parse_name(name).map(Atom::from) else {
            return InvalidName;
        };

        // If property set already contains an entry with name as its property name (compared
        // codepoint-wise), throw an InvalidModificationError and exit this algorithm.
        if self.custom_property_script_registry().get(&name).is_some() {
            return AlreadyRegistered;
        }
        // Attempt to consume a syntax definition from syntax. If it returns failure, throw a
        // SyntaxError. Otherwise, let syntax definition be the returned syntax definition.
        let Ok(syntax) = Descriptor::from_str(syntax, /* preserve_specified = */ false) else {
            return InvalidSyntax;
        };

        let initial_value = match initial_value {
            Some(value) => {
                let parsed = Parser::new(value)
                    .parse_entirely(|input| {
                        input.skip_whitespace();
                        SpecifiedValue::parse(input, None, url_data).map(Arc::new)
                    })
                    .ok();
                if parsed.is_none() {
                    return InvalidInitialValue;
                }
                parsed
            },
            None => None,
        };

        if let Err(error) =
            PropertyRegistration::validate_initial_value(&syntax, initial_value.as_deref())
        {
            return match error {
                PropertyRegistrationError::InitialValueNotComputationallyIndependent => {
                    InitialValueNotComputationallyIndependent
                },
                PropertyRegistrationError::InvalidInitialValue => InvalidInitialValue,
                PropertyRegistrationError::NoInitialValue => NoInitialValue,
            };
        }

        let property_registration = PropertyRegistration {
            name: PropertyRuleName(name),
            descriptors: PropertyDescriptors {
                syntax: Some(syntax),
                inherits: Some(if inherits {
                    Inherits::True
                } else {
                    Inherits::False
                }),
                initial_value,
            },
            url_data: url_data.clone(),
            source_location: SourceLocation { line: 0, column: 0 },
        };
        self.custom_property_script_registry_mut()
            .register(property_registration);
        self.rebuild_initial_values_for_custom_properties();

        SuccessfullyRegistered
    }
}

/// A vector that is sorted in layer order.
#[derive(Clone, Debug, Deref, MallocSizeOf)]
pub struct LayerOrderedVec<T>(Vec<(T, LayerId)>);
impl<T> Default for LayerOrderedVec<T> {
    fn default() -> Self {
        Self(Default::default())
    }
}

/// A map that is sorted in layer order.
#[derive(Clone, Debug, Deref, MallocSizeOf)]
pub struct LayerOrderedMap<T>(PrecomputedHashMap<Atom, SmallVec<[(T, LayerId); 1]>>);
impl<T> Default for LayerOrderedMap<T> {
    fn default() -> Self {
        Self(Default::default())
    }
}

impl<T: 'static> LayerOrderedVec<T> {
    fn clear(&mut self) {
        self.0.clear();
    }
    fn push(&mut self, v: T, id: LayerId) {
        self.0.push((v, id));
    }
    fn sort(&mut self, layers: &[CascadeLayer]) {
        self.0.sort_by_key(|(_, id)| layers[id.0 as usize].order)
    }
}

impl<T: 'static> LayerOrderedMap<T> {
    fn shrink_if_needed(&mut self) {
        self.0.shrink_if_needed();
    }
    fn clear(&mut self) {
        self.0.clear();
    }
    fn try_insert(&mut self, name: Atom, v: T, id: LayerId) -> Result<(), AllocErr> {
        self.try_insert_with(name, v, id, |_, _| Ordering::Equal)
    }
    fn try_insert_with(
        &mut self,
        name: Atom,
        v: T,
        id: LayerId,
        cmp: impl Fn(&T, &T) -> Ordering,
    ) -> Result<(), AllocErr> {
        self.0.try_reserve(1)?;
        let vec = self.0.entry(name).or_default();
        if let Some(&mut (ref mut val, ref last_id)) = vec.last_mut() {
            if *last_id == id {
                if cmp(val, &v) != Ordering::Greater {
                    *val = v;
                }
                return Ok(());
            }
        }
        vec.push((v, id));
        Ok(())
    }
    fn sort(&mut self, layers: &[CascadeLayer]) {
        self.sort_with(layers, |_, _| Ordering::Equal)
    }
    fn sort_with(&mut self, layers: &[CascadeLayer], cmp: impl Fn(&T, &T) -> Ordering) {
        for (_, v) in self.0.iter_mut() {
            v.sort_by(|(v1, id1), (v2, id2)| {
                let order1 = layers[id1.0 as usize].order;
                let order2 = layers[id2.0 as usize].order;
                order1.cmp(&order2).then_with(|| cmp(v1, v2))
            })
        }
    }
    /// Get an entry on the LayerOrderedMap by name.
    pub fn get(&self, name: &Atom) -> Option<&T> {
        let vec = self.0.get(name)?;
        Some(&vec.last()?.0)
    }
}

/// Wrapper to allow better tracking of memory usage by page rule lists.
///
/// This includes the layer ID for use with the named page table.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct PageRuleData {
    /// Layer ID for sorting page rules after matching.
    pub layer: LayerId,
    /// Page rule
    #[ignore_malloc_size_of = "Arc, stylesheet measures as primary ref"]
    pub rule: Arc<Locked<PageRule>>,
}

/// Stores page rules indexed by page names.
#[derive(Clone, Debug, Default, MallocSizeOf)]
pub struct PageRuleMap {
    /// Page rules, indexed by page name. An empty atom indicates no page name.
    pub rules: PrecomputedHashMap<Atom, SmallVec<[PageRuleData; 1]>>,
}

impl PageRuleMap {
    #[inline]
    fn clear(&mut self) {
        self.rules.clear();
    }

    /// Uses page-name and pseudo-classes to match all applicable
    /// page-rules and append them to the matched_rules vec.
    /// This will ensure correct rule order for cascading.
    pub fn match_and_append_rules<'a>(
        &'a self,
        matched_rules: &mut Vec<ApplicableDeclarationBlock<'a>>,
        origin: Origin,
        guards: &'a StylesheetGuards,
        cascade_data: &'a DocumentCascadeData,
        name: &Option<Atom>,
        pseudos: PagePseudoClassFlags,
    ) {
        let level = match origin {
            Origin::UserAgent => CascadeLevel::new(CascadeOrigin::UA),
            Origin::User => CascadeLevel::new(CascadeOrigin::User),
            Origin::Author => CascadeLevel::same_tree_author_normal(),
        };
        let cascade_data = cascade_data.borrow_for_origin(origin);
        let start = matched_rules.len();

        self.match_and_add_rules(
            matched_rules,
            level,
            guards,
            cascade_data,
            &atom!(""),
            pseudos,
        );
        if let Some(name) = name {
            self.match_and_add_rules(matched_rules, level, guards, cascade_data, name, pseudos);
        }

        // Because page-rules do not have source location information stored,
        // use stable sort to ensure source locations are preserved.
        matched_rules[start..].sort_by_key(|block| block.sort_key());
    }

    fn match_and_add_rules<'a>(
        &'a self,
        extra_declarations: &mut Vec<ApplicableDeclarationBlock<'a>>,
        level: CascadeLevel,
        guards: &'a StylesheetGuards,
        cascade_data: &'a CascadeData,
        name: &Atom,
        pseudos: PagePseudoClassFlags,
    ) {
        let rules = match self.rules.get(name) {
            Some(rules) => rules,
            None => return,
        };
        for data in rules.iter() {
            let rule = data.rule.read_with(level.guard(guards));
            let specificity = match rule.match_specificity(pseudos) {
                Some(specificity) => specificity,
                None => continue,
            };
            extra_declarations.push(ApplicableDeclarationBlock::new(
                StyleSourceBorrow::from_declarations(rule.block.borrow_arc()),
                0,
                level,
                specificity,
                cascade_data.layer_order_for(data.layer),
                ScopeProximity::infinity(), // Page rule can't have nested rules anyway.
                RuleCascadeFlags::empty(),
            ));
        }
    }
}

impl MallocShallowSizeOf for PageRuleMap {
    fn shallow_size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        self.rules.shallow_size_of(ops)
    }
}

type PositionTryMap = LayerOrderedMap<Arc<Locked<PositionTryRule>>>;

/// This struct holds data which users of Stylist may want to extract from stylesheets which can be
/// done at the same time as updating.
#[derive(Clone, Debug, Default)]
pub struct ExtraStyleData {
    /// A list of effective font-face rules and their origin.
    pub font_faces: LayerOrderedVec<Arc<Locked<FontFaceRule>>>,

    /// A list of effective font-feature-values rules.
    pub font_feature_values: LayerOrderedVec<Arc<FontFeatureValuesRule>>,

    /// A list of effective font-palette-values rules.
    pub font_palette_values: LayerOrderedVec<Arc<FontPaletteValuesRule>>,

    /// A map of effective counter-style rules.
    pub counter_styles: LayerOrderedMap<Arc<Locked<CounterStyleRule>>>,

    /// A map of effective @position-try rules.
    pub position_try_rules: PositionTryMap,

    /// A map of effective page rules.
    pub pages: PageRuleMap,

    /// A list of effective @view-transition rules.
    pub view_transitions: LayerOrderedVec<Arc<ViewTransitionRule>>,
}

impl ExtraStyleData {
    /// Add the given @font-face rule.
    fn add_font_face(&mut self, rule: &Arc<Locked<FontFaceRule>>, layer: LayerId) {
        self.font_faces.push(rule.clone(), layer);
    }

    /// Add the given @font-feature-values rule.
    fn add_font_feature_values(&mut self, rule: &Arc<FontFeatureValuesRule>, layer: LayerId) {
        self.font_feature_values.push(rule.clone(), layer);
    }

    /// Add the given @font-palette-values rule.
    fn add_font_palette_values(&mut self, rule: &Arc<FontPaletteValuesRule>, layer: LayerId) {
        self.font_palette_values.push(rule.clone(), layer);
    }

    /// Add the given @counter-style rule.
    fn add_counter_style(
        &mut self,
        guard: &SharedRwLockReadGuard,
        rule: &Arc<Locked<CounterStyleRule>>,
        layer: LayerId,
    ) -> Result<(), AllocErr> {
        let name = rule.read_with(guard).name().0.clone();
        self.counter_styles.try_insert(name, rule.clone(), layer)
    }

    /// Add the given @position-try rule.
    fn add_position_try(
        &mut self,
        name: Atom,
        rule: Arc<Locked<PositionTryRule>>,
        layer: LayerId,
    ) -> Result<(), AllocErr> {
        self.position_try_rules.try_insert(name, rule, layer)
    }

    /// Add the given @page rule.
    fn add_page(
        &mut self,
        guard: &SharedRwLockReadGuard,
        rule: &Arc<Locked<PageRule>>,
        layer: LayerId,
    ) -> Result<(), AllocErr> {
        let page_rule = rule.read_with(guard);
        let mut add_rule = |name| {
            let vec = self.pages.rules.entry(name).or_default();
            vec.push(PageRuleData {
                layer,
                rule: rule.clone(),
            });
        };
        if page_rule.selectors.0.is_empty() {
            add_rule(atom!(""));
        } else {
            for selector in page_rule.selectors.as_slice() {
                add_rule(selector.name.0.clone());
            }
        }
        Ok(())
    }

    fn add_view_transition(&mut self, rule: &Arc<ViewTransitionRule>, layer: LayerId) {
        self.view_transitions.push(rule.clone(), layer)
    }

    fn sort_by_layer(&mut self, layers: &[CascadeLayer]) {
        self.font_faces.sort(layers);
        self.font_feature_values.sort(layers);
        self.font_palette_values.sort(layers);
        self.counter_styles.sort(layers);
        self.position_try_rules.sort(layers);
        self.view_transitions.sort(layers);
    }

    fn clear(&mut self) {
        self.font_faces.clear();
        self.font_feature_values.clear();
        self.font_palette_values.clear();
        self.counter_styles.clear();
        self.position_try_rules.clear();
        self.pages.clear();
    }
}

// Don't let a prefixed keyframes animation override
// a non-prefixed one.
fn compare_keyframes_in_same_layer(v1: &KeyframesAnimation, v2: &KeyframesAnimation) -> Ordering {
    if v1.vendor_prefix.is_some() == v2.vendor_prefix.is_some() {
        Ordering::Equal
    } else if v2.vendor_prefix.is_some() {
        Ordering::Greater
    } else {
        Ordering::Less
    }
}

/// An iterator over the different ExtraStyleData.
pub struct ExtraStyleDataIterator<'a>(DocumentCascadeDataIter<'a>);

impl<'a> Iterator for ExtraStyleDataIterator<'a> {
    type Item = (&'a ExtraStyleData, Origin);

    fn next(&mut self) -> Option<Self::Item> {
        self.0.next().map(|d| (&d.0.extra_data, d.1))
    }
}

impl MallocSizeOf for ExtraStyleData {
    /// Measure heap usage.
    fn size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        let mut n = 0;
        n += self.font_faces.shallow_size_of(ops);
        n += self.font_feature_values.shallow_size_of(ops);
        n += self.font_palette_values.shallow_size_of(ops);
        n += self.counter_styles.shallow_size_of(ops);
        n += self.position_try_rules.shallow_size_of(ops);
        n += self.pages.shallow_size_of(ops);
        n
    }
}

/// SelectorMapEntry implementation for use in our revalidation selector map.
#[cfg_attr(feature = "gecko", derive(MallocSizeOf))]
#[derive(Clone, Debug)]
struct RevalidationSelectorAndHashes {
    #[cfg_attr(
        feature = "gecko",
        ignore_malloc_size_of = "CssRules have primary refs, we measure there"
    )]
    selector: Selector<SelectorImpl>,
    selector_offset: usize,
    hashes: AncestorHashes,
}

impl RevalidationSelectorAndHashes {
    fn new(selector: Selector<SelectorImpl>, hashes: AncestorHashes) -> Self {
        let selector_offset = {
            // We basically want to check whether the first combinator is a
            // pseudo-element combinator.  If it is, we want to use the offset
            // one past it.  Otherwise, our offset is 0.
            let mut index = 0;
            let mut iter = selector.iter();

            // First skip over the first ComplexSelector.
            //
            // We can't check what sort of what combinator we have until we do
            // that.
            for _ in &mut iter {
                index += 1; // Simple selector
            }

            match iter.next_sequence() {
                Some(Combinator::PseudoElement) => index + 1, // +1 for the combinator
                _ => 0,
            }
        };

        RevalidationSelectorAndHashes {
            selector,
            selector_offset,
            hashes,
        }
    }
}

impl SelectorMapEntry for RevalidationSelectorAndHashes {
    fn selector(&self) -> SelectorIter<'_, SelectorImpl> {
        self.selector.iter_from(self.selector_offset)
    }
}

/// A selector visitor implementation that collects all the state the Stylist
/// cares about a selector.
struct StylistSelectorVisitor<'a> {
    /// Whether we've past the rightmost compound selector, not counting
    /// pseudo-elements.
    passed_rightmost_selector: bool,

    /// Whether the selector needs revalidation for the style sharing cache.
    needs_revalidation: &'a mut bool,

    /// Whether any selector can make the match result of an element that isn't
    /// itself a link depend on the visitedness of a link.
    non_link_visited_dependency: &'a mut bool,

    /// Flags for which selector list-containing components the visitor is
    /// inside of, if any
    in_selector_list_of: SelectorListKind,

    /// The filter with all the id's getting referenced from selectors.
    mapped_ids: &'a mut PrecomputedHashSet<Atom>,

    /// The filter with the IDs getting referenced from the selector list of
    /// :nth-child(... of <selector list>) selectors.
    nth_of_mapped_ids: &'a mut PrecomputedHashSet<Atom>,

    /// The filter with the local names of attributes there are selectors for.
    attribute_dependencies: &'a mut PrecomputedHashSet<LocalName>,

    /// The filter with the classes getting referenced from the selector list of
    /// :nth-child(... of <selector list>) selectors.
    nth_of_class_dependencies: &'a mut PrecomputedHashSet<Atom>,

    /// The filter with the local names of attributes there are selectors for
    /// within the selector list of :nth-child(... of <selector list>)
    /// selectors.
    nth_of_attribute_dependencies: &'a mut PrecomputedHashSet<LocalName>,

    /// The filter with the local names of custom states in selectors for
    /// within the selector list of :nth-child(... of <selector list>)
    /// selectors.
    nth_of_custom_state_dependencies: &'a mut PrecomputedHashSet<AtomIdent>,

    /// All the states selectors in the page reference.
    state_dependencies: &'a mut ElementState,

    /// All the state selectors in the page reference within the selector list
    /// of :nth-child(... of <selector list>) selectors.
    nth_of_state_dependencies: &'a mut ElementState,

    /// All the document states selectors in the page reference.
    document_state_dependencies: &'a mut DocumentState,
}

fn component_needs_revalidation(c: &Component<SelectorImpl>) -> bool {
    match *c {
        Component::AttributeInNoNamespaceExists { .. }
        | Component::AttributeInNoNamespace { .. }
        | Component::AttributeOther(_)
        | Component::Empty
        | Component::Nth(_)
        | Component::NthOf(_)
        | Component::Has(_) => true,
        Component::NonTSPseudoClass(ref p) => p.needs_cache_revalidation(),
        _ => false,
    }
}

impl<'a> StylistSelectorVisitor<'a> {
    fn visit_nested_selector(
        &mut self,
        in_selector_list_of: SelectorListKind,
        selector: &Selector<SelectorImpl>,
    ) {
        let old_passed_rightmost_selector = self.passed_rightmost_selector;
        let old_in_selector_list_of = self.in_selector_list_of;

        // NOTE: Not resetting passed_rightmost_selector, intentionally.
        self.in_selector_list_of = in_selector_list_of;
        let _ret = selector.visit(self);
        debug_assert!(_ret, "We never return false");

        self.passed_rightmost_selector = old_passed_rightmost_selector;
        self.in_selector_list_of = old_in_selector_list_of;
    }
}

impl<'a> SelectorVisitor for StylistSelectorVisitor<'a> {
    type Impl = SelectorImpl;

    fn visit_complex_selector(&mut self, combinator: Option<Combinator>) -> bool {
        *self.needs_revalidation =
            *self.needs_revalidation || combinator.is_some_and(|c| c.is_sibling());

        // NOTE(emilio): this call happens before we visit any of the simple
        // selectors in the next ComplexSelector, so we can use this to skip
        // looking at them.
        self.passed_rightmost_selector = self.passed_rightmost_selector
            || !matches!(combinator, None | Some(Combinator::PseudoElement));

        true
    }

    fn visit_selector_list(
        &mut self,
        list_kind: SelectorListKind,
        list: &[Selector<Self::Impl>],
    ) -> bool {
        let in_selector_list_of = self.in_selector_list_of | list_kind;
        for selector in list {
            self.visit_nested_selector(in_selector_list_of, selector);
        }
        true
    }

    fn visit_relative_selector_list(
        &mut self,
        list: &[selectors::parser::RelativeSelector<Self::Impl>],
    ) -> bool {
        let in_selector_list_of = self.in_selector_list_of | SelectorListKind::HAS;
        for selector in list {
            self.visit_nested_selector(in_selector_list_of, &selector.selector);
        }
        true
    }

    fn visit_attribute_selector(
        &mut self,
        _ns: &NamespaceConstraint<&Namespace>,
        name: &LocalName,
        lower_name: &LocalName,
    ) -> bool {
        if self.in_selector_list_of.relevant_to_nth_of_dependencies() {
            self.nth_of_attribute_dependencies.insert(name.clone());
            if name != lower_name {
                self.nth_of_attribute_dependencies
                    .insert(lower_name.clone());
            }
        }

        self.attribute_dependencies.insert(name.clone());
        if name != lower_name {
            self.attribute_dependencies.insert(lower_name.clone());
        }

        true
    }

    fn visit_simple_selector(&mut self, s: &Component<SelectorImpl>) -> bool {
        *self.needs_revalidation = *self.needs_revalidation || component_needs_revalidation(s);

        match *s {
            Component::NonTSPseudoClass(NonTSPseudoClass::CustomState(ref name)) => {
                // CustomStateSet is special cased as it is a functional pseudo
                // class with unbounded inner values. This is different to
                // other psuedo class like :emtpy or :dir() which can be packed
                // into the ElementState bitflags. For CustomState, however,
                // the state name should be checked for presence in the selector.
                if self.in_selector_list_of.relevant_to_nth_of_dependencies() {
                    self.nth_of_custom_state_dependencies.insert(name.0.clone());
                }
            },
            Component::NonTSPseudoClass(ref p) => {
                self.state_dependencies.insert(p.state_flag());
                self.document_state_dependencies
                    .insert(p.document_state_flag());

                if self.in_selector_list_of.relevant_to_nth_of_dependencies() {
                    self.nth_of_state_dependencies.insert(p.state_flag());
                }

                if self.passed_rightmost_selector
                    && matches!(*p, NonTSPseudoClass::Link | NonTSPseudoClass::Visited)
                {
                    *self.non_link_visited_dependency = true;
                }
            },
            Component::ID(ref id) => {
                self.mapped_ids.insert(id.0.clone());
                if self.in_selector_list_of.relevant_to_nth_of_dependencies() {
                    self.nth_of_mapped_ids.insert(id.0.clone());
                }
            },
            Component::Class(ref class)
                if self.in_selector_list_of.relevant_to_nth_of_dependencies() =>
            {
                self.nth_of_class_dependencies.insert(class.0.clone());
            },
            _ => {},
        }

        true
    }
}

/// A set of rules for element and pseudo-elements.
#[derive(Clone, Debug, Default, MallocSizeOf)]
struct GenericElementAndPseudoRules<Map> {
    /// Rules from stylesheets at this `CascadeData`'s origin.
    element_map: Map,

    /// Rules from stylesheets at this `CascadeData`'s origin that correspond
    /// to a given pseudo-element.
    ///
    /// FIXME(emilio): There are a bunch of wasted entries here in practice.
    /// Figure out a good way to do a `PerNonAnonBox` and `PerAnonBox` (for
    /// `precomputed_values_for_pseudo`) without duplicating a lot of code.
    pseudos_map: PerPseudoElementMap<Self>,
}

impl<Map: Default + MallocSizeOf> GenericElementAndPseudoRules<Map> {
    #[inline(always)]
    fn for_insertion(&mut self, pseudo_elements: &[&PseudoElement]) -> &mut Map {
        let mut current = self;
        for &pseudo_element in pseudo_elements {
            debug_assert!(
                !pseudo_element.is_precomputed()
                    && !pseudo_element.is_unknown_webkit_pseudo_element(),
                "Precomputed pseudos should end up in precomputed_pseudo_element_decls, \
                 and unknown webkit pseudos should be discarded before getting here"
            );

            current = current
                .pseudos_map
                .get_or_insert_with(pseudo_element, Default::default);
        }

        &mut current.element_map
    }

    #[inline]
    fn rules(&self, pseudo_elements: &[PseudoElement]) -> Option<&Map> {
        let mut current = self;
        for pseudo in pseudo_elements {
            current = current.pseudos_map.get(pseudo)?;
        }
        Some(&current.element_map)
    }

    /// Measures heap usage.
    #[cfg(feature = "gecko")]
    fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        sizes.mElementAndPseudosMaps += self.element_map.size_of(ops);

        for elem in self.pseudos_map.iter() {
            sizes.mElementAndPseudosMaps += MallocSizeOf::size_of(elem, ops);
        }
    }
}

type ElementAndPseudoRules = GenericElementAndPseudoRules<SelectorMap<Rule>>;
type PartMap = PrecomputedHashMap<Atom, SmallVec<[Rule; 1]>>;
type PartElementAndPseudoRules = GenericElementAndPseudoRules<PartMap>;

impl ElementAndPseudoRules {
    // TODO(emilio): Should we retain storage of these?
    fn clear(&mut self) {
        self.element_map.clear();
        self.pseudos_map.clear();
    }

    fn shrink_if_needed(&mut self) {
        self.element_map.shrink_if_needed();
        for pseudo in self.pseudos_map.iter_mut() {
            pseudo.shrink_if_needed();
        }
    }
}

impl PartElementAndPseudoRules {
    // TODO(emilio): Should we retain storage of these?
    fn clear(&mut self) {
        self.element_map.clear();
        self.pseudos_map.clear();
    }
}

/// The id of a given layer, a sequentially-increasing identifier.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord)]
pub struct LayerId(u16);

impl LayerId {
    /// The id of the root layer.
    pub const fn root() -> Self {
        Self(0)
    }
}

#[derive(Clone, Debug, MallocSizeOf)]
struct CascadeLayer {
    id: LayerId,
    order: LayerOrder,
    children: Vec<LayerId>,
}

impl CascadeLayer {
    const fn root() -> Self {
        Self {
            id: LayerId::root(),
            order: LayerOrder::root(),
            children: vec![],
        }
    }
}

/// The id of a given container condition, a sequentially-increasing identifier
/// for a given style set.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord)]
pub struct ContainerConditionId(u16);

impl ContainerConditionId {
    /// A special id that represents no container rule.
    pub const fn none() -> Self {
        Self(0)
    }
}

#[derive(Clone, Debug, MallocSizeOf)]
struct ContainerConditionReference {
    parent: ContainerConditionId,
    /// Contains the container conditions of a particular rule.
    ///
    /// This should only ever be empty for the root rule, which acts as a
    /// sentinel value.
    #[ignore_malloc_size_of = "Arc"]
    conditions: ArcSlice<ContainerCondition>,
}

impl ContainerConditionReference {
    /// Creates an empty, root container condition reference.
    fn none() -> Self {
        Self {
            parent: ContainerConditionId::none(),
            conditions: ArcSlice::default(),
        }
    }
}

/// The id of a given scope condition, a sequentially-increasing identifier
/// for a given style set.
#[derive(Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, PartialOrd, Ord)]
pub struct ScopeConditionId(u16);

impl ScopeConditionId {
    /// Construct a new scope condition id.
    pub fn new(id: u16) -> Self {
        Self(id)
    }

    /// A special id that represents no scope rule.
    pub const fn none() -> Self {
        Self(0)
    }
}

/// Data required to process this scope condition.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct ScopeConditionReference {
    /// The ID of outer scope condition, `none()` otherwise.
    parent: ScopeConditionId,
    /// Start and end bounds of the scope. None implies sentinel data (i.e. Not a scope condition).
    condition: Option<ScopeBoundsWithHashes>,
    /// Implicit scope root of this scope condition, computed unconditionally,
    /// even if the start bound may be Some.
    #[ignore_malloc_size_of = "Raw ptr behind the scenes"]
    implicit_scope_root: StylistImplicitScopeRoot,
    /// Is the condition trivial? See `ScopeBoundsWithHashes::is_trivial`.
    is_trivial: bool,
}

impl ScopeConditionReference {
    /// Create a new scope condition.
    pub fn new(
        parent: ScopeConditionId,
        condition: Option<ScopeBoundsWithHashes>,
        implicit_scope_root: ImplicitScopeRoot,
        is_trivial: bool,
    ) -> Self {
        Self {
            parent,
            condition,
            implicit_scope_root: StylistImplicitScopeRoot::Normal(implicit_scope_root),
            is_trivial,
        }
    }

    /// Create a sentinel scope condition.
    pub const fn none() -> Self {
        Self {
            parent: ScopeConditionId::none(),
            condition: None,
            implicit_scope_root: StylistImplicitScopeRoot::default_const(),
            is_trivial: true,
        }
    }
}

/// All potential sscope root candidates.
pub struct ScopeRootCandidates {
    /// List of scope root candidates.
    pub candidates: Vec<ScopeRootCandidate>,
    /// Is the scope condition matching these candidates trivial? See `ScopeBoundsWithHashes::is_trivial`.
    pub is_trivial: bool,
}

impl Default for ScopeRootCandidates {
    fn default() -> Self {
        Self {
            candidates: vec![],
            is_trivial: true,
        }
    }
}

impl ScopeRootCandidates {
    fn empty(is_trivial: bool) -> Self {
        Self {
            candidates: vec![],
            is_trivial,
        }
    }
}

/// Start and end bound of a scope, along with their selector hashes.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct ScopeBoundWithHashes {
    // TODO(dshin): With replaced parent selectors, these may be unique...
    #[ignore_malloc_size_of = "Arc"]
    selectors: SelectorList<SelectorImpl>,
    hashes: SmallVec<[AncestorHashes; 1]>,
}

impl ScopeBoundWithHashes {
    fn new(quirks_mode: QuirksMode, selectors: SelectorList<SelectorImpl>) -> Self {
        let mut hashes = SmallVec::with_capacity(selectors.len());
        for selector in selectors.slice() {
            hashes.push(AncestorHashes::new(selector, quirks_mode));
        }
        Self { selectors, hashes }
    }

    fn new_no_hash(selectors: SelectorList<SelectorImpl>) -> Self {
        let hashes = selectors
            .slice()
            .iter()
            .map(|_| AncestorHashes {
                packed_hashes: [0, 0, 0],
            })
            .collect();
        Self { selectors, hashes }
    }
}

/// Bounds for this scope, along with corresponding selector hashes.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct ScopeBoundsWithHashes {
    /// Start of the scope bound. If None, implies implicit scope root.
    start: Option<ScopeBoundWithHashes>,
    /// Optional end of the scope bound.
    end: Option<ScopeBoundWithHashes>,
}

impl ScopeBoundsWithHashes {
    /// Create a new scope bound, hashing selectors for fast rejection.
    fn new(
        quirks_mode: QuirksMode,
        start: Option<SelectorList<SelectorImpl>>,
        end: Option<SelectorList<SelectorImpl>>,
    ) -> Self {
        Self {
            start: start.map(|selectors| ScopeBoundWithHashes::new(quirks_mode, selectors)),
            end: end.map(|selectors| ScopeBoundWithHashes::new(quirks_mode, selectors)),
        }
    }

    /// Create a new scope bound, but not hashing any selector.
    pub fn new_no_hash(
        start: Option<SelectorList<SelectorImpl>>,
        end: Option<SelectorList<SelectorImpl>>,
    ) -> Self {
        Self {
            start: start.map(ScopeBoundWithHashes::new_no_hash),
            end: end.map(ScopeBoundWithHashes::new_no_hash),
        }
    }

    fn selectors_for(
        bound_with_hashes: Option<&ScopeBoundWithHashes>,
    ) -> impl Iterator<Item = &Selector<SelectorImpl>> {
        bound_with_hashes
            .map(|b| b.selectors.slice().iter())
            .into_iter()
            .flatten()
    }

    fn start_selectors(&self) -> impl Iterator<Item = &Selector<SelectorImpl>> {
        Self::selectors_for(self.start.as_ref())
    }

    fn end_selectors(&self) -> impl Iterator<Item = &Selector<SelectorImpl>> {
        Self::selectors_for(self.end.as_ref())
    }

    fn is_trivial(&self) -> bool {
        fn scope_bound_is_trivial(bound: &Option<ScopeBoundWithHashes>, default: bool) -> bool {
            bound.as_ref().map_or(default, |bound| {
                scope_selector_list_is_trivial(&bound.selectors)
            })
        }

        // Given an implicit scope, we are unable to tell if the cousins share the same implicit root.
        scope_bound_is_trivial(&self.start, false) && scope_bound_is_trivial(&self.end, true)
    }
}

/// Find all scope conditions for a given condition ID, indexing into the given list of scope conditions.
pub fn scope_root_candidates<E>(
    scope_conditions: &[ScopeConditionReference],
    id: ScopeConditionId,
    element: &E,
    override_matches_shadow_host_for_part: bool,
    scope_subject_map: &ScopeSubjectMap,
    context: &mut MatchingContext<SelectorImpl>,
) -> ScopeRootCandidates
where
    E: TElement,
{
    let condition_ref = &scope_conditions[id.0 as usize];
    let bounds = match condition_ref.condition {
        None => return ScopeRootCandidates::default(),
        Some(ref c) => c,
    };
    // Make sure the parent scopes ara evaluated first. This runs a bit counter to normal
    // selector matching where rightmost selectors match first. However, this avoids having
    // to traverse through descendants (i.e. Avoids tree traversal vs linear traversal).
    let outer_result = scope_root_candidates(
        scope_conditions,
        condition_ref.parent,
        element,
        override_matches_shadow_host_for_part,
        scope_subject_map,
        context,
    );

    let is_trivial = condition_ref.is_trivial && outer_result.is_trivial;
    let is_outermost_scope = condition_ref.parent == ScopeConditionId::none();
    if !is_outermost_scope && outer_result.candidates.is_empty() {
        return ScopeRootCandidates::empty(is_trivial);
    }

    let (root_target, matches_shadow_host) = if let Some(start) = bounds.start.as_ref() {
        if let Some(filter) = context.bloom_filter {
            // Use the bloom filter here. If our ancestors do not have the right hashes,
            // there's no point in traversing up. Besides, the filter is built for this depth,
            // so the filter contains more data than it should, the further we go up the ancestor
            // chain. It wouldn't generate wrong results, but makes the traversal even more pointless.
            if !start
                .hashes
                .iter()
                .any(|entry| selector_may_match(entry, filter))
            {
                return ScopeRootCandidates::empty(is_trivial);
            }
        }
        (
            ScopeTarget::Selector(&start.selectors),
            scope_start_matches_shadow_host(&start.selectors),
        )
    } else {
        let implicit_root = condition_ref.implicit_scope_root;
        match implicit_root {
            StylistImplicitScopeRoot::Normal(r) => (
                ScopeTarget::Implicit(r.element(context.current_host)),
                r.matches_shadow_host(),
            ),
            StylistImplicitScopeRoot::Cached(index) => {
                let host = context
                    .current_host
                    .expect("Cached implicit scope for light DOM implicit scope");
                match E::implicit_scope_for_sheet_in_shadow_root(host, index) {
                    None => return ScopeRootCandidates::empty(is_trivial),
                    Some(root) => (
                        ScopeTarget::Implicit(root.element(context.current_host)),
                        root.matches_shadow_host(),
                    ),
                }
            },
        }
    };
    // For `::part`, we need to be able to reach the outer tree. Parts without the corresponding
    // `exportparts` attribute will be rejected at the selector matching time.
    let matches_shadow_host = override_matches_shadow_host_for_part || matches_shadow_host;

    let potential_scope_roots = if is_outermost_scope {
        collect_scope_roots(
            *element,
            None,
            context,
            &root_target,
            matches_shadow_host,
            scope_subject_map,
        )
    } else {
        let mut result = vec![];
        for activation in outer_result.candidates {
            let mut this_result = collect_scope_roots(
                *element,
                Some(activation.root),
                context,
                &root_target,
                matches_shadow_host,
                scope_subject_map,
            );
            result.append(&mut this_result);
        }
        result
    };

    if potential_scope_roots.is_empty() {
        return ScopeRootCandidates::empty(is_trivial);
    }

    let candidates = if let Some(end) = bounds.end.as_ref() {
        let mut result = vec![];
        // If any scope-end selector matches, we're not in scope.
        for scope_root in potential_scope_roots {
            if end
                .selectors
                .slice()
                .iter()
                .zip(end.hashes.iter())
                .all(|(selector, hashes)| {
                    // Like checking for scope-start, use the bloom filter here.
                    if let Some(filter) = context.bloom_filter {
                        if !selector_may_match(hashes, filter) {
                            // Selector this hash belongs to won't cause us to be out of this scope.
                            return true;
                        }
                    }

                    !element_is_outside_of_scope(
                        selector,
                        *element,
                        scope_root.root,
                        context,
                        matches_shadow_host,
                    )
                })
            {
                result.push(scope_root);
            }
        }
        result
    } else {
        potential_scope_roots
    };

    ScopeRootCandidates {
        candidates,
        is_trivial,
    }
}

/// Implicit scope root, which may or may not be cached (i.e. For shadow DOM author
/// styles that are cached and shared).
#[derive(Copy, Clone, Debug, MallocSizeOf)]
enum StylistImplicitScopeRoot {
    Normal(ImplicitScopeRoot),
    Cached(usize),
}
// Should be safe, only mutated through mutable methods in `Stylist`.
unsafe impl Sync for StylistImplicitScopeRoot {}

impl StylistImplicitScopeRoot {
    const fn default_const() -> Self {
        // Use the "safest" fallback.
        Self::Normal(ImplicitScopeRoot::DocumentElement)
    }
}

impl Default for StylistImplicitScopeRoot {
    fn default() -> Self {
        Self::default_const()
    }
}

/// Data resulting from performing the CSS cascade that is specific to a given
/// origin.
///
/// FIXME(emilio): Consider renaming and splitting in `CascadeData` and
/// `InvalidationData`? That'd make `clear_cascade_data()` clearer.
#[derive(Debug, Clone, MallocSizeOf)]
pub struct CascadeData {
    /// The data coming from normal style rules that apply to elements at this
    /// cascade level.
    normal_rules: ElementAndPseudoRules,

    /// The `:host` pseudo rules that are the rightmost selector (without
    /// accounting for pseudo-elements), or `:scope` rules that may match
    /// the featureless host.
    featureless_host_rules: Option<Box<ElementAndPseudoRules>>,

    /// The data coming from ::slotted() pseudo-element rules.
    ///
    /// We need to store them separately because an element needs to match
    /// ::slotted() pseudo-element rules in different shadow roots.
    ///
    /// In particular, we need to go through all the style data in all the
    /// containing style scopes starting from the closest assigned slot.
    slotted_rules: Option<Box<ElementAndPseudoRules>>,

    /// The data coming from ::part() pseudo-element rules.
    ///
    /// We need to store them separately because an element needs to match
    /// ::part() pseudo-element rules in different shadow roots.
    part_rules: Option<Box<PartElementAndPseudoRules>>,

    /// The invalidation map for these rules.
    invalidation_map: InvalidationMap,

    /// The relative selector equivalent of the invalidation map.
    relative_selector_invalidation_map: InvalidationMap,

    additional_relative_selector_invalidation_map: AdditionalRelativeSelectorInvalidationMap,

    /// The attribute local names that appear in attribute selectors.  Used
    /// to avoid taking element snapshots when an irrelevant attribute changes.
    /// (We don't bother storing the namespace, since namespaced attributes are
    /// rare.)
    attribute_dependencies: PrecomputedHashSet<LocalName>,

    /// The classes that appear in the selector list of
    /// :nth-child(... of <selector list>). Used to avoid restyling siblings of
    /// an element when an irrelevant class changes.
    nth_of_class_dependencies: PrecomputedHashSet<Atom>,

    /// The attributes that appear in the selector list of
    /// :nth-child(... of <selector list>). Used to avoid restyling siblings of
    /// an element when an irrelevant attribute changes.
    nth_of_attribute_dependencies: PrecomputedHashSet<LocalName>,

    /// The custom states that appear in the selector list of
    /// :nth-child(... of <selector list>). Used to avoid restyling siblings of
    /// an element when an irrelevant custom state changes.
    nth_of_custom_state_dependencies: PrecomputedHashSet<AtomIdent>,

    /// The element state bits that are relied on by selectors.  Like
    /// `attribute_dependencies`, this is used to avoid taking element snapshots
    /// when an irrelevant element state bit changes.
    state_dependencies: ElementState,

    /// Whether some selector tests `:link` / `:visited` from a position that
    /// can change the match result of an element that isn't itself a link.
    non_link_visited_dependency: bool,

    /// The element state bits that are relied on by selectors that appear in
    /// the selector list of :nth-child(... of <selector list>).
    nth_of_state_dependencies: ElementState,

    /// The document state bits that are relied on by selectors.  This is used
    /// to tell whether we need to restyle the entire document when a document
    /// state bit changes.
    document_state_dependencies: DocumentState,

    /// The ids that appear in the rightmost complex selector of selectors (and
    /// hence in our selector maps).  Used to determine when sharing styles is
    /// safe: we disallow style sharing for elements whose id matches this
    /// filter, and hence might be in one of our selector maps.
    mapped_ids: PrecomputedHashSet<Atom>,

    /// The IDs that appear in the selector list of
    /// :nth-child(... of <selector list>). Used to avoid restyling siblings
    /// of an element when an irrelevant ID changes.
    nth_of_mapped_ids: PrecomputedHashSet<Atom>,

    /// Selectors that require explicit cache revalidation (i.e. which depend
    /// on state that is not otherwise visible to the cache, like attributes or
    /// tree-structural state like child index and pseudos).
    #[ignore_malloc_size_of = "Arc"]
    selectors_for_cache_revalidation: SelectorMap<RevalidationSelectorAndHashes>,

    /// A map with all the animations at this `CascadeData`'s origin, indexed
    /// by name.
    animations: LayerOrderedMap<KeyframesAnimation>,

    /// A map with all the layer-ordered registrations from style at this `CascadeData`'s origin,
    /// indexed by name.
    #[ignore_malloc_size_of = "Arc"]
    custom_property_registrations: LayerOrderedMap<Arc<PropertyRegistration>>,

    /// Custom media query registrations.
    custom_media: CustomMediaMap,

    /// A map from cascade layer name to layer order.
    layer_id: FxHashMap<LayerName, LayerId>,

    /// The list of cascade layers, indexed by their layer id.
    layers: SmallVec<[CascadeLayer; 1]>,

    /// The list of container conditions, indexed by their id.
    container_conditions: SmallVec<[ContainerConditionReference; 1]>,

    /// A map of attributes that are referenced by `attr()` functions in container queries.
    /// The attribute exists as a key if it is referenced inside a style container query.
    /// The enum value tells us whether or not the container query condition is named.
    attr_function_dependencies: PrecomputedHashMap<LocalName, ContainerAttributeDependencyKind>,

    /// The list of scope conditions, indexed by their id.
    scope_conditions: SmallVec<[ScopeConditionReference; 1]>,

    /// Map of unique selectors on scope start selectors' subjects.
    scope_subject_map: ScopeSubjectMap,

    /// Effective media query results cached from the last rebuild.
    effective_media_query_results: EffectiveMediaQueryResults,

    /// Extra data, like different kinds of rules, etc.
    extra_data: ExtraStyleData,

    /// A monotonically increasing counter to represent the order on which a
    /// style rule appears in a stylesheet, needed to sort them by source order.
    rules_source_order: u32,

    /// The total number of selectors.
    num_selectors: usize,

    /// The total number of declarations.
    num_declarations: usize,
}

static IMPLICIT_SCOPE: LazyLock<SelectorList<SelectorImpl>> = LazyLock::new(|| {
    // Implicit scope, as per https://github.com/w3c/csswg-drafts/issues/10196
    // Also, `&` is `:where(:scope)`, as per https://github.com/w3c/csswg-drafts/issues/9740
    // ``:where(:scope)` effectively behaves the same as the implicit scope.
    let list = SelectorList::implicit_scope();
    list.mark_as_intentionally_leaked();
    list
});

fn scope_start_matches_shadow_host(start: &SelectorList<SelectorImpl>) -> bool {
    // TODO(emilio): Should we carry a MatchesFeaturelessHost rather than a bool around?
    // Pre-existing behavior with multiple selectors matches this tho.
    start
        .slice()
        .iter()
        .any(|s| s.matches_featureless_host(true).may_match())
}

/// Replace any occurrence of parent selector in the given selector with a implicit scope selector.
pub fn replace_parent_selector_with_implicit_scope(
    selectors: &SelectorList<SelectorImpl>,
) -> SelectorList<SelectorImpl> {
    selectors.replace_parent_selector(&IMPLICIT_SCOPE)
}

impl CascadeData {
    /// Creates an empty `CascadeData`.
    pub fn new() -> Self {
        Self {
            normal_rules: ElementAndPseudoRules::default(),
            featureless_host_rules: None,
            slotted_rules: None,
            part_rules: None,
            invalidation_map: InvalidationMap::new(),
            relative_selector_invalidation_map: InvalidationMap::new(),
            additional_relative_selector_invalidation_map:
                AdditionalRelativeSelectorInvalidationMap::new(),
            nth_of_mapped_ids: PrecomputedHashSet::default(),
            nth_of_class_dependencies: PrecomputedHashSet::default(),
            nth_of_attribute_dependencies: PrecomputedHashSet::default(),
            nth_of_custom_state_dependencies: PrecomputedHashSet::default(),
            nth_of_state_dependencies: ElementState::empty(),
            attribute_dependencies: PrecomputedHashSet::default(),
            state_dependencies: ElementState::empty(),
            non_link_visited_dependency: false,
            document_state_dependencies: DocumentState::empty(),
            mapped_ids: PrecomputedHashSet::default(),
            selectors_for_cache_revalidation: SelectorMap::new(),
            animations: Default::default(),
            custom_property_registrations: Default::default(),
            custom_media: Default::default(),
            layer_id: Default::default(),
            layers: smallvec::smallvec![CascadeLayer::root()],
            container_conditions: smallvec::smallvec![ContainerConditionReference::none()],
            attr_function_dependencies: PrecomputedHashMap::default(),
            scope_conditions: smallvec::smallvec![ScopeConditionReference::none()],
            scope_subject_map: Default::default(),
            extra_data: ExtraStyleData::default(),
            effective_media_query_results: EffectiveMediaQueryResults::new(),
            rules_source_order: 0,
            num_selectors: 0,
            num_declarations: 0,
        }
    }

    /// Rebuild the cascade data from a given SheetCollection, incrementally if possible.
    pub fn rebuild<'a, S>(
        &mut self,
        device: &Device,
        quirks_mode: QuirksMode,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        difference: &mut CascadeDataDifference,
    ) -> Result<(), AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        if !collection.dirty() {
            return Ok(());
        }

        let validity = collection.data_validity();

        let mut old_position_try_data = LayerOrderedMap::default();
        if validity != DataValidity::Valid {
            old_position_try_data = std::mem::take(&mut self.extra_data.position_try_rules);
            self.clear_cascade_data();
            if validity == DataValidity::FullyInvalid {
                self.clear_invalidation_data();
            }
        }

        let mut result = Ok(());

        collection.each(|index, stylesheet, rebuild_kind| {
            result = self.add_stylesheet(
                device,
                quirks_mode,
                stylesheet,
                index,
                guard,
                rebuild_kind,
                /* precomputed_pseudo_element_decls = */ None,
                if validity == DataValidity::Valid {
                    Some(difference)
                } else {
                    None
                },
            );
            result.is_ok()
        });

        self.did_finish_rebuild();

        // For DataValidity::Valid, we pass the difference down to `add_stylesheet` so that we
        // populate it with new data. Otherwise we need to diff with the old data.
        if validity != DataValidity::Valid {
            difference.update(&old_position_try_data, &self.extra_data.position_try_rules);
        }

        result
    }

    /// Returns the custom media query map.
    pub fn custom_media_map(&self) -> &CustomMediaMap {
        &self.custom_media
    }

    /// Returns the invalidation map.
    pub fn invalidation_map(&self) -> &InvalidationMap {
        &self.invalidation_map
    }

    /// Returns the relative selector invalidation map.
    pub fn relative_selector_invalidation_map(&self) -> &InvalidationMap {
        &self.relative_selector_invalidation_map
    }

    /// Returns the relative selector invalidation map data.
    pub fn relative_invalidation_map_attributes(
        &self,
    ) -> &AdditionalRelativeSelectorInvalidationMap {
        &self.additional_relative_selector_invalidation_map
    }

    /// Returns whether the given ElementState bit is relied upon by a selector
    /// of some rule.
    #[inline]
    pub fn has_state_dependency(&self, state: ElementState) -> bool {
        self.state_dependencies.intersects(state)
    }

    /// Returns whether the given Custom State is relied upon by a selector
    /// of some rule in the selector list of :nth-child(... of <selector list>).
    #[inline]
    pub fn has_nth_of_custom_state_dependency(&self, state: &AtomIdent) -> bool {
        self.nth_of_custom_state_dependencies.contains(state)
    }

    /// Returns whether the given ElementState bit is relied upon by a selector
    /// of some rule in the selector list of :nth-child(... of <selector list>).
    #[inline]
    pub fn has_nth_of_state_dependency(&self, state: ElementState) -> bool {
        self.nth_of_state_dependencies.intersects(state)
    }

    /// Returns whether the given attribute might appear in an attribute
    /// selector of some rule.
    #[inline]
    pub fn might_have_attribute_dependency(&self, local_name: &LocalName) -> bool {
        self.attribute_dependencies.contains(local_name)
    }

    /// Whether matching an element that isn't itself a link can depend on
    /// whether a link is visited. Note this doesn't account for pseudo-element
    /// rules, whose originating element is the one `:visited` would match.
    pub fn has_non_link_visited_dependency(&self) -> bool {
        self.non_link_visited_dependency
    }

    /// Returns whether the given attribute might appear in an attribute
    /// function inside a container rule. Unnamed container means that the
    /// value is used in an unamed style container rule. NamedContainer means
    /// that the value is used in a named container rule. None means the value
    /// is not used in a style container prelude at all.
    #[inline]
    pub fn might_have_attribute_dependency_in_container(
        &self,
        local_name: &LocalName,
    ) -> ContainerAttributeDependencyKind {
        self.attr_function_dependencies
            .get(local_name)
            .copied()
            .unwrap_or(ContainerAttributeDependencyKind::None)
    }

    /// Returns whether the given ID might appear in an ID selector in the
    /// selector list of :nth-child(... of <selector list>).
    #[inline]
    pub fn might_have_nth_of_id_dependency(&self, id: &Atom) -> bool {
        self.nth_of_mapped_ids.contains(id)
    }

    /// Returns whether the given class might appear in a class selector in the
    /// selector list of :nth-child(... of <selector list>).
    #[inline]
    pub fn might_have_nth_of_class_dependency(&self, class: &Atom) -> bool {
        self.nth_of_class_dependencies.contains(class)
    }

    /// Returns whether the given attribute might appear in an attribute
    /// selector in the selector list of :nth-child(... of <selector list>).
    #[inline]
    pub fn might_have_nth_of_attribute_dependency(&self, local_name: &LocalName) -> bool {
        self.nth_of_attribute_dependencies.contains(local_name)
    }

    /// Returns the normal rule map for a given pseudo-element.
    #[inline]
    pub fn normal_rules(&self, pseudo_elements: &[PseudoElement]) -> Option<&SelectorMap<Rule>> {
        self.normal_rules.rules(pseudo_elements)
    }

    /// Returns the featureless pseudo rule map for a given pseudo-element.
    #[inline]
    pub fn featureless_host_rules(
        &self,
        pseudo_elements: &[PseudoElement],
    ) -> Option<&SelectorMap<Rule>> {
        self.featureless_host_rules
            .as_ref()
            .and_then(|d| d.rules(pseudo_elements))
    }

    /// Whether there's any featureless rule that could match in this scope.
    pub fn any_featureless_host_rules(&self) -> bool {
        self.featureless_host_rules.is_some()
    }

    /// Returns the slotted rule map for a given pseudo-element.
    #[inline]
    pub fn slotted_rules(&self, pseudo_elements: &[PseudoElement]) -> Option<&SelectorMap<Rule>> {
        self.slotted_rules
            .as_ref()
            .and_then(|d| d.rules(pseudo_elements))
    }

    /// Whether there's any ::slotted rule that could match in this scope.
    pub fn any_slotted_rule(&self) -> bool {
        self.slotted_rules.is_some()
    }

    /// Returns the parts rule map for a given pseudo-element.
    #[inline]
    pub fn part_rules(&self, pseudo_elements: &[PseudoElement]) -> Option<&PartMap> {
        self.part_rules
            .as_ref()
            .and_then(|d| d.rules(pseudo_elements))
    }

    /// Whether there's any ::part rule that could match in this scope.
    pub fn any_part_rule(&self) -> bool {
        self.part_rules.is_some()
    }

    #[inline]
    fn layer_order_for(&self, id: LayerId) -> LayerOrder {
        self.layers[id.0 as usize].order
    }

    pub(crate) fn container_condition_matches<E>(
        &self,
        mut id: ContainerConditionId,
        stylist: &Stylist,
        element: E,
        context: &mut MatchingContext<E::Impl>,
    ) -> bool
    where
        E: TElement,
    {
        loop {
            let condition_ref = &self.container_conditions[id.0 as usize];
            if condition_ref.conditions.is_empty() {
                return true;
            }
            let matches = condition_ref.conditions.iter().any(|condition| {
                condition
                    .matches(
                        stylist,
                        element,
                        context.extra_data.originating_element_style,
                        &mut context.extra_data.cascade_input_flags,
                    )
                    .to_bool(/* unknown = */ false)
            });
            if !matches {
                return false;
            }
            id = condition_ref.parent;
        }
    }

    pub(crate) fn find_scope_proximity_if_matching<E: TElement>(
        &self,
        rule: &Rule,
        element: E,
        context: &mut MatchingContext<E::Impl>,
    ) -> ScopeProximity {
        context
            .extra_data
            .cascade_input_flags
            .insert(ComputedValueFlags::CONSIDERED_NONTRIVIAL_SCOPED_STYLE);

        // Whether the scope root matches a shadow host mostly olny depends on scope-intrinsic
        // parameters (i.e. bounds/implicit scope) - except for the use of `::parts`, where
        // matching crosses the shadow boundary.
        let result = scope_root_candidates(
            &self.scope_conditions,
            rule.scope_condition_id,
            &element,
            rule.selector.is_part(),
            &self.scope_subject_map,
            context,
        );
        for candidate in result.candidates {
            if context.nest_for_scope(Some(candidate.root), |context| {
                rule.matches_selector(&element, context)
            }) {
                return candidate.proximity;
            }
        }
        ScopeProximity::infinity()
    }

    fn did_finish_rebuild(&mut self) {
        self.shrink_maps_if_needed();
        self.compute_layer_order();
    }

    fn shrink_maps_if_needed(&mut self) {
        self.normal_rules.shrink_if_needed();
        if let Some(ref mut host_rules) = self.featureless_host_rules {
            host_rules.shrink_if_needed();
        }
        if let Some(ref mut slotted_rules) = self.slotted_rules {
            slotted_rules.shrink_if_needed();
        }
        self.animations.shrink_if_needed();
        self.custom_property_registrations.shrink_if_needed();
        self.invalidation_map.shrink_if_needed();
        self.relative_selector_invalidation_map.shrink_if_needed();
        self.additional_relative_selector_invalidation_map
            .shrink_if_needed();
        self.attribute_dependencies.shrink_if_needed();
        self.nth_of_attribute_dependencies.shrink_if_needed();
        self.nth_of_custom_state_dependencies.shrink_if_needed();
        self.nth_of_class_dependencies.shrink_if_needed();
        self.nth_of_mapped_ids.shrink_if_needed();
        self.mapped_ids.shrink_if_needed();
        self.layer_id.shrink_if_needed();
        self.selectors_for_cache_revalidation.shrink_if_needed();
        self.scope_subject_map.shrink_if_needed();
    }

    fn compute_layer_order(&mut self) {
        debug_assert_ne!(
            self.layers.len(),
            0,
            "There should be at least the root layer!"
        );
        if self.layers.len() == 1 {
            return; // Nothing to do
        }
        let (first, remaining) = self.layers.split_at_mut(1);
        let root = &mut first[0];
        let mut order = LayerOrder::first();
        compute_layer_order_for_subtree(root, remaining, &mut order);

        // NOTE(emilio): This is a bit trickier than it should to avoid having
        // to clone() around layer indices.
        fn compute_layer_order_for_subtree(
            parent: &mut CascadeLayer,
            remaining_layers: &mut [CascadeLayer],
            order: &mut LayerOrder,
        ) {
            for child in parent.children.iter() {
                debug_assert!(
                    parent.id < *child,
                    "Children are always registered after parents"
                );
                let child_index = (child.0 - parent.id.0 - 1) as usize;
                let (first, remaining) = remaining_layers.split_at_mut(child_index + 1);
                let child = &mut first[child_index];
                compute_layer_order_for_subtree(child, remaining, order);
            }

            if parent.id != LayerId::root() {
                parent.order = *order;
                order.inc();
            }
        }
        self.extra_data.sort_by_layer(&self.layers);
        self.animations
            .sort_with(&self.layers, compare_keyframes_in_same_layer);
        self.custom_property_registrations.sort(&self.layers)
    }

    /// Collects all the applicable media query results into `results`.
    ///
    /// This duplicates part of the logic in `add_stylesheet`, which is
    /// a bit unfortunate.
    ///
    /// FIXME(emilio): With a bit of smartness in
    /// `media_feature_affected_matches`, we could convert
    /// `EffectiveMediaQueryResults` into a vector without too much effort.
    fn collect_applicable_media_query_results_into<S>(
        device: &Device,
        stylesheet: &S,
        guard: &SharedRwLockReadGuard,
        results: &mut Vec<MediaListKey>,
        contents_list: &mut StyleSheetContentList,
        custom_media_map: &mut CustomMediaMap,
    ) where
        S: StylesheetInDocument + 'static,
    {
        if !stylesheet.enabled() {
            return;
        }
        if !stylesheet.is_effective_for_device(device, custom_media_map, guard) {
            return;
        }

        debug!(" + {:?}", stylesheet);
        let contents = stylesheet.contents(guard);
        results.push(contents.to_media_list_key());

        // Safety: StyleSheetContents are reference-counted with Arc.
        contents_list.push(StylesheetContentsPtr(unsafe {
            Arc::from_raw_addrefed(contents)
        }));

        let mut iter = stylesheet
            .contents(guard)
            .effective_rules(device, custom_media_map, guard);
        while let Some(rule) = iter.next() {
            match *rule {
                CssRule::CustomMedia(ref custom_media) => {
                    iter.custom_media()
                        .insert(custom_media.name.0.clone(), custom_media.condition.clone());
                },
                CssRule::Import(ref lock) => {
                    let import_rule = lock.read_with(guard);
                    debug!(" + {:?}", import_rule.stylesheet.media(guard));
                    results.push(import_rule.to_media_list_key());
                },
                CssRule::Media(ref media_rule) => {
                    debug!(" + {:?}", media_rule.media_queries.read_with(guard));
                    results.push(media_rule.to_media_list_key());
                },
                _ => {},
            }
        }
    }

    fn add_styles(
        &mut self,
        selectors: &SelectorList<SelectorImpl>,
        declarations: &Arc<Locked<PropertyDeclarationBlock>>,
        ancestor_selectors: Option<&SelectorList<SelectorImpl>>,
        containing_rule_state: &ContainingRuleState,
        mut replaced_selectors: Option<&mut ReplacedSelectors>,
        guard: &SharedRwLockReadGuard,
        rebuild_kind: SheetRebuildKind,
        mut precomputed_pseudo_element_decls: Option<&mut PrecomputedPseudoElementDeclarations>,
        quirks_mode: QuirksMode,
        mut collected_scope_dependencies: Option<&mut Vec<Dependency>>,
    ) -> Result<(), AllocErr> {
        self.num_declarations += declarations.read_with(guard).len();
        for selector in selectors.slice() {
            self.num_selectors += 1;

            let pseudo_elements = selector.pseudo_elements();
            let inner_pseudo_element = pseudo_elements.first();
            if let Some(pseudo) = inner_pseudo_element {
                if pseudo.is_precomputed() {
                    debug_assert!(selector.is_universal());
                    debug_assert!(ancestor_selectors.is_none());
                    debug_assert_eq!(containing_rule_state.layer_id, LayerId::root());
                    // Because we precompute pseudos, we cannot possibly calculate scope proximity.
                    debug_assert!(!containing_rule_state.scope_is_effective());
                    precomputed_pseudo_element_decls
                        .as_mut()
                        .expect("Expected precomputed declarations for the UA level")
                        .get_or_insert_with(pseudo, Vec::new)
                        .push(StyleSource::from_declarations(declarations.clone()));
                    continue;
                }
                if pseudo_elements
                    .iter()
                    .any(|p| p.is_unknown_webkit_pseudo_element())
                {
                    continue;
                }
            }

            debug_assert!(!pseudo_elements
                .iter()
                .any(|p| p.is_precomputed() || p.is_unknown_webkit_pseudo_element()));

            let selector = match ancestor_selectors {
                Some(s) => selector.replace_parent_selector(s),
                None => selector.clone(),
            };

            let hashes = AncestorHashes::new(&selector, quirks_mode);

            let rule = Rule::new(
                selector,
                hashes,
                StyleSource::from_declarations(declarations.clone()),
                self.rules_source_order,
                containing_rule_state.layer_id,
                containing_rule_state.container_condition_id,
                containing_rule_state.cascade_flags(),
                containing_rule_state.containing_scope_rule_state.id,
            );

            if let Some(ref mut replaced_selectors) = replaced_selectors {
                replaced_selectors.push(rule.selector.clone())
            }

            if rebuild_kind.should_rebuild_invalidation() {
                let mut scope_dependencies = note_selector_for_invalidation(
                    &rule.selector,
                    quirks_mode,
                    &mut self.invalidation_map,
                    &mut self.relative_selector_invalidation_map,
                    &mut self.additional_relative_selector_invalidation_map,
                    None,
                    None,
                )?;
                let mut needs_revalidation = false;
                let mut visitor = StylistSelectorVisitor {
                    passed_rightmost_selector: false,
                    needs_revalidation: &mut needs_revalidation,
                    non_link_visited_dependency: &mut self.non_link_visited_dependency,
                    in_selector_list_of: SelectorListKind::default(),
                    mapped_ids: &mut self.mapped_ids,
                    nth_of_mapped_ids: &mut self.nth_of_mapped_ids,
                    attribute_dependencies: &mut self.attribute_dependencies,
                    nth_of_class_dependencies: &mut self.nth_of_class_dependencies,
                    nth_of_attribute_dependencies: &mut self.nth_of_attribute_dependencies,
                    nth_of_custom_state_dependencies: &mut self.nth_of_custom_state_dependencies,
                    state_dependencies: &mut self.state_dependencies,
                    nth_of_state_dependencies: &mut self.nth_of_state_dependencies,
                    document_state_dependencies: &mut self.document_state_dependencies,
                };
                rule.selector.visit(&mut visitor);

                if needs_revalidation {
                    self.selectors_for_cache_revalidation.insert(
                        RevalidationSelectorAndHashes::new(
                            rule.selector.clone(),
                            rule.hashes.clone(),
                        ),
                        quirks_mode,
                    )?;
                }

                if let (Some(inner_scope_deps), Some(scope_deps)) = (
                    scope_dependencies.as_mut(),
                    collected_scope_dependencies.as_mut(),
                ) {
                    scope_deps.append(inner_scope_deps)
                }
            }

            // Part is special, since given it doesn't have any
            // selectors inside, it's not worth using a whole
            // SelectorMap for it.
            if let Some(parts) = rule.selector.parts() {
                // ::part() has all semantics, so we just need to
                // put any of them in the selector map.
                //
                // We choose the last one quite arbitrarily,
                // expecting it's slightly more likely to be more
                // specific.
                let map = self
                    .part_rules
                    .get_or_insert_with(Box::default)
                    .for_insertion(&pseudo_elements);
                map.try_reserve(1)?;
                let vec = map.entry(parts.last().unwrap().clone().0).or_default();
                vec.try_reserve(1)?;
                vec.push(rule);
            } else {
                let scope_matches_shadow_host = containing_rule_state
                    .containing_scope_rule_state
                    .matches_shadow_host
                    == ScopeMatchesShadowHost::Yes;
                let matches_featureless_host_only = match rule
                    .selector
                    .matches_featureless_host(scope_matches_shadow_host)
                {
                    MatchesFeaturelessHost::Only => true,
                    MatchesFeaturelessHost::Yes => {
                        // We need to insert this in featureless_host_rules but also normal_rules.
                        self.featureless_host_rules
                            .get_or_insert_with(Box::default)
                            .for_insertion(&pseudo_elements)
                            .insert(rule.clone(), quirks_mode)?;
                        false
                    },
                    MatchesFeaturelessHost::Never => false,
                };

                // NOTE(emilio): It's fine to look at :host and then at
                // ::slotted(..), since :host::slotted(..) could never
                // possibly match, as <slot> is not a valid shadow host.
                // :scope may match featureless shadow host if the scope
                // root is the shadow root.
                // See https://github.com/w3c/csswg-drafts/issues/9025
                let rules = if matches_featureless_host_only {
                    self.featureless_host_rules.get_or_insert_with(Box::default)
                } else if rule.selector.is_slotted() {
                    self.slotted_rules.get_or_insert_with(Box::default)
                } else {
                    &mut self.normal_rules
                }
                .for_insertion(&pseudo_elements);
                rules.insert(rule, quirks_mode)?;
            }
        }
        self.rules_source_order += 1;
        Ok(())
    }

    fn add_rule_list<S>(
        &mut self,
        rules: std::slice::Iter<CssRule>,
        device: &Device,
        quirks_mode: QuirksMode,
        stylesheet: &S,
        sheet_index: usize,
        guard: &SharedRwLockReadGuard,
        rebuild_kind: SheetRebuildKind,
        containing_rule_state: &mut ContainingRuleState,
        mut precomputed_pseudo_element_decls: Option<&mut PrecomputedPseudoElementDeclarations>,
        mut difference: Option<&mut CascadeDataDifference>,
    ) -> Result<(), AllocErr>
    where
        S: StylesheetInDocument + 'static,
    {
        for rule in rules {
            // Handle leaf rules first, as those are by far the most common
            // ones, and are always effective, so we can skip some checks.
            let mut handled = true;
            let mut list_for_nested_rules = None;
            match *rule {
                CssRule::Style(ref locked) => {
                    let style_rule = locked.read_with(guard);
                    let has_nested_rules = style_rule.rules.is_some();
                    let mut replaced_selectors = ReplacedSelectors::new();
                    let ancestor_selectors = containing_rule_state.ancestor_selector_lists.last();
                    let collect_replaced_selectors =
                        has_nested_rules && ancestor_selectors.is_some();
                    let mut inner_dependencies: Option<Vec<Dependency>> =
                        containing_rule_state.scope_is_effective().then(Vec::new);
                    self.add_styles(
                        &style_rule.selectors,
                        &style_rule.block,
                        ancestor_selectors,
                        containing_rule_state,
                        if collect_replaced_selectors {
                            Some(&mut replaced_selectors)
                        } else {
                            None
                        },
                        guard,
                        rebuild_kind,
                        precomputed_pseudo_element_decls.as_deref_mut(),
                        quirks_mode,
                        inner_dependencies.as_mut(),
                    )?;
                    if let Some(mut scope_dependencies) = inner_dependencies {
                        containing_rule_state
                            .containing_scope_rule_state
                            .inner_dependencies
                            .append(&mut scope_dependencies);
                    }
                    if has_nested_rules {
                        handled = false;
                        list_for_nested_rules = Some(if collect_replaced_selectors {
                            SelectorList::from_iter(replaced_selectors.drain(..))
                        } else {
                            style_rule.selectors.clone()
                        });
                    }
                },
                CssRule::NestedDeclarations(ref rule) => {
                    if let Some(ancestor_selectors) =
                        containing_rule_state.ancestor_selector_lists.last()
                    {
                        let decls = &rule.read_with(guard).block;
                        let selectors = match containing_rule_state.nested_declarations_context {
                            NestedDeclarationsContext::Style => ancestor_selectors,
                            NestedDeclarationsContext::Scope => &*IMPLICIT_SCOPE,
                        };
                        let mut inner_dependencies: Option<Vec<Dependency>> =
                            containing_rule_state.scope_is_effective().then(Vec::new);
                        self.add_styles(
                            selectors,
                            decls,
                            /* ancestor_selectors = */ None,
                            containing_rule_state,
                            /* replaced_selectors = */ None,
                            guard,
                            // We don't need to rebuild invalidation data, since our ancestor style
                            // rule would've done this.
                            SheetRebuildKind::CascadeOnly,
                            precomputed_pseudo_element_decls.as_deref_mut(),
                            quirks_mode,
                            inner_dependencies.as_mut(),
                        )?;
                        if let Some(mut scope_dependencies) = inner_dependencies {
                            containing_rule_state
                                .containing_scope_rule_state
                                .inner_dependencies
                                .append(&mut scope_dependencies);
                        }
                    }
                },
                CssRule::Keyframes(ref keyframes_rule) => {
                    debug!("Found valid keyframes rule: {:?}", *keyframes_rule);
                    let keyframes_rule = keyframes_rule.read_with(guard);
                    let name = keyframes_rule.name.as_atom().clone();
                    let animation = KeyframesAnimation::from_keyframes(
                        &keyframes_rule.keyframes,
                        keyframes_rule.vendor_prefix.clone(),
                        guard,
                    );
                    self.animations.try_insert_with(
                        name,
                        animation,
                        containing_rule_state.layer_id,
                        compare_keyframes_in_same_layer,
                    )?;
                },
                CssRule::Property(ref registration) => {
                    self.custom_property_registrations.try_insert(
                        registration.name.0.clone(),
                        Arc::clone(registration),
                        containing_rule_state.layer_id,
                    )?;
                },
                CssRule::FontFace(ref rule) => {
                    // NOTE(emilio): We don't care about container_condition_id
                    // because:
                    //
                    //     Global, name-defining at-rules such as @keyframes or
                    //     @font-face or @layer that are defined inside container
                    //     queries are not constrained by the container query
                    //     conditions.
                    //
                    // https://drafts.csswg.org/css-contain-3/#container-rule
                    // (Same elsewhere)
                    self.extra_data
                        .add_font_face(rule, containing_rule_state.layer_id);
                },
                CssRule::FontFeatureValues(ref rule) => {
                    self.extra_data
                        .add_font_feature_values(rule, containing_rule_state.layer_id);
                },
                CssRule::FontPaletteValues(ref rule) => {
                    self.extra_data
                        .add_font_palette_values(rule, containing_rule_state.layer_id);
                },
                CssRule::CounterStyle(ref rule) => {
                    self.extra_data.add_counter_style(
                        guard,
                        rule,
                        containing_rule_state.layer_id,
                    )?;
                },
                CssRule::PositionTry(ref rule) => {
                    let name = rule.read_with(guard).name.0.clone();
                    if let Some(ref mut difference) = difference {
                        difference.changed_position_try_names.insert(name.clone());
                    }
                    self.extra_data.add_position_try(
                        name,
                        rule.clone(),
                        containing_rule_state.layer_id,
                    )?;
                },
                CssRule::Page(ref rule) => {
                    self.extra_data
                        .add_page(guard, rule, containing_rule_state.layer_id)?;
                    handled = false;
                },
                CssRule::ViewTransition(ref rule) => {
                    self.extra_data
                        .add_view_transition(rule, containing_rule_state.layer_id);
                },
                _ => {
                    handled = false;
                },
            }

            if handled {
                // Assert that there are no children, and that the rule is
                // effective.
                if cfg!(debug_assertions) {
                    let mut effective = false;
                    let children = EffectiveRulesIterator::<&CustomMediaMap>::children(
                        rule,
                        device,
                        quirks_mode,
                        &self.custom_media,
                        guard,
                        &mut effective,
                    );
                    debug_assert!(children.is_empty());
                    debug_assert!(effective);
                }
                continue;
            }

            let mut effective = false;
            let children = EffectiveRulesIterator::<&CustomMediaMap>::children(
                rule,
                device,
                quirks_mode,
                &self.custom_media,
                guard,
                &mut effective,
            );
            if !effective {
                continue;
            }

            fn maybe_register_layer(data: &mut CascadeData, layer: &LayerName) -> LayerId {
                // TODO: Measure what's more common / expensive, if
                // layer.clone() or the double hash lookup in the insert
                // case.
                if let Some(id) = data.layer_id.get(layer) {
                    return *id;
                }
                let id = LayerId(data.layers.len() as u16);

                let parent_layer_id = if layer.layer_names().len() > 1 {
                    let mut parent = layer.clone();
                    parent.0.pop();

                    *data
                        .layer_id
                        .get_mut(&parent)
                        .expect("Parent layers should be registered before child layers")
                } else {
                    LayerId::root()
                };

                data.layers[parent_layer_id.0 as usize].children.push(id);
                data.layers.push(CascadeLayer {
                    id,
                    // NOTE(emilio): Order is evaluated after rebuild in
                    // compute_layer_order.
                    order: LayerOrder::first(),
                    children: vec![],
                });

                data.layer_id.insert(layer.clone(), id);

                id
            }

            fn maybe_register_layers(
                data: &mut CascadeData,
                name: Option<&LayerName>,
                containing_rule_state: &mut ContainingRuleState,
            ) {
                let anon_name;
                let name = match name {
                    Some(name) => name,
                    None => {
                        anon_name = LayerName::new_anonymous();
                        &anon_name
                    },
                };
                for name in name.layer_names() {
                    containing_rule_state.layer_name.0.push(name.clone());
                    containing_rule_state.layer_id =
                        maybe_register_layer(data, &containing_rule_state.layer_name);
                }
                debug_assert_ne!(containing_rule_state.layer_id, LayerId::root());
            }

            let saved_containing_rule_state = containing_rule_state.save();
            match *rule {
                CssRule::Import(ref lock) => {
                    let import_rule = lock.read_with(guard);
                    if rebuild_kind.should_rebuild_invalidation() {
                        self.effective_media_query_results
                            .saw_effective(import_rule);
                    }
                    match import_rule.layer {
                        ImportLayer::Named(ref name) => {
                            maybe_register_layers(self, Some(name), containing_rule_state)
                        },
                        ImportLayer::Anonymous => {
                            maybe_register_layers(self, None, containing_rule_state)
                        },
                        ImportLayer::None => {},
                    }
                },
                CssRule::Media(ref media_rule) => {
                    if rebuild_kind.should_rebuild_invalidation() {
                        self.effective_media_query_results
                            .saw_effective(&**media_rule);
                    }
                },
                CssRule::LayerBlock(ref rule) => {
                    maybe_register_layers(self, rule.name.as_ref(), containing_rule_state);
                },
                CssRule::CustomMedia(ref custom_media) => {
                    self.custom_media
                        .insert(custom_media.name.0.clone(), custom_media.condition.clone());
                },
                CssRule::LayerStatement(ref rule) => {
                    for name in &*rule.names {
                        maybe_register_layers(self, Some(name), containing_rule_state);
                        // Register each layer individually.
                        containing_rule_state.restore(&saved_containing_rule_state);
                    }
                },
                CssRule::Style(..) => {
                    containing_rule_state.nested_declarations_context =
                        NestedDeclarationsContext::Style;
                    if let Some(s) = list_for_nested_rules {
                        containing_rule_state.ancestor_selector_lists.push(s);
                    }
                },
                CssRule::Container(ref rule) => {
                    let id = ContainerConditionId(self.container_conditions.len() as u16);
                    let condition = ContainerConditionReference {
                        parent: containing_rule_state.container_condition_id,
                        conditions: rule.conditions.0.clone(),
                    };
                    self.container_conditions.push(condition);
                    containing_rule_state.container_condition_id = id;

                    if rebuild_kind.should_rebuild_invalidation() {
                        for condition in rule.conditions.0.iter() {
                            condition.insert_attribute_references_in_dependency_map(
                                &mut self.attr_function_dependencies,
                                &mut self.attribute_dependencies,
                            );
                        }
                    }
                },
                CssRule::StartingStyle(..) => {
                    containing_rule_state
                        .cascade_flags
                        .insert(RuleCascadeFlags::STARTING_STYLE);
                },
                CssRule::AppearanceBase(..) => {
                    containing_rule_state
                        .cascade_flags
                        .insert(RuleCascadeFlags::APPEARANCE_BASE);
                },
                CssRule::Scope(ref rule) => {
                    containing_rule_state.nested_declarations_context =
                        NestedDeclarationsContext::Scope;
                    let id = ScopeConditionId(self.scope_conditions.len() as u16);
                    let mut matches_shadow_host = false;
                    let implicit_scope_root = if let Some(start) = rule.bounds.start.as_ref() {
                        matches_shadow_host = scope_start_matches_shadow_host(start);
                        // Would be unused, but use the default as fallback.
                        StylistImplicitScopeRoot::default()
                    } else {
                        // (Re)Moving stylesheets trigger a complete flush, so saving the implicit
                        // root here should be safe.
                        if let Some(root) = stylesheet.implicit_scope_root() {
                            matches_shadow_host = root.matches_shadow_host();
                            match root {
                                ImplicitScopeRoot::InLightTree(_)
                                | ImplicitScopeRoot::Constructed
                                | ImplicitScopeRoot::DocumentElement => {
                                    StylistImplicitScopeRoot::Normal(root)
                                },
                                ImplicitScopeRoot::ShadowHost(_)
                                | ImplicitScopeRoot::InShadowTree(_) => {
                                    // Style data can be shared between shadow trees, so we must
                                    // query the implicit root for that specific tree.
                                    // Shared stylesheet means shared sheet indices, so we can
                                    // use that to locate the implicit root.
                                    // Technically, this can also be applied to the light tree,
                                    // but that requires also knowing about what cascade level we're at.
                                    StylistImplicitScopeRoot::Cached(sheet_index)
                                },
                            }
                        } else {
                            // Could not find implicit scope root, but use the default as fallback.
                            StylistImplicitScopeRoot::default()
                        }
                    };

                    let replaced =
                        {
                            let start = rule.bounds.start.as_ref().map(|selector| {
                                match containing_rule_state.ancestor_selector_lists.last() {
                                    Some(s) => selector.replace_parent_selector(s),
                                    None => selector.clone(),
                                }
                            });
                            let implicit_scope_selector = &*IMPLICIT_SCOPE;
                            let end = rule.bounds.end.as_ref().map(|selector| {
                                selector.replace_parent_selector(implicit_scope_selector)
                            });
                            containing_rule_state
                                .ancestor_selector_lists
                                .push(implicit_scope_selector.clone());
                            ScopeBoundsWithHashes::new(quirks_mode, start, end)
                        };

                    if let Some(selectors) = replaced.start.as_ref() {
                        self.scope_subject_map
                            .add_bound_start(&selectors.selectors, quirks_mode);
                    }

                    let is_trivial = replaced.is_trivial();
                    self.scope_conditions.push(ScopeConditionReference {
                        parent: containing_rule_state.containing_scope_rule_state.id,
                        condition: Some(replaced),
                        implicit_scope_root,
                        is_trivial,
                    });

                    containing_rule_state
                        .containing_scope_rule_state
                        .matches_shadow_host
                        .nest_for_scope(matches_shadow_host);
                    containing_rule_state.containing_scope_rule_state.id = id;
                    containing_rule_state
                        .containing_scope_rule_state
                        .inner_dependencies
                        .reserve(children.iter().len());
                },
                // We don't care about any other rule.
                _ => {},
            }

            if !children.is_empty() {
                self.add_rule_list(
                    children.iter(),
                    device,
                    quirks_mode,
                    stylesheet,
                    sheet_index,
                    guard,
                    rebuild_kind,
                    containing_rule_state,
                    precomputed_pseudo_element_decls.as_deref_mut(),
                    difference.as_deref_mut(),
                )?;
            }

            if let Some(scope_restore_data) =
                containing_rule_state.restore(&saved_containing_rule_state)
            {
                let (cur_scope_inner_dependencies, scope_idx) = scope_restore_data;
                let cur_scope = &self.scope_conditions[scope_idx.0 as usize];
                if let Some(cond) = cur_scope.condition.as_ref() {
                    let mut _unused = false;
                    let visitor = StylistSelectorVisitor {
                        passed_rightmost_selector: true,
                        needs_revalidation: &mut _unused,
                        non_link_visited_dependency: &mut self.non_link_visited_dependency,
                        in_selector_list_of: SelectorListKind::default(),
                        mapped_ids: &mut self.mapped_ids,
                        nth_of_mapped_ids: &mut self.nth_of_mapped_ids,
                        attribute_dependencies: &mut self.attribute_dependencies,
                        nth_of_class_dependencies: &mut self.nth_of_class_dependencies,
                        nth_of_attribute_dependencies: &mut self.nth_of_attribute_dependencies,
                        nth_of_custom_state_dependencies: &mut self
                            .nth_of_custom_state_dependencies,
                        state_dependencies: &mut self.state_dependencies,
                        nth_of_state_dependencies: &mut self.nth_of_state_dependencies,
                        document_state_dependencies: &mut self.document_state_dependencies,
                    };

                    let dependency_vector = build_scope_dependencies(
                        quirks_mode,
                        cur_scope_inner_dependencies,
                        visitor,
                        cond,
                        &mut self.invalidation_map,
                        &mut self.relative_selector_invalidation_map,
                        &mut self.additional_relative_selector_invalidation_map,
                    )?;

                    containing_rule_state
                        .containing_scope_rule_state
                        .inner_dependencies
                        .extend(dependency_vector);
                }
            }
        }

        Ok(())
    }

    // Returns Err(..) to signify OOM
    fn add_stylesheet<S>(
        &mut self,
        device: &Device,
        quirks_mode: QuirksMode,
        stylesheet: &S,
        sheet_index: usize,
        guard: &SharedRwLockReadGuard,
        rebuild_kind: SheetRebuildKind,
        precomputed_pseudo_element_decls: Option<&mut PrecomputedPseudoElementDeclarations>,
        difference: Option<&mut CascadeDataDifference>,
    ) -> Result<(), AllocErr>
    where
        S: StylesheetInDocument + 'static,
    {
        if !stylesheet.enabled() {
            return Ok(());
        }

        if !stylesheet.is_effective_for_device(device, &self.custom_media, guard) {
            return Ok(());
        }

        let contents = stylesheet.contents(guard);
        if rebuild_kind.should_rebuild_invalidation() {
            self.effective_media_query_results.saw_effective(contents);
        }

        let mut state = ContainingRuleState::default();
        self.add_rule_list(
            contents.rules(guard).iter(),
            device,
            quirks_mode,
            stylesheet,
            sheet_index,
            guard,
            rebuild_kind,
            &mut state,
            precomputed_pseudo_element_decls,
            difference,
        )?;

        Ok(())
    }

    /// Returns whether all the media-feature affected values matched before and
    /// match now in the given stylesheet.
    pub fn media_feature_affected_matches<S>(
        &self,
        stylesheet: &S,
        guard: &SharedRwLockReadGuard,
        device: &Device,
        quirks_mode: QuirksMode,
    ) -> bool
    where
        S: StylesheetInDocument + 'static,
    {
        use crate::invalidation::media_queries::PotentiallyEffectiveMediaRules;

        let effective_now = stylesheet.is_effective_for_device(device, &self.custom_media, guard);

        let contents = stylesheet.contents(guard);
        let effective_then = self.effective_media_query_results.was_effective(contents);

        if effective_now != effective_then {
            debug!(
                " > Stylesheet {:?} changed -> {}, {}",
                stylesheet.media(guard),
                effective_then,
                effective_now
            );
            return false;
        }

        if !effective_now {
            return true;
        }

        // We don't need a custom media map for PotentiallyEffectiveMediaRules.
        let custom_media = CustomMediaMap::default();
        let mut iter =
            contents.iter_rules::<PotentiallyEffectiveMediaRules, _>(device, &custom_media, guard);
        while let Some(rule) = iter.next() {
            match *rule {
                CssRule::Style(..)
                | CssRule::NestedDeclarations(..)
                | CssRule::Namespace(..)
                | CssRule::FontFace(..)
                | CssRule::Container(..)
                | CssRule::CounterStyle(..)
                | CssRule::Supports(..)
                | CssRule::Keyframes(..)
                | CssRule::Margin(..)
                | CssRule::Page(..)
                | CssRule::Property(..)
                | CssRule::Document(..)
                | CssRule::LayerBlock(..)
                | CssRule::LayerStatement(..)
                | CssRule::FontPaletteValues(..)
                | CssRule::FontFeatureValues(..)
                | CssRule::Scope(..)
                | CssRule::StartingStyle(..)
                | CssRule::AppearanceBase(..)
                | CssRule::CustomMedia(..)
                | CssRule::PositionTry(..)
                | CssRule::ViewTransition(..) => {
                    // Not affected by device changes. @custom-media is handled by the potential
                    // @media rules referencing it being handled.
                    continue;
                },
                CssRule::Import(ref lock) => {
                    let import_rule = lock.read_with(guard);
                    let effective_now = match import_rule.stylesheet.media(guard) {
                        Some(m) => m.evaluate(
                            device,
                            quirks_mode,
                            &mut CustomMediaEvaluator::new(&self.custom_media, guard),
                        ),
                        None => true,
                    };
                    let effective_then = self
                        .effective_media_query_results
                        .was_effective(import_rule);
                    if effective_now != effective_then {
                        debug!(
                            " > @import rule {:?} changed {} -> {}",
                            import_rule.stylesheet.media(guard),
                            effective_then,
                            effective_now
                        );
                        return false;
                    }

                    if !effective_now {
                        iter.skip_children();
                    }
                },
                CssRule::Media(ref media_rule) => {
                    let mq = media_rule.media_queries.read_with(guard);
                    let effective_now = mq.evaluate(
                        device,
                        quirks_mode,
                        &mut CustomMediaEvaluator::new(&self.custom_media, guard),
                    );
                    let effective_then = self
                        .effective_media_query_results
                        .was_effective(&**media_rule);

                    if effective_now != effective_then {
                        debug!(
                            " > @media rule {:?} changed {} -> {}",
                            mq, effective_then, effective_now
                        );
                        return false;
                    }

                    if !effective_now {
                        iter.skip_children();
                    }
                },
            }
        }

        true
    }

    /// Returns the custom properties map.
    pub fn custom_property_registrations(&self) -> &LayerOrderedMap<Arc<PropertyRegistration>> {
        &self.custom_property_registrations
    }

    fn revalidate_scopes<E: TElement>(
        &self,
        element: &E,
        matching_context: &mut MatchingContext<E::Impl>,
        result: &mut ScopeRevalidationResult,
    ) {
        // TODO(dshin): A scope block may not contain style rule for this element, but we don't keep
        // track of that, so we check _all_ scope conditions. It's possible for two comparable elements
        // to share scope & relevant styles rules, but also differ in scopes that do not contain style
        // rules relevant to them. So while we can be certain that an identical result share scoped styles
        // (Given that other sharing conditions are met), it is uncertain if elements with non-matching
        // results do not.
        for condition_id in 1..self.scope_conditions.len() {
            let condition = &self.scope_conditions[condition_id];
            let matches = if condition.is_trivial {
                // Just ignore this condition - for style sharing candidates, guaranteed
                // the same match result.
                continue;
            } else {
                let result = scope_root_candidates(
                    &self.scope_conditions,
                    ScopeConditionId(condition_id as u16),
                    element,
                    // This should be ok since we aren't sharing styles across shadow boundaries.
                    false,
                    &self.scope_subject_map,
                    matching_context,
                );
                !result.candidates.is_empty()
            };
            result.scopes_matched.push(matches);
        }
    }

    /// Clears the cascade data, but not the invalidation data.
    fn clear_cascade_data(&mut self) {
        self.normal_rules.clear();
        if let Some(ref mut slotted_rules) = self.slotted_rules {
            slotted_rules.clear();
        }
        if let Some(ref mut part_rules) = self.part_rules {
            part_rules.clear();
        }
        if let Some(ref mut host_rules) = self.featureless_host_rules {
            host_rules.clear();
        }
        self.animations.clear();
        self.custom_property_registrations.clear();
        self.layer_id.clear();
        self.layers.clear();
        self.layers.push(CascadeLayer::root());
        self.custom_media.clear();
        self.container_conditions.clear();
        self.container_conditions
            .push(ContainerConditionReference::none());
        self.scope_conditions.clear();
        self.scope_conditions.push(ScopeConditionReference::none());
        self.extra_data.clear();
        self.rules_source_order = 0;
        self.num_selectors = 0;
        self.num_declarations = 0;
    }

    fn clear_invalidation_data(&mut self) {
        self.invalidation_map.clear();
        self.relative_selector_invalidation_map.clear();
        self.additional_relative_selector_invalidation_map.clear();
        self.attribute_dependencies.clear();
        self.attr_function_dependencies.clear();
        self.nth_of_attribute_dependencies.clear();
        self.nth_of_custom_state_dependencies.clear();
        self.nth_of_class_dependencies.clear();
        self.state_dependencies = ElementState::empty();
        self.nth_of_state_dependencies = ElementState::empty();
        self.non_link_visited_dependency = false;
        self.document_state_dependencies = DocumentState::empty();
        self.mapped_ids.clear();
        self.nth_of_mapped_ids.clear();
        self.selectors_for_cache_revalidation.clear();
        self.effective_media_query_results.clear();
        self.scope_subject_map.clear();
    }
}

fn note_scope_selector_for_invalidation(
    quirks_mode: QuirksMode,
    scope_dependencies: &Arc<servo_arc::HeaderSlice<(), Dependency>>,
    dependency_vector: &mut Vec<Dependency>,
    invalidation_map: &mut InvalidationMap,
    relative_selector_invalidation_map: &mut InvalidationMap,
    additional_relative_selector_invalidation_map: &mut AdditionalRelativeSelectorInvalidationMap,
    visitor: &mut StylistSelectorVisitor<'_>,
    scope_kind: ScopeDependencyInvalidationKind,
    s: &Selector<SelectorImpl>,
) -> Result<(), AllocErr> {
    let mut new_inner_dependencies = note_selector_for_invalidation(
        &s.clone(),
        quirks_mode,
        invalidation_map,
        relative_selector_invalidation_map,
        additional_relative_selector_invalidation_map,
        Some(scope_dependencies),
        Some(scope_kind),
    )?;
    s.visit(visitor);
    if let Some(dep) = new_inner_dependencies.as_mut() {
        dependency_vector.append(dep);
    }
    Ok(())
}

fn build_scope_dependencies(
    quirks_mode: QuirksMode,
    mut cur_scope_inner_dependencies: Vec<Dependency>,
    mut visitor: StylistSelectorVisitor<'_>,
    cond: &ScopeBoundsWithHashes,
    invalidation_map: &mut InvalidationMap,
    relative_selector_invalidation_map: &mut InvalidationMap,
    additional_relative_selector_invalidation_map: &mut AdditionalRelativeSelectorInvalidationMap,
) -> Result<Vec<Dependency>, AllocErr> {
    if cond.end.is_some() {
        let deps =
            ThinArc::from_header_and_iter((), cur_scope_inner_dependencies.clone().into_iter());
        let mut end_dependency_vector = Vec::new();
        for s in cond.end_selectors() {
            note_scope_selector_for_invalidation(
                quirks_mode,
                &deps,
                &mut end_dependency_vector,
                invalidation_map,
                relative_selector_invalidation_map,
                additional_relative_selector_invalidation_map,
                &mut visitor,
                ScopeDependencyInvalidationKind::ScopeEnd,
                s,
            )?;
        }
        cur_scope_inner_dependencies.append(&mut end_dependency_vector);
    }
    let inner_scope_dependencies =
        ThinArc::from_header_and_iter((), cur_scope_inner_dependencies.into_iter());

    Ok(if cond.start.is_some() {
        let mut dependency_vector = Vec::new();
        for s in cond.start_selectors() {
            note_scope_selector_for_invalidation(
                quirks_mode,
                &inner_scope_dependencies,
                &mut dependency_vector,
                invalidation_map,
                relative_selector_invalidation_map,
                additional_relative_selector_invalidation_map,
                &mut visitor,
                ScopeDependencyInvalidationKind::ExplicitScope,
                s,
            )?;
        }
        dependency_vector
    } else {
        vec![Dependency::new(
            IMPLICIT_SCOPE.slice()[0].clone(),
            0,
            Some(inner_scope_dependencies),
            DependencyInvalidationKind::Scope(ScopeDependencyInvalidationKind::ImplicitScope),
        )]
    })
}

impl CascadeDataCacheEntry for CascadeData {
    fn rebuild<S>(
        device: &Device,
        quirks_mode: QuirksMode,
        collection: SheetCollectionFlusher<S>,
        guard: &SharedRwLockReadGuard,
        old: &Self,
        difference: &mut CascadeDataDifference,
    ) -> Result<Arc<Self>, AllocErr>
    where
        S: StylesheetInDocument + PartialEq + 'static,
    {
        debug_assert!(collection.dirty(), "We surely need to do something?");
        // If we're doing a full rebuild anyways, don't bother cloning the data.
        let mut updatable_entry = match collection.data_validity() {
            DataValidity::Valid | DataValidity::CascadeInvalid => old.clone(),
            DataValidity::FullyInvalid => Self::new(),
        };
        updatable_entry.rebuild(device, quirks_mode, collection, guard, difference)?;
        Ok(Arc::new(updatable_entry))
    }

    #[cfg(feature = "gecko")]
    fn add_size_of(&self, ops: &mut MallocSizeOfOps, sizes: &mut ServoStyleSetSizes) {
        self.normal_rules.add_size_of(ops, sizes);
        if let Some(ref slotted_rules) = self.slotted_rules {
            slotted_rules.add_size_of(ops, sizes);
        }
        if let Some(ref part_rules) = self.part_rules {
            part_rules.add_size_of(ops, sizes);
        }
        if let Some(ref host_rules) = self.featureless_host_rules {
            host_rules.add_size_of(ops, sizes);
        }
        sizes.mInvalidationMap += self.invalidation_map.size_of(ops);
        sizes.mRevalidationSelectors += self.selectors_for_cache_revalidation.size_of(ops);
        sizes.mOther += self.animations.size_of(ops);
        sizes.mOther += self.effective_media_query_results.size_of(ops);
        sizes.mOther += self.extra_data.size_of(ops);
    }
}

impl Default for CascadeData {
    fn default() -> Self {
        CascadeData::new()
    }
}

/// A rule, that wraps a style rule, but represents a single selector of the
/// rule.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct Rule {
    /// The selector this struct represents. We store this and the
    /// any_{important,normal} booleans inline in the Rule to avoid
    /// pointer-chasing when gathering applicable declarations, which
    /// can ruin performance when there are a lot of rules.
    #[ignore_malloc_size_of = "CssRules have primary refs, we measure there"]
    pub selector: Selector<SelectorImpl>,

    /// The ancestor hashes associated with the selector.
    pub hashes: AncestorHashes,

    /// The source order this style rule appears in. Note that we only use
    /// three bytes to store this value in ApplicableDeclarationsBlock, so
    /// we could repurpose that storage here if we needed to.
    pub source_order: u32,

    /// The current layer id of this style rule.
    pub layer_id: LayerId,

    /// The current @container rule id.
    pub container_condition_id: ContainerConditionId,

    /// Flags for special cascade behaviors.
    pub cascade_flags: RuleCascadeFlags,

    /// The current @scope rule id.
    pub scope_condition_id: ScopeConditionId,

    /// Whether the selector map always covers our selector.
    pub bucket_matches: BucketMatches,

    /// The actual style rule.
    #[ignore_malloc_size_of = "Secondary ref. Primary ref is in StyleRule under Stylesheet."]
    pub style_source: StyleSource,
}

impl SelectorMapEntry for Rule {
    fn selector(&self) -> SelectorIter<'_, SelectorImpl> {
        self.selector.iter()
    }

    fn set_bucket_matches(&mut self, bucket_matches: BucketMatches) {
        self.bucket_matches = bucket_matches;
    }
}

impl Rule {
    /// Returns the specificity of the rule.
    pub fn specificity(&self) -> u32 {
        self.selector.specificity()
    }

    /// Turns this rule into an `ApplicableDeclarationBlock` for the given
    /// cascade level.
    pub fn to_applicable_declaration_block(
        &self,
        level: CascadeLevel,
        cascade_data: &CascadeData,
        scope_proximity: ScopeProximity,
    ) -> ApplicableDeclarationBlock<'_> {
        ApplicableDeclarationBlock::new(
            self.style_source.borrow(),
            self.source_order,
            level,
            self.specificity(),
            cascade_data.layer_order_for(self.layer_id),
            scope_proximity,
            self.cascade_flags,
        )
    }

    /// Creates a new Rule.
    pub fn new(
        selector: Selector<SelectorImpl>,
        hashes: AncestorHashes,
        style_source: StyleSource,
        source_order: u32,
        layer_id: LayerId,
        container_condition_id: ContainerConditionId,
        cascade_flags: RuleCascadeFlags,
        scope_condition_id: ScopeConditionId,
    ) -> Self {
        Self {
            selector,
            hashes,
            style_source,
            source_order,
            layer_id,
            container_condition_id,
            cascade_flags,
            scope_condition_id,
            bucket_matches: BucketMatches::Unknown,
        }
    }

    fn iter_past_subject<'a, E: TElement>(
        selector: &'a Selector<SelectorImpl>,
        mut element: E,
        context: &mut MatchingContext<E::Impl>,
    ) -> (E, SelectorIter<'a, SelectorImpl>) {
        let mut offset = 0;
        let mut skipped_pseudo = false;
        // Skip the subject + pseudo bit. Note that nested selector lists are dealt with in
        // find_bucket.
        let mut iter = selector.iter();
        loop {
            for _ in &mut iter {
                offset += 1;
            }
            if iter.next_sequence() != Some(Combinator::PseudoElement) {
                break;
            }
            if skipped_pseudo || context.matching_mode() != MatchingMode::ForStatelessPseudoElement
            {
                element = element.pseudo_element_originating_element().unwrap();
            }
            skipped_pseudo = true;
            offset += 1;
        }
        (element, selector.iter_from(offset))
    }

    /// Tests a given element against our selector.
    #[inline(always)]
    pub fn matches_selector<E: TElement>(
        &self,
        element: &E,
        context: &mut MatchingContext<E::Impl>,
    ) -> bool {
        if context
            .bloom_filter
            .is_some_and(|f| !selector_may_match(&self.hashes, f))
        {
            return false;
        }
        if self.bucket_matches == BucketMatches::Full {
            return true;
        }
        let originating_element;
        let (element, iter, subject) = if self.bucket_matches == BucketMatches::Subject {
            let (e, iter) = Self::iter_past_subject(&self.selector, *element, context);
            originating_element = e;
            (&originating_element, iter, SubjectOrPseudoElement::No)
        } else {
            (element, self.selector.iter(), SubjectOrPseudoElement::Yes)
        };
        matches_complex_selector(iter, element, context, subject).to_bool(true)
    }
}

// The size of this is critical to performance on the bloom-basic
// microbenchmark.
// When iterating over a large Rule array, we want to be able to fast-reject
// selectors (with the inline hashes) with as few cache misses as possible.
size_of_test!(Rule, 40);

/// A function to be able to test the revalidation stuff.
pub fn needs_revalidation_for_testing(s: &Selector<SelectorImpl>) -> bool {
    let mut needs_revalidation = false;
    let mut mapped_ids = Default::default();
    let mut nth_of_mapped_ids = Default::default();
    let mut attribute_dependencies = Default::default();
    let mut nth_of_class_dependencies = Default::default();
    let mut nth_of_attribute_dependencies = Default::default();
    let mut nth_of_custom_state_dependencies = Default::default();
    let mut state_dependencies = ElementState::empty();
    let mut nth_of_state_dependencies = ElementState::empty();
    let mut document_state_dependencies = DocumentState::empty();
    let mut non_link_visited_dependency = false;
    let mut visitor = StylistSelectorVisitor {
        passed_rightmost_selector: false,
        needs_revalidation: &mut needs_revalidation,
        non_link_visited_dependency: &mut non_link_visited_dependency,
        in_selector_list_of: SelectorListKind::default(),
        mapped_ids: &mut mapped_ids,
        nth_of_mapped_ids: &mut nth_of_mapped_ids,
        attribute_dependencies: &mut attribute_dependencies,
        nth_of_class_dependencies: &mut nth_of_class_dependencies,
        nth_of_attribute_dependencies: &mut nth_of_attribute_dependencies,
        nth_of_custom_state_dependencies: &mut nth_of_custom_state_dependencies,
        state_dependencies: &mut state_dependencies,
        nth_of_state_dependencies: &mut nth_of_state_dependencies,
        document_state_dependencies: &mut document_state_dependencies,
    };
    s.visit(&mut visitor);
    needs_revalidation
}
