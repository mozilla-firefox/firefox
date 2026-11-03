//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/translator/Compiler.h"
#include "common/unsafe_buffers.h"

#include <sstream>

#include "angle_gl.h"

#include "common/BinaryStream.h"
#include "common/CompiledShaderState.h"
#include "common/PackedEnums.h"
#include "common/angle_version_info.h"

#include "compiler/translator/CallDAG.h"
#include "compiler/translator/CollectVariables.h"
#include "compiler/translator/Initialize.h"
#include "compiler/translator/IsASTDepthBelowLimit.h"
#include "compiler/translator/OutputTree.h"
#include "compiler/translator/ParseContext.h"
#include "compiler/translator/SizeClipCullDistance.h"
#include "compiler/translator/VariablePacker.h"
#include "compiler/translator/ir/src/compile.h"
#include "compiler/translator/tree_ops/ClampFragDepth.h"
#include "compiler/translator/tree_ops/ClampIndirectIndices.h"
#include "compiler/translator/tree_ops/ClampPointSize.h"
#include "compiler/translator/tree_ops/DeclareAndInitBuiltinsForInstancedMultiview.h"
#include "compiler/translator/tree_ops/DeferGlobalInitializers.h"
#include "compiler/translator/tree_ops/EmulateGLFragColorBroadcast.h"
#include "compiler/translator/tree_ops/EmulateMultiDrawShaderBuiltins.h"
#include "compiler/translator/tree_ops/EnsureLoopForwardProgress.h"
#include "compiler/translator/tree_ops/ExpandFragmentOutputsToVec4.h"
#include "compiler/translator/tree_ops/FoldConstantSwitch.h"
#include "compiler/translator/tree_ops/FoldExpressions.h"
#include "compiler/translator/tree_ops/InitializeVariables.h"
#include "compiler/translator/tree_ops/PruneEmptyCases.h"
#include "compiler/translator/tree_ops/PruneNoOps.h"
#include "compiler/translator/tree_ops/RemoveArrayLengthMethod.h"
#include "compiler/translator/tree_ops/RemoveInactiveInterfaceVariables.h"
#include "compiler/translator/tree_ops/RemoveUnreferencedVariables.h"
#include "compiler/translator/tree_ops/RemoveUnusedFramebufferFetch.h"
#include "compiler/translator/tree_ops/RewritePixelLocalStorage.h"
#include "compiler/translator/tree_ops/ScalarizeVecAndMatConstructorArgs.h"
#include "compiler/translator/tree_ops/SeparateDeclarations.h"
#include "compiler/translator/tree_ops/SimplifyLoopConditions.h"
#include "compiler/translator/tree_ops/SplitSequenceOperator.h"
#include "compiler/translator/tree_ops/glsl/RewriteRepeatedAssignToSwizzled.h"
#include "compiler/translator/tree_ops/glsl/UseInterfaceBlockFields.h"
#include "compiler/translator/tree_ops/glsl/WrapStructConstructors.h"
#include "compiler/translator/tree_ops/glsl/apple/AddAndTrueToLoopCondition.h"
#include "compiler/translator/tree_ops/glsl/apple/UnfoldShortCircuitAST.h"
#include "compiler/translator/tree_util/FindSymbolNode.h"
#include "compiler/translator/tree_util/IntermNodePatternMatcher.h"
#include "compiler/translator/tree_util/IntermNode_util.h"
#include "compiler/translator/tree_util/ReplaceShadowingVariables.h"
#include "compiler/translator/tree_util/ReplaceVariable.h"
#include "compiler/translator/util.h"

// #define ANGLE_FUZZER_CORPUS_OUTPUT_DIR "corpus/"

#if defined(ANGLE_FUZZER_CORPUS_OUTPUT_DIR)
#    include "common/hash_utils.h"
#    include "common/mathutil.h"
#endif

namespace sh
{

namespace
{
// Helper that returns if a top-level node is unused.  If it's a function, the function prototype is
// returned as well.
bool IsTopLevelNodeUnusedFunction(const CallDAG &callDag,
                                  const std::vector<TFunctionMetadata> &metadata,
                                  TIntermNode *node,
                                  const TFunction **functionOut)
{
    const TIntermFunctionPrototype *asFunctionPrototype   = node->getAsFunctionPrototypeNode();
    const TIntermFunctionDefinition *asFunctionDefinition = node->getAsFunctionDefinition();

    *functionOut = nullptr;

    if (asFunctionDefinition)
    {
        *functionOut = asFunctionDefinition->getFunction();
    }
    else if (asFunctionPrototype)
    {
        *functionOut = asFunctionPrototype->getFunction();
    }
    if (*functionOut == nullptr)
    {
        return false;
    }

    size_t callDagIndex = callDag.findIndex((*functionOut)->uniqueId());
    if (callDagIndex == CallDAG::InvalidIndex)
    {
        // This happens only for unimplemented prototypes which are thus unused
        ASSERT(asFunctionPrototype);
        return true;
    }

    ASSERT(callDagIndex < metadata.size());
    return !metadata[callDagIndex].used;
}

void AddBuiltInToInitList(TSymbolTable *symbolTable,
                          int shaderVersion,
                          TIntermBlock *root,
                          const char *name,
                          InitVariableList *list)
{
    const TIntermSymbol *builtin = FindSymbolNode(root, ImmutableString(name));
    const TVariable *builtinVar  = nullptr;
    if (builtin != nullptr)
    {
        builtinVar = &builtin->variable();
    }
    else
    {
        builtinVar = static_cast<const TVariable *>(
            symbolTable->findBuiltIn(ImmutableString(name), shaderVersion));
    }

    if (builtinVar != nullptr)
    {
        list->push_back(builtinVar);
    }
}

#if defined(ANGLE_FUZZER_CORPUS_OUTPUT_DIR)
void DumpFuzzerCase(char const *const *shaderStrings,
                    size_t numStrings,
                    uint32_t type,
                    uint32_t spec,
                    uint32_t output,
                    const ShCompileOptions &options)
{
    ShaderDumpHeader header{};
    header.type   = type;
    header.spec   = spec;
    header.output = output;
    memcpy(&header.basicCompileOptions, &options, offsetof(ShCompileOptions, metal));
    static_assert(offsetof(ShCompileOptions, metal) <= sizeof(header.basicCompileOptions));
    memcpy(&header.metalCompileOptions, &options.metal, sizeof(options.metal));
    static_assert(sizeof(options.metal) <= sizeof(header.metalCompileOptions));
    memcpy(&header.plsCompileOptions, &options.pls, sizeof(options.pls));
    static_assert(sizeof(options.pls) <= sizeof(header.plsCompileOptions));
    size_t contentsLength = sizeof(header) + 1;  // Extra: header + nul terminator.
    for (size_t i = 0; i < numStrings; i++)
    {
        contentsLength += strlen(shaderStrings[i]);
    }
    std::vector<uint8_t> contents(rx::roundUp<size_t>(contentsLength, 4), 0);
    memcpy(&contents[0], &header, sizeof(header));
    uint8_t *data = &contents[sizeof(header)];
    for (size_t i = 0; i < numStrings; i++)
    {
        auto length = strlen(shaderStrings[i]);
        memcpy(data, shaderStrings[i], length);
        data += length;
    }
    auto hash = angle::ComputeGenericHash(contents);

    std::ostringstream o = sh::InitializeStream<std::ostringstream>();
    o << ANGLE_FUZZER_CORPUS_OUTPUT_DIR << std::hex << std::setw(16) << std::setfill('0') << hash
      << ".sample";
    std::string s = o.str();

    // Must match the input format of the fuzzer
    FILE *f = fopen(s.c_str(), "w");
    fwrite(contents.data(), sizeof(char), contentsLength, f);
    fclose(f);
}
#endif  // defined(ANGLE_FUZZER_CORPUS_OUTPUT_DIR)

// Helper function to check if the TIntermNode is a struct specifier
bool IsCurrentNodeStructTypeDeclaration(TIntermNode *node)
{
    TIntermDeclaration *declarationNode = node->getAsDeclarationNode();
    if (declarationNode != nullptr)
    {
        TIntermTyped *typeNode = declarationNode->getSequence()->front()->getAsTyped();
        if (typeNode != nullptr && (typeNode->getType().getBasicType() == EbtStruct &&
                                    typeNode->getType().isStructSpecifier()))
        {
            return true;
        }
    }
    return false;
}

// Helper function to check if the TIntermNode is a uniform type declaration, and it is not a struct
// specifier
bool IsCurrentNodeUniformDeclaration(TIntermNode *node)
{
    TIntermDeclaration *declarationNode = node->getAsDeclarationNode();
    if (declarationNode != nullptr)
    {
        TIntermTyped *typeNode = declarationNode->getSequence()->front()->getAsTyped();
        if (typeNode != nullptr && typeNode->getType().getQualifier() == TQualifier::EvqUniform &&
            !IsCurrentNodeStructTypeDeclaration(node))
        {
            return true;
        }
    }
    return false;
}

// Comparator function used for sorting shader uniforms
struct UniformSortComparator
{
    // returns true if the first < second, returns false otherwise
    bool operator()(TIntermNode *first, TIntermNode *second)
    {
        const TType &firstType = first->getAsDeclarationNode()
                                     ->getSequence()
                                     ->front()
                                     ->getAsSymbolNode()
                                     ->variable()
                                     .getType();
        const TType &secondType = second->getAsDeclarationNode()
                                      ->getSequence()
                                      ->front()
                                      ->getAsSymbolNode()
                                      ->variable()
                                      .getType();
        // If both uniforms are structs, do not reorder them
        if (firstType.getStruct() != nullptr && secondType.getStruct() != nullptr)
        {
            return false;
        }

        // Next sort by precisions
        // Group uniforms into high-precision and non-high-precision. A non-highp uniform is
        // considered "smaller" than a highp uniform.
        const TPrecision firstPrecision  = firstType.getPrecision();
        const TPrecision secondPrecision = secondType.getPrecision();
        const bool firstIsHighP          = (firstPrecision == TPrecision::EbpHigh);
        const bool secondIsHighP         = (secondPrecision == TPrecision::EbpHigh);
        if (firstIsHighP != secondIsHighP)
        {
            return secondIsHighP;
        }
        // If both are highp, they are equivalent. Do not reorder them.
        if (firstIsHighP)
        {
            return false;
        }
        // If we reach here, both uniforms are non-highp. We further sort them based on a list of
        // criteria
        ASSERT(firstType.getPrecision() != TPrecision::EbpHigh &&
               secondType.getPrecision() != TPrecision::EbpHigh);
        // criteria 1: sort by whether the uniform is a struct. Non-structs is smaller.
        if ((firstType.getStruct() == nullptr) != (secondType.getStruct() == nullptr))
        {
            return firstType.getStruct() == nullptr;
        }
        // criteria 2: sort by arrayness. Non-array element is smaller.
        if (firstType.isArray() != secondType.isArray())
        {
            return !firstType.isArray();
        }
        // criteria 3, non-matrix is smaller than matrix
        if (firstType.isMatrix() != secondType.isMatrix())
        {
            return !firstType.isMatrix();
        }
        // if both are matrix, sort by matrix size
        if (firstType.isMatrix() == secondType.isMatrix() && firstType.isMatrix())
        {
            if (firstType.getCols() != secondType.getCols())
            {
                return firstType.getCols() < secondType.getCols();
            }
            else
            {
                return firstType.getRows() < secondType.getRows();
            }
        }
        // criteria 4, non-vector is smaller
        if (firstType.isVector() != secondType.isVector())
        {
            return !firstType.isVector();
        }
        // if both are vectors, sort by vector size
        if (firstType.isVector() == secondType.isVector() && firstType.isVector())
        {
            return firstType.getNominalSize() < secondType.getNominalSize();
        }

        // If we can't determine which element is smaller based on previous criteria, consider first
        // and second as equivalent.
        return false;
    }
};

// To make CollectVariable's recursive algorithms more efficient, turn the "(uniform, field_chain)"
// tuples into a map of uniform to set of (nested) fields.
SamplersStaticallyUsedWithTexelFetch PreprocessSamplersStaticallyUsedWithTexelFetch(
    const TUnorderedSet<SamplerAccess> samplersStaticallyUsedWithTexelFetch)
{
    SamplersStaticallyUsedWithTexelFetch result;

    for (const SamplerAccess &access : samplersStaticallyUsedWithTexelFetch)
    {
        SelectedFields *fields = &result[access.uniform];
        for (uint32_t fieldIndex : access.fields)
        {
            auto [it, inserted] = fields->subfields.try_emplace(fieldIndex, nullptr);
            if (inserted)
            {
                it->second = new SelectedFields();
            }
            fields = it->second;
        }
    }

    return result;
}
}  // anonymous namespace

bool IsGLSL150OrNewer(ShShaderOutput output)
{
    return (output == SH_GLSL_150_CORE_OUTPUT || output == SH_GLSL_330_CORE_OUTPUT ||
            output == SH_GLSL_400_CORE_OUTPUT || output == SH_GLSL_410_CORE_OUTPUT ||
            output == SH_GLSL_420_CORE_OUTPUT || output == SH_GLSL_430_CORE_OUTPUT ||
            output == SH_GLSL_440_CORE_OUTPUT || output == SH_GLSL_450_CORE_OUTPUT);
}

bool IsGLSL420OrNewer(ShShaderOutput output)
{
    return (output == SH_GLSL_420_CORE_OUTPUT || output == SH_GLSL_430_CORE_OUTPUT ||
            output == SH_GLSL_440_CORE_OUTPUT || output == SH_GLSL_450_CORE_OUTPUT);
}

bool IsGLSL410OrOlder(ShShaderOutput output)
{
    return (output == SH_GLSL_150_CORE_OUTPUT || output == SH_GLSL_330_CORE_OUTPUT ||
            output == SH_GLSL_400_CORE_OUTPUT || output == SH_GLSL_410_CORE_OUTPUT);
}

size_t GetGlobalMaxTokenSize(ShShaderSpec spec)
{
    // WebGL defines a max token length of 256, while ES2 leaves max token
    // size undefined. ES3 defines a max size of 1024 characters.
    switch (spec)
    {
        case SH_WEBGL_SPEC:
            return 256;
        default:
            return 1024;
    }
}

int GetMaxUniformVectorsForShaderType(GLenum shaderType, const ShBuiltInResources &resources)
{
    switch (shaderType)
    {
        case GL_VERTEX_SHADER:
            return resources.MaxVertexUniformVectors;
        case GL_FRAGMENT_SHADER:
            return resources.MaxFragmentUniformVectors;

        // TODO (jiawei.shao@intel.com): check if we need finer-grained component counting
        case GL_COMPUTE_SHADER:
            return resources.MaxComputeUniformComponents / 4;
        case GL_GEOMETRY_SHADER_EXT:
            return resources.MaxGeometryUniformComponents / 4;
        default:
            UNREACHABLE();
            return -1;
    }
}

namespace
{
class [[nodiscard]] TScopedSymbolTableLevel
{
  public:
    TScopedSymbolTableLevel(TSymbolTable *table) : mTable(table)
    {
        ASSERT(mTable->isEmpty());
        mTable->push();
    }
    ~TScopedSymbolTableLevel()
    {
        while (!mTable->isEmpty())
        {
            mTable->pop();
        }
    }

  private:
    TSymbolTable *mTable;
};

}  // namespace

int GetMaxShaderVersionForSpec(ShShaderSpec spec)
{
    switch (spec)
    {
        case SH_GLES2_SPEC:
        case SH_WEBGL_SPEC:
            return 100;
        case SH_GLES3_SPEC:
        case SH_WEBGL2_SPEC:
            return 300;
        case SH_GLES3_1_SPEC:
            return 310;
        case SH_GLES3_2_SPEC:
            return 320;
        default:
            UNREACHABLE();
            return 0;
    }
}

TShHandleBase::TShHandleBase()
{
    SetGlobalPoolAllocator(&allocator);
}

TShHandleBase::~TShHandleBase()
{
    SetGlobalPoolAllocator(nullptr);
}

TCompiler::TCompiler(sh::GLenum type, ShShaderSpec spec, ShShaderOutput output)
    : mShaderType(type),
      mShaderSpec(spec),
      mOutputType(output),
      mDiagnostics(mInfoSink.info),
      mSourcePath(nullptr),
      mVariablesCollected(false),
      mGLPositionInitialized(false),
      mComputeShaderLocalSizeDeclared(false),
      mComputeShaderLocalSize(1),
      mGeometryShaderMaxVertices(-1),
      mGeometryShaderInvocations(0),
      mGeometryShaderInputPrimitiveType(EptUndefined),
      mGeometryShaderOutputPrimitiveType(EptUndefined),
      mTessControlShaderOutputVertices(0),
      mTessEvaluationShaderInputPrimitiveType(EtetUndefined),
      mTessEvaluationShaderInputVertexSpacingType(EtetUndefined),
      mTessEvaluationShaderInputOrderingType(EtetUndefined),
      mTessEvaluationShaderInputPointType(EtetUndefined),
      mHasAnyPreciseType(false),
      mAdvancedBlendEquations(0),
      mUsesDerivatives(false),
      mCompileOptions{}
{}

TCompiler::~TCompiler() {}

bool TCompiler::shouldRunLoopAndIndexingValidation() const
{
    // If compiling an ESSL 1.00 shader for WebGL, validate loop and indexing as well (to verify
    // that the shader only uses minimal functionality of ESSL 1.00 as in Appendix A of the spec).
    return IsWebGLBasedSpec(mShaderSpec) && mShaderVersion == 100;
}

bool TCompiler::Init(const ShBuiltInResources &resources)
{
    SetGlobalPoolAllocator(&allocator);

    // Generate built-in symbol table.
    if (!initBuiltInSymbolTable(resources))
    {
        return false;
    }

    mResources = resources;
    setResourceString();

    InitExtensionBehavior(resources, mExtensionBehavior);
    return true;
}

TIntermBlock *TCompiler::compileTreeForTesting(angle::Span<const char *const> shaderStrings,
                                               const ShCompileOptions &compileOptionsIn)
{
    ResetExtensionBehavior(mResources, mExtensionBehavior, compileOptionsIn);

    const ShCompileOptions compileOptions = adjustOptions(compileOptionsIn);
    return compileTreeImpl(shaderStrings, compileOptions);
}

TIntermBlock *TCompiler::compileTreeImpl(angle::Span<const char *const> shaderStrings,
                                         const ShCompileOptions &compileOptions)
{
    // Remember the compile options for helper functions such as validateAST.
    mCompileOptions = compileOptions;

    clearResults();

    ASSERT(!shaderStrings.empty());
    ASSERT(GetGlobalPoolAllocator());

    // First string is path of source file if flag is set. The actual source follows.
    size_t firstSource = 0;
    if (compileOptions.sourcePath)
    {
        mSourcePath = shaderStrings[0];
        ++firstSource;
    }

    TParseContext parseContext(mSymbolTable, mExtensionBehavior, mShaderType, mShaderSpec,
                               compileOptions, &mDiagnostics, mResources, getOutputType());

    // We preserve symbols at the built-in level from compile-to-compile.
    // Start pushing the user-defined symbols at global level.
    TScopedSymbolTableLevel globalLevel(&mSymbolTable);
    ASSERT(mSymbolTable.atGlobalLevel());

    // Parse shader.
    if (PaParseStrings(shaderStrings.subspan(firstSource), nullptr, &parseContext) != 0)
    {
        return nullptr;
    }

    if (!parseContext.postParseChecks())
    {
        return nullptr;
    }

    setShaderMetadata(parseContext);

    TIntermBlock *root = parseContext.getTreeRoot();
#ifdef ANGLE_IR
    if (compileOptions.useIR)
    {
        ASSERT(root == nullptr);
        ir::IR ir = parseContext.getIR();

        // Create an AST out of the IR while the rest of the translator is still AST based.
        ir::Output output = ir::GenerateAST(std::move(ir), this, compileOptions);
        root              = output.root;

        // Vertex shader inputs and compute shader built-ins are gathered as "inputs" in IR, but are
        // exposed as an "attributes" list.
        if (mShaderType == GL_VERTEX_SHADER || mShaderType == GL_COMPUTE_SHADER)
        {
            mAttributes = std::move(output.inputs);
        }
        else
        {
            mInputVaryings = std::move(output.inputs);
        }
        // Fragment shader outputs are exposed in a separate list from the other shader stages.
        if (mShaderType == GL_FRAGMENT_SHADER)
        {
            mOutputVariables = std::move(output.outputs);
        }
        else
        {
            mOutputVaryings = std::move(output.outputs);
        }
        mUniforms            = std::move(output.uniforms);
        mSharedVariables     = std::move(output.shared);
        mUniformBlocks       = std::move(output.uniformBlocks);
        mShaderStorageBlocks = std::move(output.storageBlocks);
        collectInterfaceBlocks();

        mVariablesCollected = true;
    }
#endif
    ASSERT(root != nullptr);

    const bool hasAnyClipCullDistance =
        parseContext.isExtensionEnabled(TExtension::ANGLE_clip_cull_distance) ||
        parseContext.isExtensionEnabled(TExtension::EXT_clip_cull_distance) ||
        parseContext.isExtensionEnabled(TExtension::APPLE_clip_distance);
    if (hasAnyClipCullDistance)
    {
        mClipDistanceSize = static_cast<uint8_t>(parseContext.getClipDistanceArraySize());
        mCullDistanceSize = static_cast<uint8_t>(parseContext.getCullDistanceArraySize());
        mMetadataFlags[MetadataFlags::HasClipDistance] = parseContext.isClipDistanceUsed();
    }

    mValidateASTOptions = {};
    if (!compileOptions.useIR)
    {
        if (!checkAndSimplifyAST(root, parseContext, compileOptions))
        {
            return nullptr;
        }
    }

    return root;
}



void TCompiler::setShaderMetadata(const TParseContext &parseContext)
{
    mShaderVersion = parseContext.getShaderVersion();

    mPragma = parseContext.pragma();
    mSymbolTable.setGlobalInvariant(mPragma.stdgl.invariantAll);

    mEarlyFragmentTestsSpecified = parseContext.isEarlyFragmentTestsSpecified();

    mMetadataFlags[MetadataFlags::HasDiscard] = parseContext.hasDiscard();
    mMetadataFlags[MetadataFlags::EnablesPerSampleShading] =
        parseContext.isSampleQualifierSpecified();

    mComputeShaderLocalSizeDeclared = parseContext.isComputeShaderLocalSizeDeclared();
    mComputeShaderLocalSize         = parseContext.getComputeShaderLocalSize();

    mNumViews = parseContext.getNumViews();

    mHasAnyPreciseType = parseContext.hasAnyPreciseType();

    mUsesDerivatives = parseContext.usesDerivatives();

    if (mShaderType == GL_FRAGMENT_SHADER)
    {
        mAdvancedBlendEquations = parseContext.getAdvancedBlendEquations();
        const std::map<int, ShPixelLocalStorageLayout> &plsLayouts =
            parseContext.pixelLocalStorageLayouts();
        // std::map keys are in sorted order, so the PLS uniform with the largest binding will be at
        // rbegin().
        mPixelLocalStorageLayouts.resize(plsLayouts.empty() ? 0 : plsLayouts.rbegin()->first + 1);
        for (const auto &[binding, layout] : plsLayouts)
        {
            mPixelLocalStorageLayouts[binding] = layout;
        }
    }
    if (mShaderType == GL_GEOMETRY_SHADER_EXT)
    {
        mGeometryShaderInputPrimitiveType  = parseContext.getGeometryShaderInputPrimitiveType();
        mGeometryShaderOutputPrimitiveType = parseContext.getGeometryShaderOutputPrimitiveType();
        mGeometryShaderMaxVertices         = parseContext.getGeometryShaderMaxVertices();
        mGeometryShaderInvocations         = parseContext.getGeometryShaderInvocations();

        mMetadataFlags[MetadataFlags::HasValidGeometryShaderInputPrimitiveType] =
            mGeometryShaderInputPrimitiveType != EptUndefined;
        mMetadataFlags[MetadataFlags::HasValidGeometryShaderOutputPrimitiveType] =
            mGeometryShaderOutputPrimitiveType != EptUndefined;
        mMetadataFlags[MetadataFlags::HasValidGeometryShaderMaxVertices] =
            mGeometryShaderMaxVertices >= 0;
    }
    if (mShaderType == GL_TESS_CONTROL_SHADER_EXT)
    {
        mTessControlShaderOutputVertices = parseContext.getTessControlShaderOutputVertices();
    }
    if (mShaderType == GL_TESS_EVALUATION_SHADER_EXT)
    {
        mTessEvaluationShaderInputPrimitiveType =
            parseContext.getTessEvaluationShaderInputPrimitiveType();
        mTessEvaluationShaderInputVertexSpacingType =
            parseContext.getTessEvaluationShaderInputVertexSpacingType();
        mTessEvaluationShaderInputOrderingType =
            parseContext.getTessEvaluationShaderInputOrderingType();
        mTessEvaluationShaderInputPointType = parseContext.getTessEvaluationShaderInputPointType();

        mMetadataFlags[MetadataFlags::HasValidTessGenMode] =
            mTessEvaluationShaderInputPrimitiveType != EtetUndefined;
        mMetadataFlags[MetadataFlags::HasValidTessGenSpacing] =
            mTessEvaluationShaderInputVertexSpacingType != EtetUndefined;
        mMetadataFlags[MetadataFlags::HasValidTessGenVertexOrder] =
            mTessEvaluationShaderInputOrderingType != EtetUndefined;
        mMetadataFlags[MetadataFlags::HasValidTessGenPointMode] =
            mTessEvaluationShaderInputPointType != EtetUndefined;
    }
}

unsigned int TCompiler::getSharedMemorySize() const
{
    unsigned int sharedMemSize = 0;
    for (const sh::ShaderVariable &var : mSharedVariables)
    {
        sharedMemSize += var.getExternalSize();
    }

    return sharedMemSize;
}

bool TCompiler::getShaderBinary(const ShHandle compilerHandle,
                                angle::Span<const char *const> shaderStrings,
                                const ShCompileOptions &compileOptions,
                                ShaderBinaryBlob *const binaryOut)
{
    if (!compile(shaderStrings, compileOptions))
    {
        return false;
    }

    gl::BinaryOutputStream stream;
    gl::ShaderType shaderType = gl::FromGLenum<gl::ShaderType>(mShaderType);
    gl::CompiledShaderState state(shaderType);
    state.buildCompiledShaderState(compilerHandle, mOutputType);

    stream.writeBytes(
        // NOTE: version api could return a string view.
        ANGLE_UNSAFE_TODO(angle::Span(
            reinterpret_cast<const unsigned char *>(angle::GetANGLEShaderProgramVersion()),
            angle::GetANGLEShaderProgramVersionHashSize())));
    stream.writeEnum(shaderType);
    stream.writeEnum(mOutputType);

    // Serialize the full source string for the shader. Ignore the source path if it is provided.
    std::string sourceString;
    size_t startingIndex = compileOptions.sourcePath ? 1 : 0;
    for (const char *str : shaderStrings.subspan(startingIndex))
    {
        sourceString.append(str);
    }
    stream.writeString(sourceString);

    stream.writeBytes(angle::byte_span_from_ref(compileOptions));
    stream.writeBytes(angle::byte_span_from_ref(mResources));

    state.serialize(stream);

    *binaryOut = stream.takeData();
    return true;
}

bool TCompiler::validateAST(TIntermNode *root)
{
    if (mCompileOptions.validateAST)
    {
        bool valid = ValidateAST(root, &mDiagnostics, mValidateASTOptions);

#if defined(ANGLE_ENABLE_ASSERTS)
        if (!valid)
        {
            OutputTree(root, mInfoSink.info);
            ANGLE_UNSAFE_TODO(
                fprintf(stderr, "AST validation error(s):\n%s\n", mInfoSink.info.c_str()));
        }
#endif
        // In debug, assert validation.  In release, validation errors will be returned back to the
        // application as internal ANGLE errors.
        ASSERT(valid);

        return valid;
    }
    return true;
}

bool TCompiler::disableValidateFunctionCall()
{
    bool wasEnabled                          = mValidateASTOptions.validateFunctionCall;
    mValidateASTOptions.validateFunctionCall = false;
    return wasEnabled;
}

void TCompiler::restoreValidateFunctionCall(bool enable)
{
    ASSERT(!mValidateASTOptions.validateFunctionCall);
    mValidateASTOptions.validateFunctionCall = enable;
}

bool TCompiler::disableValidateVariableReferences()
{
    bool wasEnabled                                = mValidateASTOptions.validateVariableReferences;
    mValidateASTOptions.validateVariableReferences = false;
    return wasEnabled;
}

void TCompiler::restoreValidateVariableReferences(bool enable)
{
    ASSERT(!mValidateASTOptions.validateVariableReferences);
    mValidateASTOptions.validateVariableReferences = enable;
}

void TCompiler::enableValidateNoMoreTransformations()
{
    mValidateASTOptions.validateNoMoreTransformations = true;
}

bool TCompiler::checkAndSimplifyAST(TIntermBlock *root,
                                    const TParseContext &parseContext,
                                    const ShCompileOptions &compileOptions)
{
    ASSERT(!compileOptions.useIR);

    // Disallow expressions deemed too complex.
    // This needs to be checked before other functions that will traverse the AST
    // to prevent potential stack overflow crashes.
    if (compileOptions.limitExpressionComplexity && !limitExpressionComplexity(root))
    {
        return false;
    }

    // Some AST validation cannot be done until an AST pass is done.
    mValidateASTOptions.validateNoStatementsAfterBranch = false;
    mValidateASTOptions.validateMultiDeclarations       = false;

    if (!validateAST(root))
    {
        return false;
    }

    // Turn |inout| variables that are never read from into |out| before collecting variables
    // and before PLS uses them.
    if (mShaderVersion >= 300 &&
        (IsExtensionEnabled(mExtensionBehavior, TExtension::EXT_shader_framebuffer_fetch) ||
         IsExtensionEnabled(mExtensionBehavior,
                            TExtension::EXT_shader_framebuffer_fetch_non_coherent)))
    {
        if (!RemoveUnusedFramebufferFetch(this, root, &mSymbolTable))
        {
            return false;
        }
    }

    // Fold expressions that could not be folded before validation or otherwise that was done as a
    // part of parsing.
    if (!FoldExpressions(this, root, &mDiagnostics))
    {
        return false;
    }
    // Folding should only be able to generate warnings.
    ASSERT(mDiagnostics.numErrors() == 0);

    const bool hasAnyClipCullDistance =
        parseContext.isExtensionEnabled(TExtension::ANGLE_clip_cull_distance) ||
        parseContext.isExtensionEnabled(TExtension::EXT_clip_cull_distance) ||
        parseContext.isExtensionEnabled(TExtension::APPLE_clip_distance);
    if (hasAnyClipCullDistance)
    {
        // gl_ClipDistance and gl_CullDistance built-in arrays have unique semantics.
        // They are pre-declared as unsized and must be sized by the shader either
        // redeclaring them or indexing them only with integral constant expressions.
        // The translator treats them as having the maximum allowed size and this pass
        // applies the actual sizes if needed.
        if (mClipDistanceSize > 0 && !parseContext.isClipDistanceRedeclared() &&
            !SizeClipCullDistance(this, root, ImmutableString("gl_ClipDistance"),
                                  mClipDistanceSize))
        {

            return false;
        }
        if (mCullDistanceSize > 0 && !parseContext.isCullDistanceRedeclared() &&
            !SizeClipCullDistance(this, root, ImmutableString("gl_CullDistance"),
                                  mCullDistanceSize))
        {
            return false;
        }
    }

    // We prune no-ops to work around driver bugs and to keep AST processing and output simple.
    // The following kinds of no-ops are pruned:
    //   1. Empty declarations "int;".
    //   2. Literal statements: "1.0;". The ESSL output doesn't define a default precision
    //      for float, so float literal statements would end up with no precision which is
    //      invalid ESSL.
    //   3. Any unreachable statement after a discard, return, break or continue.
    // After this empty declarations are not allowed in the AST.
    if (!PruneNoOps(this, root, &mSymbolTable))
    {
        return false;
    }
    mValidateASTOptions.validateNoStatementsAfterBranch = true;

    // We need to generate globals early if we have non constant initializers enabled.
    bool initializeLocalsAndGlobals    = compileOptions.initializeUninitializedLocals;
    bool canUseLoopsToInitialize       = !compileOptions.dontUseLoopsToInitializeVariables;
    bool enableNonConstantInitializers = IsExtensionEnabled(
        mExtensionBehavior, TExtension::EXT_shader_non_constant_global_initializers);

    if (enableNonConstantInitializers &&
        !DeferGlobalInitializers(this, root, initializeLocalsAndGlobals, canUseLoopsToInitialize,
                                 compileOptions.forceDeferNonConstGlobalInitializers,
                                 &mSymbolTable))
    {
        return false;
    }

    // Create the function DAG.
    initCallDag(root);

    // Checks which functions are used
    mFunctionMetadata.clear();
    mFunctionMetadata.resize(mCallDag.size());
    tagUsedFunctions();

    if (!pruneUnusedFunctions(root))
    {
        return false;
    }

    if (IsSpecWithFunctionBodyNewScope(mShaderSpec, mShaderVersion))
    {
        if (!ReplaceShadowingVariables(this, root, &mSymbolTable))
        {
            return false;
        }
    }

    // For now, rewrite pixel local storage before collecting variables or any operations on
    // images.
    //
    // TODO(anglebug.com/40096838):
    //   Should this actually run after collecting variables?
    //   Do we need more introspection?
    //   Do we want to hide rewritten shader image uniforms from glGetActiveUniform?
    if (hasPixelLocalStorageUniforms())
    {
        ASSERT(
            IsExtensionEnabled(mExtensionBehavior, TExtension::ANGLE_shader_pixel_local_storage));
        if (!RewritePixelLocalStorage(this, root, getSymbolTable(), compileOptions,
                                      getShaderVersion()))
        {
            return false;
        }
    }

    if (compileOptions.initializeBuiltinsForInstancedMultiview &&
        (parseContext.isExtensionEnabled(TExtension::OVR_multiview2) ||
         parseContext.isExtensionEnabled(TExtension::OVR_multiview)))
    {
        // Note: if multiview is enabled via #extension all, num_views may not be set.
        if (!DeclareAndInitBuiltinsForInstancedMultiview(this, root, std::max(mNumViews, 1),
                                                         mShaderType, compileOptions, mOutputType,
                                                         &mSymbolTable))
        {
            return false;
        }
    }

    if (compileOptions.addAndTrueToLoopCondition)
    {
        if (!AddAndTrueToLoopCondition(this, root))
        {
            return false;
        }
    }

    if (compileOptions.unfoldShortCircuit)
    {
        if (!UnfoldShortCircuitAST(this, root))
        {
            return false;
        }
    }

    if (compileOptions.emulateGLDrawID &&
        IsExtensionEnabled(mExtensionBehavior, TExtension::ANGLE_multi_draw))
    {
        if (!EmulateGLDrawID(this, root, &mSymbolTable))
        {
            return false;
        }
    }

    if (compileOptions.emulateGLBaseVertexBaseInstance &&
        IsExtensionEnabled(mExtensionBehavior,
                           TExtension::ANGLE_base_vertex_base_instance_shader_builtin))
    {
        if (!EmulateGLBaseVertexBaseInstance(this, root, &mSymbolTable,
                                             compileOptions.addBaseVertexToVertexID))
        {
            return false;
        }
    }

    if (mShaderType == GL_FRAGMENT_SHADER && mShaderVersion == 100 && mResources.EXT_draw_buffers &&
        mResources.MaxDrawBuffers > 1 &&
        IsExtensionEnabled(mExtensionBehavior, TExtension::EXT_draw_buffers))
    {
        // In WebGL2, gl_FragData has only one element.  But in WebGL2, EXT_draw_buffers is not a
        // supported extension.
        ASSERT(mShaderSpec != SH_WEBGL2_SPEC);
        if (!EmulateGLFragColorBroadcast(this, root, mResources.MaxDrawBuffers,
                                         mResources.MaxDualSourceDrawBuffers, &mSymbolTable,
                                         mShaderVersion))
        {
            return false;
        }
    }

    if (!sortUniforms(root))
    {
        return false;
    }

    // Needs to run before SimplifyLoopConditions to be able to detect |for| loops correctly.
    if (compileOptions.ensureLoopForwardProgress)
    {
        EnsureLoopForwardProgressMode mode = mOutputType == SH_MSL_METAL_OUTPUT
                                                 ? EnsureLoopForwardProgressMode::Volatile
                                                 : EnsureLoopForwardProgressMode::Counter;
        if (!EnsureLoopForwardProgress(this, root, mode))
        {
            return false;
        }
    }

    if (compileOptions.simplifyLoopConditions)
    {
        if (!SimplifyLoopConditions(this, root, &getSymbolTable()))
        {
            return false;
        }
    }
    else
    {
        // Split multi declarations and remove calls to array length().
        // Note that SimplifyLoopConditions needs to be run before any other AST transformations
        // that may need to generate new statements from loop conditions or loop expressions.
        if (!SimplifyLoopConditions(this, root,
                                    IntermNodePatternMatcher::kMultiDeclaration |
                                        IntermNodePatternMatcher::kArrayLengthMethod,
                                    &getSymbolTable()))
        {
            return false;
        }
    }

    // Note that separate declarations need to be run before other AST transformations that
    // generate new statements from expressions.
    if (!SeparateDeclarations(*this, *root, mCompileOptions.separateCompoundStructDeclarations))
    {
        return false;
    }
    mValidateASTOptions.validateMultiDeclarations = true;

    // Move declarations before functions to simplify transformations.
    MoveDeclarationsBeforeFunctions(root);

    if (!SplitSequenceOperator(this, root, IntermNodePatternMatcher::kArrayLengthMethod,
                               &getSymbolTable()))
    {
        return false;
    }

    if (!RemoveArrayLengthMethod(this, root))
    {
        return false;
    }
    // Fold the expressions again, because |RemoveArrayLengthMethod| can introduce new
    // constants.
    if (!FoldExpressions(this, root, &mDiagnostics))
    {
        return false;
    }

    // Fold switch statements with constant expression.  Run after FoldExpressions because the
    // switch selector may need folding.  This pass may leave dead code, so prune it again
    // afterwards.
    mValidateASTOptions.validateNoStatementsAfterBranch = false;
    if (!FoldConstantSwitch(this, root, &mSymbolTable))
    {
        return false;
    }
    if (!PruneNoOps(this, root, &mSymbolTable))
    {
        return false;
    }
    mValidateASTOptions.validateNoStatementsAfterBranch = true;

    if (!RemoveUnreferencedVariables(this, root, &mSymbolTable))
    {
        return false;
    }

    // In case the last case inside a switch statement is a certain type of no-op, GLSL
    // compilers in drivers may not accept it. In this case we clean up the dead code from the
    // end of switch statements. This is also required because PruneNoOps or
    // RemoveUnreferencedVariables may have left switch statements that only contained an empty
    // declaration inside the final case in an invalid state. Relies on that PruneNoOps and
    // RemoveUnreferencedVariables have already been run.
    if (!PruneEmptyCases(this, root))
    {
        return false;
    }

    const SamplersStaticallyUsedWithTexelFetch samplersStaticallyUsedWithTexelFetch =
        PreprocessSamplersStaticallyUsedWithTexelFetch(
            parseContext.getSamplersStaticallyUsedWithTexelFetch());
    collectVariables(root, samplersStaticallyUsedWithTexelFetch);

    if (compileOptions.useUnusedStandardSharedBlocks)
    {
        if (!useAllMembersInUnusedStandardAndSharedBlocks(root))
        {
            return false;
        }
    }

    if (compileOptions.enforcePackingRestrictions)
    {
        int maxUniformVectors = GetMaxUniformVectorsForShaderType(mShaderType, mResources);
        if (mShaderType == GL_VERTEX_SHADER && compileOptions.emulateClipOrigin)
        {
            --maxUniformVectors;
        }
        // Returns true if, after applying the packing rules in the GLSL ES 1.00.17 spec
        // Appendix A, section 7, the shader does not use too many uniforms.
        if (!CheckVariablesInPackingLimits(maxUniformVectors, mUniforms))
        {
            mDiagnostics.globalError("too many uniforms");
            return false;
        }
    }

    if (compileOptions.scalarizeVecAndMatConstructorArgs)
    {
        if (!ScalarizeVecAndMatConstructorArgs(this, root, &mSymbolTable))
        {
            return false;
        }
    }

    if (compileOptions.clampIndirectArrayBounds)
    {
        if (!ClampIndirectIndices(this, root, &mSymbolTable, mExtensionBehavior))
        {
            return false;
        }
    }

    // Remove declarations of inactive shader interface variables so backends don't need to
    // account for them.  Note that currently, CollectVariables marks every field of an active
    // uniform that's of struct type as active, i.e. no extracted sampler is inactive, so this
    // can be done before extracting samplers from structs.
    //
    // For the MSL output, keep the inactive fragment outputs, but remove them otherwise.
    if (compileOptions.removeInactiveVariables)
    {
        if (!RemoveInactiveInterfaceVariables(this, root, &getSymbolTable(), getAttributes(),
                                              getInputVaryings(), getOutputVariables(),
                                              getUniforms(), getInterfaceBlocks(),
                                              !compileOptions.retainInactiveFragmentOutputs))
        {
            return false;
        }
    }

    if (compileOptions.initOutputVariables)
    {
        if (!initializeOutputVariables(root))
        {
            return false;
        }
    }

    // gl_Position may have already been initialized among other output variables, in that case
    // we don't need to initialize it twice.
    if (!mGLPositionInitialized && compileOptions.initGLPosition)
    {
        if (!initializeGLPosition(root))
        {
            return false;
        }
        mGLPositionInitialized = true;
    }

    if (mShaderType == GL_VERTEX_SHADER && compileOptions.initGLPointSize)
    {
        InitVariableList list;
        AddBuiltInToInitList(&mSymbolTable, mShaderVersion, root, "gl_PointSize", &list);

        if (!list.empty() && !InitializeVariables(this, root, list, &mSymbolTable, mShaderVersion,
                                                 mExtensionBehavior, false))
        {
            return false;
        }
    }

    // DeferGlobalInitializers needs to be run before other AST transformations that generate
    // new statements from expressions. But it's fine to run DeferGlobalInitializers after the
    // above SplitSequenceOperator and RemoveArrayLengthMethod since they only have an effect on
    // the AST on ESSL >= 3.00, and the initializers that need to be deferred can only exist in
    // ESSL < 3.00.  Exception: if EXT_shader_non_constant_global_initializers is enabled, we
    // must generate global initializers before we generate the DAG, since initializers may call
    // functions which must not be optimized out
    if (!enableNonConstantInitializers &&
        !DeferGlobalInitializers(this, root, initializeLocalsAndGlobals, canUseLoopsToInitialize,
                                 compileOptions.forceDeferNonConstGlobalInitializers,
                                 &mSymbolTable))
    {
        return false;
    }

    if (initializeLocalsAndGlobals)
    {
        // Initialize uninitialized local variables.
        // In some cases initializing can generate extra statements in the parent block, such as
        // when initializing nameless structs or initializing arrays in ESSL 1.00. In that case
        // we need to first simplify loop conditions. We've already separated declarations
        // earlier, which is also required. If we don't follow the Appendix A limitations, loop
        // init statements can declare arrays or nameless structs and have multiple
        // declarations.

        if (!shouldRunLoopAndIndexingValidation())
        {
            if (!SimplifyLoopConditions(this, root, IntermNodePatternMatcher::kArrayDeclaration,
                                        &getSymbolTable()))
            {
                return false;
            }
        }

        if (!InitializeUninitializedLocals(this, root, getShaderVersion(), canUseLoopsToInitialize,
                                           &getSymbolTable()))
        {
            return false;
        }
    }

    if (compileOptions.avoidComplexExpressionsInStructConstructor)
    {
        // Must run after InitializeUninitializedLocals, as that would introduce struct constructors
        if (!WrapStructConstructors(this, root, &mSymbolTable))
        {
            return false;
        }
    }

    if (compileOptions.clampPointSize)
    {
        if (!ClampPointSize(this, root, mResources.MinPointSize, mResources.MaxPointSize,
                            &getSymbolTable()))
        {
            return false;
        }
    }

    if (compileOptions.clampFragDepth)
    {
        if (!ClampFragDepth(this, root, &getSymbolTable()))
        {
            return false;
        }
    }

    if (compileOptions.rewriteRepeatedAssignToSwizzled)
    {
        if (!sh::RewriteRepeatedAssignToSwizzled(this, root))
        {
            return false;
        }
    }

    if (compileOptions.expandFragmentOutputsToVec4 && mShaderVersion >= 300)
    {
        if (!ExpandFragmentOutputsToVec4(this, root, &getSymbolTable()))
        {
            return false;
        }
    }

    return true;
}

ShCompileOptions TCompiler::adjustOptions(const ShCompileOptions &compileOptionsIn)
{
    ShCompileOptions compileOptions = compileOptionsIn;

    // Apply key workarounds.
    if (shouldFlattenPragmaStdglInvariantAll())
    {
        // This should be harmless to do in all cases, but for the moment, do it only conditionally.
        compileOptions.flattenPragmaSTDGLInvariantAll = true;
    }

    // Disable options that are not applicable.
    if (mShaderType == GL_COMPUTE_SHADER)
    {
        compileOptions.initOutputVariables                     = false;
        compileOptions.initializeBuiltinsForInstancedMultiview = false;
    }
    if (mShaderType != GL_VERTEX_SHADER)
    {
        compileOptions.initGLPosition                  = false;
        compileOptions.emulateGLDrawID                 = false;
        compileOptions.emulateGLBaseVertexBaseInstance = false;
        // Note: technically clamping gl_PointSize should be done in the last pre-rasterization
        // stage, but is currently only done in the vertex shader.
        compileOptions.clampPointSize = false;
    }
    if (mShaderType != GL_FRAGMENT_SHADER)
    {
        compileOptions.clampFragDepth = false;
        compileOptions.retainInactiveFragmentOutputs = false;
        compileOptions.expandFragmentOutputsToVec4   = false;
    }

#if !defined(ANGLE_IR)
    compileOptions.useIR = false;
#endif

    return compileOptions;
}

bool TCompiler::compile(angle::Span<const char *const> shaderStrings,
                        const ShCompileOptions &compileOptionsIn)
{
#if defined(ANGLE_FUZZER_CORPUS_OUTPUT_DIR)
    DumpFuzzerCase(shaderStrings.data(), shaderStrings.size(), mShaderType, mShaderSpec,
                   mOutputType, compileOptionsIn);
#endif  // defined(ANGLE_FUZZER_CORPUS_OUTPUT_DIR)

    if (shaderStrings.empty())
    {
        return true;
    }

    // Certain extensions are not allowed in WebGL2.
    if (mShaderSpec == SH_WEBGL2_SPEC)
    {
        ASSERT(!mResources.EXT_frag_depth && !mResources.EXT_shader_texture_lod &&
               !mResources.EXT_draw_buffers && !mResources.NV_draw_buffers);
    }

    // Reset the extension behavior for each compilation unit.  Support for some extensions depends
    // on compile flags.  This is done before resetting the flags that don't apply to some shader
    // stages because extensions are either exposed to all or none of the stages.
    ResetExtensionBehavior(mResources, mExtensionBehavior, compileOptionsIn);

    const ShCompileOptions compileOptions = adjustOptions(compileOptionsIn);

    TScopedPoolAllocator scopedAlloc;
    TIntermBlock *root = compileTreeImpl(shaderStrings, compileOptions);

    if (root)
    {
        if (compileOptions.intermediateTree)
        {
            OutputTree(root, mInfoSink.info);
        }

        if (compileOptions.objectCode)
        {
            PerformanceDiagnostics perfDiagnostics(&mDiagnostics);
            if (!translate(root, compileOptions, &perfDiagnostics))
            {
                return false;
            }
        }

        // For simplicity, this substitution of the name is done after all the transformations are
        // done.  A number of transformations and generators rely on being able to find
        // ShaderVariables by matching the |name| field with |TVariable::name|.
        bool lookForDrawID = IsExtensionEnabled(mExtensionBehavior, TExtension::ANGLE_multi_draw) &&
                             compileOptions.emulateGLDrawID;
        bool lookForBaseVertexBaseInstance =
            IsExtensionEnabled(mExtensionBehavior,
                               TExtension::ANGLE_base_vertex_base_instance_shader_builtin) &&
            compileOptions.emulateGLBaseVertexBaseInstance;

        if (lookForDrawID || lookForBaseVertexBaseInstance)
        {
            ASSERT(mShaderType == GL_VERTEX_SHADER);
            for (auto &uniform : mUniforms)
            {
                if (lookForDrawID && uniform.name == "angle_DrawID" &&
                    uniform.mappedName == "angle_DrawID")
                {
                    uniform.name = "gl_DrawID";
                }
                else if (lookForBaseVertexBaseInstance && uniform.name == "angle_BaseVertex" &&
                         uniform.mappedName == "angle_BaseVertex")
                {
                    uniform.name = "gl_BaseVertex";
                }
                else if (lookForBaseVertexBaseInstance && uniform.name == "angle_BaseInstance" &&
                         uniform.mappedName == "angle_BaseInstance")
                {
                    uniform.name = "gl_BaseInstance";
                }
            }
        }

        // The IntermNode tree doesn't need to be deleted here, since the
        // memory will be freed in a big chunk by the PoolAllocator.
        return true;
    }
    return false;
}

bool TCompiler::initBuiltInSymbolTable(const ShBuiltInResources &resources)
{
    if (resources.MaxDrawBuffers < 1)
    {
        return false;
    }
    if (resources.EXT_blend_func_extended && resources.MaxDualSourceDrawBuffers < 1)
    {
        return false;
    }

    mSymbolTable.initializeBuiltIns(mShaderType, mShaderSpec, resources);

    return true;
}

void TCompiler::setResourceString()
{
    std::ostringstream strstream = sh::InitializeStream<std::ostringstream>();

    // clang-format off
    strstream << ":MaxVertexAttribs:" << mResources.MaxVertexAttribs
        << ":MaxVertexUniformVectors:" << mResources.MaxVertexUniformVectors
        << ":MaxVaryingVectors:" << mResources.MaxVaryingVectors
        << ":MaxVertexTextureImageUnits:" << mResources.MaxVertexTextureImageUnits
        << ":MaxCombinedTextureImageUnits:" << mResources.MaxCombinedTextureImageUnits
        << ":MaxTextureImageUnits:" << mResources.MaxTextureImageUnits
        << ":MaxFragmentUniformVectors:" << mResources.MaxFragmentUniformVectors
        << ":MaxDrawBuffers:" << mResources.MaxDrawBuffers
        << ":ShadingRateFlag2VerticalPixelsEXT:" << mResources.ShadingRateFlag2VerticalPixelsEXT
        << ":ShadingRateFlag2VerticalPixelsEXT:" << mResources.ShadingRateFlag2VerticalPixelsEXT
        << ":ShadingRateFlag2HorizontalPixelsEXT:" << mResources.ShadingRateFlag2HorizontalPixelsEXT
        << ":ShadingRateFlag4HorizontalPixelsEXT:" << mResources.ShadingRateFlag4HorizontalPixelsEXT
        << ":OES_standard_derivatives:" << mResources.OES_standard_derivatives
        << ":OES_EGL_image_external:" << mResources.OES_EGL_image_external
        << ":OES_EGL_image_external_essl3:" << mResources.OES_EGL_image_external_essl3
        << ":NV_EGL_stream_consumer_external:" << mResources.NV_EGL_stream_consumer_external
        << ":ARB_texture_rectangle:" << mResources.ARB_texture_rectangle
        << ":EXT_draw_buffers:" << mResources.EXT_draw_buffers
        << ":MaxExpressionComplexity:" << mResources.MaxExpressionComplexity
        << ":MaxStatementDepth:" << mResources.MaxStatementDepth
        << ":MaxCallStackDepth:" << mResources.MaxCallStackDepth
        << ":MaxFunctionParameters:" << mResources.MaxFunctionParameters
        << ":EXT_blend_func_extended:" << mResources.EXT_blend_func_extended
        << ":EXT_conservative_depth:" << mResources.EXT_conservative_depth
        << ":EXT_frag_depth:" << mResources.EXT_frag_depth
        << ":EXT_primitive_bounding_box:" << mResources.EXT_primitive_bounding_box
        << ":OES_primitive_bounding_box:" << mResources.OES_primitive_bounding_box
        << ":EXT_separate_shader_objects:" << mResources.EXT_separate_shader_objects
        << ":EXT_shader_texture_lod:" << mResources.EXT_shader_texture_lod
        << ":EXT_shader_framebuffer_fetch:" << mResources.EXT_shader_framebuffer_fetch
        << ":EXT_shader_framebuffer_fetch_non_coherent:" << mResources.EXT_shader_framebuffer_fetch_non_coherent
        << ":ARM_shader_framebuffer_fetch:" << mResources.ARM_shader_framebuffer_fetch
        << ":ARM_shader_framebuffer_fetch_depth_stencil:" << mResources.ARM_shader_framebuffer_fetch_depth_stencil
        << ":OVR_multiview2:" << mResources.OVR_multiview2
        << ":OVR_multiview:" << mResources.OVR_multiview
        << ":EXT_YUV_target:" << mResources.EXT_YUV_target
        << ":EXT_geometry_shader:" << mResources.EXT_geometry_shader
        << ":OES_geometry_shader:" << mResources.OES_geometry_shader
        << ":OES_shader_io_blocks:" << mResources.OES_shader_io_blocks
        << ":EXT_shader_io_blocks:" << mResources.EXT_shader_io_blocks
        << ":EXT_gpu_shader5:" << mResources.EXT_gpu_shader5
        << ":OES_texture_3D:" << mResources.OES_texture_3D
        << ":MaxVertexOutputVectors:" << mResources.MaxVertexOutputVectors
        << ":MaxFragmentInputVectors:" << mResources.MaxFragmentInputVectors
        << ":MinProgramTexelOffset:" << mResources.MinProgramTexelOffset
        << ":MaxProgramTexelOffset:" << mResources.MaxProgramTexelOffset
        << ":MaxFragmentUniformBlocks:" << mResources.MaxFragmentUniformBlocks
        << ":MaxVertexUniformBlocks:" << mResources.MaxVertexUniformBlocks
        << ":MaxDualSourceDrawBuffers:" << mResources.MaxDualSourceDrawBuffers
        << ":MaxViewsOVR:" << mResources.MaxViewsOVR
        << ":NV_draw_buffers:" << mResources.NV_draw_buffers
        << ":ANGLE_multi_draw:" << mResources.ANGLE_multi_draw
        << ":ANGLE_base_vertex_base_instance_shader_builtin:" << mResources.ANGLE_base_vertex_base_instance_shader_builtin
        << ":APPLE_clip_distance:" << mResources.APPLE_clip_distance
        << ":OES_texture_cube_map_array:" << mResources.OES_texture_cube_map_array
        << ":EXT_texture_cube_map_array:" << mResources.EXT_texture_cube_map_array
        << ":EXT_texture_query_lod:" << mResources.EXT_texture_query_lod
        << ":EXT_texture_shadow_lod:" << mResources.EXT_texture_shadow_lod
        << ":EXT_shadow_samplers:" << mResources.EXT_shadow_samplers
        << ":OES_shader_multisample_interpolation:" << mResources.OES_shader_multisample_interpolation
        << ":OES_shader_image_atomic:" << mResources.OES_shader_image_atomic
        << ":EXT_tessellation_shader:" << mResources.EXT_tessellation_shader
        << ":OES_tessellation_shader:" << mResources.OES_tessellation_shader
        << ":OES_texture_buffer:" << mResources.OES_texture_buffer
        << ":EXT_texture_buffer:" << mResources.EXT_texture_buffer
        << ":EXT_fragment_shading_rate:" << mResources.EXT_fragment_shading_rate
        << ":EXT_fragment_shading_rate_primitive:" << mResources.EXT_fragment_shading_rate_primitive
        << ":OES_sample_variables:" << mResources.OES_sample_variables
        << ":EXT_clip_cull_distance:" << mResources.EXT_clip_cull_distance
        << ":ANGLE_clip_cull_distance:" << mResources.ANGLE_clip_cull_distance
        << ":MinProgramTextureGatherOffset:" << mResources.MinProgramTextureGatherOffset
        << ":MaxProgramTextureGatherOffset:" << mResources.MaxProgramTextureGatherOffset
        << ":MaxImageUnits:" << mResources.MaxImageUnits
        << ":MaxSamples:" << mResources.MaxSamples
        << ":MaxVertexImageUniforms:" << mResources.MaxVertexImageUniforms
        << ":MaxFragmentImageUniforms:" << mResources.MaxFragmentImageUniforms
        << ":MaxComputeImageUniforms:" << mResources.MaxComputeImageUniforms
        << ":MaxCombinedImageUniforms:" << mResources.MaxCombinedImageUniforms
        << ":MaxVariableSizeInBytes:" << mResources.MaxVariableSizeInBytes
        << ":MaxPrivateVariableSizeInBytes:" << mResources.MaxPrivateVariableSizeInBytes
        << ":MaxTotalPrivateVariableSizeInBytes:" << mResources.MaxTotalPrivateVariableSizeInBytes
        << ":MaxCombinedShaderOutputResources:" << mResources.MaxCombinedShaderOutputResources
        << ":MaxComputeWorkGroupCountX:" << mResources.MaxComputeWorkGroupCount[0]
        << ":MaxComputeWorkGroupCountY:" << mResources.MaxComputeWorkGroupCount[1]
        << ":MaxComputeWorkGroupCountZ:" << mResources.MaxComputeWorkGroupCount[2]
        << ":MaxComputeWorkGroupSizeX:" << mResources.MaxComputeWorkGroupSize[0]
        << ":MaxComputeWorkGroupSizeY:" << mResources.MaxComputeWorkGroupSize[1]
        << ":MaxComputeWorkGroupSizeZ:" << mResources.MaxComputeWorkGroupSize[2]
        << ":MaxComputeUniformComponents:" << mResources.MaxComputeUniformComponents
        << ":MaxComputeTextureImageUnits:" << mResources.MaxComputeTextureImageUnits
        << ":MaxComputeAtomicCounters:" << mResources.MaxComputeAtomicCounters
        << ":MaxComputeAtomicCounterBuffers:" << mResources.MaxComputeAtomicCounterBuffers
        << ":MaxVertexAtomicCounters:" << mResources.MaxVertexAtomicCounters
        << ":MaxFragmentAtomicCounters:" << mResources.MaxFragmentAtomicCounters
        << ":MaxCombinedAtomicCounters:" << mResources.MaxCombinedAtomicCounters
        << ":MaxAtomicCounterBindings:" << mResources.MaxAtomicCounterBindings
        << ":MaxVertexAtomicCounterBuffers:" << mResources.MaxVertexAtomicCounterBuffers
        << ":MaxFragmentAtomicCounterBuffers:" << mResources.MaxFragmentAtomicCounterBuffers
        << ":MaxCombinedAtomicCounterBuffers:" << mResources.MaxCombinedAtomicCounterBuffers
        << ":MaxAtomicCounterBufferSize:" << mResources.MaxAtomicCounterBufferSize
        << ":MaxComputeUnformBlocks:" << mResources.MaxComputeUniformBlocks
        << ":MaxGeometryUniformComponents:" << mResources.MaxGeometryUniformComponents
        << ":MaxGeometryInputComponents:" << mResources.MaxGeometryInputComponents
        << ":MaxGeometryOutputComponents:" << mResources.MaxGeometryOutputComponents
        << ":MaxGeometryOutputVertices:" << mResources.MaxGeometryOutputVertices
        << ":MaxGeometryTotalOutputComponents:" << mResources.MaxGeometryTotalOutputComponents
        << ":MaxGeometryTextureImageUnits:" << mResources.MaxGeometryTextureImageUnits
        << ":MaxGeometryAtomicCounterBuffers:" << mResources.MaxGeometryAtomicCounterBuffers
        << ":MaxGeometryAtomicCounters:" << mResources.MaxGeometryAtomicCounters
        << ":MaxGeometryShaderInvocations:" << mResources.MaxGeometryShaderInvocations
        << ":MaxGeometryImageUniforms:" << mResources.MaxGeometryImageUniforms
        << ":MaxGeometryUniformBlocks:" << mResources.MaxGeometryUniformBlocks
        << ":MaxClipDistances" << mResources.MaxClipDistances
        << ":MaxCullDistances" << mResources.MaxCullDistances
        << ":MaxCombinedClipAndCullDistances" << mResources.MaxCombinedClipAndCullDistances
        << ":MaxTessControlInputComponents:" << mResources.MaxTessControlInputComponents
        << ":MaxTessControlOutputComponents:" << mResources.MaxTessControlOutputComponents
        << ":MaxTessControlTextureImageUnits:" << mResources.MaxTessControlTextureImageUnits
        << ":MaxTessControlUniformComponents:" << mResources.MaxTessControlUniformComponents
        << ":MaxTessControlTotalOutputComponents:" << mResources.MaxTessControlTotalOutputComponents
        << ":MaxTessControlImageUniforms:" << mResources.MaxTessControlImageUniforms
        << ":MaxTessControlAtomicCounters:" << mResources.MaxTessControlAtomicCounters
        << ":MaxTessControlAtomicCounterBuffers:" << mResources.MaxTessControlAtomicCounterBuffers
        << ":MaxTessControlUniformBlocks:" << mResources.MaxTessControlUniformBlocks
        << ":MaxTessPatchComponents:" << mResources.MaxTessPatchComponents
        << ":MaxPatchVertices:" << mResources.MaxPatchVertices
        << ":MaxTessGenLevel:" << mResources.MaxTessGenLevel
        << ":MaxTessEvaluationInputComponents:" << mResources.MaxTessEvaluationInputComponents
        << ":MaxTessEvaluationOutputComponents:" << mResources.MaxTessEvaluationOutputComponents
        << ":MaxTessEvaluationTextureImageUnits:" << mResources.MaxTessEvaluationTextureImageUnits
        << ":MaxTessEvaluationUniformComponents:" << mResources.MaxTessEvaluationUniformComponents
        << ":MaxTessEvaluationImageUniforms:" << mResources.MaxTessEvaluationImageUniforms
        << ":MaxTessEvaluationAtomicCounters:" << mResources.MaxTessEvaluationAtomicCounters
        << ":MaxTessEvaluationAtomicCounterBuffers:" << mResources.MaxTessEvaluationAtomicCounterBuffers
        << ":MaxTessControlUniformBlocks:" << mResources.MaxTessControlUniformBlocks
    ;
    // clang-format on

    mBuiltInResourcesString = strstream.str();
}

void TCompiler::collectVariables(
    TIntermBlock *root,
    const SamplersStaticallyUsedWithTexelFetch &samplersStaticallyUsedWithTexelFetch)
{
    // Variable collection is done from the IR already.
    ASSERT(!mCompileOptions.useIR);
    ASSERT(!mVariablesCollected);
    CollectVariables(root, &mAttributes, &mOutputVariables, &mUniforms, &mInputVaryings,
                     &mOutputVaryings, &mSharedVariables, &mUniformBlocks, &mShaderStorageBlocks,
                     mResources.HashFunction, &mNameMap, &mSymbolTable, mShaderType,
                     mExtensionBehavior, mCompileOptions.transformFloatUniformTo16Bits,
                     samplersStaticallyUsedWithTexelFetch);
    collectInterfaceBlocks();
    mVariablesCollected = true;
}

void TCompiler::collectInterfaceBlocks()
{
    ASSERT(mInterfaceBlocks.empty());
    mInterfaceBlocks.reserve(mUniformBlocks.size() + mShaderStorageBlocks.size());
    mInterfaceBlocks.insert(mInterfaceBlocks.end(), mUniformBlocks.begin(), mUniformBlocks.end());
    mInterfaceBlocks.insert(mInterfaceBlocks.end(), mShaderStorageBlocks.begin(),
                            mShaderStorageBlocks.end());
}

void TCompiler::clearResults()
{
    mInfoSink.info.erase();
    mInfoSink.obj.erase();
    mInfoSink.debug.erase();
    mDiagnostics.resetErrorCount();

    mMetadataFlags.reset();

    mAttributes.clear();
    mOutputVariables.clear();
    mUniforms.clear();
    mInputVaryings.clear();
    mOutputVaryings.clear();
    mSharedVariables.clear();
    mInterfaceBlocks.clear();
    mUniformBlocks.clear();
    mShaderStorageBlocks.clear();
    mVariablesCollected    = false;
    mGLPositionInitialized = false;

    mNumViews = -1;

    mClipDistanceSize = 0;
    mCullDistanceSize = 0;

    mGeometryShaderInputPrimitiveType  = EptUndefined;
    mGeometryShaderOutputPrimitiveType = EptUndefined;
    mGeometryShaderInvocations         = 0;
    mGeometryShaderMaxVertices         = -1;

    mTessControlShaderOutputVertices            = 0;
    mTessEvaluationShaderInputPrimitiveType     = EtetUndefined;
    mTessEvaluationShaderInputVertexSpacingType = EtetUndefined;
    mTessEvaluationShaderInputOrderingType      = EtetUndefined;
    mTessEvaluationShaderInputPointType         = EtetUndefined;

    mNameMap.clear();

    mSourcePath = nullptr;

    mSymbolTable.clearCompilationResults();
}

void TCompiler::initCallDag(TIntermNode *root)
{
    mCallDag.clear();
    mCallDag.init(root);
}

void TCompiler::tagUsedFunctions()
{
    // Search from main, starting from the end of the DAG as it's usually found at the end of the
    // shader.
    for (size_t i = mCallDag.size(); i-- > 0;)
    {
        if (mCallDag.getRecordFromIndex(i).node->getFunction()->isMain())
        {
            internalTagUsedFunction(i);
            break;
        }
    }
}

void TCompiler::internalTagUsedFunction(size_t index)
{
    if (mFunctionMetadata[index].used)
    {
        return;
    }

    mFunctionMetadata[index].used = true;

    for (int calleeIndex : mCallDag.getRecordFromIndex(index).callees)
    {
        internalTagUsedFunction(calleeIndex);
    }
}

bool TCompiler::sortUniforms(TIntermBlock *root)
{
    // First: Separate sequences into three chunks
    TIntermSequence structTypeDeclarationSequence;
    TIntermSequence uniformDeclarationSequence;
    TIntermSequence remainingSequence;

    TIntermSequence *sequence = root->getSequence();
    size_t nodeIndex          = 0;
    while (nodeIndex < sequence->size())
    {
        TIntermNode *node = sequence->at(nodeIndex);
        if (IsCurrentNodeStructTypeDeclaration(node))
        {
            structTypeDeclarationSequence.push_back(node);
        }
        else if (IsCurrentNodeUniformDeclaration(node))
        {
            uniformDeclarationSequence.push_back(node);
        }
        else
        {
            remainingSequence.push_back(node);
        }
        ++nodeIndex;
    }

    // Second: Sort uniforms based on their precisions and data types
    std::stable_sort(uniformDeclarationSequence.begin(), uniformDeclarationSequence.end(),
                     UniformSortComparator());

    // Third: merge three chunks
    TIntermSequence reorderedSequence;
    reorderedSequence.reserve(structTypeDeclarationSequence.size() +
                              uniformDeclarationSequence.size() + remainingSequence.size());
    std::move(structTypeDeclarationSequence.begin(), structTypeDeclarationSequence.end(),
              std::back_inserter(reorderedSequence));
    std::move(uniformDeclarationSequence.begin(), uniformDeclarationSequence.end(),
              std::back_inserter(reorderedSequence));
    std::move(remainingSequence.begin(), remainingSequence.end(),
              std::back_inserter(reorderedSequence));

    root->replaceAllChildren(std::move(reorderedSequence));
    return validateAST(root);
}

bool TCompiler::pruneUnusedFunctions(TIntermBlock *root)
{
    TIntermSequence *sequence = root->getSequence();

    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < sequence->size(); ++readIndex)
    {
        TIntermNode *node = sequence->at(readIndex);

        // Keep anything that's not unused.
        const TFunction *function = nullptr;
        const bool shouldPrune =
            IsTopLevelNodeUnusedFunction(mCallDag, mFunctionMetadata, node, &function);
        if (!shouldPrune)
        {
            (*sequence)[writeIndex++] = node;
            continue;
        }

        // If a function is unused, it may have a struct declaration in its return value which
        // shouldn't be pruned.  In that case, replace the function definition with the struct
        // definition.
        ASSERT(function != nullptr);
        const TType &returnType = function->getReturnType();
        if (!returnType.isStructSpecifier())
        {
            continue;
        }

        TVariable *structVariable =
            new TVariable(&mSymbolTable, kEmptyImmutableString, &returnType, SymbolType::Empty);
        TIntermSymbol *structSymbol           = new TIntermSymbol(structVariable);
        TIntermDeclaration *structDeclaration = new TIntermDeclaration;
        structDeclaration->appendDeclarator(structSymbol);

        structSymbol->setLine(node->getLine());
        structDeclaration->setLine(node->getLine());

        (*sequence)[writeIndex++] = structDeclaration;
    }

    sequence->resize(writeIndex);

    return validateAST(root);
}

bool TCompiler::limitExpressionComplexity(TIntermBlock *root)
{
    if (!IsASTDepthBelowLimit(root, mResources.MaxExpressionComplexity))
    {
        mDiagnostics.globalError("Expression too complex.");
        return false;
    }

    return true;
}

bool TCompiler::initializeGLPosition(TIntermBlock *root)
{
    InitVariableList list;
    AddBuiltInToInitList(&mSymbolTable, mShaderVersion, root, "gl_Position", &list);

    if (!list.empty())
    {
        return InitializeVariables(this, root, list, &mSymbolTable, mShaderVersion,
                                   mExtensionBehavior, false);
    }

    return true;
}

bool TCompiler::useAllMembersInUnusedStandardAndSharedBlocks(TIntermBlock *root)
{
    sh::InterfaceBlockList list;

    for (const sh::InterfaceBlock &block : mUniformBlocks)
    {
        if (!block.staticUse &&
            (block.layout == sh::BLOCKLAYOUT_STD140 || block.layout == sh::BLOCKLAYOUT_SHARED))
        {
            list.push_back(block);
        }
    }

    return sh::UseInterfaceBlockFields(this, root, list, mSymbolTable);
}

bool TCompiler::initializeOutputVariables(TIntermBlock *root)
{
    // Place `main` at the end of the shader if not already.  If a variable is declared after main,
    // main cannot reference it.
    {
        const TIntermSequence *original = root->getSequence();
        TIntermSequence reordered;
        TIntermNode *main = nullptr;

        for (TIntermNode *node : *original)
        {
            TIntermFunctionDefinition *function = node->getAsFunctionDefinition();
            if (function != nullptr && function->getFunction()->isMain())
            {
                ASSERT(main == nullptr);
                main = node;
            }
            else
            {
                reordered.push_back(node);
            }
        }
        ASSERT(main != nullptr);
        reordered.push_back(main);

        root->replaceAllChildren(std::move(reordered));
    }

    InitVariableList list;

    for (TIntermNode *node : *root->getSequence())
    {
        TIntermDeclaration *asDecl = node->getAsDeclarationNode();
        if (asDecl == nullptr)
        {
            continue;
        }

        TIntermSymbol *symbol = asDecl->getSequence()->front()->getAsSymbolNode();
        if (symbol == nullptr)
        {
            TIntermBinary *initNode = asDecl->getSequence()->front()->getAsBinaryNode();
            ASSERT(initNode->getOp() == EOpInitialize);
            symbol = initNode->getLeft()->getAsSymbolNode();
        }
        ASSERT(symbol);

        // inout variables represent the context of the framebuffer when the draw call starts, so
        // they have to be considered as already initialized.
        const TQualifier qualifier = symbol->getType().getQualifier();
        if (qualifier != EvqFragmentInOut && IsShaderOut(symbol->getType().getQualifier()))
        {
            list.push_back(&symbol->variable());
        }
    }

    // Initialize built-in outputs as well.
    const std::vector<ShaderVariable> &outputVariables =
        mShaderType == GL_FRAGMENT_SHADER ? mOutputVariables : mOutputVaryings;

    for (const ShaderVariable &var : outputVariables)
    {
        if (var.isFragmentInOut || !var.isBuiltIn())
        {
            continue;
        }

        AddBuiltInToInitList(&mSymbolTable, mShaderVersion, root, var.name.c_str(), &list);
        if (var.name == "gl_Position")
        {
            ASSERT(!mGLPositionInitialized);
            mGLPositionInitialized = true;
        }
    }

    return InitializeVariables(this, root, list, &mSymbolTable, mShaderVersion, mExtensionBehavior,
                               false);
}

const TExtensionBehavior &TCompiler::getExtensionBehavior() const
{
    return mExtensionBehavior;
}

const char *TCompiler::getSourcePath() const
{
    return mSourcePath;
}

const ShBuiltInResources &TCompiler::getResources() const
{
    return mResources;
}

bool TCompiler::isVaryingDefined(const char *varyingName)
{
    ASSERT(mVariablesCollected);
    for (const ShaderVariable &inputVarying : mInputVaryings)
    {
        if (inputVarying.name == varyingName)
        {
            return true;
        }
    }
    for (const ShaderVariable &outputVarying : mOutputVaryings)
    {
        if (outputVarying.name == varyingName)
        {
            return true;
        }
    }

    return false;
}

}  // namespace sh
