/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use crate::cg;
use crate::to_css::{CssFieldAttrs, CssInputAttrs, CssVariantAttrs};
use proc_macro2::TokenStream;
use quote::quote;
use quote::ToTokens;
use syn::{Data, DataEnum, DeriveInput, Fields, Path, WhereClause};
use synstructure::{BindingInfo, Structure};

/// Derive implementation of the `ToTyped` trait.
///
/// This derive supports both enums and structs:
///
/// * Enums
///   * Pure keyword enums: Enums made up entirely of unit variants
///     (e.g. `enum Visibility { Visible, Hidden, Collapse }`). In this case,
///     the derive optimizes by delegating to `ToCss` once for the whole value,
///     then wrapping the result in `TypedValue::Keyword`.
///
///   * Mixed enums with unit variants: Enums that contain both unit
///     variants (keywords) and data-carrying variants. The derive generates a
///     `match` implementation where unit variants are reified as
///     `TypedValue::Keyword`, and fielded variants reify by calling
///     `.to_typed()` on their inner values by default. Variants or types
///     marked with `skip_derive_fields` return `Err(())` instead.
///
/// * Structs
///   * Structs are handled similarly to data-carrying variants in mixed enums.
///     Unless `skip_derive_fields` is set, and as long as the type is not
///     marked as a bitflags type, `.to_typed()` is generated for their inner
///     values; otherwise, they return `Err(())`.
///
/// Unit variants are mapped to keywords using their Rust identifier converted
/// via `to_css_identifier`. Attributes like `#[css(keyword = "...")]` will
/// override the behavior and use the provided keyword instead.
///
/// For other kinds of types (e.g. unions), no `to_typed` method is generated;
/// the default implementation applies, which always returns `Err(())`.
///
/// This allows keywords to be reified automatically into `CSSKeywordValue`
/// objects, while leaving more complex value types to be implemented
/// incrementally as Typed OM support expands.
///
/// Summary of derive attributes recognized by this derive:
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
/// * `#[typed(skip_if = "...")]` on a field conditionally disables reification
///   for that field. If the provided function returns `true` for the field
///   value, the field is ignored.
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

    let body = match &input.data {
        // Handle enums.
        Data::Enum(DataEnum { variants, .. }) => {
            // Check if this enum consists entirely of unit variants (no fields).
            let all_unit = variants.iter().all(|v| matches!(v.fields, Fields::Unit));

            if all_unit {
                // Optimization: for all-unit enums, reuse `ToCss` once and
                // wrap the result in a `TypedValue::Keyword`, instead of
                // generating a full match. This avoids code bloat while
                // producing the same runtime behavior.
                quote! {
                    fn to_typed(&self, dest: &mut thin_vec::ThinVec<style_traits::TypedValue>) -> Result<(), ()> {
                      let s = style_traits::ToCss::to_css_cssstring(self);
                      dest.push(style_traits::TypedValue::Keyword(style_traits::KeywordValue(s)));
                      Ok(())
                    }
                }
            } else {
                // Mixed enums: generate a `match` where unit variants map to
                // `TypedValue::Keyword` and all other variants return
                // `Err(())`. This is more verbose in code size, but allows
                // selective handling of individual variants.
                let s = Structure::new(&input);
                let match_body = s.each_variant(|variant| {
                    derive_variant_arm(
                        variant,
                        input_attrs.skip_derive_fields || input_attrs.todo_derive_fields,
                        &mut where_clause,
                    )
                });

                quote! {
                    fn to_typed(&self, dest: &mut thin_vec::ThinVec<style_traits::TypedValue>) -> Result<(), ()> {
                        match *self {
                            #match_body
                        }
                    }
                }
            }
        },

        // Handle structs that are not bitflags.
        Data::Struct(_) => {
            if css_input_attrs.bitflags.is_none() {
                let s = Structure::new(&input);
                let match_body = s.each_variant(|variant| {
                    derive_variant_arm(
                        variant,
                        input_attrs.skip_derive_fields || input_attrs.todo_derive_fields,
                        &mut where_clause,
                    )
                });

                quote! {
                    fn to_typed(&self, dest: &mut thin_vec::ThinVec<style_traits::TypedValue>) -> Result<(), ()> {
                        match *self {
                            #match_body
                        }
                    }
                }
            } else {
                quote! {}
            }
        },

        // Otherwise, don’t emit any `to_typed` method body. The default
        // implementation (returning `Err(())`) will apply.
        _ => quote! {},
    };

    input.generics.where_clause = where_clause;

    let name = &input.ident;

    // Split the input type’s generics into pieces we can use for impl.
    let (impl_generics, ty_generics, where_clause) = input.generics.split_for_impl();

    // Put it all together into the impl block.
    quote! {
        impl #impl_generics style_traits::ToTyped for #name #ty_generics #where_clause {
            #body
        }
    }
}

/// Generate the match arm expression for a struct or enum variant in a derived
/// `ToTyped` implementation.
///
/// * Unit variants are reified into `TypedValue::Keyword`, using the variant’s
///   identifier converted with `cg::to_css_identifier` or a custom keyword if
///   provided through `#[css(keyword = "...")]`.
/// * Variants marked with `#[css(skip)]` or `#[typed(skip)]` or
///   `#[typed(todo)]` return `Err(())`.
/// * Variants with fields delegate to `derive_variant_fields_expr()` by
///   default; if `skip_derive_fields` is set, they return `Err(())`.
///
/// Note: `#[css(keyword = "...")]` overrides are now recognized in this
/// `derive_variant_arm` path, but the support is not yet exercised because we
/// currently have no mixed enums that use keyword overrides together with
/// `ToTyped`. This keeps the behavior the same as before, all existing enums
/// with keyword overrides (e.g. `#[css(keyword = "preserve-3d")]`) are still
/// pure keyword enums and are handled through the all-unit `ToCss` path.
fn derive_variant_arm(
    variant: &synstructure::VariantInfo,
    skip_derive_fields: bool,
    where_clause: &mut Option<WhereClause>,
) -> TokenStream {
    let bindings = variant.bindings();
    // Get the underlying syn AST node for this variant.
    let ast = variant.ast();
    let identifier = &ast.ident;

    // Parse any #[css(...)] attributes attached to this variant.
    let css_variant_attrs = cg::parse_variant_attrs_from_ast::<CssVariantAttrs>(&ast);

    // Parse any #[typed(...)] attributes attached to this variant.
    let variant_attrs = cg::parse_variant_attrs_from_ast::<TypedVariantAttrs>(&ast);

    // If the variant is explicitly marked #[css(skip)], don’t generate
    // anything for it, always return Err(()).
    if css_variant_attrs.skip {
        return quote! {Err(())};
    }

    // If the variant is explicitly marked #[typed(skip)] or #[typed(todo)],
    // don’t generate anything for it, always return Err(()).
    if variant_attrs.skip || variant_attrs.todo {
        return quote! {Err(())};
    }

    // If the variant has no bindings (i.e. no data fields), treat it as a unit
    // variant and reify it as a keyword.
    if bindings.is_empty() {
        // If #[css(keyword = "...")] is present, use it.
        // Else convert the Rust variant name into its CSS identifier form
        // (e.g. AvoidColumn -> "avoid-column").
        let keyword = css_variant_attrs
            .keyword
            .unwrap_or_else(|| cg::to_css_identifier(&identifier.to_string()));

        // Emit code to wrap this keyword into a TypedValue.
        quote! {
            dest.push(style_traits::TypedValue::Keyword(
                style_traits::KeywordValue(style_traits::CssString::from(#keyword))
            ));
            Ok(())
        }
    } else if !skip_derive_fields {
        let allow_multiple_values = css_variant_attrs.comma && !variant_attrs.no_multiple_values;

        derive_variant_fields_expr(bindings, where_clause, allow_multiple_values)
    } else {
        // This variant has one or more fields, but field reification is
        // disabled. With `skip_derive_fields`, this variant simply returns
        // `Err(())`.
        quote! {
            Err(())
        }
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
/// Fields marked with `#[css(skip)]`, or skipped by
/// `#[typed(skip_if = "...")]`, are ignored.
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
            let css_field_attrs = cg::parse_field_attrs::<CssFieldAttrs>(&binding.ast());
            let field_attrs = cg::parse_field_attrs::<TypedFieldAttrs>(&binding.ast());
            if css_field_attrs.skip {
                return None;
            }
            Some((binding, css_field_attrs, field_attrs))
        })
        .peekable();

    // If no usable fields remain, generate code that just returns Err(()).
    let (first, css_field_attrs, field_attrs) = match iter.next() {
        Some(triple) => triple,
        None => return quote! { Err(()) },
    };

    // At this point we have at least one usable field in `first`.

    // Handle the simple case of exactly one non-iterable field.
    if !css_field_attrs.iterable && iter.peek().is_none() {
        // Add a trait bound `T: ToTyped` to ensure the field type implements
        // the required conversion, and emit a call to its `.to_typed()`
        // method.
        let ty = &first.ast().ty;
        cg::add_predicate(where_clause, parse_quote!(#ty: style_traits::ToTyped));

        let mut expr = quote! { style_traits::ToTyped::to_typed(#first, dest) };

        if let Some(condition) = field_attrs.skip_if {
            expr = quote! {
                if !#condition(#first) {
                    #expr
                }
            }
        }

        return expr;
    }

    // Handle the general case by appending reified output from the supported
    // fields directly to the destination.
    let mut expr = derive_single_field_expr(first, css_field_attrs, field_attrs, where_clause);
    for (binding, css_field_attrs, field_attrs) in iter {
        derive_single_field_expr(binding, css_field_attrs, field_attrs, where_clause)
            .to_tokens(&mut expr)
    }

    quote! {{
        let old_len = dest.len();
        #expr
        if !#allow_multiple_values && dest.len() - old_len > 1 {
            dest.truncate(old_len);
            return Err(());
        }
        Ok(())
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
/// If `#[typed(skip_if = "...")]` is present and the provided function returns
/// `true` for the field value, the field contributes no reified output.
///
/// The appropriate `T: ToTyped` bounds for the field type or iterable element
/// type(s) are added to the `where` clause.
fn derive_single_field_expr(
    field: &BindingInfo,
    css_field_attrs: CssFieldAttrs,
    field_attrs: TypedFieldAttrs,
    where_clause: &mut Option<WhereClause>,
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
            cg::add_predicate(where_clause, parse_quote!(#item_ty: style_traits::ToTyped));
        }

        if let Some(if_empty) = css_field_attrs.if_empty {
            quote! {
                let mut iter = #field.iter().peekable();
                if iter.peek().is_none() {
                    dest.push(style_traits::TypedValue::Keyword(
                        style_traits::KeywordValue(style_traits::CssString::from(#if_empty)),
                    ));
                } else {
                    for item in iter {
                        style_traits::ToTyped::to_typed(&item, dest)?;
                    }
                }
            }
        } else {
            quote! {
                for item in #field.iter() {
                    style_traits::ToTyped::to_typed(&item, dest)?;
                }
            }
        }
    } else {
        // Add a trait bound `T: ToTyped` to ensure the field type implements
        // the required conversion, and emit a call to its `.to_typed()`
        // method.
        let ty = &field.ast().ty;
        cg::add_predicate(where_clause, parse_quote!(#ty: style_traits::ToTyped));

        quote! {
           style_traits::ToTyped::to_typed(#field, dest)?;
        }
    };

    if let Some(condition) = field_attrs.skip_if {
        expr = quote! {
            if !#condition(#field) {
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
}
