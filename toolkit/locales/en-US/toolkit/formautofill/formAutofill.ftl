# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## OS Prompt Dialog

# The macos string is preceded by the operating system (macOS) with "Firefox is trying to ",
# and has a period added to its end. Make sure to test in your locale.
autofill-use-payment-method-os-prompt-macos = use stored payment method information
autofill-use-payment-method-os-prompt-windows = { -brand-short-name } is trying to use stored payment method information. Confirm access to this Windows account below.
autofill-use-payment-method-os-prompt-other = { -brand-short-name } is trying to use stored payment method information.


# In macOS, this string is preceded by the operating system with "Firefox is trying to ",
# and has a period added to its end. Make sure to test in your locale.
autofill-edit-payment-method-os-prompt-macos = show stored payment method information
autofill-edit-payment-method-os-prompt-windows = { -brand-short-name } is trying to show stored payment method information. Confirm access to this Windows account below.
autofill-edit-payment-method-os-prompt-other = { -brand-short-name } is trying to show stored payment method information.

# In macOS, this string is preceded by the operating system with "Firefox is trying to ",
# and has a period added to its end. Make sure to test in your locale.
autofill-delete-payment-method-os-prompt-macos = delete stored payment method information
autofill-delete-payment-method-os-prompt-windows = { -brand-short-name } is trying to delete stored payment method information. Confirm access to this Windows account below.
autofill-delete-payment-method-os-prompt-other = { -brand-short-name } is trying to delete stored payment method information.

# The button leads users to Form Autofill browser preferences.
credit-card-doorhanger-options-button =
    .title = Form autofill options

## The credit card capture doorhanger

# If Sync is enabled and credit card sync is available,
# this checkbox is displayed on the doorhanger shown when saving credit card.
credit-card-doorhanger-credit-cards-sync-checkbox = Sync all saved cards across my devices

## Used on the doorhanger when users submit payment with credit card.

credit-card-save-doorhanger-header = Securely save this card?
credit-card-save-doorhanger-description = { -brand-short-name } encrypts your card number. Your security code won’t be saved.

## Used on the doorhanger when users submit a new card and security codes can be saved.

credit-card-save-payment-method-doorhanger-header = Save this payment method?
# Also used on the doorhanger when a saved card is submitted with a new or
# changed security code.
# “Checkout” refers to the payment stage on websites.
credit-card-save-payment-method-doorhanger-description = { -brand-short-name } encrypts and autofills your payment details for faster checkout.
credit-card-capture-dismiss-button =
    .label = Dismiss
    .accessKey = D

# The toggle turns saving security codes on and off. The label stays the same
# in both states.
credit-card-doorhanger-always-save-security-codes-toggle =
    .label = Always save security codes

credit-card-capture-save-button =
    .label = Save
    .accessKey = S
credit-card-capture-cancel-button =
    .label = Not now
    .accessKey = W
credit-card-capture-never-save-payment-methods-button =
    .label = Never save payment methods
    .accessKey = N

# Used on the doorhanger when an credit card change is detected.

credit-card-update-doorhanger-header = Update card?
credit-card-update-doorhanger-description = Card to update:

credit-card-capture-save-new-button =
    .label = Save as new card
    .accessKey = C
credit-card-capture-update-button =
    .label = Update existing card
    .accessKey = U

## Used on the doorhanger when a saved card is submitted with changed details
## and security codes can be saved.

credit-card-update-saved-payment-method-doorhanger-header = Update saved payment method?
# Saves the submitted details as a new payment method instead of updating
# the saved one.
credit-card-capture-save-as-new-payment-method-button =
    .label = Save as new
    .accessKey = S
credit-card-capture-update-payment-method-button =
    .label = Update
    .accessKey = U

## Used on the doorhanger when a saved card is submitted with a new or changed
## security code.

credit-card-save-security-code-doorhanger-header = Save security code?

##

# Label for the button in the dropdown menu used to clear the populated form.
autofill-clear-form-label = Clear Autofill Form

# Used as a label for the button, displayed at the bottom of the dropdown suggestion, to open Form Autofill browser preferences.
autofill-manage-addresses-label = Manage addresses

# Used as a label for the button, displayed at the bottom of the dropdown suggestion, to open Form Autofill browser preferences.
autofill-manage-payment-methods-label = Manage payment methods

## The name of the security code printed on a payment card, as each card
## network calls it.

# Visa calls it "CVV".
credit-card-security-code-name-visa = CVV
# Mastercard calls it "CVC".
credit-card-security-code-name-mastercard = CVC
# American Express calls it "CID".
credit-card-security-code-name-amex = CID
# Used for any other card network, or when the network isn't known.
credit-card-security-code-name-other = Security code

## Details of the card shown on the credit card capture doorhanger, below the
## masked card number. Each message is used for a different combination of the
## available details.
## Variables:
##   $name (String): The cardholder name
##   $month (String): Two-digit month the card expires
##   $year (String): Two-digit year the card expires
##   $securityCode (String): The name of the card's security code, from one of
##     the credit-card-security-code-name-* strings (for example "CVV")

credit-card-doorhanger-card-details-name = { $name }
credit-card-doorhanger-card-details-expiration = { $month }/{ $year }
credit-card-doorhanger-card-details-security-code = { $securityCode }
credit-card-doorhanger-card-details-name-expiration = { $name } | { $month }/{ $year }
credit-card-doorhanger-card-details-name-security-code = { $name } | { $securityCode }
credit-card-doorhanger-card-details-expiration-security-code = { $month }/{ $year } | { $securityCode }
credit-card-doorhanger-card-details-name-expiration-security-code = { $name } | { $month }/{ $year } | { $securityCode }

## These are brand names and should only be translated when a locale-specific name for that brand is in common use

autofill-card-network-amex = American Express
autofill-card-network-cartebancaire = Carte Bancaire
autofill-card-network-diners = Diners Club
autofill-card-network-discover = Discover
autofill-card-network-jcb = JCB
autofill-card-network-mastercard = MasterCard
autofill-card-network-mir = MIR
autofill-card-network-unionpay = Union Pay
autofill-card-network-visa = Visa

# Used in autofill drop down suggestion to indicate what other categories Form Autofill will attempt to fill.
autofill-category-address = address
autofill-category-name = name
autofill-category-organization = organization
autofill-category-tel = phone
autofill-category-email = email

# This string is used in drop down suggestion when users try to autofill credit
# card on an insecure website (without https).
autofill-insecure-field-warning-description = { -brand-short-name } has detected an insecure site. Form Autofill is temporarily disabled.
