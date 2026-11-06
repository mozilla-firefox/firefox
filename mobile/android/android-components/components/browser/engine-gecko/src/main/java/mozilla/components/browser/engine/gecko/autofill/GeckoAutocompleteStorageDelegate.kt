/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.autofill

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.launch
import mozilla.components.browser.engine.gecko.ext.toAddress
import mozilla.components.browser.engine.gecko.ext.toAutocompleteAddress
import mozilla.components.browser.engine.gecko.ext.toCreditCardEntry
import mozilla.components.browser.engine.gecko.ext.toLoginEntry
import mozilla.components.concept.storage.CreditCard
import mozilla.components.concept.storage.CreditCardsAddressesStorageDelegate
import mozilla.components.concept.storage.Login
import mozilla.components.concept.storage.LoginStorageDelegate
import org.mozilla.geckoview.Autocomplete
import org.mozilla.geckoview.GeckoResult

/**
 * Gecko credit card and login storage delegate that handles runtime storage requests. This allows the Gecko runtime to
 * call the underlying storage to handle requests for fetching, saving and updating of autocomplete items in the
 * storage.
 *
 * @param creditCardsAddressesStorageDelegate An instance of [CreditCardsAddressesStorageDelegate]. Provides methods for
 *   retrieving [CreditCard]s from the underlying storage.
 * @param loginStorageDelegate An instance of [LoginStorageDelegate]. Provides read/write methods for the [Login]
 *   storage.
 * @param applicationScope The [CoroutineScope] used for launching storage operations. These callbacks are invoked by
 *   Gecko and must complete even if a specific component's lifecycle has ended. Callers should pass an
 *   application-scoped coroutine scope when possible.
 */
class GeckoAutocompleteStorageDelegate(
    private val creditCardsAddressesStorageDelegate: CreditCardsAddressesStorageDelegate,
    private val loginStorageDelegate: LoginStorageDelegate,
    private val applicationScope: CoroutineScope,
) : Autocomplete.StorageDelegate {

    override fun onAddressFetch(): GeckoResult<Array<Autocomplete.Address>>? {
        val result = GeckoResult<Array<Autocomplete.Address>>()

        applicationScope.launch(IO) {
            val addresses =
                creditCardsAddressesStorageDelegate.onAddressesFetch().map { it.toAutocompleteAddress() }.toTypedArray()

            result.complete(addresses)
        }

        return result
    }

    override fun onCreditCardFetch(): GeckoResult<Array<Autocomplete.CreditCard>> {
        val result = GeckoResult<Array<Autocomplete.CreditCard>>()

        applicationScope.launch(IO) {
            val creditCards =
                creditCardsAddressesStorageDelegate
                    .onCreditCardsFetch()
                    .map {
                        Autocomplete.CreditCard.Builder()
                            .guid(it.guid)
                            .name(it.billingName)
                            .number(it.cardNumber)
                            .expirationMonth(it.expiryMonth.toString())
                            .expirationYear(it.expiryYear.toString())
                            .securityCode(it.cardCvv)
                            .build()
                    }
                    .toTypedArray()

            result.complete(creditCards)
        }

        return result
    }

    override fun onCreditCardSave(creditCard: Autocomplete.CreditCard) {
        applicationScope.launch(IO) {
            creditCardsAddressesStorageDelegate.onCreditCardSave(creditCard.toCreditCardEntry())
        }
    }

    override fun onAddressSave(address: Autocomplete.Address) {
        applicationScope.launch(IO) {
            creditCardsAddressesStorageDelegate.onAddressSave(address.toAddress())
        }
    }

    override fun onLoginSave(login: Autocomplete.LoginEntry) {
        loginStorageDelegate.onLoginSave(login.toLoginEntry())
    }

    override fun onLoginFetch(domain: String): GeckoResult<Array<Autocomplete.LoginEntry>> {
        val result = GeckoResult<Array<Autocomplete.LoginEntry>>()

        applicationScope.launch(IO) {
            val storedLogins = loginStorageDelegate.onLoginFetch(domain)

            val logins = storedLogins.await().map { it.toLoginEntry() }.toTypedArray()

            result.complete(logins)
        }

        return result
    }
}
