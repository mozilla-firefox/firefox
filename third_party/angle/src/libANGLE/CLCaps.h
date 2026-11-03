//
// Copyright 2026 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#ifndef LIBANGLE_CLCAPS_H_
#define LIBANGLE_CLCAPS_H_

#include <angle_cl.h>
#include "CL/cl_platform.h"

#include <string>

namespace cl
{

struct DeviceCaps
{
    DeviceCaps() = default;

    cl_uint vendorID                            = 0u;
    cl_uint maxComputeUnits                     = 0u;
    cl_uint maxWorkItemDimensions               = 0u;
    cl_uint preferredVectorWidthChar            = 0u;
    cl_uint preferredVectorWidthShort           = 0u;
    cl_uint preferredVectorWidthInt             = 0u;
    cl_uint preferredVectorWidthLong            = 0u;
    cl_uint preferredVectorWidthFloat           = 0u;
    cl_uint preferredVectorWidthDouble          = 0u;
    cl_uint preferredVectorWidthHalf            = 0u;
    cl_uint nativeVectorWidthChar               = 0u;
    cl_uint nativeVectorWidthShort              = 0u;
    cl_uint nativeVectorWidthInt                = 0u;
    cl_uint nativeVectorWidthLong               = 0u;
    cl_uint nativeVectorWidthFloat              = 0u;
    cl_uint nativeVectorWidthDouble             = 0u;
    cl_uint nativeVectorWidthHalf               = 0u;
    cl_uint maxClockFrequency                   = 0u;
    cl_uint addressBits                         = 0u;
    cl_uint maxReadImageArgs                    = 0u;
    cl_uint maxWriteImageArgs                   = 0u;
    cl_uint maxReadWriteImageArgs               = 0u;
    cl_uint maxSamplers                         = 0u;
    cl_uint maxPipeArgs                         = 0u;
    cl_uint pipeMaxActiveReservations           = 0u;
    cl_uint pipeMaxPacketSize                   = 0u;
    cl_uint minDataTypeAlignSize                = 0u;
    cl_uint globalMemCacheType                  = 0u;
    cl_uint globalMemCachelineSize              = 0u;
    cl_uint maxConstantArgs                     = 0u;
    cl_uint localMemType                        = 0u;
    cl_uint errorCorrectionSupport              = 0u;
    cl_uint hostUnifiedMemory                   = 0u;
    cl_uint endianLittle                        = 0u;
    cl_uint available                           = 0u;
    cl_uint compilerAvailable                   = 0u;
    cl_uint linkerAvailable                     = 0u;
    cl_uint queueOnDevicePreferredSize          = 0u;
    cl_uint queueOnDeviceMaxSize                = 0u;
    cl_uint maxOnDeviceQueues                   = 0u;
    cl_uint maxOnDeviceEvents                   = 0u;
    cl_uint preferredInteropUserSync            = 0u;
    cl_uint partitionMaxSubDevices              = 0u;
    cl_uint preferredPlatformAtomicAlignment    = 0u;
    cl_uint preferredGlobalAtomicAlignment      = 0u;
    cl_uint preferredLocalAtomicAlignment       = 0u;
    cl_uint maxNumSubGroups                     = 0u;
    cl_uint subGroupIndependentForwardProgress  = 0u;
    cl_uint nonUniformWorkGroupSupport          = 0u;
    cl_uint workGroupCollectiveFunctionsSupport = 0u;
    cl_uint genericAddressSpaceSupport          = 0u;
    cl_uint pipeSupport                         = 0u;

    cl_ulong singleFpConfig            = 0u;
    cl_ulong doubleFpConfig            = 0u;
    cl_ulong globalMemCacheSize        = 0u;
    cl_ulong globalMemSize             = 0u;
    cl_ulong maxConstantBufferSize     = 0u;
    cl_ulong localMemSize              = 0u;
    cl_ulong executionCapabilities     = 0u;
    cl_ulong queueOnHostProperties     = 0u;
    cl_ulong queueOnDeviceProperties   = 0u;
    cl_ulong partitionAffinityDomain   = 0u;
    cl_ulong sVM_Capabilities          = 0u;
    cl_ulong atomicMemoryCapabilities  = 0u;
    cl_ulong atomicFenceCapabilities   = 0u;
    cl_ulong deviceEnqueueCapabilities = 0u;
    cl_ulong halfFpConfig              = 0u;
    cl_ulong maxMemAllocSize           = 0u;

    size_t maxWorkGroupSize                 = 0u;
    size_t maxParameterSize                 = 0u;
    size_t maxGlobalVariableSize            = 0u;
    size_t globalVariablePreferredTotalSize = 0u;
    size_t profilingTimerResolution         = 0u;
    size_t printfBufferSize                 = 0u;
    size_t preferredWorkGroupSizeMultiple   = 0u;

    std::string name                           = "";
    std::string vendor                         = "";
    std::string driverVersion                  = "";
    std::string profile                        = "";
    std::string openCL_C_Version               = "";
    std::string latestConformanceVersionPassed = "";
    std::string version                        = "";
};

}  // namespace cl

#endif  // LIBANGLE_CLCAPS_H_
