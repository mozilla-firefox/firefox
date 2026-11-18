/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;

pub fn map_root(input: general::Root, context: &Context) -> Result<Root> {
    let mut context = context.clone();
    context.update_from_root(&input)?;

    let mut builder = RootBuilder::new(input, &context)?;
    builder.populate_pointer_types(&context)?;
    builder.populate_scaffolding_calls(&context)?;
    builder.populate_callback_interfaces(&context)?;
    builder.populate_callback_return_handlers();

    let module_docs = docs::api_module_docs(&builder.lib.namespaces)?;

    Ok(Root {
        lib: builder.lib,
        fixtures_lib: builder.fixtures_lib,
        module_docs,
    })
}

/// Builds up the 2 LibraryRoot instances for the root node
///
/// This process gets its own struct because the steps depend on each other in tricky ways.
struct RootBuilder {
    lib: LibraryRoot,
    fixtures_lib: LibraryRoot,
}

impl RootBuilder {
    fn new(input: general::Root, context: &Context) -> Result<Self> {
        let mut ffi_definitions = vec![];
        let mut fixtures_ffi_definitions = vec![];
        let mut seen_ffi_definitions = HashSet::new();

        for namespace in input.namespaces.values() {
            namespace.try_visit(|def: &general::FfiDefinition| {
                if seen_ffi_definitions.insert(def) {
                    let def = def.clone().map_node(context)?;
                    if namespaces::is_fixture_namespace(&namespace.name) {
                        fixtures_ffi_definitions.push(def);
                    } else {
                        ffi_definitions.push(def);
                    }
                }
                Ok(())
            })?;
        }

        let mut namespaces = vec![];
        let mut fixture_namespaces = vec![];
        for namespace in input.namespaces.into_values() {
            let namespace = namespace.map_node(context)?;
            if namespaces::is_fixture_namespace(&namespace.name) {
                fixture_namespaces.push(namespace);
            } else {
                namespaces.push(namespace);
            }
        }

        let lib = LibraryRoot {
            namespaces,
            is_fixtures: false,
            ffi_definitions,
            // These will be populated by the other methods
            scaffolding_calls: vec![],
            pointer_types: vec![],
            callback_interfaces: vec![],
            cpp_callback_return_handlers: vec![],
        };
        let fixtures_lib = LibraryRoot {
            namespaces: fixture_namespaces,
            is_fixtures: true,
            ffi_definitions: fixtures_ffi_definitions,
            scaffolding_calls: vec![],
            pointer_types: vec![],
            callback_interfaces: vec![],
            cpp_callback_return_handlers: vec![],
        };
        Ok(Self { lib, fixtures_lib })
    }

    fn populate_pointer_types(&mut self, context: &Context) -> Result<()> {
        for lib in self.libs_mut() {
            let mut pointer_types = vec![];
            for namespace in lib.namespaces.iter() {
                namespace.try_visit(|int: &Interface| {
                    let id = context.pointer_id(int.self_type.id)?;
                    let type_id = int.self_type.id;
                    pointer_types.push(PointerType {
                        id,
                        name: format!("kPointerType{id}"),
                        ffi_value_class: format!("FfiValueObjectHandle{type_id}"),
                        label: format!("{}::{}", namespace.name, int.name),
                        ffi_func_clone: int.ffi_func_clone.clone(),
                        ffi_func_free: int.ffi_func_free.clone(),
                        trait_interface_info: match int.vtable {
                            None => None,
                            Some(_) => {
                                let callback_id =
                                    context.callback_interface_id(int.self_type.id)?;
                                Some(PointerTypeTraitInterfaceInfo {
                                    clone_fn: format!("callback_clone_{callback_id}"),
                                    free_fn: format!("callback_free_{callback_id}"),
                                })
                            }
                        },
                    });
                    Ok(())
                })?;
            }
            pointer_types.sort_by_key(|p| p.id);
            lib.pointer_types = pointer_types;
        }
        Ok(())
    }

    fn populate_scaffolding_calls(&mut self, context: &Context) -> Result<()> {
        let mut seen = HashSet::new();
        for lib in self.libs_mut() {
            let mut calls = vec![];
            let ffi_func_map: HashMap<&String, &FfiFunction> = lib
                .ffi_definitions
                .iter()
                .filter_map(|def| match def {
                    FfiDefinition::RustFunction(ffi_func) => Some((&ffi_func.name.0, ffi_func)),
                    _ => None,
                })
                .collect();
            lib.namespaces.try_visit(|callable: &Callable| {
                // Callback interface methods don't have scaffolding functions associated with them
                if matches!(callable.kind, CallableKind::VTableMethod { .. }) {
                    return Ok(());
                }
                if !seen.insert(callable.id) {
                    return Ok(());
                }

                let ffi_func = *ffi_func_map.get(&callable.ffi_func.0).ok_or_else(|| {
                    anyhow!(
                        "`populate_scaffolding_calls`: failed to find ffi function: {}",
                        callable.ffi_func.0,
                    )
                })?;

                let mut arguments = match &callable.kind {
                    CallableKind::Method { self_type, .. } => {
                        let ty = self_type.clone();
                        vec![Argument {
                            name: "uniffi_ptr".to_string(),
                            ffi_value_class: ffi_types::ffi_value_class(&ty)?,
                            ty,
                            by_ref: true,
                            optional: false,
                            default: None,
                        }]
                    }
                    _ => vec![],
                };
                arguments.extend(callable.arguments.clone());

                calls.push(ScaffoldingCall {
                    id: context.map_callable_id(callable.id),
                    ffi_func: ffi_func.clone(),
                    arguments,
                    return_ty: callable.return_type.clone(),
                });
                Ok(())
            })?;
            calls.sort_by_key(|c| c.id);
            lib.scaffolding_calls = calls;
        }
        Ok(())
    }

    fn populate_callback_interfaces(&mut self, context: &Context) -> Result<()> {
        for lib in self.libs_mut() {
            let mut callback_interfaces = vec![];
            lib.namespaces.try_visit(|cbi: &CallbackInterface| {
                callback_interfaces.push(callback_interfaces::cpp_callback_interface(
                    &cbi.name,
                    &cbi.self_type,
                    &cbi.vtable,
                    context,
                )?);
                Ok(())
            })?;

            lib.namespaces.try_visit(|int: &Interface| {
                if let Some(vtable) = &int.vtable {
                    callback_interfaces.push(callback_interfaces::cpp_callback_interface(
                        &int.name,
                        &int.self_type,
                        vtable,
                        context,
                    )?);
                }
                Ok(())
            })?;
            lib.callback_interfaces = callback_interfaces;
        }
        Ok(())
    }

    fn populate_callback_return_handlers(&mut self) {
        let mut seen = HashSet::new();

        for lib in self.libs_mut() {
            let mut return_handlers = vec![];
            lib.callback_interfaces
                .visit(|meth: &CppCallbackInterfaceMethod| {
                    if seen.insert(&meth.return_handler_class_name) {
                        return_handlers.push(CppCallbackReturnHandlerClass {
                            name: meth.return_handler_class_name.clone(),
                            return_ty: meth.return_ty.clone(),
                        });
                    }
                });
            lib.cpp_callback_return_handlers = return_handlers;
        }
    }

    fn libs_mut(&mut self) -> impl Iterator<Item = &mut LibraryRoot> {
        std::iter::once(&mut self.lib).chain(std::iter::once(&mut self.fixtures_lib))
    }
}

impl LibraryRoot {
    /// #ifdef statement for this library root
    ///
    /// For the fixtures library is an #ifdef statement that only enables the code when
    /// `MOZ_UNIFFI_FIXTURES` is present.  For the regular library it's the empty string
    pub fn ifdef_start(&self) -> &'static str {
        if self.is_fixtures {
            "#ifdef MOZ_UNIFFI_FIXTURES"
        } else {
            ""
        }
    }

    /// Close the `#ifdef` from [Self::ifdef_start]
    pub fn ifdef_end(&self) -> &'static str {
        if self.is_fixtures {
            "#endif /* MOZ_UNIFFI_FIXTURES */"
        } else {
            ""
        }
    }
}

impl Root {
    pub fn libraries(&self) -> impl Iterator<Item = &LibraryRoot> {
        std::iter::once(&self.lib).chain(std::iter::once(&self.fixtures_lib))
    }

    pub fn namespaces(&self) -> impl Iterator<Item = &Namespace> {
        self.libraries().flat_map(|lib| &lib.namespaces)
    }
}
