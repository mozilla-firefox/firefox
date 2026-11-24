/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <QuartzCore/QuartzCore.h>
#import <UIKit/UIEvent.h>
#import <UIKit/UIGraphics.h>
#import <UIKit/UIInterface.h>
#import <UIKit/UIKit.h>
#import <UIKit/UIScreen.h>
#import <UIKit/UITapGestureRecognizer.h>
#import <UIKit/UITouch.h>
#import <UIKit/UIView.h>
#import <UIKit/UIViewController.h>
#import <UIKit/UIWindow.h>

#include "ScreenHelperUIKit.h"
#include "nsAppShell.h"
#include "nsIAppWindow.h"
#include "nsIWindowWatcher.h"
#include "nsWindow.h"
#ifdef ACCESSIBILITY
#  include "mozilla/a11y/LocalAccessible.h"
#  include "nsAccessibilityService.h"
#endif

#include "nsGfxCIID.h"
#include "nsWidgetsCID.h"

#include "TextInputHandler.h"
#include "UIKitUtils.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "nsObjCExceptions.h"
#include "nsQueryObject.h"
#include "nsRegion.h"
#include "nsTArray.h"

#include "mozilla/BasicEvents.h"
#include "mozilla/EventForwards.h"
#include "mozilla/MacStringHelpers.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/NativeLayerCA.h"
#include "mozilla/widget/GeckoViewSupport.h"
#ifdef ACCESSIBILITY
#  include "mozilla/a11y/MUIRootAccessibleProtocol.h"
#endif

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::widget;
using mozilla::dom::Touch;
using mozilla::widget::UIKitUtils;

#define ALOG(args...)    \
  fprintf(stderr, args); \
  fprintf(stderr, "\n")

static LayoutDeviceIntPoint UIKitPointsToDevPixels(CGPoint aPoint,
                                                   CGFloat aBackingScale) {
  return LayoutDeviceIntPoint(NSToIntRound(aPoint.x * aBackingScale),
                              NSToIntRound(aPoint.y * aBackingScale));
}

static CGRect DevPixelsToUIKitPoints(const LayoutDeviceIntRect& aRect,
                                     CGFloat aBackingScale) {
  return CGRectMake((CGFloat)aRect.x / aBackingScale,
                    (CGFloat)aRect.y / aBackingScale,
                    (CGFloat)aRect.width / aBackingScale,
                    (CGFloat)aRect.height / aBackingScale);
}

// Used to retain a Cocoa object for the remainder of a method's execution.
class nsAutoRetainUIKitObject {
 public:
  explicit nsAutoRetainUIKitObject(id anObject) { mObject = [anObject retain]; }
  ~nsAutoRetainUIKitObject() { [mObject release]; }

 private:
  id mObject;  // [STRONG]
};

#ifdef ACCESSIBILITY
@interface ChildView : UIView <UIKeyInput, MUIRootAccessibleProtocol> {
#else
@interface ChildView : UIView <UIKeyInput> {
#endif
 @public
  nsWindow* mGeckoChild;  // weak ref
  BOOL mWaitingForPaint;
  NSMapTable<UITouch*, NSNumber*>* mTouches;
  int mNextTouchID;

  // The CALayer that wraps Gecko's rendered contents. It's a sublayer of
  // mPixelHostingView's backing layer. Always non-null.
  CALayer* mRootCALayer;  // [STRONG]

  // Whether we're inside updateRootCALayer at the moment.
  BOOL mIsUpdatingLayer;
}
// sets up our view, attaching it to its owning gecko view
- (id)initWithFrame:(CGRect)inFrame geckoChild:(nsWindow*)inChild;
// Our Gecko child was Destroy()ed
- (void)widgetDestroyed;
// Tear down this ChildView
- (void)delayedTearDown;
- (void)sendMouseEvent:(EventMessage)aType
                 point:(LayoutDeviceIntPoint)aPoint
                widget:(nsWindow*)aWindow;
- (void)handleTap:(UITapGestureRecognizer*)sender;
- (BOOL)isUsingMainThreadOpenGL;
- (void)drawUsingOpenGL;
- (void)drawUsingOpenGLCallback;
- (void)sendTouchEvent:(EventMessage)aType
               touches:(NSSet*)aTouches
                widget:(nsWindow*)aWindow;
// Event handling (UIResponder)
- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event;
- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event;
- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event;
- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event;
// Reacts to the view being resized.
- (void)layoutSubviews;

- (void)markLayerForDisplay;
- (CALayer*)rootCALayer;
- (void)updateRootCALayer;

- (void)activateWindow:(NSNotification*)notification;
- (void)deactivateWindow:(NSNotification*)notification;

#ifdef ACCESSIBILITY
// MUIRootAccessible
- (BOOL)hasRepresentedView;
- (id)representedView;

// MUIAccessible
- (BOOL)isAccessibilityElement;
- (NSString*)accessibilityLabel;
- (CGRect)accessibilityFrame;
- (NSString*)accessibilityValue;
- (uint64_t)accessibilityTraits;
- (NSInteger)accessibilityElementCount;
- (nullable id)accessibilityElementAtIndex:(NSInteger)index;
- (NSInteger)indexOfAccessibilityElement:(id)element;
- (NSArray* _Nullable)accessibilityElements;
- (UIAccessibilityContainerType)accessibilityContainerType;
#endif

@end

@implementation ChildView
- (id)initWithFrame:(CGRect)inFrame geckoChild:(nsWindow*)inChild {
  self.multipleTouchEnabled = YES;
  if ((self = [super initWithFrame:inFrame])) {
    mGeckoChild = inChild;

    mRootCALayer = [[CALayer layer] retain];
    mRootCALayer.position = CGPointZero;
    mRootCALayer.bounds = CGRectZero;
    mRootCALayer.anchorPoint = CGPointZero;
    mRootCALayer.contentsGravity = kCAGravityTopLeft;
    [[self layer] addSublayer:mRootCALayer];
  }
  ALOG("[ChildView[%p] initWithFrame:] (mGeckoChild = %p)", (void*)self,
       (void*)mGeckoChild);
  self.opaque = YES;
  self.alpha = 1.0;

  UITapGestureRecognizer* tapRecognizer =
      [[UITapGestureRecognizer alloc] initWithTarget:self
                                              action:@selector(handleTap:)];
  tapRecognizer.numberOfTapsRequired = 1;
  [self addGestureRecognizer:tapRecognizer];

  mTouches = [[NSMapTable alloc] init];
  mNextTouchID = 0;

  // This is managed with weak references by the notification center so that we
  // do not need to call removeObserver.
  // https://developer.apple.com/documentation/foundation/nsnotificationcenter/1415360-addobserver#discussion
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(activateWindow:)
             name:UIWindowDidBecomeKeyNotification
           object:nil];
  [[NSNotificationCenter defaultCenter]
      addObserver:self
         selector:@selector(deactivateWindow:)
             name:UIWindowDidResignKeyNotification
           object:nil];

  return self;
}

- (void)widgetDestroyed {
  mGeckoChild = nullptr;
  [mTouches release];
}

- (void)delayedTearDown {
  [self removeFromSuperview];
  [self release];
}

- (void)activateWindow:(NSNotification*)notification {
  ALOG("[[ChildView[%p] activateWindow]", (void*)self);

  if (!mGeckoChild) {
    return;
  }

  if (nsIWidgetListener* listener = mGeckoChild->GetWidgetListener()) {
    listener->WindowActivated();
  }
}

- (void)deactivateWindow:(NSNotification*)notification {
  ALOG("[[ChildView[%p] deactivateWindow]", (void*)self);

  if (!mGeckoChild) {
    return;
  }

  if (nsIWidgetListener* listener = mGeckoChild->GetWidgetListener()) {
    listener->WindowDeactivated();
  }
}

- (void)sendMouseEvent:(EventMessage)aType
                 point:(LayoutDeviceIntPoint)aPoint
                widget:(nsWindow*)aWindow {
  MOZ_DIAGNOSTIC_ASSERT(
      aType != eContextMenu,
      "eContextMenu event may need to be dispatched as WidgetPointerEvent");
  WidgetMouseEvent event(true, aType, aWindow, WidgetMouseEvent::eReal);

  event.mRefPoint = aPoint;
  event.mClickCount = 1;
  event.mButton = MouseButton::ePrimary;
  event.mInputSource = mozilla::dom::MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;

  aWindow->DispatchEvent(&event);
}

- (void)handleTap:(UITapGestureRecognizer*)sender {
  if (sender.state == UIGestureRecognizerStateEnded) {
    ALOG("[ChildView[%p] handleTap]", self);
    LayoutDeviceIntPoint lp = UIKitPointsToDevPixels(
        [sender locationInView:self], [self contentScaleFactor]);
    [self sendMouseEvent:eMouseMove point:lp widget:mGeckoChild];
    [self sendMouseEvent:eMouseDown point:lp widget:mGeckoChild];
    [self sendMouseEvent:eMouseUp point:lp widget:mGeckoChild];
  }
}

- (void)sendTouchEvent:(EventMessage)aType
               touches:(NSSet*)aTouches
                widget:(nsWindow*)aWindow {
  WidgetTouchEvent event(true, aType, aWindow);
  // XXX: I think nativeEvent.timestamp * 1000 is probably usable here but
  // I don't care that much right now.
  event.mTouches.SetCapacity(aTouches.count);
  for (UITouch* touch in aTouches) {
    LayoutDeviceIntPoint loc = UIKitPointsToDevPixels(
        [touch locationInView:self], [self contentScaleFactor]);
    LayoutDeviceIntPoint radius = UIKitPointsToDevPixels(
        CGPointMake([touch majorRadius], [touch majorRadius]),
        [self contentScaleFactor]);
    NSNumber* value = [mTouches objectForKey:touch];
    if (value == nil) {
      // This shouldn't happen.
      NS_ASSERTION(false, "Got a touch that we didn't know about");
      continue;
    }
    int id = [value intValue];
    auto t = MakeRefPtr<Touch>(id, loc, radius, 0.0f, 1.0f);
    event.mRefPoint = loc;
    event.mTouches.AppendElement(t);
  }
  aWindow->DispatchInputEvent(&event);
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  ALOG("[ChildView[%p] touchesBegan", self);
  if (!mGeckoChild) return;

  for (UITouch* touch : touches) {
    [mTouches setObject:[NSNumber numberWithInt:mNextTouchID] forKey:touch];
    mNextTouchID++;
  }
  [self sendTouchEvent:eTouchStart
               touches:[event allTouches]
                widget:mGeckoChild];
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  ALOG("[ChildView[%p] touchesCancelled", self);
  [self sendTouchEvent:eTouchCancel touches:touches widget:mGeckoChild];
  for (UITouch* touch : touches) {
    [mTouches removeObjectForKey:touch];
  }
  if (mTouches.count == 0) {
    mNextTouchID = 0;
  }
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  ALOG("[ChildView[%p] touchesEnded", self);
  if (!mGeckoChild) return;

  [self sendTouchEvent:eTouchEnd touches:touches widget:mGeckoChild];
  for (UITouch* touch : touches) {
    [mTouches removeObjectForKey:touch];
  }
  if (mTouches.count == 0) {
    mNextTouchID = 0;
  }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event {
  ALOG("[ChildView[%p] touchesMoved", self);
  if (!mGeckoChild) return;

  [self sendTouchEvent:eTouchMove
               touches:[event allTouches]
                widget:mGeckoChild];
}

- (void)layoutSubviews {
  ALOG("[ChildView[%p] layoutSubviews", self);
  if (!mGeckoChild ||
      mGeckoChild->GetWindowType() != nsIWidget::WindowType::TopLevel) {
    return;
  }

  CGFloat scaleFactor = [self contentScaleFactor];
  mGeckoChild->DoResize(self.frame.origin.x * scaleFactor,
                        self.frame.origin.y * scaleFactor,
                        self.frame.size.width * scaleFactor,
                        self.frame.size.height * scaleFactor, false);
}

- (BOOL)canBecomeFirstResponder {
  if (!mGeckoChild) {
    return NO;
  }

  if (mGeckoChild->IsVirtualKeyboardDisabled()) {
    return NO;
  }
  return YES;
}

- (void)setNeedsDisplayInRect:(CGRect)aRect {
  if ([self isUsingMainThreadOpenGL]) {
    // Draw without calling drawRect. This prevent us from
    // needing to access the normal window buffer surface unnecessarily, so we
    // waste less time synchronizing the two surfaces.
    if (!mWaitingForPaint) {
      mWaitingForPaint = YES;
      // Use NSRunLoopCommonModes instead of the default NSDefaultRunLoopMode
      // so that the timer also fires while a native menu is open.
      [self performSelector:@selector(drawUsingOpenGLCallback)
                 withObject:nil
                 afterDelay:0
                    inModes:[NSArray arrayWithObject:NSRunLoopCommonModes]];
    }
  }
}

- (void)markLayerForDisplay {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  if (!mIsUpdatingLayer) {
    // This call will cause updateRootCALayer to be called during the upcoming
    // main thread CoreAnimation transaction. It will also trigger a transaction
    // if no transaction is currently pending.
    [[self layer] setNeedsDisplay];
  }
}

- (void)updateRootCALayer {
  if (NS_IsMainThread() && mGeckoChild) {
    MOZ_RELEASE_ASSERT(!mIsUpdatingLayer, "Re-entrant layer display?");
    mIsUpdatingLayer = YES;
    mGeckoChild->HandleMainThreadCATransaction();
    mIsUpdatingLayer = NO;
  }
}

- (CALayer*)rootCALayer {
  return mRootCALayer;
}

- (BOOL)isUsingMainThreadOpenGL {
  if (!mGeckoChild || ![self window]) return NO;

  return NO;
}

- (void)drawUsingOpenGL {
  ALOG("drawUsingOpenGL");
  AUTO_PROFILER_LABEL("ChildView::drawUsingOpenGL", OTHER);

  if (!mGeckoChild->IsVisible()) return;

  mWaitingForPaint = NO;
  mGeckoChild->PaintWindow();
}

// Called asynchronously after setNeedsDisplay in order to avoid entering the
// normal drawing machinery.
- (void)drawUsingOpenGLCallback {
  if (mWaitingForPaint) {
    [self drawUsingOpenGL];
  }
}

// The display system has told us that a portion of our view is dirty. Tell
// gecko to paint it
- (void)drawRect:(CGRect)aRect {
  CGContextRef cgContext = UIGraphicsGetCurrentContext();
  [self drawRect:aRect inContext:cgContext];
}

- (void)drawRect:(CGRect)aRect inContext:(CGContextRef)aContext {
#ifdef DEBUG_UPDATE
  LayoutDeviceIntRect geckoBounds = mGeckoChild->GetBounds();

  fprintf(stderr,
          "---- Update[%p][%p] [%f %f %f %f] cgc: %p\n  gecko bounds: [%d %d "
          "%d %d]\n",
          self, mGeckoChild, aRect.origin.x, aRect.origin.y, aRect.size.width,
          aRect.size.height, aContext, geckoBounds.x, geckoBounds.y,
          geckoBounds.width, geckoBounds.height);

  CGAffineTransform xform = CGContextGetCTM(aContext);
  fprintf(stderr, "  xform in: [%f %f %f %f %f %f]\n", xform.a, xform.b,
          xform.c, xform.d, xform.tx, xform.ty);
#endif

  if (true) {
    // For Gecko-initiated repaints in OpenGL mode, drawUsingOpenGL is
    // directly called from a delayed perform callback - without going through
    // drawRect.
    // Paints that come through here are triggered by something that Cocoa
    // controls, for example by window resizing or window focus changes.

    // Do GL composition and return.
    [self drawUsingOpenGL];
    return;
  }
  AUTO_PROFILER_LABEL("ChildView::drawRect", OTHER);

  // Gecko content is painted through the compositor, so we have nothing to
  // draw here.
  if ([self isOpaque]) {
    // Gecko refused to draw, but we've claimed to be opaque, so we have to
    // draw something--fill with white.
    CGContextSetRGBFillColor(aContext, 1, 1, 1, 1);
    CGContextFillRect(aContext, aRect);
  }

#ifdef DEBUG_UPDATE
  fprintf(stderr, "---- update done ----\n");

#  if 0
  CGContextSetRGBStrokeColor (aContext,
                            ((((unsigned long)self) & 0xff)) / 255.0,
                            ((((unsigned long)self) & 0xff00) >> 8) / 255.0,
                            ((((unsigned long)self) & 0xff0000) >> 16) / 255.0,
                            0.5);
#  endif
  CGContextSetRGBStrokeColor(aContext, 1, 0, 0, 0.8);
  CGContextSetLineWidth(aContext, 4.0);
  CGContextStrokeRect(aContext, aRect);
#endif
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  [(ChildView*)[self superview] updateRootCALayer];
}

// UIKeyInput

- (void)insertText:(NSString*)text {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return;
  }
  widget::TextInputHandler* textInputHandler =
      mGeckoChild->GetTextInputHandler();
  if (!textInputHandler) {
    return;
  }
  textInputHandler->InsertText(text);
}

- (void)deleteBackward {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return;
  }
  widget::TextInputHandler* textInputHandler =
      mGeckoChild->GetTextInputHandler();
  if (!textInputHandler) {
    return;
  }
  textInputHandler->HandleCommand(Command::DeleteCharBackward);
}

- (BOOL)hasText {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return NO;
  }
  widget::InputContext context = mGeckoChild->GetInputContext();
  if (context.mIMEState.mEnabled == IMEEnabled::Disabled) {
    return NO;
  }
  return YES;
}

// UITextInputTraits

- (UIKeyboardType)keyboardType {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return UIKeyboardTypeDefault;
  }
  return UIKitUtils::GetUIKeyboardType(mGeckoChild->GetInputContext());
}

- (UIReturnKeyType)returnKeyType {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return UIReturnKeyDefault;
  }
  return UIKitUtils::GetUIReturnKeyType(mGeckoChild->GetInputContext());
}

- (UITextAutocapitalizationType)autocapitalizationType {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return UITextAutocapitalizationTypeNone;
  }
  return UIKitUtils::GetUITextAutocapitalizationType(
      mGeckoChild->GetInputContext());
}

- (UITextAutocorrectionType)autocorrectionType {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return UITextAutocorrectionTypeDefault;
  }

  return UIKitUtils::GetUITextAutocorrectionType(
      mGeckoChild->GetInputContext());
}

- (BOOL)isSecureTextEntry {
  if (!mGeckoChild || mGeckoChild->Destroyed()) {
    return NO;
  }
  if (mGeckoChild->GetInputContext().IsPasswordEditor()) {
    return YES;
  }
  return NO;
}

#ifdef ACCESSIBILITY
// MUIRootAccessible

- (id<MUIRootAccessibleProtocol>)accessible {
  if (!mGeckoChild) return nil;

  id<MUIRootAccessibleProtocol> nativeAccessible = nil;

  // nsAutoRetainCocoaObject kungFuDeathGrip(self);
  RefPtr<nsWindow> geckoChild(mGeckoChild);
  RefPtr<a11y::LocalAccessible> accessible = geckoChild->GetRootAccessible();
  if (!accessible) return nil;

  accessible->GetNativeInterface((void**)&nativeAccessible);

  return nativeAccessible;
}

- (BOOL)hasRepresentedView {
  return YES;
}

- (id)representedView {
  return self;
}

- (BOOL)isAccessibilityElement {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super isAccessibilityElement];
  }

  return [[self accessible] isAccessibilityElement];
}

- (NSString*)accessibilityLabel {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityLabel];
  }

  return [[self accessible] accessibilityLabel];
}

- (CGRect)accessibilityFrame {
  // Use the UIView implementation here. We rely on the position of this
  // frame to place gecko bounds in the right offset.
  return [super accessibilityFrame];
}

- (NSString*)accessibilityValue {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityValue];
  }

  return [[self accessible] accessibilityValue];
}

- (uint64_t)accessibilityTraits {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityTraits];
  }

  return [[self accessible] accessibilityTraits];
}

- (NSInteger)accessibilityElementCount {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityElementCount];
  }

  return [[self accessible] accessibilityElementCount];
}

- (nullable id)accessibilityElementAtIndex:(NSInteger)index {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityElementAtIndex:index];
  }

  return [[self accessible] accessibilityElementAtIndex:index];
}

- (NSInteger)indexOfAccessibilityElement:(id)element {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super indexOfAccessibilityElement:element];
  }

  return [[self accessible] indexOfAccessibilityElement:element];
}

- (NSArray* _Nullable)accessibilityElements {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityElements];
  }

  return [[self accessible] accessibilityElements];
}

- (UIAccessibilityContainerType)accessibilityContainerType {
  if (!mozilla::a11y::ShouldA11yBeEnabled()) {
    return [super accessibilityContainerType];
  }

  return [[self accessible] accessibilityContainerType];
}
#endif

@end

NS_IMPL_ISUPPORTS_INHERITED(nsWindow, nsIWidget, nsWindow);

nsWindow::nsWindow()
    : mNativeView(nullptr),
      mVisible(false),
      mSizeMode(nsSizeMode_Normal),
      mParent(nullptr) {}

nsWindow::~nsWindow() {
  [mNativeView widgetDestroyed];  // Safe if mNativeView is nil.
  TearDownView();                 // Safe if called twice.
}

void nsWindow::TearDownView() {
  if (!mNativeView) return;

  [mNativeView performSelectorOnMainThread:@selector(delayedTearDown)
                                withObject:nil
                             waitUntilDone:false];
  mNativeView = nil;
}

bool nsWindow::IsTopLevel() {
  return mWindowType == WindowType::TopLevel ||
         mWindowType == WindowType::Dialog ||
         mWindowType == WindowType::Invisible;
}

//
// nsIWidget
//

nsresult nsWindow::Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                          const widget::InitData& aInitData) {
  ALOG("nsWindow[%p]::Create %p [%d %d %d %d]", (void*)this, (void*)aParent,
       aRect.x, aRect.y, aRect.width, aRect.height);
  nsWindow* parent = (nsWindow*)aParent;

  mBounds = aRect;

  ALOG("nsWindow[%p]::Create bounds: %d %d %d %d", (void*)this, mBounds.x,
       mBounds.y, mBounds.width, mBounds.height);

  // Set defaults which can be overriden from aInitData in BaseCreate
  mWindowType = WindowType::TopLevel;
  mBorderStyle = BorderStyle::Default;

  nsIWidget::BaseCreate(aParent, aInitData);

  NS_ASSERTION(IsTopLevel() || parent,
               "non top level window doesn't have a parent!");

  mNativeView = [[ChildView alloc]
      initWithFrame:DevPixelsToUIKitPoints(mBounds, BackingScaleFactor())
         geckoChild:this];
  mNativeView.hidden = YES;

  if (parent) {
    parent->mChildren.AppendElement(this);
    mParent = parent;
  }

  if (parent && parent->mNativeView) {
    [parent->mNativeView addSubview:mNativeView];
  }

  CGFloat scaleFactor = [UIScreen mainScreen].scale;

  mNativeLayerRoot =
      NativeLayerRootCA::CreateForCALayer([mNativeView rootCALayer]);
  mNativeLayerRoot->SetBackingScale(scaleFactor);

  mTextInputHandler = new widget::TextInputHandler(this);

  return NS_OK;
}

void nsWindow::Destroy() {
  for (uint32_t i = 0; i < mChildren.Length(); ++i) {
    // why do we still have children?
    mChildren[i]->ClearParent();
  }

  if (mParent) mParent->mChildren.RemoveElement(this);

  if (mTextInputHandler) {
    mTextInputHandler->OnDestroyed();
  }
  mTextInputHandler = nullptr;

  [mNativeView widgetDestroyed];

  nsCOMPtr<nsIWidget> kungFuDeathGrip(this);

  nsIWidget::Destroy();

  // ReportDestroyEvent();

  TearDownView();

  nsIWidget::OnDestroy();
}

void nsWindow::Show(bool aState) {
  if (aState != mVisible) {
    mNativeView.hidden = aState ? NO : YES;
    if (aState) {
      if (mParent) {
        [mParent->mNativeView bringSubviewToFront:mNativeView];
      }
      [mNativeView setNeedsDisplay];
    }
    mVisible = aState;
  }
}

void nsWindow::Move(const DesktopPoint& aPoint) {
  if (!mNativeView || (mBounds.x == aPoint.x && mBounds.y == aPoint.y)) {
    return;
  }

  // XXX: handle this
  // The point we have is in Gecko coordinates (origin top-left). Convert
  // it to Cocoa ones (origin bottom-left).
  mBounds.x = aPoint.x;
  mBounds.y = aPoint.y;

  if (mWindowType != WindowType::TopLevel) {
    mNativeView.frame = DevPixelsToUIKitPoints(mBounds, BackingScaleFactor());
  }

  if (mVisible) [mNativeView setNeedsDisplay];

  ReportMoveEvent();
}

void nsWindow::Resize(const DesktopRect& aRect, bool aRepaint) {
  DoResize(aRect.x, aRect.y, aRect.width, aRect.height, aRepaint);
}

void nsWindow::DoResize(double aX, double aY, double aWidth, double aHeight,
                        bool aRepaint) {
  // FIXME: This code is confused about integers vs. double coords, and desktop
  // vs. device pixels.
  BOOL isMoving = mBounds.x != aX || mBounds.y != aY;
  BOOL isResizing = mBounds.width != aWidth || mBounds.height != aHeight;
  if (!mNativeView || (!isMoving && !isResizing)) {
    return;
  }

  if (isMoving) {
    mBounds.x = aX;
    mBounds.y = aY;
  }
  if (isResizing) {
    mBounds.width = aWidth;
    mBounds.height = aHeight;
  }

  if (mWindowType != WindowType::TopLevel) {
    [mNativeView
        setFrame:DevPixelsToUIKitPoints(mBounds, BackingScaleFactor())];
  }

  if (mVisible && aRepaint) [mNativeView setNeedsDisplay];

  if (isMoving) ReportMoveEvent();

  if (isResizing) ReportSizeEvent();
}

void nsWindow::Resize(const DesktopSize& aSize, bool aRepaint) {
  if (!mNativeView ||
      (mBounds.width == aSize.width && mBounds.height == aSize.height)) {
    return;
  }

  mBounds.width = aSize.width;
  mBounds.height = aSize.height;

  if (mWindowType != WindowType::TopLevel) {
    [mNativeView
        setFrame:DevPixelsToUIKitPoints(mBounds, BackingScaleFactor())];
  }

  if (mVisible && aRepaint) [mNativeView setNeedsDisplay];

  ReportSizeEvent();
}

void nsWindow::SetSizeMode(nsSizeMode aMode) {
  if (aMode == static_cast<int32_t>(mSizeMode)) {
    return;
  }

  // FIXME: Delegate this to our embedder.
  mSizeMode = static_cast<nsSizeMode>(aMode);
  if (aMode == nsSizeMode_Maximized || aMode == nsSizeMode_Fullscreen) {
    // Resize to fill screen
    nsIWidget::InfallibleMakeFullScreen(true);
  }
  ReportSizeModeEvent(aMode);
}

void nsWindow::Invalidate(const LayoutDeviceIntRect& aRect) {
  if (!mNativeView || !mVisible) return;

  [mNativeView setNeedsLayout];
  [mNativeView setNeedsDisplayInRect:DevPixelsToUIKitPoints(
                                         mBounds, BackingScaleFactor())];
}

void nsWindow::SetFocus(Raise, mozilla::dom::CallerType) {
  [[mNativeView window] makeKeyWindow];
  [mNativeView becomeFirstResponder];
}

void nsWindow::PaintWindow() {
  if (mWidgetListener) {
    mWidgetListener->PaintWindow(this);
  }
}

void nsWindow::ReportMoveEvent() { NotifyWindowMoved(mBounds.TopLeft()); }

void nsWindow::ReportSizeModeEvent(nsSizeMode aMode) {
  if (mWidgetListener) {
    // This is terrible.
    nsSizeMode theMode;
    switch (aMode) {
      case nsSizeMode_Maximized:
        theMode = nsSizeMode_Maximized;
        break;
      case nsSizeMode_Fullscreen:
        theMode = nsSizeMode_Fullscreen;
        break;
      default:
        return;
    }
    mWidgetListener->SizeModeChanged(theMode);
  }
}

void nsWindow::ReportSizeEvent() {
  LayoutDeviceIntRect innerBounds = GetClientBounds();

  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, innerBounds.Size());
  }

  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, innerBounds.Size());
  }
}

LayoutDeviceIntRect nsWindow::GetScreenBounds() {
  return LayoutDeviceIntRect(WidgetToScreenOffset(), mBounds.Size());
}

LayoutDeviceIntPoint nsWindow::WidgetToScreenOffset() {
  LayoutDeviceIntPoint offset(0, 0);
  if (mParent) {
    offset = mParent->WidgetToScreenOffset();
  }

  CGPoint temp = [mNativeView convertPoint:temp toView:nil];

  if (!mParent && mNativeView.window) {
    // convert to screen coords
    temp = [mNativeView.window convertPoint:temp toWindow:nil];
  }

  offset.x += static_cast<int32_t>(temp.x);
  offset.y += static_cast<int32_t>(temp.y);

  return offset;
}

void nsWindow::SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  const bool changingEnabledState =
      aContext.IsInputAttributeChanged(mInputContext);

  mInputContext = aContext;

  if (IsVirtualKeyboardDisabled()) {
    [mNativeView resignFirstResponder];
    return;
  }

  [mNativeView becomeFirstResponder];

  if (aAction.UserMightRequestOpenVKB() || changingEnabledState) {
    // TODO(m_kato):
    // It is unnecessary to call reloadInputViews with changingEnabledState if
    // virtual keyboard is disappeared.
    [mNativeView reloadInputViews];
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

widget::InputContext nsWindow::GetInputContext() {
  if (!mTextInputHandler) {
    InputContext context;
    context.mIMEState.mEnabled = IMEEnabled::Disabled;
    context.mIMEState.mOpen = IMEState::OPEN_STATE_NOT_SUPPORTED;
    return context;
  }
  return mInputContext;
}

widget::TextEventDispatcherListener*
nsWindow::GetNativeTextEventDispatcherListener() {
  return mTextInputHandler;
}

bool nsWindow::IsVirtualKeyboardDisabled() const {
  return mInputContext.mIMEState.mEnabled == IMEEnabled::Disabled ||
         mInputContext.mHTMLInputMode.EqualsLiteral("none");
}

void nsWindow::SetBackgroundColor(const nscolor& aColor) {
  mNativeView.backgroundColor = [UIColor colorWithRed:NS_GET_R(aColor)
                                                green:NS_GET_G(aColor)
                                                 blue:NS_GET_B(aColor)
                                                alpha:NS_GET_A(aColor)];
}

void* nsWindow::GetNativeData(uint32_t aDataType) {
  void* retVal = nullptr;

  switch (aDataType) {
    case NS_NATIVE_WIDGET:
      retVal = (void*)mNativeView;
      break;

    case NS_NATIVE_WINDOW:
      retVal = [mNativeView window];
      break;

    case NS_NATIVE_GRAPHIC:
      NS_ERROR("Requesting NS_NATIVE_GRAPHIC on a UIKit child view!");
      break;

    case NS_NATIVE_OFFSETX:
      retVal = 0;
      break;

    case NS_NATIVE_OFFSETY:
      retVal = 0;
      break;

    case NS_RAW_NATIVE_IME_CONTEXT:
      retVal = GetPseudoIMEContext();
      if (retVal) {
        break;
      }
      retVal = NS_ONLY_ONE_NATIVE_IME_CONTEXT;
      break;
  }

  return retVal;
}

CGFloat nsWindow::BackingScaleFactor() {
  if (mNativeView) {
    return [mNativeView contentScaleFactor];
  }
  return [UIScreen mainScreen].scale;
}

int32_t nsWindow::RoundsWidgetCoordinatesTo() {
  if (BackingScaleFactor() == 2.0) {
    return 2;
  }
  return 1;
}

layers::NativeLayerRoot* nsWindow::GetNativeLayerRoot() {
  return mNativeLayerRoot;
}

void nsWindow::HandleMainThreadCATransaction() {
  // Trigger a synchronous OMTC composite. This will call NextSurface and
  // NotifySurfaceReady on the compositor thread to update mNativeLayerRoot's
  // contents, and the main thread (this thread) will wait inside PaintWindow
  // during that time.
  PaintWindow();

  {
    // Apply the changes inside mNativeLayerRoot to the underlying CALayers. Now
    // is a good time to call this because we know we're currently inside a main
    // thread CATransaction, and the lock makes sure that no composition is
    // currently in progress, so we won't present half-composited state to the
    // screen.
    // TODO: We don't have a lock oops
    // MutexAutoLock lock(mCompositingLock);
    mNativeLayerRoot->CommitToScreen();
  }

  MaybeScheduleUnsuspendAsyncCATransactions();
}

// The following three methods are primarily an attempt to avoid glitches during
// window resizing.
// Here's some background on how these glitches come to be:
// CoreAnimation transactions are per-thread. They don't nest across threads.
// If you submit a transaction on the main thread and a transaction on a
// different thread, the two will race to the window server and show up on the
// screen in the order that they happen to arrive in at the window server.
// When the window size changes, there's another event that needs to be
// synchronized with: the window "shape" change. Cocoa has built-in
// synchronization mechanics that make sure that *main thread* window paints
// during window resizes are synchronized properly with the window shape change.
// But no such built-in synchronization exists for CATransactions that are
// triggered on a non-main thread. To cope with this, we define a "danger zone"
// during which we simply avoid triggering any CATransactions on a non-main
// thread (called "async" CATransactions here). This danger zone starts at the
// earliest opportunity at which we know about the size change, which is
// nsChildView::Resize, and ends at a point at which we know for sure that the
// paint has been handled completely, which is when we return to the event loop
// after layer display.
void nsWindow::SuspendAsyncCATransactions() {
  if (mUnsuspendAsyncCATransactionsRunnable) {
    mUnsuspendAsyncCATransactionsRunnable->Cancel();
    mUnsuspendAsyncCATransactionsRunnable = nullptr;
  }

  // Make sure that there actually will be a CATransaction on the main thread
  // during which we get a chance to schedule unsuspension. Otherwise we might
  // accidentally stay suspended indefinitely.
  [mNativeView markLayerForDisplay];

  mNativeLayerRoot->SuspendOffMainThreadCommits();
}

void nsWindow::MaybeScheduleUnsuspendAsyncCATransactions() {
  if (mNativeLayerRoot->AreOffMainThreadCommitsSuspended() &&
      !mUnsuspendAsyncCATransactionsRunnable) {
    mUnsuspendAsyncCATransactionsRunnable = NewCancelableRunnableMethod(
        "nsWindow::MaybeScheduleUnsuspendAsyncCATransactions", this,
        &nsWindow::UnsuspendAsyncCATransactions);
    NS_DispatchToMainThread(mUnsuspendAsyncCATransactionsRunnable);
  }
}

void nsWindow::UnsuspendAsyncCATransactions() {
  mUnsuspendAsyncCATransactionsRunnable = nullptr;

  if (mNativeLayerRoot->UnsuspendOffMainThreadCommits()) {
    // We need to call mNativeLayerRoot->CommitToScreen() at the next available
    // opportunity.
    // The easiest way to handle this request is to mark the layer as needing
    // display, because this will schedule a main thread CATransaction, during
    // which HandleMainThreadCATransaction will call CommitToScreen().
    [mNativeView markLayerForDisplay];
  }
}

EventDispatcher* nsWindow::GetEventDispatcher() const {
  if (mIOSView) {
    return mIOSView->mEventDispatcher;
  }
  return nullptr;
}

already_AddRefed<nsIWidget> nsIWidget::CreateTopLevelWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

already_AddRefed<nsIWidget> nsIWidget::CreateChildWindow() {
  nsCOMPtr<nsIWidget> window = new nsWindow();
  return window.forget();
}

/* static */
already_AddRefed<nsWindow> nsWindow::From(nsPIDOMWindowOuter* aDOMWindow) {
  nsCOMPtr<nsIWidget> widget = WidgetUtils::DOMWindowToWidget(aDOMWindow);
  return From(widget);
}

/* static */
already_AddRefed<nsWindow> nsWindow::From(nsIWidget* aWidget) {
  RefPtr<nsWindow> window = do_QueryObject(aWidget);
  return window.forget();
}

NS_IMPL_ISUPPORTS(IOSView, nsIGeckoViewEventDispatcher, nsIGeckoViewView)

nsresult IOSView::GetInitData(JSContext* aCx,
                              JS::MutableHandle<JS::Value> aOut) {
  return EventDispatcher::UnboxBundle(aCx, mInitData.get(), aOut);
}

@interface GeckoViewWindowImpl : NSObject <GeckoViewWindow> {
 @public
  RefPtr<nsWindow> mWindow;
  nsCOMPtr<nsPIDOMWindowOuter> mOuterWindow;
}

@end

@implementation GeckoViewWindowImpl
- (UIView*)view {
  return mWindow ? (UIView*)mWindow->GetNativeData(NS_NATIVE_WIDGET) : nil;
}

- (void)close {
  if (mWindow) {
    if (IOSView* iosView = mWindow->GetIOSView()) {
      iosView->mEventDispatcher->Detach();
    }
    mWindow = nullptr;
  }

  if (const nsCOMPtr<nsPIDOMWindowOuter> outerWin = std::move(mOuterWindow)) {
    MOZ_ASSERT(!mOuterWindow);
    outerWin->ForceClose();
  }
}
@end

id<GeckoViewWindow> GeckoViewOpenWindow(NSString* aId,
                                        id<SwiftEventDispatcher> aDispatcher,
                                        NSDictionary* aInitData,
                                        bool aPrivateMode) {
  MOZ_ASSERT(NS_IsMainThread());

  AUTO_PROFILER_LABEL("GeckoViewOpenWindows", OTHER);

  nsCOMPtr<nsIWindowWatcher> ww = do_GetService(NS_WINDOWWATCHER_CONTRACTID);
  MOZ_RELEASE_ASSERT(ww);

  nsAutoCString url;
  nsresult rv = Preferences::GetCString("toolkit.defaultChromeURI", url);
  if (NS_FAILED(rv)) {
    url = "chrome://geckoview/content/geckoview.xhtml"_ns;
  }

  // Prepare an nsIGeckoViewView to pass as argument to the window.
  auto iosView = MakeRefPtr<IOSView>();
  iosView->mEventDispatcher->Attach(aDispatcher);
  iosView->mInitData.AssignUnderGetRule((CFDictionaryRef)aInitData);

  nsAutoCString chromeFlags("chrome,dialog=0,remote,resizable");
  if (aPrivateMode) {
    chromeFlags += ",private";
  }

  nsAutoString windowId;
  CopyNSStringToXPCOMString(aId, windowId);

  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  ww->OpenWindow(nullptr, url, windowId, chromeFlags, iosView,
                 getter_AddRefs(domWindow));
  MOZ_RELEASE_ASSERT(domWindow);

  nsCOMPtr<nsPIDOMWindowOuter> pdomWindow = nsPIDOMWindowOuter::From(domWindow);
  const RefPtr<nsWindow> window = nsWindow::From(pdomWindow);
  MOZ_ASSERT(window);

  window->SetIOSView(iosView.forget());

  if (nsIWidgetListener* widgetListener = window->GetWidgetListener()) {
    nsCOMPtr<nsIAppWindow> appWindow(widgetListener->GetAppWindow());
    if (appWindow) {
      // Our window is not intrinsically sized, so tell AppWindow to
      // not set a size for us.
      appWindow->SetIntrinsicallySized(false);
    }
  }

  GeckoViewWindowImpl* gvWindow = [[GeckoViewWindowImpl alloc] init];
  gvWindow->mOuterWindow = pdomWindow;
  gvWindow->mWindow = window;
  return [gvWindow autorelease];
}
