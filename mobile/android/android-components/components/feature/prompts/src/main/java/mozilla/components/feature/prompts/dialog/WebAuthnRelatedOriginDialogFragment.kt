/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.dialog

import android.app.Dialog
import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.FrameLayout
import android.widget.TextView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import mozilla.components.feature.prompts.R
import com.google.android.material.R as materialR

internal class WebAuthnRelatedOriginDialogFragment : PromptDialogFragment() {

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        return BottomSheetDialog(requireContext(), R.style.MozDialogStyle).apply {
            setCancelable(true)
            setOnShowListener {
                val bottomSheet = findViewById<View>(materialR.id.design_bottom_sheet) as FrameLayout
                val behavior = BottomSheetBehavior.from(bottomSheet)
                behavior.state = BottomSheetBehavior.STATE_EXPANDED
                behavior.isDraggable = false
            }
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        return LayoutInflater.from(requireContext()).inflate(
            R.layout.mozac_feature_prompts_webauthn_related_origin,
            container,
            false,
        )
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        view.findViewById<TextView>(R.id.message).text = message

        view.findViewById<Button>(R.id.allow_button).apply {
            text = getString(R.string.webauthn_continue)
            setOnClickListener {
                feature?.onConfirm(sessionId, promptRequestUID, false)
                dismiss()
            }
        }

        view.findViewById<Button>(R.id.deny_button).apply {
            text = getString(R.string.mozac_feature_prompts_cancel)
            setOnClickListener {
                feature?.onCancel(sessionId, promptRequestUID)
                dismiss()
            }
        }
    }

    override fun onCancel(dialog: DialogInterface) {
        super.onCancel(dialog)
        feature?.onCancel(sessionId, promptRequestUID)
    }

    companion object {
        fun newInstance(
            sessionId: String?,
            promptRequestUID: String,
            message: String,
        ): WebAuthnRelatedOriginDialogFragment {
            val fragment = WebAuthnRelatedOriginDialogFragment()
            val arguments = fragment.arguments ?: Bundle()
            with(arguments) {
                putString(KEY_SESSION_ID, sessionId)
                putString(KEY_PROMPT_UID, promptRequestUID)
                putBoolean(KEY_SHOULD_DISMISS_ON_LOAD, false)
                putString(KEY_TITLE, "")
                putString(KEY_MESSAGE, message)
            }
            fragment.arguments = arguments
            return fragment
        }
    }
}
