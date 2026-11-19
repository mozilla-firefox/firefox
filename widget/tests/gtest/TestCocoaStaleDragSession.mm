/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/Attributes.h"
#include "mozilla/EventForwards.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsDragService.h"
#include "nsIDragService.h"
#include "nsIDragSession.h"
#include "nsServiceManagerUtils.h"

extern bool gUserCancelledDrag;

namespace {

already_AddRefed<nsIDragSession> StartSession(nsIDragService* aService) {
  // StartDragSession only checks that it was given a widget provider, so the
  // service itself works as a stand-in here.
  nsCOMPtr<nsIDragSession> session = aService->StartDragSession(aService);
  return session.forget();
}

bool HasCurrentSession(nsIDragService* aService) {
  nsCOMPtr<nsIDragSession> session;
  aService->GetCurrentSession(nullptr, getter_AddRefs(session));
  return session != nullptr;
}

// Gives the session the native drag view that InvokeDragSessionImpl sets for a
// drag that we start.
void GiveNativeDragView(nsIDragSession* aSession) {
  static_cast<nsDragSession*>(aSession)->SetNativeDragViewForTests(
      (ChildView*)[[NSView alloc] initWithFrame:NSZeroRect]);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void EndSession(nsIDragSession* aSession) {
  nsCOMPtr<nsIDragSession> session = aSession;
  session->EndDragSession(false, 0);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void EndAsStale(nsDragSession* aSession) {
  RefPtr<nsDragSession> session = aSession;
  session->EndAsStale();
}

// Records whether the session reports a cancelled drag when it fires dragend,
// and ends the session again from there, as a dragend listener can.
class ReenteringDragSession final : public nsDragSession {
 public:
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD FireDragEventAtSource(
      mozilla::EventMessage aEventMessage, uint32_t aKeyModifiers) override {
    if (aEventMessage == mozilla::eDragEnd) {
      mWasCancelledAtDragEnd = mUserCancelled;
      EndDragSession(false, 0);
      mWasCancelledAfterReentry = mUserCancelled;
    }
    return NS_OK;
  }

  bool mWasCancelledAtDragEnd = false;
  bool mWasCancelledAfterReentry = false;

 private:
  ~ReenteringDragSession() = default;
};

}  // namespace

// A drag session that we started and whose native drag session is gone has to
// be ended, or it stays alive for the rest of the session and blocks every drag
// that follows.
TEST(CocoaStaleDragSession, EndsSessionWithoutNativeDrag)
{
  nsCOMPtr<nsIDragService> service =
      do_GetService("@mozilla.org/widget/dragservice;1");
  ASSERT_NE(service, nullptr);
  ASSERT_FALSE(HasCurrentSession(service));

  nsCOMPtr<nsIDragSession> session = StartSession(service);
  ASSERT_NE(session, nullptr);
  ASSERT_TRUE(HasCurrentSession(service));
  GiveNativeDragView(session);

  nsDragService::EndStaleDragSession("test");
  EXPECT_FALSE(HasCurrentSession(service));

  // The cancellation is only for the stale session, not for the next drag.
  EXPECT_FALSE(gUserCancelledDrag);
}

// A stale session reports a cancelled drag, also when it is ended again while
// it fires dragend, and the cancellation does not carry over to the next drag.
TEST(CocoaStaleDragSession, ReportsCancellation)
{
  RefPtr<ReenteringDragSession> session =
      mozilla::MakeRefPtr<ReenteringDragSession>();
  GiveNativeDragView(session);

  EndAsStale(session);
  EXPECT_TRUE(session->mWasCancelledAtDragEnd);
  EXPECT_TRUE(session->mWasCancelledAfterReentry);
  EXPECT_FALSE(gUserCancelledDrag);
}

// A drag that another application started stays over our views until the
// system tells us that it exited or dropped, even if we get mouse events in the
// meantime.
TEST(CocoaStaleDragSession, KeepsSessionFromOtherApplication)
{
  nsCOMPtr<nsIDragService> service =
      do_GetService("@mozilla.org/widget/dragservice;1");
  ASSERT_NE(service, nullptr);
  ASSERT_FALSE(HasCurrentSession(service));

  nsCOMPtr<nsIDragSession> session = StartSession(service);
  ASSERT_NE(session, nullptr);

  nsDragService::EndStaleDragSession("test");
  EXPECT_TRUE(HasCurrentSession(service));

  EndSession(session);
  EXPECT_FALSE(HasCurrentSession(service));
}

// Sessions that automated tests drive by hand must be left alone, even when
// they look like a drag that we started.
TEST(CocoaStaleDragSession, KeepsSessionForTests)
{
  nsCOMPtr<nsIDragService> service =
      do_GetService("@mozilla.org/widget/dragservice;1");
  ASSERT_NE(service, nullptr);
  ASSERT_FALSE(HasCurrentSession(service));

  nsCOMPtr<nsIDragSession> session = StartSession(service);
  ASSERT_NE(session, nullptr);
  session->InitForTests(nsIDragService::DRAGDROP_ACTION_MOVE);
  GiveNativeDragView(session);

  nsDragService::EndStaleDragSession("test");
  EXPECT_TRUE(HasCurrentSession(service));

  EndSession(session);
  EXPECT_FALSE(HasCurrentSession(service));
}
