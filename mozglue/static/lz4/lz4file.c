/*
 * LZ4 file library
 * Copyright (c) Yann Collet and LZ4 contributors. All rights reserved.
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following disclaimer
 *   in the documentation and/or other materials provided with the
 *   distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - LZ4 homepage : http://www.lz4.org
 * - LZ4 source repository : https://github.com/lz4/lz4
 */
#include <string.h>
#include <assert.h>
#include "lz4.h"
#include "lz4file.h"

/* =====   Memory routines   ===== */
/* custom functions if provided (LZ4F_readOpen_advanced(), LZ4F_writeOpen_advanced()),
 * otherwise malloc() and free(), or LZ4_malloc() and LZ4_free()
 * when lz4 is compiled with LZ4_USER_MEMORY_FUNCTIONS (see lz4.c) */
#if defined(LZ4_USER_MEMORY_FUNCTIONS)
void* LZ4_malloc(size_t s);
void  LZ4_free(void* p);
#  define LZ4FILE_MALLOC(s)    LZ4_malloc(s)
#  define LZ4FILE_FREE(p)      LZ4_free(p)
#else
#  include <stdlib.h>   /* malloc, free */
#  define LZ4FILE_MALLOC(s)    malloc(s)
#  define LZ4FILE_FREE(p)      free(p)
#endif

static void* fileMalloc(size_t s, LZ4F_CustomMem cmem)
{
  if (cmem.customAlloc != NULL) return cmem.customAlloc(cmem.opaqueState, s);
  return LZ4FILE_MALLOC(s);
}

static void fileFree(void* p, LZ4F_CustomMem cmem)
{
  if (p == NULL) return;
  if (cmem.customFree != NULL) cmem.customFree(cmem.opaqueState, p);
  else LZ4FILE_FREE(p);
}

/* =====   Error Handling   ===== */
static LZ4F_errorCode_t returnErrorCode(LZ4F_errorCodes code)
{
    return (LZ4F_errorCode_t)-(ptrdiff_t)code;
}

#undef RETURN_ERROR
#define RETURN_ERROR(e) return returnErrorCode(LZ4F_ERROR_ ## e)

/* =====    Read API Implementation    ===== */

/* Resume points : positions where decoding can restart, recorded while reading :
 * frame starts, and block starts within frames using independent blocks.
 * They are spaced by at least LZ4F_RESUME_SPAN bytes of decoded data, to bound memory usage.
 * The start of file is an implicit resume point. */
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
#  define LZ4F_RESUME_SPAN 64           /* resume points within small fuzzing inputs */
#else
#  define LZ4F_RESUME_SPAN (1 << 20)
#endif
#define LZ4F_SKIP_BUFFER_SIZE (64 << 10)

typedef struct {
  unsigned long long cPos;       /* position within compressed data : start of a frame, or of a block */
  unsigned long long dPos;       /* matching position within decoded data */
  unsigned long long frameDPos;  /* block start : position of its frame within decoded data */
  LZ4F_frameInfo_t frameInfo;    /* block start : parameters of its frame */
  int isBlockStart;
} LZ4F_resumePoint;

struct LZ4_readFile_s {
  LZ4F_dctx* dctxPtr;
  FILE* fp;
  LZ4_byte* srcBuf;
  size_t srcBufNext;
  size_t srcBufSize;
  size_t srcBufMaxSize;
  unsigned long long srcBufPos;   /* position of srcBuf within compressed data */
  unsigned long long decodedPos;  /* reading position within decoded data */
  unsigned long long frameDPos;   /* position of current frame within decoded data */
  size_t hint;                    /* input expected by the decoder, up to the end of next block header : 0 between frames */
  LZ4F_errorCode_t error;         /* decoding error : the decoder can't continue, until restartFrom() */
  int seekable;
  LZ4F_resumePoint* points;
  size_t nbPoints;
  size_t pointsCapacity;
  LZ4_byte* skipBuf;              /* receives data skipped by LZ4F_seek() */
  LZ4F_CustomMem cmem;
};

static void freeReadFileResources(LZ4_readFile_t* lz4fRead)
{
  if (lz4fRead==NULL) return;
  LZ4F_freeDecompressionContext(lz4fRead->dctxPtr);
  fileFree(lz4fRead->srcBuf, lz4fRead->cmem);
  fileFree(lz4fRead->points, lz4fRead->cmem);
  fileFree(lz4fRead->skipBuf, lz4fRead->cmem);
  fileFree(lz4fRead, lz4fRead->cmem);
}

static LZ4F_errorCode_t readAndParseHeader(LZ4_readFile_t* readFile, FILE* fp)
{
    char headerBuf[LZ4F_HEADER_SIZE_MAX];
    LZ4F_frameInfo_t frameInfo;
    size_t consumedSize;

    /* Read the header from file */
    const size_t bytesRead = fread(headerBuf, 1, sizeof(headerBuf), fp);
    if (bytesRead < LZ4F_HEADER_SIZE_MIN + LZ4F_ENDMARK_SIZE) {
        RETURN_ERROR(io_read);
    }

    /* Parse frame information */
    consumedSize = bytesRead;
    { const size_t result = LZ4F_getFrameInfo(readFile->dctxPtr, &frameInfo, headerBuf, &consumedSize);
      if (LZ4F_isError(result)) {
          return result;
      }
      readFile->hint = result;
    }

    /* Determine buffer size based on block size */
    { const size_t blockSize = LZ4F_getBlockSize(frameInfo.blockSizeID);
      if (LZ4F_isError(blockSize)) {
          RETURN_ERROR(maxBlockSize_invalid);
      }
      readFile->srcBufMaxSize = blockSize;
    }

    /* Allocate source buffer */
    assert(readFile->srcBuf == NULL); /* Should be NULL from creation */
    readFile->srcBuf = (LZ4_byte*)fileMalloc(readFile->srcBufMaxSize, readFile->cmem);
    if (readFile->srcBuf == NULL) {
        RETURN_ERROR(allocation_failed);
    }

    /* Store remaining header data in buffer */
    readFile->srcBufPos = consumedSize;
    readFile->srcBufSize = bytesRead - consumedSize;
    if (readFile->srcBufSize > 0) {
        memcpy(readFile->srcBuf, headerBuf + consumedSize, readFile->srcBufSize);
    }
    readFile->srcBufNext = 0;

    return LZ4F_OK_NoError;
}

LZ4F_errorCode_t LZ4F_readOpen_advanced(LZ4_readFile_t** lz4fRead, FILE* fp, LZ4F_CustomMem customMem)
{
    LZ4_readFile_t* readFile;

    /* Validate parameters */
    if (lz4fRead == NULL) {
        RETURN_ERROR(parameter_null);
    }
    *lz4fRead = NULL;   /* in case of failure */
    if (fp == NULL) {
        RETURN_ERROR(parameter_null);
    }

    /* Allocate read file structure */
    readFile = (LZ4_readFile_t*)fileMalloc(sizeof(LZ4_readFile_t), customMem);
    if (readFile == NULL) {
        RETURN_ERROR(allocation_failed);
    }
    memset(readFile, 0, sizeof(*readFile));
    readFile->cmem = customMem;

    readFile->fp = fp;
    readFile->seekable = (ftell(fp) != -1L);   /* no position on pipes */

    /* Initialize decompression context */
    readFile->dctxPtr = LZ4F_createDecompressionContext_advanced(customMem, LZ4F_VERSION);
    if (readFile->dctxPtr == NULL) {
        freeReadFileResources(readFile);
        RETURN_ERROR(allocation_failed);
    }

    /* Read and parse the header */
    { LZ4F_errorCode_t const result = readAndParseHeader(readFile, fp);
      if (LZ4F_isError(result)) {
          freeReadFileResources(readFile);
          return result;
    } }

    *lz4fRead = readFile;
    return LZ4F_OK_NoError;
}

LZ4F_errorCode_t LZ4F_readOpen(LZ4_readFile_t** lz4fRead, FILE* fp)
{
    return LZ4F_readOpen_advanced(lz4fRead, fp, LZ4F_defaultCMem);
}

/* records a resume point at current decoded position, if far enough from the previous one.
 * Points are appended in increasing order : decoding again a part already indexed records nothing.
 * The index is optional : on allocation failure, the point is just not recorded.
 * @frameInfo : NULL for a frame start */
static void addResumePoint(LZ4_readFile_t* lz4fRead, unsigned long long cPos, const LZ4F_frameInfo_t* frameInfo)
{
  unsigned long long const lastDPos = lz4fRead->nbPoints ? lz4fRead->points[lz4fRead->nbPoints-1].dPos : 0;
  LZ4F_resumePoint* p;
  if (lz4fRead->decodedPos < lastDPos + LZ4F_RESUME_SPAN) return;
  if (lz4fRead->nbPoints == lz4fRead->pointsCapacity) {
    size_t const newCapacity = lz4fRead->pointsCapacity ? 2 * lz4fRead->pointsCapacity : 64;
    LZ4F_resumePoint* const newPoints = (LZ4F_resumePoint*)fileMalloc(newCapacity * sizeof(*newPoints), lz4fRead->cmem);   /* no realloc() among user functions */
    if (newPoints == NULL) return;
    if (lz4fRead->nbPoints) memcpy(newPoints, lz4fRead->points, lz4fRead->nbPoints * sizeof(*newPoints));
    fileFree(lz4fRead->points, lz4fRead->cmem);
    lz4fRead->points = newPoints;
    lz4fRead->pointsCapacity = newCapacity;
  }
  p = lz4fRead->points + lz4fRead->nbPoints++;
  memset(p, 0, sizeof(*p));
  p->cPos = cPos;
  p->dPos = lz4fRead->decodedPos;
  p->frameDPos = lz4fRead->frameDPos;
  p->isBlockStart = (frameInfo != NULL);
  if (frameInfo != NULL) p->frameInfo = *frameInfo;
}

/* the current frame isn't finished : input can't end here */
static int frameInProgress(const LZ4_readFile_t* lz4fRead)
{
  return lz4fRead->hint != 0;
}

/* input provided at once to the decoder : up to the end of next block header,
 * or the minimal header of next frame */
static size_t inputLimit(const LZ4_readFile_t* lz4fRead)
{
  return frameInProgress(lz4fRead) ? lz4fRead->hint : LZ4F_HEADER_SIZE_MIN;
}

/* updates the reading state after LZ4F_decompress() consumed @consumed bytes out of @provided,
 * and records resume points */
static void trackPosition(LZ4_readFile_t* lz4fRead, size_t decStatus, size_t provided, size_t consumed)
{
  unsigned long long const cPos = lz4fRead->srcBufPos + lz4fRead->srcBufNext;
  if (decStatus == 0) {   /* end of frame : next frame starts here */
    lz4fRead->hint = 0;
    lz4fRead->frameDPos = lz4fRead->decodedPos;
    addResumePoint(lz4fRead, cPos, NULL);
    return;
  }
  /* exactly the hinted input was consumed, and next hint covers a block :
   * decoding stopped right after a block header */
  if (consumed == provided && provided == inputLimit(lz4fRead) && decStatus > LZ4F_BLOCK_HEADER_SIZE) {
    LZ4F_frameInfo_t frameInfo;
    size_t noInput = 0;
    /* fails within a frame header, which isn't followed by a block header yet */
    if (!LZ4F_isError(LZ4F_getFrameInfo(lz4fRead->dctxPtr, &frameInfo, NULL, &noInput))
      && frameInfo.frameType == LZ4F_frame
      && frameInfo.blockMode == LZ4F_blockIndependent)
      addResumePoint(lz4fRead, cPos - LZ4F_BLOCK_HEADER_SIZE, &frameInfo);
  }
  lz4fRead->hint = decStatus;
}

size_t LZ4F_read(LZ4_readFile_t* lz4fRead, void* buf, size_t size)
{
  LZ4_byte* outPtr = (LZ4_byte*)buf;
  size_t totalBytesRead = 0;

  if (lz4fRead == NULL || buf == NULL)
    RETURN_ERROR(parameter_null);
  if (LZ4F_isError(lz4fRead->error))
    return lz4fRead->error;

  while (totalBytesRead < size) {
    size_t srcBytes = lz4fRead->srcBufSize - lz4fRead->srcBufNext;
    size_t dstBytes = size - totalBytesRead;
    size_t provided;

    if (srcBytes == 0) {
      size_t bytesRead;
      lz4fRead->srcBufPos += lz4fRead->srcBufSize;
      lz4fRead->srcBufSize = 0;
      lz4fRead->srcBufNext = 0;
      bytesRead = fread(lz4fRead->srcBuf, 1, lz4fRead->srcBufMaxSize, lz4fRead->fp);
      if (bytesRead == 0) {
        if (ferror(lz4fRead->fp) || frameInProgress(lz4fRead)) {
          RETURN_ERROR(io_read);   /* read error, or input ends within a frame */
        }
        break; /* end of input reached */
      }
      /* success: ret > 0*/
      lz4fRead->srcBufSize = bytesRead;
      srcBytes = lz4fRead->srcBufSize;
    }

    /* provide at most the hinted amount : decoding then stops right after each block header */
    if (srcBytes > inputLimit(lz4fRead)) srcBytes = inputLimit(lz4fRead);
    provided = srcBytes;

    { size_t const decStatus = LZ4F_decompress(
                          lz4fRead->dctxPtr,
                          outPtr, &dstBytes,
                          lz4fRead->srcBuf + lz4fRead->srcBufNext,
                          &srcBytes,
                          NULL);
      if (LZ4F_isError(decStatus)) {
          lz4fRead->error = decStatus;   /* the dctx can't be used again before a reset */
          return decStatus;
      }
      lz4fRead->srcBufNext += srcBytes;
      lz4fRead->decodedPos += dstBytes;
      totalBytesRead += dstBytes;
      outPtr += dstBytes;
      trackPosition(lz4fRead, decStatus, provided, srcBytes);
    }
  }

  return totalBytesRead;
}

/* moves @fp by @delta bytes : fseek() offsets are long, which may be 32-bit */
static int fseekRelative(FILE* fp, long long delta)
{
  long const stepMax = 1L << 30;
  while (delta != 0) {
    long const step = (delta > stepMax) ? stepMax : (delta < -stepMax) ? -stepMax : (long)delta;
    if (fseek(fp, step, SEEK_CUR) != 0) return -1;
    delta -= step;
  }
  return 0;
}

/* @return : last resume point at or before @pos within decoded data, or NULL for start of file */
static const LZ4F_resumePoint* findResumePoint(const LZ4_readFile_t* lz4fRead, unsigned long long pos)
{
  size_t lo = 0, hi = lz4fRead->nbPoints;
  while (lo < hi) {   /* first point beyond @pos is within [lo, hi] */
    size_t const mid = lo + (hi - lo) / 2;
    if (lz4fRead->points[mid].dPos <= pos) lo = mid + 1; else hi = mid;
  }
  return lo ? lz4fRead->points + lo - 1 : NULL;
}

/* restarts decoding from resume point @p (NULL : start of file) */
static LZ4F_errorCode_t restartFrom(LZ4_readFile_t* lz4fRead, const LZ4F_resumePoint* p)
{
  unsigned long long const cPos = p ? p->cPos : 0;
  unsigned long long const filePos = lz4fRead->srcBufPos + lz4fRead->srcBufSize;
  if (!lz4fRead->seekable || fseekRelative(lz4fRead->fp, (long long)cPos - (long long)filePos))
    RETURN_ERROR(io_read);
  lz4fRead->srcBufPos = cPos;
  lz4fRead->srcBufSize = 0;
  lz4fRead->srcBufNext = 0;
  lz4fRead->decodedPos = p ? p->dPos : 0;
  if (p && p->isBlockStart) {
    size_t const result = LZ4F_resumeFrame(lz4fRead->dctxPtr, &p->frameInfo, p->dPos - p->frameDPos, NULL, 0);
    if (LZ4F_isError(result)) return result;
    lz4fRead->hint = LZ4F_BLOCK_HEADER_SIZE;
    lz4fRead->frameDPos = p->frameDPos;
  } else {
    LZ4F_resetDecompressionContext(lz4fRead->dctxPtr);
    lz4fRead->hint = 0;
    lz4fRead->frameDPos = lz4fRead->decodedPos;
  }
  lz4fRead->error = LZ4F_OK_NoError;
  return LZ4F_OK_NoError;
}

LZ4F_errorCode_t LZ4F_seek(LZ4_readFile_t* lz4fRead, long long offset, int origin)
{
  unsigned long long target;

  if (lz4fRead == NULL)
    RETURN_ERROR(parameter_null);

  switch (origin) {
  case SEEK_SET:
    if (offset < 0) RETURN_ERROR(parameter_invalid);
    target = (unsigned long long)offset;
    break;
  case SEEK_CUR:
    if (offset < 0 && (unsigned long long)-(offset + 1) >= lz4fRead->decodedPos)
      RETURN_ERROR(parameter_invalid);   /* before start */
    target = lz4fRead->decodedPos + (unsigned long long)offset;
    break;
  default:   /* SEEK_END is not supported */
    RETURN_ERROR(parameter_invalid);
  }

  /* restart from closest resume point : required backward, and avoids decoding forward */
  { const LZ4F_resumePoint* const p = findResumePoint(lz4fRead, target);
    if (target < lz4fRead->decodedPos) {
      LZ4F_errorCode_t const result = restartFrom(lz4fRead, p);
      if (LZ4F_isError(result)) return result;
    } else if (p != NULL && p->dPos > lz4fRead->decodedPos && lz4fRead->seekable) {
      LZ4F_errorCode_t const result = restartFrom(lz4fRead, p);
      if (LZ4F_isError(result)) return result;
  } }

  /* decode and discard up to target */
  if (lz4fRead->decodedPos < target && lz4fRead->skipBuf == NULL) {
    lz4fRead->skipBuf = (LZ4_byte*)fileMalloc(LZ4F_SKIP_BUFFER_SIZE, lz4fRead->cmem);
    if (lz4fRead->skipBuf == NULL) RETURN_ERROR(allocation_failed);
  }
  while (lz4fRead->decodedPos < target) {
    unsigned long long const remaining = target - lz4fRead->decodedPos;
    size_t const toRead = (remaining < LZ4F_SKIP_BUFFER_SIZE) ? (size_t)remaining : LZ4F_SKIP_BUFFER_SIZE;
    size_t const result = LZ4F_read(lz4fRead, lz4fRead->skipBuf, toRead);
    if (LZ4F_isError(result)) return result;
    if (result == 0) RETURN_ERROR(parameter_invalid);   /* beyond end of decoded data */
  }
  return LZ4F_OK_NoError;
}

unsigned long long LZ4F_tell(const LZ4_readFile_t* lz4fRead)
{
  return lz4fRead ? lz4fRead->decodedPos : 0;
}

LZ4F_errorCode_t LZ4F_readClose(LZ4_readFile_t* lz4fRead)
{
  if (lz4fRead == NULL)
    RETURN_ERROR(parameter_null);
  freeReadFileResources(lz4fRead);
  return LZ4F_OK_NoError;
}

/* =====   write API   ===== */

struct LZ4_writeFile_s {
  LZ4F_cctx* cctxPtr;
  FILE* fp;
  LZ4_byte* dstBuf;
  size_t maxWriteSize;
  size_t dstBufMaxSize;
  LZ4F_errorCode_t errCode;
  LZ4F_CustomMem cmem;
};

static void freeWriteFileResources(LZ4_writeFile_t* state)
{
  if (state == NULL) return;
  LZ4F_freeCompressionContext(state->cctxPtr);
  fileFree(state->dstBuf, state->cmem);
  fileFree(state, state->cmem);
}

static LZ4F_errorCode_t writeHeader(LZ4_writeFile_t* writeFile,
                  FILE* fp,
            const LZ4F_preferences_t* prefsPtr)
{
  LZ4_byte headerBuf[LZ4F_HEADER_SIZE_MAX];

  /* Generate header */
  LZ4F_errorCode_t const headerSize = LZ4F_compressBegin(writeFile->cctxPtr,
                                  headerBuf, LZ4F_HEADER_SIZE_MAX, prefsPtr);
  if (LZ4F_isError(headerSize)) {
    return headerSize;
  }

  /* Write header to file */
  if (headerSize != fwrite(headerBuf, 1, headerSize, fp)) {
    RETURN_ERROR(io_write);
  }

  return LZ4F_OK_NoError;
}

LZ4F_errorCode_t LZ4F_writeOpen_advanced(LZ4_writeFile_t** lz4fWrite, FILE* fp, const LZ4F_preferences_t* prefsPtr,
                                         LZ4F_CustomMem customMem)
{
  LZ4_writeFile_t* writeFile;
  size_t blockSize;

  if (lz4fWrite == NULL)
      RETURN_ERROR(parameter_null);
  *lz4fWrite = NULL;   /* in case of failure */
  if (fp == NULL)
      RETURN_ERROR(parameter_null);

  /* Validate block size */
  { LZ4F_blockSizeID_t const blockSizeID = prefsPtr ? prefsPtr->frameInfo.blockSizeID : LZ4F_default;
    blockSize = LZ4F_getBlockSize(blockSizeID);
    if (LZ4F_isError(blockSize)) {
        RETURN_ERROR(maxBlockSize_invalid);
  } }

  /* Allocate write file structure */
  writeFile = (LZ4_writeFile_t*)fileMalloc(sizeof(LZ4_writeFile_t), customMem);
  if (writeFile == NULL) {
    RETURN_ERROR(allocation_failed);
  }
  memset(writeFile, 0, sizeof(*writeFile));
  writeFile->cmem = customMem;
  writeFile->fp = fp;
  writeFile->errCode = LZ4F_OK_NoError;
  writeFile->maxWriteSize = blockSize;

  /* Calculate and allocate destination buffer */
  writeFile->dstBufMaxSize = LZ4F_compressBound(blockSize, prefsPtr);
  writeFile->dstBuf = (LZ4_byte*)fileMalloc(writeFile->dstBufMaxSize, customMem);
  if (writeFile->dstBuf == NULL) {
    freeWriteFileResources(writeFile);
    RETURN_ERROR(allocation_failed);
  }

  /* Initialize compression context */
  writeFile->cctxPtr = LZ4F_createCompressionContext_advanced(customMem, LZ4F_VERSION);
  if (writeFile->cctxPtr == NULL) {
    freeWriteFileResources(writeFile);
    RETURN_ERROR(allocation_failed);
  }

    /* Write header to file */
  { LZ4F_errorCode_t const writeStatus = writeHeader(writeFile, fp, prefsPtr);
    if (LZ4F_isError(writeStatus)) {
        freeWriteFileResources(writeFile);
        return writeStatus;
  } }

  *lz4fWrite = writeFile;
  return LZ4F_OK_NoError;
}

LZ4F_errorCode_t LZ4F_writeOpen(LZ4_writeFile_t** lz4fWrite, FILE* fp, const LZ4F_preferences_t* prefsPtr)
{
  return LZ4F_writeOpen_advanced(lz4fWrite, fp, prefsPtr, LZ4F_defaultCMem);
}

size_t LZ4F_write(LZ4_writeFile_t* lz4fWrite, const void* buf, size_t size)
{
  const LZ4_byte* p = (const LZ4_byte*)buf;
  size_t remainingBytes = size;

  /* Validate parameters */
  if (lz4fWrite == NULL || buf == NULL)
    RETURN_ERROR(parameter_null);

  while (remainingBytes) {
    size_t const chunkSize = (remainingBytes > lz4fWrite->maxWriteSize) ? lz4fWrite->maxWriteSize : remainingBytes;

    /* Compress and write chunk */
    size_t cSize = LZ4F_compressUpdate(lz4fWrite->cctxPtr,
                              lz4fWrite->dstBuf, lz4fWrite->dstBufMaxSize,
                              p, chunkSize,
                              NULL);
    if (LZ4F_isError(cSize)) {
      lz4fWrite->errCode = cSize;
      return cSize;
    }

    if (cSize != 0 && cSize != fwrite(lz4fWrite->dstBuf, 1, cSize, lz4fWrite->fp)) {
      lz4fWrite->errCode = returnErrorCode(LZ4F_ERROR_io_write);
      RETURN_ERROR(io_write);
    }

    /* Update positions */
    p += chunkSize;
    remainingBytes -= chunkSize;
  }

  return size;
}

LZ4F_errorCode_t LZ4F_writeClose(LZ4_writeFile_t* lz4fWrite)
{
  LZ4F_errorCode_t ret;

  if (lz4fWrite == NULL) {
    RETURN_ERROR(parameter_null);
  }

  ret = lz4fWrite->errCode;   /* a previous LZ4F_write() failure leaves the frame incomplete */
  if (ret == LZ4F_OK_NoError) {
    size_t endSize;
    do {   /* dstBuf >= LZ4F_compressBound(0) : a single invocation completes the frame */
      endSize = LZ4F_compressEnd(lz4fWrite->cctxPtr,
                                 lz4fWrite->dstBuf, lz4fWrite->dstBufMaxSize,
                                 NULL);
      if (LZ4F_isError(endSize)) {
        ret = endSize;
        break;
      }
      if (endSize != fwrite(lz4fWrite->dstBuf, 1, endSize, lz4fWrite->fp)) {
        ret = returnErrorCode(LZ4F_ERROR_io_write);
        break;
      }
    } while (endSize == lz4fWrite->dstBufMaxSize);
  }

  freeWriteFileResources(lz4fWrite);
  return ret;
}
