# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate import REPLACE
from fluent.migrate.helpers import VARIABLE_REFERENCE, transforms_from


def migrate(ctx):
    """Bug 2086954 - Migrate pippki.properties strings to Fluent, part {index}."""

    source = "security/manager/chrome/pippki/pippki.properties"
    target = "browser/browser/pageInfo.ftl"
    encryption_args = {
        "%1$S": VARIABLE_REFERENCE("algorithm"),
        "%2$S": VARIABLE_REFERENCE("keySize"),
        "%3$S": VARIABLE_REFERENCE("protocol"),
    }
    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
security-technical-no-encryption =
    .value = { COPY(from_path, "pageInfo_NoEncryption") }
security-technical-mixed-content =
    .value = { COPY(from_path, "pageInfo_MixedContent") }
security-technical-no-encryption-transit = { COPY(from_path, "pageInfo_Privacy_None2") }
security-technical-no-encryption-page = { COPY(from_path, "pageInfo_Privacy_None4") }
security-technical-encrypted-page = { COPY(from_path, "pageInfo_Privacy_Encrypted1") }
security-technical-encrypted-description = { COPY(from_path, "pageInfo_Privacy_Encrypted2") }
security-technical-mixed-content-description = { COPY(from_path, "pageInfo_MixedContent2") }
security-technical-weak-cipher = { COPY(from_path, "pageInfo_WeakCipher") }
security-technical-ct-compliant = { COPY(from_path, "pageInfo_CertificateTransparency_Compliant") }
""",
            from_path=source,
        )
        + [
            FTL.Message(
                id=FTL.Identifier("security-technical-encrypted"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("value"),
                        value=REPLACE(
                            source,
                            "pageInfo_EncryptionWithBitsAndProtocol",
                            encryption_args,
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("security-technical-broken-encryption"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("value"),
                        value=REPLACE(
                            source,
                            "pageInfo_BrokenEncryption",
                            encryption_args,
                        ),
                    ),
                ],
            ),
            FTL.Message(
                id=FTL.Identifier("security-technical-no-encryption-website"),
                value=REPLACE(
                    source,
                    "pageInfo_Privacy_None1",
                    {"%1$S": VARIABLE_REFERENCE("website")},
                ),
            ),
        ],
    )

    target = "security/manager/security/pippki/pippki.ftl"
    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
change-password-not-set =
    .value = { COPY(from_path, "password_not_set") }
download-cert-unnamed-ca = { COPY(from_path, "unnamedCA") }
pippki-get-pkcs12-password-message = { COPY(from_path, "getPKCS12FilePasswordMessage") }
""",
            from_path=source,
        )
        + [
            FTL.Message(
                id=FTL.Identifier("download-cert-trust-header"),
                value=REPLACE(
                    source,
                    "newCAMessage1",
                    {"%1$S": VARIABLE_REFERENCE("caName")},
                ),
            ),
        ],
    )
