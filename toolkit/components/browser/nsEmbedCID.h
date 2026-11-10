/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSEMBEDCID_H
#define NSEMBEDCID_H

/**
 * @file
 * @brief List of, and documentation for, frozen Gecko embedding contracts.
 */

/**
 * Prompt Service ContractID
 *   The prompt service (which can be gotten by calling getServiceByContractID
 *   on this ContractID) is the way to pose various prompts, alerts,
 *   and confirmation dialogs to the user.
 *
 * This contract implements the following interfaces:
 * nsIPromptService
 *
 * Embedders may override this ContractID with their own implementation if they
 * want more control over the way prompts, alerts, and confirmation dialogs are
 * presented to the user.
 */
#define NS_PROMPTSERVICE_CONTRACTID "@mozilla.org/prompter;1"

#endif  // NSEMBEDCID_H
