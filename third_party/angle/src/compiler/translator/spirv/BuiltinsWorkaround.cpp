//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/spirv/BuiltinsWorkaround.h"

#include "angle_gl.h"
#include "compiler/translator/Symbol.h"
#include "compiler/translator/SymbolTable.h"
#include "compiler/translator/tree_util/BuiltIn.h"
#include "compiler/translator/tree_util/DriverUniform.h"

namespace sh
{

namespace
{
constexpr const ImmutableString kGlInstanceIDString("gl_InstanceID");
constexpr const ImmutableString kGlVertexIDString("gl_VertexID");

class TBuiltinsWorkaround : public TIntermTraverser
{
  public:
    TBuiltinsWorkaround(TSymbolTable *symbolTable,
                        const DriverUniform *driverUniforms)
        : TIntermTraverser(true, false, false, symbolTable),
          mDriverUniforms(driverUniforms)
    {}

    void visitSymbol(TIntermSymbol *node) override;

  private:
    void ensureVersionIsAtLeast(int version);

    const DriverUniform *mDriverUniforms;
};

void TBuiltinsWorkaround::visitSymbol(TIntermSymbol *node)
{
    if (node->variable().symbolType() == SymbolType::BuiltIn &&
        node->getName() == kGlInstanceIDString)
    {
        TIntermSymbol *instanceIndexRef = new TIntermSymbol(BuiltInVariable::gl_InstanceIndex());
        TIntermBinary *subBaseInstance =
            new TIntermBinary(EOpSub, instanceIndexRef, mDriverUniforms->getBaseInstance());
        queueReplacement(subBaseInstance, OriginalNode::IS_DROPPED);
    }
    else if (node->getName() == kGlVertexIDString)
    {
        TIntermSymbol *vertexIndexRef = new TIntermSymbol(BuiltInVariable::gl_VertexIndex());
        queueReplacement(vertexIndexRef, OriginalNode::IS_DROPPED);
    }
}
}  // anonymous namespace

[[nodiscard]] bool ShaderBuiltinsWorkaround(TCompiler *compiler,
                                            TIntermBlock *root,
                                            const DriverUniform *driverUniforms,
                                            TSymbolTable *symbolTable)
{
    TBuiltinsWorkaround builtins(symbolTable, driverUniforms);
    root->traverse(&builtins);
    if (!builtins.updateTree(compiler, root))
    {
        return false;
    }
    return true;
}

}  // namespace sh
