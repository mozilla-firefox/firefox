/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The `env()` arbitrary substitution function.
//! https://drafts.csswg.org/css-env-1/#funcdef-env

use super::{
    ArbitraryDeclarationValue, ParsedArgument, Substitution, SubstitutionArgument,
    SubstitutionParserContext, SubstitutionReplacementContext, parse_optional_fallback,
};
use crate::custom_properties::{Name, VariableValue};
use crate::device::Device;
use crate::parser::{Parse, ParserContext};
use crate::stylesheets::UrlExtraData;
use crate::{Atom, derives::*};
use cssparser::{Delimiter, Parser};
use style_traits::ParseError;

/// Environment variable name.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct EnvName(pub Name);

impl Parse for EnvName {
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        // TODO(emilio): For env() this should be <custom-ident> per spec, but no other browser does
        // that, see https://github.com/w3c/csswg-drafts/issues/3262.
        Ok(EnvName(Atom::from(input.expect_ident()?.as_ref())))
    }
}

/// Data specific to an `env()` function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct EnvFunction {
    /// The referenced environment variable name.
    pub name: SubstitutionArgument<EnvName>,
    /// The fallback for this function, if any.
    pub fallback: Option<ArbitraryDeclarationValue>,
}

impl EnvFunction {
    /// Parses the `env()` function arguments according to its argument grammar and syntax:
    /// - <env-args> = env(<declaration-value>, <declaration-value>?)
    /// - env() = env(<custom-ident> <integer [0,∞]>*, <declaration-value>?)
    ///
    /// TODO(Bug 2081127) - Support the optional index arguments.
    pub fn parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        let name = SubstitutionArgument::try_parse(input, context, Delimiter::Comma)?;
        let fallback = parse_optional_fallback(input, context)?;
        Ok(Self { name, fallback })
    }

    /// Replaces the `env()` function with its substitution value.
    /// https://drafts.csswg.org/css-env-1/#substitute-an-env
    pub fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        let name = self.replace_name(source, context);
        let device = context.stylist.device();
        let mut substitution = name
            .value
            .and_then(|name| {
                device
                    .environment()
                    .get(&name.0, device, &context.parser_context(&source.url_data))
                    .map(|v| Substitution::from_value(v, /* attr_tainted */ false))
            })
            .ok_or(())
            .or_else(|_| {
                context.substitute_declaration_value(self.fallback.as_ref().ok_or(())?, source)
            })?;
        substitution.attr_tainted |= name.attr_tainted;
        Ok(substitution)
    }

    /// Returns the name, substituting and parsing it first if it has references.
    pub fn replace_name<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> ParsedArgument<'a, EnvName> {
        self.name.substitute_and_parse(source, context)
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    pub fn for_each_unparsed_value<F>(&self, mut f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        if let SubstitutionArgument::Unparsed(ref arg) = self.name {
            f(arg);
        }
        if let Some(ref fallback) = self.fallback {
            f(fallback);
        }
    }
}

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

fn get_submit_string(device: &Device, url_data: &UrlExtraData) -> VariableValue {
    VariableValue::string(device.submit_string(), url_data)
}

static CHROME_ENVIRONMENT_VARIABLES: [EnvironmentVariable; 10] = [
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
    make_variable!(atom!("-moz-submit-string"), get_submit_string),
];

impl CssEnvironment {
    /// Get an env() variable, either custom or not.
    #[inline]
    pub fn get(
        &self,
        name: &Atom,
        device: &Device,
        parser_context: &ParserContext,
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
            let mut parser = cssparser::Parser::new(param.value.0.as_ref());
            // need to carry around full variable value https://bugzilla.mozilla.org/show_bug.cgi?id=2028998
            return VariableValue::parse(parser_context, &mut parser).ok();
        }

        if let Some(var) = ENVIRONMENT_VARIABLES.iter().find(|var| var.name == *name) {
            return Some((var.evaluator)(device, parser_context.url_data));
        }
        if !parser_context.chrome_rules_enabled() {
            return None;
        }
        let var = CHROME_ENVIRONMENT_VARIABLES
            .iter()
            .find(|var| var.name == *name)?;
        Some((var.evaluator)(device, parser_context.url_data))
    }
}
