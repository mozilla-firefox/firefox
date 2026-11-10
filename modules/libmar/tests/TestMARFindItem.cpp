/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

#include <stdlib.h>
#include <string.h>

#include "mar.h"
#include "mozilla/Assertions.h"

int main() {
  const unsigned char archive[] = {
      'M',  'A', 'R', '1',  0, 0, 0, 9,  // Offset to the index.
      0x42,                              // Contents of "a".
      0,    0,   0,   14,                // Index size.
      0,    0,   0,   8,                 // Offset to "a".
      0,    0,   0,   1,                 // Length of "a".
      0,    0,   1,   0xa4,              // File permissions (0644).
      'a',  0,
  };

  auto* mar = static_cast<MarFile*>(calloc(1, sizeof(MarFile)));
  MOZ_RELEASE_ASSERT(mar);
  mar->buffer = static_cast<unsigned char*>(malloc(sizeof(archive)));
  MOZ_RELEASE_ASSERT(mar->buffer);
  memcpy(mar->buffer, archive, sizeof(archive));
  mar->data_len = sizeof(archive);

  MOZ_RELEASE_ASSERT(!mar_find_item(mar, "missing"));

  const MarItem* item = mar_find_item(mar, "a");
  MOZ_RELEASE_ASSERT(item);
  MOZ_RELEASE_ASSERT(strcmp(item->name, "a") == 0);
  unsigned char content = 0;
  MOZ_RELEASE_ASSERT(mar_read(mar, item, 0, &content, 1) == 1);
  MOZ_RELEASE_ASSERT(content == 0x42);

  MOZ_RELEASE_ASSERT(!mar_find_item(mar, "missing"));

  mar_close(mar);
  return 0;
}
