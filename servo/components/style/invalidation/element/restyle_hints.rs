/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Restyle hints: an optimization to avoid unnecessarily matching selectors.

bitflags! {
    /// The kind of restyle we need to do for a given element.
    #[repr(C)]
    #[derive(Clone, Copy, Debug)]
    pub struct RestyleHint: u16 {
        /// Do a selector match of the element.
        const RESTYLE_SELF = 1 << 0;

        /// Do a selector match of the element's pseudo-elements. Always to be combined with
        /// RESTYLE_SELF.
        const RESTYLE_PSEUDOS = 1 << 1;

        /// Do a selector match if the element is a pseudo-element.
        const RESTYLE_SELF_IF_PSEUDO = 1 << 2;

        /// Do a selector match of the element's descendants.
        const RESTYLE_DESCENDANTS = 1 << 3;

        /// Recascade the current element.
        const RECASCADE_SELF = 1 << 4;

        /// Recascade the current element if it inherits any reset style.
        const RECASCADE_SELF_IF_INHERIT_RESET_STYLE = 1 << 5;

        /// Recascade all descendant elements.
        const RECASCADE_DESCENDANTS = 1 << 6;

        /// Replace the style data coming from CSS transitions without updating
        /// any other style data.
        const RESTYLE_CSS_TRANSITIONS = 1 << 7;

        /// Replace the style data coming from CSS animations without updating
        /// any other style data.
        const RESTYLE_CSS_ANIMATIONS = 1 << 8;

        /// Don't re-run selector-matching on the element, only the style
        /// attribute has changed, and this change didn't have any other
        /// dependencies.
        const RESTYLE_STYLE_ATTRIBUTE = 1 << 9;

        /// Replace the style data coming from SMIL animations without updating
        /// any other style data.
        const RESTYLE_SMIL = 1 << 10;

        /// Match self if this element is dependent on a style query.
        const RESTYLE_IF_AFFECTED_BY_STYLE_QUERIES = 1 << 11;

        /// Match self or descendants if dependent on a named style query.
        const RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER = 1 << 12;

        /// Do a selector match of the element if it depends on an ancestor's font
        /// or this element's writing mode. Used to handle the invalidation cases
        /// for style container queries in which relative font metrics change or
        /// when a writing-mode-dependant unit needs to be updated for a style
        /// container query.
        const RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT = 1 << 13;

        /// We don't need to match this element but do a selector match of it's
        /// children if they are affected by style container queries.
        const RESTYLE_CHILD_IF_AFFECTED_BY_STYLE_QUERIES = 1 <<14;

        /// Replace presentational hints without re-running selector matching.
        const RESTYLE_PRES_HINTS = 1 << 15;
    }
}

impl RestyleHint {
    /// Creates a new `RestyleHint` indicating that the current element and all
    /// its descendants must be fully restyled.
    #[inline]
    pub fn restyle_subtree() -> Self {
        RestyleHint::RESTYLE_SELF | RestyleHint::RESTYLE_DESCENDANTS
    }

    /// Creates a new `RestyleHint` indicating that the current element and all
    /// its descendants must be recascaded.
    #[inline]
    pub fn recascade_subtree() -> Self {
        RestyleHint::RECASCADE_SELF | RestyleHint::RECASCADE_DESCENDANTS
    }

    /// Returns whether this hint invalidates the element and all its
    /// descendants.
    #[inline]
    pub fn contains_subtree(&self) -> bool {
        self.contains(Self::restyle_subtree())
    }

    /// Returns whether we'll recascade all of the descendants.
    #[inline]
    pub fn will_recascade_subtree(&self) -> bool {
        self.contains_subtree() || self.contains(Self::recascade_subtree())
    }

    /// Returns whether we need to restyle this element.
    pub fn has_non_animation_invalidations(&self) -> bool {
        !(*self & !Self::for_animations()).is_empty()
    }

    /// Clears this restyle hint, and returns the hint to propagate to child elements.
    pub fn propagate(&mut self) -> Self {
        std::mem::take(self).propagate_to_children()
    }

    /// Returns a new `RestyleHint` appropriate for children of the current element.
    fn propagate_to_children(&self) -> Self {
        if self.contains(RestyleHint::RESTYLE_DESCENDANTS) {
            return Self::restyle_subtree();
        }
        let mut result = Self::empty();
        if self.contains(Self::RESTYLE_PSEUDOS) {
            result |= Self::RESTYLE_SELF_IF_PSEUDO;
        }
        if self.contains(Self::RECASCADE_DESCENDANTS) {
            result |= Self::recascade_subtree();
        }
        if self.contains(Self::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT) {
            result |= Self::RESTYLE_IF_AFFECTED_BY_WM_OR_ANCESTOR_FONT;
        }
        if self.contains(Self::RESTYLE_CHILD_IF_AFFECTED_BY_STYLE_QUERIES) {
            result |= Self::RESTYLE_IF_AFFECTED_BY_STYLE_QUERIES;
        }
        if self.contains(Self::RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER) {
            // We may need to restyle further down the tree if rules are
            // declared for a named container.
            // e.g @container my-name {#b {...}}
            // and <div id=a> <div> <div id=b> </div> </div> </div>
            // If a toggles `container-name: my-name` the rules for #b
            // also invalidate. This is why we need one hint for unnamed
            // container and one for named containers.
            result |= Self::RESTYLE_IF_AFFECTED_BY_NAMED_STYLE_CONTAINER;
        }
        result
    }

    /// Returns a hint that contains all the replacement hints.
    pub fn replacements() -> Self {
        RestyleHint::RESTYLE_STYLE_ATTRIBUTE
            | RestyleHint::RESTYLE_PRES_HINTS
            | Self::for_animations()
    }

    /// The replacements for the animation cascade levels.
    #[inline]
    pub fn for_animations() -> Self {
        RestyleHint::RESTYLE_SMIL
            | RestyleHint::RESTYLE_CSS_ANIMATIONS
            | RestyleHint::RESTYLE_CSS_TRANSITIONS
    }

    /// Returns whether the hint specifies that an animation cascade level must
    /// be replaced.
    #[inline]
    pub fn has_animation_hint(&self) -> bool {
        self.intersects(Self::for_animations())
    }

    /// The hints to recascade the current element.
    #[inline]
    pub fn recascade_self() -> Self {
        Self::RECASCADE_SELF | Self::RECASCADE_SELF_IF_INHERIT_RESET_STYLE
    }

    /// Returns whether the hint specifies that some cascade levels must be
    /// replaced.
    #[inline]
    pub fn has_replacements(&self) -> bool {
        self.intersects(Self::replacements())
    }
}

impl Default for RestyleHint {
    fn default() -> Self {
        Self::empty()
    }
}

#[cfg(feature = "servo")]
malloc_size_of::malloc_size_of_is_0!(RestyleHint);
