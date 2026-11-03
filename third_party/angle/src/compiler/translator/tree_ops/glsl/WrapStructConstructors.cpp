//
// Copyright 2026 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/tree_ops/glsl/WrapStructConstructors.h"

#include <limits>
#include <string>

#include "compiler/translator/Compiler.h"
#include "compiler/translator/ImmutableString.h"
#include "compiler/translator/IntermRebuild.h"
#include "compiler/translator/Symbol.h"
#include "compiler/translator/tree_util/FindFunction.h"
#include "compiler/translator/tree_util/IntermNode_util.h"

namespace sh
{

namespace
{
bool IsSimple(TIntermNode *node)
{
    if (node->getAsSymbolNode() != nullptr)
    {
        return true;
    }

    TIntermConstantUnion *asConst = node->getAsConstantUnion();
    if (asConst != nullptr)
    {
        return !asConst->getType().isArray() && asConst->getType().getStruct() == nullptr;
    }

    return false;
}

class WrapRebuilder final : public TIntermRebuild
{
  public:
    explicit WrapRebuilder(TCompiler &compiler) : TIntermRebuild(compiler, false, true) {}

    PostResult visitAggregatePost(TIntermAggregate &node) override
    {
        // Transform only struct constructors
        if (node.getOp() != EOpConstruct)
        {
            return node;
        }

        const TType &type = node.getType();
        if (type.isArray())
        {
            return node;
        }

        const TStructure *structure = type.getStruct();
        if (structure == nullptr)
        {
            return node;
        }

        // Check if the arguments are complex.  If the arguments are only variables, or basic type
        // constants, leave the constructor be.
        TIntermSequence *arguments = node.getSequence();
        bool isAnyComplex          = false;
        for (TIntermNode *arg : *arguments)
        {
            if (!IsSimple(arg))
            {
                isAnyComplex = true;
                break;
            }
        }

        if (!isAnyComplex)
        {
            return node;
        }

        if (mIsGlobalScope)
        {
            return transformConstructorGlobalScope(node, structure);
        }
        else
        {
            return transformConstructorFunctionScope(node, structure);
        }
    }

    PostResult transformConstructorGlobalScope(TIntermAggregate &node, const TStructure *structure)
    {
        // For any argument that is complex, create a temp variable that holds its value, and
        // replace the argument with that.
        TIntermSequence *arguments = node.getSequence();
        TIntermSequence replacement;
        for (TIntermNode *arg : *arguments)
        {
            if (IsSimple(arg))
            {
                replacement.push_back(arg);
            }
            else
            {
                const TVariable *globalVariable =
                    CreateTempVariable(&mSymbolTable, &arg->getAsTyped()->getType(), EvqConst);
                TIntermSymbol *global          = new TIntermSymbol(globalVariable);
                TIntermDeclaration *globalDecl = new TIntermDeclaration();
                globalDecl->appendDeclarator(
                    new TIntermBinary(EOpInitialize, global, arg->getAsTyped()));
                mAdditionalVariables.push_back(globalDecl);

                replacement.push_back(global->deepCopy());
            }
        }

        // Replace the constructor.
        return *TIntermAggregate::CreateConstructor(node.getType(), &replacement);
    }

    PostResult transformConstructorFunctionScope(TIntermAggregate &node,
                                                 const TStructure *structure)
    {
        TIntermSequence *arguments = node.getSequence();

        // Create a constructor function for it, if not already
        const TFunction *constructor = makeConstructorHelper(structure);

        // Replace the constructor call with a call to the helper function.
        return *TIntermAggregate::CreateFunctionCall(*constructor, arguments);
    }

    bool rewrite(TIntermBlock &root)
    {
        // Process struct constructors in global declarations differently from in-function ones.
        // The global declarations cannot call a function if they are |const|, so instead their
        // complex arguments are pulled into global variables.  For in-function struct constructors,
        // a helper function is used.
        TIntermSequence *original = root.getSequence();
        TIntermSequence replacement;
        for (TIntermNode *node : *original)
        {
            // Choose which strategy to use for this node.
            mIsGlobalScope = !node->getAsFunctionDefinition();
            const bool failed = rebuild(*node).isFail();
            ASSERT(!failed);
            replacement.insert(replacement.end(), mAdditionalVariables.begin(),
                               mAdditionalVariables.end());
            mAdditionalVariables.clear();
            replacement.push_back(node);
        }
        root.replaceAllChildren(std::move(replacement));

        addConstructorFunctions(root);

        return mCompiler.validateAST(&root);
    }

    void addConstructorFunctions(TIntermBlock &root)
    {
        if (mConstructorFunctions.empty())
        {
            return;
        }

        // Split the declarations from functions.  Move all declarations up, similar to
        // MoveDeclarationsBeforeFunctions, and add the new helpers in between declarations and
        // existing functions.
        TIntermSequence *original = root.getSequence();

        TIntermSequence replacement;
        TIntermSequence functionDefs;

        // Accumulate non-function-definition declarations in |replacement| and function definitions
        // in |functionDefs|.
        for (TIntermNode *node : *original)
        {
            if (node->getAsFunctionDefinition() || node->getAsFunctionPrototypeNode())
            {
                functionDefs.push_back(node);
            }
            else
            {
                replacement.push_back(node);
            }
        }

        // Add constructor helpers
        for (const auto &function : mConstructorFunctions)
        {
            replacement.push_back(function.second);
        }

        // Append function definitions to |replacement|.
        replacement.insert(replacement.end(), functionDefs.begin(), functionDefs.end());

        // Replace root's sequence with |replacement|.
        root.replaceAllChildren(std::move(replacement));
    }

  private:
    const TFunction *makeConstructorHelper(const TStructure *structure)
    {
        auto it = mConstructorFunctions.find(structure);
        if (it != mConstructorFunctions.end())
        {
            return it->second->getFunction();
        }

        const TType *returnType = new TType(structure, false);
        TFunction *function     = new TFunction(&mSymbolTable, kEmptyImmutableString,
                                                SymbolType::AngleInternal, returnType, true);

        // Add parameters matching the struct fields.
        const TFieldList &fields         = structure->fields();
        TIntermSequence *constructorArgs = new TIntermSequence();
        for (const TField *field : fields)
        {
            const TVariable *var = CreateTempVariable(&mSymbolTable, field->type(), EvqParamIn);
            function->addParameter(var);
            constructorArgs->push_back(new TIntermSymbol(var));
        }

        TIntermBlock *body = new TIntermBlock;

        // StructType(param0, param1, ...)
        TIntermTyped *constructorCall =
            TIntermAggregate::CreateConstructor(*returnType, constructorArgs);

        // return ...;
        TIntermBranch *returnStatement = new TIntermBranch(EOpReturn, constructorCall);
        body->appendStatement(returnStatement);

        TIntermFunctionDefinition *functionDefinition =
            CreateInternalFunctionDefinitionNode(*function, body);

        // Cache the definition so it'll be added to the shader at the right place.
        mConstructorFunctions[structure] = functionDefinition;
        return function;
    }

    bool mIsGlobalScope = false;
    // For global-scope constructors:
    TVector<TIntermDeclaration *> mAdditionalVariables;
    // For function-scope constructors:
    TMap<const TStructure *, TIntermFunctionDefinition *> mConstructorFunctions;
};
}  // namespace

bool WrapStructConstructors(TCompiler *compiler, TIntermBlock *root, TSymbolTable *symbolTable)
{
    WrapRebuilder rebuilder(*compiler);
    return rebuilder.rewrite(*root);
}

}  // namespace sh
