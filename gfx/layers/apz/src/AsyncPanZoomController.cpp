/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AsyncPanZoomController.h"  // for AsyncPanZoomController, etc

#include <math.h>       // for fabsf, fabs, atan2
#include <stdint.h>     // for uint32_t, uint64_t
#include <sys/types.h>  // for int32_t
#include <algorithm>    // for max, min
#include <utility>      // for std::make_pair

#include "APZCTreeManager.h"            // for APZCTreeManager
#include "AsyncPanZoomAnimation.h"      // for AsyncPanZoomAnimation
#include "AutoDirWheelDeltaAdjuster.h"  // for APZAutoDirWheelDeltaAdjuster
#include "AutoscrollAnimation.h"        // for AutoscrollAnimation
#include "Axis.h"                       // for AxisX, AxisY, Axis, etc
#include "CheckerboardEvent.h"          // for CheckerboardEvent
#include "Compositor.h"                 // for Compositor
#include "DesktopFlingPhysics.h"        // for DesktopFlingPhysics
#include "FrameMetrics.h"               // for FrameMetrics, etc
#include "GenericFlingAnimation.h"      // for GenericFlingAnimation
#include "GestureEventListener.h"       // for GestureEventListener
#include "HitTestingTreeNode.h"         // for HitTestingTreeNode
#include "InputData.h"                  // for MultiTouchInput, etc
#include "InputBlockState.h"            // for InputBlockState, TouchBlockState
#include "InputQueue.h"                 // for InputQueue
#include "Overscroll.h"                 // for OverscrollAnimation
#include "OverscrollHandoffState.h"     // for OverscrollHandoffState
#include "SimpleVelocityTracker.h"      // for SimpleVelocityTracker
#include "Units.h"                      // for CSSRect, CSSPoint, etc
#include "UnitTransforms.h"             // for TransformTo
#include "apz/public/CompositorScrollUpdate.h"
#include "base/message_loop.h"          // for MessageLoop
#include "base/task.h"                  // for NewRunnableMethod, etc
#include "gfxTypes.h"                   // for gfxFloat
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/BasicEvents.h"        // for Modifiers, MODIFIER_*
#include "mozilla/ClearOnShutdown.h"    // for ClearOnShutdown
#include "mozilla/ServoStyleConsts.h"   // for StyleComputedTimingFunction
#include "mozilla/EventForwards.h"      // for nsEventStatus_*
#include "mozilla/EventStateManager.h"  // for EventStateManager
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/MouseEvents.h"     // for WidgetWheelEvent
#include "mozilla/Preferences.h"     // for Preferences
#include "mozilla/RecursiveMutex.h"  // for RecursiveMutexAutoLock, etc
#include "mozilla/RefPtr.h"          // for RefPtr
#include "mozilla/ScrollTypes.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/StaticPrefs_mousewheel.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_slider.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/StaticPrefs_toolkit.h"
#include "mozilla/Telemetry.h"  // for Telemetry
#include "mozilla/TimeStamp.h"  // for TimeDuration, TimeStamp
#include "mozilla/dom/CheckerboardReportService.h"  // for CheckerboardEventStorage
// note: CheckerboardReportService.h actually lives in gfx/layers/apz/util/
#include "mozilla/dom/Touch.h"              // for Touch
#include "mozilla/gfx/gfxVars.h"            // for gfxVars
#include "mozilla/gfx/BasePoint.h"          // for BasePoint
#include "mozilla/gfx/BaseRect.h"           // for BaseRect
#include "mozilla/gfx/Point.h"              // for Point, RoundedToInt, etc
#include "mozilla/gfx/Rect.h"               // for RoundedIn
#include "mozilla/gfx/ScaleFactor.h"        // for ScaleFactor
#include "mozilla/layers/APZThreadUtils.h"  // for AssertOnControllerThread, etc
#include "mozilla/layers/APZUtils.h"        // for AsyncTransform
#include "mozilla/layers/CompositorController.h"  // for CompositorController
#include "mozilla/layers/DirectionUtils.h"  // for GetAxis{Start,End,Length,Scale}
#include "mozilla/layers/APZPublicUtils.h"   // for GetScrollMode
#include "mozilla/webrender/WebRenderAPI.h"  // for MinimapData
#include "mozilla/mozalloc.h"                // for operator new, etc
#include "mozilla/Unused.h"                  // for unused
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsCOMPtr.h"  // for already_AddRefed
#include "nsDebug.h"   // for NS_WARNING
#include "nsLayoutUtils.h"
#include "nsMathUtils.h"  // for NS_hypot
#include "nsPoint.h"      // for nsIntPoint
#include "nsStyleConsts.h"
#include "nsTArray.h"        // for nsTArray, nsTArray_Impl, etc
#include "nsThreadUtils.h"   // for NS_IsMainThread
#include "nsViewportInfo.h"  // for ViewportMinScale(), ViewportMaxScale()
#include "prsystem.h"        // for PR_GetPhysicalMemorySize
#include "ScrollSnap.h"      // for ScrollSnapUtils
#include "ScrollAnimationPhysics.h"  // for ComputeAcceleratedWheelDelta
#include "SmoothMsdScrollAnimation.h"
#include "SmoothScrollAnimation.h"
#include "WheelScrollAnimation.h"
#if defined(MOZ_WIDGET_ANDROID)
#  include "AndroidAPZ.h"
#endif  // defined(MOZ_WIDGET_ANDROID)

static mozilla::LazyLogModule sApzCtlLog("apz.controller");
#define APZC_LOG(...) MOZ_LOG(sApzCtlLog, LogLevel::Debug, (__VA_ARGS__))
#define APZC_LOGV(...) MOZ_LOG(sApzCtlLog, LogLevel::Verbose, (__VA_ARGS__))

// Log to the apz.controller log with additional info from the APZC
#define APZC_LOG_DETAIL(fmt, apzc, ...)                   \
  APZC_LOG("%p(%s scrollId=%" PRIu64 "): " fmt, (apzc),   \
           (apzc)->IsRootContent() ? "root" : "subframe", \
           (apzc)->GetScrollId(), ##__VA_ARGS__)
#define APZC_LOGV_DETAIL(fmt, apzc, ...)                   \
  APZC_LOGV("%p(%s scrollId=%" PRIu64 "): " fmt, (apzc),   \
            (apzc)->IsRootContent() ? "root" : "subframe", \
            (apzc)->GetScrollId(), ##__VA_ARGS__)

#define APZC_LOG_FM_COMMON(fm, prefix, level, ...)                 \
  if (MOZ_LOG_TEST(sApzCtlLog, level)) {                           \
    std::stringstream ss;                                          \
    ss << nsPrintfCString(prefix, __VA_ARGS__).get() << ":" << fm; \
    MOZ_LOG(sApzCtlLog, level, ("%s\n", ss.str().c_str()));        \
  }
#define APZC_LOG_FM(fm, prefix, ...) \
  APZC_LOG_FM_COMMON(fm, prefix, LogLevel::Debug, __VA_ARGS__)
#define APZC_LOGV_FM(fm, prefix, ...) \
  APZC_LOG_FM_COMMON(fm, prefix, LogLevel::Verbose, __VA_ARGS__)

namespace mozilla {
namespace layers {

typedef mozilla::layers::AllowedTouchBehavior AllowedTouchBehavior;
typedef GeckoContentController::APZStateChange APZStateChange;
typedef GeckoContentController::TapType TapType;
typedef mozilla::gfx::Point Point;
typedef mozilla::gfx::Matrix4x4 Matrix4x4;

// Choose between platform-specific implementations.
#ifdef MOZ_WIDGET_ANDROID
typedef WidgetOverscrollEffect OverscrollEffect;
typedef AndroidSpecificState PlatformSpecificState;
#else
typedef GenericOverscrollEffect OverscrollEffect;
typedef PlatformSpecificStateBase
    PlatformSpecificState;  // no extra state, just use the base class
#endif

/**
 * \page APZCPrefs APZ preferences
 *
 * The following prefs are used to control the behaviour of the APZC.
 * The default values are provided in StaticPrefList.yaml.
 *
 * \li\b apz.allow_double_tap_zooming
 * Pref that allows or disallows double tap to zoom
 *
 * \li\b apz.allow_immediate_handoff
 * If set to true, scroll can be handed off from one APZC to another within
 * a single input block. If set to false, a single input block can only
 * scroll one APZC.
 *
 * \li\b apz.allow_zooming_out
 * If set to true, APZ will allow zooming out past the initial scale on
 * desktop. This is false by default to match Chrome's behaviour.
 *
 * \li\b apz.android.chrome_fling_physics.friction
 * A tunable parameter for Chrome fling physics on Android that governs
 * how quickly a fling animation slows down due to friction (and therefore
 * also how far it reaches). Should be in the range [0-1].
 *
 * \li\b apz.android.chrome_fling_physics.inflexion
 * A tunable parameter for Chrome fling physics on Android that governs
 * the shape of the fling curve. Should be in the range [0-1].
 *
 * \li\b apz.android.chrome_fling_physics.stop_threshold
 * A tunable parameter for Chrome fling physics on Android that governs
 * how close the fling animation has to get to its target destination
 * before it stops.
 * Units: ParentLayer pixels
 *
 * \li\b apz.autoscroll.enabled
 * If set to true, autoscrolling is driven by APZ rather than the content
 * process main thread.
 *
 * \li\b apz.axis_lock.mode
 * The preferred axis locking style. See AxisLockMode for possible values.
 *
 * \li\b apz.axis_lock.lock_angle
 * Angle from axis within which we stay axis-locked.\n
 * Units: radians
 *
 * \li\b apz.axis_lock.breakout_threshold
 * Distance in inches the user must pan before axis lock can be broken.\n
 * Units: (real-world, i.e. screen) inches
 *
 * \li\b apz.axis_lock.breakout_angle
 * Angle at which axis lock can be broken.\n
 * Units: radians
 *
 * \li\b apz.axis_lock.direct_pan_angle
 * If the angle from an axis to the line drawn by a pan move is less than
 * this value, we can assume that panning can be done in the allowed direction
 * (horizontal or vertical).\n
 * Currently used only for touch-action css property stuff and was addded to
 * keep behaviour consistent with IE.\n
 * Units: radians
 *
 * \li\b apz.content_response_timeout
 * Amount of time before we timeout response from content. For example, if
 * content is being unruly/slow and we don't get a response back within this
 * time, we will just pretend that content did not preventDefault any touch
 * events we dispatched to it.\n
 * Units: milliseconds
 *
 * \li\b apz.danger_zone_x
 * \li\b apz.danger_zone_y
 * When drawing high-res tiles, we drop down to drawing low-res tiles
 * when we know we can't keep up with the scrolling. The way we determine
 * this is by checking if we are entering the "danger zone", which is the
 * boundary of the painted content. For example, if the painted content
 * goes from y=0...1000 and the visible portion is y=250...750 then
 * we're far from checkerboarding. If we get to y=490...990 though then we're
 * only 10 pixels away from showing checkerboarding so we are probably in
 * a state where we can't keep up with scrolling. The danger zone prefs specify
 * how wide this margin is; in the above example a y-axis danger zone of 10
 * pixels would make us drop to low-res at y=490...990.\n
 * This value is in screen pixels.
 *
 * \li\b apz.disable_for_scroll_linked_effects
 * Setting this pref to true will disable APZ scrolling on documents where
 * scroll-linked effects are detected. A scroll linked effect is detected if
 * positioning or transform properties are updated inside a scroll event
 * dispatch; we assume that such an update is in response to the scroll event
 * and is therefore a scroll-linked effect which will be laggy with APZ
 * scrolling.
 *
 * \li\b apz.displayport_expiry_ms
 * While a scrollable frame is scrolling async, we set a displayport on it
 * to make sure it is layerized. However this takes up memory, so once the
 * scrolling stops we want to remove the displayport. This pref controls how
 * long after scrolling stops the displayport is removed. A value of 0 will
 * disable the expiry behavior entirely.
 * Units: milliseconds
 *
 * \li\b apz.drag.enabled
 * Setting this pref to true will cause APZ to handle mouse-dragging of
 * scrollbar thumbs.
 *
 * \li\b apz.drag.touch.enabled
 * Setting this pref to true will cause APZ to handle touch-dragging of
 * scrollbar thumbs. Only has an effect if apz.drag.enabled is also true.
 *
 * \li\b apz.enlarge_displayport_when_clipped
 * Pref that enables enlarging of the displayport along one axis when the
 * generated displayport's size is beyond that of the scrollable rect on the
 * opposite axis.
 *
 * \li\b apz.fling_accel_min_fling_velocity
 * The minimum velocity of the second fling, and the minimum velocity of the
 * previous fling animation at the point of interruption, for the new fling to
 * be considered for fling acceleration.
 * Units: screen pixels per milliseconds
 *
 * \li\b apz.fling_accel_min_pan_velocity
 * The minimum velocity during the pan gesture that causes a fling for that
 * fling to be considered for fling acceleration.
 * Units: screen pixels per milliseconds
 *
 * \li\b apz.fling_accel_max_pause_interval_ms
 * The maximum time that is allowed to elapse between the touch start event that
 * interrupts the previous fling, and the touch move that initiates panning for
 * the current fling, for that fling to be considered for fling acceleration.
 * Units: milliseconds
 *
 * \li\b apz.fling_accel_base_mult
 * \li\b apz.fling_accel_supplemental_mult
 * When applying an acceleration on a fling, the new computed velocity is
 * (new_fling_velocity * base_mult) + (old_velocity * supplemental_mult).
 * The base_mult and supplemental_mult multiplier values are controlled by
 * these prefs. Note that "old_velocity" here is the initial velocity of the
 * previous fling _after_ acceleration was applied to it (if applicable).
 *
 * \li\b apz.fling_curve_function_x1
 * \li\b apz.fling_curve_function_y1
 * \li\b apz.fling_curve_function_x2
 * \li\b apz.fling_curve_function_y2
 * \li\b apz.fling_curve_threshold_inches_per_ms
 * These five parameters define a Bezier curve function and threshold used to
 * increase the actual velocity relative to the user's finger velocity. When the
 * finger velocity is below the threshold (or if the threshold is not positive),
 * the velocity is used as-is. If the finger velocity exceeds the threshold
 * velocity, then the function defined by the curve is applied on the part of
 * the velocity that exceeds the threshold. Note that the upper bound of the
 * velocity is still specified by the \b apz.max_velocity_inches_per_ms pref,
 * and the function will smoothly curve the velocity from the threshold to the
 * max. In general the function parameters chosen should define an ease-out
 * curve in order to increase the velocity in this range, or an ease-in curve to
 * decrease the velocity. A straight-line curve is equivalent to disabling the
 * curve entirely by setting the threshold to -1. The max velocity pref must
 * also be set in order for the curving to take effect, as it defines the upper
 * bound of the velocity curve.\n
 * The points (x1, y1) and (x2, y2) used as the two intermediate control points
 * in the cubic bezier curve; the first and last points are (0,0) and (1,1).\n
 * Some example values for these prefs can be found at\n
 * https://searchfox.org/mozilla-central/rev/f82d5c549f046cb64ce5602bfd894b7ae807c8f8/dom/animation/ComputedTimingFunction.cpp#27-33
 *
 * \li\b apz.fling_friction
 * Amount of friction applied during flings. This is used in the following
 * formula: v(t1) = v(t0) * (1 - f)^(t1 - t0), where v(t1) is the velocity
 * for a new sample, v(t0) is the velocity at the previous sample, f is the
 * value of this pref, and (t1 - t0) is the amount of time, in milliseconds,
 * that has elapsed between the two samples.\n
 * NOTE: Not currently used in Android fling calculations.
 *
 * \li\b apz.fling_min_velocity_threshold
 * Minimum velocity for a fling to actually kick off. If the user pans and lifts
 * their finger such that the velocity is smaller than or equal to this amount,
 * no fling is initiated.\n
 * Units: screen pixels per millisecond
 *
 * \li\b apz.fling_stop_on_tap_threshold
 * When flinging, if the velocity is above this number, then a tap on the
 * screen will stop the fling without dispatching a tap to content. If the
 * velocity is below this threshold a tap will also be dispatched.
 * Note: when modifying this pref be sure to run the APZC gtests as some of
 * them depend on the value of this pref.\n
 * Units: screen pixels per millisecond
 *
 * \li\b apz.fling_stopped_threshold
 * When flinging, if the velocity goes below this number, we just stop the
 * animation completely. This is to prevent asymptotically approaching 0
 * velocity and rerendering unnecessarily.\n
 * Units: screen pixels per millisecond.\n
 * NOTE: Should not be set to anything
 * other than 0.0 for Android except for tests to disable flings.
 *
 * \li\b apz.keyboard.enabled
 * Determines whether scrolling with the keyboard will be allowed to be handled
 * by APZ.
 *
 * \li\b apz.keyboard.passive-listeners
 * When enabled, APZ will interpret the passive event listener flag to mean
 * that the event listener won't change the focused element or selection of
 * the page. With this, web content can use passive key listeners and not have
 * keyboard APZ disabled.
 *
 * \li\b apz.max_tap_time
 * Maximum time for a touch on the screen and corresponding lift of the finger
 * to be considered a tap. This also applies to double taps, except that it is
 * used both for the interval between the first touchdown and first touchup,
 * and for the interval between the first touchup and the second touchdown.\n
 * Units: milliseconds.
 *
 * \li\b apz.max_velocity_inches_per_ms
 * Maximum velocity.  Velocity will be capped at this value if a faster fling
 * occurs.  Negative values indicate unlimited velocity.\n
 * Units: (real-world, i.e. screen) inches per millisecond
 *
 * \li\b apz.max_velocity_queue_size
 * Maximum size of velocity queue. The queue contains last N velocity records.
 * On touch end we calculate the average velocity in order to compensate
 * touch/mouse drivers misbehaviour.
 *
 * \li\b apz.min_skate_speed
 * Minimum amount of speed along an axis before we switch to "skate" multipliers
 * rather than using the "stationary" multipliers.\n
 * Units: CSS pixels per millisecond
 *
 * \li\b apz.one_touch_pinch.enabled
 * Whether or not the "one-touch-pinch" gesture (for zooming with one finger)
 * is enabled or not.
 *
 * \li\b apz.overscroll.enabled
 * Pref that enables overscrolling. If this is disabled, excess scroll that
 * cannot be handed off is discarded.
 *
 * \li\b apz.overscroll.min_pan_distance_ratio
 * The minimum ratio of the pan distance along one axis to the pan distance
 * along the other axis needed to initiate overscroll along the first axis
 * during panning.
 *
 * \li\b apz.overscroll.stretch_factor
 * How much overscrolling can stretch content along an axis.
 * The maximum stretch along an axis is a factor of (1 + kStretchFactor).
 * (So if kStretchFactor is 0, you can't stretch at all; if kStretchFactor
 * is 1, you can stretch at most by a factor of 2).
 *
 * \li\b apz.overscroll.stop_distance_threshold
 * \li\b apz.overscroll.stop_velocity_threshold
 * Thresholds for stopping the overscroll animation. When both the distance
 * and the velocity fall below their thresholds, we stop oscillating.\n
 * Units: screen pixels (for distance)
 *        screen pixels per millisecond (for velocity)
 *
 * \li\b apz.overscroll.spring_stiffness
 * The spring stiffness constant for the overscroll mass-spring-damper model.
 *
 * \li\b apz.overscroll.damping
 * The damping constant for the overscroll mass-spring-damper model.
 *
 * \li\b apz.overscroll.max_velocity
 * The maximum velocity (in ParentLayerPixels per millisecond) allowed when
 * initiating the overscroll snap-back animation.
 *
 * \li\b apz.paint_skipping.enabled
 * When APZ is scrolling and sending repaint requests to the main thread, often
 * the main thread doesn't actually need to do a repaint. This pref allows the
 * main thread to skip doing those repaints in cases where it doesn't need to.
 *
 * \li\b apz.pinch_lock.mode
 * The preferred pinch locking style. See PinchLockMode for possible values.
 *
 * \li\b apz.pinch_lock.scroll_lock_threshold
 * Pinch locking is triggered if the user scrolls more than this distance
 * and pinches less than apz.pinch_lock.span_lock_threshold.\n
 * Units: (real-world, i.e. screen) inches
 *
 * \li\b apz.pinch_lock.span_breakout_threshold
 * Distance in inches the user must pinch before lock can be broken.\n
 * Units: (real-world, i.e. screen) inches measured between two touch points
 *
 * \li\b apz.pinch_lock.span_lock_threshold
 * Pinch locking is triggered if the user pinches less than this distance
 * and scrolls more than apz.pinch_lock.scroll_lock_threshold.\n
 * Units: (real-world, i.e. screen) inches measured between two touch points
 *
 * \li\b apz.pinch_lock.buffer_max_age
 * To ensure that pinch locking threshold calculations are not affected by
 * variations in touch screen sensitivity, calculations draw from a buffer of
 * recent events. This preference specifies the maximum time that events are
 * held in this buffer.
 * Units: milliseconds
 *
 * \li\b apz.popups.enabled
 * Determines whether APZ is used for XUL popup widgets with remote content.
 * Ideally, this should always be true, but it is currently not well tested, and
 * has known issues, so needs to be prefable.
 *
 * \li\b apz.record_checkerboarding
 * Whether or not to record detailed info on checkerboarding events.
 *
 * \li\b apz.second_tap_tolerance
 * Constant describing the tolerance in distance we use, multiplied by the
 * device DPI, within which a second tap is counted as part of a gesture
 * continuing from the first tap. Making this larger allows the user more
 * distance between the first and second taps in a "double tap" or "one touch
 * pinch" gesture.\n
 * Units: (real-world, i.e. screen) inches
 *
 * \li\b apz.test.logging_enabled
 * Enable logging of APZ test data (see bug 961289).
 *
 * \li\b apz.touch_move_tolerance
 * See the description for apz.touch_start_tolerance below. This is a similar
 * threshold, except it is used to suppress touchmove events from being
 * delivered to content for NON-scrollable frames (or more precisely, for APZCs
 * where ArePointerEventsConsumable returns false).\n Units: (real-world, i.e.
 * screen) inches
 *
 * \li\b apz.touch_start_tolerance
 * Constant describing the tolerance in distance we use, multiplied by the
 * device DPI, before we start panning the screen. This is to prevent us from
 * accidentally processing taps as touch moves, and from very short/accidental
 * touches moving the screen. touchmove events are also not delivered to content
 * within this distance on scrollable frames.\n
 * Units: (real-world, i.e. screen) inches
 *
 * \li\b apz.velocity_bias
 * How much to adjust the displayport in the direction of scrolling. This value
 * is multiplied by the velocity and added to the displayport offset.
 *
 * \li\b apz.velocity_relevance_time_ms
 * When computing a fling velocity from the most recently stored velocity
 * information, only velocities within the most X milliseconds are used.
 * This pref controls the value of X.\n
 * Units: ms
 *
 * \li\b apz.x_skate_size_multiplier
 * \li\b apz.y_skate_size_multiplier
 * The multiplier we apply to the displayport size if it is skating (current
 * velocity is above \b apz.min_skate_speed). We prefer to increase the size of
 * the Y axis because it is more natural in the case that a user is reading a
 * page page that scrolls up/down. Note that one, both or neither of these may
 * be used at any instant.\n In general we want \b
 * apz.[xy]_skate_size_multiplier to be smaller than the corresponding
 * stationary size multiplier because when panning fast we would like to paint
 * less and get faster, more predictable paint times. When panning slowly we
 * can afford to paint more even though it's slower.
 *
 * \li\b apz.x_stationary_size_multiplier
 * \li\b apz.y_stationary_size_multiplier
 * The multiplier we apply to the displayport size if it is not skating (see
 * documentation for the skate size multipliers above).
 *
 * \li\b apz.x_skate_highmem_adjust
 * \li\b apz.y_skate_highmem_adjust
 * On high memory systems, we adjust the displayport during skating
 * to be larger so we can reduce checkerboarding.
 *
 * \li\b apz.zoom_animation_duration_ms
 * This controls how long the zoom-to-rect animation takes.\n
 * Units: ms
 *
 * \li\b apz.scale_repaint_delay_ms
 * How long to delay between repaint requests during a scale.
 * A negative number prevents repaint requests during a scale.\n
 * Units: ms
 */

/**
 * Computed time function used for sampling frames of a zoom to animation.
 */
StaticAutoPtr<StyleComputedTimingFunction> gZoomAnimationFunction;

/**
 * Computed time function used for curving up velocity when it gets high.
 */
StaticAutoPtr<StyleComputedTimingFunction> gVelocityCurveFunction;

/**
 * The estimated duration of a paint for the purposes of calculating a new
 * displayport, in milliseconds.
 */
static const double kDefaultEstimatedPaintDurationMs = 50;

/**
 * Returns true if this is a high memory system and we can use
 * extra memory for a larger displayport to reduce checkerboarding.
 */
static bool gIsHighMemSystem = false;
static bool IsHighMemSystem() { return gIsHighMemSystem; }

// An RAII class to hide the dynamic toolbar on Android.
class MOZ_RAII AutoDynamicToolbarHider final {
 public:
  explicit AutoDynamicToolbarHider(AsyncPanZoomController* aApzc)
      : mApzc(aApzc) {
    MOZ_ASSERT(mApzc);
  }
  ~AutoDynamicToolbarHider() {
    if (mHideDynamicToolbar) {
      RefPtr<GeckoContentController> controller =
          mApzc->GetGeckoContentController();
      controller->HideDynamicToolbar(mApzc->GetGuid());
    }
  }

  void Hide() { mHideDynamicToolbar = true; }

  friend class AsyncPanZoomController;

 private:
  AsyncPanZoomController* mApzc;
  bool mHideDynamicToolbar = false;
};

AsyncPanZoomAnimation* PlatformSpecificStateBase::CreateFlingAnimation(
    AsyncPanZoomController& aApzc, const FlingHandoffState& aHandoffState,
    float aPLPPI) {
  return new GenericFlingAnimation<DesktopFlingPhysics>(aApzc, aHandoffState,
                                                        aPLPPI);
}

UniquePtr<VelocityTracker> PlatformSpecificStateBase::CreateVelocityTracker(
    Axis* aAxis) {
  return MakeUnique<SimpleVelocityTracker>(aAxis);
}

// This is a helper class for populating
// AsyncPanZoomController::mUpdatesSinceLastSample when the visual scroll offset
// or zoom level changes.
//
// The class records the current offset and zoom level in its constructor, and
// again in the destructor, and if they have changed, records a compositor
// scroll update with the Source provided in the constructor.
//
// This allows tracking the source of compositor scroll updates in higher-level
// functions such as AttemptScroll or NotifyLayersUpdated, rather than having
// to propagate the source into lower-level functions such as
// SetVisualScrollOffset.
//
// Note however that there is a limit to how far up the call stack this class
// can be used: mRecursiveMutex must be held for the duration of the object's
// lifetime (and to ensure this, the constructor takes a proof-of-lock
// parameter). This is necessary because otherwise, the class could record
// a change to the scroll offset or zoom made by another thread in between
// construction and destruction, for which the source would be incorrect.
class MOZ_STACK_CLASS AsyncPanZoomController::AutoRecordCompositorScrollUpdate
    final {
 public:
  AutoRecordCompositorScrollUpdate(
      AsyncPanZoomController* aApzc, CompositorScrollUpdate::Source aSource,
      const RecursiveMutexAutoLock& aProofOfApzcLock)
      : mApzc(aApzc),
        mProofOfApzcLock(aProofOfApzcLock),
        mSource(aSource),
        mPreviousMetrics(aApzc->GetCurrentMetricsForCompositorScrollUpdate(
            aProofOfApzcLock)) {}
  ~AutoRecordCompositorScrollUpdate() {
    if (!mApzc->IsRootContent()) {
      // Compositor scroll updates are only recorded for the root content APZC.
      // This check may need to be relaxed in bug 1861329, if we start to allow
      // some subframes to move the dynamic toolbar.
      return;
    }
    CompositorScrollUpdate::Metrics newMetrics =
        mApzc->GetCurrentMetricsForCompositorScrollUpdate(mProofOfApzcLock);
    if (newMetrics != mPreviousMetrics) {
      mApzc->mUpdatesSinceLastSample.push_back({newMetrics, mSource});
    }
  }

 private:
  AsyncPanZoomController* mApzc;
  const RecursiveMutexAutoLock& mProofOfApzcLock;
  CompositorScrollUpdate::Source mSource;
  CompositorScrollUpdate::Metrics mPreviousMetrics;
};

SampleTime AsyncPanZoomController::GetFrameTime() const {
  APZCTreeManager* treeManagerLocal = GetApzcTreeManager();
  return treeManagerLocal ? treeManagerLocal->GetFrameTime()
                          : SampleTime::FromNow();
}

bool AsyncPanZoomController::IsZero(const ParentLayerPoint& aPoint) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  return layers::IsZero(ToCSSPixels(aPoint));
}

bool AsyncPanZoomController::IsZero(ParentLayerCoord aCoord) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  return FuzzyEqualsAdditive(ToCSSPixels(aCoord), CSSCoord(),
                             COORDINATE_EPSILON);
}

bool AsyncPanZoomController::FuzzyGreater(ParentLayerCoord aCoord1,
                                          ParentLayerCoord aCoord2) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return ToCSSPixels(aCoord1 - aCoord2) > COORDINATE_EPSILON;
}

class StateChangeNotificationBlocker final {
 public:
  explicit StateChangeNotificationBlocker(AsyncPanZoomController* aApzc)
      : mApzc(aApzc) {
    RecursiveMutexAutoLock lock(mApzc->mRecursiveMutex);
    mInitialState = mApzc->mState;
    mApzc->mNotificationBlockers++;
  }

  StateChangeNotificationBlocker(const StateChangeNotificationBlocker&) =
      delete;
  StateChangeNotificationBlocker(StateChangeNotificationBlocker&& aOther)
      : mApzc(aOther.mApzc), mInitialState(aOther.mInitialState) {
    aOther.mApzc = nullptr;
  }

  ~StateChangeNotificationBlocker() {
    if (!mApzc) {  // moved-from
      return;
    }
    AsyncPanZoomController::PanZoomState newState;
    {
      RecursiveMutexAutoLock lock(mApzc->mRecursiveMutex);
      mApzc->mNotificationBlockers--;
      newState = mApzc->mState;
    }
    mApzc->DispatchStateChangeNotification(mInitialState, newState);
  }

 private:
  AsyncPanZoomController* mApzc;
  AsyncPanZoomController::PanZoomState mInitialState;
};

class ThreadSafeStateChangeNotificationBlocker final {
 public:
  explicit ThreadSafeStateChangeNotificationBlocker(
      AsyncPanZoomController* aApzc) {
    RecursiveMutexAutoLock lock(aApzc->mRecursiveMutex);
    mApzcPtr = RefPtr(aApzc);
    mApzcPtr->mNotificationBlockers++;
    mInitialState = mApzcPtr->mState;
  }

  ThreadSafeStateChangeNotificationBlocker(
      const StateChangeNotificationBlocker&) = delete;
  ThreadSafeStateChangeNotificationBlocker(
      ThreadSafeStateChangeNotificationBlocker&& aOther)
      : mApzcPtr(std::move(aOther.mApzcPtr)),
        mInitialState(aOther.mInitialState) {
    aOther.mApzcPtr = nullptr;
  }

  ~ThreadSafeStateChangeNotificationBlocker() {
    // The point of the ThreadSafeStateChangeNotificationBlocker is to keep a
    // live reference to an APZC. If this reference doesn't exist, then it must
    // have been moved from, and the other state in the object isn't valid, so
    // we early out
    if (mApzcPtr == nullptr) {
      return;
    }
    AsyncPanZoomController::PanZoomState newState;
    {
      RecursiveMutexAutoLock lock(mApzcPtr->mRecursiveMutex);
      mApzcPtr->mNotificationBlockers--;
      newState = mApzcPtr->mState;
    }
    mApzcPtr->DispatchStateChangeNotification(mInitialState, newState);
  }

 private:
  RefPtr<AsyncPanZoomController> mApzcPtr;
  AsyncPanZoomController::PanZoomState mInitialState;
};

/**
 * An RAII class to temporarily apply async test attributes to the provided
 * AsyncPanZoomController.
 *
 * This class should be used in the implementation of any AsyncPanZoomController
 * method that queries the async scroll offset or async zoom (this includes
 * the async layout viewport offset, since modifying the async scroll offset
 * may result in the layout viewport moving as well).
 */
class MOZ_RAII AutoApplyAsyncTestAttributes final {
 public:
  explicit AutoApplyAsyncTestAttributes(
      const AsyncPanZoomController*,
      const RecursiveMutexAutoLock& aProofOfLock);
  ~AutoApplyAsyncTestAttributes();

 private:
  AsyncPanZoomController* mApzc;
  FrameMetrics mPrevFrameMetrics;
  ParentLayerPoint mPrevOverscroll;
  const RecursiveMutexAutoLock& mProofOfLock;
};

AutoApplyAsyncTestAttributes::AutoApplyAsyncTestAttributes(
    const AsyncPanZoomController* aApzc,
    const RecursiveMutexAutoLock& aProofOfLock)
    // Having to use const_cast here seems less ugly than the alternatives
    // of making several members of AsyncPanZoomController that
    // ApplyAsyncTestAttributes() modifies |mutable|, or several methods that
    // query the async transforms non-const.
    : mApzc(const_cast<AsyncPanZoomController*>(aApzc)),
      mPrevFrameMetrics(aApzc->Metrics()),
      mPrevOverscroll(aApzc->GetOverscrollAmountInternal()),
      mProofOfLock(aProofOfLock) {
  mApzc->ApplyAsyncTestAttributes(aProofOfLock);
}

AutoApplyAsyncTestAttributes::~AutoApplyAsyncTestAttributes() {
  mApzc->UnapplyAsyncTestAttributes(mProofOfLock, mPrevFrameMetrics,
                                    mPrevOverscroll);
}

class ZoomAnimation : public AsyncPanZoomAnimation {
 public:
  ZoomAnimation(AsyncPanZoomController& aApzc, const CSSPoint& aStartOffset,
                const CSSToParentLayerScale& aStartZoom,
                const CSSPoint& aEndOffset,
                const CSSToParentLayerScale& aEndZoom)
      : mApzc(aApzc),
        mTotalDuration(TimeDuration::FromMilliseconds(
            StaticPrefs::apz_zoom_animation_duration_ms())),
        mStartOffset(aStartOffset),
        mStartZoom(aStartZoom),
        mEndOffset(aEndOffset),
        mEndZoom(aEndZoom) {}

  virtual bool DoSample(FrameMetrics& aFrameMetrics,
                        const TimeDuration& aDelta) override {
    mDuration += aDelta;
    double animPosition = mDuration / mTotalDuration;

    if (animPosition >= 1.0) {
      aFrameMetrics.SetZoom(mEndZoom);
      mApzc.SetVisualScrollOffset(mEndOffset);
      return false;
    }

    // Sample the zoom at the current time point.  The sampled zoom
    // will affect the final computed resolution.
    float sampledPosition =
        gZoomAnimationFunction->At(animPosition, /* aBeforeFlag = */ false);

    // We scale the scrollOffset linearly with sampledPosition, so the zoom
    // needs to scale inversely to match.
    if (mStartZoom == CSSToParentLayerScale(0) ||
        mEndZoom == CSSToParentLayerScale(0)) {
      return false;
    }

    aFrameMetrics.SetZoom(
        CSSToParentLayerScale(1 / (sampledPosition / mEndZoom.scale +
                                   (1 - sampledPosition) / mStartZoom.scale)));

    mApzc.SetVisualScrollOffset(CSSPoint::FromUnknownPoint(gfx::Point(
        mEndOffset.x * sampledPosition + mStartOffset.x * (1 - sampledPosition),
        mEndOffset.y * sampledPosition +
            mStartOffset.y * (1 - sampledPosition))));
    return true;
  }

  virtual bool WantsRepaints() override { return true; }

 private:
  AsyncPanZoomController& mApzc;

  TimeDuration mDuration;
  const TimeDuration mTotalDuration;

  // Old metrics from before we started a zoom animation. This is only valid
  // when we are in the "ANIMATED_ZOOM" state. This is used so that we can
  // interpolate between the start and end frames. We only use the
  // |mViewportScrollOffset| and |mResolution| fields on this.
  CSSPoint mStartOffset;
  CSSToParentLayerScale mStartZoom;

  // Target metrics for a zoom to animation. This is only valid when we are in
  // the "ANIMATED_ZOOM" state. We only use the |mViewportScrollOffset| and
  // |mResolution| fields on this.
  CSSPoint mEndOffset;
  CSSToParentLayerScale mEndZoom;
};

/*static*/
void AsyncPanZoomController::InitializeGlobalState() {
  static bool sInitialized = false;
  if (sInitialized) return;
  sInitialized = true;

  MOZ_ASSERT(NS_IsMainThread());

  gZoomAnimationFunction = new StyleComputedTimingFunction(
      StyleComputedTimingFunction::Keyword(StyleTimingKeyword::Ease));
  ClearOnShutdown(&gZoomAnimationFunction);
  gVelocityCurveFunction =
      new StyleComputedTimingFunction(StyleComputedTimingFunction::CubicBezier(
          StaticPrefs::apz_fling_curve_function_x1_AtStartup(),
          StaticPrefs::apz_fling_curve_function_y1_AtStartup(),
          StaticPrefs::apz_fling_curve_function_x2_AtStartup(),
          StaticPrefs::apz_fling_curve_function_y2_AtStartup()));
  ClearOnShutdown(&gVelocityCurveFunction);

  uint64_t sysmem = PR_GetPhysicalMemorySize();
  uint64_t threshold = 1LL << 32;  // 4 GB in bytes
  gIsHighMemSystem = sysmem >= threshold;

  PlatformSpecificState::InitializeGlobalState();
}

AsyncPanZoomController::AsyncPanZoomController(
    LayersId aLayersId, APZCTreeManager* aTreeManager,
    const RefPtr<InputQueue>& aInputQueue,
    GeckoContentController* aGeckoContentController, GestureBehavior aGestures)
    : mLayersId(aLayersId),
      mGeckoContentController(aGeckoContentController),
      mRefPtrMonitor("RefPtrMonitor"),
      // mTreeManager must be initialized before GetFrameTime() is called
      mTreeManager(aTreeManager),
      mRecursiveMutex("AsyncPanZoomController"),
      mLastContentPaintMetrics(mLastContentPaintMetadata.GetMetrics()),
      mPanDirRestricted(false),
      mPinchLocked(false),
      mPinchEventBuffer(TimeDuration::FromMilliseconds(
          StaticPrefs::apz_pinch_lock_buffer_max_age_AtStartup())),
      mTouchScrollEventBuffer(
          TimeDuration::FromMilliseconds(
              StaticPrefs::apz_touch_scroll_buffer_max_age_AtStartup()),
          2),
      mZoomConstraints(false, false,
                       mScrollMetadata.GetMetrics().GetDevPixelsPerCSSPixel() *
                           ViewportMinScale() / ParentLayerToScreenScale(1),
                       mScrollMetadata.GetMetrics().GetDevPixelsPerCSSPixel() *
                           ViewportMaxScale() / ParentLayerToScreenScale(1)),
      mLastSampleTime(GetFrameTime()),
      mLastCheckerboardReport(GetFrameTime()),
      mOverscrollEffect(MakeUnique<OverscrollEffect>(*this)),
      mState(NOTHING),
      mX(this),
      mY(this),
      mNotificationBlockers(0),
      mInputQueue(aInputQueue),
      mPinchPaintTimerSet(false),
      mDelayedTransformEnd(false),
      mTestAttributeAppliers(0),
      mTestHasAsyncKeyScrolled(false),
      mCheckerboardEventLock("APZCBELock") {
  if (aGestures == USE_GESTURE_DETECTOR) {
    mGestureEventListener = new GestureEventListener(this);
  }
  // Put one default-constructed sampled state in the queue.
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mSampledState.emplace_back();
}

AsyncPanZoomController::~AsyncPanZoomController() { MOZ_ASSERT(IsDestroyed()); }

PlatformSpecificStateBase* AsyncPanZoomController::GetPlatformSpecificState() {
  if (!mPlatformSpecificState) {
    mPlatformSpecificState = MakeUnique<PlatformSpecificState>();
  }
  return mPlatformSpecificState.get();
}

already_AddRefed<GeckoContentController>
AsyncPanZoomController::GetGeckoContentController() const {
  MonitorAutoLock lock(mRefPtrMonitor);
  RefPtr<GeckoContentController> controller = mGeckoContentController;
  return controller.forget();
}

already_AddRefed<GestureEventListener>
AsyncPanZoomController::GetGestureEventListener() const {
  MonitorAutoLock lock(mRefPtrMonitor);
  RefPtr<GestureEventListener> listener = mGestureEventListener;
  return listener.forget();
}

const RefPtr<InputQueue>& AsyncPanZoomController::GetInputQueue() const {
  return mInputQueue;
}

void AsyncPanZoomController::Destroy() {
  AssertOnUpdaterThread();

  CancelAnimation(CancelAnimationFlags::ScrollSnap);

  {  // scope the lock
    MonitorAutoLock lock(mRefPtrMonitor);
    mGeckoContentController = nullptr;
    mGestureEventListener = nullptr;
  }
  mParent = nullptr;
  mTreeManager = nullptr;
}

bool AsyncPanZoomController::IsDestroyed() const {
  return mTreeManager == nullptr;
}

float AsyncPanZoomController::GetDPI() const {
  if (APZCTreeManager* localPtr = mTreeManager) {
    return localPtr->GetDPI();
  }
  // If this APZC has been destroyed then this value is not going to be
  // used for anything that the user will end up seeing, so we can just
  // return 0.
  return 0.0;
}

ScreenCoord AsyncPanZoomController::GetTouchStartTolerance() const {
  return (StaticPrefs::apz_touch_start_tolerance() * GetDPI());
}

ScreenCoord AsyncPanZoomController::GetTouchMoveTolerance() const {
  return (StaticPrefs::apz_touch_move_tolerance() * GetDPI());
}

ScreenCoord AsyncPanZoomController::GetSecondTapTolerance() const {
  return (StaticPrefs::apz_second_tap_tolerance() * GetDPI());
}

/* static */ AsyncPanZoomController::AxisLockMode
AsyncPanZoomController::GetAxisLockMode() {
  return static_cast<AxisLockMode>(StaticPrefs::apz_axis_lock_mode());
}

bool AsyncPanZoomController::UsingStatefulAxisLock() const {
  return (GetAxisLockMode() == AxisLockMode::STANDARD ||
          GetAxisLockMode() == AxisLockMode::STICKY ||
          GetAxisLockMode() == AxisLockMode::BREAKABLE);
}

/* static */ AsyncPanZoomController::PinchLockMode
AsyncPanZoomController::GetPinchLockMode() {
  return static_cast<PinchLockMode>(StaticPrefs::apz_pinch_lock_mode());
}

PointerEventsConsumableFlags AsyncPanZoomController::ArePointerEventsConsumable(
    const TouchBlockState* aBlock, const MultiTouchInput& aInput) const {
  uint32_t touchPoints = aInput.mTouches.Length();
  if (touchPoints == 0) {
    // Cant' do anything with zero touch points
    return {false, false};
  }

  // This logic is simplified, erring on the side of returning true if we're
  // not sure. It's safer to pretend that we can consume the event and then
  // not be able to than vice-versa. But at the same time, we should try hard
  // to return an accurate result, because returning true can trigger a
  // pointercancel event to web content, which can break certain features
  // that are using touch-action and handling the pointermove events.
  //
  // Note that in particular this function can return true if APZ is waiting on
  // the main thread for touch-action information. In this scenario, the
  // APZEventState::MainThreadAgreesEventsAreConsumableByAPZ() function tries
  // to use the main-thread touch-action information to filter out false
  // positives.
  //
  // We could probably enhance this logic to determine things like "we're
  // not pannable, so we can only zoom in, and the zoom is already maxed
  // out, so we're not zoomable either" but no need for that at this point.

  bool pannableX = aBlock->GetOverscrollHandoffChain()->CanScrollInDirection(
      this, ScrollDirection::eHorizontal);
  bool touchActionAllowsX = aBlock->TouchActionAllowsPanningX();
  bool pannableY = (aBlock->GetOverscrollHandoffChain()->CanScrollInDirection(
                        this, ScrollDirection::eVertical) ||
                    // In the case of the root APZC with any dynamic toolbar, it
                    // shoule be pannable if there is room moving the dynamic
                    // toolbar.
                    (IsRootContent() && CanVerticalScrollWithDynamicToolbar()));
  bool touchActionAllowsY = aBlock->TouchActionAllowsPanningY();

  bool pannable;
  bool touchActionAllowsPanning;

  Maybe<ScrollDirection> panDirection =
      aBlock->GetBestGuessPanDirection(aInput);
  if (panDirection == Some(ScrollDirection::eVertical)) {
    pannable = pannableY;
    touchActionAllowsPanning = touchActionAllowsY;
  } else if (panDirection == Some(ScrollDirection::eHorizontal)) {
    pannable = pannableX;
    touchActionAllowsPanning = touchActionAllowsX;
  } else {
    // If we don't have a guessed pan direction, err on the side of returning
    // true.
    pannable = pannableX || pannableY;
    touchActionAllowsPanning = touchActionAllowsX || touchActionAllowsY;
  }

  if (touchPoints == 1) {
    return {pannable, touchActionAllowsPanning};
  }

  bool zoomable = ZoomConstraintsAllowZoom();
  bool touchActionAllowsZoom = aBlock->TouchActionAllowsPinchZoom();

  return {pannable || zoomable,
          touchActionAllowsPanning || touchActionAllowsZoom};
}

nsEventStatus AsyncPanZoomController::HandleDragEvent(
    const MouseInput& aEvent, const AsyncDragMetrics& aDragMetrics,
    OuterCSSCoord aInitialThumbPos, const CSSRect& aInitialScrollableRect) {
  // RDM is a special case where touch events will be synthesized in response
  // to mouse events, and APZ will receive both even though RDM prevent-defaults
  // the mouse events. This is because mouse events don't opt into APZ waiting
  // to check if the event has been prevent-defaulted and are still processed
  // as a result. To handle this, have APZ ignore mouse events when RDM and
  // touch simulation are active.
  bool isRDMTouchSimulationActive = false;
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    isRDMTouchSimulationActive =
        mScrollMetadata.GetIsRDMTouchSimulationActive();
  }

  if (!StaticPrefs::apz_drag_enabled() || isRDMTouchSimulationActive) {
    return nsEventStatus_eIgnore;
  }

  if (!GetApzcTreeManager()) {
    return nsEventStatus_eConsumeNoDefault;
  }

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    if (aEvent.mType == MouseInput::MouseType::MOUSE_UP) {
      if (mState == SCROLLBAR_DRAG) {
        APZC_LOG("%p ending drag\n", this);
        SetState(NOTHING);
      }

      SnapBackIfOverscrolled();

      return nsEventStatus_eConsumeNoDefault;
    }
  }

  HitTestingTreeNodeAutoLock node;
  GetApzcTreeManager()->FindScrollThumbNode(aDragMetrics, mLayersId, node);
  if (!node) {
    APZC_LOG("%p unable to find scrollthumb node with viewid %" PRIu64 "\n",
             this, aDragMetrics.mViewId);
    return nsEventStatus_eConsumeNoDefault;
  }

  if (aEvent.mType == MouseInput::MouseType::MOUSE_DOWN) {
    APZC_LOG("%p starting scrollbar drag\n", this);
    SetState(SCROLLBAR_DRAG);
  }

  if (aEvent.mType != MouseInput::MouseType::MOUSE_MOVE) {
    APZC_LOG("%p discarding event of type %d\n", this, aEvent.mType);
    return nsEventStatus_eConsumeNoDefault;
  }

  const ScrollbarData& scrollbarData = node->GetScrollbarData();
  MOZ_ASSERT(scrollbarData.mScrollbarLayerType ==
             layers::ScrollbarLayerType::Thumb);
  MOZ_ASSERT(scrollbarData.mDirection.isSome());
  ScrollDirection direction = *scrollbarData.mDirection;

  bool isMouseAwayFromThumb = false;
  if (int snapMultiplier = StaticPrefs::slider_snapMultiplier()) {
    // It's fine to ignore the async component of the thumb's transform,
    // because any async transform of the thumb will be in the direction of
    // scrolling, but here we're interested in the other direction.
    ParentLayerRect thumbRect =
        (node->GetTransform() * AsyncTransformMatrix())
            .TransformBounds(LayerRect(node->GetVisibleRect()));
    ScrollDirection otherDirection = GetPerpendicularDirection(direction);
    ParentLayerCoord distance =
        GetAxisStart(otherDirection, thumbRect.DistanceTo(aEvent.mLocalOrigin));
    ParentLayerCoord thumbWidth = GetAxisLength(otherDirection, thumbRect);
    // Avoid triggering this condition spuriously when the thumb is
    // offscreen and its visible region is therefore empty.
    if (thumbWidth > 0 && thumbWidth * snapMultiplier < distance) {
      isMouseAwayFromThumb = true;
      APZC_LOG("%p determined mouse is away from thumb, will snap\n", this);
    }
  }

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  OuterCSSCoord thumbPosition;
  if (isMouseAwayFromThumb) {
    thumbPosition = aInitialThumbPos;
  } else {
    thumbPosition = ConvertScrollbarPoint(aEvent.mLocalOrigin, scrollbarData) -
                    aDragMetrics.mScrollbarDragOffset;
  }

  OuterCSSCoord maxThumbPos = scrollbarData.mScrollTrackLength;
  maxThumbPos -= scrollbarData.mThumbLength;

  float scrollPercent =
      maxThumbPos.value == 0.0f ? 0.0f : (float)(thumbPosition / maxThumbPos);
  APZC_LOG("%p scrollbar dragged to %f percent\n", this, scrollPercent);

  CSSCoord minScrollPosition =
      GetAxisStart(direction, aInitialScrollableRect.TopLeft());
  CSSCoord maxScrollPosition =
      GetAxisStart(direction, aInitialScrollableRect.BottomRight()) -
      GetAxisLength(direction, Metrics().CalculateCompositedSizeInCssPixels());
  CSSCoord scrollPosition =
      minScrollPosition +
      (scrollPercent * (maxScrollPosition - minScrollPosition));

  scrollPosition = std::max(scrollPosition, minScrollPosition);
  scrollPosition = std::min(scrollPosition, maxScrollPosition);

  CSSPoint scrollOffset = Metrics().GetVisualScrollOffset();
  if (direction == ScrollDirection::eHorizontal) {
    scrollOffset.x = scrollPosition;
  } else {
    scrollOffset.y = scrollPosition;
  }
  APZC_LOG("%p set scroll offset to %s from scrollbar drag\n", this,
           ToString(scrollOffset).c_str());
  // Since the scroll position was calculated based on the scrollable rect at
  // the start of the drag, we need to clamp the scroll position in case the
  // scrollable rect has since shrunk.
  ClampAndSetVisualScrollOffset(scrollOffset);
  ScheduleCompositeAndMaybeRepaint();

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::HandleInputEvent(
    const InputData& aEvent,
    const ScreenToParentLayerMatrix4x4& aTransformToApzc) {
  APZThreadUtils::AssertOnControllerThread();

  nsEventStatus rv = nsEventStatus_eIgnore;

  switch (aEvent.mInputType) {
    case MULTITOUCH_INPUT: {
      MultiTouchInput multiTouchInput = aEvent.AsMultiTouchInput();
      RefPtr<GestureEventListener> listener = GetGestureEventListener();
      if (listener) {
        // We only care about screen coordinates in the gesture listener,
        // so we don't bother transforming the event to parent layer coordinates
        rv = listener->HandleInputEvent(multiTouchInput);
        if (rv == nsEventStatus_eConsumeNoDefault) {
          return rv;
        }
      }

      if (!multiTouchInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }

      switch (multiTouchInput.mType) {
        case MultiTouchInput::MULTITOUCH_START:
          rv = OnTouchStart(multiTouchInput);
          break;
        case MultiTouchInput::MULTITOUCH_MOVE:
          rv = OnTouchMove(multiTouchInput);
          break;
        case MultiTouchInput::MULTITOUCH_END:
          rv = OnTouchEnd(multiTouchInput);
          break;
        case MultiTouchInput::MULTITOUCH_CANCEL:
          rv = OnTouchCancel(multiTouchInput);
          break;
      }
      break;
    }
    case PANGESTURE_INPUT: {
      PanGestureInput panGestureInput = aEvent.AsPanGestureInput();
      if (!panGestureInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }

      switch (panGestureInput.mType) {
        case PanGestureInput::PANGESTURE_MAYSTART:
          rv = OnPanMayBegin(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_CANCELLED:
          rv = OnPanCancelled(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_START:
          rv = OnPanBegin(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_PAN:
          rv = OnPan(panGestureInput, FingersOnTouchpad::Yes);
          break;
        case PanGestureInput::PANGESTURE_END:
          rv = OnPanEnd(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_MOMENTUMSTART:
          rv = OnPanMomentumStart(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_MOMENTUMPAN:
          rv = OnPan(panGestureInput, FingersOnTouchpad::No);
          break;
        case PanGestureInput::PANGESTURE_MOMENTUMEND:
          rv = OnPanMomentumEnd(panGestureInput);
          break;
        case PanGestureInput::PANGESTURE_INTERRUPTED:
          rv = OnPanInterrupted(panGestureInput);
          break;
      }
      break;
    }
    case MOUSE_INPUT: {
      MouseInput mouseInput = aEvent.AsMouseInput();
      if (!mouseInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }
      break;
    }
    case SCROLLWHEEL_INPUT: {
      ScrollWheelInput scrollInput = aEvent.AsScrollWheelInput();
      if (!scrollInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }

      rv = OnScrollWheel(scrollInput);
      break;
    }
    case PINCHGESTURE_INPUT: {
      // The APZCTreeManager should take care of ensuring that only root-content
      // APZCs get pinch inputs.
      MOZ_ASSERT(IsRootContent());
      PinchGestureInput pinchInput = aEvent.AsPinchGestureInput();
      if (!pinchInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }

      rv = HandleGestureEvent(pinchInput);
      break;
    }
    case TAPGESTURE_INPUT: {
      TapGestureInput tapInput = aEvent.AsTapGestureInput();
      if (!tapInput.TransformToLocal(aTransformToApzc)) {
        return rv;
      }

      rv = HandleGestureEvent(tapInput);
      break;
    }
    case KEYBOARD_INPUT: {
      const KeyboardInput& keyInput = aEvent.AsKeyboardInput();
      rv = OnKeyboard(keyInput);
      break;
    }
  }

  return rv;
}

nsEventStatus AsyncPanZoomController::HandleGestureEvent(
    const InputData& aEvent) {
  APZThreadUtils::AssertOnControllerThread();

  nsEventStatus rv = nsEventStatus_eIgnore;

  switch (aEvent.mInputType) {
    case PINCHGESTURE_INPUT: {
      // This may be invoked via a one-touch-pinch gesture from
      // GestureEventListener. In that case we want redirect it to the enclosing
      // root-content APZC.
      if (!IsRootContent()) {
        if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
          if (RefPtr<AsyncPanZoomController> root =
                  treeManagerLocal->FindZoomableApzc(this)) {
            rv = root->HandleGestureEvent(aEvent);
          }
        }
        break;
      }
      PinchGestureInput pinchGestureInput = aEvent.AsPinchGestureInput();
      pinchGestureInput.TransformToLocal(GetTransformToThis());
      switch (pinchGestureInput.mType) {
        case PinchGestureInput::PINCHGESTURE_START:
          rv = OnScaleBegin(pinchGestureInput);
          break;
        case PinchGestureInput::PINCHGESTURE_SCALE:
          rv = OnScale(pinchGestureInput);
          break;
        case PinchGestureInput::PINCHGESTURE_FINGERLIFTED:
        case PinchGestureInput::PINCHGESTURE_END:
          rv = OnScaleEnd(pinchGestureInput);
          break;
      }
      break;
    }
    case TAPGESTURE_INPUT: {
      TapGestureInput tapGestureInput = aEvent.AsTapGestureInput();
      tapGestureInput.TransformToLocal(GetTransformToThis());
      switch (tapGestureInput.mType) {
        case TapGestureInput::TAPGESTURE_LONG:
          rv = OnLongPress(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_LONG_UP:
          rv = OnLongPressUp(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_UP:
          rv = OnSingleTapUp(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_CONFIRMED:
          rv = OnSingleTapConfirmed(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_DOUBLE:
          if (!IsRootContent()) {
            if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
              if (AsyncPanZoomController* apzc =
                      treeManagerLocal->FindRootApzcFor(GetLayersId())) {
                rv = apzc->OnDoubleTap(tapGestureInput);
              }
            }
            break;
          }
          rv = OnDoubleTap(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_SECOND:
          rv = OnSecondTap(tapGestureInput);
          break;
        case TapGestureInput::TAPGESTURE_CANCEL:
          rv = OnCancelTap(tapGestureInput);
          break;
      }
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled input event");
      break;
  }

  return rv;
}

void AsyncPanZoomController::StartAutoscroll(const ScreenPoint& aPoint) {
  // Cancel any existing animation.
  CancelAnimation();

  SetState(AUTOSCROLL);
  StartAnimation(do_AddRef(new AutoscrollAnimation(*this, aPoint)));
}

void AsyncPanZoomController::StopAutoscroll() {
  if (mState == AUTOSCROLL) {
    CancelAnimation(TriggeredExternally);
  }
}

nsEventStatus AsyncPanZoomController::OnTouchStart(
    const MultiTouchInput& aEvent) {
  APZC_LOG_DETAIL("got a touch-start in state %s\n", this,
                  ToString(mState).c_str());
  mPanDirRestricted = false;

  switch (mState) {
    case FLING:
    case ANIMATING_ZOOM:
    case SMOOTH_SCROLL:
    case SMOOTHMSD_SCROLL:
    case OVERSCROLL_ANIMATION:
    case WHEEL_SCROLL:
    case KEYBOARD_SCROLL:
    case PAN_MOMENTUM:
    case AUTOSCROLL:
      MOZ_ASSERT(GetCurrentTouchBlock());
      GetCurrentTouchBlock()->GetOverscrollHandoffChain()->CancelAnimations(
          ExcludeOverscroll);
      [[fallthrough]];
    case SCROLLBAR_DRAG:
    case NOTHING: {
      ParentLayerPoint point = GetFirstTouchPoint(aEvent);
      mLastTouch.mPosition = mStartTouch = GetFirstExternalTouchPoint(aEvent);
      StartTouch(point, aEvent.mTimeStamp);
      if (RefPtr<GeckoContentController> controller =
              GetGeckoContentController()) {
        MOZ_ASSERT(GetCurrentTouchBlock());
        const bool canBePanOrZoom =
            GetCurrentTouchBlock()->GetOverscrollHandoffChain()->CanBePanned(
                this) ||
            (ZoomConstraintsAllowDoubleTapZoom() &&
             GetCurrentTouchBlock()->TouchActionAllowsDoubleTapZoom());
        controller->NotifyAPZStateChange(
            GetGuid(), APZStateChange::eStartTouch, canBePanOrZoom,
            Some(GetCurrentTouchBlock()->GetBlockId()));
      }
      mLastTouch.mTimeStamp = mTouchStartTime = aEvent.mTimeStamp;
      SetState(TOUCHING);
      mTouchScrollEventBuffer.push(aEvent);
      break;
    }
    case TOUCHING:
    case PANNING:
    case PANNING_LOCKED_X:
    case PANNING_LOCKED_Y:
    case PINCHING:
      NS_WARNING("Received impossible touch in OnTouchStart");
      break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchMove(
    const MultiTouchInput& aEvent) {
  APZC_LOG_DETAIL("got a touch-move in state %s\n", this,
                  ToString(mState).c_str());
  switch (mState) {
    case FLING:
    case SMOOTHMSD_SCROLL:
    case NOTHING:
    case ANIMATING_ZOOM:
      // May happen if the user double-taps and drags without lifting after the
      // second tap. Ignore the move if this happens.
      return nsEventStatus_eIgnore;

    case TOUCHING: {
      ScreenCoord panThreshold = GetTouchStartTolerance();
      ExternalPoint extPoint = GetFirstExternalTouchPoint(aEvent);
      Maybe<std::pair<MultiTouchInput, MultiTouchInput>> splitEvent;

      // We intentionally skip the UpdateWithTouchAtDevicePoint call when the
      // panThreshold is zero. This ensures more deterministic behaviour during
      // testing. If we call that, Axis::mPos gets updated to the point of this
      // touchmove event, but we "consume" the move to overcome the
      // panThreshold, so it's hard to pan a specific amount reliably from a
      // mochitest.
      if (panThreshold > 0.0f) {
        const float vectorLength = PanVector(extPoint).Length();

        if (vectorLength < panThreshold) {
          UpdateWithTouchAtDevicePoint(aEvent);
          mLastTouch = {extPoint, aEvent.mTimeStamp};

          return nsEventStatus_eIgnore;
        }

        splitEvent = MaybeSplitTouchMoveEvent(aEvent, panThreshold,
                                              vectorLength, extPoint);

        UpdateWithTouchAtDevicePoint(splitEvent ? splitEvent->first : aEvent);
      }

      nsEventStatus result;
      const MultiTouchInput& firstEvent =
          splitEvent ? splitEvent->first : aEvent;
      mTouchScrollEventBuffer.push(firstEvent);

      MOZ_ASSERT(GetCurrentTouchBlock());
      if (GetCurrentTouchBlock()->TouchActionAllowsPanningXY()) {
        // In the calls to StartPanning() below, the first argument needs to be
        // the External position of |firstEvent|.
        // However, instead of computing that using
        // GetFirstExternalTouchPoint(firstEvent), we pass |extPoint| which
        // has been modified by MaybeSplitTouchMoveEvent() to the desired
        // value. This is a workaround for the fact that recomputing the
        // External point would require a round-trip through |mScreenPoint|
        // which is an integer.

        // User tries to trigger a touch behavior. If allowed touch behavior is
        // vertical pan + horizontal pan (touch-action value is equal to AUTO)
        // we can return ConsumeNoDefault status immediately to trigger cancel
        // event further.
        // It should happen independent of the parent type (whether it is
        // scrolling or not).
        StartPanning(extPoint, firstEvent.mTimeStamp);
        result = nsEventStatus_eConsumeNoDefault;
      } else {
        result = StartPanning(extPoint, firstEvent.mTimeStamp);
      }

      if (splitEvent && IsInPanningState()) {
        TrackTouch(splitEvent->second);
        return nsEventStatus_eConsumeNoDefault;
      }

      return result;
    }

    case PANNING:
    case PANNING_LOCKED_X:
    case PANNING_LOCKED_Y:
    case PAN_MOMENTUM:
      TrackTouch(aEvent);
      return nsEventStatus_eConsumeNoDefault;

    case PINCHING:
      // The scale gesture listener should have handled this.
      NS_WARNING(
          "Gesture listener should have handled pinching in OnTouchMove.");
      return nsEventStatus_eIgnore;

    case SMOOTH_SCROLL:
    case WHEEL_SCROLL:
    case KEYBOARD_SCROLL:
    case OVERSCROLL_ANIMATION:
    case AUTOSCROLL:
    case SCROLLBAR_DRAG:
      // Should not receive a touch-move in the OVERSCROLL_ANIMATION state
      // as touch blocks that begin in an overscrolled state cancel the
      // animation. The same is true for wheel scroll animations.
      NS_WARNING("Received impossible touch in OnTouchMove");
      break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchEnd(
    const MultiTouchInput& aEvent) {
  APZC_LOG_DETAIL("got a touch-end in state %s\n", this,
                  ToString(mState).c_str());
  OnTouchEndOrCancel();

  // In case no touch behavior triggered previously we can avoid sending
  // scroll events or requesting content repaint. This condition is added
  // to make tests consistent - in case touch-action is NONE (and therefore
  // no pans/zooms can be performed) we expected neither scroll or repaint
  // events.
  if (mState != NOTHING) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
  }

  switch (mState) {
    case FLING:
      // Should never happen.
      NS_WARNING("Received impossible touch end in OnTouchEnd.");
      [[fallthrough]];
    case ANIMATING_ZOOM:
    case SMOOTHMSD_SCROLL:
    case NOTHING:
      // May happen if the user double-taps and drags without lifting after the
      // second tap. Ignore if this happens.
      return nsEventStatus_eIgnore;

    case TOUCHING:
      // We may have some velocity stored on the axis from move events
      // that were not big enough to trigger scrolling. Clear that out.
      SetVelocityVector(ParentLayerPoint(0, 0));
      MOZ_ASSERT(GetCurrentTouchBlock());
      APZC_LOG("%p still has %u touch points active\n", this,
               GetCurrentTouchBlock()->GetActiveTouchCount());
      // In cases where the user is panning, then taps the second finger without
      // entering a pinch, we will arrive here when the second finger is lifted.
      // However the first finger is still down so we want to remain in state
      // TOUCHING.
      if (GetCurrentTouchBlock()->GetActiveTouchCount() == 0) {
        // It's possible we may be overscrolled if the user tapped during a
        // previous overscroll pan. Make sure to snap back in this situation.
        // An ancestor APZC could be overscrolled instead of this APZC, so
        // walk the handoff chain as well.
        GetCurrentTouchBlock()
            ->GetOverscrollHandoffChain()
            ->SnapBackOverscrolledApzc(this);
        mFlingAccelerator.Reset();
        // SnapBackOverscrolledApzc() will put any APZC it causes to snap back
        // into the OVERSCROLL_ANIMATION state. If that's not us, since we're
        // done TOUCHING enter the NOTHING state.
        if (mState != OVERSCROLL_ANIMATION) {
          SetState(NOTHING);
        }
      }
      return nsEventStatus_eIgnore;

    case PANNING:
    case PANNING_LOCKED_X:
    case PANNING_LOCKED_Y:
    case PAN_MOMENTUM: {
      MOZ_ASSERT(GetCurrentTouchBlock());
      EndTouch(aEvent.mTimeStamp, Axis::ClearAxisLock::Yes);
      return HandleEndOfPan();
    }
    case PINCHING:
      SetState(NOTHING);
      // Scale gesture listener should have handled this.
      NS_WARNING(
          "Gesture listener should have handled pinching in OnTouchEnd.");
      return nsEventStatus_eIgnore;

    case SMOOTH_SCROLL:
    case WHEEL_SCROLL:
    case KEYBOARD_SCROLL:
    case OVERSCROLL_ANIMATION:
    case AUTOSCROLL:
    case SCROLLBAR_DRAG:
      // Should not receive a touch-end in the OVERSCROLL_ANIMATION state
      // as touch blocks that begin in an overscrolled state cancel the
      // animation. The same is true for WHEEL_SCROLL.
      NS_WARNING("Received impossible touch in OnTouchEnd");
      break;
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnTouchCancel(
    const MultiTouchInput& aEvent) {
  APZC_LOG_DETAIL("got a touch-cancel in state %s\n", this,
                  ToString(mState).c_str());
  OnTouchEndOrCancel();
  CancelAnimationAndGestureState();
  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScaleBegin(
    const PinchGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a scale-begin in state %s\n", this,
                  ToString(mState).c_str());

  mPinchLocked = false;
  mPinchPaintTimerSet = false;
  // Note that there may not be a touch block at this point, if we received the
  // PinchGestureEvent directly from widget code without any touch events.
  if (HasReadyTouchBlock() &&
      !GetCurrentTouchBlock()->TouchActionAllowsPinchZoom()) {
    return nsEventStatus_eIgnore;
  }

  // For platforms that don't support APZ zooming, dispatch a message to the
  // content controller, it may want to do something else with this gesture.
  // FIXME: bug 1525793 -- this may need to handle zooming or not on a
  // per-document basis.
  if (!StaticPrefs::apz_allow_zooming()) {
    if (RefPtr<GeckoContentController> controller =
            GetGeckoContentController()) {
      APZC_LOG("%p notifying controller of pinch gesture start\n", this);
      controller->NotifyPinchGesture(
          aEvent.mType, GetGuid(),
          ViewAs<LayoutDevicePixel>(
              aEvent.mFocusPoint,
              PixelCastJustification::
                  LayoutDeviceIsScreenForUntransformedEvent),
          0, aEvent.modifiers);
    }
  }

  SetState(PINCHING);
  glean::apz_zoom::pinchsource.AccumulateSingleSample((int)aEvent.mSource);
  SetVelocityVector(ParentLayerPoint(0, 0));
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mLastZoomFocus =
      aEvent.mLocalFocusPoint - Metrics().GetCompositionBounds().TopLeft();

  mPinchEventBuffer.push(aEvent);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScale(const PinchGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a scale in state %s\n", this, ToString(mState).c_str());

  if (HasReadyTouchBlock() &&
      !GetCurrentTouchBlock()->TouchActionAllowsPinchZoom()) {
    return nsEventStatus_eIgnore;
  }

  if (mState != PINCHING) {
    return nsEventStatus_eConsumeNoDefault;
  }

  mPinchEventBuffer.push(aEvent);
  HandlePinchLocking(aEvent);
  bool allowZoom = ZoomConstraintsAllowZoom() && !mPinchLocked;

  // If we are pinch-locked, this is a two-finger pan.
  // Tracking panning distance and velocity.
  // UpdateWithTouchAtDevicePoint() acquires the tree lock, so
  // it cannot be called while the mRecursiveMutex lock is held.
  if (mPinchLocked) {
    mX.UpdateWithTouchAtDevicePoint(aEvent.mLocalFocusPoint.x,
                                    aEvent.mTimeStamp);
    mY.UpdateWithTouchAtDevicePoint(aEvent.mLocalFocusPoint.y,
                                    aEvent.mTimeStamp);
  }

  // FIXME: bug 1525793 -- this may need to handle zooming or not on a
  // per-document basis.
  if (!StaticPrefs::apz_allow_zooming()) {
    if (RefPtr<GeckoContentController> controller =
            GetGeckoContentController()) {
      APZC_LOG("%p notifying controller of pinch gesture\n", this);
      controller->NotifyPinchGesture(
          aEvent.mType, GetGuid(),
          ViewAs<LayoutDevicePixel>(
              aEvent.mFocusPoint,
              PixelCastJustification::
                  LayoutDeviceIsScreenForUntransformedEvent),
          ViewAs<LayoutDevicePixel>(
              aEvent.mCurrentSpan - aEvent.mPreviousSpan,
              PixelCastJustification::
                  LayoutDeviceIsScreenForUntransformedEvent),
          aEvent.modifiers);
    }
  }

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    AutoRecordCompositorScrollUpdate csu(
        this, CompositorScrollUpdate::Source::UserInteraction, lock);

    // Only the root APZC is zoomable, and the root APZC is not allowed to have
    // different x and y scales. If it did, the calculations in this function
    // would have to be adjusted (as e.g. it would no longer be valid to take
    // the minimum or maximum of the ratios of the widths and heights of the
    // page rect and the composition bounds).
    MOZ_ASSERT(Metrics().IsRootContent());

    CSSToParentLayerScale userZoom = Metrics().GetZoom();
    ParentLayerPoint focusPoint =
        aEvent.mLocalFocusPoint - Metrics().GetCompositionBounds().TopLeft();
    CSSPoint cssFocusPoint;
    if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
      cssFocusPoint = focusPoint / Metrics().GetZoom();
    }

    ParentLayerPoint focusChange = mLastZoomFocus - focusPoint;
    mLastZoomFocus = focusPoint;
    // If displacing by the change in focus point will take us off page bounds,
    // then reduce the displacement such that it doesn't.
    focusChange.x -= mX.DisplacementWillOverscrollAmount(focusChange.x);
    focusChange.y -= mY.DisplacementWillOverscrollAmount(focusChange.y);
    if (userZoom != CSSToParentLayerScale(0)) {
      ScrollBy(focusChange / userZoom);
    }

    // If the span is zero or close to it, we don't want to process this zoom
    // change because we're going to get wonky numbers for the spanRatio. So
    // let's bail out here. Note that we do this after the focus-change-scroll
    // above, so that if we have a pinch with zero span but changing focus,
    // such as generated by some Synaptics touchpads on Windows, we still
    // scroll properly.
    float prevSpan = aEvent.mPreviousSpan;
    if (fabsf(prevSpan) <= EPSILON || fabsf(aEvent.mCurrentSpan) <= EPSILON) {
      // We might have done a nonzero ScrollBy above, so update metrics and
      // repaint/recomposite
      ScheduleCompositeAndMaybeRepaint();
      return nsEventStatus_eConsumeNoDefault;
    }
    float spanRatio = aEvent.mCurrentSpan / aEvent.mPreviousSpan;

    // When we zoom in with focus, we can zoom too much towards the boundaries
    // that we actually go over them. These are the needed displacements along
    // either axis such that we don't overscroll the boundaries when zooming.
    CSSPoint neededDisplacement;

    CSSToParentLayerScale realMinZoom = mZoomConstraints.mMinZoom;
    CSSToParentLayerScale realMaxZoom = mZoomConstraints.mMaxZoom;
    realMinZoom.scale =
        std::max(realMinZoom.scale, Metrics().GetCompositionBounds().Width() /
                                        Metrics().GetScrollableRect().Width());
    realMinZoom.scale =
        std::max(realMinZoom.scale, Metrics().GetCompositionBounds().Height() /
                                        Metrics().GetScrollableRect().Height());
    if (realMaxZoom < realMinZoom) {
      realMaxZoom = realMinZoom;
    }

    bool doScale = allowZoom && ((spanRatio > 1.0 && userZoom < realMaxZoom) ||
                                 (spanRatio < 1.0 && userZoom > realMinZoom));

    if (doScale) {
      spanRatio = std::clamp(spanRatio, realMinZoom.scale / userZoom.scale,
                             realMaxZoom.scale / userZoom.scale);

      // Note that the spanRatio here should never put us into OVERSCROLL_BOTH
      // because up above we clamped it.
      neededDisplacement.x =
          -mX.ScaleWillOverscrollAmount(spanRatio, cssFocusPoint.x);
      neededDisplacement.y =
          -mY.ScaleWillOverscrollAmount(spanRatio, cssFocusPoint.y);

      ScaleWithFocus(spanRatio, cssFocusPoint);

      if (neededDisplacement != CSSPoint()) {
        ScrollBy(neededDisplacement);
      }

      // We don't want to redraw on every scale, so throttle it.
      if (!mPinchPaintTimerSet) {
        const int delay = StaticPrefs::apz_scale_repaint_delay_ms();
        if (delay >= 0) {
          if (RefPtr<GeckoContentController> controller =
                  GetGeckoContentController()) {
            mPinchPaintTimerSet = true;
            controller->PostDelayedTask(
                NewRunnableMethod(
                    "layers::AsyncPanZoomController::"
                    "DoDelayedRequestContentRepaint",
                    this,
                    &AsyncPanZoomController::DoDelayedRequestContentRepaint),
                delay);
          }
        }
      } else if (apz::AboutToCheckerboard(mLastContentPaintMetrics,
                                          Metrics())) {
        // If we already scheduled a throttled repaint request but are also
        // in danger of checkerboarding soon, trigger the repaint request to
        // go out immediately. This should reduce the amount of time we spend
        // checkerboarding.
        //
        // Note that if we remain in this "about to
        // checkerboard" state over a period of time with multiple pinch input
        // events (which is quite likely), then we will flip-flop between taking
        // the above branch (!mPinchPaintTimerSet) and this branch (which will
        // flush the repaint request and reset mPinchPaintTimerSet to false).
        // This is sort of desirable because it halves the number of repaint
        // requests we send, and therefore reduces IPC traffic.
        // Keep in mind that many of these repaint requests will be ignored on
        // the main-thread anyway due to the resolution mismatch - the first
        // repaint request will be honored because APZ's notion of the painted
        // resolution matches the actual main thread resolution, but that first
        // repaint request will change the resolution on the main thread.
        // Subsequent repaint requests will be ignored in APZCCallbackHelper, at
        // https://searchfox.org/mozilla-central/rev/e0eb861a187f0bb6d994228f2e0e49b2c9ee455e/gfx/layers/apz/util/APZCCallbackHelper.cpp#331-338,
        // until we receive a NotifyLayersUpdated call that re-syncs APZ's
        // notion of the painted resolution to the main thread. These ignored
        // repaint requests are contributing to IPC traffic needlessly, and so
        // halving the number of repaint requests (as mentioned above) seems
        // desirable.
        DoDelayedRequestContentRepaint();
      }
    } else {
      // Trigger a repaint request after scrolling.
      RequestContentRepaint();
    }

    // We did a ScrollBy call above even if we didn't do a scale, so we
    // should composite for that.
    ScheduleComposite();
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnScaleEnd(
    const PinchGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a scale-end in state %s\n", this,
                  ToString(mState).c_str());

  mPinchPaintTimerSet = false;

  if (HasReadyTouchBlock() &&
      !GetCurrentTouchBlock()->TouchActionAllowsPinchZoom()) {
    return nsEventStatus_eIgnore;
  }

  // FIXME: bug 1525793 -- this may need to handle zooming or not on a
  // per-document basis.
  if (!StaticPrefs::apz_allow_zooming()) {
    if (RefPtr<GeckoContentController> controller =
            GetGeckoContentController()) {
      controller->NotifyPinchGesture(
          aEvent.mType, GetGuid(),
          ViewAs<LayoutDevicePixel>(
              aEvent.mFocusPoint,
              PixelCastJustification::
                  LayoutDeviceIsScreenForUntransformedEvent),
          0, aEvent.modifiers);
    }
  }

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    ScheduleComposite();
    RequestContentRepaint();
  }

  mPinchEventBuffer.clear();

  if (aEvent.mType == PinchGestureInput::PINCHGESTURE_FINGERLIFTED) {
    // One finger is still down, so transition to a TOUCHING state
    if (!mPinchLocked) {
      mPanDirRestricted = false;
      mLastTouch.mPosition = mStartTouch =
          ToExternalPoint(aEvent.mScreenOffset, aEvent.mFocusPoint);
      mLastTouch.mTimeStamp = mTouchStartTime = aEvent.mTimeStamp;
      StartTouch(aEvent.mLocalFocusPoint, aEvent.mTimeStamp);
      SetState(TOUCHING);
    } else {
      // If we are pinch locked, StartTouch() was already called
      // when we entered the pinch lock.
      StartPanning(ToExternalPoint(aEvent.mScreenOffset, aEvent.mFocusPoint),
                   aEvent.mTimeStamp);
    }
  } else {
    // Otherwise, handle the gesture being completely done.

    // Some of the code paths below, like ScrollSnap() or HandleEndOfPan(),
    // may start an animation, but otherwise we want to end up in the NOTHING
    // state. To avoid state change notification churn, we use a
    // notification blocker.
    bool stateWasPinching = (mState == PINCHING);
    StateChangeNotificationBlocker blocker(this);
    SetState(NOTHING);

    if (ZoomConstraintsAllowZoom()) {
      RecursiveMutexAutoLock lock(mRecursiveMutex);

      // We can get into a situation where we are overscrolled at the end of a
      // pinch if we go into overscroll with a two-finger pan, and then turn
      // that into a pinch by increasing the span sufficiently. In such a case,
      // there is no snap-back animation to get us out of overscroll, so we need
      // to get out of it somehow.
      // Moreover, in cases of scroll handoff, the overscroll can be on an APZC
      // further up in the handoff chain rather than on the current APZC, so
      // we need to clear overscroll along the entire handoff chain.
      if (HasReadyTouchBlock()) {
        GetCurrentTouchBlock()->GetOverscrollHandoffChain()->ClearOverscroll();
      } else {
        ClearOverscroll();
      }
      // Along with clearing the overscroll, we also want to snap to the nearest
      // snap point as appropriate.
      ScrollSnap(ScrollSnapFlags::IntendedEndPosition);
    } else {
      // when zoom is not allowed
      EndTouch(aEvent.mTimeStamp, Axis::ClearAxisLock::Yes);
      if (stateWasPinching) {
        // still pinching
        if (HasReadyTouchBlock()) {
          return HandleEndOfPan();
        }
      }
    }
  }
  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::HandleEndOfPan() {
  MOZ_ASSERT(!mAnimation);
  MOZ_ASSERT(GetCurrentTouchBlock() || GetCurrentPanGestureBlock());
  GetCurrentInputBlock()->GetOverscrollHandoffChain()->FlushRepaints();
  ParentLayerPoint flingVelocity = GetVelocityVector();

  // Clear our velocities; if DispatchFling() gives the fling to us,
  // the fling velocity gets *added* to our existing velocity in
  // AcceptFling().
  SetVelocityVector(ParentLayerPoint(0, 0));
  // Clear our state so that we don't stay in the PANNING state
  // if DispatchFling() gives the fling to somone else. However,
  // don't send the state change notification until we've determined
  // what our final state is to avoid notification churn.
  StateChangeNotificationBlocker blocker(this);
  SetState(NOTHING);

  APZC_LOG("%p starting a fling animation if %f > %f\n", this,
           flingVelocity.Length().value,
           StaticPrefs::apz_fling_min_velocity_threshold());

  if (flingVelocity.Length() <=
      StaticPrefs::apz_fling_min_velocity_threshold()) {
    // Relieve overscroll now if needed, since we will not transition to a fling
    // animation and then an overscroll animation, and relieve it then.
    GetCurrentInputBlock()
        ->GetOverscrollHandoffChain()
        ->SnapBackOverscrolledApzc(this);
    mFlingAccelerator.Reset();
    return nsEventStatus_eConsumeNoDefault;
  }

  // Make a local copy of the tree manager pointer and check that it's not
  // null before calling DispatchFling(). This is necessary because Destroy(),
  // which nulls out mTreeManager, could be called concurrently.
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    const FlingHandoffState handoffState{
        flingVelocity,
        GetCurrentInputBlock()->GetOverscrollHandoffChain(),
        Some(mTouchStartRestingTimeBeforePan),
        mMinimumVelocityDuringPan.valueOr(0),
        false /* not handoff */,
        GetCurrentInputBlock()->GetScrolledApzc()};
    treeManagerLocal->DispatchFling(this, handoffState);
  }
  return nsEventStatus_eConsumeNoDefault;
}

Maybe<LayoutDevicePoint> AsyncPanZoomController::ConvertToGecko(
    const ScreenIntPoint& aPoint) {
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    if (Maybe<ScreenIntPoint> layoutPoint =
            treeManagerLocal->ConvertToGecko(aPoint, this)) {
      return Some(LayoutDevicePoint(ViewAs<LayoutDevicePixel>(
          *layoutPoint,
          PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent)));
    }
  }
  return Nothing();
}

OuterCSSCoord AsyncPanZoomController::ConvertScrollbarPoint(
    const ParentLayerPoint& aScrollbarPoint,
    const ScrollbarData& aThumbData) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  CSSPoint scrollbarPoint;
  if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
    // First, get it into the right coordinate space.
    scrollbarPoint = aScrollbarPoint / Metrics().GetZoom();
  }

  // The scrollbar can be transformed with the frame but the pres shell
  // resolution is only applied to the scroll frame.
  OuterCSSPoint outerScrollbarPoint =
      scrollbarPoint * Metrics().GetCSSToOuterCSSScale();

  // Now, get it to be relative to the beginning of the scroll track.
  OuterCSSRect cssCompositionBound =
      Metrics().CalculateCompositionBoundsInOuterCssPixels();
  return GetAxisStart(*aThumbData.mDirection, outerScrollbarPoint) -
         GetAxisStart(*aThumbData.mDirection, cssCompositionBound) -
         aThumbData.mScrollTrackStart;
}

static bool AllowsScrollingMoreThanOnePage(double aMultiplier) {
  return Abs(aMultiplier) >=
         EventStateManager::MIN_MULTIPLIER_VALUE_ALLOWING_OVER_ONE_PAGE_SCROLL;
}

ParentLayerPoint AsyncPanZoomController::GetScrollWheelDelta(
    const ScrollWheelInput& aEvent) const {
  return GetScrollWheelDelta(aEvent, aEvent.mDeltaX, aEvent.mDeltaY,
                             aEvent.mUserDeltaMultiplierX,
                             aEvent.mUserDeltaMultiplierY);
}

ParentLayerPoint AsyncPanZoomController::GetScrollWheelDelta(
    const ScrollWheelInput& aEvent, double aDeltaX, double aDeltaY,
    double aMultiplierX, double aMultiplierY) const {
  ParentLayerSize scrollAmount;
  ParentLayerSize pageScrollSize;

  {
    // Grab the lock to access the frame metrics.
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    LayoutDeviceIntSize scrollAmountLD = mScrollMetadata.GetLineScrollAmount();
    LayoutDeviceIntSize pageScrollSizeLD =
        mScrollMetadata.GetPageScrollAmount();
    scrollAmount = scrollAmountLD / Metrics().GetDevPixelsPerCSSPixel() *
                   Metrics().GetZoom();
    pageScrollSize = pageScrollSizeLD / Metrics().GetDevPixelsPerCSSPixel() *
                     Metrics().GetZoom();
  }

  ParentLayerPoint delta;
  switch (aEvent.mDeltaType) {
    case ScrollWheelInput::SCROLLDELTA_LINE: {
      delta.x = aDeltaX * scrollAmount.width;
      delta.y = aDeltaY * scrollAmount.height;
      break;
    }
    case ScrollWheelInput::SCROLLDELTA_PAGE: {
      delta.x = aDeltaX * pageScrollSize.width;
      delta.y = aDeltaY * pageScrollSize.height;
      break;
    }
    case ScrollWheelInput::SCROLLDELTA_PIXEL: {
      delta = ToParentLayerCoordinates(ScreenPoint(aDeltaX, aDeltaY),
                                       aEvent.mOrigin);
      break;
    }
  }

  // Apply user-set multipliers.
  delta.x *= aMultiplierX;
  delta.y *= aMultiplierY;
  APZC_LOGV(
      "user-multiplied delta is %s (deltaType %d, line size %s, page size %s)",
      ToString(delta).c_str(), (int)aEvent.mDeltaType,
      ToString(scrollAmount).c_str(), ToString(pageScrollSize).c_str());

  // For the conditions under which we allow system scroll overrides, see
  // WidgetWheelEvent::OverriddenDelta{X,Y}.
  // Note that we do *not* restrict this to the root content, see bug 1217715
  // for discussion on this.
  if (StaticPrefs::mousewheel_system_scroll_override_enabled() &&
      !aEvent.IsCustomizedByUserPrefs() &&
      aEvent.mDeltaType == ScrollWheelInput::SCROLLDELTA_LINE &&
      aEvent.mAllowToOverrideSystemScrollSpeed) {
    delta.x = WidgetWheelEvent::ComputeOverriddenDelta(delta.x, false);
    delta.y = WidgetWheelEvent::ComputeOverriddenDelta(delta.y, true);
    APZC_LOGV("overridden delta is %s", ToString(delta).c_str());
  }

  // If this is a line scroll, and this event was part of a scroll series, then
  // it might need extra acceleration. See WheelHandlingHelper.cpp.
  if (aEvent.mDeltaType == ScrollWheelInput::SCROLLDELTA_LINE &&
      aEvent.mScrollSeriesNumber > 0) {
    int32_t start = StaticPrefs::mousewheel_acceleration_start();
    if (start >= 0 && aEvent.mScrollSeriesNumber >= uint32_t(start)) {
      int32_t factor = StaticPrefs::mousewheel_acceleration_factor();
      if (factor > 0) {
        delta.x = ComputeAcceleratedWheelDelta(
            delta.x, aEvent.mScrollSeriesNumber, factor);
        delta.y = ComputeAcceleratedWheelDelta(
            delta.y, aEvent.mScrollSeriesNumber, factor);
      }
    }
  }

  // We shouldn't scroll more than one page at once except when the
  // user preference is large.
  if (!AllowsScrollingMoreThanOnePage(aMultiplierX) &&
      Abs(delta.x) > pageScrollSize.width) {
    delta.x = (delta.x >= 0) ? pageScrollSize.width : -pageScrollSize.width;
  }
  if (!AllowsScrollingMoreThanOnePage(aMultiplierY) &&
      Abs(delta.y) > pageScrollSize.height) {
    delta.y = (delta.y >= 0) ? pageScrollSize.height : -pageScrollSize.height;
  }

  return delta;
}

nsEventStatus AsyncPanZoomController::OnKeyboard(const KeyboardInput& aEvent) {
  // Mark that this APZC has async key scrolled
  mTestHasAsyncKeyScrolled = true;

  // Calculate the destination for this keyboard scroll action
  CSSPoint destination = GetKeyboardDestination(aEvent.mAction);
  ScrollOrigin scrollOrigin =
      SmoothScrollAnimation::GetScrollOriginForAction(aEvent.mAction.mType);
  Maybe<CSSSnapDestination> snapDestination =
      MaybeAdjustDestinationForScrollSnapping(
          aEvent, destination,
          GetScrollSnapFlagsForKeyboardAction(aEvent.mAction));
  ScrollMode scrollMode = apz::GetScrollModeForOrigin(scrollOrigin);

  RecordScrollPayload(aEvent.mTimeStamp);
  // If the scrolling is instant, then scroll immediately to the destination
  if (scrollMode == ScrollMode::Instant) {
    CancelAnimation();

    ParentLayerPoint startPoint, endPoint;

    {
      RecursiveMutexAutoLock lock(mRecursiveMutex);

      // CallDispatchScroll interprets the start and end points as the start and
      // end of a touch scroll so they need to be reversed.
      startPoint = destination * Metrics().GetZoom();
      endPoint = Metrics().GetVisualScrollOffset() * Metrics().GetZoom();
    }

    ParentLayerPoint delta = endPoint - startPoint;

    ScreenPoint distance = ToScreenCoordinates(
        ParentLayerPoint(fabs(delta.x), fabs(delta.y)), startPoint);

    OverscrollHandoffState handoffState(
        *mInputQueue->GetCurrentKeyboardBlock()->GetOverscrollHandoffChain(),
        distance, ScrollSource::Keyboard);

    CallDispatchScroll(startPoint, endPoint, handoffState);
    ParentLayerPoint remainingDelta = endPoint - startPoint;
    if (remainingDelta != delta) {
      // If any scrolling happened, set KEYBOARD_SCROLL explicitly so that it
      // will trigger a TransformEnd notification.
      SetState(KEYBOARD_SCROLL);
    }

    if (snapDestination) {
      {
        RecursiveMutexAutoLock lock(mRecursiveMutex);
        mLastSnapTargetIds = std::move(snapDestination->mTargetIds);
      }
    }
    SetState(NOTHING);

    return nsEventStatus_eConsumeDoDefault;
  }

  // The lock must be held across the entire update operation, so the
  // compositor doesn't end the animation before we get a chance to
  // update it.
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  if (snapDestination) {
    // If we're scroll snapping, use a smooth scroll animation to get
    // the desired physics. Note that SmoothMsdScrollTo() will re-use an
    // existing smooth scroll animation if there is one.
    APZC_LOG("%p keyboard scrolling to snap point %s\n", this,
             ToString(destination).c_str());
    SmoothMsdScrollTo(std::move(*snapDestination), ScrollTriggeredByScript::No);
    return nsEventStatus_eConsumeDoDefault;
  }

  // Use a keyboard scroll animation to scroll, reusing an existing one if it
  // exists
  if (mState != KEYBOARD_SCROLL) {
    CancelAnimation();

    // Keyboard input that does not change the scroll position should not
    // cause a TransformBegin state change, in order to avoid firing a
    // scrollend event when no scrolling occurred.
    if (!CanScroll(ConvertDestinationToDelta(destination))) {
      return nsEventStatus_eConsumeDoDefault;
    }
    SetState(KEYBOARD_SCROLL);

    nsPoint initialPosition =
        CSSPoint::ToAppUnits(Metrics().GetVisualScrollOffset());
    StartAnimation(do_AddRef(
        new SmoothScrollAnimation(*this, initialPosition, scrollOrigin)));
  }

  // Convert velocity from ParentLayerPoints/ms to ParentLayerPoints/s and then
  // to appunits/second.
  nsPoint velocity;
  if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
    velocity =
        CSSPoint::ToAppUnits(ParentLayerPoint(mX.GetVelocity() * 1000.0f,
                                              mY.GetVelocity() * 1000.0f) /
                             Metrics().GetZoom());
  }

  SmoothScrollAnimation* animation = mAnimation->AsSmoothScrollAnimation();
  MOZ_ASSERT(animation);

  animation->UpdateDestination(aEvent.mTimeStamp,
                               CSSPixel::ToAppUnits(destination),
                               nsSize(velocity.x, velocity.y));

  return nsEventStatus_eConsumeDoDefault;
}

CSSPoint AsyncPanZoomController::GetKeyboardDestination(
    const KeyboardScrollAction& aAction) const {
  CSSSize lineScrollSize;
  CSSSize pageScrollSize;
  CSSPoint scrollOffset;
  CSSRect scrollRect;
  ParentLayerRect compositionBounds;

  {
    // Grab the lock to access the frame metrics.
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    lineScrollSize = mScrollMetadata.GetLineScrollAmount() /
                     Metrics().GetDevPixelsPerCSSPixel();
    pageScrollSize = mScrollMetadata.GetPageScrollAmount() /
                     Metrics().GetDevPixelsPerCSSPixel();

    scrollOffset = GetCurrentAnimationDestination(lock).valueOr(
        Metrics().GetVisualScrollOffset());

    scrollRect = Metrics().GetScrollableRect();
    compositionBounds = Metrics().GetCompositionBounds();
  }

  // Calculate the scroll destination based off of the scroll type and direction
  CSSPoint scrollDestination = scrollOffset;

  switch (aAction.mType) {
    case KeyboardScrollAction::eScrollCharacter: {
      int32_t scrollDistance =
          StaticPrefs::toolkit_scrollbox_horizontalScrollDistance();

      if (aAction.mForward) {
        scrollDestination.x += scrollDistance * lineScrollSize.width;
      } else {
        scrollDestination.x -= scrollDistance * lineScrollSize.width;
      }
      break;
    }
    case KeyboardScrollAction::eScrollLine: {
      int32_t scrollDistance =
          StaticPrefs::toolkit_scrollbox_verticalScrollDistance();
      if (scrollDistance * lineScrollSize.height <=
          compositionBounds.Height()) {
        if (aAction.mForward) {
          scrollDestination.y += scrollDistance * lineScrollSize.height;
        } else {
          scrollDestination.y -= scrollDistance * lineScrollSize.height;
        }
        break;
      }
      [[fallthrough]];
    }
    case KeyboardScrollAction::eScrollPage: {
      if (aAction.mForward) {
        scrollDestination.y += pageScrollSize.height;
      } else {
        scrollDestination.y -= pageScrollSize.height;
      }
      break;
    }
    case KeyboardScrollAction::eScrollComplete: {
      if (aAction.mForward) {
        scrollDestination.y = scrollRect.YMost();
      } else {
        scrollDestination.y = scrollRect.Y();
      }
      break;
    }
  }

  return scrollDestination;
}

ScrollSnapFlags AsyncPanZoomController::GetScrollSnapFlagsForKeyboardAction(
    const KeyboardScrollAction& aAction) const {
  switch (aAction.mType) {
    case KeyboardScrollAction::eScrollCharacter:
    case KeyboardScrollAction::eScrollLine:
      return ScrollSnapFlags::IntendedDirection;
    case KeyboardScrollAction::eScrollPage:
      return ScrollSnapFlags::IntendedDirection |
             ScrollSnapFlags::IntendedEndPosition;
    case KeyboardScrollAction::eScrollComplete:
      return ScrollSnapFlags::IntendedEndPosition;
  }
  return ScrollSnapFlags::Disabled;
}

ParentLayerPoint AsyncPanZoomController::GetDeltaForEvent(
    const InputData& aEvent) const {
  ParentLayerPoint delta;
  if (aEvent.mInputType == SCROLLWHEEL_INPUT) {
    delta = GetScrollWheelDelta(aEvent.AsScrollWheelInput());
  } else if (aEvent.mInputType == PANGESTURE_INPUT) {
    const PanGestureInput& panInput = aEvent.AsPanGestureInput();
    delta = ToParentLayerCoordinates(panInput.UserMultipliedPanDisplacement(),
                                     panInput.mPanStartPoint);
  }
  return delta;
}

CSSRect AsyncPanZoomController::GetCurrentScrollRangeInCssPixels() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return Metrics().CalculateScrollRange();
}

bool AsyncPanZoomController::AllowOneTouchPinch() const {
  return StaticPrefs::apz_one_touch_pinch_enabled() &&
         ZoomConstraintsAllowZoom();
}

// Return whether or not the underlying layer can be scrolled on either axis.
bool AsyncPanZoomController::CanScroll(const InputData& aEvent) const {
  ParentLayerPoint delta = GetDeltaForEvent(aEvent);
  APZC_LOGV_DETAIL("CanScroll: event delta is %s", this,
                   ToString(delta).c_str());
  if (!delta.x && !delta.y) {
    return false;
  }

  if (SCROLLWHEEL_INPUT == aEvent.mInputType) {
    const ScrollWheelInput& scrollWheelInput = aEvent.AsScrollWheelInput();
    // If it's a wheel scroll, we first check if it is an auto-dir scroll.
    // 1. For an auto-dir scroll, check if it's delta should be adjusted, if it
    //    is, then we can conclude it must be scrollable; otherwise, fall back
    //    to checking if it is scrollable without adjusting its delta.
    // 2. For a non-auto-dir scroll, simply check if it is scrollable without
    //    adjusting its delta.
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    if (scrollWheelInput.IsAutoDir(mScrollMetadata.ForceMousewheelAutodir())) {
      auto deltaX = scrollWheelInput.mDeltaX;
      auto deltaY = scrollWheelInput.mDeltaY;
      bool isRTL =
          IsContentOfHonouredTargetRightToLeft(scrollWheelInput.HonoursRoot(
              mScrollMetadata.ForceMousewheelAutodirHonourRoot()));
      APZAutoDirWheelDeltaAdjuster adjuster(deltaX, deltaY, mX, mY, isRTL);
      if (adjuster.ShouldBeAdjusted()) {
        // If we detect that the delta values should be adjusted for an auto-dir
        // wheel scroll, then it is impossible to be an unscrollable scroll.
        return true;
      }
    }
    return CanScrollWithWheel(delta);
  }
  return CanScroll(delta);
}

ScrollDirections AsyncPanZoomController::GetAllowedHandoffDirections(
    HandoffConsumer aConsumer) const {
  ScrollDirections result;
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  // In Fission there can be non-scrollable APZCs. It's unclear whether
  // overscroll-behavior should be respected for these
  // (see https://github.com/w3c/csswg-drafts/issues/6523) but
  // we currently don't, to match existing practice.
  const bool isScrollable = mX.CanScroll() || mY.CanScroll();
  const bool isRoot = IsRootContent();
  if ((!isScrollable && !isRoot) || mX.OverscrollBehaviorAllowsHandoff()) {
    result += ScrollDirection::eHorizontal;
  }
  if ((!isScrollable && !isRoot) || mY.OverscrollBehaviorAllowsHandoff()) {
    // Bug 1902313: Block pull-to-refresh on pages with overflow-y:hidden
    // to match Chrome behaviour.
    bool blockPullToRefreshForOverflowHidden =
        isRoot && aConsumer == HandoffConsumer::PullToRefresh &&
        GetScrollMetadata().GetOverflow().mOverflowY == StyleOverflow::Hidden;
    if (!blockPullToRefreshForOverflowHidden) {
      result += ScrollDirection::eVertical;
    }
  }
  return result;
}

bool AsyncPanZoomController::CanScroll(const ParentLayerPoint& aDelta) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mX.CanScroll(ParentLayerCoord(aDelta.x)) ||
         mY.CanScroll(ParentLayerCoord(aDelta.y));
}

bool AsyncPanZoomController::CanScrollWithWheel(
    const ParentLayerPoint& aDelta) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  // For more details about the concept of a disregarded direction, refer to the
  // code in struct ScrollMetadata which defines mDisregardedDirection.
  Maybe<ScrollDirection> disregardedDirection =
      mScrollMetadata.GetDisregardedDirection();
  if (mX.CanScroll(ParentLayerCoord(aDelta.x)) &&
      disregardedDirection != Some(ScrollDirection::eHorizontal)) {
    return true;
  }
  if (mY.CanScroll(ParentLayerCoord(aDelta.y)) &&
      disregardedDirection != Some(ScrollDirection::eVertical)) {
    return true;
  }
  APZC_LOGV_FM(Metrics(),
               "cannot scroll with wheel (disregarded direction is %s)",
               ToString(disregardedDirection).c_str());
  return false;
}

bool AsyncPanZoomController::CanScroll(ScrollDirection aDirection) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  switch (aDirection) {
    case ScrollDirection::eHorizontal:
      return mX.CanScroll();
    case ScrollDirection::eVertical:
      return mY.CanScroll();
  }
  MOZ_ASSERT_UNREACHABLE("Invalid value");
  return false;
}

bool AsyncPanZoomController::CanVerticalScrollWithDynamicToolbar() const {
  MOZ_ASSERT(IsRootContent());

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mY.CanVerticalScrollWithDynamicToolbar();
}

bool AsyncPanZoomController::CanOverscrollUpwards() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return !mY.CanScrollTo(eSideTop) && mY.OverscrollBehaviorAllowsHandoff();
}

bool AsyncPanZoomController::CanScrollDownwards() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mY.CanScrollTo(eSideBottom);
}

SideBits AsyncPanZoomController::ScrollableDirections() const {
  SideBits result;
  {  // scope lock to respect lock ordering with APZCTreeManager::mTreeLock
    // which will be acquired in the `GetCompositorFixedLayerMargins` below.
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    result = mX.ScrollableDirections() | mY.ScrollableDirections();
  }

  if (IsRootContent()) {
    if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
      ScreenMargin fixedLayerMargins =
          treeManagerLocal->GetCompositorFixedLayerMargins();
      {
        RecursiveMutexAutoLock lock(mRecursiveMutex);
        result |= mY.ScrollableDirectionsWithDynamicToolbar(fixedLayerMargins);
      }
    }
  }

  return result;
}

bool AsyncPanZoomController::IsContentOfHonouredTargetRightToLeft(
    bool aHonoursRoot) const {
  if (aHonoursRoot) {
    return mScrollMetadata.IsAutoDirRootContentRTL();
  }
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return Metrics().IsHorizontalContentRightToLeft();
}

bool AsyncPanZoomController::AllowScrollHandoffInCurrentBlock() const {
  bool result = mInputQueue->AllowScrollHandoff();
  if (!StaticPrefs::apz_allow_immediate_handoff()) {
    if (InputBlockState* currentBlock = GetCurrentInputBlock()) {
      // Do not allow handoff beyond the first APZC to scroll.
      if (currentBlock->GetScrolledApzc() == this) {
        result = false;
        APZC_LOG("%p dropping handoff; AllowImmediateHandoff=false\n", this);
      }
    }
  }
  return result;
}

void AsyncPanZoomController::DoDelayedRequestContentRepaint() {
  if (!IsDestroyed() && mPinchPaintTimerSet) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    RequestContentRepaint();
  }
  mPinchPaintTimerSet = false;
}

void AsyncPanZoomController::DoDelayedTransformEndNotification(
    PanZoomState aOldState) {
  if (!IsDestroyed() && IsDelayedTransformEndSet()) {
    DispatchStateChangeNotification(aOldState, NOTHING);
  }
  SetDelayedTransformEnd(false);
}

static void AdjustDeltaForAllowedScrollDirections(
    ParentLayerPoint& aDelta,
    const ScrollDirections& aAllowedScrollDirections) {
  if (!aAllowedScrollDirections.contains(ScrollDirection::eHorizontal)) {
    aDelta.x = 0;
  }
  if (!aAllowedScrollDirections.contains(ScrollDirection::eVertical)) {
    aDelta.y = 0;
  }
}

nsEventStatus AsyncPanZoomController::OnScrollWheel(
    const ScrollWheelInput& aEvent) {
  // Get the scroll wheel's delta values in parent-layer pixels. But before
  // getting the values, we need to check if it is an auto-dir scroll and if it
  // should be adjusted, if both answers are yes, let's adjust X and Y values
  // first, and then get the delta values in parent-layer pixels based on the
  // adjusted values.
  bool adjustedByAutoDir = false;
  auto deltaX = aEvent.mDeltaX;
  auto deltaY = aEvent.mDeltaY;
  ParentLayerPoint delta;
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    if (aEvent.IsAutoDir(mScrollMetadata.ForceMousewheelAutodir())) {
      // It's an auto-dir scroll, so check if its delta should be adjusted, if
      // so, adjust it.
      bool isRTL = IsContentOfHonouredTargetRightToLeft(aEvent.HonoursRoot(
          mScrollMetadata.ForceMousewheelAutodirHonourRoot()));
      APZAutoDirWheelDeltaAdjuster adjuster(deltaX, deltaY, mX, mY, isRTL);
      if (adjuster.ShouldBeAdjusted()) {
        adjuster.Adjust();
        adjustedByAutoDir = true;
      }
    }
  }
  // Ensure the calls to GetScrollWheelDelta are outside the mRecursiveMutex
  // lock since these calls may acquire the APZ tree lock. Holding
  // mRecursiveMutex while acquiring the APZ tree lock is lock ordering
  // violation.
  if (adjustedByAutoDir) {
    // If the original delta values have been adjusted, we pass them to
    // replace the original delta values in |aEvent| so that the delta values
    // in parent-layer pixels are caculated based on the adjusted values, not
    // the original ones.
    // Pay special attention to the last two parameters. They are in a swaped
    // order so that they still correspond to their delta after adjustment.
    delta = GetScrollWheelDelta(aEvent, deltaX, deltaY,
                                aEvent.mUserDeltaMultiplierY,
                                aEvent.mUserDeltaMultiplierX);
  } else {
    // If the original delta values haven't been adjusted by auto-dir, just pass
    // the |aEvent| and caculate the delta values in parent-layer pixels based
    // on the original delta values from |aEvent|.
    delta = GetScrollWheelDelta(aEvent);
  }

  APZC_LOG("%p got a scroll-wheel with delta in parent-layer pixels: %s\n",
           this, ToString(delta).c_str());

  if (adjustedByAutoDir) {
    MOZ_ASSERT(delta.x || delta.y,
               "Adjusted auto-dir delta values can never be all-zero.");
    APZC_LOG("%p got a scroll-wheel with adjusted auto-dir delta values\n",
             this);
  } else if ((delta.x || delta.y) && !CanScrollWithWheel(delta)) {
    // We can't scroll this apz anymore, so we simply drop the event.
    if (mInputQueue->GetActiveWheelTransaction() &&
        StaticPrefs::test_mousescroll()) {
      if (RefPtr<GeckoContentController> controller =
              GetGeckoContentController()) {
        controller->NotifyMozMouseScrollEvent(GetScrollId(),
                                              u"MozMouseScrollFailed"_ns);
      }
    }
    return nsEventStatus_eConsumeNoDefault;
  }

  MOZ_ASSERT(mInputQueue->GetCurrentWheelBlock());
  AdjustDeltaForAllowedScrollDirections(
      delta, mInputQueue->GetCurrentWheelBlock()->GetAllowedScrollDirections());

  if (delta.x == 0 && delta.y == 0) {
    // Avoid spurious state changes and unnecessary work
    return nsEventStatus_eIgnore;
  }

  switch (aEvent.mScrollMode) {
    case ScrollWheelInput::SCROLLMODE_INSTANT: {
      // Wheel events from "clicky" mouse wheels trigger scroll snapping to the
      // next snap point. Check for this, and adjust the delta to take into
      // account the snap point.
      CSSPoint startPosition;
      {
        RecursiveMutexAutoLock lock(mRecursiveMutex);
        startPosition = Metrics().GetVisualScrollOffset();
      }
      Maybe<CSSSnapDestination> snapDestination =
          MaybeAdjustDeltaForScrollSnappingOnWheelInput(aEvent, delta,
                                                        startPosition);

      ScreenPoint distance = ToScreenCoordinates(
          ParentLayerPoint(fabs(delta.x), fabs(delta.y)), aEvent.mLocalOrigin);

      CancelAnimation();

      OverscrollHandoffState handoffState(
          *mInputQueue->GetCurrentWheelBlock()->GetOverscrollHandoffChain(),
          distance, ScrollSource::Wheel);
      ParentLayerPoint startPoint = aEvent.mLocalOrigin;
      ParentLayerPoint endPoint = aEvent.mLocalOrigin - delta;
      RecordScrollPayload(aEvent.mTimeStamp);

      CallDispatchScroll(startPoint, endPoint, handoffState);
      ParentLayerPoint remainingDelta = endPoint - startPoint;
      if (remainingDelta != delta) {
        // If any scrolling happened, set WHEEL_SCROLL explicitly so that it
        // will trigger a TransformEnd notification.
        SetState(WHEEL_SCROLL);
      }

      if (snapDestination) {
        {
          RecursiveMutexAutoLock lock(mRecursiveMutex);
          mLastSnapTargetIds = std::move(snapDestination->mTargetIds);
        }
      }
      SetState(NOTHING);

      // The calls above handle their own locking; moreover,
      // ToScreenCoordinates() and CallDispatchScroll() can grab the tree lock.
      RecursiveMutexAutoLock lock(mRecursiveMutex);
      RequestContentRepaint();

      break;
    }

    case ScrollWheelInput::SCROLLMODE_SMOOTH: {
      // The lock must be held across the entire update operation, so the
      // compositor doesn't end the animation before we get a chance to
      // update it.
      RecursiveMutexAutoLock lock(mRecursiveMutex);

      RecordScrollPayload(aEvent.mTimeStamp);
      // Perform scroll snapping if appropriate.
      // If we're already in a wheel scroll or smooth scroll animation,
      // the delta is applied to its destination, not to the current
      // scroll position. Take this into account when finding a snap point.
      CSSPoint startPosition = GetCurrentAnimationDestination(lock).valueOr(
          Metrics().GetVisualScrollOffset());

      if (Maybe<CSSSnapDestination> snapDestination =
              MaybeAdjustDeltaForScrollSnappingOnWheelInput(aEvent, delta,
                                                            startPosition)) {
        // If we're scroll snapping, use a smooth scroll animation to get
        // the desired physics. Note that SmoothMsdScrollTo() will re-use an
        // existing smooth scroll animation if there is one.
        APZC_LOG("%p wheel scrolling to snap point %s\n", this,
                 ToString(startPosition).c_str());
        SmoothMsdScrollTo(std::move(*snapDestination),
                          ScrollTriggeredByScript::No);
        break;
      }

      // Otherwise, use a wheel scroll animation, also reusing one if possible.
      if (mState != WHEEL_SCROLL) {
        CancelAnimation();
        SetState(WHEEL_SCROLL);

        nsPoint initialPosition =
            CSSPoint::ToAppUnits(Metrics().GetVisualScrollOffset());
        StartAnimation(do_AddRef(new WheelScrollAnimation(
            *this, initialPosition, aEvent.mDeltaType)));
      }
      // Convert velocity from ParentLayerPoints/ms to ParentLayerPoints/s and
      // then to appunits/second.

      nsPoint deltaInAppUnits;
      nsPoint velocity;
      if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
        deltaInAppUnits = CSSPoint::ToAppUnits(delta / Metrics().GetZoom());
        velocity =
            CSSPoint::ToAppUnits(ParentLayerPoint(mX.GetVelocity() * 1000.0f,
                                                  mY.GetVelocity() * 1000.0f) /
                                 Metrics().GetZoom());
      }

      WheelScrollAnimation* animation = mAnimation->AsWheelScrollAnimation();
      animation->UpdateDelta(GetFrameTime().Time(), deltaInAppUnits,
                             nsSize(velocity.x, velocity.y));
      break;
    }
  }

  return nsEventStatus_eConsumeNoDefault;
}

void AsyncPanZoomController::NotifyMozMouseScrollEvent(
    const nsString& aString) const {
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (!controller) {
    return;
  }
  controller->NotifyMozMouseScrollEvent(GetScrollId(), aString);
}

nsEventStatus AsyncPanZoomController::OnPanMayBegin(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-maybegin in state %s\n", this,
                  ToString(mState).c_str());

  StartTouch(aEvent.mLocalPanStartPoint, aEvent.mTimeStamp);
  MOZ_ASSERT(GetCurrentPanGestureBlock());
  GetCurrentPanGestureBlock()->GetOverscrollHandoffChain()->CancelAnimations(
      ExcludeOverscroll);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanCancelled(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-cancelled in state %s\n", this,
                  ToString(mState).c_str());

  mX.CancelGesture();
  mY.CancelGesture();

  MOZ_ASSERT(GetCurrentPanGestureBlock());
  GetCurrentPanGestureBlock()
      ->GetOverscrollHandoffChain()
      ->SnapBackOverscrolledApzc(this);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanBegin(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-begin in state %s\n", this,
                  ToString(mState).c_str());

  MOZ_ASSERT(GetCurrentPanGestureBlock());
  GetCurrentPanGestureBlock()->GetOverscrollHandoffChain()->CancelAnimations(
      ExcludeOverscroll);

  StartTouch(aEvent.mLocalPanStartPoint, aEvent.mTimeStamp);

  if (!UsingStatefulAxisLock()) {
    SetState(PANNING);
  } else {
    float dx = aEvent.mPanDisplacement.x, dy = aEvent.mPanDisplacement.y;

    if (dx != 0.0f || dy != 0.0f) {
      double angle = atan2(dy, dx);  // range [-pi, pi]
      angle = fabs(angle);           // range [0, pi]
      HandlePanning(angle);
    } else {
      SetState(PANNING);
    }
  }

  // Call into OnPan in order to process any delta included in this event.
  OnPan(aEvent, FingersOnTouchpad::Yes);

  return nsEventStatus_eConsumeNoDefault;
}

std::tuple<ParentLayerPoint, ScreenPoint>
AsyncPanZoomController::GetDisplacementsForPanGesture(
    const PanGestureInput& aEvent) {
  // Note that there is a multiplier that applies onto the "physical" pan
  // displacement (how much the user's fingers moved) that produces the
  // "logical" pan displacement (how much the page should move). For some of the
  // code below it makes more sense to use the physical displacement rather than
  // the logical displacement, and vice-versa.
  ScreenPoint physicalPanDisplacement = aEvent.mPanDisplacement;
  ParentLayerPoint logicalPanDisplacement =
      aEvent.UserMultipliedLocalPanDisplacement();
  if (aEvent.mDeltaType == PanGestureInput::PANDELTA_PAGE) {
    // Pan events with page units are used by Gtk, so this replicates Gtk:
    // https://gitlab.gnome.org/GNOME/gtk/blob/c734c7e9188b56f56c3a504abee05fa40c5475ac/gtk/gtkrange.c#L3065-3073
    CSSSize pageScrollSize;
    CSSToParentLayerScale zoom;
    {
      // Grab the lock to access the frame metrics.
      RecursiveMutexAutoLock lock(mRecursiveMutex);
      pageScrollSize = mScrollMetadata.GetPageScrollAmount() /
                       Metrics().GetDevPixelsPerCSSPixel();
      zoom = Metrics().GetZoom();
    }
    // scrollUnit* is in units of "ParentLayer pixels per page proportion"...
    auto scrollUnitWidth = std::min(std::pow(pageScrollSize.width, 2.0 / 3.0),
                                    pageScrollSize.width / 2.0) *
                           zoom.scale;
    auto scrollUnitHeight = std::min(std::pow(pageScrollSize.height, 2.0 / 3.0),
                                     pageScrollSize.height / 2.0) *
                            zoom.scale;
    // ... and pan displacements are in units of "page proportion count"
    // here, so the products of them and scrollUnit* are in ParentLayer pixels
    ParentLayerPoint physicalPanDisplacementPL(
        physicalPanDisplacement.x * scrollUnitWidth,
        physicalPanDisplacement.y * scrollUnitHeight);
    physicalPanDisplacement = ToScreenCoordinates(physicalPanDisplacementPL,
                                                  aEvent.mLocalPanStartPoint);
    logicalPanDisplacement.x *= scrollUnitWidth;
    logicalPanDisplacement.y *= scrollUnitHeight;

    // Accelerate (decelerate) any pans by raising it to a user configurable
    // power (apz.touch_acceleration_factor_x, apz.touch_acceleration_factor_y)
    //
    // Confine input for pow() to greater than or equal to 0 to avoid domain
    // errors with non-integer exponents
    if (mX.GetVelocity() != 0) {
      float absVelocity = std::abs(mX.GetVelocity());
      logicalPanDisplacement.x *=
          std::pow(absVelocity,
                   StaticPrefs::apz_touch_acceleration_factor_x()) /
          absVelocity;
    }

    if (mY.GetVelocity() != 0) {
      float absVelocity = std::abs(mY.GetVelocity());
      logicalPanDisplacement.y *=
          std::pow(absVelocity,
                   StaticPrefs::apz_touch_acceleration_factor_y()) /
          absVelocity;
    }
  }

  MOZ_ASSERT(GetCurrentPanGestureBlock());
  AdjustDeltaForAllowedScrollDirections(
      logicalPanDisplacement,
      GetCurrentPanGestureBlock()->GetAllowedScrollDirections());

  if (GetAxisLockMode() == AxisLockMode::DOMINANT_AXIS) {
    // Given a pan gesture and both directions have a delta, implement
    // dominant axis scrolling and only use the delta for the larger
    // axis.
    if (logicalPanDisplacement.y != 0 && logicalPanDisplacement.x != 0) {
      if (fabs(logicalPanDisplacement.y) >= fabs(logicalPanDisplacement.x)) {
        logicalPanDisplacement.x = 0;
        physicalPanDisplacement.x = 0;
      } else {
        logicalPanDisplacement.y = 0;
        physicalPanDisplacement.y = 0;
      }
    }
  }

  return {logicalPanDisplacement, physicalPanDisplacement};
}

CSSPoint AsyncPanZoomController::ToCSSPixels(ParentLayerPoint value) const {
  if (this->Metrics().GetZoom() == CSSToParentLayerScale(0)) {
    return CSSPoint{0, 0};
  }
  return (value / this->Metrics().GetZoom());
}

CSSCoord AsyncPanZoomController::ToCSSPixels(ParentLayerCoord value) const {
  if (this->Metrics().GetZoom() == CSSToParentLayerScale(0)) {
    return CSSCoord{0};
  }
  return (value / this->Metrics().GetZoom());
}

nsEventStatus AsyncPanZoomController::OnPan(
    const PanGestureInput& aEvent, FingersOnTouchpad aFingersOnTouchpad) {
  APZC_LOG_DETAIL("got a pan-pan in state %s\n", this,
                  ToString(GetState()).c_str());

  if (GetState() == SMOOTHMSD_SCROLL) {
    if (aFingersOnTouchpad == FingersOnTouchpad::No) {
      // When a SMOOTHMSD_SCROLL scroll is being processed on a frame, mouse
      // wheel and trackpad momentum scroll position updates will not cancel the
      // SMOOTHMSD_SCROLL scroll animations, enabling scripts that depend on
      // them to be responsive without forcing the user to wait for the momentum
      // scrolling to completely stop.
      return nsEventStatus_eConsumeNoDefault;
    }

    // SMOOTHMSD_SCROLL scrolls are cancelled by pan gestures.
    CancelAnimation();
  }

  if (GetState() == NOTHING) {
    // This event block was interrupted by something else. If the user's fingers
    // are still on on the touchpad we want to resume scrolling, otherwise we
    // ignore the rest of the scroll gesture.
    if (aFingersOnTouchpad == FingersOnTouchpad::No) {
      return nsEventStatus_eConsumeNoDefault;
    }
    // Resume / restart the pan.
    // PanBegin will call back into this function with mState == PANNING.
    return OnPanBegin(aEvent);
  }

  auto [logicalPanDisplacement, physicalPanDisplacement] =
      GetDisplacementsForPanGesture(aEvent);

  {
    // Grab the lock to protect the animation from being canceled on the updater
    // thread.
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    MOZ_ASSERT_IF(GetState() == OVERSCROLL_ANIMATION, mAnimation);

    if (GetState() == OVERSCROLL_ANIMATION && mAnimation &&
        aFingersOnTouchpad == FingersOnTouchpad::No) {
      // If there is an on-going overscroll animation, we tell the animation
      // whether the displacements should be handled by the animation or not.
      MOZ_ASSERT(mAnimation->AsOverscrollAnimation());
      if (RefPtr<OverscrollAnimation> overscrollAnimation =
              mAnimation->AsOverscrollAnimation()) {
        overscrollAnimation->HandlePanMomentum(logicalPanDisplacement);
        // And then as a result of the above call, if the animation is currently
        // affecting on the axis, drop the displacement value on the axis so
        // that we stop further oversrolling on the axis.
        if (overscrollAnimation->IsManagingXAxis()) {
          logicalPanDisplacement.x = 0;
          physicalPanDisplacement.x = 0;
        }
        if (overscrollAnimation->IsManagingYAxis()) {
          logicalPanDisplacement.y = 0;
          physicalPanDisplacement.y = 0;
        }
      }
    }
  }

  HandlePanningUpdate(physicalPanDisplacement);

  MOZ_ASSERT(GetCurrentPanGestureBlock());
  ScreenPoint panDistance(fabs(physicalPanDisplacement.x),
                          fabs(physicalPanDisplacement.y));
  OverscrollHandoffState handoffState(
      *GetCurrentPanGestureBlock()->GetOverscrollHandoffChain(), panDistance,
      ScrollSource::Touchpad);

  // Create fake "touch" positions that will result in the desired scroll
  // motion. Note that the pan displacement describes the change in scroll
  // position: positive displacement values mean that the scroll position
  // increases. However, an increase in scroll position means that the scrolled
  // contents are moved to the left / upwards. Since our simulated "touches"
  // determine the motion of the scrolled contents, not of the scroll position,
  // they need to move in the opposite direction of the pan displacement.
  ParentLayerPoint startPoint = aEvent.mLocalPanStartPoint;
  ParentLayerPoint endPoint =
      aEvent.mLocalPanStartPoint - logicalPanDisplacement;
  if (logicalPanDisplacement != ParentLayerPoint()) {
    // Don't expect a composite to be triggered if the displacement is zero
    RecordScrollPayload(aEvent.mTimeStamp);
  }

  const ParentLayerPoint velocity = GetVelocityVector();
  bool consumed = CallDispatchScroll(startPoint, endPoint, handoffState);

  const ParentLayerPoint visualDisplacement = ToParentLayerCoordinates(
      handoffState.mTotalMovement, aEvent.mPanStartPoint);
  // We need to update the axis velocity in order to get a useful display port
  // size and position. We need to do so even if this is a momentum pan (i.e.
  // aFingersOnTouchpad == No); in that case the "with touch" part is not
  // really appropriate, so we may want to rethink this at some point.
  // Note that we have to make all simulated positions relative to
  // Axis::GetPos(), because the current position is an invented position, and
  // because resetting the position to the mouse position (e.g.
  // aEvent.mLocalStartPoint) would mess up velocity calculation. (This is
  // the only caller of UpdateWithTouchAtDevicePoint() for pan events, so
  // there is no risk of other calls resetting the position.)
  // Also note that if there is an on-going overscroll animation in the axis,
  // we shouldn't call UpdateWithTouchAtDevicePoint because the call changes
  // the velocity which should be managed by the overscroll animation.
  // Finally, note that we do this *after* CallDispatchScroll(), so that the
  // position we use reflects the actual amount of movement that occurred
  // (in particular, if we're in overscroll, if reflects the amount of movement
  // *after* applying resistance). This is important because we want the axis
  // velocity to track the visual movement speed of the page.
  if (visualDisplacement.x != 0) {
    mX.UpdateWithTouchAtDevicePoint(mX.GetPos() - visualDisplacement.x,
                                    aEvent.mTimeStamp);
  }
  if (visualDisplacement.y != 0) {
    mY.UpdateWithTouchAtDevicePoint(mY.GetPos() - visualDisplacement.y,
                                    aEvent.mTimeStamp);
  }

  if (aFingersOnTouchpad == FingersOnTouchpad::No) {
    if (IsOverscrolled() && GetState() != OVERSCROLL_ANIMATION) {
      StartOverscrollAnimation(velocity, GetOverscrollSideBits());
    } else if (!consumed) {
      // If there is unconsumed scroll and we're in the momentum part of the
      // pan gesture, terminate the momentum scroll. This prevents momentum
      // scroll events from unexpectedly causing scrolling later if somehow
      // the APZC becomes scrollable again in this direction (e.g. if the user
      // uses some other input method to scroll in the opposite direction).
      SetState(NOTHING);
    }
  }

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanEnd(const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-end in state %s\n", this,
                  ToString(mState).c_str());

  // This can happen if the OS sends a second pan-end event after the first one
  // has already started an overscroll animation or entered a fling state.
  // This has been observed on some Wayland versions.
  PanZoomState currentState = GetState();
  if (currentState == OVERSCROLL_ANIMATION || currentState == NOTHING ||
      currentState == FLING) {
    return nsEventStatus_eIgnore;
  }

  if (aEvent.mPanDisplacement != ScreenPoint{}) {
    // Call into OnPan in order to process the delta included in this event.
    OnPan(aEvent, FingersOnTouchpad::Yes);
  }

  // Do not unlock the axis lock at the end of a pan gesture. The axis lock
  // should extend into the momentum scroll.
  EndTouch(aEvent.mTimeStamp, Axis::ClearAxisLock::No);

  // Use HandleEndOfPan for fling on platforms that don't
  // emit momentum events (Gtk).
  if (aEvent.mSimulateMomentum) {
    return HandleEndOfPan();
  }

  MOZ_ASSERT(GetCurrentPanGestureBlock());
  RefPtr<const OverscrollHandoffChain> overscrollHandoffChain =
      GetCurrentPanGestureBlock()->GetOverscrollHandoffChain();

  // Call SnapBackOverscrolledApzcForMomentum regardless whether this APZC is
  // overscrolled or not since overscroll animations for ancestor APZCs in this
  // overscroll handoff chain might have been cancelled by the current pan
  // gesture block.
  overscrollHandoffChain->SnapBackOverscrolledApzcForMomentum(
      this, GetVelocityVector());
  // If this APZC is overscrolled, the above SnapBackOverscrolledApzcForMomentum
  // triggers an overscroll animation. When we're finished with the overscroll
  // animation, the state will be reset and a TransformEnd will be sent to the
  // main thread.
  currentState = GetState();
  if (currentState != OVERSCROLL_ANIMATION) {
    // Do not send a state change notification to the content controller here.
    // Instead queue a delayed task to dispatch the notification if no
    // momentum pan or scroll snap follows the pan-end.
    RefPtr<GeckoContentController> controller = GetGeckoContentController();
    if (controller) {
      SetDelayedTransformEnd(true);
      controller->PostDelayedTask(
          NewRunnableMethod<PanZoomState>(
              "layers::AsyncPanZoomController::"
              "DoDelayedTransformEndNotification",
              this, &AsyncPanZoomController::DoDelayedTransformEndNotification,
              currentState),
          StaticPrefs::apz_scrollend_event_content_delay_ms());
      SetStateNoContentControllerDispatch(NOTHING);
    } else {
      SetState(NOTHING);
    }
  }

  // Drop any velocity on axes where we don't have room to scroll anyways
  // (in this APZC, or an APZC further in the handoff chain).
  // This ensures that we don't enlarge the display port unnecessarily.
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    if (!overscrollHandoffChain->CanScrollInDirection(
            this, ScrollDirection::eHorizontal)) {
      mX.SetVelocity(0);
    }
    if (!overscrollHandoffChain->CanScrollInDirection(
            this, ScrollDirection::eVertical)) {
      mY.SetVelocity(0);
    }
  }

  RequestContentRepaint();
  ScrollSnapToDestination();

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanMomentumStart(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-momentumstart in state %s\n", this,
                  ToString(mState).c_str());

  if (mState == SMOOTHMSD_SCROLL || mState == OVERSCROLL_ANIMATION) {
    return nsEventStatus_eConsumeNoDefault;
  }

  if (IsDelayedTransformEndSet()) {
    // Do not send another TransformBegin notification if we have not
    // delivered a corresponding TransformEnd. Also ensure that any
    // queued transform-end due to a pan-end is not sent. Instead rely
    // on the transform-end sent due to the momentum pan.
    SetDelayedTransformEnd(false);
    SetStateNoContentControllerDispatch(PAN_MOMENTUM);
  } else {
    SetState(PAN_MOMENTUM);
  }

  // Call into OnPan in order to process any delta included in this event.
  OnPan(aEvent, FingersOnTouchpad::No);

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanMomentumEnd(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-momentumend in state %s\n", this,
                  ToString(mState).c_str());

  if (mState == OVERSCROLL_ANIMATION) {
    return nsEventStatus_eConsumeNoDefault;
  }

  // Call into OnPan in order to process any delta included in this event.
  OnPan(aEvent, FingersOnTouchpad::No);

  // We need to reset the velocity to zero. We don't really have a "touch"
  // here because the touch has already ended long before the momentum
  // animation started, but I guess it doesn't really matter for now.
  mX.CancelGesture();
  mY.CancelGesture();
  SetState(NOTHING);

  RequestContentRepaint();

  return nsEventStatus_eConsumeNoDefault;
}

nsEventStatus AsyncPanZoomController::OnPanInterrupted(
    const PanGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a pan-interrupted in state %s\n", this,
                  ToString(mState).c_str());

  CancelAnimation();

  return nsEventStatus_eIgnore;
}

nsEventStatus AsyncPanZoomController::OnLongPress(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a long-press in state %s\n", this,
                  ToString(mState).c_str());
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (controller) {
    if (Maybe<LayoutDevicePoint> geckoScreenPoint =
            ConvertToGecko(aEvent.mPoint)) {
      TouchBlockState* touch = GetCurrentTouchBlock();
      if (!touch) {
        APZC_LOG(
            "%p dropping long-press because some non-touch block interrupted "
            "it\n",
            this);
        return nsEventStatus_eIgnore;
      }
      if (touch->IsDuringFastFling()) {
        APZC_LOG("%p dropping long-press because of fast fling\n", this);
        return nsEventStatus_eIgnore;
      }
      uint64_t blockId = GetInputQueue()->InjectNewTouchBlock(this);
      controller->HandleTap(TapType::eLongTap, *geckoScreenPoint,
                            aEvent.modifiers, GetGuid(), blockId, Nothing());
      return nsEventStatus_eConsumeNoDefault;
    }
  }
  return nsEventStatus_eIgnore;
}

nsEventStatus AsyncPanZoomController::OnLongPressUp(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a long-tap-up in state %s\n", this,
                  ToString(mState).c_str());
  return GenerateSingleTap(TapType::eLongTapUp, aEvent.mPoint,
                           aEvent.modifiers);
}

nsEventStatus AsyncPanZoomController::GenerateSingleTap(
    TapType aType, const ScreenIntPoint& aPoint,
    mozilla::Modifiers aModifiers) {
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (controller) {
    if (Maybe<LayoutDevicePoint> geckoScreenPoint = ConvertToGecko(aPoint)) {
      TouchBlockState* touch = GetCurrentTouchBlock();
      // |touch| may be null in the case where this function is
      // invoked by GestureEventListener on a timeout. In that case we already
      // verified that the single tap is allowed so we let it through.
      // XXX there is a bug here that in such a case the touch block that
      // generated this tap will not get its mSingleTapOccurred flag set.
      // See https://bugzilla.mozilla.org/show_bug.cgi?id=1256344#c6
      if (touch) {
        if (touch->IsDuringFastFling()) {
          APZC_LOG(
              "%p dropping single-tap because it was during a fast-fling\n",
              this);
          return nsEventStatus_eIgnore;
        }

        // The below `single-tap-occurred` flag is only used to tell whether the
        // touch block caused a `click` event or not, thus for long-tap events,
        // it's not necessary.
        if (aType != TapType::eLongTapUp) {
          touch->SetSingleTapState(apz::SingleTapState::WasClick);
        }
      }
      // Because this may be being running as part of
      // APZCTreeManager::ReceiveInputEvent, calling controller->HandleTap
      // directly might mean that content receives the single tap message before
      // the corresponding touch-up. To avoid that we schedule the singletap
      // message to run on the next spin of the event loop. See bug 965381 for
      // the issue this was causing.
      APZC_LOG("posting runnable for HandleTap from GenerateSingleTap");
      RefPtr<Runnable> runnable =
          NewRunnableMethod<TapType, LayoutDevicePoint, mozilla::Modifiers,
                            ScrollableLayerGuid, uint64_t,
                            Maybe<DoubleTapToZoomMetrics>>(
              "layers::GeckoContentController::HandleTap", controller,
              &GeckoContentController::HandleTap, aType, *geckoScreenPoint,
              aModifiers, GetGuid(), touch ? touch->GetBlockId() : 0,
              Nothing());

      controller->PostDelayedTask(runnable.forget(), 0);
      return nsEventStatus_eConsumeNoDefault;
    }
  }
  return nsEventStatus_eIgnore;
}

void AsyncPanZoomController::OnTouchEndOrCancel() {
  mTouchScrollEventBuffer.clear();
  if (RefPtr<GeckoContentController> controller = GetGeckoContentController()) {
    MOZ_ASSERT(GetCurrentTouchBlock());
    controller->NotifyAPZStateChange(
        GetGuid(), APZStateChange::eEndTouch,
        static_cast<int>(GetCurrentTouchBlock()->SingleTapState()),
        Some(GetCurrentTouchBlock()->GetBlockId()));
  }
}

nsEventStatus AsyncPanZoomController::OnSingleTapUp(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a single-tap-up in state %s\n", this,
                  ToString(mState).c_str());
  // If mZoomConstraints.mAllowDoubleTapZoom is true we wait for a call to
  // OnSingleTapConfirmed before sending event to content
  MOZ_ASSERT(GetCurrentTouchBlock());
  if (!(ZoomConstraintsAllowDoubleTapZoom() &&
        GetCurrentTouchBlock()->TouchActionAllowsDoubleTapZoom())) {
    return GenerateSingleTap(TapType::eSingleTap, aEvent.mPoint,
                             aEvent.modifiers);
  }

  // Ignore the event if it does not have valid local coordinates.
  // GenerateSingleTap will not send a tap in this case.
  if (!ConvertToGecko(aEvent.mPoint)) {
    return nsEventStatus_eIgnore;
  }

  // Here we need to wait for the call to OnSingleTapConfirmed, we need to tell
  // it to ActiveElementManager so that we can do element activation once
  // ActiveElementManager got a single tap event later.
  if (TouchBlockState* touch = GetCurrentTouchBlock()) {
    if (!touch->IsDuringFastFling()) {
      touch->SetSingleTapState(apz::SingleTapState::NotYetDetermined);
    }
  }
  return nsEventStatus_eIgnore;
}

nsEventStatus AsyncPanZoomController::OnSingleTapConfirmed(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a single-tap-confirmed in state %s\n", this,
                  ToString(mState).c_str());
  return GenerateSingleTap(TapType::eSingleTap, aEvent.mPoint,
                           aEvent.modifiers);
}

nsEventStatus AsyncPanZoomController::OnDoubleTap(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a double-tap in state %s\n", this,
                  ToString(mState).c_str());

  MOZ_ASSERT(IsRootForLayersId(),
             "This function should be called for the root content APZC or "
             "OOPIF root APZC");

  CSSToCSSMatrix4x4 transformToRootContentApzc;
  RefPtr<AsyncPanZoomController> rootContentApzc;
  if (IsRootContent()) {
    rootContentApzc = RefPtr{this};
  } else {
    if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
      rootContentApzc = treeManagerLocal->FindZoomableApzc(this);
      if (rootContentApzc) {
        MOZ_ASSERT(rootContentApzc->GetLayersId() != GetLayersId());
        MOZ_ASSERT(this == treeManagerLocal->FindRootApzcFor(GetLayersId()));
        transformToRootContentApzc =
            treeManagerLocal->GetOopifToRootContentTransform(this);
      }
    }
  }

  if (!rootContentApzc) {
    return nsEventStatus_eIgnore;
  }

  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (controller) {
    if (rootContentApzc->ZoomConstraintsAllowDoubleTapZoom() &&
        (!GetCurrentTouchBlock() ||
         GetCurrentTouchBlock()->TouchActionAllowsDoubleTapZoom())) {
      if (Maybe<LayoutDevicePoint> geckoScreenPoint =
              ConvertToGecko(aEvent.mPoint)) {
        controller->HandleTap(
            TapType::eDoubleTap, *geckoScreenPoint, aEvent.modifiers, GetGuid(),
            GetCurrentTouchBlock() ? GetCurrentTouchBlock()->GetBlockId() : 0,
            Some(DoubleTapToZoomMetrics{rootContentApzc->GetVisualViewport(),
                                        rootContentApzc->GetScrollableRect(),
                                        transformToRootContentApzc}));
      }
    }
    return nsEventStatus_eConsumeNoDefault;
  }
  return nsEventStatus_eIgnore;
}

nsEventStatus AsyncPanZoomController::OnSecondTap(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a second-tap in state %s\n", this,
                  ToString(mState).c_str());
  return GenerateSingleTap(TapType::eSecondTap, aEvent.mPoint,
                           aEvent.modifiers);
}

nsEventStatus AsyncPanZoomController::OnCancelTap(
    const TapGestureInput& aEvent) {
  APZC_LOG_DETAIL("got a cancel-tap in state %s\n", this,
                  ToString(mState).c_str());
  // XXX: Implement this.
  return nsEventStatus_eIgnore;
}

ScreenToParentLayerMatrix4x4 AsyncPanZoomController::GetTransformToThis()
    const {
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    return treeManagerLocal->GetScreenToApzcTransform(this);
  }
  return ScreenToParentLayerMatrix4x4();
}

ScreenPoint AsyncPanZoomController::ToScreenCoordinates(
    const ParentLayerPoint& aVector, const ParentLayerPoint& aAnchor) const {
  return TransformVector(GetTransformToThis().Inverse(), aVector, aAnchor);
}

// TODO: figure out a good way to check the w-coordinate is positive and return
// the result
ParentLayerPoint AsyncPanZoomController::ToParentLayerCoordinates(
    const ScreenPoint& aVector, const ScreenPoint& aAnchor) const {
  return TransformVector(GetTransformToThis(), aVector, aAnchor);
}

ParentLayerPoint AsyncPanZoomController::ToParentLayerCoordinates(
    const ScreenPoint& aVector, const ExternalPoint& aAnchor) const {
  return ToParentLayerCoordinates(
      aVector,
      ViewAs<ScreenPixel>(aAnchor, PixelCastJustification::ExternalIsScreen));
}

ExternalPoint AsyncPanZoomController::ToExternalPoint(
    const ExternalPoint& aScreenOffset, const ScreenPoint& aScreenPoint) {
  return aScreenOffset +
         ViewAs<ExternalPixel>(aScreenPoint,
                               PixelCastJustification::ExternalIsScreen);
}

ScreenPoint AsyncPanZoomController::PanVector(const ExternalPoint& aPos) const {
  return ScreenPoint(fabs(aPos.x - mStartTouch.x),
                     fabs(aPos.y - mStartTouch.y));
}

bool AsyncPanZoomController::Contains(const ScreenIntPoint& aPoint) const {
  ScreenToParentLayerMatrix4x4 transformToThis = GetTransformToThis();
  Maybe<ParentLayerIntPoint> point = UntransformBy(transformToThis, aPoint);
  if (!point) {
    return false;
  }

  ParentLayerIntRect cb;
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    GetFrameMetrics().GetCompositionBounds().ToIntRect(&cb);
  }
  return cb.Contains(*point);
}

bool AsyncPanZoomController::IsInOverscrollGutter(
    const ScreenPoint& aHitTestPoint) const {
  if (!IsPhysicallyOverscrolled()) {
    return false;
  }

  Maybe<ParentLayerPoint> apzcPoint =
      UntransformBy(GetTransformToThis(), aHitTestPoint);
  if (!apzcPoint) return false;
  return IsInOverscrollGutter(*apzcPoint);
}

bool AsyncPanZoomController::IsInOverscrollGutter(
    const ParentLayerPoint& aHitTestPoint) const {
  ParentLayerRect compositionBounds;
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    compositionBounds = GetFrameMetrics().GetCompositionBounds();
  }
  if (!compositionBounds.Contains(aHitTestPoint)) {
    // Point is outside of scrollable element's bounds altogether.
    return false;
  }
  auto overscrollTransform = GetOverscrollTransform(eForEventHandling);
  ParentLayerPoint overscrollUntransformed =
      overscrollTransform.Inverse().TransformPoint(aHitTestPoint);

  if (compositionBounds.Contains(overscrollUntransformed)) {
    // Point is over scrollable content.
    return false;
  }

  // Point is in gutter.
  return true;
}

bool AsyncPanZoomController::IsOverscrolled() const {
  return mOverscrollEffect->IsOverscrolled();
}

bool AsyncPanZoomController::IsPhysicallyOverscrolled() const {
  // As an optimization, avoid calling Apply/UnapplyAsyncTestAttributes
  // unless we're in a test environment where we need it.
  if (StaticPrefs::apz_overscroll_test_async_scroll_offset_enabled()) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);
    return mX.IsOverscrolled() || mY.IsOverscrolled();
  }
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mX.IsOverscrolled() || mY.IsOverscrolled();
}

bool AsyncPanZoomController::IsInInvalidOverscroll() const {
  return mX.IsInInvalidOverscroll() || mY.IsInInvalidOverscroll();
}

ParentLayerPoint AsyncPanZoomController::PanStart() const {
  return ParentLayerPoint(mX.PanStart(), mY.PanStart());
}

const ParentLayerPoint AsyncPanZoomController::GetVelocityVector() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return ParentLayerPoint(mX.GetVelocity(), mY.GetVelocity());
}

void AsyncPanZoomController::SetVelocityVector(
    const ParentLayerPoint& aVelocityVector) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mX.SetVelocity(aVelocityVector.x);
  mY.SetVelocity(aVelocityVector.y);
}

void AsyncPanZoomController::HandlePanningWithTouchAction(double aAngle) {
  // Handling of cross sliding will need to be added in this method after
  // touch-action released enabled by default.
  MOZ_ASSERT(GetCurrentTouchBlock());
  RefPtr<const OverscrollHandoffChain> overscrollHandoffChain =
      GetCurrentInputBlock()->GetOverscrollHandoffChain();
  bool canScrollHorizontal =
      !mX.IsAxisLocked() && overscrollHandoffChain->CanScrollInDirection(
                                this, ScrollDirection::eHorizontal);
  bool canScrollVertical =
      !mY.IsAxisLocked() && overscrollHandoffChain->CanScrollInDirection(
                                this, ScrollDirection::eVertical);
  if (GetCurrentTouchBlock()->TouchActionAllowsPanningXY()) {
    if (canScrollHorizontal && canScrollVertical) {
      if (apz::IsCloseToHorizontal(aAngle,
                                   StaticPrefs::apz_axis_lock_lock_angle())) {
        mY.SetAxisLocked(true);
        SetState(PANNING_LOCKED_X);
      } else if (apz::IsCloseToVertical(
                     aAngle, StaticPrefs::apz_axis_lock_lock_angle())) {
        mX.SetAxisLocked(true);
        SetState(PANNING_LOCKED_Y);
      } else {
        SetState(PANNING);
      }
    } else if (canScrollHorizontal || canScrollVertical) {
      SetState(PANNING);
    } else {
      SetState(NOTHING);
    }
  } else if (GetCurrentTouchBlock()->TouchActionAllowsPanningX()) {
    // Using bigger angle for panning to keep behavior consistent
    // with IE.
    if (apz::IsCloseToHorizontal(
            aAngle, StaticPrefs::apz_axis_lock_direct_pan_angle())) {
      mY.SetAxisLocked(true);
      SetState(PANNING_LOCKED_X);
      mPanDirRestricted = true;
    } else {
      // Don't treat these touches as pan/zoom movements since 'touch-action'
      // value requires it.
      SetState(NOTHING);
    }
  } else if (GetCurrentTouchBlock()->TouchActionAllowsPanningY()) {
    if (apz::IsCloseToVertical(aAngle,
                               StaticPrefs::apz_axis_lock_direct_pan_angle())) {
      mX.SetAxisLocked(true);
      SetState(PANNING_LOCKED_Y);
      mPanDirRestricted = true;
    } else {
      SetState(NOTHING);
    }
  } else {
    SetState(NOTHING);
  }
  if (!IsInPanningState()) {
    // If we didn't enter a panning state because touch-action disallowed it,
    // make sure to clear any leftover velocity from the pre-threshold
    // touchmoves.
    mX.SetVelocity(0);
    mY.SetVelocity(0);
  }
}

void AsyncPanZoomController::HandlePanning(double aAngle) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  MOZ_ASSERT(GetCurrentInputBlock());
  RefPtr<const OverscrollHandoffChain> overscrollHandoffChain =
      GetCurrentInputBlock()->GetOverscrollHandoffChain();
  bool canScrollHorizontal =
      !mX.IsAxisLocked() && overscrollHandoffChain->CanScrollInDirection(
                                this, ScrollDirection::eHorizontal);
  bool canScrollVertical =
      !mY.IsAxisLocked() && overscrollHandoffChain->CanScrollInDirection(
                                this, ScrollDirection::eVertical);

  MOZ_ASSERT(UsingStatefulAxisLock());

  if (!canScrollHorizontal || !canScrollVertical) {
    SetState(PANNING);
  } else if (apz::IsCloseToHorizontal(
                 aAngle, StaticPrefs::apz_axis_lock_lock_angle())) {
    mY.SetAxisLocked(true);
    if (canScrollHorizontal) {
      SetState(PANNING_LOCKED_X);
    }
  } else if (apz::IsCloseToVertical(aAngle,
                                    StaticPrefs::apz_axis_lock_lock_angle())) {
    mX.SetAxisLocked(true);
    if (canScrollVertical) {
      SetState(PANNING_LOCKED_Y);
    }
  } else {
    SetState(PANNING);
  }
}

void AsyncPanZoomController::HandlePanningUpdate(
    const ScreenPoint& aPanDistance) {
  // If we're axis-locked, check if the user is trying to break the lock
  if ((GetAxisLockMode() == AxisLockMode::STICKY ||
       GetAxisLockMode() == AxisLockMode::BREAKABLE) &&
      !mPanDirRestricted) {
    ParentLayerPoint vector =
        ToParentLayerCoordinates(aPanDistance, mStartTouch);

    float angle = atan2f(vector.y, vector.x);  // range [-pi, pi]
    angle = fabsf(angle);                      // range [0, pi]

    float breakThreshold =
        StaticPrefs::apz_axis_lock_breakout_threshold() * GetDPI();

    if (fabs(aPanDistance.x) > breakThreshold ||
        fabs(aPanDistance.y) > breakThreshold) {
      switch (mState) {
        case PANNING_LOCKED_X:
          if (!apz::IsCloseToHorizontal(
                  angle, StaticPrefs::apz_axis_lock_breakout_angle())) {
            mY.SetAxisLocked(false);
            // If we are within the lock angle from the Y axis and STICKY,
            // lock onto the Y axis. BREAKABLE should not re-acquire the lock.
            if (apz::IsCloseToVertical(
                    angle, StaticPrefs::apz_axis_lock_lock_angle()) &&
                GetAxisLockMode() != AxisLockMode::BREAKABLE) {
              mX.SetAxisLocked(true);
              SetState(PANNING_LOCKED_Y);
            } else {
              SetState(PANNING);
            }
          }
          break;

        case PANNING_LOCKED_Y:
          if (!apz::IsCloseToVertical(
                  angle, StaticPrefs::apz_axis_lock_breakout_angle())) {
            mX.SetAxisLocked(false);
            // If we are within the lock angle from the X axis and STICKY,
            // lock onto the X axis. BREAKABLE should not re-acquire the lock.
            if (apz::IsCloseToHorizontal(
                    angle, StaticPrefs::apz_axis_lock_lock_angle()) &&
                GetAxisLockMode() != AxisLockMode::BREAKABLE) {
              mY.SetAxisLocked(true);
              SetState(PANNING_LOCKED_X);
            } else {
              SetState(PANNING);
            }
          }
          break;

        case PANNING:
          // `HandlePanning` can re-acquire the axis lock, which we don't want
          // to do if the lock is BREAKABLE
          if (GetAxisLockMode() != AxisLockMode::BREAKABLE) {
            HandlePanning(angle);
          }
          break;

        default:
          break;
      }
    }
  }
}

void AsyncPanZoomController::HandlePinchLocking(
    const PinchGestureInput& aEvent) {
  // Focus change and span distance calculated from an event buffer
  // Used to handle pinch locking irrespective of touch screen sensitivity
  // Note: both values fall back to the same value as
  //       their un-buffered counterparts if there is only one (the latest)
  //       event in the buffer. ie: when the touch screen is dispatching
  //       events slower than the lifetime of the buffer
  ParentLayerCoord bufferedSpanDistance;
  ParentLayerPoint focusPoint, bufferedFocusChange;
  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    focusPoint = mPinchEventBuffer.back().mLocalFocusPoint -
                 Metrics().GetCompositionBounds().TopLeft();
    ParentLayerPoint bufferedLastZoomFocus =
        (mPinchEventBuffer.size() > 1)
            ? mPinchEventBuffer.front().mLocalFocusPoint -
                  Metrics().GetCompositionBounds().TopLeft()
            : mLastZoomFocus;

    bufferedFocusChange = bufferedLastZoomFocus - focusPoint;
    bufferedSpanDistance = fabsf(mPinchEventBuffer.front().mPreviousSpan -
                                 mPinchEventBuffer.back().mCurrentSpan);
  }

  // Convert to screen coordinates
  ScreenCoord spanDistance =
      ToScreenCoordinates(ParentLayerPoint(0, bufferedSpanDistance), focusPoint)
          .Length();
  ScreenPoint focusChange =
      ToScreenCoordinates(bufferedFocusChange, focusPoint);

  if (mPinchLocked) {
    if (GetPinchLockMode() == PINCH_STICKY) {
      ScreenCoord spanBreakoutThreshold =
          StaticPrefs::apz_pinch_lock_span_breakout_threshold() * GetDPI();
      mPinchLocked = !(spanDistance > spanBreakoutThreshold);
    }
  } else {
    if (GetPinchLockMode() != PINCH_FREE) {
      ScreenCoord spanLockThreshold =
          StaticPrefs::apz_pinch_lock_span_lock_threshold() * GetDPI();
      ScreenCoord scrollLockThreshold =
          StaticPrefs::apz_pinch_lock_scroll_lock_threshold() * GetDPI();

      if (spanDistance < spanLockThreshold &&
          focusChange.Length() > scrollLockThreshold) {
        mPinchLocked = true;

        // We are transitioning to a two-finger pan that could trigger
        // a fling at its end, so start tracking velocity.
        StartTouch(aEvent.mLocalFocusPoint, aEvent.mTimeStamp);
      }
    }
  }
}

nsEventStatus AsyncPanZoomController::StartPanning(
    const ExternalPoint& aStartPoint, const TimeStamp& aEventTime) {
  ParentLayerPoint vector =
      ToParentLayerCoordinates(PanVector(aStartPoint), mStartTouch);
  double angle = atan2(vector.y, vector.x);  // range [-pi, pi]
  angle = fabs(angle);                       // range [0, pi]

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  HandlePanningWithTouchAction(angle);

  if (IsInPanningState()) {
    mTouchStartRestingTimeBeforePan = aEventTime - mTouchStartTime;
    mMinimumVelocityDuringPan = Nothing();

    if (RefPtr<GeckoContentController> controller =
            GetGeckoContentController()) {
      controller->NotifyAPZStateChange(GetGuid(),
                                       APZStateChange::eStartPanning);
    }
    return nsEventStatus_eConsumeNoDefault;
  }
  // Don't consume an event that didn't trigger a panning.
  return nsEventStatus_eIgnore;
}

void AsyncPanZoomController::UpdateWithTouchAtDevicePoint(
    const MultiTouchInput& aEvent) {
  const SingleTouchData& touchData = aEvent.mTouches[0];
  // Take historical touch data into account in order to improve the accuracy
  // of the velocity estimate. On many Android devices, the touch screen samples
  // at a higher rate than vsync (e.g. 100Hz vs 60Hz), and the historical data
  // lets us take advantage of those high-rate samples.
  for (const auto& historicalData : touchData.mHistoricalData) {
    ParentLayerPoint historicalPoint = historicalData.mLocalScreenPoint;
    mX.UpdateWithTouchAtDevicePoint(historicalPoint.x,
                                    historicalData.mTimeStamp);
    mY.UpdateWithTouchAtDevicePoint(historicalPoint.y,
                                    historicalData.mTimeStamp);
  }
  ParentLayerPoint point = touchData.mLocalScreenPoint;
  mX.UpdateWithTouchAtDevicePoint(point.x, aEvent.mTimeStamp);
  mY.UpdateWithTouchAtDevicePoint(point.y, aEvent.mTimeStamp);
}

Maybe<CompositionPayload> AsyncPanZoomController::NotifyScrollSampling() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mSampledState.front().TakeScrollPayload();
}

bool AsyncPanZoomController::AttemptScroll(
    ParentLayerPoint& aStartPoint, ParentLayerPoint& aEndPoint,
    OverscrollHandoffState& aOverscrollHandoffState) {
  // "start - end" rather than "end - start" because e.g. moving your finger
  // down (*positive* direction along y axis) causes the vertical scroll offset
  // to *decrease* as the page follows your finger.
  ParentLayerPoint displacement = aStartPoint - aEndPoint;

  ParentLayerPoint overscroll;  // will be used outside monitor block

  // If the direction of panning is reversed within the same input block,
  // a later event in the block could potentially scroll an APZC earlier
  // in the handoff chain, than an earlier event in the block (because
  // the earlier APZC was scrolled to its extent in the original direction).
  // We want to disallow this.
  bool scrollThisApzc = false;
  if (InputBlockState* block = GetCurrentInputBlock()) {
    scrollThisApzc =
        !block->GetScrolledApzc() || block->IsDownchainOfScrolledApzc(this);
  }

  ParentLayerPoint adjustedDisplacement;
  if (scrollThisApzc) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    AutoRecordCompositorScrollUpdate csu(
        this, CompositorScrollUpdate::Source::UserInteraction, lock);

    bool respectDisregardedDirections =
        ScrollSourceRespectsDisregardedDirections(
            aOverscrollHandoffState.mScrollSource);
    bool forcesVerticalOverscroll = respectDisregardedDirections &&
                                    mScrollMetadata.GetDisregardedDirection() ==
                                        Some(ScrollDirection::eVertical);
    bool forcesHorizontalOverscroll =
        respectDisregardedDirections &&
        mScrollMetadata.GetDisregardedDirection() ==
            Some(ScrollDirection::eHorizontal);

    bool yChanged =
        mY.AdjustDisplacement(displacement.y, adjustedDisplacement.y,
                              overscroll.y, forcesVerticalOverscroll);
    bool xChanged =
        mX.AdjustDisplacement(displacement.x, adjustedDisplacement.x,
                              overscroll.x, forcesHorizontalOverscroll);
    if (xChanged || yChanged) {
      ScheduleComposite();
    }

    if (!IsZero(adjustedDisplacement) &&
        Metrics().GetZoom() != CSSToParentLayerScale(0)) {
      ScrollBy(adjustedDisplacement / Metrics().GetZoom());
      if (InputBlockState* block = GetCurrentInputBlock()) {
        bool displacementIsUserVisible = true;

        {  // Release the APZC lock before calling ToScreenCoordinates which
          // acquires the APZ tree lock. Note that this just unlocks the mutex
          // once, so if we're locking it multiple times on the callstack then
          // this will be insufficient.
          RecursiveMutexAutoUnlock unlock(mRecursiveMutex);

          ScreenIntPoint screenDisplacement = RoundedToInt(
              ToScreenCoordinates(adjustedDisplacement, aStartPoint));
          // If the displacement we just applied rounds to zero in screen space,
          // then it's probably not going to be visible to the user. In that
          // case let's not mark this APZC as scrolled, so that even if the
          // immediate handoff pref is disabled, we'll allow doing the handoff
          // to the next APZC.
          if (screenDisplacement == ScreenIntPoint()) {
            displacementIsUserVisible = false;
          }
        }
        if (displacementIsUserVisible) {
          block->SetScrolledApzc(this);
        }
      }
      // Note that in the case of instant scrolling, the last snap target ids
      // will be set after AttemptScroll call so that we can clobber them
      // unconditionally here.
      mLastSnapTargetIds = ScrollSnapTargetIds{};
      ScheduleCompositeAndMaybeRepaint();
    }

    // Adjust the start point to reflect the consumed portion of the scroll.
    aStartPoint = aEndPoint + overscroll;
  } else {
    overscroll = displacement;
  }

  // Accumulate the amount of actual scrolling that occurred into the handoff
  // state. Note that ToScreenCoordinates() needs to be called outside the
  // mutex.
  if (!IsZero(adjustedDisplacement)) {
    aOverscrollHandoffState.mTotalMovement +=
        ToScreenCoordinates(adjustedDisplacement, aEndPoint);
  }

  // If we consumed the entire displacement as a normal scroll, great.
  if (IsZero(overscroll)) {
    return true;
  }

  if (AllowScrollHandoffInCurrentBlock()) {
    // If there is overscroll, first try to hand it off to an APZC later
    // in the handoff chain to consume (either as a normal scroll or as
    // overscroll).
    // Note: "+ overscroll" rather than "- overscroll" because "overscroll"
    // is what's left of "displacement", and "displacement" is "start - end".
    ++aOverscrollHandoffState.mChainIndex;
    bool consumed =
        CallDispatchScroll(aStartPoint, aEndPoint, aOverscrollHandoffState);
    if (consumed) {
      return true;
    }

    overscroll = aStartPoint - aEndPoint;
    MOZ_ASSERT(!IsZero(overscroll));
  }

  // If there is no APZC later in the handoff chain that accepted the
  // overscroll, try to accept it ourselves. We only accept it if we
  // are pannable.
  if (ScrollSourceAllowsOverscroll(aOverscrollHandoffState.mScrollSource)) {
    APZC_LOG("%p taking overscroll during panning\n", this);

    ParentLayerPoint prevVisualOverscroll = GetOverscrollAmount();

    OverscrollForPanning(overscroll, aOverscrollHandoffState.mPanDistance);

    // Accumulate the amount of change to the overscroll that occurred into the
    // handoff state. Note that the input amount, |overscroll|, is turned into
    // some smaller visual overscroll amount (queried via GetOverscrollAmount())
    // by applying resistance (Axis::ApplyResistance()), and it's the latter we
    // want to count towards OverscrollHandoffState::mTotalMovement.
    ParentLayerPoint visualOverscrollChange =
        GetOverscrollAmount() - prevVisualOverscroll;
    if (!IsZero(visualOverscrollChange)) {
      aOverscrollHandoffState.mTotalMovement +=
          ToScreenCoordinates(visualOverscrollChange, aEndPoint);
    }
  }

  aStartPoint = aEndPoint + overscroll;

  return IsZero(overscroll);
}

void AsyncPanZoomController::OverscrollForPanning(
    ParentLayerPoint& aOverscroll, const ScreenPoint& aPanDistance) {
  // Only allow entering overscroll along an axis if the pan distance along
  // that axis is greater than the pan distance along the other axis by a
  // configurable factor. If we are already overscrolled, don't check this.
  if (!IsOverscrolled()) {
    if (aPanDistance.x <
        StaticPrefs::apz_overscroll_min_pan_distance_ratio() * aPanDistance.y) {
      aOverscroll.x = 0;
    }
    if (aPanDistance.y <
        StaticPrefs::apz_overscroll_min_pan_distance_ratio() * aPanDistance.x) {
      aOverscroll.y = 0;
    }
  }

  OverscrollBy(aOverscroll);
}

ScrollDirections AsyncPanZoomController::GetOverscrollableDirections() const {
  ScrollDirections result;

  RecursiveMutexAutoLock lock(mRecursiveMutex);

  // If the target has the disregarded direction, it means it's single line
  // text control, thus we don't want to overscroll in both directions.
  if (mScrollMetadata.GetDisregardedDirection()) {
    return result;
  }

  if (mX.CanScroll() && mX.OverscrollBehaviorAllowsOverscrollEffect()) {
    result += ScrollDirection::eHorizontal;
  }

  if (mY.CanScroll() && mY.OverscrollBehaviorAllowsOverscrollEffect()) {
    result += ScrollDirection::eVertical;
  }

  return result;
}

void AsyncPanZoomController::OverscrollBy(ParentLayerPoint& aOverscroll) {
  if (!StaticPrefs::apz_overscroll_enabled()) {
    return;
  }

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  // Do not go into overscroll in a direction in which we have no room to
  // scroll to begin with.
  ScrollDirections overscrollableDirections = GetOverscrollableDirections();
  if (IsZero(aOverscroll.x)) {
    overscrollableDirections -= ScrollDirection::eHorizontal;
  }
  if (IsZero(aOverscroll.y)) {
    overscrollableDirections -= ScrollDirection::eVertical;
  }

  mOverscrollEffect->ConsumeOverscroll(aOverscroll, overscrollableDirections);
}

RefPtr<const OverscrollHandoffChain>
AsyncPanZoomController::BuildOverscrollHandoffChain() {
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    return treeManagerLocal->BuildOverscrollHandoffChain(this);
  }

  // This APZC IsDestroyed(). To avoid callers having to special-case this
  // scenario, just build a 1-element chain containing ourselves.
  OverscrollHandoffChain* result = new OverscrollHandoffChain;
  result->Add(this);
  return result;
}

ParentLayerPoint AsyncPanZoomController::AttemptFling(
    const FlingHandoffState& aHandoffState) {
  // The PLPPI computation acquires the tree lock, so it needs to be performed
  // on the controller thread, and before the APZC lock is acquired.
  APZThreadUtils::AssertOnControllerThread();
  float PLPPI = ComputePLPPI(PanStart(), aHandoffState.mVelocity);

  RecursiveMutexAutoLock lock(mRecursiveMutex);

  if (!IsPannable()) {
    return aHandoffState.mVelocity;
  }

  // We may have a pre-existing velocity for whatever reason (for example,
  // a previously handed off fling). We don't want to clobber that.
  APZC_LOG_DETAIL("accepting fling with velocity %s\n", this,
                  ToString(aHandoffState.mVelocity).c_str());
  ParentLayerPoint residualVelocity = aHandoffState.mVelocity;
  if (mX.CanScroll()) {
    mX.SetVelocity(mX.GetVelocity() + aHandoffState.mVelocity.x);
    residualVelocity.x = 0;
  }
  if (mY.CanScroll()) {
    mY.SetVelocity(mY.GetVelocity() + aHandoffState.mVelocity.y);
    residualVelocity.y = 0;
  }

  if (!aHandoffState.mIsHandoff && aHandoffState.mScrolledApzc == this) {
    residualVelocity.x = 0;
    residualVelocity.y = 0;
  }

  // If we're not scrollable in at least one of the directions in which we
  // were handed velocity, don't start a fling animation.
  // The |IsFinite()| condition should only fail when running some tests
  // that generate events faster than the clock resolution.
  ParentLayerPoint velocity = GetVelocityVector();
  if (!velocity.IsFinite() ||
      velocity.Length() <= StaticPrefs::apz_fling_min_velocity_threshold()) {
    // Relieve overscroll now if needed, since we will not transition to a fling
    // animation and then an overscroll animation, and relieve it then.
    aHandoffState.mChain->SnapBackOverscrolledApzc(this);
    return residualVelocity;
  }

  // If there's a scroll snap point near the predicted fling destination,
  // scroll there using a smooth scroll animation. Otherwise, start a
  // fling animation.
  ScrollSnapToDestination();
  if (mState != SMOOTHMSD_SCROLL) {
    SetState(FLING);
    RefPtr<AsyncPanZoomAnimation> fling =
        GetPlatformSpecificState()->CreateFlingAnimation(*this, aHandoffState,
                                                         PLPPI);
    StartAnimation(fling.forget());
  }

  return residualVelocity;
}

float AsyncPanZoomController::ComputePLPPI(ParentLayerPoint aPoint,
                                           ParentLayerPoint aDirection) const {
  // Avoid division-by-zero.
  if (aDirection == ParentLayerPoint()) {
    return GetDPI();
  }

  // Convert |aDirection| into a unit vector.
  aDirection = aDirection / aDirection.Length();

  // Place the vector at |aPoint| and convert to screen coordinates.
  // The length of the resulting vector is the number of Screen coordinates
  // that equal 1 ParentLayer coordinate in the given direction.
  float screenPerParent = ToScreenCoordinates(aDirection, aPoint).Length();

  // Finally, factor in the DPI scale.
  return GetDPI() / screenPerParent;
}

Maybe<CSSPoint> AsyncPanZoomController::GetCurrentAnimationDestination(
    const RecursiveMutexAutoLock& aProofOfLock) const {
  if (mState == WHEEL_SCROLL) {
    return Some(mAnimation->AsWheelScrollAnimation()->GetDestination());
  }
  if (mState == SMOOTH_SCROLL) {
    return Some(mAnimation->AsSmoothScrollAnimation()->GetDestination());
  }
  if (mState == SMOOTHMSD_SCROLL) {
    return Some(mAnimation->AsSmoothMsdScrollAnimation()->GetDestination());
  }
  if (mState == KEYBOARD_SCROLL) {
    return Some(mAnimation->AsSmoothScrollAnimation()->GetDestination());
  }

  return Nothing();
}

ParentLayerPoint
AsyncPanZoomController::AdjustHandoffVelocityForOverscrollBehavior(
    ParentLayerPoint& aHandoffVelocity) const {
  ParentLayerPoint residualVelocity;
  ScrollDirections handoffDirections = GetAllowedHandoffDirections();
  if (!handoffDirections.contains(ScrollDirection::eHorizontal)) {
    residualVelocity.x = aHandoffVelocity.x;
    aHandoffVelocity.x = 0;
  }
  if (!handoffDirections.contains(ScrollDirection::eVertical)) {
    residualVelocity.y = aHandoffVelocity.y;
    aHandoffVelocity.y = 0;
  }
  return residualVelocity;
}

bool AsyncPanZoomController::OverscrollBehaviorAllowsSwipe() const {
  // Swipe navigation is a "non-local" overscroll behavior like handoff.
  return GetAllowedHandoffDirections().contains(ScrollDirection::eHorizontal);
}

void AsyncPanZoomController::HandleFlingOverscroll(
    const ParentLayerPoint& aVelocity, SideBits aOverscrollSideBits,
    const RefPtr<const OverscrollHandoffChain>& aOverscrollHandoffChain,
    const RefPtr<const AsyncPanZoomController>& aScrolledApzc) {
  APZCTreeManager* treeManagerLocal = GetApzcTreeManager();
  if (treeManagerLocal) {
    const FlingHandoffState handoffState{
        aVelocity, aOverscrollHandoffChain, Nothing(),
        0,         true /* handoff */,      aScrolledApzc};
    ParentLayerPoint residualVelocity =
        treeManagerLocal->DispatchFling(this, handoffState);
    FLING_LOG("APZC %p left with residual velocity %s\n", this,
              ToString(residualVelocity).c_str());
    if (!IsZero(residualVelocity) && IsPannable() &&
        StaticPrefs::apz_overscroll_enabled()) {
      // Obey overscroll-behavior.
      RecursiveMutexAutoLock lock(mRecursiveMutex);
      if (!mX.OverscrollBehaviorAllowsOverscrollEffect()) {
        residualVelocity.x = 0;
      }
      if (!mY.OverscrollBehaviorAllowsOverscrollEffect()) {
        residualVelocity.y = 0;
      }

      // If there is velocity left over from the fling which could not
      // be handed off to another other APZC in the handoff chain,
      // start an overscroll animation which will enter overscroll
      // and then relieve it.
      if (!IsZero(residualVelocity)) {
        mOverscrollEffect->RelieveOverscroll(residualVelocity,
                                             aOverscrollSideBits);
      }

      // Additionally snap back any other APZC in the handoff chain
      // which may be overscrolled (e.g. an ancestor whose overscroll
      // animation may have been interrupted by the input gesture which
      // triggered the fling).
      aOverscrollHandoffChain->SnapBackOverscrolledApzcForMomentum(
          this, residualVelocity);
    }
  }
}

ParentLayerPoint AsyncPanZoomController::ConvertDestinationToDelta(
    CSSPoint& aDestination) const {
  ParentLayerPoint startPoint, endPoint;

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    startPoint = aDestination * Metrics().GetZoom();
    endPoint = Metrics().GetVisualScrollOffset() * Metrics().GetZoom();
  }

  return startPoint - endPoint;
}

void AsyncPanZoomController::SmoothScrollTo(
    CSSSnapDestination&& aDestination,
    ScrollTriggeredByScript aTriggeredByScript, const ScrollOrigin& aOrigin) {
  // Convert velocity from ParentLayerPoints/ms to ParentLayerPoints/s and then
  // to appunits/second.
  nsPoint destination = CSSPoint::ToAppUnits(aDestination.mPosition);
  nsSize velocity;
  if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
    velocity = CSSSize::ToAppUnits(ParentLayerSize(mX.GetVelocity() * 1000.0f,
                                                   mY.GetVelocity() * 1000.0f) /
                                   Metrics().GetZoom());
  }

  if (mState == SMOOTH_SCROLL && mAnimation) {
    RefPtr<SmoothScrollAnimation> animation(
        mAnimation->AsSmoothScrollAnimation());
    if (animation->GetScrollOrigin() == aOrigin) {
      APZC_LOG("%p updating destination on existing animation\n", this);
      animation->UpdateDestinationAndSnapTargets(
          GetFrameTime().Time(), destination, velocity,
          std::move(aDestination.mTargetIds), aTriggeredByScript);
      return;
    }
  }

  CancelAnimation();

  // If no scroll is required, we should exit early to avoid triggering
  // a scrollend event when no scrolling occurred.
  if (ConvertDestinationToDelta(aDestination.mPosition) == ParentLayerPoint()) {
    return;
  }

  SetState(SMOOTH_SCROLL);
  nsPoint initialPosition =
      CSSPoint::ToAppUnits(Metrics().GetVisualScrollOffset());
  RefPtr<SmoothScrollAnimation> animation =
      new SmoothScrollAnimation(*this, initialPosition, aOrigin);
  animation->UpdateDestinationAndSnapTargets(
      GetFrameTime().Time(), destination, velocity,
      std::move(aDestination.mTargetIds), aTriggeredByScript);
  StartAnimation(animation.forget());
}

void AsyncPanZoomController::SmoothMsdScrollTo(
    CSSSnapDestination&& aDestination,
    ScrollTriggeredByScript aTriggeredByScript) {
  if (mState == SMOOTHMSD_SCROLL && mAnimation) {
    APZC_LOG("%p updating destination on existing animation\n", this);
    RefPtr<SmoothMsdScrollAnimation> animation(
        static_cast<SmoothMsdScrollAnimation*>(mAnimation.get()));
    animation->SetDestination(aDestination.mPosition,
                              std::move(aDestination.mTargetIds),
                              aTriggeredByScript);
    return;
  }

  // If no scroll is required, we should exit early to avoid triggering
  // a scrollend event when no scrolling occurred.
  if (ConvertDestinationToDelta(aDestination.mPosition) == ParentLayerPoint()) {
    return;
  }
  CancelAnimation();
  SetState(SMOOTHMSD_SCROLL);
  // Convert velocity from ParentLayerPoints/ms to ParentLayerPoints/s.
  CSSPoint initialVelocity;
  if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
    initialVelocity = ParentLayerPoint(mX.GetVelocity() * 1000.0f,
                                       mY.GetVelocity() * 1000.0f) /
                      Metrics().GetZoom();
  }

  StartAnimation(do_AddRef(new SmoothMsdScrollAnimation(
      *this, Metrics().GetVisualScrollOffset(), initialVelocity,
      aDestination.mPosition,
      StaticPrefs::layout_css_scroll_behavior_spring_constant(),
      StaticPrefs::layout_css_scroll_behavior_damping_ratio(),
      std::move(aDestination.mTargetIds), aTriggeredByScript)));
}

void AsyncPanZoomController::StartOverscrollAnimation(
    const ParentLayerPoint& aVelocity, SideBits aOverscrollSideBits) {
  MOZ_ASSERT(mState != OVERSCROLL_ANIMATION);

  SetState(OVERSCROLL_ANIMATION);

  ParentLayerPoint velocity = aVelocity;
  AdjustDeltaForAllowedScrollDirections(velocity,
                                        GetOverscrollableDirections());
  StartAnimation(
      do_AddRef(new OverscrollAnimation(*this, velocity, aOverscrollSideBits)));
}

bool AsyncPanZoomController::CallDispatchScroll(
    ParentLayerPoint& aStartPoint, ParentLayerPoint& aEndPoint,
    OverscrollHandoffState& aOverscrollHandoffState) {
  // Make a local copy of the tree manager pointer and check if it's not
  // null before calling DispatchScroll(). This is necessary because
  // Destroy(), which nulls out mTreeManager, could be called concurrently.
  APZCTreeManager* treeManagerLocal = GetApzcTreeManager();
  if (!treeManagerLocal) {
    return false;
  }

  // Obey overscroll-behavior.
  ParentLayerPoint endPoint = aEndPoint;
  if (aOverscrollHandoffState.mChainIndex > 0) {
    ScrollDirections handoffDirections = GetAllowedHandoffDirections();
    if (!handoffDirections.contains(ScrollDirection::eHorizontal)) {
      endPoint.x = aStartPoint.x;
    }
    if (!handoffDirections.contains(ScrollDirection::eVertical)) {
      endPoint.y = aStartPoint.y;
    }
    if (aStartPoint == endPoint) {
      // Handoff not allowed in either direction - don't even bother.
      return false;
    }
  }

  return treeManagerLocal->DispatchScroll(this, aStartPoint, endPoint,
                                          aOverscrollHandoffState);
}

void AsyncPanZoomController::RecordScrollPayload(const TimeStamp& aTimeStamp) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (!mScrollPayload) {
    mScrollPayload = Some(
        CompositionPayload{CompositionPayloadType::eAPZScroll, aTimeStamp});
  }
}

void AsyncPanZoomController::StartTouch(const ParentLayerPoint& aPoint,
                                        TimeStamp aTimestamp) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mX.StartTouch(aPoint.x, aTimestamp);
  mY.StartTouch(aPoint.y, aTimestamp);
}

void AsyncPanZoomController::EndTouch(TimeStamp aTimestamp,
                                      Axis::ClearAxisLock aClearAxisLock) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mX.EndTouch(aTimestamp, aClearAxisLock);
  mY.EndTouch(aTimestamp, aClearAxisLock);
}

void AsyncPanZoomController::TrackTouch(const MultiTouchInput& aEvent) {
  mTouchScrollEventBuffer.push(aEvent);
  ExternalPoint extPoint = GetFirstExternalTouchPoint(aEvent);
  ExternalPoint refPoint;
  if (mTouchScrollEventBuffer.size() > 1) {
    refPoint = GetFirstExternalTouchPoint(mTouchScrollEventBuffer.front());
  } else {
    refPoint = mStartTouch;
  }

  ScreenPoint panVector = ViewAs<ScreenPixel>(
      extPoint - refPoint, PixelCastJustification::ExternalIsScreen);

  HandlePanningUpdate(panVector);

  ParentLayerPoint prevTouchPoint(mX.GetPos(), mY.GetPos());
  ParentLayerPoint touchPoint = GetFirstTouchPoint(aEvent);

  UpdateWithTouchAtDevicePoint(aEvent);

  auto velocity = GetVelocityVector().Length();
  if (mMinimumVelocityDuringPan) {
    mMinimumVelocityDuringPan =
        Some(std::min(*mMinimumVelocityDuringPan, velocity));
  } else {
    mMinimumVelocityDuringPan = Some(velocity);
  }

  if (prevTouchPoint != touchPoint) {
    MOZ_ASSERT(GetCurrentTouchBlock());
    OverscrollHandoffState handoffState(
        *GetCurrentTouchBlock()->GetOverscrollHandoffChain(),
        PanVector(extPoint), ScrollSource::Touchscreen);
    RecordScrollPayload(aEvent.mTimeStamp);
    CallDispatchScroll(prevTouchPoint, touchPoint, handoffState);
  }
}

ParentLayerPoint AsyncPanZoomController::GetFirstTouchPoint(
    const MultiTouchInput& aEvent) {
  return ((SingleTouchData&)aEvent.mTouches[0]).mLocalScreenPoint;
}

ExternalPoint AsyncPanZoomController::GetFirstExternalTouchPoint(
    const MultiTouchInput& aEvent) {
  return ToExternalPoint(aEvent.mScreenOffset,
                         ((SingleTouchData&)aEvent.mTouches[0]).mScreenPoint);
}

ParentLayerPoint AsyncPanZoomController::GetOverscrollAmount() const {
  if (StaticPrefs::apz_overscroll_test_async_scroll_offset_enabled()) {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);
    return GetOverscrollAmountInternal();
  }
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return GetOverscrollAmountInternal();
}

ParentLayerPoint AsyncPanZoomController::GetOverscrollAmountInternal() const {
  return {mX.GetOverscroll(), mY.GetOverscroll()};
}

SideBits AsyncPanZoomController::GetOverscrollSideBits() const {
  return apz::GetOverscrollSideBits({mX.GetOverscroll(), mY.GetOverscroll()});
}

void AsyncPanZoomController::RestoreOverscrollAmount(
    const ParentLayerPoint& aOverscroll) {
  mX.RestoreOverscroll(aOverscroll.x);
  mY.RestoreOverscroll(aOverscroll.y);
}

void AsyncPanZoomController::StartAnimation(
    already_AddRefed<AsyncPanZoomAnimation> aAnimation) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mAnimation = aAnimation;
  mLastSampleTime = GetFrameTime();
  ScheduleComposite();
}

void AsyncPanZoomController::CancelAnimation(CancelAnimationFlags aFlags) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  APZC_LOG_DETAIL("running CancelAnimation(0x%x) in state %s\n", this, aFlags,
                  ToString(mState).c_str());

  if (mAnimation) {
    mAnimation->Cancel(aFlags);
  }

  SetState(NOTHING);
  mLastSnapTargetIds = ScrollSnapTargetIds{};
  mAnimation = nullptr;
  // Since there is no animation in progress now the axes should
  // have no velocity either. If we are dropping the velocity from a non-zero
  // value we should trigger a repaint as the displayport margins are dependent
  // on the velocity and the last repaint request might not have good margins
  // any more.
  bool repaint = !IsZero(GetVelocityVector());
  mX.SetVelocity(0);
  mY.SetVelocity(0);
  mX.SetAxisLocked(false);
  mY.SetAxisLocked(false);
  // Setting the state to nothing and cancelling the animation can
  // preempt normal mechanisms for relieving overscroll, so we need to clear
  // overscroll here.
  if (!(aFlags & ExcludeOverscroll) && IsOverscrolled()) {
    ClearOverscroll();
    repaint = true;
  }
  // Similar to relieving overscroll, we also need to snap to any snap points
  // if appropriate.
  if (aFlags & CancelAnimationFlags::ScrollSnap) {
    ScrollSnap(ScrollSnapFlags::IntendedEndPosition);
  }
  if (repaint) {
    RequestContentRepaint();
    ScheduleComposite();
  }
}

void AsyncPanZoomController::ClearOverscroll() {
  mOverscrollEffect->ClearOverscroll();
}

void AsyncPanZoomController::ClearPhysicalOverscroll() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mX.ClearOverscroll();
  mY.ClearOverscroll();
}

void AsyncPanZoomController::SetCompositorController(
    CompositorController* aCompositorController) {
  mCompositorController = aCompositorController;
}

void AsyncPanZoomController::SetVisualScrollOffset(const CSSPoint& aOffset) {
  Metrics().SetVisualScrollOffset(aOffset);
  Metrics().RecalculateLayoutViewportOffset();
}

void AsyncPanZoomController::ClampAndSetVisualScrollOffset(
    const CSSPoint& aOffset) {
  Metrics().ClampAndSetVisualScrollOffset(aOffset);
  Metrics().RecalculateLayoutViewportOffset();
}

void AsyncPanZoomController::ScrollBy(const CSSPoint& aOffset) {
  SetVisualScrollOffset(Metrics().GetVisualScrollOffset() + aOffset);
}

void AsyncPanZoomController::ScrollByAndClamp(const CSSPoint& aOffset) {
  ClampAndSetVisualScrollOffset(Metrics().GetVisualScrollOffset() + aOffset);
}

void AsyncPanZoomController::ScaleWithFocus(float aScale,
                                            const CSSPoint& aFocus) {
  Metrics().ZoomBy(aScale);
  // We want to adjust the scroll offset such that the CSS point represented by
  // aFocus remains at the same position on the screen before and after the
  // change in zoom. The below code accomplishes this; see
  // https://bugzilla.mozilla.org/show_bug.cgi?id=923431#c6 for an in-depth
  // explanation of how.
  SetVisualScrollOffset((Metrics().GetVisualScrollOffset() + aFocus) -
                        (aFocus / aScale));
}

/*static*/
gfx::IntSize AsyncPanZoomController::GetDisplayportAlignmentMultiplier(
    const ScreenSize& aBaseSize) {
  gfx::IntSize multiplier(1, 1);
  float baseWidth = aBaseSize.width;
  while (baseWidth > 500) {
    baseWidth /= 2;
    multiplier.width *= 2;
    if (multiplier.width >= 8) {
      break;
    }
  }
  float baseHeight = aBaseSize.height;
  while (baseHeight > 500) {
    baseHeight /= 2;
    multiplier.height *= 2;
    if (multiplier.height >= 8) {
      break;
    }
  }
  return multiplier;
}

/**
 * Enlarges the displayport along both axes based on the velocity.
 */
static CSSSize CalculateDisplayPortSize(
    const CSSSize& aCompositionSize, const CSSPoint& aVelocity,
    AsyncPanZoomController::ZoomInProgress aZoomInProgress,
    const CSSToScreenScale2D& aDpPerCSS) {
  bool xIsStationarySpeed =
      fabsf(aVelocity.x) < StaticPrefs::apz_min_skate_speed();
  bool yIsStationarySpeed =
      fabsf(aVelocity.y) < StaticPrefs::apz_min_skate_speed();
  float xMultiplier = xIsStationarySpeed
                          ? StaticPrefs::apz_x_stationary_size_multiplier()
                          : StaticPrefs::apz_x_skate_size_multiplier();
  float yMultiplier = yIsStationarySpeed
                          ? StaticPrefs::apz_y_stationary_size_multiplier()
                          : StaticPrefs::apz_y_skate_size_multiplier();

  if (IsHighMemSystem() && !xIsStationarySpeed) {
    xMultiplier += StaticPrefs::apz_x_skate_highmem_adjust();
  }

  if (IsHighMemSystem() && !yIsStationarySpeed) {
    yMultiplier += StaticPrefs::apz_y_skate_highmem_adjust();
  }

  if (aZoomInProgress == AsyncPanZoomController::ZoomInProgress::Yes) {
    // If a zoom is in progress, we will be making content visible on the
    // x and y axes in equal proportion, because the zoom operation scales
    // equally on the x and y axes. The default multipliers computed above are
    // biased towards the y-axis since that's where most scrolling occurs, but
    // in the case of zooming, we should really use equal multipliers on both
    // axes. This does that while preserving the total displayport area
    // quantity (aCompositionSize.Area() * xMultiplier * yMultiplier).
    // Note that normally changing the shape of the displayport is expensive
    // and should be avoided, but if a zoom is in progress the displayport
    // is likely going to be fully repainted anyway due to changes in resolution
    // so there should be no marginal cost to also changing the shape of it.
    float areaMultiplier = xMultiplier * yMultiplier;
    xMultiplier = sqrt(areaMultiplier);
    yMultiplier = xMultiplier;
  }

  // Scale down the margin multipliers by the alignment multiplier because
  // the alignment code will expand the displayport outward to the multiplied
  // alignment. This is not necessary for correctness, but for performance;
  // if we don't do this the displayport can end up much larger. The math here
  // is actually just scaling the part of the multipler that is > 1, so that
  // we never end up with xMultiplier or yMultiplier being less than 1 (that
  // would result in a guaranteed checkerboarding situation). Note that the
  // calculation doesn't cancel exactly the increased margin from applying
  // the alignment multiplier, but this is simple and should provide
  // reasonable behaviour in most cases.
  gfx::IntSize alignmentMultipler =
      AsyncPanZoomController::GetDisplayportAlignmentMultiplier(
          aCompositionSize * aDpPerCSS);
  if (xMultiplier > 1) {
    xMultiplier = ((xMultiplier - 1) / alignmentMultipler.width) + 1;
  }
  if (yMultiplier > 1) {
    yMultiplier = ((yMultiplier - 1) / alignmentMultipler.height) + 1;
  }

  return aCompositionSize * CSSSize(xMultiplier, yMultiplier);
}

/**
 * Ensures that the displayport is at least as large as the visible area
 * inflated by the danger zone. If this is not the case then the
 * "AboutToCheckerboard" function in TiledContentClient.cpp will return true
 * even in the stable state.
 */
static CSSSize ExpandDisplayPortToDangerZone(
    const CSSSize& aDisplayPortSize, const FrameMetrics& aFrameMetrics) {
  CSSSize dangerZone(0.0f, 0.0f);
  if (aFrameMetrics.DisplayportPixelsPerCSSPixel().xScale != 0 &&
      aFrameMetrics.DisplayportPixelsPerCSSPixel().yScale != 0) {
    dangerZone = ScreenSize(StaticPrefs::apz_danger_zone_x(),
                            StaticPrefs::apz_danger_zone_y()) /
                 aFrameMetrics.DisplayportPixelsPerCSSPixel();
  }
  const CSSSize compositionSize =
      aFrameMetrics.CalculateBoundedCompositedSizeInCssPixels();

  const float xSize = std::max(aDisplayPortSize.width,
                               compositionSize.width + (2 * dangerZone.width));

  const float ySize =
      std::max(aDisplayPortSize.height,
               compositionSize.height + (2 * dangerZone.height));

  return CSSSize(xSize, ySize);
}

/**
 * Attempts to redistribute any area in the displayport that would get clipped
 * by the scrollable rect, or be inaccessible due to disabled scrolling, to the
 * other axis, while maintaining total displayport area.
 */
static void RedistributeDisplayPortExcess(CSSSize& aDisplayPortSize,
                                          const CSSRect& aScrollableRect) {
  // As aDisplayPortSize.height * aDisplayPortSize.width does not change,
  // we are just scaling by the ratio and its inverse.
  if (aDisplayPortSize.height > aScrollableRect.Height()) {
    aDisplayPortSize.width *=
        (aDisplayPortSize.height / aScrollableRect.Height());
    aDisplayPortSize.height = aScrollableRect.Height();
  } else if (aDisplayPortSize.width > aScrollableRect.Width()) {
    aDisplayPortSize.height *=
        (aDisplayPortSize.width / aScrollableRect.Width());
    aDisplayPortSize.width = aScrollableRect.Width();
  }
}

/* static */
const ScreenMargin AsyncPanZoomController::CalculatePendingDisplayPort(
    const FrameMetrics& aFrameMetrics, const ParentLayerPoint& aVelocity,
    ZoomInProgress aZoomInProgress) {
  if (aFrameMetrics.IsScrollInfoLayer()) {
    // Don't compute margins. Since we can't asynchronously scroll this frame,
    // we don't want to paint anything more than the composition bounds.
    return ScreenMargin();
  }

  CSSSize compositionSize =
      aFrameMetrics.CalculateBoundedCompositedSizeInCssPixels();
  CSSPoint velocity;
  if (aFrameMetrics.GetZoom() != CSSToParentLayerScale(0)) {
    velocity = aVelocity / aFrameMetrics.GetZoom();  // avoid division by zero
  }
  CSSRect scrollableRect = aFrameMetrics.GetExpandedScrollableRect();

  // Calculate the displayport size based on how fast we're moving along each
  // axis.
  CSSSize displayPortSize =
      CalculateDisplayPortSize(compositionSize, velocity, aZoomInProgress,
                               aFrameMetrics.DisplayportPixelsPerCSSPixel());

  displayPortSize =
      ExpandDisplayPortToDangerZone(displayPortSize, aFrameMetrics);

  if (StaticPrefs::apz_enlarge_displayport_when_clipped()) {
    RedistributeDisplayPortExcess(displayPortSize, scrollableRect);
  }

  // We calculate a "displayport" here which is relative to the scroll offset.
  // Note that the scroll offset we have here in the APZ code may not be the
  // same as the base rect that gets used on the layout side when the
  // displayport margins are actually applied, so it is important to only
  // consider the displayport as margins relative to a scroll offset rather than
  // relative to something more unchanging like the scrollable rect origin.

  // Center the displayport based on its expansion over the composition size.
  CSSRect displayPort((compositionSize.width - displayPortSize.width) / 2.0f,
                      (compositionSize.height - displayPortSize.height) / 2.0f,
                      displayPortSize.width, displayPortSize.height);

  // Offset the displayport, depending on how fast we're moving and the
  // estimated time it takes to paint, to try to minimise checkerboarding.
  float paintFactor = kDefaultEstimatedPaintDurationMs;
  displayPort.MoveBy(velocity * paintFactor * StaticPrefs::apz_velocity_bias());

  APZC_LOGV_FM(aFrameMetrics,
               "Calculated displayport as %s from velocity %s zooming %d paint "
               "time %f metrics",
               ToString(displayPort).c_str(), ToString(aVelocity).c_str(),
               (int)aZoomInProgress, paintFactor);

  CSSMargin cssMargins;
  cssMargins.left = -displayPort.X();
  cssMargins.top = -displayPort.Y();
  cssMargins.right =
      displayPort.Width() - compositionSize.width - cssMargins.left;
  cssMargins.bottom =
      displayPort.Height() - compositionSize.height - cssMargins.top;

  return cssMargins * aFrameMetrics.DisplayportPixelsPerCSSPixel();
}

void AsyncPanZoomController::ScheduleComposite() {
  if (mCompositorController) {
    mCompositorController->ScheduleRenderOnCompositorThread(
        wr::RenderReasons::APZ);
  }
}

void AsyncPanZoomController::ScheduleCompositeAndMaybeRepaint() {
  ScheduleComposite();
  RequestContentRepaint();
}

void AsyncPanZoomController::FlushRepaintForOverscrollHandoff() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  RequestContentRepaint();
}

void AsyncPanZoomController::FlushRepaintForNewInputBlock() {
  APZC_LOG("%p flushing repaint for new input block\n", this);

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  RequestContentRepaint();
}

bool AsyncPanZoomController::SnapBackIfOverscrolled() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (SnapBackIfOverscrolledForMomentum(ParentLayerPoint(0, 0))) {
    return true;
  }
  // If we don't kick off an overscroll animation, we still need to snap to any
  // nearby snap points, assuming we haven't already done so when we started
  // this fling
  if (mState != FLING) {
    ScrollSnap(ScrollSnapFlags::IntendedEndPosition);
  }
  return false;
}

bool AsyncPanZoomController::SnapBackIfOverscrolledForMomentum(
    const ParentLayerPoint& aVelocity) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  // It's possible that we're already in the middle of an overscroll
  // animation - if so, don't start a new one.
  if (IsOverscrolled() && mState != OVERSCROLL_ANIMATION) {
    APZC_LOG("%p is overscrolled, starting snap-back\n", this);
    mOverscrollEffect->RelieveOverscroll(aVelocity, GetOverscrollSideBits());
    return true;
  }
  return false;
}

bool AsyncPanZoomController::IsFlingingFast() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  if (mState == FLING && GetVelocityVector().Length() >
                             StaticPrefs::apz_fling_stop_on_tap_threshold()) {
    APZC_LOG("%p is moving fast\n", this);
    return true;
  }
  return false;
}

bool AsyncPanZoomController::IsPannable() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mX.CanScroll() || mY.CanScroll();
}

bool AsyncPanZoomController::IsScrollInfoLayer() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return Metrics().IsScrollInfoLayer();
}

int32_t AsyncPanZoomController::GetLastTouchIdentifier() const {
  RefPtr<GestureEventListener> listener = GetGestureEventListener();
  return listener ? listener->GetLastTouchIdentifier() : -1;
}

void AsyncPanZoomController::RequestContentRepaint(
    RepaintUpdateType aUpdateType) {
  // Reinvoke this method on the repaint thread if it's not there already. It's
  // important to do this before the call to CalculatePendingDisplayPort, so
  // that CalculatePendingDisplayPort uses the most recent available version of
  // Metrics(). just before the paint request is dispatched to content.
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (!controller) {
    return;
  }
  if (!controller->IsRepaintThread()) {
    // Even though we want to do the actual repaint request on the repaint
    // thread, we want to update the expected gecko metrics synchronously.
    // Otherwise we introduce a race condition where we might read from the
    // expected gecko metrics on the controller thread before or after it gets
    // updated on the repaint thread, when in fact we always want the updated
    // version when reading.
    {  // scope lock
      RecursiveMutexAutoLock lock(mRecursiveMutex);
      mExpectedGeckoMetrics.UpdateFrom(Metrics());
    }

    // use the local variable to resolve the function overload.
    auto func =
        static_cast<void (AsyncPanZoomController::*)(RepaintUpdateType)>(
            &AsyncPanZoomController::RequestContentRepaint);
    controller->DispatchToRepaintThread(NewRunnableMethod<RepaintUpdateType>(
        "layers::AsyncPanZoomController::RequestContentRepaint", this, func,
        aUpdateType));
    return;
  }

  MOZ_ASSERT(controller->IsRepaintThread());

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  ParentLayerPoint velocity = GetVelocityVector();
  ScreenMargin displayportMargins = CalculatePendingDisplayPort(
      Metrics(), velocity,
      (mState == PINCHING || mState == ANIMATING_ZOOM) ? ZoomInProgress::Yes
                                                       : ZoomInProgress::No);
  Metrics().SetPaintRequestTime(TimeStamp::Now());
  RequestContentRepaint(velocity, displayportMargins, aUpdateType);
}

static CSSRect GetDisplayPortRect(const FrameMetrics& aFrameMetrics,
                                  const ScreenMargin& aDisplayportMargins) {
  // This computation is based on what happens in CalculatePendingDisplayPort.
  // If that changes then this might need to change too.
  // Note that the display port rect APZ computes is relative to the visual
  // scroll offset. It's adjusted to be relative to the layout scroll offset
  // when the main thread processes a repaint request (in
  // APZCCallbackHelper::AdjustDisplayPortForScrollDelta()) and ultimately
  // applied (in DisplayPortUtils::GetDisplayPort()) in this adjusted form.
  CSSRect baseRect(aFrameMetrics.GetVisualScrollOffset(),
                   aFrameMetrics.CalculateBoundedCompositedSizeInCssPixels());
  baseRect.Inflate(aDisplayportMargins /
                   aFrameMetrics.DisplayportPixelsPerCSSPixel());
  return baseRect;
}

void AsyncPanZoomController::RequestContentRepaint(
    const ParentLayerPoint& aVelocity, const ScreenMargin& aDisplayportMargins,
    RepaintUpdateType aUpdateType) {
  mRecursiveMutex.AssertCurrentThreadIn();

  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (!controller) {
    return;
  }
  MOZ_ASSERT(controller->IsRepaintThread());

  APZScrollAnimationType animationType = APZScrollAnimationType::No;
  if (mAnimation) {
    animationType = mAnimation->WasTriggeredByScript()
                        ? APZScrollAnimationType::TriggeredByScript
                        : APZScrollAnimationType::TriggeredByUserInput;
  }
  RepaintRequest request(Metrics(), aDisplayportMargins, aUpdateType,
                         animationType, mScrollGeneration, mLastSnapTargetIds,
                         IsInScrollingGesture());

  if (request.IsRootContent() && request.GetZoom() != mLastNotifiedZoom &&
      mState != PINCHING && mState != ANIMATING_ZOOM) {
    controller->NotifyScaleGestureComplete(
        GetGuid(),
        (request.GetZoom() / request.GetDevPixelsPerCSSPixel()).scale);
    mLastNotifiedZoom = request.GetZoom();
  }

  // If we're trying to paint what we already think is painted, discard this
  // request since it's a pointless paint.
  if (request.GetDisplayPortMargins().WithinEpsilonOf(
          mLastPaintRequestMetrics.GetDisplayPortMargins(), EPSILON) &&
      request.GetVisualScrollOffset().WithinEpsilonOf(
          mLastPaintRequestMetrics.GetVisualScrollOffset(), EPSILON) &&
      request.GetPresShellResolution() ==
          mLastPaintRequestMetrics.GetPresShellResolution() &&
      request.GetZoom() == mLastPaintRequestMetrics.GetZoom() &&
      request.GetLayoutViewport().WithinEpsilonOf(
          mLastPaintRequestMetrics.GetLayoutViewport(), EPSILON) &&
      request.GetScrollGeneration() ==
          mLastPaintRequestMetrics.GetScrollGeneration() &&
      request.GetScrollUpdateType() ==
          mLastPaintRequestMetrics.GetScrollUpdateType() &&
      request.GetScrollAnimationType() ==
          mLastPaintRequestMetrics.GetScrollAnimationType() &&
      request.GetLastSnapTargetIds() ==
          mLastPaintRequestMetrics.GetLastSnapTargetIds()) {
    return;
  }

  APZC_LOGV("%p requesting content repaint %s", this,
            ToString(request).c_str());
  {  // scope lock
    MutexAutoLock lock(mCheckerboardEventLock);
    if (mCheckerboardEvent && mCheckerboardEvent->IsRecordingTrace()) {
      std::stringstream info;
      info << " velocity " << aVelocity;
      std::string str = info.str();
      mCheckerboardEvent->UpdateRendertraceProperty(
          CheckerboardEvent::RequestedDisplayPort,
          GetDisplayPortRect(Metrics(), aDisplayportMargins), str);
    }
  }

  controller->RequestContentRepaint(request);
  mExpectedGeckoMetrics.UpdateFrom(Metrics());
  mLastPaintRequestMetrics = request;

  // We're holding the APZC lock here, so redispatch this so we can get
  // the tree lock without the APZC lock.
  controller->DispatchToRepaintThread(
      NewRunnableMethod<AsyncPanZoomController*>(
          "layers::APZCTreeManager::SendSubtreeTransformsToChromeMainThread",
          GetApzcTreeManager(),
          &APZCTreeManager::SendSubtreeTransformsToChromeMainThread, this));
}

bool AsyncPanZoomController::UpdateAnimation(
    const RecursiveMutexAutoLock& aProofOfLock, const SampleTime& aSampleTime,
    nsTArray<RefPtr<Runnable>>* aOutDeferredTasks) {
  AssertOnSamplerThread();

  // This function may get called multiple with the same sample time, if we
  // composite multiple times at the same timestamp.
  // However we only want to do one animation step per composition so we need
  // to deduplicate these calls first.
  // Even if there's no animation, if we have a scroll offset change pending due
  // to the frame delay, we need to keep compositing.
  if (mLastSampleTime == aSampleTime) {
    APZC_LOGV_DETAIL(
        "UpdateAnimation short-circuit, animation=%p, pending frame-delayed "
        "offset=%d\n",
        this, mAnimation.get(), HavePendingFrameDelayedOffset());
    return !!mAnimation || HavePendingFrameDelayedOffset();
  }

  // We're at a new timestamp, so advance to the next sample in the deque, if
  // there is one. That one will be used for all the code that reads the
  // eForCompositing transforms in this vsync interval.
  AdvanceToNextSample();

  // And then create a new sample, which will be used in the *next* vsync
  // interval. We do the sample at this point and not later in order to try
  // and enforce one frame delay between computing the async transform and
  // compositing it to the screen. This one-frame delay gives code running on
  // the main thread a chance to try and respond to the scroll position change,
  // so that e.g. a main-thread animation can stay in sync with user-driven
  // scrolling or a compositor animation.
  bool needComposite = SampleCompositedAsyncTransform(aProofOfLock);
  APZC_LOGV_DETAIL("UpdateAnimation needComposite=%d mAnimation=%p\n", this,
                   needComposite, mAnimation.get());

  TimeDuration sampleTimeDelta = aSampleTime - mLastSampleTime;
  mLastSampleTime = aSampleTime;

  if (needComposite || mAnimation) {
    // Bump the scroll generation before we call RequestContentRepaint below
    // so that the RequestContentRepaint call will surely use the new
    // generation.
    if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
      mScrollGeneration = treeManagerLocal->NewAPZScrollGeneration();
    }
  }

  if (mAnimation) {
    AutoRecordCompositorScrollUpdate csu(
        this,
        mAnimation->WasTriggeredByScript()
            ? CompositorScrollUpdate::Source::Other
            : CompositorScrollUpdate::Source::UserInteraction,
        aProofOfLock);
    bool continueAnimation = mAnimation->Sample(Metrics(), sampleTimeDelta);
    bool wantsRepaints = mAnimation->WantsRepaints();
    *aOutDeferredTasks = mAnimation->TakeDeferredTasks();
    if (!continueAnimation) {
      SetState(NOTHING);
      if (mAnimation->AsSmoothMsdScrollAnimation()) {
        {
          RecursiveMutexAutoLock lock(mRecursiveMutex);
          mLastSnapTargetIds =
              mAnimation->AsSmoothMsdScrollAnimation()->TakeSnapTargetIds();
        }
      } else if (mAnimation->AsSmoothScrollAnimation()) {
        RecursiveMutexAutoLock lock(mRecursiveMutex);
        mLastSnapTargetIds =
            mAnimation->AsSmoothScrollAnimation()->TakeSnapTargetIds();
      }
      mAnimation = nullptr;
    }
    // Request a repaint at the end of the animation in case something such as a
    // call to NotifyLayersUpdated was invoked during the animation and Gecko's
    // current state is some intermediate point of the animation.
    if (!continueAnimation || wantsRepaints) {
      RequestContentRepaint();
    }
    needComposite = true;
  }
  return needComposite;
}

AsyncTransformComponentMatrix AsyncPanZoomController::GetOverscrollTransform(
    AsyncTransformConsumer aMode) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);

  if (aMode == eForCompositing && mScrollMetadata.IsApzForceDisabled()) {
    return AsyncTransformComponentMatrix();
  }

  if (!IsPhysicallyOverscrolled()) {
    return AsyncTransformComponentMatrix();
  }

  // The overscroll effect is a simple translation by the overscroll offset.
  ParentLayerPoint overscrollOffset(-mX.GetOverscroll(), -mY.GetOverscroll());
  return AsyncTransformComponentMatrix().PostTranslate(overscrollOffset.x,
                                                       overscrollOffset.y, 0);
}

bool AsyncPanZoomController::AdvanceAnimations(const SampleTime& aSampleTime) {
  AssertOnSamplerThread();

  // Don't send any state-change notifications until the end of the function,
  // because we may go through some intermediate states while we finish
  // animations and start new ones.
  ThreadSafeStateChangeNotificationBlocker blocker(this);

  // The eventual return value of this function. The compositor needs to know
  // whether or not to advance by a frame as soon as it can. For example, if a
  // fling is happening, it has to keep compositing so that the animation is
  // smooth. If an animation frame is requested, it is the compositor's
  // responsibility to schedule a composite.
  bool requestAnimationFrame = false;
  nsTArray<RefPtr<Runnable>> deferredTasks;

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    {  // scope lock
      CSSRect visibleRect = GetVisibleRect(lock);
      MutexAutoLock lock2(mCheckerboardEventLock);
      // Update RendertraceProperty before UpdateAnimation() call, since
      // the UpdateAnimation() updates effective ScrollOffset for next frame
      // if APZFrameDelay is enabled.
      if (mCheckerboardEvent) {
        mCheckerboardEvent->UpdateRendertraceProperty(
            CheckerboardEvent::UserVisible, visibleRect);
      }
    }

    requestAnimationFrame = UpdateAnimation(lock, aSampleTime, &deferredTasks);
  }
  // Execute any deferred tasks queued up by mAnimation's Sample() (called by
  // UpdateAnimation()). This needs to be done after the monitor is released
  // since the tasks are allowed to call APZCTreeManager methods which can grab
  // the tree lock.
  // Move the ThreadSafeStateChangeNotificationBlocker into the task so that
  // notifications continue to be blocked until the deferred tasks have run.
  // Must be the ThreadSafe variant of StateChangeNotificationBlocker to
  // guarantee that the APZ is alive until the deferred tasks are done
  if (!deferredTasks.IsEmpty()) {
    APZThreadUtils::RunOnControllerThread(NS_NewRunnableFunction(
        "AsyncPanZoomController::AdvanceAnimations deferred tasks",
        [blocker = std::move(blocker),
         deferredTasks = std::move(deferredTasks)]() {
          for (uint32_t i = 0; i < deferredTasks.Length(); ++i) {
            deferredTasks[i]->Run();
          }
        }));
  }

  // If any of the deferred tasks starts a new animation, it will request a
  // new composite directly, so we can just return requestAnimationFrame here.
  return requestAnimationFrame;
}

ParentLayerPoint AsyncPanZoomController::GetCurrentAsyncScrollOffset(
    AsyncTransformConsumer aMode) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);

  return GetEffectiveScrollOffset(aMode, lock) * GetEffectiveZoom(aMode, lock);
}

CSSRect AsyncPanZoomController::GetCurrentAsyncVisualViewport(
    AsyncTransformConsumer aMode) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);

  return CSSRect(
      GetEffectiveScrollOffset(aMode, lock),
      FrameMetrics::CalculateCompositedSizeInCssPixels(
          Metrics().GetCompositionBounds(), GetEffectiveZoom(aMode, lock)));
}

AsyncTransform AsyncPanZoomController::GetCurrentAsyncTransform(
    AsyncTransformConsumer aMode, AsyncTransformComponents aComponents,
    std::size_t aSampleIndex) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);

  CSSToParentLayerScale effectiveZoom;
  if (aComponents.contains(AsyncTransformComponent::eVisual)) {
    effectiveZoom = GetEffectiveZoom(aMode, lock, aSampleIndex);
  } else {
    effectiveZoom =
        Metrics().LayersPixelsPerCSSPixel() * LayerToParentLayerScale(1.0f);
  }

  LayerToParentLayerScale compositedAsyncZoom =
      effectiveZoom / Metrics().LayersPixelsPerCSSPixel();

  ParentLayerPoint translation;
  if (aComponents.contains(AsyncTransformComponent::eVisual)) {
    // There is no "lastPaintVisualOffset" to subtract here; the visual offset
    // is entirely async.

    CSSPoint currentVisualOffset =
        GetEffectiveScrollOffset(aMode, lock, aSampleIndex) -
        GetEffectiveLayoutViewport(aMode, lock, aSampleIndex).TopLeft();

    translation += currentVisualOffset * effectiveZoom;
  }
  if (aComponents.contains(AsyncTransformComponent::eLayout)) {
    CSSPoint lastPaintLayoutOffset;
    if (mLastContentPaintMetrics.IsScrollable()) {
      lastPaintLayoutOffset = mLastContentPaintMetrics.GetLayoutScrollOffset();
    }

    CSSPoint currentLayoutOffset =
        GetEffectiveLayoutViewport(aMode, lock, aSampleIndex).TopLeft();

    translation +=
        (currentLayoutOffset - lastPaintLayoutOffset) * effectiveZoom;
  }

  return AsyncTransform(compositedAsyncZoom, -translation);
}

AsyncTransformComponentMatrix
AsyncPanZoomController::GetAsyncTransformForInputTransformation(
    AsyncTransformComponents aComponents, LayersId aForLayersId) const {
  AsyncTransformComponentMatrix result;
  // If we are the root, and |aForLayersId| is different from our LayersId,
  // |aForLayersId| must be in a remote subdocument.
  if (IsRootContent() && aForLayersId != GetLayersId()) {
    result =
        ViewAs<AsyncTransformComponentMatrix>(GetPaintedResolutionTransform());
  }
  // Order of transforms: the painted resolution (if any) applies first, and
  // any async transform on top of that.
  result = result * AsyncTransformComponentMatrix(GetCurrentAsyncTransform(
                        eForEventHandling, aComponents));
  // The overscroll transform is considered part of the layout component of
  // the async transform, because it should not apply to fixed content.
  if (aComponents.contains(AsyncTransformComponent::eLayout)) {
    result = result * GetOverscrollTransform(eForEventHandling);
  }
  return result;
}

Matrix4x4 AsyncPanZoomController::GetPaintedResolutionTransform() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  MOZ_ASSERT(IsRootContent());
  float resolution = mLastContentPaintMetrics.GetPresShellResolution();
  return Matrix4x4::Scaling(resolution, resolution, 1.f);
}

LayoutDeviceToParentLayerScale AsyncPanZoomController::GetCurrentPinchZoomScale(
    AsyncTransformConsumer aMode) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  AutoApplyAsyncTestAttributes testAttributeApplier(this, lock);
  CSSToParentLayerScale scale = GetEffectiveZoom(aMode, lock);
  return scale / Metrics().GetDevPixelsPerCSSPixel();
}

AutoTArray<wr::SampledScrollOffset, 2>
AsyncPanZoomController::GetSampledScrollOffsets() const {
  AssertOnSamplerThread();

  RecursiveMutexAutoLock lock(mRecursiveMutex);

  const AsyncTransformComponents asyncTransformComponents =
      GetZoomAnimationId()
          ? AsyncTransformComponents{AsyncTransformComponent::eLayout}
          : LayoutAndVisual;

  // If layerTranslation includes only the layout component of the async
  // transform then it has not been scaled by the async zoom, so we want to
  // divide it by the resolution. If layerTranslation includes the visual
  // component, then we should use the pinch zoom scale, which includes the
  // async zoom. However, we only use LayoutAndVisual for non-zoomable APZCs,
  // so it makes no difference.
  LayoutDeviceToParentLayerScale resolution =
      GetCumulativeResolution() * LayerToParentLayerScale(1.0f);

  AutoTArray<wr::SampledScrollOffset, 2> sampledOffsets;

  for (std::deque<SampledAPZCState>::size_type index = 0;
       index < mSampledState.size(); index++) {
    ParentLayerPoint layerTranslation =
        GetCurrentAsyncTransform(AsyncPanZoomController::eForCompositing,
                                 asyncTransformComponents, index)
            .mTranslation;

    // Include the overscroll transform here in scroll offsets transform
    // to ensure that we do not overscroll fixed content.
    layerTranslation =
        GetOverscrollTransform(AsyncPanZoomController::eForCompositing)
            .TransformPoint(layerTranslation);
    // The positive translation means the painted content is supposed to
    // move down (or to the right), and that corresponds to a reduction in
    // the scroll offset. Since we are effectively giving WR the async
    // scroll delta here, we want to negate the translation.
    LayoutDevicePoint asyncScrollDelta = -layerTranslation / resolution;
    sampledOffsets.AppendElement(wr::SampledScrollOffset{
        wr::ToLayoutVector2D(asyncScrollDelta),
        wr::ToWrAPZScrollGeneration(mSampledState[index].Generation())});
  }

  return sampledOffsets;
}

bool AsyncPanZoomController::SuppressAsyncScrollOffset() const {
  return mScrollMetadata.IsApzForceDisabled() ||
         (Metrics().IsMinimalDisplayPort() &&
          StaticPrefs::apz_prefer_jank_minimal_displayports());
}

CSSRect AsyncPanZoomController::GetEffectiveLayoutViewport(
    AsyncTransformConsumer aMode, const RecursiveMutexAutoLock& aProofOfLock,
    std::size_t aSampleIndex) const {
  if (aMode == eForCompositing && SuppressAsyncScrollOffset()) {
    return mLastContentPaintMetrics.GetLayoutViewport();
  }
  if (aMode == eForCompositing) {
    return mSampledState[aSampleIndex].GetLayoutViewport();
  }
  return Metrics().GetLayoutViewport();
}

CSSPoint AsyncPanZoomController::GetEffectiveScrollOffset(
    AsyncTransformConsumer aMode, const RecursiveMutexAutoLock& aProofOfLock,
    std::size_t aSampleIndex) const {
  if (aMode == eForCompositing && SuppressAsyncScrollOffset()) {
    return mLastContentPaintMetrics.GetVisualScrollOffset();
  }
  if (aMode == eForCompositing) {
    return mSampledState[aSampleIndex].GetVisualScrollOffset();
  }
  return Metrics().GetVisualScrollOffset();
}

CSSToParentLayerScale AsyncPanZoomController::GetEffectiveZoom(
    AsyncTransformConsumer aMode, const RecursiveMutexAutoLock& aProofOfLock,
    std::size_t aSampleIndex) const {
  if (aMode == eForCompositing && SuppressAsyncScrollOffset()) {
    return mLastContentPaintMetrics.GetZoom();
  }
  if (aMode == eForCompositing) {
    return mSampledState[aSampleIndex].GetZoom();
  }
  return Metrics().GetZoom();
}

void AsyncPanZoomController::AdvanceToNextSample() {
  AssertOnSamplerThread();
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  // Always keep at least one state in mSampledState.
  if (mSampledState.size() > 1) {
    mSampledState.pop_front();
  }
}

bool AsyncPanZoomController::HavePendingFrameDelayedOffset() const {
  AssertOnSamplerThread();
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  const bool nextFrameWillChange =
      mSampledState.size() >= 2 && mSampledState[0] != mSampledState[1];
  const bool frameAfterThatWillChange =
      mSampledState.back() != SampledAPZCState(Metrics());
  return nextFrameWillChange || frameAfterThatWillChange;
}

bool AsyncPanZoomController::SampleCompositedAsyncTransform(
    const RecursiveMutexAutoLock& aProofOfLock) {
  MOZ_ASSERT(mSampledState.size() <= 2);
  bool sampleChanged = (mSampledState.back() != SampledAPZCState(Metrics()));
  mSampledState.emplace_back(
      Metrics(), std::move(mScrollPayload), mScrollGeneration,
      // Will consume mUpdatesSinceLastSample and leave it empty
      std::move(mUpdatesSinceLastSample));
  return sampleChanged;
}

void AsyncPanZoomController::ResampleCompositedAsyncTransform(
    const RecursiveMutexAutoLock& aProofOfLock) {
  // This only gets called during testing situations, so the fact that this
  // drops the scroll payload from mSampledState.front() is not really a
  // problem.
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    mScrollGeneration = treeManagerLocal->NewAPZScrollGeneration();
  }
  mSampledState.front() = SampledAPZCState(
      Metrics(), {}, mScrollGeneration,
      // Will consume mUpdatesSinceLastSample and leave it empty
      std::move(mUpdatesSinceLastSample));
}

void AsyncPanZoomController::ApplyAsyncTestAttributes(
    const RecursiveMutexAutoLock& aProofOfLock) {
  if (mTestAttributeAppliers == 0) {
    if (mTestAsyncScrollOffset != CSSPoint() ||
        mTestAsyncZoom != LayerToParentLayerScale()) {
      // TODO Currently we update Metrics() and resample, which will cause
      // the very latest user input to get immediately captured in the sample,
      // and may defeat our attempt at "frame delay" (i.e. delaying the user
      // input from affecting composition by one frame).
      // Instead, maybe we should just apply the mTest* stuff directly to
      // mSampledState.front(). We can even save/restore that SampledAPZCState
      // instance in the AutoApplyAsyncTestAttributes instead of Metrics().
      Metrics().ZoomBy(mTestAsyncZoom.scale);
      CSSPoint asyncScrollPosition = Metrics().GetVisualScrollOffset();
      CSSPoint requestedPoint =
          asyncScrollPosition + this->mTestAsyncScrollOffset;
      CSSPoint clampedPoint =
          Metrics().CalculateScrollRange().ClampPoint(requestedPoint);
      CSSPoint difference = mTestAsyncScrollOffset - clampedPoint;

      ScrollByAndClamp(mTestAsyncScrollOffset);

      if (StaticPrefs::apz_overscroll_test_async_scroll_offset_enabled()) {
        ParentLayerPoint overscroll = difference * Metrics().GetZoom();
        OverscrollBy(overscroll);
      }
      ResampleCompositedAsyncTransform(aProofOfLock);
    }
  }
  ++mTestAttributeAppliers;
}

void AsyncPanZoomController::UnapplyAsyncTestAttributes(
    const RecursiveMutexAutoLock& aProofOfLock,
    const FrameMetrics& aPrevFrameMetrics,
    const ParentLayerPoint& aPrevOverscroll) {
  MOZ_ASSERT(mTestAttributeAppliers >= 1);
  --mTestAttributeAppliers;
  if (mTestAttributeAppliers == 0) {
    if (mTestAsyncScrollOffset != CSSPoint() ||
        mTestAsyncZoom != LayerToParentLayerScale()) {
      Metrics() = aPrevFrameMetrics;
      RestoreOverscrollAmount(aPrevOverscroll);
      ResampleCompositedAsyncTransform(aProofOfLock);
    }
  }
}

Matrix4x4 AsyncPanZoomController::GetTransformToLastDispatchedPaint(
    const AsyncTransformComponents& aComponents, LayersId aForLayersId) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  CSSPoint componentOffset;

  // The computation of the componentOffset should roughly be the negation
  // of the translation in GetCurrentAsyncTransform() with the expected
  // gecko metrics substituted for the effective scroll offsets.
  if (aComponents.contains(AsyncTransformComponent::eVisual)) {
    componentOffset += mExpectedGeckoMetrics.GetLayoutScrollOffset() -
                       mExpectedGeckoMetrics.GetVisualScrollOffset();
  }

  if (aComponents.contains(AsyncTransformComponent::eLayout)) {
    CSSPoint lastPaintLayoutOffset;

    if (mLastContentPaintMetrics.IsScrollable()) {
      lastPaintLayoutOffset = mLastContentPaintMetrics.GetLayoutScrollOffset();
    }

    componentOffset +=
        lastPaintLayoutOffset - mExpectedGeckoMetrics.GetLayoutScrollOffset();
  }

  LayerPoint scrollChange = componentOffset *
                            mLastContentPaintMetrics.GetDevPixelsPerCSSPixel() *
                            mLastContentPaintMetrics.GetCumulativeResolution();

  // We're interested in the async zoom change. Factor out the content scale
  // that may change when dragging the window to a monitor with a different
  // content scale.
  LayoutDeviceToParentLayerScale lastContentZoom =
      mLastContentPaintMetrics.GetZoom() /
      mLastContentPaintMetrics.GetDevPixelsPerCSSPixel();
  LayoutDeviceToParentLayerScale lastDispatchedZoom =
      mExpectedGeckoMetrics.GetZoom() /
      mExpectedGeckoMetrics.GetDevPixelsPerCSSPixel();
  float zoomChange = 1.0;
  if (aComponents.contains(AsyncTransformComponent::eVisual) &&
      lastDispatchedZoom != LayoutDeviceToParentLayerScale(0)) {
    zoomChange = lastContentZoom.scale / lastDispatchedZoom.scale;
  }
  Matrix4x4 result;
  // If we are the root, and |aForLayersId| is different from our LayersId,
  // |aForLayersId| must be in a remote subdocument.
  if (IsRootContent() && aForLayersId != GetLayersId()) {
    result = GetPaintedResolutionTransform();
  }
  // Order of transforms: the painted resolution (if any) applies first, and
  // any async transform on top of that.
  return result * Matrix4x4::Translation(scrollChange.x, scrollChange.y, 0)
                      .PostScale(zoomChange, zoomChange, 1);
}

CSSRect AsyncPanZoomController::GetVisibleRect(
    const RecursiveMutexAutoLock& aProofOfLock) const {
  AutoApplyAsyncTestAttributes testAttributeApplier(this, aProofOfLock);
  CSSPoint currentScrollOffset = GetEffectiveScrollOffset(
      AsyncPanZoomController::eForCompositing, aProofOfLock);
  CSSRect visible = CSSRect(currentScrollOffset,
                            Metrics().CalculateCompositedSizeInCssPixels());
  return visible;
}

static CSSRect GetPaintedRect(const FrameMetrics& aFrameMetrics) {
  CSSRect displayPort = aFrameMetrics.GetDisplayPort();
  if (displayPort.IsEmpty()) {
    // Fallback to use the viewport if the diplayport hasn't been set.
    // This situation often happens non-scrollable iframe's root scroller in
    // Fission.
    return aFrameMetrics.GetVisualViewport();
  }

  return displayPort + aFrameMetrics.GetLayoutScrollOffset();
}

uint32_t AsyncPanZoomController::GetCheckerboardMagnitude(
    const ParentLayerRect& aClippedCompositionBounds) const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  CSSRect painted = GetPaintedRect(mLastContentPaintMetrics);
  painted.Inflate(CSSMargin::FromAppUnits(
      nsMargin(1, 1, 1, 1)));  // fuzz for rounding error

  CSSRect visible = GetVisibleRect(lock);  // relative to scrolled frame origin
  if (visible.IsEmpty() || painted.Contains(visible)) {
    // early-exit if we're definitely not checkerboarding
    return 0;
  }

  // aClippedCompositionBounds and Metrics().GetCompositionBounds() are both
  // relative to the layer tree origin.
  // The "*RelativeToItself*" variables are relative to the comp bounds origin
  ParentLayerRect visiblePartOfCompBoundsRelativeToItself =
      aClippedCompositionBounds - Metrics().GetCompositionBounds().TopLeft();
  CSSRect visiblePartOfCompBoundsRelativeToItselfInCssSpace;
  if (Metrics().GetZoom() != CSSToParentLayerScale(0)) {
    visiblePartOfCompBoundsRelativeToItselfInCssSpace =
        (visiblePartOfCompBoundsRelativeToItself / Metrics().GetZoom());
  }

  // This one is relative to the scrolled frame origin, same as `visible`
  CSSRect visiblePartOfCompBoundsInCssSpace =
      visiblePartOfCompBoundsRelativeToItselfInCssSpace + visible.TopLeft();

  visible = visible.Intersect(visiblePartOfCompBoundsInCssSpace);

  CSSIntRegion checkerboard;
  // Round so as to minimize checkerboarding; if we're only showing fractional
  // pixels of checkerboarding it's not really worth counting
  checkerboard.Sub(RoundedIn(visible), RoundedOut(painted));
  uint32_t area = checkerboard.Area();
  if (area) {
    APZC_LOG_FM(Metrics(),
                "%p is currently checkerboarding (painted %s visible %s)", this,
                ToString(painted).c_str(), ToString(visible).c_str());
  }
  return area;
}

void AsyncPanZoomController::ReportCheckerboard(
    const SampleTime& aSampleTime,
    const ParentLayerRect& aClippedCompositionBounds) {
  if (mLastCheckerboardReport == aSampleTime) {
    // This function will get called multiple times for each APZC on a single
    // composite (once for each layer it is attached to). Only report the
    // checkerboard once per composite though.
    return;
  }
  mLastCheckerboardReport = aSampleTime;

  bool recordTrace = StaticPrefs::apz_record_checkerboarding();
  bool forTelemetry = Telemetry::CanRecordBase();
  uint32_t magnitude = GetCheckerboardMagnitude(aClippedCompositionBounds);

  // IsInTransformingState() acquires the APZC lock and thus needs to
  // be called before acquiring mCheckerboardEventLock.
  bool inTransformingState = IsInTransformingState();

  MutexAutoLock lock(mCheckerboardEventLock);
  if (!mCheckerboardEvent && (recordTrace || forTelemetry)) {
    mCheckerboardEvent = MakeUnique<CheckerboardEvent>(recordTrace);
  }
  mPotentialCheckerboardTracker.InTransform(inTransformingState,
                                            recordTrace || forTelemetry);
  if (magnitude) {
    mPotentialCheckerboardTracker.CheckerboardSeen();
  }
  UpdateCheckerboardEvent(lock, magnitude);
}

void AsyncPanZoomController::UpdateCheckerboardEvent(
    const MutexAutoLock& aProofOfLock, uint32_t aMagnitude) {
  if (mCheckerboardEvent && mCheckerboardEvent->RecordFrameInfo(aMagnitude)) {
    // This checkerboard event is done. Report some metrics to telemetry.
    mozilla::glean::gfx_checkerboard::severity.AccumulateSingleSample(
        mCheckerboardEvent->GetSeverity());
    mozilla::glean::gfx_checkerboard::peak_pixel_count.AccumulateSingleSample(
        mCheckerboardEvent->GetPeak());
    mozilla::glean::gfx_checkerboard::duration.AccumulateRawDuration(
        mCheckerboardEvent->GetDuration());

    // mCheckerboardEvent only gets created if we are supposed to record
    // telemetry so we always pass true for aRecordTelemetry.
    mPotentialCheckerboardTracker.CheckerboardDone(
        /* aRecordTelemetry = */ true);

    if (StaticPrefs::apz_record_checkerboarding()) {
      // if the pref is enabled, also send it to the storage class. it may be
      // chosen for public display on about:checkerboard, the hall of fame for
      // checkerboard events.
      uint32_t severity = mCheckerboardEvent->GetSeverity();
      std::string log = mCheckerboardEvent->GetLog();
      CheckerboardEventStorage::Report(severity, log);
    }
    mCheckerboardEvent = nullptr;
  }
}

void AsyncPanZoomController::FlushActiveCheckerboardReport() {
  MutexAutoLock lock(mCheckerboardEventLock);
  // Pretend like we got a frame with 0 pixels checkerboarded. This will
  // terminate the checkerboard event and flush it out
  UpdateCheckerboardEvent(lock, 0);
}

void AsyncPanZoomController::NotifyLayersUpdated(
    const ScrollMetadata& aScrollMetadata, bool aIsFirstPaint,
    bool aThisLayerTreeUpdated) {
  AssertOnUpdaterThread();

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  bool isDefault = mScrollMetadata.IsDefault();

  const FrameMetrics& aLayerMetrics = aScrollMetadata.GetMetrics();

  if ((aScrollMetadata == mLastContentPaintMetadata) && !isDefault) {
    // No new information here, skip it.
    APZC_LOGV("%p NotifyLayersUpdated short-circuit\n", this);
    return;
  }

  // FIXME: CompositorScrollUpdate::Source::Other is not accurate for every
  //        change made by NotifyLayersUpdated. We may need to track different
  //        sources for different ScrollPositionUpdates.
  AutoRecordCompositorScrollUpdate updater(
      this, CompositorScrollUpdate::Source::Other, lock);

  // If the Metrics scroll offset is different from the last scroll offset
  // that the main-thread sent us, then we know that the user has been doing
  // something that triggers a scroll. This check is the APZ equivalent of the
  // check on the main-thread at
  // https://hg.mozilla.org/mozilla-central/file/97a52326b06a/layout/generic/nsGfxScrollFrame.cpp#l4050
  // There is code below (the use site of userScrolled) that prevents a
  // restored- scroll-position update from overwriting a user scroll, again
  // equivalent to how the main thread code does the same thing.
  // XXX Suspicious comparison between layout and visual scroll offsets.
  // This may not do the right thing when we're zoomed in.
  CSSPoint lastScrollOffset = mLastContentPaintMetrics.GetLayoutScrollOffset();
  bool userScrolled =
      !FuzzyEqualsCoordinate(Metrics().GetVisualScrollOffset().x,
                             lastScrollOffset.x) ||
      !FuzzyEqualsCoordinate(Metrics().GetVisualScrollOffset().y,
                             lastScrollOffset.y);

  if (aScrollMetadata.DidContentGetPainted()) {
    mLastContentPaintMetadata = aScrollMetadata;
  }

  mScrollMetadata.SetScrollParentId(aScrollMetadata.GetScrollParentId());
  APZC_LOGV_FM(aLayerMetrics,
               "%p got a NotifyLayersUpdated with aIsFirstPaint=%d, "
               "aThisLayerTreeUpdated=%d",
               this, aIsFirstPaint, aThisLayerTreeUpdated);

  {  // scope lock
    MutexAutoLock lock(mCheckerboardEventLock);
    if (mCheckerboardEvent && mCheckerboardEvent->IsRecordingTrace()) {
      std::string str;
      if (aThisLayerTreeUpdated) {
        if (!aLayerMetrics.GetPaintRequestTime().IsNull()) {
          // Note that we might get the paint request time as non-null, but with
          // aThisLayerTreeUpdated false. That can happen if we get a layer
          // transaction from a different process right after we get the layer
          // transaction with aThisLayerTreeUpdated == true. In this case we
          // want to ignore the paint request time because it was already dumped
          // in the previous layer transaction.
          TimeDuration paintTime =
              TimeStamp::Now() - aLayerMetrics.GetPaintRequestTime();
          std::stringstream info;
          info << " painttime " << paintTime.ToMilliseconds();
          str = info.str();
        } else {
          // This might be indicative of a wasted paint particularly if it
          // happens during a checkerboard event.
          str = " (this layertree updated)";
        }
      }
      mCheckerboardEvent->UpdateRendertraceProperty(
          CheckerboardEvent::Page, aLayerMetrics.GetScrollableRect());
      mCheckerboardEvent->UpdateRendertraceProperty(
          CheckerboardEvent::PaintedDisplayPort, GetPaintedRect(aLayerMetrics),
          str);
    }
  }

  // The main thread may send us a visual scroll offset update. This is
  // different from a layout viewport offset update in that the layout viewport
  // offset is limited to the layout scroll range, while the visual viewport
  // offset is not.
  // However, there are some conditions in which the layout update will clobber
  // the visual update, and we want to ignore the visual update in those cases.
  // This variable tracks that.
  bool ignoreVisualUpdate = false;

  // TODO if we're in a drag and scrollOffsetUpdated is set then we want to
  // ignore it

  bool needContentRepaint = false;
  RepaintUpdateType contentRepaintType = RepaintUpdateType::eNone;
  bool viewportSizeUpdated = false;
  bool needToReclampScroll = false;

  if ((aIsFirstPaint && aThisLayerTreeUpdated) || isDefault ||
      Metrics().IsRootContent() != aLayerMetrics.IsRootContent()) {
    if (Metrics().IsRootContent() && !aLayerMetrics.IsRootContent()) {
      // We only support zooming on root content APZCs
      SetZoomAnimationId(Nothing());
    }

    // Initialize our internal state to something sane when the content
    // that was just painted is something we knew nothing about previously
    CancelAnimation();

    // Keep our existing scroll generation and existing scroll offsets, if there
    // are scroll updates. In this case we'll update our scroll generation and
    // offsets when processing the scroll update array below. If there are no
    // scroll updates, take the generation from the incoming metrics. Bug
    // 1662019 will simplify this later.
    ScrollGeneration oldScrollGeneration = Metrics().GetScrollGeneration();
    CSSPoint oldLayoutScrollOffset = Metrics().GetLayoutScrollOffset();
    CSSPoint oldVisualScrollOffset = Metrics().GetVisualScrollOffset();
    mScrollMetadata = aScrollMetadata;
    if (!aScrollMetadata.GetScrollUpdates().IsEmpty()) {
      Metrics().SetScrollGeneration(oldScrollGeneration);
      Metrics().SetLayoutScrollOffset(oldLayoutScrollOffset);
      Metrics().SetVisualScrollOffset(oldVisualScrollOffset);
    }

    mExpectedGeckoMetrics.UpdateFrom(aLayerMetrics);

    for (auto& sampledState : mSampledState) {
      sampledState.UpdateScrollProperties(Metrics());
      sampledState.UpdateZoomProperties(Metrics());
    }

    if (aLayerMetrics.HasNonZeroDisplayPortMargins()) {
      // A non-zero display port margin here indicates a displayport has
      // been set by a previous APZC for the content at this guid. The
      // scrollable rect may have changed since then, making the margins
      // wrong, so we need to calculate a new display port.
      // It is important that we request a repaint here only when we need to
      // otherwise we will end up setting a display port on every frame that
      // gets a view id.
      APZC_LOG("%p detected non-empty margins which probably need updating\n",
               this);
      needContentRepaint = true;
    }
  } else {
    // If we're not taking the aLayerMetrics wholesale we still need to pull
    // in some things into our local Metrics() because these things are
    // determined by Gecko and our copy in Metrics() may be stale.

    if (Metrics().GetLayoutViewport().Size() !=
        aLayerMetrics.GetLayoutViewport().Size()) {
      CSSRect layoutViewport = Metrics().GetLayoutViewport();
      // The offset will be updated if necessary via
      // RecalculateLayoutViewportOffset().
      layoutViewport.SizeTo(aLayerMetrics.GetLayoutViewport().Size());
      Metrics().SetLayoutViewport(layoutViewport);

      needContentRepaint = true;
      viewportSizeUpdated = true;
    }

    // TODO: Rely entirely on |aScrollMetadata.IsResolutionUpdated()| to
    //       determine which branch to take, and drop the other conditions.
    CSSToParentLayerScale oldZoom = Metrics().GetZoom();
    if (FuzzyEqualsAdditive(
            Metrics().GetCompositionBoundsWidthIgnoringScrollbars(),
            aLayerMetrics.GetCompositionBoundsWidthIgnoringScrollbars()) &&
        Metrics().GetDevPixelsPerCSSPixel() ==
            aLayerMetrics.GetDevPixelsPerCSSPixel() &&
        !viewportSizeUpdated && !aScrollMetadata.IsResolutionUpdated()) {
      // Any change to the pres shell resolution was requested by APZ and is
      // already included in our zoom; however, other components of the
      // cumulative resolution (a parent document's pres-shell resolution, or
      // the css-driven resolution) may have changed, and we need to update
      // our zoom to reflect that. Note that we can't just take
      // aLayerMetrics.mZoom because the APZ may have additional async zoom
      // since the repaint request.
      float totalResolutionChange = 1.0;

      if (Metrics().GetCumulativeResolution() != LayoutDeviceToLayerScale(0)) {
        totalResolutionChange = aLayerMetrics.GetCumulativeResolution().scale /
                                Metrics().GetCumulativeResolution().scale;
      }

      float presShellResolutionChange = aLayerMetrics.GetPresShellResolution() /
                                        Metrics().GetPresShellResolution();
      if (presShellResolutionChange != 1.0f) {
        needContentRepaint = true;
      }
      Metrics().ZoomBy(totalResolutionChange / presShellResolutionChange);
      for (auto& sampledState : mSampledState) {
        sampledState.ZoomBy(totalResolutionChange / presShellResolutionChange);
      }
    } else {
      // Take the new zoom as either device scale or composition width or
      // viewport size got changed (e.g. due to orientation change, or content
      // changing the meta-viewport tag), or the main thread originated a
      // resolution change for another reason (e.g. Ctrl+0 was pressed to
      // reset the zoom).
      Metrics().SetZoom(aLayerMetrics.GetZoom());
      for (auto& sampledState : mSampledState) {
        sampledState.UpdateZoomProperties(aLayerMetrics);
      }
      Metrics().SetDevPixelsPerCSSPixel(
          aLayerMetrics.GetDevPixelsPerCSSPixel());
    }

    if (Metrics().GetZoom() != oldZoom) {
      // If the zoom changed, the scroll range in CSS pixels may have changed
      // even if the composition bounds didn't.
      needToReclampScroll = true;
    }

    mExpectedGeckoMetrics.UpdateZoomFrom(aLayerMetrics);

    if (!Metrics().GetScrollableRect().IsEqualEdges(
            aLayerMetrics.GetScrollableRect())) {
      Metrics().SetScrollableRect(aLayerMetrics.GetScrollableRect());
      needContentRepaint = true;
      needToReclampScroll = true;
    }
    if (!Metrics().GetCompositionBounds().IsEqualEdges(
            aLayerMetrics.GetCompositionBounds())) {
      Metrics().SetCompositionBounds(aLayerMetrics.GetCompositionBounds());
      needToReclampScroll = true;
    }
    Metrics().SetCompositionBoundsWidthIgnoringScrollbars(
        aLayerMetrics.GetCompositionBoundsWidthIgnoringScrollbars());

    if (Metrics().IsRootContent() &&
        Metrics().GetCompositionSizeWithoutDynamicToolbar() !=
            aLayerMetrics.GetCompositionSizeWithoutDynamicToolbar()) {
      Metrics().SetCompositionSizeWithoutDynamicToolbar(
          aLayerMetrics.GetCompositionSizeWithoutDynamicToolbar());
      needToReclampScroll = true;
    }
    Metrics().SetBoundingCompositionSize(
        aLayerMetrics.GetBoundingCompositionSize());
    Metrics().SetPresShellResolution(aLayerMetrics.GetPresShellResolution());
    Metrics().SetCumulativeResolution(aLayerMetrics.GetCumulativeResolution());
    Metrics().SetTransformToAncestorScale(
        aLayerMetrics.GetTransformToAncestorScale());
    mScrollMetadata.SetLineScrollAmount(aScrollMetadata.GetLineScrollAmount());
    mScrollMetadata.SetPageScrollAmount(aScrollMetadata.GetPageScrollAmount());
    mScrollMetadata.SetSnapInfo(ScrollSnapInfo(aScrollMetadata.GetSnapInfo()));
    mScrollMetadata.SetIsLayersIdRoot(aScrollMetadata.IsLayersIdRoot());
    mScrollMetadata.SetIsAutoDirRootContentRTL(
        aScrollMetadata.IsAutoDirRootContentRTL());
    Metrics().SetIsScrollInfoLayer(aLayerMetrics.IsScrollInfoLayer());
    Metrics().SetHasNonZeroDisplayPortMargins(
        aLayerMetrics.HasNonZeroDisplayPortMargins());
    Metrics().SetMinimalDisplayPort(aLayerMetrics.IsMinimalDisplayPort());
    mScrollMetadata.SetForceDisableApz(aScrollMetadata.IsApzForceDisabled());
    mScrollMetadata.SetIsRDMTouchSimulationActive(
        aScrollMetadata.GetIsRDMTouchSimulationActive());
    mScrollMetadata.SetForceMousewheelAutodir(
        aScrollMetadata.ForceMousewheelAutodir());
    mScrollMetadata.SetForceMousewheelAutodirHonourRoot(
        aScrollMetadata.ForceMousewheelAutodirHonourRoot());
    mScrollMetadata.SetIsPaginatedPresentation(
        aScrollMetadata.IsPaginatedPresentation());
    mScrollMetadata.SetDisregardedDirection(
        aScrollMetadata.GetDisregardedDirection());
    mScrollMetadata.SetOverscrollBehavior(
        aScrollMetadata.GetOverscrollBehavior());
    mScrollMetadata.SetOverflow(aScrollMetadata.GetOverflow());
  }

  bool instantScrollMayTriggerTransform = false;
  bool scrollOffsetUpdated = false;
  bool smoothScrollRequested = false;
  bool didCancelAnimation = false;
  Maybe<CSSPoint> cumulativeRelativeDelta;
  for (const auto& scrollUpdate : aScrollMetadata.GetScrollUpdates()) {
    APZC_LOG("%p processing scroll update %s\n", this,
             ToString(scrollUpdate).c_str());
    if (!(Metrics().GetScrollGeneration() < scrollUpdate.GetGeneration())) {
      // This is stale, let's ignore it
      APZC_LOG("%p scrollupdate generation stale, dropping\n", this);
      continue;
    }
    Metrics().SetScrollGeneration(scrollUpdate.GetGeneration());

    MOZ_ASSERT(scrollUpdate.GetOrigin() != ScrollOrigin::Apz);
    if (userScrolled &&
        !nsLayoutUtils::CanScrollOriginClobberApz(scrollUpdate.GetOrigin())) {
      APZC_LOG("%p scrollupdate cannot clobber APZ userScrolled\n", this);
      continue;
    }
    // XXX: if we get here, |scrollUpdate| is clobbering APZ, so we may want
    // to reset |userScrolled| back to false so that subsequent scrollUpdates
    // in this loop don't get dropped by the check above. Need to add a test
    // that exercises this scenario, as we don't currently have one.

    if (scrollUpdate.GetMode() == ScrollMode::Smooth ||
        scrollUpdate.GetMode() == ScrollMode::SmoothMsd) {
      smoothScrollRequested = true;

      // Requests to animate the visual scroll position override requests to
      // simply update the visual scroll offset to a particular point. Since
      // we have an animation request, we set ignoreVisualUpdate to true to
      // indicate we don't need to apply the visual scroll update in
      // aLayerMetrics.
      ignoreVisualUpdate = true;

      // For relative updates we want to add the relative offset to any existing
      // destination, or the current visual offset if there is no existing
      // destination.
      CSSPoint base = GetCurrentAnimationDestination(lock).valueOr(
          Metrics().GetVisualScrollOffset());

      CSSPoint destination;
      if (scrollUpdate.GetType() == ScrollUpdateType::Relative) {
        CSSPoint delta =
            scrollUpdate.GetDestination() - scrollUpdate.GetSource();
        APZC_LOG("%p relative smooth scrolling from %s by %s\n", this,
                 ToString(base).c_str(), ToString(delta).c_str());
        destination = Metrics().CalculateScrollRange().ClampPoint(base + delta);
      } else if (scrollUpdate.GetType() == ScrollUpdateType::PureRelative) {
        CSSPoint delta = scrollUpdate.GetDelta();
        APZC_LOG("%p pure-relative smooth scrolling from %s by %s\n", this,
                 ToString(base).c_str(), ToString(delta).c_str());
        destination = Metrics().CalculateScrollRange().ClampPoint(base + delta);
      } else {
        APZC_LOG("%p smooth scrolling to %s\n", this,
                 ToString(scrollUpdate.GetDestination()).c_str());
        destination = scrollUpdate.GetDestination();
      }

      if (scrollUpdate.GetMode() == ScrollMode::SmoothMsd) {
        SmoothMsdScrollTo(
            CSSSnapDestination{destination, scrollUpdate.GetSnapTargetIds()},
            scrollUpdate.GetScrollTriggeredByScript());
      } else {
        MOZ_ASSERT(scrollUpdate.GetMode() == ScrollMode::Smooth);
        SmoothScrollTo(
            CSSSnapDestination{destination, scrollUpdate.GetSnapTargetIds()},
            scrollUpdate.GetScrollTriggeredByScript(),
            scrollUpdate.GetOrigin());
      }
      continue;
    }

    MOZ_ASSERT(scrollUpdate.GetMode() == ScrollMode::Instant ||
               scrollUpdate.GetMode() == ScrollMode::Normal);

    instantScrollMayTriggerTransform =
        scrollUpdate.GetMode() == ScrollMode::Instant &&
        scrollUpdate.GetScrollTriggeredByScript() ==
            ScrollTriggeredByScript::No;

    // If the layout update is of a higher priority than the visual update, then
    // we don't want to apply the visual update.
    // If the layout update is of a clobbering type (or a smooth scroll request,
    // which is handled above) then it takes precedence over an eRestore visual
    // update. But we also allow the possibility for the main thread to ask us
    // to scroll both the layout and visual viewports to distinct (but
    // compatible) locations (via e.g. both updates being of a non-clobbering/
    // eRestore type).
    if (nsLayoutUtils::CanScrollOriginClobberApz(scrollUpdate.GetOrigin()) &&
        aLayerMetrics.GetVisualScrollUpdateType() !=
            FrameMetrics::eMainThread) {
      ignoreVisualUpdate = true;
    }

    Maybe<CSSPoint> relativeDelta;
    if (scrollUpdate.GetType() == ScrollUpdateType::Relative) {
      APZC_LOG(
          "%p relative updating scroll offset from %s by %s\n", this,
          ToString(Metrics().GetVisualScrollOffset()).c_str(),
          ToString(scrollUpdate.GetDestination() - scrollUpdate.GetSource())
              .c_str());

      scrollOffsetUpdated = true;

      // It's possible that the main thread has ignored an APZ scroll offset
      // update for the pending relative scroll that we have just received.
      // When this happens, we need to send a new scroll offset update with
      // the combined scroll offset or else the main thread may have an
      // incorrect scroll offset for a period of time.
      if (Metrics().HasPendingScroll(aLayerMetrics)) {
        needContentRepaint = true;
        contentRepaintType = RepaintUpdateType::eUserAction;
      }

      relativeDelta =
          Some(Metrics().ApplyRelativeScrollUpdateFrom(scrollUpdate));
      Metrics().RecalculateLayoutViewportOffset();
    } else if (scrollUpdate.GetType() == ScrollUpdateType::PureRelative) {
      APZC_LOG("%p pure-relative updating scroll offset from %s by %s\n", this,
               ToString(Metrics().GetVisualScrollOffset()).c_str(),
               ToString(scrollUpdate.GetDelta()).c_str());

      scrollOffsetUpdated = true;

      // Always need a repaint request with a repaint type for pure relative
      // scrolls because apz is doing the scroll at the main thread's request.
      // The main thread has not updated it's scroll offset yet, it is depending
      // on apz to tell it where to scroll.
      needContentRepaint = true;
      contentRepaintType = RepaintUpdateType::eVisualUpdate;

      // We have to ignore a visual scroll offset update otherwise it will
      // clobber the relative scrolling we are about to do. We perform
      // visualScrollOffset = visualScrollOffset + delta. Then the
      // visualScrollOffsetUpdated block below will do visualScrollOffset =
      // aLayerMetrics.GetVisualDestination(). We need visual scroll offset
      // updates to be incorporated into this scroll update loop to properly fix
      // this.
      ignoreVisualUpdate = true;

      relativeDelta =
          Some(Metrics().ApplyPureRelativeScrollUpdateFrom(scrollUpdate));
      Metrics().RecalculateLayoutViewportOffset();
    } else {
      APZC_LOG("%p updating scroll offset from %s to %s\n", this,
               ToString(Metrics().GetVisualScrollOffset()).c_str(),
               ToString(scrollUpdate.GetDestination()).c_str());
      bool offsetChanged = Metrics().ApplyScrollUpdateFrom(scrollUpdate);
      Metrics().RecalculateLayoutViewportOffset();

      if (offsetChanged || scrollUpdate.GetMode() != ScrollMode::Instant ||
          scrollUpdate.GetType() != ScrollUpdateType::Absolute ||
          scrollUpdate.GetOrigin() != ScrollOrigin::None) {
        // We get a NewScrollFrame update for newly created scroll frames. Only
        // if this was not a NewScrollFrame update or the offset changed do we
        // request repaint. This is important so that we don't request repaint
        // for every new content and set a full display port on it.
        scrollOffsetUpdated = true;
      }
    }

    if (relativeDelta) {
      cumulativeRelativeDelta =
          !cumulativeRelativeDelta
              ? relativeDelta
              : Some(*cumulativeRelativeDelta + *relativeDelta);
    } else {
      // If the scroll update is not relative, clobber the cumulative delta,
      // i.e. later updates win.
      cumulativeRelativeDelta.reset();
    }

    // If an animation is underway, tell it about the scroll offset update.
    // Some animations can handle some scroll offset updates and continue
    // running. Those that can't will return false, and we cancel them.
    if (ShouldCancelAnimationForScrollUpdate(relativeDelta)) {
      // Cancel the animation (which might also trigger a repaint request)
      // after we update the scroll offset above. Otherwise we can be left
      // in a state where things are out of sync.
      CancelAnimation();
      didCancelAnimation = true;
    }
  }

  if (aIsFirstPaint || needToReclampScroll) {
    // The scrollable rect or composition bounds may have changed in a way that
    // makes our local scroll offset out of bounds, so clamp it.
    ClampAndSetVisualScrollOffset(Metrics().GetVisualScrollOffset());
  }

  // If our scroll range changed (for example, because the page dynamically
  // loaded new content, thereby increasing the size of the scrollable rect),
  // and we're overscrolled, being overscrolled may no longer be a valid
  // state (for example, we may no longer be at the edge of our scroll range),
  // then try to fill it out with the new content if the overscroll amount is
  // inside the new scroll range.
  if (needToReclampScroll && IsInInvalidOverscroll()) {
    if (!cumulativeRelativeDelta) {
      // TODO: If we have a cumulative delta, can we combine the overscroll
      //  change with it?
      CSSPoint scrollPositionChange = MaybeFillOutOverscrollGutter(lock);
      if (scrollPositionChange != CSSPoint()) {
        cumulativeRelativeDelta = Some(scrollPositionChange);
      }
    }
    if (mState == OVERSCROLL_ANIMATION) {
      CancelAnimation();
      didCancelAnimation = true;
    } else if (IsOverscrolled()) {
      ClearOverscroll();
    }
  }

  if (scrollOffsetUpdated) {
    // Because of the scroll generation update, any inflight paint requests
    // are going to be ignored by layout, and so mExpectedGeckoMetrics becomes
    // incorrect for the purposes of calculating the LD transform. To correct
    // this we need to update mExpectedGeckoMetrics to be the last thing we
    // know was painted by Gecko.
    mExpectedGeckoMetrics.UpdateFrom(aLayerMetrics);

    // Since the scroll offset has changed, we need to recompute the
    // displayport margins and send them to layout. Otherwise there might be
    // scenarios where for example we scroll from the top of a page (where the
    // top displayport margin is zero) to the bottom of a page, which will
    // result in a displayport that doesn't extend upwards at all.
    // Note that even if the CancelAnimation call above requested a repaint
    // this is fine because we already have repaint request deduplication.
    needContentRepaint = true;
    // Since the main-thread scroll offset changed we should trigger a
    // recomposite to make sure it becomes user-visible.
    ScheduleComposite();

    // If the scroll offset was updated, we're not in a transforming state,
    // and we are scrolling by a non-zero delta, we should ensure
    // TransformBegin and TransformEnd notifications are sent.
    if (!IsTransformingState(mState) && instantScrollMayTriggerTransform &&
        cumulativeRelativeDelta && *cumulativeRelativeDelta != CSSPoint() &&
        !didCancelAnimation) {
      SendTransformBeginAndEnd();
    }
  }

  if (smoothScrollRequested && !scrollOffsetUpdated) {
    mExpectedGeckoMetrics.UpdateFrom(aLayerMetrics);
    // Need to acknowledge the request.
    needContentRepaint = true;
  }

  // If `isDefault` is true, this APZC is a "new" one (this is the first time
  // it's getting a NotifyLayersUpdated call). In this case we want to apply the
  // visual scroll offset from the main thread to our scroll offset.
  // The main thread may also ask us to scroll the visual viewport to a
  // particular location. However, in all cases, we want to ignore the visual
  // offset update if ignoreVisualUpdate is true, because we're clobbering
  // the visual update with a layout update.
  bool visualScrollOffsetUpdated =
      !ignoreVisualUpdate &&
      (isDefault ||
       aLayerMetrics.GetVisualScrollUpdateType() != FrameMetrics::eNone);

  if (visualScrollOffsetUpdated) {
    APZC_LOG("%p updating visual scroll offset from %s to %s (updateType %d)\n",
             this, ToString(Metrics().GetVisualScrollOffset()).c_str(),
             ToString(aLayerMetrics.GetVisualDestination()).c_str(),
             (int)aLayerMetrics.GetVisualScrollUpdateType());
    bool offsetChanged = Metrics().ClampAndSetVisualScrollOffset(
        aLayerMetrics.GetVisualDestination());

    // If this is the first time we got metrics for this content (isDefault) and
    // the update type was none and the offset didn't change then we don't have
    // to do anything. This is important because we don't want to request
    // repaint on the initial NotifyLayersUpdated for every content and thus set
    // a full display port.
    if (aLayerMetrics.GetVisualScrollUpdateType() == FrameMetrics::eNone &&
        !offsetChanged) {
      visualScrollOffsetUpdated = false;
    }
  }
  if (visualScrollOffsetUpdated) {
    // The rest of this branch largely follows the code in the
    // |if (scrollOffsetUpdated)| branch above. Eventually it should get
    // merged into that branch.
    Metrics().RecalculateLayoutViewportOffset();
    mExpectedGeckoMetrics.UpdateFrom(aLayerMetrics);
    if (ShouldCancelAnimationForScrollUpdate(Nothing())) {
      CancelAnimation();
    }
    // The main thread did not actually paint a displayport at the target
    // visual offset, so we need to ask it to repaint. We need to set the
    // contentRepaintType to something other than eNone, otherwise the main
    // thread will short-circuit the repaint request.
    // Don't do this for eRestore visual updates as a repaint coming from APZ
    // breaks the scroll offset restoration mechanism.
    needContentRepaint = true;
    if (aLayerMetrics.GetVisualScrollUpdateType() ==
        FrameMetrics::eMainThread) {
      contentRepaintType = RepaintUpdateType::eVisualUpdate;
    }
    ScheduleComposite();
  }

  if (viewportSizeUpdated) {
    // While we want to accept the main thread's layout viewport _size_,
    // its position may be out of date in light of async scrolling, to
    // adjust it if necessary to make sure it continues to enclose the
    // visual viewport.
    // Note: it's important to do this _after_ we've accepted any
    // updated composition bounds.
    Metrics().RecalculateLayoutViewportOffset();
  }

  // Modify sampled state lastly.
  if (scrollOffsetUpdated || visualScrollOffsetUpdated) {
    for (auto& sampledState : mSampledState) {
      if (!didCancelAnimation && cumulativeRelativeDelta.isSome()) {
        sampledState.UpdateScrollPropertiesWithRelativeDelta(
            Metrics(), *cumulativeRelativeDelta);
      } else {
        sampledState.UpdateScrollProperties(Metrics());
      }
    }
  }
  if (aIsFirstPaint || needToReclampScroll) {
    for (auto& sampledState : mSampledState) {
      sampledState.ClampVisualScrollOffset(Metrics());
    }
  }

  if (needContentRepaint) {
    // This repaint request could be driven by a user action if we accept a
    // relative scroll offset update
    RequestContentRepaint(contentRepaintType);
  }
}

FrameMetrics& AsyncPanZoomController::Metrics() {
  mRecursiveMutex.AssertCurrentThreadIn();
  return mScrollMetadata.GetMetrics();
}

const FrameMetrics& AsyncPanZoomController::Metrics() const {
  mRecursiveMutex.AssertCurrentThreadIn();
  return mScrollMetadata.GetMetrics();
}

bool CompositorScrollUpdate::Metrics::operator==(const Metrics& aOther) const {
  // Consider two metrcs to be the same if the scroll offsets are the same
  // when rounded to the nearest screen pixel. This avoids spurious updates
  // due to small rounding errors, which consumers do not care about because
  // if the scroll offset does not change in screen pixels, what is composited
  // should not change either.
  return RoundedToInt(mVisualScrollOffset * mZoom) ==
             RoundedToInt(aOther.mVisualScrollOffset * aOther.mZoom) &&
         mZoom == aOther.mZoom;
}

bool CompositorScrollUpdate::operator==(
    const CompositorScrollUpdate& aOther) const {
  return mMetrics == aOther.mMetrics && mSource == aOther.mSource;
}

std::vector<CompositorScrollUpdate>
AsyncPanZoomController::GetCompositorScrollUpdates() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  MOZ_ASSERT(Metrics().IsRootContent());
  return mSampledState[0].Updates();
}

CompositorScrollUpdate::Metrics
AsyncPanZoomController::GetCurrentMetricsForCompositorScrollUpdate(
    const RecursiveMutexAutoLock& aProofOfApzcLock) const {
  return {Metrics().GetVisualScrollOffset(), Metrics().GetZoom()};
}

wr::MinimapData AsyncPanZoomController::GetMinimapData() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  wr::MinimapData result;
  result.is_root_content = IsRootContent();
  // We want the minimap to reflect the scroll offset actually composited,
  // which could be older than the latest one in Metrics() due to the frame
  // delay.
  CSSRect visualViewport = GetCurrentAsyncVisualViewport(eForCompositing);
  result.visual_viewport = wr::ToLayoutRect(visualViewport.ToUnknownRect());
  CSSRect layoutViewport = GetEffectiveLayoutViewport(eForCompositing, lock);
  result.layout_viewport = wr::ToLayoutRect(layoutViewport.ToUnknownRect());
  result.scrollable_rect =
      wr::ToLayoutRect(Metrics().GetScrollableRect().ToUnknownRect());
  // The display port is stored relative to the layout viewport origin.
  // Translate it to be relative to the document origin, like the other rects.
  CSSRect displayPort = mLastContentPaintMetrics.GetDisplayPort() +
                        mLastContentPaintMetrics.GetLayoutScrollOffset();
  result.displayport = wr::ToLayoutRect(displayPort.ToUnknownRect());
  // Remaining fields (zoom_transform, root_content_scroll_id,
  // root_content_pipeline_id) will be populated by the caller, since they
  // require information from other APZCs to compute.
  return result;
}

const FrameMetrics& AsyncPanZoomController::GetFrameMetrics() const {
  return Metrics();
}

const ScrollMetadata& AsyncPanZoomController::GetScrollMetadata() const {
  mRecursiveMutex.AssertCurrentThreadIn();
  return mScrollMetadata;
}

void AsyncPanZoomController::AssertOnSamplerThread() const {
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    treeManagerLocal->AssertOnSamplerThread();
  }
}

void AsyncPanZoomController::AssertOnUpdaterThread() const {
  if (APZCTreeManager* treeManagerLocal = GetApzcTreeManager()) {
    treeManagerLocal->AssertOnUpdaterThread();
  }
}

APZCTreeManager* AsyncPanZoomController::GetApzcTreeManager() const {
  mRecursiveMutex.AssertNotCurrentThreadIn();
  return mTreeManager;
}

void AsyncPanZoomController::ZoomToRect(const ZoomTarget& aZoomTarget,
                                        const uint32_t aFlags) {
  CSSRect rect = aZoomTarget.targetRect;
  if (!rect.IsFinite()) {
    NS_WARNING("ZoomToRect got called with a non-finite rect; ignoring...");
    return;
  }

  if (rect.IsEmpty() && (aFlags & DISABLE_ZOOM_OUT)) {
    // Double-tap-to-zooming uses an empty rect to mean "zoom out".
    // If zooming out is disabled, an empty rect is nonsensical
    // and will produce undesirable scrolling.
    NS_WARNING(
        "ZoomToRect got called with an empty rect and zoom out disabled; "
        "ignoring...");
    return;
  }

  AutoDynamicToolbarHider dynamicToolbarHider(this);

  {
    RecursiveMutexAutoLock lock(mRecursiveMutex);

    // If we are zooming to focus an input element near the bottom of the
    // scrollable rect, it may be covered up by the dynamic toolbar and we may
    // not have room to scroll it into view. In such cases, trigger hiding of
    // the dynamic toolbar to ensure the input element is visible.
    if (aFlags & ZOOM_TO_FOCUSED_INPUT) {
      // Long and short viewport heights, corresponding to CSS length values of
      // 100lvh and 100svh.
      const CSSCoord lvh = ToCSSPixels(Metrics().GetCompositionBounds().height);
      const CSSCoord svh = ToCSSPixels(
          Metrics().GetCompositionSizeWithoutDynamicToolbar().height);
      const CSSCoord scrollableRectHeight =
          Metrics().GetScrollableRect().height;

      auto mightNeedToHideToolbar = [&]() -> bool {
        // While the software keyboard is visible on resizes-visual mode,
        // if the target rect is underneath of the toolbar, we will have to
        // hide the toolbar.
        if (aFlags & ZOOM_TO_FOCUSED_INPUT_ON_RESIZES_VISUAL) {
          return true;
        }
        // FIXME: This condition is too strict even in resizes-content mode,
        // it's possible for the toolbar to cover up an element at the bottom
        // of the scrollable rect even if `scrollableRectHeight > lvh`.
        // We need to either relax the condition, or find a different solution
        // such as bug 1920019 comment 8.
        return scrollableRectHeight > svh && scrollableRectHeight < lvh;
      };

      if (mightNeedToHideToolbar()) {
        const CSSCoord targetDistanceFromBottom =
            (Metrics().GetScrollableRect().YMost() -
             aZoomTarget.targetRect.YMost());
        const CSSCoord dynamicToolbarHeight = (lvh - svh);
        if (targetDistanceFromBottom < dynamicToolbarHeight) {
          dynamicToolbarHider.Hide();
        }
      }
    }

    MOZ_ASSERT(Metrics().IsRootContent());

    const float defaultZoomInAmount =
        StaticPrefs::apz_doubletapzoom_defaultzoomin();

    ParentLayerRect compositionBounds = Metrics().GetCompositionBounds();
    CSSRect cssPageRect = Metrics().GetScrollableRect();
    CSSPoint scrollOffset = Metrics().GetVisualScrollOffset();
    CSSSize sizeBeforeZoom = Metrics().CalculateCompositedSizeInCssPixels();
    CSSToParentLayerScale currentZoom = Metrics().GetZoom();
    CSSToParentLayerScale targetZoom;

    // The minimum zoom to prevent over-zoom-out.
    // If the zoom factor is lower than this (i.e. we are zoomed more into the
    // page), then the CSS content rect, in layers pixels, will be smaller than
    // the composition bounds. If this happens, we can't fill the target
    // composited area with this frame.
    const CSSRect cssExpandedPageRect = Metrics().GetExpandedScrollableRect();
    CSSToParentLayerScale localMinZoom(
        std::max(compositionBounds.Width() / cssExpandedPageRect.Width(),
                 compositionBounds.Height() / cssExpandedPageRect.Height()));

    localMinZoom.scale =
        std::clamp(localMinZoom.scale, mZoomConstraints.mMinZoom.scale,
                   mZoomConstraints.mMaxZoom.scale);

    localMinZoom = std::max(mZoomConstraints.mMinZoom, localMinZoom);
    CSSToParentLayerScale localMaxZoom =
        std::max(localMinZoom, mZoomConstraints.mMaxZoom);

    if (!rect.IsEmpty()) {
      // Intersect the zoom-to-rect to the CSS rect to make sure it fits.
      rect = rect.Intersect(cssPageRect);
      targetZoom = CSSToParentLayerScale(
          std::min(compositionBounds.Width() / rect.Width(),
                   compositionBounds.Height() / rect.Height()));
      if (aFlags & DISABLE_ZOOM_OUT) {
        targetZoom = std::max(targetZoom, currentZoom);
      }
    }

    // 1. If the rect is empty, the content-side logic for handling a double-tap
    //    requested that we zoom out.
    // 2. currentZoom is equal to mZoomConstraints.mMaxZoom and user still
    // double-tapping it
    // Treat these cases as a request to zoom out as much as possible
    // unless cantZoomOutBehavior == ZoomIn and currentZoom
    // is equal to localMinZoom and user still double-tapping it, then try to
    // zoom in a small amount to provide feedback to the user.
    bool zoomOut = false;
    // True if we are already zoomed out and we are asked to either stay there
    // or zoom out more and cantZoomOutBehavior == ZoomIn.
    bool zoomInDefaultAmount = false;
    if (aFlags & DISABLE_ZOOM_OUT) {
      zoomOut = false;
    } else {
      if (rect.IsEmpty()) {
        if (currentZoom == localMinZoom &&
            aZoomTarget.cantZoomOutBehavior == CantZoomOutBehavior::ZoomIn &&
            (defaultZoomInAmount != 1.f)) {
          zoomInDefaultAmount = true;
        } else {
          zoomOut = true;
        }
      } else if (currentZoom == localMaxZoom && targetZoom >= localMaxZoom) {
        zoomOut = true;
      }
    }

    // already at min zoom and asked to zoom out further
    if (!zoomOut && currentZoom == localMinZoom && targetZoom <= localMinZoom &&
        aZoomTarget.cantZoomOutBehavior == CantZoomOutBehavior::ZoomIn &&
        (defaultZoomInAmount != 1.f)) {
      zoomInDefaultAmount = true;
    }
    MOZ_ASSERT(!(zoomInDefaultAmount && zoomOut));

    if (zoomInDefaultAmount) {
      targetZoom =
          CSSToParentLayerScale(currentZoom.scale * defaultZoomInAmount);
    }

    if (zoomOut) {
      targetZoom = localMinZoom;
    }

    if (aFlags & PAN_INTO_VIEW_ONLY) {
      targetZoom = currentZoom;
    } else if (aFlags & ONLY_ZOOM_TO_DEFAULT_SCALE) {
      CSSToParentLayerScale zoomAtDefaultScale =
          Metrics().GetDevPixelsPerCSSPixel() *
          LayoutDeviceToParentLayerScale(1.0);
      if (targetZoom.scale > zoomAtDefaultScale.scale) {
        // Only change the zoom if we are less than the default zoom
        if (currentZoom.scale < zoomAtDefaultScale.scale) {
          targetZoom = zoomAtDefaultScale;
        } else {
          targetZoom = currentZoom;
        }
      }
    }

    targetZoom.scale =
        std::clamp(targetZoom.scale, localMinZoom.scale, localMaxZoom.scale);

    // For zoom-to-focused-input, we've already centered the given focused
    // element in nsDOMWindowUtils::ZoomToFocusedInput() so that if the target
    // zoom scale would be same we don't need to trigger a ZoomAnimation.
    if ((aFlags & ZOOM_TO_FOCUSED_INPUT) && targetZoom == currentZoom) {
      return;
    }

    FrameMetrics endZoomToMetrics = Metrics();
    endZoomToMetrics.SetZoom(CSSToParentLayerScale(targetZoom));
    CSSSize sizeAfterZoom =
        endZoomToMetrics.CalculateCompositedSizeInCssPixels();

    if (zoomInDefaultAmount || zoomOut) {
      // For the zoom out case we should always center what was visible
      // otherwise it feels like we are scrolling as well as zooming out. For
      // the non-zoomOut case, if we've been provided a pointer location, zoom
      // around that, otherwise just zoom in to the center of what's currently
      // visible.
      if (!zoomOut && aZoomTarget.documentRelativePointerPosition.isSome()) {
        rect = CSSRect(aZoomTarget.documentRelativePointerPosition->x -
                           sizeAfterZoom.width / 2,
                       aZoomTarget.documentRelativePointerPosition->y -
                           sizeAfterZoom.height / 2,
                       sizeAfterZoom.Width(), sizeAfterZoom.Height());
      } else {
        rect = CSSRect(
            scrollOffset.x + (sizeBeforeZoom.width - sizeAfterZoom.width) / 2,
            scrollOffset.y + (sizeBeforeZoom.height - sizeAfterZoom.height) / 2,
            sizeAfterZoom.Width(), sizeAfterZoom.Height());
      }

      rect = rect.Intersect(cssPageRect);
    }

    // Check if we can fit the full elementBoundingRect.
    if (!aZoomTarget.targetRect.IsEmpty() && !zoomOut &&
        aZoomTarget.elementBoundingRect.isSome()) {
      MOZ_ASSERT(aZoomTarget.elementBoundingRect->Contains(rect));
      CSSRect elementBoundingRect =
          aZoomTarget.elementBoundingRect->Intersect(cssPageRect);
      if (elementBoundingRect.width <= sizeAfterZoom.width &&
          elementBoundingRect.height <= sizeAfterZoom.height) {
        rect = elementBoundingRect;
      }
    }

    // Vertically center the zoomed element in the screen.
    if (!zoomOut &&
        (sizeAfterZoom.height - rect.Height() > COORDINATE_EPSILON)) {
      rect.MoveByY(-(sizeAfterZoom.height - rect.Height()) * 0.5f);
      if (rect.Y() < 0.0f) {
        rect.MoveToY(0.0f);
      }
    }

    // Horizontally center the zoomed element in the screen.
    if (!zoomOut && (sizeAfterZoom.width - rect.Width() > COORDINATE_EPSILON)) {
      rect.MoveByX(-(sizeAfterZoom.width - rect.Width()) * 0.5f);
      if (rect.X() < 0.0f) {
        rect.MoveToX(0.0f);
      }
    }

    bool intersectRectAgain = false;
    // If we can't zoom out enough to show the full rect then shift the rect we
    // are able to show to center what was visible.
    // Note that this calculation works no matter the relation of sizeBeforeZoom
    // to sizeAfterZoom, ie whether we are increasing or decreasing zoom.
    if (!zoomOut &&
        (rect.Height() - sizeAfterZoom.height > COORDINATE_EPSILON)) {
      rect.y =
          scrollOffset.y + (sizeBeforeZoom.height - sizeAfterZoom.height) / 2;
      rect.height = sizeAfterZoom.Height();

      intersectRectAgain = true;
    }

    if (!zoomOut && (rect.Width() - sizeAfterZoom.width > COORDINATE_EPSILON)) {
      rect.x =
          scrollOffset.x + (sizeBeforeZoom.width - sizeAfterZoom.width) / 2;
      rect.width = sizeAfterZoom.Width();

      intersectRectAgain = true;
    }
    if (intersectRectAgain) {
      rect = rect.Intersect(cssPageRect);
    }

    // If any of these conditions are met, the page will be overscrolled after
    // zoomed. Attempting to scroll outside of the valid scroll range will cause
    // problems.
    if (rect.Y() + sizeAfterZoom.height > cssPageRect.YMost()) {
      rect.MoveToY(std::max(cssPageRect.Y(),
                            cssPageRect.YMost() - sizeAfterZoom.height));
    }
    if (rect.Y() < cssPageRect.Y()) {
      rect.MoveToY(cssPageRect.Y());
    }
    if (rect.X() + sizeAfterZoom.width > cssPageRect.XMost()) {
      rect.MoveToX(
          std::max(cssPageRect.X(), cssPageRect.XMost() - sizeAfterZoom.width));
    }
    if (rect.X() < cssPageRect.X()) {
      rect.MoveToX(cssPageRect.X());
    }

    endZoomToMetrics.SetVisualScrollOffset(rect.TopLeft());
    endZoomToMetrics.RecalculateLayoutViewportOffset();

    SetState(ANIMATING_ZOOM);
    StartAnimation(do_AddRef(new ZoomAnimation(
        *this, Metrics().GetVisualScrollOffset(), Metrics().GetZoom(),
        endZoomToMetrics.GetVisualScrollOffset(), endZoomToMetrics.GetZoom())));

    RequestContentRepaint();
  }
}

InputBlockState* AsyncPanZoomController::GetCurrentInputBlock() const {
  return GetInputQueue()->GetCurrentBlock();
}

TouchBlockState* AsyncPanZoomController::GetCurrentTouchBlock() const {
  return GetInputQueue()->GetCurrentTouchBlock();
}

PanGestureBlockState* AsyncPanZoomController::GetCurrentPanGestureBlock()
    const {
  return GetInputQueue()->GetCurrentPanGestureBlock();
}

PinchGestureBlockState* AsyncPanZoomController::GetCurrentPinchGestureBlock()
    const {
  return GetInputQueue()->GetCurrentPinchGestureBlock();
}

void AsyncPanZoomController::ResetTouchInputState() {
  TouchBlockState* block = GetCurrentTouchBlock();
  if (block && block->HasStateBeenReset()) {
    // Bail out only if there's a touch block that the state of the touch block
    // has been reset.
    return;
  }

  MultiTouchInput cancel(MultiTouchInput::MULTITOUCH_CANCEL, 0,
                         TimeStamp::Now(), 0);
  RefPtr<GestureEventListener> listener = GetGestureEventListener();
  if (listener) {
    listener->HandleInputEvent(cancel);
  }
  CancelAnimationAndGestureState();
  // Clear overscroll along the entire handoff chain, in case an APZC
  // later in the chain is overscrolled.
  if (block) {
    block->GetOverscrollHandoffChain()->ClearOverscroll();
    block->ResetState();
  }
}

void AsyncPanZoomController::ResetPanGestureInputState() {
  PanGestureBlockState* block = GetCurrentPanGestureBlock();
  if (block && block->HasStateBeenReset()) {
    // Bail out only if there's a pan gesture block that the state of the pan
    // gesture block has been reset.
    return;
  }

  // Unlike in ResetTouchInputState(), do not cancel animations unconditionally.
  // Doing so would break scenarios where content handled `wheel` events
  // triggered by pan gesture input by calling preventDefault() and doing its
  // own smooth (animated) scrolling. However, we do need to call
  // CancelAnimation for its state-resetting effect if there isn't an animation
  // running, otherwise we could e.g. get stuck in a PANNING state if content
  // preventDefault()s an event in the middle of a pan gesture.
  if (!mAnimation) {
    CancelAnimationAndGestureState();
  }

  // Clear overscroll along the entire handoff chain, in case an APZC
  // later in the chain is overscrolled.
  if (block) {
    block->GetOverscrollHandoffChain()->ClearOverscroll();
    block->ResetState();
  }
}

void AsyncPanZoomController::CancelAnimationAndGestureState() {
  mX.CancelGesture();
  mY.CancelGesture();
  CancelAnimation(CancelAnimationFlags::ScrollSnap);
}

bool AsyncPanZoomController::HasReadyTouchBlock() const {
  return GetInputQueue()->HasReadyTouchBlock();
}

bool AsyncPanZoomController::CanHandleScrollOffsetUpdate(PanZoomState aState) {
  return aState == NOTHING || aState == PAN_MOMENTUM || aState == TOUCHING ||
         IsPanningState(aState);
}

bool AsyncPanZoomController::ShouldCancelAnimationForScrollUpdate(
    const Maybe<CSSPoint>& aRelativeDelta) {
  // Never call CancelAnimation() for a no-op relative update.
  if (aRelativeDelta == Some(CSSPoint())) {
    return false;
  }

  if (mAnimation) {
    return !mAnimation->HandleScrollOffsetUpdate(aRelativeDelta);
  }

  return !CanHandleScrollOffsetUpdate(mState);
}

AsyncPanZoomController::PanZoomState
AsyncPanZoomController::SetStateNoContentControllerDispatch(
    PanZoomState aNewState) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  APZC_LOG_DETAIL("changing from state %s to %s\n", this,
                  ToString(mState).c_str(), ToString(aNewState).c_str());
  PanZoomState oldState = mState;
  mState = aNewState;
  return oldState;
}

void AsyncPanZoomController::SetState(PanZoomState aNewState) {
  // When a state transition to a transforming state is occuring and a delayed
  // transform end notification exists, send the TransformEnd notification
  // before the TransformBegin notification is sent for the input state change.
  if (IsTransformingState(aNewState) && IsDelayedTransformEndSet()) {
    MOZ_ASSERT(!IsTransformingState(mState));
    SetDelayedTransformEnd(false);
    DispatchStateChangeNotification(PANNING, NOTHING);
  }

  PanZoomState oldState = SetStateNoContentControllerDispatch(aNewState);

  DispatchStateChangeNotification(oldState, aNewState);
}

auto AsyncPanZoomController::GetState() const -> PanZoomState {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mState;
}

void AsyncPanZoomController::DispatchStateChangeNotification(
    PanZoomState aOldState, PanZoomState aNewState) {
  {  // scope the lock
    RecursiveMutexAutoLock lock(mRecursiveMutex);
    if (mNotificationBlockers > 0) {
      return;
    }
  }

  if (RefPtr<GeckoContentController> controller = GetGeckoContentController()) {
    if (!IsTransformingState(aOldState) && IsTransformingState(aNewState)) {
      controller->NotifyAPZStateChange(GetGuid(),
                                       APZStateChange::eTransformBegin);
    } else if (IsTransformingState(aOldState) &&
               !IsTransformingState(aNewState)) {
      controller->NotifyAPZStateChange(GetGuid(),
                                       APZStateChange::eTransformEnd);
    }
  }
}
void AsyncPanZoomController::SendTransformBeginAndEnd() {
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (controller) {
    controller->NotifyAPZStateChange(GetGuid(),
                                     APZStateChange::eTransformBegin);
    controller->NotifyAPZStateChange(GetGuid(), APZStateChange::eTransformEnd);
  }
}

bool AsyncPanZoomController::IsInTransformingState() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return IsTransformingState(mState);
}

bool AsyncPanZoomController::IsTransformingState(PanZoomState aState) {
  return !(aState == NOTHING || aState == TOUCHING);
}

bool AsyncPanZoomController::IsPanningState(PanZoomState aState) {
  return (aState == PANNING || aState == PANNING_LOCKED_X ||
          aState == PANNING_LOCKED_Y);
}

bool AsyncPanZoomController::IsInPanningState() const {
  return IsPanningState(mState);
}

bool AsyncPanZoomController::IsInScrollingGesture() const {
  return IsPanningState(mState) || mState == SCROLLBAR_DRAG ||
         mState == TOUCHING || mState == PINCHING;
}

bool AsyncPanZoomController::IsDelayedTransformEndSet() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mDelayedTransformEnd;
}

void AsyncPanZoomController::SetDelayedTransformEnd(bool aDelayedTransformEnd) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mDelayedTransformEnd = aDelayedTransformEnd;
}

void AsyncPanZoomController::UpdateZoomConstraints(
    const ZoomConstraints& aConstraints) {
  if ((MOZ_LOG_TEST(sApzCtlLog, LogLevel::Debug) &&
       (aConstraints != mZoomConstraints)) ||
      MOZ_LOG_TEST(sApzCtlLog, LogLevel::Verbose)) {
    APZC_LOG("%p updating zoom constraints to %d %d %f %f\n", this,
             aConstraints.mAllowZoom, aConstraints.mAllowDoubleTapZoom,
             aConstraints.mMinZoom.scale, aConstraints.mMaxZoom.scale);
  }

  if (std::isnan(aConstraints.mMinZoom.scale) ||
      std::isnan(aConstraints.mMaxZoom.scale)) {
    NS_WARNING("APZC received zoom constraints with NaN values; dropping...");
    return;
  }

  RecursiveMutexAutoLock lock(mRecursiveMutex);
  CSSToParentLayerScale min = Metrics().GetDevPixelsPerCSSPixel() *
                              ViewportMinScale() / ParentLayerToScreenScale(1);
  CSSToParentLayerScale max = Metrics().GetDevPixelsPerCSSPixel() *
                              ViewportMaxScale() / ParentLayerToScreenScale(1);

  // inf float values and other bad cases should be sanitized by the code below.
  mZoomConstraints.mAllowZoom = aConstraints.mAllowZoom;
  mZoomConstraints.mAllowDoubleTapZoom = aConstraints.mAllowDoubleTapZoom;
  mZoomConstraints.mMinZoom =
      (min > aConstraints.mMinZoom ? min : aConstraints.mMinZoom);
  mZoomConstraints.mMaxZoom =
      (max > aConstraints.mMaxZoom ? aConstraints.mMaxZoom : max);
  if (mZoomConstraints.mMaxZoom < mZoomConstraints.mMinZoom) {
    mZoomConstraints.mMaxZoom = mZoomConstraints.mMinZoom;
  }
}

bool AsyncPanZoomController::ZoomConstraintsAllowZoom() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mZoomConstraints.mAllowZoom;
}

bool AsyncPanZoomController::ZoomConstraintsAllowDoubleTapZoom() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mZoomConstraints.mAllowDoubleTapZoom;
}

void AsyncPanZoomController::PostDelayedTask(already_AddRefed<Runnable> aTask,
                                             int aDelayMs) {
  APZThreadUtils::AssertOnControllerThread();
  RefPtr<Runnable> task = aTask;
  RefPtr<GeckoContentController> controller = GetGeckoContentController();
  if (controller) {
    controller->PostDelayedTask(task.forget(), aDelayMs);
  }
  // If there is no controller, that means this APZC has been destroyed, and
  // we probably don't need to run the task. It will get destroyed when the
  // RefPtr goes out of scope.
}

bool AsyncPanZoomController::Matches(const ScrollableLayerGuid& aGuid) {
  return aGuid == GetGuid();
}

bool AsyncPanZoomController::HasTreeManager(
    const APZCTreeManager* aTreeManager) const {
  return GetApzcTreeManager() == aTreeManager;
}

void AsyncPanZoomController::GetGuid(ScrollableLayerGuid* aGuidOut) const {
  if (aGuidOut) {
    *aGuidOut = GetGuid();
  }
}

ScrollableLayerGuid AsyncPanZoomController::GetGuid() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return ScrollableLayerGuid(mLayersId, Metrics().GetPresShellId(),
                             Metrics().GetScrollId());
}

void AsyncPanZoomController::SetTestAsyncScrollOffset(const CSSPoint& aPoint) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mTestAsyncScrollOffset = aPoint;
  ScheduleComposite();
}

void AsyncPanZoomController::SetTestAsyncZoom(
    const LayerToParentLayerScale& aZoom) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mTestAsyncZoom = aZoom;
  ScheduleComposite();
}

Maybe<CSSSnapDestination> AsyncPanZoomController::FindSnapPointNear(
    const CSSPoint& aDestination, ScrollUnit aUnit,
    ScrollSnapFlags aSnapFlags) {
  mRecursiveMutex.AssertCurrentThreadIn();
  APZC_LOG("%p scroll snapping near %s\n", this,
           ToString(aDestination).c_str());
  CSSRect scrollRange = Metrics().CalculateScrollRange();
  if (auto snapDestination = ScrollSnapUtils::GetSnapPointForDestination(
          mScrollMetadata.GetSnapInfo(), aUnit, aSnapFlags,
          CSSRect::ToAppUnits(scrollRange),
          CSSPoint::ToAppUnits(Metrics().GetVisualScrollOffset()),
          CSSPoint::ToAppUnits(aDestination))) {
    CSSPoint cssSnapPoint = CSSPoint::FromAppUnits(snapDestination->mPosition);
    // GetSnapPointForDestination() can produce a destination that's outside
    // of the scroll frame's scroll range. Clamp it here (this matches the
    // behaviour of the main-thread code path, which clamps it in
    // ScrollContainerFrame::ScrollTo()).
    return Some(CSSSnapDestination{scrollRange.ClampPoint(cssSnapPoint),
                                   snapDestination->mTargetIds});
  }
  return Nothing();
}

Maybe<std::pair<MultiTouchInput, MultiTouchInput>>
AsyncPanZoomController::MaybeSplitTouchMoveEvent(
    const MultiTouchInput& aOriginalEvent, ScreenCoord aPanThreshold,
    float aVectorLength, ExternalPoint& aExtPoint) {
  if (aVectorLength <= aPanThreshold) {
    return Nothing();
  }

  auto splitEvent = std::make_pair(aOriginalEvent, aOriginalEvent);

  SingleTouchData& firstTouchData = splitEvent.first.mTouches[0];
  SingleTouchData& secondTouchData = splitEvent.second.mTouches[0];

  firstTouchData.mHistoricalData.Clear();
  secondTouchData.mHistoricalData.Clear();

  ExternalPoint destination = aExtPoint;
  ExternalPoint thresholdPosition;

  const float ratio = aPanThreshold / aVectorLength;
  thresholdPosition.x = mStartTouch.x + ratio * (destination.x - mStartTouch.x);
  thresholdPosition.y = mStartTouch.y + ratio * (destination.y - mStartTouch.y);

  TouchSample start{mLastTouch};
  // To compute the timestamp of the first event (which is at the threshold),
  // use linear interpolation with the starting point |start| being the last
  // event that's before the threshold, and the end point |end| being the first
  // event after the threshold.

  // The initial choice for |start| is the last touch event before
  // |aOriginalEvent|, and the initial choice for |end| is |aOriginalEvent|.

  // However, the historical data points stored in |aOriginalEvent| may contain
  // intermediate positions that can serve as tighter bounds for the
  // interpolation.
  TouchSample end{destination, aOriginalEvent.mTimeStamp};

  for (const auto& historicalData :
       aOriginalEvent.mTouches[0].mHistoricalData) {
    ExternalPoint histExtPoint = ToExternalPoint(aOriginalEvent.mScreenOffset,
                                                 historicalData.mScreenPoint);

    if (PanVector(histExtPoint).Length() <
        PanVector(thresholdPosition).Length()) {
      start = {histExtPoint, historicalData.mTimeStamp};
    } else {
      break;
    }
  }

  for (const SingleTouchData::HistoricalTouchData& histData :
       Reversed(aOriginalEvent.mTouches[0].mHistoricalData)) {
    ExternalPoint histExtPoint =
        ToExternalPoint(aOriginalEvent.mScreenOffset, histData.mScreenPoint);

    if (PanVector(histExtPoint).Length() >
        PanVector(thresholdPosition).Length()) {
      end = {histExtPoint, histData.mTimeStamp};
    } else {
      break;
    }
  }

  const float totalLength =
      ScreenPoint(fabs(end.mPosition.x - start.mPosition.x),
                  fabs(end.mPosition.y - start.mPosition.y))
          .Length();
  const float thresholdLength =
      ScreenPoint(fabs(thresholdPosition.x - start.mPosition.x),
                  fabs(thresholdPosition.y - start.mPosition.y))
          .Length();
  const float splitRatio = thresholdLength / totalLength;

  splitEvent.first.mTimeStamp =
      start.mTimeStamp +
      (end.mTimeStamp - start.mTimeStamp).MultDouble(splitRatio);

  for (const auto& historicalData :
       aOriginalEvent.mTouches[0].mHistoricalData) {
    if (historicalData.mTimeStamp > splitEvent.first.mTimeStamp) {
      secondTouchData.mHistoricalData.AppendElement(historicalData);
    } else {
      firstTouchData.mHistoricalData.AppendElement(historicalData);
    }
  }

  firstTouchData.mScreenPoint = RoundedToInt(
      ViewAs<ScreenPixel>(thresholdPosition - splitEvent.first.mScreenOffset,
                          PixelCastJustification::ExternalIsScreen));

  // Recompute firstTouchData.mLocalScreenPoint.
  splitEvent.first.TransformToLocal(
      GetCurrentTouchBlock()->GetTransformToApzc());

  // Pass |thresholdPosition| back out to the caller via |aExtPoint|
  aExtPoint = thresholdPosition;

  return Some(splitEvent);
}

void AsyncPanZoomController::ScrollSnapNear(const CSSPoint& aDestination,
                                            ScrollSnapFlags aSnapFlags) {
  if (Maybe<CSSSnapDestination> snapDestination = FindSnapPointNear(
          aDestination, ScrollUnit::DEVICE_PIXELS, aSnapFlags)) {
    if (snapDestination->mPosition != Metrics().GetVisualScrollOffset()) {
      APZC_LOG("%p smooth scrolling to snap point %s\n", this,
               ToString(snapDestination->mPosition).c_str());
      SmoothMsdScrollTo(std::move(*snapDestination),
                        ScrollTriggeredByScript::No);
    }
  }
}

void AsyncPanZoomController::ScrollSnap(ScrollSnapFlags aSnapFlags) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  ScrollSnapNear(Metrics().GetVisualScrollOffset(), aSnapFlags);
}

void AsyncPanZoomController::ScrollSnapToDestination() {
  RecursiveMutexAutoLock lock(mRecursiveMutex);

  float friction = StaticPrefs::apz_fling_friction();
  ParentLayerPoint velocity(mX.GetVelocity(), mY.GetVelocity());
  ParentLayerPoint predictedDelta;
  // "-velocity / log(1.0 - friction)" is the integral of the deceleration
  // curve modeled for flings in the "Axis" class.
  if (velocity.x != 0.0f && friction != 0.0f) {
    predictedDelta.x = -velocity.x / log(1.0 - friction);
  }
  if (velocity.y != 0.0f && friction != 0.0f) {
    predictedDelta.y = -velocity.y / log(1.0 - friction);
  }

  // If the fling will overscroll, don't scroll snap, because then the user
  // user would not see any overscroll animation.
  bool flingWillOverscroll =
      IsOverscrolled() && ((velocity.x.value * mX.GetOverscroll() >= 0) ||
                           (velocity.y.value * mY.GetOverscroll() >= 0));
  if (flingWillOverscroll) {
    return;
  }

  CSSPoint startPosition = Metrics().GetVisualScrollOffset();
  ScrollSnapFlags snapFlags = ScrollSnapFlags::IntendedEndPosition;
  if (predictedDelta != ParentLayerPoint()) {
    snapFlags |= ScrollSnapFlags::IntendedDirection;
  }
  if (Maybe<CSSSnapDestination> snapDestination =
          MaybeAdjustDeltaForScrollSnapping(ScrollUnit::DEVICE_PIXELS,
                                            snapFlags, predictedDelta,
                                            startPosition)) {
    APZC_LOG(
        "%p fling snapping.  friction: %f velocity: %f, %f "
        "predictedDelta: %f, %f position: %f, %f "
        "snapDestination: %f, %f\n",
        this, friction, velocity.x.value, velocity.y.value,
        predictedDelta.x.value, predictedDelta.y.value,
        Metrics().GetVisualScrollOffset().x.value,
        Metrics().GetVisualScrollOffset().y.value, startPosition.x.value,
        startPosition.y.value);

    // Ensure that any queued transform-end due to a pan-end is not
    // sent. Instead rely on the transform-end sent due to the
    // scroll snap animation.
    SetDelayedTransformEnd(false);

    SmoothMsdScrollTo(std::move(*snapDestination), ScrollTriggeredByScript::No);
  }
}

Maybe<CSSSnapDestination>
AsyncPanZoomController::MaybeAdjustDeltaForScrollSnapping(
    ScrollUnit aUnit, ScrollSnapFlags aSnapFlags, ParentLayerPoint& aDelta,
    CSSPoint& aStartPosition) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  CSSToParentLayerScale zoom = Metrics().GetZoom();
  if (zoom == CSSToParentLayerScale(0)) {
    return Nothing();
  }
  CSSPoint destination = Metrics().CalculateScrollRange().ClampPoint(
      aStartPosition + ToCSSPixels(aDelta));

  if (Maybe<CSSSnapDestination> snapDestination =
          FindSnapPointNear(destination, aUnit, aSnapFlags)) {
    aDelta = (snapDestination->mPosition - aStartPosition) * zoom;
    aStartPosition = snapDestination->mPosition;
    return snapDestination;
  }
  return Nothing();
}

Maybe<CSSSnapDestination>
AsyncPanZoomController::MaybeAdjustDeltaForScrollSnappingOnWheelInput(
    const ScrollWheelInput& aEvent, ParentLayerPoint& aDelta,
    CSSPoint& aStartPosition) {
  // Don't scroll snap for pixel scrolls. This matches the main thread
  // behaviour in EventStateManager::DoScrollText().
  if (aEvent.mDeltaType == ScrollWheelInput::SCROLLDELTA_PIXEL) {
    return Nothing();
  }

  // Note that this MaybeAdjustDeltaForScrollSnappingOnWheelInput also gets
  // called for pan gestures at least on older Mac and Windows. In such cases
  // `aEvent.mDeltaType` is `SCROLLDELTA_PIXEL` which should be filtered out by
  // the above `if` block, so we assume all incoming `aEvent` are purely wheel
  // events, thus we basically use `IntendedDirection` here.
  // If we want to change the behavior, i.e. we want to do scroll snap for
  // such cases as well, we need to use `IntendedEndPoint`.
  ScrollSnapFlags snapFlags = ScrollSnapFlags::IntendedDirection;
  if (aEvent.mDeltaType == ScrollWheelInput::SCROLLDELTA_PAGE) {
    // On Windows there are a couple of cases where scroll events happen with
    // SCROLLDELTA_PAGE, in such case we consider it's a page scroll.
    snapFlags |= ScrollSnapFlags::IntendedEndPosition;
  }
  return MaybeAdjustDeltaForScrollSnapping(
      ScrollWheelInput::ScrollUnitForDeltaType(aEvent.mDeltaType),
      ScrollSnapFlags::IntendedDirection, aDelta, aStartPosition);
}

Maybe<CSSSnapDestination>
AsyncPanZoomController::MaybeAdjustDestinationForScrollSnapping(
    const KeyboardInput& aEvent, CSSPoint& aDestination,
    ScrollSnapFlags aSnapFlags) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  ScrollUnit unit = KeyboardScrollAction::GetScrollUnit(aEvent.mAction.mType);

  if (Maybe<CSSSnapDestination> snapPoint =
          FindSnapPointNear(aDestination, unit, aSnapFlags)) {
    aDestination = snapPoint->mPosition;
    return snapPoint;
  }
  return Nothing();
}

void AsyncPanZoomController::SetZoomAnimationId(
    const Maybe<uint64_t>& aZoomAnimationId) {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  mZoomAnimationId = aZoomAnimationId;
}

Maybe<uint64_t> AsyncPanZoomController::GetZoomAnimationId() const {
  RecursiveMutexAutoLock lock(mRecursiveMutex);
  return mZoomAnimationId;
}

CSSPoint AsyncPanZoomController::MaybeFillOutOverscrollGutter(
    const RecursiveMutexAutoLock& aProofOfLock) {
  CSSPoint delta = ToCSSPixels(GetOverscrollAmount());
  CSSPoint origin = Metrics().GetVisualScrollOffset();
  CSSRect scrollRange = Metrics().CalculateScrollRange();
  if (!scrollRange.ContainsInclusively(origin + delta)) {
    return CSSPoint();
  }
  SetVisualScrollOffset(origin + delta);
  Metrics().RecalculateLayoutViewportOffset();
  return Metrics().GetVisualScrollOffset() - origin;
}

std::ostream& operator<<(std::ostream& aOut,
                         const AsyncPanZoomController::PanZoomState& aState) {
  switch (aState) {
    case AsyncPanZoomController::PanZoomState::NOTHING:
      aOut << "NOTHING";
      break;
    case AsyncPanZoomController::PanZoomState::FLING:
      aOut << "FLING";
      break;
    case AsyncPanZoomController::PanZoomState::TOUCHING:
      aOut << "TOUCHING";
      break;
    case AsyncPanZoomController::PanZoomState::PANNING:
      aOut << "PANNING";
      break;
    case AsyncPanZoomController::PanZoomState::PANNING_LOCKED_X:
      aOut << "PANNING_LOCKED_X";
      break;
    case AsyncPanZoomController::PanZoomState::PANNING_LOCKED_Y:
      aOut << "PANNING_LOCKED_Y";
      break;
    case AsyncPanZoomController::PanZoomState::PAN_MOMENTUM:
      aOut << "PAN_MOMENTUM";
      break;
    case AsyncPanZoomController::PanZoomState::PINCHING:
      aOut << "PINCHING";
      break;
    case AsyncPanZoomController::PanZoomState::ANIMATING_ZOOM:
      aOut << "ANIMATING_ZOOM";
      break;
    case AsyncPanZoomController::PanZoomState::OVERSCROLL_ANIMATION:
      aOut << "OVERSCROLL_ANIMATION";
      break;
    case AsyncPanZoomController::PanZoomState::SMOOTH_SCROLL:
      aOut << "SMOOTH_SCROLL";
      break;
    case AsyncPanZoomController::PanZoomState::SMOOTHMSD_SCROLL:
      aOut << "SMOOTHMSD_SCROLL";
      break;
    case AsyncPanZoomController::PanZoomState::WHEEL_SCROLL:
      aOut << "WHEEL_SCROLL";
      break;
    case AsyncPanZoomController::PanZoomState::KEYBOARD_SCROLL:
      aOut << "KEYBOARD_SCROLL";
      break;
    case AsyncPanZoomController::PanZoomState::AUTOSCROLL:
      aOut << "AUTOSCROLL";
      break;
    case AsyncPanZoomController::PanZoomState::SCROLLBAR_DRAG:
      aOut << "SCROLLBAR_DRAG";
      break;
    default:
      aOut << "UNKNOWN_STATE";
      break;
  }
  return aOut;
}

bool operator==(const PointerEventsConsumableFlags& aLhs,
                const PointerEventsConsumableFlags& aRhs) {
  return (aLhs.mHasRoom == aRhs.mHasRoom) &&
         (aLhs.mAllowedByTouchAction == aRhs.mAllowedByTouchAction);
}

std::ostream& operator<<(std::ostream& aOut,
                         const PointerEventsConsumableFlags& aFlags) {
  aOut << std::boolalpha << "{ hasRoom: " << aFlags.mHasRoom
       << ", allowedByTouchAction: " << aFlags.mAllowedByTouchAction << "}";
  return aOut;
}

}  // namespace layers
}  // namespace mozilla
