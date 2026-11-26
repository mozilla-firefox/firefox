/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/SymbolType.h"

#include "gc/HashUtil.h"
#include "js/Printer.h"  // js::GenericPrinter, js::Fprinter
#include "util/StringBuilder.h"
#include "vm/JSContext.h"
#include "vm/JSONPrinter.h"  // js::JSONPrinter
#include "vm/Realm.h"

#include "vm/Realm-inl.h"

using JS::Symbol;
using namespace js;

bool Symbol::isLocal() const {
  bool local = !zoneFromAnyThread()->isAtomsZone();
  MOZ_ASSERT_IF(local, isUnique());
  return local;
}

bool Symbol::isShared() const { return !isLocal(); }

JSAtom* Symbol::description() const {
  const Symbol* symbol = isLocal() ? sharedSymbol() : this;
  TenuredCell* cell = symbol->headerPtr();
  if (!cell) {
    return nullptr;
  }

  return &cell->as<JSString>()->asAtom();
}

Symbol* Symbol::sharedSymbol() const {
  MOZ_ASSERT(isLocal());
  return headerPtr()->as<JS::Symbol>();
}

Symbol* Symbol::newSharedSymbol(JSContext* cx, JS::SymbolCode code,
                                uint32_t hash, Handle<JSAtom*> description) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(cx->runtime()));
  AutoAllocInAtomsZone az(cx);
  return cx->newCell<Symbol>(code, hash, description);
}

Symbol* Symbol::newLocalSymbol(JSContext* cx, JS::SymbolCode code,
                               uint32_t hash, Handle<JS::Symbol*> shared) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(cx->runtime()));
  MOZ_ASSERT(code == JS::SymbolCode::UniqueSymbol);
  MOZ_ASSERT_IF(shared, shared->isShared());
  return cx->newCell<Symbol>(code, hash, shared);
}

Symbol* Symbol::new_(JSContext* cx, JS::SymbolCode code,
                     HandleString description) {
  MOZ_ASSERT(code == JS::SymbolCode::UniqueSymbol ||
             code == JS::SymbolCode::PrivateNameSymbol);

  Rooted<JSAtom*> atom(cx);
  if (description) {
    atom = AtomizeString(cx, description);
    if (!atom) {
      return nullptr;
    }
  }

  Rooted<Symbol*> sym(
      cx, newSharedSymbol(cx, code, cx->runtime()->randomHashCode(), atom));
  if (!sym) {
    return nullptr;
  }

  if (code == JS::SymbolCode::PrivateNameSymbol) {
    cx->atomReferences().recordRef(cx, sym.get());
  } else {
    MOZ_ASSERT(code == JS::SymbolCode::UniqueSymbol);
    sym.set(wrap(cx, sym));
  }

  return sym;
}

/* static */
Symbol* Symbol::wrap(JSContext* cx, Handle<JS::Symbol*> shared) {
  MOZ_ASSERT(shared->isShared());
  MOZ_ASSERT(shared->isUnique());
  MOZ_ASSERT(!cx->zone()->isAtomsZone());

  auto& symbols = cx->zone()->localSymbolMap();
  DependentAddPtr<gc::LocalSymbolMap> ptr(cx, symbols, shared);
  if (ptr) {
    Symbol* local = ptr->value();
    MOZ_ASSERT(local->isLocal());
    gc::ReadBarrier(local);
    return local;
  }

  Symbol* local = newLocalSymbol(cx, shared->code(), shared->hash(), shared);
  if (!local) {
    return nullptr;
  }

  MOZ_ASSERT(local->isLocal());
  if (!ptr.add(cx, symbols, shared, local)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  return local;
}

Symbol* Symbol::newWellKnown(JSContext* cx, JS::SymbolCode code,
                             Handle<PropertyName*> description) {
  MOZ_ASSERT(code < JS::SymbolCode::WellKnownAPILimit);
  return newSharedSymbol(cx, code, cx->runtime()->randomHashCode(),
                         description);
}

Symbol* Symbol::for_(JSContext* cx, HandleString description) {
  Rooted<JSAtom*> atom(cx, AtomizeString(cx, description));
  if (!atom) {
    return nullptr;
  }

  cx->recordRef(atom);

  SymbolRegistry& registry = cx->symbolRegistry();
  Rooted<Symbol*> sym(cx);
  DependentAddPtr<SymbolRegistry> p(cx, registry, atom);
  if (p) {
    sym = *p;
    if (!cx->wrapOrRecordRef(&sym)) {
      return nullptr;
    }
    return sym;
  }

  // Rehash the hash of the atom to give the corresponding symbol a hash
  // that is different than the hash of the corresponding atom.
  HashNumber hash = mozilla::HashGeneric(atom->hash());
  sym = newSharedSymbol(cx, SymbolCode::InSymbolRegistry, hash, atom);
  if (!sym) {
    return nullptr;
  }

  if (!p.add(cx, registry, atom, sym) || !cx->wrapOrRecordRef(&sym)) {
    return nullptr;
  }

  return sym;
}

#if defined(DEBUG) || defined(JS_JITSPEW)
void Symbol::dump() const {
  js::Fprinter out(stderr);
  dump(out);
}

void Symbol::dump(js::GenericPrinter& out) const {
  js::JSONPrinter json(out);
  dump(json);
  out.put("\n");
}

void Symbol::dump(js::JSONPrinter& json) const {
  json.beginObject();
  dumpFields(json);
  json.endObject();
}

template <typename KnownF, typename UnknownF>
void SymbolCodeToString(JS::SymbolCode code, KnownF known, UnknownF unknown) {
  switch (code) {
#  define DEFINE_CASE(name)    \
    case JS::SymbolCode::name: \
      known(#name);            \
      break;
    JS_FOR_EACH_WELL_KNOWN_SYMBOL(DEFINE_CASE)
#  undef DEFINE_CASE

    case JS::SymbolCode::Limit:
      known("Limit");
      break;
    case JS::SymbolCode::WellKnownAPILimit:
      known("WellKnownAPILimit");
      break;
    case JS::SymbolCode::PrivateNameSymbol:
      known("PrivateNameSymbol");
      break;
    case JS::SymbolCode::InSymbolRegistry:
      known("InSymbolRegistry");
      break;
    case JS::SymbolCode::UniqueSymbol:
      known("UniqueSymbol");
      break;
    default:
      unknown(uint32_t(code));
      break;
  }
}

void Symbol::dumpFields(js::JSONPrinter& json) const {
  json.formatProperty("address", "(JS::Symbol*)0x%p", this);

  SymbolCodeToString(
      code_, [&](const char* name) { json.property("code", name); },
      [&](uint32_t code) {
        json.formatProperty("code", "Unknown(%08x)", code);
      });

  json.formatProperty("hash", "0x%08x", hash());

  if (description()) {
    js::GenericPrinter& out = json.beginStringProperty("description");
    description()->dumpCharsNoQuote(out);
    json.endStringProperty();
  } else {
    json.nullProperty("description");
  }
}

void Symbol::dumpStringContent(js::GenericPrinter& out) const {
  dumpPropertyName(out);

  if (!isWellKnownSymbol()) {
    out.printf(" @ (JS::Symbol*)0x%p", this);
  }
}

void Symbol::dumpPropertyName(js::GenericPrinter& out) const {
  if (isWellKnownSymbol()) {
    // All the well-known symbol names are ASCII.
    description()->dumpCharsNoQuote(out);
  } else if (code_ == SymbolCode::InSymbolRegistry ||
             code_ == SymbolCode::UniqueSymbol) {
    out.printf(code_ == SymbolCode::InSymbolRegistry ? "Symbol.for("
                                                     : "Symbol(");

    if (description()) {
      description()->dumpCharsSingleQuote(out);
    } else {
      out.printf("undefined");
    }

    out.putChar(')');
  } else if (code_ == SymbolCode::PrivateNameSymbol) {
    MOZ_ASSERT(description());
    out.putChar('#');
    description()->dumpCharsNoQuote(out);
  } else {
    out.printf("<Invalid Symbol code=%u>", unsigned(code_));
  }
}
#endif  // defined(DEBUG) || defined(JS_JITSPEW)

JS::Symbol* js::UnwrapSymbol(JS::Symbol* symbol) {
  if (!symbol->isLocal()) {
    return symbol;
  }

  return symbol->sharedSymbol();
}

bool js::SymbolDescriptiveString(JSContext* cx, Symbol* sym,
                                 MutableHandleValue result) {
  // steps 2-5
  JSStringBuilder sb(cx);
  if (!sb.append("Symbol(")) {
    return false;
  }
  if (JSAtom* desc = sym->description()) {
    if (!sb.append(desc)) {
      return false;
    }
  }
  if (!sb.append(')')) {
    return false;
  }

  // step 6
  JSString* str = sb.finishString();
  if (!str) {
    return false;
  }
  result.setString(str);
  return true;
}

JS::ubi::Node::Size JS::ubi::Concrete<JS::Symbol>::size(
    mozilla::MallocSizeOf mallocSizeOf) const {
  // If we start allocating symbols in the nursery, we will need to update
  // this method.
  MOZ_ASSERT(get().isTenured());
  return js::gc::Arena::thingSize(get().asTenured().getAllocKind());
}
