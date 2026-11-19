/******* BEGIN LICENSE BLOCK *******
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Initial Developers of the Original Code are Kevin Hendricks (MySpell)
 * and László Németh (Hunspell). Portions created by the Initial Developers
 * are Copyright (C) 2002-2005 the Initial Developers. All Rights Reserved.
 *
 * Contributor(s): Kevin Hendricks (kevin.hendricks@sympatico.ca)
 *                 David Einstein (deinst@world.std.com)
 *                 Michiel van Leeuwen (mvl@exedo.nl)
 *                 Caolan McNamara (cmc@openoffice.org)
 *                 László Németh (nemethl@gyorsposta.hu)
 *                 Davide Prina
 *                 Giuseppe Modugno
 *                 Gianluca Turconi
 *                 Simon Brouwer
 *                 Noll Janos
 *                 Biro Arpad
 *                 Goldman Eleonora
 *                 Sarlos Tamas
 *                 Bencsath Boldizsar
 *                 Halacsy Peter
 *                 Dvornik Laszlo
 *                 Gefferth Andras
 *                 Nagy Viktor
 *                 Varga Daniel
 *                 Chris Halls
 *                 Rene Engelhard
 *                 Bram Moolenaar
 *                 Dafydd Jones
 *                 Harri Pitkanen
 *                 Andras Timar
 *                 Tor Lillqvist
 *                 Jesper Kristensen (mail@jesperkristensen.dk)
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 ******* END LICENSE BLOCK *******/

#include "mozHunspell.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsIObserverService.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIFile.h"
#include "nsUnicharUtils.h"
#include "nsCRT.h"
#include "mozInlineSpellChecker.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsNetUtil.h"
#include "prenv.h"
#include "mozilla/Components.h"
#include "mozilla/Services.h"
#include "mozilla/dom/ContentParent_NotifyUpdatedDictionaries.h"

#include <tuple>

using namespace mozilla;

NS_IMPL_CYCLE_COLLECTING_ADDREF(mozHunspell)
NS_IMPL_CYCLE_COLLECTING_RELEASE(mozHunspell)

NS_INTERFACE_MAP_BEGIN(mozHunspell)
  NS_INTERFACE_MAP_ENTRY(mozISpellCheckingEngine)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIMemoryReporter)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, mozISpellCheckingEngine)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(mozHunspell)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_WEAK(mozHunspell, mPersonalDictionary)

NS_IMPL_COMPONENT_FACTORY(mozHunspell) {
  auto hunspell = MakeRefPtr<mozHunspell>();
  if (NS_SUCCEEDED(hunspell->Init())) {
    return hunspell.forget().downcast<mozISpellCheckingEngine>();
  }
  return nullptr;
}

mozHunspell::mozHunspell() {
#ifdef DEBUG
  // There must be only one instance of this class: it reports memory based on
  // a single static count in HunspellAllocator.
  static bool hasRun = false;
  MOZ_ASSERT(!hasRun);
  hasRun = true;
#endif
}

nsresult mozHunspell::Init() {
  LoadDictionaryList(false);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, "profile-do-change", true);
    obs->AddObserver(this, "profile-after-change", true);
  }

  mozilla::RegisterWeakMemoryReporter(this);

  return NS_OK;
}

mozHunspell::~mozHunspell() {
  mozilla::UnregisterWeakMemoryReporter(this);

  mPersonalDictionary = nullptr;
  mHunspells.Clear();
}

NS_IMETHODIMP
mozHunspell::GetDictionaries(nsTArray<nsCString>& aDictionaries) {
  MOZ_ASSERT(aDictionaries.IsEmpty());
  for (auto iter = mHunspells.ConstIter(); !iter.Done(); iter.Next()) {
    if (iter.Data().mEnabled) {
      aDictionaries.AppendElement(iter.Key());
    }
  }
  return NS_OK;
}

/* Set the Dictionaries.
 * This also Loads the dictionaries and initializes the converter using the
 * dictionaries converter
 */
NS_IMETHODIMP
mozHunspell::SetDictionaries(const nsTArray<nsCString>& aDictionaries) {
  if (aDictionaries.IsEmpty()) {
    mHunspells.Clear();
    return NS_OK;
  }

  // Disable any dictionaries we've already loaded that we're not
  // going to use.
  for (auto iter = mHunspells.Iter(); !iter.Done(); iter.Next()) {
    if (!aDictionaries.Contains(iter.Key())) {
      iter.Data().mEnabled = false;
    }
  }

  bool firstDictionary = true;
  for (const auto& dictionary : aDictionaries) {
    NS_ConvertUTF8toUTF16 dict(dictionary);
    nsIURI* affFile = mDictionaries.GetWeak(dict);
    if (!affFile) {
      return NS_ERROR_FILE_NOT_FOUND;
    }

    nsAutoCString affFileName;
    nsresult rv = affFile->GetSpec(affFileName);
    NS_ENSURE_SUCCESS(rv, rv);

    if (auto entry = mHunspells.Lookup(dictionary)) {
      if (entry.Data().mAffixFileName == affFileName) {
        entry.Data().mEnabled = true;
        continue;
      }
    }

    DictionaryData dictionaryData;
    dictionaryData.mAffixFileName = affFileName;

    // Load the first dictionary now, we'll load the others lazily during
    // checking.
    if (firstDictionary) {
      rv = dictionaryData.LoadIfNecessary();
      NS_ENSURE_SUCCESS(rv, rv);
      firstDictionary = false;
    }

    mHunspells.InsertOrUpdate(dictionary, std::move(dictionaryData));
  }

  // If we have a large number of dictionaries loaded, try freeing any disabled
  // dictionaries to limit memory use.
  if (mHunspells.Count() > 10) {
    mHunspells.RemoveIf([](const auto& iter) { return !iter.Data().mEnabled; });
  }

  return NS_OK;
}

NS_IMETHODIMP mozHunspell::GetPersonalDictionary(
    mozIPersonalDictionary** aPersonalDictionary) {
  *aPersonalDictionary = mPersonalDictionary;
  NS_IF_ADDREF(*aPersonalDictionary);
  return NS_OK;
}

NS_IMETHODIMP mozHunspell::SetPersonalDictionary(
    mozIPersonalDictionary* aPersonalDictionary) {
  mPersonalDictionary = aPersonalDictionary;
  return NS_OK;
}

NS_IMETHODIMP mozHunspell::GetDictionaryList(
    nsTArray<nsCString>& aDictionaries) {
  MOZ_ASSERT(aDictionaries.IsEmpty());
  for (const auto& key : mDictionaries.Keys()) {
    aDictionaries.AppendElement(NS_ConvertUTF16toUTF8(key));
  }

  return NS_OK;
}

void mozHunspell::LoadDictionaryList(bool aNotifyChildProcesses) {
  mDictionaries.Clear();

  nsresult rv;

  // find built in dictionaries, or dictionaries specified in
  // spellchecker.dictionary_path in prefs
  nsCOMPtr<nsIFile> dictDir;

  // check preferences first
  nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefs) {
    nsAutoCString extDictPath;
    rv = prefs->GetCharPref("spellchecker.dictionary_path", extDictPath);
    if (NS_SUCCEEDED(rv)) {
      // set the spellchecker.dictionary_path
      rv = NS_NewNativeLocalFile(extDictPath, getter_AddRefs(dictDir));
    }
    if (dictDir) {
      LoadDictionariesFromDir(dictDir);
    }
  }

  // find dictionaries in DICPATH
  char* dicEnv = PR_GetEnv("DICPATH");
  if (dicEnv) {
    // do a two-pass dance so dictionaries are loaded right-to-left as
    // preference
    nsTArray<nsCOMPtr<nsIFile>> dirs;
    nsAutoCString env(dicEnv);  // assume dicEnv is UTF-8

    char* currPath = nullptr;
    char* nextPaths = env.BeginWriting();
    while ((currPath = NS_strtok(":", &nextPaths))) {
      nsCOMPtr<nsIFile> dir;
      rv = NS_NewNativeLocalFile(nsCString(currPath), getter_AddRefs(dir));
      if (NS_SUCCEEDED(rv)) {
        dirs.AppendElement(dir);
      }
    }

    // load them in reverse order so they override each other properly
    for (int32_t i = dirs.Length() - 1; i >= 0; i--) {
      LoadDictionariesFromDir(dirs[i]);
    }
  }

  // find dictionaries from restartless extensions
  for (int32_t i = 0; i < mDynamicDirectories.Count(); i++) {
    LoadDictionariesFromDir(mDynamicDirectories[i]);
  }

  for (const auto& dictionaryEntry : mDynamicDictionaries) {
    mDictionaries.InsertOrUpdate(dictionaryEntry.GetKey(),
                                 dictionaryEntry.GetData());
  }

  DictionariesChanged(aNotifyChildProcesses);
}

void mozHunspell::DictionariesChanged(bool aNotifyChildProcesses) {
  // Now we have finished updating the list of dictionaries, update the current
  // dictionary and any editors which may use it.
  mozInlineSpellChecker::UpdateCanEnableInlineSpellChecking();

  if (aNotifyChildProcesses) {
    mozilla::dom::ContentParent_NotifyUpdatedDictionaries();
  }

  // Check if the current dictionaries are still available.
  // If not, try to replace it with other dictionaries of the same language.
  if (!mHunspells.IsEmpty()) {
    nsTArray<nsCString> dictionaries;
    for (auto iter = mHunspells.ConstIter(); !iter.Done(); iter.Next()) {
      if (iter.Data().mEnabled) {
        dictionaries.AppendElement(iter.Key());
      }
    }
    nsresult rv = SetDictionaries(dictionaries);
    if (NS_SUCCEEDED(rv)) return;
  }

  // If the current dictionaries are gone, and we don't have a good replacement,
  // set no current dictionary.
  if (!mHunspells.IsEmpty()) {
    nsTArray<nsCString> empty;
    SetDictionaries(empty);
  }
}

NS_IMETHODIMP
mozHunspell::LoadDictionariesFromDir(nsIFile* aDir) {
  nsresult rv;

  bool check = false;
  rv = aDir->Exists(&check);
  if (NS_FAILED(rv) || !check) return NS_ERROR_UNEXPECTED;

  rv = aDir->IsDirectory(&check);
  if (NS_FAILED(rv) || !check) return NS_ERROR_UNEXPECTED;

  nsCOMPtr<nsIDirectoryEnumerator> files;
  rv = aDir->GetDirectoryEntries(getter_AddRefs(files));
  if (NS_FAILED(rv)) return NS_ERROR_UNEXPECTED;

  nsCOMPtr<nsIFile> file;
  while (NS_SUCCEEDED(files->GetNextFile(getter_AddRefs(file))) && file) {
    nsAutoString leafName;
    file->GetLeafName(leafName);
    if (!StringEndsWith(leafName, u".dic"_ns)) continue;

    nsAutoString dict(leafName);
    dict.SetLength(dict.Length() - 4);  // magic length of ".dic"

    // check for the presence of the .aff file
    leafName = dict;
    leafName.AppendLiteral(".aff");
    file->SetLeafName(leafName);
    rv = file->Exists(&check);
    if (NS_FAILED(rv) || !check) continue;

    // Replace '_' separator with '-'
    dict.ReplaceChar('_', '-');

    nsCOMPtr<nsIURI> uri;
    rv = NS_NewFileURI(getter_AddRefs(uri), file);
    NS_ENSURE_SUCCESS(rv, rv);

    mDictionaries.InsertOrUpdate(dict, uri);
  }

  return NS_OK;
}

nsresult mozHunspell::DictionaryData::ConvertCharset(const nsAString& aStr,
                                                     std::string& aDst) {
  if (NS_WARN_IF(!mEncoder)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  auto src = Span(aStr.BeginReading(), aStr.Length());
  CheckedInt<size_t> needed =
      mEncoder->MaxBufferLengthFromUTF16WithoutReplacement(src.Length());
  if (!needed.isValid()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  aDst.resize(needed.value());

  char* dstPtr = &aDst[0];
  auto dst = Span(reinterpret_cast<uint8_t*>(dstPtr), needed.value());

  uint32_t result;
  size_t written;
  std::tie(result, std::ignore, written) =
      mEncoder->EncodeFromUTF16WithoutReplacement(src, dst, true);
  MOZ_ASSERT(result != kOutputFull);
  if (result != kInputEmpty) {
    return NS_ERROR_UENC_NOMAPPING;
  }
  aDst.resize(written);
  mEncoder->Encoding()->NewEncoderInto(*mEncoder);
  return NS_OK;
}

nsresult mozHunspell::DictionaryData::LoadIfNecessary() {
  if (mHunspell && mEncoder && mDecoder) {
    return NS_OK;
  }

  if (mLoadFailed) {
    return NS_ERROR_FAILURE;
  }

  nsCString dictFileName = mAffixFileName;
  int32_t dotPos = dictFileName.RFindChar('.');
  if (dotPos == -1) {
    mLoadFailed = true;
    return NS_ERROR_FAILURE;
  }
  dictFileName.SetLength(dotPos);
  dictFileName.AppendLiteral(".dic");

  UniquePtr<RLBoxHunspell> hunspell(
      RLBoxHunspell::Create(mAffixFileName, dictFileName));
  if (!hunspell) {
    mLoadFailed = true;
    // TODO Bug 1788857: Verify error propagation in case of inaccessible file
    return NS_ERROR_OUT_OF_MEMORY;
  }
  mHunspell = std::move(hunspell);
  auto encoding =
      Encoding::ForLabelNoReplacement(mHunspell->get_dict_encoding());
  if (!encoding) {
    mLoadFailed = true;
    return NS_ERROR_UCONV_NOCONV;
  }
  mEncoder = encoding->NewEncoder();
  mDecoder = encoding->NewDecoderWithoutBOMHandling();
  return NS_OK;
}

NS_IMETHODIMP
mozHunspell::CollectReports(nsIHandleReportCallback* aHandleReport,
                            nsISupports* aData, bool /*aAnonymize*/) {
  MOZ_COLLECT_REPORT("explicit/spell-check", KIND_HEAP, UNITS_BYTES,
                     HunspellAllocator::MemoryAllocated(),
                     "Memory used by the spell-checking engine.");

  return NS_OK;
}

NS_IMETHODIMP
mozHunspell::Check(const nsAString& aWord, bool* aResult) {
  if (NS_WARN_IF(!aResult)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(mHunspells.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  *aResult = true;
  for (auto iter = mHunspells.Iter(); !iter.Done(); iter.Next()) {
    if (!iter.Data().mEnabled) {
      continue;
    }

    nsresult rv = iter.Data().LoadIfNecessary();
    if (NS_FAILED(rv)) {
      continue;
    }

    std::string charsetWord;
    rv = iter.Data().ConvertCharset(aWord, charsetWord);
    if (NS_FAILED(rv)) {
      continue;
    }

    // Depending upon the encoding, we might end up with a string that begins
    // with the null byte. Since the hunspell interface uses C-style strings,
    // this appears like an empty string, and hunspell marks empty strings as
    // spelled correctly. Skip these cases to allow another dictionary to have
    // the chance to spellcheck them.
    if (charsetWord.empty() || charsetWord[0] == 0) {
      continue;
    }

    *aResult = iter.Data().mHunspell->spell(charsetWord);
    if (*aResult) {
      break;
    }
  }

  if (!*aResult && mPersonalDictionary) {
    return mPersonalDictionary->Check(aWord, aResult);
  }

  return NS_OK;
}

NS_IMETHODIMP
mozHunspell::Suggest(const nsAString& aWord, nsTArray<nsString>& aSuggestions) {
  if (NS_WARN_IF(mHunspells.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(aSuggestions.IsEmpty());

  for (auto iter = mHunspells.Iter(); !iter.Done(); iter.Next()) {
    if (!iter.Data().mEnabled) {
      continue;
    }

    nsresult rv = iter.Data().LoadIfNecessary();
    if (NS_FAILED(rv)) {
      continue;
    }

    std::string charsetWord;
    rv = iter.Data().ConvertCharset(aWord, charsetWord);
    NS_ENSURE_SUCCESS(rv, rv);

    std::vector<std::string> suggestions =
        iter.Data().mHunspell->suggest(charsetWord);
    if (!suggestions.empty()) {
      aSuggestions.SetCapacity(aSuggestions.Length() + suggestions.size());
      for (Span<const char> charSrc : suggestions) {
        // Convert the suggestion to utf16
        auto src = AsBytes(charSrc);
        nsresult rv =
            iter.Data().mDecoder->Encoding()->DecodeWithoutBOMHandling(
                src, *aSuggestions.AppendElement());
        NS_ENSURE_SUCCESS(rv, rv);
        iter.Data().mDecoder->Encoding()->NewDecoderWithoutBOMHandlingInto(
            *iter.Data().mDecoder);
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
mozHunspell::Observe(nsISupports* /*aSubj*/, const char* aTopic,
                     const char16_t* /*aData*/) {
  NS_ASSERTION(!strcmp(aTopic, "profile-do-change") ||
                   !strcmp(aTopic, "profile-after-change"),
               "Unexpected observer topic");

  LoadDictionaryList(false);

  return NS_OK;
}

NS_IMETHODIMP mozHunspell::AddDirectory(nsIFile* aDir) {
  mDynamicDirectories.AppendObject(aDir);
  LoadDictionaryList(true);
  return NS_OK;
}

NS_IMETHODIMP mozHunspell::RemoveDirectory(nsIFile* aDir) {
  mDynamicDirectories.RemoveObject(aDir);
  LoadDictionaryList(true);

#ifdef MOZ_THUNDERBIRD
  /*
   * This notification is needed for Thunderbird. Thunderbird derives the
   * dictionary from the document's "lang" attribute. If a dictionary is
   * removed, we need to change the "lang" attribute.
   */
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, SPELLCHECK_DICTIONARY_REMOVE_NOTIFICATION,
                         nullptr);
  }
#endif
  return NS_OK;
}

NS_IMETHODIMP mozHunspell::AddDictionary(const nsAString& aLang,
                                         nsIURI* aFile) {
  NS_ENSURE_TRUE(aFile, NS_ERROR_INVALID_ARG);

  mDynamicDictionaries.InsertOrUpdate(aLang, aFile);
  mDictionaries.InsertOrUpdate(aLang, aFile);
  DictionariesChanged(true);
  return NS_OK;
}

NS_IMETHODIMP mozHunspell::RemoveDictionary(const nsAString& aLang,
                                            nsIURI* aFile, bool* aRetVal) {
  NS_ENSURE_TRUE(aFile, NS_ERROR_INVALID_ARG);
  *aRetVal = false;

  nsCOMPtr<nsIURI> file = mDynamicDictionaries.Get(aLang);
  bool equal;
  if (file && NS_SUCCEEDED(file->Equals(aFile, &equal)) && equal) {
    mDynamicDictionaries.Remove(aLang);
    LoadDictionaryList(true);
    *aRetVal = true;
  }
  return NS_OK;
}
