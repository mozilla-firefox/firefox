/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HangDetails.h"

#include "nsIHangDetails.h"
#include "nsPrintfCString.h"
#include "js/Array.h"               // JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineElement
#include "mozilla/FileUtils.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/FileUtils.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/GfxMessageUtils.h"  // For ParamTraits<GeckoProcessType>
#include "mozilla/ResultExtensions.h"
#include "mozilla/SharedLibraries.h"
#include "mozilla/Try.h"

static const char MAGIC[] = "permahangsavev1";

namespace mozilla {

NS_IMETHODIMP
nsHangDetails::GetWasPersisted(bool* aWasPersisted) {
  *aWasPersisted = mPersistedToDisk == PersistedToDisk::Yes;
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetDuration(double* aDuration) {
  *aDuration = mDetails.duration().ToMilliseconds();
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetThread(nsACString& aName) {
  aName.Assign(mDetails.threadName());
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetRunnableName(nsACString& aRunnableName) {
  aRunnableName.Assign(mDetails.runnableName());
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetProcess(nsACString& aName) {
  aName.Assign(mDetails.process());
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetRemoteType(nsACString& aName) {
  aName.Assign(mDetails.remoteType());
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetAnnotations(JSContext* aCx,
                              JS::MutableHandle<JS::Value> aVal) {
  // We create an Array with ["key", "value"] string pair entries for each item
  // in our annotations object.
  auto& annotations = mDetails.annotations();
  size_t length = annotations.Length();
  JS::Rooted<JSObject*> retObj(aCx, JS::NewArrayObject(aCx, length));
  if (!retObj) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < length; ++i) {
    const auto& annotation = annotations[i];
    JS::Rooted<JSObject*> annotationPair(aCx, JS::NewArrayObject(aCx, 2));
    if (!annotationPair) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JSString*> key(aCx,
                              JS_NewUCStringCopyN(aCx, annotation.name().get(),
                                                  annotation.name().Length()));
    if (!key) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JSString*> value(
        aCx, JS_NewUCStringCopyN(aCx, annotation.value().get(),
                                 annotation.value().Length()));
    if (!value) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (!JS_DefineElement(aCx, annotationPair, 0, key, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (!JS_DefineElement(aCx, annotationPair, 1, value, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (!JS_DefineElement(aCx, retObj, i, annotationPair, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  aVal.setObject(*retObj);
  return NS_OK;
}

namespace {

nsresult StringFrame(JSContext* aCx, JS::RootedObject& aTarget, size_t aIndex,
                     const char* aString) {
  JSString* jsString = JS_NewStringCopyZ(aCx, aString);
  if (!jsString) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  JS::Rooted<JSString*> string(aCx, jsString);
  if (!string) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  if (!JS_DefineElement(aCx, aTarget, aIndex, string, JSPROP_ENUMERATE)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  return NS_OK;
}

}  // anonymous namespace

NS_IMETHODIMP
nsHangDetails::GetStack(JSContext* aCx, JS::MutableHandle<JS::Value> aStack) {
  auto& stack = mDetails.stack();
  uint32_t length = stack.stack().Length();
  JS::Rooted<JSObject*> ret(aCx, JS::NewArrayObject(aCx, length));
  if (!ret) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (uint32_t i = 0; i < length; ++i) {
    auto& entry = stack.stack()[i];
    switch (entry.type()) {
      case HangEntry::TnsCString: {
        nsresult rv = StringFrame(aCx, ret, i, entry.get_nsCString().get());
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryBufOffset: {
        uint32_t offset = entry.get_HangEntryBufOffset().index();

        // NOTE: We can't trust the offset we got, as we might have gotten it
        // from a compromised content process. Validate that it is in bounds.
        if (NS_WARN_IF(stack.strbuffer().IsEmpty() ||
                       offset >= stack.strbuffer().Length())) {
          MOZ_ASSERT_UNREACHABLE("Corrupted offset data");
          return NS_ERROR_FAILURE;
        }

        // NOTE: If our content process is compromised, it could send us back a
        // strbuffer() which didn't have a null terminator. If the last byte in
        // the buffer is not '\0', we abort, to make sure we don't read out of
        // bounds.
        if (stack.strbuffer().LastElement() != '\0') {
          MOZ_ASSERT_UNREACHABLE("Corrupted strbuffer data");
          return NS_ERROR_FAILURE;
        }

        // We know this offset is safe because of the previous checks.
        const int8_t* start = stack.strbuffer().Elements() + offset;
        nsresult rv =
            StringFrame(aCx, ret, i, reinterpret_cast<const char*>(start));
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryModOffset: {
        const HangEntryModOffset& mo = entry.get_HangEntryModOffset();

        JS::Rooted<JSObject*> jsFrame(aCx, JS::NewArrayObject(aCx, 2));
        if (!jsFrame) {
          return NS_ERROR_OUT_OF_MEMORY;
        }

        if (!JS_DefineElement(aCx, jsFrame, 0, mo.module(), JSPROP_ENUMERATE)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }

        nsPrintfCString hexString("%" PRIxPTR, (uintptr_t)mo.offset());
        JS::Rooted<JSString*> hex(aCx, JS_NewStringCopyZ(aCx, hexString.get()));
        if (!hex || !JS_DefineElement(aCx, jsFrame, 1, hex, JSPROP_ENUMERATE)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }

        if (!JS_DefineElement(aCx, ret, i, jsFrame, JSPROP_ENUMERATE)) {
          return NS_ERROR_OUT_OF_MEMORY;
        }
        break;
      }
      case HangEntry::THangEntryProgCounter: {
        // Don't bother recording fixed program counters to JS
        nsresult rv = StringFrame(aCx, ret, i, "(unresolved)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryContent: {
        nsresult rv = StringFrame(aCx, ret, i, "(content script)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryJit: {
        nsresult rv = StringFrame(aCx, ret, i, "(jit frame)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryWasm: {
        nsresult rv = StringFrame(aCx, ret, i, "(wasm)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntryChromeScript: {
        nsresult rv = StringFrame(aCx, ret, i, "(chrome script)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      case HangEntry::THangEntrySuppressed: {
        nsresult rv = StringFrame(aCx, ret, i, "(profiling suppressed)");
        NS_ENSURE_SUCCESS(rv, rv);
        break;
      }
      default:
        MOZ_CRASH("Unsupported HangEntry type?");
    }
  }

  aStack.setObject(*ret);
  return NS_OK;
}

NS_IMETHODIMP
nsHangDetails::GetModules(JSContext* aCx, JS::MutableHandle<JS::Value> aVal) {
  auto& modules = mDetails.stack().modules();
  size_t length = modules.Length();
  JS::Rooted<JSObject*> retObj(aCx, JS::NewArrayObject(aCx, length));
  if (!retObj) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (size_t i = 0; i < length; ++i) {
    const HangModule& module = modules[i];
    JS::Rooted<JSObject*> jsModule(aCx, JS::NewArrayObject(aCx, 2));
    if (!jsModule) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JSString*> name(
        aCx, JS_NewUCStringCopyN(aCx, module.name().BeginReading(),
                                 module.name().Length()));
    if (!JS_DefineElement(aCx, jsModule, 0, name, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    JS::Rooted<JSString*> breakpadId(
        aCx, JS_NewStringCopyN(aCx, module.breakpadId().BeginReading(),
                               module.breakpadId().Length()));
    if (!JS_DefineElement(aCx, jsModule, 1, breakpadId, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (!JS_DefineElement(aCx, retObj, i, jsModule, JSPROP_ENUMERATE)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }

  aVal.setObject(*retObj);
  return NS_OK;
}

// Processing and submitting the stack as an observer notification.

void nsHangDetails::Submit() {
  RefPtr<nsHangDetails> hangDetails = this;
  nsCOMPtr<nsIRunnable> notifyObservers =
      NS_NewRunnableFunction("NotifyBHRHangObservers", [hangDetails] {
        // The place we need to report the hang to varies depending on process.
        //
        // In child processes, we report the hang to our parent process, while
        // if we're in the parent process, we report a bhr-thread-hang observer
        // notification.
        switch (XRE_GetProcessType()) {
          case GeckoProcessType_Content: {
            auto cc = dom::ContentChild::GetSingleton();
            if (cc) {
              // Use the kind so we don't get URIs from Fission isolated
              // processes.
              hangDetails->mDetails.remoteType() =
                  cc->GetRemoteType().StringifyKind();
              (void)cc->SendBHRThreadHang(hangDetails->mDetails);
            }
            break;
          }
          case GeckoProcessType_GPU: {
            auto gp = gfx::GPUParent::GetSingleton();
            if (gp) {
              (void)gp->SendBHRThreadHang(hangDetails->mDetails);
            }
            break;
          }
          case GeckoProcessType_Default: {
            nsCOMPtr<nsIObserverService> os =
                mozilla::services::GetObserverService();
            if (os) {
              os->NotifyObservers(hangDetails, "bhr-thread-hang", nullptr);
            }
            break;
          }
          default:
            // XXX: Consider handling GeckoProcessType_GMPlugin and
            // GeckoProcessType_Plugin?
            NS_WARNING("Unsupported BHR process type - discarding hang.");
            break;
        }
      });

  nsresult rv = SchedulerGroup::Dispatch(notifyObservers.forget());
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
}

NS_IMPL_ISUPPORTS(nsHangDetails, nsIHangDetails)

namespace {

// Sorting comparator used by ReadModuleInformation. Sorts PC Frames by their
// PC.
struct PCFrameComparator {
  bool LessThan(HangEntry* const& a, HangEntry* const& b) const {
    return a->get_HangEntryProgCounter().pc() <
           b->get_HangEntryProgCounter().pc();
  }
  bool Equals(HangEntry* const& a, HangEntry* const& b) const {
    return a->get_HangEntryProgCounter().pc() ==
           b->get_HangEntryProgCounter().pc();
  }
};

}  // anonymous namespace

void ReadModuleInformation(HangStack& stack) {
  // modules() should be empty when we start filling it.
  stack.modules().Clear();

  // Create a sorted list of the PCs in the current stack.
  AutoTArray<HangEntry*, 100> frames;
  for (auto& frame : stack.stack()) {
    if (frame.type() == HangEntry::THangEntryProgCounter) {
      frames.AppendElement(&frame);
    }
  }
  PCFrameComparator comparator;
  frames.Sort(comparator);

  SharedLibraryInfo rawModules = SharedLibraryInfo::GetInfoForSelf();
  rawModules.SortByAddress();

  size_t frameIdx = 0;
  for (size_t i = 0; i < rawModules.GetSize(); ++i) {
    const SharedLibrary& info = rawModules.GetEntry(i);
    uintptr_t moduleStart = info.GetStart();
    uintptr_t moduleEnd = info.GetEnd() - 1;
    // the interval is [moduleStart, moduleEnd)

    bool moduleReferenced = false;
    for (; frameIdx < frames.Length(); ++frameIdx) {
      auto& frame = frames[frameIdx];
      uint64_t pc = frame->get_HangEntryProgCounter().pc();
      // We've moved past this frame, let's go to the next one.
      if (pc >= moduleEnd) {
        break;
      }
      if (pc >= moduleStart) {
        uint64_t offset = pc - moduleStart;
        if (NS_WARN_IF(offset > UINT32_MAX)) {
          continue;  // module/offset can only hold 32-bit offsets into shared
                     // libraries.
        }

        // If we found the module, rewrite the Frame entry to instead be a
        // ModOffset one. mModules.Length() will be the index of the module when
        // we append it below, and we set moduleReferenced to true to ensure
        // that we do.
        moduleReferenced = true;
        uint32_t module = stack.modules().Length();
        HangEntryModOffset modOffset(module, static_cast<uint32_t>(offset));
        *frame = modOffset;
      }
    }

    if (moduleReferenced) {
      HangModule module(NS_ConvertUTF8toUTF16(info.GetDebugName().c_str()),
                        nsCString(info.GetBreakpadId().c_str()));
      stack.modules().AppendElement(module);
    }
  }
}

Result<Ok, nsresult> ReadData(PRFileDesc* aFile, void* aPtr, size_t aLength) {
  int32_t readResult = PR_Read(aFile, aPtr, aLength);
  if (readResult < 0 || size_t(readResult) != aLength) {
    return Err(NS_ERROR_FAILURE);
  }
  return Ok();
}

Result<Ok, nsresult> WriteData(PRFileDesc* aFile, void* aPtr, size_t aLength) {
  int32_t writeResult = PR_Write(aFile, aPtr, aLength);
  if (writeResult < 0 || size_t(writeResult) != aLength) {
    return Err(NS_ERROR_FAILURE);
  }
  return Ok();
}

Result<Ok, nsresult> WriteUint(PRFileDesc* aFile, const CheckedUint32& aInt) {
  if (!aInt.isValid()) {
    MOZ_ASSERT_UNREACHABLE("Integer value out of bounds.");
    return Err(NS_ERROR_UNEXPECTED);
  }
  int32_t value = aInt.value();
  MOZ_TRY(WriteData(aFile, (void*)&value, sizeof(value)));
  return Ok();
}

Result<uint32_t, nsresult> ReadUint(PRFileDesc* aFile) {
  int32_t value;
  MOZ_TRY(ReadData(aFile, (void*)&value, sizeof(value)));
  return value;
}

Result<Ok, nsresult> WriteCString(PRFileDesc* aFile, const char* aString) {
  size_t length = strlen(aString);
  MOZ_TRY(WriteUint(aFile, CheckedUint32(length)));
  MOZ_TRY(WriteData(aFile, (void*)aString, length));
  return Ok();
}

template <typename CharT>
Result<Ok, nsresult> WriteTString(PRFileDesc* aFile,
                                  const nsTString<CharT>& aString) {
  MOZ_TRY(WriteUint(aFile, CheckedUint32(aString.Length())));
  size_t size = aString.Length() * sizeof(CharT);
  MOZ_TRY(WriteData(aFile, (void*)aString.get(), size));
  return Ok();
}

template <typename CharT>
Result<nsTString<CharT>, nsresult> ReadTString(PRFileDesc* aFile) {
  uint32_t length = MOZ_TRY(ReadUint(aFile));
  nsTString<CharT> result;
  CharT buffer[512];
  size_t bufferLength = sizeof(buffer) / sizeof(CharT);
  while (length != 0) {
    size_t toRead = std::min(bufferLength, size_t(length));
    size_t toReadSize = toRead * sizeof(CharT);
    MOZ_TRY(ReadData(aFile, (void*)buffer, toReadSize));

    if (!result.Append(buffer, toRead, mozilla::fallible)) {
      return Err(NS_ERROR_FAILURE);
    }

    if (length > bufferLength) {
      length -= bufferLength;
    } else {
      length = 0;
    }
  }
  return result;
}

Result<Ok, nsresult> WriteEntry(PRFileDesc* aFile, const HangStack& aStack,
                                const HangEntry& aEntry) {
  MOZ_TRY(WriteUint(aFile, uint32_t(aEntry.type())));
  switch (aEntry.type()) {
    case HangEntry::TnsCString: {
      MOZ_TRY(WriteTString(aFile, aEntry.get_nsCString()));
      break;
    }
    case HangEntry::THangEntryBufOffset: {
      uint32_t offset = aEntry.get_HangEntryBufOffset().index();

      if (NS_WARN_IF(aStack.strbuffer().IsEmpty() ||
                     offset >= aStack.strbuffer().Length())) {
        MOZ_ASSERT_UNREACHABLE("Corrupted offset data");
        return Err(NS_ERROR_FAILURE);
      }

      if (aStack.strbuffer().LastElement() != '\0') {
        MOZ_ASSERT_UNREACHABLE("Corrupted strbuffer data");
        return Err(NS_ERROR_FAILURE);
      }

      const char* start = (const char*)aStack.strbuffer().Elements() + offset;
      MOZ_TRY(WriteCString(aFile, start));
      break;
    }
    case HangEntry::THangEntryModOffset: {
      const HangEntryModOffset& mo = aEntry.get_HangEntryModOffset();

      MOZ_TRY(WriteUint(aFile, CheckedUint32(mo.module())));
      MOZ_TRY(WriteUint(aFile, CheckedUint32(mo.offset())));
      break;
    }
    case HangEntry::THangEntryProgCounter:
    case HangEntry::THangEntryContent:
    case HangEntry::THangEntryJit:
    case HangEntry::THangEntryWasm:
    case HangEntry::THangEntryChromeScript:
    case HangEntry::THangEntrySuppressed: {
      break;
    }
    default:
      MOZ_CRASH("Unsupported HangEntry type?");
  }
  return Ok();
}

Result<Ok, nsresult> ReadEntry(PRFileDesc* aFile, HangStack& aStack) {
  uint32_t type = MOZ_TRY(ReadUint(aFile));
  HangEntry::Type entryType = HangEntry::Type(type);
  switch (entryType) {
    case HangEntry::TnsCString:
    case HangEntry::THangEntryBufOffset: {
      nsCString str = MOZ_TRY(ReadTString<char>(aFile));
      aStack.stack().AppendElement(std::move(str));
      break;
    }
    case HangEntry::THangEntryModOffset: {
      uint32_t module = MOZ_TRY(ReadUint(aFile));
      uint32_t offset = MOZ_TRY(ReadUint(aFile));
      aStack.stack().AppendElement(HangEntryModOffset(module, offset));
      break;
    }
    case HangEntry::THangEntryProgCounter: {
      aStack.stack().AppendElement(HangEntryProgCounter());
      break;
    }
    case HangEntry::THangEntryContent: {
      aStack.stack().AppendElement(HangEntryContent());
      break;
    }
    case HangEntry::THangEntryJit: {
      aStack.stack().AppendElement(HangEntryJit());
      break;
    }
    case HangEntry::THangEntryWasm: {
      aStack.stack().AppendElement(HangEntryWasm());
      break;
    }
    case HangEntry::THangEntryChromeScript: {
      aStack.stack().AppendElement(HangEntryChromeScript());
      break;
    }
    case HangEntry::THangEntrySuppressed: {
      aStack.stack().AppendElement(HangEntrySuppressed());
      break;
    }
    default:
      return Err(NS_ERROR_UNEXPECTED);
  }
  return Ok();
}

Result<HangDetails, nsresult> ReadHangDetailsFromFile(nsIFile* aFile) {
  AutoFDClose raiiFd;
  nsresult rv =
      aFile->OpenNSPRFileDesc(PR_RDONLY, 0644, getter_Transfers(raiiFd));
  const auto fd = raiiFd.get();
  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  uint8_t magicBuffer[sizeof(MAGIC)];
  MOZ_TRY(ReadData(fd, (void*)magicBuffer, sizeof(MAGIC)));

  if (memcmp(magicBuffer, MAGIC, sizeof(MAGIC)) != 0) {
    return Err(NS_ERROR_FAILURE);
  }

  HangDetails result;
  uint32_t duration = MOZ_TRY(ReadUint(fd));
  result.duration() = TimeDuration::FromMilliseconds(duration);
  result.threadName() = MOZ_TRY(ReadTString<char>(fd));
  result.runnableName() = MOZ_TRY(ReadTString<char>(fd));
  result.process() = MOZ_TRY(ReadTString<char>(fd));
  result.remoteType() = MOZ_TRY(ReadTString<char>(fd));

  uint32_t numAnnotations = MOZ_TRY(ReadUint(fd));
  auto& annotations = result.annotations();

  // Add a "Unrecovered" annotation so we can know when processing this that
  // the hang persisted until the process was closed.
  if (!annotations.SetCapacity(numAnnotations + 1, mozilla::fallible)) {
    return Err(NS_ERROR_FAILURE);
  }
  annotations.AppendElement(HangAnnotation(u"Unrecovered"_ns, u"true"_ns));

  for (size_t i = 0; i < numAnnotations; ++i) {
    HangAnnotation annot;
    annot.name() = MOZ_TRY(ReadTString<char16_t>(fd));
    annot.value() = MOZ_TRY(ReadTString<char16_t>(fd));
    annotations.AppendElement(std::move(annot));
  }

  auto& stack = result.stack();
  uint32_t numFrames = MOZ_TRY(ReadUint(fd));
  if (!stack.stack().SetCapacity(numFrames, mozilla::fallible)) {
    return Err(NS_ERROR_FAILURE);
  }

  for (size_t i = 0; i < numFrames; ++i) {
    MOZ_TRY(ReadEntry(fd, stack));
  }

  uint32_t numModules = MOZ_TRY(ReadUint(fd));
  auto& modules = stack.modules();
  if (!annotations.SetCapacity(numModules, mozilla::fallible)) {
    return Err(NS_ERROR_FAILURE);
  }

  for (size_t i = 0; i < numModules; ++i) {
    HangModule module;
    module.name() = MOZ_TRY(ReadTString<char16_t>(fd));
    module.breakpadId() = MOZ_TRY(ReadTString<char>(fd));
    modules.AppendElement(std::move(module));
  }

  return result;
}

Result<Ok, nsresult> WriteHangDetailsToFile(HangDetails& aDetails,
                                            nsIFile* aFile) {
  if (NS_WARN_IF(!aFile)) {
    return Err(NS_ERROR_INVALID_POINTER);
  }

  AutoFDClose raiiFd;
  nsresult rv = aFile->OpenNSPRFileDesc(
      PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE, 0644, getter_Transfers(raiiFd));
  const auto fd = raiiFd.get();

  if (NS_FAILED(rv)) {
    return Err(rv);
  }

  MOZ_TRY(WriteData(fd, (void*)MAGIC, sizeof(MAGIC)));

  double duration = aDetails.duration().ToMilliseconds();
  if (duration > double(std::numeric_limits<uint32_t>::max())) {
    // Something has gone terribly wrong if we've hung for more than 2^32 ms.
    return Err(NS_ERROR_FAILURE);
  }

  MOZ_TRY(WriteUint(fd, uint32_t(duration)));
  MOZ_TRY(WriteTString(fd, aDetails.threadName()));
  MOZ_TRY(WriteTString(fd, aDetails.runnableName()));
  MOZ_TRY(WriteTString(fd, aDetails.process()));
  MOZ_TRY(WriteTString(fd, aDetails.remoteType()));
  MOZ_TRY(WriteUint(fd, CheckedUint32(aDetails.annotations().Length())));

  for (auto& annot : aDetails.annotations()) {
    MOZ_TRY(WriteTString(fd, annot.name()));
    MOZ_TRY(WriteTString(fd, annot.value()));
  }

  auto& stack = aDetails.stack();
  ReadModuleInformation(stack);

  MOZ_TRY(WriteUint(fd, CheckedUint32(stack.stack().Length())));
  for (auto& entry : stack.stack()) {
    MOZ_TRY(WriteEntry(fd, stack, entry));
  }

  auto& modules = stack.modules();
  MOZ_TRY(WriteUint(fd, CheckedUint32(modules.Length())));

  for (auto& module : modules) {
    MOZ_TRY(WriteTString(fd, module.name()));
    MOZ_TRY(WriteTString(fd, module.breakpadId()));
  }

  return Ok();
}

NS_IMETHODIMP
ProcessHangStackRunnable::Run() {
  PersistedToDisk persistedToDisk = PersistedToDisk::No;
  if (mPermahangFile &&
      WriteHangDetailsToFile(mHangDetails, mPermahangFile).isOk()) {
    persistedToDisk = PersistedToDisk::Yes;
  }

  // NOTE: Reading module information can take a long time, which is why we do
  // it off-main-thread.
  if (mHangDetails.stack().modules().IsEmpty()) {
    ReadModuleInformation(mHangDetails.stack());
  }

  RefPtr<nsHangDetails> hangDetails =
      new nsHangDetails(std::move(mHangDetails), persistedToDisk);
  hangDetails->Submit();

  return NS_OK;
}

NS_IMETHODIMP
SubmitPersistedPermahangRunnable::Run() {
  auto hangDetailsResult = ReadHangDetailsFromFile(mPermahangFile);
  if (hangDetailsResult.isErr()) {
    // If we somehow failed in trying to deserialize the hang file, go ahead
    // and delete it to prevent future runs from having to go through the
    // same thing. If we succeeded, however, the file should be cleaned up
    // once the hang is submitted.
    (void)mPermahangFile->Remove(false);
    return hangDetailsResult.unwrapErr();
  }
  RefPtr<nsHangDetails> hangDetails =
      new nsHangDetails(hangDetailsResult.unwrap(), PersistedToDisk::Yes);
  hangDetails->Submit();

  return NS_OK;
}

}  // namespace mozilla
