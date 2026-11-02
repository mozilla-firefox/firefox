/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Invalidation of element styles relative selectors.

use crate::FxHashMap;
use crate::data::ElementData;
use crate::dom::{TElement, TNode};
use crate::invalidation::element::element_wrapper::{ElementWrapper, Snapshots};
use crate::invalidation::element::invalidation_map::{
    AdditionalRelativeSelectorInvalidationMap, Dependency, DependencyInvalidationKind,
    InvalidationMap, NormalDependencyInvalidationKind, RelativeDependencyInvalidationKind,
    ScopeDependencyInvalidationKind, TSStateForInvalidation,
};
use crate::invalidation::element::invalidator::{
    DescendantInvalidationLists, Invalidation, InvalidationProcessor, InvalidationResult,
    InvalidationVector, SiblingTraversalMap, TreeStyleInvalidator,
    note_scope_dependency_force_at_subject,
};
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::invalidation::element::state_and_attributes::{
    check_dependency, dependency_may_be_relevant, invalidated_descendants, invalidated_self,
    invalidated_sibling, push_invalidation, should_process_descendants,
};
use crate::stylist::{CascadeData, Stylist};
use dom::ElementState;
use selectors::OpaqueElement;
use selectors::kleene_value::KleeneValue;
use selectors::matching::{
    ElementSelectorFlags, MatchingContext, MatchingForInvalidation, MatchingMode,
    NeedsSelectorFlags, QuirksMode, SelectorCaches, SubjectOrPseudoElement, VisitedHandlingMode,
    early_reject_by_local_name, matches_compound_selector, matches_selector,
};
use selectors::parser::SelectorKey;
use smallvec::{SmallVec, smallvec};
use std::ops::DerefMut;

/// Kind of DOM mutation this relative selector invalidation is being carried out in.
#[derive(Clone, Copy)]
pub enum DomMutationOperation {
    /// Insertion operation, can cause side effect, but presumed already happened.
    Insert,
    /// Append operation, cannot cause side effect.
    Append,
    /// Removal operation, can cause side effect, but presumed already happened. Sibling relationships are destroyed.
    Remove,
    /// Invalidating for side effect of a DOM operation, for the previous sibling.
    SideEffectPrevSibling,
    /// Invalidating for side effect of a DOM operation, for the next sibling.
    SideEffectNextSibling,
}

impl DomMutationOperation {
    fn accept<E: TElement>(&self, d: &Dependency, e: E) -> bool {
        match self {
            Self::Insert | Self::Append | Self::Remove => {
                !e.relative_selector_search_direction().is_empty()
            },
            // `:has(+ .a + .b)` with `.anchor + .a + .remove + .b` - `.a` would be present
            // in the search path.
            Self::SideEffectPrevSibling => {
                !e.relative_selector_search_direction().is_empty()
                    && d.right_combinator_is_next_sibling()
            },
            // If an element is being removed and would cause next-sibling match to happen,
            // e.g. `:has(+ .a)` with `.anchor + .remove + .a`, `.a` isn't yet searched
            // for relative selector matching.
            Self::SideEffectNextSibling => d.dependency_is_relative_with_single_next_sibling(),
        }
    }

    fn is_side_effect(&self) -> bool {
        match self {
            Self::Insert | Self::Append | Self::Remove => false,
            Self::SideEffectPrevSibling | Self::SideEffectNextSibling => true,
        }
    }
}

/// Context required to try and optimize away relative dependencies.
struct OptimizationContext<'a, E: TElement> {
    sibling_traversal_map: &'a SiblingTraversalMap<E>,
    operation: DomMutationOperation,
}

impl<'a, E: TElement> OptimizationContext<'a, E> {
    fn can_be_ignored(
        &self,
        quirks_mode: QuirksMode,
        is_subtree: bool,
        element: E,
        host: Option<OpaqueElement>,
        dependency: &Dependency,
        leftmost_collapse_offset: usize,
    ) -> bool {
        if is_subtree {
            // Subtree elements don't have unaffected sibling to look at.
            return false;
        }
        debug_assert!(
            matches!(
                dependency.invalidation_kind(),
                DependencyInvalidationKind::Relative(..)
            ),
            "Non-relative selector being evaluated for optimization"
        );
        // This optimization predecates on the fact that there may be a sibling that can readily
        // "take over" this element.
        let sibling = match self.sibling_traversal_map.prev_sibling_for(&element) {
            None => {
                if matches!(self.operation, DomMutationOperation::Append) {
                    return false;
                }
                match self.sibling_traversal_map.next_sibling_for(&element) {
                    Some(s) => s,
                    None => return false,
                }
            },
            Some(s) => s,
        };
        {
            // Run through the affected compund.
            let iter = dependency.selector.iter_from(dependency.selector_offset);
            for c in iter {
                if c.has_indexed_selector_in_subject() {
                    // We do not calculate indices during invalidation as they're wasteful - as a side effect,
                    // such selectors always return true, breaking this optimization. Note that we only check
                    // this compound only because the check to skip compares against this element's sibling.
                    // i.e. Given `:has(:nth-child(2) .foo)`, we'd try to find `.foo`'s sibling, which
                    // shares `:nth-child` up the selector.
                    return false;
                }
            }
        }
        let dependency_is_rightmost = dependency.selector_offset == 0;
        if !dependency_is_rightmost {
            let combinator = dependency
                .selector
                .combinator_at_match_order(dependency.selector_offset - 1);
            if combinator.is_ancestor() {
                // We can safely ignore these, since we're about to traverse the
                // rest of the affected tree anyway to find the rightmost invalidated element.
                return true;
            }
            if combinator.is_sibling() && matches!(self.operation, DomMutationOperation::Append) {
                // If we're at the top of the DOM tree being mutated, we can ignore it if the
                // operation is append - we know we'll cover all the later siblings and their descendants.
                return true;
            }
        }

        // We have a situation like `:has(.item .item + .item + .item)`, where the first element in the sibling
        // chain position (i.e. The element matched by the second `.item` from the left) mutates. By the time we
        // get here, we've collapsed the 4 dependencies for each of `.item` position into one at the rightmost
        // position. Before we look for a standin, we need to find which `.item` this element matches - Doing
        // that would generate more work than it saves.
        if dependency_is_rightmost
            && leftmost_collapse_offset != dependency.selector_offset
            && self
                .sibling_traversal_map
                .next_sibling_for(&element)
                .is_some()
        {
            return false;
        }

        let mut caches = SelectorCaches::default();
        let mut matching_context = MatchingContext::new(
            MatchingMode::Normal,
            None,
            &mut caches,
            quirks_mode,
            NeedsSelectorFlags::No,
            MatchingForInvalidation::Yes,
        );
        matching_context.current_host = host;
        let sibling_matches = matches_selector(
            &dependency.selector,
            dependency.selector_offset,
            None,
            sibling,
            &mut matching_context,
        );
        if sibling_matches {
            // Remember that at this point, we know that the combinator to the right of this
            // compound is a sibling combinator. Effectively, we've found a standin for the
            // element we're mutating.
            // e.g. Given `:has(... .a ~ .b ...)`, we're the mutating element matching `... .a`,
            // if we find a sibling that matches the `... .a`, it can stand in for us.
            debug_assert!(
                dependency.next.is_some(),
                "No relative selector outer dependency?"
            );
            return dependency.next.as_ref().is_some_and(|deps| {
                // ... However, if the standin sibling can be the anchor, we can't skip it, since
                // that sibling should be invlidated to become the anchor.
                let next = &deps.as_ref().slice()[0];
                !matches_selector(
                    &next.selector,
                    next.selector_offset,
                    None,
                    sibling,
                    &mut matching_context,
                )
            });
        }
        // Ok, there's no standin element - but would this element have matched the upstream
        // selector anyway? If we don't, either the match exists somewhere far from us
        // (In which case our mutation doesn't really matter), or it doesn't exist at all,
        // so we can just skip the invalidation.
        let (combinator, prev_offset) = {
            let mut iter = dependency.selector.iter_from(dependency.selector_offset);
            let mut o = dependency.selector_offset;
            while iter.next().is_some() {
                o += 1;
            }
            let combinator = iter.next_sequence();
            o += 1;
            debug_assert!(
                combinator.is_some(),
                "Should at least see a relative combinator"
            );
            (combinator.unwrap(), o)
        };
        if combinator.is_sibling() && prev_offset >= dependency.selector.len() - 1 {
            // Hit the relative combinator - we don't have enough information to
            // see if there's going to be a downstream match.
            return false;
        }
        !matches_selector(
            &dependency.selector,
            dependency.selector_offset,
            None,
            element,
            &mut matching_context,
        )
    }
}

/// Overall invalidator for handling relative selector invalidations.
pub struct RelativeSelectorInvalidator<'a, 'b: 'a, E>
where
    E: TElement,
{
    /// Element triggering the invalidation.
    pub element: E,
    /// Quirks mode of the current invalidation.
    pub quirks_mode: QuirksMode,
    /// Snapshot containing changes to invalidate against.
    /// Can be None if it's a DOM mutation.
    pub snapshots: Option<&'a Snapshots<'b>>,
    /// Callback to trigger when the subject element is invalidated.
    pub invalidated: fn(E, &InvalidationResult),
    /// The traversal map that should be used to process invalidations.
    pub sibling_traversal_map: SiblingTraversalMap<E>,
}

struct RelativeSelectorInvalidation<'a> {
    host: Option<OpaqueElement>,
    kind: RelativeDependencyInvalidationKind,
    dependency: &'a Dependency,
}

type ElementDependencies<'a> = SmallVec<[(Option<OpaqueElement>, &'a Dependency); 1]>;
type Dependencies<'a, E> = SmallVec<[(E, ElementDependencies<'a>); 1]>;
type AlreadyInvalidated<'a, E> = SmallVec<[AlreadyInvalidatedEntry<'a, E>; 2]>;

struct AlreadyInvalidatedEntry<'a, E>
where
    E: TElement + 'a,
{
    /// Element where the invalidation will begin.
    element: E,
    /// The current shadow host.
    host: Option<OpaqueElement>,
    /// Dependency chain for this invalidation.
    dependency: &'a Dependency,
    /// The offset, of the leftmost dependencies that this
    /// invalidation collapsed. See the `update()` function
    /// for more information.
    leftmost_collapse_offset: usize,
}

impl<'a, E> AlreadyInvalidatedEntry<'a, E>
where
    E: TElement + 'a,
{
    fn new(element: E, host: Option<OpaqueElement>, dependency: &'a Dependency) -> Self {
        Self {
            element,
            host,
            dependency,
            leftmost_collapse_offset: dependency.selector_offset,
        }
    }

    /// Update this invalidation with a new invalidation that may collapse with it.
    fn update(&mut self, element: E, host: Option<OpaqueElement>, dependency: &'a Dependency) {
        // This dependency should invalidate the same way - Collapse the invalidation
        // to a more general case so we don't do duplicate work.
        // e.g. For `:has(.item .item + .item + .item)`, since the anchor would be located
        // in the ancestor chain for any invalidation triggered by any `.item` compound,
        // 4 entries can collapse into one - but keep track of the leftmost offset.
        if self.dependency.selector_offset > dependency.selector_offset {
            *self = Self {
                element,
                host,
                dependency,
                leftmost_collapse_offset: self.leftmost_collapse_offset,
            };
        } else if self.leftmost_collapse_offset < dependency.selector_offset {
            self.leftmost_collapse_offset = dependency.selector_offset;
        }
    }
}

/// Interface for collecting relative selector dependencies.
pub struct RelativeSelectorDependencyCollector<'a, 's, E>
where
    E: TElement,
{
    /// Dependencies that need to run through the normal invalidation that may generate
    /// a relative selector invalidation.
    dependencies: FxHashMap<E, ElementDependencies<'a>>,
    /// Dependencies that created an invalidation right away.
    invalidations: AlreadyInvalidated<'a, E>,
    /// The top element in the subtree being invalidated.
    top: E,
    /// Optional context that will be used to try and skip invalidations
    /// by running selector matches.
    optimization_context: Option<OptimizationContext<'a, E>>,
    quirks_mode: QuirksMode,
    /// Snapshots map, if not in the context of DOM mutation.
    snapshots: Option<&'a Snapshots<'s>>,
}

type Invalidations<'a> = SmallVec<[RelativeSelectorInvalidation<'a>; 1]>;
type InnerInvalidations<'a, E> = SmallVec<[(E, RelativeSelectorInvalidation<'a>); 1]>;

struct ToInvalidate<'a, E: TElement + 'a> {
    /// Dependencies to run through normal invalidator.
    dependencies: Dependencies<'a, E>,
    /// Dependencies already invalidated.
    invalidations: Invalidations<'a>,
}

impl<'a, E: TElement + 'a> Default for ToInvalidate<'a, E> {
    fn default() -> Self {
        Self {
            dependencies: Dependencies::default(),
            invalidations: Invalidations::default(),
        }
    }
}

fn invalidation_can_collapse(
    a: &Dependency,
    b: &Dependency,
    allow_indexed_selectors: bool,
) -> bool {
    // We want to detect identical dependencies that occur at different
    // compounds but has the identical compound in the same selector,
    // e.g. :has(.item .item).

    // If they trigger different invalidations, they shouldn't be collapsed.
    if a.relative_invalidation_kind() != b.relative_invalidation_kind() {
        return false;
    }

    // Not in the same selector, trivially skipped.
    if SelectorKey::new(&a.selector) != SelectorKey::new(&b.selector) {
        return false;
    }

    // Check that this is in the same nesting.
    // TODO(dshin): @scope probably brings more subtleties...
    let mut a_next = a.next.as_ref();
    let mut b_next = b.next.as_ref();
    while let (Some(a_deps), Some(b_deps)) = (a_next, b_next) {
        // This is a bit subtle - but we don't need to do the checks we do at higher levels.
        // Cases like `:is(.item .foo) :is(.item .foo)` where `.item` invalidates would
        // point to different dependencies, pointing to the same outer selector, but
        // differing in selector offset.
        let a_nexts = a_deps.as_ref().slice();
        let b_nexts = b_deps.as_ref().slice();
        if a_nexts.is_empty() || b_nexts.is_empty() {
            // Can happen when we get out to empty @scope() rules.
            // If they're both empty, we can just do nothing for both.
            return a_nexts.is_empty() == b_nexts.is_empty();
        }
        let a_n = &a_nexts[0];
        let b_n = &b_nexts[0];
        if SelectorKey::new(&a_n.selector) != SelectorKey::new(&b_n.selector) {
            return false;
        }
        a_next = a_n.next.as_ref();
        b_next = b_n.next.as_ref();
    }
    if a_next.is_some() || b_next.is_some() {
        return false;
    }

    // Ok, now, do the compounds actually match?
    // This can get expensive quickly, but we're assuming that:
    //
    //   * In most cases, authors don't generally duplicate compounds in a selector, so
    //     this fails quickly
    //   * In cases where compounds are duplicated, reducing the number of invalidations
    //     has a payoff that offsets the comparison cost
    //
    // Note, `.a.b` != `.b.a` - doesn't affect correctness, though.
    // TODO(dshin): Caching this may be worth it as well?
    let mut a_iter = a.selector.iter_from(a.selector_offset);
    let mut b_iter = b.selector.iter_from(b.selector_offset);
    loop {
        let a_component = a_iter.next();
        let b_component = b_iter.next();

        if a_component != b_component {
            return false;
        }
        let Some(component) = a_component else {
            return true;
        };
        if !allow_indexed_selectors && component.has_indexed_selector_in_subject() {
            // The element's positioning matters, so can't collapse.
            return false;
        }
    }
}

impl<'a, 's, E> RelativeSelectorDependencyCollector<'a, 's, E>
where
    E: TElement,
{
    fn new(
        top: E,
        quirks_mode: QuirksMode,
        optimization_context: Option<OptimizationContext<'a, E>>,
        snapshots: Option<&'a Snapshots<'s>>,
    ) -> Self {
        Self {
            dependencies: FxHashMap::default(),
            invalidations: AlreadyInvalidated::default(),
            top,
            quirks_mode,
            optimization_context,
            snapshots,
        }
    }

    fn insert_invalidation(
        &mut self,
        element: E,
        dependency: &'a Dependency,
        host: Option<OpaqueElement>,
    ) {
        let in_subtree = element != self.top;
        if let Some(entry) = self.invalidations.iter_mut().find(|entry| {
            // If we're in the subtree of DOM manipulation - worrying the about positioning of this element
            // is irrelevant, because the DOM structure is either completely new or about to go away.
            let both_in_subtree = in_subtree && entry.element != self.top;
            // If we're considering the same element for invalidation, their evaluation of the indexed selector
            // is identical by definition.
            let same_element = element == entry.element;
            invalidation_can_collapse(
                dependency,
                entry.dependency,
                both_in_subtree || same_element,
            )
        }) {
            entry.update(element, host, dependency)
        } else {
            self.invalidations
                .push(AlreadyInvalidatedEntry::new(element, host, dependency));
        }
    }

    /// Add this dependency, if it is unique (i.e. Different outer dependency or same outer dependency
    /// but requires a different invalidation traversal).
    pub fn add_dependency(
        &mut self,
        dependency: &'a Dependency,
        element: E,
        host: Option<OpaqueElement>,
    ) {
        if let Some(snapshots) = self.snapshots
            && !Self::compound_may_be_different(
                self.quirks_mode,
                dependency,
                element,
                host,
                snapshots,
            )
        {
            // Nothing in the compound changed, we can safely ignore.
            return;
        }
        match dependency.invalidation_kind() {
            DependencyInvalidationKind::FullSelector => unreachable!(),
            DependencyInvalidationKind::Normal(..) | DependencyInvalidationKind::Scope(..) => {
                self.dependencies
                    .entry(element)
                    .or_default()
                    .push((host, dependency));
            },
            DependencyInvalidationKind::Relative(kind) => {
                debug_assert!(
                    dependency.next.is_some(),
                    "Orphaned inner relative selector?"
                );
                if element != self.top
                    && matches!(
                        kind,
                        RelativeDependencyInvalidationKind::Parent
                            | RelativeDependencyInvalidationKind::PrevSibling
                            | RelativeDependencyInvalidationKind::EarlierSibling
                    )
                {
                    return;
                }
                if early_reject_by_local_name(
                    &dependency.selector,
                    dependency.selector_offset,
                    element,
                ) {
                    return;
                }
                self.insert_invalidation(element, dependency, host);
            },
        };
    }

    /// Check for a quick exit for the invalidation initiating element. Dependencies
    /// up the chain are checked by the invalidator.
    fn compound_may_be_different(
        quirks_mode: QuirksMode,
        dependency: &'a Dependency,
        element: E,
        host: Option<OpaqueElement>,
        snapshots: &Snapshots<'s>,
    ) -> bool {
        debug_assert!(element.has_snapshot());
        let mut selector_caches = SelectorCaches::default();
        let mut matching_context = MatchingContext::<'_, E::Impl>::new_for_visited(
            MatchingMode::Normal,
            None,
            &mut selector_caches,
            VisitedHandlingMode::AllLinksVisitedAndUnvisited,
            quirks_mode,
            NeedsSelectorFlags::No,
            MatchingForInvalidation::YesForComparison,
        );
        matching_context.current_host = host;
        // Pseudos are disallowed in :has, so this should work.
        let is_rightmost = if dependency.selector_offset == 0 {
            SubjectOrPseudoElement::Yes
        } else {
            SubjectOrPseudoElement::No
        };
        let matches_now = matches_compound_selector(
            &mut dependency.selector.iter_from(dependency.selector_offset),
            element,
            &mut matching_context,
            is_rightmost,
        );

        let wrapper = ElementWrapper::new(element, snapshots);
        let matched_then = matches_compound_selector(
            &mut dependency.selector.iter_from(dependency.selector_offset),
            wrapper,
            &mut matching_context,
            is_rightmost,
        );
        matched_then != matches_now || matches_now == KleeneValue::Unknown
    }

    /// Get the dependencies in a list format.
    fn get(self) -> ToInvalidate<'a, E> {
        let mut result = ToInvalidate::default();
        for invalidation in self.invalidations {
            match invalidation.dependency.invalidation_kind() {
                DependencyInvalidationKind::FullSelector => unreachable!(),
                DependencyInvalidationKind::Normal(_) | DependencyInvalidationKind::Scope(_) => {
                    unreachable!("Inner selector in invalidation?")
                },
                DependencyInvalidationKind::Relative(kind) => {
                    if let Some(context) = self.optimization_context.as_ref()
                        && context.can_be_ignored(
                            self.quirks_mode,
                            invalidation.element != self.top,
                            invalidation.element,
                            invalidation.host,
                            invalidation.dependency,
                            invalidation.leftmost_collapse_offset,
                        )
                    {
                        continue;
                    }
                    let dependency = &invalidation.dependency.next.as_ref().unwrap().slice()[0];
                    result.invalidations.push(RelativeSelectorInvalidation {
                        kind,
                        host: invalidation.host,
                        dependency,
                    });
                    // We move the invalidation up to the top of the subtree to avoid unnecessary traveral, but
                    // this means that we need to take ancestor-earlier sibling invalidations into account, as
                    // they'd look into earlier siblings of the top of the subtree as well.
                    if invalidation.element != self.top
                        && matches!(
                            kind,
                            RelativeDependencyInvalidationKind::AncestorEarlierSibling
                                | RelativeDependencyInvalidationKind::AncestorPrevSibling
                        )
                    {
                        result.invalidations.push(RelativeSelectorInvalidation {
                            kind: if matches!(
                                kind,
                                RelativeDependencyInvalidationKind::AncestorPrevSibling
                            ) {
                                RelativeDependencyInvalidationKind::PrevSibling
                            } else {
                                RelativeDependencyInvalidationKind::EarlierSibling
                            },
                            host: invalidation.host,
                            dependency,
                        });
                    }
                },
            };
        }
        for (key, element_dependencies) in self.dependencies {
            result.dependencies.push((key, element_dependencies));
        }
        result
    }

    fn collect_all_dependencies_for_element(
        &mut self,
        element: E,
        scope: Option<OpaqueElement>,
        quirks_mode: QuirksMode,
        map: &'a InvalidationMap,
        additional_relative_selector_invalidation_map: &'a AdditionalRelativeSelectorInvalidationMap,
        operation: DomMutationOperation,
    ) {
        element.id().map(|v| {
            if let Some(v) = map.id_to_selector.get(v, quirks_mode) {
                for dependency in v {
                    if !operation.accept(dependency, element) {
                        continue;
                    }
                    self.add_dependency(dependency, element, scope);
                }
            }
        });
        element.each_class(|v| {
            if let Some(v) = map.class_to_selector.get(v, quirks_mode) {
                for dependency in v {
                    if !operation.accept(dependency, element) {
                        continue;
                    }
                    self.add_dependency(dependency, element, scope);
                }
            }
        });
        element.each_custom_state(|v| {
            if let Some(v) = map.custom_state_affecting_selectors.get(v) {
                for dependency in v {
                    if !operation.accept(dependency, element) {
                        continue;
                    }
                    self.add_dependency(dependency, element, scope);
                }
            }
        });
        element.each_attr_name(|v| {
            if let Some(v) = map.other_attribute_affecting_selectors.get(v) {
                for dependency in v {
                    if !operation.accept(dependency, element) {
                        continue;
                    }
                    self.add_dependency(dependency, element, scope);
                }
            }
        });
        let state = element.state();
        map.state_affecting_selectors.lookup_with_additional(
            element,
            quirks_mode,
            None,
            &[],
            ElementState::empty(),
            |dependency| {
                if !dependency.state.intersects(state) {
                    return true;
                }
                if !operation.accept(&dependency.dep, element) {
                    return true;
                }
                self.add_dependency(&dependency.dep, element, scope);
                true
            },
        );

        additional_relative_selector_invalidation_map
            .ts_state_to_selector
            .lookup_with_additional(
                element,
                quirks_mode,
                None,
                &[],
                ElementState::empty(),
                |dependency| {
                    if !operation.accept(&dependency.dep, element) {
                        return true;
                    }
                    // This section contain potential optimization for not running full invalidation -
                    // consult documentation in `TSStateForInvalidation`.
                    if dependency.state.may_be_optimized() {
                        if operation.is_side_effect() {
                            // Side effect operations act on element not being mutated, so they can't
                            // change the match outcome of these optimizable pseudoclasses.
                            return true;
                        }
                        debug_assert!(
                            self.optimization_context.is_some(),
                            "Optimization context not available for DOM mutation?"
                        );
                        if dependency.state.contains(TSStateForInvalidation::EMPTY)
                            && element.first_element_child().is_some()
                        {
                            return true;
                        }

                        let sibling_traversal_map = self
                            .optimization_context
                            .as_ref()
                            .unwrap()
                            .sibling_traversal_map;
                        if dependency
                            .state
                            .contains(TSStateForInvalidation::NTH_EDGE_FIRST)
                            && sibling_traversal_map.prev_sibling_for(&element).is_some()
                        {
                            return true;
                        }

                        if dependency
                            .state
                            .contains(TSStateForInvalidation::NTH_EDGE_LAST)
                            && sibling_traversal_map.next_sibling_for(&element).is_some()
                        {
                            return true;
                        }
                    }
                    self.add_dependency(&dependency.dep, element, scope);
                    true
                },
            );

        if let Some(v) = additional_relative_selector_invalidation_map
            .type_to_selector
            .get(element.local_name())
        {
            for dependency in v {
                if !operation.accept(dependency, element) {
                    continue;
                }
                self.add_dependency(dependency, element, scope);
            }
        }

        for dependency in &additional_relative_selector_invalidation_map.any_to_selector {
            if !operation.accept(dependency, element) {
                continue;
            }
            self.add_dependency(dependency, element, scope);
        }
    }

    fn is_empty(&self) -> bool {
        self.invalidations.is_empty() && self.dependencies.is_empty()
    }
}

impl<'a, 'b, E> RelativeSelectorInvalidator<'a, 'b, E>
where
    E: TElement + 'a,
{
    /// Gather relative selector dependencies for the given element, and invalidate as necessary.
    #[inline(never)]
    pub fn invalidate_relative_selectors_for_this<F>(
        self,
        stylist: &'a Stylist,
        mut gather_dependencies: F,
    ) where
        F: FnMut(
            &E,
            Option<OpaqueElement>,
            &'a CascadeData,
            QuirksMode,
            &mut RelativeSelectorDependencyCollector<'a, 'b, E>,
        ),
    {
        let mut collector = RelativeSelectorDependencyCollector::new(
            self.element,
            self.quirks_mode,
            None,
            self.snapshots,
        );
        stylist.for_each_cascade_data_with_scope(self.element, |data, scope| {
            let map = data.relative_invalidation_map_attributes();
            if !map.used {
                return;
            }
            gather_dependencies(
                &self.element,
                scope.map(|e| e.opaque()),
                data,
                self.quirks_mode,
                &mut collector,
            );
        });
        if collector.is_empty() {
            return;
        }
        self.invalidate_from_dependencies(collector.get());
    }

    /// Gather relative selector dependencies for the given element (And its subtree) that mutated, and invalidate as necessary.
    #[inline(never)]
    pub fn invalidate_relative_selectors_for_dom_mutation(
        self,
        subtree: bool,
        stylist: &'a Stylist,
        inherited_search_path: ElementSelectorFlags,
        operation: DomMutationOperation,
    ) {
        let mut collector = RelativeSelectorDependencyCollector::new(
            self.element,
            self.quirks_mode,
            if operation.is_side_effect() {
                None
            } else {
                Some(OptimizationContext {
                    sibling_traversal_map: &self.sibling_traversal_map,
                    operation,
                })
            },
            None,
        );
        let mut traverse_subtree = false;
        self.element.apply_selector_flags(inherited_search_path);
        stylist.for_each_cascade_data_with_scope(self.element, |data, scope| {
            let map_attributes = data.relative_invalidation_map_attributes();
            if !map_attributes.used {
                return;
            }
            let map = data.relative_selector_invalidation_map();
            traverse_subtree |= map_attributes.needs_ancestors_traversal;
            collector.collect_all_dependencies_for_element(
                self.element,
                scope.map(|e| e.opaque()),
                self.quirks_mode,
                map,
                map_attributes,
                operation,
            );
        });

        if subtree && traverse_subtree {
            for node in self.element.as_node().dom_descendants() {
                let descendant = match node.as_element() {
                    Some(e) => e,
                    None => continue,
                };
                descendant.apply_selector_flags(inherited_search_path);
                stylist.for_each_cascade_data_with_scope(descendant, |data, scope| {
                    let map_attributes = data.relative_invalidation_map_attributes();
                    if !map_attributes.used {
                        return;
                    }
                    let map = data.relative_selector_invalidation_map();
                    collector.collect_all_dependencies_for_element(
                        descendant,
                        scope.map(|e| e.opaque()),
                        self.quirks_mode,
                        map,
                        map_attributes,
                        operation,
                    );
                });
            }
        }
        if collector.is_empty() {
            return;
        }
        self.invalidate_from_dependencies(collector.get());
    }

    /// Carry out complete invalidation triggered by a relative selector invalidation.
    fn invalidate_from_dependencies(&self, to_invalidate: ToInvalidate<'a, E>) {
        for (element, dependencies) in to_invalidate.dependencies {
            let mut selector_caches = SelectorCaches::default();
            let mut processor = RelativeSelectorInnerInvalidationProcessor::new(
                self.quirks_mode,
                self.snapshots,
                &dependencies,
                &mut selector_caches,
                &self.sibling_traversal_map,
            );
            TreeStyleInvalidator::new(element, None, &mut processor).invalidate();
            for (element, invalidation) in processor.take_invalidations() {
                self.invalidate_upwards(element, &invalidation);
            }
        }
        for invalidation in to_invalidate.invalidations {
            self.invalidate_upwards(self.element, &invalidation);
        }
    }

    fn invalidate_upwards(&self, element: E, invalidation: &RelativeSelectorInvalidation<'a>) {
        // This contains the main reason for why relative selector invalidation is handled
        // separately - It travels ancestor and/or earlier sibling direction.
        match invalidation.kind {
            RelativeDependencyInvalidationKind::Parent => {
                element.parent_element().map(|e| {
                    if !Self::in_search_direction(
                        &e,
                        ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR,
                    ) {
                        return;
                    }
                    self.handle_anchor(e, invalidation.dependency, invalidation.host);
                });
            },
            RelativeDependencyInvalidationKind::Ancestors => {
                let mut parent = element.parent_element();
                while let Some(par) = parent {
                    if !Self::in_search_direction(
                        &par,
                        ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR,
                    ) {
                        return;
                    }
                    self.handle_anchor(par, invalidation.dependency, invalidation.host);
                    parent = par.parent_element();
                }
            },
            RelativeDependencyInvalidationKind::PrevSibling => {
                self.sibling_traversal_map
                    .prev_sibling_for(&element)
                    .map(|e| {
                        if !Self::in_search_direction(
                            &e,
                            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING,
                        ) {
                            return;
                        }
                        self.handle_anchor(e, invalidation.dependency, invalidation.host);
                    });
            },
            RelativeDependencyInvalidationKind::AncestorPrevSibling => {
                let mut parent = element.parent_element();
                while let Some(par) = parent {
                    if !Self::in_search_direction(
                        &par,
                        ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR,
                    ) {
                        return;
                    }
                    par.prev_sibling_element().map(|e| {
                        if !Self::in_search_direction(
                            &e,
                            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING,
                        ) {
                            return;
                        }
                        self.handle_anchor(e, invalidation.dependency, invalidation.host);
                    });
                    parent = par.parent_element();
                }
            },
            RelativeDependencyInvalidationKind::EarlierSibling => {
                let mut sibling = self.sibling_traversal_map.prev_sibling_for(&element);
                while let Some(sib) = sibling {
                    if !Self::in_search_direction(
                        &sib,
                        ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING,
                    ) {
                        return;
                    }
                    self.handle_anchor(sib, invalidation.dependency, invalidation.host);
                    sibling = sib.prev_sibling_element();
                }
            },
            RelativeDependencyInvalidationKind::AncestorEarlierSibling => {
                let mut parent = element.parent_element();
                while let Some(par) = parent {
                    if !Self::in_search_direction(
                        &par,
                        ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_ANCESTOR,
                    ) {
                        return;
                    }
                    let mut sibling = par.prev_sibling_element();
                    while let Some(sib) = sibling {
                        if !Self::in_search_direction(
                            &sib,
                            ElementSelectorFlags::RELATIVE_SELECTOR_SEARCH_DIRECTION_SIBLING,
                        ) {
                            return;
                        }
                        self.handle_anchor(sib, invalidation.dependency, invalidation.host);
                        sibling = sib.prev_sibling_element();
                    }
                    parent = par.parent_element();
                }
            },
        }
    }

    /// Is this element in the direction of the given relative selector search path?
    fn in_search_direction(element: &E, desired: ElementSelectorFlags) -> bool {
        element
            .relative_selector_search_direction()
            .intersects(desired)
    }

    /// Handle a potential relative selector anchor.
    fn handle_anchor(
        &self,
        element: E,
        outer_dependency: &Dependency,
        host: Option<OpaqueElement>,
    ) {
        let is_rightmost = Self::is_subject(outer_dependency);
        if (is_rightmost
            && !element.has_selector_flags(ElementSelectorFlags::ANCHORS_RELATIVE_SELECTOR))
            || (!is_rightmost
                && !element.has_selector_flags(
                    ElementSelectorFlags::ANCHORS_RELATIVE_SELECTOR_NON_SUBJECT,
                ))
        {
            // If it was never a relative selector anchor, don't bother.
            return;
        }
        let mut selector_caches = SelectorCaches::default();
        let matching_context = MatchingContext::<'_, E::Impl>::new_for_visited(
            MatchingMode::Normal,
            None,
            &mut selector_caches,
            VisitedHandlingMode::AllLinksVisitedAndUnvisited,
            self.quirks_mode,
            NeedsSelectorFlags::No,
            MatchingForInvalidation::Yes,
        );
        let mut data = match element.mutate_data() {
            Some(data) => data,
            None => return,
        };
        let mut processor = RelativeSelectorOuterInvalidationProcessor {
            element,
            host,
            data: data.deref_mut(),
            dependency: outer_dependency,
            matching_context,
            traversal_map: &self.sibling_traversal_map,
        };
        let result = TreeStyleInvalidator::new(element, None, &mut processor).invalidate();
        (self.invalidated)(element, &result);
    }

    /// Does this relative selector dependency have its relative selector in the subject position?
    fn is_subject(outer_dependency: &Dependency) -> bool {
        debug_assert!(
            matches!(
                outer_dependency.invalidation_kind(),
                DependencyInvalidationKind::Normal(_) | DependencyInvalidationKind::Scope(_)
            ),
            "Outer selector of relative selector is relative?"
        );

        if let Some(x) = outer_dependency.next.as_ref() {
            // We only care to ensure that we're the subject in the outermost selector of the current
            // selector - Crossing over a scope invalidation would mean moving into a selector inside
            // the current scope block
            if matches!(
                outer_dependency.invalidation_kind(),
                DependencyInvalidationKind::Normal(..)
            ) && !Self::is_subject(&x.as_ref().slice()[0])
            {
                // Not subject in outer selector.
                return false;
            }
        }
        outer_dependency
            .selector
            .is_rightmost(outer_dependency.selector_offset)
    }
}

/// Blindly invalidate everything outside of a relative selector.
/// Consider `:is(.a :has(.b) .c ~ .d) ~ .e .f`, where .b gets deleted.
/// Since the tree mutated, we cannot rely on snapshots.
pub struct RelativeSelectorOuterInvalidationProcessor<'a, 'b, E: TElement> {
    /// Element being invalidated.
    pub element: E,
    /// The current shadow host, if any.
    pub host: Option<OpaqueElement>,
    /// Data for the element being invalidated.
    pub data: &'a mut ElementData,
    /// Dependency to be processed.
    pub dependency: &'b Dependency,
    /// Matching context to use for invalidation.
    pub matching_context: MatchingContext<'a, E::Impl>,
    /// Traversal map for this invalidation.
    pub traversal_map: &'a SiblingTraversalMap<E>,
}

impl<'a, 'b: 'a, E: 'a> InvalidationProcessor<'b, 'a, E>
    for RelativeSelectorOuterInvalidationProcessor<'a, 'b, E>
where
    E: TElement,
{
    fn invalidates_on_pseudo_element(&self) -> bool {
        true
    }

    fn check_outer_dependency(
        &mut self,
        _dependency: &Dependency,
        _element: E,
        _: Option<OpaqueElement>,
    ) -> bool {
        // At this point, we know a relative selector invalidated, and are ignoring them.
        true
    }

    fn matching_context(&mut self) -> &mut MatchingContext<'a, E::Impl> {
        &mut self.matching_context
    }

    fn sibling_traversal_map(&self) -> &SiblingTraversalMap<E> {
        self.traversal_map
    }

    fn collect_invalidations(
        &mut self,
        element: E,
        _self_invalidations: &mut InvalidationVector<'b>,
        descendant_invalidations: &mut DescendantInvalidationLists<'b>,
        sibling_invalidations: &mut InvalidationVector<'b>,
    ) -> bool {
        debug_assert_eq!(element, self.element);
        debug_assert!(
            self.matching_context.matching_for_invalidation(),
            "Not matching for invalidation?"
        );

        // Ok, this element can potentially an anchor to the given dependency.
        // Before we do the potentially-costly ancestor/earlier sibling traversal,
        // See if it can actuall be an anchor by trying to match the "rest" of the selector
        // outside and to the left of `:has` in question.
        // e.g. Element under consideration can only be the anchor to `:has` in
        // `.foo .bar ~ .baz:has()`, iff it matches `.foo .bar ~ .baz`.
        let invalidated_self = {
            let mut invalidated = false;
            let mut dependencies_to_invalidate: SmallVec<[&Dependency; 1]> =
                smallvec![self.dependency];
            while let Some(d) = dependencies_to_invalidate.pop() {
                debug_assert!(
                    matches!(
                        d.invalidation_kind(),
                        DependencyInvalidationKind::Normal(_)
                            | DependencyInvalidationKind::Scope(_)
                    ),
                    "Unexpected dependency kind"
                );
                if !dependency_may_be_relevant(d, &element, false) {
                    continue;
                }
                if !matches_selector(
                    &d.selector,
                    d.selector_offset,
                    None,
                    element,
                    self.matching_context(),
                ) {
                    continue;
                }

                let invalidation_kind = d.invalidation_kind();

                if let DependencyInvalidationKind::Scope(scope_kind) = invalidation_kind
                    && d.selector.is_rightmost(d.selector_offset)
                {
                    if scope_kind == ScopeDependencyInvalidationKind::ScopeEnd {
                        let invalidations = note_scope_dependency_force_at_subject(
                            d,
                            self.matching_context.current_host,
                            self.matching_context.scope_element,
                            false,
                        );
                        descendant_invalidations
                            .dom_descendants
                            .extend(invalidations);

                        invalidated |= true;
                    } else if let Some(ref next) = d.next {
                        dependencies_to_invalidate.extend(next.as_ref().slice());
                    }
                    continue;
                }

                if matches!(
                    invalidation_kind,
                    DependencyInvalidationKind::Normal(NormalDependencyInvalidationKind::Element)
                ) {
                    if let Some(ref deps) = d.next {
                        // Normal dependencies should only have one next
                        dependencies_to_invalidate.push(&deps.as_ref().slice()[0]);
                        continue;
                    }
                    invalidated |= true;
                    continue;
                }
                debug_assert_ne!(d.selector_offset, 0);
                debug_assert_ne!(d.selector_offset, d.selector.len());
                let invalidation = Invalidation::new(d, self.host, None);
                invalidated |= push_invalidation(
                    invalidation,
                    d.invalidation_kind(),
                    descendant_invalidations,
                    sibling_invalidations,
                );
            }
            invalidated
        };

        if invalidated_self {
            self.data.hint.insert(RestyleHint::RESTYLE_SELF);
        }
        invalidated_self
    }

    fn should_process_descendants(&mut self, element: E) -> bool {
        if element == self.element {
            return should_process_descendants(self.data);
        }

        match element.borrow_data() {
            Some(d) => should_process_descendants(&d),
            None => false,
        }
    }

    fn recursion_limit_exceeded(&mut self, _element: E) {
        unreachable!("Unexpected recursion limit");
    }

    fn invalidated_descendants(&mut self, element: E, child: E) {
        invalidated_descendants(element, child)
    }

    fn invalidated_self(&mut self, element: E) {
        debug_assert_ne!(element, self.element);
        invalidated_self(element);
    }

    fn invalidated_sibling(&mut self, element: E, of: E) {
        debug_assert_ne!(element, self.element);
        invalidated_sibling(element, of);
    }
}

/// Invalidation for the selector(s) inside a relative selector.
pub struct RelativeSelectorInnerInvalidationProcessor<'a, 'b, 's, E>
where
    E: TElement,
{
    /// Matching context to be used.
    matching_context: MatchingContext<'b, E::Impl>,
    /// Table of snapshots.
    snapshots: Option<&'b Snapshots<'s>>,
    /// Incoming dependencies to be processed.
    dependencies: &'b ElementDependencies<'a>,
    /// Generated invalidations.
    invalidations: InnerInvalidations<'a, E>,
    /// Traversal map for this invalidation.
    traversal_map: &'b SiblingTraversalMap<E>,
}

impl<'a, 'b, 's, E> RelativeSelectorInnerInvalidationProcessor<'a, 'b, 's, E>
where
    E: TElement,
{
    fn new(
        quirks_mode: QuirksMode,
        snapshots: Option<&'b Snapshots<'s>>,
        dependencies: &'b ElementDependencies<'a>,
        selector_caches: &'b mut SelectorCaches,
        traversal_map: &'b SiblingTraversalMap<E>,
    ) -> Self {
        let matching_context = MatchingContext::new_for_visited(
            MatchingMode::Normal,
            None,
            selector_caches,
            VisitedHandlingMode::AllLinksVisitedAndUnvisited,
            quirks_mode,
            NeedsSelectorFlags::No,
            MatchingForInvalidation::Yes,
        );
        Self {
            matching_context,
            snapshots,
            dependencies,
            invalidations: InnerInvalidations::default(),
            traversal_map,
        }
    }

    fn note_dependency(
        &mut self,
        element: E,
        host: Option<OpaqueElement>,
        dependency: &'a Dependency,
        descendant_invalidations: &mut DescendantInvalidationLists<'a>,
        sibling_invalidations: &mut InvalidationVector<'a>,
    ) {
        match dependency.invalidation_kind() {
            DependencyInvalidationKind::FullSelector => unreachable!(),
            DependencyInvalidationKind::Normal(_) | DependencyInvalidationKind::Scope(_) => (),
            DependencyInvalidationKind::Relative(kind) => {
                self.found_relative_selector_invalidation(element, kind, dependency);
                return;
            },
        }
        if matches!(
            dependency.normal_invalidation_kind(),
            NormalDependencyInvalidationKind::Element
        ) {
            // Ok, keep heading outwards.
            debug_assert!(
                dependency.next.is_some(),
                "Orphaned inner selector dependency?"
            );
            if let Some(next) = dependency.next.as_ref() {
                self.note_dependency(
                    element,
                    host,
                    &next.as_ref().slice()[0],
                    descendant_invalidations,
                    sibling_invalidations,
                );
            }
            return;
        }
        let invalidation = Invalidation::new(dependency, None, None);
        match dependency.normal_invalidation_kind() {
            NormalDependencyInvalidationKind::Descendants => {
                // Descendant invalidations are simplified due to pseudo-elements not being available within the relative selector.
                descendant_invalidations.dom_descendants.push(invalidation)
            },
            NormalDependencyInvalidationKind::Siblings => sibling_invalidations.push(invalidation),
            // Note(dshin, bug 1940212): Nesting can enabling stuffing pseudo-elements into :has, like `::marker { :has(&) }`.
            // Ideally, we can just not insert the dependency into the invalidation map, but the necessary selector information
            // for this (i.e. `HAS_PSEUDO`) is filtered out in `replace_parent_selector` through
            // `SelectorFlags::forbidden_for_nesting`, so just ignoring such dependencies here is the best we can do.
            _ => (),
        }
    }

    /// Take the generated invalidations.
    fn take_invalidations(self) -> InnerInvalidations<'a, E> {
        self.invalidations
    }
}

impl<'a, 'b, E> InvalidationProcessor<'a, 'b, E>
    for RelativeSelectorInnerInvalidationProcessor<'a, 'b, '_, E>
where
    E: TElement,
{
    fn check_outer_dependency(
        &mut self,
        dependency: &Dependency,
        element: E,
        _: Option<OpaqueElement>,
    ) -> bool {
        if let Some(snapshots) = self.snapshots {
            let wrapper = ElementWrapper::new(element, snapshots);
            return check_dependency(
                dependency,
                element,
                wrapper,
                &mut self.matching_context,
                None,
            );
        }
        // Just invalidate if we don't have a snapshot.
        true
    }

    fn matching_context(&mut self) -> &mut MatchingContext<'b, E::Impl> {
        &mut self.matching_context
    }

    fn collect_invalidations(
        &mut self,
        element: E,
        _self_invalidations: &mut InvalidationVector<'a>,
        descendant_invalidations: &mut DescendantInvalidationLists<'a>,
        sibling_invalidations: &mut InvalidationVector<'a>,
    ) -> bool {
        for (scope, dependency) in self.dependencies {
            self.note_dependency(
                element,
                *scope,
                dependency,
                descendant_invalidations,
                sibling_invalidations,
            )
        }
        false
    }

    fn should_process_descendants(&mut self, _element: E) -> bool {
        true
    }

    fn recursion_limit_exceeded(&mut self, _element: E) {
        unreachable!("Unexpected recursion limit");
    }

    // Don't do anything for normal invalidations.
    fn invalidated_self(&mut self, _element: E) {}
    fn invalidated_sibling(&mut self, _sibling: E, _of: E) {}
    fn invalidated_descendants(&mut self, _element: E, _child: E) {}

    fn found_relative_selector_invalidation(
        &mut self,
        element: E,
        kind: RelativeDependencyInvalidationKind,
        dep: &'a Dependency,
    ) {
        debug_assert!(dep.next.is_some(), "Orphaned inners selector?");
        if element.relative_selector_search_direction().is_empty() {
            return;
        }
        self.invalidations.push((
            element,
            RelativeSelectorInvalidation {
                host: self.matching_context.current_host,
                kind,
                dependency: &dep.next.as_ref().unwrap().as_ref().slice()[0],
            },
        ));
    }

    fn sibling_traversal_map(&self) -> &SiblingTraversalMap<E> {
        self.traversal_map
    }
}
