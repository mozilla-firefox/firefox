// /* This Source Code Form is subject to the terms of the Mozilla Public
//  * License, v. 2.0. If a copy of the MPL was not distributed with this
//  * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
use super::{common, ConcreteCodeOracle};
use std::borrow::Borrow;
use std::fmt::{self, Display};

use crate::backends::{CodeOracle, LiteralRenderer, TypeIdentifier};
use crate::intermediate_representation::{Literal, PrefBranch};

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
    P: Display,
    V: Display,
    D: Display,
{
    let oracle = &ConcreteCodeOracle;
    let ct = oracle.find(type_.borrow());
    Ok(ct.property_getter(oracle, &vars, &prop, &default))
}

#[askama::filter_fn]
pub fn preference_getter<T, P, K>(
    type_: T,
    _: &dyn askama::Values,
    prefs: P,
    pref_key: K,
) -> Result<String, askama::Error>
where
    T: Borrow<TypeIdentifier>,
    P: fmt::Display,
    K: fmt::Display,
{
    let oracle = &ConcreteCodeOracle;
    let ct = oracle.find(type_.borrow());
    if let Some(getter) = ct.preference_getter(oracle, &prefs, &pref_key) {
        Ok(getter)
    } else {
        unreachable!("The preference for type {} isn't available. This is a bug in Nimbus FML Kotlin generator", type_.borrow());
    }
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

/// Get the idiomatic Kotlin rendering of a class name (for enums, records, errors, etc).
#[askama::filter_fn]
pub fn class_name<N>(nm: N, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    N: fmt::Display,
{
    Ok(common::class_name(&nm))
}

/// Get the idiomatic Kotlin rendering of a variable name.
#[askama::filter_fn]
pub fn var_name<N>(nm: N, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    N: fmt::Display,
{
    Ok(common::var_name(&nm))
}

/// Get the idiomatic Kotlin rendering of an individual enum variant.
#[askama::filter_fn]
pub fn enum_variant_name<F>(nm: F, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    F: fmt::Display,
{
    Ok(common::enum_variant_name(&nm))
}

#[askama::filter_fn]
pub fn comment<T>(txt: T, _: &dyn askama::Values, spaces: &str) -> Result<String, askama::Error>
where
    T: fmt::Display,
{
    use textwrap::{fill, Options};

    let indent_start = "/** ".to_string();
    let indent_mid = format!("{} * ", spaces);
    let indent_end = format!("{} */", spaces);

    let options = Options::new(80)
        .initial_indent(&indent_mid)
        .subsequent_indent(&indent_mid);

    let lines = fill(txt.to_string().as_str(), options);
    Ok(format!(
        "{start}\n{lines}\n{indent}",
        start = indent_start,
        lines = lines,
        indent = indent_end
    ))
}

#[askama::filter_fn]
pub fn quoted<T>(txt: T, _: &dyn askama::Values) -> Result<String, askama::Error>
where
    T: fmt::Display,
{
    Ok(common::quoted(&txt))
}

#[askama::filter_fn]
pub fn pref_branch_string(
    pref_branch: PrefBranch,
    _: &dyn askama::Values,
) -> Result<String, askama::Error> {
    Ok(match pref_branch {
        PrefBranch::Default => "PrefBranch.DEFAULT",
        PrefBranch::User => "PrefBranch.USER",
    }
    .into())
}
