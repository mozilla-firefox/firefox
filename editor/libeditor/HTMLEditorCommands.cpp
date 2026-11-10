/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/EditorCommands.h"

#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc
#include "mozilla/EditorBase.h"  // for EditorBase
#include "mozilla/ErrorResult.h"
#include "mozilla/HTMLEditor.h"  // for HTMLEditor
#include "mozilla/dom/Element.h"
#include "nsAString.h"
#include "nsAtom.h"                   // for nsAtom, nsStaticAtom, etc
#include "nsCommandParams.h"          // for nsCommandParams, etc
#include "nsComponentManagerUtils.h"  // for do_CreateInstance
#include "nsGkAtoms.h"                // for nsGkAtoms, nsGkAtoms::font, etc
#include "nsIClipboard.h"             // for nsIClipboard, etc
#include "nsIEditingSession.h"
#include "nsIPrincipal.h"     // for nsIPrincipal
#include "nsLiteralString.h"  // for NS_LITERAL_STRING
#include "nsReadableUtils.h"  // for EmptyString
#include "nsString.h"         // for nsAutoString, nsString, etc
#include "nsStringFwd.h"      // for nsString

class nsISupports;

namespace mozilla {
using dom::Element;

// prototype
static nsresult GetListState(HTMLEditor* aHTMLEditor, bool* aMixed,
                             nsAString& aLocalName);

// defines
#define STATE_ENABLED "state_enabled"
#define STATE_ALL "state_all"
#define STATE_ANY "state_any"
#define STATE_MIXED "state_mixed"
#define STATE_BEGIN "state_begin"
#define STATE_END "state_end"
#define STATE_ATTRIBUTE "state_attribute"
#define STATE_DATA "state_data"

/*****************************************************************************
 * mozilla::StateUpdatingCommandBase
 *****************************************************************************/

bool StateUpdatingCommandBase::IsCommandEnabled(Command aCommand,
                                                EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  if (!htmlEditor->IsModifiable() || !htmlEditor->IsSelectionEditable() ||
      !htmlEditor->IsStyleEditable()) {
    return false;
  }
  if (aCommand == Command::FormatAbsolutePosition) {
    return htmlEditor->IsAbsolutePositionEditorEnabled();
  }
  return true;
}

nsresult StateUpdatingCommandBase::DoCommand(Command aCommand,
                                             EditorBase& aEditorBase,
                                             nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsStaticAtom* tagName = GetTagName(aCommand);
  if (NS_WARN_IF(!tagName)) {
    return NS_ERROR_UNEXPECTED;
  }
  nsresult rv = ToggleState(MOZ_KnownLive(*tagName), MOZ_KnownLive(*htmlEditor),
                            aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "StateUpdatingCommandBase::ToggleState() failed");
  return rv;
}

nsresult StateUpdatingCommandBase::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  if (!aEditorBase) {
    return NS_OK;
  }
  HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsStaticAtom* tagName = GetTagName(aCommand);
  if (NS_WARN_IF(!tagName)) {
    return NS_ERROR_UNEXPECTED;
  }
  // MOZ_KnownLive(htmlEditor) because the lifetime of aEditorBase is guaranteed
  // by the callers.
  // MOZ_KnownLive(tagName) because nsStaticAtom instances are alive until
  // shutting down.
  nsresult rv = GetCurrentState(MOZ_KnownLive(*tagName),
                                MOZ_KnownLive(*htmlEditor), aParams);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "StateUpdatingCommandBase::GetCurrentState() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::StyleUpdatingCommand
 *****************************************************************************/

StaticRefPtr<StyleUpdatingCommand> StyleUpdatingCommand::sInstance;

nsresult StyleUpdatingCommand::GetCurrentState(nsStaticAtom& aTagName,
                                               HTMLEditor& aHTMLEditor,
                                               nsCommandParams& aParams) const {
  bool firstOfSelectionHasProp = false;
  bool anyOfSelectionHasProp = false;
  bool allOfSelectionHasProp = false;

  nsresult rv = aHTMLEditor.GetInlineProperty(
      aTagName, nullptr, u""_ns, &firstOfSelectionHasProp,
      &anyOfSelectionHasProp, &allOfSelectionHasProp);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetInlineProperty() failed");

  aParams.SetBool(STATE_ENABLED, NS_SUCCEEDED(rv));
  aParams.SetBool(STATE_ALL, allOfSelectionHasProp);
  aParams.SetBool(STATE_ANY, anyOfSelectionHasProp);
  aParams.SetBool(STATE_MIXED, anyOfSelectionHasProp && !allOfSelectionHasProp);
  aParams.SetBool(STATE_BEGIN, firstOfSelectionHasProp);
  aParams.SetBool(STATE_END, allOfSelectionHasProp);  // not completely accurate
  return NS_OK;
}

nsresult StyleUpdatingCommand::ToggleState(nsStaticAtom& aTagName,
                                           HTMLEditor& aHTMLEditor,
                                           nsIPrincipal* aPrincipal) const {
  RefPtr params = MakeRefPtr<nsCommandParams>();

  // tags "href" and "name" are special cases in the core editor
  // they are used to remove named anchor/link and shouldn't be used for
  // insertion
  bool doTagRemoval;
  if (&aTagName == nsGkAtoms::href || &aTagName == nsGkAtoms::name) {
    doTagRemoval = true;
  } else {
    // check current selection; set doTagRemoval if formatting should be removed
    nsresult rv = GetCurrentState(aTagName, aHTMLEditor, *params);
    if (NS_FAILED(rv)) {
      NS_WARNING("StyleUpdatingCommand::GetCurrentState() failed");
      return rv;
    }
    ErrorResult error;
    doTagRemoval = params->GetBool(STATE_ALL, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
  }

  if (doTagRemoval) {
    nsresult rv =
        aHTMLEditor.RemoveInlinePropertyAsAction(aTagName, nullptr, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveInlinePropertyAsAction() failed");
    return rv;
  }

  nsresult rv = aHTMLEditor.SetInlinePropertyAsAction(aTagName, nullptr, u""_ns,
                                                      aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertyAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::ListCommand
 *****************************************************************************/

StaticRefPtr<ListCommand> ListCommand::sInstance;

nsresult ListCommand::GetCurrentState(nsStaticAtom& aTagName,
                                      HTMLEditor& aHTMLEditor,
                                      nsCommandParams& aParams) const {
  bool bMixed;
  nsAutoString localName;
  nsresult rv = GetListState(&aHTMLEditor, &bMixed, localName);
  if (NS_FAILED(rv)) {
    NS_WARNING("GetListState() failed");
    return rv;
  }

  bool inList = aTagName.Equals(localName);
  aParams.SetBool(STATE_ALL, !bMixed && inList);
  aParams.SetBool(STATE_MIXED, bMixed);
  aParams.SetBool(STATE_ENABLED, true);
  return NS_OK;
}

nsresult ListCommand::ToggleState(nsStaticAtom& aTagName,
                                  HTMLEditor& aHTMLEditor,
                                  nsIPrincipal* aPrincipal) const {
  RefPtr params = MakeRefPtr<nsCommandParams>();
  nsresult rv = GetCurrentState(aTagName, aHTMLEditor, *params);
  if (NS_FAILED(rv)) {
    NS_WARNING("ListCommand::GetCurrentState() failed");
    return rv;
  }

  ErrorResult error;
  bool inList = params->GetBool(STATE_ALL, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  nsDependentAtomString listType(&aTagName);
  if (inList) {
    nsresult rv = aHTMLEditor.RemoveListAsAction(listType, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveListAsAction() failed");
    return rv;
  }

  rv = aHTMLEditor.MakeOrChangeListAsAction(
      aTagName, u""_ns, HTMLEditor::SelectAllOfCurrentList::No, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::MakeOrChangeListAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::ListItemCommand
 *****************************************************************************/

StaticRefPtr<ListItemCommand> ListItemCommand::sInstance;

nsresult ListItemCommand::GetCurrentState(nsStaticAtom& aTagName,
                                          HTMLEditor& aHTMLEditor,
                                          nsCommandParams& aParams) const {
  ErrorResult error;
  ListItemElementSelectionState state(aHTMLEditor, error);
  if (error.Failed()) {
    NS_WARNING("ListItemElementSelectionState failed");
    return error.StealNSResult();
  }

  if (state.IsNotOneTypeDefinitionListItemElementSelected()) {
    aParams.SetBool(STATE_ALL, false);
    aParams.SetBool(STATE_MIXED, true);
    return NS_OK;
  }

  nsStaticAtom* selectedListItemTagName = nullptr;
  if (state.IsLIElementSelected()) {
    selectedListItemTagName = nsGkAtoms::li;
  } else if (state.IsDTElementSelected()) {
    selectedListItemTagName = nsGkAtoms::dt;
  } else if (state.IsDDElementSelected()) {
    selectedListItemTagName = nsGkAtoms::dd;
  }
  aParams.SetBool(STATE_ALL, &aTagName == selectedListItemTagName);
  aParams.SetBool(STATE_MIXED, false);
  return NS_OK;
}

nsresult ListItemCommand::ToggleState(nsStaticAtom& aTagName,
                                      HTMLEditor& aHTMLEditor,
                                      nsIPrincipal* aPrincipal) const {
  // Need to use aTagName????
  RefPtr params = MakeRefPtr<nsCommandParams>();
  GetCurrentState(aTagName, aHTMLEditor, *params);
  ErrorResult error;
  bool inList = params->GetBool(STATE_ALL, error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  if (inList) {
    // To remove a list, first get what kind of list we're in
    bool bMixed;
    nsAutoString localName;
    nsresult rv = GetListState(&aHTMLEditor, &bMixed, localName);
    if (NS_FAILED(rv)) {
      NS_WARNING("GetListState() failed");
      return rv;
    }
    if (localName.IsEmpty() || bMixed) {
      return NS_OK;
    }
    rv = aHTMLEditor.RemoveListAsAction(localName, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveListAsAction() failed");
    return rv;
  }

  // Set to the requested paragraph type
  // XXX Note: This actually doesn't work for "LI",
  //    but we currently don't use this for non DL lists anyway.
  // Problem: won't this replace any current block paragraph style?
  nsresult rv = aHTMLEditor.SetParagraphStateAsAction(
      nsDependentAtomString(&aTagName), aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetParagraphFormatAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::RemoveListCommand
 *****************************************************************************/

StaticRefPtr<RemoveListCommand> RemoveListCommand::sInstance;

bool RemoveListCommand::IsCommandEnabled(Command /*aCommand*/,
                                         EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  if (!htmlEditor->IsModifiable() || !htmlEditor->IsSelectionEditable() ||
      !htmlEditor->IsStyleEditable()) {
    return false;
  }

  // It is enabled if we are in any list type
  bool bMixed;
  nsAutoString localName;
  nsresult rv = GetListState(MOZ_KnownLive(htmlEditor), &bMixed, localName);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "GetListState() failed");
  return NS_SUCCEEDED(rv) && (bMixed || !localName.IsEmpty());
}

nsresult RemoveListCommand::DoCommand(Command /*aCommand*/,
                                      EditorBase& aEditorBase,
                                      nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  // This removes any list type
  nsresult rv =
      MOZ_KnownLive(htmlEditor)->RemoveListAsAction(u""_ns, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::RemoveListAsAction() failed");
  return rv;
}

nsresult RemoveListCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::IndentCommand
 *****************************************************************************/

StaticRefPtr<IndentCommand> IndentCommand::sInstance;

bool IndentCommand::IsCommandEnabled(Command /*aCommand*/,
                                     EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult IndentCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                                  nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->IndentAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::IndentAsAction() failed");
  return rv;
}

nsresult IndentCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::OutdentCommand
 *****************************************************************************/

StaticRefPtr<OutdentCommand> OutdentCommand::sInstance;

bool OutdentCommand::IsCommandEnabled(Command /*aCommand*/,
                                      EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult OutdentCommand::DoCommand(Command /*aCommand*/,
                                   EditorBase& aEditorBase,
                                   nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->OutdentAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::OutdentAsAction() failed");
  return rv;
}

nsresult OutdentCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::MultiStateCommandBase
 *****************************************************************************/

bool MultiStateCommandBase::IsCommandEnabled(Command /*aCommand*/,
                                             EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  // should be disabled sometimes, like if the current selection is an image
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult MultiStateCommandBase::DoCommand(Command /*aCommand*/,
                                          EditorBase& /*aEditorBase*/,
                                          nsIPrincipal* /*aPrincipal*/) const {
  NS_WARNING(
      "who is calling MultiStateCommandBase::DoCommand (no implementation)?");
  return NS_OK;
}

nsresult MultiStateCommandBase::DoCommandParam(Command aCommand,
                                               const nsAString& aStringParam,
                                               EditorBase& aEditorBase,
                                               nsIPrincipal* aPrincipal) const {
  NS_WARNING_ASSERTION(aCommand != Command::FormatJustify,
                       "Command::FormatJustify should be used only for "
                       "IsCommandEnabled() and GetCommandStateParams()");
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = SetState(MOZ_KnownLive(htmlEditor), aStringParam, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "MultiStateCommandBase::SetState() failed");
  return rv;
}

nsresult MultiStateCommandBase::GetCommandStateParams(
    Command /*aCommand*/, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  if (!aEditorBase) {
    return NS_OK;
  }
  HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = GetCurrentState(MOZ_KnownLive(htmlEditor), aParams);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "MultiStateCommandBase::GetCurrentState() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::FormatBlockStateCommand
 *****************************************************************************/

StaticRefPtr<FormatBlockStateCommand> FormatBlockStateCommand::sInstance;

nsresult FormatBlockStateCommand::GetCurrentState(
    HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult error;
  ParagraphStateAtSelection state(
      *aHTMLEditor,
      ParagraphStateAtSelection::FormatBlockMode::HTMLFormatBlockCommand,
      error);
  if (error.Failed()) {
    NS_WARNING("ParagraphStateAtSelection failed");
    return error.StealNSResult();
  }
  aParams.SetBool(STATE_MIXED, state.IsMixed());
  if (NS_WARN_IF(!state.GetFirstParagraphStateAtSelection())) {
    aParams.SetCString(STATE_ATTRIBUTE, ""_ns);
  } else {
    nsCString paragraphState;  // Don't use `nsAutoCString` for avoiding copy.
    state.GetFirstParagraphStateAtSelection()->ToUTF8String(paragraphState);
    aParams.SetCString(STATE_ATTRIBUTE, paragraphState);
  }
  return NS_OK;
}

nsresult FormatBlockStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                           const nsAString& aNewState,
                                           nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor) || NS_WARN_IF(aNewState.IsEmpty())) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = aHTMLEditor->FormatBlockAsAction(aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::FormatBlockAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::ParagraphStateCommand
 *****************************************************************************/

StaticRefPtr<ParagraphStateCommand> ParagraphStateCommand::sInstance;

nsresult ParagraphStateCommand::GetCurrentState(
    HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult error;
  ParagraphStateAtSelection state(
      *aHTMLEditor,
      ParagraphStateAtSelection::FormatBlockMode::XULParagraphStateCommand,
      error);
  if (error.Failed()) {
    NS_WARNING("ParagraphStateAtSelection failed");
    return error.StealNSResult();
  }
  aParams.SetBool(STATE_MIXED, state.IsMixed());
  if (NS_WARN_IF(!state.GetFirstParagraphStateAtSelection())) {
    // XXX This is odd behavior, we should fix this later.
    aParams.SetCString(STATE_ATTRIBUTE, "x"_ns);
  } else {
    nsCString paragraphState;  // Don't use `nsAutoCString` for avoiding copy.
    state.GetFirstParagraphStateAtSelection()->ToUTF8String(paragraphState);
    aParams.SetCString(STATE_ATTRIBUTE, paragraphState);
  }
  return NS_OK;
}

nsresult ParagraphStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                         const nsAString& aNewState,
                                         nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = aHTMLEditor->SetParagraphStateAsAction(aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetParagraphStateAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::FontFaceStateCommand
 *****************************************************************************/

StaticRefPtr<FontFaceStateCommand> FontFaceStateCommand::sInstance;

nsresult FontFaceStateCommand::GetCurrentState(HTMLEditor* aHTMLEditor,
                                               nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoString outStateString;
  bool outMixed;
  nsresult rv = aHTMLEditor->GetFontFaceState(&outMixed, outStateString);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetFontFaceState() failed");
    return rv;
  }
  aParams.SetBool(STATE_MIXED, outMixed);
  aParams.SetCString(STATE_ATTRIBUTE, NS_ConvertUTF16toUTF8(outStateString));
  return NS_OK;
}

nsresult FontFaceStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                        const nsAString& aNewState,
                                        nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNewState.IsEmpty() || aNewState.EqualsLiteral("normal")) {
    nsresult rv = aHTMLEditor->RemoveInlinePropertyAsAction(
        *nsGkAtoms::font, nsGkAtoms::face, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveInlinePropertyAsAction(nsGkAtoms::"
                         "font, nsGkAtoms::face) failed");
    return rv;
  }

  nsresult rv = aHTMLEditor->SetInlinePropertyAsAction(
      *nsGkAtoms::font, nsGkAtoms::face, aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertyAsAction(nsGkAtoms::font, "
                       "nsGkAtoms::face) failed");
  return rv;
}

/*****************************************************************************
 * mozilla::FontSizeStateCommand
 *****************************************************************************/

StaticRefPtr<FontSizeStateCommand> FontSizeStateCommand::sInstance;

nsresult FontSizeStateCommand::GetCurrentState(HTMLEditor* aHTMLEditor,
                                               nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoString outStateString;
  bool firstHas, anyHas, allHas;
  nsresult rv = aHTMLEditor->GetInlinePropertyWithAttrValue(
      *nsGkAtoms::font, nsGkAtoms::size, u""_ns, &firstHas, &anyHas, &allHas,
      outStateString);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::GetInlinePropertyWithAttrValue(nsGkAtoms::font, "
        "nsGkAtoms::size) failed");
    return rv;
  }

  nsAutoCString tOutStateString;
  LossyCopyUTF16toASCII(outStateString, tOutStateString);
  aParams.SetBool(STATE_MIXED, anyHas && !allHas);
  aParams.SetCString(STATE_ATTRIBUTE, tOutStateString);
  aParams.SetBool(STATE_ENABLED, true);

  return NS_OK;
}

// acceptable values for "aNewState" are:
//   -2
//   -1
//    0
//   +1
//   +2
//   +3
//   medium
//   normal
nsresult FontSizeStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                        const nsAString& aNewState,
                                        nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aNewState.IsEmpty() && !aNewState.EqualsLiteral("normal") &&
      !aNewState.EqualsLiteral("medium")) {
    nsresult rv = aHTMLEditor->SetInlinePropertyAsAction(
        *nsGkAtoms::font, nsGkAtoms::size, aNewState, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::SetInlinePropertyAsAction(nsGkAtoms::"
                         "font, nsGkAtoms::size) failed");
    return rv;
  }

  // remove any existing font size, big or small
  nsresult rv = aHTMLEditor->RemoveInlinePropertyAsAction(
      *nsGkAtoms::font, nsGkAtoms::size, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::RemoveInlinePropertyAsAction(nsGkAtoms::"
                       "font, nsGkAtoms::size) failed");
  return rv;
}

/*****************************************************************************
 * mozilla::FontColorStateCommand
 *****************************************************************************/

StaticRefPtr<FontColorStateCommand> FontColorStateCommand::sInstance;

nsresult FontColorStateCommand::GetCurrentState(
    HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  bool outMixed;
  nsAutoString outStateString;
  nsresult rv = aHTMLEditor->GetFontColorState(&outMixed, outStateString);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetFontColorState() failed");
    return rv;
  }

  nsAutoCString tOutStateString;
  LossyCopyUTF16toASCII(outStateString, tOutStateString);
  aParams.SetBool(STATE_MIXED, outMixed);
  aParams.SetCString(STATE_ATTRIBUTE, tOutStateString);
  return NS_OK;
}

nsresult FontColorStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                         const nsAString& aNewState,
                                         nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNewState.IsEmpty() || aNewState.EqualsLiteral("normal")) {
    nsresult rv = aHTMLEditor->RemoveInlinePropertyAsAction(
        *nsGkAtoms::font, nsGkAtoms::color, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveInlinePropertyAsAction(nsGkAtoms::"
                         "font, nsGkAtoms::color) failed");
    return rv;
  }

  nsresult rv = aHTMLEditor->SetInlinePropertyAsAction(
      *nsGkAtoms::font, nsGkAtoms::color, aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertyAsAction(nsGkAtoms::font, "
                       "nsGkAtoms::color) failed");
  return rv;
}

/*****************************************************************************
 * mozilla::HighlightColorStateCommand
 *****************************************************************************/

StaticRefPtr<HighlightColorStateCommand> HighlightColorStateCommand::sInstance;

nsresult HighlightColorStateCommand::GetCurrentState(
    HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  bool outMixed;
  nsAutoString outStateString;
  nsresult rv = aHTMLEditor->GetHighlightColorState(&outMixed, outStateString);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetHighlightColorState() failed");
    return rv;
  }

  nsAutoCString tOutStateString;
  LossyCopyUTF16toASCII(outStateString, tOutStateString);
  aParams.SetBool(STATE_MIXED, outMixed);
  aParams.SetCString(STATE_ATTRIBUTE, tOutStateString);
  return NS_OK;
}

nsresult HighlightColorStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                              const nsAString& aNewState,
                                              nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNewState.IsEmpty() || aNewState.EqualsLiteral("normal")) {
    nsresult rv = aHTMLEditor->RemoveInlinePropertyAsAction(
        *nsGkAtoms::font, nsGkAtoms::bgcolor, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "HTMLEditor::RemoveInlinePropertyAsAction(nsGkAtoms::"
                         "font, nsGkAtoms::bgcolor) failed");
    return rv;
  }

  nsresult rv = aHTMLEditor->SetInlinePropertyAsAction(
      *nsGkAtoms::font, nsGkAtoms::bgcolor, aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertyAsAction(nsGkAtoms::font, "
                       "nsGkAtoms::bgcolor) failed");
  return rv;
}

/*****************************************************************************
 * mozilla::BackgroundColorStateCommand
 *****************************************************************************/

StaticRefPtr<BackgroundColorStateCommand>
    BackgroundColorStateCommand::sInstance;

nsresult BackgroundColorStateCommand::GetCurrentState(
    HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  bool outMixed;
  nsAutoString outStateString;
  nsresult rv = aHTMLEditor->GetBackgroundColorState(&outMixed, outStateString);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetBackgroundColorState() failed");
    return rv;
  }

  nsAutoCString tOutStateString;
  LossyCopyUTF16toASCII(outStateString, tOutStateString);
  aParams.SetBool(STATE_MIXED, outMixed);
  aParams.SetCString(STATE_ATTRIBUTE, tOutStateString);
  return NS_OK;
}

nsresult BackgroundColorStateCommand::SetState(HTMLEditor* aHTMLEditor,
                                               const nsAString& aNewState,
                                               nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = aHTMLEditor->SetBackgroundColorAsAction(aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetBackgroundColorAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::AlignCommand
 *****************************************************************************/

StaticRefPtr<AlignCommand> AlignCommand::sInstance;

nsresult AlignCommand::GetCurrentState(HTMLEditor* aHTMLEditor,
                                       nsCommandParams& aParams) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }

  ErrorResult error;
  AlignStateAtSelection state(*aHTMLEditor, error);
  if (error.Failed()) {
    if (!state.IsSelectionRangesFound()) {
      // If there was no selection ranges, we shouldn't throw exception for
      // compatibility with the other browsers, but I have no better idea
      // than returning empty string in this case.  Oddly, Blink/WebKit returns
      // "true" or "false", but it's different from us and the value does not
      // make sense.  Additionally, WPT loves our behavior.
      error.SuppressException();
      aParams.SetBool(STATE_MIXED, false);
      aParams.SetCString(STATE_ATTRIBUTE, ""_ns);
      return NS_OK;
    }
    NS_WARNING("AlignStateAtSelection failed");
    return error.StealNSResult();
  }
  nsCString alignment;  // Don't use `nsAutoCString` to avoid copying string.
  switch (state.AlignmentAtSelectionStart()) {
    default:
    case nsIHTMLEditor::eLeft:
      alignment.AssignLiteral("left");
      break;
    case nsIHTMLEditor::eCenter:
      alignment.AssignLiteral("center");
      break;
    case nsIHTMLEditor::eRight:
      alignment.AssignLiteral("right");
      break;
    case nsIHTMLEditor::eJustify:
      alignment.AssignLiteral("justify");
      break;
  }
  aParams.SetBool(STATE_MIXED, false);
  aParams.SetCString(STATE_ATTRIBUTE, alignment);
  return NS_OK;
}

nsresult AlignCommand::SetState(HTMLEditor* aHTMLEditor,
                                const nsAString& aNewState,
                                nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aHTMLEditor)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = aHTMLEditor->AlignAsAction(aNewState, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "HTMLEditor::AlignAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::AbsolutePositioningCommand
 *****************************************************************************/

StaticRefPtr<AbsolutePositioningCommand> AbsolutePositioningCommand::sInstance;

nsresult AbsolutePositioningCommand::GetCurrentState(
    nsStaticAtom& /*aTagName*/, HTMLEditor& aHTMLEditor,
    nsCommandParams& aParams) const {
  if (!aHTMLEditor.IsAbsolutePositionEditorEnabled()) {
    aParams.SetBool(STATE_MIXED, false);
    aParams.SetCString(STATE_ATTRIBUTE, ""_ns);
    return NS_OK;
  }

  RefPtr<Element> container =
      aHTMLEditor.GetAbsolutelyPositionedSelectionContainer();
  aParams.SetBool(STATE_MIXED, false);
  aParams.SetCString(STATE_ATTRIBUTE, container ? "absolute"_ns : ""_ns);
  return NS_OK;
}

nsresult AbsolutePositioningCommand::ToggleState(
    nsStaticAtom& /*aTagName*/, HTMLEditor& aHTMLEditor,
    nsIPrincipal* aPrincipal) const {
  RefPtr<Element> container =
      aHTMLEditor.GetAbsolutelyPositionedSelectionContainer();
  nsresult rv = aHTMLEditor.SetSelectionToAbsoluteOrStaticAsAction(!container,
                                                                   aPrincipal);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::SetSelectionToAbsoluteOrStaticAsAction() failed");
  return rv;
}

/*****************************************************************************
 * mozilla::DecreaseZIndexCommand
 *****************************************************************************/

StaticRefPtr<DecreaseZIndexCommand> DecreaseZIndexCommand::sInstance;

bool DecreaseZIndexCommand::IsCommandEnabled(Command /*aCommand*/,
                                             EditorBase* aEditorBase) const {
  RefPtr<HTMLEditor> htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  if (!htmlEditor->IsAbsolutePositionEditorEnabled() ||
      !htmlEditor->IsStyleEditable()) {
    return false;
  }
  RefPtr<Element> positionedElement = htmlEditor->GetPositionedElement();
  if (!positionedElement) {
    return false;
  }
  return htmlEditor->GetZIndex(*positionedElement) > 0;
}

nsresult DecreaseZIndexCommand::DoCommand(Command /*aCommand*/,
                                          EditorBase& aEditorBase,
                                          nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->AddZIndexAsAction(-1, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::AddZIndexAsAction(-1) failed");
  return rv;
}

nsresult DecreaseZIndexCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::IncreaseZIndexCommand
 *****************************************************************************/

StaticRefPtr<IncreaseZIndexCommand> IncreaseZIndexCommand::sInstance;

bool IncreaseZIndexCommand::IsCommandEnabled(Command /*aCommand*/,
                                             EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  if (!htmlEditor->IsAbsolutePositionEditorEnabled() ||
      !htmlEditor->IsStyleEditable()) {
    return false;
  }
  return !!htmlEditor->GetPositionedElement();
}

nsresult IncreaseZIndexCommand::DoCommand(Command /*aCommand*/,
                                          EditorBase& aEditorBase,
                                          nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->AddZIndexAsAction(1, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::AddZIndexAsAction(1) failed");
  return rv;
}

nsresult IncreaseZIndexCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::RemoveStylesCommand
 *****************************************************************************/

StaticRefPtr<RemoveStylesCommand> RemoveStylesCommand::sInstance;

bool RemoveStylesCommand::IsCommandEnabled(Command /*aCommand*/,
                                           EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  // test if we have any styles?
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult RemoveStylesCommand::DoCommand(Command /*aCommand*/,
                                        EditorBase& aEditorBase,
                                        nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  nsresult rv =
      MOZ_KnownLive(htmlEditor)->RemoveAllInlinePropertiesAsAction(aPrincipal);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::RemoveAllInlinePropertiesAsAction() failed");
  return rv;
}

nsresult RemoveStylesCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::IncreaseFontSizeCommand
 *****************************************************************************/

StaticRefPtr<IncreaseFontSizeCommand> IncreaseFontSizeCommand::sInstance;

bool IncreaseFontSizeCommand::IsCommandEnabled(Command /*aCommand*/,
                                               EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  // test if we are at max size?
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult IncreaseFontSizeCommand::DoCommand(Command /*aCommand*/,
                                            EditorBase& aEditorBase,
                                            nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->IncreaseFontSizeAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::IncreaseFontSizeAsAction() failed");
  return rv;
}

nsresult IncreaseFontSizeCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::DecreaseFontSizeCommand
 *****************************************************************************/

StaticRefPtr<DecreaseFontSizeCommand> DecreaseFontSizeCommand::sInstance;

bool DecreaseFontSizeCommand::IsCommandEnabled(Command /*aCommand*/,
                                               EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  // test if we are at min size?
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

nsresult DecreaseFontSizeCommand::DoCommand(Command /*aCommand*/,
                                            EditorBase& aEditorBase,
                                            nsIPrincipal* aPrincipal) const {
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_OK;
  }
  nsresult rv = MOZ_KnownLive(htmlEditor)->DecreaseFontSizeAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::DecreaseFontSizeAsAction() failed");
  return rv;
}

nsresult DecreaseFontSizeCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::InsertHTMLCommand
 *****************************************************************************/

StaticRefPtr<InsertHTMLCommand> InsertHTMLCommand::sInstance;

bool InsertHTMLCommand::IsCommandEnabled(Command /*aCommand*/,
                                         EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor || htmlEditor->ComputeEditContext()) {
    return false;
  }
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable();
}

nsresult InsertHTMLCommand::DoCommand(Command /*aCommand*/,
                                      EditorBase& aEditorBase,
                                      nsIPrincipal* aPrincipal) const {
  // If InsertHTMLCommand is called with no parameters, it was probably called
  // with an empty string parameter ''. In this case, it should act the same as
  // the delete command
  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv =
      MOZ_KnownLive(htmlEditor)->InsertHTMLAsAction(u""_ns, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertHTMLAsAction() failed");
  return rv;
}

nsresult InsertHTMLCommand::DoCommandParam(Command /*aCommand*/,
                                           const nsAString& aStringParam,
                                           EditorBase& aEditorBase,
                                           nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(aStringParam.IsVoid())) {
    return NS_ERROR_INVALID_ARG;
  }

  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv =
      MOZ_KnownLive(htmlEditor)->InsertHTMLAsAction(aStringParam, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertHTMLAsAction() failed");
  return rv;
}

nsresult InsertHTMLCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::InsertTagCommand
 *****************************************************************************/

StaticRefPtr<InsertTagCommand> InsertTagCommand::sInstance;

bool InsertTagCommand::IsCommandEnabled(Command /*aCommand*/,
                                        EditorBase* aEditorBase) const {
  HTMLEditor* htmlEditor = HTMLEditor::GetFrom(aEditorBase);
  if (!htmlEditor) {
    return false;
  }
  return htmlEditor->IsModifiable() && htmlEditor->IsSelectionEditable() &&
         htmlEditor->IsStyleEditable();
}

// corresponding STATE_ATTRIBUTE is: src (img) and href (a)
nsresult InsertTagCommand::DoCommand(Command aCommand, EditorBase& aEditorBase,
                                     nsIPrincipal* aPrincipal) const {
  nsAtom* tagName = GetTagName(aCommand);
  if (NS_WARN_IF(tagName != nsGkAtoms::hr)) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  if (!htmlEditor->IsStyleEditable()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  RefPtr<Element> newElement =
      MOZ_KnownLive(htmlEditor)
          ->CreateElementWithDefaults(MOZ_KnownLive(*tagName));
  if (NS_WARN_IF(!newElement)) {
    return NS_ERROR_FAILURE;
  }
  HTMLEditor::InsertElementOptions options{
      HTMLEditor::InsertElementOption::DeleteSelection};
  // We did insert <img> without splitting ancestor inline elements, but the
  // other browsers split them.  Therefore, let's do it only when the document
  // is content.
  if (tagName == nsGkAtoms::img &&
      htmlEditor->GetDocument()->IsContentDocument()) {
    options += HTMLEditor::InsertElementOption::SplitAncestorInlineElements;
  }
  nsresult rv =
      MOZ_KnownLive(htmlEditor)
          ->InsertElementAtSelectionAsAction(newElement, options, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertElementAtSelectionAsAction() failed");
  return rv;
}

nsresult InsertTagCommand::DoCommandParam(Command aCommand,
                                          const nsAString& aStringParam,
                                          EditorBase& aEditorBase,
                                          nsIPrincipal* aPrincipal) const {
  MOZ_ASSERT(aCommand != Command::InsertHorizontalRule);

  if (NS_WARN_IF(aStringParam.IsEmpty())) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAtom* tagName = GetTagName(aCommand);
  if (NS_WARN_IF(!tagName)) {
    return NS_ERROR_UNEXPECTED;
  }

  HTMLEditor* htmlEditor = aEditorBase.GetAsHTMLEditor();
  if (NS_WARN_IF(!htmlEditor)) {
    return NS_ERROR_FAILURE;
  }
  if (!htmlEditor->IsStyleEditable()) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  // filter out tags we don't know how to insert
  nsAtom* attribute = nullptr;
  if (tagName == nsGkAtoms::a) {
    attribute = nsGkAtoms::href;
  } else if (tagName == nsGkAtoms::img) {
    attribute = nsGkAtoms::src;
  } else {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  RefPtr<Element> newElement =
      MOZ_KnownLive(htmlEditor)
          ->CreateElementWithDefaults(MOZ_KnownLive(*tagName));
  if (!newElement) {
    NS_WARNING("HTMLEditor::CreateElementWithDefaults() failed");
    return NS_ERROR_FAILURE;
  }

  ErrorResult error;
  newElement->SetAttr(attribute, aStringParam, error);
  if (error.Failed()) {
    NS_WARNING("Element::SetAttr() failed");
    return error.StealNSResult();
  }

  // do actual insertion
  if (tagName == nsGkAtoms::a) {
    nsresult rv =
        MOZ_KnownLive(htmlEditor)
            ->InsertLinkAroundSelectionAsAction(newElement, aPrincipal);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::InsertLinkAroundSelectionAsAction() failed");
    return rv;
  }

  HTMLEditor::InsertElementOptions options{
      HTMLEditor::InsertElementOption::DeleteSelection};
  // We did insert <img> without splitting ancestor inline elements, but the
  // other browsers split them.  Therefore, let's do it only when the document
  // is content.
  if (htmlEditor->GetDocument()->IsContentDocument()) {
    options += HTMLEditor::InsertElementOption::SplitAncestorInlineElements;
  }
  nsresult rv =
      MOZ_KnownLive(htmlEditor)
          ->InsertElementAtSelectionAsAction(newElement, options, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::InsertElementAtSelectionAsAction() failed");
  return rv;
}

nsresult InsertTagCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * Helper methods
 *****************************************************************************/

static nsresult GetListState(HTMLEditor* aHTMLEditor, bool* aMixed,
                             nsAString& aLocalName) {
  MOZ_ASSERT(aHTMLEditor);
  MOZ_ASSERT(aMixed);

  *aMixed = false;
  aLocalName.Truncate();

  ErrorResult error;
  ListElementSelectionState state(*aHTMLEditor, error);
  if (error.Failed()) {
    NS_WARNING("ListElementSelectionState failed");
    return error.StealNSResult();
  }
  if (state.IsNotOneTypeListElementSelected()) {
    *aMixed = true;
    return NS_OK;
  }

  if (state.IsOLElementSelected()) {
    aLocalName.AssignLiteral("ol");
  } else if (state.IsULElementSelected()) {
    aLocalName.AssignLiteral("ul");
  } else if (state.IsDLElementSelected()) {
    aLocalName.AssignLiteral("dl");
  }
  return NS_OK;
}

}  // namespace mozilla
