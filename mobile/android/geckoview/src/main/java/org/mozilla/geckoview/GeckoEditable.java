/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.graphics.RectF;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.os.SystemClock;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.Selection;
import android.text.Spannable;
import android.text.SpannableString;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextPaint;
import android.text.TextUtils;
import android.text.method.KeyListener;
import android.text.method.TextKeyListener;
import android.text.style.CharacterStyle;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyCharacterMap;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.ref.WeakReference;
import java.lang.reflect.Array;
import java.lang.reflect.Field;
import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import org.mozilla.gecko.GeckoEditableChild;
import org.mozilla.gecko.IGeckoEditableChild;
import org.mozilla.gecko.IGeckoEditableParent;
import org.mozilla.gecko.MozLog;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;
import org.mozilla.gecko.util.ThreadUtils.AssertBehavior;
import org.mozilla.geckoview.SessionTextInput.EditableListener.IMEContextFlags;
import org.mozilla.geckoview.SessionTextInput.EditableListener.IMENotificationType;
import org.mozilla.geckoview.SessionTextInput.EditableListener.IMEState;

/**
 * GeckoEditable implements only some functions of Editable The field mText contains the actual
 * underlying SpannableStringBuilder/Editable that contains our text.
 */
/* package */ final class GeckoEditable extends IGeckoEditableParent.Stub
    implements InvocationHandler, Editable, SessionTextInput.EditableClient {

  private static final boolean DEBUG = false;
  private static final boolean LOGGING = BuildConfig.NIGHTLY_BUILD;
  private static final String LOGTAG = "GeckoEditable";
  private static final String MOZLOGTAG = "IMEHandler";

  private static final long DISMISS_VKB_DELAY_MS = 100;

  // Filters to implement Editable's filtering functionality
  private InputFilter[] mFilters;

  /**
   * We need a WeakReference here to avoid unnecessary retention of the GeckoSession. Passing
   * objects around via JNI seems to confuse the GC into thinking we have a native GC root.
   */
  /* package */ final WeakReference<GeckoSession> mSession;

  private final AsyncText mText;
  private final Editable mProxy;
  private final ConcurrentLinkedQueue<Action> mActions;
  private KeyCharacterMap mKeyMap;

  // mIcRunHandler is the Handler that currently runs Gecko-to-IC Runnables
  // mIcPostHandler is the Handler to post Gecko-to-IC Runnables to
  // The two can be different when switching from one handler to another
  private Handler mIcRunHandler;
  private Handler mIcPostHandler;

  // Parent process child used as a default for key events.
  /* package */ IGeckoEditableChild mDefaultChild; // Used by IC thread.
  // Parent or content process child that has the focus.
  /* package */ IGeckoEditableChild mFocusedChild; // Used by IC thread.
  /* package */ IBinder mFocusedToken; // Used by Gecko/binder thread.
  /* package */ SessionTextInput.EditableListener mListener;

  /* package */ boolean mInBatchMode; // Used by IC thread
  /* package */ boolean mNeedSync; // Used by IC thread
  private boolean mHasCompositionOnBeginningBatchMode = false; // Used by IC thread
  // Gecko side needs an updated composition from Java;
  private boolean mNeedUpdateComposition; // Used by IC thread
  private boolean mSuppressKeyUp; // Used by IC thread

  @IMEState
  private int mIMEState = // Used by IC thread.
      SessionTextInput.EditableListener.IME_STATE_DISABLED;

  private String mIMETypeHint = ""; // Used by IC/UI thread.
  private String mIMEModeHint = ""; // Used by IC thread.
  private String mIMEActionHint = ""; // Used by IC thread.
  private String mIMEAutocapitalize = ""; // Used by IC thread.
  private boolean mIMEAutocorrect = false; // Used by IC thread.
  @IMEContextFlags private int mIMEFlags; // Used by IC thread.
  private volatile boolean mIsNewICCreated = false; // Used by IC and UI

  private boolean mIgnoreSelectionChange; // Used by Gecko thread
  // Combined offsets from the previous batch of onTextChange calls; valid
  // between the onTextChange calls and the next onSelectionChange call.
  private int mLastTextChangeStart = Integer.MAX_VALUE; // Used by Gecko thread
  private int mLastTextChangeOldEnd = -1; // Used by Gecko thread
  private int mLastTextChangeNewEnd = -1; // Used by Gecko thread
  private boolean mLastTextChangeReplacedSelection; // Used by Gecko thread

  private static class HideSoftInputTask implements Runnable {
    private GeckoSession mSession;
    private boolean mCancelled;

    public HideSoftInputTask(final GeckoSession session) {
      mSession = session;
      mCancelled = false;
    }

    @Override
    public void run() {
      if (DEBUG) {
        ThreadUtils.assertOnUiThread();
      }
      if (mCancelled) {
        return;
      }

      // If View has no focus, keyboard will be handled by other focused View.
      // So we shouldn't show/dismiss keyboard now.
      final SessionTextInput textInput = mSession.getTextInput();
      final View view = textInput.getView();
      if (view != null && !view.hasFocus()) {
        return;
      }

      textInput.getDelegate().hideSoftInput(mSession);
    }

    public void cancel() {
      mCancelled = true;
    }
  }

  private HideSoftInputTask mHideSoftInputTask;

  // Prevent showSoftInput and hideSoftInput from being called multiple times in a row,
  // including reentrant calls on some devices. Used by UI/IC thread.
  /* package */ final AtomicInteger mSoftInputReentrancyGuard = new AtomicInteger();

  private static final int IME_RANGE_CARETPOSITION = 1;
  private static final int IME_RANGE_RAWINPUT = 2;
  private static final int IME_RANGE_SELECTEDRAWTEXT = 3;
  private static final int IME_RANGE_CONVERTEDTEXT = 4;
  private static final int IME_RANGE_SELECTEDCONVERTEDTEXT = 5;

  private static final int IME_RANGE_LINE_NONE = 0;
  private static final int IME_RANGE_LINE_SOLID = 1;
  private static final int IME_RANGE_LINE_DOTTED = 2;
  private static final int IME_RANGE_LINE_DASHED = 3;
  private static final int IME_RANGE_LINE_DOUBLE = 4;
  private static final int IME_RANGE_LINE_WAVY = 5;

  private static final int IME_RANGE_UNDERLINE = 1;
  private static final int IME_RANGE_FORECOLOR = 2;
  private static final int IME_RANGE_BACKCOLOR = 4;
  private static final int IME_RANGE_LINECOLOR = 8;

  private void onKeyEvent(
      final IGeckoEditableChild child,
      final KeyEvent event,
      final int action,
      final int savedMetaState,
      final boolean isSynthesizedImeKey)
      throws RemoteException {
    // Use a separate action argument so we can override the key's original action,
    // e.g. change ACTION_MULTIPLE to ACTION_DOWN. That way we don't have to allocate
    // a new key event just to change its action field.
    //
    // Normally we expect event.getMetaState() to reflect the current meta-state; however,
    // some software-generated key events may not have event.getMetaState() set, e.g. key
    // events from Swype. Therefore, it's necessary to combine the key's meta-states
    // with the meta-states that we keep separately in KeyListener
    final int metaState = event.getMetaState() | savedMetaState;
    final int unmodifiedMetaState =
        metaState & ~(KeyEvent.META_ALT_MASK | KeyEvent.META_CTRL_MASK | KeyEvent.META_META_MASK);

    final int unicodeChar = event.getUnicodeChar(metaState);
    final int unmodifiedUnicodeChar = event.getUnicodeChar(unmodifiedMetaState);
    final int domPrintableKeyValue =
        unicodeChar >= ' '
            ? unicodeChar
            : unmodifiedMetaState != metaState ? unmodifiedUnicodeChar : 0;
    final boolean waitingReply = GeckoInputConnection.isMediaKeyEvent(event);

    // If a modifier (e.g. meta key) caused a different character to be entered, we
    // drop that modifier from the metastate for the generated keypress event.
    final int keyPressMetaState =
        (unicodeChar >= ' ' && unicodeChar != unmodifiedUnicodeChar)
            ? unmodifiedMetaState
            : metaState;

    // For synthesized keys, ignore modifier metastates from the synthesized event,
    // because the synthesized modifier metastates don't reflect the actual state of
    // the meta keys (bug 1387889). For example, the Latin sharp S (U+00DF) is
    // synthesized as Alt+S, but we don't want the Alt metastate because the Alt key
    // is not actually pressed in this case.
    final int keyUpDownMetaState =
        isSynthesizedImeKey ? (unmodifiedMetaState | savedMetaState) : metaState;

    child.onKeyEvent(
        action,
        event.getKeyCode(),
        event.getScanCode(),
        keyUpDownMetaState,
        keyPressMetaState,
        event.getEventTime(),
        domPrintableKeyValue,
        event.getRepeatCount(),
        event.getFlags(),
        isSynthesizedImeKey,
        waitingReply,
        event);
  }

  /**
   * Class that encapsulates asynchronous text editing. There are two copies of the text, a current
   * copy and a shadow copy. Both can be modified independently through the current*** and shadow***
   * methods, respectively. The current copy can only be modified on the Gecko side and reflects the
   * authoritative version of the text. The shadow copy can only be modified on the IC side and
   * reflects what we think the current text is. Periodically, the shadow copy can be synced to the
   * current copy through syncShadowText, so the shadow copy once again refers to the same text as
   * the current copy.
   */
  private final class AsyncText {
    // The current text is the update-to-date version of the text, and is only updated
    // on the Gecko side.
    private final SpannableStringBuilder mCurrentText = new SpannableStringBuilder();
    // Track changes on the current side for syncing purposes.
    // Start of the changed range in current text since last sync.
    private int mCurrentStart = Integer.MAX_VALUE;
    // End of the changed range (before the change) in current text since last sync.
    private int mCurrentOldEnd;
    // End of the changed range (after the change) in current text since last sync.
    private int mCurrentNewEnd;
    // Track selection changes separately.
    private boolean mCurrentSelectionChanged;

    // The shadow text is what we think the current text is on the Java side, and is
    // periodically synced with the current text.
    private final SpannableStringBuilder mShadowText = new SpannableStringBuilder();
    // Track changes on the shadow side for syncing purposes.
    // Start of the changed range in shadow text since last sync.
    private int mShadowStart = Integer.MAX_VALUE;
    // End of the changed range (before the change) in shadow text since last sync.
    private int mShadowOldEnd;
    // End of the changed range (after the change) in shadow text since last sync.
    private int mShadowNewEnd;

    private void addCurrentChangeLocked(final int start, final int oldEnd, final int newEnd) {
      // Merge the new change into any existing change.
      mCurrentStart = Math.min(mCurrentStart, start);
      mCurrentOldEnd += Math.max(0, oldEnd - mCurrentNewEnd);
      mCurrentNewEnd = newEnd + Math.max(0, mCurrentNewEnd - oldEnd);
    }

    public synchronized void currentReplace(
        final int start, final int end, final CharSequence newText) {
      // On Gecko or binder thread.
      mCurrentText.replace(start, end, newText);
      addCurrentChangeLocked(start, end, start + newText.length());
    }

    public synchronized void currentSetSelection(final int start, final int end) {
      // On Gecko or binder thread.
      Selection.setSelection(mCurrentText, start, end);
      mCurrentSelectionChanged = true;
    }

    public synchronized void currentSetSpan(
        final Object obj, final int start, final int end, final int flags) {
      // On Gecko or binder thread.
      mCurrentText.setSpan(obj, start, end, flags);
      addCurrentChangeLocked(start, end, end);
    }

    public synchronized void currentRemoveSpan(final Object obj) {
      // On Gecko or binder thread.
      if (obj == null) {
        mCurrentText.clearSpans();
        addCurrentChangeLocked(0, mCurrentText.length(), mCurrentText.length());
        return;
      }
      final int start = mCurrentText.getSpanStart(obj);
      final int end = mCurrentText.getSpanEnd(obj);
      if (start < 0 || end < 0) {
        return;
      }
      mCurrentText.removeSpan(obj);
      addCurrentChangeLocked(start, end, end);
    }

    // Return Spanned instead of Editable because the returned object is supposed to
    // be read-only. Editing should be done through one of the current*** methods.
    public Spanned getCurrentText() {
      // On Gecko or binder thread.
      return mCurrentText;
    }

    private void addShadowChange(final int start, final int oldEnd, final int newEnd) {
      // Merge the new change into any existing change.
      mShadowStart = Math.min(mShadowStart, start);
      mShadowOldEnd += Math.max(0, oldEnd - mShadowNewEnd);
      mShadowNewEnd = newEnd + Math.max(0, mShadowNewEnd - oldEnd);
    }

    public void shadowReplace(final int start, final int end, final CharSequence newText) {
      if (DEBUG) {
        assertOnIcThread();
      }
      mShadowText.replace(start, end, newText);
      addShadowChange(start, end, start + newText.length());
    }

    public void shadowSetSpan(final Object obj, final int start, final int end, final int flags) {
      if (DEBUG) {
        assertOnIcThread();
      }
      mShadowText.setSpan(obj, start, end, flags);
      addShadowChange(start, end, end);
    }

    public void shadowRemoveSpan(final Object obj) {
      if (DEBUG) {
        assertOnIcThread();
      }
      if (obj == null) {
        mShadowText.clearSpans();
        addShadowChange(0, mShadowText.length(), mShadowText.length());
        return;
      }
      final int start = mShadowText.getSpanStart(obj);
      final int end = mShadowText.getSpanEnd(obj);
      if (start < 0 || end < 0) {
        return;
      }
      mShadowText.removeSpan(obj);
      addShadowChange(start, end, end);
    }

    // Return Spanned instead of Editable because the returned object is supposed to
    // be read-only. Editing should be done through one of the shadow*** methods.
    public Spanned getShadowText() {
      if (DEBUG) {
        assertOnIcThread();
      }
      return mShadowText;
    }

    /**
     * Check whether we are currently discarding the composition. It means that shadow text has
     * composition, but current text has no composition. So syncShadowText will discard composition.
     *
     * @return true if discarding composition
     */
    private boolean isDiscardingComposition() {
      if (!isComposing(mShadowText)) {
        return false;
      }

      return !isComposing(mCurrentText);
    }

    public synchronized void syncShadowText(final SessionTextInput.EditableListener listener) {
      if (DEBUG) {
        assertOnIcThread();
      }

      if (mCurrentStart > mCurrentOldEnd && mShadowStart > mShadowOldEnd) {
        // Still check selection changes.
        if (!mCurrentSelectionChanged) {
          return;
        }
        final int start = Selection.getSelectionStart(mCurrentText);
        final int end = Selection.getSelectionEnd(mCurrentText);
        Selection.setSelection(mShadowText, start, end);
        mCurrentSelectionChanged = false;

        if (listener != null) {
          listener.onSelectionChange();
        }
        return;
      }

      if (isDiscardingComposition()) {
        if (listener != null) {
          listener.onDiscardComposition();
        }
      }

      // Copy the portion of the current text that has changed over to the shadow
      // text, with consideration for any concurrent changes in the shadow text.
      final int start = Math.min(mShadowStart, mCurrentStart);
      final int shadowEnd = mShadowNewEnd + Math.max(0, mCurrentOldEnd - mShadowOldEnd);
      final int currentEnd = mCurrentNewEnd + Math.max(0, mShadowOldEnd - mCurrentOldEnd);

      // Remove existing spans that may no longer be in the new text.
      Object[] spans = mShadowText.getSpans(start, shadowEnd, Object.class);
      for (final Object span : spans) {
        mShadowText.removeSpan(span);
      }

      mShadowText.replace(start, shadowEnd, mCurrentText, start, currentEnd);

      // The replace() call may not have copied all affected spans, so we re-copy all the
      // spans manually just in case. Expand bounds by 1 so we get all the spans.
      spans =
          mCurrentText.getSpans(
              Math.max(start - 1, 0),
              Math.min(currentEnd + 1, mCurrentText.length()),
              Object.class);
      for (final Object span : spans) {
        if (span == Selection.SELECTION_START || span == Selection.SELECTION_END) {
          continue;
        }
        mShadowText.setSpan(
            span,
            mCurrentText.getSpanStart(span),
            mCurrentText.getSpanEnd(span),
            mCurrentText.getSpanFlags(span));
      }

      // SpannableStringBuilder has some internal logic to fix up selections, but we
      // don't want that, so we always fix up the selection a second time.
      final int selStart = Selection.getSelectionStart(mCurrentText);
      final int selEnd = Selection.getSelectionEnd(mCurrentText);
      Selection.setSelection(mShadowText, selStart, selEnd);

      if (DEBUG && !checkEqualText(mShadowText, mCurrentText)) {
        // Sanity check.
        throw new IllegalStateException(
            "Failed to sync: "
                + mShadowStart
                + '-'
                + mShadowOldEnd
                + '-'
                + mShadowNewEnd
                + '/'
                + mCurrentStart
                + '-'
                + mCurrentOldEnd
                + '-'
                + mCurrentNewEnd);
      }

      if (listener != null) {
        // Call onTextChange after selection fix-up but before we call
        // onSelectionChange.
        listener.onTextChange();

        if (mCurrentSelectionChanged
            || (mCurrentOldEnd != mCurrentNewEnd
                && (selStart >= mCurrentStart || selEnd >= mCurrentStart))) {
          listener.onSelectionChange();
        }
      }

      // These values ensure the first change is properly added.
      mCurrentStart = mShadowStart = Integer.MAX_VALUE;
      mCurrentOldEnd = mShadowOldEnd = 0;
      mCurrentNewEnd = mShadowNewEnd = 0;
      mCurrentSelectionChanged = false;
    }
  }

  private static boolean checkEqualText(final Spanned s1, final Spanned s2) {
    if (!s1.toString().equals(s2.toString())) {
      return false;
    }

    final Object[] o1s = s1.getSpans(0, s1.length(), Object.class);
    final Object[] o2s = s2.getSpans(0, s2.length(), Object.class);

    if (o1s.length != o2s.length) {
      return false;
    }

    o1loop:
    for (final Object o1 : o1s) {
      for (final Object o2 : o2s) {
        if (o1 != o2) {
          continue;
        }
        if (s1.getSpanStart(o1) != s2.getSpanStart(o2)
            || s1.getSpanEnd(o1) != s2.getSpanEnd(o2)
            || s1.getSpanFlags(o1) != s2.getSpanFlags(o2)) {
          return false;
        }
        continue o1loop;
      }
      // o1 not found in o2s.
      return false;
    }
    return true;
  }

  /* An action that alters the Editable

     Each action corresponds to a Gecko event. While the Gecko event is being sent to the Gecko
     thread, the action stays on top of mActions queue. After the Gecko event is processed and
     replied, the action is removed from the queue
  */
  private static final class Action {
    // For input events (keypress, etc.); use with onImeSynchronize
    static final int TYPE_EVENT = 0;
    // For Editable.replace() call; use with onImeReplaceText
    static final int TYPE_REPLACE_TEXT = 1;
    // For Editable.setSpan() call; use with onImeSynchronize
    static final int TYPE_SET_SPAN = 2;
    // For Editable.removeSpan() call; use with onImeSynchronize
    static final int TYPE_REMOVE_SPAN = 3;
    // For switching handler; use with onImeSynchronize
    static final int TYPE_SET_HANDLER = 4;

    final int mType;
    int mStart;
    int mEnd;
    CharSequence mSequence;
    Object mSpanObject;
    int mSpanFlags;
    Handler mHandler;

    Action(final int type) {
      mType = type;
    }

    @Override
    public String toString() {
      final StringBuilder sb = new StringBuilder("Action(mType=");
      sb.append(getConstantName(Action.class, "TYPE_", mType));
      switch (mType) {
        case TYPE_SET_SPAN:
          sb.append(", mSpanObject=")
              .append(mSpanObject)
              .append(", mStart=")
              .append(mStart)
              .append(", mEnd=")
              .append(mEnd)
              .append(", mSpanFlags=")
              .append(mSpanFlags);
          break;
        case TYPE_REMOVE_SPAN:
          sb.append(", mSpanObject=")
              .append(mSpanObject)
              .append(", mSpanFlags=")
              .append(mSpanFlags);
          break;
        case TYPE_REPLACE_TEXT:
          sb.append(", mStart=")
              .append(mStart)
              .append(", mEnd=")
              .append(mEnd)
              .append(", mSequence=\"")
              .append(mSequence)
              .append("\"");
          break;
        default:
          break;
      }
      sb.append(")");
      return sb.toString();
    }

    static Action newReplaceText(final CharSequence text, final int start, final int end) {
      if (start < 0 || start > end) {
        Log.e(LOGTAG, "invalid replace text offsets: " + start + " to " + end);
        throw new IllegalArgumentException("invalid replace text offsets");
      }

      final Action action = new Action(TYPE_REPLACE_TEXT);
      action.mSequence = text;
      action.mStart = start;
      action.mEnd = end;
      return action;
    }

    static Action newSetSpan(final Object object, final int start, final int end, final int flags) {
      if (start < 0 || start > end) {
        Log.e(LOGTAG, "invalid span offsets: " + start + " to " + end);
        throw new IllegalArgumentException("invalid span offsets");
      }
      final Action action = new Action(TYPE_SET_SPAN);
      action.mSpanObject = object;
      action.mStart = start;
      action.mEnd = end;
      action.mSpanFlags = flags;
      return action;
    }

    static Action newRemoveSpan(final Object object) {
      final Action action = new Action(TYPE_REMOVE_SPAN);
      action.mSpanObject = object;
      return action;
    }

    static Action newSetHandler(final Handler handler) {
      final Action action = new Action(TYPE_SET_HANDLER);
      action.mHandler = handler;
      return action;
    }
  }

  private void icOfferAction(final Action action) {
    if (DEBUG) {
      assertOnIcThread();
    }
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("offer: action=");
      sb.append(action.toString());
      MozLog.d(MOZLOGTAG, sb.toString());
    }

    switch (action.mType) {
      case Action.TYPE_EVENT:
      case Action.TYPE_SET_HANDLER:
        break;

      case Action.TYPE_SET_SPAN:
        mText.shadowSetSpan(
            action.mSpanObject, action.mStart,
            action.mEnd, action.mSpanFlags);
        break;

      case Action.TYPE_REMOVE_SPAN:
        action.mSpanFlags = mText.getShadowText().getSpanFlags(action.mSpanObject);
        mText.shadowRemoveSpan(action.mSpanObject);
        break;

      case Action.TYPE_REPLACE_TEXT:
        mText.shadowReplace(action.mStart, action.mEnd, action.mSequence);
        break;

      default:
        throw new IllegalStateException("Action not processed");
    }

    // Always perform actions on the shadow text side above, so we still act as a
    // valid Editable object, but don't send the actions to Gecko below if we haven't
    // been focused or initialized, or we've been destroyed.
    if (mFocusedChild == null || mListener == null) {
      return;
    }

    mActions.offer(action);

    try {
      icPerformAction(action);
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Remote call failed", e);
      // Undo the offer.
      mActions.remove(action);
    }
  }

  private void icPerformAction(final Action action) throws RemoteException {
    switch (action.mType) {
      case Action.TYPE_EVENT:
      case Action.TYPE_SET_HANDLER:
        mFocusedChild.onImeSynchronize();
        break;

      case Action.TYPE_SET_SPAN:
        {
          final boolean isAddingComposition = (action.mSpanFlags & Spanned.SPAN_COMPOSING) != 0;
          final boolean needUpdate =
              (action.mSpanFlags & Spanned.SPAN_INTERMEDIATE) == 0
                  && (isAddingComposition
                      || action.mSpanObject == Selection.SELECTION_START
                      || action.mSpanObject == Selection.SELECTION_END);

          action.mSequence = TextUtils.substring(mText.getShadowText(), action.mStart, action.mEnd);

          mNeedUpdateComposition |= needUpdate;
          if (needUpdate) {
            if (mInBatchMode && isAddingComposition) {
              // In batch mode, we defer composition updates until the end of the batch.
              if (LOGGING) {
                MozLog.d(
                    MOZLOGTAG,
                    "Defer composition update due to batch mode: action=" + action.toString());
              }
            } else {
              icMaybeSendComposition(
                  mText.getShadowText(),
                  SEND_COMPOSITION_NOTIFY_GECKO | SEND_COMPOSITION_KEEP_CURRENT);
            }
          }

          mFocusedChild.onImeSynchronize();
          break;
        }
      case Action.TYPE_REMOVE_SPAN:
        {
          final boolean isRemovingComposition = (action.mSpanFlags & Spanned.SPAN_COMPOSING) != 0;
          final boolean needUpdate =
              (action.mSpanFlags & Spanned.SPAN_INTERMEDIATE) == 0 && isRemovingComposition;

          mNeedUpdateComposition |= needUpdate;
          if (needUpdate) {
            if (mInBatchMode && isRemovingComposition) {
              // In batch mode, we defer composition updates until the end of the batch.
              if (LOGGING) {
                MozLog.d(
                    MOZLOGTAG,
                    "Defer composition update due to batch mode: action=" + action.toString());
              }
            } else {
              icMaybeSendComposition(
                  mText.getShadowText(),
                  SEND_COMPOSITION_NOTIFY_GECKO | SEND_COMPOSITION_KEEP_CURRENT);
            }
          }

          mFocusedChild.onImeSynchronize();
          break;
        }
      case Action.TYPE_REPLACE_TEXT:
        // Always sync text after a replace action, so that if the Gecko
        // text is not changed, we will revert the shadow text to before.
        mNeedSync = true;

        // Because we get composition styling here essentially for free,
        // we don't need to check if we're in batch mode.
        if (icMaybeSendComposition(action.mSequence, SEND_COMPOSITION_USE_ENTIRE_TEXT)) {
          mFocusedChild.onImeReplaceText(action.mStart, action.mEnd, action.mSequence.toString());
          break;
        }

        // Since we don't have a composition, we can try sending key events.
        sendCharKeyEvents(action);

        // onImeReplaceText will set the selection range. But we don't
        // know whether event state manager is processing text and
        // selection. So current shadow may not be synchronized with
        // Gecko's text and selection. So we have to avoid unnecessary
        // selection update.
        final int selStartOnShadow = Selection.getSelectionStart(mText.getShadowText());
        final int selEndOnShadow = Selection.getSelectionEnd(mText.getShadowText());
        int actionStart = action.mStart;
        int actionEnd = action.mEnd;
        // If action range is collapsed and selection of shadow text is
        // collapsed, we may try to dispatch keypress on current caret
        // position. Action range is previous range before dispatching
        // keypress, and shadow range is new range after dispatching
        // it.
        if (action.mStart == action.mEnd
            && selStartOnShadow == selEndOnShadow
            && action.mStart == selStartOnShadow + action.mSequence.toString().length()) {
          // Replacing range is same value as current shadow's selection.
          // So it is unnecessary to update the selection on Gecko.
          actionStart = -1;
          actionEnd = -1;
        }
        mFocusedChild.onImeReplaceText(actionStart, actionEnd, action.mSequence.toString());
        break;

      default:
        throw new IllegalStateException("Action not processed");
    }
  }

  private KeyEvent[] synthesizeKeyEvents(final CharSequence cs) {
    try {
      if (mKeyMap == null) {
        mKeyMap = KeyCharacterMap.load(KeyCharacterMap.VIRTUAL_KEYBOARD);
      }
    } catch (final Exception e) {
      // KeyCharacterMap.UnavailableException is not found on Gingerbread;
      // besides, it seems like HC and ICS will throw something other than
      // KeyCharacterMap.UnavailableException; so use a generic Exception here
      return null;
    }
    final KeyEvent[] keyEvents = mKeyMap.getEvents(cs.toString().toCharArray());
    if (keyEvents == null || keyEvents.length == 0) {
      return null;
    }
    return keyEvents;
  }

  private void sendCharKeyEvents(final Action action) throws RemoteException {
    if (action.mSequence.length() != 1
        || (action.mSequence instanceof Spannable
            && ((Spannable) action.mSequence).nextSpanTransition(-1, Integer.MAX_VALUE, null)
                < Integer.MAX_VALUE)) {
      // Spans are not preserved when we use key events,
      // so we need the sequence to not have any spans
      return;
    }
    final KeyEvent[] keyEvents = synthesizeKeyEvents(action.mSequence);
    if (keyEvents == null) {
      return;
    }
    for (final KeyEvent event : keyEvents) {
      if (KeyEvent.isModifierKey(event.getKeyCode())) {
        continue;
      }
      if (event.getAction() == KeyEvent.ACTION_UP && mSuppressKeyUp) {
        continue;
      }
      if (DEBUG) {
        Log.d(LOGTAG, "sending: " + event);
      }
      onKeyEvent(
          mFocusedChild,
          event,
          event.getAction(),
          /* metaState */ 0, /* isSynthesizedImeKey */
          true);
    }
  }

  public GeckoEditable(@NonNull final GeckoSession session) {
    if (DEBUG) {
      // Called by SessionTextInput.
      ThreadUtils.assertOnUiThread();
    }

    mSession = new WeakReference<>(session);
    mText = new AsyncText();
    mActions = new ConcurrentLinkedQueue<Action>();

    final Class<?>[] PROXY_INTERFACES = {Editable.class};
    mProxy =
        (Editable) Proxy.newProxyInstance(Editable.class.getClassLoader(), PROXY_INTERFACES, this);

    mIcRunHandler = mIcPostHandler = ThreadUtils.getUiHandler();
  }

  @Override // IGeckoEditableParent
  public void setDefaultChild(final IGeckoEditableChild child) {
    if (DEBUG) {
      // On Gecko or binder thread.
      Log.d(LOGTAG, "setDefaultEditableChild " + child);
    }
    mDefaultChild = child;
  }

  public void setListener(final SessionTextInput.EditableListener newListener) {
    if (DEBUG) {
      // Called by SessionTextInput.
      ThreadUtils.assertOnUiThread();
      Log.d(LOGTAG, "setListener " + newListener);
    }

    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (DEBUG) {
              Log.d(LOGTAG, "onViewChange (set listener)");
            }

            mListener = newListener;
          }
        });
  }

  private boolean onIcThread() {
    return mIcRunHandler.getLooper() == Looper.myLooper();
  }

  private void assertOnIcThread() {
    ThreadUtils.assertOnThread(mIcRunHandler.getLooper().getThread(), AssertBehavior.THROW);
  }

  private Object getField(final Object obj, final String field, final Object def) {
    try {
      return obj.getClass().getField(field).get(obj);
    } catch (final Exception e) {
      return def;
    }
  }

  // Flags for icMaybeSendComposition
  @Retention(RetentionPolicy.SOURCE)
  @IntDef(
      flag = true,
      value = {
        SEND_COMPOSITION_USE_ENTIRE_TEXT,
        SEND_COMPOSITION_NOTIFY_GECKO,
        SEND_COMPOSITION_KEEP_CURRENT
      })
  public @interface CompositionFlags {}

  // If text has composing spans, treat the entire text as a Gecko composition,
  // instead of just the spanned part.
  private static final int SEND_COMPOSITION_USE_ENTIRE_TEXT = 1 << 0;
  // Notify Gecko of the new composition ranges;
  // otherwise, the caller is responsible for notifying Gecko.
  private static final int SEND_COMPOSITION_NOTIFY_GECKO = 1 << 1;
  // Keep the current composition when updating;
  // composition is not updated if there is no current composition.
  private static final int SEND_COMPOSITION_KEEP_CURRENT = 1 << 2;

  /**
   * Send composition ranges to Gecko if the text has composing spans.
   *
   * @param sequence Text with possible composing spans
   * @param flags Bitmask of SEND_COMPOSITION_* flags for updating composition.
   * @return Whether there was a composition
   */
  private boolean icMaybeSendComposition(
      final CharSequence sequence, @CompositionFlags final int flags) throws RemoteException {
    final boolean useEntireText = (flags & SEND_COMPOSITION_USE_ENTIRE_TEXT) != 0;
    final boolean notifyGecko = (flags & SEND_COMPOSITION_NOTIFY_GECKO) != 0;
    final boolean keepCurrent = (flags & SEND_COMPOSITION_KEEP_CURRENT) != 0;
    final int updateFlags = keepCurrent ? GeckoEditableChild.FLAG_KEEP_CURRENT_COMPOSITION : 0;

    if (!keepCurrent) {
      // If keepCurrent is true, the composition may not actually be updated;
      // so we may still need to update the composition in the future.
      mNeedUpdateComposition = false;
    }

    int selStart = Selection.getSelectionStart(sequence);
    int selEnd = Selection.getSelectionEnd(sequence);

    if (sequence instanceof Spanned) {
      final Spanned text = (Spanned) sequence;
      final Object[] spans = text.getSpans(0, text.length(), Object.class);
      boolean found = false;
      int composingStart = useEntireText ? 0 : Integer.MAX_VALUE;
      int composingEnd = useEntireText ? text.length() : 0;

      // Find existence and range of any composing spans (spans with the
      // SPAN_COMPOSING flag set).
      for (final Object span : spans) {
        if ((text.getSpanFlags(span) & Spanned.SPAN_COMPOSING) == 0) {
          continue;
        }
        found = true;
        if (useEntireText) {
          break;
        }
        composingStart = Math.min(composingStart, text.getSpanStart(span));
        composingEnd = Math.max(composingEnd, text.getSpanEnd(span));
      }

      if (useEntireText && (selStart < 0 || selEnd < 0)) {
        selStart = composingEnd;
        selEnd = composingEnd;
      }

      if (found) {
        if (selStart < composingStart || selEnd > composingEnd) {
          // GBoard will set caret position that is out of composing
          // range. Unfortunately, Gecko doesn't support this caret
          // position. So we shouldn't set composing range data now.
          // But this is temporary composing range, then GBoard will
          // set valid range soon.
          if (DEBUG) {
            final StringBuilder sb =
                new StringBuilder("icSendComposition(): invalid caret position. ");
            sb.append("composing = ")
                .append(composingStart)
                .append("-")
                .append(composingEnd)
                .append(", selection = ")
                .append(selStart)
                .append("-")
                .append(selEnd);
            Log.d(LOGTAG, sb.toString());
          }
        } else {
          icSendComposition(text, selStart, selEnd, composingStart, composingEnd);
          if (notifyGecko) {
            mFocusedChild.onImeUpdateComposition(composingStart, composingEnd, updateFlags);
          }
          return true;
        }
      }
    }

    if (notifyGecko) {
      // Set the selection by using a composition without ranges.
      mFocusedChild.onImeUpdateComposition(selStart, selEnd, updateFlags);
    }

    if (DEBUG) {
      Log.d(LOGTAG, "icSendComposition(): no composition");
    }
    return false;
  }

  private void icSendComposition(
      final Spanned text,
      final int selStart,
      final int selEnd,
      final int composingStart,
      final int composingEnd)
      throws RemoteException {
    if (DEBUG) {
      assertOnIcThread();
    }
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("icSendComposition(");
      debugAppend(sb, text)
          .append(", range = ")
          .append(composingStart)
          .append("-")
          .append(composingEnd)
          .append(", selection = ")
          .append(selStart)
          .append("-")
          .append(selEnd)
          .append(")");
      MozLog.d(MOZLOGTAG, sb.toString());
    }

    if (selEnd >= composingStart && selEnd <= composingEnd) {
      mFocusedChild.onImeAddCompositionRange(
          selEnd - composingStart,
          selEnd - composingStart,
          IME_RANGE_CARETPOSITION,
          0,
          0,
          false,
          0,
          0,
          0);
    }

    int rangeStart = composingStart;
    final TextPaint tp = new TextPaint();
    final TextPaint emptyTp = new TextPaint();
    // set initial foreground color to 0, because we check for tp.getColor() == 0
    // below to decide whether to pass a foreground color to Gecko
    emptyTp.setColor(0);
    do {
      final int rangeType;
      int rangeStyles = 0;
      int rangeLineStyle = IME_RANGE_LINE_NONE;
      boolean rangeBoldLine = false;
      int rangeForeColor = 0, rangeBackColor = 0, rangeLineColor = 0;
      int rangeEnd = text.nextSpanTransition(rangeStart, composingEnd, Object.class);

      if (selStart > rangeStart && selStart < rangeEnd) {
        rangeEnd = selStart;
      } else if (selEnd > rangeStart && selEnd < rangeEnd) {
        rangeEnd = selEnd;
      }
      final CharacterStyle[] styleSpans = text.getSpans(rangeStart, rangeEnd, CharacterStyle.class);

      if (LOGGING) {
        final StringBuilder sb = new StringBuilder(" found ");
        sb.append(styleSpans.length)
            .append(" spans @ ")
            .append(rangeStart)
            .append("-")
            .append(rangeEnd);
        MozLog.d(MOZLOGTAG, sb.toString());
      }

      if (styleSpans.length == 0) {
        rangeType =
            (selStart == rangeStart && selEnd == rangeEnd)
                ? IME_RANGE_SELECTEDRAWTEXT
                : IME_RANGE_RAWINPUT;
      } else {
        rangeType =
            (selStart == rangeStart && selEnd == rangeEnd)
                ? IME_RANGE_SELECTEDCONVERTEDTEXT
                : IME_RANGE_CONVERTEDTEXT;
        tp.set(emptyTp);
        for (final CharacterStyle span : styleSpans) {
          span.updateDrawState(tp);
        }

        final int tpUnderlineColor;
        final float tpUnderlineThickness;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
          tpUnderlineColor = tp.underlineColor;
          tpUnderlineThickness = tp.underlineThickness;
        } else {
          // These TextPaint fields only exist on Android ICS+ and are not in the SDK.
          tpUnderlineColor = (Integer) getField(tp, "underlineColor", 0);
          tpUnderlineThickness = (Float) getField(tp, "underlineThickness", 0.0f);
        }
        if (tpUnderlineColor != 0) {
          rangeStyles |= IME_RANGE_UNDERLINE | IME_RANGE_LINECOLOR;
          rangeLineColor = tpUnderlineColor;
          // Approximately translate underline thickness to what Gecko understands
          if (tpUnderlineThickness <= 0.5f) {
            rangeLineStyle = IME_RANGE_LINE_DOTTED;
          } else {
            rangeLineStyle = IME_RANGE_LINE_SOLID;
            if (tpUnderlineThickness >= 2.0f) {
              rangeBoldLine = true;
            }
          }
        } else if (tp.isUnderlineText()) {
          rangeStyles |= IME_RANGE_UNDERLINE;
          rangeLineStyle = IME_RANGE_LINE_SOLID;
        }
        if (tp.getColor() != 0) {
          rangeStyles |= IME_RANGE_FORECOLOR;
          rangeForeColor = tp.getColor();
        }
        if (tp.bgColor != 0) {
          rangeStyles |= IME_RANGE_BACKCOLOR;
          rangeBackColor = tp.bgColor;
        }
      }
      mFocusedChild.onImeAddCompositionRange(
          rangeStart - composingStart,
          rangeEnd - composingStart,
          rangeType,
          rangeStyles,
          rangeLineStyle,
          rangeBoldLine,
          rangeForeColor,
          rangeBackColor,
          rangeLineColor);
      rangeStart = rangeEnd;

      if (LOGGING) {
        final StringBuilder sb = new StringBuilder(" added ");
        sb.append(rangeType)
            .append(" : ")
            .append(Integer.toHexString(rangeStyles))
            .append(" : ")
            .append(Integer.toHexString(rangeForeColor))
            .append(" : ")
            .append(Integer.toHexString(rangeBackColor));
        MozLog.d(MOZLOGTAG, sb.toString());
      }
    } while (rangeStart < composingEnd);
  }

  @Override // SessionTextInput.EditableClient
  public void sendKeyEvent(
      final @Nullable View view, final int action, final @NonNull KeyEvent event) {
    final Editable editable = mProxy;
    final KeyListener keyListener = TextKeyListener.getInstance();
    final KeyEvent translatedEvent = translateKey(event.getKeyCode(), event);

    // We only let TextKeyListener do UI things on the UI thread.
    final View v = ThreadUtils.isOnUiThread() ? view : null;
    final int keyCode = translatedEvent.getKeyCode();
    final boolean handled;

    if (shouldSkipKeyListener(keyCode, translatedEvent)) {
      handled = false;
    } else if (action == KeyEvent.ACTION_DOWN) {
      setSuppressKeyUp(true);
      handled = keyListener.onKeyDown(v, editable, keyCode, translatedEvent);
    } else if (action == KeyEvent.ACTION_UP) {
      handled = keyListener.onKeyUp(v, editable, keyCode, translatedEvent);
    } else {
      handled = keyListener.onKeyOther(v, editable, translatedEvent);
    }

    if (!handled) {
      sendKeyEvent(translatedEvent, action, TextKeyListener.getMetaState(editable));
    }

    if (action == KeyEvent.ACTION_DOWN) {
      if (!handled) {
        // Usually, the down key listener call above adjusts meta states for us.
        // However, if the call didn't handle the event, we have to manually
        // adjust meta states so the meta states remain consistent.
        TextKeyListener.adjustMetaAfterKeypress(editable);
      }
      setSuppressKeyUp(false);
    }
  }

  private void sendKeyEvent(final @NonNull KeyEvent event, final int action, final int metaState) {
    if (DEBUG) {
      assertOnIcThread();
    }
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("sendKeyEvent(");
      sb.append(event).append(", ").append(action).append(", ").append(metaState).append(")");
      MozLog.d(MOZLOGTAG, sb.toString());
    }
    /*
       We are actually sending two events to Gecko here,
       1. Event from the event parameter (key event)
       2. Sync event from the icOfferAction call
       The first event is a normal event that does not reply back to us,
       the second sync event will have a reply, during which we see that there is a pending
       event-type action, and update the shadow text accordingly.
    */
    try {
      if (mFocusedChild == null) {
        if (mDefaultChild == null) {
          Log.w(LOGTAG, "Discarding key event");
          return;
        }
        // Not focused; send simple key event to chrome window.
        onKeyEvent(mDefaultChild, event, action, metaState, /* isSynthesizedImeKey */ false);
        return;
      }

      // Most IMEs handle arrow key, then set caret position. But GBoard
      // doesn't handle it. GBoard will dispatch KeyEvent for arrow left/right
      // even if having IME composition.
      // Since Gecko doesn't dispatch keypress during IME composition due to
      // DOM UI events spec, we have to emulate arrow key's behaviour.
      boolean commitCompositionBeforeKeyEvent = action == KeyEvent.ACTION_DOWN;
      if (isComposing(mText.getShadowText())
          && action == KeyEvent.ACTION_DOWN
          && event.hasNoModifiers()) {
        final int selStart = Selection.getSelectionStart(mText.getShadowText());
        final int selEnd = Selection.getSelectionEnd(mText.getShadowText());
        if (selStart == selEnd) {
          // If dispatching arrow left/right key into composition,
          // we update IME caret.
          switch (event.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_LEFT:
              if (getComposingStart(mText.getShadowText()) < selStart) {
                Selection.setSelection(getEditable(), selStart - 1, selStart - 1);
                mNeedUpdateComposition = true;
                commitCompositionBeforeKeyEvent = false;
              } else if (selStart == 0) {
                // Keep current composition
                commitCompositionBeforeKeyEvent = false;
              }
              break;
            case KeyEvent.KEYCODE_DPAD_RIGHT:
              if (getComposingEnd(mText.getShadowText()) > selEnd) {
                Selection.setSelection(getEditable(), selStart + 1, selStart + 1);
                mNeedUpdateComposition = true;
                commitCompositionBeforeKeyEvent = false;
              } else if (selEnd == mText.getShadowText().length()) {
                // Keep current composition
                commitCompositionBeforeKeyEvent = false;
              }
              break;
          }
        }
      }

      // Focused; key event may go to chrome window or to content window.
      if (mNeedUpdateComposition) {
        icMaybeSendComposition(mText.getShadowText(), SEND_COMPOSITION_NOTIFY_GECKO);
      }

      if (commitCompositionBeforeKeyEvent) {
        mFocusedChild.onImeRequestCommit();
      }
      onKeyEvent(mFocusedChild, event, action, metaState, /* isSynthesizedImeKey */ false);
      icOfferAction(new Action(Action.TYPE_EVENT));
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Remote call failed", e);
    }
  }

  private boolean shouldSkipKeyListener(final int keyCode, final @NonNull KeyEvent event) {
    if (mIMEState == SessionTextInput.EditableListener.IME_STATE_DISABLED) {
      return true;
    }

    // Preserve enter and tab keys for the browser
    if (keyCode == KeyEvent.KEYCODE_ENTER
        || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER
        || keyCode == KeyEvent.KEYCODE_TAB) {
      return true;
    }
    // BaseKeyListener returns false even if it handled these keys for us,
    // so we skip the key listener entirely and handle these ourselves
    return keyCode == KeyEvent.KEYCODE_DEL || keyCode == KeyEvent.KEYCODE_FORWARD_DEL;
  }

  private static KeyEvent translateSonyXperiaGamepadKeys(final int keyCode, final KeyEvent event) {
    // The cross and circle button mappings may be swapped in the different regions so
    // determine if they are swapped so the proper key codes can be mapped to the keys
    final boolean areKeysSwapped = areSonyXperiaGamepadKeysSwapped();

    int translatedKeyCode = keyCode;
    // If a Sony Xperia, remap the cross and circle buttons to buttons
    // A and B for the gamepad API
    switch (keyCode) {
      case KeyEvent.KEYCODE_BACK:
        translatedKeyCode =
            (areKeysSwapped ? KeyEvent.KEYCODE_BUTTON_A : KeyEvent.KEYCODE_BUTTON_B);
        break;

      case KeyEvent.KEYCODE_DPAD_CENTER:
        translatedKeyCode =
            (areKeysSwapped ? KeyEvent.KEYCODE_BUTTON_B : KeyEvent.KEYCODE_BUTTON_A);
        break;

      default:
        return event;
    }

    return new KeyEvent(event.getAction(), translatedKeyCode);
  }

  private static final int SONY_XPERIA_GAMEPAD_DEVICE_ID = 196611;

  private static boolean isSonyXperiaGamepadKeyEvent(final KeyEvent event) {
    return (event.getDeviceId() == SONY_XPERIA_GAMEPAD_DEVICE_ID
        && "Sony Ericsson".equals(Build.MANUFACTURER)
        && ("R800".equals(Build.MODEL) || "R800i".equals(Build.MODEL)));
  }

  private static boolean areSonyXperiaGamepadKeysSwapped() {
    // The cross and circle buttons on Sony Xperia phones are swapped
    // in different regions
    // http://developer.sonymobile.com/2011/02/13/xperia-play-game-keys/
    final char DEFAULT_O_BUTTON_LABEL = 0x25CB;

    boolean swapped = false;
    final int[] deviceIds = InputDevice.getDeviceIds();

    for (int i = 0; deviceIds != null && i < deviceIds.length; i++) {
      final KeyCharacterMap keyCharacterMap = KeyCharacterMap.load(deviceIds[i]);
      if (keyCharacterMap != null
          && DEFAULT_O_BUTTON_LABEL
              == keyCharacterMap.getDisplayLabel(KeyEvent.KEYCODE_DPAD_CENTER)) {
        swapped = true;
        break;
      }
    }
    return swapped;
  }

  private KeyEvent translateKey(final int keyCode, final @NonNull KeyEvent event) {
    if (isSonyXperiaGamepadKeyEvent(event)) {
      return translateSonyXperiaGamepadKeys(keyCode, event);
    }
    return event;
  }

  @Override // SessionTextInput.EditableClient
  public Editable getEditable() {
    if (!onIcThread()) {
      // Android may be holding an old InputConnection; ignore
      if (DEBUG) {
        Log.i(LOGTAG, "getEditable() called on non-IC thread");
      }
      return null;
    }
    if (mListener == null) {
      // We haven't initialized or we've been destroyed.
      return null;
    }
    return mProxy;
  }

  @Override // SessionTextInput.EditableClient
  public void setBatchMode(final boolean inBatchMode) {
    if (!onIcThread()) {
      // Android may be holding an old InputConnection; ignore
      if (DEBUG) {
        Log.i(LOGTAG, "setBatchMode() called on non-IC thread");
      }
      return;
    }

    boolean syncShadowText = !inBatchMode && mNeedSync;

    if (mInBatchMode != inBatchMode) {
      mInBatchMode = inBatchMode;

      final boolean hasComposition = isComposing(mText.getShadowText());

      if (inBatchMode) {
        mHasCompositionOnBeginningBatchMode = hasComposition;
      } else {
        // Exiting batch mode. If composing state is changed, or we have a composition, send it to
        // Gecko.
        if (mNeedUpdateComposition && mFocusedChild != null) {
          try {
            if (mHasCompositionOnBeginningBatchMode || hasComposition) {
              icMaybeSendComposition(
                  mText.getShadowText(),
                  SEND_COMPOSITION_NOTIFY_GECKO | SEND_COMPOSITION_KEEP_CURRENT);
            }
            // Gecko won't notify us of text changes if composing state is changed only. So we
            // shouldn't call onImeSynchronize in this case.
            // Also, during batch mode, if text is changed, we already send it to Gecko, so we
            // cannot sync shadow text yet until Gecko notifies us of the text/selection changes.
            syncShadowText = false;
          } catch (final RemoteException e) {
            Log.e(LOGTAG, "Remote call failed", e);
          }
        }
      }
    }

    if (!inBatchMode && mFocusedChild != null) {
      // We may not commit composition on Gecko even if Java side has
      // no composition. So we have to sync composition state with Gecko
      // when batch edit is done.
      //
      // i.e. Although finishComposingText removes composing span, we
      // don't commit current composition yet.
      final Editable editable = getEditable();
      if (editable != null && !isComposing(editable)) {
        try {
          mFocusedChild.onImeRequestCommit();
        } catch (final RemoteException e) {
          Log.e(LOGTAG, "Remote call failed", e);
        }
      }
      // Committing composition doesn't change text, so we can sync shadow text.
    }

    if (syncShadowText) {
      icSyncShadowText();
    }
  }

  /* package */ void icSyncShadowText() {
    if (mListener == null) {
      // Not yet attached or already destroyed.
      return;
    }

    if (mInBatchMode || !mActions.isEmpty()) {
      mNeedSync = true;
      return;
    }

    mNeedSync = false;
    mText.syncShadowText(mListener);
  }

  private void setSuppressKeyUp(final boolean suppress) {
    if (DEBUG) {
      assertOnIcThread();
    }
    // Suppress key up event generated as a result of
    // translating characters to key events
    mSuppressKeyUp = suppress;
  }

  @Override // SessionTextInput.EditableClient
  public Handler setInputConnectionHandler(final Handler handler) {
    if (handler == mIcRunHandler) {
      return mIcRunHandler;
    }
    if (DEBUG) {
      assertOnIcThread();
    }

    // There are three threads at this point: Gecko thread, old IC thread, and new IC
    // thread, and we want to safely switch from old IC thread to new IC thread.
    // We first send a TYPE_SET_HANDLER action to the Gecko thread; this ensures that
    // the Gecko thread is stopped at a known point. At the same time, the old IC
    // thread blocks on the action; this ensures that the old IC thread is stopped at
    // a known point. Finally, inside the Gecko thread, we post a Runnable to the old
    // IC thread; this Runnable switches from old IC thread to new IC thread. We
    // switch IC thread on the old IC thread to ensure any pending Runnables on the
    // old IC thread are processed before we switch over. Inside the Gecko thread, we
    // also post a Runnable to the new IC thread; this Runnable blocks until the
    // switch is complete; this ensures that the new IC thread won't accept
    // InputConnection calls until after the switch.

    handler.post(
        new Runnable() { // Make the new IC thread wait.
          @Override
          public void run() {
            synchronized (handler) {
              while (mIcRunHandler != handler) {
                try {
                  handler.wait();
                } catch (final InterruptedException e) {
                }
              }
            }
          }
        });

    icOfferAction(Action.newSetHandler(handler));
    return handler;
  }

  @Override // SessionTextInput.EditableClient
  public void postToInputConnection(final Runnable runnable) {
    mIcPostHandler.post(runnable);
  }

  @Override // SessionTextInput.EditableClient
  public void requestCursorUpdates(@CursorMonitorMode final int requestMode) {
    try {
      if (mFocusedChild != null) {
        mFocusedChild.onImeRequestCursorUpdates(requestMode);
      }
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Remote call failed", e);
    }
  }

  @Override // SessionTextInput.EditableClient
  public void insertImage(final @NonNull byte[] data, final @NonNull String mimeType) {
    if (mFocusedChild == null) {
      return;
    }

    try {
      mFocusedChild.onImeInsertImage(data, mimeType);
    } catch (final RemoteException e) {
      Log.e(LOGTAG, "Remote call to insert image failed", e);
    }
  }

  private void geckoSetIcHandler(final Handler newHandler) {
    // On Gecko or binder thread.
    mIcPostHandler.post(
        new Runnable() { // posting to old IC thread
          @Override
          public void run() {
            synchronized (newHandler) {
              mIcRunHandler = newHandler;
              newHandler.notify();
            }
          }
        });

    // At this point, all future Runnables should be posted to the new IC thread, but
    // we don't switch mIcRunHandler yet because there may be pending Runnables on the
    // old IC thread still waiting to run.
    mIcPostHandler = newHandler;
  }

  private void geckoActionReply(final Action action) {
    // On Gecko or binder thread.
    if (action == null) {
      Log.w(LOGTAG, "Mismatched reply");
      return;
    }
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("reply: action=");
      sb.append(action.toString());
      MozLog.d(MOZLOGTAG, sb.toString());
    }
    switch (action.mType) {
      case Action.TYPE_REPLACE_TEXT:
        {
          final Spanned currentText = mText.getCurrentText();
          final int actionNewEnd = action.mStart + action.mSequence.length();
          if (mLastTextChangeStart > mLastTextChangeNewEnd
              || mLastTextChangeNewEnd > currentText.length()
              || action.mStart < mLastTextChangeStart
              || actionNewEnd > mLastTextChangeNewEnd) {
            // Replace-text action doesn't match our text change.
            break;
          }

          int indexInText =
              TextUtils.indexOf(
                  currentText, action.mSequence, action.mStart, mLastTextChangeNewEnd);
          if (indexInText < 0 && action.mStart != mLastTextChangeStart) {
            final String changedText =
                TextUtils.substring(currentText, mLastTextChangeStart, actionNewEnd);
            indexInText = changedText.lastIndexOf(action.mSequence.toString());
            if (indexInText >= 0) {
              indexInText += mLastTextChangeStart;
            }
          }
          if (indexInText < 0) {
            // Replace-text action doesn't match our current text.
            break;
          }

          final int selStart = Selection.getSelectionStart(currentText);
          final int selEnd = Selection.getSelectionEnd(currentText);

          // Replace-text action matches our current text; copy the new spans to the
          // current text.
          mText.currentReplace(
              indexInText, indexInText + action.mSequence.length(), action.mSequence);
          // Make sure selection is preserved.
          mText.currentSetSelection(selStart, selEnd);

          // The text change is caused by the replace-text event. If the text change
          // replaced the previous selection, we need to rely on Gecko for an updated
          // selection, so don't ignore selection change. However, if the text change
          // did not replace the previous selection, we can ignore the Gecko selection
          // in favor of the Java selection.
          mIgnoreSelectionChange = !mLastTextChangeReplacedSelection;
          break;
        }

      case Action.TYPE_SET_SPAN:
        final int len = mText.getCurrentText().length();
        if (action.mStart > len
            || action.mEnd > len
            || !TextUtils.substring(mText.getCurrentText(), action.mStart, action.mEnd)
                .equals(action.mSequence)) {
          if (DEBUG) {
            Log.d(LOGTAG, "discarding stale set span call");
          }
          break;
        }
        if ((action.mSpanObject == Selection.SELECTION_START
                || action.mSpanObject == Selection.SELECTION_END)
            && (action.mStart < mLastTextChangeStart && action.mEnd < mLastTextChangeStart
                || action.mStart > mLastTextChangeOldEnd && action.mEnd > mLastTextChangeOldEnd)) {
          // Use the Java selection if, between text-change notification and replace-text
          // processing, we specifically set the selection to outside the replaced range.
          mLastTextChangeReplacedSelection = false;
        }
        mText.currentSetSpan(action.mSpanObject, action.mStart, action.mEnd, action.mSpanFlags);
        break;

      case Action.TYPE_REMOVE_SPAN:
        mText.currentRemoveSpan(action.mSpanObject);
        break;

      case Action.TYPE_SET_HANDLER:
        geckoSetIcHandler(action.mHandler);
        break;
    }
  }

  private synchronized boolean binderCheckToken(final IBinder token, final boolean allowNull) {
    // Verify that we're getting an IME notification from the currently focused child.
    if (mFocusedToken == token || (mFocusedToken == null && allowNull)) {
      return true;
    }
    Log.w(LOGTAG, "Invalid token");
    return false;
  }

  @Override // IGeckoEditableParent
  public void notifyIME(final IGeckoEditableChild child, @IMENotificationType final int type) {
    // On Gecko or binder thread.
    if (DEBUG) {
      // NOTIFY_IME_REPLY_EVENT is logged separately, inside geckoActionReply()
      if (type != SessionTextInput.EditableListener.NOTIFY_IME_REPLY_EVENT) {
        Log.d(
            LOGTAG,
            "notifyIME("
                + getConstantName(SessionTextInput.EditableListener.class, "NOTIFY_IME_", type)
                + ")");
      }
    }

    final IBinder token = child.asBinder();
    if (type == SessionTextInput.EditableListener.NOTIFY_IME_OF_TOKEN) {
      synchronized (this) {
        if (mFocusedToken != null && mFocusedToken != token && mFocusedToken.pingBinder()) {
          // Focused child already exists and is alive.
          Log.w(LOGTAG, "Already focused");
          return;
        }
        mFocusedToken = token;
        return;
      }
    } else if (type == SessionTextInput.EditableListener.NOTIFY_IME_OPEN_VKB) {
      // Always from parent process.
      ThreadUtils.assertOnGeckoThread();
    } else if (!binderCheckToken(token, /* allowNull */ false)) {
      return;
    }

    if (type == SessionTextInput.EditableListener.NOTIFY_IME_OF_BLUR) {
      synchronized (this) {
        onTextChange(token, "", 0, Integer.MAX_VALUE, false);
        mActions.clear();
        mFocusedToken = null;
      }
    } else if (type == SessionTextInput.EditableListener.NOTIFY_IME_REPLY_EVENT) {
      geckoActionReply(mActions.poll());
      if (!mActions.isEmpty()) {
        // Only post to IC thread below when the queue is empty.
        return;
      }
    }

    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            icNotifyIME(child, type);
          }
        });
  }

  /* package */ void icNotifyIME(
      final IGeckoEditableChild child, @IMENotificationType final int type) {
    if (DEBUG) {
      assertOnIcThread();
    }

    if (type == SessionTextInput.EditableListener.NOTIFY_IME_REPLY_EVENT) {
      if (mNeedSync) {
        icSyncShadowText();
      }
      return;
    }

    switch (type) {
      case SessionTextInput.EditableListener.NOTIFY_IME_OF_FOCUS:
        if (mHideSoftInputTask != null) {
          mHideSoftInputTask.cancel();
          mHideSoftInputTask = null;
        }
        if (mFocusedChild != null) {
          // Already focused, so blur first.
          icRestartInput(
              GeckoSession.TextInputDelegate.RESTART_REASON_BLUR, /* toggleSoftInput */ false);
        }

        mFocusedChild = child;
        mNeedSync = false;
        mText.syncShadowText(/* listener */ null);

        // Most of the time notifyIMEContext comes _before_ notifyIME, but sometimes it
        // comes _after_ notifyIME. In that case, the state is disabled here, and
        // notifyIMEContext is responsible for calling restartInput.
        if (mIMEState == SessionTextInput.EditableListener.IME_STATE_DISABLED) {
          mIMEState = SessionTextInput.EditableListener.IME_STATE_UNKNOWN;
        } else {
          icRestartInput(
              GeckoSession.TextInputDelegate.RESTART_REASON_FOCUS, /* toggleSoftInput */ true);
        }
        break;

      case SessionTextInput.EditableListener.NOTIFY_IME_OF_BLUR:
        if (mFocusedChild != null) {
          mFocusedChild = null;
          icRestartInput(
              GeckoSession.TextInputDelegate.RESTART_REASON_BLUR, /* toggleSoftInput */ true);
        }
        break;

      case SessionTextInput.EditableListener.NOTIFY_IME_OPEN_VKB:
        toggleSoftInput(/* force */ true, mIMEState);
        return; // Don't notify listener.

      case SessionTextInput.EditableListener.NOTIFY_IME_TO_COMMIT_COMPOSITION:
        {
          // Gecko already committed its composition. However, Android keyboards
          // have trouble dealing with us removing the composition manually on the
          // Java side. Therefore, we keep the composition intact on the Java side.
          // The text content should still be in-sync on both sides.
          //
          // Nevertheless, if we somehow lost the composition, we must force the
          // keyboard to reset.
          if (isComposing(mText.getShadowText())) {
            // Still have composition; no need to reset.
            return; // Don't notify listener.
          }
          // No longer have composition; perform reset.
          icRestartInput(
              GeckoSession.TextInputDelegate.RESTART_REASON_CONTENT_CHANGE,
              /* toggleSoftInput */ false);
          return; // Don't notify listener.
        }

      case SessionTextInput.EditableListener.NOTIFY_IME_OF_TOKEN:
      case SessionTextInput.EditableListener.NOTIFY_IME_REPLY_EVENT:
      case SessionTextInput.EditableListener.NOTIFY_IME_TO_CANCEL_COMPOSITION:
      default:
        throw new IllegalArgumentException("Invalid notifyIME type: " + type);
    }

    if (mListener != null) {
      mListener.notifyIME(type);
    }
  }

  @Override // IGeckoEditableParent
  public void notifyIMEContext(
      final IBinder token,
      @IMEState final int state,
      final String typeHint,
      final String modeHint,
      final String actionHint,
      final String autocapitalize,
      final boolean autocorrect,
      @IMEContextFlags final int flags) {
    // On Gecko or binder thread.
    if (DEBUG) {
      final StringBuilder sb = new StringBuilder("notifyIMEContext(");
      sb.append(getConstantName(SessionTextInput.EditableListener.class, "IME_STATE_", state))
          .append(", type=\"")
          .append(typeHint)
          .append("\", inputmode=\"")
          .append(modeHint)
          .append("\", autocapitalize=\"")
          .append(autocapitalize)
          .append("\", autocorrect=\"")
          .append(autocorrect)
          .append("\", flags=0x")
          .append(Integer.toHexString(flags))
          .append(")");
      Log.d(LOGTAG, sb.toString());
    }

    // Regular notifyIMEContext calls all come from the parent process (with the default child),
    // so always allow calls from there. We can get additional notifyIMEContext calls during
    // a session transfer; calls in those cases can come from child processes, and we must
    // perform a token check in that situation.
    if (token != mDefaultChild.asBinder() && !binderCheckToken(token, /* allowNull */ false)) {
      return;
    }

    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            icNotifyIMEContext(
                state, typeHint, modeHint, actionHint, autocapitalize, autocorrect, flags);
          }
        });
  }

  /* package */ void icNotifyIMEContext(
      @IMEState final int originalState,
      final String typeHint,
      final String modeHint,
      final String actionHint,
      final String autocapitalize,
      final boolean autocorrect,
      @IMEContextFlags final int flags) {
    if (DEBUG) {
      assertOnIcThread();
    }

    // For some input type we will use a widget to display the ui, for those we must not
    // display the ime. We can display a widget for date and time types and, if the sdk version
    // is 11 or greater, for datetime/month/week as well.
    final int state;
    if ((typeHint != null
            && (typeHint.equalsIgnoreCase("date")
                || typeHint.equalsIgnoreCase("time")
                || typeHint.equalsIgnoreCase("month")
                || typeHint.equalsIgnoreCase("week")
                || typeHint.equalsIgnoreCase("datetime-local")))
        || (modeHint != null && modeHint.equals("none"))) {
      state = SessionTextInput.EditableListener.IME_STATE_DISABLED;
    } else {
      state = originalState;
    }

    final int oldState = mIMEState;
    mIMEState = state;
    mIMETypeHint = (typeHint == null) ? "" : typeHint;
    mIMEModeHint = (modeHint == null) ? "" : modeHint;
    mIMEActionHint = (actionHint == null) ? "" : actionHint;
    mIMEAutocapitalize = (autocapitalize == null) ? "" : autocapitalize;
    mIMEAutocorrect = autocorrect;
    mIMEFlags = flags;

    if (mListener != null) {
      mListener.notifyIMEContext(state, typeHint, modeHint, actionHint, flags);
    }

    if (mFocusedChild == null) {
      // We have no focus.
      return;
    }

    if ((flags & SessionTextInput.EditableListener.IME_FOCUS_NOT_CHANGED) != 0) {
      if (DEBUG) {
        final StringBuilder sb = new StringBuilder("icNotifyIMEContext: ");
        sb.append("focus isn't changed. oldState=")
            .append(oldState)
            .append(", newState=")
            .append(state);
        Log.d(LOGTAG, sb.toString());
      }
      if (((oldState == SessionTextInput.EditableListener.IME_STATE_ENABLED
                  || oldState == SessionTextInput.EditableListener.IME_STATE_PASSWORD)
              && state == SessionTextInput.EditableListener.IME_STATE_DISABLED)
          || (oldState == SessionTextInput.EditableListener.IME_STATE_DISABLED
              && (state == SessionTextInput.EditableListener.IME_STATE_ENABLED
                  || state == SessionTextInput.EditableListener.IME_STATE_PASSWORD))) {
        // Even if focus isn't changed, software keyboard state is changed.
        // We have to show or dismiss it.
        icRestartInput(
            GeckoSession.TextInputDelegate.RESTART_REASON_CONTENT_CHANGE,
            /* toggleSoftInput */ true);
        return;
      }
    }

    if (state == SessionTextInput.EditableListener.IME_STATE_DISABLED) {
      // When focus is being lost, icNotifyIME with NOTIFY_IME_OF_BLUR
      // will dismiss it.
      // So ignore to control software keyboard at this time.
      return;
    }

    // We changed state while focused. If the old state is unknown, it means this
    // notifyIMEContext call came _after_ the notifyIME call, so we need to call
    // restartInput(FOCUS) here (see comment in icNotifyIME). Otherwise, this change
    // counts as a content change.
    if (oldState == SessionTextInput.EditableListener.IME_STATE_UNKNOWN) {
      icRestartInput(
          GeckoSession.TextInputDelegate.RESTART_REASON_FOCUS, /* toggleSoftInput */ true);
    } else if (oldState != SessionTextInput.EditableListener.IME_STATE_DISABLED) {
      icRestartInput(
          GeckoSession.TextInputDelegate.RESTART_REASON_CONTENT_CHANGE,
          /* toggleSoftInput */ false);
    }
  }

  private void icRestartInput(
      @GeckoSession.RestartReason final int reason, final boolean toggleSoftInput) {
    if (DEBUG) {
      assertOnIcThread();
    }

    ThreadUtils.runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            if (LOGGING) {
              final StringBuilder sb = new StringBuilder("restartInput(reason=");
              sb.append(
                      getConstantName(
                          GeckoSession.TextInputDelegate.class, "RESTART_REASON_", reason))
                  .append(", toggleSoftInput=")
                  .append(toggleSoftInput)
                  .append(")");
              MozLog.d(MOZLOGTAG, sb.toString());
            }

            // Avoid multiple toggleSoftInput call. If this becomes true, onCreateInputConnection is
            // called.
            mIsNewICCreated = false;

            final GeckoSession session = mSession.get();
            if (session != null) {
              session.getTextInput().getDelegate().restartInput(session, reason);
            }

            if (!toggleSoftInput) {
              return;
            }
            postToInputConnection(
                new Runnable() {
                  @Override
                  public void run() {
                    int state = mIMEState;
                    if (reason == GeckoSession.TextInputDelegate.RESTART_REASON_BLUR
                        && mFocusedChild == null) {
                      // On blur, notifyIMEContext() is called after notifyIME(). Therefore,
                      // mIMEState is not up-to-date here and we need to override it.
                      state = SessionTextInput.EditableListener.IME_STATE_DISABLED;
                    }
                    if (state != SessionTextInput.EditableListener.IME_STATE_DISABLED
                        && mIsNewICCreated) {
                      // If state isn't disabled, and new InputConnection is created during
                      // icRestartInput, we don't call toggleSoftInput twice.
                      return;
                    }

                    // Unnecessary to track onCreateInputConnection.
                    mIsNewICCreated = true;

                    // GeckoSession and mFocusedChild would be null due to navigating away or blur.
                    // So we should set force flag to dismiss software keyboard.
                    final boolean force =
                        reason == GeckoSession.TextInputDelegate.RESTART_REASON_BLUR
                            && state == SessionTextInput.EditableListener.IME_STATE_DISABLED;
                    toggleSoftInput(force, state);
                  }
                });
          }
        });
  }

  public void onCreateInputConnection(final EditorInfo outAttrs) {
    final int state = mIMEState;
    final String typeHint = mIMETypeHint;
    final String modeHint = mIMEModeHint;
    final String actionHint = mIMEActionHint;
    final String autocapitalize = mIMEAutocapitalize;
    boolean autocorrect = mIMEAutocorrect;
    final int flags = mIMEFlags;

    // New InputConnection is created.
    mIsNewICCreated = true;

    // Some keyboards require us to fill out outAttrs even if we return null.
    outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE;
    outAttrs.actionLabel = null;

    if (modeHint.equals("none")) {
      // inputmode=none hides VKB at force.
      outAttrs.inputType = InputType.TYPE_NULL;
      toggleSoftInput(/* force */ true, SessionTextInput.EditableListener.IME_STATE_DISABLED);
      return;
    }

    if (state == SessionTextInput.EditableListener.IME_STATE_DISABLED) {
      outAttrs.inputType = InputType.TYPE_NULL;
      toggleSoftInput(/* force */ false, state);
      return;
    }

    // We give priority to typeHint so that content authors can't annoy
    // users by doing dumb things like opening the numeric keyboard for
    // an email form field.
    outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
    if (state == SessionTextInput.EditableListener.IME_STATE_PASSWORD
        || "password".equalsIgnoreCase(typeHint)) {
      outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_PASSWORD;
    } else if (typeHint.equalsIgnoreCase("url") || modeHint.equals("mozAwesomebar")) {
      outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_URI;
    } else if (typeHint.equalsIgnoreCase("email")) {
      outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
    } else if (typeHint.equalsIgnoreCase("tel")) {
      outAttrs.inputType = InputType.TYPE_CLASS_PHONE;
    } else if (typeHint.equalsIgnoreCase("number") || typeHint.equalsIgnoreCase("range")) {
      outAttrs.inputType =
          InputType.TYPE_CLASS_NUMBER
              | InputType.TYPE_NUMBER_VARIATION_NORMAL
              | InputType.TYPE_NUMBER_FLAG_DECIMAL;
    } else {
      // We look at modeHint
      if (modeHint.equals("tel")) {
        outAttrs.inputType = InputType.TYPE_CLASS_PHONE;
      } else if (modeHint.equals("url")) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI;
      } else if (modeHint.equals("email")) {
        outAttrs.inputType |= InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS;
      } else if (modeHint.equals("numeric")) {
        outAttrs.inputType = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_VARIATION_NORMAL;
      } else if (modeHint.equals("decimal")) {
        outAttrs.inputType = InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL;
      } else {
        // TYPE_TEXT_FLAG_IME_MULTI_LINE flag makes the fullscreen IME line wrap
        outAttrs.inputType |=
            InputType.TYPE_TEXT_FLAG_IME_MULTI_LINE | InputType.TYPE_TEXT_VARIATION_WEB_EDIT_TEXT;
      }
    }

    // Even if dom.forms.autocorrect is false, we shouldn't set auto correct flag for email, url and
    // password.
    final int validation = outAttrs.inputType & InputType.TYPE_MASK_VARIATION;
    if (validation == InputType.TYPE_TEXT_VARIATION_PASSWORD
        || validation == InputType.TYPE_TEXT_VARIATION_URI
        || validation == InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS) {
      autocorrect = false;
    }

    if (autocorrect
        && ((outAttrs.inputType & InputType.TYPE_MASK_CLASS) == InputType.TYPE_CLASS_TEXT)) {
      outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_AUTO_CORRECT;
    }

    if (autocapitalize.equals("characters")) {
      outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS;
    } else if (autocapitalize.equals("none")) {
      // not set anymore.
    } else if (autocapitalize.equals("sentences")) {
      outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
    } else if (autocapitalize.equals("words")) {
      outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_WORDS;
    } else if (modeHint.length() == 0
        && (outAttrs.inputType & InputType.TYPE_TEXT_FLAG_IME_MULTI_LINE) != 0
        && !typeHint.equalsIgnoreCase("text")) {
      // auto-capitalized mode is the default for types other than text (bug 871884)
      // except to password, url and email.
      outAttrs.inputType |= InputType.TYPE_TEXT_FLAG_CAP_SENTENCES;
    }

    if (actionHint.equals("enter")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE;
    } else if (actionHint.equals("go")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_GO;
    } else if (actionHint.equals("done")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE;
    } else if (actionHint.equals("next")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_NEXT;
    } else if (actionHint.equals("previous")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_PREVIOUS;
    } else if (actionHint.equals("search") || typeHint.equals("search")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_SEARCH;
    } else if (actionHint.equals("send")) {
      outAttrs.imeOptions = EditorInfo.IME_ACTION_SEND;
    } else if (actionHint.equals("maybenext")) {
      // this should be low priority as "maybenext" is internal type
      outAttrs.imeOptions = EditorInfo.IME_ACTION_NEXT;
    } else if (actionHint.length() > 0) {
      if (DEBUG) Log.w(LOGTAG, "Unexpected actionHint=\"" + actionHint + "\"");
      outAttrs.actionLabel = actionHint;
    }

    if ((flags & SessionTextInput.EditableListener.IME_FLAG_PRIVATE_BROWSING) != 0) {
      outAttrs.imeOptions |= EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING;
    }

    if (typeHint.length() == 0) {
      // contenteditable allows image insertion.
      outAttrs.contentMimeTypes = new String[] {"image/gif", "image/jpeg", "image/png"};
    }

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
      final Spanned currentText = mText.getCurrentText();
      outAttrs.initialSelStart = Selection.getSelectionStart(currentText);
      outAttrs.initialSelEnd = Selection.getSelectionEnd(currentText);
      outAttrs.setInitialSurroundingText(currentText);
    }

    toggleSoftInput(/* force */ false, state);
  }

  /* package */ void toggleSoftInput(final boolean force, @IMEState final int state) {
    if (DEBUG) {
      final StringBuilder sb = new StringBuilder("toggleSoftInput(force=");
      sb.append(force)
          .append(", state=")
          .append(getConstantName(SessionTextInput.EditableListener.class, "IME_STATE_", state))
          .append(")");
      Log.d(LOGTAG, sb.toString());
    }

    // Can be called from UI or IC thread.
    final int flags = mIMEFlags;

    // There are three paths that toggleSoftInput() can be called:
    // 1) through calling restartInput(), which then indirectly calls
    //    onCreateInputConnection() and then toggleSoftInput().
    // 2) through calling toggleSoftInput() directly from restartInput().
    //    This path is the fallback in case 1) does not happen.
    // 3) through a system-generated onCreateInputConnection() call when the activity
    //    is restored from background, which then calls toggleSoftInput().
    // mSoftInputReentrancyGuard is needed to ensure that between the different paths,
    // the soft input is only toggled exactly once.

    ThreadUtils.runOnUiThread(
        new Runnable() {
          @Override
          public void run() {
            try {
              final int reentrancyGuard = mSoftInputReentrancyGuard.incrementAndGet();
              final boolean isReentrant = reentrancyGuard > 1;

              // When using Find In Page, we can still receive notifyIMEContext calls due to the
              // selection changing when highlighting. However in this case we don't want to
              // show/hide the keyboard because the find box has the focus and is taking input from
              // the keyboard.
              final GeckoSession session = mSession.get();

              if (session == null) {
                return;
              }

              final View view = session.getTextInput().getView();
              final boolean isFocused = (view == null) || view.hasFocus();

              final boolean isUserAction =
                  ((flags & SessionTextInput.EditableListener.IME_FLAG_USER_ACTION) != 0);

              if (!force && (isReentrant || !isFocused || !isUserAction)) {
                if (DEBUG) {
                  Log.d(
                      LOGTAG,
                      "toggleSoftInput: no-op, reentrant="
                          + isReentrant
                          + ", focused="
                          + isFocused
                          + ", user="
                          + isUserAction);
                }
                return;
              }

              if (mHideSoftInputTask != null) {
                mHideSoftInputTask.cancel();
                mHideSoftInputTask = null;
              }

              if (state == SessionTextInput.EditableListener.IME_STATE_DISABLED) {
                mHideSoftInputTask = new HideSoftInputTask(session);
                ThreadUtils.postToUiThreadDelayed(mHideSoftInputTask, DISMISS_VKB_DELAY_MS);
                return;
              }
              {
                final GeckoBundle bundle = new GeckoBundle();
                // This bit is subtle. We want to force-zoom to the input
                // if we're _not_ force-showing the virtual keyboard.
                //
                // We only force-show the virtual keyboard as a result of
                // something that _doesn't_ switch the focus, and we don't
                // want to move the view out of the focused editor unless
                // we _actually_ show toggle the keyboard.
                bundle.putBoolean("force", !force);
                session.getEventDispatcher().dispatch("GeckoView:ZoomToInput", bundle);
              }
              session.getTextInput().getDelegate().showSoftInput(session);
            } finally {
              mSoftInputReentrancyGuard.decrementAndGet();
            }
          }
        });
  }

  @Override // IGeckoEditableParent
  public void onSelectionChange(
      final IBinder token, final int start, final int end, final boolean causedOnlyByComposition) {
    // On Gecko or binder thread.
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("onSelectionChange(");
      sb.append(start)
          .append(", ")
          .append(end)
          .append(", ")
          .append(causedOnlyByComposition)
          .append(")");
      MozLog.d(MOZLOGTAG, sb.toString());
    }

    if (!binderCheckToken(token, /* allowNull */ false)) {
      return;
    }

    if (start != end && !causedOnlyByComposition) {
      // selection isn't collapsed. This change is caused by web content side.
      // So we have to update the selection in Java side.
      mIgnoreSelectionChange = false;
    }

    if (mIgnoreSelectionChange) {
      mIgnoreSelectionChange = false;
    } else {
      mText.currentSetSelection(start, end);
    }

    // We receive selection change notification after receiving replies for pending
    // events, so we can reset text change bounds at this point.
    mLastTextChangeStart = Integer.MAX_VALUE;
    mLastTextChangeOldEnd = -1;
    mLastTextChangeNewEnd = -1;
    mLastTextChangeReplacedSelection = false;

    if (causedOnlyByComposition) {
      // It is unnecessary to sync shadow text since this change is by composition from Java
      // side.
      return;
    }

    // It is ready to synchronize Java text with Gecko text when no more input events is
    // dispatched.
    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            icSyncShadowText();
          }
        });
  }

  private boolean geckoIsSameText(final int start, final int oldEnd, final CharSequence newText) {
    return oldEnd - start == newText.length()
        && TextUtils.regionMatches(mText.getCurrentText(), start, newText, 0, oldEnd - start);
  }

  @Override // IGeckoEditableParent
  public void onTextChange(
      final IBinder token,
      final CharSequence text,
      final int start,
      final int unboundedOldEnd,
      final boolean causedOnlyByComposition) {
    // On Gecko or binder thread.
    if (LOGGING) {
      final StringBuilder sb = new StringBuilder("onTextChange(");
      debugAppend(sb, text)
          .append(", ")
          .append(start)
          .append(", ")
          .append(unboundedOldEnd)
          .append(")");
      MozLog.d(MOZLOGTAG, sb.toString());
    }

    if (!binderCheckToken(token, /* allowNull */ false)) {
      return;
    }

    if (unboundedOldEnd >= Integer.MAX_VALUE / 2) {
      // Integer.MAX_VALUE / 2 is a magic number to synchronize all.
      // (See GeckoEditableSupport::FlushIMEText.)
      // Previous text transactions are unnecessary now, so we have to ignore it.
      mActions.clear();
    }

    final int currentLength = mText.getCurrentText().length();
    final int oldEnd = unboundedOldEnd > currentLength ? currentLength : unboundedOldEnd;
    final int newEnd = start + text.length();

    if (start == 0 && unboundedOldEnd > currentLength && !causedOnlyByComposition) {
      // | oldEnd > currentLength | signals entire text is cleared (e.g. for
      // newly-focused editors). Simply replace the text in that case; replace in
      // two steps to properly clear composing spans that span the whole range.
      mText.currentReplace(0, currentLength, "");
      mText.currentReplace(0, 0, text);

      // Don't ignore the next selection change because we are re-syncing with Gecko
      mIgnoreSelectionChange = false;

      mLastTextChangeStart = Integer.MAX_VALUE;
      mLastTextChangeOldEnd = -1;
      mLastTextChangeNewEnd = -1;
      mLastTextChangeReplacedSelection = false;

    } else if (!geckoIsSameText(start, oldEnd, text)) {
      final Spanned currentText = mText.getCurrentText();
      final int selStart = Selection.getSelectionStart(currentText);
      final int selEnd = Selection.getSelectionEnd(currentText);

      // True if the selection was in the middle of the replaced text; in that case
      // we don't know where to place the selection after replacement, and must rely
      // on the Gecko selection.
      mLastTextChangeReplacedSelection |=
          (selStart >= start && selStart <= oldEnd) || (selEnd >= start && selEnd <= oldEnd);

      // Gecko side initiated the text change. Replace in two steps to properly
      // clear composing spans that span the whole range.
      mText.currentReplace(start, oldEnd, "");
      mText.currentReplace(start, start, text);

      mLastTextChangeStart = Math.min(start, mLastTextChangeStart);
      mLastTextChangeOldEnd = Math.max(oldEnd, mLastTextChangeOldEnd);
      mLastTextChangeNewEnd = Math.max(newEnd, mLastTextChangeNewEnd);

    } else {
      // Nothing to do because the text is the same. This could happen when
      // the composition is updated for example, in which case we want to keep the
      // Java selection.
      final Action action = mActions.peek();
      mIgnoreSelectionChange =
          mIgnoreSelectionChange
              || (action != null
                  && (action.mType == Action.TYPE_REPLACE_TEXT
                      || action.mType == Action.TYPE_SET_SPAN
                      || action.mType == Action.TYPE_REMOVE_SPAN));

      mLastTextChangeStart = Math.min(start, mLastTextChangeStart);
      mLastTextChangeOldEnd = Math.max(oldEnd, mLastTextChangeOldEnd);
      mLastTextChangeNewEnd = Math.max(newEnd, mLastTextChangeNewEnd);
    }

    // onTextChange is always followed by onSelectionChange, so we let
    // onSelectionChange schedule a shadow text sync.
  }

  @Override // IGeckoEditableParent
  public void onDefaultKeyEvent(final IBinder token, final KeyEvent event) {
    // On Gecko or binder thread.
    if (DEBUG) {
      final StringBuilder sb = new StringBuilder("onDefaultKeyEvent(");
      sb.append("action=")
          .append(event.getAction())
          .append(", ")
          .append("keyCode=")
          .append(event.getKeyCode())
          .append(", ")
          .append("metaState=")
          .append(event.getMetaState())
          .append(", ")
          .append("time=")
          .append(event.getEventTime())
          .append(", ")
          .append("repeatCount=")
          .append(event.getRepeatCount())
          .append(")");
      Log.d(LOGTAG, sb.toString());
    }

    // Allow default key processing even if we're not focused.
    if (!binderCheckToken(token, /* allowNull */ true)) {
      return;
    }

    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (mListener == null) {
              return;
            }
            mListener.onDefaultKeyEvent(event);
          }
        });
  }

  @Override // IGeckoEditableParent
  public void updateCompositionRects(
      final IBinder token, final RectF[] rects, final RectF caretRect) {
    // On Gecko or binder thread.
    if (DEBUG) {
      Log.d(LOGTAG, "updateCompositionRects(rects.length = " + rects.length + ")");
    }

    if (!binderCheckToken(token, /* allowNull */ false)) {
      return;
    }

    mIcPostHandler.post(
        new Runnable() {
          @Override
          public void run() {
            if (mListener == null) {
              return;
            }
            mListener.updateCompositionRects(rects, caretRect);
          }
        });
  }

  // InvocationHandler interface

  static String getConstantName(final Class<?> cls, final String prefix, final Object value) {
    for (final Field fld : cls.getDeclaredFields()) {
      try {
        if (fld.getName().startsWith(prefix) && fld.get(null).equals(value)) {
          return fld.getName();
        }
      } catch (final IllegalAccessException e) {
      }
    }
    return String.valueOf(value);
  }

  private static String getPrintableChar(final char chr) {
    if (chr >= 0x20 && chr <= 0x7e) {
      return String.valueOf(chr);
    } else if (chr == '\n') {
      return "\u21b2";
    }
    return String.format("\\u%04x", (int) chr);
  }

  static StringBuilder debugAppend(final StringBuilder sb, final Object obj) {
    if (obj == null) {
      sb.append("null");
    } else if (obj instanceof GeckoEditable) {
      sb.append("GeckoEditable");
    } else if (obj instanceof GeckoEditableChild) {
      sb.append("GeckoEditableChild");
    } else if (Proxy.isProxyClass(obj.getClass())) {
      debugAppend(sb, Proxy.getInvocationHandler(obj));
    } else if (obj instanceof Character) {
      sb.append('\'').append(getPrintableChar((Character) obj)).append('\'');
    } else if (obj instanceof CharSequence) {
      final String str = obj.toString();
      sb.append('"');
      for (int i = 0; i < str.length(); i++) {
        final char chr = str.charAt(i);
        if (chr >= 0x20 && chr <= 0x7e) {
          sb.append(chr);
        } else {
          sb.append(getPrintableChar(chr));
        }
      }
      sb.append('"');
    } else if (obj.getClass().isArray()) {
      sb.append(obj.getClass().getComponentType().getSimpleName())
          .append('[')
          .append(Array.getLength(obj))
          .append(']');
    } else {
      sb.append(obj);
    }
    return sb;
  }

  @Override
  public Object invoke(final Object proxy, final Method method, final Object[] args)
      throws Throwable {
    final Object target;
    final Class<?> methodInterface = method.getDeclaringClass();
    if (DEBUG) {
      // Editable methods should all be called from the IC thread
      assertOnIcThread();
    }
    if (methodInterface == Editable.class
        || methodInterface == Appendable.class
        || methodInterface == Spannable.class) {
      // Method alters the Editable; route calls to our implementation
      target = this;
    } else {
      target = mText.getShadowText();
    }

    final Object ret = method.invoke(target, args);
    if (DEBUG) {
      final StringBuilder log = new StringBuilder(method.getName());
      log.append("(");
      if (args != null) {
        for (final Object arg : args) {
          debugAppend(log, arg).append(", ");
        }
        if (args.length > 0) {
          log.setLength(log.length() - 2);
        }
      }
      if (method.getReturnType().equals(Void.TYPE)) {
        log.append(")");
      } else {
        debugAppend(log.append(") = "), ret);
      }
      Log.d(LOGTAG, log.toString());
    }
    return ret;
  }

  // Spannable interface

  @Override
  public void removeSpan(final Object what) {
    if (what == null) {
      return;
    }

    if (what == Selection.SELECTION_START || what == Selection.SELECTION_END) {
      Log.w(LOGTAG, "selection removed with removeSpan()");
    }

    icOfferAction(Action.newRemoveSpan(what));
  }

  @Override
  public void setSpan(final Object what, final int start, final int end, final int flags) {
    icOfferAction(Action.newSetSpan(what, start, end, flags));
  }

  // Appendable interface

  @Override
  public Editable append(final CharSequence text) {
    return replace(mProxy.length(), mProxy.length(), text, 0, text.length());
  }

  @Override
  public Editable append(final CharSequence text, final int start, final int end) {
    return replace(mProxy.length(), mProxy.length(), text, start, end);
  }

  @Override
  public Editable append(final char text) {
    return replace(mProxy.length(), mProxy.length(), String.valueOf(text), 0, 1);
  }

  // Editable interface

  @Override
  public InputFilter[] getFilters() {
    return mFilters;
  }

  @Override
  public void setFilters(final InputFilter[] filters) {
    mFilters = filters;
  }

  @Override
  public void clearSpans() {
    /* XXX this clears the selection spans too,
    but there is no way to clear the corresponding selection in Gecko */
    Log.w(LOGTAG, "selection cleared with clearSpans()");
    icOfferAction(Action.newRemoveSpan(/* what */ null));
  }

  @Override
  public Editable replace(
      final int st, final int en, final CharSequence source, final int start, final int end) {
    CharSequence text = source;
    if (start < 0 || start > end || end > text.length()) {
      Log.e(
          LOGTAG,
          "invalid replace offsets: " + start + " to " + end + ", length: " + text.length());
      throw new IllegalArgumentException("invalid replace offsets");
    }
    if (start != 0 || end != text.length()) {
      text = text.subSequence(start, end);
    }
    if (mFilters != null) {
      // Filter text before sending the request to Gecko
      for (int i = 0; i < mFilters.length; ++i) {
        final CharSequence cs = mFilters[i].filter(text, 0, text.length(), mProxy, st, en);
        if (cs != null) {
          text = cs;
        }
      }
    }
    if (text == source) {
      // Always create a copy
      text = new SpannableString(source);
    }
    icOfferAction(Action.newReplaceText(text, Math.min(st, en), Math.max(st, en)));
    return mProxy;
  }

  @Override
  public void clear() {
    replace(0, mProxy.length(), "", 0, 0);
  }

  @Override
  public Editable delete(final int st, final int en) {
    return replace(st, en, "", 0, 0);
  }

  @Override
  public Editable insert(final int where, final CharSequence text, final int start, final int end) {
    return replace(where, where, text, start, end);
  }

  @Override
  public Editable insert(final int where, final CharSequence text) {
    return replace(where, where, text, 0, text.length());
  }

  @Override
  public Editable replace(final int st, final int en, final CharSequence text) {
    return replace(st, en, text, 0, text.length());
  }

  /* GetChars interface */

  @Override
  public void getChars(final int start, final int end, final char[] dest, final int destoff) {
    /* overridden Editable interface methods in GeckoEditable must not be called directly
    outside of GeckoEditable. Instead, the call must go through mProxy, which ensures
    that Java is properly synchronized with Gecko */
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  /* Spanned interface */

  @Override
  public int getSpanEnd(final Object tag) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public int getSpanFlags(final Object tag) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public int getSpanStart(final Object tag) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public <T> T[] getSpans(final int start, final int end, final Class<T> type) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  @SuppressWarnings("rawtypes") // nextSpanTransition uses raw Class in its Android declaration
  public int nextSpanTransition(final int start, final int limit, final Class type) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  /* CharSequence interface */

  @Override
  public char charAt(final int index) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public int length() {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public CharSequence subSequence(final int start, final int end) {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  @Override
  public String toString() {
    throw new UnsupportedOperationException("method must be called through mProxy");
  }

  public boolean onKeyPreIme(
      final @Nullable View view, final int keyCode, final @NonNull KeyEvent event) {
    return false;
  }

  public boolean onKeyDown(
      final @Nullable View view, final int keyCode, final @NonNull KeyEvent event) {
    return processKey(view, KeyEvent.ACTION_DOWN, keyCode, event);
  }

  public boolean onKeyUp(
      final @Nullable View view, final int keyCode, final @NonNull KeyEvent event) {
    return processKey(view, KeyEvent.ACTION_UP, keyCode, event);
  }

  public boolean onKeyMultiple(
      final @Nullable View view,
      final int keyCode,
      final int repeatCount,
      final @NonNull KeyEvent event) {
    if (keyCode == KeyEvent.KEYCODE_UNKNOWN) {
      // KEYCODE_UNKNOWN means the characters are in KeyEvent.getCharacters()
      @SuppressWarnings("deprecation")
      final String str = event.getCharacters();
      for (int i = 0; i < str.length(); i++) {
        final KeyEvent charEvent = getCharKeyEvent(str.charAt(i));
        if (!processKey(view, KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_UNKNOWN, charEvent)
            || !processKey(view, KeyEvent.ACTION_UP, KeyEvent.KEYCODE_UNKNOWN, charEvent)) {
          return false;
        }
      }
      return true;
    }

    for (int i = 0; i < repeatCount; i++) {
      if (!processKey(view, KeyEvent.ACTION_DOWN, keyCode, event)
          || !processKey(view, KeyEvent.ACTION_UP, keyCode, event)) {
        return false;
      }
    }

    return true;
  }

  public boolean onKeyLongPress(
      final @Nullable View view, final int keyCode, final @NonNull KeyEvent event) {
    return false;
  }

  /** Get a key that represents a given character. */
  private static KeyEvent getCharKeyEvent(final char c) {
    final long time = SystemClock.uptimeMillis();
    @SuppressWarnings("deprecation")
    final int action = KeyEvent.ACTION_MULTIPLE;
    return new KeyEvent(time, time, action, KeyEvent.KEYCODE_UNKNOWN, /* repeat */ 0) {
      @Override
      public int getUnicodeChar() {
        return c;
      }

      @Override
      public int getUnicodeChar(final int metaState) {
        return c;
      }
    };
  }

  private boolean processKey(
      final @Nullable View view,
      final int action,
      final int keyCode,
      final @NonNull KeyEvent event) {
    if (keyCode > KeyEvent.getMaxKeyCode() || !shouldProcessKey(keyCode, event)) {
      return false;
    }

    postToInputConnection(
        new Runnable() {
          @Override
          public void run() {
            sendKeyEvent(view, action, event);
          }
        });
    return true;
  }

  private static boolean shouldProcessKey(final int keyCode, final KeyEvent event) {
    switch (keyCode) {
      case KeyEvent.KEYCODE_MENU:
      case KeyEvent.KEYCODE_BACK:
      case KeyEvent.KEYCODE_FORWARD:
      case KeyEvent.KEYCODE_VOLUME_UP:
      case KeyEvent.KEYCODE_VOLUME_DOWN:
      case KeyEvent.KEYCODE_VOLUME_MUTE:
      case KeyEvent.KEYCODE_SEARCH:
      // ignore HEADSETHOOK to allow hold-for-voice-search to work
      case KeyEvent.KEYCODE_HEADSETHOOK:
        return false;
    }
    return true;
  }

  private static boolean isComposing(final Spanned text) {
    final Object[] spans = text.getSpans(0, text.length(), Object.class);
    for (final Object span : spans) {
      if ((text.getSpanFlags(span) & Spanned.SPAN_COMPOSING) != 0) {
        return true;
      }
    }

    return false;
  }

  private static int getComposingStart(final Spanned text) {
    int composingStart = Integer.MAX_VALUE;
    final Object[] spans = text.getSpans(0, text.length(), Object.class);
    for (final Object span : spans) {
      if ((text.getSpanFlags(span) & Spanned.SPAN_COMPOSING) != 0) {
        composingStart = Math.min(composingStart, text.getSpanStart(span));
      }
    }

    return composingStart;
  }

  private static int getComposingEnd(final Spanned text) {
    int composingEnd = -1;
    final Object[] spans = text.getSpans(0, text.length(), Object.class);
    for (final Object span : spans) {
      if ((text.getSpanFlags(span) & Spanned.SPAN_COMPOSING) != 0) {
        composingEnd = Math.max(composingEnd, text.getSpanEnd(span));
      }
    }

    return composingEnd;
  }
}
