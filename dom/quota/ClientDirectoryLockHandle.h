/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_QUOTA_CLIENTDIRECTORYLOCKHANDLE_H_
#define DOM_QUOTA_CLIENTDIRECTORYLOCKHANDLE_H_

#include "mozilla/RefPtr.h"
#include "mozilla/dom/quota/ConditionalCompilation.h"
#include "nsISupportsImpl.h"

namespace mozilla::dom::quota {

class ClientDirectoryLock;

/**
 * @class ClientDirectoryLockHandle
 * @brief RAII-style wrapper for managing a ClientDirectoryLock.
 *
 * ClientDirectoryLockHandle is a RAII-style wrapper that manages a
 * ClientDirectoryLock created by QuotaManager::OpenClientDirectory.
 *
 * This class ensures that the associated directory lock remains acquired
 * while the handle is in scope and automatically drops it when destroyed.
 *
 * ## Usage:
 * - See QuotaManager::OpenClientDirectory for details on obtaining a
 *   ClientDirectoryLockHandle.
 * - The handle should be retained for as long as access to the directory is
 *   needed.
 *
 * ## Threading:
 * - Must be used only on the thread that created it, except that it may be
 *   safely destroyed from another thread after being moved (see also
 *   Destruction).
 * - `AssertIsOnOwningThread()` is primarily used internally to verify correct
 *   threading, but clients can use it for additional thread-safety checks if
 *   needed.
 *
 * ## Destruction:
 * - If the lock has already been dropped (e.g., due to move), the destructor
 *   does nothing.
 * - The destructor automatically drops the lock if it is still held.
 * - Thus, it is safe to destroy a handle from any thread as long as the handle
 *   was moved beforehand on the owning thread.
 *
 * ## Key Features:
 * - Move-only: Prevents accidental copies.
 * - Implicit boolean conversion to check if the handle holds a valid
 *   `ClientDirectoryLock`.
 * - Easy access to the underlying ClientDirectoryLock using `operator*` and
 *   `operator->`.
 * - Moved-from handles are placed in a well-defined inert state and can be
 *   safely inspected using `IsInert()` for diagnostic purposes.
 */
class ClientDirectoryLockHandle final {
 public:
  ClientDirectoryLockHandle();

  explicit ClientDirectoryLockHandle(
      RefPtr<ClientDirectoryLock> aClientDirectoryLock);

  ClientDirectoryLockHandle(const ClientDirectoryLockHandle&) = delete;

  ClientDirectoryLockHandle(ClientDirectoryLockHandle&& aOther) noexcept;

  ~ClientDirectoryLockHandle();

  void AssertIsOnOwningThread() const;

  ClientDirectoryLockHandle& operator=(const ClientDirectoryLockHandle&) =
      delete;

  ClientDirectoryLockHandle& operator=(
      ClientDirectoryLockHandle&& aOther) noexcept;

  explicit operator bool() const;

  ClientDirectoryLock* get() const;

  ClientDirectoryLock& operator*() const;

  ClientDirectoryLock* operator->() const;

  /**
   * Returns true if this handle is in an inert state — either it was
   * default-constructed and never assigned a lock, or it was explicitly
   * cleared (via move).
   *
   * This method is primarily intended for use in destructors of objects that
   * own a ClientDirectoryLockHandle, to assert that the lock has been properly
   * dropped and cleared before destruction.
   *
   * It is safe to call this method at any time on the owning thread. It may
   * also be called from other threads during destruction, under the assumption
   * that no other thread is concurrently accessing or modifying the handle.
   *
   * This method should not be used for control flow or runtime decision
   * making.
   */
  DIAGNOSTICONLY(bool IsInert() const);

 private:
  NS_DECL_OWNINGTHREAD

  RefPtr<ClientDirectoryLock> mClientDirectoryLock;
};

}  // namespace mozilla::dom::quota

#endif  // DOM_QUOTA_CLIENTDIRECTORYLOCKHANDLE_H_
