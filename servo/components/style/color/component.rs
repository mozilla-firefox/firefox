/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Parse/serialize and resolve a single color component.

use std::fmt::Write;

use super::{AbsoluteColor, parsing::ChannelKeyword};
use crate::derives::*;
use crate::typed_om::NumericType;
use crate::{
    parser::ParserContext,
    values::{
        animated::ToAnimatedValue,
        computed,
        specified::calc::{CalcParseFlags, Leaf, PercentageContext, SpecifiedCalcNode},
    },
};
use cssparser::{Parser, Token, color::OPAQUE};
use style_traits::{ParseError, StyleParseErrorKind, ToCss};

/// A single color component.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
#[repr(u8)]
pub enum ColorComponent<ValueType> {
    /// The "none" keyword.
    None,
    /// A absolute value.
    Value(ValueType),
    /// A channel keyword, e.g. `r`, `l`, `alpha`, etc.
    ChannelKeyword(ChannelKeyword),
    /// A calc() value.
    Calc(Box<SpecifiedCalcNode>),
    /// Used when alpha components are not specified.
    AlphaOmitted,
}

impl<ValueType> ColorComponent<ValueType> {
    /// Return true if the component is "none".
    #[inline]
    pub fn is_none(&self) -> bool {
        matches!(self, Self::None)
    }
}

/// An utility trait that allows the construction of [ColorComponent]
/// `ValueType`'s after parsing a color component.
pub trait ColorComponentType: Sized + Clone {
    // TODO(tlouw): This function should be named according to the rules in the spec
    //              stating that all the values coming from color components are
    //              numbers and that each has their own rules dependeing on types.
    /// Construct a new component from a single value.
    fn from_value(value: f32) -> Self;

    /// Returns whether the given numeric type is valid for this color component.
    fn is_valid_type(ty: &NumericType) -> bool;

    /// Try to create a new component from the given token.
    fn try_from_token(token: &Token) -> Result<Self, ()>;

    /// Try to create a new component from the given [CalcNodeLeaf] that was
    /// resolved from a [SpecifiedCalcNode].
    fn try_from_leaf(leaf: &Leaf) -> Result<Self, ()>;
}

impl<ValueType: ColorComponentType> ColorComponent<ValueType> {
    /// Parse a single [ColorComponent].
    pub fn parse(
        context: &ParserContext,
        input: &mut Parser,
        allow_none: bool,
        allowed_channel_keywords: ChannelKeyword,
        percentage_context: PercentageContext,
    ) -> Result<Self, ParseError> {
        match *input.next()? {
            Token::Ident(ref value) if allow_none && value.eq_ignore_ascii_case("none") => {
                Ok(ColorComponent::None)
            },
            Token::Ident(ref ident) => Ok(match ChannelKeyword::from_ident(ident) {
                Ok(channel_keyword) if allowed_channel_keywords.contains(channel_keyword) => {
                    ColorComponent::ChannelKeyword(channel_keyword)
                },
                _ => return Err(ParseError::unexpected_token()),
            }),
            Token::Function(ref name) => {
                let function = SpecifiedCalcNode::math_function(context, name)?;
                let mut flags = CalcParseFlags::new(percentage_context);
                flags.color_components = allowed_channel_keywords;
                let mut node = SpecifiedCalcNode::parse(context, input, function, flags)?;
                node.simplify_and_sort();
                if !node
                    .numeric_type()
                    .is_ok_and(|ty| ValueType::is_valid_type(&ty))
                {
                    return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
                }
                Ok(Self::Calc(Box::new(node)))
            },
            ref t => ValueType::try_from_token(t)
                .map(Self::Value)
                .map_err(|_| ParseError::unexpected_token()),
        }
    }

    /// Compute the component's value against `context`, substituting color
    /// channel references with the matching channel of `origin_color` when it is
    /// provided (and already converted into this function's color space).
    ///
    /// If no (absolute) origin color is available, channel references are kept
    /// intact, so the component can still be resolved later at use-value time.
    pub fn to_computed_value(
        &self,
        context: Option<&computed::Context>,
        origin_color: Option<&AbsoluteColor>,
    ) -> Result<Self, ()> {
        Ok(match self {
            Self::None => Self::None,
            Self::Value(v) => Self::Value(v.clone()),
            Self::ChannelKeyword(channel_keyword) => {
                let channel_value = origin_color
                    .and_then(|c| c.get_component_by_channel_keyword(*channel_keyword).ok());
                match channel_value {
                    Some(value) => Self::Value(ValueType::from_value(value.unwrap_or(0.0))),
                    // If the color needs to get resolved later (there's no absolute origin color),
                    // or the channel is invalid, we still keep the channel keyword around.
                    None => Self::ChannelKeyword(*channel_keyword),
                }
            },
            Self::Calc(node) => {
                // Try to compute, substitute channels and fold the calc tree in a single pass. If
                // it resolves to a concrete value, collapse to a value; otherwise keep the computed
                // (still symbolic) calc tree.
                let mut computed = node.to_computed_value(context, origin_color)?;
                computed.simplify_and_sort();
                if let Some(leaf) = computed.as_leaf()
                    && let Ok(value) = ValueType::try_from_leaf(&leaf)
                {
                    Self::Value(value)
                } else {
                    Self::Calc(Box::new(computed))
                }
            },
            Self::AlphaOmitted => match origin_color {
                // <https://drafts.csswg.org/css-color-5/#rcs-intro>
                // If the alpha value of the relative color is omitted, it
                // defaults to that of the origin color (rather than defaulting to
                // 100%, as it does in the absolute syntax).
                Some(origin_color) => match origin_color.alpha() {
                    Some(alpha) => Self::Value(ValueType::from_value(alpha)),
                    None => Self::None,
                },
                None => Self::AlphaOmitted,
            },
        })
    }

    /// Resolve an already-computed [ColorComponent] into a float. None is
    /// "none". This assumes color channel references have already been
    /// substituted by [`to_computed_value`], and so does not require an origin
    /// color.
    pub fn resolve(&self) -> Result<Option<ValueType>, ()> {
        Ok(match self {
            Self::None => None,
            Self::Value(value) => Some(value.clone()),
            // An unsubstituted channel reference can't be resolved without an
            // origin color.
            Self::ChannelKeyword(_) => return Err(()),
            Self::Calc(node) => Some(ValueType::try_from_leaf(&node.resolve()?)?),
            Self::AlphaOmitted => Some(ValueType::from_value(OPAQUE)),
        })
    }
}

impl<ValueType: ToCss> ToCss for ColorComponent<ValueType> {
    fn to_css<W>(&self, dest: &mut style_traits::CssWriter<W>) -> std::fmt::Result
    where
        W: Write,
    {
        match self {
            ColorComponent::None => dest.write_str("none")?,
            ColorComponent::Value(value) => value.to_css(dest)?,
            ColorComponent::ChannelKeyword(channel_keyword) => channel_keyword.to_css(dest)?,
            ColorComponent::Calc(node) => {
                // When we only have a channel keyword in a leaf node, we should serialize it with
                // calc(..), except when one of the rgb color space functions are used, e.g.
                // rgb(..), hsl(..) or hwb(..) for historical reasons.
                // <https://github.com/web-platform-tests/wpt/issues/47921>
                node.to_css(dest)?;
            },
            ColorComponent::AlphaOmitted => {
                debug_assert!(false, "can't serialize an omitted alpha component");
            },
        }

        Ok(())
    }
}

impl<ValueType> ToAnimatedValue for ColorComponent<ValueType> {
    type AnimatedValue = Self;

    fn to_animated_value(self, _context: &crate::values::animated::Context) -> Self::AnimatedValue {
        self
    }

    fn from_animated_value(animated: Self::AnimatedValue) -> Self {
        animated
    }
}
