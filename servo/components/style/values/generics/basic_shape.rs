/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the [`basic-shape`](https://drafts.csswg.org/css-shapes/#typedef-basic-shape)
//! types that are generic over their `ToCss` implementations.

use crate::Zero;
use crate::derives::*;
use crate::values::animated::{Animate, Procedure, ToAnimatedZero, lists};
use crate::values::computed::Percentage;
use crate::values::distance::{ComputeSquaredDistance, SquaredDistance};
use crate::values::generics::{
    NonNegative, Optional, border::GenericBorderRadius, position::GenericPositionOrAuto, rect::Rect,
};
use crate::values::specified::svg_path::{PathCommand, SVGPathData};
use std::fmt::{self, Write};
use style_traits::{CssWriter, ToCss};
use thin_vec::ThinVec;

/// <https://drafts.fxtf.org/css-masking-1/#typedef-geometry-box>
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    PartialEq,
    Parse,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[derive(Default)]
pub enum ShapeGeometryBox {
    /// Depending on which kind of element this style value applied on, the
    /// default value of the reference-box can be different.  For an HTML
    /// element, the default value of reference-box is border-box; for an SVG
    /// element, the default value is fill-box.  Since we can not determine the
    /// default value at parsing time, we keep this value to make a decision on
    /// it.
    #[css(skip)]
    #[default]
    ElementDependent,
    FillBox,
    StrokeBox,
    ViewBox,
    ShapeBox(ShapeBox),
}

/// Skip the serialization if the author omits the box or specifies border-box.
#[inline]
fn is_default_box_for_clip_path(b: &ShapeGeometryBox) -> bool {
    // Note: for clip-path, ElementDependent is always border-box, so we have to check both of them
    // for serialization.
    matches!(b, ShapeGeometryBox::ElementDependent)
        || matches!(b, ShapeGeometryBox::ShapeBox(ShapeBox::BorderBox))
}

/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-box
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    Copy,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[derive(Default)]
pub enum ShapeBox {
    #[default]
    MarginBox,
    BorderBox,
    PaddingBox,
    ContentBox,
}

/// A value for the `clip-path` property.
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[animation(no_bound(U))]
#[repr(u8)]
pub enum GenericClipPath<BasicShape, U> {
    #[animation(error)]
    None,
    #[animation(error)]
    // XXX This will likely change to skip since it seems Typed OM Level 1
    // won't be updated to cover this case even though there's some preparation
    // in WPT tests for this.
    #[typed(todo)]
    Url(U),
    #[typed(skip)]
    Shape(
        #[animation(field_bound)] Box<BasicShape>,
        #[css(skip_if = "is_default_box_for_clip_path")] ShapeGeometryBox,
    ),
    #[animation(error)]
    Box(ShapeGeometryBox),
}

pub use self::GenericClipPath as ClipPath;

/// A value for the `shape-outside` property.
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[animation(no_bound(I))]
#[repr(u8)]
pub enum GenericShapeOutside<BasicShape, I> {
    #[animation(error)]
    None,
    #[animation(error)]
    Image(I),
    #[typed(skip)]
    Shape(Box<BasicShape>, #[css(skip_if = "is_default")] ShapeBox),
    #[animation(error)]
    Box(ShapeBox),
}

pub use self::GenericShapeOutside as ShapeOutside;

/// The reference box for a `border-shape` component.
///
/// <https://drafts.csswg.org/css-borders-4/#border-shape>
#[allow(missing_docs)]
#[derive(
    Clone,
    Copy,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum BorderShapeGeometryBox {
    /// The path runs along the middle of the border area.
    HalfBorderBox,
    MarginBox,
    BorderBox,
    PaddingBox,
    ContentBox,
    FillBox,
    StrokeBox,
    ViewBox,
}

/// One `<basic-shape> <geometry-box>?` component of `border-shape`.
/// An omitted `<geometry-box>` is resolved to its positional default at
/// parse time.
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct GenericBorderShapeComponent<BasicShape> {
    /// The `<basic-shape>` of this component.
    pub shape: BasicShape,
    /// The `<geometry-box>` the shape is resolved against.
    pub geometry_box: BorderShapeGeometryBox,
}

pub use self::GenericBorderShapeComponent as BorderShapeComponent;

/// A value for the `border-shape` property: `none` or one or two
/// `<basic-shape> <geometry-box>?` components in one buffer.
///
/// <https://drafts.csswg.org/css-borders-4/#border-shape>
#[derive(
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[typed(todo_derive_fields)]
pub struct GenericBorderShape<BasicShape>(pub ThinVec<GenericBorderShapeComponent<BasicShape>>);

pub use self::GenericBorderShape as BorderShape;

/// Resolves an omitted `<geometry-box>`: `half-border-box` for a lone
/// component, `border-box` / `padding-box` for the first / second of a pair.
pub fn border_shape_default_geometry_box(index: usize, len: usize) -> BorderShapeGeometryBox {
    if len == 1 {
        BorderShapeGeometryBox::HalfBorderBox
    } else if index == 0 {
        BorderShapeGeometryBox::BorderBox
    } else {
        BorderShapeGeometryBox::PaddingBox
    }
}

// A box matching the positional default is omitted (shortest serialization).
impl<BasicShape: ToCss> ToCss for GenericBorderShape<BasicShape> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.0.is_empty() {
            return dest.write_str("none");
        }
        for (i, component) in self.0.iter().enumerate() {
            if i != 0 {
                dest.write_char(' ')?;
            }
            component.shape.to_css(dest)?;
            if component.geometry_box != border_shape_default_geometry_box(i, self.0.len()) {
                dest.write_char(' ')?;
                component.geometry_box.to_css(dest)?;
            }
        }
        Ok(())
    }
}

/// The <basic-shape>.
///
/// https://drafts.csswg.org/css-shapes-1/#supported-basic-shapes
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum GenericBasicShape<Angle, AxisPosition, Position, LengthPercentage, BasicShapeRect> {
    /// The <basic-shape-rect>.
    Rect(BasicShapeRect),
    /// Defines a circle with a center and a radius.
    Circle(
        #[animation(field_bound)]
        #[css(field_bound)]
        #[shmem(field_bound)]
        Circle<Position, LengthPercentage>,
    ),
    /// Defines an ellipse with a center and x-axis/y-axis radii.
    Ellipse(
        #[animation(field_bound)]
        #[css(field_bound)]
        #[shmem(field_bound)]
        Ellipse<Position, LengthPercentage>,
    ),
    /// Defines a polygon with pair arguments.
    Polygon(GenericPolygon<LengthPercentage>),
    /// Defines a path() or shape().
    PathOrShape(
        #[animation(field_bound)]
        #[css(field_bound)]
        #[compute(field_bound)]
        GenericPathOrShapeFunction<Angle, AxisPosition, Position, LengthPercentage>,
    ),
}

pub use self::GenericBasicShape as BasicShape;

/// <https://drafts.csswg.org/css-shapes/#funcdef-inset>
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[css(function = "inset")]
#[repr(C)]
pub struct GenericInsetRect<LengthPercentage> {
    pub rect: Rect<LengthPercentage>,
    #[shmem(field_bound)]
    #[animation(field_bound)]
    pub round: GenericBorderRadius<NonNegative<LengthPercentage>>,
}

pub use self::GenericInsetRect as InsetRect;

/// <https://drafts.csswg.org/css-shapes/#funcdef-circle>
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[css(function)]
#[repr(C)]
pub struct Circle<Position, LengthPercentage> {
    pub position: GenericPositionOrAuto<Position>,
    #[animation(field_bound)]
    pub radius: GenericShapeRadius<LengthPercentage>,
}

/// <https://drafts.csswg.org/css-shapes/#funcdef-ellipse>
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[css(function)]
#[repr(C)]
pub struct Ellipse<Position, LengthPercentage> {
    pub position: GenericPositionOrAuto<Position>,
    #[animation(field_bound)]
    pub semiaxis_x: GenericShapeRadius<LengthPercentage>,
    #[animation(field_bound)]
    pub semiaxis_y: GenericShapeRadius<LengthPercentage>,
}

/// <https://drafts.csswg.org/css-shapes/#typedef-shape-radius>
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum GenericShapeRadius<LengthPercentage> {
    Length(
        #[animation(field_bound)]
        #[parse(field_bound)]
        NonNegative<LengthPercentage>,
    ),
    #[animation(error)]
    ClosestSide,
    #[animation(error)]
    FarthestSide,
    #[animation(error)]
    FarthestCorner,
    #[animation(error)]
    ClosestCorner,
}

pub use self::GenericShapeRadius as ShapeRadius;

/// A generic type for representing the `polygon()` function
///
/// <https://drafts.csswg.org/css-shapes/#funcdef-polygon>
#[derive(
    Clone,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[css(comma, function = "polygon")]
#[repr(C)]
pub struct GenericPolygon<LengthPercentage> {
    /// The filling rule for a polygon.
    #[css(skip_if = "is_default")]
    pub fill: FillRule,
    /// A collection of (x, y) coordinates to draw the polygon.
    #[css(iterable)]
    pub coordinates: crate::OwnedSlice<PolygonCoord<LengthPercentage>>,
}

pub use self::GenericPolygon as Polygon;

/// Coordinates for Polygon.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct PolygonCoord<LengthPercentage>(pub LengthPercentage, pub LengthPercentage);

/// path() function or shape() function.
#[derive(
    Clone,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum GenericPathOrShapeFunction<Angle, AxisPosition, Position, LengthPercentage> {
    /// Defines a path with SVG path syntax.
    Path(Path),
    /// Defines a shape function, which is identical to path() but it uses the CSS syntax.
    Shape(
        #[css(field_bound)]
        #[compute(field_bound)]
        Shape<Angle, AxisPosition, Position, LengthPercentage>,
    ),
}

// https://drafts.csswg.org/css-shapes/#typedef-fill-rule
// NOTE: Basic shapes spec says that these are the only two values, however
// https://www.w3.org/TR/SVG/painting.html#FillRuleProperty
// says that it can also be `inherit`
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum FillRule {
    Nonzero,
    Evenodd,
}

/// The path function.
///
/// https://drafts.csswg.org/css-shapes-1/#funcdef-basic-shape-path
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[css(comma, function = "path")]
#[repr(C)]
pub struct Path {
    /// The filling rule for the svg path.
    #[css(skip_if = "is_default")]
    pub fill: FillRule,
    /// The svg path data.
    pub path: SVGPathData,
}

impl Path {
    /// Returns the slice of PathCommand.
    #[inline]
    pub fn commands(&self) -> &[PathCommand] {
        self.path.commands()
    }
}

impl<B, U> ToAnimatedZero for ClipPath<B, U> {
    fn to_animated_zero(&self) -> Result<Self, ()> {
        Err(())
    }
}

impl<B, U> ToAnimatedZero for ShapeOutside<B, U> {
    fn to_animated_zero(&self) -> Result<Self, ()> {
        Err(())
    }
}

impl<Length> ToCss for InsetRect<Length>
where
    Length: ToCss + PartialEq + Zero,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str("inset(")?;
        self.rect.to_css(dest)?;
        if !self.round.is_zero() {
            dest.write_str(" round ")?;
            self.round.to_css(dest)?;
        }
        dest.write_char(')')
    }
}

impl<Position, LengthPercentage> ToCss for Circle<Position, LengthPercentage>
where
    LengthPercentage: ToCss + PartialEq,
    Position: ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let has_radius = self.radius != Default::default();

        dest.write_str("circle(")?;
        if has_radius {
            self.radius.to_css(dest)?;
        }

        // Preserve the `at <position>` even if it specified the default value.
        // https://github.com/w3c/csswg-drafts/issues/8695
        if !matches!(self.position, GenericPositionOrAuto::Auto) {
            if has_radius {
                dest.write_char(' ')?;
            }
            dest.write_str("at ")?;
            self.position.to_css(dest)?;
        }
        dest.write_char(')')
    }
}

impl<Position, LengthPercentage> ToCss for Ellipse<Position, LengthPercentage>
where
    LengthPercentage: ToCss + PartialEq,
    Position: ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        let has_radii =
            self.semiaxis_x != Default::default() || self.semiaxis_y != Default::default();

        dest.write_str("ellipse(")?;
        if has_radii {
            self.semiaxis_x.to_css(dest)?;
            dest.write_char(' ')?;
            self.semiaxis_y.to_css(dest)?;
        }

        // Preserve the `at <position>` even if it specified the default value.
        // https://github.com/w3c/csswg-drafts/issues/8695
        if !matches!(self.position, GenericPositionOrAuto::Auto) {
            if has_radii {
                dest.write_char(' ')?;
            }
            dest.write_str("at ")?;
            self.position.to_css(dest)?;
        }
        dest.write_char(')')
    }
}

impl<L> Default for ShapeRadius<L> {
    #[inline]
    fn default() -> Self {
        ShapeRadius::ClosestSide
    }
}

impl<L> Animate for Polygon<L>
where
    L: Animate,
{
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        if self.fill != other.fill {
            return Err(());
        }
        let coordinates =
            lists::by_computed_value::animate(&self.coordinates, &other.coordinates, procedure)?;
        Ok(Polygon {
            fill: self.fill,
            coordinates,
        })
    }
}

impl<L> ComputeSquaredDistance for Polygon<L>
where
    L: ComputeSquaredDistance,
{
    fn compute_squared_distance(&self, other: &Self) -> Result<SquaredDistance, ()> {
        if self.fill != other.fill {
            return Err(());
        }
        lists::by_computed_value::squared_distance(&self.coordinates, &other.coordinates)
    }
}

impl Default for FillRule {
    #[inline]
    fn default() -> Self {
        FillRule::Nonzero
    }
}

#[inline]
fn is_default<T: Default + PartialEq>(fill: &T) -> bool {
    *fill == Default::default()
}

/// The shape function defined in css-shape-2.
/// shape() = shape(<fill-rule>? from <coordinate-pair>, <shape-command>#)
///
/// https://drafts.csswg.org/css-shapes-2/#shape-function
#[derive(
    Clone,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct Shape<Angle, AxisPosition, Position, LengthPercentage> {
    /// The filling rule for this shape.
    pub fill: FillRule,
    /// The shape command data. Note that the starting point will be the first command in this
    /// slice.
    // Note: The first command is always GenericShapeCommand::Move.
    #[compute(field_bound)]
    pub commands:
        crate::OwnedSlice<GenericShapeCommand<Angle, AxisPosition, Position, LengthPercentage>>,
}

impl<Angle, AxisPosition, Position, LengthPercentage>
    Shape<Angle, AxisPosition, Position, LengthPercentage>
{
    /// Returns the slice of GenericShapeCommand<..>.
    #[inline]
    pub fn commands(
        &self,
    ) -> &[GenericShapeCommand<Angle, AxisPosition, Position, LengthPercentage>] {
        &self.commands
    }
}

impl<Angle, AxisPosition, Position, LengthPercentage> Animate
    for Shape<Angle, AxisPosition, Position, LengthPercentage>
where
    Angle: Animate,
    AxisPosition: Animate,
    Position: Animate,
    LengthPercentage: Animate,
{
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        if self.fill != other.fill {
            return Err(());
        }
        let commands =
            lists::by_computed_value::animate(&self.commands, &other.commands, procedure)?;
        Ok(Self {
            fill: self.fill,
            commands,
        })
    }
}

impl<Angle, AxisPosition, Position, LengthPercentage> ComputeSquaredDistance
    for Shape<Angle, AxisPosition, Position, LengthPercentage>
where
    Angle: ComputeSquaredDistance,
    AxisPosition: ComputeSquaredDistance,
    Position: ComputeSquaredDistance,
    LengthPercentage: ComputeSquaredDistance,
{
    fn compute_squared_distance(&self, other: &Self) -> Result<SquaredDistance, ()> {
        if self.fill != other.fill {
            return Err(());
        }
        lists::by_computed_value::squared_distance(&self.commands, &other.commands)
    }
}

impl<Angle, AxisPosition, Position, LengthPercentage> ToCss
    for Shape<Angle, AxisPosition, Position, LengthPercentage>
where
    Angle: ToCss + Zero,
    AxisPosition: ToCss,
    Position: ToCss,
    LengthPercentage: PartialEq + ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        use style_traits::values::SequenceWriter;

        // Per spec, we must have the first move command and at least one following command.
        debug_assert!(self.commands.len() > 1);

        dest.write_str("shape(")?;
        if !is_default(&self.fill) {
            self.fill.to_css(dest)?;
            dest.write_char(' ')?;
        }
        dest.write_str("from ")?;
        match &self.commands[0] {
            ShapeCommand::Move {
                point: CommandEndPoint::ToPosition(pos),
            } => pos.to_css(dest)?,
            ShapeCommand::Move {
                point: CommandEndPoint::ByCoordinate(coord),
            } => coord.to_css(dest)?,
            _ => unreachable!("The first command must be move"),
        }
        dest.write_str(", ")?;
        {
            let mut writer = SequenceWriter::new(dest, ", ");
            for command in self.commands.iter().skip(1) {
                writer.item(command)?;
            }
        }
        dest.write_char(')')
    }
}

/// This is a more general shape(path) command type, for both shape() and path().
///
/// https://www.w3.org/TR/SVG11/paths.html#PathData
/// https://drafts.csswg.org/css-shapes-2/#shape-function
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[allow(missing_docs)]
#[repr(C, u8)]
pub enum GenericShapeCommand<Angle, AxisPosition, Position, LengthPercentage> {
    /// The move command.
    Move {
        point: CommandEndPoint<Position, LengthPercentage>,
    },
    /// The line command.
    Line {
        point: CommandEndPoint<Position, LengthPercentage>,
    },
    /// The hline command.
    HLine {
        x: AxisEndPoint<AxisPosition, LengthPercentage>,
    },
    /// The vline command.
    VLine {
        y: AxisEndPoint<AxisPosition, LengthPercentage>,
    },
    /// The cubic Bézier curve command.
    CubicCurve {
        point: CommandEndPoint<Position, LengthPercentage>,
        control1: ControlPoint<Position, LengthPercentage>,
        control2: ControlPoint<Position, LengthPercentage>,
    },
    /// The quadratic Bézier curve command.
    QuadCurve {
        point: CommandEndPoint<Position, LengthPercentage>,
        control1: ControlPoint<Position, LengthPercentage>,
    },
    /// The smooth command.
    SmoothCubic {
        point: CommandEndPoint<Position, LengthPercentage>,
        control2: ControlPoint<Position, LengthPercentage>,
    },
    /// The smooth quadratic Bézier curve command.
    SmoothQuad {
        point: CommandEndPoint<Position, LengthPercentage>,
    },
    /// The arc command.
    Arc {
        point: CommandEndPoint<Position, LengthPercentage>,
        radii: ArcRadii<LengthPercentage>,
        arc_sweep: ArcSweep,
        arc_size: ArcSize,
        rotate: Angle,
    },
    /// The closepath command.
    Close,
}

pub use self::GenericShapeCommand as ShapeCommand;

impl<Angle, AxisPosition, Position, LengthPercentage> ToCss
    for ShapeCommand<Angle, AxisPosition, Position, LengthPercentage>
where
    Angle: ToCss + Zero,
    AxisPosition: ToCss,
    Position: ToCss,
    LengthPercentage: PartialEq + ToCss,
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        use self::ShapeCommand::*;
        match *self {
            Move { ref point } => {
                dest.write_str("move ")?;
                point.to_css(dest)
            },
            Line { ref point } => {
                dest.write_str("line ")?;
                point.to_css(dest)
            },
            HLine { ref x } => {
                dest.write_str("hline ")?;
                x.to_css(dest)
            },
            VLine { ref y } => {
                dest.write_str("vline ")?;
                y.to_css(dest)
            },
            CubicCurve {
                ref point,
                ref control1,
                ref control2,
            } => {
                dest.write_str("curve ")?;
                point.to_css(dest)?;
                dest.write_str(" with ")?;
                control1.to_css(dest, point.is_abs())?;
                dest.write_char(' ')?;
                dest.write_char('/')?;
                dest.write_char(' ')?;
                control2.to_css(dest, point.is_abs())
            },
            QuadCurve {
                ref point,
                ref control1,
            } => {
                dest.write_str("curve ")?;
                point.to_css(dest)?;
                dest.write_str(" with ")?;
                control1.to_css(dest, point.is_abs())
            },
            SmoothCubic {
                ref point,
                ref control2,
            } => {
                dest.write_str("smooth ")?;
                point.to_css(dest)?;
                dest.write_str(" with ")?;
                control2.to_css(dest, point.is_abs())
            },
            SmoothQuad { ref point } => {
                dest.write_str("smooth ")?;
                point.to_css(dest)
            },
            Arc {
                ref point,
                ref radii,
                arc_sweep,
                arc_size,
                ref rotate,
            } => {
                dest.write_str("arc ")?;
                point.to_css(dest)?;
                dest.write_str(" of ")?;
                radii.to_css(dest)?;

                if matches!(arc_sweep, ArcSweep::Cw) {
                    dest.write_str(" cw")?;
                }

                if matches!(arc_size, ArcSize::Large) {
                    dest.write_str(" large")?;
                }

                if !rotate.is_zero() {
                    dest.write_str(" rotate ")?;
                    rotate.to_css(dest)?;
                }
                Ok(())
            },
            Close => dest.write_str("close"),
        }
    }
}

/// Defines the end point of the command, which can be specified in absolute or relative coordinates,
/// determined by their "to" or "by" components respectively.
/// https://drafts.csswg.org/css-shapes/#typedef-shape-command-end-point
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum CommandEndPoint<Position, LengthPercentage> {
    ToPosition(Position),
    ByCoordinate(CoordinatePair<LengthPercentage>),
}

impl<Position, LengthPercentage> CommandEndPoint<Position, LengthPercentage> {
    /// Return true if it is absolute, i.e. it is To.
    #[inline]
    pub fn is_abs(&self) -> bool {
        matches!(self, CommandEndPoint::ToPosition(_))
    }
}

impl<Position, LengthPercentage> CommandEndPoint<Position, LengthPercentage> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
        Position: ToCss,
        LengthPercentage: ToCss,
    {
        match self {
            CommandEndPoint::ToPosition(pos) => {
                dest.write_str("to ")?;
                pos.to_css(dest)
            },
            CommandEndPoint::ByCoordinate(coord) => {
                dest.write_str("by ")?;
                coord.to_css(dest)
            },
        }
    }
}

/// Defines the end point for the commands <horizontal-line-command> and <vertical-line-command>, which
/// can be specified in absolute or relative values, determined by their "to" or "by" components respectively.
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-horizontal-line-command
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-vertical-line-command
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    Copy,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum AxisEndPoint<AxisPosition, LengthPercentage> {
    ToPosition(AxisPosition),
    ByCoordinate(LengthPercentage),
}

impl<AxisPosition, LengthPercentage> AxisEndPoint<AxisPosition, LengthPercentage> {
    /// Return true if it is absolute, i.e. it is To.
    #[inline]
    pub fn is_abs(&self) -> bool {
        matches!(self, AxisEndPoint::ToPosition(_))
    }
}

impl<AxisPosition: ToCss, LengthPercentage: ToCss> ToCss
    for AxisEndPoint<AxisPosition, LengthPercentage>
{
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.is_abs() {
            dest.write_str("to ")?;
        } else {
            dest.write_str("by ")?;
        }
        match self {
            AxisEndPoint::ToPosition(pos) => pos.to_css(dest),
            AxisEndPoint::ByCoordinate(coord) => coord.to_css(dest),
        }
    }
}

/// Defines how the absolutely positioned end point for <horizontal-line-command> and
/// <vertical-line-command> is positioned.
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-horizontal-line-command
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-vertical-line-command
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum AxisPosition<LengthPercentage> {
    LengthPercent(LengthPercentage),
    Keyword(AxisPositionKeyword),
}

/// The set of position keywords used in <horizontal-line-command> and <vertical-line-command>
/// for absolute positioning. Note: this is the shared union list between hline and vline, so
/// not every value is valid for either. I.e. hline cannot be positioned with top or y-start.
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-horizontal-line-command
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-vertical-line-command
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum AxisPositionKeyword {
    Center,
    Left,
    Right,
    Top,
    Bottom,
    XStart,
    XEnd,
    YStart,
    YEnd,
}

impl AxisPositionKeyword {
    /// Returns the axis position keyword as its corresponding percentage.
    #[inline]
    pub fn as_percentage(&self) -> Percentage {
        match self {
            Self::Center => Percentage(0.5),
            Self::Left | Self::Top | Self::XStart | Self::YStart => Percentage(0.),
            Self::Right | Self::Bottom | Self::XEnd | Self::YEnd => Percentage(1.),
        }
    }
}

/// Defines a pair of coordinates, representing a rightward and downward offset, respectively, from
/// a specified reference point. Percentages are resolved against the width or height,
/// respectively, of the reference box.
/// https://drafts.csswg.org/css-shapes-2/#typedef-shape-coordinate-pair
#[allow(missing_docs)]
#[derive(
    AddAssign,
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct CoordinatePair<LengthPercentage> {
    pub x: LengthPercentage,
    pub y: LengthPercentage,
}

impl<LengthPercentage> CoordinatePair<LengthPercentage> {
    /// Create a CoordinatePair.
    #[inline]
    pub fn new(x: LengthPercentage, y: LengthPercentage) -> Self {
        Self { x, y }
    }
}

/// Defines a control point for a quadratic or cubic Bézier curve, which can be specified
/// in absolute or relative coordinates.
/// https://drafts.csswg.org/css-shapes/#typedef-shape-control-point
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    Copy,
    ComputeSquaredDistance,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C, u8)]
pub enum ControlPoint<Position, LengthPercentage> {
    Absolute(Position),
    Relative(RelativeControlPoint<LengthPercentage>),
}

impl<Position, LengthPercentage> ControlPoint<Position, LengthPercentage> {
    /// Serialize <control-point>
    pub fn to_css<W>(&self, dest: &mut CssWriter<W>, is_end_point_abs: bool) -> fmt::Result
    where
        W: Write,
        Position: ToCss,
        LengthPercentage: ToCss,
    {
        match self {
            ControlPoint::Absolute(pos) => pos.to_css(dest),
            ControlPoint::Relative(point) => point.to_css(dest, is_end_point_abs),
        }
    }
}

/// Defines a relative control point to a quadratic or cubic Bézier curve, dependent on the
/// reference value. The default `None` is to be relative to the command’s starting point.
/// https://drafts.csswg.org/css-shapes/#typedef-shape-relative-control-point
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct RelativeControlPoint<LengthPercentage> {
    pub coord: CoordinatePair<LengthPercentage>,
    pub reference: ControlReference,
}

impl<LengthPercentage: ToCss> RelativeControlPoint<LengthPercentage> {
    fn to_css<W>(&self, dest: &mut CssWriter<W>, is_end_point_abs: bool) -> fmt::Result
    where
        W: Write,
    {
        self.coord.to_css(dest)?;
        match self.reference {
            ControlReference::Origin if is_end_point_abs => Ok(()),
            ControlReference::Start if !is_end_point_abs => Ok(()),
            other => {
                dest.write_str(" from ")?;
                other.to_css(dest)
            },
        }
    }
}

impl<LengthPercentage: ComputeSquaredDistance> ComputeSquaredDistance
    for RelativeControlPoint<LengthPercentage>
{
    fn compute_squared_distance(&self, other: &Self) -> Result<SquaredDistance, ()> {
        self.coord.compute_squared_distance(&other.coord)
    }
}

/// Defines the point of reference for a <relative-control-point>.
///
/// When a reference is not specified, depending on whether the associated
/// <command-end-point> is absolutely or relatively positioned, the default
/// will be `Origin` or `Start`, respectively.
/// https://drafts.csswg.org/css-shapes/#typedef-shape-relative-control-point
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    Copy,
    Debug,
    Deserialize,
    Eq,
    MallocSizeOf,
    PartialEq,
    Parse,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub enum ControlReference {
    Start,
    End,
    Origin,
}

/// Defines the radiuses for an <arc-command>.
///
/// The first <length-percentage> is the ellipse's horizontal radius, and the second is
/// the vertical radius. If only one value is provided, it is used for both radii, and any
/// <percentage> is resolved against the direction-agnostic size of the reference box.
/// https://drafts.csswg.org/css-shapes-1/#typedef-shape-arc-command
#[allow(missing_docs)]
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    Deserialize,
    MallocSizeOf,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct ArcRadii<LengthPercentage> {
    pub rx: LengthPercentage,
    pub ry: Optional<LengthPercentage>,
}

/// This indicates that the arc that is traced around the ellipse clockwise or counter-clockwise
/// from the center.
/// https://drafts.csswg.org/css-shapes-2/#typedef-shape-arc-sweep
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    FromPrimitive,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ArcSweep {
    /// Counter-clockwise. The default value. (This also represents 0 in the svg path.)
    Ccw = 0,
    /// Clockwise. (This also represents 1 in the svg path.)
    Cw = 1,
}

impl Animate for ArcSweep {
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        use num_traits::FromPrimitive;
        // If an arc command has different <arc-sweep> between its starting and ending list, then
        // the interpolated result uses cw for any progress value between 0 and 1.
        // Note: we cast progress from f64->f32->f64 to drop tiny noise near 0.0.
        let progress = procedure.weights().1 as f32 as f64;
        let procedure = Procedure::Interpolate { progress };
        (*self as i32 as f32)
            .animate(&(*other as i32 as f32), procedure)
            .map(|v| ArcSweep::from_u8((v > 0.) as u8).unwrap_or(ArcSweep::Ccw))
    }
}

impl ComputeSquaredDistance for ArcSweep {
    fn compute_squared_distance(&self, other: &Self) -> Result<SquaredDistance, ()> {
        (*self as i32).compute_squared_distance(&(*other as i32))
    }
}

/// This indicates that the larger or smaller, respectively, of the two possible arcs must be
/// chosen.
/// https://drafts.csswg.org/css-shapes-2/#typedef-shape-arc-size
#[derive(
    Clone,
    Copy,
    Debug,
    Deserialize,
    FromPrimitive,
    MallocSizeOf,
    Parse,
    PartialEq,
    Serialize,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum ArcSize {
    /// Choose the small one. The default value. (This also represents 0 in the svg path.)
    Small = 0,
    /// Choose the large one. (This also represents 1 in the svg path.)
    Large = 1,
}

impl Animate for ArcSize {
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        use num_traits::FromPrimitive;
        // If it has different <arc-size> keywords, then the interpolated result uses large for any
        // progress value between 0 and 1.
        // Note: we cast progress from f64->f32->f64 to drop tiny noise near 0.0.
        let progress = procedure.weights().1 as f32 as f64;
        let procedure = Procedure::Interpolate { progress };
        (*self as i32 as f32)
            .animate(&(*other as i32 as f32), procedure)
            .map(|v| ArcSize::from_u8((v > 0.) as u8).unwrap_or(ArcSize::Small))
    }
}

impl ComputeSquaredDistance for ArcSize {
    fn compute_squared_distance(&self, other: &Self) -> Result<SquaredDistance, ()> {
        (*self as i32).compute_squared_distance(&(*other as i32))
    }
}
