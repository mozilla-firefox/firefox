/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;
use indexmap::IndexMap;
use std::collections::BTreeMap;

pub fn map_root(input: general::Root, context: &Context) -> Result<Root> {
    let mut context = context.clone();
    context.update_from_root(&input)?;

    let mut builder = RootBuilder::new(input, &context)?;
    builder.populate_pointer_types(&context)?;
    builder.populate_scaffolding_calls(&context)?;
    builder.populate_callback_interfaces(&context)?;
    builder.populate_callback_return_handlers();

    let libs: Vec<LibraryRoot> = builder.libs.into_values().collect();
    let ffi_definition_blocks = builder.ffi_definition_blocks;
    let mut module_docs = vec![];
    for lib in libs
        .iter()
        .filter(|lib| !lib.group.contains(LibraryGroup::FIXTURES))
    {
        module_docs.extend(docs::api_module_docs(&lib.namespaces)?);
    }

    Ok(Root {
        libs,
        ffi_definition_blocks,
        module_docs,
    })
}

/// Builds up the LibraryRoot instances for the root node
///
/// This process gets its own struct because the steps depend on each other in tricky ways.
struct RootBuilder {
    libs: BTreeMap<LibraryGroup, LibraryRoot>,
    ffi_definition_blocks: Vec<FfiDefinitionBlock>,
}

impl RootBuilder {
    fn new(input: general::Root, context: &Context) -> Result<Self> {
        let input_namespaces: Vec<_> = input
            .namespaces
            .into_values()
            .map(|namespace| (namespaces::library_group(&namespace, context), namespace))
            .collect();

        let mut ffi_definition_groups: IndexMap<&general::FfiDefinition, LibraryGroup> =
            IndexMap::new();
        for (group, namespace) in &input_namespaces {
            namespace.visit(|def: &general::FfiDefinition| {
                *ffi_definition_groups
                    .entry(def)
                    .or_insert(LibraryGroup::empty()) |= *group;
            });
        }

        let mut builder = Self {
            libs: BTreeMap::new(),
            ffi_definition_blocks: vec![],
        };
        for (def, group) in ffi_definition_groups {
            let def = def.clone().map_node(context)?;
            match builder.ffi_definition_blocks.last_mut() {
                Some(block) if block.group == group => block.definitions.push(def),
                _ => builder.ffi_definition_blocks.push(FfiDefinitionBlock {
                    group,
                    definitions: vec![def],
                }),
            }
        }
        for (group, namespace) in input_namespaces {
            builder
                .lib_mut(group)
                .namespaces
                .push(namespace.map_node(context)?);
        }
        Ok(builder)
    }

    fn lib_mut(&mut self, group: LibraryGroup) -> &mut LibraryRoot {
        self.libs
            .entry(group)
            .or_insert_with(|| LibraryRoot::new(group))
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
        let ffi_func_map: HashMap<String, FfiFunction> = self
            .ffi_definition_blocks
            .iter()
            .flat_map(|block| &block.definitions)
            .filter_map(|def| match def {
                FfiDefinition::RustFunction(ffi_func) => {
                    Some((ffi_func.name.0.clone(), ffi_func.clone()))
                }
                _ => None,
            })
            .collect();
        let mut seen = HashSet::new();
        for lib in self.libs_mut() {
            let mut calls = vec![];
            lib.namespaces.try_visit(|callable: &Callable| {
                // Callback interface methods don't have scaffolding functions associated with them
                if matches!(callable.kind, CallableKind::VTableMethod { .. }) {
                    return Ok(());
                }
                if !seen.insert(callable.id) {
                    return Ok(());
                }

                let ffi_func = ffi_func_map.get(&callable.ffi_func.0).ok_or_else(|| {
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
        let mut handlers: IndexMap<String, (LibraryGroup, CppCallbackReturnHandlerClass)> =
            IndexMap::new();
        for lib in self.libs.values() {
            lib.callback_interfaces
                .visit(|meth: &CppCallbackInterfaceMethod| {
                    handlers
                        .entry(meth.return_handler_class_name.clone())
                        .or_insert_with(|| {
                            (
                                LibraryGroup::empty(),
                                CppCallbackReturnHandlerClass {
                                    name: meth.return_handler_class_name.clone(),
                                    return_ty: meth.return_ty.clone(),
                                },
                            )
                        })
                        .0 |= lib.group;
                });
        }
        for (_, (group, handler)) in handlers {
            self.lib_mut(group)
                .cpp_callback_return_handlers
                .push(handler);
        }
    }

    fn libs_mut(&mut self) -> impl Iterator<Item = &mut LibraryRoot> {
        self.libs.values_mut()
    }
}

impl LibraryRoot {
    fn new(group: LibraryGroup) -> Self {
        Self {
            group,
            namespaces: vec![],
            scaffolding_calls: vec![],
            pointer_types: vec![],
            callback_interfaces: vec![],
            cpp_callback_return_handlers: vec![],
        }
    }

    pub fn ifdef_start(&self) -> String {
        self.group.ifdef_start()
    }

    pub fn ifdef_end(&self) -> String {
        self.group.ifdef_end()
    }
}

impl FfiDefinitionBlock {
    pub fn ifdef_start(&self) -> String {
        self.group.ifdef_start()
    }

    pub fn ifdef_end(&self) -> String {
        self.group.ifdef_end()
    }
}

impl LibraryGroup {
    /// Preprocessor condition for this group, or `None` if it's compiled into every build
    fn condition(self) -> Option<String> {
        const GECKOVIEW: &str = "defined(MOZ_WIDGET_ANDROID) || defined(XP_IOS)";
        if self.contains(Self::ALL_PLATFORMS) {
            return None;
        }
        let mut terms = vec![];
        if self.contains(Self::DESKTOP) {
            terms.push(format!("!({GECKOVIEW})"));
        } else if self.contains(Self::GECKOVIEW) {
            terms.push(GECKOVIEW.to_string());
        }
        if self.contains(Self::FIXTURES) {
            terms.push("defined(MOZ_UNIFFI_FIXTURES)".to_string());
        }
        Some(match terms.as_slice() {
            [term] => term.clone(),
            _ => terms
                .iter()
                .map(|term| format!("({term})"))
                .collect::<Vec<_>>()
                .join(" || "),
        })
    }

    /// #if statement for this group
    fn ifdef_start(self) -> String {
        match self.condition() {
            Some(condition) => format!("#if {condition}"),
            None => String::new(),
        }
    }

    /// Close the `#if` from [Self::ifdef_start]
    fn ifdef_end(self) -> String {
        match self.condition() {
            Some(condition) => format!("#endif /* {condition} */"),
            None => String::new(),
        }
    }
}

impl Root {
    pub fn libraries(&self) -> impl Iterator<Item = &LibraryRoot> {
        self.libs.iter()
    }

    pub fn namespaces(&self) -> impl Iterator<Item = &Namespace> {
        self.libraries().flat_map(|lib| &lib.namespaces)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const D: LibraryGroup = LibraryGroup::DESKTOP;
    const G: LibraryGroup = LibraryGroup::GECKOVIEW;
    const F: LibraryGroup = LibraryGroup::FIXTURES;

    #[test]
    fn condition() {
        let cases = [
            (D | G, None),
            (D, Some("!(defined(MOZ_WIDGET_ANDROID) || defined(XP_IOS))")),
            (G, Some("defined(MOZ_WIDGET_ANDROID) || defined(XP_IOS)")),
            (D | G | F, None),
            (F, Some("defined(MOZ_UNIFFI_FIXTURES)")),
            (
                D | F,
                Some("(!(defined(MOZ_WIDGET_ANDROID) || defined(XP_IOS))) || (defined(MOZ_UNIFFI_FIXTURES))"),
            ),
            (
                G | F,
                Some("(defined(MOZ_WIDGET_ANDROID) || defined(XP_IOS)) || (defined(MOZ_UNIFFI_FIXTURES))"),
            ),
        ];
        for (group, expected) in cases {
            assert_eq!(group.condition().as_deref(), expected, "{:?}", group);
        }
    }
}
