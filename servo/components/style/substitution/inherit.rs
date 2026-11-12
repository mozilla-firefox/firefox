/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The `inherit()` arbitrary substitution function.
//! https://drafts.csswg.org/css-values-5/#inherit-notation

use super::{
    ArbitraryDeclarationValue, CustomPropertyName, ParsedArgument, Substitution,
    SubstitutionArgument, SubstitutionParserContext, SubstitutionReplacementContext,
    parse_optional_fallback,
};
use crate::computed_value_flags::ComputedValueFlags;
use crate::custom_properties::VariableValue;
use crate::derives::*;
use crate::properties_and_values::rule::Descriptors as PropertyDescriptors;
use crate::properties_and_values::value::ComputedValue as ComputedRegisteredValue;
use cssparser::{Delimiter, Parser};
use style_traits::ParseError;

/// The data specific to an `inherit()` reference.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct InheritFunction {
    /// The inherited custom property name.
    pub name: SubstitutionArgument<CustomPropertyName>,
    /// The fallback for this function, if any.
    pub fallback: Option<ArbitraryDeclarationValue>,
}

impl InheritFunction {
    /// Parses the `inherit()` function arguments according to its argument grammar and syntax:
    /// - <inherit-args> = inherit(<declaration-value>, <declaration-value>?)
    /// - inherit() = inherit(<custom-property-name>, <declaration-value>?)
    pub fn parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        let name = SubstitutionArgument::try_parse(input, context, Delimiter::Comma)?;
        let fallback = parse_optional_fallback(input, context)?;
        Ok(Self { name, fallback })
    }

    /// Replaces the `inherit()` function with its substitution value.
    /// https://drafts.csswg.org/css-values-5/#replace-an-inherit-function
    pub fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        let name = self.replace_name(source, context);
        let parent_value = name.value.and_then(|name| {
            let (registration, parent_value) = Self::resolve_parent_custom_property(context, &name);
            if !registration.inherits() {
                context
                    .computed_context
                    .style()
                    .add_flags(ComputedValueFlags::INHERITS_RESET_STYLE);
            }
            parent_value
        });
        let mut substitution = match parent_value {
            Some(value) => Substitution::from_value(value.to_variable_value(), value.attr_tainted),
            None => {
                context.substitute_declaration_value(self.fallback.as_ref().ok_or(())?, source)?
            },
        };
        substitution.attr_tainted |= name.attr_tainted;
        Ok(substitution)
    }

    /// Returns the name, substituting and parsing it first if it has references.
    pub fn replace_name<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> ParsedArgument<'a, CustomPropertyName> {
        self.name.substitute_and_parse(source, context)
    }

    /// Looks up the given custom property name on the parent's computed styles.
    pub fn resolve_parent_custom_property<'b>(
        context: &SubstitutionReplacementContext<'_, 'b, '_, '_>,
        name: &CustomPropertyName,
    ) -> (&'b PropertyDescriptors, Option<&'b ComputedRegisteredValue>) {
        let registration = context.stylist.get_custom_property_registration(&name.0);
        let parent_value = context
            .computed_context
            .inherited_custom_properties()
            .get(registration, &name.0);
        (registration, parent_value)
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    pub fn for_each_unparsed_value<F>(&self, mut f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        if let SubstitutionArgument::Unparsed(arg) = &self.name {
            f(arg);
        }
        if let Some(ref fallback) = self.fallback {
            f(fallback);
        }
    }
}
