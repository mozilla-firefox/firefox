/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.share

import android.net.Uri
import android.os.Bundle
import android.view.LayoutInflater
import android.view.ViewGroup
import android.widget.ImageView
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.net.toUri
import androidx.fragment.app.DialogFragment
import androidx.fragment.compose.content
import mozilla.components.compose.base.button.IconButton
import org.mozilla.fenix.R
import org.mozilla.fenix.components.share.QR_CODE_URI_KEY
import org.mozilla.fenix.theme.FirefoxTheme

/** A DialogFragment that displays a QR code for sharing a tab. It provides an option to download the QR code image. */
class QRCodeDialogFragment : DialogFragment() {

    private val downloader = QRCodeDownloader()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setStyle(STYLE_NO_TITLE, R.style.QRCodeDialogStyle)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val qrCodeUri = arguments?.getString(QR_CODE_URI_KEY)?.toUri()
        FirefoxTheme {
            qrCodeUri?.let { uri ->
                Surface {
                    QRCodeDisplayScreen(
                        qrCodeUri = uri,
                        onDownloadClick = {
                            downloader.saveQRCodeToDownloads(
                                qrCodeUri = uri,
                                contentResolver = requireActivity().contentResolver,
                                context = requireContext(),
                            )
                        },
                        onClose = { dismiss() },
                    )
                }
            }
        }
    }

    companion object {
        const val TAG = "QRCodeDialogFragment"

        /**
         * Creates a new instance of [QRCodeDialogFragment] with the provided QR code URI.
         *
         * @param qrCodeUri The URI of the QR code image to be displayed in the dialog.
         * @return A new instance of [QRCodeDialogFragment] with the QR code URI set in the arguments.
         */
        fun newInstance(qrCodeUri: String) =
            QRCodeDialogFragment().apply {
                arguments =
                    Bundle().apply {
                        putString(QR_CODE_URI_KEY, qrCodeUri)
                    }
            }
    }
}

/**
 * Composable function to display the QR code along with instructions and a download option.
 *
 * @param qrCodeUri The URI of the QR code image to be displayed.
 * @param onDownloadClick Callback invoked when the download option is clicked.
 * @param onClose Callback invoked when the close button is clicked.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun QRCodeDisplayScreen(
    qrCodeUri: Uri,
    onDownloadClick: () -> Unit,
    onClose: () -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        TopAppBar(
            title = {
                Text(
                    style = FirefoxTheme.typography.headline5,
                    text = stringResource(R.string.qr_code_display_title),
                    modifier = Modifier.padding(8.dp).weight(1f),
                )
            },
            navigationIcon = {
                IconButton(
                    onClick = { onClose() },
                    contentDescription = stringResource(R.string.qr_code_display_close),
                ) {
                    Icon(
                        imageVector = Icons.Default.Close,
                        contentDescription = null,
                    )
                }
            },
        )
        Column(
            modifier = Modifier.fillMaxSize().padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = stringResource(R.string.qr_code_display_share_nearby),
                style = FirefoxTheme.typography.headline6,
                color = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.padding(bottom = 8.dp, start = 48.dp, end = 48.dp),
            )
            Text(
                text = stringResource(R.string.qr_code_display_instructions),
                style = FirefoxTheme.typography.body2,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(bottom = 32.dp, start = 48.dp, end = 48.dp),
            )
            AndroidView(
                factory = { context ->
                    ImageView(context).apply {
                        setImageURI(qrCodeUri)
                        scaleType = ImageView.ScaleType.FIT_CENTER
                    }
                },
                modifier =
                    Modifier.size(280.dp, 280.dp)
                        .border(
                            1.dp,
                            MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f),
                            MaterialTheme.shapes.large,
                        ),
            )
            Text(
                text = stringResource(R.string.qr_code_display_download),
                color = MaterialTheme.colorScheme.primary,
                style = FirefoxTheme.typography.button,
                modifier = Modifier.clickable { onDownloadClick() }.padding(top = 40.dp, start = 48.dp, end = 48.dp),
            )
        }
    }
}
