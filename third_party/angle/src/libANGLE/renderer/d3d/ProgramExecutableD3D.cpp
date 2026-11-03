//
// Copyright 2023 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// ProgramExecutableD3D.cpp: Implementation of ProgramExecutableD3D.
//

#include "libANGLE/renderer/d3d/ProgramExecutableD3D.h"
#include "common/unsafe_buffers.h"

#include "common/bitset_utils.h"
#include "common/span.h"
#include "common/string_utils.h"
#include "libANGLE/Framebuffer.h"
#include "libANGLE/FramebufferAttachment.h"
#include "libANGLE/renderer/d3d/FramebufferD3D.h"
#include "libANGLE/renderer/d3d/ShaderExecutableD3D.h"
#include "libANGLE/renderer/renderer_utils.h"
#include "libANGLE/trace.h"

namespace rx
{
namespace
{
void GetDefaultInputLayoutFromShader(const gl::SharedCompiledShaderState &vertexShader,
                                     gl::InputLayout *inputLayoutOut)
{
    inputLayoutOut->clear();

    if (!vertexShader)
    {
        return;
    }

    for (const sh::ShaderVariable &shaderAttr : vertexShader->activeAttributes)
    {
        if (shaderAttr.type != GL_NONE)
        {
            GLenum transposedType = gl::TransposeMatrixType(shaderAttr.type);

            for (size_t rowIndex = 0;
                 static_cast<int>(rowIndex) < gl::VariableRowCount(transposedType); ++rowIndex)
            {
                GLenum componentType = gl::VariableComponentType(transposedType);
                GLuint components    = static_cast<GLuint>(gl::VariableColumnCount(transposedType));
                bool pureInt         = (componentType != GL_FLOAT);

                gl::VertexAttribType attribType =
                    gl::FromGLenum<gl::VertexAttribType>(componentType);

                angle::FormatID defaultID =
                    gl::GetVertexFormatID(attribType, GL_FALSE, components, pureInt);

                inputLayoutOut->push_back(defaultID);
            }
        }
    }
}

size_t GetMaxOutputIndex(const std::vector<PixelShaderOutputVariable> &shaderOutputVars,
                         size_t location)
{
    size_t maxIndex = 0;
    for (auto &outputVar : shaderOutputVars)
    {
        if (outputVar.outputLocation == location)
        {
            maxIndex = std::max(maxIndex, outputVar.outputIndex);
        }
    }
    return maxIndex;
}

void GetDefaultOutputLayoutFromShader(
    const std::vector<PixelShaderOutputVariable> &shaderOutputVars,
    std::vector<GLenum> *outputLayoutOut)
{
    outputLayoutOut->clear();

    if (!shaderOutputVars.empty())
    {
        size_t location = shaderOutputVars[0].outputLocation;
        size_t maxIndex = GetMaxOutputIndex(shaderOutputVars, location);
        outputLayoutOut->assign(maxIndex + 1,
                                GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(location));
    }
}

void GetDefaultImage2DBindLayoutFromShader(const std::vector<sh::ShaderVariable> &image2DUniforms,
                                           gl::ImageUnitTextureTypeMap *image2DBindLayout)
{
    image2DBindLayout->clear();

    for (const sh::ShaderVariable &image2D : image2DUniforms)
    {
        if (gl::IsImage2DType(image2D.type))
        {
            if (image2D.binding == -1)
            {
                image2DBindLayout->insert(std::make_pair(0, gl::TextureType::_2D));
            }
            else
            {
                for (unsigned int index = 0; index < image2D.getArraySizeProduct(); index++)
                {
                    image2DBindLayout->insert(
                        std::make_pair(image2D.binding + index, gl::TextureType::_2D));
                }
            }
        }
    }
}

gl::PrimitiveMode GetGeometryShaderTypeFromDrawMode(gl::PrimitiveMode drawMode)
{
    switch (drawMode)
    {
        // Uses the point sprite geometry shader.
        case gl::PrimitiveMode::Points:
            return gl::PrimitiveMode::Points;

        // All line drawing uses the same geometry shader.
        case gl::PrimitiveMode::Lines:
        case gl::PrimitiveMode::LineStrip:
        case gl::PrimitiveMode::LineLoop:
            return gl::PrimitiveMode::Lines;

        // The triangle fan primitive is emulated with strips in D3D11.
        case gl::PrimitiveMode::Triangles:
        case gl::PrimitiveMode::TriangleFan:
            return gl::PrimitiveMode::Triangles;

        // Special case for triangle strips.
        case gl::PrimitiveMode::TriangleStrip:
            return gl::PrimitiveMode::TriangleStrip;

        default:
            UNREACHABLE();
            return gl::PrimitiveMode::InvalidEnum;
    }
}

// Helper class that gathers uniform info from the default uniform block.
class UniformEncodingVisitorD3D : public sh::BlockEncoderVisitor
{
  public:
    UniformEncodingVisitorD3D(gl::ShaderType shaderType,
                              HLSLRegisterType registerType,
                              sh::BlockLayoutEncoder *encoder,
                              D3DUniformMap *uniformMapOut)
        : sh::BlockEncoderVisitor("", "", encoder),
          mShaderType(shaderType),
          mRegisterType(registerType),
          mUniformMapOut(uniformMapOut)
    {}

    void visitNamedOpaqueObject(const sh::ShaderVariable &sampler,
                                const std::string &name,
                                const std::string &mappedName,
                                const std::vector<unsigned int> &arraySizes) override
    {
        auto uniformMapEntry = mUniformMapOut->find(name);
        if (uniformMapEntry == mUniformMapOut->end())
        {
            (*mUniformMapOut)[name] =
                new D3DUniform(sampler.type, mRegisterType, name, sampler.arraySizes, true);
        }
    }

    void encodeVariable(const sh::ShaderVariable &variable,
                        const sh::BlockMemberInfo &variableInfo,
                        const std::string &name,
                        const std::string &mappedName) override
    {
        auto uniformMapEntry   = mUniformMapOut->find(name);
        D3DUniform *d3dUniform = nullptr;

        if (uniformMapEntry != mUniformMapOut->end())
        {
            d3dUniform = uniformMapEntry->second;
        }
        else
        {
            d3dUniform =
                new D3DUniform(variable.type, mRegisterType, name, variable.arraySizes, true);
            (*mUniformMapOut)[name] = d3dUniform;
        }

        d3dUniform->registerElement = static_cast<unsigned int>(
            sh::BlockLayoutEncoder::GetBlockRegisterElement(variableInfo));
        unsigned int reg =
            static_cast<unsigned int>(sh::BlockLayoutEncoder::GetBlockRegister(variableInfo));

        ASSERT(mShaderType != gl::ShaderType::InvalidEnum);
        d3dUniform->mShaderRegisterIndexes[mShaderType] = reg;
    }

  private:
    gl::ShaderType mShaderType;
    HLSLRegisterType mRegisterType;
    D3DUniformMap *mUniformMapOut;
};

}  // anonymous namespace

// D3DUniform Implementation
D3DUniform::D3DUniform(GLenum type,
                       HLSLRegisterType reg,
                       const std::string &nameIn,
                       const std::vector<unsigned int> &arraySizesIn,
                       bool defaultBlock)
    : typeInfo(gl::GetUniformTypeInfo(type)),
      name(nameIn),
      arraySizes(arraySizesIn),
      mShaderData({}),
      regType(reg),
      registerCount(0),
      registerElement(0)
{
    mShaderRegisterIndexes.fill(GL_INVALID_INDEX);

    // We use data storage for default block uniforms to cache values that are sent to D3D during
    // rendering
    // Uniform blocks/buffers are treated separately by the Renderer (ES3 path only)
    if (defaultBlock)
    {
        // Use the row count as register count, will work for non-square matrices.
        registerCount = typeInfo.rowCount * getArraySizeProduct();
    }
}

D3DUniform::~D3DUniform() {}

unsigned int D3DUniform::getArraySizeProduct() const
{
    return gl::ArraySizeProduct(arraySizes);
}

const uint8_t *D3DUniform::getDataPtrToElement(size_t elementIndex) const
{
    ASSERT((!isArray() && elementIndex == 0) ||
           (isArray() && elementIndex < getArraySizeProduct()));

    if (isSampler())
    {
        return reinterpret_cast<const uint8_t *>(&mSamplerData[elementIndex]);
    }

    return ANGLE_UNSAFE_TODO(firstNonNullData() +
                             (elementIndex > 0 ? (typeInfo.internalSize * elementIndex) : 0u));
}

bool D3DUniform::isSampler() const
{
    return typeInfo.isSampler;
}

bool D3DUniform::isImage() const
{
    return typeInfo.isImageType;
}

bool D3DUniform::isImage2D() const
{
    return gl::IsImage2DType(typeInfo.type);
}

bool D3DUniform::isReferencedByShader(gl::ShaderType shaderType) const
{
    return mShaderRegisterIndexes[shaderType] != GL_INVALID_INDEX;
}

const uint8_t *D3DUniform::firstNonNullData() const
{
    if (!mSamplerData.empty())
    {
        return reinterpret_cast<const uint8_t *>(mSamplerData.data());
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        if (mShaderData[shaderType])
        {
            return mShaderData[shaderType];
        }
    }

    UNREACHABLE();
    return nullptr;
}

// D3DInterfaceBlock Implementation
D3DInterfaceBlock::D3DInterfaceBlock()
{
    mShaderRegisterIndexes.fill(GL_INVALID_INDEX);
}

D3DInterfaceBlock::D3DInterfaceBlock(const D3DInterfaceBlock &other) = default;

D3DUniformBlock::D3DUniformBlock()
{
    mUseStructuredBuffers.fill(false);
    mByteWidths.fill(0u);
    mStructureByteStrides.fill(0u);
}

D3DUniformBlock::D3DUniformBlock(const D3DUniformBlock &other) = default;

// D3DVarying Implementation
D3DVarying::D3DVarying() : semanticIndex(0), componentCount(0), outputSlot(0) {}

D3DVarying::D3DVarying(const std::string &semanticNameIn,
                       unsigned int semanticIndexIn,
                       unsigned int componentCountIn,
                       unsigned int outputSlotIn)
    : semanticName(semanticNameIn),
      semanticIndex(semanticIndexIn),
      componentCount(componentCountIn),
      outputSlot(outputSlotIn)
{}

D3DVertexExecutable::D3DVertexExecutable(const gl::InputLayout &inputLayout,
                                         const Signature &signature,
                                         ShaderExecutableD3D *shaderExecutable)
    : mInputs(inputLayout), mSignature(signature), mShaderExecutable(shaderExecutable)
{}

D3DVertexExecutable::~D3DVertexExecutable()
{
    SafeDelete(mShaderExecutable);
}

// static
D3DVertexExecutable::HLSLAttribType D3DVertexExecutable::GetAttribType(GLenum type)
{
    switch (type)
    {
        case GL_INT:
            return HLSLAttribType::SIGNED_INT;
        case GL_UNSIGNED_INT:
            return HLSLAttribType::UNSIGNED_INT;
        case GL_SIGNED_NORMALIZED:
        case GL_UNSIGNED_NORMALIZED:
        case GL_FLOAT:
            return HLSLAttribType::FLOAT;
        default:
            UNREACHABLE();
            return HLSLAttribType::FLOAT;
    }
}

// static
void D3DVertexExecutable::getSignature(RendererD3D *renderer,
                                       const gl::InputLayout &inputLayout,
                                       Signature *signatureOut)
{
    signatureOut->assign(inputLayout.size(), HLSLAttribType::FLOAT);

    for (size_t index = 0; index < inputLayout.size(); ++index)
    {
        angle::FormatID vertexFormatID = inputLayout[index];
        if (vertexFormatID == angle::FormatID::NONE)
        {
            continue;
        }

        VertexConversionType conversionType = renderer->getVertexConversionType(vertexFormatID);
        if ((conversionType & VERTEX_CONVERT_GPU) == 0)
        {
            continue;
        }

        GLenum componentType   = renderer->getVertexComponentType(vertexFormatID);
        (*signatureOut)[index] = GetAttribType(componentType);
    }
}

bool D3DVertexExecutable::matchesSignature(const Signature &signature) const
{
    size_t limit = std::max(mSignature.size(), signature.size());
    for (size_t index = 0; index < limit; ++index)
    {
        // treat undefined indexes as FLOAT
        auto a = index < signature.size() ? signature[index] : HLSLAttribType::FLOAT;
        auto b = index < mSignature.size() ? mSignature[index] : HLSLAttribType::FLOAT;
        if (a != b)
        {
            return false;
        }
    }

    return true;
}

D3DPixelExecutable::D3DPixelExecutable(const std::vector<GLenum> &outputSignature,
                                       const gl::ImageUnitTextureTypeMap &image2DSignature,
                                       ShaderExecutableD3D *shaderExecutable)
    : mOutputSignature(outputSignature),
      mImage2DSignature(image2DSignature),
      mShaderExecutable(shaderExecutable)
{}

D3DPixelExecutable::~D3DPixelExecutable()
{
    SafeDelete(mShaderExecutable);
}

D3DSampler::D3DSampler() : active(false), logicalTextureUnit(0), textureType(gl::TextureType::_2D)
{}

D3DImage::D3DImage() : active(false), logicalImageUnit(0) {}

unsigned int ProgramExecutableD3D::mCurrentSerial = 1;

ProgramExecutableD3D::ProgramExecutableD3D(const gl::ProgramExecutable *executable)
    : ProgramExecutableImpl(executable),
      mUsesPointSize(false),
      mUsesFlatInterpolation(false),
      mUsedShaderSamplerRanges({}),
      mDirtySamplerMapping(true),
      mUsedImageRange({}),
      mUsedReadonlyImageRange({}),
      mSerial(issueSerial())
{
    reset();
}

ProgramExecutableD3D::~ProgramExecutableD3D() {}

void ProgramExecutableD3D::destroy(const gl::Context *context) {}

void ProgramExecutableD3D::reset()
{
    mVertexExecutables.clear();
    mPixelExecutables.clear();

    for (auto &geometryExecutable : mGeometryExecutables)
    {
        geometryExecutable.reset(nullptr);
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        mShaderHLSL[shaderType].clear();
    }

    mFragDepthUsage      = FragDepthUsage::Unused;
    mUsesSampleMask      = false;
    mHasMultiviewEnabled = false;
    mUsesVertexID        = false;
    mUsesViewID          = false;
    mPixelShaderKey.clear();
    mUsesPointSize         = false;
    mUsesFlatInterpolation = false;

    SafeDeleteContainer(mD3DUniforms);
    mD3DUniformBlocks.clear();

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        mShaderUniformStorages[shaderType].reset();
        mShaderSamplers[shaderType].clear();
        mImages[shaderType].clear();
        mReadonlyImages[shaderType].clear();
    }

    mUsedShaderSamplerRanges.fill({0, 0});
    mDirtySamplerMapping = true;
    mUsedImageRange.fill({0, 0});
    mUsedReadonlyImageRange.fill({0, 0});

    mAttribLocationToD3DSemantic.fill(-1);

    mStreamOutVaryings.clear();

    mGeometryShaderPreamble.clear();

    markUniformsClean();

    mCachedPixelExecutableIndex.reset();
    mCachedVertexExecutableIndex.reset();
}

bool ProgramExecutableD3D::load(const gl::Context *context,
                                RendererD3D *renderer,
                                gl::BinaryInputStream *stream)
{
    gl::InfoLog &infoLog = mExecutable->getInfoLog();

    reset();

    DeviceIdentifier binaryDeviceIdentifier = {};
    stream->readBytes(angle::byte_span_from_ref(binaryDeviceIdentifier));

    DeviceIdentifier identifier = renderer->getAdapterIdentifier();
    if (ANGLE_UNSAFE_TODO(memcmp(&identifier, &binaryDeviceIdentifier, sizeof(DeviceIdentifier))) !=
        0)
    {
        infoLog << "Invalid program binary, device configuration has changed.";
        return false;
    }

    int compileFlags = stream->readInt<int>();
    if (compileFlags != ANGLE_COMPILE_OPTIMIZATION_LEVEL)
    {
        infoLog << "Mismatched compilation flags.";
        return false;
    }

    for (int &index : mAttribLocationToD3DSemantic)
    {
        stream->readInt(&index);
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        size_t samplerCount = stream->readInt<size_t>();
        for (size_t sampleIndex = 0; sampleIndex < samplerCount; ++sampleIndex)
        {
            D3DSampler sampler;
            stream->readBool(&sampler.active);
            stream->readInt(&sampler.logicalTextureUnit);
            stream->readEnum(&sampler.textureType);
            mShaderSamplers[shaderType].push_back(sampler);
        }

        unsigned int samplerRangeLow, samplerRangeHigh;
        stream->readInt(&samplerRangeLow);
        stream->readInt(&samplerRangeHigh);
        mUsedShaderSamplerRanges[shaderType] = gl::RangeUI(samplerRangeLow, samplerRangeHigh);
    }

    {
        const gl::ShaderType shaderType = gl::ShaderType::Fragment;
        size_t imageCount               = stream->readInt<size_t>();
        for (size_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
        {
            D3DImage image;
            stream->readBool(&image.active);
            stream->readInt(&image.logicalImageUnit);
            mImages[shaderType].push_back(image);
        }

        size_t readonlyImageCount = stream->readInt<size_t>();
        for (size_t imageIndex = 0; imageIndex < readonlyImageCount; ++imageIndex)
        {
            D3DImage image;
            stream->readBool(&image.active);
            stream->readInt(&image.logicalImageUnit);
            mReadonlyImages[shaderType].push_back(image);
        }

        unsigned int imageRangeLow, imageRangeHigh, readonlyImageRangeLow, readonlyImageRangeHigh;
        stream->readInt(&imageRangeLow);
        stream->readInt(&imageRangeHigh);
        stream->readInt(&readonlyImageRangeLow);
        stream->readInt(&readonlyImageRangeHigh);
        mUsedImageRange[shaderType] = gl::RangeUI(imageRangeLow, imageRangeHigh);
        mUsedReadonlyImageRange[shaderType] =
            gl::RangeUI(readonlyImageRangeLow, readonlyImageRangeHigh);
    }

    if (stream->error())
    {
        infoLog << "Invalid program binary.";
        return false;
    }

    {
        const gl::ShaderType shaderType = gl::ShaderType::Fragment;
        size_t image2DUniformCount      = stream->readInt<size_t>();
        if (stream->error())
        {
            infoLog << "Invalid program binary.";
            return false;
        }

        ASSERT(mImage2DUniforms[shaderType].empty());
        for (size_t image2DUniformIndex = 0; image2DUniformIndex < image2DUniformCount;
             ++image2DUniformIndex)
        {
            sh::ShaderVariable image2Duniform;
            gl::LoadShaderVar(stream, &image2Duniform);
            mImage2DUniforms[shaderType].push_back(image2Duniform);
        }
    }

    size_t uniformCount = stream->readInt<size_t>();
    if (stream->error())
    {
        infoLog << "Invalid program binary.";
        return false;
    }

    const auto &linkedUniforms = mExecutable->getUniforms();
    ASSERT(mD3DUniforms.empty());
    for (size_t uniformIndex = 0; uniformIndex < uniformCount; uniformIndex++)
    {
        const gl::LinkedUniform &linkedUniform = linkedUniforms[uniformIndex];
        // Could D3DUniform just change to use unsigned int instead of std::vector for arraySizes?
        // Frontend always flatten the array to at most 1D array.
        std::vector<unsigned int> arraySizes;
        if (linkedUniform.isArray())
        {
            arraySizes.push_back(linkedUniform.getBasicTypeElementCount());
        }
        D3DUniform *d3dUniform = new D3DUniform(linkedUniform.getType(), HLSLRegisterType::None,
                                                mExecutable->getUniformNames()[uniformIndex],
                                                arraySizes, linkedUniform.isInDefaultBlock());
        stream->readEnum(&d3dUniform->regType);
        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            stream->readInt(&d3dUniform->mShaderRegisterIndexes[shaderType]);
        }
        stream->readInt(&d3dUniform->registerCount);
        stream->readInt(&d3dUniform->registerElement);

        mD3DUniforms.push_back(d3dUniform);
    }

    size_t blockCount = stream->readInt<size_t>();
    if (stream->error())
    {
        infoLog << "Invalid program binary.";
        return false;
    }

    ASSERT(mD3DUniformBlocks.empty());
    for (size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
    {
        D3DUniformBlock uniformBlock;
        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            stream->readInt(&uniformBlock.mShaderRegisterIndexes[shaderType]);
            stream->readBool(&uniformBlock.mUseStructuredBuffers[shaderType]);
            stream->readInt(&uniformBlock.mByteWidths[shaderType]);
            stream->readInt(&uniformBlock.mStructureByteStrides[shaderType]);
        }
        mD3DUniformBlocks.push_back(uniformBlock);
    }

    size_t streamOutVaryingCount = stream->readInt<size_t>();
    mStreamOutVaryings.resize(streamOutVaryingCount);
    for (size_t varyingIndex = 0; varyingIndex < streamOutVaryingCount; ++varyingIndex)
    {
        D3DVarying *varying = &mStreamOutVaryings[varyingIndex];

        stream->readString(&varying->semanticName);
        stream->readInt(&varying->semanticIndex);
        stream->readInt(&varying->componentCount);
        stream->readInt(&varying->outputSlot);
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        stream->readString(&mShaderHLSL[shaderType]);
        stream->readBytes(angle::byte_span_from_ref(mShaderWorkarounds[shaderType]));
    }

    stream->readEnum(&mFragDepthUsage);
    stream->readBool(&mUsesSampleMask);
    stream->readBool(&mHasMultiviewEnabled);
    stream->readBool(&mUsesVertexID);
    stream->readBool(&mUsesViewID);
    stream->readBool(&mUsesPointSize);
    stream->readBool(&mUsesFlatInterpolation);

    const size_t pixelShaderKeySize = stream->readInt<size_t>();
    mPixelShaderKey.resize(pixelShaderKeySize);
    for (size_t pixelShaderKeyIndex = 0; pixelShaderKeyIndex < pixelShaderKeySize;
         pixelShaderKeyIndex++)
    {
        stream->readInt(&mPixelShaderKey[pixelShaderKeyIndex].type);
        stream->readString(&mPixelShaderKey[pixelShaderKeyIndex].name);
        stream->readString(&mPixelShaderKey[pixelShaderKeyIndex].source);
        stream->readInt(&mPixelShaderKey[pixelShaderKeyIndex].outputLocation);
        stream->readInt(&mPixelShaderKey[pixelShaderKeyIndex].outputIndex);
    }

    stream->readString(&mGeometryShaderPreamble);

    return true;
}

angle::Result ProgramExecutableD3D::loadBinaryShaderExecutables(d3d::Context *contextD3D,
                                                                RendererD3D *renderer,
                                                                gl::BinaryInputStream *stream)
{
    gl::InfoLog &infoLog = mExecutable->getInfoLog();
    bool separateAttribs = mExecutable->getTransformFeedbackBufferMode() == GL_SEPARATE_ATTRIBS;

    size_t vertexShaderCount = stream->readInt<size_t>();
    for (size_t vertexShaderIndex = 0; vertexShaderIndex < vertexShaderCount; vertexShaderIndex++)
    {
        size_t inputLayoutSize = stream->readInt<size_t>();
        gl::InputLayout inputLayout(inputLayoutSize, angle::FormatID::NONE);

        for (size_t inputIndex = 0; inputIndex < inputLayoutSize; inputIndex++)
        {
            inputLayout[inputIndex] = stream->readEnum<angle::FormatID>();
        }

        size_t vertexShaderSize                   = stream->readInt<size_t>();
        const unsigned char *vertexShaderFunction = stream->remainingSpan().data();
        ShaderExecutableD3D *shaderExecutable     = nullptr;

        ANGLE_TRY(renderer->loadExecutable(contextD3D, vertexShaderFunction, vertexShaderSize,
                                           gl::ShaderType::Vertex, mStreamOutVaryings,
                                           separateAttribs, &shaderExecutable));

        if (!shaderExecutable)
        {
            infoLog << "Could not create vertex shader.";
            return angle::Result::Stop;
        }

        // generated converted input layout
        D3DVertexExecutable::Signature signature;
        D3DVertexExecutable::getSignature(renderer, inputLayout, &signature);

        // add new binary
        mVertexExecutables.push_back(std::unique_ptr<D3DVertexExecutable>(
            new D3DVertexExecutable(inputLayout, signature, shaderExecutable)));

        stream->skip(vertexShaderSize);
    }

    size_t pixelShaderCount = stream->readInt<size_t>();
    for (size_t pixelShaderIndex = 0; pixelShaderIndex < pixelShaderCount; pixelShaderIndex++)
    {
        size_t outputCount = stream->readInt<size_t>();
        std::vector<GLenum> outputs(outputCount);
        for (size_t outputIndex = 0; outputIndex < outputCount; outputIndex++)
        {
            stream->readInt(&outputs[outputIndex]);
        }

        const size_t image2DCount = stream->readInt<size_t>();
        gl::ImageUnitTextureTypeMap image2Ds;
        for (size_t index = 0; index < image2DCount; index++)
        {
            unsigned int imageUint;
            gl::TextureType textureType;
            stream->readInt(&imageUint);
            stream->readEnum(&textureType);
            image2Ds.insert({imageUint, textureType});
        }

        size_t pixelShaderSize                   = stream->readInt<size_t>();
        const unsigned char *pixelShaderFunction = stream->remainingSpan().data();
        ShaderExecutableD3D *shaderExecutable    = nullptr;

        ANGLE_TRY(renderer->loadExecutable(contextD3D, pixelShaderFunction, pixelShaderSize,
                                           gl::ShaderType::Fragment, mStreamOutVaryings,
                                           separateAttribs, &shaderExecutable));

        if (!shaderExecutable)
        {
            infoLog << "Could not create pixel shader.";
            return angle::Result::Stop;
        }

        // add new binary
        mPixelExecutables.push_back(std::unique_ptr<D3DPixelExecutable>(
            new D3DPixelExecutable(outputs, image2Ds, shaderExecutable)));

        stream->skip(pixelShaderSize);
    }

    for (std::unique_ptr<ShaderExecutableD3D> &geometryExe : mGeometryExecutables)
    {
        size_t geometryShaderSize = stream->readInt<size_t>();
        if (geometryShaderSize == 0)
        {
            continue;
        }

        const unsigned char *geometryShaderFunction = stream->remainingSpan().data();
        ShaderExecutableD3D *geometryExecutable     = nullptr;
        ANGLE_TRY(renderer->loadExecutable(contextD3D, geometryShaderFunction, geometryShaderSize,
                                           gl::ShaderType::Geometry, mStreamOutVaryings,
                                           separateAttribs, &geometryExecutable));

        if (!geometryExecutable)
        {
            infoLog << "Could not create geometry shader.";
            return angle::Result::Stop;
        }

        geometryExe.reset(geometryExecutable);

        stream->skip(geometryShaderSize);
    }

    for (const gl::ShaderType shaderType : {gl::ShaderType::Vertex, gl::ShaderType::Fragment})
    {
        size_t bindLayoutCount = stream->readInt<size_t>();
        for (size_t bindLayoutIndex = 0; bindLayoutIndex < bindLayoutCount; bindLayoutIndex++)
        {
            mImage2DBindLayoutCache[shaderType].insert(std::pair<unsigned int, gl::TextureType>(
                stream->readInt<unsigned int>(), gl::TextureType::_2D));
        }
    }

    initializeUniformStorage(renderer, mExecutable->getLinkedShaderStages());

    dirtyAllUniforms();

    return angle::Result::Continue;
}

void ProgramExecutableD3D::save(const gl::Context *context,
                                RendererD3D *renderer,
                                gl::BinaryOutputStream *stream)
{
    // Output the DeviceIdentifier before we output any shader code
    // When we load the binary again later, we can validate the device identifier before trying to
    // compile any HLSL
    DeviceIdentifier binaryIdentifier = renderer->getAdapterIdentifier();
    stream->writeBytes(angle::byte_span_from_ref(binaryIdentifier));

    stream->writeInt(ANGLE_COMPILE_OPTIMIZATION_LEVEL);

    for (int d3dSemantic : mAttribLocationToD3DSemantic)
    {
        stream->writeInt(d3dSemantic);
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        stream->writeInt(mShaderSamplers[shaderType].size());
        for (const D3DSampler &sampler : mShaderSamplers[shaderType])
        {
            stream->writeBool(sampler.active);
            stream->writeInt(sampler.logicalTextureUnit);
            stream->writeEnum(sampler.textureType);
        }

        stream->writeInt(mUsedShaderSamplerRanges[shaderType].low());
        stream->writeInt(mUsedShaderSamplerRanges[shaderType].high());
    }

    {
        const gl::ShaderType shaderType = gl::ShaderType::Fragment;
        stream->writeInt(mImages[shaderType].size());
        for (const D3DImage &image : mImages[shaderType])
        {
            stream->writeBool(image.active);
            stream->writeInt(image.logicalImageUnit);
        }

        stream->writeInt(mReadonlyImages[shaderType].size());
        for (const D3DImage &readonlyImage : mReadonlyImages[shaderType])
        {
            stream->writeBool(readonlyImage.active);
            stream->writeInt(readonlyImage.logicalImageUnit);
        }

        stream->writeInt(mUsedImageRange[shaderType].low());
        stream->writeInt(mUsedImageRange[shaderType].high());
        stream->writeInt(mUsedReadonlyImageRange[shaderType].low());
        stream->writeInt(mUsedReadonlyImageRange[shaderType].high());
    }

    {
        const gl::ShaderType shaderType = gl::ShaderType::Fragment;
        stream->writeInt(mImage2DUniforms[shaderType].size());
        for (const sh::ShaderVariable &image2DUniform : mImage2DUniforms[shaderType])
        {
            gl::WriteShaderVar(stream, image2DUniform);
        }
    }

    stream->writeInt(mD3DUniforms.size());
    for (const D3DUniform *uniform : mD3DUniforms)
    {
        // Type, name and arraySize are redundant, so aren't stored in the binary.
        stream->writeEnum(uniform->regType);
        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            stream->writeIntOrNegOne(uniform->mShaderRegisterIndexes[shaderType]);
        }
        stream->writeInt(uniform->registerCount);
        stream->writeInt(uniform->registerElement);
    }

    stream->writeInt(mD3DUniformBlocks.size());
    for (const D3DUniformBlock &uniformBlock : mD3DUniformBlocks)
    {
        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            stream->writeIntOrNegOne(uniformBlock.mShaderRegisterIndexes[shaderType]);
            stream->writeBool(uniformBlock.mUseStructuredBuffers[shaderType]);
            stream->writeInt(uniformBlock.mByteWidths[shaderType]);
            stream->writeInt(uniformBlock.mStructureByteStrides[shaderType]);
        }
    }

    stream->writeInt(mStreamOutVaryings.size());
    for (const D3DVarying &varying : mStreamOutVaryings)
    {
        stream->writeString(varying.semanticName);
        stream->writeInt(varying.semanticIndex);
        stream->writeInt(varying.componentCount);
        stream->writeInt(varying.outputSlot);
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        stream->writeString(mShaderHLSL[shaderType]);
        stream->writeBytes(angle::byte_span_from_ref(mShaderWorkarounds[shaderType]));
    }

    stream->writeEnum(mFragDepthUsage);
    stream->writeBool(mUsesSampleMask);
    stream->writeBool(mHasMultiviewEnabled);
    stream->writeBool(mUsesVertexID);
    stream->writeBool(mUsesViewID);
    stream->writeBool(mUsesPointSize);
    stream->writeBool(mUsesFlatInterpolation);

    const std::vector<PixelShaderOutputVariable> &pixelShaderKey = mPixelShaderKey;
    stream->writeInt(pixelShaderKey.size());
    for (const PixelShaderOutputVariable &variable : pixelShaderKey)
    {
        stream->writeInt(variable.type);
        stream->writeString(variable.name);
        stream->writeString(variable.source);
        stream->writeInt(variable.outputLocation);
        stream->writeInt(variable.outputIndex);
    }

    stream->writeString(mGeometryShaderPreamble);

    stream->writeInt(mVertexExecutables.size());
    for (const std::unique_ptr<D3DVertexExecutable> &vertexExecutable : mVertexExecutables)
    {
        const gl::InputLayout &inputLayout = vertexExecutable->inputs();
        stream->writeInt(inputLayout.size());

        for (angle::FormatID inputFormat : inputLayout)
        {
            stream->writeEnum(inputFormat);
        }

        size_t vertexShaderSize = vertexExecutable->shaderExecutable()->getLength();
        stream->writeInt(vertexShaderSize);

        const uint8_t *vertexBlob = vertexExecutable->shaderExecutable()->getFunction();
        stream->writeBytes(ANGLE_UNSAFE_TODO(angle::Span(vertexBlob, vertexShaderSize)));
    }

    stream->writeInt(mPixelExecutables.size());
    for (const std::unique_ptr<D3DPixelExecutable> &pixelExecutable : mPixelExecutables)
    {

        const std::vector<GLenum> &outputs = pixelExecutable->outputSignature();
        stream->writeInt(outputs.size());
        for (unsigned int output : outputs)
        {
            stream->writeInt(output);
        }

        const gl::ImageUnitTextureTypeMap &image2Ds = pixelExecutable->image2DSignature();
        stream->writeInt(image2Ds.size());
        for (const auto &image2D : image2Ds)
        {
            stream->writeInt(image2D.first);
            stream->writeEnum(image2D.second);
        }

        size_t pixelShaderSize = pixelExecutable->shaderExecutable()->getLength();
        stream->writeInt(pixelShaderSize);

        const uint8_t *pixelBlob = pixelExecutable->shaderExecutable()->getFunction();
        stream->writeBytes(ANGLE_UNSAFE_TODO(angle::Span(pixelBlob, pixelShaderSize)));
    }

    for (auto const &geometryExecutable : mGeometryExecutables)
    {
        if (!geometryExecutable)
        {
            stream->writeInt<size_t>(0);
            continue;
        }

        size_t geometryShaderSize = geometryExecutable->getLength();
        stream->writeInt(geometryShaderSize);
        stream->writeBytes(
            ANGLE_UNSAFE_TODO(angle::Span(geometryExecutable->getFunction(), geometryShaderSize)));
    }

    for (const gl::ShaderType shaderType : {gl::ShaderType::Vertex, gl::ShaderType::Fragment})
    {
        stream->writeInt(mImage2DBindLayoutCache[shaderType].size());
        for (auto &image2DBindLayout : mImage2DBindLayoutCache[shaderType])
        {
            stream->writeInt(image2DBindLayout.first);
        }
    }
}

bool ProgramExecutableD3D::hasVertexExecutableForCachedInputLayout()
{
    return mCachedVertexExecutableIndex.valid();
}

bool ProgramExecutableD3D::hasGeometryExecutableForPrimitiveType(RendererD3D *renderer,
                                                                 const gl::State &state,
                                                                 gl::PrimitiveMode drawMode)
{
    if (!usesGeometryShader(renderer, state.getProvokingVertex(), drawMode))
    {
        // No shader necessary mean we have the required (null) executable.
        return true;
    }

    gl::PrimitiveMode geometryShaderType = GetGeometryShaderTypeFromDrawMode(drawMode);
    return mGeometryExecutables[geometryShaderType].get() != nullptr;
}

bool ProgramExecutableD3D::hasPixelExecutableForCachedOutputLayout()
{
    return mCachedPixelExecutableIndex.valid();
}

void ProgramExecutableD3D::dirtyAllUniforms()
{
    mShaderUniformsDirty = mExecutable->getLinkedShaderStages();
}

void ProgramExecutableD3D::markUniformsClean()
{
    mShaderUniformsDirty.reset();
}

const std::vector<D3DUBOCache> &ProgramExecutableD3D::getShaderUniformBufferCache(
    gl::ShaderType shaderType) const
{
    return mShaderUBOCaches[shaderType];
}

const std::vector<D3DUBOCacheUseSB> &ProgramExecutableD3D::getShaderUniformBufferCacheUseSB(
    gl::ShaderType shaderType) const
{
    return mShaderUBOCachesUseSB[shaderType];
}

GLint ProgramExecutableD3D::getSamplerMapping(gl::ShaderType type,
                                              unsigned int samplerIndex,
                                              const gl::Caps &caps) const
{
    GLint logicalTextureUnit = -1;

    ASSERT(type != gl::ShaderType::InvalidEnum);

    ASSERT(samplerIndex < static_cast<unsigned int>(caps.maxShaderTextureImageUnits[type]));

    const auto &samplers = mShaderSamplers[type];
    if (samplerIndex < samplers.size() && samplers[samplerIndex].active)
    {
        logicalTextureUnit = samplers[samplerIndex].logicalTextureUnit;
    }

    if (logicalTextureUnit >= 0 && logicalTextureUnit < caps.maxCombinedTextureImageUnits)
    {
        return logicalTextureUnit;
    }

    return -1;
}

// Returns the texture type for a given Direct3D 9 sampler type and
// index (0-15 for the pixel shader and 0-3 for the vertex shader).
gl::TextureType ProgramExecutableD3D::getSamplerTextureType(gl::ShaderType type,
                                                            unsigned int samplerIndex) const
{
    ASSERT(type != gl::ShaderType::InvalidEnum);

    const auto &samplers = mShaderSamplers[type];
    ASSERT(samplerIndex < samplers.size());
    ASSERT(samplers[samplerIndex].active);

    return samplers[samplerIndex].textureType;
}

gl::RangeUI ProgramExecutableD3D::getUsedSamplerRange(gl::ShaderType type) const
{
    ASSERT(type != gl::ShaderType::InvalidEnum);
    return mUsedShaderSamplerRanges[type];
}

void ProgramExecutableD3D::updateSamplerMapping()
{
    ASSERT(mDirtySamplerMapping);

    mDirtySamplerMapping = false;

    // Retrieve sampler uniform values
    for (const D3DUniform *d3dUniform : mD3DUniforms)
    {
        if (!d3dUniform->isSampler())
        {
            continue;
        }

        int count = d3dUniform->getArraySizeProduct();

        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            if (!d3dUniform->isReferencedByShader(shaderType))
            {
                continue;
            }

            unsigned int firstIndex = d3dUniform->mShaderRegisterIndexes[shaderType];

            std::vector<D3DSampler> &samplers = mShaderSamplers[shaderType];
            for (int i = 0; i < count; i++)
            {
                unsigned int samplerIndex = firstIndex + i;

                if (samplerIndex < samplers.size())
                {
                    ASSERT(samplers[samplerIndex].active);
                    samplers[samplerIndex].logicalTextureUnit = d3dUniform->mSamplerData[i];
                }
            }
        }
    }
}

GLint ProgramExecutableD3D::getImageMapping(gl::ShaderType type,
                                            unsigned int imageIndex,
                                            bool readonly,
                                            const gl::Caps &caps) const
{
    GLint logicalImageUnit = -1;
    ASSERT(imageIndex < static_cast<unsigned int>(caps.maxImageUnits));
    if (readonly && imageIndex < mReadonlyImages[type].size() &&
        mReadonlyImages[type][imageIndex].active)
    {
        logicalImageUnit = mReadonlyImages[type][imageIndex].logicalImageUnit;
    }
    else if (imageIndex < mImages[type].size() && mImages[type][imageIndex].active)
    {
        logicalImageUnit = mImages[type][imageIndex].logicalImageUnit;
    }

    if (logicalImageUnit >= 0 && logicalImageUnit < caps.maxImageUnits)
    {
        return logicalImageUnit;
    }

    return -1;
}

gl::RangeUI ProgramExecutableD3D::getUsedImageRange(gl::ShaderType type, bool readonly) const
{
    return readonly ? mUsedReadonlyImageRange[type] : mUsedImageRange[type];
}

bool ProgramExecutableD3D::usesPointSpriteEmulation() const
{
    return mUsesPointSize;
}

bool ProgramExecutableD3D::usesGeometryShaderForPointSpriteEmulation() const
{
    return usesPointSpriteEmulation();
}

bool ProgramExecutableD3D::usesGeometryShader(RendererD3D *renderer,
                                              const gl::ProvokingVertexConvention provokingVertex,
                                              const gl::PrimitiveMode drawMode) const
{
    if (mHasMultiviewEnabled && !renderer->canSelectViewInVertexShader())
    {
        return true;
    }
    if (drawMode != gl::PrimitiveMode::Points)
    {
        if (!mUsesFlatInterpolation)
        {
            return false;
        }
        return provokingVertex == gl::ProvokingVertexConvention::LastVertexConvention;
    }
    return usesGeometryShaderForPointSpriteEmulation();
}

angle::Result ProgramExecutableD3D::getVertexExecutableForCachedInputLayout(
    d3d::Context *context,
    RendererD3D *renderer,
    ShaderExecutableD3D **outExectuable,
    gl::InfoLog *infoLog)
{
    if (mCachedVertexExecutableIndex.valid())
    {
        *outExectuable =
            mVertexExecutables[mCachedVertexExecutableIndex.value()]->shaderExecutable();
        return angle::Result::Continue;
    }

    // Generate new dynamic layout with attribute conversions
    std::string vertexHLSL = DynamicHLSL::GenerateVertexShaderForInputLayout(
        renderer, mShaderHLSL[gl::ShaderType::Vertex], mCachedInputLayout,
        mExecutable->getProgramInputs(), mPixelShaderKey.size());
    std::string finalVertexHLSL = DynamicHLSL::GenerateShaderForImage2DBindSignature(
        *this, gl::ShaderType::Vertex, mAttachedShaders[gl::ShaderType::Vertex], vertexHLSL,
        mImage2DUniforms[gl::ShaderType::Vertex], mImage2DBindLayoutCache[gl::ShaderType::Vertex],
        static_cast<unsigned int>(mPixelShaderKey.size()));

    // Generate new vertex executable
    ShaderExecutableD3D *vertexExecutable = nullptr;

    gl::InfoLog tempInfoLog;
    gl::InfoLog *currentInfoLog = infoLog ? infoLog : &tempInfoLog;

    ANGLE_TRY(renderer->compileToExecutable(
        context, *currentInfoLog, finalVertexHLSL, gl::ShaderType::Vertex, mStreamOutVaryings,
        mExecutable->getTransformFeedbackBufferMode() == GL_SEPARATE_ATTRIBS,
        mShaderWorkarounds[gl::ShaderType::Vertex], &vertexExecutable));

    if (vertexExecutable)
    {
        mVertexExecutables.push_back(std::unique_ptr<D3DVertexExecutable>(
            new D3DVertexExecutable(mCachedInputLayout, mCachedVertexSignature, vertexExecutable)));
        mCachedVertexExecutableIndex = mVertexExecutables.size() - 1;
    }
    else if (!infoLog)
    {
        ERR() << "Error compiling dynamic vertex executable:" << std::endl
              << tempInfoLog.str() << std::endl;
    }

    *outExectuable = vertexExecutable;
    return angle::Result::Continue;
}

angle::Result ProgramExecutableD3D::getGeometryExecutableForPrimitiveType(
    d3d::Context *context,
    RendererD3D *renderer,
    const gl::Caps &caps,
    gl::ProvokingVertexConvention provokingVertex,
    gl::PrimitiveMode drawMode,
    ShaderExecutableD3D **outExecutable,
    gl::InfoLog *infoLog)
{
    if (outExecutable)
    {
        *outExecutable = nullptr;
    }

    // Return a null shader if the current rendering doesn't use a geometry shader
    if (!usesGeometryShader(renderer, provokingVertex, drawMode))
    {
        return angle::Result::Continue;
    }

    gl::PrimitiveMode geometryShaderType = GetGeometryShaderTypeFromDrawMode(drawMode);

    if (mGeometryExecutables[geometryShaderType])
    {
        if (outExecutable)
        {
            *outExecutable = mGeometryExecutables[geometryShaderType].get();
        }
        return angle::Result::Continue;
    }

    std::string geometryHLSL = DynamicHLSL::GenerateGeometryShaderHLSL(
        caps, geometryShaderType, renderer->presentPathFastEnabled(), mHasMultiviewEnabled,
        renderer->canSelectViewInVertexShader(), usesGeometryShaderForPointSpriteEmulation(),
        mGeometryShaderPreamble);

    gl::InfoLog tempInfoLog;
    gl::InfoLog *currentInfoLog = infoLog ? infoLog : &tempInfoLog;

    ShaderExecutableD3D *geometryExecutable = nullptr;
    angle::Result result                    = renderer->compileToExecutable(
        context, *currentInfoLog, geometryHLSL, gl::ShaderType::Geometry, mStreamOutVaryings,
        mExecutable->getTransformFeedbackBufferMode() == GL_SEPARATE_ATTRIBS,
        CompilerWorkaroundsD3D(), &geometryExecutable);

    if (!infoLog && result == angle::Result::Stop)
    {
        ERR() << "Error compiling dynamic geometry executable:" << std::endl
              << tempInfoLog.str() << std::endl;
    }

    if (geometryExecutable != nullptr)
    {
        mGeometryExecutables[geometryShaderType].reset(geometryExecutable);
    }

    if (outExecutable)
    {
        *outExecutable = mGeometryExecutables[geometryShaderType].get();
    }
    return result;
}

angle::Result ProgramExecutableD3D::getPixelExecutableForCachedOutputLayout(
    d3d::Context *context,
    RendererD3D *renderer,
    ShaderExecutableD3D **outExecutable,
    gl::InfoLog *infoLog)
{
    if (mCachedPixelExecutableIndex.valid())
    {
        *outExecutable = mPixelExecutables[mCachedPixelExecutableIndex.value()]->shaderExecutable();
        return angle::Result::Continue;
    }

    std::string pixelHLSL = DynamicHLSL::GeneratePixelShaderForOutputSignature(
        mShaderHLSL[gl::ShaderType::Fragment], mPixelShaderKey, mFragDepthUsage, mUsesSampleMask,
        mPixelShaderOutputLayoutCache, mPixelShaderKey.size());

    std::string finalPixelHLSL = DynamicHLSL::GenerateShaderForImage2DBindSignature(
        *this, gl::ShaderType::Fragment, mAttachedShaders[gl::ShaderType::Fragment], pixelHLSL,
        mImage2DUniforms[gl::ShaderType::Fragment],
        mImage2DBindLayoutCache[gl::ShaderType::Fragment],
        static_cast<unsigned int>(mPixelShaderKey.size()));

    // Generate new pixel executable
    ShaderExecutableD3D *pixelExecutable = nullptr;

    gl::InfoLog tempInfoLog;
    gl::InfoLog *currentInfoLog = infoLog ? infoLog : &tempInfoLog;

    ANGLE_TRY(renderer->compileToExecutable(
        context, *currentInfoLog, finalPixelHLSL, gl::ShaderType::Fragment, mStreamOutVaryings,
        mExecutable->getTransformFeedbackBufferMode() == GL_SEPARATE_ATTRIBS,
        mShaderWorkarounds[gl::ShaderType::Fragment], &pixelExecutable));

    if (pixelExecutable)
    {
        mPixelExecutables.push_back(std::unique_ptr<D3DPixelExecutable>(new D3DPixelExecutable(
            mPixelShaderOutputLayoutCache, mImage2DBindLayoutCache[gl::ShaderType::Fragment],
            pixelExecutable)));
        mCachedPixelExecutableIndex = mPixelExecutables.size() - 1;
    }
    else if (!infoLog)
    {
        ERR() << "Error compiling dynamic pixel executable:" << std::endl
              << tempInfoLog.str() << std::endl;
    }

    *outExecutable = pixelExecutable;
    return angle::Result::Continue;
}

bool ProgramExecutableD3D::hasNamedUniform(const std::string &name)
{
    for (D3DUniform *d3dUniform : mD3DUniforms)
    {
        if (d3dUniform->name == name)
        {
            return true;
        }
    }

    return false;
}

void ProgramExecutableD3D::initAttribLocationsToD3DSemantic(
    const gl::SharedCompiledShaderState &vertexShader)
{
    if (!vertexShader)
    {
        return;
    }

    // Init semantic index
    int semanticIndex = 0;
    for (const sh::ShaderVariable &attribute : vertexShader->activeAttributes)
    {
        if (attribute.isBuiltIn())
        {
            continue;
        }

        int regCount    = gl::VariableRegisterCount(attribute.type);
        GLuint location = mExecutable->getAttributeLocation(attribute.name);
        ASSERT(location != std::numeric_limits<GLuint>::max());

        for (int reg = 0; reg < regCount; ++reg)
        {
            mAttribLocationToD3DSemantic[location + reg] = semanticIndex++;
        }
    }
}

void ProgramExecutableD3D::initializeUniformBlocks()
{
    if (mExecutable->getUniformBlocks().empty())
    {
        return;
    }

    ASSERT(mD3DUniformBlocks.empty());

    // Assign registers and update sizes.
    for (const gl::InterfaceBlock &uniformBlock : mExecutable->getUniformBlocks())
    {
        unsigned int uniformBlockElement =
            uniformBlock.pod.isArray ? uniformBlock.pod.arrayElement : 0;

        D3DUniformBlock d3dUniformBlock;

        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            if (uniformBlock.isActive(shaderType))
            {
                ASSERT(mAttachedShaders[shaderType]);
                unsigned int baseRegister =
                    mAttachedShaders[shaderType]->getUniformBlockRegister(uniformBlock.name);
                d3dUniformBlock.mShaderRegisterIndexes[shaderType] =
                    baseRegister + uniformBlockElement;
                bool useStructuredBuffer =
                    mAttachedShaders[shaderType]->shouldUniformBlockUseStructuredBuffer(
                        uniformBlock.name);
                if (useStructuredBuffer)
                {
                    d3dUniformBlock.mUseStructuredBuffers[shaderType] = true;
                    d3dUniformBlock.mByteWidths[shaderType]           = uniformBlock.pod.dataSize;
                    d3dUniformBlock.mStructureByteStrides[shaderType] =
                        uniformBlock.pod.firstFieldArraySize == 0u
                            ? uniformBlock.pod.dataSize
                            : uniformBlock.pod.dataSize / uniformBlock.pod.firstFieldArraySize;
                }
            }
        }

        mD3DUniformBlocks.push_back(d3dUniformBlock);
    }
}

void ProgramExecutableD3D::initializeUniformStorage(RendererD3D *renderer,
                                                    const gl::ShaderBitSet &availableShaderStages)
{
    // Compute total default block size
    gl::ShaderMap<unsigned int> shaderRegisters = {};
    for (const D3DUniform *d3dUniform : mD3DUniforms)
    {
        if (d3dUniform->isSampler())
        {
            continue;
        }

        for (gl::ShaderType shaderType : availableShaderStages)
        {
            if (d3dUniform->isReferencedByShader(shaderType))
            {
                shaderRegisters[shaderType] = std::max(
                    shaderRegisters[shaderType],
                    d3dUniform->mShaderRegisterIndexes[shaderType] + d3dUniform->registerCount);
            }
        }
    }

    // We only reset uniform storages for the shader stages available in the program (attached
    // shaders in ProgramExecutableD3D::link() and linkedShaderStages in
    // ProgramExecutableD3D::load()).
    for (gl::ShaderType shaderType : availableShaderStages)
    {
        mShaderUniformStorages[shaderType].reset(
            renderer->createUniformStorage(shaderRegisters[shaderType] * 16u));
    }

    // Iterate the uniforms again to assign data pointers to default block uniforms.
    for (D3DUniform *d3dUniform : mD3DUniforms)
    {
        if (d3dUniform->isSampler())
        {
            d3dUniform->mSamplerData.resize(d3dUniform->getArraySizeProduct(), 0);
            continue;
        }

        for (gl::ShaderType shaderType : availableShaderStages)
        {
            if (d3dUniform->isReferencedByShader(shaderType))
            {
                d3dUniform->mShaderData[shaderType] =
                    mShaderUniformStorages[shaderType]->getDataPointer(
                        d3dUniform->mShaderRegisterIndexes[shaderType],
                        d3dUniform->registerElement);
            }
        }
    }
}

void ProgramExecutableD3D::updateCachedInputLayoutFromShader(
    RendererD3D *renderer,
    const gl::SharedCompiledShaderState &vertexShader)
{
    GetDefaultInputLayoutFromShader(vertexShader, &mCachedInputLayout);
    D3DVertexExecutable::getSignature(renderer, mCachedInputLayout, &mCachedVertexSignature);
    updateCachedVertexExecutableIndex();
}

void ProgramExecutableD3D::updateCachedOutputLayoutFromShader()
{
    GetDefaultOutputLayoutFromShader(mPixelShaderKey, &mPixelShaderOutputLayoutCache);
    updateCachedPixelExecutableIndex();
}

void ProgramExecutableD3D::updateCachedImage2DBindLayoutFromShader(gl::ShaderType shaderType)
{
    GetDefaultImage2DBindLayoutFromShader(mImage2DUniforms[shaderType],
                                          &mImage2DBindLayoutCache[shaderType]);
    switch (shaderType)
    {
        case gl::ShaderType::Fragment:
            updateCachedPixelExecutableIndex();
            break;
        case gl::ShaderType::Vertex:
            updateCachedVertexExecutableIndex();
            break;
        default:
            ASSERT(false);
            break;
    }
}

void ProgramExecutableD3D::updateCachedInputLayout(RendererD3D *renderer,
                                                   UniqueSerial associatedSerial,
                                                   const gl::State &state)
{
    if (mCurrentVertexArrayStateSerial == associatedSerial)
    {
        return;
    }

    mCurrentVertexArrayStateSerial = associatedSerial;
    mCachedInputLayout.clear();

    const auto &vertexAttributes             = state.getVertexArray()->getVertexAttributes();
    const gl::AttributesMask &attributesMask = mExecutable->getActiveAttribLocationsMask();

    for (size_t locationIndex : attributesMask)
    {
        int d3dSemantic = mAttribLocationToD3DSemantic[locationIndex];

        if (d3dSemantic != -1)
        {
            if (mCachedInputLayout.size() < static_cast<size_t>(d3dSemantic + 1))
            {
                mCachedInputLayout.resize(d3dSemantic + 1, angle::FormatID::NONE);
            }
            mCachedInputLayout[d3dSemantic] =
                GetVertexFormatID(vertexAttributes[locationIndex],
                                  state.getVertexAttribCurrentValue(locationIndex).Type);
        }
    }

    D3DVertexExecutable::getSignature(renderer, mCachedInputLayout, &mCachedVertexSignature);

    updateCachedVertexExecutableIndex();
}

void ProgramExecutableD3D::updateCachedOutputLayout(const gl::Context *context,
                                                    const gl::Framebuffer *framebuffer)
{
    mPixelShaderOutputLayoutCache.clear();

    FramebufferD3D *fboD3D   = GetImplAs<FramebufferD3D>(framebuffer);
    const auto &colorbuffers = fboD3D->getColorAttachmentsForRender(context);

    for (const gl::FramebufferAttachment *colorbuffer : colorbuffers)
    {
        if (colorbuffer)
        {
            auto binding    = colorbuffer->getBinding() == GL_BACK ? GL_COLOR_ATTACHMENT0
                                                                   : colorbuffer->getBinding();
            size_t maxIndex = binding != GL_NONE ? GetMaxOutputIndex(mPixelShaderKey,
                                                                     binding - GL_COLOR_ATTACHMENT0)
                                                 : 0;
            mPixelShaderOutputLayoutCache.insert(mPixelShaderOutputLayoutCache.end(), maxIndex + 1,
                                                 binding);
        }
        else
        {
            mPixelShaderOutputLayoutCache.push_back(GL_NONE);
        }
    }

    updateCachedPixelExecutableIndex();
}

void ProgramExecutableD3D::updateCachedImage2DBindLayout(const gl::Context *context,
                                                         const gl::ShaderType shaderType)
{
    const auto &glState = context->getState();
    for (auto &image2DBindLayout : mImage2DBindLayoutCache[shaderType])
    {
        const gl::ImageUnit &imageUnit = glState.getImageUnit(image2DBindLayout.first);
        if (imageUnit.texture.get())
        {
            image2DBindLayout.second = imageUnit.texture->getType();
        }
        else
        {
            image2DBindLayout.second = gl::TextureType::_2D;
        }
    }

    switch (shaderType)
    {
        case gl::ShaderType::Vertex:
            updateCachedVertexExecutableIndex();
            break;
        case gl::ShaderType::Fragment:
            updateCachedPixelExecutableIndex();
            break;
        default:
            ASSERT(false);
            break;
    }
}

void ProgramExecutableD3D::updateCachedVertexExecutableIndex()
{
    mCachedVertexExecutableIndex.reset();
    for (size_t executableIndex = 0; executableIndex < mVertexExecutables.size(); executableIndex++)
    {
        if (mVertexExecutables[executableIndex]->matchesSignature(mCachedVertexSignature))
        {
            mCachedVertexExecutableIndex = executableIndex;
            break;
        }
    }
}

void ProgramExecutableD3D::updateCachedPixelExecutableIndex()
{
    mCachedPixelExecutableIndex.reset();
    for (size_t executableIndex = 0; executableIndex < mPixelExecutables.size(); executableIndex++)
    {
        if (mPixelExecutables[executableIndex]->matchesSignature(
                mPixelShaderOutputLayoutCache, mImage2DBindLayoutCache[gl::ShaderType::Fragment]))
        {
            mCachedPixelExecutableIndex = executableIndex;
            break;
        }
    }
}

void ProgramExecutableD3D::updateUniformBufferCache(const gl::Caps &caps)
{
    if (mExecutable->getUniformBlocks().empty())
    {
        return;
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        mShaderUBOCaches[shaderType].clear();
        mShaderUBOCachesUseSB[shaderType].clear();
    }

    for (unsigned int uniformBlockIndex = 0; uniformBlockIndex < mD3DUniformBlocks.size();
         uniformBlockIndex++)
    {
        const D3DUniformBlock &uniformBlock = mD3DUniformBlocks[uniformBlockIndex];
        GLuint blockBinding = mExecutable->getUniformBlockBinding(uniformBlockIndex);

        // Unnecessary to apply an unreferenced standard or shared UBO
        for (gl::ShaderType shaderType : gl::AllShaderTypes())
        {
            if (!uniformBlock.activeInShader(shaderType))
            {
                continue;
            }

            bool useStructuredBuffer   = uniformBlock.mUseStructuredBuffers[shaderType];
            unsigned int registerIndex = uniformBlock.mShaderRegisterIndexes[shaderType];
            if (useStructuredBuffer)
            {
                D3DUBOCacheUseSB cacheUseSB;
                cacheUseSB.registerIndex       = registerIndex;
                cacheUseSB.binding             = blockBinding;
                cacheUseSB.byteWidth           = uniformBlock.mByteWidths[shaderType];
                cacheUseSB.structureByteStride = uniformBlock.mStructureByteStrides[shaderType];
                mShaderUBOCachesUseSB[shaderType].push_back(cacheUseSB);
            }
            else
            {
                ASSERT(registerIndex <
                       static_cast<unsigned int>(caps.maxShaderUniformBlocks[shaderType]));
                D3DUBOCache cache;
                cache.registerIndex = registerIndex;
                cache.binding       = blockBinding;
                mShaderUBOCaches[shaderType].push_back(cache);
            }
        }
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        GLuint uniformBlockCount = static_cast<GLuint>(mShaderUBOCaches[shaderType].size() +
                                                       mShaderUBOCachesUseSB[shaderType].size());
        ASSERT(uniformBlockCount <=
               static_cast<unsigned int>(caps.maxShaderUniformBlocks[shaderType]));
    }
}

void ProgramExecutableD3D::defineUniformsAndAssignRegisters(
    RendererD3D *renderer,
    const gl::ShaderMap<gl::SharedCompiledShaderState> &shaders)
{
    D3DUniformMap uniformMap;

    gl::ShaderBitSet attachedShaders;
    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        const gl::SharedCompiledShaderState &shader = shaders[shaderType];
        if (shader)
        {
            for (const sh::ShaderVariable &uniform : shader->uniforms)
            {
                if (uniform.active)
                {
                    defineUniformBase(shader->shaderType, uniform, &uniformMap);
                }
            }

            attachedShaders.set(shader->shaderType);
        }
    }

    // Initialize the D3DUniform list to mirror the indexing of the GL layer.
    for (GLuint index = 0; index < static_cast<GLuint>(mExecutable->getUniforms().size()); index++)
    {
        const gl::LinkedUniform &glUniform = mExecutable->getUniforms()[index];
        if (!glUniform.isInDefaultBlock())
        {
            continue;
        }

        std::string name = mExecutable->getUniformNames()[index];
        if (glUniform.isArray())
        {
            // In the program state, array uniform names include [0] as in the program resource
            // spec. Here we don't include it.
            // TODO(oetuaho@nvidia.com): consider using the same uniform naming here as in the GL
            // layer.
            ASSERT(angle::EndsWith(name, "[0]"));
            name.resize(name.length() - 3);
        }
        auto mapEntry = uniformMap.find(name);
        ASSERT(mapEntry != uniformMap.end());
        mD3DUniforms.push_back(mapEntry->second);
    }

    assignAllSamplerRegisters(shaders);
    // Samplers and readonly images share shader input resource slot, adjust low value of
    // readonly image range.
    {
        const gl::ShaderType shaderType = gl::ShaderType::Fragment;
        mUsedReadonlyImageRange[shaderType] =
            gl::RangeUI(mUsedShaderSamplerRanges[shaderType].high(),
                        mUsedShaderSamplerRanges[shaderType].high());
    }
    assignAllImageRegisters();
    initializeUniformStorage(renderer, attachedShaders);
}

void ProgramExecutableD3D::defineUniformBase(gl::ShaderType shaderType,
                                             const sh::ShaderVariable &uniform,
                                             D3DUniformMap *uniformMap)
{
    sh::StubBlockEncoder stubEncoder;

    // Samplers get their registers assigned in assignAllSamplerRegisters, and images get their
    // registers assigned in assignAllImageRegisters.
    if (gl::IsSamplerType(uniform.type))
    {
        UniformEncodingVisitorD3D visitor(shaderType, HLSLRegisterType::Texture, &stubEncoder,
                                          uniformMap);
        sh::TraverseShaderVariable(uniform, false, &visitor);
        return;
    }

    if (gl::IsImageType(uniform.type))
    {
        if (uniform.readonly)
        {
            UniformEncodingVisitorD3D visitor(shaderType, HLSLRegisterType::Texture, &stubEncoder,
                                              uniformMap);
            sh::TraverseShaderVariable(uniform, false, &visitor);
        }
        else
        {
            UniformEncodingVisitorD3D visitor(shaderType, HLSLRegisterType::UnorderedAccessView,
                                              &stubEncoder, uniformMap);
            sh::TraverseShaderVariable(uniform, false, &visitor);
        }
        mImageBindingMap[uniform.name] = uniform.binding;
        return;
    }

    const SharedCompiledShaderStateD3D &shaderD3D = mAttachedShaders[shaderType];

    bool skipUniformEncoding =
        // Uniform is a real builtin in HLSL
        (uniform.isBuiltIn() && !uniform.isEmulatedBuiltIn()) ||
        // Uniform ended up as unreferenced during HLSL generation.
        !shaderD3D->hasUniform(uniform.name);

    if (skipUniformEncoding)
    {
        UniformEncodingVisitorD3D visitor(shaderType, HLSLRegisterType::None, &stubEncoder,
                                          uniformMap);
        sh::TraverseShaderVariable(uniform, false, &visitor);
        return;
    }

    unsigned int startRegister = shaderD3D->getUniformRegister(uniform.name);
    sh::HLSLBlockEncoder encoder(true);
    encoder.skipRegisters(startRegister);

    UniformEncodingVisitorD3D visitor(shaderType, HLSLRegisterType::None, &encoder, uniformMap);
    sh::TraverseShaderVariable(uniform, false, &visitor);
}

void ProgramExecutableD3D::assignAllSamplerRegisters(
    const gl::ShaderMap<gl::SharedCompiledShaderState> &shaders)
{
    for (size_t uniformIndex = 0; uniformIndex < mD3DUniforms.size(); ++uniformIndex)
    {
        if (mD3DUniforms[uniformIndex]->isSampler())
        {
            assignSamplerRegisters(shaders, uniformIndex);
        }
    }
}

void ProgramExecutableD3D::assignSamplerRegisters(
    const gl::ShaderMap<gl::SharedCompiledShaderState> &shaders,
    size_t uniformIndex)
{
    D3DUniform *d3dUniform = mD3DUniforms[uniformIndex];
    ASSERT(d3dUniform->isSampler());
    std::string translatedName             = gl::ParseResourceName(d3dUniform->name, nullptr);
    const gl::LinkedUniform &linkedUniform = mExecutable->getUniforms()[uniformIndex];
    const bool isSamplerInStruct           = translatedName.find('.') != std::string::npos;

    // Arrays of arrays have one D3DUniform per inner array, while the shader stores register
    // information for the outermost array.
    unsigned int registerOffset =
        linkedUniform.pod.parentArrayIndex * d3dUniform->getArraySizeProduct();
    // Extracted sampler arrays combine the elements of arrays of structs.
    if (isSamplerInStruct)
    {
        registerOffset += linkedUniform.getOuterArrayOffset();
        // Extracted samplers use the original uniform path without array indices.
        translatedName = RemoveArraySubscripts(translatedName);
    }

    bool hasUniform = false;
    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        if (!shaders[shaderType])
        {
            continue;
        }

        const SharedCompiledShaderStateD3D &shaderD3D = mAttachedShaders[shaderType];
        if (shaderD3D->hasUniform(translatedName))
        {
            d3dUniform->mShaderRegisterIndexes[shaderType] =
                shaderD3D->getUniformRegister(translatedName) + registerOffset;
            ASSERT(d3dUniform->mShaderRegisterIndexes[shaderType] != GL_INVALID_VALUE);

            AssignSamplers(d3dUniform->mShaderRegisterIndexes[shaderType], d3dUniform->typeInfo,
                           d3dUniform->getArraySizeProduct(), mShaderSamplers[shaderType],
                           &mUsedShaderSamplerRanges[shaderType]);
            hasUniform = true;
        }
    }

    ASSERT(hasUniform);
}

// static
void ProgramExecutableD3D::AssignSamplers(unsigned int startSamplerIndex,
                                          const gl::UniformTypeInfo &typeInfo,
                                          unsigned int samplerCount,
                                          std::vector<D3DSampler> &outSamplers,
                                          gl::RangeUI *outUsedRange)
{
    unsigned int samplerIndex = startSamplerIndex;

    do
    {
        ASSERT(samplerIndex < outSamplers.size());
        D3DSampler *sampler         = &outSamplers[samplerIndex];
        sampler->active             = true;
        sampler->textureType        = gl::FromGLenum<gl::TextureType>(typeInfo.textureType);
        sampler->logicalTextureUnit = 0;
        outUsedRange->extend(samplerIndex);
        samplerIndex++;
    } while (samplerIndex < startSamplerIndex + samplerCount);
}

void ProgramExecutableD3D::assignAllImageRegisters()
{
    for (size_t uniformIndex = 0; uniformIndex < mD3DUniforms.size(); ++uniformIndex)
    {
        if (mD3DUniforms[uniformIndex]->isImage() && !mD3DUniforms[uniformIndex]->isImage2D())
        {
            assignImageRegisters(uniformIndex);
        }
    }
}

void ProgramExecutableD3D::assignImageRegisters(size_t uniformIndex)
{
    // TODO(xinghua.cao@intel.com): Implement image variables in vertex shader and pixel shader.
    UNIMPLEMENTED();
}

// static
void ProgramExecutableD3D::AssignImages(unsigned int startImageIndex,
                                        int startLogicalImageUnit,
                                        unsigned int imageCount,
                                        std::vector<D3DImage> &outImages,
                                        gl::RangeUI *outUsedRange)
{
    unsigned int imageIndex = startImageIndex;

    // If declare without a binding qualifier, any uniform image variable (include all elements of
    // unbound image array) shoud be bound to unit zero.
    if (startLogicalImageUnit == -1)
    {
        ASSERT(imageIndex < outImages.size());
        D3DImage *image         = &outImages[imageIndex];
        image->active           = true;
        image->logicalImageUnit = 0;
        outUsedRange->extend(imageIndex);
        return;
    }

    unsigned int logcalImageUnit = startLogicalImageUnit;
    do
    {
        ASSERT(imageIndex < outImages.size());
        D3DImage *image         = &outImages[imageIndex];
        image->active           = true;
        image->logicalImageUnit = logcalImageUnit;
        outUsedRange->extend(imageIndex);
        imageIndex++;
        logcalImageUnit++;
    } while (imageIndex < startImageIndex + imageCount);
}

void ProgramExecutableD3D::assignImage2DRegisters(gl::ShaderType shaderType,
                                                  unsigned int startImageIndex,
                                                  int startLogicalImageUnit,
                                                  bool readonly)
{
    if (readonly)
    {
        AssignImages(startImageIndex, startLogicalImageUnit, 1, mReadonlyImages[shaderType],
                     &mUsedReadonlyImageRange[shaderType]);
    }
    else
    {
        AssignImages(startImageIndex, startLogicalImageUnit, 1, mImages[shaderType],
                     &mUsedImageRange[shaderType]);
    }
}

void ProgramExecutableD3D::gatherTransformFeedbackVaryings(
    const gl::VaryingPacking &varyingPacking,
    const std::vector<std::string> &tfVaryingNames,
    const BuiltinInfo &builtins)
{
    const std::string varyingSemantic = "TEXCOORD";

    // Gather the linked varyings that are used for transform feedback, they should all exist.
    mStreamOutVaryings.clear();

    for (unsigned int outputSlot = 0; outputSlot < static_cast<unsigned int>(tfVaryingNames.size());
         ++outputSlot)
    {
        const auto &tfVaryingName = tfVaryingNames[outputSlot];
        if (tfVaryingName == "gl_Position")
        {
            if (builtins.glPosition.enabled)
            {
                mStreamOutVaryings.emplace_back(builtins.glPosition.semantic,
                                                builtins.glPosition.indexOrSize, 4, outputSlot);
            }
        }
        else if (tfVaryingName == "gl_FragCoord")
        {
            if (builtins.glFragCoord.enabled)
            {
                mStreamOutVaryings.emplace_back(builtins.glFragCoord.semantic,
                                                builtins.glFragCoord.indexOrSize, 4, outputSlot);
            }
        }
        else if (tfVaryingName == "gl_PointSize")
        {
            if (builtins.glPointSize.enabled)
            {
                mStreamOutVaryings.emplace_back("PSIZE", 0, 1, outputSlot);
            }
        }
        else
        {
            const auto &registerInfos = varyingPacking.getRegisterList();
            for (GLuint registerIndex = 0u; registerIndex < registerInfos.size(); ++registerIndex)
            {
                const auto &registerInfo = registerInfos[registerIndex];
                const auto &varying      = registerInfo.packedVarying->varying();
                GLenum transposedType    = gl::TransposeMatrixType(varying.type);
                int componentCount       = gl::VariableColumnCount(transposedType);
                ASSERT(!varying.isBuiltIn() && !varying.isStruct());

                // There can be more than one register assigned to a particular varying, and each
                // register needs its own stream out entry.
                if (registerInfo.tfVaryingName() == tfVaryingName)
                {
                    mStreamOutVaryings.emplace_back(varyingSemantic, registerIndex, componentCount,
                                                    outputSlot);
                }
            }
        }
    }
}

D3DUniform *ProgramExecutableD3D::getD3DUniformFromLocation(
    const gl::VariableLocation &locationInfo)
{
    return mD3DUniforms[locationInfo.index];
}

const D3DUniform *ProgramExecutableD3D::getD3DUniformFromLocation(
    const gl::VariableLocation &locationInfo) const
{
    return mD3DUniforms[locationInfo.index];
}

unsigned int ProgramExecutableD3D::issueSerial()
{
    return mCurrentSerial++;
}

void ProgramExecutableD3D::setUniform1fv(GLint location, GLsizei count, const GLfloat *v)
{
    setUniformInternal(location, count, v, GL_FLOAT);
}

void ProgramExecutableD3D::setUniform2fv(GLint location, GLsizei count, const GLfloat *v)
{
    setUniformInternal(location, count, v, GL_FLOAT_VEC2);
}

void ProgramExecutableD3D::setUniform3fv(GLint location, GLsizei count, const GLfloat *v)
{
    setUniformInternal(location, count, v, GL_FLOAT_VEC3);
}

void ProgramExecutableD3D::setUniform4fv(GLint location, GLsizei count, const GLfloat *v)
{
    setUniformInternal(location, count, v, GL_FLOAT_VEC4);
}

void ProgramExecutableD3D::setUniformMatrix2fv(GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat *value)
{
    setUniformMatrixfvInternal<2, 2>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix3fv(GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat *value)
{
    setUniformMatrixfvInternal<3, 3>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix4fv(GLint location,
                                               GLsizei count,
                                               GLboolean transpose,
                                               const GLfloat *value)
{
    setUniformMatrixfvInternal<4, 4>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix2x3fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<2, 3>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix3x2fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<3, 2>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix2x4fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<2, 4>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix4x2fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<4, 2>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix3x4fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<3, 4>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniformMatrix4x3fv(GLint location,
                                                 GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
    setUniformMatrixfvInternal<4, 3>(location, count, transpose, value);
}

void ProgramExecutableD3D::setUniform1iv(GLint location, GLsizei count, const GLint *v)
{
    setUniformInternal(location, count, v, GL_INT);
}

void ProgramExecutableD3D::setUniform2iv(GLint location, GLsizei count, const GLint *v)
{
    setUniformInternal(location, count, v, GL_INT_VEC2);
}

void ProgramExecutableD3D::setUniform3iv(GLint location, GLsizei count, const GLint *v)
{
    setUniformInternal(location, count, v, GL_INT_VEC3);
}

void ProgramExecutableD3D::setUniform4iv(GLint location, GLsizei count, const GLint *v)
{
    setUniformInternal(location, count, v, GL_INT_VEC4);
}

void ProgramExecutableD3D::setUniform1uiv(GLint location, GLsizei count, const GLuint *v)
{
    setUniformInternal(location, count, v, GL_UNSIGNED_INT);
}

void ProgramExecutableD3D::setUniform2uiv(GLint location, GLsizei count, const GLuint *v)
{
    setUniformInternal(location, count, v, GL_UNSIGNED_INT_VEC2);
}

void ProgramExecutableD3D::setUniform3uiv(GLint location, GLsizei count, const GLuint *v)
{
    setUniformInternal(location, count, v, GL_UNSIGNED_INT_VEC3);
}

void ProgramExecutableD3D::setUniform4uiv(GLint location, GLsizei count, const GLuint *v)
{
    setUniformInternal(location, count, v, GL_UNSIGNED_INT_VEC4);
}

// Assume count is already clamped.
template <typename T>
void ProgramExecutableD3D::setUniformImpl(D3DUniform *targetUniform,
                                          const gl::VariableLocation &locationInfo,
                                          GLsizei count,
                                          const T *v,
                                          uint8_t *targetState,
                                          GLenum uniformType)
{
    const int components                  = targetUniform->typeInfo.componentCount;
    const unsigned int arrayElementOffset = locationInfo.arrayIndex;
    const int blockSize                   = 4;

    if (targetUniform->typeInfo.type == uniformType)
    {
        T *dest =
            ANGLE_UNSAFE_TODO(reinterpret_cast<T *>(targetState) + arrayElementOffset * blockSize);
        const T *source = v;

        // If the component is equal to the block size, we can optimize to a single memcpy.
        // Otherwise, we have to do partial block writes.
        if (components == blockSize)
        {
            ANGLE_UNSAFE_TODO(memcpy(dest, source, components * count * sizeof(T)));
        }
        else
        {
            for (GLint i = 0; i < count;
                 ANGLE_UNSAFE_TODO((i++, dest += blockSize, source += components)))
            {
                ANGLE_UNSAFE_TODO(memcpy(dest, source, components * sizeof(T)));
            }
        }
    }
    else
    {
        ASSERT(targetUniform->typeInfo.type == gl::VariableBoolVectorType(uniformType));
        GLint *boolParams =
            ANGLE_UNSAFE_TODO(reinterpret_cast<GLint *>(targetState) + arrayElementOffset * 4);

        for (GLint i = 0; i < count; i++)
        {
            GLint *dest     = ANGLE_UNSAFE_TODO(boolParams + (i * 4));
            const T *source = ANGLE_UNSAFE_TODO(v + (i * components));

            for (int c = 0; c < components; c++)
            {
                ANGLE_UNSAFE_TODO(dest[c]) =
                    (ANGLE_UNSAFE_TODO(source[c]) == static_cast<T>(0)) ? GL_FALSE : GL_TRUE;
            }
        }
    }
}

template <typename T>
void ProgramExecutableD3D::setUniformInternal(GLint location,
                                              GLsizei count,
                                              const T *v,
                                              GLenum uniformType)
{
    const gl::VariableLocation &locationInfo = mExecutable->getUniformLocations()[location];
    D3DUniform *targetUniform                = mD3DUniforms[locationInfo.index];

    if (targetUniform->typeInfo.isSampler)
    {
        ASSERT(uniformType == GL_INT);
        size_t size = count * sizeof(T);
        GLint *dest = &targetUniform->mSamplerData[locationInfo.arrayIndex];
        if (ANGLE_UNSAFE_TODO(memcmp(dest, v, size)) != 0)
        {
            ANGLE_UNSAFE_TODO(memcpy(dest, v, size));
            mDirtySamplerMapping = true;
        }
        return;
    }

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        uint8_t *targetState = targetUniform->mShaderData[shaderType];
        if (targetState)
        {
            setUniformImpl(targetUniform, locationInfo, count, v, targetState, uniformType);
            mShaderUniformsDirty.set(shaderType);
        }
    }
}

template <int cols, int rows>
void ProgramExecutableD3D::setUniformMatrixfvInternal(GLint location,
                                                      GLsizei countIn,
                                                      GLboolean transpose,
                                                      const GLfloat *value)
{
    const gl::VariableLocation &uniformLocation = mExecutable->getUniformLocations()[location];
    D3DUniform *targetUniform                   = getD3DUniformFromLocation(uniformLocation);
    unsigned int arrayElementOffset             = uniformLocation.arrayIndex;
    unsigned int elementCount                   = targetUniform->getArraySizeProduct();

    for (gl::ShaderType shaderType : gl::AllShaderTypes())
    {
        if (targetUniform->mShaderData[shaderType])
        {
            SetFloatUniformMatrixHLSL<cols, rows>::Run(
                arrayElementOffset, elementCount, countIn, transpose, value,
                targetUniform->mShaderData[shaderType], false);
            mShaderUniformsDirty.set(shaderType);
        }
    }
}

template <typename DestT>
void ProgramExecutableD3D::getUniformInternal(GLint location, DestT *dataOut) const
{
    const gl::VariableLocation &locationInfo = mExecutable->getUniformLocations()[location];
    const gl::LinkedUniform &uniform         = mExecutable->getUniforms()[locationInfo.index];

    const D3DUniform *targetUniform = getD3DUniformFromLocation(locationInfo);
    const uint8_t *srcPointer       = targetUniform->getDataPtrToElement(locationInfo.arrayIndex);

    if (gl::IsMatrixType(uniform.getType()))
    {
        GetMatrixUniform(uniform.getType(), dataOut, reinterpret_cast<const DestT *>(srcPointer),
                         true, false);
    }
    else
    {
        ANGLE_UNSAFE_TODO(memcpy(dataOut, srcPointer, uniform.getElementSize()));
    }
}

void ProgramExecutableD3D::getUniformfv(const gl::Context *context,
                                        GLint location,
                                        GLfloat *params) const
{
    getUniformInternal(location, params);
}

void ProgramExecutableD3D::getUniformiv(const gl::Context *context,
                                        GLint location,
                                        GLint *params) const
{
    getUniformInternal(location, params);
}

void ProgramExecutableD3D::getUniformuiv(const gl::Context *context,
                                         GLint location,
                                         GLuint *params) const
{
    getUniformInternal(location, params);
}

}  // namespace rx
