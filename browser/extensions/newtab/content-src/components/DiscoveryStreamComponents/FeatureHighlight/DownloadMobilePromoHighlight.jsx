/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { FeatureHighlight } from "./FeatureHighlight";
import React, { useCallback } from "react";
import { useSelector } from "react-redux";

const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_A =
  "mobileDownloadModal.variant-a";
const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_B =
  "mobileDownloadModal.variant-b";
const PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_C =
  "mobileDownloadModal.variant-c";

export function DownloadMobilePromoHighlight({
  position,
  dispatch,
  handleDismiss,
  handleBlock,
}) {
  const onDismiss = useCallback(() => {
    handleDismiss();
    handleBlock();
  }, [handleDismiss, handleBlock]);

  const prefs = useSelector(state => state.Prefs.values);
  const mobileDownloadPromoVarA =
    prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_A];
  const mobileDownloadPromoVarB =
    prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_B];
  const mobileDownloadPromoVarC =
    prefs[PREF_MOBILE_DOWNLOAD_HIGHLIGHT_VARIANT_C];

  function getActiveVariant() {
    if (mobileDownloadPromoVarA) {
      return "A";
    }
    if (mobileDownloadPromoVarB) {
      return "B";
    }
    if (mobileDownloadPromoVarC) {
      return "C";
    }
    return null;
  }

  function getVariantQRCodeImg() {
    const variant = getActiveVariant();
    switch (variant) {
      case "A":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-a.png";
      case "B":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-b.png";
      case "C":
        return "chrome://newtab/content/data/content/assets/download-qr-code-var-c.png";
      default:
        return null;
    }
  }

  function getVariantCopy() {
    const variant = getActiveVariant();
    switch (variant) {
      case "A":
        return "newtab-download-mobile-highlight-body-variant-a";
      case "B":
        return "newtab-download-mobile-highlight-body-variant-b";
      case "C":
        return "newtab-download-mobile-highlight-body-variant-c";
      default:
        return null;
    }
  }

  return (
    <div className="download-firefox-feature-highlight">
      <FeatureHighlight
        position={position}
        feature="FEATURE_DOWNLOAD_MOBILE_PROMO"
        dispatch={dispatch}
        message={
          <div className="download-firefox-feature-highlight-content">
            <img
              src={getVariantQRCodeImg()}
              data-l10n-id="newtab-download-mobile-highlight-image"
              width="120"
              height="191"
              alt=""
            />
            <p
              className="title"
              data-l10n-id="newtab-download-mobile-highlight-title"
            />
            <p className="subtitle" data-l10n-id={getVariantCopy()} />
          </div>
        }
        openedOverride={true}
        showButtonIcon={false}
        dismissCallback={onDismiss}
        outsideClickCallback={handleDismiss}
      />
    </div>
  );
}
