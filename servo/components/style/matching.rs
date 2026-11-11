/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! High-level interface to CSS selector matching.

#![allow(unsafe_code)]
#![deny(missing_docs)]

use crate::computed_value_flags::ComputedValueFlags;
#[cfg(feature = "servo")]
use crate::context::CascadeInputs;
use crate::context::{ElementCascadeInputs, QuirksMode};
use crate::context::{SharedStyleContext, StyleContext};
use crate::data::ElementData;
#[cfg(feature = "servo")]
use crate::data::ElementStyles;
use crate::dom::TElement;
#[cfg(feature = "servo")]
use crate::dom::TNode;
use crate::invalidation::element::restyle_hints::RestyleHint;
use crate::properties::ComputedValues;
use crate::properties::PropertyDeclarationBlock;
use crate::properties::longhands::display::computed_value::T as Display;
#[cfg(feature = "servo")]
use crate::rule_tree::RuleCascadeFlags;
use crate::rule_tree::{CascadeLevel, CascadeOrigin, StrongRuleNode};
use crate::selector_parser::{PseudoElement, RestyleDamage};
use crate::shared_lock::Locked;
use crate::style_resolver::StyleResolverForElement;
use crate::style_resolver::{PseudoElementResolution, ResolvedElementStyles};
use crate::stylesheets::layer_rule::LayerOrder;
use crate::stylist::RuleInclusion;
use crate::traversal_flags::TraversalFlags;
use crate::values::generics::animation::GenericAnimationTimeline;
use crate::values::specified::animation::Scroller;
use servo_arc::{Arc, ArcBorrow};

/// Represents the result of comparing an element's old and new style.
#[derive(Debug)]
pub struct StyleDifference {
    /// The resulting damage.
    pub damage: RestyleDamage,
    /// Whether any styles changed.
    pub change: StyleChange,
}

/// Represents whether or not the style of an element has changed.
#[derive(Clone, Copy, Debug)]
pub enum StyleChange {
    /// The style hasn't changed.
    Unchanged,
    /// The style has changed.
    Changed {
        /// Whether only reset properties have changed.
        reset_only: bool,
        /// Whether custom properties have changed.
        custom_properties_changed: bool,
    },
}

/// Determines which styles are being cascaded currently.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum CascadeVisitedMode {
    /// Cascade the regular, unvisited styles.
    Unvisited,
    /// Cascade the styles used when an element's relevant link is visited.  A
    /// "relevant link" is the element being matched if it is a link or the
    /// nearest ancestor link.
    Visited,
}

trait PrivateMatchMethods: TElement {
    fn replace_single_rule_node(
        context: &SharedStyleContext,
        level: CascadeLevel,
        layer_order: LayerOrder,
        pdb: Option<ArcBorrow<Locked<PropertyDeclarationBlock>>>,
        path: &mut StrongRuleNode,
    ) -> bool {
        let stylist = &context.stylist;
        let guards = &context.guards;

        let mut important_rules_changed = false;
        let new_node = stylist.rule_tree().update_rule_at_level(
            level,
            layer_order,
            pdb,
            path,
            guards,
            &mut important_rules_changed,
        );
        if let Some(n) = new_node {
            *path = n;
        }
        important_rules_changed
    }

    /// Updates the rule nodes without re-running selector matching, using just
    /// the rule tree, for a specific visited mode.
    ///
    /// Returns true if an !important rule was replaced.
    fn replace_rules_internal(
        &self,
        replacements: RestyleHint,
        context: &mut StyleContext<Self>,
        cascade_visited: CascadeVisitedMode,
        cascade_inputs: &mut ElementCascadeInputs,
    ) -> bool {
        debug_assert!(
            replacements.intersects(RestyleHint::replacements())
                && (replacements & !RestyleHint::replacements()).is_empty()
        );

        let primary_rules = match cascade_visited {
            CascadeVisitedMode::Unvisited => cascade_inputs.primary.rules.as_mut(),
            CascadeVisitedMode::Visited => cascade_inputs.primary.visited_rules.as_mut(),
        };

        let primary_rules = match primary_rules {
            Some(r) => r,
            None => return false,
        };

        let mut result = false;
        if replacements.contains(RestyleHint::RESTYLE_STYLE_ATTRIBUTE) {
            let style_attribute = self.style_attribute();
            result |= Self::replace_single_rule_node(
                context.shared,
                CascadeLevel::same_tree_author_normal(),
                LayerOrder::style_attribute(),
                style_attribute,
                primary_rules,
            );
            result |= Self::replace_single_rule_node(
                context.shared,
                CascadeLevel::same_tree_author_important(),
                LayerOrder::style_attribute(),
                style_attribute,
                primary_rules,
            );
        }

        if replacements.intersects(RestyleHint::for_animations()) {
            if replacements.contains(RestyleHint::RESTYLE_SMIL) {
                Self::replace_single_rule_node(
                    context.shared,
                    CascadeLevel::new(CascadeOrigin::SMILOverride),
                    LayerOrder::root(),
                    self.smil_override(),
                    primary_rules,
                );
            }

            if replacements.contains(RestyleHint::RESTYLE_CSS_TRANSITIONS) {
                Self::replace_single_rule_node(
                    context.shared,
                    CascadeLevel::new(CascadeOrigin::Transitions),
                    LayerOrder::root(),
                    self.transition_rule(context.shared)
                        .as_ref()
                        .map(|a| a.borrow_arc()),
                    primary_rules,
                );
            }

            if replacements.contains(RestyleHint::RESTYLE_CSS_ANIMATIONS) {
                Self::replace_single_rule_node(
                    context.shared,
                    CascadeLevel::new(CascadeOrigin::Animations),
                    LayerOrder::root(),
                    self.animation_rule(context.shared)
                        .as_ref()
                        .map(|a| a.borrow_arc()),
                    primary_rules,
                );
            }
        }

        result
    }

    #[inline]
    fn requires_animation_update_for_scroll_self(
        old: &ComputedValues,
        new: &ComputedValues,
    ) -> bool {
        // Need to specifically take care of `animation-timeline: scroll(self)` - unlike other values, it can become inactive.
        // When we switch in and out of being scrollable, we should make sure to perform the animation update.
        // Specifying scroll in any axis makes the other axis scrollable [1], so we need to update on either axis changing.
        // This does not apply to `scroll(root)`, since the viewport scroller is always available, or `scroll(nearest)`,
        // which will go up to root.
        // [1]: https://drafts.csswg.org/css-overflow/#propdef-overflow
        let scrollable_changed = old.get_overflow_x().is_scrollable()
            != new.get_overflow_x().is_scrollable()
            || old.get_overflow_y().is_scrollable() != new.get_overflow_y().is_scrollable();
        if !scrollable_changed {
            return false;
        }
        new.get_ui().animation_timeline_iter().any(|timeline| {
            let scroll_function = match timeline {
                GenericAnimationTimeline::Scroll(ref sf) => sf,
                _ => return false,
            };
            if scroll_function.scroller != Scroller::SelfElement {
                return false;
            }
            true
        })
    }

    /// If there is no transition rule in the ComputedValues, it returns None.
    fn after_change_style(
        &self,
        context: &mut StyleContext<Self>,
        primary_style: &Arc<ComputedValues>,
    ) -> Option<Arc<ComputedValues>> {
        // Actually `PseudoElementResolution` doesn't really matter.
        StyleResolverForElement::new(
            *self,
            context,
            RuleInclusion::All,
            PseudoElementResolution::IfApplicable,
        )
        .after_change_style(primary_style)
    }

    fn needs_animations_update(
        &self,
        context: &mut StyleContext<Self>,
        old_style: Option<&ComputedValues>,
        new_style: &ComputedValues,
        pseudo_element: Option<PseudoElement>,
    ) -> bool {
        let new_ui_style = new_style.get_ui();
        let new_style_specifies_animations = new_ui_style.specifies_animations();

        let has_animations = self.has_css_animations(context.shared, pseudo_element);
        if !new_style_specifies_animations && !has_animations {
            return false;
        }

        let old_style = match old_style {
            Some(old) => old,
            // If we have no old style but have animations, we may be a
            // pseudo-element which was re-created without style changes.
            //
            // This can happen when we reframe the pseudo-element without
            // restyling it (due to content insertion on a flex container or
            // such, for example). See bug 1564366.
            //
            // FIXME(emilio): The really right fix for this is keeping the
            // pseudo-element itself around on reframes, but that's a bit
            // harder. If we do that we can probably remove quite a lot of the
            // EffectSet complexity though, since right now it's stored on the
            // parent element for pseudo-elements given we need to keep it
            // around...
            None => {
                return new_style_specifies_animations || new_style.is_pseudo_style();
            },
        };

        let old_ui_style = old_style.get_ui();

        let keyframes_could_have_changed = context
            .shared
            .traversal_flags
            .contains(TraversalFlags::ForCSSRuleChanges);

        // If the traversal is triggered due to changes in CSS rules changes, we
        // need to try to update all CSS animations on the element if the
        // element has or will have CSS animation style regardless of whether
        // the animation is running or not.
        //
        // TODO: We should check which @keyframes were added/changed/deleted and
        // update only animations corresponding to those @keyframes.
        if keyframes_could_have_changed {
            return true;
        }

        // If the animations changed, well...
        if !old_ui_style.animations_equals(new_ui_style) {
            return true;
        }

        let old_display = *old_style.get_display();
        let new_display = *new_style.get_display();

        // If we were display: none, we may need to trigger animations.
        if old_display == Display::None && new_display != Display::None {
            return new_style_specifies_animations;
        }

        // If we are becoming display: none, we may need to stop animations.
        if old_display != Display::None && new_display == Display::None {
            return has_animations;
        }

        // We might need to update animations if writing-mode or direction
        // changed, and any of the animations contained logical properties.
        //
        // We may want to be more granular, but it's probably not worth it.
        if new_style.writing_mode != old_style.writing_mode {
            return has_animations;
        }

        if Self::requires_animation_update_for_scroll_self(old_style, new_style) {
            return has_animations;
        }

        false
    }

    fn might_need_transitions_update(
        &self,
        context: &StyleContext<Self>,
        old_style: Option<&ComputedValues>,
        new_style: &ComputedValues,
        pseudo_element: Option<PseudoElement>,
    ) -> bool {
        let old_style = match old_style {
            Some(v) => v,
            None => return false,
        };

        if !self.has_css_transitions(context.shared, pseudo_element)
            && !new_style.get_ui().specifies_transitions()
        {
            return false;
        }

        if old_style.get_display().is_none() {
            return false;
        }

        true
    }

    #[cfg(feature = "gecko")]
    fn maybe_resolve_starting_style(
        &self,
        context: &mut StyleContext<Self>,
        before_change_style: Option<&Arc<ComputedValues>>,
        new_styles: &ResolvedElementStyles,
    ) -> Option<Arc<ComputedValues>> {
        // For both cases:
        // If there is no transitions specified we don't have to resolve starting style.
        let new_primary = new_styles.primary_style();
        if !new_primary.get_ui().specifies_transitions() {
            return None;
        }

        // We resolve starting style only if we don't have before-change-style, or we change from
        // display:none.
        if before_change_style.is_some()
            && !new_primary.is_display_property_changed_from_none(before_change_style.map(|s| &**s))
        {
            return None;
        }

        let mut resolver = StyleResolverForElement::new(
            *self,
            context,
            RuleInclusion::All,
            PseudoElementResolution::IfApplicable,
        );

        let starting_style = resolver.resolve_starting_style(new_primary)?;
        if starting_style.style().get_display().is_none() {
            return None;
        }

        Some(starting_style.0)
    }

    /// Handle CSS Transitions. Returns None if we don't need to update transitions. And it returns
    /// the before-change style per CSS Transitions spec.
    ///
    /// Note: The before-change style could be the computed values of all properties on the element
    /// as of the previous style change event, or the starting style if we don't have the valid
    /// before-change style there.
    #[cfg(feature = "gecko")]
    fn process_transitions(
        &self,
        context: &mut StyleContext<Self>,
        before_change_style: Option<&Arc<ComputedValues>>,
        new_styles: &mut ResolvedElementStyles,
    ) -> Option<Arc<ComputedValues>> {
        let starting_values =
            self.maybe_resolve_starting_style(context, before_change_style, new_styles);
        let before_change_or_starting = starting_values.as_ref().or(before_change_style);
        let new_values = new_styles.primary_style_mut();

        if !self.might_need_transitions_update(
            context,
            before_change_or_starting.map(|s| &**s),
            new_values,
            /* pseudo_element = */ None,
        ) {
            return None;
        }

        let after_change_style =
            if self.has_css_transitions(context.shared, /* pseudo_element = */ None) {
                self.after_change_style(context, new_values)
            } else {
                None
            };

        // In order to avoid creating a SequentialTask for transitions which
        // may not be updated, we check it per property to make sure Gecko
        // side will really update transition.
        if !self.needs_transitions_update(
            before_change_or_starting.unwrap(),
            after_change_style.as_ref().unwrap_or(new_values),
        ) {
            return None;
        }

        if let Some(values_without_transitions) = after_change_style {
            *new_values = values_without_transitions;
        }

        // Move the new-created starting style, or clone the before-change style.
        if starting_values.is_some() {
            starting_values
        } else {
            before_change_style.cloned()
        }
    }

    /// Updates animations and transitions for the new style. `before_change_style` is the
    /// before-change style of the element (see `StyleResolverForElement::before_change_style`), or
    /// None if this is the initial style.
    #[cfg(feature = "gecko")]
    fn process_animations(
        &self,
        context: &mut StyleContext<Self>,
        new_styles: &mut ResolvedElementStyles,
        important_rules_changed: bool,
        before_change_style: Option<&Arc<ComputedValues>>,
    ) {
        use crate::context::UpdateAnimationsTasks;
        use crate::properties::style_structs::UI;

        let old_values = before_change_style.map(|s| &**s);

        // Bug 868975: These steps should examine and update the visited styles
        // in addition to the unvisited styles.

        let mut tasks = UpdateAnimationsTasks::empty();

        {
            let new_ui = new_styles.primary_style().get_ui();
            let changed = |specified: fn(&UI) -> bool, equals: fn(&UI, &UI) -> bool| {
                old_values.map_or_else(|| specified(new_ui), |old| !equals(old.get_ui(), new_ui))
            };
            if changed(UI::specifies_timeline_scope, UI::timeline_scope_equals) {
                tasks.insert(UpdateAnimationsTasks::TIMELINE_SCOPES);
            }
            if changed(UI::specifies_scroll_timelines, UI::scroll_timelines_equals) {
                tasks.insert(UpdateAnimationsTasks::SCROLL_TIMELINES);
            }
            if changed(UI::specifies_view_timelines, UI::view_timelines_equals) {
                tasks.insert(UpdateAnimationsTasks::VIEW_TIMELINES);
            }
        }

        if self.needs_animations_update(
            context,
            old_values,
            new_styles.primary_style(),
            /* pseudo_element = */ None,
        ) {
            tasks.insert(UpdateAnimationsTasks::CSS_ANIMATIONS);
        }

        let transitions_before_change_style =
            self.process_transitions(context, before_change_style, new_styles);
        if transitions_before_change_style.is_some() {
            tasks.insert(UpdateAnimationsTasks::CSS_TRANSITIONS);
        }

        if self.has_animations(context.shared) {
            tasks.insert(UpdateAnimationsTasks::EFFECT_PROPERTIES);
            if important_rules_changed {
                tasks.insert(UpdateAnimationsTasks::CASCADE_RESULTS);
            }
            if new_styles
                .primary_style()
                .is_display_property_changed_from_none(old_values)
            {
                tasks.insert(UpdateAnimationsTasks::DISPLAY_CHANGED_FROM_NONE);
            }
        }

        if !tasks.is_empty() {
            let task = crate::context::SequentialTask::update_animations(
                *self,
                transitions_before_change_style,
                tasks,
            );
            context.thread_local.tasks.push(task);
        }
    }

    #[cfg(feature = "servo")]
    fn process_animations(
        &self,
        context: &mut StyleContext<Self>,
        old_styles: &mut ElementStyles,
        new_resolved_styles: &mut ResolvedElementStyles,
        _important_rules_changed: bool,
    ) {
        use crate::animation::AnimationSetKey;
        use crate::dom::TDocument;

        let style_changed = self.process_animations_for_style(
            context,
            &mut old_styles.primary,
            new_resolved_styles.primary_style_mut(),
            /* pseudo_element = */ None,
        );

        // If we have modified animation or transitions, we recascade style for this node.
        if style_changed {
            let primary_style = new_resolved_styles.primary_style();
            let mut rule_node = primary_style.rules().clone();
            let declarations = context.shared.animations.get_all_declarations(
                &AnimationSetKey::new_for_non_pseudo(self.as_node().opaque()),
                context.shared.current_time_for_animations,
                self.as_node().owner_doc().shared_lock(),
            );
            Self::replace_single_rule_node(
                &context.shared,
                CascadeLevel::new(CascadeOrigin::Transitions),
                LayerOrder::root(),
                declarations.transitions.as_ref().map(|a| a.borrow_arc()),
                &mut rule_node,
            );
            Self::replace_single_rule_node(
                &context.shared,
                CascadeLevel::new(CascadeOrigin::Animations),
                LayerOrder::root(),
                declarations.animations.as_ref().map(|a| a.borrow_arc()),
                &mut rule_node,
            );

            if rule_node != *primary_style.rules() {
                let inputs = CascadeInputs {
                    rules: Some(rule_node),
                    visited_rules: primary_style.visited_rules().cloned(),
                    flags: primary_style.flags.for_cascade_inputs(),
                    included_cascade_flags: RuleCascadeFlags::empty(),
                };

                new_resolved_styles.primary.style = StyleResolverForElement::new(
                    *self,
                    context,
                    RuleInclusion::All,
                    PseudoElementResolution::IfApplicable,
                )
                .cascade_style_and_visited_with_default_parents(inputs);
            }
        }

        self.process_animations_for_pseudo(
            context,
            old_styles,
            new_resolved_styles,
            PseudoElement::Before,
        );
        self.process_animations_for_pseudo(
            context,
            old_styles,
            new_resolved_styles,
            PseudoElement::After,
        );
    }

    #[cfg(feature = "servo")]
    fn process_animations_for_pseudo(
        &self,
        context: &mut StyleContext<Self>,
        old_styles: &ElementStyles,
        new_resolved_styles: &mut ResolvedElementStyles,
        pseudo_element: PseudoElement,
    ) {
        use crate::animation::AnimationSetKey;
        use crate::dom::TDocument;

        let key = AnimationSetKey::new_for_pseudo(self.as_node().opaque(), pseudo_element.clone());
        let style = match new_resolved_styles.pseudos.get(&pseudo_element) {
            Some(style) => Arc::clone(style),
            None => {
                context
                    .shared
                    .animations
                    .cancel_all_animations_for_key(&key);
                return;
            },
        };

        let old_style = old_styles.pseudos.get(&pseudo_element).cloned();
        self.process_animations_for_style(
            context,
            &old_style,
            &style,
            Some(pseudo_element.clone()),
        );

        let declarations = context.shared.animations.get_all_declarations(
            &key,
            context.shared.current_time_for_animations,
            self.as_node().owner_doc().shared_lock(),
        );
        if declarations.is_empty() {
            return;
        }

        let mut rule_node = style.rules().clone();
        Self::replace_single_rule_node(
            &context.shared,
            CascadeLevel::new(CascadeOrigin::Transitions),
            LayerOrder::root(),
            declarations.transitions.as_ref().map(|a| a.borrow_arc()),
            &mut rule_node,
        );
        Self::replace_single_rule_node(
            &context.shared,
            CascadeLevel::new(CascadeOrigin::Animations),
            LayerOrder::root(),
            declarations.animations.as_ref().map(|a| a.borrow_arc()),
            &mut rule_node,
        );
        if rule_node == *style.rules() {
            return;
        }

        let inputs = CascadeInputs {
            rules: Some(rule_node),
            visited_rules: style.visited_rules().cloned(),
            flags: style.flags.for_cascade_inputs(),
            included_cascade_flags: RuleCascadeFlags::empty(),
        };

        let new_style = StyleResolverForElement::new(
            *self,
            context,
            RuleInclusion::All,
            PseudoElementResolution::IfApplicable,
        )
        .cascade_style_and_visited_for_pseudo_with_default_parents(
            inputs,
            &pseudo_element,
            &new_resolved_styles.primary,
        );

        new_resolved_styles
            .pseudos
            .set(&pseudo_element, new_style.0);
    }

    #[cfg(feature = "servo")]
    fn process_animations_for_style(
        &self,
        context: &mut StyleContext<Self>,
        old_values: &Option<Arc<ComputedValues>>,
        new_values: &Arc<ComputedValues>,
        pseudo_element: Option<PseudoElement>,
    ) -> bool {
        use crate::animation::{AnimationSetKey, AnimationState};

        // We need to call this before accessing the `ElementAnimationSet` from the
        // map because this call will do a RwLock::read().
        let needs_animations_update = self.needs_animations_update(
            context,
            old_values.as_deref(),
            new_values,
            pseudo_element,
        );

        let might_need_transitions_update = self.might_need_transitions_update(
            context,
            old_values.as_deref(),
            new_values,
            pseudo_element,
        );

        let mut after_change_style = None;
        if might_need_transitions_update {
            after_change_style = self.after_change_style(context, new_values);
        }

        let key = AnimationSetKey::new(self.as_node().opaque(), pseudo_element);
        let shared_context = context.shared;
        let mut animation_set = shared_context
            .animations
            .sets
            .write()
            .remove(&key)
            .unwrap_or_default();

        // Starting animations is expensive, because we have to recalculate the style
        // for all the keyframes. We only want to do this if we think that there's a
        // chance that the animations really changed.
        if needs_animations_update {
            let mut resolver = StyleResolverForElement::new(
                *self,
                context,
                RuleInclusion::All,
                PseudoElementResolution::IfApplicable,
            );

            animation_set.update_animations_for_new_style::<Self>(
                *self,
                &shared_context,
                &new_values,
                &mut resolver,
            );
        }

        animation_set.update_transitions_for_new_style(
            might_need_transitions_update,
            &shared_context,
            old_values.as_ref(),
            after_change_style.as_ref().unwrap_or(new_values),
        );

        // This should change the computed values in the style, so we don't need
        // to mark this set as dirty.
        animation_set
            .transitions
            .retain(|transition| transition.state != AnimationState::Finished);

        animation_set
            .animations
            .retain(|animation| animation.state != AnimationState::Finished);

        // If the ElementAnimationSet is empty, and don't store it in order to
        // save memory and to avoid extra processing later.
        let changed_animations = animation_set.dirty;
        if !animation_set.is_empty() {
            animation_set.dirty = false;
            shared_context
                .animations
                .sets
                .write()
                .insert(key, animation_set);
        }

        changed_animations
    }

    /// Computes and applies non-redundant damage.
    fn accumulate_damage_for(
        &self,
        shared_context: &SharedStyleContext,
        damage: &mut RestyleDamage,
        old_values: &ComputedValues,
        new_values: &ComputedValues,
        pseudo: Option<&PseudoElement>,
    ) -> RestyleHint {
        debug!("accumulate_damage_for: {:?}", self);
        debug_assert!(
            !shared_context
                .traversal_flags
                .contains(TraversalFlags::Forgetful)
        );

        let difference = self.compute_style_difference(old_values, new_values, pseudo);

        *damage |= difference.damage;

        debug!(" > style difference: {:?}", difference);

        let mut children_hint = RestyleHint::empty();
        if old_values.flags.maybe_inherited() != new_values.flags.maybe_inherited() {
            // Even if the styles are otherwise equal, we need to cascade the children in order to
            // ensure the correct propagation of inherited computed value flags.
            debug!(
                " > flags changed: {:?} != {:?}",
                old_values.flags, new_values.flags
            );
            children_hint |= RestyleHint::RECASCADE_SELF;
        } else if old_values.effective_zoom != new_values.effective_zoom {
            // Similarly, even if styles are equal, we need to propagate zoom changes.
            debug!(
                " > zoom changed: {:?} != {:?}",
                old_values.effective_zoom, new_values.effective_zoom
            );
            children_hint |= RestyleHint::RECASCADE_SELF;
        }

        let StyleChange::Changed {
            reset_only,
            custom_properties_changed,
        } = difference.change
        else {
            return children_hint;
        };

        let new_container_name = new_values.get_container_name();
        if new_container_name != old_values.get_container_name() {
            // If we're becoming or stopped to become a named container, we need to potentially
            // restyle children.
            children_hint |= RestyleHint::RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER;
        } else if custom_properties_changed {
            // Custom property changes affect style queries. How specifically depends on whether
            // we're a named container (more expensive, need to check the subtree) or not.
            children_hint |= if !new_container_name.is_none() {
                RestyleHint::RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER
            } else {
                RestyleHint::RESTYLE_IF_AFFECTED_BY_STYLE_QUERIES
            };
        }

        if reset_only {
            // If only reset properties changed, we _might_ need to unconditionally restyle, but
            // most likely we can get away with stopping the cascade at the next level, if our
            // children don't inherit reset properties.
            children_hint |=
                if need_to_unconditionally_recascade_for_reset_change(old_values, new_values) {
                    RestyleHint::RECASCADE_SELF
                } else {
                    RestyleHint::RECASCADE_SELF_IF_INHERIT_RESET_STYLE
                };
        } else {
            // If inherited properties changed, we need to cascade our children.
            children_hint |= RestyleHint::RECASCADE_SELF;
        }

        children_hint
    }
}

/// Whether we need to recascade children for a change in non-inherited properties.
fn need_to_unconditionally_recascade_for_reset_change(
    old_values: &ComputedValues,
    new_values: &ComputedValues,
) -> bool {
    let old_display = *old_values.get_display();
    let new_display = *new_values.get_display();

    if old_display != new_display {
        // If we used to be a display: none element, and no longer are, our
        // children need to be restyled because they're unstyled.
        if old_display == Display::None {
            return true;
        }
        // Blockification of children may depend on our display value, so we need to actually do the
        // recascade. We could potentially do better, but it doesn't seem worth it.
        if old_display.is_item_container() != new_display.is_item_container() {
            return true;
        }
        // We may also need to blockify and un-blockify descendants if our display goes from / to
        // display: contents, since the "layout parent style" changes.
        if old_display.is_contents() || new_display.is_contents() {
            return true;
        }
        // Line break suppression may also be affected if the display
        // type changes from ruby to non-ruby.
        #[cfg(feature = "gecko")]
        if old_display.is_ruby_type() != new_display.is_ruby_type() {
            return true;
        }
    }

    // Children with justify-items: auto may depend on our
    // justify-items property value.
    //
    // Similarly, we could potentially do better, but this really
    // seems not common enough to care about.
    #[cfg(feature = "gecko")]
    {
        use crate::values::specified::align::AlignFlags;

        let old_justify_items = *old_values.get_position().get_justify_items();
        let new_justify_items = *new_values.get_position().get_justify_items();

        let was_legacy_justify_items = old_justify_items.computed.contains(AlignFlags::LEGACY);

        let is_legacy_justify_items = new_justify_items.computed.contains(AlignFlags::LEGACY);

        if is_legacy_justify_items != was_legacy_justify_items {
            return true;
        }

        if was_legacy_justify_items && old_justify_items.computed != new_justify_items.computed {
            return true;
        }
    }

    false
}

impl<E: TElement> PrivateMatchMethods for E {}

/// The public API that elements expose for selector matching.
pub trait MatchMethods: TElement {
    /// Returns the closest parent element that doesn't have a display: contents
    /// style (and thus generates a box).
    ///
    /// This is needed to correctly handle blockification of flex and grid
    /// items.
    ///
    /// Returns itself if the element has no parent. In practice this doesn't
    /// happen because the root element is blockified per spec, but it could
    /// happen if we decide to not blockify for roots of disconnected subtrees,
    /// which is a kind of dubious behavior.
    fn layout_parent(&self) -> Self {
        let mut current = *self;
        loop {
            current = match current.traversal_parent() {
                Some(el) => el,
                None => return current,
            };

            let is_display_contents = current
                .borrow_data()
                .unwrap()
                .styles
                .primary()
                .is_display_contents();

            if !is_display_contents {
                return current;
            }
        }
    }

    /// Rather than comparing the resolved line-height, which can be expensive to compute
    /// as it involves locking and font metrics access, we consider that line-height may have
    /// changed if the font-size or line-height property itself has changed, or if the value
    /// is 'normal' and one of the properties that affects font selection (family, style,
    /// weight, width) has changed.
    fn line_height_likely_changed(
        old_style: Option<&Arc<ComputedValues>>,
        new_style: &Arc<ComputedValues>,
    ) -> bool {
        let old_line_height = old_style.map(|s| *s.get_font().get_line_height());
        let new_line_height = *new_style.get_font().get_line_height();
        // Return true if the old value was missing, or if the computed values are different.
        if old_line_height.is_none_or(|lh| lh != new_line_height) {
            return true;
        }
        // If the value isn't `normal`, it doesn't depend on font metrics: return false.
        if !new_line_height.is_normal() {
            return false;
        }
        // Check the font-selection properties, which could affect metrics used to resolve
        // `normal` line-height.
        macro_rules! font_property_changed {
            ($equals: ident) => {
                old_style.is_none_or(|s| !s.get_font().$equals(new_style.get_font()))
            };
        }
        font_property_changed!(font_family_equals)
            || font_property_changed!(font_style_equals)
            || font_property_changed!(font_weight_equals)
            || font_property_changed!(font_width_equals)
    }

    /// Updates the styles with the new ones, diffs them, and stores the restyle
    /// damage.
    fn finish_restyle(
        &self,
        context: &mut StyleContext<Self>,
        data: &mut ElementData,
        mut new_styles: ResolvedElementStyles,
        important_rules_changed: bool,
        before_change_style: Option<Arc<ComputedValues>>,
        animation_only: bool,
    ) -> RestyleHint {
        // Nothing needs to be updated if the element was restyled only because of animations.
        if !animation_only {
            #[cfg(feature = "gecko")]
            self.process_animations(
                context,
                &mut new_styles,
                important_rules_changed,
                before_change_style
                    .as_ref()
                    .or(data.styles.primary.as_ref()),
            );
            #[cfg(feature = "servo")]
            self.process_animations(
                context,
                &mut data.styles,
                &mut new_styles,
                important_rules_changed,
            );
        }

        // First of all, update the styles, and keep the before-change style around for the
        // children, see StyleResolverForElement::before_change_style.
        let mut old_styles = data.set_styles(new_styles);
        if !animation_only {
            data.before_change_style = before_change_style.or_else(|| old_styles.primary.take());
        }
        let old_primary_style = old_styles
            .primary
            .as_ref()
            .or(data.before_change_style.as_ref());

        let new_primary_style = data.styles.primary.as_ref().unwrap();

        let mut child_restyle_hint = RestyleHint::empty();
        let is_root = new_primary_style
            .flags
            .contains(ComputedValueFlags::IS_ROOT_ELEMENT_STYLE);

        let device = context.shared.stylist.device();
        let new_font_size = new_primary_style.get_font().slow_clone_font_size();
        let new_container_type = *new_primary_style.get_container_type();

        let old_font_size = old_primary_style.map(|s| s.get_font().slow_clone_font_size());
        let font_size_changed = old_font_size.is_none_or(|fs| fs != new_font_size);

        let line_height_likely_changed = font_size_changed
            || Self::line_height_likely_changed(old_primary_style, new_primary_style);

        // Update root font-relative units. If any of these unit values changed
        // since last time, ensure that we recascade the entire tree.
        if is_root {
            debug_assert!(self.owner_doc_matches_for_testing(device));
            device.set_root_style(new_primary_style);

            // Update root font size for rem units
            if font_size_changed {
                let size = new_font_size.computed_size();
                device.set_root_font_size(new_primary_style.effective_zoom.unzoom(size.px()));
            }

            // Update root line height for rlh units
            if line_height_likely_changed {
                let new_line_height = device
                    .calc_line_height(
                        new_primary_style.get_font(),
                        new_primary_style.writing_mode,
                        None,
                    )
                    .0;
                device.set_root_line_height(
                    new_primary_style
                        .effective_zoom
                        .unzoom(new_line_height.px()),
                );
            }

            // Update root font metrics for rcap, rch, rex, ric units. Since querying
            // font metrics can be an expensive call, they are only updated if these
            // units are used in the document.
            if device.used_root_font_metrics() && device.update_root_font_metrics() {
                child_restyle_hint |= RestyleHint::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT;
            }
        }

        if font_size_changed || line_height_likely_changed {
            child_restyle_hint |= RestyleHint::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT;
        }

        if context.shared.stylist.quirks_mode() == QuirksMode::Quirks
            && self.is_html_document_body_element()
        {
            // NOTE(emilio): We _could_ handle dynamic changes to it if it
            // changes and before we reach our children the cascade stops,
            // but we don't track right now whether we use the document body
            // color, and nobody else handles that properly anyway.
            let device = context.shared.stylist.device();

            // Needed for the "inherit from body" quirk.
            let text_color = *new_primary_style.get_inherited_text().get_color();
            device.set_body_text_color(text_color);
        }

        // Don't accumulate damage if we're in a forgetful traversal.
        if context
            .shared
            .traversal_flags
            .contains(TraversalFlags::Forgetful)
        {
            return RestyleHint::RECASCADE_SELF;
        }

        // Also, don't do anything if there was no style.
        let Some(old_primary_style) = old_primary_style else {
            return RestyleHint::RECASCADE_SELF;
        };

        // Check for changes in writing mode here because we don't care
        // if the old style didn't exist because that should be resolved
        // when computing the style from scratch.
        if !old_primary_style.writing_mode_equals(new_primary_style) {
            child_restyle_hint |= RestyleHint::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT;
        }

        let old_container_type = *old_primary_style.get_container_type();
        if old_container_type != new_container_type && !new_container_type.is_size_container_type()
        {
            // Stopped being a size container. Re-evaluate container queries and units on all our descendants.
            // Changes into and between different size containment is handled in `UpdateContainerQueryStyles`.
            child_restyle_hint |= RestyleHint::restyle_subtree();
        } else if old_container_type.is_size_container_type()
            && !old_primary_style.is_display_contents()
            && new_primary_style.is_display_contents()
        {
            // Also re-evaluate when a container gets 'display: contents', since size queries will now evaluate to unknown.
            // Other displays like 'inline' will keep generating a box, so they are handled in `UpdateContainerQueryStyles`.
            child_restyle_hint |= RestyleHint::restyle_subtree();
        }

        child_restyle_hint |= self.accumulate_damage_for(
            context.shared,
            &mut data.damage,
            &old_primary_style,
            new_primary_style,
            None,
        );

        if data.styles.pseudos.is_empty() && old_styles.pseudos.is_empty() {
            // This is the common case; no need to examine pseudos here.
            return child_restyle_hint;
        }

        let pseudo_styles = old_styles
            .pseudos
            .as_array()
            .iter()
            .zip(data.styles.pseudos.as_array().iter());

        for (i, (old, new)) in pseudo_styles.enumerate() {
            match (old, new) {
                (Some(old), Some(new)) => {
                    self.accumulate_damage_for(
                        context.shared,
                        &mut data.damage,
                        old,
                        new,
                        Some(&PseudoElement::from_eager_index(i)),
                    );
                },
                (&None, &None) => {},
                _ => {
                    // It's possible that we're switching from not having
                    // ::before/::after at all to having styles for them but not
                    // actually having a useful pseudo-element.  Check for that
                    // case.
                    let pseudo = PseudoElement::from_eager_index(i);
                    let new_pseudo_should_exist =
                        new.as_ref().is_some_and(|s| pseudo.should_exist(s));
                    let old_pseudo_should_exist =
                        old.as_ref().is_some_and(|s| pseudo.should_exist(s));
                    if new_pseudo_should_exist != old_pseudo_should_exist {
                        data.damage |= RestyleDamage::reconstruct();
                        return child_restyle_hint;
                    }
                },
            }
        }

        child_restyle_hint
    }

    /// Updates the rule nodes without re-running selector matching, using just
    /// the rule tree.
    ///
    /// Returns true if an !important rule was replaced.
    fn replace_rules(
        &self,
        replacements: RestyleHint,
        context: &mut StyleContext<Self>,
        cascade_inputs: &mut ElementCascadeInputs,
    ) -> bool {
        let mut result = false;
        result |= self.replace_rules_internal(
            replacements,
            context,
            CascadeVisitedMode::Unvisited,
            cascade_inputs,
        );
        result |= self.replace_rules_internal(
            replacements,
            context,
            CascadeVisitedMode::Visited,
            cascade_inputs,
        );
        result
    }

    /// Given the old and new style of this element, and whether it's a
    /// pseudo-element, compute the restyle damage used to determine which
    /// kind of layout or painting operations we'll need.
    fn compute_style_difference(
        &self,
        old_values: &ComputedValues,
        new_values: &ComputedValues,
        pseudo: Option<&PseudoElement>,
    ) -> StyleDifference {
        debug_assert!(pseudo.is_none_or(|p| p.is_eager()));
        #[cfg(feature = "gecko")]
        {
            RestyleDamage::compute_style_difference(old_values, new_values)
        }
        #[cfg(feature = "servo")]
        {
            RestyleDamage::compute_style_difference::<Self>(old_values, new_values)
        }
    }
}

impl<E: TElement> MatchMethods for E {}
