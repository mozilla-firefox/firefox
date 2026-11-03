//
// Copyright 2018 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PruneEmptyCases.cpp: The PruneEmptyCases function prunes cases that are followed by nothing from
// the AST.

#include "compiler/translator/tree_ops/PruneEmptyCases.h"

#include "compiler/translator/Symbol.h"
#include "compiler/translator/tree_util/IntermTraverse.h"

namespace sh
{

namespace
{

bool AreEmptyBlocks(const TIntermSequence *statements);

bool IsEmptyBlock(TIntermNode *node)
{
    TIntermBlock *asBlock = node->getAsBlock();
    if (asBlock)
    {
        return AreEmptyBlocks(asBlock->getSequence());
    }
    // Empty declarations should have already been pruned, otherwise they would need to be handled
    // here. Note that declarations for struct types do contain a nameless child node.
    ASSERT(node->getAsDeclarationNode() == nullptr ||
           !node->getAsDeclarationNode()->getSequence()->empty());
    // Pure literal statements should also already be pruned.
    ASSERT(node->getAsConstantUnion() == nullptr);
    return false;
}

// Return true if all statements in "statements" consist only of empty blocks and no-op statements.
// Returns true also if there are no statements.
bool AreEmptyBlocks(const TIntermSequence *statements)
{
    for (TIntermNode *statement : *statements)
    {
        if (!IsEmptyBlock(statement))
        {
            return false;
        }
    }
    return true;
}

bool EndsInBranch(const TIntermSequence &statements)
{
    if (statements.empty())
    {
        return false;
    }

    TIntermNode *lastStatement = statements.back();
    if (lastStatement->getAsBlock())
    {
        return EndsInBranch(*lastStatement->getAsBlock()->getSequence());
    }

    TIntermBranch *branchNode = lastStatement->getAsBranchNode();
    return branchNode != nullptr;
}

class PruneEmptyCasesTraverser : private TIntermTraverser
{
  public:
    [[nodiscard]] static bool apply(TCompiler *compiler, TIntermBlock *root);

  private:
    PruneEmptyCasesTraverser();
    bool visitBlock(Visit visit, TIntermBlock *node) override;
    bool visitSwitch(Visit visit, TIntermSwitch *node) override;
};

bool PruneEmptyCasesTraverser::apply(TCompiler *compiler, TIntermBlock *root)
{
    PruneEmptyCasesTraverser prune;
    root->traverse(&prune);
    return prune.updateTree(compiler, root);
}

PruneEmptyCasesTraverser::PruneEmptyCasesTraverser() : TIntermTraverser(true, false, false) {}

bool PruneEmptyCasesTraverser::visitBlock(Visit visit, TIntermBlock *node)
{
    TIntermSequence &statements = *node->getSequence();
    size_t writeIndex           = 0;

    for (size_t statementIndex = 0; statementIndex < statements.size(); ++statementIndex)
    {
        TIntermNode *statement = statements[statementIndex];

        // Visit the statement first.  If it's a switch that is no-op, it will clear its statements,
        // read to be removed from this block.
        statement->traverse(this);

        // Remove the switch if it's pruned.
        TIntermSwitch *asSwitch = statement->getAsSwitchNode();
        if (asSwitch != nullptr && asSwitch->getStatementList()->getSequence()->empty())
        {
            // If switch's init condition has a side effect (like |switch(a++)|), preserve that.
            TIntermTyped *init = asSwitch->getInit();
            if (!init->hasSideEffects())
            {
                continue;
            }
            statement = init;
        }

        statements[writeIndex++] = statement;
    }
    statements.resize(writeIndex);

    // If the parent block is a switch and the block doesn't end in a branch, add an implicit
    // |break|.
    if (getParentNode() != nullptr && getParentNode()->getAsSwitchNode() != nullptr &&
        !EndsInBranch(statements))
    {
        statements.push_back(new TIntermBranch(EOpBreak, nullptr));
    }

    return false;
}

bool PruneEmptyCasesTraverser::visitSwitch(Visit visit, TIntermSwitch *node)
{
    // This may mutate the statementList, but that's okay, since traversal has not yet reached
    // there.
    TIntermBlock *statementList = node->getStatementList();
    TIntermSequence *statements = statementList->getSequence();

    // Iterate block children in reverse order. Cases that are only followed by other cases or empty
    // blocks are marked for pruning.
    size_t i                       = statements->size();
    size_t lastNoOpInStatementList = i;

    while (i > 0)
    {
        --i;
        TIntermNode *statement = statements->at(i);

        // Ignore the last |break| for the purposes of this pruning
        if (i + 1 == statements->size() && statement->getAsBranchNode() != nullptr &&
            statement->getAsBranchNode()->getFlowOp() == EOpBreak)
        {
            continue;
        }
        if (statement->getAsCaseNode() || IsEmptyBlock(statement))
        {
            lastNoOpInStatementList = i;
        }
        else
        {
            break;
        }
    }
    if (lastNoOpInStatementList < statements->size())
    {
        // The extra cases can only be removed if there is no |default|, otherwise dropping them
        // changes the cases that land in |default|.
        bool hasDefault = false;
        for (i = 0; i < lastNoOpInStatementList; ++i)
        {
            TIntermNode *statement = statements->at(i);
            if (statement->getAsCaseNode() != nullptr &&
                !statement->getAsCaseNode()->hasCondition())
            {
                hasDefault = true;
                break;
            }
        }

        if (!hasDefault)
        {
            statements->erase(statements->begin() + lastNoOpInStatementList, statements->end());
        }
    }

    return !statements->empty();
}

}  // namespace

bool PruneEmptyCases(TCompiler *compiler, TIntermBlock *root)
{
    return PruneEmptyCasesTraverser::apply(compiler, root);
}

}  // namespace sh
