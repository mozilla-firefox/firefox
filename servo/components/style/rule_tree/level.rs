/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![forbid(unsafe_code)]

//! The cascade level and shadow cascade order for tracking shadow tree rules.

use crate::derives::*;
use crate::dom::{TElement, TNode, TShadowRoot};
use crate::properties::Importance;
use crate::shared_lock::{SharedRwLockReadGuard, StylesheetGuards};
use crate::stylesheets::Origin;

use std::cmp::{Ord, Ordering, PartialOrd};

/// The cascade level these rules are relevant at, as per[1][2][3].
///
/// We store them as a bitfield with:
///
///  * Shadow cascade order (4 bits). See ShadowCascadeOrder for details.
///  * Importance bit. Whether the declaration was !important or not.
///  * The CascadeOrigin of the declaration as per [1].
///
/// Presentational hints for SVG and HTML are in the "author-level zero-specificity" level, that
/// is, right after user rules, and before author rules.
///
/// See also [4] for the Shadow DOM bits. We rely on the invariant that rules
/// from outside the tree the element is in can't affect the element.
///
/// The opposite is not true (i.e., :host and ::slotted) from an "inner" shadow tree may affect an
/// element connected to the document or an "outer" shadow tree.
///
/// [1]: https://drafts.csswg.org/css-cascade/#cascade-origin
/// [2]: https://drafts.csswg.org/css-cascade/#preshint
/// [3]: https://html.spec.whatwg.org/multipage/#presentational-hints
/// [4]: https://drafts.csswg.org/css-scoping/#shadow-cascading
#[repr(C)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    Hash,
    PartialEq,
    Serialize,
    Deserialize,
    ToAnimatedValue,
    ToResolvedValue,
    ToShmem,
)]
pub struct CascadeLevel(u8);
bitflags! {
    impl CascadeLevel: u8 {
        /// The three bits that represent the CascadeOrigin.
        const ORIGIN_BITS = 0b00000111;
        /// Whether the declarations are `!important` or not.
        const IMPORTANT = 1 << 3;
        /// The three bits for cascade order absolute value. If you change this, please change
        /// CASCADE_ORDER_SHIFT accordingly.
        const CASCADE_ORDER_BITS = 0b01110000;
        /// The bit for the sign.
        const CASCADE_ORDER_SIGN = 1 << 7;
    }
}

malloc_size_of::malloc_size_of_is_0!(CascadeLevel);

/// The cascade origin of rules.
///
/// Presentational hints for SVG and HTML are in the "author-level zero-specificity" level, that
/// is, right after user rules, and before author rules.
///
/// The order of variants declared here is significant, and must be in _ascending_ order of
/// precedence.
///
/// [1]: https://drafts.csswg.org/css-cascade/#cascade-origin
/// [2]: https://drafts.csswg.org/css-cascade/#cascade-origin
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    FromPrimitive,
    Hash,
    MallocSizeOf,
    Ord,
    PartialEq,
    PartialOrd,
    Serialize,
    ToAnimatedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum CascadeOrigin {
    /// Normal User-Agent rules.
    UA = 0,
    /// User normal rules.
    User,
    /// Presentational hints.
    PresHints,
    /// Styles from author styles.
    Author,
    /// https://drafts.csswg.org/css-anchor-position-1/#position-fallback-origin
    PositionFallback,
    /// SVG SMIL animations.
    SMILOverride,
    /// CSS animations and script-generated animations.
    Animations,
    /// CSS Transitions
    Transitions,
}

impl CascadeOrigin {
    /// Returns the "simplified" origin.
    #[inline]
    pub fn origin(self) -> Origin {
        match self {
            Self::UA => Origin::UserAgent,
            Self::User => Origin::User,
            _ => Origin::Author,
        }
    }

    /// Select a lock guard for this origin.
    #[inline]
    pub fn guard<'a>(&self, guards: &'a StylesheetGuards<'a>) -> &'a SharedRwLockReadGuard<'a> {
        match *self {
            Self::UA | Self::User => guards.ua_or_user,
            _ => guards.author,
        }
    }
}

/// Whether the transitions level overrides the given important level
fn transitions_override(level: CascadeLevel) -> bool {
    debug_assert!(level.is_important());
    if level.origin() == CascadeOrigin::UA {
        return crate::pref!("layout.css.transitions-override-ua");
    }
    true
}

impl Ord for CascadeLevel {
    fn cmp(&self, other: &Self) -> Ordering {
        let self_important = self.is_important();
        if self_important != other.is_important() {
            return if self_important {
                if other.origin() == CascadeOrigin::Transitions && transitions_override(*self) {
                    return Ordering::Less;
                }
                Ordering::Greater
            } else {
                if self.origin() == CascadeOrigin::Transitions && transitions_override(*other) {
                    return Ordering::Greater;
                }
                Ordering::Less
            };
        }
        let origin_cmp = self
            .origin()
            .cmp(&other.origin())
            .then_with(|| self.shadow_order().cmp(&other.shadow_order()));
        if self_important {
            origin_cmp.reverse()
        } else {
            origin_cmp
        }
    }
}

impl PartialOrd for CascadeLevel {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl CascadeLevel {
    /// The shift that we apply to the cascade order.
    pub const CASCADE_ORDER_SHIFT: usize = 4;
    /// The author level at the same tree. Here for convenience to avoid duplicating it from C++.
    pub const SAME_TREE_AUTHOR_NORMAL: Self = Self(CascadeOrigin::Author as u8);

    /// Returns a cascade level with a given origin.
    pub const fn new(origin: CascadeOrigin) -> Self {
        let bits = origin as u8;
        debug_assert!((bits & Self::ORIGIN_BITS.bits()) == bits);
        Self(bits)
    }

    /// Returns the cascade origin.
    #[inline]
    pub fn origin(self) -> CascadeOrigin {
        use num_traits::FromPrimitive;
        let origin = (self & Self::ORIGIN_BITS).bits();
        CascadeOrigin::from_u8(origin).unwrap()
    }

    /// Convert this level from "unimportant" to "important".
    pub fn important(self) -> Self {
        debug_assert!(
            matches!(
                self.origin(),
                CascadeOrigin::UA | CascadeOrigin::User | CascadeOrigin::Author
            ),
            "{self:?}"
        );
        let mut result = self;
        result.insert(Self::IMPORTANT);
        result
    }

    /// Convert this level from "important" to "non-important".
    pub fn unimportant(self) -> Self {
        let mut result = self;
        result.remove(Self::IMPORTANT);
        result
    }

    /// Select a lock guard for this level
    #[inline]
    pub fn guard<'a>(&self, guards: &'a StylesheetGuards<'a>) -> &'a SharedRwLockReadGuard<'a> {
        self.origin().guard(guards)
    }

    /// Returns the cascade level for author important declarations from the
    /// same tree as the element.
    #[inline]
    pub fn same_tree_author_important() -> Self {
        Self::new(CascadeOrigin::Author).important()
    }

    /// Returns the cascade level for author normal declarations from the same
    /// tree as the element.
    #[inline]
    pub fn same_tree_author_normal() -> Self {
        Self::new(CascadeOrigin::Author)
    }

    /// Returns whether this cascade level represents important rules of some
    /// sort.
    #[inline]
    pub fn is_important(self) -> bool {
        self.intersects(Self::IMPORTANT)
    }

    /// Returns the importance relevant for this rule. Pretty similar to
    /// `is_important`.
    #[inline]
    pub fn importance(self) -> Importance {
        if self.is_important() {
            Importance::Important
        } else {
            Importance::Normal
        }
    }

    /// Returns whether this cascade level represents an animation rules.
    #[inline]
    pub fn is_animation(self) -> bool {
        match self.origin() {
            CascadeOrigin::SMILOverride
            | CascadeOrigin::Animations
            | CascadeOrigin::Transitions => true,
            _ => false,
        }
    }

    /// Returns whether this cascade level is tree.
    #[inline]
    pub fn is_tree(self) -> bool {
        self.origin() == CascadeOrigin::Author
    }

    /// Returns the shadow tree order.
    #[inline]
    pub fn shadow_order(self) -> ShadowCascadeOrder {
        let neg = self.intersects(Self::CASCADE_ORDER_SIGN);
        let abs = (self & Self::CASCADE_ORDER_BITS).bits() >> Self::CASCADE_ORDER_SHIFT;
        ShadowCascadeOrder(if neg { -(abs as i8) } else { abs as i8 })
    }

    /// Returns an author normal cascade level with the given shadow cascade order.
    #[inline]
    pub fn author_normal(shadow_cascade_order: ShadowCascadeOrder) -> Self {
        let abs = shadow_cascade_order.0.unsigned_abs() << Self::CASCADE_ORDER_SHIFT;
        let mut result = Self::new(CascadeOrigin::Author);
        result |= Self::from_bits_truncate(abs);
        result.set(Self::CASCADE_ORDER_SIGN, shadow_cascade_order.0 < 0);
        result
    }

    /// Get the shadow root corresponding to the shadow cascade order, from the given element.
    pub fn get_shadow_root_for_scoped<E: TElement>(
        &self,
        element: E,
    ) -> Option<<<E as TElement>::ConcreteNode as TNode>::ConcreteShadowRoot> {
        let mut cascade_order = self.shadow_order().0;
        if cascade_order < 0 {
            let element = element.ultimate_originating_element();
            let mut slot = element.assigned_slot();
            while let Some(s) = slot {
                cascade_order += 1;
                if cascade_order == 0 {
                    return s.containing_shadow();
                }
                slot = s.assigned_slot();
            }
            if cascade_order != -1 {
                return None;
            }

            return element.shadow_root();
        }
        debug_assert!(cascade_order >= 0);
        let mut containing_shadow = element.containing_shadow();
        while let Some(s) = containing_shadow {
            if cascade_order == 0 {
                break;
            }
            cascade_order -= 1;
            containing_shadow = s.host().containing_shadow();
        }
        containing_shadow
    }
}

/// A counter to track how many shadow root rules deep we are. This is used to
/// handle:
///
/// https://drafts.csswg.org/css-scoping/#shadow-cascading
///
/// See the static functions for the meaning of different values.
#[derive(Clone, Copy, Debug, Eq, Hash, MallocSizeOf, Ord, PartialEq, PartialOrd)]
#[repr(transparent)]
pub struct ShadowCascadeOrder(i8);

impl ShadowCascadeOrder {
    /// We keep a maximum of 3 bits of order as a limit so that we can pack CascadeLevel in 1 byte.
    const MAX: i8 = 0b111;
    const MIN: i8 = -Self::MAX;

    /// A level for the outermost shadow tree (the shadow tree we own, and the
    /// ones from the slots we're slotted in).
    #[inline]
    pub fn for_outermost_shadow_tree() -> Self {
        Self(-1)
    }

    /// A level for the element's tree.
    #[inline]
    pub fn for_same_tree() -> Self {
        Self(0)
    }

    /// A level for the innermost containing tree (the one closest to the
    /// element).
    #[inline]
    pub fn for_innermost_containing_tree() -> Self {
        Self(1)
    }

    /// Returns true if the level is in the same or a containing shadow tree.
    #[inline]
    pub fn is_in_same_or_containing_tree(&self) -> bool {
        self.0 > 0
    }

    /// Decrement the level, moving inwards. We should only move inwards if
    /// we're traversing slots.
    #[inline]
    pub fn dec(&mut self) {
        debug_assert!(self.0 < 0);
        if self.0 != Self::MIN {
            self.0 -= 1;
        }
    }

    /// Increment the level, moving outwards.
    #[inline]
    pub fn inc(&mut self) {
        if self.0 != Self::MAX {
            self.0 += 1;
        }
    }
}

impl std::ops::Neg for ShadowCascadeOrder {
    type Output = Self;
    #[inline]
    fn neg(self) -> Self {
        Self(self.0.neg())
    }
}
