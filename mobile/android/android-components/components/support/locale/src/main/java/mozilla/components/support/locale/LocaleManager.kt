/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.locale

import android.annotation.SuppressLint
import android.content.Context
import android.content.SharedPreferences
import android.content.res.Configuration
import android.content.res.Resources
import androidx.annotation.VisibleForTesting
import androidx.core.content.edit
import androidx.core.os.ConfigurationCompat
import java.util.Locale
import mozilla.components.support.base.R as supportBaseR
import mozilla.components.support.locale.LocaleManager.getCurrentLocale
import mozilla.components.support.locale.LocaleManager.setNewLocale

/** Helper for apps that want to change locale defined by the system. */
object LocaleManager {
    /**
     * Change the system defined locale to the indicated in the [language] parameter. This new [language] will be stored
     * and will be the new current locale returned by [getCurrentLocale].
     *
     * After calling this function, to visualize the locale changes you have to make sure all your visible activities
     * get recreated. If your app is using the single activity approach, this will be trivial just call
     * [AppCompatActivity.recreate]. On the other hand, if you have multiple activity this could be tricky, one
     * alternative could be restarting your application process see https://github.com/JakeWharton/ProcessPhoenix
     *
     * @param context The [Context]
     * @param localeUseCase The [LocaleUseCases] used to notify the [BrowserStore] of the [Locale] changes.
     * @param language The new [Locale] that has been selected
     * @return A new Context object for whose resources are adjusted to match the new [language].
     */
    fun setNewLocale(context: Context, localeUseCase: LocaleUseCases? = null, locale: Locale?): Context {
        Storage.save(context, locale?.toLanguageTag())
        notifyStore(locale, localeUseCase)

        return onConfigurationChanged(context)
    }

    /**
     * The latest stored locale saved by [setNewLocale].
     *
     * @return The current selected locale. If the app is following the system default then this value will be null.
     */
    fun getCurrentLocale(context: Context): Locale? {
        return Storage.getLocale(context)?.toLocale()
    }

    /**
     * Change the current locale to the system defined one. As a result, [getCurrentLocale] will return null.
     *
     * After calling this function, to visualize the locale changes you have to make sure all your visible activities
     * get recreated. If your app is using the single activity approach, this will be trivial just call
     * [AppCompatActivity.recreate]. On the other hand, if you have multiple activity this could be tricky, one
     * alternative could be restarting your application process see https://github.com/JakeWharton/ProcessPhoenix
     */
    fun resetToSystemDefault(context: Context, localeUseCase: LocaleUseCases?) {
        clear(context)
        val locale = getSystemDefault()

        updateSystemLocale(locale)
        updateConfiguration(context, locale)

        notifyStore(locale, localeUseCase)
    }

    /** Returns the locale set by the system */
    fun getSystemDefault(): Locale {
        val config = Resources.getSystem().configuration
        return ConfigurationCompat.getLocales(config).get(0) ?: Locale.getDefault()
    }

    /**
     * Applies only the Locale to a new Context without using any cached overrides from [baseContext]. Use this if
     * overriding [android.app.Application.attachBaseContext] or [android.app.Activity.attachBaseContext].
     *
     * See [onConfigurationChanged] if you want the opposite, i.e., to include cached overrides from [baseContext].
     *
     * @param baseContext the [Context] to update.
     */
    @SuppressLint("AppBundleLocaleChanges")
    fun onAttachBaseContext(baseContext: Context): Context {
        val locale = getCurrentLocale(baseContext) ?: getSystemDefault()

        updateSystemLocale(locale)

        val configuration =
            Configuration().apply {
                setLocale(locale)
                setLayoutDirection(locale)
            }
        return baseContext.createConfigurationContext(configuration)
    }

    /**
     * Updates localization resources to use the current locale.
     *
     * See [onAttachBaseContext] to avoid using cached overrides.
     *
     * @return A context that is locale aware.
     */
    fun onConfigurationChanged(baseContext: Context): Context {
        val locale = getCurrentLocale(baseContext) ?: getSystemDefault()

        updateSystemLocale(locale)
        return updateConfiguration(baseContext, locale)
    }

    /** Notify the [BrowserStore] that the [Locale] has been changed via [LocaleUseCases]. */
    private fun notifyStore(locale: Locale?, localeUseCase: LocaleUseCases?) {
        localeUseCase?.let { useCases ->
            useCases.notifyLocaleChanged(locale)
        }
    }

    @SuppressLint("AppBundleLocaleChanges")
    private fun updateConfiguration(context: Context, locale: Locale): Context {
        val configuration = Configuration(context.resources.configuration)
        configuration.setLocale(locale)
        configuration.setLayoutDirection(locale)
        return context.createConfigurationContext(configuration)
    }

    private fun updateSystemLocale(locale: Locale) {
        Locale.setDefault(locale)
    }

    /**
     * Clears the stored locale preference.
     *
     * @param context The [Context] used to access SharedPreferences.
     */
    @VisibleForTesting
    fun clear(context: Context) {
        Storage.clear(context)
    }

    private object Storage {
        private const val PREFERENCE_FILE = "mozac_support_base_locale_manager_preference"
        private var currentLocal: String? = null

        @SuppressLint("NewApi")
        fun getLocale(context: Context): String? {
            return if (currentLocal == null && !android.os.Process.isIsolated()) {
                val settings = getSharedPreferences(context)
                val key = context.getString(supportBaseR.string.mozac_support_base_locale_preference_key_locale)
                currentLocal = settings.getString(key, null)
                currentLocal
            } else {
                currentLocal
            }
        }

        @Synchronized
        fun save(context: Context, localeCode: String?) {
            val settings = getSharedPreferences(context)
            val key = context.getString(supportBaseR.string.mozac_support_base_locale_preference_key_locale)
            settings.edit { putString(key, localeCode) }
            currentLocal = localeCode
        }

        fun clear(context: Context) {
            val settings = getSharedPreferences(context)
            settings.edit { clear() }
            currentLocal = null
        }

        private fun getSharedPreferences(context: Context): SharedPreferences {
            return context.getSharedPreferences(PREFERENCE_FILE, Context.MODE_PRIVATE)
        }
    }
}
