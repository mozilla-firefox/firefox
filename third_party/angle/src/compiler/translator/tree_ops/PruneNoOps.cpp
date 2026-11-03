//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PruneNoOps.cpp: The PruneNoOps function prunes no-op statements.

#include "compiler/translator/tree_ops/PruneNoOps.h"

#include "compiler/translator/Symbol.h"
#include "compiler/translator/tree_util/IntermTraverse.h"
#include "compiler/translator/tree_util/ReplaceVariable.h"

namespace sh
{

namespace
{
bool IsNoOp(TIntermNode *node)
{
    bool isEmptyDeclaration = node->getAsDeclarationNode() != nullptr &&
                              node->getAsDeclarationNode()->getSequence()->empty();
    if (isEmptyDeclaration)
    {
        return true;
    }

    if (node->getAsTyped() == nullptr || node->getAsFunctionPrototypeNode() != nullptr)
    {
        return false;
    }

    return !node->getAsTyped()->hasSideEffects();
}

enum class CommaExpression
{
    // The LHS of the comma expression will get thrown away, so it can be decomposed and its side
    // effects extracted
    ThrowAway,
    // The RHS of the comma expression needs to be retained as its result is used.
    FinalResult,
};

class PruneNoOpsTraverser : private TIntermTraverser
{
  public:
    [[nodiscard]] static bool apply(TCompiler *compiler,
                                    TIntermBlock *root,
                                    TSymbolTable *symbolTable);

  private:
    PruneNoOpsTraverser(TSymbolTable *symbolTable);
    bool visitDeclaration(Visit, TIntermDeclaration *node) override;
    bool visitSwitch(Visit visit, TIntermSwitch *node) override;
    bool visitBlock(Visit visit, TIntermBlock *node) override;
    bool visitBinary(Visit visit, TIntermBinary *node) override;
    bool visitLoop(Visit visit, TIntermLoop *loop) override;
    bool visitBranch(Visit visit, TIntermBranch *node) override;
    TIntermTyped *pruneCommaThrowAwayExpression(TIntermTyped *statement);
    TIntermTyped *pruneNoOpCommaExpressions(TIntermTyped *statement, CommaExpression commaExpr);
    TIntermTyped *mergePrunedNoOpCommaExpressions(TIntermTyped *lhs, TIntermTyped *rhs);

    bool mIsBranchVisited = false;

    TVector<TVector<const TVariable *>> mSwitchPrunedDeclarationsStack;
    TVector<const TVariable *> mSwitchDeclarationsToRename;
};

bool PruneNoOpsTraverser::apply(TCompiler *compiler, TIntermBlock *root, TSymbolTable *symbolTable)
{
    PruneNoOpsTraverser prune(symbolTable);
    root->traverse(&prune);
    ASSERT(prune.mSwitchPrunedDeclarationsStack.empty());
    if (!prune.updateTree(compiler, root))
    {
        return false;
    }

    VariableReplacementMap replaceMap;
    for (const TVariable *var : prune.mSwitchDeclarationsToRename)
    {
        const TVariable *replacementVariable = new TVariable(
            symbolTable, kEmptyImmutableString, &var->getType(), SymbolType::AngleInternal);
        replaceMap[var->uniqueId()] = new TIntermSymbol(replacementVariable);
    }
    return ReplaceVariables(compiler, root, replaceMap);
}

PruneNoOpsTraverser::PruneNoOpsTraverser(TSymbolTable *symbolTable)
    : TIntermTraverser(true, false, false, symbolTable)
{}

bool PruneNoOpsTraverser::visitDeclaration(Visit visit, TIntermDeclaration *node)
{
    ASSERT(visit == PreVisit);

    TIntermSequence *sequence = node->getSequence();
    if (sequence->size() >= 1)
    {
        TIntermSymbol *declaratorSymbol = sequence->front()->getAsSymbolNode();
        // Prune declarations without a variable name, unless it's an interface block declaration.
        if (declaratorSymbol != nullptr &&
            declaratorSymbol->variable().symbolType() == SymbolType::Empty &&
            !declaratorSymbol->isInterfaceBlock())
        {
            if (sequence->size() > 1)
            {
                // Generate a replacement that will remove the empty declarator in the beginning of
                // a declarator list. Example of a declaration that will be changed:
                // float, a;
                // will be changed to
                // float a;
                // This applies also to struct declarations.
                TIntermSequence emptyReplacement;
                mMultiReplacements.emplace_back(node, declaratorSymbol,
                                                std::move(emptyReplacement));
            }
            else if (declaratorSymbol->getBasicType() != EbtStruct)
            {
                // If there are entirely empty non-struct declarations, they result in
                // TIntermDeclaration nodes without any children in the parsing stage. These are
                // handled in visitBlock and visitLoop.
                UNREACHABLE();
            }
            else if (declaratorSymbol->getQualifier() != EvqGlobal &&
                     declaratorSymbol->getQualifier() != EvqTemporary)
            {
                // Single struct declarations may just declare the struct type and no variables, so
                // they should not be pruned. Here we handle an empty struct declaration with a
                // qualifier, for example like this:
                //   const struct a { int i; };
                // NVIDIA GL driver version 367.27 doesn't accept this kind of declarations, so we
                // convert the declaration to a regular struct declaration. This is okay, since ESSL
                // 1.00 spec section 4.1.8 says about structs that "The optional qualifiers only
                // apply to any declarators, and are not part of the type being defined for name."

                // Create a new variable to use in the declarator so that the variable and node
                // types are kept consistent.
                TType *type = new TType(declaratorSymbol->getType());
                if (mInGlobalScope)
                {
                    type->setQualifier(EvqGlobal);
                }
                else
                {
                    type->setQualifier(EvqTemporary);
                }
                TVariable *variable =
                    new TVariable(mSymbolTable, kEmptyImmutableString, type, SymbolType::Empty);
                queueReplacementWithParent(node, declaratorSymbol, new TIntermSymbol(variable),
                                           OriginalNode::IS_DROPPED);
            }
            return false;
        }
    }
    return true;
}

bool PruneNoOpsTraverser::visitSwitch(Visit visit, TIntermSwitch *node)
{
    node->getInit()->traverse(this);

    // Before visiting the block, push a list of variable declarations that were pruned because they
    // were declared directly in the body of the switch but after a branch.  The variable
    // declarations are instead prepended to the switch, in case they are used in a later case which
    // references them in live code.
    mSwitchPrunedDeclarationsStack.push_back({});

    node->getStatementList()->traverse(this);
    if (!mSwitchPrunedDeclarationsStack.back().empty())
    {
        TIntermSequence replacement;
        for (const TVariable *toDeclare : mSwitchPrunedDeclarationsStack.back())
        {
            TIntermDeclaration *decl = new TIntermDeclaration();
            decl->appendDeclarator(new TIntermSymbol(toDeclare));
            replacement.push_back(decl);
        }
        replacement.push_back(node);
        mMultiReplacements.emplace_back(getParentNode()->getAsBlock(), node,
                                        std::move(replacement));

        // The names may shadow variables outside the switch, so rename them at the end.
        mSwitchDeclarationsToRename.insert(mSwitchDeclarationsToRename.end(),
                                           mSwitchPrunedDeclarationsStack.back().begin(),
                                           mSwitchPrunedDeclarationsStack.back().end());
    }

    mSwitchPrunedDeclarationsStack.pop_back();
    return false;
}

bool PruneNoOpsTraverser::visitBlock(Visit visit, TIntermBlock *node)
{
    ASSERT(visit == PreVisit);

    TIntermSequence &statements = *node->getSequence();
    size_t writeIndex           = 0;

    // Visit each statement in the block one by one.  Once a branch is visited (break, continue,
    // return or discard), drop the rest of the statements.
    for (size_t statementIndex = 0; statementIndex < statements.size(); ++statementIndex)
    {
        TIntermNode *statement = statements[statementIndex];

        // If the statement is a switch case label, stop pruning and continue visiting the children.
        if (statement->getAsCaseNode() != nullptr)
        {
            mIsBranchVisited = false;
        }

        // If a branch is visited, prune the statement.  If the statement is a no-op, also prune it.
        if (mIsBranchVisited || IsNoOp(statement))
        {
            // If this is a declaration, remember the variable.  The declaration is moved to before
            // the switch, to support cases like:
            //
            //    switch(u0){
            //        case 0:
            //            break;
            //            vec4 d = vec4(0);
            //        default:
            //            d.a = .0;
            //    }
            if (mIsBranchVisited && getParentNode()->getAsSwitchNode() != nullptr)
            {
                TIntermDeclaration *decl = statement->getAsDeclarationNode();
                if (decl != nullptr)
                {
                    for (TIntermNode *declarator : *decl->getSequence())
                    {
                        TIntermSymbol *symbol        = declarator->getAsSymbolNode();
                        const TVariable *declaredVar = nullptr;
                        if (symbol != nullptr)
                        {
                            declaredVar = &symbol->variable();
                        }
                        else
                        {
                            TIntermBinary *initNode = declarator->getAsBinaryNode();
                            ASSERT(initNode && initNode->getOp() == EOpInitialize);
                            ASSERT(initNode->getLeft()->getAsSymbolNode());
                            declaredVar = &initNode->getLeft()->getAsSymbolNode()->variable();
                        }

                        mSwitchPrunedDeclarationsStack.back().push_back(declaredVar);
                    }
                }
            }

            continue;
        }

        // If the statement is a series of expressions delimited by comma, the resulting value is
        // not used (because this is a block-level statement).  Prune the no-op statements, and put
        // the ones with side effect back together with comma.
        if (statement->getAsBinaryNode() != nullptr)
        {
            statement = pruneNoOpCommaExpressions(statement->getAsBinaryNode(),
                                                  CommaExpression::FinalResult);
            if (statement == nullptr)
            {
                continue;
            }
        }

        // Visit the statement if not pruned.
        statements[writeIndex++] = statement;
        statement->traverse(this);
    }
    statements.resize(writeIndex);

    // If the parent is a block and mIsBranchVisited is set, this is a nested block without any
    // condition (like if, loop or switch), so the rest of the parent block should also be pruned.
    // Otherwise the parent block should be unaffected.
    if (mIsBranchVisited && getParentNode()->getAsBlock() == nullptr)
    {
        mIsBranchVisited = false;
    }

    return false;
}

bool PruneNoOpsTraverser::visitBinary(Visit visit, TIntermBinary *node)
{
    if (node->getOp() == EOpComma && getParentNode()->getAsBlock() == nullptr)
    {
        // Prune LHS of the comma.  This is not done if the parent is a block node because
        // visitBlock() already does it.
        TIntermTyped *prunedLeft = pruneCommaThrowAwayExpression(node->getLeft());
        if (prunedLeft != node->getLeft())
        {
            // If completely pruned, replace with RHS, otherwise replace the LHS with its side
            // effects.
            queueReplacement(prunedLeft != nullptr
                                 ? new TIntermBinary(EOpComma, prunedLeft, node->getRight())
                                 : node->getRight(),
                             OriginalNode::IS_DROPPED);

            node->getRight()->traverse(this);
            if (prunedLeft)
            {
                prunedLeft->traverse(this);
            }
            return false;
        }
    }

    return true;
}

TIntermTyped *PruneNoOpsTraverser::pruneCommaThrowAwayExpression(TIntermTyped *statement)
{
    if (IsNoOp(statement))
    {
        return nullptr;
    }

    TIntermBinary *asBinary = statement->getAsBinaryNode();
    if (asBinary == nullptr)
    {
        return statement;
    }

    switch (asBinary->getOp())
    {
        case EOpIndexDirect:
        case EOpIndexDirectStruct:
        case EOpIndexDirectInterfaceBlock:
            return pruneNoOpCommaExpressions(asBinary->getLeft(), CommaExpression::ThrowAway);
        case EOpIndexIndirect:
        case EOpComma:
        {
            // Prune both the indexed and the indexee.  If both have side effects, join them with a
            // comma.
            //
            // Same with a comma operation's left and right hand side expressions.  Since
            // |statement| is itself the LHS of a comma operation, both its LHS and RHS can be
            // pruned.
            TIntermTyped *prunedLeft =
                pruneNoOpCommaExpressions(asBinary->getLeft(), CommaExpression::ThrowAway);
            TIntermTyped *prunedRight =
                pruneNoOpCommaExpressions(asBinary->getRight(), CommaExpression::ThrowAway);
            return mergePrunedNoOpCommaExpressions(prunedLeft, prunedRight);
        }
        default:
            return statement;
    }
}

TIntermTyped *PruneNoOpsTraverser::pruneNoOpCommaExpressions(TIntermTyped *statement,
                                                             CommaExpression commaExpr)
{
    TIntermBinary *commaSeparatedExpressions = statement->getAsBinaryNode();
    if (commaSeparatedExpressions == nullptr || commaSeparatedExpressions->getOp() != EOpComma)
    {
        // If this is not the final result of comma, try to extract side effect out of the
        // expression and throw the rest away.  In an expression like
        // |struct_with_sampler[side_effect]|, this allows it to be replaced by |side_effect| alone.
        return commaExpr == CommaExpression::ThrowAway ? pruneCommaThrowAwayExpression(statement)
                                                       : statement;
    }

    TIntermTyped *left  = commaSeparatedExpressions->getLeft();
    TIntermTyped *right = commaSeparatedExpressions->getRight();

    TIntermTyped *prunedLeft =
        IsNoOp(left) ? nullptr : pruneNoOpCommaExpressions(left, CommaExpression::ThrowAway);
    TIntermTyped *prunedRight =
        IsNoOp(right) ? nullptr : pruneNoOpCommaExpressions(right, commaExpr);

    if (left == prunedLeft && right == prunedRight)
    {
        // Nothing got pruned.
        return statement;
    }

    return mergePrunedNoOpCommaExpressions(prunedLeft, prunedRight);
}

TIntermTyped *PruneNoOpsTraverser::mergePrunedNoOpCommaExpressions(TIntermTyped *lhs,
                                                                   TIntermTyped *rhs)
{
    // If either side is pruned, return the other side.  Automatically returns nullptr if both sides
    // are pruned.
    if (rhs == nullptr)
    {
        return lhs;
    }
    if (lhs == nullptr)
    {
        return rhs;
    }

    return new TIntermBinary(EOpComma, lhs, rhs);
}

bool PruneNoOpsTraverser::visitLoop(Visit visit, TIntermLoop *loop)
{
    ASSERT(visit == PreVisit);

    TIntermTyped *expr = loop->getExpression();
    if (expr != nullptr && IsNoOp(expr))
    {
        loop->setExpression(nullptr);
    }
    TIntermNode *init = loop->getInit();
    if (init != nullptr && IsNoOp(init))
    {
        loop->setInit(nullptr);
    }

    return true;
}

bool PruneNoOpsTraverser::visitBranch(Visit visit, TIntermBranch *node)
{
    ASSERT(visit == PreVisit);

    mIsBranchVisited = true;

    // Only possible child is the value of a return statement, which has nothing to prune.
    return false;
}
}  // namespace

bool PruneNoOps(TCompiler *compiler, TIntermBlock *root, TSymbolTable *symbolTable)
{
    return PruneNoOpsTraverser::apply(compiler, root, symbolTable);
}

}  // namespace sh
