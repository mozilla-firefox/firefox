/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EditorCommands_h
#define mozilla_EditorCommands_h

#include "mozilla/EditorForwards.h"
#include "mozilla/EventForwards.h"
#include "mozilla/Maybe.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TypedEnumBits.h"
#include "nsGkAtoms.h"
#include "mozilla/ControllerCommand.h"
#include "nsIPrincipal.h"
#include "nsISupportsImpl.h"
#include "nsRefPtrHashtable.h"
#include "nsStringFwd.h"

class nsAtom;
class nsCommandParams;
class nsICommandParams;
class nsIEditingSession;
class nsITransferable;
class nsStaticAtom;

namespace mozilla {

/**
 * EditorCommandParamType tells you that EditorCommand subclasses refer
 * which type in nsCommandParams (e.g., bool or nsString) or do not refer.
 * If they refer some types, also set where is in nsCommandParams, e.g.,
 * whether "state_attribute" or "state_data".
 */
enum class EditorCommandParamType : uint16_t {
  // The command does not take params (even if specified, always ignored).
  None = 0,
  // The command refers nsCommandParams::GetBool() result.
  Bool = 1 << 0,
  // The command refers nsCommandParams::GetString() result.
  // This may be specified with CString.  In such case,
  // nsCommandParams::GetCString() is preferred.
  String = 1 << 1,
  // The command refers nsCommandParams::GetCString() result.
  CString = 1 << 2,
  // The command refers nsCommandParams::GetISupports("transferable") result.
  Transferable = 1 << 3,

  // The command refres "state_attribute" of nsCommandParams when calling
  // GetBool()/GetString()/GetCString().  This must not be set when the
  // type is None or Transferable.
  StateAttribute = 1 << 14,
  // The command refers "state_data" of nsCommandParams when calling
  // GetBool()/GetString()/GetCString().  This must not be set when the
  // type is None or Transferable.
  StateData = 1 << 15,
};

MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(EditorCommandParamType)

/**
 * This is a base class for commands registered with the editor controller.
 * Note that such commands are designed as singleton classes.  So, MUST be
 * stateless. Any state must be stored via the refCon (an nsIEditor).
 */

class EditorCommand : public ControllerCommand {
 public:
  static EditorCommandParamType GetParamType(Command aCommand) {
    // Keep same order of registration in EditorController.cpp and
    // HTMLEditorController.cpp.
    switch (aCommand) {
      // UndoCommand
      case Command::HistoryUndo:
        return EditorCommandParamType::None;
      // RedoCommand
      case Command::HistoryRedo:
        return EditorCommandParamType::None;
      // CutCommand
      case Command::Cut:
        return EditorCommandParamType::None;
      // CutOrDeleteCommand
      case Command::CutOrDelete:
        return EditorCommandParamType::None;
      // CopyCommand
      case Command::Copy:
        return EditorCommandParamType::None;
      // CopyOrDeleteCommand
      case Command::CopyOrDelete:
        return EditorCommandParamType::None;
      // SelectAllCommand
      case Command::SelectAll:
        return EditorCommandParamType::None;
      // PasteCommand
      case Command::Paste:
        return EditorCommandParamType::None;
      case Command::PasteTransferable:
        return EditorCommandParamType::Transferable;
      // SwitchTextDirectionCommand
      case Command::FormatSetBlockTextDirection:
        return EditorCommandParamType::None;
      // DeleteCommand
      case Command::Delete:
      case Command::DeleteCharBackward:
      case Command::DeleteCharForward:
      case Command::DeleteWordBackward:
      case Command::DeleteWordForward:
      case Command::DeleteToBeginningOfLine:
      case Command::DeleteToEndOfLine:
        return EditorCommandParamType::None;
      // InsertPlaintextCommand
      case Command::InsertText:
        return EditorCommandParamType::String |
               EditorCommandParamType::StateData;
      // InsertParagraphCommand
      case Command::InsertParagraph:
        return EditorCommandParamType::None;
      // InsertLineBreakCommand
      case Command::InsertLineBreak:
        return EditorCommandParamType::None;
      // PasteQuotationCommand
      case Command::PasteAsQuotation:
        return EditorCommandParamType::None;

      // SelectionMoveCommand
      case Command::ScrollTop:
      case Command::ScrollBottom:
      case Command::MoveTop:
      case Command::MoveBottom:
      case Command::SelectTop:
      case Command::SelectBottom:
      case Command::LineNext:
      case Command::LinePrevious:
      case Command::SelectLineNext:
      case Command::SelectLinePrevious:
      case Command::CharPrevious:
      case Command::CharNext:
      case Command::SelectCharPrevious:
      case Command::SelectCharNext:
      case Command::BeginLine:
      case Command::EndLine:
      case Command::SelectBeginLine:
      case Command::SelectEndLine:
      case Command::BeginParagraph:
      case Command::EndParagraph:
      case Command::SelectBeginParagraph:
      case Command::SelectEndParagraph:
      case Command::WordPrevious:
      case Command::WordNext:
      case Command::SelectWordPrevious:
      case Command::SelectWordNext:
      case Command::ScrollPageUp:
      case Command::ScrollPageDown:
      case Command::ScrollLineUp:
      case Command::ScrollLineDown:
      case Command::MovePageUp:
      case Command::MovePageDown:
      case Command::SelectPageUp:
      case Command::SelectPageDown:
      case Command::MoveLeft:
      case Command::MoveRight:
      case Command::MoveUp:
      case Command::MoveDown:
      case Command::MoveLeft2:
      case Command::MoveRight2:
      case Command::MoveUp2:
      case Command::MoveDown2:
      case Command::MoveLeft3:
      case Command::MoveRight3:
      case Command::SelectLeft:
      case Command::SelectRight:
      case Command::SelectUp:
      case Command::SelectDown:
      case Command::SelectLeft2:
      case Command::SelectRight2:
      case Command::SelectUp2:
      case Command::SelectDown2:
      case Command::SelectLeft3:
      case Command::SelectRight3:
        return EditorCommandParamType::None;
      // PasteNoFormattingCommand
      case Command::PasteWithoutFormat:
        return EditorCommandParamType::None;

      // DocumentStateCommand
      case Command::EditorObserverDocumentCreated:
      case Command::EditorObserverDocumentLocationChanged:
      case Command::EditorObserverDocumentWillBeDestroyed:
        return EditorCommandParamType::None;
      // SetDocumentStateCommand
      case Command::SetDocumentModified:
      case Command::SetDocumentUseCSS:
      case Command::SetDocumentReadOnly:
      case Command::SetDocumentInsertBROnEnterKeyPress:
        return EditorCommandParamType::Bool |
               EditorCommandParamType::StateAttribute;
      case Command::SetDocumentDefaultParagraphSeparator:
        return EditorCommandParamType::CString |
               EditorCommandParamType::StateAttribute;
      case Command::ToggleObjectResizers:
      case Command::ToggleInlineTableEditor:
      case Command::ToggleAbsolutePositionEditor:
      case Command::EnableCompatibleJoinSplitNodeDirection:
        return EditorCommandParamType::Bool |
               EditorCommandParamType::StateAttribute;

      // IndentCommand
      case Command::FormatIndent:
        return EditorCommandParamType::None;
      // OutdentCommand
      case Command::FormatOutdent:
        return EditorCommandParamType::None;
      // StyleUpdatingCommand
      case Command::FormatBold:
      case Command::FormatItalic:
      case Command::FormatUnderline:
      case Command::FormatTeletypeText:
      case Command::FormatStrikeThrough:
      case Command::FormatSuperscript:
      case Command::FormatSubscript:
      case Command::FormatNoBreak:
      case Command::FormatEmphasis:
      case Command::FormatStrong:
      case Command::FormatCitation:
      case Command::FormatAbbreviation:
      case Command::FormatAcronym:
      case Command::FormatCode:
      case Command::FormatSample:
      case Command::FormatVariable:
      case Command::FormatRemoveLink:
        return EditorCommandParamType::None;
      // ListCommand
      case Command::InsertOrderedList:
      case Command::InsertUnorderedList:
        return EditorCommandParamType::None;
      // ListItemCommand
      case Command::InsertDefinitionTerm:
      case Command::InsertDefinitionDetails:
        return EditorCommandParamType::None;
      // RemoveListCommand
      case Command::FormatRemoveList:
        return EditorCommandParamType::None;
      // ParagraphStateCommand
      case Command::FormatBlock:
      case Command::ParagraphState:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // FontFaceStateCommand
      case Command::FormatFontName:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // FontSizeStateCommand
      case Command::FormatFontSize:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // FontColorStateCommand
      case Command::FormatFontColor:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // BackgroundColorStateCommand
      case Command::FormatDocumentBackgroundColor:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // HighlightColorStateCommand
      case Command::FormatBackColor:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // AlignCommand:
      case Command::FormatJustifyLeft:
      case Command::FormatJustifyRight:
      case Command::FormatJustifyCenter:
      case Command::FormatJustifyFull:
      case Command::FormatJustifyNone:
        return EditorCommandParamType::CString |
               EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      // RemoveStylesCommand
      case Command::FormatRemove:
        return EditorCommandParamType::None;
      // IncreaseFontSizeCommand
      case Command::FormatIncreaseFontSize:
        return EditorCommandParamType::None;
      // DecreaseFontSizeCommand
      case Command::FormatDecreaseFontSize:
        return EditorCommandParamType::None;
      // InsertHTMLCommand
      case Command::InsertHTML:
        return EditorCommandParamType::String |
               EditorCommandParamType::StateData;
      // InsertTagCommand
      case Command::InsertLink:
      case Command::InsertImage:
        return EditorCommandParamType::String |
               EditorCommandParamType::StateAttribute;
      case Command::InsertHorizontalRule:
        return EditorCommandParamType::None;
      // AbsolutePositioningCommand
      case Command::FormatAbsolutePosition:
        return EditorCommandParamType::None;
      // DecreaseZIndexCommand
      case Command::FormatDecreaseZIndex:
        return EditorCommandParamType::None;
      // IncreaseZIndexCommand
      case Command::FormatIncreaseZIndex:
        return EditorCommandParamType::None;

      default:
        MOZ_ASSERT_UNREACHABLE("Unknown Command");
        return EditorCommandParamType::None;
    }
  }

  // ControllerCommand methods.  Use EditorCommand specific methods instead
  // for internal use.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool IsCommandEnabled(
      const nsACString& aCommandName, nsISupports* aCommandRefCon) final;
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(const nsACString& aCommandName,
                                        nsICommandParams* aParams,
                                        nsISupports* aCommandRefCon) final;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void GetCommandStateParams(
      const nsACString& aCommandName, nsICommandParams* aParams,
      nsISupports* aCommandRefCon) final;

  MOZ_CAN_RUN_SCRIPT virtual bool IsCommandEnabled(
      Command aCommand, EditorBase* aEditorBase) const = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommand(
      Command aCommand, EditorBase& aEditorBase,
      nsIPrincipal* aPrincipal) const = 0;

  /**
   * @param aEditorBase         If the context is an editor, should be set to
   *                            it.  Otherwise, nullptr.
   * @param aEditingSession     If the context is an editing session, should be
   *                            set to it.  This usually occurs if editor has
   *                            not been created yet during initialization.
   *                            Otherwise, nullptr.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult GetCommandStateParams(
      Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
      nsIEditingSession* aEditingSession) const = 0;

  /**
   * Called only when the result of EditorCommand::GetParamType(aCommand) is
   * EditorCommandParamType::None.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommandParam(
      Command /*aCommand*/, EditorBase& /*aEditorBase*/,
      nsIPrincipal* /*aPrincipal*/) const {
    MOZ_ASSERT_UNREACHABLE("Wrong overload is called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Called only when the result of EditorCommand::GetParamType(aCommand)
   * includes EditorCommandParamType::Bool.  If aBoolParam is Nothing, it
   * means that given param was nullptr.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommandParam(
      Command /*aCommand*/, const Maybe<bool>& /*aBoolParam8*/,
      EditorBase& /*aEditorBase*/, nsIPrincipal* /*aPrincipal*/) const {
    MOZ_ASSERT_UNREACHABLE("Wrong overload is called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Called only when the result of EditorCommand::GetParamType(aCommand)
   * includes EditorCommandParamType::CString.  If aCStringParam is void, it
   * means that given param was nullptr.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommandParam(
      Command /*aCommand*/, const nsACString& /*aCStringParam*/,
      EditorBase& /*aEditorBase*/, nsIPrincipal* /*aPrincipal*/) const {
    MOZ_ASSERT_UNREACHABLE("Wrong overload is called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Called only when the result of EditorCommand::GetParamType(aCommand)
   * includes EditorCommandParamType::String.  If aStringParam is void, it
   * means that given param was nullptr.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommandParam(
      Command /*aCommand*/, const nsAString& /*aStringParam*/,
      EditorBase& /*aEditorBase*/, nsIPrincipal* /*aPrincipal*/) const {
    MOZ_ASSERT_UNREACHABLE("Wrong overload is called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  /**
   * Called only when the result of EditorCommand::GetParamType(aCommand) is
   * EditorCommandParamType::Transferable.  If aTransferableParam may be
   * nullptr.
   */
  MOZ_CAN_RUN_SCRIPT virtual nsresult DoCommandParam(
      Command /*aCommand*/, nsITransferable* /*aTransferableParam*/,
      EditorBase& /*aEditorBase*/, nsIPrincipal* /*aPrincipal*/) const {
    MOZ_ASSERT_UNREACHABLE("Wrong overload is called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

 protected:
  EditorCommand() = default;
  virtual ~EditorCommand() = default;
};

#define NS_DECL_EDITOR_COMMAND_COMMON_METHODS                              \
 public:                                                                   \
  MOZ_CAN_RUN_SCRIPT bool IsCommandEnabled(                                \
      Command aCommand, EditorBase* aEditorBase) const final;              \
  using EditorCommand::IsCommandEnabled;                                   \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(                                   \
      Command aCommand, EditorBase& aEditorBase, nsIPrincipal* aPrincipal) \
      const final;                                                         \
  using EditorCommand::DoCommand;                                          \
  MOZ_CAN_RUN_SCRIPT nsresult GetCommandStateParams(                       \
      Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase, \
      nsIEditingSession* aEditingSession) const final;                     \
  using EditorCommand::GetCommandStateParams;                              \
  using EditorCommand::DoCommandParam;

#define NS_DECL_DO_COMMAND_PARAM_DELEGATE_TO_DO_COMMAND                    \
 public:                                                                   \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommandParam(                              \
      Command aCommand, EditorBase& aEditorBase, nsIPrincipal* aPrincipal) \
      const final {                                                        \
    return DoCommand(aCommand, aEditorBase, aPrincipal);                   \
  }

#define NS_DECL_DO_COMMAND_PARAM_FOR_BOOL_PARAM        \
 public:                                               \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommandParam(          \
      Command aCommand, const Maybe<bool>& aBoolParam, \
      EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const final;

#define NS_DECL_DO_COMMAND_PARAM_FOR_CSTRING_PARAM       \
 public:                                                 \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommandParam(            \
      Command aCommand, const nsACString& aCStringParam, \
      EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const final;

#define NS_DECL_DO_COMMAND_PARAM_FOR_STRING_PARAM      \
 public:                                               \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommandParam(          \
      Command aCommand, const nsAString& aStringParam, \
      EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const final;

#define NS_DECL_DO_COMMAND_PARAM_FOR_TRANSFERABLE_PARAM      \
 public:                                                     \
  MOZ_CAN_RUN_SCRIPT nsresult DoCommandParam(                \
      Command aCommand, nsITransferable* aTransferableParam, \
      EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const final;

#define NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd) \
 public:                                                   \
  static EditorCommand* GetInstance() {                    \
    if (!sInstance) {                                      \
      sInstance = new _cmd();                              \
    }                                                      \
    return sInstance;                                      \
  }                                                        \
                                                           \
  static void Shutdown() { sInstance = nullptr; }          \
                                                           \
 private:                                                  \
  static StaticRefPtr<_cmd> sInstance;

#define NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(_cmd) \
  class _cmd final : public EditorCommand {                     \
    NS_DECL_EDITOR_COMMAND_COMMON_METHODS                       \
    NS_DECL_DO_COMMAND_PARAM_DELEGATE_TO_DO_COMMAND             \
    NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd)          \
                                                                \
   protected:                                                   \
    _cmd() = default;                                           \
    virtual ~_cmd() = default;                                  \
  };

#define NS_DECL_EDITOR_COMMAND_FOR_BOOL_PARAM(_cmd)    \
  class _cmd final : public EditorCommand {            \
    NS_DECL_EDITOR_COMMAND_COMMON_METHODS              \
    NS_DECL_DO_COMMAND_PARAM_FOR_BOOL_PARAM            \
    NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd) \
                                                       \
   protected:                                          \
    _cmd() = default;                                  \
    virtual ~_cmd() = default;                         \
  };

#define NS_DECL_EDITOR_COMMAND_FOR_CSTRING_PARAM(_cmd) \
  class _cmd final : public EditorCommand {            \
    NS_DECL_EDITOR_COMMAND_COMMON_METHODS              \
    NS_DECL_DO_COMMAND_PARAM_FOR_CSTRING_PARAM         \
    NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd) \
                                                       \
   protected:                                          \
    _cmd() = default;                                  \
    virtual ~_cmd() = default;                         \
  };

#define NS_DECL_EDITOR_COMMAND_FOR_STRING_PARAM(_cmd)  \
  class _cmd final : public EditorCommand {            \
    NS_DECL_EDITOR_COMMAND_COMMON_METHODS              \
    NS_DECL_DO_COMMAND_PARAM_FOR_STRING_PARAM          \
    NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd) \
                                                       \
   protected:                                          \
    _cmd() = default;                                  \
    virtual ~_cmd() = default;                         \
  };

#define NS_DECL_EDITOR_COMMAND_FOR_TRANSFERABLE_PARAM(_cmd) \
  class _cmd final : public EditorCommand {                 \
    NS_DECL_EDITOR_COMMAND_COMMON_METHODS                   \
    NS_DECL_DO_COMMAND_PARAM_FOR_TRANSFERABLE_PARAM         \
    NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(_cmd)      \
                                                            \
   protected:                                               \
    _cmd() = default;                                       \
    virtual ~_cmd() = default;                              \
  };

// basic editor commands
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(UndoCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(RedoCommand)

NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(CutCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(CutOrDeleteCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(CopyCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(CopyOrDeleteCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(PasteCommand)
NS_DECL_EDITOR_COMMAND_FOR_TRANSFERABLE_PARAM(PasteTransferableCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(SwitchTextDirectionCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(DeleteCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(SelectAllCommand)

NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(SelectionMoveCommands)

// Insert content commands
NS_DECL_EDITOR_COMMAND_FOR_STRING_PARAM(InsertPlaintextCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(InsertParagraphCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(InsertLineBreakCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(PasteQuotationCommand)

/******************************************************************************
 * Commands for HTML editor
 ******************************************************************************/

// virtual base class for commands that need to save and update Boolean state
// (like styles etc)
class StateUpdatingCommandBase : public EditorCommand {
 public:
  NS_DECL_EDITOR_COMMAND_COMMON_METHODS
  NS_DECL_DO_COMMAND_PARAM_DELEGATE_TO_DO_COMMAND

 protected:
  StateUpdatingCommandBase() = default;
  virtual ~StateUpdatingCommandBase() = default;

  // get the current state (on or off) for this style or block format
  MOZ_CAN_RUN_SCRIPT virtual nsresult GetCurrentState(
      nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
      nsCommandParams& aParams) const = 0;

  // add/remove the style
  MOZ_CAN_RUN_SCRIPT virtual nsresult ToggleState(
      nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
      nsIPrincipal* aPrincipal) const = 0;

  static nsStaticAtom* GetTagName(Command aCommand) {
    switch (aCommand) {
      case Command::FormatBold:
        return nsGkAtoms::b;
      case Command::FormatItalic:
        return nsGkAtoms::i;
      case Command::FormatUnderline:
        return nsGkAtoms::u;
      case Command::FormatTeletypeText:
        return nsGkAtoms::tt;
      case Command::FormatStrikeThrough:
        return nsGkAtoms::strike;
      case Command::FormatSuperscript:
        return nsGkAtoms::sup;
      case Command::FormatSubscript:
        return nsGkAtoms::sub;
      case Command::FormatNoBreak:
        return nsGkAtoms::nobr;
      case Command::FormatEmphasis:
        return nsGkAtoms::em;
      case Command::FormatStrong:
        return nsGkAtoms::strong;
      case Command::FormatCitation:
        return nsGkAtoms::cite;
      case Command::FormatAbbreviation:
        return nsGkAtoms::abbr;
      case Command::FormatAcronym:
        return nsGkAtoms::acronym;
      case Command::FormatCode:
        return nsGkAtoms::code;
      case Command::FormatSample:
        return nsGkAtoms::samp;
      case Command::FormatVariable:
        return nsGkAtoms::var;
      case Command::FormatRemoveLink:
        return nsGkAtoms::href;
      case Command::InsertOrderedList:
        return nsGkAtoms::ol;
      case Command::InsertUnorderedList:
        return nsGkAtoms::ul;
      case Command::InsertDefinitionTerm:
        return nsGkAtoms::dt;
      case Command::InsertDefinitionDetails:
        return nsGkAtoms::dd;
      case Command::FormatAbsolutePosition:
        return nsGkAtoms::_empty;
      default:
        return nullptr;
    }
  }
  friend class InsertTagCommand;  // for allowing it to access GetTagName()
};

// Shared class for the various style updating commands like bold, italics etc.
// Suitable for commands whose state is either 'on' or 'off'.
class StyleUpdatingCommand final : public StateUpdatingCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(StyleUpdatingCommand)

 protected:
  StyleUpdatingCommand() = default;
  virtual ~StyleUpdatingCommand() = default;

  // get the current state (on or off) for this style or block format
  MOZ_CAN_RUN_SCRIPT nsresult
  GetCurrentState(nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
                  nsCommandParams& aParams) const final;

  // add/remove the style
  MOZ_CAN_RUN_SCRIPT nsresult ToggleState(nsStaticAtom& aTagName,
                                          HTMLEditor& aHTMLEditor,
                                          nsIPrincipal* aPrincipal) const final;
};

class InsertTagCommand final : public EditorCommand {
 public:
  NS_DECL_EDITOR_COMMAND_COMMON_METHODS
  NS_DECL_DO_COMMAND_PARAM_DELEGATE_TO_DO_COMMAND
  NS_DECL_DO_COMMAND_PARAM_FOR_STRING_PARAM
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(InsertTagCommand)

 protected:
  InsertTagCommand() = default;
  virtual ~InsertTagCommand() = default;

  static nsAtom* GetTagName(Command aCommand) {
    switch (aCommand) {
      case Command::InsertLink:
        return nsGkAtoms::a;
      case Command::InsertImage:
        return nsGkAtoms::img;
      case Command::InsertHorizontalRule:
        return nsGkAtoms::hr;
      default:
        return StateUpdatingCommandBase::GetTagName(aCommand);
    }
  }
};

class ListCommand final : public StateUpdatingCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(ListCommand)

 protected:
  ListCommand() = default;
  virtual ~ListCommand() = default;

  // get the current state (on or off) for this style or block format
  MOZ_CAN_RUN_SCRIPT nsresult
  GetCurrentState(nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
                  nsCommandParams& aParams) const final;

  // add/remove the style
  MOZ_CAN_RUN_SCRIPT nsresult ToggleState(nsStaticAtom& aTagName,
                                          HTMLEditor& aHTMLEditor,
                                          nsIPrincipal* aPrincipal) const final;
};

class ListItemCommand final : public StateUpdatingCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(ListItemCommand)

 protected:
  ListItemCommand() = default;
  virtual ~ListItemCommand() = default;

  // get the current state (on or off) for this style or block format
  MOZ_CAN_RUN_SCRIPT nsresult
  GetCurrentState(nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
                  nsCommandParams& aParams) const final;

  // add/remove the style
  MOZ_CAN_RUN_SCRIPT nsresult ToggleState(nsStaticAtom& aTagName,
                                          HTMLEditor& aHTMLEditor,
                                          nsIPrincipal* aPrincipal) const final;
};

// Base class for commands whose state consists of a string (e.g. para format)
class MultiStateCommandBase : public EditorCommand {
 public:
  NS_DECL_EDITOR_COMMAND_COMMON_METHODS
  NS_DECL_DO_COMMAND_PARAM_FOR_STRING_PARAM

 protected:
  MultiStateCommandBase() = default;
  virtual ~MultiStateCommandBase() = default;

  MOZ_CAN_RUN_SCRIPT virtual nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const = 0;
  MOZ_CAN_RUN_SCRIPT virtual nsresult SetState(
      HTMLEditor* aHTMLEditor, const nsAString& aNewState,
      nsIPrincipal* aPrincipal) const = 0;
};

/**
 * The command class for Document.execCommand("formatBlock"),
 * Document.queryCommandValue("formatBlock") etc.
 */
class FormatBlockStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(FormatBlockStateCommand)

 protected:
  FormatBlockStateCommand() = default;
  virtual ~FormatBlockStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

/**
 * The command class for the legacy XUL edit command, cmd_paragraphState.
 * This command treats only <p>, <pre>, <h1>, <h2>, <h3>, <h4>, <h5>, <h6>,
 * <address> as a format node.
 */
class ParagraphStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(ParagraphStateCommand)

 protected:
  ParagraphStateCommand() = default;
  virtual ~ParagraphStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class FontFaceStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(FontFaceStateCommand)

 protected:
  FontFaceStateCommand() = default;
  virtual ~FontFaceStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class FontSizeStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(FontSizeStateCommand)

 protected:
  FontSizeStateCommand() = default;
  virtual ~FontSizeStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class HighlightColorStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(HighlightColorStateCommand)

 protected:
  HighlightColorStateCommand() = default;
  virtual ~HighlightColorStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class FontColorStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(FontColorStateCommand)

 protected:
  FontColorStateCommand() = default;
  virtual ~FontColorStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class AlignCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(AlignCommand)

 protected:
  AlignCommand() = default;
  virtual ~AlignCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class BackgroundColorStateCommand final : public MultiStateCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(BackgroundColorStateCommand)

 protected:
  BackgroundColorStateCommand() = default;
  virtual ~BackgroundColorStateCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult GetCurrentState(
      HTMLEditor* aHTMLEditor, nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult SetState(HTMLEditor* aHTMLEditor,
                                       const nsAString& aNewState,
                                       nsIPrincipal* aPrincipal) const final;
};

class AbsolutePositioningCommand final : public StateUpdatingCommandBase {
 public:
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(AbsolutePositioningCommand)

 protected:
  AbsolutePositioningCommand() = default;
  virtual ~AbsolutePositioningCommand() = default;

  MOZ_CAN_RUN_SCRIPT nsresult
  GetCurrentState(nsStaticAtom& aTagName, HTMLEditor& aHTMLEditor,
                  nsCommandParams& aParams) const final;
  MOZ_CAN_RUN_SCRIPT nsresult ToggleState(nsStaticAtom& aTagName,
                                          HTMLEditor& aHTMLEditor,
                                          nsIPrincipal* aPrincipal) const final;
};

// composer commands

NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(DocumentStateCommand)

class SetDocumentStateCommand final : public EditorCommand {
 public:
  NS_DECL_EDITOR_COMMAND_COMMON_METHODS
  NS_DECL_DO_COMMAND_PARAM_FOR_BOOL_PARAM
  NS_DECL_DO_COMMAND_PARAM_FOR_CSTRING_PARAM
  NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON(SetDocumentStateCommand)

 private:
  SetDocumentStateCommand() = default;
  virtual ~SetDocumentStateCommand() = default;
};

NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(DecreaseZIndexCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(IncreaseZIndexCommand)

// Generic commands

// Edit menu
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(PasteNoFormattingCommand)

// Block transformations
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(IndentCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(OutdentCommand)

NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(RemoveListCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(RemoveStylesCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(IncreaseFontSizeCommand)
NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE(DecreaseFontSizeCommand)

// Insert content commands
NS_DECL_EDITOR_COMMAND_FOR_STRING_PARAM(InsertHTMLCommand)

#undef NS_DECL_EDITOR_COMMAND_FOR_NO_PARAM_WITH_DELEGATE
#undef NS_DECL_EDITOR_COMMAND_FOR_BOOL_PARAM
#undef NS_DECL_EDITOR_COMMAND_FOR_CSTRING_PARAM
#undef NS_DECL_EDITOR_COMMAND_FOR_STRING_PARAM
#undef NS_DECL_EDITOR_COMMAND_FOR_TRANSFERABLE_PARAM
#undef NS_DECL_EDITOR_COMMAND_COMMON_METHODS
#undef NS_DECL_DO_COMMAND_PARAM_DELEGATE_TO_DO_COMMAND
#undef NS_DECL_DO_COMMAND_PARAM_FOR_BOOL_PARAM
#undef NS_DECL_DO_COMMAND_PARAM_FOR_CSTRING_PARAM
#undef NS_DECL_DO_COMMAND_PARAM_FOR_STRING_PARAM
#undef NS_DECL_DO_COMMAND_PARAM_FOR_TRANSFERABLE_PARAM
#undef NS_INLINE_DECL_EDITOR_COMMAND_MAKE_SINGLETON

}  // namespace mozilla

#endif  // #ifndef mozilla_EditorCommands_h
