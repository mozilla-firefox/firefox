/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGNOMEShellSearchProvider.h"

#include "RemoteUtils.h"
#include "nsIStringBundle.h"
#include "nsServiceManagerUtils.h"
#include "nsPrintfCString.h"
#include "mozilla/XREAppData.h"
#include "nsAppRunner.h"
#include "nsImportModule.h"
#include "nsIOpenTabsProvider.h"

#define DBUS_BUS_NAME_TEMPLATE "org.mozilla.%s.SearchProvider"
#define DBUS_OBJECT_PATH_TEMPLATE "/org/mozilla/%s/SearchProvider"

const char* GetDBusBusName() {
  static const char* name = []() {
    nsAutoCString appName;
    gAppData->GetDBusAppName(appName);
    return ToNewCString(nsPrintfCString(DBUS_BUS_NAME_TEMPLATE,
                                        appName.get()));  // Intentionally leak
  }();
  return name;
}

const char* GetDBusObjectPath() {
  static const char* path = []() {
    nsAutoCString appName;
    gAppData->GetDBusAppName(appName);
    return ToNewCString(nsPrintfCString(DBUS_OBJECT_PATH_TEMPLATE,
                                        appName.get()));  // Intentionally leak
  }();
  return path;
}

static bool GetGnomeSearchTitle(const char* aSearchedTerm,
                                nsAutoCString& aGnomeSearchTitle) {
  static nsCOMPtr<nsIStringBundle> bundle;
  if (!bundle) {
    nsCOMPtr<nsIStringBundleService> sbs =
        do_GetService(NS_STRINGBUNDLE_CONTRACTID);
    if (NS_WARN_IF(!sbs)) {
      return false;
    }

    sbs->CreateBundle("chrome://browser/locale/browser.properties",
                      getter_AddRefs(bundle));
    if (NS_WARN_IF(!bundle)) {
      return false;
    }
  }

  AutoTArray<nsString, 1> formatStrings;
  CopyUTF8toUTF16(nsCString(aSearchedTerm), *formatStrings.AppendElement());

  nsAutoString gnomeSearchTitle;
  bundle->FormatStringFromName("gnomeSearchProviderSearchWeb", formatStrings,
                               gnomeSearchTitle);
  AppendUTF16toUTF8(gnomeSearchTitle, aGnomeSearchTitle);
  return true;
}

int DBusGetIndexFromIDKey(const char* aIDKey) {
  // ID is NN:S:URL where NN is index to our current history
  // result container.
  char tmp[] = {aIDKey[0], aIDKey[1], '\0'};
  return atoi(tmp);
}

char DBusGetStateFromIDKey(const char* aIDKey) {
  // ID is NN:S:URL where NN is index to our current history
  // result container, and S is the state, which can be 'o'pen or 'h'istory
  if (std::strlen(aIDKey) > 3) {
    return aIDKey[3];
  }
  // Should never happen, but just to avoid any possible segfault, we
  // default to state 'history'.
  return 'h';
}

static void ConcatArray(nsACString& aOutputStr, const char** aStringArray) {
  for (const char** term = aStringArray; *term; term++) {
    aOutputStr.Append(*term);
    if (*(term + 1)) {
      aOutputStr.Append(" ");
    }
  }
}

// GetInitialResultSet :: (as) → (as)
// GetSubsearchResultSet :: (as,as) → (as)
void DBusHandleResultSet(RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
                         GVariant* aParameters, bool aInitialSearch,
                         GDBusMethodInvocation* aReply) {
  // Inital search has params (as), any following one has (as,as) and we want
  // the second string array.
  fprintf(stderr, "%s\n", g_variant_get_type_string(aParameters));
  RefPtr<GVariant> variant = dont_AddRef(
      g_variant_get_child_value(aParameters, aInitialSearch ? 0 : 1));
  const char** stringArray = g_variant_get_strv(variant, nullptr);
  if (!stringArray) {
    g_dbus_method_invocation_return_error(
        aReply, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Wrong params!");
    return;
  }

  aSearchResult->SetReply(aReply);
  nsAutoCString searchTerm;
  ConcatArray(searchTerm, stringArray);
  aSearchResult->SetSearchTerm(searchTerm.get());
  GetGNOMEShellHistoryService()->QueryHistory(aSearchResult);
  // DBus reply will be send asynchronously by
  // nsGNOMEShellHistorySearchResult::SendDBusSearchResultReply()
  // when GetGNOMEShellHistoryService() has the results.

  g_free(stringArray);
}

/*
  "icon-data": a tuple of type (iiibiiay) describing a pixbuf with width,
              height, rowstride, has-alpha,
              bits-per-sample, channels,
              image data
*/
static void DBusAppendIcon(GVariantBuilder* aBuilder, GnomeHistoryIcon* aIcon) {
  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("(iiibiiay)"));
  g_variant_builder_add_value(&b, g_variant_new_int32(aIcon->GetWidth()));
  g_variant_builder_add_value(&b, g_variant_new_int32(aIcon->GetHeight()));
  g_variant_builder_add_value(&b, g_variant_new_int32(aIcon->GetWidth() * 4));
  g_variant_builder_add_value(&b, g_variant_new_boolean(true));
  g_variant_builder_add_value(&b, g_variant_new_int32(8));
  g_variant_builder_add_value(&b, g_variant_new_int32(4));
  g_variant_builder_add_value(
      &b, g_variant_new_fixed_array(G_VARIANT_TYPE("y"), aIcon->GetData(),
                                    aIcon->GetWidth() * aIcon->GetHeight() * 4,
                                    sizeof(char)));
  g_variant_builder_add(aBuilder, "{sv}", "icon-data",
                        g_variant_builder_end(&b));
}

/* Appends history search results to the DBUS reply.

  We can return those fields at GetResultMetas:

  "id": the result ID
  "name": the display name for the result
  "icon": a serialized GIcon (see g_icon_serialize()), or alternatively,
  "gicon": a textual representation of a GIcon (see g_icon_to_string()),
           or alternativly,
  "icon-data": a tuple of type (iiibiiay) describing a pixbuf with width,
              height, rowstride, has-alpha, bits-per-sample, and image data
  "description": an optional short description (1-2 lines)
*/
static already_AddRefed<GVariant> DBusAppendResultID(
    nsGNOMEShellHistorySearchResult* aSearchResult, const char* aID) {
  nsCOMPtr<nsINavHistoryContainerResultNode> container =
      aSearchResult->GetSearchResultContainer();

  int index = DBusGetIndexFromIDKey(aID);
  char state = DBusGetStateFromIDKey(aID);
  nsCOMPtr<nsINavHistoryResultNode> child;
  container->GetChild(index, getter_AddRefs(child));
  nsAutoCString title;
  if (!child || NS_FAILED(child->GetTitle(title))) {
    return nullptr;
  }

  if (title.IsEmpty()) {
    if (NS_FAILED(child->GetUri(title)) || title.IsEmpty()) {
      return nullptr;
    }
  }

  // Check if the URI state is "open tab". If so, mark it with an asterisk to
  // indicate this to the user.
  if (state == 'o') {
    title = "(*) "_ns + title;
  }

  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));

  const char* titleStr = title.get();
  g_variant_builder_add(&b, "{sv}", "id", g_variant_new_string(aID));
  g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(titleStr));

  GnomeHistoryIcon* icon = aSearchResult->GetHistoryIcon(index);
  if (icon) {
    DBusAppendIcon(&b, icon);
  } else {
    g_variant_builder_add(&b, "{sv}", "gicon",
                          g_variant_new_string("text-html"));
  }
  return dont_AddRef(g_variant_ref_sink(g_variant_builder_end(&b)));
}

// Search the web for: "searchTerm" to the DBUS reply.
static already_AddRefed<GVariant> DBusAppendSearchID(const char* aID) {
  /* aID contains:

     KEYWORD_SEARCH_STRING:ssssss

     KEYWORD_SEARCH_STRING is a 'special:search' keyword
     ssssss is a searched term, must be at least one character long
  */

  // aID contains only 'KEYWORD_SEARCH_STRING:' so we're missing searched
  // string.
  if (strlen(aID) <= KEYWORD_SEARCH_STRING_LEN + 1) {
    return nullptr;
  }

  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&b, "{sv}", "id",
                        g_variant_new_string(KEYWORD_SEARCH_STRING));

  // Extract ssssss part from aID
  nsAutoCString searchTerm(aID + KEYWORD_SEARCH_STRING_LEN + 1);
  nsAutoCString gnomeSearchTitle;
  if (GetGnomeSearchTitle(searchTerm.get(), gnomeSearchTitle)) {
    g_variant_builder_add(&b, "{sv}", "name",
                          g_variant_new_string(gnomeSearchTitle.get()));
    // TODO: When running on flatpak/snap we may need to use
    // icon like org.mozilla.Firefox or so.
    g_variant_builder_add(&b, "{sv}", "gicon", g_variant_new_string("firefox"));
  }

  return dont_AddRef(g_variant_ref_sink(g_variant_builder_end(&b)));
}

// GetResultMetas :: (as) → (aa{sv})
void DBusHandleResultMetas(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
    GVariant* aParameters, GDBusMethodInvocation* aReply) {
  RefPtr<GVariant> variant =
      dont_AddRef(g_variant_get_child_value(aParameters, 0));
  gsize elements;
  const char** stringArray = g_variant_get_strv(variant, &elements);
  if (!stringArray) {
    g_dbus_method_invocation_return_error(
        aReply, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Wrong params!");
    return;
  }

  GVariantBuilder b;
  g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));
  for (gsize i = 0; i < elements; i++) {
    RefPtr<GVariant> value;
    if (strncmp(stringArray[i], KEYWORD_SEARCH_STRING,
                KEYWORD_SEARCH_STRING_LEN) == 0) {
      value = DBusAppendSearchID(stringArray[i]);
    } else {
      value = DBusAppendResultID(aSearchResult, stringArray[i]);
    }
    if (value) {
      g_variant_builder_add_value(&b, value);
    }
  }

  GVariant* v = g_variant_builder_end(&b);
  g_dbus_method_invocation_return_value(aReply, g_variant_new_tuple(&v, 1));

  g_free(stringArray);
}  // namespace mozilla

static void ActivateResultID(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
    const char* aResultID, uint32_t aTimeStamp) {
  mozilla::UniquePtr<char[]> commandLine;
  int len;

  if (strncmp(aResultID, KEYWORD_SEARCH_STRING, KEYWORD_SEARCH_STRING_LEN) ==
      0) {
    const char* urlList[3] = {"unused", "--search",
                              aSearchResult->GetSearchTerm().get()};
    commandLine =
        ConstructCommandLine(std::size(urlList), urlList, nullptr, &len);
  } else {
    int keyIndex = atoi(aResultID);
    char state = DBusGetStateFromIDKey(aResultID);
    nsCOMPtr<nsINavHistoryResultNode> child;
    aSearchResult->GetSearchResultContainer()->GetChild(keyIndex,
                                                        getter_AddRefs(child));
    if (!child) {
      return;
    }

    nsAutoCString uri;
    nsresult rv = child->GetUri(uri);
    if (NS_FAILED(rv)) {
      return;
    }

    // If the state of the URI is 'o'pen, we send it along to JS and let
    // it switch the tab accordingly
    if (state == 'o') {
      // If we can't successfully switch to an open tab, use the existing
      // 'open in a new tab'-mechanism as a fallback.
      nsresult rv;
      nsCOMPtr<nsIOpenTabsProvider> provider = do_ImportESModule(
          "moz-src:///browser/components/shell/OpenTabsProvider.sys.mjs", &rv);
      if (NS_SUCCEEDED(rv)) {
        rv = provider->SwitchToOpenTab(uri);
        if (NS_SUCCEEDED(rv)) {
          return;
        }
      }
    }

    const char* urlList[2] = {"unused", uri.get()};
    commandLine =
        ConstructCommandLine(std::size(urlList), urlList, nullptr, &len);
  }

  if (commandLine) {
    aSearchResult->HandleCommandLine(mozilla::Span(commandLine.get(), len),
                                     aTimeStamp);
  }
}

static void DBusLaunchWithAllResults(
    RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
    uint32_t aTimeStamp) {
  uint32_t childCount = 0;
  nsresult rv =
      aSearchResult->GetSearchResultContainer()->GetChildCount(&childCount);
  if (NS_FAILED(rv) || childCount == 0) {
    return;
  }

  if (childCount > MAX_SEARCH_RESULTS_NUM) {
    childCount = MAX_SEARCH_RESULTS_NUM;
  }

  // Allocate space for all found results, "unused", "--search" and
  // potential search request.
  const char** urlList =
      (const char**)moz_xmalloc(sizeof(char*) * (childCount + 3));
  int urlListElements = 0;

  urlList[urlListElements++] = strdup("unused");

  for (uint32_t i = 0; i < childCount; i++) {
    nsCOMPtr<nsINavHistoryResultNode> child;
    aSearchResult->GetSearchResultContainer()->GetChild(i,
                                                        getter_AddRefs(child));

    if (!IsHistoryResultNodeURI(child)) {
      continue;
    }

    nsAutoCString uri;
    nsresult rv = child->GetUri(uri);
    if (NS_FAILED(rv)) {
      continue;
    }
    urlList[urlListElements++] = strdup(uri.get());
  }

  // When there isn't any uri to open pass search at least.
  if (urlListElements == 1) {
    urlList[urlListElements++] = strdup("--search");
    urlList[urlListElements++] = strdup(aSearchResult->GetSearchTerm().get());
  }

  int len;
  mozilla::UniquePtr<char[]> commandLine =
      ConstructCommandLine(urlListElements, urlList, nullptr, &len);
  if (commandLine) {
    aSearchResult->HandleCommandLine(mozilla::Span(commandLine.get(), len),
                                     aTimeStamp);
  }

  for (int i = 0; i < urlListElements; i++) {
    free((void*)urlList[i]);
  }
  free(urlList);
}

// ActivateResult :: (s,as,u) → ()
void DBusActivateResult(RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
                        GVariant* aParameters, GDBusMethodInvocation* aReply) {
  const char* resultID;

  // aParameters is "(s,as,u)" type
  RefPtr<GVariant> r = dont_AddRef(g_variant_get_child_value(aParameters, 0));
  if (!(resultID = g_variant_get_string(r, nullptr))) {
    g_dbus_method_invocation_return_error(
        aReply, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Wrong params!");
    return;
  }
  RefPtr<GVariant> t = dont_AddRef(g_variant_get_child_value(aParameters, 2));
  uint32_t timestamp = g_variant_get_uint32(t);

  ActivateResultID(aSearchResult, resultID, timestamp);
  g_dbus_method_invocation_return_value(aReply, nullptr);
}

// LaunchSearch :: (as,u) → ()
void DBusLaunchSearch(RefPtr<nsGNOMEShellHistorySearchResult> aSearchResult,
                      GVariant* aParameters, GDBusMethodInvocation* aReply) {
  RefPtr<GVariant> variant =
      dont_AddRef(g_variant_get_child_value(aParameters, 1));
  if (!variant) {
    g_dbus_method_invocation_return_error(
        aReply, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "Wrong params!");
    return;
  }
  DBusLaunchWithAllResults(aSearchResult, g_variant_get_uint32(variant));
  g_dbus_method_invocation_return_value(aReply, nullptr);
}

bool IsHistoryResultNodeURI(nsINavHistoryResultNode* aHistoryNode) {
  uint32_t type;
  nsresult rv = aHistoryNode->GetType(&type);
  if (NS_FAILED(rv) || type != nsINavHistoryResultNode::RESULT_TYPE_URI)
    return false;

  nsAutoCString title;
  rv = aHistoryNode->GetTitle(title);
  if (NS_SUCCEEDED(rv) && !title.IsEmpty()) {
    return true;
  }

  rv = aHistoryNode->GetUri(title);
  return NS_SUCCEEDED(rv) && !title.IsEmpty();
}
