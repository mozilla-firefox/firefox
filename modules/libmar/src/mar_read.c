/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include "city.h"
#include "mar_private.h"
#include "mar.h"
#ifdef XP_WIN
#  define strdup _strdup
#endif

/* This block must be at most 104 bytes.
   MAR channel name < 64 bytes, and product version < 32 bytes + 3 NULL
   terminator bytes. We only check for 96 though because we remove 8
   bytes above from the additionalBlockSize: We subtract
   sizeof(additionalBlockSize) and sizeof(additionalBlockID) */
#define MAXADDITIONALBLOCKSIZE 96

static uint32_t mar_hash_name(const char* name) {
  return CityHash64(name, strlen(name)) % TABLESIZE;
}

static int mar_insert_item(MarFile* mar, const char* name, uint32_t namelen,
                           uint32_t offset, uint32_t length, uint32_t flags) {
  MarItem *item, *root;
  uint32_t hash;

  item = (MarItem*)malloc(sizeof(MarItem) + namelen);
  if (!item) {
    return -1;
  }
  item->next = NULL;
  item->offset = offset;
  item->length = length;
  item->flags = flags;
  memcpy(item->name, name, namelen + 1);

  hash = mar_hash_name(name);

  root = mar->item_table[hash];
  if (!root) {
    mar->item_table[hash] = item;
  } else {
    /* append item */
    while (root->next) root = root->next;
    root->next = item;
  }
  return 0;
}

static int mar_consume_index(MarFile* mar, char** buf, const char* buf_end) {
  /*
   * Each item has the following structure:
   *   uint32_t offset      (network byte order)
   *   uint32_t length      (network byte order)
   *   uint32_t flags       (network byte order)
   *   char     name[N]     (where N >= 1)
   *   char     null_byte;
   */
  uint32_t offset;
  uint32_t length;
  uint32_t flags;
  const char* name;
  int namelen;

  if ((buf_end - *buf) < (int)(3 * sizeof(uint32_t) + 2)) {
    return -1;
  }

  memcpy(&offset, *buf, sizeof(offset));
  *buf += sizeof(offset);

  memcpy(&length, *buf, sizeof(length));
  *buf += sizeof(length);

  memcpy(&flags, *buf, sizeof(flags));
  *buf += sizeof(flags);

  offset = ntohl(offset);
  length = ntohl(length);
  flags = ntohl(flags);

  name = *buf;
  /* find namelen; must take care not to read beyond buf_end */
  while (**buf) {
    /* buf_end points one byte past the end of buf's allocation */
    if (*buf == (buf_end - 1)) {
      return -1;
    }
    ++(*buf);
  }
  namelen = (*buf - name);
  /* must ensure that namelen is valid */
  if (namelen < 0) {
    return -1;
  }
  /* consume null byte */
  if (*buf == buf_end) {
    return -1;
  }
  ++(*buf);

  return mar_insert_item(mar, name, namelen, offset, length, flags);
}

static int mar_read_index(MarFile* mar) {
  char id[MAR_ID_SIZE], *buf, *bufptr, *bufend;
  uint32_t offset_to_index, size_of_index;
  size_t mar_position = 0;

  /* verify MAR ID */
  if (mar_read_buffer(mar, id, &mar_position, MAR_ID_SIZE) != 0) {
    return -1;
  }
  if (memcmp(id, MAR_ID, MAR_ID_SIZE) != 0) {
    return -1;
  }

  if (mar_read_buffer(mar, &offset_to_index, &mar_position, sizeof(uint32_t)) !=
      0) {
    return -1;
  }
  offset_to_index = ntohl(offset_to_index);

  mar_position = 0;
  if (mar_buffer_seek(mar, &mar_position, offset_to_index) != 0) {
    return -1;
  }
  if (mar_read_buffer(mar, &size_of_index, &mar_position, sizeof(uint32_t)) !=
      0) {
    return -1;
  }
  size_of_index = ntohl(size_of_index);

  if (size_of_index > mar->data_len) {
    return -1;
  }
  buf = (char*)malloc(size_of_index);
  if (!buf) {
    return -1;
  }
  if (mar_read_buffer(mar, buf, &mar_position, size_of_index) != 0) {
    free(buf);
    return -1;
  }

  bufptr = buf;
  bufend = buf + size_of_index;
  while (bufptr < bufend && mar_consume_index(mar, &bufptr, bufend) == 0);

  free(buf);
  return (bufptr == bufend) ? 0 : -1;
}

/**
 * Adds an offset and length to the MarFile's index_list
 * @param mar     The MarFile that owns this offset length pair
 * @param offset  The byte offset in the archive to be marked as processed
 * @param length  The length corresponding to this byte offset
 * @return int    1 on success, 0 if offset has been previously processed
 *                -1 if unable to allocate space for the SeenIndexes
 */
static int mar_insert_offset(MarFile* mar, uint32_t offset, uint32_t length) {
  /* Ignore files with no length */
  if (length == 0) {
    return 1;
  }

  SeenIndex* index = (SeenIndex*)malloc(sizeof(SeenIndex));
  if (!index) {
    return -1;
  }
  index->next = NULL;
  index->offset = offset;
  index->length = length;
  uint32_t index_end = index->offset + index->length - 1;

  /* If this is our first index store it at the front */
  if (mar->index_list == NULL) {
    mar->index_list = index;
    return 1;
  }

  /* Search for matching indexes in the list of those previously visited */
  SeenIndex* previous;
  SeenIndex* current = mar->index_list;
  while (current != NULL) {
    uint32_t current_end = current->offset + current->length - 1;

    /* If index has collided with the front or end of current or if current has
       collided with the front or end of index return false */
    if ((index->offset >= current->offset && index->offset <= current_end) ||
        (index_end >= current->offset && index_end <= current_end) ||
        (current->offset >= index->offset && current->offset <= index_end) ||
        (current_end >= index->offset && current_end <= index_end)) {
      free(index);
      return 0;
    }

    /* else move to the next in the list */
    previous = current;
    current = current->next;
  }

  /* These indexes are valid, track them */
  previous->next = index;
  return 1;
}

/**
 * Internal shared code for mar_open and mar_wopen.
 * Reads the entire MAR into memory. Fails if it is bigger than
 * MAX_SIZE_OF_MAR_FILE bytes.
 */
static MarReadResult mar_fpopen(FILE* fp, MarFile** out_mar) {
  *out_mar = NULL;
  MarFile* mar;

  mar = (MarFile*)malloc(sizeof(*mar));
  if (!mar) {
    return MAR_MEM_ERROR;
  }

  off_t buffer_size = -1;
  if (fseeko(fp, 0, SEEK_END) == 0) {
    buffer_size = ftello(fp);
  }
  rewind(fp);
  if (buffer_size < 0) {
    fprintf(stderr, "Warning: MAR size could not be determined\n");
    buffer_size = MAX_SIZE_OF_MAR_FILE;
  }
  if (buffer_size > MAX_SIZE_OF_MAR_FILE) {
    fprintf(stderr, "ERROR: MAR exceeds maximum size (%lli)\n",
            (long long int)buffer_size);
    free(mar);
    return MAR_FILE_TOO_BIG_ERROR;
  }

  mar->buffer = malloc(buffer_size);
  if (!mar->buffer) {
    fprintf(stderr, "ERROR: MAR buffer could not be allocated\n");
    free(mar);
    return MAR_MEM_ERROR;
  }
  mar->data_len = fread(mar->buffer, 1, buffer_size, fp);
  if (fgetc(fp) != EOF) {
    fprintf(stderr, "ERROR: File is larger than buffer (%lli)\n",
            (long long int)buffer_size);
    free(mar->buffer);
    free(mar);
    return MAR_IO_ERROR;
  }
  if (ferror(fp)) {
    fprintf(stderr, "ERROR: Failed to read MAR\n");
    free(mar->buffer);
    free(mar);
    return MAR_IO_ERROR;
  }

  mar->item_table_is_valid = 0;
  memset(mar->item_table, 0, sizeof(mar->item_table));
  mar->index_list = NULL;

  *out_mar = mar;
  return MAR_READ_SUCCESS;
}

MarReadResult mar_open(const char* path, MarFile** out_mar) {
  *out_mar = NULL;

  FILE* fp;

  fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "ERROR: could not open file in mar_open()\n");
    perror(path);
    return MAR_IO_ERROR;
  }

  MarReadResult result = mar_fpopen(fp, out_mar);
  fclose(fp);
  return result;
}

#ifdef XP_WIN
MarReadResult mar_wopen(const wchar_t* path, MarFile** out_mar) {
  *out_mar = NULL;

  FILE* fp;

  _wfopen_s(&fp, path, L"rb");
  if (!fp) {
    fprintf(stderr, "ERROR: could not open file in mar_wopen()\n");
    _wperror(path);
    return MAR_IO_ERROR;
  }

  MarReadResult result = mar_fpopen(fp, out_mar);
  fclose(fp);
  return result;
}
#endif

void mar_close(MarFile* mar) {
  MarItem* item;
  SeenIndex* index;
  int i;

  free(mar->buffer);

  for (i = 0; i < TABLESIZE; ++i) {
    item = mar->item_table[i];
    while (item) {
      MarItem* temp = item;
      item = item->next;
      free(temp);
    }
  }

  while (mar->index_list != NULL) {
    index = mar->index_list;
    mar->index_list = index->next;
    free(index);
  }

  free(mar);
}

int mar_read_buffer(MarFile* mar, void* dest, size_t* position, size_t size) {
  // size may be provided by the MAR, which we may not have finished validating
  // the signature on yet. Make sure not to trust it in a way that could
  // cause an overflow.
  if (size > mar->data_len) {
    return -1;
  }
  if (*position > mar->data_len - size) {
    return -1;
  }
  memcpy(dest, mar->buffer + *position, size);
  *position += size;
  return 0;
}

int mar_read_buffer_max(MarFile* mar, void* dest, size_t* position,
                        size_t size) {
  // size may be provided by the MAR, which we may not have finished validating
  // the signature on yet. Make sure not to trust it in a way that could
  // cause an overflow.
  if (mar->data_len <= *position) {
    return 0;
  }
  size_t read_count = mar->data_len - *position;
  if (read_count > size) {
    read_count = size;
  }
  memcpy(dest, mar->buffer + *position, read_count);
  *position += read_count;
  return read_count;
}

int mar_buffer_seek(MarFile* mar, size_t* position, size_t distance) {
  // distance may be provided by the MAR, which we may not have finished
  // validating the signature on yet. Make sure not to trust it in a way that
  // could cause an overflow.
  if (distance > mar->data_len) {
    return -1;
  }
  if (*position > mar->data_len - distance) {
    return -1;
  }
  *position += distance;
  return 0;
}

/**
 * Determines the MAR file information.
 *
 * @param mar                    An open MAR file.
 * @param mar_position           The current position in the MAR.
 *                               Its value will be updated to the current
 *                               position in the MAR after the function exits.
 *                               Since its initial value will never actually be
 *                               used, this is effectively an outparam.
 * @param hasSignatureBlock      Optional out parameter specifying if the MAR
 *                               file has a signature block or not.
 * @param numSignatures          Optional out parameter for storing the number
 *                               of signatures in the MAR file.
 * @param hasAdditionalBlocks    Optional out parameter specifying if the MAR
 *                               file has additional blocks or not.
 * @param offsetAdditionalBlocks Optional out parameter for the offset to the
 *                               first additional block. Value is only valid if
 *                               hasAdditionalBlocks is not equal to 0.
 * @param numAdditionalBlocks    Optional out parameter for the number of
 *                               additional blocks.  Value is only valid if
 *                               hasAdditionalBlocks is not equal to 0.
 * @return 0 on success and non-zero on failure.
 */
int get_open_mar_file_info(MarFile* mar, size_t* mar_position,
                           int* hasSignatureBlock, uint32_t* numSignatures,
                           int* hasAdditionalBlocks,
                           uint32_t* offsetAdditionalBlocks,
                           uint32_t* numAdditionalBlocks) {
  uint32_t offsetToIndex, offsetToContent, signatureCount, signatureLen, i;

  /* One of hasSignatureBlock or hasAdditionalBlocks must be non NULL */
  if (!hasSignatureBlock && !hasAdditionalBlocks) {
    return -1;
  }

  /* Skip to the start of the offset index */
  *mar_position = 0;
  if (mar_buffer_seek(mar, mar_position, MAR_ID_SIZE) != 0) {
    return -1;
  }

  /* Read the offset to the index. */
  if (mar_read_buffer(mar, &offsetToIndex, mar_position,
                      sizeof(offsetToIndex)) != 0) {
    return -1;
  }
  offsetToIndex = ntohl(offsetToIndex);

  if (numSignatures) {
    /* Skip past the MAR file size field */
    if (mar_buffer_seek(mar, mar_position, sizeof(uint64_t)) != 0) {
      return -1;
    }

    /* Read the number of signatures field */
    if (mar_read_buffer(mar, numSignatures, mar_position,
                        sizeof(*numSignatures)) != 0) {
      return -1;
    }
    *numSignatures = ntohl(*numSignatures);
  }

  /* Skip to the first index entry past the index size field
     We do it in 2 calls because offsetToIndex + sizeof(uint32_t)
     could overflow in theory. */
  *mar_position = 0;
  if (mar_buffer_seek(mar, mar_position, offsetToIndex) != 0) {
    return -1;
  }

  if (mar_buffer_seek(mar, mar_position, sizeof(uint32_t)) != 0) {
    return -1;
  }

  /* Read the first offset to content field. */
  if (mar_read_buffer(mar, &offsetToContent, mar_position,
                      sizeof(offsetToContent)) != 0) {
    return -1;
  }
  offsetToContent = ntohl(offsetToContent);

  /* Check if we have a new or old MAR file */
  if (hasSignatureBlock) {
    if (offsetToContent == MAR_ID_SIZE + sizeof(uint32_t)) {
      *hasSignatureBlock = 0;
    } else {
      *hasSignatureBlock = 1;
    }
  }

  /* If the caller doesn't care about the product info block
     value, then just return */
  if (!hasAdditionalBlocks) {
    return 0;
  }

  /* Skip to the start of the signature block */
  *mar_position = 0;
  if (mar_buffer_seek(mar, mar_position, SIGNATURE_BLOCK_OFFSET) != 0) {
    return -1;
  }

  /* Get the number of signatures */
  if (mar_read_buffer(mar, &signatureCount, mar_position,
                      sizeof(signatureCount)) != 0) {
    return -1;
  }
  signatureCount = ntohl(signatureCount);

  /* Check that we have less than the max amount of signatures so we don't
     waste too much of either updater's or signmar's time. */
  if (signatureCount > MAX_SIGNATURES) {
    return -1;
  }

  /* Skip past the whole signature block */
  for (i = 0; i < signatureCount; i++) {
    /* Skip past the signature algorithm ID */
    if (mar_buffer_seek(mar, mar_position, sizeof(uint32_t)) != 0) {
      return -1;
    }

    /* Read the signature length and skip past the signature */
    if (mar_read_buffer(mar, &signatureLen, mar_position, sizeof(uint32_t)) !=
        0) {
      return -1;
    }
    signatureLen = ntohl(signatureLen);
    if (mar_buffer_seek(mar, mar_position, signatureLen) != 0) {
      return -1;
    }
  }

  if (*mar_position <= (size_t)INT64_MAX &&
      (int64_t)mar_position == (int64_t)offsetToContent) {
    *hasAdditionalBlocks = 0;
  } else {
    if (numAdditionalBlocks) {
      /* We have an additional block, so read in the number of additional blocks
         and set the offset. */
      *hasAdditionalBlocks = 1;
      if (mar_read_buffer(mar, numAdditionalBlocks, mar_position,
                          sizeof(uint32_t)) != 0) {
        return -1;
      }
      *numAdditionalBlocks = ntohl(*numAdditionalBlocks);
      if (offsetAdditionalBlocks) {
        if (*mar_position > (size_t)UINT32_MAX) {
          return -1;
        }
        *offsetAdditionalBlocks = (uint32_t)*mar_position;
      }
    } else if (offsetAdditionalBlocks) {
      /* numAdditionalBlocks is not specified but offsetAdditionalBlocks
         is, so fill it! */
      if (mar_buffer_seek(mar, mar_position, sizeof(uint32_t)) != 0) {
        return -1;
      }
      if (*mar_position > (size_t)UINT32_MAX) {
        return -1;
      }
      *offsetAdditionalBlocks = (uint32_t)*mar_position;
    }
  }

  return 0;
}

/**
 * Reads the product info block from the MAR file's additional block section.
 * The caller is responsible for freeing the fields in infoBlock
 * if the return is successful.
 *
 * @param infoBlock Out parameter for where to store the result to
 * @return 0 on success, -1 on failure
 */
int read_product_info_block(char* path,
                            struct ProductInformationBlock* infoBlock) {
  int rv;
  MarFile* mar;
  MarReadResult result = mar_open(path, &mar);
  if (result != MAR_READ_SUCCESS) {
    fprintf(stderr,
            "ERROR: could not open file in read_product_info_block()\n");
    return -1;
  }
  rv = mar_read_product_info_block(mar, infoBlock);
  mar_close(mar);
  return rv;
}

/**
 * Reads the product info block from the MAR file's additional block section.
 * The caller is responsible for freeing the fields in infoBlock
 * if the return is successful.
 *
 * @param infoBlock Out parameter for where to store the result to
 * @return 0 on success, -1 on failure
 */
int mar_read_product_info_block(MarFile* mar,
                                struct ProductInformationBlock* infoBlock) {
  uint32_t offsetAdditionalBlocks, numAdditionalBlocks, additionalBlockSize,
      additionalBlockID;
  int hasAdditionalBlocks;
  size_t mar_position = 0;

  /* The buffer size is 97 bytes because the MAR channel name < 64 bytes, and
     product version < 32 bytes + 3 NULL terminator bytes. */
  char buf[MAXADDITIONALBLOCKSIZE + 1] = {'\0'};
  if (get_open_mar_file_info(mar, &mar_position, NULL, NULL,
                             &hasAdditionalBlocks, &offsetAdditionalBlocks,
                             &numAdditionalBlocks) != 0) {
    return -1;
  }

  /* We only have the one additional block type and only one is expected to be
     in a MAR file so check if any exist and process the first found */
  if (numAdditionalBlocks > 0) {
    /* Read the additional block size */
    if (mar_read_buffer(mar, &additionalBlockSize, &mar_position,
                        sizeof(additionalBlockSize)) != 0) {
      return -1;
    }
    additionalBlockSize = ntohl(additionalBlockSize) -
                          sizeof(additionalBlockSize) -
                          sizeof(additionalBlockID);

    /* Additional Block sizes should only be 96 bytes long */
    if (additionalBlockSize > MAXADDITIONALBLOCKSIZE) {
      return -1;
    }

    /* Read the additional block ID */
    if (mar_read_buffer(mar, &additionalBlockID, &mar_position,
                        sizeof(additionalBlockID)) != 0) {
      return -1;
    }
    additionalBlockID = ntohl(additionalBlockID);

    if (PRODUCT_INFO_BLOCK_ID == additionalBlockID) {
      const char* location;
      int len;

      if (mar_read_buffer(mar, buf, &mar_position, additionalBlockSize) != 0) {
        return -1;
      }

      /* Extract the MAR channel name from the buffer.  For now we
         point to the stack allocated buffer but we strdup this
         if we are within bounds of each field's max length. */
      location = buf;
      len = strlen(location);
      infoBlock->MARChannelID = location;
      location += len + 1;
      if (len >= 64) {
        infoBlock->MARChannelID = NULL;
        return -1;
      }

      /* Extract the version from the buffer */
      len = strlen(location);
      infoBlock->productVersion = location;
      if (len >= 32) {
        infoBlock->MARChannelID = NULL;
        infoBlock->productVersion = NULL;
        return -1;
      }
      infoBlock->MARChannelID = strdup(infoBlock->MARChannelID);
      infoBlock->productVersion = strdup(infoBlock->productVersion);
      return 0;
    } else {
      /* This is not the additional block you're looking for. Move along. */
      if (mar_buffer_seek(mar, &mar_position, additionalBlockSize) != 0) {
        return -1;
      }
    }
  }

  /* If we had a product info block we would have already returned */
  return -1;
}

const MarItem* mar_find_item(MarFile* mar, const char* name) {
  uint32_t hash;
  const MarItem* item;

  if (!mar->item_table_is_valid) {
    if (mar_read_index(mar)) {
      return NULL;
    } else {
      mar->item_table_is_valid = 1;
    }
  }

  hash = mar_hash_name(name);

  item = mar->item_table[hash];
  while (item && strcmp(item->name, name) != 0) {
    item = item->next;
  }

  /* If this is the first time seeing this item's indexes, return it */
  if (mar_insert_offset(mar, item->offset, item->length) == 1) {
    return item;
  } else {
    fprintf(stderr, "ERROR: file content collision in mar_find_item()\n");
    return NULL;
  }
}

int mar_enum_items(MarFile* mar, MarItemCallback callback, void* closure) {
  MarItem* item;
  int i, rv;

  if (!mar->item_table_is_valid) {
    if (mar_read_index(mar)) {
      return -1;
    } else {
      mar->item_table_is_valid = 1;
    }
  }

  for (i = 0; i < TABLESIZE; ++i) {
    item = mar->item_table[i];
    while (item) {
      /* if this is the first time seeing this item's indexes, process it */
      if (mar_insert_offset(mar, item->offset, item->length) == 1) {
        rv = callback(mar, item, closure);
        if (rv) {
          return rv;
        }
      } else {
        fprintf(stderr, "ERROR: file content collision in mar_enum_items()\n");
        return 1;
      }
      item = item->next;
    }
  }

  return 0;
}

int mar_read(MarFile* mar, const MarItem* item, int offset, uint8_t* buf,
             int bufsize) {
  int nr;
  size_t mar_position = 0;

  if (offset == (int)item->length) {
    return 0;
  }
  if (offset > (int)item->length) {
    return -1;
  }

  nr = item->length - offset;
  if (nr > bufsize) {
    nr = bufsize;
  }

  // Avoid adding item->offset and offset directly, just in case of overflow.
  if (mar_buffer_seek(mar, &mar_position, item->offset)) {
    return -1;
  }
  if (mar_buffer_seek(mar, &mar_position, offset)) {
    return -1;
  }

  return mar_read_buffer_max(mar, buf, &mar_position, nr);
}

/**
 * Determines the MAR file information.
 *
 * @param path                   The path of the MAR file to check.
 * @param hasSignatureBlock      Optional out parameter specifying if the MAR
 *                               file has a signature block or not.
 * @param numSignatures          Optional out parameter for storing the number
 *                               of signatures in the MAR file.
 * @param hasAdditionalBlocks    Optional out parameter specifying if the MAR
 *                               file has additional blocks or not.
 * @param offsetAdditionalBlocks Optional out parameter for the offset to the
 *                               first additional block. Value is only valid if
 *                               hasAdditionalBlocks is not equal to 0.
 * @param numAdditionalBlocks    Optional out parameter for the number of
 *                               additional blocks.  Value is only valid if
 *                               has_additional_blocks is not equal to 0.
 * @return 0 on success and non-zero on failure.
 */
int get_mar_file_info(const char* path, int* hasSignatureBlock,
                      uint32_t* numSignatures, int* hasAdditionalBlocks,
                      uint32_t* offsetAdditionalBlocks,
                      uint32_t* numAdditionalBlocks) {
  int rv;
  MarFile* mar;
  size_t mar_position = 0;
  MarReadResult result = mar_open(path, &mar);
  if (result != MAR_READ_SUCCESS) {
    fprintf(stderr, "ERROR: could not read file in get_mar_file_info()\n");
    return -1;
  }

  rv = get_open_mar_file_info(mar, &mar_position, hasSignatureBlock,
                              numSignatures, hasAdditionalBlocks,
                              offsetAdditionalBlocks, numAdditionalBlocks);

  mar_close(mar);
  return rv;
}
