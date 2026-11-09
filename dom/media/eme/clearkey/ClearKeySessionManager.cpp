/*
 * Copyright 2015, Mozilla Foundation and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ClearKeySessionManager.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ClearKeyDecryptionManager.h"
#include "ClearKeyPersistence.h"
#include "ClearKeyStorage.h"
#include "ClearKeyUtils.h"
#include "content_decryption_module.h"
#include "psshparser/PsshParser.h"

using namespace cdm;

using std::function;
using std::string;
using std::vector;

ClearKeySessionManager::ClearKeySessionManager(Host_11* aHost)
    : mDecryptionManager(ClearKeyDecryptionManager::Get()) {
  CK_LOGD("ClearKeySessionManager ctor %p", this);
  AddRef();

  mHost = aHost;
  mPersistence = new ClearKeyPersistence(mHost);
}

ClearKeySessionManager::~ClearKeySessionManager() {
  CK_LOGD("ClearKeySessionManager dtor %p", this);
}

void ClearKeySessionManager::Init(bool aDistinctiveIdentifierAllowed,
                                  bool aPersistentStateAllowed) {
  CK_LOGD("ClearKeySessionManager::Init");

  RefPtr<ClearKeySessionManager> self(this);
  function<void()> onPersistentStateLoaded = [self]() {
    while (!self->mDeferredInitialize.empty()) {
      function<void()> func = self->mDeferredInitialize.front();
      self->mDeferredInitialize.pop();

      func();
    }
    if (self->mHost) {
      // The session manager should be the last thing the ClearKey CDM is
      // waiting on to be initialized.
      self->mHost->OnInitialized(true);
    }
  };

  mPersistence->EnsureInitialized(aPersistentStateAllowed,
                                  std::move(onPersistentStateLoaded));
}

void ClearKeySessionManager::CreateSession(uint32_t aPromiseId,
                                           InitDataType aInitDataType,
                                           const uint8_t* aInitData,
                                           uint32_t aInitDataSize,
                                           SessionType aSessionType) {
  CK_LOGD("ClearKeySessionManager::CreateSession type:%u", aInitDataType);

  // Copy the init data so it is correctly captured by the lambda
  vector<uint8_t> initData(aInitData, aInitData + aInitDataSize);

  RefPtr<ClearKeySessionManager> self(this);
  function<void()> deferrer = [self, aPromiseId, aInitDataType,
                               initData = std::move(initData), aSessionType]() {
    self->CreateSession(aPromiseId, aInitDataType, initData.data(),
                        initData.size(), aSessionType);
  };

  // If we haven't loaded, don't do this yet
  if (MaybeDeferTillInitialized(std::move(deferrer))) {
    CK_LOGD("Deferring CreateSession");
    return;
  }

  CK_LOGARRAY("ClearKeySessionManager::CreateSession initdata: ", aInitData,
              aInitDataSize);

  // If 'DecryptingComplete' has been called mHost will be null so we can't
  // won't be able to resolve our promise
  if (!mHost) {
    CK_LOGD("ClearKeySessionManager::CreateSession: mHost is nullptr");
    return;
  }

  // initDataType must be "cenc", "keyids", or "webm".
  if (aInitDataType != InitDataType::kCenc &&
      aInitDataType != InitDataType::kKeyIds &&
      aInitDataType != InitDataType::kWebM) {
    string message = "initDataType is not supported by ClearKey";
    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionNotSupportedError,
                           0, message.c_str(), message.size());

    return;
  }

  string sessionId = mPersistence->GetNewSessionId(aSessionType);
  assert(mSessions.find(sessionId) == mSessions.end());

  ClearKeySession* session = new ClearKeySession(sessionId, aSessionType);

  if (!session->Init(aInitDataType, aInitData, aInitDataSize)) {
    CK_LOGD("Failed to initialize session: %s", sessionId.c_str());

    const static char* message = "Failed to initialize session";
    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionInvalidStateError,
                           0, message, strlen(message));
    delete session;

    return;
  }

  mSessions[sessionId] = session;
  mLastSessionId = sessionId;

  const vector<KeyId>& sessionKeys = session->GetKeyIds();
  vector<KeyId> neededKeys;

  for (auto it = sessionKeys.begin(); it != sessionKeys.end(); it++) {
    // Need to request this key ID from the client. We always send a key
    // request, whether or not another session has sent a request with the same
    // key ID. Otherwise a script can end up waiting for another script to
    // respond to the request (which may not necessarily happen).
    CK_LOGARRAY("Key ID: ", it->data(), it->size());
    neededKeys.push_back(*it);
    mDecryptionManager->ExpectKeyId(*it);
  }

  if (neededKeys.empty()) {
    CK_LOGD("No keys needed from client.");
    return;
  }

  // Send a request for needed key data.
  string request;
  ClearKeyUtils::MakeKeyRequest(neededKeys, request, aSessionType);

  // Resolve the promise with the new session information.
  mHost->OnResolveNewSessionPromise(aPromiseId, sessionId.c_str(),
                                    sessionId.size());

  mHost->OnSessionMessage(sessionId.c_str(), sessionId.size(),
                          MessageType::kLicenseRequest, request.c_str(),
                          request.size());
}

void ClearKeySessionManager::LoadSession(uint32_t aPromiseId,
                                         const char* aSessionId,
                                         uint32_t aSessionIdLength) {
  CK_LOGD("ClearKeySessionManager::LoadSession");

  // Copy the sessionId into a string so the lambda captures it properly.
  string sessionId(aSessionId, aSessionId + aSessionIdLength);

  // Hold a reference to the SessionManager so that it isn't released before
  // we try to use it.
  RefPtr<ClearKeySessionManager> self(this);
  function<void()> deferrer = [self, aPromiseId, sessionId]() {
    self->LoadSession(aPromiseId, sessionId.data(), sessionId.size());
  };

  if (MaybeDeferTillInitialized(std::move(deferrer))) {
    CK_LOGD("Deferring LoadSession");
    return;
  }

  // If the SessionManager has been shutdown mHost will be null and we won't
  // be able to resolve the promise.
  if (!mHost) {
    return;
  }

  if (!ClearKeyUtils::IsValidSessionId(aSessionId, aSessionIdLength)) {
    mHost->OnResolveNewSessionPromise(aPromiseId, nullptr, 0);
    return;
  }

  if (!mPersistence->IsPersistentSessionId(sessionId)) {
    mHost->OnResolveNewSessionPromise(aPromiseId, nullptr, 0);
    return;
  }

  function<void(const uint8_t*, uint32_t)> success =
      [self, sessionId, aPromiseId](const uint8_t* data, uint32_t size) {
        self->PersistentSessionDataLoaded(aPromiseId, sessionId, data, size);
      };

  function<void()> failure = [self, aPromiseId] {
    if (!self->mHost) {
      return;
    }
    // As per the API described in ContentDecryptionModule_8
    self->mHost->OnResolveNewSessionPromise(aPromiseId, nullptr, 0);
  };

  ReadData(mHost, sessionId, std::move(success), std::move(failure));
}

void ClearKeySessionManager::PersistentSessionDataLoaded(
    uint32_t aPromiseId, const string& aSessionId, const uint8_t* aKeyData,
    uint32_t aKeyDataSize) {
  CK_LOGD("ClearKeySessionManager::PersistentSessionDataLoaded");

  // Check that the SessionManager has not been shut down before we try and
  // resolve any promises.
  if (!mHost) {
    return;
  }

  if (Contains(mSessions, aSessionId) ||
      (aKeyDataSize % (2 * CENC_KEY_LEN)) != 0) {
    // As per the instructions in ContentDecryptionModule_8
    mHost->OnResolveNewSessionPromise(aPromiseId, nullptr, 0);
    return;
  }

  ClearKeySession* session =
      new ClearKeySession(aSessionId, SessionType::kPersistentLicense);

  mSessions[aSessionId] = session;
  mLastSessionId = aSessionId;

  uint32_t numKeys = aKeyDataSize / (2 * CENC_KEY_LEN);

  vector<KeyInformation> keyInfos;
  vector<KeyIdPair> keyPairs;
  for (uint32_t i = 0; i < numKeys; i++) {
    const uint8_t* base = aKeyData + 2 * CENC_KEY_LEN * i;

    KeyIdPair keyPair;

    keyPair.mKeyId = KeyId(base, base + CENC_KEY_LEN);
    assert(keyPair.mKeyId.size() == CENC_KEY_LEN);

    keyPair.mKey = Key(base + CENC_KEY_LEN, base + 2 * CENC_KEY_LEN);
    assert(keyPair.mKey.size() == CENC_KEY_LEN);

    session->AddKeyId(keyPair.mKeyId);

    mDecryptionManager->ExpectKeyId(keyPair.mKeyId);
    mDecryptionManager->InitKey(keyPair.mKeyId, keyPair.mKey);
    mKeyIds.insert(keyPair.mKey);
    keyPairs.push_back(keyPair);

    KeyInformation keyInfo = {};
    keyInfo.key_id = keyPairs.back().mKeyId.data();
    keyInfo.key_id_size = keyPair.mKeyId.size();
    keyInfo.status = KeyStatus::kUsable;

    keyInfos.push_back(keyInfo);
  }

  mHost->OnSessionKeysChange(aSessionId.data(), aSessionId.size(), true,
                             keyInfos.data(), keyInfos.size());

  mHost->OnResolveNewSessionPromise(aPromiseId, aSessionId.c_str(),
                                    aSessionId.size());
}

void ClearKeySessionManager::UpdateSession(uint32_t aPromiseId,
                                           const char* aSessionId,
                                           uint32_t aSessionIdLength,
                                           const uint8_t* aResponse,
                                           uint32_t aResponseSize) {
  CK_LOGD("ClearKeySessionManager::UpdateSession");

  // Copy the method arguments so we can capture them in the lambda
  string sessionId(aSessionId, aSessionId + aSessionIdLength);
  vector<uint8_t> response(aResponse, aResponse + aResponseSize);

  // Hold  a reference to the SessionManager so it isn't released before we
  // callback.
  RefPtr<ClearKeySessionManager> self(this);
  // sessionId is captured by copy (not std::move) because the outer function
  // still uses it below to look up the session in mSessions.
  function<void()> deferrer = [self, aPromiseId, sessionId, response]() {
    self->UpdateSession(aPromiseId, sessionId.data(), sessionId.size(),
                        response.data(), response.size());
  };

  // If we haven't fully loaded, defer calling this method
  if (MaybeDeferTillInitialized(std::move(deferrer))) {
    CK_LOGD("Deferring LoadSession");
    return;
  }

  // Make sure the SessionManager has not been shutdown before we try and
  // resolve any promises.
  if (!mHost) {
    return;
  }

  CK_LOGD("Updating session: %s", sessionId.c_str());

  auto itr = mSessions.find(sessionId);
  if (itr == mSessions.end() || !(itr->second)) {
    CK_LOGW("ClearKey CDM couldn't resolve session ID in UpdateSession.");
    CK_LOGD("Unable to find session: %s", sessionId.c_str());
    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                           nullptr, 0);

    return;
  }
  ClearKeySession* session = itr->second;

  // Verify the size of session response.
  if (aResponseSize >= kMaxSessionResponseLength) {
    CK_LOGW("Session response size is not within a reasonable size.");
    CK_LOGD("Failed to parse response for session %s", sessionId.c_str());

    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                           nullptr, 0);

    return;
  }

  // Parse the response for any (key ID, key) pairs.
  vector<KeyIdPair> keyPairs;
  if (!ClearKeyUtils::ParseJWK(aResponse, aResponseSize, keyPairs,
                               session->Type())) {
    CK_LOGW("ClearKey CDM failed to parse JSON Web Key.");

    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                           nullptr, 0);

    return;
  }

  vector<KeyInformation> keyInfos;
  for (size_t i = 0; i < keyPairs.size(); i++) {
    KeyIdPair& keyPair = keyPairs[i];
    mDecryptionManager->InitKey(keyPair.mKeyId, keyPair.mKey);
    mKeyIds.insert(keyPair.mKeyId);

    KeyInformation keyInfo = {};
    keyInfo.key_id = keyPair.mKeyId.data();
    keyInfo.key_id_size = keyPair.mKeyId.size();
    keyInfo.status = KeyStatus::kUsable;

    keyInfos.push_back(keyInfo);
  }

  mHost->OnSessionKeysChange(aSessionId, aSessionIdLength, true,
                             keyInfos.data(), keyInfos.size());

  if (session->Type() != SessionType::kPersistentLicense) {
    mHost->OnResolvePromise(aPromiseId);
    return;
  }

  // Store the keys on disk. We store a record whose name is the sessionId,
  // and simply append each keyId followed by its key.
  vector<uint8_t> keydata;
  Serialize(session, keydata);

  function<void()> resolve = [self, aPromiseId]() {
    if (!self->mHost) {
      return;
    }
    self->mHost->OnResolvePromise(aPromiseId);
  };

  function<void()> reject = [self, aPromiseId]() {
    if (!self->mHost) {
      return;
    }

    static const char* message = "Couldn't store cenc key init data";
    self->mHost->OnRejectPromise(aPromiseId,
                                 Exception::kExceptionInvalidStateError, 0,
                                 message, strlen(message));
  };

  WriteData(mHost, sessionId, keydata, std::move(resolve), std::move(reject));
}

void ClearKeySessionManager::Serialize(const ClearKeySession* aSession,
                                       std::vector<uint8_t>& aOutKeyData) {
  const std::vector<KeyId>& keyIds = aSession->GetKeyIds();
  for (size_t i = 0; i < keyIds.size(); i++) {
    const KeyId& keyId = keyIds[i];
    if (!mDecryptionManager->HasKeyForKeyId(keyId)) {
      continue;
    }
    assert(keyId.size() == CENC_KEY_LEN);
    aOutKeyData.insert(aOutKeyData.end(), keyId.begin(), keyId.end());
    const Key& key = mDecryptionManager->GetDecryptionKey(keyId);
    assert(key.size() == CENC_KEY_LEN);
    aOutKeyData.insert(aOutKeyData.end(), key.begin(), key.end());
  }
}

void ClearKeySessionManager::CloseSession(uint32_t aPromiseId,
                                          const char* aSessionId,
                                          uint32_t aSessionIdLength) {
  CK_LOGD("ClearKeySessionManager::CloseSession");

  // Copy the sessionId into a string so we capture it properly.
  string sessionId(aSessionId, aSessionId + aSessionIdLength);
  // Hold a reference to the session manager, so it doesn't get deleted
  // before we need to use it.
  RefPtr<ClearKeySessionManager> self(this);
  function<void()> deferrer = [self, aPromiseId, sessionId]() {
    self->CloseSession(aPromiseId, sessionId.data(), sessionId.size());
  };

  // If we haven't loaded, call this method later.
  if (MaybeDeferTillInitialized(std::move(deferrer))) {
    CK_LOGD("Deferring CloseSession");
    return;
  }

  // If DecryptingComplete has been called mHost will be null and we won't
  // be able to resolve our promise.
  if (!mHost) {
    return;
  }

  auto itr = mSessions.find(sessionId);
  if (itr == mSessions.end()) {
    CK_LOGW("ClearKey CDM couldn't close non-existent session.");
    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                           nullptr, 0);

    return;
  }

  ClearKeySession* session = itr->second;
  assert(session);

  ClearInMemorySessionData(session);

  mHost->OnSessionClosed(aSessionId, aSessionIdLength);
  mHost->OnResolvePromise(aPromiseId);
}

void ClearKeySessionManager::ClearInMemorySessionData(
    ClearKeySession* aSession) {
  mSessions.erase(aSession->Id());
  delete aSession;
}

void ClearKeySessionManager::RemoveSession(uint32_t aPromiseId,
                                           const char* aSessionId,
                                           uint32_t aSessionIdLength) {
  CK_LOGD("ClearKeySessionManager::RemoveSession");

  // Copy the sessionId into a string so it can be captured for the lambda.
  string sessionId(aSessionId, aSessionId + aSessionIdLength);

  // Hold a reference to the SessionManager, so it isn't released before we
  // try and use it.
  RefPtr<ClearKeySessionManager> self(this);
  function<void()> deferrer = [self, aPromiseId, sessionId]() {
    self->RemoveSession(aPromiseId, sessionId.data(), sessionId.size());
  };

  // If we haven't fully loaded, defer calling this method.
  if (MaybeDeferTillInitialized(std::move(deferrer))) {
    CK_LOGD("Deferring RemoveSession");
    return;
  }

  // Check that the SessionManager has not been shutdown before we try and
  // resolve any promises.
  if (!mHost) {
    return;
  }

  auto itr = mSessions.find(sessionId);
  if (itr == mSessions.end()) {
    CK_LOGW("ClearKey CDM couldn't remove non-existent session.");

    mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                           nullptr, 0);

    return;
  }

  ClearKeySession* session = itr->second;
  assert(session);
  string sid = session->Id();
  bool isPersistent = session->Type() == SessionType::kPersistentLicense;
  ClearInMemorySessionData(session);

  if (!isPersistent) {
    mHost->OnResolvePromise(aPromiseId);
    return;
  }

  mPersistence->PersistentSessionRemoved(sid);

  vector<uint8_t> emptyKeydata;

  function<void()> resolve = [self, aPromiseId]() {
    if (!self->mHost) {
      return;
    }
    self->mHost->OnResolvePromise(aPromiseId);
  };

  function<void()> reject = [self, aPromiseId]() {
    if (!self->mHost) {
      return;
    }
    static const char* message = "Could not remove session";
    self->mHost->OnRejectPromise(aPromiseId, Exception::kExceptionTypeError, 0,
                                 message, strlen(message));
  };

  WriteData(mHost, sessionId, emptyKeydata, std::move(resolve),
            std::move(reject));
}

void ClearKeySessionManager::SetServerCertificate(uint32_t aPromiseId,
                                                  const uint8_t* aServerCert,
                                                  uint32_t aServerCertSize) {
  // ClearKey CDM doesn't support this method by spec.
  CK_LOGD("ClearKeySessionManager::SetServerCertificate");
  mHost->OnRejectPromise(aPromiseId, Exception::kExceptionNotSupportedError, 0,
                         nullptr /* message */, 0 /* messageLen */);
}

Status ClearKeySessionManager::Decrypt(const InputBuffer_2& aBuffer,
                                       DecryptedBlock* aDecryptedBlock) {
  CK_LOGD("ClearKeySessionManager::Decrypt");

  CK_LOGARRAY("Key: ", aBuffer.key_id, aBuffer.key_id_size);

  Buffer* buffer = mHost->Allocate(aBuffer.data_size);
  assert(buffer != nullptr);
  assert(buffer->Data() != nullptr);
  assert(buffer->Capacity() >= aBuffer.data_size);

  memcpy(buffer->Data(), aBuffer.data, aBuffer.data_size);

  Status status = Status::kSuccess;
  // According to the comment `If |iv_size| = 0, the data is unencrypted.`
  // Use iv_size to determine if the sample is encrypted.
  if (aBuffer.iv_size != 0) {
    status = mDecryptionManager->Decrypt(buffer->Data(), buffer->Size(),
                                         CryptoMetaData(&aBuffer));
  }

  aDecryptedBlock->SetDecryptedBuffer(buffer);
  aDecryptedBlock->SetTimestamp(aBuffer.timestamp);

  return status;
}

void ClearKeySessionManager::DecryptingComplete() {
  CK_LOGD("ClearKeySessionManager::DecryptingComplete %p", this);

  for (auto it = mSessions.begin(); it != mSessions.end(); it++) {
    delete it->second;
  }
  mSessions.clear();
  mLastSessionId = std::nullopt;

  mDecryptionManager = nullptr;
  mHost = nullptr;

  Release();
}

bool ClearKeySessionManager::MaybeDeferTillInitialized(
    function<void()>&& aMaybeDefer) {
  if (mPersistence->IsLoaded()) {
    return false;
  }

  mDeferredInitialize.emplace(std::move(aMaybeDefer));
  return true;
}

void ClearKeySessionManager::OnQueryOutputProtectionStatus(
    QueryResult aResult, uint32_t aLinkMask, uint32_t aOutputProtectionMask) {
  MOZ_ASSERT(mHasOutstandingOutputProtectionQuery,
             "Should only be called if a query is outstanding");
  CK_LOGD("ClearKeySessionManager::OnQueryOutputProtectionStatus");
  mHasOutstandingOutputProtectionQuery = false;

  if (aResult == QueryResult::kQueryFailed) {
    // Indicate the query failed. This can happen if we're in shutdown.
    NotifyOutputProtectionStatus(KeyStatus::kInternalError);
    return;
  }

  if (aLinkMask & OutputLinkTypes::kLinkTypeNetwork) {
    NotifyOutputProtectionStatus(KeyStatus::kOutputRestricted);
    return;
  }

  NotifyOutputProtectionStatus(KeyStatus::kUsable);
}

void ClearKeySessionManager::QueryOutputProtectionStatusIfNeeded() {
  MOZ_ASSERT(
      mHost,
      "Should not query protection status if we're shutdown (mHost == null)!");
  CK_LOGD(
      "ClearKeySessionManager::UpdateOutputProtectionStatusAndQueryIfNeeded");
  if (mLastOutputProtectionQueryTime.IsNull()) {
    // We haven't perfomed a check yet, get a query going.
    MOZ_ASSERT(
        !mHasOutstandingOutputProtectionQuery,
        "Shouldn't have an outstanding query if we haven't recorded a time");
    QueryOutputProtectionStatusFromHost();
    return;
  }

  MOZ_ASSERT(!mLastOutputProtectionQueryTime.IsNull(),
             "Should have already handled the case where we don't yet have a "
             "previous check time");
  const mozilla::TimeStamp now = mozilla::TimeStamp::NowLoRes();
  const mozilla::TimeDuration timeSinceQuery =
      now - mLastOutputProtectionQueryTime;

  // The time between output protection checks to the host. I.e. if this amount
  // of time has passed since the last check with the host, another should be
  // performed (provided the first check has been handled).
  static const mozilla::TimeDuration kOutputProtectionQueryInterval =
      mozilla::TimeDuration::FromMilliseconds(200);
  // The number of kOutputProtectionQueryInterval intervals we can miss before
  // we decide a check has failed. I.e. if this value is 2, if we have not
  // received a reply to a check after kOutputProtectionQueryInterval * 2
  // time, we consider the check failed.
  constexpr uint32_t kMissedIntervalsBeforeFailure = 2;
  // The length of time after which we will restrict output until we get a
  // query response.
  static const mozilla::TimeDuration kTimeToWaitBeforeFailure =
      kOutputProtectionQueryInterval * kMissedIntervalsBeforeFailure;

  if ((timeSinceQuery > kOutputProtectionQueryInterval) &&
      !mHasOutstandingOutputProtectionQuery) {
    // We don't have an outstanding query and enough time has passed we should
    // query again.
    QueryOutputProtectionStatusFromHost();
    return;
  }

  if ((timeSinceQuery > kTimeToWaitBeforeFailure) &&
      mHasOutstandingOutputProtectionQuery) {
    // A reponse was not received fast enough, notify.
    NotifyOutputProtectionStatus(KeyStatus::kInternalError);
  }
}

void ClearKeySessionManager::QueryOutputProtectionStatusFromHost() {
  MOZ_ASSERT(
      mHost,
      "Should not query protection status if we're shutdown (mHost == null)!");
  CK_LOGD("ClearKeySessionManager::QueryOutputProtectionStatusFromHost");
  if (mHost) {
    mLastOutputProtectionQueryTime = mozilla::TimeStamp::NowLoRes();
    mHost->QueryOutputProtectionStatus();
    mHasOutstandingOutputProtectionQuery = true;
  }
}

void ClearKeySessionManager::NotifyOutputProtectionStatus(KeyStatus aStatus) {
  MOZ_ASSERT(aStatus == KeyStatus::kUsable ||
                 aStatus == KeyStatus::kOutputRestricted ||
                 aStatus == KeyStatus::kInternalError,
             "aStatus should have an expected value");
  CK_LOGD("ClearKeySessionManager::NotifyOutputProtectionStatus");
  if (!mLastSessionId.has_value()) {
    // If we don't have a session id, either because we're too early, or are
    // shutting down, don't notify.
    return;
  }

  string& lastSessionId = mLastSessionId.value();

  // Use 'output-protection' as the key ID. This helps tests disambiguate key
  // status updates related to this.
  const uint8_t kKeyId[] = {'o', 'u', 't', 'p', 'u', 't', '-', 'p', 'r',
                            'o', 't', 'e', 'c', 't', 'i', 'o', 'n'};
  KeyInformation keyInfo = {};
  keyInfo.key_id = kKeyId;
  keyInfo.key_id_size = std::size(kKeyId);
  keyInfo.status = aStatus;

  vector<KeyInformation> keyInfos;
  keyInfos.push_back(keyInfo);

  // At time of writing, Gecko's higher level handling doesn't use this arg.
  // However, we set it to false to mimic Chromium's similar case. Since
  // Clearkey is used to test the Chromium CDM path, it doesn't hurt to try
  // and mimic their behaviour.
  bool hasAdditionalUseableKey = false;
  mHost->OnSessionKeysChange(lastSessionId.c_str(), lastSessionId.size(),
                             hasAdditionalUseableKey, keyInfos.data(),
                             keyInfos.size());
}
