//
// Copyright 2017 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// IntermNode_util.h: High-level utilities for creating AST nodes and node hierarchies. Mostly meant
// to be used in AST transforms.

#ifndef COMPILER_TRANSLATOR_INTERMNODEUTIL_H_
#define COMPILER_TRANSLATOR_INTERMNODEUTIL_H_

#include <optional>

#include "compiler/translator/IntermNode.h"
#include "compiler/translator/Name.h"
#include "compiler/translator/tree_util/FindFunction.h"

namespace sh
{

class TSymbolTable;
class TVariable;

TIntermFunctionPrototype *CreateInternalFunctionPrototypeNode(const TFunction &func);
TIntermFunctionDefinition *CreateInternalFunctionDefinitionNode(const TFunction &func,
                                                                TIntermBlock *functionBody);

TIntermTyped *CreateZeroNode(const TType &type);
TIntermConstantUnion *CreateFloatNode(float value, TPrecision precision);
TIntermConstantUnion *CreateVecNode(const float values[],
                                    unsigned int vecSize,
                                    TPrecision precision);
TIntermConstantUnion *CreateUVecNode(const unsigned int values[],
                                     unsigned int vecSize,
                                     TPrecision precision);
TIntermConstantUnion *CreateIndexNode(int index);
TIntermConstantUnion *CreateUIntNode(unsigned int value);
TIntermConstantUnion *CreateBoolNode(bool value);
TIntermConstantUnion *CreateYuvCscNode(TYuvCscStandardEXT value);

// Create temporary variable of known type |type|.
TVariable *CreateTempVariable(TSymbolTable *symbolTable, const TType *type);

// Create temporary variable compatible with user-provide type |type|.
// Possibly creates a new type. The qualifer of the new type of the new variable is |qualifier|.
TVariable *CreateTempVariable(TSymbolTable *symbolTable, const TType *type, TQualifier qualifier);

TIntermSymbol *CreateTempSymbolNode(const TVariable *tempVariable);
TIntermDeclaration *CreateTempDeclarationNode(const TVariable *tempVariable);
TIntermDeclaration *CreateTempInitDeclarationNode(const TVariable *tempVariable,
                                                  TIntermTyped *initializer);
TIntermBinary *CreateTempAssignmentNode(const TVariable *tempVariable, TIntermTyped *rightNode);

TVariable *DeclareTempVariable(TSymbolTable *symbolTable,
                               const TType *type,
                               TQualifier qualifier,
                               TIntermDeclaration **declarationOut);
TVariable *DeclareTempVariable(TSymbolTable *symbolTable,
                               TIntermTyped *initializer,
                               TQualifier qualifier,
                               TIntermDeclaration **declarationOut);
std::pair<const TVariable *, const TVariable *> DeclareStructure(
    TIntermBlock *root,
    TSymbolTable *symbolTable,
    TFieldList *fieldList,
    TQualifier qualifier,
    const TMemoryQualifier &memoryQualifier,
    uint32_t arraySize,
    const ImmutableString &structTypeName,
    const ImmutableString *structInstanceName);
TInterfaceBlock *DeclareInterfaceBlock(TSymbolTable *symbolTable,
                                       TFieldList *fieldList,
                                       const TLayoutQualifier &layoutQualifier,
                                       const ImmutableString &blockTypeName);

const TVariable *DeclareInterfaceBlockVariable(TIntermBlock *root,
                                               TSymbolTable *symbolTable,
                                               TQualifier qualifier,
                                               const TInterfaceBlock *interfaceBlock,
                                               const TLayoutQualifier &layoutQualifier,
                                               const TMemoryQualifier &memoryQualifier,
                                               uint32_t arraySize,
                                               const ImmutableString &blockVariableName);

// `expr` must be an lvalue. This will return the root variable, e.g. the root variable of
// `a[0].b[0].xy` is `a`,
const TVariable *FindRootVariable(TIntermNode *expr);

// Creates a variable for a struct type.
const TVariable &CreateStructTypeVariable(TSymbolTable &symbolTable, const TStructure &structure);

// Creates a variable for a struct instance.
const TVariable &CreateInstanceVariable(
    TSymbolTable &symbolTable,
    const TStructure &structure,
    const Name &name,
    TQualifier qualifier                              = TQualifier::EvqTemporary,
    const angle::Span<const unsigned int> *arraySizes = nullptr);

// Accesses a field for the given node with the given field name.
// The node must be a struct instance.
TIntermBinary &AccessField(const TVariable &structInstanceVar, const Name &field);

// Accesses a field for the given node with the given field name.
// The node must be a struct instance.
TIntermBinary &AccessField(TIntermTyped &object, const Name &field);

// Accesses a field for the given node by its field index.
// The node must be a struct instance.
TIntermBinary &AccessFieldByIndex(TIntermTyped &object, int index);

// Accesses `object` by index, returning a binary referencing the field of the named interface
// block.
// Note: nameless interface blocks' fields are represented by individual TVariables, and so this
// helper cannot generate an access to them.
TIntermBinary *AccessFieldOfNamedInterfaceBlock(const TVariable *object, int index);

// If the input node is nullptr, return nullptr.
// If the input node is a block node, return it.
// If the input node is not a block node, put it inside a block node and return that.
TIntermBlock *EnsureBlock(TIntermNode *node);

// If the input node is nullptr, return a new block.
// If the input node is a block node, return it.
// If the input node is not a block node, put it inside a block node and return that.
TIntermBlock *EnsureLoopBodyBlock(TIntermNode *node);

// Should be called from inside Compiler::compileTreeImpl() where the global level is in scope.
TIntermSymbol *ReferenceGlobalVariable(const ImmutableString &name,
                                       const TSymbolTable &symbolTable);

TIntermSymbol *ReferenceBuiltInVariable(const ImmutableString &name,
                                        TSymbolTable &symbolTable,
                                        int shaderVersion);

TIntermTyped *CreateBuiltInFunctionCallNode(const char *name,
                                            TIntermSequence *arguments,
                                            const TSymbolTable &symbolTable,
                                            int shaderVersion);
TIntermTyped *CreateBuiltInFunctionCallNode(const char *name,
                                            const std::initializer_list<TIntermNode *> &arguments,
                                            const TSymbolTable &symbolTable,
                                            int shaderVersion);
TIntermTyped *CreateBuiltInUnaryFunctionCallNode(const char *name,
                                                 TIntermTyped *argument,
                                                 const TSymbolTable &symbolTable,
                                                 int shaderVersion);

inline void GetSwizzleIndex(TVector<uint32_t> *indexOut) {}

template <typename T, typename... ArgsT>
void GetSwizzleIndex(TVector<uint32_t> *indexOut, T arg, ArgsT... args)
{
    indexOut->push_back(arg);
    GetSwizzleIndex(indexOut, args...);
}

template <typename... ArgsT>
TIntermSwizzle *CreateSwizzle(TIntermTyped *reference, ArgsT... args)
{
    TVector<uint32_t> swizzleIndex;
    GetSwizzleIndex(&swizzleIndex, args...);
    return new TIntermSwizzle(reference, swizzleIndex);
}

// Returns true if a block ends in a branch (break, continue, return, etc).  This is only correct
// after PruneNoOps, because it expects empty blocks after a branch to have been already pruned,
// i.e. a block can only end in a branch if its last statement is a branch or is a block ending in
// branch.
bool EndsInBranch(TIntermBlock *block);

// Cast a scalar to the basic type of type. No-ops if scalar is already the right type.
TIntermNode *CastScalar(const TType &type, TIntermTyped *scalar);

// Given |(a, b, c, d)|, returns |d|.
TIntermTyped *RemoveCommaLeftHandSide(TIntermTyped *node);

// Called on root, moves declarations before function definitions.  Done so that transformations
// that need to do something with globals and append code for them in main() don't have to deal with
// declarations after main.
void MoveDeclarationsBeforeFunctions(TIntermBlock *root);

}  // namespace sh

#endif  // COMPILER_TRANSLATOR_INTERMNODEUTIL_H_
