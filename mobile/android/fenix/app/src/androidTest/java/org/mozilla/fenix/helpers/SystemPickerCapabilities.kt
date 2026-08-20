/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.content.Intent
import android.content.pm.PackageManager
import android.provider.MediaStore
import org.mozilla.fenix.helpers.TestHelper.appContext

/**
 * Answers "which app will service this system intent on THIS device", so a test can assert on the picker the device
 * actually offers instead of one hard-coded from whatever image it was written on.
 *
 * Why this exists: `<input type="file">` makes Gecko show a chooser of camera-capture options plus one content picker,
 * and which content picker that is varies by OS release. On API 37 the row is "Files" (com.google.android.documentsui);
 * on API 34 -- which is what CI and Firebase Test Lab run -- it is "Media" (the photo picker in
 * com.google.android.providers.media.module), and there is no "Files" row at all. A test that hard-codes either label
 * passes on one image and fails on the other.
 *
 * Deliberately resolved from [PackageManager] rather than branched on `Build.VERSION.SDK_INT`. A version check is only
 * a proxy for the capability, and it is wrong on any image that disagrees with the release it was written for; asking
 * which activities resolve the intent is exact and needs no maintenance as releases land.
 *
 * Deliberately never falls back to "skip the assertion". Every entry point here either returns something the caller
 * must then assert on, or fails with the device's actual state in the message. An "if present" helper is how the legacy
 * version of this test became unfailable -- it caught the AssertionFailedError from `intended()` and only logged it, so
 * it could not detect that no picker opened at all.
 */
object SystemPickerCapabilities {

    /** An activity that can service the intent, with the label the chooser will show for it. */
    data class ResolvedApp(val packageName: String, val label: String)

    /** Apps that can service ACTION_GET_CONTENT for [mimeType] -- i.e. candidate content pickers. */
    fun contentPickers(mimeType: String = "*/*"): List<ResolvedApp> =
        resolve(Intent(Intent.ACTION_GET_CONTENT).apply { type = mimeType })

    /**
     * Apps that can service the camera/camcorder capture intents. Gecko adds these to the same chooser, so they have to
     * be subtracted before the remaining row can be called "the content picker".
     */
    fun captureApps(): List<ResolvedApp> =
        resolve(Intent(MediaStore.ACTION_IMAGE_CAPTURE)) + resolve(Intent(MediaStore.ACTION_VIDEO_CAPTURE))

    /**
     * Rows that would be a content picker if present: everything that can service ACTION_GET_CONTENT, minus the
     * camera/camcorder capture rows Gecko adds to the same chooser.
     *
     * Fails rather than returning empty. A device where nothing services ACTION_GET_CONTENT cannot show a file chooser
     * at all, and a test that quietly continued from that would assert nothing.
     */
    fun contentPickerCandidates(mimeType: String = "*/*"): List<ResolvedApp> {
        val pickers = contentPickers(mimeType)
        check(pickers.isNotEmpty()) {
            "No app on this device resolves ACTION_GET_CONTENT for '$mimeType', so no file chooser can appear."
        }
        val captureLabels = captureApps().map { it.label }.toSet()
        val candidates = pickers.filterNot { it.label in captureLabels }.distinctBy { it.packageName }
        check(candidates.isNotEmpty()) {
            "Every ACTION_GET_CONTENT handler on this device is also a capture app, so no content-picker row " +
                "can be identified. handlers=${pickers.map { it.label }} capture=$captureLabels"
        }
        return candidates
    }

    /**
     * Which of [candidates] is actually on screen, given the chooser rows in [visibleLabels].
     *
     * Requires exactly one match: zero means the chooser is not up (or offers nothing pickable), several means this
     * needs a human. Either way the message carries the device's real state so nobody has to reproduce it.
     */
    fun contentPickerRow(visibleLabels: List<String>, candidates: List<ResolvedApp>): ResolvedApp {
        val present = candidates.filter { it.label in visibleLabels }
        check(present.size == 1) {
            "Expected exactly one content-picker row in the chooser, found ${present.size}.\n" +
                "  visible rows : $visibleLabels\n" +
                "  candidates   : ${candidates.map { "${it.label} (${it.packageName})" }}"
        }
        return present.single()
    }

    private fun resolve(intent: Intent): List<ResolvedApp> {
        val pm = appContext.packageManager
        return pm.queryIntentActivities(intent, PackageManager.MATCH_DEFAULT_ONLY).map {
            ResolvedApp(packageName = it.activityInfo.packageName, label = it.loadLabel(pm).toString())
        }
    }
}
