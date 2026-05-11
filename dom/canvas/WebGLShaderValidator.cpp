/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLShaderValidator.h"

#include <string>
#include <vector>

#include "GLContext.h"
#include "MurmurHash3.h"
#include "WebGLContext.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/gfx/Logging.h"
#include "nsPrintfCString.h"

namespace mozilla {
namespace webgl {

uint64_t IdentifierHashFunc(const char* name, size_t len) {
  // NB: we use the x86 function everywhere, even though it's suboptimal perf
  // on x64.  They return different results; not sure if that's a requirement.
  uint64_t hash[2];
  MurmurHash3_x86_128(name, len, 0, hash);
  return hash[0];
}

static ShCompileOptions ChooseValidatorCompileOptions(
    const ShBuiltInResources& resources, const mozilla::gl::GLContext* gl) {
  ShCompileOptions options = {};
  options.variables = true;
  options.enforcePackingRestrictions = true;
  options.objectCode = true;
  options.initGLPosition = true;
  options.initializeUninitializedLocals = true;
  options.initOutputVariables = true;
  options.clampIndirectArrayBounds = true;

  if (kIsMacOS) {
    options.removeInvariantAndCentroidForESSL3 = true;
  }

  if (gl->WorkAroundDriverBugs()) {
    if (kIsMacOS) {
      // Work around https://bugs.webkit.org/show_bug.cgi?id=124684,
      // https://chromium.googlesource.com/angle/angle/+/5e70cf9d0b1bb
      options.unfoldShortCircuit = true;

      // Work around that Mac drivers handle struct scopes incorrectly.
      options.regenerateStructNames = true;
      options.initOutputVariables = true;
      options.initGLPointSize = true;

      if (gl->Vendor() == gl::GLVendor::Intel) {
        // Work around that Intel drivers on Mac OSX handle for-loop
        // incorrectly.
        options.addAndTrueToLoopCondition = true;

        options.rewriteTexelFetchOffsetToTexelFetch = true;
      }
    }

    if (!gl->IsANGLE() && gl->Vendor() == gl::GLVendor::Intel) {
      // Failures on at least Windows+Intel+OGL on:
      // conformance/glsl/constructors/glsl-construct-mat2.html
      options.scalarizeVecAndMatConstructorArgs = true;
    }
  }

  // -

  if (resources.MaxExpressionComplexity > 0) {
    options.limitExpressionComplexity = true;
  }
  if (resources.MaxCallStackDepth > 0) {
    options.limitCallStackDepth = true;
  }

  return options;
}

}  // namespace webgl

////////////////////////////////////////

static ShShaderOutput ShaderOutput(gl::GLContext* gl) {
  if (gl->IsGLES()) {
    return SH_ESSL_OUTPUT;
  }
  uint32_t version = gl->ShadingLanguageVersion();
  switch (version) {
    case 100:
      return SH_GLSL_COMPATIBILITY_OUTPUT;
    case 120:
      return SH_GLSL_COMPATIBILITY_OUTPUT;
    case 130:
      return SH_GLSL_130_OUTPUT;
    case 140:
      return SH_GLSL_140_OUTPUT;
    case 150:
      return SH_GLSL_150_CORE_OUTPUT;
    case 330:
      return SH_GLSL_330_CORE_OUTPUT;
    case 400:
      return SH_GLSL_400_CORE_OUTPUT;
    case 410:
      return SH_GLSL_410_CORE_OUTPUT;
    case 420:
      return SH_GLSL_420_CORE_OUTPUT;
    case 430:
      return SH_GLSL_430_CORE_OUTPUT;
    case 440:
      return SH_GLSL_440_CORE_OUTPUT;
    default:
      if (version >= 450) {
        // "OpenGL 4.6 is also guaranteed to support all previous versions of
        // the OpenGL Shading Language back to version 1.10."
        return SH_GLSL_450_CORE_OUTPUT;
      }
      gfxCriticalNote << "Unexpected GLSL version: " << version;
  }

  return SH_GLSL_COMPATIBILITY_OUTPUT;
}

std::unique_ptr<webgl::ShaderValidator> WebGLContext::CreateShaderValidator(
    GLenum shaderType) const {
  const auto spec = (IsWebGL2() ? SH_WEBGL2_SPEC : SH_WEBGL_SPEC);
  const auto outputLanguage = ShaderOutput(gl);

  ShBuiltInResources resources;
  sh::InitBuiltInResources(&resources);

  resources.HashFunction = webgl::IdentifierHashFunc;

  const auto& limits = Limits();

  resources.MaxVertexAttribs = limits.maxVertexAttribs;
  resources.MaxVertexUniformVectors = mGLMaxVertexUniformVectors;
  resources.MaxVertexTextureImageUnits = mGLMaxVertexTextureImageUnits;
  resources.MaxCombinedTextureImageUnits = limits.maxTexUnits;
  resources.MaxTextureImageUnits = mGLMaxFragmentTextureImageUnits;
  resources.MaxFragmentUniformVectors = mGLMaxFragmentUniformVectors;

  resources.MaxVertexOutputVectors = mGLMaxVertexOutputVectors;
  resources.MaxFragmentInputVectors = mGLMaxFragmentInputVectors;
  resources.MaxVaryingVectors = mGLMaxFragmentInputVectors;

  if (IsWebGL2()) {
    resources.MinProgramTexelOffset = mGLMinProgramTexelOffset;
    resources.MaxProgramTexelOffset = mGLMaxProgramTexelOffset;
  }

  resources.MaxDrawBuffers = MaxValidDrawBuffers();

  if (IsExtensionEnabled(WebGLExtensionID::EXT_frag_depth))
    resources.EXT_frag_depth = 1;

  if (IsExtensionEnabled(WebGLExtensionID::OES_standard_derivatives))
    resources.OES_standard_derivatives = 1;

  if (IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers))
    resources.EXT_draw_buffers = 1;

  if (IsExtensionEnabled(WebGLExtensionID::EXT_shader_texture_lod))
    resources.EXT_shader_texture_lod = 1;

  if (IsExtensionEnabled(WebGLExtensionID::OVR_multiview2)) {
    resources.OVR_multiview = 1;
    resources.OVR_multiview2 = 1;
    resources.MaxViewsOVR = limits.maxMultiviewLayers;
  }

  // Tell ANGLE to allow highp in frag shaders. (unless disabled)
  // If underlying GLES doesn't have highp in frag shaders, it should complain
  // anyways.
  resources.FragmentPrecisionHigh = mDisableFragHighP ? 0 : 1;

  if (gl->WorkAroundDriverBugs()) {
#ifdef XP_MACOSX
    if (gl->Vendor() == gl::GLVendor::NVIDIA) {
      // Work around bug 890432
      resources.MaxExpressionComplexity = 1000;
    }
#endif
  }

  // -

  resources.MaxVariableSizeInBytes = [&]() -> size_t {
    const auto kibytes = StaticPrefs::webgl_glsl_max_var_size_in_kibytes();
    if (kibytes >= 0) {
      return static_cast<size_t>(kibytes) * 1024;
    }

    return resources.MaxVariableSizeInBytes;
  }();

  resources.MaxPrivateVariableSizeInBytes = [&]() -> size_t {
    const auto bytes = StaticPrefs::webgl_glsl_max_private_var_size_in_bytes();
    if (bytes >= 0) {
      return static_cast<size_t>(bytes);
    }

    if (kIsMacOS) {
      return 128 * 1024;  // 8k vec4s
    }

    return resources.MaxPrivateVariableSizeInBytes;
  }();

  // -

  const auto compileOptions =
      webgl::ChooseValidatorCompileOptions(resources, gl);
  auto ret = webgl::ShaderValidator::Create(shaderType, spec, outputLanguage,
                                            resources, compileOptions);
  if (!ret) return ret;

  ret->mIfNeeded_webgl_gl_VertexID_Offset |=
      mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst;

  return ret;
}

////////////////////////////////////////

namespace webgl {

/*static*/
std::unique_ptr<ShaderValidator> ShaderValidator::Create(
    GLenum shaderType, ShShaderSpec spec, ShShaderOutput outputLanguage,
    const ShBuiltInResources& resources, ShCompileOptions compileOptions) {
  ShHandle handle =
      sh::ConstructCompiler(shaderType, spec, outputLanguage, &resources);
  MOZ_RELEASE_ASSERT(handle);
  if (!handle) return nullptr;

  return std::unique_ptr<ShaderValidator>(
      new ShaderValidator(handle, compileOptions, resources.MaxVaryingVectors));
}

ShaderValidator::~ShaderValidator() { sh::Destruct(mHandle); }

inline bool StartsWith(const std::string_view& str,
                       const std::string_view& part) {
  return str.find(part) == 0;
}

inline std::vector<std::string_view> Split(std::string_view src,
                                           const std::string_view& delim,
                                           const size_t maxSplits = -1) {
  std::vector<std::string_view> ret;
  for (const auto i : IntegerRange(maxSplits)) {
    (void)i;
    const auto end = src.find(delim);
    if (end == size_t(-1)) {
      break;
    }
    ret.push_back(src.substr(0, end));
    src = src.substr(end + delim.size());
  }
  ret.push_back(src);
  return ret;
}

std::unique_ptr<const ShaderValidatorResults>
ShaderValidator::ValidateAndTranslate(const char* const source) const {
  auto ret = std::make_unique<ShaderValidatorResults>();

  const std::array<const char*, 1> parts = {source};
  ret->mValid =
      sh::Compile(mHandle, parts.data(), parts.size(), mCompileOptions);

  ret->mInfoLog = sh::GetInfoLog(mHandle);

  if (ret->mValid) {
    ret->mObjectCode = sh::GetObjectCode(mHandle);
    ret->mShaderVersion = sh::GetShaderVersion(mHandle);
    ret->mVertexShaderNumViews = sh::GetVertexShaderNumViews(mHandle);

    ret->mAttributes = *sh::GetAttributes(mHandle);
    ret->mInterfaceBlocks = *sh::GetInterfaceBlocks(mHandle);
    ret->mOutputVariables = *sh::GetOutputVariables(mHandle);
    ret->mUniforms = *sh::GetUniforms(mHandle);
    ret->mVaryings = *sh::GetVaryings(mHandle);

    ret->mMaxVaryingVectors = mMaxVaryingVectors;

    const auto& nameMap = *sh::GetNameHashingMap(mHandle);
    for (const auto& pair : nameMap) {
      ret->mNameMap.insert(pair);
    }

    // -
    // Custom translation steps
    auto* const translatedSource = &ret->mObjectCode;

    // gl_VertexID -> webgl_gl_VertexID
    // gl_InstanceID -> webgl_gl_InstanceID

    std::string header;
    std::string_view body = *translatedSource;
    if (StartsWith(body, "#version")) {
      const auto parts = Split(body, "\n", 1);
      header = parts.at(0);
      header += "\n";
      body = parts.at(1);
    }

    for (const auto& attrib : ret->mAttributes) {
      if (mIfNeeded_webgl_gl_VertexID_Offset && attrib.name == "gl_VertexID" &&
          attrib.staticUse) {
        header += "uniform int webgl_gl_VertexID_Offset;\n";
        header +=
            "#define gl_VertexID (gl_VertexID + webgl_gl_VertexID_Offset)\n";
        ret->mNeeds_webgl_gl_VertexID_Offset = true;
      }
    }

    if (header.size()) {
      auto combined = header;
      combined += body;
      *translatedSource = combined;
    }
  }

  sh::ClearResults(mHandle);
  return ret;
}

bool ShaderValidatorResults::CanLinkTo(const ShaderValidatorResults& vert,
                                       nsCString* const out_log) const {
  MOZ_ASSERT(mValid);
  MOZ_ASSERT(vert.mValid);

  if (vert.mShaderVersion != mShaderVersion) {
    nsPrintfCString error(
        "Vertex shader version %d does not match"
        " fragment shader version %d.",
        vert.mShaderVersion, mShaderVersion);
    *out_log = error;
    return false;
  }

  for (const auto& itrFrag : mUniforms) {
    for (const auto& itrVert : vert.mUniforms) {
      if (itrVert.name != itrFrag.name) continue;

      if (!itrVert.isSameUniformAtLinkTime(itrFrag)) {
        nsPrintfCString error(
            "Uniform `%s` is not linkable between"
            " attached shaders.",
            itrFrag.name.c_str());
        *out_log = error;
        return false;
      }

      break;
    }
  }

  for (const auto& fragVar : mInterfaceBlocks) {
    for (const auto& vertVar : vert.mInterfaceBlocks) {
      if (vertVar.name != fragVar.name) continue;

      if (!vertVar.isSameInterfaceBlockAtLinkTime(fragVar)) {
        nsPrintfCString error(
            "Interface block `%s` is not linkable between"
            " attached shaders.",
            fragVar.name.c_str());
        *out_log = error;
        return false;
      }

      break;
    }
  }

  {
    std::vector<sh::ShaderVariable> staticUseVaryingList;

    for (const auto& fragVarying : mVaryings) {
      static const char prefix[] = "gl_";
      if (StartsWith(fragVarying.name, prefix)) {
        if (fragVarying.staticUse) {
          staticUseVaryingList.push_back(fragVarying);
        }
        continue;
      }

      bool definedInVertShader = false;
      bool staticVertUse = false;

      for (const auto& vertVarying : vert.mVaryings) {
        if (vertVarying.name != fragVarying.name) continue;

        if (!vertVarying.isSameVaryingAtLinkTime(fragVarying, mShaderVersion)) {
          nsPrintfCString error(
              "Varying `%s`is not linkable between"
              " attached shaders.",
              fragVarying.name.c_str());
          *out_log = error;
          return false;
        }

        definedInVertShader = true;
        staticVertUse = vertVarying.staticUse;
        break;
      }

      if (!definedInVertShader && fragVarying.staticUse) {
        nsPrintfCString error(
            "Varying `%s` has static-use in the frag"
            " shader, but is undeclared in the vert"
            " shader.",
            fragVarying.name.c_str());
        *out_log = error;
        return false;
      }

      if (staticVertUse && fragVarying.staticUse) {
        staticUseVaryingList.push_back(fragVarying);
      }
    }

    if (!sh::CheckVariablesWithinPackingLimits(mMaxVaryingVectors,
                                               staticUseVaryingList)) {
      *out_log =
          "Statically used varyings do not fit within packing limits. (see"
          " GLSL ES Specification 1.0.17, p111)";
      return false;
    }
  }

  if (mShaderVersion == 100) {
    // Enforce ESSL1 invariant linking rules.
    bool isInvariant_Position = false;
    bool isInvariant_PointSize = false;
    bool isInvariant_FragCoord = false;
    bool isInvariant_PointCoord = false;

    for (const auto& varying : vert.mVaryings) {
      if (varying.name == "gl_Position") {
        isInvariant_Position = varying.isInvariant;
      } else if (varying.name == "gl_PointSize") {
        isInvariant_PointSize = varying.isInvariant;
      }
    }

    for (const auto& varying : mVaryings) {
      if (varying.name == "gl_FragCoord") {
        isInvariant_FragCoord = varying.isInvariant;
      } else if (varying.name == "gl_PointCoord") {
        isInvariant_PointCoord = varying.isInvariant;
      }
    }

    ////

    const auto fnCanBuiltInsLink = [](bool vertIsInvariant,
                                      bool fragIsInvariant) {
      if (vertIsInvariant) return true;

      return !fragIsInvariant;
    };

    if (!fnCanBuiltInsLink(isInvariant_Position, isInvariant_FragCoord)) {
      *out_log =
          "gl_Position must be invariant if gl_FragCoord is. (see GLSL ES"
          " Specification 1.0.17, p39)";
      return false;
    }

    if (!fnCanBuiltInsLink(isInvariant_PointSize, isInvariant_PointCoord)) {
      *out_log =
          "gl_PointSize must be invariant if gl_PointCoord is. (see GLSL ES"
          " Specification 1.0.17, p39)";
      return false;
    }
  }

  return true;
}

size_t ShaderValidatorResults::SizeOfIncludingThis(
    const mozilla::MallocSizeOf fnSizeOf) const {
  auto ret = fnSizeOf(this);

  // std::string heap allocations are not measured here because:
  // 1. Small String Optimization (SSO) means data() may point to inline
  //    storage within the std::string object (already counted in
  //    fnSizeOf(this))
  // 2. There's no standard way to distinguish SSO from heap-allocated strings
  // 3. Calling fnSizeOf on a pointer to inline storage is inappropriate

  if (!mAttributes.empty()) {
    ret += fnSizeOf(mAttributes.data());
  }
  if (!mInterfaceBlocks.empty()) {
    ret += fnSizeOf(mInterfaceBlocks.data());
  }
  if (!mOutputVariables.empty()) {
    ret += fnSizeOf(mOutputVariables.data());
  }
  if (!mUniforms.empty()) {
    ret += fnSizeOf(mUniforms.data());
  }
  if (!mVaryings.empty()) {
    ret += fnSizeOf(mVaryings.data());
  }

  return ret;
}

}  // namespace webgl
}  // namespace mozilla
