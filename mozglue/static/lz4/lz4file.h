/*
   LZ4 file library
   Header File
   Copyright (c) Yann Collet and LZ4 contributors. All rights reserved.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - LZ4 source repository : https://github.com/lz4/lz4
   - LZ4 public forum : https://groups.google.com/forum/#!forum/lz4c
*/
#ifndef LZ4FILE_H
#define LZ4FILE_H

#include <stdio.h>  /* FILE* */
#include "lz4frame_static.h"

#if defined (__cplusplus)
extern "C" {
#endif

typedef struct LZ4_readFile_s LZ4_readFile_t;
typedef struct LZ4_writeFile_s LZ4_writeFile_t;

/** LZ4 File Decompression **/

/**
 * Opens an LZ4 file for reading.
 * Note that the FILE* handle @p fp must be opened in binary mode.
 *
 * @param lz4fRead  Pointer to receive the read file handle.
 *                  It is an OUT parameter, so its initial value is ignored and will be overwritten.
 *                  Its value on exit is only valid if the function returns LZ4F_OK_NoError.
 *                  On failure, it is set to NULL.
 * @param fp        FILE* positioned at start of LZ4 file (binary mode).
 *
 * @return LZ4F_OK_NoError on success, or error code on failure.
 *         Can be tested with LZ4F_isError().
 *
 * @note Must be closed with LZ4F_readClose() when done.
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_readOpen(LZ4_readFile_t** lz4fRead, FILE* fp);

/*! LZ4F_readOpen_advanced() : v1.11.0+
 *  Same as LZ4F_readOpen(), with all allocations performed by @customMem :
 *  the handle, its buffers, and its decompression context, up to LZ4F_readClose().
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_readOpen_advanced(LZ4_readFile_t** lz4fRead, FILE* fp, LZ4F_CustomMem customMem);

/*! LZ4F_read() :
 * Read lz4file content to buffer.
 * `lz4f` must use LZ4_readOpen to set first.
 * `buf` read data buffer.
 * `size` read data buffer size.
 * @return : number of bytes read, 0 at end of file, or an error code (can be tested using LZ4F_isError()).
 *  A file ending within a frame (truncated) is reported as an error (ERROR_io_read).
 *  After a decoding error (corrupted data), it keeps returning that error, until a backward LZ4F_seek().
 */
LZ4FLIB_STATIC_API size_t LZ4F_read(LZ4_readFile_t* lz4fRead, void* buf, size_t size);

/*! LZ4F_seek() : v1.11.0+
 *  Moves the reading position within decoded data, like fseek().
 * @origin : SEEK_SET (from the beginning) or SEEK_CUR (from current position).
 *           SEEK_END is not supported.
 *  Seeking forward decodes and discards data up to the requested position.
 *  Seeking backward requires the FILE* to be seekable : decoding restarts from the closest preceding
 *  resume point, recorded automatically while reading : frame starts, and block starts within frames
 *  using independent blocks. Within frames using linked blocks, decoding restarts from the frame start :
 *  prefer independent blocks for files meant for random access.
 * @return : 0 on success, or an error code (can be tested using LZ4F_isError()).
 *  Seeking beyond the end of decoded data stops at the end, and returns an error.
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_seek(LZ4_readFile_t* lz4fRead, long long offset, int origin);

/*! LZ4F_tell() : v1.11.0+
 * @return : current reading position within decoded data.
 */
LZ4FLIB_STATIC_API unsigned long long LZ4F_tell(const LZ4_readFile_t* lz4fRead);

/*! LZ4F_readClose() :
 * Close lz4file handle.
 * `lz4f` must use LZ4_readOpen to set first.
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_readClose(LZ4_readFile_t* lz4fRead);

/** LZ4 File Decompression **/

/**
 * Opens an LZ4 file for writing.
 * Note that the FILE* handle @p fp must be opened in write binary mode.
 *
 * @param lz4fWrite Pointer to receive the write file handle.
 *                  It is an OUT parameter, so its initial value is ignored and will be overwritten.
 *                  Its value on exit is only valid if the function returns LZ4F_OK_NoError.
 *                  On failure, it is set to NULL.
 * @param fp        FILE* positioned at start of LZ4 file (binary mode).
 *
 * @return LZ4F_OK_NoError on success, or error code on failure.
 *         Can be tested with LZ4F_isError().
 *
 * @note Must be closed with LZ4F_writeClose() when done.
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_writeOpen(LZ4_writeFile_t** lz4fWrite, FILE* fp, const LZ4F_preferences_t* prefsPtr);

/*! LZ4F_writeOpen_advanced() : v1.11.0+
 *  Same as LZ4F_writeOpen(), with all allocations performed by @customMem :
 *  the handle, its buffer, and its compression context, up to LZ4F_writeClose().
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_writeOpen_advanced(LZ4_writeFile_t** lz4fWrite, FILE* fp, const LZ4F_preferences_t* prefsPtr,
                                                            LZ4F_CustomMem customMem);

/*! LZ4F_write() :
 * Write buffer to lz4file.
 * `lz4f` must use LZ4F_writeOpen to set first.
 * `buf` write data buffer.
 * `size` write data buffer size.
 */
LZ4FLIB_STATIC_API size_t LZ4F_write(LZ4_writeFile_t* lz4fWrite, const void* buf, size_t size);

/*! LZ4F_writeClose() :
 * Close lz4file handle.
 * `lz4f` must use LZ4F_writeOpen to set first.
 * @return LZ4F_OK_NoError on success, or error code on failure,
 *         including one from a previous LZ4F_write() : the frame is then incomplete.
 *  Resources are released in all cases.
 */
LZ4FLIB_STATIC_API LZ4F_errorCode_t LZ4F_writeClose(LZ4_writeFile_t* lz4fWrite);

#if defined (__cplusplus)
}
#endif

#endif /* LZ4FILE_H */
