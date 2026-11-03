//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

// Note: During transition to IR, ParseContext.cpp builds the AST and IR at the same time, which is
// not efficient.  The AST is used for validation purposes, but a stack that only contains the
// necessary information needed for validation is sufficient, so for example operations such as
// constant folding etc don't need to be performed.  Such a stack could be very light, including IR
// ids that could then be used to query information out of the ir itself.
//
// This is best done when an AST-only build is no longer possible so that there wouldn't need to be
// a fallback to AST maintained at the same time.

#include "compiler/translator/ParseContext.h"
#include "common/unsafe_buffers.h"
#include "compiler/translator/Compiler.h"

#include <anglebase/sha1.h>
#include <stdarg.h>
#include <stdio.h>

#include "common/angleutils.h"
#include "common/mathutil.h"
#include "common/utilities.h"
#include "compiler/preprocessor/SourceLocation.h"
#include "compiler/translator/Declarator.h"
#include "compiler/translator/ImmutableStringBuilder.h"
#include "compiler/translator/ValidateGlobalInitializer.h"
#include "compiler/translator/glslang.h"
#include "compiler/translator/tree_util/BuiltIn.h"
#include "compiler/translator/tree_util/IntermNode_util.h"
#include "compiler/translator/util.h"

namespace sh
{

///////////////////////////////////////////////////////////////////////
//
// Sub- vector and matrix fields
//
////////////////////////////////////////////////////////////////////////

namespace
{
const int kWebGLMaxStructNesting = 4;

// The 1024 character identifier limit, `-2` for the `_u`
constexpr size_t kMaxAvailableIdentifierLength = 1022;

bool ShouldEnforceESSL100LoopAndIndexingLimitations(ShShaderSpec spec, int shaderVersion)
{
    // If compiling an ESSL 1.00 shader for WebGL, validate loop and indexing as well (to verify
    // that the shader only uses minimal functionality of ESSL 1.00 as in Appendix A of the spec).
    return IsWebGLBasedSpec(spec) && shaderVersion == 100;
}

struct IsSamplerFunc
{
    bool operator()(TBasicType type) { return IsSampler(type); }
};
struct IsOpaqueFunc
{
    bool operator()(TBasicType type) { return IsOpaqueType(type); }
};

template <typename OpaqueFunc>
bool ContainsOpaque(const TStructure *structType);

template <typename OpaqueFunc>
bool ContainsOpaque(const TType &type)
{
    if (OpaqueFunc{}(type.getBasicType()))
    {
        return true;
    }
    if (type.getBasicType() == EbtStruct)
    {
        return ContainsOpaque<OpaqueFunc>(type.getStruct());
    }

    return false;
}

template <typename OpaqueFunc>
bool ContainsOpaque(const TStructure *structType)
{
    for (const auto &field : structType->fields())
    {
        if (ContainsOpaque<OpaqueFunc>(*field->type()))
        {
            return true;
        }
    }
    return false;
}

// Get a token from an image argument to use as an error message token.
const char *GetImageArgumentToken(TIntermTyped *imageNode)
{
    ASSERT(IsImage(imageNode->getBasicType()));
    while (imageNode->getAsBinaryNode() &&
           (imageNode->getAsBinaryNode()->getOp() == EOpIndexIndirect ||
            imageNode->getAsBinaryNode()->getOp() == EOpIndexDirect))
    {
        imageNode = imageNode->getAsBinaryNode()->getLeft();
    }
    TIntermSymbol *imageSymbol = imageNode->getAsSymbolNode();
    if (imageSymbol)
    {
        return imageSymbol->getName().data();
    }
    return "image";
}

bool CanSetDefaultPrecisionOnType(const TPublicType &type)
{
    if (!SupportsPrecision(type.getBasicType()))
    {
        return false;
    }
    if (type.getBasicType() == EbtUInt)
    {
        // ESSL 3.00.4 section 4.5.4
        return false;
    }
    if (type.isAggregate())
    {
        // Not allowed to set for aggregate types
        return false;
    }
    return true;
}

// Map input primitive types to input array sizes in a geometry shader.
GLuint GetGeometryShaderInputArraySize(TLayoutPrimitiveType primitiveType)
{
    switch (primitiveType)
    {
        case EptPoints:
            return 1u;
        case EptLines:
            return 2u;
        case EptTriangles:
            return 3u;
        case EptLinesAdjacency:
            return 4u;
        case EptTrianglesAdjacency:
            return 6u;
        default:
            UNREACHABLE();
            return 0u;
    }
}

bool IsBufferOrSharedVariable(TIntermTyped *var)
{
    if (var->isInterfaceBlock() || var->getQualifier() == EvqBuffer ||
        var->getQualifier() == EvqShared)
    {
        return true;
    }
    return false;
}

TIntermTyped *FindLValueBase(TIntermTyped *node)
{
    do
    {
        const TIntermBinary *binary = node->getAsBinaryNode();
        if (binary == nullptr)
        {
            return node;
        }

        TOperator op = binary->getOp();
        if (op != EOpIndexDirect && op != EOpIndexIndirect)
        {
            return static_cast<TIntermTyped *>(nullptr);
        }

        node = binary->getLeft();
    } while (true);
}

void AddAdvancedBlendEquation(gl::BlendEquationType eq, TLayoutQualifier *qualifier)
{
    qualifier->advancedBlendEquations.set(static_cast<uint32_t>(eq));
}

constexpr bool IsValidWithPixelLocalStorage(TLayoutImageInternalFormat internalFormat)
{
    switch (internalFormat)
    {
        case EiifRGBA8:
        case EiifRGBA8I:
        case EiifRGBA8UI:
        case EiifR32F:
        case EiifR32I:
        case EiifR32UI:
            return true;
        default:
            return false;
    }
}

constexpr ShPixelLocalStorageFormat ImageFormatToPLSFormat(TLayoutImageInternalFormat format)
{
    switch (format)
    {
        default:
            return ShPixelLocalStorageFormat::NotPLS;
        case EiifRGBA8:
            return ShPixelLocalStorageFormat::RGBA8;
        case EiifRGBA8I:
            return ShPixelLocalStorageFormat::RGBA8I;
        case EiifRGBA8UI:
            return ShPixelLocalStorageFormat::RGBA8UI;
        case EiifR32F:
            return ShPixelLocalStorageFormat::R32F;
        case EiifR32I:
            return ShPixelLocalStorageFormat::R32I;
        case EiifR32UI:
            return ShPixelLocalStorageFormat::R32UI;
    }
}

bool UsesDerivatives(TIntermAggregate *functionCall)
{
    const TOperator op = functionCall->getOp();
    if (BuiltInGroup::IsDerivativesFS(op))
    {
        return true;
    }
    switch (op)
    {
        // TextureFirstVersions with implicit LOD
        case EOpTexture2D:
        case EOpTexture2DProj:
        case EOpTextureCube:
        case EOpTexture3D:
        case EOpTexture3DProj:
        case EOpShadow2DEXT:
        case EOpShadow2DProjEXT:
        // TextureFirstVersionsBias
        case EOpTexture2DBias:
        case EOpTexture2DProjBias:
        case EOpTextureCubeBias:
        case EOpTexture3DBias:
        case EOpTexture3DProjBias:
        // TextureNoBias
        case EOpTexture:
        case EOpTextureProj:
        // TextureBias
        case EOpTextureBias:
        case EOpTextureProjBias:
        // TextureOffsetNoBias
        case EOpTextureOffset:
        case EOpTextureProjOffset:
        // TextureOffsetBias
        case EOpTextureOffsetBias:
        case EOpTextureProjOffsetBias:
        // TextureQueryLod
        case EOpTextureQueryLOD:
            return true;
        default:
            return false;
    }
}

bool IsSamplerOrStructWithOnlySamplers(const TType *type)
{
    return IsSampler(type->getBasicType()) || type->isStructureContainingOnlySamplers();
}

bool IsClipCullEncountered(const ClipCullDistanceInfo &info)
{
    return info.maxIndex >= 0 || info.hasNonConstIndex || info.hasArrayLengthMethodCall;
}

void MarkClipCullFirstEncounter(const TSourceLoc &line, ClipCullDistanceInfo *info)
{
    if (info->firstEncounter.first_line < 0)
    {
        info->firstEncounter = line;
    }
}

void MarkClipCullRedeclaredSize(const TSourceLoc &line,
                                uint32_t arraySize,
                                ClipCullDistanceInfo *info)
{
    MarkClipCullFirstEncounter(line, info);
    info->size = arraySize;
}

void MarkClipCullArrayLengthMethodCall(const TSourceLoc &line, ClipCullDistanceInfo *info)
{
    MarkClipCullFirstEncounter(line, info);
    info->hasArrayLengthMethodCall = true;
}

void MarkClipCullIndex(const TSourceLoc &line, TIntermTyped *indexExpr, ClipCullDistanceInfo *info)
{
    MarkClipCullFirstEncounter(line, info);
    const TConstantUnion *constIdx = indexExpr->getConstantValue();
    if (constIdx)
    {
        int idx = 0;
        switch (constIdx->getType())
        {
            case EbtInt:
                idx = constIdx->getIConst();
                break;
            case EbtUInt:
                idx = constIdx->getUConst();
                break;
            default:
                // This can happen due to a compile error that is generated elsewhere.
                break;
        }

        info->maxIndex = std::max(info->maxIndex, idx);
    }
    else
    {
        info->hasNonConstIndex = true;
    }
}

void ValidateFragColorAndFragData(GLenum shaderType,
                                  int shaderVersion,
                                  const TSymbolTable &symbolTable,
                                  TDiagnostics *diagnostics)
{
    if (shaderVersion > 100 || shaderType != GL_FRAGMENT_SHADER)
    {
        return;
    }

    bool usesFragColor = false;
    bool usesFragData  = false;
    // This validation is a bit stricter than the spec - it's only an error to write to
    // both FragData and FragColor. But because it's better not to have reads from undefined
    // variables, we always return an error if they are both referenced, rather than only if they
    // are written.
    if (symbolTable.isStaticallyUsed(*BuiltInVariable::gl_FragColor()) ||
        symbolTable.isStaticallyUsed(*BuiltInVariable::gl_SecondaryFragColorEXT()))
    {
        usesFragColor = true;
    }
    const bool secondaryFragDataUsed = symbolTable.isSecondaryFragDataUsed();
    if (symbolTable.isStaticallyUsed(*symbolTable.gl_FragData()) || secondaryFragDataUsed)
    {
        usesFragData = true;
    }
    if (usesFragColor && usesFragData)
    {
        const char *errorMessage = "cannot use both gl_FragData and gl_FragColor";
        if (symbolTable.isStaticallyUsed(*BuiltInVariable::gl_SecondaryFragColorEXT()) ||
            secondaryFragDataUsed)
        {
            errorMessage =
                "cannot use both output variable sets (gl_FragData, gl_SecondaryFragDataEXT)"
                " and (gl_FragColor, gl_SecondaryFragColorEXT)";
        }
        diagnostics->globalError(errorMessage);
    }
}

bool IsESSL100ConstantExpression(TIntermNode *node)
{
    return node->getAsConstantUnion() != nullptr && node->getAsTyped()->getQualifier() == EvqConst;
}

// Calculate the size of a variable for validation purposes.  If the variable is a UBO, add padding
// that makes the calculated size _at least_ as large as std140 requires.  Given the limits are
// arbitrary and overly large, there is no need to be precise about this calculation as long as the
// calculated size is an overestimation of the real size (which could be, by a small amount).
angle::base::CheckedNumeric<size_t> CalculateVariableSize(const TType *type, bool isStd140)
{
    constexpr size_t kVec4Size = sizeof(float) * 4;

    if (type->isArray())
    {
        TType elementType = *type;
        elementType.toArrayBaseType();
        angle::base::CheckedNumeric<size_t> elementSize =
            CalculateVariableSize(&elementType, isStd140);

        return elementSize * type->getArraySizeProduct();
    }

    if (type->getBasicType() == EbtStruct || type->getBasicType() == EbtInterfaceBlock)
    {
        const TFieldList &fields                      = type->getBasicType() == EbtStruct
                                                            ? type->getStruct()->fields()
                                                            : type->getInterfaceBlock()->fields();
        angle::base::CheckedNumeric<size_t> totalSize = 0;
        for (const TField *field : fields)
        {
            const TType *fieldType = field->type();
            totalSize += CalculateVariableSize(fieldType, isStd140);
        }
        return totalSize;
    }

    if (type->isMatrix())
    {
        if (isStd140)
        {
            // Ignore row vs column major, and get the biggest size of the two possibilities as a
            // possibly slight overestimation.  Note that the size according to std140 is either
            // rows times vec4 or cols times vec4 based on how the matrix is laid out.
            return std::max(type->getRows(), type->getCols()) * kVec4Size;
        }
        else
        {
            return type->getRows() * type->getCols() * sizeof(float);
        }
    }

    // For vectors and scalars, return the size of a vec4 for std140.  This is a slight
    // overestimation.  If this is the element of an array though, it's accurate (which is why it's
    // a slight overestimation, e.g. the size of a large array of a scalar type is not
    // overestimated).
    return isStd140 ? kVec4Size : type->getNominalSize() * sizeof(float);
}

unsigned int GetMaxUniformBlocksForShaderType(sh::GLenum shaderType,
                                              const ShCompileOptions &options,
                                              const ShBuiltInResources &resources)
{
    // If the validatePerStageMaxUniformBlocks workaround is disabled. Set a limit that will not be
    // hit.
    if (!options.validatePerStageMaxUniformBlocks)
    {
        return std::numeric_limits<unsigned int>::max();
    }

    switch (shaderType)
    {
        case GL_FRAGMENT_SHADER:
            return resources.MaxFragmentUniformBlocks;
        case GL_VERTEX_SHADER:
            return resources.MaxVertexUniformBlocks;
        case GL_COMPUTE_SHADER:
            return resources.MaxComputeUniformBlocks;
        case GL_GEOMETRY_SHADER:
            return resources.MaxGeometryUniformBlocks;
        case GL_TESS_CONTROL_SHADER:
            return resources.MaxTessControlUniformBlocks;
        case GL_TESS_EVALUATION_SHADER:
            return resources.MaxTessEvaluationUniformBlocks;
        default:
            UNREACHABLE();
            return 0;
    }
}

enum class StructureOriginalScope
{
    Global,
    FunctionLocal,
};

TIntermDeclaration *DeclareStruct(TSymbolTable *symbolTable, const TStructure *structure)
{
    TType *namedType = new TType(structure, true);
    namedType->setQualifier(EvqGlobal);

    TVariable *structVariable =
        new TVariable(symbolTable, kEmptyImmutableString, namedType, SymbolType::Empty);
    TIntermSymbol *structDeclarator       = new TIntermSymbol(structVariable);
    TIntermDeclaration *structDeclaration = new TIntermDeclaration;
    structDeclaration->appendDeclarator(structDeclarator);
    return structDeclaration;
}

TIntermDeclaration *RenameAndDeclareStruct(TSymbolTable *symbolTable,
                                           TStructure *structure,
                                           StructureOriginalScope scope)
{
    ASSERT(!structure->name().empty());

    // We need +2 space for _0 if global, or +11 for "_uniqueId" if function-local.  Using +11
    // always for simplicity.  +1 for the NUL terminator.
    //
    // If appending ID and the name is too long, cut off the end of the name.  The ID makes it
    // unique.  Where _0 is used, long names are hashed to avoid collision.
    constexpr uint32_t kAppendExtraChars = 1 + 11;  // underscore + 32-bit number

    ImmutableStringBuilder builder(structure->name().length() + kAppendExtraChars);
    if (scope == StructureOriginalScope::Global &&
        structure->name().length() > kESSLMaxIdentifierLength - kAppendExtraChars)
    {
        angle::base::SecureHashAlgorithm hasher;
        hasher.Init();
        hasher.Update(structure->name().data(), structure->name().length());
        hasher.Final();
        // SAFETY: The SHA1 hasher guarantees that the returned digest is kSHA1Length bytes.
        const std::string hashed = angle::UintStreamToHexString(
            ANGLE_UNSAFE_BUFFERS(angle::Span(hasher.Digest(), angle::base::kSHA1Length)));
        builder << "struct" << ImmutableString(hashed.c_str(), hashed.size()) << "_0";
    }
    else
    {
        const ImmutableString name(structure->name().data(),
                                   std::min<size_t>(structure->name().length(),
                                                    kESSLMaxIdentifierLength - kAppendExtraChars));
        builder << name << "_"
                << (scope == StructureOriginalScope::Global ? 0 : structure->uniqueId().get());
    }
    structure->setName(builder);

    return DeclareStruct(symbolTable, structure);
}

unsigned int GetTypeComponentCount(const TType &type)
{
    unsigned int components = 0;
    if (type.getBasicType() == EbtInterfaceBlock)
    {
        for (const TField *field : type.getInterfaceBlock()->fields())
        {
            components += GetTypeComponentCount(*field->type());
        }
    }
    else if (type.getStruct())
    {
        for (const TField *field : type.getStruct()->fields())
        {
            components += GetTypeComponentCount(*field->type());
        }
    }
    else
    {
        components = static_cast<unsigned int>(type.getNominalSize()) * type.getSecondarySize();
    }
    components *= type.getArraySizeProduct();
    return components;
}

SamplerAccess GetSamplerAccess(TIntermTyped *node)
{
    // Ignore left-hand side of comma
    while (node->getAsBinaryNode() && node->getAsBinaryNode()->getOp() == EOpComma)
    {
        node = node->getAsBinaryNode()->getRight();
    }

    // Look for |a.b[i].c| and extract |a| as well as the field indices of |b| and |c|.
    SamplerAccess access = {};
    TVector<uint32_t> fields;
    while (true)
    {
        TIntermSymbol *asSymbol = node->getAsSymbolNode();
        if (asSymbol != nullptr)
        {
            access.uniform = &asSymbol->variable();
            break;
        }

        TIntermBinary *asBinary = node->getAsBinaryNode();
        // In a correct shader, there should be nothing but EOpIndex* until the symbol is found, but
        // the code cannot assert that as the shader may have errors.
        if (asBinary != nullptr)
        {
            if (asBinary->getOp() == EOpIndexDirectStruct)
            {
                TIntermConstantUnion *field = asBinary->getRight()->getAsConstantUnion();
                if (field != nullptr)
                {
                    fields.push_back(field->getIConst(0));
                }
            }
            node = asBinary->getLeft();
        }
        else
        {
            // Erroneous shader
            break;
        }
    }

    // The fields are gathered in reverse order, which is fixed here.
    access.fields.insert(access.fields.end(), fields.rbegin(), fields.rend());
    return access;
}

SamplerAccess ConcatSamplerAccess(const SamplerAccess &callerAccess,
                                  const SamplerAccess &calleeAccess)
{
    // If caller has var.field1 and callee has param.field2, the result is var.field1.field2.
    SamplerAccess result = callerAccess;
    result.fields.insert(result.fields.end(), calleeAccess.fields.begin(),
                         calleeAccess.fields.end());
    return result;
}

void CombineSamplerAccessToTexelFetch(
    const SamplerAccess &callerAccess,
    const TVector<TUnorderedSet<SamplerAccess>> &calleeParamsStaticallyUsedWithTexelFetch,
    uint32_t calleeArgIndex,
    TUnorderedSet<SamplerAccess> *samplersStaticallyUsedWithTexelFetch)
{
    if (calleeArgIndex >= calleeParamsStaticallyUsedWithTexelFetch.size())
    {
        return;
    }

    for (const SamplerAccess &calleeAccess :
         calleeParamsStaticallyUsedWithTexelFetch[calleeArgIndex])
    {
        // uniform+fields is passed to function argument, which itself applies more fields and then
        // uses the result in texelFetch.
        SamplerAccess access = ConcatSamplerAccess(callerAccess, calleeAccess);
        samplersStaticallyUsedWithTexelFetch->insert(std::move(access));
    }
}

void CombineSamplerAccessToFunctionArg(
    const SamplerAccess &callerAccess,
    const TVector<TUnorderedSet<SamplerAsFunctionArg>> &calleeParamsPassedToCallee,
    uint32_t calleeArgIndex,
    TUnorderedSet<SamplerAsFunctionArg> *uniformsPassedToCallee)
{
    if (calleeArgIndex >= calleeParamsPassedToCallee.size())
    {
        return;
    }

    for (const SamplerAsFunctionArg &calleeParamToArg : calleeParamsPassedToCallee[calleeArgIndex])
    {
        // uniform+fields is passed to function argument, which itself applies more fields and then
        // passes it to another function.
        SamplerAsFunctionArg uniformToArg;
        uniformToArg.access         = ConcatSamplerAccess(callerAccess, calleeParamToArg.access);
        uniformToArg.callee         = calleeParamToArg.callee;
        uniformToArg.calleeArgIndex = calleeParamToArg.calleeArgIndex;

        // Make it such that it looks like the callee is directly passing the uniform to it's own
        // callees.
        uniformsPassedToCallee->insert(std::move(uniformToArg));
    }
}

TVector<const TFunction *> GetSamplerPassingFunctionsInDAGOrder(
    const TFunction *main,
    const TUnorderedMap<const TFunction *, FunctionSamplerAccess> &functionsSamplerAccess)
{
    // The call graph is already checked for loops before this function is called.  Additionally,
    // |main| is already verified to be present.
    TUnorderedSet<const TFunction *> visited;

    struct StackEntry
    {
        const TFunction *function;
        bool postVisit;
    };
    TVector<StackEntry> visitStack;
    visitStack.reserve(functionsSamplerAccess.size());
    visitStack.push_back({main, false});

    // Topological sort via DFS.
    TVector<const TFunction *> sorted;
    while (!visitStack.empty())
    {
        StackEntry &entry         = visitStack.back();
        const TFunction *function = entry.function;

        if (entry.postVisit)
        {
            visited.insert(function);
            sorted.push_back(function);
            visitStack.pop_back();
            continue;
        }

        // If node is already visited, ignore it.
        if (visited.find(function) != visited.end())
        {
            visitStack.pop_back();
            continue;
        }

        // Leave the entry be for post-visit
        entry.postVisit = true;

        // Add the callees to the stack
        auto functionSamplerAccess = functionsSamplerAccess.find(function);
        if (functionSamplerAccess == functionsSamplerAccess.end())
        {
            continue;
        }

        for (const SamplerAsFunctionArg &arg : functionSamplerAccess->second.uniformsPassedToCallee)
        {
            visitStack.push_back({arg.callee, false});
        }
        for (const auto &paramToArg : functionSamplerAccess->second.paramsPassedToCallee)
        {
            for (const SamplerAsFunctionArg &arg : paramToArg)
            {
                visitStack.push_back({arg.callee, false});
            }
        }
    }

    return sorted;
}
}  // namespace

// This tracks each binding point's current default offset for inheritance of subsequent
// variables using the same binding, and keeps offsets unique and non overlapping.
// See GLSL ES 3.1, section 4.4.6.
class TParseContext::AtomicCounterBindingState
{
  public:
    AtomicCounterBindingState() : mDefaultOffset(0) {}
    // Inserts a new span and returns -1 if overlapping, else returns the starting offset of
    // newly inserted span.
    int insertSpan(int start, size_t length)
    {
        gl::RangeI newSpan(start, start + static_cast<int>(length));
        for (const auto &span : mSpans)
        {
            if (newSpan.intersects(span))
            {
                return -1;
            }
        }
        mSpans.push_back(newSpan);
        mDefaultOffset = newSpan.high();
        return start;
    }
    // Inserts a new span starting from the default offset.
    int appendSpan(size_t length) { return insertSpan(mDefaultOffset, length); }
    void setDefaultOffset(int offset) { mDefaultOffset = offset; }

  private:
    int mDefaultOffset;
    std::vector<gl::RangeI> mSpans;
};

TParseContext::TParseContext(TSymbolTable &symt,
                             TExtensionBehavior &ext,
                             sh::GLenum type,
                             ShShaderSpec spec,
                             const ShCompileOptions &options,
                             TDiagnostics *diagnostics,
                             const ShBuiltInResources &resources,
                             ShShaderOutput outputType)
    : symbolTable(symt),
      mDeferredNonEmptyDeclarationErrorCheck(false),
      mShaderType(type),
      mShaderSpec(spec),
      mCompileOptions(options),
      mResources(resources),
      mShaderVersion(100),
      mTreeRoot(nullptr),
      mStructNestingLevel(0),
      mCurrentFunction(nullptr),
      mFunctionReturnsValue(false),
      mEarlyFragmentTestsSpecified(false),
      mHasDiscard(false),
      mSampleQualifierSpecified(false),
      mPositionRedeclaredForSeparateShaderObject(false),
      mPointSizeRedeclaredForSeparateShaderObject(false),
      mPositionOrPointSizeUsedForSeparateShaderObject(false),
      mUsesDerivatives(false),
      mDefaultUniformMatrixPacking(EmpColumnMajor),
      mDefaultUniformBlockStorage(sh::IsWebGLBasedSpec(spec) ? EbsStd140 : EbsShared),
      mDefaultBufferMatrixPacking(EmpColumnMajor),
      mDefaultBufferBlockStorage(sh::IsWebGLBasedSpec(spec) ? EbsStd140 : EbsShared),
      mDiagnostics(diagnostics),
      mDirectiveHandler(ext, *mDiagnostics, *this, mShaderType),
      mPreprocessor(mDiagnostics,
                    &mDirectiveHandler,
                    angle::pp::PreprocessorSettings(
                        spec,
                        options.allowExtensionDisableAfterNonPPTokensInWebGL
                            ? angle::pp::WebGLExtensionDisableBehavior::AnywhereInShader
                            : angle::pp::WebGLExtensionDisableBehavior::Standard)),
      mScanner(nullptr),
      mComputeShaderLocalSizeDeclared(false),
      mComputeShaderLocalSize(-1),
      mNumViews(-1),
      mMaxUniformBlocks(GetMaxUniformBlocksForShaderType(mShaderType, options, resources)),
      mNumUniformBlocks(0),
      mNumOutputVaryingComponents(0),
      mDeclaringFunction(false),
      mDeclaringMain(false),
      mMainFunction(nullptr),
      mIsReturnVisitedInMain(false),
      mValidateESSL100Limitations(
          ShouldEnforceESSL100LoopAndIndexingLimitations(spec, mShaderVersion)),
      mFragmentOutputIndex1Used(false),
      mFragmentOutputFragDepthUsed(false),
      mMaxFragDataArrayIndexUsed(0),
      mGeometryShaderInputPrimitiveType(EptUndefined),
      mGeometryShaderOutputPrimitiveType(EptUndefined),
      mGeometryShaderInvocations(0),
      mGeometryShaderMaxVertices(-1),
      mGeometryInputArraySize(0),
      mTessControlShaderOutputVertices(0),
      mTessEvaluationShaderInputPrimitiveType(EtetUndefined),
      mTessEvaluationShaderInputVertexSpacingType(EtetUndefined),
      mTessEvaluationShaderInputOrderingType(EtetUndefined),
      mTessEvaluationShaderInputPointType(EtetUndefined),
      mHasAnyPreciseType(false),
      mAdvancedBlendEquations(0),
      mFunctionBodyNewScope(false),
      mOutputType(outputType),
      mIRBuilder(gl::FromGLenum<gl::ShaderType>(type), options)
{
    mDiagnostics->setIRBuilder(&mIRBuilder);

    // If not using the IR, don't build it by pretending there's been an error.
    if (!mCompileOptions.useIR)
    {
        mIRBuilder.onError();
    }
}

TParseContext::~TParseContext()
{
    mDiagnostics->setIRBuilder(nullptr);
}

ir::IR TParseContext::getIR()
{
    // Set advanced blend if specified.  This is not done during parse because multiple statements
    // accumulate modes.
    if (mAdvancedBlendEquations.any())
    {
        mIRBuilder.setAdvancedBlendEquations(mAdvancedBlendEquations.bits());
    }

    return ir::Builder::destroy(std::move(mIRBuilder));
}

void TParseContext::onShaderVersionDeclared(const TSourceLoc &loc, int version)
{
    mShaderVersion = version;
    checkShaderVersion(loc);
    // Update cached decisions that depend on the shader version
    mValidateESSL100Limitations =
        ShouldEnforceESSL100LoopAndIndexingLimitations(mShaderSpec, mShaderVersion);
}

void TParseContext::fatal(const TSourceLoc &loc, const char *reason)
{
    error(loc, reason, "");
    mPreprocessor.forceEOF();
}

bool TParseContext::checkShaderVersion(const TSourceLoc &loc)
{
    if (GetMaxShaderVersionForSpec(mShaderSpec) < mShaderVersion)
    {
        std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
        reasonStream << "unsupported shader version ";
        reasonStream << mShaderVersion;
        fatal(loc, reasonStream.str().c_str());
        return false;
    }

    switch (mShaderType)
    {
        case GL_COMPUTE_SHADER:
            if (mShaderVersion < 310)
            {
                fatal(loc, "Compute shader is not supported in this shader version.");
                return false;
            }
            break;

        case GL_GEOMETRY_SHADER_EXT:
            if (mShaderVersion < 310)
            {
                fatal(loc, "Geometry shader is not supported in this shader version.");
                return false;
            }
            break;

        case GL_TESS_CONTROL_SHADER_EXT:
        case GL_TESS_EVALUATION_SHADER_EXT:
            if (mShaderVersion < 310)
            {
                fatal(loc, "Tessellation shaders are not supported in this shader version.");
                return false;
            }
            break;

        default:
            break;
    }

    return true;
}

bool TParseContext::checkCanUseShaderType(const TSourceLoc &loc)
{
    switch (mShaderType)
    {
        case GL_GEOMETRY_SHADER_EXT:
            if (mShaderVersion == 310)
            {
                if (!checkCanUseOneOfExtensions(
                        loc, std::array<TExtension, 2u>{{TExtension::EXT_geometry_shader,
                                                         TExtension::OES_geometry_shader}}))
                {
                    return false;
                }
            }
            break;

        case GL_TESS_CONTROL_SHADER_EXT:
        case GL_TESS_EVALUATION_SHADER_EXT:
            if (mShaderVersion == 310)
            {
                if (!checkCanUseOneOfExtensions(
                        loc, std::array<TExtension, 2u>{{TExtension::EXT_tessellation_shader,
                                                         TExtension::OES_tessellation_shader}}))
                {
                    return false;
                }
            }
            break;

        default:
            break;
    }

    return true;
}

bool TParseContext::anyMultiviewExtensionAvailable()
{
    return isExtensionEnabled(TExtension::OVR_multiview) ||
           isExtensionEnabled(TExtension::OVR_multiview2);
}

bool TParseContext::parseVectorFields(const TSourceLoc &line,
                                      const ImmutableString &compString,
                                      uint32_t vecSize,
                                      TVector<uint32_t> *fieldOffsets)
{
    ASSERT(fieldOffsets);
    size_t fieldCount = compString.length();
    if (fieldCount > 4u)
    {
        error(line, "illegal vector field selection", compString);
        return false;
    }
    fieldOffsets->resize(fieldCount);

    enum
    {
        exyzw,
        ergba,
        estpq
    } fieldSet[4];

    for (unsigned int i = 0u; i < fieldOffsets->size(); ++i)
    {
        switch (compString[i])
        {
            case 'x':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 0;
                    fieldSet[i]        = exyzw;
                })
                break;
            case 'r':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 0;
                    fieldSet[i]        = ergba;
                })
                break;
            case 's':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 0;
                    fieldSet[i]        = estpq;
                })
                break;
            case 'y':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 1;
                    fieldSet[i]        = exyzw;
                })
                break;
            case 'g':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 1;
                    fieldSet[i]        = ergba;
                })
                break;
            case 't':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 1;
                    fieldSet[i]        = estpq;
                })
                break;
            case 'z':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 2;
                    fieldSet[i]        = exyzw;
                })
                break;
            case 'b':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 2;
                    fieldSet[i]        = ergba;
                })
                break;
            case 'p':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 2;
                    fieldSet[i]        = estpq;
                })
                break;

            case 'w':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 3;
                    fieldSet[i]        = exyzw;
                })
                break;
            case 'a':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 3;
                    fieldSet[i]        = ergba;
                })
                break;
            case 'q':
                ANGLE_UNSAFE_TODO({
                    (*fieldOffsets)[i] = 3;
                    fieldSet[i]        = estpq;
                })
                break;
            default:
                error(line, "illegal vector field selection", compString);
                return false;
        }
    }

    for (unsigned int i = 0u; i < fieldOffsets->size(); ++i)
    {
        if ((*fieldOffsets)[i] >= vecSize)
        {
            error(line, "vector field selection out of range", compString);
            return false;
        }

        if (i > 0)
        {
            if (ANGLE_UNSAFE_TODO(fieldSet[i] != fieldSet[i - 1]))
            {
                error(line, "illegal - vector component fields not from the same set", compString);
                return false;
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////
//
// Errors
//
////////////////////////////////////////////////////////////////////////

//
// Used by flex/bison to output all syntax and parsing errors.
//
void TParseContext::error(const TSourceLoc &loc, const char *reason, const char *token)
{
    mDiagnostics->error(loc, reason, token);
}

void TParseContext::error(const TSourceLoc &loc, const char *reason, const ImmutableString &token)
{
    mDiagnostics->error(loc, reason, token.data());
}

void TParseContext::warning(const TSourceLoc &loc, const char *reason, const char *token)
{
    mDiagnostics->warning(loc, reason, token);
}

void TParseContext::errorIfPLSDeclared(const TSourceLoc &loc, PLSIllegalOperations op)
{
    if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
    {
        return;
    }
    if (mPLSLayouts.empty())
    {
        // No pixel local storage uniforms have been declared yet. Remember this potential error in
        // case PLS gets declared later.
        mPLSPotentialErrors.emplace_back(loc, op);
        return;
    }
    switch (op)
    {
        case PLSIllegalOperations::Discard:
            error(loc, "illegal discard when pixel local storage is declared", "discard");
            break;
        case PLSIllegalOperations::ReturnFromMain:
            error(loc, "illegal return from main when pixel local storage is declared", "return");
            break;
        case PLSIllegalOperations::AssignFragDepth:
            error(loc, "value not assignable when pixel local storage is declared", "gl_FragDepth");
            break;
        case PLSIllegalOperations::AssignSampleMask:
            error(loc, "value not assignable when pixel local storage is declared",
                  "gl_SampleMask");
            break;
        case PLSIllegalOperations::FragDataIndexNonzero:
            error(loc, "illegal nonzero index qualifier when pixel local storage is declared",
                  "layout");
            break;
        case PLSIllegalOperations::EnableAdvancedBlendEquation:
            error(loc, "illegal advanced blend equation when pixel local storage is declared",
                  "layout");
            break;
    }
}

void TParseContext::outOfRangeError(bool isError,
                                    const TSourceLoc &loc,
                                    const char *reason,
                                    const char *token)
{
    if (isError)
    {
        error(loc, reason, token);
    }
    else
    {
        warning(loc, reason, token);
    }
}

void TParseContext::setTreeRoot(TIntermBlock *treeRoot)
{
#ifdef ANGLE_IR
    // When the IR is used, make sure the temporary tree created during parse is not used by anyone.
    // With IR, eventually this tree doesn't need to be created at all, a stack of node properties
    // to verify / propagate is sufficient during parse for validation purposes.
    if (mCompileOptions.useIR)
    {
        return;
    }
#endif
    mTreeRoot = treeRoot;
    mTreeRoot->setIsTreeRoot();
}

//
// Same error message for all places assignments don't work.
//
void TParseContext::assignError(const TSourceLoc &line,
                                const char *op,
                                const TType &left,
                                const TType &right)
{
    TInfoSinkBase reasonStream;
    reasonStream << "cannot convert from '" << right << "' to '" << left << "'";
    error(line, reasonStream.c_str(), op);
}

//
// Same error message for all places unary operations don't work.
//
void TParseContext::unaryOpError(const TSourceLoc &line, const char *op, const TType &operand)
{
    TInfoSinkBase reasonStream;
    reasonStream << "wrong operand type - no operation '" << op
                 << "' exists that takes an operand of type " << operand
                 << " (or there is no acceptable conversion)";
    error(line, reasonStream.c_str(), op);
}

//
// Same error message for all binary operations don't work.
//
void TParseContext::binaryOpError(const TSourceLoc &line,
                                  const char *op,
                                  const TType &left,
                                  const TType &right)
{
    TInfoSinkBase reasonStream;
    reasonStream << "wrong operand types - no operation '" << op
                 << "' exists that takes a left-hand operand of type '" << left
                 << "' and a right operand of type '" << right
                 << "' (or there is no acceptable conversion)";
    error(line, reasonStream.c_str(), op);
}

void TParseContext::checkPrecisionSpecified(const TSourceLoc &line,
                                            TPrecision precision,
                                            TBasicType type)
{
    if (precision != EbpUndefined && !SupportsPrecision(type))
    {
        error(line, "illegal type for precision qualifier", getBasicString(type));
    }

    if (precision == EbpUndefined)
    {
        switch (type)
        {
            case EbtFloat:
                error(line, "No precision specified for (float)", "");
                return;
            case EbtInt:
            case EbtUInt:
                UNREACHABLE();  // there's always a predeclared qualifier
                error(line, "No precision specified (int)", "");
                return;
            default:
                if (IsOpaqueType(type))
                {
                    error(line, "No precision specified", getBasicString(type));
                    return;
                }
        }
    }
}

void TParseContext::markStaticUseIfSymbol(TIntermNode *node)
{
    TIntermSwizzle *swizzleNode = node->getAsSwizzleNode();
    if (swizzleNode)
    {
        markStaticUseIfSymbol(swizzleNode->getOperand());
        return;
    }
    TIntermBinary *binaryNode = node->getAsBinaryNode();
    if (binaryNode)
    {
        switch (binaryNode->getOp())
        {
            case EOpIndexDirect:
            case EOpIndexIndirect:
            case EOpIndexDirectStruct:
            case EOpIndexDirectInterfaceBlock:
                markStaticUseIfSymbol(binaryNode->getLeft());
                return;
            default:
                return;
        }
    }
    TIntermSymbol *symbolNode = node->getAsSymbolNode();
    if (symbolNode)
    {
        symbolTable.markStaticUse(symbolNode->variable());
    }
}

// Both test and if necessary, spit out an error, to see if the node is really
// an l-value that can be operated on this way.
bool TParseContext::checkCanBeLValue(const TSourceLoc &line, const char *op, TIntermTyped *node)
{
    TIntermSwizzle *swizzleNode = node->getAsSwizzleNode();
    if (swizzleNode)
    {
        bool ok = checkCanBeLValue(line, op, swizzleNode->getOperand());
        if (ok && swizzleNode->hasDuplicateOffsets())
        {
            error(line, " l-value of swizzle cannot have duplicate components", op);
            return false;
        }
        return ok;
    }

    TIntermBinary *binaryNode = node->getAsBinaryNode();
    if (binaryNode)
    {
        switch (binaryNode->getOp())
        {
            case EOpIndexDirect:
            case EOpIndexIndirect:
            case EOpIndexDirectStruct:
            case EOpIndexDirectInterfaceBlock:
                if (node->getMemoryQualifier().readonly)
                {
                    error(line, "can't modify a readonly variable", op);
                    return false;
                }
                return checkCanBeLValue(line, op, binaryNode->getLeft());
            default:
                break;
        }
        error(line, " l-value required", op);
        return false;
    }

    std::string message;
    switch (node->getQualifier())
    {
        case EvqConst:
            message = "can't modify a const";
            break;
        case EvqParamConst:
            message = "can't modify a const";
            break;
        case EvqAttribute:
            message = "can't modify an attribute";
            break;
        case EvqFragmentIn:
        case EvqVertexIn:
        case EvqGeometryIn:
        case EvqTessControlIn:
        case EvqTessEvaluationIn:
        case EvqSmoothIn:
        case EvqFlatIn:
        case EvqNoPerspectiveIn:
        case EvqCentroidIn:
        case EvqSampleIn:
        case EvqNoPerspectiveCentroidIn:
        case EvqNoPerspectiveSampleIn:
        case EvqPatchIn:
            message = "can't modify an input";
            break;
        case EvqUniform:
            message = "can't modify a uniform";
            break;
        case EvqVaryingIn:
            message = "can't modify a varying";
            break;
        case EvqInstanceID:
            message = "can't modify gl_InstanceID";
            break;
        case EvqVertexID:
            message = "can't modify gl_VertexID";
            break;
        case EvqBaseVertex:
            message = "can't modify gl_BaseVertex";
            break;
        case EvqBaseInstance:
            message = "can't modify gl_BaseInstance";
            break;
        case EvqDrawID:
            message = "can't modify gl_DrawID";
            break;
        case EvqFragCoord:
            message = "can't modify gl_FragCoord";
            break;
        case EvqFrontFacing:
            message = "can't modify gl_FrontFacing";
            break;
        case EvqHelperInvocation:
            message = "can't modify gl_HelperInvocation";
            break;
        case EvqPointCoord:
            message = "can't modify gl_PointCoord";
            break;
        case EvqNumWorkGroups:
            message = "can't modify gl_NumWorkGroups";
            break;
        case EvqWorkGroupSize:
            message = "can't modify gl_WorkGroupSize";
            break;
        case EvqWorkGroupID:
            message = "can't modify gl_WorkGroupID";
            break;
        case EvqLocalInvocationID:
            message = "can't modify gl_LocalInvocationID";
            break;
        case EvqGlobalInvocationID:
            message = "can't modify gl_GlobalInvocationID";
            break;
        case EvqLocalInvocationIndex:
            message = "can't modify gl_LocalInvocationIndex";
            break;
        case EvqViewIDOVR:
            message = "can't modify gl_ViewID_OVR";
            break;
        case EvqDepthRange:
            message = "can't modify gl_DepthRange";
            break;
        case EvqComputeIn:
            message = "can't modify work group size variable";
            break;
        case EvqPerVertexIn:
            message = "can't modify any member in gl_in";
            break;
        case EvqPrimitiveIDIn:
            message = "can't modify gl_PrimitiveIDIn";
            break;
        case EvqInvocationID:
            message = "can't modify gl_InvocationID";
            break;
        case EvqPrimitiveID:
            if (mShaderType == GL_FRAGMENT_SHADER)
            {
                message = "can't modify gl_PrimitiveID in a fragment shader";
            }
            break;
        case EvqLayerIn:
            message = "can't modify gl_Layer in a fragment shader";
            break;
        case EvqShadingRateEXT:
            message = "can't modify gl_ShadingRateEXT";
            break;
        case EvqSampleID:
            message = "can't modify gl_SampleID";
            break;
        case EvqSampleMaskIn:
            message = "can't modify gl_SampleMaskIn";
            break;
        case EvqSamplePosition:
            message = "can't modify gl_SamplePosition";
            break;
        case EvqNumSamples:
            message = "can't modify gl_NumSamples";
            break;
        case EvqPatchVerticesIn:
            message = "can't modify gl_PatchVerticesIn";
            break;
        case EvqTessCoord:
            message = "can't modify gl_TessCoord";
            break;
        case EvqClipDistance:
            if (mShaderType == GL_FRAGMENT_SHADER)
            {
                message = "can't modify gl_ClipDistance in a fragment shader";
            }
            break;
        case EvqCullDistance:
            if (mShaderType == GL_FRAGMENT_SHADER)
            {
                message = "can't modify gl_CullDistance in a fragment shader";
            }
            break;
        case EvqFragDepth:
            errorIfPLSDeclared(line, PLSIllegalOperations::AssignFragDepth);
            break;
        case EvqSampleMask:
            errorIfPLSDeclared(line, PLSIllegalOperations::AssignSampleMask);
            break;
        default:
            //
            // Type that can't be written to?
            //
            if (node->getBasicType() == EbtVoid)
            {
                message = "can't modify void";
            }
            if (IsOpaqueType(node->getBasicType()))
            {
                message = "can't modify a variable with type ";
                message += getBasicString(node->getBasicType());
            }
            else if (node->getMemoryQualifier().readonly)
            {
                message = "can't modify a readonly variable";
            }
    }

    // Disallow modification of structs that have samplers, even if the field being modified is not
    // a sampler for example in code like this:
    //
    //     void f(StructWithSampler s) {
    //         s.non_sampler = 1.0;
    //     }
    //
    // The above is valid GLSL code, but is not currently supported due to limitations of the
    // MonomorphizeUnsupportedFunctions pass.  On mesa/GL drivers, where
    // MonomorphizeUnsupportedFunctions is not used, code like the above crashes the compiler.
    if (message.empty())
    {
        TIntermTyped *lvalueBase = FindLValueBase(node);
        if (lvalueBase->getType().isStructureContainingSamplers())
        {
            message = "modifying structures containing samplers is not currently supported";
        }
    }

    ASSERT(binaryNode == nullptr && swizzleNode == nullptr);
    TIntermSymbol *symNode = node->getAsSymbolNode();
    if (message.empty() && symNode != nullptr)
    {
        if (mValidateESSL100Limitations)
        {
            checkESSL100NoLoopSymbolAssign(symNode, line);
        }
        if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior)
        {
            // For simplicity, if a variable is written to, assume it's no longer always true.
            mConstantTrueVariables.erase(symNode->variable().uniqueId());
        }

        symbolTable.markStaticUse(symNode->variable());
        return true;
    }

    std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
    reasonStream << "l-value required";
    if (!message.empty())
    {
        if (symNode)
        {
            // Symbol inside an expression can't be nameless.
            ASSERT(symNode->variable().symbolType() != SymbolType::Empty);
            const ImmutableString &symbol = symNode->getName();
            reasonStream << " (" << message << " \"" << symbol << "\")";
        }
        else
        {
            reasonStream << " (" << message << ")";
        }
    }
    std::string reason = reasonStream.str();
    error(line, reason.c_str(), op);

    return false;
}

// Both test, and if necessary spit out an error, to see if the node is really
// a constant.
void TParseContext::checkIsConst(TIntermTyped *node)
{
    if (node->getQualifier() != EvqConst)
    {
        error(node->getLine(), "constant expression required", "");
    }
}

// Both test, and if necessary spit out an error, to see if the node is really
// an integer.
void TParseContext::checkIsScalarInteger(TIntermTyped *node, const char *token)
{
    if (!node->isScalarInt())
    {
        error(node->getLine(), "integer expression required", token);
    }
}

// Both test, and if necessary spit out an error, to see if we are currently
// globally scoped.
bool TParseContext::checkIsAtGlobalLevel(const TSourceLoc &line, const char *token)
{
    if (!symbolTable.atGlobalLevel())
    {
        error(line, "only allowed at global scope", token);
        return false;
    }
    return true;
}

void TParseContext::checkIsValidExpressionStatement(const TSourceLoc &line, TIntermTyped *expr)
{
    if (expr->isInterfaceBlock())
    {
        error(line, "expression statement is not allowed for interface blocks", "");
    }
}

// ESSL 3.00.5 sections 3.8 and 3.9.
// If it starts "gl_" or contains two consecutive underscores, it's reserved.
// Also checks for "webgl_" and "_webgl_" reserved identifiers if parsing a webgl shader.
bool TParseContext::checkIsNotReserved(const TSourceLoc &line, const ImmutableString &identifier)
{
    static const char *reservedErrMsg = "reserved built-in name";
    if (gl::IsBuiltInName(identifier.data()))
    {
        error(line, reservedErrMsg, identifier);
        return false;
    }
    if (sh::IsWebGLBasedSpec(mShaderSpec))
    {
        if (identifier.beginsWith("webgl_") || identifier.beginsWith("_webgl_"))
        {
            error(line, reservedErrMsg, identifier);
            return false;
        }
    }
    if (identifier.contains("__"))
    {
        if (sh::IsWebGLBasedSpec(mShaderSpec))
        {
            error(line,
                  "identifiers containing two consecutive underscores (__) are reserved as "
                  "possible future keywords",
                  identifier);
            return false;
        }
        else
        {
            // Using double underscores is allowed, but may result in unintended behaviors, so a
            // warning is issued.
            // OpenGL ES Shader Language 3.2 specification:
            // > 3.7. Keywords
            // > ...
            // > In addition, all identifiers containing two consecutive underscores (__) are
            // > reserved for use by underlying software layers. Defining such a name in a shader
            // > does not itself result in an error, but may result in unintended behaviors that
            // > stem from having multiple definitions of the same name.
            warning(line,
                    "all identifiers containing two consecutive underscores (__) are reserved - "
                    "unintented behaviors are possible",
                    identifier.data());
        }
    }

    // Validate that identifier names won't conflict with the name hashing done later.
    // See https://crbug.com/499176133
    if (identifier.length() > kMaxAvailableIdentifierLength && identifier[0] == '_' &&
        (identifier[1] == kUserVariableNamePrefix || identifier[1] == kUserBlockNamePrefix))
    {
        std::string err = "identifiers beginning with `_` must be <= " +
                          std::to_string(kMaxAvailableIdentifierLength) + " characters";
        error(line, err.c_str(), identifier);
        return false;
    }
    return true;
}

// Make sure the argument types are correct for constructing a specific type.
bool TParseContext::checkConstructorArguments(const TSourceLoc &line,
                                              const TIntermSequence &arguments,
                                              const TType &type)
{
    if (arguments.empty())
    {
        error(line, "constructor does not have any arguments", "constructor");
        return false;
    }

    for (TIntermNode *arg : arguments)
    {
        markStaticUseIfSymbol(arg);
        const TIntermTyped *argTyped = arg->getAsTyped();
        ASSERT(argTyped != nullptr);
        if (IsOpaqueType(argTyped->getBasicType()))
        {
            std::string reason("cannot convert a variable with type ");
            reason += getBasicString(argTyped->getBasicType());
            error(line, reason.c_str(), "constructor");
            return false;
        }
        else if (argTyped->getType().isStructureContainingSamplers())
        {
            std::string reason("cannot convert a variable with struct type containing samplers");
            error(line, reason.c_str(), "constructor");
            return false;
        }
        else if (argTyped->getMemoryQualifier().writeonly)
        {
            error(line, "cannot convert a variable with writeonly", "constructor");
            return false;
        }
        if (argTyped->getBasicType() == EbtVoid)
        {
            error(line, "cannot convert a void", "constructor");
            return false;
        }
        else if (argTyped->getBasicType() == EbtYuvCscStandardEXT)
        {
            error(line, "cannot convert a yuvCscStandardEXT", "constructor");
            return false;
        }
    }

    if (type.isArray())
    {
        // The size of an unsized constructor should already have been determined.
        ASSERT(!type.isUnsizedArray());
        if (static_cast<size_t>(type.getOutermostArraySize()) != arguments.size())
        {
            error(line, "array constructor needs one argument per array element", "constructor");
            return false;
        }
        // GLSL ES 3.00 section 5.4.4: Each argument must be the same type as the element type of
        // the array.
        for (TIntermNode *const &argNode : arguments)
        {
            const TType &argType = argNode->getAsTyped()->getType();
            if (mShaderVersion < 310 && argType.isArray())
            {
                error(line, "constructing from a non-dereferenced array", "constructor");
                return false;
            }
            if (!argType.isElementTypeOf(type))
            {
                error(line, "Array constructor argument has an incorrect type", "constructor");
                return false;
            }
        }
    }
    else if (type.getBasicType() == EbtStruct)
    {
        const TFieldList &fields = type.getStruct()->fields();
        if (fields.size() != arguments.size())
        {
            error(line,
                  "Number of constructor parameters does not match the number of structure fields",
                  "constructor");
            return false;
        }

        for (size_t i = 0; i < fields.size(); i++)
        {
            if (i >= arguments.size() ||
                arguments[i]->getAsTyped()->getType() != *fields[i]->type())
            {
                error(line, "Structure constructor arguments do not match structure fields",
                      "constructor");
                return false;
            }
        }
    }
    else
    {
        // We're constructing a scalar, vector, or matrix.

        // Note: It's okay to have too many components available, but not okay to have unused
        // arguments. 'full' will go to true when enough args have been seen. If we loop again,
        // there is an extra argument, so 'overFull' will become true.

        size_t size    = 0;
        bool full      = false;
        bool overFull  = false;
        bool matrixArg = false;
        for (TIntermNode *arg : arguments)
        {
            const TIntermTyped *argTyped = arg->getAsTyped();
            ASSERT(argTyped != nullptr);

            if (argTyped->getBasicType() == EbtStruct)
            {
                error(line, "a struct cannot be used as a constructor argument for this type",
                      "constructor");
                return false;
            }
            if (argTyped->getBasicType() == EbtInterfaceBlock)
            {
                error(line,
                      "an interface block cannot be used as a constructor argument for this type",
                      "constructor");
                return false;
            }
            if (argTyped->getType().isArray())
            {
                error(line, "constructing from a non-dereferenced array", "constructor");
                return false;
            }
            if (argTyped->getType().isMatrix())
            {
                matrixArg = true;
            }

            size += argTyped->getType().getObjectSize();
            if (full)
            {
                overFull = true;
            }
            if (size >= type.getObjectSize())
            {
                full = true;
            }
        }

        if (type.isMatrix() && matrixArg)
        {
            if (arguments.size() != 1)
            {
                error(line, "constructing matrix from matrix can only take one argument",
                      "constructor");
                return false;
            }
        }
        else
        {
            if (size != 1 && size < type.getObjectSize())
            {
                error(line, "not enough data provided for construction", "constructor");
                return false;
            }
            if (overFull)
            {
                error(line, "too many arguments", "constructor");
                return false;
            }
        }
    }

    return true;
}

// This function checks to see if a void variable has been declared and raise an error message for
// such a case
//
// returns true in case of an error
//
bool TParseContext::checkIsNonVoid(const TSourceLoc &line,
                                   const ImmutableString &identifier,
                                   const TBasicType &type)
{
    if (type == EbtVoid)
    {
        error(line, "illegal use of type 'void'", identifier);
        return false;
    }

    return true;
}

// This function checks to see if the node (for the expression) contains a scalar boolean expression
// or not.
bool TParseContext::checkIsScalarBool(const TSourceLoc &line, const TIntermTyped *type)
{
    if (type->getBasicType() != EbtBool || !type->isScalar())
    {
        error(line, "boolean expression expected", "");
        return false;
    }
    return true;
}

// This function checks to see if the node (for the expression) contains a scalar boolean expression
// or not.
void TParseContext::checkIsScalarBool(const TSourceLoc &line, const TPublicType &pType)
{
    if (pType.getBasicType() != EbtBool || pType.isAggregate())
    {
        error(line, "boolean expression expected", "");
    }
}

bool TParseContext::checkIsNotOpaqueType(const TSourceLoc &line,
                                         const TTypeSpecifierNonArray &pType,
                                         const char *reason)
{
    if (pType.type == EbtStruct)
    {
        if (ContainsOpaque<IsSamplerFunc>(pType.userDef))
        {
            std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
            reasonStream << reason << " (structure contains a sampler)";
            std::string reasonStr = reasonStream.str();
            error(line, reasonStr.c_str(), getBasicString(pType.type));
            return false;
        }
        // only samplers need to be checked from structs, since other opaque types can't be struct
        // members.
        return true;
    }
    else if (IsOpaqueType(pType.type))
    {
        error(line, reason, getBasicString(pType.type));
        return false;
    }

    return true;
}

void TParseContext::checkDeclaratorLocationIsNotSpecified(const TSourceLoc &line,
                                                          const TPublicType &pType)
{
    if (pType.layoutQualifier.location != -1)
    {
        error(line, "location must only be specified for a single input or output variable",
              "location");
    }
}

void TParseContext::checkLocationIsNotSpecified(const TSourceLoc &location,
                                                const TLayoutQualifier &layoutQualifier)
{
    if (layoutQualifier.location != -1)
    {
        const char *errorMsg = "invalid layout qualifier: only valid on program inputs and outputs";
        if (mShaderVersion >= 310)
        {
            errorMsg =
                "invalid layout qualifier: only valid on shader inputs, outputs, and uniforms";
        }
        error(location, errorMsg, "location");
    }
}

void TParseContext::checkStd430IsForShaderStorageBlock(const TSourceLoc &location,
                                                       const TLayoutBlockStorage &blockStorage,
                                                       const TQualifier &qualifier)
{
    if (blockStorage == EbsStd430 && qualifier != EvqBuffer)
    {
        error(location, "The std430 layout is supported only for shader storage blocks.", "std430");
    }
}

// Do size checking for an array type's size.
unsigned int TParseContext::checkIsValidArraySize(const TSourceLoc &line, TIntermTyped *expr)
{
    TIntermConstantUnion *constant = expr->getAsConstantUnion();

    // ANGLE should be able to fold any EvqConst expressions resulting in an integer - but to be
    // safe against corner cases we still check for constant folding. Some interpretations of the
    // spec have allowed constant expressions with side effects - like array length() method on a
    // non-constant array.
    if (expr->getQualifier() != EvqConst || constant == nullptr || !constant->isScalarInt())
    {
        error(line, "array size must be a constant integer expression", "");
        return 1u;
    }

    unsigned int size = 0u;

    if (constant->getBasicType() == EbtUInt)
    {
        size = constant->getUConst(0);
    }
    else
    {
        int signedSize = constant->getIConst(0);

        if (signedSize < 0)
        {
            error(line, "array size must be non-negative", "");
            return 1u;
        }

        size = static_cast<unsigned int>(signedSize);
    }

#ifdef ANGLE_IR
    if (mCompileOptions.useIR)
    {
        // Pop the array size from the IR too.  IR's evaluation should be equal to the AST constant
        // fold; when the AST goes away, the size as evaluated by IR is going to be used.
        const uint32_t sizeAccordingToIr = mIRBuilder.popArraySize();
        ASSERT(mDiagnostics->numErrors() != 0 || size == sizeAccordingToIr);
    }
#endif

    if (size == 0u)
    {
        error(line, "array size must be greater than zero", "");
        return 1u;
    }

    return size;
}

bool TParseContext::checkIsValidArrayDimension(const TSourceLoc &line,
                                               TVector<unsigned int> *arraySizes)
{
    if (mCompileOptions.limitExpressionComplexity &&
        arraySizes->size() > static_cast<size_t>(mResources.MaxExpressionComplexity))
    {
        error(line, "array has too many dimensions", "");
        return false;
    }
    return true;
}

// See if this qualifier can be an array.
bool TParseContext::checkIsValidQualifierForArray(const TSourceLoc &line,
                                                  const TPublicType &elementQualifier)
{
    if ((elementQualifier.qualifier == EvqAttribute) ||
        (elementQualifier.qualifier == EvqVertexIn) ||
        (elementQualifier.qualifier == EvqConst && mShaderVersion < 300))
    {
        error(line, "cannot declare arrays of this qualifier",
              TType(elementQualifier).getQualifierString());
        return false;
    }

    return true;
}

// See if this element type can be formed into an array.
bool TParseContext::checkArrayElementIsNotArray(const TSourceLoc &line,
                                                const TPublicType &elementType)
{
    if (mShaderVersion < 310 && elementType.isArray())
    {
        TInfoSinkBase typeString;
        typeString << TType(elementType);
        error(line, "cannot declare arrays of arrays", typeString.c_str());
        return false;
    }
    return true;
}

// Check for array-of-arrays being used as non-allowed shader inputs/outputs.
bool TParseContext::checkArrayOfArraysInOut(const TSourceLoc &line,
                                            const TPublicType &elementType,
                                            const TType &arrayType)
{
    if (arrayType.isArrayOfArrays())
    {
        if (elementType.qualifier == EvqVertexOut)
        {
            error(line, "vertex shader output cannot be an array of arrays",
                  TType(elementType).getQualifierString());
            return false;
        }
        if (elementType.qualifier == EvqFragmentIn)
        {
            error(line, "fragment shader input cannot be an array of arrays",
                  TType(elementType).getQualifierString());
            return false;
        }
        if (elementType.qualifier == EvqFragmentOut || elementType.qualifier == EvqFragmentInOut)
        {
            error(line, "fragment shader output cannot be an array of arrays",
                  TType(elementType).getQualifierString());
            return false;
        }
    }
    return true;
}

// Check if this qualified element type can be formed into an array. This is only called when array
// brackets are associated with an identifier in a declaration, like this:
//   float a[2];
// Similar checks are done in addFullySpecifiedType for array declarations where the array brackets
// are associated with the type, like this:
//   float[2] a;
bool TParseContext::checkIsValidTypeAndQualifierForArray(const TSourceLoc &indexLocation,
                                                         const TPublicType &elementType)
{
    if (!checkArrayElementIsNotArray(indexLocation, elementType))
    {
        return false;
    }
    // In ESSL1.00 shaders, structs cannot be varying (section 4.3.5). This is checked elsewhere.
    // In ESSL3.00 shaders, struct inputs/outputs are allowed but not arrays of structs (section
    // 4.3.4).
    // Geometry shader requires each user-defined input be declared as arrays or inside input
    // blocks declared as arrays (GL_EXT_geometry_shader section 11.1gs.4.3). For the purposes of
    // interface matching, such variables and blocks are treated as though they were not declared
    // as arrays (GL_EXT_geometry_shader section 7.4.1).
    if (mShaderVersion >= 300 && elementType.getBasicType() == EbtStruct &&
        sh::IsVarying(elementType.qualifier) &&
        !IsGeometryShaderInput(mShaderType, elementType.qualifier) &&
        !IsTessellationControlShaderInput(mShaderType, elementType.qualifier) &&
        !IsTessellationEvaluationShaderInput(mShaderType, elementType.qualifier) &&
        !IsTessellationControlShaderOutput(mShaderType, elementType.qualifier))
    {
        TInfoSinkBase typeString;
        typeString << TType(elementType);
        error(indexLocation, "cannot declare arrays of structs of this qualifier",
              typeString.c_str());
        return false;
    }

    // Support for arrays of samplerExternalOES and __samplerExternal2DY2Y are broken in some
    // backends.
    if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior &&
        (elementType.getBasicType() == EbtSamplerExternalOES ||
         elementType.getBasicType() == EbtSamplerExternal2DY2YEXT))
    {
        TInfoSinkBase typeString;
        typeString << TType(elementType);
        error(indexLocation, "arrays of external samplers are currently unsupported",
              typeString.c_str());
        return false;
    }

    return checkIsValidQualifierForArray(indexLocation, elementType);
}

void TParseContext::checkNestingLevel(const TSourceLoc &line)
{
    if (mControlFlow.size() > static_cast<size_t>(mResources.MaxStatementDepth))
    {
        error(line, "statement is too deeply nested", "");
    }
}

// Enforce non-initializer type/qualifier rules.
void TParseContext::checkCanBeDeclaredWithoutInitializer(const TSourceLoc &line,
                                                         const ImmutableString &identifier,
                                                         TType *type)
{
    ASSERT(type != nullptr);
    if (type->getQualifier() == EvqConst)
    {
        // Make the qualifier make sense.
        type->setQualifier(EvqTemporary);

        // Generate informative error messages for ESSL1.
        // In ESSL3 arrays and structures containing arrays can be constant.
        if (mShaderVersion < 300 && type->isStructureContainingArrays())
        {
            error(line,
                  "structures containing arrays may not be declared constant since they cannot be "
                  "initialized",
                  identifier);
        }
        else
        {
            error(line, "variables with qualifier 'const' must be initialized", identifier);
        }
    }
}

void TParseContext::checkDeclarationIsValidArraySize(const TSourceLoc &line,
                                                     const ImmutableString &identifier,
                                                     TType *type)
{

    // Implicitly declared arrays are only allowed with tessellation or geometry shader inputs
    if (type->isUnsizedArray() &&
        ((mShaderType != GL_TESS_CONTROL_SHADER && mShaderType != GL_TESS_EVALUATION_SHADER &&
          mShaderType != GL_GEOMETRY_SHADER) ||
         (mShaderType == GL_GEOMETRY_SHADER && type->getQualifier() == EvqGeometryOut)))
    {
        error(line,
              "implicitly sized arrays only allowed for tessellation shaders "
              "or geometry shader inputs",
              identifier);
    }
}

bool TParseContext::checkVariableSize(const TSourceLoc &line,
                                      const ImmutableString &identifier,
                                      const TType *type)
{
    // Prevent unrealistically large variable sizes in shaders.  This works around driver bugs
    // around int-size limits (such as 2GB).  The limits are generously large enough that no real
    // shader should ever hit it.
    //
    // The size check does not take std430 into account as it is intended for WebGL shaders.  For
    // the same reason, other shader stages than vertex/fragment/compute are ignored as defer-sized
    // variables e.g. in geometry shaders are not handled.
    if (!mCompileOptions.rejectWebglShadersWithLargeVariables ||
        (mShaderType != GL_VERTEX_SHADER && mShaderType != GL_FRAGMENT_SHADER &&
         mShaderType != GL_COMPUTE_SHADER))
    {
        return true;
    }

    // Check the cache of validated types first.
    size_t variableSize   = 0;
    auto preValidatedIter = mValidatedVariableTypeSizes.find(*type);
    if (preValidatedIter != mValidatedVariableTypeSizes.end())
    {
        variableSize = preValidatedIter->second;
    }
    else
    {
        // Note: the only allowed interface block in webgl shaders is UBOs in std140 mode, so the
        // size is unconditionally calculated with std140 rules if the variable is an interface
        // block. Uniform variables are treated the same way as UBOs, as they are often packed the
        // same way later on.
        variableSize = CalculateVariableSize(
                           type, type->isInterfaceBlock() || type->getQualifier() == EvqUniform)
                           .ValueOrDefault(std::numeric_limits<size_t>::max());

        mValidatedVariableTypeSizes[*type] = variableSize;
    }

    if (mResources.MaxVariableSizeInBytes && variableSize >= mResources.MaxVariableSizeInBytes)
    {
        error(line, "Size of declared variable exceeds implementation-defined limit", identifier);
        return false;
    }

    switch (type->getQualifier())
    {
        // List of all types that need to be limited (for example because they cause overflows
        // in drivers, or create trouble for the SPIR-V gen as the number of an instruction's
        // arguments cannot be more than 64KB (see OutputSPIRVTraverser::cast)).

        // Local/global variables
        case EvqTemporary:
        case EvqGlobal:
        case EvqConst:

        // Function arguments
        case EvqParamIn:
        case EvqParamOut:
        case EvqParamInOut:
        case EvqParamConst:

        // Varyings
        case EvqVaryingIn:
        case EvqVaryingOut:
        case EvqSmoothOut:
        case EvqFlatOut:
        case EvqNoPerspectiveOut:
        case EvqCentroidOut:
        case EvqSampleOut:
        case EvqNoPerspectiveCentroidOut:
        case EvqNoPerspectiveSampleOut:
        case EvqSmoothIn:
        case EvqFlatIn:
        case EvqNoPerspectiveIn:
        case EvqCentroidIn:
        case EvqNoPerspectiveCentroidIn:
        case EvqNoPerspectiveSampleIn:
        case EvqVertexOut:
        case EvqFragmentIn:
        case EvqPerVertexIn:
        case EvqPerVertexOut:

            if (mResources.MaxPrivateVariableSizeInBytes &&
                variableSize >= mResources.MaxPrivateVariableSizeInBytes)
            {
                error(line,
                      "Size of declared private variable exceeds implementation-defined limit",
                      identifier);
                return false;
            }
            mTotalPrivateVariablesSize += variableSize;
            break;
        default:
            break;
    }

    return true;
}

void TParseContext::checkVaryingLocations(const TSourceLoc &line, const TVariable *variable)
{
    // This function only checks those varyings with explicit locations for the purposes of conflict
    // detection.
    const TType &type = variable->getType();
    if (type.getLayoutQualifier().location == -1)
    {
        return;
    }

    const bool isVaryingIn  = IsVaryingIn(type.getQualifier());
    const bool isVaryingOut = IsVaryingOut(type.getQualifier());
    if (!isVaryingIn && !isVaryingOut)
    {
        return;
    }

    VariableAndField conflictingSymbol;
    const TField *conflictingFieldInNewSymbol = nullptr;

    if (!ValidateVaryingLocation(variable,
                                 isVaryingIn ? &mInputVaryingLocations : &mOutputVaryingLocations,
                                 mShaderType, &conflictingSymbol, &conflictingFieldInNewSymbol))
    {
        std::stringstream strstr = sh::InitializeStream<std::stringstream>();
        strstr << "'" << variable->name();
        if (conflictingFieldInNewSymbol != nullptr)
        {
            strstr << "." << conflictingFieldInNewSymbol->name();
        }
        strstr << "' conflicting location with '" << conflictingSymbol.variable->name();
        if (conflictingSymbol.field)
        {
            strstr << "." << conflictingSymbol.field->name();
        }
        strstr << "'";
        error(line, strstr.str().c_str(), variable->name());
    }
}

void TParseContext::checkFragmentOutputLocations(const TSourceLoc &line, const TVariable *variable)
{
    const TType &type                       = variable->getType();
    const TLayoutQualifier &layoutQualifier = type.getLayoutQualifier();
    if (type.getQualifier() != EvqFragmentOut && type.getQualifier() != EvqFragmentInOut)
    {
        return;
    }

    VariableAndLocation fragmentOutput;
    fragmentOutput.line     = line;
    fragmentOutput.variable = variable;

    // Keep track of the variables for conflict check at the end.  In particular, the limit check
    // and error messages should use either |MAX_DRAW_BUFFERS| or |MAX_DUAL_SOURCE_DRAW_BUFFERS|,
    // and that depends on whether |index=1| is used by any declaration.
    if (layoutQualifier.location != -1)
    {
        mFragmentOutputsWithLocation.push_back(fragmentOutput);
        if (layoutQualifier.index == 1)
        {
            mFragmentOutputIndex1Used = true;
        }
    }
    else if (layoutQualifier.yuv == true)
    {
        mFragmentOutputsYuv.push_back(fragmentOutput);
    }
    else
    {
        mFragmentOutputsWithoutLocation.push_back(fragmentOutput);
    }
}

void TParseContext::checkVariableLocations(const TSourceLoc &line, const TVariable *variable)
{
    // Interface variables cannot be declared inside functions.
    if (mCurrentFunction != nullptr || variable->symbolType() == SymbolType::Empty)
    {
        return;
    }

    // In ESSL 310, the shader may assign explicit locations to varyings, which need to be checked
    // for conflicts.
    if (mShaderVersion >= 310)
    {
        checkVaryingLocations(line, variable);
    }

    // In ESSL 300, the shader may assign explicit locations to fragment outputs, which need to be
    // checked for conflicts.
    if (mShaderVersion >= 300 && mShaderType == GL_FRAGMENT_SHADER)
    {
        checkFragmentOutputLocations(line, variable);
    }
}

// Do some simple checks that are shared between all variable declarations,
// and update the symbol table.
//
// Returns true if declaring the variable succeeded.
//
bool TParseContext::declareVariable(const TSourceLoc &line,
                                    const ImmutableString &identifier,
                                    const TType *declarationType,
                                    GeomTessArray sized,
                                    TVariable **variable)
{
    ASSERT(*variable == nullptr);

    // When gl_Position and gl_PointSize are redeclared per EXT_separate_shader_objects, make sure
    // they have the right qualifier.
    const TType *type = declarationType;
    if (identifier == "gl_Position" || identifier == "gl_PointSize")
    {
        TType *fixedType = new TType(*type);
        fixedType->setQualifier(identifier == "gl_Position" ? EvqPosition : EvqPointSize);
        fixedType->setTypeId(type->typeId());
        type = fixedType;
    }

    SymbolType symbolType = SymbolType::UserDefined;
    switch (type->getQualifier())
    {
        case EvqPosition:
        case EvqPointSize:
        case EvqClipDistance:
        case EvqCullDistance:
        case EvqFragDepth:
        case EvqLastFragData:
        case EvqLastFragColor:
        case EvqLastFragDepth:
        case EvqLastFragStencil:
            symbolType = SymbolType::BuiltIn;

            if (mBuiltInQualified[type->getQualifier()])
            {
                error(
                    line,
                    "built-ins cannot be redeclared after being qualified as invariant or precise",
                    identifier);
            }
            break;
        default:
            break;
    }

    // If this is a nameless struct, then either the variable is a shader input/output or not.  In
    // the former case, remove the struct from mNamelessStructs and let it be declared together
    // with the variable.  This is needed to support shader linking.  In the latter case, make the
    // type not a struct specifier and let the struct be separately declared.
    if (type->getStruct() != nullptr)
    {
        auto it = std::find(mNamelessStructs.begin(), mNamelessStructs.end(), type->getStruct());
        if (it != mNamelessStructs.end())
        {
            if (IsShaderIn(type->getQualifier()) || IsShaderOut(type->getQualifier()))
            {
                mNamelessStructs.erase(it);
            }
            else
            {
                TType *newType = new TType(*type);
                newType->removeStructSpecifier();
                newType->setTypeId(type->typeId());
                type = newType;
            }
        }
    }

    *variable = new TVariable(&symbolTable, identifier, type, symbolType);

    if (type->getQualifier() == EvqFragmentOut)
    {
        if (type->getLayoutQualifier().index != -1 && type->getLayoutQualifier().location == -1)
        {
            error(line,
                  "If index layout qualifier is specified for a fragment output, location must "
                  "also be specified.",
                  "index");
            return false;
        }
    }
    else
    {
        checkIndexIsNotSpecified(line, type->getLayoutQualifier().index);
    }

    if (!((identifier.beginsWith("gl_LastFragData") || type->getQualifier() == EvqFragmentInOut) &&
          (isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch) ||
           isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch_non_coherent))) &&
        !(type->isPixelLocal() && isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage)))
    {
        checkNoncoherentIsNotSpecified(line, type->getLayoutQualifier().noncoherent);
    }
    else if (isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch_non_coherent) &&
             !isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch))
    {
        checkNoncoherentIsSpecified(line, type->getLayoutQualifier().noncoherent);
    }

    checkBindingIsValid(line, *type);

    bool needsReservedCheck = true;
    const TVariable *builtInSymbol =
        static_cast<const TVariable *>(symbolTable.findBuiltIn(identifier, mShaderVersion));

    // Some built-ins may be redeclared with a new precision qualifier, but must otherwise match the
    // built-in in type, array dimensions etc: gl_LastFragData, gl_LastFragColorARM,
    // gl_LastFragDepthARM, gl_LastFragStencilARM, gl_ClipDistance, gl_CullDistance, gl_FragDepth,
    // gl_Position, gl_PointSize.
    //
    // For gl_ClipDistance and gl_CullDistance, the array size can be less than the built-in's.
    if (builtInSymbol != nullptr)
    {
        const TType &expectedType = builtInSymbol->getType();

        uint32_t expectedArraySize         = 0;
        bool canArraySizeBeLessThanBuiltIn = false;
        const char *arraySizeCheckError    = nullptr;

        switch (expectedType.getQualifier())
        {
            case EvqLastFragData:
                expectedArraySize = static_cast<const TVariable *>(
                                        symbolTable.findBuiltIn(
                                            ImmutableString("gl_MaxDrawBuffers"), mShaderVersion))
                                        ->getConstPointer()
                                        ->getIConst();
                arraySizeCheckError =
                    "redeclaration of gl_LastFragData with size != gl_MaxDrawBuffers";
                needsReservedCheck = !checkCanUseOneOfExtensions(line, builtInSymbol->extensions());
                break;
            case EvqLastFragColor:
            case EvqLastFragDepth:
            case EvqLastFragStencil:
                needsReservedCheck = !checkCanUseOneOfExtensions(line, builtInSymbol->extensions());
                break;
            case EvqClipDistance:
            {
                const TVariable *maxClipDistances =
                    static_cast<const TVariable *>(symbolTable.findBuiltIn(
                        ImmutableString("gl_MaxClipDistances"), mShaderVersion));
                if (maxClipDistances != nullptr)
                {
                    expectedArraySize = maxClipDistances->getConstPointer()->getIConst();
                    canArraySizeBeLessThanBuiltIn = true;
                    arraySizeCheckError =
                        "redeclaration of gl_ClipDistance with size > gl_MaxClipDistances";
                    needsReservedCheck =
                        !checkCanUseOneOfExtensions(line, builtInSymbol->extensions());
                }
                else
                {
                    // Unsupported extension
                    error(line, "redeclaration of built-in is not allowed", identifier);
                    return false;
                }
                break;
            }
            case EvqCullDistance:
            {
                const TVariable *maxCullDistances =
                    static_cast<const TVariable *>(symbolTable.findBuiltIn(
                        ImmutableString("gl_MaxCullDistances"), mShaderVersion));
                if (maxCullDistances != nullptr)
                {
                    expectedArraySize = maxCullDistances->getConstPointer()->getIConst();
                    canArraySizeBeLessThanBuiltIn = true;
                    arraySizeCheckError =
                        "redeclaration of gl_CullDistance with size > gl_MaxCullDistances";
                    needsReservedCheck =
                        !checkCanUseOneOfExtensions(line, builtInSymbol->extensions());
                }
                else
                {
                    // Unsupported extension
                    error(line, "redeclaration of built-in is not allowed", identifier);
                    return false;
                }
                break;
            }
            case EvqFragDepth:
                needsReservedCheck = !isExtensionEnabled(TExtension::EXT_conservative_depth) ||
                                     mShaderType != GL_FRAGMENT_SHADER ||
                                     symbolType == SymbolType::UserDefined;
                break;
            case EvqPosition:
            case EvqPointSize:
                if (isExtensionEnabled(TExtension::EXT_separate_shader_objects) &&
                    mShaderType == GL_VERTEX_SHADER)
                {
                    needsReservedCheck = false;
                    if (expectedType.getQualifier() == EvqPosition)
                    {
                        mPositionRedeclaredForSeparateShaderObject = true;
                    }
                    else
                    {
                        mPointSizeRedeclaredForSeparateShaderObject = true;
                    }
                    if (mPositionOrPointSizeUsedForSeparateShaderObject)
                    {
                        error(line,
                              "When EXT_separate_shader_objects is enabled, both gl_Position and "
                              "gl_PointSize must be redeclared before either is used",
                              identifier);
                    }
                }
                else
                {
                    error(line, "redeclaration of built-in is not allowed", identifier);
                    return false;
                }
                break;
            default:
                error(line, "reserved built-in name", identifier);
                return false;
        }

        // No built-in is an array of arrays.
        if (type->isArrayOfArrays())
        {
            error(line, "redeclaration of built-in as an array of arrays", identifier);
            return false;
        }

        if (type->getBasicType() != expectedType.getBasicType() ||
            type->getNominalSize() != expectedType.getNominalSize() ||
            type->getSecondarySize() != expectedType.getSecondarySize() ||
            type->isArray() != expectedType.isArray())
        {
            error(line, "redeclaration of built-in with a different type", identifier);
            return false;
        }

        if (type->isArray())
        {
            unsigned int arraySize = type->getOutermostArraySize();
            if (arraySize > expectedArraySize ||
                (!canArraySizeBeLessThanBuiltIn && arraySize != expectedArraySize))
            {
                error(line, arraySizeCheckError, identifier);
                return false;
            }
        }

        // Record the redeclared size of gl_Clip/CullDistance.  Do not allow redeclaration after
        // these built-ins are already referenced to avoid having to fix the AST after the fact.
        // With IR, this could be more easily supported if needed.
        switch (expectedType.getQualifier())
        {
            case EvqClipDistance:
                if (IsClipCullEncountered(mClipDistanceInfo))
                {
                    error(line,
                          "redeclaration of gl_ClipDistance after it is referenced is not allowed",
                          identifier);
                    return false;
                }
                MarkClipCullRedeclaredSize(line, type->getOutermostArraySize(), &mClipDistanceInfo);
                break;
            case EvqCullDistance:
                if (IsClipCullEncountered(mCullDistanceInfo))
                {
                    error(line,
                          "redeclaration of gl_CullDistance after it is referenced is not allowed",
                          identifier);
                    return false;
                }
                MarkClipCullRedeclaredSize(line, type->getOutermostArraySize(), &mCullDistanceInfo);
                break;
            default:
                break;
        }
    }

    if (needsReservedCheck && !checkIsNotReserved(line, identifier))
    {
        return false;
    }

    if (!symbolTable.declare(*variable))
    {
        error(line, "redefinition", identifier);
        return false;
    }
    addAndCheckOutputVaryings(**variable, line);

    if (!checkIsNonVoid(line, identifier, type->getBasicType()))
    {
        return false;
    }

    if (!checkVariableSize(line, identifier, type))
    {
        return false;
    }
    checkVariableLocations(line, *variable);

    // Declare the variable in IR
    declareIRVariable(*variable, sized);

    return true;
}

void TParseContext::declareIRVariable(const TVariable *variable, GeomTessArray sized)
{
    // If the variable is yet to be sized, don't declare it yet.
    if (sized == GeomTessArray::Deferred)
    {
        mDeferredArrayVariablesToSize.push_back(variable);
        return;
    }

    const TType &type = variable->getType();
    ASSERT(type.isTypeIdSet());

    ir::VariableId variableId;
    switch (type.getQualifier())
    {
        case EvqTemporary:
        case EvqGlobal:
        case EvqConst:
        case EvqParamIn:
        case EvqParamOut:
        case EvqParamInOut:
        case EvqParamConst:
            // Temporary variables
            variableId = mIRBuilder.declareTempVariable(variable->name(), type.typeId(), type);
            break;
        default:
            // Interface and built-in variables
            variableId = mIRBuilder.declareInterfaceVariable(
                variable->symbolType() == SymbolType::Empty ? kEmptyImmutableString
                                                            : variable->name(),
                type.typeId(), type, ir::DeclarationSource::Shader);
            break;
    }
    mVariableToId[variable] = VariableToIdInfo{variableId, VariableToIdInfo::kNoImplicitField};
}

void TParseContext::parseParameterQualifier(const TSourceLoc &line,
                                            const TTypeQualifierBuilder &typeQualifierBuilder,
                                            TPublicType &type)
{
    // The only parameter qualifiers a parameter can have are in, out, inout or const.
    TTypeQualifier typeQualifier =
        typeQualifierBuilder.getParameterTypeQualifier(type.getBasicType(), mDiagnostics);

    if (typeQualifier.qualifier == EvqParamOut || typeQualifier.qualifier == EvqParamInOut)
    {
        if (IsOpaqueType(type.getBasicType()))
        {
            error(line, "opaque types cannot be output parameters", type.getBasicString());
        }
    }

    if (!IsImage(type.getBasicType()))
    {
        checkMemoryQualifierIsNotSpecified(typeQualifier.memoryQualifier, line);
    }
    else
    {
        type.setMemoryQualifier(typeQualifier.memoryQualifier);
    }

    type.setQualifier(typeQualifier.qualifier);

    if (typeQualifier.precision != EbpUndefined)
    {
        type.setPrecision(typeQualifier.precision);
    }

    if (typeQualifier.precise)
    {
        type.setPrecise(true);
    }
}

void TParseContext::addParameter(TFunction *function, TParameter *param)
{
    const TVariable *variable = param->createVariable(&symbolTable);
    function->addParameter(variable);
}

template <size_t size>
bool TParseContext::checkCanUseOneOfExtensions(const TSourceLoc &line,
                                               const std::array<TExtension, size> &extensions)
{
    ASSERT(!extensions.empty());
    const TExtensionBehavior &extBehavior = extensionBehavior();

    bool canUseWithWarning    = false;
    bool canUseWithoutWarning = false;

    const char *errorMsgString   = "";
    TExtension errorMsgExtension = TExtension::UNDEFINED;

    for (TExtension extension : extensions)
    {
        auto extIter = extBehavior.find(extension);
        if (canUseWithWarning)
        {
            // We already have an extension that we can use, but with a warning.
            // See if we can use the alternative extension without a warning.
            if (extIter == extBehavior.end())
            {
                continue;
            }
            if (extIter->second == EBhEnable || extIter->second == EBhRequire)
            {
                canUseWithoutWarning = true;
                break;
            }
            continue;
        }
        if (extension == TExtension::UNDEFINED)
        {
            continue;
        }
        else if (extIter == extBehavior.end())
        {
            errorMsgString    = "extension is not supported";
            errorMsgExtension = extension;
        }
        else if (extIter->second == EBhUndefined || extIter->second == EBhDisable)
        {
            errorMsgString    = "extension is disabled";
            errorMsgExtension = extension;
        }
        else if (extIter->second == EBhWarn)
        {
            errorMsgExtension = extension;
            canUseWithWarning = true;
        }
        else
        {
            ASSERT(extIter->second == EBhEnable || extIter->second == EBhRequire);
            canUseWithoutWarning = true;
            break;
        }
    }

    if (canUseWithoutWarning)
    {
        return true;
    }
    if (canUseWithWarning)
    {
        warning(line, "extension is being used", GetExtensionNameString(errorMsgExtension));
        return true;
    }
    error(line, errorMsgString, GetExtensionNameString(errorMsgExtension));
    return false;
}

template bool TParseContext::checkCanUseOneOfExtensions(
    const TSourceLoc &line,
    const std::array<TExtension, 1> &extensions);
template bool TParseContext::checkCanUseOneOfExtensions(
    const TSourceLoc &line,
    const std::array<TExtension, 2> &extensions);
template bool TParseContext::checkCanUseOneOfExtensions(
    const TSourceLoc &line,
    const std::array<TExtension, 3> &extensions);

bool TParseContext::checkCanUseExtension(const TSourceLoc &line, TExtension extension)
{
    ASSERT(extension != TExtension::UNDEFINED);
    return checkCanUseOneOfExtensions(line, std::array<TExtension, 1u>{{extension}});
}

// ESSL 3.00.6 section 4.8 Empty Declarations: "The combinations of qualifiers that cause
// compile-time or link-time errors are the same whether or not the declaration is empty".
// This function implements all the checks that are done on qualifiers regardless of if the
// declaration is empty.
void TParseContext::declarationQualifierErrorCheck(const sh::TQualifier qualifier,
                                                   const sh::TLayoutQualifier &layoutQualifier,
                                                   const TSourceLoc &location)
{
    if (qualifier == EvqShared && !layoutQualifier.isEmpty())
    {
        error(location, "Shared memory declarations cannot have layout specified", "layout");
    }

    if (layoutQualifier.matrixPacking != EmpUnspecified)
    {
        error(location, "layout qualifier only valid for interface blocks",
              getMatrixPackingString(layoutQualifier.matrixPacking));
        return;
    }

    if (layoutQualifier.blockStorage != EbsUnspecified)
    {
        error(location, "layout qualifier only valid for interface blocks",
              getBlockStorageString(layoutQualifier.blockStorage));
        return;
    }

    if (qualifier != EvqFragDepth)
    {
        checkDepthIsNotSpecified(location, layoutQualifier.depth);
    }

    if (qualifier == EvqFragmentOut)
    {
        if (layoutQualifier.location != -1 && layoutQualifier.yuv == true)
        {
            error(location, "invalid layout qualifier combination", "yuv");
            return;
        }
    }
    else
    {
        checkYuvIsNotSpecified(location, layoutQualifier.yuv);
    }

    if (qualifier != EvqFragmentIn)
    {
        checkEarlyFragmentTestsIsNotSpecified(location, layoutQualifier.earlyFragmentTests);
    }

    // If multiview extension is enabled, "in" qualifier is allowed in the vertex shader in previous
    // parsing steps. So it needs to be checked here.
    if (anyMultiviewExtensionAvailable() && mShaderVersion < 300 && qualifier == EvqVertexIn)
    {
        error(location, "storage qualifier supported in GLSL ES 3.00 and above only", "in");
    }

    bool canHaveLocation = qualifier == EvqVertexIn || qualifier == EvqFragmentOut;
    if (mShaderVersion >= 300 &&
        (isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch) ||
         isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch_non_coherent)))
    {
        // In the case of EXT_shader_framebuffer_fetch or EXT_shader_framebuffer_fetch_non_coherent
        // extension, the location of inout qualifier is used to set the input attachment index
        canHaveLocation = canHaveLocation || qualifier == EvqFragmentInOut;
    }
    if (mShaderVersion >= 310)
    {
        canHaveLocation = canHaveLocation || qualifier == EvqUniform || IsVarying(qualifier);
        // We're not checking whether the uniform location is in range here since that depends on
        // the type of the variable.
        // The type can only be fully determined for non-empty declarations.
    }
    if (!canHaveLocation)
    {
        checkLocationIsNotSpecified(location, layoutQualifier);
    }
}

void TParseContext::atomicCounterQualifierErrorCheck(const TPublicType &publicType,
                                                     const TSourceLoc &location)
{
    if (publicType.precision != EbpHigh)
    {
        error(location, "Can only be highp", "atomic counter");
    }
    // dEQP enforces compile error if location is specified. See uniform_location.test.
    if (publicType.layoutQualifier.location != -1)
    {
        error(location, "location must not be set for atomic_uint", "layout");
    }
    if (publicType.layoutQualifier.binding == -1)
    {
        error(location, "no binding specified", "atomic counter");
    }
}

void TParseContext::emptyDeclarationErrorCheck(const TType &type, const TSourceLoc &location)
{
    if (type.isUnsizedArray())
    {
        // ESSL3 spec section 4.1.9: Array declaration which leaves the size unspecified is an
        // error. It is assumed that this applies to empty declarations as well.
        error(location, "empty array declaration needs to specify a size", "");
    }

    if (type.getQualifier() != EvqFragmentOut)
    {
        checkIndexIsNotSpecified(location, type.getLayoutQualifier().index);
    }
}

// These checks are done for all declarations that are non-empty. They're done for non-empty
// declarations starting a declarator list, and declarators that follow an empty declaration.
void TParseContext::nonEmptyDeclarationErrorCheck(const TPublicType &publicType,
                                                  const TSourceLoc &identifierLocation)
{
    switch (publicType.qualifier)
    {
        case EvqVaryingIn:
        case EvqVaryingOut:
        case EvqAttribute:
        case EvqVertexIn:
        case EvqFragmentOut:
        case EvqFragmentInOut:
        case EvqComputeIn:
            if (publicType.getBasicType() == EbtStruct)
            {
                error(identifierLocation, "cannot be used with a structure",
                      getQualifierString(publicType.qualifier));
                return;
            }
            break;
        case EvqBuffer:
            if (publicType.getBasicType() != EbtInterfaceBlock)
            {
                error(identifierLocation,
                      "cannot declare buffer variables at global scope(outside a block)",
                      getQualifierString(publicType.qualifier));
                return;
            }
            break;
        default:
            break;
    }
    std::string reason(getBasicString(publicType.getBasicType()));
    reason += "s must be uniform";
    if (publicType.qualifier != EvqUniform &&
        !checkIsNotOpaqueType(identifierLocation, publicType.typeSpecifierNonArray, reason.c_str()))
    {
        return;
    }

    if ((publicType.qualifier != EvqTemporary && publicType.qualifier != EvqGlobal &&
         publicType.qualifier != EvqConst) &&
        publicType.getBasicType() == EbtYuvCscStandardEXT)
    {
        error(identifierLocation, "cannot be used with a yuvCscStandardEXT",
              getQualifierString(publicType.qualifier));
        return;
    }

    if (mShaderVersion >= 310 && publicType.qualifier == EvqUniform)
    {
        // Valid uniform declarations can't be unsized arrays since uniforms can't be initialized.
        // But invalid shaders may still reach here with an unsized array declaration.
        TType type(publicType);
        if (!type.isUnsizedArray())
        {
            checkUniformLocationInRange(identifierLocation, type.getLocationCount(),
                                        publicType.layoutQualifier);
        }
    }

    if (mShaderVersion >= 300 && publicType.qualifier == EvqVertexIn)
    {
        // Valid vertex input declarations can't be unsized arrays since they can't be initialized.
        // But invalid shaders may still reach here with an unsized array declaration.
        TType type(publicType);
        if (!type.isUnsizedArray())
        {
            checkAttributeLocationInRange(identifierLocation, type.getLocationCount(),
                                          publicType.layoutQualifier);
        }
    }

    // check for layout qualifier issues
    const TLayoutQualifier layoutQualifier = publicType.layoutQualifier;

    if (IsImage(publicType.getBasicType()))
    {

        switch (layoutQualifier.imageInternalFormat)
        {
            case EiifRGBA32F:
            case EiifRGBA16F:
            case EiifR32F:
            case EiifRGBA8:
            case EiifRGBA8_SNORM:
                if (!IsFloatImage(publicType.getBasicType()))
                {
                    error(identifierLocation,
                          "internal image format requires a floating image type",
                          getBasicString(publicType.getBasicType()));
                    return;
                }
                break;
            case EiifRGBA32I:
            case EiifRGBA16I:
            case EiifRGBA8I:
            case EiifR32I:
                if (!IsIntegerImage(publicType.getBasicType()))
                {
                    error(identifierLocation,
                          "internal image format requires an integer image type",
                          getBasicString(publicType.getBasicType()));
                    return;
                }
                break;
            case EiifRGBA32UI:
            case EiifRGBA16UI:
            case EiifRGBA8UI:
            case EiifR32UI:
                if (!IsUnsignedImage(publicType.getBasicType()))
                {
                    error(identifierLocation,
                          "internal image format requires an unsigned image type",
                          getBasicString(publicType.getBasicType()));
                    return;
                }
                break;
            case EiifUnspecified:
                error(identifierLocation, "layout qualifier", "No image internal format specified");
                return;
            default:
                error(identifierLocation, "layout qualifier", "unrecognized token");
                return;
        }

        // GLSL ES 3.10 Revision 4, 4.9 Memory Access Qualifiers
        switch (layoutQualifier.imageInternalFormat)
        {
            case EiifR32F:
            case EiifR32I:
            case EiifR32UI:
                break;
            default:
                if (!publicType.memoryQualifier.readonly && !publicType.memoryQualifier.writeonly)
                {
                    error(identifierLocation, "layout qualifier",
                          "Except for images with the r32f, r32i and r32ui format qualifiers, "
                          "image variables must be qualified readonly and/or writeonly");
                    return;
                }
                break;
        }
    }
    else if (IsPixelLocal(publicType.getBasicType()))
    {
        if (getShaderType() != GL_FRAGMENT_SHADER)
        {
            error(identifierLocation,
                  "undefined use of pixel local storage outside a fragment shader",
                  getBasicString(publicType.getBasicType()));
            return;
        }
        switch (layoutQualifier.imageInternalFormat)
        {
            case EiifR32F:
            case EiifRGBA8:
                if (publicType.getBasicType() != EbtPixelLocalANGLE)
                {
                    error(identifierLocation, "pixel local storage format requires pixelLocalANGLE",
                          getImageInternalFormatString(layoutQualifier.imageInternalFormat));
                }
                break;
            case EiifR32I:
            case EiifRGBA8I:
                if (publicType.getBasicType() != EbtIPixelLocalANGLE)
                {
                    error(identifierLocation,
                          "pixel local storage format requires ipixelLocalANGLE",
                          getImageInternalFormatString(layoutQualifier.imageInternalFormat));
                }
                break;
            case EiifR32UI:
            case EiifRGBA8UI:
                if (publicType.getBasicType() != EbtUPixelLocalANGLE)
                {
                    error(identifierLocation,
                          "pixel local storage format requires upixelLocalANGLE",
                          getImageInternalFormatString(layoutQualifier.imageInternalFormat));
                }
                break;
            case EiifRGBA8_SNORM:
            case EiifRGBA16F:
            case EiifRGBA32F:
            case EiifRGBA16I:
            case EiifRGBA32I:
            case EiifRGBA16UI:
            case EiifRGBA32UI:
            default:
                ASSERT(!IsValidWithPixelLocalStorage(layoutQualifier.imageInternalFormat));
                error(identifierLocation, "illegal pixel local storage format",
                      getImageInternalFormatString(layoutQualifier.imageInternalFormat));
                break;
            case EiifUnspecified:
                error(identifierLocation, "pixel local storage requires a format specifier",
                      "layout qualifier");
                break;
        }
        checkMemoryQualifierIsNotSpecified(publicType.memoryQualifier, identifierLocation);
        checkDeclaratorLocationIsNotSpecified(identifierLocation, publicType);
    }
    else
    {
        checkInternalFormatIsNotSpecified(identifierLocation, layoutQualifier.imageInternalFormat);
        checkMemoryQualifierIsNotSpecified(publicType.memoryQualifier, identifierLocation);
    }

    if (IsAtomicCounter(publicType.getBasicType()))
    {
        atomicCounterQualifierErrorCheck(publicType, identifierLocation);
    }
    else
    {
        checkOffsetIsNotSpecified(identifierLocation, layoutQualifier.offset);
    }
}

void TParseContext::checkBindingIsValid(const TSourceLoc &identifierLocation, const TType &type)
{
    TLayoutQualifier layoutQualifier = type.getLayoutQualifier();
    // Note that the ESSL 3.10 section 4.4.5 is not particularly clear on how the binding qualifier
    // on arrays of arrays should be handled. We interpret the spec so that the binding value is
    // incremented for each element of the innermost nested arrays. This is in line with how arrays
    // of arrays of blocks are specified to behave in GLSL 4.50 and a conservative interpretation
    // when it comes to which shaders are accepted by the compiler.
    int arrayTotalElementCount = type.getArraySizeProduct();
    if (IsPixelLocal(type.getBasicType()))
    {
        checkPixelLocalStorageBindingIsValid(identifierLocation, type);
    }
    else if (mShaderVersion < 310)
    {
        checkBindingIsNotSpecified(identifierLocation, layoutQualifier.binding);
    }
    else if (IsImage(type.getBasicType()))
    {
        checkImageBindingIsValid(identifierLocation, layoutQualifier.binding,
                                 arrayTotalElementCount);
    }
    else if (IsSampler(type.getBasicType()))
    {
        checkSamplerBindingIsValid(identifierLocation, layoutQualifier.binding,
                                   arrayTotalElementCount);
    }
    else if (IsAtomicCounter(type.getBasicType()))
    {
        checkAtomicCounterBindingIsValid(identifierLocation, layoutQualifier.binding);
    }
    else
    {
        ASSERT(!IsOpaqueType(type.getBasicType()));
        checkBindingIsNotSpecified(identifierLocation, layoutQualifier.binding);
    }
}

void TParseContext::checkCanUseLayoutQualifier(const TSourceLoc &location)
{
    constexpr std::array<TExtension, 4u> extensions{
        {TExtension::EXT_shader_framebuffer_fetch,
         TExtension::EXT_shader_framebuffer_fetch_non_coherent,
         TExtension::KHR_blend_equation_advanced, TExtension::ANGLE_shader_pixel_local_storage}};
    if (getShaderVersion() < 300 && !checkCanUseOneOfExtensions(location, extensions))
    {
        error(location, "qualifier supported in GLSL ES 3.00 and above only", "layout");
    }
}

bool TParseContext::checkLayoutQualifierSupported(const TSourceLoc &location,
                                                  const ImmutableString &layoutQualifierName,
                                                  int versionRequired)
{

    if (mShaderVersion < versionRequired)
    {
        error(location, "invalid layout qualifier: not supported", layoutQualifierName);
        return false;
    }
    return true;
}

bool TParseContext::checkWorkGroupSizeIsNotSpecified(const TSourceLoc &location,
                                                     const TLayoutQualifier &layoutQualifier)
{
    const sh::WorkGroupSize &localSize = layoutQualifier.localSize;
    for (size_t i = 0u; i < localSize.size(); ++i)
    {
        if (localSize[i] != -1)
        {
            error(location,
                  "invalid layout qualifier: only valid when used with 'in' in a compute shader "
                  "global layout declaration",
                  getWorkGroupSizeString(i));
            return false;
        }
    }

    return true;
}

void TParseContext::checkInternalFormatIsNotSpecified(const TSourceLoc &location,
                                                      TLayoutImageInternalFormat internalFormat)
{
    if (internalFormat != EiifUnspecified)
    {
        if (mShaderVersion < 310)
        {
            if (IsValidWithPixelLocalStorage(internalFormat))
            {
                error(location,
                      "invalid layout qualifier: not supported before GLSL ES 3.10, except pixel "
                      "local storage",
                      getImageInternalFormatString(internalFormat));
            }
            else
            {
                error(location, "invalid layout qualifier: not supported before GLSL ES 3.10",
                      getImageInternalFormatString(internalFormat));
            }
        }
        else
        {
            if (IsValidWithPixelLocalStorage(internalFormat))
            {
                error(location,
                      "invalid layout qualifier: only valid when used with images or pixel local "
                      "storage ",
                      getImageInternalFormatString(internalFormat));
            }
            else
            {
                error(location, "invalid layout qualifier: only valid when used with images",
                      getImageInternalFormatString(internalFormat));
            }
        }
    }
}

void TParseContext::checkIndexIsNotSpecified(const TSourceLoc &location, int index)
{
    if (index != -1)
    {
        error(location,
              "invalid layout qualifier: only valid when used with a fragment shader output in "
              "ESSL version >= 3.00 and EXT_blend_func_extended is enabled",
              "index");
    }
}

void TParseContext::checkBindingIsNotSpecified(const TSourceLoc &location, int binding)
{
    if (binding != -1)
    {
        if (mShaderVersion < 310)
        {
            error(location,
                  "invalid layout qualifier: only valid when used with pixel local storage",
                  "binding");
        }
        else
        {
            error(location,
                  "invalid layout qualifier: only valid when used with opaque types or blocks",
                  "binding");
        }
    }
}

void TParseContext::checkOffsetIsNotSpecified(const TSourceLoc &location, int offset)
{
    if (offset != -1)
    {
        error(location, "invalid layout qualifier: only valid when used with atomic counters",
              "offset");
    }
}

void TParseContext::checkImageBindingIsValid(const TSourceLoc &location,
                                             int binding,
                                             int arrayTotalElementCount)
{
    // Expects arraySize to be 1 when setting binding for only a single variable.
    if (binding >= 0 && binding + arrayTotalElementCount > mResources.MaxImageUnits)
    {
        error(location, "image binding greater than gl_MaxImageUnits", "binding");
    }
}

void TParseContext::checkSamplerBindingIsValid(const TSourceLoc &location,
                                               int binding,
                                               int arrayTotalElementCount)
{
    // Expects arraySize to be 1 when setting binding for only a single variable.
    if (binding >= 0 && binding + arrayTotalElementCount > mResources.MaxCombinedTextureImageUnits)
    {
        error(location, "sampler binding greater than maximum texture units", "binding");
    }
}

void TParseContext::checkBlockBindingIsValid(const TSourceLoc &location,
                                             const TQualifier &qualifier,
                                             int binding,
                                             int arraySize)
{
    int size = (arraySize == 0 ? 1 : arraySize);
    if (qualifier == EvqUniform)
    {
        if (binding + size > mResources.MaxUniformBufferBindings)
        {
            error(location, "uniform block binding greater than MAX_UNIFORM_BUFFER_BINDINGS",
                  "binding");
        }
    }
    else if (qualifier == EvqBuffer)
    {
        if (binding + size > mResources.MaxShaderStorageBufferBindings)
        {
            error(location,
                  "shader storage block binding greater than MAX_SHADER_STORAGE_BUFFER_BINDINGS",
                  "binding");
        }
    }
}
void TParseContext::checkAtomicCounterBindingIsValid(const TSourceLoc &location, int binding)
{
    if (binding >= mResources.MaxAtomicCounterBindings)
    {
        error(location, "atomic counter binding greater than gl_MaxAtomicCounterBindings",
              "binding");
    }
}

void TParseContext::checkPixelLocalStorageBindingIsValid(const TSourceLoc &location,
                                                         const TType &type)
{
    TLayoutQualifier layoutQualifier = type.getLayoutQualifier();
    if (type.isArray())
    {
        // PLS is not allowed in arrays.
        // TODO(anglebug.com/40096838): Consider allowing this once more backends are implemented.
        error(location, "pixel local storage handles cannot be aggregated in arrays", "array");
    }
    else if (layoutQualifier.binding < 0)
    {
        error(location, "pixel local storage requires a binding index", "layout qualifier");
    }
    // TODO(anglebug.com/40096838):
    else if (layoutQualifier.binding >= mResources.MaxPixelLocalStoragePlanes)
    {
        error(location, "pixel local storage binding out of range", "layout qualifier");
    }
    else if (mPLSLayouts.find(layoutQualifier.binding) != mPLSLayouts.end())
    {
        error(location, "duplicate pixel local storage binding index",
              std::to_string(layoutQualifier.binding).c_str());
    }
    else
    {
        mPLSLayouts[layoutQualifier.binding] = {
            .format      = ImageFormatToPLSFormat(layoutQualifier.imageInternalFormat),
            .noncoherent = layoutQualifier.noncoherent,
        };
        // "mPLSLayouts" is how we know whether any pixel local storage uniforms have been declared,
        // so flush the queue of potential errors once mPLSLayouts isn't empty.
        if (!mPLSPotentialErrors.empty())
        {
            for (const auto &[loc, op] : mPLSPotentialErrors)
            {
                errorIfPLSDeclared(loc, op);
            }
            mPLSPotentialErrors.clear();
        }
    }
}

void TParseContext::checkUniformLocationInRange(const TSourceLoc &location,
                                                int objectLocationCount,
                                                const TLayoutQualifier &layoutQualifier)
{
    int loc = layoutQualifier.location;
    if (loc >= 0)  // Shader-specified location
    {
        if (loc >= mResources.MaxUniformLocations ||
            objectLocationCount > mResources.MaxUniformLocations ||
            static_cast<unsigned int>(loc) + static_cast<unsigned int>(objectLocationCount) >
                static_cast<unsigned int>(mResources.MaxUniformLocations))
        {
            error(location, "Uniform location out of range", "location");
        }
    }
}

void TParseContext::checkAttributeLocationInRange(const TSourceLoc &location,
                                                  int objectLocationCount,
                                                  const TLayoutQualifier &layoutQualifier)
{
    int loc = layoutQualifier.location;
    if (loc >= 0)  // Shader-specified location
    {
        if (loc >= mResources.MaxVertexAttribs ||
            objectLocationCount > mResources.MaxVertexAttribs ||
            static_cast<unsigned int>(loc) + static_cast<unsigned int>(objectLocationCount) >
                static_cast<unsigned int>(mResources.MaxVertexAttribs))
        {
            error(location, "Attribute location out of range", "location");
        }
    }
}

void TParseContext::checkDepthIsNotSpecified(const TSourceLoc &location, TLayoutDepth depth)
{
    if (depth != EdUnspecified)
    {
        error(location, "invalid layout qualifier: only valid on gl_FragDepth",
              getDepthString(depth));
    }
}

void TParseContext::checkYuvIsNotSpecified(const TSourceLoc &location, bool yuv)
{
    if (yuv != false)
    {
        error(location, "invalid layout qualifier: only valid on program outputs", "yuv");
    }
}

void TParseContext::checkEarlyFragmentTestsIsNotSpecified(const TSourceLoc &location,
                                                          bool earlyFragmentTests)
{
    if (earlyFragmentTests != false)
    {
        error(location,
              "invalid layout qualifier: only valid when used with 'in' in a fragment shader",
              "early_fragment_tests");
    }
}

void TParseContext::checkNoncoherentIsSpecified(const TSourceLoc &location, bool noncoherent)
{
    if (noncoherent == false)
    {
        error(location,
              "'noncoherent' qualifier must be used when "
              "GL_EXT_shader_framebuffer_fetch_non_coherent extension is used",
              "noncoherent");
    }
}

void TParseContext::checkNoncoherentIsNotSpecified(const TSourceLoc &location, bool noncoherent)
{
    if (noncoherent != false)
    {
        error(location,
              "invalid layout qualifier: only valid when used with 'gl_LastFragData', the "
              "variable decorated with 'inout' in a fragment shader, or pixel local storage",
              "noncoherent");
    }
}

void TParseContext::checkTCSOutVarIndexIsValid(TIntermBinary *binaryExpression,
                                               const TSourceLoc &location)
{
    ASSERT(binaryExpression->getOp() == EOpIndexIndirect ||
           binaryExpression->getOp() == EOpIndexDirect);
    const TIntermSymbol *intermSymbol = binaryExpression->getRight()->getAsSymbolNode();
    if ((intermSymbol == nullptr) || (intermSymbol->getName() != "gl_InvocationID"))
    {
        error(location,
              "tessellation-control per-vertex output l-value must be indexed with "
              "gl_InvocationID",
              "[");
    }
}

void TParseContext::functionCallRValueLValueErrorCheck(const TFunction *fnCandidate,
                                                       TIntermAggregate *fnCall)
{
    for (size_t i = 0; i < fnCandidate->getParamCount(); ++i)
    {
        TQualifier qual        = fnCandidate->getParam(i)->getType().getQualifier();
        TIntermTyped *argument = (*fnCall->getSequence())[i]->getAsTyped();
        bool argumentIsRead    = (IsQualifierUnspecified(qual) || qual == EvqParamIn ||
                               qual == EvqParamInOut || qual == EvqParamConst);
        if (argumentIsRead)
        {
            markStaticUseIfSymbol(argument);
            if (!IsImage(argument->getBasicType()))
            {
                if (argument->getMemoryQualifier().writeonly)
                {
                    error(argument->getLine(),
                          "Writeonly value cannot be passed for 'in' or 'inout' parameters.",
                          fnCall->functionName());
                    return;
                }
            }
        }
        if (qual == EvqParamOut || qual == EvqParamInOut)
        {
            if (!checkCanBeLValue(argument->getLine(), "assign", argument))
            {
                error(argument->getLine(),
                      "Constant value cannot be passed for 'out' or 'inout' parameters.",
                      fnCall->functionName());
                return;
            }
        }
    }
}

void TParseContext::checkClipCullDistanceWholeArrayUse(const TSourceLoc &location,
                                                       TIntermTyped *node,
                                                       const char *message)
{
    switch (node->getQualifier())
    {
        case EvqClipDistance:
            if (mClipDistanceInfo.size == 0)
            {
                error(location, message, "gl_ClipDistance");
                return;
            }
            break;
        case EvqCullDistance:
            if (mCullDistanceInfo.size == 0)
            {
                error(location, message, "gl_CullDistance");
                return;
            }
            break;
        default:
        {
            TIntermBinary *asBinary = node->getAsBinaryNode();
            if (asBinary != nullptr && asBinary->getOp() == EOpComma)
            {
                checkClipCullDistanceWholeArrayUse(location, asBinary->getRight(), message);
                return;
            }
            TIntermTernary *asTernary = node->getAsTernaryNode();
            if (asTernary != nullptr)
            {
                checkClipCullDistanceWholeArrayUse(location, asTernary->getTrueExpression(),
                                                   message);
                checkClipCullDistanceWholeArrayUse(location, asTernary->getFalseExpression(),
                                                   message);
            }
        }
            break;
    }
}

void TParseContext::functionCallClipCullDistanceCheck(const TFunction *fnCandidate,
                                                      TIntermAggregate *fnCall)
{
    // If clip/cull distance is not redeclared, they can't be passed to a function because their
    // size is unknown.  Per EXT_clip_cull_distance, only indexing with constants can implicitly
    // size the built-ins, passing to a function shouldn't try to size them.
    for (size_t i = 0; i < fnCandidate->getParamCount(); ++i)
    {
        TIntermTyped *argument = (*fnCall->getSequence())[i]->getAsTyped();
        checkClipCullDistanceWholeArrayUse(argument->getLine(), argument,
                                           "Cannot pass to function unless it is explicitly sized");
    }
}

void TParseContext::functionCallFragDataCheck(const TFunction *fnCandidate,
                                              TIntermAggregate *fnCall)
{
    for (size_t i = 0; i < fnCandidate->getParamCount(); ++i)
    {
        TIntermTyped *argument = (*fnCall->getSequence())[i]->getAsTyped();
        // Note: ESSL 100 does not allow arrays in ternary operator, so there is no need to check
        // for TIntermTernary here for a whole-array use of gl_FragData, only descending into
        // EOpComma nodes is sufficient.
        if (RemoveCommaLeftHandSide(argument)->getQualifier() == EvqFragData)
        {
            // The whole array is passed to the function.  For validation purposes, assume all
            // indices are accessed in the function.
            ASSERT(argument->getType().isArray());
            mMaxFragDataArrayIndexUsed = argument->getType().getOutermostArraySize() - 1;
        }
    }
}

void TParseContext::checkInvariantVariableQualifier(bool invariant,
                                                    const TQualifier qualifier,
                                                    const TSourceLoc &invariantLocation)
{
    if (!invariant)
    {
        return;
    }

    if (mShaderVersion < 300)
    {
        // input variables in the fragment shader can be also qualified as invariant
        if (!sh::CanBeInvariantESSL1(qualifier))
        {
            error(invariantLocation, "Cannot be qualified as invariant.", "invariant");
        }
    }
    else
    {
        if (!sh::CanBeInvariantESSL3OrGreater(qualifier))
        {
            error(invariantLocation, "Cannot be qualified as invariant.", "invariant");
        }
    }
}

void TParseContext::checkAdvancedBlendEquationsNotSpecified(
    const TSourceLoc &location,
    const AdvancedBlendEquations &advancedBlendEquations,
    const TQualifier &qualifier)
{
    if (advancedBlendEquations.any() && qualifier != EvqFragmentOut)
    {
        error(location,
              "invalid layout qualifier: blending equation qualifiers are only permitted on the "
              "fragment 'out' qualifier ",
              "blend_support_qualifier");
    }
}

bool TParseContext::isExtensionEnabled(TExtension extension) const
{
    return IsExtensionEnabled(extensionBehavior(), extension);
}

void TParseContext::handleExtensionDirective(const TSourceLoc &loc,
                                             const char *extName,
                                             const char *behavior)
{
    angle::pp::SourceLocation srcLoc;
    srcLoc.file = loc.first_file;
    srcLoc.line = loc.first_line;
    mDirectiveHandler.handleExtension(srcLoc, extName, behavior);
}

void TParseContext::handlePragmaDirective(const TSourceLoc &loc,
                                          const char *name,
                                          const char *value,
                                          bool stdgl)
{
    angle::pp::SourceLocation srcLoc;
    srcLoc.file = loc.first_file;
    srcLoc.line = loc.first_line;
    mDirectiveHandler.handlePragma(srcLoc, name, value, stdgl);
}

sh::WorkGroupSize TParseContext::getComputeShaderLocalSize() const
{
    sh::WorkGroupSize result(-1);
    for (size_t i = 0u; i < result.size(); ++i)
    {
        if (mComputeShaderLocalSizeDeclared && mComputeShaderLocalSize[i] == -1)
        {
            result[i] = 1;
        }
        else
        {
            result[i] = mComputeShaderLocalSize[i];
        }
    }
    return result;
}

TIntermConstantUnion *TParseContext::addScalarLiteral(const TConstantUnion *constantUnion,
                                                      const TSourceLoc &line)
{
    TIntermConstantUnion *node = new TIntermConstantUnion(
        constantUnion, TType(constantUnion->getType(), EbpUndefined, EvqConst));
    node->setLine(line);
    pushConstant(constantUnion, node->getType());
    return node;
}

/////////////////////////////////////////////////////////////////////////////////
//
// Non-Errors.
//
/////////////////////////////////////////////////////////////////////////////////

const TVariable *TParseContext::getNamedVariable(const TSourceLoc &location,
                                                 const ImmutableString &name,
                                                 const TSymbol *symbol)
{
    if (!symbol)
    {
        error(location, "undeclared identifier", name);
        return nullptr;
    }

    if (!symbol->isVariable())
    {
        error(location, "variable expected", name);
        return nullptr;
    }

    const TVariable *variable = static_cast<const TVariable *>(symbol);

    if (!variable->extensions().empty() && variable->extensions()[0] != TExtension::UNDEFINED)
    {
        checkCanUseOneOfExtensions(location, variable->extensions());
    }

    // GLSL ES 3.1 Revision 4, 7.1.3 Compute Shader Special Variables
    if (getShaderType() == GL_COMPUTE_SHADER && !mComputeShaderLocalSizeDeclared &&
        variable->getType().getQualifier() == EvqWorkGroupSize)
    {
        error(location,
              "It is an error to use gl_WorkGroupSize before declaring the local group size",
              "gl_WorkGroupSize");
    }

    // If EXT_shader_framebuffer_fetch_non_coherent is used, gl_LastFragData should be decorated
    // with 'layout(noncoherent)' EXT_shader_framebuffer_fetch_non_coherent spec: "Unless the
    // GL_EXT_shader_framebuffer_fetch extension  has been enabled in addition, it's an error to use
    // gl_LastFragData if it hasn't been explicitly redeclared with layout(noncoherent)."
    if (isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch_non_coherent) &&
        !isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch) &&
        variable->getType().getQualifier() == EvqLastFragData)
    {
        checkNoncoherentIsSpecified(location, variable->getType().getLayoutQualifier().noncoherent);
    }

    // When EXT_separate_shader_objects is enabled, gl_Position and gl_PointSize must both be
    // redeclared before either is accessed:
    //
    // > The following vertex shader outputs may be redeclared at global scope to
    // > specify a built-in output interface, with or without special qualifiers:
    // >
    // >     gl_Position
    // >     gl_PointSize
    // >
    // >   When compiling shaders using either of the above variables, both such
    // >   variables must be redeclared prior to use.  ((Note:  This restriction
    // >   applies only to shaders using version 300 that enable the
    // >   EXT_separate_shader_objects extension; shaders not enabling the
    // >   extension do not have this requirement.))
    //
    // However, there are dEQP tests that enable all extensions and don't actually redeclare these
    // variables.  Per https://gitlab.khronos.org/opengl/API/-/issues/169, there are drivers that do
    // enforce this, but they fail linking instead of compilation.
    //
    // In ANGLE, we make sure that they are both redeclared before use if any is redeclared, but if
    // neither are redeclared, we don't fail compilation.  Currently, linking also doesn't fail in
    // ANGLE (similarly to almost all other drivers).
    if (isExtensionEnabled(TExtension::EXT_separate_shader_objects) &&
        mShaderType == GL_VERTEX_SHADER)
    {
        if (variable->getType().getQualifier() == EvqPosition ||
            variable->getType().getQualifier() == EvqPointSize)
        {
            mPositionOrPointSizeUsedForSeparateShaderObject = true;
            const bool eitherIsRedeclared = mPositionRedeclaredForSeparateShaderObject ||
                                            mPointSizeRedeclaredForSeparateShaderObject;
            const bool bothAreRedeclared = mPositionRedeclaredForSeparateShaderObject &&
                                           mPointSizeRedeclaredForSeparateShaderObject;

            if (eitherIsRedeclared && !bothAreRedeclared)
            {
                error(location,
                      "When EXT_separate_shader_objects is enabled, both gl_Position and "
                      "gl_PointSize must be redeclared before either is used",
                      name);
            }
        }
    }

    return variable;
}

ir::VariableId TParseContext::declareBuiltInOnFirstUse(const TVariable *variable)
{
    if (variable->symbolType() == SymbolType::BuiltIn &&
        mVariableToId.find(variable) == mVariableToId.end())
    {
        const TType *variableType = &variable->getType();

        // For and clip/cull distance, let the IR know that they are not actually sized yet.
        if (variableType->getQualifier() == EvqClipDistance ||
            variableType->getQualifier() == EvqCullDistance)
        {
            TType *unsizedArrayType = new TType(*variableType);
            unsizedArrayType->toArrayBaseType();
            unsizedArrayType->makeArray(0);
            variableType = unsizedArrayType;
        }

        // For gl_FragData, change the array size to 1 if MRT is not supported; only index 0 is
        // valid for access.  Note that gl_FragData usage itself is restricted to ESSL 100, so a
        // version check is unnecessary.
        if (variableType->getQualifier() == EvqFragData &&
            !isExtensionEnabled(TExtension::EXT_draw_buffers))
        {
            TType *singleElementArrayType = new TType(*variableType);
            singleElementArrayType->toArrayBaseType();
            singleElementArrayType->makeArray(1);
            variableType = singleElementArrayType;
        }

        const ir::TypeId typeId = getTypeId(*variableType);
        const ir::VariableId id = mIRBuilder.declareInterfaceVariable(
            variable->name(), typeId, *variableType, ir::DeclarationSource::Internal);
        mVariableToId[variable] = VariableToIdInfo{id, VariableToIdInfo::kNoImplicitField};

        switch (variableType->getQualifier())
        {
            case EvqClipDistance:
                mClipDistanceInfo.id = id;
                break;
            case EvqCullDistance:
                mCullDistanceInfo.id = id;
                break;
            default:
                break;
        }

        return id;
    }

    if (mDiagnostics->numErrors() > 0)
    {
        return {};
    }

    return mVariableToId.at(variable).id;
}

void TParseContext::declareFunction(const TFunction *function, FunctionDeclaration declaration)
{
    // If the function prototype hasn't been previously encountered, this is a new function that
    // should be declared to the IR first.
    if (mFunctionToId.find(function) == mFunctionToId.end())
    {
        TVector<ir::VariableId> params;
        TVector<TQualifier> paramDirections;
        for (size_t i = 0; i < function->getParamCount(); ++i)
        {
            const TVariable *param = function->getParam(i);
            const TType &paramType = param->getType();

            ir::VariableId paramId = mIRBuilder.declareFunctionParam(
                param->name(), getTypeId(paramType), paramType, paramType.getQualifier());
            mVariableToId[param] = VariableToIdInfo{paramId, VariableToIdInfo::kNoImplicitField};

            params.push_back(paramId);
            paramDirections.push_back(paramType.getQualifier());
        }

        mFunctionToId[function] = ANGLE_UNSAFE_TODO(mIRBuilder.newFunction(
            function->name(), angle::Span(params.data(), params.size()),
            angle::Span(paramDirections.data(), paramDirections.size()),
            getTypeId(function->getReturnType()), function->getReturnType()));
    }
    else if (declaration == FunctionDeclaration::Definition)
    {
        TVector<ImmutableString> paramNames;
        TVector<ir::VariableId> paramIds(function->getParamCount());
        for (size_t i = 0; i < function->getParamCount(); ++i)
        {
            const TVariable *param = function->getParam(i);
            paramNames.push_back(param->name());
        }
        ANGLE_UNSAFE_TODO(mIRBuilder.updateFunctionParamNames(
            mFunctionToId[function], angle::Span(paramNames.data(), paramNames.size()),
            angle::Span(paramIds.data(), paramIds.size())));

        // When a prototype is previously visited, `declareFunction` has already created the
        // variables for the function parameters in the |if| above.  When the function prototype is
        // visited again during function definition, the real argument names are provided (so the
        // variables should be renamed).  New |TVariable|s are also created by the parser, which
        // should map to the same IR ids.  At this point, the old |TVariable|s are lost, so the IR
        // returns the variable ids for the parameters.
        for (size_t i = 0; i < function->getParamCount(); ++i)
        {
            const TVariable *param = function->getParam(i);
            mVariableToId[param] =
                VariableToIdInfo{paramIds[i], VariableToIdInfo::kNoImplicitField};
        }
    }
}

TIntermTyped *TParseContext::parseVariableIdentifier(const TSourceLoc &location,
                                                     const ImmutableString &name,
                                                     const TSymbol *symbol)
{
    const TVariable *variable = getNamedVariable(location, name, symbol);

    if (!variable)
    {
        TIntermTyped *node = CreateZeroNode(TType(EbtFloat, EbpHigh, EvqConst));
        node->setLine(location);
        return node;
    }

    const TType &variableType = variable->getType();
    TIntermTyped *node        = nullptr;

    if (variable->getConstPointer() && variableType.canReplaceWithConstantUnion())
    {
        const TConstantUnion *constArray = variable->getConstPointer();
        node                             = new TIntermConstantUnion(constArray, variableType);
    }
    else if (variableType.getQualifier() == EvqWorkGroupSize && mComputeShaderLocalSizeDeclared)
    {
        // gl_WorkGroupSize can be used to size arrays according to the ESSL 3.10.4 spec, so it
        // needs to be added to the AST as a constant and not as a symbol.
        sh::WorkGroupSize workGroupSize = getComputeShaderLocalSize();
        TConstantUnion *constArray      = new TConstantUnion[3];
        for (size_t i = 0; i < 3; ++i)
        {
            ANGLE_UNSAFE_TODO(constArray[i]).setUConst(static_cast<unsigned int>(workGroupSize[i]));
        }

        ASSERT(variableType.getBasicType() == EbtUInt);
        ASSERT(variableType.getObjectSize() == 3);

        TType type(variableType);
        type.setQualifier(EvqConst);
        node = new TIntermConstantUnion(constArray, type);
    }
    else if ((mGeometryShaderInputPrimitiveType != EptUndefined) &&
             (variableType.getQualifier() == EvqPerVertexIn))
    {
        ASSERT(symbolTable.getGlInVariableWithArraySize() != nullptr);
        variable = symbolTable.getGlInVariableWithArraySize();
        node     = new TIntermSymbol(variable);
    }
    else
    {
        // gl_LastFragDepthARM and gl_LastFragStencilARM cannot be accessed if early_fragment_tests
        // is specified.
        if ((variableType.getQualifier() == EvqLastFragDepth ||
             variableType.getQualifier() == EvqLastFragStencil) &&
            isEarlyFragmentTestsSpecified())
        {
            error(location,
                  "gl_LastFragDepthARM and gl_LastFragStencilARM cannot be accessed because "
                  "early_fragment_tests is specified",
                  name);
        }

        node = new TIntermSymbol(variable);
    }
    ASSERT(node != nullptr);
    node->setLine(location);

    // Push the variable or its equivalent constant.  Note that when the variable is declared as
    // `const`, the variable is pushed instead of the constant so its precision is retained.
    if (variableType.getQualifier() == EvqConst &&
        mVariableToId.find(variable) != mVariableToId.end())
    {
        pushVariable(variable);
    }
    else if (node->getAsConstantUnion())
    {
        pushConstant(node->getAsConstantUnion()->getConstantValue(), variableType);
    }
    else if (variable->getConstPointer())
    {
        pushConstant(variable->getConstPointer(), variableType);
    }
    else if (node->getAsSymbolNode())
    {
        // For built-ins, declare them in the IR on first reference.
        declareBuiltInOnFirstUse(variable);
        pushVariable(variable);

        if (variableType.getQualifier() == EvqFragDepth)
        {
            mFragmentOutputFragDepthUsed = true;
        }
    }

    return node;
}

void TParseContext::adjustRedeclaredBuiltInType(const TSourceLoc &line,
                                                const ImmutableString &identifier,
                                                TType *type)
{
    if (identifier == "gl_ClipDistance")
    {
        const TQualifier qualifier = type->getQualifier();
        if ((mShaderType == GL_VERTEX_SHADER &&
             !(qualifier == EvqVertexOut || qualifier == EvqVaryingOut)) ||
            (mShaderType == GL_FRAGMENT_SHADER && qualifier != EvqFragmentIn))
        {
            error(line, "invalid or missing storage qualifier", identifier);
            return;
        }

        type->setQualifier(EvqClipDistance);
    }
    else if (identifier == "gl_CullDistance")
    {
        const TQualifier qualifier = type->getQualifier();
        if ((mShaderType == GL_VERTEX_SHADER && qualifier != EvqVertexOut) ||
            (mShaderType == GL_FRAGMENT_SHADER && qualifier != EvqFragmentIn))
        {
            error(line, "invalid or missing storage qualifier", identifier);
            return;
        }

        type->setQualifier(EvqCullDistance);
    }
    else if (identifier == "gl_LastFragData")
    {
        type->setQualifier(EvqLastFragData);
    }
    else if (identifier == "gl_LastFragColorARM")
    {
        type->setQualifier(EvqLastFragColor);
    }
    else if (identifier == "gl_LastFragDepthARM")
    {
        type->setQualifier(EvqLastFragDepth);
    }
    else if (identifier == "gl_LastFragStencilARM")
    {
        type->setQualifier(EvqLastFragStencil);
    }
    else if (identifier == "gl_Position")
    {
        type->setQualifier(EvqPosition);
    }
    else if (identifier == "gl_PointSize")
    {
        type->setQualifier(EvqPointSize);
    }
}

// Initializers show up in several places in the grammar.  Have one set of
// code to handle them here.
//
// Returns true on success.
bool TParseContext::executeInitializer(const TSourceLoc &line,
                                       const ImmutableString &identifier,
                                       TType *type,
                                       TIntermTyped *initializer,
                                       TIntermBinary **initNode)
{
    ASSERT(initNode != nullptr);
    ASSERT(*initNode == nullptr);

    if (type->isUnsizedArray())
    {
        // In case initializer is not an array or type has more dimensions than initializer, this
        // will default to setting array sizes to 1. We have not checked yet whether the initializer
        // actually is an array or not. Having a non-array initializer for an unsized array will
        // result in an error later, so we don't generate an error message here.
        type->sizeUnsizedArrays(initializer->getType().getArraySizes());
    }

    type->setTypeId(getTypeId(*type));

    const TQualifier qualifier = type->getQualifier();

    bool constError = false;
    if (qualifier == EvqConst)
    {
        if (EvqConst != initializer->getType().getQualifier())
        {
            TInfoSinkBase reasonStream;
            reasonStream << "assigning non-constant to '" << *type << "'";
            error(line, reasonStream.c_str(), "=");

            // We're still going to declare the variable to avoid extra error messages.
            type->setQualifier(EvqTemporary);
            constError = true;
        }
    }

    TVariable *variable = nullptr;
    if (!declareVariable(line, identifier, type, GeomTessArray::Sized, &variable))
    {
        return false;
    }

    if (constError)
    {
        return false;
    }

    bool nonConstGlobalInitializers =
        IsExtensionEnabled(mDirectiveHandler.extensionBehavior(),
                           TExtension::EXT_shader_non_constant_global_initializers);
    bool globalInitWarning = false;
    bool tooComplex        = false;
    if (symbolTable.atGlobalLevel())
    {
        bool valid = ValidateGlobalInitializer(
            initializer, mShaderVersion, sh::IsWebGLBasedSpec(mShaderSpec),
            nonConstGlobalInitializers, &globalInitWarning, &tooComplex);
        if (!valid || tooComplex)
        {
            // Error message does not completely match behavior with ESSL 1.00, but
            // we want to steer developers towards only using constant expressions.
            //
            // Note: the "Expression too complex" check can be removed once IR is the only path, as
            // it's not sensitive to expression depth.
            error(line,
                  tooComplex ? "Expression too complex"
                             : "global variable initializers must be constant expressions",
                  "=");
            return false;
        }
        if (globalInitWarning)
        {
            warning(line,
                    "global variable initializers should be constant expressions "
                    "(uniforms and globals are allowed in global initializers for legacy "
                    "compatibility)",
                    "=");
        }
    }

    // identifier must be of type constant, a global, or a temporary
    if ((qualifier != EvqTemporary) && (qualifier != EvqGlobal) && (qualifier != EvqConst))
    {
        error(line, " cannot initialize this type of qualifier ",
              variable->getType().getQualifierString());
        return false;
    }

    TIntermSymbol *intermSymbol = new TIntermSymbol(variable);
    intermSymbol->setLine(line);

    if (!binaryOpCommonCheck(EOpInitialize, intermSymbol, initializer, line))
    {
        assignError(line, "=", variable->getType(), initializer->getType());
        return false;
    }

    const TConstantUnion *initializerConstArray = initializer->getConstantValue();
    if (initializerConstArray)
    {
        if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior)
        {
            // If this is `bool variable = true`, track it.  If it's ever used as l-value, it's
            // removed from this list.  At the end of parse, if a variable is in this list, it's set
            // to true and never modified.
            if (type->isScalarBool() && initializerConstArray->getBConst())
            {
                mConstantTrueVariables.insert(variable->uniqueId());
            }
        }

        // Save the constant folded value to the variable if possible.
        if (qualifier == EvqConst)
        {
            variable->shareConstPointer(initializerConstArray);
            if (initializer->getType().canReplaceWithConstantUnion())
            {
                mIRBuilder.initialize(mVariableToId.at(variable).id);
                ASSERT(*initNode == nullptr);
                return true;
            }
        }
    }

    mIRBuilder.initialize(mVariableToId.at(variable).id);

    *initNode = new TIntermBinary(EOpInitialize, intermSymbol, initializer);
    markStaticUseIfSymbol(initializer);
    (*initNode)->setLine(line);
    return true;
}

TIntermNode *TParseContext::addConditionInitializer(const TPublicType &pType,
                                                    const ImmutableString &identifier,
                                                    TIntermTyped *initializer,
                                                    const TSourceLoc &loc)
{
    checkIsScalarBool(loc, pType);
    TIntermBinary *initNode = nullptr;
    TType *type             = new TType(pType);
    if (executeInitializer(loc, identifier, type, initializer, &initNode))
    {
        // The initializer is valid. The init condition needs to have a node - either the
        // initializer node, or a constant node in case the initialized variable is const and won't
        // be recorded in the AST.
        if (initNode == nullptr)
        {
            return initializer;
        }
        else
        {
            TIntermDeclaration *declaration = new TIntermDeclaration();
            declaration->appendDeclarator(initNode);
            return declaration;
        }
    }
    return nullptr;
}

void TParseContext::checkESSL100ForLoopInit(TIntermNode *init, const TSourceLoc &line)
{
    // The loop must be a `for` loop, and have the following form according to ESSL 100 spec,
    // Appendix A:
    //
    //    for (type symbol = initializer; symbol op constant; symbol += constant)
    //
    // Validate the init statement here.
    if (init == nullptr)
    {
        error(line, "Missing init declaration", "for");
        return;
    }

    //
    // init-declaration has the form:
    //     type-specifier identifier = constant-expression
    //
    TIntermDeclaration *decl = init->getAsDeclarationNode();
    if (decl == nullptr)
    {
        error(line, "Invalid init declaration", "for");
        return;
    }
    TIntermSequence *declSeq = decl->getSequence();
    if (declSeq->size() != 1)
    {
        error(line, "Invalid init declaration", "for");
        return;
    }
    TIntermBinary *declInit = (*declSeq)[0]->getAsBinaryNode();
    if (declInit == nullptr || declInit->getOp() != EOpInitialize)
    {
        error(line, "Invalid init declaration", "for");
        return;
    }
    TIntermSymbol *symbol = declInit->getLeft()->getAsSymbolNode();
    if (symbol == nullptr)
    {
        error(line, "Invalid init declaration", "for");
        return;
    }
    // The loop index has type int or float.
    TBasicType type = symbol->getBasicType();
    if ((type != EbtInt && type != EbtUInt && type != EbtFloat) || !symbol->isScalar())
    {
        error(line, "Invalid type for loop index", getBasicString(type));
        return;
    }
    // The loop index is initialized with constant expression.
    if (!IsESSL100ConstantExpression(declInit->getRight()))
    {
        error(line, "Loop index cannot be initialized with non-constant expression",
              symbol->getName());
        return;
    }

    // Keep track of the loop symbol.  The loop symbol is not allowed to be modified in the body.
    ASSERT(mControlFlow.back().type == ControlFlowType::Loop);
    mControlFlow.back().forLoopSymbol = symbol->uniqueId();
}

void TParseContext::checkESSL100ForLoopCondition(TIntermNode *condition, const TSourceLoc &line)
{
    if (condition == nullptr)
    {
        error(line, "Missing condition", "for");
        return;
    }

    // condition has the form:
    //     loop_index relational_operator constant_expression
    TIntermBinary *binOp = condition->getAsBinaryNode();
    if (binOp == nullptr)
    {
        error(line, "Invalid condition", "for");
        return;
    }
    // Loop index should be to the left of relational operator.
    TIntermSymbol *symbol = binOp->getLeft()->getAsSymbolNode();
    if (symbol == nullptr)
    {
        error(line, "Invalid condition", "for");
        return;
    }
    ASSERT(mControlFlow.back().type == ControlFlowType::Loop);
    if (symbol->uniqueId() != mControlFlow.back().forLoopSymbol)
    {
        error(line, "Expected loop index", symbol->getName());
        return;
    }
    // Relational operator is one of: > >= < <= == or !=.
    switch (binOp->getOp())
    {
        case EOpEqual:
        case EOpNotEqual:
        case EOpLessThan:
        case EOpGreaterThan:
        case EOpLessThanEqual:
        case EOpGreaterThanEqual:
            break;
        default:
            error(line, "Invalid relational operator", GetOperatorString(binOp->getOp()));
            return;
    }
    // Loop index must be compared with a constant.
    if (!IsESSL100ConstantExpression(binOp->getRight()))
    {
        error(line, "Loop index cannot be compared with non-constant expression",
              symbol->getName());
        return;
    }
}

void TParseContext::checkESSL100ForLoopContinue(TIntermNode *statement, const TSourceLoc &line)
{
    if (statement == nullptr)
    {
        error(line, "Missing expression", "for");
        return;
    }

    // for expression has one of the following forms:
    //
    //     loop_index++
    //     loop_index--
    //     loop_index += constant_expression
    //     loop_index -= constant_expression
    //     ++loop_index
    //     --loop_index
    //
    // The last two forms are not specified in the spec, but we're assuming its an oversight.
    TIntermUnary *unOp   = statement->getAsUnaryNode();
    TIntermBinary *binOp = unOp ? nullptr : statement->getAsBinaryNode();

    TOperator op            = EOpNull;
    const TFunction *opFunc = nullptr;
    TIntermSymbol *symbol   = nullptr;
    if (unOp != nullptr)
    {
        op     = unOp->getOp();
        opFunc = unOp->getFunction();
        symbol = unOp->getOperand()->getAsSymbolNode();
    }
    else if (binOp != nullptr)
    {
        op     = binOp->getOp();
        symbol = binOp->getLeft()->getAsSymbolNode();
    }

    // The operand must be loop index.
    if (symbol == nullptr)
    {
        error(line, "Invalid expression", "for");
        return;
    }
    ASSERT(mControlFlow.back().type == ControlFlowType::Loop);
    if (symbol->uniqueId() != mControlFlow.back().forLoopSymbol)
    {
        error(line, "Expected loop index", symbol->getName());
        return;
    }

    // The operator is one of: ++ -- += -=.
    switch (op)
    {
        case EOpPostIncrement:
        case EOpPostDecrement:
        case EOpPreIncrement:
        case EOpPreDecrement:
            ASSERT(unOp != nullptr && binOp == nullptr);
            break;
        case EOpAddAssign:
        case EOpSubAssign:
            ASSERT(unOp == nullptr && binOp != nullptr);
            break;
        default:
            if (BuiltInGroup::IsBuiltIn(op))
            {
                ASSERT(opFunc != nullptr);
                error(line, "Invalid built-in call", opFunc->name().data());
            }
            else
            {
                error(line, "Invalid operator", GetOperatorString(op));
            }
            return;
    }

    // Loop index must be incremented/decremented with a constant.
    if (binOp != nullptr)
    {
        if (!IsESSL100ConstantExpression(binOp->getRight()))
        {
            error(line, "Loop index cannot be modified by non-constant expression",
                  symbol->getName());
            return;
        }
    }

    // After the continue statement is visited, mark the for loop symbol as needing to stay
    // constant.
    mControlFlow.back().isForLoopSymbolConstant = true;
}

bool TParseContext::isESSL100ConstantLoopSymbol(TIntermSymbol *symbol)
{
    ASSERT(symbol != nullptr);
    const TSymbolUniqueId symbolUniqueId = symbol->uniqueId();

    for (const ControlFlow &controlFlow : mControlFlow)
    {
        if (controlFlow.isForLoopSymbolConstant && symbolUniqueId == controlFlow.forLoopSymbol)
        {
            return true;
        }
    }
    return false;
}

void TParseContext::checkESSL100NoLoopSymbolAssign(TIntermSymbol *symbol, const TSourceLoc &line)
{
    if (isESSL100ConstantLoopSymbol(symbol))
    {
        error(line, "Loop index cannot be statically assigned to within the body of the loop",
              symbol->getName());
    }
}

void TParseContext::checkESSL100ConstantIndex(TIntermTyped *index, const TSourceLoc &line)
{
    // According to ESSL 100 spec, Appendix A:
    //
    // > constant-index-expressions are a superset of constant-expressions.
    // > Constant-index-expressions can include loop indices as defined in GLSL ES 1.0 spec,
    // > Appendix A, section 4.
    //
    // > The following are constant-index-expressions:
    // > - Constant expressions
    // > - Loop indices as defined in section 4
    // > - Expressions composed of both of the above
    //
    // To implement the above, all subnodes of index are visited:
    //
    // * If any are symbols, they must be a loop index.
    // * Otherwise if they have no children, they must have a constant value.
    // * No user function calls are allowed (every other forbidden function call ends up using a
    //   symbol, such as texture2D())
    //
    // Since the expression complexity validation is not done yet (check against
    // MaxExpressionComplexity), this operation is not done with recursion.
    std::vector<TIntermTyped *> toInspect;
    toInspect.push_back(index);

    while (!toInspect.empty())
    {
        TIntermTyped *node = toInspect.back();
        toInspect.pop_back();

        if (node->getAsAggregate() && node->getAsAggregate()->isFunctionCall())
        {
            error(line, "Index expression cannot contain function calls", "[]");
            return;
        }

        size_t childCount = node->getChildCount();
        if (childCount == 0)
        {
            // If a symbol is used that's not const or a loop index, this expression is not allowed.
            TIntermSymbol *symbol = node->getAsSymbolNode();
            if (symbol != nullptr)
            {
                if (symbol->getQualifier() != EvqConst && !isESSL100ConstantLoopSymbol(symbol))
                {
                    error(line, "Index expression can only contain const or loop symbols",
                          symbol->getName().data());
                    return;
                }
            }
            else if (!node->hasConstantValue())
            {
                error(line, "Index expression must be constant", "[]");
                return;
            }
        }

        for (size_t childIndex = 0; childIndex < childCount; ++childIndex)
        {
            toInspect.push_back(node->getChildNode(childIndex)->getAsTyped());
        }
    }
}

void TParseContext::popControlFlow()
{
    ASSERT(!mControlFlow.empty());
    const ControlFlow justEndedControlFlow = mControlFlow.back();
    mControlFlow.pop_back();

    if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior)
    {
        // Carry information about whether break or return are present in the block to the parent
        // block.
        if (!mControlFlow.empty())
        {
            mControlFlow.back().hasReturn =
                mControlFlow.back().hasReturn || justEndedControlFlow.hasReturn;
            // `break` in an if block or just a nested block also break out of the outer construct.
            if (justEndedControlFlow.type == ControlFlowType::If ||
                justEndedControlFlow.type == ControlFlowType::NewScope)
            {
                mControlFlow.back().hasBreak =
                    mControlFlow.back().hasBreak || justEndedControlFlow.hasBreak;
            }
        }

        if (justEndedControlFlow.type != ControlFlowType::Loop || justEndedControlFlow.hasReturn ||
            justEndedControlFlow.hasBreak)
        {
            return;
        }

        // If the loop has a constant-true condition without a break or return, it will loop
        // forever. Give a parse error about it.
        if (justEndedControlFlow.isLoopConditionConstantTrue)
        {
            error(justEndedControlFlow.loopLocation, "Infinite loop detected in the shader", "");
            return;
        }

        // Otherwise, if the loop is based on a symbol that stays constant-true until the end of the
        // shader, that's also an obvious infinite loop.
        if (justEndedControlFlow.loopConditionConstantTrueSymbol != nullptr &&
            mConstantTrueVariables.find(
                justEndedControlFlow.loopConditionConstantTrueSymbol->uniqueId()) !=
                mConstantTrueVariables.end())
        {
            // But we can't know whether the variable will stay unchanged until the end of the
            // shader, so the decision to produce a compile error is deferred.
            VariableAndLocation loopVariable;
            loopVariable.line     = justEndedControlFlow.loopLocation;
            loopVariable.variable = justEndedControlFlow.loopConditionConstantTrueSymbol;

            mPossiblyInfiniteLoops.push_back(loopVariable);
        }
    }
}

void TParseContext::beginNestedScope()
{
    symbolTable.push();

    ControlFlow flow = {};
    flow.type        = ControlFlowType::NewScope;
    mControlFlow.push_back(flow);
}

void TParseContext::endNestedScope()
{
    symbolTable.pop();
    popControlFlow();
}

void TParseContext::beginLoop(TLoopType loopType, const TSourceLoc &line)
{
    ControlFlow flow = {};
    flow.type        = ControlFlowType::Loop;
    mControlFlow.push_back(flow);

    checkNestingLevel(line);

    // According to ESSL 100 spec, Appendix A, while and do-while don't need to be supported.
    // WebGL forbids them, and so they must be rejected.
    if (mValidateESSL100Limitations && loopType != ELoopFor)
    {
        error(line, "This type of loop is not allowed", loopType == ELoopWhile ? "while" : "do");
    }
}

void TParseContext::onLoopConditionBegin(TIntermNode *init, const TSourceLoc &line)
{
    if (mValidateESSL100Limitations)
    {
        checkESSL100ForLoopInit(init, line);
    }

    // Make sure variables declared in |init| are scoped to the for loop in the IR.  This is to aid
    // generators distinguish between:
    //
    //     for (int i = 0; ...) { }
    //
    // and:
    //
    //     int i = 0;
    //     for (; ...) { }
    //
    // because they otherwise look identical in the IR.  The difference between the two is that in
    // the second case, |i| might be used after the `for` loop.  This rescoping of the variable is
    // purely an optimization; the IR would be able to tell if |i| is later used or not by visiting
    // the rest of the block, which is simply less efficient.
    if (init != nullptr)
    {
        TIntermDeclaration *declaration = init->getAsDeclarationNode();
        if (declaration != nullptr)
        {
            for (TIntermNode *singleDecl : *declaration->getSequence())
            {
                // Extract the symbol and scope it to the `for` loop.  All the `nullptr` checks here
                // are there to avoid crashes in the presence of compile errors.
                TIntermBinary *symbolInit = singleDecl->getAsBinaryNode();
                TIntermSymbol *symbol =
                    symbolInit != nullptr && symbolInit->getOp() == EOpInitialize
                        ? symbolInit->getLeft()->getAsSymbolNode()
                        : singleDecl->getAsSymbolNode();
                // The check makes sure sole struct declarations are skipped, like `struct S { ...
                // };` which the translator declares with an "empty" variable.  Same if init
                // expression is just `S;`.
                if (symbol != nullptr && symbol->variable().symbolType() != SymbolType::Empty)
                {
                    mIRBuilder.rescopeAsForLoopVariable(mVariableToId.at(&symbol->variable()).id);
                }
            }
        }
    }

    mIRBuilder.beginLoopCondition();
}

void TParseContext::onLoopConditionEnd(TIntermNode *condition, const TSourceLoc &line)
{
    if (mValidateESSL100Limitations)
    {
        checkESSL100ForLoopCondition(condition, line);
    }

    if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior)
    {
        mControlFlow.back().loopLocation = line;
        TIntermConstantUnion *constCondition =
            condition ? condition->getAsConstantUnion() : nullptr;
        TIntermSymbol *conditionSymbol = condition ? condition->getAsSymbolNode() : nullptr;

        const bool isConditionConstantTrue =
            condition == nullptr ||
            (constCondition != nullptr && constCondition->getType().isScalarBool() &&
             constCondition->getBConst(0));

        if (isConditionConstantTrue)
        {
            mControlFlow.back().isLoopConditionConstantTrue = true;
        }
        else if (conditionSymbol != nullptr &&
                 mConstantTrueVariables.find(conditionSymbol->uniqueId()) !=
                     mConstantTrueVariables.end())
        {
            mControlFlow.back().loopConditionConstantTrueSymbol = &conditionSymbol->variable();
        }
    }

    if (condition == nullptr)
    {
        // If a condition is not specified, assume it's true (possible with for(..;;..)).
        mIRBuilder.pushConstantBool(true);
    }
    else if (condition->getAsDeclarationNode())
    {
        // If a condition is a variable declaration (like while (bool cond = ...)), push the
        // variable to the stack so it's loaded from.
        TIntermDeclaration *declaration = condition->getAsDeclarationNode();
        TIntermBinary *declarator       = declaration->getSequence()->front()->getAsBinaryNode();
        ASSERT(declarator->getLeft()->getAsSymbolNode());
        pushVariable(&declarator->getLeft()->getAsSymbolNode()->variable());
    }
    mIRBuilder.endLoopCondition();
}

void TParseContext::onLoopContinueEnd(TIntermNode *statement, const TSourceLoc &line)
{
    if (mValidateESSL100Limitations)
    {
        checkESSL100ForLoopContinue(statement, line);
    }

    mIRBuilder.endLoopContinue();
    endStatementWithValue(statement);
}

void TParseContext::onDoLoopBegin()
{
    mIRBuilder.beginDoLoop();
}

void TParseContext::onDoLoopConditionBegin()
{
    mIRBuilder.beginDoLoopCondition();
}

TIntermNode *TParseContext::addLoop(TLoopType type,
                                    TIntermNode *init,
                                    TIntermNode *cond,
                                    TIntermTyped *expr,
                                    TIntermNode *body,
                                    const TSourceLoc &line)
{
    popControlFlow();

    TIntermNode *node       = nullptr;
    TIntermTyped *typedCond = nullptr;
    if (cond)
    {
        markStaticUseIfSymbol(cond);
        typedCond = cond->getAsTyped();
    }
    if (expr)
    {
        markStaticUseIfSymbol(expr);
    }

    // In case the loop body was not parsed as a block and contains a statement that simply refers
    // to a variable, we need to mark it as statically used.
    if (body)
    {
        markStaticUseIfSymbol(body);
    }
    if (cond == nullptr || typedCond)
    {
        if (type == ELoopDoWhile && typedCond)
        {
            checkIsScalarBool(line, typedCond);
        }

        if (type == ELoopDoWhile)
        {
            mIRBuilder.endDoLoop();
        }
        else
        {
            mIRBuilder.endLoop();
        }

        // In the case of other loops, it was checked before that the condition is a scalar boolean.
        ASSERT(mDiagnostics->numErrors() > 0 || typedCond == nullptr ||
               (typedCond->getBasicType() == EbtBool && !typedCond->isArray() &&
                !typedCond->isVector()));

        node = new TIntermLoop(type, init, typedCond, expr, EnsureLoopBodyBlock(body));
        node->setLine(line);
        return node;
    }

    ASSERT(type != ELoopDoWhile);
    mIRBuilder.endLoop();

    TIntermDeclaration *declaration = cond->getAsDeclarationNode();
    ASSERT(declaration);
    TIntermBinary *declarator = declaration->getSequence()->front()->getAsBinaryNode();
    ASSERT(declarator->getLeft()->getAsSymbolNode());

    // The condition is a declaration. In the AST representation we don't support declarations as
    // loop conditions. Wrap the loop to a block that declares the condition variable and contains
    // the loop.
    TIntermBlock *block = new TIntermBlock();

    TIntermDeclaration *declareCondition = new TIntermDeclaration();
    declareCondition->appendDeclarator(declarator->getLeft()->deepCopy());
    block->appendStatement(declareCondition);

    TIntermBinary *conditionInit = new TIntermBinary(EOpAssign, declarator->getLeft()->deepCopy(),
                                                     declarator->getRight()->deepCopy());
    TIntermLoop *loop = new TIntermLoop(type, init, conditionInit, expr, EnsureLoopBodyBlock(body));
    block->appendStatement(loop);
    loop->setLine(line);
    block->setLine(line);
    return block;
}

void TParseContext::onIfTrueBlockBegin(TIntermTyped *cond, const TSourceLoc &loc)
{
    ControlFlow flow = {};
    flow.type        = ControlFlowType::If;
    mControlFlow.push_back(flow);

    checkIsScalarBool(loc, cond);
    mIRBuilder.beginIfTrueBlock();
}

void TParseContext::onIfTrueBlockEnd()
{
    mIRBuilder.endIfTrueBlock();
}

void TParseContext::onIfFalseBlockBegin()
{
    mIRBuilder.beginIfFalseBlock();
}

void TParseContext::onIfFalseBlockEnd()
{
    mIRBuilder.endIfFalseBlock();
}

TIntermNode *TParseContext::addIfElse(TIntermTyped *cond,
                                      TIntermNodePair code,
                                      const TSourceLoc &loc)
{
    popControlFlow();

    bool isScalarBool = checkIsScalarBool(loc, cond);
    // In case the conditional statements were not parsed as blocks and contain a statement that
    // simply refers to a variable, we need to mark them as statically used.
    if (code.node1)
    {
        markStaticUseIfSymbol(code.node1);
    }
    if (code.node2)
    {
        markStaticUseIfSymbol(code.node2);
    }

    mIRBuilder.endIf();

    // For compile time constant conditions, prune the code now.
    if (isScalarBool && cond->getAsConstantUnion())
    {
        if (cond->getAsConstantUnion()->getBConst(0) == true)
        {
            return EnsureBlock(code.node1);
        }
        else
        {
            return EnsureBlock(code.node2);
        }
    }

    TIntermIfElse *node = new TIntermIfElse(cond, EnsureBlock(code.node1), EnsureBlock(code.node2));
    markStaticUseIfSymbol(cond);
    node->setLine(loc);

    return node;
}

void TParseContext::addFullySpecifiedType(TPublicType *typeSpecifier)
{
    checkPrecisionSpecified(typeSpecifier->getLine(), typeSpecifier->precision,
                            typeSpecifier->getBasicType());

    if (mShaderVersion < 300 && typeSpecifier->isArray())
    {
        error(typeSpecifier->getLine(), "not supported", "first-class array");
        typeSpecifier->clearArrayness();
    }
}

TPublicType TParseContext::addFullySpecifiedType(const TTypeQualifierBuilder &typeQualifierBuilder,
                                                 const TPublicType &typeSpecifier)
{
    TTypeQualifier typeQualifier = typeQualifierBuilder.getVariableTypeQualifier(mDiagnostics);

    TPublicType returnType     = typeSpecifier;
    returnType.qualifier       = typeQualifier.qualifier;
    returnType.invariant       = typeQualifier.invariant;
    returnType.precise         = typeQualifier.precise;
    returnType.layoutQualifier = typeQualifier.layoutQualifier;
    returnType.memoryQualifier = typeQualifier.memoryQualifier;
    returnType.precision       = typeSpecifier.precision;

    if (typeQualifier.precision != EbpUndefined)
    {
        returnType.precision = typeQualifier.precision;
    }

    checkPrecisionSpecified(typeSpecifier.getLine(), returnType.precision,
                            typeSpecifier.getBasicType());

    checkInvariantVariableQualifier(returnType.invariant, returnType.qualifier,
                                    typeSpecifier.getLine());

    checkWorkGroupSizeIsNotSpecified(typeSpecifier.getLine(), returnType.layoutQualifier);

    checkEarlyFragmentTestsIsNotSpecified(typeSpecifier.getLine(),
                                          returnType.layoutQualifier.earlyFragmentTests);

    switch (returnType.qualifier)
    {
        case EvqSmooth:
        case EvqFlat:
        case EvqNoPerspective:
        case EvqCentroid:
        case EvqSample:
        case EvqNoPerspectiveCentroid:
        case EvqNoPerspectiveSample:
            // These qualifiers must be merged with |in| or |out| qualifiers.
            error(typeSpecifier.getLine(), "qualifier can only be used with in and out variables",
                  getQualifierString(returnType.qualifier));
            break;

        case EvqSampleIn:
        case EvqSampleOut:
        case EvqNoPerspectiveSampleIn:
        case EvqNoPerspectiveSampleOut:
            mSampleQualifierSpecified = true;
            break;
        default:
            break;
    }

    if (mShaderVersion < 300)
    {
        if (typeSpecifier.isArray())
        {
            error(typeSpecifier.getLine(), "not supported", "first-class array");
            returnType.clearArrayness();
        }

        if (returnType.qualifier == EvqAttribute &&
            (typeSpecifier.getBasicType() == EbtBool || typeSpecifier.getBasicType() == EbtInt))
        {
            error(typeSpecifier.getLine(), "cannot be bool or int",
                  getQualifierString(returnType.qualifier));
        }

        if ((returnType.qualifier == EvqVaryingIn || returnType.qualifier == EvqVaryingOut) &&
            (typeSpecifier.getBasicType() == EbtBool || typeSpecifier.getBasicType() == EbtInt))
        {
            error(typeSpecifier.getLine(), "cannot be bool or int",
                  getQualifierString(returnType.qualifier));
        }
    }
    else
    {
        if (!returnType.layoutQualifier.isEmpty())
        {
            checkIsAtGlobalLevel(typeSpecifier.getLine(), "layout");
        }
        if (sh::IsVarying(returnType.qualifier) || returnType.qualifier == EvqVertexIn ||
            returnType.qualifier == EvqFragmentOut || returnType.qualifier == EvqFragmentInOut)
        {
            checkInputOutputTypeIsValidES3(returnType.qualifier, typeSpecifier,
                                           typeSpecifier.getLine());
        }
        if (returnType.qualifier == EvqComputeIn)
        {
            error(typeSpecifier.getLine(), "'in' can only be used to specify the local group size",
                  "in");
        }
    }

    return returnType;
}

void TParseContext::checkInputOutputTypeIsValidES3(const TQualifier qualifier,
                                                   const TPublicType &type,
                                                   const TSourceLoc &qualifierLocation)
{
    // An input/output variable can never be bool or a sampler. Samplers are checked elsewhere.
    if (type.getBasicType() == EbtBool)
    {
        error(qualifierLocation, "cannot be bool", getQualifierString(qualifier));
    }

    // Specific restrictions apply for vertex shader inputs and fragment shader outputs.
    switch (qualifier)
    {
        case EvqVertexIn:
            // ESSL 3.00 section 4.3.4
            if (type.isArray())
            {
                error(qualifierLocation, "cannot be array", getQualifierString(qualifier));
            }
            // Vertex inputs with a struct type are disallowed in nonEmptyDeclarationErrorCheck
            return;
        case EvqFragmentOut:
        case EvqFragmentInOut:
            // ESSL 3.00 section 4.3.6
            if (type.typeSpecifierNonArray.isMatrix())
            {
                error(qualifierLocation, "cannot be matrix", getQualifierString(qualifier));
            }
            // Fragment outputs with a struct type are disallowed in nonEmptyDeclarationErrorCheck
            return;
        default:
            break;
    }

    // Vertex shader outputs / fragment shader inputs have a different, slightly more lenient set of
    // restrictions.
    bool typeContainsIntegers =
        (type.getBasicType() == EbtInt || type.getBasicType() == EbtUInt ||
         type.isStructureContainingType(EbtInt) || type.isStructureContainingType(EbtUInt));
    bool extendedShaderTypes = mShaderVersion >= 320 ||
                               isExtensionEnabled(TExtension::EXT_geometry_shader) ||
                               isExtensionEnabled(TExtension::OES_geometry_shader) ||
                               isExtensionEnabled(TExtension::EXT_tessellation_shader) ||
                               isExtensionEnabled(TExtension::OES_tessellation_shader);
    if (typeContainsIntegers && qualifier != EvqFlatIn && qualifier != EvqFlatOut &&
        (!extendedShaderTypes || mShaderType == GL_FRAGMENT_SHADER))
    {
        error(qualifierLocation, "must use 'flat' interpolation here",
              getQualifierString(qualifier));
    }

    if (type.getBasicType() == EbtStruct)
    {
        // ESSL 3.00 sections 4.3.4 and 4.3.6.
        // These restrictions are only implied by the ESSL 3.00 spec, but
        // the ESSL 3.10 spec lists these restrictions explicitly.
        if (type.isArray())
        {
            error(qualifierLocation, "cannot be an array of structures",
                  getQualifierString(qualifier));
        }
        if (type.isStructureContainingArrays())
        {
            error(qualifierLocation, "cannot be a structure containing an array",
                  getQualifierString(qualifier));
        }
        if (type.isStructureContainingType(EbtStruct))
        {
            error(qualifierLocation, "cannot be a structure containing a structure",
                  getQualifierString(qualifier));
        }
        if (type.isStructureContainingType(EbtBool))
        {
            error(qualifierLocation, "cannot be a structure containing a bool",
                  getQualifierString(qualifier));
        }
    }
}

void TParseContext::checkLocalVariableConstStorageQualifier(const TQualifierWrapperBase &qualifier)
{
    if (qualifier.getType() == QtStorage)
    {
        const TStorageQualifierWrapper &storageQualifier =
            static_cast<const TStorageQualifierWrapper &>(qualifier);
        if (!declaringFunction() && storageQualifier.getQualifier() != EvqConst &&
            !symbolTable.atGlobalLevel())
        {
            error(storageQualifier.getLine(),
                  "Local variables can only use the const storage qualifier.",
                  storageQualifier.getQualifierString());
        }
    }
}

void TParseContext::checkMemoryQualifierIsNotSpecified(const TMemoryQualifier &memoryQualifier,
                                                       const TSourceLoc &location)
{
    const std::string reason(
        "Only allowed with shader storage blocks, variables declared within shader storage blocks "
        "and variables declared as image types.");
    if (memoryQualifier.readonly)
    {
        error(location, reason.c_str(), "readonly");
    }
    if (memoryQualifier.writeonly)
    {
        error(location, reason.c_str(), "writeonly");
    }
    if (memoryQualifier.coherent)
    {
        error(location, reason.c_str(), "coherent");
    }
    if (memoryQualifier.restrictQualifier)
    {
        error(location, reason.c_str(), "restrict");
    }
    if (memoryQualifier.volatileQualifier)
    {
        error(location, reason.c_str(), "volatile");
    }
}

// Make sure there is no offset overlapping, and store the newly assigned offset to "type" in
// intermediate tree.
void TParseContext::checkAtomicCounterOffsetDoesNotOverlap(bool forceAppend,
                                                           const TSourceLoc &loc,
                                                           TType *type)
{
    const size_t size = type->isArray() ? kAtomicCounterArrayStride * type->getArraySizeProduct()
                                        : kAtomicCounterSize;
    TLayoutQualifier layoutQualifier = type->getLayoutQualifier();
    auto &bindingState               = mAtomicCounterBindingStates[layoutQualifier.binding];
    int offset;
    if (layoutQualifier.offset == -1 || forceAppend)
    {
        offset = bindingState.appendSpan(size);
    }
    else
    {
        offset = bindingState.insertSpan(layoutQualifier.offset, size);
    }
    if (offset == -1)
    {
        error(loc, "Offset overlapping", "atomic counter");
        return;
    }
    layoutQualifier.offset = offset;
    type->setLayoutQualifier(layoutQualifier);
}

void TParseContext::checkAtomicCounterOffsetAlignment(const TSourceLoc &location, const TType &type)
{
    TLayoutQualifier layoutQualifier = type.getLayoutQualifier();

    // OpenGL ES 3.1 Table 6.5, Atomic counter offset must be a multiple of 4
    if (layoutQualifier.offset % 4 != 0)
    {
        error(location, "Offset must be multiple of 4", "atomic counter");
    }
}

void TParseContext::checkAtomicCounterOffsetLimit(const TSourceLoc &location, const TType &type)
{
    TLayoutQualifier layoutQualifier = type.getLayoutQualifier();

    if (layoutQualifier.offset >= mResources.MaxAtomicCounterBufferSize)
    {
        error(location, "Offset must not exceed the maximum atomic counter buffer size",
              "atomic counter");
    }
}

void TParseContext::checkAtomicCounterOffsetIsValid(bool forceAppend,
                                                    const TSourceLoc &loc,
                                                    TType *type)
{
    checkAtomicCounterOffsetDoesNotOverlap(forceAppend, loc, type);
    checkAtomicCounterOffsetAlignment(loc, *type);
    checkAtomicCounterOffsetLimit(loc, *type);
}

void TParseContext::checkGeometryShaderInputAndSetArraySize(const TSourceLoc &location,
                                                            const ImmutableString &token,
                                                            TType *type,
                                                            GeomTessArray *sizedOut)
{
    if (type->getQualifier() == EvqPerVertexIn && mShaderType == GL_GEOMETRY_SHADER)
    {
        // This is a redeclaration of gl_in, which may be unsized.
        if (!type->isArray())
        {
            error(location, "gl_in must be an array", "gl_in");
            type->makeArray(0);
        }

        // If the size is already determined, set the size / verify it:
        if (mGeometryShaderInputPrimitiveType != EptUndefined)
        {
            ASSERT(mGeometryInputArraySize != 0);
            if (type->getOutermostArraySize() > 0 &&
                type->getOutermostArraySize() != mGeometryInputArraySize)
            {
                error(location, "gl_in array size inconsistent with primitive", "gl_in");
            }
            else if (type->getOutermostArraySize() == 0)
            {
                type->sizeOutermostUnsizedArray(mGeometryInputArraySize);
            }
        }
        else
        {
            warning(location,
                    "Missing a valid input primitive declaration before declaring an unsized "
                    "gl_in array",
                    "Deferred");
            mDeferredArrayTypesToSize.push_back(type);
            *sizedOut = GeomTessArray::Deferred;
        }
    }
    else if (IsGeometryShaderInput(mShaderType, type->getQualifier()))
    {
        if (type->isArray() && type->getOutermostArraySize() == 0u)
        {
            // Set size for the unsized geometry shader inputs if they are declared after a valid
            // input primitive declaration.
            if (mGeometryShaderInputPrimitiveType != EptUndefined)
            {
                ASSERT(symbolTable.getGlInVariableWithArraySize() != nullptr);
                type->sizeOutermostUnsizedArray(
                    symbolTable.getGlInVariableWithArraySize()->getType().getOutermostArraySize());
            }
            else
            {
                // [GLSL ES 3.2 SPEC Chapter 4.4.1.2]
                // An input can be declared without an array size if there is a previous layout
                // which specifies the size.
                warning(location,
                        "Missing a valid input primitive declaration before declaring an unsized "
                        "array input",
                        "Deferred");
                mDeferredArrayTypesToSize.push_back(type);
                *sizedOut = GeomTessArray::Deferred;
            }
        }
        else if (type->isArray())
        {
            setGeometryShaderInputArraySize(type->getOutermostArraySize(), location);
        }
        else
        {
            error(location, "Geometry shader input variable must be declared as an array", token);
        }
    }
}

void TParseContext::checkTessellationShaderUnsizedArraysAndSetSize(const TSourceLoc &location,
                                                                   const ImmutableString &token,
                                                                   TType *type,
                                                                   GeomTessArray *sizedOut)
{
    TQualifier qualifier = type->getQualifier();

    if (qualifier == EvqPerVertexIn && type->isArray() &&
        (mShaderType == GL_TESS_CONTROL_SHADER || mShaderType == GL_TESS_EVALUATION_SHADER))
    {
        // gl_in in both tessellation stages should be sized as gl_MaxPatchVertices
        if (type->getOutermostArraySize() == 0)
        {
            ASSERT(mResources.MaxPatchVertices > 0);
            type->sizeOutermostUnsizedArray(mResources.MaxPatchVertices);
        }
        else if (type->getOutermostArraySize() !=
                 static_cast<unsigned int>(mResources.MaxPatchVertices))
        {
            error(location,
                  "If a size is specified for a tessellation control or evaluation gl_in "
                  "variable, it must match the maximum patch size (gl_MaxPatchVertices).",
                  token);
        }
        return;
    }
    if (qualifier == EvqPerVertexOut && type->isArray() && mShaderType == GL_TESS_CONTROL_SHADER)
    {
        if (type->getOutermostArraySize() == 0)
        {
            if (mTessControlShaderOutputVertices == 0)
            {
                error(location,
                      "Missing a valid vertices declaration before declaring an unsized "
                      "gl_out array",
                      "gl_out");
            }
            else
            {
                type->sizeOutermostUnsizedArray(mTessControlShaderOutputVertices);
            }
        }
        else if (type->getOutermostArraySize() !=
                     static_cast<unsigned int>(mTessControlShaderOutputVertices) &&
                 mTessControlShaderOutputVertices != 0)
        {
            error(location,
                  "If a size is specified for a tessellation control gl_out "
                  "variable, it must match the the number of vertices in the output patch.",
                  token);
        }
        return;
    }

    if (!IsTessellationControlShaderOutput(mShaderType, qualifier) &&
        !IsTessellationControlShaderInput(mShaderType, qualifier) &&
        !IsTessellationEvaluationShaderInput(mShaderType, qualifier))
    {
        return;
    }

    // Such variables must be declared as arrays or inside output blocks declared as arrays.
    if (!type->isArray())
    {
        error(location, "Tessellation interface variables must be declared as an array", token);
        return;
    }

    // If a size is specified, it must match the maximum patch size.
    unsigned int outermostSize = type->getOutermostArraySize();
    if (outermostSize == 0u)
    {
        switch (qualifier)
        {
            case EvqTessControlIn:
            case EvqTessEvaluationIn:
            case EvqSmoothIn:
            case EvqFlatIn:
            case EvqNoPerspectiveIn:
            case EvqCentroidIn:
            case EvqSampleIn:
            case EvqNoPerspectiveCentroidIn:
            case EvqNoPerspectiveSampleIn:
                // Declaring an array size is optional. If no size is specified, it will be taken
                // from the implementation-dependent maximum patch size (gl_MaxPatchVertices).
                ASSERT(mResources.MaxPatchVertices > 0);
                type->sizeOutermostUnsizedArray(mResources.MaxPatchVertices);
                break;
            case EvqTessControlOut:
            case EvqTessEvaluationOut:
            case EvqSmoothOut:
            case EvqFlatOut:
            case EvqNoPerspectiveOut:
            case EvqCentroidOut:
            case EvqSampleOut:
            case EvqNoPerspectiveCentroidOut:
            case EvqNoPerspectiveSampleOut:
                // Declaring an array size is optional. If no size is specified, it will be taken
                // from output patch size declared in the shader.  If the patch size is not yet
                // declared, this is deferred until such time as it does.
                if (mTessControlShaderOutputVertices == 0)
                {
                    mDeferredArrayTypesToSize.push_back(type);
                    *sizedOut = GeomTessArray::Deferred;
                }
                else
                {
                    type->sizeOutermostUnsizedArray(mTessControlShaderOutputVertices);
                }
                break;
            default:
                UNREACHABLE();
                break;
        }
        return;
    }

    if (IsTessellationControlShaderInput(mShaderType, qualifier) ||
        IsTessellationEvaluationShaderInput(mShaderType, qualifier))
    {
        if (outermostSize != static_cast<unsigned int>(mResources.MaxPatchVertices))
        {
            error(location,
                  "If a size is specified for a tessellation control or evaluation user-defined "
                  "input variable, it must match the maximum patch size (gl_MaxPatchVertices).",
                  token);
        }
    }
    else if (IsTessellationControlShaderOutput(mShaderType, qualifier))
    {
        if (outermostSize != static_cast<unsigned int>(mTessControlShaderOutputVertices) &&
            mTessControlShaderOutputVertices != 0)
        {
            error(location,
                  "If a size is specified for a tessellation control user-defined per-vertex "
                  "output variable, it must match the the number of vertices in the output "
                  "patch.",
                  token);
        }
    }
}

TIntermDeclaration *TParseContext::parseSingleDeclaration(
    TPublicType &publicType,
    const TSourceLoc &identifierOrTypeLocation,
    const ImmutableString &identifier)
{
    TType *type = new TType(publicType);
    if ((mCompileOptions.flattenPragmaSTDGLInvariantAll || mCompileOptions.useIR) &&
        mDirectiveHandler.pragma().stdgl.invariantAll)
    {
        TQualifier qualifier = type->getQualifier();

        // The directive handler has already taken care of rejecting invalid uses of this pragma
        // (for example, in ESSL 3.00 fragment shaders), so at this point, flatten it into all
        // affected variable declarations:
        //
        // 1. Built-in special variables which are inputs to the fragment shader. (These are handled
        // elsewhere, in TranslatorGLSL.)
        //
        // 2. Outputs from vertex shaders in ESSL 1.00 and 3.00 (EvqVaryingOut and EvqVertexOut). It
        // is actually less likely that there will be bugs in the handling of ESSL 3.00 shaders, but
        // the way this is currently implemented we have to enable this compiler option before
        // parsing the shader and determining the shading language version it uses. If this were
        // implemented as a post-pass, the workaround could be more targeted.
        if (qualifier == EvqVaryingOut || qualifier == EvqVertexOut)
        {
            type->setInvariant(true);
        }
    }

    if (identifier == "gl_FragDepth")
    {
        if (type->getQualifier() == EvqFragmentOut)
        {
            type->setQualifier(EvqFragDepth);
        }
        else
        {
            error(identifierOrTypeLocation,
                  "gl_FragDepth can only be redeclared as fragment output", identifier);
        }
    }

    GeomTessArray sized = GeomTessArray::Sized;
    checkGeometryShaderInputAndSetArraySize(identifierOrTypeLocation, identifier, type, &sized);
    checkTessellationShaderUnsizedArraysAndSetSize(identifierOrTypeLocation, identifier, type,
                                                   &sized);
    if (sized == GeomTessArray::Sized)
    {
        type->setTypeId(getTypeId(*type));
    }

    declarationQualifierErrorCheck(type->getQualifier(), publicType.layoutQualifier,
                                   identifierOrTypeLocation);

    bool emptyDeclaration                  = (identifier == "");
    mDeferredNonEmptyDeclarationErrorCheck = emptyDeclaration;

    TIntermSymbol *symbol = nullptr;
    if (emptyDeclaration)
    {
        emptyDeclarationErrorCheck(*type, identifierOrTypeLocation);
        // In most cases we don't need to create a symbol node for an empty declaration.
        if (IsAtomicCounter(publicType.getBasicType()))
        {
            setAtomicCounterBindingDefaultOffset(publicType, identifierOrTypeLocation);
        }
    }
    else
    {
        nonEmptyDeclarationErrorCheck(publicType, identifierOrTypeLocation);

        checkCanBeDeclaredWithoutInitializer(identifierOrTypeLocation, identifier, type);
        checkDeclarationIsValidArraySize(identifierOrTypeLocation, identifier, type);

        if (IsAtomicCounter(type->getBasicType()))
        {
            checkAtomicCounterOffsetIsValid(false, identifierOrTypeLocation, type);
        }

        adjustRedeclaredBuiltInType(identifierOrTypeLocation, identifier, type);

        TVariable *variable = nullptr;
        if (declareVariable(identifierOrTypeLocation, identifier, type, sized, &variable))
        {
            symbol = new TIntermSymbol(variable);
        }
    }

    TIntermDeclaration *declaration = new TIntermDeclaration();
    declaration->setLine(identifierOrTypeLocation);
    if (symbol)
    {
        symbol->setLine(identifierOrTypeLocation);
        declaration->appendDeclarator(symbol);
    }
    return declaration;
}

TIntermDeclaration *TParseContext::parseSingleArrayDeclaration(
    TPublicType &elementType,
    const TSourceLoc &identifierLocation,
    const ImmutableString &identifier,
    const TSourceLoc &indexLocation,
    const TVector<unsigned int> &arraySizes)
{
    mDeferredNonEmptyDeclarationErrorCheck = false;

    declarationQualifierErrorCheck(elementType.qualifier, elementType.layoutQualifier,
                                   identifierLocation);

    nonEmptyDeclarationErrorCheck(elementType, identifierLocation);

    checkIsValidTypeAndQualifierForArray(indexLocation, elementType);

    TType *arrayType = new TType(elementType);
    arrayType->makeArrays(arraySizes);

    checkArrayOfArraysInOut(indexLocation, elementType, *arrayType);

    GeomTessArray sized = GeomTessArray::Sized;
    checkGeometryShaderInputAndSetArraySize(indexLocation, identifier, arrayType, &sized);
    checkTessellationShaderUnsizedArraysAndSetSize(indexLocation, identifier, arrayType, &sized);

    checkCanBeDeclaredWithoutInitializer(identifierLocation, identifier, arrayType);
    checkDeclarationIsValidArraySize(identifierLocation, identifier, arrayType);

    if (IsAtomicCounter(arrayType->getBasicType()))
    {
        checkAtomicCounterOffsetIsValid(false, identifierLocation, arrayType);
    }

    adjustRedeclaredBuiltInType(identifierLocation, identifier, arrayType);
    if (sized == GeomTessArray::Sized)
    {
        arrayType->setTypeId(getTypeId(*arrayType));
    }

    TIntermDeclaration *declaration = new TIntermDeclaration();
    declaration->setLine(identifierLocation);

    TVariable *variable = nullptr;
    if (declareVariable(identifierLocation, identifier, arrayType, sized, &variable))
    {
        TIntermSymbol *symbol = new TIntermSymbol(variable);
        symbol->setLine(identifierLocation);
        declaration->appendDeclarator(symbol);
    }

    return declaration;
}

TIntermDeclaration *TParseContext::parseSingleInitDeclaration(const TPublicType &publicType,
                                                              const TSourceLoc &identifierLocation,
                                                              const ImmutableString &identifier,
                                                              const TSourceLoc &initLocation,
                                                              TIntermTyped *initializer)
{
    mDeferredNonEmptyDeclarationErrorCheck = false;

    declarationQualifierErrorCheck(publicType.qualifier, publicType.layoutQualifier,
                                   identifierLocation);

    nonEmptyDeclarationErrorCheck(publicType, identifierLocation);

    TIntermDeclaration *declaration = new TIntermDeclaration();
    declaration->setLine(identifierLocation);

    TIntermBinary *initNode = nullptr;
    TType *type             = new TType(publicType);
    if (executeInitializer(identifierLocation, identifier, type, initializer, &initNode))
    {
        if (initNode)
        {
            declaration->appendDeclarator(initNode);
        }
        else if (publicType.isStructSpecifier())
        {
            // The initialization got constant folded.  If it's a struct, declare the struct anyway.
            TVariable *emptyVariable =
                new TVariable(&symbolTable, kEmptyImmutableString, type, SymbolType::Empty);
            TIntermSymbol *symbol = new TIntermSymbol(emptyVariable);
            symbol->setLine(publicType.getLine());
            declaration->appendDeclarator(symbol);
        }
    }
    return declaration;
}

TIntermDeclaration *TParseContext::parseSingleArrayInitDeclaration(
    TPublicType &elementType,
    const TSourceLoc &identifierLocation,
    const ImmutableString &identifier,
    const TSourceLoc &indexLocation,
    const TVector<unsigned int> &arraySizes,
    const TSourceLoc &initLocation,
    TIntermTyped *initializer)
{
    mDeferredNonEmptyDeclarationErrorCheck = false;

    declarationQualifierErrorCheck(elementType.qualifier, elementType.layoutQualifier,
                                   identifierLocation);

    nonEmptyDeclarationErrorCheck(elementType, identifierLocation);

    checkIsValidTypeAndQualifierForArray(indexLocation, elementType);

    TType *arrayType = new TType(elementType);
    arrayType->makeArrays(arraySizes);

    TIntermDeclaration *declaration = new TIntermDeclaration();
    declaration->setLine(identifierLocation);

    // initNode will correspond to the whole of "type b[n] = initializer".
    TIntermBinary *initNode = nullptr;
    if (executeInitializer(identifierLocation, identifier, arrayType, initializer, &initNode))
    {
        if (initNode)
        {
            declaration->appendDeclarator(initNode);
        }
    }

    return declaration;
}

TIntermGlobalQualifierDeclaration *TParseContext::parseGlobalQualifierDeclaration(
    const TTypeQualifierBuilder &typeQualifierBuilder,
    const TSourceLoc &identifierLoc,
    const ImmutableString &identifier,
    const TSymbol *symbol)
{
    TTypeQualifier typeQualifier = typeQualifierBuilder.getVariableTypeQualifier(mDiagnostics);

    if (!typeQualifier.invariant && !typeQualifier.precise)
    {
        error(identifierLoc, "Expected invariant or precise", identifier);
        return nullptr;
    }
    if (typeQualifier.invariant && !checkIsAtGlobalLevel(identifierLoc, "invariant varying"))
    {
        return nullptr;
    }
    if (!symbol)
    {
        error(identifierLoc, "undeclared identifier declared as invariant or precise", identifier);
        return nullptr;
    }
    if (!IsQualifierUnspecified(typeQualifier.qualifier))
    {
        error(identifierLoc, "invariant or precise declaration specifies qualifier",
              getQualifierString(typeQualifier.qualifier));
    }
    if (typeQualifier.precision != EbpUndefined)
    {
        error(identifierLoc, "invariant or precise declaration specifies precision",
              getPrecisionString(typeQualifier.precision));
    }
    if (!typeQualifier.layoutQualifier.isEmpty())
    {
        error(identifierLoc, "invariant or precise declaration specifies layout", "'layout'");
    }

    const TVariable *variable = getNamedVariable(identifierLoc, identifier, symbol);
    if (!variable)
    {
        return nullptr;
    }
    const TType &type = variable->getType();

    checkInvariantVariableQualifier(typeQualifier.invariant, type.getQualifier(),
                                    typeQualifier.line);
    checkMemoryQualifierIsNotSpecified(typeQualifier.memoryQualifier, typeQualifier.line);

    if (typeQualifier.invariant)
    {
        symbolTable.addInvariantVarying(*variable);
    }

    TIntermSymbol *intermSymbol = new TIntermSymbol(variable);
    intermSymbol->setLine(identifierLoc);

    mBuiltInQualified[type.getQualifier()] = true;

    const ir::VariableId id = declareBuiltInOnFirstUse(variable);
    if (typeQualifier.invariant)
    {
        mIRBuilder.markVariableInvariant(id);
    }
    if (typeQualifier.precise)
    {
        mIRBuilder.markVariablePrecise(id);
    }

    return new TIntermGlobalQualifierDeclaration(intermSymbol, typeQualifier.precise,
                                                 identifierLoc);
}

void TParseContext::parseDeclarator(TPublicType &publicType,
                                    const TSourceLoc &identifierLocation,
                                    const ImmutableString &identifier,
                                    TIntermDeclaration *declarationOut)
{
    // If the declaration starting this declarator list was empty (example: int,), some checks were
    // not performed.
    if (mDeferredNonEmptyDeclarationErrorCheck)
    {
        nonEmptyDeclarationErrorCheck(publicType, identifierLocation);
        mDeferredNonEmptyDeclarationErrorCheck = false;
    }

    checkDeclaratorLocationIsNotSpecified(identifierLocation, publicType);

    TType *type = new TType(publicType);

    GeomTessArray sized = GeomTessArray::Sized;
    checkGeometryShaderInputAndSetArraySize(identifierLocation, identifier, type, &sized);
    checkTessellationShaderUnsizedArraysAndSetSize(identifierLocation, identifier, type, &sized);

    checkCanBeDeclaredWithoutInitializer(identifierLocation, identifier, type);
    checkDeclarationIsValidArraySize(identifierLocation, identifier, type);

    if (IsAtomicCounter(type->getBasicType()))
    {
        checkAtomicCounterOffsetIsValid(true, identifierLocation, type);
    }

    adjustRedeclaredBuiltInType(identifierLocation, identifier, type);
    if (sized == GeomTessArray::Sized)
    {
        type->setTypeId(getTypeId(*type));
    }

    TVariable *variable = nullptr;
    if (declareVariable(identifierLocation, identifier, type, sized, &variable))
    {
        TIntermSymbol *symbol = new TIntermSymbol(variable);
        symbol->setLine(identifierLocation);
        declarationOut->appendDeclarator(symbol);
    }
}

void TParseContext::parseArrayDeclarator(TPublicType &elementType,
                                         const TSourceLoc &identifierLocation,
                                         const ImmutableString &identifier,
                                         const TSourceLoc &arrayLocation,
                                         const TVector<unsigned int> &arraySizes,
                                         TIntermDeclaration *declarationOut)
{
    // If the declaration starting this declarator list was empty (example: int,), some checks were
    // not performed.
    if (mDeferredNonEmptyDeclarationErrorCheck)
    {
        nonEmptyDeclarationErrorCheck(elementType, identifierLocation);
        mDeferredNonEmptyDeclarationErrorCheck = false;
    }

    checkDeclaratorLocationIsNotSpecified(identifierLocation, elementType);

    if (checkIsValidTypeAndQualifierForArray(arrayLocation, elementType))
    {
        TType *arrayType = new TType(elementType);
        arrayType->makeArrays(arraySizes);

        GeomTessArray sized = GeomTessArray::Sized;
        checkGeometryShaderInputAndSetArraySize(identifierLocation, identifier, arrayType, &sized);
        checkTessellationShaderUnsizedArraysAndSetSize(identifierLocation, identifier, arrayType,
                                                       &sized);

        checkCanBeDeclaredWithoutInitializer(identifierLocation, identifier, arrayType);
        checkDeclarationIsValidArraySize(identifierLocation, identifier, arrayType);

        if (IsAtomicCounter(arrayType->getBasicType()))
        {
            checkAtomicCounterOffsetDoesNotOverlap(true, identifierLocation, arrayType);

            checkAtomicCounterOffsetAlignment(identifierLocation, *arrayType);
        }

        adjustRedeclaredBuiltInType(identifierLocation, identifier, arrayType);
        if (sized == GeomTessArray::Sized)
        {
            arrayType->setTypeId(getTypeId(*arrayType));
        }

        TVariable *variable = nullptr;
        if (declareVariable(identifierLocation, identifier, arrayType, sized, &variable))
        {
            TIntermSymbol *symbol = new TIntermSymbol(variable);
            symbol->setLine(identifierLocation);
            declarationOut->appendDeclarator(symbol);
        }
    }
}

void TParseContext::parseInitDeclarator(const TPublicType &publicType,
                                        const TSourceLoc &identifierLocation,
                                        const ImmutableString &identifier,
                                        const TSourceLoc &initLocation,
                                        TIntermTyped *initializer,
                                        TIntermDeclaration *declarationOut)
{
    // If the declaration starting this declarator list was empty (example: int,), some checks were
    // not performed.
    if (mDeferredNonEmptyDeclarationErrorCheck)
    {
        nonEmptyDeclarationErrorCheck(publicType, identifierLocation);
        mDeferredNonEmptyDeclarationErrorCheck = false;
    }

    checkDeclaratorLocationIsNotSpecified(identifierLocation, publicType);

    TIntermBinary *initNode = nullptr;
    TType *type             = new TType(publicType);
    if (executeInitializer(identifierLocation, identifier, type, initializer, &initNode))
    {
        //
        // build the intermediate representation
        //
        if (initNode)
        {
            declarationOut->appendDeclarator(initNode);
        }
    }
}

void TParseContext::parseArrayInitDeclarator(const TPublicType &elementType,
                                             const TSourceLoc &identifierLocation,
                                             const ImmutableString &identifier,
                                             const TSourceLoc &indexLocation,
                                             const TVector<unsigned int> &arraySizes,
                                             const TSourceLoc &initLocation,
                                             TIntermTyped *initializer,
                                             TIntermDeclaration *declarationOut)
{
    // If the declaration starting this declarator list was empty (example: int,), some checks were
    // not performed.
    if (mDeferredNonEmptyDeclarationErrorCheck)
    {
        nonEmptyDeclarationErrorCheck(elementType, identifierLocation);
        mDeferredNonEmptyDeclarationErrorCheck = false;
    }

    checkDeclaratorLocationIsNotSpecified(identifierLocation, elementType);

    checkIsValidTypeAndQualifierForArray(indexLocation, elementType);

    TType *arrayType = new TType(elementType);
    arrayType->makeArrays(arraySizes);

    // initNode will correspond to the whole of "b[n] = initializer".
    TIntermBinary *initNode = nullptr;
    if (executeInitializer(identifierLocation, identifier, arrayType, initializer, &initNode))
    {
        if (initNode)
        {
            declarationOut->appendDeclarator(initNode);
        }
    }
}

TIntermNode *TParseContext::addEmptyStatement(const TSourceLoc &location)
{
    // It's simpler to parse an empty statement as a constant expression rather than having a
    // different type of node just for empty statements, that will be pruned from the AST anyway.
    TIntermNode *node = CreateZeroNode(TType(EbtInt, EbpMedium));
    node->setLine(location);
    // Because the node that is pushed has a value, appendStatement will expect to pop something
    // from the stack.  So push the same bogus value to the IR too.
    mIRBuilder.pushConstantInt(0);
    return node;
}

void TParseContext::setAtomicCounterBindingDefaultOffset(const TPublicType &publicType,
                                                         const TSourceLoc &location)
{
    const TLayoutQualifier &layoutQualifier = publicType.layoutQualifier;
    checkAtomicCounterBindingIsValid(location, layoutQualifier.binding);
    if (layoutQualifier.binding == -1 || layoutQualifier.offset == -1)
    {
        error(location, "Requires both binding and offset", "layout");
        return;
    }
    mAtomicCounterBindingStates[layoutQualifier.binding].setDefaultOffset(layoutQualifier.offset);
}

void TParseContext::parseDefaultPrecisionQualifier(const TPrecision precision,
                                                   const TPublicType &type,
                                                   const TSourceLoc &loc)
{
    if (!CanSetDefaultPrecisionOnType(type))
    {
        error(loc, "illegal type argument for default precision qualifier",
              getBasicString(type.getBasicType()));
        return;
    }
    symbolTable.setDefaultPrecision(type.getBasicType(), precision);
}

bool TParseContext::checkPrimitiveTypeMatchesTypeQualifier(const TTypeQualifier &typeQualifier)
{
    switch (typeQualifier.layoutQualifier.primitiveType)
    {
        case EptLines:
        case EptLinesAdjacency:
        case EptTriangles:
        case EptTrianglesAdjacency:
            return typeQualifier.qualifier == EvqGeometryIn;

        case EptLineStrip:
        case EptTriangleStrip:
            return typeQualifier.qualifier == EvqGeometryOut;

        case EptPoints:
            return true;

        default:
            UNREACHABLE();
            return false;
    }
}

void TParseContext::setGeometryShaderInputArraySize(unsigned int inputArraySize,
                                                    const TSourceLoc &line)
{
    if (!symbolTable.setGlInArraySize(inputArraySize, getShaderVersion()))
    {
        error(line,
              "Array size or input primitive declaration doesn't match the size of earlier sized "
              "array inputs.",
              "layout");
    }
    mGeometryInputArraySize = inputArraySize;
}

bool TParseContext::parseGeometryShaderInputLayoutQualifier(const TTypeQualifier &typeQualifier)
{
    ASSERT(typeQualifier.qualifier == EvqGeometryIn);

    const TLayoutQualifier &layoutQualifier = typeQualifier.layoutQualifier;

    if (layoutQualifier.maxVertices != -1)
    {
        error(typeQualifier.line,
              "max_vertices can only be declared in 'out' layout in a geometry shader", "layout");
        return false;
    }

    // Set mGeometryInputPrimitiveType if exists
    if (layoutQualifier.primitiveType != EptUndefined)
    {
        if (!checkPrimitiveTypeMatchesTypeQualifier(typeQualifier))
        {
            error(typeQualifier.line, "invalid primitive type for 'in' layout", "layout");
            return false;
        }

        if (mGeometryShaderInputPrimitiveType == EptUndefined)
        {
            mGeometryShaderInputPrimitiveType = layoutQualifier.primitiveType;
            const GLuint inputArraySize =
                GetGeometryShaderInputArraySize(mGeometryShaderInputPrimitiveType);

            // Size any implicitly sized arrays that have already been declared.  Done before
            // verifying gl_in's array size, since that could also need to be sized.
            sizeUnsizedArrayTypes(inputArraySize);
            setGeometryShaderInputArraySize(inputArraySize, typeQualifier.line);

            mIRBuilder.setGsPrimitiveIn(mGeometryShaderInputPrimitiveType);
        }
        else if (mGeometryShaderInputPrimitiveType != layoutQualifier.primitiveType)
        {
            error(typeQualifier.line, "primitive doesn't match earlier input primitive declaration",
                  "layout");
            return false;
        }
    }

    // Set mGeometryInvocations if exists
    if (layoutQualifier.invocations > 0)
    {
        if (mGeometryShaderInvocations == 0)
        {
            mGeometryShaderInvocations = layoutQualifier.invocations;
            mIRBuilder.setGsInvocations(mGeometryShaderInvocations);
        }
        else if (mGeometryShaderInvocations != layoutQualifier.invocations)
        {
            error(typeQualifier.line, "invocations contradicts to the earlier declaration",
                  "layout");
            return false;
        }
    }

    return true;
}

bool TParseContext::parseGeometryShaderOutputLayoutQualifier(const TTypeQualifier &typeQualifier)
{
    ASSERT(typeQualifier.qualifier == EvqGeometryOut);

    const TLayoutQualifier &layoutQualifier = typeQualifier.layoutQualifier;

    if (layoutQualifier.invocations > 0)
    {
        error(typeQualifier.line,
              "invocations can only be declared in 'in' layout in a geometry shader", "layout");
        return false;
    }

    // Set mGeometryOutputPrimitiveType if exists
    if (layoutQualifier.primitiveType != EptUndefined)
    {
        if (!checkPrimitiveTypeMatchesTypeQualifier(typeQualifier))
        {
            error(typeQualifier.line, "invalid primitive type for 'out' layout", "layout");
            return false;
        }

        if (mGeometryShaderOutputPrimitiveType == EptUndefined)
        {
            mGeometryShaderOutputPrimitiveType = layoutQualifier.primitiveType;
            mIRBuilder.setGsPrimitiveOut(mGeometryShaderOutputPrimitiveType);
        }
        else if (mGeometryShaderOutputPrimitiveType != layoutQualifier.primitiveType)
        {
            error(typeQualifier.line,
                  "primitive doesn't match earlier output primitive declaration", "layout");
            return false;
        }
    }

    // Set mGeometryMaxVertices if exists
    if (layoutQualifier.maxVertices > -1)
    {
        if (mGeometryShaderMaxVertices == -1)
        {
            mGeometryShaderMaxVertices = layoutQualifier.maxVertices;
            mIRBuilder.setGsMaxVertices(mGeometryShaderMaxVertices);
        }
        else if (mGeometryShaderMaxVertices != layoutQualifier.maxVertices)
        {
            error(typeQualifier.line, "max_vertices contradicts to the earlier declaration",
                  "layout");
            return false;
        }
    }

    return true;
}

bool TParseContext::parseTessControlShaderOutputLayoutQualifier(const TTypeQualifier &typeQualifier)
{
    ASSERT(typeQualifier.qualifier == EvqTessControlOut);

    const TLayoutQualifier &layoutQualifier = typeQualifier.layoutQualifier;

    if (layoutQualifier.vertices == 0)
    {
        error(typeQualifier.line, "No vertices specified", "layout");
        return false;
    }

    // Set mTessControlShaderOutputVertices if exists
    if (mTessControlShaderOutputVertices == 0)
    {
        mTessControlShaderOutputVertices = layoutQualifier.vertices;
        mIRBuilder.setTcsVertices(mTessControlShaderOutputVertices);

        // Size any implicitly sized arrays that have already been declared.
        sizeUnsizedArrayTypes(mTessControlShaderOutputVertices);
    }
    else
    {
        error(typeQualifier.line, "Duplicated vertices specified", "layout");
    }
    return true;
}

bool TParseContext::parseTessEvaluationShaderInputLayoutQualifier(
    const TTypeQualifier &typeQualifier)
{
    ASSERT(typeQualifier.qualifier == EvqTessEvaluationIn);

    const TLayoutQualifier &layoutQualifier = typeQualifier.layoutQualifier;

    // Set mTessEvaluationShaderInputPrimitiveType if exists
    if (layoutQualifier.tesPrimitiveType != EtetUndefined)
    {
        if (mTessEvaluationShaderInputPrimitiveType == EtetUndefined)
        {
            mTessEvaluationShaderInputPrimitiveType = layoutQualifier.tesPrimitiveType;
            mIRBuilder.setTesPrimitive(mTessEvaluationShaderInputPrimitiveType);
        }
        else
        {
            error(typeQualifier.line, "Duplicated primitive type declaration", "layout");
        }
    }
    // Set mTessEvaluationShaderVertexSpacingType if exists
    if (layoutQualifier.tesVertexSpacingType != EtetUndefined)
    {
        if (mTessEvaluationShaderInputVertexSpacingType == EtetUndefined)
        {
            mTessEvaluationShaderInputVertexSpacingType = layoutQualifier.tesVertexSpacingType;
            mIRBuilder.setTesVertexSpacing(mTessEvaluationShaderInputVertexSpacingType);
        }
        else
        {
            error(typeQualifier.line, "Duplicated vertex spacing declaration", "layout");
        }
    }
    // Set mTessEvaluationShaderInputOrderingType if exists
    if (layoutQualifier.tesOrderingType != EtetUndefined)
    {
        if (mTessEvaluationShaderInputOrderingType == EtetUndefined)
        {
            mTessEvaluationShaderInputOrderingType = layoutQualifier.tesOrderingType;
            mIRBuilder.setTesOrdering(mTessEvaluationShaderInputOrderingType);
        }
        else
        {
            error(typeQualifier.line, "Duplicated ordering declaration", "layout");
        }
    }
    // Set mTessEvaluationShaderInputPointType if exists
    if (layoutQualifier.tesPointType != EtetUndefined)
    {
        if (mTessEvaluationShaderInputPointType == EtetUndefined)
        {
            mTessEvaluationShaderInputPointType = layoutQualifier.tesPointType;
            mIRBuilder.setTesPointMode(mTessEvaluationShaderInputPointType);
        }
        else
        {
            error(typeQualifier.line, "Duplicated point type declaration", "layout");
        }
    }

    return true;
}

void TParseContext::sizeUnsizedArrayTypes(uint32_t arraySize)
{
    while (!mDeferredArrayTypesToSize.empty())
    {
        TType *type = mDeferredArrayTypesToSize.back();

        // Pop the type out of |mDeferredArrayTypesToSize| to satisfy an ASSERT in |getTypeId| that
        // the type declaration is not deferred!
        mDeferredArrayTypesToSize.pop_back();

        type->sizeOutermostUnsizedArray(arraySize);
        type->setTypeId(getTypeId(*type));
    }

    for (const TVariable *variable : mDeferredArrayVariablesToSize)
    {
        declareIRVariable(variable, GeomTessArray::Sized);
    }
    mDeferredArrayVariablesToSize.clear();
}

void TParseContext::parseGlobalLayoutQualifier(const TTypeQualifierBuilder &typeQualifierBuilder)
{
    TTypeQualifier typeQualifier = typeQualifierBuilder.getVariableTypeQualifier(mDiagnostics);
    const TLayoutQualifier layoutQualifier = typeQualifier.layoutQualifier;

    checkInvariantVariableQualifier(typeQualifier.invariant, typeQualifier.qualifier,
                                    typeQualifier.line);

    // It should never be the case, but some strange parser errors can send us here.
    if (layoutQualifier.isEmpty())
    {
        error(typeQualifier.line, "Error during layout qualifier parsing.", "?");
        return;
    }

    if (!layoutQualifier.isCombinationValid())
    {
        error(typeQualifier.line, "invalid layout qualifier combination", "layout");
        return;
    }

    checkIndexIsNotSpecified(typeQualifier.line, layoutQualifier.index);

    checkBindingIsNotSpecified(typeQualifier.line, layoutQualifier.binding);

    checkMemoryQualifierIsNotSpecified(typeQualifier.memoryQualifier, typeQualifier.line);

    checkInternalFormatIsNotSpecified(typeQualifier.line, layoutQualifier.imageInternalFormat);

    checkDepthIsNotSpecified(typeQualifier.line, layoutQualifier.depth);

    checkYuvIsNotSpecified(typeQualifier.line, layoutQualifier.yuv);

    checkOffsetIsNotSpecified(typeQualifier.line, layoutQualifier.offset);

    checkStd430IsForShaderStorageBlock(typeQualifier.line, layoutQualifier.blockStorage,
                                       typeQualifier.qualifier);

    checkAdvancedBlendEquationsNotSpecified(
        typeQualifier.line, layoutQualifier.advancedBlendEquations, typeQualifier.qualifier);

    if (typeQualifier.qualifier != EvqFragmentIn)
    {
        checkEarlyFragmentTestsIsNotSpecified(typeQualifier.line,
                                              layoutQualifier.earlyFragmentTests);
    }

    if (typeQualifier.qualifier == EvqComputeIn)
    {
        if (mComputeShaderLocalSizeDeclared &&
            !layoutQualifier.isLocalSizeEqual(mComputeShaderLocalSize))
        {
            error(typeQualifier.line, "Work group size does not match the previous declaration",
                  "layout");
            return;
        }

        if (mShaderVersion < 310)
        {
            error(typeQualifier.line, "in type qualifier supported in GLSL ES 3.10 only", "layout");
            return;
        }

        if (!layoutQualifier.localSize.isAnyValueSet())
        {
            error(typeQualifier.line, "No local work group size specified", "layout");
            return;
        }

        const TVariable *maxComputeWorkGroupSize = static_cast<const TVariable *>(
            symbolTable.findBuiltIn(ImmutableString("gl_MaxComputeWorkGroupSize"), mShaderVersion));

        const TConstantUnion *maxComputeWorkGroupSizeData =
            maxComputeWorkGroupSize->getConstPointer();

        for (size_t i = 0u; i < layoutQualifier.localSize.size(); ++i)
        {
            if (layoutQualifier.localSize[i] != -1)
            {
                mComputeShaderLocalSize[i]             = layoutQualifier.localSize[i];
                const int maxComputeWorkGroupSizeValue =
                    ANGLE_UNSAFE_TODO(maxComputeWorkGroupSizeData[i]).getIConst();
                if (mComputeShaderLocalSize[i] < 1 ||
                    mComputeShaderLocalSize[i] > maxComputeWorkGroupSizeValue)
                {
                    std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
                    reasonStream << "invalid value: Value must be at least 1 and no greater than "
                                 << maxComputeWorkGroupSizeValue;
                    const std::string &reason = reasonStream.str();

                    error(typeQualifier.line, reason.c_str(), getWorkGroupSizeString(i));
                    return;
                }
            }
        }

        mComputeShaderLocalSizeDeclared = true;
    }
    else if (typeQualifier.qualifier == EvqGeometryIn)
    {
        if (mShaderVersion < 310)
        {
            error(typeQualifier.line, "in type qualifier supported in GLSL ES 3.10 only", "layout");
            return;
        }

        if (!parseGeometryShaderInputLayoutQualifier(typeQualifier))
        {
            return;
        }
    }
    else if (typeQualifier.qualifier == EvqGeometryOut)
    {
        if (mShaderVersion < 310)
        {
            error(typeQualifier.line, "out type qualifier supported in GLSL ES 3.10 only",
                  "layout");
            return;
        }

        if (!parseGeometryShaderOutputLayoutQualifier(typeQualifier))
        {
            return;
        }
    }
    else if (anyMultiviewExtensionAvailable() && typeQualifier.qualifier == EvqVertexIn)
    {
        // This error is only specified in WebGL, but tightens unspecified behavior in the native
        // specification.
        if (mNumViews != -1 && layoutQualifier.numViews != mNumViews)
        {
            error(typeQualifier.line, "Number of views does not match the previous declaration",
                  "layout");
            return;
        }

        if (layoutQualifier.numViews == -1)
        {
            error(typeQualifier.line, "No num_views specified", "layout");
            return;
        }

        if (layoutQualifier.numViews > mResources.MaxViewsOVR)
        {
            error(typeQualifier.line, "num_views greater than the value of GL_MAX_VIEWS_OVR",
                  "layout");
            return;
        }

        mNumViews = layoutQualifier.numViews;
        mIRBuilder.setNumViews(mNumViews);
    }
    else if (typeQualifier.qualifier == EvqFragmentIn)
    {
        if (mShaderVersion < 310)
        {
            error(typeQualifier.line,
                  "in type qualifier without variable declaration supported in GLSL ES 3.10 and "
                  "after",
                  "layout");
            return;
        }

        if (!layoutQualifier.earlyFragmentTests)
        {
            error(typeQualifier.line,
                  "only early_fragment_tests is allowed as layout qualifier when not declaring a "
                  "variable",
                  "layout");
            return;
        }

        mEarlyFragmentTestsSpecified = true;
        mIRBuilder.setEarlyFragmentTests(mEarlyFragmentTestsSpecified);
    }
    else if (typeQualifier.qualifier == EvqFragmentOut)
    {
        if (mShaderVersion < 320 && !isExtensionEnabled(TExtension::KHR_blend_equation_advanced))
        {
            error(typeQualifier.line,
                  "out type qualifier without variable declaration is supported in GLSL ES 3.20,"
                  " or if GL_KHR_blend_equation_advanced is enabled",
                  "layout");
            return;
        }

        if (!layoutQualifier.advancedBlendEquations.any())
        {
            error(typeQualifier.line,
                  "only blend equations are allowed as layout qualifier when not declaring a "
                  "variable",
                  "layout");
            return;
        }

        errorIfPLSDeclared(typeQualifier.line, PLSIllegalOperations::EnableAdvancedBlendEquation);
        mAdvancedBlendEquations |= layoutQualifier.advancedBlendEquations;
    }
    else if (typeQualifier.qualifier == EvqTessControlOut)
    {
        if (mShaderVersion < 310)
        {
            error(typeQualifier.line, "out type qualifier supported in GLSL ES 3.10 and after",
                  "layout");
            return;
        }

        if (!parseTessControlShaderOutputLayoutQualifier(typeQualifier))
        {
            return;
        }
    }
    else if (typeQualifier.qualifier == EvqTessEvaluationIn)
    {
        if (mShaderVersion < 310)
        {
            error(typeQualifier.line, "in type qualifier supported in GLSL ES 3.10 and after",
                  "layout");
            return;
        }

        if (!parseTessEvaluationShaderInputLayoutQualifier(typeQualifier))
        {
            return;
        }
    }
    else
    {
        if (!checkWorkGroupSizeIsNotSpecified(typeQualifier.line, layoutQualifier))
        {
            return;
        }

        if (typeQualifier.qualifier != EvqUniform && typeQualifier.qualifier != EvqBuffer)
        {
            error(typeQualifier.line, "invalid qualifier: global layout can only be set for blocks",
                  getQualifierString(typeQualifier.qualifier));
            return;
        }

        if (mShaderVersion < 300)
        {
            error(typeQualifier.line, "layout qualifiers supported in GLSL ES 3.00 and after",
                  "layout");
            return;
        }

        checkLocationIsNotSpecified(typeQualifier.line, layoutQualifier);

        if (layoutQualifier.matrixPacking != EmpUnspecified)
        {
            if (typeQualifier.qualifier == EvqUniform)
            {
                mDefaultUniformMatrixPacking = layoutQualifier.matrixPacking;
            }
            else if (typeQualifier.qualifier == EvqBuffer)
            {
                mDefaultBufferMatrixPacking = layoutQualifier.matrixPacking;
            }
        }

        if (layoutQualifier.blockStorage != EbsUnspecified)
        {
            if (typeQualifier.qualifier == EvqUniform)
            {
                mDefaultUniformBlockStorage = layoutQualifier.blockStorage;
            }
            else if (typeQualifier.qualifier == EvqBuffer)
            {
                mDefaultBufferBlockStorage = layoutQualifier.blockStorage;
            }
        }
    }
}

TIntermFunctionPrototype *TParseContext::createPrototypeNodeFromFunction(
    const TFunction &function,
    const TSourceLoc &location,
    bool insertParametersToSymbolTable)
{
    checkIsNotReserved(location, function.name());

    TIntermFunctionPrototype *prototype = new TIntermFunctionPrototype(&function);
    prototype->setLine(location);

    for (size_t i = 0; i < function.getParamCount(); i++)
    {
        const TVariable *param = function.getParam(i);

        // If the parameter has no name, it's not an error, just don't add it to symbol table (could
        // be used for unused args).
        if (param->symbolType() != SymbolType::Empty)
        {
            if (insertParametersToSymbolTable)
            {
                if (!symbolTable.declare(const_cast<TVariable *>(param)))
                {
                    error(location, "redefinition", param->name());
                }
            }
            // Unsized type of a named parameter should have already been checked and sanitized.
            ASSERT(!param->getType().isUnsizedArray());
        }
    }
    return prototype;
}

TIntermFunctionPrototype *TParseContext::addFunctionPrototypeDeclaration(
    const TFunction &parsedFunction,
    const TSourceLoc &location)
{
    // Note: function found from the symbol table could be the same as parsedFunction if this is the
    // first declaration. Either way the instance in the symbol table is used to track whether the
    // function is declared multiple times.
    bool hadPrototypeDeclaration = false;
    const TFunction *function    = symbolTable.markFunctionHasPrototypeDeclaration(
        parsedFunction.getMangledName(), &hadPrototypeDeclaration);

    if (hadPrototypeDeclaration && mShaderVersion == 100)
    {
        // ESSL 1.00.17 section 4.2.7.
        // Doesn't apply to ESSL 3.00.4: see section 4.2.3.
        error(location, "duplicate function prototype declarations are not allowed", "function");
    }

    TIntermFunctionPrototype *prototype =
        createPrototypeNodeFromFunction(*function, location, false);

    symbolTable.pop();

    if (!symbolTable.atGlobalLevel())
    {
        // ESSL 3.00.4 section 4.2.4.
        error(location, "local function prototype declarations are not allowed", "function");
    }

    // Declare the function to the IR.  It's body is not yet specified.
    declareFunction(function, FunctionDeclaration::Prototype);
    return prototype;
}

TIntermFunctionDefinition *TParseContext::addFunctionDefinition(
    TIntermFunctionPrototype *functionPrototype,
    TIntermBlock *functionBody,
    const TSourceLoc &location)
{
    ASSERT(functionPrototype->getFunction() == mCurrentFunction);

    // Undo push at end of parseFunctionDefinitionHeader() below for ESSL1.00 case
    if (mFunctionBodyNewScope)
    {
        mFunctionBodyNewScope = false;
        symbolTable.pop();
    }

    // Check that non-void functions have at least one return statement.
    if (mCurrentFunction->getReturnType().getBasicType() != EbtVoid && !mFunctionReturnsValue)
    {
        error(location, "Function does not return a value", mCurrentFunction->name());
    }
    if (mCompileOptions.limitExpressionComplexity &&
        mCurrentFunction->getParamCount() >
            static_cast<unsigned int>(mResources.MaxFunctionParameters))
    {
        error(location, "Function has too many parameters", mCurrentFunction->name());
    }

    for (size_t paramIndex = 0; paramIndex < mCurrentFunction->getParamCount(); ++paramIndex)
    {
        const TVariable *param = mCurrentFunction->getParam(paramIndex);
        checkVariableSize(functionPrototype->getLine(), param->name(), &param->getType());
    }

    if (functionBody == nullptr)
    {
        functionBody = new TIntermBlock();
        functionBody->setLine(location);
    }
    TIntermFunctionDefinition *functionNode =
        new TIntermFunctionDefinition(functionPrototype, functionBody);
    functionNode->setLine(location);

    if (mDeclaringMain)
    {
        mMainFunction = mCurrentFunction;
    }
    mCurrentFunction = nullptr;

    mIRBuilder.endFunction();

    symbolTable.pop();
    return functionNode;
}

void TParseContext::parseFunctionDefinitionHeader(const TSourceLoc &location,
                                                  const TFunction *function,
                                                  TIntermFunctionPrototype **prototypeOut)
{
    ASSERT(function);

    bool wasDefined = false;
    function        = symbolTable.setFunctionParameterNamesFromDefinition(function, &wasDefined);
    if (wasDefined)
    {
        error(location, "function already has a body", function->name());
    }

    // Remember the return type for later checking for return statements.
    mCurrentFunction      = function;
    mFunctionReturnsValue = false;
    // The function is about to be defined
    mDefinedFunctions.insert(function);

    *prototypeOut = createPrototypeNodeFromFunction(*function, location, true);
    ASSERT(mControlFlow.empty());

    // ESSL 1.00 spec allows for variable in function body to redefine parameter
    if (IsSpecWithFunctionBodyNewScope(mShaderSpec, mShaderVersion))
    {
        mFunctionBodyNewScope = true;
        symbolTable.push();
    }

    // If the function prototype hasn't been previously encountered, this is a new function that
    // should be declared to the IR first.
    declareFunction(function, FunctionDeclaration::Definition);
    mIRBuilder.beginFunction(mFunctionToId.at(function));
}

TFunction *TParseContext::parseFunctionDeclarator(const TSourceLoc &location, TFunction *function)
{
    //
    // We don't know at this point whether this is a function definition or a prototype.
    // The definition production code will check for redefinitions.
    // In the case of ESSL 1.00 the prototype production code will also check for redeclarations.
    //

    for (size_t i = 0u; i < function->getParamCount(); ++i)
    {
        const TVariable *param = function->getParam(i);
        const TType &paramType = param->getType();

        checkPrecisionSpecified(location, paramType.getPrecision(), paramType.getBasicType());
    }

    if (getShaderVersion() >= 300)
    {
        if (symbolTable.isUnmangledBuiltInName(function->name(), getShaderVersion(),
                                               extensionBehavior()))
        {
            // With ESSL 3.00 and above, names of built-in functions cannot be redeclared as
            // functions. Therefore overloading or redefining builtin functions is an error.
            error(location, "Name of a built-in function cannot be redeclared as function",
                  function->name());
        }
    }
    else
    {
        // ESSL 1.00.17 section 4.2.6: built-ins can be overloaded but not redefined. We assume that
        // this applies to redeclarations as well.
        const TSymbol *builtIn =
            symbolTable.findBuiltIn(function->getMangledName(), getShaderVersion());
        if (builtIn)
        {
            error(location, "built-in functions cannot be redefined", function->name());
        }
    }

    // Return types and parameter qualifiers must match in all redeclarations, so those are checked
    // here.
    const TFunction *prevDec =
        static_cast<const TFunction *>(symbolTable.findGlobal(function->getMangledName()));
    if (prevDec)
    {
        if (prevDec->getReturnType() != function->getReturnType())
        {
            error(location, "function must have the same return type in all of its declarations",
                  function->getReturnType().getBasicString());
        }
        for (size_t i = 0; i < prevDec->getParamCount(); ++i)
        {
            if (prevDec->getParam(i)->getType().getQualifier() !=
                function->getParam(i)->getType().getQualifier())
            {
                error(location,
                      "function must have the same parameter qualifiers in all of its declarations",
                      function->getParam(i)->getType().getQualifierString());
            }
        }
    }

    // Check for previously declared variables using the same name.
    const TSymbol *prevSym   = symbolTable.find(function->name(), getShaderVersion());
    bool insertUnmangledName = true;
    if (prevSym)
    {
        if (!prevSym->isFunction())
        {
            error(location, "redefinition of a function", function->name());
        }
        insertUnmangledName = false;
    }
    // Parsing is at the inner scope level of the function's arguments and body statement at this
    // point, but declareUserDefinedFunction takes care of declaring the function at the global
    // scope.
    symbolTable.declareUserDefinedFunction(function, insertUnmangledName);

    // Raise error message if main function takes any parameters or return anything other than void
    if (function->isMain())
    {
        if (function->getParamCount() > 0)
        {
            error(location, "function cannot take any parameter(s)", "main");
        }
        if (function->getReturnType().getBasicType() != EbtVoid)
        {
            error(location, "main function cannot return a value",
                  function->getReturnType().getBasicString());
        }
    }

    mDeclaringMain         = function->isMain();
    mIsReturnVisitedInMain = false;

    //
    // If this is a redeclaration, it could also be a definition, in which case, we want to use the
    // variable names from this one, and not the one that's
    // being redeclared.  So, pass back up this declaration, not the one in the symbol table.
    //
    return function;
}

TFunction *TParseContext::parseFunctionHeader(const TPublicType &type,
                                              const ImmutableString &name,
                                              const TSourceLoc &location)
{
    if (type.qualifier != EvqGlobal && type.qualifier != EvqTemporary)
    {
        error(location, "no qualifiers allowed for function return",
              getQualifierString(type.qualifier));
    }
    if (!type.layoutQualifier.isEmpty())
    {
        error(location, "no qualifiers allowed for function return", "layout");
    }
    // make sure an opaque type is not involved as well...
    std::string reason(getBasicString(type.getBasicType()));
    reason += "s can't be function return values";
    checkIsNotOpaqueType(location, type.typeSpecifierNonArray, reason.c_str());
    if (mShaderVersion < 300)
    {
        // Array return values are forbidden, but there's also no valid syntax for declaring array
        // return values in ESSL 1.00.
        ASSERT(!type.isArray() || mDiagnostics->numErrors() > 0);

        if (type.isStructureContainingArrays())
        {
            // ESSL 1.00.17 section 6.1 Function Definitions
            TInfoSinkBase typeString;
            typeString << TType(type);
            error(location, "structures containing arrays can't be function return values",
                  typeString.c_str());
        }
    }

    // Add the function as a prototype after parsing it (we do not support recursion)
    return new TFunction(&symbolTable, name, SymbolType::UserDefined, new TType(type), false);
}

TFunctionLookup *TParseContext::addNonConstructorFunc(const ImmutableString &name,
                                                      const TSymbol *symbol)
{
    return TFunctionLookup::CreateFunctionCall(name, symbol);
}

TFunctionLookup *TParseContext::addConstructorFunc(const TPublicType &publicType)
{
    if (mShaderVersion < 300 && publicType.isArray())
    {
        error(publicType.getLine(), "array constructor supported in GLSL ES 3.00 and above only",
              "[]");
    }
    if (publicType.isStructSpecifierForValidation())
    {
        error(publicType.getLine(), "constructor can't be a structure definition",
              getBasicString(publicType.getBasicType()));
    }

    TType *type = new TType(publicType);
    if (!type->canBeConstructed())
    {
        error(publicType.getLine(), "cannot construct this type",
              getBasicString(publicType.getBasicType()));
        type->setBasicType(EbtFloat);
    }
    return TFunctionLookup::CreateConstructor(type);
}

void TParseContext::checkIsNotUnsizedArray(const TSourceLoc &line,
                                           const char *errorMessage,
                                           const ImmutableString &token,
                                           TType *arrayType)
{
    if (arrayType->isUnsizedArray())
    {
        error(line, errorMessage, token);
        arrayType->sizeUnsizedArrays(angle::Span<const unsigned int>());
    }
}

TParameter TParseContext::parseParameterDeclarator(const TPublicType &type,
                                                   const ImmutableString &name,
                                                   const TSourceLoc &nameLoc)
{
    if (!name.empty())
    {
        if (type.getBasicType() == EbtVoid)
        {
            error(nameLoc, "illegal use of type 'void'", name);
        }
    }
    if (type.isStructSpecifierForValidation())
    {
        // ESSL 3.00.6 section 12.10.
        error(nameLoc, "Function parameter type cannot be a structure definition", name);
    }
    checkIsNotReserved(nameLoc, name);
    TParameter param{name.data(), type};
    if (param.type.isUnsizedArray())
    {
        error(nameLoc, "function parameter array must specify a size", name);
        param.type.sizeUnsizedArrays();
    }
    return param;
}

TParameter TParseContext::parseParameterArrayDeclarator(const TPublicType &elementType,
                                                        const ImmutableString &name,
                                                        const TSourceLoc &nameLoc,
                                                        TVector<unsigned int> *arraySizes,
                                                        const TSourceLoc &arrayLoc)
{
    checkArrayElementIsNotArray(arrayLoc, elementType);
    TPublicType arrayType{elementType};
    arrayType.makeArrays(arraySizes);
    return parseParameterDeclarator(arrayType, name, nameLoc);
}

bool TParseContext::checkUnsizedArrayConstructorArgumentDimensionality(
    const TIntermSequence &arguments,
    TType type,
    const TSourceLoc &line)
{
    if (arguments.empty())
    {
        error(line, "implicitly sized array constructor must have at least one argument", "[]");
        return false;
    }
    for (TIntermNode *arg : arguments)
    {
        const TIntermTyped *element = arg->getAsTyped();
        ASSERT(element);
        if (element->getType().isUnsizedArray())
        {
            error(line, "constructing from an unsized array", "constructor");
            return false;
        }
        size_t dimensionalityFromElement = element->getType().getNumArraySizes() + 1u;
        if (dimensionalityFromElement > type.getNumArraySizes())
        {
            error(line, "constructing from a non-dereferenced array", "constructor");
            return false;
        }
        else if (dimensionalityFromElement < type.getNumArraySizes())
        {
            if (dimensionalityFromElement == 1u)
            {
                error(line, "implicitly sized array of arrays constructor argument is not an array",
                      "constructor");
            }
            else
            {
                error(line,
                      "implicitly sized array of arrays constructor argument dimensionality is too "
                      "low",
                      "constructor");
            }
            return false;
        }
    }
    return true;
}

// This function is used to test for the correctness of the parameters passed to various constructor
// functions and also convert them to the right datatype if it is allowed and required.
//
// Returns a node to add to the tree regardless of if an error was generated or not.
//
TIntermTyped *TParseContext::addConstructor(TFunctionLookup *fnCall, const TSourceLoc &line)
{
    TType type = fnCall->constructorType();

    TIntermSequence &arguments = fnCall->arguments();
    if (type.isUnsizedArray())
    {
        if (!checkUnsizedArrayConstructorArgumentDimensionality(arguments, type, line))
        {
            type.sizeUnsizedArrays(angle::Span<const unsigned int>());
            return CreateZeroNode(type);
        }
        TIntermTyped *firstElement = arguments.at(0)->getAsTyped();
        ASSERT(firstElement);
        if (type.getOutermostArraySize() == 0u)
        {
            type.sizeOutermostUnsizedArray(static_cast<unsigned int>(arguments.size()));
        }
        for (size_t i = 0; i < firstElement->getType().getNumArraySizes(); ++i)
        {
            if (type.getArraySizes()[i] == 0u)
            {
                type.setArraySize(i, firstElement->getType().getArraySizes()[i]);
            }
        }
        ASSERT(!type.isUnsizedArray());
    }

    checkVariableSize(line, ImmutableString(""), &type);

    if (!checkConstructorArguments(line, arguments, type))
    {
        return CreateZeroNode(type);
    }

    mIRBuilder.construct(getTypeId(type), arguments.size());

    TIntermAggregate *constructorNode = TIntermAggregate::CreateConstructor(type, &arguments);
    constructorNode->setLine(line);

    return constructorNode->fold(mDiagnostics);
}

//
// Interface/uniform blocks
TIntermDeclaration *TParseContext::addInterfaceBlock(
    const TTypeQualifierBuilder &typeQualifierBuilder,
    const TSourceLoc &nameLine,
    const ImmutableString &blockName,
    TFieldList *fieldList,
    const ImmutableString &instanceName,
    const TSourceLoc &instanceLine,
    const TVector<unsigned int> *arraySizes,
    const TSourceLoc &arraySizesLine)
{
    checkDoesNotHaveTooManyFields(blockName, fieldList, nameLine);

    // Ensure there are no duplicate field names
    checkDoesNotHaveDuplicateFieldNames(fieldList, nameLine);

    const bool isGLPerVertex = blockName == "gl_PerVertex";
    // gl_PerVertex is allowed to be redefined and therefore not reserved
    if (!isGLPerVertex)
    {
        checkIsNotReserved(nameLine, blockName);
    }

    TTypeQualifier typeQualifier = typeQualifierBuilder.getVariableTypeQualifier(mDiagnostics);

    const bool isUniformOrBuffer =
        typeQualifier.qualifier == EvqUniform || typeQualifier.qualifier == EvqBuffer;
    const bool isShaderIoBlock = IsShaderIoBlock(typeQualifier.qualifier);

    if (mShaderVersion < 310 && typeQualifier.qualifier != EvqUniform)
    {
        error(typeQualifier.line,
              "invalid qualifier: interface blocks must be uniform in version lower than GLSL ES "
              "3.10",
              getQualifierString(typeQualifier.qualifier));
    }
    else if (typeQualifier.qualifier == EvqPatchOut)
    {
        if ((!isExtensionEnabled(TExtension::EXT_tessellation_shader) &&
             !isExtensionEnabled(TExtension::OES_tessellation_shader) && mShaderVersion < 320) ||
            mShaderType != GL_TESS_CONTROL_SHADER)
        {
            error(typeQualifier.line,
                  "invalid qualifier: 'patch out' requires a tessellation control shader",
                  getQualifierString(typeQualifier.qualifier));
        }
    }
    else if (typeQualifier.qualifier == EvqPatchIn)
    {
        if ((!isExtensionEnabled(TExtension::EXT_tessellation_shader) &&
             !isExtensionEnabled(TExtension::OES_tessellation_shader) && mShaderVersion < 320) ||
            mShaderType != GL_TESS_EVALUATION_SHADER)
        {
            error(typeQualifier.line,
                  "invalid qualifier: 'patch in' requires a tessellation evaluation shader",
                  getQualifierString(typeQualifier.qualifier));
        }
    }
    else if (typeQualifier.qualifier != EvqUniform && typeQualifier.qualifier != EvqBuffer)
    {
        if (isShaderIoBlock)
        {
            if (!isExtensionEnabled(TExtension::OES_shader_io_blocks) &&
                !isExtensionEnabled(TExtension::EXT_shader_io_blocks) &&
                !isExtensionEnabled(TExtension::OES_geometry_shader) &&
                !isExtensionEnabled(TExtension::EXT_geometry_shader) && mShaderVersion < 320)
            {
                error(typeQualifier.line,
                      "invalid qualifier: shader IO blocks need shader io block extension",
                      getQualifierString(typeQualifier.qualifier));
            }

            // Both inputs and outputs of tessellation control shaders must be arrays.
            // For tessellation evaluation shaders, only inputs must necessarily be arrays.
            // Inputs of geometry shaders must be arrays too.
            const bool isTCS = mShaderType == GL_TESS_CONTROL_SHADER;
            const bool isTESIn =
                mShaderType == GL_TESS_EVALUATION_SHADER && IsShaderIn(typeQualifier.qualifier);
            const bool isGSIn =
                mShaderType == GL_GEOMETRY_SHADER && IsShaderIn(typeQualifier.qualifier);
            if (arraySizes == nullptr && (isTCS || isTESIn || isGSIn))
            {
                error(typeQualifier.line, "type must be an array", blockName);
            }
        }
        else
        {
            error(typeQualifier.line,
                  "invalid qualifier: interface blocks must be uniform or buffer",
                  getQualifierString(typeQualifier.qualifier));
        }
    }

    if (typeQualifier.invariant)
    {
        error(typeQualifier.line, "invalid qualifier on interface block", "invariant");
    }

    if (typeQualifier.qualifier != EvqBuffer)
    {
        checkMemoryQualifierIsNotSpecified(typeQualifier.memoryQualifier, typeQualifier.line);
    }

    // Verify array sizes
    if (arraySizes)
    {
        if (isUniformOrBuffer)
        {
            if (arraySizes->size() == 0)
            {
                error(arraySizesLine, "unsized arrays are not allowed with interface blocks", "");
            }
            if (arraySizes->size() > 1)
            {
                error(arraySizesLine, "array of arrays are not allowed with interface blocks", "");
            }
        }
        else if (isShaderIoBlock)
        {
            size_t arrayDimensions = arraySizes->size();

            // Geometry shader inputs have a level arrayness that must be ignored.
            if (mShaderType == GL_GEOMETRY_SHADER_EXT && IsVaryingIn(typeQualifier.qualifier))
            {
                ASSERT(arrayDimensions > 0);
                --arrayDimensions;

                // Validate that the array size of input matches the geometry layout
                // declaration, if not automatic (specified as []).
                const unsigned int geometryDim = arraySizes->back();
                if (geometryDim > 0 && geometryDim != mGeometryInputArraySize)
                {
                    error(arraySizesLine,
                          "geometry shader input block array size inconsistent "
                          "with primitive",
                          "");
                }
            }

            if (arrayDimensions > 1)
            {
                error(arraySizesLine, "array of arrays are not allowed with I/O blocks", "");
            }
        }
    }
    else if (isShaderIoBlock && mShaderType == GL_GEOMETRY_SHADER_EXT &&
             IsVaryingIn(typeQualifier.qualifier))
    {
        error(arraySizesLine, "geometry shader input blocks must be an array", "");
    }

    // Validate max uniform block limits
    if (typeQualifier.qualifier == EvqUniform)
    {
        unsigned int blockCount =
            arraySizes == nullptr || arraySizes->empty() ? 1 : (*arraySizes)[0];
        if (mNumUniformBlocks + blockCount > mMaxUniformBlocks)
        {
            error(arraySizesLine,
                  "uniform block count greater than per stage maximum uniform blocks", "");
        }
        else
        {
            mNumUniformBlocks += blockCount;
        }
    }

    checkIndexIsNotSpecified(typeQualifier.line, typeQualifier.layoutQualifier.index);

    if (mShaderVersion < 310)
    {
        checkBindingIsNotSpecified(typeQualifier.line, typeQualifier.layoutQualifier.binding);
    }
    else
    {
        unsigned int arraySize =
            arraySizes == nullptr || arraySizes->empty() ? 0 : (*arraySizes)[0];
        checkBlockBindingIsValid(typeQualifier.line, typeQualifier.qualifier,
                                 typeQualifier.layoutQualifier.binding, arraySize);
    }

    checkDepthIsNotSpecified(typeQualifier.line, typeQualifier.layoutQualifier.depth);
    checkYuvIsNotSpecified(typeQualifier.line, typeQualifier.layoutQualifier.yuv);
    checkEarlyFragmentTestsIsNotSpecified(typeQualifier.line,
                                          typeQualifier.layoutQualifier.earlyFragmentTests);
    checkNoncoherentIsNotSpecified(typeQualifier.line, typeQualifier.layoutQualifier.noncoherent);

    TLayoutQualifier blockLayoutQualifier = typeQualifier.layoutQualifier;
    if (!IsShaderIoBlock(typeQualifier.qualifier) && typeQualifier.qualifier != EvqPatchIn &&
        typeQualifier.qualifier != EvqPatchOut)
    {
        checkLocationIsNotSpecified(typeQualifier.line, blockLayoutQualifier);
    }
    checkStd430IsForShaderStorageBlock(typeQualifier.line, blockLayoutQualifier.blockStorage,
                                       typeQualifier.qualifier);

    if (blockLayoutQualifier.matrixPacking == EmpUnspecified)
    {
        if (typeQualifier.qualifier == EvqUniform)
        {
            blockLayoutQualifier.matrixPacking = mDefaultUniformMatrixPacking;
        }
        else if (typeQualifier.qualifier == EvqBuffer)
        {
            blockLayoutQualifier.matrixPacking = mDefaultBufferMatrixPacking;
        }
    }

    if (blockLayoutQualifier.blockStorage == EbsUnspecified)
    {
        if (typeQualifier.qualifier == EvqUniform)
        {
            blockLayoutQualifier.blockStorage = mDefaultUniformBlockStorage;
        }
        else if (typeQualifier.qualifier == EvqBuffer)
        {
            blockLayoutQualifier.blockStorage = mDefaultBufferBlockStorage;
        }
    }

    checkWorkGroupSizeIsNotSpecified(nameLine, blockLayoutQualifier);

    checkInternalFormatIsNotSpecified(nameLine, blockLayoutQualifier.imageInternalFormat);

    // check for sampler types and apply layout qualifiers
    for (size_t memberIndex = 0; memberIndex < fieldList->size(); ++memberIndex)
    {
        TField *field    = (*fieldList)[memberIndex];
        TType *fieldType = field->type();
        if (ContainsOpaque<IsOpaqueFunc>(*fieldType))
        {
            error(field->line(), "Opaque types are not allowed in interface blocks", blockName);
        }

        const TQualifier qualifier = fieldType->getQualifier();
        switch (qualifier)
        {
            case EvqGlobal:
                break;
            case EvqUniform:
                if (typeQualifier.qualifier == EvqBuffer)
                {
                    error(field->line(), "invalid qualifier on shader storage block member",
                          getQualifierString(qualifier));
                }
                break;
            case EvqBuffer:
                if (typeQualifier.qualifier == EvqUniform)
                {
                    error(field->line(), "invalid qualifier on uniform block member",
                          getQualifierString(qualifier));
                }
                break;
            // a member variable in io block may have different interpolation.
            case EvqSmoothIn:
            case EvqSmoothOut:
            case EvqFlatIn:
            case EvqFlatOut:
            case EvqNoPerspectiveIn:
            case EvqNoPerspectiveOut:
            case EvqCentroidIn:
            case EvqCentroidOut:
            case EvqSampleIn:
            case EvqSampleOut:
            case EvqNoPerspectiveCentroidIn:
            case EvqNoPerspectiveCentroidOut:
            case EvqNoPerspectiveSampleIn:
            case EvqNoPerspectiveSampleOut:
                break;
            // a member variable can have an incomplete qualifier because shader io block has either
            // in or out.
            case EvqSmooth:
            case EvqFlat:
            case EvqNoPerspective:
            case EvqCentroid:
            case EvqSample:
            case EvqNoPerspectiveCentroid:
            case EvqNoPerspectiveSample:
            case EvqGeometryIn:
            case EvqGeometryOut:
                if (!IsShaderIoBlock(typeQualifier.qualifier) &&
                    typeQualifier.qualifier != EvqPatchIn &&
                    typeQualifier.qualifier != EvqPatchOut &&
                    typeQualifier.qualifier != EvqGeometryIn &&
                    typeQualifier.qualifier != EvqGeometryOut)
                {
                    error(field->line(), "invalid qualifier on interface block member",
                          getQualifierString(qualifier));
                }
                break;
            default:
                error(field->line(), "invalid qualifier on interface block member",
                      getQualifierString(qualifier));
                break;
        }

        // On interface block members, invariant is only applicable to output I/O blocks.
        const bool isOutputShaderIoBlock = isShaderIoBlock && IsShaderOut(typeQualifier.qualifier);
        if (fieldType->isInvariant() && !isOutputShaderIoBlock)
        {
            error(field->line(), "invalid qualifier on interface block member", "invariant");
        }
        // Set invariant on output I/O blocks if invariant(all) is globally specified
        if (isOutputShaderIoBlock && mDirectiveHandler.pragma().stdgl.invariantAll &&
            (mCompileOptions.flattenPragmaSTDGLInvariantAll || mCompileOptions.useIR))
        {
            fieldType->setInvariant(true);
        }

        // check layout qualifiers
        TLayoutQualifier fieldLayoutQualifier = fieldType->getLayoutQualifier();
        checkIndexIsNotSpecified(field->line(), fieldLayoutQualifier.index);
        checkBindingIsNotSpecified(field->line(), fieldLayoutQualifier.binding);

        if (fieldLayoutQualifier.blockStorage != EbsUnspecified)
        {
            error(field->line(), "invalid layout qualifier: cannot be used here",
                  getBlockStorageString(fieldLayoutQualifier.blockStorage));
        }

        const bool isMatrixPackingApplicable = fieldType->isMatrixPackingApplicable();
        if (fieldLayoutQualifier.matrixPacking == EmpUnspecified)
        {
            if (isMatrixPackingApplicable)
            {
                fieldLayoutQualifier.matrixPacking = blockLayoutQualifier.matrixPacking;
            }
        }
        else if (!isMatrixPackingApplicable)
        {
            warning(field->line(),
                    "extraneous layout qualifier: only has an effect on matrix types",
                    getMatrixPackingString(fieldLayoutQualifier.matrixPacking));
            fieldLayoutQualifier.matrixPacking = EmpUnspecified;
        }

        fieldType->setLayoutQualifier(fieldLayoutQualifier);

        if (mShaderVersion < 310 || memberIndex != fieldList->size() - 1u ||
            typeQualifier.qualifier != EvqBuffer)
        {
            // ESSL 3.10 spec section 4.1.9 allows for runtime-sized arrays.
            checkIsNotUnsizedArray(field->line(),
                                   "array members of interface blocks must specify a size",
                                   field->name(), field->type());
        }

        if (typeQualifier.qualifier == EvqBuffer)
        {
            // set memory qualifiers
            // GLSL ES 3.10 session 4.9 [Memory Access Qualifiers]. When a block declaration is
            // qualified with a memory qualifier, it is as if all of its members were declared with
            // the same memory qualifier.
            const TMemoryQualifier &blockMemoryQualifier = typeQualifier.memoryQualifier;
            TMemoryQualifier fieldMemoryQualifier        = fieldType->getMemoryQualifier();
            fieldMemoryQualifier.readonly |= blockMemoryQualifier.readonly;
            fieldMemoryQualifier.writeonly |= blockMemoryQualifier.writeonly;
            fieldMemoryQualifier.coherent |= blockMemoryQualifier.coherent;
            fieldMemoryQualifier.restrictQualifier |= blockMemoryQualifier.restrictQualifier;
            fieldMemoryQualifier.volatileQualifier |= blockMemoryQualifier.volatileQualifier;
            // TODO(jiajia.qin@intel.com): Decide whether if readonly and writeonly buffer variable
            // is legal. See bug https://github.com/KhronosGroup/OpenGL-API/issues/7
            fieldType->setMemoryQualifier(fieldMemoryQualifier);
        }

        // For per-vertex members, apply the appropriate built-in qualifiers to the members.
        if (isGLPerVertex)
        {
            if (field->name() == "gl_Position")
            {
                fieldType->setQualifier(EvqPosition);
            }
            if (field->name() == "gl_PointSize")
            {
                fieldType->setQualifier(EvqPointSize);
            }
            if (field->name() == "gl_ClipDistance")
            {
                fieldType->setQualifier(EvqClipDistance);
            }
            if (field->name() == "gl_CullDistance")
            {
                fieldType->setQualifier(EvqCullDistance);
            }
        }
    }

    SymbolType instanceSymbolType = SymbolType::UserDefined;
    if (isGLPerVertex)
    {
        // Mark gl_PerVertex as built-in if usage is not erroneous.  If it is, there will be failure
        // elsewhere that validates gl_PerVertex cannot be used when not a built-in.
        if (IsVaryingOut(typeQualifier.qualifier) && mShaderType != GL_FRAGMENT_SHADER &&
            mShaderType != GL_COMPUTE_SHADER)
        {
            instanceSymbolType      = SymbolType::BuiltIn;
            typeQualifier.qualifier = EvqPerVertexOut;
        }
        else if (IsVaryingIn(typeQualifier.qualifier) && mShaderType != GL_VERTEX_SHADER &&
                 mShaderType != GL_FRAGMENT_SHADER && mShaderType != GL_COMPUTE_SHADER)
        {
            instanceSymbolType      = SymbolType::BuiltIn;
            typeQualifier.qualifier = EvqPerVertexIn;
        }
    }
    TInterfaceBlock *interfaceBlock = new TInterfaceBlock(&symbolTable, blockName, fieldList,
                                                          blockLayoutQualifier, instanceSymbolType);
    if (!symbolTable.declare(interfaceBlock) && isUniformOrBuffer)
    {
        error(nameLine, "redefinition of an interface block name", blockName);
    }

    GeomTessArray sized = GeomTessArray::Sized;
    TType *interfaceBlockType =
        new TType(interfaceBlock, typeQualifier.qualifier, blockLayoutQualifier);
    if (arraySizes)
    {
        interfaceBlockType->makeArrays(*arraySizes);
        checkDeclarationIsValidArraySize(instanceLine, instanceName, interfaceBlockType);
    }
    checkGeometryShaderInputAndSetArraySize(instanceLine, instanceName, interfaceBlockType, &sized);
    checkTessellationShaderUnsizedArraysAndSetSize(instanceLine, instanceName, interfaceBlockType,
                                                   &sized);

    // If this is gl_PerVertex, make sure the instance name is as expected.
    if (interfaceBlockType->getQualifier() == EvqPerVertexOut)
    {
        switch (mShaderType)
        {
            case GL_VERTEX_SHADER:
            case GL_TESS_EVALUATION_SHADER_EXT:
            case GL_GEOMETRY_SHADER_EXT:
                if (!instanceName.empty())
                {
                    error(instanceLine,
                          "out gl_PerVertex instance name must be empty in this shader",
                          instanceName);
                    instanceSymbolType = SymbolType::UserDefined;
                }
                break;
            case GL_TESS_CONTROL_SHADER_EXT:
                if (instanceName != "gl_out")
                {
                    error(instanceLine,
                          "out gl_PerVertex instance name must be gl_out in this shader",
                          instanceName);
                    instanceSymbolType = SymbolType::UserDefined;
                }
                break;
            default:
                UNREACHABLE();
                break;
        }
    }
    else if (interfaceBlockType->getQualifier() == EvqPerVertexIn)
    {
        if (instanceName != "gl_in")
        {
            error(instanceLine, "in gl_PerVertex instance name must be gl_in", instanceName);
            instanceSymbolType = SymbolType::UserDefined;
        }
    }

    if (sized == GeomTessArray::Sized)
    {
        interfaceBlockType->setTypeId(getTypeId(*interfaceBlockType));
    }

    // The instance variable gets created to refer to the interface block type from the AST
    // regardless of if there's an instance name. It's created as an empty symbol if there is no
    // instance name.
    TVariable *instanceVariable =
        new TVariable(&symbolTable, instanceName, interfaceBlockType,
                      instanceName.empty() ? SymbolType::Empty : instanceSymbolType);

    checkVariableSize(nameLine, blockName, interfaceBlockType);
    checkVariableLocations(nameLine, instanceVariable);
    declareIRVariable(instanceVariable, sized);

    if (instanceVariable->symbolType() == SymbolType::Empty)
    {
        // Cannot have an array variable that is yet to be sized but which also has no name (because
        // there is no way to specify arrayness with [] without a name).
        ASSERT(sized == GeomTessArray::Sized);
        const ir::VariableId instanceId = mVariableToId.at(instanceVariable).id;

        // define symbols for the members of the interface block
        for (size_t memberIndex = 0; memberIndex < fieldList->size(); ++memberIndex)
        {
            TField *field    = (*fieldList)[memberIndex];
            TType *fieldType = new TType(*field->type());

            // set parent pointer of the field variable
            fieldType->setInterfaceBlockField(interfaceBlock, memberIndex);

            fieldType->setQualifier(typeQualifier.qualifier);

            SymbolType symbolType = SymbolType::UserDefined;
            if (field->name() == "gl_Position" || field->name() == "gl_PointSize" ||
                field->name() == "gl_ClipDistance" || field->name() == "gl_CullDistance")
            {
                // These builtins can be redefined only when used within a redefined gl_PerVertex
                // block
                if (interfaceBlockType->getQualifier() != EvqPerVertexIn &&
                    interfaceBlockType->getQualifier() != EvqPerVertexOut)
                {
                    error(field->line(), "redefinition in an invalid interface block",
                          field->name());
                }
                symbolType = SymbolType::BuiltIn;
            }
            TVariable *fieldVariable =
                new TVariable(&symbolTable, field->name(), fieldType, symbolType);
            if (!symbolTable.declare(fieldVariable))
            {
                error(field->line(), "redefinition of an interface block member name",
                      field->name());
            }
            addAndCheckOutputVaryings(*fieldVariable, field->line());

            // Don't declare variables for fields of nameless interface blocks in the IR, just
            // remember to implicitly index the instance variable when referenced.
            mVariableToId[fieldVariable] =
                VariableToIdInfo{instanceId, static_cast<uint32_t>(memberIndex)};
        }
    }
    else
    {
        // gl_in and gl_out are allowed to be redeclared
        bool isGlIn =
            interfaceBlockType->getQualifier() == EvqPerVertexIn && instanceName == "gl_in";
        bool isGlOut = interfaceBlockType->getQualifier() == EvqPerVertexOut &&
                       instanceName == "gl_out" && mShaderType == GL_TESS_CONTROL_SHADER_EXT;

        if (!isGlIn && !isGlOut)
        {
            checkIsNotReserved(instanceLine, instanceName);
        }
        else if (isGlIn)
        {
            symbolTable.onGlInVariableRedeclaration(instanceVariable);
        }

        // add a symbol for this interface block
        if (!symbolTable.declare(instanceVariable))
        {
            error(instanceLine, "redefinition of an interface block instance name", instanceName);
        }
        addAndCheckOutputVaryings(*instanceVariable, instanceLine);
    }

    TIntermSymbol *blockSymbol = new TIntermSymbol(instanceVariable);
    blockSymbol->setLine(typeQualifier.line);
    TIntermDeclaration *declaration = new TIntermDeclaration();
    declaration->appendDeclarator(blockSymbol);
    declaration->setLine(nameLine);

    exitStructDeclaration();
    return declaration;
}

void TParseContext::enterStructDeclaration(const TSourceLoc &line,
                                           const ImmutableString &identifier)
{
    ++mStructNestingLevel;

    // Embedded structure definitions are not supported per GLSL ES spec.
    // ESSL 1.00.17 section 10.9. ESSL 3.00.6 section 12.11.
    if (mStructNestingLevel > 1)
    {
        error(line, "Embedded struct definitions are not allowed", "struct");
    }
}

void TParseContext::exitStructDeclaration()
{
    --mStructNestingLevel;
}

void TParseContext::checkIsBelowStructNestingLimit(const TSourceLoc &line, const TField &field)
{
    if (!sh::IsWebGLBasedSpec(mShaderSpec))
    {
        return;
    }

    if (field.type()->getBasicType() != EbtStruct)
    {
        return;
    }

    // We're already inside a structure definition at this point, so add
    // one to the field's struct nesting.
    if (1 + field.type()->getDeepestStructNesting() > kWebGLMaxStructNesting)
    {
        std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
        if (field.type()->getStruct()->symbolType() == SymbolType::Empty)
        {
            // This may happen in case there are nested struct definitions. While they are also
            // invalid GLSL, they don't cause a syntax error.
            reasonStream << "Struct nesting";
        }
        else
        {
            reasonStream << "Reference of struct type " << field.type()->getStruct()->name();
        }
        reasonStream << " exceeds maximum allowed nesting level of " << kWebGLMaxStructNesting;
        std::string reason = reasonStream.str();
        error(line, reason.c_str(), field.name());
        return;
    }
}

//
// Parse an array index expression
//
TIntermTyped *TParseContext::addIndexExpression(TIntermTyped *baseExpression,
                                                const TSourceLoc &location,
                                                TIntermTyped *indexExpression)
{
    if (!baseExpression->isArray() && !baseExpression->isMatrix() && !baseExpression->isVector())
    {
        if (baseExpression->getAsSymbolNode())
        {
            error(location, " left of '[' is not of type array, matrix, or vector ",
                  baseExpression->getAsSymbolNode()->getName());
        }
        else
        {
            error(location, " left of '[' is not of type array, matrix, or vector ", "expression");
        }

        return CreateZeroNode(TType(EbtFloat, EbpHigh, EvqConst));
    }

    TIntermTyped *effectivelyIndexedExpression = RemoveCommaLeftHandSide(baseExpression);
    switch (effectivelyIndexedExpression->getQualifier())
    {
        case EvqPerVertexIn:
            if (mGeometryShaderInputPrimitiveType == EptUndefined &&
                mShaderType == GL_GEOMETRY_SHADER_EXT)
            {
                error(location, "missing input primitive declaration before indexing gl_in.", "[");
                return CreateZeroNode(TType(EbtFloat, EbpHigh, EvqConst));
            }
            break;
        case EvqClipDistance:
            MarkClipCullIndex(location, indexExpression, &mClipDistanceInfo);
            break;
        case EvqCullDistance:
            MarkClipCullIndex(location, indexExpression, &mCullDistanceInfo);
            break;
        default:
            break;
    }

    TIntermConstantUnion *indexConstantUnion = indexExpression->getAsConstantUnion();

    // ES3.2 or ES3.1's EXT_gpu_shader5 allow dynamically uniform expressions to be used as indices
    // of opaque types (samplers and atomic counters) as well as UBOs, but not SSBOs and images.
    bool allowUniformIndices = mShaderVersion >= 320 ||
                               isExtensionEnabled(TExtension::EXT_gpu_shader5) ||
                               isExtensionEnabled(TExtension::OES_gpu_shader5);

    // ANGLE should be able to fold any constant expressions resulting in an integer - but to be
    // safe we don't treat "EvqConst" that's evaluated according to the spec as being sufficient
    // for constness. Some interpretations of the spec have allowed constant expressions with side
    // effects - like array length() method on a non-constant array.
    if (indexExpression->getQualifier() != EvqConst || indexConstantUnion == nullptr)
    {
        if (effectivelyIndexedExpression->isInterfaceBlock())
        {
            switch (effectivelyIndexedExpression->getQualifier())
            {
                case EvqPerVertexIn:
                    break;
                case EvqUniform:
                    if (!allowUniformIndices)
                    {
                        error(location,
                              "array indexes for uniform block arrays must be constant integral "
                              "expressions",
                              "[");
                    }
                    break;
                case EvqBuffer:
                    error(location,
                          "array indexes for shader storage block arrays must be constant integral "
                          "expressions",
                          "[");
                    break;
                default:
                    // It's ok for shader I/O blocks to be dynamically indexed
                    if (!IsShaderIoBlock(effectivelyIndexedExpression->getQualifier()) &&
                        effectivelyIndexedExpression->getQualifier() != EvqPatchIn &&
                        effectivelyIndexedExpression->getQualifier() != EvqPatchOut)
                    {
                        // We can reach here only in error cases.
                        ASSERT(mDiagnostics->numErrors() > 0);
                    }
                    break;
            }
        }
        else if (effectivelyIndexedExpression->getQualifier() == EvqFragmentOut ||
                 effectivelyIndexedExpression->getQualifier() == EvqFragmentInOut)
        {
            error(location,
                  "array indexes for fragment outputs must be constant integral expressions", "[");
        }
        else if (effectivelyIndexedExpression->getQualifier() == EvqLastFragData)
        {
            error(location,
                  "array indexes for gl_LastFragData must be constant integral expressions", "[");
        }
        else if (mShaderSpec == SH_WEBGL2_SPEC &&
                 effectivelyIndexedExpression->getQualifier() == EvqFragData)
        {
            error(location, "array index for gl_FragData must be constant zero", "[");
        }
        else if (mShaderSpec == SH_WEBGL2_SPEC &&
                 effectivelyIndexedExpression->getQualifier() == EvqSecondaryFragDataEXT)
        {
            error(location, "array index for gl_SecondaryFragDataEXT must be constant zero", "[");
        }
        else if (effectivelyIndexedExpression->isArray())
        {
            TBasicType elementType = effectivelyIndexedExpression->getType().getBasicType();

            // Note: In Section 12.30 of the ESSL 3.00 spec on p143-144:
            //
            //   Indexing of arrays of samplers by constant-index-expressions is
            //   supported in GLSL ES 1.00. A constant-index-expression is an
            //   expression formed from constant-expressions and certain loop indices,
            //   defined for a subset of loop constructs. Should this functionality be
            //   included in GLSL ES 3.00?
            //
            //   RESOLUTION: No. Arrays of samplers may only be indexed by constant-
            //   integral-expressions.
            if (IsSampler(elementType) && !allowUniformIndices && mShaderVersion > 100)
            {
                error(location, "array index for samplers must be constant integral expressions",
                      "[");
            }
            else if (IsImage(elementType))
            {
                error(location,
                      "array indexes for image arrays must be constant integral expressions", "[");
            }
        }
    }

    int index                   = 0;
    bool outOfRangeIndexIsError = false;

    // If the index is using the comma operator, descend to the right-most value, see if that's a
    // constant.  This is used for validating the index only, we can't constant fold the expression
    // due to the left-hand-side of the comma.
    TIntermTyped *commaRHS = indexExpression;
    while (true)
    {
        TIntermConstantUnion *constant = commaRHS->getAsConstantUnion();
        if (constant)
        {
            // If an out-of-range index is not qualified as constant, the behavior in the spec is
            // undefined. This applies even if ANGLE has been able to constant fold it (ANGLE may
            // constant fold expressions that are not constant expressions). The most compatible way
            // to handle this case is to report a warning instead of an error and force the index to
            // be in the correct range.
            outOfRangeIndexIsError = commaRHS->getQualifier() == EvqConst;
            index                  = 0;
            if (constant->getBasicType() == EbtInt)
            {
                index = constant->getIConst(0);
            }
            else if (constant->getBasicType() == EbtUInt)
            {
                index = static_cast<int>(constant->getUConst(0));
            }

            if (index < 0)
            {
                outOfRangeError(outOfRangeIndexIsError, location, "index expression is negative",
                                "[]");
            }
            break;
        }

        TIntermBinary *asBinary = commaRHS->getAsBinaryNode();
        if (asBinary == nullptr || asBinary->getOp() != EOpComma)
        {
            break;
        }
        commaRHS = asBinary->getRight();
    }

    if (indexConstantUnion)
    {
        int safeIndex = -1;
        if (index < 0)
        {
            safeIndex = 0;
        }

        if (!effectivelyIndexedExpression->getType().isUnsizedArray())
        {
            if (effectivelyIndexedExpression->isArray() &&
                effectivelyIndexedExpression->getQualifier() == EvqFragData)
            {
                mMaxFragDataArrayIndexUsed = std::max(mMaxFragDataArrayIndexUsed, index);
                if (index > 0 && !isExtensionEnabled(TExtension::EXT_draw_buffers))
                {
                    outOfRangeError(outOfRangeIndexIsError, location,
                                    "array index for gl_FragData must be zero when "
                                    "GL_EXT_draw_buffers is disabled",
                                    "[]");
                    safeIndex = 0;
                }
            }
            // Only do generic out-of-range check if similar error hasn't already been reported.
            if (safeIndex < 0)
            {
                if (effectivelyIndexedExpression->isArray())
                {
                    safeIndex =
                        checkIndexLessThan(outOfRangeIndexIsError, location, index,
                                           effectivelyIndexedExpression->getOutermostArraySize(),
                                           "array index out of range");
                }
                else if (effectivelyIndexedExpression->isMatrix())
                {
                    safeIndex =
                        checkIndexLessThan(outOfRangeIndexIsError, location, index,
                                           effectivelyIndexedExpression->getType().getCols(),
                                           "matrix field selection out of range");
                }
                else
                {
                    ASSERT(effectivelyIndexedExpression->isVector());
                    safeIndex =
                        checkIndexLessThan(outOfRangeIndexIsError, location, index,
                                           effectivelyIndexedExpression->getType().getNominalSize(),
                                           "vector field selection out of range");
                }
            }

            ASSERT(safeIndex >= 0);
            // Data of constant unions can't be changed, because it may be shared with other
            // constant unions or even builtins, like gl_MaxDrawBuffers. Instead use a new
            // sanitized object.
            if (safeIndex != index || indexConstantUnion->getBasicType() != EbtInt)
            {
                TConstantUnion *safeConstantUnion = new TConstantUnion();
                safeConstantUnion->setIConst(safeIndex);
                indexExpression =
                    new TIntermConstantUnion(safeConstantUnion, TType(indexExpression->getType()));
            }

            TIntermBinary *node =
                new TIntermBinary(EOpIndexDirect, baseExpression, indexExpression);
            node->setLine(location);

            if (effectivelyIndexedExpression->isVector() &&
                !effectivelyIndexedExpression->isArray())
            {
                const uint32_t irIndex = mIRBuilder.popArraySize();
#ifdef ANGLE_IR
                ASSERT(!mCompileOptions.useIR || mDiagnostics->numErrors() > 0 ||
                       irIndex == static_cast<uint32_t>(index));
#endif
                mIRBuilder.vectorComponent(irIndex);
            }
            else
            {
                mIRBuilder.index();
            }

            return expressionOrFoldedResult(node);
        }
    }

    // According to ESSL 100 spec, Appendix A, the index expression must be a
    // constant-index-expression unless the operand is a uniform in a vertex shader.
    if (mValidateESSL100Limitations &&
        !(mShaderType == GL_VERTEX_SHADER &&
          effectivelyIndexedExpression->getQualifier() == EvqUniform))
    {
        checkESSL100ConstantIndex(indexExpression, location);
    }

    mIRBuilder.index();

    markStaticUseIfSymbol(indexExpression);
    TIntermBinary *node = new TIntermBinary(EOpIndexIndirect, baseExpression, indexExpression);
    node->setLine(location);
    // Indirect indexing can never be constant folded.
    return node;
}

int TParseContext::checkIndexLessThan(bool outOfRangeIndexIsError,
                                      const TSourceLoc &location,
                                      int index,
                                      unsigned int arraySize,
                                      const char *reason)
{
    // A negative index should already have been checked.
    ASSERT(index >= 0);
    if (static_cast<unsigned int>(index) >= arraySize)
    {
        std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
        reasonStream << reason << " '" << index << "'";
        std::string token = reasonStream.str();
        outOfRangeError(outOfRangeIndexIsError, location, reason, "[]");
        return arraySize - 1;
    }
    return index;
}

TIntermTyped *TParseContext::addFieldSelectionExpression(TIntermTyped *baseExpression,
                                                         const TSourceLoc &dotLocation,
                                                         const ImmutableString &fieldString,
                                                         const TSourceLoc &fieldLocation)
{
    if (baseExpression->isArray())
    {
        error(fieldLocation, "cannot apply dot operator to an array", ".");
        return baseExpression;
    }

    if (baseExpression->isVector())
    {
        TVector<uint32_t> fieldOffsets;
        if (!parseVectorFields(fieldLocation, fieldString, baseExpression->getNominalSize(),
                               &fieldOffsets))
        {
            fieldOffsets.resize(1);
            fieldOffsets[0] = 0;
        }

        if (fieldOffsets.size() == 1)
        {
            mIRBuilder.vectorComponent(fieldOffsets[0]);
        }
        else
        {
            ANGLE_UNSAFE_TODO(mIRBuilder.vectorComponentMulti(
                angle::Span(fieldOffsets.data(), fieldOffsets.size())));
        }

        TIntermSwizzle *node = new TIntermSwizzle(baseExpression, fieldOffsets);
        node->setLine(dotLocation);

        return node->fold(mDiagnostics);
    }
    else if (baseExpression->getBasicType() == EbtStruct)
    {
        const TFieldList &fields = baseExpression->getType().getStruct()->fields();
        if (fields.empty())
        {
            error(dotLocation, "structure has no fields", "Internal Error");
            return baseExpression;
        }
        else
        {
            bool fieldFound = false;
            unsigned int i;
            for (i = 0; i < fields.size(); ++i)
            {
                if (fields[i]->name() == fieldString)
                {
                    fieldFound = true;
                    break;
                }
            }
            if (fieldFound)
            {
                mIRBuilder.structField(i);

                // The AST RewriteStructSamplers transformation cannot handle (..., struct).sampler.
                // Fail compilation until the IR.
                if (!mCompileOptions.useIR && baseExpression->getAsBinaryNode() &&
                    baseExpression->getAsBinaryNode()->getOp() == EOpComma &&
                    baseExpression->getType().isStructureContainingSamplers())
                {
                    error(dotLocation,
                          "accessing fields of the result of a comma expression that is a "
                          "structure with samplers is not currently supported",
                          "Internal Error");
                }

                TIntermTyped *index = CreateIndexNode(i);
                index->setLine(fieldLocation);
                TIntermBinary *node =
                    new TIntermBinary(EOpIndexDirectStruct, baseExpression, index);
                node->setLine(dotLocation);
                return expressionOrFoldedResult(node);
            }
            else
            {
                error(dotLocation, " no such field in structure", fieldString);
                return baseExpression;
            }
        }
    }
    else if (baseExpression->isInterfaceBlock())
    {
        const TFieldList &fields = baseExpression->getType().getInterfaceBlock()->fields();
        if (fields.empty())
        {
            error(dotLocation, "interface block has no fields", "Internal Error");
            return baseExpression;
        }
        else
        {
            bool fieldFound = false;
            unsigned int i;
            for (i = 0; i < fields.size(); ++i)
            {
                if (fields[i]->name() == fieldString)
                {
                    fieldFound = true;
                    break;
                }
            }
            if (fieldFound)
            {
                mIRBuilder.structField(i);

                // Validate extension requirements for gl_PerVertex fields.
                const TInterfaceBlock *interfaceBlock =
                    baseExpression->getType().getInterfaceBlock();
                if (interfaceBlock && interfaceBlock->name() == "gl_PerVertex")
                {
                    TQualifier fieldQualifier = fields[i]->type()->getQualifier();
                    if (fieldQualifier == EvqClipDistance || fieldQualifier == EvqCullDistance)
                    {
                        // gl_ClipDistance and gl_CullDistance require EXT_clip_cull_distance or
                        // ANGLE_clip_cull_distance extension.
                        if (!checkCanUseOneOfExtensions(
                                fieldLocation,
                                std::array<TExtension, 2u>{{TExtension::EXT_clip_cull_distance,
                                                            TExtension::ANGLE_clip_cull_distance}}))
                        {
                            return baseExpression;
                        }
                    }
                    else if (mShaderType == GL_GEOMETRY_SHADER && fieldQualifier == EvqPointSize)
                    {
                        if (!checkCanUseOneOfExtensions(
                                fieldLocation,
                                std::array<TExtension, 2u>{{TExtension::EXT_geometry_point_size,
                                                            TExtension::OES_geometry_point_size}}))
                        {
                            return baseExpression;
                        }
                    }
                    else if ((mShaderType == GL_TESS_CONTROL_SHADER ||
                              mShaderType == GL_TESS_EVALUATION_SHADER) &&
                             fieldQualifier == EvqPointSize)
                    {
                        if (!checkCanUseOneOfExtensions(
                                fieldLocation, std::array<TExtension, 2u>{
                                                   {TExtension::EXT_tessellation_point_size,
                                                    TExtension::OES_tessellation_point_size}}))
                        {
                            return baseExpression;
                        }
                    }
                }

                TIntermTyped *index = CreateIndexNode(i);
                index->setLine(fieldLocation);
                TIntermBinary *node =
                    new TIntermBinary(EOpIndexDirectInterfaceBlock, baseExpression, index);
                node->setLine(dotLocation);
                // Indexing interface blocks can never be constant folded.
                return node;
            }
            else
            {
                error(dotLocation, " no such field in interface block", fieldString);
                return baseExpression;
            }
        }
    }
    else
    {
        if (mShaderVersion < 300)
        {
            error(dotLocation, " field selection requires structure or vector on left hand side",
                  fieldString);
        }
        else
        {
            error(dotLocation,
                  " field selection requires structure, vector, or interface block on left hand "
                  "side",
                  fieldString);
        }
        return baseExpression;
    }
}

TLayoutQualifier TParseContext::parseLayoutQualifier(const ImmutableString &qualifierType,
                                                     const TSourceLoc &qualifierTypeLine)
{
    TLayoutQualifier qualifier = TLayoutQualifier::Create();

    if (qualifierType == "shared")
    {
        if (sh::IsWebGLBasedSpec(mShaderSpec))
        {
            error(qualifierTypeLine, "Only std140 layout is allowed in WebGL", "shared");
        }
        qualifier.blockStorage = EbsShared;
    }
    else if (qualifierType == "packed")
    {
        if (sh::IsWebGLBasedSpec(mShaderSpec))
        {
            error(qualifierTypeLine, "Only std140 layout is allowed in WebGL", "packed");
        }
        qualifier.blockStorage = EbsPacked;
    }
    else if (qualifierType == "std430")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.blockStorage = EbsStd430;
    }
    else if (qualifierType == "std140")
    {
        qualifier.blockStorage = EbsStd140;
    }
    else if (qualifierType == "row_major")
    {
        qualifier.matrixPacking = EmpRowMajor;
    }
    else if (qualifierType == "column_major")
    {
        qualifier.matrixPacking = EmpColumnMajor;
    }
    else if (qualifierType == "location")
    {
        error(qualifierTypeLine, "invalid layout qualifier: location requires an argument",
              qualifierType);
    }
    else if (qualifierType == "yuv" && mShaderType == GL_FRAGMENT_SHADER)
    {
        if (checkCanUseExtension(qualifierTypeLine, TExtension::EXT_YUV_target))
        {
            qualifier.yuv = true;
        }
    }
    else if (qualifierType == "early_fragment_tests")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.earlyFragmentTests = true;
    }
    else if (qualifierType == "rgba32f")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA32F;
    }
    else if (qualifierType == "rgba16f")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA16F;
    }
    else if (qualifierType == "r32f")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifR32F;
    }
    else if (qualifierType == "rgba8")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifRGBA8;
    }
    else if (qualifierType == "rgba8_snorm")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA8_SNORM;
    }
    else if (qualifierType == "rgba32i")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA32I;
    }
    else if (qualifierType == "rgba16i")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA16I;
    }
    else if (qualifierType == "rgba8i")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifRGBA8I;
    }
    else if (qualifierType == "r32i")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifR32I;
    }
    else if (qualifierType == "rgba32ui")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA32UI;
    }
    else if (qualifierType == "rgba16ui")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        qualifier.imageInternalFormat = EiifRGBA16UI;
    }
    else if (qualifierType == "rgba8ui")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifRGBA8UI;
    }
    else if (qualifierType == "r32ui")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        qualifier.imageInternalFormat = EiifR32UI;
    }
    else if (mShaderType == GL_GEOMETRY_SHADER_EXT &&
             (mShaderVersion >= 320 ||
              (checkCanUseOneOfExtensions(
                   qualifierTypeLine,
                   std::array<TExtension, 2u>{
                       {TExtension::EXT_geometry_shader, TExtension::OES_geometry_shader}}) &&
               checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310))))
    {
        if (qualifierType == "points")
        {
            qualifier.primitiveType = EptPoints;
        }
        else if (qualifierType == "lines")
        {
            qualifier.primitiveType = EptLines;
        }
        else if (qualifierType == "lines_adjacency")
        {
            qualifier.primitiveType = EptLinesAdjacency;
        }
        else if (qualifierType == "triangles")
        {
            qualifier.primitiveType = EptTriangles;
        }
        else if (qualifierType == "triangles_adjacency")
        {
            qualifier.primitiveType = EptTrianglesAdjacency;
        }
        else if (qualifierType == "line_strip")
        {
            qualifier.primitiveType = EptLineStrip;
        }
        else if (qualifierType == "triangle_strip")
        {
            qualifier.primitiveType = EptTriangleStrip;
        }
        else
        {
            error(qualifierTypeLine, "invalid layout qualifier", qualifierType);
        }
    }
    else if (mShaderType == GL_TESS_EVALUATION_SHADER_EXT &&
             (mShaderVersion >= 320 ||
              (checkCanUseOneOfExtensions(
                   qualifierTypeLine,
                   std::array<TExtension, 2u>{{TExtension::EXT_tessellation_shader,
                                               TExtension::OES_tessellation_shader}}) &&
               checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310))))
    {
        if (qualifierType == "triangles")
        {
            qualifier.tesPrimitiveType = EtetTriangles;
        }
        else if (qualifierType == "quads")
        {
            qualifier.tesPrimitiveType = EtetQuads;
        }
        else if (qualifierType == "isolines")
        {
            qualifier.tesPrimitiveType = EtetIsolines;
        }
        else if (qualifierType == "equal_spacing")
        {
            qualifier.tesVertexSpacingType = EtetEqualSpacing;
        }
        else if (qualifierType == "fractional_even_spacing")
        {
            qualifier.tesVertexSpacingType = EtetFractionalEvenSpacing;
        }
        else if (qualifierType == "fractional_odd_spacing")
        {
            qualifier.tesVertexSpacingType = EtetFractionalOddSpacing;
        }
        else if (qualifierType == "cw")
        {
            qualifier.tesOrderingType = EtetCw;
        }
        else if (qualifierType == "ccw")
        {
            qualifier.tesOrderingType = EtetCcw;
        }
        else if (qualifierType == "point_mode")
        {
            qualifier.tesPointType = EtetPointMode;
        }
        else
        {
            error(qualifierTypeLine, "invalid layout qualifier", qualifierType);
        }
    }
    else if (mShaderType == GL_FRAGMENT_SHADER)
    {
        if (qualifierType == "noncoherent")
        {
            if (checkCanUseOneOfExtensions(
                    qualifierTypeLine, std::array<TExtension, 3u>{
                                           {TExtension::EXT_shader_framebuffer_fetch,
                                            TExtension::EXT_shader_framebuffer_fetch_non_coherent,
                                            TExtension::ANGLE_shader_pixel_local_storage}}))
            {
                checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 100);
                qualifier.noncoherent = true;
            }
        }
        else if (qualifierType == "blend_support_multiply")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Multiply, &qualifier);
        }
        else if (qualifierType == "blend_support_screen")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Screen, &qualifier);
        }
        else if (qualifierType == "blend_support_overlay")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Overlay, &qualifier);
        }
        else if (qualifierType == "blend_support_darken")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Darken, &qualifier);
        }
        else if (qualifierType == "blend_support_lighten")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Lighten, &qualifier);
        }
        else if (qualifierType == "blend_support_colordodge")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Colordodge, &qualifier);
        }
        else if (qualifierType == "blend_support_colorburn")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Colorburn, &qualifier);
        }
        else if (qualifierType == "blend_support_hardlight")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Hardlight, &qualifier);
        }
        else if (qualifierType == "blend_support_softlight")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Softlight, &qualifier);
        }
        else if (qualifierType == "blend_support_difference")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Difference, &qualifier);
        }
        else if (qualifierType == "blend_support_exclusion")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::Exclusion, &qualifier);
        }
        else if (qualifierType == "blend_support_hsl_hue")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::HslHue, &qualifier);
        }
        else if (qualifierType == "blend_support_hsl_saturation")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::HslSaturation, &qualifier);
        }
        else if (qualifierType == "blend_support_hsl_color")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::HslColor, &qualifier);
        }
        else if (qualifierType == "blend_support_hsl_luminosity")
        {
            AddAdvancedBlendEquation(gl::BlendEquationType::HslLuminosity, &qualifier);
        }
        else if (qualifierType == "blend_support_all_equations")
        {
            qualifier.advancedBlendEquations.setAll();
        }
        else if (qualifierType == "depth_any")
        {
            qualifier.depth = EdAny;
        }
        else if (qualifierType == "depth_greater")
        {
            qualifier.depth = EdGreater;
        }
        else if (qualifierType == "depth_less")
        {
            qualifier.depth = EdLess;
        }
        else if (qualifierType == "depth_unchanged" && !sh::IsWebGLBasedSpec(mShaderSpec))
        {
            qualifier.depth = EdUnchanged;
        }
        else
        {
            error(qualifierTypeLine, "invalid layout qualifier", qualifierType);
        }

        if (qualifier.advancedBlendEquations.any() && mShaderVersion < 320)
        {
            if (!checkCanUseExtension(qualifierTypeLine, TExtension::KHR_blend_equation_advanced))
            {
                qualifier.advancedBlendEquations.reset();
            }
        }
    }
    else
    {
        error(qualifierTypeLine, "invalid layout qualifier", qualifierType);
    }

    return qualifier;
}

void TParseContext::parseLocalSize(const ImmutableString &qualifierType,
                                   const TSourceLoc &qualifierTypeLine,
                                   int intValue,
                                   const TSourceLoc &intValueLine,
                                   const std::string &intValueString,
                                   size_t index,
                                   sh::WorkGroupSize *localSize)
{
    checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
    if (intValue < 1)
    {
        std::stringstream reasonStream = sh::InitializeStream<std::stringstream>();
        reasonStream << "out of range: " << getWorkGroupSizeString(index) << " must be positive";
        std::string reason = reasonStream.str();
        error(intValueLine, reason.c_str(), intValueString.c_str());
    }
    (*localSize)[index] = intValue;
}

void TParseContext::parseNumViews(int intValue,
                                  const TSourceLoc &intValueLine,
                                  const std::string &intValueString,
                                  int *numViews)
{
    // This error is only specified in WebGL, but tightens unspecified behavior in the native
    // specification.
    if (intValue < 1)
    {
        error(intValueLine, "out of range: num_views must be positive", intValueString.c_str());
    }
    *numViews = intValue;
}

void TParseContext::parseInvocations(int intValue,
                                     const TSourceLoc &intValueLine,
                                     const std::string &intValueString,
                                     int *numInvocations)
{
    // Although SPEC isn't clear whether invocations can be less than 1, we add this limit because
    // it doesn't make sense to accept invocations <= 0.
    if (intValue < 1 || intValue > mResources.MaxGeometryShaderInvocations)
    {
        error(intValueLine,
              "out of range: invocations must be in the range of [1, "
              "MAX_GEOMETRY_SHADER_INVOCATIONS_OES]",
              intValueString.c_str());
    }
    else
    {
        *numInvocations = intValue;
    }
}

void TParseContext::parseMaxVertices(int intValue,
                                     const TSourceLoc &intValueLine,
                                     const std::string &intValueString,
                                     int *maxVertices)
{
    // Although SPEC isn't clear whether max_vertices can be less than 0, we add this limit because
    // it doesn't make sense to accept max_vertices < 0.
    if (intValue < 0 || intValue > mResources.MaxGeometryOutputVertices)
    {
        error(
            intValueLine,
            "out of range: max_vertices must be in the range of [0, gl_MaxGeometryOutputVertices]",
            intValueString.c_str());
    }
    else
    {
        *maxVertices = intValue;
    }
}

void TParseContext::parseVertices(int intValue,
                                  const TSourceLoc &intValueLine,
                                  const std::string &intValueString,
                                  int *vertices)
{
    if (intValue < 1 || intValue > mResources.MaxPatchVertices)
    {
        error(intValueLine,
              "out of range : vertices must be in the range of [1, gl_MaxPatchVertices]",
              intValueString.c_str());
    }
    else
    {
        *vertices = intValue;
    }
}

void TParseContext::parseIndexLayoutQualifier(int intValue,
                                              const TSourceLoc &intValueLine,
                                              const std::string &intValueString,
                                              int *index)
{
    // EXT_blend_func_extended specifies that most validation should happen at link time, but since
    // we're validating output variable locations at compile time, it makes sense to validate that
    // index is 0 or 1 also at compile time. Also since we use "-1" as a placeholder for unspecified
    // index, we can't accept it here.
    if (intValue < 0 || intValue > 1)
    {
        error(intValueLine, "out of range: index layout qualifier can only be 0 or 1",
              intValueString.c_str());
    }
    else
    {
        *index = intValue;
    }
}

TLayoutQualifier TParseContext::parseLayoutQualifier(const ImmutableString &qualifierType,
                                                     const TSourceLoc &qualifierTypeLine,
                                                     int intValue,
                                                     const TSourceLoc &intValueLine)
{
    TLayoutQualifier qualifier = TLayoutQualifier::Create();

    std::string intValueString = Str(intValue);

    if (qualifierType == "location")
    {
        // must check that location is non-negative
        if (intValue < 0)
        {
            error(intValueLine, "out of range: location must be non-negative",
                  intValueString.c_str());
        }
        else
        {
            qualifier.location           = intValue;
            qualifier.locationsSpecified = 1;
        }
    }
    else if (qualifierType == "binding")
    {
        if (!isExtensionEnabled(TExtension::ANGLE_shader_pixel_local_storage))
        {
            checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        }
        if (intValue < 0)
        {
            error(intValueLine, "out of range: binding must be non-negative",
                  intValueString.c_str());
        }
        else
        {
            qualifier.binding = intValue;
        }
    }
    else if (qualifierType == "offset")
    {
        checkLayoutQualifierSupported(qualifierTypeLine, qualifierType, 310);
        if (intValue < 0)
        {
            error(intValueLine, "out of range: offset must be non-negative",
                  intValueString.c_str());
        }
        else
        {
            qualifier.offset = intValue;
        }
    }
    else if (qualifierType == "local_size_x")
    {
        parseLocalSize(qualifierType, qualifierTypeLine, intValue, intValueLine, intValueString, 0u,
                       &qualifier.localSize);
    }
    else if (qualifierType == "local_size_y")
    {
        parseLocalSize(qualifierType, qualifierTypeLine, intValue, intValueLine, intValueString, 1u,
                       &qualifier.localSize);
    }
    else if (qualifierType == "local_size_z")
    {
        parseLocalSize(qualifierType, qualifierTypeLine, intValue, intValueLine, intValueString, 2u,
                       &qualifier.localSize);
    }
    else if (qualifierType == "num_views" && mShaderType == GL_VERTEX_SHADER)
    {
        if (checkCanUseOneOfExtensions(
                qualifierTypeLine, std::array<TExtension, 2u>{
                                       {TExtension::OVR_multiview, TExtension::OVR_multiview2}}))
        {
            parseNumViews(intValue, intValueLine, intValueString, &qualifier.numViews);
        }
    }
    else if (qualifierType == "invocations" && mShaderType == GL_GEOMETRY_SHADER_EXT &&
             (mShaderVersion >= 320 ||
              checkCanUseOneOfExtensions(
                  qualifierTypeLine,
                  std::array<TExtension, 2u>{
                      {TExtension::EXT_geometry_shader, TExtension::OES_geometry_shader}})))
    {
        parseInvocations(intValue, intValueLine, intValueString, &qualifier.invocations);
    }
    else if (qualifierType == "max_vertices" && mShaderType == GL_GEOMETRY_SHADER_EXT &&
             (mShaderVersion >= 320 ||
              checkCanUseOneOfExtensions(
                  qualifierTypeLine,
                  std::array<TExtension, 2u>{
                      {TExtension::EXT_geometry_shader, TExtension::OES_geometry_shader}})))
    {
        parseMaxVertices(intValue, intValueLine, intValueString, &qualifier.maxVertices);
    }
    else if (qualifierType == "index" && mShaderType == GL_FRAGMENT_SHADER &&
             checkCanUseExtension(qualifierTypeLine, TExtension::EXT_blend_func_extended))
    {
        parseIndexLayoutQualifier(intValue, intValueLine, intValueString, &qualifier.index);
        if (intValue != 0)
        {
            errorIfPLSDeclared(qualifierTypeLine, PLSIllegalOperations::FragDataIndexNonzero);
        }
    }
    else if (qualifierType == "vertices" && mShaderType == GL_TESS_CONTROL_SHADER_EXT &&
             (mShaderVersion >= 320 ||
              checkCanUseOneOfExtensions(
                  qualifierTypeLine,
                  std::array<TExtension, 2u>{
                      {TExtension::EXT_tessellation_shader, TExtension::OES_tessellation_shader}})))
    {
        parseVertices(intValue, intValueLine, intValueString, &qualifier.vertices);
    }
    else
    {
        error(qualifierTypeLine, "invalid layout qualifier", qualifierType);
    }

    return qualifier;
}

TTypeQualifierBuilder *TParseContext::createTypeQualifierBuilder(const TSourceLoc &loc)
{
    return new TTypeQualifierBuilder(
        new TStorageQualifierWrapper(symbolTable.atGlobalLevel() ? EvqGlobal : EvqTemporary, loc),
        mShaderVersion);
}

TStorageQualifierWrapper *TParseContext::parseGlobalStorageQualifier(TQualifier qualifier,
                                                                     const TSourceLoc &loc)
{
    checkIsAtGlobalLevel(loc, getQualifierString(qualifier));
    return new TStorageQualifierWrapper(qualifier, loc);
}

TStorageQualifierWrapper *TParseContext::parseVaryingQualifier(const TSourceLoc &loc)
{
    if (getShaderType() == GL_VERTEX_SHADER)
    {
        return parseGlobalStorageQualifier(EvqVaryingOut, loc);
    }
    return parseGlobalStorageQualifier(EvqVaryingIn, loc);
}

TStorageQualifierWrapper *TParseContext::parseInQualifier(const TSourceLoc &loc)
{
    if (declaringFunction())
    {
        return new TStorageQualifierWrapper(EvqParamIn, loc);
    }

    switch (getShaderType())
    {
        case GL_VERTEX_SHADER:
        {
            if (mShaderVersion < 300 && !anyMultiviewExtensionAvailable())
            {
                error(loc, "storage qualifier supported in GLSL ES 3.00 and above only", "in");
            }
            return new TStorageQualifierWrapper(EvqVertexIn, loc);
        }
        case GL_FRAGMENT_SHADER:
        {
            if (mShaderVersion < 300)
            {
                error(loc, "storage qualifier supported in GLSL ES 3.00 and above only", "in");
            }
            return new TStorageQualifierWrapper(EvqFragmentIn, loc);
        }
        case GL_COMPUTE_SHADER:
        {
            return new TStorageQualifierWrapper(EvqComputeIn, loc);
        }
        case GL_GEOMETRY_SHADER:
        {
            return new TStorageQualifierWrapper(EvqGeometryIn, loc);
        }
        case GL_TESS_CONTROL_SHADER:
        {
            return new TStorageQualifierWrapper(EvqTessControlIn, loc);
        }
        case GL_TESS_EVALUATION_SHADER:
        {
            return new TStorageQualifierWrapper(EvqTessEvaluationIn, loc);
        }
        default:
        {
            UNREACHABLE();
            return new TStorageQualifierWrapper(EvqLast, loc);
        }
    }
}

TStorageQualifierWrapper *TParseContext::parseOutQualifier(const TSourceLoc &loc)
{
    if (declaringFunction())
    {
        return new TStorageQualifierWrapper(EvqParamOut, loc);
    }
    switch (getShaderType())
    {
        case GL_VERTEX_SHADER:
        {
            if (mShaderVersion < 300)
            {
                error(loc, "storage qualifier supported in GLSL ES 3.00 and above only", "out");
            }
            return new TStorageQualifierWrapper(EvqVertexOut, loc);
        }
        case GL_FRAGMENT_SHADER:
        {
            if (mShaderVersion < 300)
            {
                error(loc, "storage qualifier supported in GLSL ES 3.00 and above only", "out");
            }
            return new TStorageQualifierWrapper(EvqFragmentOut, loc);
        }
        case GL_COMPUTE_SHADER:
        {
            error(loc, "storage qualifier isn't supported in compute shaders", "out");
            return new TStorageQualifierWrapper(EvqParamOut, loc);
        }
        case GL_GEOMETRY_SHADER_EXT:
        {
            return new TStorageQualifierWrapper(EvqGeometryOut, loc);
        }
        case GL_TESS_CONTROL_SHADER_EXT:
        {
            return new TStorageQualifierWrapper(EvqTessControlOut, loc);
        }
        case GL_TESS_EVALUATION_SHADER_EXT:
        {
            return new TStorageQualifierWrapper(EvqTessEvaluationOut, loc);
        }
        default:
        {
            UNREACHABLE();
            return new TStorageQualifierWrapper(EvqLast, loc);
        }
    }
}

TStorageQualifierWrapper *TParseContext::parseInOutQualifier(const TSourceLoc &loc)
{
    if (!declaringFunction())
    {
        if (mShaderVersion < 300)
        {
            error(loc, "storage qualifier supported in GLSL ES 3.00 and above only", "inout");
        }

        if (getShaderType() != GL_FRAGMENT_SHADER)
        {
            error(loc, "storage qualifier isn't supported in non-fragment shaders", "inout");
        }

        if (isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch) ||
            isExtensionEnabled(TExtension::EXT_shader_framebuffer_fetch_non_coherent))
        {
            return new TStorageQualifierWrapper(EvqFragmentInOut, loc);
        }

        error(loc,
              "invalid qualifier: can be used with either function parameters or the variables for "
              "fetching input attachment data",
              "inout");
    }
    return new TStorageQualifierWrapper(EvqParamInOut, loc);
}

TLayoutQualifier TParseContext::joinLayoutQualifiers(TLayoutQualifier leftQualifier,
                                                     TLayoutQualifier rightQualifier,
                                                     const TSourceLoc &rightQualifierLocation)
{
    return sh::JoinLayoutQualifiers(leftQualifier, rightQualifier, rightQualifierLocation,
                                    mDiagnostics);
}

TDeclarator *TParseContext::parseStructDeclarator(const ImmutableString &identifier,
                                                  const TSourceLoc &loc)
{
    return new TDeclarator(identifier, loc);
}

TDeclarator *TParseContext::parseStructArrayDeclarator(const ImmutableString &identifier,
                                                       const TSourceLoc &loc,
                                                       const TVector<unsigned int> *arraySizes)
{
    return new TDeclarator(identifier, arraySizes, loc);
}

void TParseContext::checkDoesNotHaveDuplicateFieldNames(const TFieldList *fields,
                                                        const TSourceLoc &location)
{
    TUnorderedMap<ImmutableString, uint32_t, ImmutableString::FowlerNollVoHash<sizeof(size_t)>>
        fieldNames;
    for (TField *field : *fields)
    {
        // Note: operator[] adds this name to the map if it doesn't already exist, and initializes
        // its value to 0.
        uint32_t count = ++fieldNames[field->name()];
        if (count != 1)
        {
            error(location, "Duplicate field name in structure", field->name());
        }
    }
}

void TParseContext::checkDoesNotHaveTooManyFields(const ImmutableString &name,
                                                  const TFieldList *fields,
                                                  const TSourceLoc &location)
{
    // Check that there are not too many fields.  SPIR-V has a limit of 16383 fields, and it would
    // be reasonable to apply that limit to all outputs.  For example, it was observed that 32768
    // fields cause the Nvidia GL driver to fail compilation, so such a limit is not too specific to
    // SPIR-V.
    constexpr size_t kMaxFieldCount = 16383;
    if (fields->size() > kMaxFieldCount)
    {
        error(location, "Too many fields in the struct (limit is 16383)", name);
    }
}

TFieldList *TParseContext::addStructFieldList(TFieldList *fields, const TSourceLoc &location)
{
    return fields;
}

TFieldList *TParseContext::combineStructFieldLists(TFieldList *processedFields,
                                                   const TFieldList *newlyAddedFields,
                                                   const TSourceLoc &location)
{
    processedFields->insert(processedFields->end(), newlyAddedFields->begin(),
                            newlyAddedFields->end());
    return processedFields;
}

TFieldList *TParseContext::addStructDeclaratorListWithQualifiers(
    const TTypeQualifierBuilder &typeQualifierBuilder,
    TPublicType *typeSpecifier,
    const TDeclaratorList *declaratorList)
{
    TTypeQualifier typeQualifier = typeQualifierBuilder.getVariableTypeQualifier(mDiagnostics);

    typeSpecifier->qualifier       = typeQualifier.qualifier;
    typeSpecifier->layoutQualifier = typeQualifier.layoutQualifier;
    typeSpecifier->memoryQualifier = typeQualifier.memoryQualifier;
    typeSpecifier->invariant       = typeQualifier.invariant;
    typeSpecifier->precise         = typeQualifier.precise;
    if (typeQualifier.precision != EbpUndefined)
    {
        typeSpecifier->precision = typeQualifier.precision;
    }
    return addStructDeclaratorList(*typeSpecifier, declaratorList);
}

TFieldList *TParseContext::addStructDeclaratorList(const TPublicType &typeSpecifier,
                                                   const TDeclaratorList *declaratorList)
{
    checkPrecisionSpecified(typeSpecifier.getLine(), typeSpecifier.precision,
                            typeSpecifier.getBasicType());

    checkIsNonVoid(typeSpecifier.getLine(), (*declaratorList)[0]->name(),
                   typeSpecifier.getBasicType());

    checkWorkGroupSizeIsNotSpecified(typeSpecifier.getLine(), typeSpecifier.layoutQualifier);
    checkEarlyFragmentTestsIsNotSpecified(typeSpecifier.getLine(),
                                          typeSpecifier.layoutQualifier.earlyFragmentTests);
    checkNoncoherentIsNotSpecified(typeSpecifier.getLine(),
                                   typeSpecifier.layoutQualifier.noncoherent);

    TFieldList *fieldList = new TFieldList();

    for (const TDeclarator *declarator : *declaratorList)
    {
        TType *type = new TType(typeSpecifier);

        if (declarator->isArray())
        {
            // Don't allow arrays of arrays in ESSL < 3.10.
            checkArrayElementIsNotArray(typeSpecifier.getLine(), typeSpecifier);
            type->makeArrays(*declarator->arraySizes());
        }
        type->setTypeId(getTypeId(*type));

        SymbolType symbolType = SymbolType::UserDefined;
        if (declarator->name() == "gl_Position" || declarator->name() == "gl_PointSize" ||
            declarator->name() == "gl_ClipDistance" || declarator->name() == "gl_CullDistance")
        {
            symbolType = SymbolType::BuiltIn;
        }
        else
        {
            checkIsNotReserved(typeSpecifier.getLine(), declarator->name());
        }
        // Nested struct declarations are invalid and an error would be already generated in that
        // case.  Mark the type as not a struct specifier to avoid an ASSERT in TField.
        if (type->isStructSpecifier())
        {
            type = new TType(type->getStruct(), false);
        }
        TField *field = new TField(type, declarator->name(), declarator->line(), symbolType);
        checkIsBelowStructNestingLimit(typeSpecifier.getLine(), *field);
        fieldList->push_back(field);
    }

    return fieldList;
}

TTypeSpecifierNonArray TParseContext::addStructure(const TSourceLoc &structLine,
                                                   const TSourceLoc &nameLine,
                                                   const ImmutableString &structName,
                                                   TFieldList *fieldList)
{
    SymbolType structSymbolType = SymbolType::UserDefined;
    const bool isNamelessStruct = structName.empty();
    if (isNamelessStruct)
    {
        structSymbolType = SymbolType::Empty;
    }

    // To simplify pulling samplers out of structs, reorder the struct fields to put the samplers at
    // the end.  Structures that *only* contain samplers are also put last.
    TFieldList *reorderedFields = new TFieldList;
    for (TField *field : *fieldList)
    {
        if (!IsSamplerOrStructWithOnlySamplers(field->type()))
        {
            reorderedFields->push_back(field);
        }
    }
    for (TField *field : *fieldList)
    {
        if (IsSamplerOrStructWithOnlySamplers(field->type()))
        {
            reorderedFields->push_back(field);
        }
    }

    TStructure *structure =
        new TStructure(&symbolTable, structName, reorderedFields, structSymbolType);

    // Store a bool in the struct if we're at global scope, to allow us to
    // skip the local struct scoping workaround in HLSL.
    structure->setAtGlobalScope(symbolTable.atGlobalLevel());

    if (structSymbolType != SymbolType::Empty)
    {
        checkIsNotReserved(nameLine, structName);
        if (!symbolTable.declare(structure))
        {
            error(nameLine, "redefinition of a struct", structName);
        }
    }

    checkDoesNotHaveTooManyFields(structName, fieldList, structLine);

    // Ensure there are no duplicate field names
    checkDoesNotHaveDuplicateFieldNames(fieldList, structLine);

    // Ensure we do not specify any storage qualifiers on the struct members
    for (TField *fieldPtr : *fieldList)
    {
        TField &field              = *fieldPtr;
        const TQualifier qualifier = field.type()->getQualifier();
        switch (qualifier)
        {
            case EvqGlobal:
            case EvqTemporary:
                break;
            default:
                error(field.line(), "invalid qualifier on struct member",
                      getQualifierString(qualifier));
                break;
        }
        if (field.type()->isInvariant())
        {
            error(field.line(), "invalid qualifier on struct member", "invariant");
        }

        const TLayoutQualifier layoutQualifier = field.type()->getLayoutQualifier();
        if (!layoutQualifier.isEmpty())
        {
            error(field.line(), "invalid layout qualifier on struct member", "layout");
        }

        const TMemoryQualifier memoryQualifier = field.type()->getMemoryQualifier();
        if (!memoryQualifier.isEmpty())
        {
            error(field.line(), "invalid memory qualifier on struct member",
                  memoryQualifier.getAnyQualifierString());
        }

        if (field.type()->isPrecise())
        {
            error(field.line(), "invalid precise qualifier on struct member", "precise");
        }

        // ESSL 3.10 section 4.1.8 -- atomic_uint or images are not allowed as structure member.
        // ANGLE_shader_pixel_local_storage also disallows PLS as struct members.
        if (IsImage(field.type()->getBasicType()) ||
            IsAtomicCounter(field.type()->getBasicType()) ||
            IsPixelLocal(field.type()->getBasicType()))
        {
            error(field.line(), "disallowed type in struct", field.type()->getBasicString());
        }

        checkIsNotUnsizedArray(field.line(), "array members of structs must specify a size",
                               field.name(), field.type());

        checkMemoryQualifierIsNotSpecified(field.type()->getMemoryQualifier(), field.line());

        checkIndexIsNotSpecified(field.line(), field.type()->getLayoutQualifier().index);

        checkBindingIsNotSpecified(field.line(), field.type()->getLayoutQualifier().binding);

        checkLocationIsNotSpecified(field.line(), field.type()->getLayoutQualifier());
    }

    mSymbolToTypeId[structure] = mIRBuilder.getStructTypeId(
        structure->symbolType() == SymbolType::Empty ? kEmptyImmutableString : structure->name(),
        ANGLE_UNSAFE_TODO(
            angle::Span<const TField *const>(reorderedFields->data(), reorderedFields->size())),
        {}, false, false, symbolTable.atGlobalLevel());

    // Nameless structs are declared inline with their variables if the variable is a shader input
    // or output.  But named structs are always separately declared at the end of parse, as well as
    // nameless structs that are used for uniforms or global/local variables.
    TTypeSpecifierNonArray typeSpecifierNonArray;
    typeSpecifierNonArray.initializeStruct(structure, isNamelessStruct, true, structLine);
    exitStructDeclaration();

    if (isNamelessStruct)
    {
        mNamelessStructs.push_back(structure);
    }
    else
    {
        if (symbolTable.atGlobalLevel())
        {
            mGlobalNamedStructs.push_back(structure);
        }
        else
        {
            mFunctionLocalNamedStructs.push_back(structure);
        }
    }

    return typeSpecifierNonArray;
}

void TParseContext::beginSwitch(const TSourceLoc &line, TIntermTyped *init)
{
    ControlFlow flow = {};
    flow.type        = ControlFlowType::Switch;
    flow.switchType  = init->getBasicType();
    mControlFlow.push_back(flow);

    symbolTable.push();

    checkNestingLevel(line);

    mIRBuilder.beginSwitch();
}

TIntermSwitch *TParseContext::addSwitch(TIntermTyped *init,
                                        TIntermBlock *statementList,
                                        const TSourceLoc &loc)
{
    symbolTable.pop();
    popControlFlow();

    TBasicType switchType = init->getBasicType();
    if ((switchType != EbtInt && switchType != EbtUInt) || init->isMatrix() || init->isArray() ||
        init->isVector())
    {
        error(init->getLine(), "init-expression in a switch statement must be a scalar integer",
              "switch");
        return nullptr;
    }

    ASSERT(statementList);

    // There have been some differences between versions of GLSL ES specs on whether this should
    // be an error or not, but this was clarified as an error in GLSL ES versions newer than 3.00
    // too.
    const size_t statementCount = statementList->getChildCount();
    if (statementCount > 0 &&
        statementList->getChildNode(statementCount - 1)->getAsCaseNode() != nullptr)
    {
        error(loc, "no statement between the last case label and the end of the switch statement",
              "switch");
        return nullptr;
    }

    // In case the last statement isn't already a branch, add |break| automatically.  Some AST
    // transformations may dead-code-eliminate the contents of the last |case| and leave the switch
    // statements ending in a case (which is what the check above forbids).  Most generators do not
    // handle this unexpected situation.
    //
    // Note that a branch may be present inside a nested block, but that's ok, the extra branch
    // added here gets eliminated in |PruneNoOps|.
    if (statementCount > 0 &&
        statementList->getChildNode(statementCount - 1)->getAsBranchNode() == nullptr)
    {
        statementList->appendStatement(new TIntermBranch(EOpBreak, nullptr));
    }

    mIRBuilder.endSwitch();

    markStaticUseIfSymbol(init);
    TIntermSwitch *node = new TIntermSwitch(init, statementList);
    node->setLine(loc);
    return node;
}

bool TParseContext::isNestedIn(ControlFlowType type) const
{
    // Used for validation, we need to know for example that a `continue` statement is nested
    // within a loop, etc.  Search backwards in the nested control flow info to find the closest
    // control flow of given type.
    for (const ControlFlow &controlFlow : angle::Reversed(mControlFlow))
    {
        if (controlFlow.type == type)
        {
            return true;
        }
    }
    return false;
}

bool TParseContext::isDirectlyUnderSwitch() const
{
    return mControlFlow.size() > 0 && mControlFlow.back().type == ControlFlowType::Switch;
}

bool TParseContext::checkCase(const TSourceLoc &line, int64_t caseValue, const char *caseOrDefault)
{
    if (!isDirectlyUnderSwitch())
    {
        error(line, "case and default labels need to be inside switch statements", caseOrDefault);
        return false;
    }
    for (int64_t existingCaseLabel : mControlFlow.back().caseLabels)
    {
        if (caseValue == existingCaseLabel)
        {
            error(line, "duplicate case label", caseOrDefault);
            return false;
        }
    }

    mControlFlow.back().caseLabels.push_back(caseValue);

    return true;
}

TIntermCase *TParseContext::addCase(TIntermTyped *condition, const TSourceLoc &loc)
{
    if (condition == nullptr)
    {
        error(loc, "case label must have a condition", "case");
        return nullptr;
    }
    if ((condition->getBasicType() != EbtInt && condition->getBasicType() != EbtUInt) ||
        condition->isMatrix() || condition->isArray() || condition->isVector())
    {
        error(condition->getLine(), "case label must be a scalar integer", "case");
        return nullptr;
    }
    TIntermConstantUnion *conditionConst = condition->getAsConstantUnion();
    // ANGLE should be able to fold any EvqConst expressions resulting in an integer - but to be
    // safe against corner cases we still check for conditionConst. Some interpretations of the
    // spec have allowed constant expressions with side effects - like array length() method on a
    // non-constant array.
    if (condition->getQualifier() != EvqConst || conditionConst == nullptr)
    {
        error(condition->getLine(), "case label must be constant", "case");
        return nullptr;
    }

    const int64_t caseValue = condition->getBasicType() == EbtInt
                                  ? static_cast<int64_t>(conditionConst->getIConst(0))
                                  : static_cast<int64_t>(conditionConst->getUConst(0));
    if (!checkCase(loc, caseValue, "case"))
    {
        return nullptr;
    }

    if (condition->getBasicType() != mControlFlow.back().switchType)
    {
        error(loc, "case label type does not match switch init-expression type", "case");
    }

    mIRBuilder.beginCase();

    TIntermCase *node = new TIntermCase(condition);
    node->setLine(loc);
    return node;
}

TIntermCase *TParseContext::addDefault(const TSourceLoc &loc)
{
    if (!checkCase(loc, ControlFlow::kDefaultCaseLabel, "default"))
    {
        return nullptr;
    }

    mIRBuilder.beginDefault();

    TIntermCase *node = new TIntermCase(nullptr);
    node->setLine(loc);
    return node;
}

TIntermTyped *TParseContext::createUnaryMath(TOperator op,
                                             TIntermTyped *child,
                                             const TSourceLoc &loc,
                                             const TFunction *func)
{
    ASSERT(child != nullptr);

    switch (op)
    {
        case EOpLogicalNot:
            if (child->getBasicType() != EbtBool || child->isMatrix() || child->isArray() ||
                child->isVector())
            {
                unaryOpError(loc, GetOperatorString(op), child->getType());
                return nullptr;
            }
            break;
        case EOpBitwiseNot:
            if ((child->getBasicType() != EbtInt && child->getBasicType() != EbtUInt) ||
                child->isMatrix() || child->isArray())
            {
                unaryOpError(loc, GetOperatorString(op), child->getType());
                return nullptr;
            }
            break;
        case EOpPostIncrement:
        case EOpPreIncrement:
        case EOpPostDecrement:
        case EOpPreDecrement:
        case EOpNegative:
        case EOpPositive:
            if (child->getBasicType() == EbtStruct || child->isInterfaceBlock() ||
                child->getBasicType() == EbtBool || child->isArray() ||
                child->getBasicType() == EbtVoid || IsOpaqueType(child->getBasicType()))
            {
                unaryOpError(loc, GetOperatorString(op), child->getType());
                return nullptr;
            }
            break;
        // Operators for math built-ins are already type checked against their prototype.
        default:
            break;
    }

    if (child->getMemoryQualifier().writeonly)
    {
        const char *opStr =
            BuiltInGroup::IsBuiltIn(op) ? func->name().data() : GetOperatorString(op);
        unaryOpError(loc, opStr, child->getType());
        return nullptr;
    }

    mIRBuilder.builtIn(op, 1);

    markStaticUseIfSymbol(child);
    TIntermUnary *node = new TIntermUnary(op, child, func);
    node->setLine(loc);

    return node->fold(mDiagnostics);
}

TIntermTyped *TParseContext::addUnaryMath(TOperator op, TIntermTyped *child, const TSourceLoc &loc)
{
    ASSERT(op != EOpNull);
    TIntermTyped *node = createUnaryMath(op, child, loc, nullptr);
    if (node == nullptr)
    {
        return child;
    }
    return node;
}

TIntermTyped *TParseContext::addUnaryMathLValue(TOperator op,
                                                TIntermTyped *child,
                                                const TSourceLoc &loc)
{
    checkCanBeLValue(loc, GetOperatorString(op), child);
    return addUnaryMath(op, child, loc);
}

TIntermTyped *TParseContext::expressionOrFoldedResult(TIntermTyped *expression)
{
    // If we can, we should return the folded version of the expression for subsequent parsing. This
    // enables folding the containing expression during parsing as well, instead of the separate
    // FoldExpressions() step where folding nested expressions requires multiple full AST
    // traversals.

    // Even if folding fails the fold() functions return some node representing the expression,
    // typically the original node. So "folded" can be assumed to be non-null.
    TIntermTyped *folded = expression->fold(mDiagnostics);
    ASSERT(folded != nullptr);
    if (folded->getQualifier() == expression->getQualifier())
    {
        // We need this expression to have the correct qualifier when validating the consuming
        // expression. So we can only return the folded node from here in case it has the same
        // qualifier as the original expression. In this kind of a cases the qualifier of the folded
        // node is EvqConst, whereas the qualifier of the expression is EvqTemporary:
        //  1. (true ? 1.0 : non_constant)
        //  2. (non_constant, 1.0)
        return folded;
    }
    return expression;
}

bool TParseContext::binaryOpCommonCheck(TOperator op,
                                        TIntermTyped *left,
                                        TIntermTyped *right,
                                        const TSourceLoc &loc)
{
    if (left->getBasicType() == EbtVoid || right->getBasicType() == EbtVoid)
    {
        error(loc, "operation with void operands", GetOperatorString(op));
        return false;
    }
    // Check opaque types are not allowed to be operands in expressions other than array indexing
    // and structure member selection.
    if (IsOpaqueType(left->getBasicType()) || IsOpaqueType(right->getBasicType()))
    {
        switch (op)
        {
            case EOpIndexDirect:
            case EOpIndexIndirect:
                break;

            default:
                ASSERT(op != EOpIndexDirectStruct);
                error(loc, "Invalid operation for variables with an opaque type",
                      GetOperatorString(op));
                return false;
        }
    }

    if (right->getMemoryQualifier().writeonly)
    {
        error(loc, "Invalid operation for variables with writeonly", GetOperatorString(op));
        return false;
    }

    if (left->getMemoryQualifier().writeonly)
    {
        switch (op)
        {
            case EOpAssign:
            case EOpInitialize:
            case EOpIndexDirect:
            case EOpIndexIndirect:
            case EOpIndexDirectStruct:
            case EOpIndexDirectInterfaceBlock:
                break;
            default:
                error(loc, "Invalid operation for variables with writeonly", GetOperatorString(op));
                return false;
        }
    }

    if (left->getType().getStruct() || right->getType().getStruct())
    {
        switch (op)
        {
            case EOpIndexDirectStruct:
                ASSERT(left->getType().getStruct());
                break;
            case EOpEqual:
            case EOpNotEqual:
            case EOpAssign:
            case EOpInitialize:
                if (left->getType() != right->getType())
                {
                    return false;
                }
                break;
            default:
                error(loc, "Invalid operation for structs", GetOperatorString(op));
                return false;
        }
    }

    if (left->isInterfaceBlock() || right->isInterfaceBlock())
    {
        switch (op)
        {
            case EOpIndexDirectInterfaceBlock:
                ASSERT(left->getType().getInterfaceBlock());
                break;
            default:
                error(loc, "Invalid operation for interface blocks", GetOperatorString(op));
                return false;
        }
    }

    if (left->isArray() != right->isArray())
    {
        error(loc, "array / non-array mismatch", GetOperatorString(op));
        return false;
    }

    if (left->isArray())
    {
        ASSERT(right->isArray());
        if (mShaderVersion < 300)
        {
            error(loc, "Invalid operation for arrays", GetOperatorString(op));
            return false;
        }

        switch (op)
        {
            case EOpEqual:
            case EOpNotEqual:
            case EOpAssign:
            case EOpInitialize:
                break;
            default:
                error(loc, "Invalid operation for arrays", GetOperatorString(op));
                return false;
        }
        // At this point, size of implicitly sized arrays should be resolved.
        if (left->getType().getArraySizes() != right->getType().getArraySizes())
        {
            error(loc, "array size mismatch", GetOperatorString(op));
            return false;
        }

        // If either side is gl_Clip/CullDistance but the built-in is not sized, that's not allowed.
        // Per EXT_clip_cull_distance, only indexing with constants can implicitly size the
        // built-ins, using them in whole-array assignment shouldn't try to size them.
        checkClipCullDistanceWholeArrayUse(
            loc, left, "Cannot use as left-hand side of assignment unless it is explicitly sized");
        checkClipCullDistanceWholeArrayUse(
            loc, right,
            "Cannot use as right-hand side of assignment unless it is explicitly sized");
    }

    // Check ops which require integer / ivec parameters
    bool isBitShift = false;
    switch (op)
    {
        case EOpBitShiftLeft:
        case EOpBitShiftRight:
        case EOpBitShiftLeftAssign:
        case EOpBitShiftRightAssign:
            // Unsigned can be bit-shifted by signed and vice versa, but we need to
            // check that the basic type is an integer type.
            isBitShift = true;
            if (!IsInteger(left->getBasicType()) || !IsInteger(right->getBasicType()))
            {
                return false;
            }
            break;
        case EOpBitwiseAnd:
        case EOpBitwiseXor:
        case EOpBitwiseOr:
        case EOpBitwiseAndAssign:
        case EOpBitwiseXorAssign:
        case EOpBitwiseOrAssign:
            // It is enough to check the type of only one operand, since later it
            // is checked that the operand types match.
            if (!IsInteger(left->getBasicType()))
            {
                return false;
            }
            break;
        default:
            break;
    }

    // Implicit type casting is not allowed in ESSL.
    if (!isBitShift && left->getBasicType() != right->getBasicType())
    {
        return false;
    }

    // Check that:
    // 1. Type sizes match exactly on ops that require that.
    // 2. Restrictions for structs that contain arrays or samplers are respected.
    // 3. Arithmetic op type dimensionality restrictions for ops other than multiply are respected.
    switch (op)
    {
        case EOpAssign:
        case EOpInitialize:
        case EOpEqual:
        case EOpNotEqual:
            // ESSL 1.00 sections 5.7, 5.8, 5.9
            if (mShaderVersion < 300 && left->getType().isStructureContainingArrays())
            {
                error(loc, "undefined operation for structs containing arrays",
                      GetOperatorString(op));
                return false;
            }
            // Samplers as l-values are disallowed also in ESSL 3.00, see section 4.1.7,
            // we interpret the spec so that this extends to structs containing samplers,
            // similarly to ESSL 1.00 spec.
            if (left->getType().isStructureContainingSamplers())
            {
                error(loc, "undefined operation for structs containing samplers",
                      GetOperatorString(op));
                return false;
            }

            if ((left->getNominalSize() != right->getNominalSize()) ||
                (left->getSecondarySize() != right->getSecondarySize()))
            {
                error(loc, "dimension mismatch", GetOperatorString(op));
                return false;
            }
            break;
        case EOpLessThan:
        case EOpGreaterThan:
        case EOpLessThanEqual:
        case EOpGreaterThanEqual:
            if (!left->isScalar() || !right->isScalar())
            {
                error(loc, "comparison operator only defined for scalars", GetOperatorString(op));
                return false;
            }
            if (left->getBasicType() == EbtBool || right->getBasicType() == EbtBool)
            {
                error(loc, "comparison operator not defined for booleans", GetOperatorString(op));
                return false;
            }
            break;
        case EOpAdd:
        case EOpSub:
        case EOpDiv:
        case EOpIMod:
        case EOpBitShiftLeft:
        case EOpBitShiftRight:
        case EOpBitwiseAnd:
        case EOpBitwiseXor:
        case EOpBitwiseOr:
        case EOpAddAssign:
        case EOpSubAssign:
        case EOpDivAssign:
        case EOpIModAssign:
        case EOpBitShiftLeftAssign:
        case EOpBitShiftRightAssign:
        case EOpBitwiseAndAssign:
        case EOpBitwiseXorAssign:
        case EOpBitwiseOrAssign:
            if ((left->isMatrix() && right->isVector()) || (left->isVector() && right->isMatrix()))
            {
                return false;
            }

            // Are the sizes compatible?
            if (left->getNominalSize() != right->getNominalSize() ||
                left->getSecondarySize() != right->getSecondarySize())
            {
                // If the nominal sizes of operands do not match:
                // One of them must be a scalar.
                if (!left->isScalar() && !right->isScalar())
                {
                    return false;
                }

                // In the case of compound assignment other than multiply-assign,
                // the right side needs to be a scalar. Otherwise a vector/matrix
                // would be assigned to a scalar. A scalar can't be shifted by a
                // vector either.
                if (!right->isScalar() &&
                    (IsAssignment(op) || op == EOpBitShiftLeft || op == EOpBitShiftRight))
                {
                    return false;
                }
            }
            break;
        default:
            break;
    }

    switch (op)
    {
        case EOpLogicalOr:
        case EOpLogicalXor:
        case EOpLogicalAnd:
            // Above operations are only supported on booleans
            ASSERT(!left->isArray() && !right->isArray() && !left->getType().getStruct() &&
                   !right->getType().getStruct());
            if (left->getBasicType() != EbtBool || !left->isScalar() || !right->isScalar())
            {
                return false;
            }
            // Basic types matching should have been already checked.
            ASSERT(right->getBasicType() == EbtBool);
            break;
        case EOpAdd:
        case EOpAddAssign:
        case EOpSub:
        case EOpSubAssign:
        case EOpDiv:
        case EOpDivAssign:
        case EOpMul:
        case EOpMulAssign:
            // Above operations are not supported on booleans
            ASSERT(!left->isArray() && !right->isArray() && !left->getType().getStruct() &&
                   !right->getType().getStruct());
            if (left->getBasicType() == EbtBool)
            {
                return false;
            }
            break;
        case EOpIMod:
        case EOpIModAssign:
            // Mod operator only supported on integers
            // Note that this is only for the % operator, not for mod()
            ASSERT(!left->isArray() && !right->isArray() && !left->getType().getStruct() &&
                   !right->getType().getStruct());
            if (left->getBasicType() != EbtInt && left->getBasicType() != EbtUInt)
            {
                return false;
            }
            break;
        default:
            break;
    }

    return true;
}

bool TParseContext::isMultiplicationTypeCombinationValid(TOperator op,
                                                         const TType &left,
                                                         const TType &right)
{
    switch (op)
    {
        case EOpMul:
        case EOpMulAssign:
            return left.getNominalSize() == right.getNominalSize() &&
                   left.getSecondarySize() == right.getSecondarySize();
        case EOpVectorTimesScalar:
            return true;
        case EOpVectorTimesScalarAssign:
            ASSERT(!left.isMatrix() && !right.isMatrix());
            return left.isVector() && !right.isVector();
        case EOpVectorTimesMatrix:
            return left.getNominalSize() == right.getRows();
        case EOpVectorTimesMatrixAssign:
            ASSERT(!left.isMatrix() && right.isMatrix());
            return left.isVector() && left.getNominalSize() == right.getRows() &&
                   left.getNominalSize() == right.getCols();
        case EOpMatrixTimesVector:
            return left.getCols() == right.getNominalSize();
        case EOpMatrixTimesScalar:
            return true;
        case EOpMatrixTimesScalarAssign:
            ASSERT(left.isMatrix() && !right.isMatrix());
            return !right.isVector();
        case EOpMatrixTimesMatrix:
            return left.getCols() == right.getRows();
        case EOpMatrixTimesMatrixAssign:
            ASSERT(left.isMatrix() && right.isMatrix());
            // We need to check two things:
            // 1. The matrix multiplication step is valid.
            // 2. The result will have the same number of columns as the lvalue.
            return left.getCols() == right.getRows() && left.getCols() == right.getCols();

        default:
            UNREACHABLE();
            return false;
    }
}

TIntermTyped *TParseContext::addBinaryMathInternal(TOperator op,
                                                   TIntermTyped *left,
                                                   TIntermTyped *right,
                                                   const TSourceLoc &loc)
{
    if (!binaryOpCommonCheck(op, left, right, loc))
    {
        return nullptr;
    }

    if (op == EOpMul)
    {
        op = TIntermBinary::GetMulOpBasedOnOperands(left->getType(), right->getType());
        if (!isMultiplicationTypeCombinationValid(op, left->getType(), right->getType()))
        {
            return nullptr;
        }
    }

    switch (op)
    {
        case EOpLogicalAnd:
            mIRBuilder.endShortCircuitAnd();
            break;
        case EOpLogicalOr:
            mIRBuilder.endShortCircuitOr();
            break;
        default:
            mIRBuilder.builtIn(op, 2);
            break;
    }

    TIntermBinary *node = new TIntermBinary(op, left, right);
    ASSERT(op != EOpAssign);
    markStaticUseIfSymbol(left);
    markStaticUseIfSymbol(right);
    node->setLine(loc);
    return expressionOrFoldedResult(node);
}

TIntermTyped *TParseContext::addBinaryMath(TOperator op,
                                           TIntermTyped *left,
                                           TIntermTyped *right,
                                           const TSourceLoc &loc)
{
    TIntermTyped *node = addBinaryMathInternal(op, left, right, loc);
    if (node == nullptr)
    {
        binaryOpError(loc, GetOperatorString(op), left->getType(), right->getType());
        return left;
    }
    return node;
}

TIntermTyped *TParseContext::addBinaryMathBooleanResult(TOperator op,
                                                        TIntermTyped *left,
                                                        TIntermTyped *right,
                                                        const TSourceLoc &loc)
{
    TIntermTyped *node = addBinaryMathInternal(op, left, right, loc);
    if (node == nullptr)
    {
        binaryOpError(loc, GetOperatorString(op), left->getType(), right->getType());
        node = CreateBoolNode(false);
        node->setLine(loc);
    }
    return node;
}

TIntermTyped *TParseContext::addAssign(TOperator op,
                                       TIntermTyped *left,
                                       TIntermTyped *right,
                                       const TSourceLoc &loc)
{
    checkCanBeLValue(loc, "assign", left);
    TIntermBinary *node = nullptr;
    if (binaryOpCommonCheck(op, left, right, loc))
    {
        TIntermBinary *lValue = left->getAsBinaryNode();
        if ((lValue != nullptr) &&
            (lValue->getOp() == EOpIndexIndirect || lValue->getOp() == EOpIndexDirect) &&
            IsTessellationControlShaderOutput(mShaderType, lValue->getLeft()->getQualifier()))
        {
            checkTCSOutVarIndexIsValid(lValue, loc);
        }

        if (op == EOpMulAssign)
        {
            op = TIntermBinary::GetMulAssignOpBasedOnOperands(left->getType(), right->getType());
            if (isMultiplicationTypeCombinationValid(op, left->getType(), right->getType()))
            {
                node = new TIntermBinary(op, left, right);
            }
        }
        else
        {
            node = new TIntermBinary(op, left, right);
        }
    }
    if (node == nullptr)
    {
        assignError(loc, "assign", left->getType(), right->getType());
        return left;
    }

    if (op != EOpAssign)
    {
        markStaticUseIfSymbol(left);
    }
    mIRBuilder.builtIn(op, 2);

    markStaticUseIfSymbol(right);
    node->setLine(loc);
    return node;
}

void TParseContext::onShortCircuitAndBegin(TIntermTyped *left, const TSourceLoc &loc)
{
    if (!left->isScalar() || left->getBasicType() != EbtBool)
    {
        error(loc, "Left hand side of && must be a scalar bool", GetOperatorString(EOpLogicalAnd));
    }
    mIRBuilder.beginShortCircuitAnd();
}

void TParseContext::onShortCircuitOrBegin(TIntermTyped *left, const TSourceLoc &loc)
{
    if (!left->isScalar() || left->getBasicType() != EbtBool)
    {
        error(loc, "Left hand side of || must be a scalar bool", GetOperatorString(EOpLogicalOr));
    }
    mIRBuilder.beginShortCircuitOr();
}

void TParseContext::onCommaLeftHandSideParsed(TIntermTyped *left)
{
    if (left->getBasicType() != EbtVoid)
    {
        // The left-hand side's value is discarded, do so before the right-hand side is parsed (and
        // pushed to the stack).
        mIRBuilder.endStatementWithValue();
    }
}

TIntermTyped *TParseContext::addComma(TIntermTyped *left,
                                      TIntermTyped *right,
                                      const TSourceLoc &loc)
{
    // WebGL2 section 5.26, the following results in an error:
    // "Sequence operator applied to void, arrays, or structs containing arrays"
    if (mShaderSpec == SH_WEBGL2_SPEC &&
        (left->isArray() || left->getBasicType() == EbtVoid ||
         left->getType().isStructureContainingArrays() || right->isArray() ||
         right->getBasicType() == EbtVoid || right->getType().isStructureContainingArrays()))
    {
        error(loc,
              "sequence operator is not allowed for void, arrays, or structs containing arrays",
              ",");
    }
    if (left->isInterfaceBlock() || right->isInterfaceBlock())
    {
        error(loc, "sequence operator is not allowed for interface blocks", ",");
    }

    TIntermBinary *commaNode = TIntermBinary::CreateComma(left, right, mShaderVersion);
    markStaticUseIfSymbol(left);
    markStaticUseIfSymbol(right);
    commaNode->setLine(loc);

    return expressionOrFoldedResult(commaNode);
}

TIntermBranch *TParseContext::addBranch(TOperator op, const TSourceLoc &loc)
{
    switch (op)
    {
        case EOpContinue:
            if (!isNestedIn(ControlFlowType::Loop))
            {
                error(loc, "continue statement only allowed in loops", "");
            }
            mIRBuilder.branchContinue();
            break;
        case EOpBreak:
            if (!isNestedIn(ControlFlowType::Loop) && !isNestedIn(ControlFlowType::Switch))
            {
                error(loc, "break statement only allowed in loops and switch statements", "");
            }
            else
            {
                mControlFlow.back().hasBreak = true;
            }
            mIRBuilder.branchBreak();
            break;
        case EOpReturn:
            if (mCurrentFunction->getReturnType().getBasicType() != EbtVoid)
            {
                error(loc, "non-void function must return a value", "return");
            }
            if (mDeclaringMain)
            {
                errorIfPLSDeclared(loc, PLSIllegalOperations::ReturnFromMain);
                mIsReturnVisitedInMain = true;
            }
            if (!mControlFlow.empty())
            {
                mControlFlow.back().hasReturn = true;
            }
            mIRBuilder.branchReturn();
            break;
        case EOpKill:
            if (mShaderType != GL_FRAGMENT_SHADER)
            {
                error(loc, "discard supported in fragment shaders only", "discard");
            }
            else
            {
                errorIfPLSDeclared(loc, PLSIllegalOperations::Discard);
            }
            mHasDiscard = true;
            mIRBuilder.branchDiscard();
            break;
        default:
            UNREACHABLE();
            break;
    }
    return addBranch(op, nullptr, loc);
}

TIntermBranch *TParseContext::addBranch(TOperator op,
                                        TIntermTyped *expression,
                                        const TSourceLoc &loc)
{
    if (expression != nullptr)
    {
        markStaticUseIfSymbol(expression);
        ASSERT(op == EOpReturn);
        mFunctionReturnsValue = true;
        if (mCurrentFunction->getReturnType().getBasicType() == EbtVoid)
        {
            error(loc, "void function cannot return a value", "return");
        }
        else if (mCurrentFunction->getReturnType() != expression->getType())
        {
            error(loc, "function return is not matching type:", "return");
        }
        if (!mControlFlow.empty())
        {
            mControlFlow.back().hasReturn = true;
        }
        mIRBuilder.branchReturnValue();
    }
    TIntermBranch *node = new TIntermBranch(op, expression);
    node->setLine(loc);
    return node;
}

void TParseContext::appendStatement(TIntermBlock *block, TIntermNode *statement)
{
    if (statement != nullptr)
    {
        // Validate that no statement is added before the first case label of a switch construct.
        if (statement->getAsCaseNode() == nullptr && isDirectlyUnderSwitch() &&
            mControlFlow.back().caseLabels.empty())
        {
            error(statement->getLine(), "statement before the first label", "switch");
        }

        markStaticUseIfSymbol(statement);
        block->appendStatement(statement);

        // Discard the value, if any.
        endStatementWithValue(statement);
    }
}

void TParseContext::checkTextureGather(TIntermAggregate *functionCall)
{
    const TOperator op    = functionCall->getOp();
    const TFunction *func = functionCall->getFunction();
    if (BuiltInGroup::IsTextureGather(op))
    {
        bool isTextureGatherOffsetOrOffsets =
            BuiltInGroup::IsTextureGatherOffset(op) || BuiltInGroup::IsTextureGatherOffsets(op);
        TIntermNode *componentNode = nullptr;
        TIntermSequence *arguments = functionCall->getSequence();
        ASSERT(arguments->size() >= 2u && arguments->size() <= 4u);
        const TIntermTyped *sampler = arguments->front()->getAsTyped();
        ASSERT(sampler != nullptr);
        switch (sampler->getBasicType())
        {
            case EbtSampler2D:
            case EbtISampler2D:
            case EbtUSampler2D:
            case EbtSampler2DArray:
            case EbtISampler2DArray:
            case EbtUSampler2DArray:
                if ((!isTextureGatherOffsetOrOffsets && arguments->size() == 3u) ||
                    (isTextureGatherOffsetOrOffsets && arguments->size() == 4u))
                {
                    componentNode = arguments->back();
                }
                break;
            case EbtSamplerCube:
            case EbtISamplerCube:
            case EbtUSamplerCube:
            case EbtSamplerCubeArray:
            case EbtISamplerCubeArray:
            case EbtUSamplerCubeArray:
                ASSERT(!isTextureGatherOffsetOrOffsets);
                if (arguments->size() == 3u)
                {
                    componentNode = arguments->back();
                }
                break;
            case EbtSampler2DShadow:
            case EbtSampler2DArrayShadow:
            case EbtSamplerCubeShadow:
            case EbtSamplerCubeArrayShadow:
                break;
            default:
                UNREACHABLE();
                break;
        }
        if (componentNode)
        {
            const TIntermConstantUnion *componentConstantUnion =
                componentNode->getAsConstantUnion();
            if (componentNode->getAsTyped()->getQualifier() != EvqConst || !componentConstantUnion)
            {
                error(functionCall->getLine(), "Texture component must be a constant expression",
                      func->name());
            }
            else
            {
                int component = componentConstantUnion->getIConst(0);
                if (component < 0 || component > 3)
                {
                    error(functionCall->getLine(), "Component must be in the range [0;3]",
                          func->name());
                }
            }
        }
    }
}

void TParseContext::checkTextureOffset(TIntermAggregate *functionCall)
{
    const TOperator op         = functionCall->getOp();
    const TFunction *func      = functionCall->getFunction();
    TIntermNode *offset        = nullptr;
    TIntermSequence *arguments = functionCall->getSequence();

    if (BuiltInGroup::IsTextureOffsetNoBias(op) || BuiltInGroup::IsTextureGatherOffsetNoComp(op) ||
        BuiltInGroup::IsTextureGatherOffsetRef(op) ||
        BuiltInGroup::IsTextureGatherOffsetsNoComp(op) ||
        BuiltInGroup::IsTextureGatherOffsetsRef(op))
    {
        offset = arguments->back();
    }
    else if (BuiltInGroup::IsTextureOffsetBias(op) || BuiltInGroup::IsTextureGatherOffsetComp(op) ||
             BuiltInGroup::IsTextureGatherOffsetsComp(op))
    {
        // A bias or comp parameter follows the offset parameter.
        ASSERT(arguments->size() >= 3);
        offset = (*arguments)[2];
    }

    // If not one of the above built-ins, there's nothing to do here.
    if (offset == nullptr)
    {
        return;
    }

    bool isTextureGatherOffset             = BuiltInGroup::IsTextureGatherOffset(op);
    bool isTextureGatherOffsets            = BuiltInGroup::IsTextureGatherOffsets(op);
    bool useTextureGatherOffsetConstraints = isTextureGatherOffset || isTextureGatherOffsets;

    int minOffsetValue = useTextureGatherOffsetConstraints
                             ? mResources.MinProgramTextureGatherOffset
                             : mResources.MinProgramTexelOffset;
    int maxOffsetValue = useTextureGatherOffsetConstraints
                             ? mResources.MaxProgramTextureGatherOffset
                             : mResources.MaxProgramTexelOffset;

    if (isTextureGatherOffsets)
    {
        // If textureGatherOffsets, the offsets parameter is an array, which is expected as an
        // aggregate constructor node or as a symbol node with a constant value.
        TIntermAggregate *offsetAggregate = offset->getAsAggregate();
        TIntermSymbol *offsetSymbol       = offset->getAsSymbolNode();

        const TConstantUnion *offsetValues = offsetAggregate ? offsetAggregate->getConstantValue()
                                             : offsetSymbol  ? offsetSymbol->getConstantValue()
                                                             : nullptr;

        if (offsetValues == nullptr)
        {
            error(functionCall->getLine(), "Texture offsets must be a constant expression",
                  func->name());
            return;
        }

        constexpr unsigned int kOffsetsCount = 4;
        const TType &offsetType =
            offsetAggregate != nullptr ? offsetAggregate->getType() : offsetSymbol->getType();
        if (offsetType.getNumArraySizes() != 1 || offsetType.getArraySizes()[0] != kOffsetsCount)
        {
            error(functionCall->getLine(), "Texture offsets must be an array of 4 elements",
                  func->name());
            return;
        }

        size_t size = offsetType.getObjectSize() / kOffsetsCount;
        for (unsigned int i = 0; i < kOffsetsCount; ++i)
        {
            ANGLE_UNSAFE_TODO(checkSingleTextureOffset(offset->getLine(), &offsetValues[i * size],
                                                       size, minOffsetValue, maxOffsetValue));
        }
    }
    else
    {
        // If textureOffset or textureGatherOffset, the offset is expected to be found as a constant
        // union.
        TIntermConstantUnion *offsetConstantUnion = offset->getAsConstantUnion();

        // ES3.2 or ES3.1's EXT_gpu_shader5 allow non-const offsets to be passed to
        // textureGatherOffset.
        bool textureGatherOffsetMustBeConst = mShaderVersion <= 310 &&
                                              !isExtensionEnabled(TExtension::EXT_gpu_shader5) &&
                                              !isExtensionEnabled(TExtension::OES_gpu_shader5);

        bool isOffsetConst =
            offset->getAsTyped()->getQualifier() == EvqConst && offsetConstantUnion != nullptr;
        bool offsetMustBeConst = !isTextureGatherOffset || textureGatherOffsetMustBeConst;

        if (!isOffsetConst && offsetMustBeConst)
        {
            error(functionCall->getLine(), "Texture offset must be a constant expression",
                  func->name());
            return;
        }

        // We cannot verify non-constant offsets to textureGatherOffset.
        if (offsetConstantUnion == nullptr)
        {
            ASSERT(!offsetMustBeConst);
            return;
        }

        size_t size                  = offsetConstantUnion->getType().getObjectSize();
        const TConstantUnion *values = offsetConstantUnion->getConstantValue();
        checkSingleTextureOffset(offset->getLine(), values, size, minOffsetValue, maxOffsetValue);
    }
}

void TParseContext::checkSingleTextureOffset(const TSourceLoc &line,
                                             const TConstantUnion *values,
                                             size_t size,
                                             int minOffsetValue,
                                             int maxOffsetValue)
{
    for (size_t i = 0u; i < size; ++i)
    {
        ANGLE_UNSAFE_TODO(ASSERT(values[i].getType() == EbtInt));
        int offsetValue = ANGLE_UNSAFE_TODO(values[i].getIConst());
        if (offsetValue > maxOffsetValue || offsetValue < minOffsetValue)
        {
            std::stringstream tokenStream = sh::InitializeStream<std::stringstream>();
            tokenStream << offsetValue;
            std::string token = tokenStream.str();
            error(line, "Texture offset value out of valid range", token.c_str());
        }
    }
}

void TParseContext::checkTexelFetch(TIntermAggregate *functionCall)
{
    const TOperator op = functionCall->getOp();
    if (op != EOpTexelFetch && op != EOpTexelFetchOffset)
    {
        return;
    }

    TIntermTyped *sampler = functionCall->getChildNode(0)->getAsTyped();
    SamplerAccess access  = GetSamplerAccess(sampler);
    if (access.uniform == nullptr)
    {
        // Erroneous shader
        return;
    }

    if (access.uniform->getType().getQualifier() == EvqUniform)
    {
        // A uniform is passed to texelFetch directly, directly mark it as statically used with
        // texelFetch right away.
        mSamplersStaticallyUsedWithTexelFetch.insert(std::move(access));
        return;
    }

    // Otherwise, it must be a function parameter that's passed to texelFetch, find out which one it
    // is.
    for (size_t paramIndex = 0; paramIndex < mCurrentFunction->getParamCount(); ++paramIndex)
    {
        if (access.uniform == mCurrentFunction->getParam(paramIndex))
        {
            // Keep track of how the parameter is accessed; at the end of shader the chain of calls
            // is followed to know which uniform is statically used with texelFetch.
            FunctionSamplerAccess &functionSamplerAccess =
                mFunctionsSamplerAccess[mCurrentFunction];
            if (functionSamplerAccess.paramsStaticallyUsedWithTexelFetch.empty())
            {
                functionSamplerAccess.paramsStaticallyUsedWithTexelFetch.resize(
                    mCurrentFunction->getParamCount());
            }
            functionSamplerAccess.paramsStaticallyUsedWithTexelFetch[paramIndex].insert(
                std::move(access));
            break;
        }
    }
}

void TParseContext::trackSamplersPassedToFunction(const TFunction *fnCandidate,
                                                  TIntermAggregate *fnCall)
{
    for (size_t paramIndex = 0; paramIndex < fnCandidate->getParamCount(); ++paramIndex)
    {
        TIntermTyped *argument = (*fnCall->getSequence())[paramIndex]->getAsTyped();
        SamplerAccess access   = GetSamplerAccess(argument);
        if (access.uniform == nullptr)
        {
            continue;
        }

        const TType &type = access.uniform->getType();
        if (!IsSampler(type.getBasicType()) && !type.isStructureContainingSamplers())
        {
            continue;
        }

        SamplerAsFunctionArg samplerAsArg;
        samplerAsArg.access         = std::move(access);
        samplerAsArg.callee         = fnCandidate;
        samplerAsArg.calleeArgIndex = static_cast<uint32_t>(paramIndex);

        if (type.getQualifier() == EvqUniform)
        {
            // A uniform with samplers is passed to a function.  Track it as it might lead to it
            // being used with texelFetch.
            mFunctionsSamplerAccess[mCurrentFunction].uniformsPassedToCallee.insert(
                std::move(samplerAsArg));
        }
        else
        {
            // Otherwise, it must be a function parameter that's passed to another function, find
            // out which one it is.
            for (size_t callerParamIndex = 0; callerParamIndex < mCurrentFunction->getParamCount();
                 ++callerParamIndex)
            {
                if (access.uniform == mCurrentFunction->getParam(callerParamIndex))
                {
                    FunctionSamplerAccess &functionSamplerAccess =
                        mFunctionsSamplerAccess[mCurrentFunction];
                    if (functionSamplerAccess.paramsPassedToCallee.empty())
                    {
                        functionSamplerAccess.paramsPassedToCallee.resize(
                            mCurrentFunction->getParamCount());
                    }
                    functionSamplerAccess.paramsPassedToCallee[callerParamIndex].insert(
                        std::move(samplerAsArg));
                    break;
                }
            }
        }
    }
}

void TParseContext::postParseTrackSamplersPassedToFunction()
{
    // Calculate the DAG order of calls from |main|.  Functions that are never called don't matter;
    // if they statically used a sampler in texelFetch, it's already marked in
    // |mSamplersStaticallyUsedWithTexelFetch|.
    TVector<const TFunction *> sorted =
        GetSamplerPassingFunctionsInDAGOrder(mMainFunction, mFunctionsSamplerAccess);

    // Go over the functions, starting from |main|, which is the last entry in |sorted|.
    //
    // Assume function F is being visited, which may call function G:
    //
    // * uniformsPassedToCallee: That means F calls G(uniform.field1).  For G itself:
    //   * paramsStaticallyUsedWithTexelFetch: G calls texelFetch(param.field2).
    //     uniform.field1.field2 is derived by combining these fields, added to the global
    //     mSamplersStaticallyUsedWithTexelFetch.
    //   * paramsPassedToCallee: G calls H(param.field2).
    //     uniform.field1.field2 is derived by combining these fields, added to G's
    //     uniformsPassedToCallee.
    // * paramsStaticallyUsedWithTexelFetch is already processed at the time of visit.  Note
    //   that this is the empty set for |main|.
    // * paramsPassedToCallee is also already processed at the time of visit.  Also empty for
    //   |main|.
    //
    // When G is later, again only uniformsPassedToCallee need processing.  At the end, all the
    // texelFetch static use is accumulated in mSamplersStaticallyUsedWithTexelFetch.
    //
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        const TFunction *function                          = sorted[sorted.size() - i - 1];
        const FunctionSamplerAccess &functionSamplerAccess = mFunctionsSamplerAccess[function];

        for (const SamplerAsFunctionArg &arg : functionSamplerAccess.uniformsPassedToCallee)
        {
            FunctionSamplerAccess *calleeSamplerAccess = &mFunctionsSamplerAccess[arg.callee];
            CombineSamplerAccessToTexelFetch(
                arg.access, calleeSamplerAccess->paramsStaticallyUsedWithTexelFetch,
                arg.calleeArgIndex, &mSamplersStaticallyUsedWithTexelFetch);
            CombineSamplerAccessToFunctionArg(arg.access, calleeSamplerAccess->paramsPassedToCallee,
                                              arg.calleeArgIndex,
                                              &calleeSamplerAccess->uniformsPassedToCallee);
        }
    }

    for (const SamplerAccess &access : mSamplersStaticallyUsedWithTexelFetch)
    {
        mIRBuilder.markTexelFetchUse(mVariableToId.at(access.uniform).id,
                                     angle::Span(access.fields));
    }
}

void TParseContext::checkInterpolationFS(TIntermAggregate *functionCall)
{
    const TFunction *func = functionCall->getFunction();
    if (!BuiltInGroup::IsInterpolationFS(functionCall->getOp()))
    {
        return;
    }

    TIntermTyped *arg0 = nullptr;

    if (functionCall->getAsAggregate())
    {
        const TIntermSequence *argp = functionCall->getSequence();
        if (argp->size() > 0)
        {
            arg0 = (*argp)[0]->getAsTyped();
        }
    }
    else
    {
        assert(functionCall->getAsUnaryNode());
        arg0 = functionCall->getAsUnaryNode()->getOperand();
    }

    // Make sure the first argument is an interpolant, or an array element of an interpolant
    if (!IsVaryingIn(arg0->getType().getQualifier()))
    {
        // It might still be an array element.
        const TIntermTyped *base = FindLValueBase(arg0);

        if (base == nullptr || (!IsVaryingIn(base->getType().getQualifier())))
        {
            error(arg0->getLine(),
                  "first argument must be an interpolant, or interpolant-array element",
                  func->name());
        }
    }
}

void TParseContext::checkAtomicMemoryBuiltinFunctions(TIntermAggregate *functionCall)
{
    const TFunction *func = functionCall->getFunction();
    if (BuiltInGroup::IsAtomicMemory(functionCall->getOp()))
    {
        TIntermSequence *arguments = functionCall->getSequence();
        TIntermTyped *memNode      = (*arguments)[0]->getAsTyped();

        if (IsBufferOrSharedVariable(memNode))
        {
            return;
        }

        while (memNode->getAsBinaryNode() || memNode->getAsSwizzleNode())
        {
            // Child 0 is "left" if binary, and the expression being swizzled if swizzle.
            // Note: we don't need to check that the binary operation is one of EOp*Index*, as any
            // other operation will result in a temp value which cannot be passed to this
            // out/inout parameter anyway.
            memNode = memNode->getChildNode(0)->getAsTyped();
            if (IsBufferOrSharedVariable(memNode))
            {
                return;
            }
        }

        error(memNode->getLine(),
              "The value passed to the mem argument of an atomic memory function does not "
              "correspond to a buffer or shared variable.",
              func->name());
    }
}

// GLSL ES 3.10 Revision 4, 4.9 Memory Access Qualifiers
void TParseContext::checkImageMemoryAccessForBuiltinFunctions(TIntermAggregate *functionCall)
{
    const TOperator op = functionCall->getOp();

    if (BuiltInGroup::IsImage(op))
    {
        TIntermSequence *arguments = functionCall->getSequence();
        TIntermTyped *imageNode    = (*arguments)[0]->getAsTyped();

        const TMemoryQualifier &memoryQualifier = imageNode->getMemoryQualifier();

        if (BuiltInGroup::IsImageStore(op))
        {
            if (memoryQualifier.readonly)
            {
                error(imageNode->getLine(),
                      "'imageStore' cannot be used with images qualified as 'readonly'",
                      GetImageArgumentToken(imageNode));
            }
        }
        else if (BuiltInGroup::IsImageLoad(op))
        {
            if (memoryQualifier.writeonly)
            {
                error(imageNode->getLine(),
                      "'imageLoad' cannot be used with images qualified as 'writeonly'",
                      GetImageArgumentToken(imageNode));
            }
        }
        else if (BuiltInGroup::IsImageAtomic(op))
        {
            if (memoryQualifier.readonly)
            {
                error(imageNode->getLine(),
                      "'imageAtomic' cannot be used with images qualified as 'readonly'",
                      GetImageArgumentToken(imageNode));
            }
            if (memoryQualifier.writeonly)
            {
                error(imageNode->getLine(),
                      "'imageAtomic' cannot be used with images qualified as 'writeonly'",
                      GetImageArgumentToken(imageNode));
            }
        }
    }
}

// GLSL ES 3.10 Revision 4, 13.51 Matching of Memory Qualifiers in Function Parameters
void TParseContext::checkImageMemoryAccessForUserDefinedFunctions(
    const TFunction *functionDefinition,
    const TIntermAggregate *functionCall)
{
    ASSERT(functionCall->getOp() == EOpCallFunctionInAST);

    const TIntermSequence &arguments = *functionCall->getSequence();

    ASSERT(functionDefinition->getParamCount() == arguments.size());

    for (size_t i = 0; i < arguments.size(); ++i)
    {
        TIntermTyped *typedArgument        = arguments[i]->getAsTyped();
        const TType &functionArgumentType  = typedArgument->getType();
        const TType &functionParameterType = functionDefinition->getParam(i)->getType();
        ASSERT(functionArgumentType.getBasicType() == functionParameterType.getBasicType());

        if (IsImage(functionArgumentType.getBasicType()))
        {
            const TMemoryQualifier &functionArgumentMemoryQualifier =
                functionArgumentType.getMemoryQualifier();
            const TMemoryQualifier &functionParameterMemoryQualifier =
                functionParameterType.getMemoryQualifier();
            if (functionArgumentMemoryQualifier.readonly &&
                !functionParameterMemoryQualifier.readonly)
            {
                error(functionCall->getLine(),
                      "Function call discards the 'readonly' qualifier from image",
                      GetImageArgumentToken(typedArgument));
            }

            if (functionArgumentMemoryQualifier.writeonly &&
                !functionParameterMemoryQualifier.writeonly)
            {
                error(functionCall->getLine(),
                      "Function call discards the 'writeonly' qualifier from image",
                      GetImageArgumentToken(typedArgument));
            }

            if (functionArgumentMemoryQualifier.coherent &&
                !functionParameterMemoryQualifier.coherent)
            {
                error(functionCall->getLine(),
                      "Function call discards the 'coherent' qualifier from image",
                      GetImageArgumentToken(typedArgument));
            }

            if (functionArgumentMemoryQualifier.volatileQualifier &&
                !functionParameterMemoryQualifier.volatileQualifier)
            {
                error(functionCall->getLine(),
                      "Function call discards the 'volatile' qualifier from image",
                      GetImageArgumentToken(typedArgument));
            }
        }
    }
}

TIntermTyped *TParseContext::addFunctionCallOrMethod(TFunctionLookup *fnCall, const TSourceLoc &loc)
{
    if (fnCall->thisNode() != nullptr)
    {
        return addMethod(fnCall, loc);
    }
    if (fnCall->isConstructor())
    {
        return addConstructor(fnCall, loc);
    }
    return addNonConstructorFunctionCall(fnCall, loc);
}

TIntermTyped *TParseContext::addMethod(TFunctionLookup *fnCall, const TSourceLoc &loc)
{
    TIntermTyped *thisNode = fnCall->thisNode();
    // It's possible for the name pointer in the TFunction to be null in case it gets parsed as
    // a constructor. But such a TFunction can't reach here, since the lexer goes into FIELDS
    // mode after a dot, which makes type identifiers to be parsed as FIELD_SELECTION instead.
    // So accessing fnCall->name() below is safe.
    if (fnCall->name() != "length")
    {
        error(loc, "invalid method", fnCall->name());
    }
    else if (!fnCall->arguments().empty())
    {
        error(loc, "method takes no parameters", "length");
    }
    else if (!thisNode->isArray())
    {
        error(loc, "length can only be called on arrays", "length");
    }
    else if (thisNode->getQualifier() == EvqPerVertexIn &&
             mGeometryShaderInputPrimitiveType == EptUndefined)
    {
        ASSERT(mShaderType == GL_GEOMETRY_SHADER_EXT);
        error(loc, "missing input primitive declaration before calling length on gl_in", "length");
    }
    else
    {
        switch (thisNode->getQualifier())
        {
            case EvqClipDistance:
                MarkClipCullArrayLengthMethodCall(loc, &mClipDistanceInfo);
                break;
            case EvqCullDistance:
                MarkClipCullArrayLengthMethodCall(loc, &mCullDistanceInfo);
                break;
            default:
                break;
        }

        mIRBuilder.arrayLength();

        TIntermUnary *node = new TIntermUnary(EOpArrayLength, thisNode, nullptr);
        markStaticUseIfSymbol(thisNode);
        node->setLine(loc);
        return node->fold(mDiagnostics);
    }
    return CreateZeroNode(TType(EbtInt, EbpUndefined, EvqConst));
}

TIntermTyped *TParseContext::addNonConstructorFunctionCallImpl(TFunctionLookup *fnCall,
                                                               const TSourceLoc &loc)
{
    // First check whether the function has been hidden by a variable name or struct typename by
    // using the symbol looked up in the lexical phase. If the function is not hidden, look for one
    // with a matching argument list.
    if (fnCall->symbol() != nullptr && !fnCall->symbol()->isFunction())
    {
        error(loc, "function name expected", fnCall->name());
    }
    else
    {
        // There are no inner functions, so it's enough to look for user-defined functions in the
        // global scope.
        const TSymbol *symbol = symbolTable.findGlobal(fnCall->getMangledName());

        if (symbol != nullptr)
        {
            // A user-defined function - could be an overloaded built-in as well.
            ASSERT(symbol->symbolType() == SymbolType::UserDefined);
            const TFunction *fnCandidate = static_cast<const TFunction *>(symbol);
            TIntermAggregate *callNode =
                TIntermAggregate::CreateFunctionCall(*fnCandidate, &fnCall->arguments());
            callNode->setLine(loc);
            checkImageMemoryAccessForUserDefinedFunctions(fnCandidate, callNode);
            functionCallRValueLValueErrorCheck(fnCandidate, callNode);
            functionCallClipCullDistanceCheck(fnCandidate, callNode);
            functionCallFragDataCheck(fnCandidate, callNode);
            trackSamplersPassedToFunction(fnCandidate, callNode);

            mCallGraph[mCurrentFunction].insert(fnCandidate);
            mIRBuilder.callFunction(mFunctionToId.at(fnCandidate));
            return callNode;
        }

        symbol = symbolTable.findBuiltIn(fnCall->getMangledName(), mShaderVersion);

        if (symbol != nullptr)
        {
            // A built-in function.
            ASSERT(symbol->symbolType() == SymbolType::BuiltIn);
            const TFunction *fnCandidate = static_cast<const TFunction *>(symbol);

            if (!fnCandidate->extensions().empty() &&
                fnCandidate->extensions()[0] != TExtension::UNDEFINED)
            {
                checkCanUseOneOfExtensions(loc, fnCandidate->extensions());
            }

            // From GLES 3.2:
            //
            // For tessellation control shaders, the barrier() function may only be placed inside
            // the function main() of the shader and may not be called within any control flow.
            // Barriers are also disallowed after a return statement in the function main().
            if (fnCandidate->getBuiltInOp() == EOpBarrierTCS)
            {
                if (mIsReturnVisitedInMain)
                {
                    error(loc,
                          "barrier() may not be called at any point after a return statement in "
                          "the function main()",
                          "barrier");
                }
                else if (!mDeclaringMain)
                {
                    error(loc, "barrier() is only allowed inside the function main()", "barrier");
                }
                else if (!mControlFlow.empty())
                {
                    error(loc, "barrier() is not allowed within any control flow", "barrier");
                }
            }

            // All function calls are mapped to a built-in operation.
            TOperator op = fnCandidate->getBuiltInOp();
            if (BuiltInGroup::IsMath(op) && fnCandidate->getParamCount() == 1)
            {
                // Treat it like a built-in unary operator.
                TIntermNode *unaryParamNode = fnCall->arguments().front();
                return createUnaryMath(op, unaryParamNode->getAsTyped(), loc, fnCandidate);
            }

            TIntermAggregate *callNode =
                TIntermAggregate::CreateBuiltInFunctionCall(*fnCandidate, &fnCall->arguments());
            callNode->setLine(loc);

            if (UsesDerivatives(callNode))
            {
                mUsesDerivatives = true;
            }

            checkAtomicMemoryBuiltinFunctions(callNode);
            checkTextureOffset(callNode);
            checkTextureGather(callNode);
            checkTexelFetch(callNode);
            checkInterpolationFS(callNode);
            checkImageMemoryAccessForBuiltinFunctions(callNode);

            // Some built-in functions have out parameters too.
            functionCallRValueLValueErrorCheck(fnCandidate, callNode);

            mIRBuilder.builtIn(op, fnCandidate->getParamCount());

            // See if we can constant fold a built-in. Note that this may be possible
            // even if it is not const-qualified.
            return callNode->fold(mDiagnostics);
        }
        else
        {
            error(loc, "no matching overloaded function found", fnCall->name());
        }
    }
    return nullptr;
}

TIntermTyped *TParseContext::addNonConstructorFunctionCall(TFunctionLookup *fnCall,
                                                           const TSourceLoc &loc)
{
    TIntermTyped *result = addNonConstructorFunctionCallImpl(fnCall, loc);
    if (result != nullptr)
    {
        return result;
    }
    // Error message was already written. Put on an unused node for error recovery.
    return CreateZeroNode(TType(EbtFloat, EbpMedium, EvqConst));
}

void TParseContext::onTernaryConditionParsed(TIntermTyped *cond, const TSourceLoc &line)
{
    checkIsScalarBool(line, cond);
    mIRBuilder.beginTernaryTrueExpression();
}

void TParseContext::onTernaryTrueExpressionParsed(TIntermTyped *trueExpression,
                                                  const TSourceLoc &line)
{
    mIRBuilder.endTernaryTrueExpression(trueExpression->getBasicType());
    mIRBuilder.beginTernaryFalseExpression();
}

TIntermTyped *TParseContext::addTernarySelection(TIntermTyped *cond,
                                                 TIntermTyped *trueExpression,
                                                 TIntermTyped *falseExpression,
                                                 const TSourceLoc &loc)
{
    if (!checkIsScalarBool(loc, cond))
    {
        return falseExpression;
    }

    if (trueExpression->getType() != falseExpression->getType())
    {
        TInfoSinkBase reasonStream;
        reasonStream << "mismatching ternary operator operand types '" << trueExpression->getType()
                     << " and '" << falseExpression->getType() << "'";
        error(loc, reasonStream.c_str(), "?:");
        return falseExpression;
    }
    const TBasicType basicType = trueExpression->getBasicType();
    if (IsOpaqueType(basicType) || trueExpression->getType().isStructureContainingSamplers())
    {
        // ESSL 1.00 section 4.1.7
        // ESSL 3.00.6 section 4.1.7
        // Opaque/sampler types are not allowed in the ternary operator, including structs with
        // samplers in them.
        error(loc, "ternary operator is not allowed for opaque types", "?:");
        return falseExpression;
    }

    if (cond->getMemoryQualifier().writeonly || trueExpression->getMemoryQualifier().writeonly ||
        falseExpression->getMemoryQualifier().writeonly)
    {
        error(loc, "ternary operator is not allowed for variables with writeonly", "?:");
        return falseExpression;
    }

    // ESSL 1.00.17 sections 5.2 and 5.7:
    // Ternary operator is not among the operators allowed for structures/arrays.
    // ESSL 3.00 and ESSL 3.10 section 5.7:
    // Ternary operator supports structs, but array support is optional for arrays.
    // ESSL 3.20 section 5.7:
    // Ternary operator supports structs and arrays unconditionally.
    // In WebGL2 section 5.26, ternary is banned for both arrays and structs.
    if ((mShaderVersion < 300 || mShaderSpec == SH_WEBGL2_SPEC) && trueExpression->isArray())
    {
        error(loc, "ternary operator is not allowed for arrays in ESSL 1.0 and webgl", "?:");
        return falseExpression;
    }
    if ((mShaderVersion < 300 || mShaderSpec == SH_WEBGL2_SPEC) && basicType == EbtStruct)
    {
        error(loc, "ternary operator is not allowed for structures in ESSL 1.0 and webgl", "?:");
        return falseExpression;
    }
    if (basicType == EbtInterfaceBlock)
    {
        error(loc, "ternary operator is not allowed for interface blocks", "?:");
        return falseExpression;
    }

    // WebGL2 section 5.26, the following results in an error:
    // "Ternary operator applied to void, arrays, or structs containing arrays"
    if (mShaderSpec == SH_WEBGL2_SPEC && basicType == EbtVoid)
    {
        error(loc, "ternary operator is not allowed for void", "?:");
        return falseExpression;
    }

    mIRBuilder.endTernaryFalseExpression(basicType);
    mIRBuilder.endTernary(basicType);

    TIntermTernary *node = new TIntermTernary(cond, trueExpression, falseExpression);
    markStaticUseIfSymbol(cond);
    markStaticUseIfSymbol(trueExpression);
    markStaticUseIfSymbol(falseExpression);
    node->setLine(loc);
    return expressionOrFoldedResult(node);
}

ir::TypeId TParseContext::getTypeId(const TType &type)
{
    if (type.isTypeIdSet())
    {
        return type.typeId();
    }

    // Normally, type ids are generated at the same time as types are, except for built-ins where
    // the type is baked in.  For now, calculate the type ID.
    const TSymbol *block     = nullptr;
    const TFieldList *fields = nullptr;
    ir::TypeId id;
    if (type.getStruct())
    {
        block  = type.getStruct();
        fields = &type.getStruct()->fields();
    }
    else if (type.getInterfaceBlock())
    {
        block  = type.getInterfaceBlock();
        fields = &type.getInterfaceBlock()->fields();
    }

    if (block)
    {
        if (mSymbolToTypeId.find(block) != mSymbolToTypeId.end())
        {
            id = mSymbolToTypeId.at(block);
        }
        else
        {
            TVector<ir::TypeId> fieldTypeIds;
            for (const TField *field : *fields)
            {
                fieldTypeIds.push_back(getTypeId(*field->type()));
            }

            // Same issue with built-ins where the type id is not baked in.  So the type id of each
            // field is also calculated here and passed in.
            id                     = ANGLE_UNSAFE_TODO(mIRBuilder.getStructTypeId(
                block->symbolType() == SymbolType::Empty ? kEmptyImmutableString : block->name(),
                angle::Span<const TField *const>(fields->data(), fields->size()),
                angle::Span<ir::TypeId>(fieldTypeIds.data(), fieldTypeIds.size()),
                block->isInterfaceBlock(), block->symbolType() == SymbolType::BuiltIn, true));
            mSymbolToTypeId[block] = id;
        }
    }
    else
    {
        id = mIRBuilder.getBasicTypeId(type.getBasicType(), type.getNominalSize(),
                                       type.getSecondarySize());
    }

    if (type.isArray())
    {
        // Make sure array types that are yet to be sized are not added to the IR yet.
        ASSERT(std::find(mDeferredArrayTypesToSize.begin(), mDeferredArrayTypesToSize.end(),
                         &type) == mDeferredArrayTypesToSize.end());
        id = mIRBuilder.getArrayTypeId(id, type.getArraySizes());
    }

    return id;
}

void TParseContext::pushVariable(const TVariable *variable)
{
    if (mDiagnostics->numErrors() > 0)
    {
        return;
    }

    const VariableToIdInfo &info = mVariableToId.at(variable);
    mIRBuilder.pushVariable(info.id);
    if (info.implicitField != VariableToIdInfo::kNoImplicitField)
    {
        mIRBuilder.structField(info.implicitField);
    }
}

const TConstantUnion *TParseContext::pushConstant(const TConstantUnion *constant, const TType &type)
{
    const ir::TypeId typeId = getTypeId(type);
    if (type.isArray())
    {
        TType elementType(type);
        elementType.toArrayElementType();

        const size_t arraySize = type.getOutermostArraySize();
        for (size_t i = 0; i < arraySize; ++i)
        {
            constant = pushConstant(constant, elementType);
        }
        mIRBuilder.construct(typeId, arraySize);
    }
    else if (type.getBasicType() == EbtStruct)
    {
        const TStructure *structure = type.getStruct();

        for (const TField *field : structure->fields())
        {
            const TType *fieldType = field->type();
            constant               = pushConstant(constant, *fieldType);
        }
        mIRBuilder.construct(typeId, structure->fields().size());
    }
    else
    {
        size_t size = type.getObjectSize();
        for (size_t i = 0; i < size; ++i, ANGLE_UNSAFE_TODO(++constant))
        {
            switch (constant->getType())
            {
                case EbtFloat:
                    mIRBuilder.pushConstantFloat(constant->getFConst());
                    break;
                case EbtInt:
                    mIRBuilder.pushConstantInt(constant->getIConst());
                    break;
                case EbtUInt:
                    mIRBuilder.pushConstantUint(constant->getUConst());
                    break;
                case EbtBool:
                    mIRBuilder.pushConstantBool(constant->getBConst());
                    break;
                case EbtYuvCscStandardEXT:
                    mIRBuilder.pushConstantYuvCscStandard(constant->getYuvCscStandardEXTConst());
                    break;
                default:
                    UNREACHABLE();
            }
        }
        if (size > 1)
        {
            mIRBuilder.construct(typeId, size);
        }
    }

    return constant;
}

void TParseContext::endStatementWithValue(TIntermNode *statement)
{
    TIntermTyped *typed = statement ? statement->getAsTyped() : nullptr;
    if (typed && typed->getBasicType() != EbtVoid)
    {
        mIRBuilder.endStatementWithValue();
    }
}

void TParseContext::prependPendingStructDeclarations()
{
    TIntermSequence allStructDecls;

    // Do the following for all structs:
    //
    // * First, modify the name of the struct.  This breaks the symbol table, as names cannot be
    //   looked up anymore, but that's ok because this is the end of parse and no transformation
    //   looks up user symbol names anymore.  The struct names are used during link to match
    //   uniforms, and so for global structs, the name is suffixed by |_0| to be identical between
    //   shader stages.  For function-local structs it's suffixed by |_uniqueId|
    // * Then, create a global declarator for the struct and append it to the list of declarations.
    //
    // The whole list is prepended to the shader at the end.  Global structs are declared first, as
    // they may be used by function-local structs.  Within global or function-local structs, they
    // are declared in the order they are encountered in the shader for the same reason.  Nameless
    // structs are declared last as they may use the other named structs, but they cannot be
    // referred to by the named ones.

    for (TStructure *structure : mGlobalNamedStructs)
    {
        allStructDecls.push_back(
            RenameAndDeclareStruct(&symbolTable, structure, StructureOriginalScope::Global));
    }

    for (TStructure *structure : mFunctionLocalNamedStructs)
    {
        allStructDecls.push_back(
            RenameAndDeclareStruct(&symbolTable, structure, StructureOriginalScope::FunctionLocal));
    }

    for (TStructure *structure : mNamelessStructs)
    {
        // If the struct is part of a shader input/output variable declaration, it's already removed
        // from this list in |declareVariable|.
        ASSERT(structure->symbolType() == SymbolType::Empty);
        structure->setName(kEmptyImmutableString);

        allStructDecls.push_back(DeclareStruct(&symbolTable, structure));
    }

    mTreeRoot->getSequence()->insert(mTreeRoot->getSequence()->begin(), allStructDecls.begin(),
                                     allStructDecls.end());
}

void TParseContext::checkCallGraph()
{
    // Verify that the call graph does not contain a loop.
    enum class VisitState
    {
        NotVisited,
        Visiting,
        Visited,
    };
    struct Visit
    {
        // Note: Can't use default initializer because of msvc.
        Visit() : state(VisitState::NotVisited) {}
        VisitState state;
        uint32_t callDepth = 0;
    };
    TUnorderedMap<const TFunction *, Visit> visitState;

    TVector<const TFunction *> visitStack;
    visitStack.reserve(mCallGraph.size());

    // Visit all the functions; even if a function is unreachable, it must still result in a compile
    // error.
    for (const auto &entry : mCallGraph)
    {
        visitStack.push_back(entry.first);

        // Check the callees of this function too, if any is undefined, it's an error.
        for (const TFunction *callee : entry.second)
        {
            if (mDefinedFunctions.find(callee) == mDefinedFunctions.end())
            {
                std::stringstream errorStream = sh::InitializeStream<std::stringstream>();
                errorStream << "Function " << callee->name() << "() called by "
                            << entry.first->name() << "() is undefined";
                mDiagnostics->globalError(errorStream.str().c_str());
            }
        }
    }

    auto checkRecursion = [this, &visitState, &visitStack](const TFunction *function,
                                                           const TFunction *callee) -> bool {
        if (visitState[callee].state == VisitState::Visiting)
        {
            std::stringstream errorStream = sh::InitializeStream<std::stringstream>();
            errorStream << "Recursive function call in the following call chain: "
                        << callee->name();
            if (callee != function)
            {
                for (const TFunction *caller : angle::Reversed(visitStack))
                {
                    if (visitState[caller].state != VisitState::Visiting)
                    {
                        continue;
                    }

                    errorStream << " <- " << caller->name();
                    if (caller == callee)
                    {
                        break;
                    }
                }
            }
            mDiagnostics->globalError(errorStream.str().c_str());
            visitState[callee].state = VisitState::Visited;
            return false;
        }
        return true;
    };

    auto postVisitCheckCallDepth = [this, &visitState](const TFunction *function) -> bool {
        if (!mCompileOptions.limitCallStackDepth)
        {
            return true;
        }

        uint32_t callDepth = 0;
        for (const TFunction *callee : mCallGraph[function])
        {
            callDepth = std::max(callDepth, visitState[callee].callDepth);
        }
        // Add one depth for the call from this function to the callees.
        ++callDepth;

        visitState[function].callDepth = callDepth;

        if (callDepth > static_cast<uint32_t>(mResources.MaxCallStackDepth))
        {
            std::stringstream errorStream = sh::InitializeStream<std::stringstream>();
            errorStream << "Call stack too deep (larger than " << mResources.MaxCallStackDepth
                        << ") in function: " << function->name();
            mDiagnostics->globalError(errorStream.str().c_str());
            return false;
        }

        return true;
    };

    while (!visitStack.empty())
    {
        const TFunction *function = visitStack.back();
        visitStack.pop_back();

        Visit &visit = visitState[function];

        // If node is already visited, ignore it as it's already checked.
        if (visit.state == VisitState::Visited)
        {
            continue;
        }
        // If the node is done being visited, mark it so.
        if (visit.state == VisitState::Visiting)
        {
            visit.state = VisitState::Visited;
            if (!postVisitCheckCallDepth(function))
            {
                break;
            }
            continue;
        }

        // Add the callees to the stack.
        visit.state = VisitState::Visiting;
        visitStack.push_back(function);

        for (const TFunction *callee : mCallGraph[function])
        {
            // If any is being visited, that's a recursion!
            if (!checkRecursion(function, callee))
            {
                break;
            }

            visitStack.push_back(callee);
        }
    }
}

void TParseContext::postParseValidateFragmentOutputLocations()
{
    TVector<VariableAndLocation> validOutputs(mFragmentOutputIndex1Used
                                                  ? mResources.MaxDualSourceDrawBuffers
                                                  : mResources.MaxDrawBuffers);
    TVector<VariableAndLocation> validSecondaryOutputs(mResources.MaxDualSourceDrawBuffers);

    for (const VariableAndLocation &variable : mFragmentOutputsWithLocation)
    {
        const TType &type = variable.variable->getType();
        ASSERT(!type.isArrayOfArrays());  // Disallowed in GLSL ES 3.10 section 4.3.6.
        const size_t elementCount =
            static_cast<size_t>(type.isArray() ? type.getOutermostArraySize() : 1u);
        const size_t location = static_cast<size_t>(type.getLayoutQualifier().location);

        ASSERT(type.getLayoutQualifier().location != -1);

        TVector<VariableAndLocation> *validOutputsToUse = &validOutputs;
        TVector<VariableAndLocation> *otherOutputsToUse = &validSecondaryOutputs;
        // The default index is 0, so we only assign the output to secondary outputs in case the
        // index is explicitly set to 1.
        if (type.getLayoutQualifier().index == 1)
        {
            validOutputsToUse = &validSecondaryOutputs;
            otherOutputsToUse = &validOutputs;
        }

        if (location + elementCount <= validOutputsToUse->size())
        {
            for (size_t elementIndex = 0; elementIndex < elementCount; elementIndex++)
            {
                const size_t offsetLocation = location + elementIndex;
                if ((*validOutputsToUse)[offsetLocation].variable != nullptr)
                {
                    std::stringstream strstr = sh::InitializeStream<std::stringstream>();
                    strstr << "conflicting output locations with previously defined output '"
                           << (*validOutputsToUse)[offsetLocation].variable->name() << "'";
                    error(variable.line, strstr.str().c_str(), variable.variable->name());
                }
                else
                {
                    (*validOutputsToUse)[offsetLocation] = variable;
                    if (offsetLocation < otherOutputsToUse->size())
                    {
                        VariableAndLocation other = (*otherOutputsToUse)[offsetLocation];
                        if (other.variable != nullptr &&
                            other.variable->getType().getBasicType() != type.getBasicType())
                        {
                            std::stringstream strstr = sh::InitializeStream<std::stringstream>();
                            strstr << "conflicting output types with previously defined output '"
                                   << other.variable->name() << "' for location " << offsetLocation;
                            error(variable.line, strstr.str().c_str(), variable.variable->name());
                        }
                    }
                }
            }
        }
        else if (elementCount > 0)
        {
            std::stringstream strstr = sh::InitializeStream<std::stringstream>();
            strstr << (elementCount > 1 ? "output array locations would exceed "
                                        : "output location must be < ")
                   << "MAX_" << (mFragmentOutputIndex1Used ? "DUAL_SOURCE_" : "") << "DRAW_BUFFERS";
            error(variable.line, strstr.str().c_str(), variable.variable->name());
        }
    }

    // For outputs without a location, one may be provided with glBindFragDataLocationEXT or
    // glBindFragDataLocationIndexedEXT, so we can't validate conflicts until link time.  However,
    // we _can_ validate that no single array is larger than MAX_DRAW_BUFFERS.
    for (const VariableAndLocation &variable : mFragmentOutputsWithoutLocation)
    {
        const TType &type = variable.variable->getType();
        ASSERT(!type.isArrayOfArrays());  // Disallowed in GLSL ES 3.10 section 4.3.6.
        const size_t elementCount =
            static_cast<size_t>(type.isArray() ? type.getOutermostArraySize() : 1u);
        if (elementCount > validOutputs.size())
        {
            std::stringstream strstr = sh::InitializeStream<std::stringstream>();
            strstr << "output array locations would exceed "
                   << "MAX_" << (mFragmentOutputIndex1Used ? "DUAL_SOURCE_" : "") << "DRAW_BUFFERS";
            error(variable.line, strstr.str().c_str(), variable.variable->name());
        }
    }

    const bool isWebGL = IsWebGLBasedSpec(mShaderSpec);
    if ((!mFragmentOutputsWithLocation.empty() && !mFragmentOutputsWithoutLocation.empty()) ||
        mFragmentOutputsWithoutLocation.size() > 1)
    {
        const char *unspecifiedLocationErrorMessage = nullptr;
        if (!isExtensionEnabled(TExtension::EXT_blend_func_extended))
        {
            unspecifiedLocationErrorMessage =
                "when EXT_blend_func_extended extension is not enabled, must explicitly specify "
                "all locations when using multiple fragment outputs";
        }
        else if (!mPLSLayouts.empty())
        {
            unspecifiedLocationErrorMessage =
                "must explicitly specify all locations when using multiple fragment outputs and "
                "pixel local storage, even if EXT_blend_func_extended is enabled";
        }
        else if (isWebGL)
        {
            unspecifiedLocationErrorMessage =
                "must explicitly specify all locations when using multiple fragment outputs "
                "in WebGL contexts, even if EXT_blend_func_extended is enabled";
        }
        if (unspecifiedLocationErrorMessage != nullptr)
        {
            for (const VariableAndLocation &variable : mFragmentOutputsWithoutLocation)
            {
                error(variable.line, unspecifiedLocationErrorMessage, variable.variable->name());
            }
        }
    }

    if (!mFragmentOutputsYuv.empty() &&
        (mFragmentOutputsYuv.size() > 1 || mFragmentOutputFragDepthUsed ||
         !mFragmentOutputsWithLocation.empty() || !mFragmentOutputsWithoutLocation.empty()))
    {
        for (const VariableAndLocation &variable : mFragmentOutputsYuv)
        {
            error(variable.line,
                  "not allowed to specify yuv qualifier when using depth or multiple color "
                  "fragment outputs",
                  variable.variable->name());
        }
    }

    // If gl_SecondaryFragDataEXT is used, then indices to gl_FragData must be smaller than
    // gl_MaxDualSourceDrawBuffersEXT.  This cannot be validated until the end of the shader because
    // it would be unknown if gl_SecondaryFragDataEXT is ever used.
    //
    // Note that gl_SecondaryFragColorEXT is not checked, because simultaneous use of gl_FragData
    // and gl_SecondaryFragColorEXT is already forbidden.  Additionally, mFragmentOutputIndex1Used
    // is not checked as it pertains to ESSL 300+.
    const bool secondaryFragDataUsed =
        symbolTable.gl_SecondaryFragDataEXT() != nullptr &&
        symbolTable.isStaticallyUsed(*symbolTable.gl_SecondaryFragDataEXT());
    if (secondaryFragDataUsed && mMaxFragDataArrayIndexUsed >= mResources.MaxDualSourceDrawBuffers)
    {
        mDiagnostics->globalError(
            "array index for gl_FragData must be less than "
            "GL_MAX_DUAL_SOURCE_DRAW_BUFFERS_EXT when "
            "gl_SecondaryFragDataEXT is used");
    }
}

bool TParseContext::postParseChecks()
{
#ifndef ANGLE_IR
    // If parse failed, we shouldn't reach here.
    ASSERT(!mCompileOptions.useIR || mTreeRoot != nullptr);
#endif

    // Struct declarations are delayed to the end of parse, add them to the AST now
    if (mTreeRoot != nullptr)
    {
        prependPendingStructDeclarations();
    }

    // If gl_Position is expected to be zero-initialized, make sure it's declared to the IR; it
    // should still be done if gl_Position is statically not used by the shader.
    if (mCompileOptions.initGLPosition)
    {
        const TSymbol *glPosition =
            symbolTable.find(ImmutableString("gl_Position"), getShaderVersion());
        ASSERT(glPosition != nullptr && glPosition->isVariable());
        declareBuiltInOnFirstUse(static_cast<const TVariable *>(glPosition));
    }

    if (mMainFunction == nullptr)
    {
        error(kNoSourceLoc, "Missing main()", "");
        return false;
    }

    for (TType *type : mDeferredArrayTypesToSize)
    {
        error(kNoSourceLoc, "Unsized global array type: ", type->getBasicString());
    }

    // Clip/cull distance validation now that the size can be determined.
    if (mClipDistanceInfo.size == 0 && mClipDistanceInfo.hasNonConstIndex)
    {
        error(mClipDistanceInfo.firstEncounter,
              "The gl_ClipDistance array must be sized by the shader either redeclaring it with a "
              "size or indexing it only with constant integral expressions",
              "gl_ClipDistance");
    }

    if (mCullDistanceInfo.size == 0 && mCullDistanceInfo.hasNonConstIndex)
    {
        error(mCullDistanceInfo.firstEncounter,
              "The gl_CullDistance array must be sized by the shader either redeclaring it with a "
              "size or indexing it only with constant integral expressions",
              "gl_CullDistance");
    }

    const unsigned int usedClipDistances = getClipDistanceArraySize();
    const unsigned int usedCullDistances = getCullDistanceArraySize();
    const unsigned int combinedClipAndCullDistances =
        usedClipDistances > 0 && usedCullDistances > 0 ? usedClipDistances + usedCullDistances : 0;

    // When cull distances are not supported, i.e., when GL_ANGLE_clip_cull_distance is
    // exposed but GL_EXT_clip_cull_distance is not exposed, the combined limit is 0.
    if (usedCullDistances > 0 && mResources.MaxCombinedClipAndCullDistances == 0)
    {
        error(mCullDistanceInfo.firstEncounter, "Cull distance functionality is not available",
              "gl_CullDistance");
    }

    if (static_cast<int>(combinedClipAndCullDistances) > mResources.MaxCombinedClipAndCullDistances)
    {
        std::stringstream strstr = sh::InitializeStream<std::stringstream>();
        strstr << "The sum of 'gl_ClipDistance' and 'gl_CullDistance' size is greater than "
                  "gl_MaxCombinedClipAndCullDistances ("
               << combinedClipAndCullDistances << " > "
               << mResources.MaxCombinedClipAndCullDistances << ")";
        error(mClipDistanceInfo.firstEncounter, strstr.str().c_str(), "gl_ClipDistance");
    }

    if (mClipDistanceInfo.hasArrayLengthMethodCall && usedClipDistances == 0)
    {
        error(mClipDistanceInfo.firstEncounter,
              "The length() method cannot be called on gl_ClipDistance that is not "
              "runtime sized and also has not yet been explicitly sized",
              "gl_ClipDistance");
    }
    if (mCullDistanceInfo.hasArrayLengthMethodCall && usedCullDistances == 0)
    {
        error(mCullDistanceInfo.firstEncounter,
              "The length() method cannot be called on gl_CullDistance that is not "
              "runtime sized and also has not yet been explicitly sized",
              "gl_CullDistance");
    }

    if (ir::IsVariableIdValid(mClipDistanceInfo.id) && usedClipDistances > 0 &&
        !isClipDistanceRedeclared())
    {
        mIRBuilder.onGlClipDistanceSized(mClipDistanceInfo.id, usedClipDistances);
    }
    if (ir::IsVariableIdValid(mCullDistanceInfo.id) && usedCullDistances > 0 &&
        !isCullDistanceRedeclared())
    {
        mIRBuilder.onGlCullDistanceSized(mCullDistanceInfo.id, usedCullDistances);
    }

    ValidateFragColorAndFragData(mShaderType, mShaderVersion, symbolTable, mDiagnostics);
    postParseValidateFragmentOutputLocations();

    if (mCompileOptions.rejectWebglShadersWithLargeVariables)
    {
        if (mResources.MaxTotalPrivateVariableSizeInBytes &&
            mTotalPrivateVariablesSize.ValueOrDefault(std::numeric_limits<size_t>::max()) >
                mResources.MaxTotalPrivateVariableSizeInBytes)
        {
            error(TSourceLoc{},
                  "Total size of declared private variables exceeds implementation-defined limit",
                  "");
        }
    }

    if (mCompileOptions.rejectWebglShadersWithUndefinedBehavior)
    {
        // For any possibly infinite loops, check if the loop variable remained unchanged and so the
        // loop is in fact definitely an infinite loop.
        for (VariableAndLocation loopVariable : mPossiblyInfiniteLoops)
        {
            if (mConstantTrueVariables.find(loopVariable.variable->uniqueId()) !=
                mConstantTrueVariables.end())
            {
                error(loopVariable.line, "Infinite loop detected in the shader",
                      loopVariable.variable->name());
            }
        }
    }

    // When PLS planes are emulated with storage images, early_fragment_tests has to be specified.
    // Until codegen is done from the IR itself, set this flag here in anticipation, avoiding a need
    // for the PLS transformation in IR to make an FFI call to set it in TCompiler.  This can be
    // removed after codegen is no longer done from AST.
    if (mCompileOptions.useIR && !mPLSLayouts.empty() &&
        mCompileOptions.pls.type == ShPixelLocalStorageType::ImageLoadStore)
    {
        mEarlyFragmentTestsSpecified = true;
        // No need to set it for the IR, the PLS rewrite transformation will do that.
    }

    if (!checkCanUseShaderType(kNoSourceLoc))
    {
        return false;
    }

    checkCallGraph();

    if (numErrors() == 0)
    {
        // texelFetch static use is determined at the end of parse, so that samplers passed via
        // functions can be followed.
        postParseTrackSamplersPassedToFunction();
    }

    return numErrors() == 0;
}

void TParseContext::addAndCheckOutputVaryings(const TVariable &variable, const TSourceLoc &line)
{
    if (mShaderType != GL_VERTEX_SHADER)
    {
        return;
    }

    if (!mCompileOptions.limitOutputVaryingsTo256)
    {
        return;
    }

    if (variable.symbolType() == SymbolType::BuiltIn)
    {
        return;
    }

    if (!IsVaryingOut(variable.getType().getQualifier()))
    {
        return;
    }

    angle::CheckedNumeric<unsigned int> checkedNum = mNumOutputVaryingComponents;
    checkedNum += GetTypeComponentCount(variable.getType());
    mNumOutputVaryingComponents =
        checkedNum.ValueOrDefault(std::numeric_limits<unsigned int>::max());

    // The cap to 256 components seems somewhat arbitrary, but this is intended as a workaround for
    // a specific driver bug, and this limit being much higher than the device limits
    // (mResources.MaxVertexOutputVectors * 4), it avoids regressing both tests and applications.
    if (mNumOutputVaryingComponents > 256)
    {
        error(line, "Too many declared shader output varying components for this device",
              variable.name());
    }
}

//
// Parse an array of strings using yyparse.
//
// Returns 0 for success.
//
int PaParseStrings(angle::Span<const char *const> string,
                   const int length[],
                   TParseContext *context)
{
    if (string.empty())
    {
        return 1;
    }

    if (glslang_initialize(context))
    {
        return 1;
    }

    int error = glslang_scan(string.size(), string.data(), length, context);
    if (!error)
    {
        error = glslang_parse(context);
    }

    glslang_finalize(context);

    return (error == 0) && (context->numErrors() == 0) ? 0 : 1;
}

}  // namespace sh
