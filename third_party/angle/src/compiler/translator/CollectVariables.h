//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// CollectVariables.h: Collect lists of shader interface variables based on the AST.

#ifndef COMPILER_TRANSLATOR_COLLECTVARIABLES_H_
#define COMPILER_TRANSLATOR_COLLECTVARIABLES_H_

#include <GLSLANG/ShaderLang.h>

#include "compiler/translator/Common.h"
#include "compiler/translator/ExtensionBehavior.h"

namespace sh
{

class TIntermBlock;
class TSymbolTable;
class TVariable;
class NameMap;

struct SelectedFields
{
    POOL_ALLOCATOR_NEW_DELETE
    TUnorderedMap<uint32_t, SelectedFields *> subfields;
};

using SamplersStaticallyUsedWithTexelFetch = TUnorderedMap<const TVariable *, SelectedFields>;

void CollectVariables(
    TIntermBlock *root,
    std::vector<ShaderVariable> *attributes,
    std::vector<ShaderVariable> *outputVariables,
    std::vector<ShaderVariable> *uniforms,
    std::vector<ShaderVariable> *inputVaryings,
    std::vector<ShaderVariable> *outputVaryings,
    std::vector<ShaderVariable> *sharedVariables,
    std::vector<InterfaceBlock> *uniformBlocks,
    std::vector<InterfaceBlock> *shaderStorageBlocks,
    ShHashFunction64 hashFunction,
    NameMap *nameMap,
    TSymbolTable *symbolTable,
    GLenum shaderType,
    const TExtensionBehavior &extensionBehavior,
    bool transformFloatUniformToFP16,
    const SamplersStaticallyUsedWithTexelFetch &samplersStaticallyUsedWithTexelFetch);
}  // namespace sh

#endif  // COMPILER_TRANSLATOR_COLLECTVARIABLES_H_
