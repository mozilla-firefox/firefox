/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/FoldTests.h"

#include "jit/IonAnalysis.h"
#include "jit/MIRGraph.h"

using namespace js;
using namespace js::jit;

// Determine whether phiBlock/testBlock simply compute a phi and perform a
// test on it.
static bool BlockIsSingleTest(MBasicBlock* phiBlock, MBasicBlock* testBlock,
                              MPhi** pphi, MTest** ptest) {
  *pphi = nullptr;
  *ptest = nullptr;

  if (phiBlock != testBlock) {
    MOZ_RELEASE_ASSERT(phiBlock->lastIns()->isGoto());
    MOZ_RELEASE_ASSERT(phiBlock->lastIns()->toGoto()->target() == testBlock);
    MOZ_RELEASE_ASSERT(testBlock->numPredecessors() == 1);
    if (!phiBlock->begin()->isGoto()) {
      return false;
    }
  }

  auto iter = testBlock->rbegin();
  if (!iter->isTest()) {
    return false;
  }
  MTest* test = iter->toTest();

  // Unwrap boolean conversion performed through the '!!' idiom.
  MInstruction* testOrNot = test;
  bool hasOddNumberOfNots = false;
  while (++iter != testBlock->rend()) {
    if (iter->isNot()) {
      // The MNot must only be used by |testOrNot|.
      auto* notIns = iter->toNot();
      if (testOrNot->getOperand(0) != notIns) {
        return false;
      }
      if (!notIns->hasOneUse()) {
        return false;
      }

      testOrNot = notIns;
      hasOddNumberOfNots = !hasOddNumberOfNots;
    } else {
      // Fail if there are any other instructions than MNot.
      return false;
    }
  }

  // There's an odd number of MNot, so this can't be the '!!' idiom.
  if (hasOddNumberOfNots) {
    return false;
  }

  MOZ_ASSERT(testOrNot->isTest() || testOrNot->isNot());

  MDefinition* testInput = testOrNot->getOperand(0);
  if (!testInput->isPhi()) {
    return false;
  }
  MPhi* phi = testInput->toPhi();
  if (phi->block() != phiBlock) {
    return false;
  }

  for (MUseIterator iter = phi->usesBegin(); iter != phi->usesEnd(); ++iter) {
    MUse* use = *iter;
    if (use->consumer() == testOrNot) {
      continue;
    }
    if (use->consumer()->isResumePoint()) {
      MBasicBlock* useBlock = use->consumer()->block();
      if (useBlock == phiBlock || useBlock == testBlock) {
        continue;
      }
    }
    return false;
  }

  for (MPhiIterator iter = phiBlock->phisBegin(); iter != phiBlock->phisEnd();
       ++iter) {
    if (*iter != phi) {
      return false;
    }
  }

  if (phiBlock != testBlock && !testBlock->phisEmpty()) {
    return false;
  }

  *pphi = phi;
  *ptest = test;

  return true;
}

// Determine if value is directly or indirectly the test input.
static bool IsTestInputMaybeToBool(MTest* test, MDefinition* value) {
  auto* input = test->input();
  bool hasEvenNumberOfNots = true;
  while (true) {
    // Only accept if there's an even number of MNot.
    if (input == value && hasEvenNumberOfNots) {
      return true;
    }

    // Unwrap boolean conversion performed through the '!!' idiom.
    if (input->isNot()) {
      input = input->toNot()->input();
      hasEvenNumberOfNots = !hasEvenNumberOfNots;
      continue;
    }

    return false;
  }
}

// |phi| is the input of an MTest instruction we're folding away. Discard it
// and mark its operands as implicitly-used, because Baseline may still read
// them after a bailout.
static void DiscardTestedPhi(MPhi* phi) {
  for (size_t i = 0; i < phi->numOperands(); i++) {
    phi->getOperand(i)->setImplicitlyUsedUnchecked();
  }
  phi->block()->discardPhi(phi);
}

// Change |block| so that it ends in a goto to the specific |target| block.
// |existingPred| is an existing predecessor of the block.
[[nodiscard]] static bool UpdateGotoSuccessor(TempAllocator& alloc,
                                              MBasicBlock* block,
                                              MBasicBlock* target,
                                              MBasicBlock* existingPred) {
  MInstruction* ins = block->lastIns();
  MOZ_RELEASE_ASSERT(ins->isGoto());
  ins->toGoto()->target()->removePredecessor(block);
  block->discardLastIns();

  MGoto* newGoto = MGoto::New(alloc, target);
  block->end(newGoto);

  return target->addPredecessorSameInputsAs(block, existingPred);
}

// Change block so that it ends in a test of the specified value, going to
// either ifTrue or ifFalse. existingPred is an existing predecessor of ifTrue
// or ifFalse with the same values incoming to ifTrue/ifFalse as block.
// existingPred is not required to be a predecessor of ifTrue/ifFalse if block
// already ends in a test going to that block on a true/false result.
[[nodiscard]] static bool UpdateTestSuccessors(
    TempAllocator& alloc, MBasicBlock* block, MDefinition* value,
    MBasicBlock* ifTrue, MBasicBlock* ifFalse, MBasicBlock* existingPred) {
  MInstruction* ins = block->lastIns();
  if (ins->isTest()) {
    MTest* test = ins->toTest();
    MOZ_RELEASE_ASSERT(test->input() == value);

    if (ifTrue != test->ifTrue()) {
      test->ifTrue()->removePredecessor(block);
      if (!ifTrue->addPredecessorSameInputsAs(block, existingPred)) {
        return false;
      }
      test->replaceSuccessor(MTest::TrueBranchIndex, ifTrue);
    }

    if (ifFalse != test->ifFalse()) {
      test->ifFalse()->removePredecessor(block);
      if (!ifFalse->addPredecessorSameInputsAs(block, existingPred)) {
        return false;
      }
      test->replaceSuccessor(MTest::FalseBranchIndex, ifFalse);
    }

    return true;
  }

  MOZ_RELEASE_ASSERT(ins->isGoto());
  ins->toGoto()->target()->removePredecessor(block);
  block->discardLastIns();

  MTest* test = MTest::New(alloc, value, ifTrue, ifFalse);
  block->end(test);

  if (!ifTrue->addPredecessorSameInputsAs(block, existingPred)) {
    return false;
  }
  if (!ifFalse->addPredecessorSameInputsAs(block, existingPred)) {
    return false;
  }
  return true;
}

/*
 * Look for a diamond pattern:
 *
 *        initialBlock
 *          /     \
 *  trueBranch  falseBranch
 *          \     /
 *          phiBlock
 *             |
 *         testBlock
 */
static bool IsDiamondPattern(MBasicBlock* initialBlock) {
  MInstruction* ins = initialBlock->lastIns();
  if (!ins->isTest()) {
    return false;
  }
  MTest* initialTest = ins->toTest();

  MBasicBlock* trueBranch = initialTest->ifTrue();
  if (trueBranch->numPredecessors() != 1 || !trueBranch->lastIns()->isGoto()) {
    return false;
  }

  MBasicBlock* falseBranch = initialTest->ifFalse();
  if (falseBranch->numPredecessors() != 1 ||
      !falseBranch->lastIns()->isGoto()) {
    return false;
  }

  MBasicBlock* phiBlock = trueBranch->lastIns()->toGoto()->target();
  if (phiBlock != falseBranch->lastIns()->toGoto()->target()) {
    return false;
  }
  if (phiBlock->numPredecessors() != 2) {
    return false;
  }
  return true;
}

[[nodiscard]] static bool MaybeFoldDiamondConditionBlock(
    MIRGraph& graph, MBasicBlock* initialBlock) {
  MOZ_ASSERT(IsDiamondPattern(initialBlock));

  // Optimize the MIR graph to improve the code generated for conditional
  // operations. A test like 'if (a ? b : c)' normally requires four blocks,
  // with a phi for the intermediate value. This can be improved to use three
  // blocks with no phi value.

  /*
   * Look for a diamond pattern:
   *
   *        initialBlock
   *          /     \
   *  trueBranch  falseBranch
   *          \     /
   *          phiBlock
   *             |
   *         testBlock
   *
   * Where phiBlock contains a single phi combining values pushed onto the
   * stack by trueBranch and falseBranch, and testBlock contains a test on
   * that phi. phiBlock and testBlock may be the same block; generated code
   * will use different blocks if the (?:) op is in an inlined function.
   */

  MTest* initialTest = initialBlock->lastIns()->toTest();

  MBasicBlock* trueBranch = initialTest->ifTrue();
  MBasicBlock* falseBranch = initialTest->ifFalse();
  if (initialBlock->isLoopBackedge() || trueBranch->isLoopBackedge() ||
      falseBranch->isLoopBackedge()) {
    return true;
  }

  MBasicBlock* phiBlock = trueBranch->lastIns()->toGoto()->target();
  MBasicBlock* testBlock = phiBlock;
  if (testBlock->lastIns()->isGoto()) {
    if (testBlock->isLoopBackedge()) {
      return true;
    }
    testBlock = testBlock->lastIns()->toGoto()->target();
    if (testBlock->numPredecessors() != 1) {
      return true;
    }
  }

  MPhi* phi;
  MTest* finalTest;
  if (!BlockIsSingleTest(phiBlock, testBlock, &phi, &finalTest)) {
    return true;
  }

  MOZ_RELEASE_ASSERT(phi->numOperands() == 2);

  // Make sure the test block does not have any outgoing loop backedges.
  if (!SplitCriticalEdgesForBlock(graph, testBlock)) {
    return false;
  }

  MDefinition* trueResult =
      phi->getOperand(phiBlock->indexForPredecessor(trueBranch));
  MDefinition* falseResult =
      phi->getOperand(phiBlock->indexForPredecessor(falseBranch));

  // OK, we found the desired pattern, now transform the graph.

  // Remove the phi from phiBlock.
  DiscardTestedPhi(phi);

  // Change the end of the block to a test that jumps directly to successors of
  // testBlock, rather than to testBlock itself.

  if (IsTestInputMaybeToBool(initialTest, trueResult)) {
    if (!UpdateGotoSuccessor(graph.alloc(), trueBranch, finalTest->ifTrue(),
                             testBlock)) {
      return false;
    }
  } else {
    if (!UpdateTestSuccessors(graph.alloc(), trueBranch, trueResult,
                              finalTest->ifTrue(), finalTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  }

  if (IsTestInputMaybeToBool(initialTest, falseResult)) {
    if (!UpdateGotoSuccessor(graph.alloc(), falseBranch, finalTest->ifFalse(),
                             testBlock)) {
      return false;
    }
  } else {
    if (!UpdateTestSuccessors(graph.alloc(), falseBranch, falseResult,
                              finalTest->ifTrue(), finalTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  }

  // Remove phiBlock, if different from testBlock.
  if (phiBlock != testBlock) {
    testBlock->removePredecessor(phiBlock);
    graph.removeBlock(phiBlock);
  }

  // Remove testBlock itself.
  finalTest->ifTrue()->removePredecessor(testBlock);
  finalTest->ifFalse()->removePredecessor(testBlock);
  graph.removeBlock(testBlock);

  return true;
}

/*
 * Look for a triangle pattern:
 *
 *        initialBlock
 *          /     \
 *  trueBranch     |
 *          \     /
 *     phiBlock+falseBranch
 *             |
 *         testBlock
 *
 * Or:
 *
 *        initialBlock
 *          /     \
 *         |    falseBranch
 *          \     /
 *     phiBlock+trueBranch
 *             |
 *         testBlock
 */
static bool IsTrianglePattern(MBasicBlock* initialBlock) {
  MInstruction* ins = initialBlock->lastIns();
  if (!ins->isTest()) {
    return false;
  }
  MTest* initialTest = ins->toTest();

  MBasicBlock* trueBranch = initialTest->ifTrue();
  MBasicBlock* falseBranch = initialTest->ifFalse();

  if (trueBranch->lastIns()->isGoto() &&
      trueBranch->lastIns()->toGoto()->target() == falseBranch) {
    if (trueBranch->numPredecessors() != 1) {
      return false;
    }
    if (falseBranch->numPredecessors() != 2) {
      return false;
    }
    return true;
  }

  if (falseBranch->lastIns()->isGoto() &&
      falseBranch->lastIns()->toGoto()->target() == trueBranch) {
    if (trueBranch->numPredecessors() != 2) {
      return false;
    }
    if (falseBranch->numPredecessors() != 1) {
      return false;
    }
    return true;
  }

  return false;
}

[[nodiscard]] static bool MaybeFoldTriangleConditionBlock(
    MIRGraph& graph, MBasicBlock* initialBlock) {
  MOZ_ASSERT(IsTrianglePattern(initialBlock));

  // Optimize the MIR graph to improve the code generated for boolean
  // operations. A test like 'if (a && b)' normally requires three blocks, with
  // a phi for the intermediate value. This can be improved to use no phi value.

  /*
   * Look for a triangle pattern:
   *
   *        initialBlock
   *          /     \
   *  trueBranch     |
   *          \     /
   *     phiBlock+falseBranch
   *             |
   *         testBlock
   *
   * Or:
   *
   *        initialBlock
   *          /     \
   *         |    falseBranch
   *          \     /
   *     phiBlock+trueBranch
   *             |
   *         testBlock
   *
   * Where phiBlock contains a single phi combining values pushed onto the stack
   * by trueBranch and falseBranch, and testBlock contains a test on that phi.
   * phiBlock and testBlock may be the same block; generated code will use
   * different blocks if the (&&) op is in an inlined function.
   */

  MTest* initialTest = initialBlock->lastIns()->toTest();

  MBasicBlock* trueBranch = initialTest->ifTrue();
  MBasicBlock* falseBranch = initialTest->ifFalse();
  if (initialBlock->isLoopBackedge() || trueBranch->isLoopBackedge() ||
      falseBranch->isLoopBackedge()) {
    return true;
  }

  MBasicBlock* phiBlock;
  if (trueBranch->lastIns()->isGoto() &&
      trueBranch->lastIns()->toGoto()->target() == falseBranch) {
    phiBlock = falseBranch;
  } else {
    MOZ_ASSERT(falseBranch->lastIns()->toGoto()->target() == trueBranch);
    phiBlock = trueBranch;
  }

  MBasicBlock* testBlock = phiBlock;
  if (testBlock->lastIns()->isGoto()) {
    MOZ_RELEASE_ASSERT(!testBlock->isLoopBackedge());

    testBlock = testBlock->lastIns()->toGoto()->target();
    if (testBlock->numPredecessors() != 1) {
      return true;
    }
  }

  MPhi* phi;
  MTest* finalTest;
  if (!BlockIsSingleTest(phiBlock, testBlock, &phi, &finalTest)) {
    return true;
  }

  MOZ_RELEASE_ASSERT(phi->numOperands() == 2);

  // If the phi-operand doesn't match the initial input, we can't fold the test.
  auto* phiInputForInitialBlock =
      phi->getOperand(phiBlock->indexForPredecessor(initialBlock));
  if (!IsTestInputMaybeToBool(initialTest, phiInputForInitialBlock)) {
    return true;
  }

  // Make sure the test block does not have any outgoing loop backedges.
  if (!SplitCriticalEdgesForBlock(graph, testBlock)) {
    return false;
  }

  MDefinition* trueResult;
  MDefinition* falseResult;
  if (phiBlock == trueBranch) {
    trueResult = phi->getOperand(phiBlock->indexForPredecessor(initialBlock));
    falseResult = phi->getOperand(phiBlock->indexForPredecessor(falseBranch));
  } else {
    trueResult = phi->getOperand(phiBlock->indexForPredecessor(trueBranch));
    falseResult = phi->getOperand(phiBlock->indexForPredecessor(initialBlock));
  }

  // OK, we found the desired pattern, now transform the graph.

  // Remove the phi from phiBlock.
  DiscardTestedPhi(phi);

  // Change the end of the block to a test that jumps directly to successors of
  // testBlock, rather than to testBlock itself.

  if (phiBlock == trueBranch) {
    if (!UpdateTestSuccessors(graph.alloc(), initialBlock, initialTest->input(),
                              finalTest->ifTrue(), initialTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  } else if (IsTestInputMaybeToBool(initialTest, trueResult)) {
    if (!UpdateGotoSuccessor(graph.alloc(), trueBranch, finalTest->ifTrue(),
                             testBlock)) {
      return false;
    }
  } else {
    if (!UpdateTestSuccessors(graph.alloc(), trueBranch, trueResult,
                              finalTest->ifTrue(), finalTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  }

  if (phiBlock == falseBranch) {
    if (!UpdateTestSuccessors(graph.alloc(), initialBlock, initialTest->input(),
                              initialTest->ifTrue(), finalTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  } else if (IsTestInputMaybeToBool(initialTest, falseResult)) {
    if (!UpdateGotoSuccessor(graph.alloc(), falseBranch, finalTest->ifFalse(),
                             testBlock)) {
      return false;
    }
  } else {
    if (!UpdateTestSuccessors(graph.alloc(), falseBranch, falseResult,
                              finalTest->ifTrue(), finalTest->ifFalse(),
                              testBlock)) {
      return false;
    }
  }

  // Remove phiBlock, if different from testBlock.
  if (phiBlock != testBlock) {
    testBlock->removePredecessor(phiBlock);
    graph.removeBlock(phiBlock);
  }

  // Remove testBlock itself.
  finalTest->ifTrue()->removePredecessor(testBlock);
  finalTest->ifFalse()->removePredecessor(testBlock);
  graph.removeBlock(testBlock);

  return true;
}

[[nodiscard]] static bool MaybeFoldConditionBlock(MIRGraph& graph,
                                                  MBasicBlock* initialBlock) {
  if (IsDiamondPattern(initialBlock)) {
    return MaybeFoldDiamondConditionBlock(graph, initialBlock);
  }
  if (IsTrianglePattern(initialBlock)) {
    return MaybeFoldTriangleConditionBlock(graph, initialBlock);
  }
  return true;
}

[[nodiscard]] static bool MaybeFoldTestBlock(MIRGraph& graph,
                                             MBasicBlock* initialBlock) {
  // Handle test expressions on more than two inputs. For example
  // |if ((x > 10) && (y > 20) && (z > 30)) { ... }|, which results in the below
  // pattern.
  //
  // Look for the pattern:
  //                       ┌─────────────────┐
  //                    1  │ 1 compare       │
  //                 ┌─────┤ 2 test compare1 │
  //                 │     └──────┬──────────┘
  //                 │            │0
  //         ┌───────▼─────────┐  │
  //         │ 3 compare       │  │
  //         │ 4 test compare3 │  └──────────┐
  //         └──┬──────────┬───┘             │
  //           1│          │0                │
  // ┌──────────▼──────┐   │                 │
  // │ 5 compare       │   └─────────┐       │
  // │ 6 goto          │             │       │
  // └───────┬─────────┘             │       │
  //         │                       │       │
  //         │    ┌──────────────────▼───────▼───────┐
  //         └───►│ 9 phi compare1 compare3 compare5 │
  //              │10 goto                           │
  //              └────────────────┬─────────────────┘
  //                               │
  //                      ┌────────▼────────┐
  //                      │11 test phi9     │
  //                      └─────┬─────┬─────┘
  //                           1│     │0
  //         ┌────────────┐     │     │      ┌─────────────┐
  //         │ TrueBranch │◄────┘     └─────►│ FalseBranch │
  //         └────────────┘                  └─────────────┘
  //
  // And transform it to:
  //
  //                      ┌─────────────────┐
  //                   1  │ 1 compare       │
  //                  ┌───┤ 2 test compare1 │
  //                  │   └──────────┬──────┘
  //                  │              │0
  //          ┌───────▼─────────┐    │
  //          │ 3 compare       │    │
  //          │ 4 test compare3 │    │
  //          └──┬─────────┬────┘    │
  //            1│         │0        │
  //  ┌──────────▼──────┐  │         │
  //  │ 5 compare       │  └──────┐  │
  //  │ 6 test compare5 │         │  │
  //  └────┬────────┬───┘         │  │
  //      1│        │0            │  │
  // ┌─────▼──────┐ │         ┌───▼──▼──────┐
  // │ TrueBranch │ └─────────► FalseBranch │
  // └────────────┘           └─────────────┘

  auto* ins = initialBlock->lastIns();
  if (!ins->isTest()) {
    return true;
  }
  auto* initialTest = ins->toTest();

  MBasicBlock* trueBranch = initialTest->ifTrue();
  MBasicBlock* falseBranch = initialTest->ifFalse();

  // MaybeFoldConditionBlock handles the case for two operands.
  MBasicBlock* phiBlock;
  if (trueBranch->numPredecessors() > 2) {
    phiBlock = trueBranch;
  } else if (falseBranch->numPredecessors() > 2) {
    phiBlock = falseBranch;
  } else {
    return true;
  }

  MBasicBlock* testBlock = phiBlock;
  if (testBlock->lastIns()->isGoto()) {
    if (testBlock->isLoopBackedge()) {
      return true;
    }
    testBlock = testBlock->lastIns()->toGoto()->target();
    if (testBlock->numPredecessors() != 1) {
      return true;
    }
  }

  MOZ_RELEASE_ASSERT(!phiBlock->isLoopBackedge());

  MPhi* phi = nullptr;
  MTest* finalTest = nullptr;
  if (!BlockIsSingleTest(phiBlock, testBlock, &phi, &finalTest)) {
    return true;
  }

  MOZ_RELEASE_ASSERT(phiBlock->numPredecessors() == phi->numOperands());

  // If the phi-operand doesn't match the initial input, we can't fold the test.
  auto* phiInputForInitialBlock =
      phi->getOperand(phiBlock->indexForPredecessor(initialBlock));
  if (!IsTestInputMaybeToBool(initialTest, phiInputForInitialBlock)) {
    return true;
  }

  MBasicBlock* newTestBlock = nullptr;
  MDefinition* newTestInput = nullptr;

  // The block of each phi operand must either end with a test instruction on
  // that phi operand or it's the sole block which ends with a goto instruction.
  for (size_t i = 0; i < phiBlock->numPredecessors(); i++) {
    auto* pred = phiBlock->getPredecessor(i);
    auto* operand = phi->getOperand(i);

    // Each predecessor must end with either a test or goto instruction.
    auto* lastIns = pred->lastIns();
    if (lastIns->isGoto() && !newTestBlock) {
      newTestBlock = pred;
      newTestInput = operand;
    } else if (lastIns->isTest()) {
      if (!IsTestInputMaybeToBool(lastIns->toTest(), operand)) {
        return true;
      }
    } else {
      return true;
    }

    MOZ_RELEASE_ASSERT(!pred->isLoopBackedge());
  }

  // Ensure we found the single goto block.
  if (!newTestBlock) {
    return true;
  }

  // Make sure the test block does not have any outgoing loop backedges.
  if (!SplitCriticalEdgesForBlock(graph, testBlock)) {
    return false;
  }

  // OK, we found the desired pattern, now transform the graph.

  // Remove the phi from phiBlock.
  DiscardTestedPhi(phi);

  // Create the new test instruction.
  if (!UpdateTestSuccessors(graph.alloc(), newTestBlock, newTestInput,
                            finalTest->ifTrue(), finalTest->ifFalse(),
                            testBlock)) {
    return false;
  }

  // Update all test instructions to point to the final target.
  while (phiBlock->numPredecessors()) {
    size_t oldNumPred = phiBlock->numPredecessors();

    auto* pred = phiBlock->getPredecessor(0);
    auto* test = pred->lastIns()->toTest();
    if (test->ifTrue() == phiBlock) {
      if (!UpdateTestSuccessors(graph.alloc(), pred, test->input(),
                                finalTest->ifTrue(), test->ifFalse(),
                                testBlock)) {
        return false;
      }
    } else {
      MOZ_RELEASE_ASSERT(test->ifFalse() == phiBlock);
      if (!UpdateTestSuccessors(graph.alloc(), pred, test->input(),
                                test->ifTrue(), finalTest->ifFalse(),
                                testBlock)) {
        return false;
      }
    }

    // Ensure we've made progress.
    MOZ_RELEASE_ASSERT(phiBlock->numPredecessors() + 1 == oldNumPred);
  }

  // Remove phiBlock, if different from testBlock.
  if (phiBlock != testBlock) {
    testBlock->removePredecessor(phiBlock);
    graph.removeBlock(phiBlock);
  }

  // Remove testBlock itself.
  finalTest->ifTrue()->removePredecessor(testBlock);
  finalTest->ifFalse()->removePredecessor(testBlock);
  graph.removeBlock(testBlock);

  return true;
}

bool jit::FoldTests(MIRGraph& graph) {
  for (PostorderIterator block(graph.poBegin()); block != graph.poEnd();
       block++) {
    if (!MaybeFoldConditionBlock(graph, *block)) {
      return false;
    }
    if (!MaybeFoldTestBlock(graph, *block)) {
      return false;
    }
  }
  return true;
}
