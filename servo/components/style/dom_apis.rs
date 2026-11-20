/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Generic implementations of some DOM APIs so they can be shared between Servo
//! and Gecko.

use crate::bloom::AtomExt as _;
use crate::context::QuirksMode;
use crate::dom::{TDocument, TElement, TNode, TShadowRoot};
use crate::invalidation::element::invalidation_map::Dependency;
use crate::invalidation::element::invalidator::{
    DescendantInvalidationLists, Invalidation, SiblingTraversalMap,
};
use crate::invalidation::element::invalidator::{InvalidationProcessor, InvalidationVector};
use crate::selector_parser::SelectorImpl;
use crate::values::AtomIdent;
use selectors::attr::CaseSensitivity;
use selectors::attr::{AttrSelectorOperation, NamespaceConstraint};
use selectors::matching::{
    self, MatchingContext, MatchingForInvalidation, MatchingMode, NeedsSelectorFlags,
    SelectorCaches,
};
use selectors::parser::{Combinator, Component, LocalName};
use selectors::subtree_filter::hash_for_subtree_filter;
use selectors::{Element, OpaqueElement, SelectorList};
use smallvec::SmallVec;

/// <https://dom.spec.whatwg.org/#dom-element-matches>
pub fn element_matches<E>(
    element: &E,
    selector_list: &SelectorList<E::Impl>,
    quirks_mode: QuirksMode,
) -> bool
where
    E: Element,
{
    let mut selector_caches = SelectorCaches::default();

    let mut context = MatchingContext::new(
        MatchingMode::Normal,
        None,
        &mut selector_caches,
        quirks_mode,
        NeedsSelectorFlags::No,
        MatchingForInvalidation::No,
    );
    context.scope_element = Some(element.opaque());
    context.current_host = element.containing_shadow_host().map(|e| e.opaque());
    matching::matches_selector_list(selector_list, *element, &mut context)
}

/// <https://dom.spec.whatwg.org/#dom-element-closest>
pub fn element_closest<E>(
    element: E,
    selector_list: &SelectorList<E::Impl>,
    quirks_mode: QuirksMode,
) -> Option<E>
where
    E: Element,
{
    let mut selector_caches = SelectorCaches::default();

    let mut context = MatchingContext::new(
        MatchingMode::Normal,
        None,
        &mut selector_caches,
        quirks_mode,
        NeedsSelectorFlags::No,
        MatchingForInvalidation::No,
    );
    context.scope_element = Some(element.opaque());
    context.current_host = element.containing_shadow_host().map(|e| e.opaque());

    let mut current = Some(element);
    while let Some(element) = current.take() {
        if matching::matches_selector_list(selector_list, element, &mut context) {
            return Some(element);
        }
        current = element.parent_element();
    }

    None
}

/// A selector query abstraction, in order to be generic over QuerySelector and
/// QuerySelectorAll.
pub trait SelectorQuery<E: TElement> {
    /// The output of the query.
    type Output;

    /// Whether the query should stop after the first element has been matched.
    fn should_stop_after_first_match() -> bool;

    /// Append an element matching after the first query.
    fn append_element(output: &mut Self::Output, element: E);

    /// Returns true if the output is empty.
    fn is_empty(output: &Self::Output) -> bool;
}

/// The result of a querySelectorAll call.
pub type QuerySelectorAllResult<E> = SmallVec<[E; 128]>;

/// A query for all the elements in a subtree.
pub struct QueryAll;

impl<E: TElement> SelectorQuery<E> for QueryAll {
    type Output = QuerySelectorAllResult<E>;

    fn should_stop_after_first_match() -> bool {
        false
    }

    fn append_element(output: &mut Self::Output, element: E) {
        output.push(element);
    }

    fn is_empty(output: &Self::Output) -> bool {
        output.is_empty()
    }
}

/// A query for the first in-tree match of all the elements in a subtree.
pub struct QueryFirst;

impl<E: TElement> SelectorQuery<E> for QueryFirst {
    type Output = Option<E>;

    fn should_stop_after_first_match() -> bool {
        true
    }

    fn append_element(output: &mut Self::Output, element: E) {
        if output.is_none() {
            *output = Some(element)
        }
    }

    fn is_empty(output: &Self::Output) -> bool {
        output.is_none()
    }
}

struct QuerySelectorProcessor<'a, 'b, E, Q>
where
    E: TElement + 'a,
    Q: SelectorQuery<E>,
    Q::Output: 'a,
{
    results: &'a mut Q::Output,
    matching_context: MatchingContext<'b, E::Impl>,
    traversal_map: SiblingTraversalMap<E>,
    dependencies: &'a [Dependency],
}

impl<'a, 'b, E, Q> InvalidationProcessor<'a, 'b, E> for QuerySelectorProcessor<'a, 'b, E, Q>
where
    E: TElement + 'a,
    Q: SelectorQuery<E>,
    Q::Output: 'a,
{
    fn light_tree_only(&self) -> bool {
        true
    }

    fn check_outer_dependency(&mut self, _: &Dependency, _: E, _: Option<OpaqueElement>) -> bool {
        debug_assert!(
            false,
            "How? We should only have parent-less dependencies here!"
        );
        true
    }

    fn collect_invalidations(
        &mut self,
        element: E,
        self_invalidations: &mut InvalidationVector<'a>,
        descendant_invalidations: &mut DescendantInvalidationLists<'a>,
        _sibling_invalidations: &mut InvalidationVector<'a>,
    ) -> bool {
        // TODO(emilio): If the element is not a root element, and
        // selector_list has any descendant combinator, we need to do extra work
        // in order to handle properly things like:
        //
        //   <div id="a">
        //     <div id="b">
        //       <div id="c"></div>
        //     </div>
        //   </div>
        //
        // b.querySelector('#a div'); // Should return "c".
        //
        // For now, assert it's a root element.
        debug_assert!(element.parent_element().is_none());

        let target_vector = if self.matching_context.scope_element.is_some() {
            &mut descendant_invalidations.dom_descendants
        } else {
            self_invalidations
        };

        for dependency in self.dependencies.iter() {
            target_vector.push(Invalidation::new(
                dependency,
                self.matching_context.current_host,
                self.matching_context.scope_element,
            ))
        }

        false
    }

    fn matching_context(&mut self) -> &mut MatchingContext<'b, E::Impl> {
        &mut self.matching_context
    }

    fn sibling_traversal_map(&self) -> &SiblingTraversalMap<E> {
        &self.traversal_map
    }

    fn should_process_descendants(&mut self, _: E) -> bool {
        if Q::should_stop_after_first_match() {
            return Q::is_empty(self.results);
        }

        true
    }

    fn invalidated_self(&mut self, e: E) {
        Q::append_element(self.results, e);
    }

    fn invalidated_sibling(&mut self, e: E, _of: E) {
        Q::append_element(self.results, e);
    }

    fn recursion_limit_exceeded(&mut self, _e: E) {}
    fn invalidated_descendants(&mut self, _e: E, _child: E) {}
}

enum Operation {
    Reject,
    Accept,
    RejectSkippingChildren,
}

impl From<bool> for Operation {
    #[inline(always)]
    fn from(matches: bool) -> Self {
        if matches {
            Operation::Accept
        } else {
            Operation::Reject
        }
    }
}

fn collect_all_elements<E, Q, F>(root: E::ConcreteNode, results: &mut Q::Output, mut filter: F)
where
    E: TElement,
    Q: SelectorQuery<E>,
    F: FnMut(E) -> Operation,
{
    let mut iter = root.dom_descendants();
    let mut cur = iter.next();
    while let Some(node) = cur {
        let element = match node.as_element() {
            Some(e) => e,
            None => {
                cur = iter.next();
                continue;
            },
        };
        match filter(element) {
            // Element matches - add to results and continue traversing its children.
            Operation::Accept => {
                Q::append_element(results, element);
                if Q::should_stop_after_first_match() {
                    return;
                }
            },
            // Element doesn't match - skip it but continue traversing its children.
            Operation::Reject => {},
            // Element doesn't match and skip entire subtree.
            Operation::RejectSkippingChildren => {
                cur = iter.next_skipping_children();
                continue;
            },
        }
        cur = iter.next();
    }
}

/// Returns whether a given element connected to `root` is descendant of `root`.
///
/// NOTE(emilio): if root == element, this returns false.
fn connected_element_is_descendant_of<E>(element: E, root: E::ConcreteNode) -> bool
where
    E: TElement,
{
    // Optimize for when the root is a document or a shadow root and the element
    // is connected to that root.
    if root.as_document().is_some() {
        debug_assert!(element.as_node().is_in_document(), "Not connected?");
        debug_assert_eq!(
            root,
            root.owner_doc().as_node(),
            "Where did this element come from?",
        );
        return true;
    }

    if root.as_shadow_root().is_some() {
        debug_assert_eq!(
            element.containing_shadow().unwrap().as_node(),
            root,
            "Not connected?"
        );
        return true;
    }

    let mut current = element.as_node().parent_node();
    while let Some(n) = current.take() {
        if n == root {
            return true;
        }

        current = n.parent_node();
    }
    false
}

/// Fast path for iterating over every element with a given id in the document
/// or shadow root that `root` is connected to.
fn fast_connected_elements_with_id<'a, N>(
    root: N,
    id: &AtomIdent,
    case_sensitivity: CaseSensitivity,
) -> Result<&'a [N::ConcreteElement], ()>
where
    N: TNode + 'a,
{
    if case_sensitivity != CaseSensitivity::CaseSensitive {
        return Err(());
    }

    if root.is_in_document() {
        return root.owner_doc().elements_with_id(id);
    }

    if let Some(shadow) = root.as_shadow_root() {
        return shadow.elements_with_id(id);
    }

    if let Some(shadow) = root.as_element().and_then(|e| e.containing_shadow()) {
        return shadow.elements_with_id(id);
    }

    Err(())
}

/// Collects elements with a given id under `root`, that pass `filter`.
fn collect_elements_with_id<E, Q, F>(
    root: E::ConcreteNode,
    id: &AtomIdent,
    results: &mut Q::Output,
    class_and_id_case_sensitivity: CaseSensitivity,
    mut filter: F,
) where
    E: TElement,
    Q: SelectorQuery<E>,
    F: FnMut(E) -> bool,
{
    let elements = match fast_connected_elements_with_id(root, id, class_and_id_case_sensitivity) {
        Ok(elements) => elements,
        Err(()) => {
            collect_all_elements::<E, Q, _>(root, results, |e| {
                Operation::from(e.has_id(id, class_and_id_case_sensitivity) && filter(e))
            });

            return;
        },
    };

    for element in elements {
        // If the element is not an actual descendant of the root, even though
        // it's connected, we don't really care about it.
        if !connected_element_is_descendant_of(*element, root) {
            continue;
        }

        if !filter(*element) {
            continue;
        }

        Q::append_element(results, *element);
        if Q::should_stop_after_first_match() {
            break;
        }
    }
}

fn get_attr_name(component: &Component<SelectorImpl>) -> Option<&crate::LocalName> {
    let (name, name_lower) = match component {
        Component::AttributeInNoNamespace { local_name, .. } => return Some(local_name),
        Component::AttributeInNoNamespaceExists {
            local_name,
            local_name_lower,
            ..
        } => (local_name, local_name_lower),
        Component::AttributeOther(attr) => {
            if attr.namespace.is_some() {
                return None;
            }
            (&attr.local_name, &attr.local_name_lower)
        },
        _ => return None,
    };
    if name != name_lower {
        return None; // TODO: Maybe optimize this?
    }
    Some(name)
}

fn get_id(component: &Component<SelectorImpl>) -> Option<&AtomIdent> {
    use selectors::attr::AttrSelectorOperator;
    Some(match component {
        Component::ID(id) => id,
        Component::AttributeInNoNamespace {
            operator,
            local_name,
            value,
            ..
        } => {
            if *local_name != local_name!("id") {
                return None;
            }
            if *operator != AttrSelectorOperator::Equal {
                return None;
            }
            AtomIdent::cast(&value.0)
        },
        _ => return None,
    })
}

/// Fast paths for querySelector with a single simple selector.
fn query_selector_single_query<E, Q>(
    root: E::ConcreteNode,
    component: &Component<E::Impl>,
    results: &mut Q::Output,
    class_and_id_case_sensitivity: CaseSensitivity,
) -> Result<(), ()>
where
    E: TElement,
    Q: SelectorQuery<E>,
{
    match *component {
        Component::ExplicitUniversalType => {
            collect_all_elements::<E, Q, _>(root, results, |_| Operation::Accept)
        },
        Component::Class(ref class) => {
            // Bloom filter can only be used when case sensitive.
            let bloom_hash = if class_and_id_case_sensitivity == CaseSensitivity::CaseSensitive {
                Some(hash_for_subtree_filter(class.0.get_hash32()))
            } else {
                None
            };

            collect_all_elements::<E, Q, _>(root, results, |element| {
                if bloom_hash.is_some_and(|hash| !element.subtree_may_have_hashes(hash)) {
                    return Operation::RejectSkippingChildren;
                }
                Operation::from(element.has_class(class, class_and_id_case_sensitivity))
            });
        },
        Component::LocalName(ref local_name) => {
            let hash = hash_for_subtree_filter(local_name.name.0.get_hash32());
            let hash_lower = if local_name.name == local_name.lower_name {
                hash
            } else {
                hash_for_subtree_filter(local_name.lower_name.0.get_hash32())
            };
            collect_all_elements::<E, Q, _>(root, results, |element| {
                if !element.subtree_may_have_hashes(hash)
                    && (hash == hash_lower || !element.subtree_may_have_hashes(hash_lower))
                {
                    return Operation::RejectSkippingChildren;
                }
                Operation::from(
                    *element.local_name()
                        == ***matching::select_name(
                            element,
                            &local_name.name,
                            &local_name.lower_name,
                        ),
                )
            })
        },
        Component::AttributeInNoNamespaceExists {
            ref local_name,
            ref local_name_lower,
        } => {
            // For HTML elements: C++ hashes lowercase
            // For XUL/SVG/MathML elements: C++ hashes original case
            let hash_original = hash_for_subtree_filter(local_name.0.get_hash32());
            let hash_lower = if local_name.0 == local_name_lower.0 {
                hash_original
            } else {
                hash_for_subtree_filter(local_name_lower.0.get_hash32())
            };

            collect_all_elements::<E, Q, _>(root, results, |element| {
                // Check bloom filter first
                let bloom_found_hash = if hash_original == hash_lower
                    || !element.as_node().owner_doc().is_html_document()
                {
                    element.subtree_may_have_hashes(hash_original)
                } else if element.is_html_element_in_html_document() {
                    // HTML elements store lowercase hashes
                    element.subtree_may_have_hashes(hash_lower)
                } else {
                    // Non-HTML elements in HTML documents might have HTML descendants
                    // with lowercase-only hashes, so check both
                    element.subtree_may_have_hashes(hash_original)
                        || element.subtree_may_have_hashes(hash_lower)
                };

                if !bloom_found_hash {
                    return Operation::RejectSkippingChildren;
                }

                Operation::from(element.has_attr_in_no_namespace(matching::select_name(
                    element,
                    local_name,
                    local_name_lower,
                )))
            });
        },
        Component::AttributeInNoNamespace {
            ref local_name,
            ref value,
            operator,
            case_sensitivity,
        } => {
            let empty_namespace = selectors::parser::namespace_empty_string::<E::Impl>();
            let namespace_constraint = NamespaceConstraint::Specific(&empty_namespace);

            // Only use bloom filter to check for attribute name existence.
            let bloom_hash = hash_for_subtree_filter(local_name.0.get_hash32());

            collect_all_elements::<E, Q, _>(root, results, |element| {
                if !element.subtree_may_have_hashes(bloom_hash) {
                    return Operation::RejectSkippingChildren;
                }
                Operation::from(element.attr_matches(
                    &namespace_constraint,
                    local_name,
                    &AttrSelectorOperation::WithValue {
                        operator,
                        case_sensitivity: matching::to_unconditional_case_sensitivity(
                            case_sensitivity,
                            element,
                        ),
                        value,
                    },
                ))
            });
        },
        ref other => {
            let id = match get_id(other) {
                Some(id) => id,
                // TODO(emilio): More fast paths?
                None => return Err(()),
            };
            collect_elements_with_id::<E, Q, _>(
                root,
                id,
                results,
                class_and_id_case_sensitivity,
                |_| true,
            );
        },
    }

    Ok(())
}

enum SimpleFilter<'a> {
    Class(&'a AtomIdent),
    Attr(&'a crate::LocalName),
    LocalName(&'a LocalName<SelectorImpl>),
}

/// Fast paths for a given selector query.
///
/// When there's only one component, we go directly to
/// `query_selector_single_query`, otherwise, we try to optimize by looking just
/// at the subtrees rooted at ids in the selector, and otherwise we try to look
/// up by class name or local name in the rightmost compound.
///
/// FIXME(emilio, nbp): This may very well be a good candidate for code to be
/// replaced by HolyJit :)
fn query_selector_fast<E, Q>(
    root: E::ConcreteNode,
    selector_list: &SelectorList<E::Impl>,
    results: &mut Q::Output,
    matching_context: &mut MatchingContext<E::Impl>,
) -> Result<(), ()>
where
    E: TElement,
    Q: SelectorQuery<E>,
{
    // We need to return elements in document order, and reordering them
    // afterwards is kinda silly.
    if selector_list.len() > 1 {
        return Err(());
    }

    let selector = &selector_list.slice()[0];
    let class_and_id_case_sensitivity = matching_context.classes_and_ids_case_sensitivity();
    // Let's just care about the easy cases for now.
    if selector.len() == 1
        && query_selector_single_query::<E, Q>(
            root,
            selector.iter().next().unwrap(),
            results,
            class_and_id_case_sensitivity,
        )
        .is_ok()
    {
        return Ok(());
    }

    let mut iter = selector.iter();
    let mut combinator: Option<Combinator> = None;

    // We want to optimize some cases where there's no id involved whatsoever,
    // like `.foo .bar`, but we don't want to make `#foo .bar` slower because of
    // that.
    let mut simple_filter = None;

    'selector_loop: loop {
        debug_assert!(combinator.is_none_or(|c| !c.is_sibling()));

        'component_loop: for component in &mut iter {
            match *component {
                Component::Class(ref class) => {
                    if combinator.is_none() {
                        simple_filter = Some(SimpleFilter::Class(class));
                    }
                },
                Component::LocalName(ref local_name) => {
                    if combinator.is_none() {
                        // Prefer to look at class rather than local-name if
                        // both are present.
                        if let Some(SimpleFilter::Class(..)) = simple_filter {
                            continue;
                        }
                        simple_filter = Some(SimpleFilter::LocalName(local_name));
                    }
                },
                ref other => {
                    if let Some(id) = get_id(other) {
                        if combinator.is_none() {
                            // In the rightmost compound, just find descendants of root that match
                            // the selector list with that id.
                            collect_elements_with_id::<E, Q, _>(
                                root,
                                id,
                                results,
                                class_and_id_case_sensitivity,
                                |e| {
                                    matching::matches_selector_list(
                                        selector_list,
                                        e,
                                        matching_context,
                                    )
                                },
                            );
                            return Ok(());
                        }

                        let elements = fast_connected_elements_with_id(
                            root,
                            id,
                            class_and_id_case_sensitivity,
                        )?;
                        if elements.is_empty() {
                            return Ok(());
                        }

                        // Results need to be in document order. Let's not bother
                        // reordering or deduplicating nodes, which we would need to
                        // do if one element with the given id were a descendant of
                        // another element with that given id.
                        if !Q::should_stop_after_first_match() && elements.len() > 1 {
                            continue;
                        }

                        for element in elements {
                            // If the element is not a descendant of the root, then
                            // it may have descendants that match our selector that
                            // _are_ descendants of the root, and other descendants
                            // that match our selector that are _not_.
                            //
                            // So we can't just walk over the element's descendants
                            // and match the selector against all of them, nor can
                            // we skip looking at this element's descendants.
                            //
                            // Give up on trying to optimize based on this id and
                            // keep walking our selector.
                            if !connected_element_is_descendant_of(*element, root) {
                                continue 'component_loop;
                            }

                            query_selector_slow::<E, Q>(
                                element.as_node(),
                                selector_list,
                                results,
                                matching_context,
                            );

                            if Q::should_stop_after_first_match() && !Q::is_empty(results) {
                                break;
                            }
                        }

                        return Ok(());
                    }
                    if combinator.is_none()
                        && simple_filter.is_none()
                        && let Some(attr_name) = get_attr_name(other)
                    {
                        simple_filter = Some(SimpleFilter::Attr(attr_name));
                    }
                },
            }
        }

        loop {
            let next_combinator = match iter.next_sequence() {
                None => break 'selector_loop,
                Some(c) => c,
            };

            // We don't want to scan stuff affected by sibling combinators,
            // given we scan the subtree of elements with a given id (and we
            // don't want to care about scanning the siblings' subtrees).
            if next_combinator.is_sibling() {
                // Advance to the next combinator.
                for _ in &mut iter {}
                continue;
            }

            combinator = Some(next_combinator);
            break;
        }
    }

    // We got here without finding any ID or such that we could handle. Try to
    // use one of the simple filters.
    let simple_filter = match simple_filter {
        Some(f) => f,
        None => return Err(()),
    };

    match simple_filter {
        SimpleFilter::Class(class) => {
            // Bloom filter can only be used when case sensitive.
            let bloom_hash = if class_and_id_case_sensitivity == CaseSensitivity::CaseSensitive {
                Some(hash_for_subtree_filter(class.0.get_hash32()))
            } else {
                None
            };
            collect_all_elements::<E, Q, _>(root, results, |element| {
                if bloom_hash.is_some_and(|hash| !element.subtree_may_have_hashes(hash)) {
                    return Operation::RejectSkippingChildren;
                }
                Operation::from(
                    element.has_class(class, class_and_id_case_sensitivity)
                        && matching::matches_selector_list(
                            selector_list,
                            element,
                            matching_context,
                        ),
                )
            });
        },
        SimpleFilter::LocalName(local_name) => {
            let hash = hash_for_subtree_filter(local_name.name.0.get_hash32());
            let hash_lower = if local_name.name == local_name.lower_name {
                hash
            } else {
                hash_for_subtree_filter(local_name.lower_name.0.get_hash32())
            };
            collect_all_elements::<E, Q, _>(root, results, |element| {
                if !element.subtree_may_have_hashes(hash)
                    && (hash == hash_lower || !element.subtree_may_have_hashes(hash_lower))
                {
                    return Operation::RejectSkippingChildren;
                }
                if *element.local_name()
                    != ***matching::select_name(element, &local_name.name, &local_name.lower_name)
                {
                    return Operation::Reject;
                }
                Operation::from(matching::matches_selector_list(
                    selector_list,
                    element,
                    matching_context,
                ))
            });
        },
        SimpleFilter::Attr(local_name) => {
            let hash = hash_for_subtree_filter(local_name.0.get_hash32());
            collect_all_elements::<E, Q, _>(root, results, |element| {
                if !element.subtree_may_have_hashes(hash) {
                    return Operation::RejectSkippingChildren;
                }
                if !element.has_attr_in_no_namespace(local_name) {
                    return Operation::Reject;
                }
                Operation::from(matching::matches_selector_list(
                    selector_list,
                    element,
                    matching_context,
                ))
            });
        },
    }

    Ok(())
}

// Slow path for a given selector query.
fn query_selector_slow<E, Q>(
    root: E::ConcreteNode,
    selector_list: &SelectorList<E::Impl>,
    results: &mut Q::Output,
    matching_context: &mut MatchingContext<E::Impl>,
) where
    E: TElement,
    Q: SelectorQuery<E>,
{
    collect_all_elements::<E, Q, _>(root, results, |element| {
        Operation::from(matching::matches_selector_list(
            selector_list,
            element,
            matching_context,
        ))
    });
}

/// Whether the invalidation machinery should be used for this query.
#[derive(PartialEq)]
pub enum MayUseInvalidation {
    /// We may use it if we deem it useful.
    Yes,
    /// Don't use it.
    No,
}

/// <https://dom.spec.whatwg.org/#dom-parentnode-queryselector>
pub fn query_selector<E, Q>(
    root: E::ConcreteNode,
    selector_list: &SelectorList<E::Impl>,
    results: &mut Q::Output,
    may_use_invalidation: MayUseInvalidation,
) where
    E: TElement,
    Q: SelectorQuery<E>,
{
    use crate::invalidation::element::invalidator::TreeStyleInvalidator;

    let mut selector_caches = SelectorCaches::default();
    let quirks_mode = root.owner_doc().quirks_mode();

    let mut matching_context = MatchingContext::new(
        MatchingMode::Normal,
        None,
        &mut selector_caches,
        quirks_mode,
        NeedsSelectorFlags::No,
        MatchingForInvalidation::No,
    );
    let root_element = root.as_element();
    matching_context.scope_element = root_element.map(|e| e.opaque());
    matching_context.current_host = match root_element {
        Some(root) => root.containing_shadow_host().map(|host| host.opaque()),
        None => root.as_shadow_root().map(|root| root.host().opaque()),
    };

    let fast_result =
        query_selector_fast::<E, Q>(root, selector_list, results, &mut matching_context);

    if fast_result.is_ok() {
        return;
    }

    // Slow path: Use the invalidation machinery if we're a root, and tree
    // traversal otherwise.
    //
    // See the comment in collect_invalidations to see why only if we're a root.
    //
    // The invalidation mechanism is only useful in presence of combinators.
    //
    // We could do that check properly here, though checking the length of the
    // selectors is a good heuristic.
    //
    // A selector with a combinator needs to have a length of at least 3: A
    // simple selector, a combinator, and another simple selector.
    let invalidation_may_be_useful = may_use_invalidation == MayUseInvalidation::Yes
        && selector_list.slice().iter().any(|s| s.len() > 2);

    if root_element.is_some() || !invalidation_may_be_useful {
        query_selector_slow::<E, Q>(root, selector_list, results, &mut matching_context);
    } else {
        let dependencies = selector_list
            .slice()
            .iter()
            .map(|selector| Dependency::for_full_selector_invalidation(selector.clone()))
            .collect::<SmallVec<[_; 5]>>();
        let mut processor = QuerySelectorProcessor::<E, Q> {
            results,
            matching_context,
            traversal_map: SiblingTraversalMap::default(),
            dependencies: &dependencies,
        };

        for node in root.dom_children() {
            if let Some(e) = node.as_element() {
                TreeStyleInvalidator::new(e, /* stack_limit_checker = */ None, &mut processor)
                    .invalidate();
            }
        }
    }
}
