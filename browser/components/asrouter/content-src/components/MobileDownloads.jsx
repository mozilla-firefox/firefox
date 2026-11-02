/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { Localized } from "./MSLocalized";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";

export const MobileDownloads = props => {
  const { QR_code: QRCode } = props.data;
  const showEmailLink =
    props.data.email && window.AWSendToDeviceEmailsSupported();

  return (
    <div className="mobile-downloads">
      {/* Avoid use of Localized element to set alt text here as a plain string value
      results in a React error due to "dangerouslySetInnerHTML" */}
      {QRCode ? (
        <img
          data-l10n-id={
            QRCode.alt_text.string_id ? QRCode.alt_text.string_id : null
          }
          className="qr-code-image"
          alt={typeof QRCode.alt_text === "string" ? QRCode.alt_text : ""}
          src={QRCode.image_url}
          loading={MultiStageUtils.getLoadingStrategyFor(QRCode.image_url)}
        />
      ) : null}
      {showEmailLink ? (
        <div>
          <Localized text={props.data.email.link_text}>
            <button
              className="email-link"
              value="email_link"
              onClick={props.handleAction}
            />
          </Localized>
        </div>
      ) : null}
    </div>
  );
};
