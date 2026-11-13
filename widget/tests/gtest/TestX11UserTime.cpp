/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/WidgetUtilsGtk.h"

// clang-format off: X11 macros conflict with gtest.
#include "X11/Xatom.h"
#include "gdk/gdkx.h"
#include "gtk/gtk.h"
// clang-format on

#include "X11UndefineNone.h"

using namespace mozilla::widget;

class X11UserTime : public testing::Test {
 protected:
  void SetUp() override {
    if (!gtk_init_check(nullptr, nullptr)) {
      GTEST_SKIP() << "No GTK display";
    }
    mDisplay = gdk_display_get_default();
    GType x11DisplayType = g_type_from_name("GdkX11Display");
    if (!x11DisplayType ||
        !G_TYPE_CHECK_INSTANCE_TYPE(mDisplay, x11DisplayType)) {
      GTEST_SKIP() << "Requires an X11 display";
    }
    mXDisplay = GDK_DISPLAY_XDISPLAY(mDisplay);
    GdkWindowAttr attributes = {};
    attributes.width = 1;
    attributes.height = 1;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.event_mask = GDK_PROPERTY_CHANGE_MASK;
    mRoot = gdk_window_new(gdk_get_default_root_window(), &attributes, 0);
    ASSERT_NE(mRoot, nullptr);
  }

  void TearDown() override {
    if (mRoot) {
      gdk_window_destroy(mRoot);
    }
  }

  Window AddWindow(uint32_t aTime, Window aParent = 0) {
    Window window = XCreateSimpleWindow(
        mXDisplay, aParent ? aParent : GDK_WINDOW_XID(mRoot), 0, 0, 1, 1, 0, 0,
        0);
    unsigned long time = aTime;
    Atom atom =
        gdk_x11_get_xatom_by_name_for_display(mDisplay, "_NET_WM_USER_TIME");
    XChangeProperty(mXDisplay, window, atom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(&time), 1);
    return window;
  }

  uint32_t Find(uint32_t aCurrentTime) {
    uint32_t latestTime = 0;
    FindLatestUserTime(mDisplay, GDK_WINDOW_XID(mRoot), aCurrentTime,
                       &latestTime);
    return latestTime;
  }

  uint32_t ServerTime() { return gdk_x11_get_server_time(mRoot); }

  GdkDisplay* mDisplay = nullptr;
  Display* mXDisplay = nullptr;
  GdkWindow* mRoot = nullptr;
};

TEST_F(X11UserTime, EmptyTree) {
  EXPECT_EQ(Find(1000), 0u);
  EXPECT_EQ(GetX11StartupUserTime(mDisplay, GDK_WINDOW_XID(mRoot)), 0u);
}

TEST_F(X11UserTime, ZeroDoesNotRequestActivation) {
  AddWindow(0);
  EXPECT_EQ(Find(1000), 0u);
  EXPECT_EQ(GetX11StartupUserTime(mDisplay, GDK_WINDOW_XID(mRoot)), 0u);
}

TEST_F(X11UserTime, PreservesDefaultGroupWindow) {
  GdkWindow* group = gdk_display_get_default_group(mDisplay);
  ASSERT_NE(group, nullptr);
  g_object_ref(group);
  GdkEventMask events = gdk_window_get_events(group);

  EXPECT_EQ(GetX11StartupUserTime(mDisplay, GDK_WINDOW_XID(mRoot)), 0u);
  EXPECT_EQ(gdk_display_get_default_group(mDisplay), group);
  EXPECT_FALSE(gdk_window_is_destroyed(group));
  EXPECT_EQ(gdk_window_get_events(group), events);
  g_object_unref(group);
}

TEST_F(X11UserTime, MostRecentDescendant) {
  Window parent = AddWindow(800);
  AddWindow(950, parent);
  AddWindow(900);
  AddWindow(0);
  EXPECT_EQ(Find(1000), 950u);
}

TEST_F(X11UserTime, FutureTimeIsNormalized) {
  AddWindow(900);
  AddWindow(1100);
  AddWindow(950);
  EXPECT_EQ(Find(1000), 1000u);
}

TEST_F(X11UserTime, FutureTimeAcrossWrap) {
  AddWindow(0xffffff00);
  AddWindow(10);
  EXPECT_EQ(Find(0xfffffff0), 0xfffffff0u);
}

TEST_F(X11UserTime, MostRecentTimeAcrossWrap) {
  AddWindow(0xfffffff0);
  AddWindow(10);
  EXPECT_EQ(Find(20), 10u);
}

TEST_F(X11UserTime, OldTimeAcrossWrapDoesNotReplaceNewTime) {
  AddWindow(10);
  AddWindow(0xfffffff0);
  EXPECT_EQ(Find(20), 10u);
}

TEST_F(X11UserTime, ZeroDoesNotReplacePreWrapTime) {
  AddWindow(0xfffffff0);
  AddWindow(0);
  EXPECT_EQ(Find(20), 0xfffffff0u);
}

TEST_F(X11UserTime, HalfPeriodBoundary) {
  AddWindow(0x80000021);
  EXPECT_EQ(Find(32), 0x80000021u);
  AddWindow(0x80000020);
  EXPECT_EQ(Find(32), 32u);
}

TEST_F(X11UserTime, RealServerClockPreservesPastTime) {
  uint32_t pastTime = ServerTime() - 100;
  ASSERT_NE(pastTime, 0u);
  AddWindow(pastTime);
  EXPECT_EQ(GetX11StartupUserTime(mDisplay, GDK_WINDOW_XID(mRoot)), pastTime);
}

TEST_F(X11UserTime, RealServerClockBoundsFutureTime) {
  uint32_t futureTime = ServerTime() + 60000;
  ASSERT_NE(futureTime, 0u);
  AddWindow(futureTime);
  uint32_t before = ServerTime();
  uint32_t result = GetX11StartupUserTime(mDisplay, GDK_WINDOW_XID(mRoot));
  uint32_t after = ServerTime();
  EXPECT_LE(result - before, after - before);
  EXPECT_NE(result, futureTime);
}
