/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Applicable declarations management.

use crate::derives::*;
use crate::rule_tree::{CascadeLevel, CascadeOrigin, RuleCascadeFlags, StyleSourceBorrow};
use crate::stylesheets::layer_rule::LayerOrder;
use smallvec::SmallVec;

/// List of applicable declarations. This is a transient structure that shuttles
/// declarations between selector matching and inserting into the rule tree, and
/// therefore we want to avoid heap-allocation where possible.
///
/// In measurements on wikipedia, we pretty much never have more than 8 applicable
/// declarations, so we could consider making this 8 entries instead of 16.
/// However, it may depend a lot on workload, and stack space is cheap.
pub type ApplicableDeclarationList<'a> = SmallVec<[ApplicableDeclarationBlock<'a>; 16]>;

/// Blink uses 18 bits to store source order, and does not check overflow [1].
/// That's a limit that could be reached in realistic webpages, so we use
/// 24 bits and enforce defined behavior in the overflow case.
///
/// Note that right now this restriction could be lifted if wanted (because we
/// no longer stash the cascade level in the remaining bits), but we keep it in
/// place in case we come up with a use-case for them, lacking reports of the
/// current limit being too small.
///
/// [1] https://cs.chromium.org/chromium/src/third_party/WebKit/Source/core/css/
///     RuleSet.h?l=128&rcl=90140ab80b84d0f889abc253410f44ed54ae04f3
const SOURCE_ORDER_BITS: usize = 24;
const SOURCE_ORDER_MAX: u32 = (1 << SOURCE_ORDER_BITS) - 1;
const SOURCE_ORDER_MASK: u32 = SOURCE_ORDER_MAX;

/// The cascade-level+layer order of this declaration.
#[derive(Clone, Copy, Debug, Eq, Hash, MallocSizeOf, PartialEq)]
pub struct CascadePriority {
    cascade_level: CascadeLevel,
    flags: RuleCascadeFlags,
    layer_order: LayerOrder,
}

const_assert_eq!(
    std::mem::size_of::<CascadePriority>(),
    std::mem::size_of::<u32>()
);

impl PartialOrd for CascadePriority {
    #[inline]
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for CascadePriority {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.cascade_level.cmp(&other.cascade_level).then_with(|| {
            let ordering = self.layer_order.cmp(&other.layer_order);
            if ordering == std::cmp::Ordering::Equal {
                return ordering;
            }
            // https://drafts.csswg.org/css-cascade-5/#cascade-layering
            //
            //     Cascade layers (like declarations) are ordered by order
            //     of appearance. When comparing declarations that belong to
            //     different layers, then for normal rules the declaration
            //     whose cascade layer is last wins, and for important rules
            //     the declaration whose cascade layer is first wins.
            //
            // But the style attribute layer for some reason is special.
            if self.cascade_level.is_important()
                && !self.layer_order.is_style_attribute_layer()
                && !other.layer_order.is_style_attribute_layer()
            {
                ordering.reverse()
            } else {
                ordering
            }
        })
    }
}

/// The kind of revert that we're applying.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum RevertKind {
    /// revert
    Origin,
    /// revert-layer
    Layer,
    /// revert-rule
    Rule,
}

impl CascadePriority {
    /// The transitions cascade priority. There's only one transition, and it's unassociated with any layer or so.
    pub const TRANSITIONS: Self = Self::new(
        CascadeLevel::new(CascadeOrigin::Transitions),
        LayerOrder::root(),
        RuleCascadeFlags::empty(),
    );

    /// Same but for animations.
    pub const ANIMATIONS: Self = Self::new(
        CascadeLevel::new(CascadeOrigin::Animations),
        LayerOrder::root(),
        RuleCascadeFlags::empty(),
    );

    /// Same but for SMIL.
    pub const SMIL: Self = Self::new(
        CascadeLevel::new(CascadeOrigin::SMILOverride),
        LayerOrder::root(),
        RuleCascadeFlags::empty(),
    );

    /// Same but for pres hints.
    pub const PRES_HINTS: Self = Self::new(
        CascadeLevel::new(CascadeOrigin::PresHints),
        LayerOrder::root(),
        RuleCascadeFlags::empty(),
    );

    /// Construct a new CascadePriority for a given (level, order, flags) triple.
    pub const fn new(
        cascade_level: CascadeLevel,
        layer_order: LayerOrder,
        flags: RuleCascadeFlags,
    ) -> Self {
        Self {
            cascade_level,
            flags,
            layer_order,
        }
    }

    /// Returns the flags.
    #[inline]
    pub fn flags(&self) -> RuleCascadeFlags {
        self.flags
    }

    /// Set given flags.
    #[inline]
    pub fn set_flags(&mut self, flags: RuleCascadeFlags) {
        self.flags.insert(flags);
    }

    /// Returns the layer order.
    #[inline]
    pub fn layer_order(&self) -> LayerOrder {
        self.layer_order
    }

    /// Returns the cascade level.
    #[inline]
    pub fn cascade_level(&self) -> CascadeLevel {
        self.cascade_level
    }

    /// Whether this declaration should be allowed if `revert` / `revert-layer` / `revert-rule`
    /// have been specified on a given origin.
    ///
    /// `self` is the priority at which the revert has been specified.
    pub fn allows_when_reverted(&self, other: &Self, kind: RevertKind) -> bool {
        match kind {
            RevertKind::Origin => {
                other.cascade_level.origin().origin() < self.cascade_level.origin().origin()
            },
            RevertKind::Layer => other.unimportant() < self.unimportant(),
            // Any other declaration for the same property we apply in the cascade needs to come
            // from another rule effectively.
            RevertKind::Rule => true,
        }
    }

    /// Convert this priority from "important" to "non-important", if needed.
    pub fn unimportant(&self) -> Self {
        Self {
            cascade_level: self.cascade_level.unimportant(),
            flags: self.flags,
            layer_order: self.layer_order,
        }
    }

    /// Whether this cascade priority is important or not.
    #[inline]
    pub fn is_important(&self) -> bool {
        self.cascade_level.is_important()
    }

    /// Convert this priority from "non-important" to "important", if needed.
    pub fn important(&self) -> Self {
        Self {
            cascade_level: self.cascade_level.important(),
            flags: self.flags,
            layer_order: self.layer_order,
        }
    }

    /// The same tree, in author origin, at the root layer.
    pub fn same_tree_author_normal_at_root_layer() -> Self {
        Self::new(
            CascadeLevel::same_tree_author_normal(),
            LayerOrder::root(),
            RuleCascadeFlags::empty(),
        )
    }
}

/// Proximity to the scope root.
///
/// https://drafts.csswg.org/css-cascade-6/#cascade-proximity
#[derive(Clone, Copy, Debug, Eq, Hash, MallocSizeOf, PartialEq)]
pub struct ScopeProximity(u16);

impl PartialOrd for ScopeProximity {
    #[inline]
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for ScopeProximity {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        // Lower proximity to scope root wins
        other.0.cmp(&self.0)
    }
}

/// Sacrifice the largest possible value for infinity. This makes the comparison
/// trivial.
const PROXIMITY_INFINITY: u16 = u16::MAX;

impl ScopeProximity {
    /// Construct a new scope proximity.
    pub fn new(proximity: usize) -> Self {
        if cfg!(debug_assertions) && proximity >= PROXIMITY_INFINITY as usize {
            warn!("Proximity out of bounds");
        }
        Self(proximity.clamp(0, (PROXIMITY_INFINITY - 1) as usize) as u16)
    }

    /// Create a scope proximity for delcarations outside of any scope root.
    pub fn infinity() -> Self {
        Self(PROXIMITY_INFINITY)
    }

    /// If the proximity is finite, get the value.
    pub fn get(&self) -> Option<u16> {
        (self.0 != PROXIMITY_INFINITY).then_some(self.0)
    }
}

/// A property declaration together with its precedence among rules of equal
/// specificity so that we can sort them.
///
/// This represents the declarations in a given declaration block for a given
/// importance.
#[derive(Clone, Copy, Debug, MallocSizeOf, PartialEq)]
pub struct ApplicableDeclarationBlock<'a> {
    /// The style source.
    pub source: StyleSourceBorrow<'a>,
    /// Order of appearance in which this rule appears - Set to 0 if not relevant
    /// (e.g. Declaration from `style="/*...*/"`, presentation hints, animations
    /// - See `CascadePriority` instead).
    source_order: u32,
    /// The specificity of the selector.
    pub specificity: u32,
    /// The proximity to the scope root.
    pub scope_proximity: ScopeProximity,
    /// The cascade priority of the rule.
    pub cascade_priority: CascadePriority,
}

impl<'a> ApplicableDeclarationBlock<'a> {
    /// Constructs an applicable declaration block from a given property declaration block borrow
    /// and importance.
    #[inline]
    pub fn from_declarations(
        source: StyleSourceBorrow<'a>,
        level: CascadeLevel,
        layer_order: LayerOrder,
    ) -> Self {
        Self::new(
            source,
            /* source_order = */ 0,
            level,
            /* specificity = */ 0,
            layer_order,
            ScopeProximity::infinity(),
            RuleCascadeFlags::empty(),
        )
    }

    /// Constructs an applicable declaration block from the given components.
    #[inline]
    pub fn new(
        source: StyleSourceBorrow<'a>,
        source_order: u32,
        level: CascadeLevel,
        specificity: u32,
        layer_order: LayerOrder,
        scope_proximity: ScopeProximity,
        flags: RuleCascadeFlags,
    ) -> Self {
        Self {
            source,
            source_order: source_order & SOURCE_ORDER_MASK,
            specificity,
            scope_proximity,
            cascade_priority: CascadePriority::new(level, layer_order, flags),
        }
    }

    /// Returns the source order of the block.
    #[inline]
    pub fn source_order(&self) -> u32 {
        self.source_order
    }

    /// Returns the cascade level of the block.
    #[inline]
    pub fn level(&self) -> CascadeLevel {
        self.cascade_priority.cascade_level()
    }

    /// Returns the cascade level of the block.
    #[inline]
    pub fn layer_order(&self) -> LayerOrder {
        self.cascade_priority.layer_order()
    }

    /// Returns the scope proximity of the block.
    #[inline]
    pub fn scope_proximity(&self) -> ScopeProximity {
        self.scope_proximity
    }

    /// Convenience method to consume self and return the right thing for the
    /// rule tree to iterate over.
    #[inline]
    pub fn for_rule_tree(self) -> (StyleSourceBorrow<'a>, CascadePriority) {
        (self.source, self.cascade_priority)
    }

    /// Return the key used to sort applicable declarations.
    #[inline]
    pub fn sort_key(&self) -> (LayerOrder, u32, ScopeProximity, u32) {
        (
            self.layer_order(),
            self.specificity,
            self.scope_proximity(),
            self.source_order(),
        )
    }
}

// Size of this struct determines sorting and selector-matching performance.
size_of_test!(ApplicableDeclarationBlock<'static>, 24);
