/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <AVFoundation/AVFoundation.h>

#include <cmath>

#include "AppleUtils.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUtils.h"
#include "ImageRegion.h"
#include "nsClipboard.h"
#include "nsCocoaUtils.h"
#include "nsChildView.h"
#include "nsMenuBarX.h"
#include "nsCocoaWindow.h"
#include "nsCOMPtr.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIAppShellService.h"
#include "nsIOSPermissionRequest.h"
#include "nsIRunnable.h"
#include "nsIAppWindow.h"
#include "nsIBaseWindow.h"
#include "nsITransferable.h"
#include "nsMenuPopupFrame.h"
#include "nsMenuUtilsX.h"
#include "nsNetUtil.h"
#include "nsPrimitiveHelpers.h"
#include "nsToolkit.h"
#include "nsCRT.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/TextEvents.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_media.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/gfx/2D.h"

using namespace mozilla;
using namespace mozilla::widget;

using mozilla::dom::Promise;
using mozilla::gfx::DataSourceSurface;
using mozilla::gfx::DrawTarget;
using mozilla::gfx::IntPoint;
using mozilla::gfx::IntRect;
using mozilla::gfx::IntSize;
using mozilla::gfx::SamplingFilter;
using mozilla::gfx::SourceSurface;
using mozilla::gfx::SurfaceFormat;
using mozilla::image::ImageRegion;

LazyLogModule gCocoaUtilsLog("nsCocoaUtils");
#undef LOG
#define LOG(...) MOZ_LOG(gCocoaUtilsLog, LogLevel::Debug, (__VA_ARGS__))

/*
 * For each audio and video capture request, we hold an owning reference
 * to a promise to be resolved when the request's async callback is invoked.
 * sVideoCapturePromises and sAudioCapturePromises are arrays of video and
 * audio promises waiting to be resolved. Each array is protected by a
 * mutex.
 */
nsCocoaUtils::PromiseArray nsCocoaUtils::sVideoCapturePromises;
nsCocoaUtils::PromiseArray nsCocoaUtils::sAudioCapturePromises;
StaticMutex nsCocoaUtils::sMediaCaptureMutex;

/**
 * Pasteboard types
 */
NSString* const kPublicUrlPboardType = @"public.url";
NSString* const kPublicUrlNamePboardType = @"public.url-name";
NSString* const kPasteboardConcealedType = @"org.nspasteboard.ConcealedType";
NSString* const kUrlsWithTitlesPboardType = @"WebURLsWithTitlesPboardType";
NSString* const kMozWildcardPboardType = @"org.mozilla.MozillaWildcard";
NSString* const kMozCustomTypesPboardType = @"org.mozilla.custom-clipdata";
NSString* const kMozFileUrlsPboardType = @"org.mozilla.file-urls";

@implementation UTIHelper

+ (NSString*)stringFromPboardType:(NSString*)aType {
  if ([aType isEqualToString:kMozWildcardPboardType] ||
      [aType isEqualToString:kMozCustomTypesPboardType] ||
      [aType isEqualToString:kPasteboardConcealedType] ||
      [aType isEqualToString:kPublicUrlPboardType] ||
      [aType isEqualToString:kPublicUrlNamePboardType] ||
      [aType isEqualToString:kMozFileUrlsPboardType] ||
      [aType isEqualToString:(NSString*)kPasteboardTypeFileURLPromise] ||
      [aType isEqualToString:(NSString*)kPasteboardTypeFilePromiseContent] ||
      [aType isEqualToString:(NSString*)kUTTypeFileURL] ||
      [aType isEqualToString:NSStringPboardType] ||
      [aType isEqualToString:NSPasteboardTypeString] ||
      [aType isEqualToString:NSPasteboardTypeHTML] ||
      [aType isEqualToString:NSPasteboardTypeRTF] ||
      [aType isEqualToString:NSPasteboardTypeTIFF] ||
      [aType isEqualToString:NSPasteboardTypePNG]) {
    return [NSString stringWithString:aType];
  }
  NSString* dynamicType = (NSString*)UTTypeCreatePreferredIdentifierForTag(
      kUTTagClassNSPboardType, (CFStringRef)aType, kUTTypeData);
  NSString* result = [NSString stringWithString:dynamicType];
  [dynamicType release];
  return result;
}

@end  // UTIHelper

static float MenuBarScreenHeight() {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSArray* allScreens = [NSScreen screens];
  if ([allScreens count]) {
    return [[allScreens objectAtIndex:0] frame].size.height;
  }

  return 0.0;

  NS_OBJC_END_TRY_BLOCK_RETURN(0.0);
}

float nsCocoaUtils::FlippedScreenY(float y) {
  return MenuBarScreenHeight() - y;
}

NSRect nsCocoaUtils::GeckoRectToCocoaRect(const DesktopIntRect& geckoRect) {
  // We only need to change the Y coordinate by starting with the primary screen
  // height and subtracting the gecko Y coordinate of the bottom of the rect.
  return NSMakeRect(geckoRect.x, MenuBarScreenHeight() - geckoRect.YMost(),
                    geckoRect.width, geckoRect.height);
}

NSPoint nsCocoaUtils::GeckoPointToCocoaPoint(
    const mozilla::DesktopPoint& aPoint) {
  return NSMakePoint(aPoint.x, MenuBarScreenHeight() - aPoint.y);
}

NSRect nsCocoaUtils::GeckoRectToCocoaRectDevPix(
    const LayoutDeviceIntRect& aGeckoRect, CGFloat aBackingScale) {
  return NSMakeRect(aGeckoRect.x / aBackingScale,
                    MenuBarScreenHeight() - aGeckoRect.YMost() / aBackingScale,
                    aGeckoRect.width / aBackingScale,
                    aGeckoRect.height / aBackingScale);
}

DesktopIntRect nsCocoaUtils::CocoaRectToGeckoRect(const NSRect& cocoaRect) {
  // We only need to change the Y coordinate by starting with the primary screen
  // height and subtracting both the cocoa y origin and the height of the
  // cocoa rect.
  DesktopIntRect rect;
  rect.x = NSToIntRound(cocoaRect.origin.x);
  rect.y =
      NSToIntRound(FlippedScreenY(cocoaRect.origin.y + cocoaRect.size.height));
  rect.width = NSToIntRound(cocoaRect.origin.x + cocoaRect.size.width) - rect.x;
  rect.height = NSToIntRound(FlippedScreenY(cocoaRect.origin.y)) - rect.y;
  return rect;
}

LayoutDeviceIntRect nsCocoaUtils::CocoaRectToGeckoRectDevPix(
    const NSRect& aCocoaRect, CGFloat aBackingScale) {
  LayoutDeviceIntRect rect;
  rect.x = NSToIntRound(aCocoaRect.origin.x * aBackingScale);
  rect.y = NSToIntRound(
      FlippedScreenY(aCocoaRect.origin.y + aCocoaRect.size.height) *
      aBackingScale);
  rect.width = NSToIntRound((aCocoaRect.origin.x + aCocoaRect.size.width) *
                            aBackingScale) -
               rect.x;
  rect.height =
      NSToIntRound(FlippedScreenY(aCocoaRect.origin.y) * aBackingScale) -
      rect.y;
  return rect;
}

NSPoint nsCocoaUtils::ScreenLocationForEvent(NSEvent* anEvent) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Don't trust mouse locations of mouse move events, see bug 443178.
  // Likewise, on macOS 26 the locationInWindow of mouseEntered/mouseExited
  // events can be reported relative to a wrong window origin, which puts the
  // point off to the side by an amount that depends on the window's screen
  // position (Bug 2043963). The live cursor location is reliable, so use it.
  if (!anEvent || [anEvent type] == NSEventTypeMouseMoved ||
      [anEvent type] == NSEventTypeMouseEntered ||
      [anEvent type] == NSEventTypeMouseExited)
    return [NSEvent mouseLocation];

  // Pin momentum scroll events to the location of the last user-controlled
  // scroll event.
  if (IsMomentumScrollEvent(anEvent))
    return ChildViewMouseTracker::sLastScrollEventScreenLocation;

  return nsCocoaUtils::ConvertPointToScreen([anEvent window],
                                            [anEvent locationInWindow]);

  NS_OBJC_END_TRY_BLOCK_RETURN(NSMakePoint(0.0, 0.0));
}

BOOL nsCocoaUtils::IsEventOverWindow(NSEvent* anEvent, NSWindow* aWindow) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return NSPointInRect(ScreenLocationForEvent(anEvent), [aWindow frame]);

  NS_OBJC_END_TRY_BLOCK_RETURN(NO);
}

NSPoint nsCocoaUtils::EventLocationForWindow(NSEvent* anEvent,
                                             NSWindow* aWindow) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  return nsCocoaUtils::ConvertPointFromScreen(aWindow,
                                              ScreenLocationForEvent(anEvent));

  NS_OBJC_END_TRY_BLOCK_RETURN(NSMakePoint(0.0, 0.0));
}

BOOL nsCocoaUtils::IsMomentumScrollEvent(NSEvent* aEvent) {
  return [aEvent type] == NSEventTypeScrollWheel &&
         [aEvent momentumPhase] != NSEventPhaseNone;
}

BOOL nsCocoaUtils::EventHasPhaseInformation(NSEvent* aEvent) {
  return [aEvent phase] != NSEventPhaseNone ||
         [aEvent momentumPhase] != NSEventPhaseNone;
}

void nsCocoaUtils::HideOSChromeOnScreen(bool aShouldHide) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  // Keep track of how many hiding requests have been made, so that they can
  // be nested.
  static int sHiddenCount = 0;

  sHiddenCount += aShouldHide ? 1 : -1;
  NS_ASSERTION(sHiddenCount >= 0, "Unbalanced HideMenuAndDockForWindow calls");

  NSApplicationPresentationOptions options =
      sHiddenCount <= 0 ? NSApplicationPresentationDefault
                        : NSApplicationPresentationHideDock |
                              NSApplicationPresentationHideMenuBar;
  [NSApp setPresentationOptions:options];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#define NS_APPSHELLSERVICE_CONTRACTID "@mozilla.org/appshell/appShellService;1"
nsIWidget* nsCocoaUtils::GetHiddenWindowWidget() {
  nsCOMPtr<nsIAppShellService> appShell(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  if (!appShell) {
    NS_WARNING(
        "Couldn't get AppShellService in order to get hidden window ref");
    return nullptr;
  }

  nsCOMPtr<nsIAppWindow> hiddenWindow;
  appShell->GetHiddenWindow(getter_AddRefs(hiddenWindow));
  if (!hiddenWindow) {
    // Don't warn, this happens during shutdown, bug 358607.
    return nullptr;
  }

  nsCOMPtr<nsIBaseWindow> baseHiddenWindow;
  baseHiddenWindow = do_GetInterface(hiddenWindow);
  if (!baseHiddenWindow) {
    NS_WARNING("Couldn't get nsIBaseWindow from hidden window (nsIAppWindow)");
    return nullptr;
  }

  nsCOMPtr<nsIWidget> hiddenWindowWidget = baseHiddenWindow->GetMainWidget();
  if (!hiddenWindowWidget) {
    NS_WARNING("Couldn't get nsIWidget from hidden window (nsIBaseWindow)");
    return nullptr;
  }

  return hiddenWindowWidget;
}

BOOL nsCocoaUtils::WasLaunchedAtLogin() {
  ProcessSerialNumber processSerialNumber = {0, kCurrentProcess};
  ProcessInfoRec processInfoRec = {};
  processInfoRec.processInfoLength = sizeof(processInfoRec);

  // There is currently no replacement for ::GetProcessInformation, which has
  // been deprecated since macOS 10.9.
  if (::GetProcessInformation(&processSerialNumber, &processInfoRec) == noErr) {
    ProcessInfoRec parentProcessInfo = {};
    parentProcessInfo.processInfoLength = sizeof(parentProcessInfo);
    if (::GetProcessInformation(&processInfoRec.processLauncher,
                                &parentProcessInfo) == noErr) {
      return parentProcessInfo.processSignature == 'lgnw';
    }
  }
  return NO;
}

BOOL nsCocoaUtils::ShouldRestoreStateDueToLaunchAtLogin() {
  // Check if we were launched by macOS as a result of having
  // "Reopen windows..." selected during a restart.
  if (!WasLaunchedAtLogin()) {
    return NO;
  }

  CFStringRef lgnwPlistName = CFSTR("com.apple.loginwindow");
  CFStringRef saveStateKey = CFSTR("TALLogoutSavesState");
  auto lgnwPlist = CFTypeRefPtr<CFPropertyListRef>::WrapUnderCreateRule(
      ::CFPreferencesCopyAppValue(saveStateKey, lgnwPlistName));
  // The .plist doesn't exist unless the user changed the "Reopen windows..."
  // preference. If it doesn't exist, restore by default (as this is the macOS
  // default).
  // https://developer.apple.com/library/mac/documentation/macosx/conceptual/bpsystemstartup/chapters/CustomLogin.html
  if (!lgnwPlist) {
    return YES;
  }

  if (::CFGetTypeID(lgnwPlist.get()) != ::CFBooleanGetTypeID()) {
    return YES;
  }

  if (CFBooleanRef shouldRestoreState =
          static_cast<CFBooleanRef>(lgnwPlist.get())) {
    return ::CFBooleanGetValue(shouldRestoreState);
  }

  return NO;
}

static void data_ss_release_callback(void* aDataSourceSurface, const void* data,
                                     size_t size) {
  if (aDataSourceSurface) {
    static_cast<DataSourceSurface*>(aDataSourceSurface)->Unmap();
    static_cast<DataSourceSurface*>(aDataSourceSurface)->Release();
  }
}

// This function assumes little endian byte order.
static bool ComputeIsEntirelyBlack(const DataSourceSurface::MappedSurface& aMap,
                                   const IntSize& aSize) {
  for (int32_t y = 0; y < aSize.height; y++) {
    size_t rowStart = y * aMap.mStride;
    for (int32_t x = 0; x < aSize.width; x++) {
      size_t index = rowStart + x * 4;
      if (aMap.mData[index + 0] != 0 || aMap.mData[index + 1] != 0 ||
          aMap.mData[index + 2] != 0) {
        return false;
      }
    }
  }
  return true;
}

nsresult nsCocoaUtils::CreateCGImageFromSurface(SourceSurface* aSurface,
                                                CGImageRef* aResult,
                                                bool* aIsEntirelyBlack) {
  RefPtr<DataSourceSurface> dataSurface;

  if (aSurface->GetFormat() == SurfaceFormat::B8G8R8A8) {
    dataSurface = aSurface->GetDataSurface();
  } else {
    // CGImageCreate only supports 16- and 32-bit bit-depth
    // Convert format to SurfaceFormat::B8G8R8A8
    dataSurface = gfxUtils::CopySurfaceToDataSourceSurfaceWithFormat(
        aSurface, SurfaceFormat::B8G8R8A8);
  }

  NS_ENSURE_TRUE(dataSurface, NS_ERROR_FAILURE);

  int32_t width = dataSurface->GetSize().width;
  int32_t height = dataSurface->GetSize().height;
  if (height < 1 || width < 1) {
    return NS_ERROR_FAILURE;
  }

  DataSourceSurface::MappedSurface map;
  if (!dataSurface->Map(DataSourceSurface::MapType::READ, &map)) {
    return NS_ERROR_FAILURE;
  }
  // The Unmap() call happens in data_ss_release_callback

  if (aIsEntirelyBlack) {
    *aIsEntirelyBlack = ComputeIsEntirelyBlack(map, dataSurface->GetSize());
  }

  // Create a CGImageRef with the bits from the image, taking into account
  // the alpha ordering and endianness of the machine so we don't have to
  // touch the bits ourselves.
  CGDataProviderRef dataProvider = ::CGDataProviderCreateWithData(
      dataSurface.forget().take(), map.mData, map.mStride * height,
      data_ss_release_callback);
  CGColorSpaceRef colorSpace = ::CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  *aResult = ::CGImageCreate(
      width, height, 8, 32, map.mStride, colorSpace,
      kCGBitmapByteOrder32Host | kCGImageAlphaPremultipliedFirst, dataProvider,
      nullptr, 0, kCGRenderingIntentDefault);
  ::CGColorSpaceRelease(colorSpace);
  ::CGDataProviderRelease(dataProvider);
  return *aResult ? NS_OK : NS_ERROR_FAILURE;
}

nsresult nsCocoaUtils::CreateNSImageFromCGImage(CGImageRef aInputImage,
                                                NSImage** aResult) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Be very careful when creating the NSImage that the backing NSImageRep is
  // exactly 1:1 with the input image. On a retina display, both [NSImage
  // lockFocus] and [NSImage initWithCGImage:size:] will create an image with a
  // 2x backing NSImageRep. This prevents NSCursor from recognizing a retina
  // cursor, which only occurs if pixelsWide and pixelsHigh are exactly 2x the
  // size of the NSImage.
  //
  // For example, if a 32x32 SVG cursor is rendered on a retina display, then
  // aInputImage will be 64x64. The resulting NSImage will be scaled back down
  // to 32x32 so it stays the correct size on the screen by changing its size
  // (resizing a NSImage only scales the image and doesn't resample the data).
  // If aInputImage is converted using [NSImage initWithCGImage:size:] then the
  // bitmap will be 128x128 and NSCursor won't recognize a retina cursor, since
  // it will expect a 64x64 bitmap.

  int32_t width = ::CGImageGetWidth(aInputImage);
  int32_t height = ::CGImageGetHeight(aInputImage);
  NSRect imageRect = ::NSMakeRect(0.0, 0.0, width, height);

  NSBitmapImageRep* offscreenRep = [[NSBitmapImageRep alloc]
      initWithBitmapDataPlanes:nullptr
                    pixelsWide:width
                    pixelsHigh:height
                 bitsPerSample:8
               samplesPerPixel:4
                      hasAlpha:YES
                      isPlanar:NO
                colorSpaceName:NSDeviceRGBColorSpace
                  bitmapFormat:NSBitmapFormatAlphaFirst
                   bytesPerRow:0
                  bitsPerPixel:0];

  NSGraphicsContext* context =
      [NSGraphicsContext graphicsContextWithBitmapImageRep:offscreenRep];
  [NSGraphicsContext saveGraphicsState];
  [NSGraphicsContext setCurrentContext:context];

  // Get the Quartz context and draw.
  CGContextRef imageContext = [[NSGraphicsContext currentContext] CGContext];
  ::CGContextDrawImage(imageContext, *(CGRect*)&imageRect, aInputImage);

  [NSGraphicsContext restoreGraphicsState];

  *aResult = [[NSImage alloc] initWithSize:NSMakeSize(width, height)];
  [*aResult addRepresentation:offscreenRep];
  [offscreenRep release];
  return NS_OK;

  NS_OBJC_END_TRY_BLOCK_RETURN(NS_ERROR_FAILURE);
}

nsresult nsCocoaUtils::CreateNSImageFromImageContainer(
    imgIContainer* aImage, uint32_t aWhichFrame,
    const SVGImageContext* aSVGContext, const NSSize& aPreferredSize,
    NSImage** aResult, CGFloat scaleFactor, bool* aIsEntirelyBlack) {
  RefPtr<SourceSurface> surface;
  int32_t width = 0;
  int32_t height = 0;
  {
    const bool gotWidth = NS_SUCCEEDED(aImage->GetWidth(&width));
    const bool gotHeight = NS_SUCCEEDED(aImage->GetHeight(&height));
    if (auto ratio = aImage->GetIntrinsicRatio()) {
      if (gotWidth != gotHeight) {
        if (gotWidth) {
          height = ratio.Inverted().ApplyTo(width);
        } else {
          width = ratio.ApplyTo(height);
        }
      } else if (!gotWidth) {
        height = std::ceil(aPreferredSize.height);
        width = ratio.ApplyTo(height);
      }
    }
  }

  // Render a vector image at the correct resolution on a retina display
  if (aImage->GetType() == imgIContainer::TYPE_VECTOR) {
    IntSize scaledSize =
        IntSize::Ceil(width * scaleFactor, height * scaleFactor);

    RefPtr<DrawTarget> drawTarget =
        gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
            scaledSize, SurfaceFormat::B8G8R8A8);
    if (!drawTarget || !drawTarget->IsValid()) {
      NS_ERROR("Failed to create valid DrawTarget");
      return NS_ERROR_FAILURE;
    }

    gfxContext context(drawTarget);

    UniquePtr<SVGImageContext> svgContext;
    if (!aSVGContext) {
      svgContext = MakeUnique<SVGImageContext>();
      aSVGContext = svgContext.get();
    }

    mozilla::image::ImgDrawResult res = aImage->Draw(
        &context, scaledSize, ImageRegion::Create(scaledSize), aWhichFrame,
        SamplingFilter::POINT, *aSVGContext,
        imgIContainer::FLAG_SYNC_DECODE | imgIContainer::FLAG_ASYNC_NOTIFY,
        1.0);

    if (res != mozilla::image::ImgDrawResult::SUCCESS) {
      return NS_ERROR_FAILURE;
    }

    surface = drawTarget->Snapshot();
  } else {
    surface =
        aImage->GetFrame(aWhichFrame, imgIContainer::FLAG_SYNC_DECODE |
                                          imgIContainer::FLAG_ASYNC_NOTIFY);
  }

  NS_ENSURE_TRUE(surface, NS_ERROR_FAILURE);

  CGImageRef imageRef = nullptr;
  nsresult rv = nsCocoaUtils::CreateCGImageFromSurface(surface, &imageRef,
                                                       aIsEntirelyBlack);
  if (NS_FAILED(rv) || !imageRef) {
    return NS_ERROR_FAILURE;
  }

  rv = nsCocoaUtils::CreateNSImageFromCGImage(imageRef, aResult);
  ::CGImageRelease(imageRef);
  if (NS_FAILED(rv) || !*aResult) {
    return NS_ERROR_FAILURE;
  }

  // Ensure the image will be rendered the correct size on a retina display
  NSSize size = NSMakeSize(width, height);
  [*aResult setSize:size];
  [[[*aResult representations] objectAtIndex:0] setSize:size];
  return NS_OK;
}

nsresult nsCocoaUtils::CreateDualRepresentationNSImageFromImageContainer(
    imgIContainer* aImage, uint32_t aWhichFrame,
    const SVGImageContext* aSVGContext, const NSSize& aPreferredSize,
    NSImage** aResult, bool* aIsEntirelyBlack) {
  NSImage* newRepresentation = nil;
  nsresult rv = CreateNSImageFromImageContainer(
      aImage, aWhichFrame, aSVGContext, aPreferredSize, &newRepresentation,
      1.0f, aIsEntirelyBlack);
  if (NS_FAILED(rv) || !newRepresentation) {
    return NS_ERROR_FAILURE;
  }

  NSSize size = newRepresentation.size;
  *aResult = [[NSImage alloc] init];
  [*aResult setSize:size];

  [[[newRepresentation representations] objectAtIndex:0] setSize:size];
  [*aResult
      addRepresentation:[[newRepresentation representations] objectAtIndex:0]];
  [newRepresentation release];
  newRepresentation = nil;

  rv = CreateNSImageFromImageContainer(aImage, aWhichFrame, aSVGContext,
                                       aPreferredSize, &newRepresentation, 2.0f,
                                       aIsEntirelyBlack);
  if (NS_FAILED(rv) || !newRepresentation) {
    [*aResult release];
    *aResult = nil;
    return NS_ERROR_FAILURE;
  }

  [[[newRepresentation representations] objectAtIndex:0] setSize:size];
  [*aResult
      addRepresentation:[[newRepresentation representations] objectAtIndex:0]];
  [newRepresentation release];
  return NS_OK;
}

// static
NSURL* nsCocoaUtils::ToNSURL(const nsAString& aURLString) {
  nsAutoCString encodedURLString;
  nsresult rv = NS_GetSpecWithNSURLEncoding(encodedURLString,
                                            NS_ConvertUTF16toUTF8(aURLString));
  NS_ENSURE_SUCCESS(rv, nullptr);

  NSString* encodedURLNSString = ToNSString(encodedURLString);
  if (!encodedURLNSString) {
    return nullptr;
  }

  return [NSURL URLWithString:encodedURLNSString];
}

// static
void nsCocoaUtils::GeckoRectToNSRect(const nsIntRect& aGeckoRect,
                                     NSRect& aOutCocoaRect) {
  aOutCocoaRect.origin.x = aGeckoRect.x;
  aOutCocoaRect.origin.y = aGeckoRect.y;
  aOutCocoaRect.size.width = aGeckoRect.width;
  aOutCocoaRect.size.height = aGeckoRect.height;
}

// static
void nsCocoaUtils::NSRectToGeckoRect(const NSRect& aCocoaRect,
                                     nsIntRect& aOutGeckoRect) {
  aOutGeckoRect.x = NSToIntRound(aCocoaRect.origin.x);
  aOutGeckoRect.y = NSToIntRound(aCocoaRect.origin.y);
  aOutGeckoRect.width =
      NSToIntRound(aCocoaRect.origin.x + aCocoaRect.size.width) -
      aOutGeckoRect.x;
  aOutGeckoRect.height =
      NSToIntRound(aCocoaRect.origin.y + aCocoaRect.size.height) -
      aOutGeckoRect.y;
}

// static
NSEvent* nsCocoaUtils::MakeNewCocoaEventWithType(NSEventType aEventType,
                                                 NSEvent* aEvent) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSEvent* newEvent =
      [NSEvent keyEventWithType:aEventType
                             location:[aEvent locationInWindow]
                        modifierFlags:[aEvent modifierFlags]
                            timestamp:[aEvent timestamp]
                         windowNumber:[aEvent windowNumber]
                              context:nil
                           characters:[aEvent characters]
          charactersIgnoringModifiers:[aEvent charactersIgnoringModifiers]
                            isARepeat:[aEvent isARepeat]
                              keyCode:[aEvent keyCode]];
  return newEvent;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// static
NSEvent* nsCocoaUtils::MakeNewCocoaEventFromWidgetEvent(
    const WidgetKeyboardEvent& aKeyEvent, NSInteger aWindowNumber,
    NSGraphicsContext* aContext) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSEventType eventType;
  if (aKeyEvent.mMessage == eKeyUp) {
    eventType = NSEventTypeKeyUp;
  } else {
    eventType = NSEventTypeKeyDown;
  }

  static const uint32_t sModifierFlagMap[][2] = {
      {MODIFIER_SHIFT, NSEventModifierFlagShift},
      {MODIFIER_CONTROL, NSEventModifierFlagControl},
      {MODIFIER_ALT, NSEventModifierFlagOption},
      {MODIFIER_ALTGRAPH, NSEventModifierFlagOption},
      {MODIFIER_META, NSEventModifierFlagCommand},
      {MODIFIER_CAPSLOCK, NSEventModifierFlagCapsLock},
      {MODIFIER_NUMLOCK, NSEventModifierFlagNumericPad},
      {MODIFIER_FN, NSEventModifierFlagFunction}};

  NSUInteger modifierFlags = 0;
  for (uint32_t i = 0; i < std::size(sModifierFlagMap); ++i) {
    if (aKeyEvent.mModifiers & sModifierFlagMap[i][0]) {
      modifierFlags |= sModifierFlagMap[i][1];
    }
  }

  NSString* characters;
  if (aKeyEvent.mCharCode) {
    characters =
        [NSString stringWithCharacters:reinterpret_cast<const unichar*>(
                                           &(aKeyEvent.mCharCode))
                                length:1];
  } else {
    uint32_t cocoaCharCode =
        nsCocoaUtils::ConvertGeckoKeyCodeToMacCharCode(aKeyEvent.mKeyCode);
    characters = [NSString
        stringWithCharacters:reinterpret_cast<const unichar*>(&cocoaCharCode)
                      length:1];
  }

  return [NSEvent keyEventWithType:eventType
                          location:NSMakePoint(0, 0)
                     modifierFlags:modifierFlags
                         timestamp:0
                      windowNumber:aWindowNumber
                           context:aContext
                        characters:characters
       charactersIgnoringModifiers:characters
                         isARepeat:NO
                           keyCode:0];  // Native key code not currently needed

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// static
void nsCocoaUtils::InitInputEvent(WidgetInputEvent& aInputEvent,
                                  NSEvent* aNativeEvent) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  aInputEvent.mModifiers = ModifiersForEvent(aNativeEvent);
  aInputEvent.mTimeStamp = GetEventTimeStamp([aNativeEvent timestamp]);

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

// static
Modifiers nsCocoaUtils::ModifiersForEvent(NSEvent* aNativeEvent) {
  NSUInteger modifiers =
      aNativeEvent ? [aNativeEvent modifierFlags] : [NSEvent modifierFlags];
  Modifiers result = 0;
  if (modifiers & NSEventModifierFlagShift) {
    result |= MODIFIER_SHIFT;
  }
  if (modifiers & NSEventModifierFlagControl) {
    result |= MODIFIER_CONTROL;
  }
  if (modifiers & NSEventModifierFlagOption) {
    result |= MODIFIER_ALT;
    // Mac's option key is similar to other platforms' AltGr key.
    // Let's set AltGr flag when option key is pressed for consistency with
    // other platforms.
    result |= MODIFIER_ALTGRAPH;
  }
  if (modifiers & NSEventModifierFlagCommand) {
    result |= MODIFIER_META;
  }

  if (modifiers & NSEventModifierFlagCapsLock) {
    result |= MODIFIER_CAPSLOCK;
  }
  // Mac doesn't have NumLock key.  We can assume that NumLock is always locked
  // if user is using a keyboard which has numpad.  Otherwise, if user is using
  // a keyboard which doesn't have numpad, e.g., MacBook's keyboard, we can
  // assume that NumLock is always unlocked.
  // Unfortunately, we cannot know whether current keyboard has numpad or not.
  // We should notify locked state only when keys in numpad are pressed.
  // By this, web applications may not be confused by unexpected numpad key's
  // key event with unlocked state.
  if (modifiers & NSEventModifierFlagNumericPad) {
    result |= MODIFIER_NUMLOCK;
  }

  // Be aware, NSEventModifierFlagFunction is also set on the native event when
  // arrow keys, the home key or some other keys are pressed. We cannot check
  // whether the 'fn' key is pressed or not by the flag alone. We need to check
  // that the event's keyCode falls outside the range of keys that will also set
  // the function modifier.
  if (!!(modifiers & NSEventModifierFlagFunction) &&
      (aNativeEvent.type == NSEventTypeKeyDown ||
       aNativeEvent.type == NSEventTypeKeyUp ||
       aNativeEvent.type == NSEventTypeFlagsChanged) &&
      !(kVK_Return <= aNativeEvent.keyCode &&
        aNativeEvent.keyCode <= NSModeSwitchFunctionKey)) {
    result |= MODIFIER_FN;
  }

  return result;
}

// static
UInt32 nsCocoaUtils::ConvertToCarbonModifier(NSUInteger aCocoaModifier) {
  UInt32 carbonModifier = 0;
  if (aCocoaModifier & NSEventModifierFlagCapsLock) {
    carbonModifier |= alphaLock;
  }
  if (aCocoaModifier & NSEventModifierFlagControl) {
    carbonModifier |= controlKey;
  }
  if (aCocoaModifier & NSEventModifierFlagOption) {
    carbonModifier |= optionKey;
  }
  if (aCocoaModifier & NSEventModifierFlagShift) {
    carbonModifier |= shiftKey;
  }
  if (aCocoaModifier & NSEventModifierFlagCommand) {
    carbonModifier |= cmdKey;
  }
  if (aCocoaModifier & NSEventModifierFlagNumericPad) {
    carbonModifier |= kEventKeyModifierNumLockMask;
  }
  if (aCocoaModifier & NSEventModifierFlagFunction) {
    carbonModifier |= kEventKeyModifierFnMask;
  }
  return carbonModifier;
}

// While HiDPI support is not 100% complete and tested, we'll have a pref
// to allow it to be turned off in case of problems (or for testing purposes).

// gfx.hidpi.enabled is an integer with the meaning:
//    <= 0 : HiDPI support is disabled
//       1 : HiDPI enabled provided all screens have the same backing resolution
//     > 1 : HiDPI enabled even if there are a mixture of screen modes

// All the following code is to be removed once HiDPI work is more complete.

static bool sHiDPIEnabled = false;
static bool sHiDPIPrefInitialized = false;

// static
bool nsCocoaUtils::HiDPIEnabled() {
  if (!sHiDPIPrefInitialized) {
    sHiDPIPrefInitialized = true;

    int prefSetting = Preferences::GetInt("gfx.hidpi.enabled", 1);
    if (prefSetting <= 0) {
      return false;
    }

    // prefSetting is at least 1, need to check attached screens...

    int scaleFactors = 0;  // used as a bitset to track the screen types found
    NSEnumerator* screenEnum = [[NSScreen screens] objectEnumerator];
    while (NSScreen* screen = [screenEnum nextObject]) {
      NSDictionary* desc = [screen deviceDescription];
      if ([desc objectForKey:NSDeviceIsScreen] == nil) {
        continue;
      }
      // Currently, we only care about differentiating "1.0" and "2.0",
      // so we set one of the two low bits to record which.
      if ([screen backingScaleFactor] > 1.0) {
        scaleFactors |= 2;
      } else {
        scaleFactors |= 1;
      }
    }

    // Now scaleFactors will be:
    //   0 if no screens (supporting backingScaleFactor) found
    //   1 if only lo-DPI screens
    //   2 if only hi-DPI screens
    //   3 if both lo- and hi-DPI screens
    // We'll enable HiDPI support if there's only a single screen type,
    // OR if the pref setting is explicitly greater than 1.
    sHiDPIEnabled = (scaleFactors <= 2) || (prefSetting > 1);
  }

  return sHiDPIEnabled;
}

// static
void nsCocoaUtils::InvalidateHiDPIState() { sHiDPIPrefInitialized = false; }

void nsCocoaUtils::GetCommandsFromKeyEvent(
    NSEvent* aEvent, nsTArray<KeyBindingsCommand>& aCommands) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  MOZ_ASSERT(aEvent);

  static NativeKeyBindingsRecorder* sNativeKeyBindingsRecorder;
  if (!sNativeKeyBindingsRecorder) {
    sNativeKeyBindingsRecorder = [NativeKeyBindingsRecorder new];
  }

  [sNativeKeyBindingsRecorder startRecording:aCommands];

  // This will trigger 0 - N calls to doCommandBySelector: and insertText:
  [sNativeKeyBindingsRecorder
      interpretKeyEvents:[NSArray arrayWithObject:aEvent]];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

@implementation NativeKeyBindingsRecorder

- (void)startRecording:(nsTArray<KeyBindingsCommand>&)aCommands {
  mCommands = &aCommands;
  mCommands->Clear();
}

- (void)doCommandBySelector:(SEL)aSelector {
  KeyBindingsCommand command = {aSelector, nil};

  mCommands->AppendElement(command);
}

- (void)insertText:(id)aString {
  KeyBindingsCommand command = {@selector(insertText:), aString};

  mCommands->AppendElement(command);
}

@end  // NativeKeyBindingsRecorder

struct KeyConversionData {
  const char* str;
  size_t strLength;
  uint32_t geckoKeyCode;
  uint32_t charCode;
};

static const KeyConversionData gKeyConversions[] = {

#define KEYCODE_ENTRY(aStr, aCode) {#aStr, sizeof(#aStr) - 1, NS_##aStr, aCode}

// Some keycodes may have different name in KeyboardEvent from its key name.
#define KEYCODE_ENTRY2(aStr, aNSName, aCode) \
  {#aStr, sizeof(#aStr) - 1, NS_##aNSName, aCode}

    KEYCODE_ENTRY(VK_CANCEL, 0x001B),
    KEYCODE_ENTRY(VK_DELETE, NSDeleteFunctionKey),
    KEYCODE_ENTRY(VK_BACK, NSBackspaceCharacter),
    KEYCODE_ENTRY2(VK_BACK_SPACE, VK_BACK, NSBackspaceCharacter),
    KEYCODE_ENTRY(VK_TAB, NSTabCharacter),
    KEYCODE_ENTRY(VK_CLEAR, NSClearLineFunctionKey),
    KEYCODE_ENTRY(VK_RETURN, NSEnterCharacter),
    KEYCODE_ENTRY(VK_SHIFT, 0),
    KEYCODE_ENTRY(VK_CONTROL, 0),
    KEYCODE_ENTRY(VK_ALT, 0),
    KEYCODE_ENTRY(VK_PAUSE, NSPauseFunctionKey),
    KEYCODE_ENTRY(VK_CAPS_LOCK, 0),
    KEYCODE_ENTRY(VK_ESCAPE, 0),
    KEYCODE_ENTRY(VK_SPACE, ' '),
    KEYCODE_ENTRY(VK_PAGE_UP, NSPageUpFunctionKey),
    KEYCODE_ENTRY(VK_PAGE_DOWN, NSPageDownFunctionKey),
    KEYCODE_ENTRY(VK_END, NSEndFunctionKey),
    KEYCODE_ENTRY(VK_HOME, NSHomeFunctionKey),
    KEYCODE_ENTRY(VK_LEFT, NSLeftArrowFunctionKey),
    KEYCODE_ENTRY(VK_UP, NSUpArrowFunctionKey),
    KEYCODE_ENTRY(VK_RIGHT, NSRightArrowFunctionKey),
    KEYCODE_ENTRY(VK_DOWN, NSDownArrowFunctionKey),
    KEYCODE_ENTRY(VK_PRINTSCREEN, NSPrintScreenFunctionKey),
    KEYCODE_ENTRY(VK_INSERT, NSInsertFunctionKey),
    KEYCODE_ENTRY(VK_HELP, NSHelpFunctionKey),
    KEYCODE_ENTRY(VK_0, '0'),
    KEYCODE_ENTRY(VK_1, '1'),
    KEYCODE_ENTRY(VK_2, '2'),
    KEYCODE_ENTRY(VK_3, '3'),
    KEYCODE_ENTRY(VK_4, '4'),
    KEYCODE_ENTRY(VK_5, '5'),
    KEYCODE_ENTRY(VK_6, '6'),
    KEYCODE_ENTRY(VK_7, '7'),
    KEYCODE_ENTRY(VK_8, '8'),
    KEYCODE_ENTRY(VK_9, '9'),
    KEYCODE_ENTRY(VK_SEMICOLON, ':'),
    KEYCODE_ENTRY(VK_EQUALS, '='),
    KEYCODE_ENTRY(VK_A, 'A'),
    KEYCODE_ENTRY(VK_B, 'B'),
    KEYCODE_ENTRY(VK_C, 'C'),
    KEYCODE_ENTRY(VK_D, 'D'),
    KEYCODE_ENTRY(VK_E, 'E'),
    KEYCODE_ENTRY(VK_F, 'F'),
    KEYCODE_ENTRY(VK_G, 'G'),
    KEYCODE_ENTRY(VK_H, 'H'),
    KEYCODE_ENTRY(VK_I, 'I'),
    KEYCODE_ENTRY(VK_J, 'J'),
    KEYCODE_ENTRY(VK_K, 'K'),
    KEYCODE_ENTRY(VK_L, 'L'),
    KEYCODE_ENTRY(VK_M, 'M'),
    KEYCODE_ENTRY(VK_N, 'N'),
    KEYCODE_ENTRY(VK_O, 'O'),
    KEYCODE_ENTRY(VK_P, 'P'),
    KEYCODE_ENTRY(VK_Q, 'Q'),
    KEYCODE_ENTRY(VK_R, 'R'),
    KEYCODE_ENTRY(VK_S, 'S'),
    KEYCODE_ENTRY(VK_T, 'T'),
    KEYCODE_ENTRY(VK_U, 'U'),
    KEYCODE_ENTRY(VK_V, 'V'),
    KEYCODE_ENTRY(VK_W, 'W'),
    KEYCODE_ENTRY(VK_X, 'X'),
    KEYCODE_ENTRY(VK_Y, 'Y'),
    KEYCODE_ENTRY(VK_Z, 'Z'),
    KEYCODE_ENTRY(VK_CONTEXT_MENU, NSMenuFunctionKey),
    KEYCODE_ENTRY(VK_NUMPAD0, '0'),
    KEYCODE_ENTRY(VK_NUMPAD1, '1'),
    KEYCODE_ENTRY(VK_NUMPAD2, '2'),
    KEYCODE_ENTRY(VK_NUMPAD3, '3'),
    KEYCODE_ENTRY(VK_NUMPAD4, '4'),
    KEYCODE_ENTRY(VK_NUMPAD5, '5'),
    KEYCODE_ENTRY(VK_NUMPAD6, '6'),
    KEYCODE_ENTRY(VK_NUMPAD7, '7'),
    KEYCODE_ENTRY(VK_NUMPAD8, '8'),
    KEYCODE_ENTRY(VK_NUMPAD9, '9'),
    KEYCODE_ENTRY(VK_MULTIPLY, '*'),
    KEYCODE_ENTRY(VK_ADD, '+'),
    KEYCODE_ENTRY(VK_SEPARATOR, 0),
    KEYCODE_ENTRY(VK_SUBTRACT, '-'),
    KEYCODE_ENTRY(VK_DECIMAL, '.'),
    KEYCODE_ENTRY(VK_DIVIDE, '/'),
    KEYCODE_ENTRY(VK_F1, NSF1FunctionKey),
    KEYCODE_ENTRY(VK_F2, NSF2FunctionKey),
    KEYCODE_ENTRY(VK_F3, NSF3FunctionKey),
    KEYCODE_ENTRY(VK_F4, NSF4FunctionKey),
    KEYCODE_ENTRY(VK_F5, NSF5FunctionKey),
    KEYCODE_ENTRY(VK_F6, NSF6FunctionKey),
    KEYCODE_ENTRY(VK_F7, NSF7FunctionKey),
    KEYCODE_ENTRY(VK_F8, NSF8FunctionKey),
    KEYCODE_ENTRY(VK_F9, NSF9FunctionKey),
    KEYCODE_ENTRY(VK_F10, NSF10FunctionKey),
    KEYCODE_ENTRY(VK_F11, NSF11FunctionKey),
    KEYCODE_ENTRY(VK_F12, NSF12FunctionKey),
    KEYCODE_ENTRY(VK_F13, NSF13FunctionKey),
    KEYCODE_ENTRY(VK_F14, NSF14FunctionKey),
    KEYCODE_ENTRY(VK_F15, NSF15FunctionKey),
    KEYCODE_ENTRY(VK_F16, NSF16FunctionKey),
    KEYCODE_ENTRY(VK_F17, NSF17FunctionKey),
    KEYCODE_ENTRY(VK_F18, NSF18FunctionKey),
    KEYCODE_ENTRY(VK_F19, NSF19FunctionKey),
    KEYCODE_ENTRY(VK_F20, NSF20FunctionKey),
    KEYCODE_ENTRY(VK_F21, NSF21FunctionKey),
    KEYCODE_ENTRY(VK_F22, NSF22FunctionKey),
    KEYCODE_ENTRY(VK_F23, NSF23FunctionKey),
    KEYCODE_ENTRY(VK_F24, NSF24FunctionKey),
    KEYCODE_ENTRY(VK_NUM_LOCK, NSClearLineFunctionKey),
    KEYCODE_ENTRY(VK_SCROLL_LOCK, NSScrollLockFunctionKey),
    KEYCODE_ENTRY(VK_COMMA, ','),
    KEYCODE_ENTRY(VK_PERIOD, '.'),
    KEYCODE_ENTRY(VK_SLASH, '/'),
    KEYCODE_ENTRY(VK_BACK_QUOTE, '`'),
    KEYCODE_ENTRY(VK_OPEN_BRACKET, '['),
    KEYCODE_ENTRY(VK_BACK_SLASH, '\\'),
    KEYCODE_ENTRY(VK_CLOSE_BRACKET, ']'),
    KEYCODE_ENTRY(VK_QUOTE, '\'')

#undef KEYCODE_ENTRY

};

uint32_t nsCocoaUtils::ConvertGeckoNameToMacCharCode(
    const nsAString& aKeyCodeName) {
  if (aKeyCodeName.IsEmpty()) {
    return 0;
  }

  nsAutoCString keyCodeName;
  LossyCopyUTF16toASCII(aKeyCodeName, keyCodeName);
  // We want case-insensitive comparison with data stored as uppercase.
  ToUpperCase(keyCodeName);

  uint32_t keyCodeNameLength = keyCodeName.Length();
  const char* keyCodeNameStr = keyCodeName.get();
  for (uint16_t i = 0; i < std::size(gKeyConversions); ++i) {
    if (keyCodeNameLength == gKeyConversions[i].strLength &&
        nsCRT::strcmp(gKeyConversions[i].str, keyCodeNameStr) == 0) {
      return gKeyConversions[i].charCode;
    }
  }

  return 0;
}

uint32_t nsCocoaUtils::ConvertGeckoKeyCodeToMacCharCode(uint32_t aKeyCode) {
  if (!aKeyCode) {
    return 0;
  }

  for (uint16_t i = 0; i < std::size(gKeyConversions); ++i) {
    if (gKeyConversions[i].geckoKeyCode == aKeyCode) {
      return gKeyConversions[i].charCode;
    }
  }

  return 0;
}

NSEventModifierFlags nsCocoaUtils::ConvertWidgetModifiersToMacModifierFlags(
    nsIWidget::NativeModifiers aNativeModifiers) {
  if (aNativeModifiers == nsIWidget::NativeModifiers::NO_MODIFIERS) {
    return 0;
  }
  struct ModifierFlagMapEntry {
    nsIWidget::NativeModifiers mWidgetModifier;
    NSEventModifierFlags mModifierFlags;
  };
  static constexpr ModifierFlagMapEntry sModifierFlagMap[] = {
      {nsIWidget::NativeModifiers::CAPS_LOCK, NSEventModifierFlagCapsLock},
      {nsIWidget::NativeModifiers::SHIFT_L, NSEventModifierFlagShift | 0x0002},
      {nsIWidget::NativeModifiers::SHIFT_R, NSEventModifierFlagShift | 0x0004},
      {nsIWidget::NativeModifiers::CTRL_L, NSEventModifierFlagControl | 0x0001},
      {nsIWidget::NativeModifiers::CTRL_R, NSEventModifierFlagControl | 0x2000},
      {nsIWidget::NativeModifiers::ALT_L, NSEventModifierFlagOption | 0x0020},
      {nsIWidget::NativeModifiers::ALT_R, NSEventModifierFlagOption | 0x0040},
      {nsIWidget::NativeModifiers::COMMAND_L,
       NSEventModifierFlagCommand | 0x0008},
      {nsIWidget::NativeModifiers::COMMAND_R,
       NSEventModifierFlagCommand | 0x0010},
      {nsIWidget::NativeModifiers::NUMERIC_KEY_PAD,
       NSEventModifierFlagNumericPad},
      {nsIWidget::NativeModifiers::HELP, NSEventModifierFlagHelp},
      {nsIWidget::NativeModifiers::FUNCTION, NSEventModifierFlagFunction}};

  NSEventModifierFlags modifierFlags = 0;
  for (const ModifierFlagMapEntry& entry : sModifierFlagMap) {
    if ((aNativeModifiers & entry.mWidgetModifier) !=
        nsIWidget::NativeModifiers::NO_MODIFIERS) {
      modifierFlags |= entry.mModifierFlags;
    }
  }
  return modifierFlags;
}

mozilla::MouseButton nsCocoaUtils::ButtonForEvent(NSEvent* aEvent) {
  switch (aEvent.type) {
    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeLeftMouseUp:
      return MouseButton::ePrimary;
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeRightMouseUp:
      return MouseButton::eSecondary;
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseDragged:
    case NSEventTypeOtherMouseUp:
      switch (aEvent.buttonNumber) {
        case 3:
          return MouseButton::eX1;
        case 4:
          return MouseButton::eX2;
        default:
          // The middle button usually has button 2, but if this is a
          // synthesized event (for which you cannot specify a buttonNumber),
          // then the button will be 0. Treat all remaining OtherMouse events as
          // the middle button.
          return MouseButton::eMiddle;
      }
    default:
      // Treat non-mouse events as the primary mouse button.
      return MouseButton::ePrimary;
  }
}

NSMutableAttributedString* nsCocoaUtils::GetNSMutableAttributedString(
    const nsAString& aText, const nsTArray<mozilla::FontRange>& aFontRanges,
    const bool aIsVertical, const CGFloat aBackingScaleFactor) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN

  NSString* nsstr = nsCocoaUtils::ToNSString(aText);
  NSMutableAttributedString* attrStr =
      [[[NSMutableAttributedString alloc] initWithString:nsstr
                                              attributes:nil] autorelease];

  int32_t lastOffset = aText.Length();
  for (auto i = aFontRanges.Length(); i > 0; --i) {
    const FontRange& fontRange = aFontRanges[i - 1];
    NSString* fontName = nsCocoaUtils::ToNSString(fontRange.mFontName);
    CGFloat fontSize = fontRange.mFontSize / aBackingScaleFactor;
    NSFont* font = [NSFont fontWithName:fontName size:fontSize];
    if (!font) {
      font = [NSFont systemFontOfSize:fontSize];
    }

    NSDictionary* attrs = @{NSFontAttributeName : font};
    NSRange range = NSMakeRange(fontRange.mStartOffset,
                                lastOffset - fontRange.mStartOffset);
    [attrStr setAttributes:attrs range:range];
    lastOffset = fontRange.mStartOffset;
  }

  if (aIsVertical) {
    [attrStr addAttribute:NSVerticalGlyphFormAttributeName
                    value:[NSNumber numberWithInt:1]
                    range:NSMakeRange(0, [attrStr length])];
  }

  return attrStr;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil)
}

TimeStamp nsCocoaUtils::GetEventTimeStamp(NSTimeInterval aEventTime) {
  if (!aEventTime) {
    // If the event is generated by a 3rd party application, its timestamp
    // may be 0.  In this case, just return current timestamp.
    // XXX Should we cache last event time?
    return TimeStamp::Now();
  }
  // The internal value of the macOS implementation of TimeStamp is based on
  // mach_absolute_time(), which measures "ticks" since boot.
  // Event timestamps are NSTimeIntervals (seconds) since boot. So the two time
  // representations already have the same base; we only need to convert
  // seconds into ticks.
  int64_t tick =
      BaseTimeDurationPlatformUtils::TicksFromMilliseconds(aEventTime * 1000.0);
  return TimeStamp::FromSystemTime(tick);
}

static NSString* ActionOnDoubleClickSystemPref() {
  NSUserDefaults* userDefaults = [NSUserDefaults standardUserDefaults];
  NSString* kAppleActionOnDoubleClickKey = @"AppleActionOnDoubleClick";
  id value = [userDefaults objectForKey:kAppleActionOnDoubleClickKey];
  if ([value isKindOfClass:[NSString class]]) {
    return value;
  }
  return nil;
}

@interface NSWindow (NSWindowShouldZoomOnDoubleClick)
+ (BOOL)_shouldZoomOnDoubleClick;  // present on 10.7 and above
@end

bool nsCocoaUtils::ShouldZoomOnTitlebarDoubleClick() {
  if ([NSWindow respondsToSelector:@selector(_shouldZoomOnDoubleClick)]) {
    return [NSWindow _shouldZoomOnDoubleClick];
  }
  return [ActionOnDoubleClickSystemPref() isEqualToString:@"Maximize"];
}

bool nsCocoaUtils::ShouldMinimizeOnTitlebarDoubleClick() {
  // Check the system preferences.
  // We could also check -[NSWindow _shouldMiniaturizeOnDoubleClick]. It's not
  // clear to me which approach would be preferable; neither is public API.
  return [ActionOnDoubleClickSystemPref() isEqualToString:@"Minimize"];
}

static const char* AVMediaTypeToString(AVMediaType aType) {
  if (aType == AVMediaTypeVideo) {
    return "video";
  }

  if (aType == AVMediaTypeAudio) {
    return "audio";
  }

  return "unexpected type";
}

static void LogAuthorizationStatus(AVMediaType aType, int aState) {
  const char* stateString;

  switch (aState) {
    case AVAuthorizationStatusAuthorized:
      stateString = "AVAuthorizationStatusAuthorized";
      break;
    case AVAuthorizationStatusDenied:
      stateString = "AVAuthorizationStatusDenied";
      break;
    case AVAuthorizationStatusNotDetermined:
      stateString = "AVAuthorizationStatusNotDetermined";
      break;
    case AVAuthorizationStatusRestricted:
      stateString = "AVAuthorizationStatusRestricted";
      break;
    default:
      stateString = "Invalid state";
  }

  LOG("%s authorization status: %s\n", AVMediaTypeToString(aType), stateString);
}

static nsresult GetPermissionState(AVMediaType aMediaType, uint16_t& aState) {
  MOZ_ASSERT(aMediaType == AVMediaTypeVideo || aMediaType == AVMediaTypeAudio);

  AVAuthorizationStatus authStatus = static_cast<AVAuthorizationStatus>(
      [AVCaptureDevice authorizationStatusForMediaType:aMediaType]);
  LogAuthorizationStatus(aMediaType, authStatus);

  // Convert AVAuthorizationStatus to nsIOSPermissionRequest const
  switch (authStatus) {
    case AVAuthorizationStatusAuthorized:
      aState = nsIOSPermissionRequest::PERMISSION_STATE_AUTHORIZED;
      return NS_OK;
    case AVAuthorizationStatusDenied:
      aState = nsIOSPermissionRequest::PERMISSION_STATE_DENIED;
      return NS_OK;
    case AVAuthorizationStatusNotDetermined:
      aState = nsIOSPermissionRequest::PERMISSION_STATE_NOTDETERMINED;
      return NS_OK;
    case AVAuthorizationStatusRestricted:
      aState = nsIOSPermissionRequest::PERMISSION_STATE_RESTRICTED;
      return NS_OK;
    default:
      MOZ_ASSERT(false, "Invalid authorization status");
      return NS_ERROR_UNEXPECTED;
  }
}

nsresult nsCocoaUtils::GetVideoCapturePermissionState(
    uint16_t& aPermissionState) {
  return GetPermissionState(AVMediaTypeVideo, aPermissionState);
}

nsresult nsCocoaUtils::GetAudioCapturePermissionState(
    uint16_t& aPermissionState) {
  return GetPermissionState(AVMediaTypeAudio, aPermissionState);
}

// Set |aPermissionState| to PERMISSION_STATE_AUTHORIZED if this application
// has already been granted permission to record the screen in macOS Security
// and Privacy system settings. If we do not have permission (because the user
// hasn't yet been asked yet or the user previously denied the prompt), use
// PERMISSION_STATE_DENIED. Returns NS_ERROR_NOT_IMPLEMENTED on macOS 10.14
// and earlier.
nsresult nsCocoaUtils::GetScreenCapturePermissionState(
    uint16_t& aPermissionState) {
  aPermissionState = nsIOSPermissionRequest::PERMISSION_STATE_NOTDETERMINED;

  if (!StaticPrefs::media_macos_screenrecording_oscheck_enabled()) {
    aPermissionState = nsIOSPermissionRequest::PERMISSION_STATE_AUTHORIZED;
    LOG("screen authorization status: authorized (test disabled via pref)");
    return NS_OK;
  }

  // Unlike with camera and microphone capture, there is no support for
  // checking the screen recording permission status. Instead, an application
  // can use the presence of window names (which are privacy sensitive) in
  // the window info list as an indication. The list only includes window
  // names if the calling application has been authorized to record the
  // screen. We use the window name, window level, and owning PID as
  // heuristics to determine if we have screen recording permission.
  AutoCFTypeRef<CFArrayRef> windowArray(
      CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID));
  if (!windowArray) {
    LOG("GetScreenCapturePermissionState() ERROR: got NULL window info list");
    return NS_ERROR_UNEXPECTED;
  }

  int32_t windowLevelDock = CGWindowLevelForKey(kCGDockWindowLevelKey);
  int32_t windowLevelNormal = CGWindowLevelForKey(kCGNormalWindowLevelKey);
  LOG("GetScreenCapturePermissionState(): DockWindowLevel: %d, "
      "NormalWindowLevel: %d",
      windowLevelDock, windowLevelNormal);

  int32_t thisPid = [[NSProcessInfo processInfo] processIdentifier];

  CFIndex windowCount = CFArrayGetCount(windowArray);
  LOG("GetScreenCapturePermissionState() returned %ld windows", windowCount);
  if (windowCount == 0) {
    return NS_ERROR_UNEXPECTED;
  }

  for (CFIndex i = 0; i < windowCount; i++) {
    CFDictionaryRef windowDict = reinterpret_cast<CFDictionaryRef>(
        CFArrayGetValueAtIndex(windowArray, i));

    // Get the window owner's PID
    int32_t windowOwnerPid = -1;
    CFNumberRef windowPidRef = reinterpret_cast<CFNumberRef>(
        CFDictionaryGetValue(windowDict, kCGWindowOwnerPID));
    if (!windowPidRef ||
        !CFNumberGetValue(windowPidRef, kCFNumberIntType, &windowOwnerPid)) {
      LOG("GetScreenCapturePermissionState() ERROR: failed to get window "
          "owner");
      continue;
    }

    // Our own window names are always readable and
    // therefore not relevant to the heuristic.
    if (thisPid == windowOwnerPid) {
      continue;
    }

    CFStringRef windowName = reinterpret_cast<CFStringRef>(
        CFDictionaryGetValue(windowDict, kCGWindowName));
    if (!windowName) {
      continue;
    }

    // macOS versions 12.2 (Monterey) or later have a status indicator when the
    // microphone is in use (an orange dot). This is implemented as a window
    // owned by the window server process. The permission check logic queries
    // window server for all windows and assumes it has the required permission
    // if it can read any window name that is at dock or normal level.
    // The StatusIndicator window is an exception and needs to be skipped
    // because it is owned by window server process and therefore when querying
    // the window server, the name is always readable.
    if (kCFCompareEqualTo ==
        CFStringCompare(windowName, CFSTR("StatusIndicator"), 0)) {
      continue;
    }

    CFNumberRef windowLayerRef = reinterpret_cast<CFNumberRef>(
        CFDictionaryGetValue(windowDict, kCGWindowLayer));
    int32_t windowLayer;
    if (!windowLayerRef ||
        !CFNumberGetValue(windowLayerRef, kCFNumberIntType, &windowLayer)) {
      LOG("GetScreenCapturePermissionState() ERROR: failed to get layer");
      continue;
    }

    // If we have a window name and the window is in the dock or normal window
    // level, and for another process, assume we have screen recording access.
    LOG("GetScreenCapturePermissionState(): windowLayer: %d", windowLayer);
    if (windowLayer == windowLevelDock || windowLayer == windowLevelNormal) {
      aPermissionState = nsIOSPermissionRequest::PERMISSION_STATE_AUTHORIZED;
      LOG("screen authorization status: authorized");
      return NS_OK;
    }
  }

  aPermissionState = nsIOSPermissionRequest::PERMISSION_STATE_DENIED;
  LOG("screen authorization status: not authorized");
  return NS_OK;
}

nsresult nsCocoaUtils::RequestVideoCapturePermission(
    RefPtr<Promise>& aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  return nsCocoaUtils::RequestCapturePermission(AVMediaTypeVideo, aPromise,
                                                sVideoCapturePromises,
                                                VideoCompletionHandler);
}

nsresult nsCocoaUtils::RequestAudioCapturePermission(
    RefPtr<Promise>& aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  return nsCocoaUtils::RequestCapturePermission(AVMediaTypeAudio, aPromise,
                                                sAudioCapturePromises,
                                                AudioCompletionHandler);
}

//
// Stores |aPromise| on |aPromiseList| and starts an asynchronous media
// capture request for the given media type |aType|. If we are already
// waiting for a capture request for this media type, don't start a new
// request. |aHandler| is invoked on an arbitrary dispatch queue when the
// request completes and must resolve any waiting Promises on the main
// thread.
//
nsresult nsCocoaUtils::RequestCapturePermission(
    AVMediaType aType, RefPtr<Promise>& aPromise, PromiseArray& aPromiseList,
    void (^aHandler)(BOOL granted)) {
  MOZ_ASSERT(aType == AVMediaTypeVideo || aType == AVMediaTypeAudio);
  LOG("RequestCapturePermission(%s)", AVMediaTypeToString(aType));

  sMediaCaptureMutex.Lock();

  // Initialize our list of promises on first invocation
  if (aPromiseList == nullptr) {
    aPromiseList = new nsTArray<RefPtr<Promise>>;
    ClearOnShutdown(&aPromiseList);
  }

  aPromiseList->AppendElement(aPromise);
  size_t nPromises = aPromiseList->Length();

  sMediaCaptureMutex.Unlock();

  LOG("RequestCapturePermission(%s): %zu promise(s) unresolved",
      AVMediaTypeToString(aType), nPromises);

  // If we had one or more more existing promises waiting to be resolved
  // by the completion handler, we don't need to start another request.
  if (nPromises > 1) {
    return NS_OK;
  }

  // Start the request
  [AVCaptureDevice requestAccessForMediaType:aType completionHandler:aHandler];
  return NS_OK;
}

//
// Audio capture request completion handler. Called from an arbitrary
// dispatch queue.
//
void (^nsCocoaUtils::AudioCompletionHandler)(BOOL) = ^void(BOOL granted) {
  nsCocoaUtils::ResolveAudioCapturePromises(granted);
};

//
// Video capture request completion handler. Called from an arbitrary
// dispatch queue.
//
void (^nsCocoaUtils::VideoCompletionHandler)(BOOL) = ^void(BOOL granted) {
  nsCocoaUtils::ResolveVideoCapturePromises(granted);
};

void nsCocoaUtils::ResolveMediaCapturePromises(bool aGranted,
                                               PromiseArray& aPromiseList) {
  StaticMutexAutoLock lock(sMediaCaptureMutex);

  // Remove each promise from the list and resolve it.
  while (aPromiseList->Length() > 0) {
    RefPtr<Promise> promise = aPromiseList->PopLastElement();

    // Resolve on main thread
    nsCOMPtr<nsIRunnable> runnable(
        NS_NewRunnableFunction("ResolveMediaAccessPromise",
                               [aGranted, aPromise = std::move(promise)]() {
                                 aPromise->MaybeResolve(aGranted);
                               }));
    NS_DispatchToMainThread(runnable.forget());
  }
}

void nsCocoaUtils::ResolveAudioCapturePromises(bool aGranted) {
  // Resolve on main thread
  nsCOMPtr<nsIRunnable> runnable(
      NS_NewRunnableFunction("ResolveAudioCapturePromise", [aGranted]() {
        ResolveMediaCapturePromises(aGranted, sAudioCapturePromises);
      }));
  NS_DispatchToMainThread(runnable.forget());
}

//
// Attempt to trigger a dialog requesting permission to record the screen.
// Unlike with the camera and microphone, there is no API to request permission
// to record the screen or to receive a callback when permission is explicitly
// allowed or denied. Here we attempt to trigger the dialog by attempting to
// capture a 1x1 pixel section of the screen. The permission dialog is not
// guaranteed to be displayed because the user may have already been prompted
// in which case macOS does not display the dialog again.
//
nsresult nsCocoaUtils::MaybeRequestScreenCapturePermission() {
  LOG("MaybeRequestScreenCapturePermission()");
  AutoCFTypeRef<CGImageRef> image(CGDisplayCreateImageForRect(
      kCGDirectMainDisplay, CGRectMake(0, 0, 1, 1)));
  return NS_OK;
}

void nsCocoaUtils::ResolveVideoCapturePromises(bool aGranted) {
  // Resolve on main thread
  nsCOMPtr<nsIRunnable> runnable(
      NS_NewRunnableFunction("ResolveVideoCapturePromise", [aGranted]() {
        ResolveMediaCapturePromises(aGranted, sVideoCapturePromises);
      }));
  NS_DispatchToMainThread(runnable.forget());
}

static PanGestureInput::PanGestureType PanGestureTypeForEvent(NSEvent* aEvent) {
  switch ([aEvent phase]) {
    case NSEventPhaseMayBegin:
      return PanGestureInput::PANGESTURE_MAYSTART;
    case NSEventPhaseCancelled:
      return PanGestureInput::PANGESTURE_CANCELLED;
    case NSEventPhaseBegan:
      return PanGestureInput::PANGESTURE_START;
    case NSEventPhaseChanged:
      return PanGestureInput::PANGESTURE_PAN;
    case NSEventPhaseEnded:
      return PanGestureInput::PANGESTURE_END;
    case NSEventPhaseNone:
      switch ([aEvent momentumPhase]) {
        case NSEventPhaseBegan:
          return PanGestureInput::PANGESTURE_MOMENTUMSTART;
        case NSEventPhaseChanged:
          return PanGestureInput::PANGESTURE_MOMENTUMPAN;
        case NSEventPhaseEnded:
          return PanGestureInput::PANGESTURE_MOMENTUMEND;
        default:
          NS_ERROR("unexpected event phase");
          return PanGestureInput::PANGESTURE_PAN;
      }
    default:
      NS_ERROR("unexpected event phase");
      return PanGestureInput::PANGESTURE_PAN;
  }
}

bool static ShouldConsiderStartingSwipeFromEvent(NSEvent* anEvent) {
  // Only initiate horizontal tracking for gestures that have just begun --
  // otherwise a scroll to one side of the page can have a swipe tacked on
  // to it.
  // [NSEvent isSwipeTrackingFromScrollEventsEnabled] checks whether the
  // AppleEnableSwipeNavigateWithScrolls global preference is set.  If it isn't,
  // fluid swipe tracking is disabled, and a horizontal two-finger gesture is
  // always a scroll (even in Safari).  This preference can't (currently) be set
  // from the Preferences UI -- only using 'defaults write'.
  NSEventPhase eventPhase = [anEvent phase];
  return [anEvent type] == NSEventTypeScrollWheel &&
         eventPhase == NSEventPhaseBegan &&
         [anEvent hasPreciseScrollingDeltas] &&
         [NSEvent isSwipeTrackingFromScrollEventsEnabled];
}

PanGestureInput nsCocoaUtils::CreatePanGestureEvent(
    NSEvent* aNativeEvent, TimeStamp aTimeStamp,
    const ScreenPoint& aPanStartPoint, const ScreenPoint& aPreciseDelta,
    const gfx::IntPoint& aLineOrPageDelta, Modifiers aModifiers) {
  PanGestureInput::PanGestureType type = PanGestureTypeForEvent(aNativeEvent);
  // Always force zero deltas on event types that shouldn't cause any scrolling,
  // so that we don't dispatch DOM wheel events for them.
  bool shouldIgnoreDeltas = type == PanGestureInput::PANGESTURE_MAYSTART ||
                            type == PanGestureInput::PANGESTURE_CANCELLED;

  PanGestureInput panEvent(
      type, aTimeStamp, aPanStartPoint,
      !shouldIgnoreDeltas ? aPreciseDelta : ScreenPoint(), aModifiers,
      PanGestureInput::IsEligibleForSwipe(
          ShouldConsiderStartingSwipeFromEvent(aNativeEvent)));

  if (!shouldIgnoreDeltas) {
    panEvent.SetLineOrPageDeltas(aLineOrPageDelta.x, aLineOrPageDelta.y);
  }

  return panEvent;
}

bool nsCocoaUtils::IsValidPasteboardType(NSString* aAvailableType,
                                         bool aAllowFileURL) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Prevent exposing fileURL for non-fileURL type.
  // We need URL provided by dropped webloc file, but don't need file's URL.
  // kUTTypeFileURL is returned by [NSPasteboard availableTypeFromArray:] for
  // kPublicUrlPboardType, since it conforms to kPublicUrlPboardType.
  bool isValid = true;
  if (!aAllowFileURL &&
      [aAvailableType
          isEqualToString:[UTIHelper
                              stringFromPboardType:(NSString*)
                                                       kUTTypeFileURL]]) {
    isValid = false;
  }

  return isValid;

  NS_OBJC_END_TRY_BLOCK_RETURN(false);
}

NSString* nsCocoaUtils::GetStringForTypeFromPasteboardItem(
    NSPasteboardItem* aItem, const NSString* aType, bool aAllowFileURL) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSString* availableType =
      [aItem availableTypeFromArray:[NSArray arrayWithObjects:(id)aType, nil]];
  if (availableType && IsValidPasteboardType(availableType, aAllowFileURL)) {
    NSString* str = [aItem stringForType:(id)availableType];
    if (!str) {
      return nil;
    }
    // Sanitize (remove NULs, etc) to align with other platforms.
    return [NSString stringWithCString:[str UTF8String]
                              encoding:NSUTF8StringEncoding];
  }

  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

NSString* nsCocoaUtils::GetFilePathFromPasteboardItem(NSPasteboardItem* aItem) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSString* urlString = GetStringForTypeFromPasteboardItem(
      aItem, [UTIHelper stringFromPboardType:(NSString*)kUTTypeFileURL], true);
  if (urlString) {
    NSURL* url = [NSURL URLWithString:urlString];
    if (url) {
      return [url path];
    }
  }

  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

NSString* nsCocoaUtils::GetTitleForURLFromPasteboardItem(
    NSPasteboardItem* item) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  NSString* name = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
      item, [UTIHelper stringFromPboardType:kPublicUrlNamePboardType]);
  if (name) {
    return name;
  }

  NSString* filePath = nsCocoaUtils::GetFilePathFromPasteboardItem(item);
  if (filePath) {
    return [filePath lastPathComponent];
  }

  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

already_AddRefed<nsISupports> nsCocoaUtils::GetDataFromPasteboardItem(
    const nsACString& aFlavor, NSPasteboardItem* aItem) {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!aItem) {
    return nullptr;
  }

  MOZ_LOG(gCocoaUtilsLog, LogLevel::Info,
          ("nsCocoaUtils::GetDataFromPasteboardItem: looking for pasteboard "
           "data of type %s\n",
           PromiseFlatCString(aFlavor).get()));

  if (aFlavor.EqualsLiteral(kFileMime)) {
    NSString* filePath = nsCocoaUtils::GetFilePathFromPasteboardItem(aItem);
    if (!filePath) {
      return nullptr;
    }

    unsigned int stringLength = [filePath length];
    unsigned int dataLength =
        (stringLength + 1) * sizeof(char16_t);  // in bytes
    char16_t* clipboardDataPtr = (char16_t*)malloc(dataLength);
    if (!clipboardDataPtr) {
      return nullptr;
    }

    [filePath getCharacters:reinterpret_cast<unichar*>(clipboardDataPtr)];
    clipboardDataPtr[stringLength] = 0;  // null terminate

    nsCOMPtr<nsIFile> file;
    nsresult rv = NS_NewLocalFile(nsDependentString(clipboardDataPtr),
                                  getter_AddRefs(file));
    free(clipboardDataPtr);
    if (NS_FAILED(rv)) {
      return nullptr;
    }

    return file.forget();
  }

  if (aFlavor.EqualsLiteral(kCustomTypesMime)) {
    NSString* availableType = [aItem
        availableTypeFromArray:[NSArray
                                   arrayWithObject:kMozCustomTypesPboardType]];
    if (!availableType ||
        !nsCocoaUtils::IsValidPasteboardType(availableType, false)) {
      return nullptr;
    }
    NSData* pasteboardData = [aItem dataForType:availableType];
    if (!pasteboardData) {
      return nullptr;
    }

    unsigned int dataLength = [pasteboardData length];
    void* clipboardDataPtr = malloc(dataLength);
    if (!clipboardDataPtr) {
      return nullptr;
    }
    [pasteboardData getBytes:clipboardDataPtr length:dataLength];

    nsCOMPtr<nsISupports> genericDataWrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        aFlavor, clipboardDataPtr, dataLength,
        getter_AddRefs(genericDataWrapper));

    free(clipboardDataPtr);
    return genericDataWrapper.forget();
  }

  NSString* pString = nil;
  if (aFlavor.EqualsLiteral(kTextMime)) {
    pString = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
        aItem, [UTIHelper stringFromPboardType:NSPasteboardTypeString]);
  } else if (aFlavor.EqualsLiteral(kHTMLMime)) {
    pString = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
        aItem, [UTIHelper stringFromPboardType:NSPasteboardTypeHTML]);
  } else if (aFlavor.EqualsLiteral(kURLMime)) {
    pString = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
        aItem, [UTIHelper stringFromPboardType:kPublicUrlPboardType]);
    if (pString) {
      NSString* title = GetTitleForURLFromPasteboardItem(aItem);
      if (!title) {
        title = pString;
      }
      pString = [NSString stringWithFormat:@"%@\n%@", pString, title];
    } else {
      // Try to convert file to file:// URL
      nsCOMPtr<nsISupports> fileData =
          GetDataFromPasteboardItem(nsDependentCString(kFileMime), aItem);
      nsCOMPtr<nsIFile> file = do_QueryInterface(fileData);
      if (file) {
        nsAutoCString urlSpec;
        if (NS_SUCCEEDED(NS_GetURLSpecFromFile(file, urlSpec))) {
          nsAutoString leafName;
          file->GetLeafName(leafName);

          NSString* url = nsCocoaUtils::ToNSString(urlSpec);
          NSString* title = nsCocoaUtils::ToNSString(leafName);
          pString = [NSString stringWithFormat:@"%@\n%@", url, title];
        }
      }
    }
  } else if (aFlavor.EqualsLiteral(kURLDataMime)) {
    pString = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
        aItem, [UTIHelper stringFromPboardType:kPublicUrlPboardType]);
  } else if (aFlavor.EqualsLiteral(kURLDescriptionMime)) {
    pString = GetTitleForURLFromPasteboardItem(aItem);
  } else if (aFlavor.EqualsLiteral(kRTFMime)) {
    pString = nsCocoaUtils::GetStringForTypeFromPasteboardItem(
        aItem, [UTIHelper stringFromPboardType:NSPasteboardTypeRTF]);
  }
  if (pString) {
    NSData* stringData;
    bool isRTF = aFlavor.EqualsLiteral(kRTFMime);
    if (isRTF) {
      stringData = [pString dataUsingEncoding:NSASCIIStringEncoding];
    } else {
      stringData = [pString dataUsingEncoding:NSUnicodeStringEncoding];
    }
    unsigned int dataLength = [stringData length];
    void* clipboardDataPtr = malloc(dataLength);
    if (!clipboardDataPtr) {
      return nullptr;
    }
    [stringData getBytes:clipboardDataPtr length:dataLength];

    // The DOM only wants LF, so convert from MacOS line endings to DOM line
    // endings.
    int32_t signedDataLength = dataLength;
    nsLinebreakHelpers::ConvertPlatformToDOMLinebreaks(isRTF, &clipboardDataPtr,
                                                       &signedDataLength);
    dataLength = signedDataLength;

    // skip BOM (Byte Order Mark to distinguish little or big endian)
    char16_t* clipboardDataPtrNoBOM = (char16_t*)clipboardDataPtr;
    if ((dataLength > 2) && ((clipboardDataPtrNoBOM[0] == 0xFEFF) ||
                             (clipboardDataPtrNoBOM[0] == 0xFFFE))) {
      dataLength -= sizeof(char16_t);
      clipboardDataPtrNoBOM += 1;
    }

    nsCOMPtr<nsISupports> genericDataWrapper;
    nsPrimitiveHelpers::CreatePrimitiveForData(
        aFlavor, clipboardDataPtrNoBOM, dataLength,
        getter_AddRefs(genericDataWrapper));
    free(clipboardDataPtr);
    return genericDataWrapper.forget();
  }

  // We have never supported this on macOS, we should someday. Normally
  // dragging images in is accomplished with a file path drag instead of the
  // image data itself.
  /*
  if (aFlavor.EqualsLiteral(kPNGImageMime) ||
  aFlavor.EqualsLiteral(kJPEGImageMime) || aFlavor.EqualsLiteral(kJPGImageMime)
  || aFlavor.EqualsLiteral(kGIFImageMime)) {

  }
  */

  return nullptr;

  NS_OBJC_END_TRY_BLOCK_RETURN(nullptr);
}

void nsCocoaUtils::SetTransferDataForTypeFromPasteboardItem(
    nsITransferable* aTransferable, const nsCString& aFlavor,
    NSPasteboardItem* aItem) {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  if (!aTransferable || !aItem) {
    return;
  }

  MOZ_LOG(gCocoaUtilsLog, LogLevel::Info,
          ("nsCocoaUtils::SetTransferDataForTypeFromPasteboardItem: looking "
           "for pasteboard data of type %s\n",
           aFlavor.get()));

  if (nsCOMPtr<nsISupports> data = GetDataFromPasteboardItem(aFlavor, aItem)) {
    aTransferable->SetTransferData(aFlavor.get(), data);
  }

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

NSRectEdge nsCocoaUtils::PopupPositionToNSRectEdge(int8_t aPosition) {
  // XUL accepts many more anchor popup alignments than Cocoa. Map to the best
  // approximate edge setting. Due to Cocoa's flipped Y-axis, NSRectEdgeMinY
  // represents the bottom edge.
  switch (aPosition) {
    case POPUPPOSITION_BEFORESTART:
    case POPUPPOSITION_BEFOREEND:
      return NSRectEdgeMaxY;
    case POPUPPOSITION_STARTBEFORE:
    case POPUPPOSITION_STARTAFTER:
      return NSRectEdgeMinX;
    case POPUPPOSITION_ENDBEFORE:
    case POPUPPOSITION_ENDAFTER:
      return NSRectEdgeMaxX;
    default:
      return NSRectEdgeMinY;
  }
}
