// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

//
// Author: dsites@google.com (Dick Sites)
//
// Unit test compact language detector, CLD2
//

// Test strings.
/* eslint-disable no-unused-vars */
const kTeststr_en =
  "confiscation of goods is assigned as the penalty part most of the courts " +
  "consist of members and when it is necessary to bring public cases before a " +
  "jury of members two courts combine for the purpose the most important cases " +
  "of all are brought jurors or";

const kTeststr_aa_Latn =
  " nagay tanito nagay tanto nagayna naharsi nahrur nake nala nammay nammay haytu nanu narig ne ni num numu o obare obe obe obisse oggole ogli olloyta ongorowe orbise othoga r rabe rade ra e rage rakub rasitte rasu reyta rog ruddi ruga s sa al bada sa ala";
const kTeststr_ab_Cyrl =
  " а зуа абзиара дақәшәоит ан лыбзиабара ахә амаӡам ауаҩы игәы иҭоу ихы иҿы ианубаалоит аҧҳәыс ҧшӡа ахацәа лышьҭоуп аҿаасҭа лара дрышьҭоуп";
const kTeststr_af_Latn =
  " aam skukuza die naam beteken hy wat skoonvee of hy wat alles onderstebo keer wysig bosveldkampe boskampe is kleiner afgeleë ruskampe wat oor min fasiliteite beskik daar is geen restaurante of winkels nie en slegs oornagbesoekers word toegelaat bateleur";
const kTeststr_ak_Latn =
  "Wɔwoo Hilla Limann Mumu-Ɔpɛnimba 12 afe 1934. Wɔwoo no wɔ Gwollu wɔ Sisala Mantaw mu Nna ne maame yɛ Mma Hayawah. Ne papa so nna ɔyɛ Babini Yomu. Ɔwarr Fulera Limann ? Ne mba yɛ esuon-- Lariba Montia [wɔwoo no Limann]; Baba Limann; Sibi Andan [wɔwoo no Limann]; Lida Limann; Danni Limann; Zilla Limann na Salma Limann. Ɔtenaa ase kɔpemm Sanda-Kwakwa da ɛtɔ so 23 wɔ afe 1998 wɔ ?.";
const kTeststr_am_Ethi =
  " ለመጠይቅ ወደ እስክንድርያ ላኩዋቸውና የእስክንድርያ ጳጳስ አቴናስዮስ ፍሬምንጦስን እራሳቸውን ሾመው ልከዋል ከዚያ እስከ ዓ ም ድረስ የኢትዮጵያ አቡነ";
const kTeststr_ar_Arab = "احتيالية بيع أي حساب";
const kTeststr_as_Beng =
  "অঞ্চল নতুন সদস্যবৃন্দ সকলোৱে ভৰ্তি হব পাৰে মুল পৃষ্ঠা জন লেখক গুগ ল দল সাৰাংশ ই পত্ৰ টা বাৰ্তা এজন";
const kTeststr_ay_Latn =
  " aru wijar aru ispañula ukaran aru witanam aru kurti aru kalis aru warani aru malta aru yatiyawi niya jakitanaka isluwiñ aru lmir phuran aru masirunan aru purtukal aru kruwat aru jakira urtu aru inklisa pirsan aru suyku aru malay aru jisk aptayma thaya";
const kTeststr_az_Arab =
  " آذربایجان دا انسان حاقلاری ائوی آچیلاجاق ب م ت ائلچيسي برمه موخاليفتي نين ليدئري ايله گؤروشه بيليب ترس شوونيسم فارس از آزادي ملتهاي تورکمن";
const kTeststr_az_Latn =
  " a az qalıb breyn rinq intellektual oyunu üzrə yarışın zona mərhələləri keçirilib miq un qalıqlarının dənizdən çıxarılması davam edir məhəmməd peyğəmbərin karikaturalarını çap edən qəzetin baş redaktoru iş otağında ölüb";
const kTeststr_ba_Cyrl =
  " арналђан бындай ђилми эш тіркињлњ тњјге тапєыр нњшер ителњ ғинуар бєхет именлектє етешлектє ауыл ўќмерџєре хеџмєт юлын ћайлаѓанда";
const kTeststr_be_Cyrl =
  " а друкаваць іх не было тэхнічна магчыма бліжэй за вільню тым самым часам нямецкае кіраўніцтва прапаноўвала апроч ўвядзення лацінкі яе";
const kTeststr_bg_Cyrl =
  " а дума попада в състояние на изпитание ключовите думи с предсказана малко под то изискване на страниците за търсене в";
// From 10% testing part of new lang=bh scrape
const kTeststr_bh_Deva =
  "काल में उनका हमला से बचे खाती एहिजा भाग के अइले आ भोजपुर नाम से नगर बसवले. एकरा बारे में विस्तार से जानकारी नीचे दीहल गइल बा. बाकिर आश्चर्यजनक रूप से मालवा के राजा भोज के बिहार आवे आ भोजपुर नगर बसावे आ चाहे भोजपुरी के साथे उनकर कवनो संबंध होखे के कवनो जानकारी भोपाल के भोज संस्थान आ चाहे मध्य प्रदेश के इतिहासकार लोगन के तनिको नइखे. हालांकि ऊ सब लोग एह बात के मानत बा कि एकरा बारे में अबहीं तकले मूर्ति बनवइलें. राजा भोज के जवना जगहा पऽ वाग्देवी के दर्शन भइल रहे, ओही स्थान पऽ एह मूर्ति के स्थापना कइल गइल. अब अगर एह मंदिर के एह शिलालेख के तस्वीर (पृष्ठ संख्या 33 पऽ प्रकाशित) रउआ धेयान से देखीं तऽ एकरा पऽ कैथी लिपि में -सीताराम- लिखल साफ लउकत बा. कैथी भोजपुरी के बहुत प्रचलित लिपि रहल बिया. एकरा बारे में कवनो शंका संदेह बिहार-यूपी के जानकार लोगन में नइखे. एल. एस. एस. वो माले के लिखल पढ़ीं ";

const kTeststr_bi_Latn =
  " king wantaem nomo hem i sakem setan mo ol rabis enjel blong hem oli aot long heven oli kamdaon long wol taswe ol samting oli kam nogud olgeta long wol ya stat long revelesen ol faet kakae i sot ol sik mo fasin blong brekem loa oli kam antap olgeta samting";
const kTeststr_blu_Latn =
  " Kuv hlub koj txawm lub ntuj yuav si ntshi nphaus los kuv tsis ua siab nkaug txawm ntiab teb yuav si ntshi nphaus los kuv tseem ua lon tsaug vim kuv hlub koj tag lub siab";
const kTeststr_blu_Latn2 =
  "Kuv hnov Txhiaj Xeeb Vaj, co-owner of Hmong Village Shopping Center, hais ua hnub ua hmo tias kom Hmoob yuav tsum txhawb Hmoob thiab listed cov mini-shops uas nyob rau hauv nws lub MALL txhua txhua kom sawv daws mus txhawb, tiam sis uas cas zaum twg twb pom nws mus kav kiav hauv taj laj qhabmeem (Sun Foods) xwb tiag. Nag hmo kuv pom nws mus shopping nrog nws poj niam hauv Sun Foods. Thaum tawm mus txog nraum parking lot kuv thiaj txhob txwm mus ze ze seb ua li nws mus yuav dab tsi tiag, thiab seb tej uas nws yuav ntawd puas muaj nyob ntawm tej kiab khw Hmoob. Surprised!!! Vuag.... txhua yam nws yuav hauv Sun Foods peb Hmoob cov khw yeej muaj tag nrho. Peb niaj hnub nqua hu kom Hmoob yuav tsum pab Hmoob yog pab li no lod?";
// From 10% testing part of new lang=bn scrape
const kTeststr_bn_Beng =
  "গ্যালারির ৩৮ বছর পূর্তিতে মূল্যছাড় অর্থনীতি বিএনপির ওয়াক আউট তপন চৌধুরী হারবাল অ্যাসোসিয়েশনের সভাপতি আন্তর্জাতিক পরামর্শক বোর্ড দিয়ে শরিয়াহ্ ইনন্ডেক্স করবে সিএসই মালিকপক্ষের কান্না, শ্রমিকের অনিশ্চয়তা মতিঝিলে সমাবেশ নিষিদ্ধ: এফবিসিসিআইয়ের ধন্যবাদ বিনোদন বিশেষ প্রতিবেদন বাংলালিংকের গ্র্যান্ডমাস্টার সিজন-৩ ব্রাজিলে বিশ্বকাপ ফুটবল আয়োজনবিরোধী বিক্ষোভ দেশের নিরাপত্তার  চেয়ে অনেক বেশি সচেতন । প্রার্থীদের দক্ষতা  ও যোগ্যতার পাশাপাশি তারা জাতীয় ইস্যুগুলোতে প্রাধান্য দিয়েছেন । ” পাঁচটি সিটিতে ২০ লাখ ভোটারদের দিয়ে জাতীয় নির্বাচনে ৮ কোটি ভোটারদের সঙ্গে তুলনা করা যাবে কি একজন দর্শকের এমন প্রশ্নে জবাবে আব্দুল্লাহ আল নোমান বলেন , “ এই পাঁচটি সিটি কর্পোরেশন নির্বাচন দেশের পাঁচটি বড় বিভাগের প্রতিনিধিত্ব করছে । এছাড়া এখানকার ভোটার রা সবাই সচেতন । তারা";

// From 10% testing part of new lang=bo scrape
const kTeststr_bo_Tibt =
  " ་གྱིས་ཁ་ཆེའི་ཕྱག་འཚལ་ཁང་ཞིག་བཤིག་སྲིད་པ། ཡར་ཀླུང་གཙང་པོར་ཆ ུ་མཛོང་བརྒྱག་རྒྱུའི་ལས་འཆར་ལ་རྒྱ་གར་གྱི་སེམས་ཚབས། རྒྱ་གརགྱི་མཚོ་འོག་དམག་གྲུར་སྦར་གས་བྱུང་བ། པ་ཀི་སི་ཏན་གྱིས་རྒྱ་གར་ལ་མི་སེར་བསད་པའི་སྐྱོན་འཛུགས་བྱས་པ། རྩོམ་ཡིག་མང་བ། འབྲེལ་མཐུད་བརྒྱུད་ལམ། ཐོན་སྐྱེད་དང་སྲི་ཞུ། ་ཐོག་དེབ་བཞི་ དཔར་འགྲེམས་གནང་ཡོད་པ་དང་བོད་ཡིག་དྲ་ཚིགས་ཁག་ནང་ལ་ཡང་རྩོམ་ཡང་ཡང་བྲིས་གནང་མཁན་རེད། ལེ་ཚན་ཁག ལེ་ཚན་ཁག འབྲེལ་ཡོད། འགྲེམ་སྟོན། རྒྱུད་ལམ་སྣ་མང་ཡིག་མཛོད། བཀོལ་སྤྱོད་པའི་འཇོག་ཡུལ་དྲ་ངོས། སྔོན་མ། རྗེས་མ། བསྟན་འཛིན་བདེ་སྐྱིད། ཚེ་རིང་རྣམ་རྒྱལ། བསྟན་འཛིན་ངག་དབང་། ཡོལ་གདོང་ཚེ་རིང་ལྷག་པ།  ་དབང་ ཕྱུག་གཉིས་ཀྱིས་བརྗོད་གཞི་བྱེ་བྲག་པ་ཞིག་ལ་བགྲོ་གླེང་གཏིང་ཟབ་བྱེད་པའི་གཟའ་ འཁོར་གཉིས་རེའི་མཚམས་ཀྱི་ལེ་ཚན་ཞིག་ཡིན། དཔྱད་ཞིབ་ཀྱིས་རྒྱ་ནག་ནང་ཁུལ་གྱི་འགྱུར་ལྡོག་དང༌། རྒྱ་ནག་དང་རྒྱལ་སྤྱིའི་འབྲེལ་བར་དམིགས་སུ་བཀར་ནས་བགྲོ་གླེང་བྱེད་ཀྱི་ཡོད།། རྒྱང་སྲིང་དུས་ཚོད།";

const kTeststr_br_Latn =
  " a chom met leuskel a ra e blas da jack irons dilabour hag aet kuit eus what is this dibab a reont da c houde michael beinhorn evit produiñ an trede pladenn kavet e vez ar ganaouennoù buhan ha buhan ganto setu stummet ar bladenn adkavet e vez enni funk";
const kTeststr_bs_Cyrl =
  "историја босне књ историја босне књ историја босне књ историја босне књ ";
// From 10% testing part of new lang=bs scrape
const kTeststr_bs_Latn =
  "Novi predsjednik Mešihata Islamske zajednice u Srbiji (IZuS) i muftija dr. Mevlud ef. Dudić izjavio je u intervjuu za Anadolu Agency (AA) kako je uvjeren da će doći do vraćanja jedinstva među muslimanima i unutar Islamske zajednice na prostoru Sandžaka, te da je njegova ruka pružena za povratak svih u okrilje Islamske zajednice u Srbiji nakon skoro sedam godina podjela u tom dijelu Srbije. Dudić je za predsjednika Mešihata IZ u Srbiji izabran 4. januara, a zvanična inauguracija će biti obavljena u prvoj polovini februara. Kako se očekuje, prisustvovat će joj i reisu-l-ulema Islamske zajednice u Srbiji Husein ef. Kavazović koji će i zvanično promovirati Dudića u novog prvog čovjeka IZ u Srbiji. Dudić će danas boraviti u prvoj zvaničnoj posjeti reisu Kavazoviću, što je njegov privi simbolični potez nakon imenovanja. ";

const kTeststr_ca_Latn =
  "al final en un únic lloc nhorabona l correu electrònic està concebut com a eina de productivitat aleshores per què perdre el temps arxivant missatges per després intentar recordar on els veu desar i per què heu d eliminar missatges importants per l";
const kTeststr_ceb_Latn =
  "Ang Sugbo usa sa mga labing ugmad nga lalawigan sa nasod. Kini ang sentro sa komersyo, edukasyon ug industriya sa sentral ug habagatang dapit sa kapupod-an. Ang mipadayag sa Sugbo isip ikapito nga labing nindot nga pulo sa , ang nag-inusarang pulo sa Pilipinas nga napasidunggan sa maong magasin sukad pa sa tuig";
const kTeststr_ceb_Latn2 =
  "Ang mga komyun sa Pransiya duol-duol sa inkorporadong mga lungsod ug mga dakbayan sa Estados Unidos. Wala kini susamang istruktura sa Hiniusang Gingharian (UK) tungod kay ang estado niini taliwala sa di-metropolitan nga distrito ug sa sibil nga parokya. Wala usab kini susamang istruktura sa Pilipinas.";
const kTeststr_chr_Cher = "ᎠᎢᏍᎩ ᎠᏟᎶᏍᏗ ᏥᏄᏍᏛᎩ ᎦᎫᏍᏛᏅᎯ ᎾᎥᎢ";
const kTeststr_co_Latn =
  " a prupusitu di risultati for utilizà a scatula per ricercà ind issi risultati servore errore u servore ha incuntratu una errore pruvisoria é ùn ha pussutu compie a vostra dumanda per piacè acimenta dinò ind una minuta tuttu listessu ligami truvà i";
const kTeststr_crs_Latn =
  "Sesel ou menm nou sel patri. Kot nou viv dan larmoni. Lazwa, lanmour ek lape. Nou remersye Bondye. Preserv labote nou pei. Larises nou losean. En leritaz byen presye. Pour boner nou zanfan. Reste touzour dan linite. Fer monte nou paviyon. Ansanm pou tou leternite. Koste Seselwa!";
const kTeststr_cs_Latn =
  " a akci opakujte film uložen vykreslit gmail tokio smazat obsah adresáře nelze načíst systémový profil jednotky smoot okud používáte pro určení polokoule značky z západ nebo v východ používejte nezáporné hodnoty zeměpisné délky nelze";
const kTeststr_cy_Latn =
  " a chofrestru eich cyfrif ymwelwch a unwaith i chi greu eich cyfrif mi fydd yn cael ei hysbysu o ch cyfeiriad ebost newydd fel eich bod yn gallu cadw mewn cysylltiad drwy gmail os nad ydych chi wedi clywed yn barod am gmail mae n gwasanaeth gwebost";
const kTeststr_da_Latn =
  " a z tallene og punktummer der er tilladte log ud angiv den ønskede adgangskode igen november gem personlige oplysninger kontrolspørgsmål det sidste tegn i dit brugernavn skal være et bogstav a z eller tal skriv de tegn du kan se i billedet nedenfor";
const kTeststr_de_Latn =
  " abschnitt ordner aktivieren werden die ordnereinstellungen im farbabschnitt deaktiviert öchten sie wirklich fortfahren eldtypen angeben optional n diesem schritt geben sie für jedesfeld aus dem datenset den typ an ieser schritt ist optional eldtypen";
const kTeststr_dv_Thaa =
  " ހިންދީ ބަހުން ވާހަކަ ދައްކާއިރު ދެވަނަ ބަހެއްގެ ގޮތުގައާއި އެނޫން ގޮތްގޮތުން ހިންދީ ބަހުން ވާހަކަ ދައްކާ މީހުންގެ އަދަދު މިލިއަނަށް";
const kTeststr_dz_Tibt =
  " རྩིས བརྐྱབ ཚུལ ལྡན དང ངེས བདེན སྦ སྟོན ནིའི དོན ལུ ཁྱོད གུག ཤད ལག ལེན འཐབ དགོ ག དང ཨིན པུཊི གྲལ ཐིག གུ";
const kTeststr_ee_Latn =
  "Yi (Di tanya sia) tatia akɔ wò ayi axa yeye dzi kple tanya si sɔ kple esi wòŋlɔ ɖe goa me, negbe axaa ɖe li kpakple tanya mawo xoxo ko. Teƒe le axa yeye sia dzi si wòateŋu atia na kpekpeɖeŋu kple nuwoŋlɔŋlɔ ne anɔ hahiãm na wò. Mehiã be na gbugbɔ ava afii na axa yeye gɔmedzedze o. Woateŋu adze wo gɔme kple nuŋɔŋlɔ dzẽwo tatia. Megavɔ̃ na nuyeyewo gɔmedzedze kroa o.";
const kTeststr_el_Grek =
  " ή αρνητική αναζήτηση λέξης κλειδιού καταστήστε τις μεμονωμένες λέξεις κλειδιά περισσότερο στοχοθετημένες με τη μετατροπή τους σε";
const kTeststr_en_Latn =
  " a backup credit card by visiting your billing preferences page or visit the adwords help centre for more details https adwords google com support bin answer py answer hl en we were unable to process the payment of for your outstanding google adwords";
const kTeststr_eo_Latn =
  " a jarcento refoje per enmetado de koncerna pastro tiam de reformita konfesio ekde refoje ekzistis luteranaj komunumanoj tamen tiuj fondis propran komunumon nur en ambaŭ apartenis ekde al la evangela eklezio en prusio resp ties rejnlanda provinceklezio en";
const kTeststr_es_Latn =
  " a continuación haz clic en el botón obtener ruta también puedes desplazarte hasta el final de la página para cambiar tus opciones de búsqueda gráfico y detalles ésta es una lista de los vídeos que te recomendamos nuestras recomendaciones se basan";
const kTeststr_et_Latn =
  " a niipea kui sinu maksimaalne igakuine krediidi limiit on meie poolt heaks kiidetud on sinu kohustuseks see krediidilimiit";
const kTeststr_eu_Latn =
  " a den eraso bat honen kontra hortaz eragiketa bakarrik behar dituen eraso batek aes apurtuko luke nahiz eta oraingoz eraso bideraezina izan gaur egungo teknologiaren mugak direla eta oraingoz kezka hauek alde batera utzi daitezke orain arteko indar";
const kTeststr_fa_Arab =
  " آب خوردن عجله می کردند به جای باز ی کتک کاری می کردند و همه چيز مثل قبل بود فقط من ماندم و يک دنيا حرف و انتظار تا عاقبت رسيد احضاريه ی ای با";
const kTeststr_fi_Latn =
  " a joilla olet käynyt tämä kerro meille kuka ä olet ei tunnistettavia käyttötietoja kuten virheraportteja käytetään google desktopin parantamiseen etsi näyttää mukautettuja uutisia google desktop keskivaihto leikkaa voit kaksoisnapsauttaa";
const kTeststr_fj_Latn =
  " i kina na i iri ka duatani na matana main a meke wesi se meke mada na meke ni yaqona oqo na meke ka dau vakayagataki ena yaqona vakaturaga e dau caka toka ga kina na vucu ka dau lagati tiko kina na ka e yaco tiko na talo ni wai ni yaqona na lewai ni wai";
const kTeststr_fo_Latn =
  " at verða átaluverdar óhóskandi ella áloypandi vit kunnu ikki garanterða at google leitanin ikki finnur naka sum er áloypandi óhóskandi ella átaluvert og google tekur onga ábyrgd yvir tær síður sum koma við í okkara leitiskipan fá tær ein";
const kTeststr_fr_Latn =
  " a accès aux collections et aux frontaux qui lui ont été attribués il peut consulter et modifier ses collections et exporter des configurations de collection toutefois il ne peut pas créer ni supprimer des collections enfin il a accès aux fonctions";
const kTeststr_fy_Latn =
  " adfertinsjes gewoan lytse adfertinsjes mei besibbe siden dy t fan belang binne foar de ynhâld fan jo berjochten wolle jo mear witte fan gmail foardat jo jo oanmelde gean dan nei wy wurkje eltse dei om gmail te ferbetterjen dêrta sille wy jo sa út en";
const kTeststr_ga_Latn =
  " a bhfuil na focail go léir i do cheist le fáil orthu ní gá ach focail breise a chur leis na cinn a cuardaíodh cheana chun an cuardach a bheachtú nó a chúngú má chuirtear focal breise isteach aimseofar fo aicme ar leith de na torthaí a fuarthas";
const kTeststr_gaa_Latn =
  "Akε mlawookpeehe kε Maŋhiεnyiεlכ oshikifככ lε eba naagbee ni maŋ lε nitsumכ ni kwεכ oshikifככ nכ lε etsככ mכ ni ye kunim ni akε lε eta esεŋ nכ. Dani nomεi baaba nכ lε, maŋ nכkwεmכ kui wuji enyכ ni yככ wכ maŋ lε mli, NPP kε NDC mli bii fכfכi wiemכi kεmaje majee amεhe. Ekomεi kwraa po yafee hiεkwεmכi ni ha ni gidigidi, pilamכ kε la shishwiemכ aaba yε heikomεi. ";
const kTeststr_gd_Latn =
  " air son is gum bi casg air a h uile briosgaid no gum faigh thu brath nuair a tha briosgaid a tighinn gad rannsachadh ghoogle gu ceart mura bheil briosgaidean ceadaichte cuiridh google briosgaid dha do neach cleachdaidh fa leth tha google a cleachdadh";
const kTeststr_gl_Latn =
  "  debe ser como mínimo taranto tendas de venda polo miúdo cociñas servizos bordado canadá viaxes parques de vehículos de recreo hotel oriental habitación recibir unha postal no enderezo indicado anteriormente";
const kTeststr_gn_Latn =
  " aháta añe ë ne mbo ehára ndive ajeruréta chupe oporandujey haĝua peëme mba épa pekaru ha áĝa oporandúvo nde eréta avei re paraguaýpe kachíke he i leúpe ndépa re úma kure tatakuápe ha leu ombohovái héë ha ujepéma kachíke he ijey";
const kTeststr_gu_Gujr =
  " આના પરિણામ પ્રમાણસર ફોન્ટ અવતરણ ચિન્હવાળા પાઠને છુપાવો બધા સમૂહો શોધાયા હાલનો જ સંદેશ વિષયની";
const kTeststr_gv_Latn =
  " and not ripe as i thought yn assyl yn shynnagh as yn lion the ass the fox and the lion va assyl as shynnagh ayns commee son nyn vendeilys as sauchys hie ad magh ayns y cheyll dy shelg cha row ad er gholl feer foddey tra veeit ad rish lion yn shynnagh";
const kTeststr_ha_Latn =
  " a cikin a kan sakamako daga sakwannin a kan sakamako daga sakwannin daga ranar zuwa a kan sakamako daga guda daga ranar zuwa a kan sakamako daga shafukan daga ranar zuwa a kan sakamako daga guda a cikin last hour a kan sakamako daga guda daga kafar";
const kTeststr_haw_Latn =
  "He puke noiʻi kūʻikena kūnoa ʻo Wikipikia. E ʻoluʻolu nō, e hāʻawi mai i kāu ʻike, kāu manaʻo, a me kou leo no ke kūkulu ʻana a me ke kākoʻo ʻana mai i ka Wikipikia Hawaiʻi. He kahua pūnaewele Hawaiʻi kēia no ka hoʻoulu ʻana i ka ʻike Hawaiʻi. Inā hiki iā ʻoe ke ʻōlelo Hawaiʻi, e ʻoluʻolu nō, e kōkua mai a e hoʻololi i nā ʻatikala ma ʻaneʻi, a pono e haʻi aku i kou mau hoa aloha e pili ana i ka Wikipikia Hawaiʻi. E ola mau nō ka ʻōlelo Hawaiʻi a mau loa aku.";
const kTeststr_hi_Deva =
  " ं ऐडवर्ड्स विज्ञापनों के अनुभव पर आधारित हैं और इनकी मदद से आपको अपने विज्ञापनों का अधिकतम लाभ";
const kTeststr_hr_Latn =
  "Posljednja dva vladara su Kijaksar (Κυαξαρης; 625-585 prije Krista), fraortov sin koji će proširiti teritorij Medije i Astijag. Kijaksar je imao kćer ili unuku koja se zvala Amitis a postala je ženom Nabukodonosora II. kojoj je ovaj izgradio Viseće vrtove Babilona. Kijaksar je modernizirao svoju vojsku i uništio Ninivu 612. prije Krista. Naslijedio ga je njegov sin, posljednji medijski kralj, Astijag, kojega je detronizirao (srušio sa vlasti) njegov unuk Kir Veliki. Zemljom su zavladali Perzijanci.";
const kTeststr_ht_Latn =
  " ak pitit tout sosyete a chita se pou sa leta dwe pwoteje yo nimewo leta fèt pou li pwoteje tout paran ak pitit nan peyi a menm jan kit paran yo marye kit yo pa marye tout manman ki fè pitit leta fèt pou ba yo konkoul menm jan tou pou timoun piti ak pou";
const kTeststr_hu_Latn =
  " a felhasználóim a google azonosító szöveget ikor látják a felhasználóim a google azonosító szöveget felhasználók a google azonosító szöveget fogják látni minden tranzakció után ha a vásárlását regisztrációját oldalunk";
const kTeststr_hy_Armn =
  " ա յ եվ նա հիացած աչքերով նայում է հինգհարկանի շենքի տարօրինակ փոքրիկ քառակուսի պատուհաններին դեռ մենք շատ ենք հետամնաց ասում է նա այսպես է";
const kTeststr_ia_Latn =
  " super le sitos que tu visita isto es necessari pro render disponibile alcun functionalitates del barra de utensiles a fin que nos pote monstrar informationes ulterior super un sito le barra de utensiles debe dicer a nos le";
// From 10% testing part of new lang=id scrape
const kTeststr_id_Latn =
  "berdiri setelah pengurusnya yang berusia 83 tahun, Fayzrahman Satarov, mendeklarasikan diri sebagai nabi dan rumahnya sebagai negara Islam Satarov digambarkan sebagai mantan ulama Islam  tahun 1970-an. Pengikutnya didorong membaca manuskripnya dan kebanyakan dilarang meninggalkan tempat persembunyian bawah tanah di dasar gedung delapan lantai mereka. Jaksa membuka penyelidikan kasus kriminal pada kelompok itu dan menyatakan akan membubarkan kelompok kalau tetap melakukan kegiatan ilegal seperti mencegah anggotanya mencari bantuan medis atau pendidikan. Sampai sekarang pihak berwajib belum melakukan penangkapan meskipun polisi mencurigai adanya tindak kekerasan pada anak. Pengadilan selanjutnya akan memutuskan apakah anak-anak diizinkan tetap tinggal dengan orang tua mereka. Kazan yang berada sekitar 800 kilometer di timur Moskow merupakan wilayah Tatarstan yang";

const kTeststr_ie_Latn =
  " abhorre exceptiones in li derivation plu cardinal por un l i es li regularità del flexion conjugation ples comparar latino sine flexione e li antiqui projectes naturalistic queles have quasi null regules de derivation ma si on nu examina li enunciationes";
const kTeststr_ig_Latn =
  "Chineke bụ aha ọzọ ndï omenala Igbo kpọro Chukwu. Mgbe ndị bekee bịara, ha mee ya nke ndi Christian. N'echiche ndi ekpere chi Omenala Ndi Igbo, Christianity, Judaism, ma Islam, Chineke nwere ọtụtụ utu aha, ma nwee nanị otu aha. Ụzọ abụọ e si akpọ aha ahụ bụ Jehovah ma Ọ bụ Yahweh. Na ọtụtụ Akwụkwọ Nsọ, e wepụla aha Chineke ma jiri utu aha bụ Onyenwe Anyị ma ọ bụ Chineke dochie ya. Ma mgbe e dere akwụkwọ nsọ, aha ahụ bụ Jehova pụtara n’ime ya, ihe dị ka ugboro pụkụ asaa(7,000).";
// From 10% testing part of new lang=ik scrape
const kTeststr_ik_Latn =
  "sabvaqjuktuq sabvaba atiqaqpa atiqaqpa ibiq iebiq ixafich niuqtulgiññatif uvani natural gas tatpikka ufasiksigiruaq maaffa savaannafarufa mi tatkivani navy qanuqjugugguuq taaptuma inna uqsrunik ivaqjiqhutik       taktuk allualiuqtuq sigukun nanuq puuvraatuq taktuum amugaa kalumnitigun nanuq agliruq allualiuqtuq";

const kTeststr_is_Latn =
  " a afköst leitarorða þinna leitarorð neikvæð leitarorð auglýsingahópa byggja upp aðallista yfir ný leitarorð fyrir auglýsingahópana og skoða ítarleg gögn um árangur leitarorða eins og samkeppni auglýsenda og leitarmagn er krafist notkun";
const kTeststr_it_Latn =
  " a causa di un intervento di manutenzione del sistema fino alle ore circa ora legale costa del pacifico del novembre le campagne esistenti continueranno a essere pubblicate come di consueto anche durante questo breve periodo di inattività ci scusiamo per";
const kTeststr_iu_Cans =
  "ᐃᑯᒪᒻᒪᑦ ᕿᓈᖏᓐᓇᓲᖑᒻᒪᑦ ᑎᑎᖅᑕᓕᒫᖅᓃᕕᑦ ᑎᑦᕆᐊᑐᓐᖏᑦᑕᑎᑦ ᑎᑎᖅᑕᑉᐱᑦ ᓯᕗᓂᖓᓂ ᑎᑎᖅᖃᖅ ᑎᑎᕆᐊᑐᓐᖏᑕᐃᑦ ᕿᓂᓲᖑᔪᒍᑦ ᑎᑎᖅᑕᓕᒫᖅᓃᕕᑦ";
const kTeststr_he_Hebr =
  " או לערוך את העדפות ההפצה אנא עקוב אחרי השלבים הבאים כנס לחשבון האישי שלך ב";
const kTeststr_ja_Hani =
  " このペ ジでは アカウントに指定された予算の履歴を一覧にしています それぞれの項目には 予算額と特定期間のステ タスが表示されます 現在または今後の予算を設定するには";
const kTeststr_jw_Latn =
  " account ten server niki kalian username meniko tanpo judul cacahe account nggonanmu wes pol pesen mu wes diguwak pesenan mu wes di simpen sante wae pesenan mu wes ke kirim mbuh tekan ora pesenan e ke kethok pesenan mu wes ke kirim mbuh tekan ora pesenan";
const kTeststr_ka_Geor =
  " ა ბირთვიდან მიღებული ელემენტი მენდელეევის პერიოდულ სიტემაში გადაინაცვლებს ორი უჯრით";
const kTeststr_kha_Latn =
  " kaba jem jai sa sngap thuh ia ki bynta ba sharum naka sohbuin jong phi nangta sa pynhiar ia ka kti kadiang jong phi sha ka krung jong phi bad da kaba pyndonkam kumjuh ia ki shympriahti jong phi sa sngap thuh shapoh ka tohtit jong phi pyndonkam ia kajuh ka";
const kTeststr_kk_Arab =
  " ﺎ ﻗﻴﺎﻧﺎﺕ ﺑﻮﻟﻤﺎﻳﺪﻯ ﺑﯘﻝ ﭘﺮﻭﺗﺴﻪﺳﯩﻦ ﻳﺎﻋﻨﻲ ﻗﺎﻻ ﻭﻣﯩﺮﯨﻨﺪﻩ ﻗﺎﺯﺍﻕ ء ﺗﯩﻠﯩﻨﯩﯔ ﻗﻮﻟﺪﺍﻧﯩﻠﻤﺎﯞﻯ ﻗﺎﺯﺍﻕ ﺟﻪﺭﯨﻨﺪﻩ";
const kTeststr_kk_Cyrl =
  " а билердің өзіне рұқсат берілмеген егер халық талап етсе ғана хан келісім берген өздеріңіз білесіздер қр қыл мыс тық кодексінде жазаның";
const kTeststr_kk_Latn =
  " bolsa da otanyna qaityp keledi al oralmandar basqa elderde diasporasy ote az bolghandyqtan bir birine komektesip bauyrmal bolady birde men poezben oralmandardyng qazaqstangha keluin kordim monghol qazaqtary poezdan tuse sala jerdi suip jylap keletin biraq";
const kTeststr_kl_Latn =
  " at nittartakkalli uani toqqarsimasatta akornanni nittartakkanut allanut ingerlaqqittoqarsinnaavoq kanukoka tassaavoq kommuneqarfiit kattuffiat nuna tamakkerlugu kommunit nittartagaannut ingerlaqqiffiusinnaasoq kisitsiserpassuit nunatsinnut tunngasut";
const kTeststr_km_Khmr =
  " ក ខ គ ឃ ង ច ឆ ជ ឈ ញ ដ ឋ ឌ ឍ ណ ត ថ ទ ធ ន ប ផ ព ភ ម យ រ ល វ ស ហ ឡ អ ឥ ឦ ឧ ឪ ឫ ឬ ឯ ឱ ទាំងអស់";
const kTeststr_kn_Knda =
  " ಂಠಯ್ಯನವರು ತುಮಕೂರು ಜಿಲ್ಲೆಯ ಚಿಕ್ಕನಾಯಕನಹಳ್ಳಿ ತಾಲ್ಲೂಕಿನ ತೀರ್ಥಪುರ ವೆಂಬ ಸಾಧಾರಣ ಹಳ್ಳಿಯ ಶ್ಯಾನುಭೋಗರ";
const kTeststr_ko_Hani =
  " 개별적으로 리포트 액세스 권한을 부여할 수 있습니다 액세스 권한 부여사용자에게 프로필 리포트에 액세스할 수 있는 권한을 부여하시려면 가용 프로필 상자에서 프로필 이름을 선택한 다음";
// From 10% testing part of new lang=ks scrape
const kTeststr_ks_Arab =
  " ژماں سرابن منز  گرٲن چھِہ خابٕک کھلونہٕ ؤڈراواں   تُلتِھ نِیَس تہٕ گوشہِ گوشہِ مندچھاوى۪س   دِلس چھُہ وون٘ت وُچھان از ستم قلم  صبوٝرٕ وول مسٲفر لیۆکھُن بێتابن منز   ورل سوال چھُہ تراواں جوابن منز    کالہٕ پھۯستہٕ پھن٘ب پگَہہ پہ   پۆت نظر دِژ نہٕ ژھالہٕ مٔت آرن     مٲنز مسول متھان چھےٚ مس والن  وۅن چھےٚ غارن تہِ نارٕ ژھٹھ ژاپان  رێش تۅرگ تراوٕہن تہٕ ون رٹہٕ ہن  ہوشہِ ہێۆچھ نہٕ پوشنوٝلس نِش  مۅہرٕ دی دی زٕلاں چھِ زى۪و حرفن  لۆدرٕ پھٔل ہى۪تھ ملر عازمؔ  سۆدرٕ کھۅنہِ منز منگاں چھُہ ندرى۪ن پن   ژے تھى۪کی یہِ مسٲفر پنن وُڈو تہٕ پڑاو   گٕتَو گٕتَو چھےٚ یہِ کۅل بُتھ تہٕ بانہٕ سٕہہ گۅردٕ چھہِ سپداں دمہٕ پُھٹ  چھِٹہ پونپر پکھہٕ داران سُہ یتى۪ن تۯاوِ  کم نظر دۯاکھ تہٕ باسیوے سُہ مۆہ ہیو یێران  مےٚ ژى۪تُرمُت چھُہ سُلی تس چھےٚ کتى۪ن تھپھ  شاد مس کراں وُچھ مےٚ خون  ژٕ خبر کیازِ کراں دۯاکھ تمِس پى۪ٹھ ماتم  أز کہِ شبہٕ آو مےٚ بێیہِ پیش سفر زانہِ خدا  دارِ پى۪ٹھ ژٲنگ ہنا تھو زِ ژے چھےٚ مێون  أنہٕ کپٹاں چھُہ زٕژن سون مظفّر عازمؔ  پوشہ برگن چھُہ سُواں چاکھ سُہ الماس قلم   لوِ کٔ ڈ نوِ سرٕ سونتس کل   پروِ بۆر بێیہ از بانبرِ ہۆت  یمبرزلہِ ٹارى۪ن منز نار   وزملہِ کۅسہٕ کتھ کٔر اظہار  کچھہِ منزٕ ؤن رووُم اچھہِ  چشمو ژوپُم کٔنڈ انبار   تماشہِ چھہِ تگاں";

const kTeststr_ks_Deva =
  "नमस्ते शारदे देवि काश्मिरपुर्वासिनि त्वामहम प्रार्थये देवि विद्य दानम च देहि मे कॉशुर लेख॒नुक सारिव॒य खॊत॒ आसान तरीक॒ छु यि देवनागरी टाइपराइटर इस्तिमाल करुन. अथ मंज़ छि कॉशुर लेख॒न॒चि सारॆय मात्रायि. अमि अलाव॒ हॆकिव तॊह्य् यिम॒ यूनिकोड एडिटर ति वरतॉविथ मगर कॉशिरि मात्रायि लेख॒नस गछ़ि हना दिकथ: अक्षरमालाछु अख मुफ़्त त॒ सॅहॅल सोफ्टवेर यॆमि स॒त्य् युनिकोड देवनागरी मंज़ ITRANS scheme स॒त्य् छु यिवान लेख॒न॒. वुछिव: सहायता. अथ स॒त्य् जुडिथ जालपृष्ठ (वेबपेज) (सॉरी अँग्रीज़ी पॉठ्य)";
const kTeststr_ku_Arab =
  " بۆ به ڕێوه بردنی نامه ی که دێتن ڕاسته وخۆ ڕه وان بکه نامه کانی گ مایل بۆ حسابی پۆستێکی تر هێنانی په یوه ندکاره کان له";
const kTeststr_ku_Latn =
  " be zmaneki ter le inglis werdegeretewe em srvise heshta le cor beta daye wate hest a taqi dekrete u bashtr dekret tewawwzmanekan wernegrawnetewe u ne hemu laperakn ke eme pshtiwan dekayn be teaweti wergerawete nermwalley wergeran teksti new wene nasnatewe";
const kTeststr_ky_Arab =
  " جانا انى تانۇۇ ۇلۇتۇن تانۇۇ قىرعىزدى بئلۉۉ دەگەندىك اچىق ايتساق ماناستى تاانىعاندىق ۅزۉڭدۉ تاانىعاندىق بۉگۉن تەما جۉكتۅمۅ ق ى رع ى ز ت ى ل ى";
const kTeststr_ky_Cyrl =
  " агай эле оболу мен садыбакас аганын өзү менен эмес эмгектери менен тааныштым жылдары ташкенде өзбекстан илимдер академиясынын баяны";
const kTeststr_la_Latn =
  " a deo qui enim nocendi causa mentiri solet si iam consulendi causa mentiatur multum profecit sed aliud est quod per se ipsum laudabile proponitur aliud quod in deterioris comparatione praeponitur aliter enim gratulamur cum sanus est homo aliter cum melius";
const kTeststr_lb_Latn =
  " a gewerkschaften och hei gefuerdert dir dammen an dir häre vun de gewerkschaften denkt un déi aarm wann der äer fuerderunge formuléiert d sechst congés woch an aarbechtszäitverkierzung hëllefen hinnen net d unhiewe vun de steigerungssäz bei de";
const kTeststr_lg_Latn =
  " abaana ba bani lukaaga mu ana mu babiri abaana ba bebayi lukaaga mu abiri mu basatu abaana ba azugaadi lukumi mu ebikumi bibiri mu abiri mu babiri abaana ba adonikamu lukaaga mu nltaaga mu mukaaga abaana ba biguvaayi enkumi bbiri mu ataano mu mukaaga";
const kTeststr_lif_Limb =
  "ᤁᤡᤖᤠᤳ ᤕᤠᤰᤌᤢᤱ ᤆᤢᤶᤗᤢᤱᤖᤧ ᤛᤥᤎᤢᤱᤃᤧᤴ ᤀᤡᤔᤠᤴᤛᤡᤱ ᤆᤧᤶᤈᤱᤗᤧ ᤁᤢᤔᤡᤱᤅᤥ ᤏᤠᤈᤡᤖᤡ ᤋᤱᤒᤣ ᥈᥆᥆᥉ ᤒᤠ ᤈᤏᤘᤖᤡ ᤗᤠᤏᤢᤀᤠᤱ ᤁ᤹ᤏᤠ ᤋᤱᤒᤣ ᤁᤠᤰ ᤏᤠ᤺ᤳᤋᤢ ᤕᤢᤖᤢᤒᤠ ᤀᤡᤔᤠᤴᤛᤡᤱ ᤋᤱᤃᤡᤵᤛᤡᤱ ᤌᤡᤶᤒᤣᤴ ᤂᤠᤃᤴ ᤛᤡᤛᤣ᤺ᤰᤗᤠ ᥇᥍ ᤂᤧᤴ ᤀᤡᤛᤡᤰ ᥇ ᤈᤏᤘᤖᤡ ᥈᥆᥆᥊ ᤀᤥ ᤏᤠᤛᤢᤵ ᤆᤥ᤺ᤰᤔᤠ ᤌᤡᤶᤒᤣ ᤋᤱᤃᤠᤶᤛᤡᤱᤗ ᤐᤳᤐᤠ ᤀᤡᤱᤄᤱ ᤘᤠ᤹";
const kTeststr_ln_Latn =
  " abakisamaki ndenge esengeli moyebami abongisamaki solo mpenza kombo ya moyebami elonguamaki kombo ya bayebami elonguamaki kombo eleki molayi po na esika epesameli limbisa esika ya kotia ba kombo esuki boye esengeli olimbola ndako na yo ya mikanda kombo";
const kTeststr_lo_Laoo =
  " ກຫາທົ່ວທັງເວັບ ແລະໃນເວັບໄຮ້ສາຍ ທຳອິດໃຫ້ທຳການຊອກຫາກ່ອນ ຈາກນັ້ນ ໃຫ້ກົດປຸ່ມເມນູ ໃນໜ້າຜົນໄດ້";
const kTeststr_lt_Latn =
  " a išsijungia mano idėja dėl geriausio laiko po pastarųjų savo santykių pasimokiau penki dalykai be kurių negaliu gyventi mano miegamajame tu surasi ideali pora išsilavinimas aukštoji mokykla koledžas universitetas pagrindinis laipsnis metai";
const kTeststr_lv_Latn =
  " a gadskārtējā izpārdošana slēpošana jāņi atlaide izmaiņas trafikā kas saistītas ar sezonas izpārdošanu speciālajām atlaidēm u c ir parastas un atslēgvārdi kas ir populāri noteiktos laika posmos šajā laikā saņems lielāku klikšķu";
const kTeststr_mfe_Latn =
  "Anz dir mwa, Sa bann delo ki to trouve la, kot fam prostitie asize, samem bann pep, bann lafoul dimoun, bann nasion ek bann langaz. Sa dis korn ki to finn trouve, ansam avek bebet la, zot pou ena laenn pou prostitie la; zot pou pran tou seki li ena e met li touni, zot pou manz so laser e bril seki reste dan dife. Parski Bondie finn met dan zot leker proze pou realiz so plan. Zot pou met zot dakor pou sed zot pouvwar bebet la ziska ki parol Bondie fini realize.";
const kTeststr_mg_Latn =
  " amporisihin i ianao mba hijery ny dika teksta ranofotsiny an ity lahatsoratra ity tsy ilaina ny opérateur efa karohina daholo ny teny rehetra nosoratanao ampiasao anaovana dokambarotra i google telugu datin ny takelaka fikarohana sary renitakelak i";
const kTeststr_mi_Latn =
  " haere ki te kainga o o haere ki te kainga o o haere ki te kainga o te rapunga ahua o haere ki te kainga o ka tangohia he ki to rapunga kaore au mohio te tikanga whakatiki o te ra he whakaharuru te pai rapunga a te rapunga ahua a e kainga o nga awhina o te";
const kTeststr_mk_Cyrl =
  " гласовите коалицијата на вмро дпмне како партија со најмногу освоени гласови ќе добие евра а на сметката на коализијата за македонија";
const kTeststr_ml_Mlym =
  " ം അങ്ങനെ ഞങ്ങള് അവരുടെ മുമ്പില് നിന്നു ഔടും ഉടനെ നിങ്ങള് പതിയിരിപ്പില് നിന്നു എഴുന്നേറ്റു";
const kTeststr_mn_Cyrl =
  " а боловсронгуй болгох орон нутгийн ажил үйлсийг уялдуулж зохицуулах дүрэм журам боловсруулах орон нутгийн өмч хөрөнгө санхүүгийн";
const kTeststr_mn_Mong = "ᠦᠭᠡ ᠵᠢᠨ ᠴᠢᠨᠭ᠎ᠠ ᠬᠦᠨᠳᠡᠢ ᠵᠢ ᠢᠯᠭᠠᠬᠣ";
const kTeststr_mr_Deva =
  "हैदराबाद  उच्चार ऐका (सहाय्य·माहिती)तेलुगू: హైదరాబాదు , उर्दू: حیدر آباد हे भारतातील आंध्र प्रदेश राज्याच्या राजधानीचे शहर आहे. हैदराबादची लोकसंख्या ७७ लाख ४० हजार ३३४ आहे. मोत्यांचे शहर अशी एकेकाळी ओळख असलेल्या या शहराला ऐतिहासिक, सांस्कृतिक आणि स्थापत्यशास्त्रीय वारसा लाभला आहे. १९९० नंतर शिक्षण आणि माहिती तंत्रज्ञान त्याचप्रमाणे औषधनिर्मिती आणि जैवतंत्रज्ञान क्षेत्रातील उद्योगधंद्यांची वाढ शहरात झाली. दक्षिण मध्य भारतातील पर्यटन आणि तेलुगू चित्रपटनिर्मितीचे हैदराबाद हे केंद्र आहे";
// From 10% testing part of new lang=ms scrape
const kTeststr_ms_Latn =
  "pengampunan beramai-ramai supaya mereka pulang ke rumah masing-masing. Orang-orang besarnya enggan mengiktiraf sultan yang dilantik oleh Belanda sebagai Yang DiPertuan Selangor. Orang ramai pula tidak mahu menjalankan perniagaan bijih timah dengan Belanda, selagi raja yang berhak tidak ditabalkan. Perdagang yang lain dibekukan terus kerana untuk membalas jasa beliau yang membantu Belanda menentang Riau, Johor dan Selangor. Di antara tiga orang Sultan juga dipandang oleh rakyat sebagai seorang sultan yang paling gigih. 1 | 2 SULTAN Sebagai ganti Sultan Ibrahim ditabalkan Raja Muhammad iaitu Raja Muda. Walaupun baginda bukan anak isteri pertama bergelar Sultan Muhammad bersemayam di Kuala Selangor juga. Pentadbiran baginda yang lemah itu menyebabkan Kuala Selangor menjadi sarang ioleh Cina di Lukut tidak diambil tindakan, sedangkan baginda sendiri banyak berhutang kepada 1";

const kTeststr_ms_Latn2 =
  "bilik sebelah berkata julai pada pm ladymariah hmm sume ni terpulang kepada individu mungkin anda bernasib baik selama ini dalam membeli hp yang bagus deli berkata julai pada pm walaupun bukan bahsa baku tp tetap bahasa melayu kan perubahan boleh dibuat";
const kTeststr_mt_Latn =
  " ata ikteb messaġġ lil indirizzi differenti billi tagħżilhom u tagħfas il buttuna ikteb żid numri tfittxijja tal kotba mur print home kotba minn pagni ghal pagna minn ghall ktieb ta aċċessa stieden habib iehor grazzi it tim tal gruppi google";
const kTeststr_my_Latn =
  " jyk ef oif gawgodcsifayvdrfhrnf bmawgrsm topf dsvj g mail tamumif avhvm atmif txjwgif yxrqhk avhvm efae m pwifavhvm ef ufkyfwdky help center odkyvmyg drsm ar avh dswjhar cgef rsm udkawdkifygw f tajzawgudk smedkifygw f jyd awmh g mail cool features rsm";
const kTeststr_my_Mymr =
  " တက္ကသုိလ္ မ္ဟ ပ္ရန္ လာ္ရပီးေနာက္ န္ဟစ္ အရ္ဝယ္ ဦးသန္ ့သည္ ပန္ းတနော္ အမ္ယုိးသား ေက္ယာင္ း";
const kTeststr_na_Latn =
  " arcol obabakaen riringa itorere ibibokiei ababaro min kuduwa airumena baoin tokin rowiowet itiket keram damadamit eigirow etoreiy row keitsito boney ibingo itsiw dorerin naoerodelaporte s nauruan dictionary a c a c d g h o p s t y aiquen ion eins aiquen";
const kTeststr_ne_Deva =
  "अरू ठाऊँबाटपनि खुलेको छ यो खाता अर अरू ठाऊँबाटपनि खुलेको छ यो खाता अर ू";
const kTeststr_nl_Latn =
  " a als volgt te werk om een configuratiebestand te maken sitemap gen py ebruik filters om de s op te geven die moeten worden toegevoegd of uitgesloten op basis van de opmaaktaal elke sitemap mag alleen de s bevatten voor een bepaalde opmaaktaal dit";
const kTeststr_nn_Latn =
  " a for verktylina til å hjelpa deg å nå oss merk at pagerank syninga ikkje automatisk kjem til å henta inn informasjon frå sider med argument dvs frå sider med eit i en dersom datamaskina di er plassert bak ein mellomtenar for vevsider kan det verka";
const kTeststr_nb_Latn =
  " a er obligatorisk tidsforskyvning plassering av katalogsøk planinformasjon loggfilbane gruppenavn kontoinformasjon passord domene gruppeinformasjon alle kampanjesporing alternativ bruker grupper oppgaveplanlegger oppgavehistorikk kontosammendrag antall";
const kTeststr_nr_Latn =
  "ikomiti elawulako yegatja  emhlanganweni walo ]imithetho mgomo ye anc ibekwa malunga wayo begodu ubudosiphambili kugandelela lokho okutjhiwo yi  lokha nayithi abantu ngibo  ";

const kTeststr_nso_Latn =
  "Bophara bja Asia ekaba 8.6% bja lefase goba 29.4% bja naga ya lefase (ntle le mawatle). Asia enale badudu bao bakabago dimillione millione tše nne (4 billion) yeo e bago 60% ya badudi ba lefase ka bophara. A bapolelwa rena sefapanong mehleng ya Pontius Pilatus. A hlokofatšwa, A bolokwa, A tsoga ka letšatši la boraro, ka mo mangwalo a bolelago ka gona, a rotogela magodimong, ";
const kTeststr_ny_Latn =
  "Boma ndi gawo la dziko lomwe linapangidwa ndi cholinga chothandiza ntchito yolamulira. Kuŵalako kulikuunikabe mandita, Edipo nyima unalephera kugonjetsa kuŵalako.";
const kTeststr_oc_Latn =
  "  Pasmens, la classificacion pus admesa uei (segon Juli Ronjat e Pèire Bèc) agropa lei parlars deis Aups dins l'occitan vivaroaupenc e non dins lo dialècte provençau.";
const kTeststr_om_Latn =
  " afaan katalaa bork bork bork hiikaa jira hin argamne gareen barbaadame hin argamne gargarsa qube en gar bayee jira garee walitti firooman gareewwan walitti firooman fuula web akka tartiiba qubeetiin agarsiisi akka tartiiba qubeetiin agarsiisaa jira akka";
const kTeststr_or_Orya = "ଅକ୍ଟୋବର ଡିସେମ୍ବର";
const kTeststr_pa_Guru =
  " ਂ ਦਿਨਾਂ ਵਿਚ ਭਾਈ ਸਾਹਿਬ ਦੀ ਬੁੱਚੜ ਗੋਬਿੰਦ ਰਾਮ ਨਾਲ ਅੜਫਸ ਚੱਲ ਰਹੀ ਸੀ ਗੋਬਿੰਦ ਰਾਮ ਨੇ ਭਾਈ ਸਾਹਿਬ ਦੀਆਂ ਭੈਣਾ";
const kTeststr_pl_Latn =
  " a australii będzie widział inne reklamy niż użytkownik z kanady kierowanie geograficzne sprawia że reklamy są lepiej dopasowane do użytkownika twojej strony oznacza to także że możesz nie zobaczyć wszystkich reklam które są wyświetlane na";
const kTeststr_ps_Arab =
  " اتو مستقل رياست جوړ شو او د پخواني ادبي انجمن څانګې ددې رياست جز شوی او ددې انجمن د ژبې مديريت د پښتو ټولنې په لوی مديريت واوښت لوی مدير يې د";
const kTeststr_pt_Latn =
  " a abit prevê que a entrada desses produtos estrangeiros no mercado têxtil e vestuário do brasil possa reduzir os preços em cerca de a partir de má notícia para os empresários que terão que lutar para garantir suas margens de lucro mas boa notícia";
const kTeststr_qu_Latn =
  " is t ipanakunatapis rikuchinankupaq qanpa simiykipi noqaykoqpa uya jllanakunamanta kunan jamoq simikunaman qelqan tiyan watukuy qpa uyata qanpa llaqtaykipi llank anakuna simimanta yanapakuna simimanta mayqen llaqtallapis kay simimanta t ijray qpa qelqa";
const kTeststr_rm_Latn =
  " Cur ch’il chantun Turitg ha dà il dretg da votar a las dunnas (1970) è ella vegnida elegida en il cussegl da vischnanca da Zumikon per la Partida liberaldemocratica svizra (PLD). Da 1974 enfin 1982 è ella stada presidenta da vischnanca da Zumikon. L’onn 1979 è Elisabeth Kopp vegnida elegida en il Cussegl naziunal e reelegida quatter onns pli tard cun in resultat da sur 100 000 vuschs. L’onn 1984 è ella daventada vicepresidenta da la PLD.";
const kTeststr_rn_Latn =
  " ishaka mu ndero y abana bawe ganira n abigisha nimba hari ingorane izo ari zo zose ushobora gusaba kubonana n umwigisha canke kuvugana nawe kuri terefone inyuma y uko babarungikira urutonde rw amanota i muhira mu bisanzwe amashure aratumira abavyeyi";
const kTeststr_ro_Latn =
  " a anunţurilor reţineţi nu plătiţi pentru clicuri sau impresii ci numai atunci când pe site ul dvs survine o acţiune dorită site urile negative nu pot avea uri de destinaţie daţi instrucţiuni societăţii dvs bancare sau constructoare să";
const kTeststr_ro_Cyrl =
  "оперативэ а органелор ши институциилор екзекутиве ши а органелор жудичиаре але путерий де стат фиекэруй орган ал путерий де стат и се";
const kTeststr_ru_Cyrl = " а неправильный формат идентификатора дн назад";
const kTeststr_rw_Latn =
  " dore ibyo ukeneye kumenya ukwo watubona ibibazo byinshi abandi babaza ububonero byibibina google onjela ho izina dyikyibina kyawe onjela ho yawe mulugo kulaho ibyandiko byawe shyilaho tegula yawe tulubaka tukongeraho iyanya mishya buliko tulambula";
const kTeststr_sa_Deva =
  " ं क र्मणस् त स्य य त्कि ङ्चेह करो त्यय ं त स्माल् लोका त्पु नरै ति अस्मै लोका य क र्मण इ ति नु काम";
const kTeststr_sa_Latn =
  " brahmā tatraivāntaradhīyata tataḥ saśiṣyo vālmīkir munir vismayam āyayau tasya śiṣyās tataḥ sarve jaguḥ ślokam imaṃ punaḥ muhur muhuḥ prīyamāṇāḥ prāhuś ca bhṛśavismitāḥ samākṣaraiś caturbhir yaḥ pādair gīto";
const kTeststr_sco_Latn =
  " a gless an geordie runciman ower a gless an tamson their man preached a hale hoor aboot the glorious memories o forty three an backsliders an profane persons like esau an aboot jeroboam the son o nebat that gaed stravagin to anither kirk an made aa israel";
const kTeststr_sd_Arab =
  " اضافو ٿي ٿيو پر اها خبر عثمان کي بعد پيئي ته سگريٽ ڇڪيندڙ مسلمان نه هو بلڪ هندو هو دڪان تي پهچي عثمان ڪسبت کولي گراهڪن جي سيرب لاهڻ شروع ڪئي پر";
const kTeststr_sg_Latn =
  " atâa na âkotta zo me lâkwê angbâ gï tarrango nî âkotta zo tî koddoro nî âde agbû tenne nî na kate töngana mbênî kotta kpalle tî nzönî dutï tî halëzo pëpe atâa sô âla lü gbâ tî ândya tî mâi na sahngo asâra gbâ tî";
const kTeststr_si_Sinh =
  " අනුරාධ මිහිඳුකුල නමින් සකුරා ට ලිපියක් තැපෑලෙන් එවා තිබුණා කි ් රස්ටි ෂෙල්ටන් ප ් රනාන්දු ද";
const kTeststr_sit_NP =
  " dialekten in de roerstreek pierre bakkes oet roerstreek blz bewirk waordebook zónjig oktoeaber is t ieëste mofers waordebook oetgekaome dit waordebook is samegestèldj";
const kTeststr_sk_Latn =
  " a aktivovať reklamnú kampaň ak chcete kampaň pred spustením ešte prispôsobiť uložte ju ako šablónu a pokračujte v úprave vyberte si jednu z možností nižšie a kliknite na tlačidlo uložiť kampaň nastavenia kampane môžete ľubovoľne";
const kTeststr_sl_Latn =
  " adsense stanje prijave za google adsense google adsense račun je bil začasno zamrznjen pozdravljeni hvala za vaše zanimanje v google adsense po pregledu vaše prijavnice so naši strokovnjaki ugotovili da spletna stran ki je trenutno povezana z vašim";
const kTeststr_sm_Latn =
  " autu mea o lo totonu le e le minaomia matou te tuu i totonu i le faamatalaina o le suesuega i taimi uma mea o lo totonu fuafua i mea e tatau fa afoi tala mai le newsgroup mataupu fa afoi mai tala e ai le mataupu e ai totonu tusitala o le itu o faamatalaga";
const kTeststr_sn_Latn =
  " chete vanyori vanotevera vakabatsira kunyora zvikamu zvino kumba home tinyorere tsamba chikamu chakumbirwa hachina kuwanikwa chikamu ichi cheninge chakayiswa kuimwe nzvimbo mudhairekitori rino chimwe chikamu chopadhuze pane chinhu chatadza kushanda bad";
const kTeststr_so_Latn =
  " a oo maanta bogga koobaad ugu qoran yahey beesha caalamka laakiin si kata oo beesha caalamku ula guntato soomaaliya waxa aan shaki ku jirin in aakhirataanka dadka soomaalida oo kaliya ay yihiin ku soomaaliya ka saari kara dhibka ay ku jirto";
const kTeststr_sq_Latn =
  " a do të kërkoni nga beogradi që të njohë pavarësinë e kosovës zoti thaçi prishtina është gati ta njoh pavarësinë e serbisë ndërsa natyrisht se do të kërkohet një gjë e tillë që edhe beogradi ta njoh shtetin e pavarur dhe sovran të";
const kTeststr_sr_Cyrl =
  "балчак балчак на мапи србије уреди демографија у насељу балчак живи пунолетна становника а просечна старост становништва износи година";
const kTeststr_sr_Latn =
  "Društvo | četvrtak 1.08.2013 | 13:43 Krade se i izvorska voda Izvor:  Gornji Milanovac -- U gružanskom selu Belo Polje prošle noći ukradeno je više od 10.000 litara kojima je obijen bazen. Bazen je bio zaključan i propisno obezbeđen.";

const kTeststr_sr_ME_Latn =
  "savjet pobjeda a radi bržeg rada pošto rom radi sporije nego ram izvorni rom se isključuje a dio ram a se rezerviše te se u njega ne ploča procesor ram memorija grafička kartica zvučna kartica modem mrežna kartica napojna jedinica uređaji za pohranjivanje";
const kTeststr_ss_Latn =
  " bakhokhintsela yesikhashana bafake imininingwane ye akhawunti leliciniso kulelifomu nangabe akukafakwa imininingwane leliciniso imali lekhokhiwe angeke ifakwe kumkhokhintsela lofanele imininingwane ye akhawunti ime ngalendlela lelandzelako inombolo";
const kTeststr_st_Latn =
  " bang ba nang le thahasello matshwao a sehlooho thuto e thehilweng hodima diphetho ke tsela ya ho ruta le ho ithuta e totobatsang hantle seo baithuti ba lokelang ho se fihlella ntlhatheo eo e sebetsang ka yona ke ya hore titjhere o hlakisa pele seo";
const kTeststr_su_Latn =
  "Nu ngatur kahirupan warga, keur kapentingan pamarentahan diatur ku RT, RW jeung Kepala Dusun, sedengkeun urusan adat dipupuhuan ku Kuncen jeung kepala adat. Sanajan Kampung Kuta teu pati anggang jeung lembur sejenna nu aya di wewengkon Desa Pasir Angin, tapi boh wangunan imah atawa tradisi kahirupan masarakatna nenggang ti nu lian.";
const kTeststr_sv_Latn =
  " a bort objekt från google desktop post äldst meny öretag dress etaljer alternativ för vad är inne yaste google skrivbord plugin program för nyheter google visa nyheter som är anpassade efter de artiklar som du läser om du till exempel läser";
const kTeststr_sw_Latn =
  " a ujumbe mpya jumla unda tafuta na angalia vikundi vya kujadiliana na kushiriki mawazo iliyopangwa kwa tarehe watumiaji wapya futa orodha hizi lugha hoja vishikanisho vilivyo dhaminiwa ujumbe sanaa na tamasha toka udhibitisho wa neno kwa haraka fikia";
const kTeststr_syr_Syrc =
  "ܐܕܪܝܣ ܓܛܘ ܫܘܪܝܐ ܡܢ ܦܪܢܣܐ ܡܢ ܐܣܦܢܝܐ ܚܐܪܘܬܐ ܒܐܕܪ ܒܢܝܣܢ ܫܛܝܚܘܬܐ ܟܠܢܝܐ ܡܝ̈ܐ ܒܥܠܡܐ";
const kTeststr_ta_Taml =
  " அங்கு ராஜேந்திர சோழனால் கட்டப்பட்ட பிரம்மாண்டமான சிவன் கோவில் ஒன்றும் உள்ளது தொகு";
const kTeststr_te_Telu =
  " ఁ దనర జయించిన తత్వ మరసి చూడఁ దాన యగును రాజయోగి యిట్లు తేజరిల్లుచు నుండు విశ్వదాభిరామ వినర వేమ";
const kTeststr_tg_Arab = "رادیو فردا راديوى آزادى";
const kTeststr_tg_Cyrl =
  " адолат ва инсондӯстиро бар фашизм нажодпарастӣ ва адоват тарҷеҳ додааст чоп кунед ба дигарон фиристед чоп кунед ба дигарон фиристед";
const kTeststr_th_Thai =
  " กฏในการค้นหา หรือหน้าเนื้อหา หากท่านเลือกลงโฆษณา ท่านอาจจะปรับต้องเพิ่มงบประมาณรายวันตา";
const kTeststr_ti_Ethi =
  " ሃገር ተረፎም ዘለዉ ኢትዮጵያውያን ኣብቲ ምስ ኢትዮጵያ ዝዳውብ ኣውራጃ ደቡብ ንኽነብሩ ኣይፍቀደሎምን እዩ ካብ ሃገር ንኽትወጽእ ዜጋ ኹን ወጻእተኛ ናይ";
const kTeststr_tk_Cyrl =
  " айдянларына ынанярмыка эхли боз мейданлары сурулип гутарылан тебигы ота гарып гумлукларда миллиондан да артыкмач ири шахлы малы миллиона";
const kTeststr_tk_Latn =
  " akyllylyk çyn söýgi üçin böwet däl de tebigylykdyr duýgularyň gödeňsiligi aç açanlygy bahyllygy söýgini betnyşanlyk derejesine düşürýändir söýeni söý söýmedige süýkenme özüni söýmeýändigini görmek ýigit üçin uly";
const kTeststr_tl_Latn =
  " a na ugma sa google ay nakaka bantog sa gitna nang kliks na nangyayari sa pamamagitan nang ordinaryong paggagamit at sa kliks na likha nang pandaraya o hindi tunay na paggamit bunga nito nasasala namin ang mga kliks na hindi kailangan o hindi gusto nang";
const kTeststr_tl_Tglg =
  " ᜋᜇ᜔ ᜐᜓᜎᜆ᜔ ᜃ ᜈᜅ᜔ ᜊᜌ᜔ᜊᜌᜒᜈ᜔ ᜂᜉᜅ᜔᜔ ᜋᜐᜈᜌ᜔ ᜎᜅ᜔ ᜁᜐ ᜉᜅ᜔ ᜀᜃ᜔ᜎᜆ᜔ ᜆᜓᜅ᜔ᜃᜓᜎ᜔ ᜐ ᜊᜌ᜔ᜊᜌᜒᜈ᜔ ᜐ ᜆᜒᜅᜒᜈ᜔ ᜃᜓ";
const kTeststr_tlh_Latn =
  " a ghuv bid soh naq jih lodni yisov chich wo vamvo qeylis lunge pu chah povpu vodleh a dah ghah cho ej dah wo che pujwi bommu tlhegh darinmohlahchu pu majqa horey so lom qa ip quv law may vad suvtahbogh wa sanid utlh quv pus datu pu a vitu chu pu johwi tar";
const kTeststr_tn_Latn =
  " go etela batla ditsebe tsa web tse di nang le le batla ditsebe tse di golaganya le tswang mo leka go batla web yotlhe batla mo web yotlhe go bona home page ya google batla mo a o ne o batla gore a o ne o batla ditsebe tsa bihari batla mo re maswabi ga go";
const kTeststr_to_Latn =
  " a ke kumi oku ikai ke ma u vakai ki hono hokohoko faka alafapeti api pe ko e uluaki peesi a ho o fekumi faka malatihi fekumi ki he lea oku fakaha atu pe ko ha fonua fekumi ki he fekumi ki he peesi oku ngaahi me a oku sai imisi alu ki he ki he ulu aki";
const kTeststr_tr_Latn =
  " a ayarlarınızı görmeniz ve yönetmeniz içindir eğer kampanyanız için günlük bütçenizi gözden geçirebileceğiniz yeri arıyorsanız kampanya yönetimi ne gidin kampanyanızı seçin ve kampanya ayarlarını düzenle yi tıklayın sunumu";
const kTeststr_ts_Latn =
  " a ku na timhaka leti nga ta vulavuriwa na google google yi hlonipha yi tlhela yi sirheleta vanhu hinkwavo lava tirhisaka google toolbar ku dyondza hi vusireleli eka system ya hina hi kombela u hlaya vusireleli bya hina eka toolbar mbulavulo wu tshikiwile";
const kTeststr_tt_Cyrl =
  "ачарга да бирмәде чәт чәт килеп тора безнең абыйнымы олы абыйнымы эштән";
const kTeststr_tt_Latn =
  " alarnı eşkärtü proğramnarın eşläwen däwam itü tatar söylämen buldıru wä sizep alu sistemnarın eşläwen däwat itü häm başqalar yılnıñ mayında tatar internetı ictimağıy oyışması milli ts isemle berençe däräcäle häm tat";
const kTeststr_tw_Latn =
  " amammui tumidifo no bɛtow ahyɛ atoro som so mpofirim na wɔasɛe no pasaa ma ayɛ nwonwa dɛn na ɛbɛka wɔn ma wɔayɛ saa bible no ma ho mmuae wɔ adiyisɛm nhoma no mu sɛ onyankopɔn na ɔde hyɛɛ wɔn komam sɛ wɔmma ne nsusuwii mmra mu";
const kTeststr_ug_Arab =
  " ئالەملەرنىڭ پەرۋەردىگارىدىن تىلەيمەن سىلەر بۇ يەرلەردە باغچىلاردىن بۇلاقلاردىن زىرائەتلەردىن يۇمشاق پىشقان خورمىلاردىن بەھرىمەن بولۇپ";
const kTeststr_ug_Cyrl =
  " а башлиди әмма бу қетимқи канада мәтбуатлириниң хәвәрлиридә илгирикидәк хитай һөкүмәт мәтбуатлиридин нәқил алидиған вә уни көчүрүп";
const kTeststr_ug_Latn =
  " adawet bolghachqa hazir musherrepmu bu ikki partiyining birleshme hökümet qurushta pikir birliki hasil qilalmasliqini kütüwatqan iken wehalenki pakistan xelq partiyisining rehbiri asif eli zerdari pakistandiki bashqa ushshaq partiyilerning rehberliri";
const kTeststr_uk_Cyrl =
  " а більший бюджет щоб забезпечити собі максимум прибутків від переходів відстежуйте свої об яви за датою географічним розташуванням";
const kTeststr_ur_Arab =
  " آپ کو کم سے کم ممکنہ رقم چارج کرتا ہے اس کی مثال کے طور پر فرض کریں اگر آپ کی زیادہ سے زیادہ قیمت فی کلِک امریکی ڈالر اور کلِک کرنے کی شرح ہو تو";
const kTeststr_uz_Arab =
  " آرقلی بوتون سیاسی حزب و گروه لرفعالیتیگه رخصت بیرگن اخبارات واسطه لری شو ییل مدتیده مثال سیز ترقی تاپکن و اهالی نینگ اقتصادی وضعیتی اوتمیش";
const kTeststr_uz_Cyrl =
  " а гапирадиган бўлсак бунинг иккита йўли бор биринчиси мана шу қуриган сатҳини қумликларни тўхтатиш учун экотизимни мустаҳкамлаш қумга";
const kTeststr_uz_Latn =
  " abadiylashtirildi aqsh ayol prezidentga tayyormi markaziy osiyo afg onistonga qanday yordam berishi mumkin ukrainada o zbekistonlik muhojirlar tazyiqdan shikoyat qilmoqda gruziya va ukraina hozircha natoga qabul qilinmaydi afg oniston o zbekistonni g";
const kTeststr_ve_Latn =
  "Vho ṱanganedzwa kha Wikipedia nga tshiVenḓa. Vhadivhi vha manwalo a TshiVenda vha talusa divhazwakale na vhubvo ha Vhavenda ngau fhambana. Vha tikedza mbuno dzavho uya nga mawanwa a thoduluso dze vha ita. Vhanwe vha vhatodulusi vhari Vhavenda vho tumbuka Afrika vhukati vha tshimbila vha tshiya Tshipembe ha Afrika, Rhodesia hune ha vho vhidzwa Zimbagwe namusi.";
const kTeststr_vi_Latn =
  " adsense cho nội dung nhà cung cấp dịch vụ di động xác minh tín dụng thay đổi nhãn kg các ô xem chi phí cho từ chối các đơn đặt hàng dạng cấp dữ liệu ác minh trang web của bạn để xem";
const kTeststr_vo_Latn =
  " brefik se volapükavol nüm balid äpubon ün dü lif lölik okas redakans älaipübons gasedi at nomöfiko äd ai mu kuratiko pläo timü koup nedäna fa ns deutän kü päproibon fa koupanef me gased at ästeifülom ad propagidön volapüki as sam ün";
const kTeststr_war_Latn =
  "Amo ini an balay han Winaray o Binisaya nga Lineyte-Samarnon nga Wikipedia, an libre ngan gawasnon nga ensayklopedya nga bisan hin-o puyde magliwat o mag-edit. An Wikipedia syahan gintikang ha Iningles nga yinaknan han tuig 2001. Ini nga bersyon Winaray gintikang han ika-25 han Septyembre 2005 ngan ha yana mayda 514,613 nga artikulo. Kon karuyag niyo magsari o magprobar, pakadto ha . An Gastrotheca pulchra[2] in uska species han Anura nga ginhulagway ni Ulisses Caramaschi ngan Rodrigues hadton 2007. An Gastrotheca pulchra in nahilalakip ha genus nga Gastrotheca, ngan familia nga Hemiphractidae.[3][4] Ginklasipika han IUCN an species komo kulang hin datos.[1] Waray hini subspecies nga nakalista.[3]";
const kTeststr_wo_Latn =
  " am ak dëgg dëggam ak gëm aji bind ji te gëstu ko te jëfandikoo tegtalu xel ci saxal ko sokraat nag jëfandikoo woon na xeltu ngir tas jikko yu rafet ci biir nit ñi ak dëggu ak soppante sokraat nag ñëw na mook aflaton platon sukkandiku ci ñaari";
const kTeststr_xh_Latn =
  " a naynga zonke futhi libhengezwa kwiwebsite yebond yasemzantsi afrika izinga elisebenzayo xa usenza olu tyalo mali liya kusebenza de liphele ixesha lotyalo mali lwakho inzala ihlawulwa rhoqo emva kweenyanga ezintandathu ngomhla wamashumi amathathu ananye";
const kTeststr_xx_Bugi = "ᨄᨛᨑᨊᨒ ᨑᨗ ᨔᨒᨗᨓᨛ ᨕᨗᨋᨗᨔᨗ ᨒᨛᨄ ᨑᨛᨔᨛᨆᨗᨊ";
const kTeststr_xx_Goth = "𐌰 𐌰𐌱𐍂𐌰𐌷𐌰𐌼 𐌰𐌲𐌲𐌹𐌻𐌹𐍃𐌺𐍃 𐌸𐌹𐌿𐌳𐌹𐍃𐌺𐍃 𐍆𐍂𐌰𐌲𐌺𐌹𐍃𐌺𐍃";
const kTeststr_yi_Hebr =
  "און פאנטאזיע ער איז באקאנט צים מערסטן פאר זיינע באַלאַדעס ער האָט געוווינט אין ווארשע יעס פאריס ליווערפול און לאנדאן סוף כל סוף איז ער";
const kTeststr_yo_Latn =
  " abinibi han ikawe alantakun le ni opolopo ede abinibi ti a to lesese bi eniyan to fe lo se fe lati se atunse jowo mo pe awon oju iwe itakunagbaye miran ti ako ni oniruru ede abinibi le faragba nipa atunse ninu se iwadi blogs ni ori itakun agbaye ti e ba";
const kTeststr_za_Hani =
  " 两个宾语的字数较少时 只带一个动词 否则就带两个动词 三句子类 从句子方面去谈汉 壮语结构格式相异的类型的 叫句子类 汉 壮语中 句子类结构格式有差别的自然不少";
const kTeststr_za_Latn =
  " dih yinzminz ndaej daengz bujbienq youjyau dih cingzyin caeuq cinhingz diuz daihit boux boux ma daengz lajmbwn couh miz cwyouz cinhyenz caeuq genzli bouxboux bingzdaengj gyoengq vunz miz lijsing caeuq liengzsim wngdang daih gyoengq de lumj beixnuengx";
const kTeststr_zh_Hans =
  "产品的简报和公告 提交该申请后无法进行更改 请确认您的选择是正确的 对于要提交的图书 我确认 我是版权所有者或已得到版权所有者的授权 要更改您的国家 地区 请在此表的最上端更改您的";
const kTeststr_zh_Hant =
  " 之前為 帳單交易作業區 已變更 廣告內容 之前為 銷售代表 之前為 張貼日期為 百分比之前為 合約 為 目標對象條件已刪除 結束日期之前為";
const kTeststr_zu_Latn =
  " ana engu uma inkinga iqhubeka siza ubike kwi isexwayiso ngenxa yephutha lomlekeleli sikwazi ukubuyisela emuva kuphela imiphumela engaqediwe ukuthola imiphumela eqediwe zama ukulayisha kabusha leli khasi emizuzwini engu uma inkinga iqhubeka siza uthumele";
const kTeststr_zzb_Latn =
  "becoose a ve a leemit qooereees tu vurds um gesh dee bork bork nu peges vere a fuoond cunteeening is a fery cummun vurd und ves nut inclooded in yuoor seerch zee ooperetur is unnecessery ve a incloode a ell seerch terms by deffoolt um de hur de hur de hur";
const kTeststr_zze_Latn =
  " a diffewent type of seawch send feedback about google wiwewess seawch to wap google com wesuwts found on de entiwe web fow wesuwts found on de mobiwe web fow de functionawity of de toolbar up button has been expanded swightwy it now considews fow exampwe";
const kTeststr_zzh_Latn =
  " b x z un b e t und rs n a dr ss p as ry an th r a dr ss ry us n a l ss mb gu us c ti n l ke a z p c d n a dr ss nt r d pl as en r n a dr ss y ur s ar h f r n ar d d n t m tch ny l c ti n w th n m l s nd m r r at d p g s th l c ti ns b l w w r ut m t ca y";
const kTeststr_zzp_Latn =
  " away ackupbay editcray ardcay ybay isitingvay ouryay illingbay eferencespray agepay orway isitvay ethay adwordsway elphay entrecay orfay oremay etailsday adwordsway ooglegay omcay upportsay";

// Two very close Wikipedia page beginnings
const kTeststr_ms_close =
  "sukiyaki wikipedia bahasa melayu ensiklopedia bebas sukiyaki dari wikipedia bahasa melayu ensiklopedia bebas lompat ke navigasi gelintar sukiyaki sukiyaki  hirisan tipis daging lembu sayur sayuran dan tauhu di dalam periuk besi yang dimasak di atas meja makan dengan cara rebusan sukiyaki dimakan dengan mence";
const kTeststr_id_close =
  "sukiyaki wikipedia indonesia ensiklopedia bebas berbahasa bebas berbahasa indonesia langsung ke navigasi cari untuk pengertian lain dari sukiyaki lihat sukiyaki irisan tipis daging sapi sayur sayuran dan tahu di dalam panci besi yang dimasak di atas meja makan dengan cara direbus sukiyaki dimakan dengan mence";

// Simple intermixed French/English text
const kTeststr_fr_en_Latn =
  "France is the largest country in Western Europe and the third-largest in Europe as a whole. " +
  "A accès aux chiens et aux frontaux qui lui ont été il peut consulter et modifier ses collections et exporter " +
  "Cet article concerne le pays européen aujourd’hui appelé République française. Pour d’autres usages du nom France, " +
  "Pour une aide rapide et effective, veuiller trouver votre aide dans le menu ci-dessus." +
  "Motoring events began soon after the construction of the first successful gasoline-fueled automobiles. The quick brown fox jumped over the lazy dog";

// This can be used to cross-check the build date of the main quadgram table
const kTeststr_version =
  "qpdbmrmxyzptlkuuddlrlrbas las les qpdbmrmxyzptlkuuddlrlrbas el la qpdbmrmxyzptlkuuddlrlrbas";
/* eslint-enable no-unused-vars */

const kTestPairs = [
  // A simple case to begin
  ["en", "ENGLISH", kTeststr_en],

  // 20 languages recognized via Unicode script
  ["hy", "ARMENIAN", kTeststr_hy_Armn],
  ["chr", "CHEROKEE", kTeststr_chr_Cher],
  ["dv", "DHIVEHI", kTeststr_dv_Thaa],
  ["ka", "GEORGIAN", kTeststr_ka_Geor],
  ["el", "GREEK", kTeststr_el_Grek],
  ["gu", "GUJARATI", kTeststr_gu_Gujr],
  ["iu", "INUKTITUT", kTeststr_iu_Cans],
  ["kn", "KANNADA", kTeststr_kn_Knda],
  ["km", "KHMER", kTeststr_km_Khmr],
  ["lo", "LAOTHIAN", kTeststr_lo_Laoo],
  ["lif", "LIMBU", kTeststr_lif_Limb],
  ["ml", "MALAYALAM", kTeststr_ml_Mlym],
  ["or", "ORIYA", kTeststr_or_Orya],
  ["pa", "PUNJABI", kTeststr_pa_Guru],
  ["si", "SINHALESE", kTeststr_si_Sinh],
  ["syr", "SYRIAC", kTeststr_syr_Syrc],
  ["tl", "TAGALOG", kTeststr_tl_Tglg], // Also in quadgram list below
  ["ta", "TAMIL", kTeststr_ta_Taml],
  ["te", "TELUGU", kTeststr_te_Telu],
  ["th", "THAI", kTeststr_th_Thai],

  // 4 languages regognized via single letters
  ["zh", "CHINESE", kTeststr_zh_Hans],
  ["zh-Hant", "CHINESET", kTeststr_zh_Hant],
  ["ja", "JAPANESE", kTeststr_ja_Hani],
  ["ko", "KOREAN", kTeststr_ko_Hani],

  // 60 languages recognized via combinations of four letters
  ["af", "AFRIKAANS", kTeststr_af_Latn],
  ["sq", "ALBANIAN", kTeststr_sq_Latn],
  ["ar", "ARABIC", kTeststr_ar_Arab],
  ["az", "AZERBAIJANI", kTeststr_az_Latn],
  ["eu", "BASQUE", kTeststr_eu_Latn],
  ["be", "BELARUSIAN", kTeststr_be_Cyrl],
  ["bn", "BENGALI", kTeststr_bn_Beng], // No Assamese in subset
  ["bh", "BIHARI", kTeststr_bh_Deva],
  ["bg", "BULGARIAN", kTeststr_bg_Cyrl],
  ["ca", "CATALAN", kTeststr_ca_Latn],
  ["ceb", "CEBUANO", kTeststr_ceb_Latn],
  ["hr", "CROATIAN", kTeststr_hr_Latn, [false, 0, "el", 4]],
  ["cs", "CZECH", kTeststr_cs_Latn],
  ["da", "DANISH", kTeststr_da_Latn],
  ["nl", "DUTCH", kTeststr_nl_Latn],
  ["en", "ENGLISH", kTeststr_en_Latn],
  ["et", "ESTONIAN", kTeststr_et_Latn],
  ["fi", "FINNISH", kTeststr_fi_Latn],
  ["fr", "FRENCH", kTeststr_fr_Latn],
  ["gl", "GALICIAN", kTeststr_gl_Latn],
  ["lg", "GANDA", kTeststr_lg_Latn],
  ["de", "GERMAN", kTeststr_de_Latn],
  ["ht", "HAITIAN_CREOLE", kTeststr_ht_Latn],
  ["he", "HEBREW", kTeststr_he_Hebr],
  ["hi", "HINDI", kTeststr_hi_Deva],
  ["hmn", "HMONG", kTeststr_blu_Latn],
  ["hu", "HUNGARIAN", kTeststr_hu_Latn],
  ["is", "ICELANDIC", kTeststr_is_Latn],
  ["id", "INDONESIAN", kTeststr_id_Latn],
  ["ga", "IRISH", kTeststr_ga_Latn],
  ["it", "ITALIAN", kTeststr_it_Latn],
  ["jw", "JAVANESE", kTeststr_jw_Latn],
  ["rw", "KINYARWANDA", kTeststr_rw_Latn],
  ["lv", "LATVIAN", kTeststr_lv_Latn],
  ["lt", "LITHUANIAN", kTeststr_lt_Latn],
  ["mk", "MACEDONIAN", kTeststr_mk_Cyrl],
  ["ms", "MALAY", kTeststr_ms_Latn],
  ["mt", "MALTESE", kTeststr_mt_Latn],
  ["mr", "MARATHI", kTeststr_mr_Deva, [false, 0, "te", 3]],
  ["ne", "NEPALI", kTeststr_ne_Deva],
  ["nb", "NORWEGIAN", kTeststr_nb_Latn],
  ["fa", "PERSIAN", kTeststr_fa_Arab],
  ["pl", "POLISH", kTeststr_pl_Latn],
  ["pt", "PORTUGUESE", kTeststr_pt_Latn],
  ["ro", "ROMANIAN", kTeststr_ro_Latn],
  ["ro", "ROMANIAN", kTeststr_ro_Cyrl],
  ["ru", "RUSSIAN", kTeststr_ru_Cyrl],
  ["gd", "SCOTS_GAELIC", kTeststr_gd_Latn],
  ["sr", "SERBIAN", kTeststr_sr_Cyrl],
  ["sr", "SERBIAN", kTeststr_sr_Latn],
  ["sk", "SLOVAK", kTeststr_sk_Latn],
  ["sl", "SLOVENIAN", kTeststr_sl_Latn],
  ["es", "SPANISH", kTeststr_es_Latn],
  ["sw", "SWAHILI", kTeststr_sw_Latn],
  ["sv", "SWEDISH", kTeststr_sv_Latn],
  ["tl", "TAGALOG", kTeststr_tl_Latn],
  ["tr", "TURKISH", kTeststr_tr_Latn],
  ["uk", "UKRAINIAN", kTeststr_uk_Cyrl],
  ["ur", "URDU", kTeststr_ur_Arab],
  ["vi", "VIETNAMESE", kTeststr_vi_Latn],
  ["cy", "WELSH", kTeststr_cy_Latn],
  ["yi", "YIDDISH", kTeststr_yi_Hebr],

  // Added 2013.08.31 so-Latn ig-Latn ha-Latn yo-Latn zu-Latn
  ["so", "SOMALI", kTeststr_so_Latn],
  ["ig", "IGBO", kTeststr_ig_Latn],
  ["ha", "HAUSA", kTeststr_ha_Latn],
  ["yo", "YORUBA", kTeststr_yo_Latn],
  ["zu", "ZULU", kTeststr_zu_Latn],
  // Added 2014.01.22 bs-Latn
  ["bs", "BOSNIAN", kTeststr_bs_Latn],

  // 2 statistically-close languages
  ["id", "INDONESIAN", kTeststr_id_close, [true, 80], []],
  ["ms", "MALAY", kTeststr_ms_close],

  // Simple intermixed French/English text
  ["fr", "FRENCH", kTeststr_fr_en_Latn, [false, 80, "en", 32]],

  // Cross-check the main quadgram table build date
  // Change the expected language each time it is rebuilt
  ["az", "AZERBAIJANI", kTeststr_version], // 2014.01.31
];

const { setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);
let { LanguageDetector, workerManager } = ChromeUtils.importESModule(
  "resource://gre/modules/translations/LanguageDetector.sys.mjs"
);

function check_result(result, langCode, expected) {
  equal(result.language, langCode, "Expected language code");

  // Round percentage up to the nearest 5%, since most strings are
  // detected at slightly less than 100%, and we don't want to
  // encode each exact value.
  let percent = result.languages[0].percent;
  percent = Math.ceil(percent / 20) * 20;

  equal(
    result.languages[0].languageCode,
    langCode,
    "Expected first guess language code"
  );
  equal(percent, expected[1] || 100, "Expected first guess language percent");

  if (expected.length < 3) {
    // We're not expecting a second language.
    equal(result.languages.length, 1, "Expected only one language result");
  } else {
    equal(result.languages.length, 2, "Expected two language results");

    equal(
      result.languages[1].languageCode,
      expected[2],
      "Expected second guess language code"
    );
    equal(
      result.languages[1].percent,
      expected[3],
      "Expected second guess language percent"
    );
  }

  equal(result.confident, !expected[0], "Expected confidence");
}

add_task(async function test_pairs() {
  for (let item of kTestPairs) {
    let params = [
      item[2],
      { text: item[2], tld: "com", language: item[0], encoding: "utf-8" },
    ];

    for (let [i, param] of params.entries()) {
      // For test items with different expected results when using the
      // language hint, use those for the hinted version of the API.
      // Otherwise, fall back to the first set of expected values.
      let expected = item[3 + i] || item[3] || [];

      let result = await LanguageDetector.detectLanguage(param);
      check_result(result, item[0], expected);
    }
  }
});

// Test that the worker is flushed shortly after processing a large
// string.
add_task(async function test_worker_flush() {
  let test_string = kTeststr_fr_en_Latn;
  let test_item = kTestPairs.find(item => item[2] == test_string);

  // Set shorter timeouts and lower string lengths to make things easier
  // on the test infrastructure.
  workerManager.LARGE_STRING = test_string.length - 1;
  workerManager.IDLE_TIMEOUT = 1000;

  equal(
    workerManager.idleTimeoutId,
    null,
    "Should have no idle timeout to start with"
  );

  let result = await LanguageDetector.detectLanguage(test_string);

  // Make sure the results are still correct.
  check_result(result, test_item[0], test_item[3]);

  // We should have an idle timeout after processing the string.
  Assert.notEqual(
    workerManager.idleTimeoutId,
    null,
    "Should have an idle timeout"
  );
  Assert.notEqual(workerManager.worker, null, "Should have a worker instance");
  Assert.notEqual(
    workerManager.workerPromise,
    null,
    "Should have a worker promise"
  );

  // Wait for the idle timeout to elapse.
  // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
  await new Promise(resolve => setTimeout(resolve, workerManager.IDLE_TIMEOUT));

  equal(
    workerManager.idleTimeoutId,
    null,
    "Should have no idle timeout after it has elapsed"
  );
  equal(
    workerManager.worker,
    null,
    "Should have no worker instance after idle timeout"
  );
  equal(
    workerManager.workerPromise,
    null,
    "Should have no worker promise after idle timeout"
  );

  // We should still be able to use the language detector after its
  // worker has been flushed.
  result = await LanguageDetector.detectLanguage(test_string);

  // Make sure the results are still correct.
  check_result(result, test_item[0], test_item[3]);
});
