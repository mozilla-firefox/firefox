/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;

pub fn map_namespace(input: general::Namespace, context: &Context) -> Result<Namespace> {
    let mut context = context.clone();
    context.update_from_namespace(&input);

    let config = context.current_namespace_config()?;
    callables::check_for_unconfigured_callables(&input, &config)?;
    let imports = config
        .custom_types
        .values()
        .flat_map(|c| c.imports.clone())
        .collect();

    Ok(Namespace {
        name: input.name,
        docstring: input.docstring,
        functions: input.functions.map_node(&context)?,
        type_definitions: input.type_definitions.map_node(&context)?,
        builtin_types: context.builtin_types()?,
        imports,
    })
}

impl Namespace {
    pub fn is_fixture(&self) -> bool {
        is_fixture_namespace(&self.name)
    }

    pub fn js_name(&self) -> String {
        format!("Rust{}", self.name.to_upper_camel_case())
    }

    pub fn js_filename(&self) -> String {
        format!("{}.sys.mjs", self.js_name())
    }
}

pub fn is_fixture_namespace(namespace_name: &str) -> bool {
    namespace_name.starts_with("uniffi_bindings_tests")
}

pub fn format_module_name(source_name: &str) -> String {
    format!("Rust{}", source_name.to_upper_camel_case())
}
