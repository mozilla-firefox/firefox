/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import kotlin.test.assertNotNull
import mozilla.components.concept.fileimporter.FileImporterError
import mozilla.components.lib.password.parser.csv.PasswordsParseError
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.Logins
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class LoginsTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN the user clicks on a login in logins list THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.managementLoginsTapped.testGetValue())
        assertNull(Logins.openIndividualLogin.testGetValue())

        store.dispatch(
            LoginClicked(
                LoginItem(
                    guid = "guid1",
                    url = "url1",
                    username = "u1",
                    password = "p1",
                    timeLastUsed = 0L,
                )
            )
        )

        assertNotNull(Logins.managementLoginsTapped.testGetValue())
        val snapshotLoginTapped = Logins.managementLoginsTapped.testGetValue()!!
        assertEquals(1, snapshotLoginTapped.size)
        assertEquals("management_logins_tapped", snapshotLoginTapped.single().name)

        assertNotNull(Logins.openIndividualLogin.testGetValue())
        val snapshotOpenLogin = Logins.openIndividualLogin.testGetValue()!!
        assertEquals(1, snapshotOpenLogin.size)
        assertEquals("open_individual_login", snapshotOpenLogin.single().name)
    }

    @Test
    fun `WHEN the user clicks on add login in logins list screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.managementAddTapped.testGetValue())

        store.dispatch(AddLoginAction.InitAdd)

        assertNotNull(Logins.managementAddTapped.testGetValue())
        val snapshotAddTapped = Logins.managementAddTapped.testGetValue()!!
        assertEquals(1, snapshotAddTapped.size)
        assertEquals("management_add_tapped", snapshotAddTapped.single().name)
    }

    @Test
    fun `WHEN the user clicks on save new login in add login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.saved.testGetValue())

        store.dispatch(AddLoginAction.AddLoginSaveClicked)

        assertNotNull(Logins.saved.testGetValue())

        val snapshotSaveAddTapped = Logins.saved.testGetValue()!!
        assertTrue(snapshotSaveAddTapped == 1)
    }

    @Test
    fun `WHEN the user clicks on copy username in details login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.copyLogin.testGetValue())

        store.dispatch(DetailLoginAction.CopyUsernameClicked("username"))

        assertNotNull(Logins.copyLogin.testGetValue())

        val snapshotCopyLoginClicked = Logins.copyLogin.testGetValue()!!
        assertEquals(1, snapshotCopyLoginClicked.size)
        assertEquals("copy_login", snapshotCopyLoginClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on copy password in details login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.copyLogin.testGetValue())

        store.dispatch(DetailLoginAction.CopyPasswordClicked("password"))

        assertNotNull(Logins.copyLogin.testGetValue())

        val snapshotCopyLoginClicked = Logins.copyLogin.testGetValue()!!
        assertEquals(1, snapshotCopyLoginClicked.size)
        assertEquals("copy_login", snapshotCopyLoginClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on show password in details login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.viewPasswordLogin.testGetValue())

        store.dispatch(DetailLoginAction.PasswordVisibilityChanged(true))

        assertNotNull(Logins.viewPasswordLogin.testGetValue())

        val snapshotViewPasswordClicked = Logins.viewPasswordLogin.testGetValue()!!
        assertEquals(1, snapshotViewPasswordClicked.size)
        assertEquals("view_password_login", snapshotViewPasswordClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on edit login in details login screen menu THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.openLoginEditor.testGetValue())

        store.dispatch(
            DetailLoginMenuAction.EditLoginMenuItemClicked(
                LoginItem(
                    guid = "guid1",
                    url = "url1",
                    username = "u1",
                    password = "p1",
                    timeLastUsed = 0L,
                )
            )
        )

        assertNotNull(Logins.openLoginEditor.testGetValue())

        val snapshotOpenEditorClicked = Logins.openLoginEditor.testGetValue()!!
        assertEquals(1, snapshotOpenEditorClicked.size)
        assertEquals("open_login_editor", snapshotOpenEditorClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on show password in edit login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.viewPasswordLogin.testGetValue())

        store.dispatch(EditLoginAction.PasswordVisibilityChanged(true))

        assertNotNull(Logins.viewPasswordLogin.testGetValue())

        val snapshotViewPasswordClicked = Logins.viewPasswordLogin.testGetValue()!!
        assertEquals(1, snapshotViewPasswordClicked.size)
        assertEquals("view_password_login", snapshotViewPasswordClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on save login in edit login screen THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.saveEditedLogin.testGetValue())

        store.dispatch(
            EditLoginAction.SaveEditClicked(
                LoginItem(
                    guid = "guid1",
                    url = "url1",
                    username = "u1",
                    password = "p1",
                    timeLastUsed = 0L,
                )
            )
        )

        assertNotNull(Logins.saveEditedLogin.testGetValue())

        val snapshotSaveEditClicked = Logins.saveEditedLogin.testGetValue()!!
        assertEquals(1, snapshotSaveEditClicked.size)
        assertEquals("save_edited_login", snapshotSaveEditClicked.single().name)
    }

    @Test
    fun `WHEN the user clicks on delete login in delete dialog THEN record screen viewed telemetry`() {
        val store = createStore()
        assertNull(Logins.deleteSavedLogin.testGetValue())
        assertNull(Logins.deleted.testGetValue())

        store.dispatch(LoginDeletionDialogAction.DeleteTapped)

        assertNotNull(Logins.deleteSavedLogin.testGetValue())
        val snapshotDeleteLoginTapped = Logins.deleteSavedLogin.testGetValue()!!
        assertEquals(1, snapshotDeleteLoginTapped.size)
        assertEquals("delete_saved_login", snapshotDeleteLoginTapped.single().name)

        assertNotNull(Logins.deleted.testGetValue())
        val snapshotDeletedLogin = Logins.deleted.testGetValue()!!
        assertEquals(1, snapshotDeletedLogin)
    }

    @Test
    fun `WHEN the user clicks import from file THEN record the import menu click telemetry`() {
        val store = createStore()

        store.dispatch(ImportAction.ImportFileClicked)

        assertEquals(1, Logins.importFromFileMenuClick.testGetValue()?.size)
    }

    @Test
    fun `WHEN an import starts THEN record the import started telemetry`() {
        val store = createStore()

        store.dispatch(ImportAction.ImportStarted)

        assertEquals(1, Logins.importStarted.testGetValue()?.size)
    }

    @Test
    fun `WHEN an import succeeds THEN record the import successful telemetry with the logins count`() {
        val store = createStore()

        store.dispatch(ImportAction.ImportSucceeded(count = 5))

        assertEquals("5", Logins.importSuccessful.testGetValue()?.single()?.extra?.get("logins_count"))
    }

    @Test
    fun `WHEN an import is cancelled THEN record the import cancelled telemetry`() {
        val store = createStore()

        store.dispatch(ImportAction.ImportCancelled)

        assertEquals(1, Logins.importCancelled.testGetValue()?.size)
    }

    @Test
    fun `WHEN imports fail with each error type THEN record the matching error codes`() {
        val store = createStore()
        val expectedCodes =
            listOf(
                FileImporterError.ReadError to ImportErrorCode.FILE_READ_ERROR,
                FileImporterError.ParseError(PasswordsParseError.UnsupportedContentType) to
                    ImportErrorCode.PARSE_ERROR_UNSUPPORTED_CONTENT_TYPE,
                FileImporterError.ParseError(PasswordsParseError.FileFormat) to
                    ImportErrorCode.PARSE_ERROR_INVALID_FORMAT,
                FileImporterError.ParseError(PasswordsParseError.ConflictingColumns) to
                    ImportErrorCode.PARSE_ERROR_CONFLICTING_COLUMNS,
                FileImporterError.ParseError(PasswordsParseError.Unexpected) to ImportErrorCode.PARSE_ERROR_UNKNOWN,
                FileImporterError.SaveError to ImportErrorCode.PASSWORDS_SAVE_ERROR,
                FileImporterError.UnknownError to ImportErrorCode.UNKNOWN_ERROR,
            )

        expectedCodes.forEach { (error, _) -> store.dispatch(ImportAction.ImportFailed(error = error)) }

        assertEquals(
            expectedCodes.map { it.second.toString() },
            Logins.importFailed.testGetValue()?.map { it.extra?.get("error_code") },
        )
    }

    private fun createStore(loginsState: LoginsState = LoginsState.default) =
        LoginsStore(
            initialState = loginsState,
            middleware = listOf(LoginsTelemetryMiddleware()),
        )
}
