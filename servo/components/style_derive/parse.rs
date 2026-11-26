/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::cg;
use crate::to_css::{CssBitflagAttrs, CssVariantAttrs};
use proc_macro2::{Span, TokenStream};
use quote::TokenStreamExt;
use syn::{self, DeriveInput, Ident, Path};
use synstructure::{Structure, VariantInfo};

#[derive(Default, FromVariant)]
#[darling(attributes(parse), default)]
pub struct ParseVariantAttrs {
    pub aliases: Option<String>,
    pub condition: Option<Path>,
    pub parse_fn: Option<Path>,
    pub skip: bool,
}

#[derive(Default, FromField)]
#[darling(attributes(parse), default)]
pub struct ParseFieldAttrs {
    field_bound: bool,
}

fn parse_bitflags(bitflags: &CssBitflagAttrs) -> TokenStream {
    let mut match_arms = TokenStream::new();
    for (rust_name, css_name) in bitflags.single_flags() {
        let rust_ident = Ident::new(&rust_name, Span::call_site());
        match_arms.append_all(quote! {
            #css_name if result.is_empty() => {
                single_flag = true;
                Self::#rust_ident
            },
        });
    }

    for (rust_name, css_name) in bitflags.mixed_flags() {
        let rust_ident = Ident::new(&rust_name, Span::call_site());
        match_arms.append_all(quote! {
            #css_name => Self::#rust_ident,
        });
    }

    let mut validate_condition = quote! { !result.is_empty() };
    if let Some(ref function) = bitflags.validate_mixed {
        validate_condition.append_all(quote! {
            && #function(&mut result)
        });
    }

    // NOTE(emilio): this loop has this weird structure because we run this code
    // to parse stuff like text-decoration-line in the text-decoration
    // shorthand, so we need to be a bit careful that we don't error if we don't
    // consume the whole thing because we find an invalid identifier or other
    // kind of token. Instead, we should leave it unconsumed.
    quote! {
        let mut result = Self::empty();
        loop {
            let mut single_flag = false;
            let flag: Result<_, style_traits::ParseError> = input.try_parse(|input| {
                Ok(try_match_ident_ignore_ascii_case! { input,
                    #match_arms
                })
            });

            let flag = match flag {
                Ok(flag) => flag,
                Err(..) => break,
            };

            if single_flag {
                return Ok(flag);
            }

            if result.intersects(flag) {
                return Err(style_traits::ParseError::custom(style_traits::StyleParseErrorKind::UnspecifiedError));
            }

            result.insert(flag);
        }
        if #validate_condition {
            Ok(result)
        } else {
            Err(style_traits::ParseError::custom(style_traits::StyleParseErrorKind::UnspecifiedError))
        }
    }
}

fn parse_non_keyword_variant(
    where_clause: &mut Option<syn::WhereClause>,
    variant: &VariantInfo,
    variant_attrs: &CssVariantAttrs,
    parse_attrs: &ParseVariantAttrs,
    skip_try: bool,
) -> TokenStream {
    let bindings = variant.bindings();
    assert!(parse_attrs.aliases.is_none());
    assert!(variant_attrs.function.is_none());
    assert!(variant_attrs.keyword.is_none());
    assert!(!parse_attrs.skip);
    assert_eq!(
        bindings.len(),
        1,
        "We only support deriving parse for simple variants"
    );
    let binding_ast = &bindings[0].ast();
    let ty = &binding_ast.ty;

    if let Some(ref bitflags) = variant_attrs.bitflags {
        assert!(skip_try, "Should be the only variant");
        assert!(parse_attrs.parse_fn.is_none(), "should not be needed");
        assert!(
            parse_attrs.condition.is_none(),
            "Should be the only variant"
        );
        assert!(where_clause.is_none(), "Generic bitflags?");
        return parse_bitflags(bitflags);
    }

    let field_attrs = cg::parse_field_attrs::<ParseFieldAttrs>(binding_ast);

    if field_attrs.field_bound {
        cg::add_predicate(where_clause, parse_quote!(#ty: crate::parser::Parse));
    }

    let mut parse = if let Some(ref parse_fn) = parse_attrs.parse_fn {
        quote! { #parse_fn(context, input) }
    } else {
        quote! { <#ty as crate::parser::Parse>::parse(context, input) }
    };

    let variant_name = &variant.ast().ident;
    let variant_name = match variant.prefix {
        Some(p) => quote! { #p::#variant_name },
        None => quote! { #variant_name },
    };

    parse = if skip_try {
        quote! {
            let v = #parse?;
            return Ok(#variant_name(v));
        }
    } else {
        quote! {
            if let Ok(v) = input.try_parse(|input| #parse) {
                return Ok(#variant_name(v));
            }
        }
    };

    if let Some(ref condition) = parse_attrs.condition {
        parse = quote! {
            if #condition(context) {
                #parse
            }
        };

        if skip_try {
            // We're the last variant and we can fail to parse due to the
            // condition clause. If that happens, we need to return an error.
            parse = quote! {
                #parse
                Err(style_traits::ParseError::custom(style_traits::StyleParseErrorKind::UnspecifiedError))
            };
        }
    }

    parse
}

pub fn derive(mut input: DeriveInput) -> TokenStream {
    let mut where_clause = input.generics.where_clause.take();
    for param in input.generics.type_params() {
        cg::add_predicate(
            &mut where_clause,
            parse_quote!(#param: crate::parser::Parse),
        );
    }

    let name = &input.ident;
    let s = Structure::new(&input);

    let mut saw_condition = false;
    let mut match_keywords = quote! {};
    let mut non_keywords = vec![];

    let mut effective_variants = 0;
    for variant in s.variants().iter() {
        let css_variant_attrs = cg::parse_variant_attrs_from_ast::<CssVariantAttrs>(&variant.ast());
        let parse_attrs = cg::parse_variant_attrs_from_ast::<ParseVariantAttrs>(&variant.ast());
        if css_variant_attrs.skip || parse_attrs.skip {
            continue;
        }

        effective_variants += 1;
        saw_condition |= parse_attrs.condition.is_some();

        if !variant.bindings().is_empty() {
            non_keywords.push((variant, css_variant_attrs, parse_attrs));
            continue;
        }

        assert!(parse_attrs.parse_fn.is_none());

        let identifier = cg::to_css_identifier(
            &css_variant_attrs
                .keyword
                .unwrap_or_else(|| variant.ast().ident.to_string()),
        );
        let ident = &variant.ast().ident;

        let condition = match parse_attrs.condition {
            Some(ref p) => quote! { if #p(context) },
            None => quote! {},
        };

        match_keywords.extend(quote! {
            #identifier #condition => Ok(#name::#ident),
        });

        let aliases = match parse_attrs.aliases {
            Some(aliases) => aliases,
            None => continue,
        };

        for alias in aliases.split(',') {
            match_keywords.extend(quote! {
                #alias #condition => Ok(#name::#ident),
            });
        }
    }

    let needs_context = saw_condition || !non_keywords.is_empty();

    let context_ident = if needs_context {
        quote! { context }
    } else {
        quote! { _ }
    };

    let has_keywords = non_keywords.len() != effective_variants;

    let mut parse_non_keywords = quote! {};
    for (i, (variant, css_attrs, parse_attrs)) in non_keywords.iter().enumerate() {
        let skip_try = !has_keywords && i == non_keywords.len() - 1;
        let parse_variant =
            parse_non_keyword_variant(&mut where_clause, variant, css_attrs, parse_attrs, skip_try);
        parse_non_keywords.extend(parse_variant);
    }

    let parse_body = if needs_context {
        let parse_keywords = if has_keywords {
            quote! {
                let location = input.position();
                let ident = input.expect_ident()?;
                cssparser::match_ignore_ascii_case! { &ident,
                    #match_keywords
                    _ => Err(style_traits::ParseError::unexpected_token())
                }
            }
        } else {
            quote! {}
        };

        quote! {
            #parse_non_keywords
            #parse_keywords
        }
    } else {
        quote! { Self::parse(input) }
    };

    let has_non_keywords = !non_keywords.is_empty();

    input.generics.where_clause = where_clause;
    let (impl_generics, ty_generics, where_clause) = input.generics.split_for_impl();

    let parse_trait_impl = quote! {
        impl #impl_generics crate::parser::Parse for #name #ty_generics #where_clause {
            #[inline]
            fn parse(
                #context_ident: &crate::parser::ParserContext,
                input: &mut cssparser::Parser,
            ) -> Result<Self, style_traits::ParseError> {
                #parse_body
            }
        }
    };

    if needs_context {
        return parse_trait_impl;
    }

    assert!(!has_non_keywords);

    // TODO(emilio): It'd be nice to get rid of these, but that makes the
    // conversion harder...
    let methods_impl = quote! {
        impl #name {
            /// Parse this keyword.
            #[inline]
            pub fn parse(
                input: &mut cssparser::Parser,
            ) -> Result<Self, style_traits::ParseError> {
                let location = input.position();
                let ident = input.expect_ident()?;
                Self::from_ident(ident.as_ref())
                    .map_err(|()| style_traits::ParseError::unexpected_token())
            }

            /// Parse this keyword from a string slice.
            #[inline]
            pub fn from_ident(ident: &str) -> Result<Self, ()> {
                cssparser::match_ignore_ascii_case! { ident,
                    #match_keywords
                    _ => Err(()),
                }
            }
        }
    };

    quote! {
        #parse_trait_impl
        #methods_impl
    }
}
