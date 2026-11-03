//
// Copyright 2025 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/tree_ops/GatherDefaultUniforms.h"

#include "GLSLANG/ShaderVars.h"
#include "angle_gl.h"
#include "common/debug.h"
#include "compiler/translator/Compiler.h"
#include "compiler/translator/ImmutableString.h"
#include "compiler/translator/SymbolUniqueId.h"
#include "compiler/translator/tree_util/IntermNode_util.h"
#include "compiler/translator/tree_util/IntermTraverse.h"
#include "compiler/translator/tree_util/ReplaceVariable.h"
#include "compiler/translator/util.h"

namespace sh
{

namespace
{

class ReplaceDefaultUniformsTraverser : public TIntermTraverser
{
  public:
    ReplaceDefaultUniformsTraverser(const VariableReplacementMap &variableMap)
        : TIntermTraverser(true, false, false), mVariableMap(variableMap)
    {}

    bool visitDeclaration(Visit visit, TIntermDeclaration *node) override
    {
        const TIntermSequence &sequence = *(node->getSequence());

        TIntermTyped *variable = sequence.front()->getAsTyped();
        const TType &type      = variable->getType();

        if (IsDefaultUniform(type))
        {
            // Remove the uniform declaration.
            TIntermSequence emptyReplacement;
            mMultiReplacements.emplace_back(getParentNode()->getAsBlock(), node,
                                            std::move(emptyReplacement));

            return false;
        }

        return true;
    }

    void visitSymbol(TIntermSymbol *symbol) override
    {
        const TVariable &variable = symbol->variable();
        const TType &type         = variable.getType();

        if (!IsDefaultUniform(type) || gl::IsBuiltInName(variable.name().data()))
        {
            return;
        }

        ASSERT(mVariableMap.count(variable.uniqueId()) > 0);

        queueReplacement(mVariableMap.at(variable.uniqueId())->deepCopy(),
                         OriginalNode::IS_DROPPED);
    }

  private:
    const VariableReplacementMap &mVariableMap;
};

// Fields of nameless interface blocks are TVariables. So when creating a new interface blocks we
// need to create all the TVariables representing its fields.
TIntermSymbol *CreateVariableForFieldOfNamelessInterfaceBlock(const TVariable *object,
                                                              int index,
                                                              TSymbolTable *symbolTable)
{
    ASSERT(object->getType().getInterfaceBlock());
    const TFieldList &fieldList = object->getType().getInterfaceBlock()->fields();

    ASSERT(0 <= index);
    ASSERT(static_cast<size_t>(index) < fieldList.size());

    ASSERT(object->symbolType() == SymbolType::Empty);

    TType *replacementType = new TType(*fieldList[index]->type());
    replacementType->setInterfaceBlockField(object->getType().getInterfaceBlock(), index);

    TVariable *replacementVariable = new TVariable(symbolTable, fieldList[index]->name(),
                                                   replacementType, fieldList[index]->symbolType());

    return new TIntermSymbol(replacementVariable);
}

}  // namespace

bool IsDefaultUniform(const TType &type)
{
    return type.getQualifier() == EvqUniform && type.getInterfaceBlock() == nullptr &&
           !IsOpaqueType(type.getBasicType());
}

bool GatherDefaultUniforms(TCompiler *compiler,
                           TIntermBlock *root,
                           TSymbolTable *symbolTable,
                           gl::ShaderType shaderType,
                           const ImmutableString &uniformBlockType,
                           const ImmutableString &uniformBlockVarName,
                           const TVariable **outUniformBlock)
{
    // First, collect all default uniforms and declare a uniform block.
    TFieldList *uniformList = new TFieldList;
    TVector<const TVariable *> uniformVars;

    for (TIntermNode *node : *root->getSequence())
    {
        TIntermDeclaration *decl = node->getAsDeclarationNode();
        if (decl == nullptr)
        {
            continue;
        }

        const TIntermSequence &sequence = *(decl->getSequence());

        TIntermSymbol *symbol = sequence.front()->getAsSymbolNode();
        if (symbol == nullptr)
        {
            continue;
        }

        const TType &type = symbol->getType();
        if (IsDefaultUniform(type))
        {
            TType *fieldType = new TType(type);

            uniformList->push_back(new TField(fieldType, symbol->getName(), symbol->getLine(),
                                              symbol->variable().symbolType()));
            uniformVars.push_back(&symbol->variable());
        }
    }

    VariableReplacementMap variableMap;

    if (uniformList->empty())
    {
        *outUniformBlock = nullptr;
    }
    else
    {
        TLayoutQualifier layoutQualifier = TLayoutQualifier::Create();
        layoutQualifier.blockStorage     = EbsStd140;
        TInterfaceBlock *interfaceBlock =
            DeclareInterfaceBlock(symbolTable, uniformList, layoutQualifier, uniformBlockType);
        // Set the mIsDefaultUniformBlock bit because the interfaceBlock represents default uniform
        // interfaceBlock.
        // Later when traversing the AST and output SPIRV, we will rely on this bit to decide if we
        // want to transform FP32 to FP16 for float based on if the float vars are inside the
        // default uniform block.
        interfaceBlock->setDefaultUniformBlock();
        *outUniformBlock = DeclareInterfaceBlockVariable(
            root, symbolTable, EvqUniform, interfaceBlock, layoutQualifier,
            TMemoryQualifier::Create(), 0, uniformBlockVarName);

        // Create a map from the uniform variables to new variables that reference the fields of the
        // block.
        for (size_t fieldIndex = 0; fieldIndex < uniformVars.size(); ++fieldIndex)
        {
            const TVariable *variable = uniformVars[fieldIndex];

            if ((*outUniformBlock)->symbolType() == SymbolType::Empty)
            {
                variableMap[variable->uniqueId()] = CreateVariableForFieldOfNamelessInterfaceBlock(
                    *outUniformBlock, static_cast<int>(fieldIndex), symbolTable);
            }
            else
            {
                variableMap[variable->uniqueId()] = AccessFieldOfNamedInterfaceBlock(
                    *outUniformBlock, static_cast<int>(fieldIndex));
            }
        }
    }

    // Finally transform the AST and make sure references to the uniforms are replaced with the new
    // variables.
    ReplaceDefaultUniformsTraverser defaultTraverser(variableMap);
    root->traverse(&defaultTraverser);
    return defaultTraverser.updateTree(compiler, root);
}

}  // namespace sh
