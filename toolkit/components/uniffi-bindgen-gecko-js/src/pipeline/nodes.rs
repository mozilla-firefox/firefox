/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::*;
pub use crate::{filters, ConcurrencyMode};
use askama::Template;
use uniffi_bindgen::pipeline::general;

uniffi_pipeline::use_prev_node!(general::AsyncData);
uniffi_pipeline::use_prev_node!(general::EnumShape);
uniffi_pipeline::use_prev_node!(general::FieldsKind);
uniffi_pipeline::use_prev_node!(general::FfiFunctionKind);
uniffi_pipeline::use_prev_node!(general::FfiFunctionTypeName);
uniffi_pipeline::use_prev_node!(general::FfiStructName);
uniffi_pipeline::use_prev_node!(general::FfiType);
uniffi_pipeline::use_prev_node!(general::HandleKind);
uniffi_pipeline::use_prev_node!(general::ObjectImpl);
uniffi_pipeline::use_prev_node!(general::Radix);
uniffi_pipeline::use_prev_node!(general::RustFfiFunctionName);
uniffi_pipeline::use_prev_node!(general::TraitKind);
uniffi_pipeline::use_prev_node!(general::Type);

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Root))]
#[map_node(root::map_root)]
pub struct Root {
    pub lib: LibraryRoot,
    pub fixtures_lib: LibraryRoot,
    pub module_docs: Vec<ApiModuleDocs>,
}

/// Root node for a single library
///
/// We create one of these for the fixture crates and one for non-fixture crates
#[derive(Debug, Clone, Node)]
pub struct LibraryRoot {
    // Is this for the fixtures library?
    pub is_fixtures: bool,
    // Namespaces for Rust crates
    pub namespaces: Vec<Namespace>,
    pub scaffolding_calls: Vec<ScaffoldingCall>,
    pub pointer_types: Vec<PointerType>,
    pub callback_interfaces: Vec<CppCallbackInterface>,
    // FFI definitions to define for this library
    //
    // Note: for the fixtures library this will exclude FFI definitions that are also in the
    // regular library to avoid duplicate definitions.
    pub ffi_definitions: Vec<FfiDefinition>,
    // Callback return handler classes to define for this library
    //
    // Note: for the fixtures library this will exclude FFI definitions that are also in the
    // regular library to avoid duplicate definitions.
    pub cpp_callback_return_handlers: Vec<CppCallbackReturnHandlerClass>,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::BuiltinTypes))]
pub struct BuiltinTypes {
    pub u8: TypeNode,
    pub i8: TypeNode,
    pub u16: TypeNode,
    pub i16: TypeNode,
    pub u32: TypeNode,
    pub i32: TypeNode,
    pub u64: TypeNode,
    pub i64: TypeNode,
    pub f32: TypeNode,
    pub f64: TypeNode,
    pub string: TypeNode,
}

/// Crate which exposes a uniffi api.
#[derive(Debug, Clone, Node, MapNode, Template)]
#[map_node(from(general::Namespace))]
#[map_node(namespaces::map_namespace)]
#[template(path = "js/Module.sys.mjs", escape = "none")]
pub struct Namespace {
    pub name: String,
    pub docstring: Option<String>,
    pub functions: Vec<Function>,
    pub type_definitions: Vec<TypeDefinition>,
    pub builtin_types: BuiltinTypes,
    pub imports: Vec<String>,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::TypeDefinition))]
pub enum TypeDefinition {
    Interface(Interface),
    CallbackInterface(CallbackInterface),
    Record(Record),
    Enum(Enum),
    Custom(CustomType),
    Simple(TypeNode),
    Optional(OptionalType),
    Sequence(SequenceType),
    Map(MapType),
    Set(SetType),
    Box(BoxedType),
    External(ExternalType),
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Record))]
#[map_node(records::map_record)]
pub struct Record {
    pub name: String,
    pub fields: Vec<Field>,
    pub fields_kind: FieldsKind,
    pub docstring: Option<String>,
    pub js_docstring: String,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, Node)]
pub struct Field {
    pub name: String,
    pub ty: TypeNode,
    pub default: Option<DefaultValueNode>,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Enum))]
#[map_node(enums::map_enum)]
pub struct Enum {
    pub name: String,
    pub is_flat: bool,
    pub shape: EnumShape,
    pub variants: Vec<Variant>,
    pub discr_type: TypeNode,
    pub js_docstring: String,
    pub docstring: Option<String>,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Variant))]
#[map_node(enums::map_variant)]
pub struct Variant {
    pub name: String,
    pub discr: LiteralNode,
    pub fields: Vec<Field>,
    pub fields_kind: FieldsKind,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Interface))]
#[map_node(interfaces::map_interface)]
pub struct Interface {
    pub name: String,
    pub pointer_id: u64,
    pub js_class_name: String,
    pub interface_base_class: InterfaceBaseClass,
    pub constructors: Vec<Constructor>,
    pub methods: Vec<Method>,
    pub uniffi_trait_methods: UniffiTraitMethods,
    pub trait_impls: Vec<ObjectTraitImpl>,
    pub imp: ObjectImpl,
    pub docstring: Option<String>,
    pub js_docstring: String,
    pub self_type: TypeNode,
    pub vtable: Option<VTable>,
    pub ffi_func_clone: RustFfiFunctionName,
    pub ffi_func_free: RustFfiFunctionName,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::CallbackInterface))]
#[map_node(callback_interfaces::map_callback_interface)]
pub struct CallbackInterface {
    pub name: String,
    pub interface_base_class: InterfaceBaseClass,
    pub vtable: VTable,
    pub docstring: Option<String>,
    pub js_docstring: String,
    pub self_type: TypeNode,
}

/// Javascript interface class.
///
/// This is an abstract base class that the interface implements.
/// For trait/callback interfaces this is what the JS code should extend.
#[derive(Debug, Clone, Node)]
pub struct InterfaceBaseClass {
    pub name: String,
    pub methods: Vec<Method>,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::CustomType))]
#[map_node(custom::map_custom_type)]
pub struct CustomType {
    pub name: String,
    pub builtin: TypeNode,
    pub docstring: Option<String>,
    pub js_docstring: String,
    pub self_type: TypeNode,
    pub type_name: Option<String>,
    pub lift_expr: Option<String>,
    pub lower_expr: Option<String>,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::OptionalType))]
pub struct OptionalType {
    pub inner: TypeNode,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::SequenceType))]
pub struct SequenceType {
    pub inner: TypeNode,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::MapType))]
pub struct MapType {
    pub key: TypeNode,
    pub value: TypeNode,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::SetType))]
pub struct SetType {
    pub inner: TypeNode,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::BoxedType))]
pub struct BoxedType {
    pub inner: TypeNode,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::ExternalType))]
#[map_node(types::map_external_type)]
pub struct ExternalType {
    pub namespace: String,
    pub name: String,
    pub self_type: TypeNode,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::UniffiTraitMethods))]
pub struct UniffiTraitMethods {
    pub debug_fmt: Option<Method>,
    pub display_fmt: Option<Method>,
    pub eq_eq: Option<Method>,
    pub eq_ne: Option<Method>,
    pub hash_hash: Option<Method>,
    pub ord_cmp: Option<Method>,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::ObjectTraitImpl))]
pub struct ObjectTraitImpl {
    pub ty: TypeNode,
    pub trait_ty: TypeNode,
}

// A `PointerType` const to define in the C++ code
#[derive(Debug, Clone, Node)]
pub struct PointerType {
    pub id: u64,
    pub name: String,
    pub label: String,
    pub ffi_value_class: String,
    pub ffi_func_clone: RustFfiFunctionName,
    pub ffi_func_free: RustFfiFunctionName,
    pub trait_interface_info: Option<PointerTypeTraitInterfaceInfo>,
}

#[derive(Debug, Clone, Node)]
pub struct PointerTypeTraitInterfaceInfo {
    pub free_fn: String,
    pub clone_fn: String,
}

// A Scaffolding call implemented in the C++ code
#[derive(Debug, Clone, Node)]
pub struct ScaffoldingCall {
    pub id: u64,
    pub ffi_func: FfiFunction,
    pub arguments: Vec<Argument>,
    pub return_ty: Option<ReturnType>,
}

// Used to generate the C++ callback interface code
#[derive(Debug, Clone, Node)]
pub struct CppCallbackInterface {
    pub id: u64,
    pub name: String,
    /// C++ class that handles:
    ///   - Lowering the JS value, storing it, then passing the value to Rust
    ///   - Storing values from Rust, then lifting them to JS
    ///   - Cleaning up the stored value when we fail to lower/lift other values.
    ///
    /// This is only generated for regular callback interfaces.  For trait interfaces, the FfiValue
    /// class is defined in `PointerType.cpp`
    pub ffi_value_class: Option<String>,
    /// Name of the C++ variable that stores the UniFFICallbackHandler instance
    pub handler_var: String,
    // Name of the C++ static variable for the VTable
    pub vtable_var: String,
    /// Rust scaffolding function to initialize the VTable
    pub init_fn: RustFfiFunctionName,
    /// Name of the function generated by uniffi-bindgen-gecko-js to free a callback interface
    /// handle.
    pub free_fn: String,
    /// Name of the function generated by uniffi-bindgen-gecko-js to clone a callback interface
    /// handle.
    pub clone_fn: String,
    pub vtable_struct_type: FfiTypeNode,
    pub methods: Vec<CppCallbackInterfaceMethod>,
}

// Used to generate the C++ code to handle a callback method
#[derive(Debug, Clone, Node)]
pub struct CppCallbackInterfaceMethod {
    /// Name of the handler function
    pub fn_name: String,
    pub kind: CallbackMethodKind,
    pub return_handler_class_name: String,
    /// Name of the subclass
    pub async_handler_class_name: String,
    pub arguments: Vec<Argument>,
    pub return_ty: Option<ReturnType>,
    pub out_pointer_ty: FfiTypeNode,
}

/// Callback method kind.
///
/// There's currently only 2 options:
///   - Methods that are async in both Rust and JS
///   - Sync Rust methods wrapped to be fire-and-forget JS methods
#[derive(Debug, Clone, Node)]
pub enum CallbackMethodKind {
    Sync,
    FireAndForget,
    Async(AsyncData),
}

#[derive(Debug, Clone, Node)]
pub struct CppCallbackReturnHandlerClass {
    pub name: String,
    pub return_ty: Option<ReturnType>,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Function))]
#[map_node(callables::map_function)]
pub struct Function {
    pub callable: Callable,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Constructor))]
#[map_node(callables::map_constructor)]
pub struct Constructor {
    pub callable: Callable,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Method))]
#[map_node(callables::map_method)]
pub struct Method {
    pub callable: Callable,
    pub docstring: Option<String>,
    pub js_docstring: String,
}

#[derive(Debug, Clone, Node)]
pub struct VTable {
    pub interface_name: String,
    pub callback_interface: bool,
    pub callback_interface_id: u64,
    pub struct_type: FfiTypeNode,
    pub init_fn: RustFfiFunctionName,
    pub methods: Vec<VTableMethod>,
}

/// Single method in a vtable
#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::VTableMethod))]
pub struct VTableMethod {
    pub callable: Callable,
    pub ffi_type: FfiTypeNode,
}

/// Common data from Function/Method/Constructor
#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Callable))]
#[map_node(callables::map_callable)]
pub struct Callable {
    pub id: u64,
    pub name: String,
    pub async_data: Option<AsyncData>,
    pub is_js_async: bool,
    pub concurrency_mode: ConcurrencyMode,
    // UniFFIScaffolding method used to invoke this callable
    pub uniffi_scaffolding_method: String,
    pub kind: CallableKind,
    pub arguments: Vec<Argument>,
    pub return_type: Option<ReturnType>,
    pub throws_type: Option<ThrowsType>,
    pub checksum: Option<u16>,
    pub ffi_func: RustFfiFunctionName,
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::CallableKind))]
pub enum CallableKind {
    Function,
    Method {
        self_type: TypeNode,
    },
    Constructor {
        self_type: TypeNode,
        primary: bool,
    },
    VTableMethod {
        self_type: TypeNode,
        for_callback_interface: bool,
    },
}

#[derive(Debug, Clone, MapNode, Node)]
#[map_node(from(general::Argument))]
#[map_node(callables::map_argument)]
pub struct Argument {
    pub name: String,
    pub ty: TypeNode,
    pub by_ref: bool,
    pub optional: bool,
    pub default: Option<DefaultValueNode>,
    pub ffi_value_class: String,
}

#[derive(Debug, Clone, MapNode, Node, Eq, PartialEq, Hash)]
#[map_node(from(general::DefaultValue))]
#[map_node(defaults::map_default_value_node)]
pub struct DefaultValueNode {
    pub default: DefaultValue,
    /// The default value rendered as a string
    pub js_lit: String,
}

#[derive(Debug, Clone, MapNode, Node, Eq, PartialEq, Hash)]
#[map_node(from(general::DefaultValue))]
pub enum DefaultValue {
    Default(TypeNode),
    Literal(LiteralNode),
}

#[derive(Debug, Clone, MapNode, Node, Eq, PartialEq, Hash)]
#[map_node(from(general::Literal))]
#[map_node(defaults::map_literal_node)]
pub struct LiteralNode {
    pub js_lit: String,
    pub lit: Literal,
}

#[derive(Debug, Clone, MapNode, Node, Eq, PartialEq, Hash)]
#[map_node(from(general::Literal))]
pub enum Literal {
    Boolean(bool),
    String(String),
    // Integers are represented as the widest representation we can.
    // Number formatting vary with language and radix, so we avoid a lot of parsing and
    // formatting duplication by using only signed and unsigned variants.
    UInt(u64, Radix, TypeNode),
    Int(i64, Radix, TypeNode),
    // Pass the string representation through as typed in the UDL.
    // This avoids a lot of uncertainty around precision and accuracy,
    // though bindings for languages less sophisticated number parsing than WebIDL
    // will have to do extra work.
    Float(String, TypeNode),
    Enum(String, TypeNode),
    EmptySequence,
    EmptyMap,
    EmptySet,
    None,
    Some { inner: Box<DefaultValueNode> },
}

#[derive(Debug, Clone, MapNode, Node, Eq, PartialEq, Hash)]
#[map_node(from(general::TypeNode))]
pub struct TypeNode {
    pub id: u64,
    pub ty: Type,
    pub canonical_name: String,
    pub is_used_as_error: bool,
    pub ffi_type: FfiTypeNode,
}

#[derive(Debug, Clone, Node)]
pub struct ReturnType {
    pub ffi_value_class: String,
    pub ty: TypeNode,
}

#[derive(Debug, Clone, Node)]
pub struct ThrowsType {
    pub ty: TypeNode,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, MapNode, Node)]
#[map_node(from(general::FfiDefinition))]
pub enum FfiDefinition {
    RustFunction(FfiFunction),
    FunctionType(FfiFunctionType),
    Struct(FfiStruct),
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiFunction))]
pub struct FfiFunction {
    pub name: RustFfiFunctionName,
    pub async_data: Option<AsyncData>,
    pub arguments: Vec<FfiArgument>,
    pub return_type: FfiReturnType,
    pub has_rust_call_status_arg: bool,
    pub kind: FfiFunctionKind,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiFunctionType))]
pub struct FfiFunctionType {
    pub name: FfiFunctionTypeName,
    pub arguments: Vec<FfiArgument>,
    pub return_type: FfiReturnType,
    pub has_rust_call_status_arg: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiStruct))]
pub struct FfiStruct {
    pub name: FfiStructName,
    pub fields: Vec<FfiField>,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiArgument))]
pub struct FfiArgument {
    pub name: String,
    pub ty: FfiTypeNode,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiField))]
pub struct FfiField {
    pub name: String,
    pub ty: FfiTypeNode,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash, Node, MapNode)]
#[map_node(from(general::FfiReturnType))]
pub struct FfiReturnType {
    pub ty: Option<FfiTypeNode>,
}

#[derive(Debug, Clone, MapNode, Node, PartialEq, Eq, Hash)]
#[map_node(from(general::FfiType))]
#[map_node(ffi_types::map_ffi_type_node)]
pub struct FfiTypeNode {
    pub ty: FfiType,
    pub type_name: String,
}

#[derive(Debug, Clone, Node, Template)]
#[template(path = "api-doc.md", escape = "none")]
pub struct ApiModuleDocs {
    pub filename: String,
    pub jsdoc_module_name: String,
    pub module_name: String,
    pub classes: Vec<String>,
    pub functions: Vec<String>,
}
