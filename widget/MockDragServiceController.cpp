/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MockDragServiceController.h"

#include "mozilla/MouseEvents.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DragEvent.h"
#include "nsBaseDragService.h"
#include "nsIFrame.h"
#include "nsPresContext.h"

namespace mozilla::test {

NS_IMPL_ISUPPORTS(MockDragServiceController, nsIMockDragServiceController)

class MockDragSession : public nsBaseDragSession {
 protected:
  MOZ_CAN_RUN_SCRIPT nsresult
  InvokeDragSessionImpl(nsIWidget* aWidget, nsIArray* aTransferableArray,
                        const mozilla::Maybe<mozilla::CSSIntRegion>& aRegion,
                        uint32_t aActionType) override {
    // In Windows' nsDragService, InvokeDragSessionImpl would establish a
    // nested (native) event loop that runs as long as the drag is happening.
    // See DoDragDrop in MSDN.
    // We cannot do anything like that here since it would block mochitest
    // from scripting behavior with MockDragServiceController::SendDragEvent.

    // mDragAction is not yet handled properly in the MockDragService.
    // This should be updated with each drag event.  Instead, we always MOVE.
    //
    // We still need to end the drag session on the source widget.
    // In normal (non-mock) Gecko operation, this happens regardless
    // of whether the drop/cancel happened over one of our widgets.
    // For instance, Windows does this in StartInvokingDragSession, after
    // DoDragDrop returns, gtk does this on eDragTaskSourceEnd and cocoa
    // does this in -[NSDraggingSource draggingSession: endedAt: operation:].
    //
    // Here, we know the source and target are Gecko windows (the test
    // framework does not support dragging to/from other apps).  We could
    // end the source drag session on drop and cancel, but sometimes we
    // will want a dragleave to end the session (representing a drag cancel
    // from Gecko) and other we times don't (like when dragging from one Gecko
    // widget to another).  Therefore, we instead rely on the test to tell
    // us when to end the source session by calling
    // MockDragServiceController::EndSourceDragSession().
    // Note that, like in non-mocked DND, we do this regardless of whether
    // the source and target were the same widget -- in that case,
    // EndDragSession is just called twice.
    mDragAction = nsIDragService::DRAGDROP_ACTION_MOVE;
    return NS_OK;
  }
};

class MockDragService : public nsBaseDragService {
 public:
  MockDragService() { SetNeverAllowSessionIsSynthesizedForTests(true); }

  NS_IMETHOD GetIsMockService(bool* aRet) override {
    *aRet = true;
    return NS_OK;
  }
  uint32_t mLastModifierKeyState = 0;

 protected:
  already_AddRefed<nsIDragSession> CreateDragSession() override {
    auto session = MakeRefPtr<MockDragSession>();
    return session.forget();
  }
};

static void SetDragEndPointFromScreenPoint(
    nsIDragSession* aSession, nsPresContext* aPc,
    const LayoutDeviceIntPoint& aScreenPt) {
  // aScreenPt is screen-relative, and we want to be
  // top-level-widget-relative.
  auto* widget = aPc->GetRootWidget();
  auto pt = aScreenPt - widget->WidgetToScreenOffset();
  pt += widget->WidgetToTopLevelWidgetOffset();
  aSession->SetDragEndPoint(pt.x, pt.y);
}

static bool IsMouseEvent(nsIMockDragServiceController::EventType aEventType) {
  using EventType = nsIMockDragServiceController::EventType;
  switch (aEventType) {
    case EventType::eMouseDown:
    case EventType::eMouseMove:
    case EventType::eMouseUp:
      return true;
    default:
      return false;
  }
}

static EventMessage MockEventTypeToEventMessage(
    nsIMockDragServiceController::EventType aEventType) {
  using EventType = nsIMockDragServiceController::EventType;

  switch (aEventType) {
    case EventType::eDragEnter:
      return EventMessage::eDragEnter;
    case EventType::eDragOver:
      return EventMessage::eDragOver;
    case EventType::eDragExit:
      return EventMessage::eDragExit;
    case EventType::eDrop:
      return EventMessage::eDrop;
    case EventType::eMouseDown:
      return EventMessage::eMouseDown;
    case EventType::eMouseMove:
      return EventMessage::eMouseMove;
    case EventType::eMouseUp:
      return EventMessage::eMouseUp;
    default:
      MOZ_ASSERT(false, "Invalid event type");
      return EventMessage::eVoidEvent;
  }
}

MockDragServiceController::MockDragServiceController()
    : mDragService(new MockDragService()) {}

MockDragServiceController::~MockDragServiceController() = default;

NS_IMETHODIMP
MockDragServiceController::GetMockDragService(nsIDragService** aService) {
  RefPtr<nsIDragService> ds = mDragService;
  ds.forget(aService);
  return NS_OK;
}

NS_IMETHODIMP
MockDragServiceController::SendEvent(
    dom::BrowsingContext* aBC,
    nsIMockDragServiceController::EventType aEventType, int32_t aScreenX,
    int32_t aScreenY, uint32_t aKeyModifiers = 0) {
  auto* embedder = aBC->Top()->GetEmbedderElement();
  NS_ENSURE_TRUE(embedder, NS_ERROR_UNEXPECTED);
  auto* frame = embedder->GetPrimaryFrame();
  NS_ENSURE_TRUE(frame, NS_ERROR_UNEXPECTED);
  auto* presCxt = frame->PresContext();
  MOZ_ASSERT(presCxt);
  RefPtr<nsIWidget> widget = nsContentUtils::WidgetForContent(embedder);
  NS_ENSURE_TRUE(widget, NS_ERROR_UNEXPECTED);

  EventMessage eventType = MockEventTypeToEventMessage(aEventType);
  UniquePtr<WidgetInputEvent> widgetEvent;
  if (IsMouseEvent(aEventType)) {
    widgetEvent = std::make_unique<WidgetMouseEvent>(
        true, eventType, widget, WidgetMouseEvent::Reason::eReal);
  } else {
    widgetEvent = std::make_unique<WidgetDragEvent>(true, eventType, widget);
  }

  widgetEvent->mWidget = widget;
  widgetEvent->mFlags.mIsSynthesizedForTests = true;

  auto clientPosInScreenCoords = widget->GetClientBounds().TopLeft();
  widgetEvent->mRefPoint =
      LayoutDeviceIntPoint(aScreenX, aScreenY) - clientPosInScreenCoords;

  RefPtr<MockDragService> ds = mDragService;

  if (aEventType == EventType::eDragEnter) {
    // We expect StartDragSession to return an "error" when a drag session
    // already exists, which it will since we are testing dragging from
    // inside Gecko, so we don't check the return value.
    ds->StartDragSession(widget);
  }

  nsCOMPtr<nsIDragSession> currentDragSession = ds->GetCurrentSession(widget);

  nsresult rv;
  switch (aEventType) {
    case EventType::eMouseDown:
    case EventType::eMouseMove:
    case EventType::eMouseUp:
      widget->DispatchInputEvent(widgetEvent.get());
      break;
    case EventType::eDragEnter:
      NS_ENSURE_TRUE(currentDragSession, NS_ERROR_UNEXPECTED);
      currentDragSession->SetDragAction(nsIDragService::DRAGDROP_ACTION_MOVE);
      widget->DispatchInputEvent(widgetEvent.get());
      break;
    case EventType::eDragExit: {
      NS_ENSURE_TRUE(currentDragSession, NS_ERROR_UNEXPECTED);
      currentDragSession->SetDragAction(nsIDragService::DRAGDROP_ACTION_MOVE);
      widget->DispatchInputEvent(widgetEvent.get());
      SetDragEndPointFromScreenPoint(currentDragSession, presCxt,
                                     LayoutDeviceIntPoint(aScreenX, aScreenY));
      nsCOMPtr<nsINode> sourceNode;
      rv = currentDragSession->GetSourceNode(getter_AddRefs(sourceNode));
      NS_ENSURE_SUCCESS(rv, rv);
      if (!sourceNode) {
        rv = currentDragSession->EndDragSession(false /* doneDrag */,
                                                aKeyModifiers);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    } break;
    case EventType::eDragOver:
      NS_ENSURE_TRUE(currentDragSession, NS_ERROR_UNEXPECTED);
      rv = currentDragSession->FireDragEventAtSource(EventMessage::eDrag,
                                                     aKeyModifiers);
      NS_ENSURE_SUCCESS(rv, rv);
      currentDragSession->SetDragAction(nsIDragService::DRAGDROP_ACTION_MOVE);
      widget->DispatchInputEvent(widgetEvent.get());
      break;
    case EventType::eDrop: {
      NS_ENSURE_TRUE(currentDragSession, NS_ERROR_UNEXPECTED);
      currentDragSession->SetDragAction(nsIDragService::DRAGDROP_ACTION_MOVE);
      widget->DispatchInputEvent(widgetEvent.get());
      SetDragEndPointFromScreenPoint(currentDragSession, presCxt,
                                     LayoutDeviceIntPoint(aScreenX, aScreenY));
      rv = currentDragSession->EndDragSession(true /* doneDrag */,
                                              aKeyModifiers);
      NS_ENSURE_SUCCESS(rv, rv);
    } break;
    default:
      MOZ_ASSERT_UNREACHABLE("Impossible event type?");
      return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
MockDragServiceController::CancelDrag(uint32_t aKeyModifiers = 0) {
  RefPtr<MockDragService> ds = mDragService;
  nsCOMPtr<nsIDragSession> currentDragSession;
  ds->GetCurrentSession(nullptr, getter_AddRefs(currentDragSession));
  MOZ_ASSERT(currentDragSession);
  return currentDragSession->EndDragSession(false /* doneDrag */,
                                            aKeyModifiers);
}
}  // namespace mozilla::test
