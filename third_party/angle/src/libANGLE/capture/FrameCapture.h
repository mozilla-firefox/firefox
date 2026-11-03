//
// Copyright 2019 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// FrameCapture.h:
//   ANGLE Frame capture interface.
//

#ifndef LIBANGLE_FRAME_CAPTURE_H_
#define LIBANGLE_FRAME_CAPTURE_H_

#include <fstream>
#include "common/unsafe_buffers.h"
#include "sys/stat.h"

#include "common/PackedEnums.h"
#include "common/SimpleMutex.h"
#include "common/frame_capture_binary_data.h"
#include "common/frame_capture_shared.h"
#include "common/frame_capture_utils.h"
#include "common/string_utils.h"
#include "common/system_utils.h"
#include "libANGLE/Context.h"
#include "libANGLE/ShareGroup.h"
#include "libANGLE/Thread.h"
#include "libANGLE/angletypes.h"
#include "libANGLE/entry_points_utils.h"

#ifdef ANGLE_ENABLE_CL
#    include "libANGLE/CLPlatform.h"
#endif

namespace gl
{
enum class GLESEnum;
}  // namespace gl

namespace angle
{
// Helper to use unique IDs for each local data variable.
class DataCounters final : angle::NonCopyable
{
  public:
    DataCounters();
    ~DataCounters();

    int getAndIncrement(EntryPoint entryPoint, const std::string &paramName);
    void reset() { mData.clear(); }

  private:
    // <CallName, ParamName>
    using Counter = std::pair<EntryPoint, std::string>;
    std::map<Counter, int> mData;
};

constexpr int kStringsNotFound = -1;
class StringCounters final : angle::NonCopyable
{
  public:
    StringCounters();
    ~StringCounters();

    int getStringCounter(const std::vector<std::string> &str);
    void setStringCounter(const std::vector<std::string> &str, int &counter);
    void reset() { mStringCounterMap.clear(); }

  private:
    std::map<std::vector<std::string>, int> mStringCounterMap;
};

class DataTracker final : angle::NonCopyable
{
  public:
    DataTracker();
    ~DataTracker();

    DataCounters &getCounters() { return mCounters; }
    StringCounters &getStringCounters() { return mStringCounters; }

    void reset()
    {
        mCounters.reset();
        mStringCounters.reset();
    }

  private:
    DataCounters mCounters;
    StringCounters mStringCounters;
};

enum class CaptureAPI : uint8_t
{
    GL = 0,
    CL = 1,

    InvalidEnum = 2,
    EnumCount   = 2,
};

class ReplayWriter final : angle::NonCopyable
{
  public:
    ReplayWriter();
    ~ReplayWriter();

    void setSourceFileExtension(const char *ext);
    void setSourceFileSizeThreshold(size_t sourceFileSizeThreshold);
    void setFilenamePattern(const std::string &pattern);
    void setSourcePrologue(const std::string &prologue);
    void setHeaderPrologue(const std::string &prologue);

    void addPublicFunction(const std::string &functionProto,
                           const std::stringstream &headerStream,
                           const std::stringstream &bodyStream);
    void addPrivateFunction(const std::string &functionProto,
                            const std::stringstream &headerStream,
                            const std::stringstream &bodyStream);
    std::string getInlineVariableName(EntryPoint entryPoint, const std::string &paramName);

    std::string getInlineStringSetVariableName(EntryPoint entryPoint,
                                               const std::string &paramName,
                                               const std::vector<std::string> &strings,
                                               bool *isNewEntryOut);

    void addStaticVariable(const std::string &customVarType, const std::string &customVarName);

    void saveFrame();
    void saveFrameIfFull();
    void saveIndexFilesAndHeader();
    void saveSetupFile();

    std::vector<std::string> getAndResetWrittenFiles();

    void reset()
    {
        mDataTracker.reset();
        mFrameIndex = 1;  // This is really the FileIndex
    }

    CaptureAPI captureAPI = CaptureAPI::GL;

  private:
    static std::string GetVarName(EntryPoint entryPoint, const std::string &paramName, int counter);

    void saveHeader();
    void writeReplaySource(const std::string &filename);
    void addWrittenFile(const std::string &filename);
    size_t getStoredReplaySourceSize() const;

    std::string mSourceFileExtension;
    size_t mSourceFileSizeThreshold;
    size_t mFrameIndex;

    DataTracker mDataTracker;
    std::string mFilenamePattern;
    std::string mSourcePrologue;
    std::string mHeaderPrologue;

    std::vector<std::string> mReplayHeaders;
    std::vector<std::string> mGlobalVariableDeclarations;
    std::vector<std::string> mStaticVariableDeclarations;

    std::vector<std::string> mPublicFunctionPrototypes;
    std::vector<std::string> mPublicFunctions;

    std::vector<std::string> mPrivateFunctionPrototypes;
    std::vector<std::string> mPrivateFunctions;

    std::vector<std::string> mWrittenFiles;
};

using BufferCalls = std::map<GLuint, std::vector<CallCapture>>;

// true means mapped, false means unmapped
using BufferMapStatusMap = std::map<GLuint, bool>;

using FenceSyncSet   = std::set<gl::SyncID>;
using FenceSyncCalls = std::map<gl::SyncID, std::vector<CallCapture>>;

// For default uniforms, we need to track which ones are dirty, and the series of calls to reset.
// Each program has unique default uniforms, and each uniform has one or more locations in the
// default buffer. For reset efficiency, we track only the uniforms dirty by location, per program.

// A set of all default uniforms (per program) that were modified during the run
using DefaultUniformLocationsSet = std::set<gl::UniformLocation>;
using DefaultUniformLocationsPerProgramMap =
    std::map<gl::ShaderProgramID, DefaultUniformLocationsSet>;

// A map of programs which maps to locations and their reset calls
using DefaultUniformCallsPerLocationMap = std::map<gl::UniformLocation, std::vector<CallCapture>>;
using DefaultUniformCallsPerProgramMap =
    std::map<gl::ShaderProgramID, DefaultUniformCallsPerLocationMap>;

using DefaultUniformBaseLocationMap =
    std::map<std::pair<gl::ShaderProgramID, gl::UniformLocation>, gl::UniformLocation>;

using ResourceSet   = std::set<GLuint>;
using ResourceCalls = std::map<GLuint, std::vector<CallCapture>>;

// For texture parameters, we should track the series of calls to reset the parameters for each
// texture and which parameters are dirty per texture.
struct TextureParamReset
{
    CallCapture call;
    bool isDirty;
};

using TextureParamResetPerParamMap   = std::map<GLenum, TextureParamReset>;
using TextureParamResetPerTextureMap = std::map<GLuint, TextureParamResetPerParamMap>;

class TrackedResource final : angle::NonCopyable
{
  public:
    TrackedResource();
    ~TrackedResource();

    const ResourceSet &getStartingResources() const { return mStartingResources; }
    ResourceSet &getStartingResources() { return mStartingResources; }
    const ResourceSet &getNewResources() const { return mNewResources; }
    ResourceSet &getNewResources() { return mNewResources; }
    const ResourceSet &getResourcesToDelete() const { return mResourcesToDelete; }
    ResourceSet &getResourcesToDelete() { return mResourcesToDelete; }
    const ResourceSet &getResourcesToRegen() const { return mResourcesToRegen; }
    ResourceSet &getResourcesToRegen() { return mResourcesToRegen; }
    const ResourceSet &getResourcesToRestore() const { return mResourcesToRestore; }
    ResourceSet &getResourcesToRestore() { return mResourcesToRestore; }

    void setGennedResource(GLuint id);
    void setDeletedResource(GLuint id);
    void setModifiedResource(GLuint id);
    bool resourceIsGenerated(GLuint id);

    ResourceCalls &getResourceRegenCalls() { return mResourceRegenCalls; }
    ResourceCalls &getResourceRestoreCalls() { return mResourceRestoreCalls; }

    void reset()
    {
        mResourceRegenCalls.clear();
        mResourceRestoreCalls.clear();
        mStartingResources.clear();
        mNewResources.clear();
        mResourcesToDelete.clear();
        mResourcesToRegen.clear();
        mResourcesToRestore.clear();
    }

  private:
    // Resource regen calls will gen a resource
    ResourceCalls mResourceRegenCalls;
    // Resource restore calls will restore the contents of a resource
    ResourceCalls mResourceRestoreCalls;

    // Resources created during startup
    ResourceSet mStartingResources;

    // Resources created during the run that need to be deleted
    ResourceSet mNewResources;
    // Resources recreated during the run that need to be deleted
    ResourceSet mResourcesToDelete;
    // Resources deleted during the run that need to be recreated
    ResourceSet mResourcesToRegen;
    // Resources modified during the run that need to be restored
    ResourceSet mResourcesToRestore;
};

using TrackedResourceArray =
    std::array<TrackedResource, static_cast<uint32_t>(ResourceIDType::EnumCount)>;

enum class ShaderProgramType
{
    ShaderType,
    ProgramType
};

// Helper to track resource changes during the capture
class ResourceTracker final : angle::NonCopyable
{
  public:
    ResourceTracker();
    ~ResourceTracker();

    BufferCalls &getBufferMapCalls() { return mBufferMapCalls; }
    BufferCalls &getBufferUnmapCalls() { return mBufferUnmapCalls; }

    std::vector<CallCapture> &getBufferBindingCalls() { return mBufferBindingCalls; }

    void setBufferMapped(gl::ContextID contextID, GLuint id);
    void setBufferUnmapped(gl::ContextID contextID, GLuint id);

    bool getStartingBuffersMappedCurrent(GLuint id) const;
    bool getStartingBuffersMappedInitial(GLuint id) const;

    void setStartingBufferMapped(GLuint id, bool mapped)
    {
        // Track the current state (which will change throughout the trace)
        mStartingBuffersMappedCurrent[id] = mapped;

        // And the initial state, to compare during frame loop reset
        mStartingBuffersMappedInitial[id] = mapped;
    }

    void onShaderProgramAccess(gl::ShaderProgramID shaderProgramID);
    uint32_t getMaxShaderPrograms() const { return mMaxShaderPrograms; }

    FenceSyncSet &getStartingFenceSyncs() { return mStartingFenceSyncs; }
    FenceSyncCalls &getFenceSyncRegenCalls() { return mFenceSyncRegenCalls; }
    FenceSyncSet &getFenceSyncsToRegen() { return mFenceSyncsToRegen; }
    void setDeletedFenceSync(gl::SyncID sync);

    DefaultUniformLocationsPerProgramMap &getDefaultUniformsToReset()
    {
        return mDefaultUniformsToReset;
    }
    DefaultUniformCallsPerLocationMap &getDefaultUniformResetCalls(gl::ShaderProgramID id)
    {
        return mDefaultUniformResetCalls[id];
    }
    void setModifiedDefaultUniform(gl::ShaderProgramID programID, gl::UniformLocation location);
    void setDefaultUniformBaseLocation(gl::ShaderProgramID programID,
                                       gl::UniformLocation location,
                                       gl::UniformLocation baseLocation);
    gl::UniformLocation getDefaultUniformBaseLocation(gl::ShaderProgramID programID,
                                                      gl::UniformLocation location)
    {
        ASSERT(mDefaultUniformBaseLocations.find({programID, location}) !=
               mDefaultUniformBaseLocations.end());
        return mDefaultUniformBaseLocations[{programID, location}];
    }

    TextureParamResetPerTextureMap &getTextureParameterResetCalls()
    {
        return mTextureParameterResetCalls;
    }

    TrackedResource &getTrackedResource(gl::ContextID contextID, ResourceIDType type);

    void getContextIDs(std::set<gl::ContextID> &idsOut);

    std::map<EGLImage, egl::AttributeMap> &getImageToAttribTable() { return mMatchImageToAttribs; }

    std::map<GLuint, egl::ImageID> &getTextureIDToImageTable() { return mMatchTextureIDToImage; }

    std::map<egl::ImageID, std::vector<uint8_t>> &getImageDataMap() { return mImageDataMap; }

    std::map<gl::ContextID, std::set<GLuint>> &getExternalImageBindingsToRestore()
    {
        return mExternalImageBindingsToRestore;
    }

    void setShaderProgramType(gl::ShaderProgramID id, angle::ShaderProgramType type)
    {
        mShaderProgramType[id] = type;
    }
    ShaderProgramType getShaderProgramType(gl::ShaderProgramID id)
    {
        ASSERT(mShaderProgramType.find(id) != mShaderProgramType.end());
        return mShaderProgramType[id];
    }

    void resetTrackedResourceArray(TrackedResourceArray &trackedResourceArray)
    {
        for (auto &trackedResource : trackedResourceArray)
        {
            trackedResource.reset();
        }
    }

    // Some data in FrameCaptureShared tracks resources across all captures while
    // other data is tracked per-capture. This function is responsible for
    // resetting the per-capture tracking data in this class.
    void resetResourceTracking()
    {
        resetTrackedResourceArray(mTrackedResourcesShared);
        for (auto &pair : mTrackedResourcesPerContext)
        {
            resetTrackedResourceArray(pair.second);
        }

        mBufferMapCalls.clear();
        mBufferUnmapCalls.clear();
        mBufferBindingCalls.clear();
        mStartingBuffersMappedInitial.clear();
        mStartingBuffersMappedCurrent.clear();
        mStartingFenceSyncs.clear();
        mFenceSyncRegenCalls.clear();
        mFenceSyncsToRegen.clear();
        mDefaultUniformsToReset.clear();
        mDefaultUniformResetCalls.clear();
        mDefaultUniformBaseLocations.clear();
        mTextureParameterResetCalls.clear();
        mImageDataMap.clear();
        mExternalImageBindingsToRestore.clear();
    }

  private:
    // These data structures are per-capture and must be reset before beginning a new
    // capture in the resetResourceTracking() function above

    // Buffer map calls will map a buffer with correct offset, length, and access flags
    BufferCalls mBufferMapCalls;
    // Buffer unmap calls will bind and unmap a given buffer
    BufferCalls mBufferUnmapCalls;
    // Buffer binding calls to restore bindings recorded during MEC
    std::vector<CallCapture> mBufferBindingCalls;
    // Whether a given buffer was mapped at the start of the trace
    BufferMapStatusMap mStartingBuffersMappedInitial;
    // The status of buffer mapping throughout the trace, modified with each Map/Unmap call
    BufferMapStatusMap mStartingBuffersMappedCurrent;
    // Maximum accessed shader program ID.
    uint32_t mMaxShaderPrograms = 0;
    // Fence sync objects created during MEC setup
    FenceSyncSet mStartingFenceSyncs;
    // Fence sync regen calls will create a fence sync objects
    FenceSyncCalls mFenceSyncRegenCalls;
    // Fence syncs to regen are a list of starting fence sync objects that were deleted and need to
    // be regen'ed.
    FenceSyncSet mFenceSyncsToRegen;
    // Default uniforms that were modified during the run
    DefaultUniformLocationsPerProgramMap mDefaultUniformsToReset;
    // Calls per default uniform to return to original state
    DefaultUniformCallsPerProgramMap mDefaultUniformResetCalls;
    // Base location of arrayed uniforms
    DefaultUniformBaseLocationMap mDefaultUniformBaseLocations;

    // Texture parameter reset calls to be emitted in ResetReplay
    TextureParamResetPerTextureMap mTextureParameterResetCalls;

    // These data structures must be preserved across all captures

    // Tracked resources per context
    TrackedResourceArray mTrackedResourcesShared;
    std::map<gl::ContextID, TrackedResourceArray> mTrackedResourcesPerContext;
    std::map<EGLImage, egl::AttributeMap> mMatchImageToAttribs;
    std::map<GLuint, egl::ImageID> mMatchTextureIDToImage;
    std::map<egl::ImageID, std::vector<uint8_t>> mImageDataMap;
    std::map<gl::ContextID, std::set<GLuint>> mExternalImageBindingsToRestore;
    std::map<gl::ShaderProgramID, ShaderProgramType> mShaderProgramType;
};

// CL specific resource tracker to track resource changes during the capture
#ifdef ANGLE_ENABLE_CL
struct ResourceTrackerCL final : angle::NonCopyable
{
    ResourceTrackerCL();
    ~ResourceTrackerCL();

    // To obtain indices of CL arguments in replay
    std::unordered_map<cl_platform_id, size_t> mCLPlatformIDIndices;
    std::unordered_map<cl_device_id, size_t> mCLDeviceIDIndices;
    std::unordered_map<cl_context, size_t> mCLContextIndices;
    std::unordered_map<cl_event, size_t> mCLEventsIndices;
    std::unordered_map<cl_command_queue, size_t> mCLCommandQueueIndices;
    std::unordered_map<cl_mem, size_t> mCLMemIndices;
    std::unordered_map<cl_sampler, size_t> mCLSamplerIndices;
    std::unordered_map<cl_program, size_t> mCLProgramIndices;
    std::unordered_map<cl_kernel, size_t> mCLKernelIndices;

    std::unordered_map<const void *, size_t> mCLVoidIndices;

    std::unordered_map<uint32_t, std::vector<size_t>> mCLParamIDToIndexVector;

    // To account for cl mem or SVM pointers that are potentially dirty
    // coming into the starting frame or from mapping and unmapping.
    std::vector<cl_mem> mCLDirtyMem;
    std::vector<void *> mCLDirtySVM;

    // Keeps track of the # of times the program is linked, including it's own creation
    std::unordered_map<cl_program, cl_uint> mCLProgramLinkCounter;

    // To keep track of the sub buffer and parent replationship
    std::unordered_map<cl_mem, cl_mem> mCLSubBufferToParent;

    // To keep track of the linked programs
    std::unordered_map<cl_program, std::vector<cl_program>> mCLLinkedPrograms;

    // Program to all the kernels in the program
    // So that when a program is released, it can also remove all the kernels.
    std::unordered_map<cl_program, std::vector<cl_kernel>> mCLProgramToKernels;

    // Kernel to program, to keep track of the program that a cloned kernel
    // belongs to. Can't use ANGLE's getProgram() because the kernel object
    // may be deleted by the time it's needed for capture.
    std::unordered_map<cl_kernel, cl_program> mCLKernelToProgram;

    // Mapped pointer to the map call
    std::unordered_map<const void *, CallCapture> mCLMapCall;

    // Gets the size of the SVM memory
    std::unordered_map<const void *, size_t> SVMToSize;

    cl_command_queue mCLCurrentCommandQueue;

    std::vector<ParamCapture> mCLResetObjs;
};
#endif

// Used by the CPP replay to filter out unnecessary code.
using HasResourceTypeMap = angle::PackedEnumBitSet<ResourceIDType>;

// Map of ResourceType to IDs and range of setup calls
using ResourceIDToSetupCallsMap =
    PackedEnumMap<ResourceIDType, std::map<GLuint, gl::Range<size_t>>>;

// Map of buffer ID to offset and size used when mapped
using BufferDataMap = std::map<gl::BufferID, std::pair<GLintptr, GLsizeiptr>>;

// A dictionary of sources indexed by shader type.
using ProgramSources = gl::ShaderMap<std::string>;

// Maps from IDs to sources.
using ShaderSourceMap  = std::map<gl::ShaderProgramID, std::string>;
using ProgramSourceMap = std::map<gl::ShaderProgramID, ProgramSources>;

// Map from textureID to level and data
using TextureLevels       = std::map<GLint, std::vector<uint8_t>>;
using TextureLevelDataMap = std::map<gl::TextureID, TextureLevels>;

struct SurfaceParams
{
    gl::Extents extents;
    EGLint type = EGL_NONE;
    egl::ColorSpace colorSpace;
};

// Map from ContextID to SurfaceParams
using SurfaceParamsMap = std::map<gl::ContextID, SurfaceParams>;

using CallVector = std::vector<std::vector<CallCapture> *>;

// A map from API entry point to calls
using CallResetMap = std::map<angle::EntryPoint, std::vector<CallCapture>>;

using TextureBinding  = std::pair<size_t, gl::TextureType>;
using TextureResetMap = std::map<TextureBinding, gl::TextureID>;

using BufferBindingPair = std::pair<gl::BufferBinding, gl::BufferID>;

// StateResetHelper provides a simple way to track whether an entry point has been called during the
// trace, along with the reset calls to get it back to starting state.  This is useful for things
// that are one dimensional, like context bindings or context state.
class StateResetHelper final : angle::NonCopyable
{
  public:
    StateResetHelper();
    ~StateResetHelper();

    const std::set<angle::EntryPoint> &getDirtyEntryPoints() const { return mDirtyEntryPoints; }
    void setEntryPointDirty(EntryPoint entryPoint) { mDirtyEntryPoints.insert(entryPoint); }

    CallResetMap &getResetCalls() { return mResetCalls; }
    const CallResetMap &getResetCalls() const { return mResetCalls; }

    void setDefaultResetCalls(const gl::Context *context, angle::EntryPoint);

    const std::set<TextureBinding> &getDirtyTextureBindings() const
    {
        return mDirtyTextureBindings;
    }
    void setTextureBindingDirty(size_t unit, gl::TextureType target)
    {
        mDirtyTextureBindings.emplace(unit, target);
    }

    TextureResetMap &getResetTextureBindings() { return mResetTextureBindings; }

    void setResetActiveTexture(size_t textureID) { mResetActiveTexture = textureID; }
    size_t getResetActiveTexture() { return mResetActiveTexture; }

    void setCurrentResetActiveTexture(size_t textureID) { mCurrentResetActiveTexture = textureID; }
    size_t getCurrentResetActiveTexture() const { return mCurrentResetActiveTexture; }

    const std::set<gl::BufferBinding> &getDirtyBufferBindings() const
    {
        return mDirtyBufferBindings;
    }
    void setBufferBindingDirty(gl::BufferBinding binding) { mDirtyBufferBindings.insert(binding); }

    const std::set<BufferBindingPair> &getStartingBufferBindings() const
    {
        return mStartingBufferBindings;
    }
    void setStartingBufferBinding(gl::BufferBinding binding, gl::BufferID bufferID)
    {
        mStartingBufferBindings.insert({binding, bufferID});
    }

    void reset()
    {
        mDirtyEntryPoints.clear();
        mResetCalls.clear();
        mDirtyTextureBindings.clear();
        mResetTextureBindings.clear();
        mResetActiveTexture        = 0;
        mCurrentResetActiveTexture = 0;
        mStartingBufferBindings.clear();
        mDirtyBufferBindings.clear();
    }

  private:
    // Dirty state per entry point
    std::set<angle::EntryPoint> mDirtyEntryPoints;

    // Reset calls per API entry point
    CallResetMap mResetCalls;

    // Dirty state per texture binding
    std::set<TextureBinding> mDirtyTextureBindings;

    // Texture bindings and active texture to restore
    TextureResetMap mResetTextureBindings;
    size_t mResetActiveTexture        = 0;
    size_t mCurrentResetActiveTexture = 0;

    // Starting and dirty buffer bindings
    std::set<BufferBindingPair> mStartingBufferBindings;
    std::set<gl::BufferBinding> mDirtyBufferBindings;
};

class FrameCapture final : angle::NonCopyable
{
  public:
    FrameCapture();
    ~FrameCapture();

    std::vector<CallCapture> &getSetupCalls() { return mSetupCalls; }
    void clearSetupCalls() { mSetupCalls.clear(); }

    StateResetHelper &getStateResetHelper() { return mStateResetHelper; }

    void reset();

  private:
    std::vector<CallCapture> mSetupCalls;

    StateResetHelper mStateResetHelper;
};

// Page range inside a coherent buffer
struct PageRange
{
    PageRange(size_t start, size_t end);
    ~PageRange();

    // Relative start page
    size_t start;

    // First page after the relative end
    size_t end;
};

// Memory address range defined by start and size
struct AddressRange
{
    AddressRange();
    AddressRange(uintptr_t start, size_t size);
    ~AddressRange();

    uintptr_t end();

    uintptr_t start;
    size_t size;
};

// Used to handle protection of buffers that overlap in pages.
enum class PageSharingType
{
    NoneShared,
    FirstShared,
    LastShared,
    FirstAndLastShared
};

class CoherentBuffer
{
  public:
    CoherentBuffer(uintptr_t start, size_t size, size_t pageSize, bool useShadowMemory);
    ~CoherentBuffer();

    // Sets the a range in the buffer clean and protects a selected range
    void protectPageRange(const PageRange &pageRange);

    // Sets all pages to clean and enables protection
    void protectAll();

    // Sets a page dirty state and sets it's protection
    void setDirty(size_t relativePage, bool dirty);

    // Shadow memory synchronization
    void updateBufferMemory();
    void updateShadowMemory();

    // Removes protection
    void removeProtection(PageSharingType sharingType);

    bool contains(size_t page, size_t *relativePage);
    bool isDirty();

    // Returns dirty page ranges
    std::vector<PageRange> getDirtyPageRanges();

    // Calculates address range from page range
    AddressRange getDirtyAddressRange(const PageRange &dirtyPageRange);
    AddressRange getRange();

    void markShadowDirty() { mShadowDirty = true; }
    bool isShadowDirty() { return mShadowDirty; }

  private:
    // Actual buffer start and size
    AddressRange mRange;

    // Start and size of page aligned protected area
    AddressRange mProtectionRange;

    // Start and end of protection in relative pages, calculated from mProtectionRange.
    size_t mProtectionStartPage;
    size_t mProtectionEndPage;

    size_t mPageCount;
    size_t mPageSize;

    // Clean pages are protected
    std::vector<bool> mDirtyPages;

    // shadow memory releated fields
    bool mShadowMemoryEnabled;
    uintptr_t mBufferStart;
    void *mShadowMemory;
    bool mShadowDirty;
};

class CoherentBufferTracker final : angle::NonCopyable
{
  public:
    CoherentBufferTracker();
    ~CoherentBufferTracker();

    bool isDirty(gl::BufferID id);
    uintptr_t addBuffer(gl::BufferID id, uintptr_t start, size_t size);
    void removeBuffer(gl::BufferID id);
    void disable();
    void enable();
    void onEndFrame();
    bool haveBuffer(gl::BufferID id);
    bool isShadowMemoryEnabled() { return mShadowMemoryEnabled; }
    void enableShadowMemory() { mShadowMemoryEnabled = true; }
    void maybeUpdateShadowMemory();
    void markAllShadowDirty();
    // Determine whether memory protection can be used directly on graphics memory
    bool canProtectDirectly(gl::Context *context);

  private:
    // Detect overlapping pages when removing protection
    PageSharingType doesBufferSharePage(gl::BufferID id);

    // Returns a map to found buffers and the corresponding pages for a given address.
    // For addresses that are in a page shared by 2 buffers, 2 results are returned.
    HashMap<std::shared_ptr<CoherentBuffer>, size_t> getBufferPagesForAddress(uintptr_t address);
    PageFaultHandlerRangeType handleWrite(uintptr_t address);

  public:
    angle::SimpleMutex mMutex;
    HashMap<GLuint, std::shared_ptr<CoherentBuffer>> mBuffers;
    bool hasBeenReset() { return mHasBeenReset; }

  private:
    bool mEnabled;
    bool mHasBeenReset;
    std::unique_ptr<PageFaultHandler> mPageFaultHandler;
    size_t mPageSize;

    bool mShadowMemoryEnabled;
};

// Shared class for any items that need to be tracked by FrameCapture across shared contexts
class FrameCaptureShared final : angle::NonCopyable
{
  public:
    FrameCaptureShared();
    ~FrameCaptureShared();

    void captureCall(gl::Context *context, CallCapture &&call, bool isCallValid);
    void checkForCaptureTrigger();
    bool checkForCaptureEnd();
    void onEndFrame(gl::Context *context);
    void onDestroyContext(const gl::Context *context);
    bool onEndCLCapture();
    void onMakeCurrent(const gl::Context *context,
                       const egl::Surface *drawSurface,
                       EGLint surfaceWidth,
                       EGLint surfaceHeight);
    bool enabled() const { return mEnabled; }

    bool isCapturing() const;
    uint32_t getFrameCount() const;

    // Returns a frame index starting from "1" as the first frame.
    uint32_t getReplayFrameIndex() const;

#ifdef ANGLE_ENABLE_CL
    void captureCLCall(CallCapture &&call, bool isCallValid);
    static void onCLProgramEnd();

    template <typename T>
    size_t getIndex(const T *object)
    {
        if (getMap<T>().find(*object) == getMap<T>().end())
        {
            return SIZE_MAX;
        }
        return getMap<T>()[*object];
    }
    size_t getCLVoidIndex(const void *v);
    std::vector<size_t> getCLObjVector(const angle::ParamCapture *paramCaptureKey);

    template <typename T>
    void setIndex(const T *object)
    {
        if (getMap<T>().find(*object) == getMap<T>().end())
        {
            size_t tempSize      = getMap<T>().size();
            getMap<T>()[*object] = tempSize;
        }
    }
    void setCLPlatformIndices(cl_platform_id *platforms, size_t numPlatforms);
    void setCLDeviceIndices(cl_device_id *devices, size_t numDevices);
    void setCLVoidIndex(const void *v);
    void setOffsetsVector(const void *args,
                          const void **argsLocations,
                          size_t numLocations,
                          const angle::ParamCapture *paramCaptureKey);
    void setCLVoidVectorIndex(const void *pointers[],
                              size_t numPointers,
                              const angle::ParamCapture *paramCaptureKey);

    template <typename T>
    using MemberFuncPtr = size_t (FrameCaptureShared::*)(const T *);

    template <typename T>
    void setCLObjVectorMap(const T *objs,
                           size_t numObjs,
                           const angle::ParamCapture *paramCaptureKey,
                           MemberFuncPtr<const T> getCLObjIndexFunc)
    {
        mResourceTrackerCL.mCLParamIDToIndexVector[paramCaptureKey->uniqueID] =
            std::vector<size_t>();
        for (size_t i = 0; i < numObjs; ++i)
        {
            mResourceTrackerCL.mCLParamIDToIndexVector[paramCaptureKey->uniqueID].push_back(
                (this->*getCLObjIndexFunc)(&ANGLE_UNSAFE_TODO(objs[i])));
        }
    }

    template <typename T>
    std::unordered_map<T, size_t> &getMap();

    void addCLResetObj(const angle::ParamCapture &param);
    void removeCLResetObj(const ParamCapture &param);
    void printCLResetObjs(std::stringstream &stream);

    void trackCLMemUpdate(const cl_mem *mem, bool created);
    void trackCLProgramUpdate(const cl_program *program,
                              bool referenced,
                              cl_uint numLinkedPrograms,
                              const cl_program *linkedPrograms);
    void trackCLEvents(const cl_event *event, bool created);
    void injectMemcpy(void *src, void *dest, size_t size, std::vector<CallCapture> *calls);
    void captureUpdateCLObjs(std::vector<CallCapture> *calls);
    void removeCLMemOccurrences(const cl_mem *mem, std::vector<CallCapture> *calls);
    void removeCLKernelOccurrences(const cl_kernel *kernel, std::vector<CallCapture> *calls);
    void removeCLProgramOccurrences(const cl_program *program, std::vector<CallCapture> *calls);
    void removeCLCall(std::vector<CallCapture> *callVector, size_t &callIndex);
#endif

    void trackBufferMapping(const gl::Context *context,
                            CallCapture *call,
                            gl::BufferID id,
                            gl::Buffer *buffer,
                            GLintptr offset,
                            GLsizeiptr length,
                            bool writable,
                            bool coherent,
                            bool persistent);

    void trackTextureUpdate(const gl::Context *context, const CallCapture &call);
    void trackTextureStateUpdate(const gl::Context *context, const CallCapture &call);
    void trackFramebufferAttachmentUpdate(const gl::Context *context, const CallCapture &call);
    void trackImageUpdate(const gl::Context *context, const CallCapture &call);
    void trackDefaultUniformUpdate(const gl::Context *context, const CallCapture &call);
    void trackVertexArrayUpdate(const gl::Context *context, const CallCapture &call);

    const std::string &getShaderSource(gl::ShaderProgramID id) const;
    void setShaderSource(gl::ShaderProgramID id, std::string sources);

    const ProgramSources &getProgramSources(gl::ShaderProgramID id) const;
    void setProgramSources(gl::ShaderProgramID id, ProgramSources sources);

    // Load data from a previously stored texture level
    const std::vector<uint8_t> &retrieveCachedTextureLevel(gl::TextureID id,
                                                           gl::TextureTarget target,
                                                           GLint level);

    // Create new texture level data and copy the source into it
    void copyCachedTextureLevel(const gl::Context *context,
                                gl::TextureID srcID,
                                GLint srcLevel,
                                gl::TextureID dstID,
                                GLint dstLevel,
                                const CallCapture &call);

    // Create the location that should be used to cache texture level data
    std::vector<uint8_t> &getCachedTextureLevelData(gl::Texture *texture,
                                                    gl::TextureTarget target,
                                                    GLint level,
                                                    EntryPoint entryPoint);

    // Capture coherent buffer storages
    void captureCoherentBufferSnapshot(const gl::Context *context, gl::BufferID bufferID);

    // Remove any cached texture levels on deletion
    void deleteCachedTextureLevelData(gl::TextureID id);

    void eraseBufferDataMapEntry(const gl::BufferID bufferId)
    {
        const auto &bufferDataInfo = mBufferDataMap.find(bufferId);
        if (bufferDataInfo != mBufferDataMap.end())
        {
            mBufferDataMap.erase(bufferDataInfo);
        }
    }

    bool hasBufferData(gl::BufferID bufferID)
    {
        const auto &bufferDataInfo = mBufferDataMap.find(bufferID);
        if (bufferDataInfo != mBufferDataMap.end())
        {
            return true;
        }
        return false;
    }

    std::pair<GLintptr, GLsizeiptr> getBufferDataOffsetAndLength(gl::BufferID bufferID)
    {
        const auto &bufferDataInfo = mBufferDataMap.find(bufferID);
        ASSERT(bufferDataInfo != mBufferDataMap.end());
        return bufferDataInfo->second;
    }

    void setCaptureActive() { mCaptureActive = true; }
    void setCaptureInactive() { mCaptureActive = false; }
    bool isCaptureActive() { return mCaptureActive; }
    bool usesMidExecutionCapture() { return mCaptureStartFrame > 1; }

    gl::ContextID getWindowSurfaceContextID() const { return mWindowSurfaceContextID; }

    void markResourceSetupCallsInactive(std::vector<CallCapture> *setupCalls,
                                        ResourceIDType type,
                                        GLuint id,
                                        gl::Range<size_t> range);

    void getOutputDirectory();
    void updateReadBufferSize(size_t readBufferSize)
    {
        mReadBufferSize = std::max(mReadBufferSize, readBufferSize);
    }

    template <typename ResourceType>
    void handleGennedResource(const gl::Context *context, ResourceType resourceID)
    {
        if (isCaptureActive())
        {
            ResourceIDType idType    = GetResourceIDTypeFromType<ResourceType>::IDType;
            TrackedResource &tracker = mResourceTracker.getTrackedResource(context->id(), idType);
            tracker.setGennedResource(resourceID.value);
        }
    }

    template <typename ResourceType>
    bool resourceIsGenerated(const gl::Context *context, ResourceType resourceID)
    {
        ResourceIDType idType    = GetResourceIDTypeFromType<ResourceType>::IDType;
        TrackedResource &tracker = mResourceTracker.getTrackedResource(context->id(), idType);
        return tracker.resourceIsGenerated(resourceID.value);
    }

    template <typename ResourceType>
    void handleDeletedResource(const gl::Context *context, ResourceType resourceID)
    {
        if (isCaptureActive())
        {
            ResourceIDType idType    = GetResourceIDTypeFromType<ResourceType>::IDType;
            TrackedResource &tracker = mResourceTracker.getTrackedResource(context->id(), idType);
            tracker.setDeletedResource(resourceID.value);
        }
    }

    void *maybeGetShadowMemoryPointer(gl::Buffer *buffer, GLsizeiptr length, GLbitfield access);
    void determineMemoryProtectionSupport(gl::Context *context);

    angle::SimpleMutex &getFrameCaptureMutex() { return mFrameCaptureMutex; }

    void markGLSyncEmitted(gl::SyncID syncID) { mEmittedGLSyncIDs.insert(syncID.value); }
    void markEGLSyncEmitted(egl::SyncID syncID) { mEmittedEGLSyncIDs.insert(syncID.value); }
    bool isGLSyncEmitted(gl::SyncID syncID) const
    {
        return mEmittedGLSyncIDs.find(syncID.value) != mEmittedGLSyncIDs.end();
    }
    bool isEGLSyncEmitted(egl::SyncID syncID) const
    {
        return mEmittedEGLSyncIDs.find(syncID.value) != mEmittedEGLSyncIDs.end();
    }

    void setDeferredLinkProgram(gl::ShaderProgramID programID)
    {
        mDeferredLinkPrograms.emplace(programID);
    }
    bool isDeferredLinkProgram(gl::ShaderProgramID programID)
    {
        return (mDeferredLinkPrograms.find(programID) != mDeferredLinkPrograms.end());
    }

    static bool isRuntimeEnabled();

    void resetCaptureStartEndFrames()
    {
        // If the trigger has been populated the frame range variables will be calculated
        // based on the trigger value, so for now reset them to unreasonable values.
        mCaptureStartFrame = mCaptureEndFrame = std::numeric_limits<uint32_t>::max();
        INFO() << "Capture trigger detected, resetting capture start/end frame.";
    }

    void initalizeTraceStorage();

  private:
    void writeJSON(const gl::Context *context);
    void writeJSONCL();
    void writeJSONCLGetInfo();
    void saveCLGetInfo(const CallCapture &call);
    void writeCppReplayIndexFiles(const gl::Context *context, bool writeResetContextCall);
    void writeCppReplayIndexFilesCL();
    void writeMainContextCppReplay(const gl::Context *context,
                                   const std::vector<CallCapture> &setupCalls,
                                   StateResetHelper &StateResetHelper);
    void writeMainContextCppReplayCL();

    void captureClientArraySnapshot(const gl::Context *context,
                                    size_t vertexCount,
                                    size_t instanceCount);
    void captureMappedBufferSnapshot(const gl::Context *context, const CallCapture &call);

    void copyCompressedTextureData(const gl::Context *context, const CallCapture &call);
    void captureCompressedTextureData(const gl::Context *context, const CallCapture &call);

    void reset();
    void resetMidExecutionCapture(gl::Context *context);
    void maybeSetSyncPoint(CallCapture &inCall);
    void maybeOverrideEntryPoint(const gl::Context *context,
                                 CallCapture &call,
                                 std::vector<CallCapture> &newCalls);
    void maybeCapturePreCallUpdates(const gl::Context *context,
                                    CallCapture &call,
                                    std::vector<CallCapture> *shareGroupSetupCalls,
                                    ResourceIDToSetupCallsMap *resourceIDToSetupCalls);
    void maybeCapturePreCallUpdatesCL(CallCapture &call);
    template <typename ParamValueType>
    void maybeGenResourceOnBind(const gl::Context *context, CallCapture &call);
    void maybeCapturePostCallUpdates(const gl::Context *context);
    void maybeCapturePostCallUpdatesCL();
    void maybeCaptureDrawArraysClientData(const gl::Context *context,
                                          CallCapture &call,
                                          size_t instanceCount);
    void maybeCaptureDrawElementsClientData(const gl::Context *context,
                                            CallCapture &call,
                                            size_t instanceCount);
    void maybeCaptureCoherentBuffers(const gl::Context *context);
    void captureCustomMapBufferFromContext(const gl::Context *context,
                                           const char *entryPointName,
                                           CallCapture &call,
                                           std::vector<CallCapture> &callsOut);
    void updateCopyImageSubData(CallCapture &call);
    void overrideProgramBinary(const gl::Context *context,
                               CallCapture &call,
                               std::vector<CallCapture> &outCalls);
    void updateResourceCountsFromParamCapture(const ParamCapture &param, ResourceIDType idType);
    void updateResourceCountsFromParamCaptureCL(const ParamCapture &param, const CallCapture &call);
    void updateResourceCountsFromCallCapture(const CallCapture &call);
    void updateResourceCountsFromCallCaptureCL(const CallCapture &call);

    void runMidExecutionCapture(gl::Context *context);

    void scanSetupCalls(std::vector<CallCapture> &setupCalls);

    std::vector<CallCapture> mFrameCalls;
    std::vector<size_t> mClientVertexArrayCallIndices;
    gl::AttributesMask mClientVertexArrayDirtyAttribMask;

    // We save one large buffer of binary data for the whole CPP replay.
    // This simplifies a lot of file management.
    FrameCaptureBinaryData mBinaryData;
    BinaryFileIndexInfo mIndexInfo;

    bool mEnabled;
    static bool mRuntimeEnabled;
    static bool mRuntimeInitialized;
    bool mSerializeStateEnabled;
    std::string mOutDirectory;
    std::string mCaptureLabel;
    bool mCompression;
    gl::AttribArray<const void *> mClientVertexArrayData;
    uint32_t mFrameIndex;
    uint32_t mCaptureStartFrame;
    uint32_t mCaptureEndFrame;
    bool mIsFirstFrame   = true;
    bool mWroteIndexFile = false;
    SurfaceParamsMap mDrawSurfaceParams;
    gl::AttribArray<size_t> mClientArraySizes;
    size_t mReadBufferSize;
    size_t mResourceIDBufferSize;
    HasResourceTypeMap mHasResourceType;
    ResourceIDToSetupCallsMap mResourceIDToSetupCalls;
    BufferDataMap mBufferDataMap;
    bool mValidateSerializedState = false;
    std::string mValidationExpression;
    PackedEnumMap<ResourceIDType, uint32_t> mMaxAccessedResourceIDs;
    std::map<ParamType, uint32_t> mMaxCLParamsSize;
    CoherentBufferTracker mCoherentBufferTracker;
    angle::SimpleMutex mFrameCaptureMutex;
    bool mCallCaptured           = false;
    bool mStartFrameCallCaptured = false;

    // Track Sync IDs emitted in the trace to allow skipping of external sync objects
    // to prevent corrupttion of Sync state tracking
    std::unordered_set<GLuint> mEmittedGLSyncIDs;
    std::unordered_set<GLuint> mEmittedEGLSyncIDs;

    // When true, it removes unnecessary calls going into
    // replay files that occur before mCaptureStartFrame
    bool removeUnneededOpenCLCalls = false;

#ifdef ANGLE_ENABLE_CL
    // OpenCL calls considered as "frames"
    std::unordered_set<EntryPoint> mCLEndFrameCalls = {EntryPoint::CLEnqueueNDRangeKernel,
                                                       EntryPoint::CLEnqueueNativeKernel,
                                                       EntryPoint::CLEnqueueTask};

    // "Optional" OpenCL calls not important for Capture/Replay
    std::unordered_set<EntryPoint> mCLOptionalCalls = {EntryPoint::CLGetPlatformInfo,
                                                       EntryPoint::CLGetDeviceInfo,
                                                       EntryPoint::CLGetContextInfo,
                                                       EntryPoint::CLGetCommandQueueInfo,
                                                       EntryPoint::CLGetProgramInfo,
                                                       EntryPoint::CLGetProgramBuildInfo,
                                                       EntryPoint::CLGetKernelInfo,
                                                       EntryPoint::CLGetKernelArgInfo,
                                                       EntryPoint::CLGetKernelWorkGroupInfo,
                                                       EntryPoint::CLGetEventInfo,
                                                       EntryPoint::CLGetEventProfilingInfo,
                                                       EntryPoint::CLGetMemObjectInfo,
                                                       EntryPoint::CLGetImageInfo,
                                                       EntryPoint::CLGetSamplerInfo,
                                                       EntryPoint::CLGetSupportedImageFormats};
    std::string mCLInfoJson;
    std::vector<std::string> mExtFuncsAdded;

    std::vector<CallCapture> mCLSetupCalls;

    ResourceTrackerCL mResourceTrackerCL;
#endif

    ResourceTracker mResourceTracker;
    ReplayWriter mReplayWriter;

    // If you don't know which frame you want to start capturing at, use the capture trigger.
    // Initialize it to the number of frames you want to capture, and then clear the value to 0 when
    // you reach the content you want to capture. Currently only available on Android.
    uint32_t mCaptureTrigger;

    // If you want to finish capture early, use the end_capture utility.
    uint32_t mEndCapture;

    bool mCaptureActive;
    std::vector<uint32_t> mActiveFrameIndices;

    // Cache most recently compiled and linked sources.
    ShaderSourceMap mCachedShaderSource;
    ProgramSourceMap mCachedProgramSources;

    // Set of programs which were created but not linked before capture was started
    std::set<gl::ShaderProgramID> mDeferredLinkPrograms;

    gl::ContextID mWindowSurfaceContextID;

    std::vector<CallCapture> mShareGroupSetupCalls;
    // Track which Contexts were created and made current at least once before MEC,
    // requiring setup for replay
    std::unordered_set<GLuint> mActiveContexts;

    // Invalid call counts per entry point while capture is active and inactive.
    std::unordered_map<EntryPoint, size_t> mInvalidCallCountsActive;
    std::unordered_map<EntryPoint, size_t> mInvalidCallCountsInactive;
};

template <typename CaptureFuncT, typename... ArgsT>
void CaptureGLCallToFrameCapture(CaptureFuncT captureFunc,
                                 bool isCallValid,
                                 gl::Context *context,
                                 ArgsT... captureParams)
{
    if (!FrameCaptureShared::isRuntimeEnabled())
    {
        // Return immediately to reduce overhead of compile-time flag
        return;
    }
    FrameCaptureShared *frameCaptureShared = context->getShareGroup()->getFrameCaptureShared();

    // Most but not all GL calls take the ContextMutex so we ensure thread-safe FrameCaptureShared
    // access by always taking the frame-capture mutex
    std::lock_guard<angle::SimpleMutex> lock(frameCaptureShared->getFrameCaptureMutex());

    if (!frameCaptureShared->isCapturing())
    {
        return;
    }

    CallCapture call = captureFunc(context->getState(), isCallValid, captureParams...);
    frameCaptureShared->captureCall(context, std::move(call), isCallValid);
}

template <typename FirstT, typename... OthersT>
egl::Display *GetEGLDisplayArg(FirstT display, OthersT... others)
{
    if constexpr (std::is_same<egl::Display *, FirstT>::value)
    {
        return display;
    }
    return nullptr;
}

template <typename CaptureFuncT, typename... ArgsT>
void CaptureEGLCallToFrameCapture(CaptureFuncT captureFunc,
                                  bool isCallValid,
                                  egl::Thread *thread,
                                  ArgsT... captureParams)
{
    if (!FrameCaptureShared::isRuntimeEnabled())
    {
        // Return immediately to reduce overhead of compile-time flag
        return;
    }
    gl::Context *context = thread->getContext();
    if (!context)
    {
        // Get a valid context from the display argument if no context is associated with this
        // thread
        egl::Display *display = GetEGLDisplayArg(captureParams...);
        if (display)
        {
            context = display->getState().contextMap.first();
        }
        if (!context)
        {
            return;
        }
    }

    // Take standard per-context EGL entry point lock
    std::lock_guard<egl::ContextMutex> lock(context->getContextMutex());

    angle::FrameCaptureShared *frameCaptureShared =
        context->getShareGroup()->getFrameCaptureShared();
    // Also take the the frame-capture mutex to cover the GL capture path
    std::lock_guard<angle::SimpleMutex> fcLock(frameCaptureShared->getFrameCaptureMutex());

    if (!frameCaptureShared->isCapturing())
    {
        return;
    }

    angle::CallCapture call = captureFunc(thread, isCallValid, captureParams...);
    frameCaptureShared->captureCall(context, std::move(call), true);
}

#ifdef ANGLE_ENABLE_CL
template <typename CaptureFuncT, typename... ArgsT>
void CaptureCLCallToFrameCapture(CaptureFuncT captureFunc, bool isCallValid, ArgsT... captureParams)
{
    if (!FrameCaptureShared::isRuntimeEnabled())
    {
        // Return immediately to reduce overhead of compile-time flag
        return;
    }
    angle::FrameCaptureShared *frameCaptureShared =
        cl::Platform::GetDefault()->getFrameCaptureShared();
    std::lock_guard<angle::SimpleMutex> lock(frameCaptureShared->getFrameCaptureMutex());
    if (!frameCaptureShared || !frameCaptureShared->isCapturing())
    {
        return;
    }
    angle::CallCapture call = captureFunc(isCallValid, captureParams...);
    frameCaptureShared->captureCLCall(std::move(call), isCallValid);
}
#endif

// Pointer capture helpers.
void CaptureMemory(const void *source, size_t size, ParamCapture *paramCapture);
void CaptureString(const GLchar *str, ParamCapture *paramCapture);
void CaptureStringLimit(const GLchar *str, uint32_t limit, ParamCapture *paramCapture);
void CaptureVertexPointerGLES1(const gl::State &glState,
                               gl::ClientVertexArrayType type,
                               const void *pointer,
                               ParamCapture *paramCapture);

gl::Program *GetProgramForCapture(const gl::State &glState, gl::ShaderProgramID handle);

// For GetIntegerv, GetFloatv, etc.
void CaptureGetParameter(const gl::State &glState,
                         GLenum pname,
                         size_t typeSize,
                         ParamCapture *paramCapture);

void CaptureGetActiveUniformBlockivParameters(const gl::State &glState,
                                              gl::ShaderProgramID programPacked,
                                              gl::UniformBlockIndex uniformBlockIndexPacked,
                                              gl::UniformBlockParameter pnamePacked,
                                              ParamCapture *paramCapture);

template <typename T>
void CaptureClearBufferValue(GLenum buffer, const T *value, ParamCapture *paramCapture)
{
    // Per the spec, color buffers have a vec4, the rest a single value
    uint32_t valueSize = (buffer == GL_COLOR) ? 4 : 1;
    CaptureMemory(value, valueSize * sizeof(T), paramCapture);
}

void CaptureGenHandlesImpl(GLsizei n, GLuint *handles, ParamCapture *paramCapture);

template <typename T>
void CaptureGenHandles(GLsizei n, T *handles, ParamCapture *paramCapture)
{
    paramCapture->dataNElements = n;
    CaptureGenHandlesImpl(n, reinterpret_cast<GLuint *>(handles), paramCapture);
}

template <typename T>
void CaptureArray(T *elements, GLsizei n, ParamCapture *paramCapture)
{
    paramCapture->dataNElements = n;
    CaptureMemory(elements, n * sizeof(T), paramCapture);
}

void CaptureShaderStrings(GLsizei count,
                          const GLchar *const *strings,
                          const GLint *length,
                          ParamCapture *paramCapture);

bool IsTrackedPerContext(ResourceIDType type);

// Function declarations & data types for both
// capturing OpenGL and OpenCL

std::string EscapeString(const std::string &string);

// Used to indicate that "shared" should be used to identify the files.
constexpr gl::ContextID kSharedContextId = {0};
// Used to indicate no context ID should be output.
constexpr gl::ContextID kNoContextId = {std::numeric_limits<uint32_t>::max()};

constexpr uint32_t kNoPartId = std::numeric_limits<uint32_t>::max();

std::ostream &operator<<(std::ostream &os, gl::ContextID contextId);

struct FmtCapturePrefix
{
    FmtCapturePrefix(gl::ContextID contextIdIn, const std::string &captureLabelIn)
        : contextId(contextIdIn), captureLabel(captureLabelIn)
    {}
    gl::ContextID contextId;
    const std::string &captureLabel;
};

std::ostream &operator<<(std::ostream &os, const FmtCapturePrefix &fmt);

// In C, when you declare or define a function that takes no parameters, you must explicitly say the
// function takes "void" parameters. When you're calling the function you omit this void. It's
// therefore necessary to know how we're using a function to know if we should emi the "void".
enum FuncUsage
{
    Prototype,
    Definition,
    Call,
};

std::ostream &operator<<(std::ostream &os, FuncUsage usage);

struct FmtReplayFunction
{
    FmtReplayFunction(gl::ContextID contextIdIn,
                      FuncUsage usageIn,
                      uint32_t frameIndexIn,
                      uint32_t partIdIn = kNoPartId)
        : contextId(contextIdIn), usage(usageIn), frameIndex(frameIndexIn), partId(partIdIn)
    {}
    gl::ContextID contextId;
    FuncUsage usage;
    uint32_t frameIndex;
    uint32_t partId;
};

std::ostream &operator<<(std::ostream &os, const FmtReplayFunction &fmt);

enum class ReplayFunc
{
    Replay,
    Setup,
    SetupInactive,
    Reset,
    SetupFirstFrame,
};

struct FmtFunction
{
    FmtFunction(ReplayFunc funcTypeIn,
                gl::ContextID contextIdIn,
                FuncUsage usageIn,
                uint32_t frameIndexIn,
                uint32_t partIdIn)
        : funcType(funcTypeIn),
          contextId(contextIdIn),
          usage(usageIn),
          frameIndex(frameIndexIn),
          partId(partIdIn)
    {}

    ReplayFunc funcType;
    gl::ContextID contextId;
    FuncUsage usage;
    uint32_t frameIndex;
    uint32_t partId;
};

std::ostream &operator<<(std::ostream &os, gl::ContextID contextId);

std::ostream &operator<<(std::ostream &os, const FmtFunction &fmt);

struct FmtSetupFunction
{
    FmtSetupFunction(uint32_t partIdIn, gl::ContextID contextIdIn, FuncUsage usageIn)
        : partId(partIdIn), contextId(contextIdIn), usage(usageIn)
    {}

    uint32_t partId;
    gl::ContextID contextId;
    FuncUsage usage;
};

std::ostream &operator<<(std::ostream &os, const FmtSetupFunction &fmt);

struct FmtSetupFirstFrameFunction
{
    FmtSetupFirstFrameFunction(uint32_t partIdIn = kNoPartId) : partId(partIdIn) {}

    uint32_t partId;
};

std::ostream &operator<<(std::ostream &os, const FmtSetupFirstFrameFunction &fmt);

struct FmtSetupInactiveFunction
{
    FmtSetupInactiveFunction(uint32_t partIdIn, gl::ContextID contextIdIn, FuncUsage usageIn)
        : partId(partIdIn), contextId(contextIdIn), usage(usageIn)
    {}

    uint32_t partId;
    gl::ContextID contextId;
    FuncUsage usage;
};

std::ostream &operator<<(std::ostream &os, const FmtSetupInactiveFunction &fmt);

struct FmtResetFunction
{
    FmtResetFunction(uint32_t partIdIn, gl::ContextID contextIdIn, FuncUsage usageIn)
        : partId(partIdIn), contextId(contextIdIn), usage(usageIn)
    {}

    uint32_t partId;
    gl::ContextID contextId;
    FuncUsage usage;
};

std::ostream &operator<<(std::ostream &os, const FmtResetFunction &fmt);

// For compatibility with C, which does not have multi-line string literals, we break strings up
// into multiple lines like:
//
//   const char *str[] = {
//   "multiple\n"
//   "line\n"
//   "strings may have \"quotes\"\n"
//   "and \\slashes\\\n",
//   };
//
// Note we need to emit extra escapes to ensure quotes and other special characters are preserved.
struct FmtMultiLineString
{
    FmtMultiLineString(const std::string &str) : strings()
    {
        std::string str2;

        // Strip any carriage returns before splitting, for consistency
        if (str.find("\r") != std::string::npos)
        {
            // str is const, so have to make a copy of it first
            str2 = str;
            ReplaceAllSubstrings(&str2, "\r", "");
        }

        strings =
            angle::SplitString(str2.empty() ? str : str2, "\n", WhitespaceHandling::KEEP_WHITESPACE,
                               SplitResult::SPLIT_WANT_ALL);
    }

    std::vector<std::string> strings;
};

std::ostream &operator<<(std::ostream &ostr, const FmtMultiLineString &fmt);

struct SaveFileHelper
{
  public:
    // We always use ios::binary to avoid inconsistent line endings when captured on Linux vs Win.
    SaveFileHelper(const std::string &filePathIn)
        : mOfs(filePathIn, std::ios::binary | std::ios::out), mFilePath(filePathIn)
    {
        if (!mOfs.is_open())
        {
            FATAL() << "Could not open " << filePathIn;
        }
    }
    ~SaveFileHelper() { printf("Saved '%s'.\n", mFilePath.c_str()); }

    template <typename T>
    SaveFileHelper &operator<<(const T &value)
    {
        mOfs << value;
        if (mOfs.bad())
        {
            FATAL() << "Error writing to " << mFilePath;
        }
        return *this;
    }

    void write(const uint8_t *data, size_t size)
    {
        mOfs.write(reinterpret_cast<const char *>(data), size);
    }

  private:
    void checkError();

    std::ofstream mOfs;
    std::string mFilePath;
};

constexpr size_t kFunctionSizeLimit = 5000;

// Limit based on MSVC Compiler Error C2026
constexpr size_t kStringLengthLimit = 16380;

// Default limit to number of bytes in a capture source files.
constexpr char kDefaultSourceFileExt[]           = "cpp";
constexpr size_t kDefaultSourceFileSizeThreshold = 400000;

void WriteCppReplayForCall(const CallCapture &call,
                           ReplayWriter &replayWriter,
                           std::ostream &out,
                           std::ostream &header,
                           FrameCaptureBinaryData *binaryData,
                           size_t *maxResourceIDBufferSize);

void WriteCppReplayForCallCL(const CallCapture &call,
                             ReplayWriter &replayWriter,
                             std::ostream &out,
                             std::ostream &header,
                             FrameCaptureBinaryData *binaryData);

void WriteBinaryParamReplay(ReplayWriter &replayWriter,
                            std::ostream &out,
                            std::ostream &header,
                            const CallCapture &call,
                            const ParamCapture &param,
                            FrameCaptureBinaryData *binaryData);

std::string GetBinaryDataFilePath(bool compression, const std::string &captureLabel);

void WriteStringPointerParamReplay(ReplayWriter &replayWriter,
                                   std::ostream &out,
                                   std::ostream &header,
                                   const CallCapture &call,
                                   const ParamCapture &param);

void WriteCppReplayFunctionWithParts(const gl::ContextID contextID,
                                     ReplayFunc replayFunc,
                                     ReplayWriter &replayWriter,
                                     uint32_t frameIndex,
                                     FrameCaptureBinaryData *binaryData,
                                     const std::vector<CallCapture> &calls,
                                     std::stringstream &header,
                                     std::stringstream &out,
                                     size_t *maxResourceIDBufferSize);

void WriteComment(std::ostream &out, const CallCapture &call);

template <typename T, typename CastT = T>
void WriteInlineData(const std::vector<uint8_t> &vec, std::ostream &out)
{
    const T *data = reinterpret_cast<const T *>(vec.data());
    size_t count  = vec.size() / sizeof(T);

    if (data == nullptr)
    {
        return;
    }

    out << static_cast<CastT>(data[0]);

    for (size_t dataIndex = 1; dataIndex < count; ++dataIndex)
    {
        out << ", " << static_cast<CastT>(data[dataIndex]);
    }
}

template <>
void WriteInlineData<GLchar>(const std::vector<uint8_t> &vec, std::ostream &out);

void AddComment(std::vector<CallCapture> *outCalls, const std::string &comment);

std::string GetCaptureTrigger();

std::string GetEndCapture();

}  // namespace angle

template <typename T>
void CaptureTextureAndSamplerParameter_params(GLenum pname,
                                              const T *param,
                                              angle::ParamCapture *paramCapture)
{
    if (pname == GL_TEXTURE_BORDER_COLOR || pname == GL_TEXTURE_CROP_RECT_OES)
    {
        CaptureMemory(param, sizeof(T) * 4, paramCapture);
    }
    else
    {
        CaptureMemory(param, sizeof(T), paramCapture);
    }
}

namespace egl
{
angle::ParamCapture CaptureAttributeMap(const egl::AttributeMap &attribMap);
}  // namespace egl

#endif  // LIBANGLE_FRAME_CAPTURE_H_
