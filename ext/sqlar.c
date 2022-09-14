<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<base href="https://sqlite.org/src/doc/tip/ext/misc/sqlar.c" />
<meta http-equiv="Content-Security-Policy" content="default-src 'self' data:; script-src 'self' 'nonce-11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa'; style-src 'self' 'unsafe-inline'; img-src * data:" />
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SQLite: Documentation</title>
<link rel="alternate" type="application/rss+xml" title="RSS Feed"  href="/src/timeline.rss" />
<link rel="stylesheet" href="/src/style.css?id=666dd6d0" type="text/css" />
</head>
<body class="doc rpage-doc cpage-doc">
<div class="header">
<div class="title">
<img id='sqlitelogo' src='/src/logo' style='height:50px;'> 
<span style='vertical-align:super;'><span id='titlesep'>/</span> Documentation</span>
</div>
<div class="status"><a href='/src/login'>Login</a>
</div>
</div>
<div class="mainmenu">
<a id='hbbtn' href='/src/sitemap' aria-label='Site Map'>&#9776;</a><a href='https://sqlite.org/' class=''>Home</a>
<a href='/src/timeline' class=''>Timeline</a>
<a href='https://sqlite.org/forum/forum' class='desktoponly'>Forum</a>
</div>
<div id='hbdrop'></div>
<div class="content"><span id="debugMsg"></span>
<blockquote><pre>
/*
** 2017-12-17
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** Utility functions sqlar_compress() and sqlar_uncompress(). Useful
** for working with sqlar archives and used by the shell tool&#39;s built-in
** sqlar support.
*/
#include &quot;sqlite3ext.h&quot;
SQLITE_EXTENSION_INIT1
#include &lt;zlib.h&gt;
#include &lt;assert.h&gt;

/*
** Implementation of the &quot;sqlar_compress(X)&quot; SQL function.
**
** If the type of X is SQLITE_BLOB, and compressing that blob using
** zlib utility function compress() yields a smaller blob, return the
** compressed blob. Otherwise, return a copy of X.
**
** SQLar uses the &quot;zlib format&quot; for compressed content.  The zlib format
** contains a two-byte identification header and a four-byte checksum at
** the end.  This is different from ZIP which uses the raw deflate format.
**
** Future enhancements to SQLar might add support for new compression formats.
** If so, those new formats will be identified by alternative headers in the
** compressed data.
*/
static void sqlarCompressFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  assert( argc==1 );
  if( sqlite3_value_type(argv[0])==SQLITE_BLOB ){
    const Bytef *pData = sqlite3_value_blob(argv[0]);
    uLong nData = sqlite3_value_bytes(argv[0]);
    uLongf nOut = compressBound(nData);
    Bytef *pOut;

    pOut = (Bytef*)sqlite3_malloc(nOut);
    if( pOut==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }else{
      if( Z_OK!=compress(pOut, &amp;nOut, pData, nData) ){
        sqlite3_result_error(context, &quot;error in compress()&quot;, -1);
      }else if( nOut&lt;nData ){
        sqlite3_result_blob(context, pOut, nOut, SQLITE_TRANSIENT);
      }else{
        sqlite3_result_value(context, argv[0]);
      }
      sqlite3_free(pOut);
    }
  }else{
    sqlite3_result_value(context, argv[0]);
  }
}

/*
** Implementation of the &quot;sqlar_uncompress(X,SZ)&quot; SQL function
**
** Parameter SZ is interpreted as an integer. If it is less than or
** equal to zero, then this function returns a copy of X. Or, if
** SZ is equal to the size of X when interpreted as a blob, also
** return a copy of X. Otherwise, decompress blob X using zlib
** utility function uncompress() and return the results (another
** blob).
*/
static void sqlarUncompressFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  uLong nData;
  uLongf sz;

  assert( argc==2 );
  sz = sqlite3_value_int(argv[1]);

  if( sz&lt;=0 || sz==(nData = sqlite3_value_bytes(argv[0])) ){
    sqlite3_result_value(context, argv[0]);
  }else{
    const Bytef *pData= sqlite3_value_blob(argv[0]);
    Bytef *pOut = sqlite3_malloc(sz);
    if( Z_OK!=uncompress(pOut, &amp;sz, pData, nData) ){
      sqlite3_result_error(context, &quot;error in uncompress()&quot;, -1);
    }else{
      sqlite3_result_blob(context, pOut, sz, SQLITE_TRANSIENT);
    }
    sqlite3_free(pOut);
  }
}


#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_sqlar_init(
  sqlite3 *db, 
  char **pzErrMsg, 
  const sqlite3_api_routines *pApi
){
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  (void)pzErrMsg;  /* Unused parameter */
  rc = sqlite3_create_function(db, &quot;sqlar_compress&quot;, 1, 
                               SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
                               sqlarCompressFunc, 0, 0);
  if( rc==SQLITE_OK ){
    rc = sqlite3_create_function(db, &quot;sqlar_uncompress&quot;, 2,
                                 SQLITE_UTF8|SQLITE_INNOCUOUS, 0,
                                 sqlarUncompressFunc, 0, 0);
  }
  return rc;
}

</pre></blockquote>
<script nonce='11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa'>/* builtin.c:620 */
(function(){
if(window.NodeList && !NodeList.prototype.forEach){NodeList.prototype.forEach = Array.prototype.forEach;}
if(!window.fossil) window.fossil={};
window.fossil.version = "2.20 [e9d7cf3e92] 2022-09-13 20:11:04 UTC";
window.fossil.rootPath = "/src"+'/';
window.fossil.config = {projectName: "SQLite",
shortProjectName: "",
projectCode: "2ab58778c2967968b94284e989e43dc11791f548",
/* Length of UUID hashes for display purposes. */hashDigits: 8, hashDigitsUrl: 16,
diffContextLines: 5,
editStateMarkers: {/*Symbolic markers to denote certain edit states.*/isNew:'[+]', isModified:'[*]', isDeleted:'[-]'},
confirmerButtonTicks: 3 /*default fossil.confirmer tick count.*/,
skin:{isDark: false/*true if the current skin has the 'white-foreground' detail*/}
};
window.fossil.user = {name: "guest",isAdmin: false};
if(fossil.config.skin.isDark) document.body.classList.add('fossil-dark-style');
window.fossil.page = {name:"doc/tip/ext/misc/sqlar.c"};
})();
</script>
<script nonce='11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa'>/* doc.c:430 */
window.addEventListener('load', ()=>window.fossil.pikchr.addSrcView(), false);
</script>
</div>
<div class="footer">
This page was generated in about
0.022s by
Fossil 2.20 [e9d7cf3e92] 2022-09-13 20:11:04
</div>
<script nonce="11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa">
(function() {
var hbButton = document.getElementById("hbbtn");
if (!hbButton) return;
if (!document.addEventListener) {
hbButton.href = "/src/sitemap";
return;
}
var panel = document.getElementById("hbdrop");
if (!panel) return;
if (!panel.style) return;
var panelBorder = panel.style.border;
var panelInitialized = false;
var panelResetBorderTimerID = 0;
var animate = panel.style.transition !== null && (typeof(panel.style.transition) == "string");
var animMS = panel.getAttribute("data-anim-ms");
if (animMS) {
animMS = parseInt(animMS);
if (isNaN(animMS) || animMS == 0)
animate = false;
else if (animMS < 0)
animMS = 400;
}
else
animMS = 400;
var panelHeight;
function calculatePanelHeight() {
panel.style.maxHeight = '';
var es   = window.getComputedStyle(panel),
edis = es.display,
epos = es.position,
evis = es.visibility;
panel.style.visibility = 'hidden';
panel.style.position   = 'absolute';
panel.style.display    = 'block';
panelHeight = panel.offsetHeight + 'px';
panel.style.display    = edis;
panel.style.position   = epos;
panel.style.visibility = evis;
}
function showPanel() {
if (panelResetBorderTimerID) {
clearTimeout(panelResetBorderTimerID);
panelResetBorderTimerID = 0;
}
if (animate) {
if (!panelInitialized) {
panelInitialized = true;
calculatePanelHeight();
panel.style.transition = 'max-height ' + animMS +
'ms ease-in-out';
panel.style.overflowY  = 'hidden';
panel.style.maxHeight  = '0';
}
setTimeout(function() {
panel.style.maxHeight = panelHeight;
panel.style.border    = panelBorder;
}, 40);
}
panel.style.display = 'block';
document.addEventListener('keydown',panelKeydown,true);
document.addEventListener('click',panelClick,false);
}
var panelKeydown = function(event) {
var key = event.which || event.keyCode;
if (key == 27) {
event.stopPropagation();
panelToggle(true);
}
};
var panelClick = function(event) {
if (!panel.contains(event.target)) {
panelToggle(true);
}
};
function panelShowing() {
if (animate) {
return panel.style.maxHeight == panelHeight;
}
else {
return panel.style.display == 'block';
}
}
function hasChildren(element) {
var childElement = element.firstChild;
while (childElement) {
if (childElement.nodeType == 1)
return true;
childElement = childElement.nextSibling;
}
return false;
}
window.addEventListener('resize',function(event) {
panelInitialized = false;
},false);
hbButton.addEventListener('click',function(event) {
event.stopPropagation();
event.preventDefault();
panelToggle(false);
},false);
function panelToggle(suppressAnimation) {
if (panelShowing()) {
document.removeEventListener('keydown',panelKeydown,true);
document.removeEventListener('click',panelClick,false);
if (animate) {
if (suppressAnimation) {
var transition = panel.style.transition;
panel.style.transition = '';
panel.style.maxHeight = '0';
panel.style.border = 'none';
setTimeout(function() {
panel.style.transition = transition;
}, 40);
}
else {
panel.style.maxHeight = '0';
panelResetBorderTimerID = setTimeout(function() {
panel.style.border = 'none';
panelResetBorderTimerID = 0;
}, animMS);
}
}
else {
panel.style.display = 'none';
}
}
else {
if (!hasChildren(panel)) {
var xhr = new XMLHttpRequest();
xhr.onload = function() {
var doc = xhr.responseXML;
if (doc) {
var sm = doc.querySelector("ul#sitemap");
if (sm && xhr.status == 200) {
panel.innerHTML = sm.outerHTML;
showPanel();
}
}
}
xhr.open("GET", "/src/sitemap?popup");
xhr.responseType = "document";
xhr.send();
}
else {
showPanel();
}
}
}
})();

</script>
<script nonce="11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa">/* style.c:913 */
function debugMsg(msg){
var n = document.getElementById("debugMsg");
if(n){n.textContent=msg;}
}
</script>
<script nonce='11981b23fb5f2bd640d020650c3b7d4715c30a00ca321dfa'>
/* hbmenu.js *************************************************************/
(function() {
var hbButton = document.getElementById("hbbtn");
if (!hbButton) return;
if (!document.addEventListener) return;
var panel = document.getElementById("hbdrop");
if (!panel) return;
if (!panel.style) return;
var panelBorder = panel.style.border;
var panelInitialized = false;
var panelResetBorderTimerID = 0;
var animate = panel.style.transition !== null && (typeof(panel.style.transition) == "string");
var animMS = panel.getAttribute("data-anim-ms");
if (animMS) {
animMS = parseInt(animMS);
if (isNaN(animMS) || animMS == 0)
animate = false;
else if (animMS < 0)
animMS = 400;
}
else
animMS = 400;
var panelHeight;
function calculatePanelHeight() {
panel.style.maxHeight = '';
var es   = window.getComputedStyle(panel),
edis = es.display,
epos = es.position,
evis = es.visibility;
panel.style.visibility = 'hidden';
panel.style.position   = 'absolute';
panel.style.display    = 'block';
panelHeight = panel.offsetHeight + 'px';
panel.style.display    = edis;
panel.style.position   = epos;
panel.style.visibility = evis;
}
function showPanel() {
if (panelResetBorderTimerID) {
clearTimeout(panelResetBorderTimerID);
panelResetBorderTimerID = 0;
}
if (animate) {
if (!panelInitialized) {
panelInitialized = true;
calculatePanelHeight();
panel.style.transition = 'max-height ' + animMS +
'ms ease-in-out';
panel.style.overflowY  = 'hidden';
panel.style.maxHeight  = '0';
}
setTimeout(function() {
panel.style.maxHeight = panelHeight;
panel.style.border    = panelBorder;
}, 40);
}
panel.style.display = 'block';
document.addEventListener('keydown',panelKeydown,true);
document.addEventListener('click',panelClick,false);
}
var panelKeydown = function(event) {
var key = event.which || event.keyCode;
if (key == 27) {
event.stopPropagation();
panelToggle(true);
}
};
var panelClick = function(event) {
if (!panel.contains(event.target)) {
panelToggle(true);
}
};
function panelShowing() {
if (animate) {
return panel.style.maxHeight == panelHeight;
}
else {
return panel.style.display == 'block';
}
}
function hasChildren(element) {
var childElement = element.firstChild;
while (childElement) {
if (childElement.nodeType == 1)
return true;
childElement = childElement.nextSibling;
}
return false;
}
window.addEventListener('resize',function(event) {
panelInitialized = false;
},false);
hbButton.addEventListener('click',function(event) {
event.stopPropagation();
event.preventDefault();
panelToggle(false);
},false);
function panelToggle(suppressAnimation) {
if (panelShowing()) {
document.removeEventListener('keydown',panelKeydown,true);
document.removeEventListener('click',panelClick,false);
if (animate) {
if (suppressAnimation) {
var transition = panel.style.transition;
panel.style.transition = '';
panel.style.maxHeight = '0';
panel.style.border = 'none';
setTimeout(function() {
panel.style.transition = transition;
}, 40);
}
else {
panel.style.maxHeight = '0';
panelResetBorderTimerID = setTimeout(function() {
panel.style.border = 'none';
panelResetBorderTimerID = 0;
}, animMS);
}
}
else {
panel.style.display = 'none';
}
}
else {
if (!hasChildren(panel)) {
var xhr = new XMLHttpRequest();
xhr.onload = function() {
var doc = xhr.responseXML;
if (doc) {
var sm = doc.querySelector("ul#sitemap");
if (sm && xhr.status == 200) {
panel.innerHTML = sm.outerHTML;
showPanel();
}
}
}
var url = hbButton.href + (hbButton.href.includes("?")?"&popup":"?popup")
xhr.open("GET", url);
xhr.responseType = "document";
xhr.send();
}
else {
showPanel();
}
}
}
})();
/* fossil.bootstrap.js *************************************************************/
"use strict";
(function () {
if(typeof window.CustomEvent === "function") return false;
window.CustomEvent = function(event, params) {
if(!params) params = {bubbles: false, cancelable: false, detail: null};
const evt = document.createEvent('CustomEvent');
evt.initCustomEvent( event, !!params.bubbles, !!params.cancelable, params.detail );
return evt;
};
})();
(function(global){
const F = global.fossil;
const timestring = function f(){
if(!f.rx1){
f.rx1 = /\.\d+Z$/;
}
const d = new Date();
return d.toISOString().replace(f.rx1,'').split('T').join(' ');
};
const localTimeString = function ff(d){
if(!ff.pad){
ff.pad = (x)=>(''+x).length>1 ? x : '0'+x;
}
d || (d = new Date());
return [
d.getFullYear(),'-',ff.pad(d.getMonth()+1),
'-',ff.pad(d.getDate()),
' ',ff.pad(d.getHours()),':',ff.pad(d.getMinutes()),
':',ff.pad(d.getSeconds())
].join('');
};
F.message = function f(msg){
const args = Array.prototype.slice.call(arguments,0);
const tgt = f.targetElement;
if(args.length) args.unshift(
localTimeString()+':'
);
if(tgt){
tgt.classList.remove('error');
tgt.innerText = args.join(' ');
}
else{
if(args.length){
args.unshift('Fossil status:');
console.debug.apply(console,args);
}
}
return this;
};
F.message.targetElement =
document.querySelector('#fossil-status-bar');
if(F.message.targetElement){
F.message.targetElement.addEventListener(
'dblclick', ()=>F.message(), false
);
}
F.error = function f(msg){
const args = Array.prototype.slice.call(arguments,0);
const tgt = F.message.targetElement;
args.unshift(timestring(),'UTC:');
if(tgt){
tgt.classList.add('error');
tgt.innerText = args.join(' ');
}
else{
args.unshift('Fossil error:');
console.error.apply(console,args);
}
return this;
};
F.encodeUrlArgs = function(obj,tgtArray,fakeEncode){
if(!obj) return '';
const a = (tgtArray instanceof Array) ? tgtArray : [],
enc = fakeEncode ? (x)=>x : encodeURIComponent;
let k, i = 0;
for( k in obj ){
if(i++) a.push('&');
a.push(enc(k),'=',enc(obj[k]));
}
return a===tgtArray ? a : a.join('');
};
F.repoUrl = function(path,urlParams){
if(!urlParams) return this.rootPath+path;
const url=[this.rootPath,path];
url.push('?');
if('string'===typeof urlParams) url.push(urlParams);
else if(urlParams && 'object'===typeof urlParams){
this.encodeUrlArgs(urlParams, url);
}
return url.join('');
};
F.isObject = function(v){
return v &&
(v instanceof Object) &&
('[object Object]' === Object.prototype.toString.apply(v) );
};
F.mergeLastWins = function(){
var k, o, i;
const n = arguments.length, rc={};
for(i = 0; i < n; ++i){
if(!F.isObject(o = arguments[i])) continue;
for( k in o ){
if(o.hasOwnProperty(k)) rc[k] = o[k];
}
}
return rc;
};
F.hashDigits = function(hash,forUrl){
const n = ('number'===typeof forUrl)
? forUrl : F.config[forUrl ? 'hashDigitsUrl' : 'hashDigits'];
return ('string'==typeof hash ? hash.substr(
0, n
) : hash);
};
F.onPageLoad = function(callback){
window.addEventListener('load', callback, false);
return this;
};
F.onDOMContentLoaded = function(callback){
window.addEventListener('DOMContentLoaded', callback, false);
return this;
};
F.shortenFilename = function(name){
const a = name.split('/');
if(a.length<=2) return name;
while(a.length>2) a.shift();
return '.../'+a.join('/');
};
F.page.addEventListener = function f(eventName, callback){
if(!f.proxy){
f.proxy = document.createElement('span');
}
f.proxy.addEventListener(eventName, callback, false);
return this;
};
F.page.dispatchEvent = function(eventName, eventDetail){
if(this.addEventListener.proxy){
try{
this.addEventListener.proxy.dispatchEvent(
new CustomEvent(eventName,{detail: eventDetail})
);
}catch(e){
console.error(eventName,"event listener threw:",e);
}
}
return this;
};
F.page.setPageTitle = function(title){
const t = document.querySelector('title');
if(t) t.innerText = title;
return this;
};
F.debounce = function f(func, wait, immediate) {
var timeout;
if(!wait) wait = f.$defaultDelay;
return function() {
const context = this, args = Array.prototype.slice.call(arguments);
const later = function() {
timeout = undefined;
if(!immediate) func.apply(context, args);
};
const callNow = immediate && !timeout;
clearTimeout(timeout);
timeout = setTimeout(later, wait);
if(callNow) func.apply(context, args);
};
};
F.debounce.$defaultDelay = 500;
})(window);
/* fossil.dom.js *************************************************************/
"use strict";
(function(F){
const argsToArray = (a)=>Array.prototype.slice.call(a,0);
const isArray = (v)=>v instanceof Array;
const dom = {
create: function(elemType){
return document.createElement(elemType);
},
createElemFactory: function(eType){
return function(){
return document.createElement(eType);
};
},
remove: function(e){
if(e.forEach){
e.forEach(
(x)=>x.parentNode.removeChild(x)
);
}else{
e.parentNode.removeChild(e);
}
return e;
},
clearElement: function f(e){
if(!f.each){
f.each = function(e){
if(e.forEach){
e.forEach((x)=>f(x));
return e;
}
while(e.firstChild) e.removeChild(e.firstChild);
};
}
argsToArray(arguments).forEach(f.each);
return arguments[0];
},
};
dom.splitClassList = function f(str){
if(!f.rx){
f.rx = /(\s+|\s*,\s*)/;
}
return str ? str.split(f.rx) : [str];
};
dom.div = dom.createElemFactory('div');
dom.p = dom.createElemFactory('p');
dom.code = dom.createElemFactory('code');
dom.pre = dom.createElemFactory('pre');
dom.header = dom.createElemFactory('header');
dom.footer = dom.createElemFactory('footer');
dom.section = dom.createElemFactory('section');
dom.span = dom.createElemFactory('span');
dom.strong = dom.createElemFactory('strong');
dom.em = dom.createElemFactory('em');
dom.ins = dom.createElemFactory('ins');
dom.del = dom.createElemFactory('del');
dom.label = function(forElem, text){
const rc = document.createElement('label');
if(forElem){
if(forElem instanceof HTMLElement){
forElem = this.attr(forElem, 'id');
}
dom.attr(rc, 'for', forElem);
}
if(text) this.append(rc, text);
return rc;
};
dom.img = function(src){
const e = this.create('img');
if(src) e.setAttribute('src',src);
return e;
};
dom.a = function(href,label){
const e = this.create('a');
if(href) e.setAttribute('href',href);
if(label) e.appendChild(dom.text(true===label ? href : label));
return e;
};
dom.hr = dom.createElemFactory('hr');
dom.br = dom.createElemFactory('br');
dom.text = function(){
return document.createTextNode(argsToArray(arguments).join(''));
};
dom.button = function(label,callback){
const b = this.create('button');
if(label) b.appendChild(this.text(label));
if('function' === typeof callback){
b.addEventListener('click', callback, false);
}
return b;
};
dom.textarea = function(){
const rc = this.create('textarea');
let rows, cols, readonly;
if(1===arguments.length){
if('boolean'===typeof arguments[0]){
readonly = !!arguments[0];
}else{
rows = arguments[0];
}
}else if(arguments.length){
rows = arguments[0];
cols = arguments[1];
readonly = arguments[2];
}
if(rows) rc.setAttribute('rows',rows);
if(cols) rc.setAttribute('cols', cols);
if(readonly) rc.setAttribute('readonly', true);
return rc;
};
dom.select = dom.createElemFactory('select');
dom.option = function(value,label){
const a = arguments;
var sel;
if(1==a.length){
if(a[0] instanceof HTMLElement){
sel = a[0];
}else{
value = a[0];
}
}else if(2==a.length){
if(a[0] instanceof HTMLElement){
sel = a[0];
value = a[1];
}else{
value = a[0];
label = a[1];
}
}
else if(3===a.length){
sel = a[0];
value = a[1];
label = a[2];
}
const o = this.create('option');
if(undefined !== value){
o.value = value;
this.append(o, this.text(label || value));
}else if(undefined !== label){
this.append(o, label);
}
if(sel) this.append(sel, o);
return o;
};
dom.h = function(level){
return this.create('h'+level);
};
dom.ul = dom.createElemFactory('ul');
dom.li = function(parent){
const li = this.create('li');
if(parent) parent.appendChild(li);
return li;
};
dom.createElemFactoryWithOptionalParent = function(childType){
return function(parent){
const e = this.create(childType);
if(parent) parent.appendChild(e);
return e;
};
};
dom.table = dom.createElemFactory('table');
dom.thead = dom.createElemFactoryWithOptionalParent('thead');
dom.tbody = dom.createElemFactoryWithOptionalParent('tbody');
dom.tfoot = dom.createElemFactoryWithOptionalParent('tfoot');
dom.tr = dom.createElemFactoryWithOptionalParent('tr');
dom.td = dom.createElemFactoryWithOptionalParent('td');
dom.th = dom.createElemFactoryWithOptionalParent('th');
dom.fieldset = function(legendText){
const fs = this.create('fieldset');
if(legendText){
this.append(
fs,
(legendText instanceof HTMLElement)
? legendText
: this.append(this.legend(legendText))
);
}
return fs;
};
dom.legend = function(legendText){
const rc = this.create('legend');
if(legendText) this.append(rc, legendText);
return rc;
};
dom.append = function f(parent){
const a = argsToArray(arguments);
a.shift();
for(let i in a) {
var e = a[i];
if(isArray(e) || e.forEach){
e.forEach((x)=>f.call(this, parent,x));
continue;
}
if('string'===typeof e
|| 'number'===typeof e
|| 'boolean'===typeof e
|| e instanceof Error) e = this.text(e);
parent.appendChild(e);
}
return parent;
};
dom.input = function(type){
return this.attr(this.create('input'), 'type', type);
};
dom.checkbox = function(value, checked){
const rc = this.input('checkbox');
if(1===arguments.length && 'boolean'===typeof value){
checked = !!value;
value = undefined;
}
if(undefined !== value) rc.value = value;
if(!!checked) rc.checked = true;
return rc;
};
dom.radio = function(){
const rc = this.input('radio');
let name, value, checked;
if(1===arguments.length && 'boolean'===typeof name){
checked = arguments[0];
name = value = undefined;
}else if(2===arguments.length){
name = arguments[0];
if('boolean'===typeof arguments[1]){
checked = arguments[1];
}else{
value = arguments[1];
checked = undefined;
}
}else if(arguments.length){
name = arguments[0];
value = arguments[1];
checked = arguments[2];
}
if(name) this.attr(rc, 'name', name);
if(undefined!==value) rc.value = value;
if(!!checked) rc.checked = true;
return rc;
};
const domAddRemoveClass = function f(action,e){
if(!f.rxSPlus){
f.rxSPlus = /\s+/;
f.applyAction = function(e,a,v){
if(!e || !v
) return;
else if(e.forEach){
e.forEach((E)=>E.classList[a](v));
}else{
e.classList[a](v);
}
};
}
var i = 2, n = arguments.length;
for( ; i < n; ++i ){
let c = arguments[i];
if(!c) continue;
else if(isArray(c) ||
('string'===typeof c
&& c.indexOf(' ')>=0
&& (c = c.split(f.rxSPlus)))
|| c.forEach
){
c.forEach((k)=>k ? f.applyAction(e, action, k) : false);
}else if(c){
f.applyAction(e, action, c);
}
}
return e;
};
dom.addClass = function(e,c){
const a = argsToArray(arguments);
a.unshift('add');
return domAddRemoveClass.apply(this, a);
};
dom.removeClass = function(e,c){
const a = argsToArray(arguments);
a.unshift('remove');
return domAddRemoveClass.apply(this, a);
};
dom.toggleClass = function f(e,c){
if(e.forEach){
e.forEach((x)=>x.classList.toggle(c));
}else{
e.classList.toggle(c);
}
return e;
};
dom.hasClass = function(e,c){
return (e && e.classList) ? e.classList.contains(c) : false;
};
dom.moveTo = function(dest,e){
const n = arguments.length;
var i = 1;
const self = this;
for( ; i < n; ++i ){
e = arguments[i];
this.append(dest, e);
}
return dest;
};
dom.moveChildrenTo = function f(dest,e){
if(!f.mv){
f.mv = function(d,v){
if(d instanceof Array){
d.push(v);
if(v.parentNode) v.parentNode.removeChild(v);
}
else d.appendChild(v);
};
}
const n = arguments.length;
var i = 1;
for( ; i < n; ++i ){
e = arguments[i];
if(!e){
console.warn("Achtung: dom.moveChildrenTo() passed a falsy value at argument",i,"of",
arguments,arguments[i]);
continue;
}
if(e.forEach){
e.forEach((x)=>f.mv(dest, x));
}else{
while(e.firstChild){
f.mv(dest, e.firstChild);
}
}
}
return dest;
};
dom.replaceNode = function f(old,nu){
var i = 1, n = arguments.length;
++f.counter;
try {
for( ; i < n; ++i ){
const e = arguments[i];
if(e.forEach){
e.forEach((x)=>f.call(this,old,e));
continue;
}
old.parentNode.insertBefore(e, old);
}
}
finally{
--f.counter;
}
if(!f.counter){
old.parentNode.removeChild(old);
}
};
dom.replaceNode.counter = 0;
dom.attr = function f(e){
if(2===arguments.length) return e.getAttribute(arguments[1]);
const a = argsToArray(arguments);
if(e.forEach){
e.forEach(function(x){
a[0] = x;
f.apply(f,a);
});
return e;
}
a.shift();
while(a.length){
const key = a.shift(), val = a.shift();
if(null===val || undefined===val){
e.removeAttribute(key);
}else{
e.setAttribute(key,val);
}
}
return e;
};
const enableDisable = function f(enable){
var i = 1, n = arguments.length;
for( ; i < n; ++i ){
let e = arguments[i];
if(e.forEach){
e.forEach((x)=>f(enable,x));
}else{
e.disabled = !enable;
}
}
return arguments[1];
};
dom.enable = function(e){
const args = argsToArray(arguments);
args.unshift(true);
return enableDisable.apply(this,args);
};
dom.disable = function(e){
const args = argsToArray(arguments);
args.unshift(false);
return enableDisable.apply(this,args);
};
dom.selectOne = function(x,origin){
var src = origin || document,
e = src.querySelector(x);
if(!e){
e = new Error("Cannot find DOM element: "+x);
console.error(e, src);
throw e;
}
return e;
};
dom.flashOnce = function f(e,howLongMs,afterFlashCallback){
if(e.dataset.isBlinking){
return;
}
if(2===arguments.length && 'function' ===typeof howLongMs){
afterFlashCallback = howLongMs;
howLongMs = f.defaultTimeMs;
}
if(!howLongMs || 'number'!==typeof howLongMs){
howLongMs = f.defaultTimeMs;
}
e.dataset.isBlinking = true;
const transition = e.style.transition;
e.style.transition = "opacity "+howLongMs+"ms ease-in-out";
const opacity = e.style.opacity;
e.style.opacity = 0;
setTimeout(function(){
e.style.transition = transition;
e.style.opacity = opacity;
delete e.dataset.isBlinking;
if(afterFlashCallback) afterFlashCallback();
}, howLongMs);
return e;
};
dom.flashOnce.defaultTimeMs = 400;
dom.flashOnce.eventHandler = (event)=>dom.flashOnce(event.target)
dom.flashNTimes = function(e,n,howLongMs,afterFlashCallback){
const args = argsToArray(arguments);
args.splice(1,1);
if(arguments.length===3 && 'function'===typeof howLongMs){
afterFlashCallback = howLongMs;
howLongMs = args[1] = this.flashOnce.defaultTimeMs;
}else if(arguments.length<3){
args[1] = this.flashOnce.defaultTimeMs;
}
n = +n;
const self = this;
const cb = args[2] = function f(){
if(--n){
setTimeout(()=>self.flashOnce(e, howLongMs, f),
howLongMs+(howLongMs*0.1));
}else if(afterFlashCallback){
afterFlashCallback();
}
};
this.flashOnce.apply(this, args);
return this;
};
dom.addClassBriefly = function f(e, className, howLongMs, afterCallback){
if(arguments.length<4 && 'function'===typeof howLongMs){
afterCallback = howLongMs;
howLongMs = f.defaultTimeMs;
}else if(arguments.length<3 || !+howLongMs){
howLongMs = f.defaultTimeMs;
}
this.addClass(e, className);
setTimeout(function(){
dom.removeClass(e, className);
if(afterCallback) afterCallback();
}, howLongMs);
return this;
};
dom.addClassBriefly.defaultTimeMs = 1000;
dom.copyTextToClipboard = function(text){
if( window.clipboardData && window.clipboardData.setData ){
window.clipboardData.setData('Text',text);
return true;
}else{
const x = document.createElement("textarea");
x.style.position = 'fixed';
x.value = text;
document.body.appendChild(x);
x.select();
var rc;
try{
document.execCommand('copy');
rc = true;
}catch(err){
rc = false;
}finally{
document.body.removeChild(x);
}
return rc;
}
};
dom.copyStyle = function f(e, style){
if(e.forEach){
e.forEach((x)=>f(x, style));
return e;
}
if(style){
let k;
for(k in style){
if(style.hasOwnProperty(k)) e.style[k] = style[k];
}
}
return e;
};
dom.effectiveHeight = function f(e){
if(!e) return 0;
if(!f.measure){
f.measure = function callee(e, depth){
if(!e) return;
const m = e.getBoundingClientRect();
if(0===depth){
callee.top = m.top;
callee.bottom = m.bottom;
}else{
callee.top = m.top ? Math.min(callee.top, m.top) : callee.top;
callee.bottom = Math.max(callee.bottom, m.bottom);
}
Array.prototype.forEach.call(e.children,(e)=>callee(e,depth+1));
if(0===depth){
f.extra += callee.bottom - callee.top;
}
return f.extra;
};
}
f.extra = 0;
f.measure(e,0);
return f.extra;
};
dom.parseHtml = function(){
let childs, string, tgt;
if(1===arguments.length){
string = arguments[0];
}else if(2==arguments.length){
tgt = arguments[0];
string  = arguments[1];
}
if(string){
const newNode = new DOMParser().parseFromString(string, 'text/html');
childs = newNode.documentElement.querySelector('body');
childs = childs ? Array.prototype.slice.call(childs.childNodes, 0) : [];
}else{
childs = [];
}
return tgt ? this.moveTo(tgt, childs) : childs;
};
F.connectPagePreviewers = function f(selector,methodNamespace){
if('string'===typeof selector){
selector = document.querySelectorAll(selector);
}else if(!selector.forEach){
selector = [selector];
}
if(!methodNamespace){
methodNamespace = F.page;
}
selector.forEach(function(e){
e.addEventListener(
'click', function(r){
const eTo = '#'===e.dataset.fPreviewTo[0]
? document.querySelector(e.dataset.fPreviewTo)
: methodNamespace[e.dataset.fPreviewTo],
eFrom = '#'===e.dataset.fPreviewFrom[0]
? document.querySelector(e.dataset.fPreviewFrom)
: methodNamespace[e.dataset.fPreviewFrom],
asText = +(e.dataset.fPreviewAsText || 0);
eTo.textContent = "Fetching preview...";
methodNamespace[e.dataset.fPreviewVia](
(eFrom instanceof Function ? eFrom.call(methodNamespace) : eFrom.value),
function(r){
if(eTo instanceof Function) eTo.call(methodNamespace, r||'');
else if(!r){
dom.clearElement(eTo);
}else if(asText){
eTo.textContent = r;
}else{
dom.parseHtml(dom.clearElement(eTo), r);
}
}
);
}, false
);
});
return this;
};
return F.dom = dom;
})(window.fossil);
/* fossil.pikchr.js *************************************************************/
(function(F){
"use strict";
const D = F.dom, P = F.pikchr = {};
P.addSrcView = function f(svg){
if(!f.hasOwnProperty('parentClick')){
f.parentClick = function(ev){
if(ev.altKey || ev.metaKey || ev.ctrlKey
|| this.classList.contains('toggle')){
this.classList.toggle('source');
ev.stopPropagation();
ev.preventDefault();
}
};
};
if(!svg) svg = 'svg.pikchr';
if('string' === typeof svg){
document.querySelectorAll(svg).forEach((e)=>f.call(this, e));
return this;
}else if(svg.forEach){
svg.forEach((e)=>f.call(this, e));
return this;
}
if(svg.dataset.pikchrProcessed){
return this;
}
svg.dataset.pikchrProcessed = 1;
const parent = svg.parentNode.parentNode;
const srcView = parent ? svg.parentNode.nextElementSibling : undefined;
if(!srcView || !srcView.classList.contains('pikchr-src')){
return this;
}
parent.addEventListener('click', f.parentClick, false);
return this;
};
})(window.fossil);
</script>
</body>
</html>
