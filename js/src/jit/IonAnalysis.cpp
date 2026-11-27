/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/IonAnalysis.h"

#include "mozilla/CheckedArithmetic.h"
#include "mozilla/HashFunctions.h"

#include <algorithm>

#include "jit/AliasAnalysis.h"
#include "jit/CompileInfo.h"
#include "jit/DominatorTree.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"

#include "vm/BytecodeUtil-inl.h"

using namespace js;
using namespace js::jit;

using mozilla::DebugOnly;

bool jit::SplitCriticalEdgesForBlock(MIRGraph& graph, MBasicBlock* block) {
  if (block->numSuccessors() < 2) {
    return true;
  }
  for (size_t i = 0; i < block->numSuccessors(); i++) {
    MBasicBlock* target = block->getSuccessor(i);
    if (target->numPredecessors() < 2) {
      continue;
    }

    // Create a simple new block which contains a goto and which split the
    // edge between block and target.
    MBasicBlock* split = MBasicBlock::NewSplitEdge(graph, block, i, target);
    if (!split) {
      return false;
    }
  }
  return true;
}

// A critical edge is an edge which is neither its successor's only predecessor
// nor its predecessor's only successor. Critical edges must be split to
// prevent copy-insertion and code motion from affecting other edges.
bool jit::SplitCriticalEdges(MIRGraph& graph) {
  for (MBasicBlockIterator iter(graph.begin()); iter != graph.end(); iter++) {
    MBasicBlock* block = *iter;
    if (!SplitCriticalEdgesForBlock(graph, block)) {
      return false;
    }
  }
  return true;
}

bool jit::IsUint32Type(const MDefinition* def) {
  if (def->isBeta()) {
    def = def->getOperand(0);
  }

  if (def->type() != MIRType::Int32) {
    return false;
  }

  return def->isUrsh() && def->getOperand(1)->isConstant() &&
         def->getOperand(1)->toConstant()->type() == MIRType::Int32 &&
         def->getOperand(1)->toConstant()->toInt32() == 0;
}

bool jit::FoldEmptyBlocks(MIRGraph& graph, bool* changed) {
  *changed = false;

  for (MBasicBlockIterator iter(graph.begin()); iter != graph.end();) {
    MBasicBlock* block = *iter;
    iter++;

    if (block->numPredecessors() != 1 || block->numSuccessors() != 1) {
      continue;
    }

    if (!block->phisEmpty()) {
      continue;
    }

    if (block->outerResumePoint()) {
      continue;
    }

    if (*block->begin() != *block->rbegin()) {
      continue;
    }

    MBasicBlock* succ = block->getSuccessor(0);
    MBasicBlock* pred = block->getPredecessor(0);

    if (succ->numPredecessors() != 1) {
      continue;
    }

    size_t pos = pred->getSuccessorIndex(block);
    pred->lastIns()->replaceSuccessor(pos, succ);

    graph.removeBlock(block);

    if (!succ->addPredecessorSameInputsAs(pred, block)) {
      return false;
    }
    succ->removePredecessor(block);

    *changed = true;
  }
  return true;
}

static void EliminateTriviallyDeadResumePointOperands(MIRGraph& graph,
                                                      MResumePoint* rp) {
  // If we will pop the top of the stack immediately after resuming,
  // then don't preserve the top value in the resume point.
  if (rp->mode() != ResumeMode::ResumeAt) {
    return;
  }

  jsbytecode* pc = rp->pc();
  if (JSOp(*pc) == JSOp::JumpTarget) {
    pc += JSOpLength_JumpTarget;
  }
  if (JSOp(*pc) != JSOp::Pop) {
    return;
  }

  size_t top = rp->stackDepth() - 1;
  MOZ_ASSERT(!rp->isObservableOperand(top));

  MDefinition* def = rp->getOperand(top);
  if (def->isConstant()) {
    return;
  }

  MConstant* constant = rp->block()->optimizedOutConstant(graph.alloc());
  rp->replaceOperand(top, constant);
}

// Operands to a resume point which are dead at the point of the resume can be
// replaced with a magic value. This pass only replaces resume points which are
// trivially dead.
//
// This is intended to ensure that extra resume points within a basic block
// will not artificially extend the lifetimes of any SSA values. This could
// otherwise occur if the new resume point captured a value which is created
// between the old and new resume point and is dead at the new resume point.
bool jit::EliminateTriviallyDeadResumePointOperands(const MIRGenerator* mir,
                                                    MIRGraph& graph) {
  for (auto* block : graph) {
    if (MResumePoint* rp = block->entryResumePoint()) {
      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      ::EliminateTriviallyDeadResumePointOperands(graph, rp);
    }
  }
  return true;
}

// Operands to a resume point which are dead at the point of the resume can be
// replaced with a magic value. This analysis supports limited detection of
// dead operands, pruning those which are defined in the resume point's basic
// block and have no uses outside the block or at points later than the resume
// point.
//
// This is intended to ensure that extra resume points within a basic block
// will not artificially extend the lifetimes of any SSA values. This could
// otherwise occur if the new resume point captured a value which is created
// between the old and new resume point and is dead at the new resume point.
bool jit::EliminateDeadResumePointOperands(const MIRGenerator* mir,
                                           MIRGraph& graph) {
  // If we are compiling try blocks, locals and arguments may be observable
  // from catch or finally blocks (which Ion does not compile). For now just
  // disable the pass in this case.
  if (graph.hasTryBlock()) {
    return true;
  }

  for (PostorderIterator block = graph.poBegin(); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Eliminate Dead Resume Point Operands (main loop)")) {
      return false;
    }

    if (MResumePoint* rp = block->entryResumePoint()) {
      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      ::EliminateTriviallyDeadResumePointOperands(graph, rp);
    }

    // The logic below can get confused on infinite loops.
    if (block->isLoopHeader() && block->backedge() == *block) {
      continue;
    }

    for (MInstructionIterator ins = block->begin(); ins != block->end();
         ins++) {
      if (MResumePoint* rp = ins->resumePoint()) {
        if (!graph.alloc().ensureBallast()) {
          return false;
        }
        ::EliminateTriviallyDeadResumePointOperands(graph, rp);
      }

      // No benefit to replacing constant operands with other constants.
      if (ins->isConstant()) {
        continue;
      }

      // Scanning uses does not give us sufficient information to tell
      // where instructions that are involved in box/unbox operations or
      // parameter passing might be live. Rewriting uses of these terms
      // in resume points may affect the interpreter's behavior. Rather
      // than doing a more sophisticated analysis, just ignore these.
      if (ins->isUnbox() || ins->isParameter() || ins->isBoxNonStrictThis()) {
        continue;
      }

      // Early intermediate values captured by resume points, such as
      // ArrayState and its allocation, may be legitimately dead in Ion code,
      // but are still needed if we bail out. They can recover on bailout.
      if (ins->isRecoveredOnBailout()) {
        MOZ_ASSERT(ins->canRecoverOnBailout());
        continue;
      }

      // If the instruction's behavior has been constant folded into a
      // separate instruction, we can't determine precisely where the
      // instruction becomes dead and can't eliminate its uses.
      if (ins->isImplicitlyUsed()) {
        continue;
      }

      // Check if this instruction's result is only used within the
      // current block, and keep track of its last use in a definition
      // (not resume point). This requires the instructions in the block
      // to be numbered, ensured by running this immediately after alias
      // analysis.
      uint32_t maxDefinition = 0;
      for (MUseIterator uses(ins->usesBegin()); uses != ins->usesEnd();
           uses++) {
        MNode* consumer = uses->consumer();
        if (consumer->isResumePoint()) {
          // If the instruction's is captured by one of the resume point, then
          // it might be observed indirectly while the frame is live on the
          // stack, so it has to be computed.
          MResumePoint* resume = consumer->toResumePoint();
          if (resume->isObservableOperand(*uses)) {
            maxDefinition = UINT32_MAX;
            break;
          }
          continue;
        }

        MDefinition* def = consumer->toDefinition();
        if (def->block() != *block || def->isBox() || def->isPhi()) {
          maxDefinition = UINT32_MAX;
          break;
        }
        maxDefinition = std::max(maxDefinition, def->id());
      }
      if (maxDefinition == UINT32_MAX) {
        continue;
      }

      // Walk the uses a second time, removing any in resume points after
      // the last use in a definition.
      for (MUseIterator uses(ins->usesBegin()); uses != ins->usesEnd();) {
        MUse* use = *uses++;
        if (use->consumer()->isDefinition()) {
          continue;
        }
        MResumePoint* mrp = use->consumer()->toResumePoint();
        if (mrp->block() != *block || !mrp->instruction() ||
            mrp->instruction() == *ins ||
            mrp->instruction()->id() <= maxDefinition) {
          continue;
        }

        if (!graph.alloc().ensureBallast()) {
          return false;
        }

        // Store an optimized out magic value in place of all dead
        // resume point operands. Making any such substitution can in
        // general alter the interpreter's behavior, even though the
        // code is dead, as the interpreter will still execute opcodes
        // whose effects cannot be observed. If the magic value value
        // were to flow to, say, a dead property access the
        // interpreter could throw an exception; we avoid this problem
        // by removing dead operands before removing dead code.
        MConstant* constant =
            MConstant::NewMagic(graph.alloc(), JS_OPTIMIZED_OUT);
        block->insertBefore(*(block->begin()), constant);
        use->replaceProducer(constant);
      }
    }
  }

  return true;
}

// Test whether |def| would be needed if it had no uses.
bool js::jit::DeadIfUnused(const MDefinition* def) {
  // Effectful instructions of course cannot be removed.
  if (def->isEffectful()) {
    return false;
  }

  // Never eliminate guard instructions.
  if (def->isGuard()) {
    return false;
  }

  // Required to be preserved, as the type guard related to this instruction
  // is part of the semantics of a transformation.
  if (def->isGuardRangeBailouts()) {
    return false;
  }

  // Control instructions have no uses, but also shouldn't be optimized out
  if (def->isControlInstruction()) {
    return false;
  }

  // Used when lowering to generate the corresponding snapshots and aggregate
  // the list of recover instructions to be repeated.
  if (def->isInstruction() && def->toInstruction()->resumePoint()) {
    return false;
  }

  return true;
}

// Similar to DeadIfUnused(), but additionally allows effectful instructions.
bool js::jit::DeadIfUnusedAllowEffectful(const MDefinition* def) {
  // Never eliminate guard instructions.
  if (def->isGuard()) {
    return false;
  }

  // Required to be preserved, as the type guard related to this instruction
  // is part of the semantics of a transformation.
  if (def->isGuardRangeBailouts()) {
    return false;
  }

  // Control instructions have no uses, but also shouldn't be optimized out
  if (def->isControlInstruction()) {
    return false;
  }

  // Used when lowering to generate the corresponding snapshots and aggregate
  // the list of recover instructions to be repeated.
  if (def->isInstruction() && def->toInstruction()->resumePoint()) {
    // All effectful instructions must have a resume point attached. We're
    // allowing effectful instructions here, so we have to ignore any resume
    // points if we want to consider effectful instructions as dead.
    if (!def->isEffectful()) {
      return false;
    }
  }

  return true;
}

// Test whether |def| may be safely discarded, due to being dead or due to being
// located in a basic block which has itself been marked for discarding.
bool js::jit::IsDiscardable(const MDefinition* def) {
  return !def->hasUses() && (DeadIfUnused(def) || def->block()->isMarked());
}

// Similar to IsDiscardable(), but additionally allows effectful instructions.
bool js::jit::IsDiscardableAllowEffectful(const MDefinition* def) {
  return !def->hasUses() &&
         (DeadIfUnusedAllowEffectful(def) || def->block()->isMarked());
}

// Instructions are useless if they are unused and have no side effects.
// This pass eliminates useless instructions.
// The graph itself is unchanged.
bool jit::EliminateDeadCode(const MIRGenerator* mir, MIRGraph& graph) {
  // Traverse in postorder so that we hit uses before definitions.
  // Traverse instruction list backwards for the same reason.
  for (PostorderIterator block = graph.poBegin(); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Eliminate Dead Code (main loop)")) {
      return false;
    }

    // Remove unused instructions.
    for (MInstructionReverseIterator iter = block->rbegin();
         iter != block->rend();) {
      MInstruction* inst = *iter++;
      if (js::jit::IsDiscardable(inst)) {
        block->discard(inst);
      }
    }
  }

  return true;
}

static inline bool IsPhiObservable(MPhi* phi, Observability observe) {
  // If the phi has uses which are not reflected in SSA, then behavior in the
  // interpreter may be affected by removing the phi.
  if (phi->isImplicitlyUsed()) {
    return true;
  }

  // Check for uses of this phi node outside of other phi nodes.
  // Note that, initially, we skip reading resume points, which we
  // don't count as actual uses. If the only uses are resume points,
  // then the SSA name is never consumed by the program.  However,
  // after optimizations have been performed, it's possible that the
  // actual uses in the program have been (incorrectly) optimized
  // away, so we must be more conservative and consider resume
  // points as well.
  for (MUseIterator iter(phi->usesBegin()); iter != phi->usesEnd(); iter++) {
    MNode* consumer = iter->consumer();
    if (consumer->isResumePoint()) {
      MResumePoint* resume = consumer->toResumePoint();
      if (observe == ConservativeObservability) {
        return true;
      }
      if (resume->isObservableOperand(*iter)) {
        return true;
      }
    } else {
      MDefinition* def = consumer->toDefinition();
      if (!def->isPhi()) {
        return true;
      }
    }
  }

  return false;
}

// Handles cases like:
//    x is phi(a, x) --> a
//    x is phi(a, a) --> a
static inline MDefinition* IsPhiRedundant(MPhi* phi) {
  MDefinition* first = phi->operandIfRedundant();
  if (first == nullptr) {
    return nullptr;
  }

  // Propagate the ImplicitlyUsed flag if |phi| is replaced with another phi.
  if (phi->isImplicitlyUsed()) {
    first->setImplicitlyUsedUnchecked();
  }

  return first;
}

bool jit::EliminatePhis(const MIRGenerator* mir, MIRGraph& graph,
                        Observability observe) {
  // Eliminates redundant or unobservable phis from the graph.  A
  // redundant phi is something like b = phi(a, a) or b = phi(a, b),
  // both of which can be replaced with a.  An unobservable phi is
  // one that whose value is never used in the program.
  //
  // Note that we must be careful not to eliminate phis representing
  // values that the interpreter will require later.  When the graph
  // is first constructed, we can be more aggressive, because there
  // is a greater correspondence between the CFG and the bytecode.
  // After optimizations such as GVN have been performed, however,
  // the bytecode and CFG may not correspond as closely to one
  // another.  In that case, we must be more conservative.  The flag
  // |conservativeObservability| is used to indicate that eliminate
  // phis is being run after some optimizations have been performed,
  // and thus we should use more conservative rules about
  // observability.  The particular danger is that we can optimize
  // away uses of a phi because we think they are not executable,
  // but the foundation for that assumption is false TI information
  // that will eventually be invalidated.  Therefore, if
  // |conservativeObservability| is set, we will consider any use
  // from a resume point to be observable.  Otherwise, we demand a
  // use from an actual instruction.

  Vector<MPhi*, 16, SystemAllocPolicy> worklist;

  // Add all observable phis to a worklist. We use the "in worklist" bit to
  // mean "this phi is live".
  for (PostorderIterator block = graph.poBegin(); block != graph.poEnd();
       block++) {
    MPhiIterator iter = block->phisBegin();
    while (iter != block->phisEnd()) {
      MPhi* phi = *iter++;

      if (mir->shouldCancel("Eliminate Phis (populate loop)")) {
        return false;
      }

      // Flag all as unused, only observable phis would be marked as used
      // when processed by the work list.
      phi->setUnused();

      // If the phi is redundant, remove it here.
      if (MDefinition* redundant = IsPhiRedundant(phi)) {
        phi->justReplaceAllUsesWith(redundant);
        block->discardPhi(phi);
        continue;
      }

      // Enqueue observable Phis.
      if (IsPhiObservable(phi, observe)) {
        phi->setInWorklist();
        if (!worklist.append(phi)) {
          return false;
        }
      }
    }
  }

  // Iteratively mark all phis reachable from live phis.
  while (!worklist.empty()) {
    if (mir->shouldCancel("Eliminate Phis (worklist)")) {
      return false;
    }

    MPhi* phi = worklist.popCopy();
    MOZ_ASSERT(phi->isUnused());
    phi->setNotInWorklist();

    // The removal of Phis can produce newly redundant phis.
    if (MDefinition* redundant = IsPhiRedundant(phi)) {
      // Add to the worklist the used phis which are impacted.
      for (MUseDefIterator it(phi); it; it++) {
        if (it.def()->isPhi()) {
          MPhi* use = it.def()->toPhi();
          if (!use->isUnused()) {
            use->setUnusedUnchecked();
            use->setInWorklist();
            if (!worklist.append(use)) {
              return false;
            }
          }
        }
      }
      phi->justReplaceAllUsesWith(redundant);
    } else {
      // Otherwise flag them as used.
      phi->setNotUnused();
    }

    // The current phi is/was used, so all its operands are used.
    for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
      MDefinition* in = phi->getOperand(i);
      if (!in->isPhi() || !in->isUnused() || in->isInWorklist()) {
        continue;
      }
      in->setInWorklist();
      if (!worklist.append(in->toPhi())) {
        return false;
      }
    }
  }

  // Sweep dead phis.
  for (PostorderIterator block = graph.poBegin(); block != graph.poEnd();
       block++) {
    if (mir->shouldCancel("Eliminate Phis (sweep dead phis)")) {
      return false;
    }

    MPhiIterator iter = block->phisBegin();
    while (iter != block->phisEnd()) {
      MPhi* phi = *iter++;
      if (phi->isUnused()) {
        if (!phi->optimizeOutAllUses(graph.alloc())) {
          return false;
        }
        block->discardPhi(phi);
      }
    }
  }

  return true;
}

void jit::RenumberBlocks(MIRGraph& graph) {
  size_t id = 0;
  for (ReversePostorderIterator block(graph.rpoBegin());
       block != graph.rpoEnd(); block++) {
    block->setId(id++);
  }
}

// A utility for code which adds/deletes blocks. Renumber the remaining blocks,
// recompute dominators, and optionally recompute AliasAnalysis dependencies.
bool jit::AccountForCFGChanges(const MIRGenerator* mir, MIRGraph& graph,
                               bool updateAliasAnalysis,
                               bool underValueNumberer) {
  // Renumber the blocks and clear out the old dominator info.
  size_t id = 0;
  for (ReversePostorderIterator i(graph.rpoBegin()), e(graph.rpoEnd()); i != e;
       ++i) {
    i->clearDominatorInfo();
    i->setId(id++);
  }

  // Recompute dominator info.
  if (!BuildDominatorTree(mir, graph)) {
    return false;
  }

  // If needed, update alias analysis dependencies.
  if (updateAliasAnalysis) {
    if (!AliasAnalysis(mir, graph).analyze()) {
      return false;
    }
  }

  AssertExtendedGraphCoherency(graph, underValueNumberer);
  return true;
}

bool jit::BuildPhiReverseMapping(MIRGraph& graph) {
  // Build a mapping such that given a basic block, whose successor has one or
  // more phis, we can find our specific input to that phi. To make this fast
  // mapping work we rely on a specific property of our structured control
  // flow graph: For a block with phis, its predecessors each have only one
  // successor with phis. Consider each case:
  //   * Blocks with less than two predecessors cannot have phis.
  //   * Breaks. A break always has exactly one successor, and the break
  //             catch block has exactly one predecessor for each break, as
  //             well as a final predecessor for the actual loop exit.
  //   * Continues. A continue always has exactly one successor, and the
  //             continue catch block has exactly one predecessor for each
  //             continue, as well as a final predecessor for the actual
  //             loop continuation. The continue itself has exactly one
  //             successor.
  //   * An if. Each branch as exactly one predecessor.
  //   * A switch. Each branch has exactly one predecessor.
  //   * Loop tail. A new block is always created for the exit, and if a
  //             break statement is present, the exit block will forward
  //             directly to the break block.
  for (MBasicBlockIterator block(graph.begin()); block != graph.end();
       block++) {
    if (block->phisEmpty()) {
      continue;
    }

    // Assert on the above.
    for (size_t j = 0; j < block->numPredecessors(); j++) {
      MBasicBlock* pred = block->getPredecessor(j);

#ifdef DEBUG
      size_t numSuccessorsWithPhis = 0;
      for (size_t k = 0; k < pred->numSuccessors(); k++) {
        MBasicBlock* successor = pred->getSuccessor(k);
        if (!successor->phisEmpty()) {
          numSuccessorsWithPhis++;
        }
      }
      MOZ_ASSERT(numSuccessorsWithPhis <= 1);
#endif

      pred->setSuccessorWithPhis(*block, j);
    }
  }

  return true;
}

struct BoundsCheckInfo {
  MBoundsCheck* check;
  uint32_t validEnd;
};

using BoundsCheckMap =
    HashMap<uint32_t, BoundsCheckInfo, DefaultHasher<uint32_t>, JitAllocPolicy>;

// Compute a hash for bounds checks which ignores constant offsets in the index.
static HashNumber BoundsCheckHashIgnoreOffset(MBoundsCheck* check) {
  SimpleLinearSum indexSum = ExtractLinearSum(check->index());
  uintptr_t index = indexSum.term ? uintptr_t(indexSum.term) : 0;
  uintptr_t length = uintptr_t(check->length());
  return index ^ length;
}

static MBoundsCheck* FindDominatingBoundsCheck(BoundsCheckMap& checks,
                                               MBoundsCheck* check,
                                               size_t index) {
  // Since we are traversing the dominator tree in pre-order, when we
  // are looking at the |index|-th block, the next numDominated() blocks
  // we traverse are precisely the set of blocks that are dominated.
  //
  // So, this value is visible in all blocks if:
  // index <= index + ins->block->numDominated()
  // and becomes invalid after that.
  HashNumber hash = BoundsCheckHashIgnoreOffset(check);
  BoundsCheckMap::Ptr p = checks.lookup(hash);
  if (!p || index >= p->value().validEnd) {
    // We didn't find a dominating bounds check.
    BoundsCheckInfo info;
    info.check = check;
    info.validEnd = index + check->block()->numDominated();

    if (!checks.put(hash, info)) return nullptr;

    return check;
  }

  return p->value().check;
}

static MathSpace ExtractMathSpace(MDefinition* ins) {
  MOZ_ASSERT(ins->isAdd() || ins->isSub());
  MBinaryArithInstruction* arith = nullptr;
  if (ins->isAdd()) {
    arith = ins->toAdd();
  } else {
    arith = ins->toSub();
  }
  switch (arith->truncateKind()) {
    case TruncateKind::NoTruncate:
    case TruncateKind::TruncateAfterBailouts:
      // TruncateAfterBailouts is considered as infinite space because the
      // LinearSum will effectively remove the bailout check.
      return MathSpace::Infinite;
    case TruncateKind::IndirectTruncate:
    case TruncateKind::Truncate:
      return MathSpace::Modulo;
  }
  MOZ_CRASH("Unknown TruncateKind");
}

static bool MonotoneAdd(int32_t lhs, int32_t rhs) {
  return (lhs >= 0 && rhs >= 0) || (lhs <= 0 && rhs <= 0);
}

static bool MonotoneSub(int32_t lhs, int32_t rhs) {
  return (lhs >= 0 && rhs <= 0) || (lhs <= 0 && rhs >= 0);
}

// Extract a linear sum from ins, if possible (otherwise giving the
// sum 'ins + 0').
SimpleLinearSum jit::ExtractLinearSum(MDefinition* ins, MathSpace space,
                                      int32_t recursionDepth) {
  const int32_t SAFE_RECURSION_LIMIT = 100;
  if (recursionDepth > SAFE_RECURSION_LIMIT) {
    return SimpleLinearSum(ins, 0);
  }

  // Unwrap Int32ToIntPtr. This instruction only changes the representation
  // (int32_t to intptr_t) without affecting the value.
  if (ins->isInt32ToIntPtr()) {
    ins = ins->toInt32ToIntPtr()->input();
  }

  if (ins->isBeta()) {
    ins = ins->getOperand(0);
  }

  MOZ_ASSERT(!ins->isInt32ToIntPtr());

  if (ins->type() != MIRType::Int32) {
    return SimpleLinearSum(ins, 0);
  }

  if (ins->isConstant()) {
    return SimpleLinearSum(nullptr, ins->toConstant()->toInt32());
  }

  if (!ins->isAdd() && !ins->isSub()) {
    return SimpleLinearSum(ins, 0);
  }

  // Only allow math which are in the same space.
  MathSpace insSpace = ExtractMathSpace(ins);
  if (space == MathSpace::Unknown) {
    space = insSpace;
  } else if (space != insSpace) {
    return SimpleLinearSum(ins, 0);
  }
  MOZ_ASSERT(space == MathSpace::Modulo || space == MathSpace::Infinite);

  // Note: support for the Modulo math space is currently disabled due to
  // security bugs. See bug 1966614.
  if (space == MathSpace::Modulo) {
    return SimpleLinearSum(ins, 0);
  }

  MDefinition* lhs = ins->getOperand(0);
  MDefinition* rhs = ins->getOperand(1);
  if (lhs->type() != MIRType::Int32 || rhs->type() != MIRType::Int32) {
    return SimpleLinearSum(ins, 0);
  }

  // Extract linear sums of each operand.
  SimpleLinearSum lsum = ExtractLinearSum(lhs, space, recursionDepth + 1);
  SimpleLinearSum rsum = ExtractLinearSum(rhs, space, recursionDepth + 1);

  // LinearSum only considers a single term operand, if both sides have
  // terms, then ignore extracted linear sums.
  if (lsum.term && rsum.term) {
    return SimpleLinearSum(ins, 0);
  }

  // Check if this is of the form <SUM> + n or n + <SUM>.
  if (ins->isAdd()) {
    int32_t constant;
    if (space == MathSpace::Modulo) {
      constant = uint32_t(lsum.constant) + uint32_t(rsum.constant);
    } else if (!mozilla::SafeAdd(lsum.constant, rsum.constant, &constant) ||
               !MonotoneAdd(lsum.constant, rsum.constant)) {
      return SimpleLinearSum(ins, 0);
    }
    return SimpleLinearSum(lsum.term ? lsum.term : rsum.term, constant);
  }

  MOZ_ASSERT(ins->isSub());
  // Check if this is of the form <SUM> - n.
  if (lsum.term) {
    int32_t constant;
    if (space == MathSpace::Modulo) {
      constant = uint32_t(lsum.constant) - uint32_t(rsum.constant);
    } else if (!mozilla::SafeSub(lsum.constant, rsum.constant, &constant) ||
               !MonotoneSub(lsum.constant, rsum.constant)) {
      return SimpleLinearSum(ins, 0);
    }
    return SimpleLinearSum(lsum.term, constant);
  }

  // Ignore any of the form n - <SUM>.
  return SimpleLinearSum(ins, 0);
}

// Extract a linear inequality holding when a boolean test goes in the
// specified direction, of the form 'lhs + lhsN <= rhs' (or >=).
bool jit::ExtractLinearInequality(const MTest* test, BranchDirection direction,
                                  SimpleLinearSum* plhs, MDefinition** prhs,
                                  bool* plessEqual) {
  if (!test->getOperand(0)->isCompare()) {
    return false;
  }

  MCompare* compare = test->getOperand(0)->toCompare();

  MDefinition* lhs = compare->getOperand(0);
  MDefinition* rhs = compare->getOperand(1);

  // TODO: optimize Compare_UInt32
  if (!compare->isInt32Comparison()) {
    return false;
  }

  MOZ_ASSERT(lhs->type() == MIRType::Int32);
  MOZ_ASSERT(rhs->type() == MIRType::Int32);

  JSOp jsop = compare->jsop();
  if (direction == FALSE_BRANCH) {
    jsop = NegateCompareOp(jsop);
  }

  SimpleLinearSum lsum = ExtractLinearSum(lhs);
  SimpleLinearSum rsum = ExtractLinearSum(rhs);

  if (!mozilla::SafeSub(lsum.constant, rsum.constant, &lsum.constant)) {
    return false;
  }

  // Normalize operations to use <= or >=.
  switch (jsop) {
    case JSOp::Le:
      *plessEqual = true;
      break;
    case JSOp::Lt:
      /* x < y ==> x + 1 <= y */
      if (!mozilla::SafeAdd(lsum.constant, 1, &lsum.constant)) {
        return false;
      }
      *plessEqual = true;
      break;
    case JSOp::Ge:
      *plessEqual = false;
      break;
    case JSOp::Gt:
      /* x > y ==> x - 1 >= y */
      if (!mozilla::SafeSub(lsum.constant, 1, &lsum.constant)) {
        return false;
      }
      *plessEqual = false;
      break;
    default:
      return false;
  }

  *plhs = lsum;
  *prhs = rsum.term;

  return true;
}

static bool TryEliminateBoundsCheck(BoundsCheckMap& checks, size_t blockIndex,
                                    MBoundsCheck* dominated, bool* eliminated) {
  MOZ_ASSERT(!*eliminated);

  // Replace all uses of the bounds check with the actual index.
  // This is (a) necessary, because we can coalesce two different
  // bounds checks and would otherwise use the wrong index and
  // (b) helps register allocation. Note that this is safe since
  // no other pass after bounds check elimination moves instructions.
  dominated->replaceAllUsesWith(dominated->index());

  if (!dominated->isMovable()) {
    return true;
  }

  if (!dominated->fallible()) {
    return true;
  }

  MBoundsCheck* dominating =
      FindDominatingBoundsCheck(checks, dominated, blockIndex);
  if (!dominating) {
    return false;
  }

  if (dominating == dominated) {
    // We didn't find a dominating bounds check.
    return true;
  }

  // We found two bounds checks with the same hash number, but we still have
  // to make sure the lengths and index terms are equal.
  if (dominating->length() != dominated->length()) {
    return true;
  }

  SimpleLinearSum sumA = ExtractLinearSum(dominating->index());
  SimpleLinearSum sumB = ExtractLinearSum(dominated->index());

  // Both terms should be nullptr or the same definition.
  if (sumA.term != sumB.term) {
    return true;
  }

  // This bounds check is redundant.
  *eliminated = true;

  // Normalize the ranges according to the constant offsets in the two indexes.
  int32_t minimumA, maximumA, minimumB, maximumB;
  if (!mozilla::SafeAdd(sumA.constant, dominating->minimum(), &minimumA) ||
      !mozilla::SafeAdd(sumA.constant, dominating->maximum(), &maximumA) ||
      !mozilla::SafeAdd(sumB.constant, dominated->minimum(), &minimumB) ||
      !mozilla::SafeAdd(sumB.constant, dominated->maximum(), &maximumB)) {
    return false;
  }

  // Update the dominating check to cover both ranges, denormalizing the
  // result per the constant offset in the index.
  int32_t newMinimum, newMaximum;
  if (!mozilla::SafeSub(std::min(minimumA, minimumB), sumA.constant,
                        &newMinimum) ||
      !mozilla::SafeSub(std::max(maximumA, maximumB), sumA.constant,
                        &newMaximum)) {
    return false;
  }

  dominating->setMinimum(newMinimum);
  dominating->setMaximum(newMaximum);
  dominating->setBailoutKind(BailoutKind::HoistBoundsCheck);

  return true;
}

// Eliminate checks which are redundant given each other or other instructions.
//
// A bounds check is considered redundant if it's dominated by another bounds
// check with the same length and the indexes differ by only a constant amount.
// In this case we eliminate the redundant bounds check and update the other one
// to cover the ranges of both checks.
//
// Bounds checks are added to a hash map and since the hash function ignores
// differences in constant offset, this offers a fast way to find redundant
// checks.
bool jit::EliminateRedundantChecks(MIRGraph& graph) {
  BoundsCheckMap checks(graph.alloc());

  // Stack for pre-order CFG traversal.
  Vector<MBasicBlock*, 1, JitAllocPolicy> worklist(graph.alloc());

  // The index of the current block in the CFG traversal.
  size_t index = 0;

  // Add all self-dominating blocks to the worklist.
  // This includes all roots. Order does not matter.
  for (MBasicBlockIterator i(graph.begin()); i != graph.end(); i++) {
    MBasicBlock* block = *i;
    if (block->immediateDominator() == block) {
      if (!worklist.append(block)) {
        return false;
      }
    }
  }

  // Starting from each self-dominating block, traverse the CFG in pre-order.
  while (!worklist.empty()) {
    MBasicBlock* block = worklist.popCopy();

    // Add all immediate dominators to the front of the worklist.
    if (!worklist.append(block->immediatelyDominatedBlocksBegin(),
                         block->immediatelyDominatedBlocksEnd())) {
      return false;
    }

    for (MDefinitionIterator iter(block); iter;) {
      MDefinition* def = *iter++;

      if (!def->isBoundsCheck()) {
        continue;
      }
      auto* boundsCheck = def->toBoundsCheck();

      bool eliminated = false;
      if (!TryEliminateBoundsCheck(checks, index, boundsCheck, &eliminated)) {
        return false;
      }

      if (eliminated) {
        block->discard(boundsCheck);
      }
    }
    index++;
  }

  MOZ_ASSERT(index == graph.numBlocks());

  return true;
}

static bool ShapeGuardIsRedundant(MGuardShape* guard,
                                  const MDefinition* storeObject,
                                  const Shape* storeShape) {
  const MDefinition* guardObject = guard->object()->skipObjectGuards();
  if (guardObject != storeObject) {
    JitSpew(JitSpew_RedundantShapeGuards, "SKIP: different objects (%d vs %d)",
            guardObject->id(), storeObject->id());
    return false;
  }

  const Shape* guardShape = guard->shape();
  if (guardShape != storeShape) {
    JitSpew(JitSpew_RedundantShapeGuards, "SKIP: different shapes");
    return false;
  }

  return true;
}

// Eliminate shape guards which are redundant given other instructions.
//
// A shape guard is redundant if we can prove that the object being
// guarded already has the correct shape. The conditions for doing so
// are as follows:
//
// 1. We can see the most recent change to the shape of this object.
//    (This can be an AddAndStoreSlot, an AllocateAndStoreSlot, or the
//    creation of the object itself.
// 2. That mutation dominates the shape guard.
// 3. The shape that was assigned at that point matches the shape
//    we expect.
//
// If all of these conditions hold, then we can remove the shape guard.
// In debug, we replace it with an AssertShape to help verify correctness.
bool jit::EliminateRedundantShapeGuards(MIRGraph& graph) {
  JitSpew(JitSpew_RedundantShapeGuards, "Begin");

  for (ReversePostorderIterator block = graph.rpoBegin();
       block != graph.rpoEnd(); block++) {
    for (MInstructionIterator insIter(block->begin());
         insIter != block->end();) {
      MInstruction* ins = *insIter;
      insIter++;

      // Skip instructions that aren't shape guards.
      if (!ins->isGuardShape()) {
        continue;
      }
      MGuardShape* guard = ins->toGuardShape();
      MDefinition* lastStore = guard->dependency();

      JitSpew(JitSpew_RedundantShapeGuards, "Visit shape guard %d",
              guard->id());
      JitSpewIndent spewIndent(JitSpew_RedundantShapeGuards);

      if (lastStore->isDiscarded() || lastStore->block()->isDead() ||
          !lastStore->block()->dominates(guard->block())) {
        JitSpew(JitSpew_RedundantShapeGuards,
                "SKIP: ins %d does not dominate block %d", lastStore->id(),
                guard->block()->id());
        continue;
      }

      if (lastStore->isAddAndStoreSlot()) {
        auto* add = lastStore->toAddAndStoreSlot();
        auto* addObject = add->object()->skipObjectGuards();
        if (!ShapeGuardIsRedundant(guard, addObject, add->shape())) {
          continue;
        }
      } else if (lastStore->isAllocateAndStoreSlot()) {
        auto* allocate = lastStore->toAllocateAndStoreSlot();
        auto* allocateObject = allocate->object()->skipObjectGuards();
        if (!ShapeGuardIsRedundant(guard, allocateObject, allocate->shape())) {
          continue;
        }
      } else if (lastStore->isStart()) {
        // The guard doesn't depend on any other instruction that is modifying
        // the object operand, so we check the object operand directly.
        auto* obj = guard->object()->skipObjectGuards();

        const Shape* initialShape = nullptr;
        if (obj->isNewObject()) {
          auto* templateObject = obj->toNewObject()->templateObject();
          if (!templateObject) {
            JitSpew(JitSpew_RedundantShapeGuards, "SKIP: no template");
            continue;
          }
          initialShape = templateObject->shape();
        } else if (obj->isNewPlainObject()) {
          initialShape = obj->toNewPlainObject()->shape();
        } else {
          JitSpew(JitSpew_RedundantShapeGuards,
                  "SKIP: not NewObject or NewPlainObject (%d)", obj->id());
          continue;
        }
        if (initialShape != guard->shape()) {
          JitSpew(JitSpew_RedundantShapeGuards, "SKIP: shapes don't match");
          continue;
        }
      } else {
        JitSpew(JitSpew_RedundantShapeGuards,
                "SKIP: Last store not supported (%d)", lastStore->id());
        continue;
      }

#ifdef DEBUG
      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      auto* assert = MAssertShape::New(graph.alloc(), guard->object(),
                                       const_cast<Shape*>(guard->shape()));
      guard->block()->insertBefore(guard, assert);
#endif

      JitSpew(JitSpew_RedundantShapeGuards, "SUCCESS: Removing shape guard %d",
              guard->id());
      guard->replaceAllUsesWith(guard->input());
      guard->block()->discard(guard);
    }
  }

  return true;
}

static void TryEliminateGCBarriersForAllocation(MInstruction* allocation) {
  MOZ_ASSERT(allocation->type() == MIRType::Object);

  JitSpew(JitSpew_RedundantGCBarriers, "Analyzing allocation %s",
          allocation->opName());

  MBasicBlock* block = allocation->block();
  MInstructionIterator insIter(block->begin(allocation));

  // Skip `allocation`.
  MOZ_ASSERT(*insIter == allocation);
  insIter++;

  // Try to optimize the other instructions in the block.
  while (insIter != block->end()) {
    MInstruction* ins = *insIter;
    insIter++;
    switch (ins->op()) {
      case MDefinition::Opcode::Constant:
      case MDefinition::Opcode::Box:
      case MDefinition::Opcode::Unbox:
        // These instructions can't trigger GC or affect this analysis in other
        // ways.
        break;
      case MDefinition::Opcode::StoreFixedSlot: {
        auto* store = ins->toStoreFixedSlot();
        if (store->object() != allocation) {
          JitSpew(JitSpew_RedundantGCBarriers,
                  "Stopped at StoreFixedSlot for other object");
          return;
        }
        store->setNeedsPreBarrier(false);
        store->setNeedsPostBarrier(false);
        JitSpew(JitSpew_RedundantGCBarriers, "Elided StoreFixedSlot barriers");
        break;
      }
      default:
        JitSpew(JitSpew_RedundantGCBarriers,
                "Stopped at unsupported instruction %s", ins->opName());
        return;
    }
  }
}

bool jit::EliminateRedundantGCBarriers(MIRGraph& graph) {
  // Peephole optimization for the following pattern:
  //
  //   0: MNewCallObject
  //   1: MStoreFixedSlot(0, ...)
  //   2: MStoreFixedSlot(0, ...)
  //
  // If the instructions immediately following the allocation instruction can't
  // trigger GC and we are storing to the new object's slots, we can elide both
  // the pre-barrier and the post-barrier. AddPostWriteBarriers will insert a
  // MIR instruction to assert the post barrier is unnecessary in debug builds.
  //
  // See also the similar optimizations in WarpBuilder::buildCallObject.

  JitSpew(JitSpew_RedundantGCBarriers, "Begin");

  for (ReversePostorderIterator block = graph.rpoBegin();
       block != graph.rpoEnd(); block++) {
    for (MInstructionIterator insIter(block->begin()); insIter != block->end();
         insIter++) {
      MInstruction* ins = *insIter;
      if (ins->isNewCallObject()) {
        MNewCallObject* allocation = ins->toNewCallObject();
        // We can only eliminate the post barrier if we know the call object
        // will be allocated in the nursery.
        if (allocation->initialHeap() == gc::Heap::Default) {
          TryEliminateGCBarriersForAllocation(allocation);
        }
      }
    }
  }

  return true;
}

bool jit::MarkLoadsUsedAsPropertyKeys(MIRGraph& graph) {
  // When a string is used as a property key, or as the key for a Map or Set, we
  // require it to be atomized. To avoid repeatedly atomizing the same string,
  // this analysis looks for cases where we are loading a value from the slot of
  // an object (which includes access to global variables and global lexicals)
  // and using it as a property key, and marks those loads. During codegen,
  // marked loads will check whether the value loaded is a non-atomized string.
  // If it is, we will atomize the string and update the stored value, ensuring
  // that future loads from the same slot will not have to atomize again.
  JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys, "Begin");

  for (ReversePostorderIterator block = graph.rpoBegin();
       block != graph.rpoEnd(); block++) {
    for (MInstructionIterator insIter(block->begin());
         insIter != block->end();) {
      MInstruction* ins = *insIter;
      insIter++;

      MDefinition* idVal = nullptr;
      if (ins->isGetPropertyCache()) {
        idVal = ins->toGetPropertyCache()->idval();
      } else if (ins->isHasOwnCache()) {
        idVal = ins->toHasOwnCache()->idval();
      } else if (ins->isSetPropertyCache()) {
        idVal = ins->toSetPropertyCache()->idval();
      } else if (ins->isGetPropSuperCache()) {
        idVal = ins->toGetPropSuperCache()->idval();
      } else if (ins->isMegamorphicLoadSlotByValue()) {
        idVal = ins->toMegamorphicLoadSlotByValue()->idVal();
      } else if (ins->isMegamorphicLoadSlotByValuePermissive()) {
        idVal = ins->toMegamorphicLoadSlotByValuePermissive()->idVal();
      } else if (ins->isMegamorphicHasProp()) {
        idVal = ins->toMegamorphicHasProp()->idVal();
      } else if (ins->isMegamorphicSetElement()) {
        idVal = ins->toMegamorphicSetElement()->index();
      } else if (ins->isProxyGetByValue()) {
        idVal = ins->toProxyGetByValue()->idVal();
      } else if (ins->isProxyHasProp()) {
        idVal = ins->toProxyHasProp()->idVal();
      } else if (ins->isProxySetByValue()) {
        idVal = ins->toProxySetByValue()->idVal();
      } else if (ins->isIdToStringOrSymbol()) {
        idVal = ins->toIdToStringOrSymbol()->idVal();
      } else if (ins->isGuardSpecificAtom()) {
        idVal = ins->toGuardSpecificAtom()->input();
      } else if (ins->isToHashableString()) {
        idVal = ins->toToHashableString()->input();
      } else if (ins->isToHashableValue()) {
        idVal = ins->toToHashableValue()->input();
      } else if (ins->isMapObjectHasValueVMCall()) {
        idVal = ins->toMapObjectHasValueVMCall()->value();
      } else if (ins->isMapObjectGetValueVMCall()) {
        idVal = ins->toMapObjectGetValueVMCall()->value();
      } else if (ins->isSetObjectHasValueVMCall()) {
        idVal = ins->toSetObjectHasValueVMCall()->value();
      } else {
        continue;
      }
      JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys,
              "Analyzing property access %s%d with idVal %s%d", ins->opName(),
              ins->id(), idVal->opName(), idVal->id());

      // Skip intermediate nodes.
      do {
        if (idVal->isLexicalCheck()) {
          idVal = idVal->toLexicalCheck()->input();
          JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys,
                  "- Skipping lexical check. idVal is now %s%d",
                  idVal->opName(), idVal->id());
          continue;
        }
        if (idVal->isUnbox() && idVal->type() == MIRType::String) {
          idVal = idVal->toUnbox()->input();
          JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys,
                  "- Skipping unbox. idVal is now %s%d", idVal->opName(),
                  idVal->id());
          continue;
        }
        break;
      } while (true);

      if (idVal->isLoadFixedSlot()) {
        JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys,
                "- SUCCESS: Marking fixed slot");
        idVal->toLoadFixedSlot()->setUsedAsPropertyKey();
      } else if (idVal->isLoadDynamicSlot()) {
        JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys,
                "- SUCCESS: Marking dynamic slot");
        idVal->toLoadDynamicSlot()->setUsedAsPropertyKey();
      } else {
        JitSpew(JitSpew_MarkLoadsUsedAsPropertyKeys, "- SKIP: %s not supported",
                idVal->opName());
      }
    }
  }

  return true;
}

enum class CanonicalizeNaN {
  // NaN values must always be first canonicalized for this instruction.
  Yes,

  // Non-canonical NaN values can be passed to this instruction. The instruction
  // never returns a non-canonical NaN value even if some input is a
  // non-canonical NaN value.
  No,

  // Non-canonical NaN values can be passed to this instruction. The instruction
  // may return a non-canonical NaN value if some input is a non-canonical NaN
  // value,
  Propagate,
};

static auto NeedToCanonicalizeNaN(const MDefinition* def) {
  switch (def->op()) {
    case MDefinition::Opcode::Phi:
    case MDefinition::Opcode::Add:
    case MDefinition::Opcode::Sub:
    case MDefinition::Opcode::Mul:
    case MDefinition::Opcode::Div:
    case MDefinition::Opcode::Mod:
    case MDefinition::Opcode::Abs:
    case MDefinition::Opcode::Atan2:
    case MDefinition::Opcode::CopySign:
    case MDefinition::Opcode::Hypot:
    case MDefinition::Opcode::MathFunction:
    case MDefinition::Opcode::MinMax:
    case MDefinition::Opcode::Pow:
    case MDefinition::Opcode::PowHalf:
    case MDefinition::Opcode::Sqrt:
    case MDefinition::Opcode::NearbyInt:
    case MDefinition::Opcode::RoundToDouble:
    case MDefinition::Opcode::ToDouble:
    case MDefinition::Opcode::ToFloat32:
    case MDefinition::Opcode::ToFloat16:
      // These definitions propagate any non-canonical NaN values to their uses.
      MOZ_ASSERT(IsFloatingPointType(def->type()));
      return CanonicalizeNaN::Propagate;

    case MDefinition::Opcode::StoreUnboxedScalar:
    case MDefinition::Opcode::StoreDataViewElement:
    case MDefinition::Opcode::StoreTypedArrayElementHole:
    case MDefinition::Opcode::TypedArrayFill:
      // These definitions accept and can store non-canonical NaN values. They
      // don't return any value.
      MOZ_ASSERT(def->type() == MIRType::None);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::Ceil:
    case MDefinition::Opcode::Floor:
    case MDefinition::Opcode::Round:
    case MDefinition::Opcode::Trunc:
    case MDefinition::Opcode::ClampToUint8:
    case MDefinition::Opcode::ToNumberInt32:
    case MDefinition::Opcode::TruncateToInt32:
    case MDefinition::Opcode::DoubleParseInt:
      // These definitions handle any NaN value, including non-canonical NaNs.
      // They always return an Int32 value.
      MOZ_ASSERT(def->type() == MIRType::Int32);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::GuardNumberToIntPtrIndex:
      // These definitions handle any NaN value, including non-canonical NaNs.
      // They always return an IntPtr value.
      MOZ_ASSERT(def->type() == MIRType::IntPtr);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::ToString:
      // These definitions handle any NaN value, including non-canonical NaNs.
      // They always return a String value.
      MOZ_ASSERT(def->type() == MIRType::String);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::Not:
    case MDefinition::Opcode::Compare:
    case MDefinition::Opcode::SameValueDouble:
      // These definitions handle any NaN value, including non-canonical NaNs.
      // Comparing non-canonical NaN values is handled the same way as comparing
      // any other NaN value. They always return a Boolean value.
      MOZ_ASSERT(def->type() == MIRType::Boolean);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::Test:
      // Similar to MCompare, MTest can handle non-canonical NaN values. MTest
      // doesn't return any value.
      MOZ_ASSERT(def->type() == MIRType::None);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::TableSwitch:
      // MTableSwitch may convert Double to Int32 via `convertDoubleToInt32`,
      // which accepts non-canonical NaN values.
      MOZ_ASSERT(def->type() == MIRType::None);
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::CanonicalizeNaN:
    case MDefinition::Opcode::TimeClip:
    case MDefinition::Opcode::NaNToZero:
      // These definitions explicitly handle NaN values and never return
      // non-canonical NaN values even if the input is a non-canonical NaN
      // value.
      MOZ_ASSERT(IsFloatingPointType(def->type()));
      return CanonicalizeNaN::No;

    case MDefinition::Opcode::Sign:
      // MSign can return non-canonical iff it returns floating point values.
      return IsFloatingPointType(def->type()) ? CanonicalizeNaN::Propagate
                                              : CanonicalizeNaN::No;

    default:
      // Require NaN canonicalization for all other definitions.
      return CanonicalizeNaN::Yes;
  }
}

using PhiList = Vector<MPhi*, 8, SystemAllocPolicy>;

static bool CanonicalizeNaNFor(MIRGraph& graph, PhiList& philist,
                               MInstruction* load) {
  Vector<MDefinition*, 16, SystemAllocPolicy> worklist;
  Vector<MUse*, 8, SystemAllocPolicy> useslist;

  // Append the load instruction to the worklist.
  if (!worklist.append(load)) {
    return false;
  }
  load->setInWorklist();

  while (!worklist.empty()) {
    // This definition can produce a non-canonical NaN value, so all uses have
    // to be inspected to determine if canonicalization is needed.
    auto* def = worklist.popCopy();

    JitSpewDef(JitSpew_CanonicalizeNaN, "Check worklist item\n", def);
    JitSpewIndent spewIndent(JitSpew_CanonicalizeNaN);

    MOZ_ASSERT(useslist.empty());

    for (MUseIterator uses(def->usesBegin()); uses != def->usesEnd();) {
      MUse* use = *uses++;

      // Recovering explicitly canonicalizes NaN values, so we can skip over
      // resume points.
      if (!use->consumer()->isDefinition()) {
        continue;
      }

      MDefinition* consumer = use->consumer()->toDefinition();

      // Recovering canonicalizes NaN values.
      if (consumer->isRecoveredOnBailout()) {
        continue;
      }

      switch (NeedToCanonicalizeNaN(consumer)) {
        case CanonicalizeNaN::Propagate:
          // Either already processed or still in the worklist.
          if (consumer->isInWorklist()) {
            continue;
          }

          JitSpewDef(JitSpew_CanonicalizeNaN, "Add consumer\n", consumer);
          if (!worklist.append(consumer)) {
            return false;
          }
          consumer->setInWorklist();
          break;
        case CanonicalizeNaN::No:
          JitSpewDef(JitSpew_CanonicalizeNaN, "Skip canonicalize for\n",
                     consumer);
          break;
        case CanonicalizeNaN::Yes: {
          JitSpewDef(JitSpew_CanonicalizeNaN, "Canonicalize for\n", consumer);
          MOZ_ASSERT(!consumer->isPhi(),
                     "useslist is expected to contain no phis");

          if (!useslist.append(use)) {
            return false;
          }
          break;
        }
      }
    }

    // No further work necessary when no uses require canonicalization.
    if (useslist.empty()) {
      continue;
    }

    // Are all uses within the same block?
    bool singleUseBlock = true;
    auto* firstUseBlock = useslist[0]->consumer()->block();
    for (size_t i = 1; i < useslist.length(); i++) {
      auto* useBlock = useslist[i]->consumer()->block();
      if (useBlock != firstUseBlock) {
        singleUseBlock = false;
        break;
      }
    }

    // If all uses are within the same block, which is assumed to be the most
    // common case, find a good position where to place the canonicalization
    // instruction.
    //
    // As an extension, dominator tree information could be used to emit a
    // single canonicalization instruction which is shared across multiple
    // blocks. This is not yet implemented.
    if (singleUseBlock) {
      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      auto* canonical = MCanonicalizeNaN::New(graph.alloc(), def);

      if (useslist.length() == 1) {
        // There's only a single use, add the canonicalization directly before
        // the consumer.
        auto* consumer = useslist[0]->consumer()->toDefinition();
        firstUseBlock->insertBefore(consumer->toInstruction(), canonical);
      } else if (firstUseBlock == def->block() && def->isInstruction()) {
        // Uses are within the same block as the definition, add the
        // canonicalization after the definition.
        firstUseBlock->insertAfter(def->toInstruction(), canonical);
      } else {
        // Otherwise add to the start of the block.
        firstUseBlock->insertBefore(firstUseBlock->safeInsertTop(), canonical);
      }

      // Replace the producer with the canonicalized NaN.
      while (!useslist.empty()) {
        auto* use = useslist.popCopy();
        use->replaceProducer(canonical);
      }
      continue;
    }

    // There are multiple uses across different blocks, add the canonicalization
    // after the instruction which can produce non-canonical NaN values.
    //
    // This approach favors emitting a single canonicalization instruction,
    // alteratively each use could get a separate canonicalization instruction.
    if (def->isInstruction()) {
      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      auto* canonical = MCanonicalizeNaN::New(graph.alloc(), def);
      def->block()->insertAfter(def->toInstruction(), canonical);

      // Replace the producer with the canonicalized NaN.
      while (!useslist.empty()) {
        auto* use = useslist.popCopy();
        use->replaceProducer(canonical);
      }
      continue;
    }

    // Fix-up phi nodes at the end.
    if (!philist.append(def->toPhi())) {
      return false;
    }
    useslist.clear();
  }
  return true;
}

static bool CanonicalizeNaNPhis(MIRGraph& graph, PhiList& philist) {
  while (!philist.empty()) {
    auto* phi = philist.popCopy();

    JitSpewDef(JitSpew_CanonicalizeNaN, "Process phi\n", phi);
    JitSpewIndent spewIndent(JitSpew_CanonicalizeNaN);

    for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
      MDefinition* def = phi->getOperand(i);

      // Phi operands which weren't marked above can't produce non-canonical
      // NaN values.
      if (!def->isInWorklist()) {
        JitSpewDef(JitSpew_CanonicalizeNaN, "Skip phi operand\n", def);
        continue;
      }

      JitSpewDef(JitSpew_CanonicalizeNaN, "Handle phi operand\n", def);

      if (!graph.alloc().ensureBallast()) {
        return false;
      }
      auto* canonical = MCanonicalizeNaN::New(graph.alloc(), def);

      auto* pred = phi->block()->getPredecessor(i);
      if (def->block() == pred && def->isInstruction()) {
        // Add canonicalization after the instruction which can produce
        // non-canonical NaN values if possible.
        pred->insertAfter(def->toInstruction(), canonical);
      } else {
        // Otherwise add canonicalization at the end of the block.
        pred->insertAtEnd(canonical);
      }
      phi->replaceOperand(i, canonical);
    }
  }
  return true;
}

bool jit::CanonicalizeNaNAtUses(const MIRGenerator* mir, MIRGraph& graph) {
  // This pass adds MCanonicalizeNaN when possibly non-canonical NaN values flow
  // into instructions which only expect canonical NaN values, like for example
  // MBox or MStoreElement.

  JitSpew(JitSpew_CanonicalizeNaN, "Begin");

  PhiList philist;
  bool hasSeenFloatingPointLoads = false;

  for (MBasicBlock* block : graph) {
    if (mir->shouldCancel("CanonicalizeNaN")) {
      return false;
    }

    for (MInstruction* ins : *block) {
      // Search for instructions which can produce non-canonical NaN values.
      Scalar::Type storageType;
      if (ins->isLoadUnboxedScalar()) {
        storageType = ins->toLoadUnboxedScalar()->storageType();
      } else if (ins->isLoadDataViewElement()) {
        storageType = ins->toLoadDataViewElement()->storageType();
      } else {
        continue;
      }
      if (!Scalar::isFloatingType(storageType)) {
        continue;
      }

      hasSeenFloatingPointLoads = true;

      if (!CanonicalizeNaNFor(graph, philist, ins)) {
        return false;
      }
    }
  }

  // Fix-up phi nodes when we know which phi operands can produce non-canonical
  // NaN values.
  if (!CanonicalizeNaNPhis(graph, philist)) {
    return false;
  }

  if (hasSeenFloatingPointLoads) {
    for (MBasicBlock* block : graph) {
      if (mir->shouldCancel("CanonicalizeNaN (unmark)")) {
        return false;
      }
      for (MPhiIterator phi(block->phisBegin()); phi != block->phisEnd();
           phi++) {
        phi->setNotInWorklistUnchecked();
      }
      for (MInstruction* ins : *block) {
        ins->setNotInWorklistUnchecked();
      }
    }
  }

  return true;
}

static bool NeedsKeepAlive(MInstruction* slotsOrElements, MInstruction* use) {
  MOZ_ASSERT(slotsOrElements->type() == MIRType::Elements ||
             slotsOrElements->type() == MIRType::Slots);

  if (slotsOrElements->block() != use->block()) {
    return true;
  }

  MBasicBlock* block = use->block();
  MInstructionIterator iter(block->begin(slotsOrElements));
  MOZ_ASSERT(*iter == slotsOrElements);
  ++iter;

  while (true) {
    MInstruction* ins = *iter;
    switch (ins->op()) {
      case MDefinition::Opcode::Nop:
      case MDefinition::Opcode::Constant:
      case MDefinition::Opcode::KeepAliveObject:
      case MDefinition::Opcode::Unbox:
      case MDefinition::Opcode::LoadDynamicSlot:
      case MDefinition::Opcode::LoadDynamicSlotAndUnbox:
      case MDefinition::Opcode::StoreDynamicSlot:
      case MDefinition::Opcode::LoadFixedSlot:
      case MDefinition::Opcode::LoadFixedSlotAndUnbox:
      case MDefinition::Opcode::StoreFixedSlot:
      case MDefinition::Opcode::LoadElement:
      case MDefinition::Opcode::LoadElementAndUnbox:
      case MDefinition::Opcode::LoadElementHole:
      case MDefinition::Opcode::StoreElement:
      case MDefinition::Opcode::StoreHoleValueElement:
      case MDefinition::Opcode::LoadUnboxedScalar:
      case MDefinition::Opcode::StoreUnboxedScalar:
      case MDefinition::Opcode::StoreTypedArrayElementHole:
      case MDefinition::Opcode::LoadDataViewElement:
      case MDefinition::Opcode::StoreDataViewElement:
      case MDefinition::Opcode::AtomicTypedArrayElementBinop:
      case MDefinition::Opcode::AtomicExchangeTypedArrayElement:
      case MDefinition::Opcode::CompareExchangeTypedArrayElement:
      case MDefinition::Opcode::InitializedLength:
      case MDefinition::Opcode::SetInitializedLength:
      case MDefinition::Opcode::ArrayLength:
      case MDefinition::Opcode::BoundsCheck:
      case MDefinition::Opcode::GuardElementNotHole:
      case MDefinition::Opcode::GuardElementsArePacked:
      case MDefinition::Opcode::InArray:
      case MDefinition::Opcode::SpectreMaskIndex:
      case MDefinition::Opcode::Add:
      case MDefinition::Opcode::DebugEnterGCUnsafeRegion:
      case MDefinition::Opcode::DebugLeaveGCUnsafeRegion:
        break;
      case MDefinition::Opcode::LoadTypedArrayElementHole: {
        // Allocating a BigInt can GC, so we have to keep the object alive.
        auto* loadIns = ins->toLoadTypedArrayElementHole();
        if (Scalar::isBigIntType(loadIns->arrayType())) {
          return true;
        }
        break;
      }
      default:
        return true;
    }

    if (ins == use) {
      // We didn't find any instructions in range [slotsOrElements, use] that
      // can GC.
      return false;
    }
    iter++;
  }

  MOZ_CRASH("Unreachable");
}

bool jit::AddPostWriteBarriers(MIRGraph& graph) {
  // Insert MPostWriteBarrier or MPostWriteElementBarrier instructions for store
  // instructions that don't have their own post-barrier code.
  //
  // This pass must run after MIR optimization passes that can move instructions
  // between the barrier and the store. This ensures we can't trigger a GC or a
  // bailout between the barrier and the store.

  for (MBasicBlockIterator block(graph.begin()); block != graph.end();
       block++) {
    for (MInstructionIterator insIter(block->begin()); insIter != block->end();
         insIter++) {
      MInstruction* ins = *insIter;

      MDefinition* object = nullptr;
      MDefinition* value = nullptr;
      bool needsBarrier = true;

      // The index operand for element barriers.
      MDefinition* index = nullptr;

      switch (ins->op()) {
        case MDefinition::Opcode::StoreFixedSlot: {
          auto* store = ins->toStoreFixedSlot();
          object = store->object();
          value = store->value();
          needsBarrier = store->needsPostBarrier();
          break;
        }
        case MDefinition::Opcode::StoreFixedSlotFromOffset: {
          auto* store = ins->toStoreFixedSlotFromOffset();
          object = store->object();
          value = store->value();
          break;
        }
        case MDefinition::Opcode::StoreDynamicSlot: {
          auto* store = ins->toStoreDynamicSlot();
          object = store->slots()->toSlots()->object();
          value = store->value();
          needsBarrier = store->needsPostBarrier();
          break;
        }
        case MDefinition::Opcode::StoreDynamicSlotFromOffset: {
          auto* store = ins->toStoreDynamicSlotFromOffset();
          object = store->slots()->toSlots()->object();
          value = store->value();
          break;
        }
        case MDefinition::Opcode::StoreElement: {
          auto* store = ins->toStoreElement();
          object = store->elements()->toElements()->object();
          value = store->value();
          if (store->canUseElementPostBarrier()) {
            index = store->index();
          }
          break;
        }
        case MDefinition::Opcode::AddAndStoreSlot: {
          auto* store = ins->toAddAndStoreSlot();
          object = store->object();
          value = store->value();
          break;
        }
        case MDefinition::Opcode::AllocateAndStoreSlot: {
          auto* store = ins->toAllocateAndStoreSlot();
          object = store->object();
          value = store->value();
          break;
        }
        case MDefinition::Opcode::SetArgumentsObjectArg: {
          auto* store = ins->toSetArgumentsObjectArg();
          object = store->argsObject();
          value = store->value();
          break;
        }
        case MDefinition::Opcode::InitHomeObject: {
          auto* store = ins->toInitHomeObject();
          object = store->function();
          value = store->homeObject();
          break;
        }
        default:
          continue;
      }

      MOZ_ASSERT(object->type() == MIRType::Object);

      if (!ValueNeedsPostBarrier(value)) {
        continue;
      }

      if (!graph.alloc().ensureBallast()) {
        return false;
      }

      if (!needsBarrier) {
#ifdef DEBUG
        // The store claims the barrier can be elided. Assert this.
        if (value->type() != MIRType::Value) {
          auto* box = MBox::New(graph.alloc(), value);
          block->insertBefore(ins, box);
          value = box;
        }
        auto* assert =
            MAssertCanElidePostWriteBarrier::New(graph.alloc(), object, value);
        block->insertBefore(ins, assert);
#endif
        continue;
      }

      if (value->isBox()) {
        value = value->toBox()->input();
      }

      MInstruction* barrier;
      if (index) {
        MOZ_ASSERT(index->type() == MIRType::Int32);
        barrier =
            MPostWriteElementBarrier::New(graph.alloc(), object, value, index);
      } else {
        barrier = MPostWriteBarrier::New(graph.alloc(), object, value);
      }
      block->insertBefore(ins, barrier);
    }
  }

  return true;
}

bool jit::AddKeepAliveInstructions(MIRGraph& graph) {
  for (MBasicBlockIterator i(graph.begin()); i != graph.end(); i++) {
    MBasicBlock* block = *i;

    for (MInstructionIterator insIter(block->begin()); insIter != block->end();
         insIter++) {
      MInstruction* ins = *insIter;
      if (ins->type() != MIRType::Elements && ins->type() != MIRType::Slots) {
        continue;
      }

      MDefinition* ownerObject;
      switch (ins->op()) {
        case MDefinition::Opcode::Elements:
        case MDefinition::Opcode::ArrayBufferViewElements:
          MOZ_ASSERT(ins->numOperands() == 1);
          ownerObject = ins->getOperand(0);
          break;
        case MDefinition::Opcode::Slots:
          ownerObject = ins->toSlots()->object();
          break;
        default:
          MOZ_CRASH("Unexpected op");
      }

      MOZ_ASSERT(ownerObject->type() == MIRType::Object);

      const MDefinition* unwrapped = ownerObject->skipObjectGuards();
      if (unwrapped->isConstant() || unwrapped->isNurseryObject()) {
        // Constants are kept alive by other pointers, for instance ImmGCPtr in
        // JIT code. NurseryObjects will be kept alive by the IonScript.
        continue;
      }

      for (MUseDefIterator uses(ins); uses; uses++) {
        MInstruction* use = uses.def()->toInstruction();

        if (use->isStoreElementHole()) {
          // StoreElementHole has an explicit object operand. If GVN
          // is disabled, we can get different unbox instructions with
          // the same object as input, so we check for that case.
          MOZ_ASSERT_IF(!use->toStoreElementHole()->object()->isUnbox() &&
                            !ownerObject->isUnbox(),
                        use->toStoreElementHole()->object() == ownerObject);
          continue;
        }

        if (!NeedsKeepAlive(ins, use)) {
#ifdef DEBUG
          if (!graph.alloc().ensureBallast()) {
            return false;
          }

          // Enter a GC unsafe region while the elements/slots are on the stack.
          auto* enter = MDebugEnterGCUnsafeRegion::New(graph.alloc());
          use->block()->insertAfter(ins, enter);

          // Leave the region after the use.
          auto* leave = MDebugLeaveGCUnsafeRegion::New(graph.alloc());
          use->block()->insertAfter(use, leave);
#endif
          continue;
        }

        if (!graph.alloc().ensureBallast()) {
          return false;
        }
        MKeepAliveObject* keepAlive =
            MKeepAliveObject::New(graph.alloc(), ownerObject);
        use->block()->insertAfter(use, keepAlive);
      }
    }
  }

  return true;
}

bool LinearSum::multiply(int32_t scale) {
  for (size_t i = 0; i < terms_.length(); i++) {
    if (!mozilla::SafeMul(scale, terms_[i].scale, &terms_[i].scale)) {
      return false;
    }
  }
  return mozilla::SafeMul(scale, constant_, &constant_);
}

bool LinearSum::add(const LinearSum& other, int32_t scale /* = 1 */) {
  for (size_t i = 0; i < other.terms_.length(); i++) {
    int32_t newScale = scale;
    if (!mozilla::SafeMul(scale, other.terms_[i].scale, &newScale)) {
      return false;
    }
    if (!add(other.terms_[i].term, newScale)) {
      return false;
    }
  }
  int32_t newConstant = scale;
  if (!mozilla::SafeMul(scale, other.constant_, &newConstant)) {
    return false;
  }
  return add(newConstant);
}

bool LinearSum::add(MDefinition* term, int32_t scale) {
  MOZ_ASSERT(term);

  if (scale == 0) {
    return true;
  }

  if (MConstant* termConst = term->maybeConstantValue()) {
    int32_t constant = termConst->toInt32();
    if (!mozilla::SafeMul(constant, scale, &constant)) {
      return false;
    }
    return add(constant);
  }

  for (size_t i = 0; i < terms_.length(); i++) {
    if (term == terms_[i].term) {
      if (!mozilla::SafeAdd(scale, terms_[i].scale, &terms_[i].scale)) {
        return false;
      }
      if (terms_[i].scale == 0) {
        terms_[i] = terms_.back();
        terms_.popBack();
      }
      return true;
    }
  }

  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!terms_.emplaceBack(term, scale)) {
    oomUnsafe.crash("LinearSum::add");
  }

  return true;
}

bool LinearSum::add(int32_t constant) {
  return mozilla::SafeAdd(constant, constant_, &constant_);
}

void LinearSum::dump(GenericPrinter& out) const {
  for (size_t i = 0; i < terms_.length(); i++) {
    int32_t scale = terms_[i].scale;
    int32_t id = terms_[i].term->id();
    MOZ_ASSERT(scale);
    if (scale > 0) {
      if (i) {
        out.printf("+");
      }
      if (scale == 1) {
        out.printf("#%d", id);
      } else {
        out.printf("%d*#%d", scale, id);
      }
    } else if (scale == -1) {
      out.printf("-#%d", id);
    } else {
      out.printf("%d*#%d", scale, id);
    }
  }
  if (constant_ > 0) {
    out.printf("+%d", constant_);
  } else if (constant_ < 0) {
    out.printf("%d", constant_);
  }
}

void LinearSum::dump() const {
  Fprinter out(stderr);
  dump(out);
  out.finish();
}

MDefinition* jit::ConvertLinearSum(TempAllocator& alloc, MBasicBlock* block,
                                   const LinearSum& sum,
                                   BailoutKind bailoutKind) {
  MDefinition* def = nullptr;

  for (size_t i = 0; i < sum.numTerms(); i++) {
    LinearTerm term = sum.term(i);
    MOZ_ASSERT(!term.term->isConstant());
    if (term.scale == 1) {
      if (def) {
        def = MAdd::New(alloc, def, term.term, MIRType::Int32);
        def->setBailoutKind(bailoutKind);
        block->insertAtEnd(def->toInstruction());
        def->computeRange(alloc);
      } else {
        def = term.term;
      }
    } else if (term.scale == -1) {
      if (!def) {
        def = MConstant::NewInt32(alloc, 0);
        block->insertAtEnd(def->toInstruction());
        def->computeRange(alloc);
      }
      def = MSub::New(alloc, def, term.term, MIRType::Int32);
      def->setBailoutKind(bailoutKind);
      block->insertAtEnd(def->toInstruction());
      def->computeRange(alloc);
    } else {
      MOZ_ASSERT(term.scale != 0);
      MConstant* factor = MConstant::NewInt32(alloc, term.scale);
      block->insertAtEnd(factor);
      MMul* mul = MMul::New(alloc, term.term, factor, MIRType::Int32);
      mul->setBailoutKind(bailoutKind);
      block->insertAtEnd(mul);
      mul->computeRange(alloc);
      if (def) {
        def = MAdd::New(alloc, def, mul, MIRType::Int32);
        def->setBailoutKind(bailoutKind);
        block->insertAtEnd(def->toInstruction());
        def->computeRange(alloc);
      } else {
        def = mul;
      }
    }
  }

  if (!def) {
    def = MConstant::NewInt32(alloc, 0);
    block->insertAtEnd(def->toInstruction());
    def->computeRange(alloc);
  }

  return def;
}

// Mark all the blocks that are in the loop with the given header.
// Returns the number of blocks marked. Set *canOsr to true if the loop is
// reachable from both the normal entry and the OSR entry.
size_t jit::MarkLoopBlocks(MIRGraph& graph, const MBasicBlock* header,
                           bool* canOsr) {
#ifdef DEBUG
  for (ReversePostorderIterator i = graph.rpoBegin(), e = graph.rpoEnd();
       i != e; ++i) {
    MOZ_ASSERT(!i->isMarked(), "Some blocks already marked");
  }
#endif

  MBasicBlock* osrBlock = graph.osrBlock();
  *canOsr = false;

  // The blocks are in RPO; start at the loop backedge, which marks the bottom
  // of the loop, and walk up until we get to the header. Loops may be
  // discontiguous, so we trace predecessors to determine which blocks are
  // actually part of the loop. The backedge is always part of the loop, and
  // so are its predecessors, transitively, up to the loop header or an OSR
  // entry.
  MBasicBlock* backedge = header->backedge();
  backedge->mark();
  size_t numMarked = 1;
  for (PostorderIterator i = graph.poBegin(backedge);; ++i) {
    MOZ_ASSERT(
        i != graph.poEnd(),
        "Reached the end of the graph while searching for the loop header");
    MBasicBlock* block = *i;
    // If we've reached the loop header, we're done.
    if (block == header) {
      break;
    }
    // A block not marked by the time we reach it is not in the loop.
    if (!block->isMarked()) {
      continue;
    }

    // This block is in the loop; trace to its predecessors.
    for (size_t p = 0, e = block->numPredecessors(); p != e; ++p) {
      MBasicBlock* pred = block->getPredecessor(p);
      if (pred->isMarked()) {
        continue;
      }

      // Blocks dominated by the OSR entry are not part of the loop
      // (unless they aren't reachable from the normal entry).
      if (osrBlock && pred != header && osrBlock->dominates(pred) &&
          !osrBlock->dominates(header)) {
        *canOsr = true;
        continue;
      }

      MOZ_ASSERT(pred->id() >= header->id() && pred->id() <= backedge->id(),
                 "Loop block not between loop header and loop backedge");

      pred->mark();
      ++numMarked;

      // A nested loop may not exit back to the enclosing loop at its
      // bottom. If we just marked its header, then the whole nested loop
      // is part of the enclosing loop.
      if (pred->isLoopHeader()) {
        MBasicBlock* innerBackedge = pred->backedge();
        if (!innerBackedge->isMarked()) {
          // Mark its backedge so that we add all of its blocks to the
          // outer loop as we walk upwards.
          innerBackedge->mark();
          ++numMarked;

          // If the nested loop is not contiguous, we may have already
          // passed its backedge. If this happens, back up.
          if (innerBackedge->id() > block->id()) {
            i = graph.poBegin(innerBackedge);
            --i;
          }
        }
      }
    }
  }

  // If there's no path connecting the header to the backedge, then this isn't
  // actually a loop. This can happen when the code starts with a loop but GVN
  // folds some branches away.
  if (!header->isMarked()) {
    jit::UnmarkLoopBlocks(graph, header);
    return 0;
  }

  return numMarked;
}

// Unmark all the blocks that are in the loop with the given header.
void jit::UnmarkLoopBlocks(MIRGraph& graph, const MBasicBlock* header) {
  MBasicBlock* backedge = header->backedge();
  for (ReversePostorderIterator i = graph.rpoBegin(header);; ++i) {
    MOZ_ASSERT(i != graph.rpoEnd(),
               "Reached the end of the graph while searching for the backedge");
    MBasicBlock* block = *i;
    if (block->isMarked()) {
      block->unmark();
      if (block == backedge) {
        break;
      }
    }
  }

#ifdef DEBUG
  for (ReversePostorderIterator i = graph.rpoBegin(), e = graph.rpoEnd();
       i != e; ++i) {
    MOZ_ASSERT(!i->isMarked(), "Not all blocks got unmarked");
  }
#endif
}

bool jit::FoldLoadsWithUnbox(const MIRGenerator* mir, MIRGraph& graph) {
  // This pass folds MLoadFixedSlot, MLoadDynamicSlot, MLoadElement instructions
  // followed by MUnbox into a single instruction. For LoadElement this allows
  // us to fuse the hole check with the type check for the unbox. It may also
  // allow us to remove some GuardElementsArePacked nodes.

  Vector<MInstruction*, 16, SystemAllocPolicy> optimizedElements;
  for (MBasicBlockIterator block(graph.begin()); block != graph.end();
       block++) {
    if (mir->shouldCancel("FoldLoadsWithUnbox")) {
      return false;
    }

    for (MInstructionIterator insIter(block->begin());
         insIter != block->end();) {
      MInstruction* ins = *insIter;
      insIter++;

      // We're only interested in loads producing a Value.
      if (!ins->isLoadFixedSlot() && !ins->isLoadDynamicSlot() &&
          !ins->isLoadElement() && !ins->isSuperFunction()) {
        continue;
      }
      if (ins->type() != MIRType::Value) {
        continue;
      }

      MInstruction* load = ins;

      // Ensure there's a single def-use (ignoring resume points) and it's an
      // unbox. Unwrap MLexicalCheck because it's redundant if we have a
      // fallible unbox (checked below).
      MDefinition* defUse = load->maybeSingleDefUse();
      if (!defUse) {
        continue;
      }
      MLexicalCheck* lexicalCheck = nullptr;
      if (defUse->isLexicalCheck()) {
        lexicalCheck = defUse->toLexicalCheck();
        defUse = lexicalCheck->maybeSingleDefUse();
        if (!defUse) {
          continue;
        }
      }
      if (!defUse->isUnbox()) {
        continue;
      }

      // For now require the load and unbox to be in the same block. This isn't
      // strictly necessary but it's the common case and could prevent bailouts
      // when moving the unbox before a loop.
      MUnbox* unbox = defUse->toUnbox();
      if (unbox->block() != *block) {
        continue;
      }
      MOZ_ASSERT_IF(lexicalCheck, lexicalCheck->block() == *block);

      MOZ_ASSERT(!IsMagicType(unbox->type()));

      // If this is a LoadElement or if we have a lexical check between the load
      // and unbox, we only support folding the load with a fallible unbox so
      // that we can eliminate the MagicValue check.
      if ((load->isLoadElement() || lexicalCheck) && !unbox->fallible()) {
        continue;
      }

      // If this is a SuperFunction, we only support folding the load when the
      // unbox is fallible and its type is Object.
      //
      // SuperFunction is currently only used for `super()` constructor calls
      // in classes, which always use fallible unbox to Object.
      if (load->isSuperFunction() &&
          !(unbox->type() == MIRType::Object && unbox->fallible())) {
        continue;
      }

      // Combine the load and unbox into a single MIR instruction.
      if (!graph.alloc().ensureBallast()) {
        return false;
      }

      MIRType type = unbox->type();
      MUnbox::Mode mode = unbox->mode();

      MInstruction* replacement;
      switch (load->op()) {
        case MDefinition::Opcode::LoadFixedSlot: {
          auto* loadIns = load->toLoadFixedSlot();
          replacement = MLoadFixedSlotAndUnbox::New(
              graph.alloc(), loadIns->object(), loadIns->slot(), mode, type,
              loadIns->usedAsPropertyKey());
          break;
        }
        case MDefinition::Opcode::LoadDynamicSlot: {
          auto* loadIns = load->toLoadDynamicSlot();
          replacement = MLoadDynamicSlotAndUnbox::New(
              graph.alloc(), loadIns->slots(), loadIns->slot(), mode, type,
              loadIns->usedAsPropertyKey());
          break;
        }
        case MDefinition::Opcode::LoadElement: {
          auto* loadIns = load->toLoadElement();
          MOZ_ASSERT(unbox->fallible());
          replacement = MLoadElementAndUnbox::New(
              graph.alloc(), loadIns->elements(), loadIns->index(), mode, type);
          MOZ_ASSERT(!IsMagicType(type));
          // FoldElementAndUnbox will implicitly check for holes by unboxing. We
          // may be able to remove a GuardElementsArePacked check. Add this
          // Elements to a list to check later (unless we just added it for
          // a different load).
          if ((optimizedElements.empty() ||
               optimizedElements.back() != loadIns) &&
              !optimizedElements.append(loadIns->elements()->toInstruction())) {
            return false;
          }
          break;
        }
        case MDefinition::Opcode::SuperFunction: {
          auto* loadIns = load->toSuperFunction();
          MOZ_ASSERT(unbox->fallible());
          MOZ_ASSERT(unbox->type() == MIRType::Object);
          replacement =
              MSuperFunctionAndUnbox::New(graph.alloc(), loadIns->callee());
          break;
        }
        default:
          MOZ_CRASH("Unexpected instruction");
      }
      replacement->setBailoutKind(BailoutKind::UnboxFolding);

      block->insertBefore(load, replacement);
      unbox->replaceAllUsesWith(replacement);
      if (lexicalCheck) {
        lexicalCheck->replaceAllUsesWith(replacement);
      }
      load->replaceAllUsesWith(replacement);

      if (lexicalCheck && *insIter == lexicalCheck) {
        insIter++;
      }
      if (*insIter == unbox) {
        insIter++;
      }
      block->discard(unbox);
      if (lexicalCheck) {
        block->discard(lexicalCheck);
      }
      block->discard(load);
    }
  }

  // For each Elements that had a load folded with an unbox, check to see if
  // there is a GuardElementsArePacked node that can be removed. It can't be
  // removed if:
  //     1. There is a loadElement/storeElement use that will not emit a
  //        hole check.
  //     2. There is another use that has not been allow-listed.
  // It is safe to add additional operations to the allow list if they don't
  // require a packed Elements array as input.
  for (auto* elements : optimizedElements) {
    bool canRemovePackedChecks = true;
    Vector<MInstruction*, 4, SystemAllocPolicy> guards;
    for (MUseDefIterator uses(elements); uses; uses++) {
      MInstruction* use = uses.def()->toInstruction();
      if (use->isGuardElementsArePacked()) {
        if (!guards.append(use)) {
          return false;
        }
      } else if (use->isLoadElement()) {
        if (!use->toLoadElement()->needsHoleCheck()) {
          canRemovePackedChecks = false;
          break;
        }
      } else if (use->isStoreElement()) {
        if (!use->toStoreElement()->needsHoleCheck()) {
          canRemovePackedChecks = false;
          break;
        }
      } else if (use->isLoadElementAndUnbox() || use->isInitializedLength() ||
                 use->isArrayLength()) {
        // These operations are not affected by the packed flag.
        continue;
      } else {
        canRemovePackedChecks = false;
        break;
      }
    }
    if (!canRemovePackedChecks) {
      continue;
    }
    for (auto* guard : guards) {
      guard->block()->discard(guard);
    }
  }

  return true;
}

// Reorder the blocks in the loop starting at the given header to be contiguous.
static void MakeLoopContiguous(MIRGraph& graph, MBasicBlock* header,
                               size_t numMarked) {
  MBasicBlock* backedge = header->backedge();

  MOZ_ASSERT(header->isMarked(), "Loop header is not part of loop");
  MOZ_ASSERT(backedge->isMarked(), "Loop backedge is not part of loop");

  // If there are any blocks between the loop header and the loop backedge
  // that are not part of the loop, prepare to move them to the end. We keep
  // them in order, which preserves RPO.
  ReversePostorderIterator insertIter = graph.rpoBegin(backedge);
  insertIter++;
  MBasicBlock* insertPt = *insertIter;

  // Visit all the blocks from the loop header to the loop backedge.
  size_t headerId = header->id();
  size_t inLoopId = headerId;
  size_t notInLoopId = inLoopId + numMarked;
  ReversePostorderIterator i = graph.rpoBegin(header);
  for (;;) {
    MBasicBlock* block = *i++;
    MOZ_ASSERT(block->id() >= header->id() && block->id() <= backedge->id(),
               "Loop backedge should be last block in loop");

    if (block->isMarked()) {
      // This block is in the loop.
      block->unmark();
      block->setId(inLoopId++);
      // If we've reached the loop backedge, we're done!
      if (block == backedge) {
        break;
      }
    } else {
      // This block is not in the loop. Move it to the end.
      graph.moveBlockBefore(insertPt, block);
      block->setId(notInLoopId++);
    }
  }
  MOZ_ASSERT(header->id() == headerId, "Loop header id changed");
  MOZ_ASSERT(inLoopId == headerId + numMarked,
             "Wrong number of blocks kept in loop");
  MOZ_ASSERT(notInLoopId == (insertIter != graph.rpoEnd() ? insertPt->id()
                                                          : graph.numBlocks()),
             "Wrong number of blocks moved out of loop");
}

// Reorder the blocks in the graph so that loops are contiguous.
bool jit::MakeLoopsContiguous(MIRGraph& graph) {
  // Visit all loop headers (in any order).
  for (MBasicBlockIterator i(graph.begin()); i != graph.end(); i++) {
    MBasicBlock* header = *i;
    if (!header->isLoopHeader()) {
      continue;
    }

    // Mark all blocks that are actually part of the loop.
    bool canOsr;
    size_t numMarked = MarkLoopBlocks(graph, header, &canOsr);

    // If the loop isn't a loop, don't try to optimize it.
    if (numMarked == 0) {
      continue;
    }

    // If there's an OSR block entering the loop in the middle, it's tricky,
    // so don't try to handle it, for now.
    if (canOsr) {
      UnmarkLoopBlocks(graph, header);
      continue;
    }

    // Move all blocks between header and backedge that aren't marked to
    // the end of the loop, making the loop itself contiguous.
    MakeLoopContiguous(graph, header, numMarked);
  }

  return true;
}

static MDefinition* SkipIterObjectUnbox(MDefinition* ins) {
  if (ins->isGuardIsNotProxy()) {
    ins = ins->toGuardIsNotProxy()->input();
  }
  if (ins->isUnbox()) {
    ins = ins->toUnbox()->input();
  }
  return ins;
}

static MDefinition* SkipBox(MDefinition* ins) {
  if (ins->isBox()) {
    return ins->toBox()->input();
  }
  return ins;
}

static MObjectToIterator* FindObjectToIteratorUse(MDefinition* ins) {
  for (MUseIterator use(ins->usesBegin()); use != ins->usesEnd(); use++) {
    if (!(*use)->consumer()->isDefinition()) {
      continue;
    }
    MDefinition* def = (*use)->consumer()->toDefinition();
    if (def->isGuardIsNotProxy()) {
      MObjectToIterator* recursed = FindObjectToIteratorUse(def);
      if (recursed) {
        return recursed;
      }
    } else if (def->isUnbox()) {
      MObjectToIterator* recursed = FindObjectToIteratorUse(def);
      if (recursed) {
        return recursed;
      }
    } else if (def->isObjectToIterator()) {
      return def->toObjectToIterator();
    }
  }

  return nullptr;
}

static bool IteratorMoreIsUsedInsideLoop(MInstruction* use,
                                         MIteratorMore* iterMore) {
  // We have an IteratorMore node, and an instruction that uses it. We can only
  // optimize that instruction to use the indices stored on that iterator if the
  // use is inside the for-in loop; otherwise, we will have closed the iterator
  // and reset the cursor.
  //
  // To verify this, we walk the path from `use` to `iterMore`, checking for an
  // IteratorEnd node that closes the iterator. There can be more than one such
  // path, but we only have to walk one. The iterator must be closed along any
  // path that leaves the loop. If `use` is outside the loop, then all paths
  // from `iterMore` to `use` must include an IteratorEnd; if it's inside the
  // loop, then no path may include an IteratorEnd. By the nature of an SSA
  // graph, `iterMore` must dominate its uses. Therefore, if we simply walk
  // the CFG by following a non-back-edge predecessor, we are guaranteed to
  // eventually reach the block containing  `iterMore`. If we have not seen
  // an IteratorEnd by that point, then `use` is inside the loop.
  //
  // We don't try to distinguish between IteratorEnd nodes for this iterator
  // and IteratorEnd nodes for some other iterator (for example, the iterator
  // of a nested for-in loop), because reasoning about that is subtle and
  // nested for-in loops are not worth optimizing.

  MBasicBlock* block = use->block();
  MInstructionReverseIterator ins = block->rbegin(use);
  while (true) {
    for (; ins != block->rend(); ins++) {
      if (*ins == iterMore) {
        return true;
      }
      if (ins->isIteratorEnd()) {
        return false;
      }
    }

    // Predecessor 0 of a loop header is the loop predecessor, so following
    // predecessor 0 never walks a back edge.
    MOZ_RELEASE_ASSERT(block->numPredecessors() > 0);
    block = block->getPredecessor(0);
    ins = block->rbegin();
  }
}

bool jit::OptimizeIteratorIndices(const MIRGenerator* mir, MIRGraph& graph) {
  bool changed = false;

  uint32_t numInitialBlocks = graph.numBlockIds();
  auto hasNoDominatorInfo = [&](MBasicBlock* block) {
    return block->id() >= numInitialBlocks;
  };

  for (ReversePostorderIterator blockIter = graph.rpoBegin();
       blockIter != graph.rpoEnd();) {
    MBasicBlock* block = *blockIter++;
    for (MInstructionIterator insIter(block->begin());
         insIter != block->end();) {
      MInstruction* ins = *insIter;
      insIter++;
      if (!graph.alloc().ensureBallast()) {
        return false;
      }

      MDefinition* receiver = nullptr;
      MDefinition* idVal = nullptr;
      MDefinition* setValue = nullptr;
      if (ins->isMegamorphicHasProp() &&
          ins->toMegamorphicHasProp()->hasOwn()) {
        receiver = ins->toMegamorphicHasProp()->object();
        idVal = ins->toMegamorphicHasProp()->idVal();
      } else if (ins->isHasOwnCache()) {
        receiver = ins->toHasOwnCache()->value();
        idVal = ins->toHasOwnCache()->idval();
      } else if (ins->isMegamorphicLoadSlotByValue()) {
        receiver = ins->toMegamorphicLoadSlotByValue()->object();
        idVal = ins->toMegamorphicLoadSlotByValue()->idVal();
      } else if (ins->isMegamorphicLoadSlotByValuePermissive()) {
        receiver = ins->toMegamorphicLoadSlotByValuePermissive()->object();
        idVal = ins->toMegamorphicLoadSlotByValuePermissive()->idVal();
      } else if (ins->isGetPropertyCache()) {
        receiver = ins->toGetPropertyCache()->value();
        idVal = ins->toGetPropertyCache()->idval();
      } else if (ins->isMegamorphicSetElement()) {
        receiver = ins->toMegamorphicSetElement()->object();
        idVal = ins->toMegamorphicSetElement()->index();
        setValue = ins->toMegamorphicSetElement()->value();
      } else if (ins->isSetPropertyCache()) {
        receiver = ins->toSetPropertyCache()->object();
        idVal = ins->toSetPropertyCache()->idval();
        setValue = ins->toSetPropertyCache()->value();
      }

      if (!receiver) {
        continue;
      }

      // Given the following structure (that occurs inside for-in loops or
      // when iterating a scalar-replaced Object.keys result):
      //   obj: some object
      //   iter: ObjectToIterator <obj>
      //   iterLoad: IteratorMore <iter> | LoadIteratorElement <iter, index>
      //   access: HasProp/GetElem <obj> <iterLoad>
      // If the iterator object has an indices array, we can speed up the
      // property access:
      // 1. If the property access is a HasProp looking for own properties,
      //    then the result will always be true if the iterator has indices,
      //    because we only populate the indices array for objects with no
      //    enumerable properties on the prototype.
      // 2. If the property access is a GetProp, then we can use the contents
      //    of the indices array to find the correct property faster than
      //    the megamorphic cache.
      // 3. If the property access is a SetProp, then we can use the contents
      //    of the indices array to find the correct slots faster than the
      //    megamorphic cache.
      //
      // In some cases involving Object.keys, we can also end up with a pattern
      // like this:
      //
      //   obj1: some object
      //   obj2: some object
      //   iter1: ObjectToIterator <obj1>
      //   iter2: ObjectToIterator <obj2>
      //   iterLoad: LoadIteratorElement <iter1>
      //   access: GetElem <obj2> <iterLoad>
      //
      // This corresponds to `obj2[Object.keys(obj1)[index]]`. In the general
      // case we can't do much with this, but if obj1 and obj2 have the same
      // shape, then we may reuse the iterator, in which case iter1 == iter2.
      // In that case, we can optimize the access as if it were using iter2,
      // at the cost of a single comparison to see if iter1 == iter2.
#ifdef JS_CODEGEN_X86
      // The ops required for this want more registers than is convenient on
      // x86
      bool supportObjectKeys = false;
#else
      bool supportObjectKeys = true;
#endif

      MObjectToIterator* iter = nullptr;
      MObjectToIterator* otherIter = nullptr;
      MDefinition* iterElementIndex = nullptr;
      if (idVal->isIteratorMore()) {
        auto* iterNext = idVal->toIteratorMore();
        if (!iterNext->iterator()->isObjectToIterator()) {
          continue;
        }

        iter = iterNext->iterator()->toObjectToIterator();
        if (SkipIterObjectUnbox(iter->object()) !=
            SkipIterObjectUnbox(receiver)) {
          continue;
        }
        if (!IteratorMoreIsUsedInsideLoop(ins, iterNext)) {
          continue;
        }
      } else if (supportObjectKeys && SkipBox(idVal)->isLoadIteratorElement()) {
        auto* iterLoad = SkipBox(idVal)->toLoadIteratorElement();

        if (!iterLoad->iter()->isObjectToIterator()) {
          continue;
        }

        iter = iterLoad->iter()->toObjectToIterator();
        if (SkipIterObjectUnbox(iter->object()) !=
            SkipIterObjectUnbox(receiver)) {
          if (!setValue) {
            otherIter = FindObjectToIteratorUse(SkipIterObjectUnbox(receiver));
          }

          if (!otherIter || hasNoDominatorInfo(block) ||
              hasNoDominatorInfo(otherIter->block()) ||
              !otherIter->dominates(ins)) {
            continue;
          }
        }
        iterElementIndex = iterLoad->index();
      } else {
        continue;
      }

      MOZ_ASSERT_IF(iterElementIndex, supportObjectKeys);
      MOZ_ASSERT_IF(otherIter, supportObjectKeys);

      MInstruction* indicesCheck = nullptr;
      if (otherIter) {
        indicesCheck = MIteratorsMatchAndHaveIndices::New(
            graph.alloc(), otherIter->object(), iter, otherIter);
      } else {
        indicesCheck =
            MIteratorHasIndices::New(graph.alloc(), iter->object(), iter);
      }

      MInstruction* replacement;
      if (ins->isHasOwnCache() || ins->isMegamorphicHasProp()) {
        MOZ_ASSERT(!setValue);
        replacement = MConstant::NewBoolean(graph.alloc(), true);
      } else if (ins->isMegamorphicLoadSlotByValue() ||
                 ins->isMegamorphicLoadSlotByValuePermissive() ||
                 ins->isGetPropertyCache()) {
        MOZ_ASSERT(!setValue);
        if (iterElementIndex) {
          replacement = MLoadSlotByIteratorIndexIndexed::New(
              graph.alloc(), receiver, iter, iterElementIndex);
        } else {
          replacement =
              MLoadSlotByIteratorIndex::New(graph.alloc(), receiver, iter);
        }
      } else {
        MOZ_ASSERT(ins->isMegamorphicSetElement() || ins->isSetPropertyCache());
        MOZ_ASSERT(setValue);
        if (iterElementIndex) {
          replacement = MStoreSlotByIteratorIndexIndexed::New(
              graph.alloc(), receiver, iter, iterElementIndex, setValue);
        } else {
          replacement = MStoreSlotByIteratorIndex::New(graph.alloc(), receiver,
                                                       iter, setValue);
        }
      }

      if (!block->wrapInstructionInFastpath(ins, replacement, indicesCheck)) {
        return false;
      }

      iter->setWantsIndices(true);
      changed = true;

      // Advance to join block.
      blockIter = graph.rpoBegin(block->getSuccessor(0)->getSuccessor(0));
      break;
    }
  }
  if (changed && !AccountForCFGChanges(mir, graph,
                                       /*updateAliasAnalysis=*/false)) {
    return false;
  }

  return true;
}
