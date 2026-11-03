//
// Copyright 2020 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DriverUniform.cpp: Add code to support driver uniforms
//

#include "compiler/translator/tree_util/DriverUniform.h"

#include "compiler/translator/Compiler.h"
#include "compiler/translator/IntermNode.h"
#include "compiler/translator/StaticType.h"
#include "compiler/translator/SymbolTable.h"
#include "compiler/translator/tree_util/FindMain.h"
#include "compiler/translator/tree_util/IntermNode_util.h"
#include "compiler/translator/util.h"

namespace sh
{

namespace
{

constexpr const char kDepthRange[]       = "depthRange";
constexpr const char kRenderArea[]       = "renderArea";
constexpr const char kFlipXY[]           = "flipXY";
constexpr const char kMisc[]             = "misc";
constexpr const char kBaseInstance[]     = "baseInstance";
constexpr const char kAcbBufferOffsets[] = "acbBufferOffsets";
constexpr const char kTransformXY[]      = "transformXY";

// Extended uniforms
constexpr const char kXfbBufferOffsets[]       = "xfbBufferOffsets";
constexpr const char kXfbVerticesPerInstance[] = "xfbVerticesPerInstance";
constexpr const char kUnused[]                 = "unused";
constexpr const char kUnused2[]                = "unused2";

// MSL-specific variable name, which is different from kDriverUniformsVarName, because the
// assumptions about the naming conventions of ANGLE-internal variables run too deeply to rename
// this one.
constexpr const char kDriverUniformsVarNameMSL[] = "ANGLE_angleUniforms";
}  // anonymous namespace

// Class DriverUniform
void DriverUniform::addComputeDriverUniformsToShader(TIntermBlock *root, TSymbolTable *symbolTable)
{
    constexpr size_t kNumComputeDriverUniforms                                               = 1;
    constexpr std::array<const char *, kNumComputeDriverUniforms> kComputeDriverUniformNames = {
        {kAcbBufferOffsets}};

    ASSERT(!mDriverUniforms);
    // This field list mirrors the structure of ComputeDriverUniforms in ContextVk.cpp.
    TFieldList *driverFieldList = new TFieldList;

    const std::array<TType *, kNumComputeDriverUniforms> kDriverUniformTypes = {{
        new TType(EbtUInt, EbpHigh, EvqGlobal, 4),
    }};

    for (size_t uniformIndex = 0; uniformIndex < kNumComputeDriverUniforms; ++uniformIndex)
    {
        TField *driverUniformField =
            new TField(kDriverUniformTypes[uniformIndex],
                       ImmutableString(kComputeDriverUniformNames[uniformIndex]), TSourceLoc(),
                       SymbolType::AngleInternal);
        driverFieldList->push_back(driverUniformField);
    }

    // Define a driver uniform block "ANGLEUniformBlock" with instance name "ANGLEUniforms".
    TLayoutQualifier layoutQualifier = TLayoutQualifier::Create();
    layoutQualifier.blockStorage     = EbsStd140;
    layoutQualifier.pushConstant     = true;

    const TInterfaceBlock *interfaceBlock = DeclareInterfaceBlock(
        symbolTable, driverFieldList, layoutQualifier, kDriverUniformsBlockName);
    mDriverUniforms = DeclareInterfaceBlockVariable(root, symbolTable, EvqUniform, interfaceBlock,
                                                    layoutQualifier, TMemoryQualifier::Create(), 0,
                                                    kDriverUniformsVarName);
}

TFieldList *DriverUniform::createUniformFields(TSymbolTable *symbolTable)
{
    constexpr size_t kNumGraphicsDriverUniforms                                                = 6;
    constexpr std::array<const char *, kNumGraphicsDriverUniforms> kGraphicsDriverUniformNames = {{
        kDepthRange,
        kRenderArea,
        kFlipXY,
        kMisc,
        kBaseInstance,
        kAcbBufferOffsets,
    }};

    // This field list mirrors the structure of GraphicsDriverUniforms in ContextVk.cpp.
    TFieldList *driverFieldList = new TFieldList;

    const std::array<TType *, kNumGraphicsDriverUniforms> kDriverUniformTypes = {{
        // depthRange: Near and far depth
        new TType(EbtFloat, EbpHigh, EvqGlobal, 2),
        // renderArea: Packed ushort2
        new TType(EbtUInt, EbpHigh, EvqGlobal),
        // flipXY: Packed snorm4
        new TType(EbtUInt, EbpHigh, EvqGlobal),
        // misc: Various bits of state
        new TType(EbtUInt, EbpHigh, EvqGlobal),
        // baseInstance: int
        new TType(EbtInt, EbpHigh, EvqGlobal),
        // acbBufferOffsets: Packed ubyte8
        new TType(EbtUInt, EbpHigh, EvqGlobal, 2),
    }};

    for (size_t uniformIndex = 0; uniformIndex < kNumGraphicsDriverUniforms; ++uniformIndex)
    {
        TField *driverUniformField =
            new TField(kDriverUniformTypes[uniformIndex],
                       ImmutableString(kGraphicsDriverUniformNames[uniformIndex]), TSourceLoc(),
                       SymbolType::AngleInternal);
        driverFieldList->push_back(driverUniformField);
    }

    // transformXY is only used by the Vulkan (SPIR-V) backend, so it's not added to the driver
    // uniforms for other backends.
    if (mOutputType == SH_SPIRV_VULKAN_OUTPUT)
    {
        TField *transformXYField =
            new TField(new TType(EbtFloat, EbpHigh, EvqGlobal, 4), ImmutableString(kTransformXY),
                       TSourceLoc(), SymbolType::AngleInternal);
        driverFieldList->push_back(transformXYField);
    }

    return driverFieldList;
}

const TType *DriverUniform::createEmulatedDepthRangeType(TSymbolTable *symbolTable)
{
    // If already defined, return it immediately.
    if (mEmulatedDepthRangeType != nullptr)
    {
        return mEmulatedDepthRangeType;
    }

    // Create the depth range type.
    TFieldList *depthRangeParamsFields = new TFieldList();
    TType *floatType                   = new TType(EbtFloat, EbpHigh, EvqGlobal, 1, 1);
    depthRangeParamsFields->push_back(
        new TField(floatType, ImmutableString("near"), TSourceLoc(), SymbolType::AngleInternal));
    depthRangeParamsFields->push_back(
        new TField(floatType, ImmutableString("far"), TSourceLoc(), SymbolType::AngleInternal));
    depthRangeParamsFields->push_back(
        new TField(floatType, ImmutableString("diff"), TSourceLoc(), SymbolType::AngleInternal));

    TStructure *emulatedDepthRangeParams = new TStructure(
        symbolTable, kEmulatedDepthRangeParams, depthRangeParamsFields, SymbolType::AngleInternal);

    mEmulatedDepthRangeType = new TType(emulatedDepthRangeParams, false);

    return mEmulatedDepthRangeType;
}

// The Add*DriverUniformsToShader operation adds an internal uniform block to a shader. The driver
// block is used to implement Vulkan-specific features and workarounds. Returns the driver uniforms
// variable.
//
// There are Graphics and Compute variations as they require different uniforms.
void DriverUniform::addGraphicsDriverUniformsToShader(TIntermBlock *root, TSymbolTable *symbolTable)
{
    ASSERT(!mDriverUniforms);

    // Declare the depth range struct type.
    const TType *emulatedDepthRangeType     = createEmulatedDepthRangeType(symbolTable);
    const TType *emulatedDepthRangeDeclType = new TType(emulatedDepthRangeType->getStruct(), true);

    const TVariable *depthRangeVar =
        new TVariable(symbolTable->nextUniqueId(), kEmptyImmutableString, SymbolType::Empty,
                      TExtension::UNDEFINED, emulatedDepthRangeDeclType);

    DeclareGlobalVariable(root, depthRangeVar);

    TFieldList *driverFieldList = createUniformFields(symbolTable);
    if (mMode == DriverUniformMode::InterfaceBlock)
    {
        // Define a driver uniform block "ANGLEUniformBlock" with instance name "ANGLEUniforms".
        TLayoutQualifier layoutQualifier = TLayoutQualifier::Create();
        layoutQualifier.blockStorage     = EbsStd140;
        layoutQualifier.pushConstant     = true;

        const TInterfaceBlock *interfaceBlock = DeclareInterfaceBlock(
            symbolTable, driverFieldList, layoutQualifier, kDriverUniformsBlockName);
        mDriverUniforms = DeclareInterfaceBlockVariable(
            root, symbolTable, EvqUniform, interfaceBlock, layoutQualifier,
            TMemoryQualifier::Create(), 0, kDriverUniformsVarName);
    }
    else
    {
        // Declare a structure "ANGLEUniformBlock" with instance name "ANGLE_angleUniforms".
        const ImmutableString varName = ImmutableString(kDriverUniformsVarNameMSL);
        auto result =
            DeclareStructure(root, symbolTable, driverFieldList, EvqUniform,
                             TMemoryQualifier::Create(), 0, kDriverUniformsBlockName, &varName);
        mDriverUniforms = result.second;
    }
}

void DriverUniform::findDeclarationAddedByIR(TIntermBlock *root)
{
    TIntermSequence *original = root->getSequence();

    for (TIntermNode *node : *original)
    {
        TIntermDeclaration *decl = node->getAsDeclarationNode();
        if (decl == nullptr || decl->getSequence()->size() != 1)
        {
            continue;
        }

        TIntermSymbol *symbol = decl->getSequence()->front()->getAsSymbolNode();
        if (symbol == nullptr)
        {
            continue;
        }

        const TVariable &variable = symbol->variable();
        const TType &type         = variable.getType();
        if (type.getInterfaceBlock() == nullptr && type.getStruct() == nullptr)
        {
            continue;
        }

        const TSymbol *typeSymbol = type.getInterfaceBlock() != nullptr
                                        ? static_cast<const TSymbol *>(type.getInterfaceBlock())
                                        : type.getStruct();

        // The gl_DepthRangeParams struct is emulated with a struct by the name of
        // kEmulatedDepthRangeParams.  This struct is only present in graphics shader.
        if (typeSymbol->symbolType() == SymbolType::AngleInternal &&
            typeSymbol->name() == kEmulatedDepthRangeParams)
        {
            ASSERT(mEmulatedDepthRangeType == nullptr);
            mEmulatedDepthRangeType = new TType(type.getStruct(), false);
            continue;
        }

        // The driver uniform declaration is normally an interface block, but could be a struct for
        // MSL.  Either way, the name of the _type_ is the same and it's kDriverUniformsBlockName.
        if (variable.symbolType() != SymbolType::Empty &&
            typeSymbol->symbolType() == SymbolType::AngleInternal &&
            typeSymbol->name() == kDriverUniformsBlockName)
        {
            ASSERT(mDriverUniforms == nullptr);
            ASSERT((mMode == DriverUniformMode::InterfaceBlock) ==
                   (type.getInterfaceBlock() != nullptr));
            mDriverUniforms = &variable;
        }
    }

    ASSERT(mDriverUniforms != nullptr);
}

TIntermTyped *DriverUniform::createDriverUniformRef(const char *fieldName) const
{
    ASSERT(mDriverUniforms);

    size_t fieldIndex = 0;
    if (mMode == DriverUniformMode::InterfaceBlock)
    {
        fieldIndex =
            FindFieldIndex(mDriverUniforms->getType().getInterfaceBlock()->fields(), fieldName);
    }
    else
    {
        fieldIndex = FindFieldIndex(mDriverUniforms->getType().getStruct()->fields(), fieldName);
    }

    TIntermSymbol *angleUniformsRef = new TIntermSymbol(mDriverUniforms);
    TConstantUnion *uniformIndex    = new TConstantUnion;
    uniformIndex->setIConst(static_cast<int>(fieldIndex));
    TIntermConstantUnion *indexRef =
        new TIntermConstantUnion(uniformIndex, *StaticType::GetBasic<EbtInt, EbpLow>());
    if (mMode == DriverUniformMode::InterfaceBlock)
    {
        return new TIntermBinary(EOpIndexDirectInterfaceBlock, angleUniformsRef, indexRef);
    }
    return new TIntermBinary(EOpIndexDirectStruct, angleUniformsRef, indexRef);
}

TIntermTyped *DriverUniform::getAcbBufferOffsets() const
{
    return createDriverUniformRef(kAcbBufferOffsets);
}

TIntermTyped *DriverUniform::getDepthRange() const
{
    ASSERT(mEmulatedDepthRangeType != nullptr);

    TIntermTyped *depthRangeRef = createDriverUniformRef(kDepthRange);
    TIntermTyped *nearRef       = new TIntermSwizzle(depthRangeRef, {0});
    TIntermTyped *farRef        = new TIntermSwizzle(depthRangeRef->deepCopy(), {1});
    TIntermTyped *diff          = new TIntermBinary(EOpSub, farRef, nearRef);

    TIntermSequence args = {
        nearRef->deepCopy(),
        farRef->deepCopy(),
        diff,
    };

    return TIntermAggregate::CreateConstructor(*mEmulatedDepthRangeType, &args);
}

TIntermTyped *DriverUniform::getViewportZScale() const
{
    ASSERT(mEmulatedDepthRangeType != nullptr);

    TIntermTyped *depthRangeRef = createDriverUniformRef(kDepthRange);
    TIntermTyped *nearRef       = new TIntermSwizzle(depthRangeRef, {0});
    TIntermTyped *farRef        = new TIntermSwizzle(depthRangeRef->deepCopy(), {1});

    TIntermTyped *isNegative = new TIntermBinary(EOpLessThan, farRef, nearRef);

    return new TIntermTernary(isNegative, CreateFloatNode(-1, EbpMedium),
                              CreateFloatNode(1, EbpMedium));
}

TIntermTyped *DriverUniform::getHalfRenderArea() const
{
    TIntermTyped *renderAreaRef = createDriverUniformRef(kRenderArea);
    TIntermTyped *width = new TIntermBinary(EOpBitwiseAnd, renderAreaRef, CreateUIntNode(0xFFFF));
    TIntermTyped *height =
        new TIntermBinary(EOpBitShiftRight, renderAreaRef->deepCopy(), CreateUIntNode(16));

    TIntermSequence widthArgs = {
        width,
    };
    TIntermTyped *widthAsFloat =
        TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtFloat, EbpHigh>(), &widthArgs);

    TIntermSequence heightArgs = {
        height,
    };
    TIntermTyped *heightAsFloat = TIntermAggregate::CreateConstructor(
        *StaticType::GetBasic<EbtFloat, EbpHigh>(), &heightArgs);

    TIntermSequence args = {
        widthAsFloat,
        heightAsFloat,
    };

    TIntermTyped *renderArea =
        TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtFloat, EbpHigh, 2>(), &args);
    return new TIntermBinary(EOpVectorTimesScalar, renderArea, CreateFloatNode(0.5, EbpMedium));
}

TIntermTyped *DriverUniform::getFlipXY(TSymbolTable *symbolTable, DriverUniformFlip stage) const
{
    TIntermTyped *flipXY = createDriverUniformRef(kFlipXY);
    TIntermTyped *values =
        CreateBuiltInUnaryFunctionCallNode("unpackSnorm4x8", flipXY, *symbolTable, 310);

    if (stage == DriverUniformFlip::Fragment)
    {
        return new TIntermSwizzle(values, {0, 1});
    }

    return new TIntermSwizzle(values, {2, 3});
}

TIntermTyped *DriverUniform::getNegFlipXY(TSymbolTable *symbolTable, DriverUniformFlip stage) const
{
    TIntermTyped *flipXY = getFlipXY(symbolTable, stage);

    constexpr std::array<float, 2> kMultiplier = {1, -1};
    return new TIntermBinary(EOpMul, flipXY, CreateVecNode(kMultiplier.data(), 2, EbpLow));
}

TIntermTyped *DriverUniform::getSwapXY() const
{
    TIntermTyped *miscRef = createDriverUniformRef(kMisc);
    TIntermTyped *swapXY  = new TIntermBinary(EOpBitwiseAnd, miscRef,
                                              CreateUIntNode(vk::kDriverUniformsMiscSwapXYMask));

    TIntermSequence args = {
        swapXY,
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtBool, EbpUndefined>(),
                                               &args);
}

TIntermTyped *DriverUniform::getBaseInstance() const
{
    return createDriverUniformRef(kBaseInstance);
}

TIntermTyped *DriverUniform::getAdvancedBlendEquation() const
{
    TIntermTyped *miscRef = createDriverUniformRef(kMisc);
    TIntermTyped *equation =
        new TIntermBinary(EOpBitShiftRight, miscRef,
                          CreateUIntNode(vk::kDriverUniformsMiscAdvancedBlendEquationOffset));
    equation = new TIntermBinary(EOpBitwiseAnd, equation,
                                 CreateUIntNode(vk::kDriverUniformsMiscAdvancedBlendEquationMask));

    return equation;
}

TIntermTyped *DriverUniform::getNumSamples() const
{
    TIntermTyped *miscRef     = createDriverUniformRef(kMisc);
    TIntermTyped *sampleCount = new TIntermBinary(
        EOpBitShiftRight, miscRef, CreateUIntNode(vk::kDriverUniformsMiscSampleCountOffset));
    sampleCount = new TIntermBinary(EOpBitwiseAnd, sampleCount,
                                    CreateUIntNode(vk::kDriverUniformsMiscSampleCountMask));

    return sampleCount;
}

TIntermTyped *DriverUniform::getClipDistancesEnabled() const
{
    TIntermTyped *miscRef     = createDriverUniformRef(kMisc);
    TIntermTyped *enabledMask = new TIntermBinary(
        EOpBitShiftRight, miscRef, CreateUIntNode(vk::kDriverUniformsMiscEnabledClipPlanesOffset));
    enabledMask = new TIntermBinary(EOpBitwiseAnd, enabledMask,
                                    CreateUIntNode(vk::kDriverUniformsMiscEnabledClipPlanesMask));

    return enabledMask;
}

TIntermTyped *DriverUniform::getTransformDepth() const
{
    TIntermTyped *miscRef        = createDriverUniformRef(kMisc);
    TIntermTyped *transformDepth = new TIntermBinary(
        EOpBitShiftRight, miscRef, CreateUIntNode(vk::kDriverUniformsMiscTransformDepthOffset));
    transformDepth = new TIntermBinary(EOpBitwiseAnd, transformDepth,
                                       CreateUIntNode(vk::kDriverUniformsMiscTransformDepthMask));

    TIntermSequence args = {
        transformDepth,
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtBool, EbpUndefined>(),
                                               &args);
}

TIntermTyped *DriverUniform::getAlphaToCoverage() const
{
    TIntermTyped *miscRef         = createDriverUniformRef(kMisc);
    TIntermTyped *alphaToCoverage = new TIntermBinary(
        EOpBitShiftRight, miscRef, CreateUIntNode(vk::kDriverUniformsMiscAlphaToCoverageOffset));
    alphaToCoverage = new TIntermBinary(EOpBitwiseAnd, alphaToCoverage,
                                        CreateUIntNode(vk::kDriverUniformsMiscAlphaToCoverageMask));

    TIntermSequence args = {
        alphaToCoverage,
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtBool, EbpUndefined>(),
                                               &args);
}

TIntermTyped *DriverUniform::getLayeredFramebuffer() const
{
    TIntermTyped *miscRef            = createDriverUniformRef(kMisc);
    TIntermTyped *layeredFramebuffer = new TIntermBinary(
        EOpBitShiftRight, miscRef, CreateUIntNode(vk::kDriverUniformsMiscLayeredFramebufferOffset));
    layeredFramebuffer =
        new TIntermBinary(EOpBitwiseAnd, layeredFramebuffer,
                          CreateUIntNode(vk::kDriverUniformsMiscLayeredFramebufferMask));

    TIntermSequence args = {
        layeredFramebuffer,
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtBool, EbpUndefined>(),
                                               &args);
}

TIntermTyped *DriverUniform::getTransformXY() const
{
    return createDriverUniformRef(kTransformXY);
}

//
// Class DriverUniformExtended
//
TFieldList *DriverUniformExtended::createUniformFields(TSymbolTable *symbolTable)
{
    TFieldList *driverFieldList = DriverUniform::createUniformFields(symbolTable);

    constexpr size_t kNumGraphicsDriverUniformsExt = 4;
    constexpr std::array<const char *, kNumGraphicsDriverUniformsExt>
        kGraphicsDriverUniformNamesExt = {
            {kXfbBufferOffsets, kXfbVerticesPerInstance, kUnused, kUnused2}};

    const std::array<TType *, kNumGraphicsDriverUniformsExt> kDriverUniformTypesExt = {{
        // xfbBufferOffsets: ivec4
        new TType(EbtInt, EbpHigh, EvqGlobal, 4),
        // xfbVerticesPerInstance: int
        new TType(EbtInt, EbpHigh, EvqGlobal),
        // unused: uvec3
        new TType(EbtUInt, EbpHigh, EvqGlobal),
        new TType(EbtUInt, EbpHigh, EvqGlobal, 2),
    }};

    for (size_t uniformIndex = 0; uniformIndex < kNumGraphicsDriverUniformsExt; ++uniformIndex)
    {
        TField *driverUniformField =
            new TField(kDriverUniformTypesExt[uniformIndex],
                       ImmutableString(kGraphicsDriverUniformNamesExt[uniformIndex]), TSourceLoc(),
                       SymbolType::AngleInternal);
        driverFieldList->push_back(driverUniformField);
    }

    return driverFieldList;
}

TIntermTyped *DriverUniformExtended::getXfbBufferOffsets() const
{
    return createDriverUniformRef(kXfbBufferOffsets);
}

TIntermTyped *DriverUniformExtended::getXfbVerticesPerInstance() const
{
    return createDriverUniformRef(kXfbVerticesPerInstance);
}

TIntermTyped *MakeSwapXMultiplier(TIntermTyped *swapped)
{
    // float(!swapped)
    TIntermSequence args = {
        new TIntermUnary(EOpLogicalNot, swapped, nullptr),
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtFloat, EbpLow>(), &args);
}

TIntermTyped *MakeSwapYMultiplier(TIntermTyped *swapped)
{
    // float(swapped)
    TIntermSequence args = {
        swapped,
    };
    return TIntermAggregate::CreateConstructor(*StaticType::GetBasic<EbtFloat, EbpLow>(), &args);
}
}  // namespace sh
