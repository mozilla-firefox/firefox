/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Per-node data used in style calculation.

use crate::computed_value_flags::ComputedValueFlags;
use crate::context::{SharedStyleContext, StackLimitChecker};
use crate::dom::TElement;
use crate::invalidation::element::invalidator::InvalidationResult;
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::properties::ComputedValues;
use crate::selector_parser::{EAGER_PSEUDO_COUNT, PseudoElement, RestyleDamage};
use crate::style_resolver::{PrimaryStyle, ResolvedElementStyles, ResolvedStyle};
use crate::traversal_flags::TraversalFlags;
use crate::values::specified::TreeCountingFunction;
#[cfg(feature = "gecko")]
use malloc_size_of::MallocSizeOfOps;
use selectors::matching::SelectorCaches;
use servo_arc::Arc;
use std::ops::{Deref, DerefMut};
use std::{fmt, mem};

#[cfg(debug_assertions)]
use atomic_refcell::{AtomicRef, AtomicRefCell, AtomicRefMut};

bitflags! {
    /// Various flags stored on ElementData.
    #[derive(Clone, Copy, Debug, Default)]
    pub struct ElementDataFlags: u8 {
        /// Whether the styles changed for this restyle.
        const WAS_RESTYLED = 1 << 0;
        /// Whether the last traversal of this element did not do
        /// any style computation. This is not true during the initial
        /// styling pass, nor is it true when we restyle (in which case
        /// WAS_RESTYLED is set).
        ///
        /// This bit always corresponds to the last time the element was
        /// traversed, so each traversal simply updates it with the appropriate
        /// value.
        const TRAVERSED_WITHOUT_STYLING = 1 << 1;

        /// Whether the primary style of this element data was reused from
        /// another element via a rule node comparison. This allows us to
        /// differentiate between elements that shared styles because they met
        /// all the criteria of the style sharing cache, compared to elements
        /// that reused style structs via rule node identity.
        ///
        /// The former gives us stronger transitive guarantees that allows us to
        /// apply the style sharing cache to cousins.
        const PRIMARY_STYLE_REUSED_VIA_RULE_NODE = 1 << 2;

        /// Whether the style of the parent of this element changed due to animations during the
        /// current traversal. If so, the before-change style of this element needs to be computed
        /// if it's restyled for other reasons, see StyleResolverForElement::before_change_style.
        const ANCESTOR_ANIMATIONS_CHANGED = 1 << 3;

        /// Whether the recascade hints of this element come only from a parent that was restyled
        /// only due to animations, so that recascading for them doesn't need to update animations
        /// or transitions.
        const RECASCADE_ONLY_FOR_ANCESTOR_ANIMATIONS = 1 << 4;
    }
}

/// A lazily-allocated list of styles for eagerly-cascaded pseudo-elements.
///
/// We use an Arc so that sharing these styles via the style sharing cache does
/// not require duplicate allocations. We leverage the copy-on-write semantics of
/// Arc::make_mut(), which is free (i.e. does not require atomic RMU operations)
/// in servo_arc.
#[derive(Clone, Debug, Default)]
pub struct EagerPseudoStyles(Option<Arc<EagerPseudoArray>>);

#[derive(Default)]
struct EagerPseudoArray(EagerPseudoArrayInner);
type EagerPseudoArrayInner = [Option<Arc<ComputedValues>>; EAGER_PSEUDO_COUNT];

impl Deref for EagerPseudoArray {
    type Target = EagerPseudoArrayInner;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for EagerPseudoArray {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

// Manually implement `Clone` here because the derived impl of `Clone` for
// array types assumes the value inside is `Copy`.
impl Clone for EagerPseudoArray {
    fn clone(&self) -> Self {
        let mut clone = Self::default();
        for i in 0..EAGER_PSEUDO_COUNT {
            clone[i] = self.0[i].clone();
        }
        clone
    }
}

// Override Debug to print which pseudos we have, and substitute the rule node
// for the much-more-verbose ComputedValues stringification.
impl fmt::Debug for EagerPseudoArray {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "EagerPseudoArray {{ ")?;
        for i in 0..EAGER_PSEUDO_COUNT {
            if let Some(ref values) = self[i] {
                write!(
                    f,
                    "{:?}: {:?}, ",
                    PseudoElement::from_eager_index(i),
                    values.rules
                )?;
            }
        }
        write!(f, "}}")
    }
}

const EMPTY_PSEUDO_ARRAY: &EagerPseudoArrayInner = &[const { None }; EAGER_PSEUDO_COUNT];

impl EagerPseudoStyles {
    /// Returns whether there are any pseudo styles.
    pub fn is_empty(&self) -> bool {
        self.0.is_none()
    }

    /// Grabs a reference to the list of styles, if they exist.
    pub fn as_optional_array(&self) -> Option<&EagerPseudoArrayInner> {
        match self.0 {
            None => None,
            Some(ref x) => Some(&x.0),
        }
    }

    /// Grabs a reference to the list of styles or a list of None if
    /// there are no styles to be had.
    pub fn as_array(&self) -> &EagerPseudoArrayInner {
        self.as_optional_array().unwrap_or(EMPTY_PSEUDO_ARRAY)
    }

    /// Returns a reference to the style for a given eager pseudo, if it exists.
    pub fn get(&self, pseudo: &PseudoElement) -> Option<&Arc<ComputedValues>> {
        debug_assert!(pseudo.is_eager());
        self.0
            .as_ref()
            .and_then(|p| p[pseudo.eager_index()].as_ref())
    }

    /// Sets the style for the eager pseudo.
    pub fn set(&mut self, pseudo: &PseudoElement, value: Arc<ComputedValues>) {
        if self.0.is_none() {
            self.0 = Some(Arc::new(Default::default()));
        }
        let arr = Arc::make_mut(self.0.as_mut().unwrap());
        arr[pseudo.eager_index()] = Some(value);
    }
}

/// The styles associated with a node, including the styles for any
/// pseudo-elements.
#[derive(Clone, Default)]
pub struct ElementStyles {
    /// The element's style.
    pub primary: Option<Arc<ComputedValues>>,
    /// A list of the styles for the element's eagerly-cascaded pseudo-elements.
    pub pseudos: EagerPseudoStyles,
}

// There's one of these per rendered elements so it better be small.
size_of_test!(ElementStyles, 16);

/// Information on how this element uses viewport units.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub enum ViewportUnitUsage {
    /// No viewport units are used.
    None = 0,
    /// There are viewport units used from regular style rules (which means we
    /// should re-cascade).
    FromDeclaration,
    /// There are viewport units used from container queries (which means we
    /// need to re-selector-match).
    FromQuery,
}

impl ElementStyles {
    /// Returns the primary style.
    pub fn get_primary(&self) -> Option<&Arc<ComputedValues>> {
        self.primary.as_ref()
    }

    /// Returns the primary style.  Panic if no style available.
    pub fn primary(&self) -> &Arc<ComputedValues> {
        self.primary.as_ref().unwrap()
    }

    /// Whether this element `display` value is `none`.
    pub fn is_display_none(&self) -> bool {
        self.primary().get_box().get_display().is_none()
    }

    /// Whether this element uses viewport units.
    pub fn viewport_unit_usage(&self) -> ViewportUnitUsage {
        fn usage_from_flags(flags: ComputedValueFlags) -> ViewportUnitUsage {
            if flags.intersects(ComputedValueFlags::USES_VIEWPORT_UNITS_ON_CONTAINER_QUERIES) {
                return ViewportUnitUsage::FromQuery;
            }
            if flags.intersects(ComputedValueFlags::USES_VIEWPORT_UNITS) {
                return ViewportUnitUsage::FromDeclaration;
            }
            ViewportUnitUsage::None
        }

        let primary = self.primary();
        let mut usage = usage_from_flags(primary.flags);

        // Check cached lazy pseudos on the primary style.
        primary.each_cached_lazy_pseudo(|style| {
            usage = std::cmp::max(usage, usage_from_flags(style.flags));
        });

        for pseudo_style in self.pseudos.as_array() {
            if let Some(pseudo_style) = pseudo_style {
                usage = std::cmp::max(usage, usage_from_flags(pseudo_style.flags));
                // Also check cached lazy pseudos on eager pseudo styles.
                pseudo_style.each_cached_lazy_pseudo(|style| {
                    usage = std::cmp::max(usage, usage_from_flags(style.flags));
                });
            }
        }

        usage
    }

    /// Whether this element uses sibling-count() or sibling-index().
    pub fn uses_tree_counting_function(&self, t: TreeCountingFunction) -> bool {
        let usage_from_flags = |flags: ComputedValueFlags| {
            if t == TreeCountingFunction::SiblingCount
                && flags.intersects(ComputedValueFlags::USES_SIBLING_COUNT)
            {
                return true;
            }
            if t == TreeCountingFunction::SiblingIndex
                && flags.intersects(ComputedValueFlags::USES_SIBLING_INDEX)
            {
                return true;
            }
            false
        };

        if usage_from_flags(self.primary().flags) {
            return true;
        }

        for pseudo_style in self.pseudos.as_array().iter().flatten() {
            if usage_from_flags(pseudo_style.flags) {
                return true;
            }
        }

        false
    }

    /// Whether this element's styles use an element-scoped `random()`.
    pub fn uses_element_scoped_random(&self) -> bool {
        let usage_from_flags = |flags: ComputedValueFlags| {
            flags.intersects(ComputedValueFlags::USES_ELEMENT_SCOPED_RANDOM)
        };

        if usage_from_flags(self.primary().flags) {
            return true;
        }

        for pseudo_style in self.pseudos.as_array().iter().flatten() {
            if usage_from_flags(pseudo_style.flags) {
                return true;
            }
        }

        false
    }

    #[cfg(feature = "gecko")]
    fn size_of_excluding_cvs(&self, _ops: &mut MallocSizeOfOps) -> usize {
        // As the method name suggests, we don't measures the ComputedValues
        // here, because they are measured on the C++ side.

        // XXX: measure the EagerPseudoArray itself, but not the ComputedValues
        // within it.

        0
    }
}

// We manually implement Debug for ElementStyles so that we can avoid the
// verbose stringification of every property in the ComputedValues. We
// substitute the rule node instead.
impl fmt::Debug for ElementStyles {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "ElementStyles {{ primary: {:?}, pseudos: {:?} }}",
            self.primary.as_ref().map(|x| &x.rules),
            self.pseudos
        )
    }
}

/// Style system data associated with an Element.
///
/// In Gecko, this hangs directly off the Element. Servo, this is embedded
/// inside of layout data, which itself hangs directly off the Element. In
/// both cases, it is wrapped inside an AtomicRefCell to ensure thread safety.
#[derive(Default)]
pub struct ElementData {
    /// The styles for the element and its pseudo-elements.
    pub styles: ElementStyles,

    /// The restyle damage, indicating what kind of layout changes are required
    /// afte restyling.
    pub damage: RestyleDamage,

    /// The restyle hint, which indicates whether selectors need to be rematched
    /// for this element, its children, and its descendants.
    pub hint: RestyleHint,

    /// Flags.
    pub flags: ElementDataFlags,

    /// The before-change style of this element for transitions, if the element was restyled during
    /// the current traversal, isn't restyled only due to animations, and its children are going to
    /// be traversed too. See StyleResolverForElement::before_change_style.
    pub before_change_style: Option<Arc<ComputedValues>>,
}

impl fmt::Debug for ElementData {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut f = f.debug_struct("ElementData");
        f.field("styles", &self.styles)
            .field("damage", &self.damage)
            .field("hint", &self.hint)
            .field("flags", &self.flags)
            .field(
                "before_change_style",
                &self.before_change_style.as_ref().map(|s| &s.rules),
            )
            .finish()
    }
}

/// A struct that wraps ElementData, giving it the ability of doing thread-safety checks.
#[derive(Debug, Default)]
pub struct ElementDataWrapper {
    inner: std::cell::UnsafeCell<ElementData>,
    /// Implements optional (debug_assertions-only) thread-safety checking.
    #[cfg(debug_assertions)]
    refcell: AtomicRefCell<()>,
}

/// A read-only reference to ElementData.
#[derive(Debug)]
pub struct ElementDataMut<'a> {
    v: &'a mut ElementData,
    #[cfg(debug_assertions)]
    _borrow: AtomicRefMut<'a, ()>,
}

/// A mutable reference to ElementData.
#[derive(Debug)]
pub struct ElementDataRef<'a> {
    v: &'a ElementData,
    #[cfg(debug_assertions)]
    _borrow: AtomicRef<'a, ()>,
}

impl ElementDataWrapper {
    /// Gets a non-exclusive reference to this ElementData.
    #[inline(always)]
    pub fn borrow(&self) -> ElementDataRef<'_> {
        #[cfg(debug_assertions)]
        let borrow = self.refcell.borrow();
        ElementDataRef {
            v: unsafe { &*self.inner.get() },
            #[cfg(debug_assertions)]
            _borrow: borrow,
        }
    }

    /// Gets an exclusive reference to this ElementData.
    #[inline(always)]
    pub fn borrow_mut(&self) -> ElementDataMut<'_> {
        #[cfg(debug_assertions)]
        let borrow = self.refcell.borrow_mut();
        ElementDataMut {
            v: unsafe { &mut *self.inner.get() },
            #[cfg(debug_assertions)]
            _borrow: borrow,
        }
    }
}

impl<'a> Deref for ElementDataRef<'a> {
    type Target = ElementData;
    #[inline]
    fn deref(&self) -> &Self::Target {
        self.v
    }
}

impl<'a> Deref for ElementDataMut<'a> {
    type Target = ElementData;
    #[inline]
    fn deref(&self) -> &Self::Target {
        &*self.v
    }
}

impl<'a> DerefMut for ElementDataMut<'a> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut *self.v
    }
}

// There's one of these per rendered elements so it better be small.
size_of_test!(ElementData, 32);

/// The kind of restyle that a single element should do.
#[derive(Debug)]
pub enum RestyleKind {
    /// We need to run selector matching plus re-cascade, that is, a full
    /// restyle.
    MatchAndCascade,
    /// We need to recascade with some replacement rule, such as the style
    /// attribute, or animation rules.
    CascadeWithReplacements(RestyleHint),
    /// We only need to recascade, for example, because only inherited
    /// properties in the parent changed.
    CascadeOnly,
}

fn needs_to_match_self(hint: RestyleHint, style: &ComputedValues) -> bool {
    if hint.intersects(RestyleHint::RESTYLE_SELF) {
        return true;
    }
    if hint.intersects(RestyleHint::RESTYLE_SELF_IF_PSEUDO) && style.is_pseudo_style() {
        return true;
    }
    if hint.intersects(RestyleHint::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT)
        && style
            .flags
            .intersects(ComputedValueFlags::USES_FONT_OR_WM_RELATIVE_UNITS_ON_CONTAINER_QUERIES)
    {
        return true;
    }
    hint.intersects(
        RestyleHint::RESTYLE_IF_AFFECTED_BY_STYLE_QUERIES
            | RestyleHint::RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER,
    ) && style
        .flags
        .intersects(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY)
}

fn needs_to_recascade_self(hint: RestyleHint, style: &ComputedValues) -> bool {
    if hint.intersects(RestyleHint::RECASCADE_SELF) {
        return true;
    }
    if hint.intersects(RestyleHint::RECASCADE_SELF_IF_INHERIT_RESET_STYLE)
        && style
            .flags
            .contains(ComputedValueFlags::INHERITS_RESET_STYLE)
    {
        return true;
    }
    if hint.intersects(RestyleHint::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT)
        && style
            .flags
            .contains(ComputedValueFlags::USES_FONT_OR_WM_RELATIVE_UNITS)
    {
        return true;
    }
    false
}

impl ElementData {
    /// Invalidates style for this element, its descendants, and later siblings,
    /// based on the snapshot of the element that we took when attributes or
    /// state changed.
    pub fn invalidate_style_if_needed<E: TElement>(
        &mut self,
        element: E,
        shared_context: &SharedStyleContext,
        stack_limit_checker: Option<&StackLimitChecker>,
        selector_caches: &mut SelectorCaches,
    ) -> InvalidationResult {
        if shared_context
            .traversal_flags
            .contains(TraversalFlags::Forgetful)
        {
            return InvalidationResult::empty();
        }

        use crate::invalidation::element::invalidator::TreeStyleInvalidator;
        use crate::invalidation::element::state_and_attributes::StateAndAttrInvalidationProcessor;

        debug!(
            "invalidate_style_if_needed: {:?}, flags: {:?}, has_snapshot: {}, \
             handled_snapshot: {}, pseudo: {:?}",
            element,
            shared_context.traversal_flags,
            element.has_snapshot(),
            element.handled_snapshot(),
            element.implemented_pseudo_element()
        );

        if !element.has_snapshot() || element.handled_snapshot() {
            return InvalidationResult::empty();
        }

        let mut processor =
            StateAndAttrInvalidationProcessor::new(shared_context, element, self, selector_caches);

        let invalidator = TreeStyleInvalidator::new(element, stack_limit_checker, &mut processor);

        let result = invalidator.invalidate();

        unsafe { element.set_handled_snapshot() }
        debug_assert!(element.handled_snapshot());

        result
    }

    /// Returns true if this element has styles.
    #[inline]
    pub fn has_styles(&self) -> bool {
        self.styles.primary.is_some()
    }

    /// Returns this element's styles as resolved styles to use for sharing.
    pub fn share_styles(&self) -> ResolvedElementStyles {
        ResolvedElementStyles {
            primary: self.share_primary_style(),
            pseudos: self.styles.pseudos.clone(),
        }
    }

    /// Returns this element's primary style as a resolved style to use for sharing.
    pub fn share_primary_style(&self) -> PrimaryStyle {
        let reused_via_rule_node = self
            .flags
            .contains(ElementDataFlags::PRIMARY_STYLE_REUSED_VIA_RULE_NODE);

        PrimaryStyle {
            style: ResolvedStyle(self.styles.primary().clone()),
            reused_via_rule_node,
        }
    }

    /// Return a copy of the element's primary style as a resolved style with the
    /// given flags.
    pub fn clone_style_with_flags(&self, flags: ComputedValueFlags) -> ResolvedStyle {
        let primary_style = self.styles.primary();
        // We are only using this pseudo to find the correct pseudo type so it
        // does not matter it technically belongs to a different style.
        let pseudo = primary_style.pseudo();
        ResolvedStyle(
            primary_style
                .deref()
                .clone_with_flags(flags, pseudo.as_ref()),
        )
    }

    /// Sets a new set of styles, returning the old ones.
    pub fn set_styles(&mut self, new_styles: ResolvedElementStyles) -> ElementStyles {
        self.flags.set(
            ElementDataFlags::PRIMARY_STYLE_REUSED_VIA_RULE_NODE,
            new_styles.primary.reused_via_rule_node,
        );
        mem::replace(&mut self.styles, new_styles.into())
    }

    /// Returns the kind of restyling that we're going to need to do on this
    /// element, based of the stored restyle hint.
    pub fn restyle_kind(&self) -> Option<RestyleKind> {
        self.restyle_kind_for_hint(self.hint)
    }

    /// Returns whether the element needs to be restyled only due to animation changes, either on
    /// itself or on an ancestor, and thus doesn't need to update animations or transitions.
    pub fn is_animation_only_restyle(&self) -> bool {
        let mut animation_hints = RestyleHint::for_animations();
        if self
            .flags
            .contains(ElementDataFlags::RECASCADE_ONLY_FOR_ANCESTOR_ANIMATIONS)
        {
            animation_hints |= RestyleHint::recascade_self();
        }
        self.has_styles()
            && self.hint.intersects(animation_hints)
            && self
                .restyle_kind_for_hint(self.hint - animation_hints)
                .is_none()
    }

    fn restyle_kind_for_hint(&self, hint: RestyleHint) -> Option<RestyleKind> {
        let style = match self.styles.primary {
            Some(ref s) => s,
            None => return Some(RestyleKind::MatchAndCascade),
        };

        if hint.is_empty() {
            return None;
        }

        if needs_to_match_self(hint, style) {
            return Some(RestyleKind::MatchAndCascade);
        }

        if hint.has_replacements() {
            return Some(RestyleKind::CascadeWithReplacements(
                hint & RestyleHint::replacements(),
            ));
        }

        if needs_to_recascade_self(hint, style) {
            return Some(RestyleKind::CascadeOnly);
        }

        None
    }

    /// Drops any restyle state from the element.
    ///
    /// FIXME(bholley): The only caller of this should probably just assert that the hint is empty
    /// and call clear_flags_and_damage().
    #[inline]
    pub fn clear_restyle_state(&mut self) {
        self.hint = RestyleHint::empty();
        self.clear_restyle_flags_and_damage();
    }

    /// Drops restyle flags and damage from the element.
    #[inline]
    pub fn clear_restyle_flags_and_damage(&mut self) {
        self.damage = RestyleDamage::empty();
        self.flags.remove(ElementDataFlags::WAS_RESTYLED);
        self.before_change_style = None;
    }

    /// Mark this element as restyled, which is useful to know whether we need
    /// to do a post-traversal.
    pub fn set_restyled(&mut self) {
        self.flags.insert(ElementDataFlags::WAS_RESTYLED);
        self.flags
            .remove(ElementDataFlags::TRAVERSED_WITHOUT_STYLING);
    }

    /// Returns true if this element was restyled.
    #[inline]
    pub fn is_restyle(&self) -> bool {
        self.flags.contains(ElementDataFlags::WAS_RESTYLED)
    }

    /// Mark that we traversed this element without computing any style for it.
    pub fn set_traversed_without_styling(&mut self) {
        self.flags
            .insert(ElementDataFlags::TRAVERSED_WITHOUT_STYLING);
    }

    /// Returns whether this element has been part of a restyle.
    #[inline]
    pub fn contains_restyle_data(&self) -> bool {
        self.is_restyle() || !self.hint.is_empty() || !self.damage.is_empty()
    }

    /// Returns whether it is safe to perform cousin sharing based on the ComputedValues
    /// identity of the primary style in this ElementData. There are a few subtle things
    /// to check.
    ///
    /// First, if a parent element was already styled and we traversed past it without
    /// restyling it, that may be because our clever invalidation logic was able to prove
    /// that the styles of that element would remain unchanged despite changes to the id
    /// or class attributes. However, style sharing relies on the strong guarantee that all
    /// the classes and ids up the respective parent chains are identical. As such, if we
    /// skipped styling for one (or both) of the parents on this traversal, we can't share
    /// styles across cousins. Note that this is a somewhat conservative check. We could
    /// tighten it by having the invalidation logic explicitly flag elements for which it
    /// ellided styling.
    ///
    /// Second, we want to only consider elements whose ComputedValues match due to a hit
    /// in the style sharing cache, rather than due to the rule-node-based reuse that
    /// happens later in the styling pipeline. The former gives us the stronger guarantees
    /// we need for style sharing, the latter does not.
    pub fn safe_for_cousin_sharing(&self) -> bool {
        if self.flags.intersects(
            ElementDataFlags::TRAVERSED_WITHOUT_STYLING
                | ElementDataFlags::PRIMARY_STYLE_REUSED_VIA_RULE_NODE,
        ) {
            return false;
        }
        if !self
            .styles
            .primary()
            .get_box()
            .get_container_type()
            .is_normal()
        {
            return false;
        }
        true
    }

    /// Measures memory usage.
    #[cfg(feature = "gecko")]
    pub fn size_of_excluding_cvs(&self, ops: &mut MallocSizeOfOps) -> usize {
        let n = self.styles.size_of_excluding_cvs(ops);

        // We may measure more fields in the future if DMD says it's worth it.

        n
    }
}
