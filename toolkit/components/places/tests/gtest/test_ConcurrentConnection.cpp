/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "places_test_harness.h"
#include "../../ConcurrentConnection.h"
#include "../../Helpers.h"

#include "mozilla/Assertions.h"
#include "mozilla/Monitor.h"
#include "mozIStorageBindingParamsArray.h"
#include "mozIStorageResultSet.h"
#include "mozIStorageRow.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsIFile.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::places;

/**
 * This file tests the ConcurrentConnection class.
 */

namespace {

class StatementCallback final : public PendingStatementCallback {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(StatementCallback,
                                       PendingStatementCallback);

  explicit StatementCallback(const nsCString& aParamValue)
      : mParamValue(aParamValue) {};

  NS_IMETHOD HandleResult(mozIStorageResultSet* aResultSet) override {
    nsCOMPtr<mozIStorageRow> row;
    MOZ_ALWAYS_SUCCEEDS(aResultSet->GetNextRow(getter_AddRefs(row)));
    MOZ_ALWAYS_SUCCEEDS(row->GetUTF8String(0, mValue));
    return NS_OK;
  }

  NS_IMETHOD
  HandleError(mozIStorageError* aError) override {
    MOZ_DIAGNOSTIC_CRASH("Unexpected error");
    return NS_OK;
  }

  NS_IMETHOD HandleCompletion(uint16_t aReason) override {
    mRv = aReason;
    mCompleted = true;
    return NS_OK;
  }

  nsresult BindParams(mozIStorageBindingParamsArray* aParamsArray) override {
    NS_ENSURE_ARG(aParamsArray);
    nsCOMPtr<mozIStorageBindingParams> params;
    nsresult rv = aParamsArray->NewBindingParams(getter_AddRefs(params));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = params->BindUTF8StringByIndex(0, mParamValue);
    NS_ENSURE_SUCCESS(rv, rv);
    return aParamsArray->AddParams(params);
  }

  uint16_t SpinUntilCompleted() {
    nsCOMPtr<nsIThread> thread(::do_GetCurrentThread());
    nsresult rv = NS_OK;
    bool processed = true;
    while (!mCompleted && NS_SUCCEEDED(rv)) {
      rv = thread->ProcessNextEvent(true, &processed);
    }
    return mRv;
  }

  nsCString mValue;

 private:
  ~StatementCallback() = default;

  bool mCompleted = false;
  uint16_t mRv = 0;
  nsCString mParamValue;
};

class TestRunnable : public Runnable {
 public:
  TestRunnable()
      : Runnable("places::TestRunnable"), mHasResult(false), mDidRun(false) {};

  NS_IMETHOD Run() override {
    MOZ_DIAGNOSTIC_ASSERT(!NS_IsMainThread(),
                          "Should not be called on the main thread");
    auto conn = ConcurrentConnection::GetInstance();
    MOZ_DIAGNOSTIC_ASSERT(conn.isSome());
    nsCOMPtr<mozIStorageStatement> stmt =
        conn.value()->GetStatementOnHelperThread(
            "SELECT * FROM sqlite_master WHERE tbl_name = 'moz_places'"_ns);
    MOZ_DIAGNOSTIC_ASSERT(stmt);
    mozStorageStatementScoper scoper(stmt);
    bool hasResult;
    MOZ_ALWAYS_SUCCEEDS(stmt->ExecuteStep(&hasResult));
    mHasResult = hasResult;
    mDidRun = true;
    return NS_OK;
  }

  bool SpinUntilResult() {
    nsCOMPtr<nsIThread> thread(::do_GetCurrentThread());
    nsresult rv = NS_OK;
    bool processed = true;
    while (!mDidRun && NS_SUCCEEDED(rv)) {
      rv = thread->ProcessNextEvent(true, &processed);
    }
    return mHasResult;
  }

 private:
  Atomic<bool> mHasResult;
  Atomic<bool> mDidRun;
  ~TestRunnable() = default;
};

class BlockingRunnable : public Runnable {
 public:
  BlockingRunnable() : Runnable("places::BlockingRunnable") {}

  NS_IMETHOD Run() override {
    MonitorAutoLock lock(mMonitor);
    while (!mReleased) {
      lock.Wait();
    }
    return NS_OK;
  }

  void Unblock() {
    MonitorAutoLock lock(mMonitor);
    mReleased = true;
    lock.Notify();
  }

 private:
  ~BlockingRunnable() = default;

  Monitor mMonitor MOZ_UNANNOTATED{"BlockingRunnable::mMonitor"};
  bool mReleased = false;
};

}  // Anonymous namespace

TEST(test_ConcurrentConnection, test_setup)
{
  // Tinderboxes are constantly on idle.  Since idle tasks can interact with
  // tests, causing random failures, disable the idle service.
  disable_idle_service();

  // Check there's no Places database file.
  nsCOMPtr<nsIFile> file;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(file));
  (void)file->Append(u"places.sqlite"_ns);
  bool exists = false;
  ASSERT_TRUE(NS_SUCCEEDED(file->Exists(&exists)));
  if (exists) {
    (void)file->Remove(false);
    (void)file->SetLeafName(u"favicons.sqlite"_ns);
    (void)file->Remove(false);
  }
}

TEST(test_ConcurrentConnection, test_database_not_present)
{
  // Initialize ConcurrentConnection.
  auto conn = ConcurrentConnection::GetInstance();
  MOZ_DIAGNOSTIC_ASSERT(conn.isSome());
  RefPtr<StatementCallback> cb =
      MakeAndAddRef<StatementCallback>("moz_icons"_ns);
  conn.value()->Queue(
      "SELECT name FROM favicons.sqlite_master WHERE type = 'table' AND tbl_name = ?"_ns,
      cb);
  RefPtr<TestRunnable> event = MakeAndAddRef<TestRunnable>();
  conn.value()->Queue(event);
  // Must await for Places to create and initialize the database as there's no
  // database file at this time. This initialized Places.
  nsCOMPtr<mozIStorageConnection> placesConn = do_get_db();
  do_check_true(placesConn);
  ASSERT_EQ(cb->SpinUntilCompleted(),
            mozIStorageStatementCallback::REASON_FINISHED);
  ASSERT_TRUE(cb->mValue.EqualsLiteral("moz_icons"));
  ASSERT_TRUE(event->SpinUntilResult());
}

TEST(test_ConcurrentConnection, test_database_initialized)
{
  // Initialize ConcurrentConnection.
  auto conn = ConcurrentConnection::GetInstance();
  MOZ_DIAGNOSTIC_ASSERT(conn.isSome());
  // GetInstance() must return the same singleton.
  auto conn2 = ConcurrentConnection::GetInstance();
  ASSERT_EQ(conn.value().get(), conn2.value().get());
  RefPtr<StatementCallback> cb =
      MakeAndAddRef<StatementCallback>("moz_places"_ns);
  conn.value()->Queue(
      "SELECT name FROM sqlite_master WHERE type = 'table' AND tbl_name = ?"_ns,
      cb);
  // Statement should be executed as Places was already initialized.
  ASSERT_EQ(cb->SpinUntilCompleted(),
            mozIStorageStatementCallback::REASON_FINISHED);
  ASSERT_TRUE(cb->mValue.EqualsLiteral("moz_places"));
}

TEST(test_ConcurrentConnection, test_queue_from_background_thread)
{
  // Queue() from a non-main thread must proxy to MT and execute correctly.
  auto conn = ConcurrentConnection::GetInstance();
  MOZ_DIAGNOSTIC_ASSERT(conn.isSome());

  RefPtr<StatementCallback> cb =
      MakeAndAddRef<StatementCallback>("moz_places"_ns);
  RefPtr<TestRunnable> event = MakeAndAddRef<TestRunnable>();

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchBackgroundTask(NS_NewRunnableFunction(
      "test_queue_off_thread", [conn = conn.value(), cb, event]() {
        conn->Queue(
            "SELECT name FROM sqlite_master WHERE type = 'table' AND tbl_name = ?"_ns,
            cb);
        conn->Queue(event);
      })));

  ASSERT_EQ(cb->SpinUntilCompleted(),
            mozIStorageStatementCallback::REASON_FINISHED);
  ASSERT_TRUE(cb->mValue.EqualsLiteral("moz_places"));
  ASSERT_TRUE(event->SpinUntilResult());
}

TEST(test_ConcurrentConnection, test_cancel_dispatched_statements_on_shutdown)
{
  auto conn = ConcurrentConnection::GetInstance();
  MOZ_DIAGNOSTIC_ASSERT(conn.isSome());

  // Keep the helper thread busy, so the statements stay queued.
  RefPtr<BlockingRunnable> blocker = MakeAndAddRef<BlockingRunnable>();
  conn.value()->Queue(blocker);

  RefPtr<StatementCallback> cb1 =
      MakeAndAddRef<StatementCallback>("moz_places"_ns);
  conn.value()->Queue(
      "SELECT name FROM sqlite_master WHERE type = 'table' AND tbl_name = ?"_ns,
      cb1);
  RefPtr<StatementCallback> cb2 =
      MakeAndAddRef<StatementCallback>("moz_icons"_ns);
  conn.value()->Queue(
      "SELECT name FROM favicons.sqlite_master WHERE type = 'table' AND tbl_name = ?"_ns,
      cb2);

  MOZ_ALWAYS_SUCCEEDS(conn.value()->BlockShutdown(nullptr));
  blocker->Unblock();

  ASSERT_EQ(cb1->SpinUntilCompleted(),
            mozIStorageStatementCallback::REASON_CANCELED);
  ASSERT_EQ(cb2->SpinUntilCompleted(),
            mozIStorageStatementCallback::REASON_CANCELED);
}

TEST(test_ConcurrentConnection, test_shutdown)
{
  RefPtr<WaitForConnectionClosed> spinClose = new WaitForConnectionClosed();
  // And let any other events finish before we quit.
  (void)NS_ProcessPendingEvents(nullptr);
}
