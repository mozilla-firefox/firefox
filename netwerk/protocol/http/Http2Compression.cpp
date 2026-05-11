/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include "Http2Compression.h"
#include "Http2HuffmanIncoming.h"
#include "Http2HuffmanOutgoing.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsIMemoryReporter.h"
#include "nsHttpHandler.h"

namespace mozilla {
namespace net {

static nsDeque<nvPair>* gStaticHeaders = nullptr;

class HpackStaticTableReporter final : public nsIMemoryReporter {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  HpackStaticTableReporter() = default;

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    MOZ_COLLECT_REPORT("explicit/network/hpack/static-table", KIND_HEAP,
                       UNITS_BYTES,
                       gStaticHeaders->SizeOfIncludingThis(MallocSizeOf),
                       "Memory usage of HPACK static table.");

    return NS_OK;
  }

 private:
  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)

  ~HpackStaticTableReporter() = default;
};

NS_IMPL_ISUPPORTS(HpackStaticTableReporter, nsIMemoryReporter)

class HpackDynamicTableReporter final : public nsIMemoryReporter {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit HpackDynamicTableReporter(Http2BaseCompressor* aCompressor)
      : mCompressor(aCompressor) {}

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override {
    MutexAutoLock lock(mMutex);
    if (mCompressor) {
      MOZ_COLLECT_REPORT("explicit/network/hpack/dynamic-tables", KIND_HEAP,
                         UNITS_BYTES,
                         mCompressor->SizeOfExcludingThis(MallocSizeOf),
                         "Aggregate memory usage of HPACK dynamic tables.");
    }
    return NS_OK;
  }

 private:
  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)

  ~HpackDynamicTableReporter() = default;

  Mutex mMutex{"HpackDynamicTableReporter"};
  Http2BaseCompressor* mCompressor MOZ_GUARDED_BY(mMutex);

  friend class Http2BaseCompressor;
};

NS_IMPL_ISUPPORTS(HpackDynamicTableReporter, nsIMemoryReporter)

StaticRefPtr<HpackStaticTableReporter> gStaticReporter;

void Http2CompressionCleanup() {
  // this happens after the socket thread has been destroyed
  delete gStaticHeaders;
  gStaticHeaders = nullptr;
  UnregisterStrongMemoryReporter(gStaticReporter);
  gStaticReporter = nullptr;
}

static void AddStaticElement(const nsCString& name, const nsCString& value) {
  nvPair* pair = new nvPair(name, value);
  gStaticHeaders->Push(pair);
}

static void AddStaticElement(const nsCString& name) {
  AddStaticElement(name, ""_ns);
}

static void InitializeStaticHeaders() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!gStaticHeaders) {
    gStaticHeaders = new nsDeque<nvPair>();
    gStaticReporter = new HpackStaticTableReporter();
    RegisterStrongMemoryReporter(do_AddRef(gStaticReporter));
    AddStaticElement(":authority"_ns);
    AddStaticElement(":method"_ns, "GET"_ns);
    AddStaticElement(":method"_ns, "POST"_ns);
    AddStaticElement(":path"_ns, "/"_ns);
    AddStaticElement(":path"_ns, "/index.html"_ns);
    AddStaticElement(":scheme"_ns, "http"_ns);
    AddStaticElement(":scheme"_ns, "https"_ns);
    AddStaticElement(":status"_ns, "200"_ns);
    AddStaticElement(":status"_ns, "204"_ns);
    AddStaticElement(":status"_ns, "206"_ns);
    AddStaticElement(":status"_ns, "304"_ns);
    AddStaticElement(":status"_ns, "400"_ns);
    AddStaticElement(":status"_ns, "404"_ns);
    AddStaticElement(":status"_ns, "500"_ns);
    AddStaticElement("accept-charset"_ns);
    AddStaticElement("accept-encoding"_ns, "gzip, deflate"_ns);
    AddStaticElement("accept-language"_ns);
    AddStaticElement("accept-ranges"_ns);
    AddStaticElement("accept"_ns);
    AddStaticElement("access-control-allow-origin"_ns);
    AddStaticElement("age"_ns);
    AddStaticElement("allow"_ns);
    AddStaticElement("authorization"_ns);
    AddStaticElement("cache-control"_ns);
    AddStaticElement("content-disposition"_ns);
    AddStaticElement("content-encoding"_ns);
    AddStaticElement("content-language"_ns);
    AddStaticElement("content-length"_ns);
    AddStaticElement("content-location"_ns);
    AddStaticElement("content-range"_ns);
    AddStaticElement("content-type"_ns);
    AddStaticElement("cookie"_ns);
    AddStaticElement("date"_ns);
    AddStaticElement("etag"_ns);
    AddStaticElement("expect"_ns);
    AddStaticElement("expires"_ns);
    AddStaticElement("from"_ns);
    AddStaticElement("host"_ns);
    AddStaticElement("if-match"_ns);
    AddStaticElement("if-modified-since"_ns);
    AddStaticElement("if-none-match"_ns);
    AddStaticElement("if-range"_ns);
    AddStaticElement("if-unmodified-since"_ns);
    AddStaticElement("last-modified"_ns);
    AddStaticElement("link"_ns);
    AddStaticElement("location"_ns);
    AddStaticElement("max-forwards"_ns);
    AddStaticElement("proxy-authenticate"_ns);
    AddStaticElement("proxy-authorization"_ns);
    AddStaticElement("range"_ns);
    AddStaticElement("referer"_ns);
    AddStaticElement("refresh"_ns);
    AddStaticElement("retry-after"_ns);
    AddStaticElement("server"_ns);
    AddStaticElement("set-cookie"_ns);
    AddStaticElement("strict-transport-security"_ns);
    AddStaticElement("transfer-encoding"_ns);
    AddStaticElement("user-agent"_ns);
    AddStaticElement("vary"_ns);
    AddStaticElement("via"_ns);
    AddStaticElement("www-authenticate"_ns);
  }
}

size_t nvPair::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  return mName.SizeOfExcludingThisIfUnshared(aMallocSizeOf) +
         mValue.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
}

size_t nvPair::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

nvFIFO::nvFIFO() { InitializeStaticHeaders(); }

nvFIFO::~nvFIFO() { Clear(); }

void nvFIFO::AddElement(const nsCString& name, const nsCString& value) {
  nvPair* pair = new nvPair(name, value);
  mByteCount += pair->Size();
  MutexAutoLock lock(mMutex);
  mTable.PushFront(pair);
}

void nvFIFO::AddElement(const nsCString& name) { AddElement(name, ""_ns); }

void nvFIFO::RemoveElement() {
  nvPair* pair = nullptr;
  {
    MutexAutoLock lock(mMutex);
    pair = mTable.Pop();
  }
  if (pair) {
    mByteCount -= pair->Size();
    delete pair;
  }
}

uint32_t nvFIFO::ByteCount() const { return mByteCount; }

uint32_t nvFIFO::Length() const {
  return mTable.GetSize() + gStaticHeaders->GetSize();
}

uint32_t nvFIFO::VariableLength() const { return mTable.GetSize(); }

size_t nvFIFO::StaticLength() const { return gStaticHeaders->GetSize(); }

void nvFIFO::Clear() {
  mByteCount = 0;
  MutexAutoLock lock(mMutex);
  while (mTable.GetSize()) {
    delete mTable.Pop();
  }
}

const nvPair* nvFIFO::operator[](size_t index) const {
  // NWGH - ensure index > 0
  // NWGH - subtract 1 from index here
  if (index >= (mTable.GetSize() + gStaticHeaders->GetSize())) {
    MOZ_ASSERT(false);
    NS_WARNING("nvFIFO Table Out of Range");
    return nullptr;
  }
  if (index >= gStaticHeaders->GetSize()) {
    return mTable.ObjectAt(index - gStaticHeaders->GetSize());
  }
  return gStaticHeaders->ObjectAt(index);
}

Http2BaseCompressor::Http2BaseCompressor() {
  mDynamicReporter = new HpackDynamicTableReporter(this);
  RegisterStrongMemoryReporter(do_AddRef(mDynamicReporter));
}

Http2BaseCompressor::~Http2BaseCompressor() {
  UnregisterStrongMemoryReporter(mDynamicReporter);
  {
    MutexAutoLock lock(mDynamicReporter->mMutex);
    mDynamicReporter->mCompressor = nullptr;
  }
  mDynamicReporter = nullptr;
}

size_t nvFIFO::SizeOfDynamicTable(mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t size = 0;
  MutexAutoLock lock(mMutex);
  for (const auto elem : mTable) {
    size += elem->SizeOfIncludingThis(aMallocSizeOf);
  }
  return size;
}

void Http2BaseCompressor::ClearHeaderTable() { mHeaderTable.Clear(); }

size_t Http2BaseCompressor::SizeOfExcludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return mHeaderTable.SizeOfDynamicTable(aMallocSizeOf);
}

void Http2BaseCompressor::MakeRoom(uint32_t amount, const char* direction) {
  // make room in the header table
  while (mHeaderTable.VariableLength() &&
         ((mHeaderTable.ByteCount() + amount) > mMaxBuffer)) {
    // NWGH - remove the "- 1" here
    uint32_t index = mHeaderTable.Length() - 1;
    LOG(("HTTP %s header table index %u %s %s removed for size.\n", direction,
         index, mHeaderTable[index]->mName.get(),
         mHeaderTable[index]->mValue.get()));
    mHeaderTable.RemoveElement();
  }
}

void Http2BaseCompressor::DumpState(const char* preamble) {
  if (!LOG_ENABLED()) {
    return;
  }

  if (!mDumpTables) {
    return;
  }

  LOG(("%s", preamble));

  LOG(("Header Table"));
  uint32_t i;
  uint32_t length = mHeaderTable.Length();
  uint32_t staticLength = mHeaderTable.StaticLength();
  // NWGH - make i = 1; i <= length; ++i
  for (i = 0; i < length; ++i) {
    const nvPair* pair = mHeaderTable[i];
    // NWGH - make this <= staticLength
    LOG(("%sindex %u: %s %s", i < staticLength ? "static " : "", i,
         pair->mName.get(), pair->mValue.get()));
  }
}

void Http2BaseCompressor::SetMaxBufferSizeInternal(uint32_t maxBufferSize) {
  MOZ_ASSERT(maxBufferSize <= mMaxBufferSetting);

  LOG(("Http2BaseCompressor::SetMaxBufferSizeInternal %u called",
       maxBufferSize));

  while (mHeaderTable.VariableLength() &&
         (mHeaderTable.ByteCount() > maxBufferSize)) {
    mHeaderTable.RemoveElement();
  }

  mMaxBuffer = maxBufferSize;
}

nsresult Http2BaseCompressor::SetInitialMaxBufferSize(uint32_t maxBufferSize) {
  MOZ_ASSERT(mSetInitialMaxBufferSizeAllowed);

  if (mSetInitialMaxBufferSizeAllowed) {
    mMaxBufferSetting = maxBufferSize;
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

void Http2BaseCompressor::SetDumpTables(bool dumpTables) {
  mDumpTables = dumpTables;
}

Http2Decompressor::~Http2Decompressor() = default;

nsresult Http2Decompressor::DecodeHeaderBlock(const uint8_t* data,
                                              uint32_t datalen,
                                              nsACString& output, bool isPush) {
  mSetInitialMaxBufferSizeAllowed = false;
  mOffset = 0;
  mData = data;
  mDataLen = datalen;
  mOutput = &output;
  // Add in some space to hopefully not have to reallocate while decompressing
  // the headers. 512 bytes seems like a good enough number.
  mOutput->Truncate();
  mOutput->SetCapacity(datalen + 512);
  mHeaderStatus.Truncate();
  mHeaderHost.Truncate();
  mHeaderScheme.Truncate();
  mHeaderPath.Truncate();
  mHeaderMethod.Truncate();
  mSeenNonColonHeader = false;
  mIsPush = isPush;

  nsresult rv = NS_OK;
  nsresult softfail_rv = NS_OK;
  while (NS_SUCCEEDED(rv) && (mOffset < mDataLen)) {
    bool modifiesTable = true;
    const char* preamble = "Decompressor state after ?";
    if (mData[mOffset] & 0x80) {
      rv = DoIndexed();
      preamble = "Decompressor state after indexed";
    } else if (mData[mOffset] & 0x40) {
      rv = DoLiteralWithIncremental();
      preamble = "Decompressor state after literal with incremental";
    } else if (mData[mOffset] & 0x20) {
      rv = DoContextUpdate();
      preamble = "Decompressor state after context update";
    } else if (mData[mOffset] & 0x10) {
      modifiesTable = false;
      rv = DoLiteralNeverIndexed();
      preamble = "Decompressor state after literal never index";
    } else {
      modifiesTable = false;
      rv = DoLiteralWithoutIndex();
      preamble = "Decompressor state after literal without index";
    }
    DumpState(preamble);
    if (rv == NS_ERROR_ILLEGAL_VALUE) {
      if (modifiesTable) {
        // Unfortunately, we can't count on our peer now having the same state
        // as us, so let's terminate the session and we can try again later.
        return NS_ERROR_FAILURE;
      }

      // This is an http-level error that we can handle by resetting the stream
      // in the upper layers. Let's note that we saw this, then continue
      // decompressing until we either hit the end of the header block or find a
      // hard failure. That way we won't get an inconsistent compression state
      // with the server.
      softfail_rv = rv;
      rv = NS_OK;
    } else if (rv == NS_ERROR_NET_RESET) {
      // This happens when we detect connection-based auth being requested in
      // the response headers. We'll paper over it for now, and the session will
      // handle this as if it received RST_STREAM with HTTP_1_1_REQUIRED.
      softfail_rv = rv;
      rv = NS_OK;
    }
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  return softfail_rv;
}

nsresult Http2Decompressor::DecodeInteger(uint32_t prefixLen, uint32_t& accum) {
  accum = 0;

  if (prefixLen) {
    uint32_t mask = (1 << prefixLen) - 1;

    accum = mData[mOffset] & mask;
    ++mOffset;

    if (accum != mask) {
      // the simple case for small values
      return NS_OK;
    }
  }

  uint32_t factor = 1;  // 128 ^ 0

  // we need a series of bytes. The high bit signifies if we need another one.
  // The first one is a a factor of 128 ^ 0, the next 128 ^1, the next 128 ^2,
  // ..

  if (mOffset >= mDataLen) {
    NS_WARNING("Ran out of data to decode integer");
    // This is session-fatal.
    return NS_ERROR_FAILURE;
  }
  bool chainBit = mData[mOffset] & 0x80;
  accum += (mData[mOffset] & 0x7f) * factor;

  ++mOffset;
  factor = factor * 128;

  while (chainBit) {
    // really big offsets are just trawling for overflows
    if (accum >= 0x800000) {
      NS_WARNING("Decoding integer >= 0x800000");
      // This is not strictly fatal to the session, but given the fact that
      // the value is way to large to be reasonable, let's just tell our peer
      // to go away.
      return NS_ERROR_FAILURE;
    }

    if (mOffset >= mDataLen) {
      NS_WARNING("Ran out of data to decode integer");
      // This is session-fatal.
      return NS_ERROR_FAILURE;
    }
    chainBit = mData[mOffset] & 0x80;
    accum += (mData[mOffset] & 0x7f) * factor;
    ++mOffset;
    factor = factor * 128;
  }
  return NS_OK;
}

static bool HasConnectionBasedAuth(const nsACString& headerValue) {
  for (const nsACString& authMethod :
       nsCCharSeparatedTokenizer(headerValue, '\n').ToRange()) {
    if (authMethod.LowerCaseEqualsLiteral("ntlm")) {
      return true;
    }
    if (authMethod.LowerCaseEqualsLiteral("negotiate")) {
      return true;
    }
  }

  return false;
}

nsresult Http2Decompressor::OutputHeader(const nsACString& name,
                                         const nsACString& value) {
  // exclusions
  if (!mIsPush &&
      (name.EqualsLiteral("connection") || name.EqualsLiteral("host") ||
       name.EqualsLiteral("keep-alive") ||
       name.EqualsLiteral("proxy-connection") || name.EqualsLiteral("te") ||
       name.EqualsLiteral("transfer-encoding") ||
       name.EqualsLiteral("upgrade") || name.Equals(("accept-encoding")))) {
    nsCString toLog(name);
    LOG(("HTTP Decompressor illegal response header found, not gatewaying: %s",
         toLog.get()));
    return NS_OK;
  }

  // Bug 1663836: reject invalid HTTP response header names - RFC7540 Sec 10.3
  const char* cFirst = name.BeginReading();
  if (cFirst != nullptr && *cFirst == ':') {
    ++cFirst;
  }
  if (!nsHttp::IsValidToken(cFirst, name.EndReading())) {
    nsCString toLog(name);
    LOG(("HTTP Decompressor invalid response header found. [%s]\n",
         toLog.get()));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  // Look for upper case characters in the name.
  for (const char* cPtr = name.BeginReading(); cPtr && cPtr < name.EndReading();
       ++cPtr) {
    if (*cPtr <= 'Z' && *cPtr >= 'A') {
      nsCString toLog(name);
      LOG(("HTTP Decompressor upper case response header found. [%s]\n",
           toLog.get()));
      return NS_ERROR_ILLEGAL_VALUE;
    }
  }

  // Look for CR, LF or NUL in value - could be smuggling (RFC7540 Sec 10.3)
  // treat as malformed
  if (!nsHttp::IsReasonableHeaderValue(value)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  // Status comes first
  if (name.EqualsLiteral(":status")) {
    nsAutoCString status("HTTP/2 "_ns);
    status.Append(value);
    status.AppendLiteral("\r\n");
    mOutput->Insert(status, 0);
    mHeaderStatus = value;
  } else if (name.EqualsLiteral(":authority")) {
    mHeaderHost = value;
  } else if (name.EqualsLiteral(":scheme")) {
    mHeaderScheme = value;
  } else if (name.EqualsLiteral(":path")) {
    mHeaderPath = value;
  } else if (name.EqualsLiteral(":method")) {
    mHeaderMethod = value;
  }

  // http/2 transport level headers shouldn't be gatewayed into http/1
  bool isColonHeader = false;
  for (const char* cPtr = name.BeginReading(); cPtr && cPtr < name.EndReading();
       ++cPtr) {
    if (*cPtr == ':') {
      isColonHeader = true;
      break;
    }
    if (*cPtr != ' ' && *cPtr != '\t') {
      isColonHeader = false;
      break;
    }
  }

  if (isColonHeader) {
    // :status is the only pseudo-header field allowed in received HEADERS
    // frames, PUSH_PROMISE allows the other pseudo-header fields
    if (!name.EqualsLiteral(":status") && !mIsPush) {
      LOG(("HTTP Decompressor found illegal response pseudo-header %s",
           PromiseFlatCString(name).get()));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    if (mSeenNonColonHeader) {
      LOG(("HTTP Decompressor found illegal : header %s",
           PromiseFlatCString(name).get()));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    LOG(("HTTP Decompressor not gatewaying %s into http/1",
         PromiseFlatCString(name).get()));
    return NS_OK;
  }

  LOG(("Http2Decompressor::OutputHeader %s %s", PromiseFlatCString(name).get(),
       PromiseFlatCString(value).get()));
  mSeenNonColonHeader = true;
  mOutput->Append(name);
  mOutput->AppendLiteral(": ");
  mOutput->Append(value);
  mOutput->AppendLiteral("\r\n");

  // Need to check if the server is going to try to speak connection-based auth
  // with us. If so, we need to kill this via h2, and dial back with http/1.1.
  // Technically speaking, the server should've just reset or goaway'd us with
  // HTTP_1_1_REQUIRED, but there are some busted servers out there, so we need
  // to check on our own to work around them.
  if (name.EqualsLiteral("www-authenticate") ||
      name.EqualsLiteral("proxy-authenticate")) {
    if (HasConnectionBasedAuth(value)) {
      LOG3(("Http2Decompressor %p connection-based auth found in %s", this,
            PromiseFlatCString(name).get()));
      return NS_ERROR_NET_RESET;
    }
  }
  return NS_OK;
}

nsresult Http2Decompressor::OutputHeader(uint32_t index) {
  // NWGH - make this < index
  // bounds check
  if (mHeaderTable.Length() <= index) {
    LOG(("Http2Decompressor::OutputHeader index too large %u", index));
    // This is session-fatal.
    return NS_ERROR_FAILURE;
  }

  return OutputHeader(mHeaderTable[index]->mName, mHeaderTable[index]->mValue);
}

nsresult Http2Decompressor::CopyHeaderString(uint32_t index, nsACString& name) {
  // NWGH - make this < index
  // bounds check
  if (mHeaderTable.Length() <= index) {
    // This is session-fatal.
    return NS_ERROR_FAILURE;
  }

  name = mHeaderTable[index]->mName;
  return NS_OK;
}

nsresult Http2Decompressor::CopyStringFromInput(uint32_t bytes,
                                                nsACString& val) {
  if (mOffset + bytes > mDataLen) {
    // This is session-fatal.
    return NS_ERROR_FAILURE;
  }

  val.Assign(reinterpret_cast<const char*>(mData) + mOffset, bytes);
  mOffset += bytes;
  return NS_OK;
}

nsresult Http2Decompressor::DecodeFinalHuffmanCharacter(
    const HuffmanIncomingTable* table, uint8_t& c, uint8_t& bitsLeft) {
  MOZ_ASSERT(mOffset <= mDataLen);
  if (mOffset > mDataLen) {
    NS_WARNING("DecodeFinalHuffmanCharacter would read beyond end of buffer");
    return NS_ERROR_FAILURE;
  }
  uint8_t mask = (1 << bitsLeft) - 1;
  uint8_t idx = mData[mOffset - 1] & mask;
  idx <<= (8 - bitsLeft);
  // Don't update bitsLeft yet, because we need to check that value against the
  // number of bits used by our encoding later on. We'll update when we are sure
  // how many bits we've actually used.

  if (table->IndexHasANextTable(idx)) {
    // Can't chain to another table when we're all out of bits in the encoding
    LOG(("DecodeFinalHuffmanCharacter trying to chain when we're out of bits"));
    return NS_ERROR_FAILURE;
  }

  const HuffmanIncomingEntry* entry = table->Entry(idx);

  if (bitsLeft < entry->mPrefixLen) {
    // We don't have enough bits to actually make a match, this is some sort of
    // invalid coding
    LOG(("DecodeFinalHuffmanCharacter does't have enough bits to match"));
    return NS_ERROR_FAILURE;
  }

  // This is a character!
  if (entry->mValue == 256) {
    // EOS
    LOG(("DecodeFinalHuffmanCharacter actually decoded an EOS"));
    return NS_ERROR_FAILURE;
  }
  c = static_cast<uint8_t>(entry->mValue & 0xFF);
  bitsLeft -= entry->mPrefixLen;

  return NS_OK;
}

uint8_t Http2Decompressor::ExtractByte(uint8_t bitsLeft,
                                       uint32_t& bytesConsumed) {
  MOZ_DIAGNOSTIC_ASSERT(mOffset < mDataLen);
  uint8_t rv;

  if (bitsLeft) {
    // Need to extract bitsLeft bits from the previous byte, and 8 - bitsLeft
    // bits from the current byte
    uint8_t mask = (1 << bitsLeft) - 1;
    rv = (mData[mOffset - 1] & mask) << (8 - bitsLeft);
    rv |= (mData[mOffset] & ~mask) >> bitsLeft;
  } else {
    rv = mData[mOffset];
  }

  // We always update these here, under the assumption that all 8 bits we got
  // here will be used. These may be re-adjusted later in the case that we don't
  // use up all 8 bits of the byte.
  ++mOffset;
  ++bytesConsumed;

  return rv;
}

nsresult Http2Decompressor::DecodeHuffmanCharacter(
    const HuffmanIncomingTable* table, uint8_t& c, uint32_t& bytesConsumed,
    uint8_t& bitsLeft) {
  uint8_t idx = ExtractByte(bitsLeft, bytesConsumed);

  if (table->IndexHasANextTable(idx)) {
    if (mOffset >= mDataLen) {
      if (!bitsLeft || (mOffset > mDataLen)) {
        // TODO - does this get me into trouble in the new world?
        // No info left in input to try to consume, we're done
        LOG(("DecodeHuffmanCharacter all out of bits to consume, can't chain"));
        return NS_ERROR_FAILURE;
      }

      // We might get lucky here!
      return DecodeFinalHuffmanCharacter(table->NextTable(idx), c, bitsLeft);
    }

    // We're sorry, Mario, but your princess is in another castle
    return DecodeHuffmanCharacter(table->NextTable(idx), c, bytesConsumed,
                                  bitsLeft);
  }

  const HuffmanIncomingEntry* entry = table->Entry(idx);
  if (entry->mValue == 256) {
    LOG(("DecodeHuffmanCharacter found an actual EOS"));
    return NS_ERROR_FAILURE;
  }
  c = static_cast<uint8_t>(entry->mValue & 0xFF);

  // Need to adjust bitsLeft (and possibly other values) because we may not have
  // consumed all of the bits of the byte we extracted.
  if (entry->mPrefixLen <= bitsLeft) {
    bitsLeft -= entry->mPrefixLen;
    --mOffset;
    --bytesConsumed;
  } else {
    bitsLeft = 8 - (entry->mPrefixLen - bitsLeft);
  }
  MOZ_ASSERT(bitsLeft < 8);

  return NS_OK;
}

nsresult Http2Decompressor::CopyHuffmanStringFromInput(uint32_t bytes,
                                                       nsACString& val) {
  if (mOffset + bytes > mDataLen) {
    LOG(("CopyHuffmanStringFromInput not enough data"));
    return NS_ERROR_FAILURE;
  }

  uint32_t bytesRead = 0;
  uint8_t bitsLeft = 0;
  nsAutoCString buf;
  nsresult rv;
  uint8_t c;

  while (bytesRead < bytes) {
    uint32_t bytesConsumed = 0;
    rv = DecodeHuffmanCharacter(&HuffmanIncomingRoot, c, bytesConsumed,
                                bitsLeft);
    if (NS_FAILED(rv)) {
      LOG(("CopyHuffmanStringFromInput failed to decode a character"));
      return rv;
    }

    bytesRead += bytesConsumed;
    buf.Append(c);
  }

  if (bytesRead > bytes) {
    LOG(("CopyHuffmanStringFromInput read more bytes than was allowed!"));
    return NS_ERROR_FAILURE;
  }

  if (bitsLeft) {
    // The shortest valid code is 4 bits, so we know there can be at most one
    // character left that our loop didn't decode. Check to see if that's the
    // case, and if so, add it to our output.
    rv = DecodeFinalHuffmanCharacter(&HuffmanIncomingRoot, c, bitsLeft);
    if (NS_SUCCEEDED(rv)) {
      buf.Append(c);
    }
  }

  if (bitsLeft > 7) {
    LOG(("CopyHuffmanStringFromInput more than 7 bits of padding"));
    return NS_ERROR_FAILURE;
  }

  if (bitsLeft) {
    // Any bits left at this point must belong to the EOS symbol, so make sure
    // they make sense (ie, are all ones)
    uint8_t mask = (1 << bitsLeft) - 1;
    uint8_t bits = mData[mOffset - 1] & mask;
    if (bits != mask) {
      LOG(
          ("CopyHuffmanStringFromInput ran out of data but found possible "
           "non-EOS symbol"));
      return NS_ERROR_FAILURE;
    }
  }

  val = buf;
  LOG(("CopyHuffmanStringFromInput decoded a full string!"));
  return NS_OK;
}

nsresult Http2Decompressor::DoIndexed() {
  // this starts with a 1 bit pattern
  MOZ_ASSERT(mData[mOffset] & 0x80);

  // This is a 7 bit prefix

  uint32_t index;
  nsresult rv = DecodeInteger(7, index);
  if (NS_FAILED(rv)) {
    return rv;
  }

  LOG(("HTTP decompressor indexed entry %u\n", index));

  if (index == 0) {
    return NS_ERROR_FAILURE;
  }
  // NWGH - remove this line, since we'll keep everything 1-indexed
  index--;  // Internally, we 0-index everything, since this is, y'know, C++

  return OutputHeader(index);
}

nsresult Http2Decompressor::DoLiteralInternal(nsACString& name,
                                              nsACString& value,
                                              uint32_t namePrefixLen) {
  // guts of doliteralwithoutindex and doliteralwithincremental
  MOZ_ASSERT(((mData[mOffset] & 0xF0) == 0x00) ||  // withoutindex
             ((mData[mOffset] & 0xF0) == 0x10) ||  // neverindexed
             ((mData[mOffset] & 0xC0) == 0x40));   // withincremental

  // first let's get the name
  uint32_t index;
  nsresult rv = DecodeInteger(namePrefixLen, index);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // sanity check
  if (mOffset >= mDataLen) {
    NS_WARNING("Http2 Decompressor ran out of data");
    // This is session-fatal
    return NS_ERROR_FAILURE;
  }

  bool isHuffmanEncoded;

  if (!index) {
    // name is embedded as a literal
    uint32_t nameLen;
    isHuffmanEncoded = mData[mOffset] & (1 << 7);
    rv = DecodeInteger(7, nameLen);
    if (NS_SUCCEEDED(rv)) {
      if (isHuffmanEncoded) {
        rv = CopyHuffmanStringFromInput(nameLen, name);
      } else {
        rv = CopyStringFromInput(nameLen, name);
      }
    }
    LOG(("Http2Decompressor::DoLiteralInternal literal name %s",
         PromiseFlatCString(name).get()));
  } else {
    // NWGH - make this index, not index - 1
    // name is from headertable
    rv = CopyHeaderString(index - 1, name);
    LOG(("Http2Decompressor::DoLiteralInternal indexed name %d %s", index,
         PromiseFlatCString(name).get()));
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  // sanity check
  if (mOffset >= mDataLen) {
    NS_WARNING("Http2 Decompressor ran out of data");
    // This is session-fatal
    return NS_ERROR_FAILURE;
  }

  // now the value
  uint32_t valueLen;
  isHuffmanEncoded = mData[mOffset] & (1 << 7);
  rv = DecodeInteger(7, valueLen);
  if (NS_SUCCEEDED(rv)) {
    if (isHuffmanEncoded) {
      rv = CopyHuffmanStringFromInput(valueLen, value);
    } else {
      rv = CopyStringFromInput(valueLen, value);
    }
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  int32_t newline = 0;
  while ((newline = value.FindChar('\n', newline)) != -1) {
    if (value[newline + 1] == ' ' || value[newline + 1] == '\t') {
      LOG(("Http2Decompressor::Disallowing folded header value %s",
           PromiseFlatCString(value).get()));
      return NS_ERROR_ILLEGAL_VALUE;
    }
    // Increment this to avoid always finding the same newline and looping
    // forever
    ++newline;
  }

  LOG(("Http2Decompressor::DoLiteralInternal value %s",
       PromiseFlatCString(value).get()));
  return NS_OK;
}

nsresult Http2Decompressor::DoLiteralWithoutIndex() {
  // this starts with 0000 bit pattern
  MOZ_ASSERT((mData[mOffset] & 0xF0) == 0x00);

  nsAutoCString name, value;
  nsresult rv = DoLiteralInternal(name, value, 4);

  LOG(("HTTP decompressor literal without index %s %s\n", name.get(),
       value.get()));

  if (NS_SUCCEEDED(rv)) {
    rv = OutputHeader(name, value);
  }
  return rv;
}

nsresult Http2Decompressor::DoLiteralWithIncremental() {
  // this starts with 01 bit pattern
  MOZ_ASSERT((mData[mOffset] & 0xC0) == 0x40);

  nsAutoCString name, value;
  nsresult rv = DoLiteralInternal(name, value, 6);
  if (NS_SUCCEEDED(rv)) {
    rv = OutputHeader(name, value);
  }
  // Let NET_RESET continue on so that we don't get out of sync, as it is just
  // used to kill the stream, not the session.
  if (NS_FAILED(rv) && rv != NS_ERROR_NET_RESET) {
    return rv;
  }

  uint32_t room = nvPair(name, value).Size();
  if (room > mMaxBuffer) {
    ClearHeaderTable();
    LOG(
        ("HTTP decompressor literal with index not inserted due to size %u %s "
         "%s\n",
         room, name.get(), value.get()));
    DumpState("Decompressor state after ClearHeaderTable");
    return rv;
  }

  MakeRoom(room, "decompressor");

  // Incremental Indexing implicitly adds a row to the header table.
  mHeaderTable.AddElement(name, value);

  uint32_t currentSize = mHeaderTable.ByteCount();
  if (currentSize > mPeakSize) {
    mPeakSize = currentSize;
  }

  uint32_t currentCount = mHeaderTable.VariableLength();
  if (currentCount > mPeakCount) {
    mPeakCount = currentCount;
  }

  LOG(("HTTP decompressor literal with index 0 %s %s\n", name.get(),
       value.get()));

  return rv;
}

nsresult Http2Decompressor::DoLiteralNeverIndexed() {
  // This starts with 0001 bit pattern
  MOZ_ASSERT((mData[mOffset] & 0xF0) == 0x10);

  nsAutoCString name, value;
  nsresult rv = DoLiteralInternal(name, value, 4);

  LOG(("HTTP decompressor literal never indexed %s %s\n", name.get(),
       value.get()));

  if (NS_SUCCEEDED(rv)) {
    rv = OutputHeader(name, value);
  }
  return rv;
}

nsresult Http2Decompressor::DoContextUpdate() {
  // This starts with 001 bit pattern
  MOZ_ASSERT((mData[mOffset] & 0xE0) == 0x20);

  // Getting here means we have to adjust the max table size, because the
  // compressor on the other end has signaled to us through HPACK (not H2)
  // that it's using a size different from the currently-negotiated size.
  // This change could either come about because we've sent a
  // SETTINGS_HEADER_TABLE_SIZE, or because the encoder has decided that
  // the current negotiated size doesn't fit its needs (for whatever reason)
  // and so it needs to change it (either up to the max allowed by our SETTING,
  // or down to some value below that)
  uint32_t newMaxSize;
  nsresult rv = DecodeInteger(5, newMaxSize);
  LOG(("Http2Decompressor::DoContextUpdate new maximum size %u", newMaxSize));
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (newMaxSize > mMaxBufferSetting) {
    // This is fatal to the session - peer is trying to use a table larger
    // than we have made available.
    return NS_ERROR_FAILURE;
  }

  SetMaxBufferSizeInternal(newMaxSize);

  return NS_OK;
}

/////////////////////////////////////////////////////////////////

Http2Compressor::~Http2Compressor() = default;

nsresult Http2Compressor::EncodeHeaderBlock(
    const nsCString& nvInput, const nsACString& method, const nsACString& path,
    const nsACString& host, const nsACString& scheme,
    const nsACString& protocol, bool simpleConnectForm, nsACString& output,
    bool addTEHeader) {
  mSetInitialMaxBufferSizeAllowed = false;
  mOutput = &output;
  output.Truncate();
  mParsedContentLength = -1;

  bool isWebsocket = (!simpleConnectForm && !protocol.IsEmpty());

  // first thing's first - context size updates (if necessary)
  if (mBufferSizeChangeWaiting) {
    if (mLowestBufferSizeWaiting < mMaxBufferSetting) {
      EncodeTableSizeChange(mLowestBufferSizeWaiting);
    }
    EncodeTableSizeChange(mMaxBufferSetting);
    mBufferSizeChangeWaiting = false;
  }

  // colon headers first
  if (!simpleConnectForm) {
    ProcessHeader(nvPair(":method"_ns, method), false, false);
    ProcessHeader(nvPair(":path"_ns, path), true, false);
    ProcessHeader(nvPair(":authority"_ns, host), false, false);
    ProcessHeader(nvPair(":scheme"_ns, scheme), false, false);
    if (isWebsocket) {
      ProcessHeader(nvPair(":protocol"_ns, protocol), false, false);
    }
  } else {
    ProcessHeader(nvPair(":method"_ns, method), false, false);
    ProcessHeader(nvPair(":authority"_ns, host), false, false);
  }

  // now the non colon headers
  const char* beginBuffer = nvInput.BeginReading();

  // This strips off the HTTP/1 method+path+version
  int32_t crlfIndex = nvInput.Find("\r\n");
  while (true) {
    int32_t startIndex = crlfIndex + 2;

    crlfIndex = nvInput.Find("\r\n", startIndex);
    if (crlfIndex == -1) {
      break;
    }

    int32_t colonIndex = Substring(nvInput, 0, crlfIndex).Find(":", startIndex);
    if (colonIndex == -1) {
      break;
    }

    nsDependentCSubstring name =
        Substring(beginBuffer + startIndex, beginBuffer + colonIndex);
    // all header names are lower case in http/2
    ToLowerCase(name);

    // exclusions
    if (name.EqualsLiteral("connection") || name.EqualsLiteral("host") ||
        name.EqualsLiteral("keep-alive") ||
        name.EqualsLiteral("proxy-connection") || name.EqualsLiteral("te") ||
        name.EqualsLiteral("transfer-encoding") ||
        name.EqualsLiteral("upgrade") ||
        name.EqualsLiteral("sec-websocket-key")) {
      continue;
    }

    // colon headers are for http/2 and this is http/1 input, so that
    // is probably a smuggling attack of some kind
    bool isColonHeader = false;
    for (const char* cPtr = name.BeginReading();
         cPtr && cPtr < name.EndReading(); ++cPtr) {
      if (*cPtr == ':') {
        isColonHeader = true;
        break;
      }
      if (*cPtr != ' ' && *cPtr != '\t') {
        isColonHeader = false;
        break;
      }
    }
    if (isColonHeader) {
      continue;
    }

    int32_t valueIndex = colonIndex + 1;

    while (valueIndex < crlfIndex && beginBuffer[valueIndex] == ' ') {
      ++valueIndex;
    }

    nsDependentCSubstring value =
        Substring(beginBuffer + valueIndex, beginBuffer + crlfIndex);

    if (name.EqualsLiteral("content-length")) {
      int64_t len;
      nsCString tmp(value);
      if (nsHttp::ParseInt64(tmp.get(), nullptr, &len)) {
        mParsedContentLength = len;
      }
    }

    if (name.EqualsLiteral("cookie")) {
      // cookie crumbling
      bool haveMoreCookies = true;
      int32_t nextCookie = valueIndex;
      while (haveMoreCookies) {
        int32_t semiSpaceIndex =
            Substring(nvInput, 0, crlfIndex).Find("; ", nextCookie);
        if (semiSpaceIndex == -1) {
          haveMoreCookies = false;
          semiSpaceIndex = crlfIndex;
        }
        nsDependentCSubstring cookie =
            Substring(beginBuffer + nextCookie, beginBuffer + semiSpaceIndex);
        // cookies less than 20 bytes are not indexed
        ProcessHeader(nvPair(name, cookie), false, cookie.Length() < 20);
        nextCookie = semiSpaceIndex + 2;
      }
    } else {
      // allow indexing of every non-cookie except authorization
      ProcessHeader(nvPair(name, value), false,
                    name.EqualsLiteral("authorization"));
    }
  }

  // NB: This is a *really* ugly hack, but to do this in the right place (the
  // transaction) would require totally reworking how/when the transaction
  // creates its request stream, which is not worth the effort and risk of
  // breakage just to add one header only to h2 connections.
  if (addTEHeader && !simpleConnectForm && !isWebsocket) {
    // Add in TE: trailers for regular requests
    nsAutoCString te("te");
    nsAutoCString trailers("trailers");
    ProcessHeader(nvPair(te, trailers), false, false);
  }

  mOutput = nullptr;
  DumpState("Compressor state after EncodeHeaderBlock");
  return NS_OK;
}

void Http2Compressor::DoOutput(Http2Compressor::outputCode code,
                               const class nvPair* pair, uint32_t index) {
  // start Byte needs to be calculated from the offset after
  // the opcode has been written out in case the output stream
  // buffer gets resized/relocated
  uint32_t offset = mOutput->Length();
  uint8_t* startByte;

  switch (code) {
    case kNeverIndexedLiteral:
      LOG(
          ("HTTP compressor %p neverindex literal with name reference %u %s "
           "%s\n",
           this, index, pair->mName.get(), pair->mValue.get()));

      // In this case, the index will have already been adjusted to be 1-based
      // instead of 0-based.
      EncodeInteger(4, index);  // 0001 4 bit prefix
      startByte =
          reinterpret_cast<unsigned char*>(mOutput->BeginWriting()) + offset;
      *startByte = (*startByte & 0x0f) | 0x10;

      if (!index) {
        HuffmanAppend(pair->mName);
      }

      HuffmanAppend(pair->mValue);
      break;

    case kPlainLiteral:
      LOG(("HTTP compressor %p noindex literal with name reference %u %s %s\n",
           this, index, pair->mName.get(), pair->mValue.get()));

      // In this case, the index will have already been adjusted to be 1-based
      // instead of 0-based.
      EncodeInteger(4, index);  // 0000 4 bit prefix
      startByte =
          reinterpret_cast<unsigned char*>(mOutput->BeginWriting()) + offset;
      *startByte = *startByte & 0x0f;

      if (!index) {
        HuffmanAppend(pair->mName);
      }

      HuffmanAppend(pair->mValue);
      break;

    case kIndexedLiteral:
      LOG(("HTTP compressor %p literal with name reference %u %s %s\n", this,
           index, pair->mName.get(), pair->mValue.get()));

      // In this case, the index will have already been adjusted to be 1-based
      // instead of 0-based.
      EncodeInteger(6, index);  // 01 2 bit prefix
      startByte =
          reinterpret_cast<unsigned char*>(mOutput->BeginWriting()) + offset;
      *startByte = (*startByte & 0x3f) | 0x40;

      if (!index) {
        HuffmanAppend(pair->mName);
      }

      HuffmanAppend(pair->mValue);
      break;

    case kIndex:
      LOG(("HTTP compressor %p index %u %s %s\n", this, index,
           pair->mName.get(), pair->mValue.get()));
      // NWGH - make this plain old index instead of index + 1
      // In this case, we are passed the raw 0-based C index, and need to
      // increment to make it 1-based and comply with the spec
      EncodeInteger(7, index + 1);
      startByte =
          reinterpret_cast<unsigned char*>(mOutput->BeginWriting()) + offset;
      *startByte = *startByte | 0x80;  // 1 1 bit prefix
      break;
  }
}

// writes the encoded integer onto the output
void Http2Compressor::EncodeInteger(uint32_t prefixLen, uint32_t val) {
  uint32_t mask = (1 << prefixLen) - 1;
  uint8_t tmp;

  if (val < mask) {
    // 1 byte encoding!
    tmp = val;
    mOutput->Append(reinterpret_cast<char*>(&tmp), 1);
    return;
  }

  if (mask) {
    val -= mask;
    tmp = mask;
    mOutput->Append(reinterpret_cast<char*>(&tmp), 1);
  }

  uint32_t q, r;
  do {
    q = val / 128;
    r = val % 128;
    tmp = r;
    if (q) {
      tmp |= 0x80;  // chain bit
    }
    val = q;
    mOutput->Append(reinterpret_cast<char*>(&tmp), 1);
  } while (q);
}

void Http2Compressor::HuffmanAppend(const nsCString& value) {
  nsAutoCString buf;
  uint8_t bitsLeft = 8;
  uint32_t length = value.Length();
  uint32_t offset;
  uint8_t* startByte;

  for (uint32_t i = 0; i < length; ++i) {
    uint8_t idx = static_cast<uint8_t>(value[i]);
    uint8_t huffLength = HuffmanOutgoing[idx].mLength;
    uint32_t huffValue = HuffmanOutgoing[idx].mValue;

    if (bitsLeft < 8) {
      // Fill in the least significant <bitsLeft> bits of the previous byte
      // first
      uint32_t val;
      if (huffLength >= bitsLeft) {
        val = huffValue & ~((1 << (huffLength - bitsLeft)) - 1);
        val >>= (huffLength - bitsLeft);
      } else {
        val = huffValue << (bitsLeft - huffLength);
      }
      val &= ((1 << bitsLeft) - 1);
      offset = buf.Length() - 1;
      startByte = reinterpret_cast<unsigned char*>(buf.BeginWriting()) + offset;
      *startByte = *startByte | static_cast<uint8_t>(val & 0xFF);
      if (huffLength >= bitsLeft) {
        huffLength -= bitsLeft;
        bitsLeft = 8;
      } else {
        bitsLeft -= huffLength;
        huffLength = 0;
      }
    }

    while (huffLength >= 8) {
      uint32_t mask = ~((1 << (huffLength - 8)) - 1);
      uint8_t val = ((huffValue & mask) >> (huffLength - 8)) & 0xFF;
      buf.Append(reinterpret_cast<char*>(&val), 1);
      huffLength -= 8;
    }

    if (huffLength) {
      // Fill in the most significant <huffLength> bits of the next byte
      bitsLeft = 8 - huffLength;
      uint8_t val = (huffValue & ((1 << huffLength) - 1)) << bitsLeft;
      buf.Append(reinterpret_cast<char*>(&val), 1);
    }
  }

  if (bitsLeft != 8) {
    // Pad the last <bitsLeft> bits with ones, which corresponds to the EOS
    // encoding
    uint8_t val = (1 << bitsLeft) - 1;
    offset = buf.Length() - 1;
    startByte = reinterpret_cast<unsigned char*>(buf.BeginWriting()) + offset;
    *startByte = *startByte | val;
  }

  // Now we know how long our encoded string is, we can fill in our length
  uint32_t bufLength = buf.Length();
  offset = mOutput->Length();
  EncodeInteger(7, bufLength);
  startByte =
      reinterpret_cast<unsigned char*>(mOutput->BeginWriting()) + offset;
  *startByte = *startByte | 0x80;

  // Finally, we can add our REAL data!
  mOutput->Append(buf);
  LOG(
      ("Http2Compressor::HuffmanAppend %p encoded %d byte original on %d "
       "bytes.\n",
       this, length, bufLength));
}

void Http2Compressor::ProcessHeader(const nvPair inputPair, bool noLocalIndex,
                                    bool neverIndex) {
  uint32_t newSize = inputPair.Size();
  uint32_t headerTableSize = mHeaderTable.Length();
  uint32_t matchedIndex = 0u;
  uint32_t nameReference = 0u;
  bool match = false;

  LOG(("Http2Compressor::ProcessHeader %s %s", inputPair.mName.get(),
       inputPair.mValue.get()));

  // NWGH - make this index = 1; index <= headerTableSize; ++index
  for (uint32_t index = 0; index < headerTableSize; ++index) {
    if (mHeaderTable[index]->mName.Equals(inputPair.mName)) {
      // NWGH - make this nameReference = index
      nameReference = index + 1;
      if (mHeaderTable[index]->mValue.Equals(inputPair.mValue)) {
        match = true;
        matchedIndex = index;
        break;
      }
    }
  }

  // We need to emit a new literal
  if (!match || noLocalIndex || neverIndex) {
    if (neverIndex) {
      DoOutput(kNeverIndexedLiteral, &inputPair, nameReference);
      DumpState("Compressor state after literal never index");
      return;
    }

    if (noLocalIndex || (newSize > (mMaxBuffer / 2)) || (mMaxBuffer < 128)) {
      DoOutput(kPlainLiteral, &inputPair, nameReference);
      DumpState("Compressor state after literal without index");
      return;
    }

    // make sure to makeroom() first so that any implied items
    // get preserved.
    MakeRoom(newSize, "compressor");
    DoOutput(kIndexedLiteral, &inputPair, nameReference);

    mHeaderTable.AddElement(inputPair.mName, inputPair.mValue);
    LOG(("HTTP compressor %p new literal placed at index 0\n", this));
    DumpState("Compressor state after literal with index");
    return;
  }

  // emit an index
  DoOutput(kIndex, &inputPair, matchedIndex);

  DumpState("Compressor state after index");
}

void Http2Compressor::EncodeTableSizeChange(uint32_t newMaxSize) {
  uint32_t offset = mOutput->Length();
  EncodeInteger(5, newMaxSize);
  uint8_t* startByte =
      reinterpret_cast<uint8_t*>(mOutput->BeginWriting()) + offset;
  *startByte = *startByte | 0x20;
}

void Http2Compressor::SetMaxBufferSize(uint32_t maxBufferSize) {
  mMaxBufferSetting = maxBufferSize;
  SetMaxBufferSizeInternal(maxBufferSize);
  if (!mBufferSizeChangeWaiting) {
    mBufferSizeChangeWaiting = true;
    mLowestBufferSizeWaiting = maxBufferSize;
  } else if (maxBufferSize < mLowestBufferSizeWaiting) {
    mLowestBufferSizeWaiting = maxBufferSize;
  }
}

}  // namespace net
}  // namespace mozilla
