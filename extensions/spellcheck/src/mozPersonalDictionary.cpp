/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozPersonalDictionary.h"

#include <utility>

#include "mozilla/Try.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIRunnable.h"
#include "nsISafeOutputStream.h"
#include "nsIUnicharInputStream.h"
#include "nsIWeakReference.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsReadableUtils.h"
#include "nsStringEnumerator.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsUnicharInputStream.h"
#include "prio.h"

#define MOZ_PERSONAL_DICT_NAME u"persdict.dat"

/**
 * This is the most braindead implementation of a personal dictionary possible.
 * There is not much complexity needed, though.  It could be made much faster,
 * and probably should, but I don't see much need for more in terms of
 * interface.
 *
 * Allowing personal words to be associated with only certain dictionaries
 * maybe.
 *
 * TODO:
 * Implement the suggestion record.
 */

NS_IMPL_ADDREF(mozPersonalDictionary)
NS_IMPL_RELEASE(mozPersonalDictionary)

NS_INTERFACE_MAP_BEGIN(mozPersonalDictionary)
  NS_INTERFACE_MAP_ENTRY(mozIPersonalDictionary)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, mozIPersonalDictionary)
NS_INTERFACE_MAP_END

class mozPersonalDictionaryLoader final : public mozilla::Runnable {
 public:
  explicit mozPersonalDictionaryLoader(mozPersonalDictionary* dict)
      : mozilla::Runnable("mozPersonalDictionaryLoader"), mDict(dict) {}

  NS_IMETHOD Run() override {
    mDict->SyncLoad();

    // Release the dictionary on the main thread
    NS_ReleaseOnMainThread("mozPersonalDictionaryLoader::mDict",
                           mDict.forget().downcast<mozIPersonalDictionary>());

    return NS_OK;
  }

 private:
  RefPtr<mozPersonalDictionary> mDict;
};

class mozPersonalDictionarySave final : public mozilla::Runnable {
 public:
  explicit mozPersonalDictionarySave(mozPersonalDictionary* aDict,
                                     nsCOMPtr<nsIFile> aFile,
                                     nsTArray<nsString>&& aDictWords)
      : mozilla::Runnable("mozPersonalDictionarySave"),
        mDictWords(std::move(aDictWords)),
        mFile(aFile),
        mDict(aDict) {}

  NS_IMETHOD Run() override {
    nsresult res;

    MOZ_ASSERT(!NS_IsMainThread());

    {
      mozilla::MonitorAutoLock mon(mDict->mMonitorSave);

      nsCOMPtr<nsIOutputStream> outStream;
      MOZ_TRY(NS_NewSafeLocalFileOutputStream(
          getter_AddRefs(outStream), mFile,
          PR_CREATE_FILE | PR_WRONLY | PR_TRUNCATE, 0664));

      // Get a buffered output stream 4096 bytes big, to optimize writes.
      nsCOMPtr<nsIOutputStream> bufferedOutputStream;
      res = NS_NewBufferedOutputStream(getter_AddRefs(bufferedOutputStream),
                                       outStream.forget(), 4096);
      if (NS_FAILED(res)) {
        return res;
      }

      uint32_t bytesWritten;
      nsAutoCString utf8Key;
      for (uint32_t i = 0; i < mDictWords.Length(); ++i) {
        CopyUTF16toUTF8(mDictWords[i], utf8Key);

        bufferedOutputStream->Write(utf8Key.get(), utf8Key.Length(),
                                    &bytesWritten);
        bufferedOutputStream->Write("\n", 1, &bytesWritten);
      }
      nsCOMPtr<nsISafeOutputStream> safeStream =
          do_QueryInterface(bufferedOutputStream);
      NS_ASSERTION(safeStream, "expected a safe output stream!");
      if (safeStream) {
        res = safeStream->Finish();
        if (NS_FAILED(res)) {
          NS_WARNING(
              "failed to save personal dictionary file! possible data loss");
        }
      }

      // Save is done, reset the state variable and notify those who are
      // waiting.
      mDict->mSavePending = false;
      mon.Notify();

      // Leaving the block where 'mon' was declared will call the destructor
      // and unlock.
    }

    // Release the dictionary on the main thread.
    NS_ReleaseOnMainThread("mozPersonalDictionarySave::mDict",
                           mDict.forget().downcast<mozIPersonalDictionary>());

    return NS_OK;
  }

 private:
  nsTArray<nsString> mDictWords;
  nsCOMPtr<nsIFile> mFile;
  RefPtr<mozPersonalDictionary> mDict;
};

mozPersonalDictionary::mozPersonalDictionary()
    : mIsLoaded(false),
      mSavePending(false),
      mMonitor("mozPersonalDictionary::mMonitor"),
      mMonitorSave("mozPersonalDictionary::mMonitorSave") {}

mozPersonalDictionary::~mozPersonalDictionary() = default;

nsresult mozPersonalDictionary::Init() {
  nsCOMPtr<nsIObserverService> svc =
      do_GetService("@mozilla.org/observer-service;1");

  NS_ENSURE_STATE(svc);
  // we want to reload the dictionary if the profile switches
  nsresult rv = svc->AddObserver(this, "profile-do-change", true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = svc->AddObserver(this, "profile-before-change", true);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  Load();

  return NS_OK;
}

void mozPersonalDictionary::WaitForLoad() {
  // If the dictionary is already loaded, we return straight away.
  if (mIsLoaded) {
    return;
  }

  // If the dictionary hasn't been loaded, we try to lock the same monitor
  // that the thread uses that does the load. This way the main thread will
  // be suspended until the monitor becomes available.
  mozilla::MonitorAutoLock mon(mMonitor);

  // The monitor has become available. This can have two reasons:
  // 1: The thread that does the load has finished.
  // 2: The thread that does the load hasn't even started.
  //    In this case we need to wait.
  if (!mIsLoaded) {
    mon.Wait();
  }
}

nsresult mozPersonalDictionary::LoadInternal() {
  nsresult rv;
  mozilla::MonitorAutoLock mon(mMonitor);

  if (mIsLoaded) {
    return NS_OK;
  }

  rv =
      NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(mFile));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!mFile) {
    return NS_ERROR_FAILURE;
  }

  rv = mFile->Append(nsLiteralString(MOZ_PERSONAL_DICT_NAME));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIRunnable> runnable = new mozPersonalDictionaryLoader(this);
  rv = target->Dispatch(runnable, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP mozPersonalDictionary::Load() {
  nsresult rv = LoadInternal();

  if (NS_FAILED(rv)) {
    mIsLoaded = true;
  }

  return rv;
}

void mozPersonalDictionary::SyncLoad() {
  MOZ_ASSERT(!NS_IsMainThread());

  mozilla::MonitorAutoLock mon(mMonitor);

  if (mIsLoaded) {
    return;
  }

  SyncLoadInternal();
  mIsLoaded = true;
  mon.Notify();
}

void mozPersonalDictionary::SyncLoadInternal() {
  MOZ_ASSERT(!NS_IsMainThread());

  // FIXME Deinst  -- get dictionary name from prefs;
  nsresult rv;
  bool dictExists;

  rv = mFile->Exists(&dictExists);
  if (NS_FAILED(rv)) {
    return;
  }

  if (!dictExists) {
    // Nothing is really wrong...
    return;
  }

  nsCOMPtr<nsIInputStream> inStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(inStream), mFile);
  if (NS_FAILED(rv)) {
    return;
  }

  nsCOMPtr<nsIUnicharInputStream> convStream;
  rv = NS_NewUnicharInputStream(inStream, getter_AddRefs(convStream));
  if (NS_FAILED(rv)) {
    return;
  }

  // we're rereading to get rid of the old data  -- we shouldn't have any,
  // but...
  mDictionaryTable.Clear();

  char16_t c;
  uint32_t nRead;
  bool done = false;
  do {  // read each line of text into the string array.
    if ((NS_OK != convStream->Read(&c, 1, &nRead)) || (nRead != 1)) break;
    while (!done && ((c == '\n') || (c == '\r'))) {
      if ((NS_OK != convStream->Read(&c, 1, &nRead)) || (nRead != 1))
        done = true;
    }
    if (!done) {
      nsAutoString word;
      while ((c != '\n') && (c != '\r') && !done) {
        word.Append(c);
        if ((NS_OK != convStream->Read(&c, 1, &nRead)) || (nRead != 1))
          done = true;
      }
      mDictionaryTable.Insert(word);
    }
  } while (!done);
}

void mozPersonalDictionary::WaitForSave() {
  // If no save is pending, we return straight away.
  if (!mSavePending) {
    return;
  }

  // If a save is pending, we try to lock the same monitor that the thread uses
  // that does the save. This way the main thread will be suspended until the
  // monitor becomes available.
  mozilla::MonitorAutoLock mon(mMonitorSave);

  // The monitor has become available. This can have two reasons:
  // 1: The thread that does the save has finished.
  // 2: The thread that does the save hasn't even started.
  //    In this case we need to wait.
  if (mSavePending) {
    mon.Wait();
  }
}

NS_IMETHODIMP mozPersonalDictionary::Save() {
  nsCOMPtr<nsIFile> theFile;
  nsresult res;

  WaitForSave();

  mSavePending = true;

  // FIXME Deinst  -- get dictionary name from prefs;
  res = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                               getter_AddRefs(theFile));
  if (NS_FAILED(res)) return res;
  if (!theFile) return NS_ERROR_FAILURE;
  res = theFile->Append(nsLiteralString(MOZ_PERSONAL_DICT_NAME));
  if (NS_FAILED(res)) return res;

  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &res);
  if (NS_WARN_IF(NS_FAILED(res))) {
    return res;
  }

  nsCOMPtr<nsIRunnable> runnable = new mozPersonalDictionarySave(
      this, theFile, mozilla::ToTArray<nsTArray<nsString>>(mDictionaryTable));
  res = target->Dispatch(runnable, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(res))) {
    return res;
  }
  return res;
}

NS_IMETHODIMP mozPersonalDictionary::GetWordList(nsIStringEnumerator** aWords) {
  NS_ENSURE_ARG_POINTER(aWords);
  *aWords = nullptr;

  WaitForLoad();

  nsTArray<nsString>* array = new nsTArray<nsString>(
      mozilla::ToTArray<nsTArray<nsString>>(mDictionaryTable));

  array->Sort();

  return NS_NewAdoptingStringEnumerator(aWords, array);
}

NS_IMETHODIMP
mozPersonalDictionary::Check(const nsAString& aWord, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  WaitForLoad();

  *aResult = (mDictionaryTable.Contains(aWord) || mIgnoreTable.Contains(aWord));
  return NS_OK;
}

NS_IMETHODIMP
mozPersonalDictionary::AddWord(const nsAString& aWord) {
  nsresult res;
  WaitForLoad();

  mDictionaryTable.Insert(aWord);
  res = Save();
  return res;
}

NS_IMETHODIMP
mozPersonalDictionary::RemoveWord(const nsAString& aWord) {
  nsresult res;
  WaitForLoad();

  mDictionaryTable.Remove(aWord);
  res = Save();
  return res;
}

NS_IMETHODIMP
mozPersonalDictionary::IgnoreWord(const nsAString& aWord) {
  // avoid adding duplicate words to the ignore list
  mIgnoreTable.EnsureInserted(aWord);
  return NS_OK;
}

NS_IMETHODIMP mozPersonalDictionary::EndSession() {
  WaitForLoad();

  WaitForSave();
  mIgnoreTable.Clear();
  return NS_OK;
}

NS_IMETHODIMP mozPersonalDictionary::Observe(nsISupports* /*aSubject*/,
                                             const char* aTopic,
                                             const char16_t* /*aData*/) {
  if (!nsCRT::strcmp(aTopic, "profile-do-change")) {
    // The observer is registered in Init() which calls Load and in turn
    // LoadInternal(); i.e. Observe() can't be called before Load().
    WaitForLoad();
    mIsLoaded = false;
    Load();  // load automatically clears out the existing dictionary table
  } else if (!nsCRT::strcmp(aTopic, "profile-before-change")) {
    WaitForSave();
  }

  return NS_OK;
}
