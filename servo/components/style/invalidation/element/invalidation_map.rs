/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Code for invalidations due to state or attribute changes.

use crate::AllocErr;
use crate::context::QuirksMode;
use crate::derives::*;
use crate::selector_map::{
    MaybeCaseInsensitiveHashMap, PrecomputedHashMap, SelectorMap, SelectorMapEntry,
};
use crate::selector_parser::{NonTSPseudoClass, SelectorImpl};
use crate::values::AtomIdent;
use crate::{Atom, LocalName, Namespace, ShrinkIfNeeded};
use dom::{DocumentState, ElementState};
use selectors::attr::NamespaceConstraint;
use selectors::parser::{
    Combinator, Component, RelativeSelector, RelativeSelectorCombinatorCount,
    RelativeSelectorMatchHint,
};
use selectors::parser::{Selector, SelectorIter};
use selectors::visitor::{SelectorListKind, SelectorVisitor};
use servo_arc::ThinArc;
use smallvec::SmallVec;

/// Mapping between (partial) CompoundSelectors (and the combinator to their
/// right) and the states and attributes they depend on.
///
/// In general, for all selectors in all applicable stylesheets of the form:
///
/// |a _ b _ c _ d _ e|
///
/// Where:
///   * |b| and |d| are simple selectors that depend on state (like :hover) or
///     attributes (like [attr...], .foo, or #foo).
///   * |a|, |c|, and |e| are arbitrary simple selectors that do not depend on
///     state or attributes.
///
/// We generate a Dependency for both |a _ b:X _| and |a _ b:X _ c _ d:Y _|,
/// even though those selectors may not appear on their own in any stylesheet.
/// This allows us to quickly scan through the dependency sites of all style
/// rules and determine the maximum effect that a given state or attribute
/// change may have on the style of elements in the document.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct Dependency {
    /// The dependency selector.
    #[ignore_malloc_size_of = "CssRules have primary refs, we measure there"]
    pub selector: Selector<SelectorImpl>,

    /// The offset into the selector that we should match on.
    pub selector_offset: usize,

    /// The next dependency for a selector chain. For example, consider
    /// the following:
    ///
    ///     .foo .bar:where(.baz span) .qux
    ///         ^               ^     ^
    ///         A               B     C
    ///
    ///  We'd generate:
    ///
    ///    * One dependency for .qux (offset: 0, next: None)
    ///    * One dependency for .baz pointing to B with next being a
    ///      dependency pointing to C.
    ///    * One dependency from .bar pointing to C (next: None)
    ///    * One dependency from .foo pointing to A (next: None)
    ///
    /// Scope blocks can add multiple next entries: e.g. With
    /// @scope (.a) { .b {/*...*/ } .c { /*...*/ }}
    /// .a's Dependency would have two entries, for .b and .c.
    #[ignore_malloc_size_of = "Arc"]
    pub next: Option<ThinArc<(), Dependency>>,

    /// What kind of selector invalidation this generates.
    kind: DependencyInvalidationKind,
}

impl SelectorMapEntry for Dependency {
    fn selector(&self) -> SelectorIter<'_, SelectorImpl> {
        self.selector.iter_from(self.selector_offset)
    }
}

/// The kind of elements down the tree this dependency may affect.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, MallocSizeOf)]
pub enum NormalDependencyInvalidationKind {
    /// This dependency may affect the element that changed itself.
    Element,
    /// This dependency affects the style of the element itself, and also the
    /// style of its descendants.
    ///
    /// TODO(emilio): Each time this feels more of a hack for eager pseudos...
    ElementAndDescendants,
    /// This dependency may affect descendants down the tree.
    Descendants,
    /// This dependency may affect siblings to the right of the element that
    /// changed.
    Siblings,
    /// This dependency may affect slotted elements of the element that changed.
    SlottedElements,
    /// This dependency may affect parts of the element that changed.
    Parts,
}

/// The kind of elements up the tree this relative selector dependency may
/// affect. Because this travels upwards, it's not viable for parallel subtree
/// traversal, and is handled separately.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, MallocSizeOf)]
pub enum RelativeDependencyInvalidationKind {
    /// This dependency may affect relative selector anchors for ancestors.
    Ancestors,
    /// This dependency may affect a relative selector anchor for the parent.
    Parent,
    /// This dependency may affect a relative selector anchor for the previous sibling.
    PrevSibling,
    /// This dependency may affect relative selector anchors for ancestors' previous siblings.
    AncestorPrevSibling,
    /// This dependency may affect relative selector anchors for earlier siblings.
    EarlierSibling,
    /// This dependency may affect relative selector anchors for ancestors' earlier siblings.
    AncestorEarlierSibling,
}

/// The kind of invalidation the subject of this dependency triggers.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, MallocSizeOf)]
pub enum ScopeDependencyInvalidationKind {
    /// This dependency's subject is an explicit scope root
    ExplicitScope,
    /// This dependency's subject is an implicit scope root
    ImplicitScope,
    /// This dependency's subject is an end scope condition
    ScopeEnd,
}

/// Invalidation kind merging normal and relative dependencies.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, MallocSizeOf)]
pub enum DependencyInvalidationKind {
    /// This dependency is for full selector invalidation.
    /// It is assuumed that there will be no next dependency to look for.
    FullSelector,
    /// This dependency is a normal dependency.
    Normal(NormalDependencyInvalidationKind),
    /// This dependency is a relative dependency.
    Relative(RelativeDependencyInvalidationKind),
    /// This dependency is a scope dependency.
    Scope(ScopeDependencyInvalidationKind),
}

/// The type of invalidation a non-relative selector can generate.
#[derive(Clone, Copy, Debug, MallocSizeOf)]
pub enum GeneratedInvalidation<'a> {
    /// Generates a normal invalidation.
    Normal,
    /// Generates a scope invalidation.
    Scope(Option<&'a ThinArc<(), Dependency>>),
}

/// Return the type of normal invalidation given a selector & an offset.
#[inline(always)]
fn get_non_relative_invalidation_kind(
    selector: &Selector<SelectorImpl>,
    selector_offset: usize,
    scope_kind: Option<ScopeDependencyInvalidationKind>,
) -> DependencyInvalidationKind {
    if let Some(kind) = scope_kind {
        return DependencyInvalidationKind::Scope(kind);
    }
    if selector_offset == 0 {
        return DependencyInvalidationKind::Normal(NormalDependencyInvalidationKind::Element);
    }
    let combinator = selector.combinator_at_match_order(selector_offset - 1);
    DependencyInvalidationKind::Normal(match combinator {
        Combinator::Child | Combinator::Descendant => NormalDependencyInvalidationKind::Descendants,
        Combinator::LaterSibling | Combinator::NextSibling => {
            NormalDependencyInvalidationKind::Siblings
        },
        Combinator::PseudoElement => NormalDependencyInvalidationKind::ElementAndDescendants,
        Combinator::SlotAssignment => NormalDependencyInvalidationKind::SlottedElements,
        Combinator::Part => NormalDependencyInvalidationKind::Parts,
    })
}

impl Dependency {
    /// Generate a new dependency
    pub fn new(
        selector: Selector<SelectorImpl>,
        selector_offset: usize,
        next: Option<ThinArc<(), Dependency>>,
        kind: DependencyInvalidationKind,
    ) -> Self {
        Self {
            selector,
            selector_offset,
            next,
            kind,
        }
    }
    /// Creates a dummy dependency to invalidate the whole selector.
    ///
    /// This is necessary because document state invalidation wants to
    /// invalidate all elements in the document.
    ///
    /// The offset is such as that Invalidation::new(self) returns a zero
    /// offset. That is, it points to a virtual "combinator" outside of the
    /// selector, so calling combinator() on such a dependency will panic.
    pub fn for_full_selector_invalidation(selector: Selector<SelectorImpl>) -> Self {
        Self {
            selector_offset: selector.len() + 1,
            selector,
            next: None,
            kind: DependencyInvalidationKind::FullSelector,
        }
    }

    /// The kind of normal invalidation that this would generate. The dependency
    /// in question must be a normal dependency.
    pub fn normal_invalidation_kind(&self) -> NormalDependencyInvalidationKind {
        if let DependencyInvalidationKind::Normal(kind) = self.kind {
            return kind;
        }
        unreachable!("Querying normal invalidation kind on non-normal dependency.");
    }

    /// The kind of relative invalidation that this would generate. The dependency
    /// in question must be a relative dependency.
    #[inline(always)]
    pub fn relative_invalidation_kind(&self) -> RelativeDependencyInvalidationKind {
        if let DependencyInvalidationKind::Relative(kind) = self.kind {
            return kind;
        }
        unreachable!("Querying relative invalidation kind on non-relative dependency.");
    }

    /// The kind of invalidation that this would generate.
    pub fn invalidation_kind(&self) -> DependencyInvalidationKind {
        self.kind
    }

    /// Is the combinator to the right of this dependency's compound selector
    /// the next sibling combinator? This matters for insertion/removal in between
    /// two elements connected through next sibling, e.g. `.foo:has(> .a + .b)`
    /// where an element gets inserted between `.a` and `.b`.
    pub fn right_combinator_is_next_sibling(&self) -> bool {
        if self.selector_offset == 0 {
            return false;
        }
        matches!(
            self.selector
                .combinator_at_match_order(self.selector_offset - 1),
            Combinator::NextSibling
        )
    }

    /// Is this dependency's compound selector a single compound in `:has`
    /// with the next sibling relative combinator i.e. `:has(> .foo)`?
    /// This matters for insertion between an anchor and an element
    /// connected through next sibling, e.g. `.a:has(> .b)`.
    pub fn dependency_is_relative_with_single_next_sibling(&self) -> bool {
        match self.invalidation_kind() {
            DependencyInvalidationKind::Relative(kind) => {
                kind == RelativeDependencyInvalidationKind::PrevSibling
            },
            _ => false,
        }
    }
}

/// The same, but for state selectors, which can track more exactly what state
/// do they track.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct StateDependency {
    /// The other dependency fields.
    pub dep: Dependency,
    /// The state this dependency is affected by.
    pub state: ElementState,
}

impl SelectorMapEntry for StateDependency {
    fn selector(&self) -> SelectorIter<'_, SelectorImpl> {
        self.dep.selector()
    }
}

/// The same, but for document state selectors.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct DocumentStateDependency {
    /// We track `Dependency` even though we don't need to track an offset,
    /// since when it changes it changes for the whole document anyway.
    #[cfg_attr(
        feature = "gecko",
        ignore_malloc_size_of = "CssRules have primary refs, we measure there"
    )]
    #[cfg_attr(feature = "servo", ignore_malloc_size_of = "Arc")]
    pub dependency: Dependency,
    /// The state this dependency is affected by.
    pub state: DocumentState,
}

/// Dependency mapping for classes or IDs.
pub type IdOrClassDependencyMap = MaybeCaseInsensitiveHashMap<Atom, SmallVec<[Dependency; 1]>>;
/// Dependency mapping for non-tree-strctural pseudo-class states.
pub type StateDependencyMap = SelectorMap<StateDependency>;
/// Dependency mapping for local names.
pub type LocalNameDependencyMap = PrecomputedHashMap<LocalName, SmallVec<[Dependency; 1]>>;
/// Dependency mapping for customstates
pub type CustomStateDependencyMap = PrecomputedHashMap<AtomIdent, SmallVec<[Dependency; 1]>>;

/// A map where we store invalidations.
///
/// This is slightly different to a SelectorMap, in the sense of that the same
/// selector may appear multiple times.
///
/// In particular, we want to lookup as few things as possible to get the fewer
/// selectors the better, so this looks up by id, class, or looks at the list of
/// state/other attribute affecting selectors.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct InvalidationMap {
    /// A map from a given class name to all the selectors with that class
    /// selector.
    pub class_to_selector: IdOrClassDependencyMap,
    /// A map from a given id to all the selectors with that ID in the
    /// stylesheets currently applying to the document.
    pub id_to_selector: IdOrClassDependencyMap,
    /// A map of all the state dependencies.
    pub state_affecting_selectors: StateDependencyMap,
    /// A list of document state dependencies in the rules we represent.
    pub document_state_selectors: Vec<DocumentStateDependency>,
    /// A map of other attribute affecting selectors.
    pub other_attribute_affecting_selectors: LocalNameDependencyMap,
    /// A map of CSS custom states
    pub custom_state_affecting_selectors: CustomStateDependencyMap,
}

/// Tree-structural pseudoclasses that we care about for (Relative selector) invalidation.
/// Specifically, we need to store information on ones that don't generate the inner selector.
/// Given the nature of these selectors:
/// * These are only relevant during DOM mutation invalidations
/// * Some invalidations may be optimized away.
#[derive(Clone, Copy, Debug, MallocSizeOf)]
pub struct TSStateForInvalidation(u8);

bitflags! {
    impl TSStateForInvalidation : u8 {
        /// :empty. This only needs to be considered for DOM mutation, and for
        /// elements that do not have any children.
        const EMPTY = 1 << 0;
        /// :nth and related selectors, without of.
        const NTH = 1 << 1;
        /// :first-child. This only needs to be considered for DOM mutation, and
        /// for elements that have no previous sibling.
        const NTH_EDGE_FIRST = 1 << 2;
        /// :last-child. This only needs to be considered for DOM mutation,
        /// and for elements have no next sibling.
        const NTH_EDGE_LAST = 1 << 3;
    }
}

impl TSStateForInvalidation {
    /// Return true if this state invalidation could be skipped (As per comment
    /// in the definition of this bitflags)
    pub fn may_be_optimized(&self) -> bool {
        (Self::EMPTY | Self::NTH_EDGE_FIRST | Self::NTH_EDGE_LAST).contains(*self)
    }
}

/// Dependency for tree-structural pseudo-classes.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct TSStateDependency {
    /// The other dependency fields.
    pub dep: Dependency,
    /// The state this dependency is affected by.
    pub state: TSStateForInvalidation,
}

impl SelectorMapEntry for TSStateDependency {
    fn selector(&self) -> SelectorIter<'_, SelectorImpl> {
        self.dep.selector()
    }
}

/// Dependency mapping for tree-structural pseudo-class states.
pub type TSStateDependencyMap = SelectorMap<TSStateDependency>;
/// Dependency mapping for * selectors.
pub type AnyDependencyMap = SmallVec<[Dependency; 1]>;

/// A map to store invalidation dependencies specific to relative selectors.
/// This keeps a lot more data than the usual map, because any change can generate
/// upward traversals that need to be handled separately.
#[derive(Clone, Debug, MallocSizeOf)]
pub struct AdditionalRelativeSelectorInvalidationMap {
    /// A map for a given tree-structural pseudo-class to all the relative selector dependencies with that type.
    pub ts_state_to_selector: TSStateDependencyMap,
    /// A map from a given type name to all the relative selector dependencies with that type.
    pub type_to_selector: LocalNameDependencyMap,
    /// All relative selector dependencies that specify `*`.
    pub any_to_selector: AnyDependencyMap,
    /// Flag indicating if any relative selector is used.
    pub used: bool,
    /// Flag indicating if invalidating a relative selector requires ancestor traversal.
    pub needs_ancestors_traversal: bool,
}

impl Default for AdditionalRelativeSelectorInvalidationMap {
    fn default() -> Self {
        Self::new()
    }
}

impl AdditionalRelativeSelectorInvalidationMap {
    /// Creates an empty `InvalidationMap`.
    pub fn new() -> Self {
        Self {
            ts_state_to_selector: TSStateDependencyMap::default(),
            type_to_selector: LocalNameDependencyMap::default(),
            any_to_selector: SmallVec::default(),
            used: false,
            needs_ancestors_traversal: false,
        }
    }

    /// Clears this map, leaving it empty.
    pub fn clear(&mut self) {
        self.ts_state_to_selector.clear();
        self.type_to_selector.clear();
        self.any_to_selector.clear();
    }

    /// Shrink the capacity of hash maps if needed.
    pub fn shrink_if_needed(&mut self) {
        self.ts_state_to_selector.shrink_if_needed();
        self.type_to_selector.shrink_if_needed();
    }
}

impl Default for InvalidationMap {
    fn default() -> Self {
        Self::new()
    }
}

impl InvalidationMap {
    /// Creates an empty `InvalidationMap`.
    pub fn new() -> Self {
        Self {
            class_to_selector: IdOrClassDependencyMap::new(),
            id_to_selector: IdOrClassDependencyMap::new(),
            state_affecting_selectors: StateDependencyMap::new(),
            document_state_selectors: Vec::new(),
            other_attribute_affecting_selectors: LocalNameDependencyMap::default(),
            custom_state_affecting_selectors: CustomStateDependencyMap::default(),
        }
    }

    /// Returns the number of dependencies stored in the invalidation map.
    pub fn len(&self) -> usize {
        self.state_affecting_selectors.len()
            + self.document_state_selectors.len()
            + self
                .other_attribute_affecting_selectors
                .iter()
                .fold(0, |accum, (_, v)| accum + v.len())
            + self
                .id_to_selector
                .iter()
                .fold(0, |accum, (_, v)| accum + v.len())
            + self
                .class_to_selector
                .iter()
                .fold(0, |accum, (_, v)| accum + v.len())
            + self
                .custom_state_affecting_selectors
                .iter()
                .fold(0, |accum, (_, v)| accum + v.len())
    }

    /// Clears this map, leaving it empty.
    pub fn clear(&mut self) {
        self.class_to_selector.clear();
        self.id_to_selector.clear();
        self.state_affecting_selectors.clear();
        self.document_state_selectors.clear();
        self.other_attribute_affecting_selectors.clear();
        self.custom_state_affecting_selectors.clear();
    }

    /// Shrink the capacity of hash maps if needed.
    pub fn shrink_if_needed(&mut self) {
        self.class_to_selector.shrink_if_needed();
        self.id_to_selector.shrink_if_needed();
        self.state_affecting_selectors.shrink_if_needed();
        self.other_attribute_affecting_selectors.shrink_if_needed();
        self.custom_state_affecting_selectors.shrink_if_needed();
    }
}

/// Adds a selector to the given `InvalidationMap`. Returns Err(..) to signify OOM.
pub fn note_selector_for_invalidation(
    selector: &Selector<SelectorImpl>,
    quirks_mode: QuirksMode,
    map: &mut InvalidationMap,
    relative_selector_invalidation_map: &mut InvalidationMap,
    additional_relative_selector_invalidation_map: &mut AdditionalRelativeSelectorInvalidationMap,
    inner_scope_dependencies: Option<&ThinArc<(), Dependency>>,
    scope_kind: Option<ScopeDependencyInvalidationKind>,
) -> Result<Option<Vec<Dependency>>, AllocErr> {
    let next_dependency = Dependency::for_full_selector_invalidation(selector.clone());
    let mut document_state = DocumentState::empty();
    let mut scope_dependencies = ScopeSelectorCollectorState {
        inner_dependencies: &inner_scope_dependencies.cloned(),
        this_dependencies: None,
        scope_kind,
    };

    {
        let mut next_stack = NextSelectors::new();
        let mut alloc_error = None;
        let mut collector = SelectorDependencyCollector {
            map,
            relative_selector_invalidation_map,
            additional_relative_selector_invalidation_map,
            document_state: &mut document_state,
            selector,
            next_selectors: &mut next_stack,
            quirks_mode,
            compound_state: PerCompoundState::new(0),
            relative_inner_collector: None,
            scope_dependencies: &mut scope_dependencies,
            alloc_error: &mut alloc_error,
        };

        let visit_result = collector.visit_whole_selector();

        debug_assert_eq!(!visit_result, alloc_error.is_some());
        if let Some(alloc_error) = alloc_error {
            return Err(alloc_error);
        }
    }

    if !document_state.is_empty() {
        let dep = DocumentStateDependency {
            state: document_state,
            dependency: next_dependency,
        };
        map.document_state_selectors.try_reserve(1)?;
        map.document_state_selectors.push(dep);
    }
    Ok(scope_dependencies.this_dependencies)
}

struct PerCompoundState {
    /// The offset at which our compound starts.
    offset: usize,

    /// The state this compound selector is affected by.
    element_state: ElementState,
}

impl PerCompoundState {
    fn new(offset: usize) -> Self {
        Self {
            offset,
            element_state: ElementState::empty(),
        }
    }
}

struct NextDependencyEntry {
    selector: Selector<SelectorImpl>,
    offset: usize,
    cached_dependency: Option<ThinArc<(), Dependency>>,
}

struct RelativeSelectorInnerCollectorState<'a> {
    next_dependency: &'a ThinArc<(), Dependency>,
    relative_compound_state: RelativeSelectorCompoundStateAttributes,
}
struct ScopeSelectorCollectorState<'a> {
    // Inner scope dependencies this scope selector need to point to
    inner_dependencies: &'a Option<ThinArc<(), Dependency>>,
    // Scope dependencies added by this scope selector
    this_dependencies: Option<Vec<Dependency>>,
    // Whether this dependency is scope start, end, or other.
    scope_kind: Option<ScopeDependencyInvalidationKind>,
}

trait Collector {
    fn dependency(&mut self) -> Dependency;
    fn id_map(&mut self) -> &mut IdOrClassDependencyMap;
    fn class_map(&mut self) -> &mut IdOrClassDependencyMap;
    fn state_map(&mut self) -> &mut StateDependencyMap;
    fn attribute_map(&mut self) -> &mut LocalNameDependencyMap;
    fn custom_state_map(&mut self) -> &mut CustomStateDependencyMap;
    fn inner_scope_dependencies(&self) -> Option<ThinArc<(), Dependency>>;
    fn this_scope_dependencies(&mut self) -> &mut Option<Vec<Dependency>>;
    fn update_states(&mut self, element_state: ElementState, document_state: DocumentState);

    // In normal invalidations, type-based dependencies don't need to be explicitly tracked;
    // elements don't change their types, and mutations cause invalidations to go descendant
    // (Where they are about to be styled anyway), and/or later-sibling direction (Where they
    // siblings after inserted/removed elements get restyled anyway).
    // However, for relative selectors, a DOM mutation can affect and arbitrary ancestor and/or
    // earlier siblings, so we need to keep track of them.
    fn type_map(&mut self) -> &mut LocalNameDependencyMap {
        unreachable!();
    }

    // Tree-structural pseudo-selectors generally invalidates in a well-defined way, which are
    // handled by RestyleManager. However, for relative selectors, as with type invalidations,
    // the direction of invalidation becomes arbitrary, so we need to keep track of them.
    fn ts_state_map(&mut self) -> &mut TSStateDependencyMap {
        unreachable!();
    }

    // Same story as type invalidation maps.
    fn any_vec(&mut self) -> &mut AnyDependencyMap {
        unreachable!();
    }
}

fn on_attribute<C: Collector>(
    local_name: &LocalName,
    local_name_lower: &LocalName,
    collector: &mut C,
) -> Result<(), AllocErr> {
    add_attr_dependency(local_name, collector)?;
    if local_name != local_name_lower {
        add_attr_dependency(local_name_lower, collector)?;
    }
    Ok(())
}

fn on_id_or_class<C: Collector>(
    s: &Component<SelectorImpl>,
    quirks_mode: QuirksMode,
    collector: &mut C,
) -> Result<(), AllocErr> {
    let dependency = collector.dependency();

    let (atom, map) = match *s {
        Component::ID(ref atom) => (atom, collector.id_map()),
        Component::Class(ref atom) => (atom, collector.class_map()),
        _ => unreachable!(),
    };
    let vec = map.try_get_or_insert_with(&atom.0, quirks_mode, SmallVec::new)?;
    vec.try_reserve(1)?;
    vec.push(dependency);
    Ok(())
}

fn on_scope<C: Collector>(collector: &mut C) -> Result<(), AllocErr> {
    let new_dependency = collector.dependency();
    let this_scope_dependencies = collector.this_scope_dependencies();

    this_scope_dependencies
        .get_or_insert(Vec::new())
        .push(new_dependency);

    Ok(())
}

fn add_attr_dependency<C: Collector>(name: &LocalName, collector: &mut C) -> Result<(), AllocErr> {
    let dependency = collector.dependency();
    let map = collector.attribute_map();
    add_local_name(name, dependency, map)
}

fn add_custom_state_dependency<C: Collector>(
    name: &AtomIdent,
    collector: &mut C,
) -> Result<(), AllocErr> {
    let dependency = collector.dependency();
    let map = collector.custom_state_map();
    map.try_reserve(1)?;
    let vec = map.entry_ref(name).or_default();
    vec.try_reserve(1)?;
    vec.push(dependency);
    Ok(())
}

fn add_local_name(
    name: &LocalName,
    dependency: Dependency,
    map: &mut LocalNameDependencyMap,
) -> Result<(), AllocErr> {
    map.try_reserve(1)?;
    let vec = map.entry_ref(name).or_default();
    vec.try_reserve(1)?;
    vec.push(dependency);
    Ok(())
}

fn on_pseudo_class<C: Collector>(pc: &NonTSPseudoClass, collector: &mut C) -> Result<(), AllocErr> {
    collector.update_states(pc.state_flag(), pc.document_state_flag());

    let attr_name = match *pc {
        #[cfg(feature = "gecko")]
        NonTSPseudoClass::MozTableBorderNonzero => local_name!("border"),
        #[cfg(feature = "gecko")]
        NonTSPseudoClass::MozSelectListBox => {
            // This depends on two attributes.
            add_attr_dependency(&local_name!("multiple"), collector)?;
            return add_attr_dependency(&local_name!("size"), collector);
        },
        NonTSPseudoClass::Lang(..) => local_name!("lang"),
        NonTSPseudoClass::CustomState(ref name) => {
            return add_custom_state_dependency(&name.0, collector);
        },
        _ => return Ok(()),
    };

    add_attr_dependency(&attr_name, collector)
}

fn add_pseudo_class_dependency<C: Collector>(
    element_state: ElementState,
    quirks_mode: QuirksMode,
    collector: &mut C,
) -> Result<(), AllocErr> {
    if element_state.is_empty() {
        return Ok(());
    }
    let dependency = collector.dependency();
    collector.state_map().insert(
        StateDependency {
            dep: dependency,
            state: element_state,
        },
        quirks_mode,
    )
}

/// Visit all the simple selectors in the iter compound
/// and return the number of simple selectors visited.
/// We need to return a tuple because we need to keep
/// track of two things:
/// 1) Should the traversal continue and
/// 2) What the offset of the compound state is.
fn visit_all_in_iter_compound<T: SelectorVisitor<Impl = SelectorImpl>>(
    visitor: &mut T,
    iter: &mut SelectorIter<'_, SelectorImpl>,
) -> (bool, usize) {
    let mut index = 0;
    for ss in iter {
        if !ss.visit(visitor) {
            return (false, index);
        }
        index += 1;
    }
    (true, index)
}

type NextSelectors = SmallVec<[NextDependencyEntry; 5]>;

/// A struct that collects invalidations for a given compound selector.
struct SelectorDependencyCollector<'a, 'b, 'c> {
    map: &'a mut InvalidationMap,
    relative_selector_invalidation_map: &'a mut InvalidationMap,
    additional_relative_selector_invalidation_map:
        &'a mut AdditionalRelativeSelectorInvalidationMap,

    /// The document this _complex_ selector is affected by.
    ///
    /// We don't need to track state per compound selector, since it's global
    /// state and it changes for everything.
    document_state: &'a mut DocumentState,

    /// The current selector and offset we're iterating.
    selector: &'a Selector<SelectorImpl>,

    /// The stack of next selectors that we have, and at which offset of the
    /// sequence.
    ///
    /// This starts empty. It grows when we find nested :is and :where selector
    /// lists. The dependency field is cached and reference counted.
    next_selectors: &'a mut NextSelectors,

    /// The quirks mode of the document where we're inserting dependencies.
    quirks_mode: QuirksMode,

    /// State relevant to a given compound selector.
    compound_state: PerCompoundState,

    /// Additional state to keep track of for collecting nested inner selectors of relative selectors
    /// Holds the next relative selector dependency and the state given to a relative selector.
    relative_inner_collector: Option<RelativeSelectorInnerCollectorState<'b>>,

    scope_dependencies: &'a mut ScopeSelectorCollectorState<'c>,

    /// The allocation error, if we OOM.
    alloc_error: &'a mut Option<AllocErr>,
}

fn next_dependency(
    next_selector: &mut NextSelectors,
    next_outer_dependency: Option<&ThinArc<(), Dependency>>,
    next_scope_dependencies: Option<&ThinArc<(), Dependency>>,
    scope_kind: Option<ScopeDependencyInvalidationKind>,
) -> Option<ThinArc<(), Dependency>> {
    if next_selector.is_empty() {
        return match next_outer_dependency {
            Some(..) => next_outer_dependency.cloned(),
            None => next_scope_dependencies.cloned(),
        };
    }

    fn dependencies_from(
        entries: &mut [NextDependencyEntry],
        next_outer_dependency: &Option<&ThinArc<(), Dependency>>,
        next_scope_dependencies: &Option<&ThinArc<(), Dependency>>,
        scope_kind: Option<ScopeDependencyInvalidationKind>,
    ) -> Option<ThinArc<(), Dependency>> {
        if entries.is_empty() {
            return next_scope_dependencies.cloned();
        }

        let last_index = entries.len() - 1;
        let (previous, last) = entries.split_at_mut(last_index);
        let last = &mut last[0];
        let selector = &last.selector;
        let selector_offset = last.offset;

        let dependency = Dependency {
            selector: selector.clone(),
            selector_offset,
            next: dependencies_from(
                previous,
                next_outer_dependency,
                next_scope_dependencies,
                scope_kind,
            ),
            kind: get_non_relative_invalidation_kind(
                selector,
                selector_offset,
                next_scope_dependencies
                    .is_some()
                    .then_some(scope_kind)
                    .flatten(),
            ),
        };

        Some(
            last.cached_dependency
                .get_or_insert_with(|| ThinArc::from_header_and_iter((), [dependency].into_iter()))
                .clone(),
        )
    }

    dependencies_from(
        next_selector,
        &next_outer_dependency,
        &next_scope_dependencies,
        scope_kind,
    )
}

impl<'a, 'b, 'c> Collector for SelectorDependencyCollector<'a, 'b, 'c> {
    fn dependency(&mut self) -> Dependency {
        let optional_dependency = self
            .relative_inner_collector
            .as_ref()
            .map(|collector| collector.next_dependency);

        let offset = self.compound_state.offset;

        let scope_dependencies = self.inner_scope_dependencies();

        let next = next_dependency(
            self.next_selectors,
            optional_dependency,
            scope_dependencies.as_ref(),
            self.scope_dependencies.scope_kind,
        );

        Dependency {
            selector: self.selector.clone(),
            selector_offset: offset,
            next,
            kind: get_non_relative_invalidation_kind(
                self.selector,
                offset,
                scope_dependencies
                    .is_some()
                    .then_some(self.scope_dependencies.scope_kind)
                    .flatten(),
            ),
        }
    }

    fn id_map(&mut self) -> &mut IdOrClassDependencyMap {
        if self.relative_inner_collector.is_none() {
            &mut self.map.id_to_selector
        } else {
            &mut self.relative_selector_invalidation_map.id_to_selector
        }
    }

    fn class_map(&mut self) -> &mut IdOrClassDependencyMap {
        if self.relative_inner_collector.is_none() {
            &mut self.map.class_to_selector
        } else {
            &mut self.relative_selector_invalidation_map.class_to_selector
        }
    }

    fn state_map(&mut self) -> &mut StateDependencyMap {
        if self.relative_inner_collector.is_none() {
            &mut self.map.state_affecting_selectors
        } else {
            &mut self
                .relative_selector_invalidation_map
                .state_affecting_selectors
        }
    }

    fn attribute_map(&mut self) -> &mut LocalNameDependencyMap {
        if self.relative_inner_collector.is_none() {
            &mut self.map.other_attribute_affecting_selectors
        } else {
            &mut self
                .relative_selector_invalidation_map
                .other_attribute_affecting_selectors
        }
    }

    fn inner_scope_dependencies(&self) -> Option<ThinArc<(), Dependency>> {
        self.scope_dependencies.inner_dependencies.clone()
    }

    fn this_scope_dependencies(&mut self) -> &mut Option<Vec<Dependency>> {
        &mut self.scope_dependencies.this_dependencies
    }

    fn update_states(&mut self, element_state: ElementState, document_state: DocumentState) {
        self.compound_state.element_state |= element_state;
        *self.document_state |= document_state;
    }

    fn custom_state_map(&mut self) -> &mut CustomStateDependencyMap {
        if self.relative_inner_collector.is_none() {
            &mut self.map.custom_state_affecting_selectors
        } else {
            &mut self
                .relative_selector_invalidation_map
                .custom_state_affecting_selectors
        }
    }

    fn type_map(&mut self) -> &mut LocalNameDependencyMap {
        debug_assert!(
            self.relative_inner_collector.is_some(),
            "Asking for relative selector invalidation outside of relative selector"
        );
        &mut self
            .additional_relative_selector_invalidation_map
            .type_to_selector
    }

    fn ts_state_map(&mut self) -> &mut TSStateDependencyMap {
        debug_assert!(
            self.relative_inner_collector.is_some(),
            "Asking for relative selector invalidation outside of relative selector"
        );
        &mut self
            .additional_relative_selector_invalidation_map
            .ts_state_to_selector
    }

    fn any_vec(&mut self) -> &mut AnyDependencyMap {
        debug_assert!(
            self.relative_inner_collector.is_some(),
            "Asking for relative selector invalidation outside of relative selector"
        );
        &mut self
            .additional_relative_selector_invalidation_map
            .any_to_selector
    }
}

impl<'a, 'b, 'c> SelectorDependencyCollector<'a, 'b, 'c> {
    fn visit_whole_selector(&mut self) -> bool {
        let iter = self.selector.iter();
        self.visit_whole_selector_from(iter, 0)
    }

    fn visit_whole_selector_from(
        &mut self,
        mut iter: SelectorIter<SelectorImpl>,
        mut index: usize,
    ) -> bool {
        loop {
            // Reset the compound state.
            self.compound_state = PerCompoundState::new(index);
            if let Some(state) = self.relative_inner_collector.as_mut() {
                state.relative_compound_state = RelativeSelectorCompoundStateAttributes::new();
            }

            // Visit all the simple selectors in this sequence.
            let (keep_traversing, index_offset) = visit_all_in_iter_compound(self, &mut iter);

            if !keep_traversing {
                return false;
            }

            index += index_offset;

            if let Err(err) = add_pseudo_class_dependency(
                self.compound_state.element_state,
                self.quirks_mode,
                self,
            ) {
                *self.alloc_error = Some(err);
                return false;
            }

            if let Some(state) = self
                .relative_inner_collector
                .as_ref()
                .map(|state| state.relative_compound_state)
            {
                if let Err(err) =
                    add_ts_pseudo_class_dependency(state.ts_state, self.quirks_mode, self)
                {
                    *self.alloc_error = Some(err);
                    return false;
                }

                if !state.added_entry {
                    // Not great - we didn't add any uniquely identifiable information.
                    if let Err(err) =
                        add_non_unique_info(self.selector, self.compound_state.offset, self)
                    {
                        *self.alloc_error = Some(err);
                        return false;
                    }
                }
            }

            let combinator = iter.next_sequence();
            if combinator.is_none() {
                return true;
            }
            index += 1; // account for the combinator
        }
    }
}

impl<'a, 'b, 'c> SelectorVisitor for SelectorDependencyCollector<'a, 'b, 'c> {
    type Impl = SelectorImpl;

    fn visit_selector_list(
        &mut self,
        _list_kind: SelectorListKind,
        list: &[Selector<SelectorImpl>],
    ) -> bool {
        let next_relative_dependency = self
            .relative_inner_collector
            .is_some()
            .then(|| ThinArc::from_header_and_iter((), std::iter::once(self.dependency())));
        for selector in list {
            // Here we cheat a bit: We can visit the rightmost compound with
            // the "outer" visitor, and it'd be fine. This reduces the amount of
            // state and attribute invalidations, and we need to check the outer
            // selector to the left anyway to avoid over-invalidation, so it
            // avoids matching it twice uselessly.
            let mut iter = selector.iter();
            let saved_added_entry = self
                .relative_inner_collector
                .as_ref()
                .map(|state| state.relative_compound_state.added_entry);

            let (keep_traversing, mut index) = visit_all_in_iter_compound(self, &mut iter);

            if !keep_traversing {
                return false;
            }

            if let Some(state) = self.relative_inner_collector.as_mut() {
                state.relative_compound_state.added_entry = saved_added_entry.unwrap_or_default();
            }
            let combinator = iter.next_sequence();
            if combinator.is_none() {
                continue;
            }

            index += 1; // account for the combinator.

            let offset = self.compound_state.offset;

            if self.relative_inner_collector.is_none() {
                self.next_selectors.push(NextDependencyEntry {
                    selector: self.selector.clone(),
                    offset,
                    cached_dependency: None,
                });
            }
            debug_assert!(
                next_relative_dependency.is_some() == self.relative_inner_collector.is_some(),
                "Next relative dependency and relative inner collector must be Some/None at the same time!"
            );
            let mut nested = SelectorDependencyCollector {
                map: &mut *self.map,
                relative_selector_invalidation_map: &mut *self.relative_selector_invalidation_map,
                additional_relative_selector_invalidation_map: &mut *self
                    .additional_relative_selector_invalidation_map,
                document_state: &mut *self.document_state,
                selector,
                next_selectors: &mut *self.next_selectors,
                quirks_mode: self.quirks_mode,
                compound_state: PerCompoundState::new(index),
                relative_inner_collector: next_relative_dependency.as_ref().map(
                    |next_dependency| RelativeSelectorInnerCollectorState {
                        next_dependency,
                        relative_compound_state: RelativeSelectorCompoundStateAttributes::new(),
                    },
                ),
                scope_dependencies: self.scope_dependencies,
                alloc_error: &mut *self.alloc_error,
            };
            if !nested.visit_whole_selector_from(iter, index) {
                return false;
            }
            self.next_selectors.pop();
        }
        true
    }

    fn visit_relative_selector_list(
        &mut self,
        list: &[selectors::parser::RelativeSelector<Self::Impl>],
    ) -> bool {
        // Ignore nested relative selectors. This can happen as a result of nesting.
        if self.relative_inner_collector.is_some() {
            return true;
        }

        self.additional_relative_selector_invalidation_map.used = true;
        for relative_selector in list {
            // We can't cheat here like we do with other selector lists - the rightmost
            // compound of a relative selector is not the subject of the invalidation.
            self.next_selectors.push(NextDependencyEntry {
                selector: self.selector.clone(),
                offset: self.compound_state.offset,
                cached_dependency: None,
            });
            let mut nested = RelativeSelectorDependencyCollector {
                map: &mut *self.map,
                relative_selector_invalidation_map: &mut *self.relative_selector_invalidation_map,
                additional_relative_selector_invalidation_map: &mut *self
                    .additional_relative_selector_invalidation_map,
                document_state: &mut *self.document_state,
                selector: relative_selector,
                combinator_count: RelativeSelectorCombinatorCount::new(relative_selector),
                next_selectors: &mut *self.next_selectors,
                quirks_mode: self.quirks_mode,
                compound_state: PerCompoundState::new(0),
                compound_state_attributes: RelativeSelectorCompoundStateAttributes::new(),
                scope_dependencies: self.scope_dependencies,
                alloc_error: &mut *self.alloc_error,
            };
            if !nested.visit_whole_selector() {
                return false;
            }
            self.next_selectors.pop();
        }
        true
    }

    fn visit_simple_selector(&mut self, s: &Component<SelectorImpl>) -> bool {
        match on_simple_selector(s, self.quirks_mode, self) {
            Ok(result) => {
                if let ComponentVisitResult::Handled(state) = result
                    && let Some(inner_collector_state) = self.relative_inner_collector.as_mut()
                {
                    inner_collector_state.relative_compound_state.added_entry = true;
                    inner_collector_state
                        .relative_compound_state
                        .ts_state
                        .insert(state);
                }
                true
            },
            Err(err) => {
                *self.alloc_error = Some(err);
                false
            },
        }
    }

    fn visit_attribute_selector(
        &mut self,
        _: &NamespaceConstraint<&Namespace>,
        local_name: &LocalName,
        local_name_lower: &LocalName,
    ) -> bool {
        if let Some(state) = self.relative_inner_collector.as_mut() {
            state.relative_compound_state.added_entry = true;
        }
        if let Err(err) = on_attribute(local_name, local_name_lower, self) {
            *self.alloc_error = Some(err);
            return false;
        }
        true
    }
}

#[derive(Clone, Copy)]
struct RelativeSelectorCompoundStateAttributes {
    ts_state: TSStateForInvalidation,
    added_entry: bool,
}

impl RelativeSelectorCompoundStateAttributes {
    fn new() -> Self {
        Self {
            ts_state: TSStateForInvalidation::empty(),
            added_entry: false,
        }
    }
}

/// A struct that collects invalidations for a given compound selector.
struct RelativeSelectorDependencyCollector<'a, 'b> {
    map: &'a mut InvalidationMap,
    relative_selector_invalidation_map: &'a mut InvalidationMap,
    additional_relative_selector_invalidation_map:
        &'a mut AdditionalRelativeSelectorInvalidationMap,

    /// The document this _complex_ selector is affected by.
    ///
    /// We don't need to track state per compound selector, since it's global
    /// state and it changes for everything.
    document_state: &'a mut DocumentState,

    /// The current inner relative selector and offset we're iterating.
    selector: &'a RelativeSelector<SelectorImpl>,
    /// Running combinator for this inner relative selector.
    combinator_count: RelativeSelectorCombinatorCount,

    /// The stack of next selectors that we have, and at which offset of the
    /// sequence.
    ///
    /// This starts empty. It grows when we find nested :is and :where selector
    /// lists. The dependency field is cached and reference counted.
    next_selectors: &'a mut NextSelectors,

    /// The quirks mode of the document where we're inserting dependencies.
    quirks_mode: QuirksMode,

    /// State relevant to a given compound selector.
    compound_state: PerCompoundState,

    /// Attributes relevant to the relative compound selector state.
    compound_state_attributes: RelativeSelectorCompoundStateAttributes,

    scope_dependencies: &'a mut ScopeSelectorCollectorState<'b>,

    /// The allocation error, if we OOM.
    alloc_error: &'a mut Option<AllocErr>,
}

fn add_non_unique_info<C: Collector>(
    selector: &Selector<SelectorImpl>,
    offset: usize,
    collector: &mut C,
) -> Result<(), AllocErr> {
    // Go through this compound again.
    for ss in selector.iter_from(offset) {
        if let Component::LocalName(name) = ss {
            let dependency = collector.dependency();
            add_local_name(&name.name, dependency, collector.type_map())?;
            if name.name != name.lower_name {
                let dependency = collector.dependency();
                add_local_name(&name.lower_name, dependency, collector.type_map())?;
            }
            return Ok(());
        };
    }
    // Ouch. Add one for *.
    collector.any_vec().try_reserve(1)?;
    let dependency = collector.dependency();
    collector.any_vec().push(dependency);
    Ok(())
}

fn add_ts_pseudo_class_dependency<C: Collector>(
    state: TSStateForInvalidation,
    quirks_mode: QuirksMode,
    collector: &mut C,
) -> Result<(), AllocErr> {
    if state.is_empty() {
        return Ok(());
    }
    let dependency = collector.dependency();
    collector.ts_state_map().insert(
        TSStateDependency {
            dep: dependency,
            state,
        },
        quirks_mode,
    )
}

impl<'a, 'b> RelativeSelectorDependencyCollector<'a, 'b> {
    fn visit_whole_selector(&mut self) -> bool {
        let mut iter = self.selector.selector.iter_skip_relative_selector_anchor();
        let mut index = 0;

        self.additional_relative_selector_invalidation_map
            .needs_ancestors_traversal |= match self.selector.match_hint {
            RelativeSelectorMatchHint::InNextSiblingSubtree
            | RelativeSelectorMatchHint::InSiblingSubtree
            | RelativeSelectorMatchHint::InSubtree => true,
            _ => false,
        };
        loop {
            // Reset the compound state.
            self.compound_state = PerCompoundState::new(index);
            self.compound_state_attributes = RelativeSelectorCompoundStateAttributes::new();

            let (keep_traversing, index_offset) = visit_all_in_iter_compound(self, &mut iter);

            if !keep_traversing {
                return false;
            }

            index += index_offset;

            if let Err(err) = add_pseudo_class_dependency(
                self.compound_state.element_state,
                self.quirks_mode,
                self,
            ) {
                *self.alloc_error = Some(err);
                return false;
            }

            if let Err(err) = add_ts_pseudo_class_dependency(
                self.compound_state_attributes.ts_state,
                self.quirks_mode,
                self,
            ) {
                *self.alloc_error = Some(err);
                return false;
            }

            if !self.compound_state_attributes.added_entry {
                // Not great - we didn't add any uniquely identifiable information.
                if let Err(err) =
                    add_non_unique_info(&self.selector.selector, self.compound_state.offset, self)
                {
                    *self.alloc_error = Some(err);
                    return false;
                }
            }

            let combinator = iter.next_sequence();
            if let Some(c) = combinator {
                match c {
                    Combinator::Child | Combinator::Descendant => {
                        self.combinator_count.child_or_descendants -= 1
                    },
                    Combinator::NextSibling | Combinator::LaterSibling => {
                        self.combinator_count.adjacent_or_next_siblings -= 1
                    },
                    Combinator::Part | Combinator::PseudoElement | Combinator::SlotAssignment => (),
                }
            } else {
                return true;
            }
            index += 1; // account for the combinator
        }
    }
}

impl<'a, 'b> Collector for RelativeSelectorDependencyCollector<'a, 'b> {
    fn dependency(&mut self) -> Dependency {
        let scope_dependencies = self.inner_scope_dependencies();
        let scope_kind = self.scope_dependencies.scope_kind;

        let next = next_dependency(
            self.next_selectors,
            None,
            scope_dependencies.as_ref(),
            scope_kind,
        );
        debug_assert!(
            next.as_ref().is_some_and(|d| !matches!(
                d.slice()[0].kind,
                DependencyInvalidationKind::Relative(_)
            )),
            "Duplicate relative dependency?"
        );
        debug_assert!(
            next.as_ref().is_some_and(|d| !d.slice().is_empty()),
            "Empty dependency?"
        );

        Dependency {
            selector: self.selector.selector.clone(),
            selector_offset: self.compound_state.offset,
            kind: DependencyInvalidationKind::Relative(
                match self.combinator_count.get_match_hint() {
                    RelativeSelectorMatchHint::InChild => {
                        RelativeDependencyInvalidationKind::Parent
                    },
                    RelativeSelectorMatchHint::InSubtree => {
                        RelativeDependencyInvalidationKind::Ancestors
                    },
                    RelativeSelectorMatchHint::InNextSibling => {
                        RelativeDependencyInvalidationKind::PrevSibling
                    },
                    RelativeSelectorMatchHint::InSibling => {
                        RelativeDependencyInvalidationKind::EarlierSibling
                    },
                    RelativeSelectorMatchHint::InNextSiblingSubtree => {
                        RelativeDependencyInvalidationKind::AncestorPrevSibling
                    },
                    RelativeSelectorMatchHint::InSiblingSubtree => {
                        RelativeDependencyInvalidationKind::AncestorEarlierSibling
                    },
                },
            ),
            next,
        }
    }

    fn id_map(&mut self) -> &mut IdOrClassDependencyMap {
        &mut self.relative_selector_invalidation_map.id_to_selector
    }

    fn class_map(&mut self) -> &mut IdOrClassDependencyMap {
        &mut self.relative_selector_invalidation_map.class_to_selector
    }

    fn state_map(&mut self) -> &mut StateDependencyMap {
        &mut self
            .relative_selector_invalidation_map
            .state_affecting_selectors
    }

    fn attribute_map(&mut self) -> &mut LocalNameDependencyMap {
        &mut self
            .relative_selector_invalidation_map
            .other_attribute_affecting_selectors
    }

    fn custom_state_map(&mut self) -> &mut CustomStateDependencyMap {
        &mut self
            .relative_selector_invalidation_map
            .custom_state_affecting_selectors
    }

    fn inner_scope_dependencies(&self) -> Option<ThinArc<(), Dependency>> {
        self.scope_dependencies.inner_dependencies.clone()
    }

    fn this_scope_dependencies(&mut self) -> &mut Option<Vec<Dependency>> {
        &mut self.scope_dependencies.this_dependencies
    }

    fn update_states(&mut self, element_state: ElementState, document_state: DocumentState) {
        self.compound_state.element_state |= element_state;
        *self.document_state |= document_state;
    }

    fn type_map(&mut self) -> &mut LocalNameDependencyMap {
        &mut self
            .additional_relative_selector_invalidation_map
            .type_to_selector
    }

    fn ts_state_map(&mut self) -> &mut TSStateDependencyMap {
        &mut self
            .additional_relative_selector_invalidation_map
            .ts_state_to_selector
    }

    fn any_vec(&mut self) -> &mut AnyDependencyMap {
        &mut self
            .additional_relative_selector_invalidation_map
            .any_to_selector
    }
}

enum ComponentVisitResult {
    /// This component is not relevant for building up the invalidation map.
    IsIrrelevant,
    /// This component has been added to the invalidation map. Any additional
    /// tree-structural pseudo-class dependency is also included, if required.
    Handled(TSStateForInvalidation),
}

#[inline(always)]
fn on_simple_selector<C: Collector>(
    s: &Component<SelectorImpl>,
    quirks_mode: QuirksMode,
    collector: &mut C,
) -> Result<ComponentVisitResult, AllocErr> {
    match *s {
        Component::ID(..) | Component::Class(..) => {
            on_id_or_class(s, quirks_mode, collector)?;
            Ok(ComponentVisitResult::Handled(
                TSStateForInvalidation::empty(),
            ))
        },
        Component::ImplicitScope | Component::Scope => {
            on_scope(collector)?;
            Ok(ComponentVisitResult::Handled(
                TSStateForInvalidation::empty(),
            ))
        },
        Component::NonTSPseudoClass(ref pc) => {
            on_pseudo_class(pc, collector)?;
            Ok(ComponentVisitResult::Handled(
                TSStateForInvalidation::empty(),
            ))
        },
        Component::Empty => Ok(ComponentVisitResult::Handled(TSStateForInvalidation::EMPTY)),
        Component::Nth(data) => {
            let kind = if data.is_simple_edge() {
                if data.ty.is_from_end() {
                    TSStateForInvalidation::NTH_EDGE_LAST
                } else {
                    TSStateForInvalidation::NTH_EDGE_FIRST
                }
            } else {
                TSStateForInvalidation::NTH
            };
            Ok(ComponentVisitResult::Handled(kind))
        },
        Component::RelativeSelectorAnchor => unreachable!("Should not visit this far"),
        _ => Ok(ComponentVisitResult::IsIrrelevant),
    }
}

impl<'a, 'b> SelectorVisitor for RelativeSelectorDependencyCollector<'a, 'b> {
    type Impl = SelectorImpl;

    fn visit_selector_list(
        &mut self,
        _list_kind: SelectorListKind,
        list: &[Selector<SelectorImpl>],
    ) -> bool {
        let mut next_stack = NextSelectors::new();
        let next_dependency = ThinArc::from_header_and_iter((), [self.dependency()].into_iter());
        for selector in list {
            let mut iter = selector.iter();
            let saved_added_entry = self.compound_state_attributes.added_entry;

            let (keep_traversing, mut index) = visit_all_in_iter_compound(self, &mut iter);

            if !keep_traversing {
                return false;
            }

            let combinator = iter.next_sequence();

            // We want to preserve added_entry, to handle all DOM manipulations
            // correctly. For example, given `.anchor:has(:not(.foo))`, and a
            // DOM tree `.anchor > .foo`, insertion of _any_ element without
            // `.foo` as `.anchor`'s child must trigger an invalidation.
            self.compound_state_attributes.added_entry = saved_added_entry;
            if combinator.is_none() {
                continue;
            }

            index += 1; // account for the combinator.

            let mut nested = SelectorDependencyCollector {
                map: &mut *self.map,
                relative_selector_invalidation_map: &mut *self.relative_selector_invalidation_map,
                additional_relative_selector_invalidation_map: self
                    .additional_relative_selector_invalidation_map,
                document_state: &mut *self.document_state,
                selector,
                next_selectors: &mut next_stack,
                quirks_mode: self.quirks_mode,
                compound_state: PerCompoundState::new(index),
                relative_inner_collector: Some(RelativeSelectorInnerCollectorState {
                    next_dependency: &next_dependency,
                    relative_compound_state: RelativeSelectorCompoundStateAttributes::new(),
                }),
                scope_dependencies: self.scope_dependencies,
                alloc_error: &mut *self.alloc_error,
            };
            if !nested.visit_whole_selector_from(iter, index) {
                return false;
            }
        }
        true
    }

    fn visit_relative_selector_list(
        &mut self,
        _list: &[selectors::parser::RelativeSelector<Self::Impl>],
    ) -> bool {
        // Ignore nested relative selectors. These can happen as a result of nesting.
        true
    }

    fn visit_simple_selector(&mut self, s: &Component<SelectorImpl>) -> bool {
        match on_simple_selector(s, self.quirks_mode, self) {
            Ok(result) => {
                if let ComponentVisitResult::Handled(state) = result {
                    self.compound_state_attributes.added_entry = true;
                    self.compound_state_attributes.ts_state.insert(state);
                }
                true
            },
            Err(err) => {
                *self.alloc_error = Some(err);
                false
            },
        }
    }

    fn visit_attribute_selector(
        &mut self,
        _: &NamespaceConstraint<&Namespace>,
        local_name: &LocalName,
        local_name_lower: &LocalName,
    ) -> bool {
        self.compound_state_attributes.added_entry = true;
        if let Err(err) = on_attribute(local_name, local_name_lower, self) {
            *self.alloc_error = Some(err);
            return false;
        }
        true
    }
}
