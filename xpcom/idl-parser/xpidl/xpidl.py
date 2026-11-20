#!/usr/bin/env python
# xpidl.py - A parser for cross-platform IDL (XPIDL) files.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""A parser for cross-platform IDL (XPIDL) files."""

import os.path
import re
import sys
import textwrap
from collections import namedtuple

from ply import lex, yacc

"""A type conforms to the following pattern:

    def nativeType(self, calltype):
        'returns a string representation of the native type
        calltype must be 'in', 'out', 'inout', or 'element'

Interface members const/method/attribute conform to the following pattern:

    name = 'string'

    def toIDL(self):
        'returns the member signature as IDL'
"""


# XXX(nika): Fix the IDL files which do this so we can remove this list?
def rustPreventForward(s):
    """These types are foward declared as interfaces, but never actually defined
    in IDL files. We don't want to generate references to them in rust for that
    reason."""
    return s in (
        "nsIFrame",
        "nsSubDocumentFrame",
    )


def attlistToIDL(attlist):
    if len(attlist) == 0:
        return ""

    attlist = list(attlist)
    attlist.sort(key=lambda a: a[0])

    attribs = ",".join([
        f"{name}({value})" if value is not None else name
        for name, value, aloc in attlist
    ])
    return f"[{attribs}] "


_paramsHardcode = {
    2: ("array", "shared", "iid_is", "size_is", "retval"),
    3: ("array", "size_is", "const"),
}


def paramAttlistToIDL(attlist):
    if len(attlist) == 0:
        return ""

    # Hack alert: g_hash_table_foreach is pretty much unimitatable... hardcode
    # quirk
    attlist = list(attlist)
    sorted = []
    if len(attlist) in _paramsHardcode:
        for p in _paramsHardcode[len(attlist)]:
            i = 0
            while i < len(attlist):
                if attlist[i][0] == p:
                    sorted.append(attlist[i])
                    del attlist[i]
                    continue

                i += 1

    sorted.extend(attlist)

    attribs = ", ".join([
        f"{name} ({value})" if value is not None else name
        for name, value, aloc in sorted
    ])
    return f"[{attribs}] "


def unaliasType(t):
    while t.kind == "typedef":
        t = t.realtype
    assert t is not None
    return t


def getBuiltinOrNativeTypeName(t):
    t = unaliasType(t)
    if t.kind == "builtin":
        return t.name
    elif t.kind == "native":
        assert t.specialtype is not None
        return f"[{t.specialtype}]"
    else:
        return None


class BuiltinLocation:
    def get(self):
        return "<builtin type>"

    def __str__(self):
        return self.get()


class Builtin:
    kind = "builtin"
    location = BuiltinLocation

    def __init__(
        self, name, nativename, rustname, tsname, signed=False, maybeConst=False
    ):
        self.name = name
        self.nativename = nativename
        self.rustname = rustname
        self.tsname = tsname
        self.signed = signed
        self.maybeConst = maybeConst

    def isPointer(self):
        """Check if this type is a pointer type - this will control how pointers act"""
        return self.nativename.endswith("*")

    def nativeType(self, calltype, shared=False, const=False):
        if self.name in ["string", "wstring"] and calltype == "element":
            raise IDLError(
                "Use string class types for string Array elements", self.location
            )

        if const:
            print(
                IDLError(
                    "[const] doesn't make sense on builtin types.",
                    self.location,
                    warning=True,
                ),
                file=sys.stderr,
            )
            const = "const "
        elif calltype == "in" and self.isPointer():
            const = "const "
        elif shared:
            if not self.isPointer():
                raise IDLError(
                    "[shared] not applicable to non-pointer types.", self.location
                )
            const = "const "
        else:
            const = ""
        suffix = "*" if "out" in calltype else ""
        return f"{const}{self.nativename} {suffix}"

    def rustType(self, calltype, shared=False, const=False):
        # We want to rewrite any *mut pointers to *const pointers if constness
        # was requested.
        const = const or ("out" not in calltype and self.isPointer()) or shared
        rustname = self.rustname
        if const and self.isPointer():
            rustname = self.rustname.replace("*mut", "*const")

        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}{rustname}"

    def tsType(self):
        if self.tsname:
            return self.tsname

        raise TSNoncompat(f"Builtin type {self.name} unsupported in TypeScript")


builtinNames = [
    Builtin("boolean", "bool", "bool", "boolean"),
    Builtin("void", "void", "()", "void"),
    Builtin("int8_t", "int8_t", "i8", "i8", True, True),
    Builtin("int16_t", "int16_t", "i16", "i16", True, True),
    Builtin("int32_t", "int32_t", "i32", "i32", True, True),
    Builtin("int64_t", "int64_t", "i64", "i64", True, True),
    Builtin("uint8_t", "uint8_t", "u8", "u8", False, True),
    Builtin("uint16_t", "uint16_t", "u16", "u16", False, True),
    Builtin("uint32_t", "uint32_t", "u32", "u32", False, True),
    Builtin("uint64_t", "uint64_t", "u64", "u64", False, True),
    Builtin("nsresult", "nsresult", "nserror::nsresult", "nsresult"),
    Builtin("float", "float", "libc::c_float", "float"),
    Builtin("double", "double", "libc::c_double", "double"),
    Builtin("char", "char", "libc::c_char", "string"),
    Builtin("string", "char *", "*const libc::c_char", "string"),
    Builtin("wchar", "char16_t", "u16", "string"),
    Builtin("wstring", "char16_t *", "*const u16", "string"),
    # NOTE: char16_t is the same type as `wchar` in C++, however it is reflected
    # into JS as an integer, allowing it to be used in constants.
    # This inconsistency sucks, but reflects existing usage so unfortunately
    # isn't very easy to change.
    Builtin("char16_t", "char16_t", "u16", "u16", False, True),
    # As seen in mfbt/RefCountType.h, this type has special handling to
    # maintain binary compatibility with MSCOM's IUnknown that cannot be
    # expressed in XPIDL.
    Builtin(
        "MozExternalRefCountType",
        "MozExternalRefCountType",
        "MozExternalRefCountType",
        None,
    ),
]

# Allow using more C-style names for the basic integer types.
builtinAlias = [
    ("octet", "uint8_t"),
    ("unsigned short", "uint16_t"),
    ("unsigned long", "uint32_t"),
    ("unsigned long long", "uint64_t"),
    ("short", "int16_t"),
    ("long", "int32_t"),
    ("long long", "int64_t"),
]

builtinMap = {}
for b in builtinNames:
    builtinMap[b.name] = b

for alias, name in builtinAlias:
    builtinMap[alias] = builtinMap[name]


class Location:
    _line = None

    def __init__(self, lexer, lineno, lexpos):
        self._lineno = lineno
        self._lexpos = lexpos
        self._lexdata = lexer.lexdata
        self._file = getattr(lexer, "filename", "<unknown>")

    __hash__ = None

    def __eq__(self, other):
        return self._lexpos == other._lexpos and self._file == other._file

    def resolve(self):
        if self._line:
            return

        startofline = self._lexdata.rfind("\n", 0, self._lexpos) + 1
        endofline = self._lexdata.find("\n", self._lexpos, self._lexpos + 80)
        self._line = self._lexdata[startofline:endofline]
        self._colno = self._lexpos - startofline

    def pointerline(self):
        def i():
            for i in range(0, self._colno):
                yield " "
            yield "^"

        return "".join(i())

    def get(self):
        self.resolve()
        return f"{self._file} line {self._lineno}:{self._colno}"

    def lineno(self):
        self.resolve()
        return self._lineno

    def __str__(self):
        self.resolve()
        return f"{self._file} line {self._lineno}:{self._colno}\n{self._line}\n{self.pointerline()}"


class NameMap:
    """Map of name -> object. Each object must have a .name and .location property.
    Setting the same name twice throws an error."""

    def __init__(self):
        self._d = {}

    def __getitem__(self, key):
        if key in builtinMap:
            return builtinMap[key]
        return self._d[key]

    def __iter__(self):
        return iter(self._d.values())

    def __contains__(self, key):
        return key in builtinMap or key in self._d

    def set(self, object):
        if object.name in builtinMap:
            raise IDLError(
                f"name '{object.name}' is a builtin and cannot be redeclared",
                object.location,
            )
        if object.name.startswith("_"):
            object.name = object.name[1:]
        if object.name in self._d:
            old = self._d[object.name]
            if old == object:
                return
            if isinstance(old, Forward) and isinstance(object, Interface):
                self._d[object.name] = object
            elif isinstance(old, Interface) and isinstance(object, Forward):
                pass
            else:
                raise IDLError(
                    f"name '{object.name}' specified twice. Previous "
                    f"location: {self._d[object.name].location}",
                    object.location,
                )
        else:
            self._d[object.name] = object

    def get(self, id, location):
        try:
            return self[id]
        except KeyError:
            raise IDLError(f"Name '{id}' not found", location)


class RustNoncompat(Exception):
    """
    This exception is raised when a particular type or function cannot be safely exposed to
    rust code
    """

    def __init__(self, reason):
        self.reason = reason

    def __str__(self):
        return self.reason


class TSNoncompat(Exception):
    """Raised when a type cannot be exposed to TypeScript."""

    def __init__(self, reason):
        self.reason = reason

    def __str__(self):
        return self.reason


class IDLError(Exception):
    def __init__(self, message, location, warning=False, notes=None):
        self.message = message
        self.location = location
        self.warning = warning
        self.notes = notes

    def __str__(self):
        prefix = self.warning and "warning" or "error"
        error = f"{prefix}: {self.message}, {self.location}"
        if self.notes is not None:
            error += f"\nnote: {self.notes}"
        return error


class Include:
    kind = "include"

    def __init__(self, filename, location):
        self.filename = filename
        self.location = location

    def __str__(self):
        return f"include '{self.filename}'\n"

    def resolve(self, parent):
        def incfiles():
            yield self.filename
            for dir in parent.incdirs:
                yield os.path.join(dir, self.filename)

        for file in incfiles():
            if not os.path.exists(file):
                continue

            if file in parent.includeCache:
                self.IDL = parent.includeCache[file]
            else:
                self.IDL = parent.parser.parse(
                    open(file, encoding="utf-8").read(), filename=file
                )
                self.IDL.resolve(
                    parent.incdirs,
                    parent.parser,
                    parent.webidlconfig,
                    parent.includeCache,
                )
                parent.includeCache[file] = self.IDL

            for type in self.IDL.getNames():
                parent.setName(type)
            parent.deps.extend(self.IDL.deps)
            return

        raise IDLError(f"File '{self.filename}' not found", self.location)


class IDL:
    def __init__(self, productions):
        self.hasSequence = False
        self.productions = productions
        self.deps = []

    def setName(self, object):
        self.namemap.set(object)

    def getName(self, id, location):
        if id.name == "Array":
            if id.params is None or len(id.params) != 1:
                raise IDLError("Array takes exactly 1 parameter", location)
            self.hasSequence = True
            return Array(self.getName(id.params[0], location), location)

        if id.params is not None:
            raise IDLError(f"Generic type '{id.name}' unrecognized", location)

        try:
            return self.namemap[id.name]
        except KeyError:
            raise IDLError(f"type '{id.name}' not found", location)

    def hasName(self, id):
        return id in self.namemap

    def getNames(self):
        return iter(self.namemap)

    def __str__(self):
        return "".join([str(p) for p in self.productions])

    def resolve(self, incdirs, parser, webidlconfig, includeCache=None):
        self.namemap = NameMap()
        self.incdirs = incdirs
        self.parser = parser
        self.webidlconfig = webidlconfig
        self.includeCache = {} if includeCache is None else includeCache
        for p in self.productions:
            p.resolve(self)

    def includes(self):
        for p in self.productions:
            if p.kind == "include":
                yield p
        if self.hasSequence:
            yield Include("nsTArray.h", BuiltinLocation)

    def needsJSTypes(self):
        for p in self.productions:
            if p.kind == "interface" and p.needsJSTypes():
                return True
        return False

    def needsFunctionTypes(self):
        for p in self.productions:
            if p.kind == "interface" and p.attributes.function:
                return True
        return False


class CDATA:
    kind = "cdata"
    _trailing_spaces_re = re.compile("\n? *$")

    def __init__(self, data, location):
        # the '// %{C++' comment generated in the header.py assumes the
        # text exactly matches the input.
        self.data = data
        self.location = location

    def resolve(self, parent):
        # This can be a false-positive if the word `virtual` is included in a
        # comment, however this doesn't seem to happen very often.
        if isinstance(parent, Interface) and re.search(r"\bvirtual\b", self.data):
            raise IDLError(
                "cannot declare a C++ `virtual` member in XPIDL interface",
                self.location,
                notes=textwrap.fill(
                    """All virtual members must be declared directly using XPIDL.
                    Both the Rust bindings and XPConnect rely on the per-platform
                    vtable layouts generated by the XPIDL compiler to allow
                    cross-language XPCOM method calls between JS and C++.
                    Consider using a `[notxpcom, nostdcall]` method instead."""
                ),
            )

    def __str__(self):
        return f"cdata: {self.location.get()}\n\t{self.data!r}\n"

    def count(self):
        return 0

    def data_with_comment(self):
        # generate a comment for the searchfox analysis.
        #
        # self.location.lineno() points the "%{C++" line.
        # self.data starts from the next line, excluding the "%}" part.
        #
        # Normalize the body, by removing the possible newline before the "%}",
        # and also removing the indent before the "%}" part.
        body = self._trailing_spaces_re.sub("", self.data)

        # "first" should point the line after the "%{C++" line,
        # and the "last" should point the line before the "%}" part.
        first = self.location.lineno() + 1
        last = first + len(body.split("\n")) - 1

        return f"// %{{C++:{first}-{last}\n{body}\n// %}}\n"


class Typedef:
    kind = "typedef"

    def __init__(self, type, name, attlist, location, doccomments):
        self.type = type
        self.name = name
        self.location = location
        self.doccomments = doccomments

    __hash__ = None

    def __eq__(self, other):
        return self.name == other.name and self.type == other.type

    def resolve(self, parent):
        parent.setName(self)
        self.realtype = parent.getName(self.type, self.location)

        if not isinstance(self.realtype, (Builtin, CEnum, Native, Typedef)):
            raise IDLError("Unsupported typedef target type", self.location)

    def nativeType(self, calltype):
        suffix = "*" if "out" in calltype else ""
        return f"{self.name} {suffix}"

    def rustType(self, calltype):
        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}{self.name}"

    def tsType(self):
        # Make sure that underlying type is supported: doesn't throw TSNoncompat.
        self.realtype.tsType()
        return self.name

    def __str__(self):
        return f"typedef {self.type} {self.name}\n"


class Forward:
    kind = "forward"

    def __init__(self, name, location, doccomments):
        self.name = name
        self.location = location
        self.doccomments = doccomments

    __hash__ = None

    def __eq__(self, other):
        return other.kind == "forward" and other.name == self.name

    def resolve(self, parent):
        # Hack alert: if an identifier is already present, move the doccomments
        # forward.
        if parent.hasName(self.name):
            for i in range(0, len(parent.productions)):
                if parent.productions[i] is self:
                    break
            for i in range(i + 1, len(parent.productions)):
                if hasattr(parent.productions[i], "doccomments"):
                    parent.productions[i].doccomments[0:0] = self.doccomments
                    break

        parent.setName(self)

    def nativeType(self, calltype):
        if calltype == "element":
            return f"RefPtr<{self.name}>"
        suffix = "*" if "out" in calltype else ""
        return f"{self.name} *{suffix}"

    def rustType(self, calltype):
        if rustPreventForward(self.name):
            raise RustNoncompat(f"forward declaration {self.name} is unsupported")
        if calltype == "element":
            return f"Option<RefPtr<{self.name}>>"
        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}*const {self.name}"

    def tsType(self):
        return self.name

    def __str__(self):
        return f"forward-declared {self.name}\n"


class Native:
    kind = "native"

    modifier = None
    specialtype = None

    # A tuple type here means that a custom value is used for each calltype:
    #   (in, out/inout, array element) respectively.
    # A `None` here means that the written type should be used as-is.
    specialtypes = {
        "nsid": None,
        "utf8string": ("const nsACString&", "nsACString&", "nsCString"),
        "cstring": ("const nsACString&", "nsACString&", "nsCString"),
        "astring": ("const nsAString&", "nsAString&", "nsString"),
        "jsval": ("JS::Handle<JS::Value>", "JS::MutableHandle<JS::Value>", "JS::Value"),
        "promise": "::mozilla::dom::Promise",
    }

    def __init__(self, name, nativename, attlist, location):
        self.name = name
        self.nativename = nativename
        self.location = location

        for attr_name, value, aloc in attlist:
            if value is not None:
                raise IDLError("Unexpected attribute value", aloc)
            if attr_name in ("ptr", "ref"):
                if self.modifier is not None:
                    raise IDLError("More than one ptr/ref modifier", aloc)
                self.modifier = attr_name
            elif attr_name in self.specialtypes.keys():
                if self.specialtype is not None:
                    raise IDLError("More than one special type", aloc)
                self.specialtype = attr_name
                if self.specialtypes[attr_name] is not None:
                    self.nativename = self.specialtypes[attr_name]
            else:
                raise IDLError("Unexpected attribute", aloc)

    __hash__ = None

    def __eq__(self, other):
        return (
            self.name == other.name
            and self.nativename == other.nativename
            and self.modifier == other.modifier
            and self.specialtype == other.specialtype
        )

    def resolve(self, parent):
        parent.setName(self)

    def isPtr(self, calltype):
        return self.modifier == "ptr"

    def isRef(self, calltype):
        return self.modifier == "ref"

    def nativeType(self, calltype, const=False, shared=False):
        if shared:
            if calltype != "out":
                raise IDLError(
                    "[shared] only applies to out parameters.", self.location
                )
            const = True

        if isinstance(self.nativename, tuple):
            if calltype == "in":
                return self.nativename[0] + " "
            elif "out" in calltype:
                return self.nativename[1] + " "
            else:
                return self.nativename[2] + " "

        # 'in' nsid parameters should be made 'const'
        if self.specialtype == "nsid" and calltype == "in":
            const = True

        if calltype == "element":
            if self.specialtype == "nsid":
                if self.isPtr(calltype):
                    raise IDLError(
                        "Array<nsIDPtr> not yet supported. "
                        "File an XPConnect bug if you need it.",
                        self.location,
                    )

                # ns[CI]?IDs should be held directly in Array<T>s
                return self.nativename

            if self.isRef(calltype):
                raise IDLError(
                    "[ref] qualified type unsupported in Array<T>", self.location
                )

            # Promises should be held in RefPtr<T> in Array<T>s
            if self.specialtype == "promise":
                return "RefPtr<mozilla::dom::Promise>"

        if self.isRef(calltype):
            m = "& "  # [ref] is always passed with a single indirection
        else:
            m = "* " if "out" in calltype else ""
            if self.isPtr(calltype):
                m += "* "
        prefix = "const " if const else ""
        return f"{prefix}{self.nativename} {m}"

    def rustType(self, calltype, const=False, shared=False):
        # For the most part, 'native' types don't make sense in rust, as they
        # are native C++ types. However, we can support a few types here, as
        # they're important and can easily be translated.
        #
        # NOTE: This code doesn't try to perfectly match C++ constness, as
        # constness doesn't affect ABI, and raw pointers are already unsafe.

        if self.modifier not in ["ptr", "ref"]:
            raise RustNoncompat("Rust only supports [ref] / [ptr] native types")

        if shared:
            if calltype != "out":
                raise IDLError(
                    "[shared] only applies to out parameters.", self.location
                )
            const = True

        # 'in' nsid parameters should be made 'const'
        if self.specialtype == "nsid" and calltype == "in":
            const = True

        prefix = "*const " if const or shared else "*mut "
        if "out" in calltype and self.isPtr(calltype):
            prefix = "*mut " + prefix

        if self.specialtype:
            # The string types are very special, and need to be handled seperately.
            if self.specialtype in ["cstring", "utf8string"]:
                if calltype == "in":
                    return "*const ::nsstring::nsACString"
                elif "out" in calltype:
                    return "*mut ::nsstring::nsACString"
                else:
                    return "::nsstring::nsCString"
            if self.specialtype == "astring":
                if calltype == "in":
                    return "*const ::nsstring::nsAString"
                elif "out" in calltype:
                    return "*mut ::nsstring::nsAString"
                else:
                    return "::nsstring::nsString"
            # nsid has some special handling, but generally re-uses the generic
            # prefix handling above.
            if self.specialtype == "nsid":
                if "element" in calltype:
                    if self.isPtr(calltype):
                        raise IDLError(
                            "Array<nsIDPtr> not yet supported. "
                            "File an XPConnect bug if you need it.",
                            self.location,
                        )
                    return self.nativename
                return prefix + self.nativename
            raise RustNoncompat(f"special type {self.specialtype} unsupported")

        # These 3 special types correspond to native pointer types which can
        # generally be supported behind pointers. Other types are not supported
        # for now.
        if self.nativename == "void":
            return prefix + "libc::c_void"
        if self.nativename == "char":
            return prefix + "libc::c_char"
        if self.nativename == "char16_t":
            return prefix + "u16"

        raise RustNoncompat(f"native type {self.nativename} unsupported")

    ts_special = {
        "astring": "string",
        "cstring": "string",
        "jsval": "any",
        "nsid": "nsID",
        "promise": "Promise<any>",
        "utf8string": "string",
    }

    def tsType(self):
        if type := self.ts_special.get(self.specialtype, None):
            return type

        raise TSNoncompat(f"Native type {self.name} unsupported in TypeScript")

    def __str__(self):
        return f"native {self.name}({self.nativename})\n"


class WebIDL:
    kind = "webidl"

    def __init__(self, name, location):
        self.name = name
        self.location = location

    __hash__ = None

    def __eq__(self, other):
        return other.kind == "webidl" and self.name == other.name

    def resolve(self, parent):
        # XXX(nika): We don't handle _every_ kind of webidl object here (as that
        # would be hard). For example, we don't support nsIDOM*-defaulting
        # interfaces.
        # TODO: More explicit compile-time checks?

        assert parent.webidlconfig is not None, (
            "WebIDL declarations require passing webidlconfig to resolve."
        )

        # Resolve our native name according to the WebIDL configs.
        config = parent.webidlconfig.get(self.name, {})
        self.native = config.get("nativeType")
        if self.native is None:
            self.native = f"mozilla::dom::{self.name}"
        self.headerFile = config.get("headerFile")
        if self.headerFile is None:
            self.headerFile = self.native.replace("::", "/") + ".h"

        parent.setName(self)

    def nativeType(self, calltype, const=False):
        prefix = "const " if const else ""
        elemtype = f"{prefix}{self.native}"
        if calltype == "element":
            return f"RefPtr<{elemtype}>"
        suffix = "*" if "out" in calltype else ""
        return f"{elemtype} *{suffix}"

    def rustType(self, calltype, const=False):
        # Just expose the type as a void* - we can't do any better.
        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}*const libc::c_void"

    def tsType(self):
        return self.name

    def __str__(self):
        return f"webidl {self.name}\n"


class Interface:
    kind = "interface"

    def __init__(self, name, attlist, base, members, location, doccomments):
        self.name = name
        self.attributes = InterfaceAttributes(attlist, location)
        self.base = base
        self.members = members
        self.location = location
        self.namemap = NameMap()
        self.doccomments = doccomments
        self.nativename = name

        for m in members:
            if not isinstance(m, CDATA):
                self.namemap.set(m)

    __hash__ = None

    def __eq__(self, other):
        return self.name == other.name and self.location == other.location

    def resolve(self, parent):
        self.idl = parent

        if not self.attributes.scriptable and self.attributes.builtinclass:
            raise IDLError(
                f"Non-scriptable interface '{self.name}' doesn't need to be "
                f"marked builtinclass",
                self.location,
            )

        # Hack alert: if an identifier is already present, libIDL assigns
        # doc comments incorrectly. This is quirks-mode extraordinaire!
        if parent.hasName(self.name):
            for member in self.members:
                if hasattr(member, "doccomments"):
                    member.doccomments[0:0] = self.doccomments
                    break
            self.doccomments = parent.getName(TypeId(self.name), None).doccomments

        if self.attributes.function:
            has_method = False
            for member in self.members:
                if member.kind == "method":
                    if has_method:
                        raise IDLError(
                            f"interface '{self.name}' has multiple methods, "
                            f"but marked 'function'",
                            self.location,
                        )
                    else:
                        has_method = True
            if self.base != "nsISupports":
                raise IDLError(
                    f"interface '{self.name}' is marked 'function', and does not inherit from nsISupports",
                    self.location,
                )

        parent.setName(self)
        if self.base is not None:
            realbase = parent.getName(TypeId(self.base), self.location)
            if realbase.kind != "interface":
                raise IDLError(
                    f"interface '{self.name}' inherits from non-interface "
                    f"type '{self.base}'",
                    self.location,
                )

            if self.attributes.scriptable and not realbase.attributes.scriptable:
                raise IDLError(
                    f"interface '{self.name}' is scriptable but derives from "
                    f"non-scriptable '{self.base}'",
                    self.location,
                    warning=True,
                )

            if (
                self.attributes.scriptable
                and realbase.attributes.builtinclass
                and not self.attributes.builtinclass
            ):
                raise IDLError(
                    f"interface '{self.name}' is not builtinclass but derives "
                    f"from builtinclass '{self.base}'",
                    self.location,
                )

            if realbase.attributes.rust_sync and not self.attributes.rust_sync:
                raise IDLError(
                    f"interface '{self.name}' is not rust_sync but derives "
                    f"from rust_sync '{self.base}'",
                    self.location,
                )

            if (
                self.attributes.rust_sync
                and self.attributes.scriptable
                and not self.attributes.builtinclass
            ):
                raise IDLError(
                    f"interface '{self.name}' is rust_sync but is not builtinclass",
                    self.location,
                )
        elif self.name != "nsISupports":
            raise IDLError(
                f"Interface '{self.name}' must inherit from nsISupports",
                self.location,
            )

        for member in self.members:
            member.resolve(self)

        # The number 250 is NOT arbitrary; this number is the maximum number of
        # stub entries defined in xpcom/reflect/xptcall/genstubs.pl
        # Do not increase this value without increasing the number in that
        # location, or you WILL cause otherwise unknown problems!
        if self.countEntries() > 250 and not self.attributes.builtinclass:
            raise IDLError(
                f"interface '{self.name}' has too many entries", self.location
            )

    def nativeType(self, calltype, const=False):
        if calltype == "element":
            return f"RefPtr<{self.name}>"
        prefix = "const " if const else ""
        suffix = "*" if "out" in calltype else ""
        return f"{prefix}{self.name} *{suffix}"

    def rustType(self, calltype, const=False):
        if calltype == "element":
            return f"Option<RefPtr<{self.name}>>"
        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}*const {self.name}"

    def __str__(self):
        l = [f"interface {self.name}\n"]
        if self.base is not None:
            l.append(f"\tbase {self.base}\n")
        l.append(str(self.attributes))
        if self.members is None:
            l.append("\tincomplete type\n")
        else:
            for m in self.members:
                l.append(str(m))
        return "".join(l)

    def getConst(self, name, location):
        # The constant may be in a base class
        iface = self
        while name not in iface.namemap and iface.base is not None:
            iface = self.idl.getName(TypeId(iface.base), self.location)
        if name not in iface.namemap:
            raise IDLError(f"cannot find symbol '{name}'", location)
        c = iface.namemap.get(name, location)
        if c.kind != "const":
            raise IDLError(f"symbol '{name}' is not a constant", location)

        return c.getValue()

    def needsJSTypes(self):
        for m in self.members:
            if m.kind == "attribute" and m.type == TypeId("jsval"):
                return True
            if m.kind == "method" and m.needsJSTypes():
                return True
        return False

    def countEntries(self):
        """Returns the number of entries in the vtable for this interface."""
        total = sum(member.count() for member in self.members)
        if self.base is not None:
            realbase = self.idl.getName(TypeId(self.base), self.location)
            total += realbase.countEntries()
        return total

    def tsType(self):
        return self.name


class InterfaceAttributes:
    uuid = None
    scriptable = False
    builtinclass = False
    function = False
    main_process_scriptable_only = False
    rust_sync = False

    def setuuid(self, value):
        self.uuid = value.lower()

    def setscriptable(self):
        self.scriptable = True

    def setfunction(self):
        self.function = True

    def setbuiltinclass(self):
        self.builtinclass = True

    def setmain_process_scriptable_only(self):
        self.main_process_scriptable_only = True

    def setrust_sync(self):
        self.rust_sync = True

    actions = {
        "uuid": (True, setuuid),
        "scriptable": (False, setscriptable),
        "builtinclass": (False, setbuiltinclass),
        "function": (False, setfunction),
        "object": (False, lambda self: True),
        "main_process_scriptable_only": (False, setmain_process_scriptable_only),
        "rust_sync": (False, setrust_sync),
    }

    def __init__(self, attlist, location):
        def badattribute(self):
            raise IDLError(f"Unexpected interface attribute '{name}'", location)

        for name, val, aloc in attlist:
            hasval, action = self.actions.get(name, (False, badattribute))
            if hasval:
                if val is None:
                    raise IDLError(f"Expected value for attribute '{name}'", aloc)

                action(self, val)
            else:
                if val is not None:
                    raise IDLError(f"Unexpected value for attribute '{name}'", aloc)

                action(self)

        if self.uuid is None:
            raise IDLError("interface has no uuid", location)

    def __str__(self):
        l = []
        if self.uuid:
            l.append(f"\tuuid: {self.uuid}\n")
        if self.scriptable:
            l.append("\tscriptable\n")
        if self.builtinclass:
            l.append("\tbuiltinclass\n")
        if self.function:
            l.append("\tfunction\n")
        if self.main_process_scriptable_only:
            l.append("\tmain_process_scriptable_only\n")
        if self.rust_sync:
            l.append("\trust_sync\n")
        return "".join(l)


class ConstMember:
    kind = "const"

    def __init__(self, type, name, value, location, doccomments):
        self.type = type
        self.name = name
        self.valueFn = value
        self.location = location
        self.doccomments = doccomments

    def resolve(self, parent):
        self.realtype = parent.idl.getName(self.type, self.location)
        self.iface = parent
        basetype = self.realtype
        while isinstance(basetype, Typedef):
            basetype = basetype.realtype
        if not isinstance(basetype, Builtin) or not basetype.maybeConst:
            raise IDLError(
                f"const may only be an integer type, not {self.type.name}",
                self.location,
            )

        self.basetype = basetype
        # Value is a lambda. Resolve it.
        self.value = self.valueFn(self.iface)

        min_val = -(2**31) if basetype.signed else 0
        max_val = 2**31 - 1 if basetype.signed else 2**32 - 1
        if self.value < min_val or self.value > max_val:
            nativetype = "int32_t" if basetype.signed else "uint32_t"
            raise IDLError(
                f"xpidl constants must fit within {nativetype}",
                self.location,
            )

    def getValue(self):
        return self.value

    def __str__(self):
        return f"\tconst {self.type} {self.name} = {self.getValue()}\n"

    def count(self):
        return 0


# Represents a single name/value pair in a CEnum
class CEnumVariant:
    # Treat CEnumVariants as consts in terms of value resolution, so we can
    # do things like binary operation values for enum members.
    kind = "const"

    def __init__(self, name, value, location):
        self.name = name
        self.valueFn = value
        self.location = location

    def getValue(self):
        return self.value


class CEnum:
    kind = "cenum"

    def __init__(self, width, name, variants, location, doccomments):
        # We have to set a name here, otherwise we won't pass namemap checks on
        # the interface. This name will change it in resolve(), in order to
        # namespace the enum within the interface.
        self.name = name
        self.basename = name
        self.width = width
        self.location = location
        self.namemap = NameMap()
        self.doccomments = doccomments
        self.variants = variants
        if self.width not in (8, 16, 32):
            raise IDLError("Width must be one of {8, 16, 32}", self.location)

    def resolve(self, iface):
        self.iface = iface
        # Renaming enum to faux-namespace the enum type to the interface in JS
        # so we don't collide in the global namespace. Hacky/ugly but it does
        # the job well enough, and the name will still be interface::variant in
        # C++.
        self.name = f"{self.iface.name}_{self.basename}"
        self.iface.idl.setName(self)

        # Compute the value for each enum variant that doesn't set its own
        # value
        next_value = 0
        for variant in self.variants:
            # CEnum variants resolve to interface level consts in javascript,
            # meaning their names could collide with other interface members.
            # Iterate through all CEnum variants to make sure there are no
            # collisions.
            self.iface.namemap.set(variant)
            # Value may be a lambda. If it is, resolve it.
            if variant.valueFn:
                next_value = variant.value = variant.valueFn(self.iface)
            else:
                variant.value = next_value
            next_value += 1

    def count(self):
        return 0

    def nativeType(self, calltype):
        suffix = "*" if "out" in calltype else ""
        return f"{self.iface.name}::{self.basename} {suffix}"

    def rustType(self, calltype):
        prefix = "*mut " if "out" in calltype else ""
        return f"{prefix}u{self.width}"

    def tsType(self):
        return f"{self.iface.name}.{self.basename}"

    def __str__(self):
        body = ", ".join(f"{v} = {v}" for v in self.variants)
        return f"\tcenum {self.name} : {self.width} {{ {body} }};\n"


# Infallible doesn't work for all return types.
#
# It also must be implemented on a builtinclass (otherwise it'd be unsound as
# it could be implemented by JS).
def ensureInfallibleIsSound(methodOrAttribute):
    if not methodOrAttribute.infallible:
        return
    ty = unaliasType(methodOrAttribute.realtype)
    if ty.kind not in [
        "builtin",
        "interface",
        "forward",
        "webidl",
        "cenum",
    ]:
        raise IDLError(
            "[infallible] only works on interfaces, domobjects, and builtin types "
            "(numbers, booleans, cenum, and raw char types)",
            methodOrAttribute.location,
        )
    ifaceAttributes = methodOrAttribute.iface.attributes
    if ifaceAttributes.scriptable and not ifaceAttributes.builtinclass:
        raise IDLError(
            "[infallible] attributes and methods are only allowed on "
            "non-[scriptable] or [builtinclass] interfaces",
            methodOrAttribute.location,
        )

    if methodOrAttribute.notxpcom:
        raise IDLError(
            "[infallible] does not make sense for a [notxpcom] method or attribute",
            methodOrAttribute.location,
        )


# An interface cannot be implemented by JS if it has a notxpcom or nostdcall
# method or attribute, or uses a by-value native type, so it must be marked as
# builtinclass.
def ensureBuiltinClassIfNeeded(methodOrAttribute):
    iface = methodOrAttribute.iface
    if not iface.attributes.scriptable or iface.attributes.builtinclass:
        return
    if iface.name == "nsISupports":
        return

    # notxpcom and nostdcall types change calling conventions, which breaks
    # xptcall wrappers. We cannot allow XPCWrappedJS to be created for
    # interfaces with these methods.
    if methodOrAttribute.notxpcom:
        raise IDLError(
            f"scriptable interface '{iface.name}' must be marked "
            f"[builtinclass] because it contains a [notxpcom] "
            f"{methodOrAttribute.kind} '{methodOrAttribute.name}'",
            methodOrAttribute.location,
        )
    if methodOrAttribute.nostdcall:
        raise IDLError(
            f"scriptable interface '{iface.name}' must be marked "
            f"[builtinclass] because it contains a [nostdcall] "
            f"{methodOrAttribute.kind} '{methodOrAttribute.name}'",
            methodOrAttribute.location,
        )

    # Methods with custom native parameters passed without indirection cannot be
    # safely handled by xptcall (as it cannot know the calling stack/register
    # layout), so require the interface to be builtinclass.
    #
    # Only "in" parameters and writable attributes are checked, as other
    # parameters are always passed indirectly, so do not impact calling
    # conventions.
    def typeNeedsBuiltinclass(type):
        inner = unaliasType(type)
        return (
            inner.kind == "native"
            and inner.specialtype is None
            and inner.modifier is None
        )

    if methodOrAttribute.kind == "method":
        for p in methodOrAttribute.params:
            if p.paramtype == "in" and typeNeedsBuiltinclass(p.realtype):
                raise IDLError(
                    f"scriptable interface '{iface.name}' must be marked "
                    f"[builtinclass] because it contains method "
                    f"'{methodOrAttribute.name}' with a by-value custom native "
                    f"parameter '{p.names}'",
                    methodOrAttribute.location,
                )
    elif methodOrAttribute.kind == "attribute" and not methodOrAttribute.readonly:
        if typeNeedsBuiltinclass(methodOrAttribute.realtype):
            raise IDLError(
                f"scriptable interface '{iface.name}' must be marked "
                f"[builtinclass] because it contains writable attribute "
                f"'{methodOrAttribute.name}' with a by-value custom native type",
                methodOrAttribute.location,
            )


def ensureNoscriptIfNeeded(methodOrAttribute):
    if not methodOrAttribute.isScriptable():
        return

    # NOTE: We can't check forward-declared interfaces to see if they're
    # scriptable, as the information about whether they're scriptable is not
    # known here.
    def typeNeedsNoscript(type):
        if type.kind in ["array", "legacyarray"]:
            return typeNeedsNoscript(type.type)
        if type.kind == "typedef":
            return typeNeedsNoscript(type.realtype)
        if type.kind == "native":
            return type.specialtype is None
        if type.kind == "interface":
            return not type.attributes.scriptable
        return False

    if typeNeedsNoscript(methodOrAttribute.realtype):
        raise IDLError(
            f"{methodOrAttribute.kind} '{methodOrAttribute.name}' must be "
            f"marked [noscript] because it has a non-scriptable type",
            methodOrAttribute.location,
        )
    if methodOrAttribute.kind == "method":
        for p in methodOrAttribute.params:
            # iid_is arguments have their type ignored, so shouldn't be checked.
            if not p.iid_is and typeNeedsNoscript(p.realtype):
                raise IDLError(
                    f"method '{methodOrAttribute.name}' must be marked "
                    f"[noscript] because it has a non-scriptable parameter "
                    f"'{p.name}'",
                    methodOrAttribute.location,
                )


class Attribute:
    kind = "attribute"
    noscript = False
    notxpcom = False
    readonly = False
    symbol = False
    implicit_jscontext = False
    nostdcall = False
    must_use = False
    binaryname = None
    infallible = False
    # explicit_setter_can_run_script is true if the attribute is explicitly
    # annotated as having a setter that can cause script to run.
    explicit_setter_can_run_script = False
    # explicit_getter_can_run_script is true if the attribute is explicitly
    # annotated as having a getter that can cause script to run.
    explicit_getter_can_run_script = False

    def __init__(self, type, name, attlist, readonly, location, doccomments):
        self.type = type
        self.name = name
        self.attlist = attlist
        self.readonly = readonly
        self.location = location
        self.doccomments = doccomments

        for attr_name, value, aloc in attlist:
            if attr_name == "binaryname":
                if value is None:
                    raise IDLError("binaryname attribute requires a value", aloc)

                self.binaryname = value
                continue

            if value is not None:
                raise IDLError("Unexpected attribute value", aloc)

            if attr_name == "noscript":
                self.noscript = True
            elif attr_name == "notxpcom":
                self.notxpcom = True
            elif attr_name == "symbol":
                self.symbol = True
            elif attr_name == "implicit_jscontext":
                self.implicit_jscontext = True
            elif attr_name == "nostdcall":
                self.nostdcall = True
            elif attr_name == "must_use":
                self.must_use = True
            elif attr_name == "infallible":
                self.infallible = True
            elif attr_name == "can_run_script":
                if (
                    self.explicit_setter_can_run_script
                    or self.explicit_getter_can_run_script
                ):
                    raise IDLError(
                        "Redundant getter_can_run_script or "
                        "setter_can_run_script annotation on "
                        "attribute",
                        aloc,
                    )
                self.explicit_setter_can_run_script = True
                self.explicit_getter_can_run_script = True
            elif attr_name == "setter_can_run_script":
                if self.explicit_setter_can_run_script:
                    raise IDLError(
                        "Redundant setter_can_run_script annotation on attribute",
                        aloc,
                    )
                self.explicit_setter_can_run_script = True
            elif attr_name == "getter_can_run_script":
                if self.explicit_getter_can_run_script:
                    raise IDLError(
                        "Redundant getter_can_run_script annotation on attribute",
                        aloc,
                    )
                self.explicit_getter_can_run_script = True
            else:
                raise IDLError(f"Unexpected attribute '{attr_name}'", aloc)

    def resolve(self, iface):
        self.iface = iface
        self.realtype = iface.idl.getName(self.type, self.location)

        ensureInfallibleIsSound(self)
        ensureBuiltinClassIfNeeded(self)
        ensureNoscriptIfNeeded(self)

    def toIDL(self):
        attribs = attlistToIDL(self.attlist)
        readonly = "readonly " if self.readonly else ""
        return f"{attribs}{readonly}attribute {self.type} {self.name};"

    def isScriptable(self):
        if not self.iface.attributes.scriptable:
            return False
        return not (self.noscript or self.notxpcom or self.nostdcall)

    def __str__(self):
        readonly = "readonly " if self.readonly else ""
        return f"\t{readonly}attribute {self.type} {self.name}\n"

    def count(self):
        return self.readonly and 1 or 2


class Method:
    kind = "method"
    noscript = False
    notxpcom = False
    symbol = False
    binaryname = None
    implicit_jscontext = False
    nostdcall = False
    must_use = False
    optional_argc = False
    # explicit_can_run_script is true if the method is explicitly annotated
    # as being able to cause script to run.
    explicit_can_run_script = False
    infallible = False

    def __init__(self, type, name, attlist, paramlist, location, doccomments, raises):
        self.type = type
        self.name = name
        self.attlist = attlist
        self.params = paramlist
        self.location = location
        self.doccomments = doccomments
        self.raises = raises

        for attr_name, value, aloc in attlist:
            if attr_name == "binaryname":
                if value is None:
                    raise IDLError("binaryname attribute requires a value", aloc)

                self.binaryname = value
                continue

            if value is not None:
                raise IDLError("Unexpected attribute value", aloc)

            if attr_name == "noscript":
                self.noscript = True
            elif attr_name == "notxpcom":
                self.notxpcom = True
            elif attr_name == "symbol":
                self.symbol = True
            elif attr_name == "implicit_jscontext":
                self.implicit_jscontext = True
            elif attr_name == "optional_argc":
                self.optional_argc = True
            elif attr_name == "nostdcall":
                self.nostdcall = True
            elif attr_name == "must_use":
                self.must_use = True
            elif attr_name == "can_run_script":
                self.explicit_can_run_script = True
            elif attr_name == "infallible":
                self.infallible = True
            else:
                raise IDLError(f"Unexpected attribute '{attr_name}'", aloc)

        self.namemap = NameMap()
        for p in paramlist:
            self.namemap.set(p)

    def resolve(self, iface):
        self.iface = iface
        self.realtype = self.iface.idl.getName(self.type, self.location)

        for p in self.params:
            p.resolve(self)
        for p in self.params:
            if p.retval and p != self.params[-1]:
                raise IDLError(
                    f"'retval' parameter '{p.name}' is not the last parameter",
                    self.location,
                )
            if p.size_is:
                size_param = self.namemap.get(p.size_is, p.location)
                if (
                    p.paramtype.count("in") == 1
                    and size_param.paramtype.count("in") == 0
                ):
                    raise IDLError(
                        "size_is parameter of an input must also be an input",
                        p.location,
                    )
                if getBuiltinOrNativeTypeName(size_param.realtype) != "uint32_t":
                    raise IDLError(
                        "size_is parameter must have type 'uint32_t'",
                        p.location,
                    )
            if p.iid_is:
                iid_param = self.namemap.get(p.iid_is, p.location)
                if (
                    p.paramtype.count("in") == 1
                    and iid_param.paramtype.count("in") == 0
                ):
                    raise IDLError(
                        "iid_is parameter of an input must also be an input",
                        p.location,
                    )
                if getBuiltinOrNativeTypeName(iid_param.realtype) != "[nsid]":
                    raise IDLError(
                        "iid_is parameter must be an nsIID",
                        self.location,
                    )

        ensureInfallibleIsSound(self)
        ensureBuiltinClassIfNeeded(self)
        ensureNoscriptIfNeeded(self)

    def isScriptable(self):
        if not self.iface.attributes.scriptable:
            return False
        return not (self.noscript or self.notxpcom or self.nostdcall)

    def __str__(self):
        params = (", ".join([p.name for p in self.params]),)
        return f"\t{self.type} {self.name}({params})\n"

    def toIDL(self):
        if len(self.raises):
            raises = ",".join(self.raises)
            raises = f" raises ({raises})"
        else:
            raises = ""

        attribs = attlistToIDL(self.attlist)
        params = ", ".join([p.toIDL() for p in self.params])
        return f"{attribs}{self.type} {self.name} ({params}){raises};"

    def needsJSTypes(self):
        if self.implicit_jscontext:
            return True
        if self.type == TypeId("jsval"):
            return True
        for p in self.params:
            t = p.realtype
            if isinstance(t, Native) and t.specialtype == "jsval":
                return True
        return False

    def count(self):
        return 1


class Param:
    size_is = None
    iid_is = None
    const = False
    array = False
    retval = False
    shared = False
    optional = False
    default_value = None

    def __init__(self, paramtype, type, name, attlist, location, realtype=None):
        self.paramtype = paramtype
        self.type = type
        self.name = name
        self.attlist = attlist
        self.location = location
        self.realtype = realtype

        for attr_name, value, aloc in attlist:
            # Put the value-taking attributes first!
            if attr_name == "size_is":
                if value is None:
                    raise IDLError("'size_is' must specify a parameter", aloc)
                self.size_is = value
            elif attr_name == "iid_is":
                if value is None:
                    raise IDLError("'iid_is' must specify a parameter", aloc)
                self.iid_is = value
            elif attr_name == "default":
                if value is None:
                    raise IDLError("'default' must specify a default value", aloc)
                self.default_value = value
            else:
                if value is not None:
                    raise IDLError(
                        f"Unexpected value for attribute '{attr_name}'", aloc
                    )

                if attr_name == "const":
                    self.const = True
                elif attr_name == "array":
                    self.array = True
                elif attr_name == "retval":
                    self.retval = True
                elif attr_name == "shared":
                    self.shared = True
                elif attr_name == "optional":
                    self.optional = True
                else:
                    raise IDLError(f"Unexpected attribute '{attr_name}'", aloc)

    def resolve(self, method):
        self.realtype = method.iface.idl.getName(self.type, self.location)
        if self.array:
            self.realtype = LegacyArray(self.realtype)

    def nativeType(self):
        kwargs = {}
        if self.shared:
            kwargs["shared"] = True
        if self.const:
            kwargs["const"] = True

        try:
            return self.realtype.nativeType(self.paramtype, **kwargs)
        except IDLError as e:
            raise IDLError(str(e), self.location)
        except TypeError:
            raise IDLError("Unexpected parameter attribute", self.location)

    def rustType(self):
        kwargs = {}
        if self.shared:
            kwargs["shared"] = True
        if self.const:
            kwargs["const"] = True

        try:
            return self.realtype.rustType(self.paramtype, **kwargs)
        except IDLError as e:
            raise IDLError(str(e), self.location)
        except TypeError:
            raise IDLError("Unexpected parameter attribute", self.location)

    def toIDL(self):
        attribs = paramAttlistToIDL(self.attlist)
        return f"{attribs}{self.paramtype} {self.type} {self.name}"

    def tsType(self):
        # A generic retval param type needs special handling.
        if self.retval and self.iid_is:
            return "nsQIResult"

        type = self.realtype.tsType()
        if self.paramtype == "inout":
            return f"InOutParam<{type}>"
        if self.paramtype == "out" and not self.retval:
            return f"OutParam<{type}>"
        return type


class LegacyArray:
    kind = "legacyarray"

    def __init__(self, basetype):
        self.type = basetype
        self.location = self.type.location

    def nativeType(self, calltype, const=False):
        if "element" in calltype:
            raise IDLError("nested [array] unsupported", self.location)

        # For legacy reasons, we have to add a 'const ' to builtin pointer array
        # types. (`[array] in string` and `[array] in wstring` parameters)
        if (
            calltype == "in"
            and isinstance(self.type, Builtin)
            and self.type.isPointer()
        ):
            const = True

        prefix = "const " if const else ""
        elemtype = self.type.nativeType("legacyelement")
        suffix = "*" if "out" in calltype else ""
        return f"{prefix}{elemtype}*{suffix}"

    def rustType(self, calltype, const=False):
        prefix1 = "*mut " if "out" in calltype else ""
        prefix2 = "*const " if const else "*mut "
        elemtype = self.type.rustType("legacyelement")
        return f"{prefix1}{prefix2}{elemtype}"

    def tsType(self):
        return self.type.tsType() + "[]"


class Array:
    kind = "array"

    def __init__(self, type, location):
        self.type = type
        self.location = location

    @property
    def name(self):
        return f"Array<{self.type.name}>"

    def resolve(self, idl):
        idl.getName(self.type, self.location)

    def nativeType(self, calltype):
        if calltype == "legacyelement":
            raise IDLError("[array] Array<T> is unsupported", self.location)

        elemtype = self.type.nativeType("element")
        base = f"nsTArray<{elemtype}>"
        if "out" in calltype:
            return f"{base}& "
        if "in" == calltype:
            return f"const {base}& "
        return base

    def rustType(self, calltype):
        if calltype == "legacyelement":
            raise IDLError("[array] Array<T> is unsupported", self.location)

        elemtype = self.type.rustType("element")
        if "out" in calltype:
            prefix = "*mut "
        elif "in" == calltype:
            prefix = "*const "
        else:
            prefix = ""
        return f"{prefix}thin_vec::ThinVec<{elemtype}>"

    def tsType(self):
        return self.type.tsType() + "[]"


TypeId = namedtuple("TypeId", "name params")


# Make str(TypeId) produce a nicer value
TypeId.__str__ = lambda self: (
    f"{self.name}<{', '.join(str(p) for p in self.params)}>"
    if self.params is not None
    else self.name
)


# Allow skipping 'params' in TypeId(..)
TypeId.__new__.__defaults__ = (None,)


class IDLParser:
    keywords = {
        "cenum": "CENUM",
        "const": "CONST",
        "interface": "INTERFACE",
        "in": "IN",
        "inout": "INOUT",
        "out": "OUT",
        "attribute": "ATTRIBUTE",
        "raises": "RAISES",
        "readonly": "READONLY",
        "native": "NATIVE",
        "typedef": "TYPEDEF",
        "webidl": "WEBIDL",
    }

    tokens = [
        "IDENTIFIER",
        "CDATA",
        "INCLUDE",
        "IID",
        "NUMBER",
        "HEXNUM",
        "LSHIFT",
        "RSHIFT",
        "NATIVEID",
    ]

    tokens.extend(keywords.values())

    states = (("nativeid", "exclusive"),)

    t_NUMBER = r"-?\d+"
    t_HEXNUM = "0x[a-fA-F0-9]+"
    t_LSHIFT = r"<<"
    t_RSHIFT = r">>"

    literals = '"(){}[]<>,;:=|+-*'

    t_ignore = " \t"

    def t_multilinecomment(self, t):
        r"/\*(\n|.)*?\*/"
        t.lexer.lineno += t.value.count("\n")
        if t.value.startswith("/**"):
            self._doccomments.append(t.value)

    def t_singlelinecomment(self, t):
        r"//[^\n]*"

    def t_IID(self, t):
        r"[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}"
        return t

    def t_IDENTIFIER(self, t):
        r"(unsigned\ long\ long|unsigned\ short|unsigned\ long|long\ long)(?!_?[A-Za-z][A-Za-z_0-9])|_?[A-Za-z][A-Za-z_0-9]*"  # NOQA: E501
        t.type = self.keywords.get(t.value, "IDENTIFIER")
        return t

    def t_LCDATA(self, t):
        r"%\{[ ]*C\+\+[ ]*\n(?P<cdata>(\n|.)*?\n?)%\}[ ]*(C\+\+)?"
        t.type = "CDATA"
        t.lexer.lineno += t.value.count("\n")
        t.value = t.lexer.lexmatch.group("cdata")
        return t

    def t_INCLUDE(self, t):
        r'\#include[ \t]+"[^"\n]+"'
        inc, value, end = t.value.split('"')
        t.value = value
        return t

    def t_directive(self, t):
        r"\#(?P<directive>[a-zA-Z]+)[^\n]+"
        directive = t.lexer.lexmatch.group("directive")
        raise IDLError(
            f"Unrecognized directive {directive}",
            Location(
                lexer=self.lexer, lineno=self.lexer.lineno, lexpos=self.lexer.lexpos
            ),
        )

    def t_newline(self, t):
        r"\n+"
        t.lexer.lineno += len(t.value)

    def t_nativeid_NATIVEID(self, t):
        # Matches non-parenthesis characters, or a single open and closing
        # parenthesis with at least one non-parenthesis character before,
        # between and after them (for compatibility with std::function).
        r"[^()\n]+(?:\([^()\n]+\)[^()\n]+)?(?=\))"
        t.lexer.begin("INITIAL")
        return t

    t_nativeid_ignore = ""

    def t_ANY_error(self, t):
        raise IDLError(
            "unrecognized input",
            Location(
                lexer=self.lexer, lineno=self.lexer.lineno, lexpos=self.lexer.lexpos
            ),
        )

    precedence = (
        ("left", "|"),
        ("left", "LSHIFT", "RSHIFT"),
        ("left", "+", "-"),
        ("left", "*"),
        ("left", "UMINUS"),
    )

    def p_idlfile(self, p):
        """idlfile : productions"""
        p[0] = IDL(p[1])

    def p_productions_start(self, p):
        """productions :"""
        p[0] = []

    def p_productions_cdata(self, p):
        """productions : CDATA productions"""
        p[0] = list(p[2])
        p[0].insert(0, CDATA(p[1], self.getLocation(p, 1)))

    def p_productions_include(self, p):
        """productions : INCLUDE productions"""
        p[0] = list(p[2])
        p[0].insert(0, Include(p[1], self.getLocation(p, 1)))

    def p_productions_interface(self, p):
        """productions : interface productions
        | typedef productions
        | native productions
        | webidl productions"""
        p[0] = list(p[2])
        p[0].insert(0, p[1])

    def p_typedef(self, p):
        """typedef : attributes TYPEDEF type IDENTIFIER ';'"""
        p[0] = Typedef(
            type=p[3],
            name=p[4],
            attlist=p[1]["attlist"],
            location=self.getLocation(p, 2),
            doccomments=getattr(p[1], "doccomments", []) + p.slice[2].doccomments,
        )

    def p_native(self, p):
        """native : attributes NATIVE IDENTIFIER afternativeid '(' NATIVEID ')' ';'"""
        p[0] = Native(
            name=p[3],
            nativename=p[6],
            attlist=p[1]["attlist"],
            location=self.getLocation(p, 2),
        )

    def p_afternativeid(self, p):
        """afternativeid :"""
        # this is a place marker: we switch the lexer into literal identifier
        # mode here, to slurp up everything until the closeparen
        self.lexer.begin("nativeid")

    def p_webidl(self, p):
        """webidl : WEBIDL IDENTIFIER ';'"""
        p[0] = WebIDL(name=p[2], location=self.getLocation(p, 2))

    def p_anyident(self, p):
        """anyident : IDENTIFIER
        | CONST"""
        p[0] = {"value": p[1], "location": self.getLocation(p, 1)}

    def p_attributes(self, p):
        """attributes : '[' attlist ']'
        |"""
        if len(p) == 1:
            p[0] = {"attlist": []}
        else:
            p[0] = {"attlist": p[2], "doccomments": p.slice[1].doccomments}

    def p_attlist_start(self, p):
        """attlist : attribute"""
        p[0] = [p[1]]

    def p_attlist_continue(self, p):
        """attlist : attribute ',' attlist"""
        p[0] = list(p[3])
        p[0].insert(0, p[1])

    def p_attribute(self, p):
        """attribute : anyident attributeval"""
        p[0] = (p[1]["value"], p[2], p[1]["location"])

    def p_attributeval(self, p):
        """attributeval : '(' IDENTIFIER ')'
        | '(' IID ')'
        |"""
        if len(p) > 1:
            p[0] = p[2]

    def p_interface(self, p):
        """interface : attributes INTERFACE IDENTIFIER ifacebase ifacebody ';'"""
        atts, INTERFACE, name, base, body, SEMI = p[1:]
        attlist = atts["attlist"]
        doccomments = []
        if "doccomments" in atts:
            doccomments.extend(atts["doccomments"])
        doccomments.extend(p.slice[2].doccomments)

        def loc():
            return self.getLocation(p, 2)

        if body is None:
            # forward-declared interface... must not have attributes!
            if len(attlist) != 0:
                raise IDLError(
                    "Forward-declared interface must not have attributes", loc()
                )

            if base is not None:
                raise IDLError("Forward-declared interface must not have a base", loc())
            p[0] = Forward(name=name, location=loc(), doccomments=doccomments)
        else:
            p[0] = Interface(
                name=name,
                attlist=attlist,
                base=base,
                members=body,
                location=loc(),
                doccomments=doccomments,
            )

    def p_ifacebody(self, p):
        """ifacebody : '{' members '}'
        |"""
        if len(p) > 1:
            p[0] = p[2]

    def p_ifacebase(self, p):
        """ifacebase : ':' IDENTIFIER
        |"""
        if len(p) == 3:
            p[0] = p[2]

    def p_members_start(self, p):
        """members :"""
        p[0] = []

    def p_members_continue(self, p):
        """members : member members"""
        p[0] = list(p[2])
        p[0].insert(0, p[1])

    def p_member_cdata(self, p):
        """member : CDATA"""
        p[0] = CDATA(p[1], self.getLocation(p, 1))

    def p_member_const(self, p):
        """member : CONST type IDENTIFIER '=' number ';'"""
        p[0] = ConstMember(
            type=p[2],
            name=p[3],
            value=p[5],
            location=self.getLocation(p, 1),
            doccomments=p.slice[1].doccomments,
        )

    # All "number" products return a function(interface)

    def p_number_decimal(self, p):
        """number : NUMBER"""
        n = int(p[1])
        p[0] = lambda i: n

    def p_number_hex(self, p):
        """number : HEXNUM"""
        n = int(p[1], 16)
        p[0] = lambda i: n

    def p_number_identifier(self, p):
        """number : IDENTIFIER"""
        id = p[1]
        loc = self.getLocation(p, 1)
        p[0] = lambda i: i.getConst(id, loc)

    def p_number_paren(self, p):
        """number : '(' number ')'"""
        p[0] = p[2]

    def p_number_neg(self, p):
        """number : '-' number %prec UMINUS"""
        n = p[2]
        p[0] = lambda i: -n(i)

    def p_number_add(self, p):
        """number : number '+' number
        | number '-' number
        | number '*' number"""
        n1 = p[1]
        n2 = p[3]
        if p[2] == "+":
            p[0] = lambda i: n1(i) + n2(i)
        elif p[2] == "-":
            p[0] = lambda i: n1(i) - n2(i)
        else:
            p[0] = lambda i: n1(i) * n2(i)

    def p_number_shift(self, p):
        """number : number LSHIFT number
        | number RSHIFT number"""
        n1 = p[1]
        n2 = p[3]
        if p[2] == "<<":
            p[0] = lambda i: n1(i) << n2(i)
        else:
            p[0] = lambda i: n1(i) >> n2(i)

    def p_number_bitor(self, p):
        """number : number '|' number"""
        n1 = p[1]
        n2 = p[3]
        p[0] = lambda i: n1(i) | n2(i)

    def p_member_cenum(self, p):
        """member : CENUM IDENTIFIER ':' NUMBER '{' variants '}' ';'"""
        p[0] = CEnum(
            name=p[2],
            width=int(p[4]),
            variants=p[6],
            location=self.getLocation(p, 1),
            doccomments=p.slice[1].doccomments,
        )

    def p_variants_start(self, p):
        """variants :"""
        p[0] = []

    def p_variants_single(self, p):
        """variants : variant"""
        p[0] = [p[1]]

    def p_variants_continue(self, p):
        """variants : variant ',' variants"""
        p[0] = [p[1]] + p[3]

    def p_variant_implicit(self, p):
        """variant : IDENTIFIER"""
        p[0] = CEnumVariant(p[1], None, self.getLocation(p, 1))

    def p_variant_explicit(self, p):
        """variant : IDENTIFIER '=' number"""
        p[0] = CEnumVariant(p[1], p[3], self.getLocation(p, 1))

    def p_member_att(self, p):
        """member : attributes optreadonly ATTRIBUTE type IDENTIFIER ';'"""
        if "doccomments" in p[1]:
            doccomments = p[1]["doccomments"]
        elif p[2] is not None:
            doccomments = p[2]
        else:
            doccomments = p.slice[3].doccomments

        p[0] = Attribute(
            type=p[4],
            name=p[5],
            attlist=p[1]["attlist"],
            readonly=p[2] is not None,
            location=self.getLocation(p, 3),
            doccomments=doccomments,
        )

    def p_member_method(self, p):
        """member : attributes type IDENTIFIER '(' paramlist ')' raises ';'"""
        if "doccomments" in p[1]:
            doccomments = p[1]["doccomments"]
        else:
            doccomments = p.slice[2].doccomments

        p[0] = Method(
            type=p[2],
            name=p[3],
            attlist=p[1]["attlist"],
            paramlist=p[5],
            location=self.getLocation(p, 3),
            doccomments=doccomments,
            raises=p[7],
        )

    def p_paramlist(self, p):
        """paramlist : param moreparams
        |"""
        if len(p) == 1:
            p[0] = []
        else:
            p[0] = list(p[2])
            p[0].insert(0, p[1])

    def p_moreparams_start(self, p):
        """moreparams :"""
        p[0] = []

    def p_moreparams_continue(self, p):
        """moreparams : ',' param moreparams"""
        p[0] = list(p[3])
        p[0].insert(0, p[2])

    def p_param(self, p):
        """param : attributes paramtype type IDENTIFIER"""
        p[0] = Param(
            paramtype=p[2],
            type=p[3],
            name=p[4],
            attlist=p[1]["attlist"],
            location=self.getLocation(p, 4),
        )

    def p_paramtype(self, p):
        """paramtype : IN
        | INOUT
        | OUT"""
        p[0] = p[1]

    def p_optreadonly(self, p):
        """optreadonly : READONLY
        |"""
        if len(p) > 1:
            p[0] = p.slice[1].doccomments
        else:
            p[0] = None

    def p_raises(self, p):
        """raises : RAISES '(' idlist ')'
        |"""
        if len(p) == 1:
            p[0] = []
        else:
            p[0] = p[3]

    def p_idlist(self, p):
        """idlist : IDENTIFIER"""
        p[0] = [p[1]]

    def p_idlist_continue(self, p):
        """idlist : IDENTIFIER ',' idlist"""
        p[0] = list(p[3])
        p[0].insert(0, p[1])

    def p_type_id(self, p):
        """type : IDENTIFIER"""
        p[0] = TypeId(name=p[1])
        p.slice[0].doccomments = p.slice[1].doccomments

    def p_type_generic(self, p):
        """type : IDENTIFIER '<' typelist '>'"""
        p[0] = TypeId(name=p[1], params=p[3])
        p.slice[0].doccomments = p.slice[1].doccomments

    def p_typelist(self, p):
        """typelist : type"""
        p[0] = [p[1]]

    def p_typelist_continue(self, p):
        """typelist : type ',' typelist"""
        p[0] = list(p[3])
        p[0].insert(0, p[1])

    def p_error(self, t):
        if not t:
            raise IDLError(
                "Syntax Error at end of file. Possibly due to missing semicolon(;), braces(}) "
                "or both",
                None,
            )
        else:
            location = Location(self.lexer, t.lineno, t.lexpos)
            raise IDLError("invalid syntax", location)

    def __init__(self):
        self._doccomments = []
        self.lexer = lex.lex(object=self, debug=False)
        self.parser = yacc.yacc(module=self, write_tables=False, debug=False)

    def clearComments(self):
        self._doccomments = []

    def token(self):
        t = self.lexer.token()
        if t is not None and t.type != "CDATA":
            t.doccomments = self._doccomments
            self._doccomments = []
        return t

    def parse(self, data, filename=None):
        if filename is not None:
            self.lexer.filename = filename
        self.lexer.lineno = 1
        self.lexer.input(data)
        idl = self.parser.parse(lexer=self)
        if filename is not None:
            idl.deps.append(filename)
        return idl

    def getLocation(self, p, i):
        return Location(self.lexer, p.lineno(i), p.lexpos(i))


if __name__ == "__main__":
    p = IDLParser()
    for f in sys.argv[1:]:
        print(f"Parsing {f}")
        p.parse(open(f, encoding="utf-8").read(), filename=f)
