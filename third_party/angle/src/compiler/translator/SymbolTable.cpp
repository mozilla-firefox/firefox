//
// Copyright 2002 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Symbol table for parsing. The design principles and most of the functionality are documented in
// the header file.
//

#if defined(_MSC_VER)
#    pragma warning(disable : 4718)
#endif

#include "compiler/translator/SymbolTable.h"
#include "common/unsafe_buffers.h"

#include "angle_gl.h"
#include "compiler/translator/ImmutableString.h"
#include "compiler/translator/IntermNode.h"
#include "compiler/translator/StaticType.h"
#include "compiler/translator/util.h"

namespace sh
{
namespace
{
bool CheckShaderType(Shader expected, GLenum actual)
{
    switch (expected)
    {
        case Shader::ALL:
            return true;
        case Shader::FRAGMENT:
            return actual == GL_FRAGMENT_SHADER;
        case Shader::VERTEX:
            return actual == GL_VERTEX_SHADER;
        case Shader::COMPUTE:
            return actual == GL_COMPUTE_SHADER;
        case Shader::GEOMETRY:
            return actual == GL_GEOMETRY_SHADER;
        case Shader::GEOMETRY_EXT:
            return actual == GL_GEOMETRY_SHADER_EXT;
        case Shader::TESS_CONTROL_EXT:
            return actual == GL_TESS_CONTROL_SHADER_EXT;
        case Shader::TESS_EVALUATION_EXT:
            return actual == GL_TESS_EVALUATION_SHADER_EXT;
        case Shader::NOT_COMPUTE:
            return actual != GL_COMPUTE_SHADER;
        default:
            UNREACHABLE();
            return false;
    }
}

bool CheckExtension(uint32_t extensionIndex, const ShBuiltInResources &resources)
{
    const int *resourcePtr = reinterpret_cast<const int *>(&resources);
    return ANGLE_UNSAFE_TODO(resourcePtr[extensionIndex]) > 0;
}
}  // namespace

class TSymbolTable::TSymbolTableLevel
{
  public:
    TSymbolTableLevel() = default;

    bool insert(TSymbol *symbol);

#ifdef ANGLE_IR
    void redeclare(TSymbol *symbol);
#endif

    // Insert a function using its unmangled name as the key.
    void insertUnmangled(TFunction *function);

    TSymbol *find(const ImmutableString &name) const;

  private:
    using tLevel        = TUnorderedMap<ImmutableString,
                                        TSymbol *,
                                        ImmutableString::FowlerNollVoHash<sizeof(size_t)>>;
    using tLevelPair    = const tLevel::value_type;
    using tInsertResult = std::pair<tLevel::iterator, bool>;

    tLevel level;
};

bool TSymbolTable::TSymbolTableLevel::insert(TSymbol *symbol)
{
    // returning true means symbol was added to the table
    tInsertResult result = level.insert(tLevelPair(symbol->getMangledName(), symbol));
    return result.second;
}

#ifdef ANGLE_IR
void TSymbolTable::TSymbolTableLevel::redeclare(TSymbol *symbol)
{
    // returning true means symbol was added to the table
    level.insert_or_assign(symbol->getMangledName(), symbol);
}
#endif

void TSymbolTable::TSymbolTableLevel::insertUnmangled(TFunction *function)
{
    level.insert(tLevelPair(function->name(), function));
}

TSymbol *TSymbolTable::TSymbolTableLevel::find(const ImmutableString &name) const
{
    tLevel::const_iterator it = level.find(name);
    if (it == level.end())
        return nullptr;
    else
        return (*it).second;
}

TSymbolTable::TSymbolTable()
    : mGlobalInvariant(false),
      mUniqueIdCounter(0),
      mShaderType(GL_FRAGMENT_SHADER),
      mShaderSpec(SH_GLES2_SPEC),
      mGlInVariableWithArraySize(nullptr)
{}

TSymbolTable::~TSymbolTable() = default;

bool TSymbolTable::isEmpty() const
{
    return mTable.empty();
}

bool TSymbolTable::atGlobalLevel() const
{
    return mTable.size() == 1u;
}

void TSymbolTable::push()
{
    mTable.emplace_back(new TSymbolTableLevel);
    mPrecisionStack.emplace_back(new PrecisionStackLevel);
}

void TSymbolTable::pop()
{
    mTable.pop_back();
    mPrecisionStack.pop_back();
}

const TFunction *TSymbolTable::markFunctionHasPrototypeDeclaration(
    const ImmutableString &mangledName,
    bool *hadPrototypeDeclarationOut) const
{
    TFunction *function         = findUserDefinedFunction(mangledName);
    *hadPrototypeDeclarationOut = function->hasPrototypeDeclaration();
    function->setHasPrototypeDeclaration();
    return function;
}

const TFunction *TSymbolTable::setFunctionParameterNamesFromDefinition(const TFunction *function,
                                                                       bool *wasDefinedOut) const
{
    TFunction *firstDeclaration = findUserDefinedFunction(function->getMangledName());
    ASSERT(firstDeclaration);
    // Note: 'firstDeclaration' could be 'function' if this is the first time we've seen function as
    // it would have just been put in the symbol table. Otherwise, we're looking up an earlier
    // occurance.
    if (function != firstDeclaration)
    {
        // The previous declaration should have the same parameters as the function definition
        // (parameter names may differ).
        firstDeclaration->shareParameters(*function);
    }

    *wasDefinedOut = firstDeclaration->isDefined();
    firstDeclaration->setDefined();
    return firstDeclaration;
}

bool TSymbolTable::setGlInArraySize(unsigned int inputArraySize, int shaderVersion)
{
    if (mGlInVariableWithArraySize)
    {
        return mGlInVariableWithArraySize->getType().getOutermostArraySize() == inputArraySize;
    }
    // Note: gl_in may be redeclared by the shader.
    const TSymbol *glPerVertexVar = find(ImmutableString("gl_in"), shaderVersion);
    ASSERT(glPerVertexVar);

    TType *glInType = new TType(static_cast<const TVariable *>(glPerVertexVar)->getType());
    glInType->sizeOutermostUnsizedArray(inputArraySize);
    mGlInVariableWithArraySize =
        new TVariable(this, glPerVertexVar->name(), glInType, glPerVertexVar->symbolType(),
                      TExtension::EXT_geometry_shader);
    return true;
}

void TSymbolTable::onGlInVariableRedeclaration(const TVariable *redeclaredGlIn)
{
    // There are 4 possibilities:
    //
    // 1. input primitive layout is set, then gl_in is encountered (not declared)
    // 2. input primitive layout is set, then gl_in is redeclared
    // 3. gl_in is redeclared with a size, then input primitive layout is set
    // 4. gl_in is redeclared without a size, then input primitive layout is set
    //
    // In case 1, setGlInArraySize declares mGlInVariableWithArraySize, but this function is not
    // called.
    //
    // In case 2, setGlInArraySize declares mGlInVariableWithArraySize, but we need to replace it
    // with the shader-declared gl_in (redeclaredGlIn).  The array size of
    // mGlInVariableWithArraySize and redeclaredGlIn should match (validated before the call).
    //
    // In case 3, this function is called when mGlInVariableWithArraySize is nullptr.  We set that
    // to redeclaredGlIn.  Later when the input primitive is encountered, setGlInArraySize verifies
    // that the size matches the expectation.
    //
    // In case 4, similarly this function is called when mGlInVariableWithArraySize is nullptr.
    // That is again set to redeclaredGlIn.  The parser needs to ensure this unsized array is sized
    // before calling setGlInArraySize which verifies the array sizes match.
    //
    // In all cases, basically mGlInVariableWithArraySize should be set to the redeclared variable.

    // If mGlInVariableWithArraySize is set when gl_in is redeclared, it's because gl_in was
    // sized before the redeclaration.  In that case, make sure the redeclared variable is also
    // sized.
    ASSERT(mGlInVariableWithArraySize == nullptr ||
           mGlInVariableWithArraySize->getType().getOutermostArraySize() ==
               redeclaredGlIn->getType().getOutermostArraySize());
    mGlInVariableWithArraySize = redeclaredGlIn;
}

const TVariable *TSymbolTable::getGlInVariableWithArraySize() const
{
    return mGlInVariableWithArraySize;
}

const TVariable *TSymbolTable::gl_FragData() const
{
    return static_cast<const TVariable *>(m_gl_FragData);
}

const TVariable *TSymbolTable::gl_SecondaryFragDataEXT() const
{
    return static_cast<const TVariable *>(m_gl_SecondaryFragDataEXT);
}

bool TSymbolTable::isSecondaryFragDataUsed() const
{
    // Extension variables may not always be initialized (saves some time at symbol table init).
    return gl_SecondaryFragDataEXT() != nullptr && isStaticallyUsed(*gl_SecondaryFragDataEXT());
}

TSymbolTable::VariableMetadata *TSymbolTable::getOrCreateVariableMetadata(const TVariable &variable)
{
    int id    = variable.uniqueId().get();
    auto iter = mVariableMetadata.find(id);
    if (iter == mVariableMetadata.end())
    {
        iter = mVariableMetadata.insert(std::make_pair(id, VariableMetadata())).first;
    }
    return &iter->second;
}

void TSymbolTable::markStaticUse(const TVariable &variable)
{
    auto metadata       = getOrCreateVariableMetadata(variable);
    metadata->staticUse = true;
}

bool TSymbolTable::isStaticallyUsed(const TVariable &variable) const
{
    ASSERT(!variable.getConstPointer());
    int id    = variable.uniqueId().get();
    auto iter = mVariableMetadata.find(id);
    return iter != mVariableMetadata.end() && iter->second.staticUse;
}

void TSymbolTable::addInvariantVarying(const TVariable &variable)
{
    ASSERT(atGlobalLevel());
    auto metadata       = getOrCreateVariableMetadata(variable);
    metadata->invariant = true;
}

bool TSymbolTable::isVaryingInvariant(const TVariable &variable) const
{
    ASSERT(atGlobalLevel());
    if (mGlobalInvariant && (IsShaderOutput(variable.getType().getQualifier())))
    {
        return true;
    }
    int id    = variable.uniqueId().get();
    auto iter = mVariableMetadata.find(id);
    return iter != mVariableMetadata.end() && iter->second.invariant;
}

void TSymbolTable::setGlobalInvariant(bool invariant)
{
    ASSERT(atGlobalLevel());
    mGlobalInvariant = invariant;
}

const TSymbol *TSymbolTable::find(const ImmutableString &name, int shaderVersion) const
{
    const TSymbol *userSymbol = findUserDefined(name);
    if (userSymbol)
    {
        return userSymbol;
    }

    return findBuiltIn(name, shaderVersion);
}

const TSymbol *TSymbolTable::findUserDefined(const ImmutableString &name) const
{
    int userDefinedLevel = static_cast<int>(mTable.size()) - 1;
    while (userDefinedLevel >= 0)
    {
        const TSymbol *symbol = mTable[userDefinedLevel]->find(name);
        if (symbol)
        {
            return symbol;
        }
        userDefinedLevel--;
    }

    return nullptr;
}

TFunction *TSymbolTable::findUserDefinedFunction(const ImmutableString &name) const
{
    // User-defined functions are always declared at the global level.
    ASSERT(!mTable.empty());
    return static_cast<TFunction *>(mTable[0]->find(name));
}

const TSymbol *TSymbolTable::findGlobal(const ImmutableString &name) const
{
    ASSERT(!mTable.empty());
    return mTable[0]->find(name);
}

bool TSymbolTable::declare(TSymbol *symbol)
{
    ASSERT(!mTable.empty());
    // The following built-ins may be redeclared by the shader: gl_ClipDistance, gl_CullDistance,
    // gl_PerVertex, gl_in (EXT_geometry_shader), gl_Position, gl_PointSize
    // (EXT_separate_shader_objects), gl_LastFragData, gl_LastFragColorARM, gl_LastFragDepthARM and
    // gl_LastFragStencilARM.
    ASSERT(symbol->symbolType() == SymbolType::UserDefined ||
           (symbol->symbolType() == SymbolType::BuiltIn && IsRedeclarableBuiltIn(symbol->name())));
    ASSERT(!symbol->isFunction());
    return mTable.back()->insert(symbol);
}

#ifdef ANGLE_IR
void TSymbolTable::redeclare(TSymbol *symbol)
{
    ASSERT(!mTable.empty());
    ASSERT(symbol->symbolType() == SymbolType::UserDefined ||
           (symbol->symbolType() == SymbolType::BuiltIn && IsRedeclarableBuiltIn(symbol->name())));
    ASSERT(!symbol->isFunction());
    mTable.back()->redeclare(symbol);
}

bool TSymbolTable::declareInternal(TSymbol *symbol)
{
    ASSERT(!mTable.empty());
    ASSERT(symbol->symbolType() == SymbolType::AngleInternal);
    ASSERT(!symbol->isFunction());
    return mTable.back()->insert(symbol);
}
#endif

void TSymbolTable::declareUserDefinedFunction(TFunction *function, bool insertUnmangledName)
{
    ASSERT(!mTable.empty());
    if (insertUnmangledName)
    {
        // Insert the unmangled name to detect potential future redefinition as a variable.
        mTable[0]->insertUnmangled(function);
    }
    mTable[0]->insert(function);
}

void TSymbolTable::setDefaultPrecision(TBasicType type, TPrecision prec)
{
    int indexOfLastElement = static_cast<int>(mPrecisionStack.size()) - 1;
    // Uses map operator [], overwrites the current value
    (*mPrecisionStack[indexOfLastElement])[type] = prec;
}

TPrecision TSymbolTable::getDefaultPrecision(TBasicType type) const
{
    if (!SupportsPrecision(type))
        return EbpUndefined;

    // unsigned integers use the same precision as signed
    TBasicType baseType = (type == EbtUInt) ? EbtInt : type;

    int level = static_cast<int>(mPrecisionStack.size()) - 1;
    ASSERT(level >= 0);  // Just to be safe. Should not happen.
    // If we dont find anything we return this. Some types don't have predefined default precision.
    TPrecision prec = EbpUndefined;
    while (level >= 0)
    {
        PrecisionStackLevel::iterator it = mPrecisionStack[level]->find(baseType);
        if (it != mPrecisionStack[level]->end())
        {
            prec = (*it).second;
            break;
        }
        level--;
    }
    return prec;
}

void TSymbolTable::clearCompilationResults()
{
    mGlobalInvariant = false;
    mUniqueIdCounter = kFirstUserDefinedSymbolId;
    mVariableMetadata.clear();
    mGlInVariableWithArraySize = nullptr;

    // User-defined scopes should have already been cleared when the compilation finished.
    ASSERT(mTable.empty());
}

int TSymbolTable::nextUniqueIdValue()
{
    ASSERT(mUniqueIdCounter < std::numeric_limits<int>::max());
    return ++mUniqueIdCounter;
}

void TSymbolTable::initializeBuiltIns(sh::GLenum type,
                                      ShShaderSpec spec,
                                      const ShBuiltInResources &resources)
{
    mShaderType = type;
    mShaderSpec = spec;
    mResources  = resources;

    // We need just one precision stack level for predefined precisions.
    mPrecisionStack.emplace_back(new PrecisionStackLevel);

    switch (type)
    {
        case GL_FRAGMENT_SHADER:
            setDefaultPrecision(EbtInt, EbpMedium);
            break;
        case GL_VERTEX_SHADER:
        case GL_COMPUTE_SHADER:
        case GL_GEOMETRY_SHADER_EXT:
        case GL_TESS_CONTROL_SHADER_EXT:
        case GL_TESS_EVALUATION_SHADER_EXT:
            setDefaultPrecision(EbtInt, EbpHigh);
            setDefaultPrecision(EbtFloat, EbpHigh);
            break;
        default:
            UNREACHABLE();
    }

    // Set defaults for sampler types that have default precision, even those that are
    // only available if an extension exists.
    // New sampler types in ESSL3 don't have default precision. ESSL1 types do.
    initSamplerDefaultPrecision(EbtSampler2D);
    initSamplerDefaultPrecision(EbtSamplerCube);
    // SamplerExternalOES is specified in the extension to have default precision.
    initSamplerDefaultPrecision(EbtSamplerExternalOES);
    // SamplerExternal2DY2YEXT is specified in the extension to have default precision.
    initSamplerDefaultPrecision(EbtSamplerExternal2DY2YEXT);
    // It isn't specified whether Sampler2DRect has default precision.
    initSamplerDefaultPrecision(EbtSampler2DRect);

    if (spec < SH_GLES3_SPEC)
    {
        // Only set the default precision of shadow samplers in ESLL1. They become core in ESSL3
        // where they do not have a defalut precision.
        initSamplerDefaultPrecision(EbtSampler2DShadow);
    }

    setDefaultPrecision(EbtAtomicCounter, EbpHigh);

    initializeBuiltInVariables(type, spec, resources);
    mUniqueIdCounter = kFirstUserDefinedSymbolId;
}

void TSymbolTable::initSamplerDefaultPrecision(TBasicType samplerType)
{
    ASSERT(samplerType >= EbtGuardSamplerBegin && samplerType <= EbtGuardSamplerEnd);
    setDefaultPrecision(samplerType, EbpLow);
}

TSymbolTable::VariableMetadata::VariableMetadata() : staticUse(false), invariant(false) {}

const TSymbol *SymbolRule::get(ShShaderSpec shaderSpec,
                               int shaderVersion,
                               sh::GLenum shaderType,
                               const ShBuiltInResources &resources,
                               const TSymbolTableBase &symbolTable) const
{
    if (mVersion == kESSL1Only && shaderVersion != static_cast<int>(kESSL1Only))
        return nullptr;

    if (mVersion > shaderVersion)
        return nullptr;

    if (!CheckShaderType(static_cast<Shader>(mShaders), shaderType))
        return nullptr;

    if (mExtensionIndex != 0 && !CheckExtension(mExtensionIndex, resources))
        return nullptr;

    return mIsVar > 0 ? symbolTable.*(mSymbolOrVar.var) : mSymbolOrVar.symbol;
}

const TSymbol *FindMangledBuiltIn(ShShaderSpec shaderSpec,
                                  int shaderVersion,
                                  sh::GLenum shaderType,
                                  const ShBuiltInResources &resources,
                                  const TSymbolTableBase &symbolTable,
                                  const SymbolRule *rules,
                                  uint16_t startIndex,
                                  uint16_t endIndex)
{
    for (uint32_t ruleIndex = startIndex; ruleIndex < endIndex; ++ruleIndex)
    {
        const TSymbol *symbol =
            ANGLE_UNSAFE_TODO(rules[ruleIndex])
                .get(shaderSpec, shaderVersion, shaderType, resources, symbolTable);
        if (symbol)
        {
            return symbol;
        }
    }

    return nullptr;
}

bool UnmangledEntry::matches(const ImmutableString &name,
                             ShShaderSpec shaderSpec,
                             int shaderVersion,
                             sh::GLenum shaderType,
                             const TExtensionBehavior &extensions) const
{
    if (name != mName)
        return false;

    if (!CheckShaderType(static_cast<Shader>(mShaderType), shaderType))
        return false;

    if (mESSLVersion == kESSL1Only && shaderVersion != static_cast<int>(kESSL1Only))
        return false;

    if (mESSLVersion > shaderVersion)
        return false;

    bool anyExtension        = false;
    bool anyExtensionEnabled = false;
    for (TExtension ext : mESSLExtensions)
    {
        if (ext != TExtension::UNDEFINED)
        {
            anyExtension        = true;
            anyExtensionEnabled = anyExtensionEnabled || IsExtensionEnabled(extensions, ext);
        }
    }

    if (!anyExtension)
        return true;

    return anyExtensionEnabled;
}
}  // namespace sh
