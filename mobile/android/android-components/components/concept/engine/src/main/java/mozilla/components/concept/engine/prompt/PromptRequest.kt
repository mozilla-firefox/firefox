/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.engine.prompt

import android.content.Context
import android.net.Uri
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication.Level
import mozilla.components.concept.engine.prompt.PromptRequest.Authentication.Method
import mozilla.components.concept.engine.prompt.PromptRequest.File
import mozilla.components.concept.engine.prompt.PromptRequest.TimeSelection.Type
import mozilla.components.concept.identitycredential.Account
import mozilla.components.concept.identitycredential.Provider
import mozilla.components.concept.storage.Address
import mozilla.components.concept.storage.CreditCardEntry
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginEntry
import java.security.Principal
import java.util.UUID

/**
 * Value type that represents a request for showing a native dialog for prompt web content.
 *
 * @param shouldDismissOnLoad Whether or not the dialog should automatically be dismissed when a new page is loaded.
 * Defaults to `true`.
 * @param uid [PromptRequest] unique identifier. Defaults to a random UUID.
 * (This two parameters, though present in all subclasses are not evaluated in subclasses equals() calls)
 */
sealed class PromptRequest(
    val shouldDismissOnLoad: Boolean = true,
    val uid: String = UUID.randomUUID().toString(),
) {
    /**
     * Value type that represents a request for a single choice prompt.
     * @property choices All the possible options.
     * @property onConfirm A callback indicating which option was selected.
     * @property onDismiss A callback executed when dismissed.
     */
    data class SingleChoice(
        val choices: Array<Choice>,
        val onConfirm: (Choice) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a multiple choice prompt.
     * @property choices All the possible options.
     * @property onConfirm A callback indicating witch options has been selected.
     * @property onDismiss A callback executed when dismissed.
     */
    data class MultipleChoice(
        val choices: Array<Choice>,
        val onConfirm: (Array<Choice>) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a menu choice prompt.
     * @property choices All the possible options.
     * @property onConfirm A callback indicating which option was selected.
     * @property onDismiss A callback executed when dismissed.
     */
    data class MenuChoice(
        val choices: Array<Choice>,
        val onConfirm: (Choice) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for an alert prompt.
     * @property title of the dialog.
     * @property message the body of the dialog.
     * @property hasShownManyDialogs tells if this page has shown multiple prompts within a short period of time.
     * @property onConfirm tells the web page if it should continue showing alerts or not.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class Alert(
        val title: String,
        val message: String,
        val hasShownManyDialogs: Boolean = false,
        val onConfirm: (Boolean) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * BeforeUnloadPrompt represents the onbeforeunload prompt.
     * This prompt is shown when a user is leaving a website and there is formation pending to be saved.
     * For more information see https://developer.mozilla.org/en-US/docs/Web/API/WindowEventHandlers/onbeforeunload.
     * @property title of the dialog.
     * @property onLeave callback to notify that the user wants leave the site.
     * @property onStay callback to notify that the user wants stay in the site.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class BeforeUnload(
        val title: String,
        val onLeave: () -> Unit,
        val onStay: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(shouldDismissOnLoad = false), Dismissible

    /**
     * Value type that represents a request for a client authentication certificate prompt.
     * @property host the domain (or IP address) that requested the certificate.
     * @property issuers array of X.500 Distinguished Names identified as acceptable issuers.
     * @property onComplete callback that is called with the chosen certificate alias (or null if
     * none was chosen) when the user deals with the prompt.
     */
    data class CertificateRequest(
        val host: String,
        val issuers: Array<Principal>?,
        val onComplete: (String?) -> Unit,
    ) : PromptRequest()

    /**
     * Value type that represents a request for a save credit card prompt.
     * @property creditCard the [CreditCardEntry] to save or update.
     * @property onConfirm callback that is called when the user confirms the save credit card request.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class SaveCreditCard(
        val creditCard: CreditCardEntry,
        val onConfirm: (CreditCardEntry) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(shouldDismissOnLoad = false), Dismissible

    /**
     * Value type that represents Identity Credential request prompts.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    sealed class IdentityCredential(
        override val onDismiss: () -> Unit,
    ) : PromptRequest(shouldDismissOnLoad = false), Dismissible {
        /**
         * Value type that represents Identity Credential request for selecting a [Provider] prompt.
         * @property providers A list of providers which the user could select from.
         * @property onConfirm callback to let the page know the user selected a provider.
         * @property onDismiss callback to let the page know the user dismissed the dialog.
         */
        data class SelectProvider(
            val providers: List<Provider>,
            val onConfirm: (Provider) -> Unit,
            override val onDismiss: () -> Unit,
        ) : IdentityCredential(onDismiss), Dismissible

        /**
         * Value type that represents Identity Credential request for selecting an [Account] prompt.
         * @property accounts A list of accounts which the user could select from.
         * @property providerName The name of the provider that will be used for the login
         * @property onConfirm callback to let the page know the user selected an account.
         * @property onDismiss callback to let the page know the user dismissed the dialog.
         */
        data class SelectAccount(
            val accounts: List<Account>,
            val provider: Provider,
            val onConfirm: (Account) -> Unit,
            override val onDismiss: () -> Unit,
        ) : IdentityCredential(onDismiss), Dismissible

        /**
         * Value type that represents Identity Credential request for a privacy policy prompt.
         * @property privacyPolicyUrl A The URL where the policy for using this provider is hosted.
         * @property termsOfServiceUrl The URL where the terms of service for using this provider are.
         * @property providerDomain The domain of the provider.
         * @property host The host of the provider.
         * @property icon A base64 string for given icon for the provider; may be null.
         * @property onConfirm callback to let the page know the user have confirmed or not the privacy policy.
         * @property onDismiss callback to let the page know the user dismissed the dialog.
         */
        data class PrivacyPolicy(
            val privacyPolicyUrl: String,
            val termsOfServiceUrl: String,
            val providerDomain: String,
            val host: String,
            val icon: String?,
            val onConfirm: (Boolean) -> Unit,
            override val onDismiss: () -> Unit,
        ) : IdentityCredential(onDismiss), Dismissible
    }

    /**
     * Value type that represents a request for a select credit card prompt.
     * @property creditCards a list of [CreditCardEntry]s to select from.
     * @property onConfirm callback that is called when the user confirms the credit card selection.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class SelectCreditCard(
        val creditCards: List<CreditCardEntry>,
        val onConfirm: (CreditCardEntry) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a save login prompt.
     * @property hint a value that helps to determine the appropriate prompting behavior.
     * @property logins a list of logins that are associated with the current domain.
     * @property onConfirm callback that is called when the user wants to save the login.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class SaveLoginPrompt(
        val hint: Int,
        val logins: List<LoginEntry>,
        val onConfirm: (LoginEntry) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(shouldDismissOnLoad = false), Dismissible

    /**
     * Value type that represents a request for a select login prompt.
     * @property logins a list of logins that are associated with the current domain.
     * @property generatedPassword the suggested strong password that was generated.
     * @property onConfirm callback that is called when the user wants to select the login.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class SelectLoginPrompt(
        val logins: List<Login>,
        val generatedPassword: String?,
        val onConfirm: (Login) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a select address prompt.
     *
     * This prompt is triggered by the user focusing on an address field.
     *
     * @property addresses List of addresses for the user to choose from.
     * @property onConfirm Callback used to confirm the selected address.
     * @property onDismiss Callback used to dismiss the address prompt.
     */
    data class SelectAddress(
        val addresses: List<Address>,
        val onConfirm: (Address) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for an alert prompt to enter a message.
     * @property title title of the dialog.
     * @property inputLabel the label of the field the user should fill.
     * @property inputValue the default value of the field.
     * @property hasShownManyDialogs tells if this page has shown multiple prompts within a short period of time.
     * @property onConfirm tells the web page if it should continue showing alerts or not.
     * @property onDismiss callback to let the page know the user dismissed the dialog.
     */
    data class TextPrompt(
        val title: String,
        val inputLabel: String,
        val inputValue: String,
        val hasShownManyDialogs: Boolean = false,
        val onConfirm: (Boolean, String) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a date prompt for picking a year, month, and day.
     * @property title of the dialog.
     * @property initialDate date that dialog should be set by default.
     * @property minimumDate date allow to be selected.
     * @property maximumDate date allow to be selected.
     * @property type indicate which [Type] of selection de user wants.
     * @property onConfirm callback that is called when the date is selected.
     * @property onClear callback that is called when the user requests the picker to be clear up.
     * @property onDismiss A callback executed when dismissed.
     */
    @Suppress("LongParameterList")
    class TimeSelection(
        val title: String,
        val initialDate: java.util.Date,
        val minimumDate: java.util.Date?,
        val maximumDate: java.util.Date?,
        val stepValue: String? = null,
        val type: Type = Type.DATE,
        val onConfirm: (java.util.Date) -> Unit,
        val onClear: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible {
        enum class Type {
            DATE, DATE_AND_TIME, TIME, MONTH
        }
    }

    /**
     * Value type that represents a request for a selecting one or multiple files.
     * @property mimeTypes a set of allowed mime types. Only these file types can be selected.
     * @property isMultipleFilesSelection true if the user can select more that one file false otherwise.
     * @property captureMode indicates if the local media capturing capabilities should be used,
     * such as the camera or microphone.
     * @property onSingleFileSelected callback to notify that the user has selected a single file.
     * @property onMultipleFilesSelected callback to notify that the user has selected multiple files.
     * @property onDismiss callback to notify that the user has canceled the file selection.
     */
    data class File(
        val mimeTypes: Array<out String>,
        val isMultipleFilesSelection: Boolean = false,
        val captureMode: FacingMode = FacingMode.NONE,
        val onSingleFileSelected: (Context, Uri) -> Unit,
        val onMultipleFilesSelected: (Context, Array<Uri>) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible {

        /**
         * @deprecated Use the new primary constructor.
         */
        constructor(
            mimeTypes: Array<out String>,
            isMultipleFilesSelection: Boolean,
            onSingleFileSelected: (Context, Uri) -> Unit,
            onMultipleFilesSelected: (Context, Array<Uri>) -> Unit,
            onDismiss: () -> Unit,
        ) : this(
            mimeTypes,
            isMultipleFilesSelection,
            FacingMode.NONE,
            onSingleFileSelected,
            onMultipleFilesSelected,
            onDismiss,
        )

        enum class FacingMode {
            NONE, ANY, FRONT_CAMERA, BACK_CAMERA
        }
        companion object {
            /**
             * Default default directory name for temporary uploads.
             */
            const val DEFAULT_UPLOADS_DIR_NAME = "/uploads"
        }
    }

    /**
     * Value type that represents a request for an authentication prompt.
     * For more related info take a look at
     * <a href="https://developer.mozilla.org/en-US/docs/Web/HTTP/Authentication>MDN docs</a>
     * @property uri The URI for the auth request or null if unknown.
     * @property title of the dialog.
     * @property message the body of the dialog.
     * @property userName default value provide for this session.
     * @property password default value provide for this session.
     * @property method type of authentication,  valid values [Method.HOST] and [Method.PROXY].
     * @property level indicates the level of security of the authentication like [Level.NONE],
     * [Level.SECURED] and [Level.PASSWORD_ENCRYPTED].
     * @property onlyShowPassword indicates if the dialog should only include a password field.
     * @property previousFailed indicates if this request is the result of a previous failed attempt to login.
     * @property isCrossOrigin indicates if this request is from a cross-origin sub-resource.
     * @property onConfirm callback to indicate the user want to start the authentication flow.
     * @property onDismiss callback to indicate the user dismissed this request.
     */
    data class Authentication(
        val uri: String?,
        val title: String,
        val message: String,
        val userName: String,
        val password: String,
        val method: Method,
        val level: Level,
        val onlyShowPassword: Boolean = false,
        val previousFailed: Boolean = false,
        val isCrossOrigin: Boolean = false,
        val onConfirm: (String, String) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible {

        enum class Level {
            NONE, PASSWORD_ENCRYPTED, SECURED
        }

        enum class Method {
            HOST, PROXY
        }
    }

    /**
     * Value type that represents a request for a selecting one or multiple files.
     * @property defaultColor true if the user can select more that one file false otherwise.
     * @property onConfirm callback to notify that the user has selected a color.
     * @property onDismiss callback to notify that the user has canceled the dialog.
     */
    data class Color(
        val defaultColor: String,
        val onConfirm: (String) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for showing a pop-pup prompt.
     * This occurs when content attempts to open a new window,
     * in a way that doesn't appear to be the result of user input.
     *
     * @property targetUri the uri that the page is trying to open.
     * @property onAllow callback to notify that the user wants to open the [targetUri].
     * @property onDeny callback to notify that the user doesn't want to open the [targetUri].
     */
    data class Popup(
        val targetUri: String,
        val onAllow: () -> Unit,
        val onDeny: () -> Unit,
        override val onDismiss: () -> Unit = { onDeny() },
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request to redirect a top-level window.
     * This occurs when a third-party frame attempts redirect the top-level window,
     * in a way that doesn't appear to be the result of user input.
     *
     * @property targetUri the uri that the page is trying to redirect to.
     * @property onAllow callback to notify that the user wants to redirect to [targetUri].
     * @property onDeny callback to notify that the user doesn't want to redirect to [targetUri].
     */
    data class Redirect(
        val targetUri: String,
        val onAllow: () -> Unit,
        val onDeny: () -> Unit,
        override val onDismiss: () -> Unit = { onDeny() },
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for showing a
     * <a href="https://developer.mozilla.org/en-US/docs/Web/API/Window/confirm>confirm prompt</a>.
     *
     * The prompt can have up to three buttons, they could be positive, negative and neutral.
     *
     * @property title of the dialog.
     * @property message the body of the dialog.
     * @property hasShownManyDialogs tells if this page has shown multiple prompts within a short period of time.
     * @property positiveButtonTitle optional title for the positive button.
     * @property negativeButtonTitle optional title for the negative button.
     * @property neutralButtonTitle optional title for the neutral button.
     * @property onConfirmPositiveButton callback to notify that the user has clicked the positive button.
     * @property onConfirmNegativeButton callback to notify that the user has clicked the negative button.
     * @property onConfirmNeutralButton callback to notify that the user has clicked the neutral button.
     * @property onDismiss callback to notify that the user has canceled the dialog.
     */
    data class Confirm(
        val title: String,
        val message: String,
        val hasShownManyDialogs: Boolean = false,
        val positiveButtonTitle: String = "",
        val negativeButtonTitle: String = "",
        val neutralButtonTitle: String = "",
        val onConfirmPositiveButton: (Boolean) -> Unit,
        val onConfirmNegativeButton: (Boolean) -> Unit,
        val onConfirmNeutralButton: (Boolean) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request to share data.
     * https://w3c.github.io/web-share/
     * @property data Share data containing title, text, and url of the request.
     * @property onSuccess Callback to notify that the user hared with another app successfully.
     * @property onFailure Callback to notify that the user attempted to share with another app, but it failed.
     * @property onDismiss Callback to notify that the user aborted the share.
     */
    data class Share(
        val data: ShareData,
        val onSuccess: () -> Unit,
        val onFailure: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a repost prompt.
     *
     * This prompt is shown whenever refreshing or navigating to a page needs resubmitting
     * POST data that has been submitted already.
     *
     * @property onConfirm callback to notify that the user wants to refresh the webpage.
     * @property onDismiss callback to notify that the user wants stay in the current webpage and not refresh it.
     */
    data class Repost(
        val onConfirm: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a folder upload confirm prompt.
     *
     * @property folderName the name of the folder that the user is trying to upload.
     * @property onConfirm callback to notify that the user allows to upload files in folder
     * @property onDismiss callback to notify that the user disallow to upload files.
     */
    data class FolderUploadPrompt(
        val folderName: String,
        val onConfirm: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a WebAuthn related origin request prompt.
     *
     * @property origin the origin of the site making the request.
     * @property rpId the relying party ID for the passkey.
     * @property isCreate true if this is a create request, false if it is a use request.
     * @property onConfirm callback to notify that the user confirmed the request.
     * @property onDismiss callback to notify that the user dismissed the request.
     */
    data class WebAuthnRelatedOriginPrompt(
        val origin: String,
        val rpId: String,
        val isCreate: Boolean,
        val onConfirm: () -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    /**
     * Value type that represents a request for a selecting one folder/directory.
     *
     * @property onSelected callback to notify that the user has selected a folder.
     * @property onDismiss callback to notify that the user has canceled the folder selection.
     */
    data class Folder(
        val onSelected: (Context, Uri) -> Unit,
        override val onDismiss: () -> Unit,
    ) : PromptRequest(), Dismissible

    interface Dismissible {
        val onDismiss: () -> Unit
    }
}

/**
 * Checks if the current [PromptRequest] is a request to pick an image.
 *
 * @return true if the current request is a request for selecting one or more images, false otherwise.
 */
fun PromptRequest?.isPhotoRequest(): Boolean {
    return this is File && mimeTypes.any { it.startsWith("image/") }
}

/**
 * Checks if the current [PromptRequest] is a request to pick a video.
 *
 * @return true if the current request is a request for selecting one or more videos, false otherwise.
 */
fun PromptRequest?.isVideoRequest(): Boolean {
    return this is File && mimeTypes.any { it.startsWith("video/") }
}
