/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Servo's media feature list and evaluator.

use crate::derives::*;
use crate::queries::feature::{AllowsRanges, Evaluator, FeatureFlags, QueryFeatureDescription};
use crate::queries::values::{Orientation, PrefersColorScheme};
use crate::values::computed::{CSSPixelLength, Context, Ratio, Resolution};
use std::fmt::Debug;

/// https://drafts.csswg.org/mediaqueries-4/#width
fn eval_width(context: &Context) -> CSSPixelLength {
    CSSPixelLength::new(context.device().au_viewport_size().width.to_f32_px())
}

/// https://drafts.csswg.org/mediaqueries-4/#height
fn eval_height(context: &Context) -> CSSPixelLength {
    CSSPixelLength::new(context.device().au_viewport_size().height.to_f32_px())
}

/// https://drafts.csswg.org/mediaqueries-4/#device-width
fn eval_device_width(context: &Context) -> CSSPixelLength {
    let device = context.device();
    let scaled = device.device_size() / device.device_pixel_ratio();
    CSSPixelLength::new(scaled.width)
}

/// https://drafts.csswg.org/mediaqueries-4/#device-height
fn eval_device_height(context: &Context) -> CSSPixelLength {
    let device = context.device();
    let scaled = device.device_size() / device.device_pixel_ratio();
    CSSPixelLength::new(scaled.height)
}

/// https://drafts.csswg.org/mediaqueries-4/#orientation
fn eval_orientation(context: &Context, value: Option<Orientation>) -> bool {
    Orientation::eval(context.device().au_viewport_size(), value)
}

#[derive(Clone, Copy, Debug, FromPrimitive, Parse, ToCss)]
#[repr(u8)]
enum Scan {
    Progressive,
    Interlace,
}

/// https://drafts.csswg.org/mediaqueries-4/#scan
fn eval_scan(_: &Context, _: Option<Scan>) -> bool {
    // Since we doesn't support the 'tv' media type, the 'scan' feature never
    // matches.
    false
}

/// https://drafts.csswg.org/mediaqueries-4/#resolution
fn eval_resolution(context: &Context) -> Resolution {
    Resolution::from_dppx(context.device().device_pixel_ratio().0)
}

/// https://compat.spec.whatwg.org/#css-media-queries-webkit-device-pixel-ratio
fn eval_device_pixel_ratio(context: &Context) -> f32 {
    eval_resolution(context).dppx()
}

fn eval_prefers_color_scheme(context: &Context, query_value: Option<PrefersColorScheme>) -> bool {
    match query_value {
        Some(v) => context.device().color_scheme() == v,
        None => true,
    }
}

bitflags! {
    /// https://drafts.csswg.org/mediaqueries-4/#mf-interaction
    #[derive(Debug, Clone, Copy, PartialEq)]
    pub struct PointerCapabilities: u8 {
        /// The input mechanism includes a pointing device of limited accuracy, such as a finger on a touchscreen.
        const COARSE = 0b001;
        /// The input mechanism includes an accurate pointing device, such as a mouse.
        const FINE = 0b010;
        /// The input mechanism can conveniently hover over elements.
        const HOVER = 0b100;
    }
}

impl Default for PointerCapabilities {
    #[cfg(any(target_os = "ios", target_os = "android", target_env = "ohos"))]
    fn default() -> Self {
        PointerCapabilities::COARSE
    }
    #[cfg(not(any(target_os = "ios", target_os = "android", target_env = "ohos")))]
    fn default() -> Self {
        PointerCapabilities::FINE | PointerCapabilities::HOVER
    }
}

#[derive(Clone, Copy, Debug, FromPrimitive, Parse, ToCss)]
#[repr(u8)]
enum Pointer {
    None,
    Coarse,
    Fine,
}

fn eval_pointer_capabilities(
    query_value: Option<Pointer>,
    pointer_capabilities: PointerCapabilities,
) -> bool {
    match query_value {
        None => !pointer_capabilities.is_empty(),
        Some(Pointer::None) => pointer_capabilities.is_empty(),
        Some(Pointer::Coarse) => pointer_capabilities.intersects(PointerCapabilities::COARSE),
        Some(Pointer::Fine) => pointer_capabilities.intersects(PointerCapabilities::FINE),
    }
}

/// https://drafts.csswg.org/mediaqueries-4/#pointer
fn eval_pointer(context: &Context, query_value: Option<Pointer>) -> bool {
    eval_pointer_capabilities(query_value, context.device().primary_pointer_capabilities())
}

/// https://drafts.csswg.org/mediaqueries-4/#descdef-media-any-pointer
fn eval_any_pointer(context: &Context, query_value: Option<Pointer>) -> bool {
    eval_pointer_capabilities(query_value, context.device().all_pointer_capabilities())
}

#[derive(Clone, Copy, Debug, FromPrimitive, Parse, ToCss)]
#[repr(u8)]
enum Hover {
    None,
    Hover,
}

fn eval_hover_capabilities(
    query_value: Option<Hover>,
    pointer_capabilities: PointerCapabilities,
) -> bool {
    let can_hover = pointer_capabilities.intersects(PointerCapabilities::HOVER);
    match query_value {
        Some(Hover::None) => !can_hover,
        Some(Hover::Hover) => can_hover,
        None => return can_hover,
    }
}

/// https://drafts.csswg.org/mediaqueries-4/#hover
fn eval_hover(context: &Context, query_value: Option<Hover>) -> bool {
    eval_hover_capabilities(query_value, context.device().primary_pointer_capabilities())
}

/// https://drafts.csswg.org/mediaqueries-4/#descdef-media-any-hover
fn eval_any_hover(context: &Context, query_value: Option<Hover>) -> bool {
    eval_hover_capabilities(query_value, context.device().all_pointer_capabilities())
}

/// <https://drafts.csswg.org/mediaqueries-4/#aspect-ratio>
fn eval_aspect_ratio(context: &Context) -> Ratio {
    let size = context.device().au_viewport_size();
    Ratio::new(size.width.0 as f32, size.height.0 as f32)
}

/// A list with all the media features that Servo supports.
pub static MEDIA_FEATURES: [QueryFeatureDescription; 15] = [
    feature!(
        atom!("width"),
        AllowsRanges::Yes,
        Evaluator::Length(eval_width),
        FeatureFlags::VIEWPORT_DEPENDENT,
    ),
    feature!(
        atom!("height"),
        AllowsRanges::Yes,
        Evaluator::Length(eval_height),
        FeatureFlags::VIEWPORT_DEPENDENT,
    ),
    feature!(
        atom!("orientation"),
        AllowsRanges::No,
        keyword_evaluator!(eval_orientation, Orientation),
        FeatureFlags::VIEWPORT_DEPENDENT,
    ),
    feature!(
        atom!("pointer"),
        AllowsRanges::No,
        keyword_evaluator!(eval_pointer, Pointer),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("any-pointer"),
        AllowsRanges::No,
        keyword_evaluator!(eval_any_pointer, Pointer),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("hover"),
        AllowsRanges::No,
        keyword_evaluator!(eval_hover, Hover),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("any-hover"),
        AllowsRanges::No,
        keyword_evaluator!(eval_any_hover, Hover),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("aspect-ratio"),
        AllowsRanges::Yes,
        Evaluator::NumberRatio(eval_aspect_ratio),
        FeatureFlags::VIEWPORT_DEPENDENT,
    ),
    feature!(
        atom!("device-width"),
        AllowsRanges::Yes,
        Evaluator::Length(eval_device_width),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("device-height"),
        AllowsRanges::Yes,
        Evaluator::Length(eval_device_height),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("scan"),
        AllowsRanges::No,
        keyword_evaluator!(eval_scan, Scan),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("resolution"),
        AllowsRanges::Yes,
        Evaluator::Resolution(eval_resolution),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("device-pixel-ratio"),
        AllowsRanges::Yes,
        Evaluator::Float(eval_device_pixel_ratio),
        FeatureFlags::WEBKIT_PREFIX,
    ),
    feature!(
        atom!("-moz-device-pixel-ratio"),
        AllowsRanges::Yes,
        Evaluator::Float(eval_device_pixel_ratio),
        FeatureFlags::empty(),
    ),
    feature!(
        atom!("prefers-color-scheme"),
        AllowsRanges::No,
        keyword_evaluator!(eval_prefers_color_scheme, PrefersColorScheme),
        FeatureFlags::empty(),
    ),
];
