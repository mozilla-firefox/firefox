/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the specified value of
//! [`basic-shape`][basic-shape]s
//!
//! [basic-shape]: https://drafts.csswg.org/css-shapes/#typedef-basic-shape

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::basic_shape::InsetRect as ComputedInsetRect;
use crate::values::computed::{
    Context, LengthPercentage as ComputedLengthPercentage, ToComputedValue,
};
use crate::values::generics::basic_shape as generic;
use crate::values::generics::basic_shape::{Path, PolygonCoord};
use crate::values::generics::position::GenericPositionOrAuto;
use crate::values::generics::rect::Rect;
use crate::values::specified::angle::Angle;
use crate::values::specified::border::BorderRadius;
use crate::values::specified::image::Image;
use crate::values::specified::length::LengthPercentageOrAuto;
use crate::values::specified::position::Position;
use crate::values::specified::url::SpecifiedUrl;
use crate::values::specified::{
    LengthPercentage, NoCalcPercentage, NonNegativeLengthPercentage, SVGPathData,
};
use crate::values::CSSFloat;
use crate::Zero;
use cssparser::{match_ignore_ascii_case, Parser};
use std::fmt::{self, Write};
use style_traits::{CssWriter, ParseError, StyleParseErrorKind, ToCss};

/// A specified alias for FillRule.
pub use crate::values::generics::basic_shape::FillRule;

/// A specified `clip-path` value.
pub type ClipPath = generic::GenericClipPath<BasicShape, SpecifiedUrl>;

/// A specified `shape-outside` value.
pub type ShapeOutside = generic::GenericShapeOutside<BasicShape, Image>;

/// A specified basic shape.
pub type BasicShape = generic::GenericBasicShape<Angle, Position, LengthPercentage, BasicShapeRect>;

/// The specified value of `inset()`.
pub type InsetRect = generic::GenericInsetRect<LengthPercentage>;

/// A specified circle.
pub type Circle = generic::Circle<Position, LengthPercentage>;

/// A specified ellipse.
pub type Ellipse = generic::Ellipse<Position, LengthPercentage>;

/// The specified value of `ShapeRadius`.
pub type ShapeRadius = generic::ShapeRadius<LengthPercentage>;

/// The specified value of `Polygon`.
pub type Polygon = generic::GenericPolygon<LengthPercentage>;

/// The specified value of `PathOrShapeFunction`.
pub type PathOrShapeFunction =
    generic::GenericPathOrShapeFunction<Angle, Position, LengthPercentage>;

/// The specified value of `ShapeCommand`.
pub type ShapeCommand = generic::GenericShapeCommand<Angle, Position, LengthPercentage>;

/// The specified value of `xywh()`.
/// Defines a rectangle via offsets from the top and left edge of the reference box, and a
/// specified width and height.
///
/// The four <length-percentage>s define, respectively, the inset from the left edge of the
/// reference box, the inset from the top edge of the reference box, the width of the rectangle,
/// and the height of the rectangle.
///
/// https://drafts.csswg.org/css-shapes-1/#funcdef-basic-shape-xywh
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToShmem)]
pub struct Xywh {
    /// The left edge of the reference box.
    pub x: LengthPercentage,
    /// The top edge of the reference box.
    pub y: LengthPercentage,
    /// The specified width.
    pub width: NonNegativeLengthPercentage,
    /// The specified height.
    pub height: NonNegativeLengthPercentage,
    /// The optional <border-radius> argument(s) define rounded corners for the inset rectangle
    /// using the border-radius shorthand syntax.
    pub round: BorderRadius,
}

/// Defines a rectangle via insets from the top and left edges of the reference box.
///
/// https://drafts.csswg.org/css-shapes-1/#funcdef-basic-shape-rect
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToShmem)]
#[repr(C)]
pub struct ShapeRectFunction {
    /// The four <length-percentage>s define the position of the top, right, bottom, and left edges
    /// of a rectangle, respectively, as insets from the top edge of the reference box (for the
    /// first and third values) or the left edge of the reference box (for the second and fourth
    /// values).
    ///
    /// An auto value makes the edge of the box coincide with the corresponding edge of the
    /// reference box: it’s equivalent to 0% as the first (top) or fourth (left) value, and
    /// equivalent to 100% as the second (right) or third (bottom) value.
    pub rect: Rect<LengthPercentageOrAuto>,
    /// The optional <border-radius> argument(s) define rounded corners for the inset rectangle
    /// using the border-radius shorthand syntax.
    pub round: BorderRadius,
}

/// The specified value of <basic-shape-rect>.
/// <basic-shape-rect> = <inset()> | <rect()> | <xywh()>
///
/// https://drafts.csswg.org/css-shapes-1/#supported-basic-shapes
#[derive(Clone, Debug, MallocSizeOf, PartialEq, SpecifiedValueInfo, ToCss, ToShmem)]
pub enum BasicShapeRect {
    /// Defines an inset rectangle via insets from each edge of the reference box.
    Inset(InsetRect),
    /// Defines a xywh function.
    #[css(function)]
    Xywh(Xywh),
    /// Defines a rect function.
    #[css(function)]
    Rect(ShapeRectFunction),
}

/// For filled shapes, we use fill-rule, and store it for path() and polygon().
/// For outline shapes, we should ignore fill-rule.
///
/// https://github.com/w3c/fxtf-drafts/issues/512
/// https://github.com/w3c/csswg-drafts/issues/7390
/// https://github.com/w3c/csswg-drafts/issues/3468
pub enum ShapeType {
    /// The CSS property uses filled shapes. The default behavior.
    Filled,
    /// The CSS property uses outline shapes. This is especially useful for offset-path.
    Outline,
}

bitflags! {
    /// The flags to represent which basic shapes we would like to support.
    ///
    /// Different properties may use different subsets of <basic-shape>:
    /// e.g.
    /// clip-path: all basic shapes.
    /// motion-path: all basic shapes (but ignore fill-rule).
    /// shape-outside: inset(), circle(), ellipse(), polygon().
    ///
    /// Also there are some properties we don't support for now:
    /// shape-inside: inset(), circle(), ellipse(), polygon().
    /// SVG shape-inside and shape-subtract: circle(), ellipse(), polygon().
    ///
    /// The spec issue proposes some better ways to clarify the usage of basic shapes, so for now
    /// we use the bitflags to choose the supported basic shapes for each property at the parse
    /// time.
    /// https://github.com/w3c/csswg-drafts/issues/7390
    #[derive(Clone, Copy)]
    #[repr(C)]
    pub struct AllowedBasicShapes: u8 {
        /// inset().
        const INSET = 1 << 0;
        /// xywh().
        const XYWH = 1 << 1;
        /// rect().
        const RECT = 1 << 2;
        /// circle().
        const CIRCLE = 1 << 3;
        /// ellipse().
        const ELLIPSE = 1 << 4;
        /// polygon().
        const POLYGON = 1 << 5;
        /// path().
        const PATH = 1 << 6;
        /// shape().
        const SHAPE = 1 << 7;

        /// All flags.
        const ALL =
            Self::INSET.bits() |
            Self::XYWH.bits() |
            Self::RECT.bits() |
            Self::CIRCLE.bits() |
            Self::ELLIPSE.bits() |
            Self::POLYGON.bits() |
            Self::PATH.bits() |
            Self::SHAPE.bits();

        /// For shape-outside.
        const SHAPE_OUTSIDE =
            Self::INSET.bits() |
            Self::XYWH.bits() |
            Self::RECT.bits() |
            Self::CIRCLE.bits() |
            Self::ELLIPSE.bits() |
            Self::POLYGON.bits();
    }
}

/// A helper for both clip-path and shape-outside parsing of shapes.
fn parse_shape_or_box<'i, 't, R, ReferenceBox>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    to_shape: impl FnOnce(Box<BasicShape>, ReferenceBox) -> R,
    to_reference_box: impl FnOnce(ReferenceBox) -> R,
    flags: AllowedBasicShapes,
) -> Result<R, ParseError<'i>>
where
    ReferenceBox: Default + Parse,
{
    let mut shape = None;
    let mut ref_box = None;
    loop {
        if shape.is_none() {
            shape = input
                .try_parse(|i| BasicShape::parse(context, i, flags, ShapeType::Filled))
                .ok();
        }

        if ref_box.is_none() {
            ref_box = input.try_parse(|i| ReferenceBox::parse(context, i)).ok();
            if ref_box.is_some() {
                continue;
            }
        }
        break;
    }

    if let Some(shp) = shape {
        return Ok(to_shape(Box::new(shp), ref_box.unwrap_or_default()));
    }

    match ref_box {
        Some(r) => Ok(to_reference_box(r)),
        None => Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
    }
}

impl Parse for ClipPath {
    #[inline]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
            return Ok(ClipPath::None);
        }

        if let Ok(url) = input.try_parse(|i| SpecifiedUrl::parse(context, i)) {
            return Ok(ClipPath::Url(url));
        }

        parse_shape_or_box(
            context,
            input,
            ClipPath::Shape,
            ClipPath::Box,
            AllowedBasicShapes::ALL,
        )
    }
}

impl Parse for ShapeOutside {
    #[inline]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        // Need to parse this here so that `Image::parse_with_cors_anonymous`
        // doesn't parse it.
        if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
            return Ok(ShapeOutside::None);
        }

        if let Ok(image) = input.try_parse(|i| Image::parse_with_cors_anonymous(context, i)) {
            debug_assert_ne!(image, Image::None);
            return Ok(ShapeOutside::Image(image));
        }

        parse_shape_or_box(
            context,
            input,
            ShapeOutside::Shape,
            ShapeOutside::Box,
            AllowedBasicShapes::SHAPE_OUTSIDE,
        )
    }
}

impl BasicShape {
    /// Parse with some parameters.
    /// 1. The supported <basic-shape>.
    /// 2. The type of shapes. Should we ignore fill-rule?
    /// 3. The default value of `at <position>`.
    pub fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        flags: AllowedBasicShapes,
        shape_type: ShapeType,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let function = input.expect_function()?.clone();
        input.parse_nested_block(move |i| {
            match_ignore_ascii_case! { &function,
                "inset" if flags.contains(AllowedBasicShapes::INSET) => {
                    InsetRect::parse_function_arguments(context, i)
                        .map(BasicShapeRect::Inset)
                        .map(BasicShape::Rect)
                },
                "xywh" if flags.contains(AllowedBasicShapes::XYWH) => {
                    Xywh::parse_function_arguments(context, i)
                        .map(BasicShapeRect::Xywh)
                        .map(BasicShape::Rect)
                },
                "rect" if flags.contains(AllowedBasicShapes::RECT) => {
                    ShapeRectFunction::parse_function_arguments(context, i)
                        .map(BasicShapeRect::Rect)
                        .map(BasicShape::Rect)
                },
                "circle" if flags.contains(AllowedBasicShapes::CIRCLE) => {
                    Circle::parse_function_arguments(context, i)
                        .map(BasicShape::Circle)
                },
                "ellipse" if flags.contains(AllowedBasicShapes::ELLIPSE) => {
                    Ellipse::parse_function_arguments(context, i)
                        .map(BasicShape::Ellipse)
                },
                "polygon" if flags.contains(AllowedBasicShapes::POLYGON) => {
                    Polygon::parse_function_arguments(context, i, shape_type)
                        .map(BasicShape::Polygon)
                },
                "path" if flags.contains(AllowedBasicShapes::PATH) => {
                    Path::parse_function_arguments(i, shape_type)
                        .map(PathOrShapeFunction::Path)
                        .map(BasicShape::PathOrShape)
                },
                "shape"
                    if flags.contains(AllowedBasicShapes::SHAPE)
                        && static_prefs::pref!("layout.css.basic-shape-shape.enabled") =>
                {
                    generic::Shape::parse_function_arguments(context, i, shape_type)
                        .map(PathOrShapeFunction::Shape)
                        .map(BasicShape::PathOrShape)
                },
                _ => Err(location
                    .new_custom_error(StyleParseErrorKind::UnexpectedFunction(function.clone()))),
            }
        })
    }
}

impl Parse for InsetRect {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("inset")?;
        input.parse_nested_block(|i| Self::parse_function_arguments(context, i))
    }
}

fn parse_round<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
) -> Result<BorderRadius, ParseError<'i>> {
    if input
        .try_parse(|i| i.expect_ident_matching("round"))
        .is_ok()
    {
        return BorderRadius::parse(context, input);
    }

    Ok(BorderRadius::zero())
}

impl InsetRect {
    /// Parse the inner function arguments of `inset()`
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let rect = Rect::parse_with(context, input, LengthPercentage::parse)?;
        let round = parse_round(context, input)?;
        Ok(generic::InsetRect { rect, round })
    }
}

fn parse_at_position<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
) -> Result<GenericPositionOrAuto<Position>, ParseError<'i>> {
    if input.try_parse(|i| i.expect_ident_matching("at")).is_ok() {
        Position::parse(context, input).map(GenericPositionOrAuto::Position)
    } else {
        Ok(GenericPositionOrAuto::Auto)
    }
}

impl Parse for Circle {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("circle")?;
        input.parse_nested_block(|i| Self::parse_function_arguments(context, i))
    }
}

impl Circle {
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let radius = input
            .try_parse(|i| ShapeRadius::parse(context, i))
            .unwrap_or_default();
        let position = parse_at_position(context, input)?;

        Ok(generic::Circle { radius, position })
    }
}

impl Parse for Ellipse {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("ellipse")?;
        input.parse_nested_block(|i| Self::parse_function_arguments(context, i))
    }
}

impl Ellipse {
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let (semiaxis_x, semiaxis_y) = input
            .try_parse(|i| -> Result<_, ParseError> {
                Ok((
                    ShapeRadius::parse(context, i)?,
                    ShapeRadius::parse(context, i)?,
                ))
            })
            .unwrap_or_default();
        let position = parse_at_position(context, input)?;

        Ok(generic::Ellipse {
            semiaxis_x,
            semiaxis_y,
            position,
        })
    }
}

fn parse_fill_rule<'i, 't>(
    input: &mut Parser<'i, 't>,
    shape_type: ShapeType,
    expect_comma: bool,
) -> FillRule {
    match shape_type {
        // Per [1] and [2], we ignore `<fill-rule>` for outline shapes, so always use a default
        // value.
        // [1] https://github.com/w3c/csswg-drafts/issues/3468
        // [2] https://github.com/w3c/csswg-drafts/issues/7390
        //
        // Also, per [3] and [4], we would like the ignore `<file-rule>` from outline shapes, e.g.
        // offset-path, which means we don't parse it when setting `ShapeType::Outline`.
        // This should be web compatible because the shipped "offset-path:path()" doesn't have
        // `<fill-rule>` and "offset-path:polygon()" is a new feature and still behind the
        // preference.
        // [3] https://github.com/w3c/fxtf-drafts/issues/512#issuecomment-1545393321
        // [4] https://github.com/w3c/fxtf-drafts/issues/512#issuecomment-1555330929
        ShapeType::Outline => Default::default(),
        ShapeType::Filled => input
            .try_parse(|i| -> Result<_, ParseError> {
                let fill = FillRule::parse(i)?;
                if expect_comma {
                    i.expect_comma()?;
                }
                Ok(fill)
            })
            .unwrap_or_default(),
    }
}

impl Parse for Polygon {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        input.expect_function_matching("polygon")?;
        input.parse_nested_block(|i| Self::parse_function_arguments(context, i, ShapeType::Filled))
    }
}

impl Polygon {
    /// Parse the inner arguments of a `polygon` function.
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        shape_type: ShapeType,
    ) -> Result<Self, ParseError<'i>> {
        let fill = parse_fill_rule(input, shape_type, true /* has comma */);
        let coordinates = input
            .parse_comma_separated(|i| {
                Ok(PolygonCoord(
                    LengthPercentage::parse(context, i)?,
                    LengthPercentage::parse(context, i)?,
                ))
            })?
            .into();

        Ok(Polygon { fill, coordinates })
    }
}

impl Path {
    /// Parse the inner arguments of a `path` function.
    fn parse_function_arguments<'i, 't>(
        input: &mut Parser<'i, 't>,
        shape_type: ShapeType,
    ) -> Result<Self, ParseError<'i>> {
        use crate::values::specified::svg_path::AllowEmpty;

        let fill = parse_fill_rule(input, shape_type, true /* has comma */);
        let path = SVGPathData::parse(input, AllowEmpty::No)?;
        Ok(Path { fill, path })
    }
}

fn round_to_css<W>(round: &BorderRadius, dest: &mut CssWriter<W>) -> fmt::Result
where
    W: Write,
{
    if !round.is_zero() {
        dest.write_str(" round ")?;
        round.to_css(dest)?;
    }
    Ok(())
}

impl ToCss for Xywh {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.x.to_css(dest)?;
        dest.write_char(' ')?;
        self.y.to_css(dest)?;
        dest.write_char(' ')?;
        self.width.to_css(dest)?;
        dest.write_char(' ')?;
        self.height.to_css(dest)?;
        round_to_css(&self.round, dest)
    }
}

impl Xywh {
    /// Parse the inner function arguments of `xywh()`.
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let x = LengthPercentage::parse(context, input)?;
        let y = LengthPercentage::parse(context, input)?;
        let width = NonNegativeLengthPercentage::parse(context, input)?;
        let height = NonNegativeLengthPercentage::parse(context, input)?;
        let round = parse_round(context, input)?;
        Ok(Xywh {
            x,
            y,
            width,
            height,
            round,
        })
    }
}

impl ToCss for ShapeRectFunction {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        self.rect.0.to_css(dest)?;
        dest.write_char(' ')?;
        self.rect.1.to_css(dest)?;
        dest.write_char(' ')?;
        self.rect.2.to_css(dest)?;
        dest.write_char(' ')?;
        self.rect.3.to_css(dest)?;
        round_to_css(&self.round, dest)
    }
}

impl ShapeRectFunction {
    /// Parse the inner function arguments of `rect()`.
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let rect = Rect::parse_all_components_with(context, input, LengthPercentageOrAuto::parse)?;
        let round = parse_round(context, input)?;
        Ok(ShapeRectFunction { rect, round })
    }
}

impl ToComputedValue for BasicShapeRect {
    type ComputedValue = ComputedInsetRect;

    #[inline]
    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        use crate::values::computed::LengthPercentage;
        use crate::values::computed::LengthPercentageOrAuto;
        use style_traits::values::specified::AllowedNumericType;

        match self {
            Self::Inset(ref inset) => inset.to_computed_value(context),
            Self::Xywh(ref xywh) => {
                // Given `xywh(x y w h)`, construct the equivalent inset() function,
                // `inset(y calc(100% - x - w) calc(100% - y - h) x)`.
                //
                // https://drafts.csswg.org/css-shapes-1/#basic-shape-computed-values
                // https://github.com/w3c/csswg-drafts/issues/9053
                let x = xywh.x.to_computed_value(context);
                let y = xywh.y.to_computed_value(context);
                let w = xywh.width.to_computed_value(context);
                let h = xywh.height.to_computed_value(context);
                // calc(100% - x - w).
                let right = LengthPercentage::hundred_percent_minus_list(
                    &[&x, &w.0],
                    AllowedNumericType::All,
                );
                // calc(100% - y - h).
                let bottom = LengthPercentage::hundred_percent_minus_list(
                    &[&y, &h.0],
                    AllowedNumericType::All,
                );

                ComputedInsetRect {
                    rect: Rect::new(y, right, bottom, x),
                    round: xywh.round.to_computed_value(context),
                }
            },
            Self::Rect(ref rect) => {
                // Given `rect(t r b l)`, the equivalent function is
                // `inset(t calc(100% - r) calc(100% - b) l)`.
                //
                // https://drafts.csswg.org/css-shapes-1/#basic-shape-computed-values
                fn compute_top_or_left(v: LengthPercentageOrAuto) -> LengthPercentage {
                    match v {
                        // it’s equivalent to 0% as the first (top) or fourth (left) value.
                        // https://drafts.csswg.org/css-shapes-1/#funcdef-basic-shape-rect
                        LengthPercentageOrAuto::Auto => LengthPercentage::zero_percent(),
                        LengthPercentageOrAuto::LengthPercentage(lp) => lp,
                    }
                }
                fn compute_bottom_or_right(v: LengthPercentageOrAuto) -> LengthPercentage {
                    match v {
                        // It's equivalent to 100% as the second (right) or third (bottom) value.
                        // So calc(100% - 100%) = 0%.
                        // https://drafts.csswg.org/css-shapes-1/#funcdef-basic-shape-rect
                        LengthPercentageOrAuto::Auto => LengthPercentage::zero_percent(),
                        LengthPercentageOrAuto::LengthPercentage(lp) => {
                            LengthPercentage::hundred_percent_minus(lp, AllowedNumericType::All)
                        },
                    }
                }

                let round = rect.round.to_computed_value(context);
                let rect = rect.rect.to_computed_value(context);
                let rect = Rect::new(
                    compute_top_or_left(rect.0),
                    compute_bottom_or_right(rect.1),
                    compute_bottom_or_right(rect.2),
                    compute_top_or_left(rect.3),
                );

                ComputedInsetRect { rect, round }
            },
        }
    }

    #[inline]
    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        Self::Inset(ToComputedValue::from_computed_value(computed))
    }
}

impl generic::Shape<Angle, Position, LengthPercentage> {
    /// Parse the inner arguments of a `shape` function.
    /// shape() = shape(<fill-rule>? from <coordinate-pair>, <shape-command>#)
    fn parse_function_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        shape_type: ShapeType,
    ) -> Result<Self, ParseError<'i>> {
        let fill = parse_fill_rule(input, shape_type, false /* no following comma */);

        let mut first = true;
        let commands = input.parse_comma_separated(|i| {
            if first {
                first = false;

                // The starting point for the first shape-command. It adds an initial absolute
                // moveto to the list of path data commands, with the <coordinate-pair> measured
                // from the top-left corner of the reference
                i.expect_ident_matching("from")?;
                Ok(ShapeCommand::Move {
                    point: generic::CommandEndPoint::parse_endpoint_as_abs(context, i)?,
                })
            } else {
                // The further path data commands.
                ShapeCommand::parse(context, i)
            }
        })?;

        // We must have one starting point and at least one following <shape-command>.
        if commands.len() < 2 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(Self {
            fill,
            commands: commands.into(),
        })
    }
}

impl Parse for ShapeCommand {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use crate::values::generics::basic_shape::{
            ArcRadii, ArcSize, ArcSweep, AxisEndPoint, CommandEndPoint, ControlPoint,
        };

        // <shape-command> = <move-command> | <line-command> | <hv-line-command> |
        //                   <curve-command> | <smooth-command> | <arc-command> | close
        Ok(try_match_ident_ignore_ascii_case! { input,
            "close" => Self::Close,
            "move" => {
                let point = CommandEndPoint::parse(context, input)?;
                Self::Move { point }
            },
            "line" => {
                let point = CommandEndPoint::parse(context, input)?;
                Self::Line { point }
            },
            "hline" => {
                let x = AxisEndPoint::parse_hline(context, input)?;
                Self::HLine { x }
            },
            "vline" => {
                let y = AxisEndPoint::parse_vline(context, input)?;
                Self::VLine { y }
            },
            "curve" => {
                let point = CommandEndPoint::parse(context, input)?;
                input.expect_ident_matching("with")?;
                let control1 = ControlPoint::parse(context, input, point.is_abs())?;
                if input.try_parse(|i| i.expect_delim('/')).is_ok() {
                    let control2 = ControlPoint::parse(context, input, point.is_abs())?;
                    Self::CubicCurve {
                        point,
                        control1,
                        control2,
                    }
                } else {
                    Self::QuadCurve {
                        point,
                        control1,
                    }
                }
            },
            "smooth" => {
                let point = CommandEndPoint::parse(context, input)?;
                if input.try_parse(|i| i.expect_ident_matching("with")).is_ok() {
                    let control2 = ControlPoint::parse(context, input, point.is_abs())?;
                    Self::SmoothCubic {
                        point,
                        control2,
                    }
                } else {
                    Self::SmoothQuad { point }
                }
            },
            "arc" => {
                let point = CommandEndPoint::parse(context, input)?;
                input.expect_ident_matching("of")?;
                let rx = LengthPercentage::parse(context, input)?;
                let ry = input.try_parse(|i| LengthPercentage::parse(context, i)).ok();
                let radii = ArcRadii { rx, ry: ry.into() };

                // [<arc-sweep> || <arc-size> || rotate <angle>]?
                let mut arc_sweep = None;
                let mut arc_size = None;
                let mut rotate = None;
                loop {
                    if arc_sweep.is_none() {
                        arc_sweep = input.try_parse(ArcSweep::parse).ok();
                    }

                    if arc_size.is_none() {
                        arc_size = input.try_parse(ArcSize::parse).ok();
                        if arc_size.is_some() {
                            continue;
                        }
                    }

                    if rotate.is_none()
                        && input
                            .try_parse(|i| i.expect_ident_matching("rotate"))
                            .is_ok()
                    {
                        rotate = Some(Angle::parse(context, input)?);
                        continue;
                    }
                    break;
                }
                Self::Arc {
                    point,
                    radii,
                    arc_sweep: arc_sweep.unwrap_or(ArcSweep::Ccw),
                    arc_size: arc_size.unwrap_or(ArcSize::Small),
                    rotate: rotate.unwrap_or(Angle::zero()),
                }
            },
        })
    }
}

impl Parse for generic::CoordinatePair<LengthPercentage> {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let x = LengthPercentage::parse(context, input)?;
        let y = LengthPercentage::parse(context, input)?;
        Ok(Self::new(x, y))
    }
}

impl generic::ControlPoint<Position, LengthPercentage> {
    /// Parse <control-point> = [ <position> | <relative-control-point> ]
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
        is_end_point_abs: bool,
    ) -> Result<Self, ParseError<'i>> {
        use generic::ControlReference;
        let coord = input.try_parse(|i| generic::CoordinatePair::parse(context, i));

        // Parse <position>
        if is_end_point_abs && coord.is_err() {
            let pos = Position::parse(context, input)?;
            return Ok(Self::Absolute(pos));
        }

        // Parse <relative-control-point> = <coordinate-pair> [from [ start | end | origin ]]?
        let coord = coord?;
        let mut reference = if is_end_point_abs {
            ControlReference::Origin
        } else {
            ControlReference::Start
        };
        if input.try_parse(|i| i.expect_ident_matching("from")).is_ok() {
            reference = ControlReference::parse(input)?;
        }

        Ok(Self::Relative(generic::RelativeControlPoint {
            coord,
            reference,
        }))
    }
}

impl Parse for generic::CommandEndPoint<Position, LengthPercentage> {
    /// Parse <command-end-point> = to <position> | by <coordinate-pair>
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if ByTo::parse(input)?.is_abs() {
            Self::parse_endpoint_as_abs(context, input)
        } else {
            let point = generic::CoordinatePair::parse(context, input)?;
            Ok(Self::ByCoordinate(point))
        }
    }
}

impl generic::CommandEndPoint<Position, LengthPercentage> {
    /// Parse <command-end-point> = to <position>
    fn parse_endpoint_as_abs<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let point = Position::parse(context, input)?;
        Ok(generic::CommandEndPoint::ToPosition(point))
    }
}

impl generic::AxisEndPoint<LengthPercentage> {
    /// Parse <horizontal-line-command>
    pub fn parse_hline<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use cssparser::Token;
        use generic::{AxisPosition, AxisPositionKeyword};

        // If the command is relative, parse for <length-percentage> only.
        if !ByTo::parse(input)?.is_abs() {
            return Ok(Self::ByCoordinate(LengthPercentage::parse(context, input)?));
        }

        let x = AxisPosition::parse(context, input)?;
        if let AxisPosition::Keyword(
            _word @ (AxisPositionKeyword::Top
            | AxisPositionKeyword::Bottom
            | AxisPositionKeyword::YStart
            | AxisPositionKeyword::YEnd),
        ) = &x
        {
            let location = input.current_source_location();
            let token = Token::Ident(x.to_css_string().into());
            return Err(location.new_unexpected_token_error(token));
        }
        Ok(Self::ToPosition(x))
    }

    /// Parse <vertical-line-command>
    pub fn parse_vline<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use cssparser::Token;
        use generic::{AxisPosition, AxisPositionKeyword};

        // If the command is relative, parse for <length-percentage> only.
        if !ByTo::parse(input)?.is_abs() {
            return Ok(Self::ByCoordinate(LengthPercentage::parse(context, input)?));
        }

        let y = AxisPosition::parse(context, input)?;
        if let AxisPosition::Keyword(
            _word @ (AxisPositionKeyword::Left
            | AxisPositionKeyword::Right
            | AxisPositionKeyword::XStart
            | AxisPositionKeyword::XEnd),
        ) = &y
        {
            // Return an error if we parsed a different keyword.
            let location = input.current_source_location();
            let token = Token::Ident(y.to_css_string().into());
            return Err(location.new_unexpected_token_error(token));
        }
        Ok(Self::ToPosition(y))
    }
}

impl ToComputedValue for generic::AxisPosition<LengthPercentage> {
    type ComputedValue = generic::AxisPosition<ComputedLengthPercentage>;

    fn to_computed_value(&self, context: &Context) -> Self::ComputedValue {
        match self {
            Self::LengthPercent(lp) => {
                Self::ComputedValue::LengthPercent(lp.to_computed_value(context))
            },
            Self::Keyword(word) => {
                let lp =
                    LengthPercentage::Percentage(NoCalcPercentage::new(word.as_percentage().0));
                Self::ComputedValue::LengthPercent(lp.to_computed_value(context))
            },
        }
    }

    fn from_computed_value(computed: &Self::ComputedValue) -> Self {
        match computed {
            Self::ComputedValue::LengthPercent(lp) => {
                Self::LengthPercent(LengthPercentage::from_computed_value(lp))
            },
            _ => unreachable!("Invalid state: computed value cannot be a keyword."),
        }
    }
}

impl ToComputedValue for generic::AxisPosition<CSSFloat> {
    type ComputedValue = Self;

    fn to_computed_value(&self, _context: &Context) -> Self {
        *self
    }

    fn from_computed_value(computed: &Self) -> Self {
        *computed
    }
}

/// This determines whether the command is absolutely or relatively positioned.
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-command-end-point
#[derive(Clone, Copy, Debug, Parse, PartialEq)]
enum ByTo {
    /// Command is relative to the command’s starting point.
    By,
    /// Command is relative to the top-left corner of the reference box.
    To,
}

impl ByTo {
    /// Return true if it is absolute, i.e. it is To.
    #[inline]
    pub fn is_abs(&self) -> bool {
        matches!(self, ByTo::To)
    }
}
