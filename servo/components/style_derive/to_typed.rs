/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::cg;
use crate::to_css::{CssBitflagAttrs, CssFieldAttrs, CssInputAttrs, CssVariantAttrs};
use proc_macro2::{Span, TokenStream};
use quote::quote;
use quote::{ToTokens, TokenStreamExt};
use syn::{Data, DeriveInput, Ident, Path, WhereClause};
use synstructure::{BindingInfo, Structure};

/// Derive implementation of the `ToTyped` trait.
///
/// This derive supports both enums and structs:
///
/// * Enums
///   * Unit variants fall through to a shared ToCss call for the value, then wrapping the result in
///     `TypedValue::Keyword`.
///
///   * Fielded variants reify by calling `.to_typed()` on their inner values by default. Variants
///     or types marked with `skip_derive_fields` return `Err(())` instead.
///
/// * Structs
///   * Bitflags structs marked with `#[css(bitflags(...))]` are handled by
///     deriving keyword reification from their bitflag metadata.
///
///   * Other structs are handled similarly to data-carrying variants in mixed enums.
///
/// This allows keywords to be reified automatically into `CSSKeywordValue` objects, while leaving
/// more complex value types to be implemented incrementally as Typed OM support expands.
///
/// Summary of derive attributes recognized by this derive:
///
/// * `#[css(bitflags(single = "...", mixed = "...", overlapping_bits))]` on a
///   struct generates keyword reification for CSS bitflags types. Values that
///   can be represented as a single CSS keyword are reified as
///   `TypedValue::Keyword`; values that would serialize to multiple CSS
///   keywords are treated as unsupported and return `Err(())`.
///
///   `overlapping_bits` is supported for bitflags where one keyword subsumes
///   other internal bits, such as `contain: size`.
///
/// * `#[typed(skip_derive_fields)]` on the type disables field recursion for
///   structs and data-carrying enum variants.
///
/// * `#[css(skip)]`, `#[typed(skip)]`, or `#[typed(todo)]` on a variant mark
///   it as unsupported and cause the generated arm to return
///   `Err(())`.
///
/// * `#[css(skip)]` on a field disables reification for that field.
///
/// * `#[css(skip_if = "...")]` / `#[typed(skip_if = "...")]` on a field
///   conditionally disables reification for that field. If the provided
///   function returns `true` for the field value, the field is ignored.
///
/// * `#[css(contextual_skip_if = "...")]` /
///   `#[typed(contextual_skip_if = "...")]` on a field conditionally disables
///   reification for that field. The provided function is called with all
///   fields in the current struct or variant. If it returns `true`, the field
///   is ignored.
///
///   Typed skip annotations override CSS skip annotations when both are
///   present.
///
/// * `#[css(keyword = "...")]` on a unit variant overrides the keyword
///   string.
///
/// * `#[css(comma)]` on the variant indicates that fields may reify to
///   multiple separate values. When present, multiple `TypedValue`s may be
///   produced across the supported fields, unless
///   `#[typed(no_multiple_values)]` is also present. If multiple values are
///   not allowed and the derived implementation would produce more than one
///   item, it treats the value as unsupported and returns `Err(())`.
///
/// * `#[typed(no_multiple_values)]` on a variant prevents it from reifying to
///   multiple `TypedValue`s, even if `#[css(comma)]` is present.
///
/// * `#[css(iterable)]` on a field indicates that the field is an iterable
///   collection whose elements should be reified individually.
///
/// * `#[css(if_empty = "...")]` on an iterable field causes the provided
///   keyword to be emitted when the iterable contains no elements.
///
/// * `#[css(represents_keyword)]` on a bool field causes the field name to be
///   reified as a keyword when the field is true.
pub fn derive(mut input: DeriveInput) -> TokenStream {
    // The mutable `where_clause` is passed down to helper functions so they
    // can append trait bounds only when necessary. In particular, a bound of
    // the form `T: ToTyped` is added only if the generated code actually calls
    // `.to_typed()` on that inner value. This avoids forcing unrelated types
    // to implement `ToTyped` when field recursion is disabled, keeping
    // compilation requirements minimal and isolated to cases where reification
    // recursion is actually performed.
    let mut where_clause = input.generics.where_clause.take();

    let css_input_attrs = cg::parse_input_attrs::<CssInputAttrs>(&input);

    let input_attrs = cg::parse_input_attrs::<TypedInputAttrs>(&input);

    if matches!(input.data, Data::Enum(..)) || css_input_attrs.bitflags.is_some() {
        assert!(
            !css_input_attrs.comma,
            "#[css(comma)] is not allowed on enums or bitflags"
        );
    }

    if let Some(ref bitflags) = css_input_attrs.bitflags {
        assert!(where_clause.is_none(), "Generic bitflags?");
        return derive_bitflags(&input, bitflags);
    }

    // Generate a `match` where unit variants fall through to to_css(), and all other variants
    // either return an error or a custom value.
    let s = Structure::new(&input);
    let mut any_keyword = false;
    let match_body = s.each_variant(|variant| {
        derive_variant_arm(
            variant,
            input_attrs.skip_derive_fields || input_attrs.todo_derive_fields,
            &mut where_clause,
            &mut any_keyword,
        )
    });

    let to_css_fallthrough = if any_keyword {
        quote! {
            let s = style_traits::ToCss::to_css_cssstring(self);
            dest.push(crate::typed_om::TypedValue::Keyword(crate::typed_om::KeywordValue(s)));
            Ok(())
        }
    } else {
        quote! {}
    };

    let body = quote! {
        fn to_typed(&self, dest: &mut thin_vec::ThinVec<crate::typed_om::TypedValue>) -> Result<(), ()> {
            match *self {
                #match_body
            }
            #to_css_fallthrough
        }
    };

    input.generics.where_clause = where_clause;

    let name = &input.ident;

    // Split the input type’s generics into pieces we can use for impl.
    let (impl_generics, ty_generics, where_clause) = input.generics.split_for_impl();

    // Put it all together into the impl block.
    quote! {
        impl #impl_generics crate::typed_om::ToTyped for #name #ty_generics #where_clause {
            #body
        }
    }
}

/// Derive a `ToTyped` implementation for CSS bitflags types.
///
/// This mirrors the `ToCss` bitflags metadata, but with stricter output:
/// Typed OM reification can only produce one `TypedValue::Keyword`. Mixed
/// values that would serialize to multiple keywords return `Err(())`.
///
/// `overlapping_bits` needs special handling because some keywords are stored
/// as combinations of lower-level bits. For example, `contain: size` also
/// contains the `inline-size` bit internally. The generated code tracks
/// consumed flags so such values still reify to `"size"` rather than being
/// rejected as multi-keyword values.
fn derive_bitflags(input: &syn::DeriveInput, bitflags: &CssBitflagAttrs) -> TokenStream {
    let name = &input.ident;
    let mut body = TokenStream::new();

    for (rust_name, css_name) in bitflags.single_flags() {
        let rust_ident = Ident::new(&rust_name, Span::call_site());
        body.append_all(quote! {
            if *self == Self::#rust_ident {
                dest.push(crate::typed_om::TypedValue::Keyword(
                    crate::typed_om::KeywordValue(style_traits::CssString::from(#css_name)),
                ));
                return Ok(());
            }
        });
    }

    body.append_all(quote! {
        let mut flag: Option<&'static str> = None;
    });

    if bitflags.overlapping_bits {
        // Track already-consumed flags so overlapping serialized keywords
        // such as `contain: size` are not mistaken for multi-keyword values.
        body.append_all(quote! {
            let mut serialized = Self::empty();
        });
    }

    for (rust_name, css_name) in bitflags.mixed_flags() {
        let rust_ident = Ident::new(&rust_name, Span::call_site());
        let init_flag = quote! {
            if flag.is_some() {
                return Err(());
            }
            flag = Some(#css_name);
        };
        if bitflags.overlapping_bits {
            body.append_all(quote! {
                if self.contains(Self::#rust_ident) && !serialized.intersects(Self::#rust_ident) {
                    #init_flag
                    serialized.insert(Self::#rust_ident);
                }
            });
        } else {
            body.append_all(quote! {
                if self.intersects(Self::#rust_ident) {
                    #init_flag
                }
            });
        }
    }

    body.append_all(quote! {
        let Some(flag) = flag else {
            return Err(());
        };

        dest.push(crate::typed_om::TypedValue::Keyword(
            crate::typed_om::KeywordValue(style_traits::CssString::from(flag)),
        ));
        Ok(())
    });

    quote! {
        impl crate::typed_om::ToTyped for #name {
            #[allow(unused_variables)]
            #[inline]
            fn to_typed(
                &self,
                dest: &mut thin_vec::ThinVec<crate::typed_om::TypedValue>,
            ) -> Result<(), ()> {
                #body
            }
        }
    }
}

/// Generate the match arm expression for a struct or enum variant in a derived
/// `ToTyped` implementation.
///
/// * Unit variants fall through to a single to_css call wrapped into a Keyword value.
/// * Variants marked with `#[css(skip)]` or `#[typed(skip)]` or `#[typed(todo)]` return `Err(())`.
/// * Variants with fields delegate to `derive_variant_fields_expr()` by default; if
/// `skip_derive_fields` is set, they return `Err(())`.
fn derive_variant_arm(
    variant: &synstructure::VariantInfo,
    skip_derive_fields: bool,
    where_clause: &mut Option<WhereClause>,
    any_keyword: &mut bool,
) -> TokenStream {
    let bindings = variant.bindings();
    // Get the underlying syn AST node for this variant.
    let ast = variant.ast();

    // Parse any #[css(...)] attributes attached to this variant.
    let css_variant_attrs = cg::parse_variant_attrs_from_ast::<CssVariantAttrs>(&ast);

    // Parse any #[typed(...)] attributes attached to this variant.
    let variant_attrs = cg::parse_variant_attrs_from_ast::<TypedVariantAttrs>(&ast);

    // If the variant is explicitly marked #[css(skip)], don’t generate
    // anything for it, always return Err(()).
    if css_variant_attrs.skip {
        return quote! { return Err(()) };
    }

    // If the variant is explicitly marked #[typed(skip)] or #[typed(todo)],
    // don’t generate anything for it, always return Err(()).
    if variant_attrs.skip || variant_attrs.todo {
        return quote! { return Err(()) };
    }

    // If the variant has no bindings (i.e. no data fields), treat it as a unit
    // variant and reify it using its css serialization.
    if bindings.is_empty() {
        if !*any_keyword {
            cg::add_predicate(where_clause, parse_quote!(Self: style_traits::ToCss));
        }
        *any_keyword = true;
        return quote! {{}};
    } else if !skip_derive_fields {
        let allow_multiple_values = css_variant_attrs.comma && !variant_attrs.no_multiple_values;
        derive_variant_fields_expr(bindings, where_clause, allow_multiple_values)
    } else {
        // This variant has one or more fields, but field reification is
        // disabled. With `skip_derive_fields`, this variant simply returns
        // `Err(())`.
        quote! { return Err(()) }
    }
}

/// Generate the match arm expression for fields of a struct or enum variant
/// in a derived `ToTyped` implementation.
///
/// This helper examines the variant’s fields and generates reification code
/// for the usable fields.
///
/// * If the variant has exactly one non-iterable field, it emits a direct
///   call to that field’s `ToTyped` implementation and adds the corresponding
///   trait bound (e.g. `T: ToTyped`) to the `where` clause.
///
/// * Otherwise, it appends the reified output of the supported fields to the
///   destination and then validates whether the enclosing variant is allowed
///   to produce multiple `TypedValue`s.
///
/// Fields marked with `#[css(skip)]`, or skipped by css/typed `skip_if` or
/// `contextual_skip_if` annotations, are ignored.
fn derive_variant_fields_expr(
    bindings: &[BindingInfo],
    where_clause: &mut Option<WhereClause>,
    allow_multiple_values: bool,
) -> TokenStream {
    // Filter out fields marked with #[css(skip)] so they are ignored during
    // reification.
    let mut iter = bindings
        .iter()
        .filter_map(|binding| {
            let css_field_attrs = cg::parse_field_attrs::<CssFieldAttrs>(binding.ast());
            let field_attrs = cg::parse_field_attrs::<TypedFieldAttrs>(binding.ast());
            if css_field_attrs.skip {
                return None;
            }
            Some((binding, css_field_attrs, field_attrs))
        })
        .peekable();

    // If no usable fields remain, generate code that just returns Err(()).
    let (first, css_field_attrs, field_attrs) = match iter.next() {
        Some(triple) => triple,
        None => return quote! { return Err(()) },
    };

    // At this point we have at least one usable field in `first`.

    // Handle the simple case of exactly one non-iterable field.
    if !css_field_attrs.iterable && iter.peek().is_none() {
        let expr =
            derive_single_field_expr(first, css_field_attrs, field_attrs, where_clause, bindings);

        return quote! {{
            #expr
            return Ok(())
        }};
    }

    // Handle the general case by appending reified output from the supported
    // fields directly to the destination.
    let mut expr =
        derive_single_field_expr(first, css_field_attrs, field_attrs, where_clause, bindings);
    for (binding, css_field_attrs, field_attrs) in iter {
        derive_single_field_expr(
            binding,
            css_field_attrs,
            field_attrs,
            where_clause,
            bindings,
        )
        .to_tokens(&mut expr)
    }

    quote! {{
        let old_len = dest.len();
        #expr
        if !#allow_multiple_values && dest.len() - old_len > 1 {
            dest.truncate(old_len);
            return Err(());
        }
        return Ok(())
    }}
}

/// Generate the expression used to reify a single field in a derived
/// `ToTyped` implementation.
///
/// For fields marked with `#[css(iterable)]`, this helper generates code that
/// iterates over the field and calls `ToTyped::to_typed` for each element. If
/// `#[css(if_empty = "...")]` is present, the generated code emits the
/// specified keyword when the iterable is empty.
///
/// For non-iterable fields, it generates a direct `ToTyped::to_typed` call
/// for the field value.
///
/// If a css/typed `skip_if` or `contextual_skip_if` annotation is present and
/// the provided function returns `true`, the field contributes no reified
/// output. Typed skip annotations override CSS skip annotations.
///
/// The appropriate `T: ToTyped` bounds for the field type or iterable element
/// type(s) are added to the `where` clause.
fn derive_single_field_expr(
    field: &BindingInfo,
    css_field_attrs: CssFieldAttrs,
    field_attrs: TypedFieldAttrs,
    where_clause: &mut Option<WhereClause>,
    bindings: &[BindingInfo],
) -> TokenStream {
    let mut expr = if css_field_attrs.iterable {
        // We add `ToTyped` bounds for the iterable's element type(s), rather
        // than for the container type itself. This avoids ToTyped forcing
        // unrelated container types to implement ToTyped.
        //
        // This is a bit more involved than other derives, but it matches how
        // Typed OM reification is structured today. If this approach works
        // well, the helper that extracts the field types
        // (field_generic_arguments) can be moved into cg.rs alongside other
        // derive helpers.
        //
        // See also the comment in the beginning of the main `derive` fn.
        for item_ty in field_generic_arguments(field) {
            cg::add_predicate(
                where_clause,
                parse_quote!(#item_ty: crate::typed_om::ToTyped),
            );
        }

        if let Some(if_empty) = css_field_attrs.if_empty {
            quote! {
                let mut iter = #field.iter().peekable();
                if iter.peek().is_none() {
                    dest.push(crate::typed_om::TypedValue::Keyword(
                        crate::typed_om::KeywordValue(style_traits::CssString::from(#if_empty)),
                    ));
                } else {
                    for item in iter {
                        crate::typed_om::ToTyped::to_typed(&item, dest)?;
                    }
                }
            }
        } else {
            quote! {
                for item in #field.iter() {
                    crate::typed_om::ToTyped::to_typed(&item, dest)?;
                }
            }
        }
    } else if css_field_attrs.represents_keyword {
        let ident = field
            .ast()
            .ident
            .as_ref()
            .expect("Unnamed field with represents_keyword?");
        let ident = cg::to_css_identifier(&ident.to_string()).replace("_", "-");
        quote! {
            if *#field {
                dest.push(crate::typed_om::TypedValue::Keyword(
                    crate::typed_om::KeywordValue(style_traits::CssString::from(#ident)),
                ));
            }
        }
    } else {
        // Add a trait bound `T: ToTyped` to ensure the field type implements
        // the required conversion, and emit a call to its `.to_typed()`
        // method.
        let ty = &field.ast().ty;
        cg::add_predicate(where_clause, parse_quote!(#ty: crate::typed_om::ToTyped));

        quote! {
           crate::typed_om::ToTyped::to_typed(#field, dest)?;
        }
    };

    let typed_has_skip = field_attrs.skip_if.is_some() || field_attrs.contextual_skip_if.is_some();

    let skip_if = field_attrs.skip_if.or_else(|| {
        if typed_has_skip {
            None
        } else {
            css_field_attrs.skip_if
        }
    });

    let contextual_skip_if = field_attrs.contextual_skip_if.or_else(|| {
        if typed_has_skip {
            None
        } else {
            css_field_attrs.contextual_skip_if
        }
    });

    assert!(
        skip_if.is_none() || contextual_skip_if.is_none(),
        "Field should not have both skip_if and contextual_skip_if"
    );

    if let Some(condition) = skip_if {
        expr = quote! {
            if !#condition(#field) {
                #expr
            }
        }
    }

    if let Some(condition) = contextual_skip_if {
        expr = quote! {
            if !#condition(#(#bindings), *) {
                #expr
            }
        }
    }

    expr
}

/// Extract generic type arguments from a field type.
///
/// This helper is used by the `derive_variant_fields_expr` when handling
/// iterable fields. The function needs to add `T: ToTyped` bounds for the
/// item type produced by iteration (since the generated code calls
/// `.to_typed()` on each item).
///
/// For example:
///   * `Vec<T>` / `OwnedSlice<T>` -> `T`
///   * `SmallVec<[T; N]>` -> `T`
///
/// The function inspects the last path segment of the field’s type and
/// returns any generic type arguments it finds, unwrapping array forms such
/// as `[T; N]` used by containers like `SmallVec`.
///
/// This is intentionally minimal and currently supports the container shapes
/// used in style structs.
pub(crate) fn field_generic_arguments(field: &BindingInfo) -> Vec<syn::Type> {
    use syn::{GenericArgument, PathArguments, Type};

    let ty = &field.ast().ty;

    let Type::Path(type_path) = ty else {
        return vec![];
    };
    let Some(seg) = type_path.path.segments.last() else {
        return vec![];
    };
    let PathArguments::AngleBracketed(args) = &seg.arguments else {
        return vec![];
    };

    let mut result = Vec::new();
    for arg in &args.args {
        let GenericArgument::Type(arg_ty) = arg else {
            continue;
        };

        // If it's something like SmallVec<[T; N]>, take T.
        match arg_ty {
            Type::Array(arr) => result.push((*arr.elem).clone()),
            _ => result.push(arg_ty.clone()),
        }
    }
    result
}

#[derive(Default, FromDeriveInput)]
#[darling(attributes(typed), default)]
pub struct TypedInputAttrs {
    /// Disables field-level recursion when deriving `ToTyped`.
    ///
    /// When set, the derive will not call `.to_typed()` on inner values (for
    /// example, struct fields or data-carrying enum variants), and instead
    /// the generated code will return `Err(())` for those cases.
    ///
    /// This is useful to avoid requiring inner types to implement `ToTyped`
    /// when reification of those fields is not yet supported.
    pub skip_derive_fields: bool,

    /// Temporarily disables field-level recursion while marking it as TODO.
    ///
    /// When set, the derive will not call `.to_typed()` on inner values and
    /// instead return `Err(())` for those cases.
    ///
    /// Unlike `skip_derive_fields`, this indicates that reification is
    /// expected to be revisited later, either to implement it or to switch to
    /// `skip_derive_fields` if it turns out to be unsupported.
    pub todo_derive_fields: bool,

    /// Prevents this type from reifying to multiple Typed OM values.
    ///
    /// This is useful for types that use `#[css(comma)]` for CSS
    /// serialization, but should still be treated as unsupported when they
    /// would produce more than one `TypedValue`.
    pub no_multiple_values: bool,
}

#[derive(Default, FromVariant)]
#[darling(attributes(typed), default)]
pub struct TypedVariantAttrs {
    /// Same as the top-level `skip_derive_fields`, but included here because
    /// struct variants are represented as both a variant and a type
    /// definition.
    ///
    /// When set, field-level reification for this variant is disabled and the
    /// generated code returns `Err(())`.
    pub skip_derive_fields: bool,

    /// Same as the top-level `todo_derive_fields`, but included here because
    /// struct variants are represented as both a variant and a type
    /// definition.
    ///
    /// When set, field-level reification for this variant is disabled and the
    /// generated code returns `Err(())`.
    pub todo_derive_fields: bool,

    /// Same as the top-level `no_multiple_values`, but included here because
    /// struct variants are represented as both a variant and a type
    /// definition.
    ///
    /// When set, this variant is prevented from reifying to multiple
    /// `TypedValue`s.
    pub no_multiple_values: bool,

    /// If present, this variant is excluded from generated reification code.
    /// `to_typed()` will always return `Err(())` for it.
    pub skip: bool,

    /// Marks this variant as a placeholder for a future implementation.
    /// Behavior is the same as `skip`, but used to indicate that reification
    /// is intentionally left unimplemented for now.
    pub todo: bool,
}

#[derive(Default, FromField)]
#[darling(attributes(typed), default)]
pub struct TypedFieldAttrs {
    /// Conditionally skips reification of this field.
    ///
    /// The provided function is called with the field value. If it returns
    /// `true`, the field is ignored and produces no `TypedValue` items.
    pub skip_if: Option<Path>,

    /// Conditionally skips reification of this field based on the full value.
    ///
    /// The provided function is called with all fields in the current struct
    /// or variant. If it returns `true`, the field is ignored and produces no
    /// `TypedValue` items.
    pub contextual_skip_if: Option<Path>,
}
