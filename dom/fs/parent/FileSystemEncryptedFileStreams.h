/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_PARENT_FILESYSTEMENCRYPTEDFILESTREAMS_H_
#define DOM_FS_PARENT_FILESYSTEMENCRYPTEDFILESTREAMS_H_

#include "mozilla/Result.h"
#include "mozilla/dom/FileSystemCipherKeyManager.h"
#include "nsCOMPtr.h"
#include "nsError.h"

class nsIFile;
class nsIRandomAccessStream;

namespace mozilla::dom::quota {
struct OriginMetadata;
}  // namespace mozilla::dom::quota

namespace mozilla::dom::fs {

struct FileId;

/**
 * Opens aFile of a private origin as a stream of plaintext, encrypted on disk
 * with the key of aFileId.
 */
Result<nsCOMPtr<nsIRandomAccessStream>, nsresult>
CreateEncryptedFileRandomAccessStream(
    const quota::OriginMetadata& aOriginMetadata,
    FileSystemCipherKeyManager& aCipherKeyManager, const FileId& aFileId,
    nsIFile* aFile);

/**
 * Copies the plaintext of aSourceFile, encrypted with the key of aSourceFileId,
 * to aDestinationFile, re-encrypting it with the key of aDestinationFileId.
 */
nsresult CopyEncryptedFile(const quota::OriginMetadata& aOriginMetadata,
                           FileSystemCipherKeyManager& aCipherKeyManager,
                           const FileId& aSourceFileId, nsIFile* aSourceFile,
                           const FileId& aDestinationFileId,
                           nsIFile* aDestinationFile);

}  // namespace mozilla::dom::fs

#endif  // DOM_FS_PARENT_FILESYSTEMENCRYPTEDFILESTREAMS_H_
