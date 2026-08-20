/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TOOLKIT_SYSTEM_GNOME_RUST_SRC_PORTALALERTSSERVICE_H_
#define TOOLKIT_SYSTEM_GNOME_RUST_SRC_PORTALALERTSSERVICE_H_

#include "ErrorList.h"
#include "nsID.h"

extern "C" {
nsresult new_portal_alerts_service(REFNSIID aIID, void** aResult);
};

#endif  // TOOLKIT_SYSTEM_GNOME_RUST_SRC_PORTALALERTSSERVICE_H_
