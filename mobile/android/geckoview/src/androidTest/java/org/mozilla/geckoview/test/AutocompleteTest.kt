/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import junit.framework.TestCase.assertTrue
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.isEmptyOrNullString
import org.hamcrest.Matchers.not
import org.hamcrest.Matchers.notNullValue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.Autocomplete
import org.mozilla.geckoview.Autocomplete.Address
import org.mozilla.geckoview.Autocomplete.AddressSelectOption
import org.mozilla.geckoview.Autocomplete.CreditCard
import org.mozilla.geckoview.Autocomplete.CreditCardSaveOption
import org.mozilla.geckoview.Autocomplete.CreditCardSelectOption
import org.mozilla.geckoview.Autocomplete.LoginEntry
import org.mozilla.geckoview.Autocomplete.LoginSaveOption
import org.mozilla.geckoview.Autocomplete.LoginSelectOption
import org.mozilla.geckoview.Autocomplete.SelectOption
import org.mozilla.geckoview.Autocomplete.StorageDelegate
import org.mozilla.geckoview.Autocomplete.UsedField
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.PromptDelegate
import org.mozilla.geckoview.GeckoSession.PromptDelegate.AutocompleteRequest
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.AssertCalled

@RunWith(AndroidJUnit4::class)
@MediumTest
class AutocompleteTest : BaseSessionTest() {
    @org.junit.Before
    fun setup() {
        // Ensure programmatic focus is allowed to open the autofill popup
        // for all tests in this class.
        sessionRule.setPrefsUntilTestEnd(mapOf("extensions.formautofill.skipProgrammaticCheckForTests" to true))
    }

    val acceptDelay: Long = 100
    // Never completed; a field so the prompt is not GC-dismissed while the test drives it.
    private val pendingResponse = GeckoResult<PromptDelegate.PromptResponse>(Handler(Looper.getMainLooper()))

    // Controls how long a selection prompt is kept open after its field blurs.
    private val dismissDelayPref = "geckoview.autocomplete.selection_dismiss_delay_ms"

    private fun waitForFill(session: GeckoSession, selector: String) {
        session
            .evaluatePromiseJS(
                "new Promise(resolve => { const check = () => document.querySelector('$selector').value ? resolve() : setTimeout(check, 10); check(); })"
            )
            .value
    }

    // This is a utility to delete previous credit card and address information.
    // Some credit card tests may not use fetched data since pop up is opened
    // before fetching it.
    private fun clearData() {
        mainSession.loadTestPath(ADDRESS_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateDuringNextWait(
            object : StorageDelegate {
                override fun onAddressFetch(): GeckoResult<Array<Address>>? {
                    return null
                }

                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return null
                }
            }
        )

        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(StorageDelegate::class, "onCreditCardFetch")
    }

    @Test
    fun loginBuilderDefaultValue() {
        val login = LoginEntry.Builder().build()

        assertThat(
            "Guid should match",
            login.guid,
            equalTo(null),
        )
        assertThat(
            "Origin should match",
            login.origin,
            equalTo(""),
        )
        assertThat(
            "Form action origin should match",
            login.formActionOrigin,
            equalTo(null),
        )
        assertThat(
            "HTTP realm should match",
            login.httpRealm,
            equalTo(null),
        )
        assertThat(
            "Username should match",
            login.username,
            equalTo(""),
        )
        assertThat(
            "Password should match",
            login.password,
            equalTo(""),
        )
    }

    @Test
    fun fetchLogins() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled(count = 1)
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    return null
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        sessionRule.waitUntilCalled(StorageDelegate::class, "onLoginFetch")
    }

    @Test
    fun fetchCreditCards() {
        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled(count = 1)
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return null
                }
            }
        )

        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(StorageDelegate::class, "onCreditCardFetch")
    }

    @Test
    fun creditCardBuilderDefaultValue() {
        val creditCard = CreditCard.Builder().build()

        assertThat(
            "Guid should match",
            creditCard.guid,
            equalTo(null),
        )
        assertThat(
            "Name should match",
            creditCard.name,
            equalTo(""),
        )
        assertThat(
            "Number should match",
            creditCard.number,
            equalTo(""),
        )
        assertThat(
            "Expiration month should match",
            creditCard.expirationMonth,
            equalTo(""),
        )
        assertThat(
            "Expiration year should match",
            creditCard.expirationYear,
            equalTo(""),
        )
        assertThat(
            "Security code should match",
            creditCard.securityCode,
            equalTo(""),
        )
    }

    @Test
    fun creditCardBuilderSecurityCode() {
        val creditCard = CreditCard.Builder().securityCode("123").build()

        assertThat(
            "Security code should match",
            creditCard.securityCode,
            equalTo("123"),
        )
    }

    @Test
    fun creditCardSelectAndFill() {
        // Workaround to fetch and open prompt
        clearData()

        // Test:
        // 1. Load a credit card form page.
        // 2. Focus on the name input field.
        //    a. Ensure onCreditCardFetch is called.
        //    b. Return the saved entries.
        //    c. Ensure onCreditCardSelect is called.
        //    d. Select and return one of the options.
        //    e. Ensure the form is filled accordingly.

        val name = arrayOf("Peter Parker", "John Doe")
        val number = arrayOf("1234-1234-1234-1234", "2345-2345-2345-2345")
        val guid = arrayOf("test-guid1", "test-guid2")
        val expMonth = arrayOf("04", "08")
        val expYear = arrayOf("22", "23")
        val savedCC =
            arrayOf(
                CreditCard.Builder()
                    .guid(guid[0])
                    .name(name[0])
                    .number(number[0])
                    .expirationMonth(expMonth[0])
                    .expirationYear(expYear[0])
                    .build(),
                CreditCard.Builder()
                    .guid(guid[1])
                    .name(name[1])
                    .number(number[1])
                    .expirationMonth(expMonth[1])
                    .expirationYear(expYear[1])
                    .build(),
            )

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateDuringNextWait(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return GeckoResult.fromValue(savedCC)
                }

                @AssertCalled(false) override fun onCreditCardSave(creditCard: CreditCard) {}
            }
        )

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onCreditCardSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<CreditCardSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be two options",
                        prompt.options.size,
                        equalTo(2),
                    )

                    for (i in 0..1) {
                        val creditCard = prompt.options[i].value

                        assertThat("Credit card should not be null", creditCard, notNullValue())
                        assertThat(
                            "Name should match",
                            creditCard.name,
                            equalTo(name[i]),
                        )
                        assertThat(
                            "Number should match",
                            creditCard.number,
                            equalTo(number[i]),
                        )
                        assertThat(
                            "Expiration month should match",
                            creditCard.expirationMonth,
                            equalTo(expMonth[i]),
                        )
                        assertThat(
                            "Expiration year should match",
                            creditCard.expirationYear,
                            equalTo(expYear[i]),
                        )
                    }

                    return GeckoResult.fromValue(prompt.confirm(prompt.options[0]))
                }
            }
        )

        // Focus on the name input field.
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onCreditCardSelect")
        waitForFill(mainSession, "#name")

        assertThat(
            "Filled name should match",
            mainSession.evaluateJS("document.querySelector('#name').value") as String,
            equalTo(name[0]),
        )
        assertThat(
            "Filled number should match",
            mainSession.evaluateJS("document.querySelector('#number').value") as String,
            equalTo(number[0]),
        )
        assertThat(
            "Filled expiration month should match",
            mainSession.evaluateJS("document.querySelector('#expMonth').value") as String,
            equalTo(expMonth[0]),
        )
        assertThat(
            "Filled expiration year should match",
            mainSession.evaluateJS("document.querySelector('#expYear').value") as String,
            equalTo(expYear[0]),
        )
    }

    @Test
    fun addressBuilderDefaultValue() {
        val address = Address.Builder().build()

        assertThat(
            "Guid should match",
            address.guid,
            equalTo(null),
        )
        assertThat(
            "Name should match",
            address.name,
            equalTo(""),
        )
        assertThat(
            "Given name should match",
            address.givenName,
            equalTo(""),
        )
        assertThat(
            "Family name should match",
            address.familyName,
            equalTo(""),
        )
        assertThat(
            "Street address should match",
            address.streetAddress,
            equalTo(""),
        )
        assertThat(
            "Address level 1 should match",
            address.addressLevel1,
            equalTo(""),
        )
        assertThat(
            "Address level 2 should match",
            address.addressLevel2,
            equalTo(""),
        )
        assertThat(
            "Address level 3 should match",
            address.addressLevel3,
            equalTo(""),
        )
        assertThat(
            "Postal code should match",
            address.postalCode,
            equalTo(""),
        )
        assertThat(
            "Country should match",
            address.country,
            equalTo(""),
        )
        assertThat(
            "Tel should match",
            address.tel,
            equalTo(""),
        )
        assertThat(
            "Email should match",
            address.email,
            equalTo(""),
        )
    }

    @Test
    fun creditCardSelectDismiss() {
        // Workaround to fetch and open prompt
        clearData()

        val name = arrayOf("Peter Parker", "John Doe", "Taro Yamada")
        val number = arrayOf("1234-1234-1234-1234", "2345-2345-2345-2345", "5555-5555-5555-5555")
        val guid = arrayOf("test-guid1", "test-guid2", "test-guid3")
        val expMonth = arrayOf("04", "08", "12")
        val expYear = arrayOf("22", "23", "24")
        val savedCC =
            arrayOf(
                CreditCard.Builder()
                    .guid(guid[0])
                    .name(name[0])
                    .number(number[0])
                    .expirationMonth(expMonth[0])
                    .expirationYear(expYear[0])
                    .build(),
                CreditCard.Builder()
                    .guid(guid[1])
                    .name(name[1])
                    .number(number[1])
                    .expirationMonth(expMonth[1])
                    .expirationYear(expYear[1])
                    .build(),
                CreditCard.Builder()
                    .guid(guid[2])
                    .name(name[2])
                    .number(number[2])
                    .expirationMonth(expMonth[2])
                    .expirationYear(expYear[2])
                    .build(),
            )

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return GeckoResult.fromValue(savedCC)
                }
            }
        )

        val result = GeckoResult<PromptDelegate.PromptResponse>()
        val promptInstanceDelegate =
            object : PromptDelegate.PromptInstanceDelegate {
                override fun onPromptDismiss(prompt: PromptDelegate.BasePrompt) {
                    result.complete(prompt.dismiss())
                }
            }

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled
                override fun onCreditCardSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<CreditCardSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat(
                        "There should be three options",
                        prompt.options.size,
                        equalTo(3),
                    )
                    prompt.setDelegate(promptInstanceDelegate)

                    return pendingResponse
                }
            }
        )

        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onCreditCardSelect")
        mainSession.evaluateJS("document.querySelector('#name').blur()")
        sessionRule.waitForResult(result)
    }

    @Test
    fun creditCardSelectCoalescesAcrossSameTypeFields() {
        // Workaround to fetch and open prompt
        clearData()

        // Keep the coalesce window open long enough that the deferred dismissal
        // can't fire mid-test, so focusing the second field deterministically
        // coalesces into the existing prompt regardless of how slow the
        // autocomplete search is on the device.
        sessionRule.setPrefsUntilTestEnd(mapOf(dismissDelayPref to 60000))

        // Moving focus between two credit card fields should keep the existing
        // selection prompt alive instead of dismissing and reopening it, which
        // would flicker. The delegate should therefore see a single
        // onCreditCardSelect and no dismissal while focus moves between fields.
        val savedCC =
            arrayOf(
                CreditCard.Builder()
                    .guid("test-guid1")
                    .name("Peter Parker")
                    .number("1234-1234-1234-1234")
                    .expirationMonth("04")
                    .expirationYear("22")
                    .build()
            )

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return GeckoResult.fromValue(savedCC)
                }
            }
        )

        var promptDismissed = false
        val dismissed = GeckoResult<PromptDelegate.PromptResponse>()
        val promptInstanceDelegate =
            object : PromptDelegate.PromptInstanceDelegate {
                override fun onPromptDismiss(prompt: PromptDelegate.BasePrompt) {
                    promptDismissed = true
                    dismissed.complete(prompt.dismiss())
                }
            }

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                // Coalescing means focusing the second field must not open a second
                // prompt; a regression that dismisses + reopens calls this twice.
                @AssertCalled(count = 1)
                override fun onCreditCardSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<CreditCardSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    prompt.setDelegate(promptInstanceDelegate)
                    // Leave the prompt open; do not confirm.
                    return pendingResponse
                }
            }
        )

        // Open the prompt on the first credit card field.
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onCreditCardSelect")

        // Move focus to a second credit card field and let its autocomplete
        // cycle run. With coalescing, no second prompt opens and the existing
        // one is not dismissed. The window is large, so the dismissal cannot
        // fire here regardless of timing.
        val settled = GeckoResult<Void>()
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        Handler(Looper.getMainLooper())
            .postDelayed(
                {
                    settled.complete(null)
                },
                acceptDelay * 5,
            )
        sessionRule.waitForResult(settled)

        assertThat(
            "The prompt should stay open while focus moves between same-type fields",
            promptDismissed,
            equalTo(false),
        )

        // Shrink the window so the dismissal is immediate, then confirm focus
        // leaving the field still dismisses the (single) prompt.
        sessionRule.setPrefsUntilTestEnd(mapOf(dismissDelayPref to 0))
        mainSession.evaluateJS("document.querySelector('#number').blur()")
        sessionRule.waitForResult(dismissed)
    }

    @Test
    fun addressSelectCoalescesAcrossSameTypeFields() {
        // Workaround to fetch and open prompt
        clearData()

        // Keep the coalesce window open long enough that the deferred dismissal
        // can't fire mid-test, so focusing the second field deterministically
        // coalesces into the existing prompt regardless of how slow the
        // autocomplete search is on the device.
        sessionRule.setPrefsUntilTestEnd(mapOf(dismissDelayPref to 60000))

        // As with credit cards, moving focus between two address fields should
        // keep the existing prompt rather than flickering.
        val savedAddress =
            Address.Builder()
                .guid("test-guid")
                .name("Peter Parker")
                .givenName("Peter")
                .familyName("Parker")
                .streetAddress("20 Ingram Street, Forest Hills Gardens, Queens")
                .postalCode("11375")
                .country("US")
                .email("spiderman@newyork.com")
                .tel("+1 180090021")
                .organization("")
                .build()

        mainSession.loadTestPath(ADDRESS_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onAddressFetch(): GeckoResult<Array<Address>>? {
                    return GeckoResult.fromValue(arrayOf(savedAddress))
                }
            }
        )

        var promptDismissed = false
        val dismissed = GeckoResult<PromptDelegate.PromptResponse>()
        val promptInstanceDelegate =
            object : PromptDelegate.PromptInstanceDelegate {
                override fun onPromptDismiss(prompt: PromptDelegate.BasePrompt) {
                    promptDismissed = true
                    dismissed.complete(prompt.dismiss())
                }
            }

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                // Coalescing means focusing the second field must not open a second
                // prompt; a regression that dismisses + reopens calls this twice.
                @AssertCalled(count = 1)
                override fun onAddressSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<AddressSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    prompt.setDelegate(promptInstanceDelegate)
                    // Leave the prompt open; do not confirm.
                    return pendingResponse
                }
            }
        )

        // Open the prompt on the first address field.
        mainSession.evaluateJS("document.querySelector('#givenName').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onAddressSelect")

        // Move focus to a second address field and let its autocomplete cycle
        // run. With coalescing, no second prompt opens and the existing one is
        // not dismissed. The window is large, so the dismissal cannot fire here
        // regardless of timing.
        val settled = GeckoResult<Void>()
        mainSession.evaluateJS("document.querySelector('#streetAddress').focus()")
        Handler(Looper.getMainLooper())
            .postDelayed(
                {
                    settled.complete(null)
                },
                acceptDelay * 5,
            )
        sessionRule.waitForResult(settled)

        assertThat(
            "The prompt should stay open while focus moves between same-type fields",
            promptDismissed,
            equalTo(false),
        )

        // Shrink the window so the dismissal is immediate, then confirm focus
        // leaving the field still dismisses the (single) prompt.
        sessionRule.setPrefsUntilTestEnd(mapOf(dismissDelayPref to 0))
        mainSession.evaluateJS("document.querySelector('#streetAddress').blur()")
        sessionRule.waitForResult(dismissed)
    }

    @Test
    fun fetchAddresses() {
        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled(count = 1)
                override fun onAddressFetch(): GeckoResult<Array<Address>>? {
                    return null
                }
            }
        )

        mainSession.loadTestPath(ADDRESS_FORM_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        sessionRule.waitUntilCalled(StorageDelegate::class, "onAddressFetch")
    }

    fun checkAddressesForCorrectness(savedAddresses: Array<Address>, selectedAddress: Address) {
        // Test:
        // 1. Load an address form page.
        // 2. Focus on the given name input field.
        //    a. Ensure onAddressFetch is called.
        //    b. Return the saved entries.
        //    c. Ensure onAddressSelect is called.
        //    d. Select and return one of the options.
        //    e. Ensure the form is filled accordingly.

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onAddressFetch(): GeckoResult<Array<Address>>? {
                    return GeckoResult.fromValue(savedAddresses)
                }

                @AssertCalled(false) override fun onAddressSave(address: Address) {}
            }
        )

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onAddressSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<AddressSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be one option",
                        prompt.options.size,
                        equalTo(savedAddresses.size),
                    )

                    val addressOption = prompt.options.find { it.value.guid == selectedAddress.guid }
                    val address = addressOption?.value

                    assertThat("Address should not be null", address, notNullValue())
                    assertThat(
                        "Guid should match",
                        address?.guid,
                        equalTo(selectedAddress.guid),
                    )
                    assertThat(
                        "Name should match",
                        address?.name,
                        equalTo(selectedAddress.name),
                    )
                    assertThat(
                        "Given name should match",
                        address?.givenName,
                        equalTo(selectedAddress.givenName),
                    )
                    assertThat(
                        "Family name should match",
                        address?.familyName,
                        equalTo(selectedAddress.familyName),
                    )
                    assertThat(
                        "Street address should match",
                        address?.streetAddress,
                        equalTo(selectedAddress.streetAddress),
                    )
                    assertThat(
                        "Address level 1 should match",
                        address?.addressLevel1,
                        equalTo(selectedAddress.addressLevel1),
                    )
                    assertThat(
                        "Address level 2 should match",
                        address?.addressLevel2,
                        equalTo(selectedAddress.addressLevel2),
                    )
                    assertThat(
                        "Address level 3 should match",
                        address?.addressLevel3,
                        equalTo(selectedAddress.addressLevel3),
                    )
                    assertThat(
                        "Postal code should match",
                        address?.postalCode,
                        equalTo(selectedAddress.postalCode),
                    )
                    assertThat(
                        "Country should match",
                        address?.country,
                        equalTo(selectedAddress.country),
                    )
                    assertThat(
                        "Tel should match",
                        address?.tel,
                        equalTo(selectedAddress.tel),
                    )
                    assertThat(
                        "Email should match",
                        address?.email,
                        equalTo(selectedAddress.email),
                    )

                    return GeckoResult.fromValue(prompt.confirm(addressOption!!))
                }
            }
        )

        mainSession.loadTestPath(ADDRESS_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        // Focus on the given name input field.
        mainSession.evaluateJS("document.querySelector('#givenName').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onAddressSelect")
        waitForFill(mainSession, "#streetAddress")

        assertThat(
            "Filled given name should match",
            mainSession.evaluateJS("document.querySelector('#givenName').value") as String,
            equalTo(selectedAddress.givenName),
        )
        assertThat(
            "Filled family name should match",
            mainSession.evaluateJS("document.querySelector('#familyName').value") as String,
            equalTo(selectedAddress.familyName),
        )
        assertThat(
            "Filled street address should match",
            mainSession.evaluateJS("document.querySelector('#streetAddress').value") as String,
            equalTo(selectedAddress.streetAddress),
        )
        assertThat(
            "Filled country should match",
            mainSession.evaluateJS("document.querySelector('#country').value") as String,
            equalTo(selectedAddress.country),
        )
        assertThat(
            "Filled postal code should match",
            mainSession.evaluateJS("document.querySelector('#postalCode').value") as String,
            equalTo(selectedAddress.postalCode),
        )
        assertThat(
            "Filled email should match",
            mainSession.evaluateJS("document.querySelector('#email').value") as String,
            equalTo(selectedAddress.email),
        )
        assertThat(
            "Filled telephone number should match",
            mainSession.evaluateJS("document.querySelector('#tel').value") as String,
            equalTo(selectedAddress.tel),
        )
        assertThat(
            "Filled organization should match",
            mainSession.evaluateJS("document.querySelector('#organization').value") as String,
            equalTo(selectedAddress.organization),
        )
    }

    @Test
    fun addressSelectAndFill() {
        val name = "Peter Parker"
        val givenName = "Peter"
        val familyName = "Parker"
        val streetAddress = "20 Ingram Street, Forest Hills Gardens, Queens"
        val postalCode = "11375"
        val country = "US"
        val email = "spiderman@newyork.com"
        val tel = "+1 180090021"
        val organization = ""
        val guid = "test-guid"
        val savedAddress =
            Address.Builder()
                .guid(guid)
                .name(name)
                .givenName(givenName)
                .familyName(familyName)
                .streetAddress(streetAddress)
                .postalCode(postalCode)
                .country(country)
                .email(email)
                .tel(tel)
                .organization(organization)
                .build()
        val savedAddresses = mutableListOf<Address>(savedAddress)

        checkAddressesForCorrectness(savedAddresses.toTypedArray(), savedAddress)
    }

    @Test
    fun addressSelectAndFillWithoutGivenName() {
        val name = "Peter Parker"
        val streetAddress = "20 Ingram Street, Forest Hills Gardens, Queens"
        val postalCode = "11375"
        val country = "US"
        val email = "spiderman@newyork.com"
        val tel = "+1 180090021"
        val organization = ""
        val guid = "test-guid"
        val builder =
            Address.Builder()
                .guid(guid)
                .name(name)
                .streetAddress(streetAddress)
                .postalCode(postalCode)
                .country(country)
                .email(email)
                .tel(tel)
                .organization(organization)

        val savedAddress = builder.build()

        val expectedAddress = builder.givenName("Peter").familyName("Parker").build()

        val savedAddresses = arrayOf(savedAddress)

        checkAddressesForCorrectness(savedAddresses, expectedAddress)
    }

    @Test
    fun addressSelectAndFillMultipleAddresses() {
        val names = arrayOf("Peter Parker", "Wade Wilson")
        val givenNames = arrayOf("Peter", "Wade")
        val familyNames = arrayOf("Parker", "Wilson")
        val streetAddresses = arrayOf("20 Ingram Street, Forest Hills Gardens, Queens", "890 Fifth Avenue, Manhattan")
        val postalCodes = arrayOf("11375", "10110")
        val countries = arrayOf("US", "US")
        val emails = arrayOf("spiderman@newyork.com", "deadpool@newyork.com")
        val tels = arrayOf("+1 180090021", "+1 180055555")
        val organizations = arrayOf("", "")
        val guids = arrayOf("test-guid-1", "test-guid-2")
        val selectedAddress =
            Address.Builder()
                .guid(guids[1])
                .name(names[1])
                .givenName(givenNames[1])
                .familyName(familyNames[1])
                .streetAddress(streetAddresses[1])
                .postalCode(postalCodes[1])
                .country(countries[1])
                .email(emails[1])
                .tel(tels[1])
                .organization(organizations[1])
                .build()
        val savedAddresses =
            mutableListOf<Address>(
                Address.Builder()
                    .guid(guids[0])
                    .name(names[0])
                    .givenName(givenNames[0])
                    .familyName(familyNames[0])
                    .streetAddress(streetAddresses[0])
                    .postalCode(postalCodes[0])
                    .country(countries[0])
                    .email(emails[0])
                    .tel(tels[0])
                    .organization(organizations[0])
                    .build(),
                selectedAddress,
            )

        checkAddressesForCorrectness(savedAddresses.toTypedArray(), selectedAddress)
    }

    @Test
    fun addressSelectDismiss() {
        val name = "Peter Parker"
        val givenName = "Peter"
        val familyName = "Parker"
        val streetAddress = "20 Ingram Street, Forest Hills Gardens, Queens"
        val postalCode = "11375"
        val country = "US"
        val email = "spiderman@newyork.com"
        val tel = "+1 180090021"
        val organization = ""
        val guid = "test-guid"
        val savedAddress =
            Address.Builder()
                .guid(guid)
                .name(name)
                .givenName(givenName)
                .familyName(familyName)
                .streetAddress(streetAddress)
                .postalCode(postalCode)
                .country(country)
                .email(email)
                .tel(tel)
                .organization(organization)
                .build()
        val savedAddresses = mutableListOf<Address>(savedAddress)

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onAddressFetch(): GeckoResult<Array<Address>>? {
                    return GeckoResult.fromValue(savedAddresses.toTypedArray())
                }
            }
        )

        val result = GeckoResult<PromptDelegate.PromptResponse>()
        val promptInstanceDelegate =
            object : PromptDelegate.PromptInstanceDelegate {
                override fun onPromptDismiss(prompt: PromptDelegate.BasePrompt) {
                    result.complete(prompt.dismiss())
                }
            }

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled
                override fun onAddressSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<AddressSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat(
                        "There should be one option",
                        prompt.options.size,
                        equalTo(1),
                    )
                    prompt.setDelegate(promptInstanceDelegate)

                    return pendingResponse
                }
            }
        )

        mainSession.loadTestPath(ADDRESS_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("document.querySelector('#givenName').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onAddressSelect")
        mainSession.evaluateJS("document.querySelector('#givenName').blur()")
        sessionRule.waitForResult(result)
    }

    @Test
    fun loginSaveDismiss() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        sessionRule.delegateDuringNextWait(
            object : StorageDelegate {
                @AssertCalled(count = 1)
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    return null
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled(count = 0) override fun onLoginSave(login: LoginEntry) {}
            }
        )

        // Assign login credentials.
        mainSession.evaluateJS("document.querySelector('#user1').value = 'user1x'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = 'pass1x'")

        // Submit the form.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")

        sessionRule.waitUntilCalled(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Session should not be null", session, notNullValue())
                    assertThat("Login should not be null", login, notNullValue())
                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo("user1x"),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo("pass1x"),
                    )

                    return GeckoResult.fromValue(prompt.dismiss())
                }
            }
        )
    }

    @Test
    fun loginSaveAccept() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginSave(login: LoginEntry) {
                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo("user1x"),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo("pass1x"),
                    )
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo("user1x"),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo("pass1x"),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign login credentials.
        mainSession.evaluateJS("document.querySelector('#user1').value = 'user1x'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = 'pass1x'")

        // Submit the form.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")

        sessionRule.waitUntilCalled(StorageDelegate::class, "onLoginSave")
    }

    @Test
    fun loginSaveModifyAccept() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginSave(login: LoginEntry) {
                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo("user1x"),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo("pass1xmod"),
                    )
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo("user1x"),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo("pass1x"),
                    )

                    val modLogin =
                        LoginEntry.Builder()
                            .origin(login.origin)
                            .formActionOrigin(login.origin)
                            .httpRealm(login.httpRealm)
                            .username(login.username)
                            .password("pass1xmod")
                            .build()

                    return GeckoResult.fromValue(prompt.confirm(LoginSaveOption(modLogin)))
                }
            }
        )

        // Assign login credentials.
        mainSession.evaluateJS("document.querySelector('#user1').value = 'user1x'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = 'pass1x'")

        // Submit the form.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")

        sessionRule.waitUntilCalled(StorageDelegate::class, "onLoginSave")
    }

    @Test
    fun loginUpdateAccept() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        val saveHandled = GeckoResult<Void>()
        val saveHandled2 = GeckoResult<Void>()

        val user1 = "user1x"
        val pass1 = "pass1x"
        val pass2 = "pass1up"
        val guid = "test-guid"
        val savedLogins = mutableListOf<LoginEntry>()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    return GeckoResult.fromValue(savedLogins.toTypedArray())
                }

                @AssertCalled(count = 2)
                override fun onLoginSave(login: LoginEntry) {
                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(forEachCall(pass1, pass2)),
                    )

                    assertThat(
                        "GUID should match",
                        login.guid,
                        equalTo(forEachCall(null, guid)),
                    )

                    val savedLogin =
                        LoginEntry.Builder()
                            .guid(guid)
                            .origin(login.origin)
                            .formActionOrigin(login.formActionOrigin)
                            .username(login.username)
                            .password(login.password)
                            .build()

                    savedLogins.add(savedLogin)

                    if (sessionRule.currentCall.counter == 1) {
                        saveHandled.complete(null)
                    } else if (sessionRule.currentCall.counter == 2) {
                        saveHandled2.complete(null)
                    }
                }
            }
        )

        sessionRule.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 2)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(forEachCall(pass1, pass2)),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign login credentials.
        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("document.querySelector('#user1').value = '$user1'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = '$pass1'")
        mainSession.evaluateJS("document.querySelector('#form1').submit()")

        sessionRule.waitForResult(saveHandled)

        // Update login credentials.
        val session2 = sessionRule.createOpenSession()
        session2.loadTestPath(FORMS3_HTML_PATH)
        session2.waitForPageStop()
        waitForFill(session2, "#user1")
        session2.evaluateJS("document.querySelector('#pass1').value = '$pass2'")
        session2.evaluateJS("document.querySelector('#form1').submit()")

        sessionRule.waitForResult(saveHandled2)
    }

    @Test
    fun creditCardSaveAccept() {
        val ccName = "MyCard"
        val ccNumber = "5105105105105100"
        val ccExpMonth = "6"
        val ccExpYear = "2024"

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardSave(creditCard: CreditCard) {
                    assertThat("Credit card name should match", creditCard.name, equalTo(ccName))
                    assertThat("Credit card number should match", creditCard.number, equalTo(ccNumber))
                    assertThat(
                        "Credit card expiration month should match",
                        creditCard.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        creditCard.expirationYear,
                        equalTo(ccExpYear),
                    )
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = request.options[0]
                    val cc = option.value

                    assertThat("Credit card should not be null", cc, notNullValue())

                    assertThat(
                        "Credit card name should match",
                        cc.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        cc.number,
                        equalTo(ccNumber),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        cc.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        cc.expirationYear,
                        equalTo(ccExpYear),
                    )

                    return GeckoResult.fromValue(request.confirm(option))
                }
            }
        )

        // Enter the card values
        mainSession.evaluateJS("document.querySelector('#name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#number').value = '$ccNumber'")
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        mainSession.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth'")
        mainSession.evaluateJS("document.querySelector('#expMonth').focus()")
        mainSession.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear'")
        mainSession.evaluateJS("document.querySelector('#expYear').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('form').requestSubmit()")

        sessionRule.waitUntilCalled(StorageDelegate::class, "onCreditCardSave")
    }

    @Test
    fun creditCardSaveAcceptForm2() {
        // TODO Bug 1764709: Right now we fill normalized credit card data to match
        // the expected result.
        val ccName = "MyCard"
        val ccNumber = "5105105105105100"
        val ccExpMonth = "6"
        val ccExpYear = "2024"

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardSave(creditCard: CreditCard) {
                    assertThat("Credit card name should match", creditCard.name, equalTo(ccName))
                    assertThat("Credit card number should match", creditCard.number, equalTo(ccNumber))
                    assertThat(
                        "Credit card expiration month should match",
                        creditCard.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        creditCard.expirationYear,
                        equalTo(ccExpYear),
                    )
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = request.options[0]
                    val cc = option.value

                    assertThat("Credit card should not be null", cc, notNullValue())

                    assertThat(
                        "Credit card name should match",
                        cc.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        cc.number,
                        equalTo(ccNumber),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        cc.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        cc.expirationYear,
                        equalTo(ccExpYear),
                    )

                    return GeckoResult.fromValue(request.confirm(option))
                }
            }
        )

        // Enter the card values
        mainSession.evaluateJS("document.querySelector('#form2 #name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#form2 #name').focus()")
        mainSession.evaluateJS("document.querySelector('#form2 #number').value = '$ccNumber'")
        mainSession.evaluateJS("document.querySelector('#form2 #number').focus()")
        mainSession.evaluateJS("document.querySelector('#form2 #exp').value = '$ccExpMonth/$ccExpYear'")
        mainSession.evaluateJS("document.querySelector('#form2 #exp').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('#form2').requestSubmit()")

        sessionRule.waitUntilCalled(StorageDelegate::class, "onCreditCardSave")
    }

    @Test
    fun creditCardSaveDismiss() {
        val ccName = "MyCard"
        val ccNumber = "5105105105105100"
        val ccExpMonth = "6"
        val ccExpYear = "2024"

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateDuringNextWait(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>>? {
                    return null
                }
            }
        )

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled(count = 0) override fun onCreditCardSave(creditCard: CreditCard) {}
            }
        )

        // Enter the card values
        mainSession.evaluateJS("document.querySelector('#name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#number').value = '$ccNumber'")
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        mainSession.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth'")
        mainSession.evaluateJS("document.querySelector('#expMonth').focus()")
        mainSession.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear'")
        mainSession.evaluateJS("document.querySelector('#expYear').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('form').requestSubmit()")

        sessionRule.waitUntilCalled(
            object : PromptDelegate {
                @AssertCalled
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = request.options[0]
                    val cc = option.value

                    assertThat("Credit card should not be null", cc, notNullValue())

                    assertThat(
                        "Credit card name should match",
                        cc.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        cc.number,
                        equalTo(ccNumber),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        cc.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        cc.expirationYear,
                        equalTo(ccExpYear),
                    )

                    return GeckoResult.fromValue(request.dismiss())
                }
            }
        )
    }

    @Test
    fun creditCardSaveModifyAccept() {
        val ccName = "MyCard"
        val ccNumber = "5105105105105100"
        val ccExpMonth = "6"
        val ccExpYearNew = "2026"
        val ccExpYear = "2024"

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardSave(creditCard: CreditCard) {
                    assertThat("Credit card name should match", creditCard.name, equalTo(ccName))
                    assertThat("Credit card number should match", creditCard.number, equalTo(ccNumber))
                    assertThat(
                        "Credit card expiration month should match",
                        creditCard.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        creditCard.expirationYear,
                        equalTo(ccExpYearNew),
                    )
                }
            }
        )

        sessionRule.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = request.options[0]
                    val cc = option.value

                    assertThat("Credit card should not be null", cc, notNullValue())

                    assertThat(
                        "Credit card name should match",
                        cc.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        cc.number,
                        equalTo(ccNumber),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        cc.expirationMonth,
                        equalTo(ccExpMonth),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        cc.expirationYear,
                        equalTo(ccExpYear),
                    )

                    val modifiedCreditCard =
                        CreditCard.Builder()
                            .name(cc.name)
                            .number(cc.number)
                            .expirationMonth(cc.expirationMonth)
                            .expirationYear(ccExpYearNew)
                            .build()

                    return GeckoResult.fromValue(request.confirm(CreditCardSaveOption(modifiedCreditCard)))
                }
            }
        )

        // Enter the card values
        mainSession.evaluateJS("document.querySelector('#name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#number').value = '$ccNumber'")
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        mainSession.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth'")
        mainSession.evaluateJS("document.querySelector('#expMonth').focus()")
        mainSession.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear'")
        mainSession.evaluateJS("document.querySelector('#expYear').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('form').requestSubmit()")

        sessionRule.waitUntilCalled(StorageDelegate::class, "onCreditCardSave")
    }

    @Test
    fun formSubmissionWithUnchangedCreditCardShouldNotTriggerSave() {
        val ccName = "Jane Doe"
        val ccNumber = "5555444433331111"
        val ccExpMonth = "6"
        val ccExpYear = "2024"
        val savedCreditCard =
            CreditCard.Builder()
                .guid("test-guid-1")
                .name(ccName)
                .number(ccNumber)
                .expirationMonth(ccExpMonth)
                .expirationYear(ccExpYear)
                .build()

        val savedCreditCards = arrayOf(savedCreditCard)

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        // Setup delegates for fetching data and handling prompts.
        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate, PromptDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>> {
                    return GeckoResult.fromValue(savedCreditCards)
                }

                // These should NOT be called because no information has changed.
                @AssertCalled(count = 0) override fun onCreditCardSave(creditCard: CreditCard) = Unit

                @AssertCalled(count = 0)
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    // This block should not be reached. If it is, the test will fail.
                    return GeckoResult.fromValue(request.dismiss())
                }
            }
        )

        // Fill in the fields with the same saved data
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#number').value = '$ccNumber'")
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        mainSession.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth'")
        mainSession.evaluateJS("document.querySelector('#expMonth').focus()")
        mainSession.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear'")
        mainSession.evaluateJS("document.querySelector('#expYear').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('form').requestSubmit()")

        // Wait for the form to submit
        mainSession.waitForRoundTrip()
    }

    @Test
    fun creditCardUpdateAccept() {
        val ccName = "MyCard"
        val ccNumber1 = "5105105105105100"
        val ccExpMonth1 = "6"
        val ccExpYear1 = "2024"
        val ccNumber2 = "4111111111111111"
        val ccExpMonth2 = "11"
        val ccExpYear2 = "2021"
        val savedCreditCards = mutableListOf<CreditCard>()

        mainSession.loadTestPath(CC_FORM_HTML_PATH)
        mainSession.waitForPageStop()

        val saveHandled1 = GeckoResult<Void>()
        val saveHandled2 = GeckoResult<Void>()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onCreditCardFetch(): GeckoResult<Array<CreditCard>> {
                    return GeckoResult.fromValue(savedCreditCards.toTypedArray())
                }

                @AssertCalled(count = 2)
                override fun onCreditCardSave(creditCard: CreditCard) {
                    assertThat(
                        "Credit card name should match",
                        creditCard.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        creditCard.number,
                        equalTo(forEachCall(ccNumber1, ccNumber2)),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        creditCard.expirationMonth,
                        equalTo(forEachCall(ccExpMonth1, ccExpMonth2)),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        creditCard.expirationYear,
                        equalTo(forEachCall(ccExpYear1, ccExpYear2)),
                    )

                    val savedCC =
                        CreditCard.Builder()
                            .guid("test1")
                            .name(creditCard.name)
                            .number(creditCard.number)
                            .expirationMonth(creditCard.expirationMonth)
                            .expirationYear(creditCard.expirationYear)
                            .build()
                    savedCreditCards.add(savedCC)

                    if (sessionRule.currentCall.counter == 1) {
                        saveHandled1.complete(null)
                    } else if (sessionRule.currentCall.counter == 2) {
                        saveHandled2.complete(null)
                    }
                }
            }
        )

        sessionRule.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 2)
                override fun onCreditCardSave(
                    session: GeckoSession,
                    request: AutocompleteRequest<CreditCardSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse> {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = request.options[0]
                    val cc = option.value

                    assertThat("Credit card should not be null", cc, notNullValue())

                    assertThat(
                        "Credit card name should match",
                        cc.name,
                        equalTo(ccName),
                    )
                    assertThat(
                        "Credit card number should match",
                        cc.number,
                        equalTo(forEachCall(ccNumber1, ccNumber2)),
                    )
                    assertThat(
                        "Credit card expiration month should match",
                        cc.expirationMonth,
                        equalTo(forEachCall(ccExpMonth1, ccExpMonth2)),
                    )
                    assertThat(
                        "Credit card expiration year should match",
                        cc.expirationYear,
                        equalTo(forEachCall(ccExpYear1, ccExpYear2)),
                    )

                    return GeckoResult.fromValue(request.confirm(option))
                }
            }
        )

        // Enter the card values
        mainSession.evaluateJS("document.querySelector('#name').value = '$ccName'")
        mainSession.evaluateJS("document.querySelector('#name').focus()")
        mainSession.evaluateJS("document.querySelector('#number').value = '$ccNumber1'")
        mainSession.evaluateJS("document.querySelector('#number').focus()")
        mainSession.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth1'")
        mainSession.evaluateJS("document.querySelector('#expMonth').focus()")
        mainSession.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear1'")
        mainSession.evaluateJS("document.querySelector('#expYear').focus()")

        // Submit the form
        mainSession.evaluateJS("document.querySelector('form').requestSubmit()")

        sessionRule.waitForResult(saveHandled1)

        // Update credit card
        val session2 = sessionRule.createOpenSession()
        session2.loadTestPath(CC_FORM_HTML_PATH)
        session2.waitForPageStop()
        session2.evaluateJS("document.querySelector('#name').value = '$ccName'")
        session2.evaluateJS("document.querySelector('#name').focus()")
        session2.evaluateJS("document.querySelector('#number').value = '$ccNumber2'")
        session2.evaluateJS("document.querySelector('#number').focus()")
        session2.evaluateJS("document.querySelector('#expMonth').value = '$ccExpMonth2'")
        session2.evaluateJS("document.querySelector('#expMonth').focus()")
        session2.evaluateJS("document.querySelector('#expYear').value = '$ccExpYear2'")
        session2.evaluateJS("document.querySelector('#expYear').focus()")

        session2.evaluateJS("document.querySelector('form').requestSubmit()")

        sessionRule.waitForResult(saveHandled2)
    }

    fun testLoginUsed(autofillEnabled: Boolean) {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        val user1 = "user1x"
        val pass1 = "pass1x"
        val guid = "test-guid"
        val origin = GeckoSessionTestRule.TEST_ENDPOINT
        val savedLogin =
            LoginEntry.Builder()
                .guid(guid)
                .origin(origin)
                .formActionOrigin(origin)
                .username(user1)
                .password(pass1)
                .build()
        val savedLogins = mutableListOf<LoginEntry>(savedLogin)

        if (autofillEnabled) {
            sessionRule.delegateUntilTestEnd(
                object : StorageDelegate {
                    @AssertCalled
                    override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                        assertThat("Domain should match", domain, equalTo("localhost"))

                        return GeckoResult.fromValue(savedLogins.toTypedArray())
                    }

                    @AssertCalled(count = 1)
                    override fun onLoginUsed(login: LoginEntry, usedFields: Int) {
                        assertThat(
                            "Used fields should match",
                            usedFields,
                            equalTo(UsedField.PASSWORD),
                        )

                        assertThat(
                            "Username should match",
                            login.username,
                            equalTo(user1),
                        )

                        assertThat(
                            "Password should match",
                            login.password,
                            equalTo(pass1),
                        )

                        assertThat(
                            "GUID should match",
                            login.guid,
                            equalTo(guid),
                        )
                    }
                }
            )
        } else {
            sessionRule.delegateUntilTestEnd(
                object : StorageDelegate {
                    @AssertCalled
                    override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                        assertThat("Domain should match", domain, equalTo("localhost"))

                        return GeckoResult.fromValue(savedLogins.toTypedArray())
                    }

                    @AssertCalled(false) override fun onLoginUsed(login: LoginEntry, usedFields: Int) {}
                }
            )
        }

        sessionRule.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(false)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    return null
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()
        if (autofillEnabled) {
            waitForFill(mainSession, "#pass1")
        }
        mainSession.evaluateJS("document.querySelector('#form1').submit()")

        if (autofillEnabled) {
            sessionRule.waitUntilCalled(StorageDelegate::class, "onLoginUsed")
        } else {
            mainSession.waitForPageStop()
        }
    }

    @Test
    fun loginUsed() {
        testLoginUsed(true)
    }

    @Test
    fun loginAutofillDisabled() {
        sessionRule.runtime.settings.loginAutofillEnabled = false
        testLoginUsed(false)
        sessionRule.runtime.settings.loginAutofillEnabled = true
    }

    fun testPasswordAutofill(autofillEnabled: Boolean) {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        val user1 = "user1x"
        val pass1 = "pass1x"
        val guid = "test-guid"
        val origin = GeckoSessionTestRule.TEST_ENDPOINT
        val savedLogin =
            LoginEntry.Builder()
                .guid(guid)
                .origin(origin)
                .formActionOrigin(origin)
                .username(user1)
                .password(pass1)
                .build()
        val savedLogins = mutableListOf<LoginEntry>(savedLogin)

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    return GeckoResult.fromValue(savedLogins.toTypedArray())
                }

                @AssertCalled(false) override fun onLoginUsed(login: LoginEntry, usedFields: Int) {}
            }
        )

        sessionRule.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(false)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    return null
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()
        mainSession.evaluateJS("document.querySelector('#user1').focus()")
        mainSession.evaluateJS("document.querySelector('#user1').value = '$user1'")
        mainSession.pressKey(KeyEvent.KEYCODE_TAB)

        val pass = mainSession.evaluateJS("document.querySelector('#pass1').value") as String

        if (autofillEnabled) {
            assertThat(
                "Password should match",
                pass,
                equalTo(pass1),
            )
        } else {
            assertThat(
                "Password should not be filled",
                pass,
                equalTo(""),
            )
        }
    }

    @Test
    fun loginAutofillDisabledPasswordAutofill() {
        sessionRule.runtime.settings.loginAutofillEnabled = false
        testPasswordAutofill(false)
        sessionRule.runtime.settings.loginAutofillEnabled = true
    }

    @Test
    fun loginAutofillEnabledPasswordAutofill() {
        testPasswordAutofill(true)
    }

    @Test
    fun loginSelectAccept() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "dom.disable_open_during_load" to false,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        // Test:
        // 1. Load a login form page.
        // 2. Input un/pw and submit.
        //    a. Ensure onLoginSave is called accordingly.
        //    b. Save the submitted login entry.
        // 3. Reload the login form page.
        //    a. Ensure onLoginFetch is called.
        //    b. Return empty login entry list to avoid autofilling.
        // 4. Input a new set of un/pw and submit.
        //    a. Ensure onLoginSave is called again.
        //    b. Save the submitted login entry.
        // 5. Reload the login form page.
        // 6. Focus on the username input field.
        //    a. Ensure onLoginFetch is called.
        //    b. Return the saved login entries.
        //    c. Ensure onLoginSelect is called.
        //    d. Select and return one of the options.
        //    e. Submit the form.
        //    f. Ensure that onLoginUsed is called.

        val user1 = "user1x"
        val user2 = "user2x"
        val pass1 = "pass1x"
        val pass2 = "pass2x"
        val savedLogins = mutableListOf<LoginEntry>()

        val saveHandled1 = GeckoResult<Void>()
        val saveHandled2 = GeckoResult<Void>()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    var logins = mutableListOf<LoginEntry>()

                    if (savedLogins.size == 2) {
                        logins = savedLogins
                    }

                    return GeckoResult.fromValue(logins.toTypedArray())
                }

                @AssertCalled(count = 2)
                override fun onLoginSave(login: LoginEntry) {
                    var username = ""
                    var password = ""
                    var handle = GeckoResult<Void>()

                    if (sessionRule.currentCall.counter == 1) {
                        username = user1
                        password = pass1
                        handle = saveHandled1
                    } else if (sessionRule.currentCall.counter == 2) {
                        username = user2
                        password = pass2
                        handle = saveHandled2
                    }

                    val savedLogin =
                        LoginEntry.Builder()
                            .guid(login.username)
                            .origin(login.origin)
                            .formActionOrigin(login.formActionOrigin)
                            .username(login.username)
                            .password(login.password)
                            .build()

                    savedLogins.add(savedLogin)

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(username),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(password),
                    )

                    handle.complete(null)
                }

                @AssertCalled(count = 1)
                override fun onLoginUsed(login: LoginEntry, usedFields: Int) {
                    assertThat(
                        "Used fields should match",
                        usedFields,
                        equalTo(UsedField.PASSWORD),
                    )

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(pass1),
                    )

                    assertThat(
                        "GUID should match",
                        login.guid,
                        equalTo(user1),
                    )
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(pass1),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign login credentials.
        mainSession.evaluateJS("document.querySelector('#user1').value = '$user1'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = '$pass1'")

        // Submit the form.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")
        sessionRule.waitForResult(saveHandled1)

        // Reload.
        val session2 = sessionRule.createOpenSession()
        session2.loadTestPath(FORMS3_HTML_PATH)
        session2.waitForPageStop()

        session2.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user2),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(pass2),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign alternative login credentials.
        session2.evaluateJS("document.querySelector('#user1').value = '$user2'")
        session2.evaluateJS("document.querySelector('#pass1').value = '$pass2'")

        // Submit the form.
        session2.evaluateJS("document.querySelector('#form1').submit()")
        sessionRule.waitForResult(saveHandled2)

        // Reload for the last time.
        val session3 = sessionRule.createOpenSession()

        session3.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be two options",
                        prompt.options.size,
                        equalTo(2),
                    )

                    var usernames = arrayOf(user1, user2)
                    var passwords = arrayOf(pass1, pass2)

                    for (i in 0..1) {
                        val login = prompt.options[i].value

                        assertThat("Login should not be null", login, notNullValue())
                        assertThat(
                            "Username should match",
                            login.username,
                            equalTo(usernames[i]),
                        )
                        assertThat(
                            "Password should match",
                            login.password,
                            equalTo(passwords[i]),
                        )
                    }

                    return GeckoResult.fromValue(prompt.confirm(prompt.options[0]))
                }
            }
        )

        session3.loadTestPath(FORMS3_HTML_PATH)
        session3.waitForPageStop()

        // Focus on the username input field.
        session3.evaluateJS("document.querySelector('#user1').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onLoginSelect")
        waitForFill(session3, "#user1")

        assertThat(
            "Filled username should match",
            session3.evaluateJS("document.querySelector('#user1').value") as String,
            equalTo(user1),
        )

        assertThat(
            "Filled password should match",
            session3.evaluateJS("document.querySelector('#pass1').value") as String,
            equalTo(pass1),
        )

        // Submit the selection.
        session3.evaluateJS("document.querySelector('#form1').submit()")
        sessionRule.waitUntilCalled(StorageDelegate::class, "onLoginUsed")
    }

    @Test
    fun loginSelectModifyAccept() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "dom.disable_open_during_load" to false,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        // Test:
        // 1. Load a login form page.
        // 2. Input un/pw and submit.
        //    a. Ensure onLoginSave is called accordingly.
        //    b. Save the submitted login entry.
        // 3. Reload the login form page.
        //    a. Ensure onLoginFetch is called.
        //    b. Return empty login entry list to avoid autofilling.
        // 4. Input a new set of un/pw and submit.
        //    a. Ensure onLoginSave is called again.
        //    b. Save the submitted login entry.
        // 5. Reload the login form page.
        // 6. Focus on the username input field.
        //    a. Ensure onLoginFetch is called.
        //    b. Return the saved login entries.
        //    c. Ensure onLoginSelect is called.
        //    d. Select and return a new login entry.
        //    e. Submit the form.
        //    f. Ensure that onLoginUsed is not called.

        val user1 = "user1x"
        val user2 = "user2x"
        val pass1 = "pass1x"
        val pass2 = "pass2x"
        val userMod = "user1xmod"
        val passMod = "pass1xmod"
        val savedLogins = mutableListOf<LoginEntry>()

        val saveHandled1 = GeckoResult<Void>()
        val saveHandled2 = GeckoResult<Void>()

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    var logins = mutableListOf<LoginEntry>()

                    if (savedLogins.size == 2) {
                        logins = savedLogins
                    }

                    return GeckoResult.fromValue(logins.toTypedArray())
                }

                @AssertCalled(count = 2)
                override fun onLoginSave(login: LoginEntry) {
                    var username = ""
                    var password = ""
                    var handle = GeckoResult<Void>()

                    if (sessionRule.currentCall.counter == 1) {
                        username = user1
                        password = pass1
                        handle = saveHandled1
                    } else if (sessionRule.currentCall.counter == 2) {
                        username = user2
                        password = pass2
                        handle = saveHandled2
                    }

                    val savedLogin =
                        LoginEntry.Builder()
                            .guid(login.username)
                            .origin(login.origin)
                            .formActionOrigin(login.formActionOrigin)
                            .username(login.username)
                            .password(login.password)
                            .build()

                    savedLogins.add(savedLogin)

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(username),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(password),
                    )

                    handle.complete(null)
                }

                @AssertCalled(false) override fun onLoginUsed(login: LoginEntry, usedFields: Int) {}
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(pass1),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign login credentials.
        mainSession.evaluateJS("document.querySelector('#user1').value = '$user1'")
        mainSession.evaluateJS("document.querySelector('#pass1').value = '$pass1'")

        // Submit the form.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")
        sessionRule.waitForResult(saveHandled1)

        // Reload.
        val session2 = sessionRule.createOpenSession()
        session2.loadTestPath(FORMS3_HTML_PATH)
        session2.waitForPageStop()

        session2.delegateDuringNextWait(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user2),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(pass2),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign alternative login credentials.
        session2.evaluateJS("document.querySelector('#user1').value = '$user2'")
        session2.evaluateJS("document.querySelector('#pass1').value = '$pass2'")

        // Submit the form.
        session2.evaluateJS("document.querySelector('#form1').submit()")
        sessionRule.waitForResult(saveHandled2)

        // Reload for the last time.
        val session3 = sessionRule.createOpenSession()

        session3.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled(count = 1)
                override fun onLoginSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be two options",
                        prompt.options.size,
                        equalTo(2),
                    )

                    var usernames = arrayOf(user1, user2)
                    var passwords = arrayOf(pass1, pass2)

                    for (i in 0..1) {
                        val login = prompt.options[i].value

                        assertThat("Login should not be null", login, notNullValue())
                        assertThat(
                            "Username should match",
                            login.username,
                            equalTo(usernames[i]),
                        )
                        assertThat(
                            "Password should match",
                            login.password,
                            equalTo(passwords[i]),
                        )
                    }

                    val login = prompt.options[0].value
                    val modOption =
                        LoginSelectOption(
                            LoginEntry.Builder()
                                .origin(login.origin)
                                .formActionOrigin(login.formActionOrigin)
                                .username(userMod)
                                .password(passMod)
                                .build()
                        )

                    return GeckoResult.fromValue(prompt.confirm(modOption))
                }
            }
        )

        session3.loadTestPath(FORMS3_HTML_PATH)
        session3.waitForPageStop()

        // Focus on the username input field.
        session3.evaluateJS("document.querySelector('#user1').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onLoginSelect")
        waitForFill(session3, "#user1")

        assertThat(
            "Filled username should match",
            session3.evaluateJS("document.querySelector('#user1').value") as String,
            equalTo(userMod),
        )

        assertThat(
            "Filled password should match",
            session3.evaluateJS("document.querySelector('#pass1').value") as String,
            equalTo(passMod),
        )

        // Submit the selection.
        session3.evaluateJS("document.querySelector('#form1').submit()")
        session3.waitForPageStop()
    }

    @Test
    fun loginSelectGeneratedPassword() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.generation.enabled" to true,
                "signon.generation.available" to true,
                "dom.disable_open_during_load" to false,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        // Test:
        // 1. Load a login form page.
        // 2. Input username.
        // 3. Focus on the password input field.
        //    a. Ensure onLoginSelect is called with a generated password.
        //    b. Return the login entry with the generated password.
        // 4. Submit the login form.
        //    a. Ensure onLoginSave is called with accordingly.

        val user1 = "user1x"
        var genPass = ""

        val saveHandled1 = GeckoResult<Void>()
        val saveHandled2 = GeckoResult<Void>()
        var numSelects = 0

        var onLoginSaveCount = 0

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    assertThat("Domain should match", domain, equalTo("localhost"))

                    return GeckoResult.fromValue(null)
                }

                @AssertCalled(count = 2)
                override fun onLoginSave(login: LoginEntry) {
                    onLoginSaveCount++

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(genPass),
                    )

                    if (onLoginSaveCount == 1) {
                        assertThat(
                            "Username should be empty",
                            login.username,
                            equalTo(""),
                        )
                        saveHandled1.complete(null)
                    } else if (onLoginSaveCount == 2) {
                        assertThat(
                            "Username should match",
                            login.username,
                            equalTo(user1),
                        )
                        saveHandled2.complete(null)
                    }
                }

                @AssertCalled(false) override fun onLoginUsed(login: LoginEntry, usedFields: Int) {}
            }
        )

        mainSession.loadTestPath(FORMS4_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled
                override fun onLoginSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be one option",
                        prompt.options.size,
                        equalTo(1),
                    )

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat(
                        "Hint should match",
                        option.hint,
                        equalTo(SelectOption.Hint.GENERATED),
                    )

                    assertThat("Login should not be null", login, notNullValue())
                    assertThat(
                        "Password should not be empty",
                        login.password,
                        not(isEmptyOrNullString()),
                    )

                    genPass = login.password

                    ++numSelects

                    return GeckoResult.fromValue(prompt.confirm(option))
                }

                @AssertCalled(count = 1)
                override fun onLoginSave(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSaveOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    val option = prompt.options[0]
                    val login = option.value

                    assertThat("Login should not be null", login, notNullValue())

                    assertThat(
                        "Username should match",
                        login.username,
                        equalTo(user1),
                    )

                    // TODO: The flag is only set for login entry updates yet.
                    /*
                    assertThat(
                        "Hint should match",
                        option.hint,
                        equalTo(LoginSaveOption.Hint.GENERATED))
                     */

                    assertThat(
                        "Password should not be empty",
                        login.password,
                        not(isEmptyOrNullString()),
                    )

                    assertThat(
                        "Password should match",
                        login.password,
                        equalTo(genPass),
                    )

                    return GeckoResult.fromValue(prompt.confirm(option))
                }
            }
        )

        // Assign username and focus on password.
        mainSession.evaluateJS("document.querySelector('#user1').value = '$user1'")
        mainSession.evaluateJS("document.querySelector('#pass1').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onLoginSelect")
        waitForFill(mainSession, "#passConfirm")

        assertThat(
            "Filled username should match",
            mainSession.evaluateJS("document.querySelector('#user1').value") as String,
            equalTo(user1),
        )

        val filledPass = mainSession.evaluateJS("document.querySelector('#pass1').value") as String

        assertThat(
            "Password should not be empty",
            filledPass,
            not(isEmptyOrNullString()),
        )

        assertThat(
            "Filled password should match",
            filledPass,
            equalTo(genPass),
        )

        val filledConfirmPass = mainSession.evaluateJS("document.querySelector('#passConfirm').value") as String

        assertThat(
            "Confirm password should match filled password",
            filledConfirmPass,
            equalTo(genPass),
        )

        sessionRule.waitForResult(saveHandled1)

        // Submit the selection.
        mainSession.evaluateJS("document.querySelector('#form1').submit()")
        mainSession.waitForPageStop()

        sessionRule.waitForResult(saveHandled2)
    }

    @Test
    fun loginSelectDismiss() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
            )
        )

        val user = arrayOf("user1x", "user2x")
        val pass = arrayOf("pass1x", "pass2x")
        val guid = arrayOf("test-guid1", "test-guid2")
        val origin = GeckoSessionTestRule.TEST_ENDPOINT
        val savedLogins =
            arrayOf(
                LoginEntry.Builder()
                    .guid(guid[0])
                    .origin(origin)
                    .formActionOrigin(origin)
                    .username(user[0])
                    .password(pass[0])
                    .build(),
                LoginEntry.Builder()
                    .guid(guid[1])
                    .origin(origin)
                    .formActionOrigin(origin)
                    .username(user[1])
                    .password(pass[1])
                    .build(),
            )

        sessionRule.delegateUntilTestEnd(
            object : StorageDelegate {
                @AssertCalled
                override fun onLoginFetch(domain: String): GeckoResult<Array<LoginEntry>>? {
                    return GeckoResult.fromValue(savedLogins)
                }
            }
        )

        val result = GeckoResult<PromptDelegate.PromptResponse>()
        val promptInstanceDelegate =
            object : PromptDelegate.PromptInstanceDelegate {
                override fun onPromptDismiss(prompt: PromptDelegate.BasePrompt) {
                    result.complete(prompt.dismiss())
                }
            }

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled
                override fun onLoginSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat(
                        "There should be two options",
                        prompt.options.size,
                        equalTo(2),
                    )
                    prompt.setDelegate(promptInstanceDelegate)

                    return pendingResponse
                }
            }
        )

        mainSession.loadTestPath(FORMS3_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.evaluateJS("document.querySelector('#user1').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onLoginSelect")
        mainSession.evaluateJS("document.querySelector('#user1').blur()")
        sessionRule.waitForResult(result)
    }

    @Test
    fun testAddressStructureGetFieldsForUS() {
        val structureResult = Autocomplete.AddressStructure.getAddressStructure("US")

        try {
            sessionRule.waitForResult(structureResult)
            assertTrue("Should not be able to retreive a structure.", true)
        } catch (e: Exception) {
            assertTrue("Should not have an exception.", false)
        }

        sessionRule.waitForResult(structureResult).let { fields ->
            val expectedResult =
                listOf(
                    Pair("name", "autofill-address-name"),
                    Pair("organization", "autofill-address-organization"),
                    Pair("street-address", "autofill-address-street"),
                    Pair("address-level2", "autofill-address-city"),
                    Pair("address-level1", "autofill-address-state"),
                    Pair("postal-code", "autofill-address-zip"),
                    Pair("country", "autofill-address-country"),
                    Pair("tel", "autofill-address-tel"),
                    Pair("email", "autofill-address-email"),
                )

            expectedResult.forEachIndexed { index, pair ->
                assertTrue(
                    "Result should have id: ${pair.first}, localizationKey: ${pair.second}",
                    fields[index].id == pair.first && fields[index].localizationKey == pair.second,
                )
            }
        }
    }

    @Test
    fun testAddressStructureGetFieldsForJP() {
        val structureResult = Autocomplete.AddressStructure.getAddressStructure("JP")

        try {
            sessionRule.waitForResult(structureResult)
            assertTrue("Should not be able to retreive a structure.", true)
        } catch (e: Exception) {
            assertTrue("Should not have an exception.", false)
        }

        sessionRule.waitForResult(structureResult).let { fields ->
            val expectedResult =
                listOf(
                    Pair("postal-code", "autofill-address-postal-code"),
                    Pair("address-level1", "autofill-address-prefecture"),
                    Pair("address-level2", "autofill-address-city"),
                    Pair("street-address", "autofill-address-street"),
                    Pair("organization", "autofill-address-organization"),
                    Pair("name", "autofill-address-name"),
                    Pair("country", "autofill-address-country"),
                    Pair("tel", "autofill-address-tel"),
                    Pair("email", "autofill-address-email"),
                )

            expectedResult.forEachIndexed { index, pair ->
                assertTrue(
                    "Result should have id: ${pair.first}, localizationKey: ${pair.second}",
                    fields[index].id == pair.first && fields[index].localizationKey == pair.second,
                )
            }
        }
    }

    @Test
    fun loginSelectRelayUsername() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                // Enable login management since it's disabled in automation.
                "signon.rememberSignons" to true,
                "signon.autofillForms.http" to true,
                "signon.generation.enabled" to true,
                "signon.generation.available" to true,
                "dom.disable_open_during_load" to false,
                "signon.userInputRequiredToCapture.enabled" to false,
                "signon.testOnlyNotWaitForPaint" to true,
                "signon.firefoxRelay.feature" to "enabled",
            )
        )

        // Test:
        // 1. Load a sign-up form page.
        // 2. Focus on the username input field.
        //    a. Ensure onLoginSelect is called with a generate relay option.
        //    b. Return the login entry with a hard-coded relay username.
        // 3. Submit the login form.
        //    a. Ensure onLoginSave is called with accordingly.

        val genRelayUsername = "userRelay"

        mainSession.loadTestPath(FORMS4_HTML_PATH)
        mainSession.waitForPageStop()

        mainSession.delegateUntilTestEnd(
            object : PromptDelegate {
                @AssertCalled
                override fun onLoginSelect(
                    session: GeckoSession,
                    prompt: AutocompleteRequest<LoginSelectOption>,
                ): GeckoResult<PromptDelegate.PromptResponse>? {
                    assertThat("Session should not be null", session, notNullValue())

                    assertThat(
                        "There should be one option",
                        prompt.options.size,
                        equalTo(1),
                    )

                    val option = prompt.options[0]
                    val login = option.value

                    // GENERATE_RELAY_USERNAME option is only offered when users are signed in to their FxAccount.
                    // However, Gecko currently doesn't detect whether the FxAccount is signed in on mobile,
                    // so this option is always OFFER_RELAY_INTEGRATION.
                    assertThat(
                        "Hint should match",
                        option.hint,
                        equalTo(SelectOption.Hint.FIREFOX_RELAY),
                    )

                    // Change `hint` to FILL_LOGIN to instruct Gecko to fill the generated relay username.
                    val hint = SelectOption.Hint.FIREFOX_RELAY
                    val origin = GeckoSessionTestRule.TEST_ENDPOINT
                    val relayUsername =
                        LoginEntry.Builder().origin(origin).formActionOrigin(origin).username(genRelayUsername).build()

                    return GeckoResult.fromValue(prompt.confirm(LoginSelectOption(relayUsername, hint)))
                }
            }
        )

        // focus on username.
        mainSession.evaluateJS("document.querySelector('#user1').focus()")
        sessionRule.waitUntilCalled(PromptDelegate::class, "onLoginSelect")
        waitForFill(mainSession, "#user1")

        assertThat(
            "Filled username should match",
            mainSession.evaluateJS("document.querySelector('#user1').value") as String,
            equalTo(genRelayUsername),
        )

        assertThat(
            "Password should be empty",
            mainSession.evaluateJS("document.querySelector('#pass1').value") as String,
            equalTo(""),
        )
    }
}
