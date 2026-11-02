# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys

# Used to get the list of Gecko process types
# xpcom/geckoprocesstypes_generator/geckoprocesstypes/__init__.py
import geckoprocesstypes

from ipdl import builtin
from ipdl.ast import (
    ASYNC,
    IN,
    INOUT,
    INSIDE_CPOW_NESTED,
    INSIDE_SYNC_NESTED,
    NOT_NESTED,
    OUT,
    SYNC,
    CxxInclude,
    Decl,
    Loc,
    QualifiedId,
    StringLiteral,
    StructDecl,
    UnionDecl,
    UsingStmt,
    Visitor,
    priorityList,
)
from ipdl.util import hash_str

_DELETE_MSG = "__delete__"

# Process attribute values which may include the parent process.
_MAYBE_PARENT_PROCS = ("any", "anydom", "compositor", "Parent")


class TypeVisitor:
    def __init__(self):
        self.visited = set()

    def defaultVisit(self, node, *args):
        raise Exception(
            "INTERNAL ERROR: no visitor for node type `%s'" % (node.__class__.__name__)
        )

    def visitVoidType(self, v, *args):
        pass

    def visitBuiltinCType(self, b, *args):
        pass

    def visitImportedCxxType(self, t, *args):
        pass

    def visitMessageType(self, m, *args):
        for param in m.params:
            param.accept(self, *args)
        for ret in m.returns:
            ret.accept(self, *args)
        if m.cdtype is not None:
            m.cdtype.accept(self, *args)

    def visitProtocolType(self, p, *args):
        # NB: don't visit manager and manages. a naive default impl
        # could result in an infinite loop
        pass

    def visitActorType(self, a, *args):
        a.protocol.accept(self, *args)

    def visitStructType(self, s, *args):
        if s in self.visited:
            return

        self.visited.add(s)
        for field in s.fields:
            field.accept(self, *args)

    def visitUnionType(self, u, *args):
        if u in self.visited:
            return

        self.visited.add(u)
        for component in u.components:
            component.accept(self, *args)

    def visitArrayType(self, a, *args):
        a.basetype.accept(self, *args)

    def visitMaybeType(self, m, *args):
        m.basetype.accept(self, *args)

    def visitUniquePtrType(self, m, *args):
        m.basetype.accept(self, *args)

    def visitNotNullType(self, m, *args):
        m.basetype.accept(self, *args)

    def visitShmemType(self, s, *args):
        pass

    def visitByteBufType(self, s, *args):
        pass

    def visitShmemChmodType(self, c, *args):
        c.shmem.accept(self)

    def visitFDType(self, s, *args):
        pass

    def visitEndpointType(self, s, *args):
        pass

    def visitManagedEndpointType(self, s, *args):
        pass


class Type:
    def __cmp__(self, o):
        return (self.fullname() > o.fullname()) - (self.fullname() < o.fullname())

    def __eq__(self, o):
        return self.__class__ == o.__class__ and self.fullname() == o.fullname()

    def __hash__(self):
        return hash_str(self.fullname())

    # Is this a C++ type?
    def isCxx(self):
        return False

    # Is this an IPDL type?

    def isIPDL(self):
        return False

    # Is this type neither compound nor an array?

    def isAtom(self):
        return False

    def isRefcounted(self):
        return False

    def isTainted(self):
        return False

    # Should this type be wrapped in `NotNull<T>` unless marked `nullable`?

    def supportsNullable(self):
        return False

    def typename(self):
        return self.__class__.__name__

    def name(self):
        raise NotImplementedError()

    def fullname(self):
        raise NotImplementedError()

    def accept(self, visitor, *args):
        visit = getattr(visitor, "visit" + self.__class__.__name__, None)
        if visit is None:
            return getattr(visitor, "defaultVisit")(self, *args)
        return visit(self, *args)


class VoidType(Type):
    def isCxx(self):
        return True

    def isIPDL(self):
        return False

    def isAtom(self):
        return True

    def name(self):
        return "void"

    def fullname(self):
        return "void"


VOID = VoidType()

# --------------------


class BuiltinCType(Type):
    def __init__(self, name):
        self._name = name

    def isCxx(self):
        return True

    def isAtom(self):
        return True

    def isSendMoveOnly(self):
        return False

    def isDataMoveOnly(self):
        return False

    def name(self):
        return self._name

    def fullname(self):
        return self._name


class ImportedCxxType(Type):
    def __init__(self, qname, refcounted, sendmoveonly, datamoveonly, tainted=False):
        assert isinstance(qname, QualifiedId)
        self.loc = qname.loc
        self.qname = qname
        self.refcounted = refcounted
        self.sendmoveonly = sendmoveonly
        self.datamoveonly = datamoveonly
        self.tainted = tainted

    def isCxx(self):
        return True

    def isAtom(self):
        return True

    def isRefcounted(self):
        return self.refcounted

    def supportsNullable(self):
        return self.refcounted

    def isTainted(self):
        return self.tainted

    def isSendMoveOnly(self):
        return self.sendmoveonly

    def isDataMoveOnly(self):
        return self.datamoveonly

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


# --------------------


class IPDLType(Type):
    def isIPDL(self):
        return True

    def isMessage(self):
        return False

    def isProtocol(self):
        return False

    def isActor(self):
        return False

    def isStruct(self):
        return False

    def isUnion(self):
        return False

    def isArray(self):
        return False

    def isMaybe(self):
        return False

    def isUniquePtr(self):
        return False

    def isNotNull(self):
        return False

    def isAtom(self):
        return True

    def isCompound(self):
        return False

    def isShmem(self):
        return False

    def isByteBuf(self):
        return False

    def isFD(self):
        return False

    def isEndpoint(self):
        return False

    def isManagedEndpoint(self):
        return False

    def hasBaseType(self):
        return False


class SendSemanticsType(IPDLType):
    def __init__(self, nestedRange, sendSemantics):
        self.nestedRange = nestedRange
        self.sendSemantics = sendSemantics

    def isAsync(self):
        return self.sendSemantics == ASYNC

    def isSync(self):
        return self.sendSemantics == SYNC

    def sendSemanticsSatisfiedBy(self, greater):
        def _unwrap(nr):
            if isinstance(nr, dict):
                return _unwrap(nr["nested"])
            elif isinstance(nr, int):
                return nr
            else:
                raise ValueError("Got unexpected nestedRange value: %s" % nr)

        lesser = self
        lnr0, gnr0, lnr1, gnr1 = (
            _unwrap(lesser.nestedRange[0]),
            _unwrap(greater.nestedRange[0]),
            _unwrap(lesser.nestedRange[1]),
            _unwrap(greater.nestedRange[1]),
        )
        if lnr0 < gnr0 or lnr1 > gnr1:
            return False

        if lesser.isAsync():
            return True
        elif lesser.isSync() and not greater.isAsync():
            return True

        return False


class MessageType(SendSemanticsType):
    def __init__(
        self,
        nested,
        prio,
        replyPrio,
        sendSemantics,
        direction,
        ctor=False,
        dtor=False,
        cdtype=None,
        compress=False,
        tainted=False,
        lazySend=False,
    ):
        assert not (ctor and dtor)
        assert not (ctor or dtor) or cdtype is not None

        SendSemanticsType.__init__(self, (nested, nested), sendSemantics)
        self.nested = nested
        self.prio = prio
        self.replyPrio = replyPrio
        self.direction = direction
        self.params = []
        self.returns = []
        self.ctor = ctor
        self.dtor = dtor
        self.cdtype = cdtype
        self.compress = compress
        self.tainted = tainted
        self.lazySend = lazySend

    def isMessage(self):
        return True

    def isCtor(self):
        return self.ctor

    def isDtor(self):
        return self.dtor

    def constructedType(self):
        return self.cdtype

    def isIn(self):
        return self.direction is IN

    def isOut(self):
        return self.direction is OUT

    def isInout(self):
        return self.direction is INOUT

    def hasReply(self):
        return len(self.returns) or self.isSync()

    def hasImplicitActorParam(self):
        return self.isCtor()


class ProtocolType(SendSemanticsType):
    def __init__(
        self,
        qname,
        nested,
        sendSemantics,
        refcounted,
        needsotherpid,
        parentproc=None,
        childproc=None,
    ):
        SendSemanticsType.__init__(self, (NOT_NESTED, nested), sendSemantics)
        self.qname = qname
        self.managers = []  # ProtocolType
        self.manages = []
        self.hasDelete = False
        self.refcounted = refcounted
        self.needsotherpid = needsotherpid
        self.procs = {"parent": parentproc, "child": childproc}

    def isProtocol(self):
        return True

    def isRefcounted(self):
        return self.refcounted

    def hasOtherPid(self):
        return all(top.needsotherpid for top in self.toplevels())

    def mayReceiveFromLessTrusted(self, side):
        """Whether |side| may live in the parent process while the other side
        lives in a different process."""
        other = "child" if side == "parent" else "parent"
        for top in self.toplevels():
            recvproc = top.procs[side] or "any"
            sendproc = top.procs[other] or "any"
            if recvproc in _MAYBE_PARENT_PROCS and sendproc != "Parent":
                return True
        return False

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)

    def addManager(self, mgrtype):
        assert mgrtype.isIPDL() and mgrtype.isProtocol()
        self.managers.append(mgrtype)

    def managedBy(self, mgr):
        self.managers = list(mgr)

    def toplevel(self):
        if self.isToplevel():
            return self
        for mgr in self.managers:
            if mgr is not self:
                return mgr.toplevel()

    def toplevels(self):
        if self.isToplevel():
            return [self]
        toplevels = list()
        for mgr in self.managers:
            if mgr is not self:
                toplevels.extend(mgr.toplevels())
        return set(toplevels)

    def isManagerOf(self, pt):
        for managed in self.manages:
            if pt is managed:
                return True
        return False

    def isManagedBy(self, pt):
        return pt in self.managers

    def isManager(self):
        return len(self.manages) > 0

    def isManaged(self):
        return 0 < len(self.managers)

    def isToplevel(self):
        return not self.isManaged()

    def manager(self):
        assert 1 == len(self.managers)
        for mgr in self.managers:
            return mgr


class ActorType(IPDLType):
    def __init__(self, protocol):
        self.protocol = protocol

    def isActor(self):
        return True

    def isRefcounted(self):
        return self.protocol.isRefcounted()

    def supportsNullable(self):
        return True

    def name(self):
        return self.protocol.name()

    def fullname(self):
        return self.protocol.fullname()


class _CompoundType(IPDLType):
    def __init__(self):
        self.defined = False  # bool
        self.mutualRec = set()  # set(_CompoundType | ArrayType)

    def isAtom(self):
        return False

    def isCompound(self):
        return True

    def itercomponents(self):
        raise Exception('"pure virtual" method')

    def mutuallyRecursiveWith(self, t, exploring=None):
        """|self| is mutually recursive with |t| iff |self| and |t|
        are in a cycle in the type graph rooted at |self|.  This function
        looks for such a cycle and returns True if found."""
        if exploring is None:
            exploring = set()

        if t.isAtom():
            return False
        elif t is self or t in self.mutualRec:
            return True
        elif t.hasBaseType():
            isrec = self.mutuallyRecursiveWith(t.basetype, exploring)
            if isrec:
                self.mutualRec.add(t)
            return isrec
        elif t in exploring:
            return False

        exploring.add(t)
        for c in t.itercomponents():
            if self.mutuallyRecursiveWith(c, exploring):
                self.mutualRec.add(c)
                return True
        exploring.remove(t)

        return False


class StructType(_CompoundType):
    def __init__(self, qname, fields):
        _CompoundType.__init__(self)
        self.qname = qname
        self.fields = fields  # [ Type ]

    def isStruct(self):
        return True

    def itercomponents(self):
        yield from self.fields

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class UnionType(_CompoundType):
    def __init__(self, qname, components):
        _CompoundType.__init__(self)
        self.qname = qname
        self.components = components  # [ Type ]

    def isUnion(self):
        return True

    def itercomponents(self):
        yield from self.components

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class ArrayType(IPDLType):
    def __init__(self, basetype):
        self.basetype = basetype

    def isAtom(self):
        return False

    def isArray(self):
        return True

    def hasBaseType(self):
        return True

    def name(self):
        return self.basetype.name() + "[]"

    def fullname(self):
        return self.basetype.fullname() + "[]"


class MaybeType(IPDLType):
    def __init__(self, basetype):
        self.basetype = basetype

    def isAtom(self):
        return False

    def isMaybe(self):
        return True

    def hasBaseType(self):
        return True

    def name(self):
        return self.basetype.name() + "?"

    def fullname(self):
        return self.basetype.fullname() + "?"


class ShmemType(IPDLType):
    def __init__(self, qname):
        self.qname = qname

    def isShmem(self):
        return True

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class ByteBufType(IPDLType):
    def __init__(self, qname):
        self.qname = qname

    def isByteBuf(self):
        return True

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class FDType(IPDLType):
    def __init__(self, qname):
        self.qname = qname

    def isFD(self):
        return True

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class EndpointType(IPDLType):
    def __init__(self, qname, actor):
        self.qname = qname
        self.actor = actor

    def isEndpoint(self):
        return True

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class ManagedEndpointType(IPDLType):
    def __init__(self, qname, actor):
        self.qname = qname
        self.actor = actor

    def isManagedEndpoint(self):
        return True

    def name(self):
        return self.qname.baseid

    def fullname(self):
        return str(self.qname)


class UniquePtrType(IPDLType):
    def __init__(self, basetype):
        self.basetype = basetype

    def isAtom(self):
        return False

    def isUniquePtr(self):
        return True

    def hasBaseType(self):
        return True

    def name(self):
        return "UniquePtr<" + self.basetype.name() + ">"

    def fullname(self):
        return "mozilla::UniquePtr<" + self.basetype.fullname() + ">"


class NotNullType(IPDLType):
    def __init__(self, basetype):
        self.basetype = basetype

    def isAtom(self):
        return False

    def isNotNull(self):
        return True

    def hasBaseType(self):
        return True

    def name(self):
        return "NotNull<" + self.basetype.name() + ">"

    def fullname(self):
        return "mozilla::NotNull<" + self.basetype.fullname() + ">"


def iteractortypes(t, visited=None):
    """Iterate over any actor(s) buried in |type|."""
    if visited is None:
        visited = set()

    # XXX |yield| semantics makes it hard to use TypeVisitor
    if not t.isIPDL():
        return
    elif t.isActor():
        yield t
    elif t.hasBaseType():
        for actor in iteractortypes(t.basetype, visited):
            yield actor
    elif t.isCompound() and t not in visited:
        visited.add(t)
        for c in t.itercomponents():
            for actor in iteractortypes(c, visited):
                yield actor


def hasTaintedBaseType(t):
    """Return true iff |t|, or the type wrapped by a nullable/maybe/array/
    uniqueptr modifier, is a [Tainted] imported C++ type. Does not look inside
    structs or unions."""
    while t.isIPDL() and t.hasBaseType():
        t = t.basetype
    return t.isTainted()


def hasshmem(type):
    """Return true iff |type| is shmem or has it buried within."""

    class found(BaseException):
        pass

    class findShmem(TypeVisitor):
        def visitShmemType(self, s):
            raise found()

    try:
        type.accept(findShmem())
    except found:
        return True
    return False


# --------------------
_builtinloc = Loc("<builtin>", 0)


def makeBuiltinUsing(tname):
    quals = tname.split("::")
    base = quals.pop()
    quals = quals[0:]
    return UsingStmt(_builtinloc, QualifiedId(_builtinloc, base, quals))


builtinUsing = [makeBuiltinUsing(t) for t in builtin.Types]
builtinHeaderIncludes = [CxxInclude(_builtinloc, f) for f in builtin.HeaderIncludes]


def errormsg(loc, fmt, *args):
    while not isinstance(loc, Loc):
        if loc is None:
            loc = Loc.NONE
        else:
            loc = loc.loc
    return "%s: error: %s" % (str(loc), fmt % args)


# --------------------


class SymbolTable:
    def __init__(self, errors):
        self.errors = errors
        self.scopes = [{}]  # stack({})
        self.currentScope = self.scopes[0]

    def enterScope(self):
        assert isinstance(self.scopes[0], dict)
        assert isinstance(self.currentScope, dict)

        self.scopes.append({})
        self.currentScope = self.scopes[-1]

    def exitScope(self):
        symtab = self.scopes.pop()
        assert self.currentScope is symtab

        self.currentScope = self.scopes[-1]

        assert isinstance(self.scopes[0], dict)
        assert isinstance(self.currentScope, dict)

    def lookup(self, sym):
        # NB: since IPDL doesn't allow any aliased names of different types,
        # it doesn't matter in which order we walk the scope chain to resolve
        # |sym|
        for scope in self.scopes:
            decl = scope.get(sym, None)
            if decl is not None:
                return decl
        return None

    def declare(self, decl):
        assert decl.progname or decl.shortname or decl.fullname
        assert decl.loc
        assert decl.type

        def tryadd(name):
            olddecl = self.lookup(name)
            if olddecl is not None:
                self.errors.append(
                    errormsg(
                        decl.loc,
                        "redeclaration of symbol `%s', first declared at %s",
                        name,
                        olddecl.loc,
                    )
                )
                return
            self.currentScope[name] = decl
            decl.scope = self.currentScope

        if decl.progname:
            tryadd(decl.progname)
        if decl.shortname:
            tryadd(decl.shortname)
        if decl.fullname:
            tryadd(decl.fullname)


class TypeCheck:
    """This pass sets the .decl attribute of AST nodes for which that is relevant;
    a decl says where, with what type, and under what name(s) a node was
    declared.

    With this information, it type checks the AST."""

    def __init__(self):
        # NB: no IPDL compile will EVER print a warning.  A program has
        # one of two attributes: it is either well typed, or not well typed.
        self.errors = []  # [ string ]

    def check(self, tu, errout=sys.stderr):
        def runpass(tcheckpass):
            tu.accept(tcheckpass)
            if len(self.errors):
                self.reportErrors(errout)
                return False
            return True

        # tag each relevant node with "decl" information, giving type, name,
        # and location of declaration
        if not runpass(GatherDecls(builtinUsing, self.errors)):
            return False

        # now that the nodes have decls, type checking is much easier.
        if not runpass(CheckTypes(self.errors)):
            return False

        return True

    def reportErrors(self, errout):
        for error in self.errors:
            print(error, file=errout)


class TcheckVisitor(Visitor):
    def __init__(self, errors):
        self.errors = errors

    def error(self, loc, fmt, *args):
        self.errors.append(errormsg(loc, fmt, *args))


class GatherDecls(TcheckVisitor):
    def __init__(self, builtinUsing, errors):
        TcheckVisitor.__init__(self, errors)

        # |self.symtab| is the symbol table for the translation unit
        # currently being visited
        self.symtab = None
        self.builtinUsing = builtinUsing

    def declare(
        self, loc, type, shortname=None, fullname=None, progname=None, attributes={}
    ):
        d = Decl(loc)
        d.type = type
        d.progname = progname
        d.shortname = shortname
        d.fullname = fullname
        d.attributes = attributes
        self.symtab.declare(d)
        return d

    # Check that only attributes allowed by an attribute spec are present
    # within the given attribute dictionary. The spec value may be either
    # `None`, for a valueless attribute, a list of valid attribute values, or a
    # callable which returns a truthy value if the attribute is valid.
    def checkAttributes(self, attributes, spec):
        for attr in attributes.values():
            if attr.name not in spec:
                self.error(attr.loc, "unknown attribute `%s'", attr.name)
                continue

            aspec = spec[attr.name]
            if aspec is None:
                if attr.value is not None:
                    self.error(
                        attr.loc,
                        "unexpected value for valueless attribute `%s'",
                        attr.name,
                    )
            elif isinstance(aspec, (list, tuple)):
                if not any(
                    (
                        isinstance(attr.value, s)
                        if isinstance(s, type)
                        else attr.value == s
                    )
                    for s in aspec
                ):
                    self.error(
                        attr.loc,
                        "invalid value for attribute `%s', expected one of: %s",
                        attr.name,
                        ", ".join(
                            s.__name__ if isinstance(s, type) else str(s) for s in aspec
                        ),
                    )
            elif callable(aspec):
                if not aspec(attr.value):
                    self.error(attr.loc, "invalid value for attribute `%s'", attr.name)
            else:
                raise Exception("INTERNAL ERROR: Invalid attribute spec")

    def visitTranslationUnit(self, tu):
        # all TranslationUnits declare symbols in global scope
        if hasattr(tu, "visited"):
            return
        tu.visited = True
        savedSymtab = self.symtab
        self.symtab = SymbolTable(self.errors)

        # pretend like the translation unit "using"-ed these for the
        # sake of type checking and C++ code generation
        tu.builtinUsing = self.builtinUsing

        # for everyone's sanity, enforce that the filename and tu name
        # match
        basefilename = os.path.basename(tu.filename)
        expectedfilename = "%s.ipdl" % (tu.name)
        if not tu.protocol:
            # header
            expectedfilename += "h"
        if basefilename != expectedfilename:
            self.error(
                tu.loc,
                "expected file for translation unit `%s' to be named `%s'; instead it's named `%s'",  # NOQA: E501
                tu.name,
                expectedfilename,
                basefilename,
            )

        if tu.protocol:
            assert tu.name == tu.protocol.name

            p = tu.protocol

            proc_options = [
                "any",
                "anychild",  # non-Parent process
                "anydom",  # Parent or Content process
                "compositor",  # Parent or GPU process
            ] + [p.proc_typename for p in geckoprocesstypes.process_types]

            self.checkAttributes(
                p.attributes,
                {
                    "ManualDealloc": None,
                    "NestedUpTo": ("not", "inside_sync", "inside_cpow"),
                    "NeedsOtherPid": None,
                    "ChildImpl": ("virtual", StringLiteral),
                    "ParentImpl": ("virtual", StringLiteral),
                    "ChildProc": proc_options,
                    "ParentProc": proc_options,
                },
            )

            if not p.name.startswith("P"):
                self.error(
                    p.loc,
                    "invalid protocol name `%s': name must begin with `P'",
                    p.name,
                )

            # FIXME/cjones: it's a little weird and counterintuitive
            # to put both the namespace and non-namespaced name in the
            # global scope.  try to figure out something better; maybe
            # a type-neutral |using| that works for C++ and protocol
            # types?
            qname = p.qname()
            fullname = str(qname)
            p.decl = self.declare(
                loc=p.loc,
                type=ProtocolType(
                    qname,
                    p.nestedUpTo(),
                    p.sendSemantics,
                    "ManualDealloc" not in p.attributes,
                    "NeedsOtherPid" in p.attributes,
                    p.procAttribute("parent"),
                    p.procAttribute("child"),
                ),
                shortname=p.name,
                fullname=fullname,
            )

            p.parentEndpointDecl = self.declare(
                loc=p.loc,
                type=EndpointType(
                    QualifiedId(
                        p.loc, "Endpoint<" + fullname + "Parent>", ["mozilla", "ipc"]
                    ),
                    ActorType(p.decl.type),
                ),
                shortname="Endpoint<" + p.name + "Parent>",
            )
            p.childEndpointDecl = self.declare(
                loc=p.loc,
                type=EndpointType(
                    QualifiedId(
                        p.loc, "Endpoint<" + fullname + "Child>", ["mozilla", "ipc"]
                    ),
                    ActorType(p.decl.type),
                ),
                shortname="Endpoint<" + p.name + "Child>",
            )

            p.parentManagedEndpointDecl = self.declare(
                loc=p.loc,
                type=ManagedEndpointType(
                    QualifiedId(
                        p.loc,
                        "ManagedEndpoint<" + fullname + "Parent>",
                        ["mozilla", "ipc"],
                    ),
                    ActorType(p.decl.type),
                ),
                shortname="ManagedEndpoint<" + p.name + "Parent>",
            )
            p.childManagedEndpointDecl = self.declare(
                loc=p.loc,
                type=ManagedEndpointType(
                    QualifiedId(
                        p.loc,
                        "ManagedEndpoint<" + fullname + "Child>",
                        ["mozilla", "ipc"],
                    ),
                    ActorType(p.decl.type),
                ),
                shortname="ManagedEndpoint<" + p.name + "Child>",
            )

            # XXX ugh, this sucks.  but we need this information to compute
            # what friend decls we need in generated C++
            p.decl.type._ast = p

        # make sure we have decls for all dependent protocols
        for pinc in tu.includes:
            pinc.accept(self)

        # declare imported (and builtin) C and C++ types
        for ctype in builtin.CTypes:
            self.declare(
                loc=_builtinloc,
                type=BuiltinCType(ctype),
                shortname=ctype,
            )
        for using in tu.builtinUsing:
            using.accept(self)
        for using in tu.using:
            using.accept(self)

        # first pass to "forward-declare" all structs and unions in
        # order to support recursive definitions
        for su in tu.structsAndUnions:
            self.declareStructOrUnion(su)

        # second pass to check each definition
        for su in tu.structsAndUnions:
            su.accept(self)

        if tu.protocol:
            # grab symbols in the protocol itself
            p.accept(self)

        self.symtab = savedSymtab

    def declareStructOrUnion(self, su):
        if hasattr(su, "decl"):
            self.symtab.declare(su.decl)
            return

        qname = su.qname()
        fullname = str(qname)

        if isinstance(su, StructDecl):
            sutype = StructType(qname, [])
        elif isinstance(su, UnionDecl):
            sutype = UnionType(qname, [])
        else:
            assert 0 and "unknown type"

        # XXX more suckage.  this time for pickling structs/unions
        # declared in headers.
        sutype._ast = su

        su.decl = self.declare(
            loc=su.loc, type=sutype, shortname=su.name, fullname=fullname
        )

    def visitInclude(self, inc):
        if inc.tu is None:
            self.error(
                inc.loc,
                "(type checking here will be unreliable because of an earlier error)",
            )
            return
        inc.tu.accept(self)
        if inc.tu.protocol:
            self.symtab.declare(inc.tu.protocol.decl)
            self.symtab.declare(inc.tu.protocol.parentEndpointDecl)
            self.symtab.declare(inc.tu.protocol.childEndpointDecl)
            self.symtab.declare(inc.tu.protocol.parentManagedEndpointDecl)
            self.symtab.declare(inc.tu.protocol.childManagedEndpointDecl)
        else:
            # This is a header.  Import its "exported" globals into
            # our scope.
            for using in inc.tu.using:
                using.accept(self)
            for su in inc.tu.structsAndUnions:
                self.declareStructOrUnion(su)

    def visitStructDecl(self, sd):
        # If we've already processed this struct, don't do it again.
        if hasattr(sd, "visited"):
            return

        stype = sd.decl.type

        self.symtab.enterScope()
        sd.visited = True

        self.checkAttributes(sd.attributes, {"Comparable": None})

        for f in sd.fields:
            ftypedecl = self.symtab.lookup(f.typespec.basename())
            if ftypedecl is None:
                self.error(
                    f.loc,
                    "field `%s' of struct `%s' has unknown type `%s'",
                    f.name,
                    sd.name,
                    f.typespec.basename(),
                )
                continue

            f.decl = self.declare(
                loc=f.loc,
                type=self._canonicalType(ftypedecl.type, f.typespec),
                shortname=f.name,
                fullname=None,
            )
            stype.fields.append(f.decl.type)

        self.symtab.exitScope()

    def visitUnionDecl(self, ud):
        utype = ud.decl.type

        # If we've already processed this union, don't do it again.
        if len(utype.components):
            return

        self.checkAttributes(ud.attributes, {"Comparable": None})

        for c in ud.components:
            cdecl = self.symtab.lookup(c.basename())
            if cdecl is None:
                self.error(
                    c.loc,
                    "unknown component type `%s' of union `%s'",
                    c.basename(),
                    ud.name,
                )
                continue
            utype.components.append(self._canonicalType(cdecl.type, c))

    def visitUsingStmt(self, using):
        fullname = str(using.type)

        self.checkAttributes(
            using.attributes,
            {
                "MoveOnly": (None, "data", "send"),
                "RefCounted": None,
                "Tainted": None,
            },
        )

        if fullname == "::mozilla::ipc::Shmem":
            ipdltype = ShmemType(using.type)
        elif fullname == "::mozilla::ipc::ByteBuf":
            ipdltype = ByteBufType(using.type)
        elif fullname == "::mozilla::ipc::FileDescriptor":
            ipdltype = FDType(using.type)
        else:
            ipdltype = ImportedCxxType(
                using.type,
                using.isRefcounted(),
                using.isSendMoveOnly(),
                using.isDataMoveOnly(),
                using.isTainted(),
            )
            existingType = self.symtab.lookup(ipdltype.fullname())
            if existingType and existingType.fullname == ipdltype.fullname():
                if ipdltype.isRefcounted() != existingType.type.isRefcounted():
                    self.error(
                        using.loc,
                        "inconsistent refcounted status of type `%s`",
                        str(using.type),
                    )
                if (
                    ipdltype.isSendMoveOnly() != existingType.type.isSendMoveOnly()
                    or ipdltype.isDataMoveOnly() != existingType.type.isDataMoveOnly()
                ):
                    self.error(
                        using.loc,
                        "inconsistent moveonly status of type `%s`",
                        str(using.type),
                    )
                if ipdltype.isTainted() != existingType.type.isTainted():
                    self.error(
                        using.loc,
                        "inconsistent tainted status of type `%s`",
                        str(using.type),
                    )
                using.decl = existingType
                return
        using.decl = self.declare(
            loc=using.loc,
            type=ipdltype,
            shortname=using.type.baseid,
            fullname=fullname,
        )

    def visitProtocol(self, p):
        # protocol scope
        self.symtab.enterScope()

        seenmgrs = set()
        for mgr in p.managers:
            if mgr.name in seenmgrs:
                self.error(mgr.loc, "manager `%s' appears multiple times", mgr.name)
                continue

            seenmgrs.add(mgr.name)
            mgr.of = p
            mgr.accept(self)

        for managed in p.managesStmts:
            managed.manager = p
            managed.accept(self)

        setattr(self, "currentProtocolDecl", p.decl)
        for msg in p.messageDecls:
            msg.accept(self)
        del self.currentProtocolDecl

        p.decl.type.hasDelete = not not self.symtab.lookup(_DELETE_MSG)
        if not (p.decl.type.hasDelete or p.decl.type.isToplevel()):
            self.error(
                p.loc,
                "destructor declaration `%s(...)' required for managed protocol `%s'",
                _DELETE_MSG,
                p.name,
            )

        if not p.decl.type.isToplevel() and p.decl.type.needsotherpid:
            self.error(p.loc, "[NeedsOtherPid] only applies to toplevel protocols")

        if p.decl.type.isToplevel():
            if not p.decl.type.isRefcounted():
                self.error(p.loc, "Toplevel protocols cannot be [ManualDealloc]")

            if "ChildProc" not in p.attributes:
                self.error(p.loc, "Toplevel protocols must specify [ChildProc]")

        if p.decl.type.isManager() and not p.decl.type.isRefcounted():
            self.error(p.loc, "[ManualDealloc] protocols cannot be managers")

        # FIXME/cjones declare all the little C++ thingies that will
        # be generated.  they're not relevant to IPDL itself, but
        # those ("invisible") symbols can clash with others in the
        # IPDL spec, and we'd like to catch those before C++ compilers
        # are allowed to obfuscate the error

        self.symtab.exitScope()

    def visitManager(self, mgr):
        mgrdecl = self.symtab.lookup(mgr.name)
        pdecl = mgr.of.decl
        assert pdecl

        pname, mgrname = pdecl.shortname, mgr.name
        loc = mgr.loc

        if mgrdecl is None:
            self.error(
                loc,
                "protocol `%s' referenced as |manager| of `%s' has not been declared",
                mgrname,
                pname,
            )
        elif not isinstance(mgrdecl.type, ProtocolType):
            self.error(
                loc,
                "entity `%s' referenced as |manager| of `%s' is not of `protocol' type; instead it is of type `%s'",  # NOQA: E501
                mgrname,
                pname,
                mgrdecl.type.typename(),
            )
        else:
            mgr.decl = mgrdecl
            pdecl.type.addManager(mgrdecl.type)

    def visitManagesStmt(self, mgs):
        mgsdecl = self.symtab.lookup(mgs.name)
        pdecl = mgs.manager.decl
        assert pdecl

        pname, mgsname = pdecl.shortname, mgs.name
        loc = mgs.loc

        if mgsdecl is None:
            self.error(
                loc,
                "protocol `%s', managed by `%s', has not been declared",
                mgsname,
                pname,
            )
        elif not isinstance(mgsdecl.type, ProtocolType):
            self.error(
                loc,
                "%s declares itself managing a non-`protocol' entity `%s' of type `%s'",
                pname,
                mgsname,
                mgsdecl.type.typename(),
            )
        else:
            mgs.decl = mgsdecl
            pdecl.type.manages.append(mgsdecl.type)

    def visitMessageDecl(self, md):
        msgname = md.name
        loc = md.loc

        self.checkAttributes(
            md.attributes,
            {
                "Tainted": None,
                "Compress": (None, "all"),
                "Priority": priorityList,
                "ReplyPriority": priorityList,
                "Nested": ("not", "inside_sync", "inside_cpow"),
                "VirtualSendImpl": None,
                "LazySend": None,
            },
        )

        if md.sendSemantics is not ASYNC and "LazySend" in md.attributes:
            self.error(loc, "non-async message `%s' cannot specify [LazySend]", msgname)

        if md.sendSemantics is not ASYNC and "ReplyPriority" in md.attributes:
            self.error(
                loc, "non-async message `%s' cannot specify [ReplyPriority]", msgname
            )

        if not md.outParams and "ReplyPriority" in md.attributes:
            self.error(
                loc, "non-returns message `%s' cannot specify [ReplyPriority]", msgname
            )

        isctor = False
        isdtor = False
        cdtype = None

        decl = self.symtab.lookup(msgname)
        if decl is not None and decl.type.isProtocol():
            # probably a ctor.  we'll check validity later.
            msgname += "Constructor"
            isctor = True
            cdtype = decl.type
        elif decl is not None:
            self.error(
                loc,
                "message name `%s' already declared as `%s'",
                msgname,
                decl.type.typename(),
            )
            # if we error here, no big deal; move on to find more

        if _DELETE_MSG == msgname:
            if md.sendSemantics is not ASYNC:
                self.error(loc, "destructor must be async")
            if md.outParams:
                self.error(loc, "destructors cannot return values")
            isdtor = True
            cdtype = self.currentProtocolDecl.type

        # enter message scope
        self.symtab.enterScope()

        msgtype = MessageType(
            nested=md.nested(),
            prio=md.priority(),
            replyPrio=md.replyPriority(),
            sendSemantics=md.sendSemantics,
            direction=md.direction,
            ctor=isctor,
            dtor=isdtor,
            cdtype=cdtype,
            compress=md.attributes.get("Compress"),
            tainted="Tainted" in md.attributes,
            lazySend="LazySend" in md.attributes,
        )

        # replace inparam Param nodes with proper Decls
        def paramToDecl(param):
            self.checkAttributes(
                param.attributes,
                {
                    # Passback indicates that the argument is unused by the Parent and is
                    #    merely returned to the Child later.
                    # AllValid indicates that the entire span of values representable by
                    #    the type are acceptable.  e.g. 0-255 in a uint8
                    "NoTaint": ("passback", "allvalid")
                },
            )

            ptname = param.typespec.basename()
            ploc = param.typespec.loc

            ptdecl = self.symtab.lookup(ptname)
            if ptdecl is None:
                self.error(
                    ploc,
                    "argument typename `%s' of message `%s' has not been declared",
                    ptname,
                    msgname,
                )
                ptype = VOID
            else:
                ptype = self._canonicalType(ptdecl.type, param.typespec)

            if "NoTaint" in param.attributes and not (
                "Tainted" in md.attributes or hasTaintedBaseType(ptype)
            ):
                self.error(
                    ploc,
                    "argument typename `%s' of message `%s' has a NoTaint attribute, but neither the message nor the type is Tainted",
                    ptname,
                    msgname,
                )

            return self.declare(
                loc=ploc, type=ptype, progname=param.name, attributes=param.attributes
            )

        for i, inparam in enumerate(md.inParams):
            pdecl = paramToDecl(inparam)
            msgtype.params.append(pdecl.type)
            md.inParams[i] = pdecl
        for i, outparam in enumerate(md.outParams):
            pdecl = paramToDecl(outparam)
            msgtype.returns.append(pdecl.type)
            md.outParams[i] = pdecl

        self.symtab.exitScope()

        md.decl = self.declare(loc=loc, type=msgtype, progname=msgname)
        md.protocolDecl = self.currentProtocolDecl
        md.decl._md = md

    def _canonicalType(self, itype, typespec):
        if itype.isIPDL() and itype.isProtocol():
            itype = ActorType(itype)

        if itype.supportsNullable():
            if not typespec.nullable:
                itype = NotNullType(itype)
        elif typespec.nullable:
            self.error(
                typespec.loc,
                "`nullable' qualifier for type `%s' is unsupported",
                itype.name(),
            )

        for modifier in typespec.modifiers:
            if modifier == "uniqueptr":
                itype = UniquePtrType(itype)
            elif modifier == "array":
                itype = ArrayType(itype)
            else:
                assert modifier == "maybe"
                itype = MaybeType(itype)

        return itype


# -----------------------------------------------------------------------------


def checkcycles(p, stack=None):
    cycles = []

    if stack is None:
        stack = []

    for cp in p.manages:
        # special case for self-managed protocols
        if cp is p:
            continue

        if cp in stack:
            return [stack + [p, cp]]
        cycles += checkcycles(cp, stack + [p])

    return cycles


def formatcycles(cycles):
    r = []
    for cycle in cycles:
        s = " -> ".join([ptype.name() for ptype in cycle])
        r.append("`%s'" % s)
    return ", ".join(r)


def fullyDefined(t, exploring=None):
    """The rules for "full definition" of a type are
    defined(atom)             := true
    defined(array basetype)   := defined(basetype)
    defined(struct f1 f2...)  := defined(f1) and defined(f2) and ...
    defined(union c1 c2 ...)  := defined(c1) or defined(c2) or ..."""
    if exploring is None:
        exploring = set()

    if t.isAtom():
        return True
    elif t.hasBaseType():
        return fullyDefined(t.basetype, exploring)
    elif t.defined:
        return True
    assert t.isCompound()

    if t in exploring:
        return False

    exploring.add(t)
    for c in t.itercomponents():
        cdefined = fullyDefined(c, exploring)
        if t.isStruct() and not cdefined:
            t.defined = False
            break
        elif t.isUnion() and cdefined:
            t.defined = True
            break
    else:
        if t.isStruct():
            t.defined = True
        elif t.isUnion():
            t.defined = False
    exploring.remove(t)

    return t.defined


class CheckTypes(TcheckVisitor):
    def __init__(self, errors):
        TcheckVisitor.__init__(self, errors)
        self.visited = set()
        self.ptype = None

    def visitInclude(self, inc):
        if inc.tu.filename in self.visited:
            return
        self.visited.add(inc.tu.filename)
        if inc.tu.protocol:
            inc.tu.protocol.accept(self)

    def visitStructDecl(self, sd):
        if not fullyDefined(sd.decl.type):
            self.error(sd.decl.loc, "struct `%s' is only partially defined", sd.name)

    def visitUnionDecl(self, ud):
        if not fullyDefined(ud.decl.type):
            self.error(ud.decl.loc, "union `%s' is only partially defined", ud.name)

    def visitProtocol(self, p):
        self.ptype = p.decl.type

        # check that we require no more "power" than our manager protocols
        ptype, pname = p.decl.type, p.decl.shortname

        for mgrtype in ptype.managers:
            if mgrtype is not None and not ptype.sendSemanticsSatisfiedBy(mgrtype):
                self.error(
                    p.decl.loc,
                    "protocol `%s' requires more powerful send semantics than its manager `%s' provides",  # NOQA: E501
                    pname,
                    mgrtype.name(),
                )

        if ptype.isToplevel():
            cycles = checkcycles(p.decl.type)
            if cycles:
                self.error(
                    p.decl.loc,
                    "cycle(s) detected in manager/manages hierarchy: %s",
                    formatcycles(cycles),
                )

        if 1 == len(ptype.managers) and ptype is ptype.manager():
            self.error(
                p.decl.loc, "top-level protocol `%s' cannot manage itself", p.name
            )

        return Visitor.visitProtocol(self, p)

    def visitManagesStmt(self, mgs):
        pdecl = mgs.manager.decl
        ptype, pname = pdecl.type, pdecl.shortname

        mgsdecl = mgs.decl
        mgstype, mgsname = mgsdecl.type, mgsdecl.shortname

        loc = mgs.loc

        # we added this information; sanity check it
        assert ptype.isManagerOf(mgstype)

        # check that the "managed" protocol agrees
        if not mgstype.isManagedBy(ptype):
            self.error(
                loc,
                "|manages| declaration in protocol `%s' does not match any |manager| declaration in protocol `%s'",  # NOQA: E501
                pname,
                mgsname,
            )

    def visitManager(self, mgr):
        pdecl = mgr.of.decl
        ptype, pname = pdecl.type, pdecl.shortname

        mgrdecl = mgr.decl
        mgrtype, mgrname = mgrdecl.type, mgrdecl.shortname

        # we added this information; sanity check it
        assert ptype.isManagedBy(mgrtype)

        loc = mgr.loc

        # check that the "manager" protocol agrees
        if not mgrtype.isManagerOf(ptype):
            self.error(
                loc,
                "|manager| declaration in protocol `%s' does not match any |manages| declaration in protocol `%s'",  # NOQA: E501
                pname,
                mgrname,
            )

    def visitMessageDecl(self, md):
        mtype, mname = md.decl.type, md.decl.progname
        ptype, pname = md.protocolDecl.type, md.protocolDecl.shortname

        loc = md.decl.loc

        if mtype.nested == INSIDE_SYNC_NESTED and not mtype.isSync():
            self.error(
                loc,
                "inside_sync nested messages must be sync (here, message `%s' in protocol `%s')",
                mname,
                pname,
            )

        if mtype.nested == INSIDE_CPOW_NESTED and (mtype.isOut() or mtype.isInout()):
            self.error(
                loc,
                "inside_cpow nested parent-to-child messages are verboten (here, message `%s' in protocol `%s')",  # NOQA: E501
                mname,
                pname,
            )

        # We allow inside_sync messages that are themselves sync to be sent from the
        # parent. Normal and inside_cpow nested messages that are sync can only come from
        # the child.
        if (
            mtype.isSync()
            and mtype.nested == NOT_NESTED
            and (mtype.isOut() or mtype.isInout())
        ):
            self.error(
                loc,
                "sync parent-to-child messages are verboten (here, message `%s' in protocol `%s')",
                mname,
                pname,
            )

        if not mtype.sendSemanticsSatisfiedBy(ptype):
            self.error(
                loc,
                "message `%s' requires more powerful send semantics than its protocol `%s' provides",  # NOQA: E501
                mname,
                pname,
            )

        if (mtype.isCtor() or mtype.isDtor()) and mtype.isAsync() and mtype.returns:
            self.error(
                loc, "asynchronous ctor/dtor message `%s' declares return values", mname
            )

        if mtype.compress and (not mtype.isAsync() or mtype.isCtor() or mtype.isDtor()):
            if mtype.isCtor() or mtype.isDtor():
                message_type = "constructor" if mtype.isCtor() else "destructor"
                error_message = (
                    "%s messages can't use compression (here, in protocol `%s')"
                    % (message_type, pname)
                )
            else:
                error_message = (
                    "message `%s' in protocol `%s' requests compression but is not async"
                    % (mname, pname)  # NOQA: E501
                )

            self.error(loc, error_message)

        if mtype.isCtor() and not ptype.isManagerOf(mtype.constructedType()):
            self.error(
                loc,
                "ctor for protocol `%s', which is not managed by protocol `%s'",
                mname[: -len("constructor")],
                pname,
            )
