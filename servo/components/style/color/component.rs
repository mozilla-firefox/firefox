/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Parse/serialize and resolve a single color component.

use std::fmt::Write;

use super::{
    parsing::{rcs_enabled, ChannelKeyword},
    AbsoluteColor,
};
use crate::derives::*;
use crate::{
    parser::ParserContext,
    values::{
        animated::ToAnimatedValue,
        generics::calc::{CalcUnits, GenericCalcNode},
        specified::calc::{AllowParse, Leaf},
        specified::number::NoCalcNumber,
    },
};
use cssparser::{color::OPAQUE, Parser, Token};
use style_traits::{ParseError, ToCss};

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
    Calc(Box<GenericCalcNode<Leaf>>),
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

    /// Return the [CalcUnits] flags that the impl can handle.
    fn units() -> CalcUnits;

    /// Try to create a new component from the given token.
    fn try_from_token(token: &Token) -> Result<Self, ()>;

    /// Try to create a new component from the given [CalcNodeLeaf] that was
    /// resolved from a [CalcNode].
    fn try_from_leaf(leaf: &Leaf) -> Result<Self, ()>;
}

impl<ValueType: ColorComponentType> ColorComponent<ValueType> {
    /// Parse a single [ColorComponent].
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        allow_none: bool,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();

        match *input.next()? {
            Token::Ident(ref value) if allow_none && value.eq_ignore_ascii_case("none") => {
                Ok(ColorComponent::None)
            },
            ref t @ Token::Ident(ref ident) => {
                let Ok(channel_keyword) = ChannelKeyword::from_ident(ident) else {
                    return Err(location.new_unexpected_token_error(t.clone()));
                };
                Ok(ColorComponent::ChannelKeyword(channel_keyword))
            },
            Token::Function(ref name) => {
                let function = GenericCalcNode::math_function(context, name, location)?;
                let allow = AllowParse::new(if rcs_enabled() {
                    ValueType::units() | CalcUnits::COLOR_COMPONENT
                } else {
                    ValueType::units()
                });
                let mut node = GenericCalcNode::parse(context, input, function, allow)?;

                // TODO(tlouw): We only have to simplify the node when we have to store it, but we
                //              only know if we have to store it much later when the whole color
                //              can't be resolved to absolute at which point the calc nodes are
                //              burried deep in a [ColorFunction] struct.
                node.simplify_and_sort();

                Ok(Self::Calc(Box::new(node)))
            },
            ref t => ValueType::try_from_token(t)
                .map(Self::Value)
                .map_err(|_| location.new_unexpected_token_error(t.clone())),
        }
    }

    /// Resolve a [ColorComponent] into a float.  None is "none".
    pub fn resolve(&self, origin_color: Option<&AbsoluteColor>) -> Result<Option<ValueType>, ()> {
        Ok(match self {
            ColorComponent::None => None,
            ColorComponent::Value(value) => Some(value.clone()),
            ColorComponent::ChannelKeyword(channel_keyword) => match origin_color {
                Some(origin_color) => {
                    let value = origin_color.get_component_by_channel_keyword(*channel_keyword)?;
                    Some(ValueType::from_value(value.unwrap_or(0.0)))
                },
                None => return Err(()),
            },
            ColorComponent::Calc(node) => {
                let Ok(resolved_leaf) = node.resolve_map(|leaf| {
                    Ok(match leaf {
                        Leaf::ColorComponent(channel_keyword) => match origin_color {
                            Some(origin_color) => {
                                let value = origin_color
                                    .get_component_by_channel_keyword(*channel_keyword)?;
                                Leaf::Number(NoCalcNumber::new(value.unwrap_or(0.0)))
                            },
                            None => return Err(()),
                        },
                        l => l.clone(),
                    })
                }) else {
                    return Err(());
                };

                Some(ValueType::try_from_leaf(&resolved_leaf)?)
            },
            ColorComponent::AlphaOmitted => {
                if let Some(origin_color) = origin_color {
                    // <https://drafts.csswg.org/css-color-5/#rcs-intro>
                    // If the alpha value of the relative color is omitted, it defaults to that of
                    // the origin color (rather than defaulting to 100%, as it does in the absolute
                    // syntax).
                    origin_color.alpha().map(ValueType::from_value)
                } else {
                    Some(ValueType::from_value(OPAQUE))
                }
            },
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
