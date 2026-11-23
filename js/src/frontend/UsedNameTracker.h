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
// The data structure also validates that all uses of private identifiers have a
// corresponding binding, to emit an early error. This can be done efficiently
// by counting unresolved uses.
//
// The algorithm:
//
// Let Used be a map of names to lists.
// Let Declared(ScopeId) be a list of declarations for a scope numbered with
// ScopeId.
// Let UnboundPrivates be a non-negative integer initialized to zero.
//
// 1. Number all scopes in monotonic increasing order in textual order.
// 2. Number all scripts in monotonic increasing order in textual order.
// 3. When an identifier `i` is first used in (ScriptId,ScopeId):
//   a. Append that use to the list Used[i] (creating the list and table entry
//      if necessary).
//   b. If `i` is a private name, increment UnboundPrivates.
// 4. When an identifier `i` is declared in a scope numbered ScopeId, append `i`
//    to Declared(ScopeId).
// 5. When we finish parsing a scope numbered with ScopeId, in script numbered
//    ScriptId, for each declared name `d` in Declared(ScopeId):
//   a. If d is found in Used, mark d as closed over if there is a value
//      (UsedScriptId, UsedScopeId) in Used[d] such that UsedScriptId > ScriptId
//      and UsedScopeId > ScopeId.
//   b. Remove all values uses in Used[d] where UsedScopeId > ScopeId.
//   c. If `d` is a private name, decrement UnboundPrivates once for each value
//      removed in step b.
// 6. When we finish parsing an outermost class or whole script, if
//    UnboundPrivates is not zero:
//   a. Walk all lists in Used to identify the offending identifiers.
//   b. Trigger a syntax error showing the offending identifiers.
//
// Steps 1 and 2 are implemented by UsedNameTracker::next{Script,Scope}Id.
// Step 3 is implemented by UsedNameTracker::noteUse.
// Step 4 is implemented by ParseContext::Scope::addDeclaredName.
// Step 5 is implemented by UsedNameTracker::noteBindingInScope and
// Parser::propagateFreeNamesAndMarkClosedOverBindings
// Step 6 is implemented by UsedNameTracker::{has,get}UnboundPrivateNames and
// their callers.
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
//
// // A third case shows how the algorithm tracks unbound private uses:
// // (class scripts and scopes simplified for clarity)
//
// class A {// Scope 10
//    bar()
//    { // Script 7, Scope 11
//       this.#y++;                        // Used[#y] = [(7,11)]. UnboundPrivates = 1 (#y: 1).
//       class B {// Scope 12
//          #x;                            // Declared(12) = [#x].
//          foo()
//          { // Script 8, Scope 13
//             return this.#x + this.#y;   // Used[#x] = [(8,13)]. UnboundPrivates = 2 (#x: 1, #y: 1).
//                                         // Used[#y] = [(7,11),(8,13)]. UnboundPrivates = 3 (#x: 1, #y: 2).
//          }                              // Leaving Script 8, Scope 13: No declared variables.
//       }                                 // Leaving Scope 12: Declared(12) = [#x]. UnboundPrivates = 2 (#y: 2).
//                                         // - Used[#x] = [].
//                                         // - We removed one entry from Used[#x],
//                                         //   so we decrement UnboundPrivates once.
//    }                                    // Leaving Script 7, Scope 11: No declared variables.
//    #y;                                  // Declared(10) = [#y].
// }                                       // Leaving Scope 10: Declared(10) = [#y]. UnboundPrivates = 0.
//                                         // - Used[#y] = [].
//                                         // - We removed two entries from Used[#y],
//                                         //   so we have to decrement UnboundPrivates twice.
//                                         //
//                                         // A call to getUnboundPrivateNames() returns [].
//                                         // - UnboundPrivates = 0, so we do not have to walk Used.

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

    // All modifications to |uses_| must go through {push,pop}Use to ensure
    // that |unboundPrivateCounter| is kept exactly equal to the sum of
    // |uses_| entries across all private names.
    bool pushUse(uint32_t scriptId, uint32_t scopeId,
                 size_t* unboundPrivateCounter) {
      if (!uses_.append(Use{scriptId, scopeId})) {
        return false;
      }

      if (visibility_ == NameVisibility::Private) {
        *unboundPrivateCounter += 1;
      }

      return true;
    }

    // All removals from |uses_| go through here.
    void popUse(size_t* unboundPrivateCounter) {
      if (visibility_ == NameVisibility::Private) {
        MOZ_ASSERT(*unboundPrivateCounter != 0);
        *unboundPrivateCounter -= 1;
      }

      uses_.popBack();
    }

    void resetToScope(uint32_t scriptId, uint32_t scopeId,
                      size_t* unboundPrivateCounter);

    bool noteUsedInScope(uint32_t scriptId, uint32_t scopeId,
                         size_t* unboundPrivateCounter) {
      if (uses_.empty() || uses_.back().scopeId < scopeId) {
        return pushUse(scriptId, scopeId, unboundPrivateCounter);
      }
      return true;
    }

    void noteBoundInScope(uint32_t scriptId, uint32_t scopeId, bool* closedOver,
                          size_t* unboundPrivateCounter) {
      *closedOver = false;
      while (!uses_.empty()) {
        Use& innermost = uses_.back();
        if (innermost.scopeId < scopeId) {
          break;
        }
        if (innermost.scriptId > scriptId) {
          *closedOver = true;
        }
        popUse(unboundPrivateCounter);
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

  // Number of unresolved uses of private names.
  // Used to determine whether there is an early error,
  // without scanning |map_|.
  size_t unboundPrivateCounter_;

 public:
  explicit UsedNameTracker(FrontendContext* fc)
      : map_(fc),
        scriptCounter_(0),
        scopeCounter_(0),
        unboundPrivateCounter_(0) {}

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
      p->value().noteBoundInScope(scriptId, scopeId, closedOver,
                                  &unboundPrivateCounter_);
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
