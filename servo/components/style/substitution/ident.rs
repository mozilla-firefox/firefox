/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The `ident()` arbitrary substitution function.
//! https://drafts.csswg.org/css-values-5/#ident

use super::{
    ArbitraryDeclarationValue, Substitution, SubstitutionArgument, SubstitutionParserContext,
    SubstitutionReplacementContext,
};
use crate::custom_properties::VariableValue;
use crate::derives::*;
use crate::parser::{Parse, ParserContext};
use crate::values::computed::ToComputedValue;
use crate::values::{AtomIdent, specified};
use cssparser::{Delimiter, Parser, TokenSerializationType, serialize_identifier};
use std::fmt::Write;
use style_traits::{ParseError, StyleParseErrorKind};

/// <ident-arg> = <string> | <integer> | <ident>
/// https://drafts.csswg.org/css-values-5/#typedef-ident-arg
#[derive(Clone, Debug, MallocSizeOf, Parse, PartialEq, ToShmem)]
pub enum IdentArg {
    /// `<string>` component
    String(crate::OwnedStr),
    /// `<integer>` component
    Integer(specified::Integer),
    /// `<ident>` component
    Ident(AtomIdent),
}

/// One or more `<ident-arg>` values.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct IdentArgs(pub crate::OwnedSlice<IdentArg>);

impl Parse for IdentArgs {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let mut args = vec![];
        while let Ok(arg) = input.try_parse(|input| IdentArg::parse(context, input)) {
            args.push(arg);
        }
        if args.is_empty() {
            return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
        }
        Ok(Self(args.into()))
    }
}

/// The data specific to an `ident()` reference.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct IdentFunction {
    /// The argument containing the ident's components.
    pub arg: SubstitutionArgument<IdentArgs>,
}

impl IdentFunction {
    /// Parses the `ident()` function arguments according to its argument grammar and syntax:
    /// - <ident-args> = ident(<declaration-value>)
    /// - ident() = ident(<ident-arg>+)
    pub fn parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        Ok(Self {
            arg: SubstitutionArgument::try_parse(input, context, Delimiter::None)?,
        })
    }

    /// Replaces the `ident()` function with its substitution value.
    /// https://drafts.csswg.org/css-values-5/#replace-an-ident-function
    pub fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        let args = self.arg.substitute_and_parse(source, context);
        let attr_tainted = args.attr_tainted;
        let mut result = String::new();

        for piece in args.value.ok_or(())?.0.iter() {
            match piece {
                IdentArg::String(s) => result.push_str(s),
                IdentArg::Integer(i) => {
                    write!(result, "{}", i.to_computed_value(context.computed_context)).unwrap()
                },
                IdentArg::Ident(i) => i.0.with_str(|i| result.push_str(i)),
            }
        }

        // "If the result can't be represented as an ident (for example, the empty string),
        // instead return the guaranteed-invalid value."
        if result.is_empty() {
            return Err(());
        }

        // Serialize in order to ensure the substituted identifier is properly escaped.
        let mut serialized = String::new();
        serialize_identifier(&result, &mut serialized).map_err(|_| ())?;
        Ok(Substitution::from_value(
            VariableValue::new(
                serialized,
                &source.url_data,
                TokenSerializationType::Ident,
                TokenSerializationType::Ident,
            ),
            attr_tainted,
        ))
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    pub fn for_each_unparsed_value<F>(&self, mut f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        if let SubstitutionArgument::Unparsed(ref arg) = self.arg {
            f(arg);
        }
    }
}
