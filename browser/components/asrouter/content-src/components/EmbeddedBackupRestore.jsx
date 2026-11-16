/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useRef, useEffect, useState, useCallback } from "react";
import { MultiStageUtils } from "../lib/multistage-utils.mjs";
import { Localized } from "./MSLocalized";

export const EmbeddedBackupRestore = ({ handleAction, skipButton, layout }) => {
  const [recoveryInProgress, setRecoveryInProgress] = useState(false);
  const ref = useRef(null);

  useEffect(() => {
    const loadRestore = async () => {
      await window.AWFindBackupsInWellKnownLocations?.();
    };
    loadRestore();
    // Clear the pref used to target the restore screen so that users will not
    // automatically see it again the next time they visit about:welcome.
    MultiStageUtils.handleUserAction({
      type: "SET_PREF",
      data: {
        pref: { name: "showRestoreFromBackup", value: false },
      },
    });
  }, []);

  const onRecoveryProgressChange = useCallback(e => {
    setRecoveryInProgress(e.detail.recoveryInProgress);
  }, []);

  useEffect(() => {
    const backupRef = ref.current;

    if (backupRef.backupServiceState) {
      setRecoveryInProgress(backupRef.backupServiceState.recoveryInProgress);
    }

    backupRef.addEventListener(
      "BackupUI:RecoveryProgress",
      onRecoveryProgressChange
    );

    return () => {
      backupRef.removeEventListener(
        "BackupUI:RecoveryProgress",
        onRecoveryProgressChange
      );
    };
  }, [onRecoveryProgressChange]);

  return (
    <div className="embedded-backup-restore-container">
      <restore-from-backup
        aboutWelcomeEmbedded="true"
        labelFontWeight="600"
        layout={layout}
        ref={ref}
      />
      {skipButton ? (
        <div className="action-buttons">
          <div className="secondary-cta">
            <Localized text={skipButton.label}>
              <button
                id="secondary_button"
                className={
                  skipButton?.has_arrow_icon
                    ? "secondary arrow-icon"
                    : "secondary"
                }
                value="skip_button"
                disabled={recoveryInProgress}
                aria-busy={recoveryInProgress || undefined}
                onClick={handleAction}
              />
            </Localized>
          </div>
        </div>
      ) : null}
    </div>
  );
};
