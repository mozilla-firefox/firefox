/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The `random-item()` arbitrary substitution function.
//! https://drafts.csswg.org/css-values-5/#funcdef-random-item

use super::{
    ArbitraryDeclarationValue, ParsedArgument, Substitution, SubstitutionArgument,
    SubstitutionParserContext, SubstitutionReplacementContext,
};
use crate::custom_properties::VariableValue;
use crate::derives::*;
use crate::values::computed::ToComputedValue;
use crate::values::specified::random::{RandomFunctionKind, RandomKey};
use crate::values::{calc_clamp_random_base_value, calc_random_integer};
use cssparser::{Delimiter, Parser};
use style_traits::ParseError;

/// The data specific to a `random-item()` reference.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct RandomItemFunction {
    /// The <random-key> for the function.
    pub random_key: SubstitutionArgument<RandomKey>,
    /// The options to randomly select from.
    pub options: crate::OwnedSlice<ArbitraryDeclarationValue>,
    /// The index of the function in the property declaration amongst all `random-item()` functions,
    /// used for `property-index-scoped` keys. This is stored here at parse time since different
    /// functions might resolve the `random_key` at different times (at parse time for keys without
    /// references, and at substitution time otherwise). This also ensures that the selected option
    /// is consistent between cycle checking and the actual substitution.
    pub property_index: i32,
}

impl RandomItemFunction {
    /// Parses the `random-item()` function arguments according to its argument grammar and syntax:
    /// - <random-item-args> = random-item(<declaration-value>, [<declaration-value>?]#)
    /// - random-item() = random-item(<random-key>, [<declaration-value>?]#)
    pub fn parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        let property_index = context
            .early_parser_context
            .property_declaration_context
            .increment_random_function_count(RandomFunctionKind::RandomItem);
        let random_key = SubstitutionArgument::try_parse_with(
            input,
            context,
            Delimiter::Comma,
            |context, input| RandomKey::parse(context, input, property_index),
        )?;
        input.expect_comma()?;
        let options = input.parse_comma_separated(|input| {
            let (value, _) = ArbitraryDeclarationValue::parse_free_form(
                input,
                context,
                Delimiter::Comma,
                /* is_non_strict */ false,
            )?;
            Ok(value)
        })?;
        Ok(Self {
            random_key,
            options: options.into(),
            property_index,
        })
    }

    /// Replaces the `random-item()` function with its substitution value.
    /// https://drafts.csswg.org/css-values-5/#replace-a-random-item-function
    pub fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        let random_key = self.replace_random_key(source, context);
        let option = random_key
            .value
            .and_then(|random_key| self.resolve_option(&*random_key, context))
            .ok_or(())?;
        let mut substitution = context.substitute_declaration_value(option, source)?;
        substitution.attr_tainted |= random_key.attr_tainted;
        Ok(substitution)
    }

    /// Returns the random key, substituting and parsing it first if it has references.
    pub fn replace_random_key<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> ParsedArgument<'a, RandomKey> {
        self.random_key
            .substitute_and_parse_with(source, context, |context, input| {
                RandomKey::parse(context, input, self.property_index)
            })
    }

    /// Resolves the option for the given `random_key`.
    pub fn resolve_option(
        &self,
        random_key: &RandomKey,
        context: &SubstitutionReplacementContext,
    ) -> Option<&ArbitraryDeclarationValue> {
        let random_base_value =
            calc_clamp_random_base_value(random_key.to_computed_value(context.computed_context));
        let random_index =
            calc_random_integer(random_base_value, self.options.len() as f32) as usize;
        self.options.get(random_index)
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    pub fn for_each_unparsed_value<F>(&self, mut f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        if let SubstitutionArgument::Unparsed(ref arg) = self.random_key {
            f(arg);
        }
        self.options.iter().for_each(f);
    }
}
