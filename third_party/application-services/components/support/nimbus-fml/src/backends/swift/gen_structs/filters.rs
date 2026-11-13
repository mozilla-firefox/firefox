// /* This Source Code Form is subject to the terms of the Mozilla Public
//  * License, v. 2.0. If a copy of the MPL was not distributed with this
//  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{common, ConcreteCodeOracle};
use std::borrow::Borrow;
use std::fmt::{self, Display};

use crate::backends::{CodeOracle, LiteralRenderer, TypeIdentifier};
use crate::intermediate_representation::Literal;

#[askama::filter_fn]
pub fn type_label<T>(type_: T, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    T: Borrow<TypeIdentifier>,
{
    let oracle = ConcreteCodeOracle;
    Ok(oracle.find(type_.borrow()).type_label(&oracle))
}

#[askama::filter_fn]
pub fn defaults_type_label<T>(type_: T, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    T: Borrow<TypeIdentifier>,
{
    let oracle = ConcreteCodeOracle;
    Ok(oracle.find(type_.borrow()).defaults_type(&oracle))
}

#[askama::filter_fn]
pub fn literal<T, R, L, C>(
    type_: T,
    _: &dyn askama::Values,
    renderer: R,
    literal: L,
    ctx: C,
) -> Result<String, askama::Error>
where
    T: Borrow<TypeIdentifier>,
    R: LiteralRenderer,
    L: Borrow<Literal>,
    C: Display,
{
    let oracle = ConcreteCodeOracle;
    Ok(oracle
        .find(type_.borrow())
        .literal(&oracle, &ctx, &renderer, literal.borrow()))
}

#[askama::filter_fn]
pub fn property<T, P, V, D>(
    type_: T,
    _: &dyn askama::Values,
    prop: P,
    vars: V,
    default: D,
) -> Result<String, askama::Error>
where
    T: Borrow<TypeIdentifier>,
    P: fmt::Display,
    V: fmt::Display,
    D: fmt::Display,
{
    let oracle = &ConcreteCodeOracle;
    let ct = oracle.find(type_.borrow());
    Ok(ct.property_getter(oracle, &vars, &prop, &default))
}

#[askama::filter_fn]
pub fn to_json<P, T>(prop: P, _: &dyn askama::Values, type_: T) -> Result<String, askama::Error>
where
    P: fmt::Display,
    T: Borrow<TypeIdentifier>,
{
    let oracle = &ConcreteCodeOracle;
    let ct = oracle.find(type_.borrow());
    Ok(ct.as_json(oracle, &prop))
}

/// Get the idiomatic Swift rendering of a class name (for enums, records, errors, etc).
#[askama::filter_fn]
pub fn class_name<N>(nm: N, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    N: fmt::Display,
{
    Ok(common::class_name(&nm))
}

/// Get the idiomatic Swift rendering of a variable name.
#[askama::filter_fn]
pub fn var_name<N>(nm: N, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    N: fmt::Display,
{
    Ok(common::var_name(&nm))
}

/// Get the idiomatic Swift rendering of an individual enum variant.
#[askama::filter_fn]
pub fn enum_variant_name<N>(nm: N, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    N: fmt::Display,
{
    Ok(common::enum_variant_name(&nm))
}

#[askama::filter_fn]
pub fn comment<T>(txt: T, _: &dyn askama::Values, spaces: &str) -> Result<String, askama::Error>
where
    T: fmt::Display,
{
    use textwrap::{fill, Options};

    let indent1 = "/// ".to_string();
    let indent2 = format!("{} /// ", spaces);

    let options = Options::new(80)
        .initial_indent(&indent1)
        .subsequent_indent(&indent2);

    let lines = fill(txt.to_string().as_str(), options);
    Ok(lines)
}

#[askama::filter_fn]
pub fn quoted<T>(txt: T, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    T: fmt::Display,
{
    Ok(common::quoted(&txt))
}

#[cfg(test)]
mod json_tests {
    use crate::intermediate_representation::TypeRef;
    use askama::Template;

    use super::*;
    use crate::backends::swift::gen_structs::filters;
    use askama::Error;

    fn run_to_json(prop: impl Display, ty: TypeIdentifier) -> String {
        #[derive(Template)]
        #[template(source = "{{ prop|to_json(ty) }}", ext = "txt")]
        struct ToJsonTemplate {
            prop: String,
            ty: TypeIdentifier,
        }

        ToJsonTemplate {
            prop: prop.to_string(),
            ty,
        }
        .render()
        .expect("Error rendering ToJsonTemplate")
    }

    #[test]
    fn scalar_types() -> Result<(), Error> {
        let p = "prop";

        assert_eq!(run_to_json(p, TypeRef::Boolean), format!("{p}"));
        assert_eq!(run_to_json(p, TypeRef::Int), format!("{p}"));
        assert_eq!(run_to_json(p, TypeRef::String), format!("{p}"));
        assert_eq!(
            run_to_json(p, TypeRef::StringAlias("Name".to_string())),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(p, TypeRef::Enum("Name".to_string())),
            format!("{p}.rawValue")
        );
        Ok(())
    }

    #[test]
    fn bundled_types() -> Result<(), Error> {
        let p = "prop";
        assert_eq!(run_to_json(p, TypeRef::BundleText), format!("{p}"));
        assert_eq!(
            run_to_json(p, TypeRef::BundleImage),
            format!("{p}.encodableImageName")
        );

        Ok(())
    }

    #[test]
    fn optional_types() -> Result<(), Error> {
        let p = "prop";
        assert_eq!(
            run_to_json(p, TypeRef::Option(Box::new(TypeRef::String))),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(p, TypeRef::Option(Box::new(TypeRef::BundleImage))),
            format!("{p}?.encodableImageName")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::Option(Box::new(TypeRef::Enum("Name".to_string())))
            ),
            format!("{p}?.rawValue")
        );
        Ok(())
    }

    #[test]
    fn list_types() -> Result<(), Error> {
        let p = "prop";
        assert_eq!(
            run_to_json(p, TypeRef::List(Box::new(TypeRef::String))),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(p, TypeRef::List(Box::new(TypeRef::BundleImage))),
            format!("{p}.map {{ $0.encodableImageName }}")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::List(Box::new(TypeRef::Enum("Name".to_string())))
            ),
            format!("{p}.map {{ $0.rawValue }}")
        );
        Ok(())
    }

    #[test]
    fn string_map_types() -> Result<(), Error> {
        let p = "prop";
        assert_eq!(
            run_to_json(p, TypeRef::StringMap(Box::new(TypeRef::String))),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(p, TypeRef::StringMap(Box::new(TypeRef::BundleImage))),
            format!("{p}.mapValuesNotNull {{ $0.encodableImageName }}")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::StringMap(Box::new(TypeRef::Enum("Name".to_string())))
            ),
            format!("{p}.mapValuesNotNull {{ $0.rawValue }}")
        );

        assert_eq!(
            run_to_json(
                p,
                TypeRef::StringMap(Box::new(TypeRef::Option(Box::new(TypeRef::String))))
            ),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::StringMap(Box::new(TypeRef::Option(Box::new(TypeRef::BundleImage))))
            ),
            format!("{p}.mapValuesNotNull {{ $0?.encodableImageName }}")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::StringMap(Box::new(TypeRef::Option(Box::new(TypeRef::Enum(
                    "Name".to_string()
                )))))
            ),
            format!("{p}.mapValuesNotNull {{ $0?.rawValue }}")
        );
        Ok(())
    }

    #[test]
    fn enum_map_types_keys() -> Result<(), Error> {
        let p = "prop";
        assert_eq!(
            run_to_json(
                p,
                TypeRef::EnumMap(Box::new(TypeRef::String), Box::new(TypeRef::String))
            ),
            format!("{p}")
        );
        assert_eq!(
            run_to_json(
                p,
                TypeRef::EnumMap(
                    Box::new(TypeRef::StringAlias("Name".to_string())),
                    Box::new(TypeRef::String)
                )
            ),
            format!("{p}")
        );

        // Mapping keys for enums. We do this because Swift encodes Dictionary<Enum, T> as
        // an array: [k0, v0, k1, v1]. Bizarre!
        assert_eq!(
            run_to_json(
                p,
                TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("Name".to_string())),
                    Box::new(TypeRef::String)
                )
            ),
            format!("{p}.mapKeysNotNull {{ $0.rawValue }}")
        );
        Ok(())
    }

    #[test]
    fn enum_map_types_keys_and_values() -> Result<(), Error> {
        let p = "prop";

        // Mapping keys for enums. We do this because Swift encodes Dictionary<Enum, T> as
        // an array: [k0, v0, k1, v1]. Bizarre!
        // Map<Enum, Image>
        assert_eq!(
            run_to_json(
                p,
                TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("Name".to_string())),
                    Box::new(TypeRef::BundleImage)
                )
            ),
            format!("prop.mapEntriesNotNull({{ $0.rawValue }}, {{ $0.encodableImageName }})")
        );

        // Map<Enum, List<String>>; we don't need to map the values, because they encode cleanly.
        assert_eq!(
            run_to_json(
                p,
                TypeRef::EnumMap(
                    Box::new(TypeRef::Enum("Name".to_string())),
                    Box::new(TypeRef::List(Box::new(TypeRef::String)))
                )
            ),
            format!("prop.mapKeysNotNull {{ $0.rawValue }}")
        );

        // Map<Enum, List<Image>>
        assert_eq!(
            run_to_json(p, TypeRef::EnumMap(Box::new(TypeRef::Enum("Name".to_string())), Box::new(TypeRef::List(Box::new(TypeRef::BundleImage))))),
            format!("prop.mapEntriesNotNull({{ $0.rawValue }}, {{ $0.map {{ $0.encodableImageName }} }})")
        );
        Ok(())
    }
}
