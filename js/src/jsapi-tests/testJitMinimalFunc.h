/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsapi_tests_jitTestGVN_h
#define jsapi_tests_jitTestGVN_h

#include "jit/DominatorTree.h"
#include "jit/IonAnalysis.h"
#include "jit/MIRGenerator.h"
#include "jit/MIRGraph.h"
#include "jit/RangeAnalysis.h"
#include "jit/ValueNumbering.h"

namespace js {
namespace jit {

struct MinimalAlloc {
  LifoAlloc lifo;
  TempAllocator alloc;

  // We are not testing the fallible allocator in these test cases, thus make
  // the lifo alloc chunk extremely large for our test cases.
  MinimalAlloc() : lifo(128 * 1024, js::MallocArena), alloc(&lifo) {
    if (!alloc.ensureBallast()) {
      MOZ_CRASH("[OOM] Not enough RAM for the test.");
    }
  }
};

struct MinimalFunc : MinimalAlloc {
  JitCompileOptions options;
  CompileInfo info;
  MIRGraph graph;
  MIRGenerator mir;
  uint32_t numParams;

  MinimalFunc()
      : options(),
        info(0),
        graph(&alloc),
        mir(static_cast<CompileRealm*>(nullptr), options, &alloc, &graph, &info,
            static_cast<const OptimizationInfo*>(nullptr)),
        numParams(0) {}

  MBasicBlock* createEntryBlock() {
    MBasicBlock* block =
        MBasicBlock::New(graph, info, nullptr, MBasicBlock::NORMAL);
    graph.addBlock(block);
    return block;
  }

  MBasicBlock* createOsrEntryBlock() {
    MBasicBlock* block =
        MBasicBlock::New(graph, info, nullptr, MBasicBlock::NORMAL);
    graph.addBlock(block);
    graph.setOsrBlock(block);
    return block;
  }

  MBasicBlock* createBlock(MBasicBlock* pred) {
    MBasicBlock* block =
        MBasicBlock::New(graph, info, pred, MBasicBlock::NORMAL);
    graph.addBlock(block);
    return block;
  }

  MParameter* createParameter() {
    MParameter* p = MParameter::New(alloc, numParams++);
    return p;
  }

  bool runGVN() {
    if (!SplitCriticalEdges(graph)) {
      return false;
    }
    RenumberBlocks(graph);
    if (!BuildDominatorTree(&mir, graph)) {
      return false;
    }
    if (!BuildPhiReverseMapping(graph)) {
      return false;
    }
    ValueNumberer gvn(&mir, graph);
    if (!gvn.run(ValueNumberer::DontUpdateAliasAnalysis)) {
      return false;
    }
    return true;
  }

  bool runRangeAnalysis() {
    if (!SplitCriticalEdges(graph)) {
      return false;
    }
    RenumberBlocks(graph);
    if (!BuildDominatorTree(&mir, graph)) {
      return false;
    }
    if (!BuildPhiReverseMapping(graph)) {
      return false;
    }
    RangeAnalysis rangeAnalysis(&mir, graph);
    if (!rangeAnalysis.addBetaNodes()) {
      return false;
    }
    if (!rangeAnalysis.analyze()) {
      return false;
    }
    if (!rangeAnalysis.addRangeAssertions()) {
      return false;
    }
    if (!rangeAnalysis.removeBetaNodes()) {
      return false;
    }
    return true;
  }
};

}  // namespace jit
}  // namespace js

#endif
