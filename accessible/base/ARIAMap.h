/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=2:tabstop=2:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_aria_ARIAMap_h_
#define mozilla_a11y_aria_ARIAMap_h_

#include "ARIAStateMap.h"
#include "mozilla/a11y/AccTypes.h"
#include "mozilla/a11y/DocAccessible.h"
#include "mozilla/a11y/Role.h"

#include "nsAtom.h"
#include "nsIContent.h"
#include "nsTHashSet.h"

class nsINode;

namespace mozilla::dom {
class Element;
}

////////////////////////////////////////////////////////////////////////////////
// Value constants

/**
 * Used to define if role requires to expose Value interface.
 */
enum EValueRule {
  /**
   * Value interface isn't exposed.
   */
  eNoValue,

  /**
   * Value interface is implemented, supports value, min and max from
   * aria-valuenow, aria-valuemin and aria-valuemax.
   */
  eHasValueMinMax,

  /**
   * Value interface is implemented, but only if the element is focusable.
   * For instance, in ARIA 1.1 the ability for authors to create adjustable
   * splitters was provided by supporting the value interface on separators
   * that are focusable. Non-focusable separators expose no value information.
   */
  eHasValueMinMaxIfFocusable
};

////////////////////////////////////////////////////////////////////////////////
// Action constants

/**
 * Used to define if the role requires to expose action.
 */
enum EActionRule {
  eNoAction,
  eActivateAction,
  eClickAction,
  ePressAction,
  eCheckUncheckAction,
  eExpandAction,
  eJumpAction,
  eOpenCloseAction,
  eSelectAction,
  eSortAction,
  eSwitchAction
};

////////////////////////////////////////////////////////////////////////////////
// Live region constants

/**
 * Used to define if role exposes default value of aria-live attribute.
 */
enum ELiveAttrRule {
  eNoLiveAttr,
  eOffLiveAttr,
  ePoliteLiveAttr,
  eAssertiveLiveAttr
};

////////////////////////////////////////////////////////////////////////////////
// Role constants

/**
 * ARIA role overrides role from native markup.
 */
const bool kUseMapRole = true;

/**
 * ARIA role doesn't override the role from native markup.
 */
const bool kUseNativeRole = false;

////////////////////////////////////////////////////////////////////////////////
// ARIA attribute characteristic masks

/**
 * This mask indicates the attribute should not be exposed as an object
 * attribute via the catch-all logic in Accessible::Attributes().
 * This means it either isn't mean't to be exposed as an object attribute, or
 * that it should, but is already handled in other code.
 */
const uint8_t ATTR_BYPASSOBJ = 0x1 << 0;
const uint8_t ATTR_BYPASSOBJ_IF_FALSE = 0x1 << 1;

/**
 * This mask indicates the attribute is expected to have an NMTOKEN or bool
 * value. (See for example usage in Accessible::Attributes())
 */
const uint8_t ATTR_VALTOKEN = 0x1 << 2;

/**
 * Indicate the attribute is global state or property (refer to
 * http://www.w3.org/TR/wai-aria/states_and_properties#global_states).
 */
const uint8_t ATTR_GLOBAL = 0x1 << 3;

/**
 * Indicates that the attribute should have an integer value.
 */
const uint8_t ATTR_VALINT = 0x1 << 4;

/**
 * Indicates that the attribute can have reflected elements.
 */
const uint8_t ATTR_REFLECT_ELEMENTS = 0x1 << 5;

////////////////////////////////////////////////////////////////////////////////
// State map entry

/**
 * Used in nsRoleMapEntry.state if no nsIAccessibleStates are automatic for
 * a given role.
 */
#define kNoReqStates 0

////////////////////////////////////////////////////////////////////////////////
// Role map entry

/**
 * For each ARIA role, this maps the nsIAccessible information.
 */
struct nsRoleMapEntry {
  /**
   * Return true if matches to the given ARIA role.
   */
  bool Is(nsAtom* aARIARole) const { return roleAtom == aARIARole; }

  /**
   * Return true if ARIA role has the given accessible type.
   */
  bool IsOfType(mozilla::a11y::AccGenericType aType) const {
    return accTypes & aType;
  }

  /**
   * Return ARIA role.
   */
  const nsDependentAtomString ARIARoleString() const {
    return nsDependentAtomString(roleAtom);
  }

  // ARIA role: string representation such as "button"
  nsStaticAtom* const roleAtom;

  // Role mapping rule: maps to enum Role
  mozilla::a11y::role role;

  // Role rule: whether to use mapped role or native semantics
  bool roleRule;

  // Value mapping rule: how to compute accessible value
  EValueRule valueRule;

  // Action mapping rule, how to expose accessible action
  EActionRule actionRule;

  // 'live' and 'container-live' object attributes mapping rule: how to expose
  // these object attributes if ARIA 'live' attribute is missed.
  ELiveAttrRule liveAttRule;

  // LocalAccessible types this role belongs to.
  uint32_t accTypes;

  // Automatic state mapping rule: always include in states
  uint64_t state;  // or kNoReqStates if no default state for this role

  // ARIA properties supported for this role (in other words, the aria-foo
  // attribute to accessible states mapping rules).
  // Currently you cannot have unlimited mappings, because
  // a variable sized array would not allow the use of
  // C++'s struct initialization feature.
  mozilla::a11y::aria::EStateRule attributeMap1;
  mozilla::a11y::aria::EStateRule attributeMap2;
  mozilla::a11y::aria::EStateRule attributeMap3;
  mozilla::a11y::aria::EStateRule attributeMap4;
};

////////////////////////////////////////////////////////////////////////////////
// ARIA map

/**
 *  These provide the mappings for WAI-ARIA roles, states and properties using
 *  the structs defined in this file and ARIAStateMap files.
 */
namespace mozilla {
namespace a11y {
class AccAttributes;

namespace aria {

/**
 * Empty role map entry. Used by accessibility service to create an accessible
 * if the accessible can't use role of used accessible class. For example,
 * it is used for table cells that aren't contained by table.
 */
extern nsRoleMapEntry gEmptyRoleMap;

/**
 * Constants for the role map entry index to indicate that the role map entry
 * isn't in sWAIRoleMaps, but rather is a special entry: nullptr,
 * gEmptyRoleMap, and sLandmarkRoleMap
 */
const uint8_t NO_ROLE_MAP_ENTRY_INDEX = UINT8_MAX - 2;
const uint8_t EMPTY_ROLE_MAP_ENTRY_INDEX = UINT8_MAX - 1;
const uint8_t LANDMARK_ROLE_MAP_ENTRY_INDEX = UINT8_MAX;

/**
 * Get the role map entry for a given DOM node. This will use the first
 * ARIA role if the role attribute provides a space delimited list of roles.
 *
 * @param aEl     [in] the DOM node to get the role map entry for
 * @return        a pointer to the role map entry for the ARIA role, or nullptr
 *                if none
 */
const nsRoleMapEntry* GetRoleMap(dom::Element* aEl);

/*
 * Get the role map entry pointer's index for a given DOM node, skipping any
 * given roles. This will use the first valid ARIA role if the role attribute
 * provides a space delimited list of roles, excluding any given roles.
 *
 * @param aEl          [in] the DOM node to get the role map entry for
 * @param aRolesToSkip [in] the roles to skip when searching the role string
 * @return             the index of the pointer to the role map entry for the
 *                     ARIA role, or NO_ROLE_MAP_ENTRY_INDEX if none
 */
uint8_t GetFirstValidRoleMapIndexExcluding(
    dom::Element* aEl, std::initializer_list<nsStaticAtom*> aRolesToSkip);

/**
 * Get the role map entry pointer's index for a given DOM node. This will use
 * the first ARIA role if the role attribute provides a space delimited list of
 * roles.
 *
 * @param aEl     [in] the DOM node to get the role map entry for
 * @return        the index of the pointer to the role map entry for the ARIA
 *                role, or NO_ROLE_MAP_ENTRY_INDEX if none
 */
uint8_t GetRoleMapIndex(dom::Element* aEl);

/**
 * Get the role map entry pointer for a given role map entry index.
 *
 * @param aRoleMapIndex  [in] the role map index to get the role map entry
 *                       pointer for
 * @return               a pointer to the role map entry for the ARIA role,
 *                       or nullptr, if none
 */
const nsRoleMapEntry* GetRoleMapFromIndex(uint8_t aRoleMapIndex);

/**
 * Get the role map entry index for a given role map entry pointer. If the role
 * map entry is within sWAIRoleMaps, return the index within that array,
 * otherwise return one of the special index constants listed above.
 *
 * @param aRoleMap  [in] the role map entry pointer to get the index for
 * @return          the index of the pointer to the role map entry, or
 *                  NO_ROLE_MAP_ENTRY_INDEX if none
 */
uint8_t GetIndexFromRoleMap(const nsRoleMapEntry* aRoleMap);

/**
 * Determine whether a role map entry index is valid.
 */
bool IsRoleMapIndexValid(uint8_t aRoleMapIndex);

/**
 * Return accessible state from ARIA universal states applied to the given
 * element.
 */
uint64_t UniversalStatesFor(dom::Element* aElement);

/**
 * Get the ARIA attribute characteristics for a given ARIA attribute.
 *
 * @param aAtom  ARIA attribute
 * @return       A bitflag representing the attribute characteristics
 *               (see above for possible bit masks, prefixed "ATTR_")
 */
uint8_t AttrCharacteristicsFor(nsAtom* aAtom);

/**
 * Return true if the element has defined aria-hidden
 * and should not be ignored per ShouldIgnoreARIAHidden.
 */
bool IsValidARIAHidden(nsIContent* aContent);

/**
 * This function calls into the function above. It verifies the validity
 * of any `aria-hidden` specified on the given Doc Accessible's
 * mContent, as well as on the root element of mContent's owner
 * doc.
 */
bool IsValidARIAHidden(DocAccessible* aDocAcc);

/**
 * Return true if the element should render its subtree
 * regardless of the presence of aria-hidden.
 */
bool ShouldIgnoreARIAHidden(nsIContent* aContent);

/**
 * Get the role map entry for a given ARIA role.
 */
const nsRoleMapEntry* GetRoleMap(const nsStaticAtom* aAriaRole);

/**
 * Represents a simple enumerator for iterating through ARIA attributes
 * exposed as object attributes on a given accessible.
 */
class AttrIterator {
 public:
  explicit AttrIterator(nsIContent* aContent);

  bool Next();

  nsAtom* AttrName() const;

  void AttrValue(nsAString& aAttrValue) const;

  /**
   * Expose this ARIA attribute in a specified AccAttributes. The appropriate
   * type will be used for the attribute; e.g. an atom for a token value.
   */
  bool ExposeAttr(AccAttributes* aTargetAttrs) const;

 private:
  AttrIterator() = delete;
  AttrIterator(const AttrIterator&) = delete;
  AttrIterator& operator=(const AttrIterator&) = delete;

  dom::Element* mElement;

  bool mIteratingDefaults;
  nsTHashSet<RefPtr<nsAtom>> mOverriddenAttrs;

  const AttrArray* mAttrs;
  uint32_t mAttrIdx;
  uint32_t mAttrCount;
  RefPtr<nsAtom> mAttrAtom;
  uint8_t mAttrCharacteristics;
};

class AttrWithCharacteristicsIterator {
 public:
  explicit AttrWithCharacteristicsIterator(uint8_t aCharacteristics)
      : mIdx(-1), mCharacteristics(aCharacteristics) {}

  bool Next();

  nsStaticAtom* AttrName() const;

 private:
  AttrWithCharacteristicsIterator() = delete;
  AttrWithCharacteristicsIterator(const AttrWithCharacteristicsIterator&) =
      delete;
  AttrWithCharacteristicsIterator& operator=(
      const AttrWithCharacteristicsIterator&) = delete;

  int32_t mIdx;
  uint8_t mCharacteristics;
};

}  // namespace aria
}  // namespace a11y
}  // namespace mozilla

#endif
