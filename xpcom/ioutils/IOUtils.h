/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_IOUtils_
#define mozilla_dom_IOUtils_

#include "js/Utility.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Attributes.h"
#include "mozilla/DataMutex.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/IOUtilsBinding.h"
#include "mozilla/dom/TypedArray.h"
#include "nsIAsyncShutdown.h"
#include "nsIFile.h"
#include "nsISerialEventTarget.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "prio.h"

class nsFileRandomAccessStream;

namespace mozilla {

/**
 * Utility class to be used with |UniquePtr| to automatically close NSPR file
 * descriptors when they go out of scope.
 *
 * Example:
 *
 *   UniquePtr<PRFileDesc, PR_CloseDelete> fd = PR_Open(path, flags, mode);
 */
class PR_CloseDelete {
 public:
  constexpr PR_CloseDelete() = default;
  PR_CloseDelete(const PR_CloseDelete& aOther) = default;
  PR_CloseDelete(PR_CloseDelete&& aOther) = default;
  PR_CloseDelete& operator=(const PR_CloseDelete& aOther) = default;
  PR_CloseDelete& operator=(PR_CloseDelete&& aOther) = default;

  void operator()(PRFileDesc* aPtr) const { PR_Close(aPtr); }
};

/**
 * Implementation for the Web IDL interface at dom/chrome-webidl/IOUtils.webidl.
 * Methods of this class must only be called from the parent process.
 */
class IOUtils final {
 public:
  class IOError;

  enum class ShutdownPhase : uint8_t {
    ProfileBeforeChange,
    SendTelemetry,
    XpcomWillShutdown,
    Count,
  };

  template <typename T>
  using PhaseArray = EnumeratedArray<IOUtils::ShutdownPhase, T,
                                     size_t(IOUtils::ShutdownPhase::Count)>;

  static already_AddRefed<dom::Promise> Read(dom::GlobalObject& aGlobal,

                                             const nsAString& aPath,
                                             const dom::ReadOptions& aOptions,
                                             ErrorResult& aError);

  static already_AddRefed<dom::Promise> ReadUTF8(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::ReadUTF8Options& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> ReadJSON(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::ReadUTF8Options& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> Write(dom::GlobalObject& aGlobal,
                                              const nsAString& aPath,
                                              const dom::Uint8Array& aData,
                                              const dom::WriteOptions& aOptions,
                                              ErrorResult& aError);

  static already_AddRefed<dom::Promise> WriteUTF8(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const nsACString& aString, const dom::WriteOptions& aOptions,
      ErrorResult& aError);

  static already_AddRefed<dom::Promise> WriteJSON(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      JS::Handle<JS::Value> aValue, const dom::WriteJSONOptions& aOptions,
      ErrorResult& aError);

  static already_AddRefed<dom::Promise> Move(dom::GlobalObject& aGlobal,
                                             const nsAString& aSourcePath,
                                             const nsAString& aDestPath,
                                             const dom::MoveOptions& aOptions,
                                             ErrorResult& aError);

  static already_AddRefed<dom::Promise> Remove(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::RemoveOptions& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> MakeDirectory(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::MakeDirectoryOptions& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> Stat(dom::GlobalObject& aGlobal,
                                             const nsAString& aPath,
                                             ErrorResult& aError);

  static already_AddRefed<dom::Promise> Copy(dom::GlobalObject& aGlobal,
                                             const nsAString& aSourcePath,
                                             const nsAString& aDestPath,
                                             const dom::CopyOptions& aOptions,
                                             ErrorResult& aError);

  static already_AddRefed<dom::Promise> SetAccessTime(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::Optional<int64_t>& aAccess, ErrorResult& aError);

  static already_AddRefed<dom::Promise> SetModificationTime(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::Optional<int64_t>& aModification, ErrorResult& aError);

 private:
  using SetTimeFn = decltype(&nsIFile::SetLastAccessedTime);

  static_assert(
      std::is_same_v<SetTimeFn, decltype(&nsIFile::SetLastModifiedTime)>);

  static already_AddRefed<dom::Promise> SetTime(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::Optional<int64_t>& aNewTime, SetTimeFn aSetTimeFn,
      const char* const aTimeKind, ErrorResult& aError);

 public:
  static already_AddRefed<dom::Promise> HasChildren(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::HasChildrenOptions& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> GetChildren(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::GetChildrenOptions& aOptions, ErrorResult& aError);

  static already_AddRefed<dom::Promise> SetPermissions(
      dom::GlobalObject& aGlobal, const nsAString& aPath, uint32_t aPermissions,
      const bool aHonorUmask, ErrorResult& aError);

  static already_AddRefed<dom::Promise> Exists(dom::GlobalObject& aGlobal,
                                               const nsAString& aPath,
                                               ErrorResult& aError);

  static already_AddRefed<dom::Promise> CreateUniqueFile(
      dom::GlobalObject& aGlobal, const nsAString& aParent,
      const nsAString& aPrefix, const uint32_t aPermissions,
      ErrorResult& aError);
  static already_AddRefed<dom::Promise> CreateUniqueDirectory(
      dom::GlobalObject& aGlobal, const nsAString& aParent,
      const nsAString& aPrefix, const uint32_t aPermissions,
      ErrorResult& aError);

 private:
  /**
   * A helper method for CreateUniqueFile and CreateUniqueDirectory.
   */
  static already_AddRefed<dom::Promise> CreateUnique(
      dom::GlobalObject& aGlobal, const nsAString& aParent,
      const nsAString& aPrefix, const uint32_t aFileType,
      const uint32_t aPermissions, ErrorResult& aError);

 public:
  static already_AddRefed<dom::Promise> ComputeHexDigest(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const dom::HashAlgorithm aAlgorithm, ErrorResult& aError);

#if defined(XP_WIN)
  static already_AddRefed<dom::Promise> GetWindowsAttributes(
      dom::GlobalObject& aGlobal, const nsAString& aPath, ErrorResult& aError);

  static already_AddRefed<dom::Promise> SetWindowsAttributes(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const mozilla::dom::WindowsFileAttributes& aAttrs, bool aRecursive,
      ErrorResult& aError);
#elif defined(XP_MACOSX)
  static already_AddRefed<dom::Promise> HasMacXAttr(dom::GlobalObject& aGlobal,
                                                    const nsAString& aPath,
                                                    const nsACString& aAttr,
                                                    ErrorResult& aError);
  static already_AddRefed<dom::Promise> GetMacXAttr(dom::GlobalObject& aGlobal,
                                                    const nsAString& aPath,
                                                    const nsACString& aAttr,
                                                    ErrorResult& aError);
  static already_AddRefed<dom::Promise> SetMacXAttr(
      dom::GlobalObject& aGlobal, const nsAString& aPath,
      const nsACString& aAttr, const dom::Uint8Array& aValue,
      ErrorResult& aError);
  static already_AddRefed<dom::Promise> DelMacXAttr(dom::GlobalObject& aGlobal,
                                                    const nsAString& aPath,
                                                    const nsACString& aAttr,
                                                    ErrorResult& aError);
#endif

#ifdef XP_UNIX
  using UnixString = dom::OwningUTF8StringOrUint8Array;
  static uint32_t LaunchProcess(dom::GlobalObject& aGlobal,
                                const dom::Sequence<UnixString>& aArgv,
                                const dom::LaunchOptions& aOptions,
                                ErrorResult& aRv);
#endif

  static already_AddRefed<dom::Promise> GetFile(
      dom::GlobalObject& aGlobal, const dom::Sequence<nsString>& aComponents,
      ErrorResult& aError);

  static already_AddRefed<dom::Promise> GetDirectory(
      dom::GlobalObject& aGlobal, const dom::Sequence<nsString>& aComponents,
      ErrorResult& aError);

  static void GetProfileBeforeChange(dom::GlobalObject& aGlobal,
                                     JS::MutableHandle<JS::Value>,
                                     ErrorResult& aRv);

  static void GetSendTelemetry(dom::GlobalObject& aGlobal,
                               JS::MutableHandle<JS::Value>, ErrorResult& aRv);

  static RefPtr<SyncReadFile> OpenFileForSyncReading(dom::GlobalObject& aGlobal,
                                                     const nsAString& aPath,
                                                     ErrorResult& aRv);

  class JsBuffer;

  /**
   * The kind of buffer to allocate.
   *
   * This controls what kind of JS object (a JSString or a dom::Uint8Array) is
   * returned by |ToJSValue()|.
   */
  enum class BufferKind {
    String,
    Uint8Array,
  };

  /**
   * Attempts to remove the file located at |aFile|.
   *
   * Must be called off the main thread.
   *
   * @param aFile          The location of the file.
   * @param aIgnoreAbsent  If true, suppress errors due to an absent target
   * file.
   * @param aRecursive     If true, attempt to recursively remove descendant
   *                       files. This option is safe to use even if the target
   *                       is not a directory.
   * @param aRetryReadonly Retry a delete that failed with a NotAllowedError by
   *                       first removing the readonly attribute. Only has an
   *                       effect on Windows.
   *
   * @return Ok if the file was removed successfully, or an error.
   */
  static Result<Ok, IOError> RemoveSync(nsIFile* aFile, bool aIgnoreAbsent,
                                        bool aRecursive, bool aRetryReadonly);

 private:
  ~IOUtils() = default;

  template <typename T>
  using IOPromise = MozPromise<T, IOError, true>;

  friend class IOUtilsShutdownBlocker;
  struct InternalFileInfo;
  struct InternalWriteOpts;
  class MozLZ4;
  class EventQueue;
  class State;

  template <typename Fn>
  static already_AddRefed<dom::Promise> WithPromiseAndState(
      dom::GlobalObject& aGlobal, ErrorResult& aError, Fn aFn);

  /**
   * Dispatch a task on the event queue and resolve or reject the associated
   * promise based on the result.
   *
   * NB: If the calling thread is a worker, this function takes care of keepting
   *     it alive until the |IOPromise| can complete.
   *
   * @param aPromise The promise corresponding to the task running on the event
   * queue.
   * @param aFunc The task to run.
   */
  template <typename OkT, typename Fn>
  static void DispatchAndResolve(EventQueue* aQueue, dom::Promise* aPromise,
                                 Fn aFunc);

  /**
   * Creates a new JS dom::Promise.
   *
   * @return The new promise, or |nullptr| on failure.
   */
  static already_AddRefed<dom::Promise> CreateJSPromise(
      dom::GlobalObject& aGlobal, ErrorResult& aError);

  // Allow conversion of |InternalFileInfo| with |ToJSValue|.
  friend bool ToJSValue(JSContext* aCx,
                        const InternalFileInfo& aInternalFileInfo,
                        JS::MutableHandle<JS::Value> aValue);

  /**
   * Attempts to read the entire file at |aPath| into a buffer.
   *
   * @param aFile       The location of the file.
   * @param aOffset     The offset to start reading from.
   * @param aMaxBytes   If |Some|, then only read up this this number of bytes,
   *                    otherwise attempt to read the whole file.
   * @param aDecompress If true, decompress the bytes read from disk before
   *                    returning the result to the caller.
   * @param aBufferKind The kind of buffer to allocate.
   *
   * @return A buffer containing the entire (decompressed) file contents, or an
   *         error.
   */
  static Result<JsBuffer, IOError> ReadSync(nsIFile* aFile,
                                            const uint64_t aOffset,
                                            const Maybe<uint32_t> aMaxBytes,
                                            const bool aDecompress,
                                            BufferKind aBufferKind);

  /**
   * Attempts to read the entire file at |aPath| as a UTF-8 string.
   *
   * @param aFile       The location of the file.
   * @param aDecompress If true, decompress the bytes read from disk before
   *                    returning the result to the caller.
   *
   * @return The (decompressed) contents of the file re-encoded as a UTF-16
   *         string.
   */
  static Result<JsBuffer, IOError> ReadUTF8Sync(nsIFile* aFile,
                                                const bool aDecompress);

  /**
   * Attempt to write the entirety of |aByteArray| to the file at |aPath|.
   * This may occur by writing to an intermediate destination and performing a
   * move, depending on |aOptions|.
   *
   * @param aFile  The location of the file.
   * @param aByteArray The data to write to the file.
   * @param aOptions   Options to modify the way the write is completed.
   *
   * @return The number of bytes written to the file, or an error if the write
   *         failed or was incomplete.
   */
  static Result<uint32_t, IOError> WriteSync(
      nsIFile* aFile, const Span<const uint8_t>& aByteArray,
      const InternalWriteOpts& aOptions);

  /**
   * Attempts to move the file located at |aSourceFile| to |aDestFile|.
   *
   * @param aSourceFile  The location of the file to move.
   * @param aDestFile    The destination for the file.
   * @param aNoOverWrite If true, abort with an error if a file already exists
   * at |aDestFile|. Otherwise, the file will be overwritten by the move.
   *
   * @return Ok if the file was moved successfully, or an error.
   */
  static Result<Ok, IOError> MoveSync(nsIFile* aSourceFile, nsIFile* aDestFile,
                                      bool aNoOverwrite);

  /**
   * Attempts to copy the file at |aSourceFile| to |aDestFile|.
   *
   * @param aSourceFile The location of the file to copy.
   * @param aDestFile   The destination that the file will be copied to.
   *
   * @return Ok if the operation was successful, or an error.
   */
  static Result<Ok, IOError> CopySync(nsIFile* aSourceFile, nsIFile* aDestFile,
                                      bool aNoOverWrite, bool aRecursive);

  /**
   * Provides the implementation for |CopySync| and |MoveSync|.
   *
   * @param aMethod      A pointer to one of |nsIFile::MoveTo| or |CopyTo|
   *                     instance methods.
   * @param aMethodName  The name of the method to the performed. Either "move"
   *                     or "copy".
   * @param aSource      The source file to be copied or moved.
   * @param aDest        The destination file.
   * @param aNoOverwrite If true, allow overwriting |aDest| during the copy or
   *                     move. Otherwise, abort with an error if the file would
   *                     be overwritten.
   *
   * @return Ok if the operation was successful, or an error.
   */
  template <typename CopyOrMoveFn>
  static Result<Ok, IOError> CopyOrMoveSync(CopyOrMoveFn aMethod,
                                            const char* aMethodName,
                                            nsIFile* aSource, nsIFile* aDest,
                                            bool aNoOverwrite);

  /**
   * Attempts to create a new directory at |aFile|.
   *
   * @param aFile             The location of the directory to create.
   * @param aCreateAncestors  If true, create missing ancestor directories as
   *                          needed. Otherwise, report an error if the target
   *                          has non-existing ancestor directories.
   * @param aIgnoreExisting   If true, suppress errors that occur if the target
   *                          directory already exists. Otherwise, propagate the
   *                          error if it occurs.
   * @param aMode             Optional file mode. Defaults to 0777 to allow the
   *                          system umask to compute the best mode for the new
   *                          directory.
   *
   * @return Ok if the directory was created successfully, or an error.
   */
  static Result<Ok, IOError> MakeDirectorySync(nsIFile* aFile,
                                               bool aCreateAncestors,
                                               bool aIgnoreExisting,
                                               int32_t aMode = 0777);

  /**
   * Attempts to stat a file at |aFile|.
   *
   * @param aFile The location of the file.
   *
   * @return An |InternalFileInfo| struct if successful, or an error.
   */
  static Result<IOUtils::InternalFileInfo, IOError> StatSync(nsIFile* aFile);

  /**
   * Attempts to update the last access or modification time of the file at
   * |aFile|.
   *
   * @param aFile     The location of the file.
   * @param SetTimeFn A member function pointer to either
   *                  nsIFile::SetLastAccessedTime or
   *                  nsIFile::SetLastModifiedTime.
   * @param aNewTime  Some value in milliseconds since Epoch.
   *
   * @return Timestamp of the file if the operation was successful, or an error.
   */
  static Result<int64_t, IOError> SetTimeSync(nsIFile* aFile,
                                              SetTimeFn aSetTimeFn,
                                              int64_t aNewTime);

  /**
   * Checks whether the directory at |aFile| has any immediate children.
   *
   * @param aFile The location of the directory.
   *
   * @param aIgnoreAbsent If true, absence of the directory (e.g., if it does
   * not exist) will not be treated as an error and will instead return false.
   *
   * @return A boolean indicating whether the directory at |aFile| has any
   *         immediate children. Otherwise, an error.
   */
  static Result<bool, IOError> HasChildrenSync(nsIFile* aFile,
                                               bool aIgnoreAbsent);

  /**
   * Returns the immediate children of the directory at |aFile|, if any.
   *
   * @param aFile The location of the directory.
   *
   * @return An array of absolute paths identifying the children of |aFile|.
   *         If there are no children, an empty array. Otherwise, an error.
   */
  static Result<nsTArray<nsString>, IOError> GetChildrenSync(
      nsIFile* aFile, bool aIgnoreAbsent);

  /**
   * Set the permissions of the given file.
   *
   * Windows does not make a distinction between user, group, and other
   * permissions like UNICES do. If a permission flag is set for any of user,
   * group, or other has a permission, then all users will have that
   * permission.
   *
   * @param aFile        The location of the file.
   * @param aPermissions The permissions to set, as a UNIX file mode.
   *
   * @return |Ok| if the permissions were successfully set, or an error.
   */
  static Result<Ok, IOError> SetPermissionsSync(nsIFile* aFile,
                                                const uint32_t aPermissions);

  /**
   * Return whether or not the file exists.
   *
   * @param aFile The location of the file.
   *
   * @return Whether or not the file exists.
   */
  static Result<bool, IOError> ExistsSync(nsIFile* aFile);

  /**
   * Create a file or directory with a unique path.
   *
   * @param aFile     The location of the file or directory (including prefix)
   * @param aFileType One of |nsIFile::NORMAL_FILE_TYPE| or
   *                  |nsIFile::DIRECTORY_TYPE|.
   * @param aperms    The permissions to create the file or directory with.
   *
   * @return A unique path.
   */
  static Result<nsString, IOError> CreateUniqueSync(
      nsIFile* aFile, const uint32_t aFileType, const uint32_t aPermissions);

  /**
   * Compute the hash of a file.
   *
   * @param aFile      The file to hash.
   * @param aAlgorithm The hashing algorithm to use.
   *
   * @return The hash of the file, as a hex digest.
   */
  static Result<nsCString, IOError> ComputeHexDigestSync(
      nsIFile* aFile, const dom::HashAlgorithm aAlgorithm);

#if defined(XP_WIN)
  /**
   * Return the Windows-specific attributes of the file.
   *
   * @param aFile The location of the file.
   *
   * @return The Windows-specific attributes of the file.
   */
  static Result<uint32_t, IOError> GetWindowsAttributesSync(nsIFile* aFile);

  /**
   * Set the Windows-specific attributes of the file.
   *
   * @param aFile       The location of the file.
   * @param aSetAttrs   The attributes to set on the file.
   * @param aClearAttrs The attributes to clear on the file.
   * @param aRecursive  Whether or not to apply the change to folder contents
   *                    recursively.
   *
   * @return |Ok| if the attributes were successfully set, or an error.
   */
  static Result<Ok, IOError> SetWindowsAttributesSync(
      nsIFile* aFile, const uint32_t aSetAttrs, const uint32_t aClearAttrs,
      bool aRecursive);
#elif defined(XP_MACOSX)
  static Result<bool, IOError> HasMacXAttrSync(nsIFile* aFile,
                                               const nsCString& aAttr);
  static Result<nsTArray<uint8_t>, IOError> GetMacXAttrSync(
      nsIFile* aFile, const nsCString& aAttr);
  static Result<Ok, IOError> SetMacXAttrSync(nsIFile* aFile,
                                             const nsCString& aAttr,
                                             const nsTArray<uint8_t>& aValue);
  static Result<Ok, IOError> DelMacXAttrSync(nsIFile* aFile,
                                             const nsCString& aAttr);
#endif

  static void GetShutdownClient(dom::GlobalObject& aGlobal,
                                JS::MutableHandle<JS::Value> aClient,
                                ErrorResult& aRv, const ShutdownPhase aPhase);

  enum class EventQueueStatus {
    Uninitialized,
    Initialized,
    Shutdown,
  };

  enum class ShutdownBlockerStatus {
    Uninitialized,
    Initialized,
    Failed,
  };

  /**
   * Internal IOUtils state.
   */
  class State {
   public:
    StaticAutoPtr<EventQueue> mEventQueue;
    EventQueueStatus mQueueStatus = EventQueueStatus::Uninitialized;
    ShutdownBlockerStatus mBlockerStatus = ShutdownBlockerStatus::Uninitialized;

    /**
     * Set up shutdown hooks to free our internals at shutdown.
     *
     * NB: Must be called on main thread.
     */
    void SetShutdownHooks();
  };

  using StateMutex = StaticDataMutex<State>;

  /**
   * Lock the state mutex and return a handle. If shutdown has not yet
   * finished, the internals will be constructed if necessary.
   *
   * @returns A handle to the internal state, which can be used to retrieve the
   *          event queue.
   *          If |Some| is returned, |mEventQueue| is guaranteed to be
   * initialized. If shutdown has finished, |Nothing| is returned.
   */
  static Maybe<StateMutex::AutoLock> GetState();

  static StateMutex sState;
};

/**
 * The IOUtils event queue.
 */
class IOUtils::EventQueue final {
  friend void IOUtils::State::SetShutdownHooks();

 public:
  EventQueue();

  EventQueue(const EventQueue&) = delete;
  EventQueue(EventQueue&&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;
  EventQueue& operator=(EventQueue&&) = delete;

  /**
   * Dispatch a task on the event queue.
   *
   * NB: If using this directly from |IOUtils| instead of
   *     |IOUtils::DispatchAndResolve| *and* the calling thread is a worker, you
   *     *must* take care to keep the worker thread alive until the |IOPromise|
   *     resolves or rejects. See the implementation of
   *     |IOUtils::DispatchAndResolve| or |IOUtils::GetWindowsAttributes| for an
   *     example.
   *
   * @param aFunc The task to dispatch on the event queue.
   *
   * @return A promise that resolves to the task's return value or rejects with
   *         an error.
   */
  template <typename OkT, typename Fn>
  RefPtr<IOPromise<OkT>> Dispatch(Fn aFunc);

  Result<already_AddRefed<nsIAsyncShutdownBarrier>, nsresult>
  GetShutdownBarrier(const ShutdownPhase aPhase);
  Result<already_AddRefed<nsIAsyncShutdownClient>, nsresult> GetShutdownClient(
      const ShutdownPhase aPhase);

 private:
  nsresult SetShutdownHooks();

  nsCOMPtr<nsISerialEventTarget> mBackgroundEventTarget;
  IOUtils::PhaseArray<nsCOMPtr<nsIAsyncShutdownBarrier>> mBarriers;
};

/**
 * An error class used with the |Result| type returned by most private |IOUtils|
 * methods.
 */
class IOUtils::IOError {
 public:
  IOError(nsresult aCode, const nsCString& aMsg)
      : mCode(aCode), mMessage(aMsg) {}

  IOError(nsresult aCode, const char* const aFmt, ...) MOZ_FORMAT_PRINTF(3, 4)
      : mCode(aCode) {
    va_list ap;
    va_start(ap, aFmt);
    mMessage.AppendVprintf(aFmt, ap);
    va_end(ap);
  }

  static IOError WithCause(const IOError& aCause, const nsCString& aMsg) {
    IOError e(aCause.mCode, aMsg);
    e.mMessage.AppendPrintf(": %s", aCause.mMessage.get());
    return e;
  }

  static IOError WithCause(const IOError& aCause, const char* const aFmt, ...)
      MOZ_FORMAT_PRINTF(2, 3) {
    va_list ap;
    va_start(ap, aFmt);

    IOError e(aCause.mCode, EmptyCString());
    e.mMessage.AppendVprintf(aFmt, ap);
    e.mMessage.AppendPrintf(": %s", aCause.mMessage.get());

    va_end(ap);
    return e;
  }

  /**
   * Returns the |nsresult| associated with this error.
   */
  nsresult Code() const { return mCode; }

  /**
   * Returns the message associated with this error.
   */
  const nsCString& Message() const { return mMessage; }

 private:
  nsresult mCode;
  nsCString mMessage;
};

/**
 * This is an easier to work with representation of a |mozilla::dom::FileInfo|
 * for private use in the IOUtils implementation.
 *
 * Because web IDL dictionaries are not easily copy/moveable, this class is
 * used instead, until converted to the proper |mozilla::dom::FileInfo| before
 * returning any results to JavaScript.
 */
struct IOUtils::InternalFileInfo {
  nsString mPath;
  dom::FileType mType = dom::FileType::Other;
  uint64_t mSize = 0;
  Maybe<PRTime> mCreationTime;  // In ms since epoch.
  PRTime mLastAccessed = 0;     // In ms since epoch.
  PRTime mLastModified = 0;     // In ms since epoch.
  uint32_t mPermissions = 0;
};

/**
 * This is an easier to work with representation of a
 * |mozilla::dom::WriteOptions| for private use in the |IOUtils|
 * implementation.
 *
 * Because web IDL dictionaries are not easily copy/moveable, this class is
 * used instead.
 */
struct IOUtils::InternalWriteOpts {
  RefPtr<nsIFile> mBackupFile;
  RefPtr<nsIFile> mTmpFile;
  dom::WriteMode mMode;
  bool mFlush = false;
  bool mCompress = false;
  size_t mLengthHint = 0;

  static Result<InternalWriteOpts, IOUtils::IOError> FromBinding(
      const dom::WriteOptions& aOptions);
  static Result<InternalWriteOpts, IOUtils::IOError> FromBinding(
      const dom::WriteJSONOptions& aOptions);
};

/**
 * Re-implements the file compression and decompression utilities found
 * in toolkit/components/lz4/lz4.js
 *
 * This implementation uses the non-standard data layout:
 *
 *  - MAGIC_NUMBER (8 bytes)
 *  - content size (uint32_t, little endian)
 *  - content, as obtained from mozilla::Compression::LZ4::compress
 *
 * See bug 1209390 for more info.
 */
class IOUtils::MozLZ4 {
 public:
  static constexpr std::array<uint8_t, 8> MAGIC_NUMBER{
      {'m', 'o', 'z', 'L', 'z', '4', '0', '\0'}};

  static const uint32_t HEADER_SIZE = 8 + sizeof(uint32_t);

  /**
   * Compresses |aUncompressed| byte array, and returns a byte array with the
   * correct format whose contents may be written to disk.
   */
  static Result<nsTArray<uint8_t>, IOError> Compress(
      Span<const uint8_t> aUncompressed);

  /**
   * Checks |aFileContents| for the correct file header, and returns the
   * decompressed content.
   */
  static Result<IOUtils::JsBuffer, IOError> Decompress(
      Span<const uint8_t> aFileContents, IOUtils::BufferKind);
};

class IOUtilsShutdownBlocker : public nsIAsyncShutdownBlocker,
                               public nsIAsyncShutdownCompletionCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER
  NS_DECL_NSIASYNCSHUTDOWNCOMPLETIONCALLBACK

  explicit IOUtilsShutdownBlocker(const IOUtils::ShutdownPhase aPhase)
      : mPhase(aPhase) {}

 private:
  virtual ~IOUtilsShutdownBlocker() = default;

  /**
   * Called on the main thread after the event queue has been flushed.
   */
  void OnFlush();

  static constexpr IOUtils::PhaseArray<const char16_t*> PHASE_NAMES{
      u"profile-before-change",
      u"profile-before-change-telemetry",
      u"xpcom-will-shutdown",
  };

  // The last shutdown phase before we should shut down the event loop.
  static constexpr auto LAST_IO_PHASE = IOUtils::ShutdownPhase::SendTelemetry;

  IOUtils::ShutdownPhase mPhase;
  nsCOMPtr<nsIAsyncShutdownClient> mParentClient;
};

/**
 * A buffer that is allocated inside one of JS heaps so that it can be converted
 * to a JSString or Uint8Array object with at most one copy in the worst case.
 */
class IOUtils::JsBuffer final {
 public:
  /**
   * Create a new buffer of the given kind with the requested capacity.
   *
   * @param aBufferKind The kind of buffer to create (either a string or an
   *                    array).
   * @param aCapacity The capacity of the buffer.
   *
   * @return Either a successfully created buffer or an error if it could not be
   * allocated.
   */
  static Result<JsBuffer, IOUtils::IOError> Create(
      IOUtils::BufferKind aBufferKind, size_t aCapacity);

  /**
   * Create a new, empty buffer.
   *
   * This operation cannot fail.
   *
   * @param aBufferKind The kind of buffer to create (either a string or an
   *                    array).
   *
   * @return An empty JsBuffer.
   */
  static JsBuffer CreateEmpty(IOUtils::BufferKind aBufferKind);

  JsBuffer(const JsBuffer&) = delete;
  JsBuffer(JsBuffer&& aOther) noexcept;
  JsBuffer& operator=(const JsBuffer&) = delete;
  JsBuffer& operator=(JsBuffer&& aOther) noexcept;

  size_t Length() { return mLength; }
  char* Elements() { return mBuffer.get(); }
  void SetLength(size_t aNewLength) {
    MOZ_RELEASE_ASSERT(aNewLength <= mCapacity);
    mLength = aNewLength;
  }

  /**
   * Return a span for writing to the buffer.
   *
   * |SetLength| should be called after the buffer has been written to.
   *
   * @returns A span for writing to. The size of the span is the entire
   *          allocated capacity.
   */
  Span<char> BeginWriting() {
    MOZ_RELEASE_ASSERT(mBuffer.get());
    return Span(mBuffer.get(), mCapacity);
  }

  /**
   * Return a span for reading from.
   *
   * @returns A span for reading form. The size of the span is the set length
   *          of the buffer.
   */
  Span<const char> BeginReading() const {
    MOZ_RELEASE_ASSERT(mBuffer.get() || mLength == 0);
    return Span(mBuffer.get(), mLength);
  }

  /**
   * Consume the JsBuffer and convert it into a JSString.
   *
   * NOTE: This method asserts the buffer was allocated as a string buffer.
   *
   * @param aBuffer The buffer to convert to a string. After this call, the
   *                buffer will be invaldated and |IntoString| cannot be called
   *                again.
   *
   * @returns A JSString with the contents of |aBuffer|.
   */
  static JSString* IntoString(JSContext* aCx, JsBuffer aBuffer);

  /**
   * Consume the JsBuffer and convert it into a Uint8Array.
   *
   * NOTE: This method asserts the buffer was allocated as an array buffer.
   *
   * @param aBuffer The buffer to convert to an array. After this call, the
   *                buffer will be invalidated and |IntoUint8Array| cannot be
   *                called again.
   *
   * @returns A JSBuffer
   */
  static JSObject* IntoUint8Array(JSContext* aCx, JsBuffer aBuffer);

  friend bool ToJSValue(JSContext* aCx, JsBuffer&& aBuffer,
                        JS::MutableHandle<JS::Value> aValue);

 private:
  IOUtils::BufferKind mBufferKind;
  size_t mCapacity;
  size_t mLength;
  JS::UniqueChars mBuffer;

  JsBuffer(BufferKind aBufferKind, size_t aCapacity);
};

class SyncReadFile : public nsISupports, public nsWrapperCache {
 public:
  SyncReadFile(nsISupports* aParent, RefPtr<nsFileRandomAccessStream>&& aStream,
               int64_t aSize);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(SyncReadFile)

  nsISupports* GetParentObject() const { return mParent; }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  int64_t Size() const { return mSize; }
  void ReadBytesInto(const dom::Uint8Array&, const int64_t, ErrorResult& aRv);
  void Close();

 private:
  virtual ~SyncReadFile();

  nsCOMPtr<nsISupports> mParent;
  RefPtr<nsFileRandomAccessStream> mStream;
  int64_t mSize = 0;
};

}  // namespace mozilla

#endif
