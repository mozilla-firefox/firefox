# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Variables:
#  $retriesLeft (Number): number of tries left
webauthn-pin-invalid-long-prompt =
    { $retriesLeft ->
        [one] Incorrect PIN. You have { $retriesLeft } attempt left before you permanently lose access to the credentials on this device.
       *[other] Incorrect PIN. You have { $retriesLeft } attempts left before you permanently lose access to the credentials on this device.
    }
webauthn-pin-invalid-short-prompt = Incorrect PIN. Try again.
webauthn-pin-required-prompt = Please enter the PIN for your device.

webauthn-select-sign-result-unknown-account = Unknown account

webauthn-a-passkey-label = Use a passkey
webauthn-another-passkey-label = Use another passkey

# Variables:
#   $domain (String): the domain of the site.
webauthn-specific-passkey-label = Passkey for { $domain }

# Variables:
#  $retriesLeft (Number): number of tries left
webauthn-uv-invalid-long-prompt =
    { $retriesLeft ->
        [one] User verification failed. You have { $retriesLeft } attempt left. Try again.
       *[other] User verification failed. You have { $retriesLeft } attempts left. Try again.
    }
webauthn-uv-invalid-short-prompt = User verification failed. Try again.

## WebAuthn prompts

# Variables:
#  $hostname (String): the origin (website) asking for the security key.
webauthn-user-presence-prompt = Touch your security key to continue with { $hostname }.
# The website is asking for extended information about your
# hardware authenticator that shouldn't be generally necessary. Permitting
# this is safe if you only use one account at this website. If you have
# multiple accounts at this website, and you use the same hardware
# authenticator, then the website could link those accounts together.
# And this is true even if you use a different profile / browser (or even Tor
# Browser). To avoid this, you should use different hardware authenticators
# for different accounts on this website.
# Variables:
#  $hostname (String): the origin (website) asking for the extended information.
webauthn-register-direct-prompt =
    { $hostname } is requesting extended information about your security key, which may affect your privacy.
webauthn-register-direct-prompt-hint =
    { -brand-short-name } can anonymize this for you, but the website might decline this key. If declined, you can try again.
# Variables:
#  $hostname (String): the origin (website) for which an account needs to be selected.
webauthn-select-sign-result-prompt =
    Multiple accounts found for { $hostname }. Select which to use or cancel.
# Variables:
#  $hostname (String): the origin (website) for which a device needs to be selected.
webauthn-select-device-prompt =
    Multiple devices found for { $hostname }. Please select one.
# Variables:
#  $hostname (String): the origin (website) for which user verification failed.
webauthn-device-blocked-prompt =
    User verification failed on { $hostname }. There are no attempts left and your device has been locked, because the wrong PIN was provided too many times. The device needs a reset.
# Variables:
#  $hostname (String): the origin (website) for which user verification failed.
webauthn-pin-auth-blocked-prompt =
    User verification failed on { $hostname }. There were too many failed attempts in a row and PIN authentication has been temporarily blocked. Your device needs a power cycle (unplug and re-insert).
# Variables:
#  $hostname (String): the origin (website) for which user verification failed.
webauthn-pin-not-set-prompt =
    User verification failed on { $hostname }. You may need to set a PIN on your device.
# Variables:
#  $hostname (String): the origin (website) for which user verification failed.
webauthn-uv-blocked-prompt =
    User verification failed on { $hostname }. There were too many failed attempts and the built-in user verification method has been blocked.
webauthn-already-registered-prompt =
    This device is already registered. Try a different device.
webauthn-cancel = Cancel
    .accesskey = c
webauthn-allow = Allow
    .accesskey = A
webauthn-block = Block
    .accesskey = B
webauthn-continue = Continue
    .accesskey = n

# Variables:
#   $origin (String): the domain of the site making the request
#   $rpId (String): the related domain the passkey is associated with
webauthn-related-origin-create-header = { $origin } wants to create a passkey for { $rpId }.
# Variables:
#   $origin (String): the domain of the site making the request
#   $rpId (String): the related domain the passkey is associated with
webauthn-related-origin-use-header = { $origin } wants to use a passkey for { $rpId }.
