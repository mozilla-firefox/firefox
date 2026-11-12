/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A [`@container`][container] rule.
//!
//! [container]: https://drafts.csswg.org/css-contain-3/#container-rule

use crate::computed_value_flags::ComputedValueFlags;
use crate::dom::{AttributeTracker, TElement};
use crate::logical_geometry::{LogicalSize, WritingMode};
use crate::parser::ParserContext;
use crate::properties::ComputedValues;
use crate::queries::feature::{AllowsRanges, Evaluator, FeatureFlags, QueryFeatureDescription};
use crate::queries::values::Orientation;
use crate::queries::{FeatureType, QueryCondition};
use crate::selector_map::{PrecomputedHashMap, PrecomputedHashSet};
use crate::shared_lock::{
    DeepCloneWithLock, Locked, SharedRwLock, SharedRwLockReadGuard, ToCssWithGuard,
};
use crate::stylesheets::{CssRules, CustomMediaEvaluator};
use crate::stylist::Stylist;
use crate::values::computed::{CSSPixelLength, ContainerType, Context, Ratio};
use crate::values::specified::ContainerName;
use crate::{LocalName, derives::*};
use app_units::Au;
use cssparser::{Parser, SourceLocation};
use euclid::default::Size2D;
#[cfg(feature = "gecko")]
use malloc_size_of::{MallocSizeOfOps, MallocUnconditionalShallowSizeOf};
use selectors::kleene_value::KleeneValue;
use selectors::matching::ElementSelectorFlags;
use servo_arc::Arc;
use std::fmt::{self, Write};
use style_traits::arc_slice::ArcSlice;
use style_traits::{CssStringWriter, CssWriter, ParseError, StyleParseErrorKind, ToCss};

/// A container rule.
#[derive(Debug, ToShmem)]
pub struct ContainerRule {
    /// The container queries and name.
    pub conditions: ContainerConditions,
    /// The nested rules inside the block.
    pub rules: Arc<Locked<CssRules>>,
    /// The source position where this rule was found.
    pub source_location: SourceLocation,
}

impl ContainerRule {
    /// Measure heap usage.
    #[cfg(feature = "gecko")]
    pub fn size_of(&self, guard: &SharedRwLockReadGuard, ops: &mut MallocSizeOfOps) -> usize {
        // Measurement of other fields may be added later.
        self.rules.unconditional_shallow_size_of(ops)
            + self.rules.read_with(guard).size_of(guard, ops)
    }
}

impl DeepCloneWithLock for ContainerRule {
    fn deep_clone_with_lock(&self, lock: &SharedRwLock, guard: &SharedRwLockReadGuard) -> Self {
        let rules = self.rules.read_with(guard);
        Self {
            conditions: self.conditions.clone(),
            rules: Arc::new(lock.wrap(rules.deep_clone_with_lock(lock, guard))),
            source_location: self.source_location,
        }
    }
}

impl ToCssWithGuard for ContainerRule {
    fn to_css(&self, guard: &SharedRwLockReadGuard, dest: &mut CssStringWriter) -> fmt::Result {
        dest.write_str("@container ")?;
        {
            let mut writer = CssWriter::new(dest);
            self.conditions.to_css(&mut writer)?;
        }
        self.rules.read_with(guard).to_css_block(guard, dest)
    }
}

/// Contains all container conditions for a container rule.
///
/// https://drafts.csswg.org/css-conditional-5/#container-rule
#[derive(Clone, Debug, ToCss, ToShmem)]
#[css(comma)]
pub struct ContainerConditions(#[css(iterable)] pub ArcSlice<ContainerCondition>);

/// A set of attribute names used for invalidation of attr() inside
/// style container queries.
#[derive(Clone, Debug, ToShmem)]
pub struct AttrReferenceSet {
    /// Referenced attribute names.
    pub names: PrecomputedHashSet<LocalName>,
    /// Whether all observed references were fully parsed as names;
    /// false if any `attr()` had nested references.
    pub is_fully_parsed: bool,
}

impl Default for AttrReferenceSet {
    fn default() -> Self {
        Self {
            names: Default::default(),
            is_fully_parsed: true,
        }
    }
}

/// Whether this container uses attr() references and if so whether or not this
/// is a named or unnamed container query. Used for determining which kind of
/// restyle hint we need when traversing invalidation.
#[derive(Debug, Clone, Copy, MallocSizeOf, PartialEq, Eq, PartialOrd, Ord)]
#[repr(C)]
pub enum ContainerAttributeDependencyKind {
    /// This container does not use attr() references.
    None = 0,
    /// This unnamed container uses attr() references.
    UnnamedContainer = 1,
    /// This named container uses attr() references.
    NamedContainer = 2,
}

impl ContainerAttributeDependencyKind {
    /// A named container that uses attr needs to invalidate all descendants that
    /// are affected by style container queries. If we only have an unnamed
    /// container or no attribute dependencies we keep looking for a condition that
    /// would require us to invalidate more.
    pub fn element_container_dependency_kind<E: TElement>(
        element: E,
        local_name: &LocalName,
        stylist: &Stylist,
    ) -> Self {
        let mut name_kind = ContainerAttributeDependencyKind::None;
        stylist.any_applicable_rule_data(element, |data| {
            let value = data.might_have_attribute_dependency_in_container(local_name);
            name_kind = std::cmp::max(name_kind, value);
            name_kind == ContainerAttributeDependencyKind::NamedContainer
        });
        name_kind
    }
}

/// A container condition and filter, combined.
#[derive(Debug, ToShmem, ToCss)]
pub struct ContainerCondition {
    #[css(skip_if = "ContainerName::is_none")]
    name: ContainerName,
    condition: Option<QueryCondition>,
    #[css(skip)]
    flags: FeatureFlags,
    #[css(skip)]
    attributes_referenced: AttrReferenceSet,
}

/// The result of a successful container query lookup.
pub struct ContainerLookupResult<E> {
    /// The relevant container.
    pub element: E,
    /// The sizing / writing-mode information of the container.
    pub info: ContainerInfo,
    /// The style of the element.
    pub style: Arc<ComputedValues>,
}

fn container_type_axes(ty_: ContainerType, wm: WritingMode) -> FeatureFlags {
    if ty_.intersects(ContainerType::SIZE) {
        FeatureFlags::all_container_axes()
    } else if ty_.intersects(ContainerType::INLINE_SIZE) {
        let physical_axis = if wm.is_vertical() {
            FeatureFlags::CONTAINER_REQUIRES_HEIGHT_AXIS
        } else {
            FeatureFlags::CONTAINER_REQUIRES_WIDTH_AXIS
        };
        FeatureFlags::CONTAINER_REQUIRES_INLINE_AXIS | physical_axis
    } else {
        FeatureFlags::empty()
    }
}

enum TraversalResult<T> {
    InProgress,
    StopTraversal,
    Done(T),
}

fn traverse_container<E, F, R>(
    mut e: E,
    originating_element_style: Option<&ComputedValues>,
    evaluator: F,
) -> Option<(E, R)>
where
    E: TElement,
    F: Fn(E, Option<&ComputedValues>) -> TraversalResult<R>,
{
    if originating_element_style.is_some() {
        match evaluator(e, originating_element_style) {
            TraversalResult::InProgress => {},
            TraversalResult::StopTraversal => return None,
            TraversalResult::Done(result) => return Some((e, result)),
        }
    }
    while let Some(element) = e.traversal_parent() {
        match evaluator(element, None) {
            TraversalResult::InProgress => {},
            TraversalResult::StopTraversal => return None,
            TraversalResult::Done(result) => return Some((element, result)),
        }
        e = element;
    }

    None
}

impl ContainerCondition {
    /// Get the name of this condition.
    #[inline]
    pub fn name(&self) -> &ContainerName {
        &self.name
    }
    /// Get the query condition of this condition
    #[inline]
    pub fn query_condition(&self) -> Option<&QueryCondition> {
        self.condition.as_ref()
    }
    /// Parse a container condition.
    pub fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let name = input
            .try_parse(|input| ContainerName::parse_for_query(context, input))
            .ok()
            .unwrap_or_else(ContainerName::none);
        let condition = input
            .try_parse(|input| QueryCondition::parse(context, input, FeatureType::Container))
            .ok();
        if condition.is_none() && name.is_none() {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        let mut attributes_referenced = AttrReferenceSet::default();
        if let Some(c) = condition.as_ref() {
            c.collect_attribute_references(&mut attributes_referenced)
        }
        let flags = condition
            .as_ref()
            .map_or(FeatureFlags::empty(), |c| c.cumulative_flags());
        Ok(Self {
            name,
            condition,
            flags,
            attributes_referenced,
        })
    }

    fn valid_container_info<E>(
        &self,
        potential_container: E,
        originating_element_style: Option<&ComputedValues>,
    ) -> TraversalResult<ContainerLookupResult<E>>
    where
        E: TElement,
    {
        let data;
        let style = match originating_element_style {
            Some(s) => s,
            None => {
                data = match potential_container.borrow_data() {
                    Some(d) => d,
                    None => return TraversalResult::InProgress,
                };
                &**data.styles.primary()
            },
        };
        let wm = style.writing_mode;
        let box_style = style.get_box();

        // Filter by container-type.
        let container_type = *box_style.get_container_type();
        let available_axes = container_type_axes(container_type, wm);
        if !available_axes.contains(self.flags.container_axes()) {
            return TraversalResult::InProgress;
        }

        // Filter by container-name.
        let container_name = box_style.get_container_name();
        for filter_name in self.name.0.iter() {
            if !container_name.0.contains(filter_name) {
                return TraversalResult::InProgress;
            }
        }

        let size = potential_container.query_container_size(box_style.get_display());
        let style = style.to_arc();
        TraversalResult::Done(ContainerLookupResult {
            element: potential_container,
            info: ContainerInfo {
                size,
                wm,
                inherited_style: {
                    potential_container.traversal_parent().and_then(|parent| {
                        parent
                            .borrow_data()
                            .and_then(|data| data.styles.get_primary().cloned())
                    })
                },
            },
            style,
        })
    }

    /// Performs container lookup for a given element.
    pub fn find_container<E>(
        &self,
        e: E,
        originating_element_style: Option<&ComputedValues>,
    ) -> Option<ContainerLookupResult<E>>
    where
        E: TElement,
    {
        traverse_container(
            e,
            originating_element_style,
            |element, originating_element_style| {
                self.valid_container_info(element, originating_element_style)
            },
        )
        .map(|(_, result)| result)
    }

    /// Tries to match a container query condition for a given element.
    pub fn matches<E>(
        &self,
        stylist: &Stylist,
        element: E,
        originating_element_style: Option<&ComputedValues>,
        invalidation_flags: &mut ComputedValueFlags,
    ) -> KleeneValue
    where
        E: TElement,
    {
        let result = self.find_container(element, originating_element_style);
        let condition = match self.condition {
            Some(ref c) => c,
            None => {
                // Condition-less container query (name only): matches if a
                // named container was found.
                return KleeneValue::from(result.is_some());
            },
        };
        // We have to tag the invalidation flags here because style container
        // query matching may return early if we cannot find a suitable
        // container element right now. However, we must also consider the case
        // when an ancestor becomes a container and we have to invalidate this
        // element from not matching to matching.
        if self.flags.contains(FeatureFlags::STYLE) {
            invalidation_flags.insert(ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY);
        }
        let (container, info) = match result {
            Some(r) => (r.element, (r.info, r.style)),
            None => {
                // If we did not find the named (or any) container,
                // the query must fail to match.
                return KleeneValue::False;
            },
        };
        // Set up the lookup for the container in question, as the condition may be using container
        // query lengths.
        let size_query_container_lookup = ContainerSizeQuery::for_element(
            container, /* known_parent_style = */ None, /* is_pseudo = */ false,
        );
        let mut attribute_tracker = AttributeTracker::new(&container);
        Context::for_container_query_evaluation(
            stylist.device(),
            Some(stylist),
            Some(info),
            size_query_container_lookup,
            &container,
            |context| {
                let matches = condition.matches(
                    context,
                    &mut CustomMediaEvaluator::none(),
                    &mut attribute_tracker,
                );
                let flags = context.style().flags();
                if flags.contains(ComputedValueFlags::USES_VIEWPORT_UNITS) {
                    // TODO(emilio): Might need something similar to improve
                    // invalidation of font relative container-query lengths.
                    invalidation_flags
                        .insert(ComputedValueFlags::USES_VIEWPORT_UNITS_ON_CONTAINER_QUERIES);
                }
                if flags.contains(ComputedValueFlags::USES_FONT_OR_WM_RELATIVE_UNITS) {
                    invalidation_flags.insert(
                        ComputedValueFlags::USES_FONT_OR_WM_RELATIVE_UNITS_ON_CONTAINER_QUERIES,
                    );
                }
                if flags.intersects(ComputedValueFlags::tree_counting_function_flags()) {
                    // Container query usage of sibling-index() and sibling-count() requires
                    // redoing selector matches on sibling changes. Although this is not itself
                    // a selector, the HAS_SLOW_SELECTOR flag is reused here because it has the
                    // required invalidation behavior.
                    container.apply_selector_flags(ElementSelectorFlags::HAS_SLOW_SELECTOR);
                }
                matches
            },
        )
    }

    /// Get the attribute functions referenced by this condition, if any.
    pub fn get_attributes_referenced(&self) -> &AttrReferenceSet {
        &self.attributes_referenced
    }

    /// Insert the attribute dependencies and attribute dependency kind into the
    /// attribute_dependencies set and the container_attr_function_dependencies map.
    /// Returns whether there are dependencies on attributes whose names could not
    /// be known at parse time.
    pub fn insert_attribute_references_in_dependency_map(
        &self,
        kind_map: &mut PrecomputedHashMap<LocalName, ContainerAttributeDependencyKind>,
        attribute_dependencies: &mut PrecomputedHashSet<LocalName>,
    ) -> ContainerAttributeDependencyKind {
        let name_kind = if self.name.is_none() {
            ContainerAttributeDependencyKind::UnnamedContainer
        } else {
            ContainerAttributeDependencyKind::NamedContainer
        };
        for attr in &self.get_attributes_referenced().names {
            kind_map
                .entry_ref(attr)
                .and_modify(|v| {
                    if *v == ContainerAttributeDependencyKind::UnnamedContainer {
                        *v = name_kind
                    }
                })
                .or_insert(name_kind);
            attribute_dependencies.get_or_insert_with(attr, Clone::clone);
        }
        if self.attributes_referenced.is_fully_parsed {
            ContainerAttributeDependencyKind::None
        } else {
            name_kind
        }
    }
}

/// Information needed to evaluate an individual container query.
#[derive(Clone)]
pub struct ContainerInfo {
    size: Size2D<Option<Au>>,
    wm: WritingMode,
    inherited_style: Option<Arc<ComputedValues>>,
}

impl ContainerInfo {
    fn size(&self) -> Option<Size2D<Au>> {
        Some(Size2D::new(self.size.width?, self.size.height?))
    }

    /// Get a reference to the container's inherited style, if any.
    pub fn inherited_style(&self) -> Option<&ComputedValues> {
        self.inherited_style.as_deref()
    }
}

fn eval_width(context: &Context) -> Option<CSSPixelLength> {
    let info = context.container_info.as_ref()?;
    Some(CSSPixelLength::new(info.size.width?.to_f32_px()))
}

fn eval_height(context: &Context) -> Option<CSSPixelLength> {
    let info = context.container_info.as_ref()?;
    Some(CSSPixelLength::new(info.size.height?.to_f32_px()))
}

fn eval_inline_size(context: &Context) -> Option<CSSPixelLength> {
    let info = context.container_info.as_ref()?;
    Some(CSSPixelLength::new(
        LogicalSize::from_physical(info.wm, info.size)
            .inline?
            .to_f32_px(),
    ))
}

fn eval_block_size(context: &Context) -> Option<CSSPixelLength> {
    let info = context.container_info.as_ref()?;
    Some(CSSPixelLength::new(
        LogicalSize::from_physical(info.wm, info.size)
            .block?
            .to_f32_px(),
    ))
}

fn eval_aspect_ratio(context: &Context) -> Option<Ratio> {
    let info = context.container_info.as_ref()?;
    Some(Ratio::new(
        info.size.width?.0 as f32,
        info.size.height?.0 as f32,
    ))
}

fn eval_orientation(context: &Context, value: Option<Orientation>) -> KleeneValue {
    let size = match context.container_info.as_ref().and_then(|info| info.size()) {
        Some(size) => size,
        None => return KleeneValue::Unknown,
    };
    KleeneValue::from(Orientation::eval(size, value))
}

/// https://drafts.csswg.org/css-contain-3/#container-features
///
/// TODO: Support style queries, perhaps.
pub static CONTAINER_FEATURES: [QueryFeatureDescription; 6] = [
    feature!(
        atom!("width"),
        AllowsRanges::Yes,
        Evaluator::OptionalLength(eval_width),
        FeatureFlags::CONTAINER_REQUIRES_WIDTH_AXIS,
    ),
    feature!(
        atom!("height"),
        AllowsRanges::Yes,
        Evaluator::OptionalLength(eval_height),
        FeatureFlags::CONTAINER_REQUIRES_HEIGHT_AXIS,
    ),
    feature!(
        atom!("inline-size"),
        AllowsRanges::Yes,
        Evaluator::OptionalLength(eval_inline_size),
        FeatureFlags::CONTAINER_REQUIRES_INLINE_AXIS,
    ),
    feature!(
        atom!("block-size"),
        AllowsRanges::Yes,
        Evaluator::OptionalLength(eval_block_size),
        FeatureFlags::CONTAINER_REQUIRES_BLOCK_AXIS,
    ),
    feature!(
        atom!("aspect-ratio"),
        AllowsRanges::Yes,
        Evaluator::OptionalNumberRatio(eval_aspect_ratio),
        // XXX from_bits_truncate is const, but the pipe operator isn't, so this
        // works around it.
        FeatureFlags::from_bits_truncate(
            FeatureFlags::CONTAINER_REQUIRES_BLOCK_AXIS.bits()
                | FeatureFlags::CONTAINER_REQUIRES_INLINE_AXIS.bits()
        ),
    ),
    feature!(
        atom!("orientation"),
        AllowsRanges::No,
        keyword_evaluator!(eval_orientation, Orientation),
        FeatureFlags::from_bits_truncate(
            FeatureFlags::CONTAINER_REQUIRES_BLOCK_AXIS.bits()
                | FeatureFlags::CONTAINER_REQUIRES_INLINE_AXIS.bits()
        ),
    ),
];

/// Result of a container size query, signifying the hypothetical containment boundary in terms of physical axes.
/// Defined by up to two size containers. Queries on logical axes are resolved with respect to the querying
/// element's writing mode.
#[derive(Copy, Clone, Default)]
pub struct ContainerSizeQueryResult {
    width: Option<Au>,
    height: Option<Au>,
}

impl ContainerSizeQueryResult {
    fn get_viewport_size(context: &Context) -> Size2D<Au> {
        use crate::values::specified::ViewportVariant;
        context.viewport_size_for_viewport_unit_resolution(ViewportVariant::Small)
    }

    fn get_logical_viewport_size(context: &Context) -> LogicalSize<Au> {
        LogicalSize::from_physical(
            context.builder.writing_mode,
            Self::get_viewport_size(context),
        )
    }

    /// Get the inline-size of the query container.
    pub fn get_container_inline_size(&self, context: &Context) -> Au {
        if context.builder.writing_mode.is_horizontal() {
            if let Some(w) = self.width {
                return w;
            }
        } else if let Some(h) = self.height {
            return h;
        }
        Self::get_logical_viewport_size(context).inline
    }

    /// Get the block-size of the query container.
    pub fn get_container_block_size(&self, context: &Context) -> Au {
        if context.builder.writing_mode.is_horizontal() {
            self.get_container_height(context)
        } else {
            self.get_container_width(context)
        }
    }

    /// Get the width of the query container.
    pub fn get_container_width(&self, context: &Context) -> Au {
        if let Some(w) = self.width {
            return w;
        }
        Self::get_viewport_size(context).width
    }

    /// Get the height of the query container.
    pub fn get_container_height(&self, context: &Context) -> Au {
        if let Some(h) = self.height {
            return h;
        }
        Self::get_viewport_size(context).height
    }

    // Merge the result of a subsequent lookup, preferring the initial result.
    fn merge(self, new_result: Self) -> Self {
        let mut result = self;
        if let Some(width) = new_result.width {
            result.width.get_or_insert(width);
        }
        if let Some(height) = new_result.height {
            result.height.get_or_insert(height);
        }
        result
    }

    fn is_complete(&self) -> bool {
        self.width.is_some() && self.height.is_some()
    }
}

/// Unevaluated lazy container size query.
pub enum ContainerSizeQuery<'a> {
    /// Query prior to evaluation.
    NotEvaluated(Box<dyn Fn() -> ContainerSizeQueryResult + 'a>),
    /// Cached evaluated result.
    Evaluated(ContainerSizeQueryResult),
}

impl<'a> ContainerSizeQuery<'a> {
    fn evaluate_potential_size_container<E>(
        e: E,
        originating_element_style: Option<&ComputedValues>,
    ) -> TraversalResult<ContainerSizeQueryResult>
    where
        E: TElement,
    {
        let data;
        let style = match originating_element_style {
            Some(s) => s,
            None => {
                data = match e.borrow_data() {
                    Some(d) => d,
                    None => return TraversalResult::InProgress,
                };
                &**data.styles.primary()
            },
        };
        if !style
            .flags
            .contains(ComputedValueFlags::SELF_OR_ANCESTOR_HAS_SIZE_CONTAINER_TYPE)
        {
            // We know we won't find a size container.
            return TraversalResult::StopTraversal;
        }

        let wm = style.writing_mode;
        let box_style = style.get_box();

        let container_type = *box_style.get_container_type();
        let size = e.query_container_size(box_style.get_display());
        if container_type.intersects(ContainerType::SIZE) {
            TraversalResult::Done(ContainerSizeQueryResult {
                width: size.width,
                height: size.height,
            })
        } else if container_type.intersects(ContainerType::INLINE_SIZE) {
            if wm.is_horizontal() {
                TraversalResult::Done(ContainerSizeQueryResult {
                    width: size.width,
                    height: None,
                })
            } else {
                TraversalResult::Done(ContainerSizeQueryResult {
                    width: None,
                    height: size.height,
                })
            }
        } else {
            TraversalResult::InProgress
        }
    }

    /// Find the query container size for a given element. Meant to be used as a callback for new().
    fn lookup<E>(
        element: E,
        originating_element_style: Option<&ComputedValues>,
    ) -> ContainerSizeQueryResult
    where
        E: TElement + 'a,
    {
        match traverse_container(
            element,
            originating_element_style,
            |e, originating_element_style| {
                Self::evaluate_potential_size_container(e, originating_element_style)
            },
        ) {
            Some((container, result)) => {
                if result.is_complete() {
                    result
                } else {
                    // Traverse up from the found size container to see if we can get a complete containment.
                    result.merge(Self::lookup(container, None))
                }
            },
            None => ContainerSizeQueryResult::default(),
        }
    }

    /// Create a new instance of the container size query for given element, with a deferred lookup callback.
    pub fn for_element<E>(
        element: E,
        known_parent_style: Option<&'a ComputedValues>,
        is_pseudo: bool,
    ) -> Self
    where
        E: TElement + 'a,
    {
        let parent;
        let data;
        let parent_style = match known_parent_style {
            Some(s) => Some(s),
            None => {
                // No need to bother if we're the top element.
                parent = match element.traversal_parent() {
                    Some(parent) => parent,
                    None => return Self::none(),
                };
                data = parent.borrow_data();
                data.as_ref().map(|data| &**data.styles.primary())
            },
        };

        // If there's no style, such as being `display: none` or so, we still want to show a
        // correct computed value, so give it a try.
        let should_traverse = parent_style.is_none_or(|s| {
            s.flags
                .contains(ComputedValueFlags::SELF_OR_ANCESTOR_HAS_SIZE_CONTAINER_TYPE)
        });
        if !should_traverse {
            return Self::none();
        }
        Self::NotEvaluated(Box::new(move || {
            Self::lookup(element, if is_pseudo { known_parent_style } else { None })
        }))
    }

    /// Create a new instance, but with optional element.
    pub fn for_option_element<E>(
        element: Option<E>,
        known_parent_style: Option<&'a ComputedValues>,
        is_pseudo: bool,
    ) -> Self
    where
        E: TElement + 'a,
    {
        if let Some(e) = element {
            Self::for_element(e, known_parent_style, is_pseudo)
        } else {
            Self::none()
        }
    }

    /// Create a query that evaluates to empty, for cases where container size query is not required.
    pub fn none() -> Self {
        ContainerSizeQuery::Evaluated(ContainerSizeQueryResult::default())
    }

    /// Get the result of the container size query, doing the lookup if called for the first time.
    pub fn get(&mut self) -> ContainerSizeQueryResult {
        match self {
            Self::NotEvaluated(lookup) => {
                *self = Self::Evaluated((lookup)());
                match self {
                    Self::Evaluated(info) => *info,
                    _ => unreachable!("Just evaluated but not set?"),
                }
            },
            Self::Evaluated(info) => *info,
        }
    }
}
