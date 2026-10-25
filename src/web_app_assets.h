#pragma once
// Static assets of the full web config page: the stylesheet served at /app.css
// and the shell script sent once at the end of the page. Both live in flash and
// stream with sendFlash(), so neither ever needs a contiguous heap allocation.
// Header-only, with no includes, so tools/web_preview can build a static
// preview from exactly these bytes.
//
// Colours are CSS variables only. Their values come from the device's active
// theme (web_theme.cpp, written inline by sendFullHead()); the :root block here
// is Camillia Dark, used when the firmware cannot supply a palette. The old
// names (--text-dim, --accent-ink, --muted) stay as aliases because the
// embedded blocks (theme picker, files pane) still use them.

// The page's three families. Without internet the stacks in --sans/--mono/
// --brand fall back to system fonts, so nothing depends on this loading.
static const char kAppFontsLink[] =
    "<link rel='preconnect' href='https://fonts.gstatic.com' crossorigin>"
    "<link rel='stylesheet' href='https://fonts.googleapis.com/css2?family=IBM+Plex+Mono"
    "&family=IBM+Plex+Sans:wght@400;600&family=Martian+Mono:wght@500&display=swap'>";

static const char kAppCss[] = R"CSS(
:root{color-scheme:dark;--bg:#080818;--panel:#100c29;--panel-2:#181439;--panel-3:#2a1d44;--line:#2c2850;
--line-hi:#6a3a5a;--input:#181439;--accent:#de5073;--text:#f4f6fb;--dim:#b0b8c8;--on-accent:#ffffff;
--good:#4ad16b;--warn:#f5c542;--bad:#e8505b;
--text-dim:var(--dim);--accent-ink:var(--on-accent);--muted:var(--dim);
--sans:"IBM Plex Sans",system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
--mono:"IBM Plex Mono",ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;
--brand:"Martian Mono",ui-monospace,SFMono-Regular,Menlo,monospace;
--rail:200px;--hd:52px;--tabbar:58px}
*,*::before,*::after{box-sizing:border-box}
html{-webkit-text-size-adjust:100%;text-size-adjust:100%}
body{margin:0;font:15px/1.5 var(--sans);background:var(--bg);color:var(--text)}
a{color:var(--accent)}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
h2{font-size:1.15em;font-weight:600;margin:0 0 .15em}
h3{font-size:1em;font-weight:600;margin:1.2em 0 .45em;color:var(--text)}
p{margin:.5em 0}

/* Header */
.app-hd{position:sticky;top:0;z-index:30;height:var(--hd);display:flex;align-items:center;gap:.6em;
padding:0 16px;background:var(--panel);border-bottom:1px solid var(--line)}
.app-hd .nm{font:500 1.05em var(--brand);color:var(--accent);letter-spacing:-.01em}
.app-hd .ln{color:var(--dim);font-size:.9em;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0}
.app-hd .chips{margin-left:auto;display:flex;gap:.4em;align-items:center}
.logout{font-size:.88em;color:var(--dim);text-decoration:none;padding:.3em .5em}
.logout:hover{color:var(--accent)}

/* Chips */
.chip,.metric-chip{display:inline-flex;align-items:center;gap:.3em;padding:.2em .65em;border-radius:999px;
background:var(--panel-2);border:1px solid var(--line);color:var(--dim);font:.78em/1.3 var(--mono);white-space:nowrap}
.chip b{font-weight:400;color:var(--text)}
.chip-good,.metric-good{color:var(--good);border-color:var(--good)}
.chip-warn,.metric-warn{color:var(--warn);border-color:var(--warn)}
.chip-bad,.metric-bad{color:var(--bad);border-color:var(--bad)}
.key-chip{cursor:pointer;margin:0}
.key-chip:hover{color:var(--text);border-color:var(--accent)}
.key-chip.copied{color:var(--good);border-color:var(--good)}

/* Frame */
.app-main{padding:16px 16px calc(var(--tabbar) + 88px);max-width:880px;min-width:0}
.app-rail{display:none}
.tabbar{position:fixed;left:0;right:0;bottom:0;z-index:30;height:var(--tabbar);display:flex;
background:var(--panel);border-top:1px solid var(--line);padding-bottom:env(safe-area-inset-bottom)}
.tabbar a,.tabbar button{flex:1;margin:0;padding:6px 0 0;display:flex;flex-direction:column;align-items:center;justify-content:flex-start;gap:2px;
background:none;border:0;border-radius:0;color:var(--dim);font:600 .72em var(--sans);text-decoration:none;cursor:pointer}
.tabbar svg{width:22px;height:22px;stroke:currentColor;fill:none;stroke-width:1.8}
.tabbar .active{color:var(--accent)}
.secbar{position:sticky;top:var(--hd);z-index:20;display:flex;align-items:center;gap:.5em;margin:-16px -16px 12px;
padding:9px 16px;background:var(--panel-2);border-bottom:1px solid var(--line)}
.secbar b{font-weight:600}
.secbar button{margin:0 0 0 auto;padding:.3em .8em}
body:not([data-tab=config]) .secbar{display:none}
@media (min-width:720px){
.app{display:grid;grid-template-columns:var(--rail) minmax(0,1fr)}
.app-rail{display:block;position:sticky;top:var(--hd);height:calc(100vh - var(--hd));overflow:auto;
padding:12px 0 24px;background:var(--panel);border-right:1px solid var(--line)}
.app-rail a{display:block;padding:6px 18px;color:var(--text);font-weight:600;text-decoration:none}
.app-rail a.active{color:var(--accent);box-shadow:inset 3px 0 var(--accent)}
.app-rail .sec-link{padding:4px 18px 4px 30px;color:var(--dim);font-weight:400;font-size:.92em}
.app-rail .sec-link.here{color:var(--accent);box-shadow:inset 3px 0 var(--accent)}
.app-main{padding:20px 28px 96px}
.tabbar,.secbar{display:none}
}
.sec-link.dirty::after{content:"";display:inline-block;width:6px;height:6px;margin-left:.45em;
border-radius:50%;background:var(--accent);vertical-align:middle}

/* Sheets */
.scrim{position:fixed;inset:0;z-index:40;background:rgba(0,0,0,.5);display:none}
.scrim.open{display:block}
.sheet{position:fixed;left:0;right:0;bottom:0;z-index:41;max-height:75vh;overflow:auto;padding:8px 0 calc(16px + env(safe-area-inset-bottom));
background:var(--panel);border-top:1px solid var(--line-hi);border-radius:16px 16px 0 0;transform:translateY(105%);visibility:hidden}
.sheet.open{transform:none;visibility:visible}
.sheet::before{content:"";display:block;width:38px;height:4px;margin:0 auto 8px;border-radius:2px;background:var(--line)}
.sheet a{display:block;padding:12px 22px;color:var(--text);text-decoration:none}
.sheet a.here,.sheet a.active{color:var(--accent);font-weight:600}

/* Save bar */
.savebar{position:fixed;left:0;right:0;bottom:var(--tabbar);z-index:25;display:flex;align-items:center;gap:.6em;
padding:10px 16px;background:var(--panel-2);border-top:2px solid var(--accent);transform:translateY(130%);visibility:hidden}
.savebar.show{transform:none;visibility:visible}
.savebar #save-count{margin-right:auto;font-size:.9em}
.savebar button{margin:0}
@media (min-width:720px){.savebar{left:var(--rail);bottom:0}}
@media (prefers-reduced-motion:no-preference){
.sheet,.savebar{transition:transform .2s ease,visibility .2s}}

/* Panes and sections */
.tab-panel{display:none}
.tab-panel.active{display:block}
.tab-pane-center{max-width:none}
.cfg-sec{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin:0 0 14px;
scroll-margin-top:calc(var(--hd) + 56px)}
.cfg-sec>h2{margin:0}
.sec-desc{margin:.1em 0 .6em;color:var(--dim);font-size:.88em}
.cfg-sub{font-size:.95em;margin:1.1em 0 .3em;padding-top:.8em;border-top:1px solid var(--line)}
.hint,.gps-hint{font-size:.82em;color:var(--dim);margin:.15em 0 .6em}
.gps-note{margin:.4em 0;font-size:.9em}
.gps-note button{padding:.2em .8em;font-size:.9em;margin:0 0 0 .4em}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:0 .9em}

/* Fields: a label wraps its control. A text-plus-control label is a row on
   wide screens (the text node is the first grid cell) and stacks on narrow. */
label{display:block;margin:.55em 0 .15em;font-size:.92em}
input[type=text],input[type=number],input[type=password],input[type=email],input[type=url],select,textarea{
width:100%;margin-top:.2em;padding:.5em .6em;font:inherit;font-size:.95em;color:var(--text);background:var(--input);
border:1px solid var(--line);border-radius:8px}
option{background:var(--panel-2);color:var(--text)}
input[readonly]{background:var(--panel);color:var(--dim)}
input[type=range]{width:100%;accent-color:var(--accent)}
.changed{border-color:var(--accent)!important;box-shadow:0 0 0 2px color-mix(in srgb,var(--accent) 30%,transparent)}
@media (min-width:720px){@supports selector(:has(*)){
.cfg-sec label:has(>input:not([type=checkbox],[type=radio],[type=hidden]),>select,>textarea){
display:grid;grid-template-columns:minmax(9em,38%) minmax(0,1fr);align-items:center;gap:0 12px}
.cfg-sec label:has(>input:not([type=checkbox],[type=radio],[type=hidden]),>select,>textarea)>*{margin-top:0}
.cfg-sec .row2 label:has(>input,>select){grid-template-columns:minmax(6em,40%) minmax(0,1fr)}
.cfg-sec .ch-fields label:has(>input,>select){display:block}}}

/* Checkbox as a toggle. Still a real checkbox: same name, same POST. */
input[type=checkbox]{appearance:none;-webkit-appearance:none;flex:none;width:34px;height:20px;margin:0 .55em 0 0;
vertical-align:middle;position:relative;border-radius:10px;background:var(--panel-3);border:1px solid var(--line);cursor:pointer}
input[type=checkbox]::after{content:"";position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;
background:var(--dim)}
input[type=checkbox]:checked{background:var(--accent);border-color:var(--accent)}
input[type=checkbox]:checked::after{left:16px;background:var(--on-accent)}
label:has(>input[type=checkbox]){display:flex;align-items:center}

/* Buttons. An unclassed button is a primary one, so markup that predates the
   classes still looks right. */
button,.btn{display:inline-flex;align-items:center;justify-content:center;gap:.4em;margin:.8em .4em 0 0;padding:.55em 1.2em;
font:600 .92em var(--sans);color:var(--on-accent);background:var(--accent);border:1px solid var(--accent);border-radius:8px;
cursor:pointer;text-decoration:none}
button:disabled{opacity:.55;cursor:default}
.btn-2{color:var(--text);background:transparent;border-color:var(--line)}
.btn-2:hover{border-color:var(--accent)}
.btn-danger{color:#fff;background:var(--bad);border-color:var(--bad)}
.notice,.msg{margin:.6em 0;padding:.55em .8em;border-radius:8px;border:1px solid var(--accent);background:var(--panel-2)}
.notice-err,.err{margin:.6em 0;padding:.55em .8em;border-radius:8px;border:1px solid var(--bad);color:var(--text);
background:color-mix(in srgb,var(--bad) 14%,var(--panel))}
details{border:1px solid var(--line);border-radius:8px;margin:.8em 0;padding:0 .8em;background:var(--panel-2)}
details[open]{padding-bottom:.8em}
summary{font-weight:600;cursor:pointer;padding:.5em 0}

/* Modals and choosers */
.chooser-btn{width:100%;margin:.1em 0 0;justify-content:flex-start;padding:.5em .6em;background:var(--input);
color:var(--text);border:1px solid var(--line);font-weight:600}
.modal-back{display:none;position:fixed;inset:0;background:rgba(0,0,0,.55);z-index:50;align-items:center;justify-content:center}
.modal-back.open{display:flex}
.modal-card{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:1em;width:min(90vw,340px);
box-shadow:0 12px 32px rgba(0,0,0,.45)}
.modal-card h3{margin:.1em 0 .6em;color:var(--accent)}
.modal-opt{width:100%;margin:.35em 0;padding:.6em;display:block;text-align:left;background:var(--panel-2);color:var(--text);
border:1px solid var(--line);font-weight:400}
.modal-opt.sel{border-color:var(--accent)}
.modal-opt b{display:block;font-weight:600}
.modal-opt span{display:block;font-size:.8em;color:var(--dim)}

/* Channels */
.ch-row{border:1px solid var(--line);border-radius:8px;background:var(--bg);padding:.5em .7em;margin:.5em 0}
.ch-row label{margin:0;font-size:.86em}
.ch-hd{display:flex;align-items:center;gap:.35em;margin-bottom:.4em}
.ch-no{font-weight:600;font-size:.86em;color:var(--accent);margin-right:auto}
.ch-fields{display:grid;grid-template-columns:1fr 2fr 1fr;gap:.4em;align-items:end}
.ch-tog{display:flex;flex-wrap:wrap;gap:.2em 1em;margin-top:.5em}
.ch-btn,.ch-clear{margin:0;padding:.3em .6em;font-size:.8em}
.ch-btn{background:var(--panel-2);color:var(--text);border-color:var(--line)}
.ch-btn[disabled]{opacity:.35}
.ch-clear{background:var(--bad);border-color:var(--bad);color:#fff}

/* Remote */
.remote-toggle{display:flex;align-items:center;gap:.55em;margin:.7em 0}
.remote-status{font-size:.84em;color:var(--dim);margin:.35em 0 .6em}
.remote-frame{display:block;width:100%;height:min(72vh,520px);border:1px solid var(--line);border-radius:8px;background:#000}
.remote-lock{display:flex;flex-wrap:wrap;align-items:center;gap:.5em;margin:.4em 0}
.remote-lock input{width:7.5em;font-size:1.2em;letter-spacing:.2em;text-align:center}

/* Nodes and map */
.map-wrap,.live-wrap{margin-top:.6em;border:1px solid var(--line);border-radius:12px;background:var(--panel);padding:.6em}
.map-canvas{display:block;width:100%;height:340px;border-radius:8px;overflow:hidden;background:#08141f}
.map-controls{display:flex;gap:.45em;flex-wrap:wrap;margin:.15em 0 .45em}
.map-mini-btn{margin:0;padding:.35em .8em;font-size:.82em;background:var(--panel-2);color:var(--text);border-color:var(--line);border-radius:999px}
.map-mini-right{margin-left:auto}
.map-mini-btn.active{background:var(--accent);color:var(--on-accent);border-color:var(--accent)}
.map-legend{display:flex;justify-content:space-between;gap:.5em;font-size:.8em;color:var(--dim);margin-top:.35em}
.node-list{margin-top:.8em;display:grid;gap:.5em}
.node-card{border:1px solid var(--line);border-radius:10px;background:var(--panel);padding:.6em .75em}
.node-head{display:flex;align-items:flex-start;justify-content:space-between;gap:.6em}
.node-title{font-weight:600;color:var(--accent);margin-bottom:.2em}
.node-meta{font-size:.84em;color:var(--dim);margin:.15em 0;word-break:break-word}
.node-meta b{color:var(--text);font-weight:600}
.fav-tag{margin-left:.4em;font-weight:600;font-size:.78em;color:var(--dim)}
.fav-btn{flex:none;white-space:nowrap}
.fav-btn[data-fav='1']{border-color:var(--accent);color:var(--accent)}
.leaflet-container{font:12px/1.2 var(--sans)}
.leaflet-control-attribution{font-size:.65em}

/* Chat */
.chat-feed{margin-top:.6em;height:min(60vh,420px);overflow:auto;border:1px solid var(--line);border-radius:12px;
background:var(--bg);padding:.6em;display:flex;flex-direction:column;gap:.45em}
.chat-msg{border:1px solid var(--line);border-radius:10px;background:var(--panel);padding:.45em .6em;max-width:88%}
.chat-msg.mine{align-self:flex-end;background:var(--panel-2);border-color:var(--accent)}
.chat-body{font-size:.92em;white-space:pre-wrap;word-break:break-word}
.chat-actions{margin-top:.3em;display:flex;flex-wrap:wrap;gap:.25em;align-items:center}
.chat-tap,.chat-mini{margin:0;padding:.1em .5em;font-size:.8em;font-weight:400;background:var(--panel-2);color:var(--text);
border-color:var(--line);border-radius:999px}
.chat-reply{margin-top:.5em;display:flex;align-items:center;gap:.5em;font-size:.84em;color:var(--dim);
border-left:3px solid var(--accent);padding:.25em .55em;background:var(--panel)}
.chat-reply span{flex:1;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chat-compose{margin-top:.5em;display:flex;flex-direction:column;gap:.45em}
.chat-compose textarea{min-height:4.8em;resize:vertical;line-height:1.35}
.chat-compose-foot{display:flex;align-items:center;gap:.5em}
.chat-char-count{font-size:.78em;color:var(--dim);margin-right:auto}
.chat-compose button{margin:0}

/* Live feed and charts. The line colours encode packet types, not theme. */
.live-toolbar{display:flex;justify-content:space-between;align-items:center;gap:.5em;margin-bottom:.45em}
.live-toolbar span{font-size:.72em;color:var(--dim)}
.live-feed{height:min(60vh,420px);overflow:auto;border:1px solid var(--line);border-radius:8px;background:var(--bg)}
.chart-wrap{margin-top:.6em;display:grid;gap:.7em}
.chart-box{border:1px solid var(--line);border-radius:12px;background:var(--panel);padding:.6em .75em}
.chart-head{display:flex;justify-content:space-between;align-items:baseline;gap:.5em;margin-bottom:.35em;font-size:.86em}
.chart-stats{font:.74em var(--mono);color:var(--dim)}
.chart-canvas{width:100%;height:180px;display:block;background:var(--bg);border:1px solid var(--line);border-radius:8px}
.chart-legend{margin-top:.3em;font-size:.74em;color:var(--dim)}
.chart-legend .swatch{display:inline-block;width:.8em;height:.8em;margin-right:.25em;vertical-align:middle;border-radius:2px}
.sw-chutil{background:#67d8ff}.sw-airutil{background:#ffd56b}.sw-snr{background:#9ceadf}.sw-rssi{background:#f7b46d}
.live-line{padding:.13em .4em;border-bottom:1px solid rgba(255,255,255,.08);font:.72em/1.25 var(--mono);color:#dfe7ef;background:#10254a}
.live-line.live-default{background:#10254a;color:#dfe7ef}
.live-line.live-err{background:#4a1d1d;color:#ff8f8f;font-weight:700}
.live-line.live-tx-ack{background:#1e3e27;color:#89e7a5}
.live-line.live-rx-ack{background:#1b3e34;color:#7de8d2}
.live-line.live-tx-text{background:#4a4318;color:#ffd56b}
.live-line.live-tx-node{background:#4a2d1f;color:#f7b46d}
.live-line.live-tx-pos{background:#4a3418;color:#ffc875}
.live-line.live-tx-tlm{background:#1a3b40;color:#84efff}
.live-line.live-tx-dm{background:#33224a;color:#f2a4ff}
.live-line.live-rx-text{background:#12345d;color:#67d8ff}
.live-line.live-rx-node{background:#1d2e58;color:#9ec3ff}
.live-line.live-rx-pos{background:#1a3754;color:#b6e5ff}
.live-line.live-rx-tlm{background:#1a3a3e;color:#9ceadf}
.live-line.live-rx-enc{background:#4a3618;color:#ffbe73}
.live-line.live-rx-other{background:#102d52;color:#8fbfff}
.live-line.live-tx-other{background:#3e3619;color:#e7c96f}

@media (max-width:719px){
.row2,.ch-fields{grid-template-columns:1fr}
button,.btn,.tabbar a{min-height:40px}
.app-hd{overflow:hidden}
.app-hd .ln,.app-hd .key-chip,.app-hd .logout{display:none}}
)CSS";

static const char kAppJs[] = R"JS(
(function(){
var W=window,D=document;
function all(s,r){return Array.prototype.slice.call((r||D).querySelectorAll(s));}
function byId(i){return D.getElementById(i);}
function ss(v){try{if(v===undefined)return sessionStorage.getItem('wcLoc');
if(v===null)sessionStorage.removeItem('wcLoc');else sessionStorage.setItem('wcLoc',v);}catch(e){}return null;}
function setHash(h){if(location.hash!==h){try{history.replaceState(null,'',h);}catch(e){}}}
var calm=W.matchMedia&&W.matchMedia('(prefers-reduced-motion: reduce)').matches;

/* Tabs: the rail, the phone tab bar and the More sheet all carry data-tab. */
W.appTab=function(tab){
if(!byId('tab-'+tab))tab='config';
D.body.setAttribute('data-tab',tab);
all('a[data-tab],button[data-tab]').forEach(function(e){
var on=e.getAttribute('data-tab')===tab;e.classList.toggle('active',on);
if(on)e.setAttribute('aria-current','page');else e.removeAttribute('aria-current');});
all('.tab-panel').forEach(function(p){p.classList.toggle('active',p.id==='tab-'+tab);});
if(tab!=='config'||location.hash.indexOf('#config')!==0)setHash('#'+tab);
};
function go(tab){if(typeof W.switchTab==='function')W.switchTab(tab);else W.appTab(tab);}

/* Sheets */
W.appOpenSheet=function(id){var s=byId(id);if(!s)return;W.appCloseSheets();
s.classList.add('open');var c=byId('scrim');if(c)c.classList.add('open');};
W.appCloseSheets=function(){all('.sheet.open,.scrim.open').forEach(function(e){e.classList.remove('open');});};

/* Section nav, built from the Config pane's sections. */
function cfgSecs(){return all('#tab-config .cfg-sec');}
function slugOf(sec){return sec.id.replace(/^sec-/,'');}
W.appBuildSectionNav=function(){
['rail-secs','sheet-secs'].forEach(function(id){var box=byId(id);if(!box)return;box.textContent='';
cfgSecs().forEach(function(sec){var h=sec.querySelector('h2');var a=D.createElement('a');
a.className='sec-link';a.href='#config/'+slugOf(sec);a.setAttribute('data-sec',slugOf(sec));
a.textContent=h?h.textContent:slugOf(sec);box.appendChild(a);});});
};
function markHere(slug){
all('.sec-link').forEach(function(a){a.classList.toggle('here',a.getAttribute('data-sec')===slug);});
var sec=byId('sec-'+slug),cur=byId('sec-current'),h=sec&&sec.querySelector('h2');
if(cur&&h)cur.textContent=h.textContent;
}
function showSec(slug,smooth){
var sec=byId('sec-'+slug);if(!sec)return;
if(D.body.getAttribute('data-tab')!=='config')go('config');
sec.scrollIntoView({behavior:smooth&&!calm?'smooth':'auto',block:'start'});
markHere(slug);setHash('#config/'+slug);
}
function spy(){
if(!('IntersectionObserver' in W))return;
var io=new IntersectionObserver(function(es){
if(D.body.getAttribute('data-tab')!=='config')return;
es.forEach(function(e){if(e.isIntersecting){var s=slugOf(e.target);markHere(s);setHash('#config/'+s);}});
},{rootMargin:'0px 0px -70% 0px'});
cfgSecs().forEach(function(s){io.observe(s);});
}
D.addEventListener('click',function(e){
var a=e.target.closest&&e.target.closest('a.sec-link,a[data-tab]');
if(!a)return;
e.preventDefault();W.appCloseSheets();
if(a.classList.contains('sec-link'))showSec(a.getAttribute('data-sec'),true);
else{go(a.getAttribute('data-tab'));W.scrollTo(0,0);}
});
D.addEventListener('keydown',function(e){if(e.key==='Escape')W.appCloseSheets();});

/* Where you were: the hash, or what the form stored before a save reloaded
   the page at /. */
W.appRestoreLocation=function(){
var loc=ss()||location.hash||'';ss(null);
var m=/^#([a-z]+)(?:\/([a-z0-9-]+))?/.exec(loc);
if(!m){W.appTab('config');return;}
go(m[1]);
if(m[1]==='config'&&m[2])setTimeout(function(){showSec(m[2],false);},0);
};

/* Unsaved changes. Every named control in the save form is compared with its
   value when the page finished loading. Hidden inputs count: the theme, font
   and blink pickers keep their choice in one. The theme builder (.tb-back)
   sits inside the form but is never saved with it. */
var F=null,count=0;
function tracked(el){
if(!el.name||el.closest('.tb-back'))return false;
var t=(el.type||'').toLowerCase();
return t!=='file'&&t!=='submit'&&t!=='button'&&t!=='reset'&&t!=='image';
}
function snap(el){
var t=(el.type||'').toLowerCase();
if(t==='checkbox'||t==='radio')return el.checked?'1':'0';
if(el.tagName==='SELECT')return String(el.selectedIndex);
return el.value;
}
function defSnap(el){
var t=(el.type||'').toLowerCase();
if(t==='checkbox'||t==='radio')return el.defaultChecked?'1':'0';
if(el.tagName==='SELECT'){for(var i=0;i<el.options.length;i++)if(el.options[i].defaultSelected)return String(i);return '0';}
return el.defaultValue;
}
function controls(){return F?Array.prototype.filter.call(F.elements,tracked):[];}
function recount(){
if(!F)return;
var dirty={};count=0;
controls().forEach(function(el){
if(el._wcBase===undefined)el._wcBase=defSnap(el);
var ch=snap(el)!==el._wcBase;
if(el.type!=='hidden')el.classList.toggle('changed',ch);
if(ch){count++;var s=el.closest('.cfg-sec');if(s)dirty[slugOf(s)]=1;}
});
all('.sec-link').forEach(function(a){a.classList.toggle('dirty',!!dirty[a.getAttribute('data-sec')]);});
var bar=byId('savebar'),n=byId('save-count');
if(n)n.textContent=count===1?'1 unsaved change':count+' unsaved changes';
if(bar)bar.classList.toggle('show',count>0);
}
W.appRebaseline=function(){controls().forEach(function(el){el._wcBase=snap(el);});recount();};
W.appTrackForm=function(f){
F=f;if(!F)return;
W.appRebaseline();
F.addEventListener('input',recount);F.addEventListener('change',recount);
F.addEventListener('submit',function(e){if(e.defaultPrevented)return;count=0;ss(location.hash||'#config');});
var d=byId('save-discard');
if(d)d.addEventListener('click',function(){
controls().forEach(function(el){if(snap(el)===el._wcBase)return;
var t=(el.type||'').toLowerCase();
if(t==='checkbox'||t==='radio')el.checked=el._wcBase==='1';
else if(el.tagName==='SELECT')el.selectedIndex=+el._wcBase;
else el.value=el._wcBase;
el.dispatchEvent(new Event('change',{bubbles:true}));});
recount();});
D.addEventListener('click',function(){setTimeout(recount,0);});
};
W.addEventListener('beforeunload',function(e){if(count>0){e.preventDefault();e.returnValue='';}});

D.addEventListener('DOMContentLoaded',function(){
var c=byId('scrim');if(c)c.addEventListener('click',W.appCloseSheets);
W.appBuildSectionNav();
W.appRestoreLocation();
spy();
// One tick later, after the page's own DOMContentLoaded handlers have set
// the values they fill in by script (region, preset, channel fields).
setTimeout(function(){W.appTrackForm(byId('cfg-form'));},0);
});
})();
)JS";
