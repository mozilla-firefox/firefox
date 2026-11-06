/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Log;
import androidx.annotation.AnyThread;
import androidx.annotation.IntDef;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringDef;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;
import org.mozilla.gecko.EventDispatcher;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.gecko.util.ThreadUtils;

/**
 * The translations controller coordinates the session and runtime messaging between GeckoView and
 * the translations toolkit.
 */
public class TranslationsController {
  /** Public constructor for TranslationsController. */
  public TranslationsController() {}

  private static final boolean DEBUG = false;
  private static final String LOGTAG = "TranslationsController";

  /**
   * Runtime translation coordinates runtime messaging between the translations toolkit actor and
   * GeckoView.
   *
   * <p>Performs translations actions that are not dependent on the page. Typical usage is for
   * setting preferences, managing downloads, and getting information on language models available.
   */
  public static class RuntimeTranslation {
    /** Public constructor for RuntimeTranslation. */
    public RuntimeTranslation() {}

    // Events Dispatched to Toolkit Translations
    private static final String ENGINE_SUPPORTED_EVENT =
        "GeckoView:Translations:IsTranslationEngineSupported";

    private static final String PREFERRED_LANGUAGES_EVENT =
        "GeckoView:Translations:PreferredLanguages";

    private static final String MANAGE_MODEL_EVENT = "GeckoView:Translations:ManageModel";

    private static final String TRANSLATION_INFORMATION_EVENT =
        "GeckoView:Translations:TranslationInformation";
    private static final String MODEL_INFORMATION_EVENT = "GeckoView:Translations:ModelInformation";

    private static final String GET_LANGUAGE_SETTING_EVENT =
        "GeckoView:Translations:GetLanguageSetting";

    private static final String GET_LANGUAGE_SETTINGS_EVENT =
        "GeckoView:Translations:GetLanguageSettings";

    private static final String SET_LANGUAGE_SETTINGS_EVENT =
        "GeckoView:Translations:SetLanguageSettings";

    private static final String GET_SPECIFIED_SITES_SETTINGS_EVENT =
        "GeckoView:Translations:GetNeverTranslateSpecifiedSites";

    private static final String SET_SPECIFIED_SITE_SETTINGS_EVENT =
        "GeckoView:Translations:SetNeverTranslateSpecifiedSite";

    private static final String GET_TRANSLATE_PAIR_DOWNLOAD_SIZE =
        "GeckoView:Translations:GetTranslateDownloadSize";

    /**
     * Checks if the device can use the supplied model binary files for translations.
     *
     * <p>Use to check if translations are ever possible.
     *
     * @return true if translations are supported on the device, or false if not.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Boolean> isTranslationsEngineSupported() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting if the translations engine supports the device.");
      }
      return EventDispatcher.getInstance()
          .queryBoolean(ENGINE_SUPPORTED_EVENT)
          .map(
              result -> result,
              exception ->
                  new TranslationsException(TranslationsException.ERROR_ENGINE_NOT_SUPPORTED));
    }

    /**
     * Returns the preferred languages of the user in the following order:
     *
     * <p>1. Most recent target languages
     *
     * <p>2. Web requested languages
     *
     * <p>3. App languages
     *
     * <p>4. OS language
     *
     * @return a GeckoResult with a user's preferred language(s) or null or an exception
     */
    @HandlerThread
    public static @NonNull GeckoResult<List<String>> preferredLanguages() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting the user's preferred languages.");
      }
      return EventDispatcher.getInstance()
          .queryBundle(PREFERRED_LANGUAGES_EVENT)
          .map(
              bundle -> {
                try {
                  final String[] languages = bundle.getStringArray("preferredLanguages");
                  if (languages != null) {
                    return Arrays.asList(languages);
                  }
                } catch (final Exception e) {
                  Log.w(LOGTAG, "Could not deserialize preferredLanguages: " + e);
                  return null;
                }
                return null;
              },
              exception ->
                  new TranslationsException(TranslationsException.ERROR_COULD_NOT_LOAD_LANGUAGES));
    }

    /**
     * Manage the language model or models. Options are to download or delete a BCP 47 language or
     * all or cache.
     *
     * @param options contain language, operation, and operation level to perform on the model
     * @return the request proceeded as expected or an exception.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> manageLanguageModel(
        final @NonNull ModelManagementOptions options) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting management of the language model.");
      }
      return EventDispatcher.getInstance()
          .queryVoid(MANAGE_MODEL_EVENT, options.toBundle())
          .map(
              result -> result,
              exception -> {
                final String exceptionData =
                    ((EventDispatcher.QueryException) exception).data.toString();
                if (exceptionData.contains("COULD_NOT_DELETE")) {
                  return new TranslationsException(
                      TranslationsException.ERROR_MODEL_COULD_NOT_DELETE);
                } else if (exceptionData.contains("LANGUAGE_REQUIRED")) {
                  return new TranslationsException(
                      TranslationsException.ERROR_MODEL_LANGUAGE_REQUIRED);
                } else if (exceptionData.contains("COULD_NOT_DOWNLOAD")) {
                  return new TranslationsException(
                      TranslationsException.ERROR_MODEL_COULD_NOT_DOWNLOAD);
                }
                return new TranslationsException(TranslationsException.ERROR_UNKNOWN);
              });
    }

    /**
     * List languages that can be translated to and from. Use is populating language selection.
     *
     * @return a GeckoResult with a TranslationSupport object with "to" and "from" languages or an
     *     exception.
     */
    @HandlerThread
    public static @NonNull GeckoResult<TranslationSupport> listSupportedLanguages() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting information on the language options.");
      }
      return EventDispatcher.getInstance()
          .queryBundle(TRANSLATION_INFORMATION_EVENT)
          .map(
              bundle -> TranslationSupport.fromBundle(bundle),
              exception ->
                  new TranslationsException(TranslationsException.ERROR_COULD_NOT_LOAD_LANGUAGES));
    }

    /**
     * When `translate()` is called on a given pair, then the system will downloaded the necessary
     * models to complete the translation. This method is to check the exact size of those
     * downloads. Typical case is informing the user of the download size for users in a low-data
     * mode.
     *
     * <p>If no download is detected, it will return 0. Note, if the model is not present, this will
     * also result in a value of 0 bytes.
     *
     * @param fromLanguage from BCP 47 code
     * @param toLanguage from BCP 47 code
     * @return The size of the file size in bytes. If no download is required, will return 0.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Long> checkPairDownloadSize(
        @NonNull final String fromLanguage, @NonNull final String toLanguage) {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting information on the language pair download size.");
      }
      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putString("fromLanguage", fromLanguage);
      bundle.putString("toLanguage", toLanguage);

      return EventDispatcher.getInstance()
          .queryBundle(GET_TRANSLATE_PAIR_DOWNLOAD_SIZE, bundle)
          .map(
              resultBundle -> {
                return resultBundle.getLong("bytes", 0L);
              });
    }

    /**
     * Convenience method for {@link #checkPairDownloadSize(String, String)}.
     *
     * @param pair language pair that will be used by `translate()`
     * @return The size of the necessary file size in bytes. If no download is required, will return
     *     0.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Long> checkPairDownloadSize(
        @NonNull final SessionTranslation.TranslationPair pair) {
      ThreadUtils.assertOnHandlerThread();
      return checkPairDownloadSize(pair.fromLanguage, pair.toLanguage);
    }

    /**
     * Creates a list of all of the available language models, their size for a full download, and
     * download state. Expected use is for displaying model state for user management.
     *
     * @return A GeckoResult with a list of the available language model's and their states or an
     *     exception.
     */
    @HandlerThread
    public static @NonNull GeckoResult<List<LanguageModel>> listModelDownloadStates() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting information on the language model.");
      }
      return EventDispatcher.getInstance()
          .queryBundle(MODEL_INFORMATION_EVENT)
          .map(
              bundle -> {
                try {
                  final GeckoBundle[] models = bundle.getBundleArray("models");
                  if (models != null) {
                    final List<LanguageModel> list = new ArrayList<>();
                    for (final var item : models) {
                      list.add(LanguageModel.fromBundle(item));
                    }
                    return list;
                  }
                } catch (final Exception e) {
                  Log.d(LOGTAG, "Could not deserialize the model states.");
                  return null;
                }
                return null;
              },
              exception ->
                  new TranslationsException(TranslationsException.ERROR_MODEL_COULD_NOT_RETRIEVE));
    }

    /**
     * Returns the given language setting for the corresponding language.
     *
     * @param languageCode The BCP 47 language portion of the code to check the settings for. For
     *     example, es, en, de, etc.
     * @return The {@link LanguageSetting} string for the language.
     */
    @HandlerThread
    public static @NonNull GeckoResult<String> getLanguageSetting(
        @NonNull final String languageCode) {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting language setting for " + languageCode + ".");
      }
      final GeckoBundle bundle = new GeckoBundle(1);
      bundle.putString("language", languageCode);
      return EventDispatcher.getInstance().queryString(GET_LANGUAGE_SETTING_EVENT, bundle);
    }

    /**
     * Creates a map of known language codes with their corresponding language setting.
     *
     * @return A GeckoResult with a map of each BCP 47 language portion of the code (key) and its
     *     corresponding {@link LanguageSetting} string (value).
     */
    @HandlerThread
    public static @NonNull GeckoResult<Map<String, String>> getLanguageSettings() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting language settings.");
      }
      return EventDispatcher.getInstance()
          .queryBundle(GET_LANGUAGE_SETTINGS_EVENT)
          .map(
              bundle -> {
                final Map<String, String> languageSettings = new HashMap<>();
                try {
                  final GeckoBundle[] fromBundle = bundle.getBundleArray("settings");
                  for (final var item : fromBundle) {
                    final var languageCode = item.getString("langTag");
                    final @LanguageSetting String setting = item.getString("setting", "offer");
                    if (languageCode != null) {
                      languageSettings.put(languageCode, setting);
                    }
                  }
                  return languageSettings;

                } catch (final Exception e) {
                  Log.w(
                      LOGTAG,
                      "An issue occurred while deserializing translation language settings: " + e);
                }
                return null;
              });
    }

    /**
     * Sets the language state for a given language.
     *
     * @param languageCode - The specified BCP 47 language portion of the code to update. For
     *     example, es, en, de, etc.
     * @param languageSetting - The specified setting for a given language.
     * @return A GeckoResult that will return void if successful or else will complete
     *     exceptionally.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> setLanguageSettings(
        final @NonNull String languageCode,
        final @NonNull @LanguageSetting String languageSetting) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Requesting setting language setting.");
      }

      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putString("language", languageCode);
      bundle.putString("languageSetting", String.valueOf(languageSetting));
      return EventDispatcher.getInstance().queryVoid(SET_LANGUAGE_SETTINGS_EVENT, bundle);
    }

    /**
     * Gets the list of sites that have a never translate site preference set. Should be used for
     * retrieving a list for global preference setting outside of a specific site.
     *
     * <p>Recommend using: {@link SessionTranslation#getNeverTranslateSiteSetting()} to query the
     * current session's site's never translate preferences.
     *
     * @return A list of display ready site URIs to set preferences for.
     */
    @HandlerThread
    public static @NonNull GeckoResult<List<String>> getNeverTranslateSiteList() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Retrieving specified never translate site settings");
      }
      return EventDispatcher.getInstance()
          .queryBundle(GET_SPECIFIED_SITES_SETTINGS_EVENT)
          .map(
              bundle -> {
                try {
                  final String[] neverTranslateSites = bundle.getStringArray("sites");
                  if (neverTranslateSites != null) {
                    return Arrays.asList(neverTranslateSites);
                  }
                } catch (final Exception e) {
                  Log.d(LOGTAG, "Could not deserialize the sites.");
                  return null;
                }
                return null;
              });
    }

    /**
     * Sets whether the specified site should be translated or not. This function should be used for
     * global updates to the never translate list.
     *
     * <p>Please use: {@link SessionTranslation#setNeverTranslateSiteSetting(Boolean)} when the
     * session is currently on the site to adjust the permissions for.
     *
     * @param origin A site origin URI that will have the specified never translate permission set.
     *     Recommend using URI values returned from {@link #getNeverTranslateSiteList()} and using
     *     the session to set a given site to ensure proper scope when possible.
     * @param neverTranslate Should be set to true if the site should never be translated or false
     *     if it should be translated.
     * @return Void if the operation to set the value completed or exceptionally if an issue
     *     occurred.
     */
    @HandlerThread
    public static @NonNull GeckoResult<Void> setNeverTranslateSpecifiedSite(
        final @NonNull Boolean neverTranslate, final @NonNull String origin) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Setting never translate for specified site uri origin: " + origin);
      }
      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putBoolean("neverTranslate", neverTranslate);
      bundle.putString("origin", origin);
      return EventDispatcher.getInstance().queryVoid(SET_SPECIFIED_SITE_SETTINGS_EVENT, bundle);
    }

    /** Options for managing the translation language models. */
    @AnyThread
    public static class ModelManagementOptions {
      /** BCP 47 language or null for global operations. */
      public final @Nullable String language;

      /** Operation to perform on the language model. */
      public final @NonNull @ModelOperation String operation;

      /** Level of operation */
      public final @NonNull @OperationLevel String operationLevel;

      /**
       * Options for managing the toolkit provided language model binaries.
       *
       * @param builder model management options builder
       */
      protected ModelManagementOptions(
          final @NonNull RuntimeTranslation.ModelManagementOptions.Builder builder) {
        if (builder.mLanguage != null) {
          this.language = builder.mLanguage.toLowerCase(Locale.ROOT);
        } else {
          this.language = builder.mLanguage;
        }
        this.operation = builder.mOperation.toLowerCase(Locale.ROOT);
        this.operationLevel = builder.mOperationLevel.toLowerCase(Locale.ROOT);
      }

      /** Serializer for Model Management Options */
      /* package */ @NonNull
      GeckoBundle toBundle() {
        final GeckoBundle bundle = new GeckoBundle(2);
        if (language != null) {
          bundle.putString("language", language);
        }
        bundle.putString("operation", operation.toString());
        bundle.putString("operationLevel", operationLevel.toString());

        return bundle;
      }

      /** Builder for Model Management Options */
      @AnyThread
      public static class Builder {
        /** Public constructor for Builder. */
        public Builder() {}

        /* package */ String mLanguage = null;
        /* package */ @ModelOperation String mOperation;
        /* package */ @OperationLevel String mOperationLevel = ALL;

        /**
         * Language builder setter.
         *
         * @param language that should be managed. No need to set in the case of a global operation
         *     level.
         * @return the language parameter for the constructor
         */
        public @NonNull RuntimeTranslation.ModelManagementOptions.Builder languageToManage(
            final @NonNull String language) {
          mLanguage = language;
          return this;
        }

        /**
         * Operation builder setter.
         *
         * @param operation that should be performed
         * @return the operation parameter for the constructor
         */
        public @NonNull RuntimeTranslation.ModelManagementOptions.Builder operation(
            final @NonNull @ModelOperation String operation) {
          mOperation = operation;
          return this;
        }

        /**
         * Operation level builder setter.
         *
         * @param operationLevel the level of the operation, e.g., language, all, or cache Default
         *     is to operate on all.
         * @return the operation level parameter for the constructor
         */
        public @NonNull RuntimeTranslation.ModelManagementOptions.Builder operationLevel(
            final @NonNull @OperationLevel String operationLevel) {
          mOperationLevel = operationLevel;
          return this;
        }

        /**
         * Builder for Model Management Options.
         *
         * @return a constructed ModelManagementOptions populated from builder options
         */
        @AnyThread
        public @NonNull ModelManagementOptions build() {
          return new ModelManagementOptions(this);
        }
      }
    }

    /** Operations toolkit can perform on the language models. */
    @Retention(RetentionPolicy.SOURCE)
    @StringDef(value = {DOWNLOAD, DELETE})
    public @interface ModelOperation {}

    /** The download operation is for downloading models. */
    public static final String DOWNLOAD = "download";

    /** The delete operation is for deleting models. */
    public static final String DELETE = "delete";

    /** Operation type for toolkit to operate on. */
    @Retention(RetentionPolicy.SOURCE)
    @StringDef(value = {LANGUAGE, CACHE, ALL})
    public @interface OperationLevel {}

    /**
     * The language type indicates the operation should be performed only on the specified language.
     */
    public static final String LANGUAGE = "language";

    /**
     * The cache type indicates that the operation should be performed on model files that do not
     * make up a suit.
     */
    public static final String CACHE = "cache";

    /** The all type indicates that the operation should be performed on all model files */
    public static final String ALL = "all";

    /** Language translation options. */
    public static class TranslationSupport {
      /** Languages we can translate from. */
      public final @Nullable List<Language> fromLanguages;

      /** Languages we can translate to. */
      public final @Nullable List<Language> toLanguages;

      /**
       * Construction for translation support, will usually be constructed from deserialize toolkit
       * information.
       *
       * @param fromLanguages list of from languages to list as translation options
       * @param toLanguages list of to languages to list as translation options
       */
      public TranslationSupport(
          @Nullable final List<Language> fromLanguages,
          @Nullable final List<Language> toLanguages) {
        this.fromLanguages = fromLanguages;
        this.toLanguages = toLanguages;
      }

      @Override
      public String toString() {
        return "TranslationSupport {"
            + "fromLanguages="
            + fromLanguages
            + ", toLanguages="
            + toLanguages
            + '}';
      }

      /**
       * Convenience method for deserializing support information.
       *
       * @param bundle contains language support information
       * @return support object
       */
      /* package */
      static @Nullable TranslationSupport fromBundle(final GeckoBundle bundle) {
        if (bundle == null) {
          return null;
        }
        final List<Language> fromLanguages = new ArrayList<>();
        final List<Language> toLanguages = new ArrayList<>();
        try {
          final GeckoBundle[] fromBundle = bundle.getBundleArray("sourceLanguages");
          for (final var item : fromBundle) {
            final var result = Language.fromBundle(item);
            if (result != null) {
              fromLanguages.add(result);
            }
          }

          final GeckoBundle[] toBundle = bundle.getBundleArray("targetLanguages");
          for (final var item : toBundle) {
            final var result = Language.fromBundle(item);
            if (result != null) {
              toLanguages.add(result);
            }
          }
        } catch (final Exception e) {
          Log.w(
              LOGTAG,
              "An issue occurred while deserializing translation support information: " + e);
        }

        return new TranslationSupport(fromLanguages, toLanguages);
      }
    }

    /** Information about a language model. */
    public static class LanguageModel {
      /** Display language. */
      public final @Nullable Language language;

      /** Model download state */
      public final @NonNull Boolean isDownloaded;

      /** Size in bytes for displaying download information. */
      public final long size;

      /**
       * Constructor for the language model.
       *
       * @param language the language the model is for.
       * @param isDownloaded if the model is currently downloaded or not.
       * @param size the size in bytes of the model.
       */
      public LanguageModel(
          @Nullable final Language language, final Boolean isDownloaded, final long size) {
        this.language = language;
        this.isDownloaded = isDownloaded;
        this.size = size;
      }

      @Override
      public String toString() {
        return "LanguageModel {"
            + "language="
            + language
            + ", isDownloaded="
            + isDownloaded
            + ", size="
            + size
            + '}';
      }

      /**
       * Convenience method for deserializing language model information.
       *
       * @param bundle contains language model information
       * @return language object
       */
      /* package */
      static @Nullable LanguageModel fromBundle(final GeckoBundle bundle) {
        if (bundle == null) {
          return null;
        }
        try {
          final var language = Language.fromBundle(bundle);
          final var isDownloaded = bundle.getBoolean("isDownloaded");
          final var size = bundle.getLong("size");
          return new LanguageModel(language, isDownloaded, size);
        } catch (final Exception e) {
          Log.w(LOGTAG, "Could not deserialize LanguageModel object: " + e);
          return null;
        }
      }
    }

    /**
     * The runtime language settings a given language may have that dictates the app's translation
     * offering behavior.
     */
    @Retention(RetentionPolicy.SOURCE)
    @StringDef(value = {ALWAYS, OFFER, NEVER})
    public @interface LanguageSetting {}

    /**
     * The translations engine should always expect this language to be translated and automatically
     * translate on page load.
     */
    public static final String ALWAYS = "always";

    /**
     * The translations engine should offer this language to be translated. This is the default
     * state, i.e., no user selection was made.
     */
    public static final String OFFER = "offer";

    /** The translations engine should never offer to translate this language. */
    public static final String NEVER = "never";
  }

  /**
   * Session translation coordinates session messaging between the translations toolkit actor and
   * GeckoView.
   *
   * <p>Performs translations actions that are dependent on the page.
   */
  public static class SessionTranslation {

    // Events Dispatched to Toolkit Translations
    private static final String TRANSLATE_EVENT = "GeckoView:Translations:Translate";
    private static final String RESTORE_PAGE_EVENT = "GeckoView:Translations:RestorePage";

    private static final String GET_NEVER_TRANSLATE_SITE =
        "GeckoView:Translations:GetNeverTranslateSite";

    private static final String SET_NEVER_TRANSLATE_SITE =
        "GeckoView:Translations:SetNeverTranslateSite";

    // Events Dispatched from Toolkit Translations
    private static final String ON_OFFER_EVENT = "GeckoView:Translations:Offer";
    private static final String ON_STATE_CHANGE_EVENT = "GeckoView:Translations:StateChange";

    private final GeckoSession mSession;
    private final SessionTranslation.Handler mHandler;

    /**
     * Construct a new translations session.
     *
     * @param session that will be dispatching and receiving events.
     */
    public SessionTranslation(final GeckoSession session) {
      mSession = session;
      mHandler = new SessionTranslation.Handler(mSession);
    }

    /**
     * Handler for receiving messages about translations.
     *
     * @return associated session handler
     */
    @AnyThread
    public @NonNull Handler getHandler() {
      return mHandler;
    }

    /**
     * Translates the session's current page based on given language and criteria specified in the
     * options.
     *
     * @param fromLanguage BCP 47 language tag that the page should be translated from. Usually will
     *     be the suggested detected language or user specified.
     * @param toLanguage BCP 47 language tag that the page should be translated to. Usually will be
     *     the suggested preference language or user specified.
     * @param options If downloadModel is set to true, then any background downloads will occur
     *     automatically. If downloadModel is set to false, then if any background downloads are
     *     required, then the request will fail with an exception, but will continue if the model is
     *     already present.
     * @return Void if the translate process begins or exceptionally if an issue occurs.
     */
    @HandlerThread
    public @NonNull GeckoResult<Void> translate(
        @NonNull final String fromLanguage,
        @NonNull final String toLanguage,
        @Nullable final TranslationOptions options) {
      if (DEBUG) {
        Log.d(
            LOGTAG,
            "Translate page requested - fromLanguage: "
                + fromLanguage
                + " toLanguage: "
                + toLanguage
                + " options: "
                + options);
      }

      if (options != null && options.downloadModel == false) {
        final var translateResult = new GeckoResult<Void>();
        TranslationsController.RuntimeTranslation.checkPairDownloadSize(fromLanguage, toLanguage)
            .then(
                (GeckoResult.OnValueListener<Long, Void>)
                    downloadBytes -> {
                      if (downloadBytes > 0) {
                        translateResult.completeExceptionally(
                            new TranslationsException(
                                TranslationsException.ERROR_MODEL_DOWNLOAD_REQUIRED));
                      } else {
                        // No download required
                        translateResult.completeFrom(this.baseTranslate(fromLanguage, toLanguage));
                      }
                      return null;
                    });
        return translateResult;
      }

      return this.baseTranslate(fromLanguage, toLanguage);
    }

    /**
     * Convenience method for calling {@link #translate(String, String, TranslationOptions)} with a
     * translation pair.
     *
     * @param translationPair the object with a from and to language
     * @param options If downloadModel is set to true, then any background downloads will occur
     *     automatically. If downloadModel is set to false, then if any background downloads are
     *     required, then the request will fail, but will continue if the model is already present.
     * @return Void if the translate process begins or exceptionally if an issue occurs.
     */
    @HandlerThread
    public @NonNull GeckoResult<Void> translate(
        @NonNull final TranslationPair translationPair,
        @Nullable final TranslationOptions options) {
      return translate(translationPair.fromLanguage, translationPair.toLanguage, options);
    }

    /**
     * This will complete a translation using defaults. Before translating, any required models will
     * be downloaded by the toolkit engine.
     *
     * @param sourceLanguage BCP 47 language tag that the page should be translated from. Usually
     *     will be the suggested detected language or user specified.
     * @param targetLanguage BCP 47 language tag that the page should be translated to. Usually will
     *     be the suggested preference language or user specified.
     * @return Void if the translate process begins or exceptionally if an issue occurs.
     */
    @HandlerThread
    private @NonNull GeckoResult<Void> baseTranslate(
        @NonNull final String sourceLanguage, @NonNull final String targetLanguage) {
      ThreadUtils.warnOnHandlerThread();

      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putString("sourceLanguage", sourceLanguage);
      bundle.putString("targetLanguage", targetLanguage);
      return mSession
          .getEventDispatcher()
          .queryVoid(TRANSLATE_EVENT, bundle)
          .map(
              result -> result,
              exception -> {
                if (TranslationsException.isEngineDeactivated(exception)) {
                  return new TranslationsException(TranslationsException.ERROR_ENGINE_DEACTIVATED);
                }
                return new TranslationsException(TranslationsException.ERROR_COULD_NOT_TRANSLATE);
              });
    }

    /**
     * Restores a page to the original or pre-translated state.
     *
     * @return if page restoration process begins or exceptionally if an issue occurs.
     */
    @HandlerThread
    public @NonNull GeckoResult<Void> restoreOriginalPage() {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Restore translated page requested");
      }
      return mSession
          .getEventDispatcher()
          .queryVoid(RESTORE_PAGE_EVENT)
          .map(
              result -> result,
              exception -> {
                if (TranslationsException.isEngineDeactivated(exception)) {
                  return new TranslationsException(TranslationsException.ERROR_ENGINE_DEACTIVATED);
                }
                return new TranslationsException(TranslationsException.ERROR_COULD_NOT_RESTORE);
              });
    }

    /**
     * Gets the setting of the site for whether it should be translated or not.
     *
     * @return The site setting for the page or exceptionally if an issue occurs.
     */
    @HandlerThread
    public @NonNull GeckoResult<Boolean> getNeverTranslateSiteSetting() {
      ThreadUtils.assertOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Retrieving never translate site setting.");
      }
      return mSession
          .getEventDispatcher()
          .queryBoolean(GET_NEVER_TRANSLATE_SITE)
          .map(
              result -> result,
              exception -> {
                if (TranslationsException.isEngineDeactivated(exception)) {
                  return new TranslationsException(TranslationsException.ERROR_ENGINE_DEACTIVATED);
                }
                return new TranslationsException(TranslationsException.ERROR_UNKNOWN);
              });
    }

    /**
     * Sets whether the site should be translated or not.
     *
     * @param neverTranslate Should be set to true if the site should never be translated or false
     *     if it should be translated.
     * @return Void if the operation to set the value completed or exceptionally if an issue
     *     occurred.
     */
    @HandlerThread
    public @NonNull GeckoResult<Void> setNeverTranslateSiteSetting(
        final @NonNull Boolean neverTranslate) {
      ThreadUtils.warnOnHandlerThread();
      if (DEBUG) {
        Log.d(LOGTAG, "Setting never translate site.");
      }
      final GeckoBundle bundle = new GeckoBundle(2);
      bundle.putBoolean("neverTranslate", neverTranslate);
      return mSession
          .getEventDispatcher()
          .queryVoid(SET_NEVER_TRANSLATE_SITE, bundle)
          .map(
              result -> result,
              exception -> {
                if (TranslationsException.isEngineDeactivated(exception)) {
                  return new TranslationsException(TranslationsException.ERROR_ENGINE_DEACTIVATED);
                }
                return new TranslationsException(TranslationsException.ERROR_UNKNOWN);
              });
    }

    /**
     * Options available for translating.
     *
     * <p>Options (default):
     *
     * <p>downloadModel (true) - Downloads any models automatically that are needed for translation.
     */
    @AnyThread
    public static class TranslationOptions {
      /** If the model should be automatically downloaded or stopped. */
      public final @NonNull boolean downloadModel;

      /**
       * Options for translation.
       *
       * @param builder that populated the translation options
       */
      protected TranslationOptions(final @NonNull Builder builder) {
        this.downloadModel = builder.mDownloadModel;
      }

      /** Builder for making translation options. */
      @AnyThread
      public static class Builder {
        /** Public constructor for Builder. */
        public Builder() {}

        /* package */ boolean mDownloadModel = true;

        /**
         * Build setter for the option for downloading a model.
         *
         * @param downloadModel should the model be automatically download or not
         * @return the model to download for the translation options
         */
        public @NonNull Builder downloadModel(final @NonNull boolean downloadModel) {
          mDownloadModel = downloadModel;
          return this;
        }

        /**
         * Final call to build the specified options.
         *
         * @return a constructed translation options
         */
        @AnyThread
        public @NonNull TranslationOptions build() {
          return new TranslationOptions(this);
        }
      }
    }

    /**
     * The translations session delegate is used for receiving translation events and information.
     */
    @AnyThread
    public interface Delegate {
      /**
       * onOfferTranslate occurs when a page should be offered for translation.
       *
       * <p>An offer should occur when all conditions are met:
       *
       * <p>* The page is not in the user's preferred language
       *
       * <p>* The page language is eligible for translation
       *
       * <p>* The host hasn't been offered for translation in this session
       *
       * <p>* No user preferences indicate that translation shouldn't be offered
       *
       * <p>* It is possible to translate
       *
       * <p>Usual use-case is to show a pop-up recommending a translation.
       *
       * @param session The associated GeckoSession.
       */
      default void onOfferTranslate(@NonNull final GeckoSession session) {}

      /**
       * onExpectedTranslate occurs when it is likely the user will want to translate and it is
       * feasible. For example, if the page is in a different language than the user preferred
       * language or languages.
       *
       * <p>Usual use-case is to add a toolbar option for translate.
       *
       * @param session The associated GeckoSession.
       */
      default void onExpectedTranslate(@NonNull final GeckoSession session) {}

      /**
       * onTranslationStateChange occurs when new information about the translation state is
       * available. This includes information when first visiting the page and after calls to
       * translate.
       *
       * @param session The associated GeckoSession.
       * @param translationState The state of the translation as reported by the translation engine.
       */
      default void onTranslationStateChange(
          @NonNull final GeckoSession session, @Nullable final TranslationState translationState) {}
    }

    /** Translation pair is the from language and to language set on the translation state. */
    public static class TranslationPair {
      /** Language the page is translated from originally. */
      public final @Nullable String fromLanguage;

      /** Language the page is translated to. */
      public final @Nullable String toLanguage;

      /**
       * Requested translation pair constructor.
       *
       * @param fromLanguage original language of page (detected or specified)
       * @param toLanguage translated to language of page (detected or specified)
       */
      public TranslationPair(
          @Nullable final String fromLanguage, @Nullable final String toLanguage) {
        this.fromLanguage = fromLanguage;
        this.toLanguage = toLanguage;
      }

      @Override
      public String toString() {
        return "TranslationPair {"
            + "fromLanguage='"
            + fromLanguage
            + '\''
            + ", toLanguage='"
            + toLanguage
            + '\''
            + '}';
      }

      /**
       * Convenience method for deserializing translation state information.
       *
       * @param bundle contains translation pair information.
       * @return translation pair
       */
      /* package */
      static @Nullable TranslationPair fromBundle(final GeckoBundle bundle) {
        if (bundle == null) {
          return null;
        }
        return new TranslationPair(
            bundle.getString("sourceLanguage"), bundle.getString("targetLanguage"));
      }
    }

    /** DetectedLanguages is information that was detected about the page or user preferences. */
    public static class DetectedLanguages {

      /** The user's preferred language tag */
      public final @Nullable String userLangTag;

      /** If the engine supports the document language. */
      public final @NonNull Boolean isDocLangTagSupported;

      /** Detected language tag of page. */
      public final @Nullable String docLangTag;

      /**
       * DetectedLanguages constructor.
       *
       * @param userLangTag - the user's preferred language tag
       * @param isDocLangTagSupported - if the engine supports the document language for translation
       * @param docLangTag - the document's detected language tag
       */
      public DetectedLanguages(
          @Nullable final String userLangTag,
          @NonNull final Boolean isDocLangTagSupported,
          @Nullable final String docLangTag) {
        this.userLangTag = userLangTag;
        this.isDocLangTagSupported = isDocLangTagSupported;
        this.docLangTag = docLangTag;
      }

      @Override
      public String toString() {
        return "DetectedLanguages {"
            + "userLangTag='"
            + userLangTag
            + '\''
            + ", isDocLangTagSupported="
            + isDocLangTagSupported
            + ", docLangTag='"
            + docLangTag
            + '\''
            + '}';
      }

      /**
       * Convenience method for deserializing detected language state information.
       *
       * @param bundle contains detected language information.
       * @return detected language information
       */
      /* package */
      static @Nullable DetectedLanguages fromBundle(final GeckoBundle bundle) {
        if (bundle == null) {
          return null;
        }
        return new DetectedLanguages(
            bundle.getString("userLangTag"),
            bundle.getBoolean("isDocLangTagSupported", false),
            bundle.getString("docLangTag"));
      }
    }

    /** The representation of the translation state. */
    public static class TranslationState {
      /** The language pair to translate. */
      public final @Nullable TranslationPair requestedTranslationPair;

      /** If an error state occurred. */
      public final @Nullable String error;

      /** Detected information about preferences and page information. */
      public final @Nullable DetectedLanguages detectedLanguages;

      /** If the translation engine is ready for use or will need to be loaded. */
      public final @NonNull Boolean isEngineReady;

      /** If the DOM has began visibly changing to the translated text. */
      public final @NonNull Boolean hasVisibleChange;

      /**
       * Translation State constructor.
       *
       * @param requestedTranslationPair the language pair to translate
       * @param error if an error occurred
       * @param detectedLanguages detected language
       * @param isEngineReady if the engine is ready for translations
       * @param hasVisibleChange if the DOM has began to visibly change to translated text
       */
      public TranslationState(
          final @Nullable TranslationPair requestedTranslationPair,
          final @Nullable String error,
          final @Nullable DetectedLanguages detectedLanguages,
          final @NonNull Boolean isEngineReady,
          final @NonNull Boolean hasVisibleChange) {
        this.requestedTranslationPair = requestedTranslationPair;
        this.error = error;
        this.detectedLanguages = detectedLanguages;
        this.isEngineReady = isEngineReady;
        this.hasVisibleChange = hasVisibleChange;
      }

      @Override
      public String toString() {
        return "TranslationState {"
            + "requestedTranslationPair="
            + requestedTranslationPair
            + ", error='"
            + error
            + '\''
            + ", detectedLanguages="
            + detectedLanguages
            + ", isEngineReady="
            + isEngineReady
            + ", hasVisibleChange="
            + hasVisibleChange
            + '}';
      }

      /**
       * Convenience method for deserializing translation state information.
       *
       * @param bundle contains information about translation state.
       * @return translation state
       */
      /* package */
      static @Nullable TranslationState fromBundle(final GeckoBundle bundle) {
        if (bundle == null) {
          return null;
        }
        return new TranslationState(
            TranslationPair.fromBundle(bundle.getBundle("requestedLanguagePair")),
            bundle.getString("error"),
            DetectedLanguages.fromBundle(bundle.getBundle("detectedLanguages")),
            bundle.getBoolean("isEngineReady", false),
            bundle.getBoolean("hasVisibleChange", false));
      }
    }

    /* package */ static class Handler extends GeckoSessionHandler<SessionTranslation.Delegate> {

      private final GeckoSession mSession;

      private Handler(final GeckoSession session) {
        super(
            "GeckoViewTranslations",
            session,
            new String[] {
              ON_OFFER_EVENT, ON_STATE_CHANGE_EVENT,
            });
        mSession = session;
      }

      @Override
      public void handleMessage(
          final Delegate delegate,
          final String event,
          final GeckoBundle message,
          final EventCallback callback) {
        if (DEBUG) {
          Log.d(LOGTAG, "handleMessage " + event);
        }
        if (delegate == null) {
          Log.w(LOGTAG, "The translations session delegate is not set.");
          return;
        }
        if (ON_OFFER_EVENT.equals(event)) {
          delegate.onOfferTranslate(mSession);
          return;
        } else if (ON_STATE_CHANGE_EVENT.equals(event)) {
          final GeckoBundle data = message.getBundle("data");
          final TranslationState translationState = TranslationState.fromBundle(data);
          if (DEBUG) {
            Log.d(LOGTAG, "Received translation state: " + translationState);
          }
          delegate.onTranslationStateChange(mSession, translationState);
          if (translationState != null
              && translationState.detectedLanguages != null
              && translationState.detectedLanguages.docLangTag != null
              && translationState.detectedLanguages.userLangTag != null
              && translationState.detectedLanguages.isDocLangTagSupported) {
            TranslationsController.RuntimeTranslation.isTranslationsEngineSupported()
                .then(
                    (GeckoResult.OnValueListener<Boolean, Void>)
                        value -> {
                          if (value) {
                            delegate.onExpectedTranslate(mSession);
                          }
                          return null;
                        });
            return;
          }
        }
      }
    }
  }

  /** Language display information. */
  public static class Language implements Comparable<Language> {
    /** Language BCP 47 code. */
    public final @NonNull String code;

    /** Language localized display name. */
    public final @Nullable String localizedDisplayName;

    /**
     * Language constructor.
     *
     * @param code BCP 47 language code
     * @param localizedDisplayName how the language should be referred to in the UI.
     */
    public Language(@NonNull final String code, @Nullable final String localizedDisplayName) {
      this.code = code;
      this.localizedDisplayName = localizedDisplayName;
    }

    @Override
    public String toString() {
      if (localizedDisplayName != null) {
        return localizedDisplayName;
      }
      return code;
    }

    /**
     * Comparator for sorting language objects is based on alphabetizing display language {@link
     * #localizedDisplayName}.
     *
     * @param otherLanguage other language being compared
     * @return 1 if this object is earlier, 0 if equal, -1 if this object should be later for
     *     sorting
     */
    @Override
    @AnyThread
    public int compareTo(@Nullable final Language otherLanguage) {
      return this.localizedDisplayName.compareTo(otherLanguage.localizedDisplayName);
    }

    /**
     * Equality checker for language objects is based on BCP 47 code equality {@link #code}.
     *
     * @param otherLanguage other language being compared
     * @return true if the BCP 47 codes match, false if they do not
     */
    @Override
    public boolean equals(@Nullable final Object otherLanguage) {
      if (otherLanguage instanceof Language) {
        return this.code.equals(((Language) otherLanguage).code);
      }
      return false;
    }

    /**
     * Required for overriding equals.
     *
     * @return object hash.
     */
    @Override
    public int hashCode() {
      return Objects.hash(code);
    }

    /**
     * Convenience method for deserializing language information.
     *
     * @param bundle contains language information
     * @return language for display
     */
    /* package */
    static @Nullable Language fromBundle(final GeckoBundle bundle) {
      if (bundle == null) {
        return null;
      }
      try {
        final String variant = bundle.getString("variant", "");
        if (!variant.isEmpty()) {
          // Variants are not currently supported in Android. Ignore this model. See Bug 1943444.
          return null;
        }
        final String code = bundle.getString("langTag", "");
        if (code.equals("")) {
          Log.w(LOGTAG, "Deserialized an empty language code.");
        }
        return new Language(code, bundle.getString("displayName"));
      } catch (final Exception e) {
        Log.w(LOGTAG, "Could not deserialize language object: " + e);
        return null;
      }
    }
  }

  /**
   * An exception to be used when there is an issue retrieving or sending information to the
   * translations toolkit engine.
   */
  public static class TranslationsException extends Exception {

    /**
     * Construct a [TranslationsException]
     *
     * @param code Error code the given exception corresponds to.
     */
    public TranslationsException(final @Code int code) {
      this.code = code;
    }

    /** Default error for unexpected issues. */
    public static final int ERROR_UNKNOWN = -1;

    /** Translations engine does not work on the device architecture. */
    public static final int ERROR_ENGINE_NOT_SUPPORTED = -2;

    /** Generic could not compete a translation error. */
    public static final int ERROR_COULD_NOT_TRANSLATE = -3;

    /** Generic could not restore the page after a translation error. */
    public static final int ERROR_COULD_NOT_RESTORE = -4;

    /** Could not load language options error. */
    public static final int ERROR_COULD_NOT_LOAD_LANGUAGES = -5;

    /** The language is not supported for translation. */
    public static final int ERROR_LANGUAGE_NOT_SUPPORTED = -6;

    /** Could not retrieve information on the language model. */
    public static final int ERROR_MODEL_COULD_NOT_RETRIEVE = -7;

    /** Could not delete the language model. */
    public static final int ERROR_MODEL_COULD_NOT_DELETE = -8;

    /** Could not download the language model. */
    public static final int ERROR_MODEL_COULD_NOT_DOWNLOAD = -9;

    /** A language is required for language scoped requests. */
    public static final int ERROR_MODEL_LANGUAGE_REQUIRED = -10;

    /** A download is required and the translate request specified do not download. */
    public static final int ERROR_MODEL_DOWNLOAD_REQUIRED = -11;

    /**
     * The translations engine is likely blocked by AI controls or else deactivated or in an invalid
     * state.
     */
    public static final int ERROR_ENGINE_DEACTIVATED = -12;

    /** Translation exception error codes. */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(
        value = {
          ERROR_UNKNOWN,
          ERROR_ENGINE_NOT_SUPPORTED,
          ERROR_COULD_NOT_TRANSLATE,
          ERROR_COULD_NOT_RESTORE,
          ERROR_COULD_NOT_LOAD_LANGUAGES,
          ERROR_LANGUAGE_NOT_SUPPORTED,
          ERROR_MODEL_COULD_NOT_RETRIEVE,
          ERROR_MODEL_COULD_NOT_DELETE,
          ERROR_MODEL_COULD_NOT_DOWNLOAD,
          ERROR_MODEL_LANGUAGE_REQUIRED,
          ERROR_MODEL_DOWNLOAD_REQUIRED,
          ERROR_ENGINE_DEACTIVATED
        })
    public @interface Code {}

    /** {@link Code} that provides more information about this exception. */
    public final @Code int code;

    @Override
    public String toString() {
      return "TranslationsException: " + code;
    }

    /**
     * Convenience method for determining if translations isn't working because {@link
     * AIFeaturesController} has actively blocked the feature.
     *
     * <p>If it isn't {@link AIFeaturesController}, then likely something else has gone wrong and
     * the actor is missing.
     *
     * @return True when it is likely that the feature has been deactivated.
     */
    static boolean isEngineDeactivated(@NonNull final Throwable exception) {
      return exception instanceof EventDispatcher.QueryException
          && ((EventDispatcher.QueryException) exception)
              .data
              .toString()
              .contains("No such JSWindowActor 'Translations'");
    }
  }
}
