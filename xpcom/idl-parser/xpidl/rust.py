# rust.py - Generate rust bindings from IDL.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Print a runtime Rust bindings file for the IDL file specified"""

# --- Safety Hazards ---

# We currently don't generate some bindings for some IDL methods in rust code,
# due to there being ABI safety hazards if we were to do so. This is the
# documentation for the reasons why we don't generate certain types of bindings,
# so that we don't accidentally start generating them in the future.

# notxpcom methods and attributes return their results directly by value. The x86
# windows stdcall ABI returns aggregates by value differently for methods than
# functions, and rust only exposes the function ABI, so that's the one we're
# using. The correct ABI can be emulated for notxpcom methods returning aggregates
# by passing an &mut ReturnType parameter as the second parameter.  This strategy
# is used by the winapi-rs crate.
# https://github.com/retep998/winapi-rs/blob/7338a5216a6a7abeefcc6bb1bc34381c81d3e247/src/macros.rs#L220-L231
#
# Right now we can generate code for notxpcom methods and attributes, as we don't
# support passing aggregates by value over these APIs ever (the types which are
# allowed in xpidl.py shouldn't include any aggregates), so the code is
# correct. In the future if we want to start supporting returning aggregates by
# value, we will need to use a workaround such as the one used by winapi.rs.

# nostdcall methods on x86 windows will use the thiscall ABI, which is not
# stable in rust right now, so we cannot generate bindings to them.

# In general, passing C++ objects by value over the C ABI is not a good idea,
# and when possible we should avoid doing so. We don't generate bindings for
# these methods here currently.

import os.path
import re

from xpidl import xpidl


class AutoIndent:
    """A small autoindenting wrapper around a fd.
    Used to make the code output more readable."""

    def __init__(self, fd):
        self.fd = fd
        self.indent = 0

    def write(self, string):
        """A smart write function which automatically adjusts the
        indentation of each line as it is written by counting braces"""
        for line in string.split("\n"):
            s = line.strip()
            indent = self.indent
            if len(s) == 0:
                indent = 0
            elif s[0] == "}":
                indent -= 1

            self.fd.write("    " * indent + s + "\n")
            for c in s:
                if c in {"(", "{", "["}:
                    self.indent += 1
                elif c in {")", "}", "]"}:
                    self.indent -= 1


def rustSanitize(s):
    keywords = [
        "abstract",
        "alignof",
        "as",
        "become",
        "box",
        "break",
        "const",
        "continue",
        "crate",
        "do",
        "else",
        "enum",
        "extern",
        "false",
        "final",
        "fn",
        "for",
        "if",
        "impl",
        "in",
        "let",
        "loop",
        "macro",
        "match",
        "mod",
        "move",
        "mut",
        "offsetof",
        "override",
        "priv",
        "proc",
        "pub",
        "pure",
        "ref",
        "return",
        "Self",
        "self",
        "sizeof",
        "static",
        "struct",
        "super",
        "trait",
        "true",
        "type",
        "typeof",
        "unsafe",
        "unsized",
        "use",
        "virtual",
        "where",
        "while",
        "yield",
    ]
    if s in keywords:
        return s + "_"
    return s


# printdoccomments = False
printdoccomments = True

if printdoccomments:

    def printComments(fd, clist, indent):
        fd.write(f"{indent}{doccomments(clist)}")

    def doccomments(clist):
        if len(clist) == 0:
            return ""
        s = "/// ```text"
        for c in clist:
            for cc in c.splitlines():
                s += "\n/// " + cc
        s += "\n/// ```\n///\n"
        return s

else:

    def printComments(fd, clist, indent):
        pass

    def doccomments(clist):
        return ""


def firstCap(str):
    return str[0].upper() + str[1:]


# Attribute VTable Methods
def attributeNativeName(a, getter):
    binaryname = rustSanitize(a.binaryname if a.binaryname else firstCap(a.name))
    prefix = "Get" if getter else "Set"
    return f"{prefix}{binaryname}"


def attributeReturnType(a, getter):
    if a.notxpcom:
        if getter:
            return a.realtype.rustType("in").strip()
        return "()"
    return "::nserror::nsresult"


def attributeParamName(a):
    return "a" + firstCap(a.name)


def attributeRawParamList(iface, a, getter):
    if getter and a.notxpcom:
        l = []
    else:
        l = [(attributeParamName(a), a.realtype.rustType("out" if getter else "in"))]
    if a.implicit_jscontext:
        raise xpidl.RustNoncompat("jscontext is unsupported")
    if a.nostdcall:
        raise xpidl.RustNoncompat("nostdcall is unsupported")
    return l


def attributeParamList(iface, a, getter):
    l = ["this: *const " + iface.name]
    l += [f"{name}: {ty}" for name, ty in attributeRawParamList(iface, a, getter)]
    return ", ".join(l)


def attrAsVTableEntry(iface, m, getter):
    name = attributeNativeName(m, getter)
    try:
        params = attributeParamList(iface, m, getter)
        ret_ty = attributeReturnType(m, getter)
        return f'pub {name}: unsafe extern "system" fn ({params}) -> {ret_ty}'
    except xpidl.RustNoncompat as reason:
        return f"""\
/// Unable to generate binding because `{reason}`
pub {name}: *const ::libc::c_void"""


# Method VTable generation functions
def methodNativeName(m):
    binaryname = m.binaryname is not None and m.binaryname or firstCap(m.name)
    return rustSanitize(binaryname)


def methodReturnType(m):
    if m.notxpcom:
        return m.realtype.rustType("in").strip()
    return "::nserror::nsresult"


def methodRawParamList(iface, m):
    l = [(rustSanitize(p.name), p.rustType()) for p in m.params]

    if m.implicit_jscontext:
        raise xpidl.RustNoncompat("jscontext is unsupported")

    if m.optional_argc:
        raise xpidl.RustNoncompat("optional_argc is unsupported")

    if m.nostdcall:
        raise xpidl.RustNoncompat("nostdcall is unsupported")

    if not m.notxpcom and m.realtype.name != "void":
        l.append(("_retval", m.realtype.rustType("out")))

    return l


def methodParamList(iface, m):
    l = [f"this: *const {iface.name}"]
    l += [f"{name}: {ty}" for name, ty in methodRawParamList(iface, m)]
    return ", ".join(l)


def methodAsVTableEntry(iface, m):
    name = methodNativeName(m)
    try:
        params = methodParamList(iface, m)
        ret_ty = methodReturnType(m)
        return f'pub {name}: unsafe extern "system" fn ({params}) -> {ret_ty}'
    except xpidl.RustNoncompat as reason:
        return f"""\
/// Unable to generate binding because `{reason}`
pub {name}: *const ::libc::c_void"""


method_impl_tmpl = """\
#[inline]
pub unsafe fn {name}(&self, {params}) -> {ret_ty} {{
    ((*self.vtable).{name})(self, {args})
}}
"""


def methodAsWrapper(iface, m):
    name = methodNativeName(m)
    try:
        param_list = methodRawParamList(iface, m)
        params = [f"{name}: {ty}" for name, ty in param_list]
        args = [x[0] for x in param_list]

        return method_impl_tmpl.format(
            name=name,
            params=", ".join(params),
            ret_ty=methodReturnType(m),
            args=", ".join(args),
        )
    except xpidl.RustNoncompat:
        # Dummy field for the doc comments to attach to.
        # Private so that it's not shown in rustdoc.
        return f"const _{name}: () = ();"


infallible_impl_tmpl = """\
#[inline]
pub unsafe fn {name}(&self) -> {realtype} {{
    let mut result = <{realtype} as ::std::default::Default>::default();
    let _rv = ((*self.vtable).{name})(self, &mut result);
    debug_assert!(_rv.succeeded());
    result
}}
"""


def attrAsWrapper(iface, m, getter):
    name = attributeNativeName(m, getter)
    try:
        if m.implicit_jscontext:
            raise xpidl.RustNoncompat("jscontext is unsupported")

        if m.nostdcall:
            raise xpidl.RustNoncompat("nostdcall is unsupported")

        param_name = attributeParamName(m)

        if getter and m.infallible and m.realtype.kind == "builtin":
            # NOTE: We don't support non-builtin infallible getters in Rust code.
            return infallible_impl_tmpl.format(
                name=name,
                realtype=m.realtype.rustType("in"),
            )

        param_list = attributeRawParamList(iface, m, getter)
        params = [f"{name}: {ty}" for name, ty in param_list]
        return method_impl_tmpl.format(
            name=name,
            params=", ".join(params),
            ret_ty=attributeReturnType(m, getter),
            args="" if getter and m.notxpcom else param_name,
        )

    except xpidl.RustNoncompat:
        # Dummy field for the doc comments to attach to.
        # Private so that it's not shown in rustdoc.
        return f"const _{name}: () = ();"


header = """\
//
// DO NOT EDIT.  THIS FILE IS GENERATED FROM $SRCDIR/{relpath}
//

"""


def idl_basename(f):
    """returns the base name of a file with the last extension stripped"""
    return os.path.splitext(os.path.basename(f))[0]


def print_rust_bindings(idl, fd, relpath):
    fd = AutoIndent(fd)

    fd.write(header.format(relpath=relpath))

    # All of the idl files will be included into the same rust module, as we
    # can't do forward declarations. Because of this, we want to ignore all
    # import statements

    for p in idl.productions:
        if p.kind in {"include", "cdata", "forward"}:
            continue

        if p.kind == "interface":
            write_interface(p, fd)
            continue

        if p.kind == "typedef":
            try:
                if printdoccomments:
                    ty = p.realtype.nativeType("in")
                    fd.write(f"/// `typedef {ty} {p.name};`\n///\n")
                    fd.write(doccomments(p.doccomments))
                rust_ty = p.realtype.rustType("in")
                fd.write(f"pub type {p.name} = {rust_ty};\n\n")
            except xpidl.RustNoncompat as reason:
                fd.write(
                    f"/* unable to generate {p.name} typedef because `{reason}` */\n\n"
                )


base_vtable_tmpl = """
/// We need to include the members from the base interface's vtable at the start
/// of the VTable definition.
pub __base: {name}VTable,

"""


vtable_tmpl = """\
// This struct represents the interface's VTable. A pointer to a statically
// allocated version of this struct is at the beginning of every {name}
// object. It contains one pointer field for each method in the interface. In
// the case where we can't generate a binding for a method, we include a void
// pointer.
#[doc(hidden)]
#[repr(C)]
pub struct {name}VTable {{{base}{entries}}}

"""


# NOTE: This template is not generated for nsISupports, as it has no base interfaces.
deref_tmpl = """\
// Every interface struct type implements `Deref` to its base interface. This
// causes methods on the base interfaces to be directly avaliable on the
// object. For example, you can call `.AddRef` or `.QueryInterface` directly
// on any interface which inherits from `nsISupports`.
impl ::std::ops::Deref for {name} {{
    type Target = {base};
    #[inline]
    fn deref(&self) -> &{base} {{
        unsafe {{
            ::std::mem::transmute(self)
        }}
    }}
}}

// Ensure we can use .coerce() to cast to our base types as well. Any type which
// our base interface can coerce from should be coercable from us as well.
impl<T: {base}Coerce> {name}Coerce for T {{
    #[inline]
    fn coerce_from(v: &{name}) -> &Self {{
        T::coerce_from(v)
    }}
}}
"""


struct_tmpl = """\
// The actual type definition for the interface. This struct has methods
// declared on it which will call through its vtable. You never want to pass
// this type around by value, always pass it behind a reference.

#[repr(C)]
pub struct {name} {{
    vtable: &'static {name}VTable,

    /// This field is a phantomdata to ensure that the VTable type and any
    /// struct containing it is not safe to send across threads by default, as
    /// XPCOM is generally not threadsafe.
    ///
    /// If this type is marked as [rust_sync], there will be explicit `Send` and
    /// `Sync` implementations on this type, which will override the inherited
    /// negative impls from `Rc`.
    __nosync: ::std::marker::PhantomData<::std::rc::Rc<u8>>,

    // Make the rust compiler aware that there might be interior mutability
    // in what actually implements the interface. This works around UB
    // introduced by https://github.com/llvm/llvm-project/commit/01859da84bad95fd51d6a03b08b60c660e642a4f
    // that a rust lint would make blatantly obvious, but doesn't exist.
    // (See https://github.com/rust-lang/rust/issues/111229).
    // This prevents optimizations, but those optimizations weren't available
    // before rustc switched to LLVM 16, and they now cause problems because
    // of the UB.
    // Until there's a lint available to find all our UB, it's simpler to
    // avoid the UB in the first place, at the cost of preventing optimizations
    // in places that don't cause UB. But again, those optimizations weren't
    // available before.
    __maybe_interior_mutability: ::std::cell::UnsafeCell<[u8; 0]>,
}}

// Implementing XpCom for an interface exposes its IID, which allows for easy
// use of the `.query_interface<T>` helper method. This also defines that
// method for {name}.
unsafe impl XpCom for {name} {{
    const IID: nsIID = nsID(0x{m0}, 0x{m1}, 0x{m2},
                            [{m3joined}]);
}}

// We need to implement the RefCounted trait so we can be used with `RefPtr`.
// This trait teaches `RefPtr` how to manage our memory.
unsafe impl RefCounted for {name} {{
    #[inline]
    unsafe fn addref(&self) {{
        self.AddRef();
    }}
    #[inline]
    unsafe fn release(&self) {{
        self.Release();
    }}
}}

// This trait is implemented on all types which can be coerced to from {name}.
// It is used in the implementation of `fn coerce<T>`. We hide it from the
// documentation, because it clutters it up a lot.
#[doc(hidden)]
pub trait {name}Coerce {{
    /// Cheaply cast a value of this type from a `{name}`.
    fn coerce_from(v: &{name}) -> &Self;
}}

// The trivial implementation: We can obviously coerce ourselves to ourselves.
impl {name}Coerce for {name} {{
    #[inline]
    fn coerce_from(v: &{name}) -> &Self {{
        v
    }}
}}

impl {name} {{
    /// Cast this `{name}` to one of its base interfaces.
    #[inline]
    pub fn coerce<T: {name}Coerce>(&self) -> &T {{
        T::coerce_from(self)
    }}
}}
"""


sendsync_tmpl = """\
// This interface is marked as [rust_sync], meaning it is safe to be transferred
// and used from multiple threads silmultaneously.  These override the default
// from the __nosync marker type allowng the type to be sent between threads.
unsafe impl Send for {name} {{}}
unsafe impl Sync for {name} {{}}
"""


wrapper_tmpl = """\
// The implementations of the function wrappers which are exposed to rust code.
// Call these methods rather than manually calling through the VTable struct.
impl {name} {{
{consts}
{methods}
}}

"""

vtable_entry_tmpl = """\
/* {idl} */
{entry},
"""


const_wrapper_tmpl = """\
{docs}
pub const {name}: {type} = {val};
"""


method_wrapper_tmpl = """\
{docs}
/// `{idl}`
{wrapper}
"""


uuid_decoder = re.compile(
    r"""(?P<m0>[a-f0-9]{8})-
                              (?P<m1>[a-f0-9]{4})-
                              (?P<m2>[a-f0-9]{4})-
                              (?P<m3>[a-f0-9]{4})-
                              (?P<m4>[a-f0-9]{12})$""",
    re.X,
)


def write_interface(iface, fd):
    if iface.namemap is None:
        raise Exception("Interface was not resolved.")

    assert iface.base or (iface.name == "nsISupports")

    # Extract the UUID's information so that it can be written into the struct definition
    names = uuid_decoder.match(iface.attributes.uuid).groupdict()
    m3str = names["m3"] + names["m4"]
    names["m3joined"] = ", ".join([f"0x{m3str[i : i + 2]}" for i in range(0, 16, 2)])
    names["name"] = iface.name

    if printdoccomments:
        if iface.base is not None:
            fd.write(f"/// `interface {iface.name} : {iface.base}`\n///\n")
        else:
            fd.write(f"/// `interface {iface.name}`\n///\n")
    printComments(fd, iface.doccomments, "")
    fd.write(struct_tmpl.format(**names))

    if iface.attributes.rust_sync:
        fd.write(sendsync_tmpl.format(name=iface.name))

    if iface.base is not None:
        fd.write(
            deref_tmpl.format(
                name=iface.name,
                base=iface.base,
            )
        )

    entries = []
    for member in iface.members:
        if type(member) is xpidl.Attribute:
            entries.append(
                vtable_entry_tmpl.format(
                    idl=member.toIDL(),
                    entry=attrAsVTableEntry(iface, member, True),
                )
            )
            if not member.readonly:
                entries.append(
                    vtable_entry_tmpl.format(
                        idl=member.toIDL(),
                        entry=attrAsVTableEntry(iface, member, False),
                    )
                )

        elif type(member) is xpidl.Method:
            entries.append(
                vtable_entry_tmpl.format(
                    idl=member.toIDL(),
                    entry=methodAsVTableEntry(iface, member),
                )
            )

    fd.write(
        vtable_tmpl.format(
            name=iface.name,
            base=base_vtable_tmpl.format(name=iface.base)
            if iface.base is not None
            else "",
            entries="\n".join(entries),
        )
    )

    # Get all of the constants
    consts = []
    for member in iface.members:
        if type(member) is xpidl.ConstMember:
            consts.append(
                const_wrapper_tmpl.format(
                    docs=doccomments(member.doccomments),
                    type=member.realtype.rustType("in"),
                    name=member.name,
                    val=member.getValue(),
                )
            )
        if type(member) is xpidl.CEnum:
            for var in member.variants:
                consts.append(
                    const_wrapper_tmpl.format(
                        docs="",
                        type=member.rustType("in"),
                        name=var.name,
                        val=var.getValue(),
                    )
                )

    methods = []
    for member in iface.members:
        if type(member) is xpidl.Attribute:
            methods.append(
                method_wrapper_tmpl.format(
                    docs=doccomments(member.doccomments),
                    idl=member.toIDL(),
                    wrapper=attrAsWrapper(iface, member, True),
                )
            )
            if not member.readonly:
                methods.append(
                    method_wrapper_tmpl.format(
                        docs=doccomments(member.doccomments),
                        idl=member.toIDL(),
                        wrapper=attrAsWrapper(iface, member, False),
                    )
                )

        elif type(member) is xpidl.Method:
            methods.append(
                method_wrapper_tmpl.format(
                    docs=doccomments(member.doccomments),
                    idl=member.toIDL(),
                    wrapper=methodAsWrapper(iface, member),
                )
            )

    fd.write(
        wrapper_tmpl.format(
            name=iface.name,
            consts="\n".join(consts),
            methods="\n".join(methods),
        )
    )
