/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Specified types for properties related to animations and transitions.

use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::properties::{NonCustomPropertyId, PropertyId, ShorthandId};
use crate::values::generics::animation as generics;
use crate::values::generics::position::{IsTreeScoped, TreeScoped};
use crate::values::specified::{LengthPercentage, NonNegativeNumber, Time};
use crate::values::{AtomIdent, CustomIdent, DashedIdent, KeyframesName};
use crate::Atom;
use cssparser::{match_ignore_ascii_case, Parser};
use std::fmt::{self, Write};
use style_traits::{
    CssWriter, KeywordsCollectFn, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss,
    ToTyped,
};

/// A given transition property, that is either `All`, a longhand or shorthand
/// property, or an unsupported or custom property.
#[derive(
    Clone, Debug, Eq, Hash, MallocSizeOf, PartialEq, ToComputedValue, ToResolvedValue, ToShmem,
)]
#[repr(u8)]
pub enum TransitionProperty {
    /// A non-custom property.
    NonCustom(NonCustomPropertyId),
    /// A custom property.
    Custom(Atom),
    /// Unrecognized property which could be any non-transitionable, custom property, or
    /// unknown property.
    Unsupported(CustomIdent),
}

impl ToCss for TransitionProperty {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        match *self {
            TransitionProperty::NonCustom(ref id) => id.to_css(dest),
            TransitionProperty::Custom(ref name) => {
                dest.write_str("--")?;
                crate::values::serialize_atom_name(name, dest)
            },
            TransitionProperty::Unsupported(ref i) => i.to_css(dest),
        }
    }
}

impl ToTyped for TransitionProperty {}

impl Parse for TransitionProperty {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let ident = input.expect_ident()?;

        let id = match PropertyId::parse_ignoring_rule_type(&ident, context) {
            Ok(id) => id,
            Err(..) => {
                // None is not acceptable as a single transition-property.
                return Ok(TransitionProperty::Unsupported(CustomIdent::from_ident(
                    location,
                    ident,
                    &["none"],
                )?));
            },
        };

        Ok(match id {
            PropertyId::NonCustom(id) => TransitionProperty::NonCustom(id.unaliased()),
            PropertyId::Custom(name) => TransitionProperty::Custom(name),
        })
    }
}

impl SpecifiedValueInfo for TransitionProperty {
    fn collect_completion_keywords(f: KeywordsCollectFn) {
        // `transition-property` can actually accept all properties and
        // arbitrary identifiers, but `all` is a special one we'd like
        // to list.
        f(&["all"]);
    }
}

impl TransitionProperty {
    /// Returns the `none` value.
    #[inline]
    pub fn none() -> Self {
        TransitionProperty::Unsupported(CustomIdent(atom!("none")))
    }

    /// Returns whether we're the `none` value.
    #[inline]
    pub fn is_none(&self) -> bool {
        matches!(*self, TransitionProperty::Unsupported(ref ident) if ident.0 == atom!("none"))
    }

    /// Returns `all`.
    #[inline]
    pub fn all() -> Self {
        TransitionProperty::NonCustom(NonCustomPropertyId::from_shorthand(ShorthandId::All))
    }

    /// Returns true if it is `all`.
    #[inline]
    pub fn is_all(&self) -> bool {
        self == &TransitionProperty::NonCustom(NonCustomPropertyId::from_shorthand(
            ShorthandId::All,
        ))
    }
}

/// A specified value for <transition-behavior-value>.
///
/// https://drafts.csswg.org/css-transitions-2/#transition-behavior-property
#[derive(
    Clone,
    Copy,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum TransitionBehavior {
    /// Transitions will not be started for discrete properties, only for interpolable properties.
    Normal,
    /// Transitions will be started for discrete properties as well as interpolable properties.
    AllowDiscrete,
}

impl TransitionBehavior {
    /// Return normal, the initial value.
    #[inline]
    pub fn normal() -> Self {
        Self::Normal
    }

    /// Return true if it is normal.
    #[inline]
    pub fn is_normal(&self) -> bool {
        matches!(*self, Self::Normal)
    }
}

/// A specified value for the `animation-duration` property.
pub type AnimationDuration = generics::GenericAnimationDuration<Time>;

impl Parse for AnimationDuration {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if static_prefs::pref!("layout.css.scroll-driven-animations.enabled")
            && input.try_parse(|i| i.expect_ident_matching("auto")).is_ok()
        {
            return Ok(Self::auto());
        }

        Time::parse_non_negative(context, input).map(AnimationDuration::Time)
    }
}

/// https://drafts.csswg.org/css-animations/#animation-iteration-count
#[derive(
    Clone, Debug, MallocSizeOf, PartialEq, Parse, SpecifiedValueInfo, ToCss, ToShmem, ToTyped,
)]
pub enum AnimationIterationCount {
    /// A `<number>` value.
    Number(NonNegativeNumber),
    /// The `infinite` keyword.
    Infinite,
}

impl AnimationIterationCount {
    /// Returns the value `1.0`.
    #[inline]
    pub fn one() -> Self {
        Self::Number(NonNegativeNumber::new(1.0))
    }

    /// Returns true if it's `1.0`.
    #[inline]
    pub fn is_one(&self) -> bool {
        *self == Self::one()
    }
}

/// A value for the `animation-name` property.
#[derive(
    Clone,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[value_info(other_values = "none")]
#[repr(C)]
pub struct AnimationName(pub KeyframesName);

impl AnimationName {
    /// Get the name of the animation as an `Atom`.
    pub fn as_atom(&self) -> Option<&Atom> {
        if self.is_none() {
            return None;
        }
        Some(self.0.as_atom())
    }

    /// Returns the `none` value.
    pub fn none() -> Self {
        AnimationName(KeyframesName::none())
    }

    /// Returns whether this is the none value.
    pub fn is_none(&self) -> bool {
        self.0.is_none()
    }
}

impl Parse for AnimationName {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if let Ok(name) = input.try_parse(|input| KeyframesName::parse(context, input)) {
            return Ok(AnimationName(name));
        }

        input.expect_ident_matching("none")?;
        Ok(AnimationName(KeyframesName::none()))
    }
}

/// https://drafts.csswg.org/css-animations/#propdef-animation-direction
#[derive(
    Copy,
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum AnimationDirection {
    Normal,
    Reverse,
    Alternate,
    AlternateReverse,
}

impl AnimationDirection {
    /// Returns true if the name matches any animation-direction keyword.
    #[inline]
    pub fn match_keywords(name: &AnimationName) -> bool {
        if let Some(name) = name.as_atom() {
            #[cfg(feature = "gecko")]
            return name.with_str(|n| Self::from_ident(n).is_ok());
            #[cfg(feature = "servo")]
            return Self::from_ident(name).is_ok();
        }
        false
    }
}

/// https://drafts.csswg.org/css-animations/#animation-play-state
#[derive(
    Copy,
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum AnimationPlayState {
    Running,
    Paused,
}

impl AnimationPlayState {
    /// Returns true if the name matches any animation-play-state keyword.
    #[inline]
    pub fn match_keywords(name: &AnimationName) -> bool {
        if let Some(name) = name.as_atom() {
            #[cfg(feature = "gecko")]
            return name.with_str(|n| Self::from_ident(n).is_ok());
            #[cfg(feature = "servo")]
            return Self::from_ident(name).is_ok();
        }
        false
    }
}

/// https://drafts.csswg.org/css-animations/#propdef-animation-fill-mode
#[derive(
    Copy,
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum AnimationFillMode {
    None,
    Forwards,
    Backwards,
    Both,
}

impl AnimationFillMode {
    /// Returns true if the name matches any animation-fill-mode keyword.
    /// Note: animation-name:none is its initial value, so we don't have to match none here.
    #[inline]
    pub fn match_keywords(name: &AnimationName) -> bool {
        if let Some(name) = name.as_atom() {
            #[cfg(feature = "gecko")]
            return name.with_str(|n| Self::from_ident(n).is_ok());
            #[cfg(feature = "servo")]
            return Self::from_ident(name).is_ok();
        }
        false
    }
}

/// https://drafts.csswg.org/css-animations-2/#animation-composition
#[derive(
    Copy,
    Clone,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[allow(missing_docs)]
pub enum AnimationComposition {
    Replace,
    Add,
    Accumulate,
}

/// A value for the <Scroller> used in scroll().
///
/// https://drafts.csswg.org/scroll-animations-1/rewrite#typedef-scroller
#[derive(
    Copy,
    Clone,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[repr(u8)]
pub enum Scroller {
    /// The nearest ancestor scroll container. (Default.)
    Nearest,
    /// The document viewport as the scroll container.
    Root,
    /// Specifies to use the element’s own principal box as the scroll container.
    #[css(keyword = "self")]
    SelfElement,
}

impl Scroller {
    /// Returns true if it is default.
    #[inline]
    fn is_default(&self) -> bool {
        matches!(*self, Self::Nearest)
    }
}

impl Default for Scroller {
    fn default() -> Self {
        Self::Nearest
    }
}

/// A value for the <Axis> used in scroll(), or a value for {scroll|view}-timeline-axis.
///
/// https://drafts.csswg.org/scroll-animations-1/#typedef-axis
/// https://drafts.csswg.org/scroll-animations-1/#scroll-timeline-axis
/// https://drafts.csswg.org/scroll-animations-1/#view-timeline-axis
#[derive(
    Copy,
    Clone,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum ScrollAxis {
    /// The block axis of the scroll container. (Default.)
    Block = 0,
    /// The inline axis of the scroll container.
    Inline = 1,
    /// The horizontal axis of the scroll container.
    X = 2,
    /// The vertical axis of the scroll container.
    Y = 3,
}

impl ScrollAxis {
    /// Returns true if it is default.
    #[inline]
    pub fn is_default(&self) -> bool {
        matches!(*self, Self::Block)
    }
}

impl Default for ScrollAxis {
    fn default() -> Self {
        Self::Block
    }
}

/// The scroll() notation.
/// https://drafts.csswg.org/scroll-animations-1/#scroll-notation
#[derive(
    Copy,
    Clone,
    Debug,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
)]
#[css(function = "scroll")]
#[repr(C)]
pub struct ScrollFunction {
    /// The scroll container element whose scroll position drives the progress of the timeline.
    #[css(skip_if = "Scroller::is_default")]
    pub scroller: Scroller,
    /// The axis of scrolling that drives the progress of the timeline.
    #[css(skip_if = "ScrollAxis::is_default")]
    pub axis: ScrollAxis,
}

impl ScrollFunction {
    /// Parse the inner function arguments of `scroll()`.
    fn parse_arguments<'i, 't>(input: &mut Parser<'i, 't>) -> Result<Self, ParseError<'i>> {
        // <scroll()> = scroll( [ <scroller> || <axis> ]? )
        // https://drafts.csswg.org/scroll-animations-1/#funcdef-scroll
        let mut scroller = None;
        let mut axis = None;
        loop {
            if scroller.is_none() {
                scroller = input.try_parse(Scroller::parse).ok();
            }

            if axis.is_none() {
                axis = input.try_parse(ScrollAxis::parse).ok();
                if axis.is_some() {
                    continue;
                }
            }
            break;
        }

        Ok(Self {
            scroller: scroller.unwrap_or_default(),
            axis: axis.unwrap_or_default(),
        })
    }
}

impl generics::ViewFunction<LengthPercentage> {
    /// Parse the inner function arguments of `view()`.
    fn parse_arguments<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        // <view()> = view( [ <axis> || <'view-timeline-inset'> ]? )
        // https://drafts.csswg.org/scroll-animations-1/#funcdef-view
        let mut axis = None;
        let mut inset = None;
        loop {
            if axis.is_none() {
                axis = input.try_parse(ScrollAxis::parse).ok();
            }

            if inset.is_none() {
                inset = input
                    .try_parse(|i| ViewTimelineInset::parse(context, i))
                    .ok();
                if inset.is_some() {
                    continue;
                }
            }
            break;
        }

        Ok(Self {
            inset: inset.unwrap_or_default(),
            axis: axis.unwrap_or_default(),
        })
    }
}

/// The typedef of scroll-timeline-name or view-timeline-name.
///
/// https://drafts.csswg.org/scroll-animations-1/#scroll-timeline-name
/// https://drafts.csswg.org/scroll-animations-1/#view-timeline-name
pub type TimelineName = TreeScoped<TimelineIdent>;

impl TimelineName {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self::with_default_level(TimelineIdent::none())
    }
}

/// The identifier for a timeline name.
#[derive(
    Clone,
    Debug,
    Eq,
    Hash,
    MallocSizeOf,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
)]
#[repr(C)]
pub struct TimelineIdent(DashedIdent);

impl TimelineIdent {
    /// Returns the `none` value.
    pub fn none() -> Self {
        Self(DashedIdent::empty())
    }

    /// Check if this is `none` value.
    pub fn is_none(&self) -> bool {
        self.0.is_empty()
    }
}

impl IsTreeScoped for TimelineIdent {
    fn is_tree_scoped(&self) -> bool {
        !self.is_none()
    }
}

impl Parse for TimelineIdent {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
            return Ok(Self::none());
        }

        DashedIdent::parse(context, input).map(TimelineIdent)
    }
}

impl ToCss for TimelineIdent {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        if self.is_none() {
            return dest.write_str("none");
        }

        self.0.to_css(dest)
    }
}

impl ToTyped for TimelineName {}

/// A specified value for the `animation-timeline` property.
pub type AnimationTimeline = generics::GenericAnimationTimeline<LengthPercentage>;

impl Parse for AnimationTimeline {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use crate::values::generics::animation::ViewFunction;

        // <single-animation-timeline> = auto | none | <dashed-ident> | <scroll()> | <view()>
        // https://drafts.csswg.org/css-animations-2/#typedef-single-animation-timeline

        if input.try_parse(|i| i.expect_ident_matching("auto")).is_ok() {
            return Ok(Self::Auto);
        }

        // This parses none or <dashed-indent>.
        if let Ok(name) = input.try_parse(|i| TimelineName::parse(context, i)) {
            return Ok(AnimationTimeline::Timeline(name));
        }

        // Parse <scroll()> or <view()>.
        let location = input.current_source_location();
        let function = input.expect_function()?.clone();
        input.parse_nested_block(move |i| {
            match_ignore_ascii_case! { &function,
                "scroll" => ScrollFunction::parse_arguments(i).map(Self::Scroll),
                "view" => ViewFunction::parse_arguments(context, i).map(Self::View),
                _ => {
                    Err(location.new_custom_error(
                        StyleParseErrorKind::UnexpectedFunction(function.clone())
                    ))
                },
            }
        })
    }
}

/// A specified value for the `view-timeline-inset` property.
pub type ViewTimelineInset = generics::GenericViewTimelineInset<LengthPercentage>;

impl Parse for ViewTimelineInset {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use crate::values::specified::LengthPercentageOrAuto;

        let start = LengthPercentageOrAuto::parse(context, input)?;
        let end = match input.try_parse(|input| LengthPercentageOrAuto::parse(context, input)) {
            Ok(end) => end,
            Err(_) => start.clone(),
        };

        Ok(Self { start, end })
    }
}

/// The view-transition-name: `none | <custom-ident> | match-element`.
///
/// https://drafts.csswg.org/css-view-transitions-1/#view-transition-name-prop
/// https://drafts.csswg.org/css-view-transitions-2/#auto-vt-name
// TODO: auto keyword.
#[derive(
    Clone,
    Debug,
    Eq,
    Hash,
    PartialEq,
    MallocSizeOf,
    SpecifiedValueInfo,
    ToCss,
    ToComputedValue,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(transparent)]
#[typed(todo_derive_fields)]
#[value_info(other_values = "none, match-element")]
pub struct ViewTransitionNameKeyword(AtomIdent);

impl ViewTransitionNameKeyword {
    /// Returns the `none` value.
    pub fn none() -> Self {
        Self(AtomIdent::new(atom!("none")))
    }
}

impl IsTreeScoped for ViewTransitionNameKeyword {
    fn is_tree_scoped(&self) -> bool {
        self.0 .0 != atom!("none")
    }
}

impl Parse for ViewTransitionNameKeyword {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        let location = input.current_source_location();
        let ident = input.expect_ident()?;
        if ident.eq_ignore_ascii_case("none") {
            return Ok(Self::none());
        }

        if ident.eq_ignore_ascii_case("match-element") {
            return Ok(Self(AtomIdent::new(atom!("match-element"))));
        }

        // We check none already, so don't need to exclude none here.
        // Note: "auto" is not supported yet so we exclude it.
        CustomIdent::from_ident(location, ident, &["auto"]).map(|i| Self(AtomIdent::new(i.0)))
    }
}

/// https://drafts.csswg.org/css-view-transitions-1/#view-transition-name-prop
pub type ViewTransitionName = TreeScoped<ViewTransitionNameKeyword>;

impl ViewTransitionName {
    /// Return the `none` value.
    pub fn none() -> Self {
        Self::with_default_level(ViewTransitionNameKeyword::none())
    }
}

/// The view-transition-class: `none | <custom-ident>+`.
///
/// https://drafts.csswg.org/css-view-transitions-2/#view-transition-class-prop
///
/// Empty slice represents `none`.
#[derive(
    Clone,
    Debug,
    Default,
    Eq,
    Hash,
    PartialEq,
    MallocSizeOf,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(C)]
#[value_info(other_values = "none")]
pub struct ViewTransitionClassList(
    #[css(iterable, if_empty = "none")]
    #[ignore_malloc_size_of = "Arc"]
    crate::ArcSlice<CustomIdent>,
);

impl IsTreeScoped for ViewTransitionClassList {
    fn is_tree_scoped(&self) -> bool {
        !self.is_none()
    }
}

impl ViewTransitionClassList {
    /// Returns the default value, `none`. We use the default slice (i.e. empty) to represent it.
    pub fn none() -> Self {
        Self(Default::default())
    }

    /// Returns whether this is the `none` value.
    pub fn is_none(&self) -> bool {
        self.0.is_empty()
    }

    /// Iterates over the contained custom idents.
    pub fn iter(&self) -> impl Iterator<Item = &CustomIdent> {
        self.0.iter()
    }
}

impl Parse for ViewTransitionClassList {
    fn parse<'i, 't>(
        _: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        use style_traits::{Separator, Space};

        if input.try_parse(|i| i.expect_ident_matching("none")).is_ok() {
            return Ok(Self::none());
        }

        Ok(Self(crate::ArcSlice::from_iter(
            Space::parse(input, |i| CustomIdent::parse(i, &["none"]))?.into_iter(),
        )))
    }
}

/// https://drafts.csswg.org/css-view-transitions-2/#view-transition-class-prop
pub type ViewTransitionClass = TreeScoped<ViewTransitionClassList>;

impl ViewTransitionClass {
    /// Returns the default value, `none`.
    pub fn none() -> Self {
        Self::with_default_level(ViewTransitionClassList::none())
    }
}

/// The <timeline-range-name> value type, which indicates a CSS identifier representing one of the
/// predefined named timeline ranges.
/// https://drafts.csswg.org/scroll-animations-1/#named-ranges
///
/// For now, only view timeline ranges use this type.
/// https://drafts.csswg.org/scroll-animations-1/#view-timelines-ranges
#[derive(
    Copy,
    Clone,
    Debug,
    Eq,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
pub enum TimelineRangeName {
    /// The default normal value.
    #[css(skip)]
    Normal,
    /// No timeline range name specified.
    #[css(skip)]
    None,
    /// Represents the full range of the view progress timeline
    Cover,
    /// Represents the range during which the principal box is either fully contained by, or fully
    /// covers, its view progress visibility range within the scrollport.
    Contain,
    /// Represents the range during which the principal box is entering the view progress
    /// visibility range.
    Entry,
    /// Represents the range during which the principal box is exiting the view progress visibility
    /// range.
    Exit,
    /// Represents the range during which the principal box crosses the end border edge.
    EntryCrossing,
    /// Represents the range during which the principal box crosses the start border edge.
    ExitCrossing,
    /// Represents the full range of the scroll container on which the view progress timeline is
    /// defined.
    Scroll,
}

impl TimelineRangeName {
    /// Returns true if it is `normal`.
    #[inline]
    pub fn is_normal(&self) -> bool {
        matches!(*self, Self::Normal)
    }

    /// Returns true if it is `none`.
    #[inline]
    pub fn is_none(&self) -> bool {
        matches!(*self, Self::None)
    }
}

/// The internal value for `animation-range-start` and `animation-range-end`.
pub type AnimationRangeValue = generics::GenericAnimationRangeValue<LengthPercentage>;

fn parse_animation_range<'i, 't>(
    context: &ParserContext,
    input: &mut Parser<'i, 't>,
    default: LengthPercentage,
) -> Result<AnimationRangeValue, ParseError<'i>> {
    if input
        .try_parse(|i| i.expect_ident_matching("normal"))
        .is_ok()
    {
        return Ok(AnimationRangeValue::normal(default));
    }

    if let Ok(lp) = input.try_parse(|i| LengthPercentage::parse(context, i)) {
        return Ok(AnimationRangeValue::length_percentage(lp));
    }

    let name = TimelineRangeName::parse(input)?;
    let lp = input
        .try_parse(|i| LengthPercentage::parse(context, i))
        .unwrap_or(default);
    Ok(AnimationRangeValue::new(name, lp))
}

/// A specified value for the `animation-range-start`.
pub type AnimationRangeStart = generics::GenericAnimationRangeStart<LengthPercentage>;

impl Parse for AnimationRangeStart {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_animation_range(context, input, LengthPercentage::zero_percent()).map(Self)
    }
}

/// A specified value for the `animation-range-end`.
pub type AnimationRangeEnd = generics::GenericAnimationRangeEnd<LengthPercentage>;

impl Parse for AnimationRangeEnd {
    fn parse<'i, 't>(
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<Self, ParseError<'i>> {
        parse_animation_range(context, input, LengthPercentage::hundred_percent()).map(Self)
    }
}
