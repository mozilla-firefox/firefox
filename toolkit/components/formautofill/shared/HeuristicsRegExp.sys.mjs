/* eslint-disable no-useless-concat */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// prettier-ignore
export const HeuristicsRegExp = {
  RULES: {
    email: undefined,
    tel: undefined,
    "tel-country-code" : undefined,
    "address-housenumber": undefined,
    "street-address": undefined,
    "address-line1": undefined,
    "address-line2": undefined,
    "address-line3": undefined,
    "postal-code": undefined,
    "address-level3": undefined,
    "address-level2": undefined,
    "address-level1": undefined,
    // Note: We place the `organization` field after the `address` fields, to
    // ensure that all address-related fields that might contain organization 
    // info are matched as address fields first.
    organization: undefined,
    country: undefined,
    // Note: We place the `cc-name` field for Credit Card first, because
    // it is more specific than the `name` field below and we want to check
    // for it before we catch the more generic one.
    "cc-name": undefined,
    name: undefined,
    "given-name": undefined,
    "additional-name": undefined,
    "family-name": undefined,
    "cc-csc": undefined,
    "cc-number": undefined,
    "cc-exp-month": undefined,
    "cc-exp-year": undefined,
    "cc-exp": undefined,
    "cc-type": undefined,
  },

  // regular expressions that only apply to label
  LABEL_RULES: {
    "address-line1": undefined,
    "address-line2": undefined,
  },

  RULE_SETS: [
    //=========================================================================
    // Firefox-specific rules
    {
      "street-address": "ulica(.*(numer|nr))?", // pl-PL
      "address-line1": "addrline1|address_1|addl1",
      "address-line2":
        "addrline2|address_2|addl2" +
        "|landmark", // common in IN
      "address-line3": "addrline3|address_3|addl3",
      "address-level2": 
        "città" + // it-IT
        "|miasto|miejscowosc|miejscowość", //pl-PL
      "address-housenumber":
        "(house|building)\\s*number|hausnummer|haus|house[a-z\-]*n(r|o)" +
        "|n[úu]mero",
      "address-level3":
        "(^address-?level-?3$)" +
        "|neighbou*rhood|barrio|bairro|colonia|suburb", // en/es/pt/mx/au/nz
      "postal-code": 
        "^PLZ(\\b|\\*)" + // de-DE
        "|kod.?pocztowy", // pl-PL
      "given-name": "imię", // pl-PL
      "additional-name": "apellido.?materno|lastlastname",
      "family-name": "nazwisko",
      "cc-name":
        "accountholdername" +
        "|titulaire", // fr-FR
      "cc-number":
        "(cc|kk)nr",    // de-DE
      "cc-exp":
        "ważna.*do" +        // pl-PL
        "|data.*ważności" +  // pl-PL
        "|mm\\s*[\\-\\/]\\s*yy" +  // en-US
        "|mm\\s*[\\-\\/]\\s*aa" +  // es-ES
        "|mm\\s*[\\-\\/]\\s*jj" +  // de-AT
        "|vervaldatum",            // nl-NL
      "cc-exp-month":
        "month" +
        "|(cc|kk)month" +    // de-DE
        "|miesiąc" +         // pl-PL
        "|mes" +             // es-ES
        "|maand",            // nl-NL
      "cc-exp-year":
        "year" +
        "|(cc|kk)year" +     // de-DE
        "|rok" +             // pl-PL
        "|(anno|año)" +      // es-ES
        "|jaar",             // nl-NL
      "cc-type":
        "(cc|card).*(type)" +
        "|kartenmarke" +     // de-DE
        "|typ.*karty",       // pl-PL
      "cc-csc":
        "(\\bcvn\\b|\\bcvv\\b|\\bcvc\\b|\\bcsc\\b|\\bcvd\\b|\\bcid\\b|\\bccv\\b)",
      "tel-country-code":
        "phone.*country|country.*phone" +
        "tel.*country|country.*tel",
      "tel": "(numer|nr)?\\.?telefonu", //pl-PL
    },

    //=========================================================================
    // These are the rules used by Bitwarden [0], converted into RegExp form.
    // [0] https://github.com/bitwarden/browser/blob/c2b8802201fac5e292d55d5caf3f1f78088d823c/src/services/autofill.service.ts#L436
    {
      email: "(^e-?mail$)|(^email-?address$)",

      tel:
        "(^phone$)" +
        "|(^mobile$)" +
        "|(^mobile-?phone$)" +
        "|(^tel$)" +
        "|(^telephone$)" +
        "|(^phone-?number$)",

      organization:
        "(^company$)" +
        "|(^company-?name$)" +
        "|(^organization$)" +
        "|(^organization-?name$)",

      "street-address":
        "(^address$)" +
        "|(^street-?address$)" +
        "|(^addr$)" +
        "|(^street$)" +
        "|(^mailing-?addr(ess)?$)" + // Modified to not grab lines, below
        "|(^billing-?addr(ess)?$)" + // Modified to not grab lines, below
        "|(^mail-?addr(ess)?$)" + // Modified to not grab lines, below
        "|(^bill-?addr(ess)?$)", // Modified to not grab lines, below

      "address-line1":
        "(^address-?1$)" +
        "|(^address-?line-?1$)" +
        "|(^addr-?1$)" +
        "|(^street-?1$)",

      "address-line2":
        "(^address-?2$)" +
        "|(^address-?line-?2$)" +
        "|(^addr-?2$)" +
        "|(^street-?2$)",

      "address-line3":
        "(^address-?3$)" +
        "|(^address-?line-?3$)" +
        "|(^addr-?3$)" +
        "|(^street-?3$)",

      "address-level2":
        "(^city$)" +
        "|(^town$)" +
        "|(^address-?level-?2$)" +
        "|(^address-?city$)" +
        "|(^address-?town$)",

      "address-level1":
        "(^state$)" +
        "|(^province$)" +
        "|(^provence$)" +
        "|(^address-?level-?1$)" +
        "|(^address-?state$)" +
        "|(^address-?province$)",

      "postal-code":
        "(^postal$)" +
        "|(^zip$)" +
        "|(^zip2$)" +
        "|(^zip-?code$)" +
        "|(^postal-?code$)" +
        "|(^post-?code$)" +
        "|(^address-?zip$)" +
        "|(^address-?postal$)" +
        "|(^address-?code$)" +
        "|(^address-?postal-?code$)" +
        "|(^address-?zip-?code$)",

      country:
        "(^country$)" +
        "|(^country-?code$)" +
        "|(^country-?name$)" +
        "|(^address-?country$)" +
        "|(^address-?country-?name$)" +
        "|(^address-?country-?code$)",

      name: "(^name$)|full-?name|your-?name",

      "given-name":
        "(^f-?name$)" +
        "|(^first-?name$)" +
        "|(^given-?name$)" +
        "|(^first-?n$)",

      "additional-name":
        "(^m-?name$)" +
        "|(^middle-?name$)" +
        "|(^additional-?name$)" +
        "|(^middle-?initial$)" +
        "|(^middle-?n$)" +
        "|(^middle-?i$)",

      "family-name":
        "(^l-?name$)" +
        "|(^last-?name$)" +
        "|(^s-?name$)" +
        "|(^surname$)" +
        "|(^family-?name$)" +
        "|(^family-?n$)" +
        "|(^last-?n$)",

      "cc-name":
        "cc-?name" +
        "|card-?name" +
        "|cardholder-?name" +
        "|cardholder",
        // "|(^name$)" + // Removed to avoid overwriting "name", above.

      "cc-number":
        "cc-?number" +
        "|cc-?num" +
        "|card-?number" +
        "|card-?num" +
        "|(^number$)" +
        "|(^cc$)" +
        "|cc-?no" +
        "|card-?no" +
        "|(^credit-?card$)" +
        "|numero-?carte" +
        "|(^carte$)" +
        "|(^carte-?credit$)" +
        "|num-?carte" +
        "|cb-?num",

      "cc-exp":
        "(^cc-?exp$)" +
        "|(^card-?exp$)" +
        "|(^cc-?expiration$)" +
        "|(^card-?expiration$)" +
        "|(^cc-?ex$)" +
        "|(^card-?ex$)" +
        "|(^card-?expire$)" +
        "|(^card-?expiry$)" +
        "|(^validite$)" +
        "|(^expiration$)" +
        "|(^expiry$)" +
        "|mm-?yy" +
        "|mm-?yyyy" +
        "|yy-?mm" +
        "|yyyy-?mm" +
        "|expiration-?date" +
        "|payment-?card-?expiration" +
        "|(^payment-?cc-?date$)",

      "cc-exp-month":
        "(^exp-?month$)" +
        "|(^cc-?exp-?month$)" +
        "|(^cc-?month$)" +
        "|(^card-?month$)" +
        "|(^cc-?mo$)" +
        "|(^card-?mo$)" +
        "|(^exp-?mo$)" +
        "|(^card-?exp-?mo$)" +
        "|(^cc-?exp-?mo$)" +
        "|(^card-?expiration-?month$)" +
        "|(^expiration-?month$)" +
        "|(^cc-?mm$)" +
        "|(^cc-?m$)" +
        "|(^card-?mm$)" +
        "|(^card-?m$)" +
        "|(^card-?exp-?mm$)" +
        "|(^cc-?exp-?mm$)" +
        "|(^exp-?mm$)" +
        "|(^exp-?m$)" +
        "|(^expire-?month$)" +
        "|(^expire-?mo$)" +
        "|(^expiry-?month$)" +
        "|(^expiry-?mo$)" +
        "|(^card-?expire-?month$)" +
        "|(^card-?expire-?mo$)" +
        "|(^card-?expiry-?month$)" +
        "|(^card-?expiry-?mo$)" +
        "|(^mois-?validite$)" +
        "|(^mois-?expiration$)" +
        "|(^m-?validite$)" +
        "|(^m-?expiration$)" +
        "|(^expiry-?date-?field-?month$)" +
        "|(^expiration-?date-?month$)" +
        "|(^expiration-?date-?mm$)" +
        "|(^exp-?mon$)" +
        "|(^validity-?mo$)" +
        "|(^exp-?date-?mo$)" +
        "|(^cb-?date-?mois$)" +
        "|(^date-?m$)",

      "cc-exp-year":
        "(^exp-?year$)" +
        "|(^cc-?exp-?year$)" +
        "|(^cc-?year$)" +
        "|(^card-?year$)" +
        "|(^cc-?yr$)" +
        "|(^card-?yr$)" +
        "|(^exp-?yr$)" +
        "|(^card-?exp-?yr$)" +
        "|(^cc-?exp-?yr$)" +
        "|(^card-?expiration-?year$)" +
        "|(^expiration-?year$)" +
        "|(^cc-?yy$)" +
        "|(^cc-?y$)" +
        "|(^card-?yy$)" +
        "|(^card-?y$)" +
        "|(^card-?exp-?yy$)" +
        "|(^cc-?exp-?yy$)" +
        "|(^exp-?yy$)" +
        "|(^exp-?y$)" +
        "|(^cc-?yyyy$)" +
        "|(^card-?yyyy$)" +
        "|(^card-?exp-?yyyy$)" +
        "|(^cc-?exp-?yyyy$)" +
        "|(^expire-?year$)" +
        "|(^expire-?yr$)" +
        "|(^expiry-?year$)" +
        "|(^expiry-?yr$)" +
        "|(^card-?expire-?year$)" +
        "|(^card-?expire-?yr$)" +
        "|(^card-?expiry-?year$)" +
        "|(^card-?expiry-?yr$)" +
        "|(^an-?validite$)" +
        "|(^an-?expiration$)" +
        "|(^annee-?validite$)" +
        "|(^annee-?expiration$)" +
        "|(^expiry-?date-?field-?year$)" +
        "|(^expiration-?date-?year$)" +
        "|(^cb-?date-?ann$)" +
        "|(^expiration-?date-?yy$)" +
        "|(^expiration-?date-?yyyy$)" +
        "|(^validity-?year$)" +
        "|(^exp-?date-?year$)" +
        "|(^date-?y$)",

      "cc-type":
        "(^cc-?type$)" +
        "|(^card-?type$)" +
        "|(^card-?brand$)" +
        "|(^cc-?brand$)" +
        "|(^cb-?type$)",
    },

    //=========================================================================
    // These rules are from Chromium source codes [1]. Most of them
    // converted to JS format have the same meaning with the original ones except
    // the first line of "address-level1".
    // [1] https://source.chromium.org/chromium/chromium/src/+/master:components/autofill/core/common/autofill_regex_constants.cc
    {
      // ==== Email ====
      email:
        "e.?mail" +
        "|courriel" + // fr
        "|correo.*electr(o|ó)nico" + // es-ES
        "|メールアドレス" + // ja-JP
        "|Электронной.?Почты" + // ru
        "|邮件|邮箱" + // zh-CN
        "|電郵地址" + // zh-TW
        "|ഇ-മെയില്‍|ഇലക്ട്രോണിക്.?" +
        "മെയിൽ" + // ml
        "|ایمیل|پست.*الکترونیک" + // fa
        "|ईमेल|इलॅक्ट्रॉनिक.?मेल" + // hi
        "|(\\b|_)eposta(\\b|_)" + // tr
        "|(?:이메일|전자.?우편|[Ee]-?mail)(.?주소)?", // ko-KR

      // ==== Telephone ====
      tel:
        "phone|mobile|contact.?number" +
        "|telefonnummer" + // de-DE
        "|telefono|teléfono" + // es
        "|telfixe" + // fr-FR
        "|電話" + // ja-JP
        "|telefone|telemovel" + // pt-BR, pt-PT
        "|телефон" + // ru
        "|मोबाइल" + // hi for mobile
        "|(\\b|_|\\*)telefon(\\b|_|\\*)" + // tr
        "|电话" + // zh-CN
        "|മൊബൈല്‍" + // ml for mobile
        "|(?:전화|핸드폰|휴대폰|휴대전화)(?:.?번호)?", // ko-KR

      // ==== Address Fields ====
      organization:
        "company|business|organization|organisation" +
        // In order to support webkit we convert all negative lookbehinds to a capture group
        // (?<!not)word -> (?<neg>notword)|word
        // TODO: Bug 1829583
        "|(?<neg>confirma)" +
        "|firma|firmenname" + // de-DE
        "|empresa" + // es
        "|societe|société" + // fr-FR
        "|ragione.?sociale" + // it-IT
        "|会社" + // ja-JP
        "|название.?компании" + // ru
        "|单位|公司" + // zh-CN
        "|شرکت" + // fa
        "|회사|직장", // ko-KR

      "street-address": "streetaddress|street-address",
      "address-line1":
        "^address$|address[_-]?line(one)?|address1|addr1|street" +
        "|(?:shipping|billing)address$" +
        "|strasse|straße" + // de-DE
        "|house.?name" + // en-GB
        "|direccion|dirección" + // es
        "|adresse" + // fr-FR
        "|indirizzo" + // it-IT
        "|^住所$|住所1" + // ja-JP
        "|morada" + // pt-BR, pt-PT
        // In order to support webkit we convert all negative lookbehinds to a capture group
        // (?<!not)word -> (?<neg>notword)|word
        // TODO: Bug 1829583
        "|(?<neg>identificação do endereço)" +
        "|(endereço)" + // pt-BR, pt-PT
        "|Адрес" + // ru
        "|地址" + // zh-CN
        "|(\\b|_)adres(?! (başlığı(nız)?|tarifi))(\\b|_)" + // tr
        "|^주소.?$|주소.?1", // ko-KR

      "address-line2":
        "address[_-]?line(2|two)|address2|addr2|street|suite|unit(?!e)" + // Firefox adds `(?!e)` to unit to skip `United State`
        "|adresszusatz|ergänzende.?angaben" + // de-DE
        "|direccion2|colonia|adicional" + // es
        "|addresssuppl|complementnom|appartement" + // fr-FR
        "|indirizzo2" + // it-IT
        "|住所2" + // ja-JP
        "|complemento|addrcomplement" + // pt-BR, pt-PT
        "|Улица" + // ru
        "|地址2" + // zh-CN
        "|주소.?2", // ko-KR

      "address-line3":
        "address[_-]?line(3|three)|address3|addr3|street|suite|unit(?!e)" + // Firefox adds `(?!e)` to unit to skip `United State`
        "|adresszusatz|ergänzende.?angaben" + // de-DE
        "|direccion3|colonia|adicional" + // es
        "|addresssuppl|complementnom|appartement" + // fr-FR
        "|indirizzo3" + // it-IT
        "|住所3" + // ja-JP
        "|complemento|addrcomplement" + // pt-BR, pt-PT
        "|Улица" + // ru
        "|地址3" + // zh-CN
        "|주소.?3", // ko-KR

      "address-level2":
        "city|town" +
        "|\\bort\\b|stadt" + // de-DE
        "|suburb" + // en-AU
        "|ciudad|provincia|localidad|poblacion" + // es
        "|ville|commune" + // fr-FR
        "|localita" + // it-IT
        "|市区町村" + // ja-JP
        "|cidade" + // pt-BR, pt-PT
        "|Город" + // ru
        "|市" + // zh-CN
        "|分區" + // zh-TW
        "|شهر" + // fa
        "|शहर" + // hi for city
        "|ग्राम|गाँव" + // hi for village
        "|നഗരം|ഗ്രാമം" + // ml for town|village
        "|((\\b|_|\\*)([İii̇]l[cç]e(miz|niz)?)(\\b|_|\\*))" + // tr
        "|^시[^도·・]|시[·・]?군[·・]?구", // ko-KR

      "address-level1":
        // In order to support webkit we convert all negative lookbehinds to a capture group
        // (?<!not)word -> (?<neg>notword)|word
        // TODO: Bug 1829583
        "(?<neg>united?.state|hist?.state|history?.state)" +
        "|state|county|region|province" +
        "|principality" + // en-UK
        "|都道府県" + // ja-JP
        "|estado|provincia" + // pt-BR, pt-PT
        "|область" + // ru
        "|省" + // zh-CN
        "|地區" + // zh-TW
        "|സംസ്ഥാനം" + // ml
        "|استان" + // fa
        "|राज्य" + // hi
        "|((\\b|_|\\*)(eyalet|[şs]ehir|[İii̇]l(imiz)?|kent)(\\b|_|\\*))" + // tr
        "|^시[·・]?도", // ko-KR

      "postal-code":
        "zip|postal|post.*code|pcode" +
        "|pin.?code" + // en-IN
        "|postleitzahl" + // de-DE
        "|\\bcp\\b" + // es
        "|\\bcdp\\b" + // fr-FR
        "|\\bcap\\b" + // it-IT
        "|郵便番号" + // ja-JP
        "|codigo|codpos|\\bcep\\b" + // pt-BR, pt-PT
        "|Почтовый.?Индекс" + // ru
        "|पिन.?कोड" + // hi
        "|പിന്‍കോഡ്" + // ml
        "|邮政编码|邮编" + // zh-CN
        "|郵遞區號" + // zh-TW
        "|(\\b|_)posta kodu(\\b|_)" + // tr
        "|우편.?번호", // ko-KR

      country:
        "country|countries" +
        "|país|pais" + // es
        "|(\\b|_)land(\\b|_)(?!.*(mark.*))" + // de-DE landmark is a type in india.
        // In order to support webkit we convert all negative lookbehinds to a capture group
        // (?<!not)word -> (?<neg>notword)|word
        // TODO: Bug 1829583
        "|(?<neg>入国|出国)" +
        "|国" + // ja-JP
        "|国家" + // zh-CN
        "|국가|나라" + // ko-KR
        "|(\\b|_)(ülke|ulce|ulke)(\\b|_)" + // tr
        "|کشور", // fa

      // ==== Name Fields ====
      "cc-name":
        "card.?(?:holder|owner)|name.*(\\b)?on(\\b)?.*card" +
        "|^(credit[-\\s]?card|card).*name|cc.?full.?name" +
        "|karteninhaber" + // de-DE
        "|nombre.*tarjeta" + // es
        "|nom.*carte" + // fr-FR
        "|nome.*cart" + // it-IT
        "|名前" + // ja-JP
        "|Имя.*карты" + // ru
        "|信用卡开户名|开户名|持卡人姓名" + // zh-CN
        "|持卡人姓名", // zh-TW

      name:
        "^name|full.?name|your.?name|customer.?name|bill.?name|ship.?name" +
        "|name.*first.*last|firstandlastname" +
        "|nombre.*y.*apellidos" + // es
        "|^nom(?!bre)" + // fr-FR
        "|お名前|氏名" + // ja-JP
        "|^nome" + // pt-BR, pt-PT
        "|نام.*نام.*خانوادگی" + // fa
        "|姓名" + // zh-CN
        "|(\\b|_|\\*)ad[ı]? soyad[ı]?(\\b|_|\\*)" + // tr
        "|성명", // ko-KR

      "given-name":
        "first.*name|initials|fname|first$|given.*name" +
        "|vorname" + // de-DE
        "|nombre" + // es
        "|forename|prénom|prenom" + // fr-FR
        "|名" + // ja-JP
        "|nome" + // pt-BR, pt-PT
        "|Имя" + // ru
        "|نام" + // fa
        "|이름" + // ko-KR
        "|പേര്" + // ml
        "|(\\b|_|\\*)(isim|ad|ad(i|ı|iniz|ınız)?)(\\b|_|\\*)" + // tr
        "|नाम", // hi

      "additional-name":
        "middle.*name|mname|middle$|middle.*initial|m\\.i\\.|mi$|\\bmi\\b",

      "family-name":
        "last.*name|lname|surname|last$|secondname|family.*name" +
        "|nachname" + // de-DE
        "|apellidos?" + // es
        "|famille|^nom(?!bre)" + // fr-FR
        "|cognome" + // it-IT
        "|姓" + // ja-JP
        "|apelidos|surename|sobrenome" + // pt-BR, pt-PT
        "|Фамилия" + // ru
        "|نام.*خانوادگی" + // fa
        "|उपनाम" + // hi
        "|മറുപേര്" + // ml
        "|(\\b|_|\\*)(soyisim|soyad(i|ı|iniz|ınız)?)(\\b|_|\\*)" + // tr
        "|\\b성(?:[^명]|\\b)", // ko-KR

      // ==== Credit Card Fields ====
      // Note: `cc-name` expression has been moved up, above `name`, in
      // order to handle specialization through ordering.
      "cc-number":
        "(add)?(?:card|cc|acct).?(?:number|#|no|num|field(?!s)|pan)" +
        // In order to support webkit we convert all negative lookbehinds to a capture group
        // (?<!not)word -> (?<neg>notword)|word
        // TODO: Bug 1829583
        "|(?<neg>telefonnummer|hausnummer|personnummer|fødselsnummer)" + // de-DE, sv-SE, no
        "|nummer" +
        "|カード番号" + // ja-JP
        "|Номер.*карты" + // ru
        "|信用卡号|信用卡号码" + // zh-CN
        "|信用卡卡號" + // zh-TW
        "|카드" + // ko-KR
        // es/pt/fr
        "|(numero|número|numéro)(?!.*(document|fono|phone|réservation))",

      "cc-exp-month":
        "expir|exp.*mo|exp.*date|ccmonth|cardmonth|addmonth" +
        "|gueltig|gültig|monat" + // de-DE
        "|fecha" + // es
        "|date.*exp" + // fr-FR
        "|scadenza" + // it-IT
        "|有効期限" + // ja-JP
        "|validade" + // pt-BR, pt-PT
        "|Срок действия карты" + // ru
        "|月", // zh-CN

      "cc-exp-year":
        "exp|^/|(add)?year" +
        "|ablaufdatum|gueltig|gültig|jahr" + // de-DE
        "|fecha" + // es
        "|scadenza" + // it-IT
        "|有効期限" + // ja-JP
        "|validade" + // pt-BR, pt-PT
        "|Срок действия карты" + // ru
        "|年|有效期", // zh-CN

      "cc-exp":
        "expir|exp.*date|^expfield$" +
        "|gueltig|gültig" + // de-DE
        "|fecha" + // es
        "|date.*exp" + // fr-FR
        "|scadenza" + // it-IT
        "|有効期限" + // ja-JP
        "|validade" + // pt-BR, pt-PT
        "|Срок действия карты", // ru

      "cc-csc":
        "verification|card.?identification|security.?code|card.?code" +
        "|security.?value" +
        "|security.?number|card.?pin|c-v-v" +
        // We omit this regexp in favor of being less generic.
        // See "Firefox-specific" rules for cc-csc
        // "|(cvn|cvv|cvc|csc|cvd|cid|ccv)(field)?" +
        "|\\bcid\\b",
    },
  ],

  LABEL_RULE_SETS: [
    {
      "address-line1":
        "(^\\W*address)" +
        "|(address\\W*$)" +
        "|(?:shipping|billing|mailing|pick.?up|drop.?off|delivery|sender|postal|" +
        "recipient|home|work|office|school|business|mail)[\\s\\-]+address" +
        "|address\\s+(of|for|to|from)" +
        "|adresse" +                         // fr-FR
        "|indirizzo" +                       // it-IT
        "|住所" +                            // ja-JP
        "|地址" +                            // zh-CN
        "|(\\b|_)adres(?! tarifi)(\\b|_)" +  // tr
        "|주소" +                            // ko-KR
        "|^alamat" +                         // id
        // Should contain street and any other address component, in any order
        "|street.*(house|building|apartment|floor)" +  // en
        "|(house|building|apartment|floor).*street" +
        "|(sokak|cadde).*(apartman|bina|daire|mahalle)" +  // tr
        "|(apartman|bina|daire|mahalle).*(sokak|cadde)" +
        "|улиц.*(дом|корпус|квартир|этаж)|(дом|корпус|квартир|этаж).*улиц",  // ru
    },
    {
      "address-line2":
        "address|line" +
        "|house|building|apartment|floor" +    // de-DE
        "|apartamento|" +    // pt
        "|adresse" +      // fr-FR
        "|indirizzo" +    // it-IT
        "|地址" +         // zh-CN
        "|주소" +         // ko-KR
        "|mieszkan(ie|ia)",         // pl-PL
    },
  ],

  EXTRA_RULES: {
    "lookup":
      "lookup|search|suchen",
  },

  _getRules(rules, rulesets) {
    function computeRule(name) {
      let regexps = [];
      rulesets.forEach(set => {
        if (set[name]) {
          // Add the rule.
          // We make the regex lower case so that we can match it against the
          // lower-cased field name and get a rough equivalent of a case-insensitive
          // match. This avoids a performance cliff with the "iu" flag on regular
          // expressions.
          regexps.push(`(${set[name].toLowerCase()})`.normalize("NFKC"));
        }
      });

      const value = new RegExp(regexps.join("|"), "gu");

      Object.defineProperty(rules, name, { get: undefined });
      Object.defineProperty(rules, name, { value });
      return value;
    }

    Object.keys(rules).forEach(field =>
      Object.defineProperty(rules, field, {
        get() {
          return computeRule(field);
        },
      })
    );

    return rules;
  },

  getLabelRules() {
    return this._getRules(this.LABEL_RULES, this.LABEL_RULE_SETS);
  },

  getRules() {
    return this._getRules(this.RULES, this.RULE_SETS);
  },

  getExtraRules(fieldName) {
    return new RegExp(this.EXTRA_RULES[fieldName], "iug");
  }
};

export default HeuristicsRegExp;
