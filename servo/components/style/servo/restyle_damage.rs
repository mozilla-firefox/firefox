/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The restyle damage is a hint that tells layout which kind of operations may
//! be needed in presence of incremental style changes.

use bitflags::Flags;

use crate::computed_values::isolation::T as Isolation;
use crate::computed_values::mix_blend_mode::T as MixBlendMode;
use crate::computed_values::transform_style::T as TransformStyle;
use crate::dom::TElement;
use crate::matching::{StyleChange, StyleDifference};
use crate::properties::{
    restyle_damage_rebuild_box, restyle_damage_rebuild_stacking_context,
    restyle_damage_recalculate_overflow, restyle_damage_repaint, style_structs, ComputedValues,
};
use crate::values::computed::basic_shape::ClipPath;
use crate::values::computed::Perspective;
use crate::values::generics::transform::{GenericRotate, GenericScale, GenericTranslate};
use std::fmt;

bitflags! {
    /// Major phases of layout that need to be run due to the damage to a node during restyling. In
    /// addition to the 4 bytes used for that, the rest of the `u16` is exposed as an extension point
    /// for users of the crate to add their own custom types of damage that correspond to the
    /// layout system they are implementing.
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub struct ServoRestyleDamage: u16 {
        /// Repaint the node itself.
        ///
        /// Propagates both up and down the flow tree.
        const REPAINT = 0b0001;

        /// Rebuilds the stacking contexts.
        ///
        /// Propagates both up and down the flow tree.
        const REBUILD_STACKING_CONTEXT = 0b0011;

        /// Recalculates the scrollable overflow.
        ///
        /// Propagates both up and down the flow tree.
        const RECALCULATE_OVERFLOW = 0b0111;

        /// Any other type of damage, which requires running layout again.
        ///
        /// Propagates both up and down the flow tree.
        const RELAYOUT = 0b1111;
    }
}

malloc_size_of::malloc_size_of_is_0!(ServoRestyleDamage);

impl ServoRestyleDamage {
    /// Compute the `StyleDifference` (including the appropriate restyle damage)
    /// for a given style change between `old` and `new`.
    pub fn compute_style_difference<E: TElement>(
        old: &ComputedValues,
        new: &ComputedValues,
    ) -> StyleDifference {
        if std::ptr::eq(old, new) {
            return StyleDifference {
                damage: ServoRestyleDamage::empty(),
                change: StyleChange::Unchanged,
            };
        }

        let mut damage = compute_damage(old, new);
        if damage.contains(ServoRestyleDamage::RELAYOUT) {
            damage |= E::compute_layout_damage(old, new);
        }

        let mut any_style_changed = !damage.is_empty();
        // FIXME(emilio): Differentiate between reset and inherited
        // properties here, and set `reset_only` appropriately so the
        // optimization to skip the cascade in those cases applies.
        let mut reset_only = !any_style_changed;

        let old_custom_props = old.custom_properties();
        let new_custom_props = new.custom_properties();
        let mut custom_properties_changed = false;
        if old_custom_props.inherited != new_custom_props.inherited {
            any_style_changed = true;
            custom_properties_changed = true;
            reset_only = false;
        } else if old_custom_props.non_inherited != new_custom_props.non_inherited {
            any_style_changed = true;
            custom_properties_changed = true;
        };
        if custom_properties_changed {
            // Paint worklets may depend on custom properties, so if they have changed we should repaint.
            damage.insert(ServoRestyleDamage::REPAINT);
        }

        let change = if any_style_changed {
            StyleChange::Changed {
                reset_only,
                custom_properties_changed,
            }
        } else {
            StyleChange::Unchanged
        };

        StyleDifference { damage, change }
    }

    /// Returns a bitmask indicating that the frame needs to be reconstructed.
    pub fn reconstruct() -> ServoRestyleDamage {
        // There's no way of knowing what kind of damage system the embedder will use, but part of
        // this interface is that a fully saturated restyle damage means to rebuild everything.
        ServoRestyleDamage::from_bits_retain(<ServoRestyleDamage as Flags>::Bits::MAX)
    }
}

impl Default for ServoRestyleDamage {
    fn default() -> Self {
        Self::empty()
    }
}

impl fmt::Display for ServoRestyleDamage {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        let mut first_elem = true;

        let to_iter = [
            (ServoRestyleDamage::REPAINT, "Repaint"),
            (
                ServoRestyleDamage::REBUILD_STACKING_CONTEXT,
                "Rebuild stacking context",
            ),
            (
                ServoRestyleDamage::RECALCULATE_OVERFLOW,
                "Recalculate overflow",
            ),
            (ServoRestyleDamage::RELAYOUT, "Relayout"),
        ];

        for &(damage, damage_str) in &to_iter {
            if self.contains(damage) {
                if !first_elem {
                    write!(f, " | ")?;
                }
                write!(f, "{}", damage_str)?;
                first_elem = false;
            }
        }

        if first_elem {
            write!(f, "NoDamage")?;
        }

        Ok(())
    }
}

fn augmented_restyle_damage_rebuild_box(old: &ComputedValues, new: &ComputedValues) -> bool {
    let old_box = old.get_box();
    let new_box = new.get_box();
    restyle_damage_rebuild_box(old, new)
        || old_box.original_display != new_box.original_display
        || old_box.has_transform_or_perspective() != new_box.has_transform_or_perspective()
        || old.get_effects().filter.0.is_empty() != new.get_effects().filter.0.is_empty()
}

fn augmented_restyle_damage_rebuild_stacking_context(
    old: &ComputedValues,
    new: &ComputedValues,
) -> bool {
    restyle_damage_rebuild_stacking_context(old, new)
        || old.guarantees_stacking_context() != new.guarantees_stacking_context()
}

fn compute_damage(old: &ComputedValues, new: &ComputedValues) -> ServoRestyleDamage {
    // Damage flags higher up the if-else chain imply damage flags lower down the if-else chain,
    // so we can skip the diffing process for later flags if an earlier flag is true
    if augmented_restyle_damage_rebuild_box(old, new) {
        ServoRestyleDamage::RELAYOUT
    } else if restyle_damage_recalculate_overflow(old, new) {
        ServoRestyleDamage::RECALCULATE_OVERFLOW
    } else if augmented_restyle_damage_rebuild_stacking_context(old, new) {
        ServoRestyleDamage::REBUILD_STACKING_CONTEXT
    } else if restyle_damage_repaint(old, new) {
        ServoRestyleDamage::REPAINT
    } else {
        ServoRestyleDamage::empty()
    }
}

impl ComputedValues {
    /// Some properties establish a stacking context when they are set to a non-initial value.
    /// In that case, the damage is only set to `ServoRestyleDamage::REPAINT` because we don't
    /// need to rebuild stacking contexts when the style changes between different non-initial
    /// values. This function checks whether any of these properties is set to a value that
    /// guarantees a stacking context, so that we only do the work when this changes.
    /// Note that it's still possible to establish a stacking context when this returns false.
    pub fn guarantees_stacking_context(&self) -> bool {
        self.get_effects().opacity != 1.0
            || self.get_effects().mix_blend_mode != MixBlendMode::Normal
            || self.get_svg().clip_path != ClipPath::None
            || self.get_box().isolation == Isolation::Isolate
    }
}

impl style_structs::Box {
    /// Whether there is a non-default transform or perspective style set
    pub fn has_transform_or_perspective(&self) -> bool {
        !self.transform.0.is_empty()
            || self.scale != GenericScale::None
            || self.rotate != GenericRotate::None
            || self.translate != GenericTranslate::None
            || self.perspective != Perspective::None
            || self.transform_style == TransformStyle::Preserve3d
    }
}
