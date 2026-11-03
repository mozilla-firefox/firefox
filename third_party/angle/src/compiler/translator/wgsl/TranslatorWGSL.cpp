//
// Copyright 2024 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/wgsl/TranslatorWGSL.h"
#include "common/unsafe_buffers.h"

#include <iostream>
#include <string_view>
#include <variant>

#include "GLSLANG/ShaderLang.h"
#include "common/log_utils.h"
#include "common/span.h"
#include "compiler/translator/BaseTypes.h"
#include "compiler/translator/Common.h"
#include "compiler/translator/Diagnostics.h"
#include "compiler/translator/ImmutableString.h"
#include "compiler/translator/ImmutableStringBuilder.h"
#include "compiler/translator/InfoSink.h"
#include "compiler/translator/IntermNode.h"
#include "compiler/translator/Operator_autogen.h"
#include "compiler/translator/OutputTree.h"
#include "compiler/translator/StaticType.h"
#include "compiler/translator/SymbolUniqueId.h"
#include "compiler/translator/Types.h"
#include "compiler/translator/tree_ops/GatherDefaultUniforms.h"
#include "compiler/translator/tree_ops/MonomorphizeUnsupportedFunctions.h"
#include "compiler/translator/tree_ops/ReduceInterfaceBlocks.h"
#include "compiler/translator/tree_ops/RewriteArrayOfArrayOfOpaqueUniforms.h"
#include "compiler/translator/tree_ops/RewriteStructSamplers.h"
#include "compiler/translator/tree_ops/SeparateDeclarations.h"
#include "compiler/translator/tree_ops/wgsl/EmulateMutableFunctionParams.h"
#include "compiler/translator/tree_ops/wgsl/PullExpressionsIntoFunctions.h"
#include "compiler/translator/tree_ops/wgsl/RewriteMixedTypeMathExprs.h"
#include "compiler/translator/tree_ops/wgsl/RewriteMultielementSwizzleAssignment.h"
#include "compiler/translator/tree_util/BuiltIn_autogen.h"
#include "compiler/translator/tree_util/DriverUniform.h"
#include "compiler/translator/tree_util/FindMain.h"
#include "compiler/translator/tree_util/IntermNode_util.h"
#include "compiler/translator/tree_util/IntermTraverse.h"
#include "compiler/translator/tree_util/RunAtTheEndOfShader.h"
#include "compiler/translator/util.h"
#include "compiler/translator/wgsl/OutputUniformBlocks.h"
#include "compiler/translator/wgsl/RewritePipelineVariables.h"
#include "compiler/translator/wgsl/Utils.h"
#include "compiler/translator/wgsl/WGSLProgramPrelude.h"

namespace sh
{
namespace
{

constexpr bool kOutputTreeBeforeTranslation = false;
constexpr bool kOutputTranslatedShader      = false;

struct VarDecl
{
    const SymbolType symbolType = SymbolType::Empty;
    const ImmutableString &symbolName;
    const TType &type;
};

TUnorderedSet<TSymbolUniqueId> FindOverloadedFunctions(TIntermBlock *root)
{
    TSet<ImmutableString> funcNames;
    TUnorderedSet<TSymbolUniqueId> uniqueIds;
    for (TIntermNode *node : *root->getSequence())
    {
        if (TIntermFunctionDefinition *funcDef = node->getAsFunctionDefinition())
        {
            if (!funcNames.insert(funcDef->getFunction()->name()).second)
            {
                uniqueIds.insert(funcDef->getFunction()->uniqueId());
            }
        }
    }
    return uniqueIds;
}

struct OperatorInfo
{
    const char *opName;
    std::optional<WGSLWrapperFunction> wgslWrapperFn = std::nullopt;
    bool isPostfix                                   = false;

    bool IsSymbolicOperator() const { return opName && !std::isalnum(opName[0]); }
};

// When emitting a list of statements, this determines whether a semicolon follows the statement.
bool RequiresSemicolonTerminator(TIntermNode &node)
{
    if (node.getAsBlock())
    {
        return false;
    }
    if (node.getAsLoopNode())
    {
        return false;
    }
    if (node.getAsSwitchNode())
    {
        return false;
    }
    if (node.getAsIfElseNode())
    {
        return false;
    }
    if (node.getAsFunctionDefinition())
    {
        return false;
    }
    if (node.getAsCaseNode())
    {
        return false;
    }

    return true;
}

// For pretty formatting of the resulting WGSL text.
bool NewlinePad(TIntermNode &node)
{
    if (node.getAsFunctionDefinition())
    {
        return true;
    }
    if (TIntermDeclaration *declNode = node.getAsDeclarationNode())
    {
        ASSERT(declNode->getChildCount() == 1);
        TIntermNode &childNode = *declNode->getChildNode(0);
        if (TIntermSymbol *symbolNode = childNode.getAsSymbolNode())
        {
            const TVariable &var = symbolNode->variable();
            return var.getType().isStructSpecifier();
        }
        return false;
    }
    return false;
}

// A traverser that generates WGSL as it walks the AST.
class OutputWGSLTraverser : public TIntermTraverser
{
  public:
    OutputWGSLTraverser(TInfoSinkBase *sink,
                        RewritePipelineVarOutput *rewritePipelineVarOutput,
                        UniformBlockMetadata *uniformBlockMetadata,
                        WGSLGenerationMetadataForUniforms *arrayElementTypesInUniforms,
                        const TUnorderedSet<TSymbolUniqueId> *overloadedFunctions,
                        WGSLProgramPrelude *prelude);
    ~OutputWGSLTraverser() override;

  protected:
    void visitSymbol(TIntermSymbol *node) override;
    void visitConstantUnion(TIntermConstantUnion *node) override;
    bool visitSwizzle(Visit visit, TIntermSwizzle *node) override;
    bool visitBinary(Visit visit, TIntermBinary *node) override;
    bool visitUnary(Visit visit, TIntermUnary *node) override;
    bool visitTernary(Visit visit, TIntermTernary *node) override;
    bool visitIfElse(Visit visit, TIntermIfElse *node) override;
    bool visitSwitch(Visit visit, TIntermSwitch *node) override;
    bool visitCase(Visit visit, TIntermCase *node) override;
    void visitFunctionPrototype(TIntermFunctionPrototype *node) override;
    bool visitFunctionDefinition(Visit visit, TIntermFunctionDefinition *node) override;
    bool visitAggregate(Visit visit, TIntermAggregate *node) override;
    bool visitBlock(Visit visit, TIntermBlock *node) override;
    bool visitGlobalQualifierDeclaration(Visit visit,
                                         TIntermGlobalQualifierDeclaration *node) override;
    bool visitDeclaration(Visit visit, TIntermDeclaration *node) override;
    bool visitLoop(Visit visit, TIntermLoop *node) override;
    bool visitBranch(Visit visit, TIntermBranch *node) override;
    void visitPreprocessorDirective(TIntermPreprocessorDirective *node) override;

  private:
    struct EmitVariableDeclarationConfig
    {
        EmitTypeConfig typeConfig;
        bool isParameter                                     = false;
        std::optional<WgslPointerAddressSpace> emitAsPointer = std::nullopt;
        bool isDeclaration                                   = false;
        bool isGlobalScope                                   = false;
    };

    void groupedTraverse(TIntermNode &node);
    void emitNameOf(const VarDecl &decl);
    void emitBareTypeName(const TType &type);
    void emitType(const TType &type);
    void emitSingleConstant(const TConstantUnion *const constUnion);
    const TConstantUnion *emitConstantUnionArray(const TConstantUnion *const constUnion,
                                                 const size_t size);
    const TConstantUnion *emitConstantUnion(const TType &type,
                                            const TConstantUnion *constUnionBegin);
    bool isStatement(TIntermNode *current);
    OperatorInfo useOperatorAndGetInfo(TIntermNode *current,
                                       TOperator op,
                                       const TType &resultType,
                                       const TType *argType0,
                                       const TType *argType1,
                                       const TType *argType2);
    const TField &getDirectField(const TIntermTyped &fieldsNode, TIntermTyped &indexNode);
    void emitIndentation();
    void emitOpenBrace();
    void emitCloseBrace();
    bool emitBlock(angle::Span<TIntermNode *> nodes);
    void emitFunctionName(const TFunction &func);
    void emitFunctionSignature(const TFunction &func);
    void emitFunctionReturn(const TFunction &func);
    void emitFunctionParameter(const TFunction &func, const TVariable &param);
    void emitStructDeclaration(const TType &type);
    void emitVariableDeclaration(const VarDecl &decl,
                                 const EmitVariableDeclarationConfig &evdConfig);
    void emitArrayIndex(TIntermTyped &leftNode, TIntermTyped &rightNode);
    void emitStructIndex(TIntermBinary *binaryNode);
    void emitStructIndexNoUnwrapping(TIntermBinary *binaryNode);
    void emitTextureBuiltin(const TOperator op, const TIntermSequence &args);

    bool emitForLoop(TIntermLoop *);
    bool emitWhileLoop(TIntermLoop *);
    bool emulateDoWhileLoop(TIntermLoop *);

    TInfoSinkBase &mSink;
    const RewritePipelineVarOutput *mRewritePipelineVarOutput;
    const UniformBlockMetadata *mUniformBlockMetadata;
    WGSLGenerationMetadataForUniforms *mWGSLGenerationMetadataForUniforms;
    const TUnorderedSet<TSymbolUniqueId> *mOverloadedFunctions;
    WGSLProgramPrelude *mPrelude;

    int mIndentLevel        = -1;
    int mLastIndentationPos = -1;

    TUnorderedSet<TSymbolUniqueId> mIsActuallyOfPointerType;
};

OutputWGSLTraverser::OutputWGSLTraverser(
    TInfoSinkBase *sink,
    RewritePipelineVarOutput *rewritePipelineVarOutput,
    UniformBlockMetadata *uniformBlockMetadata,
    WGSLGenerationMetadataForUniforms *wgslGenerationMetadataForUniforms,
    const TUnorderedSet<TSymbolUniqueId> *overloadedFunctions,
    WGSLProgramPrelude *prelude)
    : TIntermTraverser(true, false, false),
      mSink(*sink),
      mRewritePipelineVarOutput(rewritePipelineVarOutput),
      mUniformBlockMetadata(uniformBlockMetadata),
      mWGSLGenerationMetadataForUniforms(wgslGenerationMetadataForUniforms),
      mOverloadedFunctions(overloadedFunctions),
      mPrelude(prelude)
{}

OutputWGSLTraverser::~OutputWGSLTraverser() = default;

void OutputWGSLTraverser::groupedTraverse(TIntermNode &node)
{
    // TODO(anglebug.com/42267100): to make generated code more readable, do not always
    // emit parentheses like WGSL is some Lisp dialect.
    const bool emitParens = true;

    if (emitParens)
    {
        mSink << "(";
    }

    node.traverse(this);

    if (emitParens)
    {
        mSink << ")";
    }
}

void OutputWGSLTraverser::emitNameOf(const VarDecl &decl)
{
    WriteNameOf(mSink, decl.symbolType, decl.symbolName, kUserVariableNamePrefix);
}

void OutputWGSLTraverser::emitIndentation()
{
    ASSERT(mIndentLevel >= 0);

    if (mLastIndentationPos == mSink.size())
    {
        return;  // Line is already indented.
    }

    for (int i = 0; i < mIndentLevel; ++i)
    {
        mSink << "  ";
    }

    mLastIndentationPos = mSink.size();
}

void OutputWGSLTraverser::emitOpenBrace()
{
    ASSERT(mIndentLevel >= 0);

    emitIndentation();
    mSink << "{\n";
    ++mIndentLevel;
}

void OutputWGSLTraverser::emitCloseBrace()
{
    ASSERT(mIndentLevel >= 1);

    --mIndentLevel;
    emitIndentation();
    mSink << "}";
}

void OutputWGSLTraverser::visitSymbol(TIntermSymbol *symbolNode)
{

    const TVariable &var = symbolNode->variable();
    const TType &type    = var.getType();
    ASSERT(var.symbolType() != SymbolType::Empty);

    // Default uniforms should no longer be referenced--they should all be in an interface block by
    // now.
    // TODO(anglebug.com/376553328): gl_DepthRange should be handled by referencing driver
    // uniforms--then the check for builtin default uniforms can be removed here.
    if (type.getBasicType() == TBasicType::EbtVoid ||
        (IsDefaultUniform(type) && var.symbolType() != SymbolType::BuiltIn))
    {
        UNREACHABLE();
    }
    else
    {
        // Accesses of pipeline variables should be rewritten as struct accesses.
        if (mRewritePipelineVarOutput->IsInputVar(var.uniqueId()))
        {
            mSink << kBuiltinInputStructName << ".";
            WriteNameOf(mSink, var, kUserVariableNamePrefix);
        }
        else if (mRewritePipelineVarOutput->IsOutputVar(var.uniqueId()))
        {
            mSink << kBuiltinOutputStructName << ".";
            WriteNameOf(mSink, var, kUserVariableNamePrefix);
        }
        else
        {
            // If this symbol refers to an outparam, that param was translated as a pointer and must
            // be dereferenced to be accessed.
            // Similarly, some symbol are actually pointers, even though in the GLSL AST they are
            // regular types (as GLSL does not have pointers, just out variables).
            const bool isOutParam = IsParamOut(var.getType().getQualifier());
            const bool isActuallyOfPointerType =
                mIsActuallyOfPointerType.contains(symbolNode->uniqueId());
            const bool needsDereference = isOutParam || isActuallyOfPointerType;
            if (needsDereference)
            {
                mSink << "(*";
            }
            WriteNameOf(mSink, var, kUserVariableNamePrefix);
            if (needsDereference)
            {
                mSink << ")";
            }
        }

        if (var.symbolType() == SymbolType::BuiltIn)
        {
            ASSERT(mRewritePipelineVarOutput->IsInputVar(var.uniqueId()) ||
                   mRewritePipelineVarOutput->IsOutputVar(var.uniqueId()) ||
                   type.getQualifier() == EvqDepthRange);
            // TODO(anglebug.com/376553328): support gl_DepthRange.
            // Match the name of the struct field in `mRewritePipelineVarOutput`.
            mSink << "_";
        }
    }
}

void OutputWGSLTraverser::emitSingleConstant(const TConstantUnion *const constUnion)
{
    switch (constUnion->getType())
    {
        case TBasicType::EbtBool:
        {
            mSink << (constUnion->getBConst() ? "true" : "false");
        }
        break;

        case TBasicType::EbtFloat:
        {
            float value = constUnion->getFConst();
            if (std::isnan(value))
            {
                UNIMPLEMENTED();
                // TODO(anglebug.com/42267100): this is not a valid constant in WGPU.
                // You can't even do something like bitcast<f32>(0xffffffffu).
                // The WGSL compiler still complains. I think this is because
                // WGSL supports implementations compiling with -ffastmath and
                // therefore nans and infinities are assumed to not exist.
                // See also https://github.com/gpuweb/gpuweb/issues/3749.
                mSink << "NAN_INVALID";
            }
            else if (std::isinf(value))
            {
                UNIMPLEMENTED();
                // see above.
                mSink << "INFINITY_INVALID";
            }
            else
            {
                mSink << value << "f";
            }
        }
        break;

        case TBasicType::EbtInt:
        {
            mSink << constUnion->getIConst() << "i";
        }
        break;

        case TBasicType::EbtUInt:
        {
            mSink << constUnion->getUConst() << "u";
        }
        break;

        default:
        {
            UNIMPLEMENTED();
        }
    }
}

const TConstantUnion *OutputWGSLTraverser::emitConstantUnionArray(
    const TConstantUnion *const constUnion,
    const size_t size)
{
    const TConstantUnion *constUnionIterated = constUnion;
    for (size_t i = 0; i < size; i++, ANGLE_UNSAFE_TODO(constUnionIterated++))
    {
        emitSingleConstant(constUnionIterated);

        if (i != size - 1)
        {
            mSink << ", ";
        }
    }
    return constUnionIterated;
}

const TConstantUnion *OutputWGSLTraverser::emitConstantUnion(const TType &type,
                                                             const TConstantUnion *constUnionBegin)
{
    const TConstantUnion *constUnionCurr = constUnionBegin;
    const TStructure *structure          = type.getStruct();
    if (structure)
    {
        emitType(type);
        // Structs are constructed with parentheses in WGSL.
        mSink << "(";
        // Emit the constructor parameters. Both GLSL and WGSL require there to be the same number
        // of parameters as struct fields.
        const TFieldList &fields = structure->fields();
        for (size_t i = 0; i < fields.size(); ++i)
        {
            const TType *fieldType = fields[i]->type();
            constUnionCurr         = emitConstantUnion(*fieldType, constUnionCurr);
            if (i != fields.size() - 1)
            {
                mSink << ", ";
            }
        }
        mSink << ")";
    }
    else
    {
        size_t size = type.getObjectSize();
        // If the type's size is more than 1, the type needs to be written with parentheses. This
        // applies for vectors, matrices, and arrays.
        bool writeType = size > 1;
        if (writeType)
        {
            emitType(type);
            mSink << "(";
        }
        constUnionCurr = emitConstantUnionArray(constUnionCurr, size);
        if (writeType)
        {
            mSink << ")";
        }
    }
    return constUnionCurr;
}

void OutputWGSLTraverser::visitConstantUnion(TIntermConstantUnion *constValueNode)
{
    emitConstantUnion(constValueNode->getType(), constValueNode->getConstantValue());
}

bool OutputWGSLTraverser::visitSwizzle(Visit, TIntermSwizzle *swizzleNode)
{
    groupedTraverse(*swizzleNode->getOperand());
    mSink << "." << swizzleNode->getOffsetsAsXYZW();

    return false;
}

bool OutputWGSLTraverser::isStatement(TIntermNode *current)
{
    if (getParentNode()->getAsLoopNode() != nullptr || getParentNode()->getAsBlock() != nullptr)
    {
        return current->getAsBlock() == nullptr;
    }
    return false;
}

OperatorInfo OutputWGSLTraverser::useOperatorAndGetInfo(TIntermNode *current,
                                                        TOperator op,
                                                        const TType &resultType,
                                                        const TType *argType0,
                                                        const TType *argType1,
                                                        const TType *argType2)
{
    switch (op)
    {
        case TOperator::EOpComma:
            // WGSL does not have a comma operator or any other way to implement "statement list as
            // an expression", so nested expressions will have to be pulled out into statements.
            // This should have been done by a preprocessing.
            UNREACHABLE();
            return {"TODO_operator"};
        case TOperator::EOpInitialize:
            return {"="};
        // Assignments are always statements in WGSL and do not yield a value, so they are
        // implemented as functions, unless the current expression is a statement and is a scalar
        // integer, in which case the normal postfix operator will do.
        case TOperator::EOpAssign:
        // Compound assignments now exist: https://www.w3.org/TR/WGSL/#compound-assignment-sec
        case TOperator::EOpAddAssign:
        case TOperator::EOpSubAssign:
        case TOperator::EOpMulAssign:
        case TOperator::EOpDivAssign:
        case TOperator::EOpIModAssign:
        case TOperator::EOpBitShiftLeftAssign:
        case TOperator::EOpBitShiftRightAssign:
        case TOperator::EOpBitwiseAndAssign:
        case TOperator::EOpBitwiseXorAssign:
        case TOperator::EOpBitwiseOrAssign:
        case TOperator::EOpVectorTimesScalarAssign:
        case TOperator::EOpVectorTimesMatrixAssign:
        case TOperator::EOpMatrixTimesScalarAssign:
        case TOperator::EOpMatrixTimesMatrixAssign:
            if (isStatement(current))
            {
                return {GetOperatorString(op)};
            }
            else
            {
                return OperatorInfo{nullptr, mPrelude->assign(*argType0, *argType1, op)};
            }

        case TOperator::EOpAdd:
            return {"+"};
        case TOperator::EOpSub:
            return {"-"};
        case TOperator::EOpMul:
            return {"*"};
        case TOperator::EOpDiv:
            return {"/"};
        // TODO(anglebug.com/42267100): Works different from GLSL for negative numbers.
        // https://github.com/gpuweb/gpuweb/discussions/2204#:~:text=not%20WGSL%3B%20etc.-,Inconsistent%20mod/%25%20operator,-At%20first%20glance
        // GLSL does `x - y * floor(x/y)`, WGSL does x - y * trunc(x/y).
        case TOperator::EOpIMod:
        case TOperator::EOpMod:
            return {"%"};
        // TODO(anglebug.com/42267100): bitwise operations can be between scalars and vectors, but
        // not in WGSL.
        case TOperator::EOpBitShiftLeft:
            return {"<<"};
        case TOperator::EOpBitShiftRight:
            return {">>"};
        case TOperator::EOpBitwiseAnd:
            return {"&"};
        case TOperator::EOpBitwiseXor:
            return {"^"};
        case TOperator::EOpBitwiseOr:
            return {"|"};
        case TOperator::EOpLessThan:
            return {"<"};
        case TOperator::EOpGreaterThan:
            return {">"};
        case TOperator::EOpLessThanEqual:
            return {"<="};
        case TOperator::EOpGreaterThanEqual:
            return {">="};
        // Component-wise comparisons are done with regular infix operators in WGSL:
        // https://www.w3.org/TR/WGSL/#comparison-expr
        case TOperator::EOpLessThanComponentWise:
            return {"<"};
        case TOperator::EOpLessThanEqualComponentWise:
            return {"<="};
        case TOperator::EOpGreaterThanEqualComponentWise:
            return {">="};
        case TOperator::EOpGreaterThanComponentWise:
            return {">"};
        case TOperator::EOpLogicalOr:
            return {"||"};
        // Logical XOR is only applied to boolean expressions so it's the same as "not equals".
        // Neither short-circuits.
        case TOperator::EOpLogicalXor:
            return {"!="};
        case TOperator::EOpLogicalAnd:
            return {"&&"};
        case TOperator::EOpNegative:
            return {"-"};
        case TOperator::EOpPositive:
            return {""};
        case TOperator::EOpLogicalNot:
            return {"!"};
        // Component-wise not done with normal prefix unary operator in WGSL:
        // https://www.w3.org/TR/WGSL/#logical-expr
        case TOperator::EOpNotComponentWise:
            return {"!"};
        case TOperator::EOpBitwiseNot:
            return {"~"};
        // ++ and -- are always statements in WGSL and do not yield a value, so they are
        // implemented as functions, unless the current expression is a statement and is a scalar
        // integer, in which case the normal postfix operator will do.
        // Note that WGSL only allows increments of scalar integers, so this also uses a function to
        // increment floats.
        case TOperator::EOpPostIncrement:
            if (isStatement(current) && argType0->isScalarInt())
            {
                return OperatorInfo{"++", std::nullopt, /*isPostfix=*/true};
            }
            else
            {
                return OperatorInfo{"", mPrelude->postIncrement(*argType0)};
            }
        case TOperator::EOpPostDecrement:
            if (isStatement(current) && argType0->isScalarInt())
            {
                return OperatorInfo{"--", std::nullopt, /*isPostfix=*/true};
            }
            else
            {
                return OperatorInfo{"", mPrelude->postDecrement(*argType0)};
            }
        // NOTE: ++ and -- can only be postfix unary operators in WGSL. If the current expression is
        // a statement and is a scalar integer, just use the postfix operator, otherwise use a
        // function call as above.
        case TOperator::EOpPreIncrement:
            if (isStatement(current) && argType0->isScalarInt())
            {
                return OperatorInfo{"++", std::nullopt, /*isPostfix=*/true};
            }
            else
            {
                return OperatorInfo{"", mPrelude->preIncrement(*argType0)};
            }
        case TOperator::EOpPreDecrement:
            if (isStatement(current) && argType0->isScalarInt())
            {
                return OperatorInfo{"--", std::nullopt, /*isPostfix=*/true};
            }
            else
            {
                return OperatorInfo{"", mPrelude->preDecrement(*argType0)};
            }
        case TOperator::EOpVectorTimesScalar:
            return {"*"};
        case TOperator::EOpVectorTimesMatrix:
            return {"*"};
        case TOperator::EOpMatrixTimesVector:
            return {"*"};
        case TOperator::EOpMatrixTimesScalar:
            return {"*"};
        case TOperator::EOpMatrixTimesMatrix:
            return {"*"};
        case TOperator::EOpEqualComponentWise:
            return {"=="};
        case TOperator::EOpNotEqualComponentWise:
            return {"!="};

        // TODO(anglebug.com/42267100): structs, matrices, and arrays are not comparable with WGSL's
        // == or !=. Comparing vectors results in a component-wise comparison returning a boolean
        // vector, which is different from GLSL (which use equal(vec, vec) for component-wise
        // comparison)
        case TOperator::EOpEqual:
            if (argType0->isVector() && argType1->isVector())
            {
                return {"==", WGSLWrapperFunction{ImmutableString("all("), ImmutableString(")")}};
            }

            if ((argType0->getStruct() && argType1->getStruct()) ||
                (argType0->isArray() && argType1->isArray()) ||
                (argType0->isMatrix() && argType1->isMatrix()))

            {
                UNIMPLEMENTED();
                return {"TODO_operator"};
            }

            return {"=="};

        case TOperator::EOpNotEqual:
            if ((argType0->isVector() && argType1->isVector()))
            {
                return {"!=", WGSLWrapperFunction{ImmutableString("all("), ImmutableString(")")}};
            }

            if ((argType0->getStruct() && argType1->getStruct()) ||
                (argType0->isArray() && argType1->isArray()) ||
                (argType0->isMatrix() && argType1->isMatrix()))
            {
                UNIMPLEMENTED();
                return {"TODO_operator"};
            }

            return {"!="};

        case TOperator::EOpKill:
        case TOperator::EOpReturn:
        case TOperator::EOpBreak:
        case TOperator::EOpContinue:
            // These should all be emitted in visitBranch().
            UNREACHABLE();
            return {"UNREACHABLE_operator"};
        case TOperator::EOpRadians:
            return {"radians"};
        case TOperator::EOpDegrees:
            return {"degrees"};
        case TOperator::EOpAtan:
            return argType1 == nullptr ? OperatorInfo{"atan"} : OperatorInfo{"atan2"};
        case TOperator::EOpRefract:
            return argType0->isVector() ? OperatorInfo{"refract"} : OperatorInfo{"TODO_operator"};
        case TOperator::EOpDistance:
            return {"distance"};
        case TOperator::EOpLength:
            return {"length"};
        case TOperator::EOpDot:
            return argType0->isVector() ? OperatorInfo{"dot"} : OperatorInfo{"*"};
        case TOperator::EOpNormalize:
            return argType0->isVector() ? OperatorInfo{"normalize"} : OperatorInfo{"sign"};
        case TOperator::EOpFaceforward:
            return argType0->isVector() ? OperatorInfo{"faceForward"}
                                        : OperatorInfo{"TODO_Operator"};
        case TOperator::EOpReflect:
            return argType0->isVector() ? OperatorInfo{"reflect"} : OperatorInfo{"TODO_Operator"};
        case TOperator::EOpMatrixCompMult:
            return {"TODO_Operator"};
        case TOperator::EOpOuterProduct:
            return {"TODO_Operator"};
        case TOperator::EOpSign:
            return {"sign"};

        case TOperator::EOpAbs:
            return {"abs"};
        case TOperator::EOpAll:
            return {"all"};
        case TOperator::EOpAny:
            return {"any"};
        case TOperator::EOpSin:
            return {"sin"};
        case TOperator::EOpCos:
            return {"cos"};
        case TOperator::EOpTan:
            return {"tan"};
        case TOperator::EOpAsin:
            return {"asin"};
        case TOperator::EOpAcos:
            return {"acos"};
        case TOperator::EOpSinh:
            return {"sinh"};
        case TOperator::EOpCosh:
            return {"cosh"};
        case TOperator::EOpTanh:
            return {"tanh"};
        case TOperator::EOpAsinh:
            return {"asinh"};
        case TOperator::EOpAcosh:
            return {"acosh"};
        case TOperator::EOpAtanh:
            return {"atanh"};
        case TOperator::EOpFma:
            return {"fma"};
        // TODO(anglebug.com/42267100): Won't accept pow(vec<f32>, f32).
        // https://github.com/gpuweb/gpuweb/discussions/2204#:~:text=Similarly%20pow(vec3%3Cf32%3E%2C%20f32)%20works%20in%20GLSL%20but%20not%20WGSL
        case TOperator::EOpPow:
            return {"pow"};  // GLSL's pow excludes negative x
        case TOperator::EOpExp:
            return {"exp"};
        case TOperator::EOpExp2:
            return {"exp2"};
        case TOperator::EOpLog:
            return {"log"};
        case TOperator::EOpLog2:
            return {"log2"};
        case TOperator::EOpSqrt:
            return {"sqrt"};
        case TOperator::EOpFloor:
            return {"floor"};
        case TOperator::EOpTrunc:
            return {"trunc"};
        case TOperator::EOpCeil:
            return {"ceil"};
        case TOperator::EOpFract:
            return {"fract"};
        case TOperator::EOpMin:
            return {"min"};
        case TOperator::EOpMax:
            return {"max"};
        case TOperator::EOpRound:
            return {
                "round"};  // TODO(anglebug.com/42267100): this is wrong and must round away from
                           // zero if there is a tie. This always rounds to the even number.
        case TOperator::EOpRoundEven:
            return {"round"};
        // TODO(anglebug.com/42267100):
        // https://github.com/gpuweb/gpuweb/discussions/2204#:~:text=clamp(vec2%3Cf32%3E%2C%20f32%2C%20f32)%20works%20in%20GLSL%20but%20not%20WGSL%3B%20etc.
        // Need to expand clamp(vec<f32>, low : f32, high : f32) ->
        // clamp(vec<f32>, vec<f32>(low), vec<f32>(high))
        case TOperator::EOpClamp:
            return {"clamp"};
        case TOperator::EOpSaturate:
            return {"saturate"};
        case TOperator::EOpMix:
            if (!argType1->isScalar() && argType2 && argType2->getBasicType() == EbtBool)
            {
                return {"TODO_Operator"};
            }
            return {"mix"};
        case TOperator::EOpStep:
            return {"step"};
        case TOperator::EOpSmoothstep:
            return {"smoothstep"};
        case TOperator::EOpModf:
            UNIMPLEMENTED();  // TODO(anglebug.com/42267100): in WGSL this returns a struct, GLSL it
                              // uses a return value and an outparam
            return {"modf"};
        case TOperator::EOpIsnan:
        case TOperator::EOpIsinf:
            UNIMPLEMENTED();  // TODO(anglebug.com/42267100): WGSL does not allow NaNs or infinity.
                              // What to do about shaders that require this?
            // Implementations are allowed to assume overflow, infinities, and NaNs are not present
            // at runtime, however. https://www.w3.org/TR/WGSL/#floating-point-evaluation
            return {"TODO_Operator"};
        case TOperator::EOpLdexp:
            // TODO(anglebug.com/42267100): won't accept first arg vector, second arg scalar
            return {"ldexp"};
        case TOperator::EOpFrexp:
            return {"frexp"};  // TODO(anglebug.com/42267100): returns a struct
        case TOperator::EOpInversesqrt:
            return {"inverseSqrt"};
        case TOperator::EOpCross:
            return {"cross"};
            // TODO(anglebug.com/42267100): are these the same? dpdxCoarse() vs dpdxFine()?
        case TOperator::EOpDFdx:
            return {"dpdx"};
        case TOperator::EOpDFdy:
            return {"dpdy"};
        case TOperator::EOpFwidth:
            return {"fwidth"};
        case TOperator::EOpTranspose:
            return {"transpose"};
        case TOperator::EOpDeterminant:
            return {"determinant"};

        case TOperator::EOpInverse:
            return {"TODO_Operator"};  // No builtin invert().
                                       // https://github.com/gpuweb/gpuweb/issues/4115

        // TODO(anglebug.com/42267100): these interpolateAt*() are not builtin
        case TOperator::EOpInterpolateAtCentroid:
            return {"TODO_Operator"};
        case TOperator::EOpInterpolateAtSample:
            return {"TODO_Operator"};
        case TOperator::EOpInterpolateAtOffset:
            return {"TODO_Operator"};
        case TOperator::EOpInterpolateAtCenter:
            return {"TODO_Operator"};

        case TOperator::EOpFloatBitsToInt:
        case TOperator::EOpFloatBitsToUint:
        case TOperator::EOpIntBitsToFloat:
        case TOperator::EOpUintBitsToFloat:
        {
#define BITCAST_SCALAR()                   \
    do                                     \
        switch (resultType.getBasicType()) \
        {                                  \
            case TBasicType::EbtInt:       \
                return {"bitcast<i32>"};   \
            case TBasicType::EbtUInt:      \
                return {"bitcast<u32>"};   \
            case TBasicType::EbtFloat:     \
                return {"bitcast<f32>"};   \
            default:                       \
                UNIMPLEMENTED();           \
                return {"TOperator_TODO"}; \
        }                                  \
    while (false)

#define BITCAST_VECTOR(vecSize)                          \
    do                                                   \
        switch (resultType.getBasicType())               \
        {                                                \
            case TBasicType::EbtInt:                     \
                return {"bitcast<vec" vecSize "<i32>>"}; \
            case TBasicType::EbtUInt:                    \
                return {"bitcast<vec" vecSize "<u32>>"}; \
            case TBasicType::EbtFloat:                   \
                return {"bitcast<vec" vecSize "<f32>>"}; \
            default:                                     \
                UNIMPLEMENTED();                         \
                return {"TOperator_TODO"};               \
        }                                                \
    while (false)

            if (resultType.isScalar())
            {
                BITCAST_SCALAR();
            }
            else if (resultType.isVector())
            {
                switch (resultType.getNominalSize())
                {
                    case 2:
                        BITCAST_VECTOR("2");
                    case 3:
                        BITCAST_VECTOR("3");
                    case 4:
                        BITCAST_VECTOR("4");
                    default:
                        UNREACHABLE();
                        return {nullptr};
                }
            }
            else
            {
                UNIMPLEMENTED();
                return {"TOperator_TODO"};
            }

#undef BITCAST_SCALAR
#undef BITCAST_VECTOR
        }

        case TOperator::EOpPackUnorm2x16:
            return {"pack2x16unorm"};
        case TOperator::EOpPackSnorm2x16:
            return {"pack2x16snorm"};

        case TOperator::EOpPackUnorm4x8:
            return {"pack4x8unorm"};
        case TOperator::EOpPackSnorm4x8:
            return {"pack4x8snorm"};

        case TOperator::EOpUnpackUnorm2x16:
            return {"unpack2x16unorm"};
        case TOperator::EOpUnpackSnorm2x16:
            return {"unpack2x16snorm"};

        case TOperator::EOpUnpackUnorm4x8:
            return {"unpack4x8unorm"};
        case TOperator::EOpUnpackSnorm4x8:
            return {"unpack4x8snorm"};

        case TOperator::EOpPackHalf2x16:
            return {"pack2x16float"};
        case TOperator::EOpUnpackHalf2x16:
            return {"unpack2x16float"};

        case TOperator::EOpBarrier:
            UNREACHABLE();
            return {"TOperator_TODO"};
        case TOperator::EOpMemoryBarrier:
            // TODO(anglebug.com/42267100): does this exist in WGPU? Device-scoped memory barrier?
            // Maybe storageBarrier()?
            UNREACHABLE();
            return {"TOperator_TODO"};
        case TOperator::EOpGroupMemoryBarrier:
            return {"workgroupBarrier"};
        case TOperator::EOpMemoryBarrierAtomicCounter:
        case TOperator::EOpMemoryBarrierBuffer:
        case TOperator::EOpMemoryBarrierShared:
            UNREACHABLE();
            return {"TOperator_TODO"};
        case TOperator::EOpAtomicAdd:
            return {"atomicAdd"};
        case TOperator::EOpAtomicMin:
            return {"atomicMin"};
        case TOperator::EOpAtomicMax:
            return {"atomicMax"};
        case TOperator::EOpAtomicAnd:
            return {"atomicAnd"};
        case TOperator::EOpAtomicOr:
            return {"atomicOr"};
        case TOperator::EOpAtomicXor:
            return {"atomicXor"};
        case TOperator::EOpAtomicExchange:
            return {"atomicExchange"};
        case TOperator::EOpAtomicCompSwap:
            return {"atomicCompareExchangeWeak"};  // TODO(anglebug.com/42267100): returns a struct.
        case TOperator::EOpBitfieldExtract:
        case TOperator::EOpBitfieldInsert:
        case TOperator::EOpBitfieldReverse:
        case TOperator::EOpBitCount:
        case TOperator::EOpFindLSB:
        case TOperator::EOpFindMSB:
        case TOperator::EOpUaddCarry:
        case TOperator::EOpUsubBorrow:
        case TOperator::EOpUmulExtended:
        case TOperator::EOpImulExtended:
        case TOperator::EOpEmitVertex:
        case TOperator::EOpEndPrimitive:
        case TOperator::EOpArrayLength:
            UNIMPLEMENTED();
            return {"TOperator_TODO"};

        case TOperator::EOpNull:
        case TOperator::EOpConstruct:
        case TOperator::EOpCallFunctionInAST:
        case TOperator::EOpCallInternalRawFunction:
        case TOperator::EOpIndexDirect:
        case TOperator::EOpIndexIndirect:
        case TOperator::EOpIndexDirectStruct:
        case TOperator::EOpIndexDirectInterfaceBlock:
            UNREACHABLE();
            return {nullptr};
        default:
            // Any other built-in function.
            return {nullptr};
    }
}

const TField &OutputWGSLTraverser::getDirectField(const TIntermTyped &fieldsNode,
                                                  TIntermTyped &indexNode)
{
    const TType &fieldsType = fieldsNode.getType();

    const TFieldListCollection *fieldListCollection = fieldsType.getStruct();
    if (fieldListCollection == nullptr)
    {
        fieldListCollection = fieldsType.getInterfaceBlock();
    }
    ASSERT(fieldListCollection);

    const TIntermConstantUnion *indexNodeAsConstantUnion = indexNode.getAsConstantUnion();
    ASSERT(indexNodeAsConstantUnion);
    const TConstantUnion &index = *indexNodeAsConstantUnion->getConstantValue();

    ASSERT(index.getType() == TBasicType::EbtInt);

    const TFieldList &fieldList = fieldListCollection->fields();
    const int indexVal          = index.getIConst();
    const TField &field         = *fieldList[indexVal];

    return field;
}

// Indexes arrays but also matrices.
void OutputWGSLTraverser::emitArrayIndex(TIntermTyped &leftNode, TIntermTyped &rightNode)
{
    TType leftType = leftNode.getType();

    // Some arrays within the uniform address space have their element types wrapped in a struct
    // when generating WGSL, so this unwraps the element (as an optimization of converting the
    // entire array back to the unwrapped type).
    bool needsUnwrapping                  = false;
    bool isUniformMatrixNeedingConversion = false;
    bool isUniformBoolNeedingConversion   = false;
    TIntermBinary *leftNodeBinary         = leftNode.getAsBinaryNode();
    if (leftNodeBinary && leftNodeBinary->getOp() == TOperator::EOpIndexDirectStruct)
    {
        const TStructure *structure = leftNodeBinary->getLeft()->getType().getStruct();

        bool isInUniformAddressSpace =
            mUniformBlockMetadata->structsInUniformAddressSpace.count(structure->uniqueId().get());

        needsUnwrapping =
            structure && ElementTypeNeedsUniformWrapperStruct(isInUniformAddressSpace, &leftType);

        isUniformMatrixNeedingConversion = isInUniformAddressSpace && IsMatCx2(&leftType);

        isUniformBoolNeedingConversion =
            isInUniformAddressSpace && leftType.getBasicType() == EbtBool;

        ASSERT(!needsUnwrapping || !isUniformMatrixNeedingConversion);
    }

    enum class ConversionScope
    {
        kNoConversionFunction,
        kConvertTheIndexedMatrix,
        kConvertTheWholeArrayIndexExpression,
    };

    ConversionScope conversionFunctionScope = ConversionScope::kNoConversionFunction;

    // Emit the left side, which should be of type matrix or array (including array of matrices).
    if (needsUnwrapping || isUniformMatrixNeedingConversion || isUniformBoolNeedingConversion)
    {
        if (isUniformMatrixNeedingConversion)
        {
            // If this array index expression is yielding an std140 matCx2 (i.e.
            // array<ANGLE_wrapped_vec2, C>), just convert the entire expression to a WGSL matCx2,
            // instead of converting the entire array of std140 matCx2s into an array of WGSL
            // matCx2s and then indexing into it.
            //
            // NOTE: this could also be indexing a column vector of a matrix.
            TType baseType = leftType;
            baseType.toArrayBaseType();
            mSink << MakeMatCx2ConversionFunctionName(&baseType) << "(";
            // Make sure the conversion function referenced here is actually generated in the
            // resulting WGSL.
            mWGSLGenerationMetadataForUniforms->outputMatCx2Conversion.insert(baseType);

            // If this an index of a single matrix, it needs conversion *before* indexing.
            // Otherwise, if this is a index of an array of matrices, it needs conversion *after*
            // indexing.
            if (leftType.isArray())
            {
                conversionFunctionScope = ConversionScope::kConvertTheWholeArrayIndexExpression;
            }
            else
            {
                conversionFunctionScope = ConversionScope::kConvertTheIndexedMatrix;
            }
        }
        else if (isUniformBoolNeedingConversion)
        {
            // Convert just this one array element into a bool instead of converting the entire
            // array into an array of booleans and indexing into that.
            OutputUniformBoolOrBvecConversion(mSink, leftType);
            conversionFunctionScope = ConversionScope::kConvertTheWholeArrayIndexExpression;
        }
        emitStructIndexNoUnwrapping(leftNodeBinary);

        if (conversionFunctionScope == ConversionScope::kConvertTheIndexedMatrix)
        {
            mSink << ")";
        }
    }
    else
    {
        groupedTraverse(leftNode);
    }

    mSink << "[";
    const TConstantUnion *constIndex = rightNode.getConstantValue();
    // If the array index is a constant that we can statically verify is within array
    // bounds, just emit that constant.
    if (!leftType.isUnsizedArray() && constIndex != nullptr && constIndex->getType() == EbtInt &&
        constIndex->getIConst() >= 0 &&
        constIndex->getIConst() < static_cast<int>(leftType.isArray()
                                                       ? leftType.getOutermostArraySize()
                                                       : leftType.getNominalSize()))
    {
        emitSingleConstant(constIndex);
    }
    else
    {
        // If the array index is not a constant within the bounds of the array, clamp the
        // index.
        mSink << "clamp(";
        groupedTraverse(rightNode);
        mSink << ", 0, ";
        // Now find the array size and clamp it.
        if (leftType.isUnsizedArray())
        {
            // TODO(anglebug.com/42267100): This is a bug to traverse the `leftNode` a
            // second time if `leftNode` has side effects (and could also have performance
            // implications). This should be stored in a temporary variable. This might also
            // be a bug in the MSL shader compiler.
            mSink << "arrayLength(&";
            groupedTraverse(leftNode);
            mSink << ")";
        }
        else
        {
            uint32_t maxSize;
            if (leftType.isArray())
            {
                maxSize = leftType.getOutermostArraySize() - 1;
            }
            else
            {
                maxSize = leftType.getNominalSize() - 1;
            }
            mSink << maxSize;
        }
        // End the clamp() function.
        mSink << ")";
    }
    // End the array index operation.
    mSink << "]";

    if (needsUnwrapping)
    {
        mSink << "." << kWrappedStructFieldName;
    }

    if (conversionFunctionScope == ConversionScope::kConvertTheWholeArrayIndexExpression)
    {
        // Close conversion function call
        mSink << ")";
    }
}

void OutputWGSLTraverser::emitStructIndex(TIntermBinary *binaryNode)
{
    ASSERT(binaryNode->getOp() == TOperator::EOpIndexDirectStruct);
    TIntermTyped &leftNode  = *binaryNode->getLeft();
    const TType *binaryNodeType = &binaryNode->getType();

    const TStructure *structure = leftNode.getType().getStruct();
    ASSERT(structure);

    bool isInUniformAddressSpace =
        mUniformBlockMetadata->structsInUniformAddressSpace.count(structure->uniqueId().get());

    bool isUniformMatrixNeedingConversion = isInUniformAddressSpace && IsMatCx2(binaryNodeType);

    bool isUniformBoolNeedingConversion =
        isInUniformAddressSpace && binaryNode->getBasicType() == EbtBool;

    bool needsUnwrapping =
        ElementTypeNeedsUniformWrapperStruct(isInUniformAddressSpace, binaryNodeType);
    if (needsUnwrapping)
    {
        ASSERT(!isUniformMatrixNeedingConversion);

        mSink << MakeUnwrappingArrayConversionFunctionName(&binaryNode->getType()) << "(";
        // Make sure the conversion function referenced here is actually generated in the resulting
        // WGSL.
        mWGSLGenerationMetadataForUniforms->arrayElementTypesThatNeedUnwrappingConversions.insert(
            *binaryNodeType);
    }
    else if (isUniformMatrixNeedingConversion)
    {
        mSink << MakeMatCx2ConversionFunctionName(binaryNodeType) << "(";
        // Make sure the conversion function referenced here is actually generated in the resulting
        // WGSL.
        mWGSLGenerationMetadataForUniforms->outputMatCx2Conversion.insert(*binaryNodeType);
    }
    else if (isUniformBoolNeedingConversion)
    {
        // Should only trigger in case of a boolean not in an array.
        OutputUniformBoolOrBvecConversion(mSink, *binaryNodeType);
    }
    emitStructIndexNoUnwrapping(binaryNode);
    if (needsUnwrapping || isUniformMatrixNeedingConversion || isUniformBoolNeedingConversion)
    {
        mSink << ")";
    }
}

void OutputWGSLTraverser::emitStructIndexNoUnwrapping(TIntermBinary *binaryNode)
{
    ASSERT(binaryNode->getOp() == TOperator::EOpIndexDirectStruct);
    TIntermTyped &leftNode  = *binaryNode->getLeft();
    TIntermTyped &rightNode = *binaryNode->getRight();

    groupedTraverse(leftNode);
    mSink << ".";
    WriteNameOf(mSink, getDirectField(leftNode, rightNode), kUserVariableNamePrefix);
}

bool OutputWGSLTraverser::visitBinary(Visit, TIntermBinary *binaryNode)
{
    const TOperator op      = binaryNode->getOp();
    TIntermTyped &leftNode  = *binaryNode->getLeft();
    TIntermTyped &rightNode = *binaryNode->getRight();

    switch (op)
    {
        case TOperator::EOpIndexDirectStruct:
            emitStructIndex(binaryNode);
            break;
        case TOperator::EOpIndexDirectInterfaceBlock:
            UNREACHABLE();  // Interface blocks should have been converted into structs.
            break;
        case TOperator::EOpIndexDirect:
        case TOperator::EOpIndexIndirect:
            emitArrayIndex(leftNode, rightNode);
            break;

        default:
        {
            const TType &resultType = binaryNode->getType();
            const TType &leftType   = leftNode.getType();
            const TType &rightType  = rightNode.getType();

            const OperatorInfo opInfo =
                useOperatorAndGetInfo(binaryNode, op, resultType, &leftType, &rightType, nullptr);

            if (opInfo.wgslWrapperFn)
            {
                mSink << opInfo.wgslWrapperFn->prefix;
            }

            auto emitArgList = [&]() {
                leftNode.traverse(this);
                mSink << ", ";
                rightNode.traverse(this);
            };

            // x * y, x ^ y, etc.
            if (opInfo.IsSymbolicOperator())
            {
                groupedTraverse(leftNode);
                mSink << " " << opInfo.opName << " ";
                groupedTraverse(rightNode);
            }
            else if (opInfo.wgslWrapperFn)
            {
                // Any necessary parentheses should be contained in opInfo.wgslWrapperFn->prefix and
                // opInfo.wgslWrapperFn->suffix.
                emitArgList();
            }
            // E.g. builtin function calls
            else
            {
                mSink << opInfo.opName << "(";
                emitArgList();
                mSink << ")";
            }

            if (opInfo.wgslWrapperFn)
            {
                mSink << opInfo.wgslWrapperFn->suffix;
            }
        }
    }

    return false;
}

bool OutputWGSLTraverser::visitUnary(Visit, TIntermUnary *unaryNode)
{
    const TOperator op      = unaryNode->getOp();
    const TType &resultType = unaryNode->getType();

    TIntermTyped &arg    = *unaryNode->getOperand();
    const TType &argType = arg.getType();

    const OperatorInfo opInfo =
        useOperatorAndGetInfo(unaryNode, op, resultType, &argType, nullptr, nullptr);

    if (opInfo.wgslWrapperFn)
    {
        mSink << opInfo.wgslWrapperFn->prefix;
    }

    // Examples: -x, ~x, ~x
    if (opInfo.IsSymbolicOperator())
    {
        if (!opInfo.isPostfix)
        {
            mSink << opInfo.opName;
        }
        groupedTraverse(arg);
        if (opInfo.isPostfix)
        {
            mSink << opInfo.opName;
        }
    }
    else
    {
        mSink << opInfo.opName << "(";
        arg.traverse(this);
        mSink << ")";
    }

    if (opInfo.wgslWrapperFn)
    {
        mSink << opInfo.wgslWrapperFn->suffix;
    }

    return false;
}

bool OutputWGSLTraverser::visitTernary(Visit, TIntermTernary *conditionalNode)
{
    // WGSL does not have a ternary. https://github.com/gpuweb/gpuweb/issues/3747
    // The select() builtin is not short circuiting. Maybe we can get if () {} else {} as an
    // expression, which would also solve the comma operator problem.
    // TODO(anglebug.com/42267100): as mentioned above this is not correct if the operands have side
    // effects. Even if they don't have side effects it could have performance implications.
    // It also doesn't work with all types that ternaries do, e.g. arrays or structs.
    mSink << "select(";
    groupedTraverse(*conditionalNode->getFalseExpression());
    mSink << ", ";
    groupedTraverse(*conditionalNode->getTrueExpression());
    mSink << ", ";
    groupedTraverse(*conditionalNode->getCondition());
    mSink << ")";

    return false;
}

bool OutputWGSLTraverser::visitIfElse(Visit, TIntermIfElse *ifThenElseNode)
{
    TIntermTyped &condNode = *ifThenElseNode->getCondition();
    TIntermBlock *thenNode = ifThenElseNode->getTrueBlock();
    TIntermBlock *elseNode = ifThenElseNode->getFalseBlock();

    mSink << "if (";
    condNode.traverse(this);
    mSink << ")";

    if (thenNode)
    {
        mSink << "\n";
        thenNode->traverse(this);
    }
    else
    {
        mSink << " {}";
    }

    if (elseNode)
    {
        mSink << "\n";
        emitIndentation();
        mSink << "else\n";
        elseNode->traverse(this);
    }

    return false;
}

bool OutputWGSLTraverser::visitSwitch(Visit, TIntermSwitch *switchNode)
{
    TIntermBlock &stmtList = *switchNode->getStatementList();

    emitIndentation();
    mSink << "switch ";
    switchNode->getInit()->traverse(this);
    mSink << "\n";

    emitOpenBrace();

    // TODO(anglebug.com/42267100): Case statements that fall through need to combined into a single
    // case statement with multiple labels.

    const size_t stmtCount = stmtList.getChildCount();
    bool inCaseList        = false;
    size_t currStmt        = 0;
    while (currStmt < stmtCount)
    {
        TIntermNode &stmtNode = *stmtList.getChildNode(currStmt);
        TIntermCase *caseNode = stmtNode.getAsCaseNode();
        if (caseNode)
        {
            if (inCaseList)
            {
                mSink << ", ";
            }
            else
            {
                emitIndentation();
                mSink << "case ";
                inCaseList = true;
            }
            caseNode->traverse(this);

            // Process the next statement.
            currStmt++;
        }
        else
        {
            // The current statement is not a case statement, end the current case list and emit all
            // the code until the next case statement. WGSL requires braces around the case
            // statement's code.
            ASSERT(inCaseList);
            inCaseList = false;
            mSink << ":\n";

            // Count the statements until the next case (or the end of the switch) and emit them as
            // a block. This assumes that the current statement list will never fallthrough to the
            // next case statement.
            size_t nextCaseStmt = currStmt + 1;
            for (;
                 nextCaseStmt < stmtCount && !stmtList.getChildNode(nextCaseStmt)->getAsCaseNode();
                 nextCaseStmt++)
            {
            }
            auto stmtListView = ANGLE_UNSAFE_TODO(angle::Span<TIntermNode *>(
                &stmtList.getSequence()->at(currStmt), nextCaseStmt - currStmt));
            emitBlock(stmtListView);
            mSink << "\n";

            // Skip to the next case statement.
            currStmt = nextCaseStmt;
        }
    }

    emitCloseBrace();

    return false;
}

bool OutputWGSLTraverser::visitCase(Visit, TIntermCase *caseNode)
{
    // "case" will have been emitted in the visitSwitch() override.

    if (caseNode->hasCondition())
    {
        TIntermTyped *condExpr = caseNode->getCondition();
        condExpr->traverse(this);
    }
    else
    {
        mSink << "default";
    }

    return false;
}

void OutputWGSLTraverser::emitFunctionReturn(const TFunction &func)
{
    const TType &returnType = func.getReturnType();
    if (returnType.getBasicType() == EbtVoid)
    {
        return;
    }
    mSink << " -> ";
    emitType(returnType);
}

void OutputWGSLTraverser::emitFunctionName(const TFunction &func)
{
    // As of Apr 2024, WGSL function overloads are "not coming soon"
    // (https://github.com/gpuweb/gpuweb/issues/876).
    // As of Sept 2025, WESL is working on overloads:
    // https://github.com/wgsl-tooling-wg/wesl-spec/issues/58.
    // So, append the symbol's ID to the overloaded functions..
    if (mOverloadedFunctions->contains(func.uniqueId()))
    {
        mSink << "ANGLEfunc" << func.uniqueId().get();
    }
    WriteNameOf(mSink, func, kUserVariableNamePrefix);
}

void OutputWGSLTraverser::emitFunctionSignature(const TFunction &func)
{
    mSink << "fn ";

    emitFunctionName(func);
    mSink << "(";

    bool emitComma          = false;
    const size_t paramCount = func.getParamCount();
    for (size_t i = 0; i < paramCount; ++i)
    {
        if (emitComma)
        {
            mSink << ", ";
        }
        emitComma = true;

        const TVariable &param = *func.getParam(i);
        emitFunctionParameter(func, param);
    }

    mSink << ")";

    emitFunctionReturn(func);
}

void OutputWGSLTraverser::emitFunctionParameter(const TFunction &func, const TVariable &param)
{
    // TODO(anglebug.com/42267100): function parameters are immutable and will need to be renamed if
    // they are mutated.
    EmitVariableDeclarationConfig evdConfig;
    evdConfig.isParameter = true;
    emitVariableDeclaration({param.symbolType(), param.name(), param.getType()}, evdConfig);
}

void OutputWGSLTraverser::visitFunctionPrototype(TIntermFunctionPrototype *funcProtoNode)
{
    // WGSL does not need function prototypes at all. Functions can be declared out of order.
}

bool OutputWGSLTraverser::visitFunctionDefinition(Visit, TIntermFunctionDefinition *funcDefNode)
{
    const TFunction &func = *funcDefNode->getFunction();
    TIntermBlock &body    = *funcDefNode->getBody();

    emitIndentation();
    emitFunctionSignature(func);
    mSink << "\n";
    body.traverse(this);

    return false;
}

void OutputWGSLTraverser::emitTextureBuiltin(const TOperator op, const TIntermSequence &args)
{

    ASSERT(BuiltInGroup::IsTexture(op));

    // The index in the GLSL function's argument list of each particular argument, e.g. bias.
    // `bias`, `lod`, `offset`, and `P` (the coordinates) are the common arguments to most texture
    // functions.
    size_t biasIndex   = 0;
    size_t lodIndex    = 0;
    size_t offsetIndex = 0;
    size_t pIndex      = 0;

    size_t dpdxIndex = 0;
    size_t dpdyIndex = 0;

    // TODO(anglebug.com/389145696): These are probably incorrect translations when sampling from
    // integer or unsigned integer samplers. Using texture() with a usampler
    // is similar to using texelFetch(), except wrap modes are respected. Possibly, the correct mip
    // levels are also selected.

    // The name of the equivalent texture function in WGSL.
    ImmutableString wgslFunctionName("");
    // GLSL stuffs 1, 2, or 3 arguments into a single vector. These represent the swizzles necessary
    // for extracting each argument from P to pass to the appropriate WGSL function.
    ImmutableString coordsSwizzle("");
    ImmutableString arrayIndexSwizzle("");
    ImmutableString depthRefSwizzle("");
    // For the projection forms of the texture builtins, the last coordinate will divide the other
    // three. This is just a swizzle for the last coordinate if the builtin call includes
    // projection.
    ImmutableString projectionDivisionSwizzle("");

    ImmutableString wgslTextureVarName("");
    ImmutableString wgslSamplerVarName("");

    constexpr std::string_view k2DCoordsSwizzle = ".xy";
    constexpr std::string_view k3DCoordsSwizzle = ".xyz";

    constexpr std::string_view kPossibleElems = "xyzw";

    // MonomorphizeUnsupportedFunctions() and RewriteStructSamplers() ensure that this is a
    // reference to the global sampler.
    TIntermSymbol *samplerNode = args[0]->getAsSymbolNode();
    // TODO(anglebug.com/389145696): this will fail if it's an array of samplers, which isn't yet
    // handled.
    if (!samplerNode)
    {
        UNIMPLEMENTED();
        mSink << "TODO_UNHANDLED_TEXTURE_FUNCTION()";
        return;
    }
    TBasicType samplerType = samplerNode->getType().getBasicType();
    ASSERT(IsSampler(samplerType));

    bool isProj = false;

    auto setWgslTextureVarName = [&]() {
        wgslTextureVarName =
            BuildConcatenatedImmutableString(kAngleTexturePrefix, samplerNode->getName());
    };

    auto setWgslSamplerVarName = [&]() {
        wgslSamplerVarName =
            BuildConcatenatedImmutableString(kAngleSamplerPrefix, samplerNode->getName());
    };

    auto setTextureSampleFunctionNameFromBias = [&]() {
        // TODO(anglebug.com/389145696): these are incorrect translations in vertex shaders, where
        // they should probably use textureLoad() (and textureDimensions()).
        if (IsShadowSampler(samplerType))
        {
            if (biasIndex != 0)
            {
                // TODO(anglebug.com/389145696): WGSL doesn't support using bias with shadow
                // samplers.
                UNIMPLEMENTED();
                wgslFunctionName = ImmutableString("TODO_CANNOT_USE_BIAS_WITH_SHADOW_SAMPLER");
            }
            else
            {
                wgslFunctionName = ImmutableString("textureSampleCompare");
            }
        }
        else
        {
            if (biasIndex == 0)
            {
                wgslFunctionName = ImmutableString("textureSample");
            }
            else
            {

                wgslFunctionName = ImmutableString("textureSampleBias");
            }
        }
    };

    switch (op)
    {
        case EOpTextureSize:
        {
            lodIndex = 1;
            ASSERT(args.size() == 2);

            wgslFunctionName = ImmutableString("textureDimensions");
            setWgslTextureVarName();
        }
        break;

        case EOpTexelFetchOffset:
        case EOpTexelFetch:
        {
            pIndex   = 1;
            lodIndex = 2;
            if (args.size() == 4)
            {
                offsetIndex = 3;
            }
            ASSERT(args.size() == 3 || args.size() == 4);

            wgslFunctionName = ImmutableString("textureLoad");
            setWgslTextureVarName();
        }
        break;

        // texture() use to be split into texture2D() and textureCube(). WGSL matches GLSL 3.0 and
        // combines them.
        case EOpTextureProj:
        case EOpTexture2DProj:
        case EOpTextureProjBias:
        case EOpTexture2DProjBias:
            isProj = true;
            [[fallthrough]];
        case EOpTexture:
        case EOpTexture2D:
        case EOpTextureCube:
        case EOpTextureBias:
        case EOpTexture2DBias:
        case EOpTextureCubeBias:
        {
            pIndex = 1;
            if (args.size() == 3)
            {
                biasIndex = 2;
            }
            ASSERT(args.size() == 2 || args.size() == 3);

            setTextureSampleFunctionNameFromBias();
            setWgslTextureVarName();
            setWgslSamplerVarName();
        }
        break;

        case EOpTextureProjLod:
        case EOpTextureProjLodOffset:
        case EOpTexture2DProjLodVS:
        case EOpTexture2DProjLodEXTFS:
            isProj = true;
            [[fallthrough]];
        case EOpTextureLod:
        case EOpTexture2DLodVS:
        case EOpTextureCubeLodVS:
        case EOpTexture2DLodEXTFS:
        case EOpTextureCubeLodEXTFS:
        case EOpTextureLodOffset:
        {
            pIndex   = 1;
            lodIndex = 2;
            if (args.size() == 4)
            {
                offsetIndex = 3;
            }
            ASSERT(args.size() == 3 || args.size() == 4);

            if (IsShadowSampler(samplerType))
            {
                // TODO(anglebug.com/389145696): WGSL may not support explicit LOD with shadow
                // samplers. textureSampleCompareLevel() only uses mip level 0.
                UNIMPLEMENTED();
                wgslFunctionName =
                    ImmutableString("TODO_CANNOT_USE_EXPLICIT_LOD_WITH_SHADOW_SAMPLER");
            }
            else
            {
                wgslFunctionName = ImmutableString("textureSampleLevel");
            }
            setWgslTextureVarName();
            setWgslSamplerVarName();
        }
        break;

        case EOpTextureProjOffset:
        case EOpTextureProjOffsetBias:
            isProj = true;
            [[fallthrough]];
        case EOpTextureOffset:
        case EOpTextureOffsetBias:
        {
            pIndex      = 1;
            offsetIndex = 2;
            if (args.size() == 4)
            {
                biasIndex = 3;
            }
            ASSERT(args.size() == 3 || args.size() == 4);

            setTextureSampleFunctionNameFromBias();
            setWgslTextureVarName();
            setWgslSamplerVarName();
        }
        break;

        case EOpTextureProjGrad:
        case EOpTextureProjGradOffset:
            isProj = true;
            [[fallthrough]];
        case EOpTextureGrad:
        case EOpTextureGradOffset:
        {
            pIndex    = 1;
            dpdxIndex = 2;
            dpdyIndex = 3;
            if (args.size() == 5)
            {
                offsetIndex = 4;
            }
            ASSERT(args.size() == 4 || args.size() == 5);

            if (IsShadowSampler(samplerType))
            {
                // TODO(anglebug.com/389145696): WGSL may not support explicit gradients with shadow
                // samplers.
                UNIMPLEMENTED();
                wgslFunctionName =
                    ImmutableString("TODO_CANNOT_USE_EXPLICIT_GRAD_WITH_SHADOW_SAMPLER");
            }
            else
            {
                wgslFunctionName = ImmutableString("textureSampleGrad");
            }
            setWgslTextureVarName();
            setWgslSamplerVarName();
        }
        break;

        default:
            UNIMPLEMENTED();
            mSink << "TODO_UNHANDLED_TEXTURE_FUNCTION()";
            return;
    }

    mSink << wgslFunctionName << "(";

    ASSERT(!wgslTextureVarName.empty());
    mSink << wgslTextureVarName;

    if (!wgslSamplerVarName.empty())
    {
        mSink << ", " << wgslSamplerVarName;

        // If using a projection division, set the swizzle that extracts the last argument from the
        // p vector.
        if (isProj)
        {
            ASSERT(pIndex == 1);
            const uint8_t vecSize = args[pIndex]->getAsTyped()->getNominalSize();
            ASSERT(vecSize == 3 || vecSize == 4);
            projectionDivisionSwizzle =
                BuildConcatenatedImmutableString('.', kPossibleElems[vecSize - 1]);
        }

        // If sampling from an array, set the swizzle that extracts the array layer number from the
        // p vector.
        if (IsSampler2DArray(samplerType))
        {
            arrayIndexSwizzle = ImmutableString(".z");
        }

        // If sampling from a shadow samplers, set the swizzle that extracts the D_ref argument from
        // the p vector.
        if (IsShadowSampler(samplerType))
        {
            size_t elemIndex = 0;
            if (IsSampler2D(samplerType))
            {
                elemIndex = 2;
            }
            else if (IsSampler2DArray(samplerType) || IsSampler3D(samplerType) ||
                     IsSamplerCube(samplerType))
            {
                elemIndex = 3;
            }

            depthRefSwizzle = BuildConcatenatedImmutableString('.', kPossibleElems[elemIndex]);
        }

        // Finally, set the swizzle for extracting coordinates from the p vector.
        if (IsSampler2D(samplerType) || IsSampler2DArray(samplerType))
        {
            coordsSwizzle = ImmutableString(k2DCoordsSwizzle.data(), k2DCoordsSwizzle.size());
        }
        else if (IsSampler3D(samplerType) || IsSamplerCube(samplerType))
        {
            coordsSwizzle = ImmutableString(k3DCoordsSwizzle.data(), k3DCoordsSwizzle.size());
        }
    }

    // TODO(anglebug.com/389145696): traversing the pArg multiple times is an error if it ever
    // contains side effects (e.g. a function call). There is also a problem if this traverses
    // function arguments in a different order, arguments with side effects that effect arguments
    // that come later may be reordered incorrectly. ESSL specs defined function argument evaluation
    // as left-to-right.
    auto traversePArg = [&]() {
        mSink << "(";
        ASSERT(pIndex != 0);
        args[pIndex]->traverse(this);
        mSink << ")";
    };

    auto outputProjectionDivisionIfNecessary = [&]() {
        if (projectionDivisionSwizzle.empty())
        {
            return;
        }
        mSink << " / ";
        traversePArg();
        mSink << projectionDivisionSwizzle;
    };

    // The arguments to the WGSL function always appear in a certain (partial) order, so output them
    // in that order.
    //
    // The order is always
    // - texture
    // - sampler
    // - coordinates
    // - array layer index
    // - depth_ref, bias, explicit level of detail (never appear together)
    // - dfdx
    // - dfdy
    // - offset
    //
    // See the texture builtin functions in the WGSL spec:
    // https://www.w3.org/TR/WGSL/#texture-builtin-functions
    //
    // For example
    // @must_use fn textureSampleLevel(t: texture_2d_array<f32>,
    //                             s: sampler,
    //                             coords: vec2<f32>,
    //                             array_index: A,
    //                             level: f32,
    //                             offset: vec2<i32>) -> vec4<f32>

    if (pIndex != 0)
    {
        mSink << ", ";
        traversePArg();
        mSink << coordsSwizzle;
        outputProjectionDivisionIfNecessary();
    }

    if (!arrayIndexSwizzle.empty())
    {
        mSink << ", i32(";
        traversePArg();
        mSink << arrayIndexSwizzle << ")";
    }

    if (!depthRefSwizzle.empty())
    {
        mSink << ", ";
        traversePArg();
        mSink << depthRefSwizzle;
        outputProjectionDivisionIfNecessary();
    }

    if (biasIndex != 0)
    {
        mSink << ", ";
        args[biasIndex]->traverse(this);
    }

    if (lodIndex != 0)
    {
        mSink << ", ";
        args[lodIndex]->traverse(this);
    }

    if (dpdxIndex != 0)
    {
        mSink << ", ";
        args[dpdxIndex]->traverse(this);
    }

    if (dpdyIndex != 0)
    {
        mSink << ", ";
        args[dpdyIndex]->traverse(this);
    }

    if (offsetIndex != 0)
    {
        mSink << ", ";
        // Both GLSL and WGSL require this to be a const expression.
        args[offsetIndex]->traverse(this);
    }

    mSink << ")";
}

bool OutputWGSLTraverser::visitAggregate(Visit, TIntermAggregate *aggregateNode)
{
    const TIntermSequence &args = *aggregateNode->getSequence();
    const TFunction *callee     = aggregateNode->getFunction();  // Can be nullptr

    auto emitArgList = [&]() {
        mSink << "(";

        bool emitComma = false;
        for (size_t i = 0; i < args.size(); i++)
        {
            if (emitComma)
            {
                mSink << ", ";
            }
            emitComma = true;

            // If outparams, must pass a pointer.
            if (callee && IsParamOut(callee->getParam(i)->getType().getQualifier()))
            {
                mSink << "&";
            }
            args[i]->traverse(this);
        }

        mSink << ")";
    };

    const TType &retType = aggregateNode->getType();

    if (aggregateNode->isConstructor())
    {

        emitType(retType);
        emitArgList();

        return false;
    }
    else
    {
        const TOperator op = aggregateNode->getOp();
        switch (op)
        {
            case TOperator::EOpCallFunctionInAST:
                emitFunctionName(*callee);
                emitArgList();
                return false;

            default:
                // There is one raw call currently and that is used to get a pointer to an l-value.
                if (op == EOpCallInternalRawFunction)
                {
                    ASSERT(callee->name() == "ANGLE_takePointer");
                    ASSERT(args.size() == 1);
                    mSink << "&";
                    groupedTraverse(*aggregateNode->getChildNode(0));
                    return false;
                }

                auto getArgType = [&](size_t index) -> const TType * {
                    if (index < args.size())
                    {
                        TIntermTyped *arg = args[index]->getAsTyped();
                        ASSERT(arg);
                        return &arg->getType();
                    }
                    return nullptr;
                };

                const TType *argType0 = getArgType(0);
                const TType *argType1 = getArgType(1);
                const TType *argType2 = getArgType(2);

                const OperatorInfo opInfo =
                    useOperatorAndGetInfo(aggregateNode, op, retType, argType0, argType1, argType2);

                if (opInfo.wgslWrapperFn)
                {
                    mSink << opInfo.wgslWrapperFn->prefix;
                }

                if (opInfo.IsSymbolicOperator())
                {
                    switch (args.size())
                    {
                        case 1:
                        {
                            TIntermNode &operandNode = *aggregateNode->getChildNode(0);
                            mSink << opInfo.opName;
                            groupedTraverse(operandNode);
                        }
                        break;

                        case 2:
                        {
                            // symbolic operators with 2 args are emitted with infix notation.
                            TIntermNode &leftNode  = *aggregateNode->getChildNode(0);
                            TIntermNode &rightNode = *aggregateNode->getChildNode(1);
                            groupedTraverse(leftNode);
                            mSink << " " << opInfo.opName << " ";
                            groupedTraverse(rightNode);
                        }
                        break;

                        default:
                            UNREACHABLE();
                            return false;
                    }
                }
                else
                {
                    // Rewrite the calls to sampler functions.
                    if (BuiltInGroup::IsTexture(op))
                    {
                        emitTextureBuiltin(op, args);
                        ASSERT(!opInfo.wgslWrapperFn.has_value());
                        return false;
                    }
                    // If the operator is not symbolic then it is a builtin that uses function call
                    // syntax: builtin(arg1, arg2, ..);
                    mSink << (opInfo.opName == nullptr ? "TODO_Operator" : opInfo.opName);
                    emitArgList();
                }

                if (opInfo.wgslWrapperFn)
                {
                    mSink << opInfo.wgslWrapperFn->suffix;
                }

                return false;
        }
    }
}

bool OutputWGSLTraverser::emitBlock(angle::Span<TIntermNode *> nodes)
{
    ASSERT(mIndentLevel >= -1);
    const bool isGlobalScope = mIndentLevel == -1;

    if (isGlobalScope)
    {
        ++mIndentLevel;
    }
    else
    {
        emitOpenBrace();
    }

    TIntermNode *prevStmtNode = nullptr;

    const size_t stmtCount = nodes.size();
    for (size_t i = 0; i < stmtCount; ++i)
    {
        TIntermNode &stmtNode = *nodes[i];

        if (isGlobalScope && prevStmtNode && (NewlinePad(*prevStmtNode) || NewlinePad(stmtNode)))
        {
            mSink << "\n";
        }
        const bool isCase = stmtNode.getAsCaseNode();
        mIndentLevel -= isCase;
        emitIndentation();
        mIndentLevel += isCase;
        stmtNode.traverse(this);
        if (RequiresSemicolonTerminator(stmtNode))
        {
            mSink << ";";
        }
        mSink << "\n";

        prevStmtNode = &stmtNode;
    }

    if (isGlobalScope)
    {
        ASSERT(mIndentLevel == 0);
        --mIndentLevel;
    }
    else
    {
        emitCloseBrace();
    }

    return false;
}

bool OutputWGSLTraverser::visitBlock(Visit, TIntermBlock *blockNode)
{
    return emitBlock(ANGLE_UNSAFE_TODO(
        angle::Span(blockNode->getSequence()->data(), blockNode->getSequence()->size())));
}

bool OutputWGSLTraverser::visitGlobalQualifierDeclaration(Visit,
                                                          TIntermGlobalQualifierDeclaration *)
{
    return false;
}

void OutputWGSLTraverser::emitStructDeclaration(const TType &type)
{
    ASSERT(type.getBasicType() == TBasicType::EbtStruct && type.isStructSpecifier());

    mSink << "struct ";
    emitBareTypeName(type);

    mSink << "\n";
    emitOpenBrace();

    const TStructure &structure = *type.getStruct();
    bool isInUniformAddressSpace =
        mUniformBlockMetadata->structsInUniformAddressSpace.count(structure.uniqueId().get()) != 0;

    bool alignTo16InUniformAddressSpace = true;
    for (const TField *field : structure.fields())
    {
        const TType *fieldType = field->type();

        emitIndentation();
        // If this struct is used in the uniform address space, it must obey the uniform address
        // space's layout constaints (https://www.w3.org/TR/WGSL/#address-space-layout-constraints).
        // WGSL's address space layout constraints nearly match std140, and the places they don't
        // are handled elsewhere.
        if (isInUniformAddressSpace)
        {
            // Here, the field must be aligned to 16 if:
            // 1. The field is a struct or array (note that matCx2 is represented as an array of
            // vec2)
            // 2. The previous field is a struct
            // 3. The field is the first in the struct (for convenience).
            if (field->type()->getStruct() || fieldType->isArray() || IsMatCx2(fieldType))
            {
                alignTo16InUniformAddressSpace = true;
            }
            if (alignTo16InUniformAddressSpace)
            {
                mSink << "@align(16) ";
            }

            // If this field is a struct, the next member should be aligned to 16.
            alignTo16InUniformAddressSpace = fieldType->getStruct();

            // If the field is an array whose stride is not aligned to 16, the element type must be
            // emitted with a wrapper struct. Record that the wrapper struct needs to be emitted.
            // Note that if the array element type is already of struct type, it doesn't need
            // another wrapper struct, it will automatically be aligned to 16 because its first
            // member is aligned to 16 (implemented above).
            if (ElementTypeNeedsUniformWrapperStruct(/*inUniformAddressSpace=*/true, fieldType))
            {
                TType innerType = *fieldType;
                innerType.toArrayElementType();
                // Multidimensional arrays not currently supported in uniforms in the WebGPU backend
                ASSERT(!innerType.isArray());
                mWGSLGenerationMetadataForUniforms->arrayElementTypesInUniforms.insert(innerType);
            }
        }

        // TODO(anglebug.com/42267100): emit qualifiers.
        EmitVariableDeclarationConfig evdConfig;
        evdConfig.typeConfig.addressSpace =
            isInUniformAddressSpace ? WgslAddressSpace::Uniform : WgslAddressSpace::NonUniform;
        emitVariableDeclaration({field->symbolType(), field->name(), *fieldType}, evdConfig);
        mSink << ",\n";
    }

    emitCloseBrace();
}

void OutputWGSLTraverser::emitVariableDeclaration(const VarDecl &decl,
                                                  const EmitVariableDeclarationConfig &evdConfig)
{
    const TBasicType basicType = decl.type.getBasicType();

    if ((decl.type.getQualifier() == EvqUniform || decl.type.getQualifier() == EvqBuffer) &&
        evdConfig.isGlobalScope)
    {
        // Uniforms/interface blocks are declared in a pre-pass, and don't need to be outputted
        // here.
        return;
    }

    if (basicType == TBasicType::EbtStruct && decl.type.isStructSpecifier())
    {
        // TODO(anglebug.com/42267100): in WGSL structs probably can't be declared in
        // function parameters or in uniform declarations or in variable declarations, or
        // anonymously either within other structs or within a variable declaration. Handle
        // these with the same AST pre-passes as other shader translators.
        ASSERT(!evdConfig.isParameter);
        emitStructDeclaration(decl.type);
        if (decl.symbolType != SymbolType::Empty)
        {
            mSink << " ";
            emitNameOf(decl);
        }
        return;
    }

    ASSERT(basicType == TBasicType::EbtStruct || decl.symbolType != SymbolType::Empty ||
           evdConfig.isParameter);

    if (evdConfig.isDeclaration)
    {
        // Pointers in WGSL must be declared with let.
        if (evdConfig.emitAsPointer)
        {
            mSink << "let";
        }
        else if (decl.type.getQualifier() == EvqConst)
        {
            mSink << "const";
        }
        else
        {
            mSink << "var";
            if (evdConfig.isGlobalScope)
            {
                if (decl.type.getQualifier() == EvqUniform)
                {
                    ASSERT(IsOpaqueType(decl.type.getBasicType()));
                    mSink << "<uniform>";
                }
                else
                {
                    mSink << "<private>";
                }
            }
        }
        mSink << " ";
    }
    else
    {
        ASSERT(!evdConfig.isGlobalScope);
    }

    if (decl.symbolType != SymbolType::Empty)
    {
        emitNameOf(decl);
    }
    mSink << " : ";

    bool isOutParam = evdConfig.isParameter && IsParamOut(decl.type.getQualifier());
    if (isOutParam)
    {
        // Arguments to outparams will always be function-local due to AST pre-passes.
        mSink << "ptr<function, ";
    }
    else if (evdConfig.emitAsPointer)
    {
        ASSERT(!evdConfig.isParameter);
        mSink << "ptr<" << StringForWgslPointerAddressSpace(evdConfig.emitAsPointer.value())
              << ", ";
    }

    WriteWgslType(mSink, decl.type, evdConfig.typeConfig);

    if (isOutParam || evdConfig.emitAsPointer)
    {
        mSink << ">";
    }
}

bool OutputWGSLTraverser::visitDeclaration(Visit, TIntermDeclaration *declNode)
{
    ASSERT(declNode->getChildCount() == 1);
    TIntermNode &node = *declNode->getChildNode(0);

    EmitVariableDeclarationConfig evdConfig;
    evdConfig.isDeclaration = true;
    evdConfig.isGlobalScope = mIndentLevel == 0;

    if (TIntermSymbol *symbolNode = node.getAsSymbolNode())
    {
        const TVariable &var = symbolNode->variable();
        if (mRewritePipelineVarOutput->IsInputVar(var.uniqueId()) ||
            mRewritePipelineVarOutput->IsOutputVar(var.uniqueId()))
        {
            // Some variables, like shader inputs/outputs/builtins, are declared in the WGSL source
            // outside of the traverser.
            return false;
        }
        emitVariableDeclaration({var.symbolType(), var.name(), var.getType()}, evdConfig);
    }
    else if (TIntermBinary *initNode = node.getAsBinaryNode())
    {
        ASSERT(initNode->getOp() == TOperator::EOpInitialize);
        TIntermSymbol *leftSymbolNode = initNode->getLeft()->getAsSymbolNode();
        TIntermTyped *valueNode       = initNode->getRight()->getAsTyped();
        ASSERT(leftSymbolNode && valueNode);

        const TVariable &var = leftSymbolNode->variable();
        if (mRewritePipelineVarOutput->IsInputVar(var.uniqueId()) ||
            mRewritePipelineVarOutput->IsOutputVar(var.uniqueId()))
        {
            // Some variables, like shader inputs/outputs/builtins, are declared in the WGSL source
            // outside of the traverser.
            return false;
        }

        if (valueNode->getAsAggregate() && valueNode->getAsAggregate()->getFunction())
        {
            const TFunction *func = valueNode->getAsAggregate()->getFunction();
            if (valueNode->getAsAggregate()->getOp() == EOpCallInternalRawFunction &&
                func->name() == "ANGLE_takePointer")
            {
                mIsActuallyOfPointerType.insert(leftSymbolNode->uniqueId());

                evdConfig.emitAsPointer = GetWgslAddressSpaceForPointer(
                    FindRootVariable(valueNode->getAsAggregate()->getChildNode(0))->getType());
            }
        }

        emitVariableDeclaration({var.symbolType(), var.name(), var.getType()}, evdConfig);
        mSink << " = ";
        groupedTraverse(*valueNode);
    }
    else
    {
        UNREACHABLE();
    }

    return false;
}

bool OutputWGSLTraverser::visitLoop(Visit, TIntermLoop *loopNode)
{
    const TLoopType loopType = loopNode->getType();

    switch (loopType)
    {
        case TLoopType::ELoopFor:
            return emitForLoop(loopNode);
        case TLoopType::ELoopWhile:
            return emitWhileLoop(loopNode);
        case TLoopType::ELoopDoWhile:
            return emulateDoWhileLoop(loopNode);
    }
}

bool OutputWGSLTraverser::emitForLoop(TIntermLoop *loopNode)
{
    ASSERT(loopNode->getType() == TLoopType::ELoopFor);

    TIntermNode *initNode  = loopNode->getInit();
    TIntermTyped *condNode = loopNode->getCondition();
    TIntermTyped *exprNode = loopNode->getExpression();

    mSink << "for (";

    if (initNode)
    {
        initNode->traverse(this);
    }
    else
    {
        mSink << " ";
    }

    mSink << "; ";

    if (condNode)
    {
        condNode->traverse(this);
    }

    mSink << "; ";

    if (exprNode)
    {
        exprNode->traverse(this);
    }

    mSink << ")\n";

    loopNode->getBody()->traverse(this);

    return false;
}

bool OutputWGSLTraverser::emitWhileLoop(TIntermLoop *loopNode)
{
    ASSERT(loopNode->getType() == TLoopType::ELoopWhile);

    TIntermNode *initNode  = loopNode->getInit();
    TIntermTyped *condNode = loopNode->getCondition();
    TIntermTyped *exprNode = loopNode->getExpression();
    ASSERT(condNode);
    ASSERT(!initNode && !exprNode);

    emitIndentation();
    mSink << "while (";
    condNode->traverse(this);
    mSink << ")\n";
    loopNode->getBody()->traverse(this);

    return false;
}

bool OutputWGSLTraverser::emulateDoWhileLoop(TIntermLoop *loopNode)
{
    ASSERT(loopNode->getType() == TLoopType::ELoopDoWhile);

    // Emulate do-while with an infinite loop and a WGSL-special "continuing" and "break-if"
    // statement.
    //
    // Example GLSL:
    // do {
    //    // Loop body, which might contain 'continue'
    // } while(condition)
    //
    // Becomes WGSL:
    // loop {
    //   // Loop body, which might contain 'continue'

    //   continuing {
    //     break if !condition;
    //   }
    // }

    TIntermNode *initNode  = loopNode->getInit();
    TIntermTyped *condNode = loopNode->getCondition();
    TIntermTyped *exprNode = loopNode->getExpression();
    ASSERT(condNode);
    ASSERT(!initNode && !exprNode);

    emitIndentation();
    // Write an infinite loop.
    mSink << "loop {\n";
    mIndentLevel++;
    // The loop body may contain a "continue" branch.
    loopNode->getBody()->traverse(this);
    mSink << "\n";
    emitIndentation();
    // At the end of the loop, break if the loop condition dos not still hold.
    mSink << "continuing {\n";
    mIndentLevel++;
    emitIndentation();
    mSink << "break if !(";
    condNode->traverse(this);
    mSink << ");\n";
    mIndentLevel--;
    emitIndentation();
    mSink << "}\n";
    mIndentLevel--;
    emitIndentation();
    mSink << "}";

    return false;
}

bool OutputWGSLTraverser::visitBranch(Visit, TIntermBranch *branchNode)
{
    const TOperator flowOp = branchNode->getFlowOp();
    TIntermTyped *exprNode = branchNode->getExpression();

    emitIndentation();

    switch (flowOp)
    {
        case TOperator::EOpKill:
        {
            ASSERT(exprNode == nullptr);
            mSink << "discard";
        }
        break;

        case TOperator::EOpReturn:
        {
            mSink << "return";
            if (exprNode)
            {
                mSink << " ";
                exprNode->traverse(this);
            }
        }
        break;

        case TOperator::EOpBreak:
        {
            ASSERT(exprNode == nullptr);
            mSink << "break";
        }
        break;

        case TOperator::EOpContinue:
        {
            ASSERT(exprNode == nullptr);
            mSink << "continue";
        }
        break;

        default:
        {
            UNREACHABLE();
        }
    }

    return false;
}

void OutputWGSLTraverser::visitPreprocessorDirective(TIntermPreprocessorDirective *node)
{
    // No preprocessor directives expected at this point.
    UNREACHABLE();
}

void OutputWGSLTraverser::emitBareTypeName(const TType &type)
{
    WriteWgslBareTypeName(mSink, type, {});
}

void OutputWGSLTraverser::emitType(const TType &type)
{
    WriteWgslType(mSink, type, {});
}

// Unlike Vulkan having auto viewport flipping extension, in WGPU we have to flip gl_Position.y
// manually.
// This operation performs flipping the gl_Position.y using this expression:
// gl_Position.y = gl_Position.y * negViewportScaleY
[[nodiscard]] bool AppendVertexShaderPositionYCorrectionToMain(TCompiler *compiler,
                                                               TIntermBlock *root,
                                                               TSymbolTable *symbolTable,
                                                               TIntermTyped *negFlipY)
{
    // Create a symbol reference to "gl_Position"
    const TVariable *position  = BuiltInVariable::gl_Position();
    TIntermSymbol *positionRef = new TIntermSymbol(position);

    // Create a swizzle to "gl_Position.y"
    TVector<uint32_t> swizzleOffsetY;
    swizzleOffsetY.push_back(1);
    TIntermSwizzle *positionY = new TIntermSwizzle(positionRef, swizzleOffsetY);

    // Create the expression "gl_Position.y * negFlipY"
    TIntermBinary *inverseY = new TIntermBinary(EOpMul, positionY->deepCopy(), negFlipY);

    // Create the assignment "gl_Position.y = gl_Position.y * negViewportScaleY
    TIntermTyped *positionYLHS = positionY->deepCopy();
    TIntermBinary *assignment  = new TIntermBinary(TOperator::EOpAssign, positionYLHS, inverseY);

    // Append the assignment as a statement at the end of the shader.
    return RunAtTheEndOfShader(compiler, root, assignment, symbolTable);
}

// This operation performs the viewport depth translation needed by WGPU. GL uses a
// clip space z range of -1 to +1 where as WGPU uses 0 to 1. The translation becomes
// this expression
//
//     z_wgpu = 0.5 * (w_gl + z_gl)
//
// where z_wgpu is the depth output of a WGPU vertex shader and z_gl is the same for GL.
bool AppendVertexShaderDepthCorrectionToMain(TCompiler *compiler,
                                             TIntermBlock *root,
                                             const DriverUniform *driverUniforms)
{
    const TVariable *position  = BuiltInVariable::gl_Position();
    TIntermSymbol *positionRef = new TIntermSymbol(position);

    TVector<uint32_t> swizzleOffsetZ = {2};
    TIntermSwizzle *positionZ        = new TIntermSwizzle(positionRef, swizzleOffsetZ);

    TIntermConstantUnion *oneHalf = CreateFloatNode(0.5f, EbpMedium);

    TVector<uint32_t> swizzleOffsetW = {3};
    TIntermSwizzle *positionW        = new TIntermSwizzle(positionRef->deepCopy(), swizzleOffsetW);

    // Create the expression "(gl_Position.z + gl_Position.w) * 0.5".
    TIntermBinary *zPlusW = new TIntermBinary(EOpAdd, positionZ->deepCopy(), positionW->deepCopy());
    TIntermBinary *halfZPlusW = new TIntermBinary(EOpMul, zPlusW, oneHalf->deepCopy());

    // Create the assignment "gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5"
    TIntermTyped *positionZLHS = positionZ->deepCopy();
    TIntermBinary *assignment  = new TIntermBinary(TOperator::EOpAssign, positionZLHS, halfZPlusW);

    // Apply depth correction if needed
    TIntermBlock *block = new TIntermBlock;
    block->appendStatement(assignment);
    TIntermIfElse *ifCall = new TIntermIfElse(driverUniforms->getTransformDepth(), block, nullptr);

    // Append the assignment as a statement at the end of the shader.
    return RunAtTheEndOfShader(compiler, root, ifCall, &compiler->getSymbolTable());
}

}  // namespace

TranslatorWGSL::TranslatorWGSL(sh::GLenum type, ShShaderSpec spec, ShShaderOutput output)
    : TCompiler(type, spec, output)
{}

bool TranslatorWGSL::preTranslateTreeModifications(TIntermBlock *root,
                                                   const ShCompileOptions &compileOptions,
                                                   const TVariable **defaultUniformBlockOut)
{
    if (!PullExpressionsIntoFunctions(this, root))
    {
        return false;
    }

    if (!EmulateMutableFunctionParams(this, root))
    {
        return false;
    }

    if (!RewriteMixedTypeMathExprs(this, root))
    {
        return false;
    }

    if (!RewriteMultielementSwizzleAssignment(this, root))
    {
        return false;
    }

    int aggregateTypesUsedForUniforms = 0;
    for (const auto &uniform : getUniforms())
    {
        if (uniform.isStruct() || uniform.isArrayOfArrays())
        {
            ++aggregateTypesUsedForUniforms;
        }
    }

    // TODO(anglebug.com/42267100): just use the struct mode to avoid a rewrite of the interface
    // block by ReduceInterfaceBlocks into a struct.
    DriverUniform driverUniforms(DriverUniformMode::InterfaceBlock, SH_WGSL_OUTPUT);
    ASSERT(getShaderType() != GL_COMPUTE_SHADER);
    if (compileOptions.useIR)
    {
        driverUniforms.findDeclarationAddedByIR(root);
    }
    else
    {
        driverUniforms.addGraphicsDriverUniformsToShader(root, &getSymbolTable());
    }

    if (getShaderType() == GL_VERTEX_SHADER)
    {
        TIntermTyped *flipNegY =
            driverUniforms.getFlipXY(&getSymbolTable(), DriverUniformFlip::PreFragment);
        flipNegY = (new TIntermSwizzle(flipNegY, {1}))->fold(nullptr);

        if (!AppendVertexShaderPositionYCorrectionToMain(this, root, &getSymbolTable(), flipNegY))
        {
            return false;
        }

        if (!AppendVertexShaderDepthCorrectionToMain(this, root, &driverUniforms))
        {
            return false;
        }
    }

    // Samplers are legal as function parameters, but samplers within structs or arrays are not
    // allowed in WGSL
    // (https://www.w3.org/TR/WGSL/#function-call-expr:~:text=A%20function%20parameter,a%20sampler%20type).
    // TODO(anglebug.com/389145696): handle arrays of samplers here.

    // If there are any function calls that take array-of-array of opaque uniform parameters, or
    // other opaque uniforms that need special handling in WebGPU, monomorphize the functions by
    // removing said parameters and replacing them in the function body with the call arguments.
    //
    // This  dramatically simplifies future transformations w.r.t to samplers in structs, array of
    //   arrays of opaque types, atomic counters etc.
    if (!compileOptions.useIR)
    {
        UnsupportedFunctionArgsBitSet args{UnsupportedFunctionArgs::StructContainingSamplers,
                                           UnsupportedFunctionArgs::ArrayOfArrayOfSamplerOrImage,
                                           UnsupportedFunctionArgs::AtomicCounter,
                                           UnsupportedFunctionArgs::Image};
        if (!MonomorphizeUnsupportedFunctions(this, root, &getSymbolTable(), args))
        {
            return false;
        }

        if (aggregateTypesUsedForUniforms > 0)
        {
            // Requires MonomorphizeUnsupportedFunctions() to have been run already.
            if (!RewriteStructSamplers(this, root, &getSymbolTable()))
            {
                return false;
            }
        }
    }
    else
    {
        // GatherDefaultUniforms below is relying on the sorting of functions and declarations that
        // was otherwise done in MonomorphizeUnsupportedFunctions.  This can be removed once more is
        // ported to IR and no transformation above is inserting a function in the middle of
        // declarations.
        TIntermSequence *original = root->getSequence();

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

        // Append function definitions to |replacement|.
        replacement.insert(replacement.end(), functionDefs.begin(), functionDefs.end());

        // Replace root's sequence with |replacement|.
        root->replaceAllChildren(std::move(replacement));
    }

    // Replace array of array of opaque uniforms with a flattened array.  This is run after
    // MonomorphizeUnsupportedFunctions and RewriteStructSamplers so that it's not possible for an
    // array of array of opaque type to be partially subscripted and passed to a function.
    // TODO(anglebug.com/389145696): Even single-level arrays of samplers are not allowed in WGSL.
    if (!RewriteArrayOfArrayOfOpaqueUniforms(this, root, &getSymbolTable()))
    {
        return false;
    }

    // RewriteStructSamplers should have already run at this point so there are not default
    // uniforms containing samplers, even within a nested struct.
    gl::ShaderType packedShaderType = gl::FromGLenum<gl::ShaderType>(getShaderType());
    if (!GatherDefaultUniforms(this, root, &getSymbolTable(), packedShaderType,
                               ImmutableString(kDefaultUniformBlockVarType),
                               ImmutableString(kDefaultUniformBlockVarName),
                               defaultUniformBlockOut))
    {
        return false;
    }

    // Note: It would be possible to to avoid running this AST modification by outputting
    // interface blocks like structs, with the wrinkle that interface blocks don't need an instance
    // variable name and so this translator would have to generate a new one and keep a map of
    // TInterfaceBlock -> WGSLName in order to output field accesses of the interface block.
    int uniqueStructId = 0;
    if (!ReduceInterfaceBlocks(*this, *root, [&uniqueStructId]() -> ImmutableString {
            return BuildConcatenatedImmutableString("ANGLE_unnamed_interface_block_",
                                                    uniqueStructId++);
        }))
    {
        return false;
    }

    return true;
}

bool TranslatorWGSL::translate(TIntermBlock *root,
                               const ShCompileOptions &compileOptions,
                               PerformanceDiagnostics *perfDiagnostics)
{
    // TODO(https://issues.angleproject.org/issues/42264589#comment3): remove this, as it is
    // deprecated.
    mValidateASTOptions.validateNoRawFunctionCalls = false;

    if (kOutputTreeBeforeTranslation)
    {
        TInfoSinkBase treeOut;
        std::cout << "Initial tree for shader type "
                  << (getShaderType() == GL_VERTEX_SHADER     ? "vertex shader"
                      : getShaderType() == GL_FRAGMENT_SHADER ? "fragment shader "
                                                              : "unknown")
                  << std::endl;
        OutputTree(root, treeOut);
        std::cout << treeOut.c_str();
    }

    const TVariable *defaultUniformBlock = nullptr;

    if (!preTranslateTreeModifications(root, compileOptions, &defaultUniformBlock))
    {
        return false;
    }

    if (kOutputTreeBeforeTranslation)
    {
        TInfoSinkBase treeOut;
        std::cout << "After preTranslateTreeModifications(): " << std::endl;
        getInfoSink().info.erase();
        OutputTree(root, treeOut);
        std::cout << treeOut.c_str();
    }
    enableValidateNoMoreTransformations();

    RewritePipelineVarOutput rewritePipelineVarOutput(getShaderType());
    WGSLGenerationMetadataForUniforms wgslGenerationMetadataForUniforms;

    // WGSL's main() will need to take parameters or return values if any glsl (input/output)
    // builtin variables are used.
    if (!GenerateMainFunctionAndIOStructs(*this, *root, rewritePipelineVarOutput))
    {
        ANGLE_LOG(ERR) << "Failed to generate WGSL main functions";
        return false;
    }

    TInfoSinkBase &sink = getInfoSink().obj;

    // GLSL allows derivatives to be calculated as long as control flow is dynamically uniform. WGSL
    // triggers a derivative_uniformity diagnostic whenever it cannot statically determine that
    // control flow is uniform, which is by default an error. Since this compiler must implement
    // GLSL semantics, use a global diagnostic filter to turn derivative_uniformity diagnostics into
    // warnings instead of the default error.
    // See https://github.com/gpuweb/gpuweb/issues/3479 and the spec:
    // https://www.w3.org/TR/WGSL/#uniformity
    sink << "diagnostic(warning,derivative_uniformity);\n";

    UniformBlockMetadata uniformBlockMetadata;
    if (!RecordUniformBlockMetadata(root, uniformBlockMetadata))
    {
        ANGLE_LOG(ERR) << "Failed to record uniform block metadata";
        return false;
    }

    TUnorderedSet<TSymbolUniqueId> overloadedFunctions = FindOverloadedFunctions(root);
    WGSLProgramPrelude prelude;

    // Generate the body of the WGSL including the GLSL main() function.
    TInfoSinkBase traverserOutput;
    OutputWGSLTraverser traverser(&traverserOutput, &rewritePipelineVarOutput,
                                  &uniformBlockMetadata, &wgslGenerationMetadataForUniforms,
                                  &overloadedFunctions, &prelude);
    root->traverse(&traverser);

    // The makeup of the prelude is determined by the traverser, and then must be outputted near the
    // top of the program.
    prelude.outputPrelude(sink);

    // Start writing the output structs that will be referred to by the `traverser`'s output.'
    if (!rewritePipelineVarOutput.OutputStructs(sink))
    {
        ANGLE_LOG(ERR) << "Failed to output pipeline structs";
        return false;
    }

    if (!OutputUniformBlocksAndSamplers(this, root, defaultUniformBlock))
    {
        ANGLE_LOG(ERR) << "Failed to output uniform blocks and samplers";
        return false;
    }

    sink << "\n";
    OutputUniformWrapperStructsAndConversions(sink, wgslGenerationMetadataForUniforms);

    // The traverser output needs to be in the code after uniform wrapper structs are emitted above,
    // since the traverser code references the wrapper struct types.
    sink << traverserOutput.str();

    // Write the actual WGSL main function, wgslMain(), which calls the GLSL main function.
    if (!rewritePipelineVarOutput.OutputMainFunction(sink))
    {
        ANGLE_LOG(ERR) << "Failed to output WGSL main function";
        return false;
    }

    if (kOutputTranslatedShader)
    {
        std::cout << sink.str();
    }

    return true;
}

bool TranslatorWGSL::shouldFlattenPragmaStdglInvariantAll()
{
    // Not neccesary for WGSL transformation.
    return false;
}
}  // namespace sh
