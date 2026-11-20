/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The struct that takes care of encapsulating all the logic on where and how
//! element styles need to be invalidated.

use crate::context::StackLimitChecker;
use crate::dom::{TElement, TNode, TShadowRoot};
use crate::invalidation::element::invalidation_map::{
    Dependency, DependencyInvalidationKind, NormalDependencyInvalidationKind,
    RelativeDependencyInvalidationKind, ScopeDependencyInvalidationKind,
};
use selectors::matching::matches_compound_selector_from;
use selectors::matching::{CompoundSelectorMatchingResult, MatchingContext};
use selectors::parser::{Combinator, Component, Selector, SelectorVisitor};
use selectors::{OpaqueElement, SelectorImpl};
use smallvec::{SmallVec, smallvec};
use std::fmt;
use std::fmt::Write;

struct SiblingInfo<E>
where
    E: TElement,
{
    affected: E,
    prev_sibling: Option<E>,
    next_sibling: Option<E>,
}

/// Traversal mapping for elements under consideration. It acts like a snapshot map,
/// though this only "maps" one element at most.
/// For general invalidations, this has no effect, especially since when
/// DOM mutates, the mutation's effect should not escape the subtree being mutated.
/// This is not the case for relative selectors, unfortunately, so we may end up
/// traversing a portion of the DOM tree that mutated. In case the mutation is removal,
/// its sibling relation is severed by the time the invalidation happens. This structure
/// recovers that relation. Note - it assumes that there is only one element under this
/// effect.
pub struct SiblingTraversalMap<E>
where
    E: TElement,
{
    info: Option<SiblingInfo<E>>,
}

impl<E> Default for SiblingTraversalMap<E>
where
    E: TElement,
{
    fn default() -> Self {
        Self { info: None }
    }
}

impl<E> SiblingTraversalMap<E>
where
    E: TElement,
{
    /// Create a new traversal map with the affected element.
    pub fn new(affected: E, prev_sibling: Option<E>, next_sibling: Option<E>) -> Self {
        Self {
            info: Some(SiblingInfo {
                affected,
                prev_sibling,
                next_sibling,
            }),
        }
    }

    /// Get the element's previous sibling element.
    pub fn next_sibling_for(&self, element: &E) -> Option<E> {
        if let Some(ref info) = self.info
            && *element == info.affected
        {
            return info.next_sibling;
        }
        element.next_sibling_element()
    }

    /// Get the element's previous sibling element.
    pub fn prev_sibling_for(&self, element: &E) -> Option<E> {
        if let Some(ref info) = self.info
            && *element == info.affected
        {
            return info.prev_sibling;
        }
        element.prev_sibling_element()
    }
}

/// A trait to abstract the collection of invalidations for a given pass.
pub trait InvalidationProcessor<'a, 'b, E>
where
    E: TElement,
{
    /// Whether an invalidation that contains only a pseudo-element selector
    /// like ::before or ::after triggers invalidation of the element that would
    /// originate it.
    fn invalidates_on_pseudo_element(&self) -> bool {
        false
    }

    /// Whether the invalidation processor only cares about light-tree
    /// descendants of a given element, that is, doesn't invalidate
    /// pseudo-elements, NAC, shadow dom...
    fn light_tree_only(&self) -> bool {
        false
    }

    /// When a dependency from a :where or :is selector matches, it may still be
    /// the case that we don't need to invalidate the full style. Consider the
    /// case of:
    ///
    ///   div .foo:where(.bar *, .baz) .qux
    ///
    /// We can get to the `*` part after a .bar class change, but you only need
    /// to restyle the element if it also matches .foo.
    ///
    /// Similarly, you only need to restyle .baz if the whole result of matching
    /// the selector changes.
    ///
    /// This function is called to check the result of matching the "outer"
    /// dependency that we generate for the parent of the `:where` selector,
    /// that is, in the case above it should match
    /// `div .foo:where(.bar *, .baz)`.
    ///
    /// `scope` is set to `Some()` if this dependency follows a scope invalidation
    /// Matching context should be adjusted accordingly with `nest_for_scope`.
    ///
    /// Returning true unconditionally here is over-optimistic and may
    /// over-invalidate.
    fn check_outer_dependency(
        &mut self,
        dependency: &Dependency,
        element: E,
        scope: Option<OpaqueElement>,
    ) -> bool;

    /// The matching context that should be used to process invalidations.
    fn matching_context(&mut self) -> &mut MatchingContext<'b, E::Impl>;

    /// The traversal map that should be used to process invalidations.
    fn sibling_traversal_map(&self) -> &SiblingTraversalMap<E>;

    /// Collect invalidations for a given element's descendants and siblings.
    ///
    /// Returns whether the element itself was invalidated.
    fn collect_invalidations(
        &mut self,
        element: E,
        self_invalidations: &mut InvalidationVector<'a>,
        descendant_invalidations: &mut DescendantInvalidationLists<'a>,
        sibling_invalidations: &mut InvalidationVector<'a>,
    ) -> bool;

    /// Returns whether the invalidation process should process the descendants
    /// of the given element.
    fn should_process_descendants(&mut self, element: E) -> bool;

    /// Executes an arbitrary action when the recursion limit is exceded (if
    /// any).
    fn recursion_limit_exceeded(&mut self, element: E);

    /// Executes an action when `Self` is invalidated.
    fn invalidated_self(&mut self, element: E);

    /// Executes an action when `sibling` is invalidated as a sibling of
    /// `of`.
    fn invalidated_sibling(&mut self, sibling: E, of: E);

    /// Called when a highlight pseudo-element (::selection, ::highlight,
    /// ::target-text) style is invalidated. These pseudos have their styles
    /// resolved lazily during painting rather than during the restyle traversal,
    /// so style changes don't automatically trigger repaints.
    fn invalidated_highlight_pseudo(&mut self, _element: E) {}

    /// Executes an action when any descendant of `Self` is invalidated.
    fn invalidated_descendants(&mut self, element: E, child: E);

    /// Executes an action when an element in a relative selector is reached.
    /// Lets the dependency to be borrowed for further processing out of the
    /// invalidation traversal.
    fn found_relative_selector_invalidation(
        &mut self,
        _element: E,
        _kind: RelativeDependencyInvalidationKind,
        _relative_dependency: &'a Dependency,
    ) {
        debug_assert!(false, "Reached relative selector dependency");
    }
}

/// Different invalidation lists for descendants.
#[derive(Debug, Default)]
pub struct DescendantInvalidationLists<'a> {
    /// Invalidations for normal DOM children and pseudo-elements.
    ///
    /// TODO(emilio): Having a list of invalidations just for pseudo-elements
    /// may save some work here and there.
    pub dom_descendants: InvalidationVector<'a>,
    /// Invalidations for slotted children of an element.
    pub slotted_descendants: InvalidationVector<'a>,
    /// Invalidations for ::part()s of an element.
    pub parts: InvalidationVector<'a>,
}

impl<'a> DescendantInvalidationLists<'a> {
    fn is_empty(&self) -> bool {
        self.dom_descendants.is_empty()
            && self.slotted_descendants.is_empty()
            && self.parts.is_empty()
    }
}

/// The struct that takes care of encapsulating all the logic on where and how
/// element styles need to be invalidated.
pub struct TreeStyleInvalidator<'a, 'b, 'c, E, P: 'a>
where
    'b: 'a,
    E: TElement,
    P: InvalidationProcessor<'b, 'c, E>,
{
    element: E,
    stack_limit_checker: Option<&'a StackLimitChecker>,
    processor: &'a mut P,
    _marker: std::marker::PhantomData<(&'b (), &'c ())>,
}

/// A vector of invalidations, optimized for small invalidation sets.
pub type InvalidationVector<'a> = SmallVec<[Invalidation<'a>; 10]>;

/// The kind of descendant invalidation we're processing.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum DescendantInvalidationKind {
    /// A DOM descendant invalidation.
    Dom,
    /// A ::slotted() descendant invalidation.
    Slotted,
    /// A ::part() descendant invalidation.
    Part,
}

/// The kind of invalidation we're processing.
///
/// We can use this to avoid pushing invalidations of the same kind to our
/// descendants or siblings.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum InvalidationKind {
    Descendant(DescendantInvalidationKind),
    Sibling,
}

/// The kind of traversal an invalidation requires.
pub enum InvalidationAddOverride {
    /// This invalidation should be added to descendant invalidation
    Descendant,
    /// This invalidation should be added to sibling invalidations
    Sibling,
}

/// An `Invalidation` is a complex selector that describes which elements,
/// relative to a current element we are processing, must be restyled.
#[derive(Clone)]
pub struct Invalidation<'a> {
    /// The dependency that generated this invalidation.
    ///
    /// Note that the offset inside the dependency is not really useful after
    /// construction.
    dependency: &'a Dependency,
    /// The right shadow host from where the rule came from, if any.
    ///
    /// This is needed to ensure that we match the selector with the right
    /// state, as whether some selectors like :host and ::part() match depends
    /// on it.
    host: Option<OpaqueElement>,
    /// The scope element from which this rule comes from, if any.
    scope: Option<OpaqueElement>,
    /// The offset of the selector pointing to a compound selector.
    ///
    /// This order is a "parse order" offset, that is, zero is the leftmost part
    /// of the selector written as parsed / serialized.
    ///
    /// It is initialized from the offset from `dependency`.
    offset: usize,
    /// Whether the invalidation was already matched by any previous sibling or
    /// ancestor.
    ///
    /// If this is the case, we can avoid pushing invalidations generated by
    /// this one if the generated invalidation is effective for all the siblings
    /// or descendants after us.
    matched_by_any_previous: bool,
    /// Whether this incalidation should always be pushed to next invalidations.
    ///
    /// This is useful for overriding invalidations we would otherwise skip.
    ///  e.g @scope(.a){:not(:scope)} where we would need the :not(:scope)
    /// invalidation to traverse down for all children of the scope root
    always_effective_for_next_descendant: bool,
}

impl<'a> Invalidation<'a> {
    /// Create a new invalidation for matching a dependency.
    pub fn new(
        dependency: &'a Dependency,
        host: Option<OpaqueElement>,
        scope: Option<OpaqueElement>,
    ) -> Self {
        debug_assert!(
            dependency.selector_offset == dependency.selector.len() + 1
                || dependency.invalidation_kind()
                    != DependencyInvalidationKind::Normal(
                        NormalDependencyInvalidationKind::Element
                    ),
            "No point to this, if the dependency matched the element we should just invalidate it"
        );
        Self {
            dependency,
            host,
            scope,
            // + 1 to go past the combinator.
            offset: dependency.selector.len() + 1 - dependency.selector_offset,
            matched_by_any_previous: false,
            always_effective_for_next_descendant: false,
        }
    }

    /// Create a new invalidation for matching a dependency from the selector's subject.
    /// Using this should be avoided whenever possible as it overinvalidates.
    /// Only use it when it's not possible to match the selector in order due to
    /// invalidations that don't necessarily start at the pointed compound, such as
    /// what happens in note_scope_dependency_force_at_subject.
    pub fn new_subject_invalidation(
        dependency: &'a Dependency,
        host: Option<OpaqueElement>,
        scope: Option<OpaqueElement>,
    ) -> Self {
        let mut compound_offset = 0;
        for s in dependency.selector.iter_raw_match_order() {
            if s.is_combinator() {
                break;
            }
            compound_offset += 1;
        }

        Self {
            dependency,
            host,
            scope,
            offset: dependency.selector.len() - compound_offset,
            matched_by_any_previous: false,
            always_effective_for_next_descendant: true,
        }
    }

    /// Create a new invalidation for matching a dependency that should always check
    /// its next descendants. It tends to overinvalidate less than new_subject_invalidation
    /// but it should also be avoided whenever possible. Specifically used when crossing
    /// into implicit scope invalidation.
    pub fn new_always_effective_for_next_descendant(
        dependency: &'a Dependency,
        host: Option<OpaqueElement>,
        scope: Option<OpaqueElement>,
    ) -> Self {
        if dependency.selector.is_rightmost(dependency.selector_offset) {
            return Self::new_subject_invalidation(dependency, host, scope);
        }

        Self {
            dependency,
            host,
            scope,
            // + 1 to go past the combinator.
            offset: dependency.selector.len() + 1 - dependency.selector_offset,
            matched_by_any_previous: false,
            always_effective_for_next_descendant: true,
        }
    }

    /// Return the combinator to the right of the currently invalidating compound
    /// Useful for determining whether this invalidation should be pushed to
    /// sibling or descendant invalidations.
    pub fn combinator_to_right(&self) -> Combinator {
        debug_assert_ne!(self.dependency.selector_offset, 0);
        self.dependency
            .selector
            .combinator_at_match_order(self.dependency.selector.len() - self.offset)
    }

    /// Whether this invalidation is effective for the next sibling or
    /// descendant after us.
    fn effective_for_next(&self) -> bool {
        if self.offset == 0 || self.always_effective_for_next_descendant {
            return true;
        }

        // TODO(emilio): For pseudo-elements this should be mostly false, except
        // for the weird pseudos in <input type="number">.
        //
        // We should be able to do better here!
        match self
            .dependency
            .selector
            .combinator_at_parse_order(self.offset - 1)
        {
            Combinator::Descendant | Combinator::LaterSibling | Combinator::PseudoElement => true,
            Combinator::Part
            | Combinator::SlotAssignment
            | Combinator::NextSibling
            | Combinator::Child => false,
        }
    }

    fn kind(&self) -> InvalidationKind {
        if self.offset == 0 {
            return InvalidationKind::Descendant(DescendantInvalidationKind::Dom);
        }

        match self
            .dependency
            .selector
            .combinator_at_parse_order(self.offset - 1)
        {
            Combinator::Child | Combinator::Descendant | Combinator::PseudoElement => {
                InvalidationKind::Descendant(DescendantInvalidationKind::Dom)
            },
            Combinator::Part => InvalidationKind::Descendant(DescendantInvalidationKind::Part),
            Combinator::SlotAssignment => {
                InvalidationKind::Descendant(DescendantInvalidationKind::Slotted)
            },
            Combinator::NextSibling | Combinator::LaterSibling => InvalidationKind::Sibling,
        }
    }
}

/// A struct that visits a selector and determines if there is a `:scope`
/// component nested withing a negation. eg. :not(:scope)
struct NegationScopeVisitor {
    /// Have we found a negation list yet
    in_negation: bool,
    /// Have we found a :scope inside a negation yet
    found_scope_in_negation: bool,
}

impl NegationScopeVisitor {
    /// Create a new NegationScopeVisitor
    fn new() -> Self {
        Self {
            in_negation: false,
            found_scope_in_negation: false,
        }
    }

    fn traverse_selector(
        mut self,
        selector: &Selector<<NegationScopeVisitor as SelectorVisitor>::Impl>,
    ) -> bool {
        selector.visit(&mut self);
        self.found_scope_in_negation
    }

    /// Traverse all the next dependencies in an outer dependency until we reach
    /// 1. :not(* :scope *)
    /// 2. a scope or relative dependency
    /// 3. the end of the chain of dependencies
    /// Return whether or not we encountered :not(* :scope *)
    fn traverse_dependency(mut self, dependency: &Dependency) -> bool {
        if dependency.next.is_none()
            || !matches!(
                dependency.invalidation_kind(),
                DependencyInvalidationKind::Normal(..)
            )
        {
            dependency.selector.visit(&mut self);
            return self.found_scope_in_negation;
        }

        let nested_visitor = Self {
            in_negation: self.in_negation,
            found_scope_in_negation: false,
        };
        dependency.selector.visit(&mut self);
        // Has to be normal dependency and next.is_some()
        nested_visitor.traverse_dependency(&dependency.next.as_ref().unwrap().slice()[0])
    }
}

impl SelectorVisitor for NegationScopeVisitor {
    type Impl = crate::selector_parser::SelectorImpl;

    fn visit_attribute_selector(
        &mut self,
        _namespace: &selectors::attr::NamespaceConstraint<
            &<Self::Impl as SelectorImpl>::NamespaceUrl,
        >,
        _local_name: &<Self::Impl as SelectorImpl>::LocalName,
        _local_name_lower: &<Self::Impl as SelectorImpl>::LocalName,
    ) -> bool {
        true
    }

    fn visit_simple_selector(&mut self, component: &Component<Self::Impl>) -> bool {
        if self.in_negation && component == &Component::Scope {
            self.found_scope_in_negation = true;
        }
        true
    }

    fn visit_relative_selector_list(
        &mut self,
        _list: &[selectors::parser::RelativeSelector<Self::Impl>],
    ) -> bool {
        true
    }

    fn visit_selector_list(
        &mut self,
        list_kind: selectors::visitor::SelectorListKind,
        list: &[selectors::parser::Selector<Self::Impl>],
    ) -> bool {
        for nested in list {
            let nested_visitor = Self {
                in_negation: list_kind.in_negation(),
                found_scope_in_negation: false,
            };

            self.found_scope_in_negation |= nested_visitor.traverse_selector(nested);
        }
        true
    }

    fn visit_complex_selector(&mut self, _combinator_to_right: Option<Combinator>) -> bool {
        true
    }
}

/// Determines if we can find a selector in the form of :not(:scope)
/// anywhere down the chain of dependencies.
pub fn any_next_has_scope_in_negation(dependency: &Dependency) -> bool {
    let next = match dependency.next.as_ref() {
        None => return false,
        Some(l) => l,
    };

    next.slice().iter().any(|dep| {
        let visitor = NegationScopeVisitor::new();
        visitor.traverse_dependency(dep)
    })
}

impl<'a> fmt::Debug for Invalidation<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use cssparser::ToCss;

        f.write_str("Invalidation(")?;
        for component in self
            .dependency
            .selector
            .iter_raw_parse_order_from(self.offset)
        {
            if matches!(*component, Component::Combinator(..)) {
                break;
            }
            component.to_css(f)?;
        }
        f.write_char(')')
    }
}

/// The result of processing a single invalidation for a given element.
struct ProcessInvalidationResult {
    /// Whether the element itself was invalidated.
    invalidated_self: bool,
    /// Whether the invalidation matched, either invalidating the element or
    /// generating another invalidation.
    matched: bool,
}

/// The result of a whole invalidation process for a given element.
pub struct InvalidationResult {
    /// Whether the element itself was invalidated.
    invalidated_self: bool,
    /// Whether the element's descendants were invalidated.
    invalidated_descendants: bool,
    /// Whether the element's siblings were invalidated.
    invalidated_siblings: bool,
}

impl InvalidationResult {
    /// Create an emtpy result.
    pub fn empty() -> Self {
        Self {
            invalidated_self: false,
            invalidated_descendants: false,
            invalidated_siblings: false,
        }
    }

    /// Whether the invalidation has invalidate the element itself.
    pub fn has_invalidated_self(&self) -> bool {
        self.invalidated_self
    }

    /// Whether the invalidation has invalidate desendants.
    pub fn has_invalidated_descendants(&self) -> bool {
        self.invalidated_descendants
    }

    /// Whether the invalidation has invalidate siblings.
    pub fn has_invalidated_siblings(&self) -> bool {
        self.invalidated_siblings
    }
}

impl<'a, 'b, 'c, E, P: 'a> TreeStyleInvalidator<'a, 'b, 'c, E, P>
where
    'b: 'a,
    E: TElement,
    P: InvalidationProcessor<'b, 'c, E>,
{
    /// Trivially constructs a new `TreeStyleInvalidator`.
    pub fn new(
        element: E,
        stack_limit_checker: Option<&'a StackLimitChecker>,
        processor: &'a mut P,
    ) -> Self {
        Self {
            element,
            stack_limit_checker,
            processor,
            _marker: std::marker::PhantomData,
        }
    }

    /// Perform the invalidation pass.
    pub fn invalidate(mut self) -> InvalidationResult {
        debug!("StyleTreeInvalidator::invalidate({:?})", self.element);

        let mut self_invalidations = InvalidationVector::new();
        let mut descendant_invalidations = DescendantInvalidationLists::default();
        let mut sibling_invalidations = InvalidationVector::new();

        let mut invalidated_self = self.processor.collect_invalidations(
            self.element,
            &mut self_invalidations,
            &mut descendant_invalidations,
            &mut sibling_invalidations,
        );

        debug!("Collected invalidations (self: {}): ", invalidated_self);
        debug!(
            " > self: {}, {:?}",
            self_invalidations.len(),
            self_invalidations
        );
        debug!(" > descendants: {:?}", descendant_invalidations);
        debug!(
            " > siblings: {}, {:?}",
            sibling_invalidations.len(),
            sibling_invalidations
        );

        let invalidated_self_from_collection = invalidated_self;

        invalidated_self |= self.process_descendant_invalidations(
            &self_invalidations,
            &mut descendant_invalidations,
            &mut sibling_invalidations,
            DescendantInvalidationKind::Dom,
        );

        if invalidated_self && !invalidated_self_from_collection {
            self.processor.invalidated_self(self.element);
        }

        let invalidated_descendants = self.invalidate_descendants(&descendant_invalidations);
        let invalidated_siblings = self.invalidate_siblings(&mut sibling_invalidations);

        InvalidationResult {
            invalidated_self,
            invalidated_descendants,
            invalidated_siblings,
        }
    }

    /// Go through later DOM siblings, invalidating style as needed using the
    /// `sibling_invalidations` list.
    ///
    /// Returns whether any sibling's style or any sibling descendant's style
    /// was invalidated.
    fn invalidate_siblings(&mut self, sibling_invalidations: &mut InvalidationVector<'b>) -> bool {
        if sibling_invalidations.is_empty() {
            return false;
        }

        let mut current = self
            .processor
            .sibling_traversal_map()
            .next_sibling_for(&self.element);
        let mut any_invalidated = false;

        while let Some(sibling) = current {
            let mut sibling_invalidator =
                TreeStyleInvalidator::new(sibling, self.stack_limit_checker, self.processor);

            let mut invalidations_for_descendants = DescendantInvalidationLists::default();
            let invalidated_sibling = sibling_invalidator.process_sibling_invalidations(
                &mut invalidations_for_descendants,
                sibling_invalidations,
            );

            if invalidated_sibling {
                sibling_invalidator
                    .processor
                    .invalidated_sibling(sibling, self.element);
            }

            any_invalidated |= invalidated_sibling;

            any_invalidated |=
                sibling_invalidator.invalidate_descendants(&invalidations_for_descendants);

            if sibling_invalidations.is_empty() {
                break;
            }

            current = self
                .processor
                .sibling_traversal_map()
                .next_sibling_for(&sibling);
        }

        any_invalidated
    }

    fn invalidate_pseudo_element_or_nac(
        &mut self,
        child: E,
        invalidations: &[Invalidation<'b>],
    ) -> bool {
        let mut sibling_invalidations = InvalidationVector::new();

        // Roots of NAC subtrees can indeed generate sibling invalidations, but
        // they can be just ignored, since they have no siblings.
        //
        // Note that we can end up testing selectors that wouldn't end up
        // matching due to this being NAC, like those coming from document
        // rules, but we overinvalidate instead of checking this.

        self.invalidate_child(
            child,
            invalidations,
            &mut sibling_invalidations,
            DescendantInvalidationKind::Dom,
        )
    }

    /// Invalidate a child and recurse down invalidating its descendants if
    /// needed.
    fn invalidate_child(
        &mut self,
        child: E,
        invalidations: &[Invalidation<'b>],
        sibling_invalidations: &mut InvalidationVector<'b>,
        descendant_invalidation_kind: DescendantInvalidationKind,
    ) -> bool {
        let mut invalidations_for_descendants = DescendantInvalidationLists::default();

        let mut invalidated_child = false;
        let invalidated_descendants = {
            let mut child_invalidator =
                TreeStyleInvalidator::new(child, self.stack_limit_checker, self.processor);

            if !sibling_invalidations.is_empty() {
                invalidated_child |= child_invalidator.process_sibling_invalidations(
                    &mut invalidations_for_descendants,
                    sibling_invalidations,
                );
            }

            invalidated_child |= child_invalidator.process_descendant_invalidations(
                invalidations,
                &mut invalidations_for_descendants,
                sibling_invalidations,
                descendant_invalidation_kind,
            );

            if invalidated_child {
                child_invalidator.processor.invalidated_self(child);
            }

            child_invalidator.invalidate_descendants(&invalidations_for_descendants)
        };

        // The child may not be a flattened tree child of the current element,
        // but may be arbitrarily deep.
        //
        // Since we keep the traversal flags in terms of the flattened tree,
        // we need to propagate it as appropriate.
        if invalidated_child || invalidated_descendants {
            self.processor.invalidated_descendants(self.element, child);
        }

        invalidated_child || invalidated_descendants
    }

    fn invalidate_nac(&mut self, invalidations: &[Invalidation<'b>]) -> bool {
        let mut any_nac_root = false;

        let element = self.element;
        element.each_anonymous_content_child(|nac| {
            any_nac_root |= self.invalidate_pseudo_element_or_nac(nac, invalidations);
        });

        any_nac_root
    }

    // NB: It's important that this operates on DOM children, which is what
    // selector-matching operates on.
    fn invalidate_dom_descendants_of(
        &mut self,
        parent: E::ConcreteNode,
        invalidations: &[Invalidation<'b>],
    ) -> bool {
        let mut any_descendant = false;

        let mut sibling_invalidations = InvalidationVector::new();
        for child in parent.dom_children() {
            let child = match child.as_element() {
                Some(e) => e,
                None => continue,
            };

            any_descendant |= self.invalidate_child(
                child,
                invalidations,
                &mut sibling_invalidations,
                DescendantInvalidationKind::Dom,
            );
        }

        any_descendant
    }

    fn invalidate_parts_in_shadow_tree(
        &mut self,
        shadow: <E::ConcreteNode as TNode>::ConcreteShadowRoot,
        invalidations: &[Invalidation<'b>],
    ) -> bool {
        debug_assert!(!invalidations.is_empty());

        let mut any = false;
        let mut sibling_invalidations = InvalidationVector::new();

        for node in shadow.as_node().dom_descendants() {
            let element = match node.as_element() {
                Some(e) => e,
                None => continue,
            };

            if element.has_part_attr() {
                any |= self.invalidate_child(
                    element,
                    invalidations,
                    &mut sibling_invalidations,
                    DescendantInvalidationKind::Part,
                );
                debug_assert!(
                    sibling_invalidations.is_empty(),
                    "::part() shouldn't have sibling combinators to the right, \
                     this makes no sense! {:?}",
                    sibling_invalidations
                );
            }

            if let Some(shadow) = element.shadow_root()
                && element.exports_any_part()
            {
                any |= self.invalidate_parts_in_shadow_tree(shadow, invalidations)
            }
        }

        any
    }

    fn invalidate_parts(&mut self, invalidations: &[Invalidation<'b>]) -> bool {
        if invalidations.is_empty() {
            return false;
        }

        let shadow = match self.element.shadow_root() {
            Some(s) => s,
            None => return false,
        };

        self.invalidate_parts_in_shadow_tree(shadow, invalidations)
    }

    fn invalidate_slotted_elements(&mut self, invalidations: &[Invalidation<'b>]) -> bool {
        if invalidations.is_empty() {
            return false;
        }

        let slot = self.element;
        self.invalidate_slotted_elements_in_slot(slot, invalidations)
    }

    fn invalidate_slotted_elements_in_slot(
        &mut self,
        slot: E,
        invalidations: &[Invalidation<'b>],
    ) -> bool {
        let mut any = false;

        let mut sibling_invalidations = InvalidationVector::new();
        for node in slot.slotted_nodes() {
            let element = match node.as_element() {
                Some(e) => e,
                None => continue,
            };

            if element.is_html_slot_element() {
                any |= self.invalidate_slotted_elements_in_slot(element, invalidations);
            } else {
                any |= self.invalidate_child(
                    element,
                    invalidations,
                    &mut sibling_invalidations,
                    DescendantInvalidationKind::Slotted,
                );
            }

            debug_assert!(
                sibling_invalidations.is_empty(),
                "::slotted() shouldn't have sibling combinators to the right, \
                 this makes no sense! {:?}",
                sibling_invalidations
            );
        }

        any
    }

    fn invalidate_non_slotted_descendants(&mut self, invalidations: &[Invalidation<'b>]) -> bool {
        if invalidations.is_empty() {
            return false;
        }

        if self.processor.light_tree_only() {
            let node = self.element.as_node();
            return self.invalidate_dom_descendants_of(node, invalidations);
        }

        let mut any_descendant = false;

        // NOTE(emilio): This is only needed for Shadow DOM to invalidate
        // correctly on :host(..) changes. Instead of doing this, we could add
        // a third kind of invalidation list that walks shadow root children,
        // but it's not clear it's worth it.
        //
        // Also, it's needed as of right now for document state invalidation,
        // where we rely on iterating every element that ends up in the composed
        // doc, but we could fix that invalidating per subtree.
        if let Some(root) = self.element.shadow_root() {
            any_descendant |= self.invalidate_dom_descendants_of(root.as_node(), invalidations);
        }

        any_descendant |= self.invalidate_dom_descendants_of(self.element.as_node(), invalidations);

        any_descendant |= self.invalidate_nac(invalidations);

        any_descendant
    }

    /// Given the descendant invalidation lists, go through the current
    /// element's descendants, and invalidate style on them.
    fn invalidate_descendants(&mut self, invalidations: &DescendantInvalidationLists<'b>) -> bool {
        if invalidations.is_empty() {
            return false;
        }

        debug!(
            "StyleTreeInvalidator::invalidate_descendants({:?})",
            self.element
        );
        debug!(" > {:?}", invalidations);

        let should_process = self.processor.should_process_descendants(self.element);

        if !should_process {
            return false;
        }

        if let Some(checker) = self.stack_limit_checker
            && checker.limit_exceeded()
        {
            self.processor.recursion_limit_exceeded(self.element);
            return true;
        }

        let mut any_descendant = false;

        any_descendant |= self.invalidate_non_slotted_descendants(&invalidations.dom_descendants);
        any_descendant |= self.invalidate_slotted_elements(&invalidations.slotted_descendants);
        any_descendant |= self.invalidate_parts(&invalidations.parts);

        any_descendant
    }

    /// Process the given sibling invalidations coming from our previous
    /// sibling.
    ///
    /// The sibling invalidations are somewhat special because they can be
    /// modified on the fly. New invalidations may be added and removed.
    ///
    /// In particular, all descendants get the same set of invalidations from
    /// the parent, but the invalidations from a given sibling depend on the
    /// ones we got from the previous one.
    ///
    /// Returns whether invalidated the current element's style.
    ///
    /// Callers should skip this when `sibling_invalidations` is empty, as it
    /// builds and drains a `SmallVec` to do nothing.
    fn process_sibling_invalidations(
        &mut self,
        descendant_invalidations: &mut DescendantInvalidationLists<'b>,
        sibling_invalidations: &mut InvalidationVector<'b>,
    ) -> bool {
        let mut i = 0;
        let mut new_sibling_invalidations = InvalidationVector::new();
        let mut invalidated_self = false;

        while i < sibling_invalidations.len() {
            let result = self.process_invalidation(
                &sibling_invalidations[i],
                descendant_invalidations,
                &mut new_sibling_invalidations,
                InvalidationKind::Sibling,
            );

            invalidated_self |= result.invalidated_self;
            sibling_invalidations[i].matched_by_any_previous |= result.matched;
            if sibling_invalidations[i].effective_for_next() {
                i += 1;
            } else {
                sibling_invalidations.remove(i);
            }
        }

        sibling_invalidations.extend(new_sibling_invalidations.drain(..));
        invalidated_self
    }

    /// Process a given invalidation list coming from our parent,
    /// adding to `descendant_invalidations` and `sibling_invalidations` as
    /// needed.
    ///
    /// Returns whether our style was invalidated as a result.
    fn process_descendant_invalidations(
        &mut self,
        invalidations: &[Invalidation<'b>],
        descendant_invalidations: &mut DescendantInvalidationLists<'b>,
        sibling_invalidations: &mut InvalidationVector<'b>,
        descendant_invalidation_kind: DescendantInvalidationKind,
    ) -> bool {
        let mut invalidated = false;

        for invalidation in invalidations {
            let result = self.process_invalidation(
                invalidation,
                descendant_invalidations,
                sibling_invalidations,
                InvalidationKind::Descendant(descendant_invalidation_kind),
            );

            invalidated |= result.invalidated_self;
            if invalidation.effective_for_next() {
                let mut invalidation = invalidation.clone();
                invalidation.matched_by_any_previous |= result.matched;
                debug_assert_eq!(
                    descendant_invalidation_kind,
                    DescendantInvalidationKind::Dom,
                    "Slotted or part invalidations don't propagate."
                );
                descendant_invalidations.dom_descendants.push(invalidation);
            }
        }

        invalidated
    }

    #[inline(always)]
    fn handle_fully_matched(
        &mut self,
        invalidation: &Invalidation<'b>,
    ) -> (ProcessInvalidationResult, SmallVec<[Invalidation<'b>; 1]>) {
        debug!(" > Invalidation matched completely");
        // We matched completely. If we're an inner selector now we need
        // to go outside our selector and carry on invalidating.
        let mut to_process: SmallVec<[&Dependency; 1]> = SmallVec::from([invalidation.dependency]);
        let mut next_invalidations: SmallVec<[Invalidation; 1]> = SmallVec::new();
        let mut result = ProcessInvalidationResult {
            invalidated_self: false,
            matched: false,
        };

        while !to_process.is_empty() {
            let mut next_dependencies: SmallVec<[&Dependency; 1]> = SmallVec::new();

            while let Some(dependency) = to_process.pop() {
                if let DependencyInvalidationKind::Scope(scope_kind) =
                    dependency.invalidation_kind()
                {
                    if scope_kind == ScopeDependencyInvalidationKind::ImplicitScope {
                        if let Some(ref deps) = dependency.next {
                            for dep in deps.as_ref().slice() {
                                let invalidation =
                                    Invalidation::new_always_effective_for_next_descendant(
                                        dep,
                                        invalidation.host,
                                        invalidation.scope,
                                    );
                                next_invalidations.push(invalidation);
                            }
                        }
                        continue;
                    }

                    let force_add = any_next_has_scope_in_negation(dependency);
                    if scope_kind == ScopeDependencyInvalidationKind::ScopeEnd || force_add {
                        let invalidations = note_scope_dependency_force_at_subject(
                            dependency,
                            invalidation.host,
                            invalidation.scope,
                            force_add,
                        );

                        next_invalidations.extend(invalidations);

                        continue;
                    }
                }

                match dependency.next {
                    None => {
                        result.invalidated_self = true;
                        result.matched = true;
                    },
                    Some(ref deps) => {
                        for n in deps.as_ref().slice() {
                            let invalidation_kind = n.invalidation_kind();
                            match invalidation_kind {
                                DependencyInvalidationKind::FullSelector => unreachable!(),
                                DependencyInvalidationKind::Normal(_) => next_dependencies.push(n),
                                //TODO(descalente, bug 1934061): Add specific handling for implicit scopes.
                                DependencyInvalidationKind::Scope(_) => {
                                    next_dependencies.push(n);
                                },
                                DependencyInvalidationKind::Relative(kind) => {
                                    self.processor.found_relative_selector_invalidation(
                                        self.element,
                                        kind,
                                        n,
                                    );
                                    result.matched = true;
                                },
                            }
                        }
                    },
                };
            }

            for cur_dependency in next_dependencies.as_ref() {
                let scope = matches!(
                    invalidation.dependency.invalidation_kind(),
                    DependencyInvalidationKind::Scope(_)
                )
                .then(|| self.element.opaque());
                debug!(" > Checking outer dependency {:?}", cur_dependency);

                // The inner selector changed, now check if the full
                // previous part of the selector did, before keeping
                // checking for descendants.
                if !self
                    .processor
                    .check_outer_dependency(cur_dependency, self.element, scope)
                {
                    // Dependency is not relevant, do not note it down
                    continue;
                }

                let invalidation_kind = cur_dependency.invalidation_kind();
                if matches!(
                    invalidation_kind,
                    DependencyInvalidationKind::Normal(NormalDependencyInvalidationKind::Element)
                ) || (matches!(invalidation_kind, DependencyInvalidationKind::Scope(_))
                    && cur_dependency
                        .selector
                        .is_rightmost(cur_dependency.selector_offset))
                {
                    // Add to dependency stack to process its next dependencies.
                    to_process.push(cur_dependency);
                    continue;
                }

                debug!(" > Generating invalidation");
                next_invalidations.push(Invalidation::new(
                    cur_dependency,
                    invalidation.host,
                    scope,
                ));
            }
        }
        (result, next_invalidations)
    }

    /// Processes a given invalidation, potentially invalidating the style of
    /// the current element.
    ///
    /// Returns whether invalidated the style of the element, and whether the
    /// invalidation should be effective to subsequent siblings or descendants
    /// down in the tree.
    fn process_invalidation(
        &mut self,
        invalidation: &Invalidation<'b>,
        descendant_invalidations: &mut DescendantInvalidationLists<'b>,
        sibling_invalidations: &mut InvalidationVector<'b>,
        invalidation_kind: InvalidationKind,
    ) -> ProcessInvalidationResult {
        debug!(
            "TreeStyleInvalidator::process_invalidation({:?}, {:?}, {:?})",
            self.element, invalidation, invalidation_kind
        );

        let matching_result = {
            let context = self.processor.matching_context();
            context.current_host = invalidation.host;

            context.nest_for_scope_condition(invalidation.scope, |ctx| {
                matches_compound_selector_from(
                    &invalidation.dependency.selector,
                    invalidation.offset,
                    ctx,
                    self.element,
                )
            })
        };

        let (mut result, next_invalidations) = match matching_result {
            CompoundSelectorMatchingResult::NotMatched => {
                return ProcessInvalidationResult {
                    invalidated_self: false,
                    matched: false,
                };
            },
            CompoundSelectorMatchingResult::FullyMatched => self.handle_fully_matched(invalidation),
            CompoundSelectorMatchingResult::Matched {
                next_combinator_offset,
            } => (
                ProcessInvalidationResult {
                    invalidated_self: false,
                    matched: true,
                },
                smallvec![Invalidation {
                    dependency: invalidation.dependency,
                    host: invalidation.host,
                    scope: invalidation.scope,
                    offset: next_combinator_offset + 1,
                    matched_by_any_previous: false,
                    always_effective_for_next_descendant: invalidation
                        .always_effective_for_next_descendant,
                }],
            ),
        };

        for next_invalidation in next_invalidations {
            let next_invalidation_kind = if next_invalidation.always_effective_for_next_descendant {
                InvalidationKind::Descendant(DescendantInvalidationKind::Dom)
            } else {
                debug_assert_ne!(
                    next_invalidation.offset, 0,
                    "Rightmost selectors shouldn't generate more invalidations",
                );

                let next_combinator = next_invalidation
                    .dependency
                    .selector
                    .combinator_at_parse_order(next_invalidation.offset - 1);

                if matches!(next_combinator, Combinator::PseudoElement)
                    && self.processor.invalidates_on_pseudo_element()
                {
                    // We need to invalidate the element whenever pseudos change, for
                    // two reasons:
                    //
                    //  * Eager pseudo styles are stored as part of the originating
                    //    element's computed style.
                    //
                    //  * Lazy pseudo-styles might be cached on the originating
                    //    element's pseudo-style cache.
                    //
                    // This could be more fine-grained (perhaps with a RESTYLE_PSEUDOS
                    // hint?).
                    //
                    // Note that we'll also restyle the pseudo-element because it would
                    // match this invalidation.
                    result.invalidated_self = true;

                    // For highlight pseudos (::selection, ::highlight, ::target-text),
                    // we also need to trigger a repaint since their styles are resolved
                    // lazily during painting.
                    if next_invalidation
                        .dependency
                        .selector
                        .pseudo_element()
                        .is_some_and(|p| p.is_lazy_painted_highlight_pseudo())
                    {
                        self.processor.invalidated_highlight_pseudo(self.element);
                    }
                }

                debug!(
                    " > Invalidation matched, next: {:?}, ({:?})",
                    next_invalidation, next_combinator
                );

                next_invalidation.kind()
            };

            // We can skip pushing under some circumstances, and we should
            // because otherwise the invalidation list could grow
            // exponentially.
            //
            //  * First of all, both invalidations need to be of the same
            //    kind. This is because of how we propagate them going to
            //    the right of the tree for sibling invalidations and going
            //    down the tree for children invalidations. A sibling
            //    invalidation that ends up generating a children
            //    invalidation ends up (correctly) in five different lists,
            //    not in the same list five different times.
            //
            //  * Then, the invalidation needs to be matched by a previous
            //    ancestor/sibling, in order to know that this invalidation
            //    has been generated already.
            //
            //  * Finally, the new invalidation needs to be
            //    `effective_for_next()`, in order for us to know that it is
            //    still in the list, since we remove the dependencies that
            //    aren't from the lists for our children / siblings.
            //
            // To go through an example, let's imagine we are processing a
            // dom subtree like:
            //
            //   <div><address><div><div/></div></address></div>
            //
            // And an invalidation list with a single invalidation like:
            //
            //   [div div div]
            //
            // When we process the invalidation list for the outer div, we
            // match it, and generate a `div div` invalidation, so for the
            // <address> child we have:
            //
            //   [div div div, div div]
            //
            // With the first of them marked as `matched`.
            //
            // When we process the <address> child, we don't match any of
            // them, so both invalidations go untouched to our children.
            //
            // When we process the second <div>, we match _both_
            // invalidations.
            //
            // However, when matching the first, we can tell it's been
            // matched, and not push the corresponding `div div`
            // invalidation, since we know it's necessarily already on the
            // list.
            //
            // Thus, without skipping the push, we'll arrive to the
            // innermost <div> with:
            //
            //   [div div div, div div, div div, div]
            //
            // While skipping it, we won't arrive here with duplicating
            // dependencies:
            //
            //   [div div div, div div, div]
            //
            let can_skip_pushing = next_invalidation_kind == invalidation_kind
                && invalidation.matched_by_any_previous
                && next_invalidation.effective_for_next();

            if can_skip_pushing {
                debug!(
                    " > Can avoid push, since the invalidation had \
                    already been matched before"
                );
            } else {
                match next_invalidation_kind {
                    InvalidationKind::Descendant(DescendantInvalidationKind::Dom) => {
                        descendant_invalidations
                            .dom_descendants
                            .push(next_invalidation);
                    },
                    InvalidationKind::Descendant(DescendantInvalidationKind::Part) => {
                        descendant_invalidations.parts.push(next_invalidation);
                    },
                    InvalidationKind::Descendant(DescendantInvalidationKind::Slotted) => {
                        descendant_invalidations
                            .slotted_descendants
                            .push(next_invalidation);
                    },
                    InvalidationKind::Sibling => {
                        sibling_invalidations.push(next_invalidation);
                    },
                }
            }
        }

        result
    }
}

/// Note the child dependencies of a scope end selector
/// This is necessary because the scope end selector is not bound to :scope
///
/// e.g @scope to (.b) {:scope .a .c {...}}
/// in the case of the following:
/// <div class=a><div id=x class=b><div class=c></div></div></div>
///
/// If we toggle class "b" in x, we would have to go up to find .a
/// if we wanted to invalidate correctly. However, this is costly.
/// Instead we just invalidate to the subject of the selector .c
pub fn note_scope_dependency_force_at_subject<'selectors>(
    dependency: &'selectors Dependency,
    current_host: Option<OpaqueElement>,
    scope: Option<OpaqueElement>,
    traversed_non_subject: bool,
) -> Vec<Invalidation<'selectors>> {
    let mut invalidations: Vec<Invalidation> = Vec::new();
    if let Some(next) = dependency.next.as_ref() {
        for dep in next.slice() {
            if dep.selector.is_rightmost(dep.selector_offset) && !traversed_non_subject {
                continue;
            }

            // Follow the normal dependencies as far as we can, leaving
            // other kinds to their own invalidation mechanisms elsewhere
            if dep.next.is_some()
                && matches!(
                    dep.invalidation_kind(),
                    DependencyInvalidationKind::Normal(_)
                )
            {
                invalidations.extend(note_scope_dependency_force_at_subject(
                    dep,
                    current_host,
                    scope,
                    // Force add from now on because we
                    // passed through a non-subject compound
                    true,
                ));
            } else {
                let invalidation = Invalidation::new_subject_invalidation(dep, current_host, scope);

                invalidations.push(invalidation);
            }
        }
    }
    invalidations
}
