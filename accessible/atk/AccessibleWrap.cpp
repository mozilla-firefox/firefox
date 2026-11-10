/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AccessibleWrap.h"

#include "AccAttributes.h"
#include "ApplicationAccessibleWrap.h"
#include "DocAccessibleParent.h"
#include "InterfaceInitFuncs.h"
#include "LocalAccessible-inl.h"
#include "OuterDocAccessible.h"
#include "Relation.h"
#include "RemoteAccessible.h"
#include "RootAccessible.h"
#include "States.h"
#include "mozilla/Sprintf.h"
#include "mozilla/a11y/PDocAccessible.h"
#include "mozilla/a11y/Platform.h"
#include "mozilla/a11y/TableAccessible.h"
#include "mozilla/a11y/TableCellAccessible.h"
#include "nsAccUtils.h"
#include "nsAccessibilityService.h"
#include "nsComponentManagerUtils.h"
#include "nsIAccessibleAnnouncementEvent.h"
#include "nsISimpleEnumerator.h"
#include "nsMai.h"
#include "nsMaiHyperlink.h"
#include "nsStateMap.h"
#include "nsString.h"

using namespace mozilla;
using namespace mozilla::a11y;

MaiAtkObject::EAvailableAtkSignals MaiAtkObject::gAvailableAtkSignals =
    eUnknown;

// defined in ApplicationAccessibleWrap.cpp
extern "C" GType g_atk_hyperlink_impl_type;

/* MaiAtkObject */

enum {
  ACTIVATE,
  CREATE,
  DEACTIVATE,
  DESTROY,
  MAXIMIZE,
  MINIMIZE,
  RESIZE,
  RESTORE,
  LAST_SIGNAL
};

enum MaiInterfaceType {
  MAI_INTERFACE_COMPONENT, /* 0 */
  MAI_INTERFACE_ACTION,
  MAI_INTERFACE_VALUE,
  MAI_INTERFACE_EDITABLE_TEXT,
  MAI_INTERFACE_HYPERTEXT,
  MAI_INTERFACE_HYPERLINK_IMPL,
  MAI_INTERFACE_SELECTION,
  MAI_INTERFACE_TABLE,
  MAI_INTERFACE_TEXT,
  MAI_INTERFACE_DOCUMENT,
  MAI_INTERFACE_IMAGE, /* 10 */
  MAI_INTERFACE_TABLE_CELL
};

static GType GetAtkTypeForMai(MaiInterfaceType type) {
  switch (type) {
    case MAI_INTERFACE_COMPONENT:
      return ATK_TYPE_COMPONENT;
    case MAI_INTERFACE_ACTION:
      return ATK_TYPE_ACTION;
    case MAI_INTERFACE_VALUE:
      return ATK_TYPE_VALUE;
    case MAI_INTERFACE_EDITABLE_TEXT:
      return ATK_TYPE_EDITABLE_TEXT;
    case MAI_INTERFACE_HYPERTEXT:
      return ATK_TYPE_HYPERTEXT;
    case MAI_INTERFACE_HYPERLINK_IMPL:
      return g_atk_hyperlink_impl_type;
    case MAI_INTERFACE_SELECTION:
      return ATK_TYPE_SELECTION;
    case MAI_INTERFACE_TABLE:
      return ATK_TYPE_TABLE;
    case MAI_INTERFACE_TEXT:
      return ATK_TYPE_TEXT;
    case MAI_INTERFACE_DOCUMENT:
      return ATK_TYPE_DOCUMENT;
    case MAI_INTERFACE_IMAGE:
      return ATK_TYPE_IMAGE;
    case MAI_INTERFACE_TABLE_CELL:
      MOZ_ASSERT(false);
  }
  return G_TYPE_INVALID;
}

#define NON_USER_EVENT ":system"

// The atk interfaces we can expose without checking what version of ATK we are
// dealing with.  At the moment AtkTableCell is the only interface we can't
// always expose.
static const GInterfaceInfo atk_if_infos[] = {
    {(GInterfaceInitFunc)componentInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)actionInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)valueInterfaceInitCB, (GInterfaceFinalizeFunc) nullptr,
     nullptr},
    {(GInterfaceInitFunc)editableTextInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)hypertextInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)hyperlinkImplInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)selectionInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)tableInterfaceInitCB, (GInterfaceFinalizeFunc) nullptr,
     nullptr},
    {(GInterfaceInitFunc)textInterfaceInitCB, (GInterfaceFinalizeFunc) nullptr,
     nullptr},
    {(GInterfaceInitFunc)documentInterfaceInitCB,
     (GInterfaceFinalizeFunc) nullptr, nullptr},
    {(GInterfaceInitFunc)imageInterfaceInitCB, (GInterfaceFinalizeFunc) nullptr,
     nullptr}};

static GQuark quark_mai_hyperlink = 0;

AtkHyperlink* MaiAtkObject::GetAtkHyperlink() {
  NS_ASSERTION(quark_mai_hyperlink, "quark_mai_hyperlink not initialized");
  MaiHyperlink* maiHyperlink =
      (MaiHyperlink*)g_object_get_qdata(G_OBJECT(this), quark_mai_hyperlink);
  if (!maiHyperlink) {
    maiHyperlink = new MaiHyperlink(acc);
    g_object_set_qdata(G_OBJECT(this), quark_mai_hyperlink, maiHyperlink);
  }

  return maiHyperlink->GetAtkHyperlink();
}

void MaiAtkObject::Shutdown() {
  acc = nullptr;
  MaiHyperlink* maiHyperlink =
      (MaiHyperlink*)g_object_get_qdata(G_OBJECT(this), quark_mai_hyperlink);
  if (maiHyperlink) {
    delete maiHyperlink;
    g_object_set_qdata(G_OBJECT(this), quark_mai_hyperlink, nullptr);
  }
}

struct MaiAtkObjectClass {
  AtkObjectClass parent_class;
};

static guint mai_atk_object_signals[LAST_SIGNAL] = {
    0,
};

static void MaybeFireNameChange(AtkObject* aAtkObj, const nsString& aNewName);

G_BEGIN_DECLS
/* callbacks for MaiAtkObject */
static void classInitCB(AtkObjectClass* aClass);
static void initializeCB(AtkObject* aAtkObj, gpointer aData);
static void finalizeCB(GObject* aObj);

/* callbacks for AtkObject virtual functions */
static const gchar* getNameCB(AtkObject* aAtkObj);
/* getDescriptionCB is also used by image interface */
const gchar* getDescriptionCB(AtkObject* aAtkObj);
static AtkRole getRoleCB(AtkObject* aAtkObj);
static AtkAttributeSet* getAttributesCB(AtkObject* aAtkObj);
static const gchar* GetLocaleCB(AtkObject*);
static AtkObject* getParentCB(AtkObject* aAtkObj);
static gint getChildCountCB(AtkObject* aAtkObj);
static AtkObject* refChildCB(AtkObject* aAtkObj, gint aChildIndex);
static gint getIndexInParentCB(AtkObject* aAtkObj);
static AtkStateSet* refStateSetCB(AtkObject* aAtkObj);
static AtkRelationSet* refRelationSetCB(AtkObject* aAtkObj);

/* the missing atkobject virtual functions */
/*
  static AtkLayer            getLayerCB(AtkObject *aAtkObj);
  static gint                getMdiZorderCB(AtkObject *aAtkObj);
  static void                SetNameCB(AtkObject *aAtkObj,
  const gchar *name);
  static void                SetDescriptionCB(AtkObject *aAtkObj,
  const gchar *description);
  static void                SetParentCB(AtkObject *aAtkObj,
  AtkObject *parent);
  static void                SetRoleCB(AtkObject *aAtkObj,
  AtkRole role);
  static guint               ConnectPropertyChangeHandlerCB(
  AtkObject  *aObj,
  AtkPropertyChangeHandler *handler);
  static void                RemovePropertyChangeHandlerCB(
  AtkObject *aAtkObj,
  guint handler_id);
  static void                InitializeCB(AtkObject *aAtkObj,
  gpointer data);
  static void                ChildrenChangedCB(AtkObject *aAtkObj,
  guint change_index,
  gpointer changed_child);
  static void                FocusEventCB(AtkObject *aAtkObj,
  gboolean focus_in);
  static void                PropertyChangeCB(AtkObject *aAtkObj,
  AtkPropertyValues *values);
  static void                StateChangeCB(AtkObject *aAtkObj,
  const gchar *name,
  gboolean state_set);
  static void                VisibleDataChangedCB(AtkObject *aAtkObj);
*/
G_END_DECLS

static GType GetMaiAtkType(uint16_t interfacesBits);
static const char* GetUniqueMaiAtkTypeName(uint16_t interfacesBits);

static gpointer parent_class = nullptr;

GType mai_atk_object_get_type(void) {
  static GType type = 0;

  if (!type) {
    static const GTypeInfo tinfo = {
        sizeof(MaiAtkObjectClass),
        (GBaseInitFunc) nullptr,
        (GBaseFinalizeFunc) nullptr,
        (GClassInitFunc)classInitCB,
        (GClassFinalizeFunc) nullptr,
        nullptr,              /* class data */
        sizeof(MaiAtkObject), /* instance size */
        0,                    /* nb preallocs */
        (GInstanceInitFunc) nullptr,
        nullptr /* value table */
    };

    type = g_type_register_static(ATK_TYPE_OBJECT, "MaiAtkObject", &tinfo,
                                  GTypeFlags(0));
    quark_mai_hyperlink = g_quark_from_static_string("MaiHyperlink");
  }
  return type;
}

AccessibleWrap::AccessibleWrap(nsIContent* aContent, DocAccessible* aDoc)
    : LocalAccessible(aContent, aDoc), mAtkObject(nullptr) {}

AccessibleWrap::~AccessibleWrap() {
  NS_ASSERTION(!mAtkObject, "ShutdownAtkObject() is not called");
}

void AccessibleWrap::ShutdownAtkObject() {
  if (!mAtkObject) return;

  NS_ASSERTION(IS_MAI_OBJECT(mAtkObject), "wrong type of atk object");
  if (IS_MAI_OBJECT(mAtkObject)) MAI_ATK_OBJECT(mAtkObject)->Shutdown();

  g_object_unref(mAtkObject);
  mAtkObject = nullptr;
}

void AccessibleWrap::Shutdown() {
  ShutdownAtkObject();
  LocalAccessible::Shutdown();
}

static uint16_t CreateMaiInterfaces(Accessible* aAccessible) {
  uint16_t interfaces = 1 << MAI_INTERFACE_COMPONENT;

  if (aAccessible->IsHyperText() && aAccessible->IsTextRole()) {
    interfaces |= (1 << MAI_INTERFACE_HYPERTEXT) | (1 << MAI_INTERFACE_TEXT) |
                  (1 << MAI_INTERFACE_EDITABLE_TEXT);
  }

  if (aAccessible->IsLink()) {
    interfaces |= 1 << MAI_INTERFACE_HYPERLINK_IMPL;
  }

  if (aAccessible->HasNumericValue()) {
    interfaces |= 1 << MAI_INTERFACE_VALUE;
  }

  if (aAccessible->IsTable()) {
    interfaces |= 1 << MAI_INTERFACE_TABLE;
  }

  if (aAccessible->IsTableCell()) {
    interfaces |= 1 << MAI_INTERFACE_TABLE_CELL;
  }

  const nsRoleMapEntry* roleMapEntry = aAccessible->ARIARoleMap();
  if (aAccessible->IsImage() ||
      (roleMapEntry && roleMapEntry->role == roles::GRAPHIC)) {
    interfaces |= 1 << MAI_INTERFACE_IMAGE;
  }

  if (aAccessible->IsDoc()) {
    interfaces |= 1 << MAI_INTERFACE_DOCUMENT;
  }

  if (aAccessible->IsSelect()) {
    interfaces |= 1 << MAI_INTERFACE_SELECTION;
  }

  // XXX: Always include the action interface because aria-actions
  // can define actions mid-life.
  interfaces |= 1 << MAI_INTERFACE_ACTION;

  return interfaces;
}

void AccessibleWrap::GetNativeInterface(void** aOutAccessible) {
  *aOutAccessible = nullptr;

  if (!mAtkObject) {
    if (IsDefunct() || IsText()) {
      // We don't create ATK objects for node which has been shutdown or
      // plain text leaves
      return;
    }

    GType type = GetMaiAtkType(CreateMaiInterfaces(this));
    if (!type) return;

    mAtkObject = reinterpret_cast<AtkObject*>(g_object_new(type, nullptr));
    if (!mAtkObject) return;

    atk_object_initialize(mAtkObject, static_cast<Accessible*>(this));
    mAtkObject->role = ATK_ROLE_INVALID;
    mAtkObject->layer = ATK_LAYER_INVALID;
  }

  *aOutAccessible = mAtkObject;
}

AtkObject* AccessibleWrap::GetAtkObject(void) {
  void* atkObj = nullptr;
  GetNativeInterface(&atkObj);
  return static_cast<AtkObject*>(atkObj);
}

// Get AtkObject from LocalAccessible interface
/* static */
AtkObject* AccessibleWrap::GetAtkObject(LocalAccessible* acc) {
  void* atkObjPtr = nullptr;
  acc->GetNativeInterface(&atkObjPtr);
  return atkObjPtr ? ATK_OBJECT(atkObjPtr) : nullptr;
}

static GType GetMaiAtkType(uint16_t interfacesBits) {
  GType type;
  static const GTypeInfo tinfo = {
      sizeof(MaiAtkObjectClass),
      (GBaseInitFunc) nullptr,
      (GBaseFinalizeFunc) nullptr,
      (GClassInitFunc) nullptr,
      (GClassFinalizeFunc) nullptr,
      nullptr,              /* class data */
      sizeof(MaiAtkObject), /* instance size */
      0,                    /* nb preallocs */
      (GInstanceInitFunc) nullptr,
      nullptr /* value table */
  };

  /*
   * The members we use to register GTypes are GetAtkTypeForMai
   * and atk_if_infos, which are constant values to each MaiInterface
   * So we can reuse the registered GType when having
   * the same MaiInterface types.
   */
  const char* atkTypeName = GetUniqueMaiAtkTypeName(interfacesBits);
  type = g_type_from_name(atkTypeName);
  if (type) {
    return type;
  }

  /*
   * gobject limits the number of types that can directly derive from any
   * given object type to 4095.
   */
  static uint16_t typeRegCount = 0;
  if (typeRegCount++ >= 4095) {
    return G_TYPE_INVALID;
  }
  type = g_type_register_static(MAI_TYPE_ATK_OBJECT, atkTypeName, &tinfo,
                                GTypeFlags(0));

  for (uint32_t index = 0; index < std::size(atk_if_infos); index++) {
    if (interfacesBits & (1 << index)) {
      g_type_add_interface_static(type,
                                  GetAtkTypeForMai((MaiInterfaceType)index),
                                  &atk_if_infos[index]);
    }
  }

  // Special case AtkTableCell so we can check what version of Atk we are
  // dealing with.
  if (IsAtkVersionAtLeast(2, 12) &&
      (interfacesBits & (1 << MAI_INTERFACE_TABLE_CELL))) {
    const GInterfaceInfo cellInfo = {
        (GInterfaceInitFunc)tableCellInterfaceInitCB,
        (GInterfaceFinalizeFunc) nullptr, nullptr};
    g_type_add_interface_static(type, gAtkTableCellGetTypeFunc(), &cellInfo);
  }

  return type;
}

static const char* GetUniqueMaiAtkTypeName(uint16_t interfacesBits) {
#define MAI_ATK_TYPE_NAME_LEN (30) /* 10+sizeof(uint16_t)*8/4+1 < 30 */

  static gchar namePrefix[] = "MaiAtkType"; /* size = 10 */
  static gchar name[MAI_ATK_TYPE_NAME_LEN + 1];

  SprintfLiteral(name, "%s%x", namePrefix, interfacesBits);
  name[MAI_ATK_TYPE_NAME_LEN] = '\0';

  return name;
}

bool AccessibleWrap::IsValidObject() {
  // to ensure we are not shut down
  return !IsDefunct();
}

/* static functions for ATK callbacks */
void classInitCB(AtkObjectClass* aClass) {
  GObjectClass* gobject_class = G_OBJECT_CLASS(aClass);

  parent_class = g_type_class_peek_parent(aClass);

  aClass->get_name = getNameCB;
  aClass->get_description = getDescriptionCB;
  aClass->get_parent = getParentCB;
  aClass->get_n_children = getChildCountCB;
  aClass->ref_child = refChildCB;
  aClass->get_index_in_parent = getIndexInParentCB;
  aClass->get_role = getRoleCB;
  aClass->get_attributes = getAttributesCB;
  aClass->get_object_locale = GetLocaleCB;
  aClass->ref_state_set = refStateSetCB;
  aClass->ref_relation_set = refRelationSetCB;

  aClass->initialize = initializeCB;

  gobject_class->finalize = finalizeCB;

  mai_atk_object_signals[ACTIVATE] = g_signal_new(
      "activate", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[CREATE] = g_signal_new(
      "create", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[DEACTIVATE] = g_signal_new(
      "deactivate", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[DESTROY] = g_signal_new(
      "destroy", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[MAXIMIZE] = g_signal_new(
      "maximize", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[MINIMIZE] = g_signal_new(
      "minimize", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[RESIZE] = g_signal_new(
      "resize", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  mai_atk_object_signals[RESTORE] = g_signal_new(
      "restore", MAI_TYPE_ATK_OBJECT, G_SIGNAL_RUN_LAST,
      0, /* default signal handler */
      nullptr, nullptr, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

void initializeCB(AtkObject* aAtkObj, gpointer aData) {
  NS_ASSERTION((IS_MAI_OBJECT(aAtkObj)), "Invalid AtkObject");
  NS_ASSERTION(aData, "Invalid Data to init AtkObject");
  if (!aAtkObj || !aData) return;

  /* call parent init function */
  /* AtkObjectClass has not a "initialize" function now,
   * maybe it has later
   */

  if (ATK_OBJECT_CLASS(parent_class)->initialize) {
    ATK_OBJECT_CLASS(parent_class)->initialize(aAtkObj, aData);
  }

  /* initialize object */
  MAI_ATK_OBJECT(aAtkObj)->acc = static_cast<Accessible*>(aData);
}

void finalizeCB(GObject* aObj) {
  if (!IS_MAI_OBJECT(aObj)) return;
  NS_ASSERTION(!MAI_ATK_OBJECT(aObj)->acc, "acc NOT null");

  // call parent finalize function
  // finalize of GObjectClass will unref the accessible parent if has
  if (G_OBJECT_CLASS(parent_class)->finalize) {
    G_OBJECT_CLASS(parent_class)->finalize(aObj);
  }
}

const gchar* getNameCB(AtkObject* aAtkObj) {
  nsAutoString name;
  if (Accessible* acc = GetInternalObj(aAtkObj)) {
    acc->Name(name);
  } else {
    return nullptr;
  }

  // XXX Firing an event from here does not seem right
  MaybeFireNameChange(aAtkObj, name);

  return aAtkObj->name;
}

static void MaybeFireNameChange(AtkObject* aAtkObj, const nsString& aNewName) {
  NS_ConvertUTF16toUTF8 newNameUTF8(aNewName);
  if (aAtkObj->name && !strcmp(aAtkObj->name, newNameUTF8.get())) return;

  // Below we duplicate the functionality of atk_object_set_name(),
  // but without calling atk_object_get_name(). Instead of
  // atk_object_get_name() we directly access aAtkObj->name. This is because
  // atk_object_get_name() would call getNameCB() which would call
  // MaybeFireNameChange() (or atk_object_set_name() before this problem was
  // fixed) and we would get an infinite recursion.
  // See http://bugzilla.mozilla.org/733712

  // Do not notify for initial name setting.
  // See bug http://bugzilla.gnome.org/665870
  bool notify = !!aAtkObj->name;

  free(aAtkObj->name);
  aAtkObj->name = strdup(newNameUTF8.get());

  if (notify) g_object_notify(G_OBJECT(aAtkObj), "accessible-name");
}

const gchar* getDescriptionCB(AtkObject* aAtkObj) {
  nsAutoString uniDesc;
  if (Accessible* acc = GetInternalObj(aAtkObj)) {
    acc->Description(uniDesc);
  } else {
    return nullptr;
  }

  NS_ConvertUTF8toUTF16 objDesc(aAtkObj->description);
  if (!uniDesc.Equals(objDesc)) {
    atk_object_set_description(aAtkObj, NS_ConvertUTF16toUTF8(uniDesc).get());
  }

  return aAtkObj->description;
}

AtkRole getRoleCB(AtkObject* aAtkObj) {
  if (aAtkObj->role != ATK_ROLE_INVALID) return aAtkObj->role;

  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return ATK_ROLE_INVALID;
  }

#ifdef DEBUG
  if (AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj)) {
    NS_ASSERTION(nsAccUtils::IsTextInterfaceSupportCorrect(accWrap),
                 "Does not support Text interface when it should");
  }
#endif

#define ROLE(geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType, \
             nameRule)                                                      \
  case roles::geckoRole:                                                    \
    aAtkObj->role = atkRole;                                                \
    break;

  role geckoRole = acc->Role();

  switch (geckoRole) {
#include "RoleMap.inc"
    default:
      MOZ_CRASH("Unknown role.");
  }

#undef ROLE

  // Transform some gecko roles to more specific ATK roles

  if (geckoRole == role::DIALOG) {
    if (acc->IsARIARole(nsGkAtoms::alertdialog)) {
      aAtkObj->role = ATK_ROLE_ALERT;
    }
  }

  // Fallback to generic roles for backward compat. We expect host systems to
  // have ATK 2.14 or higher, since the minimum GTK version is 3.14 which was
  // released in the same cycle as ATK 2.14.

  if (!IsAtkVersionAtLeast(2, 16)) {
    switch (aAtkObj->role) {
      case ATK_ROLE_STATIC:
        aAtkObj->role = ATK_ROLE_TEXT;
        break;
      case ATK_ROLE_MATH_FRACTION:
      case ATK_ROLE_MATH_ROOT:
        aAtkObj->role = ATK_ROLE_SECTION;
        break;
      default:
        break;
    }
  }

  if (!IsAtkVersionAtLeast(2, 26)) {
    switch (aAtkObj->role) {
      case ATK_ROLE_FOOTNOTE:
        aAtkObj->role = ATK_ROLE_SECTION;
        break;
      default:
        break;
    }
  }

  if (!IsAtkVersionAtLeast(2, 34)) {
    switch (aAtkObj->role) {
      case ATK_ROLE_CONTENT_DELETION:
      case ATK_ROLE_CONTENT_INSERTION:
        aAtkObj->role = ATK_ROLE_SECTION;
        break;
      default:
        break;
    }
  }

  if (!IsAtkVersionAtLeast(2, 36)) {
    switch (aAtkObj->role) {
      case ATK_ROLE_MARK:
        aAtkObj->role = ATK_ROLE_TEXT;
        break;
      case ATK_ROLE_SUGGESTION:
      case ATK_ROLE_COMMENT:
        aAtkObj->role = ATK_ROLE_SECTION;
        break;
      default:
        break;
    }
  }

  return aAtkObj->role;
}

static AtkAttributeSet* ConvertToAtkAttributeSet(AccAttributes* aAttributes) {
  if (!aAttributes) {
    return nullptr;
  }

  AtkAttributeSet* objAttributeSet = nullptr;

  for (auto iter : *aAttributes) {
    nsAutoString name;
    iter.NameAsString(name);
    if (name.Equals(u"placeholder")) {
      name.AssignLiteral(u"placeholder-text");
    }

    nsAutoString value;
    iter.ValueAsString(value);

    AtkAttribute* objAttr = (AtkAttribute*)g_malloc(sizeof(AtkAttribute));
    objAttr->name = g_strdup(NS_ConvertUTF16toUTF8(name).get());
    objAttr->value = g_strdup(NS_ConvertUTF16toUTF8(value).get());
    objAttributeSet = g_slist_prepend(objAttributeSet, objAttr);
  }

  // libspi will free it
  return objAttributeSet;
}

AtkAttributeSet* getAttributesCB(AtkObject* aAtkObj) {
  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return nullptr;
  }
  RefPtr<AccAttributes> attributes = acc->Attributes();
  nsAccUtils::SetAccGroupAttrs(attributes, acc);
  return ConvertToAtkAttributeSet(attributes);
}

const gchar* GetLocaleCB(AtkObject* aAtkObj) {
  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return nullptr;
  }

  nsAutoString locale;
  acc->Language(locale);
  return AccessibleWrap::ReturnString(locale);
}

AtkObject* getParentCB(AtkObject* aAtkObj) {
  if (aAtkObj->accessible_parent) return aAtkObj->accessible_parent;

  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return nullptr;
  }

  Accessible* parent = acc->Parent();
  AtkObject* atkParent = parent ? GetWrapperFor(parent) : nullptr;
  if (atkParent) atk_object_set_parent(aAtkObj, atkParent);

  return aAtkObj->accessible_parent;
}

gint getChildCountCB(AtkObject* aAtkObj) {
  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc || nsAccUtils::MustPrune(acc)) {
    return 0;
  }
  return static_cast<gint>(acc->EmbeddedChildCount());
}

AtkObject* refChildCB(AtkObject* aAtkObj, gint aChildIndex) {
  // aChildIndex should not be less than zero
  if (aChildIndex < 0) {
    return nullptr;
  }

  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc || nsAccUtils::MustPrune(acc)) {
    return nullptr;
  }
  Accessible* accChild = acc->EmbeddedChildAt(aChildIndex);
  if (!accChild) {
    return nullptr;
  }

  AtkObject* childAtkObj = GetWrapperFor(accChild);
  NS_ASSERTION(childAtkObj, "Fail to get AtkObj");
  if (!childAtkObj) {
    return nullptr;
  }

  g_object_ref(childAtkObj);

  if (aAtkObj != childAtkObj->accessible_parent) {
    atk_object_set_parent(childAtkObj, aAtkObj);
  }

  return childAtkObj;
}

gint getIndexInParentCB(AtkObject* aAtkObj) {
  // We don't use LocalAccessible::IndexInParent() because we don't include text
  // leaf nodes as children in ATK.
  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return -1;
  }
  if (acc->IsDoc()) {
    return 0;
  }
  Accessible* parent = acc->Parent();
  if (!parent) {
    return -1;
  }
  return parent->IndexOfEmbeddedChild(acc);
}

static void TranslateStates(uint64_t aState, roles::Role aRole,
                            AtkStateSet* aStateSet) {
  // https://w3c.github.io/aria/core-aam/#ariaReadonlyTrue
  if ((aState & states::READONLY)) {
    // If an accessible is read-only it should not have an editable state.
    if (aRole != roles::LISTITEM) {
      // ...however, we don't do this for list items because Gecko always
      // exposes those as read only.
      aState &= ~states::EDITABLE;
    } else {
      aState &= ~states::READONLY;
    }

    // A read-only accessible should not be checkable either.
    aState &= ~states::CHECKABLE;
  }

  // Convert every state to an entry in AtkStateMap
  uint64_t bitMask = 1;
  for (auto stateIndex : gAtkStateMap) {
    if (stateIndex.atkState) {  // There's potentially an ATK state for this
      bool isStateOn = (aState & bitMask) != 0;
      if (stateIndex.stateMapEntryType == kMapOpposite) {
        isStateOn = !isStateOn;
      }
      if (isStateOn) {
        atk_state_set_add_state(aStateSet, stateIndex.atkState);
      }
    }
    bitMask <<= 1;
  }
}

AtkStateSet* refStateSetCB(AtkObject* aAtkObj) {
  AtkStateSet* state_set = nullptr;
  state_set = ATK_OBJECT_CLASS(parent_class)->ref_state_set(aAtkObj);

  if (Accessible* acc = GetInternalObj(aAtkObj)) {
    TranslateStates(acc->State(), acc->Role(), state_set);
  } else {
    TranslateStates(states::DEFUNCT, roles::NOTHING, state_set);
  }

  return state_set;
}

static void UpdateAtkRelation(RelationType aType, Accessible* aAcc,
                              AtkRelationType aAtkType,
                              AtkRelationSet* aAtkSet) {
  if (aAtkType == ATK_RELATION_NULL) return;

  AtkRelation* atkRelation =
      atk_relation_set_get_relation_by_type(aAtkSet, aAtkType);
  if (atkRelation) atk_relation_set_remove(aAtkSet, atkRelation);

  Relation rel(aAcc->RelationByType(aType));
  nsTArray<AtkObject*> targets;
  Accessible* tempAcc = nullptr;
  while ((tempAcc = rel.Next())) {
    targets.AppendElement(GetWrapperFor(tempAcc));
  }

  if (targets.Length()) {
    atkRelation =
        atk_relation_new(targets.Elements(), targets.Length(), aAtkType);
    atk_relation_set_add(aAtkSet, atkRelation);
    g_object_unref(atkRelation);
  }
}

AtkRelationSet* refRelationSetCB(AtkObject* aAtkObj) {
  AtkRelationSet* relation_set =
      ATK_OBJECT_CLASS(parent_class)->ref_relation_set(aAtkObj);

  Accessible* acc = GetInternalObj(aAtkObj);
  if (!acc) {
    return relation_set;
  }

#define RELATIONTYPE(geckoType, geckoTypeName, atkType, msaaType, ia2Type) \
  UpdateAtkRelation(RelationType::geckoType, acc, atkType, relation_set);

#include "RelationTypeMap.inc"

#undef RELATIONTYPE

  return relation_set;
}

// Check if aAtkObj is a valid MaiAtkObject, and return the AccessibleWrap
// for it.
AccessibleWrap* GetAccessibleWrap(AtkObject* aAtkObj) {
  NS_ENSURE_TRUE(IS_MAI_OBJECT(aAtkObj), nullptr);

  // If we're working with an ATK object, we need to convert the Accessible
  // back to an AccessibleWrap:
  Accessible* storedAcc = MAI_ATK_OBJECT(aAtkObj)->acc;
  if (!storedAcc) {
    return nullptr;
  }
  auto* accWrap = static_cast<AccessibleWrap*>(storedAcc->AsLocal());

  // Check if the accessible was deconstructed.
  if (!accWrap) return nullptr;

  NS_ENSURE_TRUE(accWrap->GetAtkObject() == aAtkObj, nullptr);

  AccessibleWrap* appAccWrap = ApplicationAcc();
  if (appAccWrap != accWrap && !accWrap->IsValidObject()) {
    return nullptr;
  }

  return accWrap;
}

RemoteAccessible* GetProxy(AtkObject* aObj) {
  Accessible* acc = GetInternalObj(aObj);
  if (!acc) {
    return nullptr;
  }

  return acc->AsRemote();
}

Accessible* GetInternalObj(AtkObject* aObj) {
  if (!aObj || !IS_MAI_OBJECT(aObj)) return nullptr;

  return MAI_ATK_OBJECT(aObj)->acc;
}

AtkObject* GetWrapperFor(Accessible* aAcc) {
  if (!aAcc) {
    return nullptr;
  }

  if (aAcc->IsRemote()) {
    return reinterpret_cast<AtkObject*>(aAcc->AsRemote()->GetWrapper());
  }

  return AccessibleWrap::GetAtkObject(aAcc->AsLocal());
}

void a11y::ProxyCreated(RemoteAccessible* aProxy) {
  MOZ_ASSERT(aProxy->RemoteParent() || aProxy->IsDoc(),
             "Need parent to check for HyperLink interface");
  GType type = GetMaiAtkType(CreateMaiInterfaces(aProxy));
  NS_ASSERTION(type, "why don't we have a type!");

  AtkObject* obj = reinterpret_cast<AtkObject*>(g_object_new(type, nullptr));
  if (!obj) return;

  atk_object_initialize(obj, static_cast<Accessible*>(aProxy));
  obj->role = ATK_ROLE_INVALID;
  obj->layer = ATK_LAYER_INVALID;
  aProxy->SetWrapper(reinterpret_cast<uintptr_t>(obj));
}

void a11y::ProxyDestroyed(RemoteAccessible* aProxy) {
  auto obj = reinterpret_cast<MaiAtkObject*>(aProxy->GetWrapper());
  if (!obj) {
    return;
  }

  obj->Shutdown();
  g_object_unref(obj);
  aProxy->SetWrapper(0);
}

void a11y::PlatformEvent(Accessible* aTarget, uint32_t aEventType) {
  AtkObject* wrapper = GetWrapperFor(aTarget);

  switch (aEventType) {
    case nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE:
      if (aTarget->IsDoc()) {
        g_signal_emit_by_name(wrapper, "load_complete");
      }
      // XXX - Handle native dialog accessibles.
      if (!aTarget->IsRoot() && aTarget->HasARIARole() &&
          aTarget->Role() == roles::DIALOG) {
        guint id = g_signal_lookup("activate", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(wrapper, id, 0);
      }
      break;
    case nsIAccessibleEvent::EVENT_DOCUMENT_RELOAD:
      if (aTarget->IsDoc()) {
        g_signal_emit_by_name(wrapper, "reload");
      }
      break;
    case nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_STOPPED:
      if (aTarget->IsDoc()) {
        g_signal_emit_by_name(wrapper, "load_stopped");
      }
      break;
    case nsIAccessibleEvent::EVENT_MENUPOPUP_START:
      atk_focus_tracker_notify(wrapper);  // fire extra focus event
      atk_object_notify_state_change(wrapper, ATK_STATE_VISIBLE, true);
      atk_object_notify_state_change(wrapper, ATK_STATE_SHOWING, true);
      break;
    case nsIAccessibleEvent::EVENT_MENUPOPUP_END:
      atk_object_notify_state_change(wrapper, ATK_STATE_VISIBLE, false);
      atk_object_notify_state_change(wrapper, ATK_STATE_SHOWING, false);
      break;
    case nsIAccessibleEvent::EVENT_ALERT:
      // A hack using state change showing events as alert events.
      atk_object_notify_state_change(wrapper, ATK_STATE_SHOWING, true);
      break;
    case nsIAccessibleEvent::EVENT_VALUE_CHANGE:
    case nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE:
      if (aTarget->HasNumericValue()) {
        // Make sure this is a numeric value. Don't fire for string value
        // changes (e.g. text editing) ATK values are always numeric.
        g_object_notify((GObject*)wrapper, "accessible-value");
      }
      break;
    case nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED:
      g_signal_emit_by_name(wrapper, "text_selection_changed");
      break;
    case nsIAccessibleEvent::EVENT_SELECTION_WITHIN:
      g_signal_emit_by_name(wrapper, "selection_changed");
      break;
    case nsIAccessibleEvent::EVENT_TEXT_ATTRIBUTE_CHANGED:
      g_signal_emit_by_name(wrapper, "text-attributes-changed");
      break;
    case nsIAccessibleEvent::EVENT_NAME_CHANGE: {
      // Don't want to passively activate the cache because a name changed.
      CacheDomainActivationBlocker cacheBlocker;
      nsAutoString newName;
      aTarget->Name(newName);
      MaybeFireNameChange(wrapper, newName);
      break;
    }
    case nsIAccessibleEvent::EVENT_WINDOW_ACTIVATE: {
      guint id = g_signal_lookup("activate", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(wrapper, id, 0);
      // Always fire a current focus event after activation.
      FocusMgr()->ForceFocusEvent();
      break;
    }
    case nsIAccessibleEvent::EVENT_WINDOW_DEACTIVATE: {
      guint id = g_signal_lookup("deactivate", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(wrapper, id, 0);
      break;
    }
    case nsIAccessibleEvent::EVENT_WINDOW_MAXIMIZE: {
      guint id = g_signal_lookup("maximize", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(wrapper, id, 0);
      break;
    }
    case nsIAccessibleEvent::EVENT_WINDOW_MINIMIZE: {
      guint id = g_signal_lookup("minimize", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(wrapper, id, 0);
      break;
    }
    case nsIAccessibleEvent::EVENT_WINDOW_RESTORE: {
      guint id = g_signal_lookup("restore", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(wrapper, id, 0);
      break;
    }
  }
}

void a11y::PlatformStateChangeEvent(Accessible* aTarget, uint64_t aState,
                                    bool aEnabled) {
  MaiAtkObject* atkObj = MAI_ATK_OBJECT(GetWrapperFor(aTarget));
  atkObj->FireStateChangeEvent(aState, aEnabled);
}

void a11y::PlatformFocusEvent(Accessible* aTarget) {
  AtkObject* wrapper = GetWrapperFor(aTarget);

  // XXX Do we really need this check? If so, do we need a similar check for
  // RemoteAccessible?
  if (LocalAccessible* localTarget = aTarget->AsLocal()) {
    a11y::RootAccessible* rootAcc = localTarget->RootAccessible();
    if (!rootAcc || !rootAcc->IsActivated()) {
      return;
    }
  }

  atk_focus_tracker_notify(wrapper);
  atk_object_notify_state_change(wrapper, ATK_STATE_FOCUSED, true);
}

void a11y::PlatformCaretMoveEvent(Accessible* aTarget, int32_t aOffset,
                                  bool /*aIsSelectionCollapsed*/,
                                  int32_t /*aGranularity*/,
                                  bool /*aFromUser*/) {
  AtkObject* wrapper = GetWrapperFor(aTarget);
  g_signal_emit_by_name(wrapper, "text_caret_moved", aOffset);
}

void MaiAtkObject::FireStateChangeEvent(uint64_t aState, bool aEnabled) {
  auto state = aState;
  int32_t stateIndex = -1;
  while (state > 0) {
    ++stateIndex;
    state >>= 1;
  }

  MOZ_ASSERT(
      stateIndex >= 0 && stateIndex < static_cast<int32_t>(gAtkStateMapLen),
      "No ATK state for internal state was found");
  if (stateIndex < 0 || stateIndex >= static_cast<int32_t>(gAtkStateMapLen)) {
    return;
  }

  if (gAtkStateMap[stateIndex].atkState != kNone) {
    MOZ_ASSERT(gAtkStateMap[stateIndex].stateMapEntryType != kNoStateChange,
               "State changes should not fired for this state");

    if (gAtkStateMap[stateIndex].stateMapEntryType == kMapOpposite) {
      aEnabled = !aEnabled;
    }

    // Fire state change for first state if there is one to map
    atk_object_notify_state_change(&parent, gAtkStateMap[stateIndex].atkState,
                                   aEnabled);
  }
}

void a11y::PlatformTextChangeEvent(Accessible* aTarget, const nsAString& aStr,
                                   int32_t aStart, uint32_t aLen,
                                   bool aIsInsert, bool aFromUser) {
  // The text change signals are defined by AtkText, which is only implemented
  // by objects with a text role.
  if (!aTarget->IsTextRole()) {
    return;
  }
  MaiAtkObject* atkObj = MAI_ATK_OBJECT(GetWrapperFor(aTarget));
  atkObj->FireTextChangeEvent(aStr, aStart, aLen, aIsInsert, aFromUser);
}

#define OLD_TEXT_INSERTED "text_changed::insert"
#define OLD_TEXT_REMOVED "text_changed::delete"
static const char* oldTextChangeStrings[2][2] = {
    {OLD_TEXT_REMOVED NON_USER_EVENT, OLD_TEXT_INSERTED NON_USER_EVENT},
    {OLD_TEXT_REMOVED, OLD_TEXT_INSERTED}};

#define TEXT_INSERTED "text-insert"
#define TEXT_REMOVED "text-remove"
#define NON_USER_DETAIL "::system"
static const char* textChangedStrings[2][2] = {
    {TEXT_REMOVED NON_USER_DETAIL, TEXT_INSERTED NON_USER_DETAIL},
    {TEXT_REMOVED, TEXT_INSERTED}};

void MaiAtkObject::FireTextChangeEvent(const nsAString& aStr, int32_t aStart,
                                       uint32_t aLen, bool aIsInsert,
                                       bool aFromUser) {
  if (gAvailableAtkSignals == eUnknown) {
    gAvailableAtkSignals = g_signal_lookup("text-insert", G_OBJECT_TYPE(this))
                               ? eHaveNewAtkTextSignals
                               : eNoNewAtkSignals;
  }

  if (gAvailableAtkSignals == eNoNewAtkSignals) {
    // XXX remove this code and the gHaveNewTextSignals check when we can
    // stop supporting old atk since it doesn't really work anyway
    // see bug 619002
    const char* signal_name = oldTextChangeStrings[aFromUser][aIsInsert];
    g_signal_emit_by_name(this, signal_name, aStart, aLen);
  } else {
    const char* signal_name = textChangedStrings[aFromUser][aIsInsert];
    g_signal_emit_by_name(this, signal_name, aStart, aLen,
                          NS_ConvertUTF16toUTF8(aStr).get());
  }
}

void a11y::PlatformShowHideEvent(Accessible* aTarget, Accessible* aParent,
                                 bool aInsert, bool aFromUser) {
  AtkObject* atkObj = GetWrapperFor(aTarget);
  if (!aInsert) {
    // XXX - Handle native dialog accessibles.
    if (!aTarget->IsRoot() && aTarget->HasARIARole() &&
        aTarget->Role() == roles::DIALOG) {
      guint id = g_signal_lookup("deactivate", MAI_TYPE_ATK_OBJECT);
      g_signal_emit(atkObj, id, 0);
    }
  }

  MaiAtkObject* obj = MAI_ATK_OBJECT(atkObj);
  obj->FireAtkShowHideEvent(GetWrapperFor(aParent), aInsert, aFromUser);
}

#define ADD_EVENT "children_changed::add"
#define HIDE_EVENT "children_changed::remove"

static const char* kMutationStrings[2][2] = {
    {HIDE_EVENT NON_USER_EVENT, ADD_EVENT NON_USER_EVENT},
    {HIDE_EVENT, ADD_EVENT},
};

void MaiAtkObject::FireAtkShowHideEvent(AtkObject* aParent, bool aIsAdded,
                                        bool aFromUser) {
  if (!aParent) {
    // XXX ATK needs a parent for these events. However, we might have already
    // unbound from the parent by the time we fire a hide event. Ideally, we
    // need to find a way to keep the parent around, but ATK clients don't seem
    // to care about these missing events.
    MOZ_ASSERT(!aIsAdded);
    return;
  }
  int32_t indexInParent = getIndexInParentCB(&this->parent);
  const char* signal_name = kMutationStrings[aFromUser][aIsAdded];
  g_signal_emit_by_name(aParent, signal_name, indexInParent, this, nullptr);
}

void a11y::PlatformSelectionEvent(Accessible*, Accessible* aWidget, uint32_t) {
  MaiAtkObject* obj = MAI_ATK_OBJECT(GetWrapperFor(aWidget));
  g_signal_emit_by_name(obj, "selection_changed");
}

// XXX Taken from atkobject.h in ATK 2.57. Updating ATK causes a plethora of
// build errors which aren't worth fixing for a single enum.
typedef enum { ATK_LIVE_NONE, ATK_LIVE_POLITE, ATK_LIVE_ASSERTIVE } AtkLive;

void a11y::PlatformAnnouncementEvent(Accessible* aTarget,
                                     const nsAString& aAnnouncement,
                                     uint16_t aPriority) {
  if (!IsAtkVersionAtLeast(2, 50)) {
    return;
  }
  AtkObject* wrapper = GetWrapperFor(aTarget);
  g_signal_emit_by_name(wrapper, "notification",
                        NS_ConvertUTF16toUTF8(aAnnouncement).get(),
                        aPriority == nsIAccessibleAnnouncementEvent::ASSERTIVE
                            ? ATK_LIVE_ASSERTIVE
                            : ATK_LIVE_POLITE);
}

mozilla::StaticAutoPtr<nsCString> sReturnedString;

// static
const char* AccessibleWrap::ReturnString(nsAString& aString) {
  if (!sReturnedString) {
    sReturnedString = new nsCString();
    ClearOnShutdown(&sReturnedString);
  }

  CopyUTF16toUTF8(aString, *sReturnedString);
  return sReturnedString->get();
}

// static
void AccessibleWrap::GetKeyBinding(Accessible* aAccessible,
                                   nsAString& aResult) {
  // Return all key bindings including access key and keyboard shortcut.

  // Get access key.
  nsAutoString keyBindingsStr;
  KeyBinding keyBinding = aAccessible->AccessKey();
  if (!keyBinding.IsEmpty()) {
    keyBinding.AppendToString(keyBindingsStr, KeyBinding::eAtkFormat);

    Accessible* parent = aAccessible->Parent();
    roles::Role role = parent ? parent->Role() : roles::NOTHING;
    if (role == roles::PARENT_MENUITEM || role == roles::MENUITEM ||
        role == roles::RADIO_MENU_ITEM || role == roles::CHECK_MENU_ITEM) {
      // It is submenu, expose keyboard shortcuts from menu hierarchy like
      // "s;<Alt>f:s"
      nsAutoString keysInHierarchyStr = keyBindingsStr;
      do {
        KeyBinding parentKeyBinding = parent->AccessKey();
        if (!parentKeyBinding.IsEmpty()) {
          nsAutoString str;
          parentKeyBinding.ToString(str, KeyBinding::eAtkFormat);
          str.Append(':');

          keysInHierarchyStr.Insert(str, 0);
        }
      } while ((parent = parent->Parent()) && parent->Role() != roles::MENUBAR);

      keyBindingsStr.Append(';');
      keyBindingsStr.Append(keysInHierarchyStr);
    }
  } else {
    // No access key, add ';' to point this.
    keyBindingsStr.Append(';');
  }

  // Get keyboard shortcut.
  keyBindingsStr.Append(';');
  if (LocalAccessible* localAcc = aAccessible->AsLocal()) {
    keyBinding = localAcc->KeyboardShortcut();
    if (!keyBinding.IsEmpty()) {
      keyBinding.AppendToString(keyBindingsStr, KeyBinding::eAtkFormat);
    }
  }
  aResult = std::move(keyBindingsStr);
}

// static
Accessible* AccessibleWrap::GetColumnHeader(TableAccessible* aAccessible,
                                            int32_t aColIdx) {
  if (!aAccessible) {
    return nullptr;
  }

  Accessible* cell = aAccessible->CellAt(0, aColIdx);
  if (!cell) {
    return nullptr;
  }

  // If the cell at the first row is column header then assume it is column
  // header for all rows,
  if (cell->Role() == roles::COLUMNHEADER) {
    return cell;
  }

  // otherwise get column header for the data cell at the first row.
  TableCellAccessible* tableCell = cell->AsTableCell();
  if (!tableCell) {
    return nullptr;
  }

  AutoTArray<Accessible*, 10> headerCells;
  tableCell->ColHeaderCells(&headerCells);
  if (headerCells.IsEmpty()) {
    return nullptr;
  }

  return headerCells[0];
}

// static
Accessible* AccessibleWrap::GetRowHeader(TableAccessible* aAccessible,
                                         int32_t aRowIdx) {
  if (!aAccessible) {
    return nullptr;
  }

  Accessible* cell = aAccessible->CellAt(aRowIdx, 0);
  if (!cell) {
    return nullptr;
  }

  // If the cell at the first column is row header then assume it is row
  // header for all columns,
  if (cell->Role() == roles::ROWHEADER) {
    return cell;
  }

  // otherwise get row header for the data cell at the first column.
  TableCellAccessible* tableCell = cell->AsTableCell();
  if (!tableCell) {
    return nullptr;
  }

  AutoTArray<Accessible*, 10> headerCells;
  tableCell->RowHeaderCells(&headerCells);
  if (headerCells.IsEmpty()) {
    return nullptr;
  }

  return headerCells[0];
}
