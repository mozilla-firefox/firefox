/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { actionCreators as ac, actionTypes } from "common/Actions.mjs";
import { connect } from "react-redux";
import React from "react";

/**
 * ConfirmDialog component.
 * One primary action button, one cancel button.
 *
 * Content displayed is controlled by `data` prop the component receives.
 * Example:
 * data: {
 *   // Any sort of data needed to be passed around by actions.
 *   payload: site.url,
 *   // Primary button AlsoToMain action.
 *   action: "DELETE_HISTORY_URL",
 *   // Primary button USerEvent action.
 *   userEvent: "DELETE",
 *   // Array of locale ids to display.
 *   message_body: ["confirm_history_delete_p1", "confirm_history_delete_notice_p2"],
 *   // Text for primary button.
 *   confirm_button_string_id: "menu_action_delete",
 *   // moz-button type for the primary button. Defaults to "destructive".
 *   confirm_button_type: "primary"
 * },
 */
// The first line names the dialog, the second describes it. Anything after
// that is read as part of the dialog rather than announced up front.
const CONFIRMATION_DIALOG_IDS = [
  "confirmation-dialog-title",
  "confirmation-dialog-description",
];

export class _ConfirmDialog extends React.PureComponent {
  constructor(props) {
    super(props);
    this._handleCancelBtn = this._handleCancelBtn.bind(this);
    this._handleConfirmBtn = this._handleConfirmBtn.bind(this);
    this.dialogRef = React.createRef();
  }

  componentDidUpdate() {
    const dialogElement = this.dialogRef.current;
    if (!dialogElement) {
      return;
    }

    // Open dialog when visible becomes true
    if (this.props.visible && !dialogElement.open) {
      dialogElement.showModal();
    }
    // Close dialog when visible becomes false
    else if (!this.props.visible && dialogElement.open) {
      dialogElement.close();
    }
  }

  _handleCancelBtn() {
    this.props.dispatch({ type: actionTypes.DIALOG_CANCEL });
    this.props.dispatch(
      ac.UserEvent({
        event: actionTypes.DIALOG_CANCEL,
        source: this.props.data.eventSource,
      })
    );
  }

  _handleConfirmBtn() {
    this.props.data.onConfirm.forEach(this.props.dispatch);
  }

  _renderModalMessage() {
    const message_body = this.props.data.body_string_id;

    if (!message_body) {
      return null;
    }

    return (
      <span>
        {message_body.map((msg, index) => (
          <p key={msg} data-l10n-id={msg} id={CONFIRMATION_DIALOG_IDS[index]} />
        ))}
      </span>
    );
  }

  render() {
    return (
      <dialog
        ref={this.dialogRef}
        className="confirmation-dialog"
        aria-modal="true"
        aria-labelledby="confirmation-dialog-title"
        aria-describedby={
          this.props.data.body_string_id?.length > 1
            ? "confirmation-dialog-description"
            : undefined
        }
        onClick={e => {
          // Close modal when clicking on the backdrop pseudo element (the background of the modal)
          if (e.target === this.dialogRef.current) {
            this._handleCancelBtn();
          }
        }}
      >
        <div className="modal">
          <section className="modal-message">
            {this.props.data.icon && (
              <span
                className={`icon icon-spacer icon-${this.props.data.icon}`}
              />
            )}
            {this._renderModalMessage()}
          </section>
          <section className="button-group">
            <moz-button-group>
              <moz-button
                type="ghost"
                onClick={this._handleCancelBtn}
                data-l10n-id={this.props.data.cancel_button_string_id}
              ></moz-button>
              <moz-button
                type={this.props.data.confirm_button_type || "destructive"}
                onClick={this._handleConfirmBtn}
                data-l10n-id={this.props.data.confirm_button_string_id}
                data-l10n-args={JSON.stringify(
                  this.props.data.confirm_button_string_args
                )}
              ></moz-button>
            </moz-button-group>
          </section>
        </div>
      </dialog>
    );
  }
}

export const ConfirmDialog = connect(state => state.Dialog)(_ConfirmDialog);
