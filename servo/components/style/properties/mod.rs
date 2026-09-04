/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Supported CSS properties and the cascade.

pub mod cascade;
pub mod declaration_block;
pub mod shorthands;

pub use self::cascade::*;
pub use self::declaration_block::*;
pub use self::generated::*;

/// The CSS properties supported by the style system.
/// Generated from the properties.mako.rs template by build.rs
#[macro_use]
#[allow(unsafe_code)]
#[deny(missing_docs)]
pub mod generated {
    include!(concat!(env!("OUT_DIR"), "/properties.rs"));
}

use crate::applicable_declarations::RevertKind;
use crate::custom_properties::{self, ComputedSubstitutionFunctions, SubstitutionResult};
use crate::derives::*;
use crate::dom::AttributeTracker;
#[cfg(feature = "gecko")]
use crate::gecko_bindings::structs::{CSSPropertyId, NonCustomCSSPropertyId, RefPtr};
use crate::logical_geometry::WritingMode;
use crate::parser::ParserContext;
use crate::stylesheets::CssRuleType;
use crate::stylesheets::Origin;
use crate::stylist::Stylist;
use crate::typed_om::{ToTyped, TypedValue};
use crate::values::{computed, serialize_atom_name};
use arrayvec::{ArrayVec, Drain as ArrayVecDrain};
use cssparser::{match_ignore_ascii_case, Parser};
use rustc_hash::FxHashMap;
use servo_arc::Arc;
use std::{
    borrow::Cow,
    fmt::{self, Write},
    mem,
};
use style_traits::{
    CssString, CssWriter, KeywordsCollectFn, ParseError, ParsingMode, SpecifiedValueInfo, ToCss,
};
use thin_vec::ThinVec;

bitflags! {
    /// A set of flags for properties.
    #[derive(Clone, Copy)]
    pub struct PropertyFlags: u16 {
        /// This longhand property applies to ::first-letter.
        const APPLIES_TO_FIRST_LETTER = 1 << 1;
        /// This longhand property applies to ::first-line.
        const APPLIES_TO_FIRST_LINE = 1 << 2;
        /// This longhand property applies to ::placeholder.
        const APPLIES_TO_PLACEHOLDER = 1 << 3;
        ///  This longhand property applies to ::cue.
        const APPLIES_TO_CUE = 1 << 4;
        /// This longhand property applies to ::marker.
        const APPLIES_TO_MARKER = 1 << 5;
        /// Applies to ::highlight() and related (::selection / ::target-text) pseudo-elements.
        /// https://drafts.csswg.org/css-pseudo/#highlight-styling
        const APPLIES_TO_HIGHLIGHT = 1 << 6;
        /// This property is a legacy shorthand.
        ///
        /// https://drafts.csswg.org/css-cascade/#legacy-shorthand
        const IS_LEGACY_SHORTHAND = 1 << 7;
        /// This shorthand remains enabled even if some subproperties are pref-disabled.
        const ALLOWS_DISABLED_SUBPROPERTIES = 1 << 8;

        /* The following flags are currently not used in Rust code, they
         * only need to be listed in corresponding properties so that
         * they can be checked in the C++ side via ServoCSSPropList.h. */

        /// This property can be animated on the compositor.
        const CAN_ANIMATE_ON_COMPOSITOR = 0;
        /// This property can produce a scroll-linked effect.
        const SCROLL_LINKED_EFFECTIVE = 0;
        /// See data.py's documentation about the affects_flags.
        const AFFECTS_LAYOUT = 0;
        #[allow(missing_docs)]
        const AFFECTS_OVERFLOW = 0;
        #[allow(missing_docs)]
        const AFFECTS_PAINT = 0;
    }
}

/// An enum to represent a CSS Wide keyword.
#[derive(
    Clone, Copy, Debug, Eq, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub enum CSSWideKeyword {
    /// The `initial` keyword.
    Initial,
    /// The `inherit` keyword.
    Inherit,
    /// The `unset` keyword.
    Unset,
    /// The `revert` keyword.
    Revert,
    /// The `revert-layer` keyword.
    RevertLayer,
    /// The `revert-rule` keyword.
    RevertRule,
}

impl CSSWideKeyword {
    /// Returns the string representation of the keyword.
    pub fn to_str(&self) -> &'static str {
        match *self {
            Self::Initial => "initial",
            Self::Inherit => "inherit",
            Self::Unset => "unset",
            Self::Revert => "revert",
            Self::RevertLayer => "revert-layer",
            Self::RevertRule => "revert-rule",
        }
    }

    /// Parses a CSS wide keyword from a CSS identifier.
    pub fn from_ident(ident: &str) -> Result<Self, ()> {
        Ok(match_ignore_ascii_case! { ident,
            "initial" => Self::Initial,
            "inherit" => Self::Inherit,
            "unset" => Self::Unset,
            "revert" => Self::Revert,
            "revert-layer" => Self::RevertLayer,
            "revert-rule" if crate::pref!("layout.css.revert-rule.enabled") => Self::RevertRule,
            _ => return Err(()),
        })
    }

    /// Parses a CSS wide keyword completely.
    pub fn parse(input: &mut Parser) -> Result<Self, ()> {
        let keyword = {
            let ident = input.expect_ident().map_err(|_| ())?;
            Self::from_ident(ident)?
        };
        input.expect_exhausted().map_err(|_| ())?;
        Ok(keyword)
    }

    /// Returns the revert kind for this wide keyword.
    pub fn revert_kind(self) -> Option<RevertKind> {
        Some(match self {
            Self::Initial | Self::Inherit | Self::Unset => return None,
            Self::Revert => RevertKind::Origin,
            Self::RevertLayer => RevertKind::Layer,
            Self::RevertRule => RevertKind::Rule,
        })
    }
}

/// A declaration using a CSS-wide keyword.
#[derive(Clone, PartialEq, ToCss, ToShmem, MallocSizeOf)]
pub struct WideKeywordDeclaration {
    #[css(skip)]
    id: LonghandId,
    /// The CSS-wide keyword.
    pub keyword: CSSWideKeyword,
}

// XXX Switch back to ToTyped derive once it can automatically handle structs
// Tracking in bug 1991631
impl ToTyped for WideKeywordDeclaration {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        self.keyword.to_typed(dest)
    }
}

/// An unparsed declaration that contains `var()` functions.
#[derive(Clone, PartialEq, ToCss, ToShmem, MallocSizeOf, ToTyped)]
pub struct VariableDeclaration {
    /// The id of the property this declaration represents.
    #[css(skip)]
    id: LonghandId,
    /// The unparsed value of the variable.
    #[ignore_malloc_size_of = "Arc"]
    pub value: Arc<UnparsedValue>,
}

/// A custom property declaration value is either an unparsed value or a CSS
/// wide-keyword.
#[derive(Clone, PartialEq, ToCss, ToShmem, ToTyped)]
pub enum CustomDeclarationValue {
    /// An unparsed value.
    Unparsed(Arc<custom_properties::SpecifiedValue>),
    /// An already-parsed value.
    Parsed(Arc<crate::properties_and_values::value::SpecifiedValue>),
    /// A wide keyword.
    CSSWideKeyword(CSSWideKeyword),
}

/// A custom property declaration with the property name and the declared value.
#[derive(Clone, PartialEq, ToCss, ToShmem, MallocSizeOf, ToTyped)]
pub struct CustomDeclaration {
    /// The name of the custom property.
    #[css(skip)]
    pub name: custom_properties::Name,
    /// The value of the custom property.
    #[ignore_malloc_size_of = "Arc"]
    pub value: CustomDeclarationValue,
}

impl fmt::Debug for PropertyDeclaration {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.id().to_css(&mut CssWriter::new(f))?;
        f.write_str(": ")?;

        // Because PropertyDeclaration::to_css requires CssStringWriter, we can't write
        // it directly to f, and need to allocate an intermediate string. This is
        // fine for debug-only code.
        let mut s = CssString::new();
        self.to_css(&mut s)?;
        write!(f, "{}", s)
    }
}

/// A longhand or shorthand property.
#[derive(
    Clone, Copy, Debug, PartialEq, Eq, Hash, ToComputedValue, ToResolvedValue, ToShmem, MallocSizeOf,
)]
#[repr(C)]
pub struct NonCustomPropertyId(u16);

impl ToCss for NonCustomPropertyId {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(self.name())
    }
}

impl NonCustomPropertyId {
    /// Returns the underlying index, used for use counter.
    pub fn bit(self) -> usize {
        self.0 as usize
    }

    /// Convert a `NonCustomPropertyId` into a `NonCustomCSSPropertyId`.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_noncustomcsspropertyid(self) -> NonCustomCSSPropertyId {
        // unsafe: guaranteed by static_assert_noncustomcsspropertyid.
        unsafe { mem::transmute(self.0) }
    }

    /// Convert an `NonCustomCSSPropertyId` into a `NonCustomPropertyId`.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn from_noncustomcsspropertyid(prop: NonCustomCSSPropertyId) -> Option<Self> {
        let prop = prop as u16;
        if prop >= property_counts::NON_CUSTOM as u16 {
            return None;
        }
        // guaranteed by static_assert_noncustomcsspropertyid above.
        Some(NonCustomPropertyId(prop))
    }

    /// Resolves the alias of a given property if needed.
    pub fn unaliased(self) -> Self {
        let Some(alias_id) = self.as_alias() else {
            return self;
        };
        alias_id.aliased_property()
    }

    /// Turns this `NonCustomPropertyId` into a `PropertyId`.
    #[inline]
    pub fn to_property_id(self) -> PropertyId {
        PropertyId::NonCustom(self)
    }

    /// Returns a longhand id, if this property is one.
    #[inline]
    pub fn as_longhand(self) -> Option<LonghandId> {
        if self.0 < property_counts::LONGHANDS as u16 {
            return Some(unsafe { mem::transmute(self.0) });
        }
        None
    }

    /// Returns a shorthand id, if this property is one.
    #[inline]
    pub fn as_shorthand(self) -> Option<ShorthandId> {
        if self.0 >= property_counts::LONGHANDS as u16
            && self.0 < property_counts::LONGHANDS_AND_SHORTHANDS as u16
        {
            return Some(unsafe { mem::transmute(self.0 - (property_counts::LONGHANDS as u16)) });
        }
        None
    }

    /// Returns an alias id, if this property is one.
    #[inline]
    pub fn as_alias(self) -> Option<AliasId> {
        debug_assert!((self.0 as usize) < property_counts::NON_CUSTOM);
        if self.0 >= property_counts::LONGHANDS_AND_SHORTHANDS as u16 {
            return Some(unsafe {
                mem::transmute(self.0 - (property_counts::LONGHANDS_AND_SHORTHANDS as u16))
            });
        }
        None
    }

    /// Returns either a longhand or a shorthand, resolving aliases.
    #[inline]
    pub fn longhand_or_shorthand(self) -> Result<LonghandId, ShorthandId> {
        let id = self.unaliased();
        match id.as_longhand() {
            Some(lh) => Ok(lh),
            None => Err(id.as_shorthand().unwrap()),
        }
    }

    /// Converts a longhand id into a non-custom property id.
    #[inline]
    pub const fn from_longhand(id: LonghandId) -> Self {
        Self(id as u16)
    }

    /// Converts a shorthand id into a non-custom property id.
    #[inline]
    pub const fn from_shorthand(id: ShorthandId) -> Self {
        Self((id as u16) + (property_counts::LONGHANDS as u16))
    }

    /// Converts an alias id into a non-custom property id.
    #[inline]
    pub const fn from_alias(id: AliasId) -> Self {
        Self((id as u16) + (property_counts::LONGHANDS_AND_SHORTHANDS as u16))
    }

    /// Iterate over all non-custom properties in arbitrary order.
    pub fn iter() -> impl Iterator<Item = Self> {
        (0..property_counts::NON_CUSTOM as u16).map(|index| Self(index))
    }
}

impl From<LonghandId> for NonCustomPropertyId {
    #[inline]
    fn from(id: LonghandId) -> Self {
        Self::from_longhand(id)
    }
}

impl From<ShorthandId> for NonCustomPropertyId {
    #[inline]
    fn from(id: ShorthandId) -> Self {
        Self::from_shorthand(id)
    }
}

impl From<AliasId> for NonCustomPropertyId {
    #[inline]
    fn from(id: AliasId) -> Self {
        Self::from_alias(id)
    }
}

/// Representation of a CSS property, that is, either a longhand, a shorthand, or a custom
/// property.
#[derive(Clone, Debug, Eq, MallocSizeOf, PartialEq)]
pub enum PropertyId {
    /// An alias for a shorthand property.
    NonCustom(NonCustomPropertyId),
    /// A custom property.
    Custom(custom_properties::Name),
}

impl ToCss for PropertyId {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            PropertyId::NonCustom(id) => dest.write_str(id.name()),
            PropertyId::Custom(ref name) => {
                dest.write_str("--")?;
                serialize_atom_name(name, dest)
            },
        }
    }
}

impl PropertyId {
    /// Return the longhand id that this property id represents.
    #[inline]
    pub fn longhand_id(&self) -> Option<LonghandId> {
        self.non_custom_non_alias_id()?.as_longhand()
    }

    /// Returns true if this property is one of the animatable properties.
    pub fn is_animatable(&self) -> bool {
        match self {
            Self::NonCustom(id) => id.is_animatable(),
            Self::Custom(_) => true,
        }
    }

    /// Returns a given property from the given name, _regardless of whether it is enabled or
    /// not_, or Err(()) for unknown properties.
    ///
    /// Do not use for non-testing purposes.
    pub fn parse_unchecked_for_testing(name: &str) -> Result<Self, ()> {
        Self::parse_unchecked(name, None)
    }

    /// Parses a property name, and returns an error if it's unknown or isn't enabled for all
    /// content.
    #[inline]
    pub fn parse_enabled_for_all_content(name: &str) -> Result<Self, ()> {
        let id = Self::parse_unchecked(name, None)?;

        if !id.enabled_for_all_content() {
            return Err(());
        }

        Ok(id)
    }

    /// Parses a property name, and returns an error if it's unknown or isn't allowed in this
    /// context.
    #[inline]
    pub fn parse(name: &str, context: &ParserContext) -> Result<Self, ()> {
        let id = Self::parse_unchecked(name, context.use_counters)?;
        if !id.allowed_in(context) {
            return Err(());
        }
        Ok(id)
    }

    /// Parses a property name, and returns an error if it's unknown or isn't allowed in this
    /// context, ignoring the rule_type checks.
    ///
    /// This is useful for parsing stuff from CSS values, for example.
    #[inline]
    pub fn parse_ignoring_rule_type(name: &str, context: &ParserContext) -> Result<Self, ()> {
        let id = Self::parse_unchecked(name, None)?;
        if !id.allowed_in_ignoring_rule_type(context) {
            return Err(());
        }
        Ok(id)
    }

    /// Returns a property id from Gecko's NonCustomCSSPropertyId.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn from_noncustomcsspropertyid(id: NonCustomCSSPropertyId) -> Option<Self> {
        Some(NonCustomPropertyId::from_noncustomcsspropertyid(id)?.to_property_id())
    }

    /// Returns a property id from Gecko's CSSPropertyId.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn from_gecko_css_property_id(property: &CSSPropertyId) -> Option<Self> {
        Some(
            if property.mId == NonCustomCSSPropertyId::eCSSPropertyExtra_variable {
                debug_assert!(!property.mCustomName.mRawPtr.is_null());
                Self::Custom(unsafe { crate::Atom::from_raw(property.mCustomName.mRawPtr) })
            } else {
                Self::NonCustom(NonCustomPropertyId::from_noncustomcsspropertyid(
                    property.mId,
                )?)
            },
        )
    }

    /// Returns true if the property is a shorthand or shorthand alias.
    #[inline]
    pub fn is_shorthand(&self) -> bool {
        self.as_shorthand().is_ok()
    }

    /// Given this property id, get it either as a shorthand or as a
    /// `PropertyDeclarationId`.
    pub fn as_shorthand(&self) -> Result<ShorthandId, PropertyDeclarationId<'_>> {
        match *self {
            Self::NonCustom(id) => match id.longhand_or_shorthand() {
                Ok(lh) => Err(PropertyDeclarationId::Longhand(lh)),
                Err(sh) => Ok(sh),
            },
            Self::Custom(ref name) => Err(PropertyDeclarationId::Custom(name)),
        }
    }

    /// Returns the `NonCustomPropertyId` corresponding to this property id.
    pub fn non_custom_id(&self) -> Option<NonCustomPropertyId> {
        match *self {
            Self::Custom(_) => None,
            Self::NonCustom(id) => Some(id),
        }
    }

    /// Returns non-alias NonCustomPropertyId corresponding to this
    /// property id.
    fn non_custom_non_alias_id(&self) -> Option<NonCustomPropertyId> {
        self.non_custom_id().map(NonCustomPropertyId::unaliased)
    }

    /// Whether the property is enabled for all content regardless of the
    /// stylesheet it was declared on (that is, in practice only checks prefs).
    #[inline]
    pub fn enabled_for_all_content(&self) -> bool {
        let id = match self.non_custom_id() {
            // Custom properties are allowed everywhere
            None => return true,
            Some(id) => id,
        };

        id.enabled_for_all_content()
    }

    /// Converts this PropertyId in NonCustomCSSPropertyId, resolving aliases to the
    /// resolved property, and returning eCSSPropertyExtra_variable for custom
    /// properties.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_noncustomcsspropertyid_resolving_aliases(&self) -> NonCustomCSSPropertyId {
        match self.non_custom_non_alias_id() {
            Some(id) => id.to_noncustomcsspropertyid(),
            None => NonCustomCSSPropertyId::eCSSPropertyExtra_variable,
        }
    }

    fn allowed_in(&self, context: &ParserContext) -> bool {
        let id = match self.non_custom_id() {
            // Custom properties are allowed everywhere, except `position-try`.
            None => {
                return !context
                    .nesting_context
                    .rule_types
                    .contains(CssRuleType::PositionTry)
            },
            Some(id) => id,
        };
        id.allowed_in(context)
    }

    #[inline]
    fn allowed_in_ignoring_rule_type(&self, context: &ParserContext) -> bool {
        let id = match self.non_custom_id() {
            // Custom properties are allowed everywhere
            None => return true,
            Some(id) => id,
        };
        id.allowed_in_ignoring_rule_type(context)
    }

    /// Whether the property supports the given CSS type.
    /// `ty` should a bitflags of constants in style_traits::CssType.
    pub fn supports_type(&self, ty: u8) -> bool {
        let id = self.non_custom_non_alias_id();
        id.map_or(0, |id| id.supported_types()) & ty != 0
    }

    /// Collect supported starting word of values of this property.
    ///
    /// See style_traits::SpecifiedValueInfo::collect_completion_keywords for more
    /// details.
    pub fn collect_property_completion_keywords(&self, f: KeywordsCollectFn) {
        if let Some(id) = self.non_custom_non_alias_id() {
            id.collect_property_completion_keywords(f);
        }
        CSSWideKeyword::collect_completion_keywords(f);
    }
}

impl ToCss for LonghandId {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(self.name())
    }
}

impl fmt::Debug for LonghandId {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        formatter.write_str(self.name())
    }
}

impl LonghandId {
    /// Get the name of this longhand property.
    #[inline]
    pub fn name(&self) -> &'static str {
        NonCustomPropertyId::from(*self).name()
    }

    /// Returns whether the longhand property is inherited by default.
    #[inline]
    pub fn inherited(self) -> bool {
        !LonghandIdSet::reset().contains(self)
    }

    /// Returns whether the longhand property is zoom-dependent.
    #[inline]
    pub fn zoom_dependent(self) -> bool {
        LonghandIdSet::zoom_dependent().contains(self)
    }

    /// Returns true if the property is one that is ignored when document
    /// colors are disabled.
    #[inline]
    pub fn ignored_when_document_colors_disabled(self) -> bool {
        LonghandIdSet::ignored_when_colors_disabled().contains(self)
    }

    /// Returns whether this longhand is `non_custom` or is a longhand of it.
    pub fn is_or_is_longhand_of(self, non_custom: NonCustomPropertyId) -> bool {
        match non_custom.longhand_or_shorthand() {
            Ok(lh) => self == lh,
            Err(sh) => self.is_longhand_of(sh),
        }
    }

    /// Returns whether this longhand is a longhand of `shorthand`.
    pub fn is_longhand_of(self, shorthand: ShorthandId) -> bool {
        self.shorthands().any(|s| s == shorthand)
    }

    /// Returns whether this property is animatable.
    #[inline]
    pub fn is_animatable(self) -> bool {
        NonCustomPropertyId::from(self).is_animatable()
    }

    /// Returns whether this property is animatable in a discrete way.
    #[inline]
    pub fn is_discrete_animatable(self) -> bool {
        // `display` is discrete but has a custom Animate impl, so it's not in the set.
        LonghandIdSet::discrete_animatable().contains(self) || self == LonghandId::Display
    }

    /// Converts from a LonghandId to an adequate NonCustomCSSPropertyId.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_noncustomcsspropertyid(self) -> NonCustomCSSPropertyId {
        NonCustomPropertyId::from(self).to_noncustomcsspropertyid()
    }

    #[cfg(feature = "gecko")]
    /// Returns a longhand id from Gecko's NonCustomCSSPropertyId.
    pub fn from_noncustomcsspropertyid(id: NonCustomCSSPropertyId) -> Option<Self> {
        NonCustomPropertyId::from_noncustomcsspropertyid(id)?
            .unaliased()
            .as_longhand()
    }

    /// Return whether this property is logical.
    #[inline]
    pub fn is_logical(self) -> bool {
        LonghandIdSet::logical().contains(self)
    }
}

impl ToCss for ShorthandId {
    #[inline]
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(self.name())
    }
}

impl ShorthandId {
    /// Get the name for this shorthand property.
    #[inline]
    pub fn name(&self) -> &'static str {
        NonCustomPropertyId::from(*self).name()
    }

    /// Converts from a ShorthandId to an adequate NonCustomCSSPropertyId.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_noncustomcsspropertyid(self) -> NonCustomCSSPropertyId {
        NonCustomPropertyId::from(self).to_noncustomcsspropertyid()
    }

    /// Converts from a NonCustomCSSPropertyId to a ShorthandId.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn from_noncustomcsspropertyid(id: NonCustomCSSPropertyId) -> Option<Self> {
        NonCustomPropertyId::from_noncustomcsspropertyid(id)?
            .unaliased()
            .as_shorthand()
    }

    /// Finds and returns an appendable value for the given declarations.
    ///
    /// Returns the optional appendable value.
    pub fn get_shorthand_appendable_value<'a, 'b: 'a>(
        self,
        declarations: &'a [&'b PropertyDeclaration],
    ) -> Option<AppendableValue<'a, 'b>> {
        let first_declaration = declarations.first()?;
        let rest = || declarations.iter().skip(1);

        // https://drafts.csswg.org/css-variables/#variables-in-shorthands
        if let Some(css) = first_declaration.with_variables_from_shorthand(self) {
            if rest().all(|d| d.with_variables_from_shorthand(self) == Some(css)) {
                return Some(AppendableValue::Css(css));
            }
            return None;
        }

        // Check whether they are all the same CSS-wide keyword.
        if let Some(keyword) = first_declaration.get_css_wide_keyword() {
            if rest().all(|d| d.get_css_wide_keyword() == Some(keyword)) {
                return Some(AppendableValue::Css(keyword.to_str()));
            }
            return None;
        }

        if self == ShorthandId::All {
            // 'all' only supports variables and CSS wide keywords.
            return None;
        }

        // Check whether all declarations can be serialized as part of shorthand.
        if declarations
            .iter()
            .all(|d| d.may_serialize_as_part_of_shorthand())
        {
            return Some(AppendableValue::DeclarationsForShorthand(
                self,
                declarations,
            ));
        }

        None
    }

    /// Returns whether this property is a legacy shorthand.
    #[inline]
    pub fn is_legacy_shorthand(self) -> bool {
        self.flags().contains(PropertyFlags::IS_LEGACY_SHORTHAND)
    }

    /// Returns whether this shorthand remains enabled even if some subproperties are pref-disabled.
    #[inline]
    pub fn allows_disabled_subproperties(self) -> bool {
        self.flags()
            .contains(PropertyFlags::ALLOWS_DISABLED_SUBPROPERTIES)
    }
}

/// The arbitrary substitution functions we support.
pub const ARBITRARY_SUBSTITUTION_FUNCTIONS: &[&str] = &["var", "env", "attr"];

fn parse_non_custom_property_declaration_value_into(
    declarations: &mut SourcePropertyDeclaration,
    context: &ParserContext,
    input: &mut Parser,
    start: &cssparser::ParserState,
    parse_entirely_into: impl FnOnce(
        &mut SourcePropertyDeclaration,
        &mut Parser,
    ) -> Result<(), ParseError>,
    parsed_wide_keyword: impl FnOnce(&mut SourcePropertyDeclaration, CSSWideKeyword),
    parsed_custom: impl FnOnce(&mut SourcePropertyDeclaration, custom_properties::VariableValue),
) -> Result<(), ParseError> {
    let mut starts_with_curly_block = false;
    if let Ok(token) = input.next() {
        match token {
            cssparser::Token::Ident(ident) => {
                if let Ok(wk) = CSSWideKeyword::from_ident(ident) {
                    if input.expect_exhausted().is_ok() {
                        return {
                            parsed_wide_keyword(declarations, wk);
                            Ok(())
                        };
                    }
                }
            },
            cssparser::Token::CurlyBracketBlock => {
                starts_with_curly_block = true;
            },
            _ => {},
        }
    };

    input.reset(start);
    input.look_for_arbitrary_substitution_functions(ARBITRARY_SUBSTITUTION_FUNCTIONS);

    let mut saw_arbitrary_substitution_functions = false;
    let err = match parse_entirely_into(declarations, input) {
        Ok(()) => {
            saw_arbitrary_substitution_functions = input.seen_arbitrary_substitution_functions();
            if !saw_arbitrary_substitution_functions {
                return Ok(());
            }
            ParseError::custom(style_traits::StyleParseErrorKind::UnspecifiedError)
        },
        Err(e) => e,
    };

    // Look for var(), env() and top-level curly blocks after the error.
    let start_pos = start.position();
    let mut at_start = start_pos == input.position();
    let mut invalid = false;
    while let Ok(token) = input.next() {
        if matches!(token, cssparser::Token::CurlyBracketBlock) {
            if !starts_with_curly_block || !at_start {
                invalid = true;
                break;
            }
        } else if starts_with_curly_block {
            invalid = true;
            break;
        }
        at_start = false;
    }
    saw_arbitrary_substitution_functions =
        saw_arbitrary_substitution_functions || input.seen_arbitrary_substitution_functions();
    if !saw_arbitrary_substitution_functions || invalid {
        return Err(err);
    }
    input.reset(start);
    let value = custom_properties::VariableValue::parse(
        input,
        Some(&context.namespaces.prefixes),
        context.url_data,
    )?;
    parsed_custom(declarations, value);
    Ok(())
}

impl PropertyDeclaration {
    fn with_variables_from_shorthand(&self, shorthand: ShorthandId) -> Option<&str> {
        match *self {
            PropertyDeclaration::WithVariables(ref declaration) => {
                let s = declaration.value.from_shorthand?;
                if s != shorthand {
                    return None;
                }
                Some(&*declaration.value.variable_value.css)
            },
            _ => None,
        }
    }

    /// Returns a CSS-wide keyword declaration for a given property.
    #[inline]
    pub fn css_wide_keyword(id: LonghandId, keyword: CSSWideKeyword) -> Self {
        Self::CSSWideKeyword(WideKeywordDeclaration { id, keyword })
    }

    /// Returns a CSS-wide keyword if the declaration's value is one.
    #[inline]
    pub fn get_css_wide_keyword(&self) -> Option<CSSWideKeyword> {
        match *self {
            PropertyDeclaration::CSSWideKeyword(ref declaration) => Some(declaration.keyword),
            _ => None,
        }
    }

    /// Returns whether the declaration may be serialized as part of a shorthand.
    ///
    /// This method returns false if this declaration contains variable or has a
    /// CSS-wide keyword value, since these values cannot be serialized as part
    /// of a shorthand.
    ///
    /// Caller should check `with_variables_from_shorthand()` and whether all
    /// needed declarations has the same CSS-wide keyword first.
    ///
    /// Note that, serialization of a shorthand may still fail because of other
    /// property-specific requirement even when this method returns true for all
    /// the longhand declarations.
    pub fn may_serialize_as_part_of_shorthand(&self) -> bool {
        match *self {
            PropertyDeclaration::CSSWideKeyword(..) | PropertyDeclaration::WithVariables(..) => {
                false
            },
            PropertyDeclaration::Custom(..) => {
                unreachable!("Serializing a custom property as part of shorthand?")
            },
            _ => true,
        }
    }

    /// Returns true if this property declaration is for one of the animatable properties.
    pub fn is_animatable(&self) -> bool {
        self.id().is_animatable()
    }

    /// Returns true if this property is a custom property, false
    /// otherwise.
    pub fn is_custom(&self) -> bool {
        matches!(*self, PropertyDeclaration::Custom(..))
    }

    /// The `context` parameter controls this:
    ///
    /// <https://drafts.csswg.org/css-animations/#keyframes>
    /// > The <declaration-list> inside of <keyframe-block> accepts any CSS property
    /// > except those defined in this specification,
    /// > but does accept the `animation-play-state` property and interprets it specially.
    ///
    /// This will not actually parse Importance values, and will always set things
    /// to Importance::Normal. Parsing Importance values is the job of PropertyDeclarationParser,
    /// we only set them here so that we don't have to reallocate
    pub fn parse_into(
        declarations: &mut SourcePropertyDeclaration,
        id: PropertyId,
        context: &ParserContext,
        input: &mut Parser,
    ) -> Result<(), ParseError> {
        assert!(declarations.is_empty());
        debug_assert!(id.allowed_in(context), "{:?}", id);
        input.skip_whitespace();

        let start = input.state();
        let non_custom_id = match id {
            PropertyId::Custom(property_name) => {
                let value = match input.try_parse(CSSWideKeyword::parse) {
                    Ok(keyword) => CustomDeclarationValue::CSSWideKeyword(keyword),
                    Err(()) => CustomDeclarationValue::Unparsed(Arc::new(
                        custom_properties::VariableValue::parse(
                            input,
                            Some(&context.namespaces.prefixes),
                            context.url_data,
                        )?,
                    )),
                };
                declarations.push(PropertyDeclaration::Custom(CustomDeclaration {
                    name: property_name,
                    value,
                }));
                return Ok(());
            },
            PropertyId::NonCustom(id) => id,
        };
        match non_custom_id.longhand_or_shorthand() {
            Ok(longhand_id) => {
                parse_non_custom_property_declaration_value_into(
                    declarations,
                    context,
                    input,
                    &start,
                    |declarations, input| {
                        let decl = input
                            .parse_entirely(|input| longhand_id.parse_value(context, input))?;
                        declarations.push(decl);
                        Ok(())
                    },
                    |declarations, wk| {
                        declarations.push(PropertyDeclaration::css_wide_keyword(longhand_id, wk));
                    },
                    |declarations, variable_value| {
                        declarations.push(PropertyDeclaration::WithVariables(VariableDeclaration {
                            id: longhand_id,
                            value: Arc::new(UnparsedValue {
                                variable_value,
                                from_shorthand: None,
                            }),
                        }))
                    },
                )?;
            },
            Err(shorthand_id) => {
                parse_non_custom_property_declaration_value_into(
                    declarations,
                    context,
                    input,
                    &start,
                    // Not using parse_entirely here: each ShorthandId::parse_into function needs
                    // to do so *before* pushing to `declarations`.
                    |declarations, input| shorthand_id.parse_into(declarations, context, input),
                    |declarations, wk| {
                        if shorthand_id == ShorthandId::All {
                            declarations.all_shorthand = AllShorthand::CSSWideKeyword(wk)
                        } else {
                            for longhand in shorthand_id.longhands() {
                                declarations
                                    .push(PropertyDeclaration::css_wide_keyword(longhand, wk));
                            }
                        }
                    },
                    |declarations, variable_value| {
                        let unparsed = Arc::new(UnparsedValue {
                            variable_value,
                            from_shorthand: Some(shorthand_id),
                        });
                        if shorthand_id == ShorthandId::All {
                            declarations.all_shorthand = AllShorthand::WithVariables(unparsed)
                        } else {
                            for id in shorthand_id.longhands() {
                                declarations.push(PropertyDeclaration::WithVariables(
                                    VariableDeclaration {
                                        id,
                                        value: unparsed.clone(),
                                    },
                                ))
                            }
                        }
                    },
                )?;
            },
        }
        if let Some(use_counters) = context.use_counters {
            use_counters.non_custom_properties.record(non_custom_id);
        }
        Ok(())
    }
}

/// A PropertyDeclarationId without references, for use as a hash map key.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum OwnedPropertyDeclarationId {
    /// A longhand.
    Longhand(LonghandId),
    /// A custom property declaration.
    Custom(custom_properties::Name),
}

impl OwnedPropertyDeclarationId {
    /// Return whether this property is logical.
    #[inline]
    pub fn is_logical(&self) -> bool {
        self.as_borrowed().is_logical()
    }

    /// Returns the corresponding PropertyDeclarationId.
    #[inline]
    pub fn as_borrowed(&self) -> PropertyDeclarationId<'_> {
        match self {
            Self::Longhand(id) => PropertyDeclarationId::Longhand(*id),
            Self::Custom(name) => PropertyDeclarationId::Custom(name),
        }
    }

    /// Convert an `CSSPropertyId` into an `OwnedPropertyDeclarationId`.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn from_gecko_css_property_id(property: &CSSPropertyId) -> Option<Self> {
        Some(match PropertyId::from_gecko_css_property_id(property)? {
            PropertyId::Custom(name) => Self::Custom(name),
            PropertyId::NonCustom(id) => Self::Longhand(id.as_longhand()?),
        })
    }
}

/// An identifier for a given property declaration, which can be either a
/// longhand or a custom property.
#[derive(Clone, Copy, Debug, PartialEq, MallocSizeOf)]
pub enum PropertyDeclarationId<'a> {
    /// A longhand.
    Longhand(LonghandId),
    /// A custom property declaration.
    Custom(&'a custom_properties::Name),
}

impl<'a> ToCss for PropertyDeclarationId<'a> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            PropertyDeclarationId::Longhand(id) => dest.write_str(id.name()),
            PropertyDeclarationId::Custom(name) => {
                dest.write_str("--")?;
                serialize_atom_name(name, dest)
            },
        }
    }
}

impl<'a> PropertyDeclarationId<'a> {
    /// Returns PropertyFlags for given property.
    #[inline(always)]
    pub fn flags(&self) -> PropertyFlags {
        match self {
            Self::Longhand(id) => id.flags(),
            Self::Custom(_) => PropertyFlags::empty(),
        }
    }

    /// Convert to an OwnedPropertyDeclarationId.
    pub fn to_owned(&self) -> OwnedPropertyDeclarationId {
        match self {
            PropertyDeclarationId::Longhand(id) => OwnedPropertyDeclarationId::Longhand(*id),
            PropertyDeclarationId::Custom(name) => {
                OwnedPropertyDeclarationId::Custom((*name).clone())
            },
        }
    }

    /// Whether a given declaration id is either the same as `other`, or a
    /// longhand of it.
    pub fn is_or_is_longhand_of(&self, other: &PropertyId) -> bool {
        match *self {
            PropertyDeclarationId::Longhand(id) => match *other {
                PropertyId::NonCustom(non_custom_id) => id.is_or_is_longhand_of(non_custom_id),
                PropertyId::Custom(_) => false,
            },
            PropertyDeclarationId::Custom(name) => {
                matches!(*other, PropertyId::Custom(ref other_name) if name == other_name)
            },
        }
    }

    /// Whether a given declaration id is a longhand belonging to this
    /// shorthand.
    pub fn is_longhand_of(&self, shorthand: ShorthandId) -> bool {
        match *self {
            PropertyDeclarationId::Longhand(ref id) => id.is_longhand_of(shorthand),
            _ => false,
        }
    }

    /// Returns the name of the property without CSS escaping.
    pub fn name(&self) -> Cow<'static, str> {
        match *self {
            PropertyDeclarationId::Longhand(id) => id.name().into(),
            PropertyDeclarationId::Custom(name) => {
                let mut s = String::new();
                write!(&mut s, "--{}", name).unwrap();
                s.into()
            },
        }
    }

    /// Returns longhand id if it is, None otherwise.
    #[inline]
    pub fn as_longhand(&self) -> Option<LonghandId> {
        match *self {
            PropertyDeclarationId::Longhand(id) => Some(id),
            _ => None,
        }
    }

    /// Return whether this property is logical.
    #[inline]
    pub fn is_logical(&self) -> bool {
        match self {
            PropertyDeclarationId::Longhand(id) => id.is_logical(),
            PropertyDeclarationId::Custom(_) => false,
        }
    }

    /// If this is a logical property, return the corresponding physical one in
    /// the given writing mode.
    ///
    /// Otherwise, return unchanged.
    #[inline]
    pub fn to_physical(&self, wm: WritingMode) -> Self {
        match self {
            Self::Longhand(id) => Self::Longhand(id.to_physical(wm)),
            Self::Custom(_) => *self,
        }
    }

    /// Returns whether this property is animatable.
    #[inline]
    pub fn is_animatable(&self) -> bool {
        match self {
            Self::Longhand(id) => id.is_animatable(),
            Self::Custom(_) => true,
        }
    }

    /// Returns whether this property is animatable in a discrete way.
    #[inline]
    pub fn is_discrete_animatable(&self) -> bool {
        match self {
            Self::Longhand(longhand) => longhand.is_discrete_animatable(),
            // TODO(bug 1885995): Refine this.
            Self::Custom(_) => true,
        }
    }

    /// Converts from a to an adequate NonCustomCSSPropertyId, returning
    /// eCSSPropertyExtra_variable for custom properties.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_noncustomcsspropertyid(self) -> NonCustomCSSPropertyId {
        match self {
            PropertyDeclarationId::Longhand(id) => id.to_noncustomcsspropertyid(),
            PropertyDeclarationId::Custom(_) => NonCustomCSSPropertyId::eCSSPropertyExtra_variable,
        }
    }

    /// Convert a `PropertyDeclarationId` into an `CSSPropertyId`
    ///
    /// FIXME(emilio, bug 1870107): We should consider using cbindgen to generate the property id
    /// representation or so.
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn to_gecko_css_property_id(&self) -> CSSPropertyId {
        match self {
            Self::Longhand(id) => CSSPropertyId {
                mId: id.to_noncustomcsspropertyid(),
                mCustomName: RefPtr::null(),
            },
            Self::Custom(name) => {
                let mut property_id = CSSPropertyId {
                    mId: NonCustomCSSPropertyId::eCSSPropertyExtra_variable,
                    mCustomName: RefPtr::null(),
                };
                property_id.mCustomName.mRawPtr = (*name).clone().into_addrefed();
                property_id
            },
        }
    }
}

/// A trait for property-id-like types that can be stored compactly in an
/// `IdSet` bitfield.
pub trait IndexedId: Copy {
    /// The number of distinct ids, i.e. the number of bits the set needs.
    const COUNT: usize;
    /// Builds an id from its index in the set. The caller must guarantee that
    /// `index < Self::COUNT`.
    unsafe fn from_index_release_unchecked(index: usize) -> Self;
    /// Returns the index of this id in the set.
    fn to_index(self) -> usize;
}

impl IndexedId for NonCustomPropertyId {
    const COUNT: usize = property_counts::NON_CUSTOM;

    #[inline(always)]
    unsafe fn from_index_release_unchecked(index: usize) -> Self {
        debug_assert!(index < Self::COUNT);
        NonCustomPropertyId(index as u16)
    }

    #[inline(always)]
    fn to_index(self) -> usize {
        self.0 as usize
    }
}

impl IndexedId for PrioritaryPropertyId {
    const COUNT: usize = property_counts::PRIORITARY;

    #[inline(always)]
    unsafe fn from_index_release_unchecked(index: usize) -> Self {
        unsafe {
            debug_assert!(index < Self::COUNT);
            std::mem::transmute(index as u8)
        }
    }

    #[inline(always)]
    fn to_index(self) -> usize {
        self as usize
    }
}

impl IndexedId for LonghandId {
    const COUNT: usize = property_counts::LONGHANDS;

    #[inline(always)]
    unsafe fn from_index_release_unchecked(index: usize) -> Self {
        unsafe {
            debug_assert!(index < Self::COUNT);
            std::mem::transmute(index as u16)
        }
    }

    #[inline(always)]
    fn to_index(self) -> usize {
        self as usize
    }
}

/// A set of non-custom properties.
pub type NonCustomPropertyIdSet =
    IdSet<NonCustomPropertyId, { (property_counts::NON_CUSTOM - 1 + 32) / 32 }>;
/// An iterator over non-custom properties.
pub type NonCustomPropertyIdSetIterator<'a> = IdSetIterator<'a, NonCustomPropertyId>;
/// A set of prioritary properties.
pub type PrioritaryPropertyIdSet =
    IdSet<PrioritaryPropertyId, { (property_counts::PRIORITARY - 1 + 32) / 32 }>;
/// An iterator over prioritary properties.
pub type PrioritaryPropertyIdSetIterator<'a> = IdSetIterator<'a, PrioritaryPropertyId>;
/// A set of longhand properties.
pub type LonghandIdSet = IdSet<LonghandId, { (property_counts::LONGHANDS - 1 + 32) / 32 }>;
/// An iterator over longhand properties.
pub type LonghandIdSetIterator<'a> = IdSetIterator<'a, LonghandId>;

/// A set of ids indexed in a bitfield. `W` is the number of `u32` chunks needed to store
/// `Id::COUNT` bits, and is filled in by the type aliases above.
///
/// TODO(emilio): It'd be nice for the const parameter to be COUNT (or even not be there and pull
/// from Id::COUNT), but that can't be done in stable rust yet, see:
/// https://github.com/rust-lang/rust/issues/76560
pub struct IdSet<Id: IndexedId, const W: usize> {
    storage: [u32; W],
    _phantom: std::marker::PhantomData<Id>,
}

impl<Id: IndexedId, const W: usize> Clone for IdSet<Id, W> {
    #[inline]
    fn clone(&self) -> Self {
        *self
    }
}

impl<Id: IndexedId, const W: usize> Copy for IdSet<Id, W> {}

impl<Id: IndexedId, const W: usize> Default for IdSet<Id, W> {
    #[inline]
    fn default() -> Self {
        Self {
            storage: [0; W],
            _phantom: std::marker::PhantomData,
        }
    }
}

impl<Id: IndexedId, const W: usize> PartialEq for IdSet<Id, W> {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        self.storage == other.storage
    }
}

impl<Id: IndexedId, const W: usize> fmt::Debug for IdSet<Id, W> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.storage.fmt(f)
    }
}

impl<Id: IndexedId, const W: usize> malloc_size_of::MallocSizeOf for IdSet<Id, W> {
    #[inline(always)]
    fn size_of(&self, _: &mut malloc_size_of::MallocSizeOfOps) -> usize {
        0
    }
}

impl<Id: IndexedId, const W: usize> IdSet<Id, W> {
    /// Creates an empty `IdSet`.
    #[inline]
    pub fn new() -> Self {
        Self::default()
    }

    /// Creates a set from its raw bitfield storage.
    pub(crate) const fn from_storage(storage: [u32; W]) -> Self {
        Self {
            storage,
            _phantom: std::marker::PhantomData,
        }
    }

    /// Insert an id in the set.
    #[inline]
    pub fn insert(&mut self, id: Id) {
        let bit = id.to_index();
        self.storage[bit / 32] |= 1 << (bit % 32);
    }

    /// Remove the given id from the set.
    #[inline]
    pub fn remove(&mut self, id: Id) {
        let bit = id.to_index();
        self.storage[bit / 32] &= !(1 << (bit % 32));
    }

    /// Return whether the given id is in the set.
    #[inline]
    pub fn contains(&self, id: Id) -> bool {
        let bit = id.to_index();
        (self.storage[bit / 32] & (1 << (bit % 32))) != 0
    }

    /// Iterate over the current id set.
    pub fn iter(&self) -> IdSetIterator<'_, Id> {
        IdSetIterator {
            chunks: &self.storage,
            cur_chunk: 0,
            cur_bit: 0,
            _phantom: std::marker::PhantomData,
        }
    }

    /// Returns whether this set contains at least every id that `other` also contains.
    pub fn contains_all(&self, other: &Self) -> bool {
        for (self_cell, other_cell) in self.storage.iter().zip(other.storage.iter()) {
            if (*self_cell & *other_cell) != *other_cell {
                return false;
            }
        }
        true
    }

    /// Returns whether this set contains any id that `other` also contains.
    pub fn contains_any(&self, other: &Self) -> bool {
        for (self_cell, other_cell) in self.storage.iter().zip(other.storage.iter()) {
            if (*self_cell & *other_cell) != 0 {
                return true;
            }
        }
        false
    }

    /// Remove all the given ids from the set.
    #[inline]
    pub fn remove_all(&mut self, other: &Self) {
        for (self_cell, other_cell) in self.storage.iter_mut().zip(other.storage.iter()) {
            *self_cell &= !*other_cell;
        }
    }

    /// Clear all bits
    #[inline]
    pub fn clear(&mut self) {
        for cell in &mut self.storage {
            *cell = 0
        }
    }

    /// Returns whether the set is empty.
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.storage.iter().all(|c| *c == 0)
    }
}

to_shmem::impl_trivial_to_shmem!(LonghandIdSet);
impl LonghandIdSet {
    /// Return whether this set contains any reset longhand.
    #[inline]
    pub fn contains_any_reset(&self) -> bool {
        self.contains_any(Self::reset())
    }
}

/// An iterator over a set of ids.
pub struct IdSetIterator<'a, Id: IndexedId> {
    chunks: &'a [u32],
    cur_chunk: u32,
    cur_bit: u32, // [0..31], note that zero means the end-most bit
    _phantom: std::marker::PhantomData<Id>,
}

impl<'a, Id: IndexedId> Iterator for IdSetIterator<'a, Id> {
    type Item = Id;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            debug_assert!(self.cur_bit < 32);
            let cur_chunk = self.cur_chunk;
            let cur_bit = self.cur_bit;
            let chunk = *self.chunks.get(cur_chunk as usize)?;
            let next_bit = (chunk >> cur_bit).trailing_zeros();
            if next_bit == 32 {
                // Totally empty chunk, skip it.
                self.cur_bit = 0;
                self.cur_chunk += 1;
                continue;
            }
            debug_assert!(cur_bit + next_bit < 32);
            let index = (cur_chunk * 32 + cur_bit + next_bit) as usize;
            debug_assert!(index < Id::COUNT);
            let id = unsafe { Id::from_index_release_unchecked(index) };
            self.cur_bit += next_bit + 1;
            if self.cur_bit == 32 {
                self.cur_bit = 0;
                self.cur_chunk += 1;
            }
            return Some(id);
        }
    }
}

/// An ArrayVec of subproperties, contains space for the longest shorthand except all.
pub type SubpropertiesVec<T> = ArrayVec<T, { property_counts::MAX_SHORTHAND_EXPANDED }>;

/// A stack-allocated vector of `PropertyDeclaration`
/// large enough to parse one CSS `key: value` declaration.
/// (Shorthands expand to multiple `PropertyDeclaration`s.)
#[derive(Default)]
pub struct SourcePropertyDeclaration {
    /// The storage for the actual declarations (except for all).
    pub declarations: SubpropertiesVec<PropertyDeclaration>,
    /// Stored separately to keep SubpropertiesVec smaller.
    pub all_shorthand: AllShorthand,
}

// This is huge, but we allocate it on the stack and then never move it,
// we only pass `&mut SourcePropertyDeclaration` references around.
#[cfg(feature = "gecko")]
size_of_test!(SourcePropertyDeclaration, 632);
#[cfg(feature = "servo")]
size_of_test!(SourcePropertyDeclaration, 568);

impl SourcePropertyDeclaration {
    /// Create one with a single PropertyDeclaration.
    #[inline]
    pub fn with_one(decl: PropertyDeclaration) -> Self {
        let mut result = Self::default();
        result.declarations.push(decl);
        result
    }

    /// Similar to Vec::drain: leaves this empty when the return value is dropped.
    pub fn drain(&mut self) -> SourcePropertyDeclarationDrain<'_> {
        SourcePropertyDeclarationDrain {
            declarations: self.declarations.drain(..),
            all_shorthand: mem::replace(&mut self.all_shorthand, AllShorthand::NotSet),
        }
    }

    /// Reset to initial state
    pub fn clear(&mut self) {
        self.declarations.clear();
        self.all_shorthand = AllShorthand::NotSet;
    }

    /// Whether we're empty.
    pub fn is_empty(&self) -> bool {
        self.declarations.is_empty() && matches!(self.all_shorthand, AllShorthand::NotSet)
    }

    /// Push a single declaration.
    pub fn push(&mut self, declaration: PropertyDeclaration) {
        let _result = self.declarations.try_push(declaration);
        debug_assert!(_result.is_ok());
    }
}

/// Return type of SourcePropertyDeclaration::drain
pub struct SourcePropertyDeclarationDrain<'a> {
    /// A drain over the non-all declarations.
    pub declarations:
        ArrayVecDrain<'a, PropertyDeclaration, { property_counts::MAX_SHORTHAND_EXPANDED }>,
    /// The all shorthand that was set.
    pub all_shorthand: AllShorthand,
}

/// An unparsed property value that contains `var()` functions.
#[derive(Debug, Eq, PartialEq, ToShmem)]
pub struct UnparsedValue {
    /// The variable value, references and so on.
    pub(super) variable_value: custom_properties::VariableValue,
    /// The shorthand this came from.
    from_shorthand: Option<ShorthandId>,
}

impl ToCss for UnparsedValue {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        // https://drafts.csswg.org/css-variables/#variables-in-shorthands
        if self.from_shorthand.is_none() {
            self.variable_value.to_css(dest)?;
        }
        Ok(())
    }
}

impl ToTyped for UnparsedValue {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        if self.from_shorthand.is_none() {
            self.variable_value.to_typed(dest)?;
            return Ok(());
        }
        Err(())
    }
}

/// A simple cache for properties that come from a shorthand and have variable
/// references.
///
/// This cache works because of the fact that you can't have competing values
/// for a given longhand coming from the same shorthand (but note that this is
/// why the shorthand needs to be part of the cache key).
pub type ShorthandsWithPropertyReferencesCache =
    FxHashMap<(ShorthandId, LonghandId), PropertyDeclaration>;

impl UnparsedValue {
    fn substitute_variables<'cache>(
        &self,
        longhand_id: LonghandId,
        substitution_functions: &ComputedSubstitutionFunctions,
        stylist: &Stylist,
        computed_context: &computed::Context,
        shorthand_cache: &'cache mut ShorthandsWithPropertyReferencesCache,
        attribute_tracker: &mut AttributeTracker,
    ) -> Cow<'cache, PropertyDeclaration> {
        let invalid_at_computed_value_time = || {
            let keyword = if longhand_id.inherited() {
                CSSWideKeyword::Inherit
            } else {
                CSSWideKeyword::Initial
            };
            Cow::Owned(PropertyDeclaration::css_wide_keyword(longhand_id, keyword))
        };

        if computed_context
            .builder
            .invalid_non_custom_properties
            .contains(longhand_id)
        {
            return invalid_at_computed_value_time();
        }

        if let Some(shorthand_id) = self.from_shorthand {
            let key = (shorthand_id, longhand_id);
            if shorthand_cache.contains_key(&key) {
                // FIXME: This double lookup should be avoidable, but rustc
                // doesn't like that, see:
                //
                // https://github.com/rust-lang/rust/issues/82146
                return Cow::Borrowed(&shorthand_cache[&key]);
            }
        }

        let SubstitutionResult { css, attr_taint } = match custom_properties::substitute(
            &self.variable_value,
            substitution_functions,
            stylist,
            computed_context,
            attribute_tracker,
        ) {
            Ok(css) => css,
            Err(..) => return invalid_at_computed_value_time(),
        };

        // As of this writing, only the base URL is used for property
        // values.
        //
        // NOTE(emilio): we intentionally pase `None` as the rule type here.
        // If something starts depending on it, it's probably a bug, since
        // it'd change how values are parsed depending on whether we're in a
        // @keyframes rule or not, for example... So think twice about
        // whether you want to do this!
        //
        // FIXME(emilio): ParsingMode is slightly fishy...
        let context = ParserContext::new(
            Origin::Author,
            &self.variable_value.url_data,
            None,
            ParsingMode::DEFAULT,
            computed_context.quirks_mode,
            /* namespaces = */ Default::default(),
            None,
            None,
            attr_taint,
        );

        let mut input = Parser::new(&css);
        input.skip_whitespace();

        if let Ok(keyword) = input.try_parse(CSSWideKeyword::parse) {
            return Cow::Owned(PropertyDeclaration::css_wide_keyword(longhand_id, keyword));
        }

        let shorthand = match self.from_shorthand {
            None => {
                return match input.parse_entirely(|input| longhand_id.parse_value(&context, input))
                {
                    Ok(decl) => Cow::Owned(decl),
                    Err(..) => invalid_at_computed_value_time(),
                }
            },
            Some(shorthand) => shorthand,
        };

        let mut decls = SourcePropertyDeclaration::default();
        // parse_into takes care of doing `parse_entirely` for us.
        if shorthand
            .parse_into(&mut decls, &context, &mut input)
            .is_err()
        {
            return invalid_at_computed_value_time();
        }

        for declaration in decls.declarations.drain(..) {
            let longhand = declaration.id().as_longhand().unwrap();
            if longhand.is_logical() {
                let writing_mode = computed_context.builder.writing_mode;
                shorthand_cache.insert(
                    (shorthand, longhand.to_physical(writing_mode)),
                    declaration.clone(),
                );
            }
            shorthand_cache.insert((shorthand, longhand), declaration);
        }

        let key = (shorthand, longhand_id);
        match shorthand_cache.get(&key) {
            Some(decl) => Cow::Borrowed(decl),
            // NOTE: Under normal circumstances we should always have a value, but when prefs
            // change we might hit this case. Consider something like `animation-timeline`, which
            // is a conditionally-enabled longhand of `animation`:
            //
            // If we have a sheet with `animation: var(--foo)`, and the `animation-timeline` pref
            // enabled, then that expands to an `animation-timeline` declaration at parse time.
            //
            // If the user disables the pref and, some time later, we get here wanting to compute
            // `animation-timeline`, parse_into won't generate any declaration for it anymore, so
            // we haven't inserted in the cache. Computing to invalid / initial seems like the most
            // sensible thing to do here.
            None => invalid_at_computed_value_time(),
        }
    }
}
/// A parsed all-shorthand value.
pub enum AllShorthand {
    /// Not present.
    NotSet,
    /// A CSS-wide keyword.
    CSSWideKeyword(CSSWideKeyword),
    /// An all shorthand with var() references that we can't resolve right now.
    WithVariables(Arc<UnparsedValue>),
}

impl Default for AllShorthand {
    fn default() -> Self {
        Self::NotSet
    }
}

impl AllShorthand {
    /// Iterates property declarations from the given all shorthand value.
    #[inline]
    pub fn declarations(&self) -> AllShorthandDeclarationIterator<'_> {
        AllShorthandDeclarationIterator {
            all_shorthand: self,
            longhands: ShorthandId::All.longhands(),
        }
    }
}

/// An iterator over the all shorthand's shorthand declarations.
pub struct AllShorthandDeclarationIterator<'a> {
    all_shorthand: &'a AllShorthand,
    longhands: NonCustomPropertyIterator<LonghandId>,
}

impl<'a> Iterator for AllShorthandDeclarationIterator<'a> {
    type Item = PropertyDeclaration;

    #[inline]
    fn next(&mut self) -> Option<Self::Item> {
        match *self.all_shorthand {
            AllShorthand::NotSet => None,
            AllShorthand::CSSWideKeyword(ref keyword) => Some(
                PropertyDeclaration::css_wide_keyword(self.longhands.next()?, *keyword),
            ),
            AllShorthand::WithVariables(ref unparsed) => {
                Some(PropertyDeclaration::WithVariables(VariableDeclaration {
                    id: self.longhands.next()?,
                    value: unparsed.clone(),
                }))
            },
        }
    }
}

/// An iterator over all the property ids that are enabled for a given
/// shorthand, if that shorthand is enabled for all content too.
pub struct NonCustomPropertyIterator<Item: 'static> {
    filter: bool,
    iter: std::slice::Iter<'static, Item>,
}

impl<Item> Iterator for NonCustomPropertyIterator<Item>
where
    Item: 'static + Copy + Into<NonCustomPropertyId>,
{
    type Item = Item;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let id = *self.iter.next()?;
            if !self.filter || id.into().enabled_for_all_content() {
                return Some(id);
            }
        }
    }
}

/// An iterator over all the properties that transition on a given style.
pub struct TransitionPropertyIterator<'a> {
    style: &'a ComputedValues,
    index_range: core::ops::Range<usize>,
    longhand_iterator: Option<NonCustomPropertyIterator<LonghandId>>,
}

impl<'a> TransitionPropertyIterator<'a> {
    /// Create a `TransitionPropertyIterator` for the given style.
    pub fn from_style(style: &'a ComputedValues) -> Self {
        Self {
            style,
            index_range: 0..style.get_ui().transition_property_count(),
            longhand_iterator: None,
        }
    }
}

/// A single iteration of the TransitionPropertyIterator.
pub struct TransitionPropertyIteration {
    /// The id of the longhand for this property.
    pub property: OwnedPropertyDeclarationId,
    /// The index of this property in the list of transition properties for this iterator's
    /// style.
    pub index: usize,
}

impl<'a> Iterator for TransitionPropertyIterator<'a> {
    type Item = TransitionPropertyIteration;

    fn next(&mut self) -> Option<Self::Item> {
        use crate::values::computed::TransitionProperty;
        loop {
            if let Some(ref mut longhand_iterator) = self.longhand_iterator {
                if let Some(longhand_id) = longhand_iterator.next() {
                    return Some(TransitionPropertyIteration {
                        property: OwnedPropertyDeclarationId::Longhand(longhand_id),
                        index: self.index_range.start - 1,
                    });
                }
                self.longhand_iterator = None;
            }

            let index = self.index_range.next()?;
            match self.style.get_ui().transition_property_at(index) {
                TransitionProperty::NonCustom(id) => {
                    match id.longhand_or_shorthand() {
                        Ok(longhand_id) => {
                            return Some(TransitionPropertyIteration {
                                property: OwnedPropertyDeclarationId::Longhand(longhand_id),
                                index,
                            });
                        },
                        Err(shorthand_id) => {
                            // In the other cases, we set up our state so that we are ready to
                            // compute the next value of the iterator and then loop (equivalent
                            // to calling self.next()).
                            self.longhand_iterator = Some(shorthand_id.longhands());
                        },
                    }
                },
                TransitionProperty::Custom(name) => {
                    return Some(TransitionPropertyIteration {
                        property: OwnedPropertyDeclarationId::Custom(name),
                        index,
                    })
                },
                TransitionProperty::Unsupported(..) => {},
            }
        }
    }
}
