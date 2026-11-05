/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Support for [custom properties for cascading variables][custom].
//!
//! [custom]: https://drafts.csswg.org/css-variables/

use crate::custom_properties_map::{CustomPropertiesMap, OwnMap};
use crate::dom::AttributeTracker;
use crate::parser::ParserContext;
use crate::properties::{CSSWideKeyword, PrioritaryPropertyId, PropertyIdRef};
use crate::properties_and_values::{
    rule::Descriptors as PropertyDescriptors,
    value::{
        AllowComputationallyDependent, ComputedValue as ComputedRegisteredValue,
        SpecifiedValue as SpecifiedRegisteredValue,
    },
};
use crate::stylesheets::UrlExtraData;
use crate::stylesheets::container_rule::AttrReferenceSet;
use crate::stylist::Stylist;
use crate::substitution::{
    AttrTaint, ReferenceFlags, References, SubstitutionArgument, SubstitutionFunction,
    SubstitutionFunctionKind, SubstitutionFunctionReference, SubstitutionParserContext,
    SubstitutionReplacementContext, parse_declaration_value,
};
use crate::typed_om::{
    ToTyped, TypedValue, UnparsedSegment, UnparsedValue, VariableReferenceValue,
};
use crate::values::computed;
use crate::{Atom, derives::*};
use cssparser::{CowRcStr, Delimiter, Parser, Token, TokenSerializationType};
use servo_arc::Arc;
use std::fmt::{self, Write};
use std::ops::{Index, IndexMut};
use style_traits::{CssString, CssWriter, ParseError, ToCss};
use thin_vec::ThinVec;

/// A custom property name is just an `Atom`.
///
/// Note that this does not include the `--` prefix
pub type Name = Atom;

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

    /// The serialization type of the first token in `css`.
    pub first_token_type: TokenSerializationType,
    /// The serialization type of the last token in `css`.
    pub last_token_type: TokenSerializationType,

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
    property_id: Option<PropertyIdRef>,
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
        property_id,
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

        let SubstitutionFunction::Var(var) = &reference.function else {
            return Err(());
        };
        let (fallback, has_fallback) = if let Some(fallback) = var.fallback.as_ref() {
            debug_assert!(fallback.start <= fallback.end);

            (
                reify_variable_value_range(
                    css,
                    references,
                    reference_index,
                    fallback.start.get(),
                    fallback.end.get(),
                )?,
                true,
            )
        } else {
            (ThinVec::new(), false)
        };

        let SubstitutionArgument::Parsed(name) = &var.name else {
            return Err(());
        };
        values.push(UnparsedSegment::VariableReference(VariableReferenceValue {
            variable: CssString::from(format!("--{}", name.0)),
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

impl VariableValue {
    /// Creates an empty value.
    pub(crate) fn empty(url_data: &UrlExtraData) -> Self {
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

    /// Appends `css` to the value, inserting a comment where needed to keep tokens separate.
    pub(crate) fn push(
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
    pub fn parse(parser_context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let mut references = References::default();
        let start_position = input.position();
        let mut context = SubstitutionParserContext::new(parser_context, start_position);
        let (first_token_type, last_token_type) =
            parse_declaration_value(input, &mut context, &mut references, Delimiter::None)?;
        let mut css = input
            .slice_from(start_position)
            .trim_ascii_start()
            .to_owned();
        if !context.missing_closing_characters.is_empty() {
            // Unescaped backslash at EOF in a quoted string is ignored.
            if css.ends_with("\\")
                && matches!(
                    context.missing_closing_characters.as_bytes()[0],
                    b'"' | b'\''
                )
            {
                css.pop();
            }
            css.push_str(&context.missing_closing_characters);
        }

        css.truncate(css.trim_ascii_end().len());
        css.shrink_to_fit();
        references.refs.shrink_to_fit();

        Ok(Self {
            css,
            url_data: parser_context.url_data.clone(),
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
    pub(crate) fn integer(number: i32, url_data: &UrlExtraData) -> Self {
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
    pub(crate) fn ident(ident: &'static str, url_data: &UrlExtraData) -> Self {
        Self::from_token(Token::Ident(ident.into()), url_data)
    }

    pub(crate) fn string(s: &str, url_data: &UrlExtraData) -> Self {
        Self::from_token(Token::QuotedString(CowRcStr::from(s)), url_data)
    }

    /// Create VariableValue from a float amount of CSS pixels.
    pub(crate) fn pixels(number: f32, url_data: &UrlExtraData) -> Self {
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
    pub(crate) fn int_ms(number: i32, url_data: &UrlExtraData) -> Self {
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
    pub(crate) fn int_pixels(number: i32, url_data: &UrlExtraData) -> Self {
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
        fn collect(refs: &References, references: &mut AttrReferenceSet) {
            for r in &refs.refs {
                if let SubstitutionFunction::Attr(attr) = &r.function {
                    match &attr.arg {
                        SubstitutionArgument::Unparsed(_) => {
                            references.is_fully_parsed = false;
                        },
                        SubstitutionArgument::Parsed(arg) => {
                            // For a non-ascii-lowercase attribute, whether we match
                            // case-sensitively depends on whether the element we're
                            // matching against is an HTML element in an HTML document.
                            // So to simplify invalidation we collect both potential
                            // references here.
                            let name = &arg.name.name;
                            references.names.insert(name.clone().into());
                            let lowercase = name.to_ascii_lowercase();
                            if *lowercase != **name {
                                references.names.insert(Atom::from(lowercase).into());
                            }
                        },
                        SubstitutionArgument::Invalid => {},
                    }
                }
                r.function
                    .for_each_unparsed_value(|value| collect(&value.references, references));
            }
        }
        collect(&self.references, references);
    }
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
        } else if let Some(ref initial_value) = registration.initial_value
            && let Ok(initial_value) = compute_value(
                &initial_value.css,
                &initial_value.url_data,
                registration,
                context,
                AttrTaint::default(),
                Some(PropertyIdRef::from(name)),
            )
        {
            context
                .builder
                .substitution_functions
                .insert_var(registration, name, initial_value);
            return;
        }
    }
    context
        .builder
        .substitution_functions
        .remove_var(registration, name);
}

/// Replace substitution functions in a pre-existing variable value.
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
    let registration = stylist.get_custom_property_registration(name);
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
    let substitution = SubstitutionReplacementContext::new(
        Some(PropertyIdRef::from(name)),
        &context.builder.substitution_functions,
        stylist,
        context,
        attribute_tracker,
    )
    .substitute_variable_value(value, None);

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
            let mut input = Parser::new(css);
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
            let value = match substitution.into_value(
                url_data,
                registration,
                context,
                Some(PropertyIdRef::from(name)),
            ) {
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
        SubstitutionFunctionKind::Env | SubstitutionFunctionKind::Ident => {
            unreachable!("Kind must be `var` or `attr`.")
        },
    }
}

/// Computes `css` against the registration of a custom property.
pub fn compute_value(
    css: &str,
    url_data: &UrlExtraData,
    registration: &PropertyDescriptors,
    computed_context: &computed::Context,
    attr_taint: AttrTaint,
    property_id: Option<PropertyIdRef>,
) -> Result<ComputedRegisteredValue, ()> {
    debug_assert!(!registration.is_universal());

    let mut input = Parser::new(css);
    SpecifiedRegisteredValue::compute(
        &mut input,
        registration,
        url_data,
        computed_context,
        AllowComputationallyDependent::Yes,
        attr_taint,
        property_id,
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
