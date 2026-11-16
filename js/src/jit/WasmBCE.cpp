/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/WasmBCE.h"

#include "jit/JitSpewer.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"
#include "wasm/WasmMetadata.h"

using mozilla::HashGeneric;

using namespace js;
using namespace js::jit;

struct LastCheckedKey {
  // The type of collection being bounds checked (e.g. a memory or a table).
  MWasmBoundsCheck::Target target;

  // The index of the collection being bounds checked (e.g. memory 0).
  uint32_t targetIndex;

  // The MIR ID of the address being bounds checked.
  uint32_t addressID;

  using Lookup = LastCheckedKey;
  static HashNumber hash(const Lookup& l) {
    return HashGeneric(l.target, l.targetIndex, l.addressID);
  }
  static bool match(const LastCheckedKey& lhs, const Lookup& rhs) {
    return lhs.target == rhs.target && lhs.targetIndex == rhs.targetIndex &&
           lhs.addressID == rhs.addressID;
  }
};

using LastCheckedMap = js::HashMap<LastCheckedKey, MWasmBoundsCheck*,
                                   LastCheckedKey, SystemAllocPolicy>;

// The Wasm Bounds Check Elimination (BCE) pass looks for bounds checks
// on SSA values that have already been checked (in the same block or in a
// dominating block). These bounds checks are redundant and thus eliminated.
//
// Note: This is safe in the presence of dynamic memory sizes as long as they
// can ONLY GROW. If we allow SHRINKING the heap, this pass should be
// RECONSIDERED.
//
// TODO (dbounov): Are there a lot of cases where there is no single dominating
// check, but a set of checks that together dominate a redundant check?
//
// TODO (dbounov): Generalize to constant additions relative to one base
bool jit::EliminateBoundsChecks(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_WasmBCE, "Begin");
  // Map for dominating block where a given definition was checked
  LastCheckedMap lastChecked;

  for (ReversePostorderIterator bIter(graph.rpoBegin());
       bIter != graph.rpoEnd(); bIter++) {
    MBasicBlock* block = *bIter;
    for (MDefinitionIterator dIter(block); dIter;) {
      MDefinition* def = *dIter++;

      if (!def->isWasmBoundsCheck()) {
        continue;
      }

      MWasmBoundsCheck* bc = def->toWasmBoundsCheck();
      MDefinition* addr = bc->index();

      if (bc->target() == MWasmBoundsCheck::Other) {
        continue;
      }

      if (addr->isConstant()) {
        // Eliminate constant-address bounds checks to addresses below the
        // memory/table minimum length, or below a constant array length.

        uint64_t addrConstantValue = UINT64_MAX;
        switch (addr->type()) {
          case MIRType::Int32: {
            addrConstantValue = addr->toConstant()->toInt32();
          } break;
          case MIRType::Int64: {
            addrConstantValue = addr->toConstant()->toInt64();
          } break;
          default:
            break;
        }

        uint64_t initialLength = 0;
        switch (bc->target()) {
          case MWasmBoundsCheck::Memory: {
            initialLength = mir->wasmCodeMeta()
                                ->memories[bc->targetIndex()]
                                .initialLength();
          } break;
          case MWasmBoundsCheck::Table: {
            initialLength =
                mir->wasmCodeMeta()->tables[bc->targetIndex()].initialLength();
          } break;
          case MWasmBoundsCheck::Array: {
            // For arrays the bounds check limit is a load of the numElements
            // field. If the array was created in this function by an
            // array.new* with a constant length, work back to that constant
            // and use it.
            MDefinition* limit = bc->boundsCheckLimit();
            if (!limit->isWasmLoadField() ||
                !limit->toWasmLoadField()->base()->isWasmNewArrayObject()) {
              continue;
            }
            MDefinition* numElements = limit->toWasmLoadField()
                                           ->base()
                                           ->toWasmNewArrayObject()
                                           ->numElements();
            if (!numElements->isConstant() ||
                numElements->type() != MIRType::Int32) {
              continue;
            }
            initialLength = numElements->toConstant()->toInt32();
          } break;
          default:
            MOZ_CRASH();
        }

        if (addrConstantValue < initialLength) {
          bc->setRedundant();
          if (JitOptions.spectreIndexMasking) {
            bc->replaceAllUsesWith(addr);
          } else {
            MOZ_ASSERT(!bc->hasUses());
          }
        }
      } else if (bc->target() == MWasmBoundsCheck::Memory ||
                 bc->target() == MWasmBoundsCheck::Table) {
        // Eliminate bounds checks that are dominated by another bounds check of
        // the same value.

        LastCheckedKey key = (LastCheckedKey){
            .target = bc->target(),
            .targetIndex = bc->targetIndex(),
            .addressID = addr->id(),
        };
        LastCheckedMap::AddPtr ptr = lastChecked.lookupForAdd(key);
        if (!ptr) {
          // We have not yet seen a bounds check for this address; track it.
          if (!lastChecked.add(ptr, key, bc)) {
            return false;
          }
        } else {
          MWasmBoundsCheck* prevCheckOfSameAddr = ptr->value();
          if (prevCheckOfSameAddr->block()->dominates(block)) {
            bc->setRedundant();
            if (JitOptions.spectreIndexMasking) {
              bc->replaceAllUsesWith(prevCheckOfSameAddr);
            } else {
              MOZ_ASSERT(!bc->hasUses());
            }
          }
        }
      }
    }
  }

  return true;
}
