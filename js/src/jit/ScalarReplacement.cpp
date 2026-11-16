/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/ScalarReplacement.h"

#include "jit/IonAnalysis.h"
#include "jit/JitSpewer.h"
#include "jit/MIR-wasm.h"
#include "jit/MIR.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"
#include "jit/WarpBuilderShared.h"
#include "js/Vector.h"
#include "vm/ArgumentsObject.h"
#include "vm/BoundFunctionObject.h"
#include "vm/DateObject.h"
#include "vm/TypedArrayObject.h"

#include "gc/ObjectKind-inl.h"

namespace js {
namespace jit {

template <typename MemoryView>
class EmulateStateOf {
 private:
  using BlockState = typename MemoryView::BlockState;

  const MIRGenerator* mir_;
  MIRGraph& graph_;

  // Block state at the entrance of all basic blocks.
  Vector<BlockState*, 8, SystemAllocPolicy> states_;

 public:
  EmulateStateOf(const MIRGenerator* mir, MIRGraph& graph)
      : mir_(mir), graph_(graph) {}

  bool run(MemoryView& view);
};

template <typename MemoryView>
bool EmulateStateOf<MemoryView>::run(MemoryView& view) {
  // Initialize the current block state of each block to an unknown state.
  if (!states_.appendN(nullptr, graph_.numBlocks())) {
    return false;
  }

  // Initialize the first block which needs to be traversed in RPO.
  MBasicBlock* startBlock = view.startingBlock();
  if (!view.initStartingState(&states_[startBlock->id()])) {
    return false;
  }

  // Iterate over each basic block which has a valid entry state, and merge
  // the state in the successor blocks.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel(MemoryView::phaseName)) {
      return false;
    }

    // Get the block state as the result of the merge of all predecessors
    // which have already been visited in RPO.  This means that backedges
    // are not yet merged into the loop.
    BlockState* state = states_[block->id()];
    if (!state) {
      continue;
    }
    view.setEntryBlockState(state);

    // Iterates over resume points, phi and instructions.
    for (MNodeIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the
      // visit function might discard itself from the basic block.
      MNode* ins = *iter++;
      if (ins->isDefinition()) {
        MDefinition* def = ins->toDefinition();
        switch (def->op()) {
#define MIR_OP(op)                 \
  case MDefinition::Opcode::op:    \
    view.visit##op(def->to##op()); \
    break;
          MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
        }
      } else {
        view.visitResumePoint(ins->toResumePoint());
      }
      if (!graph_.alloc().ensureBallast()) {
        return false;
      }
      if (view.oom()) {
        return false;
      }
    }

    // For each successor, merge the current state into the state of the
    // successors.
    for (size_t s = 0; s < block->numSuccessors(); s++) {
      MBasicBlock* succ = block->getSuccessor(s);
      if (!view.mergeIntoSuccessorState(*block, succ, &states_[succ->id()])) {
        return false;
      }
    }
  }

  states_.clear();
  return true;
}

static inline bool IsOptimizableObjectInstruction(MInstruction* ins) {
  return ins->isNewObject() || ins->isNewPlainObject() ||
         ins->isNewCallObject() || ins->isNewIterator() ||
         ins->isNewBoundFunction();
}

static bool PhiOperandEqualTo(MDefinition* operand, MInstruction* newObject) {
  if (operand == newObject) {
    return true;
  }

  switch (operand->op()) {
    case MDefinition::Opcode::GuardShape:
      return PhiOperandEqualTo(operand->toGuardShape()->input(), newObject);

    case MDefinition::Opcode::GuardToClass:
      return PhiOperandEqualTo(operand->toGuardToClass()->input(), newObject);

    case MDefinition::Opcode::GuardBoundFunctionIsConstructor:
      return PhiOperandEqualTo(
          operand->toGuardBoundFunctionIsConstructor()->object(), newObject);

    case MDefinition::Opcode::GuardObjectIdentity:
      return PhiOperandEqualTo(operand->toGuardObjectIdentity()->object(),
                               newObject);

    case MDefinition::Opcode::CheckIsObj:
      return PhiOperandEqualTo(operand->toCheckIsObj()->input(), newObject);

    case MDefinition::Opcode::Unbox:
      return PhiOperandEqualTo(operand->toUnbox()->input(), newObject);

    default:
      return false;
  }
}

// Return true if all phi operands are equal to |newObject|.
static bool PhiOperandsEqualTo(MPhi* phi, MInstruction* newObject) {
  MOZ_ASSERT(IsOptimizableObjectInstruction(newObject));

  for (size_t i = 0, e = phi->numOperands(); i < e; i++) {
    if (!PhiOperandEqualTo(phi->getOperand(i), newObject)) {
      return false;
    }
  }
  return true;
}

static bool IsObjectEscaped(MDefinition* ins, MInstruction* newObject,
                            const Shape* shapeDefault = nullptr);

// Returns False if the lambda is not escaped and if it is optimizable by
// ScalarReplacementOfObject.
static bool IsLambdaEscaped(MInstruction* ins, MInstruction* lambda,
                            MInstruction* newObject, const Shape* shape) {
  MOZ_ASSERT(lambda->isLambda() || lambda->isFunctionWithProto());
  MOZ_ASSERT(IsOptimizableObjectInstruction(newObject));
  JitSpewDef(JitSpew_Escape, "Check lambda\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // The scope chain is not escaped if none of the Lambdas which are
  // capturing it are escaped.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();
    if (!consumer->isDefinition()) {
      // Cannot optimize if it is observable from fun.arguments or others.
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable lambda cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::GuardToFunction: {
        auto* guard = def->toGuardToFunction();
        if (IsLambdaEscaped(guard, lambda, newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardFunctionScript: {
        auto* guard = def->toGuardFunctionScript();
        BaseScript* actual;
        if (lambda->isLambda()) {
          actual = lambda->toLambda()->templateFunction()->baseScript();
        } else {
          actual = lambda->toFunctionWithProto()->function()->baseScript();
        }
        if (actual != guard->expected()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching script guard\n",
                     guard);
          return true;
        }
        if (IsLambdaEscaped(guard, lambda, newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::FunctionEnvironment: {
        if (IsObjectEscaped(def->toFunctionEnvironment(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }
  JitSpew(JitSpew_Escape, "Lambda is not escaped");
  return false;
}

static bool IsLambdaEscaped(MInstruction* lambda, MInstruction* newObject,
                            const Shape* shape) {
  return IsLambdaEscaped(lambda, lambda, newObject, shape);
}

// Returns False if the object is not escaped and if it is optimizable by
// ScalarReplacementOfObject.
//
// For the moment, this code is dumb as it only supports objects which are not
// changing shape.
static bool IsObjectEscaped(MDefinition* ins, MInstruction* newObject,
                            const Shape* shapeDefault) {
  MOZ_ASSERT(ins->type() == MIRType::Object || ins->isPhi());
  MOZ_ASSERT(IsOptimizableObjectInstruction(newObject));

  JitSpewDef(JitSpew_Escape, "Check object\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  const Shape* shape = shapeDefault;
  if (!shape) {
    if (ins->isNewPlainObject()) {
      shape = ins->toNewPlainObject()->shape();
    } else if (JSObject* templateObj = MObjectState::templateObjectOf(ins)) {
      shape = templateObj->shape();
    }
  }

  if (!shape) {
    JitSpew(JitSpew_Escape, "No shape defined.");
    return true;
  }

  MOZ_RELEASE_ASSERT(!shape->isDictionary());

  // Check if the object is escaped. If the object is not the first argument
  // of either a known Store / Load, then we consider it as escaped. This is a
  // cheap and conservative escape analysis.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();
    if (!consumer->isDefinition()) {
      // Cannot optimize if it is observable from fun.arguments or others.
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable object cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::StoreFixedSlot:
      case MDefinition::Opcode::LoadFixedSlot:
      case MDefinition::Opcode::LoadFixedSlotAndUnbox:
        // Not escaped if it is the first argument.
        if (def->indexOf(*i) == 0) {
          break;
        }

        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;

      case MDefinition::Opcode::Slots: {
        // Ensure MSlots is only used by MStoreDynamicSlot and MLoadDynamicSlot.
        MSlots* slots = def->toSlots();
        for (MUseIterator i(slots->usesBegin()); i != slots->usesEnd(); i++) {
          MDefinition* def = (*i)->consumer()->toDefinition();
          if (!def->isLoadDynamicSlot() && !def->isStoreDynamicSlot()) {
            JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
            return true;
          }
          MOZ_ASSERT(def->indexOf(*i) == 0);
        }
        break;
      }

      case MDefinition::Opcode::GuardShape: {
        MGuardShape* guard = def->toGuardShape();
        if (shape != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", guard);
          return true;
        }
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardToClass: {
        MGuardToClass* guard = def->toGuardToClass();
        if (!shape || shape->getObjectClass() != guard->getClass()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching class guard\n", guard);
          return true;
        }
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::CheckIsObj: {
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Lambda:
      case MDefinition::Opcode::FunctionWithProto: {
        if (IsLambdaEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Phi: {
        auto* phi = def->toPhi();
        if (!PhiOperandsEqualTo(phi, newObject)) {
          JitSpewDef(JitSpew_Escape, "has different phi operands\n", def);
          return true;
        }
        if (IsObjectEscaped(phi, newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Compare: {
        bool canFold;
        if (!def->toCompare()->tryFold(&canFold)) {
          JitSpewDef(JitSpew_Escape, "has an unsupported compare\n", def);
          return true;
        }
        break;
      }

      // Doesn't escape the object.
      case MDefinition::Opcode::IsObject:
      case MDefinition::Opcode::BoundFunctionNumArgs:
        break;

      case MDefinition::Opcode::GuardBoundFunctionIsConstructor: {
        auto* guard = def->toGuardBoundFunctionIsConstructor();
        if (!newObject->isNewBoundFunction()) {
          JitSpewDef(JitSpew_Escape, "is not a bound function\n", guard);
          return true;
        }
        JSObject* templateObj = MObjectState::templateObjectOf(newObject);
        if (!templateObj->as<BoundFunctionObject>().isConstructor()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching isConstructor guard\n",
                     guard);
          return true;
        }
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardObjectIdentity: {
        // tryAttachBoundFunction guards newTarget == callee for constructing
        // calls. Both operands are the object we're replacing, so the guard is
        // trivially true and doesn't escape it. Note that either operand can be
        // a chain of guards on that object rather than the object itself.
        auto* guard = def->toGuardObjectIdentity();
        if (guard->bailOnEquality() ||
            !PhiOperandEqualTo(guard->object(), newObject) ||
            !PhiOperandEqualTo(guard->expected(), newObject)) {
          JitSpewDef(JitSpew_Escape, "has a non-trivial identity guard\n",
                     guard);
          return true;
        }
        if (IsObjectEscaped(def->toInstruction(), newObject, shape)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // This instruction is a no-op used to verify that scalar replacement
      // is working as expected in jit-test.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      // This is just a special flavor of constant which lets us optimize
      // out some guards in certain circumstances. We'll turn this into a
      // regular constant later.
      case MDefinition::Opcode::ConstantProto:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Object is not escaped");
  return false;
}

class ObjectMemoryView : public MDefinitionVisitorDefaultNoop {
 public:
  using BlockState = MObjectState;
  static const char phaseName[];

 private:
  TempAllocator& alloc_;
  MConstant* undefinedVal_;
  MInstruction* obj_;
  MBasicBlock* startBlock_;
  BlockState* state_;

  // Used to improve the memory usage by sharing common modification.
  const MResumePoint* lastResumePoint_;

  bool oom_;

 public:
  ObjectMemoryView(TempAllocator& alloc, MInstruction* obj);

  MBasicBlock* startingBlock();
  bool initStartingState(BlockState** outState);

  void setEntryBlockState(BlockState* state);
  bool mergeIntoSuccessorState(MBasicBlock* curr, MBasicBlock* succ,
                               BlockState** pSuccState);

#ifdef DEBUG
  void assertSuccess();
#else
  void assertSuccess() {}
#endif

  bool oom() const { return oom_; }

 private:
  MDefinition* functionForCallObject(MDefinition* ins);

 public:
  void visitResumePoint(MResumePoint* rp);
  void visitObjectState(MObjectState* ins);
  void visitStoreFixedSlot(MStoreFixedSlot* ins);
  void visitLoadFixedSlot(MLoadFixedSlot* ins);
  void visitLoadFixedSlotAndUnbox(MLoadFixedSlotAndUnbox* ins);
  void visitBoundFunctionNumArgs(MBoundFunctionNumArgs* ins);
  void visitGuardBoundFunctionIsConstructor(
      MGuardBoundFunctionIsConstructor* ins);
  void visitGuardObjectIdentity(MGuardObjectIdentity* ins);
  void visitStoreDynamicSlot(MStoreDynamicSlot* ins);
  void visitLoadDynamicSlot(MLoadDynamicSlot* ins);
  void visitGuardShape(MGuardShape* ins);
  void visitGuardToClass(MGuardToClass* ins);
  void visitCheckIsObj(MCheckIsObj* ins);
  void visitUnbox(MUnbox* ins);
  void visitFunctionEnvironment(MFunctionEnvironment* ins);
  void visitGuardToFunction(MGuardToFunction* ins);
  void visitGuardFunctionScript(MGuardFunctionScript* ins);
  void visitLambda(MLambda* ins);
  void visitFunctionWithProto(MFunctionWithProto* ins);
  void visitPhi(MPhi* ins);
  void visitCompare(MCompare* ins);
  void visitConstantProto(MConstantProto* ins);
  void visitIsObject(MIsObject* ins);
};

/* static */ const char ObjectMemoryView::phaseName[] =
    "Scalar Replacement of Object";

ObjectMemoryView::ObjectMemoryView(TempAllocator& alloc, MInstruction* obj)
    : alloc_(alloc),
      undefinedVal_(nullptr),
      obj_(obj),
      startBlock_(obj->block()),
      state_(nullptr),
      lastResumePoint_(nullptr),
      oom_(false) {
  // Annotate snapshots RValue such that we recover the store first.
  obj_->setIncompleteObject();

  // Annotate the instruction such that we do not replace it by a
  // Magic(JS_OPTIMIZED_OUT) in case of removed uses.
  obj_->setImplicitlyUsedUnchecked();
}

MBasicBlock* ObjectMemoryView::startingBlock() { return startBlock_; }

bool ObjectMemoryView::initStartingState(BlockState** outState) {
  // Uninitialized slots have an "undefined" value.
  undefinedVal_ = MConstant::NewUndefined(alloc_);
  startBlock_->insertBefore(obj_, undefinedVal_);

  // Create a new block state and insert at it at the location of the new
  // object.
  BlockState* state = BlockState::New(alloc_, obj_);
  if (!state) {
    return false;
  }

  startBlock_->insertAfter(obj_, state);

  // Initialize the properties of the object state.
  state->initFromTemplateObject(alloc_, undefinedVal_);

  // Hold out of resume point until it is visited.
  state->setInWorklist();

  *outState = state;
  return true;
}

void ObjectMemoryView::setEntryBlockState(BlockState* state) { state_ = state; }

bool ObjectMemoryView::mergeIntoSuccessorState(MBasicBlock* curr,
                                               MBasicBlock* succ,
                                               BlockState** pSuccState) {
  BlockState* succState = *pSuccState;

  // When a block has no state yet, create an empty one for the
  // successor.
  if (!succState) {
    // If the successor is not dominated then the object cannot flow
    // in this basic block without a Phi.  We know that no Phi exist
    // in non-dominated successors as the conservative escaped
    // analysis fails otherwise.  Such condition can succeed if the
    // successor is a join at the end of a if-block and the object
    // only exists within the branch.
    if (!startBlock_->dominates(succ)) {
      return true;
    }

    // If there is only one predecessor, carry over the last state of the
    // block to the successor.  As the block state is immutable, if the
    // current block has multiple successors, they will share the same entry
    // state.
    if (succ->numPredecessors() <= 1 || !state_->numSlots()) {
      *pSuccState = state_;
      return true;
    }

    // If we have multiple predecessors, then we allocate one Phi node for
    // each predecessor, and create a new block state which only has phi
    // nodes.  These would later be removed by the removal of redundant phi
    // nodes.
    succState = BlockState::Copy(alloc_, state_);
    if (!succState) {
      return false;
    }

    size_t numPreds = succ->numPredecessors();
    for (size_t slot = 0; slot < state_->numSlots(); slot++) {
      MPhi* phi = MPhi::New(alloc_.fallible());
      if (!phi || !phi->reserveLength(numPreds)) {
        return false;
      }

      // Fill the input of the successors Phi with undefined
      // values, and each block later fills the Phi inputs.
      for (size_t p = 0; p < numPreds; p++) {
        phi->addInput(undefinedVal_);
      }

      // Add Phi in the list of Phis of the basic block.
      succ->addPhi(phi);
      succState->setSlot(slot, phi);
    }

    // Insert the newly created block state instruction at the beginning
    // of the successor block, after all the phi nodes.  Note that it
    // would be captured by the entry resume point of the successor
    // block.
    succ->insertBefore(succ->safeInsertTop(), succState);
    *pSuccState = succState;
  }

  MOZ_ASSERT_IF(succ == startBlock_, startBlock_->isLoopHeader());
  if (succ->numPredecessors() > 1 && succState->numSlots() &&
      succ != startBlock_) {
    // We need to re-compute successorWithPhis as the previous EliminatePhis
    // phase might have removed all the Phis from the successor block.
    size_t currIndex;
    MOZ_ASSERT(!succ->phisEmpty());
    if (curr->successorWithPhis()) {
      MOZ_ASSERT(curr->successorWithPhis() == succ);
      currIndex = curr->positionInPhiSuccessor();
    } else {
      currIndex = succ->indexForPredecessor(curr);
      curr->setSuccessorWithPhis(succ, currIndex);
    }
    MOZ_ASSERT(succ->getPredecessor(currIndex) == curr);

    // Copy the current slot states to the index of current block in all the
    // Phi created during the first visit of the successor.
    for (size_t slot = 0; slot < state_->numSlots(); slot++) {
      MPhi* phi = succState->getSlot(slot)->toPhi();
      phi->replaceOperand(currIndex, state_->getSlot(slot));
    }
  }

  return true;
}

#ifdef DEBUG
void ObjectMemoryView::assertSuccess() {
  for (MUseIterator i(obj_->usesBegin()); i != obj_->usesEnd(); i++) {
    MNode* ins = (*i)->consumer();
    MDefinition* def = nullptr;

    // Resume points have been replaced by the object state.
    if (ins->isResumePoint() ||
        (def = ins->toDefinition())->isRecoveredOnBailout()) {
      MOZ_ASSERT(obj_->isIncompleteObject());
      continue;
    }

    // The only remaining uses would be removed by DCE, which will also
    // recover the object on bailouts.
    MOZ_ASSERT(def->isSlots() || def->isLambda() || def->isFunctionWithProto());
    MOZ_ASSERT(!def->hasDefUses());
  }
}
#endif

void ObjectMemoryView::visitResumePoint(MResumePoint* rp) {
  // As long as the MObjectState is not yet seen next to the allocation, we do
  // not patch the resume point to recover the side effects.
  if (!state_->isInWorklist()) {
    rp->addStore(alloc_, state_, lastResumePoint_);
    lastResumePoint_ = rp;
  }
}

void ObjectMemoryView::visitObjectState(MObjectState* ins) {
  if (ins->isInWorklist()) {
    ins->setNotInWorklist();
  }
}

void ObjectMemoryView::visitStoreFixedSlot(MStoreFixedSlot* ins) {
  // Skip stores made on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // Clone the state and update the slot value.
  if (state_->hasFixedSlot(ins->slot())) {
    state_ = BlockState::Copy(alloc_, state_);
    if (!state_) {
      oom_ = true;
      return;
    }

    state_->setFixedSlot(ins->slot(), ins->value());
    ins->block()->insertBefore(ins->toInstruction(), state_);
  } else {
    // UnsafeSetReserveSlot can access baked-in slots which are guarded by
    // conditions, which are not seen by the escape analysis.
    MBail* bailout = MBail::New(alloc_, BailoutKind::Inevitable);
    ins->block()->insertBefore(ins, bailout);
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitLoadFixedSlot(MLoadFixedSlot* ins) {
  // Skip loads made on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // Replace load by the slot value.
  if (state_->hasFixedSlot(ins->slot())) {
    ins->replaceAllUsesWith(state_->getFixedSlot(ins->slot()));
  } else {
    // UnsafeGetReserveSlot can access baked-in slots which are guarded by
    // conditions, which are not seen by the escape analysis.
    MBail* bailout = MBail::New(alloc_, BailoutKind::Inevitable);
    ins->block()->insertBefore(ins, bailout);
    ins->replaceAllUsesWith(undefinedVal_);
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitLoadFixedSlotAndUnbox(MLoadFixedSlotAndUnbox* ins) {
  // Skip loads made on other objects.
  if (ins->object() != obj_) {
    return;
  }

  MOZ_ASSERT(state_->hasFixedSlot(ins->slot()));
  MDefinition* val = state_->getFixedSlot(ins->slot());
  if (val->type() == ins->type()) {
    ins->replaceAllUsesWith(val);
  } else {
    auto* unbox = MUnbox::New(alloc_, val, ins->type(), ins->mode());
    ins->block()->insertBefore(ins, unbox);
    ins->replaceAllUsesWith(unbox);
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitBoundFunctionNumArgs(MBoundFunctionNumArgs* ins) {
  // Skip instructions on other objects.
  if (ins->object() != obj_) {
    return;
  }

  JSObject* templateObj = MObjectState::templateObjectOf(obj_);
  size_t numBoundArgs = templateObj->as<BoundFunctionObject>().numBoundArgs();

  auto* num = MConstant::NewInt32(alloc_, int32_t(numBoundArgs));
  ins->block()->insertBefore(ins, num);
  ins->replaceAllUsesWith(num);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardBoundFunctionIsConstructor(
    MGuardBoundFunctionIsConstructor* ins) {
  // Skip guards on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // IsObjectEscaped checked the template object is a constructor.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardObjectIdentity(MGuardObjectIdentity* ins) {
  // Skip guards on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // IsObjectEscaped checked both operands are this object.
  MOZ_ASSERT(ins->expected() == obj_);
  MOZ_ASSERT(!ins->bailOnEquality());
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitStoreDynamicSlot(MStoreDynamicSlot* ins) {
  // Skip stores made on other objects.
  MSlots* slots = ins->slots()->toSlots();
  if (slots->object() != obj_) {
    // Guard objects are replaced when they are visited.
    MOZ_ASSERT(!slots->object()->isGuardShape() ||
               slots->object()->toGuardShape()->object() != obj_);
    return;
  }

  // Clone the state and update the slot value.
  if (state_->hasDynamicSlot(ins->slot())) {
    state_ = BlockState::Copy(alloc_, state_);
    if (!state_) {
      oom_ = true;
      return;
    }

    state_->setDynamicSlot(ins->slot(), ins->value());
    ins->block()->insertBefore(ins->toInstruction(), state_);
  } else {
    // UnsafeSetReserveSlot can access baked-in slots which are guarded by
    // conditions, which are not seen by the escape analysis.
    MBail* bailout = MBail::New(alloc_, BailoutKind::Inevitable);
    ins->block()->insertBefore(ins, bailout);
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitLoadDynamicSlot(MLoadDynamicSlot* ins) {
  // Skip loads made on other objects.
  MSlots* slots = ins->slots()->toSlots();
  if (slots->object() != obj_) {
    // Guard objects are replaced when they are visited.
    MOZ_ASSERT(!slots->object()->isGuardShape() ||
               slots->object()->toGuardShape()->object() != obj_);
    return;
  }

  // Replace load by the slot value.
  if (state_->hasDynamicSlot(ins->slot())) {
    ins->replaceAllUsesWith(state_->getDynamicSlot(ins->slot()));
  } else {
    // UnsafeGetReserveSlot can access baked-in slots which are guarded by
    // conditions, which are not seen by the escape analysis.
    MBail* bailout = MBail::New(alloc_, BailoutKind::Inevitable);
    ins->block()->insertBefore(ins, bailout);
    ins->replaceAllUsesWith(undefinedVal_);
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardShape(MGuardShape* ins) {
  // Skip guards on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // Replace the guard by its object.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardToClass(MGuardToClass* ins) {
  // Skip guards on other objects.
  if (ins->object() != obj_) {
    return;
  }

  // Replace the guard by its object.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitCheckIsObj(MCheckIsObj* ins) {
  // Skip checks on other objects.
  if (ins->input() != obj_) {
    return;
  }

  // Replace the check by its object.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitUnbox(MUnbox* ins) {
  // Skip unrelated unboxes.
  if (ins->input() != obj_) {
    return;
  }
  MOZ_ASSERT(ins->type() == MIRType::Object);

  // Replace the unbox with the object.
  ins->replaceAllUsesWith(obj_);

  // Remove the unbox.
  ins->block()->discard(ins);
}

MDefinition* ObjectMemoryView::functionForCallObject(MDefinition* ins) {
  // Return early when we don't replace MNewCallObject.
  if (!obj_->isNewCallObject()) {
    return nullptr;
  }

  // Unwrap instructions until we found either MLambda or MFunctionWithProto.
  // Return the function instruction if their environment chain matches the
  // MNewCallObject we're about to replace.
  while (true) {
    switch (ins->op()) {
      case MDefinition::Opcode::Lambda: {
        if (ins->toLambda()->environmentChain() == obj_) {
          return ins;
        }
        return nullptr;
      }
      case MDefinition::Opcode::FunctionWithProto: {
        if (ins->toFunctionWithProto()->environmentChain() == obj_) {
          return ins;
        }
        return nullptr;
      }
      case MDefinition::Opcode::FunctionEnvironment:
        ins = ins->toFunctionEnvironment()->function();
        break;
      case MDefinition::Opcode::GuardToFunction:
        ins = ins->toGuardToFunction()->object();
        break;
      case MDefinition::Opcode::GuardFunctionScript:
        ins = ins->toGuardFunctionScript()->function();
        break;
      default:
        return nullptr;
    }
  }
}

void ObjectMemoryView::visitFunctionEnvironment(MFunctionEnvironment* ins) {
  // Skip function environment which are not aliases of the NewCallObject.
  if (!functionForCallObject(ins)) {
    return;
  }

  // Replace the function environment by the scope chain of the lambda.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardToFunction(MGuardToFunction* ins) {
  // Skip guards on other objects.
  auto* function = functionForCallObject(ins);
  if (!function) {
    return;
  }

  // Replace the guard by its object.
  ins->replaceAllUsesWith(function);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitGuardFunctionScript(MGuardFunctionScript* ins) {
  // Skip guards on other objects.
  auto* function = functionForCallObject(ins);
  if (!function) {
    return;
  }

  // Replace the guard by its object.
  ins->replaceAllUsesWith(function);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitLambda(MLambda* ins) {
  if (ins->environmentChain() != obj_) {
    return;
  }

  // In order to recover the lambda we need to recover the scope chain, as the
  // lambda is holding it.
  ins->setIncompleteObject();
}

void ObjectMemoryView::visitFunctionWithProto(MFunctionWithProto* ins) {
  if (ins->environmentChain() != obj_) {
    return;
  }

  ins->setIncompleteObject();
}

void ObjectMemoryView::visitPhi(MPhi* ins) {
  // Skip phis on other objects.
  if (!PhiOperandsEqualTo(ins, obj_)) {
    return;
  }

  // Replace the phi by its object.
  ins->replaceAllUsesWith(obj_);

  // Remove original instruction.
  ins->block()->discardPhi(ins);
}

void ObjectMemoryView::visitCompare(MCompare* ins) {
  // Skip unrelated comparisons.
  if (ins->lhs() != obj_ && ins->rhs() != obj_) {
    return;
  }

  bool folded;
  MOZ_ALWAYS_TRUE(ins->tryFold(&folded));

  auto* cst = MConstant::NewBoolean(alloc_, folded);
  ins->block()->insertBefore(ins, cst);

  // Replace the comparison with a constant.
  ins->replaceAllUsesWith(cst);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitConstantProto(MConstantProto* ins) {
  if (ins->getReceiverObject() != obj_) {
    return;
  }

  auto* cst = ins->protoObject();
  ins->replaceAllUsesWith(cst);
  ins->block()->discard(ins);
}

void ObjectMemoryView::visitIsObject(MIsObject* ins) {
  // Skip unrelated tests.
  if (ins->input() != obj_) {
    return;
  }

  auto* cst = MConstant::NewBoolean(alloc_, true);
  ins->block()->insertBefore(ins, cst);

  // Replace the test with a constant.
  ins->replaceAllUsesWith(cst);

  // Remove original instruction.
  ins->block()->discard(ins);
}

static bool IndexOf(MDefinition* ins, int32_t* res) {
  MOZ_ASSERT(ins->isLoadElement() || ins->isStoreElement());
  MDefinition* indexDef = ins->getOperand(1);  // ins->index();
  if (indexDef->isSpectreMaskIndex()) {
    indexDef = indexDef->toSpectreMaskIndex()->index();
  }
  if (indexDef->isBoundsCheck()) {
    indexDef = indexDef->toBoundsCheck()->index();
  }
  if (indexDef->isToNumberInt32()) {
    indexDef = indexDef->toToNumberInt32()->getOperand(0);
  }
  MConstant* indexDefConst = indexDef->maybeConstantValue();
  if (!indexDefConst || indexDefConst->type() != MIRType::Int32) {
    return false;
  }
  *res = indexDefConst->toInt32();
  return true;
}

static inline bool IsOptimizableArrayInstruction(MInstruction* ins) {
  return ins->isNewArray() || ins->isNewArrayObject();
}

// We don't support storing holes when doing scalar replacement, so any
// optimizable MNewArrayObject instruction is guaranteed to be packed.
static inline bool IsPackedArray(MInstruction* ins) {
  return ins->isNewArrayObject();
}

// Returns False if the elements is not escaped and if it is optimizable by
// ScalarReplacementOfArray.
static bool IsElementEscaped(MDefinition* def, MInstruction* newArray,
                             uint32_t arraySize) {
  MOZ_ASSERT(def->isElements());
  MOZ_ASSERT(IsOptimizableArrayInstruction(newArray));

  JitSpewDef(JitSpew_Escape, "Check elements\n", def);
  JitSpewIndent spewIndent(JitSpew_Escape);

  for (MUseIterator i(def->usesBegin()); i != def->usesEnd(); i++) {
    // The MIRType::Elements cannot be captured in a resume point as
    // it does not represent a value allocation.
    MDefinition* access = (*i)->consumer()->toDefinition();

    switch (access->op()) {
      case MDefinition::Opcode::LoadElement: {
        MOZ_ASSERT(access->toLoadElement()->elements() == def);

        // If the index is not a constant then this index can alias
        // all others. We do not handle this case.
        int32_t index;
        if (!IndexOf(access, &index)) {
          JitSpewDef(JitSpew_Escape,
                     "has a load element with a non-trivial index\n", access);
          return true;
        }
        if (index < 0 || arraySize <= uint32_t(index)) {
          JitSpewDef(JitSpew_Escape,
                     "has a load element with an out-of-bound index\n", access);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::StoreElement: {
        MStoreElement* storeElem = access->toStoreElement();
        MOZ_ASSERT(storeElem->elements() == def);

        // StoreElement must bail out if it stores to a hole, in case
        // there is a setter on the prototype chain. If this StoreElement
        // might store to a hole, we can't scalar-replace it.
        if (storeElem->needsHoleCheck()) {
          JitSpewDef(JitSpew_Escape, "has a store element with a hole check\n",
                     storeElem);
          return true;
        }

        // If the index is not a constant then this index can alias
        // all others. We do not handle this case.
        int32_t index;
        if (!IndexOf(storeElem, &index)) {
          JitSpewDef(JitSpew_Escape,
                     "has a store element with a non-trivial index\n",
                     storeElem);
          return true;
        }
        if (index < 0 || arraySize <= uint32_t(index)) {
          JitSpewDef(JitSpew_Escape,
                     "has a store element with an out-of-bound index\n",
                     storeElem);
          return true;
        }

        // Dense element holes are written using MStoreHoleValueElement instead
        // of MStoreElement.
        MOZ_ASSERT(storeElem->value()->type() != MIRType::MagicHole);
        break;
      }

      case MDefinition::Opcode::SetInitializedLength:
        MOZ_ASSERT(access->toSetInitializedLength()->elements() == def);
        break;

      case MDefinition::Opcode::InitializedLength:
        MOZ_ASSERT(access->toInitializedLength()->elements() == def);
        break;

      case MDefinition::Opcode::ArrayLength:
        MOZ_ASSERT(access->toArrayLength()->elements() == def);
        break;

      case MDefinition::Opcode::ApplyArray:
        MOZ_ASSERT(access->toApplyArray()->getElements() == def);
        if (!IsPackedArray(newArray)) {
          JitSpewDef(JitSpew_Escape, "is not guaranteed to be packed\n",
                     access);
          return true;
        }
        break;

      case MDefinition::Opcode::ConstructArray:
        MOZ_ASSERT(access->toConstructArray()->getElements() == def);
        if (!IsPackedArray(newArray)) {
          JitSpewDef(JitSpew_Escape, "is not guaranteed to be packed\n",
                     access);
          return true;
        }
        break;

      case MDefinition::Opcode::GuardElementsArePacked:
        MOZ_ASSERT(access->toGuardElementsArePacked()->elements() == def);
        if (!IsPackedArray(newArray)) {
          JitSpewDef(JitSpew_Escape, "is not guaranteed to be packed\n",
                     access);
          return true;
        }
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", access);
        return true;
    }
  }
  JitSpew(JitSpew_Escape, "Elements is not escaped");
  return false;
}

// Returns False if the array is not escaped and if it is optimizable by
// ScalarReplacementOfArray.
//
// For the moment, this code is dumb as it only supports arrays which are not
// changing length, with only access with known constants.
static bool IsArrayEscaped(MInstruction* ins, MInstruction* newArray) {
  MOZ_ASSERT(ins->type() == MIRType::Object);
  MOZ_ASSERT(IsOptimizableArrayInstruction(newArray));

  JitSpewDef(JitSpew_Escape, "Check array\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  const Shape* shape;
  uint32_t length;
  if (newArray->isNewArrayObject()) {
    length = newArray->toNewArrayObject()->length();
    shape = newArray->toNewArrayObject()->shape();
  } else {
    length = newArray->toNewArray()->length();
    JSObject* templateObject = newArray->toNewArray()->templateObject();
    if (!templateObject) {
      JitSpew(JitSpew_Escape, "No template object defined.");
      return true;
    }
    shape = templateObject->shape();
  }

  if (length >= 16) {
    JitSpew(JitSpew_Escape, "Array has too many elements");
    return true;
  }

  // Check if the object is escaped. If the object is not the first argument
  // of either a known Store / Load, then we consider it as escaped. This is a
  // cheap and conservative escape analysis.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();
    if (!consumer->isDefinition()) {
      // Cannot optimize if it is observable from fun.arguments or others.
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable array cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::Elements: {
        MElements* elem = def->toElements();
        MOZ_ASSERT(elem->object() == ins);
        if (IsElementEscaped(elem, newArray, length)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", elem);
          return true;
        }

        break;
      }

      case MDefinition::Opcode::GuardShape: {
        MGuardShape* guard = def->toGuardShape();
        if (shape != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", guard);
          return true;
        }
        if (IsArrayEscaped(guard, newArray)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }

        break;
      }

      case MDefinition::Opcode::GuardToClass: {
        MGuardToClass* guard = def->toGuardToClass();
        if (shape->getObjectClass() != guard->getClass()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching class guard\n", guard);
          return true;
        }
        if (IsArrayEscaped(guard, newArray)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }

        break;
      }

      case MDefinition::Opcode::GuardArrayIsPacked: {
        auto* guard = def->toGuardArrayIsPacked();
        if (!IsPackedArray(newArray)) {
          JitSpewDef(JitSpew_Escape, "is not guaranteed to be packed\n", def);
          return true;
        }
        if (IsArrayEscaped(guard, newArray)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (IsArrayEscaped(def->toInstruction(), newArray)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // This instruction is supported for |JSOp::OptimizeSpreadCall|.
      case MDefinition::Opcode::Compare: {
        bool canFold;
        if (!def->toCompare()->tryFold(&canFold)) {
          JitSpewDef(JitSpew_Escape, "has an unsupported compare\n", def);
          return true;
        }
        break;
      }

      // This instruction is a no-op used to verify that scalar replacement
      // is working as expected in jit-test.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Array is not escaped");
  return false;
}

// This is just a class designed to extract the common elements across
// several different Array replacement strategies to avoid code duplication.
// There is nothing essential or sacred about it, it just felt like this
// was some pretty basic stuff we often want to do when we're replacing a
// true JS array with something which cheaply approximates it. When
// inheriting from this in the future, please validate that each of its
// core visit functions is safe to do in your new context.
class GenericArrayReplacer : public MDefinitionVisitorDefaultNoop {
 protected:
  TempAllocator& alloc_;
  MInstruction* arr_;

  bool isTargetElements(MDefinition* elements);
  void discardInstruction(MInstruction* ins, MDefinition* elements);
  void visitLength(MInstruction* ins, MDefinition* elements);

  GenericArrayReplacer(TempAllocator& alloc, MInstruction* arr)
      : alloc_(alloc), arr_(arr) {}

 public:
  void visitGuardToClass(MGuardToClass* ins);
  void visitGuardShape(MGuardShape* ins);
  void visitGuardArrayIsPacked(MGuardArrayIsPacked* ins);
  void visitUnbox(MUnbox* ins);
  void visitCompare(MCompare* ins);
  void visitGuardElementsArePacked(MGuardElementsArePacked* ins);
};

bool GenericArrayReplacer::isTargetElements(MDefinition* elements) {
  return elements->isElements() && elements->toElements()->object() == arr_;
}

void GenericArrayReplacer::discardInstruction(MInstruction* ins,
                                              MDefinition* elements) {
  MOZ_ASSERT(elements->isElements());
  ins->block()->discard(ins);
  if (!elements->hasLiveDefUses()) {
    elements->block()->discard(elements->toInstruction());
  }
}

void GenericArrayReplacer::visitGuardToClass(MGuardToClass* ins) {
  // Skip guards on other objects.
  if (ins->object() != arr_) {
    return;
  }
  MOZ_ASSERT(ins->getClass() == &ArrayObject::class_);

  // Replace the guard with the array object.
  ins->replaceAllUsesWith(arr_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void GenericArrayReplacer::visitGuardShape(MGuardShape* ins) {
  // Skip guards on other objects.
  if (ins->object() != arr_) {
    return;
  }

  // Replace the guard with the array object.
  ins->replaceAllUsesWith(arr_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void GenericArrayReplacer::visitGuardArrayIsPacked(MGuardArrayIsPacked* ins) {
  // Skip guards on other objects.
  if (ins->array() != arr_) {
    return;
  }

  // Replace the guard by its object.
  ins->replaceAllUsesWith(arr_);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void GenericArrayReplacer::visitUnbox(MUnbox* ins) {
  // Skip unrelated unboxes.
  if (ins->input() != arr_) {
    return;
  }
  MOZ_ASSERT(ins->type() == MIRType::Object);

  // Replace the unbox with the array object.
  ins->replaceAllUsesWith(arr_);

  // Remove the unbox.
  ins->block()->discard(ins);
}

void GenericArrayReplacer::visitCompare(MCompare* ins) {
  // Skip unrelated comparisons.
  if (ins->lhs() != arr_ && ins->rhs() != arr_) {
    return;
  }

  bool folded;
  MOZ_ALWAYS_TRUE(ins->tryFold(&folded));

  auto* cst = MConstant::NewBoolean(alloc_, folded);
  ins->block()->insertBefore(ins, cst);

  // Replace the comparison with a constant.
  ins->replaceAllUsesWith(cst);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void GenericArrayReplacer::visitGuardElementsArePacked(
    MGuardElementsArePacked* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isTargetElements(elements)) {
    return;
  }

  // Remove original instruction.
  discardInstruction(ins, elements);
}

// This class replaces every MStoreElement and MSetInitializedLength by an
// MArrayState which emulates the content of the array. All MLoadElement,
// MInitializedLength and MArrayLength are replaced by the corresponding value.
//
// In order to restore the value of the array correctly in case of bailouts, we
// replace all reference of the allocation by the MArrayState definition.
class ArrayMemoryView : public GenericArrayReplacer {
 public:
  using BlockState = MArrayState;
  static const char* phaseName;

 private:
  MConstant* undefinedVal_;
  MConstant* length_;
  MBasicBlock* startBlock_;
  BlockState* state_;

  // Used to improve the memory usage by sharing common modification.
  const MResumePoint* lastResumePoint_;

  bool oom_;

 public:
  ArrayMemoryView(TempAllocator& alloc, MInstruction* arr);

  MBasicBlock* startingBlock();
  bool initStartingState(BlockState** pState);

  void setEntryBlockState(BlockState* state);
  bool mergeIntoSuccessorState(MBasicBlock* curr, MBasicBlock* succ,
                               BlockState** pSuccState);

#ifdef DEBUG
  void assertSuccess();
#else
  void assertSuccess() {}
#endif

  bool oom() const { return oom_; }

 private:
  bool isArrayStateElements(MDefinition* elements);
  void discardInstruction(MInstruction* ins, MDefinition* elements);

 public:
  void visitResumePoint(MResumePoint* rp);
  void visitArrayState(MArrayState* ins);
  void visitStoreElement(MStoreElement* ins);
  void visitLoadElement(MLoadElement* ins);
  void visitSetInitializedLength(MSetInitializedLength* ins);
  void visitInitializedLength(MInitializedLength* ins);
  void visitArrayLength(MArrayLength* ins);
  void visitApplyArray(MApplyArray* ins);
  void visitConstructArray(MConstructArray* ins);
};

const char* ArrayMemoryView::phaseName = "Scalar Replacement of Array";

ArrayMemoryView::ArrayMemoryView(TempAllocator& alloc, MInstruction* arr)
    : GenericArrayReplacer(alloc, arr),
      undefinedVal_(nullptr),
      length_(nullptr),
      startBlock_(arr->block()),
      state_(nullptr),
      lastResumePoint_(nullptr),
      oom_(false) {
  // Annotate snapshots RValue such that we recover the store first.
  arr_->setIncompleteObject();

  // Annotate the instruction such that we do not replace it by a
  // Magic(JS_OPTIMIZED_OUT) in case of removed uses.
  arr_->setImplicitlyUsedUnchecked();
}

MBasicBlock* ArrayMemoryView::startingBlock() { return startBlock_; }

bool ArrayMemoryView::initStartingState(BlockState** pState) {
  // Uninitialized elements have an "undefined" value.
  undefinedVal_ = MConstant::NewUndefined(alloc_);
  MConstant* initLength = MConstant::NewInt32(alloc_, 0);
  arr_->block()->insertBefore(arr_, undefinedVal_);
  arr_->block()->insertBefore(arr_, initLength);

  // Create a new block state and insert at it at the location of the new array.
  BlockState* state = BlockState::New(alloc_, arr_, initLength);
  if (!state) {
    return false;
  }

  startBlock_->insertAfter(arr_, state);

  // Initialize the elements of the array state.
  state->initFromTemplateObject(alloc_, undefinedVal_);

  // Hold out of resume point until it is visited.
  state->setInWorklist();

  *pState = state;
  return true;
}

void ArrayMemoryView::setEntryBlockState(BlockState* state) { state_ = state; }

bool ArrayMemoryView::mergeIntoSuccessorState(MBasicBlock* curr,
                                              MBasicBlock* succ,
                                              BlockState** pSuccState) {
  BlockState* succState = *pSuccState;

  // When a block has no state yet, create an empty one for the
  // successor.
  if (!succState) {
    // If the successor is not dominated then the array cannot flow
    // in this basic block without a Phi.  We know that no Phi exist
    // in non-dominated successors as the conservative escaped
    // analysis fails otherwise.  Such condition can succeed if the
    // successor is a join at the end of a if-block and the array
    // only exists within the branch.
    if (!startBlock_->dominates(succ)) {
      return true;
    }

    // If there is only one predecessor, carry over the last state of the
    // block to the successor.  As the block state is immutable, if the
    // current block has multiple successors, they will share the same entry
    // state.
    if (succ->numPredecessors() <= 1 || !state_->numElements()) {
      *pSuccState = state_;
      return true;
    }

    // If we have multiple predecessors, then we allocate one Phi node for
    // each predecessor, and create a new block state which only has phi
    // nodes.  These would later be removed by the removal of redundant phi
    // nodes.
    succState = BlockState::Copy(alloc_, state_);
    if (!succState) {
      return false;
    }

    size_t numPreds = succ->numPredecessors();
    for (size_t index = 0; index < state_->numElements(); index++) {
      MPhi* phi = MPhi::New(alloc_.fallible());
      if (!phi || !phi->reserveLength(numPreds)) {
        return false;
      }

      // Fill the input of the successors Phi with undefined
      // values, and each block later fills the Phi inputs.
      for (size_t p = 0; p < numPreds; p++) {
        phi->addInput(undefinedVal_);
      }

      // Add Phi in the list of Phis of the basic block.
      succ->addPhi(phi);
      succState->setElement(index, phi);
    }

    // Insert the newly created block state instruction at the beginning
    // of the successor block, after all the phi nodes.  Note that it
    // would be captured by the entry resume point of the successor
    // block.
    succ->insertBefore(succ->safeInsertTop(), succState);
    *pSuccState = succState;
  }

  MOZ_ASSERT_IF(succ == startBlock_, startBlock_->isLoopHeader());
  if (succ->numPredecessors() > 1 && succState->numElements() &&
      succ != startBlock_) {
    // We need to re-compute successorWithPhis as the previous EliminatePhis
    // phase might have removed all the Phis from the successor block.
    size_t currIndex;
    MOZ_ASSERT(!succ->phisEmpty());
    if (curr->successorWithPhis()) {
      MOZ_ASSERT(curr->successorWithPhis() == succ);
      currIndex = curr->positionInPhiSuccessor();
    } else {
      currIndex = succ->indexForPredecessor(curr);
      curr->setSuccessorWithPhis(succ, currIndex);
    }
    MOZ_ASSERT(succ->getPredecessor(currIndex) == curr);

    // Copy the current element states to the index of current block in all
    // the Phi created during the first visit of the successor.
    for (size_t index = 0; index < state_->numElements(); index++) {
      MPhi* phi = succState->getElement(index)->toPhi();
      phi->replaceOperand(currIndex, state_->getElement(index));
    }
  }

  return true;
}

#ifdef DEBUG
void ArrayMemoryView::assertSuccess() { MOZ_ASSERT(!arr_->hasLiveDefUses()); }
#endif

void ArrayMemoryView::visitResumePoint(MResumePoint* rp) {
  // As long as the MArrayState is not yet seen next to the allocation, we do
  // not patch the resume point to recover the side effects.
  if (!state_->isInWorklist()) {
    rp->addStore(alloc_, state_, lastResumePoint_);
    lastResumePoint_ = rp;
  }
}

void ArrayMemoryView::visitArrayState(MArrayState* ins) {
  if (ins->isInWorklist()) {
    ins->setNotInWorklist();
  }
}

bool ArrayMemoryView::isArrayStateElements(MDefinition* elements) {
  return elements->isElements() && elements->toElements()->object() == arr_;
}

void ArrayMemoryView::discardInstruction(MInstruction* ins,
                                         MDefinition* elements) {
  MOZ_ASSERT(elements->isElements());
  ins->block()->discard(ins);
  if (!elements->hasLiveDefUses()) {
    elements->block()->discard(elements->toInstruction());
  }
}

void ArrayMemoryView::visitStoreElement(MStoreElement* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  // Register value of the setter in the state.
  int32_t index;
  MOZ_ALWAYS_TRUE(IndexOf(ins, &index));
  state_ = BlockState::Copy(alloc_, state_);
  if (!state_) {
    oom_ = true;
    return;
  }

  state_->setElement(index, ins->value());
  ins->block()->insertBefore(ins, state_);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitLoadElement(MLoadElement* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  // Replace by the value contained at the index.
  int32_t index;
  MOZ_ALWAYS_TRUE(IndexOf(ins, &index));

  // The only way to store a hole value in a new array is with
  // StoreHoleValueElement, which IsElementEscaped does not allow.
  // Therefore, we do not have to do a hole check.
  MDefinition* element = state_->getElement(index);
  MOZ_ASSERT(element->type() != MIRType::MagicHole);

  ins->replaceAllUsesWith(element);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitSetInitializedLength(MSetInitializedLength* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  // Replace by the new initialized length.
  state_ = BlockState::Copy(alloc_, state_);
  if (!state_) {
    oom_ = true;
    return;
  }

  MConstant* initLength = MConstant::NewInt32(alloc_, ins->length());
  ins->block()->insertBefore(ins, initLength);
  ins->block()->insertBefore(ins, state_);
  state_->setInitializedLength(initLength);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitInitializedLength(MInitializedLength* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  // Replace by the value of the length.
  ins->replaceAllUsesWith(state_->initializedLength());

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitArrayLength(MArrayLength* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  // Replace by the value of the length.
  if (!length_) {
    length_ = MConstant::NewInt32(alloc_, state_->numElements());
    arr_->block()->insertBefore(arr_, length_);
  }
  ins->replaceAllUsesWith(length_);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitApplyArray(MApplyArray* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->getElements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  uint32_t numElements = state_->numElements();

  CallInfo callInfo(alloc_, /*constructing=*/false, ins->ignoresReturnValue());
  if (!callInfo.initForApplyArray(ins->getFunction(), ins->getThis(),
                                  numElements)) {
    oom_ = true;
    return;
  }

  for (uint32_t i = 0; i < numElements; i++) {
    auto* element = state_->getElement(i);
    MOZ_ASSERT(element->type() != MIRType::MagicHole);

    callInfo.initArg(i, element);
  }

  auto addUndefined = [this]() { return undefinedVal_; };

  bool needsThisCheck = false;
  bool isDOMCall = false;
  auto* call = MakeCall(alloc_, addUndefined, callInfo, needsThisCheck,
                        ins->getSingleTarget(), isDOMCall);
  if (!call) {
    oom_ = true;
    return;
  }
  if (!ins->maybeCrossRealm()) {
    call->setNotCrossRealm();
  }

  ins->block()->insertBefore(ins, call);
  ins->replaceAllUsesWith(call);

  call->stealResumePoint(ins);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void ArrayMemoryView::visitConstructArray(MConstructArray* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->getElements();
  if (!isArrayStateElements(elements)) {
    return;
  }

  uint32_t numElements = state_->numElements();

  CallInfo callInfo(alloc_, /*constructing=*/true, ins->ignoresReturnValue());
  if (!callInfo.initForConstructArray(ins->getFunction(), ins->getThis(),
                                      ins->getNewTarget(), numElements)) {
    oom_ = true;
    return;
  }

  for (uint32_t i = 0; i < numElements; i++) {
    auto* element = state_->getElement(i);
    MOZ_ASSERT(element->type() != MIRType::MagicHole);

    callInfo.initArg(i, element);
  }

  auto addUndefined = [this]() { return undefinedVal_; };

  bool needsThisCheck = ins->needsThisCheck();
  bool isDOMCall = false;
  auto* call = MakeCall(alloc_, addUndefined, callInfo, needsThisCheck,
                        ins->getSingleTarget(), isDOMCall);
  if (!call) {
    oom_ = true;
    return;
  }
  if (!ins->maybeCrossRealm()) {
    call->setNotCrossRealm();
  }

  ins->block()->insertBefore(ins, call);
  ins->replaceAllUsesWith(call);

  call->stealResumePoint(ins);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

static inline bool IsOptimizableArgumentsInstruction(MInstruction* ins) {
  return ins->isCreateArgumentsObject() ||
         ins->isCreateInlinedArgumentsObject();
}

class ArgumentsReplacer : public MDefinitionVisitorDefaultNoop {
 private:
  const MIRGenerator* mir_;
  MIRGraph& graph_;
  MInstruction* args_;

  bool oom_ = false;

  TempAllocator& alloc() { return graph_.alloc(); }

  bool isInlinedArguments() const {
    return args_->isCreateInlinedArgumentsObject();
  }

  MNewArrayObject* inlineArgsArray(MInstruction* ins, Shape* shape,
                                   uint32_t begin, uint32_t count);

  void visitGuardToClass(MGuardToClass* ins);
  void visitGuardProto(MGuardProto* ins);
  void visitGuardArgumentsObjectFlags(MGuardArgumentsObjectFlags* ins);
  void visitGuardObjectHasSameRealm(MGuardObjectHasSameRealm* ins);
  void visitUnbox(MUnbox* ins);
  void visitGetArgumentsObjectArg(MGetArgumentsObjectArg* ins);
  void visitLoadArgumentsObjectArg(MLoadArgumentsObjectArg* ins);
  void visitLoadArgumentsObjectArgHole(MLoadArgumentsObjectArgHole* ins);
  void visitInArgumentsObjectArg(MInArgumentsObjectArg* ins);
  void visitArgumentsObjectLength(MArgumentsObjectLength* ins);
  void visitApplyArgsObj(MApplyArgsObj* ins);
  void visitArrayFromArgumentsObject(MArrayFromArgumentsObject* ins);
  void visitArgumentsSlice(MArgumentsSlice* ins);
  void visitLoadFixedSlot(MLoadFixedSlot* ins);

  bool oom() const { return oom_; }

 public:
  ArgumentsReplacer(const MIRGenerator* mir, MIRGraph& graph,
                    MInstruction* args)
      : mir_(mir), graph_(graph), args_(args) {
    MOZ_ASSERT(IsOptimizableArgumentsInstruction(args_));
  }

  bool escapes(MInstruction* ins, bool guardedForMapped = false);
  bool run();
  void assertSuccess();
};

// Returns false if the arguments object does not escape.
bool ArgumentsReplacer::escapes(MInstruction* ins, bool guardedForMapped) {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check arguments object\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // We can replace inlined arguments in scripts with OSR entries, but
  // the outermost arguments object has already been allocated before
  // we enter via OSR and can't be replaced.
  if (ins->isCreateArgumentsObject() && graph_.osrBlock()) {
    JitSpew(JitSpew_Escape, "Can't replace outermost OSR arguments");
    return true;
  }

  // Check all uses to see whether they can be supported without
  // allocating an ArgumentsObject.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize
    // if it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable args object cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::GuardToClass: {
        MGuardToClass* guard = def->toGuardToClass();
        if (!guard->isArgumentsObjectClass()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching class guard\n", guard);
          return true;
        }
        bool isMapped = guard->getClass() == &MappedArgumentsObject::class_;
        if (escapes(guard, isMapped)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardProto: {
        if (escapes(def->toInstruction(), guardedForMapped)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardArgumentsObjectFlags: {
        if (escapes(def->toInstruction(), guardedForMapped)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardObjectHasSameRealm: {
        if (escapes(def->toInstruction(), guardedForMapped)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (escapes(def->toInstruction())) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::LoadFixedSlot: {
        MLoadFixedSlot* load = def->toLoadFixedSlot();

        // We can replace arguments.callee.
        if (load->slot() == ArgumentsObject::CALLEE_SLOT.index()) {
          MOZ_ASSERT(guardedForMapped);
          continue;
        }
        JitSpew(JitSpew_Escape, "is escaped by unsupported LoadFixedSlot\n");
        return true;
      }

      case MDefinition::Opcode::ApplyArgsObj: {
        // Forwarded formals are read via the CallObject.
        if (args_->block()->info().anyFormalIsForwarded()) {
          JitSpew(JitSpew_Escape, "has forwarded formal arguments\n");
          return true;
        }
        if (ins == def->toApplyArgsObj()->getThis()) {
          JitSpew(JitSpew_Escape, "is escaped as |this| arg of ApplyArgsObj\n");
          return true;
        }
        MOZ_ASSERT(ins == def->toApplyArgsObj()->getArgsObj());
        break;
      }

      case MDefinition::Opcode::ArgumentsSlice:
      case MDefinition::Opcode::ArrayFromArgumentsObject:
      case MDefinition::Opcode::LoadArgumentsObjectArg:
      case MDefinition::Opcode::LoadArgumentsObjectArgHole:
        // Forwarded formals are read via the CallObject.
        if (args_->block()->info().anyFormalIsForwarded()) {
          JitSpew(JitSpew_Escape, "has forwarded formal arguments\n");
          return true;
        }
        break;

      // This is a replaceable consumer.
      case MDefinition::Opcode::ArgumentsObjectLength:
      case MDefinition::Opcode::GetArgumentsObjectArg:
      case MDefinition::Opcode::InArgumentsObjectArg:
        break;

      // This instruction is a no-op used to test that scalar replacement
      // is working as expected.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "ArgumentsObject is not escaped");
  return false;
}

// Replacing the arguments object is simpler than replacing an object
// or array, because the arguments object does not change state.
bool ArgumentsReplacer::run() {
  MBasicBlock* startBlock = args_->block();

  // Iterate over each basic block.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel("Scalar replacement of Arguments Object")) {
      return false;
    }

    // Iterates over phis and instructions.
    // We do not have to visit resume points. Any resume points that capture
    // the argument object will be handled by the Sink pass.
    for (MDefinitionIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the
      // visit function might discard itself from the basic block.
      MDefinition* def = *iter++;
      switch (def->op()) {
#define MIR_OP(op)              \
  case MDefinition::Opcode::op: \
    visit##op(def->to##op());   \
    break;
        MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
      }
      if (!graph_.alloc().ensureBallast()) {
        return false;
      }
      if (oom()) {
        return false;
      }
    }
  }

  assertSuccess();
  return true;
}

void ArgumentsReplacer::assertSuccess() {
  MOZ_ASSERT(args_->canRecoverOnBailout());
  MOZ_ASSERT(!args_->hasLiveDefUses());
}

void ArgumentsReplacer::visitGuardToClass(MGuardToClass* ins) {
  // Skip guards on other objects.
  if (ins->object() != args_) {
    return;
  }
  MOZ_ASSERT(ins->isArgumentsObjectClass());

  // Replace the guard with the args object.
  ins->replaceAllUsesWith(args_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitGuardProto(MGuardProto* ins) {
  // Skip guards on other objects.
  if (ins->object() != args_) {
    return;
  }

  // The prototype can only be changed through explicit operations, for example
  // by calling |Reflect.setPrototype|. We have already determined that the args
  // object doesn't escape, so its prototype can't be mutated.

  // Replace the guard with the args object.
  ins->replaceAllUsesWith(args_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitGuardArgumentsObjectFlags(
    MGuardArgumentsObjectFlags* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

#ifdef DEBUG
  // Each *_OVERRIDDEN_BIT can only be set by setting or deleting a
  // property of the args object. We have already determined that the
  // args object doesn't escape, so its properties can't be mutated.
  //
  // FORWARDED_ARGUMENTS_BIT is set if any mapped formal is closed
  // over. When that's the case, escapes() returns true for any
  // consumer that may read a forwarded formal, so the args object
  // isn't replaced and we don't reach this point.
  uint32_t supportedBits = ArgumentsObject::LENGTH_OVERRIDDEN_BIT |
                           ArgumentsObject::ITERATOR_OVERRIDDEN_BIT |
                           ArgumentsObject::ELEMENT_OVERRIDDEN_BIT |
                           ArgumentsObject::CALLEE_OVERRIDDEN_BIT |
                           ArgumentsObject::FORWARDED_ARGUMENTS_BIT;

  MOZ_ASSERT((ins->flags() & ~supportedBits) == 0);
  MOZ_ASSERT_IF(ins->flags() & ArgumentsObject::FORWARDED_ARGUMENTS_BIT,
                !args_->block()->info().anyFormalIsForwarded());
#endif

  // Replace the guard with the args object.
  ins->replaceAllUsesWith(args_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitGuardObjectHasSameRealm(
    MGuardObjectHasSameRealm* ins) {
  // Skip guards on other objects.
  if (ins->object() != args_) {
    return;
  }

  // We can eliminate this guard because the arguments object is created in the
  // script's realm and we don't inline cross-realm calls.

  // Replace the guard with the args object.
  ins->replaceAllUsesWith(args_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitUnbox(MUnbox* ins) {
  // Skip unrelated unboxes.
  if (ins->getOperand(0) != args_) {
    return;
  }
  MOZ_ASSERT(ins->type() == MIRType::Object);

  // Replace the unbox with the args object.
  ins->replaceAllUsesWith(args_);

  // Remove the unbox.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitGetArgumentsObjectArg(
    MGetArgumentsObjectArg* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  // We don't support setting arguments in ArgumentsReplacer::escapes,
  // so we can load the initial value of the argument without worrying
  // about it being stale.
  MDefinition* getArg;
  if (isInlinedArguments()) {
    // Inlined frames have direct access to the actual arguments.
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();
    if (ins->argno() < actualArgs->numActuals()) {
      getArg = actualArgs->getArg(ins->argno());
    } else {
      // Omitted arguments are not mapped to the arguments object, and
      // will always be undefined.
      auto* undef = MConstant::NewUndefined(alloc());
      ins->block()->insertBefore(ins, undef);
      getArg = undef;
    }
  } else {
    // Load the argument from the frame.
    auto* index = MConstant::NewInt32(alloc(), ins->argno());
    ins->block()->insertBefore(ins, index);

    auto* loadArg = MGetFrameArgument::New(alloc(), index);
    ins->block()->insertBefore(ins, loadArg);
    getArg = loadArg;
  }
  ins->replaceAllUsesWith(getArg);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitLoadArgumentsObjectArg(
    MLoadArgumentsObjectArg* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  MDefinition* index = ins->index();

  MInstruction* loadArg;
  if (isInlinedArguments()) {
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();

    // Insert bounds check.
    auto* length = MConstant::NewInt32(alloc(), actualArgs->numActuals());
    ins->block()->insertBefore(ins, length);

    MInstruction* check = MBoundsCheck::New(alloc(), index, length);
    check->setBailoutKind(ins->bailoutKind());
    ins->block()->insertBefore(ins, check);

    if (mir_->outerInfo().hadBoundsCheckBailout()) {
      check->setNotMovable();
    }

    loadArg = MGetInlinedArgument::New(alloc(), check, actualArgs);
    if (!loadArg) {
      oom_ = true;
      return;
    }
  } else {
    // Insert bounds check.
    auto* length = MArgumentsLength::New(alloc());
    ins->block()->insertBefore(ins, length);

    MInstruction* check = MBoundsCheck::New(alloc(), index, length);
    check->setBailoutKind(ins->bailoutKind());
    ins->block()->insertBefore(ins, check);

    if (mir_->outerInfo().hadBoundsCheckBailout()) {
      check->setNotMovable();
    }

    if (JitOptions.spectreIndexMasking) {
      check = MSpectreMaskIndex::New(alloc(), check, length);
      ins->block()->insertBefore(ins, check);
    }

    loadArg = MGetFrameArgument::New(alloc(), check);
  }
  ins->block()->insertBefore(ins, loadArg);
  ins->replaceAllUsesWith(loadArg);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitLoadArgumentsObjectArgHole(
    MLoadArgumentsObjectArgHole* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  MDefinition* index = ins->index();

  MInstruction* loadArg;
  if (isInlinedArguments()) {
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();

    loadArg = MGetInlinedArgumentHole::New(alloc(), index, actualArgs);
    if (!loadArg) {
      oom_ = true;
      return;
    }
  } else {
    auto* length = MArgumentsLength::New(alloc());
    ins->block()->insertBefore(ins, length);

    loadArg = MGetFrameArgumentHole::New(alloc(), index, length);
  }
  loadArg->setBailoutKind(ins->bailoutKind());
  ins->block()->insertBefore(ins, loadArg);
  ins->replaceAllUsesWith(loadArg);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitInArgumentsObjectArg(MInArgumentsObjectArg* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  MDefinition* index = ins->index();

  // Ensure the index is non-negative.
  auto* guardedIndex = MGuardInt32IsNonNegative::New(alloc(), index);
  guardedIndex->setBailoutKind(ins->bailoutKind());
  ins->block()->insertBefore(ins, guardedIndex);

  MInstruction* length;
  if (isInlinedArguments()) {
    uint32_t argc = args_->toCreateInlinedArgumentsObject()->numActuals();
    length = MConstant::NewInt32(alloc(), argc);
  } else {
    length = MArgumentsLength::New(alloc());
  }
  ins->block()->insertBefore(ins, length);

  auto* compare = MCompare::New(alloc(), guardedIndex, length, JSOp::Lt,
                                MCompare::Compare_Int32);
  ins->block()->insertBefore(ins, compare);
  ins->replaceAllUsesWith(compare);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitArgumentsObjectLength(
    MArgumentsObjectLength* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  MInstruction* length;
  if (isInlinedArguments()) {
    uint32_t argc = args_->toCreateInlinedArgumentsObject()->numActuals();
    length = MConstant::NewInt32(alloc(), argc);
  } else {
    length = MArgumentsLength::New(alloc());
  }
  ins->block()->insertBefore(ins, length);
  ins->replaceAllUsesWith(length);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitApplyArgsObj(MApplyArgsObj* ins) {
  // Skip other arguments objects.
  if (ins->getArgsObj() != args_) {
    return;
  }

  MInstruction* newIns;
  if (isInlinedArguments()) {
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();
    CallInfo callInfo(alloc(), /*constructing=*/false,
                      ins->ignoresReturnValue());

    callInfo.initForApplyInlinedArgs(ins->getFunction(), ins->getThis(),
                                     actualArgs->numActuals());
    for (uint32_t i = 0; i < actualArgs->numActuals(); i++) {
      callInfo.initArg(i, actualArgs->getArg(i));
    }

    auto addUndefined = [this, &ins]() -> MConstant* {
      MConstant* undef = MConstant::NewUndefined(alloc());
      ins->block()->insertBefore(ins, undef);
      return undef;
    };

    bool needsThisCheck = false;
    bool isDOMCall = false;
    auto* call = MakeCall(alloc(), addUndefined, callInfo, needsThisCheck,
                          ins->getSingleTarget(), isDOMCall);
    if (!call) {
      oom_ = true;
      return;
    }
    if (!ins->maybeCrossRealm()) {
      call->setNotCrossRealm();
    }
    newIns = call;
  } else {
    auto* numArgs = MArgumentsLength::New(alloc());
    ins->block()->insertBefore(ins, numArgs);

    // TODO: Should we rename MApplyArgs?
    auto* apply = MApplyArgs::New(alloc(), ins->getSingleTarget(),
                                  ins->getFunction(), numArgs, ins->getThis());
    apply->setBailoutKind(ins->bailoutKind());
    if (!ins->maybeCrossRealm()) {
      apply->setNotCrossRealm();
    }
    if (ins->ignoresReturnValue()) {
      apply->setIgnoresReturnValue();
    }
    newIns = apply;
  }

  ins->block()->insertBefore(ins, newIns);
  ins->replaceAllUsesWith(newIns);

  newIns->stealResumePoint(ins);
  ins->block()->discard(ins);
}

MNewArrayObject* ArgumentsReplacer::inlineArgsArray(MInstruction* ins,
                                                    Shape* shape,
                                                    uint32_t begin,
                                                    uint32_t count) {
  auto* actualArgs = args_->toCreateInlinedArgumentsObject();

  // Contrary to |WarpBuilder::build_Rest()|, we can always create
  // MNewArrayObject, because we're guaranteed to have a shape and all
  // arguments can be stored into fixed elements.
  static_assert(
      gc::CanUseFixedElementsForArray(ArgumentsObject::MaxInlinedArgs));

  gc::Heap heap = gc::Heap::Default;

  // Allocate an array of the correct size.
  auto* shapeConstant = MConstant::NewShape(alloc(), shape);
  ins->block()->insertBefore(ins, shapeConstant);

  auto* newArray = MNewArrayObject::New(alloc(), shapeConstant, count, heap);
  ins->block()->insertBefore(ins, newArray);

  if (count) {
    auto* elements = MElements::New(alloc(), newArray);
    ins->block()->insertBefore(ins, elements);

    for (uint32_t i = 0; i < count; i++) {
      auto* index = MConstant::NewInt32(alloc(), i);
      ins->block()->insertBefore(ins, index);

      MDefinition* arg = actualArgs->getArg(begin + i);
      auto* store =
          MStoreElement::NewNoPreBarrier(alloc(), elements, index, arg,
                                         /* needsHoleCheck = */ false);
      ins->block()->insertBefore(ins, store);
    }

    auto* initLength =
        MSetInitializedLength::New(alloc(), elements, count,
                                   /* needsPreBarrier = */ false);
    ins->block()->insertBefore(ins, initLength);
  }

  return newArray;
}

void ArgumentsReplacer::visitArrayFromArgumentsObject(
    MArrayFromArgumentsObject* ins) {
  // Skip other arguments objects.
  if (ins->argsObject() != args_) {
    return;
  }

  // We can only replace `arguments` because we've verified that the `arguments`
  // object hasn't been modified in any way. This implies that the arguments
  // stored in the stack frame haven't been changed either.
  //
  // The idea to replace `arguments` in spread calls `f(...arguments)` is now as
  // follows:
  // We replace |MArrayFromArgumentsObject| with the identical instructions we
  // emit when building a rest-array object, cf. |WarpBuilder::build_Rest()|. In
  // a next step, scalar replacement will then replace these new instructions
  // themselves.

  Shape* shape = ins->shape();
  MOZ_ASSERT(shape);

  MDefinition* replacement;
  if (isInlinedArguments()) {
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();
    uint32_t numActuals = actualArgs->numActuals();
    MOZ_ASSERT(numActuals <= ArgumentsObject::MaxInlinedArgs);

    replacement = inlineArgsArray(ins, shape, 0, numActuals);
  } else {
    // We can use |MRest| to read all arguments, because we've guaranteed that
    // the arguments stored in the stack frame haven't changed; see the comment
    // at the start of this method.

    auto* numActuals = MArgumentsLength::New(alloc());
    ins->block()->insertBefore(ins, numActuals);

    // Set |numFormals| to zero to read all arguments, including any formals.
    uint32_t numFormals = 0;

    auto* rest = MRest::New(alloc(), numActuals, numFormals, shape);
    ins->block()->insertBefore(ins, rest);

    replacement = rest;
  }

  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

static uint32_t NormalizeSlice(MDefinition* def, uint32_t length) {
  int32_t value = def->toConstant()->toInt32();
  if (value < 0) {
    return std::max(int32_t(uint32_t(value) + length), 0);
  }
  return std::min(uint32_t(value), length);
}

void ArgumentsReplacer::visitArgumentsSlice(MArgumentsSlice* ins) {
  // Skip other arguments objects.
  if (ins->object() != args_) {
    return;
  }

  // Optimise the common pattern |Array.prototype.slice.call(arguments, begin)|,
  // where |begin| is a non-negative, constant int32.
  //
  // An absent end-index is replaced by |arguments.length|, so we try to match
  // |Array.prototype.slice.call(arguments, begin, arguments.length)|.
  if (isInlinedArguments()) {
    // When this is an inlined arguments, |arguments.length| has been replaced
    // by a constant.
    if (ins->begin()->isConstant() && ins->end()->isConstant()) {
      auto* actualArgs = args_->toCreateInlinedArgumentsObject();
      uint32_t numActuals = actualArgs->numActuals();
      MOZ_ASSERT(numActuals <= ArgumentsObject::MaxInlinedArgs);

      uint32_t begin = NormalizeSlice(ins->begin(), numActuals);
      uint32_t end = NormalizeSlice(ins->end(), numActuals);
      uint32_t count = end > begin ? end - begin : 0;
      MOZ_ASSERT(count <= numActuals);

      Shape* shape = ins->templateObj()->shape();
      auto* newArray = inlineArgsArray(ins, shape, begin, count);

      ins->replaceAllUsesWith(newArray);

      // Remove original instruction.
      ins->block()->discard(ins);
      return;
    }
  } else {
    // Otherwise |arguments.length| is emitted as MArgumentsLength.
    if (ins->begin()->isConstant() && ins->end()->isArgumentsLength()) {
      int32_t begin = ins->begin()->toConstant()->toInt32();
      if (begin >= 0) {
        auto* numActuals = MArgumentsLength::New(alloc());
        ins->block()->insertBefore(ins, numActuals);

        // Set |numFormals| to read all arguments starting at |begin|.
        uint32_t numFormals = begin;

        Shape* shape = ins->templateObj()->shape();

        // Use MRest because it can be scalar replaced, which enables further
        // optimizations.
        auto* rest = MRest::New(alloc(), numActuals, numFormals, shape);
        ins->block()->insertBefore(ins, rest);

        ins->replaceAllUsesWith(rest);

        // Remove original instruction.
        ins->block()->discard(ins);
        return;
      }
    }
  }

  MInstruction* numArgs;
  if (isInlinedArguments()) {
    uint32_t argc = args_->toCreateInlinedArgumentsObject()->numActuals();
    numArgs = MConstant::NewInt32(alloc(), argc);
  } else {
    numArgs = MArgumentsLength::New(alloc());
  }
  ins->block()->insertBefore(ins, numArgs);

  auto* begin = MNormalizeSliceTerm::New(alloc(), ins->begin(), numArgs);
  ins->block()->insertBefore(ins, begin);

  auto* end = MNormalizeSliceTerm::New(alloc(), ins->end(), numArgs);
  ins->block()->insertBefore(ins, end);

  auto* beginMin = MMinMax::NewMin(alloc(), begin, end, MIRType::Int32);
  ins->block()->insertBefore(ins, beginMin);

  // Safe to truncate because both operands are positive and end >= beginMin.
  auto* count = MSub::New(alloc(), end, beginMin, MIRType::Int32);
  count->setTruncateKind(TruncateKind::Truncate);
  ins->block()->insertBefore(ins, count);

  MInstruction* replacement;
  if (isInlinedArguments()) {
    auto* actualArgs = args_->toCreateInlinedArgumentsObject();
    replacement =
        MInlineArgumentsSlice::New(alloc(), beginMin, count, actualArgs,
                                   ins->templateObj(), ins->initialHeap());
    if (!replacement) {
      oom_ = true;
      return;
    }
  } else {
    replacement = MFrameArgumentsSlice::New(
        alloc(), beginMin, count, ins->templateObj(), ins->initialHeap());
  }
  ins->block()->insertBefore(ins, replacement);

  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void ArgumentsReplacer::visitLoadFixedSlot(MLoadFixedSlot* ins) {
  // Skip other arguments objects.
  if (ins->object() != args_) {
    return;
  }

  MOZ_ASSERT(ins->slot() == ArgumentsObject::CALLEE_SLOT.index());

  MDefinition* replacement;
  if (isInlinedArguments()) {
    replacement = args_->toCreateInlinedArgumentsObject()->getCallee();
  } else {
    auto* callee = MCallee::New(alloc());
    ins->block()->insertBefore(ins, callee);
    replacement = callee;
  }
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

static inline bool IsOptimizableRestInstruction(MInstruction* ins) {
  return ins->isRest();
}

class RestReplacer : public GenericArrayReplacer {
 private:
  const MIRGenerator* mir_;
  MIRGraph& graph_;

  MRest* rest() const { return arr_->toRest(); }
  MDefinition* restLength(MInstruction* ins);

  void visitLength(MInstruction* ins, MDefinition* elements);
  void visitLoadElement(MLoadElement* ins);
  void visitArrayLength(MArrayLength* ins);
  void visitInitializedLength(MInitializedLength* ins);
  void visitApplyArray(MApplyArray* ins);
  void visitConstructArray(MConstructArray* ins);

  bool escapes(MElements* ins);

 public:
  RestReplacer(const MIRGenerator* mir, MIRGraph& graph, MInstruction* rest)
      : GenericArrayReplacer(graph.alloc(), rest), mir_(mir), graph_(graph) {
    MOZ_ASSERT(IsOptimizableRestInstruction(arr_));
  }

  bool escapes(MInstruction* ins);
  bool run();
  void assertSuccess();
};

void RestReplacer::assertSuccess() {
  MOZ_ASSERT(arr_->canRecoverOnBailout());
  MOZ_ASSERT(!arr_->hasLiveDefUses());
}

// Returns false if the rest array object does not escape.
bool RestReplacer::escapes(MInstruction* ins) {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check rest array\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // We can replace rest arrays in scripts with OSR entries, but the outermost
  // rest object has already been allocated before we enter via OSR and can't be
  // replaced.
  // See also the same restriction when replacing |arguments|.
  if (graph_.osrBlock()) {
    JitSpew(JitSpew_Escape, "Can't replace outermost OSR rest array");
    return true;
  }

  // Check all uses to see whether they can be supported without allocating an
  // ArrayObject for the rest parameter.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize
    // if it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable rest array cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::Elements: {
        auto* elem = def->toElements();
        MOZ_ASSERT(elem->object() == ins);
        if (escapes(elem)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardShape: {
        auto* guard = def->toGuardShape();
        if (rest()->shape() != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardToClass: {
        auto* guard = def->toGuardToClass();
        if (guard->getClass() != &ArrayObject::class_) {
          JitSpewDef(JitSpew_Escape, "has a non-matching class guard\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardArrayIsPacked: {
        // Rest arrays are always packed as long as they aren't modified.
        auto* guard = def->toGuardArrayIsPacked();
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (escapes(def->toInstruction())) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // This instruction is supported for |JSOp::OptimizeSpreadCall|.
      case MDefinition::Opcode::Compare: {
        bool canFold;
        if (!def->toCompare()->tryFold(&canFold)) {
          JitSpewDef(JitSpew_Escape, "has an unsupported compare\n", def);
          return true;
        }
        break;
      }

      // This instruction is a no-op used to test that scalar replacement is
      // working as expected.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Rest array object is not escaped");
  return false;
}

bool RestReplacer::escapes(MElements* ins) {
  JitSpewDef(JitSpew_Escape, "Check rest array elements\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    // The MIRType::Elements cannot be captured in a resume point as it does not
    // represent a value allocation.
    MDefinition* def = (*i)->consumer()->toDefinition();

    switch (def->op()) {
      case MDefinition::Opcode::LoadElement:
        MOZ_ASSERT(def->toLoadElement()->elements() == ins);
        break;

      case MDefinition::Opcode::ArrayLength:
        MOZ_ASSERT(def->toArrayLength()->elements() == ins);
        break;

      case MDefinition::Opcode::InitializedLength:
        MOZ_ASSERT(def->toInitializedLength()->elements() == ins);
        break;

      case MDefinition::Opcode::ApplyArray:
        MOZ_ASSERT(def->toApplyArray()->getElements() == ins);
        break;

      case MDefinition::Opcode::ConstructArray:
        MOZ_ASSERT(def->toConstructArray()->getElements() == ins);
        break;

      case MDefinition::Opcode::GuardElementsArePacked:
        MOZ_ASSERT(def->toGuardElementsArePacked()->elements() == ins);
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Rest array object is not escaped");
  return false;
}

// Replacing the rest array object is simpler than replacing an object or array,
// because the rest array object does not change state.
bool RestReplacer::run() {
  MBasicBlock* startBlock = arr_->block();

  // Iterate over each basic block.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel("Scalar replacement of rest array object")) {
      return false;
    }

    // Iterates over phis and instructions.
    // We do not have to visit resume points. Any resume points that capture the
    // rest array object will be handled by the Sink pass.
    for (MDefinitionIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the visit
      // function might discard itself from the basic block.
      MDefinition* def = *iter++;
      switch (def->op()) {
#define MIR_OP(op)              \
  case MDefinition::Opcode::op: \
    visit##op(def->to##op());   \
    break;
        MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
      }
      if (!alloc_.ensureBallast()) {
        return false;
      }
    }
  }

  assertSuccess();
  return true;
}

void RestReplacer::visitLoadElement(MLoadElement* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->elements();
  if (!isTargetElements(elements)) {
    return;
  }

  MDefinition* index = ins->index();

  // Adjust the index to skip any extra formals.
  if (uint32_t formals = rest()->numFormals()) {
    auto* numFormals = MConstant::NewInt32(alloc_, formals);
    ins->block()->insertBefore(ins, numFormals);

    auto* add = MAdd::New(alloc_, index, numFormals, TruncateKind::Truncate);
    ins->block()->insertBefore(ins, add);

    index = add;
  }

  auto* loadArg = MGetFrameArgument::New(alloc_, index);

  ins->block()->insertBefore(ins, loadArg);
  ins->replaceAllUsesWith(loadArg);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

MDefinition* RestReplacer::restLength(MInstruction* ins) {
  // Compute |Math.max(numActuals - numFormals, 0)| for the rest array length.

  auto* numActuals = rest()->numActuals();

  if (uint32_t formals = rest()->numFormals()) {
    auto* numFormals = MConstant::NewInt32(alloc_, formals);
    ins->block()->insertBefore(ins, numFormals);

    auto* length = MSub::New(alloc_, numActuals, numFormals, MIRType::Int32);
    length->setTruncateKind(TruncateKind::Truncate);
    ins->block()->insertBefore(ins, length);

    auto* zero = MConstant::NewInt32(alloc_, 0);
    ins->block()->insertBefore(ins, zero);

    auto* minmax = MMinMax::NewMax(alloc_, length, zero, MIRType::Int32);
    ins->block()->insertBefore(ins, minmax);

    return minmax;
  }

  return numActuals;
}

void RestReplacer::visitLength(MInstruction* ins, MDefinition* elements) {
  MOZ_ASSERT(ins->isArrayLength() || ins->isInitializedLength());

  // Skip other array objects.
  if (!isTargetElements(elements)) {
    return;
  }

  MDefinition* replacement = restLength(ins);

  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void RestReplacer::visitArrayLength(MArrayLength* ins) {
  visitLength(ins, ins->elements());
}

void RestReplacer::visitInitializedLength(MInitializedLength* ins) {
  // The initialized length of a rest array is equal to its length.
  visitLength(ins, ins->elements());
}

void RestReplacer::visitApplyArray(MApplyArray* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->getElements();
  if (!isTargetElements(elements)) {
    return;
  }

  auto* numActuals = restLength(ins);

  auto* apply =
      MApplyArgs::New(alloc_, ins->getSingleTarget(), ins->getFunction(),
                      numActuals, ins->getThis(), rest()->numFormals());
  apply->setBailoutKind(ins->bailoutKind());
  if (!ins->maybeCrossRealm()) {
    apply->setNotCrossRealm();
  }
  if (ins->ignoresReturnValue()) {
    apply->setIgnoresReturnValue();
  }
  ins->block()->insertBefore(ins, apply);

  ins->replaceAllUsesWith(apply);

  apply->stealResumePoint(ins);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

void RestReplacer::visitConstructArray(MConstructArray* ins) {
  // Skip other array objects.
  MDefinition* elements = ins->getElements();
  if (!isTargetElements(elements)) {
    return;
  }

  auto* numActuals = restLength(ins);

  auto* construct = MConstructArgs::New(
      alloc_, ins->getSingleTarget(), ins->getFunction(), numActuals,
      ins->getThis(), ins->getNewTarget(), rest()->numFormals());
  construct->setBailoutKind(ins->bailoutKind());
  if (!ins->maybeCrossRealm()) {
    construct->setNotCrossRealm();
  }

  ins->block()->insertBefore(ins, construct);
  ins->replaceAllUsesWith(construct);

  construct->stealResumePoint(ins);

  // Remove original instruction.
  discardInstruction(ins, elements);
}

static inline bool IsOptimizableSubarrayInstruction(MInstruction* ins) {
  return ins->isTypedArraySubarray();
}

class SubarrayReplacer : public MDefinitionVisitorDefaultNoop {
 private:
  const MIRGenerator* mir_;
  MIRGraph& graph_;
  MInstruction* subarray_;
  uint32_t initialNumInstrIds_;

  TempAllocator& alloc() { return graph_.alloc(); }
  MTypedArraySubarray* subarray() const {
    return subarray_->toTypedArraySubarray();
  }

  bool escapes(MArrayBufferViewElements* ins) const;
  bool escapes(MGuardHasAttachedArrayBuffer* ins) const;

  void visitArrayBufferViewByteOffset(MArrayBufferViewByteOffset* ins);
  void visitArrayBufferViewElements(MArrayBufferViewElements* ins);
  void visitArrayBufferViewLength(MArrayBufferViewLength* ins);
  void visitGuardHasAttachedArrayBuffer(MGuardHasAttachedArrayBuffer* ins);
  void visitGuardShape(MGuardShape* ins);
  void visitLoadUnboxedScalar(MLoadUnboxedScalar* ins);
  void visitStoreUnboxedScalar(MStoreUnboxedScalar* ins);
  void visitTypedArrayElementSize(MTypedArrayElementSize* ins);
  void visitTypedArrayFill(MTypedArrayFill* ins);
  void visitTypedArraySet(MTypedArraySet* ins);
  void visitTypedArraySubarray(MTypedArraySubarray* ins);
  void visitUnbox(MUnbox* ins);

  // New instructions created in SubarrayReplacer.
  bool isNewInstruction(MDefinition* ins) const {
    return ins->id() >= initialNumInstrIds_;
  }

  bool isNewGuardHasAttachedArrayBuffer(MDefinition* ins) const {
    // GuardHasAttachedArrayBuffer is replaced with a guard on the subarray's
    // object.
    if (ins->isGuardHasAttachedArrayBuffer() && isNewInstruction(ins)) {
      MOZ_ASSERT(ins->toGuardHasAttachedArrayBuffer()->object() ==
                 subarray()->object());
      return true;
    }
    return false;
  }

  bool isSubarray(MDefinition* ins) const {
    // New MGuardHasAttachedArrayBuffer instructions aren't expected to flow
    // into |isSubarray()| callers.
    MOZ_ASSERT(!isNewGuardHasAttachedArrayBuffer(ins));
    return ins == subarray_;
  }

  bool isSubarrayOrGuard(MDefinition* ins) const {
    return ins == subarray_ || isNewGuardHasAttachedArrayBuffer(ins);
  }

  MDefinition* toSubarrayObject(MDefinition* ins) const {
    MOZ_ASSERT(isSubarrayOrGuard(ins));
    if (ins == subarray_) {
      return subarray()->object();
    }
    return ins;
  }

  bool isSubarrayElements(MArrayBufferViewElements* ins) const {
    // ArrayBufferViewElements is replaced with an access to the subarray's
    // object.
    if (isNewInstruction(ins)) {
      MOZ_ASSERT(ins->object() == subarray()->object());
      return true;
    }
    return false;
  }

#ifdef DEBUG
  static bool isBoundsCheck(MDefinition* ins) {
    if (ins->isSpectreMaskIndex()) {
      ins = ins->toSpectreMaskIndex()->index();
    }
    return ins->isBoundsCheck();
  }
#endif

  auto* templateObject() const {
    JSObject* obj = subarray()->templateObject();
    MOZ_ASSERT(obj, "missing template object");
    return &obj->as<TypedArrayObject>();
  }

  auto elementType() const { return templateObject()->type(); }

  bool isImmutable() const {
    return templateObject()->is<ImmutableTypedArrayObject>();
  }

 public:
  SubarrayReplacer(const MIRGenerator* mir, MIRGraph& graph,
                   MInstruction* subarray)
      : mir_(mir),
        graph_(graph),
        subarray_(subarray),
        initialNumInstrIds_(graph.getNumInstructionIds()) {
    MOZ_ASSERT(IsOptimizableSubarrayInstruction(subarray_));
  }

  bool escapes(MInstruction* ins) const;
  bool run();
  void assertSuccess() const;
};

void SubarrayReplacer::visitUnbox(MUnbox* ins) {
  // Skip unbox on other objects.
  if (!isSubarray(ins->input())) {
    return;
  }
  MOZ_ASSERT(ins->type() == MIRType::Object);

  // Replace the unbox with the subarray object.
  ins->replaceAllUsesWith(subarray_);

  // Remove the unbox.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitGuardShape(MGuardShape* ins) {
  // Skip guards on other objects.
  if (!isSubarray(ins->object())) {
    return;
  }

  // Replace the guard with the subarray object.
  ins->replaceAllUsesWith(subarray_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitGuardHasAttachedArrayBuffer(
    MGuardHasAttachedArrayBuffer* ins) {
  // Skip guards on other objects.
  if (!isSubarray(ins->object())) {
    return;
  }

  // Create a new guard on the subarray's input argument.
  auto* newGuard =
      MGuardHasAttachedArrayBuffer::New(alloc(), subarray()->object());
  newGuard->setBailoutKind(ins->bailoutKind());
  ins->block()->insertBefore(ins, newGuard);

  // Replace the guard.
  ins->replaceAllUsesWith(newGuard);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitArrayBufferViewLength(MArrayBufferViewLength* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }

  MDefinition* replacement;
  if (!isImmutable()) {
    // Get length of |subarray->object()|.
    auto* length = MArrayBufferViewLength::New(alloc(), subarray()->object());
    ins->block()->insertBefore(ins, length);

    // Minimum to zero the length if the underlying buffer is now detached.
    auto* minmax =
        MMinMax::NewMin(alloc(), subarray()->length(), length, MIRType::IntPtr);
    ins->block()->insertBefore(ins, minmax);

    replacement = minmax;
  } else {
    replacement = subarray()->length();
  }

  // Replace the instruction.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitArrayBufferViewByteOffset(
    MArrayBufferViewByteOffset* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }

  auto* shift = MConstant::NewIntPtr(alloc(), TypedArrayShift(elementType()));
  ins->block()->insertBefore(ins, shift);

  MDefinition* start;
  if (!isImmutable()) {
    // Get length of |subarray->object()|.
    auto* length = MArrayBufferViewLength::New(alloc(), subarray()->object());
    ins->block()->insertBefore(ins, length);

    // Minimum to zero |start| if the underlying buffer is now detached.
    auto* minmax =
        MMinMax::NewMin(alloc(), subarray()->start(), length, MIRType::IntPtr);
    ins->block()->insertBefore(ins, minmax);

    start = minmax;
  } else {
    start = subarray()->start();
  }

  // Shift to convert start index to start byte-offset.
  auto* adjustment = MLsh::New(alloc(), start, shift, MIRType::IntPtr);
  ins->block()->insertBefore(ins, adjustment);

  // Byte-offset of |subarray->object()|.
  auto* byteOffset =
      MArrayBufferViewByteOffset::New(alloc(), subarray()->object());
  ins->block()->insertBefore(ins, byteOffset);

  // Actual byte-offset into the array buffer.
  auto* replacement =
      MAdd::New(alloc(), byteOffset, adjustment, MIRType::IntPtr);
  ins->block()->insertBefore(ins, replacement);

  // Replace the byte-offset.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitArrayBufferViewElements(
    MArrayBufferViewElements* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }

  auto* replacement =
      MArrayBufferViewElements::New(alloc(), subarray()->object());
  ins->block()->insertBefore(ins, replacement);

  // Replace the elements.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitLoadUnboxedScalar(MLoadUnboxedScalar* ins) {
  // Skip other array buffer view elements.
  if (!isSubarrayElements(ins->elements()->toArrayBufferViewElements())) {
    return;
  }
  MOZ_ASSERT(isBoundsCheck(ins->index()));

  // This MAdd can't overflow because `ins.index` is a bounds-checked index
  // into the subarray and `subarray.start` is a valid index into
  // `subarray.object`.
  //
  // Given non-negative `ins.index`, `subarray.start`, and `subarray.length`,
  // the following two conditions hold:
  // 1. `ins.index < subarray.length`
  // 2. `subarray.start + subarray.length <= subarray.object.length`
  //
  // And therefore also:
  // `ins.index + subarray.start < subarray.object.length`
  //
  // Which means the addition can't overflow.
  auto* adjustedIndex =
      MAdd::New(alloc(), ins->index(), subarray()->start(), MIRType::IntPtr);
  ins->block()->insertBefore(ins, adjustedIndex);

  auto* replacement =
      MLoadUnboxedScalar::New(alloc(), ins->elements(), adjustedIndex,
                              ins->storageType(), ins->requiresMemoryBarrier());
  replacement->setResultType(ins->type());
  replacement->setBailoutKind(ins->bailoutKind());
  if (ins->resumePoint()) {
    replacement->stealResumePoint(ins);
  }
  ins->block()->insertBefore(ins, replacement);

  // Replace the load.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitStoreUnboxedScalar(MStoreUnboxedScalar* ins) {
  // Skip other array buffer view elements.
  if (!isSubarrayElements(ins->elements()->toArrayBufferViewElements())) {
    return;
  }
  MOZ_ASSERT(isBoundsCheck(ins->index()));

  // See visitLoadUnboxedScalar for why this addition can't overflow.
  auto* adjustedIndex =
      MAdd::New(alloc(), ins->index(), subarray()->start(), MIRType::IntPtr);
  ins->block()->insertBefore(ins, adjustedIndex);

  auto* replacement = MStoreUnboxedScalar::New(
      alloc(), ins->elements(), adjustedIndex, ins->value(), ins->writeType(),
      ins->requiresMemoryBarrier());
  replacement->stealResumePoint(ins);
  ins->block()->insertBefore(ins, replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitTypedArrayElementSize(MTypedArrayElementSize* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }

  int32_t bytesPerElement = TypedArrayElemSize(elementType());
  auto* replacement = MConstant::NewInt32(alloc(), bytesPerElement);
  ins->block()->insertBefore(ins, replacement);

  // Replace the element-size.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitTypedArrayFill(MTypedArrayFill* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }

  auto* subarrayStart = subarray()->start();
  auto* subarrayLength = subarray()->length();

  // Make |start| and |end| relative to |subarrayLength|.
  auto* relativeStart =
      MToIntegerIndex::New(alloc(), ins->start(), subarrayLength);
  ins->block()->insertBefore(ins, relativeStart);

  auto* relativeEnd = MToIntegerIndex::New(alloc(), ins->end(), subarrayLength);
  ins->block()->insertBefore(ins, relativeEnd);

  // Compute actual start and end indices by adding |subarrayStart|.
  auto* actualStart =
      MAdd::New(alloc(), relativeStart, subarrayStart, MIRType::IntPtr);
  ins->block()->insertBefore(ins, actualStart);

  auto* actualEnd =
      MAdd::New(alloc(), relativeEnd, subarrayStart, MIRType::IntPtr);
  ins->block()->insertBefore(ins, actualEnd);

  auto* newFill =
      MTypedArrayFill::New(alloc(), subarray()->object(), ins->value(),
                           actualStart, actualEnd, ins->elementType());
  newFill->setBailoutKind(ins->bailoutKind());
  newFill->stealResumePoint(ins);
  ins->block()->insertBefore(ins, newFill);

  // Replace the fill.
  ins->replaceAllUsesWith(newFill);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitTypedArraySet(MTypedArraySet* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->target()) && !isSubarrayOrGuard(ins->source())) {
    return;
  }

  // The replaced |subarray| instruction can be the target, source, or both
  // operands of MTypedArraySet:
  //
  // - Target operand: `ta.subarray(...).set(...)`
  // - Source operand: `ta.set(src.subarray(...), ...)`
  // - Both operands: `sub = src.subarray(...); sub.set(sub, ...)`.
  //
  // When |subarray| is the target operand, |subarray->start| needs to be added
  // to |ins->offset|.
  //
  // When |subarray| is the source operand, MTypedArraySet is replaced with
  // MTypedArraySetFromSubarray to pass through |subarray->start| and
  // |subarray->length|.
  //
  // When |subarray| is both the target and the source operand, the call is
  // either a no-op instruction, or bails out and then throws an exception.

  MInstruction* replacement;
  if (isSubarrayOrGuard(ins->target()) && isSubarrayOrGuard(ins->source())) {
    // Either a no-op when the offset is zero. Or bails out when the offset is
    // non-zero. (Bail-out happens through MGuardTypedArraySetOffset.)
    replacement = MNop::New(alloc());
  } else if (isSubarrayOrGuard(ins->target())) {
    auto* target = toSubarrayObject(ins->target());

    // Addition can't overflow because preceding guards ensure:
    // 1. |ins->offset()| and |subarray->start()| are both non-negative.
    // 2. |ins->offset()| is a valid index into |subarray|.
    // 3. |subarray->start()| is a valid index |subarray->object()|.
    auto* newOffset =
        MAdd::New(alloc(), ins->offset(), subarray()->start(), MIRType::IntPtr);
    ins->block()->insertBefore(ins, newOffset);

    replacement = MTypedArraySet::New(alloc(), target, ins->source(), newOffset,
                                      ins->canUseBitwiseCopy());
  } else {
    auto* source = toSubarrayObject(ins->source());

    replacement = MTypedArraySetFromSubarray::New(
        alloc(), ins->target(), source, ins->offset(), subarray()->start(),
        subarray()->length(), ins->canUseBitwiseCopy());
  }
  replacement->stealResumePoint(ins);
  ins->block()->insertBefore(ins, replacement);

  // Replace the set.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void SubarrayReplacer::visitTypedArraySubarray(MTypedArraySubarray* ins) {
  // Skip other typed array objects.
  if (!isSubarrayOrGuard(ins->object())) {
    return;
  }
  MOZ_ASSERT(!ins->isScalarReplaced());

  // Add both |start| operands to get the adjusted start index.
  auto* newStart =
      MAdd::New(alloc(), subarray()->start(), ins->start(), MIRType::IntPtr);
  ins->block()->insertBefore(ins, newStart);

  auto* replacement = MTypedArraySubarray::New(
      alloc(), subarray()->object(), newStart, ins->length(),
      ins->templateObject(), ins->initialHeap());
  replacement->stealResumePoint(ins);
  ins->block()->insertBefore(ins, replacement);

  // Replace the subarray.
  ins->replaceAllUsesWith(replacement);

  // Remove original instruction.
  ins->block()->discard(ins);
}

// Returns false if the subarray typed array elements do not escape.
bool SubarrayReplacer::escapes(MArrayBufferViewElements* ins) const {
  MOZ_ASSERT(ins->type() == MIRType::Elements);

  JitSpewDef(JitSpew_Escape, "Check subarray typed array elements\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    // The MIRType::Elements cannot be captured in a resume point as it does
    // not represent a value allocation.
    MDefinition* def = (*i)->consumer()->toDefinition();

    switch (def->op()) {
      // Replacable instructions.
      case MDefinition::Opcode::LoadUnboxedScalar:
      case MDefinition::Opcode::StoreUnboxedScalar:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Subarray typed array elements is not escaped");
  return false;
}

// Returns false if the subarray guard-has-attached-buffer does not escape.
bool SubarrayReplacer::escapes(MGuardHasAttachedArrayBuffer* ins) const {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check subarray typed array guard\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // Check all uses to see whether they can be supported without allocating an
  // TypedArrayObject for the `%TypedArray%.prototype.subarray` call.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize if
    // it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable guard cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::ArrayBufferViewElements: {
        auto* elements = def->toArrayBufferViewElements();
        if (escapes(elements)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // Replacable instructions.
      case MDefinition::Opcode::ArrayBufferViewByteOffset:
      case MDefinition::Opcode::ArrayBufferViewLength:
      case MDefinition::Opcode::TypedArrayElementSize:
      case MDefinition::Opcode::TypedArrayFill:
      case MDefinition::Opcode::TypedArraySet:
      case MDefinition::Opcode::TypedArraySubarray:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Subarray guard-has-attached-buffer is not escaped");
  return false;
}

// Returns false if the subarray typed array object does not escape.
bool SubarrayReplacer::escapes(MInstruction* ins) const {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check subarray typed array\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // Check all uses to see whether they can be supported without allocating an
  // TypedArrayObject for the `%TypedArray%.prototype.subarray` call.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize if
    // it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable subarray cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::GuardShape: {
        auto* guard = def->toGuardShape();
        if (templateObject()->shape() != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (escapes(def->toInstruction())) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardHasAttachedArrayBuffer: {
        auto* guard = def->toGuardHasAttachedArrayBuffer();
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::ArrayBufferViewElements: {
        auto* elements = def->toArrayBufferViewElements();
        if (escapes(elements)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // Replacable instructions.
      case MDefinition::Opcode::ArrayBufferViewByteOffset:
      case MDefinition::Opcode::ArrayBufferViewLength:
      case MDefinition::Opcode::TypedArrayElementSize:
      case MDefinition::Opcode::TypedArrayFill:
      case MDefinition::Opcode::TypedArraySet:
      case MDefinition::Opcode::TypedArraySubarray:
        break;

      // This instruction is a no-op used to test that scalar replacement is
      // working as expected.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Subarray typed array object is not escaped");
  return false;
}

bool SubarrayReplacer::run() {
  MBasicBlock* startBlock = subarray_->block();

  // Iterate over each basic block.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel("Scalar replacement of subarray object")) {
      return false;
    }

    // Iterates over phis and instructions.
    // We do not have to visit resume points. Any resume points that capture the
    // subarray typed array object will be handled by the Sink pass.
    for (MDefinitionIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the visit
      // function might discard itself from the basic block.
      MDefinition* def = *iter++;
      switch (def->op()) {
#define MIR_OP(op)              \
  case MDefinition::Opcode::op: \
    visit##op(def->to##op());   \
    break;
        MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
      }
      if (!graph_.alloc().ensureBallast()) {
        return false;
      }
    }
  }

  assertSuccess();
  return true;
}

void SubarrayReplacer::assertSuccess() const {
  subarray()->setScalarReplaced();
  MOZ_ASSERT(subarray_->canRecoverOnBailout());
  MOZ_ASSERT(!subarray_->hasLiveDefUses());
}

static inline bool IsOptimizableNewDateObjectInstruction(MInstruction* ins) {
  return ins->isNewDateObject();
}

class DateObjectReplacer : public MDefinitionVisitorDefaultNoop {
 private:
  const MIRGenerator* mir_;
  MIRGraph& graph_;
  MInstruction* dateObject_;

  // Allow scalar replacement if a date component is read exactly once.
  bool hasSeenDateComponent_ = false;

  TempAllocator& alloc() { return graph_.alloc(); }

  MNewDateObject* newDateObject() const {
    return dateObject_->toNewDateObject();
  }
  auto* templateObject() const { return newDateObject()->templateObject(); }

  void visitGuardShape(MGuardShape* ins);
  void visitUnbox(MUnbox* ins);
  void visitLoadFixedSlot(MLoadFixedSlot* ins);
  void visitDateFillLocalTimeSlots(MDateFillLocalTimeSlots* ins);

 public:
  DateObjectReplacer(const MIRGenerator* mir, MIRGraph& graph,
                     MInstruction* dateObject)
      : mir_(mir), graph_(graph), dateObject_(dateObject) {
    MOZ_ASSERT(IsOptimizableNewDateObjectInstruction(dateObject));
  }

  bool escapes(MInstruction* ins);
  bool run();
  void assertSuccess() const;
};

void DateObjectReplacer::visitUnbox(MUnbox* ins) {
  // Skip unbox on other objects.
  if (ins->input() != dateObject_) {
    return;
  }
  MOZ_ASSERT(ins->type() == MIRType::Object);

  // Replace the unbox with the date object.
  ins->replaceAllUsesWith(dateObject_);

  // Remove the unbox.
  ins->block()->discard(ins);
}

void DateObjectReplacer::visitGuardShape(MGuardShape* ins) {
  // Skip guards on other objects.
  if (ins->object() != dateObject_) {
    return;
  }

  // Replace the guard with the date object.
  ins->replaceAllUsesWith(dateObject_);

  // Remove the guard.
  ins->block()->discard(ins);
}

void DateObjectReplacer::visitLoadFixedSlot(MLoadFixedSlot* ins) {
  // Skip load on other objects.
  if (ins->object() != dateObject_) {
    return;
  }

  auto* utcTime = newDateObject()->utcTime();

  MDefinition* replacement;
  switch (ins->slot()) {
    case DateObject::UTC_TIME_SLOT.index(): {
      // Replace load with the UTC time argument.
      replacement = utcTime;
      break;
    }
    case DateObject::LOCAL_YEAR_SLOT.index(): {
      auto* yearFromTime = MYearFromTime::New(alloc(), utcTime);
      ins->block()->insertBefore(ins, yearFromTime);
      replacement = yearFromTime;
      break;
    }
    case DateObject::LOCAL_MONTH_SLOT.index(): {
      auto* monthFromTime = MMonthFromTime::New(alloc(), utcTime);
      ins->block()->insertBefore(ins, monthFromTime);
      replacement = monthFromTime;
      break;
    }
    case DateObject::LOCAL_DATE_SLOT.index(): {
      auto* dateFromTime = MDateFromTime::New(alloc(), utcTime);
      ins->block()->insertBefore(ins, dateFromTime);
      replacement = dateFromTime;
      break;
    }
    default:
      MOZ_CRASH("unexpected slot");
  }

  // Replace the load.
  ins->replaceAllUsesWith(replacement);

  // Remove the load.
  ins->block()->discard(ins);
}

void DateObjectReplacer::visitDateFillLocalTimeSlots(
    MDateFillLocalTimeSlots* ins) {
  // Skip fill on other objects.
  if (ins->date() != dateObject_) {
    return;
  }

  // Remove the fill without any replacement.
  ins->block()->discard(ins);
}

// Returns false if the Date object does not escape.
bool DateObjectReplacer::escapes(MInstruction* ins) {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check Date object\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // Check all uses to see whether they can be supported without allocating a
  // DateObject.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize if
    // it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape, "Observable date object cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::GuardShape: {
        auto* guard = def->toGuardShape();
        if (templateObject()->shape() != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (escapes(def->toInstruction())) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::LoadFixedSlot: {
        auto* load = def->toLoadFixedSlot();

        switch (load->slot()) {
          case DateObject::UTC_TIME_SLOT.index():
            // We can replace loading the UTC time slot.
            break;
          case DateObject::LOCAL_YEAR_SLOT.index():
          case DateObject::LOCAL_MONTH_SLOT.index():
          case DateObject::LOCAL_DATE_SLOT.index():
            // We can replace loading these date component slots. Only allow a
            // single load, because it's probably more efficient to use the
            // cached components in the Date object if multiple loads happen.
            if (!hasSeenDateComponent_) {
              hasSeenDateComponent_ = true;
              break;
            }
            [[fallthrough]];
          default:
            JitSpew(JitSpew_Escape,
                    "is escaped by unsupported LoadFixedSlot\n");
            return true;
        }
        break;
      }

      // No-op for scalar replaced date objects.
      case MDefinition::Opcode::DateFillLocalTimeSlots:
        break;

      // This instruction is a no-op used to test that scalar replacement is
      // working as expected.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Date object is not escaped");
  return false;
}

bool DateObjectReplacer::run() {
  MBasicBlock* startBlock = dateObject_->block();

  // Iterate over each basic block.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel("Scalar replacement of new Date Objects")) {
      return false;
    }

    // Iterates over phis and instructions.
    // We do not have to visit resume points. Any resume points that capture the
    // new Date object will be handled by the Sink pass.
    for (MDefinitionIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the visit
      // function might discard itself from the basic block.
      MDefinition* def = *iter++;
      switch (def->op()) {
#define MIR_OP(op)              \
  case MDefinition::Opcode::op: \
    visit##op(def->to##op());   \
    break;
        MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
      }
      if (!graph_.alloc().ensureBallast()) {
        return false;
      }
    }
  }

  assertSuccess();
  return true;
}

void DateObjectReplacer::assertSuccess() const {
  MOZ_ASSERT(dateObject_->canRecoverOnBailout());
  MOZ_ASSERT(!dateObject_->hasLiveDefUses());
}

// WebAssembly only supports scalar replacement of structs with only inline
// data for now.
static inline bool IsOptimizableWasmStructInstruction(MInstruction* ins) {
  return ins->isWasmNewStructObject() &&
         !ins->toWasmNewStructObject()->isOutline();
}

class WasmStructMemoryView : public MDefinitionVisitorDefaultNoop {
 public:
  using BlockState = MWasmStructState;
  static const char phaseName[];

 private:
  TempAllocator& alloc_;
  MWasmNewStructObject* struct_;
  MConstant* undefinedVal_;
  MBasicBlock* startBlock_;
  BlockState* state_;

  bool oom_;

 public:
  WasmStructMemoryView(TempAllocator& alloc, MWasmNewStructObject* wasmStruct);

  MBasicBlock* startingBlock();
  bool initStartingState(BlockState** pState);

  void setEntryBlockState(BlockState* state);
  bool mergeIntoSuccessorState(MBasicBlock* curr, MBasicBlock* succ,
                               BlockState** pSuccState);

  void assertSuccess();

  bool oom() const { return oom_; }

 public:
  void visitResumePoint(MResumePoint* rp);
  void visitPhi(MPhi* ins);
  void visitWasmStoreField(MWasmStoreField* ins);
  void visitWasmStoreFieldRef(MWasmStoreFieldRef* ins);
  void visitWasmLoadField(MWasmLoadField* ins);
  void visitWasmPostWriteBarrierWholeCell(MWasmPostWriteBarrierWholeCell* ins);
};

void WasmStructMemoryView::setEntryBlockState(BlockState* state) {
  state_ = state;
}

void WasmStructMemoryView::assertSuccess() {
  // Make sure that the undefined value used as a placeholder is not used.
  MOZ_RELEASE_ASSERT(!undefinedVal_->hasUses());

  // Make sure that the MWasmNewStruct instruction is not used anymore.
  MOZ_RELEASE_ASSERT(!struct_->hasUses());
}

MBasicBlock* WasmStructMemoryView::startingBlock() { return startBlock_; }

bool WasmStructMemoryView::initStartingState(BlockState** pState) {
  // We need this undefined value to initialize phi inputs if we create some
  // later.
  undefinedVal_ = MConstant::NewUndefined(alloc_);

  // Create a new block state and insert at it at the location of the new
  // struct.
  BlockState* state = BlockState::New(alloc_, struct_);
  if (!state) {
    return false;
  }

  *pState = state;
  return true;
}

// Return true if all phi operands are equal to |newStruct|.
static bool WasmStructPhiOperandsEqualTo(MPhi* phi, MInstruction* newStruct) {
  MOZ_ASSERT(IsOptimizableWasmStructInstruction(newStruct));

  for (size_t i = 0; i < phi->numOperands(); i++) {
    if (!PhiOperandEqualTo(phi->getOperand(i), newStruct)) {
      return false;
    }
  }
  return true;
}

void WasmStructMemoryView::visitPhi(MPhi* ins) {
  // Skip phis on other objects.
  if (!WasmStructPhiOperandsEqualTo(ins, struct_)) {
    return;
  }

  // Replace the phi by its object.
  ins->replaceAllUsesWith(struct_);

  // Remove original instruction.
  ins->block()->discardPhi(ins);
}

// We need to define this method for the pass to work,
// but we don't have resume points in wasm.
void WasmStructMemoryView::visitResumePoint(MResumePoint* rp) {}

void WasmStructMemoryView::visitWasmStoreField(MWasmStoreField* ins) {
  // Skip stores made on other structs.
  MDefinition* base = ins->base();
  if (base != struct_) {
    return;
  }

  // Clone the state and update the field value.
  state_ = BlockState::Copy(alloc_, state_);
  if (!state_) {
    oom_ = true;
    return;
  }

  // Update the state
  state_->setField(ins->structFieldIndex().value(), ins->value());

  // Remove original instruction.
  ins->block()->discard(ins);
}

void WasmStructMemoryView::visitWasmStoreFieldRef(MWasmStoreFieldRef* ins) {
  // Skip stores made on other structs.
  MDefinition* base = ins->base();
  if (base != struct_) {
    return;
  }

  // Clone the state and update the field value.
  state_ = BlockState::Copy(alloc_, state_);
  if (!state_) {
    oom_ = true;
    return;
  }

  // Update the state
  state_->setField(ins->structFieldIndex().value(), ins->value());

  // Remove original instruction.
  ins->block()->discard(ins);
}

void WasmStructMemoryView::visitWasmLoadField(MWasmLoadField* ins) {
  // Skip loads made on other structs.
  MDefinition* base = ins->base();
  if (base != struct_) {
    return;
  }

  MDefinition* value = state_->getField(ins->structFieldIndex().value());

  // Packed fields (i8/i16) require widening. Since we're eliding the load,
  // insert MIR to apply the equivalent widening operation.
  MWideningOp wideningOp = ins->wideningOp();
  if (wideningOp != MWideningOp::None) {
    // Widening only goes to Int32.
    MOZ_ASSERT(ins->type() == MIRType::Int32);

    MBasicBlock* block = ins->block();
    switch (wideningOp) {
      case MWideningOp::FromU8:
      case MWideningOp::FromU16: {
        int32_t maskVal = wideningOp == MWideningOp::FromU8 ? 0xFF : 0xFFFF;
        auto* mask = MConstant::NewInt32(alloc_, maskVal);
        if (!mask) {
          oom_ = true;
          return;
        }
        block->insertBefore(ins, mask);
        auto* widened = MBitAnd::New(alloc_, value, mask, MIRType::Int32);
        if (!widened) {
          oom_ = true;
          return;
        }
        block->insertBefore(ins, widened);
        value = widened;
        break;
      }
      case MWideningOp::FromS8:
      case MWideningOp::FromS16: {
        int32_t shiftAmount = wideningOp == MWideningOp::FromS8 ? 24 : 16;
        auto* shift = MConstant::NewInt32(alloc_, shiftAmount);
        if (!shift) {
          oom_ = true;
          return;
        }
        block->insertBefore(ins, shift);
        auto* lsh = MLsh::New(alloc_, value, shift, MIRType::Int32);
        if (!lsh) {
          oom_ = true;
          return;
        }
        block->insertBefore(ins, lsh);
        auto* widened = MRsh::New(alloc_, lsh, shift, MIRType::Int32);
        if (!widened) {
          oom_ = true;
          return;
        }
        block->insertBefore(ins, widened);
        value = widened;
        break;
      }
      default:
        MOZ_CRASH("Unexpected widening op");
    }
  }

  // Replace load by the (possibly widened) field value.
  ins->replaceAllUsesWith(value);

  // Remove original instruction.
  ins->block()->discard(ins);
}

void WasmStructMemoryView::visitWasmPostWriteBarrierWholeCell(
    MWasmPostWriteBarrierWholeCell* ins) {
  // Skip loads made on other objects.
  if (ins->object() != struct_) {
    return;
  }

  // Remove original instruction.
  ins->block()->discard(ins);
}

bool WasmStructMemoryView::mergeIntoSuccessorState(MBasicBlock* curr,
                                                   MBasicBlock* succ,
                                                   BlockState** pSuccState) {
  BlockState* succState = *pSuccState;

  // When a block has no state yet, create an empty one for the
  // successor.
  if (!succState) {
    // If the successor is not dominated then the struct cannot flow
    // in this basic block without a Phi.  We know that no Phi exist
    // in non-dominated successors as the conservative escaped
    // analysis fails otherwise.  Such condition can succeed if the
    // successor is a join at the end of a if-block and the struct
    // only exists within the branch.
    if (!startBlock_->dominates(succ)) {
      return true;
    }

    // If there is only one predecessor, carry over the last state of the
    // block to the successor. As the block state is immutable, if the
    // current block has multiple successors, they will share the same entry
    // state.
    if (succ->numPredecessors() <= 1 || !state_->numFields()) {
      *pSuccState = state_;
      return true;
    }

    // If we have multiple predecessors, then we allocate one Phi node for
    // each predecessor, and create a new block state which only has phi
    // nodes. These would later be removed by the removal of redundant phi
    // nodes.
    succState = BlockState::Copy(alloc_, state_);
    if (!succState) {
      return false;
    }

    size_t numPreds = succ->numPredecessors();
    for (size_t index = 0; index < state_->numFields(); index++) {
      MPhi* phi = MPhi::New(alloc_.fallible());
      if (!phi || !phi->reserveLength(numPreds)) {
        return false;
      }

      // Fill the input of the successors Phi with undefined
      // values, and each block later fills the Phi inputs.
      for (size_t p = 0; p < numPreds; p++) {
        phi->addInput(undefinedVal_);
      }

      // Add Phi in the list of Phis of the basic block.
      succ->addPhi(phi);

      // Set the result type of this phi depending on the previous fields.
      phi->setResultType(succState->getField(index)->type());
      succState->setField(index, phi);
    }

    *pSuccState = succState;
  }

  MOZ_ASSERT_IF(succ == startBlock_, startBlock_->isLoopHeader());
  if (succ->numPredecessors() > 1 && succState->numFields() &&
      succ != startBlock_) {
    // We need to re-compute successorWithPhis as the previous EliminatePhis
    // phase might have removed all the Phis from the successor block.
    size_t currIndex;
    MOZ_ASSERT(!succ->phisEmpty());
    if (curr->successorWithPhis()) {
      MOZ_ASSERT(curr->successorWithPhis() == succ);
      currIndex = curr->positionInPhiSuccessor();
    } else {
      currIndex = succ->indexForPredecessor(curr);
      curr->setSuccessorWithPhis(succ, currIndex);
    }
    MOZ_ASSERT(succ->getPredecessor(currIndex) == curr);

    // Copy the current element states to the index of current block in all
    // the Phi created during the first visit of the successor.
    for (size_t index = 0; index < state_->numFields(); index++) {
      MPhi* phi = succState->getField(index)->toPhi();
      phi->replaceOperand(currIndex, state_->getField(index));
    }
  }

  return true;
}

// Returns False if the wasm struct is not escaped and if it is optimizable by
// ScalarReplacementOfStruct.
static bool IsWasmStructEscaped(MDefinition* ins, MInstruction* newStruct) {
  MOZ_ASSERT(ins->type() == MIRType::WasmAnyRef);
  MOZ_ASSERT(IsOptimizableWasmStructInstruction(newStruct));

  JitSpewDef(JitSpew_Escape, "Check wasm struct\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // Don't do scalar replacement on big structs.
  if (newStruct->isWasmNewStructObject()) {
    if (newStruct->toWasmNewStructObject()->structType().fields_.length() >
        wasm::MaxFieldsScalarReplacementStructs) {
      JitSpew(JitSpew_Escape, "struct too big for scalar replacement\n");
      return true;
    }
  }

  // Check if the struct is escaped. If the object is not the first argument
  // of either a known Store / Load, then we consider it as escaped. This is a
  // cheap and conservative escape analysis.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    if (!consumer->isDefinition()) {
      JitSpew(JitSpew_Escape, "Wasm struct is escaped");
      return true;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      // This instruction can only store primitive types.
      // Another struct won't be able to escape through it.
      case MDefinition::Opcode::WasmStoreField: {
        break;
      }
      case MDefinition::Opcode::WasmStoreFieldRef: {
        // Escaped if it's stored into another struct.
        if (def->toWasmStoreFieldRef()->value() == ins) {
          JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
          return true;
        }
        break;
      }
      // Not escaped if we load into a field of this struct.
      case MDefinition::Opcode::WasmLoadField: {
        break;
      }
      // Handle phis.
      case MDefinition::Opcode::Phi: {
        auto* phi = def->toPhi();
        if (!WasmStructPhiOperandsEqualTo(phi, newStruct)) {
          JitSpewDef(JitSpew_Escape, "has different phi operands\n", def);
          return true;
        }
        if (IsWasmStructEscaped(phi, newStruct)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::WasmPostWriteBarrierWholeCell: {
        break;
      }

      // By default, we consider the struct as escaped.
      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Struct is not escaped");
  return false;
}

/* static */ const char WasmStructMemoryView::phaseName[] =
    "Scalar Replacement of wasm structs";

WasmStructMemoryView::WasmStructMemoryView(TempAllocator& alloc,
                                           MWasmNewStructObject* wasmStruct)
    : alloc_(alloc),
      struct_(wasmStruct),
      undefinedVal_(nullptr),
      startBlock_(wasmStruct->block()),
      state_(nullptr),
      oom_(false) {}

static inline bool IsOptimizableObjectKeysInstruction(MInstruction* ins) {
  return ins->isObjectKeys();
}

class ObjectKeysReplacer : public GenericArrayReplacer {
 private:
  const MIRGenerator* mir_;
  MIRGraph& graph_;
  MObjectToIterator* objToIter_ = nullptr;

  MObjectKeys* objectKeys() const { return arr_->toObjectKeys(); }

  void visitLength(MInstruction* ins, MDefinition* elements);

  void visitLoadElement(MLoadElement* ins);
  void visitArrayLength(MArrayLength* ins);
  void visitInitializedLength(MInitializedLength* ins);

  bool escapes(MElements* ins);

 public:
  ObjectKeysReplacer(const MIRGenerator* mir, MIRGraph& graph,
                     MInstruction* arr)
      : GenericArrayReplacer(graph.alloc(), arr), mir_(mir), graph_(graph) {
    MOZ_ASSERT(IsOptimizableObjectKeysInstruction(arr_));
  }

  bool escapes(MInstruction* ins);
  bool run(MInstructionIterator& outerIterator);
  void assertSuccess();
};

// Returns false if the Object.keys array object does not escape.
bool ObjectKeysReplacer::escapes(MInstruction* ins) {
  MOZ_ASSERT(ins->type() == MIRType::Object);

  JitSpewDef(JitSpew_Escape, "Check Object.keys array\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  // Check all uses to see whether they can be supported without allocating an
  // ArrayObject for the Object.keys parameter.
  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    MNode* consumer = (*i)->consumer();

    // If a resume point can observe this instruction, we can only optimize
    // if it is recoverable.
    if (consumer->isResumePoint()) {
      if (!consumer->toResumePoint()->isRecoverableOperand(*i)) {
        JitSpew(JitSpew_Escape,
                "Observable Object.keys array cannot be recovered");
        return true;
      }
      continue;
    }

    MDefinition* def = consumer->toDefinition();
    switch (def->op()) {
      case MDefinition::Opcode::Elements: {
        auto* elem = def->toElements();
        MOZ_ASSERT(elem->object() == ins);
        if (escapes(elem)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardShape: {
        const Shape* shape = objectKeys()->resultShape();
        MOZ_DIAGNOSTIC_ASSERT(shape);
        auto* guard = def->toGuardShape();
        if (shape != guard->shape()) {
          JitSpewDef(JitSpew_Escape, "has a non-matching guard shape\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardToClass: {
        auto* guard = def->toGuardToClass();
        if (guard->getClass() != &ArrayObject::class_) {
          JitSpewDef(JitSpew_Escape, "has a non-matching class guard\n", def);
          return true;
        }
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::GuardArrayIsPacked: {
        // Object.keys arrays are always packed as long as they aren't modified.
        auto* guard = def->toGuardArrayIsPacked();
        if (escapes(guard)) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      case MDefinition::Opcode::Unbox: {
        if (def->type() != MIRType::Object) {
          JitSpewDef(JitSpew_Escape, "has an invalid unbox\n", def);
          return true;
        }
        if (escapes(def->toInstruction())) {
          JitSpewDef(JitSpew_Escape, "is indirectly escaped by\n", def);
          return true;
        }
        break;
      }

      // This instruction is supported for |JSOp::OptimizeSpreadCall|.
      case MDefinition::Opcode::Compare: {
        bool canFold;
        if (!def->toCompare()->tryFold(&canFold)) {
          JitSpewDef(JitSpew_Escape, "has an unsupported compare\n", def);
          return true;
        }
        break;
      }

      // This instruction is a no-op used to test that scalar replacement is
      // working as expected.
      case MDefinition::Opcode::AssertRecoveredOnBailout:
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Object.keys array object is not escaped");
  return false;
}

bool ObjectKeysReplacer::escapes(MElements* ins) {
  JitSpewDef(JitSpew_Escape, "Check Object.keys array elements\n", ins);
  JitSpewIndent spewIndent(JitSpew_Escape);

  for (MUseIterator i(ins->usesBegin()); i != ins->usesEnd(); i++) {
    // The MIRType::Elements cannot be captured in a resume point as it does not
    // represent a value allocation.
    MDefinition* def = (*i)->consumer()->toDefinition();

    switch (def->op()) {
      case MDefinition::Opcode::LoadElement: {
        MOZ_ASSERT(def->toLoadElement()->elements() == ins);
        break;
      }

      case MDefinition::Opcode::ArrayLength:
        MOZ_ASSERT(def->toArrayLength()->elements() == ins);
        break;

      case MDefinition::Opcode::InitializedLength:
        MOZ_ASSERT(def->toInitializedLength()->elements() == ins);
        break;

      case MDefinition::Opcode::GuardElementsArePacked:
        MOZ_ASSERT(def->toGuardElementsArePacked()->elements() == ins);
        break;

      default:
        JitSpewDef(JitSpew_Escape, "is escaped by\n", def);
        return true;
    }
  }

  JitSpew(JitSpew_Escape, "Object.keys array object is not escaped");
  return false;
}

bool ObjectKeysReplacer::run(MInstructionIterator& outerIterator) {
  MBasicBlock* startBlock = arr_->block();

  objToIter_ = MObjectToIterator::New(alloc_, objectKeys()->object(), nullptr);
  objToIter_->setSkipRegistration(true);
  objToIter_->stealResumePoint(arr_);
  arr_->block()->insertBefore(arr_, objToIter_);

  // Iterate over each basic block.
  for (ReversePostorderIterator block = graph_.rpoBegin(startBlock);
       block != graph_.rpoEnd(); block++) {
    if (mir_->shouldCancel("Scalar replacement of Object.keys array object")) {
      return false;
    }

    // Iterates over phis and instructions.
    // We do not have to visit resume points. Any resume points that capture the
    // Object.keys array object will be handled by the Sink pass.
    for (MDefinitionIterator iter(*block); iter;) {
      // Increment the iterator before visiting the instruction, as the visit
      // function might discard itself from the basic block.
      MDefinition* def = *iter++;
      switch (def->op()) {
#define MIR_OP(op)              \
  case MDefinition::Opcode::op: \
    visit##op(def->to##op());   \
    break;
        MIR_OPCODE_LIST(MIR_OP)
#undef MIR_OP
      }
      if (!graph_.alloc().ensureBallast()) {
        return false;
      }
    }
  }

  assertSuccess();

  auto* forRecovery = MObjectKeysFromIterator::New(alloc_, objToIter_);
  arr_->block()->insertBefore(arr_, forRecovery);

  auto* nop = MNop::New(alloc_);
  arr_->block()->insertBefore(arr_, nop);
  if (!nop->copyResumePointFrom(alloc_, objToIter_)) {
    return false;
  }

  {
    // Use the PropertyIteratorObject in the resume point for the
    // MObjectToIterator instruction. If this instruction calls a VM function
    // that triggers an invalidation, we use ResumeMode::ResumeAfterObjectKeys
    // to create the array from the iterator object when we bail out.
    MResumePoint* rp = objToIter_->resumePoint();
    size_t n = rp->numOperands() - 1;
    for (size_t i = 0; i < n; i++) {
      MOZ_RELEASE_ASSERT(rp->getOperand(i) != arr_);
    }
    MOZ_RELEASE_ASSERT(rp->getOperand(n) == arr_);
    rp->replaceOperand(n, objToIter_);
    MOZ_RELEASE_ASSERT(rp->mode() == ResumeMode::ResumeAfter);
    rp->setMode(ResumeMode::ResumeAfterObjectKeys);
  }
  arr_->replaceAllUsesWith(forRecovery);

  // We need to explicitly discard the instruction since it's marked as
  // effectful and we stole its resume point, which will trip assertion
  // failures later. We can't discard the instruction out from underneath
  // the iterator though, and we can't do the trick where we increment the
  // iterator at the top of the loop because we might discard the *next*
  // instruction, so we do this goofiness.
  outerIterator--;
  arr_->block()->discard(arr_);

  if (!graph_.alloc().ensureBallast()) {
    return false;
  }

  return true;
}

void ObjectKeysReplacer::assertSuccess() {
  MOZ_ASSERT(!arr_->hasLiveDefUses());
}

void ObjectKeysReplacer::visitLoadElement(MLoadElement* ins) {
  if (!isTargetElements(ins->elements())) {
    return;
  }

  auto* load = MLoadIteratorElement::New(alloc_, objToIter_, ins->index());
  ins->block()->insertBefore(ins, load);

  ins->replaceAllUsesWith(load);
  discardInstruction(ins, ins->elements());
}

void ObjectKeysReplacer::visitLength(MInstruction* ins, MDefinition* elements) {
  if (!isTargetElements(elements)) {
    return;
  }

  auto* newLen = MIteratorLength::New(alloc_, objToIter_);
  ins->block()->insertBefore(ins, newLen);

  ins->replaceAllUsesWith(newLen);
  discardInstruction(ins, elements);
}

void ObjectKeysReplacer::visitArrayLength(MArrayLength* ins) {
  visitLength(ins, ins->elements());
}

void ObjectKeysReplacer::visitInitializedLength(MInitializedLength* ins) {
  // The initialized length of an Object.keys array is equal to its length.
  visitLength(ins, ins->elements());
}

bool ScalarReplacement(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_Escape, "Begin (ScalarReplacement)");

  EmulateStateOf<ObjectMemoryView> replaceObject(mir, graph);
  EmulateStateOf<ArrayMemoryView> replaceArray(mir, graph);
  EmulateStateOf<WasmStructMemoryView> replaceWasmStructs(mir, graph);
  bool addedPhi = false;

  for (ReversePostorderIterator block = graph.rpoBegin();
       block != graph.rpoEnd(); block++) {
    if (mir->shouldCancel("Scalar Replacement (main loop)")) {
      return false;
    }

    for (MInstructionIterator ins = block->begin(); ins != block->end();
         ins++) {
      if (IsOptimizableObjectInstruction(*ins) &&
          !IsObjectEscaped(*ins, *ins)) {
        ObjectMemoryView view(graph.alloc(), *ins);
        if (!replaceObject.run(view)) {
          return false;
        }
        view.assertSuccess();
        addedPhi = true;
        continue;
      }

      if (IsOptimizableArrayInstruction(*ins) && !IsArrayEscaped(*ins, *ins)) {
        ArrayMemoryView view(graph.alloc(), *ins);
        if (!replaceArray.run(view)) {
          return false;
        }
        view.assertSuccess();
        addedPhi = true;
        continue;
      }

      if (IsOptimizableArgumentsInstruction(*ins)) {
        ArgumentsReplacer replacer(mir, graph, *ins);
        if (replacer.escapes(*ins)) {
          continue;
        }
        if (!replacer.run()) {
          return false;
        }
        continue;
      }

      if (IsOptimizableRestInstruction(*ins)) {
        RestReplacer replacer(mir, graph, *ins);
        if (replacer.escapes(*ins)) {
          continue;
        }
        if (!replacer.run()) {
          return false;
        }
        continue;
      }

      if (IsOptimizableSubarrayInstruction(*ins)) {
        SubarrayReplacer replacer(mir, graph, *ins);
        if (replacer.escapes(*ins)) {
          continue;
        }
        if (!replacer.run()) {
          return false;
        }
        continue;
      }

      if (IsOptimizableNewDateObjectInstruction(*ins)) {
        DateObjectReplacer replacer(mir, graph, *ins);
        if (replacer.escapes(*ins)) {
          continue;
        }
        if (!replacer.run()) {
          return false;
        }
        continue;
      }

      if (IsOptimizableWasmStructInstruction(*ins) &&
          !IsWasmStructEscaped(*ins, *ins)) {
        WasmStructMemoryView view(graph.alloc(), ins->toWasmNewStructObject());
        if (!replaceWasmStructs.run(view)) {
          return false;
        }
        view.assertSuccess();
        addedPhi = true;
        continue;
      }
    }
  }

  if (addedPhi) {
    // Phis added by Scalar Replacement are only redundant Phis which are
    // not directly captured by any resume point but only by the MDefinition
    // state. The conservative observability only focuses on Phis which are
    // not used as resume points operands.
    AssertExtendedGraphCoherency(graph);
    if (!EliminatePhis(mir, graph, ConservativeObservability)) {
      return false;
    }
  }

  return true;
}

bool ReplaceObjectKeys(const MIRGenerator* mir, MIRGraph& graph) {
  JitSpew(JitSpew_Escape, "Begin (Object.Keys Replacement)");

  for (ReversePostorderIterator block = graph.rpoBegin();
       block != graph.rpoEnd(); block++) {
    if (mir->shouldCancel("Object.Keys Replacement (main loop)")) {
      return false;
    }

    for (MInstructionIterator ins = block->begin(); ins != block->end();
         ins++) {
      if (IsOptimizableObjectKeysInstruction(*ins)) {
        ObjectKeysReplacer replacer(mir, graph, *ins);
        if (replacer.escapes(*ins)) {
          continue;
        }
        if (!replacer.run(ins)) {
          return false;
        }
        continue;
      }
    }
  }

  return true;
}

} /* namespace jit */
} /* namespace js */
