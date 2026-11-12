/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemEncryptedFileStreams.h"

#include "FileSystemParentTypes.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/quota/EncryptedRandomAccessStream_impl.h"
#include "mozilla/dom/quota/FileStreams.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "nsIFileStreams.h"
#include "prio.h"

namespace mozilla::dom::fs {

using EncryptedRandomAccessStream =
    quota::EncryptedRandomAccessStream<FileSystemCipherStrategy>;

Result<nsCOMPtr<nsIRandomAccessStream>, nsresult>
CreateEncryptedFileRandomAccessStream(
    const quota::OriginMetadata& aOriginMetadata,
    FileSystemCipherKeyManager& aCipherKeyManager, const FileId& aFileId,
    nsIFile* aFile) {
  MOZ_ASSERT(aOriginMetadata.mPersistenceType ==
             quota::PERSISTENCE_TYPE_PRIVATE);

  QM_TRY_UNWRAP(MovingNotNull<nsCOMPtr<nsIRandomAccessStream>> baseStream,
                CreateFileRandomAccessStream(
                    quota::PERSISTENCE_TYPE_PRIVATE, aOriginMetadata,
                    quota::Client::FILESYSTEM, aFile, /* aIOFlags */ -1,
                    /* aPerm */ -1, nsIFileRandomAccessStream::DEFER_OPEN));

  QM_TRY(MOZ_TO_RESULT(FileSystemCipherStrategy::Init()));

  // The key is keyed by the file, because the cipher strategy derives its block
  // keys only from the master key and the block number, and the AAD identifies
  // only the block. With one master key per origin, the same-numbered blocks of
  // its files would share a derived key. So a block could be substituted
  // between two files without the authentication noticing. Also, the random
  // nonces of all those blocks would have to stay collision free under that one
  // key, which is fatal for ChaCha20-Poly1305.
  QM_TRY_INSPECT(const auto& cipherKey,
                 aCipherKeyManager.Ensure(aFileId.Value()));
  QM_TRY_UNWRAP(
      RefPtr<EncryptedRandomAccessStream> stream,
      EncryptedRandomAccessStream::Create(FileSystemCipherStrategy{},
                                          std::move(baseStream), cipherKey));

  return nsCOMPtr<nsIRandomAccessStream>(std::move(stream));
}

nsresult CopyEncryptedFile(const quota::OriginMetadata& aOriginMetadata,
                           FileSystemCipherKeyManager& aCipherKeyManager,
                           const FileId& aSourceFileId, nsIFile* aSourceFile,
                           const FileId& aDestinationFileId,
                           nsIFile* aDestinationFile) {
  QM_TRY_INSPECT(const auto& source, CreateEncryptedFileRandomAccessStream(
                                         aOriginMetadata, aCipherKeyManager,
                                         aSourceFileId, aSourceFile));

  // In theory, aDestinationFile may already exist, and reading its stale
  // content as ciphertext could fail. Truncate it through a quota tracked
  // stream, so that the quota usage stays in sync.
  {
    QM_TRY_UNWRAP(nsCOMPtr<nsIRandomAccessStream> truncated,
                  CreateFileRandomAccessStream(
                      quota::PERSISTENCE_TYPE_PRIVATE, aOriginMetadata,
                      quota::Client::FILESYSTEM, aDestinationFile,
                      PR_WRONLY | PR_TRUNCATE));
    QM_TRY(MOZ_TO_RESULT(truncated->OutputStream()->Close()));
  }

  QM_TRY_INSPECT(const auto& destination,
                 CreateEncryptedFileRandomAccessStream(
                     aOriginMetadata, aCipherKeyManager, aDestinationFileId,
                     aDestinationFile));

  nsCOMPtr<nsIInputStream> const input = source->InputStream();
  nsCOMPtr<nsIOutputStream> const output = destination->OutputStream();

  auto autoClose = MakeScopeExit([input, output] {
    input->Close();
    output->Close();
  });

  constexpr uint32_t kBufferSize = 32 * 1024;
  const auto buffer = std::make_unique<char[]>(kBufferSize);

  while (true) {
    uint32_t read = 0;
    QM_TRY(MOZ_TO_RESULT(input->Read(buffer.get(), kBufferSize, &read)));
    if (!read) {
      break;
    }

    uint32_t written = 0;
    QM_TRY(MOZ_TO_RESULT(output->Write(buffer.get(), read, &written)));
    QM_TRY(OkIf(written == read), NS_ERROR_FAILURE);
  }

  // Closing saves the last block, so its failure is the copy's failure.
  QM_TRY(MOZ_TO_RESULT(output->Close()));

  return NS_OK;
}

}  // namespace mozilla::dom::fs
