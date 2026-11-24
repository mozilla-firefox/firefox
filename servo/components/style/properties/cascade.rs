/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The main cascading algorithm of the style system.

use crate::applicable_declarations::{CascadePriority, RevertKind};
use crate::color::AbsoluteColor;
use crate::computed_value_flags::ComputedValueFlags;
use crate::context::TreeCountingCaches;
use crate::custom_properties::{
    CustomPropertiesBuilder, DeferFontRelativeCustomPropertyResolution,
};
use crate::dom::{AttributeTracker, DummyElementContext, ElementContext, TElement};
#[cfg(feature = "gecko")]
use crate::font_metrics::FontMetricsOrientation;
use crate::logical_geometry::WritingMode;
use crate::properties::{
    property_counts, CSSWideKeyword, ComputedValues, DeclarationImportanceIterator, LonghandId,
    LonghandIdSet, PrioritaryPropertyId, PropertyDeclaration, PropertyDeclarationId, PropertyFlags,
    ShorthandsWithPropertyReferencesCache, StyleBuilder, CASCADE_PROPERTY,
};
use crate::rule_cache::{RuleCache, RuleCacheConditions};
use crate::rule_tree::{CascadeLevel, CascadeOrigin, RuleCascadeFlags, StrongRuleNode};
use crate::selector_parser::PseudoElement;
use crate::shared_lock::StylesheetGuards;
use crate::style_adjuster::StyleAdjuster;
use crate::stylesheets::container_rule::ContainerSizeQuery;
use crate::stylesheets::layer_rule::LayerOrder;
use crate::stylist::Stylist;
#[cfg(feature = "gecko")]
use crate::values::specified::length::FontBaseSize;
use crate::values::specified::position::PositionTryFallbacksTryTactic;
use crate::values::{computed, specified};
use rustc_hash::FxHashMap;
use selectors::matching::ElementSelectorFlags;
use servo_arc::Arc;
use smallvec::SmallVec;
use std::borrow::Cow;

/// Whether we're resolving a style with the purposes of re-parenting for ::first-line or
/// @position-try.
#[derive(Copy, Clone)]
#[allow(missing_docs)]
pub enum CascadeReparenting<'a> {
    No,
    FirstLine {
        /// The style we're re-parenting for ::first-line. ::first-line only affects inherited
        /// properties so we use this to avoid some work and also ensure correctness by copying the
        /// reset structs from this style.
        base_style: &'a ComputedValues,
    },
    PositionTry {
        /// The base style for our @position-try declarations.
        base_style: &'a ComputedValues,
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
    cascade_reparenting: CascadeReparenting,
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
        cascade_reparenting,
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
        let guard = self.priority.cascade_level().origin().guard(&self.guards);
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
    cascade_reparenting: CascadeReparenting,
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
        cascade_reparenting,
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
    },
}

fn iter_declarations<'custom_builder, 'decls: 'custom_builder, 'builder>(
    iter: impl Iterator<Item = (&'decls PropertyDeclaration, CascadePriority)>,
    declarations: &mut Declarations<'decls>,
    mut custom_builder: Option<&mut CustomPropertiesBuilder<'custom_builder, 'builder>>,
    attribute_tracker: &mut AttributeTracker,
) {
    for (declaration, priority) in iter {
        if let PropertyDeclaration::Custom(ref declaration) = *declaration {
            if let Some(ref mut builder) = custom_builder {
                builder.cascade(declaration, priority, attribute_tracker);
            }
        } else {
            let id = declaration.id().as_longhand().unwrap();
            declarations.note_declaration(declaration, priority, id);
            if CustomPropertiesBuilder::might_have_non_custom_or_attr_dependency(id, declaration) {
                if let Some(ref mut builder) = custom_builder {
                    builder.maybe_note_non_custom_dependency(id, declaration, attribute_tracker);
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
    cascade_reparenting: CascadeReparenting<'_>,
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
    let is_root_element = pseudo.is_none() && element.map_or(false, |e| e.is_root());
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

    let using_cached_properties;
    let ignore_colors = context.builder.device.forced_colors().is_active();
    let mut cascade = Cascade::new(cascade_reparenting, try_tactic, ignore_colors);
    let mut declarations = Default::default();
    let mut shorthand_cache = ShorthandsWithPropertyReferencesCache::default();
    let mut attribute_tracker = AttributeTracker::new(element_context);

    let properties_to_apply = match cascade_mode {
        CascadeMode::Visited { unvisited_context } => {
            context.builder.substitution_functions =
                unvisited_context.builder.substitution_functions.clone();
            context.builder.writing_mode = unvisited_context.builder.writing_mode;
            context.builder.color_scheme = unvisited_context.builder.color_scheme;
            // We never insert visited styles into the cache so we don't need to try looking it up.
            // It also wouldn't be super-profitable, only a handful :visited properties are
            // non-inherited.
            using_cached_properties = false;
            // TODO(bug 1859385): If we match the same rules when visited and unvisited, we could
            // try to avoid gathering the declarations. That'd be:
            //      unvisited_context.builder.rules.as_ref() == Some(rules)
            iter_declarations(iter, &mut declarations, None, &mut attribute_tracker);

            LonghandIdSet::visited_dependent()
        },
        CascadeMode::Unvisited { visited_rules } => {
            let deferred_custom_properties = {
                let mut builder = CustomPropertiesBuilder::new(stylist, &mut context);
                iter_declarations(
                    iter,
                    &mut declarations,
                    Some(&mut builder),
                    &mut attribute_tracker,
                );
                // Detect cycles, remove properties participating in them, and resolve properties, except:
                // * Registered custom properties that depend on font-relative properties (Resolved)
                //   when prioritary properties are resolved), and
                // * Any property that, in turn, depend on properties like above.
                builder.build(
                    DeferFontRelativeCustomPropertyResolution::Yes,
                    &mut attribute_tracker,
                )
            };

            // Resolve prioritary properties - Guaranteed to not fall into a cycle with existing custom
            // properties.
            cascade.apply_prioritary_properties(
                &mut context,
                &declarations,
                &mut shorthand_cache,
                &mut attribute_tracker,
            );

            // Resolve the deferred custom properties.
            if let Some(deferred) = deferred_custom_properties {
                CustomPropertiesBuilder::build_deferred(
                    deferred,
                    stylist,
                    &mut context,
                    &mut attribute_tracker,
                );
            }

            if let Some(visited_rules) = visited_rules {
                cascade.compute_visited_style_if_needed(
                    &mut context,
                    element,
                    parent_style,
                    layout_parent_style,
                    visited_rules,
                    guards,
                );
            }

            using_cached_properties =
                cascade.try_to_use_cached_properties(&mut context, rule_cache, guards);

            if !using_cached_properties {
                LonghandIdSet::late_group()
            } else {
                match cascade.cascade_reparenting {
                    CascadeReparenting::No => LonghandIdSet::late_group_only_inherited(),
                    CascadeReparenting::PositionTry { .. } => {
                        LonghandIdSet::late_group_position_try()
                    },
                    CascadeReparenting::FirstLine { .. } => {
                        LonghandIdSet::late_group_first_line_only_inherited()
                    },
                }
            }
        },
    };

    cascade.apply_non_prioritary_properties(
        &mut context,
        &declarations.longhand_declarations,
        &mut shorthand_cache,
        &properties_to_apply,
        &mut attribute_tracker,
    );

    context.builder.attribute_references = attribute_tracker.finalize();

    cascade.finished_applying_properties(&mut context.builder);

    context.builder.clear_modified_reset();

    if matches!(cascade_mode, CascadeMode::Unvisited { .. }) {
        StyleAdjuster::new(&mut context.builder).adjust(
            layout_parent_style.unwrap_or(inherited_style),
            element,
            try_tactic,
            &cascade.author_specified,
        );
    }

    if context.builder.modified_reset() || using_cached_properties {
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

#[cfg(feature = "gecko")]
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
    #[cfg(feature = "gecko")]
    {
        let forced = context
            .builder
            .get_inherited_text()
            .clone_forced_color_adjust();
        if forced == computed::ForcedColorAdjust::None {
            return;
        }
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
            if static_prefs::pref!("browser.display.permit_backplate") {
                if bkg
                    .0
                    .iter()
                    .all(|image| matches!(*image, Image::Url(..) | Image::None))
                {
                    return;
                }
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
struct Declarations<'a> {
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

struct Cascade<'b> {
    cascade_reparenting: CascadeReparenting<'b>,
    try_tactic: &'b PositionTryFallbacksTryTactic,
    ignore_colors: bool,
    seen: LonghandIdSet,
    author_specified: LonghandIdSet,
    reverted_set: LonghandIdSet,
    reverted: FxHashMap<LonghandId, (CascadePriority, RevertKind)>,
    declarations_to_apply_unless_overridden: DeclarationsToApplyUnlessOverriden,
}

impl<'b> Cascade<'b> {
    fn new(
        cascade_reparenting: CascadeReparenting<'b>,
        try_tactic: &'b PositionTryFallbacksTryTactic,
        ignore_colors: bool,
    ) -> Self {
        Self {
            cascade_reparenting,
            try_tactic,
            ignore_colors,
            seen: LonghandIdSet::default(),
            author_specified: LonghandIdSet::default(),
            reverted_set: Default::default(),
            reverted: Default::default(),
            declarations_to_apply_unless_overridden: Default::default(),
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
            &context.builder.substitution_functions(),
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
    ) -> bool {
        let mut index = decls.prioritary_positions[id as usize].most_important;
        if index == DeclarationIndex::MAX {
            return false;
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
            if self.seen.contains(longhand_id) {
                return true; // Common case, we're done.
            }
            debug_assert!(
                decl.next_index == 0 || decl.next_index > index,
                "should make progress! {} -> {}",
                index,
                decl.next_index,
            );
            index = decl.next_index;
            if index == 0 {
                break;
            }
        }
        false
    }

    fn apply_prioritary_properties(
        &mut self,
        context: &mut computed::Context,
        decls: &Declarations,
        cache: &mut ShorthandsWithPropertyReferencesCache,
        attribute_tracker: &mut AttributeTracker,
    ) {
        // Keeps apply_one_prioritary_property calls readable, considering the repititious
        // arguments.
        macro_rules! apply {
            ($prop:ident) => {
                self.apply_one_prioritary_property(
                    context,
                    decls,
                    cache,
                    PrioritaryPropertyId::$prop,
                    attribute_tracker,
                )
            };
        }

        if !decls.has_prioritary_properties {
            return;
        }

        #[cfg(feature = "gecko")]
        apply!(MozDefaultAppearance);
        #[cfg(feature = "gecko")]
        if apply!(Appearance) && is_base_appearance(&context) {
            context
                .style()
                .add_flags(ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE);
            context
                .included_cascade_flags
                .insert(RuleCascadeFlags::APPEARANCE_BASE);
        }

        let has_writing_mode = apply!(WritingMode) | apply!(Direction);
        #[cfg(feature = "gecko")]
        let has_writing_mode = has_writing_mode | apply!(TextOrientation);

        if has_writing_mode {
            context.builder.writing_mode = WritingMode::new(context.builder.get_inherited_box())
        }

        if apply!(Zoom) {
            context.builder.recompute_effective_zooms();
            if !context.builder.effective_zoom_for_inheritance.is_one() {
                // NOTE(emilio): This is a bit of a hack, but matches the shipped WebKit and Blink
                // behavior for now. Ideally, in the future, we have a pass over all
                // implicitly-or-explicitly-inherited properties that can contain lengths and
                // re-compute them properly, see https://github.com/w3c/csswg-drafts/issues/9397.
                // TODO(emilio): we need to eagerly do this for line-height as well, probably.
                self.recompute_font_size_for_zoom_change(&mut context.builder);
            }
        }

        // Compute font-family.
        let has_font_family = apply!(FontFamily);
        let has_lang = apply!(XLang);
        #[cfg(feature = "gecko")]
        {
            if has_lang {
                self.recompute_initial_font_family_if_needed(&mut context.builder);
            }
            if has_font_family {
                self.prioritize_user_fonts_if_needed(&mut context.builder);
            }

            // Compute font-size.
            if apply!(XTextScale) {
                self.unzoom_fonts_if_needed(&mut context.builder);
            }
            let has_font_size = apply!(FontSize);
            let has_math_depth = apply!(MathDepth);
            let has_min_font_size_ratio = apply!(MozMinFontSizeRatio);

            if has_math_depth && has_font_size {
                self.recompute_math_font_size_if_needed(context);
            }
            if has_lang || has_font_family {
                self.recompute_keyword_font_size_if_needed(context);
            }
            if has_font_size || has_min_font_size_ratio || has_lang || has_font_family {
                self.constrain_font_size_if_needed(&mut context.builder);
            }
        }

        #[cfg(feature = "servo")]
        {
            apply!(FontSize);
            if has_lang || has_font_family {
                self.recompute_keyword_font_size_if_needed(context);
            }
        }

        // Compute the rest of the first-available-font-affecting properties.
        apply!(FontWeight);
        apply!(FontStretch);
        apply!(FontStyle);
        #[cfg(feature = "gecko")]
        apply!(FontSizeAdjust);

        #[cfg(feature = "gecko")]
        apply!(ForcedColorAdjust);
        // color-scheme needs to be after forced-color-adjust, since it's one of the "skipped in
        // forced-colors-mode" properties.
        if apply!(ColorScheme) {
            context.builder.color_scheme = context.builder.get_inherited_ui().color_scheme_bits();
        }
        apply!(LineHeight);
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
        for declaration in &*longhand_declarations {
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
                if !self.seen.contains(longhand_id) {
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
        debug_assert!(self.seen.contains(LonghandId::Zoom));
        for prop in LonghandIdSet::zoom_dependent_inherited_properties().iter() {
            if self.seen.contains(prop) {
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
        if self.seen.contains(longhand_id) {
            return;
        }

        if !(priority.flags() - context.included_cascade_flags).is_empty() {
            return;
        }

        if self.reverted_set.contains(longhand_id) {
            if let Some(&(reverted_priority, revert_kind)) = self.reverted.get(&longhand_id) {
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
                    // We intentionally don't want to insert it into `self.seen`, `reverted` takes
                    // care of rejecting other declarations as needed.
                    self.reverted_set.insert(longhand_id);
                    self.reverted.insert(longhand_id, (priority, revert_kind));
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

        self.seen.insert(longhand_id);
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
        // We could (and used to) use a pattern match here, but that bloats this
        // function to over 100K of compiled code!
        //
        // To improve i-cache behavior, we outline the individual functions and
        // use virtual dispatch instead.
        (CASCADE_PROPERTY[longhand_id as usize])(&declaration, context);
    }

    fn compute_visited_style_if_needed<E>(
        &self,
        context: &mut computed::Context,
        element: Option<E>,
        parent_style: Option<&ComputedValues>,
        layout_parent_style: Option<&ComputedValues>,
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
            self.cascade_reparenting,
            self.try_tactic,
            CascadeMode::Visited {
                unvisited_context: &*context,
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

    fn try_to_use_cached_properties(
        &self,
        context: &mut computed::Context<'b>,
        cache: Option<&'b RuleCache>,
        guards: &StylesheetGuards,
    ) -> bool {
        let style = match self.cascade_reparenting {
            CascadeReparenting::No => {
                let Some(style) = cache.and_then(|c| c.find(guards, &context)) else {
                    return false;
                };
                style
            },
            CascadeReparenting::PositionTry { base_style }
            | CascadeReparenting::FirstLine { base_style } => base_style,
        };

        context.builder.copy_reset_longhands_from(style);

        if let CascadeReparenting::PositionTry { .. } = self.cascade_reparenting {
            context.builder.copy_inherited_longhands_from(style);
            context.builder.reset_position_try_properties();
        }

        // We're using the styles from another element or a base style, and we'll skip applying the
        // relevant properties. So we need to do the relevant bookkeeping here to keep these bits
        // correct.
        //
        // When using this optimization, we also need to copy whether the old style specified
        // viewport units / used font-relative lengths, this one would as well. It matches the same
        // rules, so it is the right thing to do anyways, even if it's only used on inherited
        // properties.
        let bits_to_copy = ComputedValueFlags::HAS_AUTHOR_SPECIFIED_BORDER_BACKGROUND
            | ComputedValueFlags::HAS_AUTHOR_SPECIFIED_GRID_AUTO_FLOW
            | ComputedValueFlags::DEPENDS_ON_SELF_FONT_METRICS
            | ComputedValueFlags::DEPENDS_ON_INHERITED_FONT_METRICS
            | ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE
            | ComputedValueFlags::USES_CONTAINER_UNITS
            | ComputedValueFlags::USES_VIEWPORT_UNITS
            | ComputedValueFlags::USES_FONT_RELATIVE_UNITS
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
        if static_prefs::pref!("browser.display.use_document_fonts") != 0
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

        if !self.seen.contains(LonghandId::XLang) && !self.seen.contains(LonghandId::FontFamily) {
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
        debug_assert!(self.seen.contains(LonghandId::XTextScale));

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
        debug_assert!(self.seen.contains(LonghandId::Zoom));
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
    fn recompute_math_font_size_if_needed(&self, context: &mut computed::Context) {
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
            let scale_between_0_and_1 = parent_script_percent_scale_down.unwrap_or_else(|| c);
            let scale_between_0_and_2 =
                parent_script_script_percent_scale_down.unwrap_or_else(|| c * c);
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
            s *= (c as f32).powi(e);
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
}
