/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Matrix;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.TransactionTooLargeException;
import android.text.TextUtils;
import android.util.Log;
import android.view.ActionMode;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.UiThread;
import java.util.ArrayList;
import java.util.List;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * Class that implements a basic SelectionActionDelegate. This class is used by GeckoView by default
 * if the consumer does not explicitly set a SelectionActionDelegate.
 *
 * <p>To provide custom actions, extend this class and override the following methods,
 *
 * <p>1) Override {@link #getAllActions} to include custom action IDs in the returned array. This
 * array must include all actions, available or not, and must not change over the class lifetime.
 *
 * <p>2) Override {@link #isActionAvailable} to return whether a custom action is currently
 * available.
 *
 * <p>3) Override {@link #prepareAction} to set custom title and/or icon for a custom action.
 *
 * <p>4) Override {@link #performAction} to perform a custom action when used.
 */
@UiThread
public class BasicSelectionActionDelegate
    implements ActionMode.Callback, GeckoSession.SelectionActionDelegate {
  private static final String LOGTAG = "BasicSelectionAction";

  /** Intent action for processing text. */
  protected static final String ACTION_PROCESS_TEXT = Intent.ACTION_PROCESS_TEXT;

  private static final String[] FLOATING_TOOLBAR_ACTIONS =
      new String[] {
        ACTION_CUT,
        ACTION_COPY,
        ACTION_PASTE,
        ACTION_SELECT_ALL,
        ACTION_PASTE_AS_PLAIN_TEXT,
        ACTION_PROCESS_TEXT
      };
  private static final String[] FIXED_TOOLBAR_ACTIONS =
      new String[] {ACTION_SELECT_ALL, ACTION_CUT, ACTION_COPY, ACTION_PASTE};

  // This is limitation of intent text.
  private static final int MAX_INTENT_TEXT_LENGTH = 100000;

  /** The activity associated with this delegate. */
  protected final @NonNull Activity mActivity;

  /** Whether to use floating toolbar for selection actions. */
  protected final boolean mUseFloatingToolbar;

  private boolean mExternalActionsEnabled;

  /** The current action mode, if any. */
  protected @Nullable ActionMode mActionMode;

  /** The current GeckoSession associated with selection. */
  protected @Nullable GeckoSession mSession;

  /** The current text selection. */
  protected @Nullable Selection mSelection;

  /** Whether the menu has been repopulated. */
  protected boolean mRepopulatedMenu;

  private @Nullable ActionMode mActionModeForClipboardPermission;

  private class Callback2Wrapper extends ActionMode.Callback2 {
    @Override
    public boolean onCreateActionMode(final ActionMode actionMode, final Menu menu) {
      return BasicSelectionActionDelegate.this.onCreateActionMode(actionMode, menu);
    }

    @Override
    public boolean onPrepareActionMode(final ActionMode actionMode, final Menu menu) {
      return BasicSelectionActionDelegate.this.onPrepareActionMode(actionMode, menu);
    }

    @Override
    public boolean onActionItemClicked(final ActionMode actionMode, final MenuItem menuItem) {
      return BasicSelectionActionDelegate.this.onActionItemClicked(actionMode, menuItem);
    }

    @Override
    public void onDestroyActionMode(final ActionMode actionMode) {
      BasicSelectionActionDelegate.this.onDestroyActionMode(actionMode);
    }

    @Override
    public void onGetContentRect(final ActionMode mode, final View view, final Rect outRect) {
      super.onGetContentRect(mode, view, outRect);
      BasicSelectionActionDelegate.this.onGetContentRect(mode, view, outRect);
    }
  }

  /**
   * Construct a BasicSelectionActionDelegate.
   *
   * @param activity The activity to associate with this delegate
   */
  public BasicSelectionActionDelegate(final @NonNull Activity activity) {
    this(activity, true);
  }

  /**
   * Construct a BasicSelectionActionDelegate.
   *
   * @param activity The activity to associate with this delegate
   * @param useFloatingToolbar Whether to use floating toolbar for selection
   */
  public BasicSelectionActionDelegate(
      final @NonNull Activity activity, final boolean useFloatingToolbar) {
    mActivity = activity;
    mUseFloatingToolbar = useFloatingToolbar;
    mExternalActionsEnabled = true;
  }

  /**
   * Set whether to include text actions from other apps in the floating toolbar.
   *
   * @param enable True if external actions should be enabled.
   */
  public void enableExternalActions(final boolean enable) {
    ThreadUtils.assertOnUiThread();
    mExternalActionsEnabled = enable;

    if (mActionMode != null) {
      mActionMode.invalidate();
    }
  }

  /**
   * Get whether text actions from other apps are enabled.
   *
   * @return True if external actions are enabled.
   */
  public boolean areExternalActionsEnabled() {
    return mExternalActionsEnabled;
  }

  /**
   * Return list of all actions in proper order, regardless of their availability at present.
   * Override to add to or remove from the default set.
   *
   * @return Array of action IDs in proper order.
   */
  protected @NonNull String[] getAllActions() {
    return mUseFloatingToolbar ? FLOATING_TOOLBAR_ACTIONS : FIXED_TOOLBAR_ACTIONS;
  }

  /**
   * Return whether an action is presently available. Override to indicate availability for custom
   * actions.
   *
   * @param id Action ID.
   * @return True if the action is presently available.
   */
  protected boolean isActionAvailable(final @NonNull String id) {
    if (mSelection == null) {
      return false;
    }

    if (mExternalActionsEnabled && !mSelection.text.isEmpty() && ACTION_PROCESS_TEXT.equals(id)) {
      return !getProcessTextExportedActivities().isEmpty();
    }

    return mSelection.isActionAvailable(id);
  }

  /**
   * Get exported activities for {@link BasicSelectionActionDelegate#ACTION_PROCESS_TEXT} when text
   * is selected.
   *
   * @return list of exported activities
   */
  private @NonNull List<ResolveInfo> getProcessTextExportedActivities() {
    final PackageManager pm = mActivity.getPackageManager();
    final List<ResolveInfo> resolvedList =
        pm.queryIntentActivityOptions(
            null, null, getProcessTextIntent(null), PackageManager.MATCH_DEFAULT_ONLY);
    final ArrayList<ResolveInfo> exportedList = new ArrayList<>();
    for (final ResolveInfo info : resolvedList) {
      if (info.activityInfo.exported) {
        exportedList.add(info);
      }
    }

    return exportedList;
  }

  /**
   * Provides access to whether there are text selection actions available. Override to indicate
   * availability for custom actions.
   *
   * @return True if there are text selection actions available.
   */
  public boolean isActionAvailable() {
    if (mSelection == null) {
      return false;
    }

    return isActionAvailable(ACTION_PROCESS_TEXT) || !mSelection.availableActions.isEmpty();
  }

  /**
   * Prepare a menu item corresponding to a certain action. Override to prepare menu item for custom
   * action.
   *
   * @param id Action ID.
   * @param item New menu item to prepare.
   */
  protected void prepareAction(final @NonNull String id, final @NonNull MenuItem item) {
    switch (id) {
      case ACTION_CUT:
        item.setTitle(android.R.string.cut);
        break;
      case ACTION_COPY:
        item.setTitle(android.R.string.copy);
        break;
      case ACTION_PASTE:
        item.setTitle(android.R.string.paste);
        break;
      case ACTION_PASTE_AS_PLAIN_TEXT:
        item.setTitle(android.R.string.paste_as_plain_text);
        break;
      case ACTION_SELECT_ALL:
        item.setTitle(android.R.string.selectAll);
        break;
      case ACTION_PROCESS_TEXT:
        throw new IllegalStateException("Unexpected action");
    }
  }

  /**
   * Perform the specified action. Override to perform custom actions.
   *
   * @param id Action ID.
   * @param item Nenu item for the action.
   * @return True if the action was performed.
   */
  protected boolean performAction(final @NonNull String id, final @NonNull MenuItem item) {
    if (ACTION_PROCESS_TEXT.equals(id)) {
      try {
        mActivity.startActivity(item.getIntent());
      } catch (final ActivityNotFoundException e) {
        Log.e(LOGTAG, "Cannot perform action", e);
        return false;
      }
      return true;
    }

    if (mSelection == null) {
      return false;
    }
    mSelection.execute(id);

    // Android behavior is to clear selection on copy.
    if (ACTION_COPY.equals(id)) {
      if (mUseFloatingToolbar) {
        clearSelection();
      } else {
        mActionMode.finish();
      }
    }
    return true;
  }

  /**
   * Get the current selection object. This object should not be stored as it does not update when
   * the selection becomes invalid. Stale actions are ignored.
   *
   * @return The {@link GeckoSession.SelectionActionDelegate.Selection} attached to the current
   *     action menu. <code>null</code> if no action menu is active.
   */
  public @Nullable Selection getSelection() {
    return mSelection;
  }

  /** Clear the current selection, if possible. */
  public void clearSelection() {
    if (mSelection == null) {
      return;
    }

    if (isActionAvailable(ACTION_COLLAPSE_TO_END)) {
      mSelection.collapseToEnd();
    } else if (isActionAvailable(ACTION_UNSELECT)) {
      mSelection.unselect();
    } else {
      mSelection.hide();
    }
  }

  private String getSelectedText(final int maxLength) {
    if (mSelection == null) {
      return "";
    }

    if (TextUtils.isEmpty(mSelection.text) || mSelection.text.length() < maxLength) {
      return mSelection.text;
    }

    return mSelection.text.substring(0, maxLength);
  }

  private Intent getProcessTextIntent(@Nullable final ResolveInfo resolveInfo) {
    final Intent intent = new Intent(Intent.ACTION_PROCESS_TEXT);
    if (resolveInfo != null) {
      intent.setComponent(
          new ComponentName(resolveInfo.activityInfo.packageName, resolveInfo.activityInfo.name));
    }
    intent.addCategory(Intent.CATEGORY_DEFAULT);
    intent.setType("text/plain");
    // If using large text, anything intent may throw RemoteException.
    intent.putExtra(Intent.EXTRA_PROCESS_TEXT, getSelectedText(MAX_INTENT_TEXT_LENGTH));
    // TODO: implement ability to replace text in Gecko for editable selection (bug 1453137).
    intent.putExtra(Intent.EXTRA_PROCESS_TEXT_READONLY, true);
    return intent;
  }

  @Override
  public boolean onCreateActionMode(final ActionMode actionMode, final Menu menu) {
    ThreadUtils.assertOnUiThread();
    final String[] allActions = getAllActions();
    for (final String actionId : allActions) {
      if (isActionAvailable(actionId)) {
        if (!mUseFloatingToolbar) {
          // Android bug where onPrepareActionMode is not called initially.
          onPrepareActionMode(actionMode, menu);
        }
        return true;
      }
    }
    return false;
  }

  @Override
  public boolean onPrepareActionMode(final ActionMode actionMode, final Menu menu) {
    ThreadUtils.assertOnUiThread();
    final String[] allActions = getAllActions();
    boolean changed = false;

    // Whether we are repopulating an existing menu.
    mRepopulatedMenu = menu.size() != 0;

    // For each action, see if it's available at present, and if necessary,
    // add to or remove from menu.
    for (int i = 0; i < allActions.length; i++) {
      final String actionId = allActions[i];
      final int menuId = i + Menu.FIRST;

      if (ACTION_PROCESS_TEXT.equals(actionId)) {
        if (mExternalActionsEnabled && mSelection != null && !mSelection.text.isEmpty()) {
          final List<ResolveInfo> exportedPackageInfo = getProcessTextExportedActivities();
          if (!exportedPackageInfo.isEmpty()) {
            for (final ResolveInfo info : exportedPackageInfo) {
              final boolean isMenuItemAdded = addProcessTextMenuItem(menu, menuId, info);
              if (isMenuItemAdded) {
                changed = true;
              }
            }
          }
        } else if (menu.findItem(menuId) != null) {
          menu.removeGroup(menuId);
          changed = true;
        }
        continue;
      }

      if (isActionAvailable(actionId)) {
        if (menu.findItem(menuId) == null) {
          prepareAction(actionId, menu.add(/* group */ Menu.NONE, menuId, menuId, /* title */ ""));
          changed = true;
        }
      } else if (menu.findItem(menuId) != null) {
        menu.removeItem(menuId);
        changed = true;
      }
    }
    return changed;
  }

  private boolean addProcessTextMenuItem(
      final Menu menu, final int menuId, final ResolveInfo info) {
    final PackageManager pm = mActivity.getPackageManager();
    boolean isMenuItemAdded = false;
    try {
      menu.add(menuId, menuId, menuId, info.loadLabel(pm))
          .setIcon(info.loadIcon(pm))
          .setIntent(getProcessTextIntent(info));
      isMenuItemAdded = true;
    } catch (final RuntimeException e) {
      if (e.getCause() instanceof TransactionTooLargeException) {
        // Binder size error. MAX_INTENT_TEXT_LENGTH is still large?
        Log.e(LOGTAG, "Cannot add intent option", e);
      } else {
        throw e;
      }
    }
    return isMenuItemAdded;
  }

  @Override
  public boolean onActionItemClicked(final ActionMode actionMode, final MenuItem menuItem) {
    ThreadUtils.assertOnUiThread();
    MenuItem realMenuItem = null;
    if (mRepopulatedMenu) {
      // When we repopulate an existing menu, Android can sometimes give us an old,
      // deleted MenuItem. Find the current MenuItem that corresponds to the old one.
      final Menu menu = actionMode.getMenu();
      final int size = menu.size();
      for (int i = 0; i < size; i++) {
        final MenuItem item = menu.getItem(i);
        if (item == menuItem
            || (item.getItemId() == menuItem.getItemId()
                && item.getTitle().equals(menuItem.getTitle()))) {
          realMenuItem = item;
          break;
        }
      }
    } else {
      realMenuItem = menuItem;
    }

    if (realMenuItem == null) {
      return false;
    }
    final String[] allActions = getAllActions();
    return performAction(allActions[realMenuItem.getItemId() - Menu.FIRST], realMenuItem);
  }

  @Override
  public void onDestroyActionMode(final ActionMode actionMode) {
    ThreadUtils.assertOnUiThread();
    if (!mUseFloatingToolbar) {
      clearSelection();
    }
    mSession = null;
    mSelection = null;
    mActionMode = null;
  }

  /**
   * Called to get the content rectangle for the selection.
   *
   * @param mode The action mode
   * @param view The view
   * @param outRect The rectangle to be filled with content bounds
   */
  public void onGetContentRect(
      final @Nullable ActionMode mode, final @Nullable View view, final @NonNull Rect outRect) {
    ThreadUtils.assertOnUiThread();
    if (mSelection == null || mSelection.screenRect == null) {
      return;
    }

    // outRect has to convert to current window coordinate.
    final Matrix matrix = new Matrix();
    mSession.getScreenToWindowManagerOffsetMatrix(mActivity, matrix);
    final RectF transformedRect = new RectF();
    matrix.mapRect(transformedRect, mSelection.screenRect);
    transformedRect.roundOut(outRect);
  }

  @Override
  public void onShowActionRequest(final GeckoSession session, final Selection selection) {
    ThreadUtils.assertOnUiThread();
    mSession = session;
    mSelection = selection;

    if (mActionMode != null) {
      if (isActionAvailable()) {
        mActionMode.invalidate();
      } else {
        mActionMode.finish();
      }
      return;
    }

    if (mActionModeForClipboardPermission != null) {
      mActionModeForClipboardPermission.finish();
      return;
    }

    if (mUseFloatingToolbar) {
      mActionMode = mActivity.startActionMode(new Callback2Wrapper(), ActionMode.TYPE_FLOATING);
    } else {
      mActionMode = mActivity.startActionMode(this);
    }
  }

  @Override
  public void onHideAction(final GeckoSession session, final int reason) {
    ThreadUtils.assertOnUiThread();
    if (mActionMode == null) {
      return;
    }

    switch (reason) {
      case HIDE_REASON_ACTIVE_SCROLL:
      case HIDE_REASON_ACTIVE_SELECTION:
      case HIDE_REASON_INVISIBLE_SELECTION:
        if (mUseFloatingToolbar) {
          // Hide the floating toolbar when scrolling/selecting.
          mActionMode.finish();
        }
        break;

      case HIDE_REASON_NO_SELECTION:
        mActionMode.finish();
        break;
    }
  }

  /** Callback class of clipboard permission. This is used on pre-M only */
  private class ClipboardPermissionCallback implements ActionMode.Callback {
    private GeckoResult<AllowOrDeny> mResult;

    public ClipboardPermissionCallback(final GeckoResult<AllowOrDeny> result) {
      mResult = result;
    }

    @Override
    public boolean onCreateActionMode(final ActionMode actionMode, final Menu menu) {
      return BasicSelectionActionDelegate.this.onCreateActionModeForClipboardPermission(
          actionMode, menu);
    }

    @Override
    public boolean onPrepareActionMode(final ActionMode actionMode, final Menu menu) {
      return false;
    }

    @Override
    public boolean onActionItemClicked(final ActionMode actionMode, final MenuItem menuItem) {
      mResult.complete(AllowOrDeny.ALLOW);
      mResult = null;
      actionMode.finish();
      return true;
    }

    @Override
    public void onDestroyActionMode(final ActionMode actionMode) {
      if (mResult != null) {
        mResult.complete(AllowOrDeny.DENY);
      }
      BasicSelectionActionDelegate.this.onDestroyActionModeForClipboardPermission(actionMode);
    }
  }

  /** Callback class of clipboard permission for Android M+ */
  private class ClipboardPermissionCallbackM extends ActionMode.Callback2 {
    private @Nullable GeckoResult<AllowOrDeny> mResult;
    private final @NonNull GeckoSession mSession;
    private final @Nullable Point mPoint;

    public ClipboardPermissionCallbackM(
        final @NonNull GeckoSession session,
        final @Nullable Point screenPoint,
        final @NonNull GeckoResult<AllowOrDeny> result) {
      mSession = session;
      mPoint = screenPoint;
      mResult = result;
    }

    @Override
    public boolean onCreateActionMode(final ActionMode actionMode, final Menu menu) {
      return BasicSelectionActionDelegate.this.onCreateActionModeForClipboardPermission(
          actionMode, menu);
    }

    @Override
    public boolean onPrepareActionMode(final ActionMode actionMode, final Menu menu) {
      return false;
    }

    @Override
    public boolean onActionItemClicked(final ActionMode actionMode, final MenuItem menuItem) {
      if (mResult != null) {
        mResult.complete(AllowOrDeny.ALLOW);
        mResult = null;
      }
      actionMode.finish();
      return true;
    }

    @Override
    public void onDestroyActionMode(final ActionMode actionMode) {
      if (mResult != null) {
        mResult.complete(AllowOrDeny.DENY);
        mResult = null;
      }
      BasicSelectionActionDelegate.this.onDestroyActionModeForClipboardPermission(actionMode);
    }

    @Override
    public void onGetContentRect(final ActionMode mode, final View view, final Rect outRect) {
      super.onGetContentRect(mode, view, outRect);

      if (mPoint == null) {
        return;
      }

      outRect.set(mPoint.x, mPoint.y, mPoint.x + 1, mPoint.y + 1);
    }
  }

  /**
   * Show action mode bar to request clipboard permission
   *
   * @param session The GeckoSession that initiated the callback.
   * @param permission An {@link ClipboardPermission} describing the permission being requested.
   * @return A {@link GeckoResult} with {@link AllowOrDeny}, determining the response to the
   *     permission request for this site.
   */
  @Override
  public GeckoResult<AllowOrDeny> onShowClipboardPermissionRequest(
      final GeckoSession session, final ClipboardPermission permission) {
    ThreadUtils.assertOnUiThread();

    final GeckoResult<AllowOrDeny> result = new GeckoResult<>();

    if (mActionMode != null) {
      mActionMode.finish();
      mActionMode = null;
    }
    if (mActionModeForClipboardPermission != null) {
      mActionModeForClipboardPermission.finish();
      mActionModeForClipboardPermission = null;
    }

    if (mUseFloatingToolbar) {
      mActionModeForClipboardPermission =
          mActivity.startActionMode(
              new ClipboardPermissionCallbackM(session, permission.screenPoint, result),
              ActionMode.TYPE_FLOATING);
    } else {
      mActionModeForClipboardPermission =
          mActivity.startActionMode(new ClipboardPermissionCallback(result));
    }

    return result;
  }

  /**
   * Dismiss action mode for requesting clipboard permission popup or model.
   *
   * @param session The GeckoSession that initiated the callback.
   */
  @Override
  public void onDismissClipboardPermissionRequest(final GeckoSession session) {
    ThreadUtils.assertOnUiThread();

    if (mActionModeForClipboardPermission != null) {
      mActionModeForClipboardPermission.finish();
      mActionModeForClipboardPermission = null;
    }
  }

  /* package */ boolean onCreateActionModeForClipboardPermission(
      final ActionMode actionMode, final Menu menu) {
    final MenuItem item = menu.add(/* group */ Menu.NONE, Menu.FIRST, Menu.FIRST, /* title */ "");
    item.setTitle(android.R.string.paste);
    return true;
  }

  /* package */ void onDestroyActionModeForClipboardPermission(final ActionMode actionMode) {
    mActionModeForClipboardPermission = null;
  }
}
