/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! CSS handling for the computed value of
//! [`basic-shape`][basic-shape]s
//!
//! [basic-shape]: https://drafts.csswg.org/css-shapes/#typedef-basic-shape

use crate::values::CSSFloat;
use crate::values::animated::{Animate, Procedure};
use crate::values::computed::angle::Angle;
use crate::values::computed::url::ComputedUrl;
use crate::values::computed::{Image, LengthPercentage, Position};
use crate::values::generics::basic_shape as generic;
use crate::values::specified::svg_path::{CoordPair, PathCommand, SVGPathPosition};

/// A computed alias for FillRule.
pub use crate::values::generics::basic_shape::FillRule;

/// A computed `clip-path` value.
pub type ClipPath = generic::GenericClipPath<BasicShape, ComputedUrl>;

/// A computed `shape-outside` value.
pub type ShapeOutside = generic::GenericShapeOutside<BasicShape, Image>;

/// A computed `border-shape` value.
pub type BorderShape = generic::GenericBorderShape<BasicShape>;

/// A computed `border-shape` component.
pub type BorderShapeComponent = generic::GenericBorderShapeComponent<BasicShape>;

/// A computed basic shape.
pub type BasicShape =
    generic::GenericBasicShape<Angle, LengthPercentage, Position, LengthPercentage, InsetRect>;

/// The computed value of `inset()`.
pub type InsetRect = generic::GenericInsetRect<LengthPercentage>;

/// A computed circle.
pub type Circle = generic::Circle<Position, LengthPercentage>;

/// A computed ellipse.
pub type Ellipse = generic::Ellipse<Position, LengthPercentage>;

/// The computed value of `ShapeRadius`.
pub type ShapeRadius = generic::GenericShapeRadius<LengthPercentage>;

/// The computed value of `shape()`.
pub type Shape = generic::Shape<Angle, LengthPercentage, Position, LengthPercentage>;

/// The computed value of `ShapeCommand`.
pub type ShapeCommand =
    generic::GenericShapeCommand<Angle, LengthPercentage, Position, LengthPercentage>;

/// The computed value of `PathOrShapeFunction`.
pub type PathOrShapeFunction =
    generic::GenericPathOrShapeFunction<Angle, LengthPercentage, Position, LengthPercentage>;

/// The computed value of `CoordinatePair`.
pub type CoordinatePair = generic::CoordinatePair<LengthPercentage>;

/// The computed value of 'ControlPoint'.
pub type ControlPoint = generic::ControlPoint<Position, LengthPercentage>;

/// The computed value of 'RelativeControlPoint'.
pub type RelativeControlPoint = generic::RelativeControlPoint<LengthPercentage>;

/// The computed value of 'CommandEndPoint'.
pub type CommandEndPoint = generic::CommandEndPoint<Position, LengthPercentage>;

/// The computed value of hline and vline's endpoint.
pub type AxisEndPoint = generic::AxisEndPoint<LengthPercentage, LengthPercentage>;

/// Animate from `Shape` to `Path`, and vice versa.
macro_rules! animate_shape {
    (
        $from:ident,
        $to:ident,
        $procedure:ident,
        $from_as_shape:expr,
        $to_as_shape:expr
    ) => {{
        // Check fill-rule.
        if $from.fill != $to.fill {
            return Err(());
        }

        // Check the list of commands. (This is a specialized lists::by_computed_value::animate().)
        let from_cmds = $from.commands();
        let to_cmds = $to.commands();
        if from_cmds.len() != to_cmds.len() {
            return Err(());
        }
        let commands = from_cmds
            .iter()
            .zip(to_cmds.iter())
            .map(|(from_cmd, to_cmd)| {
                $from_as_shape(from_cmd).animate(&$to_as_shape(to_cmd), $procedure)
            })
            .collect::<Result<Vec<ShapeCommand>, ()>>()?;

        Ok(Shape {
            fill: $from.fill,
            commands: commands.into(),
        })
    }};
}

impl Animate for PathOrShapeFunction {
    #[inline]
    fn animate(&self, other: &Self, procedure: Procedure) -> Result<Self, ()> {
        // Per spec, commands are "the same" if they use the same command keyword, and use the same
        // <by-to> keyword. For curve and smooth, they also must have the same number of control
        // points. Therefore, we don't have to do normalization here. (Note that we do
        // normalization if we animate from path() to path(). See svg_path.rs for more details.)
        //
        // https://drafts.csswg.org/css-shapes-2/#interpolating-shape
        match (self, other) {
            (Self::Path(from), Self::Path(to)) => from.animate(to, procedure).map(Self::Path),
            (Self::Shape(from), Self::Shape(to)) => from.animate(to, procedure).map(Self::Shape),
            (Self::Shape(from), Self::Path(to)) => {
                // Animate from shape() to path(). We convert each PathCommand into ShapeCommand,
                // and return shape().
                animate_shape!(
                    from,
                    to,
                    procedure,
                    (|shape_cmd| shape_cmd),
                    ShapeCommand::from
                )
                .map(Self::Shape)
            },
            (Self::Path(from), Self::Shape(to)) => {
                // Animate from path() to shape(). We convert each PathCommand into ShapeCommand,
                // and return shape().
                animate_shape!(
                    from,
                    to,
                    procedure,
                    ShapeCommand::from,
                    (|shape_cmd| shape_cmd)
                )
                .map(Self::Shape)
            },
        }
    }
}

impl From<&PathCommand> for ShapeCommand {
    #[inline]
    fn from(path: &PathCommand) -> Self {
        match path {
            &PathCommand::Close => Self::Close,
            PathCommand::Move { point } => Self::Move {
                point: point.into(),
            },
            PathCommand::Line { point } => Self::Move {
                point: point.into(),
            },
            PathCommand::HLine { x } => Self::HLine { x: x.into() },
            PathCommand::VLine { y } => Self::VLine { y: y.into() },
            PathCommand::CubicCurve {
                point,
                control1,
                control2,
            } => Self::CubicCurve {
                point: point.into(),
                control1: control1.into(),
                control2: control2.into(),
            },
            PathCommand::QuadCurve { point, control1 } => Self::QuadCurve {
                point: point.into(),
                control1: control1.into(),
            },
            PathCommand::SmoothCubic { point, control2 } => Self::SmoothCubic {
                point: point.into(),
                control2: control2.into(),
            },
            PathCommand::SmoothQuad { point } => Self::SmoothQuad {
                point: point.into(),
            },
            &PathCommand::Arc {
                ref point,
                ref radii,
                arc_sweep,
                arc_size,
                rotate,
            } => Self::Arc {
                point: point.into(),
                radii: radii.into(),
                arc_sweep,
                arc_size,
                rotate: Angle::from_degrees(rotate),
            },
        }
    }
}

impl From<&CoordPair> for CoordinatePair {
    #[inline]
    fn from(p: &CoordPair) -> Self {
        use crate::values::computed::CSSPixelLength;
        Self::new(
            LengthPercentage::new_length(CSSPixelLength::new(p.x)),
            LengthPercentage::new_length(CSSPixelLength::new(p.y)),
        )
    }
}

impl From<&SVGPathPosition> for Position {
    #[inline]
    fn from(p: &SVGPathPosition) -> Self {
        use crate::values::computed::CSSPixelLength;
        Self::new(
            LengthPercentage::new_length(CSSPixelLength::new(p.horizontal)),
            LengthPercentage::new_length(CSSPixelLength::new(p.vertical)),
        )
    }
}

impl From<&generic::CommandEndPoint<SVGPathPosition, CSSFloat>> for CommandEndPoint {
    #[inline]
    fn from(p: &generic::CommandEndPoint<SVGPathPosition, CSSFloat>) -> Self {
        match p {
            generic::CommandEndPoint::ToPosition(pos) => Self::ToPosition(pos.into()),
            generic::CommandEndPoint::ByCoordinate(coord) => Self::ByCoordinate(coord.into()),
        }
    }
}

impl From<&generic::AxisEndPoint<CSSFloat, CSSFloat>> for AxisEndPoint {
    #[inline]
    fn from(p: &generic::AxisEndPoint<CSSFloat, CSSFloat>) -> Self {
        use crate::values::computed::CSSPixelLength;
        match p {
            generic::AxisEndPoint::ToPosition(pos) => {
                Self::ToPosition(LengthPercentage::new_length(CSSPixelLength::new(*pos)))
            },
            generic::AxisEndPoint::ByCoordinate(pos) => {
                Self::ByCoordinate(LengthPercentage::new_length(CSSPixelLength::new(*pos)))
            },
        }
    }
}

impl From<&generic::ControlPoint<SVGPathPosition, CSSFloat>> for ControlPoint {
    #[inline]
    fn from(p: &generic::ControlPoint<SVGPathPosition, CSSFloat>) -> Self {
        match p {
            generic::ControlPoint::Absolute(pos) => Self::Absolute(pos.into()),
            generic::ControlPoint::Relative(point) => Self::Relative(RelativeControlPoint {
                coord: CoordinatePair::from(&point.coord),
                reference: point.reference,
            }),
        }
    }
}

impl From<&generic::ArcRadii<CSSFloat>> for generic::ArcRadii<LengthPercentage> {
    #[inline]
    fn from(p: &generic::ArcRadii<CSSFloat>) -> Self {
        use crate::values::computed::CSSPixelLength;
        Self {
            rx: LengthPercentage::new_length(CSSPixelLength::new(p.rx)),
            ry: p
                .ry
                .map(|v| LengthPercentage::new_length(CSSPixelLength::new(v))),
        }
    }
}
