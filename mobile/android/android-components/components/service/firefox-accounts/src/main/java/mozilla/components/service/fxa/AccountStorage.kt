/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.fxa

import android.content.Context
import android.content.SharedPreferences
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import mozilla.appservices.fxaclient.FxaRustAuthState
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.sync.AccountEvent
import mozilla.components.concept.sync.AccountEventsObserver
import mozilla.components.concept.sync.StatePersistenceCallback
import mozilla.components.lib.dataprotect.SecureAbove22Preferences
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.observer.ObserverRegistry
import java.lang.ref.WeakReference

const val FXA_STATE_PREFS_KEY = "fxaAppState"
const val FXA_STATE_KEY = "fxaState"

/**
 * Represents state of our account on disk - is it new, or restored?
 */
internal sealed class AccountOnDisk : WithAccount {
    data class Restored(val account: FirefoxAccount) : AccountOnDisk() {
        override fun account() = account
    }
    data class New(val account: FirefoxAccount) : AccountOnDisk() {
        override fun account() = account
    }
}

internal interface WithAccount {
    fun account(): FirefoxAccount
}

/**
 * Knows how to read account from disk (or creating a new instance if there's no account),
 * registering necessary watchers.
 */
open class StorageWrapper(
    private val accountManager: FxaAccountManager,
    accountEventObserverRegistry: ObserverRegistry<AccountEventsObserver>,
    private val serverConfig: ServerConfig,
    private val crashReporter: CrashReporting? = null,
) {
    private class PersistenceCallback(
        private val accountManager: WeakReference<FxaAccountManager>,
    ) : StatePersistenceCallback {
        private val logger = Logger("FxaStatePersistenceCallback")

        override fun persist(data: String) {
            val storage = accountManager.get()?.getAccountStorage()
            logger.debug("Persisting account state into $storage")
            storage?.write(data)
        }
    }

    private val statePersistenceCallback = PersistenceCallback(WeakReference(accountManager))
    private val accountEventsIntegration = AccountEventsIntegration(accountEventObserverRegistry)

    internal fun account(): AccountOnDisk {
        return try {
            when (val account = accountManager.getAccountStorage().read()) {
                null -> AccountOnDisk.New(obtainAccount())
                else -> AccountOnDisk.Restored(account)
            }
        } catch (e: FxaPanicException) {
            // Don't swallow panics from the underlying library.
            throw e
        } catch (e: FxaException) {
            // Locally corrupt accounts are simply treated as 'absent'.
            AccountOnDisk.New(obtainAccount())
        }.also {
            watchAccount(it.account())
        }
    }

    private fun watchAccount(account: FirefoxAccount) {
        account.registerPersistenceCallback(statePersistenceCallback)
        account.deviceConstellation().register(accountEventsIntegration)
    }

    /**
     * Exists strictly for testing purposes, allowing tests to specify their own implementation of [FirefoxAccount].
     */
    @VisibleForTesting
    open fun obtainAccount(): FirefoxAccount = FirefoxAccount(serverConfig, crashReporter)
}

/**
 * In the future, this could be an internal account-related events processing layer.
 * E.g., once we grow events such as "please logout".
 * For now, we just pass everything downstream as-is.
 */
internal class AccountEventsIntegration(
    private val listenerRegistry: ObserverRegistry<AccountEventsObserver>,
) : AccountEventsObserver {
    private val logger = Logger("AccountEventsIntegration")

    override fun onEvents(events: List<AccountEvent>) {
        logger.info("Received events, notifying listeners")
        listenerRegistry.notifyObservers { onEvents(events) }
    }
}

internal interface AccountStorage {
    @Throws(Exception::class)
    fun read(): FirefoxAccount?
    fun write(accountState: String)
    fun clear()
}

/**
 * Account storage layer which uses plaintext storage implementation.
 *
 * Migration from [SecureAbove22AccountStorage] will happen upon initialization,
 * unless disabled via [migrateFromSecureStorage].
 */
@SuppressWarnings("TooGenericExceptionCaught")
internal class SharedPrefAccountStorage(
    val context: Context,
    private val crashReporter: CrashReporting? = null,
    migrateFromSecureStorage: Boolean = true,
) : AccountStorage {
    internal val logger = Logger("mozac/SharedPrefAccountStorage")

    init {
        if (migrateFromSecureStorage) {
            // In case we switched from SecureAbove22AccountStorage to this implementation, migrate persisted account
            // and clear out the old storage layer.
            val secureStorage = SecureAbove22AccountStorage(
                context,
                crashReporter,
                migrateFromPlaintextStorage = false,
            )
            try {
                secureStorage.read()?.let { secureAccount ->
                    this.write(secureAccount.toJSONString())
                    secureStorage.clear()
                }
            } catch (e: Exception) {
                // Certain devices crash on various Keystore exceptions. While trying to migrate
                // to use the plaintext storage we don't want to crash if we can't access secure
                // storage, and just catch the errors.
                logger.error("Migrating from secure storage failed", e)
            }
        }
    }

    /**
     * @throws FxaException if JSON failed to parse into a [FirefoxAccount].
     */
    @Throws(FxaException::class)
    override fun read(): FirefoxAccount? {
        val savedJSON = accountPreferences().getString(FXA_STATE_KEY, null)
            ?: return null

        // May throw a generic FxaException if it fails to process saved JSON.
        val account = FirefoxAccount.fromJSONString(savedJSON, crashReporter)
        val state = account.getAuthState()
        if (state != FxaRustAuthState.CONNECTED && crashReporter != null) {
            crashReporter.submitCaughtException(
                AbnormalAccountStorageEvent.RestoringNonConnectedAccount(
                    "Restoring account from an unexpected state: $state",
                ),
            )
        }
        return account
    }

    override fun write(accountState: String) {
        accountPreferences().edit { putString(FXA_STATE_KEY, accountState) }
    }

    override fun clear() {
        accountPreferences().edit { remove(FXA_STATE_KEY) }
    }

    private fun accountPreferences(): SharedPreferences {
        return context.getSharedPreferences(FXA_STATE_PREFS_KEY, Context.MODE_PRIVATE)
    }
}

/**
 * A base class for exceptions describing abnormal account storage behaviour.
 */
internal abstract class AbnormalAccountStorageEvent(message: String? = null) : Exception(message) {
    /**
     * Account state was expected to be present, but it wasn't.
     */
    internal class UnexpectedlyMissingAccountState(message: String? = null) : AbnormalAccountStorageEvent(message)
    internal class RestoringNonConnectedAccount(message: String? = null) : AbnormalAccountStorageEvent(message)
}

/**
 * Account storage layer which uses encrypted-at-rest storage implementation for supported API levels (23+).
 * On older API versions account state is stored in plaintext.
 *
 * Migration from [SharedPrefAccountStorage] will happen upon initialization,
 * unless disabled via [migrateFromPlaintextStorage].
 */
internal class SecureAbove22AccountStorage(
    context: Context,
    private val crashReporter: CrashReporting? = null,
    migrateFromPlaintextStorage: Boolean = true,
) : AccountStorage {
    companion object {
        private const val STORAGE_NAME = "fxaStateAC"
        private const val KEY_ACCOUNT_STATE = "fxaState"
        private const val PREF_NAME = "fxaStatePrefAC"
        private const val PREF_KEY_HAS_STATE = "fxaStatePresent"
    }

    private val store = SecureAbove22Preferences(context, STORAGE_NAME, crashReporting = crashReporter)

    // Prefs are used here to keep track of abnormal storage behaviour - namely, account state disappearing without
    // being cleared first through this class. Note that clearing application data will clear both 'store' and 'prefs'.
    private val prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)

    init {
        if (migrateFromPlaintextStorage) {
            // In case we switched from SharedPrefAccountStorage to this implementation, migrate persisted account
            // and clear out the old storage layer.
            val plaintextStorage = SharedPrefAccountStorage(context, migrateFromSecureStorage = false)
            plaintextStorage.read()?.let { plaintextAccount ->
                this.write(plaintextAccount.toJSONString())
                plaintextStorage.clear()
            }
        }
    }

    /**
     * @throws FxaException if JSON failed to parse into a [FirefoxAccount].
     */
    @Throws(FxaException::class)
    override fun read(): FirefoxAccount? {
        return store.getString(KEY_ACCOUNT_STATE).also {
            // If account state is missing, but we expected it to be present, report an exception.
            if (it == null && prefs.getBoolean(PREF_KEY_HAS_STATE, false)) {
                crashReporter?.submitCaughtException(AbnormalAccountStorageEvent.UnexpectedlyMissingAccountState())
                // Clear prefs to make sure we only submit this exception once.
                prefs.edit { clear() }
            }
        }?.let { FirefoxAccount.fromJSONString(it, crashReporter) }
    }

    override fun write(accountState: String) {
        store.putString(KEY_ACCOUNT_STATE, accountState)
        prefs.edit { putBoolean(PREF_KEY_HAS_STATE, true) }
    }

    override fun clear() {
        store.clear()
        prefs.edit { clear() }
    }
}
