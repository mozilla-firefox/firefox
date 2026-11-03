//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// EnsureLoopForwardProgress is an AST traverser that ensures that loops are either finite or
// invoke side-effects.
//

#ifndef COMPILER_TRANSLATOR_TREEOPS_ENSURELOOPFORWARDPROGRESS_H_
#define COMPILER_TRANSLATOR_TREEOPS_ENSURELOOPFORWARDPROGRESS_H_

#include "common/angleutils.h"
#include "common/debug.h"

namespace sh
{
class TCompiler;
class TIntermNode;

enum class EnsureLoopForwardProgressMode
{
    // Call a function with a volatile variable in the loop
    Volatile,
    // Use a counter that is practically infinite and break in the loop
    Counter,
};

[[nodiscard]] bool EnsureLoopForwardProgress(TCompiler *compiler,
                                             TIntermNode *root,
                                             EnsureLoopForwardProgressMode mode);
}  // namespace sh

#endif  // COMPILER_TRANSLATOR_TREEOPS_ENSURELOOPFORWARDPROGRESS_H_
