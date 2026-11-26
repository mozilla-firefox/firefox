/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview_example;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.TypedArray;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffColorFilter;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.text.format.DateFormat;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.MimeTypeMap;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.CheckedTextView;
import android.widget.DatePicker;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.TimePicker;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.Locale;
import java.util.UUID;
import org.mozilla.geckoview.AllowOrDeny;
import org.mozilla.geckoview.Autocomplete;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.MediaSource;
import org.mozilla.geckoview.SlowScriptResponse;

final class BasicGeckoViewPrompt implements GeckoSession.PromptDelegate {
  protected static final String LOGTAG = "BasicGeckoViewPrompt";

  private final Activity mActivity;
  public int filePickerRequestCode = 1;
  private int mFileType;
  private GeckoResult<PromptResponse> mFileResponse;
  private FilePrompt mFilePrompt;

  public BasicGeckoViewPrompt(final Activity activity) {
    mActivity = activity;
  }

  @Override
  public GeckoResult<PromptResponse> onAlertPrompt(
      final GeckoSession session, final AlertPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder =
        new AlertDialog.Builder(activity)
            .setTitle(prompt.title)
            .setMessage(prompt.message)
            .setPositiveButton(android.R.string.ok, /* onClickListener */ null);
    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();
    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  @Override
  public GeckoResult<PromptResponse> onButtonPrompt(
      final GeckoSession session, final ButtonPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder =
        new AlertDialog.Builder(activity).setTitle(prompt.title).setMessage(prompt.message);

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    final DialogInterface.OnClickListener listener =
        new DialogInterface.OnClickListener() {
          @Override
          public void onClick(final DialogInterface dialog, final int which) {
            if (which == DialogInterface.BUTTON_POSITIVE) {
              res.complete(prompt.confirm(ButtonPrompt.Type.POSITIVE));
            } else if (which == DialogInterface.BUTTON_NEGATIVE) {
              res.complete(prompt.confirm(ButtonPrompt.Type.NEGATIVE));
            } else {
              res.complete(prompt.dismiss());
            }
          }
        };

    builder.setPositiveButton(android.R.string.ok, listener);
    builder.setNegativeButton(android.R.string.cancel, listener);

    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  @Override
  public GeckoResult<PromptResponse> onSharePrompt(
      final GeckoSession session, final SharePrompt prompt) {
    return GeckoResult.fromValue(prompt.dismiss());
  }

  @Nullable
  @Override
  public GeckoResult<PromptResponse> onRepostConfirmPrompt(
      final GeckoSession session, final RepostConfirmPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder =
        new AlertDialog.Builder(activity)
            .setTitle(R.string.repost_confirm_title)
            .setMessage(R.string.repost_confirm_message);

    GeckoResult<PromptResponse> res = new GeckoResult<>();

    final DialogInterface.OnClickListener listener =
        (dialog, which) -> {
          if (which == DialogInterface.BUTTON_POSITIVE) {
            res.complete(prompt.confirm(AllowOrDeny.ALLOW));
          } else if (which == DialogInterface.BUTTON_NEGATIVE) {
            res.complete(prompt.confirm(AllowOrDeny.DENY));
          } else {
            res.complete(prompt.dismiss());
          }
        };

    builder.setPositiveButton(R.string.repost_confirm_resend, listener);
    builder.setNegativeButton(R.string.repost_confirm_cancel, listener);

    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  @Nullable
  @Override
  public GeckoResult<PromptResponse> onCreditCardSave(
      @NonNull GeckoSession session,
      @NonNull AutocompleteRequest<Autocomplete.CreditCardSaveOption> request) {
    DebugLog.i(LOGTAG, "onCreditCardSave " + request.options[0].value);
    return null;
  }

  @Nullable
  @Override
  public GeckoResult<PromptResponse> onLoginSave(
      @NonNull GeckoSession session,
      @NonNull AutocompleteRequest<Autocomplete.LoginSaveOption> request) {
    DebugLog.i(LOGTAG, "onLoginSave");
    return GeckoResult.fromValue(request.confirm(request.options[0]));
  }

  @Nullable
  @Override
  public GeckoResult<PromptResponse> onBeforeUnloadPrompt(
      final GeckoSession session, final BeforeUnloadPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder =
        new AlertDialog.Builder(activity)
            .setTitle(R.string.before_unload_title)
            .setMessage(R.string.before_unload_message);

    GeckoResult<PromptResponse> res = new GeckoResult<>();

    final DialogInterface.OnClickListener listener =
        (dialog, which) -> {
          if (which == DialogInterface.BUTTON_POSITIVE) {
            res.complete(prompt.confirm(AllowOrDeny.ALLOW));
          } else if (which == DialogInterface.BUTTON_NEGATIVE) {
            res.complete(prompt.confirm(AllowOrDeny.DENY));
          } else {
            res.complete(prompt.dismiss());
          }
        };

    builder.setPositiveButton(R.string.before_unload_leave_page, listener);
    builder.setNegativeButton(R.string.before_unload_stay, listener);

    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  @Nullable
  @Override
  public GeckoResult<PromptResponse> onFolderUploadPrompt(
      final GeckoSession session, final FolderUploadPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder =
        new AlertDialog.Builder(activity)
            .setTitle(R.string.folder_upload_title)
            .setMessage(R.string.folder_upload_message);

    final GeckoResult<PromptResponse> res = new GeckoResult<>();

    final DialogInterface.OnClickListener listener =
        (dialog, which) -> {
          if (which == DialogInterface.BUTTON_POSITIVE) {
            res.complete(prompt.confirm(AllowOrDeny.ALLOW));
          } else if (which == DialogInterface.BUTTON_NEGATIVE) {
            res.complete(prompt.confirm(AllowOrDeny.DENY));
          } else {
            res.complete(prompt.dismiss());
          }
        };

    builder.setPositiveButton(R.string.folder_upload_confirm_accept, listener);
    builder.setNegativeButton(R.string.folder_upload_confirm_cancel, listener);

    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  private int getViewPadding(final AlertDialog.Builder builder) {
    final TypedArray attr =
        builder
            .getContext()
            .obtainStyledAttributes(new int[] {android.R.attr.listPreferredItemPaddingLeft});
    final int padding = attr.getDimensionPixelSize(0, 1);
    attr.recycle();
    return padding;
  }

  private LinearLayout addStandardLayout(
      final AlertDialog.Builder builder, final String title, final String msg) {
    final ScrollView scrollView = new ScrollView(builder.getContext());
    final LinearLayout container = new LinearLayout(builder.getContext());
    final int horizontalPadding = getViewPadding(builder);
    final int verticalPadding = (msg == null || msg.isEmpty()) ? horizontalPadding : 0;
    container.setOrientation(LinearLayout.VERTICAL);
    container.setPadding(
        /* left */ horizontalPadding, /* top */ verticalPadding,
        /* right */ horizontalPadding, /* bottom */ verticalPadding);
    scrollView.addView(container);
    builder.setTitle(title).setMessage(msg).setView(scrollView);
    return container;
  }

  private AlertDialog createStandardDialog(
      final AlertDialog.Builder builder,
      final BasePrompt prompt,
      final GeckoResult<PromptResponse> response) {
    final AlertDialog dialog = builder.create();
    dialog.setOnDismissListener(
        new DialogInterface.OnDismissListener() {
          @Override
          public void onDismiss(final DialogInterface dialog) {
            if (!prompt.isComplete()) {
              response.complete(prompt.dismiss());
            }
          }
        });
    return dialog;
  }

  @Override
  public GeckoResult<PromptResponse> onTextPrompt(
      final GeckoSession session, final TextPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    final LinearLayout container = addStandardLayout(builder, prompt.title, prompt.message);
    final EditText editText = new EditText(builder.getContext());
    editText.setText(prompt.defaultValue);
    container.addView(editText);

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    builder
        .setNegativeButton(android.R.string.cancel, /* listener */ null)
        .setPositiveButton(
            android.R.string.ok,
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                res.complete(prompt.confirm(editText.getText().toString()));
              }
            });

    createStandardDialog(builder, prompt, res).show();
    return res;
  }

  @Override
  public GeckoResult<PromptResponse> onAuthPrompt(
      final GeckoSession session, final AuthPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    final LinearLayout container = addStandardLayout(builder, prompt.title, prompt.message);

    final int flags = prompt.authOptions.flags;
    final int level = prompt.authOptions.level;
    final EditText username;
    if ((flags & AuthPrompt.AuthOptions.Flags.ONLY_PASSWORD) == 0) {
      username = new EditText(builder.getContext());
      username.setHint(R.string.username);
      username.setText(prompt.authOptions.username);
      container.addView(username);
    } else {
      username = null;
    }

    final EditText password = new EditText(builder.getContext());
    password.setHint(R.string.password);
    password.setText(prompt.authOptions.password);
    password.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
    container.addView(password);

    if (level != AuthPrompt.AuthOptions.Level.NONE) {
      final ImageView secure = new ImageView(builder.getContext());
      secure.setImageResource(android.R.drawable.ic_lock_lock);
      container.addView(secure);
    }

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    builder
        .setNegativeButton(android.R.string.cancel, /* listener */ null)
        .setPositiveButton(
            android.R.string.ok,
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                if ((flags & AuthPrompt.AuthOptions.Flags.ONLY_PASSWORD) == 0) {
                  res.complete(
                      prompt.confirm(username.getText().toString(), password.getText().toString()));

                } else {
                  res.complete(prompt.confirm(password.getText().toString()));
                }
              }
            });
    createStandardDialog(builder, prompt, res).show();

    return res;
  }

  private static class ModifiableChoice {
    public boolean modifiableSelected;
    public String modifiableLabel;
    public final ChoicePrompt.Choice choice;

    public ModifiableChoice(ChoicePrompt.Choice c) {
      choice = c;
      modifiableSelected = choice.selected;
      modifiableLabel = choice.label;
    }
  }

  private void addChoiceItems(
      final int type,
      final ArrayAdapter<ModifiableChoice> list,
      final ChoicePrompt.Choice[] items,
      final String indent) {
    if (type == ChoicePrompt.Type.MENU) {
      for (final ChoicePrompt.Choice item : items) {
        list.add(new ModifiableChoice(item));
      }
      return;
    }

    for (final ChoicePrompt.Choice item : items) {
      final ModifiableChoice modItem = new ModifiableChoice(item);

      final ChoicePrompt.Choice[] children = item.items;

      if (indent != null && children == null) {
        modItem.modifiableLabel = indent + modItem.modifiableLabel;
      }
      list.add(modItem);

      if (children != null) {
        final String newIndent;
        if (type == ChoicePrompt.Type.SINGLE || type == ChoicePrompt.Type.MULTIPLE) {
          newIndent = (indent != null) ? indent + '\t' : "\t";
        } else {
          newIndent = null;
        }
        addChoiceItems(type, list, children, newIndent);
      }
    }
  }

  private void onChoicePromptImpl(
      final GeckoSession session,
      final String title,
      final String message,
      final int type,
      final ChoicePrompt.Choice[] choices,
      final ChoicePrompt prompt,
      final GeckoResult<PromptResponse> res) {
    final Activity activity = mActivity;
    if (activity == null) {
      res.complete(prompt.dismiss());
      return;
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    addStandardLayout(builder, title, message);

    final ListView list = new ListView(builder.getContext());
    if (type == ChoicePrompt.Type.MULTIPLE) {
      list.setChoiceMode(ListView.CHOICE_MODE_MULTIPLE);
    }

    final ArrayAdapter<ModifiableChoice> adapter =
        new ArrayAdapter<ModifiableChoice>(
            builder.getContext(), android.R.layout.simple_list_item_1) {
          private static final int TYPE_MENU_ITEM = 0;
          private static final int TYPE_MENU_CHECK = 1;
          private static final int TYPE_SEPARATOR = 2;
          private static final int TYPE_GROUP = 3;
          private static final int TYPE_SINGLE = 4;
          private static final int TYPE_MULTIPLE = 5;
          private static final int TYPE_COUNT = 6;

          private LayoutInflater mInflater;
          private View mSeparator;

          @Override
          public int getViewTypeCount() {
            return TYPE_COUNT;
          }

          @Override
          public int getItemViewType(final int position) {
            final ModifiableChoice item = getItem(position);
            if (item.choice.separator) {
              return TYPE_SEPARATOR;
            } else if (type == ChoicePrompt.Type.MENU) {
              return item.modifiableSelected ? TYPE_MENU_CHECK : TYPE_MENU_ITEM;
            } else if (item.choice.items != null) {
              return TYPE_GROUP;
            } else if (type == ChoicePrompt.Type.SINGLE) {
              return TYPE_SINGLE;
            } else if (type == ChoicePrompt.Type.MULTIPLE) {
              return TYPE_MULTIPLE;
            } else {
              throw new UnsupportedOperationException();
            }
          }

          @Override
          public boolean isEnabled(final int position) {
            final ModifiableChoice item = getItem(position);
            return !item.choice.separator
                && !item.choice.disabled
                && ((type != ChoicePrompt.Type.SINGLE && type != ChoicePrompt.Type.MULTIPLE)
                    || item.choice.items == null);
          }

          @Override
          public View getView(final int position, View view, final ViewGroup parent) {
            final int itemType = getItemViewType(position);
            final int layoutId;
            if (itemType == TYPE_SEPARATOR) {
              if (mSeparator == null) {
                mSeparator = new View(getContext());
                mSeparator.setLayoutParams(
                    new ListView.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 2, itemType));
                final TypedArray attr =
                    getContext().obtainStyledAttributes(new int[] {android.R.attr.listDivider});
                mSeparator.setBackgroundResource(attr.getResourceId(0, 0));
                attr.recycle();
              }
              return mSeparator;
            } else if (itemType == TYPE_MENU_ITEM) {
              layoutId = android.R.layout.simple_list_item_1;
            } else if (itemType == TYPE_MENU_CHECK) {
              layoutId = android.R.layout.simple_list_item_checked;
            } else if (itemType == TYPE_GROUP) {
              layoutId = android.R.layout.preference_category;
            } else if (itemType == TYPE_SINGLE) {
              layoutId = android.R.layout.simple_list_item_single_choice;
            } else if (itemType == TYPE_MULTIPLE) {
              layoutId = android.R.layout.simple_list_item_multiple_choice;
            } else {
              throw new UnsupportedOperationException();
            }

            if (view == null) {
              if (mInflater == null) {
                mInflater = LayoutInflater.from(builder.getContext());
              }
              view = mInflater.inflate(layoutId, parent, false);
            }

            final ModifiableChoice item = getItem(position);
            final TextView text = (TextView) view;
            text.setEnabled(!item.choice.disabled);
            text.setText(item.modifiableLabel);
            if (view instanceof CheckedTextView) {
              final boolean selected = item.modifiableSelected;
              if (itemType == TYPE_MULTIPLE) {
                list.setItemChecked(position, selected);
              } else {
                ((CheckedTextView) view).setChecked(selected);
              }
            }
            return view;
          }
        };
    addChoiceItems(type, adapter, choices, /* indent */ null);

    list.setAdapter(adapter);
    builder.setView(list);

    final AlertDialog dialog;
    if (type == ChoicePrompt.Type.SINGLE || type == ChoicePrompt.Type.MENU) {
      dialog = createStandardDialog(builder, prompt, res);
      list.setOnItemClickListener(
          new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(
                final AdapterView<?> parent, final View v, final int position, final long id) {
              final ModifiableChoice item = adapter.getItem(position);
              if (type == ChoicePrompt.Type.MENU) {
                final ChoicePrompt.Choice[] children = item.choice.items;
                if (children != null) {
                  // Show sub-menu.
                  dialog.setOnDismissListener(null);
                  dialog.dismiss();
                  onChoicePromptImpl(
                      session, item.modifiableLabel, /* msg */ null, type, children, prompt, res);
                  return;
                }
              }
              res.complete(prompt.confirm(item.choice));
              dialog.dismiss();
            }
          });
    } else if (type == ChoicePrompt.Type.MULTIPLE) {
      list.setOnItemClickListener(
          new AdapterView.OnItemClickListener() {
            @Override
            public void onItemClick(
                final AdapterView<?> parent, final View v, final int position, final long id) {
              final ModifiableChoice item = adapter.getItem(position);
              item.modifiableSelected = ((CheckedTextView) v).isChecked();
            }
          });
      builder
          .setNegativeButton(android.R.string.cancel, /* listener */ null)
          .setPositiveButton(
              android.R.string.ok,
              new DialogInterface.OnClickListener() {
                @Override
                public void onClick(final DialogInterface dialog, final int which) {
                  final int len = adapter.getCount();
                  ArrayList<String> items = new ArrayList<>(len);
                  for (int i = 0; i < len; i++) {
                    final ModifiableChoice item = adapter.getItem(i);
                    if (item.modifiableSelected) {
                      items.add(item.choice.id);
                    }
                  }
                  res.complete(prompt.confirm(items.toArray(new String[items.size()])));
                }
              });
      dialog = createStandardDialog(builder, prompt, res);
    } else {
      throw new UnsupportedOperationException();
    }
    dialog.show();

    prompt.setDelegate(
        new PromptInstanceDelegate() {
          @Override
          public void onPromptDismiss(final BasePrompt prompt) {
            dialog.dismiss();
          }

          @Override
          public void onPromptUpdate(final BasePrompt prompt) {
            dialog.setOnDismissListener(null);
            dialog.dismiss();
            final ChoicePrompt newPrompt = (ChoicePrompt) prompt;
            onChoicePromptImpl(
                session,
                newPrompt.title,
                newPrompt.message,
                newPrompt.type,
                newPrompt.choices,
                newPrompt,
                res);
          }
        });
  }

  @Override
  public GeckoResult<PromptResponse> onChoicePrompt(
      final GeckoSession session, final ChoicePrompt prompt) {
    final GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();
    onChoicePromptImpl(
        session, prompt.title, prompt.message, prompt.type, prompt.choices, prompt, res);
    return res;
  }

  private static int parseColor(final String value, final int def) {
    try {
      return Color.parseColor(value);
    } catch (final IllegalArgumentException e) {
      return def;
    }
  }

  @Override
  public GeckoResult<PromptResponse> onColorPrompt(
      final GeckoSession session, final ColorPrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    addStandardLayout(builder, prompt.title, /* msg */ null);

    final int initial = parseColor(prompt.defaultValue, /* def */ 0);
    final ArrayAdapter<Integer> adapter =
        new ArrayAdapter<Integer>(builder.getContext(), android.R.layout.simple_list_item_1) {
          private LayoutInflater mInflater;

          @Override
          public int getViewTypeCount() {
            return 2;
          }

          @Override
          public int getItemViewType(final int position) {
            return (getItem(position) == initial) ? 1 : 0;
          }

          @Override
          public View getView(final int position, View view, final ViewGroup parent) {
            if (mInflater == null) {
              mInflater = LayoutInflater.from(builder.getContext());
            }
            final int color = getItem(position);
            if (view == null) {
              view =
                  mInflater.inflate(
                      (color == initial)
                          ? android.R.layout.simple_list_item_checked
                          : android.R.layout.simple_list_item_1,
                      parent,
                      false);
            }
            view.setBackgroundResource(android.R.drawable.editbox_background);
            view.getBackground()
                .setColorFilter(new PorterDuffColorFilter(color, PorterDuff.Mode.MULTIPLY));
            return view;
          }
        };

    adapter.addAll(
        0xffff4444 /* holo_red_light */,
        0xffcc0000 /* holo_red_dark */,
        0xffffbb33 /* holo_orange_light */,
        0xffff8800 /* holo_orange_dark */,
        0xff99cc00 /* holo_green_light */,
        0xff669900 /* holo_green_dark */,
        0xff33b5e5 /* holo_blue_light */,
        0xff0099cc /* holo_blue_dark */,
        0xffaa66cc /* holo_purple */,
        0xffffffff /* white */,
        0xffaaaaaa /* lighter_gray */,
        0xff555555 /* darker_gray */,
        0xff000000 /* black */);

    final ListView list = new ListView(builder.getContext());
    list.setAdapter(adapter);
    builder.setView(list);

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    final AlertDialog dialog = createStandardDialog(builder, prompt, res);
    list.setOnItemClickListener(
        new AdapterView.OnItemClickListener() {
          @Override
          public void onItemClick(
              final AdapterView<?> parent, final View v, final int position, final long id) {
            res.complete(
                prompt.confirm(String.format("#%06x", 0xffffff & adapter.getItem(position))));
            dialog.dismiss();
          }
        });
    dialog.show();

    return res;
  }

  private static Date parseDate(
      final SimpleDateFormat formatter, final String value, final boolean defaultToNow) {
    try {
      if (value != null && !value.isEmpty()) {
        return formatter.parse(value);
      }
    } catch (final ParseException e) {
    }
    return defaultToNow ? new Date() : null;
  }

  private static void setTimePickerTime(final TimePicker picker, final Calendar cal) {
    picker.setHour(cal.get(Calendar.HOUR_OF_DAY));
    picker.setMinute(cal.get(Calendar.MINUTE));
  }

  private static void setCalendarTime(final Calendar cal, final TimePicker picker) {
    cal.set(Calendar.HOUR_OF_DAY, picker.getHour());
    cal.set(Calendar.MINUTE, picker.getMinute());
  }

  @Override
  public GeckoResult<PromptResponse> onDateTimePrompt(
      final GeckoSession session, final DateTimePrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }
    final String format;
    if (prompt.type == DateTimePrompt.Type.DATE) {
      format = "yyyy-MM-dd";
    } else if (prompt.type == DateTimePrompt.Type.MONTH) {
      format = "yyyy-MM";
    } else if (prompt.type == DateTimePrompt.Type.WEEK) {
      format = "yyyy-'W'ww";
    } else if (prompt.type == DateTimePrompt.Type.TIME) {
      format = "HH:mm";
    } else if (prompt.type == DateTimePrompt.Type.DATETIME_LOCAL) {
      format = "yyyy-MM-dd'T'HH:mm";
    } else {
      throw new UnsupportedOperationException();
    }

    final SimpleDateFormat formatter = new SimpleDateFormat(format, Locale.ROOT);
    final Date minDate = parseDate(formatter, prompt.minValue, /* defaultToNow */ false);
    final Date maxDate = parseDate(formatter, prompt.maxValue, /* defaultToNow */ false);
    final Date date = parseDate(formatter, prompt.defaultValue, /* defaultToNow */ true);
    final Calendar cal = formatter.getCalendar();
    cal.setTime(date);

    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    final DatePicker datePicker;
    if (prompt.type == DateTimePrompt.Type.DATE
        || prompt.type == DateTimePrompt.Type.MONTH
        || prompt.type == DateTimePrompt.Type.WEEK
        || prompt.type == DateTimePrompt.Type.DATETIME_LOCAL) {
      final DatePicker picker = new DatePicker(builder.getContext());
      picker.init(
          cal.get(Calendar.YEAR),
          cal.get(Calendar.MONTH),
          cal.get(Calendar.DAY_OF_MONTH), /* listener */
          null);
      if (minDate != null) {
        picker.setMinDate(minDate.getTime());
      }
      if (maxDate != null) {
        picker.setMaxDate(maxDate.getTime());
      }
      datePicker = picker;
    } else {
      datePicker = null;
    }

    final TimePicker timePicker;
    if (prompt.type == DateTimePrompt.Type.TIME
        || prompt.type == DateTimePrompt.Type.DATETIME_LOCAL) {
      final TimePicker picker = new TimePicker(builder.getContext());
      setTimePickerTime(picker, cal);
      picker.setIs24HourView(DateFormat.is24HourFormat(builder.getContext()));
      timePicker = picker;
    } else {
      timePicker = null;
    }

    final LinearLayout container = addStandardLayout(builder, prompt.title, /* msg */ null);
    container.setPadding(/* left */ 0, /* top */ 0, /* right */ 0, /* bottom */ 0);
    if (datePicker != null) {
      container.addView(datePicker);
    }
    if (timePicker != null) {
      container.addView(timePicker);
    }

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    final DialogInterface.OnClickListener listener =
        new DialogInterface.OnClickListener() {
          @Override
          public void onClick(final DialogInterface dialog, final int which) {
            if (which == DialogInterface.BUTTON_NEUTRAL) {
              // Clear
              res.complete(prompt.confirm(""));
              return;
            }
            if (datePicker != null) {
              cal.set(datePicker.getYear(), datePicker.getMonth(), datePicker.getDayOfMonth());
            }
            if (timePicker != null) {
              setCalendarTime(cal, timePicker);
            }
            res.complete(prompt.confirm(formatter.format(cal.getTime())));
          }
        };
    builder
        .setNegativeButton(android.R.string.cancel, /* listener */ null)
        .setNeutralButton(R.string.clear_field, listener)
        .setPositiveButton(android.R.string.ok, listener);

    final AlertDialog dialog = createStandardDialog(builder, prompt, res);
    dialog.show();

    prompt.setDelegate(
        new PromptInstanceDelegate() {
          @Override
          public void onPromptDismiss(final BasePrompt prompt) {
            dialog.dismiss();
          }
        });
    return res;
  }

  private GeckoResult<PromptResponse> onFolderPrompt(
      final GeckoSession session, final FilePrompt prompt) {
    final Activity activity = mActivity;
    final Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
    intent.addCategory(Intent.CATEGORY_DEFAULT);
    final GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    try {
      mFileResponse = res;
      mFilePrompt = prompt;
      activity.startActivityForResult(intent, filePickerRequestCode);
    } catch (final ActivityNotFoundException e) {
      Log.e(LOGTAG, "Cannot launch activity", e);
      return GeckoResult.fromValue(prompt.dismiss());
    }
    return res;
  }

  @Override
  public GeckoResult<PromptResponse> onFilePrompt(GeckoSession session, FilePrompt prompt) {
    final Activity activity = mActivity;
    if (activity == null) {
      return GeckoResult.fromValue(prompt.dismiss());
    }

    if (prompt.type == FilePrompt.Type.FOLDER) {
      return onFolderPrompt(session, prompt);
    }

    // Merge all given MIME types into one, using wildcard if needed.
    String mimeType = null;
    String mimeSubtype = null;
    if (prompt.mimeTypes != null) {
      for (final String rawType : prompt.mimeTypes) {
        final String normalizedType = rawType.trim().toLowerCase(Locale.ROOT);
        final int len = normalizedType.length();
        int slash = normalizedType.indexOf('/');
        if (slash < 0) {
          slash = len;
        }
        final String newType = normalizedType.substring(0, slash);
        final String newSubtype = normalizedType.substring(Math.min(slash + 1, len));
        if (mimeType == null) {
          mimeType = newType;
        } else if (!mimeType.equals(newType)) {
          mimeType = "*";
        }
        if (mimeSubtype == null) {
          mimeSubtype = newSubtype;
        } else if (!mimeSubtype.equals(newSubtype)) {
          mimeSubtype = "*";
        }
      }
    }

    final Intent intent = new Intent(Intent.ACTION_GET_CONTENT);
    intent.setType(
        (mimeType != null ? mimeType : "*") + '/' + (mimeSubtype != null ? mimeSubtype : "*"));
    intent.addCategory(Intent.CATEGORY_OPENABLE);
    intent.putExtra(Intent.EXTRA_LOCAL_ONLY, true);
    if (prompt.type == FilePrompt.Type.MULTIPLE) {
      intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
    }
    if (prompt.mimeTypes.length > 0) {
      intent.putExtra(Intent.EXTRA_MIME_TYPES, prompt.mimeTypes);
    }

    GeckoResult<PromptResponse> res = new GeckoResult<PromptResponse>();

    try {
      mFileResponse = res;
      mFilePrompt = prompt;
      activity.startActivityForResult(intent, filePickerRequestCode);
    } catch (final ActivityNotFoundException e) {
      Log.e(LOGTAG, "Cannot launch activity", e);
      return GeckoResult.fromValue(prompt.dismiss());
    }

    return res;
  }

  public void onFileCallbackResult(final int resultCode, final Intent data) {
    if (mFileResponse == null) {
      return;
    }

    final GeckoResult<PromptResponse> res = mFileResponse;
    mFileResponse = null;

    final FilePrompt prompt = mFilePrompt;
    mFilePrompt = null;

    if (resultCode != Activity.RESULT_OK || data == null) {
      res.complete(prompt.dismiss());
      return;
    }

    Uri uri = data.getData();

    if (prompt.type == FilePrompt.Type.FOLDER) {
      res.complete(prompt.confirm(mActivity, uri));
      return;
    }

    final ClipData clip = data.getClipData();

    if (uri == null) return;
    if (!"file".equalsIgnoreCase(uri.getScheme())) {
      uri = toFileUri(mActivity.getBaseContext(), "/temps", uri);
      if (uri == null) return;
    }

    if (prompt.type == FilePrompt.Type.SINGLE
        || (prompt.type == FilePrompt.Type.MULTIPLE && clip == null)) {
      res.complete(prompt.confirm(mActivity, uri));
    } else if (prompt.type == FilePrompt.Type.MULTIPLE) {
      if (clip == null) {
        Log.w(LOGTAG, "No selected file");
        res.complete(prompt.dismiss());
        return;
      }
      final int count = clip.getItemCount();
      final ArrayList<Uri> uris = new ArrayList<>(count);
      for (int i = 0; i < count; i++) {
        uris.add(clip.getItemAt(i).getUri());
      }
      res.complete(prompt.confirm(mActivity, uris.toArray(new Uri[uris.size()])));
    }
  }

  /**
   * Copy the content of [this] [Uri] into a temporary file in the given [dirToCopy]
   *
   * @return A "file://" [Uri] which contains the content of [this] [Uri] or null if an issue
   *     occurs.
   */
  public Uri toFileUri(Context context, String dirToCopy, Uri uri) {
    File cacheDirectory = new File(context.getCacheDir(), dirToCopy);
    if (!cacheDirectory.exists() && !cacheDirectory.mkdirs()) return null;

    String fileName = getFileNameFromUri(context.getContentResolver(), uri);
    if (fileName == null)
      fileName = generateFileName(getFileExtFromUri(context.getContentResolver(), uri));

    File temporalFile = new File(cacheDirectory, fileName.replaceAll("[^a-zA-Z0-9._]+", "_"));
    if (copyUriToFile(context.getContentResolver(), uri, temporalFile))
      return Uri.fromFile(temporalFile);
    return null;
  }

  private static String generateFileName(String fileExtension) {
    String randomId = UUID.randomUUID().toString().replace("-", "");
    return fileExtension == null || fileExtension.isEmpty()
        ? randomId
        : randomId + "." + fileExtension;
  }

  private String getFileExtFromUri(ContentResolver contentResolver, Uri uri) {
    String mimeType = contentResolver.getType(uri);
    if (mimeType == null) return null;
    return MimeTypeMap.getSingleton().getExtensionFromMimeType(mimeType);
  }

  private String getFileNameFromUri(ContentResolver contentResolver, Uri uri) {
    try (Cursor cursor = contentResolver.query(uri, null, null, null, null)) {
      if (cursor != null && cursor.moveToFirst()) {
        int nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
        if (nameIndex >= 0) return cursor.getString(nameIndex);
      }
    }
    return null;
  }

  private boolean copyUriToFile(ContentResolver contentResolver, Uri uri, File temporalFile) {
    try (InputStream inputStream = contentResolver.openInputStream(uri);
        FileOutputStream output = new FileOutputStream(temporalFile)) {
      if (inputStream == null) return false;
      byte[] buffer = new byte[8192];
      int len;
      while ((len = inputStream.read(buffer)) != -1) {
        output.write(buffer, 0, len);
      }
      return true;
    } catch (Exception e) {
      return false;
    }
  }

  public GeckoResult<Integer> onPermissionPrompt(
      final GeckoSession session,
      final String title,
      final GeckoSession.PermissionDelegate.ContentPermission perm) {
    final Activity activity = mActivity;
    final GeckoResult<Integer> res = new GeckoResult<>();
    if (activity == null) {
      res.complete(GeckoSession.PermissionDelegate.ContentPermission.VALUE_PROMPT);
      return res;
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    builder
        .setTitle(title)
        .setNegativeButton(
            android.R.string.cancel,
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                res.complete(GeckoSession.PermissionDelegate.ContentPermission.VALUE_DENY);
              }
            })
        .setPositiveButton(
            android.R.string.ok,
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                res.complete(GeckoSession.PermissionDelegate.ContentPermission.VALUE_ALLOW);
              }
            });

    final AlertDialog dialog = builder.create();
    dialog.show();
    return res;
  }

  public void onSlowScriptPrompt(
      GeckoSession geckoSession, String title, GeckoResult<SlowScriptResponse> reportAction) {
    final Activity activity = mActivity;
    if (activity == null) {
      return;
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    builder
        .setTitle(title)
        .setNegativeButton(
            activity.getString(R.string.wait),
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                reportAction.complete(SlowScriptResponse.CONTINUE);
              }
            })
        .setPositiveButton(
            activity.getString(R.string.stop),
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                reportAction.complete(SlowScriptResponse.STOP);
              }
            });

    final AlertDialog dialog = builder.create();
    dialog.show();
  }

  private Spinner addMediaSpinner(
      final Context context,
      final ViewGroup container,
      final MediaSource[] sources,
      final String[] sourceNames) {
    final ArrayAdapter<MediaSource> adapter =
        new ArrayAdapter<MediaSource>(context, android.R.layout.simple_spinner_item) {
          private View convertView(final int position, final View view) {
            if (view != null) {
              final MediaSource item = getItem(position);
              ((TextView) view).setText(sourceNames != null ? sourceNames[position] : item.name);
            }
            return view;
          }

          @Override
          public View getView(final int position, View view, final ViewGroup parent) {
            return convertView(position, super.getView(position, view, parent));
          }

          @Override
          public View getDropDownView(final int position, final View view, final ViewGroup parent) {
            return convertView(position, super.getDropDownView(position, view, parent));
          }
        };
    adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
    adapter.addAll(sources);

    final Spinner spinner = new Spinner(context);
    spinner.setAdapter(adapter);
    spinner.setSelection(0);
    container.addView(spinner);
    return spinner;
  }

  public void onMediaPrompt(
      final GeckoSession session,
      final String title,
      final MediaSource[] video,
      final MediaSource[] audio,
      final String[] videoNames,
      final String[] audioNames,
      final GeckoSession.PermissionDelegate.MediaCallback callback) {
    final Activity activity = mActivity;
    if (activity == null || (video == null && audio == null)) {
      callback.reject();
      return;
    }
    final AlertDialog.Builder builder = new AlertDialog.Builder(activity);
    final LinearLayout container = addStandardLayout(builder, title, /* msg */ null);

    final Spinner videoSpinner;
    if (video != null) {
      videoSpinner = addMediaSpinner(builder.getContext(), container, video, videoNames);
    } else {
      videoSpinner = null;
    }

    final Spinner audioSpinner;
    if (audio != null) {
      audioSpinner = addMediaSpinner(builder.getContext(), container, audio, audioNames);
    } else {
      audioSpinner = null;
    }

    builder
        .setNegativeButton(android.R.string.cancel, /* listener */ null)
        .setPositiveButton(
            android.R.string.ok,
            new DialogInterface.OnClickListener() {
              @Override
              public void onClick(final DialogInterface dialog, final int which) {
                final MediaSource video =
                    (videoSpinner != null) ? (MediaSource) videoSpinner.getSelectedItem() : null;
                final MediaSource audio =
                    (audioSpinner != null) ? (MediaSource) audioSpinner.getSelectedItem() : null;
                callback.grant(video, audio);
              }
            });

    final AlertDialog dialog = builder.create();
    dialog.setOnDismissListener(
        new DialogInterface.OnDismissListener() {
          @Override
          public void onDismiss(final DialogInterface dialog) {
            callback.reject();
          }
        });
    dialog.show();
  }

  public void onMediaPrompt(
      final GeckoSession session,
      final String title,
      final MediaSource[] video,
      final MediaSource[] audio,
      final GeckoSession.PermissionDelegate.MediaCallback callback) {
    onMediaPrompt(session, title, video, audio, null, null, callback);
  }

  @Override
  public GeckoResult<PromptResponse> onPopupPrompt(
      final GeckoSession session, final PopupPrompt prompt) {
    return GeckoResult.fromValue(prompt.confirm(AllowOrDeny.ALLOW));
  }
}
