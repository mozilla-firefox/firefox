/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.share

import android.app.Activity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.net.Uri
import android.os.Build
import android.service.chooser.ChooserAction
import androidx.annotation.RequiresApi
import com.google.zxing.WriterException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.share
import mozilla.components.support.ktx.android.content.shareWithChooserActions
import mozilla.components.support.ktx.kotlin.trimmed
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.share.QRCodeGenerator

internal const val SAVE_PDF_ACTION = "org.mozilla.fenix.ACTION_SAVE_TO_PDF"
internal const val PRINT_ACTION = "org.mozilla.fenix.ACTION_PRINT"
internal const val TAB_ID_KEY = "tabID"
internal const val SEND_TO_DEVICES_ACTION = "org.mozilla.fenix.ACTION_SEND_TO_DEVICES"
internal const val QR_CODE_URI_KEY = "qr_code_uri"

/** The chooser actions that can be added to the share intent chooser in the native share sheet. */
enum class ShareSheetChooserAction {
    SAVE_PDF,
    PRINT,
    SEND_TO_DEVICES,
    QR_CODE;

    companion object {
        /** Chooser actions offered when sharing a set of tabs. */
        val tabChooserActions = listOf(SEND_TO_DEVICES, QR_CODE)
    }
}

/**
 * Whether the system share sheet is supported on this device. Returns `true` for API 34 and above.
 *
 * Check this before invoking [ShareSheetLauncher.showSystemShareSheet]. When `false`, fall back to the in-app share
 * fragment for a richer experience than the bare `Intent.ACTION_SEND` available on older Android versions.
 */
val isSystemShareSheetSupported: Boolean
    get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE

/**
 * Delegate interface to abstract away the share implementation, allowing for easier testing and separation of concerns.
 */
interface ShareDelegate {
    /**
     * Basic share function to invoke the native share sheet without any additional chooser actions.
     *
     * @param text The text to share, typically the URL of the page.
     * @param subject The subject of the share, typically the title of the page.
     */
    fun share(text: String, subject: String)

    /**
     * Share function to invoke the native share sheet with additional chooser actions for API 34+.
     *
     * @param text The text to share, typically the URL of the page.
     * @param subject The subject of the share, typically the title of the page.
     * @param actions An array of [ChooserAction] that will be added to the share intent chooser.
     * @param thumbnailUri Optional thumbnail shown in the share sheet preview.
     */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    fun shareWithChooserActions(
        text: String,
        subject: String,
        actions: Array<ChooserAction>,
        thumbnailUri: Uri? = null,
    )
}

private class ContextShareDelegate(private val getContext: () -> Context) : ShareDelegate {
    override fun share(text: String, subject: String) {
        getContext().share(text = text, subject = subject)
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    override fun shareWithChooserActions(
        text: String,
        subject: String,
        actions: Array<ChooserAction>,
        thumbnailUri: Uri?,
    ) {
        getContext()
            .shareWithChooserActions(
                text = text,
                subject = subject,
                actions = actions,
                thumbnailUri = thumbnailUri,
            )
    }
}

/** Interface for handling share events and launching the system share sheet. */
interface ShareSheetLauncher {

    /**
     * Show the system share sheet for sharing resources outside the app.
     *
     * @param id The session id of the tab to share from.
     * @param url The url to share.
     * @param title The title of the page to share.
     * @param text Optional text to share alongside the url. When non-empty it is prepended to the shared text.
     * @param subject Optional subject for the share. When `null` or empty, defaults to the title.
     * @param isPrivate Whether the tab is in private browsing mode.
     * @param isCustomTab Whether the share is being initiated from a custom tab, used to determine the correct
     *   destination to pop up to when navigating to the share fragment.
     * @param chooserActions An array of [ChooserAction] that will be added to the share intent chooser in the native
     *   share sheet.
     */
    fun showSystemShareSheet(
        id: String?,
        url: String,
        title: String?,
        text: String = "",
        subject: String? = null,
        isPrivate: Boolean = false,
        isCustomTab: Boolean = false,
        chooserActions: List<ShareSheetChooserAction> = defaultChooserActions,
    )

    /**
     * Show the system share sheet for sharing multiple items outside the app.
     *
     * @param items The list of [ShareData] items to share.
     * @param isPrivate Whether the tabs are in private browsing mode.
     * @param subject Optional explicit subject for the share. When `null`, defaults to the first item's title.
     * @param text Optional text to share alongside the urls. When non-empty it is prepended to the shared text.
     * @param chooserActions An array of [ChooserAction] that will be added to the share intent chooser in the native
     *   share sheet.
     * @param thumbnailUri Optional thumbnail shown in the share sheet preview.
     */
    fun showSystemShareSheet(
        items: List<ShareData>,
        isPrivate: Boolean = false,
        subject: String? = null,
        text: String? = null,
        chooserActions: List<ShareSheetChooserAction> = listOf(ShareSheetChooserAction.SEND_TO_DEVICES),
        thumbnailUri: Uri? = null,
    )

    companion object {
        val defaultChooserActions =
            listOf(
                ShareSheetChooserAction.SAVE_PDF,
                ShareSheetChooserAction.PRINT,
                ShareSheetChooserAction.SEND_TO_DEVICES,
                ShareSheetChooserAction.QR_CODE,
            )
    }
}

/**
 * Default implementation for launching the system share sheet.
 *
 * @param applicationContext The application [Context] used to build share intents and chooser actions.
 * @param homeActivityClass The [Class] of the activity used to handle send-to-devices and display QR codes.
 * @param qrCodeGenerator [QRCodeGenerator] used to generate QR codes for URLs.
 * @param cacheHelper [CacheHelper] used to store image in cache.
 * @param shareDelegate [ShareDelegate] used to invoke share actions.
 * @param scope [CoroutineScope] used to dispatch QR code generation off the main thread.
 * @param ioDispatcher [CoroutineDispatcher] used for IO-bound QR code generation work.
 * @param crashReporter [CrashReporting] instance used to record caught exceptions.
 */
class DefaultShareSheetLauncher(
    private val applicationContext: Context,
    private val homeActivityClass: Class<out Activity>,
    private val qrCodeGenerator: QRCodeGenerator = QRCodeGenerator(),
    private val cacheHelper: CacheHelper = CacheHelper(),
    private val shareDelegate: ShareDelegate = ContextShareDelegate { applicationContext },
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Main),
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val crashReporter: CrashReporting? = null,
) : ShareSheetLauncher {

    private val logger = Logger("DefaultShareSheetLauncher")

    companion object {
        private const val PRINT_REQUEST_CODE_OFFSET = 1
        private const val SEND_TO_DEVICES_REQUEST_CODE_OFFSET = 2
        private const val QR_CODE_REQUEST_CODE_OFFSET = 3
    }

    override fun showSystemShareSheet(
        id: String?,
        url: String,
        title: String?,
        text: String,
        subject: String?,
        isPrivate: Boolean,
        isCustomTab: Boolean,
        chooserActions: List<ShareSheetChooserAction>,
    ) {
        val displayUrl = url.trimmed()
        val shareText = listOf(text, displayUrl).filter { it.isNotEmpty() }.joinToString("\n")
        val shareSubject = subject?.ifEmpty { null } ?: title ?: ""
        if (id != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            scope.launch {
                val qrCodeAction =
                    withContext(ioDispatcher) {
                        sendQRCodeChooserAction(
                            context = applicationContext,
                            url = displayUrl,
                            id = id,
                        )
                    }

                shareDelegate.shareWithChooserActions(
                    text = shareText,
                    subject = shareSubject,
                    actions =
                        chooserActions
                            .mapNotNull {
                                it.toChooserAction(
                                    applicationContext = applicationContext,
                                    id = id,
                                    url = url,
                                    title = title,
                                    isPrivate = isPrivate,
                                    qrCodeAction = qrCodeAction,
                                )
                            }
                            .toTypedArray(),
                )
            }
        } else {
            shareDelegate.share(text = shareText, subject = shareSubject)
        }
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun ShareSheetChooserAction.toChooserAction(
        applicationContext: Context,
        id: String,
        url: String,
        title: String?,
        isPrivate: Boolean,
        qrCodeAction: ChooserAction?,
    ) =
        when (this) {
            ShareSheetChooserAction.SAVE_PDF -> savePDFChooserAction(applicationContext, id)
            ShareSheetChooserAction.PRINT -> printAction(applicationContext, id)
            ShareSheetChooserAction.SEND_TO_DEVICES ->
                sendToDevicesAction(applicationContext, listOf(url), listOf(title.orEmpty()), isPrivate)
            ShareSheetChooserAction.QR_CODE -> qrCodeAction
        }

    override fun showSystemShareSheet(
        items: List<ShareData>,
        isPrivate: Boolean,
        subject: String?,
        text: String?,
        chooserActions: List<ShareSheetChooserAction>,
        thumbnailUri: Uri?,
    ) {
        val itemsWithUrl = items.filter { it.url != null }
        val urls = itemsWithUrl.map { it.url.orEmpty() }
        val titles = itemsWithUrl.map { it.title.orEmpty() }

        val urlText =
            if (urls.size == 1) {
                urls[0]
            } else {
                urls.mapIndexed { i, url -> "${i + 1}. $url" }.joinToString("\n")
            }
        val shareText = listOfNotNull(text, urlText).filter { it.isNotEmpty() }.joinToString("\n")
        val shareSubject = subject ?: items.firstOrNull()?.title ?: ""

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            scope.launch {
                val qrCodeAction =
                    if (urls.size == 1) {
                        withContext(ioDispatcher) {
                            sendQRCodeChooserAction(
                                context = applicationContext,
                                url = urls[0],
                            )
                        }
                    } else {
                        null
                    }

                shareDelegate.shareWithChooserActions(
                    text = shareText,
                    subject = shareSubject,
                    actions =
                        chooserActions
                            .mapNotNull {
                                it.toChooserAction(
                                    applicationContext = applicationContext,
                                    urls = urls,
                                    titles = titles,
                                    isPrivate = isPrivate,
                                    qrCodeAction = qrCodeAction,
                                )
                            }
                            .toTypedArray(),
                    thumbnailUri = thumbnailUri,
                )
            }
        } else {
            shareDelegate.share(text = shareText, subject = shareSubject)
        }
    }

    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun ShareSheetChooserAction.toChooserAction(
        applicationContext: Context,
        urls: List<String>,
        titles: List<String>,
        isPrivate: Boolean,
        qrCodeAction: ChooserAction?,
    ) =
        when (this) {
            ShareSheetChooserAction.SAVE_PDF -> null
            ShareSheetChooserAction.PRINT -> null
            ShareSheetChooserAction.SEND_TO_DEVICES -> sendToDevicesAction(applicationContext, urls, titles, isPrivate)
            ShareSheetChooserAction.QR_CODE -> qrCodeAction
        }

    /**
     * Create a [ChooserAction] for saving the current page as a PDF.
     *
     * @param context The context used to create intents.
     * @param id The session ID of the tab to save as PDF.
     * @return A [ChooserAction] that can be added to the share intent chooser.
     */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun savePDFChooserAction(context: Context, id: String): ChooserAction {
        val icon = Icon.createWithResource(context, iconsR.drawable.mozac_ic_save_file_24)

        val actionIntent =
            Intent(context, SaveToPdfReceiver::class.java).apply {
                action = SAVE_PDF_ACTION
                putExtra(TAB_ID_KEY, id)
            }

        val pendingIntent =
            PendingIntent.getBroadcast(
                context,
                id.hashCode(),
                actionIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )

        return ChooserAction.Builder(
                icon,
                context.getString(R.string.share_save_to_pdf),
                pendingIntent,
            )
            .build()
    }

    /**
     * Create a [ChooserAction] for sending the given tabs to other devices.
     *
     * @param context The context used to create intents.
     * @param urls The URLs of the tabs to send.
     * @param titles The titles of the tabs to send, aligned by index with [urls].
     * @param isPrivate Whether the tabs are in private browsing mode.
     * @return A [ChooserAction] that can be added to the share intent chooser.
     */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun sendToDevicesAction(
        context: Context,
        urls: List<String>,
        titles: List<String>,
        isPrivate: Boolean,
    ): ChooserAction {
        val icon = Icon.createWithResource(context, iconsR.drawable.mozac_ic_device_desktop_send_24)

        val actionIntent =
            Intent(context, homeActivityClass).apply {
                action = SEND_TO_DEVICES_ACTION
                putStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_URLS, ArrayList(urls))
                putStringArrayListExtra(SendToDevicesDialogFragment.EXTRA_TITLES, ArrayList(titles))
                putExtra(
                    SendToDevicesDialogFragment.EXTRA_PRIVACY,
                    if (isPrivate) {
                        SendToDevicesDialogFragment.PRIVACY_PRIVATE
                    } else {
                        SendToDevicesDialogFragment.PRIVACY_NORMAL
                    },
                )
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }

        val pendingIntent =
            PendingIntent.getActivity(
                context,
                urls.joinToString().hashCode() + SEND_TO_DEVICES_REQUEST_CODE_OFFSET,
                actionIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )

        return ChooserAction.Builder(
                icon,
                context.getString(R.string.share_device_subheader),
                pendingIntent,
            )
            .build()
    }

    /**
     * Create a [ChooserAction] for printing the current page.
     *
     * @param context The context used to create intents.
     * @param id The session ID of the tab to print.
     * @return A [ChooserAction] that can be added to the share intent chooser.
     */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun printAction(context: Context, id: String): ChooserAction {
        val icon = Icon.createWithResource(context, iconsR.drawable.mozac_ic_print_24)

        val actionIntent =
            Intent(context, PrintReceiver::class.java).apply {
                action = PRINT_ACTION
                putExtra(TAB_ID_KEY, id)
            }

        val pendingIntent =
            PendingIntent.getBroadcast(
                context,
                id.hashCode() + PRINT_REQUEST_CODE_OFFSET,
                actionIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )

        return ChooserAction.Builder(
                icon,
                context.getString(R.string.menu_print),
                pendingIntent,
            )
            .build()
    }

    /**
     * Create a [ChooserAction] that generates and displays a QR code for the given URL.
     *
     * @param context The context used to create intents and notifications.
     * @param url The URL to generate a QR code for.
     * @param id The session ID of the tab, used to compute the unique request code.
     * @return A [ChooserAction] that can be added to the share intent chooser or `null` if the URL could not be encoded
     *   into a QR code (e.g. it exceeds the QR code data capacity).
     */
    @RequiresApi(Build.VERSION_CODES.UPSIDE_DOWN_CAKE)
    private fun sendQRCodeChooserAction(
        context: Context,
        url: String,
        id: String? = null,
    ): ChooserAction? {
        val qrCodeBitmap =
            try {
                qrCodeGenerator.generateQRCodeImage(url, 300, 300, context)
            } catch (e: WriterException) {
                val message = "DefaultShareSheetLauncher - Failed to generate QR code for share sheet"
                logger.error(message = message, throwable = e)
                crashReporter?.recordCrashBreadcrumb(Breadcrumb(message = message))
                crashReporter?.submitCaughtException(e)

                return null
            }

        val qrCodeUri = cacheHelper.saveBitmapToCache(context, qrCodeBitmap, url.hashCode().toString())
        val icon = Icon.createWithResource(context, iconsR.drawable.mozac_ic_qr_code_24)

        val displayIntent =
            Intent(context, homeActivityClass).apply {
                putExtra(QR_CODE_URI_KEY, qrCodeUri.toString())
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }

        val pendingIntent =
            PendingIntent.getActivity(
                context,
                if (id != null) {
                    id.hashCode() + QR_CODE_REQUEST_CODE_OFFSET
                } else {
                    url.hashCode() + QR_CODE_REQUEST_CODE_OFFSET
                },
                displayIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
            )
        return ChooserAction.Builder(
                icon,
                context.getString(R.string.share_qr_code),
                pendingIntent,
            )
            .build()
    }
}
