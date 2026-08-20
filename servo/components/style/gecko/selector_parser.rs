/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Gecko-specific bits for selector-parsing.

use crate::computed_value_flags::ComputedValueFlags;
use crate::derives::*;
use crate::invalidation::element::document_state::InvalidationMatchingData;
use crate::properties::ComputedValues;
use crate::selector_parser::{Direction, HorizontalDirection, SelectorParser};
use crate::str::starts_with_ignore_ascii_case;
use crate::string_cache::{Atom, Namespace, WeakAtom, WeakNamespace};
use crate::values::{AtomIdent, AtomString, CSSInteger, CustomIdent};
use cssparser::{match_ignore_ascii_case, CowRcStr, SourceLocation, ToCss, Token};
use cssparser::{BasicParseError, BasicParseErrorKind, Parser};
use dom::{DocumentState, ElementState, HEADING_LEVEL_OFFSET};
use selectors::parser::SelectorParseErrorKind;
use std::fmt;
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss as ToCss_};
use thin_vec::ThinVec;

pub use crate::gecko::pseudo_element::{
    PseudoElement, Target, EAGER_PSEUDOS, EAGER_PSEUDO_COUNT, PSEUDO_COUNT,
};
pub use crate::gecko::snapshot::SnapshotMap;

bitflags! {
    // See NonTSPseudoClass::is_enabled_in()
    #[derive(Copy, Clone)]
    struct NonTSPseudoClassFlag: u8 {
        const PSEUDO_CLASS_ENABLED_IN_UA_SHEETS = 1 << 0;
        const PSEUDO_CLASS_ENABLED_IN_CHROME = 1 << 1;
        const PSEUDO_CLASS_ENABLED_IN_UA_SHEETS_AND_CHROME =
            NonTSPseudoClassFlag::PSEUDO_CLASS_ENABLED_IN_UA_SHEETS.bits() |
            NonTSPseudoClassFlag::PSEUDO_CLASS_ENABLED_IN_CHROME.bits();
    }
}

/// The type used to store the language argument to the `:lang` pseudo-class.
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToCss, ToShmem)]
#[css(comma)]
pub struct Lang(#[css(iterable)] pub ThinVec<AtomIdent>);

/// The type used to store the state argument to the `:state` pseudo-class.
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToCss, ToShmem)]
pub struct CustomState(pub AtomIdent);

/// The properties that comprise a :heading() pseudoclass (e.g. a list of An+Bs).
/// https://drafts.csswg.org/selectors-5/#headings
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
pub struct HeadingSelectorData(pub ThinVec<CSSInteger>);

impl HeadingSelectorData {
    /// Matches the heading level from the given state against the list of
    /// heading level selectors. If AnPlusBs intersect with the level packed in
    /// ElementState then this will return true.
    pub fn matches_state(&self, state: ElementState) -> bool {
        let bits = state.intersection(ElementState::HEADING_LEVEL_BITS).bits();
        // If none of the HEADING_LEVEL_BITS are set on ElementState,
        // then this is not a heading level, so return false.
        if bits == 0 {
            return false;
        }
        // :heading selector will provide an empty levels list. It matches against any
        // heading level, so we can return true here.
        if self.0.is_empty() {
            return true;
        }
        let level = (bits >> HEADING_LEVEL_OFFSET) as i32;
        debug_assert!(level > 0 && level < 16);
        self.0.iter().any(|item| *item == level)
    }
}

macro_rules! pseudo_class_name {
    ([$(($css:expr, $name:ident, $state:tt, $flags:tt),)*]) => {
        /// Our representation of a non tree-structural pseudo-class.
        #[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq, ToShmem)]
        pub enum NonTSPseudoClass {
            $(
                #[doc = $css]
                $name,
            )*
            /// The `:lang` pseudo-class.
            Lang(Lang),
            /// The `:dir` pseudo-class.
            Dir(Direction),
            /// The :state` pseudo-class.
            CustomState(CustomState),
            /// The `:heading` & `:heading()` pseudo-classes.
            Heading(HeadingSelectorData),
            /// The :active-view-transition-type() pseudo-class:
            /// https://drafts.csswg.org/css-view-transitions-2/#the-active-view-transition-type-pseudo
            ActiveViewTransitionType(ThinVec<CustomIdent>),
            /// The non-standard `:-moz-locale-dir` pseudo-class.
            MozLocaleDir(Direction),
        }
    }
}
apply_non_ts_list!(pseudo_class_name);

impl ToCss for NonTSPseudoClass {
    fn to_css<W>(&self, dest: &mut W) -> fmt::Result
    where
        W: fmt::Write,
    {
        macro_rules! pseudo_class_serialize {
            ([$(($css:expr, $name:ident, $state:tt, $flags:tt),)*]) => {
                match *self {
                    $(NonTSPseudoClass::$name => concat!(":", $css),)*
                    NonTSPseudoClass::Lang(ref lang) => {
                        dest.write_str(":lang(")?;
                        lang.to_css(&mut CssWriter::new(dest))?;
                        return dest.write_char(')');
                    },
                    NonTSPseudoClass::CustomState(ref state) => {
                        dest.write_str(":state(")?;
                        state.to_css(&mut CssWriter::new(dest))?;
                        return dest.write_char(')');
                    },
                    NonTSPseudoClass::MozLocaleDir(ref dir) => {
                        dest.write_str(":-moz-locale-dir(")?;
                        dir.to_css(&mut CssWriter::new(dest))?;
                        return dest.write_char(')')
                    },
                    NonTSPseudoClass::Dir(ref dir) => {
                        dest.write_str(":dir(")?;
                        dir.to_css(&mut CssWriter::new(dest))?;
                        return dest.write_char(')')
                    },
                    NonTSPseudoClass::ActiveViewTransitionType(ref types) => {
                        dest.write_str(":active-view-transition-type(")?;
                        let mut first = true;
                        for ty in types.iter() {
                            if !first {
                                dest.write_str(", ")?;
                            }
                            first = false;
                            ty.to_css(&mut CssWriter::new(dest))?;
                        }
                        return dest.write_char(')')
                    },
                    NonTSPseudoClass::Heading(ref levels) => {
                        dest.write_str(":heading")?;
                        if levels.0.is_empty() {
                            return Ok(());
                        }
                        dest.write_str("(")?;
                        let mut first = true;
                        for item in levels.0.iter() {
                            if !first {
                                dest.write_str(", ")?;
                            }
                            first = false;
                            ToCss::to_css(item, dest)?;
                        }
                        return dest.write_str(")");
                    },
                }
            }
        }
        let ser = apply_non_ts_list!(pseudo_class_serialize);
        dest.write_str(ser)
    }
}

impl NonTSPseudoClass {
    /// Parses the name and returns a non-ts-pseudo-class if succeeds.
    /// None otherwise. It doesn't check whether the pseudo-class is enabled
    /// in a particular state.
    pub fn parse_non_functional(name: &str) -> Option<Self> {
        macro_rules! pseudo_class_parse {
            ([$(($css:expr, $name:ident, $state:tt, $flags:tt),)*]) => {
                match_ignore_ascii_case! { &name,
                    $($css => Some(NonTSPseudoClass::$name),)*
                    "heading" => Some(NonTSPseudoClass::Heading(HeadingSelectorData([].into()))),
                    "-moz-full-screen" => Some(NonTSPseudoClass::Fullscreen),
                    "-moz-read-only" => Some(NonTSPseudoClass::ReadOnly),
                    "-moz-read-write" => Some(NonTSPseudoClass::ReadWrite),
                    "-moz-focusring" => Some(NonTSPseudoClass::FocusVisible),
                    "-moz-ui-valid" => Some(NonTSPseudoClass::UserValid),
                    "-moz-ui-invalid" => Some(NonTSPseudoClass::UserInvalid),
                    "-webkit-autofill" => Some(NonTSPseudoClass::Autofill),
                    _ => None,
                }
            }
        }
        apply_non_ts_list!(pseudo_class_parse)
    }

    /// Returns true if this pseudo-class has any of the given flags set.
    fn has_any_flag(&self, flags: NonTSPseudoClassFlag) -> bool {
        macro_rules! check_flag {
            (_) => {
                false
            };
            ($flags:ident) => {
                NonTSPseudoClassFlag::$flags.intersects(flags)
            };
        }
        macro_rules! pseudo_class_check_is_enabled_in {
            ([$(($css:expr, $name:ident, $state:tt, $flags:tt),)*]) => {
                match *self {
                    $(NonTSPseudoClass::$name => check_flag!($flags),)*
                    NonTSPseudoClass::MozLocaleDir(_) => check_flag!(PSEUDO_CLASS_ENABLED_IN_UA_SHEETS_AND_CHROME),
                    NonTSPseudoClass::Heading(_) => check_flag!(PSEUDO_CLASS_ENABLED_IN_UA_SHEETS),
                    NonTSPseudoClass::CustomState(_) |
                    NonTSPseudoClass::Lang(_) |
                    NonTSPseudoClass::ActiveViewTransitionType(_) |
                    NonTSPseudoClass::Dir(_) => false,
                }
            }
        }
        apply_non_ts_list!(pseudo_class_check_is_enabled_in)
    }

    /// Returns whether the pseudo-class is enabled in content sheets.
    #[inline]
    fn is_enabled_in_content(&self) -> bool {
        if matches!(*self, Self::Heading(..)) {
            return static_prefs::pref!("dom.headingoffset.enabled");
        }
        if matches!(*self, Self::PictureInPicture) {
            return static_prefs::pref!("dom.media-pip.enabled");
        }
        if matches!(*self, NonTSPseudoClass::MozPlaceholder) {
            return static_prefs::pref!("layout.css.moz-placeholder.content.enabled");
        }
        if matches!(
            *self,
            Self::Playing
                | Self::Paused
                | Self::Seeking
                | Self::Buffering
                | Self::Stalled
                | Self::Muted
                | Self::VolumeLocked
        ) {
            return static_prefs::pref!("dom.media.pseudo-classes.enabled");
        }
        !self.has_any_flag(NonTSPseudoClassFlag::PSEUDO_CLASS_ENABLED_IN_UA_SHEETS_AND_CHROME)
    }

    /// Get the state flag associated with a pseudo-class, if any.
    pub fn state_flag(&self) -> ElementState {
        macro_rules! flag {
            (_) => {
                ElementState::empty()
            };
            ($state:ident) => {
                ElementState::$state
            };
        }
        macro_rules! pseudo_class_state {
            ([$(($css:expr, $name:ident, $state:tt, $flags:tt),)*]) => {
                match *self {
                    $(NonTSPseudoClass::$name => flag!($state),)*
                    NonTSPseudoClass::Dir(ref dir) => dir.element_state(),
                    NonTSPseudoClass::Heading(..) => ElementState::HEADING_LEVEL_BITS,
                    NonTSPseudoClass::ActiveViewTransitionType(..) => ElementState::ACTIVE_VIEW_TRANSITION,
                    NonTSPseudoClass::MozLocaleDir(..) |
                    NonTSPseudoClass::CustomState(..) |
                    NonTSPseudoClass::Lang(..) => ElementState::empty(),
                }
            }
        }
        apply_non_ts_list!(pseudo_class_state)
    }

    /// Get the document state flag associated with a pseudo-class, if any.
    pub fn document_state_flag(&self) -> DocumentState {
        match *self {
            NonTSPseudoClass::MozLocaleDir(ref dir) => match dir.as_horizontal_direction() {
                Some(HorizontalDirection::Ltr) => DocumentState::LTR_LOCALE,
                Some(HorizontalDirection::Rtl) => DocumentState::RTL_LOCALE,
                None => DocumentState::empty(),
            },
            NonTSPseudoClass::MozWindowInactive => DocumentState::WINDOW_INACTIVE,
            _ => DocumentState::empty(),
        }
    }

    /// Returns true if the given pseudoclass should trigger style sharing cache
    /// revalidation.
    pub fn needs_cache_revalidation(&self) -> bool {
        self.state_flag().is_empty()
            && !matches!(
                *self,
                // :dir() depends on state only, but may have an empty state_flag for invalid
                // arguments.
                NonTSPseudoClass::Dir(_) |
                      // We prevent style sharing for NAC.
                      NonTSPseudoClass::MozNativeAnonymous |
                      // :-moz-placeholder is parsed but never matches.
                      NonTSPseudoClass::MozPlaceholder |
                      // :-moz-is-html, :-moz-locale-dir and :-moz-window-inactive
                      // depend only on the state of the document, which is invariant across all
                      // elements involved in a given style cache.
                      NonTSPseudoClass::MozIsHTML |
                      NonTSPseudoClass::MozLocaleDir(_) |
                      NonTSPseudoClass::MozWindowInactive
            )
    }
}

impl ::selectors::parser::NonTSPseudoClass for NonTSPseudoClass {
    #[inline]
    fn is_active_or_hover(&self) -> bool {
        matches!(*self, NonTSPseudoClass::Active | NonTSPseudoClass::Hover)
    }

    /// We intentionally skip the link-related ones.
    #[inline]
    fn is_user_action_state(&self) -> bool {
        matches!(
            *self,
            NonTSPseudoClass::Hover | NonTSPseudoClass::Active | NonTSPseudoClass::Focus
        )
    }
}

/// The dummy struct we use to implement our selector parsing.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SelectorImpl;

/// A set of extra data to carry along with the matching context, either for
/// selector-matching or invalidation.
#[derive(Default)]
pub struct ExtraMatchingData<'a> {
    /// The invalidation data to invalidate doc-state pseudo-classes correctly.
    pub invalidation_data: InvalidationMatchingData,

    /// The invalidation bits from matching container queries. These are here
    /// just for convenience mostly.
    pub cascade_input_flags: ComputedValueFlags,

    /// The style of the originating element in order to evaluate @container
    /// size queries affecting pseudo-elements.
    pub originating_element_style: Option<&'a ComputedValues>,
}

impl ::selectors::SelectorImpl for SelectorImpl {
    type ExtraMatchingData<'a> = ExtraMatchingData<'a>;
    type AttrValue = AtomString;
    type Identifier = AtomIdent;
    type LocalName = AtomIdent;
    type NamespacePrefix = AtomIdent;
    type NamespaceUrl = Namespace;
    type BorrowedNamespaceUrl = WeakNamespace;
    type BorrowedLocalName = WeakAtom;

    type PseudoElement = PseudoElement;
    type NonTSPseudoClass = NonTSPseudoClass;

    fn should_collect_attr_hash(name: &AtomIdent) -> bool {
        !crate::bloom::is_attr_name_excluded_from_filter(name)
    }
}

impl<'a> SelectorParser<'a> {
    fn is_pseudo_class_enabled(&self, pseudo_class: &NonTSPseudoClass) -> bool {
        if pseudo_class.is_enabled_in_content() {
            return true;
        }

        if self.in_user_agent_stylesheet()
            && pseudo_class.has_any_flag(NonTSPseudoClassFlag::PSEUDO_CLASS_ENABLED_IN_UA_SHEETS)
        {
            return true;
        }

        if self.chrome_rules_enabled()
            && pseudo_class.has_any_flag(NonTSPseudoClassFlag::PSEUDO_CLASS_ENABLED_IN_CHROME)
        {
            return true;
        }

        if matches!(*pseudo_class, NonTSPseudoClass::MozBroken) {
            return static_prefs::pref!("layout.css.moz-broken.content.enabled");
        }

        return false;
    }

    fn is_pseudo_element_enabled(&self, pseudo_element: &PseudoElement) -> bool {
        if pseudo_element.enabled_in_content(&self.url_data, self.for_supports_rule) {
            return true;
        }

        if self.in_user_agent_stylesheet() && pseudo_element.enabled_in_ua_sheets() {
            return true;
        }

        if self.chrome_rules_enabled() && pseudo_element.enabled_in_chrome() {
            return true;
        }

        return false;
    }
}

/// Parse the functional pseudo-element with the function name.
pub fn parse_functional_pseudo_element_with_name<'i, 't>(
    name: &CowRcStr<'i>,
    parser: &mut Parser<'i, 't>,
    target: Target,
) -> Result<PseudoElement, ParseError<'i>> {
    use crate::gecko::pseudo_element::PtNameAndClassSelector;

    if matches!(target, Target::Selector)
        && starts_with_ignore_ascii_case(name.as_ref(), "-moz-tree-")
    {
        // Tree pseudo-elements can have zero or more arguments, separated
        // by either comma or space.
        let mut args = ThinVec::new();
        loop {
            let location = parser.current_source_location();
            match parser.next() {
                Ok(&Token::Ident(ref ident)) => args.push(Atom::from(ident.as_ref())),
                Ok(&Token::Comma) => {},
                Ok(t) => return Err(location.new_unexpected_token_error(t.clone())),
                Err(BasicParseError {
                    kind: BasicParseErrorKind::EndOfInput,
                    ..
                }) => break,
                _ => unreachable!("Parser::next() shouldn't return any other error"),
            }
        }
        return PseudoElement::tree_pseudo_element(name.as_ref(), args).ok_or(
            parser.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                name.clone(),
            )),
        );
    }

    Ok(match_ignore_ascii_case! { &name,
        "highlight" => PseudoElement::Highlight(AtomIdent::from(parser.expect_ident()?.as_ref())),
        "picker" => {
            let picker_element = parser.expect_ident()?.as_ref();
            if !picker_element.eq_ignore_ascii_case("select") {
                return Err(parser.new_custom_error(
                    SelectorParseErrorKind::UnsupportedPseudoClassOrElement(name.clone()),
                ));
            }
            // Don't use the actual ident, because it is not always all lowercase.
            PseudoElement::Picker(AtomIdent(atom!("select")))
        },
        "view-transition-group" => {
            PseudoElement::ViewTransitionGroup(PtNameAndClassSelector::parse(parser, target)?)
        },
        "view-transition-image-pair" => {
            PseudoElement::ViewTransitionImagePair(PtNameAndClassSelector::parse(parser, target)?)
        },
        "view-transition-old" => {
            PseudoElement::ViewTransitionOld(PtNameAndClassSelector::parse(parser, target)?)
        },
        "view-transition-new" => {
            PseudoElement::ViewTransitionNew(PtNameAndClassSelector::parse(parser, target)?)
        },
        _ => {
            return Err(parser.new_custom_error(
                SelectorParseErrorKind::UnsupportedPseudoClassOrElement(name.clone()),
            ));
        },
    })
}

impl<'a, 'i> ::selectors::Parser<'i> for SelectorParser<'a> {
    type Impl = SelectorImpl;
    type Error = StyleParseErrorKind<'i>;

    #[inline]
    fn parse_parent_selector(&self) -> bool {
        true
    }

    #[inline]
    fn parse_slotted(&self) -> bool {
        true
    }

    #[inline]
    fn parse_host(&self) -> bool {
        true
    }

    #[inline]
    fn parse_nth_child_of(&self) -> bool {
        true
    }

    #[inline]
    fn parse_is_and_where(&self) -> bool {
        true
    }

    #[inline]
    fn parse_has(&self) -> bool {
        true
    }

    #[inline]
    fn parse_part(&self) -> bool {
        true
    }

    #[inline]
    fn is_is_alias(&self, function: &str) -> bool {
        function.eq_ignore_ascii_case("-moz-any")
    }

    #[inline]
    fn allow_forgiving_selectors(&self) -> bool {
        !self.for_supports_rule
    }

    fn parse_non_ts_pseudo_class(
        &self,
        location: SourceLocation,
        name: CowRcStr<'i>,
    ) -> Result<NonTSPseudoClass, ParseError<'i>> {
        if let Some(pseudo_class) = NonTSPseudoClass::parse_non_functional(&name) {
            if self.is_pseudo_class_enabled(&pseudo_class) {
                return Ok(pseudo_class);
            }
        }
        Err(
            location.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                name,
            )),
        )
    }

    fn parse_non_ts_functional_pseudo_class<'t>(
        &self,
        name: CowRcStr<'i>,
        parser: &mut Parser<'i, 't>,
        _after_part: bool,
    ) -> Result<NonTSPseudoClass, ParseError<'i>> {
        let pseudo_class = match_ignore_ascii_case! { &name,
            "lang" => {
                let result = parser.parse_comma_separated(|input| {
                    Ok(AtomIdent::from(input.expect_ident_or_string()?.as_ref()))
                })?;
                if result.is_empty() {
                    return Err(parser.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                NonTSPseudoClass::Lang(Lang(result.into()))
            },
            "state" => {
                let result = AtomIdent::from(parser.expect_ident()?.as_ref());
                NonTSPseudoClass::CustomState(CustomState(result))
            },
            "heading" => {
                let result = parser.parse_comma_separated(|input| Ok(input.expect_integer()?))?;
                if result.is_empty() {
                    return Err(parser.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                NonTSPseudoClass::Heading(HeadingSelectorData(result.into()))
            },
            "active-view-transition-type" => {
                let result = parser.parse_comma_separated(|input| CustomIdent::parse(input, &[]))?;
                if result.is_empty() {
                    return Err(parser.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                NonTSPseudoClass::ActiveViewTransitionType(result.into())
            },
            "-moz-locale-dir" => {
                NonTSPseudoClass::MozLocaleDir(Direction::parse(parser)?)
            },
            "dir" => {
                NonTSPseudoClass::Dir(Direction::parse(parser)?)
            },
            _ => return Err(parser.new_custom_error(
                SelectorParseErrorKind::UnsupportedPseudoClassOrElement(name.clone())
            ))
        };
        if self.is_pseudo_class_enabled(&pseudo_class) {
            Ok(pseudo_class)
        } else {
            Err(
                parser.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                    name,
                )),
            )
        }
    }

    fn parse_pseudo_element(
        &self,
        location: SourceLocation,
        name: CowRcStr<'i>,
    ) -> Result<PseudoElement, ParseError<'i>> {
        if let Some(pseudo) = PseudoElement::from_slice(&name) {
            if self.is_pseudo_element_enabled(&pseudo) {
                return Ok(pseudo);
            }
        }

        // @supports must report disabled/unknown `-webkit-*` pseudos as unsupported.
        if !self.for_supports_rule {
            if let Some(pseudo) = PseudoElement::unknown_webkit_from_name(&name) {
                return Ok(pseudo);
            }
        }

        Err(
            location.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                name,
            )),
        )
    }

    fn parse_functional_pseudo_element<'t>(
        &self,
        name: CowRcStr<'i>,
        parser: &mut Parser<'i, 't>,
    ) -> Result<PseudoElement, ParseError<'i>> {
        let pseudo = parse_functional_pseudo_element_with_name(&name, parser, Target::Selector)?;
        if self.is_pseudo_element_enabled(&pseudo) {
            return Ok(pseudo);
        }

        Err(
            parser.new_custom_error(SelectorParseErrorKind::UnsupportedPseudoClassOrElement(
                name,
            )),
        )
    }

    fn default_namespace(&self) -> Option<Namespace> {
        self.namespaces.default.clone()
    }

    fn namespace_for_prefix(&self, prefix: &AtomIdent) -> Option<Namespace> {
        self.namespaces.prefixes.get(prefix).cloned()
    }
}

impl SelectorImpl {
    /// A helper to traverse each eagerly cascaded pseudo-element, executing
    /// `fun` on it.
    #[inline]
    pub fn each_eagerly_cascaded_pseudo_element<F>(mut fun: F)
    where
        F: FnMut(PseudoElement),
    {
        for pseudo in &EAGER_PSEUDOS {
            fun(pseudo.clone())
        }
    }
}

// Selector and component sizes are important for matching performance.
size_of_test!(selectors::parser::Selector<SelectorImpl>, 8);
size_of_test!(selectors::parser::Component<SelectorImpl>, 24);
size_of_test!(PseudoElement, 16);
size_of_test!(NonTSPseudoClass, 16);
