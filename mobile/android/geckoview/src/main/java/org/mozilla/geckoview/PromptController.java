/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview;

import android.util.Base64;
import android.util.Log;
import java.security.Principal;
import java.util.HashMap;
import java.util.Map;
import javax.security.auth.x500.X500Principal;
import org.json.JSONException;
import org.mozilla.gecko.util.EventCallback;
import org.mozilla.gecko.util.GeckoBundle;
import org.mozilla.geckoview.Autocomplete.AddressSaveOption;
import org.mozilla.geckoview.Autocomplete.AddressSelectOption;
import org.mozilla.geckoview.Autocomplete.CreditCardSaveOption;
import org.mozilla.geckoview.Autocomplete.CreditCardSelectOption;
import org.mozilla.geckoview.Autocomplete.LoginSaveOption;
import org.mozilla.geckoview.Autocomplete.LoginSelectOption;
import org.mozilla.geckoview.GeckoSession.PromptDelegate;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AlertPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AuthPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AuthPrompt.AuthOptions;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AutocompleteRequest;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BasePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BasePrompt.Observer;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.BeforeUnloadPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.ButtonPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.CertificateRequest;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.ChoicePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.ColorPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.DateTimePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.FilePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.FolderUploadPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.AccountSelectorPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.PrivacyPolicyPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.IdentityCredential.ProviderSelectorPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PopupPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptInstanceDelegate;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.PromptResponse;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.RedirectPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.RepostConfirmPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.SharePrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.TextPrompt;
import org.mozilla.geckoview.GeckoSession.PromptDelegate.WebAuthnRelatedOriginPrompt;

/* package */ class PromptController {
  private static final String LOGTAG = "Prompts";

  private static class PromptStorage implements BasePrompt.Observer {
    private final Map<String, BasePrompt> mPrompts = new HashMap<>();

    public void addPrompt(final String id, final BasePrompt prompt) {
      if (mPrompts.containsKey(id)) {
        Log.e(LOGTAG, "Prompt already exists! id=" + id);
        if (BuildConfig.DEBUG_BUILD) {
          throw new RuntimeException("Prompt already exists! id=" + id);
        }
      }
      mPrompts.put(id, prompt);
    }

    @Override
    public void onPromptCompleted(final BasePrompt prompt) {
      // No need to notify this delegate since the prompt has been completed already.
      mPrompts.remove(prompt.id);
    }

    public void dismiss(final String id) {
      final BasePrompt prompt = mPrompts.get(id);
      if (prompt == null) {
        return;
      }
      final PromptInstanceDelegate delegate = prompt.getDelegate();
      if (delegate != null) {
        delegate.onPromptDismiss(prompt);
      }
      mPrompts.remove(prompt.id);
    }

    public boolean contains(final String id) {
      return mPrompts.containsKey(id);
    }

    public void update(final BasePrompt prompt) {
      final BasePrompt previousPrompt = mPrompts.get(prompt.id);
      if (previousPrompt == null) {
        return;
      }
      final PromptInstanceDelegate delegate = previousPrompt.getDelegate();
      if (delegate == null) {
        return;
      }
      prompt.setDelegate(delegate);
      delegate.onPromptUpdate(prompt);
      mPrompts.put(prompt.id, prompt);
    }
  }

  final PromptStorage mStorage = new PromptStorage();

  public void dismissPrompt(final String id) {
    mStorage.dismiss(id);
  }

  public void updatePrompt(final GeckoBundle message) {
    final String type = message.getString("type");
    final PromptHandler<?> handler = sPromptHandlers.handlerFor(type);
    if (handler == null) {
      // Invalid prompt message type to update the prompt.
      return;
    }
    final BasePrompt prompt = handler.newPrompt(message, mStorage);
    if (prompt == null) {
      // Invalid prompt message to update the prompt.
      return;
    }
    if (!mStorage.contains(prompt.id)) {
      // Invalid prompt id to update the prompt. Dismissed?
      return;
    }

    mStorage.update(prompt);
  }

  public void handleEvent(
      final GeckoSession session, final GeckoBundle message, final EventCallback callback) {
    Log.d(LOGTAG, "handleEvent " + message.getString("type"));
    final PromptDelegate delegate = session.getPromptDelegate();
    if (delegate == null) {
      // Default behavior is same as calling dismiss() on callback.
      callback.sendSuccess(null);
      return;
    }

    final String type = message.getString("type");
    final PromptHandler<?> handler = sPromptHandlers.handlerFor(type);
    if (handler == null) {
      callback.sendError("Invalid type: " + type);
      return;
    }
    final GeckoResult<PromptResponse> res = getResponse(message, session, delegate, handler);

    if (res == null) {
      // Adhere to default behavior if the delegate returns null.
      callback.sendSuccess(null);
    } else {
      res.accept(
          value -> value.dispatch(callback),
          exception -> callback.sendError("Failed to get prompt response."));
    }
  }

  private <PromptType extends BasePrompt> GeckoResult<PromptResponse> getResponse(
      final GeckoBundle message,
      final GeckoSession session,
      final PromptDelegate delegate,
      final PromptHandler<PromptType> handler) {
    final PromptType prompt = handler.newPrompt(message, mStorage);
    if (prompt == null) {
      try {
        Log.e(LOGTAG, "Invalid prompt: " + message.toJSONObject().toString());
      } catch (final JSONException ex) {
        Log.e(LOGTAG, "Invalid prompt, invalid data", ex);
      }

      return GeckoResult.fromException(new IllegalArgumentException("Invalid prompt data."));
    }

    mStorage.addPrompt(prompt.id, prompt);
    return handler.callDelegate(prompt, session, delegate);
  }

  private interface PromptHandler<PromptType extends BasePrompt> {
    PromptType newPrompt(GeckoBundle info, Observer observer);

    GeckoResult<PromptResponse> callDelegate(
        PromptType prompt, GeckoSession session, PromptDelegate delegate);
  }

  private static final class AlertHandler implements PromptHandler<AlertPrompt> {
    @Override
    public AlertPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new AlertPrompt(
          info.getString("id"), info.getString("title"), info.getString("msg"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AlertPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onAlertPrompt(session, prompt);
    }
  }

  private static final class BeforeUnloadHandler implements PromptHandler<BeforeUnloadPrompt> {
    @Override
    public BeforeUnloadPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new BeforeUnloadPrompt(info.getString("id"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final BeforeUnloadPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onBeforeUnloadPrompt(session, prompt);
    }
  }

  private static final class WebAuthnRelatedOriginHandler
      implements PromptHandler<WebAuthnRelatedOriginPrompt> {
    @Override
    public WebAuthnRelatedOriginPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new WebAuthnRelatedOriginPrompt(
          info.getString("id"),
          info.getString("origin"),
          info.getString("rpId"),
          info.getBoolean("isCreate"),
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final WebAuthnRelatedOriginPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onWebAuthnRelatedOriginPrompt(session, prompt);
    }
  }

  private static final class ButtonHandler implements PromptHandler<ButtonPrompt> {
    @Override
    public ButtonPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new ButtonPrompt(
          info.getString("id"), info.getString("title"), info.getString("msg"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final ButtonPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onButtonPrompt(session, prompt);
    }
  }

  private static final class TextHandler implements PromptHandler<TextPrompt> {
    @Override
    public TextPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new TextPrompt(
          info.getString("id"),
          info.getString("title"),
          info.getString("msg"),
          info.getString("value"),
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final TextPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onTextPrompt(session, prompt);
    }
  }

  private static final class AuthHandler implements PromptHandler<AuthPrompt> {
    @Override
    public AuthPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new AuthPrompt(
          info.getString("id"),
          info.getString("title"),
          info.getString("msg"),
          new AuthOptions(info.getBundle("options")),
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AuthPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onAuthPrompt(session, prompt);
    }
  }

  private static final class CertificateHandler implements PromptHandler<CertificateRequest> {
    @Override
    public CertificateRequest newPrompt(final GeckoBundle info, final Observer observer) {
      final String[] issuersEncoded = info.getStringArray("issuers");
      Principal[] issuers = null;
      if (issuersEncoded != null && issuersEncoded.length > 0) {
        issuers = new Principal[issuersEncoded.length];
        for (int i = 0; i < issuersEncoded.length; i++) {
          // The encoded issuers have already been decoded by NSS, but if NSS
          // is more permissive than X500Principal, decoding could fail. In
          // that case, fail open by not filtering by issuer at all.
          try {
            issuers[i] = new X500Principal(Base64.decode(issuersEncoded[i], Base64.DEFAULT));
          } catch (final IllegalArgumentException ex) {
            Log.e(LOGTAG, "Error decoding issuer '" + issuersEncoded[i] + "'", ex);
            issuers = null;
            break;
          }
        }
      }
      return new CertificateRequest(
          info.getString("id"), observer, info.getString("host"), issuers);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final CertificateRequest prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onRequestCertificate(session, prompt);
    }
  }

  private static final class ChoiceHandler implements PromptHandler<ChoicePrompt> {
    @Override
    public ChoicePrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final int intMode;
      final String mode = info.getString("mode");
      if ("menu".equals(mode)) {
        intMode = ChoicePrompt.Type.MENU;
      } else if ("single".equals(mode)) {
        intMode = ChoicePrompt.Type.SINGLE;
      } else if ("multiple".equals(mode)) {
        intMode = ChoicePrompt.Type.MULTIPLE;
      } else {
        return null;
      }

      final GeckoBundle[] choiceBundles = info.getBundleArray("choices");
      final ChoicePrompt.Choice[] choices;
      if (choiceBundles == null || choiceBundles.length == 0) {
        choices = new ChoicePrompt.Choice[0];
      } else {
        choices = new ChoicePrompt.Choice[choiceBundles.length];
        for (int i = 0; i < choiceBundles.length; i++) {
          choices[i] = new ChoicePrompt.Choice(choiceBundles[i]);
        }
      }

      return new ChoicePrompt(
          info.getString("id"),
          info.getString("title"),
          info.getString("msg"),
          intMode,
          choices,
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final ChoicePrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onChoicePrompt(session, prompt);
    }
  }

  private static final class ColorHandler implements PromptHandler<ColorPrompt> {
    @Override
    public ColorPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new ColorPrompt(
          info.getString("id"),
          info.getString("title"),
          info.getString("value"),
          info.getStringArray("predefinedValues"),
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final ColorPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onColorPrompt(session, prompt);
    }
  }

  private static final class DateTimeHandler implements PromptHandler<DateTimePrompt> {
    @Override
    public DateTimePrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final String mode = info.getString("mode");
      final int intMode;
      if ("date".equals(mode)) {
        intMode = DateTimePrompt.Type.DATE;
      } else if ("month".equals(mode)) {
        intMode = DateTimePrompt.Type.MONTH;
      } else if ("week".equals(mode)) {
        intMode = DateTimePrompt.Type.WEEK;
      } else if ("time".equals(mode)) {
        intMode = DateTimePrompt.Type.TIME;
      } else if ("datetime-local".equals(mode)) {
        intMode = DateTimePrompt.Type.DATETIME_LOCAL;
      } else {
        return null;
      }

      final String defaultValue = info.getString("value");
      final String minValue = info.getString("min");
      final String maxValue = info.getString("max");
      final String stepValue = info.getString("step");
      return new DateTimePrompt(
          info.getString("id"),
          info.getString("title"),
          intMode,
          defaultValue,
          minValue,
          maxValue,
          stepValue,
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final DateTimePrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onDateTimePrompt(session, prompt);
    }
  }

  private static final class FileHandler implements PromptHandler<FilePrompt> {
    @Override
    public FilePrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final String mode = info.getString("mode");
      final int intMode;
      if ("single".equals(mode)) {
        intMode = FilePrompt.Type.SINGLE;
      } else if ("multiple".equals(mode)) {
        intMode = FilePrompt.Type.MULTIPLE;
      } else if ("folder".equals(mode)) {
        intMode = FilePrompt.Type.FOLDER;
      } else {
        return null;
      }

      final String[] mimeTypes = info.getStringArray("mimeTypes");
      final int capture = info.getInt("capture");
      return new FilePrompt(
          info.getString("id"), info.getString("title"), intMode, capture, mimeTypes, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final FilePrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onFilePrompt(session, prompt);
    }
  }

  private static final class FolderUploadHandler implements PromptHandler<FolderUploadPrompt> {
    @Override
    public FolderUploadPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new FolderUploadPrompt(
          info.getString("id"), info.getString("directoryName"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final FolderUploadPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onFolderUploadPrompt(session, prompt);
    }
  }

  private static final class PopupHandler implements PromptHandler<PopupPrompt> {
    @Override
    public PopupPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new PopupPrompt(info.getString("id"), info.getString("targetUri"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final PopupPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onPopupPrompt(session, prompt);
    }
  }

  private static final class RedirectHandler implements PromptHandler<RedirectPrompt> {
    @Override
    public RedirectPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new RedirectPrompt(info.getString("id"), info.getString("targetUri"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final RedirectPrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onRedirectPrompt(session, prompt);
    }
  }

  private static final class RepostHandler implements PromptHandler<RepostConfirmPrompt> {
    @Override
    public RepostConfirmPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new RepostConfirmPrompt(info.getString("id"), observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final RepostConfirmPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onRepostConfirmPrompt(session, prompt);
    }
  }

  private static final class ShareHandler implements PromptHandler<SharePrompt> {
    @Override
    public SharePrompt newPrompt(final GeckoBundle info, final Observer observer) {
      return new SharePrompt(
          info.getString("id"),
          info.getString("title"),
          info.getString("text"),
          info.getString("uri"),
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final SharePrompt prompt, final GeckoSession session, final PromptDelegate delegate) {
      return delegate.onSharePrompt(session, prompt);
    }
  }

  private static final class LoginSaveHandler
      implements PromptHandler<AutocompleteRequest<LoginSaveOption>> {
    @Override
    public AutocompleteRequest<LoginSaveOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final int hint = info.getInt("hint");
      final GeckoBundle[] loginBundles = info.getBundleArray("logins");

      if (loginBundles == null) {
        return null;
      }

      final Autocomplete.LoginSaveOption[] options =
          new Autocomplete.LoginSaveOption[loginBundles.length];

      for (int i = 0; i < options.length; ++i) {
        options[i] =
            new Autocomplete.LoginSaveOption(new Autocomplete.LoginEntry(loginBundles[i]), hint);
      }

      return new AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<LoginSaveOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onLoginSave(session, prompt);
    }
  }

  private static final class CreditCardSaveHandler
      implements PromptHandler<AutocompleteRequest<CreditCardSaveOption>> {
    @Override
    public AutocompleteRequest<CreditCardSaveOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final int hint = info.getInt("hint");
      final GeckoBundle[] creditCardBundles = info.getBundleArray("creditCards");

      if (creditCardBundles == null) {
        return null;
      }

      final Autocomplete.CreditCardSaveOption[] options =
          new Autocomplete.CreditCardSaveOption[creditCardBundles.length];

      for (int i = 0; i < options.length; ++i) {
        options[i] =
            new Autocomplete.CreditCardSaveOption(
                new Autocomplete.CreditCard(creditCardBundles[i]), hint);
      }

      return new PromptDelegate.AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<CreditCardSaveOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onCreditCardSave(session, prompt);
    }
  }

  private static final class AddressSaveHandler
      implements PromptHandler<AutocompleteRequest<AddressSaveOption>> {
    @Override
    public AutocompleteRequest<AddressSaveOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final GeckoBundle[] addressBundles = info.getBundleArray("addresses");

      if (addressBundles == null) {
        return null;
      }

      final Autocomplete.AddressSaveOption[] options =
          new Autocomplete.AddressSaveOption[addressBundles.length];

      final int hint = info.getInt("hint");
      for (int i = 0; i < options.length; ++i) {
        options[i] =
            new Autocomplete.AddressSaveOption(new Autocomplete.Address(addressBundles[i]), hint);
      }

      return new AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<AddressSaveOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onAddressSave(session, prompt);
    }
  }

  private static final class LoginSelectHandler
      implements PromptHandler<AutocompleteRequest<LoginSelectOption>> {
    @Override
    public AutocompleteRequest<LoginSelectOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final GeckoBundle[] optionBundles = info.getBundleArray("options");

      if (optionBundles == null) {
        return null;
      }

      final Autocomplete.LoginSelectOption[] options =
          new Autocomplete.LoginSelectOption[optionBundles.length];

      for (int i = 0; i < options.length; ++i) {
        options[i] = Autocomplete.LoginSelectOption.fromBundle(optionBundles[i]);
      }
      return new AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<LoginSelectOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onLoginSelect(session, prompt);
    }
  }

  private static final class IdentityCredentialSelectProviderHandler
      implements PromptHandler<ProviderSelectorPrompt> {
    @Override
    public ProviderSelectorPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final GeckoBundle[] providerBundles = info.getBundleArray("providers");
      if (providerBundles == null) {
        return null;
      }

      final ProviderSelectorPrompt.Provider[] providers =
          new ProviderSelectorPrompt.Provider[providerBundles.length];

      for (int i = 0; i < providerBundles.length; ++i) {
        providers[i] = ProviderSelectorPrompt.Provider.fromBundle(providerBundles[i]);
      }

      return new ProviderSelectorPrompt(info.getString("id"), providers, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final ProviderSelectorPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onSelectIdentityCredentialProvider(session, prompt);
    }
  }

  private static final class IdentityCredentialSelectAccountHandler
      implements PromptHandler<AccountSelectorPrompt> {
    @Override
    public AccountSelectorPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final GeckoBundle providerBundle = info.getBundle("accounts");
      if (providerBundle == null) {
        return null;
      }
      final GeckoBundle[] accountBundles = providerBundle.getBundleArray("accounts");
      if (accountBundles == null) {
        return null;
      }

      final AccountSelectorPrompt.Account[] accounts =
          new AccountSelectorPrompt.Account[accountBundles.length];

      for (int i = 0; i < accountBundles.length; ++i) {
        accounts[i] = AccountSelectorPrompt.Account.fromBundle(accountBundles[i]);
      }

      final AccountSelectorPrompt.Provider provider =
          AccountSelectorPrompt.Provider.fromBundle(providerBundle.getBundle("provider"));

      return new AccountSelectorPrompt(info.getString("id"), accounts, provider, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AccountSelectorPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onSelectIdentityCredentialAccount(session, prompt);
    }
  }

  private static final class IdentityCredentialShowPrivacyPolicyHandler
      implements PromptHandler<PrivacyPolicyPrompt> {
    @Override
    public PrivacyPolicyPrompt newPrompt(final GeckoBundle info, final Observer observer) {
      final String privacyPolicyUrl = info.getString("privacyPolicyUrl");
      final String termsOfServiceUrl = info.getString("termsOfServiceUrl");
      final String providerDomain = info.getString("providerDomain");
      final String host = info.getString("host");
      final String icon = info.getString("icon");

      return new PrivacyPolicyPrompt(
          info.getString("id"),
          privacyPolicyUrl,
          termsOfServiceUrl,
          providerDomain,
          host,
          icon,
          observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final PrivacyPolicyPrompt prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onShowPrivacyPolicyIdentityCredential(session, prompt);
    }
  }

  private static final class CreditCardSelectHandler
      implements PromptHandler<AutocompleteRequest<CreditCardSelectOption>> {
    @Override
    public AutocompleteRequest<CreditCardSelectOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final GeckoBundle[] optionBundles = info.getBundleArray("options");

      if (optionBundles == null) {
        return null;
      }

      final Autocomplete.CreditCardSelectOption[] options =
          new Autocomplete.CreditCardSelectOption[optionBundles.length];

      for (int i = 0; i < options.length; ++i) {
        options[i] = Autocomplete.CreditCardSelectOption.fromBundle(optionBundles[i]);
      }

      return new AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<CreditCardSelectOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onCreditCardSelect(session, prompt);
    }
  }

  private static final class AddressSelectHandler
      implements PromptHandler<AutocompleteRequest<AddressSelectOption>> {
    @Override
    public AutocompleteRequest<AddressSelectOption> newPrompt(
        final GeckoBundle info, final Observer observer) {
      final GeckoBundle[] optionBundles = info.getBundleArray("options");

      if (optionBundles == null) {
        return null;
      }

      final Autocomplete.AddressSelectOption[] options =
          new Autocomplete.AddressSelectOption[optionBundles.length];

      for (int i = 0; i < options.length; ++i) {
        options[i] = Autocomplete.AddressSelectOption.fromBundle(optionBundles[i]);
      }

      return new AutocompleteRequest<>(info.getString("id"), options, observer);
    }

    @Override
    public GeckoResult<PromptResponse> callDelegate(
        final AutocompleteRequest<AddressSelectOption> prompt,
        final GeckoSession session,
        final PromptDelegate delegate) {
      return delegate.onAddressSelect(session, prompt);
    }
  }

  private static class PromptHandlers {
    final Map<String, PromptHandler<?>> mPromptHandlers = new HashMap<>();

    public void register(final PromptHandler<?> handler, final String type) {
      mPromptHandlers.put(type, handler);
    }

    public PromptHandler<?> handlerFor(final String type) {
      return mPromptHandlers.get(type);
    }
  }

  private static final PromptHandlers sPromptHandlers = new PromptHandlers();

  static {
    sPromptHandlers.register(new AlertHandler(), "alert");
    sPromptHandlers.register(new BeforeUnloadHandler(), "beforeUnload");
    sPromptHandlers.register(new WebAuthnRelatedOriginHandler(), "webauthn-related-origin");
    sPromptHandlers.register(new ButtonHandler(), "button");
    sPromptHandlers.register(new TextHandler(), "text");
    sPromptHandlers.register(new AuthHandler(), "auth");
    sPromptHandlers.register(new CertificateHandler(), "certificate");
    sPromptHandlers.register(new ChoiceHandler(), "choice");
    sPromptHandlers.register(new ColorHandler(), "color");
    sPromptHandlers.register(new DateTimeHandler(), "datetime");
    sPromptHandlers.register(new FileHandler(), "file");
    sPromptHandlers.register(new FolderUploadHandler(), "folderUpload");
    sPromptHandlers.register(new PopupHandler(), "popup");
    sPromptHandlers.register(new RedirectHandler(), "redirect");
    sPromptHandlers.register(new RepostHandler(), "repost");
    sPromptHandlers.register(new ShareHandler(), "share");
    sPromptHandlers.register(new LoginSaveHandler(), "Autocomplete:Save:Login");
    sPromptHandlers.register(new CreditCardSaveHandler(), "Autocomplete:Save:CreditCard");
    sPromptHandlers.register(new AddressSaveHandler(), "Autocomplete:Save:Address");
    sPromptHandlers.register(new LoginSelectHandler(), "Autocomplete:Select:Login");
    sPromptHandlers.register(
        new IdentityCredentialSelectProviderHandler(), "IdentityCredential:Select:Provider");
    sPromptHandlers.register(
        new IdentityCredentialShowPrivacyPolicyHandler(), "IdentityCredential:Show:Policy");
    sPromptHandlers.register(
        new IdentityCredentialSelectAccountHandler(), "IdentityCredential:Select:Account");
    sPromptHandlers.register(new CreditCardSelectHandler(), "Autocomplete:Select:CreditCard");
    sPromptHandlers.register(new AddressSelectHandler(), "Autocomplete:Select:Address");
  }
}
