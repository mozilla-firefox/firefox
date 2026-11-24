/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FormAutofillNative.h"

#include <math.h>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/dom/AutocompleteInfoBinding.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLLabelElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/HTMLSelectElement.h"
#include "mozilla/RustRegex.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsTStringHasher.h"
#include "mozilla/StaticPtr.h"

namespace mozilla::dom {

static const char kWhitespace[] = "\b\t\r\n ";

enum class RegexKey : uint8_t {
  CC_NAME,
  CC_NUMBER,
  CC_EXP,
  CC_EXP_MONTH,
  CC_EXP_YEAR,
  CC_TYPE,
  MM_MONTH,
  YY_OR_YYYY,
  MONTH,
  YEAR,
  MMYY,
  VISA_CHECKOUT,
  CREDIT_CARD_NETWORK,
  CREDIT_CARD_NETWORK_EXACT_MATCH,
  CREDIT_CARD_NETWORK_LONG,
  TWO_OR_FOUR_DIGIT_YEAR,
  DWFRM,
  BML,
  TEMPLATED_VALUE,
  FIRST,
  LAST,
  GIFT,
  SUBSCRIPTION,
  VALIDATION,

  Count
};

// We don't follow the coding style (naming start with capital letter) here and
// the following CCXXX enum class because we want to sync the rule naming with
// the JS implementation.
enum class CCNumberParams : uint8_t {
  idOrNameMatchNumberRegExp,
  labelsMatchNumberRegExp,
  closestLabelMatchesNumberRegExp,
  placeholderMatchesNumberRegExp,
  ariaLabelMatchesNumberRegExp,
  idOrNameMatchGift,
  labelsMatchGift,
  placeholderMatchesGift,
  ariaLabelMatchesGift,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,
  inputTypeNotNumbery,

  Count,
};

enum class CCNameParams : uint8_t {
  idOrNameMatchNameRegExp,
  labelsMatchNameRegExp,
  closestLabelMatchesNameRegExp,
  placeholderMatchesNameRegExp,
  ariaLabelMatchesNameRegExp,
  idOrNameMatchFirst,
  labelsMatchFirst,
  placeholderMatchesFirst,
  ariaLabelMatchesFirst,
  idOrNameMatchLast,
  labelsMatchLast,
  placeholderMatchesLast,
  ariaLabelMatchesLast,
  idOrNameMatchFirstAndLast,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,

  Count,
};

enum class CCTypeParams : uint8_t {
  idOrNameMatchTypeRegExp,
  labelsMatchTypeRegExp,
  closestLabelMatchesTypeRegExp,
  idOrNameMatchVisaCheckout,
  ariaLabelMatchesVisaCheckout,
  isSelectWithCreditCardOptions,
  isRadioWithCreditCardText,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,

  Count,
};

enum class CCExpParams : uint8_t {
  labelsMatchExpRegExp,
  closestLabelMatchesExpRegExp,
  placeholderMatchesExpRegExp,
  labelsMatchExpWith2Or4DigitYear,
  placeholderMatchesExpWith2Or4DigitYear,
  labelsMatchMMYY,
  placeholderMatchesMMYY,
  maxLengthIs7,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,
  isExpirationMonthLikely,
  isExpirationYearLikely,
  idOrNameMatchMonth,
  idOrNameMatchYear,
  idOrNameMatchExpMonthRegExp,
  idOrNameMatchExpYearRegExp,
  idOrNameMatchValidation,
  Count,
};

enum class CCExpMonthParams : uint8_t {
  idOrNameMatchExpMonthRegExp,
  labelsMatchExpMonthRegExp,
  closestLabelMatchesExpMonthRegExp,
  placeholderMatchesExpMonthRegExp,
  ariaLabelMatchesExpMonthRegExp,
  idOrNameMatchMonth,
  labelsMatchMonth,
  placeholderMatchesMonth,
  ariaLabelMatchesMonth,
  nextFieldIdOrNameMatchExpYearRegExp,
  nextFieldLabelsMatchExpYearRegExp,
  nextFieldPlaceholderMatchExpYearRegExp,
  nextFieldAriaLabelMatchExpYearRegExp,
  nextFieldIdOrNameMatchYear,
  nextFieldLabelsMatchYear,
  nextFieldPlaceholderMatchesYear,
  nextFieldAriaLabelMatchesYear,
  nextFieldMatchesExpYearAutocomplete,
  isExpirationMonthLikely,
  nextFieldIsExpirationYearLikely,
  maxLengthIs2,
  placeholderMatchesMM,
  roleIsMenu,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,

  Count,
};

enum class CCExpYearParams : uint8_t {
  idOrNameMatchExpYearRegExp,
  labelsMatchExpYearRegExp,
  closestLabelMatchesExpYearRegExp,
  placeholderMatchesExpYearRegExp,
  ariaLabelMatchesExpYearRegExp,
  idOrNameMatchYear,
  labelsMatchYear,
  placeholderMatchesYear,
  ariaLabelMatchesYear,
  previousFieldIdOrNameMatchExpMonthRegExp,
  previousFieldLabelsMatchExpMonthRegExp,
  previousFieldPlaceholderMatchExpMonthRegExp,
  previousFieldAriaLabelMatchExpMonthRegExp,
  previousFieldIdOrNameMatchMonth,
  previousFieldLabelsMatchMonth,
  previousFieldPlaceholderMatchesMonth,
  previousFieldAriaLabelMatchesMonth,
  previousFieldMatchesExpMonthAutocomplete,
  isExpirationYearLikely,
  previousFieldIsExpirationMonthLikely,
  placeholderMatchesYYOrYYYY,
  roleIsMenu,
  idOrNameMatchSubscription,
  idOrNameMatchDwfrmAndBml,
  hasTemplatedValue,

  Count,
};

struct AutofillParams {
  EnumeratedArray<CCNumberParams, double, size_t(CCNumberParams::Count)>
      mCCNumberParams;
  EnumeratedArray<CCNameParams, double, size_t(CCNameParams::Count)>
      mCCNameParams;
  EnumeratedArray<CCTypeParams, double, size_t(CCTypeParams::Count)>
      mCCTypeParams;
  EnumeratedArray<CCExpParams, double, size_t(CCExpParams::Count)> mCCExpParams;
  EnumeratedArray<CCExpMonthParams, double, size_t(CCExpMonthParams::Count)>
      mCCExpMonthParams;
  EnumeratedArray<CCExpYearParams, double, size_t(CCExpYearParams::Count)>
      mCCExpYearParams;
};

// clang-format off
constexpr AutofillParams kCoefficents{
    .mCCNumberParams = {
      /* idOrNameMatchNumberRegExp */ 7.679469585418701,
      /* labelsMatchNumberRegExp */ 5.122580051422119,
      /* closestLabelMatchesNumberRegExp */ 2.1256935596466064,
      /* placeholderMatchesNumberRegExp */ 9.471800804138184,
      /* ariaLabelMatchesNumberRegExp */ 6.067715644836426,
      /* idOrNameMatchGift */ -22.946273803710938,
      /* labelsMatchGift */ -7.852959632873535,
      /* placeholderMatchesGift */ -2.355496406555176,
      /* ariaLabelMatchesGift */ -2.940307855606079,
      /* idOrNameMatchSubscription */ 0.11255314946174622,
      /* idOrNameMatchDwfrmAndBml */ -0.0006645023822784424,
      /* hasTemplatedValue */ -0.11370040476322174,
      /* inputTypeNotNumbery */ -3.750155210494995
    },
    .mCCNameParams = {
      /* idOrNameMatchNameRegExp */ 7.496212959289551,
      /* labelsMatchNameRegExp */ 6.081472873687744,
      /* closestLabelMatchesNameRegExp */ 2.600574254989624,
      /* placeholderMatchesNameRegExp */ 5.750874042510986,
      /* ariaLabelMatchesNameRegExp */ 5.162227153778076,
      /* idOrNameMatchFirst */ -6.742659091949463,
      /* labelsMatchFirst */ -0.5234538912773132,
      /* placeholderMatchesFirst */ -3.4615235328674316,
      /* ariaLabelMatchesFirst */ -1.3145145177841187,
      /* idOrNameMatchLast */ -12.561869621276855,
      /* labelsMatchLast */ -0.27417105436325073,
      /* placeholderMatchesLast */ -1.434966802597046,
      /* ariaLabelMatchesLast */ -2.9319725036621094,
      /* idOrNameMatchFirstAndLast */ 24.123435974121094,
      /* idOrNameMatchSubscription */ 0.08349418640136719,
      /* idOrNameMatchDwfrmAndBml */ 0.01882520318031311,
      /* hasTemplatedValue */ 0.182317852973938
    },
    .mCCTypeParams = {
      /* idOrNameMatchTypeRegExp */ 2.0581533908843994,
      /* labelsMatchTypeRegExp */ 1.0784518718719482,
      /* closestLabelMatchesTypeRegExp */ 0.6995877623558044,
      /* idOrNameMatchVisaCheckout */ -3.320356845855713,
      /* ariaLabelMatchesVisaCheckout */ -3.4196767807006836,
      /* isSelectWithCreditCardOptions */ 10.337477684020996,
      /* isRadioWithCreditCardText */ 4.530318737030029,
      /* idOrNameMatchSubscription */ -3.7206356525421143,
      /* idOrNameMatchDwfrmAndBml */ -0.08782318234443665,
      /* hasTemplatedValue */ 0.1772511601448059
    },
    .mCCExpParams = {
      /* labelsMatchExpRegExp */ 7.588159561157227,
      /* closestLabelMatchesExpRegExp */ 1.41484534740448,
      /* placeholderMatchesExpRegExp */ 8.759064674377441,
      /* labelsMatchExpWith2Or4DigitYear */ -3.876218795776367,
      /* placeholderMatchesExpWith2Or4DigitYear */ 2.8364884853363037,
      /* labelsMatchMMYY */ 8.836017608642578,
      /* placeholderMatchesMMYY */ -0.5231751799583435,
      /* maxLengthIs7 */ 1.3565447330474854,
      /* idOrNameMatchSubscription */ 0.1779913753271103,
      /* idOrNameMatchDwfrmAndBml */ 0.21037884056568146,
      /* hasTemplatedValue */ 0.14900512993335724,
      /* isExpirationMonthLikely */ -3.223409652709961,
      /* isExpirationYearLikely */ -2.536919593811035,
      /* idOrNameMatchMonth */ -3.6893014907836914,
      /* idOrNameMatchYear */ -3.108184337615967,
      /* idOrNameMatchExpMonthRegExp */ -2.264357089996338,
      /* idOrNameMatchExpYearRegExp */ -2.7957723140716553,
      /* idOrNameMatchValidation */ -2.29402756690979
    },
    .mCCExpMonthParams = {
      /* idOrNameMatchExpMonthRegExp */ 0.2787344455718994,
      /* labelsMatchExpMonthRegExp */ 1.298413634300232,
      /* closestLabelMatchesExpMonthRegExp */ -11.206244468688965,
      /* placeholderMatchesExpMonthRegExp */ 1.2605619430541992,
      /* ariaLabelMatchesExpMonthRegExp */ 1.1330018043518066,
      /* idOrNameMatchMonth */ 6.1464314460754395,
      /* labelsMatchMonth */ 0.7051732540130615,
      /* placeholderMatchesMonth */ 0.7463492751121521,
      /* ariaLabelMatchesMonth */ 1.8244760036468506,
      /* nextFieldIdOrNameMatchExpYearRegExp */ 0.06347066164016724,
      /* nextFieldLabelsMatchExpYearRegExp */ -0.1692247837781906,
      /* nextFieldPlaceholderMatchExpYearRegExp */ 1.0434566736221313,
      /* nextFieldAriaLabelMatchExpYearRegExp */ 1.751156210899353,
      /* nextFieldIdOrNameMatchYear */ -0.532447338104248,
      /* nextFieldLabelsMatchYear */ 1.3248541355133057,
      /* nextFieldPlaceholderMatchesYear */ 0.604235827922821,
      /* nextFieldAriaLabelMatchesYear */ 1.5364223718643188,
      /* nextFieldMatchesExpYearAutocomplete */ 6.285938262939453,
      /* isExpirationMonthLikely */ 13.117807388305664,
      /* nextFieldIsExpirationYearLikely */ 7.182341575622559,
      /* maxLengthIs2 */ 4.477289199829102,
      /* placeholderMatchesMM */ 14.403288841247559,
      /* roleIsMenu */ 5.770959854125977,
      /* idOrNameMatchSubscription */ -0.043085768818855286,
      /* idOrNameMatchDwfrmAndBml */ 0.02823038399219513,
      /* hasTemplatedValue */ 0.07234494388103485
    },
    .mCCExpYearParams = {
      /* idOrNameMatchExpYearRegExp */ 5.426016807556152,
      /* labelsMatchExpYearRegExp */ 1.3240209817886353,
      /* closestLabelMatchesExpYearRegExp */ -8.702284812927246,
      /* placeholderMatchesExpYearRegExp */ 0.9059725999832153,
      /* ariaLabelMatchesExpYearRegExp */ 0.5550334453582764,
      /* idOrNameMatchYear */ 5.362994194030762,
      /* labelsMatchYear */ 2.7185044288635254,
      /* placeholderMatchesYear */ 0.7883157134056091,
      /* ariaLabelMatchesYear */ 0.311492383480072,
      /* previousFieldIdOrNameMatchExpMonthRegExp */ 1.8155208826065063,
      /* previousFieldLabelsMatchExpMonthRegExp */ -0.46133187413215637,
      /* previousFieldPlaceholderMatchExpMonthRegExp */ 1.0374903678894043,
      /* previousFieldAriaLabelMatchExpMonthRegExp */ -0.5901495814323425,
      /* previousFieldIdOrNameMatchMonth */ -5.960310935974121,
      /* previousFieldLabelsMatchMonth */ 0.6495584845542908,
      /* previousFieldPlaceholderMatchesMonth */ 0.7198042273521423,
      /* previousFieldAriaLabelMatchesMonth */ 3.4590985774993896,
      /* previousFieldMatchesExpMonthAutocomplete */ 2.986003875732422,
      /* isExpirationYearLikely */ 4.021566390991211,
      /* previousFieldIsExpirationMonthLikely */ 9.298635482788086,
      /* placeholderMatchesYYOrYYYY */ 10.457176208496094,
      /* roleIsMenu */ 1.1051956415176392,
      /* idOrNameMatchSubscription */ 0.000688597559928894,
      /* idOrNameMatchDwfrmAndBml */ 0.15687309205532074,
      /* hasTemplatedValue */ -0.19141331315040588
    }
};
// clang-format off

constexpr float kCCNumberBias = -4.948795795440674;
constexpr float kCCNameBias = -5.3578081130981445;
// Comment out code that are not used right now
/*
constexpr float kCCTypeBias = -5.979659557342529;
constexpr float kCCExpBias = -5.849575996398926;
constexpr float kCCExpMonthBias = -8.844199180603027;
constexpr float kCCExpYearBias = -6.499860763549805;
*/

struct Rule {
  RegexKey key;
  const char* pattern;
};

const Rule kFirefoxRules[] = {
    {RegexKey::MM_MONTH, "^mm$|\\(mm\\)"},
    {RegexKey::YY_OR_YYYY, "^(yy|yyyy)$|\\(yy\\)|\\(yyyy\\)"},
    {RegexKey::MONTH, "month"},
    {RegexKey::YEAR, "year"},
    {RegexKey::MMYY, "mm\\s*(/|\\\\)\\s*yy"},
    {RegexKey::VISA_CHECKOUT, "visa(-|\\s)checkout"},
    // This should be a union of NETWORK_NAMES in CreditCard.sys.mjs
    {RegexKey::CREDIT_CARD_NETWORK_LONG,
     "american express|master card|union pay"},
    // Please also update CREDIT_CARD_NETWORK_EXACT_MATCH while updating
    // CREDIT_CARD_NETWORK
    {RegexKey::CREDIT_CARD_NETWORK,
     "amex|cartebancaire|diners|discover|jcb|mastercard|mir|unionpay|visa"},
    {RegexKey::CREDIT_CARD_NETWORK_EXACT_MATCH,
     "^\\s*(?:amex|cartebancaire|diners|discover|jcb|mastercard|mir|unionpay|"
     "visa)\\s*$"},
    {RegexKey::TWO_OR_FOUR_DIGIT_YEAR,
     "(?:exp.*date[^y\\\\n\\\\r]*|mm\\\\s*[-/]?\\\\s*)yy(?:yy)?(?:[^y]|$)"},
    {RegexKey::DWFRM, "^dwfrm"},
    {RegexKey::BML, "BML"},
    {RegexKey::TEMPLATED_VALUE, "^\\{\\{.*\\}\\}$"},
    {RegexKey::FIRST, "first"},
    {RegexKey::LAST, "last"},
    {RegexKey::GIFT, "gift"},
    {RegexKey::SUBSCRIPTION, "subscription"},
    {RegexKey::VALIDATION, "validate|validation"},
};

// These are the rules used by Bitwarden [0], converted into RegExp form.
// [0]
// https://github.com/bitwarden/browser/blob/c2b8802201fac5e292d55d5caf3f1f78088d823c/src/services/autofill.service.ts#L436
const Rule kCreditCardRules[] = {
    /* eslint-disable */
    // Let us keep our consistent wrapping.
    {RegexKey::CC_NAME,
     // Firefox-specific rules
     "account.*holder.*name"
     "|^(credit[-\\s]?card|card).*name"
     // de-DE
     "|^(kredit)?(karten|konto)inhaber"
     "|^(name).*karte"
     // fr-FR
     "|nom.*(titulaire|détenteur)"
     "|(titulaire|détenteur).*(carte)"
     // it-IT
     "|titolare.*carta"
     // pl-PL
     "|posiadacz.*karty"
     // es-ES
     "|nombre.*(titular|tarjeta)"
     // nl-NL
     "|naam.*op.*kaart"
     // Rules from Bitwarden
     "|cc-?name"
     "|card-?name"
     "|cardholder-?name"
     "|(^nom$)"
     // Rules are from Chromium source codes
     "|card.?(?:holder|owner)|name.*(\\b)?on(\\b)?.*card"
     "|(?:card|cc).?name|cc.?full.?name"
     "|(?:card|cc).?owner"
     "|nom.*carte"                      // fr-FR
     "|nome.*cart"                      // it-IT
     "|名前"                            // ja-JP
     "|Имя.*карты"                      // ru
     "|信用卡开户名|开户名|持卡人姓名"  // zh-CN
     "|持卡人姓名"},                    // zh-TW
    /* eslint-enable */

    {RegexKey::CC_NUMBER,
     // Firefox-specific rules
     // de-DE
     "(cc|kk)nr"
     "|(kredit)?(karten)(nummer|nr)"
     // it-IT
     "|numero.*carta"
     // fr-FR
     "|(numero|número|numéro).*(carte)"
     // pl-PL
     "|numer.*karty"
     // es-ES
     "|(número|numero).*tarjeta"
     // nl-NL
     "|kaartnummer"
     // Rules from Bitwarden
     "|cc-?number"
     "|cc-?num"
     "|card-?number"
     "|card-?num"
     "|cc-?no"
     "|card-?no"
     "|numero-?carte"
     "|num-?carte"
     "|cb-?num"
     // Rules are from Chromium source codes
     "|(add)?(?:card|cc|acct).?(?:number|#|no|num)"
     "|カード番号"           // ja-JP
     "|Номер.*карты"         // ru
     "|信用卡号|信用卡号码"  // zh-CN
     "|信用卡卡號"           // zh-TW
     "|카드"},               // ko-KR

    {RegexKey::CC_EXP,
     // Firefox-specific rules
     "mm\\s*(/|\\|-)\\s*(yy|jj|aa)"
     "|(month|mois)\\s*(/|\\|-|et)\\s*(year|année)"
     // de-DE
     // fr-FR
     // Rules from Bitwarden
     "|(^cc-?exp$)"
     "|(^card-?exp$)"
     "|(^cc-?expiration$)"
     "|(^card-?expiration$)"
     "|(^cc-?ex$)"
     "|(^card-?ex$)"
     "|(^card-?expire$)"
     "|(^card-?expiry$)"
     "|(^validite$)"
     "|(^expiration$)"
     "|(^expiry$)"
     "|mm-?yy"
     "|mm-?yyyy"
     "|yy-?mm"
     "|yyyy-?mm"
     "|expiration-?date"
     "|payment-?card-?expiration"
     "|(^payment-?cc-?date$)"
     // Rules are from Chromium source codes
     "|expir|exp.*date|^expfield$"
     "|ablaufdatum|gueltig|gültig"  // de-DE
     "|fecha"                       // es
     "|date.*exp"                   // fr-FR
     "|scadenza"                    // it-IT
     "|有効期限"                    // ja-JP
     "|validade"                    // pt-BR, pt-PT
     "|Срок действия карты"},       // ru

    {RegexKey::CC_EXP_MONTH,
     // Firefox-specific rules
     "(cc|kk)month"  // de-DE
     // Rules from Bitwarden
     "|(^exp-?month$)"
     "|(^cc-?exp-?month$)"
     "|(^cc-?month$)"
     "|(^card-?month$)"
     "|(^cc-?mo$)"
     "|(^card-?mo$)"
     "|(^exp-?mo$)"
     "|(^card-?exp-?mo$)"
     "|(^cc-?exp-?mo$)"
     "|(^card-?expiration-?month$)"
     "|(^expiration-?month$)"
     "|(^cc-?mm$)"
     "|(^cc-?m$)"
     "|(^card-?mm$)"
     "|(^card-?m$)"
     "|(^card-?exp-?mm$)"
     "|(^cc-?exp-?mm$)"
     "|(^exp-?mm$)"
     "|(^exp-?m$)"
     "|(^expire-?month$)"
     "|(^expire-?mo$)"
     "|(^expiry-?month$)"
     "|(^expiry-?mo$)"
     "|(^card-?expire-?month$)"
     "|(^card-?expire-?mo$)"
     "|(^card-?expiry-?month$)"
     "|(^card-?expiry-?mo$)"
     "|(^mois-?validite$)"
     "|(^mois-?expiration$)"
     "|(^m-?validite$)"
     "|(^m-?expiration$)"
     "|(^expiry-?date-?field-?month$)"
     "|(^expiration-?date-?month$)"
     "|(^expiration-?date-?mm$)"
     "|(^exp-?mon$)"
     "|(^validity-?mo$)"
     "|(^exp-?date-?mo$)"
     "|(^cb-?date-?mois$)"
     "|(^date-?m$)"
     // Rules are from Chromium source codes
     "|exp.*mo|ccmonth|cardmonth|addmonth"
     "|monat"  // de-DE
     // "|fecha" // es
     // "|date.*exp" // fr-FR
     // "|scadenza" // it-IT
     // "|有効期限" // ja-JP
     // "|validade" // pt-BR, pt-PT
     // "|Срок действия карты" // ru
     "|月"},  // zh-CN

    {RegexKey::CC_EXP_YEAR,
     // Firefox-specific rules
     "(cc|kk)year"  // de-DE
     // Rules from Bitwarden
     "|(^exp-?year$)"
     "|(^cc-?exp-?year$)"
     "|(^cc-?year$)"
     "|(^card-?year$)"
     "|(^cc-?yr$)"
     "|(^card-?yr$)"
     "|(^exp-?yr$)"
     "|(^card-?exp-?yr$)"
     "|(^cc-?exp-?yr$)"
     "|(^card-?expiration-?year$)"
     "|(^expiration-?year$)"
     "|(^cc-?yy$)"
     "|(^cc-?y$)"
     "|(^card-?yy$)"
     "|(^card-?y$)"
     "|(^card-?exp-?yy$)"
     "|(^cc-?exp-?yy$)"
     "|(^exp-?yy$)"
     "|(^exp-?y$)"
     "|(^cc-?yyyy$)"
     "|(^card-?yyyy$)"
     "|(^card-?exp-?yyyy$)"
     "|(^cc-?exp-?yyyy$)"
     "|(^expire-?year$)"
     "|(^expire-?yr$)"
     "|(^expiry-?year$)"
     "|(^expiry-?yr$)"
     "|(^card-?expire-?year$)"
     "|(^card-?expire-?yr$)"
     "|(^card-?expiry-?year$)"
     "|(^card-?expiry-?yr$)"
     "|(^an-?validite$)"
     "|(^an-?expiration$)"
     "|(^annee-?validite$)"
     "|(^annee-?expiration$)"
     "|(^expiry-?date-?field-?year$)"
     "|(^expiration-?date-?year$)"
     "|(^cb-?date-?ann$)"
     "|(^expiration-?date-?yy$)"
     "|(^expiration-?date-?yyyy$)"
     "|(^validity-?year$)"
     "|(^exp-?date-?year$)"
     "|(^date-?y$)"
     // Rules are from Chromium source codes
     "|(add)?year"
     "|jahr"  // de-DE
     // "|fecha" // es
     // "|scadenza" // it-IT
     // "|有効期限" // ja-JP
     // "|validade" // pt-BR, pt-PT
     // "|Срок действия карты" // ru
     "|年|有效期"},  // zh-CN

    {RegexKey::CC_TYPE,
     // Firefox-specific rules
     "type"
     // de-DE
     "|Kartenmarke"
     // Rules from Bitwarden
     "|(^cc-?type$)"
     "|(^card-?type$)"
     "|(^card-?brand$)"
     "|(^cc-?brand$)"
     "|(^cb-?type$)"},
    // Rules are from Chromium source codes
};

static double Sigmoid(double x) { return 1.0 / (1.0 + exp(-x)); }

class FormAutofillImpl {
 public:
  FormAutofillImpl();

  void GetFormAutofillConfidences(
      GlobalObject& aGlobal, const Sequence<OwningNonNull<Element>>& aElements,
      nsTArray<FormAutofillConfidences>& aResults, ErrorResult& aRv);

 private:
  const RustRegex& GetRegex(RegexKey key);

  bool StringMatchesRegExp(const nsACString& str, RegexKey key);
  bool StringMatchesRegExp(const nsAString& str, RegexKey key);
  bool TextContentMatchesRegExp(Element& element, RegexKey key);
  size_t CountRegExpMatches(const nsACString& str, RegexKey key);
  size_t CountRegExpMatches(const nsAString& str, RegexKey key);
  bool IdOrNameMatchRegExp(Element& element, RegexKey key);
  bool NextFieldMatchesExpYearAutocomplete(Element* aNextField);
  bool PreviousFieldMatchesExpMonthAutocomplete(Element* aPrevField);
  bool LabelMatchesRegExp(Element& element, const nsTArray<nsCString>* labels,
                          RegexKey key);
  bool ClosestLabelMatchesRegExp(Element& aElement, RegexKey aKey);
  bool PlaceholderMatchesRegExp(Element& element, RegexKey key);
  bool AriaLabelMatchesRegExp(Element& element, RegexKey key);
  bool AutocompleteStringMatches(Element& aElement, const nsAString& aKey);

  bool HasTemplatedValue(Element& element);
  bool MaxLengthIs(Element& aElement, int32_t aValue);
  bool IsExpirationMonthLikely(Element& element);
  bool IsExpirationYearLikely(Element& element);
  bool InputTypeNotNumbery(Element& element);
  bool IsSelectWithCreditCardOptions(Element& element);
  bool IsRadioWithCreditCardText(Element& element,
                                 const nsTArray<nsCString>* labels,
                                 ErrorResult& aRv);
  bool MatchesExpYearAutocomplete(Element& element);
  bool RoleIsMenu(Element& element);

  Element* FindRootForField(Element* aElement);

  Element* FindField(const Sequence<OwningNonNull<Element>>& aElements,
                     uint32_t aStartIndex, int8_t aDirection);
  Element* NextField(const Sequence<OwningNonNull<Element>>& aElements,
                     uint32_t aStartIndex);
  Element* PrevField(const Sequence<OwningNonNull<Element>>& aElements,
                     uint32_t aStartIndex);

  // Array contains regular expressions to match the corresponding
  // field. Ex, CC number, CC type, etc.
  using RegexStringArray =
      EnumeratedArray<RegexKey, nsCString, size_t(RegexKey::Count)>;
  RegexStringArray mRuleMap;

  // Array that holds RegexWrapper that created by regex::ffi::regex_new
  using RegexWrapperArray = EnumeratedArray<RegexKey, RustRegex, size_t(RegexKey::Count)>;
  RegexWrapperArray mRegexes;
};

FormAutofillImpl::FormAutofillImpl() {
  const Rule* rulesets[] = {&kFirefoxRules[0], &kCreditCardRules[0]};
  size_t rulesetLengths[] = {std::size(kFirefoxRules),
                             std::size(kCreditCardRules)};

  for (uint32_t i = 0; i < std::size(rulesetLengths); ++i) {
    for (uint32_t j = 0; j < rulesetLengths[i]; ++j) {
      nsCString& rule = mRuleMap[rulesets[i][j].key];
      if (!rule.IsEmpty()) {
        rule.Append("|");
      }
      rule.Append(rulesets[i][j].pattern);
    }
  }
}

const RustRegex& FormAutofillImpl::GetRegex(RegexKey aKey) {
  if (!mRegexes[aKey]) {
    RustRegex regex(mRuleMap[aKey], RustRegexOptions().CaseInsensitive(true));
    MOZ_DIAGNOSTIC_ASSERT(regex);
    mRegexes[aKey] = std::move(regex);
  }
  return mRegexes[aKey];
}

bool FormAutofillImpl::StringMatchesRegExp(const nsACString& aStr,
                                           RegexKey aKey) {
  return GetRegex(aKey).IsMatch(aStr);
}

bool FormAutofillImpl::StringMatchesRegExp(const nsAString& aStr,
                                           RegexKey aKey) {
  return StringMatchesRegExp(NS_ConvertUTF16toUTF8(aStr), aKey);
}

bool FormAutofillImpl::TextContentMatchesRegExp(Element& element,
                                                RegexKey key) {
  ErrorResult rv;
  nsAutoString text;
  element.GetTextContent(text, rv);
  if (rv.Failed()) {
    return false;
  }

  return StringMatchesRegExp(text, key);
}

size_t FormAutofillImpl::CountRegExpMatches(const nsACString& aStr,
                                            RegexKey aKey) {
  return GetRegex(aKey).CountMatches(aStr);
}

size_t FormAutofillImpl::CountRegExpMatches(const nsAString& aStr,
                                            RegexKey aKey) {
  return CountRegExpMatches(NS_ConvertUTF16toUTF8(aStr), aKey);
}

bool FormAutofillImpl::NextFieldMatchesExpYearAutocomplete(
    Element* aNextField) {
  return AutocompleteStringMatches(*aNextField, u"cc-exp-year"_ns);
}

bool FormAutofillImpl::PreviousFieldMatchesExpMonthAutocomplete(
    Element* aPrevField) {
  return AutocompleteStringMatches(*aPrevField, u"cc-exp-month"_ns);
}

bool FormAutofillImpl::IdOrNameMatchRegExp(Element& aElement, RegexKey key) {
  nsAutoString str;
  aElement.GetId(str);
  if (StringMatchesRegExp(str, key)) {
    return true;
  }
  aElement.GetAttr(nsGkAtoms::name, str);
  return StringMatchesRegExp(str, key);
}

bool FormAutofillImpl::LabelMatchesRegExp(
    Element& aElement, const nsTArray<nsCString>* labelStrings, RegexKey key) {
  if (labelStrings) {
    for (const auto& str : *labelStrings) {
      if (StringMatchesRegExp(str, key)) {
        return true;
      }
    }
  }

  Element* parent = aElement.GetParentElement();
  if (!parent) {
    return false;
  }

  ErrorResult aRv;
  if (parent->IsHTMLElement(nsGkAtoms::td)) {
    Element* pp = parent->GetParentElement();
    if (pp) {
      return TextContentMatchesRegExp(*pp, key);
    }
  }
  if (parent->IsHTMLElement(nsGkAtoms::td)) {
    Element* pes = aElement.GetPreviousElementSibling();
    if (pes) {
      return TextContentMatchesRegExp(*pes, key);
    }
  }
  return false;
}

bool FormAutofillImpl::ClosestLabelMatchesRegExp(Element& aElement,
                                                 RegexKey aKey) {
  ErrorResult aRv;
  Element* pes = aElement.GetPreviousElementSibling();
  if (pes && pes->IsHTMLElement(nsGkAtoms::label)) {
    return TextContentMatchesRegExp(*pes, aKey);
  }

  Element* nes = aElement.GetNextElementSibling();
  if (nes && nes->IsHTMLElement(nsGkAtoms::label)) {
    return TextContentMatchesRegExp(*nes, aKey);
  }

  return false;
}

bool FormAutofillImpl::PlaceholderMatchesRegExp(Element& aElement,
                                                RegexKey aKey) {
  nsAutoString str;
  if (!aElement.GetAttr(nsGkAtoms::placeholder, str)) {
    return false;
  }
  return StringMatchesRegExp(str, aKey);
}

bool FormAutofillImpl::AriaLabelMatchesRegExp(Element& aElement,
                                              RegexKey aKey) {
  nsAutoString str;
  if (!aElement.GetAttr(nsGkAtoms::aria_label, str)) {
    return false;
  }
  return StringMatchesRegExp(str, aKey);
}

bool FormAutofillImpl::AutocompleteStringMatches(Element& aElement,
                                                 const nsAString& aKey) {
  Nullable<AutocompleteInfo> info;
  if (auto* input = HTMLInputElement::FromNode(aElement)) {
    input->GetAutocompleteInfo(info);
  } else {
    AutocompleteInfo autoInfo;
    if (auto* select = HTMLSelectElement::FromNode(aElement)) {
      select->GetAutocompleteInfo(autoInfo);
      info.SetValue(autoInfo);
    }
  }

  if (info.IsNull()) {
    return false;
  }

  return info.Value().mFieldName.Equals(aKey);
}

bool FormAutofillImpl::HasTemplatedValue(Element& aElement) {
  nsAutoString str;
  if (!aElement.GetAttr(nsGkAtoms::value, str)) {
    return false;
  }
  return StringMatchesRegExp(str, RegexKey::TEMPLATED_VALUE);
}

bool FormAutofillImpl::RoleIsMenu(Element& aElement) {
  return aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::role,
                              nsGkAtoms::menu, eCaseMatters);
}

bool FormAutofillImpl::InputTypeNotNumbery(Element& aElement) {
  auto* input = HTMLInputElement::FromNode(aElement);
  if (!input) {
    return true;
  }

  auto type = input->ControlType();
  return type != FormControlType::InputText &&
         type != FormControlType::InputTel &&
         type != FormControlType::InputNumber;
}

bool FormAutofillImpl::IsSelectWithCreditCardOptions(Element& aElement) {
  auto* select = HTMLSelectElement::FromNode(aElement);
  if (!select) {
    return false;
  }

  RefPtr<HTMLCollection> options = select->Options();
  for (uint32_t i = 0; i < options->Length(); ++i) {
    auto* item = options->Item(i);
    auto* option = HTMLOptionElement::FromNode(item);
    if (!option) {
      continue;
    }
    // Bug 1756799, consider using getAttribute("value") instead of .value
    nsAutoString str;
    option->GetValue(str);
    if (StringMatchesRegExp(str, RegexKey::CREDIT_CARD_NETWORK_EXACT_MATCH) ||
        StringMatchesRegExp(str, RegexKey::CREDIT_CARD_NETWORK_LONG)) {
      return true;
    }

    option->GetText(str);
    if (StringMatchesRegExp(str, RegexKey::CREDIT_CARD_NETWORK_EXACT_MATCH) ||
        StringMatchesRegExp(str, RegexKey::CREDIT_CARD_NETWORK_LONG)) {
      return true;
    }
  }
  return false;
}

bool FormAutofillImpl::IsRadioWithCreditCardText(
    Element& aElement, const nsTArray<nsCString>* aLabels, ErrorResult& aRv) {
  auto* input = HTMLInputElement::FromNode(aElement);
  if (!input) {
    return false;
  }
  auto type = input->ControlType();
  if (type != FormControlType::InputRadio) {
    return false;
  }

  nsAutoString str;
  input->GetValue(str, CallerType::System);
  if (CountRegExpMatches(str, RegexKey::CREDIT_CARD_NETWORK) == 1) {
    return true;
  }

  if (aLabels) {
    size_t labelsMatched = 0;
    for (const auto& label : *aLabels) {
      size_t labelMatches =
          CountRegExpMatches(label, RegexKey::CREDIT_CARD_NETWORK);
      if (labelMatches > 1) {
        return false;
      }
      if (labelMatches > 0) {
        labelsMatched++;
      }
    }

    if (labelsMatched) {
      return labelsMatched == 1;
    }
  }

  // Bug 1756798 : Remove reading text content in a <input>
  nsAutoString text;
  aElement.GetTextContent(text, aRv);
  if (aRv.Failed()) {
    return false;
  }
  return CountRegExpMatches(text, RegexKey::CREDIT_CARD_NETWORK) == 1;
}

bool FormAutofillImpl::MaxLengthIs(Element& aElement, int32_t aValue) {
  auto* input = HTMLInputElement::FromNode(aElement);
  if (!input) {
    return false;
  }
  return input->MaxLength() == aValue;
}

static bool TestOptionElementForInteger(Element* aElement, int32_t aTestValue) {
  auto* option = HTMLOptionElement::FromNodeOrNull(aElement);
  if (!option) {
    return false;
  }
  nsAutoString str;
  option->GetValue(str);
  nsContentUtils::ParseHTMLIntegerResultFlags parseFlags;
  int32_t val = nsContentUtils::ParseHTMLInteger(str, &parseFlags);
  if (val == aTestValue) {
    return true;
  }
  option->GetRenderedLabel(str);
  val = nsContentUtils::ParseHTMLInteger(str, &parseFlags);
  return val == aTestValue;
}

static bool MatchOptionContiguousInteger(HTMLOptionsCollection* aOptions,
                                         uint32_t aNumContiguous,
                                         int32_t aInteger) {
  uint32_t len = aOptions->Length();
  if (aNumContiguous > len) {
    return false;
  }

  for (uint32_t i = 0; i <= aOptions->Length() - aNumContiguous; i++) {
    bool match = true;
    for (uint32_t j = 0; j < aNumContiguous; j++) {
      if (!TestOptionElementForInteger(aOptions->Item(i + j), aInteger + j)) {
        match = false;
        break;
      }
    }
    if (match) {
      return true;
    }
  }
  return false;
}

bool FormAutofillImpl::IsExpirationYearLikely(Element& aElement) {
  auto* select = HTMLSelectElement::FromNode(aElement);
  if (!select) {
    return false;
  }

  auto* options = select->Options();
  if (!options) {
    return false;
  }

  PRExplodedTime tm;
  PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &tm);
  uint16_t currentYear = tm.tm_year;

  return MatchOptionContiguousInteger(options, 3, currentYear);
}

bool FormAutofillImpl::IsExpirationMonthLikely(Element& aElement) {
  auto* select = HTMLSelectElement::FromNode(aElement);
  if (!select) {
    return false;
  }

  auto* options = select->Options();
  if (!options) {
    return false;
  }

  if (options->Length() != 12 && options->Length() != 13) {
    return false;
  }

  return MatchOptionContiguousInteger(options, 12, 1);
}

Element* FormAutofillImpl::FindRootForField(Element* aElement) {
  if (const auto* control =
          nsGenericHTMLFormControlElement::FromNode(aElement)) {
    if (Element* form = control->GetFormInternal()) {
      return form;
    }
  }

  return aElement->OwnerDoc()->GetDocumentElement();
}

Element* FormAutofillImpl::FindField(
    const Sequence<OwningNonNull<Element>>& aElements, uint32_t aStartIndex,
    int8_t aDirection) {
  MOZ_ASSERT(aDirection == 1 || aDirection == -1);
  MOZ_ASSERT(aStartIndex < aElements.Length());

  Element* curFieldRoot = FindRootForField(aElements[aStartIndex]);
  bool isRootForm = curFieldRoot->IsHTMLElement(nsGkAtoms::form);

  uint32_t num =
      aDirection == 1 ? aElements.Length() - aStartIndex - 1 : aStartIndex;
  for (uint32_t i = 0, searchIndex = aStartIndex; i < num; i++) {
    searchIndex += aDirection;
    const auto& element = aElements[searchIndex];
    Element* root = FindRootForField(element);

    if (isRootForm) {
      // Only search fields that are within the same root element.
      if (curFieldRoot != root) {
        return nullptr;
      }
    } else {
      // Exclude elements inside the rootElement that are already in a <form>.
      if (root->IsHTMLElement(nsGkAtoms::form)) {
        continue;
      }
    }

    if (element->IsAnyOfHTMLElements(nsGkAtoms::input, nsGkAtoms::select)) {
      return element.get();
    }
  }

  return nullptr;
}

Element* FormAutofillImpl::NextField(
    const Sequence<OwningNonNull<Element>>& aElements, uint32_t aStartIndex) {
  return FindField(aElements, aStartIndex, 1);
}

Element* FormAutofillImpl::PrevField(
    const Sequence<OwningNonNull<Element>>& aElements, uint32_t aStartIndex) {
  return FindField(aElements, aStartIndex, -1);
}

static void ExtractLabelStrings(nsINode* aNode, nsTArray<nsCString>& aStrings,
                                ErrorResult& aRv) {
  if (aNode->IsAnyOfHTMLElements(nsGkAtoms::script, nsGkAtoms::noscript,
                                 nsGkAtoms::option, nsGkAtoms::style)) {
    return;
  }

  if (aNode->IsText() || !aNode->HasChildren()) {
    nsAutoString text;
    aNode->GetTextContent(text, aRv);
    if (aRv.Failed()) {
      return;
    }

    text.Trim(kWhitespace);
    CopyUTF16toUTF8(text, *aStrings.AppendElement());
    return;
  }

  for (nsINode* child = aNode->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsElement() || child->IsText()) {
      ExtractLabelStrings(child, aStrings, aRv);
      if (aRv.Failed()) {
        return;
      }
    }
  }
}

nsTArray<nsCString>* GetLabelStrings(
    Element* aElement,
    const nsTHashMap<void*, nsTArray<nsCString>>& aElementMap,
    const nsTHashMap<nsAtom*, nsTArray<nsCString>>& aIdMap) {
  if (!aElement) {
    return nullptr;
  }

  if (nsAtom* idAtom = aElement->GetID()) {
    return aIdMap.Lookup(idAtom).DataPtrOrNull();
  }

  return aElementMap.Lookup(aElement).DataPtrOrNull();
}

void FormAutofillImpl::GetFormAutofillConfidences(
    GlobalObject& aGlobal, const Sequence<OwningNonNull<Element>>& aElements,
    nsTArray<FormAutofillConfidences>& aResults, ErrorResult& aRv) {
  if (aElements.IsEmpty()) {
    return;
  }

  // Create Labels
  auto* document = aElements[0]->OwnerDoc();
#ifdef DEBUG
  for (uint32_t i = 1; i < aElements.Length(); ++i) {
    MOZ_ASSERT(document == aElements[i]->OwnerDoc());
  }
#endif

  RefPtr<ContentList> labels = document->GetElementsByTagName(u"label"_ns);
  nsTHashMap<void*, nsTArray<nsCString>> elementsToLabelStrings;
  nsTHashMap<nsAtom*, nsTArray<nsCString>> elementsIdToLabelStrings;
  if (labels) {
    for (uint32_t i = 0; i < labels->Length(); ++i) {
      auto* item = labels->Item(i);
      auto* label = HTMLLabelElement::FromNode(item);
      if (NS_WARN_IF(!label)) {
        continue;
      }
      auto* control = label->GetLabeledElementInternal();
      if (!control) {
        continue;
      }
      nsTArray<nsCString> labelStrings;
      ExtractLabelStrings(label, labelStrings, aRv);
      if (aRv.Failed()) {
        return;
      }

      // We need two maps here to keep track controls with id and without id.
      // We can't just use map without id to cover all cases  because there
      // might be multiple elements with the same id.
      if (control->GetID()) {
        elementsIdToLabelStrings.LookupOrInsert(control->GetID())
            .AppendElements(std::move(labelStrings));
      } else {
        elementsToLabelStrings.LookupOrInsert(control).AppendElements(
            std::move(labelStrings));
      }
    }
  }

  nsTArray<AutofillParams> paramSet;
  paramSet.SetLength(aElements.Length());

  for (uint32_t i = 0; i < aElements.Length(); ++i) {
    auto& params = paramSet[i];
    const auto& element = aElements[i];

    const nsTArray<nsCString>* labelStrings = GetLabelStrings(
        element, elementsToLabelStrings, elementsIdToLabelStrings);

    bool idOrNameMatchDwfrmAndBml =
        IdOrNameMatchRegExp(element, RegexKey::DWFRM) &&
        IdOrNameMatchRegExp(element, RegexKey::BML);
    bool hasTemplatedValue = HasTemplatedValue(element);
    bool inputTypeNotNumbery = InputTypeNotNumbery(element);
    bool idOrNameMatchSubscription =
        IdOrNameMatchRegExp(element, RegexKey::SUBSCRIPTION);
    bool idOrNameMatchFirstAndLast =
        IdOrNameMatchRegExp(element, RegexKey::FIRST) &&
        IdOrNameMatchRegExp(element, RegexKey::LAST);

#define RULE_IMPL2(rule, type) params.m##type##Params[type##Params::rule]
#define RULE_IMPL(rule, type) RULE_IMPL2(rule, type)
#define RULE(rule) RULE_IMPL(rule, RULE_TYPE)

    // cc-number
#define RULE_TYPE CCNumber
    RULE(idOrNameMatchNumberRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_NUMBER);
    RULE(labelsMatchNumberRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_NUMBER);
    RULE(closestLabelMatchesNumberRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_NUMBER);
    RULE(placeholderMatchesNumberRegExp) =
        PlaceholderMatchesRegExp(element, RegexKey::CC_NUMBER);
    RULE(ariaLabelMatchesNumberRegExp) =
        AriaLabelMatchesRegExp(element, RegexKey::CC_NUMBER);
    RULE(idOrNameMatchGift) = IdOrNameMatchRegExp(element, RegexKey::GIFT);
    RULE(labelsMatchGift) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::GIFT);
    RULE(placeholderMatchesGift) =
        PlaceholderMatchesRegExp(element, RegexKey::GIFT);
    RULE(ariaLabelMatchesGift) =
        AriaLabelMatchesRegExp(element, RegexKey::GIFT);
    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
    RULE(inputTypeNotNumbery) = inputTypeNotNumbery;
#undef RULE_TYPE

    // cc-name
#define RULE_TYPE CCName
    RULE(idOrNameMatchNameRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_NAME);
    RULE(labelsMatchNameRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_NAME);
    RULE(closestLabelMatchesNameRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_NAME);
    RULE(placeholderMatchesNameRegExp) =
        PlaceholderMatchesRegExp(element, RegexKey::CC_NAME);
    RULE(ariaLabelMatchesNameRegExp) =
        AriaLabelMatchesRegExp(element, RegexKey::CC_NAME);
    RULE(idOrNameMatchFirst) = IdOrNameMatchRegExp(element, RegexKey::FIRST);
    RULE(labelsMatchFirst) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::FIRST);
    RULE(placeholderMatchesFirst) =
        PlaceholderMatchesRegExp(element, RegexKey::FIRST);
    RULE(ariaLabelMatchesFirst) =
        AriaLabelMatchesRegExp(element, RegexKey::FIRST);
    RULE(idOrNameMatchLast) = IdOrNameMatchRegExp(element, RegexKey::LAST);
    RULE(labelsMatchLast) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::LAST);
    RULE(placeholderMatchesLast) =
        PlaceholderMatchesRegExp(element, RegexKey::LAST);
    RULE(ariaLabelMatchesLast) =
        AriaLabelMatchesRegExp(element, RegexKey::LAST);
    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchFirstAndLast) = idOrNameMatchFirstAndLast;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
#undef RULE_TYPE

    // We only use Fathom to detect cc-number & cc-name fields for now.
    // Comment out code below instead of removing them to make it clear that
    // the current design is to support multiple rules.
/*
    Element* nextFillableField = NextField(aElements, i);
    Element* prevFillableField = PrevField(aElements, i);

    const nsTArray<nsCString>* nextLabelStrings = GetLabelStrings(
        nextFillableField, elementsToLabelStrings, elementsIdToLabelStrings);
    const nsTArray<nsCString>* prevLabelStrings = GetLabelStrings(
        prevFillableField, elementsToLabelStrings, elementsIdToLabelStrings);
    bool roleIsMenu = RoleIsMenu(element);

    // cc-type
#define RULE_TYPE CCType
    RULE(idOrNameMatchTypeRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_TYPE);
    RULE(labelsMatchTypeRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_TYPE);
    RULE(closestLabelMatchesTypeRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_TYPE);
    RULE(idOrNameMatchVisaCheckout) =
        IdOrNameMatchRegExp(element, RegexKey::VISA_CHECKOUT);
    RULE(ariaLabelMatchesVisaCheckout) =
        AriaLabelMatchesRegExp(element, RegexKey::VISA_CHECKOUT);
    RULE(isSelectWithCreditCardOptions) =
        IsSelectWithCreditCardOptions(element);
    RULE(isRadioWithCreditCardText) =
        IsRadioWithCreditCardText(element, labelStrings, aRv);
    if (aRv.Failed()) {
      return;
    }

    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
#undef RULE_TYPE

    // cc-exp
#define RULE_TYPE CCExp
    RULE(labelsMatchExpRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_EXP);
    RULE(closestLabelMatchesExpRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_EXP);
    RULE(placeholderMatchesExpRegExp) =
        PlaceholderMatchesRegExp(element, RegexKey::CC_EXP);
    RULE(labelsMatchExpWith2Or4DigitYear) = LabelMatchesRegExp(
        element, labelStrings, RegexKey::TWO_OR_FOUR_DIGIT_YEAR);
    RULE(placeholderMatchesExpWith2Or4DigitYear) =
        PlaceholderMatchesRegExp(element, RegexKey::TWO_OR_FOUR_DIGIT_YEAR);
    RULE(labelsMatchMMYY) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::MMYY);
    RULE(placeholderMatchesMMYY) =
        PlaceholderMatchesRegExp(element, RegexKey::MMYY);
    RULE(maxLengthIs7) = MaxLengthIs(element, 7);
    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
    RULE(isExpirationMonthLikely) = IsExpirationMonthLikely(element);
    RULE(isExpirationYearLikely) = IsExpirationYearLikely(element);
    RULE(idOrNameMatchMonth) = IdOrNameMatchRegExp(element, RegexKey::MONTH);
    RULE(idOrNameMatchYear) = IdOrNameMatchRegExp(element, RegexKey::YEAR);
    RULE(idOrNameMatchExpMonthRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_EXP_MONTH);
    RULE(idOrNameMatchExpYearRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_EXP_YEAR);
    RULE(idOrNameMatchValidation) =
        IdOrNameMatchRegExp(element, RegexKey::VALIDATION);
#undef RULE_TYPE

    // cc-exp-month
#define RULE_TYPE CCExpMonth
    RULE(idOrNameMatchExpMonthRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_EXP_MONTH);
    RULE(labelsMatchExpMonthRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_EXP_MONTH);
    RULE(closestLabelMatchesExpMonthRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_EXP_MONTH);
    RULE(placeholderMatchesExpMonthRegExp) =
        PlaceholderMatchesRegExp(element, RegexKey::CC_EXP_MONTH);
    RULE(ariaLabelMatchesExpMonthRegExp) =
        AriaLabelMatchesRegExp(element, RegexKey::CC_EXP_MONTH);
    RULE(idOrNameMatchMonth) = IdOrNameMatchRegExp(element, RegexKey::MONTH);
    RULE(labelsMatchMonth) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::MONTH);
    RULE(placeholderMatchesMonth) =
        PlaceholderMatchesRegExp(element, RegexKey::MONTH);
    RULE(ariaLabelMatchesMonth) =
        AriaLabelMatchesRegExp(element, RegexKey::MONTH);
    RULE(nextFieldIdOrNameMatchExpYearRegExp) =
        nextFillableField &&
        IdOrNameMatchRegExp(*nextFillableField, RegexKey::CC_EXP_YEAR);
    RULE(nextFieldLabelsMatchExpYearRegExp) =
        nextFillableField &&
        LabelMatchesRegExp(element, nextLabelStrings, RegexKey::CC_EXP_YEAR);
    RULE(nextFieldPlaceholderMatchExpYearRegExp) =
        nextFillableField &&
        PlaceholderMatchesRegExp(*nextFillableField, RegexKey::CC_EXP_YEAR);
    RULE(nextFieldAriaLabelMatchExpYearRegExp) =
        nextFillableField &&
        AriaLabelMatchesRegExp(*nextFillableField, RegexKey::CC_EXP_YEAR);
    RULE(nextFieldIdOrNameMatchYear) =
        nextFillableField &&
        IdOrNameMatchRegExp(*nextFillableField, RegexKey::YEAR);
    RULE(nextFieldLabelsMatchYear) =
        nextFillableField &&
        LabelMatchesRegExp(element, nextLabelStrings, RegexKey::YEAR);
    RULE(nextFieldPlaceholderMatchesYear) =
        nextFillableField &&
        PlaceholderMatchesRegExp(*nextFillableField, RegexKey::YEAR);
    RULE(nextFieldAriaLabelMatchesYear) =
        nextFillableField &&
        AriaLabelMatchesRegExp(*nextFillableField, RegexKey::YEAR);
    RULE(nextFieldMatchesExpYearAutocomplete) =
        nextFillableField &&
        NextFieldMatchesExpYearAutocomplete(nextFillableField);
    RULE(isExpirationMonthLikely) = IsExpirationMonthLikely(element);
    RULE(nextFieldIsExpirationYearLikely) =
        nextFillableField && IsExpirationYearLikely(*nextFillableField);
    RULE(maxLengthIs2) = MaxLengthIs(element, 2);
    RULE(placeholderMatchesMM) =
        PlaceholderMatchesRegExp(element, RegexKey::MM_MONTH);
    RULE(roleIsMenu) = roleIsMenu;
    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
#undef RULE_TYPE

    // cc-exp-year
#define RULE_TYPE CCExpYear
    RULE(idOrNameMatchExpYearRegExp) =
        IdOrNameMatchRegExp(element, RegexKey::CC_EXP_YEAR);
    RULE(labelsMatchExpYearRegExp) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::CC_EXP_YEAR);
    RULE(closestLabelMatchesExpYearRegExp) =
        ClosestLabelMatchesRegExp(element, RegexKey::CC_EXP_YEAR);
    RULE(placeholderMatchesExpYearRegExp) =
        PlaceholderMatchesRegExp(element, RegexKey::CC_EXP_YEAR);
    RULE(ariaLabelMatchesExpYearRegExp) =
        AriaLabelMatchesRegExp(element, RegexKey::CC_EXP_YEAR);
    RULE(idOrNameMatchYear) = IdOrNameMatchRegExp(element, RegexKey::YEAR);
    RULE(labelsMatchYear) =
        LabelMatchesRegExp(element, labelStrings, RegexKey::YEAR);
    RULE(placeholderMatchesYear) =
        PlaceholderMatchesRegExp(element, RegexKey::YEAR);
    RULE(ariaLabelMatchesYear) =
        AriaLabelMatchesRegExp(element, RegexKey::YEAR);
    RULE(previousFieldIdOrNameMatchExpMonthRegExp) =
        prevFillableField &&
        IdOrNameMatchRegExp(*prevFillableField, RegexKey::CC_EXP_MONTH);
    RULE(previousFieldLabelsMatchExpMonthRegExp) =
        prevFillableField &&
        LabelMatchesRegExp(element, prevLabelStrings, RegexKey::CC_EXP_MONTH);
    RULE(previousFieldPlaceholderMatchExpMonthRegExp) =
        prevFillableField &&
        PlaceholderMatchesRegExp(*prevFillableField, RegexKey::CC_EXP_MONTH);
    RULE(previousFieldAriaLabelMatchExpMonthRegExp) =
        prevFillableField &&
        AriaLabelMatchesRegExp(*prevFillableField, RegexKey::CC_EXP_MONTH);
    RULE(previousFieldIdOrNameMatchMonth) =
        prevFillableField &&
        IdOrNameMatchRegExp(*prevFillableField, RegexKey::MONTH);
    RULE(previousFieldLabelsMatchMonth) =
        prevFillableField &&
        LabelMatchesRegExp(element, prevLabelStrings, RegexKey::MONTH);
    RULE(previousFieldPlaceholderMatchesMonth) =
        prevFillableField &&
        PlaceholderMatchesRegExp(*prevFillableField, RegexKey::MONTH);
    RULE(previousFieldAriaLabelMatchesMonth) =
        prevFillableField &&
        AriaLabelMatchesRegExp(*prevFillableField, RegexKey::MONTH);
    RULE(previousFieldMatchesExpMonthAutocomplete) =
        prevFillableField &&
        PreviousFieldMatchesExpMonthAutocomplete(prevFillableField);
    RULE(isExpirationYearLikely) = IsExpirationYearLikely(element);
    RULE(previousFieldIsExpirationMonthLikely) =
        prevFillableField && IsExpirationMonthLikely(*prevFillableField);
    RULE(placeholderMatchesYYOrYYYY) =
        PlaceholderMatchesRegExp(element, RegexKey::YY_OR_YYYY);
    RULE(roleIsMenu) = roleIsMenu;
    RULE(idOrNameMatchSubscription) = idOrNameMatchSubscription;
    RULE(idOrNameMatchDwfrmAndBml) = idOrNameMatchDwfrmAndBml;
    RULE(hasTemplatedValue) = hasTemplatedValue;
#undef RULE_TYPE
*/

#undef RULE_IMPL2
#undef RULE_IMPL
#undef RULE

#define CALCULATE_SCORE(type, score)                                        \
  for (auto i : MakeEnumeratedRange(type##Params::Count)) {                 \
    (score) += params.m##type##Params[i] * kCoefficents.m##type##Params[i]; \
  }                                                                         \
  (score) = Sigmoid(score + k##type##Bias);

    // Calculating the final score of each rule
    FormAutofillConfidences score;
    CALCULATE_SCORE(CCNumber, score.mCcNumber)
    CALCULATE_SCORE(CCName, score.mCcName)

    // Comment out code that are not used right now
    // CALCULATE_SCORE(CCType, score.mCcType)
    // CALCULATE_SCORE(CCExp, score.mCcExp)
    // CALCULATE_SCORE(CCExpMonth, score.mCcExpMonth)
    // CALCULATE_SCORE(CCExpYear, score.mCcExpYear)

#undef CALCULATE_SCORE

    aResults.AppendElement(score);
  }
}

static StaticAutoPtr<FormAutofillImpl> sFormAutofillInstance;

static FormAutofillImpl* GetFormAutofillImpl() {
  if (!sFormAutofillInstance) {
    sFormAutofillInstance = new FormAutofillImpl();
    ClearOnShutdown(&sFormAutofillInstance);
  }
  return sFormAutofillInstance;
}

/* static */
void FormAutofillNative::GetFormAutofillConfidences(
    GlobalObject& aGlobal, const Sequence<OwningNonNull<Element>>& aElements,
    nsTArray<FormAutofillConfidences>& aResults, ErrorResult& aRv) {
  GetFormAutofillImpl()->GetFormAutofillConfidences(aGlobal, aElements,
                                                    aResults, aRv);
}

}  // namespace mozilla::dom
