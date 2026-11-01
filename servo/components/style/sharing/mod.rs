/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Code related to the style sharing cache, an optimization that allows similar
//! nodes to share style without having to run selector matching twice.
//!
//! The basic setup is as follows.  We have an LRU cache of style sharing
//! candidates.  When we try to style a target element, we first check whether
//! we can quickly determine that styles match something in this cache, and if
//! so we just use the cached style information.  This check is done with a
//! StyleBloom filter set up for the target element, which may not be a correct
//! state for the cached candidate element if they're cousins instead of
//! siblings.
//!
//! The complicated part is determining that styles match.  This is subject to
//! the following constraints:
//!
//! 1) The target and candidate must be inheriting the same styles.
//! 2) The target and candidate must have exactly the same rules matching them.
//! 3) The target and candidate must have exactly the same non-selector-based
//!    style information (inline styles, presentation hints).
//! 4) The target and candidate must have exactly the same rules matching their
//!    pseudo-elements, because an element's style data points to the style
//!    data for its pseudo-elements.
//!
//! These constraints are satisfied in the following ways:
//!
//! * We check that the parents of the target and the candidate have the same
//!   computed style.  This addresses constraint 1.
//!
//! * We check that the target and candidate have the same inline style and
//!   presentation hint declarations.  This addresses constraint 3.
//!
//! * We ensure that a target matches a candidate only if they have the same
//!   matching result for all selectors that target either elements or the
//!   originating elements of pseudo-elements.  This addresses constraint 4
//!   (because it prevents a target that has pseudo-element styles from matching
//!   a candidate that has different pseudo-element styles) as well as
//!   constraint 2.
//!
//! The actual checks that ensure that elements match the same rules are
//! conceptually split up into two pieces.  First, we do various checks on
//! elements that make sure that the set of possible rules in all selector maps
//! in the stylist (for normal styling and for pseudo-elements) that might match
//! the two elements is the same.  For example, we enforce that the target and
//! candidate must have the same localname and namespace.  Second, we have a
//! selector map of "revalidation selectors" that the stylist maintains that we
//! actually match against the target and candidate and then check whether the
//! two sets of results were the same.  Due to the up-front selector map checks,
//! we know that the target and candidate will be matched against the same exact
//! set of revalidation selectors, so the match result arrays can be compared
//! directly.
//!
//! It's very important that a selector be added to the set of revalidation
//! selectors any time there are two elements that could pass all the up-front
//! checks but match differently against some ComplexSelector in the selector.
//! If that happens, then they can have descendants that might themselves pass
//! the up-front checks but would have different matching results for the
//! selector in question.  In this case, "descendants" includes pseudo-elements,
//! so there is a single selector map of revalidation selectors that includes
//! both selectors targeting elements and selectors targeting pseudo-element
//! originating elements.  We ensure that the pseudo-element parts of all these
//! selectors are effectively stripped off, so that matching them all against
//! elements makes sense.

use crate::bloom::StyleBloom;
use crate::computed_value_flags::ComputedValueFlags;
use crate::context::{CascadeInputs, SharedStyleContext, StyleContext};
use crate::dom::{SendElement, TElement, TNode};
use crate::properties::ComputedValues;
use crate::rule_tree::StyleSource;
use crate::selector_map::RelevantAttributes;
use crate::style_resolver::{PrimaryStyle, ResolvedElementStyles};
use crate::stylist::Stylist;
use crate::values::AtomIdent;
use atomic_refcell::{AtomicRefCell, AtomicRefMut};
use selectors::matching::{NeedsSelectorFlags, SelectorCaches, VisitedHandlingMode};
use smallbitvec::SmallBitVec;
use smallvec::SmallVec;
use std::marker::PhantomData;
use std::mem;
use std::ops::Deref;
use std::ptr::NonNull;
use thin_vec::ThinVec;
use uluru::LRUCache;

mod checks;

/// The amount of nodes that the style sharing candidate cache should hold at most.
///
/// The cache size was chosen by measuring style sharing and resulting performance on a few pages;
/// sizes up to about 32 were giving good sharing improvements (e.g. 3x fewer styles having to be
/// resolved than at size 8) and slight performance improvements.  Sizes larger than 32 haven't
/// really been tested.
pub const SHARING_CACHE_SIZE: usize = 32;

/// The max amount of DOM depths we keep around to share styles across. See bug 2052731.
const SHARING_MAX_LEVELS: usize = 8;

/// Opaque pointer type to compare ComputedValues identities.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct OpaqueComputedValues(NonNull<()>);

unsafe impl Send for OpaqueComputedValues {}
unsafe impl Sync for OpaqueComputedValues {}

impl OpaqueComputedValues {
    fn from(cv: &ComputedValues) -> Self {
        let p =
            unsafe { NonNull::new_unchecked(cv as *const ComputedValues as *const () as *mut ()) };
        OpaqueComputedValues(p)
    }

    fn eq(&self, cv: &ComputedValues) -> bool {
        Self::from(cv) == *self
    }
}

/// The results from the revalidation step.
///
/// Rather than either:
///
///  * Plainly rejecting sharing for elements with different attributes (which would be unfortunate
///    because a lot of elements have different attributes yet those attributes are not
///    style-relevant).
///
///  * Having to give up on per-attribute bucketing, which would be unfortunate because it
///    increases the cost of revalidation for pages with lots of global attribute selectors (see
///    bug 1868316).
///
///  * We also store the style-relevant attributes for these elements, in order to guarantee that
///    we end up looking at the same selectors.
///
#[derive(Debug, Default)]
pub struct RevalidationResult {
    /// A bit for each selector matched. This is sound because we guarantee we look up into the
    /// same buckets via the pre-revalidation checks and relevant_attributes.
    pub selectors_matched: SmallBitVec,
    /// The set of attributes of this element that were relevant for its style.
    pub relevant_attributes: RelevantAttributes,
}

/// The results from trying to revalidate scopes this element is in.
#[derive(Debug, Default, PartialEq)]
pub struct ScopeRevalidationResult {
    /// A bit for each scope activated.
    pub scopes_matched: SmallBitVec,
}

impl PartialEq for RevalidationResult {
    fn eq(&self, other: &Self) -> bool {
        if self.relevant_attributes != other.relevant_attributes {
            return false;
        }

        // This assert "ensures", to some extent, that the two candidates have matched the
        // same rulehash buckets, and as such, that the bits we're comparing represent the
        // same set of selectors.
        debug_assert_eq!(self.selectors_matched.len(), other.selectors_matched.len());
        self.selectors_matched == other.selectors_matched
    }
}

/// We use this as an inline capacity for the class_list member below, but also as a reasonable cap
/// to avoid sorting too large class lists.
const REASONABLE_CLASS_LIST_SIZE: usize = 5;

/// Some data we want to avoid recomputing all the time while trying to share style.
#[derive(Debug, Default)]
pub struct ValidationData {
    /// The class list of this element.
    ///
    /// TODO(emilio): Maybe check whether rules for these classes apply to the element?
    /// TODO(emilio): Maybe this should be another ThinVec... But classes are definitely more common
    /// than everything else on this struct.
    class_list: Option<SmallVec<[AtomIdent; REASONABLE_CLASS_LIST_SIZE]>>,

    /// The part list of this element.
    ///
    /// TODO(emilio): Maybe check whether rules with these part names apply to
    /// the element?
    part_list: Option<ThinVec<AtomIdent>>,

    /// The list of presentational attributes of the element.
    ///
    /// We only need the declaration blocks, since all the other fields of the applicable
    /// declaration are the same for all presentational hints.
    pres_hints: Option<ThinVec<StyleSource>>,

    /// The pointer identity of the parent ComputedValues.
    parent_style_identity: Option<OpaqueComputedValues>,

    /// The cached result of matching this entry against the revalidation
    /// selectors.
    revalidation_match_results: Option<RevalidationResult>,
}

impl ValidationData {
    /// Move the cached data to a new instance, and return it.
    pub fn take(&mut self) -> Self {
        std::mem::take(self)
    }

    /// Get or compute the list of presentational attributes associated with
    /// this element.
    pub fn pres_hints<E>(&mut self, element: E) -> &[StyleSource]
    where
        E: TElement,
    {
        self.pres_hints.get_or_insert_with(|| {
            // This should basically never spill.
            let mut pres_hints = SmallVec::<[_; 5]>::new();
            element.synthesize_presentational_hints_for_legacy_attributes(
                VisitedHandlingMode::AllLinksUnvisited,
                &mut pres_hints,
            );
            ThinVec::from_iter(pres_hints.drain(..).map(|d| d.source.to_owned()))
        })
    }

    /// Get or compute the part-list associated with this element.
    pub fn part_list<E>(&mut self, element: E) -> &[AtomIdent]
    where
        E: TElement,
    {
        if !element.has_part_attr() {
            return &[];
        }
        self.part_list.get_or_insert_with(|| {
            let mut list = ThinVec::new();
            element.each_part(|p| list.push(p.clone()));
            // See below for the reasoning.
            if list.len() <= REASONABLE_CLASS_LIST_SIZE {
                list.sort_unstable_by_key(|a| a.get_hash());
            }
            list
        })
    }

    /// Get or compute the class-list associated with this element.
    pub fn class_list<E>(&mut self, element: E) -> &[AtomIdent]
    where
        E: TElement,
    {
        self.class_list.get_or_insert_with(|| {
            let mut list = SmallVec::<[_; REASONABLE_CLASS_LIST_SIZE]>::new();
            element.each_class(|c| list.push(c.clone()));
            // Assuming there are a reasonable number of classes, sort them to so that we don't
            // mistakenly reject sharing candidates when one element has "foo bar" and the other has
            // "bar foo".
            if list.len() <= REASONABLE_CLASS_LIST_SIZE {
                list.sort_unstable_by_key(|a| a.get_hash());
            }
            list
        })
    }

    /// Get or compute the parent style identity.
    pub fn parent_style_identity<E>(&mut self, el: E) -> OpaqueComputedValues
    where
        E: TElement,
    {
        self.parent_style_identity
            .get_or_insert_with(|| {
                let parent = el.inheritance_parent().unwrap();

                OpaqueComputedValues::from(parent.borrow_data().unwrap().styles.primary())
            })
            .clone()
    }

    /// Computes the revalidation results if needed, and returns it.
    /// Inline so we know at compile time what bloom_known_valid is.
    #[inline]
    fn revalidation_match_results<E>(
        &mut self,
        element: E,
        stylist: &Stylist,
        bloom: &StyleBloom<E>,
        selector_caches: &mut SelectorCaches,
        bloom_known_valid: bool,
        needs_selector_flags: NeedsSelectorFlags,
    ) -> &RevalidationResult
    where
        E: TElement,
    {
        self.revalidation_match_results.get_or_insert_with(|| {
            // The bloom filter may already be set up for our element.
            // If it is, use it.  If not, we must be in a candidate
            // (i.e. something in the cache), and the element is one
            // of our cousins, not a sibling.  In that case, we'll
            // just do revalidation selector matching without a bloom
            // filter, to avoid thrashing the filter.
            let bloom_to_use = if bloom_known_valid {
                debug_assert_eq!(bloom.current_parent(), element.traversal_parent());
                Some(bloom.filter())
            } else if bloom.current_parent() == element.traversal_parent() {
                Some(bloom.filter())
            } else {
                None
            };
            stylist.match_revalidation_selectors(
                element,
                bloom_to_use,
                selector_caches,
                needs_selector_flags,
            )
        })
    }
}

/// Information regarding a style sharing candidate, that is, an entry in the
/// style sharing cache.
///
/// Note that this information is stored in TLS and cleared after the traversal,
/// and once here, the style information of the element is immutable, so it's
/// safe to access.
#[derive(Debug)]
pub struct StyleSharingCandidate<E> {
    /// The element.
    element: E,
    validation_data: ValidationData,
    considered_nontrivial_scoped_style: bool,
}

impl<E: TElement> Deref for StyleSharingCandidate<E> {
    type Target = E;

    fn deref(&self) -> &Self::Target {
        &self.element
    }
}

impl<E: TElement> StyleSharingCandidate<E> {
    /// Get the classlist of this candidate.
    fn class_list(&mut self) -> &[AtomIdent] {
        self.validation_data.class_list(self.element)
    }

    /// Get the part list of this candidate.
    fn part_list(&mut self) -> &[AtomIdent] {
        self.validation_data.part_list(self.element)
    }

    /// Get the pres hints of this candidate.
    fn pres_hints(&mut self) -> &[StyleSource] {
        self.validation_data.pres_hints(self.element)
    }

    /// Get the parent style identity.
    fn parent_style_identity(&mut self) -> OpaqueComputedValues {
        self.validation_data.parent_style_identity(self.element)
    }

    /// Compute the bit vector of revalidation selector match results
    /// for this candidate.
    fn revalidation_match_results(
        &mut self,
        stylist: &Stylist,
        bloom: &StyleBloom<E>,
        selector_caches: &mut SelectorCaches,
    ) -> &RevalidationResult {
        self.validation_data.revalidation_match_results(
            self.element,
            stylist,
            bloom,
            selector_caches,
            /* bloom_known_valid = */ false,
            // The candidate must already have the right bits already, if
            // needed.
            NeedsSelectorFlags::No,
        )
    }

    fn scope_revalidation_results(
        &mut self,
        stylist: &Stylist,
        selector_caches: &mut SelectorCaches,
    ) -> ScopeRevalidationResult {
        stylist.revalidate_scopes(&self.element, selector_caches, NeedsSelectorFlags::No)
    }
}

impl<E: TElement> PartialEq<StyleSharingCandidate<E>> for StyleSharingCandidate<E> {
    fn eq(&self, other: &Self) -> bool {
        self.element == other.element
    }
}

/// An element we want to test against the style sharing cache.
pub struct StyleSharingTarget<E: TElement> {
    element: E,
    validation_data: ValidationData,
}

impl<E: TElement> Deref for StyleSharingTarget<E> {
    type Target = E;

    fn deref(&self) -> &Self::Target {
        &self.element
    }
}

impl<E: TElement> StyleSharingTarget<E> {
    /// Trivially construct a new StyleSharingTarget to test against the cache.
    pub fn new(element: E) -> Self {
        Self {
            element,
            validation_data: ValidationData::default(),
        }
    }

    fn class_list(&mut self) -> &[AtomIdent] {
        self.validation_data.class_list(self.element)
    }

    fn part_list(&mut self) -> &[AtomIdent] {
        self.validation_data.part_list(self.element)
    }

    /// Get the pres hints of this candidate.
    fn pres_hints(&mut self) -> &[StyleSource] {
        self.validation_data.pres_hints(self.element)
    }

    /// Get the parent style identity.
    fn parent_style_identity(&mut self) -> OpaqueComputedValues {
        self.validation_data.parent_style_identity(self.element)
    }

    fn revalidation_match_results(
        &mut self,
        stylist: &Stylist,
        bloom: &StyleBloom<E>,
        selector_caches: &mut SelectorCaches,
    ) -> &RevalidationResult {
        // It's important to set the selector flags. Otherwise, if we succeed in
        // sharing the style, we may not set the slow selector flags for the
        // right elements (which may not necessarily be |element|), causing
        // missed restyles after future DOM mutations.
        //
        // Gecko's test_bug534804.html exercises this. A minimal testcase is:
        // <style> #e:empty + span { ... } </style>
        // <span id="e">
        //   <span></span>
        // </span>
        // <span></span>
        //
        // The style sharing cache will get a hit for the second span. When the
        // child span is subsequently removed from the DOM, missing selector
        // flags would cause us to miss the restyle on the second span.
        self.validation_data.revalidation_match_results(
            self.element,
            stylist,
            bloom,
            selector_caches,
            /* bloom_known_valid = */ true,
            NeedsSelectorFlags::Yes,
        )
    }

    fn scope_revalidation_results(
        &mut self,
        stylist: &Stylist,
        selector_caches: &mut SelectorCaches,
    ) -> ScopeRevalidationResult {
        stylist.revalidate_scopes(&self.element, selector_caches, NeedsSelectorFlags::Yes)
    }

    /// Attempts to share a style with another node.
    pub fn share_style_if_possible(
        &mut self,
        context: &mut StyleContext<E>,
    ) -> Option<ResolvedElementStyles> {
        let cache = &mut context.thread_local.sharing_cache;
        let shared_context = &context.shared;
        let bloom_filter = &context.thread_local.bloom_filter;
        let selector_caches = &mut context.thread_local.selector_caches;

        debug_assert_eq!(
            bloom_filter.current_parent(),
            self.element.traversal_parent()
        );

        cache.share_style_if_possible(shared_context, bloom_filter, selector_caches, self)
    }

    /// Gets the validation data used to match against this target, if any.
    pub fn take_validation_data(&mut self) -> ValidationData {
        self.validation_data.take()
    }
}

struct SharingCacheBase<Candidate> {
    /// The DOM depth of the candidates in this cache, if any.
    dom_depth: usize,
    entries: LRUCache<Candidate, SHARING_CACHE_SIZE>,
}

impl<Candidate> Default for SharingCacheBase<Candidate> {
    fn default() -> Self {
        Self {
            dom_depth: 0,
            entries: LRUCache::default(),
        }
    }
}

impl<Candidate> SharingCacheBase<Candidate> {
    fn clear(&mut self) {
        self.entries.clear();
        self.dom_depth = 0
    }

    fn is_empty(&self) -> bool {
        self.entries.len() == 0
    }
}

impl<E: TElement> SharingCache<E> {
    fn insert(
        &mut self,
        element: E,
        validation_data_holder: Option<&mut StyleSharingTarget<E>>,
        considered_nontrivial_scoped_style: bool,
    ) {
        let validation_data = match validation_data_holder {
            Some(v) => v.take_validation_data(),
            None => ValidationData::default(),
        };
        self.entries.insert(StyleSharingCandidate {
            element,
            validation_data,
            considered_nontrivial_scoped_style,
        });
    }
}

/// Style sharing caches are are large allocations, so we store them in thread-local
/// storage such that they can be reused across style traversals. Ideally, we'd just
/// stack-allocate these buffers with uninitialized memory, but right now rustc can't
/// avoid memmoving the entire cache during setup, which gets very expensive. See
/// issues like [1] and [2].
///
/// Given that the cache stores entries of type TElement, we transmute to usize
/// before storing in TLS. This is safe as long as we make sure to empty the cache
/// before we let it go.
///
/// [1] https://github.com/rust-lang/rust/issues/42763
/// [2] https://github.com/rust-lang/rust/issues/13707
type SharingCache<E> = SharingCacheBase<StyleSharingCandidate<E>>;
type TypelessSharingCache = SharingCacheBase<StyleSharingCandidate<usize>>;

thread_local! {
    // See the comment on bloom.rs about why do we leak this.
    static SHARING_CACHE_KEY: &'static AtomicRefCell<[TypelessSharingCache; SHARING_MAX_LEVELS]> =
        Box::leak(Default::default());
}

/// A set of LRU caches of the last few nodes seen, one per DOM depth, so that we can try to
/// aggressively try to share their styles.
///
/// We key the candidates by DOM depth, because sharing requires an identical inheritance parent
/// (see `test_candidate`), which in practice means the candidates worth testing are the ones at the
/// target's own depth.
///
/// Note that these caches only live for the duration of one traversal so storing nodes here
/// temporarily is safe.
pub struct StyleSharingCache<E: TElement> {
    /// The per-depth LRU caches, with the type cast away to allow persisting the allocation across
    /// traversals.
    cache_typeless: AtomicRefMut<'static, [TypelessSharingCache; SHARING_MAX_LEVELS]>,
    /// Bind this structure to the lifetime of E, since that's what we effectively store.
    marker: PhantomData<SendElement<E>>,
}

impl<E: TElement> Drop for StyleSharingCache<E> {
    fn drop(&mut self) {
        self.clear();
    }
}

impl<E: TElement> Default for StyleSharingCache<E> {
    fn default() -> Self {
        Self::new()
    }
}

impl<E: TElement> StyleSharingCache<E> {
    fn cache_mut_at(&mut self, index: usize) -> &mut SharingCache<E> {
        let base: &mut TypelessSharingCache = &mut self.cache_typeless[index % SHARING_MAX_LEVELS];
        unsafe { mem::transmute(base) }
    }

    /// Create a new style sharing candidate cache.
    ///
    /// Forced out of line to limit stack frame sizes after extra inlining from
    /// https://github.com/rust-lang/rust/pull/43931
    ///
    /// See https://github.com/servo/servo/pull/18420#issuecomment-328769322
    #[inline(never)]
    pub fn new() -> Self {
        assert_eq!(
            mem::size_of::<SharingCache<E>>(),
            mem::size_of::<TypelessSharingCache>()
        );
        assert_eq!(
            mem::align_of::<SharingCache<E>>(),
            mem::align_of::<TypelessSharingCache>()
        );
        let cache = SHARING_CACHE_KEY.with(|c| c.borrow_mut());
        debug_assert!(cache.iter().all(|c| c.is_empty()));
        StyleSharingCache {
            cache_typeless: cache,
            marker: PhantomData,
        }
    }

    /// Tries to insert an element in the style sharing cache.
    ///
    /// Fails if we know it should never be in the cache.
    ///
    /// NB: We pass a source for the validation data, rather than the data itself,
    /// to avoid memmoving at each function call. See rust issue #42763.
    pub fn insert_if_possible(
        &mut self,
        element: &E,
        style: &PrimaryStyle,
        validation_data_holder: Option<&mut StyleSharingTarget<E>>,
        dom_depth: usize,
        shared_context: &SharedStyleContext,
    ) {
        let parent = match element.traversal_parent() {
            Some(element) => element,
            None => {
                debug!("Failing to insert to the cache: no parent element");
                return;
            },
        };

        if !element.matches_user_and_content_rules() {
            debug!("Failing to insert into the cache: no tree rules:");
            return;
        }

        if element.has_unique_presentational_hints() {
            debug!("Failing to insert into the cache: unique presentational hints");
            return;
        }

        // If the element has running animations, we can't share style.
        //
        // This is distinct from the specifies_{animations,transitions} check below,
        // because:
        //   * Animations can be triggered directly via the Web Animations API.
        //   * Our computed style can still be affected by animations after we no
        //     longer match any animation rules, since removing animations involves
        //     a sequential task and an additional traversal.
        if element.has_animations(shared_context) {
            debug!("Failing to insert to the cache: running animations");
            return;
        }

        if element.smil_override().is_some() {
            debug!("Failing to insert to the cache: SMIL");
            return;
        }

        debug!(
            "Inserting into cache: {:?} with parent {:?}",
            element, parent
        );
        debug_assert_eq!(element.as_node().depth(), dom_depth);

        let cache = self.cache_mut_at(dom_depth);
        if cache.dom_depth != dom_depth {
            debug!(
                "Clearing cache because depth changed from {:?} to {:?}, element: {:?}",
                cache.dom_depth, dom_depth, element
            );
            cache.clear();
            cache.dom_depth = dom_depth;
        }
        cache.insert(
            *element,
            validation_data_holder,
            style
                .style()
                .flags
                .intersects(ComputedValueFlags::CONSIDERED_NONTRIVIAL_SCOPED_STYLE),
        );
    }

    /// Clear the style sharing candidate cache (all depths).
    pub fn clear(&mut self) {
        for c in &mut *self.cache_typeless {
            c.clear();
        }
    }

    /// Attempts to share a style with another node.
    fn share_style_if_possible(
        &mut self,
        shared_context: &SharedStyleContext,
        bloom_filter: &StyleBloom<E>,
        selector_caches: &mut SelectorCaches,
        target: &mut StyleSharingTarget<E>,
    ) -> Option<ResolvedElementStyles> {
        if shared_context.options.disable_style_sharing_cache {
            debug!(
                "{:?} Cannot share style: style sharing cache disabled",
                target.element
            );
            return None;
        }

        if target.inheritance_parent().is_none() {
            debug!(
                "{:?} Cannot share style: element has no parent",
                target.element
            );
            return None;
        }

        if !target.matches_user_and_content_rules() {
            debug!("{:?} Cannot share style: content rules", target.element);
            return None;
        }

        if target.element.has_unique_presentational_hints() {
            debug!(
                "{:?} Cannot share style: unique presentational hints",
                target.element
            );
            return None;
        }

        let dom_depth = bloom_filter.matching_depth();
        debug_assert_eq!(target.element.as_node().depth(), dom_depth);
        let cache = self.cache_mut_at(dom_depth);
        if cache.dom_depth != dom_depth {
            debug!(
                "{:?} Cannot share style: cache holds depth {:?}, not {:?}",
                target.element, cache.dom_depth, dom_depth
            );
            return None;
        }

        cache.entries.lookup(|candidate| {
            Self::test_candidate(
                target,
                candidate,
                shared_context,
                bloom_filter,
                selector_caches,
                shared_context,
            )
        })
    }

    fn test_candidate(
        target: &mut StyleSharingTarget<E>,
        candidate: &mut StyleSharingCandidate<E>,
        shared: &SharedStyleContext,
        bloom: &StyleBloom<E>,
        selector_caches: &mut SelectorCaches,
        shared_context: &SharedStyleContext,
    ) -> Option<ResolvedElementStyles> {
        debug_assert!(target.matches_user_and_content_rules());
        debug_assert!(candidate.element.matches_user_and_content_rules());

        // Check that we have the same parent, or at least that the parents
        // share styles and permit sharing across their children. The latter
        // check allows us to share style between cousins if the parents
        // shared style.
        if !checks::parents_allow_sharing(target, candidate) {
            trace!("Miss: Parent");
            return None;
        }

        if target.local_name() != candidate.element.local_name() {
            trace!("Miss: Local Name");
            return None;
        }

        if target.namespace() != candidate.element.namespace() {
            trace!("Miss: Namespace");
            return None;
        }

        // We do not ignore visited state here, because Gecko needs to store
        // extra bits on visited styles, so these contexts cannot be shared.
        if target.element.state() != candidate.state() {
            trace!("Miss: User and Author State");
            return None;
        }

        if target.is_link() != candidate.element.is_link() {
            trace!("Miss: Link");
            return None;
        }

        // If two elements belong to different shadow trees, different rules may apply to them, from
        // the respective trees, so check their cascade data pointers.
        if !checks::shadow_root_style_data_equals(
            target.element.containing_shadow(),
            candidate.element.containing_shadow(),
        ) {
            trace!("Miss: Different containing shadow root style data");
            return None;
        }

        // Shadow hosts can share style when they have matching CascadeData pointers, which ensures
        // they match the same :host rules.
        if !checks::shadow_root_style_data_equals(
            target.element.shadow_root(),
            candidate.element.shadow_root(),
        ) {
            trace!("Miss: Different shadow root style data");
            return None;
        }

        // If the elements are not assigned to the same slot they could match
        // different ::slotted() rules in the slot scope.
        //
        // If two elements are assigned to different slots, even within the same
        // shadow root, they could match different rules, due to the slot being
        // assigned to yet another slot in another shadow root.
        if target.element.assigned_slot() != candidate.element.assigned_slot() {
            // TODO(emilio): We could have a look at whether the shadow roots
            // actually have slotted rules and such.
            trace!("Miss: Different assigned slots");
            return None;
        }

        if target.implemented_pseudo_element() != candidate.implemented_pseudo_element() {
            trace!("Miss: Element backed pseudo-element");
            return None;
        }

        if target.element.has_animations(shared_context)
            || candidate.element.has_animations(shared_context)
        {
            trace!("Miss: Has Animations");
            return None;
        }

        if target.element.smil_override().is_some() {
            trace!("Miss: SMIL");
            return None;
        }

        // It's possible that there are no styles for either id.
        if checks::may_match_different_id_rules(shared, target.element, candidate.element) {
            trace!("Miss: ID Attr");
            return None;
        }

        if !checks::have_same_style_attribute(target, candidate, shared_context) {
            trace!("Miss: Style Attr");
            return None;
        }

        if !checks::have_same_class(target, candidate) {
            trace!("Miss: Class");
            return None;
        }

        if !checks::have_same_presentational_hints(target, candidate) {
            trace!("Miss: Pres Hints");
            return None;
        }

        if !checks::have_same_parts(target, candidate) {
            trace!("Miss: Shadow parts");
            return None;
        }

        if !checks::have_same_referenced_attrs(target, candidate) {
            trace!("Miss: Attr references");
            return None;
        }

        if !checks::have_shareable_element_dependent_functions(target, candidate) {
            trace!("Miss: Element-dependent functions");
            return None;
        }

        if !checks::revalidate(target, candidate, shared, bloom, selector_caches) {
            trace!("Miss: Revalidation");
            return None;
        }

        // While the scoped style rules may be different (e.g. `@scope { .foo + .foo { /* .. */} }`),
        // we rely on revalidation to handle that.
        if candidate.considered_nontrivial_scoped_style
            && !checks::revalidate_scope(target, candidate, shared, selector_caches)
        {
            trace!("Miss: Active Scopes");
            return None;
        }

        debug!(
            "Sharing allowed between {:?} and {:?}",
            target.element, candidate.element
        );
        Some(candidate.element.borrow_data().unwrap().share_styles())
    }

    /// Attempts to find an element in the cache with the given primary rule
    /// node and parent.
    ///
    /// FIXME(emilio): re-measure this optimization, and remove if it's not very
    /// useful... It's probably not worth the complexity / obscure bugs.
    pub fn lookup_by_rules(
        &mut self,
        shared_context: &SharedStyleContext,
        inherited: &ComputedValues,
        inputs: &CascadeInputs,
        target: E,
        dom_depth: usize,
    ) -> Option<PrimaryStyle> {
        debug_assert_eq!(target.as_node().depth(), dom_depth);
        if shared_context.options.disable_style_sharing_cache {
            return None;
        }

        let cache = self.cache_mut_at(dom_depth);
        if cache.dom_depth != dom_depth {
            return None;
        }

        cache.entries.lookup(|candidate| {
            debug_assert_ne!(candidate.element, target);
            if !candidate.parent_style_identity().eq(inherited) {
                return None;
            }
            let data = candidate.element.borrow_data().unwrap();
            let style = data.styles.primary();
            if style.rules.as_ref() != Some(inputs.rules.as_ref().unwrap()) {
                return None;
            }
            if style.visited_rules() != inputs.visited_rules.as_ref() {
                return None;
            }
            let sharing_target = StyleSharingTarget::new(target);
            if !checks::have_same_referenced_attrs(&sharing_target, candidate) {
                return None;
            }
            if !checks::have_shareable_element_dependent_functions(&sharing_target, candidate) {
                return None;
            }
            // NOTE(emilio): We only need to check name / namespace because we
            // do name-dependent style adjustments, like the display: contents
            // to display: none adjustment.
            if target.namespace() != candidate.element.namespace()
                || target.local_name() != candidate.element.local_name()
            {
                return None;
            }
            // When using container units, inherited style + rules matched aren't enough to
            // determine whether the style is the same. We could actually do a full container
            // lookup but for now we just check that our actual traversal parent matches.
            if data
                .styles
                .primary()
                .flags
                .intersects(ComputedValueFlags::USES_CONTAINER_UNITS)
                && candidate.element.traversal_parent() != target.traversal_parent()
            {
                return None;
            }
            // Rule nodes and styles are computed independent of the element's actual visitedness,
            // but at the end of the cascade (in `adjust_for_visited`) we do store the
            // RELEVANT_LINK_VISITED flag, so we can't share by rule node between visited and
            // unvisited styles. We don't check for visitedness and just refuse to share for links
            // entirely, so that visitedness doesn't affect timing.
            if target.is_link() || candidate.element.is_link() {
                return None;
            }

            let target_depends_on_style_queries = inputs
                .flags
                .contains(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY);
            let candidate_depends_on_style_queries = style
                .flags
                .contains(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY);

            if target_depends_on_style_queries != candidate_depends_on_style_queries {
                // If we're considering sharing across two elements, target
                // depends on style queries and candidate doesn't, right
                // now we can share it, but by cloning the candidate style
                // if we adjust the flags.
                // If we're considering sharing across two elements, target
                // does not depend on style queries and candidate does, we
                // can share them with the same flags, but that would
                // overinvalidate if we already know we don't need to keep
                // `DEPENDS_ON_CONTAINER_STYLE_QUERY`.
                let mut new_flags = inputs.flags | style.flags;
                new_flags.set(
                    ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY,
                    target_depends_on_style_queries,
                );

                return Some(PrimaryStyle {
                    style: data.clone_style_with_flags(new_flags),
                    reused_via_rule_node: true,
                });
            }

            Some(data.share_primary_style())
        })
    }
}
