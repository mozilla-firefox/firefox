/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! DOM types to be shared between Rust and C++.

use bitflags::bitflags;
use malloc_size_of::malloc_size_of_is_0;

pub const HEADING_LEVEL_OFFSET: usize = 57;

bitflags! {
    /// Event-based element states.
    #[repr(C)]
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub struct ElementState: u64 {
        /// The mouse is down on this element.
        /// <https://html.spec.whatwg.org/multipage/#selector-active>
        /// FIXME(#7333): set/unset this when appropriate
        const ACTIVE = 1 << 0;
        /// This element has focus.
        /// <https://html.spec.whatwg.org/multipage/#selector-focus>
        const FOCUS = 1 << 1;
        /// The mouse is hovering over this element.
        /// <https://html.spec.whatwg.org/multipage/#selector-hover>
        const HOVER = 1 << 2;
        /// Content is enabled (and can be disabled).
        /// <http://www.whatwg.org/html/#selector-enabled>
        const ENABLED = 1 << 3;
        /// Content is disabled.
        /// <http://www.whatwg.org/html/#selector-disabled>
        const DISABLED = 1 << 4;
        /// Content is checked.
        /// <https://html.spec.whatwg.org/multipage/#selector-checked>
        const CHECKED = 1 << 5;
        /// <https://html.spec.whatwg.org/multipage/#selector-indeterminate>
        const INDETERMINATE = 1 << 6;
        /// <https://html.spec.whatwg.org/multipage/#selector-placeholder-shown>
        const PLACEHOLDER_SHOWN = 1 << 7;
        /// <https://html.spec.whatwg.org/multipage/#selector-target>
        const URLTARGET = 1 << 8;
        /// <https://fullscreen.spec.whatwg.org/#%3Afullscreen-pseudo-class>
        const FULLSCREEN = 1 << 9;
        /// <https://html.spec.whatwg.org/multipage/#selector-valid>
        const VALID = 1 << 10;
        /// <https://html.spec.whatwg.org/multipage/#selector-invalid>
        const INVALID = 1 << 11;
        /// <https://drafts.csswg.org/selectors-4/#user-valid-pseudo>
        const USER_VALID = 1 << 12;
        /// <https://drafts.csswg.org/selectors-4/#user-invalid-pseudo>
        const USER_INVALID = 1 << 13;
        /// All the validity bits at once.
        const VALIDITY_STATES = Self::VALID.bits() | Self::INVALID.bits() | Self::USER_VALID.bits() | Self::USER_INVALID.bits();
        /// Non-standard: https://developer.mozilla.org/en-US/docs/Web/CSS/:-moz-broken
        const BROKEN = 1 << 14;
        /// <https://html.spec.whatwg.org/multipage/#selector-required>
        const REQUIRED = 1 << 15;
        /// <https://html.spec.whatwg.org/multipage/#selector-optional>
        /// We use an underscore to workaround a silly windows.h define.
        const OPTIONAL_ = 1 << 16;
        /// <https://html.spec.whatwg.org/multipage/#selector-defined>
        const DEFINED = 1 << 17;
        /// <https://html.spec.whatwg.org/multipage/#selector-visited>
        const VISITED = 1 << 18;
        /// <https://html.spec.whatwg.org/multipage/#selector-link>
        const UNVISITED = 1 << 19;
        /// <https://drafts.csswg.org/selectors-4/#the-any-link-pseudo>
        const VISITED_OR_UNVISITED = Self::VISITED.bits() | Self::UNVISITED.bits();
        /// Non-standard: https://developer.mozilla.org/en-US/docs/Web/CSS/:-moz-drag-over
        const DRAGOVER = 1 << 20;
        /// <https://html.spec.whatwg.org/multipage/#selector-in-range>
        const INRANGE = 1 << 21;
        /// <https://html.spec.whatwg.org/multipage/#selector-out-of-range>
        const OUTOFRANGE = 1 << 22;
        /// <https://html.spec.whatwg.org/multipage/#selector-read-only>
        const READONLY = 1 << 23;
        /// <https://html.spec.whatwg.org/multipage/#selector-read-write>
        const READWRITE = 1 << 24;
        /// <https://html.spec.whatwg.org/multipage/#selector-default>
        const DEFAULT = 1 << 25;
        /// Non-standard & undocumented.
        const OPTIMUM = 1 << 26;
        /// Non-standard & undocumented.
        const SUB_OPTIMUM = 1 << 27;
        /// Non-standard & undocumented.
        const SUB_SUB_OPTIMUM = 1 << 28;
        /// All the above <meter> bits in one place.
        const METER_OPTIMUM_STATES = Self::OPTIMUM.bits() | Self::SUB_OPTIMUM.bits() | Self::SUB_SUB_OPTIMUM.bits();
        /// Non-standard & undocumented.
        const INCREMENT_SCRIPT_LEVEL = 1 << 29;
        /// <https://drafts.csswg.org/selectors-4/#the-focus-visible-pseudo>
        const FOCUSRING = 1 << 30;
        /// <https://drafts.csswg.org/selectors-4/#the-focus-within-pseudo>
        const FOCUS_WITHIN = 1u64 << 31;
        /// :dir matching; the states are used for dynamic change detection.
        /// State that elements that match :dir(ltr) are in.
        const LTR = 1u64 << 32;
        /// State that elements that match :dir(rtl) are in.
        const RTL = 1u64 << 33;
        /// State that HTML elements that have a "dir" attr are in.
        const HAS_DIR_ATTR = 1u64 << 34;
        /// State that HTML elements with dir="ltr" (or something
        /// case-insensitively equal to "ltr") are in.
        const HAS_DIR_ATTR_LTR = 1u64 << 35;
        /// State that HTML elements with dir="rtl" (or something
        /// case-insensitively equal to "rtl") are in.
        const HAS_DIR_ATTR_RTL = 1u64 << 36;
        /// State that HTML <bdi> elements without a valid-valued "dir" attr or
        /// any HTML elements (including <bdi>) with dir="auto" (or something
        /// case-insensitively equal to "auto") are in.
        const HAS_DIR_ATTR_LIKE_AUTO = 1u64 << 37;
        /// Non-standard & undocumented.
        const AUTOFILL = 1u64 << 38;
        /// Non-standard & undocumented.
        const AUTOFILL_PREVIEW = 1u64 << 39;
        /// State for modal elements:
        /// <https://drafts.csswg.org/selectors-4/#modal-state>
        const MODAL = 1u64 << 40;
        /// <https://html.spec.whatwg.org/multipage/#inert-subtrees>
        const INERT = 1u64 << 41;
        /// State for the topmost modal element in top layer
        const TOPMOST_MODAL = 1u64 << 42;
        /// Initially used for the devtools highlighter, but now somehow only
        /// used for the devtools accessibility inspector.
        const DEVTOOLS_HIGHLIGHTED = 1u64 << 43;
        /// Used for the devtools style editor. Probably should go away.
        const STYLEEDITOR_TRANSITIONING = 1u64 << 44;
        /// For :-moz-value-empty (to show widgets like the reveal password
        /// button or the clear button).
        const VALUE_EMPTY = 1u64 << 45;
        /// For :-moz-revealed.
        const REVEALED = 1u64 << 46;
        /// https://html.spec.whatwg.org/#selector-popover-open
        /// Match element's popover visibility state of showing
        const POPOVER_OPEN = 1u64 << 47;
        /// https://drafts.csswg.org/css-scoping-1/#the-has-slotted-pseudo
        /// Match whether a slot element has assigned nodes
        const HAS_SLOTTED = 1u64 << 48;
        /// https://drafts.csswg.org/selectors-4/#open-state
        /// Match whether an openable element is currently open
        const OPEN = 1u64 << 49;
        /// For :active-view-transition.
        /// <https://www.w3.org/TR/css-view-transitions-2/#the-active-view-transition-pseudo>
        const ACTIVE_VIEW_TRANSITION = 1u64 << 50;
        /// For :-moz-suppress-for-print-selection.
        const SUPPRESS_FOR_PRINT_SELECTION = 1u64 << 51;
        /// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-paused
        const PAUSED = 1u64 << 52;
        /// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-seeking
        const SEEKING = 1u64 << 53;
        /// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-buffering
        const BUFFERING = 1u64 << 54;
        /// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-stalled
        const STALLED = 1u64 << 55;
        /// https://html.spec.whatwg.org/multipage/semantics-other.html#selector-muted
        const MUTED = 1u64 << 56;
        /// https://drafts.csswg.org/selectors-5/#headings
        /// These 4 bits are used to pack the elements heading level into the element state
        /// Heading levels can be from 1-9 so 4 bits allows us to express the full range.
        const HEADING_LEVEL_BITS = 0b1111u64 << HEADING_LEVEL_OFFSET;
        /// https://w3c.github.io/picture-in-picture/#css-pseudo-class
        const PICTURE_IN_PICTURE = 1u64 << 61;
        /// This element is fullscreen and was requested to have keyboard lock.
        const FULLSCREEN_KEYBOARD_LOCK = 1u64 << 62;

        /// Some convenience unions.
        const DIR_STATES = Self::LTR.bits() | Self::RTL.bits();

        const DIR_ATTR_STATES = Self::HAS_DIR_ATTR.bits() |
                                Self::HAS_DIR_ATTR_LTR.bits() |
                                Self::HAS_DIR_ATTR_RTL.bits() |
                                Self::HAS_DIR_ATTR_LIKE_AUTO.bits();

        const DISABLED_STATES = Self::DISABLED.bits() | Self::ENABLED.bits();

        const REQUIRED_STATES = Self::REQUIRED.bits() | Self::OPTIONAL_.bits();
    }
}

bitflags! {
    /// Event-based document states.
    #[repr(C)]
    #[derive(Clone, Copy, Debug, Eq, PartialEq)]
    pub struct DocumentState: u64 {
        /// Window activation status
        const WINDOW_INACTIVE = 1 << 0;
        /// RTL locale: specific to the XUL localedir attribute
        const RTL_LOCALE = 1 << 1;
        /// LTR locale: specific to the XUL localedir attribute
        const LTR_LOCALE = 1 << 2;

        const ALL_LOCALEDIR_BITS = Self::LTR_LOCALE.bits() | Self::RTL_LOCALE.bits();
    }
}

malloc_size_of_is_0!(ElementState, DocumentState);
