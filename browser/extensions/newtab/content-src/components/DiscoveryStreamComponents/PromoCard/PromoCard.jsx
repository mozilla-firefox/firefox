/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useCallback } from "react";
import { useDispatch } from "react-redux";
import { actionCreators as ac, actionTypes as at } from "common/Actions.mjs";
import { useIntersectionObserver } from "../../../lib/utils";
import { CUSTOMIZE_PANEL_SOURCES } from "content-src/lib/constants.mjs";

const PREF_PROMO_CARD_DISMISSED = "discoverystream.promoCard.visible";
const PROMO_CARD_IMAGE_SRC =
  "chrome://newtab/content/data/content/assets/firefox-mascot-prop-paintbucket-rgb.svg";

/**
 * The PromoCard component displays a promotional message.
 * It is used next to the AdBanner component in a four-column layout.
 */

const PromoCard = () => {
  const dispatch = useDispatch();

  const onCtaClick = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.PROMO_CARD_CLICK,
      })
    );
    dispatch({
      type: at.SHOW_PERSONALIZE,
      data: { source: CUSTOMIZE_PANEL_SOURCES.PROMO_CARD },
    });
  }, [dispatch]);

  const onDismissClick = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.PROMO_CARD_DISMISS,
      })
    );
    dispatch(ac.SetPref(PREF_PROMO_CARD_DISMISSED, false));
  }, [dispatch]);

  const handleIntersection = useCallback(() => {
    dispatch(
      ac.AlsoToMain({
        type: at.PROMO_CARD_IMPRESSION,
      })
    );
  }, [dispatch]);

  const ref = useIntersectionObserver(handleIntersection);

  return (
    <div
      className="promo-card-wrapper"
      ref={el => {
        ref.current = [el];
      }}
    >
      <div className="promo-card-dismiss-button">
        <moz-button
          type="icon ghost"
          size="small"
          data-l10n-id="newtab-promo-card-dismiss-button"
          iconsrc="chrome://global/skin/icons/close.svg"
          onClick={onDismissClick}
          onKeyDown={onDismissClick}
        />
      </div>
      <div className="promo-card-inner">
        <div className="img-wrapper">
          <img src={PROMO_CARD_IMAGE_SRC} alt="" />
        </div>
        <div className="promo-card-content">
          <div className="promo-card-copy">
            <div className="promo-card-title-wrapper">
              <span
                className="promo-card-title"
                data-l10n-id="newtab-promo-card-title-addons"
              />
            </div>
            <p
              className="promo-card-body"
              data-l10n-id="newtab-promo-card-body-addons"
            />
          </div>
          <div className="promo-card-cta-wrapper">
            <moz-button
              className="promo-card-cta"
              type="default"
              data-l10n-id="newtab-promo-card-cta-addons"
              onClick={onCtaClick}
            />
          </div>
        </div>
      </div>
    </div>
  );
};

export { PromoCard };
