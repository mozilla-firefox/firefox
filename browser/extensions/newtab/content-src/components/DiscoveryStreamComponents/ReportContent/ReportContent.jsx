/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
import React, { useRef, useEffect, useCallback, useState } from "react";
import { useSelector, useDispatch } from "react-redux";
import { actionTypes as at, actionCreators as ac } from "common/Actions.mjs";

export const PREF_REPORT_ADS_NEW_REASONS_ENABLED =
  "discoverystream.reportAds.newReasons.enabled";

const LEGACY_AD_REPORT_REASONS = [
  {
    l10nId: "newtab-report-ads-reason-not-interested",
    value: "not_interested",
  },
  { l10nId: "newtab-report-ads-reason-inappropriate", value: "inappropriate" },
  {
    l10nId: "newtab-report-ads-reason-seen-it-too-many-times",
    value: "seen_too_many_times",
  },
];

/**
 * @backward-compat { version 161 }
 * The main process maps these values to MozAdsReportReason, which only has
 * SCAM, MISLEADING and OFFENSIVE from Firefox 161. On older versions reports
 * sent through the ads client are dropped, so only use this list when both
 * PREF_REPORT_ADS_NEW_REASONS_ENABLED and adsNewReportReasonsSupported (set by
 * PrefsFeed from the Firefox version) are true.
 */
const AD_REPORT_REASONS = [
  { l10nId: "newtab-report-ads-reason-scam", value: "scam" },
  { l10nId: "newtab-report-ads-reason-misleading", value: "misleading" },
  { l10nId: "newtab-report-ads-reason-offensive", value: "offensive" },
];

export const ReportContent = spocs => {
  const dispatch = useDispatch();
  const modal = useRef(null);
  const radioGroupRef = useRef(null);
  const submitButtonRef = useRef(null);
  const report = useSelector(state => state.DiscoveryStream.report);
  const newReportReasonsEnabled = useSelector(
    state => state.Prefs.values[PREF_REPORT_ADS_NEW_REASONS_ENABLED]
  );
  const newReportReasonsSupported = useSelector(
    state => state.Prefs.values.adsNewReportReasonsSupported
  );
  const adReportReasons =
    newReportReasonsEnabled && newReportReasonsSupported
      ? AD_REPORT_REASONS
      : LEGACY_AD_REPORT_REASONS;
  const [valueSelected, setValueSelected] = useState(false);
  const [selectedReason, setSelectedReason] = useState(null);
  const spocData = spocs.spocs.data;

  // Sends a dispatch to update the redux store when modal is cancelled
  const handleCancel = () => {
    dispatch(
      ac.AlsoToMain({
        type: at.REPORT_CLOSE,
      })
    );
  };

  const handleSubmit = useCallback(() => {
    const {
      card_type,
      corpus_item_id,
      position,
      reporting_url,
      scheduled_corpus_item_id,
      section_position,
      section,
      title,
      topic,
      url,
    } = report;

    if (card_type === "organic") {
      dispatch(
        ac.AlsoToMain({
          type: at.REPORT_CONTENT_SUBMIT,
          data: {
            card_type,
            corpus_item_id,
            report_reason: selectedReason,
            scheduled_corpus_item_id,
            section_position,
            section,
            title,
            topic,
            url,
          },
        })
      );
    } else if (card_type === "spoc") {
      // Retrieve placement_id by comparing spocData with the ad that was reported
      const getPlacementId = () => {
        if (!spocData || !report.url) {
          return null;
        }

        for (const [placementId, spocList] of Object.entries(spocData)) {
          for (const spoc of Object.values(spocList)) {
            if (spoc?.url === report.url) {
              return placementId;
            }
          }
        }
        return null;
      };

      const placement_id = getPlacementId();

      dispatch(
        ac.AlsoToMain({
          type: at.REPORT_AD_SUBMIT,
          data: {
            report_reason: selectedReason,
            placement_id,
            position,
            reporting_url,
            url,
          },
        })
      );
    }

    dispatch(
      ac.AlsoToMain({
        type: at.BLOCK_URL,
        data: [{ ...report }],
      })
    );

    dispatch(
      ac.OnlyToOneContent(
        {
          type: at.SHOW_TOAST_MESSAGE,
          data: {
            toastId: "reportSuccessToast",
            showNotifications: true,
          },
        },
        "ActivityStream:Content"
      )
    );
  }, [dispatch, selectedReason, report, spocData]);

  // Opens and closes the modal based on user interaction
  useEffect(() => {
    if (report.visible && modal?.current) {
      modal.current.showModal();

      // Clear any previously selected radio button
      const radioGroup = radioGroupRef.current;
      if (radioGroup) {
        const selectedRadioButton =
          radioGroup.querySelector("moz-radio[checked]");
        if (selectedRadioButton) {
          selectedRadioButton.removeAttribute("checked");
        }
      }

      // Clear out the states
      setValueSelected(false);
      setSelectedReason(null);
    } else if (!report.visible && modal?.current?.open) {
      modal.current.close();
    }
  }, [report.visible]);

  // Updates the submit button's state based on if a value is selected
  useEffect(() => {
    const radioGroup = radioGroupRef.current;
    const submitButton = submitButtonRef.current;

    const handleRadioChange = e => {
      const reasonValue = e?.target?.value;

      if (reasonValue) {
        setValueSelected(true);
        setSelectedReason(reasonValue);
      }
    };

    if (radioGroup) {
      radioGroup.addEventListener("change", handleRadioChange);
    }

    // Handle submit button state on valueSelected change
    const updateSubmitState = () => {
      if (valueSelected) {
        submitButton.removeAttribute("disabled");
      } else {
        submitButton.setAttribute("disabled", "");
      }
    };

    updateSubmitState();

    return () => {
      if (radioGroup) {
        radioGroup.removeEventListener("change", handleRadioChange);
      }
    };
  }, [valueSelected, selectedReason]);

  return (
    <dialog
      className="report-content-form"
      id="dialog-report"
      ref={modal}
      onClose={() => dispatch({ type: at.REPORT_CLOSE })}
    >
      <form action="">
        {/* spocs and stories are going to have different reporting
          options, so placed a conditional to render the different reasons */}
        {report.card_type === "spoc" ? (
          <>
            <moz-radio-group
              name="report"
              ref={radioGroupRef}
              id="report-group"
              data-l10n-id="newtab-report-ads-why-reporting"
              className="report-ads-options"
              headingLevel="3"
            >
              {adReportReasons.map(({ l10nId, value }) => (
                <moz-radio
                  key={value}
                  data-l10n-id={l10nId}
                  value={value}
                ></moz-radio>
              ))}
            </moz-radio-group>
          </>
        ) : (
          <>
            <moz-radio-group
              name="report"
              ref={radioGroupRef}
              id="report-group"
              data-l10n-id="newtab-report-content-why-reporting-this"
              className="report-content-options"
              headingLevel="3"
            >
              <moz-radio
                data-l10n-id="newtab-report-content-wrong-category"
                value="wrong_category"
              ></moz-radio>
              <moz-radio
                data-l10n-id="newtab-report-content-outdated"
                value="outdated"
              ></moz-radio>
              <moz-radio
                data-l10n-id="newtab-report-content-inappropriate-offensive"
                value="inappropriate_or_offensive"
              ></moz-radio>
              <moz-radio
                data-l10n-id="newtab-report-content-spam-misleading"
                value="spam_or_misleading"
              ></moz-radio>
              <moz-radio
                data-l10n-id="newtab-report-content-requires-payment-subscription"
                value="requires_payment_or_subscription"
              >
                <a
                  slot="support-link"
                  is="moz-support-link"
                  support-page="recommendations-firefox-new-tab#w_what-is-a-paywall"
                  data-l10n-id="newtab-report-content-requires-payment-subscription-learn-more"
                  rel="noreferrer"
                  target="_blank"
                ></a>
              </moz-radio>
            </moz-radio-group>
          </>
        )}

        <moz-button-group>
          <moz-button
            data-l10n-id="newtab-report-cancel"
            onClick={handleCancel}
            className="cancel-report-btn"
          ></moz-button>

          <moz-button
            type="primary"
            data-l10n-id="newtab-report-submit"
            ref={submitButtonRef}
            onClick={handleSubmit}
            className="submit-report-btn"
          ></moz-button>
        </moz-button-group>
      </form>
    </dialog>
  );
};
