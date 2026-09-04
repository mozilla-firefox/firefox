/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A cache from rule node to computed values, in order to cache reset
//! properties.

use crate::computed_value_flags::ComputedValueFlags;
use crate::context::CascadeInputs;
use crate::logical_geometry::WritingMode;
use crate::properties::{ComputedValues, StyleBuilder};
use crate::rule_tree::{RuleCascadeFlags, StrongRuleNode};
use crate::selector_parser::PseudoElement;
use crate::shared_lock::StylesheetGuards;
use crate::values::computed::{Context, NonNegativeLength};
use crate::values::specified::color::ColorSchemeFlags;
use rustc_hash::FxHashMap;
use servo_arc::Arc;
use smallvec::SmallVec;

/// The conditions for caching and matching a style in the rule cache.
#[derive(Clone, Debug, Default)]
pub struct RuleCacheConditions {
    uncacheable: bool,
    font_size: Option<NonNegativeLength>,
    line_height: Option<NonNegativeLength>,
    writing_mode: Option<WritingMode>,
    color_scheme: Option<ColorSchemeFlags>,
}

impl RuleCacheConditions {
    /// Sets the style as depending in the font-size value.
    pub fn set_font_size_dependency(&mut self, font_size: NonNegativeLength) {
        debug_assert!(self.font_size.is_none_or(|f| f == font_size));
        self.font_size = Some(font_size);
    }

    /// Sets the style as depending in the line-height value.
    pub fn set_line_height_dependency(&mut self, line_height: NonNegativeLength) {
        debug_assert!(self.line_height.is_none_or(|l| l == line_height));
        self.line_height = Some(line_height);
    }

    /// Sets the style as depending in the color-scheme property value.
    pub fn set_color_scheme_dependency(&mut self, color_scheme: ColorSchemeFlags) {
        debug_assert!(self.color_scheme.is_none_or(|cs| cs == color_scheme));
        self.color_scheme = Some(color_scheme);
    }

    /// Sets the style as uncacheable.
    pub fn set_uncacheable(&mut self) {
        self.uncacheable = true;
    }

    /// Sets the style as depending in the writing-mode value `writing_mode`.
    pub fn set_writing_mode_dependency(&mut self, writing_mode: WritingMode) {
        debug_assert!(self.writing_mode.is_none_or(|wm| wm == writing_mode));
        self.writing_mode = Some(writing_mode);
    }

    /// Returns whether the current style's reset properties are cacheable.
    fn cacheable(&self) -> bool {
        !self.uncacheable
    }
}

#[derive(Debug)]
struct CachedConditions {
    font_size: Option<NonNegativeLength>,
    line_height: Option<NonNegativeLength>,
    color_scheme: Option<ColorSchemeFlags>,
    writing_mode: Option<WritingMode>,
}

impl CachedConditions {
    /// Returns whether `style` matches the conditions.
    fn matches(&self, cached_style: &ComputedValues, style: &StyleBuilder) -> bool {
        if cached_style.effective_zoom != style.effective_zoom {
            return false;
        }

        if cached_style
            .flags
            .intersects(ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE)
            != style
                .flags()
                .intersects(ComputedValueFlags::IS_IN_APPEARANCE_BASE_SUBTREE)
        {
            return false;
        }

        if let Some(fs) = self.font_size {
            if style.get_font().clone_font_size().computed_size != fs {
                return false;
            }
        }

        if let Some(lh) = self.line_height {
            let new_line_height =
                style
                    .device
                    .calc_line_height(style.get_font(), style.writing_mode, None);
            if new_line_height != lh {
                return false;
            }
        }

        if self
            .color_scheme
            .is_some_and(|cs| style.get_inherited_ui().color_scheme_bits() != cs)
        {
            return false;
        }

        if self.writing_mode.is_some_and(|wm| style.writing_mode != wm) {
            return false;
        }

        true
    }
}

/// A TLS cache from rules matched to computed values.
pub struct RuleCache {
    // FIXME(emilio): Consider using LRUCache or something like that?
    map: FxHashMap<StrongRuleNode, SmallVec<[(CachedConditions, Arc<ComputedValues>); 1]>>,
}

impl Default for RuleCache {
    fn default() -> Self {
        Self::new()
    }
}

impl RuleCache {
    /// Creates an empty `RuleCache`.
    pub fn new() -> Self {
        Self {
            map: FxHashMap::default(),
        }
    }

    /// Walk the rule tree and return a rule node for using as the key
    /// for rule cache.
    ///
    /// It currently skips animation / style attribute / preshint rules when they don't contain any
    /// declaration of a reset property. We don't skip other levels because walking the whole
    /// parent chain can be expensive.
    ///
    /// TODO(emilio): Measure this, this was not super-well measured for performance (this was done
    /// for memory in bug 1427681)... Walking the rule tree might be worth it if we hit the cache
    /// enough?
    fn get_rule_node_for_cache<'r>(
        guards: &StylesheetGuards,
        mut rule_node: Option<&'r StrongRuleNode>,
    ) -> Option<&'r StrongRuleNode> {
        use crate::rule_tree::CascadeOrigin;
        while let Some(node) = rule_node {
            let priority = node.cascade_priority();
            let cascade_level = priority.cascade_level();
            let should_try_to_skip = cascade_level.is_animation()
                || cascade_level.origin() == CascadeOrigin::PresHints
                || priority.layer_order().is_style_attribute_layer();
            if !should_try_to_skip {
                break;
            }
            if let Some(source) = node.style_source() {
                let decls = source.get().read_with(cascade_level.guard(guards));
                if decls.contains_any_reset() {
                    break;
                }
            }
            rule_node = node.parent();
        }
        rule_node
    }

    /// Finds a node in the properties matched cache.
    ///
    /// This needs to receive a `StyleBuilder` with the `early` properties
    /// already applied.
    pub fn find(&self, guards: &StylesheetGuards, context: &Context) -> Option<&ComputedValues> {
        // A pseudo-element with property restrictions can result in different
        // computed values if it's also used for a non-pseudo.
        if context
            .builder
            .pseudo
            .is_some_and(|p| !p.property_restriction().is_empty())
        {
            return None;
        }

        if context
            .included_cascade_flags
            .contains(RuleCascadeFlags::STARTING_STYLE)
        {
            // We don't want to cache nor include starting-style rules.
            return None;
        }

        let rules = context.builder.rules.as_ref();
        let rules = Self::get_rule_node_for_cache(guards, rules)?;
        let cached_values = self.map.get(rules)?;

        for (conditions, values) in cached_values.iter() {
            if conditions.matches(values, &context.builder) {
                debug!("Using cached reset style with conditions {:?}", conditions);
                return Some(&**values);
            }
        }
        None
    }

    /// Inserts a node into the rules cache if possible.
    ///
    /// Returns whether the style was inserted into the cache.
    pub fn insert_if_possible(
        &mut self,
        guards: &StylesheetGuards,
        style: &Arc<ComputedValues>,
        pseudo: Option<&PseudoElement>,
        inputs: &CascadeInputs,
        conditions: &RuleCacheConditions,
    ) -> bool {
        if !conditions.cacheable() {
            return false;
        }

        // A pseudo-element with property restrictions can result in different computed values if
        // it's also used for a non-pseudo.
        // TODO: we could consider inserting them and just checking the builder like we do for zoom.
        if pseudo.is_some_and(|p| !p.property_restriction().is_empty()) {
            return false;
        }

        // Don't insert @starting-style styles in the cache, for the same reason.
        if inputs
            .included_cascade_flags
            .contains(RuleCascadeFlags::STARTING_STYLE)
        {
            return false;
        }

        let rules = style.rules.as_ref();
        let rules = match Self::get_rule_node_for_cache(guards, rules) {
            Some(r) => r.clone(),
            None => return false,
        };

        debug!(
            "Inserting cached reset style with conditions {:?}",
            conditions
        );
        let cached_conditions = CachedConditions {
            writing_mode: conditions.writing_mode,
            font_size: conditions.font_size,
            line_height: conditions.line_height,
            color_scheme: conditions.color_scheme,
        };
        self.map
            .entry(rules)
            .or_default()
            .push((cached_conditions, style.clone()));
        true
    }
}
