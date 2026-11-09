/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example.utils;

import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import androidx.annotation.NonNull;
import androidx.core.graphics.Insets;
import androidx.core.view.OnApplyWindowInsetsListener;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.core.view.WindowInsetsControllerCompat;

/** Collection of helper methods specific to the Android {@link Window}. */
public class WindowUtils {

  /**
   * Setup handling persistent insets - system bars and display cutouts ourselves instead of the
   * framework. This results in keeping the same behavior for such insets while allowing to
   * separately control the behavior for other dynamic insets.
   *
   * <p>This only works on Android Q and above. On older versions calling this will result in no-op.
   */
  public static void setupPersistentInsets(Window window, final boolean isBrowser) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      WindowCompat.setDecorFitsSystemWindows(window, false);

      View rootView = window.getDecorView().findViewById(android.R.id.content);
      int persistentInsetsTypes =
          WindowInsetsCompat.Type.systemBars() | WindowInsetsCompat.Type.displayCutout();

      ViewCompat.setOnApplyWindowInsetsListener(
          rootView,
          (v, windowInsets) -> {
            boolean isInImmersiveMode =
                (window.getAttributes().flags & WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS)
                    != 0;
            Insets persistentInsets =
                isInImmersiveMode
                    ? Insets.of(0, 0, 0, 0)
                    : windowInsets.getInsets(persistentInsetsTypes);

            rootView.setPadding(
                persistentInsets.left,
                persistentInsets.top,
                persistentInsets.right,
                isBrowser && EdgeToEdgeUtils.isEdgeToEdgeEnabled() ? 0 : persistentInsets.bottom);

            // Pass window insets further to allow below listeners also know when there is a change.
            return windowInsets;
          });
    }
  }

  /**
   * Allow the window to be resized to fit the keyboard.
   *
   * <p>This only works on Android P and below. On newer versions calling this will result in no-op.
   */
  @SuppressWarnings("deprecation")
  public static void setupImeBehavior(Window window) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) {
      window.setSoftInputMode(
          WindowManager.LayoutParams.SOFT_INPUT_STATE_UNSPECIFIED
              | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
    }
  }

  /**
   * Attempts to enter in immersive mode - fullscreen with the status bar and navigation buttons
   * hidden, expanding itself into the notch area for devices running API 28+.
   *
   * <p>This will automatically register and use an inset listener: {@link
   * android.view.View.OnApplyWindowInsetsListener} to restore immersive mode if interactions with
   * various other widgets like the keyboard or dialogs get the window out of immersive mode without
   * {@link #exitImmersiveMode(Window)} being called.
   */
  public static void enterImmersiveMode(Window window) {
    WindowInsetsControllerCompat insetsController =
        new WindowInsetsControllerCompat(window, window.getDecorView());
    insetsController.hide(WindowInsetsCompat.Type.systemBars());
    insetsController.setSystemBarsBehavior(
        WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);

    OnApplyWindowInsetsListener insetsListener =
        new OnApplyWindowInsetsListener() {
          @NonNull
          @Override
          public WindowInsetsCompat onApplyWindowInsets(
              @NonNull View view, WindowInsetsCompat insetsCompat) {
            if (insetsCompat.isVisible(WindowInsetsCompat.Type.statusBars())) {
              insetsController.hide(WindowInsetsCompat.Type.systemBars());
              insetsController.setSystemBarsBehavior(
                  WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
            // Allow the decor view to have a chance to process the incoming WindowInsets.
            return ViewCompat.onApplyWindowInsets(view, insetsCompat);
          }
        };

    ViewCompat.setOnApplyWindowInsetsListener(window.getDecorView(), insetsListener);

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      window.setFlags(
          WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
          WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
      WindowManager.LayoutParams attributes = window.getAttributes();
      attributes.layoutInDisplayCutoutMode =
          WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
      window.setAttributes(attributes);
    }
  }

  /**
   * Shows the system UI insets that were hidden for this window, thereby exiting the immersive
   * experience.
   *
   * <p>For devices running API 28+, this function also restores the application's use of the notch
   * area of the phone to the default behavior.
   */
  public static void exitImmersiveMode(Window window) {
    WindowInsetsControllerCompat insetsController =
        new WindowInsetsControllerCompat(window, window.getDecorView());
    insetsController.show(WindowInsetsCompat.Type.systemBars());

    ViewCompat.setOnApplyWindowInsetsListener(window.getDecorView(), null);
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
      window.clearFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);
      WindowManager.LayoutParams attributes = window.getAttributes();
      attributes.layoutInDisplayCutoutMode =
          WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
      window.setAttributes(attributes);
    }
  }
}
