# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

password-quality-meter = Password quality meter

## Change Password dialog

change-device-password-window =
  .title = Change Password

# Variables:
# $tokenName (String) - Security device of the change password dialog
change-password-token = Security Device: { $tokenName }
change-password-old = Current password:
change-password-new = New password:
change-password-reenter = New password (again):
change-password-not-set =
  .value = (not set)

pippki-failed-pw-change = Unable to change password.
pippki-incorrect-pw = You did not enter the correct current password. Please try again.
pippki-pw-change-ok = Password successfully changed.

pippki-pw-empty-warning = Your stored passwords and private keys will not be protected.
pippki-pw-erased-ok = You have deleted your password. { pippki-pw-empty-warning }
pippki-pw-not-wanted = Warning! You have decided not to use a password. { pippki-pw-empty-warning }

pippki-pw-change2empty-in-fips-mode = You are currently in FIPS mode. FIPS requires a non-empty password.

## Reset Primary Password dialog

reset-primary-password-window2 =
  .title = Reset Primary Password
  .style = min-width: 40em
reset-password-button-label =
  .label = Reset
reset-primary-password-text = If you reset your Primary Password, all your stored web and e-mail passwords, personal certificates, and private keys will be forgotten. Are you sure you want to reset your Primary Password?

pippki-reset-password-confirmation-title = Reset Primary Password
pippki-reset-password-confirmation-message = Your Primary Password has been reset.

## Downloading cert dialog

download-cert-window2 =
  .title = Downloading Certificate
  .style = min-width: 46em
download-cert-message = You have been asked to trust a new Certificate Authority (CA).
# Variables:
#   $caName (string) - The name of the Certificate Authority
download-cert-trust-header = Do you want to trust “{ $caName }” for the following purposes?
# Used as $caName in download-cert-trust-header when the Certificate Authority has no name
download-cert-unnamed-ca = Certificate Authority (unnamed)
download-cert-trust-ssl =
  .label = Trust this CA to identify websites.
download-cert-trust-email =
  .label = Trust this CA to identify email users.
download-cert-message-desc = Before trusting this CA for any purpose, you should examine its certificate and its policy and procedures (if available).
download-cert-view-cert =
  .label = View
download-cert-view-text = Examine CA certificate

## Client Authentication Ask dialog

client-auth-window =
  .title = User Identification Request
client-auth-send-no-certificate =
  .label = Don’t send a certificate

# Variables:
# $hostname (String) - The domain name of the site requesting the client authentication certificate
client-auth-site-identification = “{ $hostname }” has requested that you identify yourself with a certificate:
client-auth-cert-details = Details of selected certificate:
# Variables:
# $issuedTo (String) - The subject common name of the currently-selected client authentication certificate
client-auth-cert-details-issued-to = Issued to: { $issuedTo }
# Variables:
# $serialNumber (String) - The serial number of the certificate (hexadecimal of the form "AA:BB:...")
client-auth-cert-details-serial-number = Serial number: { $serialNumber }
# Variables:
# $notBefore (String) - The date before which the certificate is not valid (e.g. Apr 21, 2023, 1:47:53 PM UTC)
# $notAfter (String) - The date after which the certificate is not valid
client-auth-cert-details-validity-period = Valid from { $notBefore } to { $notAfter }
# Variables:
# $keyUsages (String) - A list of already-localized key usages for which the certificate may be used
client-auth-cert-details-key-usages = Key usages: { $keyUsages }
# Variables:
# $emailAddresses (String) - A list of email addresses present in the certificate
client-auth-cert-details-email-addresses = Email addresses: { $emailAddresses }
# Variables:
# $issuedBy (String) - The issuer common name of the certificate
client-auth-cert-details-issued-by = Issued by: { $issuedBy }
# Variables:
# $storedOn (String) - The name of the token holding the certificate (for example, "OS Client Cert Token (Modern)")
client-auth-cert-details-stored-on = Stored on: { $storedOn }

client-auth-cert-remember-label = Remember this decision:
client-auth-cert-remember-never =
  .label = Once
client-auth-cert-remember-always =
  .label = Permanently
client-auth-cert-remember-temporarily =
  .label = For this session

## Set password (p12) dialog

set-password-window =
  .title = Choose a Certificate Backup Password
set-password-message = The certificate backup password you set here protects the backup file that you are about to create. You must set this password to proceed with the backup.
set-password-backup-pw =
  .value = Certificate backup password:
set-password-repeat-backup-pw =
  .value = Certificate backup password (again):
set-password-reminder = Important: If you forget your certificate backup password, you will not be able to restore this backup later. Please record it in a safe location.

## Certificate backup import prompt

pippki-get-pkcs12-password-message = Please enter the password that was used to encrypt this certificate backup:

## Protected authentication dialog

protected-auth-window =
  .title = Protected Authentication

# Variables:
# $tokenName (String) - The name of the token to authenticate to (for example, “OS Client Cert Token (Modern)”)
protected-auth-prompt = Please authenticate to the security device ({ $tokenName }). How to do so depends on the device (for example, using a fingerprint reader or entering a code with a keypad).
