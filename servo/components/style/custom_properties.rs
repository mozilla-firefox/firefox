/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Support for [custom properties for cascading variables][custom].
//!
//! [custom]: https://drafts.csswg.org/css-variables/

use crate::custom_properties_map::{CustomPropertiesMap, OwnMap};
use crate::device::Device;
use crate::dom::AttributeTracker;
use crate::properties::{CSSWideKeyword, PrioritaryPropertyId};
use crate::properties_and_values::{
    rule::Descriptors as PropertyDescriptors,
    syntax::Descriptor as SyntaxDescriptor,
    value::{
        AllowComputationallyDependent, ComputedValue as ComputedRegisteredValue,
        SpecifiedValue as SpecifiedRegisteredValue,
    },
};
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::stylesheets::UrlExtraData;
use crate::stylist::Stylist;
use crate::typed_om::{
    ToTyped, TypedValue, UnparsedSegment, UnparsedValue, VariableReferenceValue,
};
use crate::values::computed;
use crate::values::generics::calc::SortKey as AttrUnit;
use crate::values::specified::{NoCalcLength, ParsedNamespace};
use crate::{derives::*, Atom, LocalName, Namespace, Prefix};
use cssparser::{
    CowRcStr, Delimiter, Parser, ParserInput, SourcePosition, Token, TokenSerializationType,
};
use rustc_hash::FxHashMap;
use selectors::parser::SelectorParseErrorKind;
use servo_arc::Arc;
use smallvec::SmallVec;
use std::borrow::Cow;
use std::fmt::{self, Write};
use std::num;
use std::ops::{Index, IndexMut};
use style_traits::{CssString, CssWriter, ParseError, StyleParseErrorKind, ToCss};
use thin_vec::ThinVec;

/// The environment from which to get `env` function values.
///
/// TODO(emilio): If this becomes a bit more complex we should probably move it
/// to the `media_queries` module, or something.
#[derive(Debug, MallocSizeOf)]
pub struct CssEnvironment;

type EnvironmentEvaluator = fn(device: &Device, url_data: &UrlExtraData) -> VariableValue;

struct EnvironmentVariable {
    name: Atom,
    evaluator: EnvironmentEvaluator,
}

macro_rules! make_variable {
    ($name:expr, $evaluator:expr) => {{
        EnvironmentVariable {
            name: $name,
            evaluator: $evaluator,
        }
    }};
}

fn get_safearea_inset_top(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(device.safe_area_insets().top, url_data)
}

fn get_safearea_inset_bottom(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(device.safe_area_insets().bottom, url_data)
}

fn get_safearea_inset_left(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(device.safe_area_insets().left, url_data)
}

fn get_safearea_inset_right(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(device.safe_area_insets().right, url_data)
}

#[cfg(feature = "gecko")]
fn get_content_preferred_color_scheme(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    use crate::queries::values::PrefersColorScheme;
    let prefers_color_scheme = unsafe {
        crate::gecko_bindings::bindings::Gecko_MediaFeatures_PrefersColorScheme(
            device.document(),
            /* use_content = */ true,
        )
    };
    VariableValue::ident(
        match prefers_color_scheme {
            PrefersColorScheme::Light => "light",
            PrefersColorScheme::Dark => "dark",
        },
        url_data,
    )
}

#[cfg(feature = "servo")]
fn get_content_preferred_color_scheme(_device: &Device, url_data: &UrlExtraData) -> VariableValue {
    // TODO: Add an implementation for Servo.
    VariableValue::ident("light", url_data)
}

fn get_scrollbar_inline_size(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(device.scrollbar_inline_size().px(), url_data)
}

fn get_hairline(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::pixels(
        app_units::Au(device.app_units_per_device_pixel()).to_f32_px(),
        url_data,
    )
}

static ENVIRONMENT_VARIABLES: [EnvironmentVariable; 4] = [
    make_variable!(atom!("safe-area-inset-top"), get_safearea_inset_top),
    make_variable!(atom!("safe-area-inset-bottom"), get_safearea_inset_bottom),
    make_variable!(atom!("safe-area-inset-left"), get_safearea_inset_left),
    make_variable!(atom!("safe-area-inset-right"), get_safearea_inset_right),
];

#[cfg(feature = "gecko")]
macro_rules! lnf_int {
    ($id:ident) => {
        unsafe {
            crate::gecko_bindings::bindings::Gecko_GetLookAndFeelInt(
                crate::gecko_bindings::bindings::LookAndFeel_IntID::$id as i32,
            )
        }
    };
}

#[cfg(feature = "servo")]
macro_rules! lnf_int {
    ($id:ident) => {
        // TODO: Add an implementation for Servo.
        0
    };
}

macro_rules! lnf_int_variable {
    ($atom:expr, $id:ident, $ctor:ident) => {{
        fn __eval(_: &Device, url_data: &UrlExtraData) -> VariableValue {
            VariableValue::$ctor(lnf_int!($id), url_data)
        }
        make_variable!($atom, __eval)
    }};
}

fn eval_gtk_csd_titlebar_radius(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    let int_pixels = lnf_int!(TitlebarRadius);
    let unzoomed_scale =
        device.device_pixel_ratio_ignoring_full_zoom().get() / device.device_pixel_ratio().get();
    VariableValue::pixels(int_pixels as f32 * unzoomed_scale, url_data)
}

static CHROME_ENVIRONMENT_VARIABLES: [EnvironmentVariable; 9] = [
    make_variable!(
        atom!("-moz-gtk-csd-titlebar-radius"),
        eval_gtk_csd_titlebar_radius
    ),
    lnf_int_variable!(
        atom!("-moz-gtk-csd-tooltip-radius"),
        TooltipRadius,
        int_pixels
    ),
    lnf_int_variable!(
        atom!("-moz-gtk-csd-close-button-position"),
        GTKCSDCloseButtonPosition,
        integer
    ),
    lnf_int_variable!(
        atom!("-moz-gtk-csd-minimize-button-position"),
        GTKCSDMinimizeButtonPosition,
        integer
    ),
    lnf_int_variable!(
        atom!("-moz-gtk-csd-maximize-button-position"),
        GTKCSDMaximizeButtonPosition,
        integer
    ),
    lnf_int_variable!(
        atom!("-moz-overlay-scrollbar-fade-duration"),
        ScrollbarFadeDuration,
        int_ms
    ),
    make_variable!(
        atom!("-moz-content-preferred-color-scheme"),
        get_content_preferred_color_scheme
    ),
    make_variable!(atom!("scrollbar-inline-size"), get_scrollbar_inline_size),
    make_variable!(atom!("hairline"), get_hairline),
];

impl CssEnvironment {
    /// Get an env() variable, either custom or not.
    #[inline]
    pub fn get(
        &self,
        name: &Atom,
        device: &Device,
        url_data: &UrlExtraData,
    ) -> Option<VariableValue> {
        #[cfg(feature = "gecko")]
        let is_link_parameter = name.as_slice().starts_with(&[b'-' as u16, b'-' as u16]);
        #[cfg(feature = "servo")]
        let is_link_parameter = name.starts_with("--");
        if is_link_parameter {
            let param = device
                .link_parameters()?
                .0
                .iter()
                .find(|p| p.name.0 == *name)?;
            let mut input = cssparser::ParserInput::new(param.value.0.as_ref());
            let mut parser = cssparser::Parser::new(&mut input);
            // need to carry around full variable value https://bugzilla.mozilla.org/show_bug.cgi?id=2028998
            return VariableValue::parse(&mut parser, None, url_data).ok();
        }

        if let Some(var) = ENVIRONMENT_VARIABLES.iter().find(|var| var.name == *name) {
            return Some((var.evaluator)(device, url_data));
        }
        if !url_data.chrome_rules_enabled() {
            return None;
        }
        let var = CHROME_ENVIRONMENT_VARIABLES
            .iter()
            .find(|var| var.name == *name)?;
        Some((var.evaluator)(device, url_data))
    }
}

/// A custom property name is just an `Atom`.
///
/// Note that this does not include the `--` prefix
pub type Name = Atom;

impl LocalName {
    #[cfg(feature = "gecko")]
    fn with_name<'a, R>(name: &'a Name, callback: impl FnOnce(&Self) -> R) -> R {
        callback(Self::cast(name))
    }

    #[cfg(feature = "servo")]
    fn with_name<'a, R>(name: &'a Name, callback: impl FnOnce(&Self) -> R) -> R {
        callback(&name.as_ref().into())
    }
}

impl From<Name> for LocalName {
    #[cfg(feature = "gecko")]
    fn from(name: Name) -> Self {
        Self::new(name)
    }

    #[cfg(feature = "servo")]
    fn from(name: Name) -> Self {
        name.as_ref().into()
    }
}

/// Parse a custom property name.
///
/// <https://drafts.csswg.org/css-variables/#typedef-custom-property-name>
pub fn parse_name(s: &str) -> Result<&str, ()> {
    if s.starts_with("--") && s.len() > 2 {
        Ok(&s[2..])
    } else {
        Err(())
    }
}

/// A value for a custom property is just a set of tokens.
///
/// We preserve the original CSS for serialization, and also the variable
/// references to other custom property names.
#[derive(Clone, Debug, MallocSizeOf, ToShmem)]
pub struct VariableValue {
    /// The raw CSS string.
    pub css: String,

    /// The url data of the stylesheet where this value came from.
    pub url_data: UrlExtraData,

    first_token_type: TokenSerializationType,
    last_token_type: TokenSerializationType,

    /// var(), env(), attr() or non-custom property (e.g. through `em`) references.
    pub references: References,

    /// Was this variable value attr tainted before. Used to keep attr tainting
    /// information when uncomputing a style that needs to be put back into the
    /// cascade.
    pub explicitly_attr_tainted: bool,
}

trivial_to_computed_value!(VariableValue);

/// Given a potentially registered variable value turn it into a computed custom property value.
pub(crate) fn compute_variable_value(
    value: &Arc<VariableValue>,
    registration: &PropertyDescriptors,
    computed_context: &computed::Context,
) -> Option<ComputedRegisteredValue> {
    if registration.is_universal() {
        return Some(ComputedRegisteredValue::universal(Arc::clone(value)));
    }
    compute_value(
        &value.css,
        &value.url_data,
        registration,
        computed_context,
        AttrTaint::default(),
    )
    .ok()
}

// For all purposes, we want values to be considered equal if their css text is equal.
impl PartialEq for VariableValue {
    fn eq(&self, other: &Self) -> bool {
        self.css == other.css
    }
}

impl Eq for VariableValue {}

impl ToCss for SpecifiedValue {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: Write,
    {
        dest.write_str(&self.css)
    }
}

impl ToTyped for SpecifiedValue {
    fn to_typed(&self, dest: &mut ThinVec<TypedValue>) -> Result<(), ()> {
        let unparsed_value = reify_variable_value(self)?;
        dest.push(TypedValue::Unparsed(unparsed_value));
        Ok(())
    }
}

fn reify_variable_value(value: &VariableValue) -> Result<UnparsedValue, ()> {
    let mut reference_index = 0;
    reify_variable_value_range(
        &value.css,
        &value.references.refs,
        &mut reference_index,
        0,
        value.css.len(),
    )
}

/// Reify a slice of the CSS string into UnparsedSegment entries.
///
/// References are stored in source order, with outer substitution functions
/// inserted before references in their fallback. The shared `reference_index`
/// relies on this ordering to recurse into fallbacks without reprocessing
/// nested referecences.
fn reify_variable_value_range(
    css: &str,
    references: &[SubstitutionFunctionReference],
    reference_index: &mut usize,
    start: usize,
    end: usize,
) -> Result<UnparsedValue, ()> {
    debug_assert!(start <= end);
    debug_assert!(end <= css.len());

    let mut values = ThinVec::new();
    let mut cur_pos = start;

    while *reference_index < references.len() {
        let reference = &references[*reference_index];

        if reference.start >= end {
            break;
        }

        debug_assert!(reference.start >= cur_pos);
        debug_assert!(reference.start <= reference.end);
        debug_assert!(reference.end <= css.len());

        if cur_pos < reference.start {
            values.push(UnparsedSegment::String(CssString::from(
                &css[cur_pos..reference.start],
            )));
        }

        *reference_index += 1;

        if reference.substitution_kind != SubstitutionFunctionKind::Var {
            return Err(());
        }

        let (fallback, has_fallback) = if let Some(fallback) = &reference.fallback {
            debug_assert!(fallback.start.get() <= reference.end - 1);

            (
                reify_variable_value_range(
                    css,
                    references,
                    reference_index,
                    fallback.start.get(),
                    reference.end - 1, // Skip the closing ')'.
                )?,
                true,
            )
        } else {
            (ThinVec::new(), false)
        };

        values.push(UnparsedSegment::VariableReference(VariableReferenceValue {
            variable: CssString::from(format!("--{}", reference.name)),
            fallback,
            has_fallback,
        }));

        cur_pos = reference.end;
    }

    if cur_pos < end {
        values.push(UnparsedSegment::String(CssString::from(&css[cur_pos..end])));
    }

    Ok(values)
}

/// A pair of separate CustomPropertiesMaps, split between custom properties
/// that have the inherit flag set and those with the flag unset.
#[repr(C)]
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ComputedCustomProperties {
    /// Map for custom properties with inherit flag set, including non-registered
    /// ones.
    pub inherited: CustomPropertiesMap,
    /// Map for custom properties with inherit flag unset.
    pub non_inherited: CustomPropertiesMap,
}

impl ComputedCustomProperties {
    /// Return whether the inherited and non_inherited maps are none.
    pub fn is_empty(&self) -> bool {
        self.inherited.is_empty() && self.non_inherited.is_empty()
    }

    /// Return the name and value of the property at specified index, if any.
    pub fn property_at(&self, index: usize) -> Option<(&Name, &Option<ComputedRegisteredValue>)> {
        // Just expose the custom property items from custom_properties.inherited, followed
        // by custom property items from custom_properties.non_inherited.
        self.inherited
            .get_index(index)
            .or_else(|| self.non_inherited.get_index(index - self.inherited.len()))
    }

    /// Insert a custom property in the corresponding inherited/non_inherited
    /// map, depending on whether the inherit flag is set or unset.
    pub fn insert(
        &mut self,
        registration: &PropertyDescriptors,
        name: &Name,
        value: ComputedRegisteredValue,
    ) {
        self.map_mut(registration).insert(name, value)
    }

    /// Remove a custom property from the corresponding inherited/non_inherited
    /// map, depending on whether the inherit flag is set or unset.
    pub fn remove(&mut self, registration: &PropertyDescriptors, name: &Name) {
        self.map_mut(registration).remove(name);
    }

    /// Shrink the capacity of the inherited maps as much as possible.
    pub fn shrink_to_fit(&mut self) {
        self.inherited.shrink_to_fit();
        self.non_inherited.shrink_to_fit();
    }

    fn map_mut(&mut self, registration: &PropertyDescriptors) -> &mut CustomPropertiesMap {
        if registration.inherits() {
            &mut self.inherited
        } else {
            &mut self.non_inherited
        }
    }

    /// Returns the relevant custom property value given a registration.
    pub fn get(
        &self,
        registration: &PropertyDescriptors,
        name: &Name,
    ) -> Option<&ComputedRegisteredValue> {
        if registration.inherits() {
            self.inherited.get(name)
        } else {
            self.non_inherited.get(name)
        }
    }

    /// Returns a property just by the name. Slightly less efficient than get(), if you already have
    /// a custom registration handy, thus the longer name.
    pub fn get_for_cssom(&self, name: &Name) -> Option<&ComputedRegisteredValue> {
        self.inherited
            .get(name)
            .or_else(|| self.non_inherited.get(name))
    }
}

/// Both specified and computed values are VariableValues, the difference is
/// whether var() functions are expanded.
pub type SpecifiedValue = VariableValue;
/// Both specified and computed values are VariableValues, the difference is
/// whether var() functions are expanded.
pub type ComputedValue = VariableValue;

/// Set of flags to references this custom property makes.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, MallocSizeOf, ToShmem)]
pub struct ReferenceFlags(u8);

bitflags! {
    impl ReferenceFlags : u8 {
        /// At least one custom property depends on font-relative units.
        const FONT_UNITS = 1 << 0;
        /// At least one custom property depends on root element's font-relative units.
        const ROOT_FONT_UNITS = 1 << 1;
        /// At least one custom property depends on line height units.
        const LH_UNITS = 1 << 2;
        /// At least one custom property depends on root element's line height units.
        const ROOT_LH_UNITS = 1 << 3;
        /// The value depends on the used color-scheme (e.g. a registered `<color>` property).
        const COLOR_SCHEME = 1 << 4;
        /// All dependencies not depending on the root element.
        const NON_ROOT_DEPENDENCIES = Self::FONT_UNITS.0 | Self::LH_UNITS.0;
        /// All dependencies depending on the root element.
        const ROOT_DEPENDENCIES = Self::ROOT_FONT_UNITS.0 | Self::ROOT_LH_UNITS.0;
        /// All non-custom dependencies
        const NON_CUSTOM = Self::NON_ROOT_DEPENDENCIES.0 | Self::ROOT_DEPENDENCIES.0;
        /// At least one attr() reference.
        const ATTR = 1 << 5;
        /// At least one env() reference.
        const ENV = 1 << 6;
        /// At least one var() reference.
        const VAR = 1 << 7;
    }
}

impl ReferenceFlags {
    /// Iterate for each non custom
    pub fn for_each_non_custom<F>(mut self, is_root_element: bool, mut f: F)
    where
        F: FnMut(SingleNonCustomReference),
    {
        // On the root element, rem etc refer to the root's own dependencies.
        if is_root_element {
            if self.intersects(Self::ROOT_FONT_UNITS) {
                self.remove(Self::ROOT_FONT_UNITS);
                self |= Self::FONT_UNITS;
            }
            if self.intersects(Self::ROOT_LH_UNITS) {
                self.remove(Self::ROOT_FONT_UNITS);
                self |= Self::LH_UNITS;
            }
        }

        for (_, r) in self.iter_names() {
            let single = match r {
                Self::FONT_UNITS => SingleNonCustomReference::FontUnits,
                Self::LH_UNITS => SingleNonCustomReference::LhUnits,
                Self::COLOR_SCHEME => SingleNonCustomReference::ColorScheme,
                Self::ROOT_FONT_UNITS
                | Self::ROOT_LH_UNITS
                | Self::VAR
                | Self::ENV
                | Self::ATTR => continue,
                _ => unreachable!("Unexpected single bit value"),
            };
            f(single);
        }
    }

    fn from_unit(value: &CowRcStr) -> Self {
        // For registered properties, any reference to font-relative dimensions
        // make it dependent on font-related properties.
        // TODO(dshin): When we unit algebra gets implemented and handled -
        // Is it valid to say that `calc(1em / 2em * 3px)` triggers this?
        if value.eq_ignore_ascii_case(NoCalcLength::LH) {
            return Self::FONT_UNITS | Self::LH_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::EM)
            || value.eq_ignore_ascii_case(NoCalcLength::EX)
            || value.eq_ignore_ascii_case(NoCalcLength::CAP)
            || value.eq_ignore_ascii_case(NoCalcLength::CH)
            || value.eq_ignore_ascii_case(NoCalcLength::IC)
        {
            return Self::FONT_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::RLH) {
            return Self::ROOT_FONT_UNITS | Self::ROOT_LH_UNITS;
        }
        if value.eq_ignore_ascii_case(NoCalcLength::REM)
            || value.eq_ignore_ascii_case(NoCalcLength::REX)
            || value.eq_ignore_ascii_case(NoCalcLength::RCH)
            || value.eq_ignore_ascii_case(NoCalcLength::RCAP)
            || value.eq_ignore_ascii_case(NoCalcLength::RIC)
        {
            return Self::ROOT_FONT_UNITS;
        }
        Self::empty()
    }
}

/// A non-custom reference that participates in cycle resolution.
/// TODO(emilio): This should probably eventually become just PrioritaryPropertyId.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[allow(missing_docs)]
pub enum SingleNonCustomReference {
    FontUnits = 0,
    LhUnits,
    ColorScheme,
}

impl SingleNonCustomReference {
    /// Returns a prioritary id for this reference.
    pub fn to_prioritary_id(self) -> PrioritaryPropertyId {
        match self {
            Self::FontUnits => PrioritaryPropertyId::FontSize,
            Self::LhUnits => PrioritaryPropertyId::LineHeight,
            Self::ColorScheme => PrioritaryPropertyId::ColorScheme,
        }
    }
}

/// A map from NonCustomReferenceMap to a T.
pub struct NonCustomReferenceMap<T>([Option<T>; 3]);

impl<T> Default for NonCustomReferenceMap<T> {
    fn default() -> Self {
        NonCustomReferenceMap(Default::default())
    }
}

impl<T> Index<SingleNonCustomReference> for NonCustomReferenceMap<T> {
    type Output = Option<T>;

    fn index(&self, reference: SingleNonCustomReference) -> &Self::Output {
        &self.0[reference as usize]
    }
}

impl<T> IndexMut<SingleNonCustomReference> for NonCustomReferenceMap<T> {
    fn index_mut(&mut self, reference: SingleNonCustomReference) -> &mut Self::Output {
        &mut self.0[reference as usize]
    }
}

/// Substitution function source: var, env, attr.
#[derive(Copy, Clone, Debug, MallocSizeOf, Hash, Eq, PartialEq, ToShmem, Parse)]
pub enum SubstitutionFunctionKind {
    /// CSS variable / custom property
    Var,
    /// Environment variable
    Env,
    /// DOM attribute
    Attr,
}

/// A wrapper map that encapsulates both the custom properties and attributes
/// for a given element.
#[repr(C)]
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ComputedSubstitutionFunctions {
    /// The applicable custom properties (includes inherited and non-inherited).
    pub custom_properties: ComputedCustomProperties,
    /// The applicable DOM attributes.
    pub attributes: OwnMap,
}

impl ComputedSubstitutionFunctions {
    /// Creates a substitution function map from optional custom properties
    /// and DOM attributes.
    #[inline(always)]
    pub fn new(
        custom_properties: Option<ComputedCustomProperties>,
        attributes: Option<OwnMap>,
    ) -> Self {
        Self {
            custom_properties: custom_properties.unwrap_or_default(),
            attributes: attributes.unwrap_or_default(),
        }
    }

    #[inline(always)]
    pub(crate) fn insert_var(
        &mut self,
        registration: &PropertyDescriptors,
        name: &Name,
        value: ComputedRegisteredValue,
    ) {
        self.custom_properties.insert(registration, name, value);
    }

    #[inline(always)]
    pub(crate) fn insert_attr(&mut self, name: &Name, value: ComputedRegisteredValue) {
        self.attributes.insert(name.clone(), Some(value));
    }

    #[inline(always)]
    pub(crate) fn remove_var(&mut self, registration: &PropertyDescriptors, name: &Name) {
        self.custom_properties.remove(registration, name);
    }

    #[inline(always)]
    pub(crate) fn remove_attr(&mut self, name: &Name) {
        self.attributes.insert(name.clone(), None);
    }

    #[inline(always)]
    pub(crate) fn get_var(
        &self,
        registration: &PropertyDescriptors,
        name: &Name,
    ) -> Option<&ComputedRegisteredValue> {
        self.custom_properties.get(registration, name)
    }

    #[inline(always)]
    pub(crate) fn get_attr(&self, name: &Name) -> Option<&ComputedRegisteredValue> {
        self.attributes.get(name).and_then(|p| p.as_ref())
    }
}

#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem, Parse)]
enum AttributeType {
    Invalid,
    None,
    RawString,
    Type(SyntaxDescriptor),
    Unit(AttrUnit),
}

/// Data specific to an attr() call like type and namespace.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttributeData {
    kind: AttributeType,
    namespace: ParsedNamespace,
}

/// For a CSS string, the range, counted in bytes, that is attr()-tainted.
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem, ToComputedValue)]
pub struct AttrTaintedRange {
    /// Start of the range, counted in bytes. Inclusive.
    start: usize,
    /// End of the range, counted in bytes. Exclusive.
    end: usize,
}

impl AttrTaintedRange {
    /// Creates a range within a CSS string that is tainted by attr().
    #[inline(always)]
    pub fn new(start: usize, end: usize) -> Self {
        debug_assert!(start <= end);
        Self { start, end }
    }
}

/// In CSS Values and Units, values produced by `attr()` are considered attr()-tainted, as are
/// functions that contain an attr()-tainted value. Using an attr()-tainted value as or in a <url>
/// makes a declaration invalid at computed-value time.
/// https://drafts.csswg.org/css-values-5/#attr-security
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttrTaint(SmallVec<[AttrTaintedRange; 1]>);

impl AttrTaint {
    /// For a CSS string, determine whether any `<url>` overlapping this `range`
    /// is disallowed due to attr()-tainting.
    #[inline(always)]
    pub fn should_disallow_urls_in_range(&self, range: &AttrTaintedRange) -> bool {
        self.0
            .iter()
            .any(|r| r.start <= range.end && r.end >= range.start)
    }

    /// Returns true if the attr()-tainted range contains no elements.
    #[inline(always)]
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }

    #[inline(always)]
    fn new_fully_tainted(end: usize) -> Self {
        let mut taint = Self::default();
        taint.push(0, end);
        taint
    }

    #[inline(always)]
    fn push(&mut self, start: usize, end: usize) {
        self.0.push(AttrTaintedRange::new(start, end));
    }
}

/// The fallback of a particular value.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct VariableFallback {
    // NOTE(emilio): We don't track fallback end, because we rely on the missing closing
    // parenthesis, if any, to be inserted, which means that we can rely on our end being
    // reference.end - 1.
    start: num::NonZeroUsize,
    first_token_type: TokenSerializationType,
    last_token_type: TokenSerializationType,
    /// References from this fallback value.
    pub references: References,
}

/// A reference to a substitution function like env() / var() / attr().
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct SubstitutionFunctionReference {
    /// The identifier for this substitution function (the first argument).
    pub name: Name,
    start: usize,
    end: usize,
    /// The fallback for this function, if any.
    pub fallback: Option<VariableFallback>,
    /// attr() specific data, only relevant if substitution_kind is `Attr`..
    pub attribute_data: AttributeData,
    prev_token_type: TokenSerializationType,
    next_token_type: TokenSerializationType,
    /// The kind of substitution function we are.
    pub substitution_kind: SubstitutionFunctionKind,
}

impl SubstitutionFunctionReference {
    /// Whether we're an attr(... type(...)) reference.
    pub fn is_attr_with_type(&self) -> bool {
        self.substitution_kind == SubstitutionFunctionKind::Attr
            && matches!(self.attribute_data.kind, AttributeType::Type(..))
    }
}

/// A struct holding information about the external references to that a custom property value may
/// have.
#[derive(Clone, Debug, Default, MallocSizeOf, PartialEq, ToShmem)]
pub struct References {
    /// The actual list of references that we end up with.
    pub refs: Vec<SubstitutionFunctionReference>,
    /// Various flags about the kind of references we hold. Note that these include data about our
    /// nested references.
    ///
    /// TODO(emilio): Do we need to distinguish between own and nested flags?
    pub flags: ReferenceFlags,
}

impl References {
    fn has_references(&self) -> bool {
        !self.refs.is_empty()
    }

    pub(crate) fn non_custom_references(&self, is_root_element: bool) -> ReferenceFlags {
        let mut mask = ReferenceFlags::NON_ROOT_DEPENDENCIES;
        if is_root_element {
            mask |= ReferenceFlags::ROOT_DEPENDENCIES
        }
        self.flags & mask
    }
}

impl VariableValue {
    fn empty(url_data: &UrlExtraData) -> Self {
        Self {
            css: String::new(),
            last_token_type: Default::default(),
            first_token_type: Default::default(),
            url_data: url_data.clone(),
            references: Default::default(),
            explicitly_attr_tainted: false,
        }
    }

    /// Create a new custom property without parsing if the CSS is known to be valid and contain no
    /// references.
    pub fn new(
        css: String,
        url_data: &UrlExtraData,
        first_token_type: TokenSerializationType,
        last_token_type: TokenSerializationType,
    ) -> Self {
        Self {
            css,
            url_data: url_data.clone(),
            first_token_type,
            last_token_type,
            references: References::default(),
            explicitly_attr_tainted: false,
        }
    }

    fn push<'i>(
        &mut self,
        css: &str,
        css_first_token_type: TokenSerializationType,
        css_last_token_type: TokenSerializationType,
        attr_taint: Option<&mut AttrTaint>,
    ) -> Result<(), ()> {
        /// Prevent values from getting terribly big since you can use custom
        /// properties exponentially.
        ///
        /// This number (2MB) is somewhat arbitrary, but silly enough that no
        /// reasonable page should hit it. We could limit by number of total
        /// substitutions, but that was very easy to work around in practice
        /// (just choose a larger initial value and boom).
        const MAX_VALUE_LENGTH_IN_BYTES: usize = 2 * 1024 * 1024;

        if self.css.len() + css.len() > MAX_VALUE_LENGTH_IN_BYTES {
            return Err(());
        }

        // This happens e.g. between two subsequent var() functions:
        // `var(--a)var(--b)`.
        //
        // In that case, css_*_token_type is nonsensical.
        if css.is_empty() {
            return Ok(());
        }

        self.first_token_type.set_if_nothing(css_first_token_type);
        // If self.first_token_type was nothing,
        // self.last_token_type is also nothing and this will be false:
        if self
            .last_token_type
            .needs_separator_when_before(css_first_token_type)
        {
            self.css.push_str("/**/")
        }
        let start = self.css.len();
        self.css.push_str(css);
        let end = self.css.len();
        if let Some(taint) = attr_taint {
            taint.push(start, end);
        }
        self.last_token_type = css_last_token_type;
        Ok(())
    }

    /// Parse a custom property value.
    pub fn parse<'i, 't>(
        input: &mut Parser<'i, 't>,
        namespaces: Option<&FxHashMap<Prefix, Namespace>>,
        url_data: &UrlExtraData,
    ) -> Result<Self, ParseError<'i>> {
        let mut references = References::default();
        let mut missing_closing_characters = String::new();
        let start_position = input.position();
        let (first_token_type, last_token_type) = parse_declaration_value(
            input,
            start_position,
            namespaces,
            &mut references,
            &mut missing_closing_characters,
        )?;
        let mut css = input
            .slice_from(start_position)
            .trim_ascii_start()
            .to_owned();
        if !missing_closing_characters.is_empty() {
            // Unescaped backslash at EOF in a quoted string is ignored.
            if css.ends_with("\\")
                && matches!(missing_closing_characters.as_bytes()[0], b'"' | b'\'')
            {
                css.pop();
            }
            css.push_str(&missing_closing_characters);
        }

        css.truncate(css.trim_ascii_end().len());
        css.shrink_to_fit();
        references.refs.shrink_to_fit();

        Ok(Self {
            css,
            url_data: url_data.clone(),
            first_token_type,
            last_token_type,
            references,
            explicitly_attr_tainted: false,
        })
    }

    /// Returns whether this value is tainted by `attr()`.
    pub fn is_attr_tainted(&self) -> bool {
        self.references.flags.intersects(ReferenceFlags::ATTR) | self.explicitly_attr_tainted
    }

    /// Create VariableValue from an int.
    fn integer(number: i32, url_data: &UrlExtraData) -> Self {
        Self::from_token(
            Token::Number {
                has_sign: false,
                value: number as f32,
                int_value: Some(number),
            },
            url_data,
        )
    }

    /// Create VariableValue from an int.
    fn ident(ident: &'static str, url_data: &UrlExtraData) -> Self {
        Self::from_token(Token::Ident(ident.into()), url_data)
    }

    /// Create VariableValue from a float amount of CSS pixels.
    fn pixels(number: f32, url_data: &UrlExtraData) -> Self {
        // FIXME (https://github.com/servo/rust-cssparser/issues/266):
        // No way to get TokenSerializationType::Dimension without creating
        // Token object.
        Self::from_token(
            Token::Dimension {
                has_sign: false,
                value: number,
                int_value: None,
                unit: CowRcStr::from("px"),
            },
            url_data,
        )
    }

    /// Create VariableValue from an integer amount of milliseconds.
    fn int_ms(number: i32, url_data: &UrlExtraData) -> Self {
        Self::from_token(
            Token::Dimension {
                has_sign: false,
                value: number as f32,
                int_value: Some(number),
                unit: CowRcStr::from("ms"),
            },
            url_data,
        )
    }

    /// Create VariableValue from an integer amount of CSS pixels.
    fn int_pixels(number: i32, url_data: &UrlExtraData) -> Self {
        Self::from_token(
            Token::Dimension {
                has_sign: false,
                value: number as f32,
                int_value: Some(number),
                unit: CowRcStr::from("px"),
            },
            url_data,
        )
    }

    fn from_token(token: Token, url_data: &UrlExtraData) -> Self {
        let token_type = token.serialization_type();
        let mut css = token.to_css_string();
        css.shrink_to_fit();

        VariableValue {
            css,
            url_data: url_data.clone(),
            first_token_type: token_type,
            last_token_type: token_type,
            references: Default::default(),
            explicitly_attr_tainted: false,
        }
    }

    /// Returns the raw CSS text from this VariableValue
    pub fn css_text(&self) -> &str {
        &self.css
    }

    /// Returns whether this variable value has any reference to the environment or other
    /// variables.
    pub fn has_references(&self) -> bool {
        self.references.has_references()
    }

    /// Returns the attribute references in this variable value, if any.
    pub fn collect_attribute_references(&self, references: &mut AttrReferenceSet) {
        self.references.refs.iter().for_each(|r| {
            if r.substitution_kind == SubstitutionFunctionKind::Attr {
                // For a non-ascii-lowercase attribute, whether we match
                // case-sensitively depends on whether the element we're
                // matching against is an HTML element in an HTML document.
                // So to simplify invalidation we collect both potential
                // references here.
                references.insert(r.name.clone().into());
                let lowercase = r.name.to_ascii_lowercase();
                if r.name != lowercase {
                    references.insert(lowercase.into());
                }
            }
        })
    }
}

/// <https://drafts.csswg.org/css-syntax-3/#typedef-declaration-value>
fn parse_declaration_value<'i, 't>(
    input: &mut Parser<'i, 't>,
    input_start: SourcePosition,
    namespaces: Option<&FxHashMap<Prefix, Namespace>>,
    references: &mut References,
    missing_closing_characters: &mut String,
) -> Result<(TokenSerializationType, TokenSerializationType), ParseError<'i>> {
    input.parse_until_before(Delimiter::Bang | Delimiter::Semicolon, |input| {
        parse_declaration_value_block(
            input,
            input_start,
            namespaces,
            references,
            missing_closing_characters,
        )
    })
}

/// Like parse_declaration_value, but accept `!` and `;` since they are only invalid at the top level.
fn parse_declaration_value_block<'i, 't>(
    input: &mut Parser<'i, 't>,
    input_start: SourcePosition,
    namespaces: Option<&FxHashMap<Prefix, Namespace>>,
    references: &mut References,
    missing_closing_characters: &mut String,
) -> Result<(TokenSerializationType, TokenSerializationType), ParseError<'i>> {
    let mut is_first = true;
    let mut first_token_type = TokenSerializationType::Nothing;
    let mut last_token_type = TokenSerializationType::Nothing;
    let mut prev_reference_index: Option<usize> = None;
    loop {
        let token_start = input.position();
        let Ok(token) = input.next_including_whitespace_and_comments() else {
            break;
        };

        let prev_token_type = last_token_type;
        let serialization_type = token.serialization_type();
        last_token_type = serialization_type;
        if is_first {
            first_token_type = last_token_type;
            is_first = false;
        }

        macro_rules! nested {
            ($closing:expr) => {{
                let mut inner_end_position = None;
                let result = input.parse_nested_block(|input| {
                    let result = parse_declaration_value_block(
                        input,
                        input_start,
                        namespaces,
                        references,
                        missing_closing_characters,
                    )?;
                    inner_end_position = Some(input.position());
                    Ok(result)
                })?;
                if inner_end_position.unwrap() == input.position() {
                    missing_closing_characters.push_str($closing);
                }
                result
            }};
        }
        if let Some(index) = prev_reference_index.take() {
            references.refs[index].next_token_type = serialization_type;
        }
        match *token {
            Token::Comment(_) => {
                let token_slice = input.slice_from(token_start);
                if !token_slice.ends_with("*/") {
                    missing_closing_characters.push_str(if token_slice.ends_with('*') {
                        "/"
                    } else {
                        "*/"
                    })
                }
            },
            Token::BadUrl(ref u) => {
                let e = StyleParseErrorKind::BadUrlInDeclarationValueBlock(u.clone());
                return Err(input.new_custom_error(e));
            },
            Token::BadString(ref s) => {
                let e = StyleParseErrorKind::BadStringInDeclarationValueBlock(s.clone());
                return Err(input.new_custom_error(e));
            },
            Token::CloseParenthesis => {
                let e = StyleParseErrorKind::UnbalancedCloseParenthesisInDeclarationValueBlock;
                return Err(input.new_custom_error(e));
            },
            Token::CloseSquareBracket => {
                let e = StyleParseErrorKind::UnbalancedCloseSquareBracketInDeclarationValueBlock;
                return Err(input.new_custom_error(e));
            },
            Token::CloseCurlyBracket => {
                let e = StyleParseErrorKind::UnbalancedCloseCurlyBracketInDeclarationValueBlock;
                return Err(input.new_custom_error(e));
            },
            Token::Function(ref name) => {
                let substitution_kind = match SubstitutionFunctionKind::from_ident(name).ok() {
                    Some(SubstitutionFunctionKind::Attr) => {
                        if static_prefs::pref!("layout.css.attr.enabled") {
                            Some(SubstitutionFunctionKind::Attr)
                        } else {
                            None
                        }
                    },
                    kind => kind,
                };
                if let Some(substitution_kind) = substitution_kind {
                    let our_ref_index = references.refs.len();
                    let mut input_end_position = None;
                    let fallback = input.parse_nested_block(|input| {
                        let mut namespace = ParsedNamespace::Known(Namespace::default());
                        if substitution_kind == SubstitutionFunctionKind::Attr {
                            if let Some(namespaces) = namespaces {
                                if let Ok(ns) = input
                                    .try_parse(|input| ParsedNamespace::parse(namespaces, input))
                                {
                                    namespace = ns;
                                    let prev = input.state();
                                    let next = match *input.next_including_whitespace()? {
                                        Token::Ident(_) => Ok(()),
                                        ref t => Err(prev
                                            .source_location()
                                            .new_unexpected_token_error(t.clone())),
                                    };
                                    input.reset(&prev);
                                    next?;
                                }
                            }
                        }
                        // TODO(emilio): For env() this should be <custom-ident> per spec, but no other browser does
                        // that, see https://github.com/w3c/csswg-drafts/issues/3262.
                        let name = input.expect_ident()?;
                        let name =
                            Atom::from(if substitution_kind == SubstitutionFunctionKind::Var {
                                match parse_name(name.as_ref()) {
                                    Ok(name) => name,
                                    Err(()) => {
                                        let name = name.clone();
                                        return Err(input.new_custom_error(
                                            SelectorParseErrorKind::UnexpectedIdent(name),
                                        ));
                                    },
                                }
                            } else {
                                name.as_ref()
                            });

                        let attribute_kind = if substitution_kind == SubstitutionFunctionKind::Attr
                        {
                            parse_attr_type(input)
                        } else {
                            AttributeType::None
                        };

                        // We want the order of the references to match source order. So we need to reserve our slot
                        // now, _before_ parsing our fallback. Note that we don't care if parsing fails after all, since
                        // if this fails we discard the whole result anyways.
                        let start = token_start.byte_index() - input_start.byte_index();
                        references.refs.push(SubstitutionFunctionReference {
                            name,
                            start,
                            // To be fixed up after parsing fallback and auto-closing via our_ref_index.
                            end: start,
                            prev_token_type,
                            // To be fixed up (if needed) on the next loop iteration via prev_reference_index.
                            next_token_type: TokenSerializationType::Nothing,
                            // To be fixed up after parsing fallback.
                            fallback: None,
                            attribute_data: AttributeData {
                                kind: attribute_kind,
                                namespace,
                            },
                            substitution_kind: substitution_kind.clone(),
                        });

                        let mut fallback = None;
                        if input.try_parse(|input| input.expect_comma()).is_ok() {
                            input.skip_whitespace();
                            let fallback_start = num::NonZeroUsize::new(
                                input.position().byte_index() - input_start.byte_index(),
                            )
                            .unwrap();
                            let mut references = References::default();
                            // NOTE(emilio): Intentionally using parse_declaration_value rather than
                            // parse_declaration_value_block, since that's what parse_fallback used to do.
                            let (first, last) = parse_declaration_value(
                                input,
                                input_start,
                                namespaces,
                                &mut references,
                                missing_closing_characters,
                            )?;
                            fallback = Some(VariableFallback {
                                start: fallback_start,
                                first_token_type: first,
                                last_token_type: last,
                                references,
                            });
                            input_end_position = Some(input.position());
                        } else {
                            let state = input.state();
                            // We still need to consume the rest of the potentially-unclosed
                            // tokens, but make sure to not consume tokens that would otherwise be
                            // invalid, by calling reset().
                            parse_declaration_value_block(
                                input,
                                input_start,
                                namespaces,
                                references,
                                missing_closing_characters,
                            )?;
                            input_end_position = Some(input.position());
                            input.reset(&state);
                        }
                        Ok(fallback)
                    })?;
                    if input_end_position.unwrap() == input.position() {
                        missing_closing_characters.push_str(")");
                    }
                    prev_reference_index = Some(our_ref_index);
                    let reference = &mut references.refs[our_ref_index];
                    reference.end = input.position().byte_index() - input_start.byte_index()
                        + missing_closing_characters.len();
                    reference.fallback = fallback;
                    references.flags |= match substitution_kind {
                        SubstitutionFunctionKind::Var => ReferenceFlags::VAR,
                        SubstitutionFunctionKind::Env => ReferenceFlags::ENV,
                        SubstitutionFunctionKind::Attr => ReferenceFlags::ATTR,
                    };
                    // Bubble up flags from our fallback, so we know what we might reference from the outer scope.
                    if let Some(ref fb) = reference.fallback {
                        references.flags |= fb.references.flags;
                    }
                } else {
                    nested!(")");
                }
            },
            Token::ParenthesisBlock => {
                nested!(")");
            },
            Token::CurlyBracketBlock => {
                nested!("}");
            },
            Token::SquareBracketBlock => {
                nested!("]");
            },
            Token::QuotedString(_) => {
                let token_slice = input.slice_from(token_start);
                let quote = &token_slice[..1];
                debug_assert!(matches!(quote, "\"" | "'"));
                if !(token_slice.ends_with(quote) && token_slice.len() > 1) {
                    missing_closing_characters.push_str(quote)
                }
            },
            Token::Ident(ref value)
            | Token::AtKeyword(ref value)
            | Token::Hash(ref value)
            | Token::IDHash(ref value)
            | Token::UnquotedUrl(ref value)
            | Token::Dimension {
                unit: ref value, ..
            } => {
                references.flags.insert(ReferenceFlags::from_unit(value));
                let is_unquoted_url = matches!(token, Token::UnquotedUrl(_));
                if value.ends_with("�") && input.slice_from(token_start).ends_with("\\") {
                    // Unescaped backslash at EOF in these contexts is interpreted as U+FFFD
                    // Check the value in case the final backslash was itself escaped.
                    // Serialize as escaped U+FFFD, which is also interpreted as U+FFFD.
                    // (Unescaped U+FFFD would also work, but removing the backslash is annoying.)
                    missing_closing_characters.push_str("�")
                }
                if is_unquoted_url && !input.slice_from(token_start).ends_with(")") {
                    missing_closing_characters.push_str(")");
                }
            },
            _ => {},
        };
    }
    Ok((first_token_type, last_token_type))
}

/// Parse <attr-type> = type( <syntax> ) | raw-string | number | <attr-unit>.
/// https://drafts.csswg.org/css-values-5/#attr-notation
fn parse_attr_type<'i, 't>(input: &mut Parser<'i, 't>) -> AttributeType {
    input
        .try_parse(|input| {
            Ok(match input.next()? {
                Token::Function(ref name) if name.eq_ignore_ascii_case("type") => {
                    AttributeType::Type(
                        input.parse_nested_block(SyntaxDescriptor::from_css_parser)?,
                    )
                },
                Token::Ident(ref ident) => {
                    if ident.eq_ignore_ascii_case("raw-string") {
                        AttributeType::RawString
                    } else if let Ok(unit) = AttrUnit::from_ident(ident) {
                        AttributeType::Unit(unit)
                    } else {
                        AttributeType::Invalid
                    }
                },
                Token::Delim('%') => AttributeType::Unit(AttrUnit::Percentage),
                _ => return Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError)),
            })
        })
        .unwrap_or(AttributeType::None)
}

/// Attribute values may reference other substitution functions we may need to process.
/// See step 6: https://drafts.csswg.org/css-values-5/#attr-substitution
pub fn get_attr_value_for_cycle_resolution(
    name: &Name,
    attribute_data: &AttributeData,
    url_data: &UrlExtraData,
    attribute_tracker: &mut AttributeTracker,
) -> Result<ComputedRegisteredValue, ()> {
    let namespace = match attribute_data.namespace {
        ParsedNamespace::Known(ref ns) => ns,
        ParsedNamespace::Unknown => return Err(()),
    };
    let attr = LocalName::with_name(name, |local_name| {
        attribute_tracker.query(local_name, namespace).ok_or(())
    })?;
    let mut input = ParserInput::new(&attr);
    let mut parser = Parser::new(&mut input);
    // TODO(Bug 2021110): Support namespaced attributes in chained references.
    let value = VariableValue::parse(&mut parser, None, &url_data).map_err(|_| ())?;
    Ok(ComputedRegisteredValue::universal(Arc::new(value)))
}

/// See https://drafts.csswg.org/css-variables-2/#invalid-at-computed-value-time
pub fn handle_invalid_at_computed_value_time(
    name: &Name,
    registration: &PropertyDescriptors,
    context: &mut computed::Context,
) {
    if !registration.is_universal() {
        // For the root element, inherited maps are empty. We should just
        // use the initial value if any, rather than removing the name.
        if registration.inherits() && !context.builder.is_root_element {
            let inherited = context.builder.inherited_style.custom_properties();
            if let Some(value) = inherited.get(registration, name) {
                context.builder.substitution_functions.insert_var(
                    registration,
                    name,
                    value.clone(),
                );
                return;
            }
        } else if let Some(ref initial_value) = registration.initial_value {
            if let Ok(initial_value) = compute_value(
                &initial_value.css,
                &initial_value.url_data,
                registration,
                context,
                AttrTaint::default(),
            ) {
                context.builder.substitution_functions.insert_var(
                    registration,
                    name,
                    initial_value,
                );
                return;
            }
        }
    }
    context
        .builder
        .substitution_functions
        .remove_var(registration, name);
}

/// Replace `var()`, `env()`, and `attr()` functions in a pre-existing variable value.
pub fn substitute_references_if_needed_and_apply(
    name: &Name,
    kind: SubstitutionFunctionKind,
    value: &Arc<VariableValue>,
    stylist: &Stylist,
    context: &mut computed::Context,
    attribute_tracker: &mut AttributeTracker,
) {
    debug_assert_ne!(kind, SubstitutionFunctionKind::Env);
    let is_var = matches!(kind, SubstitutionFunctionKind::Var);
    let registration = stylist.get_custom_property_registration(&name);
    if is_var && !value.has_references() && registration.is_universal() {
        // Trivial path: no references and no need to compute the value, just apply it directly.
        let computed_value = ComputedRegisteredValue::universal(Arc::clone(value));
        context
            .builder
            .substitution_functions
            .insert_var(registration, name, computed_value);
        return;
    }

    let url_data = &value.url_data;
    let substitution = substitute_internal(
        value,
        &context.builder.substitution_functions,
        stylist,
        context,
        attribute_tracker,
        &mut SmallVec::new(),
        None,
    );

    let Ok(substitution) = substitution else {
        if is_var {
            handle_invalid_at_computed_value_time(name, registration, context);
        } else {
            context.builder.substitution_functions.remove_attr(name);
        }
        return;
    };

    // If variable fallback results in a wide keyword, deal with it now.
    let inherited = context.builder.inherited_style.custom_properties();
    if is_var {
        let css = &substitution.css;
        let css_wide_kw = {
            let mut input = ParserInput::new(&css);
            let mut input = Parser::new(&mut input);
            input.try_parse(CSSWideKeyword::parse)
        };

        if let Ok(kw) = css_wide_kw {
            // TODO: It's unclear what this should do for revert / revert-layer, see
            // https://github.com/w3c/csswg-drafts/issues/9131. For now treating as unset
            // seems fine?
            match (kw, registration.inherits(), context.is_root_element()) {
                (CSSWideKeyword::Initial, _, _)
                | (CSSWideKeyword::Revert, false, _)
                | (CSSWideKeyword::RevertLayer, false, _)
                | (CSSWideKeyword::RevertRule, false, _)
                | (CSSWideKeyword::Unset, false, _)
                | (CSSWideKeyword::Revert, true, true)
                | (CSSWideKeyword::RevertLayer, true, true)
                | (CSSWideKeyword::RevertRule, true, true)
                | (CSSWideKeyword::Unset, true, true)
                | (CSSWideKeyword::Inherit, _, true) => {
                    remove_and_insert_initial_value(
                        name,
                        registration,
                        &mut context.builder.substitution_functions,
                    );
                },
                (CSSWideKeyword::Revert, true, false)
                | (CSSWideKeyword::RevertLayer, true, false)
                | (CSSWideKeyword::RevertRule, true, false)
                | (CSSWideKeyword::Inherit, _, false)
                | (CSSWideKeyword::Unset, true, false) => {
                    match inherited.get(registration, name) {
                        Some(value) => {
                            context.builder.substitution_functions.insert_var(
                                registration,
                                name,
                                value.clone(),
                            );
                        },
                        None => {
                            context
                                .builder
                                .substitution_functions
                                .remove_var(registration, name);
                        },
                    };
                },
            }
            return;
        }
    }

    match kind {
        SubstitutionFunctionKind::Var => {
            let value = match substitution.into_value(url_data, registration, context) {
                Ok(v) => v,
                Err(()) => {
                    handle_invalid_at_computed_value_time(name, registration, context);
                    return;
                },
            };
            context
                .builder
                .substitution_functions
                .insert_var(registration, name, value);
        },
        SubstitutionFunctionKind::Attr => {
            let mut value = ComputedRegisteredValue::universal(Arc::new(VariableValue::new(
                substitution.css.into_owned(),
                url_data,
                substitution.first_token_type,
                substitution.last_token_type,
            )));
            value.attr_tainted |= substitution.attr_tainted;
            context
                .builder
                .substitution_functions
                .insert_attr(name, value);
        },
        SubstitutionFunctionKind::Env => unreachable!("Kind cannot be env."),
    }
}

#[derive(Default, Debug)]
struct Substitution<'a> {
    css: Cow<'a, str>,
    first_token_type: TokenSerializationType,
    last_token_type: TokenSerializationType,
    attr_tainted: bool,
}

impl<'a> Substitution<'a> {
    fn from_value(v: VariableValue, attr_tainted: bool) -> Self {
        Substitution {
            css: v.css.into(),
            first_token_type: v.first_token_type,
            last_token_type: v.last_token_type,
            attr_tainted,
        }
    }

    fn into_value(
        self,
        url_data: &UrlExtraData,
        registration: &PropertyDescriptors,
        computed_context: &computed::Context,
    ) -> Result<ComputedRegisteredValue, ()> {
        if registration.is_universal() {
            let mut value = ComputedRegisteredValue::universal(Arc::new(VariableValue::new(
                self.css.into_owned(),
                url_data,
                self.first_token_type,
                self.last_token_type,
            )));
            value.attr_tainted |= self.attr_tainted;
            return Ok(value);
        }
        let taint = if self.attr_tainted {
            // Per spec: substitution value of an arbitrary substitution function is
            // attr()-tainted as a whole if any attr()-tainted values were involved
            // in creating that substitution value.
            // https://drafts.csswg.org/css-values-5/#attr-security
            AttrTaint::new_fully_tainted(self.css.len())
        } else {
            AttrTaint::default()
        };
        let mut v = compute_value(&self.css, url_data, registration, computed_context, taint)?;
        v.attr_tainted |= self.attr_tainted;
        Ok(v)
    }

    fn new(
        css: Cow<'a, str>,
        first_token_type: TokenSerializationType,
        last_token_type: TokenSerializationType,
        attr_tainted: bool,
    ) -> Self {
        Self {
            css,
            first_token_type,
            last_token_type,
            attr_tainted,
        }
    }
}

/// Result of var(), env(), and attr() substitution.
#[derive(Debug)]
pub struct SubstitutionResult<'a> {
    /// The resolved CSS string after substitution.
    pub css: Cow<'a, str>,
    /// Regions in the `css` string that are attr()-tainted, if any.
    pub attr_taint: AttrTaint,
}

fn compute_value(
    css: &str,
    url_data: &UrlExtraData,
    registration: &PropertyDescriptors,
    computed_context: &computed::Context,
    attr_taint: AttrTaint,
) -> Result<ComputedRegisteredValue, ()> {
    debug_assert!(!registration.is_universal());

    let mut input = ParserInput::new(&css);
    let mut input = Parser::new(&mut input);

    SpecifiedRegisteredValue::compute(
        &mut input,
        registration,
        None,
        url_data,
        computed_context,
        AllowComputationallyDependent::Yes,
        attr_taint,
    )
}

/// Removes the named registered custom property and inserts its uncomputed initial value.
pub(crate) fn remove_and_insert_initial_value(
    name: &Name,
    registration: &PropertyDescriptors,
    substitution_functions: &mut ComputedSubstitutionFunctions,
) {
    substitution_functions.remove_var(registration, name);
    if let Some(ref initial_value) = registration.initial_value {
        let value = ComputedRegisteredValue::universal(Arc::clone(initial_value));
        substitution_functions.insert_var(registration, name, value);
    }
}

fn do_substitute_chunk<'a>(
    css: &'a str,
    start: usize,
    end: usize,
    first_token_type: TokenSerializationType,
    last_token_type: TokenSerializationType,
    url_data: &UrlExtraData,
    substitution_functions: &'a ComputedSubstitutionFunctions,
    stylist: &Stylist,
    computed_context: &computed::Context,
    references: &'a [SubstitutionFunctionReference],
    attribute_tracker: &mut AttributeTracker,
    seen: &mut SmallVec<[&'a Name; 8]>,
    mut attr_taint: Option<&mut AttrTaint>,
) -> Result<Substitution<'a>, ()> {
    if start == end {
        // Empty string. Easy.
        return Ok(Substitution::default());
    }
    // Easy case: no references involved.
    if references.is_empty() {
        let result = &css[start..end];
        return Ok(Substitution::new(
            Cow::Borrowed(result),
            first_token_type,
            last_token_type,
            Default::default(),
        ));
    }

    let mut substituted = ComputedValue::empty(url_data);
    let mut next_token_type = first_token_type;
    let mut cur_pos = start;
    let mut attr_tainted = false;
    let mut references = references.iter();
    while let Some(reference) = references.next() {
        if reference.start != cur_pos {
            substituted.push(
                &css[cur_pos..reference.start],
                next_token_type,
                reference.prev_token_type,
                /* attr_taint */ None,
            )?;
        }

        let substitution = substitute_one_reference(
            css,
            url_data,
            substitution_functions,
            reference,
            stylist,
            computed_context,
            attribute_tracker,
            seen,
        )?;

        // Optimize the property: var(--...) case to avoid allocating at all.
        if reference.start == start && reference.end == end {
            if let Some(taint) = attr_taint.filter(|_| substitution.attr_tainted) {
                taint.push(start, substitution.css.len());
            }
            return Ok(substitution);
        }

        substituted.push(
            &substitution.css,
            substitution.first_token_type,
            substitution.last_token_type,
            attr_taint
                .as_deref_mut()
                .filter(|_| substitution.attr_tainted),
        )?;
        attr_tainted |= substitution.attr_tainted;
        next_token_type = reference.next_token_type;
        cur_pos = reference.end;
    }
    // Push the rest of the value if needed.
    if cur_pos != end {
        substituted.push(
            &css[cur_pos..end],
            next_token_type,
            last_token_type,
            /* attr_taint */ None,
        )?;
    }
    Ok(Substitution::from_value(substituted, attr_tainted))
}

fn quoted_css_string(src: &str) -> String {
    let mut dest = String::with_capacity(src.len() + 2);
    cssparser::serialize_string(src, &mut dest).unwrap();
    dest
}

fn substitute_one_reference<'a>(
    css: &'a str,
    url_data: &UrlExtraData,
    substitution_functions: &'a ComputedSubstitutionFunctions,
    reference: &'a SubstitutionFunctionReference,
    stylist: &Stylist,
    computed_context: &computed::Context,
    attribute_tracker: &mut AttributeTracker,
    seen: &mut SmallVec<[&'a Name; 8]>,
) -> Result<Substitution<'a>, ()> {
    let simple_attr_subst = |s: &str| {
        Some(Substitution::new(
            Cow::Owned(quoted_css_string(s)),
            TokenSerializationType::Nothing,
            TokenSerializationType::Nothing,
            /* attr_tainted */ true,
        ))
    };
    let substitution: Option<_> = match reference.substitution_kind {
        SubstitutionFunctionKind::Var => {
            let registration = stylist.get_custom_property_registration(&reference.name);
            match substitution_functions.get_var(registration, &reference.name) {
                None => None,
                // If the referenced value is itself still unresolved (i.e. it has references), we
                // are substituting against a partially-resolved map -- this happens while applying
                // a prioritary property in the middle of custom-property resolution. Resolve it
                // recursively, guarding against cycles (which the resolver will also remove, but it
                // may not have gotten to this variable yet).
                Some(v) => match v.as_universal() {
                    Some(u) if u.has_references() => {
                        if seen.contains(&&reference.name) {
                            // Cycle: the primary is guaranteed-invalid, so fall through to the
                            // fallback (if any), like any other invalid primary.
                            None
                        } else {
                            seen.push(&reference.name);
                            let result = substitute_internal(
                                u,
                                substitution_functions,
                                stylist,
                                computed_context,
                                attribute_tracker,
                                seen,
                                /* attr_taint */ None,
                            );
                            seen.pop();
                            match result {
                                Ok(mut substitution) => {
                                    substitution.attr_tainted |= v.attr_tainted;
                                    Some(substitution)
                                },
                                // The primary couldn't be resolved (invalid); use the fallback.
                                Err(()) => None,
                            }
                        }
                    },
                    _ => Some(Substitution::from_value(
                        v.to_variable_value(),
                        v.attr_tainted,
                    )),
                },
            }
        },
        SubstitutionFunctionKind::Env => {
            let device = stylist.device();
            device
                .environment()
                .get(&reference.name, device, url_data)
                .map(|v| Substitution::from_value(v, /* attr_tainted */ false))
        },
        // https://drafts.csswg.org/css-values-5/#attr-substitution
        SubstitutionFunctionKind::Attr => {
            let namespace = match reference.attribute_data.namespace {
                ParsedNamespace::Known(ref ns) => Some(ns),
                ParsedNamespace::Unknown => None,
            };
            namespace
                .and_then(|namespace| {
                    LocalName::with_name(&reference.name, |local_name| {
                        attribute_tracker.query(local_name, namespace)
                    })
                })
                .map_or_else(
                    || {
                        // Special case when fallback and <attr-type> are omitted.
                        // See FAILURE: https://drafts.csswg.org/css-values-5/#attr-substitution
                        if reference.fallback.is_none()
                            && reference.attribute_data.kind == AttributeType::None
                        {
                            simple_attr_subst("")
                        } else {
                            None
                        }
                    },
                    |attr| {
                        let attr = if let AttributeType::Type(_) = &reference.attribute_data.kind {
                            // If we're evaluating a container query, we haven't run the cascade
                            // and populated substitution_functions.attributes, so we can't do the
                            // get_attr() lookup here.
                            // TODO: This means chained attr() references will not work reliably in
                            // container style queries:
                            // https://bugzilla.mozilla.org/show_bug.cgi?id=2028861
                            if computed_context.in_container_query {
                                attr
                            } else {
                                substitution_functions
                                    .get_attr(&reference.name)
                                    .map(|v| v.to_variable_value())?
                                    .css
                            }
                        } else {
                            attr
                        };
                        let mut input = ParserInput::new(&attr);
                        let mut parser = Parser::new(&mut input);
                        match &reference.attribute_data.kind {
                            AttributeType::Unit(unit) => {
                                let css = {
                                    // Verify that attribute data is a <number-token>.
                                    parser.expect_number().ok()?;
                                    let mut s = attr.clone();
                                    s.push_str(unit.as_ref());
                                    s
                                };
                                let serialization = match unit {
                                    AttrUnit::Number => TokenSerializationType::Number,
                                    AttrUnit::Percentage => TokenSerializationType::Percentage,
                                    _ => TokenSerializationType::Dimension,
                                };
                                let value =
                                    ComputedValue::new(css, url_data, serialization, serialization);
                                Some(Substitution::from_value(
                                    value, /* attr_tainted */ true,
                                ))
                            },
                            AttributeType::Type(syntax) => {
                                let value = SpecifiedRegisteredValue::parse(
                                    &mut parser,
                                    &syntax,
                                    url_data,
                                    None,
                                    AllowComputationallyDependent::Yes,
                                    AttrTaint::default(),
                                )
                                .ok()?;
                                let value = value.to_variable_value();
                                Some(Substitution::from_value(
                                    value, /* attr_tainted */ true,
                                ))
                            },
                            AttributeType::RawString | AttributeType::None => {
                                simple_attr_subst(&attr)
                            },
                            AttributeType::Invalid => None,
                        }
                    },
                )
        },
    };

    if let Some(s) = substitution {
        return Ok(s);
    }

    let Some(ref fallback) = reference.fallback else {
        return Err(());
    };

    do_substitute_chunk(
        css,
        fallback.start.get(),
        reference.end - 1, // Skip the closing parenthesis of the reference value.
        fallback.first_token_type,
        fallback.last_token_type,
        url_data,
        substitution_functions,
        stylist,
        computed_context,
        &fallback.references.refs,
        attribute_tracker,
        seen,
        /* attr_taint */ None,
    )
}

/// Replace `var()`, `env()`, and `attr()` functions. Return `Err(..)` for invalid at computed time.
fn substitute_internal<'a>(
    variable_value: &'a VariableValue,
    substitution_functions: &'a ComputedSubstitutionFunctions,
    stylist: &Stylist,
    computed_context: &computed::Context,
    attribute_tracker: &mut AttributeTracker,
    seen: &mut SmallVec<[&'a Name; 8]>,
    mut attr_taint: Option<&mut AttrTaint>,
) -> Result<Substitution<'a>, ()> {
    do_substitute_chunk(
        &variable_value.css,
        /* start = */ 0,
        /* end = */ variable_value.css.len(),
        variable_value.first_token_type,
        variable_value.last_token_type,
        &variable_value.url_data,
        substitution_functions,
        stylist,
        computed_context,
        &variable_value.references.refs,
        attribute_tracker,
        seen,
        attr_taint.as_deref_mut(),
    )
}

/// Replace var(), env(), and attr() functions, returning the resulting CSS string.
pub fn substitute<'a>(
    variable_value: &'a VariableValue,
    substitution_functions: &'a ComputedSubstitutionFunctions,
    stylist: &Stylist,
    computed_context: &computed::Context,
    attribute_tracker: &mut AttributeTracker,
) -> Result<SubstitutionResult<'a>, ()> {
    debug_assert!(variable_value.has_references());
    let mut attr_taint = AttrTaint::default();
    let v = substitute_internal(
        variable_value,
        substitution_functions,
        stylist,
        computed_context,
        attribute_tracker,
        &mut SmallVec::new(),
        Some(&mut attr_taint),
    )?;
    Ok(SubstitutionResult {
        css: v.css,
        attr_taint,
    })
}
