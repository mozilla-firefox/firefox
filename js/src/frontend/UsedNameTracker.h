/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_UsedNameTracker_h
#define frontend_UsedNameTracker_h

#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"

#include <stdint.h>

#include "frontend/ParserAtom.h"                   // TaggedParserAtomIndex
#include "frontend/TaggedParserAtomIndexHasher.h"  // TaggedParserAtomIndexHasher
#include "frontend/Token.h"
#include "js/AllocPolicy.h"
#include "js/HashTable.h"
#include "js/Vector.h"

namespace js {
namespace frontend {

// A data structure for tracking used names per parsing session in order to
// compute which bindings are closed over. Scripts and scopes are numbered
// monotonically in textual order and unresolved uses of a name are tracked by
// lists of identifier uses, which are a pair of (ScriptId,ScopeId).
//
// For an identifier `i` with a use (ScriptId,ScopeId) in the Used list,
// ScriptId tracks the most nested script that has a use of u, and ScopeId
// tracks the most nested scope that is still being parsed (as the lists will be
// filtered as we finish processing a particular scope).
//
// ScriptId is used to answer the question "is `i` used by a nested function?"
// ScopeId is used to answer the question "is `i` used in any scopes currently
//                                         being parsed?"
//
// The algorithm:
//
// Let Used be a map of names to lists.
// Let Declared(ScopeId) be a list of declarations for a scope numbered with
// ScopeId
//
// 1. Number all scopes in monotonic increasing order in textual order.
// 2. Number all scripts in monotonic increasing order in textual order.
// 3. When an identifier `i` is used in (ScriptId,ScopeId), append that use to
//    the list Used[i] (creating the list and table entry if necessary).
// 4. When an identifier `i` is declared in a scope numbered ScopeId, append `i`
//    to Declared(ScopeId).
// 5. When we finish parsing a scope numbered with ScopeId, in script numbered
//    ScriptId, for each declared name d in Declared(ScopeId):
//   a. If d is found in Used, mark d as closed over if there is a value
//      (UsedScriptId, UsedScopeId) in Used[d] such that UsedScriptId > ScriptId
//      and UsedScopeId > ScopeId.
//   b. Remove all values uses in Used[d] where UsedScopeId > ScopeId.
//
// Steps 1 and 2 are implemented by UsedNameTracker::next{Script,Scope}Id.
// Step 3 is implemented by UsedNameTracker::noteUsedInScope.
// Step 4 is implemented by ParseContext::Scope::addDeclaredName.
// Step 5 is implemented by UsedNameTracker::noteBoundInScope and
// Parser::propagateFreeNamesAndMarkClosedOverBindings
//
// The following is a worked example to show how the algorithm works on a
// relatively simple piece of code. (clang-format is disabled due to the width
// of the example).

// clang-format off
//
// // Script 1, Scope 1
// var x = 1;                              // Declared(1) = [x];
// function f() {// Script 2, Scope 2
//     if (x > 10) { //Scope 3             // Used[x] = [(2,2)];
//         var x = 12;                     // Declared(3) = [x];
//         function g() // Script 3
//         { // Scope 4
//             return x;                   // Used[x] = [(2,2), (3,4)]
//         }                               // Leaving Script 3, Scope 4: No declared variables.
//     }                                   // Leaving Script 2, Scope 3: Declared(3) = [x];
//                                         // - Used[x][1] = (2,2) is not > (2,3)
//                                         // - Used[x][2] = (3,4) is > (2,3), so mark x as closed over.
//                                         // - Update Used[x]: [] -- Makes sense, as at this point we have
//                                         //                         bound all the unbound x to a particlar
//                                         //                         declaration..
//
//     else { // Scope 5
//         var x = 14;                     // Declared(5) = [x]
//         function g() // Script 4
//         { // Scope 6
//             return y;                   // Used[y] = [(4,6)]
//         }                               // Leaving Script 4, Scope 6: No declarations.
//     }                                   // Leaving Script 2, Scope 5: Declared(5) = [x]
//                                         // - Used[x] = [], so don't mark x as closed over.
//     var y = 12;                         // Declared(2) = [y]
// }                                       // Leaving Script 2, Scope 2: Declared(2) = [y]
//                                         // - Used[y][1] = (4,6) is > (2,2), so mark y as closed over.
//                                         // - Update Used[y]: [].
//
// // A second case shows why isClosedOver() must scan *every*
// // outstanding use, not just the most recent one. Uses are appended in
// // increasing scopeId order, so a use from a more deeply nested script can sit
// // before a later use in a newer sibling scope of the same script.
//
// function outer() {// Script 5, Scope 7
//     var z = 1;                          // Declared(7) = [z]
//     function inner() // Script 6
//     { // Scope 8
//         return z;                       // Used[z] = [(6,8)]
//     }                                   // Leaving Script 6, Scope 8: No declared variables.
//     { // Scope 9
//         z++;                            // Used[z] = [(6,8), (5,9)]
//     }                                   // Leaving Script 5, Scope 9: No declared variables.
// }                                       // Leaving Script 5, Scope 7: Declared(7) = [z]
//                                         //
//                                         // Query isClosedOver(z) for Script 5.
//                                         // - Used[z].back() = Used[z][2] = (5,9): 5 is not > 5.
//                                         // - But Used[z][1] = (6,8): 6 > 5, so z is
//                                         //   closed over.
//                                         // Inspecting only the last use would wrongly
//                                         // report false.

// clang-format on

struct UnboundPrivateName {
  TaggedParserAtomIndex atom;
  TokenPos position;

  UnboundPrivateName(TaggedParserAtomIndex atom, TokenPos position)
      : atom(atom), position(position) {}
};

class UsedNameTracker {
 public:
  struct Use {
    uint32_t scriptId;
    uint32_t scopeId;
  };

  class UsedNameInfo {
    friend class UsedNameTracker;

    Vector<Use, 6> uses_;

    NameVisibility visibility_ = NameVisibility::Public;

    // The first place this name was used. This is important to track
    // for private names, as we will use this location to issue
    // diagnostics for using a name that's not defined lexically.
    mozilla::Maybe<TokenPos> firstUsePos_;

    void resetToScope(uint32_t scriptId, uint32_t scopeId);

    bool noteUsedInScope(uint32_t scriptId, uint32_t scopeId) {
      if (uses_.empty() || uses_.back().scopeId < scopeId) {
        return uses_.append(Use{scriptId, scopeId});
      }
      return true;
    }

    void noteBoundInScope(uint32_t scriptId, uint32_t scopeId,
                          bool* closedOver) {
      *closedOver = false;
      while (!uses_.empty()) {
        Use& innermost = uses_.back();
        if (innermost.scopeId < scopeId) {
          break;
        }
        if (innermost.scriptId > scriptId) {
          *closedOver = true;
        }
        uses_.popBack();
      }
    }

   public:
    explicit UsedNameInfo(FrontendContext* fc, NameVisibility visibility,
                          mozilla::Maybe<TokenPos> position)
        : uses_(fc), visibility_(visibility), firstUsePos_(position) {}

    UsedNameInfo(UsedNameInfo&& other) = default;

    bool isUsedInScript(uint32_t scriptId) const {
      return !uses_.empty() && uses_.back().scriptId >= scriptId;
    }

    // A name is closed over if any outstanding use comes from a more deeply
    // nested script. Uses are appended by increasing scopeId (source order of
    // scope creation), so a later use in a newer sibling/lexical scope of the
    // current script can follow an earlier use from a nested script.
    //
    // For a worked example, see the top of the file.
    bool isClosedOver(uint32_t scriptId) const {
      for (const Use& use : uses_) {
        if (use.scriptId > scriptId) {
          return true;
        }
      }
      return false;
    }

    // To allow disambiguating public and private symbols
    bool isPublic() { return visibility_ == NameVisibility::Public; }

    bool empty() const { return uses_.empty(); }

    mozilla::Maybe<TokenPos> pos() { return firstUsePos_; }

    // When we leave a scope, and subsequently find a new private name
    // reference, we don't want our error messages to be attributed to an old
    // scope, so we update the position in that scenario.
    void maybeUpdatePos(mozilla::Maybe<TokenPos> p) {
      MOZ_ASSERT_IF(!isPublic(), p.isSome());

      if (empty() && !isPublic()) {
        firstUsePos_ = std::move(p);
      }
    }
  };

  using UsedNameMap =
      HashMap<TaggedParserAtomIndex, UsedNameInfo, TaggedParserAtomIndexHasher>;

 private:
  // The map of names to chains of uses.
  UsedNameMap map_;

  // Monotonically increasing id for all nested scripts.
  uint32_t scriptCounter_;

  // Monotonically increasing id for all nested scopes.
  uint32_t scopeCounter_;

  // Set if a private name was encountered.
  // Used to short circuit some private field early error checks
  bool hasPrivateNames_;

 public:
  explicit UsedNameTracker(FrontendContext* fc)
      : map_(fc),
        scriptCounter_(0),
        scopeCounter_(0),
        hasPrivateNames_(false) {}

  uint32_t nextScriptId() {
    MOZ_ASSERT(scriptCounter_ != UINT32_MAX,
               "ParseContext::Scope::init should have prevented wraparound");
    return scriptCounter_++;
  }

  uint32_t nextScopeId() {
    MOZ_ASSERT(scopeCounter_ != UINT32_MAX);
    return scopeCounter_++;
  }

  UsedNameMap::Ptr lookup(TaggedParserAtomIndex name) const {
    return map_.lookup(name);
  }

  void noteBindingInScope(TaggedParserAtomIndex name, uint32_t scriptId,
                          uint32_t scopeId, bool* closedOver) {
    if (auto p = map_.lookup(name)) {
      p->value().noteBoundInScope(scriptId, scopeId, closedOver);
    }
  }

  [[nodiscard]] bool noteUse(
      FrontendContext* fc, TaggedParserAtomIndex name,
      NameVisibility visibility, uint32_t scriptId, uint32_t scopeId,
      mozilla::Maybe<TokenPos> tokenPosition = mozilla::Nothing());

  // Fill maybeUnboundName with the first (source order) unbound name, or
  // Nothing() if there are no unbound names.
  [[nodiscard]] bool hasUnboundPrivateNames(
      FrontendContext* fc,
      mozilla::Maybe<UnboundPrivateName>& maybeUnboundName);

  // Return a list of unbound private names, sorted by increasing location in
  // the source.
  [[nodiscard]] bool getUnboundPrivateNames(
      Vector<UnboundPrivateName, 8>& unboundPrivateNames);

  struct RewindToken {
   private:
    friend class UsedNameTracker;
    uint32_t scriptId;
    uint32_t scopeId;
  };

  RewindToken getRewindToken() const {
    RewindToken token;
    token.scriptId = scriptCounter_;
    token.scopeId = scopeCounter_;
    return token;
  }

  // Resets state so that scriptId and scopeId are the innermost script and
  // scope, respectively. Used for rewinding state on syntax parse failure.
  void rewind(RewindToken token);

  const UsedNameMap& map() const { return map_; }

#if defined(DEBUG) || defined(JS_JITSPEW)
  void dump(ParserAtomsTable& table);
#endif
};

}  // namespace frontend
}  // namespace js

#endif
