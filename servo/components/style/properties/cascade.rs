/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The main cascading algorithm of the style system.

use crate::applicable_declarations::{CascadePriority, RevertKind};
use crate::color::AbsoluteColor;
use crate::computed_value_flags::ComputedValueFlags;
use crate::context::TreeCountingCaches;
use crate::custom_properties::{
    get_attr_value_for_cycle_resolution, handle_invalid_at_computed_value_time,
    remove_and_insert_initial_value, substitute_references_if_needed_and_apply,
    ComputedCustomProperties, ComputedSubstitutionFunctions, Name, NonCustomReferenceMap,
    ReferenceFlags, References, SingleNonCustomReference, SubstitutionFunctionKind, VariableValue,
};
use crate::dom::{AttributeTracker, DummyElementContext, ElementContext, TElement};
#[cfg(feature = "gecko")]
use crate::font_metrics::FontMetricsOrientation;
use crate::properties::{
    property_counts, CSSWideKeyword, ComputedValues, DeclarationImportanceIterator, LonghandId,
    LonghandIdSet, PrioritaryPropertyId, PrioritaryPropertyIdSet, PropertyDeclaration,
    PropertyDeclarationId, PropertyFlags, ShorthandsWithPropertyReferencesCache, StyleBuilder,
    CASCADE_PROPERTY,
};
use crate::properties::{CustomDeclaration, CustomDeclarationValue, UnparsedValue};
use crate::properties_and_values::rule::Descriptors as PropertyDescriptors;
use crate::properties_and_values::value::ComputedValue as ComputedRegisteredValue;
use crate::rule_cache::{RuleCache, RuleCacheConditions};
use crate::rule_tree::{CascadeLevel, CascadeOrigin, RuleCascadeFlags, StrongRuleNode};
use crate::selector_map::{PrecomputedHashMap, PrecomputedHashSet};
use crate::selector_parser::PseudoElement;
use crate::shared_lock::StylesheetGuards;
use crate::style_adjuster::StyleAdjuster;
use crate::stylesheets::container_rule::ContainerSizeQuery;
use crate::stylesheets::layer_rule::LayerOrder;
use crate::stylesheets::UrlExtraData;
use crate::stylist::Stylist;
use crate::values::computed::ToComputedValue;
#[cfg(feature = "gecko")]
use crate::values::specified::length::FontBaseSize;
use crate::values::specified::position::PositionTryFallbacksTryTactic;
use crate::values::{computed, specified};
use rustc_hash::FxHashMap;
use selectors::matching::ElementSelectorFlags;
use servo_arc::Arc;
use smallvec::SmallVec;
use std::borrow::Cow;
use std::cmp;
use std::collections::hash_map::Entry;

/// Whether we're resolving a style with the purposes of reparenting for ::first-line.
#[derive(Copy, Clone)]
#[allow(missing_docs)]
pub enum FirstLineReparenting<'a> {
    No,
    Yes {
        /// The style we're re-parenting for ::first-line. ::first-line only affects inherited
        /// properties so we use this to avoid some work and also ensure correctness by copying the
        /// reset structs from this style.
        style_to_reparent: &'a ComputedValues,
    },
}

/// Performs the CSS cascade, computing new styles for an element from its parent style.
///
/// The arguments are:
///
///   * `device`: Used to get the initial viewport and other external state.
///
///   * `rule_node`: The rule node in the tree that represent the CSS rules that
///   matched.
///
///   * `parent_style`: The parent style, if applicable; if `None`, this is the root node.
///
/// Returns the computed values.
///   * `flags`: Various flags.
///
pub fn cascade<E>(
    stylist: &Stylist,
    pseudo: Option<&PseudoElement>,
    rule_node: &StrongRuleNode,
    guards: &StylesheetGuards,
    parent_style: Option<&ComputedValues>,
    layout_parent_style: Option<&ComputedValues>,
    first_line_reparenting: FirstLineReparenting,
    try_tactic: &PositionTryFallbacksTryTactic,
    visited_rules: Option<&StrongRuleNode>,
    cascade_input_flags: ComputedValueFlags,
    included_cascade_flags: RuleCascadeFlags,
    rule_cache: Option<&RuleCache>,
    rule_cache_conditions: &mut RuleCacheConditions,
    element: Option<E>,
    tree_counting_caches: &mut TreeCountingCaches,
) -> Arc<ComputedValues>
where
    E: TElement,
{
    cascade_rules(
        stylist,
        pseudo,
        rule_node,
        guards,
        parent_style,
        layout_parent_style,
        first_line_reparenting,
        try_tactic,
        CascadeMode::Unvisited { visited_rules },
        cascade_input_flags,
        included_cascade_flags,
        rule_cache,
        rule_cache_conditions,
        element,
        tree_counting_caches,
    )
}

struct DeclarationIterator<'a> {
    // Global to the iteration.
    guards: &'a StylesheetGuards<'a>,
    restriction: PropertyFlags,
    // The rule we're iterating over.
    current_rule_node: Option<&'a StrongRuleNode>,
    // Per rule state.
    declarations: DeclarationImportanceIterator<'a>,
    priority: CascadePriority,
}

impl<'a> DeclarationIterator<'a> {
    #[inline]
    fn new(
        rule_node: &'a StrongRuleNode,
        guards: &'a StylesheetGuards,
        pseudo: Option<&PseudoElement>,
    ) -> Self {
        let restriction = pseudo.map_or(PropertyFlags::empty(), |p| p.property_restriction());
        let mut iter = Self {
            guards,
            current_rule_node: Some(rule_node),
            priority: CascadePriority::new(
                CascadeLevel::new(CascadeOrigin::UA),
                LayerOrder::root(),
                RuleCascadeFlags::empty(),
            ),
            declarations: DeclarationImportanceIterator::default(),
            restriction,
        };
        iter.update_for_node(rule_node);
        iter
    }

    fn update_for_node(&mut self, node: &'a StrongRuleNode) {
        self.priority = node.cascade_priority();
        let guard = self.priority.cascade_level().origin().guard(self.guards);
        self.declarations = match node.style_source() {
            Some(source) => source.read(guard).declaration_importance_iter(),
            None => DeclarationImportanceIterator::default(),
        };
    }
}

impl<'a> Iterator for DeclarationIterator<'a> {
    type Item = (&'a PropertyDeclaration, CascadePriority);

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        loop {
            if let Some((decl, importance)) = self.declarations.next_back() {
                if self.priority.cascade_level().is_important() != importance.important() {
                    continue;
                }

                if !self.restriction.is_empty() {
                    // decl.id() is either a longhand or a custom
                    // property.  Custom properties are always allowed, but
                    // longhands are only allowed if they have our
                    // restriction flag set.
                    if let PropertyDeclarationId::Longhand(id) = decl.id() {
                        if !id.flags().contains(self.restriction)
                            && self.priority.cascade_level().origin() != CascadeOrigin::UA
                        {
                            continue;
                        }
                    }
                }

                return Some((decl, self.priority));
            }

            let next_node = self.current_rule_node.take()?.parent()?;
            self.current_rule_node = Some(next_node);
            self.update_for_node(next_node);
        }
    }
}

fn cascade_rules<E>(
    stylist: &Stylist,
    pseudo: Option<&PseudoElement>,
    rule_node: &StrongRuleNode,
    guards: &StylesheetGuards,
    parent_style: Option<&ComputedValues>,
    layout_parent_style: Option<&ComputedValues>,
    first_line_reparenting: FirstLineReparenting,
    try_tactic: &PositionTryFallbacksTryTactic,
    cascade_mode: CascadeMode,
    cascade_input_flags: ComputedValueFlags,
    included_cascade_flags: RuleCascadeFlags,
    rule_cache: Option<&RuleCache>,
    rule_cache_conditions: &mut RuleCacheConditions,
    element: Option<E>,
    tree_counting_caches: &mut TreeCountingCaches,
) -> Arc<ComputedValues>
where
    E: TElement,
{
    apply_declarations(
        stylist,
        pseudo,
        rule_node,
        guards,
        DeclarationIterator::new(rule_node, guards, pseudo),
        parent_style,
        layout_parent_style,
        first_line_reparenting,
        try_tactic,
        cascade_mode,
        cascade_input_flags,
        included_cascade_flags,
        rule_cache,
        rule_cache_conditions,
        element,
        tree_counting_caches,
    )
}

/// Whether we're cascading for visited or unvisited styles.
#[derive(Clone, Copy)]
pub enum CascadeMode<'a, 'b> {
    /// We're cascading for unvisited styles.
    Unvisited {
        /// The visited rules that should match the visited style.
        visited_rules: Option<&'a StrongRuleNode>,
    },
    /// We're cascading for visited styles.
    Visited {
        /// The cascade for our unvisited style.
        unvisited_context: &'a computed::Context<'b>,
        /// The properties set on the unvisited style. These are useful if we can prove that the
        /// visited rules are the same as the unvisited declarations.
        unvisited_properties: &'a LonghandIdSet,
    },
}

fn iter_declarations<'c, 'decls: 'c>(
    iter: impl Iterator<Item = (&'decls PropertyDeclaration, CascadePriority)>,
    declarations: &mut Declarations<'decls>,
    mut custom: Option<(&mut Cascade<'c>, &mut computed::Context)>,
    attribute_tracker: &mut AttributeTracker,
) {
    for (declaration, priority) in iter {
        if let PropertyDeclaration::Custom(ref declaration) = *declaration {
            if let Some((ref mut cascade, ref mut context)) = custom {
                cascade.cascade_custom_property(context, declaration, priority);
            }
        } else {
            let id = declaration.id().as_longhand().unwrap();
            declarations.note_declaration(declaration, priority, id);
            if Cascade::might_have_non_custom_or_attr_dependency(id, declaration) {
                if let Some((ref mut cascade, ref mut context)) = custom {
                    cascade.maybe_note_non_custom_dependency(
                        context,
                        id,
                        declaration,
                        attribute_tracker,
                    );
                }
            }
        }
    }
}

/// NOTE: This function expects the declaration with more priority to appear
/// first.
pub fn apply_declarations<'decls, E, I>(
    stylist: &Stylist,
    pseudo: Option<&PseudoElement>,
    rules: &StrongRuleNode,
    guards: &StylesheetGuards,
    iter: I,
    parent_style: Option<&ComputedValues>,
    layout_parent_style: Option<&ComputedValues>,
    first_line_reparenting: FirstLineReparenting<'_>,
    try_tactic: &PositionTryFallbacksTryTactic,
    cascade_mode: CascadeMode,
    cascade_input_flags: ComputedValueFlags,
    included_cascade_flags: RuleCascadeFlags,
    rule_cache: Option<&RuleCache>,
    rule_cache_conditions: &mut RuleCacheConditions,
    element: Option<E>,
    tree_counting_caches: &mut TreeCountingCaches,
) -> Arc<ComputedValues>
where
    E: TElement,
    I: Iterator<Item = (&'decls PropertyDeclaration, CascadePriority)>,
{
    debug_assert!(layout_parent_style.is_none() || parent_style.is_some());
    let device = stylist.device();
    let inherited_style = parent_style.unwrap_or(device.default_computed_values());
    let is_root_element = pseudo.is_none() && element.is_some_and(|e| e.is_root());
    let container_size_query =
        ContainerSizeQuery::for_option_element(element, Some(inherited_style), pseudo.is_some());

    let originating_element = element.map(|e| e.ultimate_originating_element());
    let element_context = match originating_element {
        Some(ref e) => e as &dyn ElementContext,
        None => &DummyElementContext {},
    };

    let mut context = computed::Context::new(
        // We'd really like to own the rules here to avoid refcount traffic, but
        // animation's usage of `apply_declarations` make this tricky. See bug
        // 1375525.
        StyleBuilder::new(
            device,
            Some(stylist),
            parent_style,
            pseudo,
            Some(rules.clone()),
            is_root_element,
        ),
        stylist.quirks_mode(),
        rule_cache_conditions,
        container_size_query,
        included_cascade_flags,
        element_context,
        tree_counting_caches,
    );

    context.style().add_flags(cascade_input_flags);
    // Reuse flags from computing registered custom properties initial values, such as
    // whether they depend on viewport units.
    context
        .style()
        .add_flags(stylist.get_custom_property_initial_values_flags());

    let using_cached_reset_properties;
    let ignore_colors = context.builder.device.forced_colors().is_active();
    let mut cascade = Cascade::new(first_line_reparenting, stylist, ignore_colors);
    let mut declarations = Default::default();
    let mut shorthand_cache = ShorthandsWithPropertyReferencesCache::default();
    let mut attribute_tracker = AttributeTracker::new(element_context);

    let properties_to_apply = match cascade_mode {
        CascadeMode::Visited {
            unvisited_context,
            unvisited_properties,
        } => {
            context.builder.substitution_functions =
                unvisited_context.builder.substitution_functions.clone();
            context.builder.writing_mode = unvisited_context.builder.writing_mode;
            context.builder.color_scheme = unvisited_context.builder.color_scheme;
            // We never insert visited styles into the cache so we don't need to try looking it up.
            // It also wouldn't be super-profitable, only a handful :visited properties are
            // non-inherited.
            using_cached_reset_properties = false;
            let visited_dependent_props = LonghandIdSet::visited_dependent();
            // If we match the same rules when visited and unvisited, and we know there isn't any
            // visited-dependent properties, we can avoid gathering the declarations, we know none
            // would be relevant.
            if unvisited_context.builder.rules.as_ref() != Some(rules)
                || unvisited_properties.contains_any(visited_dependent_props)
            {
                iter_declarations(iter, &mut declarations, None, &mut attribute_tracker);
            }
            visited_dependent_props
        },
        CascadeMode::Unvisited { .. } => {
            cascade.init_custom_properties(&mut context);
            iter_declarations(
                iter,
                &mut declarations,
                Some((&mut cascade, &mut context)),
                &mut attribute_tracker,
            );
            // Detect cycles, remove properties participating in them, and resolve custom
            // properties, applying prioritary properties (font-size, color-scheme, line-height)
            // interleaved so that a custom property depending on `em`/`lh`/the used color-scheme is
            // substituted once the prioritary property it needs has been applied.
            cascade.apply_custom_and_prioritary_properties(
                &mut context,
                &declarations,
                &mut shorthand_cache,
                &mut attribute_tracker,
            );

            using_cached_reset_properties =
                cascade.try_to_use_cached_reset_properties(&mut context, rule_cache, guards);

            if using_cached_reset_properties {
                LonghandIdSet::late_group_only_inherited()
            } else {
                LonghandIdSet::late_group()
            }
        },
    };

    cascade.apply_non_prioritary_properties(
        &mut context,
        &declarations.longhand_declarations,
        &mut shorthand_cache,
        properties_to_apply,
        &mut attribute_tracker,
    );

    context.builder.attribute_references = attribute_tracker.finalize();

    cascade.finished_applying_properties(&mut context.builder);

    context.builder.clear_modified_reset();

    if let CascadeMode::Unvisited { visited_rules } = cascade_mode {
        if let Some(visited_rules) = visited_rules {
            cascade.compute_visited_style_if_needed(
                &mut context,
                element,
                parent_style,
                layout_parent_style,
                try_tactic,
                visited_rules,
                guards,
            );
        }

        StyleAdjuster::new(&mut context.builder).adjust(
            layout_parent_style.unwrap_or(inherited_style),
            element,
            try_tactic,
            &cascade.author_specified,
        );
    }

    if context.builder.modified_reset() || using_cached_reset_properties {
        // If we adjusted any reset structs, we can't cache this ComputedValues.
        //
        // Also, if we re-used existing reset structs, don't bother caching it back again. (Aside
        // from being wasted effort, it will be wrong, since context.rule_cache_conditions won't be
        // set appropriately if we didn't compute those reset properties.)
        context.rule_cache_conditions.borrow_mut().set_uncacheable();
    }

    if context
        .builder
        .flags()
        .intersects(ComputedValueFlags::tree_counting_function_flags())
    {
        if let Some(el) = element {
            el.apply_selector_flags(ElementSelectorFlags::MAY_HAVE_TREE_COUNTING_FUNCTION);
        } else {
            debug_assert!(
                false,
                "Tree counting function flag applied without an element?"
            );
        }
    }

    context.builder.build()
}

/// For ignored colors mode, we sometimes want to do something equivalent to
/// "revert-or-initial", where we `revert` for a given origin, but then apply a
/// given initial value if nothing in other origins did override it.
///
/// This is a bit of a clunky way of achieving this.
type DeclarationsToApplyUnlessOverriden = SmallVec<[PropertyDeclaration; 2]>;

fn is_base_appearance(context: &computed::Context) -> bool {
    use computed::Appearance;
    let box_style = context.builder.get_box();
    match box_style.clone_appearance() {
        Appearance::BaseSelect => {
            matches!(
                box_style.clone__moz_default_appearance(),
                Appearance::Listbox | Appearance::Menulist
            )
        },
        Appearance::Base => box_style.clone__moz_default_appearance() != Appearance::None,
        _ => false,
    }
}

fn tweak_when_ignoring_colors(
    context: &computed::Context,
    longhand_id: LonghandId,
    origin: CascadeOrigin,
    declaration: &mut Cow<PropertyDeclaration>,
    declarations_to_apply_unless_overridden: &mut DeclarationsToApplyUnlessOverriden,
) {
    use crate::values::computed::ToComputedValue;
    use crate::values::specified::Color;

    if !longhand_id.ignored_when_document_colors_disabled() {
        return;
    }

    let is_ua_or_user_rule = matches!(origin, CascadeOrigin::User | CascadeOrigin::UA);
    if is_ua_or_user_rule {
        return;
    }

    // Always honor colors if forced-color-adjust is set to none.
    let forced = context
        .builder
        .get_inherited_text()
        .clone_forced_color_adjust();
    if forced == computed::ForcedColorAdjust::None {
        return;
    }

    fn alpha_channel(color: &Color, context: &computed::Context) -> f32 {
        // We assume here currentColor is opaque.
        color
            .to_computed_value(context)
            .resolve_to_absolute(&AbsoluteColor::BLACK)
            .alpha
    }

    // A few special-cases ahead.
    match **declaration {
        // Honor CSS-wide keywords like unset / revert / initial...
        PropertyDeclaration::CSSWideKeyword(..) => return,
        PropertyDeclaration::BackgroundColor(ref color) => {
            // We honor system colors and transparent colors unconditionally.
            //
            // NOTE(emilio): We honor transparent unconditionally, like we do
            // for color, even though it causes issues like bug 1625036. The
            // reasoning is that the conditions that trigger that (having
            // mismatched widget and default backgrounds) are both uncommon, and
            // broken in other applications as well, and not honoring
            // transparent makes stuff uglier or break unconditionally
            // (bug 1666059, bug 1755713).
            if color.honored_in_forced_colors_mode(context, /* allow_transparent = */ true) {
                return;
            }
            // For background-color, we revert or initial-with-preserved-alpha
            // otherwise, this is needed to preserve semi-transparent
            // backgrounds.
            let alpha = alpha_channel(color, context);
            if alpha == 0.0 {
                return;
            }
            let mut color = context.builder.device.default_background_color();
            color.alpha = alpha;
            declarations_to_apply_unless_overridden
                .push(PropertyDeclaration::BackgroundColor(color.into()))
        },
        PropertyDeclaration::Color(ref color) => {
            // We honor color: transparent and system colors.
            if color
                .0
                .honored_in_forced_colors_mode(context, /* allow_transparent = */ true)
            {
                return;
            }
            // If the inherited color would be transparent, but we would
            // override this with a non-transparent color, then override it with
            // the default color. Otherwise just let it inherit through.
            if context
                .builder
                .get_parent_inherited_text()
                .clone_color()
                .alpha
                == 0.0
            {
                let color = context.builder.device.default_color();
                declarations_to_apply_unless_overridden.push(PropertyDeclaration::Color(
                    specified::ColorPropertyValue(color.into()),
                ))
            }
        },
        // We honor url background-images if backplating.
        #[cfg(feature = "gecko")]
        PropertyDeclaration::BackgroundImage(ref bkg) => {
            use crate::values::generics::image::Image;
            if crate::pref!("browser.display.permit_backplate")
                && bkg
                    .0
                    .iter()
                    .all(|image| matches!(*image, Image::Url(..) | Image::None))
            {
                return;
            }
        },
        _ => {
            // We honor system colors more generally for all colors.
            //
            // We used to honor transparent but that causes accessibility
            // regressions like bug 1740924.
            //
            // NOTE(emilio): This doesn't handle caret-color and accent-color
            // because those use a slightly different syntax (<color> | auto for
            // example).
            //
            // That's probably fine though, as using a system color for
            // caret-color doesn't make sense (using currentColor is fine), and
            // we ignore accent-color in high-contrast-mode anyways.
            if let Some(color) = declaration.color_value() {
                if color
                    .honored_in_forced_colors_mode(context, /* allow_transparent = */ false)
                {
                    return;
                }
            }
        },
    }

    *declaration.to_mut() =
        PropertyDeclaration::css_wide_keyword(longhand_id, CSSWideKeyword::Revert);
}

/// We track the index only for prioritary properties. For other properties we can just iterate.
type DeclarationIndex = u16;

/// "Prioritary" properties are properties that other properties depend on in one way or another.
///
/// We keep track of their position in the declaration vector, in order to be able to cascade them
/// separately in precise order.
#[derive(Copy, Clone)]
struct PrioritaryDeclarationPosition {
    // DeclarationIndex::MAX signals no index.
    most_important: DeclarationIndex,
    least_important: DeclarationIndex,
}

impl Default for PrioritaryDeclarationPosition {
    fn default() -> Self {
        Self {
            most_important: DeclarationIndex::MAX,
            least_important: DeclarationIndex::MAX,
        }
    }
}

#[derive(Copy, Clone)]
struct Declaration<'a> {
    decl: &'a PropertyDeclaration,
    priority: CascadePriority,
    next_index: DeclarationIndex,
}

/// The set of property declarations from our rules.
#[derive(Default)]
pub(crate) struct Declarations<'a> {
    /// Whether we have any prioritary property. This is just a minor optimization.
    has_prioritary_properties: bool,
    /// A list of all the applicable longhand declarations.
    longhand_declarations: SmallVec<[Declaration<'a>; 64]>,
    /// The prioritary property position data.
    prioritary_positions: [PrioritaryDeclarationPosition; property_counts::PRIORITARY],
}

impl<'a> Declarations<'a> {
    fn note_prioritary_property(&mut self, id: PrioritaryPropertyId) {
        let new_index = self.longhand_declarations.len();
        if new_index >= DeclarationIndex::MAX as usize {
            // This prioritary property is past the amount of declarations we can track. Let's give
            // up applying it to prevent getting confused.
            return;
        }

        self.has_prioritary_properties = true;
        let new_index = new_index as DeclarationIndex;
        let position = &mut self.prioritary_positions[id as usize];
        if position.most_important == DeclarationIndex::MAX {
            // We still haven't seen this property, record the current position as the most
            // prioritary index.
            position.most_important = new_index;
        } else {
            // Let the previous item in the list know about us.
            self.longhand_declarations[position.least_important as usize].next_index = new_index;
        }
        position.least_important = new_index;
    }

    fn note_declaration(
        &mut self,
        decl: &'a PropertyDeclaration,
        priority: CascadePriority,
        id: LonghandId,
    ) {
        if let Some(id) = PrioritaryPropertyId::from_longhand(id) {
            self.note_prioritary_property(id);
        }
        self.longhand_declarations.push(Declaration {
            decl,
            priority,
            next_index: 0,
        });
    }
}

#[derive(Default)]
struct RevertedSet {
    // Just to avoid the hashmap lookup in the common case.
    longhands_set: LonghandIdSet,
    longhands: FxHashMap<LonghandId, (CascadePriority, RevertKind)>,
    custom: PrecomputedHashMap<Name, (CascadePriority, RevertKind)>,
}

#[derive(Default)]
struct SeenSubstitutionFunctions<'a> {
    /// The boolean means whether the value may have references. If false, we don't need to bother
    /// performing lookups for cycle detection.
    var: PrecomputedHashMap<&'a Name, bool>,
    attr: PrecomputedHashSet<&'a Name>,
}

#[derive(Default)]
struct SeenSet<'a> {
    longhands: LonghandIdSet,
    custom: SeenSubstitutionFunctions<'a>,
}

/// Only registered (typed) properties emit a non-custom edge, from their own value.
fn find_non_custom_references(
    registration: &PropertyDescriptors,
    value: &VariableValue,
    is_root_element: bool,
) -> ReferenceFlags {
    use crate::properties_and_values::syntax::data_type::DependentDataTypes;

    let mut result = ReferenceFlags::empty();
    let Some(syntax) = registration.syntax.as_ref() else {
        return result;
    };
    let dependent_types = syntax.dependent_types();
    let may_reference_length = dependent_types.intersects(DependentDataTypes::LENGTH);
    if may_reference_length {
        result |= value.references.non_custom_references(is_root_element);
    }
    if dependent_types.intersects(DependentDataTypes::COLOR) {
        // The value depends on the used color-scheme (e.g. a `<color>` property referencing
        // `light-dark()` or a system color).
        result |= ReferenceFlags::COLOR_SCHEME;
    }
    result
}

/// Resolves the custom properties for a single `@keyframes` keyframe, layered on top of the base
/// style's already-computed custom properties.
///
/// `@keyframes` aren't part of the regular cascade (see bug 1883255), so this lets the caller seed
/// the substitution map with the element's computed custom properties, cascade the keyframe's
/// custom declarations on top, and resolve them (running cycle detection and substitution) into
/// `context.builder.substitution_functions`, which the subsequent animation-value computation reads
/// to substitute `var()` references.
pub struct KeyframeCustomPropertiesBuilder<'a> {
    cascade: Cascade<'a>,
    decls: Declarations<'a>,
    shorthand_cache: ShorthandsWithPropertyReferencesCache,
}

impl<'a> KeyframeCustomPropertiesBuilder<'a> {
    /// Creates a new builder, seeding the substitution map with `base` (typically the element's
    /// computed custom properties).
    pub fn new(
        stylist: &'a Stylist,
        context: &mut computed::Context,
        base: ComputedCustomProperties,
    ) -> Self {
        context.builder.substitution_functions =
            ComputedSubstitutionFunctions::new(Some(base), None);
        Self {
            cascade: Cascade::new_for_custom_properties_only(stylist),
            decls: Declarations::default(),
            shorthand_cache: ShorthandsWithPropertyReferencesCache::default(),
        }
    }

    /// Cascades a single custom-property declaration from the keyframe.
    pub fn cascade(
        &mut self,
        context: &mut computed::Context,
        declaration: &'a CustomDeclaration,
        priority: CascadePriority,
    ) {
        self.cascade
            .cascade_custom_property(context, declaration, priority);
    }

    /// Resolves the cascaded custom properties into `context.builder.substitution_functions`.
    pub fn build(
        mut self,
        context: &mut computed::Context,
        attribute_tracker: &mut AttributeTracker,
    ) {
        self.cascade.apply_custom_and_prioritary_properties(
            context,
            &self.decls,
            &mut self.shorthand_cache,
            attribute_tracker,
        );
    }
}

pub(crate) struct Cascade<'a> {
    first_line_reparenting: FirstLineReparenting<'a>,
    stylist: &'a Stylist,
    ignore_colors: bool,
    seen: SeenSet<'a>,
    reverted: RevertedSet,
    author_specified: LonghandIdSet,
    declarations_to_apply_unless_overridden: DeclarationsToApplyUnlessOverriden,
    may_have_custom_property_cycles: bool,
    references_from_non_custom_properties: NonCustomReferenceMap<Vec<Arc<UnparsedValue>>>,
    /// Set of prioritary properties that have already been applied.
    ensured_prioritary: PrioritaryPropertyIdSet,
}

impl<'a> Cascade<'a> {
    fn new(
        first_line_reparenting: FirstLineReparenting<'a>,
        stylist: &'a Stylist,
        ignore_colors: bool,
    ) -> Self {
        Self {
            first_line_reparenting,
            stylist,
            ignore_colors,
            seen: Default::default(),
            author_specified: Default::default(),
            reverted: Default::default(),
            declarations_to_apply_unless_overridden: Default::default(),
            may_have_custom_property_cycles: false,
            ensured_prioritary: PrioritaryPropertyIdSet::default(),
            references_from_non_custom_properties: Default::default(),
        }
    }

    /// Creates a `Cascade` for resolving custom properties outside of a full cascade (e.g. for
    /// keyframes). The visited-style and position-try paths aren't reachable in this mode.
    fn new_for_custom_properties_only(stylist: &'a Stylist) -> Self {
        Self {
            first_line_reparenting: FirstLineReparenting::No,
            stylist,
            ignore_colors: false,
            seen: Default::default(),
            author_specified: Default::default(),
            reverted: Default::default(),
            declarations_to_apply_unless_overridden: Default::default(),
            may_have_custom_property_cycles: false,
            ensured_prioritary: PrioritaryPropertyIdSet::default(),
            references_from_non_custom_properties: Default::default(),
        }
    }

    fn substitute_variables_if_needed<'cache, 'decl>(
        &self,
        context: &mut computed::Context,
        shorthand_cache: &'cache mut ShorthandsWithPropertyReferencesCache,
        declaration: &'decl PropertyDeclaration,
        attribute_tracker: &mut AttributeTracker,
    ) -> Cow<'decl, PropertyDeclaration>
    where
        'cache: 'decl,
    {
        let declaration = match *declaration {
            PropertyDeclaration::WithVariables(ref declaration) => declaration,
            ref d => return Cow::Borrowed(d),
        };

        if !declaration.id.inherited() {
            context.rule_cache_conditions.borrow_mut().set_uncacheable();

            // NOTE(emilio): We only really need to add the `display` /
            // `content` flag if the CSS variable has not been specified on our
            // declarations, but we don't have that information at this point,
            // and it doesn't seem like an important enough optimization to
            // warrant it.
            if matches!(declaration.id, LonghandId::Display | LonghandId::Content) {
                context
                    .builder
                    .add_flags(ComputedValueFlags::DISPLAY_OR_CONTENT_DEPEND_ON_INHERITED_STYLE);
            }
        }

        debug_assert!(
            context.builder.stylist.is_some(),
            "Need a Stylist to substitute variables!"
        );
        declaration.value.substitute_variables(
            declaration.id,
            context.builder.substitution_functions(),
            context.builder.stylist.unwrap(),
            context,
            shorthand_cache,
            attribute_tracker,
        )
    }

    fn apply_one_prioritary_property(
        &mut self,
        context: &mut computed::Context,
        decls: &Declarations,
        cache: &mut ShorthandsWithPropertyReferencesCache,
        id: PrioritaryPropertyId,
        attr_provider: &mut AttributeTracker,
    ) {
        let mut index = decls.prioritary_positions[id as usize].most_important;
        if index == DeclarationIndex::MAX {
            return;
        }

        let longhand_id = id.to_longhand();
        debug_assert!(
            !longhand_id.is_logical(),
            "That could require more book-keeping"
        );
        loop {
            let decl = decls.longhand_declarations[index as usize];
            self.apply_one_longhand(
                context,
                longhand_id,
                decl.decl,
                decl.priority,
                cache,
                attr_provider,
            );
            if self.seen.longhands.contains(longhand_id) {
                // Found it!
                self.did_apply_prioritary_property(context, id);
                return;
            }
            debug_assert!(
                decl.next_index == 0 || decl.next_index > index,
                "should make progress! {} -> {}",
                index,
                decl.next_index,
            );
            index = decl.next_index;
            if index == 0 {
                return;
            }
        }
    }

    /// Some prioritary properties need book-keeping, which this takes care of.
    fn did_apply_prioritary_property(
        &mut self,
        context: &mut computed::Context,
        id: PrioritaryPropertyId,
    ) {
        use crate::properties::PrioritaryPropertyId::*;
        match id {
            Appearance => {
                if is_base_appearance(context) {
                    context
                        .style()
                        .add_flags(ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE);
                    context
                        .included_cascade_flags
                        .insert(RuleCascadeFlags::APPEARANCE_BASE);
                }
            },
            WritingMode | Direction | TextOrientation => {
                context.builder.writing_mode =
                    crate::logical_geometry::WritingMode::new(context.builder.get_inherited_box());
            },
            Zoom => {
                context.builder.recompute_effective_zooms();
                if !context.builder.effective_zoom_for_inheritance.is_one() {
                    // NOTE(emilio): This is a bit of a hack, but matches the shipped WebKit and Blink
                    // behavior for now. Ideally, in the future, we have a pass over all
                    // implicitly-or-explicitly-inherited properties that can contain lengths and
                    // re-compute them properly, see https://github.com/w3c/csswg-drafts/issues/9397.
                    // TODO(emilio): we need to eagerly do this for line-height as well, probably.
                    self.recompute_font_size_for_zoom_change(&mut context.builder);
                }
            },
            XLang => {
                #[cfg(feature = "gecko")]
                self.recompute_initial_font_family_if_needed(&mut context.builder);
                self.recompute_keyword_font_size_if_needed(context);
            },
            FontFamily => {
                #[cfg(feature = "gecko")]
                self.prioritize_user_fonts_if_needed(&mut context.builder);
                self.recompute_keyword_font_size_if_needed(context);
            },
            FontSize => {
                if self.seen.longhands.contains(LonghandId::MathDepth) {
                    #[cfg(feature = "gecko")]
                    Self::recompute_math_font_size_if_needed(context);
                }
                if self.seen.longhands.contains(LonghandId::XLang)
                    || self.seen.longhands.contains(LonghandId::FontFamily)
                {
                    self.recompute_keyword_font_size_if_needed(context);
                }
                #[cfg(feature = "gecko")]
                self.constrain_font_size_if_needed(&mut context.builder);
            },
            XTextScale => {
                #[cfg(feature = "gecko")]
                self.unzoom_fonts_if_needed(&mut context.builder);
            },
            MozMinFontSizeRatio => {
                #[cfg(feature = "gecko")]
                self.constrain_font_size_if_needed(&mut context.builder);
            },
            ColorScheme => {
                context.builder.color_scheme =
                    context.builder.get_inherited_ui().color_scheme_bits();
            },
            MozDefaultAppearance | MathDepth | FontWeight | FontWidth | FontStyle
            | FontSizeAdjust | ForcedColorAdjust | LineHeight => {},
        }
    }

    fn apply_non_prioritary_properties(
        &mut self,
        context: &mut computed::Context,
        longhand_declarations: &[Declaration],
        shorthand_cache: &mut ShorthandsWithPropertyReferencesCache,
        properties_to_apply: &LonghandIdSet,
        attribute_tracker: &mut AttributeTracker,
    ) {
        debug_assert!(!properties_to_apply.contains_any(LonghandIdSet::prioritary_properties()));
        debug_assert!(self.declarations_to_apply_unless_overridden.is_empty());
        for declaration in longhand_declarations {
            let mut longhand_id = declaration.decl.id().as_longhand().unwrap();
            if !properties_to_apply.contains(longhand_id) {
                continue;
            }
            debug_assert!(PrioritaryPropertyId::from_longhand(longhand_id).is_none());
            let is_logical = longhand_id.is_logical();
            if is_logical {
                let wm = context.builder.writing_mode;
                context
                    .rule_cache_conditions
                    .borrow_mut()
                    .set_writing_mode_dependency(wm);
                longhand_id = longhand_id.to_physical(wm);
            }
            self.apply_one_longhand(
                context,
                longhand_id,
                declaration.decl,
                declaration.priority,
                shorthand_cache,
                attribute_tracker,
            );
        }
        if !self.declarations_to_apply_unless_overridden.is_empty() {
            debug_assert!(self.ignore_colors);
            for declaration in std::mem::take(&mut self.declarations_to_apply_unless_overridden) {
                let longhand_id = declaration.id().as_longhand().unwrap();
                debug_assert!(!longhand_id.is_logical());
                if !self.seen.longhands.contains(longhand_id) {
                    unsafe {
                        self.do_apply_declaration(context, longhand_id, &declaration);
                    }
                }
            }
        }

        if !context.builder.effective_zoom_for_inheritance.is_one() {
            self.recompute_zoom_dependent_inherited_lengths(context);
        }
    }

    #[cold]
    fn recompute_zoom_dependent_inherited_lengths(&self, context: &mut computed::Context) {
        debug_assert!(self.seen.longhands.contains(LonghandId::Zoom));
        for prop in LonghandIdSet::zoom_dependent_inherited_properties().iter() {
            if self.seen.longhands.contains(prop) {
                continue;
            }
            let declaration = PropertyDeclaration::css_wide_keyword(prop, CSSWideKeyword::Inherit);
            unsafe {
                self.do_apply_declaration(context, prop, &declaration);
            }
        }
    }

    fn apply_one_longhand(
        &mut self,
        context: &mut computed::Context,
        longhand_id: LonghandId,
        declaration: &PropertyDeclaration,
        priority: CascadePriority,
        cache: &mut ShorthandsWithPropertyReferencesCache,
        attribute_tracker: &mut AttributeTracker,
    ) {
        debug_assert!(!longhand_id.is_logical());
        if self.seen.longhands.contains(longhand_id) {
            return;
        }

        if !(priority.flags() - context.included_cascade_flags).is_empty() {
            return;
        }

        if self.reverted.longhands_set.contains(longhand_id) {
            if let Some(&(reverted_priority, revert_kind)) =
                self.reverted.longhands.get(&longhand_id)
            {
                if !reverted_priority.allows_when_reverted(&priority, revert_kind) {
                    return;
                }
            }
        }

        let mut declaration =
            self.substitute_variables_if_needed(context, cache, declaration, attribute_tracker);

        // When document colors are disabled, do special handling of
        // properties that are marked as ignored in that mode.
        let origin = priority.cascade_level().origin();
        if self.ignore_colors {
            tweak_when_ignoring_colors(
                context,
                longhand_id,
                origin,
                &mut declaration,
                &mut self.declarations_to_apply_unless_overridden,
            );
        }
        let can_skip_apply = match declaration.get_css_wide_keyword() {
            Some(keyword) => {
                if let Some(revert_kind) = keyword.revert_kind() {
                    // We intentionally don't want to insert it into
                    // `self.seen.longhands`, `reverted` takes care of rejecting
                    // other declarations as needed.
                    self.reverted.longhands_set.insert(longhand_id);
                    self.reverted
                        .longhands
                        .insert(longhand_id, (priority, revert_kind));
                    return;
                }

                let inherited = longhand_id.inherited();
                let zoomed = !context.builder.effective_zoom_for_inheritance.is_one()
                    && longhand_id.zoom_dependent();
                match keyword {
                    CSSWideKeyword::Revert
                    | CSSWideKeyword::RevertLayer
                    | CSSWideKeyword::RevertRule => unreachable!(),
                    CSSWideKeyword::Unset => !zoomed || !inherited,
                    CSSWideKeyword::Inherit => inherited && !zoomed,
                    CSSWideKeyword::Initial => !inherited,
                }
            },
            None => false,
        };

        self.seen.longhands.insert(longhand_id);
        if origin.is_author_origin() {
            self.author_specified.insert(longhand_id);
        }

        if !can_skip_apply {
            // Set context.scope to this declaration's cascade level so that
            // tree-scoped properties (anchor-name, position-anchor, anchor-scope)
            // get the correct scope when converted to computed values.
            let old_scope = context.scope;
            let cascade_level = priority.cascade_level();
            context.scope = cascade_level;
            unsafe { self.do_apply_declaration(context, longhand_id, &declaration) }
            context.scope = old_scope;
        }
    }

    #[inline]
    unsafe fn do_apply_declaration(
        &self,
        context: &mut computed::Context,
        longhand_id: LonghandId,
        declaration: &PropertyDeclaration,
    ) {
        debug_assert!(!longhand_id.is_logical());
        unsafe {
            // We could (and used to) use a pattern match here, but that bloats this
            // function to over 100K of compiled code!
            //
            // To improve i-cache behavior, we outline the individual functions and
            // use virtual dispatch instead.
            (CASCADE_PROPERTY[longhand_id as usize])(declaration, context);
        }
    }

    fn compute_visited_style_if_needed<E>(
        &self,
        context: &mut computed::Context,
        element: Option<E>,
        parent_style: Option<&ComputedValues>,
        layout_parent_style: Option<&ComputedValues>,
        try_tactic: &PositionTryFallbacksTryTactic,
        visited_rules: &StrongRuleNode,
        guards: &StylesheetGuards,
    ) where
        E: TElement,
    {
        let is_link = context.builder.pseudo.is_none() && element.unwrap().is_link();

        macro_rules! visited_parent {
            ($parent:expr) => {
                if is_link {
                    $parent
                } else {
                    $parent.map(|p| p.visited_style().unwrap_or(p))
                }
            };
        }

        // We could call apply_declarations directly, but that'd cause
        // another instantiation of this function which is not great.
        let style = cascade_rules(
            context.builder.stylist.unwrap(),
            context.builder.pseudo,
            visited_rules,
            guards,
            visited_parent!(parent_style),
            visited_parent!(layout_parent_style),
            self.first_line_reparenting,
            try_tactic,
            CascadeMode::Visited {
                unvisited_context: &*context,
                unvisited_properties: &self.seen.longhands,
            },
            // Cascade input flags don't matter for the visited style, they are
            // in the main (unvisited) style.
            Default::default(),
            context.included_cascade_flags,
            // The rule cache doesn't care about caching :visited
            // styles, we cache the unvisited style instead. We still do
            // need to set the caching dependencies properly if present
            // though, so the cache conditions need to match.
            None, // rule_cache
            &mut *context.rule_cache_conditions.borrow_mut(),
            element,
            &mut *context.tree_counting_caches.borrow_mut(),
        );
        context.builder.visited_style = Some(style);
    }

    fn finished_applying_properties(&self, builder: &mut StyleBuilder) {
        #[cfg(feature = "gecko")]
        {
            if let Some(bg) = builder.get_background_if_mutated() {
                bg.fill_arrays();
            }

            if let Some(svg) = builder.get_svg_if_mutated() {
                svg.fill_arrays();
            }
        }

        if self
            .author_specified
            .contains_any(LonghandIdSet::border_background_properties())
        {
            builder.add_flags(ComputedValueFlags::HAS_AUTHOR_SPECIFIED_BORDER_BACKGROUND);
        }

        if self.author_specified.contains(LonghandId::Color) {
            builder.add_flags(ComputedValueFlags::HAS_AUTHOR_SPECIFIED_TEXT_COLOR);
        }

        if self.author_specified.contains(LonghandId::TextShadow) {
            builder.add_flags(ComputedValueFlags::HAS_AUTHOR_SPECIFIED_TEXT_SHADOW);
        }

        if self.author_specified.contains(LonghandId::GridAutoFlow) {
            builder.add_flags(ComputedValueFlags::HAS_AUTHOR_SPECIFIED_GRID_AUTO_FLOW);
        }
        #[cfg(feature = "servo")]
        {
            if let Some(font) = builder.get_font_if_mutated() {
                font.compute_font_hash();
            }
        }
    }

    fn try_to_use_cached_reset_properties(
        &self,
        context: &mut computed::Context<'a>,
        cache: Option<&'a RuleCache>,
        guards: &StylesheetGuards,
    ) -> bool {
        let style = match self.first_line_reparenting {
            FirstLineReparenting::Yes { style_to_reparent } => style_to_reparent,
            FirstLineReparenting::No => {
                let Some(cache) = cache else { return false };
                let Some(style) = cache.find(guards, context) else {
                    return false;
                };
                style
            },
        };

        context.builder.copy_reset_from(style);

        // We're using the same reset style as another element, and we'll skip
        // applying the relevant properties. So we need to do the relevant
        // bookkeeping here to keep these bits correct.
        //
        // Note that the border/background properties are non-inherited, so we
        // don't need to do anything else other than just copying the bits over.
        //
        // When using this optimization, we also need to copy whether the old
        // style specified viewport units / used font-relative lengths, this one
        // would as well.  It matches the same rules, so it is the right thing
        // to do anyways, even if it's only used on inherited properties.
        let bits_to_copy = ComputedValueFlags::HAS_AUTHOR_SPECIFIED_BORDER_BACKGROUND
            | ComputedValueFlags::HAS_AUTHOR_SPECIFIED_GRID_AUTO_FLOW
            | ComputedValueFlags::DEPENDS_ON_SELF_FONT_METRICS
            | ComputedValueFlags::DEPENDS_ON_INHERITED_FONT_METRICS
            | ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE
            | ComputedValueFlags::USES_CONTAINER_UNITS
            | ComputedValueFlags::USES_VIEWPORT_UNITS
            | ComputedValueFlags::USES_FONT_OR_WM_RELATIVE_UNITS
            | ComputedValueFlags::DEPENDS_ON_CONTAINER_STYLE_QUERY
            | ComputedValueFlags::USES_SIBLING_COUNT
            | ComputedValueFlags::USES_SIBLING_INDEX;
        context.builder.add_flags(style.flags & bits_to_copy);

        true
    }

    /// The initial font depends on the current lang group so we may need to
    /// recompute it if the language changed.
    #[inline]
    #[cfg(feature = "gecko")]
    fn recompute_initial_font_family_if_needed(&self, builder: &mut StyleBuilder) {
        use crate::gecko_bindings::bindings;
        use crate::values::computed::font::FontFamily;

        let default_font_type = {
            let font = builder.get_font();

            if !font.mFont.family.is_initial {
                return;
            }

            let default_font_type = unsafe {
                bindings::Gecko_nsStyleFont_ComputeFallbackFontTypeForLanguage(
                    builder.device.document(),
                    font.mLanguage.mRawPtr,
                )
            };

            let initial_generic = font.mFont.family.families.single_generic();
            debug_assert!(
                initial_generic.is_some(),
                "Initial font should be just one generic font"
            );
            if initial_generic == Some(default_font_type) {
                return;
            }

            default_font_type
        };

        // NOTE: Leaves is_initial untouched.
        builder.mutate_font().mFont.family.families =
            FontFamily::generic(default_font_type).families.clone();
    }

    /// Prioritize user fonts if needed by pref.
    #[inline]
    #[cfg(feature = "gecko")]
    fn prioritize_user_fonts_if_needed(&self, builder: &mut StyleBuilder) {
        use crate::gecko_bindings::bindings;

        // Check the use_document_fonts setting for content, but for chrome
        // documents they're treated as always enabled.
        if crate::pref!("browser.display.use_document_fonts") != 0
            || builder.device.chrome_rules_enabled_for_document()
        {
            return;
        }

        let default_font_type = {
            let font = builder.get_font();

            if font.mFont.family.is_system_font {
                return;
            }

            if !font.mFont.family.families.needs_user_font_prioritization() {
                return;
            }

            unsafe {
                bindings::Gecko_nsStyleFont_ComputeFallbackFontTypeForLanguage(
                    builder.device.document(),
                    font.mLanguage.mRawPtr,
                )
            }
        };

        let font = builder.mutate_font();
        font.mFont
            .family
            .families
            .prioritize_first_generic_or_prepend(default_font_type);
    }

    /// Some keyword sizes depend on the font family and language.
    fn recompute_keyword_font_size_if_needed(&self, context: &mut computed::Context) {
        use crate::values::computed::ToComputedValue;

        if !self.seen.longhands.contains(LonghandId::XLang)
            && !self.seen.longhands.contains(LonghandId::FontFamily)
        {
            return;
        }

        let new_size = {
            let font = context.builder.get_font();
            let info = font.clone_font_size().keyword_info;
            let new_size = match info.kw {
                specified::FontSizeKeyword::None => return,
                _ => {
                    context.for_non_inherited_property = false;
                    specified::FontSize::Keyword(info).to_computed_value(context)
                },
            };

            #[cfg(feature = "gecko")]
            if font.mScriptUnconstrainedSize == new_size.computed_size {
                return;
            }

            new_size
        };

        context.builder.mutate_font().set_font_size(new_size);
    }

    /// Some properties, plus setting font-size itself, may make us go out of
    /// our minimum font-size range.
    #[cfg(feature = "gecko")]
    fn constrain_font_size_if_needed(&self, builder: &mut StyleBuilder) {
        use crate::gecko_bindings::bindings;
        use crate::values::generics::NonNegative;

        let min_font_size = {
            let font = builder.get_font();
            let min_font_size = unsafe {
                bindings::Gecko_nsStyleFont_ComputeMinSize(&**font, builder.device.document())
            };

            if font.mFont.size.0 >= min_font_size {
                return;
            }

            NonNegative(min_font_size)
        };

        builder.mutate_font().mFont.size = min_font_size;
    }

    /// <svg:text> is not affected by text zoom, and it uses a preshint to disable it. We fix up
    /// the struct when this happens by unzooming its contained font values, which will have been
    /// zoomed in the parent.
    #[cfg(feature = "gecko")]
    fn unzoom_fonts_if_needed(&self, builder: &mut StyleBuilder) {
        debug_assert!(self.seen.longhands.contains(LonghandId::XTextScale));

        let parent_text_scale = builder.get_parent_font().clone__x_text_scale();
        let text_scale = builder.get_font().clone__x_text_scale();
        if parent_text_scale == text_scale {
            return;
        }
        debug_assert_ne!(
            parent_text_scale.text_zoom_enabled(),
            text_scale.text_zoom_enabled(),
            "There's only one value that disables it"
        );
        debug_assert!(
            !text_scale.text_zoom_enabled(),
            "We only ever disable text zoom never enable it"
        );
        let device = builder.device;
        builder.mutate_font().unzoom_fonts(device);
    }

    fn recompute_font_size_for_zoom_change(&self, builder: &mut StyleBuilder) {
        debug_assert!(self.seen.longhands.contains(LonghandId::Zoom));
        // NOTE(emilio): Intentionally not using the effective zoom here, since all the inherited
        // zooms are already applied.
        let old_size = builder.get_font().clone_font_size();
        let new_size = old_size.zoom(builder.effective_zoom_for_inheritance);
        if old_size == new_size {
            return;
        }
        builder.mutate_font().set_font_size(new_size);
    }

    /// Special handling of font-size: math (used for MathML).
    /// https://w3c.github.io/mathml-core/#the-math-script-level-property
    /// TODO: Bug: 1548471: MathML Core also does not specify a script min size
    /// should we unship that feature or standardize it?
    #[cfg(feature = "gecko")]
    fn recompute_math_font_size_if_needed(context: &mut computed::Context) {
        use crate::values::generics::NonNegative;

        // Do not do anything if font-size: math or math-depth is not set.
        if context.builder.get_font().clone_font_size().keyword_info.kw
            != specified::FontSizeKeyword::Math
        {
            return;
        }

        const SCALE_FACTOR_WHEN_INCREMENTING_MATH_DEPTH_BY_ONE: f32 = 0.71;

        // Helper function that calculates the scale factor applied to font-size
        // when math-depth goes from parent_math_depth to computed_math_depth.
        // This function is essentially a modification of the MathML3's formula
        // 0.71^(parent_math_depth - computed_math_depth) so that a scale factor
        // of parent_script_percent_scale_down is applied when math-depth goes
        // from 0 to 1 and parent_script_script_percent_scale_down is applied
        // when math-depth goes from 0 to 2. This is also a straightforward
        // implementation of the specification's algorithm:
        // https://w3c.github.io/mathml-core/#the-math-script-level-property
        fn scale_factor_for_math_depth_change(
            parent_math_depth: i32,
            computed_math_depth: i32,
            parent_script_percent_scale_down: Option<f32>,
            parent_script_script_percent_scale_down: Option<f32>,
        ) -> f32 {
            let mut a = parent_math_depth;
            let mut b = computed_math_depth;
            let c = SCALE_FACTOR_WHEN_INCREMENTING_MATH_DEPTH_BY_ONE;
            let scale_between_0_and_1 = parent_script_percent_scale_down.unwrap_or(c);
            let scale_between_0_and_2 = parent_script_script_percent_scale_down.unwrap_or(c * c);
            let mut s = 1.0;
            let mut invert_scale_factor = false;
            if a == b {
                return s;
            }
            if b < a {
                std::mem::swap(&mut a, &mut b);
                invert_scale_factor = true;
            }
            let mut e = b - a;
            if a <= 0 && b >= 2 {
                s *= scale_between_0_and_2;
                e -= 2;
            } else if a == 1 {
                s *= scale_between_0_and_2 / scale_between_0_and_1;
                e -= 1;
            } else if b == 1 {
                s *= scale_between_0_and_1;
                e -= 1;
            }
            s *= c.powi(e);
            if invert_scale_factor {
                1.0 / s.max(f32::MIN_POSITIVE)
            } else {
                s
            }
        }

        let (new_size, new_unconstrained_size) = {
            use crate::values::specified::font::QueryFontMetricsFlags;

            let builder = &context.builder;
            let font = builder.get_font();
            let parent_font = builder.get_parent_font();

            let delta = font.mMathDepth.saturating_sub(parent_font.mMathDepth);

            if delta == 0 {
                return;
            }

            let mut min = parent_font.mScriptMinSize;
            if font.mXTextScale.text_zoom_enabled() {
                min = builder.device.zoom_text(min);
            }

            // Calculate scale factor following MathML Core's algorithm.
            let scale = {
                // Script scale factors are independent of orientation.
                let font_metrics = context.query_font_metrics(
                    FontBaseSize::InheritedStyle,
                    FontMetricsOrientation::Horizontal,
                    QueryFontMetricsFlags::NEEDS_MATH_SCALES,
                );
                scale_factor_for_math_depth_change(
                    parent_font.mMathDepth as i32,
                    font.mMathDepth as i32,
                    font_metrics.script_percent_scale_down,
                    font_metrics.script_script_percent_scale_down,
                )
            };

            let parent_size = parent_font.mSize.0;
            let parent_unconstrained_size = parent_font.mScriptUnconstrainedSize.0;
            let new_size = parent_size.scale_by(scale);
            let new_unconstrained_size = parent_unconstrained_size.scale_by(scale);

            if scale <= 1. {
                // The parent size can be smaller than scriptminsize, e.g. if it
                // was specified explicitly. Don't scale in this case, but we
                // don't want to set it to scriptminsize either since that will
                // make it larger.
                if parent_size <= min {
                    (parent_size, new_unconstrained_size)
                } else {
                    (min.max(new_size), new_unconstrained_size)
                }
            } else {
                // If the new unconstrained size is larger than the min size,
                // this means we have escaped the grasp of scriptminsize and can
                // revert to using the unconstrained size.
                // However, if the new size is even larger (perhaps due to usage
                // of em units), use that instead.
                (
                    new_size.min(new_unconstrained_size.max(min)),
                    new_unconstrained_size,
                )
            }
        };
        let font = context.builder.mutate_font();
        font.mFont.size = NonNegative(new_size);
        font.mSize = NonNegative(new_size);
        font.mScriptUnconstrainedSize = NonNegative(new_unconstrained_size);
    }

    /// Seeds `context.builder.substitution_functions` with the inherited custom properties and the
    /// registered initial values, before custom-property declarations are cascaded into it.
    fn init_custom_properties(&mut self, context: &mut computed::Context) {
        let is_root_element = context.is_root_element();
        let initial_values = self.stylist.get_custom_property_initial_values();
        let inherited = if is_root_element {
            debug_assert!(context.inherited_custom_properties().is_empty());
            initial_values.inherited.clone()
        } else {
            context.inherited_custom_properties().inherited.clone()
        };
        let properties = ComputedCustomProperties {
            inherited,
            non_inherited: initial_values.non_inherited.clone(),
        };
        context.builder.substitution_functions =
            ComputedSubstitutionFunctions::new(Some(properties), None);
    }

    /// Resolves the custom properties and applies the prioritary properties in a single
    /// cycle-tracked walk: as a custom property depending on `em`/`lh`/the used color-scheme
    /// becomes resolvable, `substitute_all` applies the prioritary property it needs (via
    /// `ensure_prioritary_property`) so it computes against the right value. Any prioritary
    /// property not triggered that way is applied at the end.
    fn apply_custom_and_prioritary_properties(
        &mut self,
        context: &mut computed::Context,
        decls: &Declarations,
        shorthand_cache: &mut ShorthandsWithPropertyReferencesCache,
        attribute_tracker: &mut AttributeTracker,
    ) {
        if self.may_have_custom_property_cycles {
            let stylist = self.stylist;
            let seen = std::mem::take(&mut self.seen.custom);
            let references = std::mem::take(&mut self.references_from_non_custom_properties);
            substitute_all(
                &seen,
                &references,
                stylist,
                context,
                self,
                decls,
                shorthand_cache,
                attribute_tracker,
            );
        }
        // Apply any prioritary property that wasn't applied while resolving custom properties.
        if decls.has_prioritary_properties {
            for id in PrioritaryPropertyId::each() {
                self.ensure_prioritary_property(
                    context,
                    decls,
                    shorthand_cache,
                    attribute_tracker,
                    id,
                );
            }
        }
        self.finish_cascade_custom_properties(context);
    }

    /// Applies a prioritary property and the prioritary properties it depends on.
    fn ensure_prioritary_property(
        &mut self,
        context: &mut computed::Context,
        decls: &Declarations,
        cache: &mut ShorthandsWithPropertyReferencesCache,
        attribute_tracker: &mut AttributeTracker,
        id: PrioritaryPropertyId,
    ) {
        if self.ensured_prioritary.contains(id) {
            return;
        }
        self.ensured_prioritary.insert(id);
        let deps = id.dependencies();
        if !self.ensured_prioritary.contains_all(deps) {
            for dep in deps.iter() {
                self.ensure_prioritary_property(context, decls, cache, attribute_tracker, dep);
            }
        }
        self.apply_one_prioritary_property(context, decls, cache, id, attribute_tracker);
    }

    /// Cascade a given custom property declaration.
    fn cascade_custom_property(
        &mut self,
        context: &mut computed::Context,
        declaration: &'a CustomDeclaration,
        priority: CascadePriority,
    ) {
        let CustomDeclaration {
            ref name,
            ref value,
        } = *declaration;

        if let Some(&(reverted_priority, revert_kind)) = self.reverted.custom.get(name) {
            if !reverted_priority.allows_when_reverted(&priority, revert_kind) {
                return;
            }
        }

        if !(priority.flags() - context.included_cascade_flags).is_empty() {
            return;
        }

        let entry = match self.seen.custom.var.entry(name) {
            Entry::Occupied(..) => return,
            Entry::Vacant(v) => v,
        };

        let registration = self.stylist.get_custom_property_registration(name);
        let initial_values = self.stylist.get_custom_property_initial_values();
        if !Self::value_may_affect_style(context, name, registration, initial_values, value) {
            entry.insert(false);
            return;
        }

        let has_references = match value {
            CustomDeclarationValue::Unparsed(unparsed_value) => {
                // Non-custom dependency is really relevant for registered custom properties
                // that require computed value of such dependencies.
                unparsed_value
                    .references
                    .flags
                    .intersects(ReferenceFlags::ATTR | ReferenceFlags::VAR)
                    || !find_non_custom_references(
                        registration,
                        unparsed_value,
                        context.is_root_element(),
                    )
                    .is_empty()
            },
            // XXX This matches the previous behavior but I don't think it's fully sound. Parsed
            // values may have non-custom references, can't they? But also how do we get a parsed
            // value at all here?
            CustomDeclarationValue::Parsed(..) => false,
            // If we're a wide keyword we either get an initial value or an inherited one (no
            // references). Revert might have references, but we remove the `seen` entry, so it
            // doesn't matter.
            CustomDeclarationValue::CSSWideKeyword(..) => false,
        };
        self.may_have_custom_property_cycles |= has_references;
        entry.insert(has_references);

        match value {
            CustomDeclarationValue::Unparsed(unparsed_value) => {
                if !has_references {
                    // If the variable value has no references to other properties, perform
                    // substitution here instead of forcing a full traversal in `substitute_all`
                    // afterwards.
                    substitute_references_if_needed_and_apply(
                        name,
                        SubstitutionFunctionKind::Var,
                        unparsed_value,
                        self.stylist,
                        context,
                        // We just checked there are no attr dependencies.
                        &mut AttributeTracker::new_dummy(),
                    );
                    return;
                }
                let value = ComputedRegisteredValue::universal(Arc::clone(unparsed_value));
                context
                    .builder
                    .substitution_functions
                    .insert_var(registration, name, value);
            },
            CustomDeclarationValue::Parsed(parsed_value) => {
                let value = parsed_value.to_computed_value(context);
                context
                    .builder
                    .substitution_functions
                    .insert_var(registration, name, value);
            },
            CustomDeclarationValue::CSSWideKeyword(keyword) => match keyword.revert_kind() {
                Some(revert_kind) => {
                    self.seen.custom.var.remove(name);
                    self.reverted
                        .custom
                        .insert(name.clone(), (priority, revert_kind));
                },
                None => match keyword {
                    CSSWideKeyword::Initial => {
                        // For non-inherited custom properties, 'initial' was handled in value_may_affect_style.
                        debug_assert!(registration.inherits(), "Should've been handled earlier");
                        remove_and_insert_initial_value(
                            name,
                            registration,
                            &mut context.builder.substitution_functions,
                        );
                    },
                    CSSWideKeyword::Inherit => {
                        // For inherited custom properties, 'inherit' was handled in value_may_affect_style.
                        debug_assert!(!registration.inherits(), "Should've been handled earlier");
                        context
                            .style()
                            .add_flags(ComputedValueFlags::INHERITS_RESET_STYLE);
                        let inherited_value = context
                            .inherited_custom_properties()
                            .non_inherited
                            .get(name)
                            .cloned();
                        if let Some(inherited_value) = inherited_value {
                            context.builder.substitution_functions.insert_var(
                                registration,
                                name,
                                inherited_value,
                            );
                        }
                    },
                    // handled in value_may_affect_style or in the revert_kind branch above.
                    CSSWideKeyword::Revert
                    | CSSWideKeyword::RevertLayer
                    | CSSWideKeyword::RevertRule
                    | CSSWideKeyword::Unset => unreachable!(),
                },
            },
        }
    }

    /// Fast check to avoid calling maybe_note_non_custom_dependency in ~all cases.
    #[inline]
    pub fn might_have_non_custom_or_attr_dependency(
        id: LonghandId,
        decl: &PropertyDeclaration,
    ) -> bool {
        if let PropertyDeclaration::WithVariables(v) = decl {
            return matches!(id, LonghandId::LineHeight | LonghandId::FontSize)
                || v.value
                    .variable_value
                    .references
                    .flags
                    .intersects(ReferenceFlags::ATTR);
        }
        false
    }

    /// Note a non-custom property with variable reference that may in turn depend on that property.
    /// e.g. `font-size` depending on a custom property that may be a registered property using `em`.
    pub fn maybe_note_non_custom_dependency(
        &mut self,
        context: &mut computed::Context,
        id: LonghandId,
        decl: &'a PropertyDeclaration,
        attribute_tracker: &mut AttributeTracker,
    ) {
        debug_assert!(Self::might_have_non_custom_or_attr_dependency(id, decl));
        let PropertyDeclaration::WithVariables(v) = decl else {
            return;
        };
        let value = &v.value.variable_value;
        let refs = &value.references;

        if !refs
            .flags
            .intersects(ReferenceFlags::VAR | ReferenceFlags::ATTR)
        {
            return;
        }

        // Attributes in non-custom properties may reference `var()` or `attr()` in their
        // values, which we need to track to support chained references and detect cycles.
        if refs.flags.intersects(ReferenceFlags::ATTR) {
            self.update_attributes_map(context, value, attribute_tracker);
            if !refs.flags.intersects(ReferenceFlags::VAR) {
                return;
            }
        }

        // The non-custom node(s) this property feeds. We don't try to figure out here which
        // referenced custom properties can actually cycle back to it: instead we record the whole
        // declaration value, and let `substitute_all` traverse its references (following fallbacks
        // only when the primary is invalid). This way, references that only appear in an unused
        // fallback (e.g. `var(--exists, var(--font-size-em))`) don't create a spurious cycle, while
        // references in a used fallback (e.g. `var(--noexist, var(--font-size-em))`) do.
        //
        // With unit algebra in `calc()`, references aren't limited to `font-size`. For example,
        // `--foo: 100ex; font-weight: calc(var(--foo) / 1ex);`, or
        // `--foo: 1em; zoom: calc(var(--foo) * 30px / 2em);`
        let references = match id {
            LonghandId::FontSize => ReferenceFlags::FONT_UNITS,
            LonghandId::LineHeight => ReferenceFlags::LH_UNITS | ReferenceFlags::FONT_UNITS,
            LonghandId::ColorScheme => ReferenceFlags::COLOR_SCHEME,
            _ => return,
        };

        references.for_each_non_custom(context.is_root_element(), |idx| {
            self.references_from_non_custom_properties[idx]
                .get_or_insert_with(Vec::new)
                .push(v.value.clone());
        });
    }

    fn value_may_affect_style(
        context: &computed::Context,
        name: &Name,
        registration: &PropertyDescriptors,
        initial_values: &ComputedCustomProperties,
        value: &CustomDeclarationValue,
    ) -> bool {
        match *value {
            CustomDeclarationValue::CSSWideKeyword(CSSWideKeyword::Inherit) => {
                // For inherited custom properties, explicit 'inherit' means we
                // can just use any existing value in the inherited
                // CustomPropertiesMap.
                if registration.inherits() {
                    return false;
                }
            },
            CustomDeclarationValue::CSSWideKeyword(CSSWideKeyword::Initial) => {
                // For non-inherited custom properties, explicit 'initial' means
                // we can just use any initial value in the registration.
                if !registration.inherits() {
                    return false;
                }
            },
            CustomDeclarationValue::CSSWideKeyword(CSSWideKeyword::Unset) => {
                // Explicit 'unset' means we can either just use any existing
                // value in the inherited CustomPropertiesMap or the initial
                // value in the registration.
                return false;
            },
            _ => {},
        }

        let existing_value = context
            .builder
            .substitution_functions
            .get_var(registration, name);
        let Some(existing_value) = existing_value else {
            if matches!(
                value,
                CustomDeclarationValue::CSSWideKeyword(CSSWideKeyword::Initial)
            ) {
                debug_assert!(registration.inherits(), "Should've been handled earlier");
                // The initial value of a custom property without a
                // guaranteed-invalid initial value is the same as it
                // not existing in the map.
                if registration.initial_value.is_none() {
                    return false;
                }
            }
            return true;
        };
        match value {
            CustomDeclarationValue::Unparsed(value) => {
                // Don't bother overwriting an existing value with the same
                // specified value.
                if let Some(existing_value) = existing_value.as_universal() {
                    return existing_value != value;
                }
            },
            CustomDeclarationValue::Parsed(..) => {
                // If the value has dependencies, context might not yield the same
                // result as the eventual value.
            },
            CustomDeclarationValue::CSSWideKeyword(kw) => {
                match kw {
                    CSSWideKeyword::Inherit => {
                        debug_assert!(!registration.inherits(), "Should've been handled earlier");
                        // existing_value is the registered initial value.
                        // Don't bother adding it to self.custom_properties.non_inherited
                        // if the key is also absent from self.inherited.non_inherited.
                        if context
                            .inherited_custom_properties()
                            .non_inherited
                            .get(name)
                            .is_none()
                        {
                            return false;
                        }
                    },
                    CSSWideKeyword::Initial => {
                        debug_assert!(registration.inherits(), "Should've been handled earlier");
                        // Don't bother overwriting an existing value with the initial value
                        // specified in the registration.
                        if let Some(initial_value) = initial_values.get(registration, name) {
                            return existing_value != initial_value;
                        }
                    },
                    CSSWideKeyword::Unset => {
                        debug_assert!(false, "Should've been handled earlier");
                    },
                    CSSWideKeyword::Revert
                    | CSSWideKeyword::RevertLayer
                    | CSSWideKeyword::RevertRule => {},
                }
            },
        };

        true
    }

    /// For a given unparsed variable, update the attributes map with its attr references.
    pub fn update_attributes_map(
        &mut self,
        context: &mut computed::Context,
        value: &'a VariableValue,
        attribute_tracker: &mut AttributeTracker,
    ) {
        let refs = &value.references;
        if !refs.flags.intersects(ReferenceFlags::ATTR) {
            return;
        }
        self.may_have_custom_property_cycles = true;

        for next in &refs.refs {
            if !next.is_attr_with_type() || !self.seen.custom.attr.insert(&next.name) {
                // Only type() can have nested references, so we don't need to eagerly look at
                // others.
                continue;
            }
            if let Ok(v) = get_attr_value_for_cycle_resolution(
                &next.name,
                &next.attribute_data,
                &value.url_data,
                attribute_tracker,
            ) {
                context
                    .builder
                    .substitution_functions
                    .insert_attr(&next.name, v);
            }
        }
    }

    /// Computes the map of applicable custom properties, saving the result into the computed
    /// context, and applies the prioritary properties interleaved with custom-property resolution.
    pub fn finish_cascade_custom_properties(&mut self, context: &mut computed::Context) {
        context
            .builder
            .substitution_functions
            .custom_properties
            .shrink_to_fit();

        // Some pages apply a lot of redundant custom properties, see e.g.
        // bug 1758974 comment 5. Try to detect the case where the values
        // haven't really changed, and save some memory by reusing the inherited
        // map in that case.
        let initial_values = self.stylist.get_custom_property_initial_values();
        let reuse_inherited = context.inherited_custom_properties().inherited
            == context
                .builder
                .substitution_functions
                .custom_properties
                .inherited;
        if reuse_inherited {
            let inherited = context.inherited_custom_properties().inherited.clone();
            context
                .builder
                .substitution_functions
                .custom_properties
                .inherited = inherited;
        }
        if initial_values.non_inherited
            == context
                .builder
                .substitution_functions
                .custom_properties
                .non_inherited
        {
            let non_inherited = initial_values.non_inherited.clone();
            context
                .builder
                .substitution_functions
                .custom_properties
                .non_inherited = non_inherited;
        }
    }
}

fn substitute_all(
    seen: &SeenSubstitutionFunctions,
    references_from_non_custom_properties: &NonCustomReferenceMap<Vec<Arc<UnparsedValue>>>,
    stylist: &Stylist,
    computed_context: &mut computed::Context,
    cascade: &mut Cascade,
    decls: &Declarations,
    shorthand_cache: &mut ShorthandsWithPropertyReferencesCache,
    attr_tracker: &mut AttributeTracker,
) {
    // The cycle dependencies removal in this function is a variant
    // of Tarjan's algorithm. It is mostly based on the pseudo-code
    // listed in
    // https://en.wikipedia.org/w/index.php?
    // title=Tarjan%27s_strongly_connected_components_algorithm&oldid=801728495

    #[derive(Clone, Eq, PartialEq, Debug)]
    enum VarType {
        Attr(Name),
        Custom(Name),
        NonCustom(SingleNonCustomReference),
    }

    /// Struct recording necessary information for each variable.
    #[derive(Debug)]
    struct VarInfo {
        /// The name of the variable. It will be taken when the corresponding variable is popped
        /// from the stack, which serves as a mark for whether the variable is currently in the
        /// stack below.
        var: Option<VarType>,
        /// If the variable is in a dependency cycle, lowlink represents a smaller index which
        /// corresponds to a variable in the same strong connected component, which is known to be
        /// accessible from this variable. It is not necessarily the root, though.
        lowlink: usize,
    }

    #[derive(Debug, Default)]
    struct OrderIndexMap {
        /// The map from the custom property name to its order index.
        var: PrecomputedHashMap<Name, usize>,
        /// The map from the attribute name to its order index.
        attr: PrecomputedHashMap<Name, usize>,
    }

    impl OrderIndexMap {
        fn clear(&mut self) {
            self.var.clear();
            self.attr.clear();
        }
    }

    /// Context struct for traversing the variable graph, so that we can
    /// avoid referencing all the fields multiple times.
    struct Context<'a, 'b: 'a, 'c, 'd> {
        /// Number of variables visited. This is used as the order index
        /// when we visit a new unresolved variable.
        count: usize,
        /// The map from a substitution function name to its order index.
        index_map: OrderIndexMap,
        /// Mapping from a non-custom dependency to its order index.
        non_custom_index_map: NonCustomReferenceMap<usize>,
        /// Information of each variable indexed by the order index.
        var_info: SmallVec<[VarInfo; 5]>,
        /// The stack of order index of visited variables. It contains
        /// all unfinished strong connected components.
        stack: SmallVec<[usize; 5]>,
        /// The stylist is used to get registered properties, and to resolve the environment to
        /// substitute `env()` variables.
        stylist: &'a Stylist,
        /// The computed context is used to get inherited custom properties, compute registered
        /// custom properties, and apply prioritary properties.
        computed_context: &'a mut computed::Context<'b>,
        /// The cascade owns prioritary-property application; when a `NonCustom` node (or a
        /// color-scheme-dependent custom property) becomes resolvable, we apply the corresponding
        /// prioritary property directly through it.
        cascade: &'a mut Cascade<'c>,
        /// The declarations, needed to apply prioritary properties.
        decls: &'a Declarations<'d>,
        /// Shorthand cache, needed to apply prioritary properties.
        cache: &'a mut ShorthandsWithPropertyReferencesCache,
    }

    impl<'a, 'b: 'a, 'c, 'd> Context<'a, 'b, 'c, 'd> {
        fn reset(&mut self) {
            self.count = 0;
            self.index_map.clear();
            self.non_custom_index_map = Default::default();
            self.var_info.clear();
            self.stack.clear();
        }

        fn map(&self) -> &ComputedSubstitutionFunctions {
            &self.computed_context.builder.substitution_functions
        }

        fn map_mut(&mut self) -> &mut ComputedSubstitutionFunctions {
            &mut self.computed_context.builder.substitution_functions
        }

        /// Marks a given `name` as being in a loop.
        fn handle_loop(&mut self, name: &VarType) {
            match name {
                VarType::Attr(name) => {
                    self.computed_context
                        .builder
                        .substitution_functions
                        .remove_attr(name);
                },
                VarType::Custom(name) => {
                    // This variable is in a loop. Resolve to invalid.
                    handle_invalid_at_computed_value_time(
                        name,
                        self.stylist.get_custom_property_registration(name),
                        self.computed_context,
                    );
                },
                VarType::NonCustom(non_custom) => {
                    self.computed_context
                        .builder
                        .invalid_non_custom_properties
                        .insert(non_custom.to_prioritary_id().to_longhand());
                },
            }
        }

        /// Applies a prioritary property (and its dependencies) while resolving custom properties.
        ///
        /// The in-progress map lives outside `computed_context` during traversal; we move it into
        /// `computed_context.builder.substitution_functions` so the prioritary declaration's `var()`
        /// references resolve against the custom properties resolved so far, then take it back out.
        fn apply_prioritary_property(
            &mut self,
            id: PrioritaryPropertyId,
            attr_tracker: &mut AttributeTracker,
        ) {
            self.cascade.ensure_prioritary_property(
                self.computed_context,
                self.decls,
                self.cache,
                attr_tracker,
                id,
            );
        }
    }

    /// Traverse the references in `root` (the value of a custom property or of a non-custom
    /// property feeding a `NonCustom` node), creating graph edges to the referenced substitution
    /// functions and updating `lowlink`/`self_ref` accordingly.
    ///
    /// We follow var()/attr()/env() fallbacks only when the primary substitution function is
    /// guaranteed-invalid (i.e. when the fallback is actually used). This matches the substitution
    /// order and the resolution of https://github.com/w3c/csswg-drafts/issues/11500: cycles (or
    /// dependencies) that only exist through an unused fallback don't count.
    ///
    /// We need to bubble up non custom references from unregistered properties.
    fn visit_value_references<'a, 'b, 'c, 'd>(
        var: &VarType,
        root: &References,
        url_data: &UrlExtraData,
        index: usize,
        references_from_non_custom_properties: &NonCustomReferenceMap<Vec<Arc<UnparsedValue>>>,
        context: &mut Context<'a, 'b, 'c, 'd>,
        lowlink: &mut usize,
        self_ref: &mut bool,
        attribute_tracker: &mut AttributeTracker,
        non_custom_references: &mut ReferenceFlags,
    ) {
        // FIXME: Maybe avoid visiting the same var twice if not needed?
        let mut refs_stack = SmallVec::<[&References; 5]>::new();
        refs_stack.push(root);
        while let Some(refs) = refs_stack.pop() {
            *non_custom_references |= refs.flags;
            for next in &refs.refs {
                if next.substitution_kind == SubstitutionFunctionKind::Env {
                    // env() doesn't reference custom properties, so it never participates in
                    // cycles; its fallback is used only when the environment doesn't provide
                    // the variable.
                    let device = context.stylist.device();
                    let present = device
                        .environment()
                        .get(&next.name, device, url_data)
                        .is_some();
                    if !present {
                        if let Some(ref fallback) = next.fallback {
                            refs_stack.push(&fallback.references);
                        }
                    }
                    continue;
                }

                let next_var = if next.substitution_kind == SubstitutionFunctionKind::Attr {
                    // An type()-less attr() within attr(... type()) can still have nested
                    // references.
                    let can_chain = next.is_attr_with_type() || matches!(var, VarType::Attr(..));
                    if !can_chain {
                        continue;
                    }
                    if context.map().get_attr(&next.name).is_none() {
                        if let Ok(val) = get_attr_value_for_cycle_resolution(
                            &next.name,
                            &next.attribute_data,
                            url_data,
                            attribute_tracker,
                        ) {
                            context.map_mut().insert_attr(&next.name, val);
                        }
                    }
                    VarType::Attr(next.name.clone())
                } else {
                    VarType::Custom(next.name.clone())
                };

                visit_link(
                    next_var,
                    index,
                    references_from_non_custom_properties,
                    context,
                    lowlink,
                    self_ref,
                    attribute_tracker,
                );

                // Now that the primary reference has been resolved, classify it to
                // decide whether its fallback is used.
                let kind = next.substitution_kind;
                let resolved = match kind {
                    SubstitutionFunctionKind::Var => {
                        let registration =
                            context.stylist.get_custom_property_registration(&next.name);
                        context.map().get_var(registration, &next.name)
                    },
                    SubstitutionFunctionKind::Attr => context.map().get_attr(&next.name),
                    SubstitutionFunctionKind::Env => unreachable!("Handled above"),
                };
                // The primary is guaranteed-invalid if it's absent from the map, or still
                // present but unresolved (i.e. part of a cycle currently being resolved).
                let mut primary_valid = false;
                if let Some(resolved) = resolved {
                    if let Some(v) = resolved.as_universal() {
                        primary_valid = !v.has_references();
                        *non_custom_references |= v.references.flags;
                    } else {
                        // Resolved and computed value, no other custom references left.
                        primary_valid = true;
                    }
                }

                if !primary_valid {
                    if let Some(ref fallback) = next.fallback {
                        refs_stack.push(&fallback.references);
                    }
                }
            }
        }
    }

    /// Traverse a single dependency `var` of the variable at order index `index`, updating its
    /// `lowlink`/`self_ref` from the result.
    fn visit_link<'a, 'b, 'c, 'd>(
        var: VarType,
        index: usize,
        non_custom_references: &NonCustomReferenceMap<Vec<Arc<UnparsedValue>>>,
        context: &mut Context<'a, 'b, 'c, 'd>,
        lowlink: &mut usize,
        self_ref: &mut bool,
        attr_tracker: &mut AttributeTracker,
    ) {
        let next_index = match traverse(var, non_custom_references, context, attr_tracker) {
            Some(index) => index,
            // There is nothing to do if the next variable has been
            // fully resolved at this point.
            None => return,
        };
        let next_info = &context.var_info[next_index];
        if next_index > index {
            // The next variable has a larger index than us, so it
            // must be inserted in the recursive call above. We want
            // to get its lowlink.
            *lowlink = cmp::min(*lowlink, next_info.lowlink);
        } else if next_index == index {
            *self_ref = true;
        } else if next_info.var.is_some() {
            // The next variable has a smaller order index and it is
            // in the stack, so we are at the same component.
            *lowlink = cmp::min(*lowlink, next_index);
        }
    }

    /// This function combines the traversal for cycle removal and value
    /// substitution. It returns either a signal None if this variable
    /// has been fully resolved (to either having no reference or being
    /// marked invalid), or the order index for the given name.
    ///
    /// When it returns, the variable corresponds to the name would be
    /// in one of the following states:
    /// * It is still in context.stack, which means it is part of an
    ///   potentially incomplete dependency circle.
    /// * It has been removed from the map.  It can be either that the
    ///   substitution failed, or it is inside a dependency circle.
    ///   When this function removes a variable from the map because
    ///   of dependency circle, it would put all variables in the same
    ///   strong connected component to the set together.
    /// * It doesn't have any reference, because either this variable
    ///   doesn't have reference at all in specified value, or it has
    ///   been completely resolved.
    /// * There is no such variable at all.
    fn traverse<'a, 'b, 'c, 'd>(
        var: VarType,
        references_from_non_custom_properties: &NonCustomReferenceMap<Vec<Arc<UnparsedValue>>>,
        context: &mut Context<'a, 'b, 'c, 'd>,
        attribute_tracker: &mut AttributeTracker,
    ) -> Option<usize> {
        // Some shortcut checks.
        // The non-custom (font/line-height/color-scheme) dependencies of this value, carried out
        // of the match so we can create the corresponding nodes once we've pushed this variable.
        let mut value_non_custom_refs = ReferenceFlags::empty();
        let mut registered = false;
        let value = match var {
            VarType::Custom(ref name) | VarType::Attr(ref name) => {
                let map = &context.computed_context.builder.substitution_functions;
                let (registration, value, kind) = if matches!(var, VarType::Custom(..)) {
                    let registration = context.stylist.get_custom_property_registration(name);
                    (
                        registration,
                        map.get_var(registration, name)?.as_universal()?,
                        SubstitutionFunctionKind::Var,
                    )
                } else {
                    // `attr()` is always treated as unregistered.
                    (
                        PropertyDescriptors::unregistered(),
                        map.get_attr(name)?.as_universal()?,
                        SubstitutionFunctionKind::Attr,
                    )
                };
                let is_root = context.computed_context.is_root_element();
                value_non_custom_refs = find_non_custom_references(registration, value, is_root);
                registered = !registration.is_universal();
                let has_dependency = value
                    .references
                    .flags
                    .intersects(ReferenceFlags::ATTR | ReferenceFlags::VAR)
                    || !value_non_custom_refs.is_empty();
                // Nothing to resolve.
                if !has_dependency {
                    debug_assert!(
                        !value.references.flags.intersects(ReferenceFlags::ENV),
                        "Should've been handled earlier"
                    );
                    if kind == SubstitutionFunctionKind::Attr || registered {
                        // We might still need to compute the value if this is not an universal
                        // registration but we thought it had a dependency during the cascade and it
                        // turned out not to. Note that if this was already computed we would've
                        // bailed out in the as_universal() check.
                        let value = value.clone();
                        substitute_references_if_needed_and_apply(
                            name,
                            kind,
                            &value,
                            context.stylist,
                            context.computed_context,
                            attribute_tracker,
                        );
                    }
                    return None;
                }

                // Has this variable been visited?
                let index_map = if kind == SubstitutionFunctionKind::Var {
                    &mut context.index_map.var
                } else {
                    &mut context.index_map.attr
                };
                match index_map.entry(name.clone()) {
                    Entry::Occupied(entry) => {
                        return Some(*entry.get());
                    },
                    Entry::Vacant(entry) => {
                        entry.insert(context.count);
                    },
                }
                // Hold a strong reference to the value so that we don't
                // need to keep reference to context.map.
                Some(value.clone())
            },
            VarType::NonCustom(ref non_custom) => {
                let entry = &mut context.non_custom_index_map[*non_custom];
                if let Some(v) = entry {
                    return Some(*v);
                }
                *entry = Some(context.count);
                None
            },
        };

        // Add new entry to the information table.
        let index = context.count;
        context.count += 1;
        debug_assert_eq!(index, context.var_info.len());
        context.var_info.push(VarInfo {
            var: Some(var.clone()),
            lowlink: index,
        });
        context.stack.push(index);

        let mut self_ref = false;
        let mut lowlink = index;
        if let Some(v) = value.as_ref() {
            debug_assert!(
                matches!(var, VarType::Custom(_) | VarType::Attr(_)),
                "Non-custom property has references?"
            );

            // Visit the references in this value...
            visit_value_references(
                &var,
                &v.references,
                &v.url_data,
                index,
                references_from_non_custom_properties,
                context,
                &mut lowlink,
                &mut self_ref,
                attribute_tracker,
                &mut value_non_custom_refs,
            );

            // ... Then the non-custom properties this value depends on (font-size, line-height,
            // color-scheme), as computed by `find_non_custom_references` above.
            let is_root = context.computed_context.is_root_element();
            if registered && !value_non_custom_refs.is_empty() {
                value_non_custom_refs.for_each_non_custom(is_root, |r| {
                    visit_link(
                        VarType::NonCustom(r),
                        index,
                        references_from_non_custom_properties,
                        context,
                        &mut lowlink,
                        &mut self_ref,
                        attribute_tracker,
                    );
                });
            }
        } else if let VarType::NonCustom(non_custom) = var {
            // line-height's resolution depends on font-size's, so its node has a graph edge to the
            // font-size node. This is needed for cycle detection (not just application order, which
            // `prioritary_property_dependencies(LineHeight)` handles): it puts the line-height node
            // in the same strongly-connected component as a font-size<->custom cycle, so the node
            // doesn't apply line-height (and, transitively, font-size) before that cycle is
            // removed.
            //
            // TODO(emilio): I think the right fix for this is
            // s/SingleNonCustomReference/PrioritaryPropertyId in VarType::NonCustom.
            if non_custom == SingleNonCustomReference::LhUnits {
                visit_link(
                    VarType::NonCustom(SingleNonCustomReference::FontUnits),
                    index,
                    references_from_non_custom_properties,
                    context,
                    &mut lowlink,
                    &mut self_ref,
                    attribute_tracker,
                );
            }
            let entry = &references_from_non_custom_properties[non_custom];
            if let Some(values) = entry.as_ref() {
                for value in values {
                    // Traverse the non-custom property's declaration value(s), creating edges to
                    // the custom properties they reference (directly or through a used fallback)
                    // that might cycle back to this node. Note we traverse unregistered/universal
                    // references too: while their own font-relative units are textual (resolved
                    // against the parent, see `find_non_custom_references`), they may chain to a
                    // registered property that does cycle back.
                    let value = &value.variable_value;
                    visit_value_references(
                        &var,
                        &value.references,
                        &value.url_data,
                        index,
                        references_from_non_custom_properties,
                        context,
                        &mut lowlink,
                        &mut self_ref,
                        attribute_tracker,
                        &mut Default::default(),
                    );
                }
            }
        }

        context.var_info[index].lowlink = lowlink;
        if lowlink != index {
            // This variable is in a loop, but it is not the root of this strong connected
            // component. We simply return for now, and the root would remove it from the map.
            //
            // This cannot be removed from the map here, because otherwise the shortcut check at the
            // beginning of this function would return the wrong value.
            return Some(index);
        }

        // This is the root of a strong-connected component.
        let mut in_loop = self_ref;
        loop {
            let var_index = context
                .stack
                .pop()
                .expect("The current variable should still be in stack");
            let var_info = &mut context.var_info[var_index];
            // We should never visit the variable again, so it's safe to take the name away.
            let var_name = var_info
                .var
                .take()
                .expect("Variable should not be popped from stack twice");
            if var_index != index {
                // Anything here is in a loop which can traverse to the node we are handling, so
                // it's invalid at computed-value time.
                in_loop = true;
            }
            if in_loop {
                context.handle_loop(&var_name);
            }
            if var_index == index {
                debug_assert_eq!(var_name, var);
                break;
            }
        }

        if in_loop {
            return None;
        }

        // Not in a loop, apply the value.
        match var {
            VarType::Custom(ref name) | VarType::Attr(ref name) => {
                if let Some(ref v) = value {
                    let kind = if matches!(var, VarType::Custom(..)) {
                        SubstitutionFunctionKind::Var
                    } else {
                        SubstitutionFunctionKind::Attr
                    };
                    // We know we're not in a loop now, perform substitution.
                    // TODO(emilio): Merge with the cycle detection loop instead?
                    substitute_references_if_needed_and_apply(
                        name,
                        kind,
                        v,
                        context.stylist,
                        context.computed_context,
                        attribute_tracker,
                    );
                }
            },
            VarType::NonCustom(non_custom) => {
                context.apply_prioritary_property(non_custom.to_prioritary_id(), attribute_tracker);
            },
        }
        // All resolved, so return the signal value.
        None
    }

    let mut context = Context {
        count: 0,
        index_map: OrderIndexMap::default(),
        non_custom_index_map: NonCustomReferenceMap::default(),
        stack: SmallVec::new(),
        var_info: SmallVec::new(),
        stylist,
        computed_context: &mut *computed_context,
        cascade: &mut *cascade,
        decls,
        cache: &mut *shorthand_cache,
    };
    let mut first = true;
    let mut run_one = |var: VarType| {
        if !first {
            context.reset();
        }
        first = false;
        traverse(
            var,
            references_from_non_custom_properties,
            &mut context,
            attr_tracker,
        );
    };
    // Note that `seen` doesn't contain names inherited from our parent, but
    // those can't have variable references (since we inherit the computed
    // variables) so we don't want to spend cycles traversing them anyway.
    for (var, has_refs) in &seen.var {
        if !has_refs {
            continue;
        }
        run_one(VarType::Custom((*var).clone()));
    }
    // Traverse potentially untraversed chained references from `attr(type())` in non-custom
    // properties.
    for attr in &seen.attr {
        run_one(VarType::Attr((*attr).clone()));
    }
}
