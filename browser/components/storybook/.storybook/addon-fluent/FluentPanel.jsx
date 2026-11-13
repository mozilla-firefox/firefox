/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useEffect, useState } from "react";
import { addons, useGlobals, useStorybookApi } from "storybook/manager-api";
// eslint-disable-next-line no-unused-vars
import { AddonPanel, Table, Form } from "storybook/internal/components";
import { FLUENT_CHANGED, FLUENT_SET_STRINGS } from "./constants.mjs";
// eslint-disable-next-line import-x/no-unassigned-import
import "./fluent-panel.css";

export const FluentPanel = ({ active }) => {
  const [fileName, setFileName] = useState(null);
  const [strings, setStrings] = useState([]);
  const [{ fluentStrings }, updateGlobals] = useGlobals();
  const channel = addons.getChannel();
  const api = useStorybookApi();

  useEffect(() => {
    channel.on(FLUENT_CHANGED, handleFluentChanged);
    return () => {
      channel.off(FLUENT_CHANGED, handleFluentChanged);
    };
  }, [channel]);

  const handleFluentChanged = (nextStrings, fluentFile) => {
    setFileName(fluentFile);
    setStrings(nextStrings);
  };

  const onInput = e => {
    let nextStrings = [];
    for (let [key, value] of strings) {
      if (key == e.target.name) {
        let stringValue = e.target.value;
        if (stringValue.startsWith(".")) {
          stringValue = "\n" + stringValue;
        }
        nextStrings.push([key, stringValue]);
      } else {
        nextStrings.push([key, value]);
      }
    }
    let stringified = nextStrings
      .map(([key, value]) => `${key} = ${value}`)
      .join("\n");
    channel.emit(FLUENT_SET_STRINGS, stringified);
    updateGlobals({
      fluentStrings: { ...fluentStrings, [fileName]: nextStrings },
    });
    return { fileName, strings };
  };

  const addonTemplate = () => {
    if (strings.length === 0) {
      return (
        <AddonPanel active={!!active} api={api}>
          <div className="addon-panel-body">
            <div className="addon-panel-message">
              This story is not configured to use Fluent.
            </div>
          </div>
        </AddonPanel>
      );
    }

    return (
      <AddonPanel active={!!active} api={api}>
        <div className="addon-panel-body">
          <Table aria-hidden="false" className="addon-panel-table">
            <thead className="addon-panel-table-head">
              <tr>
                <th>
                  <span>Identifier</span>
                </th>
                <th>
                  <span>String</span>
                </th>
              </tr>
            </thead>
            <tbody className="addon-panel-table-body">
              {strings.map(([identifier, value]) => (
                <tr key={identifier}>
                  <td>
                    <span>{identifier}</span>
                  </td>
                  <td>
                    <Form.Textarea
                      name={identifier}
                      onInput={onInput}
                      defaultValue={value
                        .trim()
                        .split("\n")
                        .map(s => s.trim())
                        .join("\n")}
                    ></Form.Textarea>
                  </td>
                </tr>
              ))}
            </tbody>
          </Table>
        </div>
      </AddonPanel>
    );
  };

  return addonTemplate();
};
