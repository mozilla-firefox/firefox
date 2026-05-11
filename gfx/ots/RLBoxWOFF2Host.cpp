/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RLBoxWOFF2Host.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/RLBoxUtils.h"
#include "mozilla/ScopeExit.h"
#include "opentype-sanitiser.h"  // For ots_ntohl

using namespace rlbox;
using namespace mozilla;

tainted_woff2<BrotliDecoderResult> RLBoxBrotliDecoderDecompressCallback(
    rlbox_sandbox_woff2& aSandbox, tainted_woff2<unsigned long> aEncodedSize,
    tainted_woff2<const char*> aEncodedBuffer,
    tainted_woff2<unsigned long*> aDecodedSize,
    tainted_woff2<char*> aDecodedBuffer) {
  if (!aEncodedBuffer || !aDecodedSize || !aDecodedBuffer) {
    return BROTLI_DECODER_RESULT_ERROR;
  }

  // We don't create temporary buffers for brotli to operate on. Instead we
  // pass a pointer to the in (encoded) and out (decoded) buffers. We check
  // (specifically, unverified_safe_pointer checks) that the buffers are within
  // the sandbox boundary (for the given sizes).

  size_t encodedSize =
      aEncodedSize.unverified_safe_because("Any size within sandbox is ok.");
  const uint8_t* encodedBuffer =
      rlbox::sandbox_reinterpret_cast<const uint8_t*>(aEncodedBuffer)
          .unverified_safe_pointer_because(encodedSize,
                                           "Pointer fits within sandbox");

  size_t decodedSize =
      (*aDecodedSize).unverified_safe_because("Any size within sandbox is ok.");
  uint8_t* decodedBuffer =
      rlbox::sandbox_reinterpret_cast<uint8_t*>(aDecodedBuffer)
          .unverified_safe_pointer_because(decodedSize,
                                           "Pointer fits within sandbox");

  BrotliDecoderResult res = BrotliDecoderDecompress(
      encodedSize, encodedBuffer, &decodedSize, decodedBuffer);

  *aDecodedSize = decodedSize;

  return res;
}

UniquePtr<RLBoxSandboxDataBase> RLBoxWOFF2SandboxPool::CreateSandboxData(
    uint64_t aSize) {
  // Create woff2 sandbox
  auto sandbox = MakeUnique<rlbox_sandbox_woff2>();

#if defined(MOZ_WASM_SANDBOXING_WOFF2)
  const w2c_mem_capacity capacity =
      get_valid_wasm2c_memory_capacity(aSize, true /* 32-bit wasm memory*/);
  bool createOK = sandbox->create_sandbox(/* shouldAbortOnFailure = */ false,
                                          &capacity, "rlbox_wasm2c_woff2");
#else
  bool createOK = sandbox->create_sandbox();
#endif
  NS_ENSURE_TRUE(createOK, nullptr);

  UniquePtr<RLBoxWOFF2SandboxData> sbxData =
      MakeUnique<RLBoxWOFF2SandboxData>(aSize, std::move(sandbox));

  // Register brotli callback
  sbxData->mDecompressCallback = sbxData->Sandbox()->register_callback(
      RLBoxBrotliDecoderDecompressCallback);
  sbxData->Sandbox()->invoke_sandbox_function(RegisterWOFF2Callback,
                                              sbxData->mDecompressCallback);

  return sbxData;
}

StaticRefPtr<RLBoxWOFF2SandboxPool> RLBoxWOFF2SandboxPool::sSingleton;

void RLBoxWOFF2SandboxPool::Initalize(size_t aDelaySeconds) {
  AssertIsOnMainThread();
  RLBoxWOFF2SandboxPool::sSingleton = new RLBoxWOFF2SandboxPool(aDelaySeconds);
  ClearOnShutdown(&RLBoxWOFF2SandboxPool::sSingleton);
}

RLBoxWOFF2SandboxData::RLBoxWOFF2SandboxData(
    uint64_t aSize, mozilla::UniquePtr<rlbox_sandbox_woff2> aSandbox)
    : mozilla::RLBoxSandboxDataBase(aSize), mSandbox(std::move(aSandbox)) {
  MOZ_COUNT_CTOR(RLBoxWOFF2SandboxData);
}

RLBoxWOFF2SandboxData::~RLBoxWOFF2SandboxData() {
  MOZ_ASSERT(mSandbox);
  mDecompressCallback.unregister();
  mSandbox->destroy_sandbox();
  MOZ_COUNT_DTOR(RLBoxWOFF2SandboxData);
}

static bool Woff2SizeValidator(size_t aLength, size_t aSize, size_t aLimit) {
  if (aSize < aLength) {
    NS_WARNING("Size of decompressed WOFF 2.0 is less than compressed size");
    return false;
  } else if (aSize == 0) {
    NS_WARNING("Size of decompressed WOFF 2.0 is set to 0");
    return false;
  } else if (aSize > aLimit) {
    NS_WARNING(
        nsPrintfCString("Size of decompressed WOFF 2.0 font exceeds %gMB",
                        aLimit / (1024.0 * 1024.0))
            .get());
    return false;
  }
  return true;
}

// Code replicated from modules/woff2/src/woff2_dec.cc
// This is used both to compute the expected size of the Woff2 RLBox sandbox
// as well as internally by WOFF2 as a performance hint
static uint32_t ComputeWOFF2FinalSize(const uint8_t* aData, size_t aLength,
                                      size_t aLimit) {
  // Expected size is stored as a 4 byte value starting from the 17th byte
  if (aLength < 20) {
    return 0;
  }

  uint32_t decompressedSize = 0;
  const void* location = &(aData[16]);
  std::memcpy(&decompressedSize, location, sizeof(decompressedSize));
  decompressedSize = ots_ntohl(decompressedSize);

  // We bump the decompressedSize slightly because it seems that some fonts
  // have an incorrectly-set value that results in decompression failure.
  // (See https://bugzilla.mozilla.org/show_bug.cgi?id=1934606, and original
  // discussion in https://github.com/harfbuzz/harfbuzz/issues/4962.)
  decompressedSize =
      std::max(decompressedSize, decompressedSize + (decompressedSize >> 4));

  if (!Woff2SizeValidator(aLength, decompressedSize, aLimit)) {
    return 0;
  }

  return decompressedSize;
}

template <typename T>
using TransferBufferToWOFF2 =
    mozilla::RLBoxTransferBufferToSandbox<T, rlbox_woff2_sandbox_type>;
template <typename T>
using WOFF2Alloc = mozilla::RLBoxAllocateInSandbox<T, rlbox_woff2_sandbox_type>;

bool RLBoxProcessWOFF2(ots::FontFile* aHeader, ots::OTSStream* aOutput,
                       const uint8_t* aData, size_t aLength, uint32_t aIndex,
                       ProcessTTCFunc* aProcessTTC,
                       ProcessTTFFunc* aProcessTTF) {
  MOZ_ASSERT(aProcessTTC);
  MOZ_ASSERT(aProcessTTF);

  // We index into aData before processing it (very end of this function). Our
  // validator ensures that the untrusted size is greater than aLength, so we
  // just need to conservatively ensure that aLength is greater than the highest
  // index (7).
  NS_ENSURE_TRUE(aLength >= 8, false);

  size_t limit =
      std::min(size_t(OTS_MAX_DECOMPRESSED_FILE_SIZE), aOutput->size());
  uint32_t expectedSize = ComputeWOFF2FinalSize(aData, aLength, limit);
  NS_ENSURE_TRUE(expectedSize > 0, false);

  // The sandbox should have space for the input, output and misc allocations
  // To account for misc allocations, we'll set the sandbox size to:
  // twice the size of (input + output)

  const uint64_t expectedSandboxSize =
      static_cast<uint64_t>(2 * (aLength + expectedSize));
  auto sandboxPoolData =
      RLBoxWOFF2SandboxPool::sSingleton->PopOrCreate(expectedSandboxSize);
  NS_ENSURE_TRUE(sandboxPoolData, false);

  const auto* sandboxData =
      static_cast<const RLBoxWOFF2SandboxData*>(sandboxPoolData->SandboxData());
  MOZ_ASSERT(sandboxData);

  auto* sandbox = sandboxData->Sandbox();

  // Transfer aData into the sandbox.

  auto data = TransferBufferToWOFF2<char>(
      sandbox, reinterpret_cast<const char*>(aData), aLength);
  NS_ENSURE_TRUE(*data, false);

  // Perform the actual conversion to TTF.

  auto sizep = WOFF2Alloc<unsigned long>(sandbox);
  auto bufp = WOFF2Alloc<char*>(sandbox);
  auto bufOwnerString =
      WOFF2Alloc<void*>(sandbox);  // pointer to string that owns the bufer

  if (!sandbox
           ->invoke_sandbox_function(RLBoxConvertWOFF2ToTTF, *data, aLength,
                                     expectedSize, sizep.get(),
                                     bufOwnerString.get(), bufp.get())
           .unverified_safe_because(
               "The ProcessTT* functions validate the decompressed data.")) {
    return false;
  }

  auto bufCleanup = mozilla::MakeScopeExit([&sandbox, &bufOwnerString] {
    // Delete the string created by RLBoxConvertWOFF2ToTTF.
    sandbox->invoke_sandbox_function(RLBoxDeleteWOFF2String,
                                     bufOwnerString.get());
  });

  // Get the actual decompression size and validate it.
  // We need to validate the size again. RLBoxConvertWOFF2ToTTF works even if
  // the computed size (with ComputeWOFF2FinalSize) is wrong, so we can't
  // trust the expectedSize to be the same as size sizep.
  bool validateOK = false;
  unsigned long actualSize =
      (*sizep.get()).copy_and_verify([&](unsigned long val) {
        validateOK = Woff2SizeValidator(aLength, val, limit);
        return val;
      });

  NS_ENSURE_TRUE(validateOK, false);

  const uint8_t* decompressed = reinterpret_cast<const uint8_t*>(
      (*bufp.get())
          .unverified_safe_pointer_because(
              actualSize,
              "Only care that the buffer is within sandbox boundary."));

  // Since ProcessTT* memcpy from the buffer, make sure it's not null.
  NS_ENSURE_TRUE(decompressed, false);

  if (aData[4] == 't' && aData[5] == 't' && aData[6] == 'c' &&
      aData[7] == 'f') {
    return aProcessTTC(aHeader, aOutput, decompressed, actualSize, aIndex);
  }
  ots::Font font(aHeader);
  return aProcessTTF(aHeader, &font, aOutput, decompressed, actualSize, 0);
}
