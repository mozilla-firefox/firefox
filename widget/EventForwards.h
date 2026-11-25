/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EventForwards_h_
#define mozilla_EventForwards_h_

#include <stdint.h>

#include "nsStringFwd.h"
#include "nsTArray.h"

#ifdef DEBUG
#  include "mozilla/StaticPrefs_dom.h"
#endif  // #ifdef DEBUG

class nsCommandParams;

/**
 * XXX Following enums should be in BasicEvents.h.  However, currently, it's
 *     impossible to use foward delearation for enum.
 */

/**
 * Return status for event processors.
 */
enum nsEventStatus {
  // The event is ignored, do default processing
  nsEventStatus_eIgnore,
  // The event is consumed, don't do default processing
  nsEventStatus_eConsumeNoDefault,
  // The event is consumed, but do default processing
  nsEventStatus_eConsumeDoDefault,
  // Value is not for use, only for serialization
  nsEventStatus_eSentinel
};

namespace mozilla {

enum class CanBubble { eYes, eNo };

enum class Cancelable { eYes, eNo };

enum class ChromeOnlyDispatch { eYes, eNo };

enum class Trusted { eYes, eNo };

enum class Composed { eYes, eNo, eDefault };

enum class SystemGroupOnly { eYes, eNo };

/**
 * Event messages
 */

using EventMessageType = uint16_t;

enum EventMessage : EventMessageType {

#define NS_EVENT_MESSAGE(aMessage) aMessage,
#define NS_EVENT_MESSAGE_FIRST_LAST(aMessage, aFirst, aLast) \
  aMessage##First = aFirst, aMessage##Last = aLast,

#include "mozilla/EventMessageList.inc"

#undef NS_EVENT_MESSAGE
#undef NS_EVENT_MESSAGE_FIRST_LAST

  // For preventing bustage due to "," after the last item.
  eEventMessage_MaxValue
};

const char* ToChar(EventMessage aEventMessage);

/**
 * Return true if aMessage should be dispatched as a WidgetPointerEvent.
 */
[[nodiscard]] bool IsPointerEventMessage(EventMessage aMessage);

/**
 * Return true if aMessage should be dispatched as a WidgetPointerEvent and
 * the message was dispatched as a WidgetMouseEvent.  So, this returns true
 * if the event message is ePointerClick, ePointerAuxClick or eContextMenu.
 */
[[nodiscard]] bool IsPointerEventMessageOriginallyMouseEventMessage(
    EventMessage aMessage);

/**
 * Return true if aMessage is not allowed to dispatch to a content node except
 * Element node when we dispatch the event as a trusted event which .
 *
 * NOTE: This is currently designed for PresShell to consider whether a content
 * node is proper event target for aMessage.  So, this may not work the expected
 * way in other cases.  Therefore, when you use this method in a new place, you
 * should check whether this returns the expected result for you.
 */
[[nodiscard]] bool IsForbiddenDispatchingToNonElementContent(
    EventMessage aMessage);

/**
 * Event class IDs
 */

enum EventClassID : uint8_t {
// The event class name will be:
//   eBasicEventClass for WidgetEvent
//   eFooEventClass for WidgetFooEvent or InternalFooEvent
#define NS_ROOT_EVENT_CLASS(aPrefix, aName) eBasic##aName##Class,
#define NS_EVENT_CLASS(aPrefix, aName) e##aName##Class,

#include "mozilla/EventClassList.inc"

#undef NS_EVENT_CLASS
#undef NS_ROOT_EVENT_CLASS
  eEventClassUninitialized,
};

const char* ToChar(EventClassID aEventClassID);

/**
 * Return true if aMessage is a valid EventMessage value for aClassID when an
 * event is read from another process.  This is used to reject events whose
 * mMessage/mClass combination is inconsistent and therefore likely tampered
 * with by a compromised content process.
 */
[[nodiscard]] bool IsValidMessageForIPC(EventMessage aMessage,
                                        EventClassID aClassID);

typedef uint16_t Modifiers;

#define NS_DEFINE_KEYNAME(aCPPName, aDOMKeyName) KEY_NAME_INDEX_##aCPPName,

enum KeyNameIndex : uint16_t {
#include "mozilla/KeyNameList.inc"
  // If a DOM keyboard event is synthesized by script, this is used.  Then,
  // specified key name should be stored and use it as .key value.
  KEY_NAME_INDEX_USE_STRING
};

#undef NS_DEFINE_KEYNAME

const nsCString ToString(KeyNameIndex aKeyNameIndex);

#define NS_DEFINE_PHYSICAL_KEY_CODE_NAME(aCPPName, aDOMCodeName) \
  CODE_NAME_INDEX_##aCPPName,

enum CodeNameIndex : uint8_t {
#include "mozilla/PhysicalKeyCodeNameList.inc"
  // If a DOM keyboard event is synthesized by script, this is used.  Then,
  // specified code name should be stored and use it as .code value.
  CODE_NAME_INDEX_USE_STRING
};

#undef NS_DEFINE_PHYSICAL_KEY_CODE_NAME

const nsCString ToString(CodeNameIndex aCodeNameIndex);

enum class ExpandToClusterBoundary : bool { No, Yes };

inline auto format_as(ExpandToClusterBoundary aExpand) {
  return aExpand == ExpandToClusterBoundary::Yes ? "Yes" : "No";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                ExpandToClusterBoundary aExpand) {
  return aStream << format_as(aExpand);
}

enum class RangeDirection : bool { Normal, Reversed };

inline auto format_as(RangeDirection aDirection) {
  return aDirection == RangeDirection::Reversed ? "Reversed" : "Normal";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                RangeDirection aDirection) {
  return aStream << format_as(aDirection);
}

#define NS_DEFINE_INPUTTYPE(aCPPName, aDOMName) e##aCPPName,

using EditorInputTypeType = uint8_t;
enum class EditorInputType : EditorInputTypeType {
#include "mozilla/InputTypeList.inc"
  // If a DOM input event is synthesized by script, this is used.  Then,
  // specified input type should be stored as string and use it as .inputType
  // value.
  eUnknown,
};

#undef NS_DEFINE_INPUTTYPE

#define NS_DEFINE_INPUTTYPE(aCPPName, aDOMName) \
  case EditorInputType::e##aCPPName:            \
    return aStream << ("EditorInputType::e" #aCPPName);

inline const std::ostream& operator<<(std::ostream& aStream,
                                      const EditorInputType& aInputType) {
  switch (aInputType) {
#include "mozilla/InputTypeList.inc"
    case EditorInputType::eUnknown:
      return aStream << "EditorInputType::eUnknown";
  }
  return aStream << "<Invalid EditorInputType>";
}

#undef NS_DEFINE_INPUTTYPE

inline bool ExposesClipboardDataOrDataTransfer(EditorInputType aInputType) {
  switch (aInputType) {
    case EditorInputType::eInsertFromPaste:
    case EditorInputType::eInsertFromPasteAsQuotation:
      return true;
    default:
      return false;
  }
}

/**
 * IsDataAvailableOnTextEditor() returns true if aInputType on TextEditor
 * should have non-null InputEvent.data value.
 */
inline bool IsDataAvailableOnTextEditor(EditorInputType aInputType) {
  switch (aInputType) {
    case EditorInputType::eInsertText:
    case EditorInputType::eInsertCompositionText:
    case EditorInputType::eInsertFromComposition:  // Only level 2
    case EditorInputType::eInsertFromPaste:
    case EditorInputType::eInsertFromPasteAsQuotation:
    case EditorInputType::eInsertTranspose:
    case EditorInputType::eInsertFromDrop:
    case EditorInputType::eInsertReplacementText:
    case EditorInputType::eInsertFromYank:
    case EditorInputType::eFormatSetBlockTextDirection:
    case EditorInputType::eFormatSetInlineTextDirection:
      return true;
    default:
      return false;
  }
}

/**
 * IsDataAvailableOnHTMLEditor() returns true if aInputType on HTMLEditor
 * should have non-null InputEvent.data value.
 */
inline bool IsDataAvailableOnHTMLEditor(EditorInputType aInputType) {
  switch (aInputType) {
    case EditorInputType::eInsertText:
    case EditorInputType::eInsertCompositionText:
    case EditorInputType::eInsertFromComposition:  // Only level 2
    case EditorInputType::eFormatSetBlockTextDirection:
    case EditorInputType::eFormatSetInlineTextDirection:
    case EditorInputType::eInsertLink:
    case EditorInputType::eFormatBackColor:
    case EditorInputType::eFormatFontColor:
    case EditorInputType::eFormatFontName:
      return true;
    default:
      return false;
  }
}

/**
 * IsDataTransferAvailableOnHTMLEditor() returns true if aInputType on
 * HTMLEditor should have non-null InputEvent.dataTransfer value.
 */
inline bool IsDataTransferAvailableOnHTMLEditor(EditorInputType aInputType) {
  switch (aInputType) {
    case EditorInputType::eInsertFromPaste:
    case EditorInputType::eInsertFromPasteAsQuotation:
    case EditorInputType::eInsertFromDrop:
    case EditorInputType::eInsertTranspose:
    case EditorInputType::eInsertReplacementText:
    case EditorInputType::eInsertFromYank:
      return true;
    default:
      return false;
  }
}

/**
 * MayHaveTargetRangesOnHTMLEditor() returns true if "beforeinput" event whose
 * whose inputType is aInputType on HTMLEditor may return non-empty static
 * range array from getTargetRanges().
 * Note that TextEditor always sets empty array.  Therefore, there is no
 * method for TextEditor.
 */
inline bool MayHaveTargetRangesOnHTMLEditor(EditorInputType aInputType) {
  switch (aInputType) {
    // Explicitly documented by the specs.
    case EditorInputType::eHistoryRedo:
    case EditorInputType::eHistoryUndo:
    // Not documented, but other browsers use empty array.
    case EditorInputType::eFormatSetBlockTextDirection:
      return false;
    default:
      return true;
  }
}

/**
 * IsCancelableBeforeInputEvent() returns true if `beforeinput` event for
 * aInputType should be cancelable.
 *
 * Input Events Level 1:
 *   https://rawgit.com/w3c/input-events/v1/index.html#x5-1-2-attributes
 * Input Events Level 2:
 *   https://w3c.github.io/input-events/#interface-InputEvent-Attributes
 */
inline bool IsCancelableBeforeInputEvent(EditorInputType aInputType) {
  switch (aInputType) {
    case EditorInputType::eInsertText:
      return true;  // In Level 1, undefined.
    case EditorInputType::eInsertReplacementText:
      return true;  // In Level 1, undefined.
    case EditorInputType::eInsertLineBreak:
      return true;  // In Level 1, undefined.
    case EditorInputType::eInsertParagraph:
      return true;  // In Level 1, undefined.
    case EditorInputType::eInsertOrderedList:
      return true;
    case EditorInputType::eInsertUnorderedList:
      return true;
    case EditorInputType::eInsertHorizontalRule:
      return true;
    case EditorInputType::eInsertFromYank:
      return true;
    case EditorInputType::eInsertFromDrop:
      return true;
    case EditorInputType::eInsertFromPaste:
      return true;
    case EditorInputType::eInsertFromPasteAsQuotation:
      return true;
    case EditorInputType::eInsertTranspose:
      return true;
    case EditorInputType::eInsertCompositionText:
      return false;
    case EditorInputType::eInsertFromComposition:
      MOZ_ASSERT(!StaticPrefs::dom_input_events_conform_to_level_1());
      return true;
    case EditorInputType::eInsertLink:
      return true;
    case EditorInputType::eDeleteCompositionText:
      MOZ_ASSERT(!StaticPrefs::dom_input_events_conform_to_level_1());
      return false;
    case EditorInputType::eDeleteWordBackward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteWordForward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteSoftLineBackward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteSoftLineForward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteEntireSoftLine:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteHardLineBackward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteHardLineForward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteByDrag:
      return true;
    case EditorInputType::eDeleteByCut:
      return true;
    case EditorInputType::eDeleteContent:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteContentBackward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eDeleteContentForward:
      return true;  // In Level 1, undefined.
    case EditorInputType::eHistoryUndo:
      return true;
    case EditorInputType::eHistoryRedo:
      return true;
    case EditorInputType::eFormatBold:
      return true;
    case EditorInputType::eFormatItalic:
      return true;
    case EditorInputType::eFormatUnderline:
      return true;
    case EditorInputType::eFormatStrikeThrough:
      return true;
    case EditorInputType::eFormatSuperscript:
      return true;
    case EditorInputType::eFormatSubscript:
      return true;
    case EditorInputType::eFormatJustifyFull:
      return true;
    case EditorInputType::eFormatJustifyCenter:
      return true;
    case EditorInputType::eFormatJustifyRight:
      return true;
    case EditorInputType::eFormatJustifyLeft:
      return true;
    case EditorInputType::eFormatIndent:
      return true;
    case EditorInputType::eFormatOutdent:
      return true;
    case EditorInputType::eFormatRemove:
      return true;
    case EditorInputType::eFormatSetBlockTextDirection:
      return true;
    case EditorInputType::eFormatSetInlineTextDirection:
      return true;
    case EditorInputType::eFormatBackColor:
      return true;
    case EditorInputType::eFormatFontColor:
      return true;
    case EditorInputType::eFormatFontName:
      return true;
    case EditorInputType::eUnknown:
      // This is not declared by Input Events, but it does not make sense to
      // allow web apps to cancel default action without inputType value check.
      // If some our specific edit actions should be cancelable, new inputType
      // value for them should be declared by the spec.
      return false;
    default:
      MOZ_ASSERT_UNREACHABLE("The new input type is not handled");
      return false;
  }
}

enum class OnlyEnabledCheck : bool { No, Yes };

inline auto format_as(OnlyEnabledCheck aOnlyEnabledCheck) {
  return aOnlyEnabledCheck == OnlyEnabledCheck::Yes ? "Yes" : "No";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                OnlyEnabledCheck aOnlyEnabledCheck) {
  return aStream << format_as(aOnlyEnabledCheck);
}

enum class PreventSetSelection : bool { No, Yes };

inline auto format_as(PreventSetSelection aPrevent) {
  return aPrevent == PreventSetSelection::Yes ? "Yes" : "No";
}

inline std::ostream& operator<<(std::ostream& aStream,
                                PreventSetSelection aPrevent) {
  return aStream << format_as(aPrevent);
}

#define NS_DEFINE_COMMAND(aName, aCommandStr) , aName
#define NS_DEFINE_COMMAND_WITH_PARAM(aName, aCommandStr, aParam) , aName
#define NS_DEFINE_COMMAND_NO_EXEC_COMMAND(aName) , aName

typedef uint8_t CommandInt;
enum class Command : CommandInt {
  DoNothing

#include "mozilla/CommandList.inc"
};
#undef NS_DEFINE_COMMAND
#undef NS_DEFINE_COMMAND_WITH_PARAM
#undef NS_DEFINE_COMMAND_NO_EXEC_COMMAND

const char* ToChar(Command aCommand);

/**
 * Return a command value for aCommandName.
 * XXX: Is there a better place to put `Command` related methods instead of
 *      global scope in `mozilla` namespace?
 *
 * @param aCommandName          Should be a XUL command name like "cmd_bold"
 *                              (case sensitive).
 * @param aCommandparams        Additional parameter value of aCommandName.
 *                              Can be nullptr, but if aCommandName requires
 *                              additional parameter and sets this to nullptr,
 *                              will return Command::DoNothing with warning.
 */
Command GetInternalCommand(const nsACString& aCommandName,
                           const nsCommandParams* aCommandParams = nullptr);

}  // namespace mozilla

/**
 * All header files should include this header instead of *Events.h.
 */

namespace mozilla {

template <class T>
class OwningNonNull;

namespace dom {
class StaticRange;
}

#define NS_EVENT_CLASS(aPrefix, aName) class aPrefix##aName;
#define NS_ROOT_EVENT_CLASS(aPrefix, aName) NS_EVENT_CLASS(aPrefix, aName)

#include "mozilla/EventClassList.inc"

#undef NS_EVENT_CLASS
#undef NS_ROOT_EVENT_CLASS

// BasicEvents.h
struct BaseEventFlags;
struct EventFlags;

class WidgetEventTime;

// TextEvents.h
enum class AccessKeyType;

struct AlternativeCharCode;
struct ShortcutKeyCandidate;

typedef nsTArray<ShortcutKeyCandidate> ShortcutKeyCandidateArray;
typedef AutoTArray<ShortcutKeyCandidate, 10> AutoShortcutKeyCandidateArray;

// TextRange.h
typedef uint8_t RawTextRangeType;
enum class TextRangeType : RawTextRangeType;

struct TextRangeStyle;
struct TextRange;

class EditCommands;
class TextRangeArray;

typedef nsTArray<OwningNonNull<dom::StaticRange>> OwningNonNullStaticRangeArray;

// FontRange.h
struct FontRange;

enum MouseButton : int16_t {
  eNotPressed = -1,
  ePrimary = 0,
  eMiddle = 1,
  eSecondary = 2,
  eX1 = 3,  // Typically, "back" button
  eX2 = 4,  // Typically, "forward" button
  eEraser = 5
};

enum MouseButtonsFlag {
  eNoButtons = 0x00,
  ePrimaryFlag = 0x01,
  eSecondaryFlag = 0x02,
  eMiddleFlag = 0x04,
  // typicall, "back" button being left side of 5-button
  // mice, see "buttons" attribute document of DOM3 Events.
  e4thFlag = 0x08,
  // typicall, "forward" button being right side of 5-button
  // mice, see "buttons" attribute document of DOM3 Events.
  e5thFlag = 0x10,
  eEraserFlag = 0x20
};

/**
 * Returns a MouseButtonsFlag value which is changed by a button state change
 * event whose mButton is aMouseButton.
 */
inline MouseButtonsFlag MouseButtonsFlagToChange(MouseButton aMouseButton) {
  switch (aMouseButton) {
    case MouseButton::ePrimary:
      return MouseButtonsFlag::ePrimaryFlag;
    case MouseButton::eMiddle:
      return MouseButtonsFlag::eMiddleFlag;
    case MouseButton::eSecondary:
      return MouseButtonsFlag::eSecondaryFlag;
    case MouseButton::eX1:
      return MouseButtonsFlag::e4thFlag;
    case MouseButton::eX2:
      return MouseButtonsFlag::e5thFlag;
    case MouseButton::eEraser:
      return MouseButtonsFlag::eEraserFlag;
    default:
      return MouseButtonsFlag::eNoButtons;
  }
}

nsCString InputSourceToString(uint16_t aInputSource);

enum class TextRangeType : RawTextRangeType;

// IMEData.h

template <typename IntType>
class StartAndEndOffsets;
template <typename IntType>
class OffsetAndData;

}  // namespace mozilla

#endif  // mozilla_EventForwards_h_
