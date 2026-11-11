#include "Helpers.h"
#include "gtest/gtest.h"
#include "mozilla/SlicedInputStream.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsCOMPtr.h"
#include "nsIInputStream.h"
#include "nsIPipe.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringStream.h"

using namespace mozilla;

// This helper class is used to call OnInputStreamReady with the right stream
// as argument.
class InputStreamCallback final : public nsIInputStreamCallback {
  nsCOMPtr<nsIAsyncInputStream> mStream;
  nsCOMPtr<nsIInputStreamCallback> mCallback;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  InputStreamCallback(nsIAsyncInputStream* aStream,
                      nsIInputStreamCallback* aCallback)
      : mStream(aStream), mCallback(aCallback) {}

  NS_IMETHOD
  OnInputStreamReady(nsIAsyncInputStream* aStream) override {
    return mCallback->OnInputStreamReady(mStream);
  }

 private:
  ~InputStreamCallback() = default;
};

NS_IMPL_ISUPPORTS(InputStreamCallback, nsIInputStreamCallback)

/* We want to ensure that sliced streams work with both seekable and
 * non-seekable input streams.  As our string streams are seekable, we need to
 * provide a string stream that doesn't permit seeking, so we can test the
 * logic that emulates seeking in sliced input streams.
 */
class NonSeekableStringStream final : public nsIAsyncInputStream {
  nsCOMPtr<nsIInputStream> mStream;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit NonSeekableStringStream(const nsACString& aBuffer) {
    NS_NewCStringInputStream(getter_AddRefs(mStream), aBuffer);
  }

  explicit NonSeekableStringStream(nsIInputStream* aStream)
      : mStream(aStream) {}

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
    return mStream->ReadSegments(aWriter, aClosure, aCount, aResult);
  }

  NS_IMETHOD
  Close() override { return mStream->Close(); }

  NS_IMETHOD
  IsNonBlocking(bool* aNonBlocking) override {
    return mStream->IsNonBlocking(aNonBlocking);
  }

  NS_IMETHOD
  CloseWithStatus(nsresult aStatus) override {
    nsCOMPtr<nsIAsyncInputStream> async = do_QueryInterface(mStream);
    if (!async) {
      MOZ_CRASH("This should not happen.");
      return NS_ERROR_FAILURE;
    }

    return async->CloseWithStatus(aStatus);
  }

  NS_IMETHOD
  AsyncWait(nsIInputStreamCallback* aCallback, uint32_t aFlags,
            uint32_t aRequestedCount, nsIEventTarget* aEventTarget) override {
    nsCOMPtr<nsIAsyncInputStream> async = do_QueryInterface(mStream);
    if (!async) {
      MOZ_CRASH("This should not happen.");
      return NS_ERROR_FAILURE;
    }

    RefPtr callback = MakeRefPtr<InputStreamCallback>(this, aCallback);

    return async->AsyncWait(callback, aFlags, aRequestedCount, aEventTarget);
  }

 private:
  ~NonSeekableStringStream() = default;
};

NS_IMPL_ISUPPORTS(NonSeekableStringStream, nsIInputStream, nsIAsyncInputStream)

// Helper function for creating a seekable nsIInputStream + a SlicedInputStream.
static RefPtr<SlicedInputStream> CreateSeekableStreams(uint32_t aSize,
                                                       uint64_t aStart,
                                                       uint64_t aLength,
                                                       nsCString& aBuffer) {
  aBuffer.SetLength(aSize);
  for (uint32_t i = 0; i < aSize; ++i) {
    aBuffer.BeginWriting()[i] = i % 10;
  }

  nsCOMPtr<nsIInputStream> stream;
  NS_NewCStringInputStream(getter_AddRefs(stream), aBuffer);
  return MakeRefPtr<SlicedInputStream>(stream.forget(), aStart, aLength);
}

// Helper function for creating a non-seekable nsIInputStream + a
// SlicedInputStream.
static RefPtr<SlicedInputStream> CreateNonSeekableStreams(uint32_t aSize,
                                                          uint64_t aStart,
                                                          uint64_t aLength,
                                                          nsCString& aBuffer) {
  aBuffer.SetLength(aSize);
  for (uint32_t i = 0; i < aSize; ++i) {
    aBuffer.BeginWriting()[i] = i % 10;
  }

  RefPtr stream = MakeRefPtr<NonSeekableStringStream>(aBuffer);
  return MakeRefPtr<SlicedInputStream>(stream.forget(), aStart, aLength);
}

// Same start, same length.
TEST(TestSlicedInputStream, Simple)
{
  const size_t kBufSize = 4096;

  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateSeekableStreams(kBufSize, 0, kBufSize, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)kBufSize, length);

  char buf2[kBufSize];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ(count, buf.Length());
  ASSERT_TRUE(nsCString(buf.get()).Equals(nsCString(buf2)));
}

// Simple sliced stream - seekable
TEST(TestSlicedInputStream, Sliced)
{
  const size_t kBufSize = 4096;

  nsCString buf;
  RefPtr<SlicedInputStream> sis = CreateSeekableStreams(kBufSize, 10, 100, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)100, length);

  char buf2[kBufSize / 2];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)100, count);
  ASSERT_TRUE(nsCString(buf.get() + 10, count).Equals(nsCString(buf2, count)));
}

// Simple sliced stream - non seekable
TEST(TestSlicedInputStream, SlicedNoSeek)
{
  const size_t kBufSize = 4096;

  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateNonSeekableStreams(kBufSize, 10, 100, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)100, length);

  char buf2[kBufSize / 2];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)100, count);
  ASSERT_TRUE(nsCString(buf.get() + 10, count).Equals(nsCString(buf2, count)));
}

// Big inputStream - seekable
TEST(TestSlicedInputStream, BigSliced)
{
  const size_t kBufSize = 4096 * 40;

  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateSeekableStreams(kBufSize, 4096 * 5, 4096 * 10, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)4096 * 10, length);

  char buf2[kBufSize / 2];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)4096 * 10, count);
  ASSERT_TRUE(
      nsCString(buf.get() + 4096 * 5, count).Equals(nsCString(buf2, count)));
}

// Big inputStream - non seekable
TEST(TestSlicedInputStream, BigSlicedNoSeek)
{
  const size_t kBufSize = 4096 * 40;

  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateNonSeekableStreams(kBufSize, 4096 * 5, 4096 * 10, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)4096 * 10, length);

  char buf2[kBufSize / 2];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)4096 * 10, count);
  ASSERT_TRUE(
      nsCString(buf.get() + 4096 * 5, count).Equals(nsCString(buf2, count)));
}

// Available size.
TEST(TestSlicedInputStream, Available)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateNonSeekableStreams(500000, 4, 400000, buf);

  uint64_t toRead = 400000;
  for (uint32_t i = 0; i < 400; ++i) {
    uint64_t length;
    ASSERT_EQ(NS_OK, sis->Available(&length));
    ASSERT_EQ(toRead, length);

    char buf2[1000];
    uint32_t count;
    ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
    ASSERT_EQ((uint64_t)1000, count);
    ASSERT_TRUE(nsCString(buf.get() + 4 + (1000 * i), count)
                    .Equals(nsCString(buf2, count)));

    toRead -= count;
  }

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)0, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)0, count);
}

// What if start is > then the size of the buffer?
TEST(TestSlicedInputStream, StartBiggerThan)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis = CreateNonSeekableStreams(500, 4000, 1, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)0, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)0, count);
}

// What if the length is > than the size of the buffer?
TEST(TestSlicedInputStream, LengthBiggerThan)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis = CreateNonSeekableStreams(500, 0, 500000, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)500, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)500, count);
}

// Like LengthBiggerThan, but for an overflowing aStart + aLength pair.
TEST(TestSlicedInputStream, LengthMuchBiggerThan)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateNonSeekableStreams(500, 100, UINT64_MAX - 1, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)500 - 100, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)(500 - 100), count);
  ASSERT_EQ(Substring(buf, 100, count), Substring(buf2, count));
}

// Like LengthMuchBiggerThan, but with a massive aStart value.
TEST(TestSlicedInputStream, StartMuchBiggerThan)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis =
      CreateNonSeekableStreams(500, UINT64_MAX - 1, 100, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)0, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)0, count);
}

// What if the length is 0?
TEST(TestSlicedInputStream, Length0)
{
  nsCString buf;
  RefPtr<SlicedInputStream> sis = CreateNonSeekableStreams(500, 0, 0, buf);

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)0, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)0, count);
}

// Seek test NS_SEEK_SET
TEST(TestSlicedInputStream, Seek_SET)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  RefPtr<SlicedInputStream> sis;
  {
    nsCOMPtr<nsIInputStream> stream;
    NS_NewCStringInputStream(getter_AddRefs(stream), buf);
    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 1, buf.Length());
  }

  ASSERT_EQ(NS_OK, sis->Seek(nsISeekableStream::NS_SEEK_SET, 1));

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)buf.Length() - 2, length);

  char buf2[4096];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)buf.Length() - 2, count);
  ASSERT_EQ(0, strncmp(buf2, "llo world", count));
}

// Seek test NS_SEEK_CUR
TEST(TestSlicedInputStream, Seek_CUR)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  RefPtr<SlicedInputStream> sis;
  {
    nsCOMPtr<nsIInputStream> stream;
    NS_NewCStringInputStream(getter_AddRefs(stream), buf);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 1, buf.Length());
  }

  ASSERT_EQ(NS_OK, sis->Seek(nsISeekableStream::NS_SEEK_CUR, 1));

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)buf.Length() - 2, length);

  char buf2[3];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)3, count);
  ASSERT_EQ(0, strncmp(buf2, "llo", count));

  ASSERT_EQ(NS_OK, sis->Seek(nsISeekableStream::NS_SEEK_CUR, 1));

  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)3, count);
  ASSERT_EQ(0, strncmp(buf2, "wor", count));
}

// Seek test NS_SEEK_END - length > real one
TEST(TestSlicedInputStream, Seek_END_Bigger)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  RefPtr<SlicedInputStream> sis;
  {
    nsCOMPtr<nsIInputStream> stream;
    NS_NewCStringInputStream(getter_AddRefs(stream), buf);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 2, buf.Length());
  }

  ASSERT_EQ(NS_OK, sis->Seek(nsISeekableStream::NS_SEEK_END, -5));

  nsCOMPtr<nsIInputStream> stream;
  NS_NewCStringInputStream(getter_AddRefs(stream), buf);
  nsCOMPtr<nsISeekableStream> seekStream = do_QueryInterface(stream);
  ASSERT_EQ(NS_OK, seekStream->Seek(nsISeekableStream::NS_SEEK_END, -5));

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)5, length);

  ASSERT_EQ(NS_OK, stream->Available(&length));
  ASSERT_EQ((uint64_t)5, length);

  char buf2[5];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)5, count);
  ASSERT_EQ(0, strncmp(buf2, "world", count));

  ASSERT_EQ(NS_OK, stream->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)5, count);
  ASSERT_EQ(0, strncmp(buf2, "world", count));
}

// Seek test NS_SEEK_END - length < real one
TEST(TestSlicedInputStream, Seek_END_Lower)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  RefPtr<SlicedInputStream> sis;
  {
    nsCOMPtr<nsIInputStream> stream;
    NS_NewCStringInputStream(getter_AddRefs(stream), buf);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 2, 6);
  }

  ASSERT_EQ(NS_OK, sis->Seek(nsISeekableStream::NS_SEEK_END, -3));

  uint64_t length;
  ASSERT_EQ(NS_OK, sis->Available(&length));
  ASSERT_EQ((uint64_t)3, length);

  char buf2[5];
  uint32_t count;
  ASSERT_EQ(NS_OK, sis->Read(buf2, sizeof(buf2), &count));
  ASSERT_EQ((uint64_t)3, count);
  ASSERT_EQ(0, strncmp(buf2, " wo", count));
}

// Check the nsIAsyncInputStream interface
TEST(TestSlicedInputStream, NoAsyncInputStream)
{
  const size_t kBufSize = 4096;

  nsCString buf;
  nsCOMPtr<nsIInputStream> sis =
      CreateSeekableStreams(kBufSize, 0, kBufSize, buf);

  // If the stream is not asyncInputStream, also SIS is not.
  nsCOMPtr<nsIAsyncInputStream> async = do_QueryInterface(sis);
  ASSERT_TRUE(!async);
}

TEST(TestSlicedInputStream, AsyncInputStream)
{
  nsCOMPtr<nsIAsyncInputStream> reader;
  nsCOMPtr<nsIAsyncOutputStream> writer;

  const uint32_t segmentSize = 1024;
  const uint32_t numSegments = 1;

  NS_NewPipe2(getter_AddRefs(reader), getter_AddRefs(writer), true,
              true,  // non-blocking - reader, writer
              segmentSize, numSegments);

  nsTArray<char> inputData;
  testing::CreateData(segmentSize, inputData);

  // We have to wrap the reader because it implements only a partial
  // nsISeekableStream interface. When ::Seek() is called, it does a MOZ_CRASH.
  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr wrapper = MakeRefPtr<NonSeekableStringStream>(reader);

    sis = MakeRefPtr<SlicedInputStream>(wrapper.forget(), 500, 500);
  }

  nsCOMPtr<nsIAsyncInputStream> async = do_QueryInterface(sis);
  ASSERT_TRUE(!!async);

  RefPtr cb = MakeRefPtr<testing::InputStreamCallback>();

  nsresult rv = async->AsyncWait(cb, 0, 0, nullptr);
  ASSERT_NS_SUCCEEDED(rv);

  ASSERT_FALSE(cb->Called());

  uint32_t numWritten = 0;
  rv = writer->Write(inputData.Elements(), inputData.Length(), &numWritten);
  ASSERT_NS_SUCCEEDED(rv);

  ASSERT_TRUE(cb->Called());

  inputData.RemoveElementsAt(0, 500);
  inputData.RemoveElementsAt(500, 24);

  testing::ConsumeAndValidateStream(async, inputData);
}

TEST(TestSlicedInputStream, QIInputStreamLength)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  for (int i = 0; i < 4; i++) {
    nsCOMPtr<nsIInputStream> sis;
    {
      RefPtr stream = MakeRefPtr<testing::LengthInputStream>(buf, i % 2, i > 1);

      sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
    }

    {
      nsCOMPtr<nsIInputStreamLength> qi = do_QueryInterface(sis);
      ASSERT_EQ(!!(i % 2), !!qi);
    }

    {
      nsCOMPtr<nsIAsyncInputStreamLength> qi = do_QueryInterface(sis);
      ASSERT_EQ(i > 1, !!qi);
    }
  }
}

TEST(TestSlicedInputStream, InputStreamLength)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr stream = MakeRefPtr<testing::LengthInputStream>(buf, true, false);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
  }

  nsCOMPtr<nsIInputStreamLength> qi = do_QueryInterface(sis);
  ASSERT_TRUE(!!qi);

  int64_t size;
  nsresult rv = qi->Length(&size);
  ASSERT_EQ(NS_OK, rv);
  ASSERT_EQ(5, size);
}

TEST(TestSlicedInputStream, NegativeInputStreamLength)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr stream =
        MakeRefPtr<testing::LengthInputStream>(buf, true, false, NS_OK, true);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
  }

  nsCOMPtr<nsIInputStreamLength> qi = do_QueryInterface(sis);
  ASSERT_TRUE(!!qi);

  int64_t size;
  nsresult rv = qi->Length(&size);
  ASSERT_EQ(NS_OK, rv);
  ASSERT_EQ(-1, size);
}

TEST(TestSlicedInputStream, AsyncInputStreamLength)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr stream = MakeRefPtr<testing::LengthInputStream>(buf, false, true);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
  }

  nsCOMPtr<nsIAsyncInputStreamLength> qi = do_QueryInterface(sis);
  ASSERT_TRUE(!!qi);

  RefPtr callback = MakeRefPtr<testing::LengthCallback>();

  nsresult rv = qi->AsyncLengthWait(callback, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "xpcom:TEST(TestSlicedInputStream, AsyncInputStreamLength)"_ns,
      [&]() { return callback->Called(); }));
  ASSERT_EQ(5, callback->Size());
}

TEST(TestSlicedInputStream, NegativeAsyncInputStreamLength)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr stream =
        MakeRefPtr<testing::LengthInputStream>(buf, false, true, NS_OK, true);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
  }

  nsCOMPtr<nsIAsyncInputStreamLength> qi = do_QueryInterface(sis);
  ASSERT_TRUE(!!qi);

  RefPtr callback = MakeRefPtr<testing::LengthCallback>();

  nsresult rv = qi->AsyncLengthWait(callback, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "xpcom:TEST(TestSlicedInputStream, NegativeAsyncInputStreamLength)"_ns,
      [&]() { return callback->Called(); }));
  ASSERT_EQ(-1, callback->Size());
}

TEST(TestSlicedInputStream, AbortLengthCallback)
{
  nsCString buf;
  buf.AssignLiteral("Hello world");

  nsCOMPtr<nsIInputStream> sis;
  {
    RefPtr stream =
        MakeRefPtr<testing::LengthInputStream>(buf, false, true, NS_OK, true);

    sis = MakeRefPtr<SlicedInputStream>(stream.forget(), 0, 5);
  }

  nsCOMPtr<nsIAsyncInputStreamLength> qi = do_QueryInterface(sis);
  ASSERT_TRUE(!!qi);

  RefPtr callback1 = MakeRefPtr<testing::LengthCallback>();
  nsresult rv = qi->AsyncLengthWait(callback1, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  RefPtr callback2 = MakeRefPtr<testing::LengthCallback>();
  rv = qi->AsyncLengthWait(callback2, GetCurrentSerialEventTarget());
  ASSERT_EQ(NS_OK, rv);

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "xpcom:TEST(TestSlicedInputStream, AbortLengthCallback)"_ns,
      [&]() { return callback2->Called(); }));
  ASSERT_TRUE(!callback1->Called());
  ASSERT_EQ(-1, callback2->Size());
}
