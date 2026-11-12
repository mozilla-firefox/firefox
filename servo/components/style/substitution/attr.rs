/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The `attr()` arbitrary substitution function.
//! https://drafts.csswg.org/css-values-5/#funcdef-attr

use super::{
    ArbitraryDeclarationValue, AttrTaint, ParsedArgument, Substitution, SubstitutionArgument,
    SubstitutionParserContext, SubstitutionReplacementContext, parse_optional_fallback,
};
use crate::custom_properties::{ComputedValue, Name, VariableValue};
use crate::parser::{Parse, ParserContext};
use crate::properties_and_values::{
    syntax::Descriptor as SyntaxDescriptor,
    value::{
        AllowComputationallyDependent, ComputedValue as ComputedRegisteredValue,
        SpecifiedValue as SpecifiedRegisteredValue,
    },
};
use crate::stylesheets::UrlExtraData;
use crate::values::generics::calc::SortKey as AttrUnit;
use crate::values::specified::ParsedNamespace;
use crate::{Atom, FxHashMap, LocalName, Namespace, Prefix, derives::*};
use cssparser::{Delimiter, Parser, Token, TokenSerializationType};
use servo_arc::Arc;
use std::borrow::Cow;
use style_traits::{ParseError, StyleParseErrorKind};

impl LocalName {
    #[cfg(feature = "gecko")]
    fn with_name<R>(name: &Name, callback: impl FnOnce(&Self) -> R) -> R {
        callback(Self::cast(name))
    }

    #[cfg(feature = "servo")]
    fn with_name<'a, R>(name: &'a Name, callback: impl FnOnce(&Self) -> R) -> R {
        callback(&name.as_str().into())
    }
}

impl From<Name> for LocalName {
    #[cfg(feature = "gecko")]
    fn from(name: Name) -> Self {
        Self::new(name)
    }

    #[cfg(feature = "servo")]
    fn from(name: Name) -> Self {
        name.as_str().into()
    }
}

/// The `<attr-type>` of an `attr()` function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
enum AttrType {
    /// No type was provided.
    None,
    /// Treat the attribute value as a <string>.
    RawString,
    /// Parse the attribute value as the given <syntax>.
    Type(SyntaxDescriptor),
    /// Parse the attribute value as a dimension with the given unit,
    /// or as a <number-token> if the unit is `number`.
    Unit(AttrUnit),
}

impl Parse for AttrType {
    /// Parse <attr-type> = type( <syntax> ) | raw-string | number | <attr-unit>.
    /// https://drafts.csswg.org/css-values-5/#attr-notation
    fn parse(_: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        Ok(input
            .try_parse(|input| {
                Ok(match input.next()? {
                    Token::Function(name) if name.eq_ignore_ascii_case("type") => {
                        Self::Type(input.parse_nested_block(SyntaxDescriptor::from_css_parser)?)
                    },
                    Token::Ident(ident) => {
                        if ident.eq_ignore_ascii_case("raw-string") {
                            Self::RawString
                        } else if let Ok(unit) = AttrUnit::from_ident(ident) {
                            Self::Unit(unit)
                        } else {
                            return Err(ParseError::custom(StyleParseErrorKind::UnexpectedIdent));
                        }
                    },
                    Token::Delim('%') => Self::Unit(AttrUnit::Percentage),
                    _ => return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError)),
                })
            })
            .unwrap_or(Self::None))
    }
}

/// The `<attr-name>` of an `attr()` function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttrName {
    /// The referenced attribute name.
    pub name: Name,
    namespace: ParsedNamespace,
}

impl AttrName {
    /// Attribute values may reference other substitution functions we may need to process.
    /// See step 6: https://drafts.csswg.org/css-values-5/#attr-substitution
    pub fn value_for_cycle_resolution(
        &self,
        context: &mut SubstitutionReplacementContext,
        url_data: &UrlExtraData,
    ) -> Result<ComputedRegisteredValue, ()> {
        let ParsedNamespace::Known(ref namespace) = self.namespace else {
            return Err(());
        };
        let attr = LocalName::with_name(&self.name, |local_name| {
            context
                .attribute_tracker
                .query(local_name, namespace)
                .ok_or(())
        })?;
        let parser_context = context.parser_context(url_data);
        let mut parser = Parser::new(&attr);
        // TODO(Bug 2021110): Support namespaced attributes in chained references.
        let value = VariableValue::parse(&parser_context, &mut parser).map_err(|_| ())?;
        Ok(ComputedRegisteredValue::universal(Arc::new(value)))
    }
}

impl Parse for AttrName {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let mut namespace = ParsedNamespace::Known(Namespace::default());
        if let Ok(ns) =
            input.try_parse(|input| ParsedNamespace::parse(&context.namespaces.prefixes, input))
        {
            namespace = ns;
            let prev = input.state();
            let is_ident = matches!(input.next_including_whitespace(), Ok(Token::Ident(_)));
            input.reset(&prev);
            if !is_ident {
                return Err(ParseError::custom(StyleParseErrorKind::UnspecifiedError));
            }
        }
        let name = Atom::from(input.expect_ident()?.as_ref());
        Ok(AttrName { name, namespace })
    }
}

/// The `<attr-name> <attr-type>?` argument of an `attr()` function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttrArgument {
    /// The referenced attribute name.
    pub name: AttrName,
    /// The type the attribute value is parsed as.
    ty: AttrType,
}

impl AttrArgument {
    /// Whether the attribute has a `type()`.
    pub fn has_type(&self) -> bool {
        matches!(self.ty, AttrType::Type(..))
    }
}

impl Parse for AttrArgument {
    fn parse(context: &ParserContext, input: &mut Parser) -> Result<Self, ParseError> {
        let name = AttrName::parse(context, input)?;
        let ty = AttrType::parse(context, input)?;
        Ok(AttrArgument { name, ty })
    }
}

/// Data specific to an `attr()` function.
#[derive(Clone, Debug, MallocSizeOf, PartialEq, ToShmem)]
pub struct AttrFunction {
    /// The referenced attribute name and optional type.
    pub arg: SubstitutionArgument<AttrArgument>,
    /// The fallback for this function, if any.
    pub fallback: Option<ArbitraryDeclarationValue>,
    /// The namespace prefixes in scope where the function was parsed.
    namespaces: Option<FxHashMap<Prefix, Namespace>>,
}

impl AttrFunction {
    /// Parses the `attr()` function arguments according to its argument grammar and syntax:
    /// - <attr-args> = attr(<declaration-value>, <declaration-value>?)
    /// - attr() = attr(<attr-name> <attr-type>? , <declaration-value>?)
    pub fn parse(
        input: &mut Parser,
        context: &mut SubstitutionParserContext,
    ) -> Result<Self, ParseError> {
        let arg = SubstitutionArgument::try_parse(input, context, Delimiter::Comma)?;
        let namespaces = match arg {
            SubstitutionArgument::Unparsed(..) => {
                Some(context.parser_context.namespaces.prefixes.clone())
            },
            SubstitutionArgument::Parsed(..) | SubstitutionArgument::Invalid => None,
        };
        let fallback = parse_optional_fallback(input, context)?;
        Ok(Self {
            arg,
            namespaces,
            fallback,
        })
    }

    /// Replaces the `attr()` function with its substitution value.
    /// https://drafts.csswg.org/css-values-5/#replace-an-attr-function
    pub fn replace<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> Result<Substitution<'a>, ()> {
        let arg = self.replace_arg(source, context);
        let is_type_omitted = arg
            .value
            .as_ref()
            .is_some_and(|arg| arg.ty == AttrType::None);
        let mut substitution = arg
            .value
            .and_then(|arg| {
                let ParsedNamespace::Known(ref namespace) = arg.name.namespace else {
                    return None;
                };
                let attr = LocalName::with_name(&arg.name.name, |local_name| {
                    context.attribute_tracker.query(local_name, namespace)
                })?;
                Some((arg, attr))
            })
            .map_or_else(
                || {
                    // Special case when fallback and <attr-type> are omitted.
                    // See FAILURE: https://drafts.csswg.org/css-values-5/#attr-substitution
                    if self.fallback.is_none() && is_type_omitted {
                        Some(simple_attr_substitution(""))
                    } else {
                        None
                    }
                },
                |(arg, attr)| {
                    let attr = if let AttrType::Type(_) = &arg.ty {
                        // If we're evaluating a container query, we haven't run the cascade
                        // and populated substitution_functions.attributes, so we can't do the
                        // get_attr() lookup here.
                        // TODO: This means chained attr() references will not work reliably in
                        // container style queries:
                        // https://bugzilla.mozilla.org/show_bug.cgi?id=2028861
                        if context.computed_context.in_container_query {
                            attr
                        } else {
                            context
                                .substitution_functions
                                .get_attr(&arg.name.name)
                                .map(|v| v.to_variable_value())?
                                .css
                        }
                    } else {
                        attr
                    };
                    let mut parser = Parser::new(&attr);
                    match &arg.ty {
                        AttrType::Unit(unit) => {
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
                            let value = ComputedValue::new(
                                css,
                                &source.url_data,
                                serialization,
                                serialization,
                            );
                            Some(Substitution::from_value(
                                value, /* attr_tainted */ true,
                            ))
                        },
                        AttrType::Type(syntax) => {
                            let value = SpecifiedRegisteredValue::parse(
                                &mut parser,
                                syntax,
                                &source.url_data,
                                AllowComputationallyDependent::Yes,
                                AttrTaint::default(),
                                context.property_id,
                            )
                            .ok()?;
                            let value = value.to_variable_value();
                            Some(Substitution::from_value(
                                value, /* attr_tainted */ true,
                            ))
                        },
                        AttrType::RawString | AttrType::None => {
                            Some(simple_attr_substitution(&attr))
                        },
                    }
                },
            )
            .ok_or(())
            .or_else(|_| {
                context.substitute_declaration_value(self.fallback.as_ref().ok_or(())?, source)
            })?;
        substitution.attr_tainted |= arg.attr_tainted;
        Ok(substitution)
    }

    /// Returns the argument, substituting and parsing it first if it has references.
    pub fn replace_arg<'a>(
        &'a self,
        source: &'a VariableValue,
        context: &mut SubstitutionReplacementContext<'a, '_, '_, '_>,
    ) -> ParsedArgument<'a, AttrArgument> {
        self.arg.substitute_and_parse(source, context)
    }

    /// Calls `f` for each arbitrary declaration value nested in the function.
    pub fn for_each_unparsed_value<F>(&self, mut f: F)
    where
        F: FnMut(&ArbitraryDeclarationValue),
    {
        if let SubstitutionArgument::Unparsed(ref arg) = self.arg {
            f(arg);
        }
        if let Some(ref fallback) = self.fallback {
            f(fallback);
        }
    }
}

fn simple_attr_substitution<'a>(s: &str) -> Substitution<'a> {
    let mut css = String::with_capacity(s.len() + 2);
    cssparser::serialize_string(s, &mut css).unwrap();
    Substitution::new(
        Cow::Owned(css),
        TokenSerializationType::Nothing,
        TokenSerializationType::Nothing,
        /* attr_tainted */ true,
    )
}
