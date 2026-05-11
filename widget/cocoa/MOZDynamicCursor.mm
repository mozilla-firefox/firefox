/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "imgIContainer.h"
#include "nsCocoaUtils.h"
#include "MOZDynamicCursor.h"
#include "nsObjCExceptions.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFile.h"

static MOZDynamicCursor* gInstance;
static CGFloat sCurrentCursorScaleFactor = 0.0f;
constinit static nsIWidget::Cursor sCurrentCursor;
static constexpr nsCursor kCustomCursor = eCursorCount;

@interface MOZDynamicCursor (PrivateMethods)
+ (NSCursor*)freshCursorWithType:(nsCursor)aCursor;
- (NSCursor*)cursorWithType:(nsCursor)aCursor;

// Set the cursor.
- (void)setCursor:(NSCursor*)aMacCursor;

@end

@interface NSCursor (CreateWithImageName)
+ (NSCursor*)cursorWithImageNamed:(NSString*)imageName hotSpot:(NSPoint)aPoint;
@end

@interface NSCursor (Undocumented)
// busyButClickableCursor is an undocumented NSCursor API, but has been in use
// since at least macOS 10.4 and through 10.9.
+ (NSCursor*)busyButClickableCursor;
@end

@implementation MOZDynamicCursor

+ (MOZDynamicCursor*)sharedInstance {
  if (!gInstance) {
    gInstance = [[MOZDynamicCursor alloc] init];
  }
  return gInstance;
}

+ (NSCursor*)freshCursorWithType:(enum nsCursor)aCursor {
  switch (aCursor) {
    case eCursor_standard:
      return [NSCursor arrowCursor];
    case eCursor_wait:
    case eCursor_spinning: {
      return [NSCursor busyButClickableCursor];
    }
    case eCursor_select:
      return [NSCursor IBeamCursor];
    case eCursor_hyperlink:
      return [NSCursor pointingHandCursor];
    case eCursor_crosshair:
      return [NSCursor crosshairCursor];
    case eCursor_move:
      return [NSCursor cursorWithImageNamed:@"move"
                                    hotSpot:NSMakePoint(12, 12)];
    case eCursor_help:
      return [NSCursor cursorWithImageNamed:@"help"
                                    hotSpot:NSMakePoint(12, 12)];
    case eCursor_copy: {
      return [NSCursor dragCopyCursor];
    }
    case eCursor_alias: {
      return [NSCursor dragLinkCursor];
    }
    case eCursor_context_menu: {
      return [NSCursor contextualMenuCursor];
    }
    case eCursor_cell:
      return [NSCursor cursorWithImageNamed:@"cell"
                                    hotSpot:NSMakePoint(12, 12)];
    case eCursor_grab:
      return [NSCursor openHandCursor];
    case eCursor_grabbing:
      return [NSCursor closedHandCursor];
    case eCursor_zoom_in:
      if (@available(macOS 15.0, *)) {
        return [NSCursor zoomInCursor];
      }
      return [NSCursor cursorWithImageNamed:@"zoomIn"
                                    hotSpot:NSMakePoint(10, 10)];
    case eCursor_zoom_out:
      if (@available(macOS 15.0, *)) {
        return [NSCursor zoomOutCursor];
      }
      return [NSCursor cursorWithImageNamed:@"zoomOut"
                                    hotSpot:NSMakePoint(10, 10)];
    case eCursor_vertical_text:
      return [NSCursor IBeamCursorForVerticalLayout];
    case eCursor_all_scroll:
      return [NSCursor openHandCursor];
    case eCursor_not_allowed:
    case eCursor_no_drop: {
      return [NSCursor operationNotAllowedCursor];
    }
    // Resize Cursors:
    // North
    case eCursor_n_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTop
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor resizeUpCursor];
    // North East
    case eCursor_ne_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor cursorWithImageNamed:@"sizeNE"
                                    hotSpot:NSMakePoint(12, 11)];
    // East
    case eCursor_e_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionRight
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor resizeRightCursor];
    // South East
    case eCursor_se_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomRight
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor cursorWithImageNamed:@"sizeSE"
                                    hotSpot:NSMakePoint(12, 12)];
    // South
    case eCursor_s_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionBottom
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor resizeDownCursor];
    // South West
    case eCursor_sw_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomLeft
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor cursorWithImageNamed:@"sizeSW"
                                    hotSpot:NSMakePoint(10, 12)];
    // West
    case eCursor_w_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionLeft
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor resizeLeftCursor];
    // North West
    case eCursor_nw_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                             inDirections:NSCursorFrameResizeDirectionsOutward];
      }
      return [NSCursor cursorWithImageNamed:@"sizeNW"
                                    hotSpot:NSMakePoint(11, 11)];
    // North & South
    case eCursor_ns_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTop
                             inDirections:NSCursorFrameResizeDirectionsAll];
      }
      return [NSCursor resizeUpDownCursor];
    // East & West
    case eCursor_ew_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionLeft
                             inDirections:NSCursorFrameResizeDirectionsAll];
      }
      return [NSCursor resizeLeftRightCursor];
    // North East & South West
    case eCursor_nesw_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                             inDirections:NSCursorFrameResizeDirectionsAll];
      }
      return [NSCursor cursorWithImageNamed:@"sizeNESW"
                                    hotSpot:NSMakePoint(12, 12)];
    // North West & South East
    case eCursor_nwse_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor
            frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                             inDirections:NSCursorFrameResizeDirectionsAll];
      }
      return [NSCursor cursorWithImageNamed:@"sizeNWSE"
                                    hotSpot:NSMakePoint(12, 12)];
    // Column Resize
    case eCursor_col_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor columnResizeCursor];
      }
      return [NSCursor resizeLeftRightCursor];
    // Row Resize
    case eCursor_row_resize:
      if (@available(macOS 15.0, *)) {
        return [NSCursor rowResizeCursor];
      }
      return [NSCursor resizeUpDownCursor];
    default:
      return [NSCursor arrowCursor];
  }
}

- (id)init {
  if ((self = [super init])) {
    mCursors = [[NSMutableDictionary alloc] initWithCapacity:25];
    mCurrentCursor = [[NSCursor arrowCursor] retain];
    mCurrentCursorType = eCursor_standard;
  }
  return self;
}

- (void)setNonCustomCursor:(const nsIWidget::Cursor&)aCursor {
  [self setCursor:[self cursorWithType:aCursor.mDefaultCursor]
             type:aCursor.mDefaultCursor];
  sCurrentCursor = aCursor;
}

- (void)setCursor:(NSCursor*)aMacCursor type:(nsCursor)aType {
  if (mCurrentCursorType != aType) {
    if (aType == eCursor_none) {
      [NSCursor hide];
    } else if (mCurrentCursorType == eCursor_none) {
      [NSCursor unhide];
    }
    mCurrentCursorType = aType;
  }

  if (mCurrentCursor != aMacCursor) {
    [mCurrentCursor release];
    mCurrentCursor = [aMacCursor retain];
    [mCurrentCursor set];
  }
}

- (nsresult)setCustomCursor:(const nsIWidget::Cursor&)aCursor
          widgetScaleFactor:(CGFloat)scaleFactor
                forceUpdate:(bool)aForceUpdate {
  // As the user moves the mouse, this gets called repeatedly with the same
  // aCursorImage
  if (!aForceUpdate && sCurrentCursor == aCursor &&
      sCurrentCursorScaleFactor == scaleFactor && mCurrentCursor) {
    return NS_OK;
  }

  sCurrentCursor = aCursor;
  sCurrentCursorScaleFactor = scaleFactor;

  if (!aCursor.IsCustom()) {
    return NS_ERROR_FAILURE;
  }

  nsIntSize size = nsIWidget::CustomCursorSize(aCursor);
  // prevent DoS attacks
  if (size.width > 128 || size.height > 128) {
    return NS_ERROR_FAILURE;
  }

  const NSSize cocoaSize = NSMakeSize(size.width, size.height);
  NSImage* cursorImage;
  nsresult rv = nsCocoaUtils::CreateNSImageFromImageContainer(
      aCursor.mContainer, imgIContainer::FRAME_FIRST, nullptr, cocoaSize,
      &cursorImage, scaleFactor);
  if (NS_FAILED(rv) || !cursorImage) {
    return NS_ERROR_FAILURE;
  }

  [cursorImage setSize:cocoaSize];
  [[[cursorImage representations] objectAtIndex:0] setSize:cocoaSize];

  // if the hotspot is nonsensical, make it 0,0
  uint32_t hotspotX =
      aCursor.mHotspotX > (uint32_t(size.width) - 1) ? 0 : aCursor.mHotspotX;
  uint32_t hotspotY =
      aCursor.mHotspotY > (uint32_t(size.height) - 1) ? 0 : aCursor.mHotspotY;
  NSPoint hotSpot = ::NSMakePoint(hotspotX, hotspotY);
  NSCursor* newCursor = [[[NSCursor alloc] initWithImage:cursorImage
                                                 hotSpot:hotSpot] autorelease];
  [self setCursor:newCursor type:kCustomCursor];
  [cursorImage release];
  return NS_OK;
}

- (NSCursor*)cursorWithType:(enum nsCursor)aCursor {
  NSCursor* result = [mCursors objectForKey:[NSNumber numberWithInt:aCursor]];
  if (!result) {
    result = [MOZDynamicCursor freshCursorWithType:aCursor];
    [mCursors setObject:result forKey:[NSNumber numberWithInt:aCursor]];
  }
  return result;
}

// This method gets called by ChildView's cursor rect (or rather its underlying
// NSTrackingArea) whenever the mouse enters it, for example after a dragging
// operation, after a menu closes, or when the mouse enters a window.
- (void)set {
  [mCurrentCursor set];
}

- (void)reassertCurrentCursor {
  // [NSCursor hide] is reference-counted: each hide must be balanced by
  // unhide. Pairing unhide+hide here is a no-op when our hide is still
  // on the stack and re-establishes the hide if AppKit dropped it under
  // us during a window transition.
  if (mCurrentCursorType == eCursor_none) {
    [NSCursor unhide];
    [NSCursor hide];
  } else {
    [mCurrentCursor set];
  }
}

- (void)dealloc {
  [mCurrentCursor release];
  [mCursors release];
  sCurrentCursor = {};
  [super dealloc];
}

@end

@implementation NSCursor (CreateWithImageName)

+ (NSCursor*)cursorWithImageNamed:(NSString*)imageName hotSpot:(NSPoint)aPoint {
  nsCOMPtr<nsIFile> resDir;
  nsAutoCString resPath;
  NSString *pathToImage, *pathToHiDpiImage;
  NSImage *cursorImage, *hiDpiCursorImage;

  nsresult rv = NS_GetSpecialDirectory(NS_GRE_DIR, getter_AddRefs(resDir));
  if (NS_FAILED(rv)) {
    NS_WARNING("Problem getting GRE directory for cursor.");
    return nil;
  }

  resDir->AppendNative("res"_ns);
  resDir->AppendNative("cursors"_ns);

  rv = resDir->GetNativePath(resPath);
  if (NS_FAILED(rv)) {
    NS_WARNING("Problem getting cursor directory.");
    return nil;
  }

  pathToImage = [NSString stringWithUTF8String:(const char*)resPath.get()];
  if (!pathToImage) {
    NS_WARNING("Problem converting cursor directory path to NSString.");
    return nil;
  }

  pathToImage = [pathToImage stringByAppendingPathComponent:imageName];
  pathToHiDpiImage = [pathToImage stringByAppendingString:@"@2x"];
  // Add same extension to both image paths.
  pathToImage = [pathToImage stringByAppendingPathExtension:@"png"];
  pathToHiDpiImage = [pathToHiDpiImage stringByAppendingPathExtension:@"png"];

  cursorImage =
      [[[NSImage alloc] initWithContentsOfFile:pathToImage] autorelease];
  if (!cursorImage) {
    NS_WARNING("Problem loading cursor image.");
    return nil;
  }

  // Note 1: There are a few different ways to get a hidpi image via
  // initWithContentsOfFile. We let the OS handle this here: when the
  // file basename ends in "@2x", it will be displayed at native resolution
  // instead of being pixel-doubled. See bug 784909 comment 7 for alternate
  // ways.
  //
  // Note 2: The OS is picky, and will ignore the hidpi representation
  // unless it is exactly twice the size of the lowdpi image.
  hiDpiCursorImage =
      [[[NSImage alloc] initWithContentsOfFile:pathToHiDpiImage] autorelease];
  if (hiDpiCursorImage) {
    NSImageRep* imageRep = [[hiDpiCursorImage representations] objectAtIndex:0];
    [cursorImage addRepresentation:imageRep];
  }
  return [[[NSCursor alloc] initWithImage:cursorImage
                                  hotSpot:aPoint] autorelease];
}

@end
