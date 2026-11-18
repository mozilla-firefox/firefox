/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_CONTAINERPARSER_H_
#define MOZILLA_CONTAINERPARSER_H_

#include "MediaContainerType.h"
#include "MediaResource.h"
#include "MediaResult.h"
#include "MediaSpan.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {

class MediaByteBuffer;
class SourceBufferResource;

DDLoggedTypeDeclName(ContainerParser);

class ContainerParser : public DecoderDoctorLifeLogger<ContainerParser> {
 public:
  explicit ContainerParser(const MediaContainerType& aType);
  virtual ~ContainerParser();

  // Return true if aData starts with an initialization segment.
  // The base implementation exists only for debug logging and is expected
  // to be called first from the overriding implementation.
  // Return NS_OK if segment is present, NS_ERROR_NOT_AVAILABLE if no sufficient
  // data is currently available to make a determination. Any other value
  // indicates an error.
  virtual MediaResult IsInitSegmentPresent(const MediaSpan& aData);

  // Return true if aData starts with a media segment.
  // The base implementation exists only for debug logging and is expected
  // to be called first from the overriding implementation.
  // Return NS_OK if segment is present, NS_ERROR_NOT_AVAILABLE if no sufficient
  // data is currently available to make a determination. Any other value
  // indicates an error.
  virtual MediaResult IsMediaSegmentPresent(const MediaSpan& aData);

  // Parse aData to extract the start and end frame times from the media
  // segment.  aData may not start on a parser sync boundary.  Return NS_OK
  // if aStart and aEnd have been updated and NS_ERROR_NOT_AVAILABLE otherwise
  // when no error were encountered.
  virtual MediaResult ParseStartAndEndTimestamps(const MediaSpan& aData,
                                                 media::TimeUnit& aStart,
                                                 media::TimeUnit& aEnd);

  // Compare aLhs and rHs, considering any error that may exist in the
  // timestamps from the format's base representation.  Return true if aLhs
  // == aRhs within the error epsilon.
  bool TimestampsFuzzyEqual(int64_t aLhs, int64_t aRhs);

  virtual int64_t GetRoundingError();

  // Return true if initialization segments have no bytes of their own and are
  // part of the media segment that follows them. InitData() then describes
  // the initialization segment, and InitSegmentRange() is empty.
  virtual bool HasImplicitInitSegment() const { return false; }

  // Return the number of bytes at the start of aData that the byte stream
  // format says must be ignored, for the segment parser loop to remove. It is
  // only called while waiting for a segment, so a media segment must end
  // before such bytes. The rest of a unit to ignore that extends past aData is
  // ignored as it is appended.
  virtual size_t SkipIgnorableBytesAtStart(const MediaSpan& aData) { return 0; }

  MediaByteBuffer* InitData();

  bool HasInitData() { return mHasInitData; }

  // Return true if a complete initialization segment has been passed
  // to ParseStartAndEndTimestamps(). The calls below to retrieve
  // MediaByteRanges will be valid from when this call first succeeds.
  bool HasCompleteInitData();
  // Returns the byte range of the first complete init segment, or an empty
  // range if not complete. It is always empty when HasImplicitInitSegment()
  // is true, as the init segment then has no bytes of its own. Use
  // HasCompleteInitData() and InitData() instead.
  MediaByteRange InitSegmentRange();
  // Returns the byte range of the first complete media segment header,
  // or an empty range if not complete.
  MediaByteRange MediaHeaderRange();
  // Returns the byte range of the first complete media segment or an empty
  // range if not complete.
  MediaByteRange MediaSegmentRange();

  static UniquePtr<ContainerParser> CreateForMIMEType(
      const MediaContainerType& aType);

  const MediaContainerType& ContainerType() const { return mType; }

 protected:
  RefPtr<MediaByteBuffer> mInitData;
  RefPtr<SourceBufferResource> mResource;
  bool mHasInitData;
  uint64_t mTotalParsed;
  uint64_t mGlobalOffset;
  MediaByteRange mCompleteInitSegmentRange;
  MediaByteRange mCompleteMediaHeaderRange;
  MediaByteRange mCompleteMediaSegmentRange;
  const MediaContainerType mType;
};

}  // namespace mozilla

#endif /* MOZILLA_CONTAINERPARSER_H_ */
