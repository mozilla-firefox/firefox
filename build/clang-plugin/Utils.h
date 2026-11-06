/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Utils_h_
#define Utils_h_

#include "CustomAttributes.h"
#include "ThirdPartyPaths.h"
#include "ThreadAllows.h"
#include "plugin.h"

#if CLANG_VERSION_FULL >= 1300
// Starting with clang-13 some functions from StringRef have been renamed
#define compare_lower compare_insensitive
#endif

inline StringRef getFilename(const SourceManager &SM, SourceLocation Loc) {
  // We use the presumed location to handle #line directives and such, so the
  // plugin is friendly to icecc / sccache users.
  auto PL = SM.getPresumedLoc(Loc);
  if (PL.isValid()) {
    return StringRef(PL.getFilename());
  }
  return SM.getFilename(Loc);
}

template <class T>
inline bool ASTIsInSystemHeader(const ASTContext &AC, const T &D) {
  auto &SourceManager = AC.getSourceManager();
  auto ExpansionLoc = SourceManager.getExpansionLoc(D.getBeginLoc());
  if (ExpansionLoc.isInvalid()) {
    return false;
  }
  return SourceManager.isInSystemHeader(ExpansionLoc);
}

template <typename T> inline StringRef getNameChecked(const T &D) {
  return D->getIdentifier() ? D->getName() : "";
}

/// A cached data of whether classes are refcounted or not.
typedef DenseMap<const CXXRecordDecl *, std::pair<const Decl *, bool>>
    RefCountedMap;
extern RefCountedMap RefCountedClasses;

inline bool classHasAddRefRelease(const CXXRecordDecl *D) {
  const RefCountedMap::iterator &It = RefCountedClasses.find(D);
  if (It != RefCountedClasses.end()) {
    return It->second.second;
  }

  bool SeenAddRef = false;
  bool SeenRelease = false;
  for (CXXRecordDecl::method_iterator Method = D->method_begin();
       Method != D->method_end(); ++Method) {
    const auto &Name = getNameChecked(Method);
    if (Name == "AddRef") {
      SeenAddRef = true;
    } else if (Name == "Release") {
      SeenRelease = true;
    }
  }
  RefCountedClasses[D] = std::make_pair(D, SeenAddRef && SeenRelease);
  return SeenAddRef && SeenRelease;
}

inline bool isClassRefCounted(QualType T);

inline bool isClassRefCounted(const CXXRecordDecl *D) {
  // Normalize so that D points to the definition if it exists.
  if (!D->hasDefinition())
    return false;
  D = D->getDefinition();
  // Base class: anyone with AddRef/Release is obviously a refcounted class.
  if (classHasAddRefRelease(D))
    return true;

  // Look through all base cases to figure out if the parent is a refcounted
  // class.
  for (CXXRecordDecl::base_class_const_iterator Base = D->bases_begin();
       Base != D->bases_end(); ++Base) {
    bool Super = isClassRefCounted(Base->getType());
    if (Super) {
      return true;
    }
  }

  return false;
}

inline bool isClassRefCounted(QualType T) {
  while (const clang::ArrayType *ArrTy = T->getAsArrayTypeUnsafe())
    T = ArrTy->getElementType();
  CXXRecordDecl *Clazz = T->getAsCXXRecordDecl();
  return Clazz ? isClassRefCounted(Clazz) : false;
}

inline const FieldDecl *getClassRefCntMember(const CXXRecordDecl *D) {
  for (RecordDecl::field_iterator Field = D->field_begin(), E = D->field_end();
       Field != E; ++Field) {
    if (getNameChecked(Field) == "mRefCnt") {
      return *Field;
    }
  }
  return 0;
}

inline bool typeHasVTable(QualType T) {
  while (const clang::ArrayType *ArrTy = T->getAsArrayTypeUnsafe())
    T = ArrTy->getElementType();
  CXXRecordDecl *Offender = T->getAsCXXRecordDecl();
  return Offender && Offender->hasDefinition() && Offender->isDynamicClass();
}

inline StringRef getDeclarationNamespace(const Decl *Declaration) {
  const DeclContext *DC =
      Declaration->getDeclContext()->getEnclosingNamespaceContext();
  const NamespaceDecl *ND = dyn_cast<NamespaceDecl>(DC);
  if (!ND) {
    return "";
  }

  while (const DeclContext *ParentDC = ND->getParent()) {
    if (!isa<NamespaceDecl>(ParentDC)) {
      break;
    }
    ND = cast<NamespaceDecl>(ParentDC);
  }

  const auto &Name = ND->getName();
  return Name;
}

inline bool isInIgnoredNamespaceForImplicitCtor(const Decl *Declaration) {
  StringRef Name = getDeclarationNamespace(Declaration);
  if (Name == "") {
    return false;
  }

  return Name == "std" ||       // standard C++ lib
         Name == "__gnu_cxx" || // gnu C++ lib
         Name == "boost" ||     // boost
         Name == "webrtc" ||    // upstream webrtc
         Name == "rtc" ||       // upstream webrtc 'base' package
#if CLANG_VERSION_MAJOR >= 16
         Name.starts_with("icu_") || // icu
#else
         Name.startswith("icu_") || // icu
#endif
         Name == "google" ||           // protobuf
         Name == "google_breakpad" ||  // breakpad
         Name == "soundtouch" ||       // libsoundtouch
         Name == "stagefright" ||      // libstagefright
         Name == "MacFileUtilities" || // MacFileUtilities
         Name == "dwarf2reader" ||     // dwarf2reader
         Name == "arm_ex_to_module" || // arm_ex_to_module
         Name == "testing" ||          // gtest
         Name == "Json" ||             // jsoncpp
         Name == "rlbox" ||            // rlbox
         Name == "v8";                 // irregexp
}

inline bool isInIgnoredNamespaceForImplicitConversion(const Decl *Declaration) {
  StringRef Name = getDeclarationNamespace(Declaration);
  if (Name == "") {
    return false;
  }

  return Name == "std" ||             // standard C++ lib
         Name == "__gnu_cxx" ||       // gnu C++ lib
         Name == "google_breakpad" || // breakpad
         Name == "webrtc" ||          // libwebrtc
         Name == "testing" ||         // gtest
         Name == "rlbox";             // rlbox
}

inline bool isIgnoredPathForImplicitConversion(const Decl *Declaration) {
  Declaration = Declaration->getCanonicalDecl();
  SourceLocation Loc = Declaration->getLocation();
  const SourceManager &SM = Declaration->getASTContext().getSourceManager();
  SmallString<1024> FileName = getFilename(SM, Loc);
  llvm::sys::fs::make_absolute(FileName);
  llvm::sys::path::reverse_iterator Begin = llvm::sys::path::rbegin(FileName),
                                    End = llvm::sys::path::rend(FileName);
  for (; Begin != End; ++Begin) {
    if (Begin->compare_lower(StringRef("graphite2")) == 0) {
      return true;
    }
    if (Begin->compare_lower(StringRef("chromium")) == 0) {
      // Ignore security/sandbox/chromium but not ipc/chromium.
      ++Begin;
      return Begin != End && Begin->compare_lower(StringRef("sandbox")) == 0;
    }
  }
  return false;
}

inline bool isIgnoredPathForSprintfLiteral(const CallExpr *Call,
                                           const SourceManager &SM) {
  SourceLocation Loc = Call->getBeginLoc();
  SmallString<1024> FileName = getFilename(SM, Loc);
  llvm::sys::fs::make_absolute(FileName);
  llvm::sys::path::reverse_iterator Begin = llvm::sys::path::rbegin(FileName),
                                    End = llvm::sys::path::rend(FileName);
  for (; Begin != End; ++Begin) {
    if (Begin->compare_lower(StringRef("angle")) == 0 ||
        Begin->compare_lower(StringRef("chromium")) == 0 ||
        Begin->compare_lower(StringRef("crashreporter")) == 0 ||
        Begin->compare_lower(StringRef("google-breakpad")) == 0 ||
        Begin->compare_lower(StringRef("gflags")) == 0 ||
        Begin->compare_lower(StringRef("harfbuzz")) == 0 ||
        Begin->compare_lower(StringRef("icu")) == 0 ||
        Begin->compare_lower(StringRef("jsoncpp")) == 0 ||
        Begin->compare_lower(StringRef("libstagefright")) == 0 ||
        Begin->compare_lower(StringRef("transport")) == 0 ||
        Begin->compare_lower(StringRef("protobuf")) == 0 ||
        Begin->compare_lower(StringRef("skia")) == 0 ||
        Begin->compare_lower(StringRef("sfntly")) == 0 ||
        // Gtest uses snprintf as GTEST_SNPRINTF_ with sizeof
        Begin->compare_lower(StringRef("testing")) == 0) {
      return true;
    }
    if (Begin->compare_lower(StringRef("webrtc")) == 0) {
      // Ignore trunk/webrtc, but not media/webrtc
      ++Begin;
      return Begin != End && Begin->compare_lower(StringRef("trunk")) == 0;
    }
  }
  return false;
}

inline bool isInterestingDeclForImplicitConversion(const Decl *Declaration) {
  return !isInIgnoredNamespaceForImplicitConversion(Declaration) &&
         !isIgnoredPathForImplicitConversion(Declaration);
}

inline bool isIgnoredExprForMustUse(const Expr *E) {
  if (const CXXOperatorCallExpr *OpCall = dyn_cast<CXXOperatorCallExpr>(E)) {
    switch (OpCall->getOperator()) {
    case OO_Equal:
    case OO_PlusEqual:
    case OO_MinusEqual:
    case OO_StarEqual:
    case OO_SlashEqual:
    case OO_PercentEqual:
    case OO_CaretEqual:
    case OO_AmpEqual:
    case OO_PipeEqual:
    case OO_LessLessEqual:
    case OO_GreaterGreaterEqual:
      return true;
    default:
      return false;
    }
  }

  if (const BinaryOperator *Op = dyn_cast<BinaryOperator>(E)) {
    return Op->isAssignmentOp();
  }

  return false;
}

inline bool typeIsRefPtr(QualType Q) {
  CXXRecordDecl *D = Q->getAsCXXRecordDecl();
  if (!D || !D->getIdentifier()) {
    return false;
  }

  StringRef name = D->getName();
  if (name == "RefPtr" || name == "nsCOMPtr") {
    return true;
  }
  return false;
}

// The method defined in clang for ignoring implicit nodes doesn't work with
// some AST trees. To get around this, we define our own implementation of
// IgnoreTrivials.
inline const Stmt *MaybeSkipOneTrivial(const Stmt *s) {
  if (!s) {
    return nullptr;
  }
  if (auto *ewc = dyn_cast<ExprWithCleanups>(s)) {
    return ewc->getSubExpr();
  }
  if (auto *mte = dyn_cast<MaterializeTemporaryExpr>(s)) {
    // With clang 10 and up `getTemporary` has been replaced with the more
    // versatile `getSubExpr`.
#if CLANG_VERSION_FULL >= 1000
    return mte->getSubExpr();
#else
    return mte->GetTemporaryExpr();
#endif
  }
  if (auto *bte = dyn_cast<CXXBindTemporaryExpr>(s)) {
    return bte->getSubExpr();
  }
  if (auto *ce = dyn_cast<CastExpr>(s)) {
    s = ce->getSubExpr();
  }
  if (auto *pe = dyn_cast<ParenExpr>(s)) {
    s = pe->getSubExpr();
  }
  // Not a trivial.
  return s;
}

inline const Stmt *IgnoreTrivials(const Stmt *s) {
  while (true) {
    const Stmt *newS = MaybeSkipOneTrivial(s);
    if (newS == s) {
      return newS;
    }
    s = newS;
  }

  // Unreachable
  return nullptr;
}

inline const Expr *IgnoreTrivials(const Expr *e) {
  return cast_or_null<Expr>(IgnoreTrivials(static_cast<const Stmt *>(e)));
}

// Returns the input if the input is not a trivial.
inline const Expr *MaybeSkipOneTrivial(const Expr *e) {
  return cast_or_null<Expr>(MaybeSkipOneTrivial(static_cast<const Stmt *>(e)));
}

const FieldDecl *getBaseRefCntMember(QualType T);

inline const FieldDecl *getBaseRefCntMember(const CXXRecordDecl *D) {
  const FieldDecl *RefCntMember = getClassRefCntMember(D);
  if (RefCntMember && isClassRefCounted(D)) {
    return RefCntMember;
  }

  for (CXXRecordDecl::base_class_const_iterator Base = D->bases_begin(),
                                                E = D->bases_end();
       Base != E; ++Base) {
    RefCntMember = getBaseRefCntMember(Base->getType());
    if (RefCntMember) {
      return RefCntMember;
    }
  }
  return 0;
}

inline const FieldDecl *getBaseRefCntMember(QualType T) {
  while (const clang::ArrayType *ArrTy = T->getAsArrayTypeUnsafe())
    T = ArrTy->getElementType();
  CXXRecordDecl *Clazz = T->getAsCXXRecordDecl();
  return Clazz ? getBaseRefCntMember(Clazz) : 0;
}

inline bool isPlacementNew(const CXXNewExpr *Expression) {
  // Regular new expressions aren't placement new
  if (Expression->getNumPlacementArgs() == 0)
    return false;
  const FunctionDecl *Declaration = Expression->getOperatorNew();
  if (Declaration && hasCustomAttribute<moz_heap_allocator>(Declaration)) {
    return false;
  }
  return true;
}

extern DenseMap<StringRef, bool> InThirdPartyPathCache;

inline bool inThirdPartyPath(SourceLocation Loc, const SourceManager &SM) {
  StringRef OriginalFileName = getFilename(SM, Loc);
  auto pair = InThirdPartyPathCache.find(OriginalFileName);
  if (pair != InThirdPartyPathCache.end()) {
    return pair->second;
  }

  SmallString<1024> FileName;
  if (llvm::sys::fs::real_path(OriginalFileName, FileName)) {
    llvm::sys::fs::make_absolute(FileName);
  }

  for (uint32_t i = 0; i < MOZ_THIRD_PARTY_PATHS_COUNT; ++i) {
    auto PathB = sys::path::begin(FileName);
    auto PathE = sys::path::end(FileName);

    auto ThirdPartyB = sys::path::begin(MOZ_THIRD_PARTY_PATHS[i]);
    auto ThirdPartyE = sys::path::end(MOZ_THIRD_PARTY_PATHS[i]);

    for (; PathB != PathE; ++PathB) {
      // Perform an inner loop to compare path segments, checking if the current
      // segment is the start of the current third party path.
      auto IPathB = PathB;
      auto IThirdPartyB = ThirdPartyB;
      for (; IPathB != PathE && IThirdPartyB != ThirdPartyE;
           ++IPathB, ++IThirdPartyB) {
        if (IPathB->compare_lower(*IThirdPartyB) != 0) {
          break;
        }
      }

      // We found a match!
      if (IThirdPartyB == ThirdPartyE) {
        InThirdPartyPathCache.insert(std::make_pair(OriginalFileName, true));
        return true;
      }
    }
  }

  InThirdPartyPathCache.insert(std::make_pair(OriginalFileName, false));
  return false;
}

inline bool inThirdPartyPath(const Decl *D, ASTContext *context) {
  D = D->getCanonicalDecl();
  SourceLocation Loc = D->getLocation();
  const SourceManager &SM = context->getSourceManager();

  return inThirdPartyPath(Loc, SM);
}

inline CXXRecordDecl *getNonTemplateSpecializedCXXRecordDecl(QualType Q) {
  auto *D = Q->getAsCXXRecordDecl();

  if (!D) {
    auto TemplateQ = Q->getAs<TemplateSpecializationType>();
    if (!TemplateQ) {
      return nullptr;
    }

    auto TemplateDecl = TemplateQ->getTemplateName().getAsTemplateDecl();
    if (!TemplateDecl) {
      return nullptr;
    }

    D = dyn_cast_or_null<CXXRecordDecl>(TemplateDecl->getTemplatedDecl());
    if (!D) {
      return nullptr;
    }
  }

  return D;
}

inline bool inThirdPartyPath(const Decl *D) {
  return inThirdPartyPath(D, &D->getASTContext());
}

inline bool inThirdPartyPath(const Stmt *S, ASTContext *context) {
  SourceLocation Loc = S->getBeginLoc();
  const SourceManager &SM = context->getSourceManager();
  auto ExpansionLoc = SM.getExpansionLoc(Loc);
  if (ExpansionLoc.isInvalid()) {
    return inThirdPartyPath(Loc, SM);
  }
  return inThirdPartyPath(ExpansionLoc, SM);
}

/// Polyfill for CXXOperatorCallExpr::isInfixBinaryOp()
inline bool isInfixBinaryOp(const CXXOperatorCallExpr *OpCall) {
#if CLANG_VERSION_FULL >= 400
  return OpCall->isInfixBinaryOp();
#else
  // Taken from clang source.
  if (OpCall->getNumArgs() != 2)
    return false;

  switch (OpCall->getOperator()) {
  case OO_Call:
  case OO_Subscript:
    return false;
  default:
    return true;
  }
#endif
}

#undef compare_lower
#endif
