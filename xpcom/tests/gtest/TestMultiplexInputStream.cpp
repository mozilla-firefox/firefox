/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Helpers.h"
#include "gtest/gtest.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/ipc/DataPipe.h"
#include "nsComponentManagerUtils.h"
#include "nsIAsyncInputStream.h"
#include "nsIAsyncOutputStream.h"
#include "nsICloneableInputStream.h"
#include "nsIInputStream.h"
#include "nsIMultiplexInputStream.h"
#include "nsIPipe.h"
#include "nsISeekableStream.h"
#include "nsIThread.h"
#include "nsStreamUtils.h"
#include "nsThreadUtils.h"

using mozilla::GetCurrentSerialEventTarget;
using mozilla::SpinEventLoopUntil;

TEST(MultiplexInputStream, Seek_SET)
{
  nsCString buf1;
  nsCString buf2;
  nsCString buf3;
  buf1.AssignLiteral("Hello world");
  buf2.AssignLiteral("The quick brown fox jumped over the lazy dog");
  buf3.AssignLiteral("Foo bar");

  nsCOMPtr<nsIInputStream> inputStream1;
  nsCOMPtr<nsIInputStream> inputStream2;
  nsCOMPtr<nsIInputStream> inputStream3;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream1), buf1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream2), buf2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream3), buf3);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_TRUE(multiplexStream);
  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(stream);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream3);
  ASSERT_NS_SUCCEEDED(rv);

  int64_t tell;
  uint64_t length;
  uint32_t count;
  char readBuf[4096];
  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(seekStream);

  // Seek forward in first input stream
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_SET, 1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf1.Length() + buf2.Length() + buf3.Length() - 1,
            length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 1);

  // Check read is correct
  rv = stream->Read(readBuf, 3, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)3, count);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf1.Length() + buf2.Length() + buf3.Length() - 4,
            length);
  ASSERT_EQ(0, strncmp(readBuf, "ell", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 4);

  // Seek to start of third input stream
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_SET,
                        buf1.Length() + buf2.Length());
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf3.Length(), length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length() + buf2.Length()));

  // Check read is correct
  rv = stream->Read(readBuf, 5, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)5, count);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf3.Length() - 5, length);
  ASSERT_EQ(0, strncmp(readBuf, "Foo b", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length() + buf2.Length() + 5));

  // Seek back to start of second stream (covers bug 1272371)
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_SET, buf1.Length());
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf2.Length() + buf3.Length(), length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length()));

  // Check read is correct
  rv = stream->Read(readBuf, 6, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)6, count);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf2.Length() - 6 + buf3.Length(), length);
  ASSERT_EQ(0, strncmp(readBuf, "The qu", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length() + 6));
}

TEST(MultiplexInputStream, Seek_CUR)
{
  nsCString buf1;
  nsCString buf2;
  nsCString buf3;
  buf1.AssignLiteral("Hello world");
  buf2.AssignLiteral("The quick brown fox jumped over the lazy dog");
  buf3.AssignLiteral("Foo bar");

  nsCOMPtr<nsIInputStream> inputStream1;
  nsCOMPtr<nsIInputStream> inputStream2;
  nsCOMPtr<nsIInputStream> inputStream3;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream1), buf1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream2), buf2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream3), buf3);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_TRUE(multiplexStream);
  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(stream);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream3);
  ASSERT_NS_SUCCEEDED(rv);

  int64_t tell;
  uint64_t length;
  uint32_t count;
  char readBuf[4096];
  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(seekStream);

  // Seek forward in first input stream
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_CUR, 1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf1.Length() + buf2.Length() + buf3.Length() - 1,
            length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 1);

  // Check read is correct
  rv = stream->Read(readBuf, 3, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)3, count);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)buf1.Length() + buf2.Length() + buf3.Length() - 4,
            length);
  ASSERT_EQ(0, strncmp(readBuf, "ell", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 4);

  // Let's go to the second stream
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_CUR, 11);
  ASSERT_NS_SUCCEEDED(rv);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 15);
  rv = stream->Read(readBuf, 3, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)3, count);
  ASSERT_EQ(0, strncmp(readBuf, "qui", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 18);

  // Let's go back to the first stream
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_CUR, -9);
  ASSERT_NS_SUCCEEDED(rv);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 9);
  rv = stream->Read(readBuf, 3, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)3, count);
  ASSERT_EQ(0, strncmp(readBuf, "ldT", count));
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 12);
}

TEST(MultiplexInputStream, Seek_END)
{
  nsCString buf1;
  nsCString buf2;
  nsCString buf3;
  buf1.AssignLiteral("Hello world");
  buf2.AssignLiteral("The quick brown fox jumped over the lazy dog");
  buf3.AssignLiteral("Foo bar");

  nsCOMPtr<nsIInputStream> inputStream1;
  nsCOMPtr<nsIInputStream> inputStream2;
  nsCOMPtr<nsIInputStream> inputStream3;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream1), buf1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream2), buf2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream3), buf3);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_TRUE(multiplexStream);
  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(stream);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream3);
  ASSERT_NS_SUCCEEDED(rv);

  int64_t tell;
  uint64_t length;
  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(seekStream);

  // SEEK_END wants <=0 values
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_END, 1);
  ASSERT_NS_FAILED(rv);

  // Let's go to the end.
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_END, 0);
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)0, length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length() + buf2.Length() + buf3.Length()));

  // -1
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_END, -1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ((uint64_t)1, length);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, int64_t(buf1.Length() + buf2.Length() + buf3.Length() - 1));

  // Almost at the beginning
  tell = 1;
  tell -= buf1.Length();
  tell -= buf2.Length();
  tell -= buf3.Length();
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_END, tell);
  ASSERT_NS_SUCCEEDED(rv);
  rv = stream->Available(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(length, buf1.Length() + buf2.Length() + buf3.Length() - 1);
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(tell, 1);
}

static already_AddRefed<nsIInputStream> CreateStreamHelper() {
  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  nsCString buf1;
  buf1.AssignLiteral("Hello");

  RefPtr inputStream1 = mozilla::MakeRefPtr<testing::AsyncStringStream>(buf1);
  multiplexStream->AppendStream(inputStream1);

  nsCString buf2;
  buf2.AssignLiteral(" ");

  RefPtr inputStream2 = mozilla::MakeRefPtr<testing::AsyncStringStream>(buf2);
  multiplexStream->AppendStream(inputStream2);

  nsCString buf3;
  buf3.AssignLiteral("World!");

  RefPtr inputStream3 = mozilla::MakeRefPtr<testing::AsyncStringStream>(buf3);
  multiplexStream->AppendStream(inputStream3);

  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  return stream.forget();
}

// AsyncWait - without EventTarget
TEST(MultiplexInputStream, AsyncWait_withoutEventTarget)
{
  nsCOMPtr<nsIInputStream> is = CreateStreamHelper();

  nsCOMPtr<nsIAsyncInputStream> ais = do_QueryInterface(is);
  ASSERT_TRUE(!!ais);

  RefPtr cb = mozilla::MakeRefPtr<testing::InputStreamCallback>();

  ASSERT_EQ(NS_OK, ais->AsyncWait(cb, 0, 0, nullptr));
  ASSERT_TRUE(cb->Called());
}

// AsyncWait - with EventTarget
TEST(MultiplexInputStream, AsyncWait_withEventTarget)
{
  nsCOMPtr<nsIInputStream> is = CreateStreamHelper();

  nsCOMPtr<nsIAsyncInputStream> ais = do_QueryInterface(is);
  ASSERT_TRUE(!!ais);

  RefPtr cb = mozilla::MakeRefPtr<testing::InputStreamCallback>();
  nsCOMPtr<nsIThread> thread = do_GetCurrentThread();

  ASSERT_EQ(NS_OK, ais->AsyncWait(cb, 0, 0, thread));

  ASSERT_FALSE(cb->Called());

  // Eventually it is called.
  MOZ_ALWAYS_TRUE(mozilla::SpinEventLoopUntil(
      "xpcom:TEST(MultiplexInputStream, AsyncWait_withEventTarget)"_ns,
      [&]() { return cb->Called(); }));
  ASSERT_TRUE(cb->Called());
}

// AsyncWait - without EventTarget - closureOnly
TEST(MultiplexInputStream, AsyncWait_withoutEventTarget_closureOnly)
{
  nsCOMPtr<nsIInputStream> is = CreateStreamHelper();

  nsCOMPtr<nsIAsyncInputStream> ais = do_QueryInterface(is);
  ASSERT_TRUE(!!ais);

  RefPtr cb = mozilla::MakeRefPtr<testing::InputStreamCallback>();

  ASSERT_EQ(NS_OK, ais->AsyncWait(cb, nsIAsyncInputStream::WAIT_CLOSURE_ONLY, 0,
                                  nullptr));
  ASSERT_FALSE(cb->Called());

  ais->CloseWithStatus(NS_ERROR_FAILURE);
  ASSERT_TRUE(cb->Called());
}

// AsyncWait - with EventTarget - closureOnly
TEST(MultiplexInputStream, AsyncWait_withEventTarget_closureOnly)
{
  nsCOMPtr<nsIInputStream> is = CreateStreamHelper();

  nsCOMPtr<nsIAsyncInputStream> ais = do_QueryInterface(is);
  ASSERT_TRUE(!!ais);

  RefPtr cb = mozilla::MakeRefPtr<testing::InputStreamCallback>();
  nsCOMPtr<nsIThread> thread = do_GetCurrentThread();

  ASSERT_EQ(NS_OK, ais->AsyncWait(cb, nsIAsyncInputStream::WAIT_CLOSURE_ONLY, 0,
                                  thread));

  ASSERT_FALSE(cb->Called());
  ais->CloseWithStatus(NS_ERROR_FAILURE);
  ASSERT_FALSE(cb->Called());

  // Eventually it is called.
  MOZ_ALWAYS_TRUE(mozilla::SpinEventLoopUntil(
      "xpcom:TEST(MultiplexInputStream, AsyncWait_withEventTarget_closureOnly)"_ns,
      [&]() { return cb->Called(); }));
  ASSERT_TRUE(cb->Called());
}

class ClosedStream final : public nsIInputStream {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  ClosedStream() = default;

  NS_IMETHOD
  Available(uint64_t* aLength) override { return NS_BASE_STREAM_CLOSED; }

  NS_IMETHOD
  StreamStatus() override { return NS_BASE_STREAM_CLOSED; }

  NS_IMETHOD
  Read(char* aBuffer, uint32_t aCount, uint32_t* aReadCount) override {
    MOZ_CRASH("This should not be called!");
    return NS_OK;
  }

  NS_IMETHOD
  ReadSegments(nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
               uint32_t* aResult) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD
  Close() override { return NS_OK; }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) override {
    *aNonBlocking = true;
    return NS_OK;
  }

 private:
  ~ClosedStream() = default;
};

NS_IMPL_ISUPPORTS(ClosedStream, nsIInputStream)

class AsyncStream final : public nsIAsyncInputStream {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit AsyncStream(int64_t aSize) : mState(eBlocked), mSize(aSize) {}

  void Unblock() { mState = eUnblocked; }

  NS_IMETHOD
  Available(uint64_t* aLength) override {
    *aLength = mState == eBlocked ? 0 : mSize;
    return mState == eClosed ? NS_BASE_STREAM_CLOSED : NS_OK;
  }

  NS_IMETHOD
  StreamStatus() override {
    return mState == eClosed ? NS_BASE_STREAM_CLOSED : NS_OK;
  }

  NS_IMETHOD
  Read(char* aBuffer, uint32_t aCount, uint32_t* aReadCount) override {
    MOZ_CRASH("This should not be called!");
    return NS_OK;
  }

  NS_IMETHOD
  ReadSegments(nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
               uint32_t* aResult) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD
  Close() override {
    mState = eClosed;
    return NS_OK;
  }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) override {
    *aNonBlocking = true;
    return NS_OK;
  }

  NS_IMETHOD
  AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
            uint32_t aRequestedCount, nsIEventTarget* aEventTarget) override {
    MOZ_CRASH("This should not be called!");
    return NS_OK;
  }

  NS_IMETHOD
  CloseWithStatus(nsresult aStatus) override { return NS_OK; }

 private:
  ~AsyncStream() = default;

  enum { eBlocked, eUnblocked, eClosed } mState;

  uint64_t mSize;
};

NS_IMPL_ISUPPORTS(AsyncStream, nsIInputStream, nsIAsyncInputStream)

class BlockingStream final : public nsIInputStream {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  BlockingStream() = default;

  NS_IMETHOD
  Available(uint64_t* aLength) override { return NS_BASE_STREAM_CLOSED; }

  NS_IMETHOD
  StreamStatus() override { return NS_BASE_STREAM_CLOSED; }

  NS_IMETHOD
  Read(char* aBuffer, uint32_t aCount, uint32_t* aReadCount) override {
    // We are actually empty.
    *aReadCount = 0;
    return NS_OK;
  }

  NS_IMETHOD
  ReadSegments(nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
               uint32_t* aResult) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD
  Close() override { return NS_OK; }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) override {
    *aNonBlocking = false;
    return NS_OK;
  }

 private:
  ~BlockingStream() = default;
};

NS_IMPL_ISUPPORTS(BlockingStream, nsIInputStream)

TEST(MultiplexInputStream, Available)
{
  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  nsCOMPtr<nsIInputStream> s = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!s);

  nsCOMPtr<nsIAsyncInputStream> as = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!as);

  uint64_t length;

  // The stream returns NS_BASE_STREAM_CLOSED if there are no substreams.
  nsresult rv = s->Available(&length);
  ASSERT_EQ(NS_BASE_STREAM_CLOSED, rv);

  RefPtr closedStream = mozilla::MakeRefPtr<ClosedStream>();
  rv = multiplexStream->AppendStream(closedStream);
  ASSERT_EQ(NS_OK, rv);

  uint64_t asyncSize = 2;
  RefPtr asyncStream = mozilla::MakeRefPtr<AsyncStream>(2);
  rv = multiplexStream->AppendStream(asyncStream);
  ASSERT_EQ(NS_OK, rv);

  nsCString buffer;
  buffer.Assign("World!!!");

  nsCOMPtr<nsIInputStream> stringStream;
  rv = NS_NewCStringInputStream(getter_AddRefs(stringStream), buffer);
  ASSERT_EQ(NS_OK, rv);

  rv = multiplexStream->AppendStream(stringStream);
  ASSERT_EQ(NS_OK, rv);

  // Now we are async.
  as = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!as);

  // Available should skip the closed stream and return 0 because the
  // asyncStream returns 0 and it's async.
  rv = s->Available(&length);
  ASSERT_EQ(NS_OK, rv);
  ASSERT_EQ((uint64_t)0, length);

  asyncStream->Unblock();

  // Now we should return only the size of the async stream because we don't
  // know when this is completed.
  rv = s->Available(&length);
  ASSERT_EQ(NS_OK, rv);
  ASSERT_EQ(asyncSize, length);

  asyncStream->Close();

  rv = s->Available(&length);
  ASSERT_EQ(NS_OK, rv);
  ASSERT_EQ(buffer.Length(), length);
}

class NonBufferableStringStream final : public nsIInputStream {
  nsCOMPtr<nsIInputStream> mStream;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit NonBufferableStringStream(const nsACString& aBuffer) {
    NS_NewCStringInputStream(getter_AddRefs(mStream), aBuffer);
  }

  NS_IMETHOD
  Available(uint64_t* aLength) override { return mStream->Available(aLength); }

  NS_IMETHOD
  StreamStatus() override { return mStream->StreamStatus(); }

  NS_IMETHOD
  Read(char* aBuffer, uint32_t aCount, uint32_t* aReadCount) override {
    return mStream->Read(aBuffer, aCount, aReadCount);
  }

  NS_IMETHOD
  ReadSegments(nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
               uint32_t* aResult) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  NS_IMETHOD
  Close() override { return mStream->Close(); }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) override {
    return mStream->IsNonBlocking(aNonBlocking);
  }

 private:
  ~NonBufferableStringStream() = default;
};

NS_IMPL_ISUPPORTS(NonBufferableStringStream, nsIInputStream)

TEST(MultiplexInputStream, Bufferable)
{
  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  nsCOMPtr<nsIInputStream> s = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!s);

  nsCString buf1;
  buf1.AssignLiteral("Hello ");
  nsCOMPtr<nsIInputStream> inputStream1;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream1), buf1);
  ASSERT_NS_SUCCEEDED(rv);

  nsCString buf2;
  buf2.AssignLiteral("world");
  RefPtr inputStream2 = mozilla::MakeRefPtr<NonBufferableStringStream>(buf2);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);

  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(!!stream);

  char buf3[1024];
  uint32_t size = 0;
  rv = stream->ReadSegments(NS_CopySegmentToBuffer, buf3, sizeof(buf3), &size);
  ASSERT_NS_SUCCEEDED(rv);

  ASSERT_EQ(size, buf1.Length() + buf2.Length());
  ASSERT_TRUE(!strncmp(buf3, "Hello world", size));
}

TEST(MultiplexInputStream, QILengthInputStream)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  // nsMultiplexInputStream doesn't expose nsIInputStreamLength if there are
  // no nsIInputStreamLength sub streams.
  {
    nsCOMPtr<nsIInputStream> inputStream;
    nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream), buf);
    ASSERT_NS_SUCCEEDED(rv);

    rv = multiplexStream->AppendStream(inputStream);
    ASSERT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIInputStreamLength> fsis = do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!fsis);

    nsCOMPtr<nsIAsyncInputStreamLength> afsis =
        do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!afsis);
  }

  // nsMultiplexInputStream exposes nsIInputStreamLength if there is one or
  // more nsIInputStreamLength sub streams.
  {
    RefPtr inputStream =
        mozilla::MakeRefPtr<testing::LengthInputStream>(buf, true, false);

    nsresult rv = multiplexStream->AppendStream(inputStream);
    ASSERT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIInputStreamLength> fsis = do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!!fsis);

    nsCOMPtr<nsIAsyncInputStreamLength> afsis =
        do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!afsis);
  }

  // nsMultiplexInputStream exposes nsIAsyncInputStreamLength if there is one
  // or more nsIAsyncInputStreamLength sub streams.
  {
    RefPtr inputStream =
        mozilla::MakeRefPtr<testing::LengthInputStream>(buf, true, true);

    nsresult rv = multiplexStream->AppendStream(inputStream);
    ASSERT_NS_SUCCEEDED(rv);

    nsCOMPtr<nsIInputStreamLength> fsis = do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!!fsis);

    nsCOMPtr<nsIAsyncInputStreamLength> afsis =
        do_QueryInterface(multiplexStream);
    ASSERT_TRUE(!!afsis);
  }
}

TEST(MultiplexInputStream, LengthInputStream)
{
  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");

  // First stream is a a simple one.
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> inputStream;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream), buf);
  ASSERT_NS_SUCCEEDED(rv);

  rv = multiplexStream->AppendStream(inputStream);
  ASSERT_NS_SUCCEEDED(rv);

  // A LengthInputStream, non-async.
  RefPtr lengthStream =
      mozilla::MakeRefPtr<testing::LengthInputStream>(buf, true, false);

  rv = multiplexStream->AppendStream(lengthStream);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIInputStreamLength> fsis = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!fsis);

  // Size is the sum of the 2 streams.
  int64_t length;
  rv = fsis->Length(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(int64_t(buf.Length() * 2), length);

  // An async LengthInputStream.
  RefPtr asyncLengthStream = mozilla::MakeRefPtr<testing::LengthInputStream>(
      buf, true, true, NS_BASE_STREAM_WOULD_BLOCK);

  rv = multiplexStream->AppendStream(asyncLengthStream);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIAsyncInputStreamLength> afsis =
      do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!afsis);

  // Now it would block.
  rv = fsis->Length(&length);
  ASSERT_EQ(NS_BASE_STREAM_WOULD_BLOCK, rv);

  // Let's read the size async.
  RefPtr callback = mozilla::MakeRefPtr<testing::LengthCallback>();
  rv = afsis->AsyncLengthWait(callback, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "xpcom:TEST(MultiplexInputStream, LengthInputStream) 1"_ns,
      [&]() { return callback->Called(); }));
  ASSERT_EQ(int64_t(buf.Length() * 3), callback->Size());

  // Now a negative stream
  lengthStream = mozilla::MakeRefPtr<testing::LengthInputStream>(
      buf, true, false, NS_OK, true);

  rv = multiplexStream->AppendStream(lengthStream);
  ASSERT_NS_SUCCEEDED(rv);

  rv = fsis->Length(&length);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(-1, length);

  // Another async LengthInputStream.
  asyncLengthStream = mozilla::MakeRefPtr<testing::LengthInputStream>(
      buf, true, true, NS_BASE_STREAM_WOULD_BLOCK);

  rv = multiplexStream->AppendStream(asyncLengthStream);
  ASSERT_NS_SUCCEEDED(rv);

  afsis = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(!!afsis);

  // Let's read the size async.
  RefPtr callback1 = mozilla::MakeRefPtr<testing::LengthCallback>();
  rv = afsis->AsyncLengthWait(callback1, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  RefPtr callback2 = mozilla::MakeRefPtr<testing::LengthCallback>();
  rv = afsis->AsyncLengthWait(callback2, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "xpcom:TEST(MultiplexInputStream, LengthInputStream) 2"_ns,
      [&]() { return callback2->Called(); }));
  ASSERT_FALSE(callback1->Called());
  ASSERT_TRUE(callback2->Called());
}

void TestMultiplexStreamReadWhileWaiting(nsIAsyncInputStream* pipeIn,
                                         nsIAsyncOutputStream* pipeOut) {
  // We had an issue where a stream which was read while a message was in-flight
  // to report the stream was ready, meaning that the stream reported 0 bytes
  // available when checked in the MultiplexInputStream's callback, and was
  // skipped over.

  nsCOMPtr<nsIThread> mainThread = NS_GetCurrentThread();

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_NS_SUCCEEDED(multiplexStream->AppendStream(pipeIn));

  nsCOMPtr<nsIInputStream> stringStream;
  ASSERT_TRUE(NS_SUCCEEDED(
      NS_NewCStringInputStream(getter_AddRefs(stringStream), "xxxx\0"_ns)));
  ASSERT_NS_SUCCEEDED(multiplexStream->AppendStream(stringStream));

  nsCOMPtr<nsIAsyncInputStream> asyncMultiplex =
      do_QueryInterface(multiplexStream);
  ASSERT_TRUE(asyncMultiplex);

  RefPtr cb = mozilla::MakeRefPtr<testing::InputStreamCallback>();
  ASSERT_NS_SUCCEEDED(asyncMultiplex->AsyncWait(cb, 0, 0, mainThread));
  EXPECT_FALSE(cb->Called());

  NS_ProcessPendingEvents(mainThread);
  EXPECT_FALSE(cb->Called());

  uint64_t available;
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, 0u);

  // Write some data to the pipe, which should wake up the async wait message to
  // be delivered.
  char toWrite[] = "1234";
  uint32_t written;
  ASSERT_NS_SUCCEEDED(pipeOut->Write(toWrite, sizeof(toWrite), &written));
  EXPECT_EQ(written, sizeof(toWrite));
  EXPECT_FALSE(cb->Called());
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, sizeof(toWrite));

  // Read that data from the stream
  char toRead[sizeof(toWrite)];
  uint32_t read;
  ASSERT_TRUE(
      NS_SUCCEEDED(asyncMultiplex->Read(toRead, sizeof(toRead), &read)));
  EXPECT_EQ(read, sizeof(toRead));
  EXPECT_STREQ(toRead, toWrite);
  EXPECT_FALSE(cb->Called());
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, 0u);

  // The multiplex stream will have detected the read and prevented the callback
  // from having been called yet.
  NS_ProcessPendingEvents(mainThread);
  EXPECT_FALSE(cb->Called());
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, 0u);

  // Write more data and close, then make sure we can read everything else in
  // the stream.
  char toWrite2[] = "56789";
  ASSERT_TRUE(
      NS_SUCCEEDED(pipeOut->Write(toWrite2, sizeof(toWrite2), &written)));
  EXPECT_EQ(written, sizeof(toWrite2));
  EXPECT_FALSE(cb->Called());
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, sizeof(toWrite2));

  ASSERT_NS_SUCCEEDED(pipeOut->Close());
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  // XXX: Theoretically if the multiplex stream could detect it, we could report
  // `sizeof(toWrite2) + 4` because the stream is complete, but there's no way
  // for the multiplex stream to know.
  EXPECT_EQ(available, sizeof(toWrite2));

  NS_ProcessPendingEvents(mainThread);
  EXPECT_TRUE(cb->Called());

  // Read that final bit of data and make sure we read it.
  char toRead2[sizeof(toWrite2)];
  ASSERT_TRUE(
      NS_SUCCEEDED(asyncMultiplex->Read(toRead2, sizeof(toRead2), &read)));
  EXPECT_EQ(read, sizeof(toRead2));
  EXPECT_STREQ(toRead2, toWrite2);
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, 5u);

  // Read the extra data as well.
  char extraRead[5];
  ASSERT_TRUE(
      NS_SUCCEEDED(asyncMultiplex->Read(extraRead, sizeof(extraRead), &read)));
  EXPECT_EQ(read, sizeof(extraRead));
  EXPECT_STREQ(extraRead, "xxxx");
  ASSERT_NS_SUCCEEDED(asyncMultiplex->Available(&available));
  EXPECT_EQ(available, 0u);
}

// Seeking backward with NS_SEEK_CUR to position 0 should reset the read
// state, allowing Clone/Length operations that require an unread stream.
TEST(MultiplexInputStream, Seek_CUR_Backward_To_Start)
{
  nsCString buf1;
  nsCString buf2;
  buf1.AssignLiteral("Hello");
  buf2.AssignLiteral("World");

  nsCOMPtr<nsIInputStream> inputStream1;
  nsCOMPtr<nsIInputStream> inputStream2;
  nsresult rv = NS_NewCStringInputStream(getter_AddRefs(inputStream1), buf1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream2), buf2);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_TRUE(multiplexStream);
  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(stream);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(seekStream);
  nsCOMPtr<nsICloneableInputStream> cloneableStream =
      do_QueryInterface(multiplexStream);
  ASSERT_TRUE(cloneableStream);

  // Read 8 bytes, crossing the stream boundary
  char readBuf[4096];
  uint32_t count;
  rv = stream->Read(readBuf, 8, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(8u, count);
  ASSERT_EQ(0, strncmp(readBuf, "HelloWor", 8));

  // No longer cloneable since we've been reading
  bool cloneable;
  rv = cloneableStream->GetCloneable(&cloneable);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_FALSE(cloneable);

  // Seek backward to position 0 across stream boundaries
  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_CUR, -8);
  ASSERT_NS_SUCCEEDED(rv);

  int64_t tell;
  rv = seekStream->Tell(&tell);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(0, tell);

  // Should be cloneable again since we've fully rewound to the start
  rv = cloneableStream->GetCloneable(&cloneable);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_TRUE(cloneable);

  // Verify reading produces the correct data
  rv = stream->Read(readBuf, sizeof(readBuf), &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(10u, count);
  ASSERT_EQ(0, strncmp(readBuf, "HelloWorld", 10));
}

// Reading exactly Available() bytes leaves the multiplex stream positioned in
// its last stream rather than past it; seeking to 0 from there must still
// reset the read state.
TEST(MultiplexInputStream, Seek_SET_To_Start_After_Reading_Available)
{
  nsCOMPtr<nsIInputStream> inputStream1;
  nsCOMPtr<nsIInputStream> inputStream2;
  nsresult rv =
      NS_NewCStringInputStream(getter_AddRefs(inputStream1), "Hello"_ns);
  ASSERT_NS_SUCCEEDED(rv);
  rv = NS_NewCStringInputStream(getter_AddRefs(inputStream2), "World"_ns);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1");
  ASSERT_TRUE(multiplexStream);
  nsCOMPtr<nsIInputStream> stream(do_QueryInterface(multiplexStream));
  ASSERT_TRUE(stream);

  rv = multiplexStream->AppendStream(inputStream1);
  ASSERT_NS_SUCCEEDED(rv);
  rv = multiplexStream->AppendStream(inputStream2);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(multiplexStream);
  ASSERT_TRUE(seekStream);

  uint64_t available;
  rv = stream->Available(&available);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(10u, available);

  char readBuf[4096];
  uint32_t count;
  rv = stream->Read(readBuf, available, &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(10u, count);
  ASSERT_FALSE(NS_InputStreamIsCloneable(stream));

  rv = seekStream->Seek(nsISeekableStream::NS_SEEK_SET, 0);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_TRUE(NS_InputStreamIsCloneable(stream));

  nsCOMPtr<nsIInputStream> clone;
  rv = NS_CloneInputStream(stream, getter_AddRefs(clone));
  ASSERT_NS_SUCCEEDED(rv);

  rv = clone->Read(readBuf, sizeof(readBuf), &count);
  ASSERT_NS_SUCCEEDED(rv);
  ASSERT_EQ(10u, count);
  ASSERT_EQ(0, strncmp(readBuf, "HelloWorld", 10));
}

TEST(MultiplexInputStream, ReadWhileWaiting_nsPipe)
{
  nsCOMPtr<nsIAsyncInputStream> pipeIn;
  nsCOMPtr<nsIAsyncOutputStream> pipeOut;
  NS_NewPipe2(getter_AddRefs(pipeIn), getter_AddRefs(pipeOut), true, true);
  TestMultiplexStreamReadWhileWaiting(pipeIn, pipeOut);
}

TEST(MultiplexInputStream, ReadWhileWaiting_DataPipe)
{
  RefPtr<mozilla::ipc::DataPipeReceiver> pipeIn;
  RefPtr<mozilla::ipc::DataPipeSender> pipeOut;
  ASSERT_TRUE(NS_SUCCEEDED(mozilla::ipc::NewDataPipe(
      mozilla::ipc::kDefaultDataPipeCapacity, getter_AddRefs(pipeOut),
      getter_AddRefs(pipeIn))));
  TestMultiplexStreamReadWhileWaiting(pipeIn, pipeOut);
}
