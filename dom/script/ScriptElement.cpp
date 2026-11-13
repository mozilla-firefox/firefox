/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScriptElement.h"

#include "ScriptLoader.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "nsContentSink.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIMutationObserver.h"
#include "nsIParser.h"
#include "nsPresContext.h"
#include "nsThreadUtils.h"

using namespace mozilla;
using namespace mozilla::dom;

NS_IMETHODIMP
ScriptElement::ScriptAvailable(nsresult aResult, nsIScriptElement* aElement,
                               bool aIsInlineClassicScript, nsIURI* aURI,
                               uint32_t aLineNo) {
  if (!aIsInlineClassicScript && NS_FAILED(aResult)) {
    nsCOMPtr<nsIParser> parser = do_QueryReferent(mCreatorParser);
    if (parser) {
      nsCOMPtr<nsIContentSink> sink = parser->GetContentSink();
      if (sink) {
        nsCOMPtr<Document> parserDoc = do_QueryInterface(sink->GetTarget());
        if (GetAsContent()->OwnerDoc() != parserDoc) {
          // Suppress errors when we've moved between docs.
          // /html/semantics/scripting-1/the-script-element/moving-between-documents/move-back-iframe-fetch-error-external-module.html
          // See also https://bugzilla.mozilla.org/show_bug.cgi?id=1849107
          return NS_OK;
        }
      }
    }

    if (parser) {
      parser->IncrementScriptNestingLevel();
    }
    // XXX Cannot dispatch the error event asynchronously? Then, we can drop
    // MOZ_CAN_RUN_SCRIPT from the script loaders.
    nsresult rv = FireErrorEvent();
    if (parser) {
      parser->DecrementScriptNestingLevel();
    }
    return rv;
  }
  return NS_OK;
}

/* virtual */
nsresult ScriptElement::FireErrorEvent() {
  const nsCOMPtr<nsIContent> cont = GetAsContent();
  return nsContentUtils::DispatchTrustedEvent(cont, u"error"_ns, CanBubble::eNo,
                                              Cancelable::eNo);
}

NS_IMETHODIMP
ScriptElement::ScriptEvaluated(nsresult aResult, nsIScriptElement* aElement,
                               bool aIsInline) {
  nsresult rv = NS_OK;
  if (!aIsInline) {
    nsCOMPtr<nsIContent> cont = GetAsContent();

    RefPtr<nsPresContext> presContext =
        nsContentUtils::GetContextForContent(cont);

    nsEventStatus status = nsEventStatus_eIgnore;
    EventMessage message = NS_SUCCEEDED(aResult) ? eLoad : eLoadError;
    WidgetEvent event(true, message);
    // Load event doesn't bubble.
    event.mFlags.mBubbles = (message != eLoad);

    EventDispatcher::Dispatch(cont, presContext, &event, nullptr, &status);
  }

  return rv;
}

void ScriptElement::CharacterDataChanged(nsIContent* aContent,
                                         const CharacterDataChangeInfo& aInfo)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  if (!nsContentUtils::IsInSameAnonymousTree(GetAsContent(), aContent)) {
    return;
  }
  UpdateTrustWorthiness(aInfo.mMutationEffectOnScript);
  MaybeProcessScript(nullptr /* aParser */);
}

void ScriptElement::AttributeChanged(Element* aElement, int32_t aNameSpaceID,
                                     nsAtom* aAttribute, AttrModType aModType,
                                     const nsAttrValue* aOldValue)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  if (aElement != GetAsContent()) {
    return;
  }
  // https://html.spec.whatwg.org/#script-processing-model
  // When a script element el that is not parser-inserted experiences one of the
  // events listed in the following list, the user agent must immediately
  // prepare the script element el:
  //  - The script element is connected and has a src attribute set where
  //  previously the element had no such attribute.
  if (aElement->IsSVGElement() && ((aNameSpaceID != kNameSpaceID_XLink &&
                                    aNameSpaceID != kNameSpaceID_None) ||
                                   aAttribute != nsGkAtoms::href)) {
    return;
  }
  if (aElement->IsHTMLElement() &&
      (aNameSpaceID != kNameSpaceID_None || aAttribute != nsGkAtoms::src)) {
    return;
  }
  if (mParserCreated == NOT_FROM_PARSER && aModType == AttrModType::Addition &&
      aElement->IsInComposedDoc()) {
    MaybeProcessScript(nullptr /* aParser */);
  }
}

void ScriptElement::ContentAppended(nsIContent* aFirstNewContent,
                                    const ContentAppendInfo& aInfo)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  if (!nsContentUtils::IsInSameAnonymousTree(GetAsContent(),
                                             aFirstNewContent)) {
    return;
  }
  UpdateTrustWorthiness(aInfo.mMutationEffectOnScript);
  // moveBefore() must not run the script.
  if (aInfo.mOldParent) {
    return;
  }
  MaybeProcessScript(nullptr /* aParser */);
}

void ScriptElement::ContentInserted(nsIContent* aChild,
                                    const ContentInsertInfo& aInfo)
    MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  if (!nsContentUtils::IsInSameAnonymousTree(GetAsContent(), aChild)) {
    return;
  }
  UpdateTrustWorthiness(aInfo.mMutationEffectOnScript);
  // moveBefore() must not run the script.
  if (aInfo.mOldParent) {
    return;
  }
  MaybeProcessScript(nullptr /* aParser */);
}

void ScriptElement::ContentWillBeRemoved(nsIContent* aChild,
                                         const ContentRemoveInfo& aInfo) {
  if (!nsContentUtils::IsInSameAnonymousTree(GetAsContent(), aChild)) {
    return;
  }
  UpdateTrustWorthiness(aInfo.mMutationEffectOnScript);
}

bool ScriptElement::MaybeProcessScript(nsCOMPtr<nsIParser> aParser) {
  nsIContent* cont = GetAsContent();
  MOZ_ASSERT(cont->IsScriptElement(),
             "nsINode::IsScriptElement should return true if the node inherits "
             "ScriptElement");

  NS_ASSERTION(cont->DebugGetSlots()->mMutationObservers.contains(this),
               "You forgot to add self as observer");

  if (mAlreadyStarted || !mDoneAddingChildren || !cont->GetComposedDoc() ||
      mMalformed || cont->IsInSVGUseShadowTree()) {
    return false;
  }

  // https://html.spec.whatwg.org/#prepare-the-script-element
  // The spec says we should calculate "source text" of inline scripts at the
  // beginning of the "Prepare the script element" algorithm.
  if (HasExternalScriptContent() || mIsTrusted ||
      TrustedTypeUtils::CanSkipTrustedTypesEnforcement(
          *GetAsContent()->AsElement())) {
    // - If it is an inline script that is trusted, we will actually retrieve
    // the "source text" lazily for performance reasons (see bug 1376651) so we
    // just pass a void string to MaybeProcessScript().
    // - If it is an external script, we actually don't need the "source text"
    // and can similarly pass a void string to MaybeProcessScript().
    bool block = MaybeProcessScript(VoidString());
    if (block && aParser) {
      aParser->BlockParser();
    }
    return block;
  }

  // This is an inline script that is not trusted (i.e. we must execute the
  // Trusted Type default policy callback to obtain a trusted "source text").
  if (nsContentUtils::IsSafeToRunScript()) {
    // - If it is safe to run script in this context, we run the default policy
    // callback and pass the returned "source text" to MaybeProcessScript().
    bool block =
        ([self = RefPtr<nsIScriptElement>(this)]()
             MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
               nsString sourceText;
               self->GetTrustedTypesCompliantInlineScriptText(sourceText);
               return static_cast<ScriptElement*>(self.get())
                   ->MaybeProcessScript(sourceText);
             })();
    if (block && aParser) {
      aParser->BlockParser();
    }
    return block;
  }

  // - The default policy callback must be wrapped in a script runner. So we
  // need to block the parser at least until we can get the "source text".
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "ScriptElement::MaybeProcessScript",
      [self = RefPtr<nsIScriptElement>(this), aParser]()
          MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
            nsString sourceText;
            self->GetTrustedTypesCompliantInlineScriptText(sourceText);
            bool block = static_cast<ScriptElement*>(self.get())
                             ->MaybeProcessScript(sourceText);
            if (!block && aParser) {
              aParser->UnblockParser();
            }
          }));
  if (aParser) {
    aParser->BlockParser();
  }
  return true;
}

bool ScriptElement::MaybeProcessScript(const nsAString& aSourceText) {
  nsIContent* cont = GetAsContent();
  if (!HasExternalScriptContent()) {
    // If el has no src attribute, and source text is the empty string, then
    // return (https://html.spec.whatwg.org/#prepare-the-script-element).
    //
    // A void aSourceText means we want to retrieve it lazily (bug 1376651), in
    // that case we browse the subtree to try and find a non-empty text node.
    bool hasInlineScriptContent =
        aSourceText.IsVoid() ? nsContentUtils::HasNonEmptyTextContent(cont)
                             : !aSourceText.IsEmpty();
    if (!hasInlineScriptContent) {
      // In the case of an empty, non-external classic script, there is nothing
      // to process. However, we must perform a microtask checkpoint afterwards,
      // as per https://html.spec.whatwg.org/#clean-up-after-running-script
      if (mKind == JS::loader::ScriptKind::eClassic && !mExternal) {
        nsContentUtils::AddScriptRunner(
            NS_NewRunnableFunction("ScriptElement::MaybeProcessScript",
                                   [doc = RefPtr{cont->OwnerDoc()}]() {
                                     nsAutoMicroTaskForDocument mt(doc);
                                   }));
      }
      return false;
    }
  }

  // Check the type attribute to determine language and version. If type exists,
  // it trumps the deprecated 'language='.
  nsAutoString type;
  bool hasType = GetScriptType(type);
  if (!type.IsEmpty()) {
    if (!nsContentUtils::IsJavascriptMIMEType(type) &&
        !type.LowerCaseEqualsASCII("module") &&
        !type.LowerCaseEqualsASCII("importmap") &&
        !(StaticPrefs::dom_speculation_rules_enabled() &&
          type.LowerCaseEqualsASCII("speculationrules"))) {
#ifdef DEBUG
      // There is a WebGL convention to store strings they need inside script
      // tags with these specific unknown script types, so don't warn for them.
      // "text/something-not-javascript" only seems to be used in the WebGL
      // conformance tests, but it is also clearly deliberately invalid, so
      // skip warning for it, too, to reduce warning spam.
      if (!type.LowerCaseEqualsASCII("x-shader/x-vertex") &&
          !type.LowerCaseEqualsASCII("x-shader/x-fragment") &&
          !type.LowerCaseEqualsASCII("text/something-not-javascript")) {
        NS_WARNING(nsPrintfCString("Unknown script type '%s'",
                                   NS_ConvertUTF16toUTF8(type).get())
                       .get());
      }
#endif  // #ifdef DEBUG
      return false;
    }
  } else if (!hasType) {
    // "language" is a deprecated attribute of HTML, so we check it only for
    // HTML script elements.
    if (cont->IsHTMLElement()) {
      nsAutoString language;
      cont->AsElement()->GetAttr(nsGkAtoms::language, language);
      if (!language.IsEmpty() &&
          !nsContentUtils::IsJavaScriptLanguage(language)) {
        return false;
      }
    }
  }

  Document* ownerDoc = cont->OwnerDoc();
  FreezeExecutionAttrs(ownerDoc);

  mAlreadyStarted = true;

  nsCOMPtr<nsIParser> parser = ((nsIScriptElement*)this)->GetCreatorParser();
  if (parser) {
    nsCOMPtr<nsIContentSink> sink = parser->GetContentSink();
    if (sink) {
      nsCOMPtr<Document> parserDoc = do_QueryInterface(sink->GetTarget());
      if (ownerDoc != parserDoc) {
        // Refactor this: https://bugzilla.mozilla.org/show_bug.cgi?id=1849107
        return false;
      }
    }
  }

  RefPtr<ScriptLoader> loader = ownerDoc->GetScriptLoader();
  if (!loader) {
    return false;
  }
  return loader->ProcessScriptElement(this, aSourceText);
}

bool ScriptElement::GetScriptType(nsAString& aType) {
  Element* element = GetAsContent()->AsElement();

  nsAutoString type;
  if (!element->GetAttr(nsGkAtoms::type, type)) {
    return false;
  }

  // ASCII whitespace https://infra.spec.whatwg.org/#ascii-whitespace:
  // U+0009 TAB, U+000A LF, U+000C FF, U+000D CR, or U+0020 SPACE.
  static const char kASCIIWhitespace[] = "\t\n\f\r ";

  const bool wasEmptyBeforeTrim = type.IsEmpty();
  type.Trim(kASCIIWhitespace);

  // If the value before trim was not empty and the value is now empty, do not
  // trim as we want to retain pure whitespace (by restoring original value)
  // because we need to treat "" and " " (etc) differently.
  if (!wasEmptyBeforeTrim && type.IsEmpty()) {
    return element->GetAttr(nsGkAtoms::type, aType);
  }

  aType.Assign(type);
  return true;
}

void ScriptElement::UpdateTrustWorthiness(
    MutationEffectOnScript aMutationEffectOnScript) {
  if (aMutationEffectOnScript == MutationEffectOnScript::DropTrustWorthiness &&
      StaticPrefs::dom_security_trusted_types_enabled()) {
    nsCOMPtr<nsIPrincipal> subjectPrincipal;
    if (JSContext* cx = nsContentUtils::GetCurrentJSContext()) {
      subjectPrincipal = nsContentUtils::SubjectPrincipal(cx);
      if (auto* principal = BasePrincipal::Cast(subjectPrincipal)) {
        if (principal->IsSystemPrincipal() ||
            principal->ContentScriptAddonPolicyCore()) {
          // This script was modified by a priviledged scripts, so continue to
          // consider it as trusted.
          return;
        }
      }
    }

    mIsTrusted = false;
  }
}

nsresult ScriptElement::GetTrustedTypesCompliantInlineScriptText(
    nsString& aSourceText) {
  MOZ_ASSERT(!mIsTrusted);

  RefPtr<Element> element = GetAsContent()->AsElement();
  nsAutoString sourceText;
  GetScriptText(sourceText);

  MOZ_ASSERT(element->IsHTMLElement() || element->IsSVGElement());
  Maybe<nsAutoString> compliantStringHolder;
  constexpr nsLiteralString htmlSinkName = u"HTMLScriptElement text"_ns;
  constexpr nsLiteralString svgSinkName = u"SVGScriptElement text"_ns;
  ErrorResult error;

  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantStringForTrustedScript(
          sourceText, element->IsHTMLElement() ? htmlSinkName : svgSinkName,
          kTrustedTypesOnlySinkGroup, *element, compliantStringHolder, error);
  if (!error.Failed()) {
    aSourceText.Assign(*compliantString);
  }
  return error.StealNSResult();
}
