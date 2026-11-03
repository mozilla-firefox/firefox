/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrintDialogGTK.h"

#include <gdk/gdk.h>  // for gdk_x11_window_get_xid
#include <gtk/gtk.h>
#include <gtk/gtkunixprint.h>
#include <stdlib.h>
#ifdef MOZ_X11
#  include <gdk/gdkx.h>

#  include "X11UndefineNone.h"
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "GRefPtr.h"
#include "MainThreadUtils.h"
#include "WidgetUtils.h"
#include "mozilla/dom/Promise.h"
#include "nsIGlobalObject.h"
#include "nsIPrintSettings.h"
#include "nsIPrintSettingsService.h"
#include "nsIStringBundle.h"
#include "nsIWidget.h"
#include "nsPIDOMWindow.h"
#include "nsPrintSettingsGTK.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::widget;

static const char header_footer_tags[][4] = {"", "&T", "&U", "&D", "&P", "&PT"};

#define CUSTOM_VALUE_INDEX gint(std::size(header_footer_tags))

static GtkWindow* get_gtk_window_for_nsiwidget(nsIWidget* widget) {
  return GTK_WINDOW(widget->GetNativeData(NS_NATIVE_SHELLWIDGET));
}

// Runs aDialog like gtk_dialog_run(), but returns GTK_RESPONSE_NONE if the
// dialog was destroyed (e.g. with its parent window) while it ran.
static gint RunDialog(GtkWidget* aDialog) {
  bool destroyed = false;
  g_signal_connect(aDialog, "destroy",
                   G_CALLBACK(+[](GtkWidget*, gpointer aDestroyed) {
                     *static_cast<bool*>(aDestroyed) = true;
                   }),
                   &destroyed);
  const gint response = gtk_dialog_run(GTK_DIALOG(aDialog));
  g_signal_handlers_disconnect_by_data(aDialog, &destroyed);
  return destroyed ? GTK_RESPONSE_NONE : response;
}

static void ShowCustomDialog(GtkComboBox* changed_box, gpointer user_data) {
  if (gtk_combo_box_get_active(changed_box) != CUSTOM_VALUE_INDEX) {
    g_object_set_data(G_OBJECT(changed_box), "previous-active",
                      GINT_TO_POINTER(gtk_combo_box_get_active(changed_box)));
    return;
  }

  GtkWindow* printDialog = GTK_WINDOW(user_data);
  nsCOMPtr<nsIStringBundleService> bundleSvc =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);

  nsCOMPtr<nsIStringBundle> printBundle;
  bundleSvc->CreateBundle("chrome://global/locale/printdialog.properties",
                          getter_AddRefs(printBundle));
  nsAutoString intlString;

  printBundle->GetStringFromName("headerFooterCustom", intlString);
  RefPtr<GtkWidget> prompt_dialog = gtk_dialog_new_with_buttons(
      NS_ConvertUTF16toUTF8(intlString).get(), printDialog,
      (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
      g_dgettext("gtk30", "_Cancel"), GTK_RESPONSE_REJECT,
      g_dgettext("gtk30", "_OK"), GTK_RESPONSE_ACCEPT, nullptr);
  gtk_dialog_set_default_response(GTK_DIALOG(prompt_dialog.get()),
                                  GTK_RESPONSE_ACCEPT);

  printBundle->GetStringFromName("customHeaderFooterPrompt", intlString);
  GtkWidget* custom_label =
      gtk_label_new(NS_ConvertUTF16toUTF8(intlString).get());
  GtkWidget* custom_entry = gtk_entry_new();
  GtkWidget* question_icon =
      gtk_image_new_from_icon_name("dialog-question", GTK_ICON_SIZE_DIALOG);

  // To be convenient, prefill the textbox with the existing value, if any, and
  // select it all so they can easily both edit it and type in a new one.
  const char* current_text =
      (const char*)g_object_get_data(G_OBJECT(changed_box), "custom-text");
  if (current_text) {
    gtk_entry_set_text(GTK_ENTRY(custom_entry), current_text);
    gtk_editable_select_region(GTK_EDITABLE(custom_entry), 0, -1);
  }
  gtk_entry_set_activates_default(GTK_ENTRY(custom_entry), TRUE);

  GtkWidget* custom_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_set_homogeneous(GTK_BOX(custom_vbox), TRUE);
  gtk_box_pack_start(GTK_BOX(custom_vbox), custom_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(custom_vbox), custom_entry, FALSE, FALSE,
                     5);  // Make entry 5px underneath label
  GtkWidget* custom_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
  gtk_box_pack_start(GTK_BOX(custom_hbox), question_icon, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(custom_hbox), custom_vbox, FALSE, FALSE,
                     10);  // Make question icon 10px away from content
  gtk_container_set_border_width(GTK_CONTAINER(custom_hbox), 2);
  gtk_widget_show_all(custom_hbox);

  gtk_box_pack_start(
      GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(prompt_dialog.get()))),
      custom_hbox, FALSE, FALSE, 0);
  gint diag_response = RunDialog(prompt_dialog);

  if (diag_response == GTK_RESPONSE_ACCEPT) {
    const gchar* response_text = gtk_entry_get_text(GTK_ENTRY(custom_entry));
    g_object_set_data_full(G_OBJECT(changed_box), "custom-text",
                           strdup(response_text), (GDestroyNotify)free);
    g_object_set_data(G_OBJECT(changed_box), "previous-active",
                      GINT_TO_POINTER(CUSTOM_VALUE_INDEX));
  } else {
    // Go back to the previous index
    gint previous_active = GPOINTER_TO_INT(
        g_object_get_data(G_OBJECT(changed_box), "previous-active"));
    gtk_combo_box_set_active(changed_box, previous_active);
  }

  gtk_widget_destroy(prompt_dialog);
}

class nsPrintDialogWidgetGTK {
 public:
  nsPrintDialogWidgetGTK(nsPIDOMWindowOuter* aParent, bool aHaveSelection,
                         nsIPrintSettings* aPrintSettings);
  ~nsPrintDialogWidgetGTK() { gtk_widget_destroy(dialog); }
  NS_ConvertUTF16toUTF8 GetUTF8FromBundle(const char* aKey);
  gint Run();

  nsresult ImportSettings(nsIPrintSettings* aNSSettings);
  nsresult ExportSettings(nsIPrintSettings* aNSSettings);

 private:
  RefPtr<GtkWidget> dialog;
  GtkWidget* shrink_to_fit_toggle;
  GtkWidget* print_bg_colors_toggle;
  GtkWidget* print_bg_images_toggle;
  GtkWidget* selection_only_toggle;
  // Owned by the dialog, not just by the grid they are attached to: GTK can
  // still use a combo box after the "changed" handler it invoked returns.
  RefPtr<GtkWidget> header_dropdown[3];  // {left, center, right}
  RefPtr<GtkWidget> footer_dropdown[3];

  nsCOMPtr<nsIStringBundle> printBundle;

  bool useNativeSelection;

  already_AddRefed<GtkWidget> ConstructHeaderFooterDropdown(
      const char16_t* currentString);
  const char* OptionWidgetToString(GtkWidget* dropdown);

  /* Code to copy between GTK and NS print settings structures.
   * In the following,
   *   "Import" means to copy from NS to GTK
   *   "Export" means to copy from GTK to NS
   */
  void ExportHeaderFooter(nsIPrintSettings* aNS);
};

nsPrintDialogWidgetGTK::nsPrintDialogWidgetGTK(nsPIDOMWindowOuter* aParent,
                                               bool aHaveSelection,
                                               nsIPrintSettings* aSettings) {
  nsCOMPtr<nsIWidget> widget = WidgetUtils::DOMWindowToWidget(aParent);
  NS_ASSERTION(widget, "Need a widget for dialog to be modal.");
  GtkWindow* gtkParent = get_gtk_window_for_nsiwidget(widget);
  NS_ASSERTION(gtkParent, "Need a GTK window for dialog to be modal.");

  nsCOMPtr<nsIStringBundleService> bundleSvc =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID);
  bundleSvc->CreateBundle("chrome://global/locale/printdialog.properties",
                          getter_AddRefs(printBundle));

  dialog = gtk_print_unix_dialog_new(GetUTF8FromBundle("printTitleGTK").get(),
                                     gtkParent);
  if (gtkParent) {
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog.get()), TRUE);
  }

  gtk_print_unix_dialog_set_manual_capabilities(
      GTK_PRINT_UNIX_DIALOG(dialog.get()),
      GtkPrintCapabilities(
          GTK_PRINT_CAPABILITY_COPIES | GTK_PRINT_CAPABILITY_COLLATE |
          GTK_PRINT_CAPABILITY_REVERSE | GTK_PRINT_CAPABILITY_SCALE |
          GTK_PRINT_CAPABILITY_GENERATE_PDF));

  // The vast majority of magic numbers in this widget construction are padding.
  // e.g. for the set_border_width below, 12px matches that of just about every
  // other window.
  GtkWidget* custom_options_tab = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_set_border_width(GTK_CONTAINER(custom_options_tab), 12);
  GtkWidget* tab_label =
      gtk_label_new(GetUTF8FromBundle("optionsTabLabelGTK").get());

  // Check buttons for shrink-to-fit and print selection
  GtkWidget* check_buttons_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_set_homogeneous(GTK_BOX(check_buttons_container), TRUE);
  shrink_to_fit_toggle = gtk_check_button_new_with_mnemonic(
      GetUTF8FromBundle("shrinkToFit").get());
  gtk_box_pack_start(GTK_BOX(check_buttons_container), shrink_to_fit_toggle,
                     FALSE, FALSE, 0);

  // GTK+2.18 and above allow us to add a "Selection" option to the main
  // settings screen, rather than adding an option on a custom tab like we must
  // do on older versions.
  if (gtk_major_version > 2 ||
      (gtk_major_version == 2 && gtk_minor_version >= 18)) {
    useNativeSelection = true;
    g_object_set(dialog, "support-selection", TRUE, "has-selection",
                 aHaveSelection, "embed-page-setup", TRUE, nullptr);
  } else {
    useNativeSelection = false;
    selection_only_toggle = gtk_check_button_new_with_mnemonic(
        GetUTF8FromBundle("selectionOnly").get());
    gtk_widget_set_sensitive(selection_only_toggle, aHaveSelection);
    gtk_box_pack_start(GTK_BOX(check_buttons_container), selection_only_toggle,
                       FALSE, FALSE, 0);
  }

  // Check buttons for printing background
  GtkWidget* appearance_buttons_container =
      gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_set_homogeneous(GTK_BOX(appearance_buttons_container), TRUE);
  print_bg_colors_toggle = gtk_check_button_new_with_mnemonic(
      GetUTF8FromBundle("printBGColors").get());
  print_bg_images_toggle = gtk_check_button_new_with_mnemonic(
      GetUTF8FromBundle("printBGImages").get());
  gtk_box_pack_start(GTK_BOX(appearance_buttons_container),
                     print_bg_colors_toggle, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(appearance_buttons_container),
                     print_bg_images_toggle, FALSE, FALSE, 0);
  g_object_set(appearance_buttons_container, "margin", 8, 0, 12, 0, NULL);

  // "Appearance" options label, bold and center-aligned
  GtkWidget* appearance_label = gtk_label_new(nullptr);
  char* pangoMarkup = g_markup_printf_escaped(
      "<b>%s</b>", GetUTF8FromBundle("printBGOptions").get());
  gtk_label_set_markup(GTK_LABEL(appearance_label), pangoMarkup);
  g_free(pangoMarkup);
  g_object_set(appearance_label, "xalign", 0, NULL);
  g_object_set(appearance_label, "yalign", 0, NULL);

  GtkWidget* appearance_vertical_squasher =
      gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(appearance_vertical_squasher), appearance_label,
                     FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(appearance_vertical_squasher),
                     appearance_buttons_container, FALSE, FALSE, 0);

  // "Header & Footer" options label, bold and center-aligned
  GtkWidget* header_footer_label = gtk_label_new(nullptr);
  pangoMarkup = g_markup_printf_escaped(
      "<b>%s</b>", GetUTF8FromBundle("headerFooter").get());
  gtk_label_set_markup(GTK_LABEL(header_footer_label), pangoMarkup);
  g_free(pangoMarkup);
  g_object_set(header_footer_label, "xalign", 0, NULL);
  g_object_set(header_footer_label, "valign", 0, NULL);

  // --- Table for making the header and footer options ---
  GtkWidget* header_footer_grid = gtk_grid_new();  // 3x3 table
  g_object_set(header_footer_grid, "margin", 8, 0, 12, 0, NULL);
  gtk_grid_set_row_spacing(GTK_GRID(header_footer_grid), 4);
  gtk_grid_set_column_spacing(GTK_GRID(header_footer_grid), 4);

  nsString header_footer_str[3];

  aSettings->GetHeaderStrLeft(header_footer_str[0]);
  aSettings->GetHeaderStrCenter(header_footer_str[1]);
  aSettings->GetHeaderStrRight(header_footer_str[2]);

  for (unsigned int i = 0; i < std::size(header_dropdown); i++) {
    header_dropdown[i] =
        ConstructHeaderFooterDropdown(header_footer_str[i].get());
    // The grid takes its own reference; ours stays in header_dropdown.
    // Those 4 magic numbers in the middle provide the position in the table.
    // The last two numbers mean 2 px padding on every side.
    gtk_grid_attach(GTK_GRID(header_footer_grid), header_dropdown[i].get(),
                    /* left = */ i,
                    /* top = */ 0, /* width = */ 1, /* height = */ 1);
  }

  const char labelKeys[][7] = {"left", "center", "right"};
  for (unsigned int i = 0; i < std::size(labelKeys); i++) {
    gtk_grid_attach(GTK_GRID(header_footer_grid),
                    gtk_label_new(GetUTF8FromBundle(labelKeys[i]).get()),
                    /* left = */ i,
                    /* top = */ 1, /* width = */ 1, /* height = */ 1);
  }

  aSettings->GetFooterStrLeft(header_footer_str[0]);
  aSettings->GetFooterStrCenter(header_footer_str[1]);
  aSettings->GetFooterStrRight(header_footer_str[2]);

  for (unsigned int i = 0; i < std::size(footer_dropdown); i++) {
    footer_dropdown[i] =
        ConstructHeaderFooterDropdown(header_footer_str[i].get());
    gtk_grid_attach(GTK_GRID(header_footer_grid), footer_dropdown[i].get(),
                    /* left = */ i,
                    /* top = */ 2, /* width = */ 1, /* height = */ 1);
  }
  // ---

  GtkWidget* header_footer_vertical_squasher =
      gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(header_footer_vertical_squasher),
                     header_footer_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(header_footer_vertical_squasher),
                     header_footer_grid, FALSE, FALSE, 0);

  // Construction of everything
  gtk_box_pack_start(GTK_BOX(custom_options_tab), check_buttons_container,
                     FALSE, FALSE, 10);  // 10px padding
  gtk_box_pack_start(GTK_BOX(custom_options_tab), appearance_vertical_squasher,
                     FALSE, FALSE, 10);
  gtk_box_pack_start(GTK_BOX(custom_options_tab),
                     header_footer_vertical_squasher, FALSE, FALSE, 0);

  gtk_print_unix_dialog_add_custom_tab(GTK_PRINT_UNIX_DIALOG(dialog.get()),
                                       custom_options_tab, tab_label);
  gtk_widget_show_all(custom_options_tab);
}

NS_ConvertUTF16toUTF8 nsPrintDialogWidgetGTK::GetUTF8FromBundle(
    const char* aKey) {
  nsAutoString intlString;
  printBundle->GetStringFromName(aKey, intlString);
  return NS_ConvertUTF16toUTF8(
      intlString);  // Return the actual object so we don't lose reference
}

const char* nsPrintDialogWidgetGTK::OptionWidgetToString(GtkWidget* dropdown) {
  gint index = gtk_combo_box_get_active(GTK_COMBO_BOX(dropdown));

  NS_ASSERTION(index <= CUSTOM_VALUE_INDEX,
               "Index of dropdown is higher than expected!");

  if (index == CUSTOM_VALUE_INDEX)
    return (const char*)g_object_get_data(G_OBJECT(dropdown), "custom-text");
  return header_footer_tags[index];
}

gint nsPrintDialogWidgetGTK::Run() {
  const gint response = RunDialog(dialog);
  gtk_widget_hide(dialog);
  return response;
}

void nsPrintDialogWidgetGTK::ExportHeaderFooter(nsIPrintSettings* aNS) {
  const char* header_footer_str;
  header_footer_str = OptionWidgetToString(header_dropdown[0].get());
  aNS->SetHeaderStrLeft(NS_ConvertUTF8toUTF16(header_footer_str));

  header_footer_str = OptionWidgetToString(header_dropdown[1].get());
  aNS->SetHeaderStrCenter(NS_ConvertUTF8toUTF16(header_footer_str));

  header_footer_str = OptionWidgetToString(header_dropdown[2].get());
  aNS->SetHeaderStrRight(NS_ConvertUTF8toUTF16(header_footer_str));

  header_footer_str = OptionWidgetToString(footer_dropdown[0].get());
  aNS->SetFooterStrLeft(NS_ConvertUTF8toUTF16(header_footer_str));

  header_footer_str = OptionWidgetToString(footer_dropdown[1].get());
  aNS->SetFooterStrCenter(NS_ConvertUTF8toUTF16(header_footer_str));

  header_footer_str = OptionWidgetToString(footer_dropdown[2].get());
  aNS->SetFooterStrRight(NS_ConvertUTF8toUTF16(header_footer_str));
}

nsresult nsPrintDialogWidgetGTK::ImportSettings(nsIPrintSettings* aNSSettings) {
  MOZ_ASSERT(aNSSettings, "aSettings must not be null");
  NS_ENSURE_TRUE(aNSSettings, NS_ERROR_FAILURE);

  nsCOMPtr<nsPrintSettingsGTK> aNSSettingsGTK(do_QueryInterface(aNSSettings));
  if (!aNSSettingsGTK) return NS_ERROR_FAILURE;

  GtkPrintSettings* settings = aNSSettingsGTK->GetGtkPrintSettings();
  GtkPageSetup* setup = aNSSettingsGTK->GetGtkPageSetup();

  // Set our custom fields:

  bool geckoBool;
  aNSSettings->GetShrinkToFit(&geckoBool);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(shrink_to_fit_toggle),
                               geckoBool);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(print_bg_colors_toggle),
                               aNSSettings->GetPrintBGColors());

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(print_bg_images_toggle),
                               aNSSettings->GetPrintBGImages());

  // Temporarily set the pages-per-sheet on the GtkPrintSettings:
  int32_t pagesPerSide = 1;
  aNSSettings->GetNumPagesPerSheet(&pagesPerSide);
  gtk_print_settings_set_number_up(settings, pagesPerSide);

  gtk_print_unix_dialog_set_settings(GTK_PRINT_UNIX_DIALOG(dialog.get()),
                                     settings);
  gtk_print_unix_dialog_set_page_setup(GTK_PRINT_UNIX_DIALOG(dialog.get()),
                                       setup);

  return NS_OK;
}

nsresult nsPrintDialogWidgetGTK::ExportSettings(nsIPrintSettings* aNSSettings) {
  MOZ_ASSERT(aNSSettings, "aSettings must not be null");
  NS_ENSURE_TRUE(aNSSettings, NS_ERROR_FAILURE);

  GtkPrintSettings* settings =
      gtk_print_unix_dialog_get_settings(GTK_PRINT_UNIX_DIALOG(dialog.get()));
  GtkPageSetup* setup =
      gtk_print_unix_dialog_get_page_setup(GTK_PRINT_UNIX_DIALOG(dialog.get()));
  GtkPrinter* printer = gtk_print_unix_dialog_get_selected_printer(
      GTK_PRINT_UNIX_DIALOG(dialog.get()));
  if (settings && setup && printer) {
    ExportHeaderFooter(aNSSettings);

    aNSSettings->SetOutputFormat(nsIPrintSettings::kOutputFormatNative);

    // Print-to-file is true by default. This must be turned off or else
    // printing won't occur! (We manually copy the spool file when this flag is
    // set, because we love our embedders) Even if it is print-to-file in GTK's
    // case, GTK does The Right Thing when we send the job.
    aNSSettings->SetOutputDestination(
        nsIPrintSettings::kOutputDestinationPrinter);

    aNSSettings->SetShrinkToFit(
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(shrink_to_fit_toggle)));

    aNSSettings->SetPrintBGColors(gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(print_bg_colors_toggle)));
    aNSSettings->SetPrintBGImages(gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(print_bg_images_toggle)));

    // Let GTK deal with pages per sheet natively.
    aNSSettings->SetNumPagesPerSheet(1);
    // Try to save native settings in the session object
    nsCOMPtr<nsPrintSettingsGTK> aNSSettingsGTK(do_QueryInterface(aNSSettings));
    if (aNSSettingsGTK) {
      aNSSettingsGTK->SetGtkPrintSettings(settings);
      aNSSettingsGTK->SetGtkPageSetup(setup);
      aNSSettingsGTK->SetGtkPrinter(printer);
      bool printSelectionOnly;
      if (useNativeSelection) {
        _GtkPrintPages pageSetting =
            (_GtkPrintPages)gtk_print_settings_get_print_pages(settings);
        printSelectionOnly = (pageSetting == _GTK_PRINT_PAGES_SELECTION);
      } else {
        printSelectionOnly = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(selection_only_toggle));
      }
      aNSSettingsGTK->SetPrintSelectionOnly(printSelectionOnly);
    }
  }

  if (settings) g_object_unref(settings);
  return NS_OK;
}

already_AddRefed<GtkWidget>
nsPrintDialogWidgetGTK::ConstructHeaderFooterDropdown(
    const char16_t* currentString) {
  GtkWidget* dropdown = gtk_combo_box_text_new();
  // Take the floating reference now so the caller owns what it gets back.
  g_object_ref_sink(dropdown);
  const char hf_options[][22] = {"headerFooterBlank", "headerFooterTitle",
                                 "headerFooterURL",   "headerFooterDate",
                                 "headerFooterPage",  "headerFooterPageTotal",
                                 "headerFooterCustom"};

  for (const auto& hf_option : hf_options) {
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(dropdown), nullptr,
                              GetUTF8FromBundle(hf_option).get());
  }

  bool shouldBeCustom = true;
  NS_ConvertUTF16toUTF8 currentStringUTF8(currentString);

  for (unsigned int i = 0; i < std::size(header_footer_tags); i++) {
    if (!strcmp(currentStringUTF8.get(), header_footer_tags[i])) {
      gtk_combo_box_set_active(GTK_COMBO_BOX(dropdown), i);
      g_object_set_data(G_OBJECT(dropdown), "previous-active",
                        GINT_TO_POINTER(i));
      shouldBeCustom = false;
      break;
    }
  }

  if (shouldBeCustom) {
    gtk_combo_box_set_active(GTK_COMBO_BOX(dropdown), CUSTOM_VALUE_INDEX);
    g_object_set_data(G_OBJECT(dropdown), "previous-active",
                      GINT_TO_POINTER(CUSTOM_VALUE_INDEX));
    char* custom_string = strdup(currentStringUTF8.get());
    g_object_set_data_full(G_OBJECT(dropdown), "custom-text", custom_string,
                           (GDestroyNotify)free);
  }

  g_signal_connect(dropdown, "changed", (GCallback)ShowCustomDialog, dialog);
  return dont_AddRef(dropdown);
}

NS_IMPL_ISUPPORTS(nsPrintDialogServiceGTK, nsIPrintDialogService)

nsPrintDialogServiceGTK::nsPrintDialogServiceGTK() = default;

nsPrintDialogServiceGTK::~nsPrintDialogServiceGTK() = default;

NS_IMETHODIMP
nsPrintDialogServiceGTK::Init() { return NS_OK; }

NS_IMETHODIMP
nsPrintDialogServiceGTK::ShowPrintDialog(mozIDOMWindowProxy* aParent,
                                         bool aHaveSelection,
                                         nsIPrintSettings* aSettings,
                                         JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aParent);
  NS_ENSURE_ARG(aSettings);
  NS_ENSURE_ARG(aCx);
  NS_ENSURE_ARG(aPromise);

  ErrorResult rvErr;
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  RefPtr<Promise> promise = Promise::Create(global, rvErr);
  if (NS_WARN_IF(rvErr.Failed())) {
    return rvErr.StealNSResult();
  }

  nsPrintDialogWidgetGTK printDialog(nsPIDOMWindowOuter::From(aParent),
                                     aHaveSelection, aSettings);
  nsresult rv = printDialog.ImportSettings(aSettings);

  if (NS_FAILED(rv)) {
    promise->MaybeReject(rv);
    promise.forget(aPromise);
    return NS_OK;
  }

  const gint response = printDialog.Run();

  // Handle the result
  switch (response) {
    case GTK_RESPONSE_OK:  // Proceed
      rv = printDialog.ExportSettings(aSettings);
      break;

    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_NONE:
      rv = NS_ERROR_ABORT;
      break;

    case GTK_RESPONSE_APPLY:  // Print preview
    default:
      NS_WARNING("Unexpected response");
      rv = NS_ERROR_ABORT;
  }

  if (NS_SUCCEEDED(rv)) {
    promise->MaybeResolveWithUndefined();
  } else {
    promise->MaybeReject(rv);
  }
  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsPrintDialogServiceGTK::ShowPageSetupDialog(mozIDOMWindowProxy* aParent,
                                             nsIPrintSettings* aNSSettings,
                                             JSContext* aCx,
                                             Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG(aParent);
  NS_ENSURE_ARG(aNSSettings);
  NS_ENSURE_ARG(aCx);
  NS_ENSURE_ARG(aPromise);

  ErrorResult rvErr;
  nsCOMPtr<nsIGlobalObject> global = xpc::CurrentNativeGlobal(aCx);
  RefPtr<Promise> promise = Promise::Create(global, rvErr);
  if (NS_WARN_IF(rvErr.Failed())) {
    return rvErr.StealNSResult();
  }

  nsCOMPtr<nsIWidget> widget =
      WidgetUtils::DOMWindowToWidget(nsPIDOMWindowOuter::From(aParent));
  NS_ASSERTION(widget, "Need a widget for dialog to be modal.");
  GtkWindow* gtkParent = get_gtk_window_for_nsiwidget(widget);
  NS_ASSERTION(gtkParent, "Need a GTK window for dialog to be modal.");

  nsCOMPtr<nsPrintSettingsGTK> aNSSettingsGTK(do_QueryInterface(aNSSettings));
  if (!aNSSettingsGTK) {
    promise->MaybeReject(NS_ERROR_FAILURE);
    promise.forget(aPromise);
    return NS_OK;
  }

  // We need to init the prefs here because aNSSettings in its current form is a
  // dummy in both uses of the word
  nsCOMPtr<nsIPrintSettingsService> psService =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (psService) {
    nsString printName;
    aNSSettings->GetPrinterName(printName);
    if (printName.IsVoid()) {
      psService->GetLastUsedPrinterName(printName);
      aNSSettings->SetPrinterName(printName);
    }
    psService->InitPrintSettingsFromPrefs(aNSSettings, true,
                                          nsIPrintSettings::kInitSaveAll);
  }

  // Frustratingly, gtk_print_run_page_setup_dialog doesn't tell us whether
  // the user cancelled or confirmed the dialog! So to avoid needlessly
  // refreshing the preview when Page Setup was cancelled, we compare the
  // serializations of old and new settings; if they're the same, bail out.
  GtkPrintSettings* gtkSettings = aNSSettingsGTK->GetGtkPrintSettings();
  GtkPageSetup* oldPageSetup = aNSSettingsGTK->GetGtkPageSetup();
  GKeyFile* oldKeyFile = g_key_file_new();
  gtk_page_setup_to_key_file(oldPageSetup, oldKeyFile, nullptr);
  gsize oldLength;
  gchar* oldData = g_key_file_to_data(oldKeyFile, &oldLength, nullptr);
  g_key_file_free(oldKeyFile);

  GtkPageSetup* newPageSetup =
      gtk_print_run_page_setup_dialog(gtkParent, oldPageSetup, gtkSettings);

  GKeyFile* newKeyFile = g_key_file_new();
  gtk_page_setup_to_key_file(newPageSetup, newKeyFile, nullptr);
  gsize newLength;
  gchar* newData = g_key_file_to_data(newKeyFile, &newLength, nullptr);
  g_key_file_free(newKeyFile);

  bool unchanged =
      (oldLength == newLength && !memcmp(oldData, newData, oldLength));
  g_free(oldData);
  g_free(newData);
  if (unchanged) {
    g_object_unref(newPageSetup);
    promise->MaybeReject(NS_ERROR_ABORT);
    promise.forget(aPromise);
    return NS_OK;
  }

  aNSSettingsGTK->SetGtkPageSetup(newPageSetup);

  // Now newPageSetup has a refcount of 2 (SetGtkPageSetup will addref), put it
  // to 1 so if this gets replaced we don't leak.
  g_object_unref(newPageSetup);

  if (psService)
    psService->MaybeSavePrintSettingsToPrefs(
        aNSSettings, nsIPrintSettings::kInitSaveOrientation |
                         nsIPrintSettings::kInitSavePaperSize |
                         nsIPrintSettings::kInitSaveUnwriteableMargins);

  promise->MaybeResolveWithUndefined();
  promise.forget(aPromise);
  return NS_OK;
}
