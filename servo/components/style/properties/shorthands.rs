/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Manual shorthand parsing and serialization
#![allow(missing_docs)]

use crate::parser::{Parse, ParserContext};
use crate::values::specified;
use cssparser::Parser;
use std::fmt::{self, Write};
use style_traits::{
    values::SequenceWriter, CssWriter, KeywordsCollectFn, ParseError, SpecifiedValueInfo,
    StyleParseErrorKind, ToCss,
};

macro_rules! expanded {
    ( $( $name: ident: $value: expr ),+ ) => {
        expanded!( $( $name: $value, )+ )
    };
    ( $( $name: ident: $value: expr, )+ ) => {
        Longhands {
            $(
                $name: $crate::properties::MaybeBoxed::maybe_boxed($value),
            )+
        }
    }
}
pub(crate) use expanded;

macro_rules! try_parse_one {
    ($context: expr, $input: expr, $var: ident, $parse_path: path) => {
        if $var.is_none() {
            if let Ok(value) = $input.try_parse(|i| $parse_path($context, i)) {
                $var = Some(value);
                continue;
            }
        }
    };
    ($input: expr, $var: ident, $parse_path: path) => {
        if $var.is_none() {
            if let Ok(value) = $input.try_parse(|i| $parse_path(i)) {
                $var = Some(value);
                continue;
            }
        }
    };
}

macro_rules! unwrap_or_initial {
    ($prop: ident) => {
        unwrap_or_initial!($prop, $prop)
    };
    ($prop: ident, $expr: expr) => {
        $expr.unwrap_or_else(|| $prop::get_initial_specified_value())
    };
}

/// Serializes a border shorthand value composed of width/style/color.
pub fn serialize_directional_border<W>(
    dest: &mut CssWriter<W>,
    width: &specified::BorderSideWidth,
    style: &specified::BorderStyle,
    color: &specified::Color,
) -> fmt::Result
where
    W: Write,
{
    use specified::{BorderSideWidth, BorderStyle, Color};
    let has_style = *style != BorderStyle::None;
    let has_color = *color != Color::CurrentColor;
    let has_width = *width != BorderSideWidth::medium();
    if !has_style && !has_color && !has_width {
        return width.to_css(dest);
    }
    let mut writer = SequenceWriter::new(dest, " ");
    if has_width {
        writer.item(width)?;
    }
    if has_style {
        writer.item(style)?;
    }
    if has_color {
        writer.item(color)?;
    }
    Ok(())
}

pub fn parse_border<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
) -> Result<
    (
        specified::BorderSideWidth,
        specified::BorderStyle,
        specified::Color,
    ),
    ParseError<'i>,
> {
    use crate::values::specified::{BorderSideWidth, BorderStyle, Color};
    let mut color = None;
    let mut style = None;
    let mut width = None;
    let mut parsed = 0;
    loop {
        parsed += 1;
        try_parse_one!(context, input, width, BorderSideWidth::parse);
        try_parse_one!(input, style, BorderStyle::parse);
        try_parse_one!(context, input, color, Color::parse);
        parsed -= 1;
        break;
    }
    if parsed == 0 {
        return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
    }
    Ok((
        width.unwrap_or(BorderSideWidth::medium()),
        style.unwrap_or(BorderStyle::None),
        color.unwrap_or(Color::CurrentColor),
    ))
}

pub mod border_block {
    use super::*;
    pub use crate::properties::generated::shorthands::border_block::*;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let (width, style, color) = super::parse_border(context, input)?;
        Ok(Longhands {
            border_block_start_width: width.clone(),
            border_block_start_style: style.clone(),
            border_block_start_color: color.clone(),
            border_block_end_width: width,
            border_block_end_style: style,
            border_block_end_color: color,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            // FIXME: Should serialize empty if start != end, right?
            super::serialize_directional_border(
                dest,
                &self.border_block_start_width,
                &self.border_block_start_style,
                &self.border_block_start_color,
            )
        }
    }
}

pub mod border_inline {
    use super::*;
    pub use crate::properties::generated::shorthands::border_inline::*;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let (width, style, color) = super::parse_border(context, input)?;
        Ok(Longhands {
            border_inline_start_width: width.clone(),
            border_inline_start_style: style.clone(),
            border_inline_start_color: color.clone(),
            border_inline_end_width: width,
            border_inline_end_style: style,
            border_inline_end_color: color,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            // FIXME: Should serialize empty if start != end, right?
            super::serialize_directional_border(
                dest,
                &self.border_inline_start_width,
                &self.border_inline_start_style,
                &self.border_inline_start_color,
            )
        }
    }
}

pub mod border_radius {
    pub use crate::properties::generated::shorthands::border_radius::*;

    use super::*;
    use crate::values::generics::border::BorderCornerRadius;
    use crate::values::generics::rect::Rect;
    use crate::values::specified::BorderRadius;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let radii = BorderRadius::parse(context, input)?;
        Ok(expanded! {
            border_top_left_radius: radii.top_left,
            border_top_right_radius: radii.top_right,
            border_bottom_right_radius: radii.bottom_right,
            border_bottom_left_radius: radii.bottom_left,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let LonghandsToSerialize {
                border_top_left_radius: &BorderCornerRadius(ref tl),
                border_top_right_radius: &BorderCornerRadius(ref tr),
                border_bottom_right_radius: &BorderCornerRadius(ref br),
                border_bottom_left_radius: &BorderCornerRadius(ref bl),
            } = *self;

            let widths = Rect::new(tl.width(), tr.width(), br.width(), bl.width());
            let heights = Rect::new(tl.height(), tr.height(), br.height(), bl.height());

            BorderRadius::serialize_rects(widths, heights, dest)
        }
    }
}

pub mod border_image {
    pub use crate::properties::generated::shorthands::border_image::*;

    use super::*;
    use crate::properties::longhands::{
        border_image_outset, border_image_repeat, border_image_slice, border_image_source,
        border_image_width,
    };

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut outset = border_image_outset::get_initial_specified_value();
        let mut repeat = border_image_repeat::get_initial_specified_value();
        let mut slice = border_image_slice::get_initial_specified_value();
        let mut source = border_image_source::get_initial_specified_value();
        let mut width = border_image_width::get_initial_specified_value();
        let mut any = false;
        let mut parsed_slice = false;
        let mut parsed_source = false;
        let mut parsed_repeat = false;
        loop {
            if !parsed_slice {
                if let Ok(value) =
                    input.try_parse(|input| border_image_slice::parse(context, input))
                {
                    parsed_slice = true;
                    any = true;
                    slice = value;
                    // Parse border image width and outset, if applicable.
                    let maybe_width_outset: Result<_, ParseError> = input.try_parse(|input| {
                        input.expect_delim('/')?;

                        // Parse border image width, if applicable.
                        let w = input
                            .try_parse(|input| border_image_width::parse(context, input))
                            .ok();

                        // Parse border image outset if applicable.
                        let o = input
                            .try_parse(|input| {
                                input.expect_delim('/')?;
                                border_image_outset::parse(context, input)
                            })
                            .ok();
                        if w.is_none() && o.is_none() {
                            return Err(
                                input.new_custom_error(StyleParseErrorKind::UnspecifiedError)
                            );
                        }
                        Ok((w, o))
                    });
                    if let Ok((w, o)) = maybe_width_outset {
                        if let Some(w) = w {
                            width = w;
                        }
                        if let Some(o) = o {
                            outset = o;
                        }
                    }
                    continue;
                }
            }
            if !parsed_source {
                if let Ok(value) =
                    input.try_parse(|input| border_image_source::parse(context, input))
                {
                    source = value;
                    parsed_source = true;
                    any = true;
                    continue;
                }
            }
            if !parsed_repeat {
                if let Ok(value) =
                    input.try_parse(|input| border_image_repeat::parse(context, input))
                {
                    repeat = value;
                    parsed_repeat = true;
                    any = true;
                    continue;
                }
            }
            break;
        }
        if !any {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
           border_image_outset: outset,
           border_image_repeat: repeat,
           border_image_slice: slice,
           border_image_source: source,
           border_image_width: width,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let mut has_any = false;
            let has_source =
                *self.border_image_source != border_image_source::get_initial_specified_value();
            has_any = has_any || has_source;
            let has_slice =
                *self.border_image_slice != border_image_slice::get_initial_specified_value();
            has_any = has_any || has_slice;
            let has_outset =
                *self.border_image_outset != border_image_outset::get_initial_specified_value();
            has_any = has_any || has_outset;
            let has_width =
                *self.border_image_width != border_image_width::get_initial_specified_value();
            has_any = has_any || has_width;
            let has_repeat =
                *self.border_image_repeat != border_image_repeat::get_initial_specified_value();
            has_any = has_any || has_repeat;
            if has_source || !has_any {
                self.border_image_source.to_css(dest)?;
                if !has_any {
                    return Ok(());
                }
            }
            let needs_slice = has_slice || has_width || has_outset;
            if needs_slice {
                if has_source {
                    dest.write_char(' ')?;
                }
                self.border_image_slice.to_css(dest)?;
                if has_width || has_outset {
                    dest.write_str(" /")?;
                    if has_width {
                        dest.write_char(' ')?;
                        self.border_image_width.to_css(dest)?;
                    }
                    if has_outset {
                        dest.write_str(" / ")?;
                        self.border_image_outset.to_css(dest)?;
                    }
                }
            }
            if has_repeat {
                if has_source || needs_slice {
                    dest.write_char(' ')?;
                }
                self.border_image_repeat.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod border {
    pub use crate::properties::generated::shorthands::border::*;

    use super::*;
    pub use crate::properties::generated::shorthands::border_left;
    use crate::properties::longhands::{
        border_image_outset, border_image_repeat, border_image_slice, border_image_source,
        border_image_width,
    };

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let (width, style, color) = super::parse_border(context, input)?;
        Ok(expanded! {
            border_top_width: width.clone(),
            border_top_style: style,
            border_top_color: color.clone(),
            border_right_width: width.clone(),
            border_right_style: style,
            border_right_color: color.clone(),
            border_bottom_width: width.clone(),
            border_bottom_style: style,
            border_bottom_color: color.clone(),
            border_left_width: width.clone(),
            border_left_style: style,
            border_left_color: color.clone(),

            // The 'border' shorthand resets 'border-image' to its initial value.
            // See https://drafts.csswg.org/css-backgrounds-3/#the-border-shorthands
            border_image_outset: border_image_outset::get_initial_specified_value(),
            border_image_repeat: border_image_repeat::get_initial_specified_value(),
            border_image_slice: border_image_slice::get_initial_specified_value(),
            border_image_source: border_image_source::get_initial_specified_value(),
            border_image_width: border_image_width::get_initial_specified_value(),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::properties::longhands;

            // If any of the border-image longhands differ from their initial specified values we should not
            // invoke serialize_directional_border(), so there is no point in continuing on to compute all_equal.
            if *self.border_image_outset
                != longhands::border_image_outset::get_initial_specified_value()
            {
                return Ok(());
            }
            if *self.border_image_repeat
                != longhands::border_image_repeat::get_initial_specified_value()
            {
                return Ok(());
            }
            if *self.border_image_slice
                != longhands::border_image_slice::get_initial_specified_value()
            {
                return Ok(());
            }
            if *self.border_image_source
                != longhands::border_image_source::get_initial_specified_value()
            {
                return Ok(());
            }
            if *self.border_image_width
                != longhands::border_image_width::get_initial_specified_value()
            {
                return Ok(());
            }

            let all_equal = {
                let border_top_width = self.border_top_width;
                let border_top_style = self.border_top_style;
                let border_top_color = self.border_top_color;
                let border_right_width = self.border_right_width;
                let border_right_style = self.border_right_style;
                let border_right_color = self.border_right_color;
                let border_bottom_width = self.border_bottom_width;
                let border_bottom_style = self.border_bottom_style;
                let border_bottom_color = self.border_bottom_color;
                let border_left_width = self.border_left_width;
                let border_left_style = self.border_left_style;
                let border_left_color = self.border_left_color;

                border_top_width == border_right_width
                    && border_right_width == border_bottom_width
                    && border_bottom_width == border_left_width
                    && border_top_style == border_right_style
                    && border_right_style == border_bottom_style
                    && border_bottom_style == border_left_style
                    && border_top_color == border_right_color
                    && border_right_color == border_bottom_color
                    && border_bottom_color == border_left_color
            };

            // If all longhands are all present, then all sides should be the same,
            // so we can just one set of color/style/width
            if !all_equal {
                return Ok(());
            }
            super::serialize_directional_border(
                dest,
                self.border_left_width,
                self.border_left_style,
                self.border_left_color,
            )
        }
    }

    // We need to implement this by hand because deriving this would also derive border-image,
    // which this property only resets. Just use the same as border-left for simplicity.
    impl SpecifiedValueInfo for Longhands {
        const SUPPORTED_TYPES: u8 = border_left::Longhands::SUPPORTED_TYPES;

        fn collect_completion_keywords(f: KeywordsCollectFn) {
            border_left::Longhands::collect_completion_keywords(f);
        }
    }
}

#[cfg(feature = "gecko")]
pub mod container {
    use super::*;
    pub use crate::properties::generated::shorthands::container::*;

    use crate::values::specified::{ContainerName, ContainerType};

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        // See https://github.com/w3c/csswg-drafts/issues/7180 for why we don't match the spec.
        let container_name = ContainerName::parse(context, input)?;
        let container_type = if input.try_parse(|input| input.expect_delim('/')).is_ok() {
            ContainerType::parse(context, input)?
        } else {
            ContainerType::NORMAL
        };
        Ok(expanded! {
            container_name: container_name,
            container_type: container_type,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.container_name.to_css(dest)?;
            if !self.container_type.is_normal() {
                dest.write_str(" / ")?;
                self.container_type.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod vertical_align {
    use super::*;
    pub use crate::properties::generated::shorthands::vertical_align::*;

    use crate::values::specified::{AlignmentBaseline, BaselineShift, BaselineSource};

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut baseline_source = None;
        let mut alignment_baseline = None;
        let mut baseline_shift = None;
        let mut parsed = 0;

        loop {
            parsed += 1;

            try_parse_one!(input, baseline_source, BaselineSource::parse_non_auto);
            try_parse_one!(input, alignment_baseline, AlignmentBaseline::parse);
            try_parse_one!(context, input, baseline_shift, BaselineShift::parse);

            parsed -= 1;
            break;
        }

        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(expanded! {
            baseline_source: baseline_source.unwrap_or(BaselineSource::Auto),
            alignment_baseline: alignment_baseline.unwrap_or(AlignmentBaseline::Baseline),
            baseline_shift: baseline_shift.unwrap_or(BaselineShift::zero()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let mut writer = SequenceWriter::new(dest, " ");
            if *self.baseline_source != BaselineSource::Auto {
                writer.item(self.baseline_source)?;
            }
            if *self.alignment_baseline != AlignmentBaseline::Baseline {
                writer.item(self.alignment_baseline)?;
            }
            if *self.baseline_shift != BaselineShift::zero() {
                writer.item(self.baseline_shift)?;
            }
            if !writer.has_written() {
                self.alignment_baseline.to_css(dest)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod page_break_before {
    use super::*;
    pub use crate::properties::generated::shorthands::page_break_before::*;

    use crate::values::specified::BreakBetween;

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        Ok(expanded! {
            break_before: BreakBetween::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.break_before.to_css_legacy(dest)
        }
    }
}

#[cfg(feature = "gecko")]
pub mod page_break_after {
    pub use crate::properties::generated::shorthands::page_break_after::*;

    use super::*;
    use crate::values::specified::BreakBetween;

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        Ok(expanded! {
            break_after: BreakBetween::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.break_after.to_css_legacy(dest)
        }
    }
}

#[cfg(feature = "gecko")]
pub mod page_break_inside {
    use super::*;
    pub use crate::properties::generated::shorthands::page_break_inside::*;
    use crate::values::specified::BreakWithin;

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        Ok(expanded! {
            break_inside: BreakWithin::parse_legacy(context, input)?,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.break_inside.to_css_legacy(dest)
        }
    }
}

#[cfg(feature = "gecko")]
pub mod offset {
    use super::*;
    pub use crate::properties::generated::shorthands::offset::*;
    use crate::values::specified::{
        LengthPercentage, OffsetPath, OffsetPosition, OffsetRotate, PositionOrAuto,
    };
    use crate::Zero;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let offset_position = input.try_parse(|i| OffsetPosition::parse(context, i)).ok();
        let offset_path = input.try_parse(|i| OffsetPath::parse(context, i)).ok();

        // Must have one of [offset-position, offset-path].
        // FIXME: The syntax is out-of-date after the update of the spec.
        if offset_position.is_none() && offset_path.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        let mut offset_distance = None;
        let mut offset_rotate = None;
        // offset-distance and offset-rotate are grouped with offset-path.
        if offset_path.is_some() {
            loop {
                if offset_distance.is_none() {
                    if let Ok(value) = input.try_parse(|i| LengthPercentage::parse(context, i)) {
                        offset_distance = Some(value);
                    }
                }

                if offset_rotate.is_none() {
                    if let Ok(value) = input.try_parse(|i| OffsetRotate::parse(context, i)) {
                        offset_rotate = Some(value);
                        continue;
                    }
                }
                break;
            }
        }

        let offset_anchor = input
            .try_parse(|i| {
                i.expect_delim('/')?;
                PositionOrAuto::parse(context, i)
            })
            .ok();

        Ok(expanded! {
            offset_position: offset_position.unwrap_or(OffsetPosition::normal()),
            offset_path: offset_path.unwrap_or(OffsetPath::none()),
            offset_distance: offset_distance.unwrap_or(LengthPercentage::zero()),
            offset_rotate: offset_rotate.unwrap_or(OffsetRotate::auto()),
            offset_anchor: offset_anchor.unwrap_or(PositionOrAuto::auto()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            // The basic concept is: we must serialize offset-position or offset-path group.
            // offset-path group means "offset-path offset-distance offset-rotate".
            let must_serialize_path = *self.offset_path != OffsetPath::None
                || (!self.offset_distance.is_zero() || !self.offset_rotate.is_auto());
            let position_is_default = matches!(self.offset_position, OffsetPosition::Normal);
            if !position_is_default || !must_serialize_path {
                self.offset_position.to_css(dest)?;
            }

            if must_serialize_path {
                if !position_is_default {
                    dest.write_char(' ')?;
                }
                self.offset_path.to_css(dest)?;
            }

            if !self.offset_distance.is_zero() {
                dest.write_char(' ')?;
                self.offset_distance.to_css(dest)?;
            }

            if !self.offset_rotate.is_auto() {
                dest.write_char(' ')?;
                self.offset_rotate.to_css(dest)?;
            }

            if *self.offset_anchor != PositionOrAuto::auto() {
                dest.write_str(" / ")?;
                self.offset_anchor.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod _webkit_perspective {
    pub use crate::properties::generated::shorthands::_webkit_perspective::*;

    use super::*;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::properties::longhands::perspective;
        use crate::values::generics::NonNegative;
        use crate::values::specified::{AllowQuirks, Length, Perspective};

        if let Ok(l) = input.try_parse(|input| {
            Length::parse_non_negative_quirky(context, input, AllowQuirks::Always)
        }) {
            Ok(expanded! {
                perspective: Perspective::Length(NonNegative(l)),
            })
        } else {
            Ok(expanded! {
                perspective: perspective::parse(context, input)?
            })
        }
    }
}

pub mod _webkit_transform {
    pub use crate::properties::generated::shorthands::_webkit_transform::*;

    use super::*;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        use crate::values::specified::Transform;
        Ok(expanded! {
            transform: Transform::parse_legacy(context, input)?,
        })
    }
}

pub mod columns {
    pub use crate::properties::generated::shorthands::columns::*;

    use super::*;
    use crate::properties::longhands::{column_count, column_width};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut column_count = None;
        let mut column_width = None;
        let mut autos = 0;

        loop {
            if input
                .try_parse(|input| input.expect_ident_matching("auto"))
                .is_ok()
            {
                // Leave the options to None, 'auto' is the initial value.
                autos += 1;
                continue;
            }

            if column_count.is_none() {
                if let Ok(value) = input.try_parse(|input| column_count::parse(context, input)) {
                    column_count = Some(value);
                    continue;
                }
            }

            if column_width.is_none() {
                if let Ok(value) = input.try_parse(|input| column_width::parse(context, input)) {
                    column_width = Some(value);
                    continue;
                }
            }

            break;
        }

        let values = autos + column_count.iter().len() + column_width.iter().len();
        if values == 0 || values > 2 {
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        } else {
            Ok(expanded! {
                column_count: unwrap_or_initial!(column_count),
                column_width: unwrap_or_initial!(column_width),
            })
        }
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if self.column_width.is_auto() {
                return self.column_count.to_css(dest);
            }
            self.column_width.to_css(dest)?;
            if !self.column_count.is_auto() {
                dest.write_char(' ')?;
                self.column_count.to_css(dest)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod column_rule {
    pub use crate::properties::generated::shorthands::column_rule::*;

    use super::*;
    use crate::properties::longhands::column_rule_color;
    use crate::properties::longhands::{column_rule_style, column_rule_width};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut column_rule_width = None;
        let mut column_rule_style = None;
        let mut column_rule_color = None;
        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, column_rule_width, column_rule_width::parse);
            try_parse_one!(context, input, column_rule_style, column_rule_style::parse);
            try_parse_one!(context, input, column_rule_color, column_rule_color::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            column_rule_width: unwrap_or_initial!(column_rule_width),
            column_rule_style: unwrap_or_initial!(column_rule_style),
            column_rule_color: unwrap_or_initial!(column_rule_color),
        })
    }
}

#[cfg(feature = "gecko")]
pub mod text_wrap {
    pub use crate::properties::generated::shorthands::text_wrap::*;

    use super::*;
    use crate::properties::longhands::{text_wrap_mode, text_wrap_style};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut mode = None;
        let mut style = None;
        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, mode, text_wrap_mode::parse);
            try_parse_one!(context, input, style, text_wrap_style::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            text_wrap_mode: unwrap_or_initial!(text_wrap_mode, mode),
            text_wrap_style: unwrap_or_initial!(text_wrap_style, style),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use text_wrap_mode::computed_value::T as Mode;
            use text_wrap_style::computed_value::T as Style;

            if matches!(self.text_wrap_style, &Style::Auto) {
                return self.text_wrap_mode.to_css(dest);
            }

            if *self.text_wrap_mode != Mode::Wrap {
                self.text_wrap_mode.to_css(dest)?;
                dest.write_char(' ')?;
            }

            self.text_wrap_style.to_css(dest)
        }
    }
}

pub mod white_space {
    pub use crate::properties::generated::shorthands::white_space::*;

    use super::*;
    use crate::properties::longhands::{text_wrap_mode, white_space_collapse};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        use text_wrap_mode::computed_value::T as Wrap;
        use white_space_collapse::computed_value::T as Collapse;

        fn parse_special_shorthands<'i, 't>(
            input: &mut Parser<'i, 't>,
        ) -> Result<Longhands, ParseError<'i>> {
            let (mode, collapse) = try_match_ident_ignore_ascii_case! { input,
                "normal" => (Wrap::Wrap, Collapse::Collapse),
                "pre" => (Wrap::Nowrap, Collapse::Preserve),
                "pre-wrap" => (Wrap::Wrap, Collapse::Preserve),
                "pre-line" => (Wrap::Wrap, Collapse::PreserveBreaks),
            };
            Ok(expanded! {
                text_wrap_mode: mode,
                white_space_collapse: collapse,
            })
        }

        if let Ok(result) = input.try_parse(parse_special_shorthands) {
            return Ok(result);
        }

        let mut wrap = None;
        let mut collapse = None;
        let mut parsed = 0;

        loop {
            parsed += 1;
            try_parse_one!(context, input, wrap, text_wrap_mode::parse);
            try_parse_one!(context, input, collapse, white_space_collapse::parse);
            parsed -= 1;
            break;
        }

        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(expanded! {
            text_wrap_mode: unwrap_or_initial!(text_wrap_mode, wrap),
            white_space_collapse: unwrap_or_initial!(white_space_collapse, collapse),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use text_wrap_mode::computed_value::T as Wrap;
            use white_space_collapse::computed_value::T as Collapse;

            match *self.text_wrap_mode {
                Wrap::Wrap => match *self.white_space_collapse {
                    Collapse::Collapse => return dest.write_str("normal"),
                    Collapse::Preserve => return dest.write_str("pre-wrap"),
                    Collapse::PreserveBreaks => return dest.write_str("pre-line"),
                    _ => (),
                },
                Wrap::Nowrap => {
                    if let Collapse::Preserve = *self.white_space_collapse {
                        return dest.write_str("pre");
                    }
                },
            }

            let mut has_value = false;
            if *self.white_space_collapse != Collapse::Collapse {
                self.white_space_collapse.to_css(dest)?;
                has_value = true;
            }

            if *self.text_wrap_mode != Wrap::Wrap {
                if has_value {
                    dest.write_char(' ')?;
                }
                self.text_wrap_mode.to_css(dest)?;
            }

            Ok(())
        }
    }

    impl SpecifiedValueInfo for Longhands {
        fn collect_completion_keywords(f: KeywordsCollectFn) {
            // Collect keywords from our longhands.
            text_wrap_mode::SpecifiedValue::collect_completion_keywords(f);
            white_space_collapse::SpecifiedValue::collect_completion_keywords(f);

            // Add the special values supported only by the shorthand
            // (see parse_special_shorthands() above).
            f(&["normal", "pre", "pre-wrap", "pre-line"])
        }
    }
}

#[cfg(feature = "gecko")]
pub mod _webkit_text_stroke {
    pub use crate::properties::generated::shorthands::_webkit_text_stroke::*;

    use super::*;
    use crate::properties::longhands::{_webkit_text_stroke_color, _webkit_text_stroke_width};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut color = None;
        let mut width = None;
        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, color, _webkit_text_stroke_color::parse);
            try_parse_one!(context, input, width, _webkit_text_stroke_width::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            _webkit_text_stroke_color: unwrap_or_initial!(_webkit_text_stroke_color, color),
            _webkit_text_stroke_width: unwrap_or_initial!(_webkit_text_stroke_width, width),
        })
    }
}

pub mod list_style {
    pub use crate::properties::generated::shorthands::list_style::*;

    use super::*;
    use crate::properties::longhands::{list_style_image, list_style_position, list_style_type};
    use crate::values::specified::Image;
    use selectors::parser::SelectorParseErrorKind;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        // `none` is ambiguous until we've finished parsing the shorthands, so we count the number
        // of times we see it.
        let mut nones = 0u8;
        let (mut image, mut position, mut list_style_type) = (None, None, None);
        let mut parsed = 0;
        loop {
            parsed += 1;

            if input
                .try_parse(|input| input.expect_ident_matching("none"))
                .is_ok()
            {
                nones += 1;
                if nones > 2 {
                    return Err(input
                        .new_custom_error(SelectorParseErrorKind::UnexpectedIdent("none".into())));
                }
                continue;
            }

            try_parse_one!(context, input, image, list_style_image::parse);
            try_parse_one!(context, input, position, list_style_position::parse);
            // list-style-type must be checked the last, because it accepts
            // arbitrary identifier for custom counter style, and thus may
            // affect values of list-style-position.
            try_parse_one!(context, input, list_style_type, list_style_type::parse);

            parsed -= 1;
            break;
        }

        let position = unwrap_or_initial!(list_style_position, position);

        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        // If there are two `none`s, then we can't have a type or image; if there is one `none`,
        // then we can't have both a type *and* an image; if there is no `none` then we're fine as
        // long as we parsed something.
        use self::list_style_type::SpecifiedValue as ListStyleType;
        match (nones, list_style_type, image) {
            (2, None, None) => Ok(expanded! {
                list_style_position: position,
                list_style_image: Image::None,
                list_style_type: ListStyleType::none(),
            }),
            (1, None, Some(image)) => Ok(expanded! {
                list_style_position: position,
                list_style_image: image,
                list_style_type: ListStyleType::none(),
            }),
            (1, Some(list_style_type), None) => Ok(expanded! {
                list_style_position: position,
                list_style_image: Image::None,
                list_style_type: list_style_type,
            }),
            (1, None, None) => Ok(expanded! {
                list_style_position: position,
                list_style_image: Image::None,
                list_style_type: ListStyleType::none(),
            }),
            (0, list_style_type, image) => Ok(expanded! {
                list_style_position: position,
                list_style_image: unwrap_or_initial!(list_style_image, image),
                list_style_type: unwrap_or_initial!(list_style_type),
            }),
            _ => Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
        }
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use list_style_image::SpecifiedValue as ListStyleImage;
            use list_style_position::SpecifiedValue as ListStylePosition;
            use list_style_type::SpecifiedValue as ListStyleType;

            let mut writer = SequenceWriter::new(dest, " ");
            if *self.list_style_position != ListStylePosition::Outside {
                writer.item(self.list_style_position)?;
            }
            if *self.list_style_image != ListStyleImage::None {
                writer.item(self.list_style_image)?;
            }
            if *self.list_style_type != ListStyleType::disc() {
                writer.item(self.list_style_type)?;
            }
            if !writer.has_written() {
                self.list_style_position.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod gap {
    pub use crate::properties::generated::shorthands::gap::*;

    use super::*;
    use crate::properties::longhands::{column_gap, row_gap};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let r_gap = row_gap::parse(context, input)?;
        let c_gap = input
            .try_parse(|input| column_gap::parse(context, input))
            .unwrap_or(r_gap.clone());

        Ok(expanded! {
            row_gap: r_gap,
            column_gap: c_gap,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.row_gap.to_css(dest)?;
            if self.row_gap != self.column_gap {
                dest.write_char(' ')?;
                self.column_gap.to_css(dest)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod marker {
    pub use crate::properties::generated::shorthands::marker::*;

    use super::*;
    use crate::values::specified::url::UrlOrNone;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let url = UrlOrNone::parse(context, input)?;

        Ok(expanded! {
            marker_start: url.clone(),
            marker_mid: url.clone(),
            marker_end: url,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if self.marker_start == self.marker_mid && self.marker_mid == self.marker_end {
                self.marker_start.to_css(dest)
            } else {
                Ok(())
            }
        }
    }
}

pub mod flex_flow {
    pub use crate::properties::generated::shorthands::flex_flow::*;

    use super::*;
    use crate::properties::longhands::{flex_direction, flex_wrap};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut parsed = 0;
        let mut direction = None;
        let mut wrap = None;
        loop {
            parsed += 1;
            try_parse_one!(context, input, direction, flex_direction::parse);
            try_parse_one!(context, input, wrap, flex_wrap::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            flex_direction: unwrap_or_initial!(flex_direction, direction),
            flex_wrap: unwrap_or_initial!(flex_wrap, wrap),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if *self.flex_direction == flex_direction::get_initial_specified_value()
                && *self.flex_wrap != flex_wrap::get_initial_specified_value()
            {
                return self.flex_wrap.to_css(dest);
            }
            self.flex_direction.to_css(dest)?;
            if *self.flex_wrap != flex_wrap::get_initial_specified_value() {
                dest.write_char(' ')?;
                self.flex_wrap.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod flex {
    pub use crate::properties::generated::shorthands::flex::*;

    use super::*;
    use crate::properties::longhands::flex_basis::SpecifiedValue as FlexBasis;
    use crate::values::specified::NonNegativeNumber;

    fn parse_flexibility<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<(NonNegativeNumber, Option<NonNegativeNumber>), ParseError<'i>> {
        let grow = NonNegativeNumber::parse(context, input)?;
        let shrink = input
            .try_parse(|i| NonNegativeNumber::parse(context, i))
            .ok();
        Ok((grow, shrink))
    }

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut grow = None;
        let mut shrink = None;
        let mut basis = None;

        if input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            return Ok(expanded! {
                flex_grow: NonNegativeNumber::new(0.0),
                flex_shrink: NonNegativeNumber::new(0.0),
                flex_basis: FlexBasis::auto(),
            });
        }
        loop {
            if grow.is_none() {
                if let Ok((flex_grow, flex_shrink)) =
                    input.try_parse(|i| parse_flexibility(context, i))
                {
                    grow = Some(flex_grow);
                    shrink = flex_shrink;
                    continue;
                }
            }
            if basis.is_none() {
                if let Ok(value) = input.try_parse(|input| FlexBasis::parse(context, input)) {
                    basis = Some(value);
                    continue;
                }
            }
            break;
        }

        if grow.is_none() && basis.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            flex_grow: grow.unwrap_or(NonNegativeNumber::new(1.0)),
            flex_shrink: shrink.unwrap_or(NonNegativeNumber::new(1.0)),
            flex_basis: basis.unwrap_or(FlexBasis::zero_percent()),
        })
    }
}

pub mod place_content {
    pub use crate::properties::generated::shorthands::place_content::*;

    use super::*;
    use crate::values::specified::align::ContentDistribution;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let align_content = ContentDistribution::parse_block(context, input)?;
        let justify_content =
            input.try_parse(|input| ContentDistribution::parse_inline(context, input));

        let justify_content = match justify_content {
            Ok(v) => v,
            Err(..) => {
                if !align_content.is_baseline_position() {
                    align_content
                } else {
                    ContentDistribution::start()
                }
            },
        };

        Ok(expanded! {
            align_content: align_content,
            justify_content: justify_content,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.align_content.to_css(dest)?;
            if self.align_content != self.justify_content {
                dest.write_char(' ')?;
                self.justify_content.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod place_self {
    pub use crate::properties::generated::shorthands::place_self::*;

    use super::*;
    use crate::values::specified::align::SelfAlignment;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let align = SelfAlignment::parse_block(context, input)?;
        let justify = input.try_parse(|input| SelfAlignment::parse_inline(context, input));

        let justify = match justify {
            Ok(v) => v,
            Err(..) => {
                debug_assert!(align.is_valid_on_both_axes());
                align
            },
        };

        Ok(expanded! {
            align_self: align,
            justify_self: justify,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.align_self.to_css(dest)?;
            if self.align_self != self.justify_self {
                dest.write_char(' ')?;
                self.justify_self.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod place_items {
    pub use crate::properties::generated::shorthands::place_items::*;

    use super::*;
    use crate::values::specified::align::{ItemPlacement, JustifyItems};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let align = ItemPlacement::parse_block(context, input)?;
        let justify = input
            .try_parse(|input| ItemPlacement::parse_inline(context, input))
            .unwrap_or_else(|_| align.clone());

        Ok(expanded! {
            align_items: align,
            justify_items: JustifyItems(justify),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.align_items.to_css(dest)?;
            if self.align_items != &self.justify_items.0 {
                dest.write_char(' ')?;
                self.justify_items.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod grid_row {
    pub use crate::properties::generated::shorthands::grid_row::*;

    use super::*;
    use crate::values::specified::GridLine;
    use crate::Zero;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let start = input.try_parse(|i| GridLine::parse(context, i))?;
        let end = if input.try_parse(|i| i.expect_delim('/')).is_ok() {
            GridLine::parse(context, input)?
        } else {
            let mut line = GridLine::auto();
            if start.line_num.is_zero() && !start.is_span {
                line.ident = start.ident.clone();
            }

            line
        };

        Ok(expanded! {
            grid_row_start: start,
            grid_row_end: end,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.grid_row_start.to_css(dest)?;
            if self.grid_row_start.can_omit(self.grid_row_end) {
                return Ok(());
            }
            dest.write_str(" / ")?;
            self.grid_row_end.to_css(dest)
        }
    }
}

pub mod grid_column {
    pub use crate::properties::generated::shorthands::grid_column::*;

    use super::*;
    use crate::values::specified::GridLine;
    use crate::Zero;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let start = input.try_parse(|i| GridLine::parse(context, i))?;
        let end = if input.try_parse(|i| i.expect_delim('/')).is_ok() {
            GridLine::parse(context, input)?
        } else {
            let mut line = GridLine::auto();
            if start.line_num.is_zero() && !start.is_span {
                line.ident = start.ident.clone();
            }

            line
        };

        Ok(expanded! {
            grid_column_start: start,
            grid_column_end: end,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.grid_column_start.to_css(dest)?;
            if self.grid_column_start.can_omit(self.grid_column_end) {
                return Ok(());
            }
            dest.write_str(" / ")?;
            self.grid_column_end.to_css(dest)
        }
    }
}

pub mod grid_area {
    pub use crate::properties::generated::shorthands::grid_area::*;

    use super::*;
    use crate::values::specified::GridLine;
    use crate::Zero;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        fn line_with_ident_from(other: &GridLine) -> GridLine {
            let mut this = GridLine::auto();
            if other.line_num.is_zero() && !other.is_span {
                this.ident = other.ident.clone();
            }

            this
        }

        let row_start = input.try_parse(|i| GridLine::parse(context, i))?;
        let (column_start, row_end, column_end) =
            if input.try_parse(|i| i.expect_delim('/')).is_ok() {
                let column_start = GridLine::parse(context, input)?;
                let (row_end, column_end) = if input.try_parse(|i| i.expect_delim('/')).is_ok() {
                    let row_end = GridLine::parse(context, input)?;
                    let column_end = if input.try_parse(|i| i.expect_delim('/')).is_ok() {
                        GridLine::parse(context, input)?
                    } else {
                        line_with_ident_from(&column_start)
                    };

                    (row_end, column_end)
                } else {
                    let row_end = line_with_ident_from(&row_start);
                    let column_end = line_with_ident_from(&column_start);
                    (row_end, column_end)
                };

                (column_start, row_end, column_end)
            } else {
                let line = line_with_ident_from(&row_start);
                (line.clone(), line.clone(), line)
            };

        Ok(expanded! {
            grid_row_start: row_start,
            grid_row_end: row_end,
            grid_column_start: column_start,
            grid_column_end: column_end,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            self.grid_row_start.to_css(dest)?;
            let mut trailing_values = 3;
            if self.grid_column_start.can_omit(self.grid_column_end) {
                trailing_values -= 1;
                if self.grid_row_start.can_omit(self.grid_row_end) {
                    trailing_values -= 1;
                    if self.grid_row_start.can_omit(self.grid_column_start) {
                        trailing_values -= 1;
                    }
                }
            }
            let values = [
                &self.grid_column_start,
                &self.grid_row_end,
                &self.grid_column_end,
            ];
            for value in values.iter().take(trailing_values) {
                dest.write_str(" / ")?;
                value.to_css(dest)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod position_try {
    pub use crate::properties::generated::shorthands::position_try::*;

    use super::*;
    use crate::values::specified::position::{PositionTryFallbacks, PositionTryOrder};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let order =
            if static_prefs::pref!("layout.css.anchor-positioning.position-try-order.enabled") {
                input.try_parse(PositionTryOrder::parse).ok()
            } else {
                None
            };
        let fallbacks = PositionTryFallbacks::parse(context, input)?;
        Ok(expanded! {
            position_try_order: order.unwrap_or(PositionTryOrder::normal()),
            position_try_fallbacks: fallbacks,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if let Some(o) = self.position_try_order {
                if *o != PositionTryOrder::Normal {
                    o.to_css(dest)?;
                    dest.write_char(' ')?;
                }
            }
            self.position_try_fallbacks.to_css(dest)
        }
    }
}

#[cfg(feature = "gecko")]
fn timeline_to_css<W>(
    name: &[specified::TimelineName],
    axes: &[specified::ScrollAxis],
    dest: &mut CssWriter<W>,
) -> fmt::Result
where
    W: fmt::Write,
{
    if name.len() != axes.len() {
        return Ok(());
    }
    for (i, (name, axis)) in std::iter::zip(name.iter(), axes.iter()).enumerate() {
        if i != 0 {
            dest.write_str(", ")?;
        }
        name.to_css(dest)?;
        if !axis.is_default() {
            dest.write_char(' ')?;
            axis.to_css(dest)?;
        }
    }
    Ok(())
}

#[cfg(feature = "gecko")]
pub mod scroll_timeline {
    pub use crate::properties::generated::shorthands::scroll_timeline::*;

    use super::*;
    use crate::properties::longhands::{scroll_timeline_axis, scroll_timeline_name};

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut names = Vec::with_capacity(1);
        let mut axes = Vec::with_capacity(1);
        input.parse_comma_separated(|input| {
            let name = scroll_timeline_name::single_value::parse(context, input)?;
            let axis = input.try_parse(|i| scroll_timeline_axis::single_value::parse(context, i));

            names.push(name);
            axes.push(axis.unwrap_or_default());

            Ok(())
        })?;

        Ok(expanded! {
            scroll_timeline_name: scroll_timeline_name::SpecifiedValue(names.into()),
            scroll_timeline_axis: scroll_timeline_axis::SpecifiedValue(axes.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            super::timeline_to_css(
                &self.scroll_timeline_name.0,
                &self.scroll_timeline_axis.0,
                dest,
            )
        }
    }
}

#[cfg(feature = "gecko")]
pub mod view_timeline {
    pub use crate::properties::generated::shorthands::view_timeline::*;

    use super::*;
    use crate::properties::longhands::{view_timeline_axis, view_timeline_name};

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut names = Vec::with_capacity(1);
        let mut axes = Vec::with_capacity(1);
        input.parse_comma_separated(|input| {
            let name = view_timeline_name::single_value::parse(context, input)?;
            let axis = input.try_parse(|i| view_timeline_axis::single_value::parse(context, i));

            names.push(name);
            axes.push(axis.unwrap_or_default());

            Ok(())
        })?;

        Ok(expanded! {
            view_timeline_name: view_timeline_name::SpecifiedValue(names.into()),
            view_timeline_axis: view_timeline_axis::SpecifiedValue(axes.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            super::timeline_to_css(&self.view_timeline_name.0, &self.view_timeline_axis.0, dest)
        }
    }
}

#[cfg(feature = "gecko")]
pub mod animation_range {
    pub use crate::properties::generated::shorthands::animation_range::*;

    use super::*;
    use crate::properties::longhands::{animation_range_end, animation_range_start};
    use crate::values::specified::LengthPercentage;

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut starts = Vec::with_capacity(1);
        let mut ends = Vec::with_capacity(1);
        input.parse_comma_separated(|input| {
            let start = animation_range_start::single_value::parse(context, input)?;
            let end = input
                .try_parse(|i| animation_range_end::single_value::parse(context, i))
                .unwrap_or_else(|_| {
                    use crate::values::generics::animation::AnimationRangeEnd;
                    use crate::values::specified::animation::{
                        AnimationRangeValue, TimelineRangeName,
                    };

                    // If `<animation-range-start>` includes a timeline range name,
                    // `animation-range-end` is set to that same timeline range name and 100%.
                    // Otherwise, any omitted longhand is set to its initial value.
                    let name = if start.0.name.is_none() {
                        TimelineRangeName::Normal
                    } else {
                        start.0.name
                    };
                    AnimationRangeEnd(AnimationRangeValue::new(
                        name,
                        LengthPercentage::hundred_percent(),
                    ))
                });

            starts.push(start);
            ends.push(end);
            Ok(())
        })?;

        Ok(expanded! {
            animation_range_start: animation_range_start::SpecifiedValue(starts.into()),
            animation_range_end: animation_range_end::SpecifiedValue(ends.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::values::specified::length::EqualsPercentage;
            let starts = &self.animation_range_start.0;
            let ends = &self.animation_range_end.0;
            if starts.len() != ends.len() {
                return Ok(());
            }

            for (i, (start, end)) in std::iter::zip(starts.iter(), ends.iter()).enumerate() {
                if i != 0 {
                    dest.write_str(", ")?;
                }
                start.to_css(dest)?;
                let can_omit_end = (start.0.name == end.0.name && end.0.lp.equals_percentage(1.0))
                    || (start.0.name.is_none() && end.0.name.is_normal());
                if !can_omit_end {
                    dest.write_char(' ')?;
                    end.to_css(dest)?;
                }
            }
            Ok(())
        }
    }
}

pub mod transition {
    pub use crate::properties::generated::shorthands::transition::*;

    use super::*;
    use crate::properties::longhands::{
        transition_behavior, transition_delay, transition_duration, transition_property,
        transition_timing_function,
    };
    use crate::values::specified::TransitionProperty;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        struct SingleTransition {
            transition_property: transition_property::SingleSpecifiedValue,
            transition_duration: transition_duration::SingleSpecifiedValue,
            transition_timing_function: transition_timing_function::SingleSpecifiedValue,
            transition_delay: transition_delay::SingleSpecifiedValue,
            transition_behavior: transition_behavior::SingleSpecifiedValue,
        }

        fn parse_one_transition<'i, 't>(
            context: &ParserContext,
            input: &mut Parser<'i, 't>,
            first: bool,
        ) -> Result<SingleTransition, ParseError<'i>> {
            let mut property = None;
            let mut duration = None;
            let mut timing_function = None;
            let mut delay = None;
            let mut behavior = None;

            let mut parsed = 0;
            loop {
                parsed += 1;

                try_parse_one!(
                    context,
                    input,
                    duration,
                    transition_duration::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    timing_function,
                    transition_timing_function::single_value::parse
                );
                try_parse_one!(context, input, delay, transition_delay::single_value::parse);
                try_parse_one!(
                    context,
                    input,
                    behavior,
                    transition_behavior::single_value::parse
                );
                if property.is_none() {
                    if let Ok(value) = input.try_parse(|i| TransitionProperty::parse(context, i)) {
                        property = Some(value);
                        continue;
                    }

                    if first && input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
                        property = Some(TransitionProperty::none());
                        continue;
                    }
                }

                parsed -= 1;
                break;
            }

            if parsed != 0 {
                Ok(SingleTransition {
                    transition_property: property.unwrap_or_else(
                        transition_property::single_value::get_initial_specified_value,
                    ),
                    transition_duration: duration.unwrap_or_else(
                        transition_duration::single_value::get_initial_specified_value,
                    ),
                    transition_timing_function: timing_function.unwrap_or_else(
                        transition_timing_function::single_value::get_initial_specified_value,
                    ),
                    transition_delay: delay.unwrap_or_else(
                        transition_delay::single_value::get_initial_specified_value,
                    ),
                    transition_behavior: behavior.unwrap_or_else(
                        transition_behavior::single_value::get_initial_specified_value,
                    ),
                })
            } else {
                Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
            }
        }

        let mut first = true;
        let mut has_transition_property_none = false;
        let results = input.parse_comma_separated(|i| {
            if has_transition_property_none {
                return Err(i.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
            let transition = parse_one_transition(context, i, first)?;
            first = false;
            has_transition_property_none = transition.transition_property.is_none();
            Ok(transition)
        })?;

        let len = results.len();
        let mut property = Vec::with_capacity(len);
        let mut duration = Vec::with_capacity(len);
        let mut timing_function = Vec::with_capacity(len);
        let mut delay = Vec::with_capacity(len);
        let mut behavior = Vec::with_capacity(len);
        for result in results {
            property.push(result.transition_property);
            duration.push(result.transition_duration);
            timing_function.push(result.transition_timing_function);
            delay.push(result.transition_delay);
            behavior.push(result.transition_behavior);
        }

        Ok(Longhands {
            transition_property: transition_property::SpecifiedValue(property.into()),
            transition_duration: transition_duration::SpecifiedValue(duration.into()),
            transition_timing_function: transition_timing_function::SpecifiedValue(
                timing_function.into(),
            ),
            transition_delay: transition_delay::SpecifiedValue(delay.into()),
            transition_behavior: transition_behavior::SpecifiedValue(behavior.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::Zero;
            use style_traits::values::SequenceWriter;

            let len = self.transition_property.0.len();
            debug_assert_ne!(
                len, 0,
                "We should always have at least one transition-property, even if none"
            );
            if self.transition_duration.0.len() != len {
                return Ok(());
            }
            if self.transition_delay.0.len() != len {
                return Ok(());
            }
            if self.transition_timing_function.0.len() != len {
                return Ok(());
            }
            if self.transition_behavior.0.len() != len {
                return Ok(());
            }
            for i in 0..len {
                if i != 0 {
                    dest.write_str(", ")?;
                }

                let has_duration = !self.transition_duration.0[i].is_zero();
                let has_timing = !self.transition_timing_function.0[i].is_ease();
                let has_delay = !self.transition_delay.0[i].is_zero();
                let has_behavior = !self.transition_behavior.0[i].is_normal();
                let has_any = has_duration || has_timing || has_delay || has_behavior;

                let mut writer = SequenceWriter::new(dest, " ");
                if !self.transition_property.0[i].is_all() || !has_any {
                    writer.item(&self.transition_property.0[i])?;
                }
                if has_duration || has_delay {
                    writer.item(&self.transition_duration.0[i])?;
                }
                if has_timing {
                    writer.item(&self.transition_timing_function.0[i])?;
                }
                if has_delay {
                    writer.item(&self.transition_delay.0[i])?;
                }
                if has_behavior {
                    writer.item(&self.transition_behavior.0[i])?;
                }
            }
            Ok(())
        }
    }
}

pub mod outline {
    pub use crate::properties::generated::shorthands::outline::*;

    use super::*;
    use crate::properties::longhands::{outline_color, outline_style, outline_width};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let _unused = context;
        let mut color = None;
        let mut style = None;
        let mut width = None;
        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, color, specified::Color::parse);
            try_parse_one!(context, input, style, outline_style::parse);
            try_parse_one!(context, input, width, outline_width::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            outline_color: unwrap_or_initial!(outline_color, color),
            outline_style: unwrap_or_initial!(outline_style, style),
            outline_width: unwrap_or_initial!(outline_width, width),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let mut writer = SequenceWriter::new(dest, " ");
            if *self.outline_color != outline_color::get_initial_specified_value() {
                writer.item(self.outline_color)?;
            }
            if *self.outline_style != outline_style::get_initial_specified_value() {
                writer.item(self.outline_style)?;
            }
            if *self.outline_width != outline_width::get_initial_specified_value() {
                writer.item(self.outline_width)?;
            }
            if !writer.has_written() {
                self.outline_style.to_css(dest)?;
            }
            Ok(())
        }
    }
}

pub mod background_position {
    pub use crate::properties::generated::shorthands::background_position::*;

    use super::*;
    use crate::properties::longhands::{background_position_x, background_position_y};
    use crate::values::specified::position::Position;
    use crate::values::specified::AllowQuirks;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut position_x = Vec::with_capacity(1);
        let mut position_y = Vec::with_capacity(1);
        let mut any = false;

        input.parse_comma_separated(|input| {
            let value = Position::parse_three_value_quirky(context, input, AllowQuirks::Yes)?;
            position_x.push(value.horizontal);
            position_y.push(value.vertical);
            any = true;
            Ok(())
        })?;
        if !any {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(expanded! {
            background_position_x: background_position_x::SpecifiedValue(position_x.into()),
            background_position_y: background_position_y::SpecifiedValue(position_y.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let len = self.background_position_x.0.len();
            if len == 0 || len != self.background_position_y.0.len() {
                return Ok(());
            }
            for i in 0..len {
                Position {
                    horizontal: self.background_position_x.0[i].clone(),
                    vertical: self.background_position_y.0[i].clone(),
                }
                .to_css(dest)?;

                if i < len - 1 {
                    dest.write_str(", ")?;
                }
            }
            Ok(())
        }
    }
}

pub mod background {
    pub use crate::properties::generated::shorthands::background::*;

    use super::*;
    use crate::properties::longhands::background_clip;
    use crate::properties::longhands::background_clip::single_value::computed_value::T as Clip;
    use crate::properties::longhands::background_origin::single_value::computed_value::T as Origin;
    use crate::properties::longhands::{
        background_attachment, background_color, background_image, background_origin,
        background_size,
    };
    use crate::properties::longhands::{
        background_position_x, background_position_y, background_repeat,
    };
    use crate::values::specified::{AllowQuirks, Color, Position, PositionComponent};

    impl From<background_origin::single_value::SpecifiedValue>
        for background_clip::single_value::SpecifiedValue
    {
        fn from(
            origin: background_origin::single_value::SpecifiedValue,
        ) -> background_clip::single_value::SpecifiedValue {
            match origin {
                background_origin::single_value::SpecifiedValue::ContentBox => {
                    background_clip::single_value::SpecifiedValue::ContentBox
                },
                background_origin::single_value::SpecifiedValue::PaddingBox => {
                    background_clip::single_value::SpecifiedValue::PaddingBox
                },
                background_origin::single_value::SpecifiedValue::BorderBox => {
                    background_clip::single_value::SpecifiedValue::BorderBox
                },
            }
        }
    }

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut background_color = None;

        let mut background_image = Vec::with_capacity(1);
        let mut background_position_x = Vec::with_capacity(1);
        let mut background_position_y = Vec::with_capacity(1);
        let mut background_repeat = Vec::with_capacity(1);
        let mut background_size = Vec::with_capacity(1);
        let mut background_attachment = Vec::with_capacity(1);
        let mut background_origin = Vec::with_capacity(1);
        let mut background_clip = Vec::with_capacity(1);
        input.parse_comma_separated(|input| {
            if background_color.is_some() {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }

            let mut image = None;
            let mut position = None;
            let mut repeat = None;
            let mut size = None;
            let mut attachment = None;
            let mut origin = None;
            let mut clip = None;
            let mut parsed = 0;
            loop {
                parsed += 1;
                try_parse_one!(context, input, background_color, Color::parse);
                if position.is_none() {
                    if let Ok(value) = input.try_parse(|input| {
                        Position::parse_three_value_quirky(context, input, AllowQuirks::No)
                    }) {
                        position = Some(value);

                        size = input
                            .try_parse(|input| {
                                input.expect_delim('/')?;
                                background_size::single_value::parse(context, input)
                            })
                            .ok();

                        continue;
                    }
                }
                try_parse_one!(context, input, image, background_image::single_value::parse);
                try_parse_one!(
                    context,
                    input,
                    repeat,
                    background_repeat::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    attachment,
                    background_attachment::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    origin,
                    background_origin::single_value::parse
                );
                try_parse_one!(context, input, clip, background_clip::single_value::parse);
                parsed -= 1;
                break;
            }
            if parsed == 0 {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
            if clip.is_none() {
                if let Some(origin) = origin {
                    clip = Some(background_clip::single_value::SpecifiedValue::from(origin));
                }
            }
            if let Some(position) = position {
                background_position_x.push(position.horizontal);
                background_position_y.push(position.vertical);
            } else {
                background_position_x.push(PositionComponent::zero());
                background_position_y.push(PositionComponent::zero());
            }
            if let Some(bg_image) = image {
                background_image.push(bg_image);
            } else {
                background_image
                    .push(background_image::single_value::get_initial_specified_value());
            }
            if let Some(bg_repeat) = repeat {
                background_repeat.push(bg_repeat);
            } else {
                background_repeat
                    .push(background_repeat::single_value::get_initial_specified_value());
            }
            if let Some(bg_size) = size {
                background_size.push(bg_size);
            } else {
                background_size.push(background_size::single_value::get_initial_specified_value());
            }
            if let Some(bg_attachment) = attachment {
                background_attachment.push(bg_attachment);
            } else {
                background_attachment
                    .push(background_attachment::single_value::get_initial_specified_value());
            }
            if let Some(bg_origin) = origin {
                background_origin.push(bg_origin);
            } else {
                background_origin
                    .push(background_origin::single_value::get_initial_specified_value());
            }
            if let Some(bg_clip) = clip {
                background_clip.push(bg_clip);
            } else {
                background_clip.push(background_clip::single_value::get_initial_specified_value());
            }
            Ok(())
        })?;

        Ok(expanded! {
            background_color: background_color.unwrap_or(Color::transparent()),
            background_image: background_image::SpecifiedValue(background_image.into()),
            background_position_x: background_position_x::SpecifiedValue(background_position_x.into()),
            background_position_y: background_position_y::SpecifiedValue(background_position_y.into()),
            background_repeat: background_repeat::SpecifiedValue(background_repeat.into()),
            background_size: background_size::SpecifiedValue(background_size.into()),
            background_attachment: background_attachment::SpecifiedValue(background_attachment.into()),
            background_origin: background_origin::SpecifiedValue(background_origin.into()),
            background_clip: background_clip::SpecifiedValue(background_clip.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let len = self.background_image.0.len();
            if len == 0 {
                return Ok(());
            }
            if len != self.background_image.0.len() {
                return Ok(());
            }
            if len != self.background_position_x.0.len() {
                return Ok(());
            }
            if len != self.background_position_y.0.len() {
                return Ok(());
            }
            if len != self.background_size.0.len() {
                return Ok(());
            }
            if len != self.background_repeat.0.len() {
                return Ok(());
            }
            if len != self.background_origin.0.len() {
                return Ok(());
            }
            if len != self.background_clip.0.len() {
                return Ok(());
            }
            if len != self.background_attachment.0.len() {
                return Ok(());
            }

            for i in 0..len {
                let image = &self.background_image.0[i];
                let position_x = &self.background_position_x.0[i];
                let position_y = &self.background_position_y.0[i];
                let repeat = &self.background_repeat.0[i];
                let size = &self.background_size.0[i];
                let attachment = &self.background_attachment.0[i];
                let origin = &self.background_origin.0[i];
                let clip = &self.background_clip.0[i];

                if i != 0 {
                    dest.write_str(", ")?;
                }

                let mut writer = SequenceWriter::new(dest, " ");
                if *image != background_image::single_value::get_initial_specified_value() {
                    writer.item(image)?;
                }

                if *position_x != PositionComponent::zero()
                    || *position_y != PositionComponent::zero()
                    || *size != background_size::single_value::get_initial_specified_value()
                {
                    writer.write_item(|dest| {
                        Position {
                            horizontal: position_x.clone(),
                            vertical: position_y.clone(),
                        }
                        .to_css(dest)?;
                        if *size != background_size::single_value::get_initial_specified_value() {
                            dest.write_str(" / ")?;
                            size.to_css(dest)?;
                        }
                        Ok(())
                    })?;
                }
                if *repeat != background_repeat::single_value::get_initial_specified_value() {
                    writer.item(repeat)?;
                }
                if *attachment != background_attachment::single_value::get_initial_specified_value()
                {
                    writer.item(attachment)?;
                }

                if *origin != Origin::PaddingBox || *clip != Clip::BorderBox {
                    writer.item(origin)?;
                    if *clip != From::from(*origin) {
                        writer.item(clip)?;
                    }
                }

                if i == len - 1 {
                    if *self.background_color != background_color::get_initial_specified_value() {
                        writer.item(self.background_color)?;
                    }
                }

                if !writer.has_written() {
                    image.to_css(dest)?;
                }
            }

            Ok(())
        }
    }
}

pub mod font {
    pub use crate::properties::generated::shorthands::font::*;

    use super::*;
    #[cfg(feature = "gecko")]
    use crate::properties::longhands::{
        font_family, font_feature_settings, font_language_override, font_size,
        font_size_adjust, font_variant_alternates, font_variant_east_asian, font_variant_emoji,
        font_variant_ligatures, font_variant_numeric, font_variant_position,
    };
    use crate::properties::longhands::{
        font_kerning, font_optical_sizing, font_stretch, font_style, font_variant_caps, font_variation_settings,
        font_weight,
    };
    #[cfg(feature = "gecko")]
    use crate::values::specified::font::SystemFont;
    use crate::values::specified::font::{
        FontFamily, FontSize, FontStretch, FontStretchKeyword, FontStyle, FontWeight, LineHeight,
    };

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut nb_normals = 0;
        let mut style = None;
        let mut variant_caps = None;
        let mut weight = None;
        let mut stretch = None;
        #[cfg(feature = "gecko")]
        if let Ok(sys) = input.try_parse(|i| SystemFont::parse(context, i)) {
            return Ok(Longhands {
                font_family: font_family::SpecifiedValue::system_font(sys),
                font_size: font_size::SpecifiedValue::system_font(sys),
                font_style: font_style::SpecifiedValue::system_font(sys),
                font_stretch: font_stretch::SpecifiedValue::system_font(sys),
                font_weight: font_weight::SpecifiedValue::system_font(sys),
                line_height: LineHeight::normal(),
                font_kerning: font_kerning::get_initial_specified_value(),
                font_language_override: font_language_override::get_initial_specified_value(),
                font_size_adjust: font_size_adjust::get_initial_specified_value(),
                font_variant_alternates: font_variant_alternates::get_initial_specified_value(),
                font_variant_east_asian: font_variant_east_asian::get_initial_specified_value(),
                font_variant_emoji: font_variant_emoji::get_initial_specified_value(),
                font_variant_ligatures: font_variant_ligatures::get_initial_specified_value(),
                font_variant_numeric: font_variant_numeric::get_initial_specified_value(),
                font_variant_position: font_variant_position::get_initial_specified_value(),
                font_feature_settings: font_feature_settings::get_initial_specified_value(),
                font_optical_sizing: font_optical_sizing::get_initial_specified_value(),
                font_variant_caps: font_variant_caps::get_initial_specified_value(),
                font_variation_settings: font_variation_settings::get_initial_specified_value(),
            });
        }

        let size;
        loop {
            if input
                .try_parse(|input| input.expect_ident_matching("normal"))
                .is_ok()
            {
                nb_normals += 1;
                continue;
            }
            try_parse_one!(context, input, style, font_style::parse);
            try_parse_one!(context, input, weight, font_weight::parse);
            if variant_caps.is_none() {
                if input
                    .try_parse(|input| input.expect_ident_matching("small-caps"))
                    .is_ok()
                {
                    variant_caps = Some(font_variant_caps::SpecifiedValue::SmallCaps);
                    continue;
                }
            }
            try_parse_one!(input, stretch, FontStretchKeyword::parse);
            size = FontSize::parse(context, input)?;
            break;
        }

        let line_height = if input.try_parse(|input| input.expect_delim('/')).is_ok() {
            Some(LineHeight::parse(context, input)?)
        } else {
            None
        };

        #[inline]
        fn count<T>(opt: &Option<T>) -> u8 {
            if opt.is_some() {
                1
            } else {
                0
            }
        }

        if (count(&style) + count(&weight) + count(&variant_caps) + count(&stretch) + nb_normals)
            > 4
        {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        let family = FontFamily::parse(context, input)?;
        let stretch = stretch.map(FontStretch::Keyword);
        Ok(Longhands {
            font_style: unwrap_or_initial!(font_style, style),
            font_weight: unwrap_or_initial!(font_weight, weight),
            font_stretch: unwrap_or_initial!(font_stretch, stretch),
            font_variant_caps: unwrap_or_initial!(font_variant_caps, variant_caps),
            font_size: size,
            line_height: line_height.unwrap_or(LineHeight::normal()),
            font_family: family,
            font_optical_sizing: font_optical_sizing::get_initial_specified_value(),
            font_variation_settings: font_variation_settings::get_initial_specified_value(),
            font_kerning: font_kerning::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_language_override: font_language_override::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_size_adjust: font_size_adjust::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_alternates: font_variant_alternates::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_east_asian: font_variant_east_asian::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_emoji: font_variant_emoji::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_ligatures: font_variant_ligatures::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_numeric: font_variant_numeric::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_variant_position: font_variant_position::get_initial_specified_value(),
            #[cfg(feature = "gecko")]
            font_feature_settings: font_feature_settings::get_initial_specified_value(),
        })
    }

    #[cfg(feature = "gecko")]
    enum CheckSystemResult {
        AllSystem(SystemFont),
        SomeSystem,
        None,
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            #[cfg(feature = "gecko")]
            match self.check_system() {
                CheckSystemResult::AllSystem(sys) => return sys.to_css(dest),
                CheckSystemResult::SomeSystem => return Ok(()),
                CheckSystemResult::None => {},
            }

            if let Some(v) = self.font_optical_sizing {
                if v != &font_optical_sizing::get_initial_specified_value() {
                    return Ok(());
                }
            }
            if let Some(v) = self.font_variation_settings {
                if v != &font_variation_settings::get_initial_specified_value() {
                    return Ok(());
                }
            }
            #[cfg(feature = "gecko")]
            if let Some(v) = self.font_variant_emoji {
                if v != &font_variant_emoji::get_initial_specified_value() {
                    return Ok(());
                }
            }

            if self.font_kerning != &font_kerning::get_initial_specified_value() {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_language_override != &font_language_override::get_initial_specified_value()
            {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_size_adjust != &font_size_adjust::get_initial_specified_value() {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_variant_alternates
                != &font_variant_alternates::get_initial_specified_value()
            {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_variant_east_asian
                != &font_variant_east_asian::get_initial_specified_value()
            {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_variant_ligatures != &font_variant_ligatures::get_initial_specified_value()
            {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_variant_numeric != &font_variant_numeric::get_initial_specified_value() {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_variant_position != &font_variant_position::get_initial_specified_value() {
                return Ok(());
            }
            #[cfg(feature = "gecko")]
            if self.font_feature_settings != &font_feature_settings::get_initial_specified_value() {
                return Ok(());
            }

            let font_stretch = match *self.font_stretch {
                FontStretch::Keyword(kw) => kw,
                FontStretch::Stretch(percentage) => {
                    match FontStretchKeyword::from_percentage(percentage.0.get()) {
                        Some(kw) => kw,
                        None => return Ok(()),
                    }
                },
                FontStretch::System(..) => return Ok(()),
            };

            if self.font_variant_caps != &font_variant_caps::get_initial_specified_value()
                && *self.font_variant_caps != font_variant_caps::SpecifiedValue::SmallCaps
            {
                return Ok(());
            }

            if self.font_style != &font_style::get_initial_specified_value() {
                self.font_style.to_css(dest)?;
                dest.write_char(' ')?;
            }
            if self.font_variant_caps != &font_variant_caps::get_initial_specified_value() {
                self.font_variant_caps.to_css(dest)?;
                dest.write_char(' ')?;
            }

            if self.font_weight != &FontWeight::normal()
                && self.font_weight != &FontWeight::from_gecko_keyword(400)
            {
                self.font_weight.to_css(dest)?;
                dest.write_char(' ')?;
            }

            if font_stretch != FontStretchKeyword::Normal {
                font_stretch.to_css(dest)?;
                dest.write_char(' ')?;
            }

            self.font_size.to_css(dest)?;

            if *self.line_height != LineHeight::normal() {
                dest.write_str(" / ")?;
                self.line_height.to_css(dest)?;
            }

            dest.write_char(' ')?;
            self.font_family.to_css(dest)?;

            Ok(())
        }
    }

    impl<'a> LonghandsToSerialize<'a> {
        #[cfg(feature = "gecko")]
        fn check_system(&self) -> CheckSystemResult {
            let mut sys = None;
            let mut all = true;

            macro_rules! check {
                ($v:expr) => {
                    match $v.get_system() {
                        Some(s) => {
                            debug_assert!(sys.is_none() || s == sys.unwrap());
                            sys = Some(s);
                        },
                        None => {
                            all = false;
                        },
                    }
                };
                ($e:expr, $($es:expr),+) => { check!($e); check!($($es),*); };
            }

            check!(
                self.font_family,
                self.font_size,
                self.font_style,
                self.font_stretch,
                self.font_weight
            );

            if self.line_height != &LineHeight::normal() {
                all = false
            }
            if all {
                CheckSystemResult::AllSystem(sys.unwrap())
            } else if sys.is_some() {
                CheckSystemResult::SomeSystem
            } else {
                CheckSystemResult::None
            }
        }
    }

    impl SpecifiedValueInfo for Longhands {
        const SUPPORTED_TYPES: u8 = FontStyle::SUPPORTED_TYPES
            | FontWeight::SUPPORTED_TYPES
            | FontStretch::SUPPORTED_TYPES
            | font_variant_caps::SpecifiedValue::SUPPORTED_TYPES
            | FontSize::SUPPORTED_TYPES
            | FontFamily::SUPPORTED_TYPES;

        fn collect_completion_keywords(f: KeywordsCollectFn) {
            FontStyle::collect_completion_keywords(f);
            FontWeight::collect_completion_keywords(f);
            FontStretch::collect_completion_keywords(f);
            font_variant_caps::SpecifiedValue::collect_completion_keywords(f);
            FontSize::collect_completion_keywords(f);
            FontFamily::collect_completion_keywords(f);

            #[cfg(feature = "gecko")]
            SystemFont::collect_completion_keywords(f);
        }
    }
}

pub mod font_variant {
    pub use crate::properties::generated::shorthands::font_variant::*;

    use super::*;
    use crate::properties::longhands::font_variant_caps;
    #[cfg(feature = "gecko")]
    use crate::properties::longhands::{
        font_variant_alternates, font_variant_east_asian, font_variant_emoji,
        font_variant_ligatures, font_variant_numeric, font_variant_position,
    };
    #[allow(unused_imports)]
    use crate::values::specified::FontVariantLigatures;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        #[cfg(feature = "gecko")]
        let mut ligatures = None;
        let mut caps = None;
        #[cfg(feature = "gecko")]
        let mut alternates = None;
        #[cfg(feature = "gecko")]
        let mut numeric = None;
        #[cfg(feature = "gecko")]
        let mut east_asian = None;
        #[cfg(feature = "gecko")]
        let mut position = None;
        #[cfg(feature = "gecko")]
        let mut emoji = None;

        if input
            .try_parse(|input| input.expect_ident_matching("normal"))
            .is_ok()
        {
        } else if input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            #[cfg(feature = "gecko")]
            {
                ligatures = Some(FontVariantLigatures::NONE);
            }
        } else {
            let mut parsed = 0;
            loop {
                parsed += 1;
                if input
                    .try_parse(|input| input.expect_ident_matching("normal"))
                    .is_ok()
                    || input
                        .try_parse(|input| input.expect_ident_matching("none"))
                        .is_ok()
                {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, ligatures, font_variant_ligatures::parse);
                try_parse_one!(context, input, caps, font_variant_caps::parse);
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, alternates, font_variant_alternates::parse);
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, numeric, font_variant_numeric::parse);
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, east_asian, font_variant_east_asian::parse);
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, position, font_variant_position::parse);
                #[cfg(feature = "gecko")]
                try_parse_one!(context, input, emoji, font_variant_emoji::parse);
                parsed -= 1;
                break;
            }

            if parsed == 0 {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
        }

        #[cfg(feature = "gecko")]
        return Ok(expanded! {
            font_variant_ligatures: unwrap_or_initial!(font_variant_ligatures, ligatures),
            font_variant_caps: unwrap_or_initial!(font_variant_caps, caps),
            font_variant_alternates: unwrap_or_initial!(font_variant_alternates, alternates),
            font_variant_numeric: unwrap_or_initial!(font_variant_numeric, numeric),
            font_variant_east_asian: unwrap_or_initial!(font_variant_east_asian, east_asian),
            font_variant_position: unwrap_or_initial!(font_variant_position, position),
            font_variant_emoji: unwrap_or_initial!(font_variant_emoji, emoji),
        });
        #[cfg(feature = "servo")]
        return Ok(expanded! {
            font_variant_caps: unwrap_or_initial!(font_variant_caps, caps),
        });
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        #[allow(unused_assignments)]
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            #[cfg(feature = "gecko")]
            let has_none_ligatures = self.font_variant_ligatures == &FontVariantLigatures::NONE;
            #[cfg(feature = "servo")]
            let has_none_ligatures = false;

            #[cfg(feature = "gecko")]
            const TOTAL_SUBPROPS: usize = 7;
            #[cfg(feature = "servo")]
            const TOTAL_SUBPROPS: usize = 1;
            let mut nb_normals = 0;
            macro_rules! count_normal {
                ($e: expr, $p: ident) => {
                    if *$e == $p::get_initial_specified_value() {
                        nb_normals += 1;
                    }
                };
                ($v: ident) => {
                    count_normal!(self.$v, $v);
                };
            }
            #[cfg(feature = "gecko")]
            count_normal!(font_variant_ligatures);
            count_normal!(font_variant_caps);
            #[cfg(feature = "gecko")]
            count_normal!(font_variant_alternates);
            #[cfg(feature = "gecko")]
            count_normal!(font_variant_numeric);
            #[cfg(feature = "gecko")]
            count_normal!(font_variant_east_asian);
            #[cfg(feature = "gecko")]
            count_normal!(font_variant_position);
            #[cfg(feature = "gecko")]
            if let Some(value) = self.font_variant_emoji {
                if value == &font_variant_emoji::get_initial_specified_value() {
                    nb_normals += 1;
                }
            } else {
                nb_normals += 1;
            }

            if nb_normals == TOTAL_SUBPROPS {
                return dest.write_str("normal");
            }
            if has_none_ligatures {
                if nb_normals == TOTAL_SUBPROPS - 1 {
                    dest.write_str("none")?;
                }
                return Ok(());
            }

            let mut writer = SequenceWriter::new(dest, " ");
            macro_rules! write {
                ($e: expr, $p: ident) => {
                    if *$e != $p::get_initial_specified_value() {
                        writer.item($e)?;
                    }
                };
                ($v: ident) => {
                    write!(self.$v, $v);
                };
            }

            #[cfg(feature = "gecko")]
            write!(font_variant_ligatures);
            write!(font_variant_caps);
            #[cfg(feature = "gecko")]
            write!(font_variant_alternates);
            #[cfg(feature = "gecko")]
            write!(font_variant_numeric);
            #[cfg(feature = "gecko")]
            write!(font_variant_east_asian);
            #[cfg(feature = "gecko")]
            write!(font_variant_position);
            #[cfg(feature = "gecko")]
            if let Some(v) = self.font_variant_emoji {
                write!(v, font_variant_emoji);
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod font_synthesis {
    pub use crate::properties::generated::shorthands::font_synthesis::*;

    use super::*;
    use crate::values::specified::{FontSynthesis, FontSynthesisStyle};

    pub fn parse_value<'i, 't>(
        _context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut weight = FontSynthesis::None;
        let mut style = FontSynthesisStyle::None;
        let mut small_caps = FontSynthesis::None;
        let mut position = FontSynthesis::None;

        if !input
            .try_parse(|input| input.expect_ident_matching("none"))
            .is_ok()
        {
            let mut has_custom_value = false;
            while !input.is_exhausted() {
                try_match_ident_ignore_ascii_case! { input,
                    "weight" if weight == FontSynthesis::None => {
                        has_custom_value = true;
                        weight = FontSynthesis::Auto;
                        continue;
                    },
                    "style" if style == FontSynthesisStyle::None => {
                        has_custom_value = true;
                        style = FontSynthesisStyle::Auto;
                        continue;
                    },
                    "small-caps" if small_caps == FontSynthesis::None => {
                        has_custom_value = true;
                        small_caps = FontSynthesis::Auto;
                        continue;
                    },
                    "position" if position == FontSynthesis::None => {
                        has_custom_value = true;
                        position = FontSynthesis::Auto;
                        continue;
                    },
                    "oblique-only" if style == FontSynthesisStyle::None => {
                        has_custom_value = true;
                        style = FontSynthesisStyle::ObliqueOnly;
                        continue;
                    },
                }
            }
            if !has_custom_value {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
        }

        Ok(expanded! {
            font_synthesis_weight: weight,
            font_synthesis_style: style,
            font_synthesis_small_caps: small_caps,
            font_synthesis_position: position,
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let mut writer = SequenceWriter::new(dest, " ");
            if self.font_synthesis_weight == &FontSynthesis::Auto {
                writer.raw_item("weight")?;
            }
            if self.font_synthesis_style != &FontSynthesisStyle::None {
                if self.font_synthesis_style == &FontSynthesisStyle::Auto {
                    writer.raw_item("style")?;
                } else {
                    writer.raw_item("oblique-only")?;
                }
            }
            if self.font_synthesis_small_caps == &FontSynthesis::Auto {
                writer.raw_item("small-caps")?;
            }
            if self.font_synthesis_position == &FontSynthesis::Auto {
                writer.raw_item("position")?;
            }
            if !writer.has_written() {
                writer.raw_item("none")?;
            }
            Ok(())
        }
    }

    // The shorthand takes the sub-property names of the longhands, and not the
    // 'auto' keyword like they do, so we can't automatically derive this.
    impl SpecifiedValueInfo for Longhands {
        fn collect_completion_keywords(f: KeywordsCollectFn) {
            f(&[
                "none",
                "oblique-only",
                "small-caps",
                "position",
                "style",
                "weight",
            ]);
        }
    }
}

#[cfg(feature = "gecko")]
pub mod text_box {
    pub use crate::properties::generated::shorthands::text_box::*;

    use super::*;
    use crate::values::specified::{TextBoxEdge, TextBoxTrim};

    pub fn parse_value<'i>(
        context: &ParserContext,
        input: &mut Parser<'i, '_>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut trim = None;
        let mut edge = None;

        if input
            .try_parse(|input| input.expect_ident_matching("normal"))
            .is_ok()
        {
            return Ok(Longhands {
                text_box_trim: TextBoxTrim::NONE,
                text_box_edge: TextBoxEdge::Auto,
            });
        }

        loop {
            try_parse_one!(context, input, trim, TextBoxTrim::parse);
            try_parse_one!(context, input, edge, TextBoxEdge::parse);
            break;
        }

        if trim.is_none() && edge.is_none() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        // From https://drafts.csswg.org/css-inline-3/#text-box-shorthand:
        // > Omitting the 'text-box-trim' value sets it to 'trim-both'
        // > (not the initial value), while omitting the 'text-box-edge'
        // > value sets it to auto (the initial value).
        Ok(Longhands {
            text_box_trim: trim.unwrap_or(TextBoxTrim::TRIM_BOTH),
            text_box_edge: edge.unwrap_or(TextBoxEdge::Auto),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if *self.text_box_trim == TextBoxTrim::NONE && *self.text_box_edge == TextBoxEdge::Auto
            {
                return dest.write_str("normal");
            }

            let mut writer = SequenceWriter::new(dest, " ");
            if *self.text_box_trim != specified::TextBoxTrim::TRIM_BOTH {
                writer.item(self.text_box_trim)?;
            }
            if *self.text_box_edge != specified::TextBoxEdge::Auto {
                writer.item(self.text_box_edge)?;
            }
            if !writer.has_written() {
                self.text_box_trim.to_css(dest)?;
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod text_emphasis {
    pub use crate::properties::generated::shorthands::text_emphasis::*;

    use super::*;
    use crate::properties::longhands::{text_emphasis_color, text_emphasis_style};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut color = None;
        let mut style = None;
        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, color, text_emphasis_color::parse);
            try_parse_one!(context, input, style, text_emphasis_style::parse);
            parsed -= 1;
            break;
        }
        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(expanded! {
            text_emphasis_color: unwrap_or_initial!(text_emphasis_color, color),
            text_emphasis_style: unwrap_or_initial!(text_emphasis_style, style),
        })
    }
}

pub mod text_decoration {
    pub use crate::properties::generated::shorthands::text_decoration::*;

    use super::*;
    #[cfg(feature = "gecko")]
    use crate::properties::longhands::text_decoration_thickness;
    use crate::properties::longhands::{
        text_decoration_color, text_decoration_line, text_decoration_style,
    };

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut line = None;
        let mut style = None;
        let mut color = None;
        #[cfg(feature = "gecko")]
        let mut thickness = None;

        let mut parsed = 0;
        loop {
            parsed += 1;
            try_parse_one!(context, input, line, text_decoration_line::parse);
            try_parse_one!(context, input, style, text_decoration_style::parse);
            try_parse_one!(context, input, color, text_decoration_color::parse);
            #[cfg(feature = "gecko")]
            try_parse_one!(context, input, thickness, text_decoration_thickness::parse);
            parsed -= 1;
            break;
        }

        if parsed == 0 {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        #[cfg(feature = "gecko")]
        return Ok(expanded! {
            text_decoration_line: unwrap_or_initial!(text_decoration_line, line),
            text_decoration_style: unwrap_or_initial!(text_decoration_style, style),
            text_decoration_color: unwrap_or_initial!(text_decoration_color, color),
            text_decoration_thickness: unwrap_or_initial!(text_decoration_thickness, thickness),
        });
        #[cfg(feature = "servo")]
        return Ok(expanded! {
            text_decoration_line: unwrap_or_initial!(text_decoration_line, line),
            text_decoration_style: unwrap_or_initial!(text_decoration_style, style),
            text_decoration_color: unwrap_or_initial!(text_decoration_color, color),
        });
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        #[allow(unused)]
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::values::specified::Color;
            use crate::values::specified::TextDecorationLine;

            let is_solid_style =
                *self.text_decoration_style == text_decoration_style::SpecifiedValue::Solid;
            let is_current_color = *self.text_decoration_color == Color::CurrentColor;
            #[cfg(feature = "gecko")]
            let is_auto_thickness = self.text_decoration_thickness.is_auto();
            #[cfg(feature = "servo")]
            let is_auto_thickness = true;
            let is_none = *self.text_decoration_line == TextDecorationLine::none();

            let mut writer = SequenceWriter::new(dest, " ");
            if (is_solid_style && is_current_color && is_auto_thickness) || !is_none {
                writer.item(self.text_decoration_line)?;
            }
            #[cfg(feature = "gecko")]
            if !is_auto_thickness {
                writer.item(self.text_decoration_thickness)?;
            }
            if !is_solid_style {
                writer.item(self.text_decoration_style)?;
            }
            if !is_current_color {
                writer.item(self.text_decoration_color)?;
            }
            Ok(())
        }
    }
}

pub mod animation {
    pub use crate::properties::generated::shorthands::animation::*;

    use super::*;
    use crate::properties::longhands::{
        animation_delay, animation_direction, animation_duration, animation_fill_mode,
        animation_iteration_count, animation_name, animation_play_state, animation_range_end,
        animation_range_start, animation_timeline, animation_timing_function,
    };

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        struct SingleAnimation {
            animation_name: animation_name::SingleSpecifiedValue,
            animation_duration: animation_duration::SingleSpecifiedValue,
            animation_timing_function: animation_timing_function::SingleSpecifiedValue,
            animation_delay: animation_delay::SingleSpecifiedValue,
            animation_iteration_count: animation_iteration_count::SingleSpecifiedValue,
            animation_direction: animation_direction::SingleSpecifiedValue,
            animation_fill_mode: animation_fill_mode::SingleSpecifiedValue,
            animation_play_state: animation_play_state::SingleSpecifiedValue,
        }

        fn parse_one_animation<'i, 't>(
            context: &ParserContext,
            input: &mut Parser<'i, 't>,
        ) -> Result<SingleAnimation, ParseError<'i>> {
            let mut name = None;
            let mut duration = None;
            let mut timing_function = None;
            let mut delay = None;
            let mut iteration_count = None;
            let mut direction = None;
            let mut fill_mode = None;
            let mut play_state = None;

            let mut parsed = 0;
            loop {
                parsed += 1;
                try_parse_one!(
                    context,
                    input,
                    duration,
                    animation_duration::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    timing_function,
                    animation_timing_function::single_value::parse
                );
                try_parse_one!(context, input, delay, animation_delay::single_value::parse);
                try_parse_one!(
                    context,
                    input,
                    iteration_count,
                    animation_iteration_count::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    direction,
                    animation_direction::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    fill_mode,
                    animation_fill_mode::single_value::parse
                );
                try_parse_one!(
                    context,
                    input,
                    play_state,
                    animation_play_state::single_value::parse
                );
                try_parse_one!(context, input, name, animation_name::single_value::parse);
                parsed -= 1;
                break;
            }

            if parsed == 0 {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
            Ok(SingleAnimation {
                animation_name: name
                    .unwrap_or_else(animation_name::single_value::get_initial_specified_value),
                animation_duration: duration
                    .unwrap_or_else(animation_duration::single_value::get_initial_specified_value),
                animation_timing_function: timing_function.unwrap_or_else(
                    animation_timing_function::single_value::get_initial_specified_value,
                ),
                animation_delay: delay
                    .unwrap_or_else(animation_delay::single_value::get_initial_specified_value),
                animation_iteration_count: iteration_count.unwrap_or_else(
                    animation_iteration_count::single_value::get_initial_specified_value,
                ),
                animation_direction: direction
                    .unwrap_or_else(animation_direction::single_value::get_initial_specified_value),
                animation_fill_mode: fill_mode
                    .unwrap_or_else(animation_fill_mode::single_value::get_initial_specified_value),
                animation_play_state: play_state.unwrap_or_else(
                    animation_play_state::single_value::get_initial_specified_value,
                ),
            })
        }

        let mut names = vec![];
        let mut durations = vec![];
        let mut timing_functions = vec![];
        let mut delays = vec![];
        let mut iteration_counts = vec![];
        let mut directions = vec![];
        let mut fill_modes = vec![];
        let mut play_states = vec![];

        let results = input.parse_comma_separated(|i| parse_one_animation(context, i))?;
        for result in results.into_iter() {
            names.push(result.animation_name);
            durations.push(result.animation_duration);
            timing_functions.push(result.animation_timing_function);
            delays.push(result.animation_delay);
            iteration_counts.push(result.animation_iteration_count);
            directions.push(result.animation_direction);
            fill_modes.push(result.animation_fill_mode);
            play_states.push(result.animation_play_state);
        }

        Ok(expanded! {
            animation_name: animation_name::SpecifiedValue(names.into()),
            animation_duration: animation_duration::SpecifiedValue(durations.into()),
            animation_timing_function: animation_timing_function::SpecifiedValue(timing_functions.into()),
            animation_delay: animation_delay::SpecifiedValue(delays.into()),
            animation_iteration_count: animation_iteration_count::SpecifiedValue(iteration_counts.into()),
            animation_direction: animation_direction::SpecifiedValue(directions.into()),
            animation_fill_mode: animation_fill_mode::SpecifiedValue(fill_modes.into()),
            animation_play_state: animation_play_state::SpecifiedValue(play_states.into()),
            animation_timeline: animation_timeline::SpecifiedValue(
                vec![animation_timeline::single_value::get_initial_specified_value()].into()
            ),
            // The animation-range properties are reset-only sub-properties of the animation
            // shorthand.
            // https://drafts.csswg.org/scroll-animations-1/#named-range-animation-declaration
            animation_range_start: animation_range_start::SpecifiedValue(
                vec![animation_range_start::single_value::get_initial_specified_value()].into()
            ),
            animation_range_end: animation_range_end::SpecifiedValue(
                vec![animation_range_end::single_value::get_initial_specified_value()].into()
            ),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::values::specified::easing::TimingFunction;
            use crate::values::specified::{
                AnimationDirection, AnimationFillMode, AnimationPlayState,
            };
            use crate::Zero;
            use style_traits::values::SequenceWriter;

            let len = self.animation_name.0.len();
            if len == 0 {
                return Ok(());
            }

            if len != self.animation_duration.0.len() {
                return Ok(());
            }
            if len != self.animation_timing_function.0.len() {
                return Ok(());
            }
            if len != self.animation_delay.0.len() {
                return Ok(());
            }
            if len != self.animation_iteration_count.0.len() {
                return Ok(());
            }
            if len != self.animation_direction.0.len() {
                return Ok(());
            }
            if len != self.animation_fill_mode.0.len() {
                return Ok(());
            }
            if len != self.animation_play_state.0.len() {
                return Ok(());
            }

            // We don't serialize animation-timeline, animation-range-start and animation-range-end
            // if any of them are not the initial value.
            if self
                .animation_timeline
                .map_or(false, |v| v.0.len() != 1 || !v.0[0].is_auto())
            {
                return Ok(());
            }
            if self
                .animation_range_start
                .map_or(false, |v| v.0.len() != 1 || !v.0[0].0.is_normal())
            {
                return Ok(());
            }
            if self
                .animation_range_end
                .map_or(false, |v| v.0.len() != 1 || !v.0[0].0.is_normal())
            {
                return Ok(());
            }

            for i in 0..len {
                if i != 0 {
                    dest.write_str(", ")?;
                }

                let has_duration = !self.animation_duration.0[i].is_auto()
                    && !self.animation_duration.0[i].is_zero();
                let has_timing_function = !self.animation_timing_function.0[i].is_ease();
                let has_delay = !self.animation_delay.0[i].is_zero();
                let has_iteration_count = !self.animation_iteration_count.0[i].is_one();
                let has_direction =
                    !matches!(self.animation_direction.0[i], AnimationDirection::Normal);
                let has_fill_mode =
                    !matches!(self.animation_fill_mode.0[i], AnimationFillMode::None);
                let has_play_state =
                    !matches!(self.animation_play_state.0[i], AnimationPlayState::Running);
                let animation_name = &self.animation_name.0[i];
                let has_name = !animation_name.is_none();

                let mut writer = SequenceWriter::new(dest, " ");

                if has_duration || has_delay {
                    writer.item(&self.animation_duration.0[i])?;
                }

                if has_timing_function || TimingFunction::match_keywords(animation_name) {
                    writer.item(&self.animation_timing_function.0[i])?;
                }

                if has_delay {
                    writer.item(&self.animation_delay.0[i])?;
                }
                if has_iteration_count {
                    writer.item(&self.animation_iteration_count.0[i])?;
                }

                if has_direction || AnimationDirection::match_keywords(animation_name) {
                    writer.item(&self.animation_direction.0[i])?;
                }

                if has_fill_mode || AnimationFillMode::match_keywords(animation_name) {
                    writer.item(&self.animation_fill_mode.0[i])?;
                }

                if has_play_state || AnimationPlayState::match_keywords(animation_name) {
                    writer.item(&self.animation_play_state.0[i])?;
                }

                if has_name || !writer.has_written() {
                    writer.item(animation_name)?;
                }
            }
            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod mask {
    pub use crate::properties::generated::shorthands::mask::*;

    use super::*;
    use crate::parser::Parse;
    use crate::properties::longhands::{
        mask_clip, mask_composite, mask_mode, mask_origin, mask_position_x, mask_position_y,
        mask_repeat,
    };
    use crate::properties::longhands::{mask_image, mask_size};
    use crate::values::specified::{Position, PositionComponent};

    impl From<mask_origin::single_value::SpecifiedValue> for mask_clip::single_value::SpecifiedValue {
        fn from(
            origin: mask_origin::single_value::SpecifiedValue,
        ) -> mask_clip::single_value::SpecifiedValue {
            match origin {
                mask_origin::single_value::SpecifiedValue::ContentBox => {
                    mask_clip::single_value::SpecifiedValue::ContentBox
                },
                mask_origin::single_value::SpecifiedValue::PaddingBox => {
                    mask_clip::single_value::SpecifiedValue::PaddingBox
                },
                mask_origin::single_value::SpecifiedValue::BorderBox => {
                    mask_clip::single_value::SpecifiedValue::BorderBox
                },
                mask_origin::single_value::SpecifiedValue::FillBox => {
                    mask_clip::single_value::SpecifiedValue::FillBox
                },
                mask_origin::single_value::SpecifiedValue::StrokeBox => {
                    mask_clip::single_value::SpecifiedValue::StrokeBox
                },
                mask_origin::single_value::SpecifiedValue::ViewBox => {
                    mask_clip::single_value::SpecifiedValue::ViewBox
                },
            }
        }
    }

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut mask_image = Vec::with_capacity(1);
        let mut mask_mode = Vec::with_capacity(1);
        let mut mask_position_x = Vec::with_capacity(1);
        let mut mask_position_y = Vec::with_capacity(1);
        let mut mask_size = Vec::with_capacity(1);
        let mut mask_repeat = Vec::with_capacity(1);
        let mut mask_origin = Vec::with_capacity(1);
        let mut mask_clip = Vec::with_capacity(1);
        let mut mask_composite = Vec::with_capacity(1);

        input.parse_comma_separated(|input| {
            let mut image = None;
            let mut mode = None;
            let mut position = None;
            let mut size = None;
            let mut repeat = None;
            let mut origin = None;
            let mut clip = None;
            let mut composite = None;
            let mut parsed = 0;
            loop {
                parsed += 1;

                try_parse_one!(context, input, image, mask_image::single_value::parse);
                if position.is_none() {
                    if let Ok(value) = input.try_parse(|input| Position::parse(context, input)) {
                        position = Some(value);
                        size = input
                            .try_parse(|input| {
                                input.expect_delim('/')?;
                                mask_size::single_value::parse(context, input)
                            })
                            .ok();

                        continue;
                    }
                }
                try_parse_one!(context, input, repeat, mask_repeat::single_value::parse);
                try_parse_one!(context, input, origin, mask_origin::single_value::parse);
                try_parse_one!(context, input, clip, mask_clip::single_value::parse);
                try_parse_one!(
                    context,
                    input,
                    composite,
                    mask_composite::single_value::parse
                );
                try_parse_one!(context, input, mode, mask_mode::single_value::parse);

                parsed -= 1;
                break;
            }
            if clip.is_none() {
                if let Some(origin) = origin {
                    clip = Some(mask_clip::single_value::SpecifiedValue::from(origin));
                }
            }
            if parsed == 0 {
                return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
            }
            if let Some(position) = position {
                mask_position_x.push(position.horizontal);
                mask_position_y.push(position.vertical);
            } else {
                mask_position_x.push(PositionComponent::zero());
                mask_position_y.push(PositionComponent::zero());
            }
            if let Some(m_image) = image {
                mask_image.push(m_image);
            } else {
                mask_image.push(mask_image::single_value::get_initial_specified_value());
            }
            if let Some(m_mode) = mode {
                mask_mode.push(m_mode);
            } else {
                mask_mode.push(mask_mode::single_value::get_initial_specified_value());
            }
            if let Some(m_size) = size {
                mask_size.push(m_size);
            } else {
                mask_size.push(mask_size::single_value::get_initial_specified_value());
            }
            if let Some(m_repeat) = repeat {
                mask_repeat.push(m_repeat);
            } else {
                mask_repeat.push(mask_repeat::single_value::get_initial_specified_value());
            }
            if let Some(m_origin) = origin {
                mask_origin.push(m_origin);
            } else {
                mask_origin.push(mask_origin::single_value::get_initial_specified_value());
            }
            if let Some(m_clip) = clip {
                mask_clip.push(m_clip);
            } else {
                mask_clip.push(mask_clip::single_value::get_initial_specified_value());
            }
            if let Some(m_composite) = composite {
                mask_composite.push(m_composite);
            } else {
                mask_composite.push(mask_composite::single_value::get_initial_specified_value());
            }
            Ok(())
        })?;

        Ok(expanded! {
           mask_image: mask_image::SpecifiedValue(mask_image.into()),
           mask_mode: mask_mode::SpecifiedValue(mask_mode.into()),
           mask_position_x: mask_position_x::SpecifiedValue(mask_position_x.into()),
           mask_position_y: mask_position_y::SpecifiedValue(mask_position_y.into()),
           mask_size: mask_size::SpecifiedValue(mask_size.into()),
           mask_repeat: mask_repeat::SpecifiedValue(mask_repeat.into()),
           mask_origin: mask_origin::SpecifiedValue(mask_origin.into()),
           mask_clip: mask_clip::SpecifiedValue(mask_clip.into()),
           mask_composite: mask_composite::SpecifiedValue(mask_composite.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            use crate::properties::longhands::mask_clip::single_value::computed_value::T as Clip;
            use crate::properties::longhands::mask_origin::single_value::computed_value::T as Origin;
            use style_traits::values::SequenceWriter;

            let len = self.mask_image.0.len();
            if len == 0 {
                return Ok(());
            }
            if self.mask_mode.0.len() != len {
                return Ok(());
            }
            if self.mask_position_x.0.len() != len {
                return Ok(());
            }
            if self.mask_position_y.0.len() != len {
                return Ok(());
            }
            if self.mask_size.0.len() != len {
                return Ok(());
            }
            if self.mask_repeat.0.len() != len {
                return Ok(());
            }
            if self.mask_origin.0.len() != len {
                return Ok(());
            }
            if self.mask_clip.0.len() != len {
                return Ok(());
            }
            if self.mask_composite.0.len() != len {
                return Ok(());
            }

            for i in 0..len {
                if i > 0 {
                    dest.write_str(", ")?;
                }

                let image = &self.mask_image.0[i];
                let mode = &self.mask_mode.0[i];
                let position_x = &self.mask_position_x.0[i];
                let position_y = &self.mask_position_y.0[i];
                let size = &self.mask_size.0[i];
                let repeat = &self.mask_repeat.0[i];
                let origin = &self.mask_origin.0[i];
                let clip = &self.mask_clip.0[i];
                let composite = &self.mask_composite.0[i];

                let mut has_other = false;
                let has_image = *image != mask_image::single_value::get_initial_specified_value();
                has_other |= has_image;
                let has_mode = *mode != mask_mode::single_value::get_initial_specified_value();
                has_other |= has_mode;
                let has_size = *size != mask_size::single_value::get_initial_specified_value();
                has_other |= has_size;
                let has_repeat =
                    *repeat != mask_repeat::single_value::get_initial_specified_value();
                has_other |= has_repeat;
                let has_composite =
                    *composite != mask_composite::single_value::get_initial_specified_value();
                has_other |= has_composite;
                let has_position = *position_x != PositionComponent::zero()
                    || *position_y != PositionComponent::zero();
                let has_origin = *origin != Origin::BorderBox;
                let has_clip = *clip != Clip::BorderBox;

                if !has_other && !has_position && !has_origin && !has_clip {
                    return image.to_css(dest);
                }

                let mut writer = SequenceWriter::new(dest, " ");
                if has_image {
                    writer.item(image)?;
                }
                if has_position || has_size {
                    writer.write_item(|dest| {
                        Position {
                            horizontal: position_x.clone(),
                            vertical: position_y.clone(),
                        }
                        .to_css(dest)?;
                        if has_size {
                            dest.write_str(" / ")?;
                            size.to_css(dest)?;
                        }
                        Ok(())
                    })?;
                }

                if has_repeat {
                    writer.item(repeat)?;
                }

                if has_origin || (has_clip && *clip != Clip::NoClip) {
                    writer.item(origin)?;
                }

                if has_clip && *clip != From::from(*origin) {
                    writer.item(clip)?;
                }

                if has_composite {
                    writer.item(composite)?;
                }

                if has_mode {
                    writer.item(mode)?;
                }
            }

            Ok(())
        }
    }
}

#[cfg(feature = "gecko")]
pub mod mask_position {
    pub use crate::properties::generated::shorthands::mask_position::*;

    use super::*;
    use crate::properties::longhands::{mask_position_x, mask_position_y};
    use crate::values::specified::Position;

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        // Vec grows from 0 to 4 by default on first push().  So allocate with capacity 1, so in
        // the common case of only one item we don't way overallocate, then shrink.  Note that we
        // always push at least one item if parsing succeeds.
        let mut position_x = Vec::with_capacity(1);
        let mut position_y = Vec::with_capacity(1);
        input.parse_comma_separated(|input| {
            let value = Position::parse(context, input)?;
            position_x.push(value.horizontal);
            position_y.push(value.vertical);
            Ok(())
        })?;

        if position_x.is_empty() {
            return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
        }

        Ok(expanded! {
            mask_position_x: mask_position_x::SpecifiedValue(position_x.into()),
            mask_position_y: mask_position_y::SpecifiedValue(position_y.into()),
        })
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            let len = self.mask_position_x.0.len();
            if len == 0 || self.mask_position_y.0.len() != len {
                return Ok(());
            }

            for i in 0..len {
                Position {
                    horizontal: self.mask_position_x.0[i].clone(),
                    vertical: self.mask_position_y.0[i].clone(),
                }
                .to_css(dest)?;

                if i < len - 1 {
                    dest.write_str(", ")?;
                }
            }

            Ok(())
        }
    }
}

pub mod grid_template {
    pub use crate::properties::generated::shorthands::grid_template::*;

    use super::*;
    use crate::parser::Parse;
    use crate::values::generics::grid::{concat_serialize_idents, TrackListValue};
    use crate::values::generics::grid::{TrackList, TrackSize};
    use crate::values::specified::grid::parse_line_names;
    use crate::values::specified::position::{
        GridTemplateAreas, TemplateAreasArc, TemplateAreasParser,
    };
    use crate::values::specified::{GenericGridTemplateComponent, GridTemplateComponent};
    use servo_arc::Arc;

    pub fn parse_grid_template<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<
        (
            GridTemplateComponent,
            GridTemplateComponent,
            GridTemplateAreas,
        ),
        ParseError<'i>,
    > {
        if let Ok(x) = input.try_parse(|i| {
            if i.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
                if !i.is_exhausted() {
                    return Err(());
                }
                return Ok((
                    GenericGridTemplateComponent::None,
                    GenericGridTemplateComponent::None,
                    GridTemplateAreas::None,
                ));
            }
            Err(())
        }) {
            return Ok(x);
        }

        let first_line_names = input.try_parse(parse_line_names).unwrap_or_default();
        let mut areas_parser = TemplateAreasParser::default();
        if areas_parser.try_parse_string(input).is_ok() {
            let mut values = vec![];
            let mut line_names = vec![];
            line_names.push(first_line_names);
            loop {
                let size = input
                    .try_parse(|i| TrackSize::parse(context, i))
                    .unwrap_or_default();
                values.push(TrackListValue::TrackSize(size));
                let mut names = input.try_parse(parse_line_names).unwrap_or_default();
                let more_names = input.try_parse(parse_line_names);

                match areas_parser.try_parse_string(input) {
                    Ok(()) => {
                        if let Ok(v) = more_names {
                            let mut names_vec = names.into_vec();
                            names_vec.extend(v.into_iter());
                            names = names_vec.into();
                        }
                        line_names.push(names);
                    },
                    Err(e) => {
                        if more_names.is_ok() {
                            return Err(e);
                        }
                        line_names.push(names);
                        break;
                    },
                };
            }

            if line_names.len() == values.len() {
                line_names.push(Default::default());
            }

            let template_areas = areas_parser
                .finish()
                .map_err(|()| input.new_custom_error(StyleParseErrorKind::UnspecifiedError))?;
            let template_rows = TrackList {
                values: values.into(),
                line_names: line_names.into(),
                auto_repeat_index: std::usize::MAX,
            };

            let template_cols = if input.try_parse(|i| i.expect_delim('/')).is_ok() {
                let value = GridTemplateComponent::parse_without_none(context, input)?;
                if let GenericGridTemplateComponent::TrackList(ref list) = value {
                    if !list.is_explicit() {
                        return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                    }
                }

                value
            } else {
                GridTemplateComponent::default()
            };

            Ok((
                GenericGridTemplateComponent::TrackList(Box::new(template_rows)),
                template_cols,
                GridTemplateAreas::Areas(TemplateAreasArc(Arc::new(template_areas))),
            ))
        } else {
            let mut template_rows = GridTemplateComponent::parse(context, input)?;
            if let GenericGridTemplateComponent::TrackList(ref mut list) = template_rows {
                if list.line_names[0].is_empty() {
                    list.line_names[0] = first_line_names;
                } else {
                    return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError));
                }
            }

            input.expect_delim('/')?;
            Ok((
                template_rows,
                GridTemplateComponent::parse(context, input)?,
                GridTemplateAreas::None,
            ))
        }
    }

    #[inline]
    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let (rows, columns, areas) = parse_grid_template(context, input)?;
        Ok(expanded! {
            grid_template_rows: rows,
            grid_template_columns: columns,
            grid_template_areas: areas,
        })
    }

    pub fn serialize_grid_template<W>(
        template_rows: &GridTemplateComponent,
        template_columns: &GridTemplateComponent,
        template_areas: &GridTemplateAreas,
        dest: &mut CssWriter<W>,
    ) -> fmt::Result
    where
        W: fmt::Write,
    {
        match *template_areas {
            GridTemplateAreas::None => {
                if template_rows.is_initial() && template_columns.is_initial() {
                    return GridTemplateComponent::default().to_css(dest);
                }
                template_rows.to_css(dest)?;
                dest.write_str(" / ")?;
                template_columns.to_css(dest)
            },
            GridTemplateAreas::Areas(ref areas) => {
                if areas.0.strings.len() != template_rows.track_list_len() {
                    return Ok(());
                }

                let track_list = match *template_rows {
                    GenericGridTemplateComponent::TrackList(ref list) => {
                        if !list.is_explicit() {
                            return Ok(());
                        }
                        list
                    },
                    _ => return Ok(()),
                };

                match *template_columns {
                    GenericGridTemplateComponent::TrackList(ref list) => {
                        if !list.is_explicit() {
                            return Ok(());
                        }
                    },
                    GenericGridTemplateComponent::Subgrid(_) => {
                        return Ok(());
                    },
                    _ => {},
                }

                let mut names_iter = track_list.line_names.iter();
                for (((i, string), names), value) in areas
                    .0
                    .strings
                    .iter()
                    .enumerate()
                    .zip(&mut names_iter)
                    .zip(track_list.values.iter())
                {
                    if i > 0 {
                        dest.write_char(' ')?;
                    }

                    if !names.is_empty() {
                        concat_serialize_idents("[", "] ", names, " ", dest)?;
                    }

                    string.to_css(dest)?;

                    if !value.is_initial() {
                        dest.write_char(' ')?;
                        value.to_css(dest)?;
                    }
                }

                if let Some(names) = names_iter.next() {
                    concat_serialize_idents(" [", "]", names, " ", dest)?;
                }

                if let GenericGridTemplateComponent::TrackList(ref list) = *template_columns {
                    dest.write_str(" / ")?;
                    list.to_css(dest)?;
                }

                Ok(())
            },
        }
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        #[inline]
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            serialize_grid_template(
                self.grid_template_rows,
                self.grid_template_columns,
                self.grid_template_areas,
                dest,
            )
        }
    }
}

pub mod grid {
    pub use crate::properties::generated::shorthands::grid::*;

    use super::*;
    use crate::parser::Parse;
    use crate::properties::longhands::{grid_auto_columns, grid_auto_flow, grid_auto_rows};
    use crate::values::generics::grid::GridTemplateComponent;
    use crate::values::specified::position::{GridAutoFlow, GridTemplateAreas};
    use crate::values::specified::{GenericGridTemplateComponent, ImplicitGridTracks};

    pub fn parse_value<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Longhands, ParseError<'i>> {
        let mut temp_rows = GridTemplateComponent::default();
        let mut temp_cols = GridTemplateComponent::default();
        let mut temp_areas = GridTemplateAreas::None;
        let mut auto_rows = ImplicitGridTracks::default();
        let mut auto_cols = ImplicitGridTracks::default();
        let mut flow = grid_auto_flow::get_initial_value();

        fn parse_auto_flow<'i, 't>(
            input: &mut Parser<'i, 't>,
            is_row: bool,
        ) -> Result<GridAutoFlow, ParseError<'i>> {
            let mut track = None;
            let mut dense = GridAutoFlow::empty();

            for _ in 0..2 {
                if input
                    .try_parse(|i| i.expect_ident_matching("auto-flow"))
                    .is_ok()
                {
                    track = if is_row {
                        Some(GridAutoFlow::ROW)
                    } else {
                        Some(GridAutoFlow::COLUMN)
                    };
                } else if input
                    .try_parse(|i| i.expect_ident_matching("dense"))
                    .is_ok()
                {
                    dense = GridAutoFlow::DENSE
                } else {
                    break;
                }
            }

            if track.is_some() {
                Ok(track.unwrap() | dense)
            } else {
                Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
            }
        }

        if let Ok((rows, cols, areas)) =
            input.try_parse(|i| super::grid_template::parse_grid_template(context, i))
        {
            temp_rows = rows;
            temp_cols = cols;
            temp_areas = areas;
        } else if let Ok(rows) = input.try_parse(|i| GridTemplateComponent::parse(context, i)) {
            temp_rows = rows;
            input.expect_delim('/')?;
            flow = parse_auto_flow(input, false)?;
            auto_cols = input
                .try_parse(|i| grid_auto_columns::parse(context, i))
                .unwrap_or_default();
        } else {
            flow = parse_auto_flow(input, true)?;
            auto_rows = input
                .try_parse(|i| grid_auto_rows::parse(context, i))
                .unwrap_or_default();
            input.expect_delim('/')?;
            temp_cols = GridTemplateComponent::parse(context, input)?;
        }

        Ok(expanded! {
            grid_template_rows: temp_rows,
            grid_template_columns: temp_cols,
            grid_template_areas: temp_areas,
            grid_auto_rows: auto_rows,
            grid_auto_columns: auto_cols,
            grid_auto_flow: flow,
        })
    }

    impl<'a> LonghandsToSerialize<'a> {
        fn is_grid_template(&self) -> bool {
            self.grid_auto_rows.is_initial()
                && self.grid_auto_columns.is_initial()
                && *self.grid_auto_flow == grid_auto_flow::get_initial_value()
        }
    }

    impl<'a> ToCss for LonghandsToSerialize<'a> {
        fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
        where
            W: fmt::Write,
        {
            if self.is_grid_template() {
                return super::grid_template::serialize_grid_template(
                    self.grid_template_rows,
                    self.grid_template_columns,
                    self.grid_template_areas,
                    dest,
                );
            }

            if *self.grid_template_areas != GridTemplateAreas::None {
                return Ok(());
            }

            if self.grid_auto_flow.contains(GridAutoFlow::COLUMN) {
                if !self.grid_auto_rows.is_initial() || !self.grid_template_columns.is_initial() {
                    return Ok(());
                }

                if let GenericGridTemplateComponent::TrackList(ref list) = *self.grid_template_rows
                {
                    if !list.is_explicit() {
                        return Ok(());
                    }
                }

                self.grid_template_rows.to_css(dest)?;
                dest.write_str(" / auto-flow")?;
                if self.grid_auto_flow.contains(GridAutoFlow::DENSE) {
                    dest.write_str(" dense")?;
                }

                if !self.grid_auto_columns.is_initial() {
                    dest.write_char(' ')?;
                    self.grid_auto_columns.to_css(dest)?;
                }

                return Ok(());
            }

            if !self.grid_auto_columns.is_initial() || !self.grid_template_rows.is_initial() {
                return Ok(());
            }

            if let GenericGridTemplateComponent::TrackList(ref list) = *self.grid_template_columns {
                if !list.is_explicit() {
                    return Ok(());
                }
            }

            dest.write_str("auto-flow")?;
            if self.grid_auto_flow.contains(GridAutoFlow::DENSE) {
                dest.write_str(" dense")?;
            }

            if !self.grid_auto_rows.is_initial() {
                dest.write_char(' ')?;
                self.grid_auto_rows.to_css(dest)?;
            }

            dest.write_str(" / ")?;
            self.grid_template_columns.to_css(dest)?;
            Ok(())
        }
    }
}
